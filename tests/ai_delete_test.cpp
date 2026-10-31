// =============================================================================
//  tests/ai_delete_test.cpp - 1.21.0 : la suppression intelligente de l'Assistant IA (AiDelete)
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un vrai projet IHM (un Document, sa pile Ctrl+Z) :
//    - un type employe : refuse seul (et dit pourquoi, compte par genre), refuse en << references >> (des variables
//      de ce type), remplace par un type EXISTANT (jamais un nom invente) ;
//    - l'instance n'est pas la definition : supprimer une instance garde le symbole et ses autres instances ; le
//      symbole seul est refuse (ses instances) ; << references >> retire ses instances et ses popups ;
//    - une vue qui appelle un script partage : le script reste ; ce qui devient inutile part en cascade (une popup,
//      puis son script et son image) seulement avec << inutiles >> ; une definition et une variable restent ;
//    - un objet supprime garde sa variable ; une variable remplacee ; ses references retirees ;
//    - les exclusions (exclure, et l'etape retiree du plan) ; une alarme acquittee par son nom ;
//    - UNE commande : un Ctrl+Z rend le projet a l'octet pres, Ctrl+Y le refait ;
//    - la confirmation obligatoire (un element du projet, ou plusieurs objets) ; un genre inconnu refuse a la lecture ;
//    - une transaction refusee au milieu : rien n'est applique ; les dependances circulaires ;
//    - le plan apres Appliquer : << appliquee >> seulement si l'element est absent et plus rien ne le cite ;
//    - la correction de la 1.21.0 : supprimer un symbole (Vues) emporte ses popups (View::ownerSymbol).
// =============================================================================
#include "../src/app/ai/AiDelete.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPlan.hpp"
#include "../src/app/hmi/HmiPanes.hpp"   // hmiDeleteViewCommand (la correction des popups de symbole)
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ai = app::ai;
namespace del = app::ai::del;
namespace plan = app::ai::plan;
using A = ai::Operation::Action;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }
bool anyHas(const std::vector<std::string>& list, const std::string& what) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return has(s, what); });
}
std::string joinAll(const std::vector<std::string>& list) {
    std::string s;
    for (const auto& x : list) s += x + " | ";
    return s;
}
bool listed(const std::vector<del::Item>& items, const std::string& name) {
    return std::any_of(items.begin(), items.end(), [&](const del::Item& it) { return it.name == name; });
}

// Le projet entier, tel que le disque l'ecrirait (sans le compteur des identifiants, qui ne recule jamais).
std::string print(const hmi::Project& p) {
    std::string s;
    for (const auto& f : hmi::serializeProject(p)) {
        s += f.path + "\n";
        if (f.data) s.append(f.data->begin(), f.data->end());
        s += "\n";
    }
    if (const auto at = s.find("prochain_id="); at != std::string::npos) {
        std::size_t end = at + 12;
        while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end;
        s.erase(at, end - at);
    }
    return s;
}

// ---- la fabrique du projet -------------------------------------------------------------------
hmi::TypeMember member(const char* name, const char* type) {
    hmi::TypeMember m;
    m.name = name;
    m.type = type;
    return m;
}
void addType(hmi::Project& p, const char* name, std::vector<hmi::TypeMember> members) {
    hmi::HmiType t;
    t.id = p.allocate();
    t.name = name;
    t.members = std::move(members);
    p.programs.types.push_back(t);
}
void addVar(hmi::Project& p, const char* name, const char* type, const char* equipment = "", const char* address = "", const char* folder = "") {
    hmi::Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = std::string(type) == "REAL" ? "0.0" : std::string(type) == "INT" ? "0" : "";
    v.equipment = equipment;
    v.address = address;
    v.folder = folder;
    p.programs.variables.push_back(v);
}
void addEquipment(hmi::Project& p, const char* name) {
    hmi::Equipment e;
    e.id = p.allocate();
    e.name = name;
    e.simulated = true;
    p.equipments.push_back(e);
}
void addAlarm(hmi::Project& p, const char* name, const char* condition, const char* message) {
    hmi::AlarmDef a;
    a.id = p.allocate();
    a.name = name;
    a.condition = condition;
    a.message = message;
    p.alarms.push_back(a);
}
void addScript(hmi::Project& p, const char* name, const char* event, const char* body) {
    hmi::Script s;
    s.id = p.allocate();
    s.name = name;
    s.event = event;
    s.body = body;
    p.programs.scripts.push_back(s);
}
void addResource(hmi::Project& p, const char* name) {
    static const auto svg = std::make_shared<const hmi::Bytes>(hmi::Bytes{'<', 's', 'v', 'g', '/', '>'});
    hmi::Resource r;
    r.id = p.allocate();
    r.name = name;
    r.format = "SVG";
    r.data = svg;
    r.bytes = svg->size();
    p.assets.resources.push_back(r);
}
hmi::Action click(hmi::Operation op, const char* target, const char* value = "") {
    hmi::Action a;
    a.trigger = hmi::Trigger::Click;
    a.operation = op;
    a.target = target;
    a.value = value;
    return a;
}
hmi::Object& place(hmi::Project& p, hmi::View& v, hmi::Kind kind, const char* name, double x = 10, double y = 10) {
    v.objects.push_back(hmi::makeObject(kind, p.allocate(), name, x, y, v.layers.front().id));
    return v.objects.back();
}
hmi::View makeRole(hmi::Project& p, const char* name, const char* role) {
    hmi::View v = hmi::makeView(p, name);
    v.role = role;
    v.width = 400;
    v.height = 300;
    return v;
}

