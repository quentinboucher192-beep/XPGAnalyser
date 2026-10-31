// =============================================================================
//  tests/hmi_relations_test.cpp - 1.19.0 : le graphe des relations, la
//  verification globale, l'analyse et l'optimisation des adresses
// -----------------------------------------------------------------------------
//  Un vrai petit projet de station (deux pompes, une cuve, un equipement
//  Modbus, des alarmes, un script, une fonction, un symbole et ses instances,
//  une popup, une image, des dossiers), avec des elements orphelins et des
//  fautes volontaires : une alarme sur une variable inexistante, deux variables
//  qui se chevauchent, des dossiers d'une liste qui n'existe pas, un joker de
//  dossier qui vise un dossier absent. Chaque question de l'Assistant y est
//  posee et sa reponse comparee aux DONNEES REELLES ; puis run(), les adresses,
//  optimize() ; puis le temps sur un projet genere (60 equipements, 600
//  variables, 30 vues).
// =============================================================================
#include "../src/hmi/HmiAudit.hpp"
#include "../src/hmi/HmiRelations.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

// ---- la fabrique du projet ------------------------------------------------------
TypeMember member(const char* name, const char* type) {
    TypeMember m;
    m.name = name;
    m.type = type;
    return m;
}
HmiType makeType(Project& p, const char* name, std::vector<TypeMember> members, const char* folder = "") {
    HmiType t;
    t.id = p.allocate();
    t.name = name;
    t.members = std::move(members);
    t.folder = folder;
    return t;
}
Variable makeVar(Project& p, const char* name, const char* type, const char* equipment = "", const char* address = "", const char* folder = "") {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = std::string(type) == "STRING" ? "''" : std::string(type) == "REAL" ? "0.0" : std::string(type) == "INT" ? "0" : "";
    v.equipment = equipment;
    v.address = address;
    v.folder = folder;
    return v;
}
Declaration makeDecl(Project& p, DeclKind kind, const char* name, const char* type, const char* value = "") {
    Declaration d;
    d.id = p.allocate();
    d.kind = kind;
    d.name = name;
    d.type = type;
    d.value = value;
    return d;
}
AlarmDef makeAlarm(Project& p, const char* name, const char* condition, const char* message) {
    AlarmDef a;
    a.id = p.allocate();
    a.name = name;
    a.condition = condition;
    a.message = message;
    return a;
}
Action click(Operation op, const char* target, const char* value = "") {
    Action a;
    a.trigger = Trigger::Click;
    a.operation = op;
    a.target = target;
    a.value = value;
    return a;
}
Object& place(Project& p, View& v, hmi::Kind kind, const char* name, double x = 10, double y = 10) {
    v.objects.push_back(makeObject(kind, p.allocate(), name, x, y, v.layers.front().id));
    return v.objects.back();
}

