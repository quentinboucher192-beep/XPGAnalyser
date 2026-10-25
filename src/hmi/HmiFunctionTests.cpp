#include "HmiFunctionTests.hpp"

#include "../sim/Runtime.hpp"
#include "../sim/ValueText.hpp"     // trueWord / falseWord (les memes mots partout, 1.12.19)
#include "../domain/ProjectModel.hpp"

#include "HmiCheck.hpp"             // Issue (les controles de Generer)
#include "HmiDecl.hpp"             // decl::codeOf
#include "HmiExpr.hpp"             // Expression, cellValue n'est pas ici
#include "HmiRuntime.hpp"
#include "HmiScript.hpp"           // splitDeclarations, LocalVar
#include "HmiSymbols.hpp"          // cellValue (la regle des cases)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Sans casse ni accents (e accentue -> e...) - pour JOURNAL et les comparaisons de texte.
std::string folded(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            ++i;
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            else if ((d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C)) out += 'u';
            else if ((d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F)) out += 'i';
            else if ((d >= 0xB2 && d <= 0xB6) || (d >= 0x92 && d <= 0x96)) out += 'o';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) { return folded(a) == folded(b); }

std::string stripQuotes(std::string s) {
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

bool isRealType(std::string_view t) { return iequals(t, "REAL") || iequals(t, "LREAL"); }

std::string qmark(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

std::string seconds(double s) {
    char b[32];
    std::snprintf(b, sizeof b, s < 10 ? "%.3f s" : "%.1f s", s);
    std::string out = b;
    for (auto& c : out) if (c == '.') c = ',';
    return out;
}

// La valeur montree : un REAL garde sa virgule (18.0, pas 18), une chaine ses apostrophes.
std::string valueText(const sim::Value& v) {
    if (v.type() == sim::Type::Real) {
        const double x = v.asReal();
        char b[48];
        if (std::isfinite(x) && std::fabs(x) < 1e15 && std::floor(x) == x) std::snprintf(b, sizeof b, "%.1f", x);
        else std::snprintf(b, sizeof b, "%.6g", x);
        return b;
    }
    if (v.type() == sim::Type::String) return "'" + v.asString() + "'";
    return formatValue(v);
}

// L'AUTOMATE DU BANC : celui de la simulation s'il tourne, en lecture seule ;
// sinon un simulateur vide, pour les fonctions standard (calque sur l'Essai).
class TrialPlc final : public sim::Environment {
public:
    explicit TrialPlc(sim::Environment* live) : live_(live) {
        if (!live_) standard_ = std::make_unique<sim::Runtime>(std::make_shared<const domain::Project>());
    }
    bool read(std::string_view n, sim::Value& out) override { return live_ && live_->read(n, out); }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view n) override { return live_ && live_->exists(n); }
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) override {
        if (!isStandardFunction(name)) return false;
        return live_ ? live_->call(name, instance, args, result) : standard_->call(name, instance, args, result);
    }
    void report(sim::Diagnostic) override {}
private:
    sim::Environment*             live_;
    std::unique_ptr<sim::Runtime> standard_;
};

// Le code E_* en tete du message d'arret d'une erreur levee (LEVER E_Niveau ...).
// Le message a la forme "fonction X : ligne N : E_Niveau : ...". Vide : aucun code lisible
// (une erreur du moteur, dont le message est deja francais : on retombe sur le texte).
std::string errorCodeOf(std::string_view why) {
    for (std::size_t i = 0; i + 1 < why.size(); ++i)
        if (why[i] == 'E' && why[i + 1] == '_' && (i == 0 || !std::isalnum(static_cast<unsigned char>(why[i - 1])))) {
            std::size_t j = i;
            while (j < why.size() && (std::isalnum(static_cast<unsigned char>(why[j])) || why[j] == '_')) ++j;
            return std::string(why.substr(i, j - i));
        }
    return {};
}

// Le message d'arret sans le prefixe "fonction X : " (plus lisible dans le detail).
std::string cleanWhy(std::string_view why) {
    std::string s = trim(why);
    if (const auto at = s.find(" : "); at != std::string::npos && s.rfind("fonction ", 0) == 0) s = s.substr(at + 3);
    return s;
}

