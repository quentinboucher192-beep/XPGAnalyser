// =============================================================================
//  tests/hmi_cycle_test.cpp - 1.27.0 : le cycle d'une vue, deballe (l'OnCycle a son moment)
// -----------------------------------------------------------------------------
//  Le moteur sans ecran : les scripts de cycle d'une vue tournent apres l'etape de leur
//  moment (1 a 9), dans l'ordre des etapes et, dans un moment, dans leur rang ; 1 cycle
//  sur N ; seulement si leur condition est vraie. L'OnCycle d'avant (apres 4) ne change
//  pas, OnOpen et OnClose non plus. Un ecran (setScreenStages) lance 8 et 9 lui-meme.
//  La mesure (cycleTiming), le disque (la vue, le .xpgst), Compiler (la condition).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiComm.hpp"
#include "../src/hmi/HmiEquipment.hpp"
#include "../src/hmi/HmiModbus.hpp"
#include "../src/hmi/HmiPipeline.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScriptFile.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiViewCycle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int checks = 0, failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

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
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic) override {}
};

void variable(Project& p, const std::string& name, const std::string& type, const std::string& initial = {}) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = initial;
    p.programs.variables.push_back(v);
}

Script cycleScript(Project& p, const std::string& name, int moment, const std::string& body, int every = 1, const std::string& when = {}) {
    Script sc;
    sc.id = p.allocate();
    sc.name = name;
    sc.event = "OnCycle";
    sc.body = body;
    sc.stage = moment;
    sc.everyCycles = every;
    sc.when = when;
    return sc;
}

long long intOf(Runtime& rt, const char* name) {
    const auto* v = rt.variable(name);
    return v ? v->asInteger() : -999;
}

bool journalHas(const Runtime& rt, std::string_view kind, std::string_view text) {
    return std::any_of(rt.journal().begin(), rt.journal().end(),
                       [&](const auto& e) { return (kind.empty() || e.kind == kind) && e.message.find(text) != std::string::npos; });
}