//  Les types T_Pompe (employe), T_Pompe2 (le meme, le remplacant), T_Vide (sans emploi) ; deux equipements ; des
//  variables (deux pompes liees, des niveaux, une consigne, un compteur, deux libres, une rangee dans un dossier) ;
//  deux alarmes ; trois scripts (un cyclique, un partage par deux vues, un appele par une popup) ; deux images ; le
//  symbole Sym_Pompe et sa popup Pop_Sym ; les popups Pop_Aide (une image, un bouton qui appelle Script_Aide) et
//  Pop_Pompe ; Vue_A (deux instances, un texte, un curseur, des boutons) ; Vue_B (la vue de demarrage).
hmi::Project station() {
    hmi::Project p;
    p.config.width = 1280;
    p.config.height = 800;
    addType(p, "T_Pompe", {member("Marche", "BOOL"), member("Defaut", "BOOL"), member("Debit", "REAL")});
    addType(p, "T_Pompe2", {member("Marche", "BOOL"), member("Defaut", "BOOL"), member("Debit", "REAL")});
    addType(p, "T_Vide", {member("X", "INT")});
    addEquipment(p, "Station_Modbus");
    addEquipment(p, "Equip_2");
    addVar(p, "Pompe_1", "T_Pompe", "Station_Modbus", "40001");
    addVar(p, "Pompe_2", "T_Pompe", "Station_Modbus", "40005");
    addVar(p, "Niveau_1", "REAL");
    addVar(p, "Niveau_2", "REAL");
    addVar(p, "Consigne", "REAL");
    addVar(p, "Compteur", "INT");
    addVar(p, "Libre_1", "INT");
    addVar(p, "Libre_2", "INT");
    addVar(p, "Archive_1", "REAL", "", "", "Process");
    p.programs.folders.push_back("Process");
    addAlarm(p, "Defaut_P1", "Pompe_1.Defaut", "Pompe 1 en d\xC3\xA9" "faut");
    addAlarm(p, "Niveau_Haut", "Niveau_1 > 90.0", "Niveau haut");
    addScript(p, "Calcul", "Cyclique", "Compteur := Compteur + 1;\n");
    addScript(p, "Calcul_Partage", "Appel", "Niveau_2 := Niveau_2 + 1.0;\n");
    addScript(p, "Script_Aide", "Appel", "Compteur := 0;\n");
    addResource(p, "logo.svg");
    addResource(p, "aide.svg");
    // Le symbole d'une pompe (un parametre Pompe : T_Pompe) et sa popup.
    hmi::View sym = makeRole(p, "Sym_Pompe", "symbole");
    hmi::ViewParam pp;
    pp.name = "Pompe";
    pp.defaultValue = "Pompe_1";
    pp.type = "T_Pompe";
    sym.params.push_back(pp);
    place(p, sym, hmi::Kind::Indicator, "Voyant").setExpr("value", "Pompe.Marche");
    p.views.push_back(sym);
    hmi::View popSym = makeRole(p, "Pop_Sym", "popup");
    popSym.ownerSymbol = sym.id;
    place(p, popSym, hmi::Kind::Text, "Txt").set("text", "D\xC3\xA9tail");
    p.views.push_back(popSym);
    // Les popups.
    hmi::View aide = makeRole(p, "Pop_Aide", "popup");
    place(p, aide, hmi::Kind::Image, "Img_Aide").set("image", "aide.svg");
    place(p, aide, hmi::Kind::Button, "Btn_Script", 10, 120).actions.push_back(click(hmi::Operation::CallScript, "Script_Aide"));
    p.views.push_back(aide);
    hmi::View popPompe = makeRole(p, "Pop_Pompe", "popup");
    hmi::ViewParam ep;
    ep.name = "Equipement";
    ep.defaultValue = "Pompe_1";
    ep.type = "T_Pompe";
    popPompe.params.push_back(ep);
    place(p, popPompe, hmi::Kind::Text, "Txt_Debit").set("text", "D\xC3\xA9" "bit : {Equipement.Debit:0.0}");
    p.views.push_back(popPompe);
    // Vue_A.
    hmi::View a = hmi::makeView(p, "Vue_A");
    hmi::Object& i1 = place(p, a, hmi::Kind::SymbolInstance, "Inst_P1", 10, 10);
    i1.set("symbol", "Sym_Pompe");
    i1.set("params", "Pompe := Pompe_1");
    hmi::Object& i2 = place(p, a, hmi::Kind::SymbolInstance, "Inst_P2", 200, 10);
    i2.set("symbol", "Sym_Pompe");
    i2.set("params", "Pompe := Pompe_2");
    place(p, a, hmi::Kind::Text, "Txt_Niveau", 10, 200).set("text", "Niveau : {Niveau_1:0.0} %");
    place(p, a, hmi::Kind::Slider, "Sld_Consigne", 10, 300).set("variable", "Consigne");
    place(p, a, hmi::Kind::Button, "Btn_Aide", 300, 300).actions.push_back(click(hmi::Operation::Popup, "Pop_Aide"));
    place(p, a, hmi::Kind::Button, "Btn_Calc", 400, 300).actions.push_back(click(hmi::Operation::CallScript, "Calcul_Partage"));
    place(p, a, hmi::Kind::Image, "Img_Logo", 600, 10).set("image", "logo.svg");
    place(p, a, hmi::Kind::Button, "Btn_Ack", 500, 300).actions.push_back(click(hmi::Operation::AckAlarm, "Niveau_Haut"));
    p.views.push_back(a);
    // Vue_B (la vue de demarrage).
    hmi::View b = hmi::makeView(p, "Vue_B");
    place(p, b, hmi::Kind::Button, "Btn_Calc2", 10, 10).actions.push_back(click(hmi::Operation::CallScript, "Calcul_Partage"));
    place(p, b, hmi::Kind::Text, "Txt_N2", 10, 100).set("text", "{Niveau_2}");
    place(p, b, hmi::Kind::Button, "Btn_Pompe", 10, 200).actions.push_back(click(hmi::Operation::Popup, "Pop_Pompe", "Equipement := Pompe_2"));
    p.views.push_back(b);
    p.config.startView = b.id;
    return p;
}

struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;

    Fixture() { doc->project = station(); }
    [[nodiscard]] hmi::Project& project() const { return doc->project; }
    [[nodiscard]] ai::Proposal proposal(std::vector<ai::Operation> ops, const char* open = "Vue_A") const {
        ai::Proposal p;
        p.id = "op1";
        p.request = "essai";
        p.summary = "Essai";
        p.ops = std::move(ops);
        ai::scopeProposal(p, doc->project.viewByName(open));
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        return cmd && static_cast<bool>(stack.push(std::move(cmd)));
    }
};

// Une operation supprimer : le genre, les noms, la strategie (vide : seul), le remplacant, les exclusions, la vue.
ai::Operation removal(std::string genre, std::vector<std::string> names, std::string strategy = {}, std::string replacement = {},
                      std::vector<std::string> keep = {}, std::string view = {}) {
    ai::Operation o;
    o.action = A::Remove;
    o.kind = std::move(genre);
    o.objects = std::move(names);
    if (!strategy.empty()) o.props.emplace_back("strategie", std::move(strategy));
    if (!replacement.empty()) o.props.emplace_back("remplacer_par", std::move(replacement));
    o.keep = std::move(keep);
    o.target = std::move(view);
    return o;
}
del::Request request(del::Genre g, std::vector<std::string> names, del::Strategy s = del::Strategy::Only, std::string replacement = {},
                     bool useless = false, std::vector<std::string> keep = {}, std::string view = {}) {
    del::Request r;
    r.genre = g;
    r.names = std::move(names);
    r.strategy = s;
    r.replacement = std::move(replacement);
    r.useless = useless;
    r.keep = std::move(keep);
    r.view = std::move(view);
    return r;
}

// ---- 1. un type employe ------------------------------------------------------------------------------------------
void typeWithUsers() {
    std::printf("1 - un type employ\xC3\xA9\n");
    Fixture f;
    const std::string before = print(f.project());
    // seul : refuse, et l'analyse dit pourquoi (par genre).
    const del::Analysis a = del::analyze(f.project(), request(del::Genre::Type, {"T_Pompe"}));
    check(!a.ok(), "T_Pompe seul : refus\xC3\xA9");
    check(anyHas(a.errors, "Pompe_1") && anyHas(a.errors, "remplace"), "le refus cite les variables et propose de remplacer : " + joinAll(a.errors));
    check(listed(a.direct, "Pompe_1") && listed(a.direct, "Pompe_2") && listed(a.direct, "Pop_Pompe") && listed(a.direct, "Sym_Pompe"),
          "d\xC3\xA9pendances directes : les deux variables, la popup et le symbole (leurs param\xC3\xA8tres)");
    check(listed(a.indirect, "Defaut_P1"), "d\xC3\xA9pendance indirecte : l'alarme qui surveille Pompe_1.Defaut");
    check(listed(a.equipments, "Station_Modbus"), "l'\xC3\xA9quipement des variables concern\xC3\xA9" "es");
    const auto sum = a.summary();
    check(anyHas(sum, "2 variables directement concern\xC3\xA9" "es"), "le r\xC3\xA9sum\xC3\xA9 : 2 variables directement concern\xC3\xA9" "es (" + joinAll(sum) + ")");
    check(anyHas(sum, "alarme d\xC3\xA9pendante"), "le r\xC3\xA9sum\xC3\xA9 : 1 alarme d\xC3\xA9pendante");
    check(anyHas(sum, "\xC3\xA9quipement concern\xC3\xA9"), "le r\xC3\xA9sum\xC3\xA9 : 1 \xC3\xA9quipement concern\xC3\xA9");
    check(print(f.project()) == before, "l'analyse ne touche pas au projet");
    // references : des variables de ce type ne se << retirent >> pas - refuse.
    const del::Analysis r = del::analyze(f.project(), request(del::Genre::Type, {"T_Pompe"}, del::Strategy::References));
    check(!r.ok() && anyHas(r.errors, "Pompe_1"), "T_Pompe en r\xC3\xA9" "f\xC3\xA9rences : refus\xC3\xA9 (des variables de ce type) : " + joinAll(r.errors));
    // remplacer par un nom invente : refuse.
    const del::Analysis bad = del::analyze(f.project(), request(del::Genre::Type, {"T_Pompe"}, del::Strategy::Replace, "T_Fantome"));
    check(!bad.ok() && anyHas(bad.errors, "n'existe pas") && anyHas(bad.errors, "jamais un nom invent"), "remplacer par T_Fantome : refus\xC3\xA9 (inexistant)");
    // par une proposition : seul refuse, rien n'est applique.
    auto p = f.proposal({removal("type", {"T_Pompe"})});
    const ai::Check ck = ai::validate(f.project(), p);
    check(!ck.ok() && ck.confirmRemoval, "la proposition : refus\xC3\xA9" "e, et une confirmation serait demand\xC3\xA9" "e");
    std::string why;
    check(!f.apply(p, &why) && print(f.project()) == before, "rien n'est appliqu\xC3\xA9 (" + why + ")");
    // remplacer par T_Pompe2 (existant) : applique, les variables passent a T_Pompe2.
    auto q = f.proposal({removal("type", {"T_Pompe"}, "remplacer", "T_Pompe2")});
    const ai::Check ck2 = ai::validate(f.project(), q);
    check(ck2.ok(), "remplacer par T_Pompe2 : valid\xC3\xA9" "e (" + joinAll(ck2.errors) + ")");
    check(f.apply(q, &why), "remplacer par T_Pompe2 : appliqu\xC3\xA9" "e (" + why + ")");
    const auto* p1 = f.project().variable("Pompe_1");
    check(!f.project().hmiTypeByName("T_Pompe") && p1 && p1->type == "T_Pompe2", "T_Pompe est parti, Pompe_1 est un T_Pompe2");
    check(f.project().viewByName("Pop_Pompe") && f.project().viewByName("Pop_Pompe")->params.front().type == "T_Pompe2", "le param\xC3\xA8tre de Pop_Pompe suit");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "un Ctrl+Z : le projet d'avant, \xC3\xA0 l'octet pr\xC3\xA8s");
}

