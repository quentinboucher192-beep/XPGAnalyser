#include "HmiFunctionTests.hpp"

#include "../sim/Runtime.hpp"
#include "../sim/ValueText.hpp"     // trueWord / falseWord (les memes mots partout, 1.12.19)
#include "../domain/ProjectModel.hpp"

#include "HmiCheck.hpp"             // Issue (les controles de Generer)
#include "HmiDecl.hpp"             // decl::codeOf
#include "HmiEnums.hpp"            // 1.13.0 : un retour ou une verif d'enumeration, compare par son nom
#include "HmiExpr.hpp"             // Expression, cellValue n'est pas ici
#include "HmiRuntime.hpp"
#include "HmiScript.hpp"           // splitDeclarations, LocalVar
#include "HmiSymbols.hpp"          // cellValue (la regle des cases)
#include "HmiTypeRegistry.hpp"     // 1.13.0 : un attendu lisible dans le type du retour (Generer)
#include "HmiTypes.hpp"            // 1.13.0 : types::typeOfPath (le type d'une verif)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
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

// 1.13.0 (seconde passe) : les parametres se lisent quel que soit leur type (un T generique, une structure IHM) - comme
// la signature (overload::signatureOf) : sans cela, un parametre de type T disparaissait et les arguments glissaient.
bool anyType(std::string_view) { return true; }

std::string upperAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

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

// Le message d'arret sans le prefixe "fonction X : " (plus lisible dans le detail).
std::string cleanWhy(std::string_view why) {
    std::string s = trim(why);
    if (const auto at = s.find(" : "); at != std::string::npos && s.rfind("fonction ", 0) == 0) s = s.substr(at + 3);
    return s;
}

// Ce que la fonction a dit en s'arretant : le message sans "fonction X : " ni "ligne N : ".
std::string stopMessage(std::string_view why) {
    std::string s = cleanWhy(why);
    if (s.rfind("ligne ", 0) == 0) {
        std::size_t k = 6;
        while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) ++k;
        if (k > 6 && s.compare(k, 3, " : ") == 0) s = s.substr(k + 3);
    }
    return s;
}

// 1.13.0 : le code E_* d'une erreur levee (LEVER('E_Niveau', ...)). Le message d'arret a la forme
// "fonction X : ligne N : E_Niveau : message" (ou "... : E_Niveau" sans message) : le code est EN TETE
// de ce que la fonction a dit, seul ou suivi de " : " - pas un E_ cite dans le message ou dans le nom
// de la fonction (Verifier_E_Niveau). Vide : aucun code (une erreur du moteur, au message francais).
std::string errorCodeOf(std::string_view why) {
    const std::string s = stopMessage(why);
    const std::string head = s.substr(0, std::min(s.find(" : "), s.size()));
    if (head.size() < 3 || (head[0] != 'E' && head[0] != 'e') || head[1] != '_') return {};
    for (const char c : head)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return {};
    return head;
}

// `word` dans `text`, en mot entier (ni lettre, ni chiffre, ni _ de part et d'autre), sans casse ni accents.
bool containsWord(std::string_view text, std::string_view word) {
    const std::string t = folded(text), w = folded(trim(word));
    if (w.empty()) return false;
    const auto inWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (auto at = t.find(w); at != std::string::npos; at = t.find(w, at + 1))
        if ((at == 0 || !inWord(t[at - 1])) && (at + w.size() >= t.size() || !inWord(t[at + w.size()]))) return true;
    return false;
}

// Compare la valeur obtenue a l'attendu (texte). `real` : le type est REAL/LREAL (la tolerance joue,
// vide = egalite stricte). Les autres types : egalite exacte (sim::Value). `note` : "obtenu X, attendu Y".
// 1.13.0 : `enumeration` (non nul : le type attendu est une enumeration IHM) - la valeur, un DINT en
// marche, se compare a l'attendu ecrit par son nom (Auto), son litteral (E_Mode#Auto), son texte ou
// son numero ; un STRING se compare tel quel, casse et accents compris.
struct Compared { bool ok{false}; std::string note; };