// ---- l'ordre des etapes : un script par moment, chacun note son rang ----
void ordreDesEtapes() {
    std::printf("l'ordre des etapes du cycle de la vue\n");
    Project p;
    View v = makeView(p, "Vue_Accueil");
    variable(p, "Seq", "INT", "0");
    for (int m = 1; m <= 9; ++m) variable(p, "R" + std::to_string(m), "INT", "0");
    for (const char* n : {"RG", "RT", "A4", "A6", "Ouv", "Ferm"}) variable(p, n, "INT", "0");
    variable(p, "Go", "BOOL", "FALSE");
    // Dans le desordre dans la vue : le moteur suit les etapes, pas l'ordre de la liste.
    for (const int m : {9, 1, 8, 4, 6, 2, 3, 5, 7})
        v.scripts.push_back(cycleScript(p, "Etape_" + std::to_string(m), m,
                                        "Seq := Seq + 1;\nR" + std::to_string(m) + " := Seq;\n"
                                            + (m == 4 ? "A4 := SYS.AlarmActiveCount;\n" : m == 6 ? "A6 := SYS.AlarmActiveCount;\n" : "")));
    Script open;
    open.id = p.allocate();
    open.name = "Vue_Accueil_OnOpen";
    open.event = "OnOpen";
    open.body = "Ouv := Ouv + 1;";
    v.scripts.push_back(open);
    Script close = open;
    close.id = p.allocate();
    close.name = "Vue_Accueil_OnClose";
    close.event = "OnClose";
    close.body = "Ferm := Ferm + 1;";
    v.scripts.push_back(close);
    // Un front montant de la vue (l'etape 5) : il note Seq tel qu'il est entre apres 4 et apres 5.
    Action front;
    front.trigger = Trigger::RisingEdge;
    front.watch = "Go";
    front.operation = Operation::Assign;
    front.target = "RT";
    front.value = "Seq";
    v.actions.push_back(front);
    // Un script general cyclique (l'etape 4).
    Script gen;
    gen.id = p.allocate();
    gen.name = "General";
    gen.event = "Cyclique";
    gen.periodMs = 100;
    gen.body = "Seq := Seq + 1;\nRG := Seq;";
    p.programs.scripts.push_back(gen);
    // Une alarme (l'etape 6).
    AlarmDef al;
    al.id = p.allocate();
    al.name = "Niveau_Haut";
    al.condition = "Niveau > 5";
    al.message = "Niveau haut";
    p.alarms.push_back(al);
    View other = makeView(p, "Vue_Autre");
    const Id vid = v.id, otherId = other.id;
    p.views = {v, other};
    p.config.startView = vid;

    FakePlc plc;
    plc.values["Niveau"] = sim::Value::real(1.0);
    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    check(intOf(rt, "Ouv") == 1, "OnOpen : a l'ouverture, avant le premier cycle (comme avant)");
    const auto ranks = [&] {
        std::vector<long long> r;
        for (int m = 1; m <= 9; ++m) r.push_back(intOf(rt, ("R" + std::to_string(m)).c_str()));
        return r;
    };
    const auto text = [](const std::vector<long long>& r) {
        std::string s;
        for (auto x : r) s += std::to_string(x) + " ";
        return s;
    };
    rt.tick(0.0);
    auto r = ranks();
    bool ascending = true;
    for (std::size_t i = 1; i < r.size(); ++i) ascending = ascending && r[i] == r[i - 1] + 1;
    check(r.front() == 1 && ascending, "les scripts tournent dans l'ordre des etapes 1 a 9 (R1..R9 = " + text(r) + ")");
    // Au cycle suivant, le script general cyclique (100 ms) tourne aussi : a l'etape 4, entre apres 3 et apres 4.
    rt.tick(0.1);
    r = ranks();
    ascending = true;
    for (std::size_t i = 1; i < r.size(); ++i) ascending = ascending && r[i] == r[i - 1] + 1 + (i == 3 ? 1 : 0);
    check(ascending && intOf(rt, "RG") == r[2] + 1 && intOf(rt, "RG") == r[3] - 1,
          "le script general cyclique (etape 4) tourne entre apres 3 et apres 4 (R1..R9 = " + text(r) + ", RG = " + std::to_string(intOf(rt, "RG")) + ")");
    // Le front : entre apres 4 et apres 5.
    (void)rt.environment().write("Go", sim::Value::boolean(true));
    rt.tick(0.2);
    check(intOf(rt, "RT") == intOf(rt, "R4") && intOf(rt, "R5") == intOf(rt, "R4") + 1,
          "le declencheur de la vue (etape 5) part entre apres 4 et apres 5 (RT = " + std::to_string(intOf(rt, "RT")) + ", R4 = "
              + std::to_string(intOf(rt, "R4")) + ")");
    // L'alarme : apres 6, elle est la ; apres 4, pas encore.
    plc.values["Niveau"] = sim::Value::real(7.5);
    rt.tick(0.3);
    check(intOf(rt, "A4") == 0 && intOf(rt, "A6") == 1,
          "au cycle ou la condition devient vraie, apres 4 ne voit pas l'alarme, apres 6 la voit (A4 = " + std::to_string(intOf(rt, "A4"))
              + ", A6 = " + std::to_string(intOf(rt, "A6")) + ")");
    rt.tick(0.4);
    check(intOf(rt, "A4") == 1, "... et apres 4 la voit au cycle suivant");
    // La mesure du cycle.
    const auto t = rt.cycleTiming();
    bool steps = t.measured() && t.view == vid;
    for (int m = 1; m <= 7; ++m) steps = steps && t.stepMs[static_cast<std::size_t>(m)] >= 0;
    check(steps && t.stepMs[8] < 0 && t.stepMs[9] < 0 && t.periodMs == 100.0,
          "la mesure : les etapes 1 a 7 mesurees, 8 et 9 sans ecran : non mesurees ; la periode");
    check(t.writes >= 11, "la mesure : les ecritures du cycle (" + std::to_string(t.writes) + ")");
    check(rt.perf().scripts.count(v.scripts.front().id) && rt.perf().scripts.at(v.scripts.front().id).time.count >= 4,
          "chaque script de cycle a sa duree mesuree (perf().scripts)");
    // Pas d'ecriture fausse : un tick qui n'est pas un cycle (20 ms plus tard) ne lance aucun script de cycle.
    const long long seq = intOf(rt, "Seq");
    rt.tick(0.42);
    check(intOf(rt, "Seq") == seq, "un tick sans cycle du (20 ms) ne lance aucun script de cycle");
    // OnClose : en quittant la vue ; ses scripts de cycle s'arretent.
    check(rt.navigate(otherId, Transition{}, 0.45), "naviguer vers Vue_Autre");
    check(intOf(rt, "Ferm") == 1, "OnClose : a la fermeture (comme avant)");
    const long long seq2 = intOf(rt, "Seq");
    rt.tick(0.5);
    check(intOf(rt, "Seq") == seq2 + 1, "la vue fermee : ses scripts de cycle ne tournent plus (seul le script general)");
}