// ---- 2. l'instance et la definition ------------------------------------------------------------------------------
void instanceVsDefinition() {
    std::printf("2 - l'instance n'est pas la d\xC3\xA9" "finition\n");
    Fixture f;
    const std::string before = print(f.project());
    std::string why;
    auto p = f.proposal({removal("instance", {"Inst_P1"}, {}, {}, {}, "Vue_A")});
    const ai::Check ck = ai::validate(f.project(), p);
    check(ck.ok() && !ck.confirmRemoval, "une instance : valid\xC3\xA9" "e, sans confirmation (un seul objet) (" + joinAll(ck.errors) + ")");
    check(!ck.removals.empty(), "l'aper\xC3\xA7u : la cat\xC3\xA9gorie Suppressions a son analyse");
    check(f.apply(p, &why), "appliqu\xC3\xA9" "e (" + why + ")");
    const hmi::View* a = f.project().viewByName("Vue_A");
    check(a && !a->objectByName("Inst_P1") && a->objectByName("Inst_P2"), "Inst_P1 part, Inst_P2 reste");
    check(f.project().viewByName("Sym_Pompe") && f.project().viewByName("Pop_Sym"), "le symbole (sa d\xC3\xA9" "finition) et sa popup restent");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "Ctrl+Z : l'instance revient");
    // une instance qui n'en est pas une
    auto bad = f.proposal({removal("instance", {"Txt_Niveau"}, {}, {}, {}, "Vue_A")});
    const ai::Check ckBad = ai::validate(f.project(), bad);
    check(!ckBad.ok() && anyHas(ckBad.errors, "n'est pas une instance"), "Txt_Niveau en instance : refus\xC3\xA9");
    // le symbole seul : refuse (ses instances)
    const del::Analysis s = del::analyze(f.project(), request(del::Genre::Symbol, {"Sym_Pompe"}));
    check(!s.ok() && anyHas(s.errors, "Inst_P1") && anyHas(s.errors, "instance"), "Sym_Pompe seul : refus\xC3\xA9 (ses instances) : " + joinAll(s.errors));
    // un symbole demande comme une vue : le genre est dit
    const del::Analysis v = del::analyze(f.project(), request(del::Genre::View, {"Sym_Pompe"}));
    check(!v.ok() && anyHas(v.errors, "symbole"), "Sym_Pompe en genre vue : refus\xC3\xA9, c'est un symbole");
    // references : ses instances partent, sa popup aussi (View::ownerSymbol) ; la vue Vue_A et le reste demeurent
    auto q = f.proposal({removal("symbole", {"Sym_Pompe"}, "references")});
    const ai::Check ck2 = ai::validate(f.project(), q);
    check(ck2.ok() && ck2.confirmRemoval && ck2.projectRemovals == 1, "le symbole en r\xC3\xA9" "f\xC3\xA9rences : valid\xC3\xA9, confirmation obligatoire (" + joinAll(ck2.errors) + ")");
    check(f.apply(q, &why), "appliqu\xC3\xA9" "e (" + why + ")");
    a = f.project().viewByName("Vue_A");
    check(!f.project().viewByName("Sym_Pompe") && !f.project().viewByName("Pop_Sym"), "Sym_Pompe et sa popup Pop_Sym sont partis");
    check(a && !a->objectByName("Inst_P1") && !a->objectByName("Inst_P2") && a->objectByName("Txt_Niveau"), "ses instances parties, le reste de Vue_A demeure");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "Ctrl+Z : tout revient d'un coup");
}