// 1.13.0 (seconde passe) : NAN, INF, +INF, -INF - les nombres qu'un retour REAL peut rendre et que readDecimal ne lit pas.
bool specialReal(std::string_view text, double& out) {
    const std::string u = upperAscii(trim(text));
    if (u == "NAN") { out = std::numeric_limits<double>::quiet_NaN(); return true; }
    if (u == "INF" || u == "+INF") { out = std::numeric_limits<double>::infinity(); return true; }
    if (u == "-INF") { out = -std::numeric_limits<double>::infinity(); return true; }
    return false;
}

// Deux reels egaux pour un cas : NAN vaut NAN, un infini le meme infini ; sinon l'ecart (la tolerance, ou presque rien).
bool sameReal(double a, double b, bool hasTol, double tol) {
    if (std::isnan(b)) return std::isnan(a);
    if (std::isinf(b)) return a == b;
    const double ecart = std::fabs(a - b);
    return hasTol ? ecart <= tol : ecart <= 1e-9 * std::max(1.0, std::fabs(b));
}

Compared compareValue(const sim::Value& got, const std::string& expectedRaw, const std::string& tolRaw, bool real,
                      const HmiType* enumeration = nullptr) {
    const std::string expected = stripQuotes(trim(expectedRaw));
    const std::string gotText = valueText(got);
    if (enumeration) {
        double n = 0;
        const bool number = parseNumber(gotText, n);
        const auto gotNumber = static_cast<std::int64_t>(std::llround(n));
        const std::string shown = number ? enumDisplay(*enumeration, gotNumber) : gotText;
        std::int64_t want = 0;
        if (!enumNumberOf(*enumeration, expected, want))
            return {false, "obtenu " + shown + ", attendu " + expected + " (pas une valeur de " + enumeration->name + ")"};
        return {number && gotNumber == want, "obtenu " + shown + ", attendu " + expected};
    }
    if (got.type() == sim::Type::String) return {got.asString() == expected, "obtenu " + gotText + ", attendu " + expected};
    double b = 0;
    const bool expNum = parseNumber(expected, b) || (real && specialReal(expected, b));   // 1.13.0 : NAN, INF aussi
    if (real && expNum) {
        const double a = got.asReal();
        if (!std::isfinite(b)) return {sameReal(a, b, false, 0), "obtenu " + gotText + ", attendu " + expected};
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
    // Un litteral type (T#5s, D#2026-10-09) : sans casse, comme en ST (t#5S vaut T#5s). Pas d'accent a y oter.
    return {upperAscii(stripQuotes(gotText)) == upperAscii(expected), note};
}

// 1.13.0 (seconde passe) : compare la valeur obtenue a un ATTENDU CALCULE (=Seuil_Haut - 5, ou un nom connu seul),
// evalue apres l'appel sur le meme banc. `shown` : l'attendu tel qu'ecrit (=Seuil_Haut). Deux nombres : l'ecart (la
// tolerance si elle sert), sinon presque rien ; un booleen, un texte : egaux ; le reste : le meme texte.
Compared compareComputed(const sim::Value& got, const sim::Value& want, const std::string& shown, const std::string& tolRaw, bool real,
                         const HmiType* enumeration = nullptr) {
    const std::string gotText = valueText(got), wantText = valueText(want);
    std::string gotShown = gotText, wantShown = wantText;
    const auto number = [](const sim::Value& v) { return sim::isNumeric(v.type()) || v.type() == sim::Type::Time; };
    if (enumeration && number(got) && number(want)) {
        gotShown = enumDisplay(*enumeration, static_cast<std::int64_t>(std::llround(got.asReal())));
        wantShown = enumDisplay(*enumeration, static_cast<std::int64_t>(std::llround(want.asReal())));
    }
    const std::string note = "obtenu " + gotShown + ", attendu " + shown + " (" + wantShown + ")";
    if (got.type() == sim::Type::String || want.type() == sim::Type::String)
        return {got.type() == want.type() && got.asString() == want.asString(), note};
    if (got.type() == sim::Type::Bool || want.type() == sim::Type::Bool)
        return {got.type() == want.type() && got.isTruthy() == want.isTruthy(), note};
    if (number(got) && number(want)) {
        double tol = 0;
        const bool hasTol = real && parseNumber(trim(tolRaw), tol);
        return {sameReal(got.asReal(), want.asReal(), hasTol, tol), note};
    }
    return {upperAscii(gotText) == upperAscii(wantText), note};
}

// 1.13.0 (seconde passe) : L'ATTENDU D'UN CAS (du retour ou d'une verif) EST-IL UNE FORMULE - ecrit =calcul, ou un nom
// connu seul (la regle des cases : Seuil_Haut vaut =Seuil_Haut) ? `type` : le type attendu (vide : inconnu). Vrai :
// `formula` recoit le calcul, sans son =, a evaluer apres l'appel. Une valeur fixe (16.0, TRUE, Auto) : faux.
// 1.13.0 (D3b) : un mot seul n'y est un attendu calcule que s'il nomme une valeur que le banc lit (testExpected).
bool expectedFormula(const Project& p, std::string_view type, const std::string& raw, std::string& formula) {
    const std::string t = trim(raw);
    if (t.empty()) return false;
    if (t.front() == '=') {
        formula = trim(std::string_view(t).substr(1));
        return true;
    }
    const TestExpected e = testExpected(p, type, t);
    if (!e.ok() || !e.formula) return false;
    formula = trim(std::string_view(e.kept).substr(1));
    return true;
}

// 1.13.0 (D3b) : la racine d'un chemin (Cuve de Cuve.Niveau, Pressions de Pressions[2]).
std::string rootName(std::string_view t) {
    std::size_t i = 0;
    while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
    return std::string(t.substr(0, i));
}

// 1.13.0 (D3b) : une VALEUR que le banc lit par son nom - une variable IHM, SYS, une variable de l'automate (le
// programme relie) ; pas un objet, une vue, une fonction, un type, une fonction standard qui porterait ce nom.
bool benchValue(const Project& p, std::string_view root) {
    if (root.empty()) return false;
    const std::string u = upperAscii(root);
    if (u == "SYS" || p.variable(root)) return true;
    if (u == "THIS" || u == "TRUE" || u == "FALSE" || isStandardFunction(root)) return false;
    return isKnownName(nullptr, root);
}

// 1.13.0 (D3b) : un attendu de ce type peut-il etre NAN, INF (REAL, LREAL, un generique qui admet un reel) ?
bool realExpected(std::string_view type) {
    const std::string u = upperAscii(trim(type));
    return u == "REAL" || u == "LREAL" || u == "ANY_REAL" || u == "ANY_NUM" || u == "ANY_MAGNITUDE";
}

// 1.13.0 (seconde passe) : LA VALEUR D'UNE POSE telle que le banc l'ecrit - lue comme toute case dans le type de sa
// cible (une variable, un membre, une case : 2,5 -> 2.5, =Seuil -> Seuil) ; une cible au type inconnu (l'automate) :
// le = de tete ote. Faux : illisible, `why` dit pourquoi. Le banc et Generer la lisent ainsi tous les deux.
bool givenText(const Project& p, const std::string& target, const std::string& raw, std::string& out, std::string& why) {
    out = trim(raw);
    if (out.empty()) return true;
    std::string type;
    if (const Variable* var = p.variable(target)) type = var->type;
    else type = types::typeOfPath(p, target);
    if (!type.empty()) {
        const auto cell = cellValue(&p, type, out);
        if (!cell.ok()) {
            why = cell.why;
            return false;
        }
        out = cell.text;
        return true;
    }
    if (out.front() == '=') out = trim(std::string_view(out).substr(1));
    return true;
}

// Le generique de `f` qui porte ce nom (T) ; nul : un type ordinaire.
const GenericParam* genericNamed(const HmiFunction& f, std::string_view type) {
    const std::string t = trim(type);
    for (const auto& g : f.generics)
        if (iequals(g.name, t)) return &g;
    return nullptr;
}

} // namespace