// ---- l'OnCycle d'avant : apres 4, chaque cycle, rien d'autre ne change ----
void ancienOnCycle() {
    std::printf("l'OnCycle d'avant : apres 4, chaque cycle\n");
    Project p;
    View v = makeView(p, "Vue_A");
    variable(p, "Cycles", "INT", "0");
    Script sc;
    sc.id = p.allocate();
    sc.name = "A_cycle";
    sc.event = "OnCycle";
    sc.body = "Cycles := Cycles + 1;";
    v.scripts.push_back(sc);
    check(sc.stage == viewcycle::kLegacyMoment && sc.everyCycles == 1 && sc.when.empty() && !viewcycle::customized(sc),
          "un script de vue OnCycle nait apres 4, chaque cycle, sans condition");
    p.views = {v};
    p.config.startView = v.id;
    FakePlc plc;
    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    for (int i = 0; i <= 20; ++i) rt.tick(i * 0.1);
    check(intOf(rt, "Cycles") == 21, "OnCycle a chaque cycle IHM (21 en 2 s, comme avant) : " + std::to_string(intOf(rt, "Cycles")));
    check(rt.perf().scripts.count(sc.id) && rt.perf().scripts.at(sc.id).name == "Vue_A.OnCycle", "son nom au journal : Vue_A.OnCycle (comme avant)");
    // Le disque : une vue d'avant s'ecrit a l'identique (pas de moment, de frequence, de condition).
    const std::string text = serializeView(p.views[0]);
    check(text.find("moment=") == std::string::npos && text.find("frequence=") == std::string::npos && text.find("condition=") == std::string::npos,
          "le disque : un OnCycle d'avant s'ecrit sans moment, frequence ni condition");
    const auto back = parseView(text);
    check(back.has_value() && back.value().scripts.size() == 1 && back.value().scripts[0].stage == 4 && back.value().scripts[0].everyCycles == 1,
          "le disque : relu, apres 4, chaque cycle");
    // Le .xpgst : le meme bloc qu'avant.
    const auto file = scriptfile::write(scriptfile::fromView(p.views[0]));
    check(file.find("(*# script evenement=OnCycle langage=ST *)") != std::string::npos, "le .xpgst : le bloc de l'OnCycle d'avant, inchange");
}

// ---- plusieurs scripts dans un moment : leur rang ; deplacer ----
void rangs() {
    std::printf("plusieurs scripts dans un moment : leur rang\n");
    Project p;
    View v = makeView(p, "Vue_R");
    variable(p, "Seq", "INT", "0");
    variable(p, "P", "INT", "0");
    variable(p, "S", "INT", "0");
    v.scripts.push_back(cycleScript(p, "Premier", 6, "Seq := Seq + 1;\nP := Seq;"));
    v.scripts.push_back(cycleScript(p, "Second", 6, "Seq := Seq + 1;\nS := Seq;"));
    const Id first = v.scripts[0].id, second = v.scripts[1].id;
    check(viewcycle::rankOf(v, first) == std::pair<int, int>{1, 2} && viewcycle::rankOf(v, second) == std::pair<int, int>{2, 2},
          "le rang dans l'etape : 1 sur 2, 2 sur 2");
    p.views = {v};
    p.config.startView = v.id;
    FakePlc plc;
    {
        Runtime rt;
        rt.bind(&p, &plc);
        rt.start(0.0);
        rt.tick(0.0);
        check(intOf(rt, "P") == 1 && intOf(rt, "S") == 2, "dans un moment : dans leur rang (Premier, puis Second)");
    }
    check(viewcycle::move(p.views[0], second, 6, 0) && viewcycle::rankOf(p.views[0], second).first == 1,
          "deplacer Second au rang 1 de l'etape 6");
    {
        Runtime rt;
        rt.bind(&p, &plc);
        rt.start(0.0);
        rt.tick(0.0);
        check(intOf(rt, "S") == 1 && intOf(rt, "P") == 2, "... il tourne le premier");
    }
    check(viewcycle::move(p.views[0], first, 2, 5) && p.views[0].scripts[0].id == first && p.views[0].scripts[0].stage == 2,
          "deplacer Premier apres 2 : il change de moment (et passe devant dans la vue)");
    check(!viewcycle::move(p.views[0], first, 2, 0), "le deposer la ou il est : rien ne change");
}