// ---- 3. une vue, un script partage, la cascade des orphelins ---------------------------------------------------------
void viewSharedScript() {
    std::printf("3 - une vue, un script partag\xC3\xA9, les orphelins en cascade\n");
    Fixture f;
    // sans << inutiles >> : seule la vue (et ses objets) part ; ce qui deviendrait orphelin est dit, garde.
    hmi::Project work = f.project();
    del::Analysis a;
    check(del::run(work, request(del::Genre::View, {"Vue_A"}), a), "Vue_A seule : supprim\xC3\xA9" "e (" + joinAll(a.errors) + ")");
    check(!work.viewByName("Vue_A") && work.viewByName("Pop_Aide") && work.generalScript("Script_Aide"), "Pop_Aide et Script_Aide restent (pas d'inutiles)");
    check(work.generalScript("Calcul_Partage"), "le script partag\xC3\xA9 reste");
    check(listed(a.shared, "Calcul_Partage"), "Calcul_Partage : partag\xC3\xA9 (gard\xC3\xA9)");
    check(listed(a.orphans, "Pop_Aide") && listed(a.orphans, "Sym_Pompe") && listed(a.orphans, "Consigne"),
          "deviendraient orphelins : Pop_Aide, Sym_Pompe (d\xC3\xA9" "finition), Consigne (variable)");
    // avec << inutiles >> : la popup, puis son script et son image ; jamais le symbole ni la variable.
    hmi::Project work2 = f.project();
    del::Analysis b;
    check(del::run(work2, request(del::Genre::View, {"Vue_A"}, del::Strategy::Only, {}, true), b), "Vue_A + inutiles : supprim\xC3\xA9" "e (" + joinAll(b.errors) + ")");
    check(!work2.viewByName("Pop_Aide") && !work2.generalScript("Script_Aide") && !work2.resourceByName("aide.svg") && !work2.resourceByName("logo.svg"),
          "en cascade : Pop_Aide, puis Script_Aide et aide.svg ; logo.svg");
    check(listed(b.removed, "Script_Aide") && listed(b.removed, "aide.svg"), "la cascade est dite (ce qui part)");
    check(work2.generalScript("Calcul_Partage") && work2.viewByName("Sym_Pompe") && work2.variable("Consigne") && work2.generalScript("Calcul"),
          "restent : le script partag\xC3\xA9, le symbole, la variable, le script cyclique");
    check(anyHas(b.summary(), "devenu"), "le r\xC3\xA9sum\xC3\xA9 dit les devenus inutiles (" + joinAll(b.summary()) + ")");
    // une exclusion : Pop_Aide garde, donc son script et son image aussi.
    hmi::Project work3 = f.project();
    del::Analysis c;
    check(del::run(work3, request(del::Genre::View, {"Vue_A"}, del::Strategy::Only, {}, true, {"Pop_Aide"}), c), "Vue_A + inutiles, exclure Pop_Aide");
    check(work3.viewByName("Pop_Aide") && work3.generalScript("Script_Aide") && work3.resourceByName("aide.svg") && !work3.resourceByName("logo.svg"),
          "Pop_Aide exclue : elle reste, avec son script et son image ; logo.svg part");
}

// ---- 4. un objet garde sa variable ; une variable remplacee, ses references retirees -----------------------------------
void objectsAndVariables() {
    std::printf("4 - un objet garde sa variable ; remplacer, r\xC3\xA9" "f\xC3\xA9rences\n");
    Fixture f;
    const std::string before = print(f.project());
    std::string why;
    auto p = f.proposal({removal("objet", {"Sld_Consigne"}, {}, {}, {}, "Vue_A")});
    check(f.apply(p, &why), "supprimer le curseur (" + why + ")");
    check(!f.project().viewByName("Vue_A")->objectByName("Sld_Consigne") && f.project().variable("Consigne"), "le curseur part, sa variable Consigne reste");
    (void)f.stack.undo();
    check(print(f.project()) == before, "Ctrl+Z");
    // une variable lue : seule, refusee
    const del::Analysis s = del::analyze(f.project(), request(del::Genre::Variable, {"Niveau_1"}));
    check(!s.ok() && anyHas(s.errors, "Txt_Niveau") && anyHas(s.errors, "Niveau_Haut"), "Niveau_1 seule : refus\xC3\xA9" "e (lue par Txt_Niveau, Niveau_Haut)");
    // remplacee par Niveau_2 (existante) : le texte et l'alarme suivent
    auto q = f.proposal({removal("variable", {"Niveau_1"}, "remplacer", "Niveau_2")});
    check(f.apply(q, &why), "Niveau_1 remplac\xC3\xA9" "e par Niveau_2 (" + why + ")");
    const auto* txt = f.project().viewByName("Vue_A")->objectByName("Txt_Niveau");
    const auto* al = f.project().alarmByName("Niveau_Haut");
    check(!f.project().variable("Niveau_1") && txt && has(txt->text("text"), "Niveau_2") && !has(txt->text("text"), "Niveau_1"), "Txt_Niveau lit Niveau_2");
    check(al && has(al->condition, "Niveau_2") && !has(al->condition, "Niveau_1"), "Niveau_Haut surveille Niveau_2");
    check(std::count_if(f.project().programs.variables.begin(), f.project().programs.variables.end(),
                        [](const hmi::Variable& v) { return v.name == "Niveau_2"; }) == 1,
          "une seule Niveau_2");
    (void)f.stack.undo();
    check(print(f.project()) == before, "Ctrl+Z : Niveau_1 revient, les textes aussi");
    // remplacee par une variable inventee : refusee
    const del::Analysis inv = del::analyze(f.project(), request(del::Genre::Variable, {"Niveau_1"}, del::Strategy::Replace, "Niveau_Invente"));
    check(!inv.ok() && anyHas(inv.errors, "n'existe pas"), "remplacer par Niveau_Invente : refus\xC3\xA9");
    // references : le curseur ne l'ecrit plus (il reste), la variable part
    auto r = f.proposal({removal("variable", {"Consigne"}, "references")});
    const ai::Check ckR = ai::validate(f.project(), r);
    check(ckR.ok() && ckR.confirmRemoval, "Consigne en r\xC3\xA9" "f\xC3\xA9rences : valid\xC3\xA9" "e, confirmation (" + joinAll(ckR.errors) + ")");
    check(std::any_of(ckR.items.begin(), ckR.items.end(), [](const ai::Item& it) { return it.name == "Sld_Consigne" && it.what == ai::Item::What::Modify; }),
          "l'aper\xC3\xA7u : Sld_Consigne modifi\xC3\xA9");
    check(f.apply(r, &why), "appliqu\xC3\xA9" "e (" + why + ")");
    const auto* sld = f.project().viewByName("Vue_A")->objectByName("Sld_Consigne");
    check(!f.project().variable("Consigne") && sld && sld->text("variable").empty(), "Consigne part, Sld_Consigne reste sans variable");
    (void)f.stack.undo();
    // references : une variable lue par une alarme (une condition) ne se retire pas
    const del::Analysis refAl = del::analyze(f.project(), request(del::Genre::Variable, {"Niveau_1"}, del::Strategy::References));
    check(!refAl.ok() && anyHas(refAl.errors, "Niveau_Haut"), "Niveau_1 en r\xC3\xA9" "f\xC3\xA9rences : refus\xC3\xA9 (la condition de Niveau_Haut)");
    // un equipement en references : ses variables sont deliees, gardees
    hmi::Project w = f.project();
    del::Analysis eq;
    check(del::run(w, request(del::Genre::Equipment, {"Station_Modbus"}, del::Strategy::References), eq), "Station_Modbus en r\xC3\xA9" "f\xC3\xA9rences (" + joinAll(eq.errors) + ")");
    check(!w.equipmentByName("Station_Modbus") && w.variable("Pompe_1") && w.variable("Pompe_1")->equipment.empty(), "ses variables restent, d\xC3\xA9li\xC3\xA9" "es");
    // un dossier de variables : ses variables remontent
    hmi::Project w2 = f.project();
    del::Analysis fo;
    check(del::run(w2, [] {
              del::Request rq = request(del::Genre::Folder, {"Process"});
              rq.list = "variables";
              return rq;
          }(), fo),
          "le dossier Process (" + joinAll(fo.errors) + ")");
    check(w2.variable("Archive_1") && w2.variable("Archive_1")->folder.empty(), "Archive_1 remonte \xC3\xA0 la racine");
}