Project station() {
    Project p;
    // Les types : T_Pompe (Marche, Defaut : un mot ; Debit : 2 ; Vitesse : 1 -> 4 mots), T_Cuve (Niveau, Temperature : 4 ;
    // Vanne : 1 -> 5 mots), et un type que rien n'emploie.
    p.programs.types.push_back(makeType(p, "T_Pompe", {member("Marche", "BOOL"), member("Defaut", "BOOL"), member("Debit", "REAL"), member("Vitesse", "INT")}, "Process"));
    p.programs.types.push_back(makeType(p, "T_Cuve", {member("Niveau", "REAL"), member("Temperature", "REAL"), member("Vanne", "BOOL")}, "Process"));
    p.programs.types.push_back(makeType(p, "T_Inutile", {member("X", "INT")}));
    // L'equipement Modbus (simule : pas de reseau) et un equipement sans variable.
    Equipment eq;
    eq.id = p.allocate();
    eq.name = "Station_Modbus";
    eq.simulated = true;
    p.equipments.push_back(eq);
    Equipment vide;
    vide.id = p.allocate();
    vide.name = "Equip_Vide";
    vide.simulated = true;
    p.equipments.push_back(vide);
    // Les variables : Pompe_1 0-3, Pompe_2 4-7, Cuve_1 10-14, Niveau_Cuve_1 20-21 ; Consigne_Debit 12-13 CHEVAUCHE Cuve_1.
    p.programs.variables.push_back(makeVar(p, "Pompe_1", "T_Pompe", "Station_Modbus", "40001", "Pompes"));
    p.programs.variables.push_back(makeVar(p, "Pompe_2", "T_Pompe", "Station_Modbus", "40005", "Pompes"));
    p.programs.variables.push_back(makeVar(p, "Cuve_1", "T_Cuve", "Station_Modbus", "40011"));
    p.programs.variables.push_back(makeVar(p, "Niveau_Cuve_1", "REAL", "Station_Modbus", "40021"));
    p.programs.variables.push_back(makeVar(p, "Consigne_Debit", "REAL", "Station_Modbus", "40013"));
    p.programs.variables.push_back(makeVar(p, "Texte_Etat", "STRING"));
    p.programs.variables.push_back(makeVar(p, "Compteur_Inutile", "INT"));
    // La fonction appelee, et une que personne n'appelle.
    HmiFunction moy;
    moy.id = p.allocate();
    moy.name = "Moyenne";
    moy.returnType = "REAL";
    moy.body = "Moyenne := (a + b) / 2.0;";
    moy.decls = {makeDecl(p, DeclKind::Parameter, "a", "REAL"), makeDecl(p, DeclKind::Parameter, "b", "REAL")};
    p.programs.functions.push_back(moy);
    HmiFunction inutile;
    inutile.id = p.allocate();
    inutile.name = "Fonction_Inutile";
    inutile.returnType = "INT";
    inutile.body = "Fonction_Inutile := 1;";
    p.programs.functions.push_back(inutile);
    // Le script qui lit Niveau_Cuve_1 et appelle Moyenne. Pompe_1 n'y est que dans un commentaire et une chaine ;
    // Compteur_Inutile y est une variable locale (elle cache la variable IHM du meme nom).
    Script calc;
    calc.id = p.allocate();
    calc.name = "Calcul_Niveau";
    calc.event = "Cyclique";
    calc.body = "Cuve_1.Niveau := Niveau_Cuve_1 * 0.5;\n"
                "Cuve_1.Temperature := Moyenne(Niveau_Cuve_1, 20.0);\n"
                "(* Pompe_1 n'est ici que dans un commentaire *)\n"
                "Texte_Etat := 'Pompe_1 arretee';\n"
                "Compteur_Inutile := Compteur_Inutile + 1;\n";
    calc.decls = {makeDecl(p, DeclKind::Variable, "Compteur_Inutile", "INT", "0")};
    p.programs.scripts.push_back(calc);
    // Un script sur appel qui ouvre la popup par son nom (une chaine qui NOMME une vue ouverte).
    Script ouvre;
    ouvre.id = p.allocate();
    ouvre.name = "Ouvre_Popup";
    ouvre.event = "Appel";
    ouvre.body = "IHM_POPUP('Pop_Pompe');\n";
    p.programs.scripts.push_back(ouvre);
    // Les alarmes : deux vraies, et une sur une variable qui n'existe pas.
    p.alarms.push_back(makeAlarm(p, "Defaut_Pompe_2", "Pompe_2.Defaut", "Pompe 2 en d\xC3\xA9" "faut"));
    p.alarms.push_back(makeAlarm(p, "Niveau_Haut", "Niveau_Cuve_1 > 90.0", "Niveau haut : {Niveau_Cuve_1:0.0} %"));
    p.alarms.push_back(makeAlarm(p, "Alarme_Fantome", "Capteur_Absent > 5.0", "Capteur absent"));
    // Les ressources : une image employee, une autre inutilisee.
    const auto svg = std::make_shared<const Bytes>(Bytes{'<', 's', 'v', 'g', '/', '>'});
    for (const char* name : {"logo.svg", "vieux.svg"}) {
        Resource r;
        r.id = p.allocate();
        r.name = name;
        r.format = "SVG";
        r.data = svg;
        r.bytes = svg->size();
        p.assets.resources.push_back(r);
    }
    // Le symbole d'une pompe (un parametre Pompe : T_Pompe) et un symbole jamais pose.
    View sym = makeView(p, "Sym_Pompe");
    sym.role = "symbole";
    sym.width = 120;
    sym.height = 80;
    ViewParam pp;
    pp.name = "Pompe";
    pp.defaultValue = "Pompe_1";
    pp.type = "T_Pompe";
    sym.params.push_back(pp);
    place(p, sym, hmi::Kind::Indicator, "Voyant").setExpr("value", "Pompe.Marche");
    p.views.push_back(sym);
    View symInutile = makeView(p, "Sym_Inutile");
    symInutile.role = "symbole";
    symInutile.width = 200;
    symInutile.height = 200;
    place(p, symInutile, hmi::Kind::Rectangle, "Cadre");
    p.views.push_back(symInutile);
    // La popup d'une pompe (son parametre Equipement : T_Pompe), et une popup que rien n'ouvre.
    View pop = makeView(p, "Pop_Pompe");
    pop.role = "popup";
    pop.width = 400;
    pop.height = 300;
    ViewParam ep;
    ep.name = "Equipement";
    ep.defaultValue = "Pompe_1";
    ep.type = "T_Pompe";
    pop.params.push_back(ep);
    place(p, pop, hmi::Kind::Text, "Txt_Debit").set("text", "D\xC3\xA9" "bit : {Equipement.Debit:0.0}");
    p.views.push_back(pop);
    View popInutile = makeView(p, "Pop_Inutile");
    popInutile.role = "popup";
    popInutile.width = 300;
    popInutile.height = 200;
    place(p, popInutile, hmi::Kind::Text, "Txt").set("text", "Rien");
    p.views.push_back(popInutile);
    // La vue de la station.
    View st = makeView(p, "Vue_Station");
    st.folder = "Station";
    Object& i1 = place(p, st, hmi::Kind::SymbolInstance, "Inst_P1", 10, 10);
    i1.set("symbol", "Sym_Pompe");
    i1.set("params", "Pompe := Pompe_1");
    Object& i2 = place(p, st, hmi::Kind::SymbolInstance, "Inst_P2", 200, 10);
    i2.set("symbol", "Sym_Pompe");
    i2.set("params", "Pompe := Pompe_2");
    place(p, st, hmi::Kind::Text, "Txt_Niveau", 10, 200).set("text", "Niveau : {Niveau_Cuve_1:0.0} %");
    place(p, st, hmi::Kind::Bargraph, "Bar_Cuve", 10, 300).setExpr("value", "Cuve_1.Niveau");
    place(p, st, hmi::Kind::Button, "Btn_P2", 300, 300).actions.push_back(click(Operation::Popup, "Pop_Pompe", "Equipement := Pompe_2"));
    place(p, st, hmi::Kind::Button, "Btn_Script", 400, 300).actions.push_back(click(Operation::CallScript, "Ouvre_Popup"));
    place(p, st, hmi::Kind::Image, "Img_Logo", 600, 10).set("image", "logo.svg");
    place(p, st, hmi::Kind::Slider, "Sld_Consigne", 600, 300).set("variable", "Consigne_Debit");
    place(p, st, hmi::Kind::VariableTable, "Tab_Archives", 600, 500).set("variables", "Archives/*");
    // Une pompe du synoptique : son Defaut relie a Pompe_2.Defaut - l'alarme de la bibliotheque (Defaut) en nait.
    place(p, st, hmi::Kind::Pump, "Pompe_Synop", 900, 10).set("fault", "Pompe_2.Defaut");
    p.views.push_back(st);
    p.config.startView = st.id;
    // Les dossiers : des dossiers gardes pour une liste qui n'existe pas (les alarmes ne se rangent pas).
    p.listFolders["alarmes"] = {"Anciennes"};
    return p;
}