// ---- 1 cycle sur N ; la condition ----
void frequenceEtCondition() {
    std::printf("1 cycle sur N, la condition\n");
    Project p;
    View v = makeView(p, "Vue_F");
    variable(p, "F", "INT", "0");
    variable(p, "C", "INT", "0");
    variable(p, "E", "INT", "0");
    variable(p, "Mode_Auto", "BOOL", "FALSE");
    v.scripts.push_back(cycleScript(p, "Tous_Les_3", 7, "F := F + 1;", 3));
    v.scripts.push_back(cycleScript(p, "En_Auto", 3, "C := C + 1;", 1, "=Mode_Auto"));
    v.scripts.push_back(cycleScript(p, "Illisible", 5, "E := E + 1;", 1, "Inconnue > 2"));
    View other = makeView(p, "Vue_G");
    const Id vid = v.id, otherId = other.id;
    p.views = {v, other};
    p.config.startView = vid;
    FakePlc plc;
    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    for (int i = 0; i < 9; ++i) rt.tick(i * 0.1);
    check(intOf(rt, "F") == 3, "1 cycle sur 3 : 3 fois en 9 cycles (le premier cycle compris) : " + std::to_string(intOf(rt, "F")));
    check(intOf(rt, "C") == 0, "condition =Mode_Auto fausse : le script ne tourne pas");
    (void)rt.environment().write("Mode_Auto", sim::Value::boolean(true));
    rt.tick(0.9);
    rt.tick(1.0);
    check(intOf(rt, "C") == 2, "condition vraie : il tourne a chaque cycle (" + std::to_string(intOf(rt, "C")) + ")");
    check(intOf(rt, "E") == 0 && journalHas(rt, "Erreur", "condition =Inconnue > 2"),
          "une condition qui ne se lit pas : le script ne tourne pas, le journal le dit");
    std::size_t said = 0;
    for (const auto& e : rt.journal()) said += e.message.find("condition =Inconnue") != std::string::npos;
    check(said == 1, "... une seule fois (" + std::to_string(said) + ")");
    // La vue rouverte : 1 cycle sur 3 repart (il tourne au premier cycle).
    const long long f = intOf(rt, "F");
    check(rt.navigate(otherId, Transition{}, 1.05) && rt.navigate(vid, Transition{}, 1.06), "fermer puis rouvrir la vue");
    rt.tick(1.1);
    check(intOf(rt, "F") == f + 1, "la vue rouverte : 1 cycle sur 3 repart, il tourne au premier cycle");
}

// ---- un ecran : 8 et 9 a sa demande ----
void ecran() {
    std::printf("un ecran : apres 8 (avant le dessin), apres 9 (le dessin fait)\n");
    Project p;
    View v = makeView(p, "Vue_E");
    variable(p, "Seq", "INT", "0");
    for (const char* n : {"R1", "R7", "R8", "R9"}) variable(p, n, "INT", "0");
    for (const int m : {1, 7, 8, 9})
        v.scripts.push_back(cycleScript(p, "S" + std::to_string(m), m, "Seq := Seq + 1;\nR" + std::to_string(m) + " := Seq;"));
    p.views = {v};
    p.config.startView = v.id;
    FakePlc plc;
    Runtime rt;
    rt.bind(&p, &plc);
    rt.setScreenStages(true);
    rt.start(0.0);
    rt.tick(0.0);
    check(intOf(rt, "R7") == 2 && intOf(rt, "R8") == 0 && intOf(rt, "R9") == 0, "avec un ecran : le tick s'arrete apres 7 (8 et 9 attendent l'ecran)");
    check(rt.afterFormulas(0.01) && intOf(rt, "R8") == 3 && intOf(rt, "R9") == 0, "afterFormulas : apres 8 tourne (vrai : la vue est a recalculer)");
    check(!rt.afterFormulas(0.02), "... une fois par cycle");
    rt.afterDrawing(0.03);
    check(intOf(rt, "R9") == 4, "afterDrawing : apres 9 tourne, le dessin fait");
    // L'ecran n'appelle rien : le tick suivant les lance avant l'etape 1 du cycle suivant.
    rt.tick(0.1);
    check(intOf(rt, "R1") == 5 && intOf(rt, "R7") == 6 && intOf(rt, "R8") == 3, "le cycle suivant : 1 a 7, puis 8 et 9 attendent encore l'ecran");
    rt.tick(0.2);
    check(intOf(rt, "R8") == 7 && intOf(rt, "R9") == 8 && intOf(rt, "R1") == 9,
          "rien d'appele : apres 8 et apres 9 du cycle d'avant partent au debut du tick suivant, avant 1 (R8 = " + std::to_string(intOf(rt, "R8"))
              + ", R9 = " + std::to_string(intOf(rt, "R9")) + ", R1 = " + std::to_string(intOf(rt, "R1")) + ")");
    rt.notePerf(0.7, 0.9, 10, 5);
    const auto t = rt.cycleTiming();
    check(std::fabs(t.stepMs[8] - 0.7) < 1e-9 && std::fabs(t.stepMs[9] - 0.9) < 1e-9, "la mesure de 8 et 9 : celle que l'ecran donne (notePerf)");
}