// ---- 5. les exclusions, une alarme, la popup en references ---------------------------------------------------------
void exclusionsAndAlarms() {
    std::printf("5 - exclusions, une alarme acquitt\xC3\xA9" "e, une popup en r\xC3\xA9" "f\xC3\xA9rences\n");
    Fixture f;
    const std::string before = print(f.project());
    std::string why;
    auto p = f.proposal({removal("variable", {"Libre_1", "Libre_2"}, {}, {}, {"Libre_2"})});
    check(f.apply(p, &why), "deux variables, Libre_2 exclue (" + why + ")");
    check(!f.project().variable("Libre_1") && f.project().variable("Libre_2"), "Libre_1 part, Libre_2 (exclue) reste");
    (void)f.stack.undo();
    // l'etape retiree du plan : rien ne part
    auto q = f.proposal({removal("variable", {"Libre_1"}), removal("type", {"T_Vide"})});
    q.excluded = {0};
    const ai::Check ck = ai::validate(f.project(), q);
    check(ck.ok() && ck.outcomes.size() == 2 && ck.outcomes[0].state == ai::Check::Outcome::State::Excluded, "l'\xC3\xA9tape retir\xC3\xA9" "e : exclue");
    check(f.apply(q, &why) && f.project().variable("Libre_1") && !f.project().hmiTypeByName("T_Vide"), "Libre_1 (retir\xC3\xA9" "e) reste, T_Vide part");
    (void)f.stack.undo();
    check(print(f.project()) == before, "Ctrl+Z");
    // une alarme acquittee par un bouton (par son nom) : seule, refusee a la verification ; en references, l'action part
    const del::Analysis s = del::analyze(f.project(), request(del::Genre::Alarm, {"Niveau_Haut"}));
    check(!s.ok() && anyHas(s.errors, "Btn_Ack"), "Niveau_Haut seule : refus\xC3\xA9" "e (Btn_Ack l'acquitte) : " + joinAll(s.errors));
    check(listed(s.potential, "Vue_A . Btn_Ack . action 1 (" + std::string(hmi::operationLabel(hmi::Operation::AckAlarm)) + ")"),
          "Btn_Ack : une d\xC3\xA9pendance potentielle (par le nom)");
    hmi::Project w = f.project();
    del::Analysis r;
    check(del::run(w, request(del::Genre::Alarm, {"Niveau_Haut"}, del::Strategy::References), r), "Niveau_Haut en r\xC3\xA9" "f\xC3\xA9rences (" + joinAll(r.errors) + ")");
    const auto* ack = w.viewByName("Vue_A")->objectByName("Btn_Ack");
    check(!w.alarmByName("Niveau_Haut") && ack && ack->actions.empty(), "l'alarme part, le bouton reste sans son action");
    // une popup en references : le bouton qui l'ouvrait perd son action ; le script qu'elle appelait reste (sans inutiles)
    hmi::Project w2 = f.project();
    del::Analysis pr;
    check(del::run(w2, request(del::Genre::Popup, {"Pop_Aide"}, del::Strategy::References), pr), "Pop_Aide en r\xC3\xA9" "f\xC3\xA9rences (" + joinAll(pr.errors) + ")");
    const auto* btn = w2.viewByName("Vue_A")->objectByName("Btn_Aide");
    check(!w2.viewByName("Pop_Aide") && btn && btn->actions.empty() && w2.generalScript("Script_Aide"), "Pop_Aide part, Btn_Aide reste sans action, Script_Aide reste");
    // la meme, seule : refusee (Btn_Aide l'ouvre)
    const del::Analysis po = del::analyze(f.project(), request(del::Genre::Popup, {"Pop_Aide"}));
    check(!po.ok() && anyHas(po.errors, "Btn_Aide"), "Pop_Aide seule : refus\xC3\xA9" "e (Btn_Aide l'ouvre)");
}

