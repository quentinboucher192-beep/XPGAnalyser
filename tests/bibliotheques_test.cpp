// =============================================================================
//  tests/bibliotheques_test.cpp - 1.13.0 (lot J8) : les espaces de noms et les bibliotheques
// -----------------------------------------------------------------------------
//  - le modele : nom complet (Gaz.T_Vanne), recherche par nom complet ou court,
//    les espaces declares ;
//  - le resolveur (hmi::names) : USING lu puis blanchi, la regle du plus long
//    chemin, la portee (son espace, la racine, les USING, l'ambiguite), la
//    reecriture pour le moteur, le renommage d'un espace ;
//  - le moteur : appels qualifies, USING, collision levee par l'espace de l'item,
//    type d'un espace en locale et en variable, IHM_APPELER('Gaz.Horloge'),
//    methode d'un type d'un espace ;
//  - le controle (Compiler, Generer) : ambigu sous USING, USING inconnu ou inutile,
//    Gaz.Inconnu, nom reserve, bibliotheque introuvable ;
//  - la serialisation : espace=, les liens et leur cache, format 33 en escalier ;
//  - les bibliotheques : publier un .xpgbib, relier (lecture seule, ids a part),
//    versions (reliee, plus recente, plus ancienne, incompatible, absente), relier
//    sous un autre nom, mettre a jour (le diff et ses ruptures), delier.
//
//  BIB_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiLibrary.hpp"
#include "../src/hmi/HmiMethods.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNames.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/hmi/HmiUnits.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace hmi;
namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("BIB_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

// ---- de petits assembleurs de modele -----------------------------------------
HmiType& addType(Project& p, const std::string& name, const std::string& ns = {}) {
    HmiType t;
    t.id = p.allocate();
    t.name = name;
    t.ns = ns;
    p.programs.types.push_back(std::move(t));
    return p.programs.types.back();
}
TypeMember member(const std::string& name, const std::string& type) {
    TypeMember m;
    m.name = name;
    m.type = type;
    return m;
}
HmiFunction& addFn(Project& p, const std::string& name, const std::string& ret, const std::string& body, const std::string& ns = {}) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    f.body = body;
    f.ns = ns;
    p.programs.functions.push_back(std::move(f));
    return p.programs.functions.back();
}
Script& addScript(Project& p, const std::string& name, const std::string& body, const std::string& ns = {}) {
    Script s;
    s.id = p.allocate();
    s.name = name;
    s.event = "Appel";
    s.body = body;
    s.ns = ns;
    p.programs.scripts.push_back(std::move(s));
    return p.programs.scripts.back();
}
Variable& addVar(Project& p, const std::string& n, const std::string& t, const std::string& init = {}) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}
std::string indexText(const std::vector<ProjectFile>& files) {
    for (const auto& f : files)
        if (f.path == "ihm.txt" && f.data) return std::string(f.data->begin(), f.data->end());
    return {};
}
std::function<bool(const std::string&, std::string&)> readerOf(const std::vector<ProjectFile>& files) {
    return [&files](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path && f.data) {
                content.assign(f.data->begin(), f.data->end());
                return true;
            }
        return false;
    };
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

// Le script `name` du projet (deja pose), lance ; ce qu'il laisse dans `readVar`.
std::string runScript(Project& p, const std::string& name, std::string* err = nullptr, const std::string& readVar = "R") {
    if (p.views.empty()) {
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
    }
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript(name, 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readVar, out) ? shown(out) : std::string("#");
}
// Un script "Essai" (a la racine, ou dans `ns`), lance.
std::string run(Project& p, const std::string& body, std::string* err = nullptr, const std::string& ns = {}, const std::string& readVar = "R") {
    addScript(p, "Essai", body, ns);
    return runScript(p, ns.empty() ? "Essai" : ns + ".Essai", err, readVar);
}

std::vector<scriptcheck::Finding> checkOf(const Project& p, const std::string& code, const std::string& ns = {},
                                          const HmiFunction* fn = nullptr) {
    scriptcheck::Scope s;
    s.project = &p;
    s.function = fn;
    s.ns = ns;
    s.plcKnown = [](std::string_view) { return false; };
    return scriptcheck::check(s, code);
}
bool hasFinding(const std::vector<scriptcheck::Finding>& fs, const std::string& needle, bool error = true) {
    for (const auto& f : fs)
        if (f.message.find(needle) != std::string::npos && (f.severity == scriptcheck::Finding::Severity::Error) == error) return true;
    return false;
}
std::string allMessages(const std::vector<scriptcheck::Finding>& fs) {
    std::string out;
    for (const auto& f : fs) out += " | " + f.message;
    return out;
}
std::vector<Issue> generOf(const Project& p) { return generate(p, [](std::string_view) { return false; }); }
bool hasIssue(const std::vector<Issue>& is, const std::string& needle, Issue::Severity s = Issue::Severity::Error) {
    for (const auto& i : is)
        if (i.severity == s && i.message.find(needle) != std::string::npos) return true;
    return false;
}

// Le projet fil rouge : l'espace Gaz (T_Vanne, EnDefaut, Horloge), l'espace Util (Moyenne, Borne).
Project buildProject() {
    Project p;
    HmiType& v = addType(p, "T_Vanne", "Gaz");
    v.members.push_back(member("Position", "REAL"));
    v.members.push_back(member("Defaut", "BOOL"));
    addFn(p, "EnDefaut", "BOOL", "VAR_INPUT v : T_Vanne; END_VAR\nEnDefaut := v.Defaut OR v.Position < 1.0;", "Gaz");
    addFn(p, "Moyenne", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nMoyenne := (a + b) / 2.0;", "Util");
    addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; lo : REAL; hi : REAL; END_VAR\nBorne := MIN(MAX(x, lo), hi);", "Util");
    addVar(p, "R", "REAL");
    return p;
}

} // namespace

// =============================================================================
//  1) LE MODELE : noms complets, recherches, espaces declares
// =============================================================================
void modele() {
    std::printf("-- modele : noms complets, recherches, espaces\n");
    check(qualifiedName("Gaz", "T_Vanne") == "Gaz.T_Vanne" && qualifiedName("", "T_X") == "T_X", "qualifiedName");
    check(splitQualified("Gaz.Ligne.T_X") == std::make_pair(std::string("Gaz.Ligne"), std::string("T_X")), "splitQualified : le dernier point");
    check(splitQualified("T_X").first.empty(), "splitQualified : sans espace");
    check(isNamespaceName("Gaz") && isNamespaceName("Gaz.Ligne") && !isNamespaceName("Gaz.") && !isNamespaceName("1Gaz") && !isNamespaceName("Gaz Ligne"),
          "isNamespaceName : des identifiants separes par des points");

    Project p = buildProject();
    const HmiType* t = p.hmiTypeByName("Gaz.T_Vanne");
    check(t && t->name == "T_Vanne", "hmiTypeByName(Gaz.T_Vanne) : le type de l'espace Gaz");
    check(p.hmiTypeByName("gaz.t_vanne") == t, "sans casse");
    check(p.hmiTypeByName("T_Vanne") == t, "un nom court : le seul de ce nom (repli)");
    check(p.hmiTypeByName("Util.T_Vanne") == nullptr, "Util.T_Vanne : pas dans cet espace");
    check(p.functionsNamed("Util.Moyenne").size() == 1 && p.functionByName("Util.Moyenne"), "functionsNamed(Util.Moyenne)");
    // Deux espaces qui definissent le meme nom : le nom court ne trouve rien (ambigu), les noms complets oui.
    addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x;", "Gaz");
    check(p.functionsNamed("Borne").empty(), "Borne dans Gaz et Util : le nom court est ambigu (rien)");
    check(p.functionsNamed("Gaz.Borne").size() == 1 && p.functionsNamed("Util.Borne").size() == 1, "les noms complets les distinguent");
    // Un item de la racine passe avant le repli.
    addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := 0.0;");
    check(p.functionsNamed("Borne").size() == 1 && p.functionsNamed("Borne").front()->ns.empty(), "la racine passe avant les espaces");
    // La cle d'une surcharge : Util.Moyenne#id.
    const HmiFunction* m = p.functionByName("Util.Moyenne");
    check(m && p.functionByKey("Util.Moyenne#" + std::to_string(m->id)) == m, "functionByKey(Util.Moyenne#id)");
    addScript(p, "Horloge", "R := 99.0;", "Gaz");
    check(p.generalScript("Gaz.Horloge") && !p.generalScript("Util.Horloge"), "generalScript(Gaz.Horloge)");
    // Les espaces declares : ceux des items, leurs parents, les espaces vides gardes.
    addType(p, "T_X", "Gaz.Ligne").members.push_back(member("a", "INT"));
    p.listFolders["espaces"].push_back("Vide");
    const auto ns = p.namespaces();
    const auto has = [&](const char* n) { return std::find(ns.begin(), ns.end(), n) != ns.end(); };
    check(has("Gaz") && has("Util") && has("Gaz.Ligne") && has("Vide") && ns.size() == 4, "namespaces() : Gaz, Gaz.Ligne, Util, Vide");
    check(p.isNamespace("gaz") && p.isNamespace("Gaz.Ligne") && !p.isNamespace("T_Vanne"), "isNamespace");
    // Le registre des types : le nom complet, la cle du projet, la provenance.
    const auto reg = typereg::Registry::build(p);
    const auto* e = reg->byName("Gaz.T_Vanne");
    check(e && e->name == "Gaz.T_Vanne" && e->key.rfind("ihm:", 0) == 0 && e->ns == "Gaz" && e->provenance == "Espace Gaz",
          "registre : Gaz.T_Vanne, cle ihm:, provenance Espace Gaz");
    check(reg->byName("T_Vanne") == e, "registre : le nom court d'un seul type le trouve");
    const auto r = reg->resolve("ARRAY[1..3] OF Gaz.T_Vanne", typereg::UseVariable);
    check(r.ok && r.text == "ARRAY[1..3] OF Gaz.T_Vanne", "registre : ARRAY[1..3] OF Gaz.T_Vanne se lit [" + r.text + " " + r.why + "]");
}