// Compare la valeur obtenue a l'attendu (texte). `real` : le type est REAL/LREAL (la tolerance joue,
// vide = egalite stricte). Les autres types : egalite exacte (sim::Value). `note` : "obtenu X, attendu Y".
struct Compared { bool ok{false}; std::string note; };
Compared compareValue(const sim::Value& got, const std::string& expectedRaw, const std::string& tolRaw, bool real) {
    const std::string expected = stripQuotes(trim(expectedRaw));
    const std::string gotText = valueText(got);
    double b = 0;
    const bool expNum = parseNumber(expected, b);
    if (real && expNum) {
        const double a = got.asReal();
        double tol = 0;
        (void)parseNumber(trim(tolRaw), tol);
        const double ecart = std::fabs(a - b);
        char buf[160];
        std::snprintf(buf, sizeof buf, "obtenu %s, attendu %s (\xC3\xA9" "cart %.6g)", gotText.c_str(), expected.c_str(), ecart);
        return {ecart <= tol, buf};
    }
    const std::string note = "obtenu " + gotText + ", attendu " + expected;
    if (got.type() == sim::Type::Bool) {
        const std::string e = folded(expected);
        const bool truthy = got.isTruthy();
        if (sim::trueWord(e)) return {truthy, note};
        if (sim::falseWord(e)) return {!truthy, note};
        return {false, note};
    }
    double a = 0;
    if (expNum && parseNumber(stripQuotes(gotText), a))
        return {std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b)), note};
    return {folded(stripQuotes(gotText)) == folded(expected), note};
}

} // namespace

// ============================================================ le rapport ======
std::size_t FunctionTestReport::count(Verdict v) const {
    return static_cast<std::size_t>(std::count_if(results.begin(), results.end(),
                                                  [v](const FunctionTestResult& r) { return r.verdict == v; }));
}
bool FunctionTestReport::ok() const {
    return !results.empty() && count(Verdict::Failed) == 0 && count(Verdict::Error) == 0;
}
bool FunctionTestReport::failed() const { return count(Verdict::Failed) + count(Verdict::Error) > 0; }

std::string FunctionTestReport::summary() const {
    if (results.empty()) return "aucun cas";
    std::string s = std::to_string(results.size()) + " cas : " + std::to_string(count(Verdict::Passed)) + " r\xC3\xA9ussi(s)";
    if (const auto f = count(Verdict::Failed)) s += ", " + std::to_string(f) + " \xC3\xA9" "chec(s)";
    if (const auto e = count(Verdict::Error)) s += ", " + std::to_string(e) + " erreur(s)";
    if (const auto k = count(Verdict::Skipped)) s += ", " + std::to_string(k) + " non jou\xC3\xA9(s)";
    return s;
}