std::set<std::string> names(const rel::Graph& g, const std::vector<std::size_t>& nodes) {
    std::set<std::string> out;
    for (const auto n : nodes) out.insert(g.node(n).name);
    return out;
}
std::string listed(const std::set<std::string>& s) {
    std::string out;
    for (const auto& x : s) out += (out.empty() ? "" : ", ") + x;
    return "{" + out + "}";
}
bool hasEdge(const rel::Graph& g, std::size_t from, std::size_t to, rel::Link link, std::string_view path = {}) {
    for (const auto e : g.out(from)) {
        const auto& edge = g.edges()[e];
        if (edge.to == to && edge.link == link && (path.empty() || edge.path == path)) return true;
    }
    return false;
}

// ---- 1. le graphe et les questions ------------------------------------------------
void testGraph(const Project& p, bool verbose) {
    const auto g = rel::build(p);
    const auto node = [&](rel::Kind k, const char* name) {
        const auto n = g.find(k, name);
        check(n.has_value(), std::string("noeud ") + name);
        return n.value_or(0);
    };
    if (verbose) {
        std::printf("  graphe : %zu noeuds, %zu aretes\n", g.size(), g.edges().size());
        for (const auto& e : g.edges()) std::printf("    %s\n", rel::edgeText(g, e).c_str());
    }
    const auto pompe1 = node(rel::Kind::Variable, "Pompe_1"), pompe2 = node(rel::Kind::Variable, "pompe_2");   // sans casse
    const auto cuve = node(rel::Kind::Variable, "Cuve_1"), niveau = node(rel::Kind::Variable, "Niveau_Cuve_1");
    const auto consigne = node(rel::Kind::Variable, "Consigne_Debit"), compteur = node(rel::Kind::Variable, "Compteur_Inutile");
    const auto tPompe = node(rel::Kind::Type, "T_Pompe"), equip = node(rel::Kind::Equipment, "Station_Modbus");
    const auto calc = node(rel::Kind::Script, "Calcul_Niveau"), ouvre = node(rel::Kind::Script, "Ouvre_Popup");
    const auto moy = node(rel::Kind::Function, "Moyenne"), pop = node(rel::Kind::Popup, "Pop_Pompe");
    const auto vue = node(rel::Kind::View, "Vue_Station"), sym = node(rel::Kind::Symbol, "Sym_Pompe");
    const auto btn = node(rel::Kind::Object, "Vue_Station.Btn_P2"), sld = node(rel::Kind::Object, "Sld_Consigne");
    const auto inst1 = node(rel::Kind::Object, "Inst_P1"), logo = node(rel::Kind::Resource, "logo.svg");
    check(g.find(rel::Kind::View, "Pop_Pompe") == pop, "find(View) trouve aussi une popup");
    check(g.byId(p.programs.variables.front().id) == pompe1, "byId : la variable Pompe_1");

    // Les aretes des champs.
    check(hasEdge(g, pompe1, tPompe, rel::Link::TypeOf), "Pompe_1 est du type T_Pompe");
    check(hasEdge(g, pompe1, equip, rel::Link::Address, "40001"), "Pompe_1 est liee a Station_Modbus (40001)");
    check(hasEdge(g, inst1, sym, rel::Link::Instance), "Inst_P1 instancie Sym_Pompe");
    check(hasEdge(g, btn, pop, rel::Link::Opens), "Btn_P2 ouvre Pop_Pompe (son action)");
    check(hasEdge(g, ouvre, pop, rel::Link::Opens), "Ouvre_Popup ouvre Pop_Pompe (IHM_POPUP('Pop_Pompe'))");
    check(hasEdge(g, pop, tPompe, rel::Link::TypeOf), "Pop_Pompe : son parametre Equipement est un T_Pompe");
    check(hasEdge(g, node(rel::Kind::Object, "Img_Logo"), logo, rel::Link::Resource), "Img_Logo cite logo.svg");
    check(hasEdge(g, vue, btn, rel::Link::Contains), "Vue_Station contient Btn_P2");
    check(hasEdge(g, node(rel::Kind::Folder, "Station"), vue, rel::Link::Contains), "le dossier Station range Vue_Station");
    check(hasEdge(g, node(rel::Kind::Folder, "Pompes"), pompe2, rel::Link::Contains), "le dossier de variables Pompes range Pompe_2");
    // Les aretes des textes.
    check(hasEdge(g, calc, niveau, rel::Link::Reads, "Niveau_Cuve_1"), "Calcul_Niveau lit Niveau_Cuve_1");
    check(hasEdge(g, calc, cuve, rel::Link::Writes, "Cuve_1.Niveau"), "Calcul_Niveau ecrit Cuve_1.Niveau");
    check(hasEdge(g, calc, moy, rel::Link::Calls), "Calcul_Niveau appelle Moyenne");
    check(hasEdge(g, sld, consigne, rel::Link::Writes), "le curseur ecrit Consigne_Debit");
    check(!hasEdge(g, calc, pompe1, rel::Link::Reads), "Pompe_1 dans un commentaire et une chaine : pas une lecture");
    check(!hasEdge(g, calc, compteur, rel::Link::Writes) && !hasEdge(g, calc, compteur, rel::Link::Reads),
          "Compteur_Inutile, local au script, cache la variable IHM");
    check(hasEdge(g, node(rel::Kind::Alarm, "Defaut_Pompe_2"), pompe2, rel::Link::Watches, "Pompe_2.Defaut"),
          "Defaut_Pompe_2 surveille Pompe_2.Defaut");
    check(hasEdge(g, ouvre, pop, rel::Link::Opens) && users(g, ouvre).size() == 1, "Ouvre_Popup : appele par Btn_Script");
    // Le parametre de la popup (Equipement) n'est pas une variable : Txt_Debit ne lit rien.
    const auto txtDebit = node(rel::Kind::Object, "Pop_Pompe.Txt_Debit");
    bool reads = false;
    for (const auto e : g.out(txtDebit)) reads = reads || g.edges()[e].link == rel::Link::Reads;
    check(!reads, "Txt_Debit lit le parametre Equipement, pas une variable");

    // << Quels objets utilisent cette variable ? >>
    check(names(g, rel::objectsUsing(g, "Niveau_Cuve_1")) == std::set<std::string>{"Txt_Niveau"},
          "objets de Niveau_Cuve_1 : " + listed(names(g, rel::objectsUsing(g, "Niveau_Cuve_1"))));
    check(names(g, rel::objectsUsing(g, "Pompe_2")) == std::set<std::string>{"Inst_P2", "Btn_P2", "Pompe_Synop"},
          "objets de Pompe_2 : " + listed(names(g, rel::objectsUsing(g, "Pompe_2"))));
    check(names(g, rel::objectsUsing(g, "Cuve_1.Niveau")) == std::set<std::string>{"Bar_Cuve"}, "objets de Cuve_1.Niveau : Bar_Cuve");
    check(rel::objectsUsing(g, "Cuve_1.Vanne").empty(), "Cuve_1.Vanne : aucun objet");
    check(names(g, rel::users(g, moy)) == std::set<std::string>{"Calcul_Niveau"}, "qui appelle Moyenne : Calcul_Niveau");
    check(names(g, rel::users(g, consigne)) == std::set<std::string>{"Sld_Consigne"}, "qui utilise Consigne_Debit : Sld_Consigne");

    // << Montre-moi les scripts qui utilisent Niveau_Cuve_1 >>
    check(names(g, rel::scriptsUsing(g, "Niveau_Cuve_1")) == std::set<std::string>{"Calcul_Niveau"}, "scripts de Niveau_Cuve_1");
    check(names(g, rel::scriptsUsing(g, "cuve_1.niveau")) == std::set<std::string>{"Calcul_Niveau"}, "scripts de Cuve_1.Niveau (sans casse)");
    check(rel::scriptsUsing(g, "Cuve_1.Vanne").empty(), "Cuve_1.Vanne : aucun script");
    check(rel::scriptsUsing(g, "Pompe_1").empty(), "Pompe_1 : aucun script (commentaire, chaine)");

    // << Si je supprime ce type, qu'est-ce qui sera affecte ? >>
    const auto hitT = names(g, rel::impact(g, tPompe));
    for (const char* n : {"Pompe_1", "Pompe_2", "Pop_Pompe", "Sym_Pompe", "Inst_P1", "Inst_P2", "Btn_P2", "Defaut_Pompe_2", "Vue_Station"})
        check(hitT.count(n) == 1, std::string("supprimer T_Pompe touche ") + n + " : " + listed(hitT));
    for (const char* n : {"Cuve_1", "Niveau_Cuve_1", "Txt_Niveau", "Station_Modbus", "Niveau_Haut", "T_Cuve"})
        check(hitT.count(n) == 0, std::string("supprimer T_Pompe ne touche pas ") + n);
    // Supprimer une variable : ce qui la lit, l'ecrit ou la surveille ; la vue de ses objets.
    const auto hitV = names(g, rel::impact(g, niveau));
    check(hitV == std::set<std::string>{"Txt_Niveau", "Calcul_Niveau", "Niveau_Haut", "Vue_Station"}, "supprimer Niveau_Cuve_1 touche : " + listed(hitV));
    // Supprimer l'equipement : ses variables (deliees), et ce qui les lit.
    const auto hitE = names(g, rel::impact(g, equip));
    for (const char* n : {"Pompe_1", "Pompe_2", "Cuve_1", "Niveau_Cuve_1", "Consigne_Debit", "Txt_Niveau", "Sld_Consigne"})
        check(hitE.count(n) == 1, std::string("supprimer Station_Modbus touche ") + n);
    check(hitE.count("Texte_Etat") == 0 && hitE.count("T_Pompe") == 0, "supprimer Station_Modbus ne touche ni Texte_Etat ni T_Pompe");
    // L'alarme d'objet : portee par Pompe_Synop (supprimee avec lui).
    const auto synop = node(rel::Kind::Object, "Pompe_Synop");
    const auto alarmSynop = node(rel::Kind::Alarm, "Vue_Station.Pompe_Synop.Defaut");
    check(hasEdge(g, synop, alarmSynop, rel::Link::Contains) && hasEdge(g, alarmSynop, pompe2, rel::Link::Watches, "Pompe_2.Defaut"),
          "Pompe_Synop porte l'alarme Defaut, qui surveille Pompe_2.Defaut");
    // Supprimer la popup : son objet part avec elle ; ce qui l'ouvre est touche.
    bool removedTxt = false, touchedBtn = false;
    for (const auto& h : rel::impactDetails(g, pop)) {
        if (h.node == txtDebit) removedTxt = h.how == rel::Hit::How::Removed;
        if (h.node == btn) touchedBtn = h.how == rel::Hit::How::Affected;
    }
    check(removedTxt && touchedBtn, "supprimer Pop_Pompe : Txt_Debit supprime, Btn_P2 touche");
    // Supprimer un dossier : ses elements montent d'un cran, rien d'autre.
    const auto hitF = rel::impactDetails(g, node(rel::Kind::Folder, "Station"));
    check(hitF.size() == 1 && hitF.front().node == vue && hitF.front().how == rel::Hit::How::Moved, "supprimer le dossier Station : Vue_Station deplacee");

    // << Quels equipements sont utilises dans cette vue ? >>
    check(names(g, rel::equipmentsOfView(g, vue)) == std::set<std::string>{"Station_Modbus"}, "equipements de Vue_Station");
    check(rel::equipmentsOfView(g, node(rel::Kind::Popup, "Pop_Inutile")).empty(), "equipements de Pop_Inutile : aucun");

    // << Quelles alarmes concernent la pompe 2 ? >>
    check(names(g, rel::alarmsAbout(g, "Pompe_2")) == std::set<std::string>{"Defaut_Pompe_2", "Vue_Station.Pompe_Synop.Defaut"},
          "alarmes de Pompe_2 : " + listed(names(g, rel::alarmsAbout(g, "Pompe_2"))));
    check(names(g, rel::alarmsAbout(g, "Niveau_Cuve_1")) == std::set<std::string>{"Niveau_Haut"}, "alarmes de Niveau_Cuve_1");

    // << Quels objets sont orphelins ? >>
    const auto orph = names(g, rel::orphans(g));
    const std::set<std::string> expected{"Compteur_Inutile", "T_Inutile", "Fonction_Inutile", "Pop_Inutile", "Sym_Inutile", "vieux.svg", "Equip_Vide"};
    check(orph == expected, "orphelins : " + listed(orph));
    check(rel::orphanReason(g, compteur).find("jamais lue") != std::string::npos, "raison : variable jamais lue ni ecrite");
    check(rel::orphanReason(g, pompe1).empty(), "Pompe_1 n'est pas orpheline");
    check(rel::describe(g, pompe1).find("T_Pompe") != std::string::npos, "describe(Pompe_1) : " + rel::describe(g, pompe1));
}