// =============================================================================
//  2) LE RESOLVEUR : USING, la regle, la portee, la reecriture, le renommage
// =============================================================================
void resolveur() {
    std::printf("-- resolveur : USING, regle du plus long chemin, portee, reecriture, renommage\n");
    const std::string code = "(* tete *)\nUSING Gaz;\nUSING Util;\nR := Moyenne(1.0, 2.0);\n";
    const auto us = names::usings(code);
    check(us.size() == 2 && us[0].ns == "Gaz" && us[0].line == 2 && us[0].column == 1 && us[1].ns == "Util" && us[1].line == 3,
          "usings : deux directives, leur ligne et leur colonne");
    const std::string blank = names::blankUsings(code);
    check(blank.size() == code.size() && blank.find("USING") == std::string::npos && blank.find("R := Moyenne") == code.find("R := Moyenne"),
          "blankUsings : les directives blanchies, les positions gardees");
    check(names::usings("R := 'USING Gaz;'; // USING Util;").empty(), "USING dans une chaine ou un commentaire : pas une directive");
    check(names::usings("x := USING;").empty(), "un mot USING qui n'est pas en tete d'instruction : rien");
    check(names::usings("VAR x : INT; END_VAR USING Gaz;").size() == 1, "USING apres END_VAR (les declarations sur la ligne 1) : lu");

    Project p = buildProject();
    addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x;", "Gaz");
    const names::Index ix(p);
    std::string full;
    check(names::qualifiedPrefix(ix, {"Gaz", "T_Vanne", "Position"}, &full) == 2 && full == "Gaz.T_Vanne",
          "le plus long chemin : Gaz.T_Vanne (puis .Position)");
    check(names::qualifiedPrefix(ix, {"Four1", "Temperature"}) == 0, "Four1.Temperature : pas un espace, un acces membre");
    check(names::qualifiedPrefix(ix, {"Gaz", "Inconnu"}) == 0, "Gaz.Inconnu : l'espace n'a pas ce nom");

    names::Scope inGaz;
    inGaz.own = "Gaz";
    check(names::resolveBare(ix, inGaz, "Borne").full == "Gaz.Borne", "dans Gaz, Borne est Gaz.Borne (son espace d'abord)");
    check(names::resolveBare(ix, inGaz, "T_Vanne").full == "Gaz.T_Vanne", "dans Gaz, T_Vanne est Gaz.T_Vanne");
    names::Scope both;
    both.imports = {"Gaz", "Util"};
    const auto amb = names::resolveBare(ix, both, "Borne");
    check(amb.full.empty() && amb.ambiguous.size() == 2, "USING Gaz et Util, Borne : ambigu (deux candidats)");
    check(names::resolveBare(ix, both, "Moyenne").full == "Util.Moyenne", "USING Util : Moyenne est Util.Moyenne");
    names::Scope hidden = both;
    hidden.hidden.insert("MOYENNE");
    check(names::resolveBare(ix, hidden, "Moyenne").full.empty(), "une locale du meme nom passe avant");
    names::Scope root;
    const auto hint = names::resolveBare(ix, root, "Moyenne");
    check(hint.full.empty() && hint.hint == "Util.Moyenne", "a la racine sans USING : rien, avec la piste Util.Moyenne");

    // La reecriture pour le moteur.
    const std::string q = names::qualify(p, "USING Util;\nR := Moyenne(Borne(1.0, 0.0, 5.0), 2.0);\nR := v.Moyenne;", "");
    check(q.find("USING") == std::string::npos && q.find("Util.Moyenne(Util.Borne(") != std::string::npos && q.find("v.Moyenne") != std::string::npos,
          "qualify : USING retire, noms courts ecrits en entier, un membre intact [" + q + "]");
    const std::string q2 = names::qualify(p, "VAR_TEMP v : T_Vanne; END_VAR\nR := Borne(1.0);", "Gaz");
    check(q2.find("v : Gaz.T_Vanne") != std::string::npos && q2.find("Gaz.Borne(1.0)") != std::string::npos,
          "qualify dans Gaz : le type et la fonction de son espace [" + q2 + "]");
    const std::string q3 = names::qualify(p, "VAR_TEMP Borne : REAL; END_VAR\nBorne := 1.0;", "Gaz");
    check(q3.find("Gaz.Borne") == std::string::npos, "une locale du nom d'une fonction de l'espace : intacte");
    check(names::qualify(p, "R := 'Borne';", "Gaz") == "R := 'Borne';", "une chaine : intacte");
    check(names::qualify(p, "R := f(Borne := 1);", "Gaz").find("Gaz.Borne") == std::string::npos, "un nom d'argument : intact");

    // Le renommage d'un espace.
    check(names::renameInCode("USING Gaz;\nx := Gaz.EnDefaut(v);\nIHM_APPELER('Gaz.Horloge');\ny := Gazette.X;", "Gaz", "Neo")
              == "USING Neo;\nx := Neo.EnDefaut(v);\nIHM_APPELER('Neo.Horloge');\ny := Gazette.X;",
          "renameInCode : USING, appel, IHM_APPELER ; un autre nom intact");
    check(names::renameInType("ARRAY[1..3] OF Gaz.T_Vanne", "Gaz", "Neo") == "ARRAY[1..3] OF Neo.T_Vanne", "renameInType");
    check(names::renameInCode("USING Gaz;", "Gaz", "Gaz.Old") == "USING Gaz.Old;", "renommer vers un sous-espace de soi : une fois");
    {
        Project r = buildProject();
        addVar(r, "V1", "Gaz.T_Vanne");
        addScript(r, "S", "USING Gaz;\nR := TO_REAL(Gaz.EnDefaut(V1));");
        const std::size_t n = names::renameNamespace(r, "Gaz", "Gas");
        check(n >= 3 && r.programs.types.front().ns == "Gas" && r.programs.variables.back().type == "Gas.T_Vanne"
                  && r.programs.scripts.back().body.find("USING Gas;") != std::string::npos && r.programs.scripts.back().body.find("Gas.EnDefaut") != std::string::npos,
              "renameNamespace : l'espace des items, les types, le code (" + std::to_string(n) + " changements)");
    }
    check(!names::validNewNamespace(p, "Gaz") && !names::validNewNamespace(p, "Ga z") && names::validNewNamespace(p, "Neo"), "validNewNamespace");
    {
        Project c = buildProject();
        addVar(c, "Gaz", "INT");
        check(!names::reservedClash(c, "Gaz").empty(), "reservedClash : une variable Gaz");
    }
    {
        // Changer l'espace d'un item (l'inspecteur) : les noms complets du code le suivent, un nom court reste.
        Project m = buildProject();
        addVar(m, "V1", "Gaz.T_Vanne");
        addScript(m, "Horloge", "R := 1.0;", "Gaz");
        Script& s = addScript(m, "S", "R := TO_REAL(Gaz.EnDefaut(V1));\nIHM_APPELER('Gaz.Horloge');\nR := Util.Moyenne(1.0, 3.0);\nR := Moyenne(1.0, 3.0);");
        const Id sid = s.id;
        const HmiFunction* f = m.functionByName("Util.Moyenne");
        std::string why;
        check(f && names::setItemNamespace(m, names::ItemKind::Function, f->id, "Calc", &why), "setItemNamespace : Util.Moyenne -> Calc [" + why + "]");
        const Script* after = m.script(sid);
        check(after && after->body.find("R := Calc.Moyenne(1.0, 3.0);") != std::string::npos && after->body.find("R := Moyenne(1.0, 3.0);") != std::string::npos,
              "le code suit : Util.Moyenne devient Calc.Moyenne, le nom court reste [" + (after ? after->body : std::string("?")) + "]");
        const Script* clock = m.generalScript("Gaz.Horloge");
        check(clock && names::setItemNamespace(m, names::ItemKind::Script, clock->id, "", &why)
                  && m.script(sid)->body.find("IHM_APPELER('Horloge')") != std::string::npos,
              "un script vers la racine : IHM_APPELER('Gaz.Horloge') devient 'Horloge'");
        const HmiType* t = m.hmiTypeByName("Gaz.T_Vanne");
        check(t && names::setItemNamespace(m, names::ItemKind::Type, t->id, "Eau", &why) && m.programs.variables.back().type == "Eau.T_Vanne",
              "un type : la variable V1 suit (Eau.T_Vanne)");
        check(!names::setItemNamespace(m, names::ItemKind::Function, m.functionByName("Gaz.EnDefaut")->id, "R", &why) && !why.empty(),
              "un espace qui porte le nom d'une variable : refuse [" + why + "]");
    }
}

