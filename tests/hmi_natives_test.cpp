// =============================================================================
//  tests/hmi_natives_test.cpp - 1.12.0 : LES NATIVES DE L'IHM
// -----------------------------------------------------------------------------
//  Chaque exemple du catalogue (outils/natives/catalogue.py -> HmiNativesData.cpp)
//  s'execute dans le moteur de l'IHM, comme en simulation : dans un script ST,
//  dans une expression et dans un texte a trous (s'il n'a pas de declarations),
//  et donne ce que dit sa fiche. Puis : rien ne manque au catalogue (chaque
//  fonction que Compiler accepte y est), chaque conversion X_TO_Y s'execute, les
//  litteraux de chaque type se lisent, les enumerations natives s'ecrivent et
//  arrivent aux fonctions IHM_, les couleurs et l'aleatoire font ce qu'ils disent.
//
//  NATIVES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNatives.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hmi;
namespace nt = hmi::natives;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("NATIVES_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

bool sameValue(const std::string& got, std::string_view want) {
    char* end = nullptr;
    const std::string w(want);
    const double b = std::strtod(w.c_str(), &end);
    if (!w.empty() && end && *end == '\0') {
        char* e2 = nullptr;
        const double a = std::strtod(got.c_str(), &e2);
        if (!got.empty() && e2 && *e2 == '\0') return std::fabs(a - b) <= 1e-4 * std::max(1.0, std::fabs(b));
    }
    return got == w;
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

// Le projet d'essai : les variables des exemples, une vue, un script appele.
struct Bench {
    Project p;
    Runtime rt;
    explicit Bench(const std::string& resultType, const std::string& body) {
        const auto var = [this](const char* n, const std::string& t, const char* init) {
            Variable v;
            v.id = p.allocate();
            v.name = n;
            v.type = t;
            v.initial = init;
            p.programs.variables.push_back(v);
        };
        var("R", resultType.empty() ? "STRING" : resultType, "");
        var("Niveau", "REAL", "42.5");
        var("Compteur", "INT", "7");
        var("Texte", "STRING", "'Pompe 3'");
        var("Marche", "BOOL", "TRUE");
        var("Mot", "WORD", "16#00F0");
        var("Tab", "ARRAY[1..5] OF INT", "");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
    }
    std::string runScript(std::string* error) {
        std::string err;
        const bool ok = rt.callScript("Essai", 0.2, &err);
        if (error) *error = ok ? std::string{} : err;
        sim::Value out;
        return rt.environment().read("R", out) ? shown(out) : std::string("#");
    }
};

// Le type de la variable R d'un exemple : celui d'une variable IHM (SINT, LINT... n'en sont pas).
std::string benchType(std::string_view t) {
    const std::string s(t);
    if (s == "SINT" || s == "USINT" || s == "BYTE") return "INT";
    if (s == "LINT") return "DINT";
    if (s == "ULINT" || s == "LWORD") return "UDINT";
    return s;
}

void runExample(const std::string& label, const nt::Example& ex) {
    if (ex.empty()) return;
    // 1. dans un script
    if (!ex.prelude.empty() && ex.prelude.find("RANDOM_SEED") == std::string_view::npos) {
        // des declarations : le script seul
    }
    {
        Bench b(benchType(ex.type), std::string(ex.prelude) + (ex.prelude.empty() ? "" : "\n") + "R := " + std::string(ex.expression) + ";");
        std::string err;
        const std::string got = b.runScript(&err);
        check(err.empty() && sameValue(got, ex.expected),
              label + " (script) : " + std::string(ex.expression) + " = " + got + (err.empty() ? "" : " [" + err + "]") + ", attendu "
                  + std::string(ex.expected));
    }
    if (!ex.prelude.empty()) return;          // des declarations : pas d'expression
    // 2. dans une expression de vue
    {
        Bench b(benchType(ex.type), "R := 0;");
        const auto e = Expression::compile(ex.expression);
        std::string got, err;
        if (!e.valid()) err = e.error();
        else if (auto r = e.evaluate(b.rt.environment())) got = shown(*r);
        else err = r.error().message();
        check(err.empty() && sameValue(got, ex.expected),
              label + " (expression) : " + std::string(ex.expression) + " = " + got + (err.empty() ? "" : " [" + err + "]"));
        // 3. dans un texte a trous
        const std::string text = TextTemplate::compile("{" + std::string(ex.expression) + "}").render(b.rt.environment());
        check(sameValue(text, ex.expected), label + " (texte a trous) : {" + std::string(ex.expression) + "} = " + text);
    }
}

// Compiler (le controle d'un script) accepte l'exemple ST de la fiche.
void compiles(const std::string& label, std::string_view st, bool scriptLevel) {
    if (st.empty() || st.find("...") != std::string_view::npos || st.find("\xE2\x80\xA6") != std::string_view::npos) return;
    std::string body(st);
    if (scriptLevel && body.find("VAR") == std::string::npos) {
        // les noms des exemples : des variables IHM pour le controle
    }
    const auto diags = checkScript(ScriptLang::ST, body, "Essai");
    std::string errors;
    for (const auto& d : diags)
        if (d.severity == ScriptDiagnostic::Severity::Error && d.message.find("inconnu") == std::string::npos
            && d.message.find("n'existe pas") == std::string::npos)
            errors += " | " + d.message;
    check(errors.empty(), label + " : l'exemple ST se lit" + errors);
}

void catalogueComplet() {
    std::printf("-- le catalogue : chaque fonction que Compiler accepte y est\n");
    const auto has = [](std::string_view n) { return nt::function(n) != nullptr; };
    // Les fonctions standard et du dialecte, que Compiler et le moteur connaissent.
    for (const char* n : {"ABS", "SQRT", "LN", "LOG", "EXP", "EXPT", "SIN", "COS", "TAN", "ASIN", "ACOS", "ATAN", "MIN", "MAX", "LIMIT", "SEL",
                          "MUX", "TRUNC", "ROUND", "LEN", "LEFT", "RIGHT", "MID", "CONCAT", "INSERT", "DELETE", "REPLACE", "FIND", "SHL",
                          "SHR", "ROL", "ROR", "NEG", "MAP_HAS", "MAP_GET", "MAP_REMOVE", "MAP_SIZE", "MAP_CLEAR", "MAP_KEYS", "MAP_BEGIN",
                          "MAP_NEXT", "MAP_END", "REF", "ADR", "LOWER_BOUND", "UPPER_BOUND", "SIZEOF", "TO_UPPER", "TO_LOWER"}) {
        check(has(n), std::string("dans le catalogue : ") + n);
        check(isStandardFunction(n) || std::string_view(n).rfind("MAP_", 0) == 0 || std::string_view(n) == "REF" || std::string_view(n) == "ADR"
                  || std::string_view(n).find("BOUND") != std::string_view::npos || std::string_view(n) == "SIZEOF" || std::string_view(n).rfind("TO_", 0) == 0,
              std::string("connue du moteur : ") + n);
    }
    // Toutes les fonctions IHM_ que Compiler connait.
    for (const char* n : {"IHM_NAVIGUER", "IHM_POPUP", "IHM_CHANGER_POPUP", "IHM_POPUP_PRECEDENTE", "IHM_CENTRER_POPUP", "IHM_FERMER_POPUP",
                          "IHM_FERMER_POPUPS", "IHM_POPUP_OUVERTE", "IHM_JOURNAL", "IHM_APPELER", "IHM_SON", "IHM_VUE", "IHM_TEMPS",
                          "IHM_UTILISATEUR", "IHM_NOM_UTILISATEUR", "IHM_GROUPE", "IHM_NIVEAU", "IHM_DECONNECTER", "IHM_PARAMETRES_SYSTEME",
                          "IHM_METTRE_DE_COTE", "IHM_REMETTRE", "IHM_FAIRE_TAIRE", "IHM_EXPORTER", "IHM_PRECEDENTE", "IHM_SUIVANTE",
                          "IHM_ACCUEIL", "IHM_MENU_CONNEXION", "IHM_LANGUE", "IHM_THEME", "IHM_GIF_JOUER", "IHM_GIF_PAUSE",
                          "IHM_GIF_ARRETER", "IHM_GIF_REJOUER", "IHM_EQUIPEMENT_OK", "IHM_EQUIPEMENT_PING", "IHM_ESCLAVE_SIMULE", "IHM_LOG"}) {
        check(has(n), std::string("dans le catalogue : ") + n);
        check(isHmiFunction(n), std::string("connue de Compiler : ") + n);
    }
    // Et l'inverse : chaque fonction IHM_ du catalogue est connue de Compiler.
    std::size_t ihm = 0;
    for (const auto& f : nt::functions())
        if (f.name.rfind("IHM_", 0) == 0) {
            ++ihm;
            check(isHmiFunction(f.name), "la fonction du catalogue est connue de Compiler : " + std::string(f.name));
        }
    check(ihm == 37, "37 fonctions IHM_ (" + std::to_string(ihm) + ")");
    // Chaque fonction : sa categorie existe, sa syntaxe en C et C++ est dite.
    for (const auto& f : nt::functions()) {
        check(nt::category(f.category) != nullptr, std::string(f.name) + " : categorie " + std::string(f.category));
        check(!f.st.empty() && !f.c.empty() && !f.cpp.empty() && !f.summary.empty(), std::string(f.name) + " : ST, C, C++ et la phrase");
        if (f.name.rfind("IHM_", 0) == 0) check(f.inC == nt::Avail::Same && f.inCpp == nt::Avail::Same, std::string(f.name) + " : meme nom en C et C++");
    }
    std::printf("   %zu fonctions, %zu categories, %zu enumerations natives\n", nt::functions().size(), nt::categories().size(), nt::enums().size());
}

void exemples() {
    std::printf("-- chaque exemple des fiches, execute\n");
    for (const auto& f : nt::functions()) {
        runExample(std::string(f.name), f.example);
        compiles(std::string(f.name), f.st, true);
    }
    for (const auto& o : nt::operators()) runExample("operateur " + std::string(o.symbol), o.example);
}

void conversions() {
    std::printf("-- les 272 conversions X_TO_Y\n");
    const auto all = nt::conversions();
    check(all.size() == 272, "272 conversions (" + std::to_string(all.size()) + ")");
    const std::map<std::string, std::string> sample = {
        {"BOOL", "TRUE"}, {"SINT", "5"}, {"INT", "7"}, {"DINT", "100000"}, {"LINT", "9"}, {"USINT", "200"}, {"UINT", "60000"},
        {"UDINT", "70000"}, {"ULINT", "11"}, {"BYTE", "16#41"}, {"WORD", "16#00F0"}, {"DWORD", "16#0001_0000"}, {"LWORD", "16#FF"},
        {"REAL", "2.6"}, {"LREAL", "3.5"}, {"STRING", "'12'"}, {"TIME", "T#1500ms"}};
    Bench b("STRING", "R := '';");
    int ok = 0;
    for (const auto& c : all) {
        const std::string expr = c.name + "(" + sample.at(c.from) + ")";
        const auto e = Expression::compile(expr);
        const auto r = e.valid() ? e.evaluate(b.rt.environment()) : core::Result<sim::Value>(core::fail(core::ErrorCode::InvalidArgument, e.error()));
        if (r) ++ok;
        else check(false, "conversion " + expr + " : " + r.error().message());
        check(!c.c.empty() && !c.cpp.empty() && !c.behaviour.empty(), c.name + " : C, C++ et ce qu'elle fait");
        int mn = 0, mx = 0;
        check(nt::arity(c.name, mn, mx) && mn == 1 && mx == 1, c.name + " : un argument");
    }
    check(ok == 272, "toutes les conversions s'executent (" + std::to_string(ok) + ")");
    // Des valeurs precises.
    const std::vector<std::pair<std::string, std::string>> exact = {
        {"INT_TO_SINT(200)", "-56"}, {"INT_TO_USINT(300)", "44"}, {"REAL_TO_INT(2.5)", "3"}, {"DINT_TO_INT(100000)", "-31072"},
        {"STRING_TO_INT('12')", "12"}, {"INT_TO_LREAL(7)", "7"}, {"TO_SINT(200)", "-56"}, {"TO_LWORD(7)", "7"}, {"BOOL_TO_INT(TRUE)", "1"}};
    for (const auto& [x, want] : exact) {
        const auto r = Expression::compile(x).evaluate(b.rt.environment());
        check(r && sameValue(shown(*r), want), x + " = " + (r ? shown(*r) : r.error().message()) + ", attendu " + want);
    }
}

void litteraux() {
    std::printf("-- les litteraux de chaque type\n");
    Bench b("STRING", "R := '';");
    const std::set<std::string> refuses = {};
    for (const auto& t : nt::typeExtras())
        for (const auto lit : t.literals) {
            const auto e = Expression::compile(lit);
            const auto r = e.valid() ? e.evaluate(b.rt.environment()) : core::Result<sim::Value>(core::fail(core::ErrorCode::InvalidArgument, e.error()));
            check(static_cast<bool>(r), std::string(t.name) + " : le litteral " + std::string(lit) + " se lit" + (r ? " (" + shown(*r) + ")" : " [" + r.error().message() + "]"));
        }
    const std::vector<std::pair<std::string, std::string>> exact = {
        {"INT#-3", "-3"}, {"DINT#-5", "-5"}, {"REAL#1.5", "1.5"}, {"LREAL#2.25", "2.25"}, {"BOOL#1", "TRUE"}, {"BOOL#0", "FALSE"},
        {"3000000000", "3000000000"}, {"16#FFFF_FFFF", "4294967295"}, {"-2147483648", "-2147483648"}, {"16#7FFF_FFFF", "2147483647"}};
    for (const auto& [x, want] : exact) {
        const auto r = Expression::compile(x).evaluate(b.rt.environment());
        check(r && sameValue(shown(*r), want), "litteral " + x + " = " + (r ? shown(*r) : r.error().message()) + ", attendu " + want);
    }
    check(!Expression::compile("INT#1.5").valid(), "INT#1.5 est refuse (un INT n'a pas de decimales)");
}

void typesDuRegistre() {
    std::printf("-- les types : le registre et le catalogue\n");
    const auto cards = nt::typeCards();
    check(cards.size() == 17, "17 types de base (" + std::to_string(cards.size()) + ")");
    for (const auto& c : cards) {
        check(!c.minText.empty() && !c.maxText.empty(), c.name + " : MIN et MAX (" + c.minText + " / " + c.maxText + ")");
        check(!c.cType.empty() && !c.cppType.empty() && !c.defaultValue.empty(), c.name + " : C, C++, defaut");
        check(!c.literals.empty(), c.name + " : des litteraux");
    }
    const auto i = nt::typeCard("INT");
    check(i && i->minText == "-32\xE2\x80\xAF" "768" && i->maxText == "32\xE2\x80\xAF" "767" && i->modbus == "1 mot (%MW)",
          "INT : -32 768 a 32 767, un mot");
    const auto w = nt::typeCard("WORD");
    check(w && w->maxText.rfind("16#FFFF", 0) == 0, "WORD : 16#FFFF");
}

void enumerations() {
    std::printf("-- les enumerations natives\n");
    const nt::NativeEnum* e = nullptr;
    const nt::EnumValue* v = nullptr;
    check(nt::parseEnumLiteral("TRANSITION#Fondu", &e, &v) && v && v->number == 1, "TRANSITION#Fondu vaut 1");
    check(nt::parseEnumLiteral(" position_popup#objet ", &e, &v) && v && v->argument == "objet", "position_popup#objet (sans casse) : 'objet'");
    check(!nt::parseEnumLiteral("TRANSITION#Glisse", &e, &v), "TRANSITION#Glisse n'existe pas");
    // Dans un script : la valeur, et la fonction IHM_ qui recoit le mot.
    {
        Bench b("DINT", "R := POSITION_POPUP#HautDroite;");
        std::string err;
        check(b.runScript(&err) == "3" && err.empty(), "POSITION_POPUP#HautDroite vaut 3 dans un script" + (err.empty() ? "" : " [" + err + "]"));
    }
    {
        Bench b("BOOL", "R := IHM_THEME(THEME_IHM#Jour);");
        std::string err;
        const std::string got = b.runScript(&err);
        check(err.empty() && got == "TRUE", "IHM_THEME(THEME_IHM#Jour) : le theme jour (" + got + ")");
        sim::Value theme;
        check(b.rt.environment().read("SYS.Theme", theme) && shown(theme) == "jour", "SYS.Theme = jour (" + shown(theme) + ")");
    }
    {
        Bench b("BOOL", "R := IHM_LOG(NIVEAU_LOG#WARNING, 'essai');");
        std::string err;
        check(b.runScript(&err) == "TRUE" && err.empty(), "IHM_LOG(NIVEAU_LOG#WARNING, ...)");
    }
    // Compiler : une valeur inconnue est signalee.
    Project p;
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto diags = scriptcheck::check(sc, "IHM_NAVIGUER('Vue_A', TRANSITION#Glisse);");
    bool said = false;
    for (const auto& d : diags) said = said || d.message.find("TRANSITION#Glisse n'existe pas") != std::string::npos;
    check(said, "Compiler : TRANSITION#Glisse n'existe pas (veux-tu dire TRANSITION#Glissement ?)");
    for (const auto& en : nt::enums()) check(!en.values.empty() && !en.summary.empty(), std::string(en.name) + " : ses valeurs et sa phrase");
}

void couleursEtHasard() {
    std::printf("-- les couleurs et l'aleatoire\n");
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    check(nt::parseColor("#2ECC71", r, g, b, a) && r == 46 && g == 204 && b == 113 && a == 255, "#2ECC71 se lit");
    check(nt::parseColor("16#2ECC7180", r, g, b, a) && a == 128, "16#2ECC7180 : l'opacite en dernier");
    check(!nt::parseColor("vert", r, g, b, a), "'vert' n'est pas une couleur");
    sim::Value out;
    std::string why;
    check(!nt::call("RGB", {sim::Value::integer(sim::Type::Int, 1)}, out, &why) && !why.empty(), "RGB(1) : il manque des arguments (" + why + ")");
    check(nt::call("RGB", {sim::Value::integer(sim::Type::Int, 300), sim::Value::integer(sim::Type::Int, -5), sim::Value::real(127.6)}, out)
              && shown(out) == "#FF0080", "RGB(300, -5, 127.6) : bornes et arrondi (" + shown(out) + ")");
    check(!nt::call("COULEUR_ROUGE", {sim::Value::text("bleu")}, out, &why) && why.find("illisible") != std::string::npos,
          "COULEUR_ROUGE('bleu') : couleur illisible");
    // La suite rejouee.
    nt::seedRandom(7);
    std::vector<std::int64_t> first;
    for (int i = 0; i < 5; ++i) {
        (void)nt::call("RANDOM_INT", {sim::Value::integer(sim::Type::DInt, 1), sim::Value::integer(sim::Type::DInt, 6)}, out);
        first.push_back(out.asInteger());
        check(out.asInteger() >= 1 && out.asInteger() <= 6, "RANDOM_INT(1, 6) dans 1..6 (" + shown(out) + ")");
    }
    nt::seedRandom(7);
    bool same = true;
    for (int i = 0; i < 5; ++i) {
        (void)nt::call("RANDOM_INT", {sim::Value::integer(sim::Type::DInt, 1), sim::Value::integer(sim::Type::DInt, 6)}, out);
        same = same && out.asInteger() == first[static_cast<std::size_t>(i)];
    }
    check(same, "RANDOM_SEED : la meme graine rejoue la meme suite");
    for (int i = 0; i < 200; ++i) {
        (void)nt::call("RANDOM", {}, out);
        if (out.asReal() < 0.0 || out.asReal() >= 1.0) { check(false, "RANDOM() hors de [0, 1[ : " + shown(out)); break; }
    }
    // Compiler : RGB a trois arguments.
    Project p;
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto diags = scriptcheck::check(sc, "VAR Fond : STRING; END_VAR\nFond := RGB(1, 2);");
    bool said = false;
    for (const auto& d : diags) said = said || d.message.find("RGB prend 3 arguments") != std::string::npos;
    check(said, "Compiler : RGB prend 3 arguments, pas 2");
}

} // namespace

int main() {
    std::printf("1.12.0 : les natives de l'IHM (catalogue, exemples, conversions, types, enumerations, couleurs, aleatoire)\n");
    catalogueComplet();
    exemples();
    conversions();
    litteraux();
    typesDuRegistre();
    enumerations();
    couleursEtHasard();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