// ---- 2. la verification globale -------------------------------------------------------
void testAudit(const Project& p, bool verbose) {
    const Project before = p;
    const auto r = audit::run(p);
    check(p == before, "run() ne modifie pas le projet");
    if (verbose) {
        std::printf("  rapport : %s\n", r.summary().c_str());
        for (const auto& f : r.findings) {
            std::printf("    [%s] %s (%s, %s) : %s\n", std::string(audit::severityLabel(f.severity)).c_str(), f.category.c_str(),
                        f.source.c_str(), f.where.c_str(), f.message.c_str());
            for (const auto& x : f.fixes)
                std::printf("        -> %s %s : %s -> %s (%s)\n", x.what.c_str(), x.target.c_str(), x.before.c_str(), x.after.c_str(), x.why.c_str());
        }
    }
    std::size_t e = 0, w = 0, i = 0;
    for (const auto& f : r.findings) (f.severity == audit::Severity::Error ? e : f.severity == audit::Severity::Warning ? w : i)++;
    check(e == r.errors && w == r.warnings && i == r.infos, "les comptes par severite : " + r.summary());
    bool sorted = true;
    for (std::size_t k = 1; k < r.findings.size(); ++k) sorted = sorted && r.findings[k - 1].severity <= r.findings[k].severity;
    check(sorted, "les erreurs d'abord, puis les avertissements, puis les informations");
    const auto find = [&](audit::Severity s, std::string_view category, std::string_view text) -> const audit::Finding* {
        for (const auto& f : r.findings)
            if (f.severity == s && (category.empty() || f.category == category) && f.message.find(text) != std::string::npos) return &f;
        return nullptr;
    };
    // L'alarme sur une variable inexistante : une erreur bloquante.
    check(find(audit::Severity::Error, "", "Capteur_Absent") != nullptr, "Alarme_Fantome : Capteur_Absent inexistante (erreur)");
    // Le chevauchement : une erreur (deux ecritures), une fois, avec l'adresse libre la plus proche.
    const auto* ov = find(audit::Severity::Error, "Adresses", "Consigne_Debit");
    check(ov != nullptr, "chevauchement Consigne_Debit / Cuve_1 : erreur Adresses");
    if (ov) {
        check(ov->fixes.size() == 1 && ov->fixes.front().what == "adresse" && ov->fixes.front().target == "variable Consigne_Debit"
                  && ov->fixes.front().before == "40013" && ov->fixes.front().after == "40016",
              "correction proposee : Consigne_Debit 40013 -> 40016");
        check(ov->message.find("Cuve_1") != std::string::npos, "le constat nomme Cuve_1");
    }
    std::size_t aboutOverlap = 0;
    for (const auto& f : r.findings)
        if (f.message.find("Consigne_Debit") != std::string::npos && f.message.find("Cuve_1") != std::string::npos) ++aboutOverlap;
    check(aboutOverlap == 1, "le chevauchement dit une seule fois (Generer le disait sans correction) : " + std::to_string(aboutOverlap));
    // Les dossiers.
    check(find(audit::Severity::Warning, "Dossiers", "alarmes") != nullptr, "dossiers gardes pour une liste inconnue (alarmes)");
    check(find(audit::Severity::Warning, "", "Archives") != nullptr, "joker Archives/* : dossier absent (Generer)");
    // Les orphelins : des informations (et ceux que Generer dit deja, une fois).
    for (const char* n : {"Compteur_Inutile", "T_Inutile", "Fonction_Inutile"})
        check(find(audit::Severity::Info, "Orphelin", n) != nullptr, std::string("orphelin signale : ") + n);
    check(find(audit::Severity::Info, "", "vieux.svg") != nullptr, "ressource inutilisee : vieux.svg (Generer)");
    check(find(audit::Severity::Info, "", "Equip_Vide") != nullptr, "equipement sans variable : Equip_Vide (Generer)");
    check(find(audit::Severity::Info, "Orphelin", "Pompe_1") == nullptr, "Pompe_1 n'est pas orpheline");
    const auto* orph = find(audit::Severity::Info, "Orphelin", "Compteur_Inutile");
    check(orph && !orph->fixes.empty() && orph->fixes.front().what == "suppression", "un orphelin : sa suppression proposee");
    // Les comptes exacts : l'alarme (Generer la dit deux fois : la variable inexistante, l'expression impossible), le
    // chevauchement ; la popup jamais ouverte (Qualite), le joker, les dossiers ; Sym_Inutile, Equip_Vide, vieux.svg et
    // les trois orphelins du graphe (la popup : une fois, par Generer).
    check(r.errors == 3 && r.warnings == 3 && r.infos == 6, "comptes exacts (3, 3, 6) : " + r.summary());
    check(find(audit::Severity::Info, "Orphelin", "Pop_Inutile") == nullptr, "Pop_Inutile : dite une fois (par la Qualite de Generer)");
    check(!r.notes.empty() && r.notes.front().find("compil") != std::string::npos, "le bilan de Compiler : une note, pas un constat");
}