// =============================================================================
//  3) LE MOTEUR
// =============================================================================
void moteur() {
    std::printf("-- moteur : appels qualifies, USING, collision, types d'un espace, IHM_APPELER, methode\n");
    std::string err;
    {
        Project p = buildProject();
        const std::string r = run(p, "R := Util.Moyenne(4.0, 6.0);", &err);
        check(r == "5", "appel qualifie Util.Moyenne(4, 6) = 5 [" + r + " " + err + "]");
    }
    {
        Project p = buildProject();
        const std::string r = run(p, "USING Util;\nR := Moyenne(4.0, 8.0);", &err);
        check(r == "6", "USING Util; Moyenne(4, 8) = 6 [" + r + " " + err + "]");
    }
    {
        // Collision : Gaz.Borne et Util.Borne - l'espace de l'item tranche.
        Project p = buildProject();
        addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x * 10.0;", "Gaz");
        std::string r = run(p, "R := Borne(2.0);", &err, "Gaz");
        check(r == "20", "un script de Gaz : Borne est Gaz.Borne (2 x 10 = 20) [" + r + " " + err + "]");
        Project q = buildProject();
        addFn(q, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x * 10.0;", "Gaz");
        r = run(q, "USING Util;\nR := Borne(12.0, 0.0, 5.0);", &err);
        check(r == "5", "a la racine, USING Util : Borne est Util.Borne (borne a 5) [" + r + " " + err + "]");
        Project s = buildProject();
        addFn(s, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x * 10.0;", "Gaz");
        r = run(s, "R := Gaz.Borne(3.0) + Util.Borne(9.0, 0.0, 1.0);", &err);
        check(r == "31", "les deux par leur nom complet (30 + 1) [" + r + " " + err + "]");
    }
    {
        // Une fonction de l'espace appelle sa voisine par son nom court.
        Project p = buildProject();
        addFn(p, "Double", "REAL", "VAR_INPUT x : REAL; END_VAR\nDouble := Moyenne(x, x) * 2.0;", "Util");
        const std::string r = run(p, "R := Util.Double(3.0);", &err);
        check(r == "6", "dans Util, Double appelle Moyenne sans prefixe (6) [" + r + " " + err + "]");
    }
    {
        // Un type d'un espace : en locale (nom complet, ou court sous USING), en variable IHM.
        Project p = buildProject();
        std::string r = run(p, "VAR_TEMP v : Gaz.T_Vanne; END_VAR\nv.Position := 42.0;\nR := v.Position;", &err);
        check(r == "42", "VAR_TEMP v : Gaz.T_Vanne (42) [" + r + " " + err + "]");
        Project q = buildProject();
        r = run(q, "USING Gaz;\nVAR_TEMP v : T_Vanne; END_VAR\nv.Position := 7.0;\nR := v.Position;", &err);
        check(r == "7", "USING Gaz; VAR_TEMP v : T_Vanne (7) [" + r + " " + err + "]");
        Project s = buildProject();
        addVar(s, "V1", "Gaz.T_Vanne");
        r = run(s, "V1.Position := 7.5;\nR := V1.Position;", &err);
        check(r == "7.5", "une variable IHM de type Gaz.T_Vanne (7.5) [" + r + " " + err + "]");
        Project t = buildProject();
        addVar(t, "V1", "Gaz.T_Vanne");
        addVar(t, "B", "BOOL");
        r = run(t, "V1.Position := 0.5;\nB := Gaz.EnDefaut(V1);", &err, {}, "B");
        check(r == "TRUE", "Gaz.EnDefaut(V1) : une structure de l'espace en parametre (TRUE) [" + r + " " + err + "]");
    }
    {
        Project p = buildProject();
        addScript(p, "Horloge", "R := 99.0;", "Gaz");
        const std::string r = run(p, "IHM_APPELER('Gaz.Horloge');", &err);
        check(r == "99", "IHM_APPELER('Gaz.Horloge') (99) [" + r + " " + err + "]");
    }
    {
        // Une methode d'un type d'un espace (lot J1) : son corps voit l'espace.
        Project p = buildProject();
        HmiType& pompe = addType(p, "T_Pompe", "Gaz");
        pompe.members.push_back(member("Debit", "REAL"));
        HmiFunction m;
        m.id = p.allocate();
        m.name = "Regler";
        m.body = "Debit := Util.Moyenne(10.0, 20.0);";
        pompe.methods.push_back(m);
        addVar(p, "P1", "Gaz.T_Pompe");
        const std::string r = run(p, "P1.Regler();\nR := P1.Debit;", &err);
        check(r == "15", "une methode d'un type de Gaz (P1.Regler() : 15) [" + r + " " + err + "]");
    }
    {
        // Sans espace : rien ne change (le meme code, le meme resultat).
        Project p;
        addFn(p, "Moyenne", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nMoyenne := (a + b) / 2.0;");
        addVar(p, "R", "REAL");
        const std::string r = run(p, "R := Moyenne(1.0, 3.0);", &err);
        check(r == "2" && !names::active(p), "un projet sans espace : inchange (2) [" + r + " " + err + "]");
    }
}

// =============================================================================
//  4) LE CONTROLE : Compiler (scriptcheck) et Generer
// =============================================================================
void controle() {
    std::printf("-- controle : ambigu, USING inconnu/inutile, Gaz.Inconnu, nom reserve\n");
    Project p = buildProject();
    addFn(p, "Borne", "REAL", "VAR_INPUT x : REAL; END_VAR\nBorne := x;", "Gaz");
    addVar(p, "V1", "Gaz.T_Vanne");
    addVar(p, "B", "BOOL");
    {
        const auto fs = checkOf(p, "R := Util.Moyenne(1.0, 2.0);\nB := Gaz.EnDefaut(V1);\nV1.Position := 3.0;");
        check(fs.empty(), "des noms complets : aucune faute" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "VAR_TEMP v : Gaz.T_Vanne; END_VAR\nv.Position := 1.0;\nR := v.Position;");
        check(fs.empty(), "une locale d'un type d'un espace : aucune faute" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "USING Gaz;\nUSING Util;\nR := Borne(1.0);");
        bool fix = false;
        for (const auto& f : fs) fix = fix || (f.fixFrom == "Borne" && (f.fixTo == "Gaz.Borne" || f.fixTo == "Util.Borne") && f.line == 3);
        check(hasFinding(fs, "ambigu entre Gaz.Borne et Util.Borne") && fix, "USING Gaz et Util, Borne : ambigu, a sa ligne, la correction d'un clic" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "USING Gazz;\nR := 1.0;");
        check(hasFinding(fs, "aucun espace de noms Gazz") && hasFinding(fs, "veux-tu dire Gaz"), "USING Gazz : inconnu, veux-tu dire Gaz" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "USING Gaz;\nR := 1.0;");
        bool fix = false;
        for (const auto& f : fs) fix = fix || (f.fixFrom == "USING Gaz;" && f.fixTo.empty());
        check(hasFinding(fs, "ne sert pas", false) && fix, "USING inutile : un avertissement, retire-le d'un clic" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "R := Util.Moyene(1.0, 2.0);");
        check(hasFinding(fs, "Util est un espace de noms : il n'a pas de Moyene") && hasFinding(fs, "Util.Moyenne"),
              "Util.Moyene : l'espace n'a pas ce nom, veux-tu dire Util.Moyenne" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "R := Moyenne(1.0, 2.0);");
        check(hasFinding(fs, "est dans l'espace Util", false), "Moyenne a la racine sans USING : avertissement, ecris Util.Moyenne" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "R := Borne(1.0);", "Gaz");
        check(fs.empty(), "dans Gaz, Borne (de son espace) : aucune faute" + allMessages(fs));
    }
    {
        const auto fs = checkOf(p, "R := MAX(1.0, 2.0);");
        check(fs.empty(), "MAX (une fonction standard) : jamais confondue avec un item d'espace" + allMessages(fs));
    }
    // Generer : un espace porte le nom d'une variable ; deux types de meme nom dans deux espaces : permis.
    {
        Project g = buildProject();
        addVar(g, "Gaz", "INT");
        check(hasIssue(generOf(g), "est \xC3\xA0 la fois un espace de noms et une variable IHM"), "Generer : une variable Gaz refusee (nom reserve)");
    }
    {
        Project g = buildProject();
        addType(g, "T_Vanne", "Util").members.push_back(member("x", "INT"));
        const auto is = generOf(g);
        check(!hasIssue(is, "type IHM en double"), "Generer : Gaz.T_Vanne et Util.T_Vanne ne sont pas en double");
    }
    {
        Project g = buildProject();
        addType(g, "T_X", "Gaz..Ligne").members.push_back(member("x", "INT"));
        check(hasIssue(generOf(g), "espace de noms illisible"), "Generer : un espace illisible refuse");
    }
}

// =============================================================================
//  5) LA SERIALISATION : espace=, format 33 en escalier
// =============================================================================
void serialisation() {
    std::printf("-- serialisation : espace=, format 33 en escalier\n");
    Project p = buildProject();
    addScript(p, "Horloge", "R := 1.0;", "Gaz");
    p.listFolders["espaces"].push_back("Vide");
    const auto files = serializeProject(p);
    const std::string idx = indexText(files);
    check(idx.find("(format 33)") != std::string::npos && idx.find("espace=\"Gaz\"") != std::string::npos && idx.find("espace=\"Util\"") != std::string::npos,
          "un espace pose : format 33, espace= ecrit");
    auto back = parseProject(readerOf(files));
    check(back.has_value(), "relu");
    if (back) {
        const HmiType* t = back->hmiTypeByName("Gaz.T_Vanne");
        check(t && t->ns == "Gaz" && back->functionByName("Util.Moyenne") && back->generalScript("Gaz.Horloge"), "relu : les espaces des items");
        const auto ns = back->namespaces();
        check(std::find(ns.begin(), ns.end(), "Vide") != ns.end(), "relu : l'espace vide garde");
        check(serializeProject(*back).size() == files.size() && indexText(serializeProject(*back)) == idx, "l'aller-retour est stable");
    }
    // Sans espace : le format d'avant.
    Project plain;
    addFn(plain, "Moyenne", "REAL", "Moyenne := 1.0;");
    check(indexText(serializeProject(plain)).find("(format 33)") == std::string::npos, "sans espace : PAS de format 33 (escalier)");
    Project units;
    addVar(units, "P", "REAL").unit = "bar";
    const std::string uidx = indexText(serializeProject(units));
    check(uidx.find("(format 32)") != std::string::npos, "une unite seule : format 32 (le cran du dessous)");
    // Un format 34 (plus recent) : refuse.
    auto tooNew = parseProject([](const std::string& path, std::string& content) {
        if (path != "ihm.txt") return false;
        content = "# IHM Studio - projet IHM (format 34)\nihm format=34 prochain_id=1\n";
        return true;
    });
    check(!tooNew, "un format 34 (plus recent) est refuse proprement");
}

// =============================================================================
//  6) LES BIBLIOTHEQUES
// =============================================================================
namespace {
fs::path tempRoot() {
    const fs::path root = fs::temp_directory_path() / ("xpg_bib_test_" + std::to_string(static_cast<long long>(std::rand())));
    fs::create_directories(root);
    return root;
}
// Une bibliotheque Util publiee a la version `version` (Moyenne, T_Point ; `extra` : une fonction de plus).
std::string publish(const fs::path& where, const std::string& version, bool extra = false, bool withBorne = true) {
    Project src;
    src.config.name = "Source";
    HmiType& pt = addType(src, "T_Point", "Util");
    pt.members.push_back(member("X", "REAL"));
    pt.members.push_back(member("Y", "REAL"));
    addFn(src, "Moyenne", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nMoyenne := (a + b) / 2.0;", "Util");
    if (withBorne) addFn(src, "Borne", "REAL", "VAR_INPUT x : REAL; lo : REAL; hi : REAL; END_VAR\nBorne := MIN(MAX(x, lo), hi);", "Util");
    addFn(src, "Milieu", "REAL", "VAR_INPUT p : T_Point; END_VAR\nMilieu := Moyenne(p.X, p.Y);", "Util");
    if (extra) addFn(src, "Ecart", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nEcart := ABS(a - b);", "Util");
    addScript(src, "Init", "R := 11.0;", "Util");
    addVar(src, "R", "REAL");
    biblio::PublishSpec spec;
    spec.name = "Util";
    spec.version = version;
    spec.description = "les outils";
    const auto plan = biblio::collect(src, spec);
    const fs::path file = where / ("Util-" + version + ".xpgbib");
    const auto st = biblio::writeFile(plan.package, file.string());
    return st ? file.string() : std::string{};
}
} // namespace

void bibliotheques() {
    std::printf("-- bibliotheques : publier, relier, versions, alias, mettre a jour, delier\n");
    check(biblio::parseVersion("1.4.0").ok && biblio::parseVersion("1.4").patch == 0 && !biblio::parseVersion("1.x").ok, "parseVersion");
    check(biblio::stateOf("1.4.0", "1.2.0", "1.2.0") == LibraryState::Newer, "1.4 pour 1.2 : plus recente compatible");
    check(biblio::stateOf("1.1.0", "1.2.0", "1.2.0") == LibraryState::Older, "1.1 pour >= 1.2 : plus ancienne");
    check(biblio::stateOf("2.0.0", "1.4.0", "1.2.0") == LibraryState::Incompatible, "2.0 pour 1.x : incompatible");
    check(biblio::stateOf("1.2.0", "1.2.0", "") == LibraryState::Linked, "la meme : reliee");
    check(biblio::bumped("1.4.2", biblio::Bump::Major) == "2.0.0" && biblio::bumped("1.4.2", biblio::Bump::Minor) == "1.5.0"
              && biblio::bumped("1.4.2", biblio::Bump::Patch) == "1.4.3",
          "bumped : majeur, mineur, correctif");

    const fs::path root = tempRoot();
    const fs::path projectDir = root / "Projet";
    fs::create_directories(projectDir / "libs");
    const std::string file = publish(projectDir / "libs", "1.2.0");
    check(!file.empty() && fs::exists(file), "publier : Util-1.2.0.xpgbib ecrit");
    {
        // Ce qui manquerait : une variable IHM lue.
        Project src;
        addFn(src, "Lire", "REAL", "Lire := Pression;", "Util");
        addVar(src, "Pression", "REAL");
        biblio::PublishSpec spec;
        spec.name = "Util";
        const auto plan = biblio::collect(src, spec);
        check(plan.refusal.empty() && !plan.missing.empty() && plan.missing.front().find("Pression") != std::string::npos,
              "publier : une variable IHM employee est dite (elle ne voyage pas)");
        biblio::PublishSpec empty;
        empty.name = "Rien";
        check(!biblio::collect(src, empty).refusal.empty(), "publier un espace vide : refuse");
    }
    auto pkg = biblio::readFile(file);
    check(pkg && pkg->manifest.name == "Util" && pkg->manifest.version == "1.2.0" && pkg->content.programs.functions.size() == 3,
          "relire le .xpgbib : nom, version, contenu");

    // Relier.
    Project p;
    p.config.name = "Projet";
    addVar(p, "R", "REAL");
    auto plan = biblio::planLink(p, file, projectDir.string());
    check(plan.ok() && plan.link.path == "libs/Util-1.2.0.xpgbib" && plan.link.version == "1.2.0" && plan.link.functions.size() == 3,
          "planLink : chemin relatif au projet, version, interface (" + plan.refusal + ")");
    check(biblio::link(p, plan, projectDir.string()) && p.libraries.size() == 1, "relier");
    const LoadedLibrary* ld = p.loadedLibrary("Util");
    check(ld && ld->state == LibraryState::Linked && !ld->fromCache, "le contenu est lu (reliee)");
    const HmiFunction* moy = p.functionByName("Util.Moyenne");
    check(moy && isLibraryId(moy->id) && p.libraryOf(moy->id) == ld, "ses fonctions : des identifiants a part (lecture seule)");
    check(moy && const_cast<Project&>(p).function(moy->id) == nullptr, "lecture seule : la version qui modifie ne la voit pas");
    check(p.programs.functions.empty(), "le code n'est PAS recopie dans le projet");
    std::string err;
    {
        Project q = p;            // une copie partage le contenu lu
        const std::string r = run(q, "R := Util.Moyenne(2.0, 4.0);", &err);
        check(r == "3", "appeler une fonction de bibliotheque (3) [" + r + " " + err + "]");
    }
    {
        Project q = p;
        const std::string r = run(q, "VAR_TEMP pt : Util.T_Point; END_VAR\npt.X := 2.0; pt.Y := 8.0;\nR := Util.Milieu(pt);", &err);
        check(r == "5", "un type et une fonction de bibliotheque qui s'appellent entre eux (5) [" + r + " " + err + "]");
    }
    {
        Project q = p;
        const std::string r = run(q, "IHM_APPELER('Util.Init');", &err);
        check(r == "11", "IHM_APPELER d'un script de bibliotheque (11) [" + r + " " + err + "]");
    }
    {
        const auto reg = typereg::Registry::build(p);
        const auto* e = reg->byName("Util.T_Point");
        check(e && e->key == "bib:Util.T_Point" && e->readOnly && e->provenance.find("Biblioth\xC3\xA8que Util") == 0,
              "registre : bib:Util.T_Point, lecture seule, provenance Bibliotheque Util");
    }
    // Relier deux fois le meme nom : refuse ; sous un autre nom : permis.
    check(!biblio::planLink(p, file, projectDir.string()).ok(), "relier une seconde Util : refuse (Relier sous le nom...)");
    {
        Project q = p;
        auto alias = biblio::planLink(q, file, projectDir.string(), "Outils");
        check(alias.ok() && alias.link.published == "Util" && biblio::link(q, alias, projectDir.string()), "relier sous le nom Outils");
        Project s = q;
        const std::string r2 = run(s, "VAR_TEMP pt : Outils.T_Point; END_VAR\npt.X := 1.0; pt.Y := 3.0;\nR := Outils.Milieu(pt);", &err);
        check(r2 == "2", "un alias : Outils.Milieu (son code renvoie a Outils) (2) [" + r2 + " " + err + "]");
    }
    // Un espace du projet du meme nom : refuse.
    {
        Project q;
        addFn(q, "X", "INT", "X := 1;", "Util");
        check(!biblio::planLink(q, file, projectDir.string()).ok(), "un espace Util dans le projet : relier Util refuse");
    }

    // La serialisation : le lien et le cache, pas le code.
    const auto files = serializeProject(p);
    const std::string idx = indexText(files);
    check(idx.find("(format 33)") != std::string::npos && idx.find("bibliotheque nom=\"Util\"") != std::string::npos
              && idx.find("bib_fonction nom=\"Moyenne\"") != std::string::npos && idx.find("bib_type nom=\"T_Point\"") != std::string::npos,
          "le lien et le cache de l'interface sont ecrits (format 33)");
    check(idx.find("Moyenne := (a + b)") == std::string::npos && files.size() < 6, "le code de la bibliotheque n'est pas ecrit");
    {
        auto back = parseProject(readerOf(files));
        check(back && back->libraries.size() == 1 && back->libraries.front().version == "1.2.0", "relu : le lien");
        const LoadedLibrary* c = back ? back->loadedLibrary("Util") : nullptr;
        check(c && c->fromCache && back->functionByName("Util.Moyenne") && back->hmiTypeByName("Util.T_Point")
                  && back->hmiTypeByName("Util.T_Point")->members.size() == 2,
              "sans disque : l'interface vient du cache (verifiable)");
        const HmiFunction* stub = back ? back->functionByName("Util.Moyenne") : nullptr;
        check(stub && stub->decls.size() == 2 && stub->returnType == "REAL", "le cache garde la signature (2 parametres, REAL)");
    }
    // Enregistrer puis ouvrir : la bibliotheque est cherchee et lue.
    check(static_cast<bool>(save(p, projectDir.string())), "enregistrer le projet");
    {
        auto opened = load(projectDir.string());
        const LoadedLibrary* l = opened ? opened->loadedLibrary("Util") : nullptr;
        check(l && l->state == LibraryState::Linked && !l->fromCache, "ouvrir : la bibliotheque est relue (reliee)");
    }
    // Plus recente compatible : 1.4.0 a la place.
    fs::remove(file);
    const std::string newer = publish(projectDir / "libs", "1.4.0", true);
    {
        Project q = p;
        q.libraries.front().path = "libs/Util-1.4.0.xpgbib";
        biblio::resolve(q, projectDir.string());
        const LoadedLibrary* l = q.loadedLibrary("Util");
        check(l && l->state == LibraryState::Newer && l->message.find("1.2.0") != std::string::npos && l->message.find("1.4.0") != std::string::npos,
              "1.4.0 trouvee pour 1.2.0 : reliee, plus recente (la note dit 1.2.0 et 1.4.0)");
        check(hasIssue(generOf(q), "compatible", Issue::Severity::Warning), "Generer le dit (un avertissement)");
        // Mettre a jour : le diff (Ecart ajoutee), rien ne casse.
        const auto up = biblio::planUpdate(q, "Util", projectDir.string());
        bool added = false;
        for (const auto& c : up.changes) added = added || (c.kind == biblio::InterfaceChange::Kind::Added && c.name == "Util.Ecart");
        check(up.refusal.empty() && up.from == "1.2.0" && up.to == "1.4.0" && added && up.breakCount() == 0,
              "mettre a jour : Util.Ecart ajoutee, aucune rupture");
        check(biblio::applyUpdate(q, up, projectDir.string()) && q.libraries.front().version == "1.4.0" && q.libraries.front().compat == "1.2.0"
                  && q.loadedLibrary("Util")->state == LibraryState::Linked,
              "appliquer : la version figee a 1.4.0, le plancher reste 1.2.0");
    }
    // Plus ancienne : 1.1.0 pour un plancher 1.2.0.
    {
        const std::string older = publish(projectDir / "libs", "1.1.0");
        Project q = p;
        q.libraries.front().path = "libs/Util-1.1.0.xpgbib";
        biblio::resolve(q, projectDir.string());
        check(q.loadedLibrary("Util")->state == LibraryState::Older && hasIssue(generOf(q), "plus ancienne", Issue::Severity::Warning),
              "1.1.0 pour >= 1.2.0 : averti (plus ancienne)");
    }
    // Incompatible : 2.0.0 (sans Borne, que le projet emploie) - refusee, le cache garde l'interface ; la mise a jour
    // montre la rupture a son endroit.
    {
        const std::string major = publish(projectDir / "libs", "2.0.0", false, false);
        Project q = p;
        addScript(q, "Emploi", "R := Util.Borne(5.0, 0.0, 1.0);");
        q.libraries.front().path = "libs/Util-2.0.0.xpgbib";
        biblio::resolve(q, projectDir.string());
        const LoadedLibrary* l = q.loadedLibrary("Util");
        check(l && l->state == LibraryState::Incompatible && l->fromCache && q.functionByName("Util.Borne"),
              "2.0.0 pour 1.x : refusee, le cache garde l'interface (Util.Borne reste connue)");
        check(hasIssue(generOf(q), "incompatible"), "Generer : incompatible (une erreur)");
        const auto up = biblio::planUpdate(q, "Util", projectDir.string());
        bool broken = false;
        for (const auto& c : up.changes)
            broken = broken || (c.kind == biblio::InterfaceChange::Kind::Removed && c.name == "Util.Borne" && !c.usedAt.empty()
                                && c.usedAt.front().find("Emploi") != std::string::npos);
        check(up.refusal.empty() && up.breakCount() >= 1 && broken, "mettre a jour vers 2.0.0 : Util.Borne retiree, employee par le script Emploi");
        std::string e2;
        const std::string r = run(q, "R := Util.Moyenne(1.0, 3.0);", &e2);
        check(r != "2" && e2.find("n'a pas de code") != std::string::npos, "une fonction du seul cache ne tourne pas, et le dit [" + e2 + "]");
    }
    // Absente.
    {
        Project q = p;
        q.libraries.front().path = "libs/Absente.xpgbib";
        biblio::resolve(q, projectDir.string());
        check(q.loadedLibrary("Util")->state == LibraryState::Missing && hasIssue(generOf(q), "introuvable"), "absente : une erreur Generer (introuvable)");
        std::string why;
        check(biblio::relocate(q, "Util", newer, projectDir.string(), &why) && q.loadedLibrary("Util")->state != LibraryState::Missing,
              "Localiser... : un autre chemin, relue (" + why + ")");
    }
    // Delier.
    {
        Project q = p;
        check(biblio::unlink(q, "Util") && q.libraries.empty() && !q.functionByName("Util.Moyenne"), "delier : le lien et son contenu partent");
        check(indexText(serializeProject(q)).find("(format 33)") == std::string::npos, "sans lien ni espace : le format redescend");
    }
    // Un .xpgtypes (un paquet) ne se relie pas.
    {
        const fs::path fake = root / "faux.xpgbib";
        { std::FILE* f = std::fopen(fake.string().c_str(), "wb"); if (f) { std::fputs("pas un zip", f); std::fclose(f); } }
        check(!biblio::planLink(p, fake.string(), projectDir.string()).ok(), "un fichier qui n'est pas une bibliotheque : refuse");
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

// =============================================================================
//  7) LA SECONDE RELECTURE (lot D2) : deux types de meme nom court dans deux espaces
// =============================================================================
namespace {
std::string leavesOf(const types::Flat& f) {
    std::string out;
    for (const auto& l : f.leaves) out += (out.empty() ? "" : ",") + l.path;
    return out;
}
} // namespace

void homonymes() {
    std::printf("-- seconde relecture : homonymes d'espaces (depliage, heritage)\n");
    std::string err;
    // T2-45 : une variable Gaz.T_Vanne prend les membres de Gaz.T_Vanne, pas ceux du T_Vanne de la racine.
    {
        Project p;
        addType(p, "T_Vanne").members.push_back(member("Etat", "BOOL"));
        HmiType& g = addType(p, "T_Vanne", "Gaz");
        g.members.push_back(member("Position", "REAL"));
        g.members.push_back(member("Defaut", "BOOL"));
        g.members.push_back(member("Commande", "INT"));
        const auto fl = types::flatten(p, "V1", "Gaz.T_Vanne");
        check(fl.ok() && leavesOf(fl) == "V1.Position,V1.Defaut,V1.Commande",
              "T2-45 : V1 : Gaz.T_Vanne a les 3 cases de Gaz (pas V1.Etat de la racine) [" + leavesOf(fl) + " " + fl.error + "]");
        check(!fl.aggregates.empty() && fl.aggregates.back().typeName == "Gaz.T_Vanne", "T2-45 : l'agregat porte le nom complet");
        addVar(p, "V1", "Gaz.T_Vanne");
        addVar(p, "R", "REAL");
        const std::string r = run(p, "V1.Position := 50.0;\nR := V1.Position;", &err);
        check(r == "50", "T2-45 : V1.Position := 50.0 tourne en marche [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }
    // T2-45 : Gaz.T_Vanne et Util.T_Vanne sans type racine ; un membre Util.T_Vanne dans Gaz.T_Vanne n'est pas un cycle.
    {
        Project p;
        addType(p, "T_Vanne", "Util").members.push_back(member("X", "INT"));
        HmiType& g = addType(p, "T_Vanne", "Gaz");
        g.members.push_back(member("Position", "REAL"));
        g.members.push_back(member("Secours", "Util.T_Vanne"));
        const auto fl = types::flatten(p, "V1", "Gaz.T_Vanne");
        check(fl.ok() && leavesOf(fl) == "V1.Position,V1.Secours.X",
              "T2-45 : deux T_Vanne d'espaces : les cases de Gaz, Secours (Util.T_Vanne) sans faux cycle [" + leavesOf(fl) + " " + fl.error + "]");
        check(types::cycleOf(p, "Gaz.T_Vanne").empty(), "T2-45 : cycleOf ne prend pas Util.T_Vanne pour Gaz.T_Vanne");
        bool dup = false;
        for (const auto& i : generOf(p)) dup = dup || i.message.find("se contient") != std::string::npos;
        check(!dup, "T2-45 : Generer ne dit pas \"se contient lui-meme\"");
    }
    // T2-48 : Gaz.T_Vanne EXTENDS Base.T_Vanne - le parent n'est pas pris pour un cycle.
    {
        Project p;
        HmiType& b = addType(p, "T_Vanne", "Base");
        b.members.push_back(member("Position", "REAL"));
        HmiFunction ouvrir;
        ouvrir.id = p.allocate();
        ouvrir.name = "Ouvrir";
        ouvrir.body = "Position := 100.0;";
        b.methods.push_back(ouvrir);
        HmiType& g = addType(p, "T_Vanne", "Gaz");
        g.base = "Base.T_Vanne";
        g.members.push_back(member("Pression", "REAL"));
        check(types::membersOf(p, "Gaz.T_Vanne").size() == 2, "T2-48 : membersOf(Gaz.T_Vanne) = Position (herite) + Pression");
        check(baseChain(p, "Gaz.T_Vanne").size() == 1, "T2-48 : baseChain(Gaz.T_Vanne) = [Base.T_Vanne]");
        check(isA(p, "Gaz.T_Vanne", "Base.T_Vanne"), "T2-48 : isA(Gaz.T_Vanne, Base.T_Vanne)");
        check(effectiveMethod(p, "Gaz.T_Vanne", "Ouvrir") != nullptr, "T2-48 : la methode Ouvrir heritee est vue");
        for (const auto& ty : p.programs.types)
            if (ty.ns == "Gaz") check(extendsProblem(p, ty).empty(), "T2-48 : Generer ne voit pas de cycle (" + extendsProblem(p, ty) + ")");
        addVar(p, "V1", "Gaz.T_Vanne");
        addVar(p, "R", "REAL");
        const std::string r = run(p, "V1.Position := 10.0;\nV1.Ouvrir();\nR := V1.Position + V1.Pression;", &err);
        check(r == "100", "T2-48 : V1.Position et V1.Ouvrir() (herites) tournent [" + r + (err.empty() ? "" : " err=" + err) + "]");
        // un vrai cycle reste dit
        Project q;
        addType(q, "T_A", "Gaz").base = "Gaz.T_B";
        addType(q, "T_B", "Gaz").base = "Gaz.T_A";
        check(extendsProblem(q, q.programs.types.front()).find("lui-m\xC3\xAAme") != std::string::npos, "T2-48 : un vrai cycle d'heritage reste dit");
    }
    // T2-48 : Gaz.T_Vanne et Util.T_Vanne implementent I_Demarrable : chacun se juge sur ses propres methodes.
    {
        Project p;
        HmiType& i = addType(p, "I_Demarrable");
        i.kind = HmiTypeKind::Interface;
        HmiFunction sig;
        sig.id = p.allocate();
        sig.name = "Demarrer";
        i.methods.push_back(sig);
        for (const char* ns : {"Gaz", "Util"}) {
            HmiType& t = addType(p, "T_Vanne", ns);
            t.interfaces = {"I_Demarrable"};
            t.members.push_back(member("Marche", "BOOL"));
            HmiFunction m;
            m.id = p.allocate();
            m.name = "Demarrer";
            m.body = "Marche := TRUE;";
            t.methods.push_back(m);
        }
        std::string misses;
        for (const auto& t : p.programs.types)
            for (const auto& miss : interfaceMisses(p, t)) misses += " | " + miss;
        check(misses.empty(), "T2-48 : deux T_Vanne d'espaces qui implementent I_Demarrable : aucun faux manque" + misses);
    }
}

// T2-46 : un cast et un appel generique explicite dans un script d'un espace (le moteur lit le texte qualifie).
void chevrons() {
    std::printf("-- seconde relecture : DYNAMIC_CAST<T> et F<INT>() dans un espace\n");
    std::string err;
    const auto build = [] {
        Project p;
        addType(p, "T_Equip", "Gaz").members.push_back(member("N", "INT"));
        HmiType& pompe = addType(p, "T_Pompe", "Gaz");
        pompe.base = "T_Equip";
        pompe.members.push_back(member("Debit", "REAL"));
        HmiFunction& lim = addFn(p, "Limiter", "T", "IF x < lo THEN Limiter := lo; ELSIF x > hi THEN Limiter := hi; ELSE Limiter := x; END_IF", "Gaz");
        lim.generics = {GenericParam{"T", "ANY_NUM"}};
        for (const char* n : {"x", "lo", "hi"}) {
            Declaration d;
            d.id = p.allocate();
            d.kind = DeclKind::Parameter;
            d.name = n;
            d.type = "T";
            d.mode = PassMode::In;
            lim.decls.push_back(d);
        }
        addVar(p, "P1", "Gaz.T_Pompe");
        addVar(p, "R", "REAL");
        return p;
    };
    {
        Project p = build();
        const std::string r = run(p, "VAR q : REF_TO T_Pompe; END_VAR\nP1.Debit := 4.0;\nq := DYNAMIC_CAST<T_Pompe>(P1);\n"
                                     "IF q <> NULL THEN R := q^.Debit; ELSE R := -1.0; END_IF;", &err, "Gaz");
        check(r == "4", "T2-46 : DYNAMIC_CAST<T_Pompe>(P1) dans un script de Gaz tourne [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }
    {
        Project p = build();
        const std::string r = run(p, "R := Limiter<INT>(250, 0, 100);", &err, "Gaz");
        check(r == "100", "T2-46 : Limiter<INT>(...) d'un script de Gaz tourne [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }
    {
        Project p = build();
        const std::string r = run(p, "VAR q : REF_TO Gaz.T_Pompe; END_VAR\nP1.Debit := 2.5;\nq := DYNAMIC_CAST<Gaz.T_Pompe>(P1);\n"
                                     "R := Gaz.Limiter<REAL>(q^.Debit, 0.0, 10.0);", &err);
        check(r == "2.5", "T2-46 : DYNAMIC_CAST<Gaz.T_Pompe> et Gaz.Limiter<REAL>(...) ecrits en entier depuis la racine [" + r
                              + (err.empty() ? "" : " err=" + err) + "]");
        check(!hasFinding(checkOf(p, "VAR q : REF_TO Gaz.T_Pompe; END_VAR\nq := DYNAMIC_CAST<Gaz.T_Pompe>(P1);\nR := Gaz.Limiter<REAL>(1.0, 0.0, 10.0);"), ""),
              "T2-46 : Compiler accepte la syntaxe complete" + allMessages(checkOf(p, "VAR q : REF_TO Gaz.T_Pompe; END_VAR\nq := DYNAMIC_CAST<Gaz.T_Pompe>(P1);\nR := Gaz.Limiter<REAL>(1.0, 0.0, 10.0);")));
    }
    {
        // Compiler : ni « REAL n'existe pas » (le type d'un appel generique), ni une comparaison de structure (le cast).
        Project p = build();
        for (const char* c : {"R := Limiter<REAL>(1.0, 0.0, 10.0);", "VAR q : REF_TO T_Pompe; END_VAR\nq := DYNAMIC_CAST<T_Pompe>(P1);"})
            check(!hasFinding(checkOf(p, c, "Gaz"), ""), std::string("T2-46 : Compiler accepte dans Gaz : ") + c + allMessages(checkOf(p, c, "Gaz")));
        check(hasFinding(checkOf(p, "R := Limiter<REAL>(1.0, 0.0, 10.0);"), "Limiter est dans l'espace Gaz", false),
              "T2-46 : a la racine, le nom court d'un generique de Gaz reste dit");
    }
    {
        Project p = build();
        const std::string r = run(p, "USING Gaz;\nR := Limiter<INT>(-5, 0, 100);", &err);
        check(r == "0", "T2-46 : Limiter<INT>(...) sous USING Gaz tourne [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }
}

// T2-47 : les unites propres d'une bibliotheque voyagent avec elle (relier, EN, le cache).
void unitesBibliotheque() {
    std::printf("-- seconde relecture : les unites d'une bibliotheque reliee\n");
    std::string err;
    const fs::path root = tempRoot();
    const fs::path projectDir = root / "Projet";
    fs::create_directories(projectDir / "libs");
    Project src;
    src.config.name = "Source";
    UnitDef nm3j;
    nm3j.symbol = "Nm3/j";
    nm3j.family = "debit_normalise";
    nm3j.factor = 1.0 / 24.0;
    src.config.units.push_back(nm3j);
    TypeMember vol = member("Volume_jour", "REAL");
    vol.unit = "Nm3/j";
    addType(src, "T_Compteur", "Gaz").members.push_back(vol);
    biblio::PublishSpec spec;
    spec.name = "Gaz";
    spec.version = "1.0.0";
    const auto pub = biblio::collect(src, spec);
    const std::string file = (projectDir / "libs" / "Gaz-1.0.0.xpgbib").string();
    check(pub.refusal.empty() && static_cast<bool>(biblio::writeFile(pub.package, file)), "T2-47 : Gaz publiee avec son unite Nm3/j");

    Project p;
    p.config.name = "Projet";
    addVar(p, "R", "REAL");
    const auto plan = biblio::planLink(p, file, projectDir.string());
    check(plan.ok() && biblio::link(p, plan, projectDir.string()), "T2-47 : Gaz reliee (" + plan.refusal + ")");
    check(!p.libraries.empty() && p.libraries.front().units.size() == 1, "T2-47 : le lien garde l'unite de la bibliotheque (cache)");
    check(units::Catalog(biblio::unitConfig(p)).known("Nm3/j") && !units::Catalog(p.config).known("Nm3/j"),
          "T2-47 : le catalogue du projet voit Nm3/j par la bibliotheque (pas dans sa propre config)");
    addVar(p, "Compteur", "Gaz.T_Compteur");
    const std::string r = run(p, "Compteur.Volume_jour := 240.0;\nR := EN(Compteur.Volume_jour, 'Nm3/h');", &err);
    check(r == "10", "T2-47 : EN(Compteur.Volume_jour, 'Nm3/h') convertit (240 Nm3/j -> 10 Nm3/h) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    // Relu sans le fichier : le cache garde l'unite (bib_unite).
    const auto files = serializeProject(p);
    check(indexText(files).find("bib_unite") != std::string::npos, "T2-47 : bib_unite ecrit dans le cache du lien");
    auto back = parseProject(readerOf(files));
    check(back && !back->libraries.empty() && back->libraries.front().units.size() == 1
              && units::Catalog(biblio::unitConfig(*back)).known("Nm3/j"),
          "T2-47 : relu sans le fichier, le cache donne encore Nm3/j");
    // Le projet garde la main : son unite du meme symbole passe avant celle de la bibliotheque.
    Project q = p;
    UnitDef mine = nm3j;
    mine.factor = 1.0;
    q.config.units.push_back(mine);
    const auto qc = biblio::unitConfig(q);
    check(qc.units.size() == 1 && qc.units.front().factor == 1.0, "T2-47 : une unite du projet de meme symbole l'emporte");
    std::error_code ec;
    fs::remove_all(root, ec);
}

// T2-51 : changer l'espace d'un type, renommer un espace - parametres de popup, heritage, corps des methodes suivent.
// T2-52 : renommer une fonction ou un script d'un espace - par identite.
void renommerEspaces() {
    std::printf("-- seconde relecture : changer d'espace, renommer un espace, une fonction, un script\n");
    std::string err;
    {
        Project p = buildProject();                                   // Gaz.T_Vanne, Gaz.EnDefaut
        View pop = makeView(p, "Popup_Vanne");
        ViewParam prm;
        prm.name = "Vanne";
        prm.type = "Gaz.T_Vanne";
        pop.params.push_back(prm);
        p.views.push_back(pop);
        HmiType& rob = addType(p, "T_Robinet");
        rob.base = "Gaz.T_Vanne";
        HmiFunction m;
        m.id = p.allocate();
        m.name = "Verifier";
        m.returnType = "BOOL";
        m.body = "Verifier := Gaz.EnDefaut(SELF);";
        rob.methods.push_back(m);
        Id vanne = kNoId;
        for (const auto& t : p.programs.types) if (t.name == "T_Vanne") vanne = t.id;
        std::string why;
        check(names::setItemNamespace(p, names::ItemKind::Type, vanne, "Gaz.Vannes", &why), "T2-51 : Gaz.T_Vanne -> Gaz.Vannes (" + why + ")");
        check(p.views.back().params.front().type == "Gaz.Vannes.T_Vanne", "T2-51 : le parametre de popup suit (" + p.views.back().params.front().type + ")");
        const HmiType* r = p.hmiTypeByName("T_Robinet");
        check(r && r->base == "Gaz.Vannes.T_Vanne" && types::membersOf(p, "T_Robinet").size() == 2, "T2-51 : le sous-type garde son parent (herite=Gaz.Vannes.T_Vanne)");
        check(names::renameNamespace(p, "Gaz", "GazNat") > 0, "T2-51 : renommer l'espace Gaz en GazNat");
        r = p.hmiTypeByName("T_Robinet");
        check(r && r->methods.front().body == "Verifier := GazNat.EnDefaut(SELF);", "T2-51 : le corps de la methode suit (GazNat.EnDefaut) ["
                                                                                  + (r ? r->methods.front().body : std::string("?")) + "]");
    }
    {
        // T2-52 : Util.Moyenne et Gaz.Moyenne ; un script de la racine appelle Util.Moyenne, un script de Gaz sa voisine.
        Project p = buildProject();
        addFn(p, "Moyenne", "REAL", "VAR_INPUT a : REAL; b : REAL; END_VAR\nMoyenne := a;", "Gaz");
        addScript(p, "S_Racine", "R := Util.Moyenne(1.0, 3.0);");
        addScript(p, "S_Gaz", "R := Moyenne(1.0, 3.0);", "Gaz");
        addScript(p, "S_Using", "USING Util;\nR := Moyenne(2.0, 4.0);");
        (void)renameFunctionEverywhere(p, "Util", "Moyenne", "MoyenneGlissante");
        for (auto& f : p.programs.functions) if (f.name == "Moyenne" && f.ns == "Util") f.name = "MoyenneGlissante";
        const Script* a = p.generalScript("S_Racine");
        const Script* b = p.generalScript("Gaz.S_Gaz");
        const Script* c = p.generalScript("S_Using");
        check(a && a->body == "R := Util.MoyenneGlissante(1.0, 3.0);", "T2-52 : Util.Moyenne(a, b) suit [" + (a ? a->body : std::string("?")) + "]");
        check(b && b->body == "R := Moyenne(1.0, 3.0);", "T2-52 : Moyenne(c, d) d'un script de Gaz (vise Gaz.Moyenne) ne change pas [" + (b ? b->body : std::string("?")) + "]");
        check(c && c->body == "USING Util;\nR := MoyenneGlissante(2.0, 4.0);", "T2-52 : sous USING Util, le nom court suit [" + (c ? c->body : std::string("?")) + "]");
        check(run(p, "R := Util.MoyenneGlissante(1.0, 3.0);", &err) == "2", "T2-52 : l'appel renomme tourne (" + err + ")");
    }
    {
        // T2-52 : renommer une fonction de la racine n'atteint pas l'homonyme d'un espace.
        Project p;
        addVar(p, "R", "REAL");
        addFn(p, "Calcul", "REAL", "Calcul := 1.0;");
        addFn(p, "Calcul", "REAL", "Calcul := 2.0;", "Gaz");
        addScript(p, "S_Gaz", "R := Calcul();", "Gaz");
        addScript(p, "S_Racine", "R := Calcul();");
        (void)renameFunctionEverywhere(p, "", "Calcul", "Calcul2");
        check(p.generalScript("Gaz.S_Gaz")->body == "R := Calcul();" && p.generalScript("S_Racine")->body == "R := Calcul2();",
              "T2-52 : renommer Calcul (racine) : son appel suit, celui de Gaz (Gaz.Calcul) non");
    }
    {
        // T2-52 : une action Script general vers un nom court que deux espaces definissent : Generer dit lequel.
        Project p;
        addVar(p, "R", "REAL");
        addScript(p, "Horloge", "R := 1.0;", "Gaz");
        addScript(p, "Horloge", "R := 2.0;", "Util");
        View v = makeView(p, "Vue_A");
        Action a;
        a.operation = Operation::CallScript;
        a.target = "Horloge";
        v.actions.push_back(a);
        p.views.push_back(v);
        check(hasIssue(generOf(p), "ambigu : Gaz.Horloge ou Util.Horloge"), "T2-52 : Script general 'Horloge' ambigu : Generer nomme les deux");
        p.views.back().actions.front().target = "Gaz.Horloge";
        check(!hasIssue(generOf(p), "Horloge"), "T2-52 : la cible par son nom complet (Gaz.Horloge) est trouvee");
    }
}

int main() {
    std::printf("1.13.0 (lot J8) : les espaces de noms et les bibliotheques\n");
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    modele();
    resolveur();
    moteur();
    controle();
    serialisation();
    bibliotheques();
    homonymes();
    chevrons();
    unitesBibliotheque();
    renommerEspaces();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
