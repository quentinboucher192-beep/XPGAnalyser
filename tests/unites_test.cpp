// =============================================================================
//  tests/unites_test.cpp - 1.13.0 (lot J6) : les unites de mesure dans les types
// -----------------------------------------------------------------------------
//  Une unite physique (bar, mbar, degC, m3/h, %) sur une variable ou un membre :
//    - la bibliotheque (hmi::units) : familles, conversion affine, verdict ;
//    - le controle STATIQUE (exprcheck) : + / - / comparaison exigent la meme
//      unite (sinon EN), l'affectation convertit d'office (ou exige EN en mode
//      strict), les familles differentes refusent ; EN / UNITE / SANS_UNITE ;
//    - Generer : une unite ne va que sur un nombre, un symbole connu ;
//    - la serialisation : unite= et la bibliotheque du projet, format 32 en
//      escalier (un projet sans unite reste au format d'avant) ;
//    - le simulateur : EN convertit, UNITE rend le symbole, SANS_UNITE l'enleve.
//
//  UNITES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiUnits.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("UNITES_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  ECHEC  %s\n", what.c_str()); }
    else if (g_obs) std::printf("  ok     %s\n", what.c_str());
}

// Un probleme d'exprcheck dont le message contient ces mots (et n'est pas un avertissement, sauf `warn`).
bool has(const std::vector<exprcheck::Problem>& ps, std::string_view text, bool warn = false) {
    for (const auto& p : ps)
        if (p.message.find(text) != std::string::npos && p.warning == warn) return true;
    return false;
}

Variable var(Project& p, const char* name, const char* type, const char* unit, const char* initial = "0") {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.unit = unit;
    v.initial = initial;
    p.programs.variables.push_back(v);
    return v;
}

exprcheck::Context ctxOf(const Project& p) {
    exprcheck::Context c;
    c.project = &p;
    c.calls = false;                 // la forme et les unites seulement (les appels sont juges ailleurs)
    return c;
}