// ============================================================ 1.13.0 (D3b) : l'attendu d'un cas ===
TestExpected testExpected(const Project& p, std::string_view typeRaw, std::string_view typed) {
    TestExpected out;
    const std::string t = trim(typed);
    if (t.empty()) return out;
    const auto fail = [&](std::string why) {
        while (!why.empty() && why.back() == '.') why.pop_back();
        out.why = why.empty() ? qmark(t) + " ne se lit pas" : std::move(why);
        out.kept.clear();
        out.formula = false;
        return out;
    };
    if (t.front() == '=') {
        const std::string e = trim(std::string_view(t).substr(1));
        if (e.empty()) return fail("rien apr\xC3\xA8s le = : \xC3\xA9" "cris le calcul (=Seuil * 2)");
        out.kept = "=" + e;
        out.formula = true;
        return out;
    }
    const std::string type = trim(typeRaw);
    double x = 0;
    if (realExpected(type) && specialReal(t, x)) {      // NAN, INF, -INF : un reel que la fonction peut rendre
        out.kept = upperAscii(t);
        return out;
    }
    const std::string read = type.empty() ? std::string("ANY") : type;
    const CellText cell = cellValue(&p, read, t);
    if (cell.ok() && cell.formula && isVariablePath(t) && !benchValue(p, rootName(t))) {
        // Un mot seul qui nomme un objet, une vue, une fonction, un type : pas une valeur du banc - la valeur fixe.
        std::string why;
        std::string lit = argumentLiteral(&p, read, t, &why);
        if (lit.empty()) return fail(why);
        out.kept = std::move(lit);
        return out;
    }
    if (!cell.ok()) return fail(cell.why);
    out.kept = cell.formula ? "=" + cell.text : cell.text;
    out.formula = cell.formula;
    return out;
}

