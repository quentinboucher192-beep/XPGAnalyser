// =============================================================================
//  app/ai/AiSandbox.cpp - 1.21.0 : le bac a sable d'execution (voir l'en-tete)
// =============================================================================
#include "AiSandbox.hpp"

#include "AiJson.hpp"
#include "AiPaths.hpp"

#include "../../core/Process.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiFunctionTests.hpp"
#include "../../hmi/HmiMigrate.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiRecording.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#endif

namespace app::ai::sandbox {

namespace json = app::ai::json;
namespace fs = std::filesystem;

namespace {

constexpr int         kFormat = 1;
constexpr std::size_t kMaxCode = 60000;          // comme creer_fonction
constexpr std::size_t kMaxCases = 50;
constexpr std::size_t kMaxProjectBytes = 256u * 1024u * 1024u;

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// ST, C, C++ (sans casse ; "CPP" et "C++" : le C++).
bool languageOf(std::string_view text, hmi::ScriptLang& out) {
    const std::string u = upper(trimmed(text));
    if (u.empty() || u == "ST") out = hmi::ScriptLang::ST;
    else if (u == "C") out = hmi::ScriptLang::C;
    else if (u == "C++" || u == "CPP" || u == "CXX") out = hmi::ScriptLang::Cpp;
    else return false;
    return true;
}
std::string languageName(hmi::ScriptLang l) {
    return l == hmi::ScriptLang::C ? "C" : l == hmi::ScriptLang::Cpp ? "C++" : "ST";
}

const char* stateKey(State s) {
    switch (s) {
        case State::Ok: return "ok";
        case State::Failed: return "echec";
        case State::Skipped: return "non_fait";
        case State::NotRun: break;
    }
    return "non_lance";
}
State stateFromKey(std::string_view k) {
    if (k == "ok") return State::Ok;
    if (k == "echec") return State::Failed;
    if (k == "non_fait") return State::Skipped;
    return State::NotRun;
}

std::string diagLines(const std::vector<hmi::ScriptDiagnostic>& diags, hmi::ScriptDiagnostic::Severity sev, std::size_t max = 12) {
    std::string out;
    std::size_t n = 0;
    for (const auto& d : diags) {
        if (d.severity != sev) continue;
        if (n++ >= max) {
            out += "\n\xE2\x80\xA6";
            break;
        }
        if (!out.empty()) out += '\n';
        out += (d.line > 0 ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + d.message;
    }
    return out;
}
std::size_t countOf(const std::vector<hmi::ScriptDiagnostic>& diags, hmi::ScriptDiagnostic::Severity sev) {
    return static_cast<std::size_t>(std::count_if(diags.begin(), diags.end(), [&](const hmi::ScriptDiagnostic& d) { return d.severity == sev; }));
}

Stage makeStage(const char* key, const char* label) {
    Stage s;
    s.key = key;
    s.label = label;
    return s;
}

std::string nowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// ---- le JSON du travail et du resultat ----------------------------------------------------------------------------
json::Value jobValue(const Job& j) {
    json::Value v = json::Value::object();
    v.set("langage", j.language);
    v.set("nom", j.name);
    v.set("retour", j.returnType);
    v.set("code", j.code);
    v.set("fonction_existante", j.existing);
    json::Value cases = json::Value::array();
    for (const auto& c : j.cases) {
        json::Value cv = json::Value::object();
        cv.set("nom", c.name);
        json::Value args = json::Value::array();
        for (const auto& a : c.arguments) args.push(a);
        cv.set("arguments", std::move(args));
        cv.set("attendu", c.expected);
        cv.set("tolerance", c.tolerance);
        cv.set("erreur", c.errorCode);
        cases.push(std::move(cv));
    }
    v.set("cas", std::move(cases));
    v.set("secondes", j.seconds);
    return v;
}

// Une valeur JSON de l'IA en texte de case : "2.5" et 2.5 donnent "2.5" ; true : "TRUE".
std::string cellText(const json::Value& v) {
    if (v.isString()) return v.asString();
    if (v.isBool()) return v.asBool() ? "TRUE" : "FALSE";
    if (v.isNumber()) {
        const double d = v.asNumber();
        if (d == static_cast<double>(static_cast<long long>(d)) && d > -1e15 && d < 1e15) return std::to_string(static_cast<long long>(d));
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", d);
        return buf;
    }
    if (v.isNull()) return {};
    return json::write(v, false);
}

bool jobFromValue(const json::Value& v, Job& j, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!v.isObject()) return fail("l'entr\xC3\xA9" "e n'est pas un objet JSON");
    j = Job{};
    j.language = v.str("langage", "ST");
    hmi::ScriptLang lang{};
    if (!languageOf(j.language, lang)) return fail("langage inconnu : " + j.language + " (ST, C ou C++)");
    j.language = languageName(lang);
    j.name = trimmed(v.str("nom"));
    j.returnType = trimmed(v.str("retour"));
    j.code = v.str("code");
    j.existing = v.flag("fonction_existante", false);
    j.seconds = std::clamp(v.num("secondes", 10.0), 1.0, 60.0);
    if (j.name.empty()) return fail("\xC2\xAB nom \xC2\xBB manque (le nom de la fonction)");
    if (j.code.size() > kMaxCode) return fail("code trop long (60 000 caract\xC3\xA8res au plus)");
    if (!j.existing && trimmed(j.code).empty()) return fail("\xC2\xAB code \xC2\xBB manque (ou \xC2\xAB fonction_existante \xC2\xBB : vrai)");
    for (const auto& cv : v.at("cas").items()) {
        if (j.cases.size() >= kMaxCases) break;
        Case c;
        c.name = trimmed(cv.str("nom"));
        for (const auto& a : cv.at("arguments").items()) c.arguments.push_back(cellText(a));
        c.expected = cellText(cv.at("attendu"));
        c.tolerance = cellText(cv.at("tolerance"));
        c.errorCode = trimmed(cv.str("erreur"));
        if (c.name.empty()) c.name = "Cas " + std::to_string(j.cases.size() + 1);
        j.cases.push_back(std::move(c));
    }
    return true;
}

json::Value outcomeValue(const Outcome& o) {
    json::Value v = json::Value::object();
    v.set("format", kFormat);
    v.set("langage", o.language);
    json::Value st = json::Value::array();
    for (const auto& s : o.stages) {
        json::Value sv = json::Value::object();
        sv.set("cle", s.key);
        sv.set("titre", s.label);
        sv.set("etat", stateKey(s.state));
        sv.set("detail", s.detail);
        st.push(std::move(sv));
    }
    v.set("etapes", std::move(st));
    json::Value cs = json::Value::array();
    for (const auto& c : o.cases) {
        json::Value cv = json::Value::object();
        cv.set("nom", c.name);
        cv.set("appel", c.call);
        cv.set("verdict", c.verdict);
        cv.set("detail", c.detail);
        cv.set("obtenu", c.got);
        cv.set("reussi", c.passed);
        cs.push(std::move(cv));
    }
    v.set("cas", std::move(cs));
    if (!o.error.empty()) v.set("erreur", o.error);
    return v;
}

// ---- les etapes du ST ----------------------------------------------------------------------------------------------
void runSt(const Job& job, const hmi::Project& source, Outcome& o) {
    Stage analyse = makeStage("analyse", "Analyse syntaxique");
    Stage compil = makeStage("compilation", "Compilation (noms, types, appels du projet)");
    Stage exec = makeStage("execution", "Ex\xC3\xA9" "cution des cas (banc d'essai neuf, simulation sans automate)");
    const auto finish = [&] {
        o.stages = {analyse, compil, exec};
    };
    const auto skipRest = [&](const char* why) {
        if (compil.state == State::NotRun) {
            compil.state = State::Skipped;
            compil.detail = why;
        }
        if (exec.state == State::NotRun) {
            exec.state = State::Skipped;
            exec.detail = why;
        }
    };

    hmi::Project p = source;   // une copie : le projet de l'application n'est jamais touche
    hmi::HmiFunction* fn = nullptr;
    for (auto& f : p.programs.functions)
        if (iequal(f.name, job.name)) {
            fn = &f;
            break;
        }
    if (job.existing) {
        if (!fn) {
            analyse.state = State::Failed;
            analyse.detail = "la fonction " + job.name + " n'existe pas dans le projet";
            skipRest("rien \xC3\xA0 essayer");
            return finish();
        }
        analyse.state = State::Ok;
        analyse.detail = "la fonction du projet, telle qu'elle est";
    } else {
        // L'analyse syntaxique : le code tel que l'IA l'a ecrit (ses blocs VAR compris).
        const auto diags = hmi::checkScript(hmi::ScriptLang::ST, job.code, job.name);
        const std::size_t errors = countOf(diags, hmi::ScriptDiagnostic::Severity::Error);
        if (errors > 0) {
            analyse.state = State::Failed;
            analyse.detail = std::to_string(errors) + (errors > 1 ? " fautes :\n" : " faute :\n") + diagLines(diags, hmi::ScriptDiagnostic::Severity::Error);
            skipRest("l'analyse syntaxique a \xC3\xA9" "chou\xC3\xA9");
            return finish();
        }
        analyse.state = State::Ok;
        analyse.detail = "aucune faute de syntaxe";
        if (const std::string w = diagLines(diags, hmi::ScriptDiagnostic::Severity::Warning); !w.empty()) analyse.detail += "\navertissements :\n" + w;
        // Dans la copie : une fonction existante prend ce code (une correction), sinon une nouvelle fonction.
        if (fn) {
            fn->body = job.code;
            fn->decls.clear();
            if (!job.returnType.empty()) fn->returnType = hmi::types::normalized(job.returnType);
        } else {
            hmi::HmiFunction f;
            f.id = p.allocate();
            f.name = job.name;
            f.returnType = hmi::types::normalized(job.returnType);
            f.body = job.code;
            f.description = "Essai du bac \xC3\xA0 sable";
            p.programs.functions.push_back(std::move(f));
            fn = &p.programs.functions.back();
        }
        // Ses blocs VAR deviennent ses declarations (comme une fonction creee par l'Assistant).
        const hmi::Id id = fn->id;
        const auto plan = hmi::migrate::plan(p, [&](const hmi::migrate::Place& pl) { return pl.id == id; });
        if (!plan.items.empty()) (void)hmi::migrate::apply(p, plan);
        fn = nullptr;
        for (auto& f : p.programs.functions)
            if (f.id == id) fn = &f;
        if (!fn) {
            compil.state = State::Failed;
            compil.detail = "la fonction a disparu de la copie (interne)";
            skipRest("interne");
            return finish();
        }
    }

    // La compilation : le Compiler du logiciel, sur cette fonction, sans automate relie (un nom inconnu est une erreur).
    hmi::CompileFocus focus;
    focus.functions.insert(fn->id);
    const hmi::NameExists noPlc = [](std::string_view) { return false; };
    const auto issues = hmi::compileWith(p, noPlc, hmi::exprcheck::PlcPaths{}, focus);
    std::string errors, warnings;
    std::size_t nErrors = 0, nWarnings = 0;
    for (const auto& is : issues) {
        if (is.item != fn->id && is.script != fn->id) continue;
        const std::string line = (is.line > 0 ? "ligne " + std::to_string(is.line) + " : " : std::string{}) + is.message;
        if (is.severity == hmi::Issue::Severity::Error) {
            if (nErrors++ < 12) errors += (errors.empty() ? "" : "\n") + line;
        } else if (is.severity == hmi::Issue::Severity::Warning) {
            if (nWarnings++ < 8) warnings += (warnings.empty() ? "" : "\n") + line;
        }
    }
    if (nErrors > 0) {
        compil.state = State::Failed;
        compil.detail = std::to_string(nErrors) + (nErrors > 1 ? " erreurs :\n" : " erreur :\n") + errors;
        skipRest("la compilation a \xC3\xA9" "chou\xC3\xA9");
        return finish();
    }
    compil.state = State::Ok;
    compil.detail = "aucune erreur";
    if (nWarnings > 0) compil.detail += " ; " + std::to_string(nWarnings) + " avertissement(s) :\n" + warnings;

    // L'execution : les cas donnes (sinon ceux de la fonction du projet), sur un banc neuf, sans automate.
    std::vector<hmi::FunctionTest> tests;
    for (const auto& c : job.cases) {
        hmi::FunctionTest t;
        t.id = p.allocate();
        t.name = c.name;
        t.arguments = c.arguments;
        t.expected = c.expected;
        t.tolerance = c.tolerance;
        t.errorCode = c.errorCode;
        tests.push_back(std::move(t));
    }
    if (tests.empty() && job.existing) tests = fn->tests;
    if (tests.empty()) {
        exec.state = State::Skipped;
        exec.detail = "aucun cas d'essai donn\xC3\xA9 : la fonction compile, mais rien n'a \xC3\xA9t\xC3\xA9 ex\xC3\xA9" "cut\xC3\xA9";
        return finish();
    }
    fn->tests = std::move(tests);
    const hmi::FunctionTestReport report = hmi::runFunctionTests(p, *fn, nullptr);
    std::size_t passed = 0;
    for (const auto& r : report.results) {
        CaseResult cr;
        cr.name = r.name;
        cr.call = r.call;
        cr.verdict = std::string(hmi::verdictLabel(r.verdict));
        cr.detail = r.detail;
        cr.got = r.got;
        cr.passed = r.verdict == hmi::Verdict::Passed;
        if (cr.passed) ++passed;
        o.cases.push_back(std::move(cr));
    }
    exec.state = State::Ok;
    exec.detail = std::to_string(report.results.size()) + " cas ex\xC3\xA9" "cut\xC3\xA9s, " + std::to_string(passed) + " r\xC3\xA9ussis";
    return finish();
}

// ---- C et C++ : la relecture statique, rien d'autre ---------------------------------------------------------------
void runC(const Job& job, hmi::ScriptLang lang, const hmi::Project& project, Outcome& o) {
    Stage analyse = makeStage("analyse", "V\xC3\xA9rification statique (accolades, parenth\xC3\xA8ses, cha\xC3\xAEnes, commentaires, noms du projet)");
    Stage compil = makeStage("compilation", "Compilation");
    Stage exec = makeStage("execution", "Ex\xC3\xA9" "cution");
    const auto diags = hmi::checkScript(lang, job.code, job.name);
    const std::size_t errors = countOf(diags, hmi::ScriptDiagnostic::Severity::Error);
    if (errors > 0) {
        analyse.state = State::Failed;
        analyse.detail = std::to_string(errors) + (errors > 1 ? " fautes :\n" : " faute :\n") + diagLines(diags, hmi::ScriptDiagnostic::Severity::Error);
    } else {
        analyse.state = State::Ok;
        analyse.detail = "aucune faute trouv\xC3\xA9" "e par la relecture (ce n'est pas une compilation)";
    }
    auto names = hmi::checkCNames(project, nullptr, job.code);
    if (const std::string w = diagLines(names, hmi::ScriptDiagnostic::Severity::Warning) + diagLines(names, hmi::ScriptDiagnostic::Severity::Error); !w.empty())
        analyse.detail += "\nnoms du projet :\n" + w;
    const char* rule = "jamais : le C et le C++ \xC3\xA9" "crits par l'IA ne sont ni compil\xC3\xA9s ni ex\xC3\xA9" "cut\xC3\xA9s (r\xC3\xA8gle du logiciel) - "
                       "v\xC3\xA9rification statique seulement";
    compil.state = State::Skipped;
    compil.detail = rule;
    exec.state = State::Skipped;
    exec.detail = rule;
    o.stages = {analyse, compil, exec};
}

std::string uniqueName() {
    std::random_device rd;
    std::mt19937_64 g((static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(g()));
    return buf;
}

} // namespace

std::string_view stateText(State s) noexcept {
    switch (s) {
        case State::Ok: return "r\xC3\xA9ussi";
        case State::Failed: return "\xC3\xA9" "chec";
        case State::Skipped: return "non fait";
        case State::NotRun: break;
    }
    return "non lanc\xC3\xA9";
}

const Stage* Outcome::stage(std::string_view key) const {
    for (const auto& s : stages)
        if (s.key == key) return &s;
    return nullptr;
}

bool Outcome::testsPassed() const {
    const Stage* c = stage("compilation");
    if (!c || c->state != State::Ok || cases.empty()) return false;
    return std::all_of(cases.begin(), cases.end(), [](const CaseResult& r) { return r.passed; });
}

std::string Outcome::report() const {
    std::string out = "Bac \xC3\xA0 sable (" + (language.empty() ? std::string("ST") : language) + ")";
    if (!process.empty()) out += " - processus isol\xC3\xA9 : " + process + (networkIsolated ? ", r\xC3\xA9seau coup\xC3\xA9" : "");
    out += "\n";
    if (!error.empty()) out += "NON EX\xC3\x89" "CUT\xC3\x89 : " + error + "\n";
    out += "- Code g\xC3\xA9n\xC3\xA9r\xC3\xA9 : oui (par l'IA)\n";
    for (const auto& s : stages) {
        out += "- " + s.label + " : " + std::string(stateText(s.state));
        if (!s.detail.empty()) out += " - " + s.detail;
        out += "\n";
    }
    if (!cases.empty()) {
        std::size_t ok = 0;
        for (const auto& c : cases) ok += c.passed ? 1 : 0;
        out += "- Tests : " + std::to_string(ok) + "/" + std::to_string(cases.size()) + " r\xC3\xA9ussis\n";
        for (const auto& c : cases) {
            out += "  - " + c.name + (c.call.empty() ? std::string{} : " : " + c.call) + " -> " + c.verdict;
            if (!c.got.empty()) out += " (obtenu " + c.got + ")";
            if (!c.detail.empty()) out += " - " + c.detail;
            out += "\n";
        }
    } else {
        out += "- Tests : aucun cas ex\xC3\xA9" "cut\xC3\xA9\n";
    }
    out += "- Int\xC3\xA9gration au projet : non (une proposition s\xC3\xA9par\xC3\xA9" "e, \xC3\xA0 valider)\n";
    out += "- Ex\xC3\xA9" "cution sur un automate r\xC3\xA9" "el : jamais (le bac \xC3\xA0 sable n'a aucune connexion)\n";
    return out;
}

Outcome runHere(const Job& job, const hmi::Project& project) {
    Outcome o;
    o.started = true;
    hmi::ScriptLang lang{};
    if (!languageOf(job.language, lang)) {
        o.error = "langage inconnu : " + job.language;
        o.language = job.language;
        return o;
    }
    o.language = languageName(lang);
    if (lang == hmi::ScriptLang::ST) runSt(job, project, o);
    else runC(job, lang, project, o);
    return o;
}

std::string childInput(const Job& job, const hmi::Project& project) {
    const std::string packed = hmi::rec::packProject(project);
    json::Value head = json::Value::object();
    head.set("format", kFormat);
    head.set("travail", jobValue(job));
    head.set("taille_projet", static_cast<std::int64_t>(packed.size()));
    std::string in = json::write(head, false);
    in += '\n';
    in += packed;
    return in;
}

bool readChildOutput(std::string_view out, Outcome& o, std::string* why) {
    // La derniere ligne JSON de la sortie (une bibliotheque pourrait ecrire avant).
    std::size_t end = out.find_last_not_of("\r\n ");
    if (end == std::string_view::npos) {
        if (why) *why = "aucune sortie";
        return false;
    }
    const std::size_t start = out.rfind('\n', end);
    const std::string_view line = out.substr(start == std::string_view::npos ? 0 : start + 1, end + 1 - (start == std::string_view::npos ? 0 : start + 1));
    json::Value v;
    std::string err;
    if (!json::parse(line, v, &err) || !v.isObject()) {
        if (why) *why = "sortie illisible : " + err;
        return false;
    }
    o.language = v.str("langage", o.language);
    o.stages.clear();
    for (const auto& sv : v.at("etapes").items()) {
        Stage s;
        s.key = sv.str("cle");
        s.label = sv.str("titre");
        s.state = stateFromKey(sv.str("etat"));
        s.detail = sv.str("detail");
        o.stages.push_back(std::move(s));
    }
    o.cases.clear();
    for (const auto& cv : v.at("cas").items()) {
        CaseResult c;
        c.name = cv.str("nom");
        c.call = cv.str("appel");
        c.verdict = cv.str("verdict");
        c.detail = cv.str("detail");
        c.got = cv.str("obtenu");
        c.passed = cv.flag("reussi", false);
        o.cases.push_back(std::move(c));
    }
    if (const std::string e = v.str("erreur"); !e.empty()) o.error = e;
    return true;
}

int childMain() {
#ifdef _WIN32
    // Un plantage de l'enfant ne doit pas ouvrir de fenetre (le parent le voit et le dit).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string in((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Outcome o;
    const auto emit = [&](int code) {
        const std::string line = json::write(outcomeValue(o), false);
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return code;
    };
    const std::size_t nl = in.find('\n');
    json::Value head;
    std::string err;
    if (nl == std::string::npos || !json::parse(std::string_view(in).substr(0, nl), head, &err) || !head.isObject()) {
        o.error = "travail illisible" + (err.empty() ? std::string{} : " : " + err);
        return emit(2);
    }
    Job job;
    if (!jobFromValue(head.at("travail"), job, &err)) {
        o.error = err;
        return emit(2);
    }
    hmi::Project project;
    const std::int64_t size = head.integer("taille_projet", 0);
    if (size > 0) {
        if (static_cast<std::size_t>(size) > kMaxProjectBytes || in.size() - nl - 1 < static_cast<std::size_t>(size)) {
            o.error = "copie du projet incompl\xC3\xA8te";
            return emit(2);
        }
        std::string why;
        if (!hmi::rec::unpackProject(std::string_view(in).substr(nl + 1, static_cast<std::size_t>(size)), project, &why)) {
            o.error = "copie du projet illisible : " + why;
            return emit(2);
        }
    }
    in.clear();
    in.shrink_to_fit();
    // Les fonctions de fichiers des scripts : le dossier de travail seulement (le parent l'a cree, vide).
    std::error_code ec;
    hmi::confineFiles(fs::current_path(ec).string());
    o = runHere(job, project);
    return emit(0);
}

Outcome run(const Job& job, const hmi::Project& project, const std::atomic<bool>* cancel, std::string program) {
    Outcome o;
    o.language = job.language;
    if (program.empty()) program = core::process::selfPath();
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("xpg-bac-a-sable-" + uniqueName());
    if (ec || !fs::create_directories(dir, ec)) {
        o.error = "dossier de travail impossible : " + ec.message();
        journal(job, o);
        return o;
    }
    core::process::Request rq;
    rq.program = program;
    rq.args = {std::string(kChildFlag)};
    rq.workDir = dir.string();
    rq.input = childInput(job, project);
    rq.limits.seconds = std::clamp(job.seconds, 1.0, 60.0);
    rq.limits.memoryBytes = 1024u * 1024u * 1024u;
    rq.limits.outputBytes = 4u * 1024u * 1024u;
    rq.isolateNetwork = true;
    const core::process::Result r = core::process::run(rq, cancel);
    fs::remove_all(dir, ec);   // le dossier de travail et ce que le code y a ecrit
    o.started = r.started;
    o.networkIsolated = r.networkIsolated;
    o.process = r.summary();
    if (!r.started) {
        o.error = r.error.empty() ? std::string("le processus n'a pas d\xC3\xA9marr\xC3\xA9") : r.error;
    } else if (r.cancelled) {
        o.error = "arr\xC3\xAAt\xC3\xA9 \xC3\xA0 ta demande";
    } else {
        std::string why;
        const bool read = readChildOutput(r.out, o, &why);
        if (r.timedOut || r.memoryExceeded || r.crashed) {
            o.error = o.process + (read ? std::string{} : " (aucun r\xC3\xA9sultat)");
            for (auto& s : o.stages)
                if (s.state == State::NotRun) s.state = State::Failed;
        } else if (!read) {
            o.error = why;
        }
    }
    journal(job, o);
    return o;
}

std::string toolJson() {
    json::Value t = json::Value::object();
    t.set("name", std::string(kTool));
    t.set("description",
          "V\xC3\xA9rifie et essaie une fonction dans un bac \xC3\xA0 sable isol\xC3\xA9 (un processus \xC3\xA0 part, sans automate, sans "
          "r\xC3\xA9seau, sans acc\xC3\xA8s aux fichiers de l'utilisateur) et rend le r\xC3\xA9sultat R\xC3\x89" "EL de chaque \xC3\xA9tape. "
          "ST : analyse syntaxique, compilation (noms, types, appels du projet), puis ex\xC3\xA9" "cution des cas donn\xC3\xA9s sur un banc neuf "
          "(une simulation). C et C++ : relecture statique seulement, jamais compil\xC3\xA9s ni ex\xC3\xA9" "cut\xC3\xA9s. Rien n'est ajout\xC3\xA9 au "
          "projet : pour l'int\xC3\xA9grer, propose ensuite l'op\xC3\xA9ration (creer_fonction). Corrige et relance si une \xC3\xA9tape \xC3\xA9" "choue "
          "(5 essais au plus par demande). Ne dis jamais qu'un code fonctionne parce qu'il compile : seuls les cas r\xC3\xA9ussis le montrent.");
    json::Value props = json::Value::object();
    const auto str = [](const char* d) {
        json::Value v = json::Value::object();
        v.set("type", "string");
        v.set("description", d);
        return v;
    };
    json::Value lang = str("ST (par d\xC3\xA9" "faut), C ou C++.");
    json::Value en = json::Value::array();
    en.push("ST");
    en.push("C");
    en.push("C++");
    lang.set("enum", std::move(en));
    props.set("langage", std::move(lang));
    props.set("nom", str("Le nom de la fonction (une fonction du projet du m\xC3\xAAme nom : son code est remplac\xC3\xA9 dans la copie essay\xC3\xA9" "e)."));
    props.set("retour", str("Le type de retour (REAL, INT, BOOL, STRING...) ; vide : une proc\xC3\xA9" "dure."));
    props.set("code", str("ST : VAR_INPUT ... END_VAR, VAR ... END_VAR puis le calcul ; le r\xC3\xA9sultat s'affecte au nom de la fonction."));
    json::Value existing = json::Value::object();
    existing.set("type", "boolean");
    existing.set("description", "Vrai : essayer la fonction du projet telle qu'elle est (code vide), avec ces cas ou les siens.");
    props.set("fonction_existante", std::move(existing));
    json::Value caseProps = json::Value::object();
    caseProps.set("nom", str("Le nom du cas."));
    json::Value args = json::Value::object();
    args.set("type", "array");
    args.set("description", "Un argument par param\xC3\xA8tre d'entr\xC3\xA9" "e, dans l'ordre, en texte (2.5, TRUE, 'Pompe 3').");
    json::Value argItems = json::Value::object();
    argItems.set("type", "string");
    args.set("items", std::move(argItems));
    caseProps.set("arguments", std::move(args));
    caseProps.set("attendu", str("Le retour attendu (vide : une proc\xC3\xA9" "dure)."));
    caseProps.set("tolerance", str("L'\xC3\xA9" "cart permis sur un REAL (vide : exact)."));
    caseProps.set("erreur", str("Le code d'erreur attendu, si le cas doit lever une erreur."));
    json::Value caseObj = json::Value::object();
    caseObj.set("type", "object");
    caseObj.set("properties", std::move(caseProps));
    json::Value cases = json::Value::object();
    cases.set("type", "array");
    cases.set("description", "Les cas d'essai (50 au plus). Sans cas : rien n'est ex\xC3\xA9" "cut\xC3\xA9.");
    cases.set("items", std::move(caseObj));
    props.set("cas", std::move(cases));
    json::Value schema = json::Value::object();
    schema.set("type", "object");
    schema.set("properties", std::move(props));
    json::Value req = json::Value::array();
    req.push("nom");
    schema.set("required", std::move(req));
    t.set("input_schema", std::move(schema));
    return json::write(t, false);
}

bool jobFromJson(std::string_view inputJson, Job& out, std::string* why) {
    json::Value v;
    std::string err;
    if (!json::parse(inputJson, v, &err)) {
        if (why) *why = "entr\xC3\xA9" "e illisible : " + err;
        return false;
    }
    return jobFromValue(v, out, why);
}

void journal(const Job& job, const Outcome& o) {
    const std::string dir = app::ai::logsDir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    json::Value v = json::Value::object();
    v.set("date", nowText());
    v.set("langage", job.language);
    v.set("nom", job.name);
    json::Value st = json::Value::object();
    for (const auto& s : o.stages) st.set(s.key, stateKey(s.state));
    v.set("etapes", std::move(st));
    std::size_t ok = 0;
    for (const auto& c : o.cases) ok += c.passed ? 1 : 0;
    v.set("cas", static_cast<std::int64_t>(o.cases.size()));
    v.set("reussis", static_cast<std::int64_t>(ok));
    v.set("processus", o.process);
    if (!o.error.empty()) v.set("erreur", o.error);
    std::ofstream f(fs::path(dir) / "bac-a-sable.jsonl", std::ios::binary | std::ios::app);
    if (f) f << json::write(v, false) << '\n';
}

} // namespace app::ai::sandbox
