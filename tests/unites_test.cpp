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
//    - 1.13.0 (relecture, groupe A) : l'affectation convertie d'office en marche,
//      EN seulement sur un chemin (indices compris, jamais une locale), la
//      correction d'un clic au bon endroit, l'unite d'un tableau et d'un membre
//      herite, MIN/MAX/LIMIT, la gravite dans un script, RETURN et les
//      arguments, l'unite d'affichage, le facteur ecrit sans perte, le genre des
//      familles, le signe micro.
//
//  UNITES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiUnits.hpp"
#include "../src/hmi/HmiDisplay.hpp"
#include "../src/hmi/HmiReports.hpp"
#include "../src/hmi/HmiTable.hpp"
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
    explicit Bench(const std::string& body, const std::vector<std::array<const char*, 4>>& vars,
                   const std::function<void(Project&)>& prepare = {}) {
        if (prepare) prepare(p);                     // 1.13.0 : des types IHM avant les variables
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
    // 1.13.0 : lance le script, puis lit une variable quelconque.
    double after(const char* name, std::string* err = nullptr) {
        std::string e;
        const bool ok = rt.callScript("Essai", 0.2, &e);
        if (err) *err = ok ? std::string{} : e;
        sim::Value out;
        return rt.environment().read(name, out) ? out.asReal() : -999999.0;
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

// ================================================================ 1.13.0 : la relecture (groupe A) ===
bool hasFinding(const std::vector<scriptcheck::Finding>& fs, std::string_view text, scriptcheck::Finding::Severity sev) {
    for (const auto& f : fs)
        if (f.message.find(text) != std::string::npos && f.severity == sev) return true;
    return false;
}
const scriptcheck::Finding* findingWith(const std::vector<scriptcheck::Finding>& fs, std::string_view text) {
    for (const auto& f : fs)
        if (f.message.find(text) != std::string::npos) return &f;
    return nullptr;
}
const exprcheck::Problem* problemWith(const std::vector<exprcheck::Problem>& ps, std::string_view text) {
    for (const auto& p : ps)
        if (p.message.find(text) != std::string::npos) return &p;
    return nullptr;
}

// n1 / n3 : l'affectation « convertie d'office » l'est vraiment en marche ; un calcul ne l'est pas (EN exige).
void affectationEnMarche() {
    std::printf("-- n1/n3 : Pression := PressionBasse convertit en marche ; un calcul exige EN\n");
    {
        Bench b("Pression := PressionBasse;", {{"Pression", "REAL", "bar", "0"}, {"PressionBasse", "REAL", "mbar", "500"}});
        std::string err;
        const double v = b.after("Pression", &err);
        check(err.empty() && std::fabs(v - 0.5) < 1e-9, "Pression := PressionBasse : 500 mbar -> 0,5 bar en marche (" + std::to_string(v) + ")");
    }
    {
        Bench b("Temperature_K := Temperature_degC;", {{"Temperature_K", "REAL", "K", "0"}, {"Temperature_degC", "REAL", "degC", "20"}});
        const double v = b.after("Temperature_K");
        check(std::fabs(v - 293.15) < 1e-9, "Temperature_K := Temperature_degC : 20 degC -> 293,15 K (" + std::to_string(v) + ")");
    }
    {
        // Un membre et une case de tableau : le moteur lit leur unite comme EN.
        Bench b("Pression := Mesures[2];", {{"Pression", "REAL", "bar", "0"}, {"Mesures", "ARRAY[1..3] OF REAL", "mbar", "[100, 2000, 300]"}});
        const double v = b.after("Pression");
        check(std::fabs(v - 2.0) < 1e-9, "Pression := Mesures[2] : 2000 mbar -> 2,0 bar (" + std::to_string(v) + ")");
    }
    {
        // Une cible entiere : la valeur convertie s'y range comme toute affectation d'un nombre.
        Bench b("Niveau := Pression;", {{"Niveau", "INT", "mbar", "0"}, {"Pression", "REAL", "bar", "1.5"}});
        std::string err;
        const double v = b.after("Niveau", &err);
        check(err.empty() && std::fabs(v - 1500.0) < 1e-9, "Niveau (INT [mbar]) := Pression (1,5 bar) : 1500 (" + std::to_string(v) + ") " + err);
    }
    {
        // Une cible ou une source sans unite : le nombre passe tel quel (rien ne change pour qui n'en met pas).
        Bench b("R := PressionBasse;", {{"PressionBasse", "REAL", "mbar", "500"}});
        check(std::fabs(b.real() - 500.0) < 1e-9, "R (sans unite) := PressionBasse : 500 tel quel");
    }
    {
        // Une locale n'a pas d'unite : elle recoit le nombre tel quel, et le rend tel quel.
        Bench b("VAR L : REAL; END_VAR\nL := PressionBasse;\nPression := L;", {{"Pression", "REAL", "bar", "0"}, {"PressionBasse", "REAL", "mbar", "500"}});
        check(std::fabs(b.after("Pression") - 500.0) < 1e-9, "une locale sans unite : pas de conversion (le controle ne promet rien)");
    }
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    const auto ctx = ctxOf(p);
    const auto calc = exprcheck::assignmentUnit(ctx, "Pression", "PressionBasse * 2.0");
    const auto* pb = problemWith(calc, "seule une variable ou un membre");
    check(pb && !pb->warning && pb->fixFrom == "PressionBasse" && pb->fixTo == "EN(PressionBasse, 'bar')",
          "Pression := PressionBasse * 2.0 : une ERREUR (le moteur ne convertit pas un calcul), correction EN(PressionBasse, 'bar')");
    check(has(exprcheck::assignmentUnit(ctx, "Pression", "(PressionBasse)"), "converti", true),
          "Pression := (PressionBasse) : un chemin entre parentheses, converti d'office");
    scriptcheck::Scope sc;
    sc.project = &p;
    check(hasFinding(scriptcheck::check(sc, "Pression := PressionBasse * 2.0;"), "seule une variable", scriptcheck::Finding::Severity::Error),
          "script : Pression := PressionBasse * 2.0 refuse (Erreur)");
    check(hasFinding(scriptcheck::check(sc, "Pression := PressionBasse;"), "converti", scriptcheck::Finding::Severity::Warning),
          "script : Pression := PressionBasse reste une info");
}

// n7 : Generer compare l'unite d'affichage a l'unite physique.
void affichage() {
    std::printf("-- n7 : Generer compare l'unite d'affichage a l'unite physique\n");
    const NameExists none = [](std::string_view) { return false; };
    const auto run = [&](const char* unit, const char* path = "Pression") {
        Project p;
        var(p, "Pression", "REAL", "bar", "9.08");
        var(p, "Temperature", "REAL", "degC", "20");
        VariableDisplay d;
        d.path = path;
        d.unit = unit;
        p.displays.push_back(d);
        return hmi::generate(p, none);
    };
    const auto err = [](const std::vector<Issue>& is, std::string_view text) {
        for (const auto& i : is)
            if (i.severity == Issue::Severity::Error && i.message.find(text) != std::string::npos) return true;
        return false;
    };
    check(err(run("mbar"), "l'affichage ne convertit pas"), "Pression [bar] montree en mbar : erreur (l'affichage ne convertit pas)");
    check(err(run("degC"), "on ne l'affiche pas en degC"), "Pression [bar] montree en degC : erreur (familles differentes)");
    check(!err(run("bar"), "l'affichage") && !err(run("bar"), "on ne l'affiche pas"), "Pression [bar] montree en bar : rien a dire");
    check(!err(run("\xC2\xB0" "C", "Temperature"), "l'affichage ne convertit pas"), "Temperature [degC] montree en \xC2\xB0" "C : la meme unite");
}

// n8 : EN ne s'accepte que sur un chemin ; la correction d'un clic n'emballe plus un calcul.
void enSurUnChemin() {
    std::printf("-- n8 : EN seulement sur une variable ou un membre (la forme que le moteur convertit)\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    const auto ctx = ctxOf(p);
    check(has(exprcheck::check(ctx, "Pression > EN(PressionBasse * 1.1, 'bar')"), "EN convertit une variable ou un membre"),
          "EN(PressionBasse * 1.1, 'bar') : refuse (le moteur ne le convertirait pas)");
    check(has(exprcheck::check(ctx, "EN(EN(PressionBasse, 'Pa'), 'bar')"), "EN convertit une variable ou un membre"),
          "EN(EN(...)) : refuse");
    check(!has(exprcheck::check(ctx, "Pression > EN(PressionBasse, 'bar') * 1.1"), "EN convertit"),
          "EN(PressionBasse, 'bar') * 1.1 : bon");
    const auto ps = exprcheck::check(ctx, "Pression > PressionBasse * 1.1");
    const auto* pb = problemWith(ps, "ne se comparent pas");
    check(pb && pb->fixFrom == "PressionBasse" && pb->fixTo == "EN(PressionBasse, 'bar')",
          "Pression > PressionBasse * 1.1 : la correction emballe la variable, pas le calcul");
}

// n9 : la correction d'un clic remplace le bon texte, au bon endroit.
void correctionDUnClic() {
    std::printf("-- n9 : la correction d'un clic : le texte exact de l'operande, une occurrence bornee\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Seuils", "ARRAY[1..3] OF REAL", "mbar");
    var(p, "i", "INT", "");
    const auto ctx = ctxOf(p);
    const auto apply = [&](const std::string& body, const exprcheck::Problem* pb) {
        Project q = p;
        Script s;
        s.id = q.allocate();
        s.name = "S";
        s.body = body;
        q.programs.scripts.push_back(s);
        FixPlace at;
        at.script = s.id;
        at.line = 1;
        if (!pb || !applyFix(q, at, pb->fixFrom, pb->fixTo)) return std::string("#");
        return q.programs.scripts.back().body;
    };
    {
        const std::string fx = "PressionBasse > Pression";
        const auto ps = exprcheck::check(ctx, fx);
        const std::string got = apply(fx, problemWith(ps, "ne se comparent pas"));
        check(got == "PressionBasse > EN(Pression, 'mbar')", "PressionBasse > Pression -> " + got + " (pas « EN(Pression, 'mbar')Basse »)");
    }
    {
        const std::string fx = "Pression-PressionBasse*2";
        const auto ps = exprcheck::check(ctx, fx);
        const std::string got = apply(fx, problemWith(ps, "ne s'additionnent pas"));
        check(got == "Pression-EN(PressionBasse, 'bar')*2", "Pression-PressionBasse*2 -> " + got + " (le texte se retrouve)");
    }
    {
        const auto ps = exprcheck::check(ctx, "Pression > Seuils[i]");
        const auto* pb = problemWith(ps, "ne se comparent pas");
        check(pb && pb->fixFrom == "Seuils[i]", "Pression > Seuils[i] : fixFrom = Seuils[i] (pas Seuils[0])");
    }
    {
        // Un nom plus long qui commence pareil n'est pas touche, ni un membre homonyme.
        exprcheck::Problem pb;
        pb.fixFrom = "Pression";
        pb.fixTo = "EN(Pression, 'mbar')";
        check(apply("X := Ligne.Pression + PressionMax + Pression;", &pb) == "X := Ligne.Pression + PressionMax + EN(Pression, 'mbar');",
              "replaceIn : seule l'occurrence bornee (ni Ligne.Pression, ni PressionMax)");
    }
}

// n10 : un tableau de premier niveau et un membre herite gardent leur unite.
void tableauEtHeritage() {
    std::printf("-- n10 : l'unite d'un tableau de premier niveau, d'un membre herite\n");
    Project p;
    var(p, "Pressions", "ARRAY[1..3] OF REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    HmiType base;
    base.id = p.allocate();
    base.name = "T_Pompe";
    base.members.push_back({"Pression", "REAL", "", "", Visibility::Public, "bar"});
    p.programs.types.push_back(base);
    HmiType derived;
    derived.id = p.allocate();
    derived.name = "T_PompeDoublee";
    derived.base = "T_Pompe";
    derived.members.push_back({"Debit", "REAL", "", "", Visibility::Public, "m3/h"});
    p.programs.types.push_back(derived);
    var(p, "Pompe2", "T_PompeDoublee", "");
    const auto ctx = ctxOf(p);
    check(exprcheck::shapeOf(ctx, "Pressions[1]").unit == "bar", "Pressions[1] : l'unite de l'element (bar)");
    check(has(exprcheck::check(ctx, "Pressions[1] > PressionBasse"), "ne se comparent pas"), "Pressions[1] > PressionBasse : refuse");
    check(!has(exprcheck::check(ctx, "EN(Pressions[1], 'mbar')"), "demande une valeur"), "EN(Pressions[1], 'mbar') : bon");
    check(exprcheck::shapeOf(ctx, "Pompe2.Pression").unit == "bar", "Pompe2.Pression (herite de T_Pompe) : bar");
    check(units::unitOfPath(p, "Pompe2.Pression") == "bar" && units::unitOfPath(p, "Pompe2.Debit") == "m3/h",
          "units::unitOfPath suit l'heritage (le moteur lit la meme unite)");
}

// n11 : MIN, MAX, LIMIT exigent la meme unite et la gardent ; ABS la garde.
void minMaxLimit() {
    std::printf("-- n11 : MIN, MAX, LIMIT comme une comparaison ; ABS garde l'unite\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    const auto ctx = ctxOf(p);
    check(has(exprcheck::check(ctx, "MAX(Pression, PressionBasse)"), "ne se comparent pas"), "MAX(Pression, PressionBasse) : refuse");
    check(has(exprcheck::check(ctx, "LIMIT(0.0, PressionBasse, 900.0) > Pression"), "ne se comparent pas"),
          "LIMIT(0.0, PressionBasse, 900.0) > Pression : refuse (le resultat est en mbar)");
    check(exprcheck::shapeOf(ctx, "MIN(Pression, 2.0)").unit == "bar", "MIN(Pression, 2.0) : en bar");
    check(has(exprcheck::check(ctx, "ABS(PressionBasse) > Pression"), "ne se comparent pas"), "ABS(PressionBasse) garde le mbar");
}

// n12 : dans un script, une faute d'unite est une erreur, avec sa correction d'un clic.
void graviteScript() {
    std::printf("-- n12 : dans un script, bar > mbar et pression - temperature sont des erreurs\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Temperature", "REAL", "degC");
    var(p, "Alarme", "BOOL", "");
    var(p, "Ecart", "REAL", "");
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto fs = scriptcheck::check(sc, "IF Pression > PressionBasse THEN Alarme := TRUE; END_IF;");
    const auto* f = findingWith(fs, "ne se comparent pas");
    check(f && f->severity == scriptcheck::Finding::Severity::Error, "IF Pression > PressionBasse : Erreur (la simulation ne part pas)");
    check(f && f->fixFrom == "PressionBasse" && f->fixTo == "EN(PressionBasse, 'bar')", "... et sa correction d'un clic");
    check(hasFinding(scriptcheck::check(sc, "Ecart := Pression - Temperature;"), "deux familles", scriptcheck::Finding::Severity::Error),
          "Ecart := Pression - Temperature : Erreur");
    p.config.strictUnits = true;
    const auto strictFs = scriptcheck::check(sc, "Pression := PressionBasse;");
    const auto* s = findingWith(strictFs, "Unit\xC3\xA9s strictes");
    check(s && s->severity == scriptcheck::Finding::Severity::Error && s->fixTo == "EN(PressionBasse, 'bar')",
          "mode strict : Erreur et correction d'un clic");
}

// n21 / n50 : EN et UNITE suivent les indices, et ignorent une locale homonyme.
void enEnMarche() {
    std::printf("-- n21/n50 : EN et UNITE sur un chemin indice ; une locale n'a pas d'unite\n");
    {
        Bench b("Pressions[2] := 2.5;\nR := EN(Pressions[2], 'mbar');", {{"Pressions", "ARRAY[1..3] OF REAL", "bar", "[0, 0, 0]"}});
        const double v = b.real();
        check(std::fabs(v - 2500.0) < 1e-6, "EN(Pressions[2], 'mbar') : 2,5 bar -> 2500 (" + std::to_string(v) + ")");
    }
    {
        Bench b("RS := UNITE(Pressions[2]);", {{"Pressions", "ARRAY[1..3] OF REAL", "bar", "[0, 0, 0]"}});
        check(b.text() == "bar", "UNITE(Pressions[2]) = 'bar'");
    }
    {
        // Un tableau dans une structure (l'exemple de l'aide) : T_Ligne.Vannes : ARRAY[1..3] OF REAL [bar].
        const auto ligne = [](Project& q) {
            HmiType t;
            t.id = q.allocate();
            t.name = "T_Ligne";
            t.members.push_back({"Vannes", "ARRAY[1..3] OF REAL", "", "", Visibility::Public, "bar"});
            q.programs.types.push_back(t);
        };
        Bench b("Ligne.Vannes[2] := 2.5;\nR := EN(Ligne.Vannes[2], 'mbar');\nRS := UNITE(Ligne.Vannes[2]);",
                {{"Ligne", "T_Ligne", "", ""}}, ligne);
        std::string err;
        const double v = b.real(&err);
        check(err.empty() && std::fabs(v - 2500.0) < 1e-6, "EN(Ligne.Vannes[2], 'mbar') : 2500 (" + std::to_string(v) + ") " + err);
        sim::Value s;
        check(b.rt.environment().read("RS", s) && s.asString() == "bar", "UNITE(Ligne.Vannes[2]) = 'bar'");
    }
    {
        // n50 : un parametre homonyme d'une variable IHM qui porte une unite n'a pas son unite.
        Bench b("FUNCTION EnMbar (Pression : REAL) : REAL\n    EnMbar := EN(Pression, 'mbar');\nEND_FUNCTION\nR := EnMbar(5.0);",
                {{"Pression", "REAL", "bar", "9"}});
        const double v = b.real();
        check(std::fabs(v - 5.0) < 1e-9, "EN(Pression, 'mbar') sur un parametre homonyme : inchange (" + std::to_string(v) + ", pas 5000)");
    }
}

// n23 : le facteur et le decalage d'une unite du projet s'ecrivent sans perte ; un facteur nul est refuse.
void facteurSansPerte() {
    std::printf("-- n23 : facteur et decalage ecrits sans perte, un facteur nul refuse a la lecture\n");
    Project p;
    p.config.unitFamilies.push_back({"courant_fin", "A", "Courant fin"});
    p.config.units.push_back({"uA", "courant", 0.000001, 0.0, "microamp\xC3\xA8re"});
    p.config.units.push_back({"psi2", "pression", 6894.757293, 0.0, "psi modifi\xC3\xA9"});
    p.config.units.push_back({"degX", "temperature", 5.0 / 9.0, 255.3722222222222, "degr\xC3\xA9 X"});
    const Project back = roundTrip(p);
    const auto factorOf = [&](const char* sym) {
        for (const auto& u : back.config.units)
            if (u.symbol == sym) return u.factor;
        return -1.0;
    };
    check(factorOf("uA") == 0.000001, "uA : facteur 0.000001 relu tel quel (pas 0)");
    check(factorOf("psi2") == 6894.757293, "psi2 : facteur 6894.757293 relu tel quel (pas 6894.757)");
    check(factorOf("degX") == 5.0 / 9.0, "degX : facteur 5/9 relu au bit pres");
    bool off = false;
    for (const auto& u : back.config.units) off = off || (u.symbol == "degX" && u.offset == 255.3722222222222);
    check(off, "degX : decalage relu au bit pres");
    // Un index ecrit a la main avec facteur=0 : l'unite est ignoree, et l'ouverture le dit.
    auto files = serializeProject(p);
    std::string ix(files.front().data->begin(), files.front().data->end());
    const auto at = ix.find("facteur=1e-06");
    check(at != std::string::npos, "l'index ecrit facteur=1e-06");
    if (at != std::string::npos) ix.replace(at, 13, "facteur=0");
    const FileReader reader = [&](const std::string& path, std::string& content) {
        if (path == files.front().path) { content = ix; return true; }
        for (const auto& f : files)
            if (f.path == path) { content.assign(f.data->begin(), f.data->end()); return true; }
        return false;
    };
    LoadReport report;
    const auto zero = parseProject(reader, &report);
    bool kept = false, said = false;
    if (zero)
        for (const auto& u : zero->config.units) kept = kept || u.symbol == "uA";
    for (const auto& w : report.warnings) said = said || w.find("uA") != std::string::npos;
    check(zero && !kept && said, "facteur=0 : l'unite uA est ignoree, et un avertissement le dit");
}

// n32 : les familles masculines.
void genreDesFamilles() {
    std::printf("-- n32 : un debit, un courant, un temps ; une pression\n");
    units::Catalog cat;
    check(cat.familyPhrase("m3/h") == "un d\xC3\xA9" "bit" && cat.familyPhrase("A") == "un courant" && cat.familyPhrase("s") == "un temps"
              && cat.familyPhrase("%") == "un pourcentage",
          "debit, courant, temps, pourcentage : au masculin");
    check(cat.familyPhrase("bar") == "une pression" && cat.familyPhrase("degC") == "une temp\xC3\xA9rature", "pression, temperature : au feminin");
    Project p;
    var(p, "Debit", "REAL", "m3/h");
    var(p, "Courant", "REAL", "A");
    const auto ps = exprcheck::check(ctxOf(p), "Debit + Courant");
    check(has(ps, "tu additionnes un d\xC3\xA9" "bit et un courant"), "Debit + Courant : « un debit et un courant »");
}

// n35 : une locale ou un parametre cache la variable IHM ; RETURN et les arguments sont juges.
void localesRetourArguments() {
    std::printf("-- n35 : les locales cachent la variable IHM ; RETURN et les arguments ont leur verdict\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "Temperature", "REAL", "degC");
    scriptcheck::Scope sc;
    sc.project = &p;
    check(!findingWith(scriptcheck::check(sc, "VAR\n    Temperature : REAL;\nEND_VAR\nPression := Temperature;"), "familles"),
          "une locale Temperature (sans unite) cache la variable IHM en degC : pas de refus");
    // RETURN dans une fonction qui rend des bar.
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Regler";
    f.returnType = "REAL";
    f.returnUnit = "bar";
    sc.function = &f;
    check(hasFinding(scriptcheck::check(sc, "RETURN Temperature;"), "tu rends une temp\xC3\xA9rature", scriptcheck::Finding::Severity::Error),
          "RETURN Temperature dans une fonction en bar : refuse");
    const auto returnFs = scriptcheck::check(sc, "RETURN PressionBasse;");
    const auto* r = findingWith(returnFs, "un RETURN ne convertit pas");
    check(r && r->severity == scriptcheck::Finding::Severity::Error && r->fixTo == "EN(PressionBasse, 'bar')",
          "RETURN PressionBasse : Erreur (pas de conversion) et correction EN");
    check(hasFinding(scriptcheck::check(sc, "Regler := PressionBasse;"), "un RETURN ne convertit pas", scriptcheck::Finding::Severity::Error),
          "Regler := PressionBasse (la valeur de la fonction) : comme RETURN");
    check(!findingWith(scriptcheck::check(sc, "RETURN Pression;"), "RETURN"), "RETURN Pression (bar) : bon");
    sc.function = nullptr;
    // Un argument vers un parametre qui porte une unite.
    HmiFunction g;
    g.id = p.allocate();
    g.name = "Consigner";
    g.returnType = "REAL";
    Declaration prm;
    prm.id = p.allocate();
    prm.kind = DeclKind::Parameter;
    prm.name = "Consigne";
    prm.type = "REAL";
    prm.unit = "bar";
    g.decls.push_back(prm);
    g.body = "Consigner := Consigne;";
    p.programs.functions.push_back(g);
    const auto ctx = ctxOf(p);
    check(has(exprcheck::check(ctx, "Consigner(Temperature) > 0"), "tu passes une temp\xC3\xA9rature"), "Consigner(Temperature) : refuse (familles)");
    const auto argPs = exprcheck::check(ctx, "Consigner(PressionBasse) > 0");
    const auto* a = problemWith(argPs, "un argument ne se convertit pas");
    check(a && !a->warning && a->fixTo == "EN(PressionBasse, 'bar')", "Consigner(PressionBasse) : erreur, correction EN");
    check(!problemWith(exprcheck::check(ctx, "Consigner(Pression) > 0"), "argument"), "Consigner(Pression) : bon");
    check(hasFinding(scriptcheck::check(sc, "Consigner(PressionBasse);"), "un argument ne se convertit pas", scriptcheck::Finding::Severity::Error),
          "script : Consigner(PressionBasse); seul sur sa ligne, juge aussi");
}

// n43 : le signe micro n'est plus jete.
void signeMicro() {
    std::printf("-- n43 : \xC2\xB5m n'est pas le metre\n");
    units::Catalog cat;
    check(!cat.known("\xC2\xB5m") && cat.familyOf("\xC2\xB5s").empty(), "\xC2\xB5m, \xC2\xB5s : inconnues (pas m ni s)");
    check(cat.relation("\xC2\xB5m", "mm") == units::Rel::Unknown, "\xC2\xB5m vs mm : Unknown (Generer le dit)");
    Config cfg;
    cfg.units.push_back({"\xC2\xB5m", "longueur", 0.000001, 0.0, "microm\xC3\xA8tre"});
    units::Catalog pcat(cfg);
    const auto c = pcat.convert("\xC2\xB5m", "mm");
    check(c && std::fabs(c->apply(50.0) - 0.05) < 1e-12, "une unite du projet \xC2\xB5m : 50 \xC2\xB5m = 0,05 mm");
    check(pcat.unit("m") && pcat.unit("m")->factor == 1.0 && pcat.unit("m")->symbol == "m", "le metre fourni n'est pas remplace par \xC2\xB5m");
    check(pcat.unit("um") && pcat.unit("um")->symbol == "\xC2\xB5m", "um (sans le signe) trouve \xC2\xB5m");
}

// ================================================================ 1.13.0 (seconde passe, lot D3) ===
Declaration declared(Project& p, DeclKind kind, const char* name, const char* type, const char* unit, PassMode mode = PassMode::In) {
    Declaration d;
    d.id = p.allocate();
    d.kind = kind;
    d.name = name;
    d.type = type;
    d.unit = unit;
    d.mode = mode;
    if (kind == DeclKind::Constant) d.value = "2.5";
    return d;
}

// T2-36 : la colonne Unite des Parametres, Locales et Constantes compte - au controle, en marche.
void colonneUniteDesDeclarations() {
    std::printf("-- T2-36 : l'unite d'un parametre, d'une locale, d'une constante de vue (controle et moteur)\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    HmiFunction f;
    f.id = p.allocate();
    f.name = "EnMbar";
    f.returnType = "REAL";
    f.decls = {declared(p, DeclKind::Parameter, "p", "REAL", "bar")};
    f.body = "EnMbar := EN(p, 'mbar');";
    scriptcheck::Scope sc;
    sc.project = &p;
    sc.function = &f;
    check(!findingWith(scriptcheck::check(sc, f.body, f.decls, decl::Role::Function), "EN demande une valeur"),
          "EN(p, 'mbar') sur un parametre p : REAL [bar] : accepte (avant : \xC2\xAB pose une unit\xC3\xA9 sur sa d\xC3\xA9" "claration \xC2\xBB)");
    check(hasFinding(scriptcheck::check(sc, "EnMbar := p - PressionBasse;", f.decls, decl::Role::Function), "ne s'additionnent pas",
                     scriptcheck::Finding::Severity::Error),
          "p [bar] - PressionBasse [mbar] : refuse");
    sc.function = nullptr;
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.decls = {declared(p, DeclKind::Variable, "Locale", "REAL", "bar")};
    check(hasFinding(scriptcheck::check(sc, "Locale := PressionBasse;", s.decls, decl::Role::Script), "converti", scriptcheck::Finding::Severity::Warning),
          "une locale de script en bar recoit PressionBasse : convertie d'office (une info)");
    // Une constante de vue.
    View v = makeView(p, "Vue_Four");
    v.constants.push_back(declared(p, DeclKind::Constant, "Seuil_Haut", "REAL", "bar"));
    p.views.push_back(v);
    exprcheck::Context ctx = ctxOf(p);
    ctx.view = &p.views.back();
    check(!has(exprcheck::check(ctx, "EN(Seuil_Haut, 'mbar') > 0"), "demande une valeur"), "fx : EN(Seuil_Haut, 'mbar') sur la constante de vue [bar] : accepte");
    check(has(exprcheck::check(ctx, "PressionBasse > Seuil_Haut"), "ne se comparent pas"), "fx : PressionBasse [mbar] > Seuil_Haut [bar] : refuse");
    check(exprcheck::shapeOf(ctx, "Seuil_Haut").unit == "bar", "la constante de vue porte bar");
    // L'affichage : un afficheur qui montre la constante montre son unite (dans sa vue, et qualifiee ailleurs).
    check(physicalDisplayUnit(p, "Seuil_Haut", &p.views.back()) == "bar", "affichage : Seuil_Haut dans sa vue montre bar (avant : rien)");
    check(physicalDisplayUnit(p, "Vue_Four.Seuil_Haut") == "bar", "affichage : Vue_Four.Seuil_Haut ailleurs montre bar");
    check(physicalDisplayUnit(p, "Seuil_Haut").empty(), "affichage : Seuil_Haut hors de sa vue n'est pas une constante : rien");
    // En marche : le parametre, la locale de script, la constante qualifiee.
    {
        Bench b("R := EnMbar(2.5);", {{"Pression", "REAL", "bar", "0"}}, [&](Project& q) {
            HmiFunction g = f;
            g.id = q.allocate();
            for (auto& d : g.decls) d.id = q.allocate();
            q.programs.functions.push_back(g);
        });
        const double v2 = b.real();
        check(std::fabs(v2 - 2500.0) < 1e-6, "en marche : EnMbar(2.5) avec p [bar] = 2500 mbar (" + std::to_string(v2) + ", avant 2.5)");
    }
    {
        Bench b("R := Bas(2.5);", {{"p", "REAL", "bar", "0"}}, [&](Project& q) {
            HmiFunction g;
            g.id = q.allocate();
            g.name = "Bas";
            g.returnType = "REAL";
            g.decls = {declared(q, DeclKind::Parameter, "p", "REAL", "")};
            g.body = "Bas := EN(p, 'mbar');";
            q.programs.functions.push_back(g);
        });
        const double v2 = b.real();
        check(std::fabs(v2 - 2.5) < 1e-9, "un parametre p sans unite, homonyme d'une variable IHM p [bar] : inchange (" + std::to_string(v2) + ")");
    }
    {
        Bench b("Locale := 2.5;\nR := EN(Locale, 'mbar');\nPression := Locale;", {{"Pression", "REAL", "mbar", "0"}}, [&](Project& q) {
            (void)q;
        });
        b.p.programs.scripts.back().decls = {declared(b.p, DeclKind::Variable, "Locale", "REAL", "bar")};
        std::string err;
        const double v2 = b.real(&err);
        check(err.empty() && std::fabs(v2 - 2500.0) < 1e-6, "en marche : une locale de script [bar] - EN(Locale, 'mbar') = 2500 (" + std::to_string(v2) + ") " + err);
        sim::Value pv;
        check(b.rt.environment().read("Pression", pv) && std::fabs(pv.asReal() - 2500.0) < 1e-6,
              "en marche : Pression [mbar] := Locale [bar] convertie d'office (" + std::to_string(pv.asReal()) + ")");
    }
    {
        Bench b("R := EN(Vue_Constantes.Seuil_Haut, 'mbar');", {}, [&](Project& q) {
            View w = makeView(q, "Vue_Constantes");
            w.constants.push_back(declared(q, DeclKind::Constant, "Seuil_Haut", "REAL", "bar"));
            q.views.push_back(w);
        });
        const double v2 = b.real();
        check(std::fabs(v2 - 2500.0) < 1e-6, "en marche : EN(Vue_Constantes.Seuil_Haut, 'mbar') = 2500 (" + std::to_string(v2) + ")");
    }
}

// R4, R6 : la correction d'un clic vise la bonne occurrence ; une correction d'espace de noms touche le debut d'un chemin.
void correctionALaBonnePlace() {
    std::printf("-- R4 / R6 : la correction d'un clic, a la bonne occurrence ; un nom d'espace dans un chemin plus long\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    const auto ctx = ctxOf(p);
    const std::string fx = "PressionBasse < 900.0 AND Pression > PressionBasse";
    const auto ps = exprcheck::check(ctx, fx);
    const auto* pb = problemWith(ps, "ne se comparent pas");
    check(pb && pb->fixAt == fx.rfind("PressionBasse"), "le probleme dit ou est l'occurrence (la seconde PressionBasse)");
    Project q = p;
    Script s;
    s.id = q.allocate();
    s.name = "S";
    s.body = fx;
    q.programs.scripts.push_back(s);
    FixPlace at;
    at.script = s.id;
    at.line = 1;
    at.column = pb ? static_cast<int>(pb->fixAt) + 1 : 0;
    check(pb && applyFix(q, at, pb->fixFrom, pb->fixTo)
              && q.programs.scripts.back().body == "PressionBasse < 900.0 AND Pression > EN(PressionBasse, 'bar')",
          "appliquee : " + q.programs.scripts.back().body + " (pas la premiere PressionBasse)");
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto fs = scriptcheck::check(sc, "IF PressionBasse < 900.0 AND Pression > PressionBasse THEN Pression := 1.0; END_IF;");
    const auto* f = findingWith(fs, "ne se comparent pas");
    check(f && f->fixColumn == static_cast<int>(std::string("IF PressionBasse < 900.0 AND Pression > ").size()) + 1,
          "script : la colonne de l'occurrence a corriger (" + std::to_string(f ? f->fixColumn : -1) + ")");
    // R6 : un espace mal ecrit suivi du reste du chemin (Gaz.Vanes.T_Vanne) se corrige encore.
    Project r = p;
    Script t;
    t.id = r.allocate();
    t.name = "T";
    t.body = "VAR V : Gaz.Vanes.T_Vanne; END_VAR";
    r.programs.scripts.push_back(t);
    FixPlace at2;
    at2.script = t.id;
    at2.line = 1;
    check(applyFix(r, at2, "Gaz.Vanes", "Gaz.Vannes") && r.programs.scripts.back().body == "VAR V : Gaz.Vannes.T_Vanne; END_VAR",
          "R6 : Gaz.Vanes -> Gaz.Vannes dans Gaz.Vanes.T_Vanne (" + r.programs.scripts.back().body + ")");
    exprcheck::Problem en;
    en.fixFrom = "Pression";
    en.fixTo = "EN(Pression, 'mbar')";
    Project u = p;
    Script w;
    w.id = u.allocate();
    w.name = "W";
    w.body = "X := Pression.Valide OR Pression > 1.0;";
    u.programs.scripts.push_back(w);
    FixPlace at3;
    at3.script = w.id;
    at3.line = 1;
    check(applyFix(u, at3, en.fixFrom, en.fixTo) && u.programs.scripts.back().body == "X := Pression.Valide OR EN(Pression, 'mbar') > 1.0;",
          "une correction EN(...) ne touche toujours pas Pression.Valide (" + u.programs.scripts.back().body + ")");
}

// 1.13.0 (lot F3) : les defauts vus aux captures - le message des Unites strictes nomme la variable et ses deux unites,
// la correction en tete ; la correction appliquee dit ou est le texte remplace (le curseur y va).
void lotF3() {
    std::printf("-- lot F3 : le message des Unites strictes ; la place du texte corrige\n");
    Project p;
    var(p, "D_PRESSION_BASSE", "REAL", "mbar");
    var(p, "D_PRESSION_TOTALE", "REAL", "bar");
    p.config.strictUnits = true;
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto fs = scriptcheck::check(sc, "D_PRESSION_TOTALE := D_PRESSION_BASSE;");
    const auto* f = findingWith(fs, "Unit\xC3\xA9s strictes");
    check(f && f->message.rfind("\xC3\xA9" "cris EN(D_PRESSION_BASSE, 'bar') : D_PRESSION_BASSE est en mbar, la cible en bar", 0) == 0
              && f->fixTo == "EN(D_PRESSION_BASSE, 'bar')",
          "R10 : la correction d'abord, puis la variable et ses deux unites (" + (f ? f->message : std::string("aucun message")) + ")");
    const auto calc = exprcheck::assignmentUnit(ctxOf(p), "D_PRESSION_TOTALE", "D_PRESSION_BASSE * 2.0", true);
    check(has(calc, "\xC3\xA9" "cris EN(D_PRESSION_BASSE, 'bar') dans le calcul : le calcul (D_PRESSION_BASSE * 2.0) est en mbar, la cible en bar"),
          "R10 : un calcul en mode strict - EN dans le calcul");
    // R1 : applyFix rend la place du texte remplace (ligne, colonne) : le curseur y va, l'ancienne plage ne vaut plus.
    Project q = p;
    Script s;
    s.id = q.allocate();
    s.name = "Bilan";
    s.body = "(* bilan *)\nX := 1;\nD_PRESSION_TOTALE := D_PRESSION_TOTALE + D_PRESSION_BASSE;\n";
    q.programs.scripts.push_back(s);
    FixPlace at;
    at.script = s.id;
    at.line = 3;
    FixPlace done;
    check(applyFix(q, at, "D_PRESSION_BASSE", "EN(D_PRESSION_BASSE, 'bar')", nullptr, &done) && done.line == 3
              && done.column == static_cast<int>(std::string("D_PRESSION_TOTALE := D_PRESSION_TOTALE + ").size()) + 1
              && q.programs.scripts.back().body.find("+ EN(D_PRESSION_BASSE, 'bar');") != std::string::npos,
          "R1 : la ligne et la colonne du texte remplace (" + std::to_string(done.line) + ", " + std::to_string(done.column) + ")");
    FixPlace none;
    check(!applyFix(q, at, "N_EXISTE_PAS", "X", nullptr, &none) && none.line == 0 && none.column == 0, "R1 : rien de remplace, aucune place");
}

// R7, R5, R13, R10 : VAR_IN_OUT, appel nomme, appel d'instance seul, RETURN / CASE / FOR.
void passagesEtInstructions() {
    std::printf("-- R7 / R5 / R13 / R10 : VAR_IN_OUT, appel nomme, appel d'instance, RETURN, CASE, FOR\n");
    Project p;
    var(p, "Pression", "REAL", "bar");
    var(p, "PressionBasse", "REAL", "mbar");
    var(p, "i", "INT", "");
    HmiFunction g;
    g.id = p.allocate();
    g.name = "Regler";
    g.decls = {declared(p, DeclKind::Parameter, "Consigne", "REAL", "bar", PassMode::InOut)};
    g.body = "Consigne := Consigne + 1.0;";
    p.programs.functions.push_back(g);
    HmiFunction h;
    h.id = p.allocate();
    h.name = "Consigner";
    h.returnType = "REAL";
    h.decls = {declared(p, DeclKind::Parameter, "Consigne", "REAL", "bar")};
    h.body = "Consigner := Consigne;";
    p.programs.functions.push_back(h);
    scriptcheck::Scope sc;
    sc.project = &p;
    const auto inout = scriptcheck::check(sc, "Regler(PressionBasse);");
    const auto* io = findingWith(inout, "VAR_IN_OUT");
    check(io && io->fixTo.empty() && io->severity == scriptcheck::Finding::Severity::Error,
          "R7 : un VAR_IN_OUT en bar recoit PressionBasse : erreur, sans correction EN (" + std::string(io ? io->message : "?") + ")");
    check(has(exprcheck::check(ctxOf(p), "Consigner(Consigne := PressionBasse) > 0"), "un argument ne se convertit pas"),
          "R5 : un appel nomme est juge (Consigne := PressionBasse)");
    check(!problemWith(exprcheck::check(ctxOf(p), "Consigner(Consigne := Pression) > 0"), "argument"), "R5 : appel nomme dans la bonne unite : bon");
    // R10 : RETURN, CASE, FOR.
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Ecart";
    f.returnType = "REAL";
    sc.function = &f;
    check(hasFinding(scriptcheck::check(sc, "RETURN Pression - PressionBasse;"), "ne s'additionnent pas", scriptcheck::Finding::Severity::Error),
          "R10 : RETURN Pression - PressionBasse (retour sans unite) : le melange est dit");
    sc.function = nullptr;
    check(hasFinding(scriptcheck::check(sc, "CASE TO_INT(Pression - PressionBasse) OF\n1: i := 1;\nEND_CASE;"), "ne s'additionnent pas",
                     scriptcheck::Finding::Severity::Error),
          "R10 : le selecteur d'un CASE est juge");
    check(hasFinding(scriptcheck::check(sc, "FOR i := 1 TO TO_INT(Pression - PressionBasse) DO\nPression := 1.0;\nEND_FOR;"), "ne s'additionnent pas",
                     scriptcheck::Finding::Severity::Error),
          "R10 : les bornes d'un FOR sont jugees");
    // R13 : un appel de fonction d'instance seul sur sa ligne.
    View sym = makeView(p, "S_Vanne");
    sym.role = "symbole";
    HmiFunction rv;
    rv.id = p.allocate();
    rv.name = "Regler";
    rv.decls = {declared(p, DeclKind::Parameter, "Consigne", "REAL", "bar")};
    rv.body = "Pression := Consigne;";
    sym.functions.push_back(rv);
    p.views.push_back(sym);
    View use = makeView(p, "Vue_Vannes");
    Object inst;
    inst.id = p.allocate();
    inst.kind = Kind::SymbolInstance;
    inst.name = "Vanne_3";
    inst.props.push_back({"symbol", "S_Vanne", ""});
    use.objects.push_back(inst);
    p.views.push_back(use);
    sc.view = &p.views.back();
    check(hasFinding(scriptcheck::check(sc, "Vanne_3.Regler(PressionBasse);"), "un argument ne se convertit pas", scriptcheck::Finding::Severity::Error),
          "R13 : Vanne_3.Regler(PressionBasse); seul sur sa ligne : juge");
}

// R1 / R8 : un tableau entier ne se convertit pas d'office.
void tableauEntier() {
    std::printf("-- R1 / R8 : Pressions [bar] := Mesures [mbar] - un tableau entier ne se convertit pas\n");
    Project p;
    var(p, "Pressions", "ARRAY[1..3] OF REAL", "bar");
    var(p, "Mesures", "ARRAY[1..3] OF REAL", "mbar");
    var(p, "Copie", "ARRAY[1..3] OF REAL", "bar");
    const auto ps = exprcheck::assignmentUnit(ctxOf(p), "Pressions", "Mesures");
    check(has(ps, "case par case") && !has(ps, "converti", true), "Pressions := Mesures : une erreur (case par case), plus l'info \xC2\xAB converti d'office \xC2\xBB");
    check(exprcheck::assignmentUnit(ctxOf(p), "Pressions", "Copie").empty(), "Pressions := Copie (meme unite) : rien a dire");
    check(has(exprcheck::assignmentUnit(ctxOf(p), "Pressions[1]", "Mesures[1]"), "converti", true), "Pressions[1] := Mesures[1] : convertie d'office (une case)");
}

// R11 : le moteur ne relit plus les variables a chaque affectation (cache) et reste juste apres un changement.
void cacheDesUnites() {
    std::printf("-- R11 : les unites lues une fois par demarrage\n");
    Bench b("Pression := PressionBasse;\nPression := PressionBasse;", {{"Pression", "REAL", "bar", "0"}, {"PressionBasse", "REAL", "mbar", "1500"}});
    const double v = b.after("Pression");
    check(std::fabs(v - 1.5) < 1e-9, "deux affectations : 1500 mbar -> 1,5 bar a chaque fois (" + std::to_string(v) + ")");
}

// T2-38 : Generer controle l'unite des declarations et des retours ; T2-39 : l'unite physique remonte au Tableau, a la
// saisie, au PID, aux rapports ; T2-40 : un format en pourcentage ne double pas le signe.
void affichageEtDeclarations() {
    std::printf("-- T2-38 / T2-39 / T2-40 : Generer et les declarations, l'unite d'affichage partout, le format en pour cent\n");
    const NameExists none = [](std::string_view) { return false; };
    const auto err = [](const std::vector<Issue>& is, std::string_view text) {
        for (const auto& i : is)
            if (i.severity == Issue::Severity::Error && i.message.find(text) != std::string::npos) return true;
        return false;
    };
    {
        Project p;
        View v = makeView(p, "Vue_Four");
        v.constants.push_back(declared(p, DeclKind::Constant, "Seuil_Haut", "REAL", "inH2O"));
        v.constants.push_back(declared(p, DeclKind::Constant, "Actif", "BOOL", "bar"));
        p.views.push_back(v);
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Debit";
        f.returnType = "REAL";
        f.returnUnit = "m3hh";
        f.body = "Debit := 1.0;";
        p.programs.functions.push_back(f);
        const auto is = hmi::generate(p, none);
        check(err(is, "Vue_Four.Seuil_Haut : unit\xC3\xA9 inconnue 'inH2O'"), "Generer : une constante de vue en inH2O (retiree du projet) - unite inconnue (avant : rien)");
        check(err(is, "Vue_Four.Actif : une unit\xC3\xA9 ne va que sur un nombre"), "Generer : une unite sur une constante BOOL - refusee");
        check(err(is, "Debit (unit\xC3\xA9 du retour) : unit\xC3\xA9 inconnue 'm3hh'"), "Generer : l'unite du retour inconnue");
    }
    // T2-39 : l'unite d'affichage - la ligne Unites et formats, sinon l'unite physique.
    {
        Project p;
        var(p, "Pression", "REAL", "bar", "9.08");
        HmiType t;
        t.id = p.allocate();
        t.name = "T_Cuve";
        TypeMember m;
        m.name = "Niveau";
        m.type = "REAL";
        m.unit = "m";
        t.members.push_back(m);
        p.programs.types.push_back(t);
        var(p, "Cuves", "ARRAY[1..2] OF T_Cuve", "", "");
        check(displayUnitOf(p, "Pression") == "bar", "displayUnitOf : sans ligne Unites et formats, l'unite physique (bar)");
        table::Column col;
        col.title = "Niveau";
        col.content = "[*].Niveau";
        const auto info = table::analyzeColumn(p, table::analyzeSource(p, "Cuves"), col);
        check(info.unit == "m", "Tableau : la colonne [*].Niveau montre m, l'unite du membre (avant : rien) [" + info.unit + "]");
        p.displays.push_back(VariableDisplay{"Pression", "mbar", "0.0"});
        check(displayUnitOf(p, "Pression") == "mbar", "displayUnitOf : une unite tapee sur la ligne l'emporte");
        // Le rapport Mesures : l'unite physique de la variable archivee.
        p.history.archived = {"Cuves[1].Niveau"};
        Report daily;
        daily.name = "Rapport_journalier";
        const auto c = report::build(p, History{}, daily, 0.0, 86400.0, {}, "2026-10-26 06:00:00.000");
        bool sawUnit = false;
        for (const auto& sec : c.sections)
            if (sec.title == "Mesures")
                for (const auto& row : sec.rows) sawUnit = sawUnit || (row.size() > 1 && row[0] == "Cuves[1].Niveau" && row[1] == "m");
        check(sawUnit, "rapport Mesures : Cuves[1].Niveau en m (avant : la colonne Unite vide)");
    }
    // T2-40 : un format 0.0% porte deja le signe.
    {
        Project p;
        var(p, "Ouverture", "REAL", "ratio", "0.45");
        check(physicalDisplayUnit(p, "Ouverture", nullptr, "0.0%").empty() && physicalDisplayUnit(p, "Ouverture", nullptr, "0.0") == "ratio",
              "Ouverture [ratio] : avec le format 0.0% pas d'unite en plus (avant : 45.0 % ratio) ; avec 0.0, ratio");
        p.displays.push_back(VariableDisplay{"Ouverture", "", "0.0%"});
        check(formatTemplate("{Ouverture:u}", p) == "{Ouverture:0.0%}", "{Ouverture:u} avec la ligne 0.0% : le signe du format seul");
        View v = makeView(p, "Vue");
        Object o;
        o.id = p.allocate();
        o.kind = Kind::NumericDisplay;
        o.setExpr("value", "Ouverture");
        o.set("unit", "");
        o.set("format", "0.0%");
        v.objects.push_back(o);
        const auto prepared = displayView(v, p, projectDisplay(p));
        check(prepared.objects[0].text("unit").empty(), "l'afficheur en 0.0% sur une variable en ratio : pas d'unite accolee");
        var(p, "Pression", "REAL", "bar", "1");
        check(physicalDisplayUnit(p, "Pression", nullptr, "0.0%") == "bar", "un format % sur une autre famille (bar) garde l'unite");
    }
}

// 1.13.1 (lot K1) : l'unite d'une constante globale (Reglages.Pression [bar]) vaut aussi DANS LES SCRIPTS - la regle des
// variables : l'affectation a une cible d'une autre unite convertit d'office, un calcul qui en part demande EN.
// Jusqu'a la 1.13.0, cette unite etait ignoree (le nombre passait tel quel, le calcul compilait).
void constanteGlobaleDansUnScript() {
    std::printf("-- 1.13.1 : l'unite d'une constante globale dans un script\n");
    const auto reglages = [](Project& q) {
        Script r;
        r.id = q.allocate();
        r.name = "Reglages";
        r.event = "Appel";
        Declaration d;
        d.kind = DeclKind::Constant;
        d.name = "Pression";
        d.type = "REAL";
        d.value = "2.5";
        d.unit = "bar";
        d.visibility = Visibility::Public;
        r.decls.push_back(d);
        q.programs.scripts.push_back(r);
        uniqueDeclarationIds(q);
    };
    {
        Bench b("Consigne_mbar := Reglages.Pression;", {{"Consigne_mbar", "REAL", "mbar", "0"}}, reglages);
        std::string err;
        const double v = b.after("Consigne_mbar", &err);
        check(err.empty() && std::fabs(v - 2500.0) < 1e-9, "Consigne_mbar := Reglages.Pression (2,5 bar) : 2500, converti d'office (" + std::to_string(v) + ") " + err);
    }
    {
        Bench b("R := EN(Reglages.Pression, 'mbar');\nRS := UNITE(Reglages.Pression);", {}, reglages);
        const double v = b.real();
        check(std::fabs(v - 2500.0) < 1e-9, "EN(Reglages.Pression, 'mbar') dans un script : 2500 (" + std::to_string(v) + ")");
        sim::Value s;
        check(b.rt.environment().read("RS", s) && s.asString() == "bar", "UNITE(Reglages.Pression) dans un script : 'bar'");
    }
    Project p;
    reglages(p);
    var(p, "Consigne_mbar", "REAL", "mbar");
    scriptcheck::Scope sc;
    sc.project = &p;
    check(hasFinding(scriptcheck::check(sc, "Consigne_mbar := Reglages.Pression * 1000.0;"), "seule une variable", scriptcheck::Finding::Severity::Error),
          "Compiler : Consigne_mbar := Reglages.Pression * 1000.0 - une erreur, le calcul demande EN (avant 1.13.1 : accepte)");
    check(!hasFinding(scriptcheck::check(sc, "Consigne_mbar := EN(Reglages.Pression, 'mbar') * 2.0;"), "seule une variable", scriptcheck::Finding::Severity::Error),
          "Compiler : Consigne_mbar := EN(Reglages.Pression, 'mbar') * 2.0 - accept\xC3\xA9");
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
    affectationEnMarche();     // 1.13.0 : la relecture, groupe A
    affichage();
    enSurUnChemin();
    correctionDUnClic();
    tableauEtHeritage();
    minMaxLimit();
    graviteScript();
    enEnMarche();
    facteurSansPerte();
    genreDesFamilles();
    localesRetourArguments();
    signeMicro();
    colonneUniteDesDeclarations();   // 1.13.0 (seconde passe, lot D3)
    correctionALaBonnePlace();
    passagesEtInstructions();
    tableauEntier();
    cacheDesUnites();
    affichageEtDeclarations();
    lotF3();                   // 1.13.0 (lot F3) : les defauts vus aux captures
    constanteGlobaleDansUnScript();   // 1.13.1 (lot K1)
    std::printf("=== %d verifications, %d echec(s) ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