// ---- une variable liee : apres 1, la valeur que la liaison vient de lire ----
void lectureApres1() {
    std::printf("apres 1 : la variable liee, lue\n");
    using namespace hmi::comm;
    auto bank = std::make_shared<hmi::modbus::MemoryBank>(200, 200);
    bank->setWord(100, 1234);
    hmi::modbus::Server server;
    if (!server.start("127.0.0.1", 0, bank)) {
        check(false, "la centrale ecoute (127.0.0.1)");
        return;
    }
    Project p;
    Equipment e;
    e.id = p.allocate();
    e.name = "Centrale";
    e.host = "127.0.0.1";
    e.port = server.port();
    e.timeoutMs = 300;
    p.equipments.push_back(e);
    p.programs.variables.push_back(Variable{p.allocate(), "Mesure", "INT", "0", "", "Centrale", "40101"});
    variable(p, "Lu", "INT", "0");
    View v = makeView(p, "Vue_L");
    v.scripts.push_back(cycleScript(p, "Lire_Mesure", 1, "Lu := Mesure;"));
    p.views = {v};
    p.config.startView = v.id;
    Link link(hmi::equip::buildPlan(p, p.equipments[0]), hmi::equip::settingsOf(p.equipments[0]));
    Runtime rt;
    rt.bind(&p, nullptr);
    Runtime::Hooks h;
    h.equipmentLink = [&](const std::string& name) -> Link* { return name == "Centrale" ? &link : nullptr; };
    rt.setHooks(h);
    rt.start(0.0);
    rt.tick(0.0);                  // la premiere lecture abonne la variable
    link.cycle();                  // la tache de communication lit
    rt.tick(0.1);
    check(intOf(rt, "Lu") == 1234, "apres 1 : le script lit la valeur que la liaison a recue (" + std::to_string(intOf(rt, "Lu")) + ")");
    bank->setWord(100, 4321);
    link.cycle();
    rt.tick(0.2);
    check(intOf(rt, "Lu") == 4321, "... et la suivante, au cycle d'apres");
    server.stop();
}