// ---- 6. un seul Ctrl+Z ; la confirmation ; la lecture -------------------------------------------------------------
void transaction() {
    std::printf("6 - une seule commande, la confirmation, la lecture\n");
    Fixture f;
    const std::string before = print(f.project());
    std::string why;
    auto inutiles = removal("vue", {"Vue_A"}, "inutiles");
    auto p = f.proposal({inutiles, removal("variable", {"Libre_1"}), removal("type", {"T_Vide"})});
    const ai::Check ck = ai::validate(f.project(), p);
    check(ck.ok() && ck.confirmRemoval && ck.projectRemovals == 3 && ck.removals.size() == 3, "trois suppressions : valid\xC3\xA9" "es, confirmation (" + joinAll(ck.errors) + ")");
    const del::Confirmation c = del::confirmation(ck, p);
    check(has(c.title, "\xC3\xA9l\xC3\xA9ments du projet") && !c.items.empty(), "la confirmation dit les \xC3\xA9l\xC3\xA9ments du projet (" + c.title + ")");
    const std::size_t stackBefore = f.stack.done().size();
    check(f.apply(p, &why), "appliqu\xC3\xA9" "e (" + why + ")");
    check(f.stack.done().size() == stackBefore + 1, "UNE commande sur la pile");
    const std::string after = print(f.project());
    check(!f.project().viewByName("Vue_A") && !f.project().viewByName("Pop_Aide") && !f.project().variable("Libre_1") && !f.project().hmiTypeByName("T_Vide"),
          "la vue, la popup devenue inutile, la variable et le type sont partis");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "Ctrl+Z : le projet d'avant, \xC3\xA0 l'octet pr\xC3\xA8s");
    check(static_cast<bool>(f.stack.redo()) && print(f.project()) == after, "Ctrl+Y : le projet d'apr\xC3\xA8s");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "Ctrl+Z encore");
    // la confirmation : un objet seul non ; deux objets oui ; un element du projet toujours
    check(!ai::validate(f.project(), f.proposal({removal("", {"Txt_Niveau"})})).confirmRemoval, "un objet : pas de confirmation");
    check(ai::validate(f.project(), f.proposal({removal("", {"Txt_Niveau", "Img_Logo"})})).confirmRemoval, "deux objets : confirmation");
    check(ai::validate(f.project(), f.proposal({removal("variable", {"Libre_1"})})).confirmRemoval, "une variable : confirmation");
    // la lecture : un genre ou une action inconnus sont refuses ; une suppression se relit telle quelle
    std::vector<ai::Operation> ops;
    ai::json::Value v;
    check(ai::json::parse("[{\"action\": \"supprimer\", \"genre\": \"licorne\", \"objets\": [\"X\"]}]", v) && !ai::operationsFromJson(v, ops, &why) && has(why, "licorne"),
          "un genre inconnu : refus\xC3\xA9 \xC3\xA0 la lecture");
    check(ai::json::parse("[{\"action\": \"detruire\", \"objets\": [\"X\"]}]", v) && !ai::operationsFromJson(v, ops, &why), "une action inconnue : refus\xC3\xA9" "e");
    check(ai::json::parse("[{\"action\": \"supprimer\", \"genre\": \"type\", \"objets\": [\"T_Pompe\"], \"strategie\": \"remplacer\", \"remplacer_par\": "
                          "\"T_Pompe2\", \"exclure\": [\"Pompe_2\"]}]",
                          v)
              && ai::operationsFromJson(v, ops, &why) && ops.size() == 1,
          "une suppression se lit (" + why + ")");
    if (ops.size() == 1) {
        del::Request rq;
        check(del::requestOf(ops[0], rq) && rq.genre == del::Genre::Type && rq.strategy == del::Strategy::Replace && rq.replacement == "T_Pompe2"
                  && rq.keep == std::vector<std::string>{"Pompe_2"},
              "genre, strat\xC3\xA9gie, rempla\xC3\xA7" "ant, exclusions");
        std::vector<ai::Operation> again;
        check(ai::operationsFromJson(ai::operationsToJson(ops), again) && again == ops, "\xC3\xA9" "crite puis relue : la m\xC3\xAAme");
        check(ai::projectLevel(ops[0]) && !ai::projectLevel(removal("instance", {"X"})), "un type : du projet ; une instance : de la vue");
    }
}

// ---- 7. une transaction refusee au milieu ; les dependances circulaires --------------------------------------------
void failuresAndCycles() {
    std::printf("7 - un \xC3\xA9" "chec au milieu, des d\xC3\xA9pendances circulaires\n");
    Fixture f;
    const std::string before = print(f.project());
    std::string why;
    // la deuxieme suppression est refusee : rien n'est applique (strict)
    auto p = f.proposal({removal("variable", {"Libre_1"}), removal("type", {"T_Pompe"}), removal("type", {"T_Vide"})});
    const ai::Check ck = ai::validate(f.project(), p);
    check(!ck.ok() && ck.outcomes.size() == 3 && ck.outcomes[1].state == ai::Check::Outcome::State::Failed
              && ck.outcomes[0].state == ai::Check::Outcome::State::Validated,
          "T_Pompe \xC3\xA9" "choue, les autres sont valid\xC3\xA9" "es");
    check(!f.apply(p, &why) && print(f.project()) == before, "refus\xC3\xA9" "e : le projet est rest\xC3\xA9 tel qu'il \xC3\xA9tait (" + why + ")");
    // malgre les erreurs : seulement ce qui est valide, en une commande
    std::vector<std::string> skipped;
    auto cmd = ai::makeCommandLenient(f.doc, p, skipped, &why);
    check(cmd && static_cast<bool>(f.stack.push(std::move(cmd))) && !skipped.empty(), "appliqu\xC3\xA9" "e malgr\xC3\xA9 l'erreur");
    check(!f.project().variable("Libre_1") && !f.project().hmiTypeByName("T_Vide") && f.project().hmiTypeByName("T_Pompe"), "Libre_1 et T_Vide partent, T_Pompe reste");
    (void)f.stack.undo();
    check(print(f.project()) == before, "Ctrl+Z");
    // deux vues qui s'ouvrent l'une l'autre : ensemble, oui ; une seule, non
    Fixture g;
    hmi::Project& pr = g.project();
    hmi::View c = hmi::makeView(pr, "Vue_C");
    hmi::View d = hmi::makeView(pr, "Vue_D");
    place(pr, c, hmi::Kind::Button, "Vers_D").actions.push_back(click(hmi::Operation::Navigate, "Vue_D"));
    place(pr, d, hmi::Kind::Button, "Vers_C").actions.push_back(click(hmi::Operation::Navigate, "Vue_C"));
    pr.views.push_back(c);
    pr.views.push_back(d);
    const del::Analysis one = del::analyze(pr, request(del::Genre::View, {"Vue_C"}));
    check(!one.ok() && anyHas(one.errors, "Vers_C"), "Vue_C seule : refus\xC3\xA9" "e (Vers_C l'ouvre)");
    const del::Analysis both = del::analyze(pr, request(del::Genre::View, {"Vue_C", "Vue_D"}));
    check(both.ok(), "Vue_C et Vue_D ensemble : possible (" + joinAll(both.errors) + ")");
    // en deux operations de la meme proposition : chacune sait que l'autre part
    auto q = g.proposal({removal("vue", {"Vue_C"}), removal("vue", {"Vue_D"})}, "Vue_B");
    const ai::Check ckq = ai::validate(pr, q);
    check(ckq.ok(), "deux op\xC3\xA9rations, un cycle : valid\xC3\xA9" "es (" + joinAll(ckq.errors) + ")");
    check(g.apply(q, &why) && !pr.viewByName("Vue_C") && !pr.viewByName("Vue_D"), "les deux vues partent (" + why + ")");
}