// ---------------------------------------------------------------- la bibliotheque ---
void bibliotheque() {
    std::printf("-- hmi::units : familles, conversion affine, verdict, type numerique\n");
    units::Catalog cat;
    check(cat.familyOf("bar") == "pression" && cat.familyOf("mbar") == "pression", "bar et mbar : famille pression");
    check(cat.familyOf("degC") == "temperature" && cat.familyOf("K") == "temperature", "degC et K : famille temperature");
    check(cat.familyOf("m3/h") == "debit", "m3/h : famille debit");
    check(cat.familyOf("Nm3/h") == "debit_normalise", "Nm3/h : une famille a part (debit normalise)");
    check(cat.familyOf("inconnue").empty(), "une unite inconnue : pas de famille");

    check(cat.relation("bar", "bar") == units::Rel::Same, "bar vs bar : identique");
    check(cat.relation("bar", "mbar") == units::Rel::SameFamily, "bar vs mbar : meme famille");
    check(cat.relation("bar", "degC") == units::Rel::OtherFamily, "bar vs degC : familles differentes");
    check(cat.relation("m3/h", "Nm3/h") == units::Rel::OtherFamily, "m3/h vs Nm3/h : familles differentes (non convertible)");
    check(cat.relation("", "bar") == units::Rel::None, "une sans unite : None (le nu adopte l'autre)");
    check(cat.relation("bar", "xyz") == units::Rel::Unknown, "un symbole inconnu : Unknown");

    const auto mbarToBar = cat.convert("mbar", "bar");
    check(mbarToBar && std::fabs(mbarToBar->factor - 0.001) < 1e-9 && std::fabs(mbarToBar->offset) < 1e-9, "mbar -> bar : x 0,001");
    check(mbarToBar && std::fabs(mbarToBar->apply(1000.0) - 1.0) < 1e-9, "1000 mbar = 1,0 bar");
    const auto cToK = cat.convert("degC", "K");
    check(cToK && std::fabs(cToK->apply(0.0) - 273.15) < 1e-9, "0 degC = 273,15 K (affine : + 273,15)");
    const auto cToF = cat.convert("degC", "degF");
    check(cToF && std::fabs(cToF->apply(100.0) - 212.0) < 1e-6, "100 degC = 212 degF");
    check(!cat.convert("bar", "degC"), "bar -> degC : pas de conversion (familles differentes)");
    check(!cat.convert("m3/h", "Nm3/h"), "m3/h -> Nm3/h : pas de conversion (on ne ment pas sur les conditions normales)");

    check(units::typeTakesUnit("REAL") && units::typeTakesUnit("INT") && units::typeTakesUnit("LREAL"), "une unite va sur REAL, INT, LREAL");
    check(units::typeTakesUnit("ARRAY[1..3] OF REAL"), "une unite va sur un ARRAY OF REAL (l'element)");
    check(!units::typeTakesUnit("BOOL") && !units::typeTakesUnit("STRING") && !units::typeTakesUnit("T_Ligne"),
          "pas d'unite sur un BOOL, un STRING, une structure");
    check(units::withUnit("REAL", "bar") == "REAL [bar]" && units::withUnit("REAL", "") == "REAL", "la forme combinee : REAL [bar]");

    // Les alias, sans casse ni accents.
    check(cat.canonical("DEGC") == "degC" && cat.canonical("m3h") == "m3/h" && cat.canonical("celsius") == "degC",
          "les alias : DEGC, m3h, celsius se replient sur l'unite fournie");

    // La bibliotheque du projet : une unite ajoutee.
    Config cfg;
    cfg.unitFamilies.push_back({"angle", "deg", "Angle"});
    cfg.units.push_back({"deg", "angle", 1.0, 0.0, "degre"});
    cfg.units.push_back({"rad", "angle", 57.29577951, 0.0, "radian"});
    units::Catalog pcat(cfg);
    check(pcat.familyOf("deg") == "angle" && pcat.relation("deg", "rad") == units::Rel::SameFamily,
          "le projet ajoute une famille et ses unites");
    check(pcat.familyOf("bar") == "pression", "les fournies restent la, a cote de celles du projet");
}

// ---------------------------------------------------------------- l'addition / comparaison ---
void additionComparaison() {
    std::printf("-- exprcheck : + - comparaison exigent la meme unite exacte\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Temperature", "REAL", "degC");
    var(p, "NombreNu", "REAL", "");
    const auto ctx = ctxOf(p);

    check(has(exprcheck::check(ctx, "Pression - PressionBasse"), "ne s'additionnent pas"),
          "bar - mbar : REFUSE (meme famille, unites differentes)");
    check(has(exprcheck::check(ctx, "Pression > PressionBasse"), "ne se comparent pas"),
          "bar > mbar : REFUSE");
    check(has(exprcheck::check(ctx, "Pression > Temperature"), "deux familles"),
          "pression vs temperature : REFUSE (familles differentes, la raison nomme les deux)");
    check(!has(exprcheck::check(ctx, "Pression - EN(PressionBasse, 'bar')"), "ne s'additionnent pas"),
          "Pression - EN(PressionBasse, 'bar') : bon (converti)");
    check(!has(exprcheck::check(ctx, "Pression > 9.0"), "ne se comparent pas"),
          "Pression > 9.0 : bon (le litteral adopte l'unite)");
    check(!has(exprcheck::check(ctx, "NombreNu + Pression"), "ne s'additionnent pas"),
          "un nombre nu + une pression : bon (le nu adopte l'unite)");

    // La correction d'un clic : inserer EN sur l'operande de droite.
    const auto ps = exprcheck::check(ctx, "Pression - PressionBasse");
    bool fix = false;
    for (const auto& q : ps) if (q.fixFrom == "PressionBasse" && q.fixTo == "EN(PressionBasse, 'bar')") fix = true;
    check(fix, "la correction d'un clic : PressionBasse -> EN(PressionBasse, 'bar')");
}