// ---- 3. les adresses ----------------------------------------------------------------------
std::string movesText(const audit::Optimization& o) {
    std::string s;
    for (const auto& m : o.moves) s += (s.empty() ? "" : "; ") + m.variable + " " + m.before + "->" + m.after;
    return s;
}
std::size_t distinctOverlaps(const Project& p) {
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& o : audit::overlaps(p))
        if (o.a.variable != o.b.variable) pairs.insert({std::min(o.a.variable, o.b.variable), std::max(o.a.variable, o.b.variable)});
    return pairs.size();
}

void testAddresses(const Project& p, bool verbose) {
    const auto rs = audit::ranges(p, "Station_Modbus");
    check(rs.size() == 13, "13 cases liees (4 + 4 + 3 + 1 + 1) : " + std::to_string(rs.size()));
    const auto temp = std::find_if(rs.begin(), rs.end(), [](const audit::Range& x) { return x.cell == "Cuve_1.Temperature"; });
    check(temp != rs.end() && temp->first == 12 && temp->last == 13 && temp->variable == "Cuve_1" && temp->written,
          "Cuve_1.Temperature : mots 12 a 13, ecrite");
    if (temp != rs.end()) check(temp->text() == "registres 40013 \xC3\xA0 40014", "Range::text : " + temp->text());
    const auto ovs = audit::overlaps(p);
    check(ovs.size() == 1, "un chevauchement : " + std::to_string(ovs.size()));
    if (!ovs.empty()) {
        const std::set<std::string> cells{ovs.front().a.cell, ovs.front().b.cell};
        check(cells == std::set<std::string>{"Cuve_1.Temperature", "Consigne_Debit"} && ovs.front().severity == 3 && ovs.front().first == 12
                  && ovs.front().last == 13,
              "Cuve_1.Temperature et Consigne_Debit, deux ecritures, mots 12-13");
    }
    const auto free = audit::freeRanges(p, "Station_Modbus", MemTable::Holding, 0, 30);
    check(free == std::vector<std::pair<long, long>>{{8, 9}, {15, 19}, {22, 30}}, "places libres 0-30 : 8-9, 15-19, 22-30");
    check(audit::nearestFreeAddress(p, "Consigne_Debit") == "40016", "adresse libre la plus proche de Consigne_Debit : 40016");
    std::string why;
    check(audit::nearestFreeAddress(p, "Texte_Etat", &why).empty() && !why.empty(), "Texte_Etat : pas liee (" + why + ")");

    // << Optimise les adresses Modbus de toutes les pompes et cuves, sans creer de chevauchement >>
    const Project before = p;
    const auto o1 = audit::optimize(p, {"Pompe_*", "Cuve_*"});
    check(p == before, "optimize() ne modifie pas le projet");
    if (verbose) {
        std::printf("  pompes et cuves : %s\n", movesText(o1).c_str());
        for (const auto& n : o1.notes) std::printf("    note : %s\n", n.c_str());
        for (const auto& f : o1.before) std::printf("    avant : %s\n", f.text.c_str());
        for (const auto& f : o1.after) std::printf("    apres : %s\n", f.text.c_str());
    }
    check(o1.moves.size() == 1 && o1.moves.front().variable == "Cuve_1" && o1.moves.front().before == "40011" && o1.moves.front().after == "40015",
          "pompes et cuves : seule Cuve_1 bouge (40011 -> 40015) : " + movesText(o1));
    check(o1.overlapsBefore == 1 && o1.overlapsAfter == 0, "pompes et cuves : plus de chevauchement apres");
    {
        Project copy = p;
        std::string refused;
        check(audit::applyMoves(copy, o1.moves, &refused) == o1.moves.size() && refused.empty(), "applyMoves applique le plan");
        check(distinctOverlaps(copy) == 0, "applique : aucun chevauchement");
    }
    // Toutes les variables : rangees sans trou ni chevauchement.
    const auto o2 = audit::optimize(p);
    check(movesText(o2) == "Cuve_1 40011->40009; Niveau_Cuve_1 40021->40014; Consigne_Debit 40013->40016", "toutes : " + movesText(o2));
    check(o2.overlapsAfter == 0, "toutes : aucun chevauchement apres");
    check(!o2.after.empty() && o2.after.front().holes == 0 && o2.after.front().first == 0 && o2.after.front().last == 16,
          "toutes : sans trou (registres 40001 a 40017)" + (o2.after.empty() ? std::string{} : " : " + o2.after.front().text));
    check(!o2.before.empty() && o2.before.front().holes > 0, "avant : des trous" + (o2.before.empty() ? std::string{} : " : " + o2.before.front().text));
    for (const auto& m : o2.moves) check(m.variable != "Pompe_1" && m.variable != "Pompe_2", "deja bien placee, pas de Move : " + m.variable);
    {
        Project copy = p;
        (void)audit::applyMoves(copy, o2.moves);
        check(distinctOverlaps(copy) == 0, "toutes, appliquees : aucun chevauchement");
        const auto again = audit::optimize(copy);
        check(again.moves.empty(), "deja optimise : aucun Move (" + movesText(again) + ")");
    }
    // Deterministe.
    check(movesText(audit::optimize(p)) == movesText(o2), "optimize() deterministe");
    // Cuve_1 figee : elle ne bouge pas, les autres se rangent autour.
    const auto o3 = audit::optimize(p, {}, {"Cuve_1"});
    check(movesText(o3) == "Niveau_Cuve_1 40021->40009; Consigne_Debit 40013->40016", "Cuve_1 figee : " + movesText(o3));
    bool frozenMoved = false;
    for (const auto& m : o3.moves) frozenMoved = frozenMoved || m.variable == "Cuve_1";
    check(!frozenMoved && o3.overlapsAfter == 0, "Cuve_1 figee ne bouge pas, aucun chevauchement apres");
    // Un nom qui ne repond a rien : une note.
    const auto o4 = audit::optimize(p, {"Vanne_*"});
    check(o4.moves.empty() && !o4.notes.empty(), "Vanne_* : rien a ranger, une note");
}