// ---- 8. le plan apres Appliquer : la verification reelle ----------------------------------------------------------
void planStates() {
    std::printf("8 - le plan apr\xC3\xA8s Appliquer\n");
    Fixture f;
    const hmi::Project before = f.project();
    auto p = f.proposal({removal("variable", {"Libre_1"}), removal("vue", {"Vue_A"}, "inutiles")});
    const ai::Check ck = ai::validate(f.project(), p);
    plan::Plan pl = plan::build(f.project(), p, ck);
    check(pl.steps.size() == 2 && std::all_of(pl.steps.begin(), pl.steps.end(), [](const plan::Step& s) { return s.category == plan::Category::Removals; }),
          "deux \xC3\xA9tapes, cat\xC3\xA9gorie Suppressions");
    check(std::any_of(pl.steps.begin(), pl.steps.end(), [](const plan::Step& s) { return has(s.title, "Supprimer la vue Vue_A"); }), "le titre dit le genre");
    std::string why;
    check(f.apply(p, &why), "appliqu\xC3\xA9" "e (" + why + ")");
    plan::Plan applied = pl;
    const plan::Report rep = plan::report(before, f.project(), applied, p);
    check(rep.applied == 2 && rep.failed == 0, "les deux \xC3\xA9tapes : appliqu\xC3\xA9" "es (r\xC3\xA9" "ussies), lues dans le projet");
    check(!rep.removed.empty() && has(plan::reportText(rep, "x"), "Supprim\xC3\xA9"), "le compte rendu dit ce qui est parti");
    // un projet d'apres ou rien n'est parti : l'etape echoue, jamais un succes qui n'a pas eu lieu
    plan::Plan fake = pl;
    const plan::Report rep2 = plan::report(before, before, fake, p);
    check(rep2.applied == 0 && rep2.failed == 2, "rien n'est parti : les deux \xC3\xA9tapes \xC3\xA9" "chouent");
    check(std::all_of(fake.steps.begin(), fake.steps.end(), [](const plan::Step& s) { return has(s.note, "encore"); }), "la note dit ce qui reste");
    std::string note;
    check(!del::confirmRemoved(before, removal("variable", {"Libre_1"}), {}, &note) && has(note, "Libre_1"), "confirmRemoved : Libre_1 est encore l\xC3\xA0");
    check(del::confirmRemoved(f.project(), removal("variable", {"Libre_1"}), {}, &note), "confirmRemoved : partie, plus aucune r\xC3\xA9" "f\xC3\xA9rence");
}

// ---- 9. la correction : un symbole supprime (Vues) emporte ses popups -----------------------------------------------
void ownerSymbolFix() {
    std::printf("9 - supprimer un symbole emporte ses popups (View::ownerSymbol)\n");
    Fixture f;
    const std::string before = print(f.project());
    const hmi::View* sym = f.project().viewByName("Sym_Pompe");
    check(sym && f.project().viewByName("Pop_Sym") && f.project().viewByName("Pop_Sym")->ownerSymbol == sym->id, "Pop_Sym appartient \xC3\xA0 Sym_Pompe");
    auto cmd = app::hmiDeleteViewCommand(f.doc, sym ? sym->id : hmi::kNoId);
    check(cmd && static_cast<bool>(f.stack.push(std::move(cmd))), "la commande Supprimer la vue");
    check(!f.project().viewByName("Sym_Pompe") && !f.project().viewByName("Pop_Sym"), "le symbole ET sa popup sont partis");
    check(f.project().viewByName("Pop_Aide") && f.project().viewByName("Vue_A"), "les autres vues restent");
    check(static_cast<bool>(f.stack.undo()) && print(f.project()) == before, "Ctrl+Z : les deux reviennent");
}

} // namespace

int main() {
    typeWithUsers();
    instanceVsDefinition();
    viewSharedScript();
    objectsAndVariables();
    exclusionsAndAlarms();
    transaction();
    failuresAndCycles();
    planStates();
    ownerSymbolFix();
    std::printf("ai_delete_test : %d v\xC3\xA9rifications, %d \xC3\xA9" "checs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