// ---------------------------------------------------------------- la propagation ---
void propagation() {
    std::printf("-- exprcheck : l'unite se propage (produit, division, EN, retour)\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "Debit", "REAL", "m3/h");
    var(p, "DebitMax", "REAL", "m3/h");
    const auto ctx = ctxOf(p);

    check(exprcheck::shapeOf(ctx, "Pression").unit == "bar", "une variable porte son unite");
    check(exprcheck::shapeOf(ctx, "Pression * 1.5").unit == "bar", "x par un nombre nu : garde l'unite");
    check(exprcheck::shapeOf(ctx, "Debit / DebitMax").unit.empty(), "m3/h / m3/h : SANS unite (un rapport)");
    check(exprcheck::shapeOf(ctx, "EN(Pression, 'mbar')").unit == "mbar", "EN rend l'unite cible");
    check(has(exprcheck::check(ctx, "Pression * Debit"), "l'unit\xC3\xA9 est perdue", true),
          "bar x m3/h : l'unite est perdue (une info)");
}

// ---------------------------------------------------------------- EN / UNITE / SANS_UNITE (statique) ---
void natives() {
    std::printf("-- exprcheck : EN / UNITE / SANS_UNITE (controle statique)\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "NombreNu", "REAL", "");
    const auto ctx = ctxOf(p);

    check(has(exprcheck::check(ctx, "EN(NombreNu, 'bar')"), "demande une valeur qui porte une unit"),
          "EN d'une valeur sans unite : refuse");
    check(has(exprcheck::check(ctx, "EN(Pression, 'xyz')"), "unit\xC3\xA9 inconnue"),
          "EN vers une unite inconnue : refuse");
    check(has(exprcheck::check(ctx, "EN(Pression, 'degC')"), "on ne convertit pas"),
          "EN(Pression, 'degC') : refuse (on ne convertit pas des bar en degC)");
    check(!has(exprcheck::check(ctx, "EN(Pression, 'mbar')"), "on ne convertit pas")
              && !has(exprcheck::check(ctx, "EN(Pression, 'mbar')"), "demande une valeur"),
          "EN(Pression, 'mbar') : bon");
    check(exprcheck::shapeOf(ctx, "UNITE(Pression)").single, "UNITE rend une valeur (un texte)");
    check(exprcheck::shapeOf(ctx, "SANS_UNITE(Pression)").unit.empty(), "SANS_UNITE : un nombre nu (plus d'unite)");
}

// ---------------------------------------------------------------- l'affectation ---
void affectation() {
    std::printf("-- exprcheck::assignmentUnit : convertit d'office, ou exige EN en mode strict\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Temperature", "REAL", "degC");
    var(p, "NombreNu", "REAL", "");
    const auto ctx = ctxOf(p);

    check(has(exprcheck::assignmentUnit(ctx, "Pression", "PressionBasse"), "converti", true),
          "Pression := PressionBasse : converti d'office (une info)");
    check(has(exprcheck::assignmentUnit(ctx, "Pression", "PressionBasse", true), "EN("),
          "en mode strict : exige EN(...)");
    check(has(exprcheck::assignmentUnit(ctx, "Pression", "Temperature"), "familles diff\xC3\xA9rentes"),
          "Pression := Temperature : REFUSE (familles)");
    check(exprcheck::assignmentUnit(ctx, "Pression", "2.5").empty(), "Pression := 2.5 : bon (le litteral adopte bar)");
    check(exprcheck::assignmentUnit(ctx, "NombreNu", "Pression").empty(), "NombreNu := Pression : bon (la cible sans unite recoit le nombre)");
}

// ---------------------------------------------------------------- Generer ---
void generer() {
    std::printf("-- Generer : une unite ne va que sur un nombre, un symbole connu\n");
    const NameExists none = [](std::string_view) { return false; };
    {
        Project p;
        var(p, "Drapeau", "BOOL", "bar");           // une unite sur un BOOL
        const auto issues = hmi::generate(p, none);
        bool found = false;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Error && i.message.find("une unit\xC3\xA9 ne va que sur un nombre") != std::string::npos) found = true;
        check(found, "une unite sur un BOOL : erreur (une unite ne va que sur un nombre)");
    }
    {
        Project p;
        var(p, "Mesure", "REAL", "zork");           // un symbole inconnu
        const auto issues = hmi::generate(p, none);
        bool found = false;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Error && i.message.find("unit\xC3\xA9 inconnue") != std::string::npos) found = true;
        check(found, "une unite inconnue : erreur");
    }
    {
        Project p;
        var(p, "Pression", "REAL", "bar");          // une unite correcte : aucune erreur d'unite
        const auto issues = hmi::generate(p, none);
        bool bad = false;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Error
                && (i.message.find("unit\xC3\xA9 inconnue") != std::string::npos
                    || i.message.find("une unit\xC3\xA9 ne va que") != std::string::npos))
                bad = true;
        check(!bad, "une unite correcte (bar sur REAL) : aucune erreur d'unite");
    }
}