// Les ecritures d'une adresse (Schneider, 4x, HR) gardees ; les zones declarees respectees ; un plan perime refuse.
void testNotations() {
    Project p;
    Equipment e;
    e.id = p.allocate();
    e.name = "Automate";
    e.simulated = true;
    e.zones.declared = true;
    e.zones.of(MemTable::Holding) = {{100, 102}, {200, 299}};
    p.equipments.push_back(e);
    p.programs.variables.push_back(makeVar(p, "A", "INT", "Automate", "%MW100"));
    p.programs.variables.push_back(makeVar(p, "B", "REAL", "Automate", "%MF105"));
    p.programs.variables.push_back(makeVar(p, "C", "INT", "Automate", "4x0201"));
    p.programs.variables.push_back(makeVar(p, "D", "INT", "Automate", "HR210"));
    const auto o = audit::optimize(p);
    // A reste (100) ; B -> %MF101 (101-102) ; C ne tient plus dans 100-102 : la zone suivante, 200, sa place ; D -> HR201.
    check(movesText(o) == "B %MF105->%MF101; D HR210->HR201", "notations et zones : " + movesText(o));
    check(audit::freeRanges(p, "Automate", MemTable::Holding, 0, 300) == std::vector<std::pair<long, long>>{{101, 102}, {201, 209}, {211, 299}},
          "places libres dans les zones declarees");
    check(audit::nearestFreeAddress(p, "B") == "%MF101", "B : la place libre la plus proche, dans les zones (%MF101)");
    // Un appareil reel : la precaution est dite.
    Project real = p;
    real.equipments.front().simulated = false;
    bool warned = false;
    for (const auto& n : audit::optimize(real).notes) warned = warned || n.find("appareil r\xC3\xA9" "el") != std::string::npos;
    check(warned, "appareil reel : la note de precaution");
    // Un plan perime (l'adresse a change depuis) n'est pas applique.
    Project changed = p;
    changed.programs.variables[1].address = "%MF110";
    std::string why;
    check(audit::applyMoves(changed, o.moves, &why) == 1 && why.find("B") != std::string::npos, "plan perime : B refusee (" + why + ")");
}