// ============================================================ le moteur =======
FunctionTestResult runFunctionTest(const Project& p, const HmiFunction& f, const FunctionTest& test, sim::Environment* plc) {
    FunctionTestResult res;
    res.test = test.id;
    res.name = test.name;
    if (!test.enabled) {
        res.verdict = Verdict::Skipped;
        return res;
    }
    const auto t0 = std::chrono::steady_clock::now();
    TrialPlc trial(plc);
    Runtime rt;
    rt.bind(&p, &trial);
    rt.prime(0.0);

    // 1) Poser avant : chaque pose ECRIT sa variable, apres la remise a l'initiale et avant l'appel
    // (pas un forcage : la fonction doit pouvoir la changer ensuite). La valeur se lit comme toute
    // case (la regle des cases) dans le type de la cible. Une variable objet (LIST, MAP...) passe par
    // setRichVariable ; une scalaire, une case, un membre, par une ecriture simple.
    const auto poseFail = [&](const std::string& target, const std::string& why) {
        res.verdict = Verdict::Error;
        res.detail = "pose " + qmark(target) + " : " + (why.empty() ? std::string("refus\xC3\xA9" "e") : why);
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    for (const auto& g : test.givens) {
        const std::string target = trim(g.target);
        if (target.empty()) continue;
        std::string valueText = trim(g.value);
        if (const Variable* var = p.variable(target)) {                 // la regle des cases, dans le type connu
            const auto cell = cellValue(&p, var->type, valueText);
            if (!cell.ok()) { poseFail(target, cell.why); return res; }
            valueText = cell.text;
        }
        std::string why;
        if (rt.richVariable(target) != nullptr) {                       // une variable objet : un litteral
            if (!rt.setRichVariable(target, valueText, &why)) { poseFail(target, why); return res; }
            continue;
        }
        const auto e = Expression::compile(valueText);
        if (!e.valid()) { poseFail(target, valueText + " illisible"); return res; }
        auto v = e.evaluate(rt.environment());
        if (!v) { poseFail(target, valueText + " ne s'\xC3\xA9value pas"); return res; }
        if (!rt.environment().write(target, *v)) { poseFail(target, "variable inconnue ou en lecture seule"); return res; }
    }

    // 2) Les arguments, un par parametre (vide : la valeur par defaut), nommes (pas de collision d'ordre).
    const auto parts = splitDeclarations(decl::codeOf(f), true);
    const auto inputs = parts.parameters();
    std::vector<std::pair<std::string, sim::Value>> args;
    std::string shown;
    for (std::size_t k = 0; k < test.arguments.size(); ++k) {
        const std::string text = trim(test.arguments[k]);
        if (text.empty()) continue;
        if (k >= inputs.size()) {
            res.verdict = Verdict::Error;
            res.detail = "trop d'arguments (" + std::to_string(test.arguments.size()) + " pour " + std::to_string(inputs.size()) + ")";
            res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return res;
        }
        if (inputs[k]->section == LocalVar::Section::Output) continue;
        const auto cell = cellValue(&p, inputs[k]->type, text);
        if (!cell.ok()) {
            res.verdict = Verdict::Error;
            res.detail = inputs[k]->name + " : " + cell.why;
            res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return res;
        }
        const auto e = Expression::compile(cell.text);
        if (!e.valid()) {
            res.verdict = Verdict::Error;
            res.detail = inputs[k]->name + " : " + text + " illisible";
            res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return res;
        }
        auto v = e.evaluate(rt.environment());
        if (!v) {
            res.verdict = Verdict::Error;
            res.detail = inputs[k]->name + " : " + text + " ne s'\xC3\xA9value pas";
            res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return res;
        }
        args.emplace_back(inputs[k]->name, *v);
        shown += (shown.empty() ? "" : ", ") + cell.text;
    }
    res.call = f.name + "(" + shown + ")";

    // 3) L'appel, comme l'Essai.
    sim::Value result;
    std::string why;
    std::vector<std::pair<std::string, sim::Value>> outputs;
    const bool ran = rt.runFunction(f.id, args, result, &why, &outputs);

    // 4) Le verdict.
    if (!test.errorCode.empty()) {
        // Le cas ATTEND une erreur.
        if (ran) {
            res.verdict = Verdict::Failed;
            res.detail = "aucune erreur lev\xC3\xA9" "e, attendu " + test.errorCode;
        } else {
            const std::string code = errorCodeOf(why);
            const bool match = (!code.empty() && iequals(code, test.errorCode))
                            || folded(why).find(folded(test.errorCode)) != std::string::npos;
            res.verdict = match ? Verdict::Passed : Verdict::Failed;
            res.detail = match ? "l\xC3\xA8ve " + cleanWhy(why)
                               : "attendu " + test.errorCode + ", obtenu " + cleanWhy(why);
        }
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }
    if (!ran) {
        // Une erreur alors qu'aucune n'etait attendue : verdict erreur.
        res.verdict = Verdict::Error;
        res.detail = cleanWhy(why);
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // Le retour, puis les verifs : toutes doivent passer.
    Verdict verdict = Verdict::Passed;
    std::string detail;
    if (!f.returnType.empty()) res.got = valueText(result);
    if (!f.returnType.empty() && !trim(test.expected).empty()) {
        const auto cmp = compareValue(result, test.expected, test.tolerance, isRealType(f.returnType));
        detail = cmp.note;
        if (!cmp.ok) verdict = Verdict::Failed;
    }
    if (verdict == Verdict::Passed) {
        for (const auto& c : test.checks) {
            const std::string target = trim(c.target);
            if (target.empty()) continue;
            if (iequals(target, "JOURNAL")) {
                const std::string want = folded(stripQuotes(trim(c.expected)));
                bool seen = false;
                for (const auto& j : rt.journal())
                    if (folded(j.message).find(want) != std::string::npos
                        || folded(j.kind + " : " + j.message).find(want) != std::string::npos) { seen = true; break; }
                if (!seen) {
                    verdict = Verdict::Failed;
                    detail = "JOURNAL ne contient pas " + qmark(stripQuotes(trim(c.expected)));
                    break;
                }
                continue;
            }
            const auto e = Expression::compile(target);
            if (!e.valid()) {
                verdict = Verdict::Error;
                detail = "v\xC3\xA9rif " + qmark(target) + " illisible";
                break;
            }
            auto v = e.evaluate(rt.environment());
            if (!v) {
                verdict = Verdict::Error;
                detail = "v\xC3\xA9rif " + qmark(target) + " ne s'\xC3\xA9value pas";
                break;
            }
            const auto cmp = compareValue(*v, c.expected, c.tolerance, v->type() == sim::Type::Real);
            if (!cmp.ok) {
                verdict = Verdict::Failed;
                detail = target + " : " + cmp.note;
                break;
            }
        }
    }
    res.verdict = verdict;
    res.detail = detail;
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

FunctionTestReport runFunctionTests(const Project& p, const HmiFunction& f, sim::Environment* plc) {
    FunctionTestReport rep;
    rep.function = f.id;
    rep.functionName = f.name;
    rep.results.reserve(f.tests.size());
    for (const auto& t : f.tests) rep.results.push_back(runFunctionTest(p, f, t, plc));
    return rep;
}

// ============================================================ le rapport ======
namespace {
std::string argsSummary(const FunctionTest& t) {
    std::string s;
    for (const auto& a : t.arguments) {
        const std::string v = trim(a);
        s += (s.empty() ? "" : " ; ") + (v.empty() ? std::string("(d\xC3\xA9" "faut)") : v);
    }
    return s;
}
std::string expectedSummary(const FunctionTest& t) {
    if (!t.errorCode.empty()) return "l\xC3\xA8ve " + t.errorCode;
    if (!trim(t.expected).empty()) return trim(t.expected);
    return t.checks.empty() ? std::string("\xE2\x80\x94") : std::string("v\xC3\xA9rifs");
}
} // namespace

ExportTable functionTestTable(const HmiFunction& f, const FunctionTestReport& rep) {
    ExportTable t;
    t.title = "Tests de " + f.name + " : " + std::string(rep.results.empty() ? "aucun cas" : rep.ok() ? "r\xC3\xA9ussi" : "\xC3\xA9" "chec");
    t.subtitle = rep.summary();
    t.headers = {"Cas", "Arguments", "Attendu", "Verdict", "D\xC3\xA9tail", "Dur\xC3\xA9" "e"};
    for (std::size_t i = 0; i < f.tests.size(); ++i) {
        const FunctionTestResult r = i < rep.results.size() ? rep.results[i] : FunctionTestResult{};
        t.rows.push_back({f.tests[i].name, argsSummary(f.tests[i]), expectedSummary(f.tests[i]),
                          std::string(verdictLabel(r.verdict)), r.detail,
                          r.verdict == Verdict::Pending || r.verdict == Verdict::Skipped ? std::string{} : seconds(r.seconds)});
    }
    t.notes.push_back("Les tests unitaires v\xC3\xA9rifient une fonction seule ; ce n'est pas un proc\xC3\xA8s-verbal de r\xC3\xA9" "ception.");
    return t;
}

ExportTable campaignTestTable(const std::vector<std::pair<const HmiFunction*, const FunctionTestReport*>>& runs) {
    ExportTable t;
    std::size_t cases = 0, passed = 0, failed = 0, errors = 0;
    for (const auto& [f, rep] : runs)
        if (rep) {
            cases += rep->results.size();
            passed += rep->count(Verdict::Passed);
            failed += rep->count(Verdict::Failed);
            errors += rep->count(Verdict::Error);
        }
    t.title = "Tests des fonctions : " + std::to_string(passed) + " / " + std::to_string(cases) + " r\xC3\xA9ussi(s)";
    std::string sub = std::to_string(cases) + " cas";
    if (failed) sub += ", " + std::to_string(failed) + " \xC3\xA9" "chec(s)";
    if (errors) sub += ", " + std::to_string(errors) + " erreur(s)";
    t.subtitle = sub;
    t.headers = {"Fonction", "Cas", "Verdict", "D\xC3\xA9tail"};
    for (const auto& [f, rep] : runs) {
        if (!f) continue;
        t.rows.push_back({f->name, "", rep ? rep->summary() : std::string("pas encore jou\xC3\xA9"), ""});
        for (std::size_t i = 0; i < f->tests.size(); ++i) {
            const FunctionTestResult r = rep && i < rep->results.size() ? rep->results[i] : FunctionTestResult{};
            t.rows.push_back({"", f->tests[i].name, std::string(verdictLabel(r.verdict)), r.detail});
        }
    }
    return t;
}

// ============================================================== Generer =======
namespace {
// Chaque fonction du projet, des vues et des symboles, avec le nom de son porteur
// (vide : le projet) - pour designer le cas fautif.
void forEachFunction(const Project& p, const std::function<void(const HmiFunction&, const std::string& owner, Id view)>& fn) {
    for (const auto& f : p.programs.functions) fn(f, {}, kNoId);
    for (const auto& v : p.views)
        for (const auto& f : v.functions) fn(f, v.name, v.id);
}
} // namespace

void checkFunctionTests(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    forEachFunction(p, [&](const HmiFunction& f, const std::string& owner, Id view) {
        if (f.tests.empty()) return;
        const auto parts = splitDeclarations(decl::codeOf(f), true);
        const std::size_t nParams = parts.parameters().size();
        const std::string fname = owner.empty() ? f.name : owner + "." + f.name;
        const auto add = [&](S sev, const FunctionTest& t, std::string msg) {
            Issue i;
            i.severity = sev;
            i.category = "Test";
            i.view = view;
            i.property = fname + (t.name.empty() ? std::string{} : " / " + t.name);
            i.message = std::move(msg);
            i.item = t.id;
            out.push_back(std::move(i));
        };
        std::vector<std::string> names;
        for (const auto& t : f.tests) {
            if (t.name.empty()) add(S::Error, t, "cas sans nom");
            else if (std::find_if(names.begin(), names.end(), [&](const std::string& n) { return iequals(n, t.name); }) != names.end())
                add(S::Error, t, "nom de cas en double : " + t.name);
            names.push_back(t.name);
            // Le nombre d'arguments ne doit pas depasser la signature.
            if (t.arguments.size() > nParams)
                add(S::Error, t, "trop d'arguments (" + std::to_string(t.arguments.size()) + " pour " + std::to_string(nParams) + ")");
            // Un attendu sur une procedure (sans retour) n'a pas de sens.
            if (f.returnType.empty() && !trim(t.expected).empty())
                add(S::Warning, t, "la fonction n'a pas de retour : l'attendu est ignor\xC3\xA9");
            // La tolerance : un nombre (REAL seulement).
            double num = 0;
            if (!trim(t.tolerance).empty() && !parseNumber(trim(t.tolerance), num))
                add(S::Error, t, "tol\xC3\xA9rance non num\xC3\xA9rique : " + t.tolerance);
            if (!trim(t.tolerance).empty() && !isRealType(f.returnType))
                add(S::Info, t, "la tol\xC3\xA9rance ne sert qu'\xC3\xA0 un retour REAL : ignor\xC3\xA9" "e");
            // Les arguments lisibles.
            const auto inputs = parts.parameters();
            for (std::size_t k = 0; k < t.arguments.size() && k < inputs.size(); ++k) {
                const std::string text = trim(t.arguments[k]);
                if (text.empty()) continue;
                const auto cell = cellValue(&p, inputs[k]->type, text);
                if (!cell.ok()) { add(S::Error, t, inputs[k]->name + " : " + cell.why); continue; }
                if (!Expression::compile(cell.text).valid()) add(S::Error, t, inputs[k]->name + " : " + text + " illisible");
            }
            // Les poses : une variable connue, une valeur lisible.
            for (const auto& g : t.givens) {
                const std::string target = trim(g.target);
                if (target.empty()) { add(S::Error, t, "pose sans cible"); continue; }
                if (!Expression::compile(target).valid()) { add(S::Warning, t, "pose " + qmark(target) + " : cible illisible"); continue; }
                // Une valeur scalaire lisible (une structure/liste [..] ou (..) : laissee a l'ecriture).
                const std::string val = trim(g.value);
                if (!val.empty() && val.front() != '[' && val.front() != '(' && !Expression::compile(val).valid())
                    add(S::Error, t, "pose " + qmark(target) + " : valeur illisible");
            }
            // Les verifs : une cible connue (ou JOURNAL), un attendu present.
            for (const auto& c : t.checks) {
                const std::string target = trim(c.target);
                if (target.empty()) { add(S::Error, t, "v\xC3\xA9rif sans cible"); continue; }
                if (iequals(target, "JOURNAL")) continue;
                if (!Expression::compile(target).valid()) add(S::Warning, t, "v\xC3\xA9rif " + qmark(target) + " : cible illisible");
                if (trim(c.expected).empty()) add(S::Warning, t, "v\xC3\xA9rif " + qmark(target) + " : aucun attendu");
            }
            // Un cas qui ne juge rien (ni retour attendu, ni verif, ni erreur attendue).
            if (trim(t.expected).empty() && t.checks.empty() && t.errorCode.empty())
                add(S::Info, t, "le cas ne v\xC3\xA9rifie rien (ni attendu, ni v\xC3\xA9rif, ni erreur)");
        }
    });
}

} // namespace hmi