// ---------------------------------------------------------------- la serialisation ---
std::string indexOf(const Project& p) {
    const auto files = serializeProject(p);
    return files.empty() ? std::string{} : std::string(files.front().data->begin(), files.front().data->end());
}
Project roundTrip(const Project& p) {
    const auto files = serializeProject(p);
    const FileReader reader = [&](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path) { content.assign(f.data->begin(), f.data->end()); return true; }
        return false;
    };
    auto back = parseProject(reader);
    return back ? *back : Project{};
}
void serialisation() {
    std::printf("-- la serialisation : unite=, la bibliotheque du projet, le format 32 en escalier\n");
    // Un projet SANS unite : format d'avant (gate-down), pas 32.
    {
        Project p;
        var(p, "Simple", "REAL", "");
        const std::string ix = indexOf(p);
        check(ix.find("(format 32)") == std::string::npos, "un projet sans unite ne monte pas au format 32 (escalier)");
        check(ix.find("unite=") == std::string::npos, "un projet sans unite n'ecrit aucun unite=");
    }
    // Une unite posee : format 32, unite= ecrit, relu tel quel.
    {
        Project p;
        var(p, "Pression", "REAL", "bar", "9.08");
        HmiType t;
        t.id = p.allocate();
        t.name = "T_Ligne";
        t.members.push_back({"Debit", "REAL", "", "", Visibility::Public, "m3/h"});
        p.programs.types.push_back(t);
        const std::string ix = indexOf(p);
        check(ix.find("(format 32)") != std::string::npos, "une unite posee : format 32");
        check(ix.find("unite=\"bar\"") != std::string::npos, "la variable ecrit unite=\"bar\"");
        const Project back = roundTrip(p);
        const Variable* v = back.variable("Pression");
        check(v && v->unit == "bar", "la variable relue garde son unite (bar)");
        check(!back.programs.types.empty() && !back.programs.types[0].members.empty()
                  && back.programs.types[0].members[0].unit == "m3/h",
              "le membre relu garde son unite (m3/h)");
    }
    // La bibliotheque du projet et l'option Unites strictes : ecrites et relues.
    {
        Project p;
        p.config.strictUnits = true;
        p.config.unitFamilies.push_back({"angle", "deg", "Angle"});
        p.config.units.push_back({"deg", "angle", 1.0, 0.0, "degre"});
        const Project back = roundTrip(p);
        check(back.config.strictUnits, "l'option Unites strictes : relue");
        check(back.config.unitFamilies.size() == 1 && back.config.unitFamilies[0].name == "angle", "la famille du projet : relue");
        check(back.config.units.size() == 1 && back.config.units[0].symbol == "deg" && back.config.units[0].family == "angle",
              "l'unite du projet : relue");
    }
}