// ---- le disque, le .xpgst, Compiler ----
void disqueEtCompiler() {
    std::printf("le disque, le .xpgst, Compiler\n");
    Project p;
    View v = makeView(p, "Vue_D");
    variable(p, "Mode_Auto", "BOOL", "FALSE");
    variable(p, "X", "INT", "0");
    v.scripts.push_back(cycleScript(p, "Mise_En_Page", 8, "X := 1;", 1, "Mode_Auto"));
    v.scripts.push_back(cycleScript(p, "Couleurs_Alarmes", 6, "X := 2;", 5));
    const std::string text = serializeView(v);
    check(text.find("moment=8") != std::string::npos && text.find("frequence=5") != std::string::npos && text.find("condition=") != std::string::npos,
          "le disque : le moment, la frequence, la condition s'ecrivent");
    const auto back = parseView(text);
    check(back.has_value() && back.value().scripts == v.scripts, "le disque : relus a l'identique");
    // Le .xpgst : tous les scripts de cycle, dans l'ordre ou ils tournent, avec leur nom et leur moment.
    const auto f = scriptfile::fromView(v);
    check(f.entries.size() == 2 && f.entries[0].name == "Couleurs_Alarmes" && f.entries[1].name == "Mise_En_Page",
          "le .xpgst : les deux scripts de cycle, dans l'ordre des etapes");
    const std::string written = scriptfile::write(f);
    scriptfile::File read;
    std::string why;
    check(scriptfile::read(written, read, &why) && read.entries.size() == 2 && read.entries[1].stage == 8 && read.entries[1].when == "Mode_Auto"
              && read.entries[0].everyCycles == 5,
          "le .xpgst relu : moment, frequence, condition " + why);
    View empty = makeView(p, "Vue_Vide");
    check(scriptfile::applyToView(p, empty, read, {}, scriptfile::Mode::Replace) == 2 && viewcycle::cycleScripts(empty).size() == 2
              && viewcycle::cycleScripts(empty)[1]->stage == 8 && viewcycle::cycleScripts(empty)[1]->name == "Mise_En_Page",
          "le .xpgst importe dans une vue vide : deux scripts de cycle, a leur moment");
    const auto states = scriptfile::compareView(read, empty);
    check(states.size() == 2 && states[0] == scriptfile::State::Same && states[1] == scriptfile::State::Same, "reimporte : identiques");
    // Compiler : la condition est une formule booleenne de la vue.
    View bad = v;
    bad.scripts[0].when = "Mode_Auto +";
    bad.scripts[1].when = "Inconnue_Cond";
    p.views = {bad};
    const auto issues = compileWith(p, [](std::string_view) { return false; }, {});
    const auto has = [&](std::string_view text1, std::string_view text2) {
        return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) {
            return i.message.find(text1) != std::string::npos && i.message.find(text2) != std::string::npos;
        });
    };
    check(has("condition =Mode_Auto +", ""), "Compiler : une condition illisible est une erreur");
    check(has("condition =Inconnue_Cond", "Inconnue_Cond"), "Compiler : une condition qui nomme une variable inconnue");
    p.views = {v};
    const auto clean = compileWith(p, [](std::string_view) { return false; }, {});
    check(std::none_of(clean.begin(), clean.end(), [](const Issue& i) { return i.message.find("condition") != std::string::npos; }),
          "Compiler : une bonne condition ne dit rien");
    // Deux scripts de cycle du meme nom : un avertissement.
    View twice = v;
    twice.scripts[1].name = "Mise_En_Page";
    p.views = {twice};
    const auto dup = generate(p, [](std::string_view) { return false; }, {});
    check(std::any_of(dup.begin(), dup.end(), [](const Issue& i) { return i.message.find("deux scripts de cycle") != std::string::npos; }),
          "Generer : deux scripts de cycle du meme nom, un avertissement");
}

// ---- les libelles ----
void libelles() {
    std::printf("les libelles du cycle\n");
    check(viewcycle::steps().size() == 10 && viewcycle::step(1).title == "Lecture des \xC3\xA9quipements"
              && viewcycle::step(10).title == "\xC3\x89" "critures envoy\xC3\xA9" "es",
          "les 10 etapes, de Lecture des equipements a Ecritures envoyees");
    check(viewcycle::momentChoices().size() == 9 && viewcycle::momentFromText(viewcycle::momentChoices()[7]) == 8
              && viewcycle::momentFromText("3") == 3 && viewcycle::momentFromText("12") == 0,
          "le menu Moment : 9 choix (apres 1 a apres 9), relus");
    check(viewcycle::everyFromText("1 cycle sur 5") == 5 && viewcycle::everyFromText("Chaque cycle") == 1 && viewcycle::everyFromText("7") == 7
              && viewcycle::everyFromText("abc") == 0,
          "la frequence : relue");
    check(viewcycle::plainCondition(" =Mode_Auto ") == "Mode_Auto", "la condition : sans le = de tete");
    Script s;
    s.event = "OnCycle";
    s.stage = 6;
    s.everyCycles = 5;
    check(viewcycle::summary(s) == "apr\xC3\xA8s l'\xC3\xA9tape 6 (Alarmes) \xC2\xB7 1 cycle sur 5" && viewcycle::badge(s) == "apr\xC3\xA8s 6 \xC2\xB7 1/5",
          "le resume : " + viewcycle::summary(s));
    check(viewcycle::dropText(8).find("apr\xC3\xA8s 8 Formules, avant 9 Dessin") != std::string::npos
              && viewcycle::dropText(8).find("dessin\xC3\xA9" "es dans ce cycle") != std::string::npos,
          "la ligne pointillee : " + viewcycle::dropText(8));
}

} // namespace

int main() {
    libelles();
    ordreDesEtapes();
    ancienOnCycle();
    rangs();
    frequenceEtCondition();
    ecran();
    lectureApres1();
    disqueEtCompiler();
    std::printf("%d controles, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