// Les dossiers incoherents : un meme dossier ecrit de deux facons, un nom illisible, une liste inconnue.
void testFolders() {
    Project p;
    View v = makeView(p, "Vue_A");
    v.folder = "Station";
    p.views.push_back(v);
    p.config.startView = v.id;
    p.listFolders["vues"] = {"station"};          // le meme que Station, autrement ecrit
    p.listFolders["styles"] = {"A//B"};           // un nom vide entre deux /
    p.listFolders["recettes"] = {"Hiver", "Ete"};  // les recettes ne se rangent pas
    p.listFolders["espaces"] = {"Gaz"};           // les espaces de noms gardes : pas une faute
    const auto r = audit::run(p);
    std::size_t folders = 0;
    bool twoWays = false, illegible = false, unknown = false;
    for (const auto& f : r.findings) {
        if (f.category != "Dossiers") continue;
        ++folders;
        twoWays = twoWays || (f.message.find("Station") != std::string::npos && f.message.find("station") != std::string::npos);
        illegible = illegible || f.message.find("A//B") != std::string::npos;
        unknown = unknown || (f.message.find("recettes") != std::string::npos && f.message.find("Hiver, Ete") != std::string::npos);
        if (f.message.find("Gaz") != std::string::npos) check(false, "les espaces de noms gardes ne sont pas des dossiers incoherents");
    }
    check(twoWays, "dossier Station / station : le meme, ecrit de deux facons");
    check(illegible, "dossier A//B : nom illisible");
    check(unknown, "dossiers de la liste inconnue recettes : Hiver, Ete");
    check(folders == 3, "trois constats Dossiers : " + std::to_string(folders));
}