// ---------------------------------------------------------------- le simulateur ---
struct Bench {
    Project p;
    Runtime rt;
    explicit Bench(const std::string& body, const std::vector<std::array<const char*, 4>>& vars) {
        for (const auto& v : vars) {
            Variable x;
            x.id = p.allocate();
            x.name = v[0];
            x.type = v[1];
            x.unit = v[2];
            x.initial = v[3];
            p.programs.variables.push_back(x);
        }
        Variable r;
        r.id = p.allocate();
        r.name = "R";
        r.type = "REAL";
        r.initial = "0";
        p.programs.variables.push_back(r);
        Variable rs;
        rs.id = p.allocate();
        rs.name = "RS";
        rs.type = "STRING";
        rs.initial = "";
        p.programs.variables.push_back(rs);
        View view = makeView(p, "Vue_Essai");
        p.views.push_back(view);
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
    double real(std::string* err = nullptr) {
        std::string e;
        const bool ok = rt.callScript("Essai", 0.2, &e);
        if (err) *err = ok ? std::string{} : e;
        sim::Value out;
        return rt.environment().read("R", out) ? out.asReal() : 0.0;
    }
    std::string text() {
        rt.callScript("Essai", 0.2, nullptr);
        sim::Value out;
        return rt.environment().read("RS", out) ? out.asString() : std::string("#");
    }
};
void simulateur() {
    std::printf("-- le simulateur : EN convertit, UNITE rend le symbole, SANS_UNITE l'enleve\n");
    {
        Bench b("R := EN(PressionBasse, 'bar');", {{"PressionBasse", "REAL", "mbar", "2000"}});
        std::string err;
        const double v = b.real(&err);
        check(err.empty() && std::fabs(v - 2.0) < 1e-6, "EN(PressionBasse, 'bar') : 2000 mbar -> 2,0 bar (" + std::to_string(v) + ")");
    }
    {
        Bench b("R := EN(Temperature, 'K');", {{"Temperature", "REAL", "degC", "20"}});
        const double v = b.real();
        check(std::fabs(v - 293.15) < 1e-4, "EN(Temperature, 'K') : 20 degC -> 293,15 K (" + std::to_string(v) + ")");
    }
    {
        Bench b("RS := UNITE(Pression);", {{"Pression", "REAL", "bar", "9"}});
        check(b.text() == "bar", "UNITE(Pression) = 'bar'");
    }
    {
        Bench b("R := SANS_UNITE(Pression);", {{"Pression", "REAL", "bar", "9.08"}});
        const double v = b.real();
        check(std::fabs(v - 9.08) < 1e-6, "SANS_UNITE(Pression) : le meme nombre, nu (" + std::to_string(v) + ")");
    }
    {
        // Une valeur sans unite declaree : EN la laisse telle quelle (Generer a deja prevenu).
        Bench b("R := EN(Libre, 'bar');", {{"Libre", "REAL", "", "5"}});
        const double v = b.real();
        check(std::fabs(v - 5.0) < 1e-6, "EN d'une valeur sans unite : inchangee a l'execution");
    }
}

// ---------------------------------------------------------------- script : l'affectation au bon endroit ---
void scriptBody() {
    std::printf("-- scriptcheck : l'affectation d'unite surface a sa ligne (Compiler / Generer)\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Temperature", "REAL", "degC");
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto hasMsg = [](const std::vector<scriptcheck::Finding>& fs, std::string_view text) {
        for (const auto& f : fs) if (f.message.find(text) != std::string::npos) return true;
        return false;
    };
    check(hasMsg(scriptcheck::check(sc, "Pression := Temperature;"), "familles diff\xC3\xA9rentes"),
          "script : Pression := Temperature refuse (familles)");
    check(hasMsg(scriptcheck::check(sc, "Pression := PressionBasse;"), "converti"),
          "script : Pression := PressionBasse converti d'office (info)");
    p.config.strictUnits = true;
    check(hasMsg(scriptcheck::check(sc, "Pression := PressionBasse;"), "EN("),
          "script en mode strict : exige EN(...)");
}

} // namespace

int main() {
    std::printf("=== unites_test : les unites de mesure dans les types (1.13.0, lot J6) ===\n");
    bibliotheque();
    additionComparaison();
    propagation();
    natives();
    affectation();
    scriptBody();
    generer();
    serialisation();
    simulateur();
    std::printf("=== %d verifications, %d echec(s) ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
