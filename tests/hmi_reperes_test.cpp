// =============================================================================
//  tests/hmi_reperes_test.cpp - 1.11.24 : LES REPERES ($...$) DANS LES SYMBOLES
// -----------------------------------------------------------------------------
//  La demande du client du 09/10 au soir (sa capture : « saisie : variable inconnue
//  (V[0]).IN_Percent », un champ de saisie dans un symbole) : des symboles et des objets
//  de la bibliotheque, chaque parametre et chaque option avec des reperes, tout ce
//  qu'un symbole peut heriter (instances imbriquees, popups du symbole, popups du
//  projet), et Dupliquer. Pour chaque cas : Compiler, Generer, et en marche (le
//  moteur : saisies, boutons, commandes, popups ; l'animation).
//
//  HMI_REPERES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiActionKinds.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiDuplicate.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiLive.hpp"
#include "../src/hmi/HmiMarkers.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiSymbols.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("HMI_REPERES_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}
void obs(const std::string& what) {
    if (g_obs) std::printf("  obs    %s\n", what.c_str());
}

// L'automate : des chemins exacts (comme le simulateur, on n'ecrit que ce qui existe).
class FakePlc final : public sim::Environment {
public:
    std::map<std::string, sim::Value> values;
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        it->second = v;
        return true;
    }
    bool exists(std::string_view n) override { return values.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

Variable hmiVar(Project& p, const char* name, const char* type, const char* initial = "") {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    return v;
}
Action act(Trigger t, Operation o, std::string target = {}, std::string value = {}) {
    Action a;
    a.trigger = t;
    a.operation = o;
    a.target = std::move(target);
    a.value = std::move(value);
    return a;
}
Object& add(Project& p, View& v, Kind k, const char* name, double x, double y) {
    const Id id = edit::add(p, v, k, x, y);
    Object* o = v.object(id);
    o->name = name;
    return *o;
}
const char* sev(Issue::Severity s) {
    return s == Issue::Severity::Error ? "ERREUR" : s == Issue::Severity::Warning ? "avert." : "info";
}

// Le nom d'un objet (d'une vue du projet) ; "?" : inconnu (un objet derive d'une instance).
std::string objectName(const Project& p, Id view, Id object) {
    if (object == kNoId) return "(la vue)";
    if (const View* v = p.view(view))
        if (const Object* o = v->object(object)) return o->name;
    for (const auto& v : p.views)
        if (const Object* o = v.object(object)) return v.name + "/" + o->name;
    return "?";
}

// ============================================================ le projet ====
//  T_Cuve : IN_Percent, Niveau (REAL), Marche (BOOL), Nom (STRING), Consigne (INT).
//  Variables IHM : V (ARRAY[0..3] OF T_Cuve), Cuve_A (T_Cuve), Idx (INT = 1).
//  Automate : W[0..1] (T_Cuve), ses membres.
//  S_Mini (M : T_Cuve) - un champ M.Consigne, un bouton +1, un texte.
//  S_Cuve (Cuve : T_Cuve ; Titre : STRING ; Haut : REAL ; Tab : ARRAY OF T_Cuve ; I : INT) :
//    chaque genre d'emploi d'un parametre - texte a trous, champ de saisie (bornes),
//    boutons (Incrementer, Basculer, Affecter, script), voyant, barre, interrupteur,
//    curseur, Tab[I], une popup du symbole, une popup du projet (C := Cuve), trois
//    instances de S_Mini (M := Cuve, M := $Cuve$, M := Tab[I]).
struct Built {
    Project p;
    FakePlc plc;
    Id view{kNoId}, sym{kNoId}, mini{kNoId}, ownPop{kNoId}, projPop{kNoId};
    std::map<std::string, Id> symObj, miniObj, ownObj, projObj;   // nom -> id dans sa vue
    std::map<std::string, Id> inst;                               // nom d'instance -> id
};

Built build() {
    Built b;
    Project& p = b.p;
    HmiType cuve;
    cuve.id = p.allocate();
    cuve.name = "T_Cuve";
    cuve.members = {{"IN_Percent", "REAL", "", ""}, {"Niveau", "REAL", "", ""}, {"Marche", "BOOL", "", ""},
                    {"Nom", "STRING", "", ""}, {"Consigne", "INT", "", ""}};
    p.programs.types.push_back(cuve);
    p.programs.variables.push_back(hmiVar(p, "V", "ARRAY[0..3] OF T_Cuve"));
    p.programs.variables.push_back(hmiVar(p, "Cuve_A", "T_Cuve"));
    p.programs.variables.push_back(hmiVar(p, "Idx", "INT", "1"));
    for (int k = 0; k < 2; ++k) {
        const std::string w = "W[" + std::to_string(k) + "].";
        b.plc.values[w + "IN_Percent"] = sim::Value::real(0.0);
        b.plc.values[w + "Niveau"] = sim::Value::real(0.0);
        b.plc.values[w + "Marche"] = sim::Value::boolean(false);
        b.plc.values[w + "Nom"] = sim::Value::text("W" + std::to_string(k));
        b.plc.values[w + "Consigne"] = sim::Value::integer(sim::Type::Int, 0);
    }
    // ---- S_Mini
    View mini = makeView(p, "S_Mini");
    mini.role = "symbole";
    mini.width = 200;
    mini.height = 80;
    mini.params.push_back({"M", "", "", "T_Cuve", ParamMode::Reference});
    {
        auto& f = add(p, mini, Kind::InputField, "Saisie_M", 0, 0);
        f.set("variable", "M.Consigne");
        f.set("mode", "numerique");
        b.miniObj["Saisie_M"] = f.id;
        auto& bt = add(p, mini, Kind::Button, "Btn_M", 100, 0);
        bt.actions.push_back(act(Trigger::Click, Operation::Increment, "M.Consigne", "1"));
        b.miniObj["Btn_M"] = bt.id;
        auto& t = add(p, mini, Kind::Text, "Txt_M", 0, 40);
        t.set("text", "{M.Nom} : {M.Consigne}");
        b.miniObj["Txt_M"] = t.id;
    }
    b.mini = mini.id;
    p.views.push_back(mini);
    // ---- S_Cuve
    View sym = makeView(p, "S_Cuve");
    sym.role = "symbole";
    sym.width = 600;
    sym.height = 400;
    sym.params.push_back({"Cuve", "", "", "T_Cuve", ParamMode::Reference});
    sym.params.push_back({"Titre", "'Cuve'", "", "STRING", ParamMode::Reference});
    sym.params.push_back({"Haut", "100.0", "", "REAL", ParamMode::Reference});
    sym.params.push_back({"Tab", "", "", "ARRAY[0..3] OF T_Cuve", ParamMode::Reference});
    sym.params.push_back({"I", "0", "", "INT", ParamMode::Reference});
    const auto keep = [&](Object& o) { b.symObj[o.name] = o.id; };
    {
        auto& t = add(p, sym, Kind::Text, "Txt", 0, 0);
        t.set("text", "{Titre} : {Cuve.IN_Percent:0.0} % / {Cuve.Consigne}");
        keep(t);
        auto& f = add(p, sym, Kind::InputField, "Saisie", 0, 40);
        f.set("variable", "Cuve.IN_Percent");
        f.set("mode", "numerique");
        f.set("min", "0");
        f.set("max", "Haut");
        keep(f);
        auto& plus = add(p, sym, Kind::Button, "Btn_Plus", 200, 0);
        plus.actions.push_back(act(Trigger::Click, Operation::Increment, "Cuve.Consigne", "1"));
        keep(plus);
        auto& bas = add(p, sym, Kind::Button, "Btn_Bascule", 200, 40);
        bas.actions.push_back(act(Trigger::Click, Operation::Toggle, "Cuve.Marche"));
        keep(bas);
        auto& aff = add(p, sym, Kind::Button, "Btn_Affecte", 200, 80);
        aff.actions.push_back(act(Trigger::Click, Operation::Assign, "Cuve.Niveau", "Cuve.IN_Percent * 2.0"));
        keep(aff);
        auto& scr = add(p, sym, Kind::Button, "Btn_Script", 200, 120);
        scr.actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Cuve.Consigne := Cuve.Consigne + 10;"));
        keep(scr);
        auto& voy = add(p, sym, Kind::Indicator, "Voyant", 400, 0);
        voy.setExpr("value", "Cuve.Marche");
        keep(voy);
        auto& barre = add(p, sym, Kind::ProgressBar, "Barre", 400, 40);
        barre.setExpr("value", "Cuve.Niveau");
        barre.setExpr("max", "Haut");
        keep(barre);
        auto& inter = add(p, sym, Kind::Switch, "Inter", 400, 120);
        inter.set("variable", "Cuve.Marche");
        inter.set("state", "Cuve.Marche");
        keep(inter);
        auto& cur = add(p, sym, Kind::Slider, "Curseur", 400, 180);
        cur.set("variable", "Cuve.Niveau");
        cur.set("state", "Cuve.Niveau");
        keep(cur);
        auto& tt = add(p, sym, Kind::Text, "Txt_Tab", 0, 120);
        tt.set("text", "{Tab[I].Nom} {Tab[I].Consigne}");
        keep(tt);
        auto& ft = add(p, sym, Kind::InputField, "Saisie_Tab", 0, 160);
        ft.set("variable", "Tab[I].Consigne");
        ft.set("mode", "numerique");
        keep(ft);
        auto& bp = add(p, sym, Kind::Button, "Btn_Pop", 200, 200);
        bp.actions.push_back(act(Trigger::Click, Operation::Popup, "Pop_Cuve"));
        keep(bp);
        auto& bd = add(p, sym, Kind::Button, "Btn_Detail", 200, 240);
        bd.actions.push_back(act(Trigger::Click, Operation::Popup, "Pop_Detail", "C := Cuve"));
        keep(bd);
    }
    b.sym = sym.id;
    p.views.push_back(sym);
    // Les instances de S_Mini dans S_Cuve (l'heritage) : M := Cuve, M := $Cuve$, M := Tab[I].
    {
        View& s = *p.viewByName("S_Cuve");
        const auto place = [&](const char* name, const char* params, double y) {
            const Id id = placeSymbol(p, s, "S_Mini", 0, y);
            s.object(id)->name = name;
            s.object(id)->set("params", params);
            b.symObj[name] = id;
        };
        place("Mini", "M := Cuve", 240);
        place("Mini_Rep", "M := $Cuve$", 300);
        place("Mini_Tab", "M := Tab[I]", 340);
    }
    // ---- la popup du symbole (elle connait ses parametres)
    View own = makeView(p, "Pop_Cuve");
    own.role = "popup";
    own.ownerSymbol = b.sym;
    {
        auto& f = add(p, own, Kind::InputField, "Saisie_P", 0, 0);
        f.set("variable", "Cuve.Consigne");
        f.set("mode", "numerique");
        b.ownObj["Saisie_P"] = f.id;
        auto& t = add(p, own, Kind::Text, "Txt_P", 0, 40);
        t.set("text", "{Titre} {Cuve.Nom}");
        b.ownObj["Txt_P"] = t.id;
        auto& bt = add(p, own, Kind::Button, "Btn_P", 0, 80);
        bt.actions.push_back(act(Trigger::Click, Operation::Increment, "Cuve.Consigne", "100"));
        b.ownObj["Btn_P"] = bt.id;
    }
    b.ownPop = own.id;
    p.views.push_back(own);
    // ---- une popup du projet (C : T_Cuve, Reference)
    View det = makeView(p, "Pop_Detail");
    det.role = "popup";
    det.params.push_back({"C", "", "", "T_Cuve", ParamMode::Reference});
    {
        auto& f = add(p, det, Kind::InputField, "Saisie_C", 0, 0);
        f.set("variable", "C.Niveau");
        f.set("mode", "numerique");
        b.projObj["Saisie_C"] = f.id;
        auto& bt = add(p, det, Kind::Button, "Btn_C", 0, 40);
        bt.actions.push_back(act(Trigger::Click, Operation::Increment, "C.Consigne", "1000"));
        b.projObj["Btn_C"] = bt.id;
    }
    b.projPop = det.id;
    p.views.push_back(det);
    // ---- la vue : des instances, chacune un autre argument a repere
    View v = makeView(p, "Vue_Test");
    v.width = 2000;
    v.height = 2000;
    b.view = v.id;
    p.views.push_back(v);
    View& vt = *p.viewByName("Vue_Test");
    const auto inst = [&](const char* name, const char* params, double x, double y) {
        const Id id = placeSymbol(p, vt, "S_Cuve", x, y);
        vt.object(id)->name = name;
        vt.object(id)->set("params", params);
        b.inst[name] = id;
    };
    inst("Sym_0", "Cuve := $V[0]$; Titre := '$Nom$'; Haut := 80.0; Tab := V; I := 0", 0, 0);
    inst("Sym_1", "Cuve := V[1]; Tab := $V$; I := $Idx$", 700, 0);
    inst("Sym_2", "Cuve := $V$[2]; Tab := V; I := 2", 0, 500);
    inst("Sym_3", "$V[3]$; 'C3'; 90.0; V; 3", 700, 500);
    inst("Sym_A", "Cuve := $Cuve_A$; Tab := V; I := 0", 0, 1000);
    inst("Sym_W", "Cuve := $W[0]$; Tab := V; I := 0", 700, 1000);
    inst("Sym_X", "Cuve := V[$Idx$]; Tab := V; I := 0", 0, 1500);
    p.config.startView = b.view;
    return b;
}

// La valeur d'un chemin (IHM ou automate), ecrite ; "#" : illisible.
std::string valueOf(Runtime& rt, std::string_view path) {
    sim::Value x;
    if (!rt.environment().read(path, x)) return "#";
    return x.type() == sim::Type::String ? x.asString() : x.display();
}

// Une valeur ecrite egale a un nombre ("12" ou "12.0").
bool sameNumber(const std::string& got, double want) {
    double x = 0;
    return parseNumber(got, x) && std::fabs(x - want) < 1e-9;
}

// Les erreurs du journal depuis `from` (leur nombre), ecrites.
std::size_t journalErrors(const Runtime& rt, std::size_t from, const std::string& label) {
    std::size_t n = 0, k = 0;
    for (const auto& e : rt.journal()) {
        if (k++ < from) continue;
        if (e.kind == "Erreur" || e.kind == "Script") {
            ++n;
            std::printf("  journal [%s] %s | %s | %s\n", label.c_str(), e.kind.c_str(), e.source.c_str(), e.message.c_str());
        }
    }
    return n;
}

// ===================================================== 1. Compiler, Generer ==
void compilerGenerer(const Built& b, const std::string& label) {
    std::printf("-- %s : Compiler et Generer\n", label.c_str());
    const auto plc = [](std::string_view root) { return root == "W"; };
    std::size_t errors = 0;
    for (const auto& i : compileWith(b.p, plc)) {
        if (i.severity == Issue::Severity::Info) continue;
        if (i.severity == Issue::Severity::Error) ++errors;
        std::printf("  compiler %s [%s] %s/%s (%s) : %s\n", sev(i.severity), i.category.c_str(),
                    b.p.view(i.view) ? b.p.view(i.view)->name.c_str() : "-", objectName(b.p, i.view, i.object).c_str(),
                    i.property.c_str(), i.message.c_str());
    }
    check(errors == 0, label + " : Compiler sans erreur");
    errors = 0;
    for (const auto& i : generate(b.p, plc)) {
        if (i.severity == Issue::Severity::Info) continue;
        if (i.severity == Issue::Severity::Error) ++errors;
        std::printf("  generer %s [%s] %s/%s (%s) : %s\n", sev(i.severity), i.category.c_str(),
                    b.p.view(i.view) ? b.p.view(i.view)->name.c_str() : "-", objectName(b.p, i.view, i.object).c_str(),
                    i.property.c_str(), i.message.c_str());
    }
    check(errors == 0, label + " : Generer sans erreur");
}

// =========================================================== 2. l'animation ==
void animation(Built& b, Runtime& rt, const std::string& label) {
    std::printf("-- %s : l'animation de la vue\n", label.c_str());
    const View* v = b.p.view(b.view);
    LiveView live;
    live.bind(expandInstances(b.p, *v));
    std::vector<LiveValue> values;
    (void)live.evaluate(rt.environment(), &values);
    std::size_t errors = 0;
    for (const auto& lv : values)
        if (lv.error) {
            ++errors;
            std::printf("  anim. ERREUR %s.%s = %s (%s)\n", objectName(b.p, b.view, lv.object).c_str(), lv.key.c_str(),
                        lv.expression.c_str(), lv.value.c_str());
        }
    check(errors == 0, label + " : chaque expression de la vue se calcule");
}

// ============================================================== 3. en marche ==
void enMarche(Built& b, const std::string& label, const std::map<std::string, std::string>& cuveOf) {
    std::printf("-- %s : en marche\n", label.c_str());
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const auto step = [&] { t += 0.1; rt.tick(t); };
    animation(b, rt, label);
    for (const auto& [instName, cuve] : cuveOf) {
        const auto it = b.inst.find(instName);
        if (it == b.inst.end()) continue;
        const Id inst = it->second;
        const auto ex = [&](const char* child) { return expandedId(inst, b.symObj.at(child)); };
        const std::string who = label + " " + instName + " (" + cuve + ")";
        std::size_t from = rt.journal().size();
        // a. le champ de saisie
        rt.objectPart(ex("Saisie"), "champ", t);
        rt.typeText("42", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(sameNumber(valueOf(rt, cuve + ".IN_Percent"), 42),
              who + " : la saisie ecrit " + cuve + ".IN_Percent (" + valueOf(rt, cuve + ".IN_Percent") + ")");
        // la borne max := Haut (un parametre du symbole ; 80 pour Sym_0, 90 pour Sym_3, 100 sinon)
        {
            const double haut = instName == "Sym_0" ? 80.0 : instName == "Sym_3" ? 90.0 : 100.0;
            rt.objectPart(ex("Saisie"), "champ", t);
            rt.typeText(formatNumber(haut + 5.0), t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(sameNumber(valueOf(rt, cuve + ".IN_Percent"), 42),
                  who + " : max := Haut (" + formatNumber(haut) + ") refuse " + formatNumber(haut + 5.0) + " (" + valueOf(rt, cuve + ".IN_Percent") + ")");
            rt.typeKey(EditKey::Escape, t);
            step();
        }
        // b. les boutons
        const std::string c0 = valueOf(rt, cuve + ".Consigne");
        rt.press(ex("Btn_Plus"), t);
        rt.release(ex("Btn_Plus"), t, true);
        step();
        check(valueOf(rt, cuve + ".Consigne") == std::to_string(std::atoi(c0.c_str()) + 1),
              who + " : Incrementer " + cuve + ".Consigne (" + c0 + " -> " + valueOf(rt, cuve + ".Consigne") + ")");
        const std::string m0 = valueOf(rt, cuve + ".Marche");
        rt.press(ex("Btn_Bascule"), t);
        rt.release(ex("Btn_Bascule"), t, true);
        step();
        check(valueOf(rt, cuve + ".Marche") != m0, who + " : Basculer " + cuve + ".Marche (" + m0 + " -> " + valueOf(rt, cuve + ".Marche") + ")");
        rt.press(ex("Btn_Affecte"), t);
        rt.release(ex("Btn_Affecte"), t, true);
        step();
        check(valueOf(rt, cuve + ".Niveau") == "84.0" || valueOf(rt, cuve + ".Niveau") == "84",
              who + " : Affecter " + cuve + ".Niveau := IN_Percent * 2 (" + valueOf(rt, cuve + ".Niveau") + ")");
        const std::string c1 = valueOf(rt, cuve + ".Consigne");
        rt.press(ex("Btn_Script"), t);
        rt.release(ex("Btn_Script"), t, true);
        step();
        check(valueOf(rt, cuve + ".Consigne") == std::to_string(std::atoi(c1.c_str()) + 10),
              who + " : le script ecrit " + cuve + ".Consigne (" + c1 + " -> " + valueOf(rt, cuve + ".Consigne") + ")");
        // c. les commandes : l'interrupteur
        const std::string m1 = valueOf(rt, cuve + ".Marche");
        rt.press(ex("Inter"), t);
        rt.release(ex("Inter"), t, true);
        step();
        check(valueOf(rt, cuve + ".Marche") != m1, who + " : l'interrupteur bascule " + cuve + ".Marche (" + m1 + " -> " + valueOf(rt, cuve + ".Marche") + ")");
        // d. les instances imbriquees : M := Cuve, M := $Cuve$
        for (const char* m : {"Mini", "Mini_Rep"}) {
            const Id saisie = expandedId(ex(m), b.miniObj.at("Saisie_M"));
            rt.objectPart(saisie, "champ", t);
            rt.typeText("7", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "7",
                  who + " : " + m + " (S_Mini dans S_Cuve) ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            const Id btn = expandedId(ex(m), b.miniObj.at("Btn_M"));
            rt.press(btn, t);
            rt.release(btn, t, true);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "8", who + " : " + m + ".Btn_M incremente " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
        }
        // e. la popup du symbole
        rt.press(ex("Btn_Pop"), t);
        rt.release(ex("Btn_Pop"), t, true);
        step();
        check(!rt.popups().empty() && rt.popups().back() == b.ownPop, who + " : la popup du symbole s'ouvre");
        if (!rt.popups().empty()) {
            rt.objectPart(b.ownObj.at("Saisie_P"), "champ", t);
            rt.typeText("55", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "55", who + " : la popup du symbole ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            rt.press(b.ownObj.at("Btn_P"), t);
            rt.release(b.ownObj.at("Btn_P"), t, true);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "155", who + " : son bouton incremente " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            (void)rt.closePopup(Transition{}, t);
            step();
        }
        // f. la popup du projet (C := Cuve)
        rt.press(ex("Btn_Detail"), t);
        rt.release(ex("Btn_Detail"), t, true);
        step();
        check(!rt.popups().empty() && rt.popups().back() == b.projPop, who + " : la popup du projet s'ouvre (C := Cuve)");
        if (!rt.popups().empty()) {
            rt.objectPart(b.projObj.at("Saisie_C"), "champ", t);
            rt.typeText("9", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(valueOf(rt, cuve + ".Niveau") == "9.0" || valueOf(rt, cuve + ".Niveau") == "9",
                  who + " : la popup du projet ecrit " + cuve + ".Niveau (" + valueOf(rt, cuve + ".Niveau") + ")");
            rt.press(b.projObj.at("Btn_C"), t);
            rt.release(b.projObj.at("Btn_C"), t, true);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "1155", who + " : son bouton incremente " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            (void)rt.closePopup(Transition{}, t);
            step();
        }
        check(journalErrors(rt, from, who) == 0, who + " : aucune erreur au journal");
    }
}


// ============================================ 4. des reperes DANS le symbole ==
//  S_Rep : ses objets ecrivent leurs parametres entre $ ($Cuve$.IN_Percent, $Cuve.Consigne$,
//  Tab[$I$], {$Cuve.Nom$}), une variable globale entre $ ($Idx$) ; ses valeurs par defaut
//  ont des reperes (Cuve := $V[2]$). Trois instances : des arguments simples, des arguments
//  a reperes, aucun argument (les valeurs par defaut).
void reperesDansLeSymbole() {
    std::printf("-- reperes dans le symbole, valeurs par defaut a repere\n");
    Built b = build();
    Project& p = b.p;
    View rep = makeView(p, "S_Rep");
    rep.role = "symbole";
    rep.width = 400;
    rep.height = 300;
    rep.params.push_back({"Cuve", "$V[2]$", "", "T_Cuve", ParamMode::Reference});
    rep.params.push_back({"Tab", "V", "", "ARRAY[0..3] OF T_Cuve", ParamMode::Reference});
    rep.params.push_back({"I", "2", "", "INT", ParamMode::Reference});
    std::map<std::string, Id> ids;
    {
        auto& f = add(p, rep, Kind::InputField, "Saisie", 0, 0);
        f.set("variable", "$Cuve$.IN_Percent");
        f.set("mode", "numerique");
        ids["Saisie"] = f.id;
        auto& bt = add(p, rep, Kind::Button, "Btn_Plus", 100, 0);
        bt.actions.push_back(act(Trigger::Click, Operation::Increment, "$Cuve.Consigne$", "1"));
        ids["Btn_Plus"] = bt.id;
        auto& t = add(p, rep, Kind::Text, "Txt", 0, 40);
        t.set("text", "{$Cuve.Nom$} : {$Cuve$.Consigne} / {Tab[$I$].Consigne}");
        ids["Txt"] = t.id;
        auto& ft = add(p, rep, Kind::InputField, "Saisie_Tab", 0, 80);
        ft.set("variable", "Tab[$I$].Consigne");
        ft.set("mode", "numerique");
        ids["Saisie_Tab"] = ft.id;
        auto& sc = add(p, rep, Kind::Button, "Btn_Script", 100, 80);
        sc.actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "$Cuve$.Niveau := $Cuve$.Niveau + 1.0;"));
        ids["Btn_Script"] = sc.id;
        auto& voy = add(p, rep, Kind::Indicator, "Voyant", 200, 0);
        voy.setExpr("value", "$Cuve$.Marche OR $Idx$ > 5");
        ids["Voyant"] = voy.id;
        auto& tg = add(p, rep, Kind::Button, "Btn_Bascule", 200, 80);
        tg.actions.push_back(act(Trigger::Click, Operation::Toggle, "=$Cuve$.Marche"));
        ids["Btn_Bascule"] = tg.id;
    }
    p.views.push_back(rep);
    View& vt = *p.viewByName("Vue_Test");
    const auto inst = [&](const char* name, const char* params, double y) {
        const Id id = placeSymbol(p, vt, "S_Rep", 1400, y);
        vt.object(id)->name = name;
        vt.object(id)->set("params", params);
        return id;
    };
    const Id r0 = inst("Rep_0", "Cuve := V[0]; Tab := V; I := 1", 0);
    const Id r1 = inst("Rep_1", "Cuve := $V[1]$; Tab := $V$; I := $Idx$", 400);
    const Id rd = inst("Rep_D", "", 800);
    compilerGenerer(b, "reperes dans le symbole");
    const std::vector<std::tuple<std::string, Id, std::string, std::string>> cases = {
        {"Rep_0", r0, "V[0]", "V[1]"}, {"Rep_1", r1, "V[1]", "V[1]"}, {"Rep_D (defauts)", rd, "V[2]", "V[2]"}};
    for (const auto& [name, id, cuve, tab] : cases) {
        Built one = b;            // une copie : chaque instance son moteur
        Runtime rt;
        rt.bind(&one.p, &one.plc);
        rt.start(0.0);
        rt.tick(0.0);
        double t = 0.1;
        const auto step = [&] { t += 0.1; rt.tick(t); };
        const auto ex = [&](const char* child) { return expandedId(id, ids.at(child)); };
        const std::size_t from = rt.journal().size();
        const std::string who = "S_Rep " + name;
        rt.objectPart(ex("Saisie"), "champ", t);
        rt.typeText("12", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(sameNumber(valueOf(rt, cuve + ".IN_Percent"), 12), who + " : $Cuve$.IN_Percent -> " + cuve + ".IN_Percent (" + valueOf(rt, cuve + ".IN_Percent") + ")");
        rt.press(ex("Btn_Plus"), t);
        rt.release(ex("Btn_Plus"), t, true);
        step();
        check(valueOf(rt, cuve + ".Consigne") == "1", who + " : $Cuve.Consigne$ incremente (" + valueOf(rt, cuve + ".Consigne") + ")");
        rt.objectPart(ex("Saisie_Tab"), "champ", t);
        rt.typeText("30", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(valueOf(rt, tab + ".Consigne") == "30", who + " : Tab[$I$].Consigne -> " + tab + ".Consigne (" + valueOf(rt, tab + ".Consigne") + ")");
        rt.press(ex("Btn_Script"), t);
        rt.release(ex("Btn_Script"), t, true);
        step();
        check(sameNumber(valueOf(rt, cuve + ".Niveau"), 1), who + " : le script $Cuve$.Niveau := ... (" + valueOf(rt, cuve + ".Niveau") + ")");
        rt.press(ex("Btn_Bascule"), t);
        rt.release(ex("Btn_Bascule"), t, true);
        step();
        check(valueOf(rt, cuve + ".Marche") == "TRUE", who + " : Basculer =$Cuve$.Marche (" + valueOf(rt, cuve + ".Marche") + ")");
        check(journalErrors(rt, from, who) == 0, who + " : aucune erreur au journal");
        animation(one, rt, who);
    }
}

// =================================== 5. alarmes, fonctions, scripts du symbole ==
void alarmesFonctionsScripts() {
    std::printf("-- les alarmes, les fonctions et les scripts du symbole, avec des arguments a reperes\n");
    Built b = build();
    Project& p = b.p;
    View& sym = *p.viewByName("S_Cuve");
    AlarmDef al;
    al.id = p.allocate();
    al.name = "Trop_Haut";
    al.condition = "Cuve.Niveau > Haut";
    al.message = "{Titre} : {Cuve.Niveau:0.0} > {Haut:0.0}";
    al.priority = 2;
    sym.alarms.push_back(al);
    HmiFunction aj;
    aj.id = p.allocate();
    aj.name = "Ajouter";
    aj.body = "Cuve.Consigne := Cuve.Consigne + 1;";
    sym.functions.push_back(aj);
    HmiFunction et;
    et.id = p.allocate();
    et.name = "Etat";
    et.returnType = "INT";
    et.body = "Etat := Cuve.Consigne * 2;";
    sym.functions.push_back(et);
    {
        auto& bt = add(p, sym, Kind::Button, "Btn_Fonction", 400, 300);
        bt.actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Ajouter();"));
        b.symObj["Btn_Fonction"] = bt.id;
        auto& tx = add(p, sym, Kind::Text, "Txt_Etat", 400, 340);
        tx.set("text", "Etat = {Etat()}");
        b.symObj["Txt_Etat"] = tx.id;
    }
    Script cyc;
    cyc.id = p.allocate();
    cyc.name = "Cycle_Cuve";
    cyc.event = "OnCycle";
    cyc.body = "IF Cuve.Marche THEN Cuve.Niveau := Cuve.Niveau + 0.5; END_IF;";
    sym.scripts.push_back(cyc);
    View& vt = *p.viewByName("Vue_Test");
    {
        auto& bv = add(p, vt, Kind::Button, "Btn_Vue", 1600, 1600);
        bv.actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Sym_0.Ajouter();\nSym_W.Ajouter();"));
        b.symObj["Btn_Vue"] = bv.id;
    }
    compilerGenerer(b, "alarmes, fonctions, scripts");
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const auto step = [&] { t += 0.1; rt.tick(t); };
    const std::size_t from = rt.journal().size();
    for (const auto& [name, cuve] : std::vector<std::pair<std::string, std::string>>{{"Sym_0", "V[0]"}, {"Sym_2", "V[2]"}, {"Sym_W", "W[0]"}}) {
        const Id inst = b.inst.at(name);
        const std::string who = "alarmes/fonctions " + name;
        rt.press(expandedId(inst, b.symObj.at("Btn_Fonction")), t);
        rt.release(expandedId(inst, b.symObj.at("Btn_Fonction")), t, true);
        step();
        check(valueOf(rt, cuve + ".Consigne") == "1", who + " : Ajouter() du symbole ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
        sim::Value high = sim::Value::real(500.0);
        (void)rt.environment().write(cuve + ".Niveau", high);
        step();
        step();
        bool active = false;
        std::string message;
        for (const auto& la : rt.alarms())
            if (la.active && la.name.find(name + ".Trop_Haut") != std::string::npos) {
                active = true;
                message = la.message;
            }
        if (!active)
            for (const auto& la : rt.alarms())
                obs("alarme : " + la.name + " | groupe " + la.group + " | " + (la.active ? "active" : "inactive") + " | " + la.message);
        check(active, who + " : l'alarme du symbole (Cuve.Niveau > Haut) apparait (" + message + ")");
        // le script OnCycle du symbole : Marche -> le niveau monte
        sim::Value on = sim::Value::boolean(true);
        (void)rt.environment().write(cuve + ".Marche", on);
        const std::string n0 = valueOf(rt, cuve + ".Niveau");
        step();
        step();
        check(valueOf(rt, cuve + ".Niveau") != n0, who + " : le script OnCycle du symbole ecrit " + cuve + ".Niveau (" + n0 + " -> " + valueOf(rt, cuve + ".Niveau") + ")");
    }
    rt.press(b.symObj.at("Btn_Vue"), t);
    rt.release(b.symObj.at("Btn_Vue"), t, true);
    step();
    check(valueOf(rt, "V[0].Consigne") == "2" && valueOf(rt, "W[0].Consigne") == "2",
          "la vue appelle Sym_0.Ajouter() et Sym_W.Ajouter() (" + valueOf(rt, "V[0].Consigne") + ", " + valueOf(rt, "W[0].Consigne") + ")");
    check(journalErrors(rt, from, "alarmes/fonctions") == 0, "alarmes, fonctions, scripts : aucune erreur au journal");
    animation(b, rt, "alarmes, fonctions, scripts");
}

// =========================================== 6. deux niveaux d'imbrication ==
//  S_Ligne (L : ARRAY OF T_Cuve ; K : INT) pose deux S_Cuve : Cuve := L[K], Cuve := $L[0]$.
void deuxNiveaux() {
    std::printf("-- deux niveaux : S_Ligne pose S_Cuve\n");
    Built b = build();
    Project& p = b.p;
    View li = makeView(p, "S_Ligne");
    li.role = "symbole";
    li.width = 1300;
    li.height = 500;
    li.params.push_back({"L", "", "", "ARRAY[0..3] OF T_Cuve", ParamMode::Reference});
    li.params.push_back({"K", "0", "", "INT", ParamMode::Reference});
    p.views.push_back(li);
    View& l = *p.viewByName("S_Ligne");
    const Id c1 = placeSymbol(p, l, "S_Cuve", 0, 0);
    l.object(c1)->name = "C1";
    l.object(c1)->set("params", "Cuve := L[K]; Tab := L; I := K");
    const Id c2 = placeSymbol(p, l, "S_Cuve", 650, 0);
    l.object(c2)->name = "C2";
    l.object(c2)->set("params", "Cuve := $L[0]$; Tab := $L$; I := 0");
    View& vt = *p.viewByName("Vue_Test");
    const Id l1 = placeSymbol(p, vt, "S_Ligne", 0, 2100);
    vt.object(l1)->name = "Ligne_1";
    vt.object(l1)->set("params", "L := $V$; K := $Idx$");
    compilerGenerer(b, "deux niveaux");
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const auto step = [&] { t += 0.1; rt.tick(t); };
    const std::size_t from = rt.journal().size();
    for (const auto& [cid, cuve] : std::vector<std::pair<Id, std::string>>{{c1, "V[1]"}, {c2, "V[0]"}}) {
        const Id inner = expandedId(l1, cid);
        const std::string who = std::string("deux niveaux ") + (cid == c1 ? "C1 (L[K])" : "C2 ($L[0]$)");
        rt.objectPart(expandedId(inner, b.symObj.at("Saisie")), "champ", t);
        rt.typeText("21", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(sameNumber(valueOf(rt, cuve + ".IN_Percent"), 21), who + " : la saisie ecrit " + cuve + ".IN_Percent (" + valueOf(rt, cuve + ".IN_Percent") + ")");
        rt.press(expandedId(inner, b.symObj.at("Btn_Plus")), t);
        rt.release(expandedId(inner, b.symObj.at("Btn_Plus")), t, true);
        step();
        check(valueOf(rt, cuve + ".Consigne") == "1", who + " : Incrementer " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
        const Id mini = expandedId(inner, b.symObj.at("Mini_Rep"));
        rt.objectPart(expandedId(mini, b.miniObj.at("Saisie_M")), "champ", t);
        rt.typeText("4", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(valueOf(rt, cuve + ".Consigne") == "4", who + " : S_Mini (M := $Cuve$) au 3e niveau ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
        rt.press(expandedId(inner, b.symObj.at("Btn_Pop")), t);
        rt.release(expandedId(inner, b.symObj.at("Btn_Pop")), t, true);
        step();
        if (!rt.popups().empty()) {
            rt.objectPart(b.ownObj.at("Saisie_P"), "champ", t);
            rt.typeText("66", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(valueOf(rt, cuve + ".Consigne") == "66", who + " : la popup du symbole, ouverte au 2e niveau, ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            (void)rt.closePopup(Transition{}, t);
            step();
        } else {
            check(false, who + " : la popup du symbole s'ouvre au 2e niveau");
        }
    }
    check(journalErrors(rt, from, "deux niveaux") == 0, "deux niveaux : aucune erreur au journal");
    animation(b, rt, "deux niveaux");
}

// ============================================================= 7. Dupliquer ==
void dupliquer() {
    std::printf("-- Dupliquer une instance a reperes (Cuve := $V[0]$ ; Titre := '$Nom$')\n");
    using namespace hmi::dup;
    Built b = build();
    View& vt = *b.p.viewByName("Vue_Test");
    const Id sym0 = b.inst.at("Sym_0");
    const Scan sc = scan(vt, {sym0}, projectBounds(b.p));
    std::string found;
    for (const auto& m : sc.markers) found += (found.empty() ? "" : ", ") + m.name + " x" + std::to_string(m.uses);
    obs("reperes trouves : " + found);
    check(sc.marker("V[0]") != nullptr, "Dupliquer trouve le repere $V[0]$ dans les arguments (" + found + ")");
    check(sc.marker("Nom") != nullptr, "Dupliquer trouve le repere '$Nom$' (une constante texte) dans les arguments (" + found + ")");
    Plan plan = defaultPlan(vt, sc, 2);
    for (int k = 1; k <= 2; ++k) {
        plan.rows[static_cast<std::size_t>(k)].markers[markerKey("V[0]")] = "V[" + std::to_string(k) + "]";
        plan.rows[static_cast<std::size_t>(k)].markers[markerKey("Nom")] = "Cuve_" + std::to_string(k);
    }
    plan.replaceOriginal = false;
    const Result r = apply(b.p, vt, plan);
    check(r.created.size() == 2, "deux copies (" + std::to_string(r.created.size()) + ")");
    std::vector<std::pair<Id, std::string>> copies;
    for (std::size_t k = 0; k < r.created.size(); ++k) {
        const Object* o = vt.object(r.created[k]);
        const std::string params = o ? o->text("params") : std::string{};
        obs("copie " + std::to_string(k + 1) + " : " + (o ? o->name : std::string("?")) + " params = " + params);
        check(params.find("$V[" + std::to_string(k + 1) + "]$") != std::string::npos,
              "la copie " + std::to_string(k + 1) + " recoit $V[" + std::to_string(k + 1) + "]$ (" + params + ")");
        copies.emplace_back(r.created[k], "V[" + std::to_string(k + 1) + "]");
    }
    compilerGenerer(b, "apres Dupliquer");
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const auto step = [&] { t += 0.1; rt.tick(t); };
    const std::size_t from = rt.journal().size();
    for (const auto& [id, cuve] : copies) {
        rt.objectPart(expandedId(id, b.symObj.at("Saisie")), "champ", t);
        rt.typeText("33", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(sameNumber(valueOf(rt, cuve + ".IN_Percent"), 33), "la copie ecrit " + cuve + ".IN_Percent (" + valueOf(rt, cuve + ".IN_Percent") + ")");
        rt.objectPart(expandedId(expandedId(id, b.symObj.at("Mini_Rep")), b.miniObj.at("Saisie_M")), "champ", t);
        rt.typeText("5", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(valueOf(rt, cuve + ".Consigne") == "5", "la copie : S_Mini (M := $Cuve$) ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
    }
    check(journalErrors(rt, from, "apres Dupliquer") == 0, "apres Dupliquer : aucune erreur au journal");
    animation(b, rt, "apres Dupliquer");
}

// ================================== 8. la bibliotheque, propriete par propriete ==
//  Chaque objet posable, chaque propriete : une formule a repere, et la meme sans repere.
//  Ce que Compiler, Generer et l'animation disent EN PLUS avec les reperes est une faute.
//  Deux fois : dans une vue (Reels[1], Flags[0]) et dans un symbole (ses parametres P, B
//  recoivent $Reels[1]$ et $Flags[0]$).
std::string formulaFor(const Prop& prop, bool markers, bool inSymbol) {
    const std::string r = inSymbol ? (markers ? "$P$" : "P") : (markers ? "$Reels[1]$" : "Reels[1]");
    const std::string f = inSymbol ? (markers ? "$B$" : "B") : (markers ? "$Flags[0]$" : "Flags[0]");
    const std::string& v = prop.value;
    double n = 0;
    if (v == "TRUE" || v == "FALSE") return f;
    if (!v.empty() && v.front() == '#' && (v.size() == 7 || v.size() == 9)) return "SEL(" + f + ", '#FF0000', '#00FF00')";
    if (!v.empty() && parseNumber(v, n)) return r + " + " + formatNumber(n);
    return {};
}
std::set<std::string> issuesOf(const Project& p) {
    std::set<std::string> out;
    const auto plc = [](std::string_view) { return false; };
    const auto bare = [](std::string m) {
        std::string o;
        for (const char c : m) if (c != '$') o += c;
        return o;
    };
    for (const auto& i : compileWith(p, plc))
        if (i.severity != Issue::Severity::Info) out.insert("compiler " + objectName(p, i.view, i.object) + " (" + i.property + ") : " + bare(i.message));
    for (const auto& i : generate(p, plc))
        if (i.severity != Issue::Severity::Info) out.insert("generer " + objectName(p, i.view, i.object) + " (" + i.property + ") : " + bare(i.message));
    return out;
}
std::set<std::string> animationErrorsOf(const Project& p, Id view) {
    std::set<std::string> out;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.0);
    LiveView live;
    live.bind(expandInstances(p, *p.view(view)));
    std::vector<LiveValue> values;
    (void)live.evaluate(rt.environment(), &values);
    for (const auto& lv : values)
        if (lv.error) {
            std::string e = lv.value;
            std::string bare;
            for (const char c : e) if (c != '$') bare += c;
            out.insert(objectName(p, view, lv.object) + "." + lv.key + " : " + bare);
        }
    return out;
}
Project bibliotheque(bool markers, bool inSymbol, std::size_t* props) {
    Project p;
    p.programs.variables.push_back(hmiVar(p, "Reels", "ARRAY[0..9] OF REAL"));
    p.programs.variables.push_back(hmiVar(p, "Flags", "ARRAY[0..9] OF BOOL"));
    View host = makeView(p, inSymbol ? "S_Biblio" : "Vue_Biblio");
    host.width = 4000;
    host.height = 4000;
    if (inSymbol) {
        host.role = "symbole";
        host.params.push_back({"P", "", "", "REAL", ParamMode::Reference});
        host.params.push_back({"B", "", "", "BOOL", ParamMode::Reference});
    }
    double x = 0, y = 0;
    std::size_t n = 0;
    for (const Kind k : kPlaceableKinds) {
        const Id id = edit::add(p, host, k, x, y);
        Object* o = host.object(id);
        o->name = "O_" + std::to_string(static_cast<int>(k));
        x += 200;
        if (x > 3600) { x = 0; y += 200; }
        for (auto& prop : o->props) {
            if (prop.key == "x" || prop.key == "y" || prop.key == "w" || prop.key == "h") continue;   // la pose : a part
            if (prop.key == "text") {
                if (prop.value.find('{') == std::string::npos) {
                    prop.value = inSymbol ? (markers ? "{$P$:0.0}" : "{P:0.0}") : (markers ? "{$Reels[1]$:0.0}" : "{Reels[1]:0.0}");
                    ++n;
                }
                continue;
            }
            if (prop.key == "variable" || prop.key == "state" || prop.key == "lamp") {
                const bool boolish = k == Kind::Switch || k == Kind::CheckBox || k == Kind::PushButton || k == Kind::IlluminatedButton
                                  || k == Kind::Indicator || k == Kind::Lamp;
                prop.value = inSymbol ? (markers ? (boolish ? "$B$" : "$P$") : (boolish ? "B" : "P"))
                                      : (markers ? (boolish ? "$Flags[2]$" : "$Reels[2]$") : (boolish ? "Flags[2]" : "Reels[2]"));
                ++n;
                continue;
            }
            const std::string f = formulaFor(prop, markers, inSymbol);
            if (f.empty()) continue;
            prop.expr = f;
            ++n;
        }
    }
    if (props) *props = n;
    const Id hostId = host.id;
    p.views.push_back(host);
    if (inSymbol) {
        View v = makeView(p, "Vue_Biblio");
        v.width = 4200;
        v.height = 4200;
        p.views.push_back(v);
        View& vv = *p.viewByName("Vue_Biblio");
        const Id i = placeSymbol(p, vv, "S_Biblio", 0, 0);
        vv.object(i)->name = "Inst";
        vv.object(i)->set("params", markers ? "P := $Reels[1]$; B := $Flags[0]$" : "P := Reels[1]; B := Flags[0]");
        p.config.startView = vv.id;
    } else {
        p.config.startView = hostId;
    }
    return p;
}
void bibliothequeDifferentielle(bool inSymbol) {
    const std::string label = inSymbol ? "la bibliotheque dans un symbole" : "la bibliotheque dans une vue";
    std::printf("-- %s : chaque objet, chaque propriete, avec et sans repere\n", label.c_str());
    std::size_t props = 0;
    const Project plain = bibliotheque(false, inSymbol, &props);
    const Project marked = bibliotheque(true, inSymbol, nullptr);
    const auto a = issuesOf(plain), b = issuesOf(marked);
    std::size_t extra = 0;
    for (const auto& i : b)
        if (!a.count(i)) {
            ++extra;
            std::printf("  en plus avec reperes : %s\n", i.c_str());
        }
    check(extra == 0, label + " : " + std::to_string(props) + " proprietes, rien de plus a Compiler et Generer avec les reperes");
    const Id va = plain.viewByName("Vue_Biblio")->id, vb = marked.viewByName("Vue_Biblio")->id;
    const auto ea = animationErrorsOf(plain, va), eb = animationErrorsOf(marked, vb);
    std::size_t more = 0;
    for (const auto& e : eb)
        if (!ea.count(e)) {
            ++more;
            std::printf("  animation en plus avec reperes : %s\n", e.c_str());
        }
    check(more == 0, label + " : l'animation ne dit rien de plus avec les reperes");
    obs(label + " : sans repere, " + std::to_string(a.size()) + " remarques, " + std::to_string(ea.size()) + " erreurs d'animation");
}

// ====================== 9. popups Copie / Les deux, Naviguer, clavier, Maths, courbes ==
void popupsEtActions() {
    std::printf("-- popups Copie / Les deux, Naviguer, clavier virtuel, Maths, courbes, tableaux\n");
    Built b = build();
    Project& p = b.p;
    View pc = makeView(p, "Pop_Copie");
    pc.role = "popup";
    pc.params.push_back({"C", "", "", "T_Cuve", ParamMode::Copy});
    pc.params.push_back({"D", "", "", "T_Cuve", ParamMode::Both});
    std::map<std::string, Id> pcIds;
    {
        auto& f1 = add(p, pc, Kind::InputField, "Saisie_C", 0, 0);
        f1.set("variable", "C.Consigne");
        f1.set("mode", "numerique");
        pcIds["Saisie_C"] = f1.id;
        auto& f2 = add(p, pc, Kind::InputField, "Saisie_D", 0, 40);
        f2.set("variable", "D.Consigne");
        f2.set("mode", "numerique");
        pcIds["Saisie_D"] = f2.id;
        auto& ap = add(p, pc, Kind::Button, "Btn_Appliquer", 0, 80);
        ap.actions.push_back(act(Trigger::Click, Operation::ApplyCopy, "D"));
        pcIds["Btn_Appliquer"] = ap.id;
    }
    const Id pcId = pc.id;
    p.views.push_back(pc);
    View vd = makeView(p, "Vue_Detail");
    vd.params.push_back({"C", "", "", "T_Cuve", ParamMode::Reference});
    Id vdSaisie = kNoId;
    {
        auto& f = add(p, vd, Kind::InputField, "Saisie_V", 0, 0);
        f.set("variable", "C.Niveau");
        f.set("mode", "numerique");
        vdSaisie = f.id;
    }
    const Id vdId = vd.id;
    p.views.push_back(vd);
    // Le clavier virtuel dans la popup du projet (C : Reference).
    {
        View& det = *p.viewByName("Pop_Detail");
        auto& kb = add(p, det, Kind::Button, "Btn_Clavier_C", 0, 80);
        Action a = act(Trigger::Click, Operation::Keyboard, "C.Niveau");
        actionkinds::KeyboardSpec k;
        k.keyboard = "numerique";
        actionkinds::setKeyboardSpec(a, k);
        kb.actions.push_back(a);
        b.projObj["Btn_Clavier_C"] = kb.id;
    }
    View& sym = *p.viewByName("S_Cuve");
    {
        auto& bc = add(p, sym, Kind::Button, "Btn_Copie", 0, 420);
        bc.actions.push_back(act(Trigger::Click, Operation::Popup, "Pop_Copie", "C := Cuve; D := $Cuve$"));
        b.symObj["Btn_Copie"] = bc.id;
        auto& bn = add(p, sym, Kind::Button, "Btn_Nav", 100, 420);
        bn.actions.push_back(act(Trigger::Click, Operation::Navigate, "Vue_Detail", "C := Cuve"));
        b.symObj["Btn_Nav"] = bn.id;
        auto& kb = add(p, sym, Kind::Button, "Btn_Clavier", 200, 420);
        Action a = act(Trigger::Click, Operation::Keyboard, "Cuve.Niveau");
        actionkinds::KeyboardSpec k;
        k.keyboard = "numerique";
        k.max = "Haut";
        actionkinds::setKeyboardSpec(a, k);
        kb.actions.push_back(a);
        b.symObj["Btn_Clavier"] = kb.id;
        auto& bm = add(p, sym, Kind::Button, "Btn_Maths", 300, 420);
        Action m = act(Trigger::Click, Operation::Maths, "Cuve.Niveau", "A * 2.0");
        m.params = "A := Cuve.IN_Percent";
        bm.actions.push_back(m);
        b.symObj["Btn_Maths"] = bm.id;
        auto& tr = add(p, sym, Kind::Trend, "Courbe", 0, 460);
        tr.set("variables", "Cuve.Niveau;Tab[I].Niveau");
        b.symObj["Courbe"] = tr.id;
        auto& tb = add(p, sym, Kind::Table, "Tableau", 300, 460);
        tb.set("cells", "=Cuve.Niveau\t{Cuve.Nom}\n=$Cuve$.Consigne\t{Tab[$I$].Nom}");
        b.symObj["Tableau"] = tb.id;
    }
    View& vt = *p.viewByName("Vue_Test");
    {
        auto& bv = add(p, vt, Kind::Button, "Btn_Pop_Vue", 1600, 1700);
        bv.actions.push_back(act(Trigger::Click, Operation::Popup, "Sym_0.Pop_Cuve"));
        b.symObj["Btn_Pop_Vue"] = bv.id;
    }
    Script ouvre;
    ouvre.id = p.allocate();
    ouvre.name = "Ouvre_Pop";
    ouvre.event = "Appel";
    ouvre.body = "IHM_POPUP('Vue_Test.Sym_A.Pop_Cuve');";
    p.programs.scripts.push_back(ouvre);
    compilerGenerer(b, "popups et actions");
    for (const auto& [name, cuve] : std::vector<std::pair<std::string, std::string>>{{"Sym_0", "V[0]"}, {"Sym_A", "Cuve_A"}, {"Sym_W", "W[0]"}}) {
        Built one = b;
        Runtime rt;
        rt.bind(&one.p, &one.plc);
        rt.start(0.0);
        rt.tick(0.0);
        double t = 0.1;
        const auto step = [&] { t += 0.1; rt.tick(t); };
        const auto ex = [&](const char* child) { return expandedId(one.inst.at(name), one.symObj.at(child)); };
        const auto press = [&](Id id) { rt.press(id, t); rt.release(id, t, true); step(); };
        const auto type = [&](Id id, const char* text) {
            rt.objectPart(id, "champ", t);
            rt.typeText(text, t);
            rt.typeKey(EditKey::Enter, t);
            step();
        };
        const std::string who = "popups/actions " + name;
        const std::size_t from = rt.journal().size();
        // Copie / Les deux
        press(ex("Btn_Copie"));
        check(!rt.popups().empty() && rt.popups().back() == pcId, who + " : Pop_Copie s'ouvre (C := Cuve ; D := $Cuve$)");
        if (!rt.popups().empty()) {
            type(pcIds.at("Saisie_C"), "5");
            check(sameNumber(valueOf(rt, cuve + ".Consigne"), 0), who + " : C (Copie) n'ecrit pas " + cuve + " (" + valueOf(rt, cuve + ".Consigne") + ")");
            type(pcIds.at("Saisie_D"), "6");
            check(sameNumber(valueOf(rt, cuve + ".Consigne"), 0), who + " : D (Les deux) attend Appliquer (" + valueOf(rt, cuve + ".Consigne") + ")");
            press(pcIds.at("Btn_Appliquer"));
            check(sameNumber(valueOf(rt, cuve + ".Consigne"), 6), who + " : Appliquer copie sur reference ecrit " + cuve + ".Consigne (" + valueOf(rt, cuve + ".Consigne") + ")");
            (void)rt.closePopup(Transition{}, t);
            step();
        }
        // Le clavier virtuel du symbole (max := Haut)
        press(ex("Btn_Clavier"));
        check(rt.promptShown(), who + " : le clavier virtuel s'ouvre (Cuve.Niveau)");
        if (rt.promptShown()) {
            rt.typeText("15", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(sameNumber(valueOf(rt, cuve + ".Niveau"), 15), who + " : le clavier virtuel ecrit " + cuve + ".Niveau (" + valueOf(rt, cuve + ".Niveau") + ")");
        }
        // Maths : Cuve.Niveau := Cuve.IN_Percent * 2
        sim::Value in = sim::Value::real(21.0);
        (void)rt.environment().write(cuve + ".IN_Percent", in);
        press(ex("Btn_Maths"));
        check(sameNumber(valueOf(rt, cuve + ".Niveau"), 42), who + " : Maths ecrit " + cuve + ".Niveau (" + valueOf(rt, cuve + ".Niveau") + ")");
        // La popup du projet : le clavier virtuel sur C.Niveau (C : Reference)
        press(ex("Btn_Detail"));
        if (!rt.popups().empty()) {
            press(one.projObj.at("Btn_Clavier_C"));
            check(rt.promptShown(), who + " : le clavier virtuel de Pop_Detail s'ouvre (C.Niveau)");
            if (rt.promptShown()) {
                rt.typeText("17", t);
                rt.typeKey(EditKey::Enter, t);
                step();
                check(sameNumber(valueOf(rt, cuve + ".Niveau"), 17), who + " : le clavier virtuel de la popup ecrit C.Niveau -> " + cuve + ".Niveau (" + valueOf(rt, cuve + ".Niveau") + ")");
            }
            (void)rt.closePopup(Transition{}, t);
            step();
        }
        // Naviguer avec C := Cuve
        press(ex("Btn_Nav"));
        check(rt.currentView() == vdId, who + " : Naviguer vers Vue_Detail (C := Cuve)");
        if (rt.currentView() == vdId) {
            const std::size_t mark = rt.journal().size();
            type(vdSaisie, "8");
            check(sameNumber(valueOf(rt, cuve + ".Niveau"), 8), who + " : Vue_Detail ecrit C.Niveau -> " + cuve + ".Niveau (" + valueOf(rt, cuve + ".Niveau") + ")");
            if (!sameNumber(valueOf(rt, cuve + ".Niveau"), 8)) {
                std::size_t k = 0;
                for (const auto& e : rt.journal())
                    if (k++ >= mark) obs("journal : " + e.kind + " | " + e.source + " | " + e.message);
                const FormState* fs = rt.formState(vdSaisie);
                obs(std::string("focus : ") + (rt.focusedObject() == vdSaisie ? "le champ" : "autre") + " ; message : " + (fs ? fs->message : std::string("-")));
            }
            (void)rt.navigate(one.view, Transition{}, t);
            step();
        }
        check(journalErrors(rt, from, who) == 0, who + " : aucune erreur au journal");
    }
    // La popup du symbole ouverte depuis la vue (Sym_0.Pop_Cuve) et par IHM_POPUP (Sym_A).
    {
        Runtime rt;
        rt.bind(&b.p, &b.plc);
        rt.start(0.0);
        rt.tick(0.0);
        double t = 0.1;
        const auto step = [&] { t += 0.1; rt.tick(t); };
        const std::size_t from = rt.journal().size();
        rt.press(b.symObj.at("Btn_Pop_Vue"), t);
        rt.release(b.symObj.at("Btn_Pop_Vue"), t, true);
        step();
        if (!rt.popups().empty()) {
            rt.objectPart(b.ownObj.at("Saisie_P"), "champ", t);
            rt.typeText("77", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(sameNumber(valueOf(rt, "V[0].Consigne"), 77), "Sym_0.Pop_Cuve (depuis la vue) ecrit V[0].Consigne (" + valueOf(rt, "V[0].Consigne") + ")");
            (void)rt.closePopup(Transition{}, t);
            step();
        } else {
            check(false, "Sym_0.Pop_Cuve s'ouvre depuis la vue");
        }
        std::string why;
        check(rt.callScript("Ouvre_Pop", t, &why), "IHM_POPUP('Vue_Test.Sym_A.Pop_Cuve') " + why);
        step();
        if (!rt.popups().empty()) {
            rt.objectPart(b.ownObj.at("Saisie_P"), "champ", t);
            rt.typeText("78", t);
            rt.typeKey(EditKey::Enter, t);
            step();
            check(sameNumber(valueOf(rt, "Cuve_A.Consigne"), 78), "IHM_POPUP : la popup de Sym_A ecrit Cuve_A.Consigne (" + valueOf(rt, "Cuve_A.Consigne") + ")");
        } else {
            check(false, "IHM_POPUP('Vue_Test.Sym_A.Pop_Cuve') ouvre la popup");
        }
        check(journalErrors(rt, from, "popups depuis la vue") == 0, "popups depuis la vue, par script : aucune erreur au journal");
        animation(b, rt, "popups et actions");
    }
}

// =================================== 10. Dupliquer DANS le symbole (son editeur) ==
//  Dans S_Cuve : Saisie_R ecrit $Cuve$.IN_Percent ; Dupliquer avec Cuve -> Tab[1], Tab[2] :
//  les copies ecrivent $Tab[1]$.IN_Percent... - en marche, V[1], V[2] (Tab := V).
void dupliquerDansLeSymbole() {
    std::printf("-- Dupliquer dans le symbole ($Cuve$ -> $Tab[1]$, $Tab[2]$)\n");
    using namespace hmi::dup;
    Built b = build();
    View& sym = *b.p.viewByName("S_Cuve");
    Id src = kNoId;
    {
        auto& f = add(b.p, sym, Kind::InputField, "Saisie_R", 0, 500);
        f.set("variable", "$Cuve$.IN_Percent");
        f.set("mode", "numerique");
        src = f.id;
    }
    const Scan sc = scan(sym, {src}, symbolParamBounds(sym));
    check(sc.marker("Cuve") != nullptr, "Dupliquer trouve $Cuve$ (un parametre du symbole)");
    Plan plan = defaultPlan(sym, sc, 2);
    for (int k = 1; k <= 2; ++k) plan.rows[static_cast<std::size_t>(k)].markers[markerKey("Cuve")] = "Tab[" + std::to_string(k) + "]";
    plan.replaceOriginal = false;
    const Result r = apply(b.p, sym, plan);
    check(r.created.size() == 2, "deux copies dans le symbole");
    for (const Id c : r.created) obs("copie : " + sym.object(c)->name + " variable = " + sym.object(c)->text("variable"));
    compilerGenerer(b, "Dupliquer dans le symbole");
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const std::size_t from = rt.journal().size();
    for (std::size_t k = 0; k < r.created.size(); ++k) {
        const Id id = expandedId(b.inst.at("Sym_0"), r.created[k]);
        rt.objectPart(id, "champ", t);
        rt.typeText("44", t);
        rt.typeKey(EditKey::Enter, t);
        t += 0.1;
        rt.tick(t);
        const std::string path = "V[" + std::to_string(k + 1) + "].IN_Percent";
        check(sameNumber(valueOf(rt, path), 44), "la copie " + std::to_string(k + 1) + " ($Tab[" + std::to_string(k + 1) + "]$) ecrit " + path + " (" + valueOf(rt, path) + ")");
    }
    check(journalErrors(rt, from, "Dupliquer dans le symbole") == 0, "Dupliquer dans le symbole : aucune erreur au journal");
}

// ===================== 11. un symbole dans une popup a parametres ; une redefinition ==
//  Pop_Ligne (C : T_Cuve) pose S_Mini avec M := $C$ ; Sym_0 redefinit Ajouter (avec Cuve).
void popupAvecSymboleEtRedefinition() {
    std::printf("-- un symbole dans une popup a parametres (M := $C$), une redefinition d'instance\n");
    Built b = build();
    Project& p = b.p;
    View pl = makeView(p, "Pop_Ligne");
    pl.role = "popup";
    pl.params.push_back({"C", "", "", "T_Cuve", ParamMode::Reference});
    p.views.push_back(pl);
    View& pop = *p.viewByName("Pop_Ligne");
    const Id m = placeSymbol(p, pop, "S_Mini", 0, 0);
    pop.object(m)->name = "Mini_P";
    pop.object(m)->set("params", "M := $C$");
    View& sym = *p.viewByName("S_Cuve");
    HmiFunction aj;
    aj.id = p.allocate();
    aj.name = "Ajouter";
    aj.isVirtual = true;
    aj.body = "Cuve.Consigne := Cuve.Consigne + 1;";
    sym.functions.push_back(aj);
    {
        auto& bt = add(p, sym, Kind::Button, "Btn_Fonction", 400, 300);
        bt.actions.push_back(act(Trigger::Click, Operation::RunScript, {}, "Ajouter();"));
        b.symObj["Btn_Fonction"] = bt.id;
        auto& bl = add(p, sym, Kind::Button, "Btn_Ligne", 500, 300);
        bl.actions.push_back(act(Trigger::Click, Operation::Popup, "Pop_Ligne", "C := $Cuve$"));
        b.symObj["Btn_Ligne"] = bl.id;
    }
    View& vt = *p.viewByName("Vue_Test");
    vt.object(b.inst.at("Sym_0"))->functionOverrides.push_back({"Ajouter", "Cuve.Consigne := Cuve.Consigne + 100;\nSUPER.Ajouter();"});
    compilerGenerer(b, "symbole dans une popup, redefinition");
    Runtime rt;
    rt.bind(&b.p, &b.plc);
    rt.start(0.0);
    rt.tick(0.0);
    double t = 0.1;
    const auto step = [&] { t += 0.1; rt.tick(t); };
    const std::size_t from = rt.journal().size();
    const Id i0 = b.inst.at("Sym_0");
    rt.press(expandedId(i0, b.symObj.at("Btn_Fonction")), t);
    rt.release(expandedId(i0, b.symObj.at("Btn_Fonction")), t, true);
    step();
    check(sameNumber(valueOf(rt, "V[0].Consigne"), 101), "Sym_0 redefinit Ajouter (Cuve := $V[0]$) : +100 puis SUPER +1 (" + valueOf(rt, "V[0].Consigne") + ")");
    rt.press(expandedId(i0, b.symObj.at("Btn_Ligne")), t);
    rt.release(expandedId(i0, b.symObj.at("Btn_Ligne")), t, true);
    step();
    check(!rt.popups().empty(), "Pop_Ligne s'ouvre (C := $Cuve$)");
    if (!rt.popups().empty()) {
        const Id field = expandedId(m, b.miniObj.at("Saisie_M"));
        rt.objectPart(field, "champ", t);
        rt.typeText("9", t);
        rt.typeKey(EditKey::Enter, t);
        step();
        check(sameNumber(valueOf(rt, "V[0].Consigne"), 9), "S_Mini (M := $C$) dans Pop_Ligne ecrit V[0].Consigne (" + valueOf(rt, "V[0].Consigne") + ")");
        const Id btn = expandedId(m, b.miniObj.at("Btn_M"));
        rt.press(btn, t);
        rt.release(btn, t, true);
        step();
        check(sameNumber(valueOf(rt, "V[0].Consigne"), 10), "son bouton incremente V[0].Consigne (" + valueOf(rt, "V[0].Consigne") + ")");
    }
    check(journalErrors(rt, from, "popup a symbole") == 0, "symbole dans une popup, redefinition : aucune erreur au journal");
}
} // namespace

int main() {
    std::printf("1.11.24 : les reperes dans les symboles (Compiler, Generer, en marche, Dupliquer)\n");
    Built b = build();
    compilerGenerer(b, "projet");
    const std::map<std::string, std::string> cuves = {{"Sym_0", "V[0]"}, {"Sym_1", "V[1]"}, {"Sym_2", "V[2]"}, {"Sym_3", "V[3]"},
                                                      {"Sym_A", "Cuve_A"}, {"Sym_W", "W[0]"}, {"Sym_X", "V[1]"}};
    // Une instance a la fois (chacune son projet) : les ecritures ne se melangent pas.
    for (const auto& [name, cuve] : cuves) {
        Built one = build();
        enMarche(one, "marche", {{name, cuve}});
    }
    reperesDansLeSymbole();
    alarmesFonctionsScripts();
    deuxNiveaux();
    dupliquer();
    bibliothequeDifferentielle(false);
    bibliothequeDifferentielle(true);
    popupsEtActions();
    dupliquerDansLeSymbole();
    popupAvecSymboleEtRedefinition();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