// ---- 4. le temps sur un gros projet ------------------------------------------------------
Project generated() {
    Project p;
    p.programs.types.push_back(makeType(p, "T_Pompe", {member("Marche", "BOOL"), member("Defaut", "BOOL"), member("Debit", "REAL"), member("Vitesse", "INT")}));
    p.programs.types.push_back(makeType(p, "T_Cuve", {member("Niveau", "REAL"), member("Temperature", "REAL"), member("Vanne", "BOOL")}));
    std::vector<std::string> vars;
    for (int e = 0; e < 60; ++e) {
        Equipment eq;
        eq.id = p.allocate();
        eq.name = "Equip_" + std::to_string(e);
        eq.simulated = true;
        p.equipments.push_back(eq);
        int word = 0;
        for (int k = 0; k < 10; ++k) {
            const std::string name = "V_" + std::to_string(e) + "_" + std::to_string(k);
            const char* type = k % 3 == 0 ? "T_Pompe" : k % 3 == 1 ? "T_Cuve" : "REAL";
            const int words = k % 3 == 0 ? 4 : k % 3 == 1 ? 5 : 2;
            char address[16];
            std::snprintf(address, sizeof address, "4%04d", word + 1);
            p.programs.variables.push_back(makeVar(p, name.c_str(), type, eq.name.c_str(), address));
            word += words + (k == 5 ? 3 : 0);   // un trou par equipement
            vars.push_back(name);
        }
    }
    for (int a = 0; a < 60; ++a)
        p.alarms.push_back(makeAlarm(p, ("Alarme_" + std::to_string(a)).c_str(), ("V_" + std::to_string(a) + "_0.Defaut").c_str(), "D\xC3\xA9" "faut"));
    for (int s = 0; s < 20; ++s) {
        Script sc;
        sc.id = p.allocate();
        sc.name = "Script_" + std::to_string(s);
        sc.event = "Cyclique";
        sc.body = vars[static_cast<std::size_t>(s * 30 + 1)] + ".Niveau := " + vars[static_cast<std::size_t>(s * 30 + 2)] + " * 0.5;\n";
        p.programs.scripts.push_back(sc);
    }
    // Le membre a montrer d'une variable : T_Pompe -> .Debit, T_Cuve -> .Niveau, REAL -> elle-meme.
    const auto shown = [&](std::size_t i) {
        const int k = static_cast<int>(i % 10);
        return vars[i] + (k % 3 == 0 ? ".Debit" : k % 3 == 1 ? ".Niveau" : "");
    };
    for (int v = 0; v < 30; ++v) {
        View view = makeView(p, "Vue_" + std::to_string(v));
        for (int o = 0; o < 20; ++o) {
            const std::size_t i = static_cast<std::size_t>(v * 20 + o) % vars.size();
            Object obj = makeObject(o % 2 ? hmi::Kind::Text : hmi::Kind::Bargraph, p.allocate(), "Obj_" + std::to_string(o), 10.0 + o * 40,
                                    10.0 + (o % 5) * 100, view.layers.front().id);
            if (o % 2) obj.set("text", "{" + shown(i) + ":0.0}");
            else obj.setExpr("value", shown(i));
            view.objects.push_back(std::move(obj));
        }
        p.views.push_back(std::move(view));
    }
    p.config.startView = p.views.front().id;
    return p;
}

void testTime(bool verbose) {
    const Project p = generated();
    const auto t0 = std::chrono::steady_clock::now();
    const auto g = rel::build(p);
    const auto t1 = std::chrono::steady_clock::now();
    const auto r = audit::run(p);
    const auto t2 = std::chrono::steady_clock::now();
    const double build = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double run = std::chrono::duration<double, std::milli>(t2 - t1).count();
    check(g.size() > 1000 && r.errors == 0, "le projet genere se lit, sans erreur : " + r.summary());
    if (verbose)
        for (std::size_t k = 0; k < r.findings.size() && k < 3; ++k)
            std::printf("    [%s] %s : %s\n", r.findings[k].category.c_str(), r.findings[k].where.c_str(), r.findings[k].message.c_str());
    std::printf("  projet genere (60 equipements, 600 variables, 30 vues, %zu objets) : build %.0f ms (%zu noeuds, %zu aretes), run %.0f ms (%s)"
                " - total %.2f s, objectif < 2 s : %s\n",
                std::size_t{30 * 20}, build, g.size(), g.edges().size(), run, r.summary().c_str(), (build + run) / 1000.0,
                build + run < 2000.0 ? "tenu" : "NON TENU");
}

} // namespace

int main(int argc, char**) {
    const bool verbose = argc > 1 || std::getenv("XPG_VERBOSE") != nullptr;
    const Project p = station();
    testGraph(p, verbose);
    testAudit(p, verbose);
    testAddresses(p, verbose);
    testNotations();
    testFolders();
    testTime(verbose);
    std::printf("hmi_relations_test : %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