// ============================================================ 1.13.0 : les types d'un cas ===
std::string testCellType(const HmiFunction& f, std::string_view declared) {
    if (const GenericParam* g = genericNamed(f, declared)) return trim(g->constraint).empty() ? std::string("ANY") : trim(g->constraint);
    return std::string(declared);
}

bool testToleranceApplies(const HmiFunction& f) {
    if (isRealType(trim(f.returnType))) return true;
    const GenericParam* g = genericNamed(f, f.returnType);
    if (!g) return false;
    const std::string c = upperAscii(trim(g->constraint));
    return c.empty() || c == "ANY" || c == "ANY_NUM" || c == "ANY_REAL" || c == "ANY_MAGNITUDE" || c == "ANY_ELEMENTARY";
}

std::vector<std::string> testParameterNames(const HmiFunction& f) {
    std::vector<std::string> out;
    const auto parts = splitDeclarations(decl::codeOf(f), true, anyType);
    for (const auto* l : parts.parameters()) out.push_back(l->name);
    return out;
}

bool remapTestArguments(HmiFunction& f, const std::vector<std::string>& before, const std::vector<std::string>& after) {
    if (before.size() == after.size()) {
        bool same = true;
        for (std::size_t k = 0; k < before.size() && same; ++k) same = iequals(before[k], after[k]);
        if (same) return false;
    }
    bool changed = false;
    for (auto& t : f.tests) {
        std::vector<std::string> args(after.size());
        for (std::size_t j = 0; j < after.size(); ++j)
            for (std::size_t i = 0; i < before.size(); ++i)
                if (iequals(before[i], after[j])) {
                    if (i < t.arguments.size()) args[j] = t.arguments[i];
                    break;
                }
        // Ce qui depassait deja la signature (un ancien desordre) : garde au bout, pour que l'onglet le montre.
        for (std::size_t i = before.size(); i < t.arguments.size(); ++i) args.push_back(t.arguments[i]);
        while (!args.empty() && trim(args.back()).empty()) args.pop_back();
        if (args != t.arguments) {
            t.arguments = std::move(args);
            changed = true;
        }
    }
    return changed;
}

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
        std::string valueText, why;
        // La regle des cases, dans le type de la cible (une variable, un membre, une case : =Seuil -> Seuil).
        if (!givenText(p, target, g.value, valueText, why)) { poseFail(target, why); return res; }
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
    const auto parts = splitDeclarations(decl::codeOf(f), true, anyType);
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
        const auto cell = cellValue(&p, testCellType(f, inputs[k]->type), text);      // 1.13.0 : T se lit dans sa contrainte
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
            // 1.13.0 : le code LEVE (en tete du message) doit etre l'attendu, exactement (sans casse) :
            // E_Niveau n'accepte ni E_Niveau_Bas, ni un E_Niveau cite dans le message ou le nom de la
            // fonction. Sans code (une erreur du moteur) : l'attendu, en mot entier, dans son message.
            const std::string code = errorCodeOf(why);
            const bool match = !code.empty() ? iequals(code, trim(test.errorCode)) : containsWord(stopMessage(why), test.errorCode);
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
    // 1.13.0 (seconde passe) : un ATTENDU CALCULE (=Seuil_Haut - 5, ou un nom connu seul) s'evalue apres l'appel, sur
    // le meme banc ; une valeur fixe se compare telle quelle. La tolerance joue sur un retour REAL - ou sur un T qui
    // vaut un REAL ici (la valeur obtenue le dit).
    const auto judge = [&](const sim::Value& got, const std::string& expected, const std::string& tol, bool real,
                           std::string_view type, const HmiType* enumeration, Compared& out) {
        std::string formula;
        if (!expectedFormula(p, type, expected, formula)) {
            out = compareValue(got, expected, tol, real, enumeration);
            return true;
        }
        const auto e = Expression::compile(formula);
        std::optional<sim::Value> want;
        if (e.valid())
            if (auto v = e.evaluate(rt.environment())) want = *v;
        if (!want) {
            out.ok = false;
            out.note = "attendu =" + formula + (e.valid() ? " ne s'\xC3\xA9value pas" : " illisible");
            return false;
        }
        out = compareComputed(got, *want, "=" + formula, tol, real, enumeration);
        return true;
    };
    if (!f.returnType.empty() && !trim(test.expected).empty()) {
        Compared cmp;
        const bool read = judge(result, test.expected, test.tolerance, isRealType(f.returnType) || result.type() == sim::Type::Real,
                                testCellType(f, f.returnType), findEnumeration(p, trim(f.returnType)), cmp);   // E_Mode -> Auto
        detail = cmp.note;
        if (!cmp.ok) verdict = read ? Verdict::Failed : Verdict::Error;
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
            const std::string targetType = types::typeOfPath(p, target);
            Compared cmp;
            const bool read = judge(*v, c.expected, c.tolerance, v->type() == sim::Type::Real, targetType,
                                    findEnumeration(p, targetType), cmp);   // 1.13.0 : une variable d'enumeration, un attendu calcule
            if (!cmp.ok) {
                verdict = read ? Verdict::Failed : Verdict::Error;
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

// 1.13.0 : la premiere racine de `e` qui n'est pas un nom connu (variable IHM ou de l'automate, SYS,
// une vue, une fonction, un type...) ; vide : toutes le sont.
std::string unknownRoot(const Project& p, const Expression& e) {
    for (const auto& r : e.roots())
        if (!isKnownName(&p, r)) return r;
    return {};
}

// 1.13.0 : pourquoi un attendu ne se lit pas dans le type `typeRaw` comme le moteur le comparera ; vide : il se lit.
// 1.13.0 (seconde passe) : pour le retour comme pour une verif. Une formule (=calcul, un nom connu seul) : lisible, et
// ses noms connus ; un nombre pour un type numerique (NAN, INF, -INF aussi pour un reel : un retour REAL peut les
// rendre) ; VRAI/FAUX pour un BOOL ; une valeur pour une enumeration ; un generique (ANY...) ou un autre type : rien
// a redire ici (sauf un nombre attendu par ANY_NUM, ANY_REAL, ANY_INT).
std::string unreadableExpected(const Project& p, const std::string& typeRaw, const std::string& expectedRaw) {
    const std::string type = trim(typeRaw);
    std::string formula;
    if (expectedFormula(p, type, expectedRaw, formula)) {
        const Expression e = Expression::compile(formula);
        if (formula.empty() || !e.valid()) return "=" + formula + " ne se lit pas";
        if (const std::string unknown = unknownRoot(p, e); !unknown.empty()) return unknown + " n'est pas un nom connu";
        return {};
    }
    const std::string expected = stripQuotes(trim(expectedRaw));
    double x = 0;
    const std::string u = upperAscii(type);
    if (u == "ANY_NUM" || u == "ANY_REAL" || u == "ANY_INT")
        return parseNumber(expected, x) || (u != "ANY_INT" && specialReal(expected, x)) ? std::string{} : expected + " n'est pas un nombre";
    if (type.empty() || u.rfind("ANY", 0) == 0) return {};
    if (const HmiType* en = findEnumeration(p, type)) {
        std::int64_t n = 0;
        return enumNumberOf(*en, expected, n) ? std::string{} : expected + " n'est pas une valeur de " + en->name;
    }
    if (typereg::simTypeOf(type) == sim::Type::Bool)
        return sim::trueWord(folded(expected)) || sim::falseWord(folded(expected)) ? std::string{}
                                                                                    : expected + " n'est pas un bool\xC3\xA9" "en (VRAI ou FAUX)";
    if (typereg::isNumber(type) && !parseNumber(expected, x) && !(isRealType(type) && specialReal(expected, x)))
        return expected + " n'est pas un nombre";
    return {};
}

// 1.13.0 (seconde passe) : un membre ou un indice d'une cible (pose, verif) qui n'existe pas (Cuve.Niveu) - le controle
// des chemins des scripts ; vide : le chemin se tient (ou sa racine n'est pas une variable IHM).
std::string pathFault(const Project& p, const std::string& target) {
    for (const auto& pp : types::pathProblems(p, target))
        if (!pp.message.empty()) return pp.message;
    return {};
}
} // namespace

void checkFunctionTests(const Project& p, std::vector<Issue>& out) {
    using S = Issue::Severity;
    forEachFunction(p, [&](const HmiFunction& f, const std::string& owner, Id view) {
        if (f.tests.empty()) return;
        const auto parts = splitDeclarations(decl::codeOf(f), true, anyType);
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
            // 1.13.0 (seconde passe) : un cas qui attend une erreur juge sur elle - son attendu ne sert pas.
            else if (!t.errorCode.empty() && !trim(t.expected).empty())
                add(S::Warning, t, "le cas attend l'erreur " + t.errorCode + " : l'attendu (" + trim(t.expected)
                                       + ") est ignor\xC3\xA9 - vide sa case dans la fiche du cas (Attendu ignor\xC3\xA9)");
            // La tolerance : un nombre (REAL seulement).
            double num = 0;
            if (!trim(t.tolerance).empty() && !parseNumber(trim(t.tolerance), num))
                add(S::Error, t, "tol\xC3\xA9rance non num\xC3\xA9rique : " + t.tolerance);
            if (!trim(t.tolerance).empty() && !testToleranceApplies(f))
                add(S::Info, t, "la tol\xC3\xA9rance ne sert qu'\xC3\xA0 un retour REAL : ignor\xC3\xA9" "e");
            // Les arguments lisibles.
            const auto inputs = parts.parameters();
            for (std::size_t k = 0; k < t.arguments.size() && k < inputs.size(); ++k) {
                const std::string text = trim(t.arguments[k]);
                if (text.empty()) continue;
                const auto cell = cellValue(&p, testCellType(f, inputs[k]->type), text);      // 1.13.0 : T dans sa contrainte
                if (!cell.ok()) { add(S::Error, t, inputs[k]->name + " : " + cell.why); continue; }
                if (!Expression::compile(cell.text).valid()) add(S::Error, t, inputs[k]->name + " : " + text + " illisible");
            }
            // 1.13.0 : l'attendu du retour, lisible comme le moteur le compare (un nombre, un booleen, une
            // valeur de l'enumeration) ; un generique (T) ou un autre type : laisse a l'appel.
            if (!f.returnType.empty() && !trim(t.expected).empty() && t.errorCode.empty())
                if (const std::string why = unreadableExpected(p, testCellType(f, f.returnType), t.expected); !why.empty())
                    add(S::Error, t, "attendu illisible : " + why);
            // Les poses : une variable connue, une valeur lisible.
            for (const auto& g : t.givens) {
                const std::string target = trim(g.target);
                if (target.empty()) { add(S::Error, t, "pose sans cible"); continue; }
                const Expression ex = Expression::compile(target);
                if (!ex.valid()) { add(S::Warning, t, "pose " + qmark(target) + " : cible illisible"); continue; }
                // 1.13.0 : la variable posee existe (une faute de frappe se voit ici, pas seulement en jouant le cas).
                if (const std::string unknown = unknownRoot(p, ex); !unknown.empty()) {
                    add(S::Error, t, "pose " + qmark(target) + " : variable inconnue (" + unknown + ")");
                    continue;
                }
                // 1.13.0 (seconde passe) : un membre, un indice qui n'existe pas (Cuve.Niveu).
                if (const std::string fault = pathFault(p, target); !fault.empty()) {
                    add(S::Error, t, "pose " + qmark(target) + " : " + fault);
                    continue;
                }
                // La valeur, lue comme le banc l'ecrit (la regle des cases, dans le type de la cible : =Seuil -> Seuil,
                // 2,5 -> 2.5) ; une structure/liste [..] ou (..) : laissee a l'ecriture.
                std::string val, why;
                if (!givenText(p, target, g.value, val, why)) {
                    add(S::Error, t, "pose " + qmark(target) + " : " + why);
                    continue;
                }
                if (!val.empty() && val.front() != '[' && val.front() != '(' && !Expression::compile(val).valid())
                    add(S::Error, t, "pose " + qmark(target) + " : valeur illisible");
            }
            // Les verifs : une cible connue (ou JOURNAL), un attendu present.
            for (const auto& c : t.checks) {
                const std::string target = trim(c.target);
                if (target.empty()) { add(S::Error, t, "v\xC3\xA9rif sans cible"); continue; }
                if (iequals(target, "JOURNAL")) continue;
                const Expression ex = Expression::compile(target);
                if (!ex.valid()) {
                    add(S::Warning, t, "v\xC3\xA9rif " + qmark(target) + " : cible illisible");
                } else if (const std::string unknown = unknownRoot(p, ex); !unknown.empty()) {
                    add(S::Error, t, "v\xC3\xA9rif " + qmark(target) + " : variable inconnue (" + unknown + ")");
                    continue;
                } else if (const std::string fault = pathFault(p, target); !fault.empty()) {    // 1.13.0 : Cuve.Niveu
                    add(S::Error, t, "v\xC3\xA9rif " + qmark(target) + " : " + fault);
                    continue;
                }
                if (trim(c.expected).empty()) add(S::Warning, t, "v\xC3\xA9rif " + qmark(target) + " : aucun attendu");
                // 1.13.0 (seconde passe) : l'attendu d'une verif, lisible dans le type de sa cible (comme celui du retour).
                else if (const std::string why = unreadableExpected(p, types::typeOfPath(p, target), c.expected); !why.empty())
                    add(S::Error, t, "v\xC3\xA9rif " + qmark(target) + " : attendu illisible : " + why);
            }
            // Un cas qui ne juge rien (ni retour attendu, ni verif, ni erreur attendue).
            if (trim(t.expected).empty() && t.checks.empty() && t.errorCode.empty())
                add(S::Info, t, "le cas ne v\xC3\xA9rifie rien (ni attendu, ni v\xC3\xA9rif, ni erreur)");
        }
    });
}

} // namespace hmi
