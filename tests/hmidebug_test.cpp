// =============================================================================
//  tests/hmidebug_test.cpp - 1.13.0 (lot J2) : le debogueur des scripts IHM
// -----------------------------------------------------------------------------
//  Un point d'arret pose sur une ligne d'un script IHM arrete VRAIMENT le
//  moteur la, au milieu de l'execution (le script tourne sur un fil a lui) : on
//  y lit la pile, les locales, les valeurs de la ligne ; rien n'est ecrit tant
//  qu'on ne continue pas. Puis le pas a pas : Suivant, Entrer, Sortir, a travers
//  une fonction IHM simple (sa propre execution) et une fonction aux types
//  riches (dans le script). IHM_POINT_ARRET avec et sans condition, une
//  condition francisee (un nom inconnu se dit tout de suite), le decalage d'une
//  ligne sans instruction, le non-arret dans une fonction appelee par une
//  expression, un script detache arrete dans sa boucle d'attente (Suivant
//  enjambe le WAIT : l'IHM respire), l'arret de la simulation en pause. Et le
//  debogueur de l'automate (RuntimeDebug) dit toujours les memes mots.
//
//  1.13.0 (seconde passe) : la pause fige tout le travail en cours (le cycle, le
//  geste, le demarrage), les gestes pendant la pause, les fonctions de vue, les
//  methodes, les actions Appeler et Execute du ST, les espions en lecture seule,
//  l'arret sous TRY ; la seance des points (suivre son code, un autre projet), le
//  volet Debogage et les touches de la simulation (avec xpg_app, sans ecran).
//
//  HMIDEBUG_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/app/TopBar.hpp"
#include "../src/app/hmi/HmiDebugPane.hpp"
#include "../src/app/hmi/HmiDebugSession.hpp"
#include "../src/app/hmi/HmiSimulation.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiCrypto.hpp"
#include "../src/hmi/HmiTable.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiKeys.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/sim/DebugShared.hpp"
#include "../src/sim/Interpreter.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("HMIDEBUG_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

void var(Project& p, const char* n, const char* t, const char* init) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
}

Id script(Project& p, const char* n, const std::string& body, bool detached = false, ScriptLang lang = ScriptLang::ST) {
    Script sc;
    sc.id = p.allocate();
    sc.name = n;
    sc.event = "Appel";
    sc.body = body;
    sc.detached = detached;
    sc.lang = lang;
    p.programs.scripts.push_back(sc);
    return sc.id;
}

Id function(Project& p, const char* name, const char* ret, std::vector<std::pair<std::string, PassMode>> params, const std::string& type,
            const std::string& body) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    for (const auto& [pn, mode] : params) {
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = pn;
        d.type = type;
        d.mode = mode;
        f.decls.push_back(std::move(d));
    }
    f.body = body;
    p.programs.functions.push_back(std::move(f));
    return p.programs.functions.back().id;
}

double value(Runtime& rt, const char* n) {
    sim::Value v;
    return rt.environment().read(n, v) ? v.asReal() : -999.0;
}

std::string errors(const Runtime& rt) {
    std::string out;
    for (const auto& e : rt.journal())
        if (e.kind == "Erreur") out += " | " + e.message;
    return out;
}

std::string console(const Runtime& rt, const std::string& kind) {
    std::string out;
    for (const auto& e : rt.journal())
        if (e.kind == kind) out += " | " + e.message;
    return out;
}

const DebugLocal* local(const DebugFrame& f, const std::string& name) {
    for (const auto& l : f.locals)
        if (l.name == name) return &l;
    return nullptr;
}

std::string stackText(const DebugHit& h) {
    std::string out;
    for (const auto& f : h.stack) out += (out.empty() ? "" : " > ") + f.label + (f.line ? ":" + std::to_string(f.line) : std::string{});
    return out;
}

DebugBreakpoint at(std::uint32_t id, Id scriptId, Id fn, std::uint32_t line, const std::string& condition = {}) {
    DebugBreakpoint b;
    b.id = id;
    b.script = scriptId;
    b.function = fn;
    b.line = line;
    b.condition = condition;
    return b;
}

// Le projet des exemples de la fiche : Statistiques_Pression et Moyenne_Pression.
const char* kStats =
    "VAR\n"                                          // 1
    "    P : REAL;\n"                                // 2
    "    Tours : INT;\n"                             // 3
    "    Mini : REAL := 100.0;\n"                    // 4
    "    Maxi : REAL;\n"                             // 5
    "END_VAR\n"                                      // 6
    "(* les statistiques de pression *)\n"           // 7
    "P := Pression;\n"                               // 8
    "Tours := Tours + 1;\n"                          // 9
    "IF P < Mini THEN\n"                             // 10
    "    Mini := P;\n"                               // 11
    "END_IF;\n"                                      // 12
    "\n"                                             // 13
    "Moy := Moyenne_Pression(P, Pression2);\n"       // 14
    "R := Mini;\n";                                  // 15

const char* kMoyenne =
    "VAR\n"                                          // 1
    "    Poids_A : REAL := 0.5;\n"                   // 2
    "    Somme : REAL;\n"                            // 3
    "END_VAR\n"                                      // 4
    "Somme := A * Poids_A + B * (1.0 - Poids_A);\n"  // 5
    "Moyenne_Pression := Somme;\n";                  // 6

struct Bench {
    Project p;
    Runtime rt;
    Id stats{kNoId}, moyenne{kNoId};
    Bench() {
        var(p, "Pression", "REAL", "3.2");
        var(p, "Pression2", "REAL", "4.0");
        var(p, "Moy", "REAL", "0.0");
        var(p, "R", "REAL", "0.0");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        stats = script(p, "Statistiques_Pression", kStats);
        moyenne = function(p, "Moyenne_Pression", "REAL", {{"A", PassMode::In}, {"B", PassMode::In}}, "REAL", kMoyenne);
    }
    void go() {
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
    }
};

// ---- un point d'arret : la pause reelle, la pile, les locales, rien d'ecrit avant Continuer ----
void pauseAndLocals() {
    std::printf("-- le point d'arret : la pause au milieu du script, ses locales, rien d'ecrit avant Continuer\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    check(b.rt.breakpoints().size() == 1 && b.rt.breakpoints()[0].effectiveLine == 10 && b.rt.breakpoints()[0].note.empty(),
          "le point de la ligne 10 est compris (ligne 10, sans note) : " + b.rt.breakpoints()[0].note);
    std::string err;
    const bool ok = b.rt.callScript("Statistiques_Pression", 0.2, &err);
    check(ok, "l'appel du script rend la main pendant la pause : " + err);
    check(b.rt.paused(), "le moteur est en pause");
    const auto& hit = b.rt.debugHit();
    check(hit.has_value() && hit->line == 10 && hit->why == DebugHit::Why::Breakpoint && hit->breakpoint == 1,
          "en pause au point 1, ligne 10");
    if (!hit) return;
    check(hit->where == "script Statistiques_Pression, ligne 10", "ou : " + hit->where);
    check(hit->stack.size() == 2 && hit->stack.back().label == "script Statistiques_Pression", "la pile : " + stackText(*hit));
    const auto& top = hit->stack.back();
    const auto* p = local(top, "P");
    const auto* t = local(top, "Tours");
    const auto* m = local(top, "Mini");
    check(p && p->value == "3.2" && p->type == "REAL", "la locale P = 3.2 (REAL) : " + (p ? p->value + " " + p->type : std::string("absente")));
    check(t && t->value == "1", "la locale Tours = 1");
    check(m && m->value == "100", "Mini n'a pas encore change (100) : " + (m ? m->value : std::string("absente")));
    bool sawP = false;
    for (const auto& [name, v] : hit->values) sawP = sawP || (name == "P" && v == "3.2");
    check(sawP, "les valeurs de la ligne disent P = 3.2");
    check(b.rt.breakpoints()[0].hits == 1, "un passage compte");
    check(value(b.rt, "R") == 0.0, "rien n'est ecrit tant qu'on ne continue pas (R = 0)");
    // Le nom sous le curseur, a l'instant de la pause : la locale du script.
    std::string shown, why;
    check(b.rt.debugEvaluate("P * 2.0", shown, &why) && shown == "6.4", "en pause, P * 2.0 se lit dans la portee de la ligne : " + shown + why);
    // L'IHM est figee : un tick ne fait pas de cycle.
    const auto before = b.rt.debugRevision();
    b.rt.tick(0.5);
    check(b.rt.paused() && b.rt.debugRevision() == before, "pendant la pause, le cycle IHM attend la reprise");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("arr\xC3\xAAt\xC3\xA9 au point 1 (script Statistiques_Pression, ligne 10)") != std::string::npos,
          "la Console dit l'arret : " + console(b.rt, "D\xC3\xA9" "bogage"));
    check(b.rt.debugContinue(), "Continuer");
    check(!b.rt.paused() && value(b.rt, "R") > 3.19 && value(b.rt, "R") < 3.21, "apres Continuer, le script va au bout (R = Mini = 3.2)");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("continu\xC3\xA9") != std::string::npos, "la Console dit continue");
    check(errors(b.rt).empty(), "aucune erreur" + errors(b.rt));
}

// ---- la condition : vraie seulement, francisee, un nom inconnu tout de suite ----
void conditions() {
    std::printf("-- la condition d'un point d'arret : dans la portee du script ; un nom inconnu se dit tout de suite\n");
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "P < 2.0 OR Tours MOD 100 = 0")});
        check(b.rt.breakpoints()[0].note.empty(), "la condition se lit : " + b.rt.breakpoints()[0].note);
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(!b.rt.paused() && value(b.rt, "R") > 3.0, "condition fausse (P = 3.2, Tours = 1) : pas d'arret");
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "P < 5.0 OR Tours MOD 100 = 0")});
        (void)b.rt.callScript("Statistiques_Pression", 0.3);
        check(b.rt.paused(), "condition vraie (P < 5.0) : arret");
        (void)b.rt.debugContinue();
    }
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "Presion < 5")});
        const auto& note = b.rt.breakpoints()[0].note;
        check(note.find("Presion n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici ; il ne s'arr\xC3\xAAtera pas") != std::string::npos,
              "un nom inconnu se dit tout de suite : " + note);
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(!b.rt.paused(), "... et il ne s'arrete pas");
    }
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "P < (")});
        check(b.rt.breakpoints()[0].note.rfind("condition invalide : elle ne se lit pas", 0) == 0,
              "une condition qui ne se lit pas : " + b.rt.breakpoints()[0].note);
    }
    {   // une variable IHM et une locale ensemble
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 11, "Pression2 > P")});
        check(b.rt.breakpoints()[0].note.empty(), "une variable IHM dans la condition est connue : " + b.rt.breakpoints()[0].note);
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(b.rt.paused() && b.rt.debugHit()->line == 11, "Pression2 > P : arret ligne 11");
        (void)b.rt.debugContinue();
    }
}

// ---- une ligne sans instruction : le point se decale, et le dit ----
void shifted() {
    std::printf("-- une ligne sans instruction : le point se decale sur la suivante\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 13), at(2, b.stats, kNoId, 40), at(3, b.stats, kNoId, 7)});
    const auto& bps = b.rt.breakpoints();
    check(bps[0].effectiveLine == 14 && bps[0].note == "d\xC3\xA9" "cal\xC3\xA9 \xC3\xA0 la ligne 14 : la ligne 13 n'a pas d'instruction",
          "ligne 13 (vide) : " + bps[0].note);
    check(bps[1].effectiveLine == 0 && bps[1].note == "aucune instruction \xC3\xA0 partir de la ligne 40 : il ne s'arr\xC3\xAAtera pas",
          "ligne 40 : " + bps[1].note);
    check(bps[2].effectiveLine == 8, "ligne 7 (un commentaire) : la ligne 8");
    const auto lines = Runtime::breakableLines(b.p, b.stats, kNoId);
    check(lines == std::vector<std::uint32_t>{8, 9, 10, 11, 14, 15}, "les lignes ou un point peut se poser : 8 9 10 11 14 15");
    const auto fl = Runtime::breakableLines(b.p, kNoId, b.moyenne);
    check(fl == std::vector<std::uint32_t>{5, 6}, "... dans la fonction : 5 6");
}

// ---- le pas a pas a travers une fonction IHM simple ----
void stepping() {
    std::printf("-- le pas a pas : Entrer, Sortir, Suivant a travers Moyenne_Pression\n");
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(b.rt.paused() && b.rt.debugHit()->line == 14, "en pause sur l'appel (ligne 14)");
        check(b.rt.debugStep(DebugStep::Into), "Entrer");
        check(b.rt.paused(), "... en pause dans la fonction");
        const auto& hit = b.rt.debugHit();
        if (!hit) return;
        check(hit->function == b.moyenne && hit->line == 5 && hit->why == DebugHit::Why::Step, "Entrer : fonction Moyenne_Pression, ligne 5 : " + hit->where);
        check(hit->stack.size() == 3 && hit->stack[1].label == "script Statistiques_Pression" && hit->stack[1].line == 14
                  && hit->stack[2].label == "fonction Moyenne_Pression",
              "la pile : le script (ligne 14) > la fonction : " + stackText(*hit));
        const auto& top = hit->stack.back();
        const auto* a = local(top, "A");
        const auto* bb = local(top, "B");
        const auto* w = local(top, "Poids_A");
        check(a && a->value == "3.2" && a->parameter && bb && bb->value == "4" && w && w->value == "0.5" && local(top, "Somme"),
              "les locales de la fonction : A, B, Poids_A, Somme");
        check(!hit->stack[1].locals.empty() && local(hit->stack[1], "P"), "le niveau du script garde ses locales (P)");
        check(b.rt.debugStep(DebugStep::Over), "Suivant (dans la fonction)");
        check(b.rt.paused() && b.rt.debugHit()->function == b.moyenne && b.rt.debugHit()->line == 6, "Suivant : ligne 6 de la fonction");
        check(b.rt.debugStep(DebugStep::Out), "Sortir");
        check(b.rt.paused() && b.rt.debugHit()->script == b.stats && b.rt.debugHit()->line == 15, "Sortir : de retour dans le script, ligne 15");
        check(value(b.rt, "Moy") > 3.59 && value(b.rt, "Moy") < 3.61, "l'appel a rendu sa valeur (Moy = 3.6)");
        (void)b.rt.debugContinue();
        check(!b.rt.paused() && errors(b.rt).empty(), "fin, sans erreur" + errors(b.rt));
    }
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(b.rt.debugStep(DebugStep::Over), "Suivant sur l'appel");
        check(b.rt.paused() && b.rt.debugHit()->script == b.stats && b.rt.debugHit()->line == 15,
              "Suivant execute l'appel d'un trait : ligne 15 du script");
        check(b.rt.debugStep(DebugStep::Over), "Suivant a la derniere ligne");
        check(!b.rt.paused() && !b.rt.stepping() && value(b.rt, "R") > 3.0, "le script est fini : l'IHM continue");
        check(console(b.rt, "D\xC3\xA9" "bogage").find("est fini : l'IHM continue") != std::string::npos, "la Console le dit");
    }
    {   // un point dans la fonction, sa condition sur un parametre
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, kNoId, b.moyenne, 1, "A > 3.0")});
        check(b.rt.breakpoints()[0].effectiveLine == 5, "le point de la ligne 1 de la fonction se decale a la ligne 5");
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(b.rt.paused() && b.rt.debugHit()->function == b.moyenne, "un point dans la fonction : arret a chaque appel (A > 3.0)");
        (void)b.rt.debugContinue();
    }
}

// ---- une fonction aux types riches (elle tourne dans le script) et une fonction interne ----
void richAndInner() {
    std::printf("-- une fonction aux types riches, une fonction interne : leurs lignes a elles\n");
    Project p;
    var(p, "Total", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id ajouter = function(p, "Ajouter", "", {{"Cumul", PassMode::InOut}, {"X", PassMode::In}}, "REAL",
                                "Cumul := Cumul + X;\n"      // 1
                                "Cumul := Cumul * 1.0;\n");  // 2
    const Id s = script(p, "Cumuls",
                        "FUNCTION Double : REAL\n"           // 1
                        "VAR_INPUT x : REAL; END_VAR\n"      // 2
                        "    Double := x * 2.0;\n"           // 3
                        "END_FUNCTION\n"                     // 4
                        "Ajouter(Total, 1.5);\n"             // 5
                        "Total := Double(Total);\n");        // 6
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, kNoId, ajouter, 2), at(2, s, kNoId, 3)});
    check(rt.breakpoints()[0].note.empty() && rt.breakpoints()[1].note.empty(), "les deux points se comprennent");
    (void)rt.callScript("Cumuls", 0.2);
    check(rt.paused() && rt.debugHit()->function == ajouter && rt.debugHit()->line == 2, "arret dans Ajouter (types riches), ligne 2");
    if (rt.debugHit()) {
        const auto& top = rt.debugHit()->stack.back();
        check(top.label == "fonction Ajouter" && local(top, "X") && local(top, "X")->value == "1.5", "la pile nomme Ajouter, ses locales (X = 1.5)");
        // 1.13.0 : les valeurs de la ligne sont celles de la ligne arretee (Cumul), pas celles du script appelant.
        bool cumul = false, caller = false;
        for (const auto& [name, val] : rt.debugHit()->values) {
            cumul = cumul || name == "Cumul";
            caller = caller || name == "Total";
        }
        check(cumul && !caller, "les valeurs de la ligne : Cumul (la ligne 2 d'Ajouter), pas Total (celle du script)");
    }
    (void)rt.debugContinue();
    check(rt.paused() && rt.debugHit()->script == s && rt.debugHit()->line == 3, "arret dans la fonction interne Double, ligne 3");
    if (rt.debugHit()) {
        check(rt.debugHit()->stack.back().label.find("Double (fonction interne") == 0, "le niveau dit la fonction interne : " + stackText(*rt.debugHit()));
        bool x = false;
        for (const auto& [name, val] : rt.debugHit()->values) x = x || (name == "x" && val == "1.5");
        check(x, "les valeurs de la ligne 3 de Double : x = 1.5");
    }
    (void)rt.debugContinue();
    check(!rt.paused() && value(rt, "Total") == 3.0 && errors(rt).empty(), "Total = (0 + 1.5) * 2 = 3" + errors(rt));
}

// ---- IHM_POINT_ARRET ----
void codeBreak() {
    std::printf("-- IHM_POINT_ARRET : sans argument a coup sur, avec un BOOL seulement s'il est vrai\n");
    Project p;
    var(p, "N", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    (void)script(p, "Boucle",
                 "VAR f : INT; END_VAR\n"                     // 1
                 "FOR f := 1 TO 5 DO\n"                       // 2
                 "    IHM_POINT_ARRET(f = 4);\n"              // 3
                 "    N := f;\n"                              // 4
                 "END_FOR;\n"                                 // 5
                 "IHM_POINT_ARRET;\n");                       // 6
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.tick(1.3);                                             // le debogueur voit IHM_POINT_ARRET dans le projet
    (void)rt.callScript("Boucle", 1.4);
    check(rt.paused() && rt.debugHit()->why == DebugHit::Why::Code && rt.debugHit()->line == 3 && value(rt, "N") == 3,
          "IHM_POINT_ARRET(f = 4) s'arrete au 4e tour (N = 3)");
    if (rt.debugHit()) {
        const auto* f = local(rt.debugHit()->stack.back(), "f");
        check(f && f->value == "4", "le compteur du FOR : f = 4");
    }
    (void)rt.debugContinue();
    check(rt.paused() && rt.debugHit()->line == 6 && value(rt, "N") == 5, "IHM_POINT_ARRET sans argument : arret a la ligne 6");
    (void)rt.debugContinue();
    check(!rt.paused() && errors(rt).empty(), "fin sans erreur" + errors(rt));
    // Sans le debogueur (le poste) : rien.
    Runtime station;
    station.bind(&p, nullptr);
    station.start(0.0);
    station.tick(0.1);
    (void)station.callScript("Boucle", 0.2);
    check(!station.paused() && value(station, "N") == 5 && errors(station).empty(), "hors simulation (le poste), IHM_POINT_ARRET ne fait rien");
}

// ---- pas d'arret dans une fonction appelee par une expression ----
void notInExpression() {
    std::printf("-- une fonction appelee par une expression (fx) ne s'arrete pas\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, kNoId, b.moyenne, 5)});
    const auto r = Expression::compile("Moyenne_Pression(1.0, 3.0)").evaluate(b.rt.environment());
    check(r && r->asReal() == 2.0, "l'expression rend sa valeur tout de suite (2.0)");
    check(!b.rt.paused(), "pas de pause");
    check(b.rt.breakpoints()[0].note.find("appel\xC3\xA9" "e par une expression : pas d'arr\xC3\xAAt") != std::string::npos,
          "le point le dit : " + b.rt.breakpoints()[0].note);
}

// ---- un script detache arrete dans sa boucle d'attente : Suivant enjambe le WAIT ----
void detached() {
    std::printf("-- un script detache, arrete dans sa boucle d'attente : Suivant enjambe le WAIT, l'IHM respire\n");
    Project p;
    var(p, "Niveau", "REAL", "0.0");
    var(p, "Consigne_Niveau", "REAL", "50.0");
    var(p, "Vanne_Entree", "BOOL", "FALSE");
    var(p, "Battements", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    (void)script(p, "Remplissage",
                 "Vanne_Entree := TRUE;\n"                    // 1
                 "WHILE Niveau < Consigne_Niveau DO\n"        // 2
                 "    IHM_POINT_ARRET;\n"                     // 3
                 "    WAIT(T#500ms);\n"                       // 4
                 "END_WHILE;\n"                               // 5
                 "Vanne_Entree := FALSE;\n", true);           // 6
    Script beat;
    beat.id = p.allocate();
    beat.name = "Battement";
    beat.event = "Cyclique";
    beat.periodMs = 100;
    beat.body = "Battements := Battements + 1;\n";
    p.programs.scripts.push_back(beat);
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(1.2);
    (void)rt.callScript("Remplissage", 1.3);
    check(rt.paused() && rt.debugHit()->line == 3 && rt.debugHit()->detached, "le script detache s'arrete a IHM_POINT_ARRET (ligne 3)");
    if (rt.debugHit()) check(rt.debugHit()->stack.front().label == "script d\xC3\xA9tach\xC3\xA9 Remplissage", "le bas de la pile : " + stackText(*rt.debugHit()));
    check(rt.debugStep(DebugStep::Over) && rt.paused() && rt.debugHit()->line == 4, "Suivant : la ligne du WAIT (4)");
    const double beats = value(rt, "Battements");
    check(rt.debugStep(DebugStep::Over) && !rt.paused(), "Suivant enjambe le WAIT : le script attend, pas de pause");
    rt.tick(1.5);
    check(!rt.paused() && value(rt, "Battements") > beats, "pendant l'attente, l'IHM respire (les cycles tournent)");
    rt.tick(1.9);
    check(rt.paused() && rt.debugHit()->line == 3 && rt.debugHit()->why == DebugHit::Why::Step, "apres le WAIT, le moteur se rearrete a la ligne d'apres (3)");
    // L'arret de la simulation pendant la pause : le script s'arrete sans erreur.
    rt.stop(2.0);
    check(!rt.paused() && !rt.scriptRunning("Remplissage") && errors(rt).empty(), "Arreter pendant la pause : sans erreur" + errors(rt));
}

// ---- le code change pendant la pause ----
void changedWhilePaused() {
    std::printf("-- le script change pendant la pause : il s'arrete la, sans erreur\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    check(b.rt.paused(), "en pause");
    for (auto& sc : b.p.programs.scripts)
        if (sc.id == b.stats) sc.body += "R := 99.0;\n";
    (void)b.rt.debugContinue();
    check(!b.rt.paused() && value(b.rt, "R") == 0.0, "il ne reprend pas au milieu d'un code qui a change (R = 0)");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("a chang\xC3\xA9 pendant la pause : son ex\xC3\xA9" "cution s'arr\xC3\xAAte l\xC3\xA0, sans erreur")
              != std::string::npos,
          "la Console le dit" + console(b.rt, "D\xC3\xA9" "bogage"));
    check(errors(b.rt).empty(), "sans erreur" + errors(b.rt));
}

// ---- un script C : non execute en simulation ----
void cScript() {
    std::printf("-- un script C ou C++ : pas de point d'arret (non execute en simulation)\n");
    Bench b;
    const Id c = script(b.p, "En_C", "x = 1;\n", false, ScriptLang::C);
    b.go();
    b.rt.setBreakpoints({at(1, c, kNoId, 1)});
    check(b.rt.breakpoints()[0].note.find("non ex\xC3\xA9" "cut\xC3\xA9 en simulation (C/C++)") != std::string::npos,
          "la note : " + b.rt.breakpoints()[0].note);
    check(Runtime::breakableLines(b.p, c, kNoId).empty(), "aucune ligne a poser");
}

// ---- sans point arme, rien ne change ----
void zeroCost() {
    std::printf("-- sans point arme ni pas demande, le script tourne comme avant (d'un trait, sur le fil de l'IHM)\n");
    Bench b;
    b.go();
    std::string err;
    check(b.rt.callScript("Statistiques_Pression", 0.2, &err) && !b.rt.paused() && value(b.rt, "R") > 3.0, "le script va au bout" + err);
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    DebugBreakpoint off = b.rt.breakpoints()[0];
    off.enabled = false;
    b.rt.setBreakpoints({off});
    (void)b.rt.callScript("Statistiques_Pression", 0.3);
    check(!b.rt.paused(), "un point desactive ne s'arrete pas");
}

// ---- le debogueur de l'automate dit les memes mots (DebugShared) ----
void sharedWords() {
    std::printf("-- les mots partages avec le debogueur de l'automate\n");
    check(sim::debug::shiftedNote(43, 42) == "d\xC3\xA9" "cal\xC3\xA9 \xC3\xA0 la ligne 43 : la ligne 42 n'a pas d'instruction", "decale");
    check(sim::debug::noStatementNote(9) == "aucune instruction \xC3\xA0 partir de la ligne 9 : il ne s'arr\xC3\xAAtera pas", "aucune instruction");
    check(sim::debug::invalidCondition("division par z\xC3\xA9ro") == "condition invalide : division par z\xC3\xA9ro ; il ne s'arr\xC3\xAAtera pas",
          "condition invalide");
    check(sim::debug::effectiveLine({3, 5, 9}, 4) == 5 && sim::debug::effectiveLine({3, 5, 9}, 10) == 0 && sim::debug::effectiveLine({3}, 0) == 3,
          "la ligne effective");
    // Le ST de l'automate ne connait pas IHM_POINT_ARRET.
    auto plc = sim::parse("IHM_POINT_ARRET();\n", "Section");
    check(static_cast<bool>(plc), "le ST de l'automate lit l'appel comme un appel quelconque (sans le connaitre)");
}


// =============================================================================
//  1.13.0 (seconde passe) : la pause fige TOUT le travail en cours ; les codes
//  copies (fonctions de vue) ; les gestes pendant la pause ; les actions qui
//  s'arretent ; les espions qui ne modifient rien ; l'arret sous TRY ; les
//  valeurs de la ligne ; les methodes ; ENTRE ; une condition non BOOL ; le cout.
// =============================================================================

Script& addViewScript(Project& p, View& v, const char* event, const std::string& body) {
    Script sc;
    sc.id = p.allocate();
    sc.name = event;
    sc.event = event;
    sc.body = body;
    v.scripts.push_back(sc);
    return v.scripts.back();
}

Script& addGeneral(Project& p, const char* name, const char* event, const std::string& body, int periodMs = 100) {
    Script sc;
    sc.id = p.allocate();
    sc.name = name;
    sc.event = event;
    sc.periodMs = periodMs;
    sc.body = body;
    p.programs.scripts.push_back(sc);
    return p.programs.scripts.back();
}

Action action(Trigger t, Operation o, std::string target = {}, std::string value = {}, std::string key = {}) {
    Action a;
    a.trigger = t;
    a.operation = o;
    a.target = std::move(target);
    a.value = std::move(value);
    a.key = std::move(key);
    return a;
}

// ---- T2-1, T2-2, T2-13 : une fonction de vue (une copie liee) : le point s'y arrete, la pile la nomme ; en pause,
// une touche relachee ou un glissement ne la liberent pas ----
void viewFunction() {
    std::printf("-- une fonction de vue : son point s'arrete, la pile la nomme ; en pause, rien ne la libere\n");
    Project p;
    var(p, "Niveau", "REAL", "10.0");
    var(p, "R", "REAL", "0.0");
    View v = makeView(p, "Vue_Four");
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Pourcent";
    f.returnType = "REAL";
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Parameter;
    d.name = "x";
    d.type = "REAL";
    d.mode = PassMode::In;
    f.decls.push_back(d);
    f.body = "Pourcent := x * 2.0;\n"            // 1
             "Pourcent := Pourcent + 1.0;\n";    // 2
    const Id fn = f.id;
    v.functions.push_back(f);
    const Id cyc = addViewScript(p, v, "OnCycle", "R := R + Pourcent(Niveau);\n").id;
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.setBreakpoints({at(1, kNoId, fn, 2)});
    check(rt.breakpoints()[0].note.empty() && rt.breakpoints()[0].effectiveLine == 2, "le point de Pourcent se comprend : " + rt.breakpoints()[0].note);
    rt.tick(0.1);
    check(rt.paused(), "le point pose dans la fonction de la vue s'arrete (l'OnCycle l'appelle)");
    if (!rt.paused() || !rt.debugHit()) return;
    const auto& hit = *rt.debugHit();
    check(hit.function == fn && hit.line == 2 && hit.where == "fonction Vue_Four.Pourcent, ligne 2", "ou : " + hit.where);
    check(hit.stack.back().label == "fonction Vue_Four.Pourcent" && hit.stack.back().function == fn, "la pile la nomme : " + stackText(hit));
    check(rt.breakpoints()[0].hits == 1, "le passage compte (il s'est arrete)");
    // En pause : une touche relachee (le relachement de F10 arrive au canevas), un glissement - rien ne se vide.
    check(!rt.keyUp("F10", 0.15), "en pause, une touche relachee (non tenue) ne fait rien");
    std::string why;
    check(!rt.swipe(1, 0.16, &why) && why == "l'IHM est en pause (d\xC3\xA9" "bogage) : Continuer (F5) d'abord", "en pause, glisser est refuse : " + why);
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(value(rt, "R") == 21.0 && errors(rt).empty(), "la fonction rend sa valeur (R = 10 * 2 + 1 = 21)" + errors(rt));

    // Entrer depuis le script de la vue : la fonction copiee se montre par son nom et sa ligne ; des touches relachees a
    // chaque pas ne la liberent pas, la valeur rendue arrive.
    rt.setBreakpoints({at(1, cyc, kNoId, 1)});
    rt.tick(0.3);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->where == "Vue_Four.OnCycle, ligne 1",
          "en pause dans l'OnCycle de la vue : " + (rt.debugHit() ? rt.debugHit()->where : std::string("-")));
    check(rt.debugStep(DebugStep::Into) && rt.paused() && rt.debugHit() && rt.debugHit()->where == "fonction Vue_Four.Pourcent, ligne 1",
          "Entrer : dans Pourcent, ligne 1 : " + (rt.debugHit() ? rt.debugHit()->where : std::string("-")));
    (void)rt.keyUp("F11", 0.31);
    check(rt.debugStep(DebugStep::Over) && rt.paused() && rt.debugHit() && rt.debugHit()->line == 2, "Suivant : ligne 2 de Pourcent");
    (void)rt.keyUp("F10", 0.32);
    (void)rt.swipe(-1, 0.33);
    check(rt.debugStep(DebugStep::Over) && !rt.paused(), "Suivant : la fonction rend la main, l'OnCycle finit");
    check(value(rt, "R") == 42.0 && errors(rt).empty(), "la valeur rendue n'est pas perdue (R = 21 + 21)" + errors(rt));
}

// ---- T2-3, T2-14 : la pause fige le reste du cycle (les autres scripts, l'OnCycle) ; le point d'un autre script
// s'arrete a son tour ----
void cycleFrozen() {
    std::printf("-- la pause fige le reste du cycle : les autres scripts et l'OnCycle attendent la reprise\n");
    Project p;
    var(p, "Calcul", "REAL", "0.0");
    var(p, "Consigne", "REAL", "0.0");
    var(p, "Vu", "REAL", "0.0");
    var(p, "Tours_B", "REAL", "0.0");
    var(p, "Tours_Vue", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    addViewScript(p, v, "OnCycle", "Tours_Vue := Tours_Vue + 1.0;\n");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id a = addGeneral(p, "A", "Cyclique", "Calcul := Calcul + 1.0;\n"     // 1
                                                 "Consigne := Calcul;\n").id;  // 2
    const Id b = addGeneral(p, "B", "Cyclique", "Vu := Consigne;\n"            // 1
                                                 "Tours_B := Tours_B + 1.0;\n").id;   // 2
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.tick(0.2);                                          // les cycliques ont tourne une fois au moins
    const double toursB = value(rt, "Tours_B"), toursVue = value(rt, "Tours_Vue"), consigne = value(rt, "Consigne");
    check(toursB >= 1.0 && toursVue >= 1.0, "sans point, A, B et l'OnCycle tournent a chaque cycle");
    rt.setBreakpoints({at(1, a, kNoId, 2), at(2, b, kNoId, 1)});
    rt.tick(0.35);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->script == a && rt.debugHit()->line == 2, "A s'arrete ligne 2");
    check(value(rt, "Tours_B") == toursB && value(rt, "Tours_Vue") == toursVue,
          "B et l'OnCycle de la vue attendent (rien n'a tourne apres A) : B " + std::to_string(value(rt, "Tours_B")) + ", vue "
              + std::to_string(value(rt, "Tours_Vue")));
    check(rt.breakpoints()[1].hits == 0 && rt.breakpoints()[1].note.empty(), "le point de B n'a ni passage ni note : B n'a pas tourne");
    check(rt.debugContinue(), "Continuer");
    check(rt.paused() && rt.debugHit() && rt.debugHit()->script == b, "puis B s'arrete a son point (le meme cycle reprend)");
    check(value(rt, "Consigne") == consigne + 1.0 && value(rt, "Tours_Vue") == toursVue, "A a fini (Consigne + 1) ; l'OnCycle attend encore");
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(value(rt, "Vu") == consigne + 1.0 && value(rt, "Tours_B") == toursB + 1.0 && value(rt, "Tours_Vue") == toursVue + 1.0,
          "B lit la Consigne finie de A, puis l'OnCycle tourne (une fois chacun)");
    check(rt.breakpoints()[0].hits == 1 && rt.breakpoints()[1].hits == 1, "un passage chacun");
    check(errors(rt).empty(), "sans erreur" + errors(rt));
}

// ---- T2-49 (et T2-3) : un geste a deux actions, le demarrage : la suite attend la fin du script en pause ----
void gestureFrozen() {
    std::printf("-- un clic a deux actions, le demarrage : la suite attend la fin du script en pause\n");
    {
        Project p;
        var(p, "X", "INT", "0");
        var(p, "Commande", "INT", "0");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        View& shown = p.views.back();
        p.config.startView = shown.id;
        const Id btn = edit::add(p, shown, Kind::Button, 10, 10);
        shown.object(btn)->name = "Btn_Recette";
        shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Preparer_Recette"));
        shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Assign, "Commande", "X"));
        const Id prep = addGeneral(p, "Preparer_Recette", "Appel", "X := 5;\n"           // 1
                                                                   "X := X + 1;\n").id;  // 2
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
        rt.setBreakpoints({at(1, prep, kNoId, 2)});
        rt.press(btn, 0.2);
        rt.release(btn, 0.25, true);
        check(rt.paused() && rt.debugHit() && rt.debugHit()->script == prep, "le clic s'arrete dans Preparer_Recette, ligne 2");
        if (rt.debugHit()) check(rt.debugHit()->stack.front().label.find("Btn_Recette") != std::string::npos, "le bas de la pile : le clic : " + stackText(*rt.debugHit()));
        check(value(rt, "Commande") == 0.0, "l'action suivante du clic (Commande := X) attend");
        check(rt.debugContinue() && !rt.paused(), "Continuer");
        check(value(rt, "X") == 6.0 && value(rt, "Commande") == 6.0, "puis elle part, avec la valeur que le script a finie (6)");
        check(errors(rt).empty(), "sans erreur" + errors(rt));
    }
    {   // un script de Demarrage : la premiere vue et son OnOpen attendent
        Project p;
        var(p, "Init", "INT", "0");
        var(p, "Ouvert", "INT", "0");
        View v = makeView(p, "Vue_Essai");
        addViewScript(p, v, "OnOpen", "Ouvert := Init;\n");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        const Id init = addGeneral(p, "Initialiser", "Demarrage", "Init := 1;\n"     // 1
                                                                  "Init := 2;\n").id; // 2
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.setBreakpoints({at(1, init, kNoId, 2)});
        rt.start(0.0);
        check(rt.paused() && rt.debugHit() && rt.debugHit()->script == init, "le script de Demarrage s'arrete ligne 2");
        check(value(rt, "Ouvert") == 0.0, "la premiere vue n'est pas encore ouverte (son OnOpen attend)");
        check(rt.debugContinue() && !rt.paused(), "Continuer");
        check(value(rt, "Ouvert") == 2.0, "puis la vue s'ouvre, son OnOpen lit Init = 2");
    }
}

// ---- T2-4 : en pause, les gestes ne font rien : la touche relachee attend la reprise, la croix d'une popup, glisser ----
void gesturesWhilePaused() {
    std::printf("-- en pause : la touche relachee attend la reprise, la croix d'une popup et glisser ne font rien\n");
    Project p;
    var(p, "Marche", "BOOL", "FALSE");
    var(p, "Pas", "INT", "0");
    var(p, "Ferme", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    View pop = makeView(p, "Pop_Pas");
    pop.role = "popup";
    pop.popup.modal = false;
    pop.actions.push_back(action(Trigger::KeyPress, Operation::CallScript, "Marche_Pas_A_Pas", {}, "F2"));
    pop.actions.push_back(action(Trigger::KeyRelease, Operation::Reset, "Marche", {}, "F2"));
    addViewScript(p, pop, "OnClose", "Ferme := Ferme + 1;\n");
    const Id popId = pop.id;
    p.views = {v, pop};
    p.config.startView = p.views.front().id;
    const Id pas = addGeneral(p, "Marche_Pas_A_Pas", "Appel", "Marche := TRUE;\n"     // 1
                                                              "Pas := Pas + 1;\n").id; // 2
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    check(rt.openPopup(popId, Transition{}, 0.12) && rt.popups().size() == 1, "la popup est ouverte");
    rt.setBreakpoints({at(1, pas, kNoId, 2)});
    check(rt.keyDown(*keys::parseChord("F2"), 0.2), "F2 enfoncee : la popup la prend");
    check(rt.paused() && value(rt, "Marche") == 1.0, "le script lance par F2 s'arrete ligne 2 (Marche = TRUE)");
    check(rt.keyUp("F2", 0.25), "F2 relachee pendant la pause : la touche est prise...");
    check(value(rt, "Marche") == 1.0, "... mais Touche relachee ne part pas pendant la pause (Marche reste TRUE)");
    check(!rt.closePopupAt(0, Transition{}, 0.3) && rt.popups().size() == 1 && value(rt, "Ferme") == 0.0,
          "la croix de la popup ne ferme rien pendant la pause (ni OnClose)");
    std::string why;
    check(!rt.swipe(1, 0.31, &why) && !why.empty(), "glisser ne change pas de vue pendant la pause : " + why);
    check(rt.debugContinue() && !rt.paused() && value(rt, "Pas") == 1.0, "Continuer : le script finit");
    // 1.13.0 (D1b-12) : la pause finie, le relachement garde part tout de suite (apres la fin du geste, dans l'ordre).
    check(value(rt, "Marche") == 0.0 && !rt.keyHeld("F2"), "la reprise finie, Touche relachee part (Marche = FALSE), F2 n'est plus tenue");
    rt.tick(0.5);
    check(value(rt, "Marche") == 0.0, "le tick suivant ne la rejoue pas une seconde fois");
    check(rt.closePopupAt(0, Transition{}, 0.6) && rt.popups().empty() && value(rt, "Ferme") == 1.0, "hors pause, la croix ferme (OnClose)");
    check(errors(rt).empty(), "sans erreur" + errors(rt));
}

// ---- T2-5, T2-20 : les actions Appeler et Execute du ST ecrit ici s'arretent (un point, IHM_POINT_ARRET) ----
void actionsStop() {
    std::printf("-- les actions Appeler et Execute du ST ecrit ici s'arretent comme un script\n");
    Bench b;
    View& shown = b.p.views.back();
    const Id btn = edit::add(b.p, shown, Kind::Button, 10, 10);
    shown.object(btn)->name = "Btn_Moyenne";
    shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Call, "Moyenne_Pression", "1.0, 3.0"));
    shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Assign, "R", "77.0"));
    const Id code = edit::add(b.p, shown, Kind::Button, 10, 60);
    shown.object(code)->name = "Btn_Code";
    shown.object(code)->actions.push_back(action(Trigger::Click, Operation::RunScript, {}, "Moy := 1.0;\nIHM_POINT_ARRET;\nMoy := 2.0;\n"));
    b.go();
    b.rt.setBreakpoints({at(1, kNoId, b.moyenne, 5)});
    b.rt.press(btn, 0.2);
    b.rt.release(btn, 0.25, true);
    check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->function == b.moyenne && b.rt.debugHit()->line == 5,
          "Appeler Moyenne_Pression : arret ligne 5 de la fonction");
    check(b.rt.breakpoints()[0].note.empty(), "le point ne dit aucune fausse raison : " + b.rt.breakpoints()[0].note);
    check(value(b.rt, "R") == 0.0, "l'action suivante du clic attend");
    check(b.rt.debugContinue() && !b.rt.paused() && value(b.rt, "R") == 77.0, "Continuer : la fonction finit, puis l'action suivante");
    // Sans point arme : IHM_POINT_ARRET dans le code d'une action (le debogueur le voit dans le projet).
    b.rt.setBreakpoints({});
    b.rt.tick(1.4);
    b.rt.press(code, 1.5);
    b.rt.release(code, 1.55, true);
    check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->why == DebugHit::Why::Code && b.rt.debugHit()->line == 2,
          "IHM_POINT_ARRET dans le code de l'action : arret ligne 2");
    if (b.rt.debugHit()) check(b.rt.debugHit()->where.find("Btn_Code") != std::string::npos, "ou : " + b.rt.debugHit()->where);
    check(value(b.rt, "Moy") == 1.0, "rien n'est ecrit apres la ligne (Moy = 1)");
    check(b.rt.debugContinue() && value(b.rt, "Moy") == 2.0, "Continuer : le code finit (Moy = 2)");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("hors d") == std::string::npos, "aucune fausse raison a la Console");
    check(errors(b.rt).empty(), "sans erreur" + errors(b.rt));
}

// ---- T2-6 : un espion ou une condition ne modifie rien ; une fonction en echec dans un espion ne fait pas echouer le script ----
void probesReadOnly() {
    std::printf("-- un espion, une condition : en lecture seule ; une faute d'espion ne reste pas au script\n");
    Bench b;
    var(b.p, "Historique", "LIST OF REAL", "");
    var(b.p, "Taille", "INT", "0");
    var(b.p, "Fin", "INT", "0");
    (void)function(b.p, "Ajoute_Histo", "INT", {}, "REAL", "LIST_ADD(Historique, 1.0);\nAjoute_Histo := 1;\n");
    const Id s = script(b.p, "Avec_Pile",
                        "VAR\n"                                  // 1
                        "    Pile : VECTOR OF INT;\n"            // 2
                        "    Index : INT;\n"                     // 3
                        "END_VAR\n"                              // 4
                        "VECTOR_PUSH(Pile, 1);\n"                // 5
                        "VECTOR_PUSH(Pile, 2);\n"                // 6
                        "Index := Index + 1;\n"                  // 7
                        "Taille := VECTOR_SIZE(Pile);\n"         // 8
                        "Fin := Index;\n");                      // 9
    b.go();
    b.rt.setBreakpoints({at(1, s, kNoId, 8)});
    std::string err;
    (void)b.rt.callScript("Avec_Pile", 0.2, &err);
    check(b.rt.paused(), "en pause ligne 8");
    std::string shown, why;
    check(!b.rt.debugEvaluate("VECTOR_POP(Pile)", shown, &why) && why.find("lecture seule") != std::string::npos,
          "l'espion VECTOR_POP(Pile) est refuse (lecture seule) : " + shown + why);
    check(b.rt.debugEvaluate("VECTOR_SIZE(Pile)", shown, &why) && shown == "2", "la pile garde ses 2 elements : " + shown + why);
    check(!b.rt.debugEvaluate("Moyenne_Pression(P, 0, 1)", shown, &why), "l'espion d'une fonction qui echoue (trop d'arguments) : " + why);
    why.clear();
    shown.clear();
    const bool histo = b.rt.debugEvaluate("Ajoute_Histo()", shown, &why);
    check(!histo && why.find("lecture seule") != std::string::npos,
          "un espion qui ajoute a une LIST de l'IHM est refuse (lecture seule) : " + std::string(histo ? "rendu " : "refus ") + shown + why);
    shown.clear();
    check(b.rt.debugEvaluate("LIST_COUNT(Historique)", shown, &why) && shown == "0", "en pause, Historique reste vide : " + shown);
    check(console(b.rt, "Erreur").empty(), "rien a la Console" + console(b.rt, "Erreur"));
    check(b.rt.debugContinue() && !b.rt.paused(), "Continuer");
    check(value(b.rt, "Taille") == 2.0 && value(b.rt, "Fin") == 1.0, "le script finit comme sans debogueur (Taille = 2, Fin = 1)");
    check(errors(b.rt).empty(), "la faute de l'espion ne reste pas au script" + errors(b.rt));
    // Hors pause, l'espion ne l'ecrit pas non plus : la liste reste vide (un script la compte).
    (void)b.rt.debugEvaluate("Ajoute_Histo()", shown, &why);
    (void)script(b.p, "Compter", "Taille := LIST_COUNT(Historique);\n");
    (void)b.rt.callScript("Compter", 0.3, &err);
    check(value(b.rt, "Taille") == 0.0, "Historique reste vide (LIST_COUNT = 0) : " + err);
    // La condition d'un point qui ecrit (une fonction interne qui avance Index) : refusee, et dite ; le script inchange.
    const Id s2 = script(b.p, "Avec_Prochain",
                         "VAR Index : INT; END_VAR\n"            // 1
                         "FUNCTION Prochain : INT\n"             // 2
                         "    Index := Index + 1;\n"             // 3
                         "    Prochain := Index;\n"              // 4
                         "END_FUNCTION\n"                        // 5
                         "Index := 10;\n"                        // 6
                         "Fin := Index;\n");                     // 7
    b.rt.setBreakpoints({at(1, s2, kNoId, 7, "Prochain() > 5")});
    (void)b.rt.callScript("Avec_Prochain", 0.4, &err);
    check(!b.rt.paused() && value(b.rt, "Fin") == 10.0, "la condition n'avance pas Index (Fin = 10) : " + err);
    check(b.rt.breakpoints()[0].note.find("lecture seule") != std::string::npos, "le point le dit : " + b.rt.breakpoints()[0].note);
}

// ---- T2-7 : Arreter pendant une pause dans une fonction appelee sous TRY : ni CATCH ni suite ----
void stopUnderTry() {
    std::printf("-- Arreter pendant une pause dans une fonction appelee sous TRY : ni le CATCH ni la suite ne tournent\n");
    Project p;
    var(p, "Vanne", "BOOL", "TRUE");
    var(p, "Pompe", "BOOL", "FALSE");
    var(p, "Pris", "BOOL", "FALSE");
    var(p, "Cuve", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id remplir = function(p, "Remplir", "", {{"Quantite", PassMode::In}}, "REAL", "Cuve := Cuve + Quantite;\n");
    (void)script(p, "Sequence",
                 "TRY\n"                                    // 1
                 "    Remplir(5.0);\n"                      // 2
                 "CATCH e\n"                                // 3
                 "    Pris := TRUE;\n"                      // 4
                 "END_TRY;\n"                               // 5
                 "Vanne := FALSE;\n"                        // 6
                 "Pompe := TRUE;\n");                       // 7
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, kNoId, remplir, 1)});
    (void)rt.callScript("Sequence", 0.2);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == remplir, "en pause dans Remplir");
    rt.stop(0.3);
    check(!rt.paused(), "Arreter : plus de pause");
    check(value(rt, "Pris") == 0.0 && value(rt, "Vanne") == 1.0 && value(rt, "Pompe") == 0.0,
          "ni le CATCH ni la suite n'ont tourne (Pris faux, Vanne vraie, Pompe fausse)");
    check(errors(rt).empty(), "sans erreur" + errors(rt));
}

// ---- T2-9, T2-56 : IHM_POINT_ARRET dans une methode, sans autre point : arret, nomme et a sa ligne ----
void methodBreak() {
    std::printf("-- IHM_POINT_ARRET dans une methode : l'arret, son nom (T_Compteur.Incr) et sa ligne a elle\n");
    Project p;
    HmiType t;
    t.id = p.allocate();
    t.name = "T_Compteur";
    TypeMember n;
    n.name = "N";
    n.type = "INT";
    t.members.push_back(n);
    HmiFunction m;
    m.id = p.allocate();
    m.name = "Incr";
    m.body = "N := N + 1;\n"            // 1
             "IHM_POINT_ARRET;\n"       // 2
             "N := N + 10;\n";          // 3
    const Id mid = m.id;
    t.methods.push_back(m);
    p.programs.types.push_back(t);
    var(p, "c", "T_Compteur", "");
    var(p, "R", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    (void)script(p, "Essai", "c.Incr();\nR := c.N;\n");
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(1.3);                                          // le debogueur voit IHM_POINT_ARRET dans la methode
    (void)rt.callScript("Essai", 1.4);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->why == DebugHit::Why::Code, "IHM_POINT_ARRET dans la methode s'arrete");
    if (rt.debugHit()) {
        const auto& hit = *rt.debugHit();
        check(hit.line == 2 && hit.function == mid && hit.where == "m\xC3\xA9thode T_Compteur.Incr, ligne 2", "ou : " + hit.where);
        check(hit.stack.back().label == "m\xC3\xA9thode T_Compteur.Incr" && hit.stack.back().line == 2, "la pile : " + stackText(hit));
    }
    check(console(rt, "D\xC3\xA9" "bogage").find("hors d") == std::string::npos, "aucune fausse raison a la Console");
    (void)rt.debugContinue();
    check(!rt.paused() && value(rt, "R") == 11.0 && errors(rt).empty(), "la methode finit (R = 11)" + errors(rt));
}

// ---- T2-56 (a) : une condition se lit dans l'espace de noms de son code ----
void namespacedCondition() {
    std::printf("-- une condition se lit comme son code : son espace de noms d'abord\n");
    Bench b;
    HmiFunction root;
    root.id = b.p.allocate();
    root.name = "EnDefaut";
    root.returnType = "BOOL";
    root.body = "EnDefaut := FALSE;\n";
    b.p.programs.functions.push_back(root);
    HmiFunction gaz = root;
    gaz.id = b.p.allocate();
    gaz.ns = "Gaz";
    gaz.body = "EnDefaut := TRUE;\n";
    b.p.programs.functions.push_back(gaz);
    Script sc;
    sc.id = b.p.allocate();
    sc.name = "Surveiller";
    sc.event = "Appel";
    sc.ns = "Gaz";
    sc.body = "R := 1.0;\nR := 2.0;\n";
    b.p.programs.scripts.push_back(sc);
    b.go();
    b.rt.setBreakpoints({at(1, sc.id, kNoId, 2, "EnDefaut()")});
    check(b.rt.breakpoints()[0].note.empty(), "la condition se lit : " + b.rt.breakpoints()[0].note);
    (void)b.rt.callScript("Gaz.Surveiller", 0.2);
    check(b.rt.paused(), "EnDefaut() appelle Gaz.EnDefaut (vrai), comme le code du script : arret");
    std::string shown, why;
    check(b.rt.debugEvaluate("EnDefaut()", shown, &why) && shown == "TRUE", "l'espion aussi lit Gaz.EnDefaut : " + shown + why);
    (void)b.rt.debugContinue();
}

// ---- T2-10, T2-11 : ENTRE ... ET ... ; une condition qui n'est pas un BOOL ----
void conditionKinds() {
    std::printf("-- une condition ENTRE ... ET ... se lit ; une condition qui n'est pas un BOOL est refusee, et dite\n");
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "P ENTRE 2.0 ET 5.0")});
        check(b.rt.breakpoints()[0].note.empty(), "P ENTRE 2.0 ET 5.0 se comprend : " + b.rt.breakpoints()[0].note);
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(b.rt.paused(), "P = 3.2 est entre 2 et 5 : arret");
        (void)b.rt.debugContinue();
    }
    {
        Bench b;
        b.go();
        b.rt.setBreakpoints({at(1, b.stats, kNoId, 10, "Tours MOD 100")});
        (void)b.rt.callScript("Statistiques_Pression", 0.2);
        check(!b.rt.paused(), "Tours MOD 100 (un INT, le = 0 oublie) : pas d'arret");
        const auto& note = b.rt.breakpoints()[0].note;
        check(note.find("condition invalide : elle rend un") != std::string::npos && note.find("pas un BOOL ; il ne s'arr\xC3\xAAtera pas") != std::string::npos,
              "la note le dit : " + note);
        check(errors(b.rt).empty() && value(b.rt, "R") > 3.0, "le script va au bout" + errors(b.rt));
        std::string shown, why, type;
        check(b.rt.debugEvaluate("Pression * 2.0", shown, &why, &type) && type == "REAL", "debugEvaluate dit le type : " + type);
    }
}

// ---- T2-12 : IHM_POINT_ARRET en commentaire ne compte pas ; un script debogable reste mesure ; IHM_SCRIPT_ACTIF ----
void sameCost() {
    std::printf("-- sans point arme : IHM_POINT_ARRET en commentaire ne change rien ; les scripts restent mesures\n");
    Bench b;
    var(b.p, "Texte", "STRING", "''");
    (void)script(b.p, "Commente", "(* IHM_POINT_ARRET; *)\nTexte := 'IHM_POINT_ARRET';\n");
    var(b.p, "Actif", "BOOL", "TRUE");
    const Id act = script(b.p, "Calcul", "Actif := IHM_SCRIPT_ACTIF('Calcul');\n");
    b.go();
    b.rt.tick(1.3);                                        // le debogueur relit le projet
    const auto runsOf = [&](Id id) {
        const auto it = b.rt.perf().scripts.find(id);
        return it == b.rt.perf().scripts.end() ? std::size_t{0} : it->second.time.count;
    };
    const std::size_t before = runsOf(b.stats);
    check(!b.rt.paused(), "pas de pause");
    (void)b.rt.callScript("Statistiques_Pression", 1.4);
    check(runsOf(b.stats) == before + 1, "le script est mesure (Performances) : le commentaire ne compte pas");
    // Un point arme ailleurs : le script tourne sur une tache du debogueur, il reste mesure ; IHM_SCRIPT_ACTIF ne voit
    // que les scripts detaches.
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    const std::size_t before2 = runsOf(act);
    (void)b.rt.callScript("Calcul", 1.5);
    check(!b.rt.paused() && runsOf(act) == before2 + 1, "sur une tache du debogueur, le script est mesure aussi");
    check(value(b.rt, "Actif") == 0.0, "IHM_SCRIPT_ACTIF('Calcul') dans Calcul (non detache) : FALSE, comme sans debogueur");
    // Le temps d'une pause ne compte pas dans la mesure du script.
    const std::size_t before3 = runsOf(b.stats);
    (void)b.rt.callScript("Statistiques_Pression", 1.6);
    check(b.rt.paused(), "en pause");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    (void)b.rt.debugContinue();
    const auto it = b.rt.perf().scripts.find(b.stats);
    check(runsOf(b.stats) == before3 + 1 && it != b.rt.perf().scripts.end() && it->second.time.last < 200.0,
          "la pause (250 ms) ne compte pas dans la mesure : " + (it == b.rt.perf().scripts.end() ? std::string("-") : std::to_string(it->second.time.last)));
}

// ---- T2-21 : un point appele par une expression ne compte pas de passage ; T2-23 : une pause demandee s'annule ----
void quietCounts() {
    std::printf("-- un point appele par une expression ne compte aucun passage ; une pause demandee s'annule\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, kNoId, b.moyenne, 5)});
    (void)Expression::compile("Moyenne_Pression(1.0, 3.0)").evaluate(b.rt.environment());
    const auto rev = b.rt.debugRevision();
    for (int i = 0; i < 5; ++i) (void)Expression::compile("Moyenne_Pression(1.0, 3.0)").evaluate(b.rt.environment());
    check(b.rt.breakpoints()[0].hits == 0, "aucun passage compte (il ne s'arrete pas)");
    check(b.rt.debugRevision() == rev, "le debogueur ne change pas a chaque evaluation (le volet ne se refait pas)");
    // Pendant une pause, l'expression de la vue dit encore la bonne raison.
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10), at(2, kNoId, b.moyenne, 5)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    check(b.rt.paused(), "en pause dans le script");
    (void)Expression::compile("Moyenne_Pression(1.0, 3.0)").evaluate(b.rt.environment());
    check(b.rt.breakpoints()[1].note.find("appel\xC3\xA9" "e par une expression") != std::string::npos,
          "pendant la pause, la raison reste l'expression : " + b.rt.breakpoints()[1].note);
    b.rt.setBreakpoints({});
    while (b.rt.paused()) (void)b.rt.debugContinue();
    // Pause demandee, puis annulee : le script suivant va au bout.
    b.rt.debugPause();
    check(b.rt.pauseAsked(), "Pause demandee");
    const auto pauses = b.rt.pauseCount();
    check(b.rt.debugCancelPause() && !b.rt.pauseAsked(), "Annuler la pause");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("pause annul\xC3\xA9" "e : l'IHM continue sans s'arr\xC3\xAAter") != std::string::npos,
          "la Console le dit");
    (void)b.rt.callScript("Statistiques_Pression", 0.4);
    check(!b.rt.paused() && b.rt.pauseCount() == pauses, "le script suivant ne s'arrete pas");
    check(!b.rt.debugCancelPause(), "rien a annuler");
}


// =============================================================================
//  1.13.0 (seconde passe) : la seance des points, la marge, le volet Debogage,
//  les touches de la simulation (l'interface, sans ecran).
// =============================================================================

std::string numbered(int n, int skip = 0, const std::string& at = {}, int atLine = 0) {
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i == skip) continue;
        out += (i == atLine ? at : "L" + std::to_string(i) + " := " + std::to_string(i) + ";") + "\n";
    }
    return out;
}

// ---- T2-17, T2-19 : un point suit son code - supprimer, joindre au-dessus ; Annuler, Retablir (le texte recharge) ----
void sessionFollowsCode() {
    std::printf("-- un point suit son code : supprimer ou joindre au-dessus, Annuler, Retablir\n");
    auto& s = app::HmiDebugSession::instance();
    auto doc = std::make_shared<hmi::Document>();
    const Id sc = script(doc->project, "Calcul", numbered(12));
    s.bindProject(doc);
    s.clear();
    const app::HmiDebugTarget code{sc, kNoId};
    (void)s.add(code, 10);
    const auto line0 = [&] { return s.breakpoints().empty() ? 0u : s.breakpoints()[0].line; };
    // La ligne 9 supprimee : le point (la ligne 10) remonte en 9, avec son instruction.
    s.followText(code, numbered(12), numbered(12, 9));
    check(line0() == 9, "la ligne au-dessus supprimee : le point remonte avec son instruction (9) : " + std::to_string(line0()));
    // Deux lignes tapees au-dessus : il descend de deux.
    s.followText(code, numbered(12, 9), "X := 0;\nY := 0;\n" + numbered(12, 9));
    check(line0() == 11, "deux lignes au-dessus : le point descend de deux (11) : " + std::to_string(line0()));
    // Joindre deux lignes (Retour arriere en debut de ligne) : la ligne 10 rejoint la 9.
    s.clear();
    (void)s.add(code, 10);
    s.followText(code, numbered(12), numbered(12, 10, "L9 := 9; L10 := 10;", 9));
    check(line0() == 9, "la ligne 10 jointe a la 9 : le point la suit (9) : " + std::to_string(line0()));
    // Un bloc supprime qui contenait le point : il reste sur ce qu'il en reste (la ligne qui suit le bloc).
    s.clear();
    (void)s.add(code, 5);
    std::string without;
    for (int i = 1; i <= 12; ++i)
        if (i < 4 || i > 6) without += "L" + std::to_string(i) + " := " + std::to_string(i) + ";\n";
    s.followText(code, numbered(12), without);
    check(line0() == 4, "le bloc 4..6 supprime : le point va a la ligne qui le suit (4) : " + std::to_string(line0()));
    // La marge : la frappe, puis Annuler (le texte recharge du projet) - le point revient a sa ligne.
    s.clear();
    (void)s.add(code, 12);
    ui::MultiLineText editor("essai.code");
    editor.setText(numbered(12));
    app::HmiBreakpointMargin margin;
    margin.attach(editor, editor, [code] { return code; }, nullptr);
    editor.setText("A := 1;\nB := 2;\n" + numbered(12));
    margin.edited();
    check(line0() == 14, "deux lignes tapees au-dessus : 14 : " + std::to_string(line0()));
    editor.setText(numbered(12));                            // Ctrl+Z : le volet recharge le code du projet
    margin.reset();
    check(line0() == 12, "Annuler : le point revient a sa ligne (12) : " + std::to_string(line0()));
    editor.setText("A := 1;\nB := 2;\n" + numbered(12));    // Ctrl+Y
    margin.reset();
    check(line0() == 14, "Retablir : il redescend (14) : " + std::to_string(line0()));
    // Le meme changement vu par un second editeur (le volet Debogage qui montre ce code) : pas deux fois.
    ui::MultiLineText shown("essai.montre");
    shown.setText(numbered(12));
    app::HmiBreakpointMargin second;
    second.attach(shown, shown, [code] { return code; }, nullptr);
    shown.setText("A := 1;\nB := 2;\n" + numbered(12));
    second.reset();
    check(line0() == 14, "un second editeur du meme code ne le decale pas une seconde fois (14) : " + std::to_string(line0()));
    s.clear();
}

// ---- T2-24 : un autre projet, meme a la meme place en memoire : les points partent ----
void sessionProject() {
    std::printf("-- les points de la seance : un projet ferme puis un autre ouvert, meme a la meme place : ils partent\n");
    auto& s = app::HmiDebugSession::instance();
    auto a = std::make_shared<hmi::Document>();
    const Id sa = script(a->project, "Calcul", numbered(5));
    s.bindProject(a);
    s.clear();
    (void)s.add(app::HmiDebugTarget{sa, kNoId}, 3);
    s.bindProject(a);
    check(s.breakpoints().size() == 1, "le meme projet : les points restent");
    const void* where = a.get();
    a.reset();                                               // Fichier > Fermer le projet
    auto b = std::make_shared<hmi::Document>();              // ... puis un autre (souvent a la meme place)
    (void)script(b->project, "Autre", numbered(5));
    s.bindProject(b);
    check(s.breakpoints().empty(), std::string("un autre projet : les points partent") + (b.get() == where ? " (a la meme adresse)" : ""));
    check(s.project() == &b->project, "la seance connait le nouveau projet");
}

// ---- T2-16 : une nouvelle pause revient au niveau 0 ; T2-22 : l'apercu d'une condition dans le code du point ----
void sessionLevelAndPreview() {
    std::printf("-- une nouvelle pause revient a la ligne d'arret ; l'apercu d'une condition juge dans le code du point\n");
    auto& s = app::HmiDebugSession::instance();
    Bench b;
    b.go();
    s.setRuntime(&b.rt);
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    (void)b.rt.debugStep(DebugStep::Into);
    s.pullFrom(b.rt);
    s.setLevel(1);
    check(b.rt.paused() && s.level() == 1, "en pause dans Moyenne_Pression ; le niveau 1 (le script) choisi");
    (void)b.rt.debugStep(DebugStep::Out);
    s.pullFrom(b.rt);
    app::HmiDebugTarget code;
    std::uint32_t line = 0;
    check(s.level() == 0 && s.pausedAt(code, line) && code.script == b.stats && line == 15,
          "Sortir : la nouvelle pause se montre a sa ligne d'arret (niveau 0, script ligne 15) : niveau " + std::to_string(s.level()));
    // En pause dans le script : la condition d'un point de Moyenne_Pression se juge dans Moyenne_Pression.
    const app::HmiDebugTarget fn{kNoId, b.moyenne};
    const std::string later = app::conditionPreview(fn, "A > 1000.0");
    check(later.find("essay\xC3\xA9" "e au passage dans fonction Moyenne_Pression") != std::string::npos, "un parametre de la fonction : " + later);
    const std::string unknown = app::conditionPreview(fn, "Inconnu > 1.0");
    check(unknown.find("Elle ne se lit pas dans fonction Moyenne_Pression : Inconnu n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici") != std::string::npos,
          "un nom inconnu de la fonction : " + unknown);
    const std::string here = app::conditionPreview(app::HmiDebugTarget{b.stats, kNoId}, "P < 5.0");
    check(here.find("Maintenant, c'est vrai (P = 3.2)") == 0, "dans le code en pause : sa valeur, la : " + here);
    const std::string notBool = app::conditionPreview(app::HmiDebugTarget{b.stats, kNoId}, "Tours MOD 100");
    check(notBool.find("pas un BOOL") != std::string::npos, "une condition qui n'est pas un BOOL le dit : " + notBool);
    (void)b.rt.debugContinue();
    s.setRuntime(nullptr);
}

// ---- T2-21 : les tableaux du volet gardent la ligne choisie ; T2-23 : Annuler la pause ; T2-16 : la reprise au niveau 0 ----
void paneKeepsAndCancels() {
    std::printf("-- le volet Debogage : la ligne choisie reste, Pause devient Annuler la pause\n");
    auto& s = app::HmiDebugSession::instance();
    Bench b;
    auto doc = std::make_shared<hmi::Document>();
    doc->project = b.p;
    b.go();
    s.bindProject(doc);
    s.clear();
    s.setRuntime(&b.rt);
    app::HmiDebugPane pane("essai.debogage", doc, [&b] { return &b.rt; });
    pane.setBounds({0.f, 0.f, 1200.f, 600.f});
    (void)s.add(app::HmiDebugTarget{b.stats, kNoId}, 10);
    (void)s.add(app::HmiDebugTarget{b.stats, kNoId}, 11);
    pane.refresh(0.0);
    check(pane.breakpointTable().selectedModelRows().empty() && pane.breakpointLines().size() == 2, "deux points dans le tableau");
    // 1.13.0 (captures F1, R9) : « Ou » commence par la ligne (en trois colonnes, la fin se coupe) ; l'infobulle dit tout.
    check(pane.breakpointLines()[0].rfind("l. 10 \xC2\xB7 script Statistiques_Pression", 0) == 0
              && pane.breakpointLines()[1].rfind("l. 11 \xC2\xB7 ", 0) == 0,
          "Ou : la ligne d'abord : " + pane.breakpointLines()[0]);
    if (const auto& m = pane.breakpointTable().model())
        check(m->rowTooltip(0).rfind("l. 10 \xC2\xB7 script Statistiques_Pression \xC2\xB7 0 passage", 0) == 0,
              "l'infobulle d'un point le dit en entier : " + m->rowTooltip(0));
    pane.breakpointTable().selectModelRows({1}, false);
    (void)s.setEnabled(s.breakpoints()[0].id, false);          // un changement : le volet se refait
    pane.refresh(0.1);
    check(pane.breakpointTable().selectedModelRows() == std::vector<ui::RowIndex>{1}, "le volet refait : le point choisi le reste");
    (void)s.setEnabled(s.breakpoints()[0].id, true);
    // Pause demandee : le bouton devient Annuler la pause ; un clic la retire.
    b.rt.debugPause();
    pane.refresh(0.2);
    check(pane.buttonText(app::HmiDebugPane::APause) == "Annuler la pause", "Pause demandee : le bouton dit Annuler la pause");
    pane.runAction(app::HmiDebugPane::APause);
    check(!b.rt.pauseAsked(), "un clic : la pause demandee est retiree");
    pane.refresh(0.3);
    check(pane.buttonText(app::HmiDebugPane::APause) == "Pause", "le bouton redevient Pause");
    // Le niveau choisi, puis Suivant : la pause suivante se montre a sa ligne.
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
    (void)b.rt.callScript("Statistiques_Pression", 0.4);
    (void)b.rt.debugStep(DebugStep::Into);
    pane.refresh(0.5);
    check(pane.chooseLevel(1) && s.level() == 1, "le niveau 1 choisi dans la pile");
    pane.runAction(app::HmiDebugPane::AStepOut);
    check(s.level() == 0, "Sortir : la nouvelle pause au niveau 0");
    (void)b.rt.debugContinue();
    s.setRuntime(nullptr);
    s.clear();
}

// ---- T2-15 : en pause, F5, F10, F11 ou que soit le clavier ; F11 n'est plus le plein ecran ----
void keysAnywhere() {
    std::printf("-- en pause : F10, F11, F5 pilotent le debogueur ou que soit le clavier ; F11 n'est pas le plein ecran\n");
    auto& s = app::HmiDebugSession::instance();
    Bench b;
    auto doc = std::make_shared<hmi::Document>();
    doc->project = b.p;
    app::HmiSimulationHost host;
    host.fullScreenWindow = [](bool) {};
    app::HmiSimulationPane pane("essai.simulation", doc, host);
    pane.setVisibility(ui::Visibility::Collapsed);              // l'onglet Simulation n'est pas a l'ecran
    auto& rt = pane.runtime();
    rt.bind(&doc->project, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    check(!app::HmiSimulationPane::claimsKey(ui::Key::F11), "hors pause, l'onglet cache : F11 reste a l'appli (le plein ecran)");
    rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
    (void)rt.callScript("Statistiques_Pression", 0.2);
    check(rt.paused(), "en pause ligne 14");
    check(app::HmiSimulationPane::claimsKey(ui::Key::F11), "en pause : F11 est au debogueur (Entrer), pas au plein ecran");
    check(app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F11, {}, false}) && rt.paused() && rt.debugHit()
              && rt.debugHit()->function == b.moyenne,
          "F11 (l'onglet cache) : Entrer dans Moyenne_Pression");
    ui::KeyMods shift;
    shift.shift = true;
    check(app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F11, shift, false}) && rt.paused() && rt.debugHit()
              && rt.debugHit()->line == 15,
          "Maj+F11 : Sortir, ligne 15 du script");
    check(app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F10, {}, false}) && !rt.paused(), "F10 : Suivant, le script finit");
    check(!app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F5, {}, false}), "hors pause, F5 n'est pas pris (Simuler, ailleurs)");
    (void)s;
}


// ---- T2-18 : la pastille IHM de la barre du haut dit la pause ----
void pausedChip() {
    std::printf("-- la barre du haut : la pastille IHM en pause (debogage)\n");
    app::TopBar bar("essai.barre");
    bar.setBounds({0.f, 0.f, 1800.f, 48.f});
    bar.setHmi(app::TopBar::Hmi::Running);
    const float running = bar.partRect("ihm").w;
    bar.setHmi(app::TopBar::Hmi::Paused);
    check(bar.hmi() == app::TopBar::Hmi::Paused, "la barre connait l'etat En pause");
    check(bar.partRect("ihm").w > running, "la pastille s'elargit pour IHM en pause (debogage) : " + std::to_string(bar.partRect("ihm").w)
                                                + " > " + std::to_string(running));
}


// =============================================================================
//  1.13.0 (les restes de la seconde passe, D1b) : un code change garde le reste
//  du travail ; un point dans une methode ou un operateur s'arrete ; une
//  redefinition d'instance se nomme ; la commande signee ; IHM_SCRIPT_ARRETER ;
//  les relachements ; le double clic ; les fils, les crochets et les exceptions ;
//  le script detache lance d'un geste ; deux points qui se rejoignent ; les
//  touches avec l'onglet detache ; la pastille.
// =============================================================================

// ---- D1b-3 : le code en pause change - le script s'arrete, la suite du geste et du demarrage continue ----
void changedKeepsWork() {
    std::printf("-- le code en pause change : ce script s'arrete, la suite du geste (et du demarrage) continue\n");
    {
        Project p;
        var(p, "X", "INT", "0");
        var(p, "Commande", "INT", "0");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        View& shown = p.views.back();
        p.config.startView = shown.id;
        const Id btn = edit::add(p, shown, Kind::Button, 10, 10);
        shown.object(btn)->name = "Btn_Recette";
        shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Preparer_Recette"));
        shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Assign, "Commande", "X"));
        const Id prep = addGeneral(p, "Preparer_Recette", "Appel", "X := 5;\n"           // 1
                                                                   "X := X + 1;\n").id;  // 2
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
        rt.setBreakpoints({at(1, prep, kNoId, 2)});
        rt.press(btn, 0.2);
        rt.release(btn, 0.25, true);
        check(rt.paused() && value(rt, "Commande") == 0.0, "le clic s'arrete dans Preparer_Recette, l'action suivante attend");
        for (auto& sc : p.programs.scripts)
            if (sc.id == prep) sc.body += "X := 99;\n";
        check(rt.debugContinue() && !rt.paused(), "Continuer");
        check(value(rt, "X") == 5.0, "le script change ne reprend pas au milieu (X = 5)");
        check(value(rt, "Commande") == 5.0, "mais la suite du clic part (Commande := X = 5), comme le dit la Console");
        check(errors(rt).empty(), "sans erreur" + errors(rt));
    }
    {   // un script de Demarrage change pendant la pause : la premiere vue s'ouvre quand meme
        Project p;
        var(p, "Init", "INT", "0");
        var(p, "Ouvert", "INT", "0");
        View v = makeView(p, "Vue_Essai");
        addViewScript(p, v, "OnOpen", "Ouvert := Init + 10;\n");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        const Id init = addGeneral(p, "Initialiser", "Demarrage", "Init := 1;\n"      // 1
                                                                  "Init := 2;\n").id;  // 2
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.setBreakpoints({at(1, init, kNoId, 2)});
        rt.start(0.0);
        check(rt.paused() && rt.currentView() == kNoId, "le script de Demarrage s'arrete ; la premiere vue attend");
        for (auto& sc : p.programs.scripts)
            if (sc.id == init) sc.body += "Init := 3;\n";
        check(rt.debugContinue() && !rt.paused(), "Continuer");
        check(rt.currentView() == p.config.startView && value(rt, "Ouvert") == 11.0,
              "la premiere vue s'ouvre (son OnOpen lit Init = 1) : l'IHM n'est pas vide");
        check(errors(rt).empty(), "sans erreur" + errors(rt));
    }
}

// ---- D1b-4, D1b-1 : un point dans une methode, un operateur ; une redefinition d'instance se nomme ----
void methodOperatorOverride() {
    std::printf("-- un point dans une methode ou un operateur s'arrete a sa ligne ; une redefinition d'instance se nomme\n");
    Project p;
    HmiType t;
    t.id = p.allocate();
    t.name = "T_Vec";
    TypeMember x;
    x.name = "X";
    x.type = "REAL";
    t.members.push_back(x);
    HmiFunction m;
    m.id = p.allocate();
    m.name = "Doubler";
    m.body = "X := X * 2.0;\n"          // 1
             "X := X + 1.0;\n";         // 2
    const Id mid = m.id;
    t.methods.push_back(m);
    HmiOperator op;
    op.id = p.allocate();
    op.op = "+";
    op.left = "T_Vec";
    op.right = "T_Vec";
    op.result = "T_Vec";
    op.body = "Resultat.X := a.X + b.X;\n"         // 1
              "Resultat.X := Resultat.X + 0.5;\n";  // 2
    const Id oid = op.id;
    t.operators.push_back(op);
    p.programs.types.push_back(t);
    var(p, "a", "T_Vec", "");
    var(p, "b", "T_Vec", "");
    var(p, "c", "T_Vec", "");
    var(p, "R", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    (void)script(p, "Avec_Methode", "a.X := 3.0;\na.Doubler();\nR := a.X;\n");
    (void)script(p, "Avec_Operateur", "a.X := 1.0;\nb.X := 2.0;\nc := a + b;\nR := c.X;\n");
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    // La methode : le point pose dans son corps (la marge du volet CODE pose {kNoId, id de la methode}).
    rt.setBreakpoints({at(1, kNoId, mid, 2)});
    check(rt.breakpoints()[0].note.empty() && rt.breakpoints()[0].effectiveLine == 2, "le point de la methode se comprend : " + rt.breakpoints()[0].note);
    (void)rt.callScript("Avec_Methode", 0.2);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == mid && rt.debugHit()->line == 2,
          "le point de la methode s'arrete, a la ligne de son corps (2) : "
              + (rt.debugHit() ? rt.debugHit()->where + " ligne " + std::to_string(rt.debugHit()->line) : std::string("pas d'arret")));
    if (rt.debugHit()) check(rt.debugHit()->where == "m\xC3\xA9thode T_Vec.Doubler, ligne 2", "ou : " + rt.debugHit()->where);
    check(value(rt, "R") == 0.0 && rt.breakpoints()[0].hits == 1, "rien d'ecrit apres la ligne ; un passage");
    (void)rt.debugContinue();
    check(!rt.paused() && value(rt, "R") == 7.0, "Continuer : la methode finit (R = 3 * 2 + 1)" + errors(rt));
    // La condition d'un point dans une methode se lit comme son corps : un membre par son nom (X), THIS.X, SELF.X.
    for (const char* cond : {"X > 5.0", "THIS.X > 5.0", "SELF.X > 5.0"}) {
        rt.setBreakpoints({at(1, kNoId, mid, 2, cond)});
        check(rt.breakpoints()[0].note.empty(), std::string("la condition ") + cond + " se comprend dans la methode : " + rt.breakpoints()[0].note);
        (void)rt.callScript("Avec_Methode", 0.2);
        check(rt.paused() && rt.debugHit() && rt.debugHit()->function == mid && rt.debugHit()->line == 2,
              std::string("la condition ") + cond + " (X vaut 6 a la ligne 2) arrete : " + rt.breakpoints()[0].note);
        if (rt.paused()) {
            std::string shown, why;
            check(rt.debugEvaluate("X", shown, &why) && (shown == "6" || shown == "6.0"), "un espion X dans la methode lit son membre : " + shown + why);
            (void)rt.debugContinue();
        }
    }
    rt.setBreakpoints({at(1, kNoId, mid, 2, "X > 100.0")});
    (void)rt.callScript("Avec_Methode", 0.2);
    check(!rt.paused() && rt.breakpoints()[0].note.empty() && value(rt, "R") == 7.0,
          "la condition X > 100.0 fausse : pas d'arret, le point reste arme : " + rt.breakpoints()[0].note + errors(rt));
    // L'operateur : son nom, sa ligne, son code (le volet), son point.
    check(Runtime::debugCodeName(p, kNoId, oid) == "op\xC3\xA9rateur + (type T_Vec)", "le nom de l'operateur : " + Runtime::debugCodeName(p, kNoId, oid));
    check(Runtime::debugCodeBody(p, kNoId, oid) == op.body, "son code, tel que son editeur le montre");
    rt.setBreakpoints({at(1, kNoId, oid, 2)});
    check(rt.breakpoints()[0].note.empty() && rt.breakpoints()[0].effectiveLine == 2, "le point de l'operateur se comprend : " + rt.breakpoints()[0].note);
    (void)rt.callScript("Avec_Operateur", 0.3);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == oid && rt.debugHit()->line == 2,
          "le point de l'operateur s'arrete, a la ligne de son corps (2) : "
              + (rt.debugHit() ? rt.debugHit()->where + " ligne " + std::to_string(rt.debugHit()->line) : std::string("pas d'arret")));
    if (rt.debugHit()) {
        check(rt.debugHit()->where == "op\xC3\xA9rateur + (type T_Vec), ligne 2", "ou : " + rt.debugHit()->where);
        check(rt.debugHit()->stack.back().label == "op\xC3\xA9rateur + (type T_Vec)", "la pile le nomme : " + stackText(*rt.debugHit()));
    }
    (void)rt.debugContinue();
    check(!rt.paused() && value(rt, "R") == 3.5 && errors(rt).empty(), "Continuer : l'operateur finit (R = 1 + 2 + 0.5)" + errors(rt));
    rt.setBreakpoints({});
    rt.stop(0.4);

    // Une redefinition d'instance : IHM_POINT_ARRET dans son code - la pile la nomme, le volet a son code.
    Project q;
    var(q, "Ouvert", "INT", "0");
    View sym = makeView(q, "Vanne");
    sym.role = "symbole";
    HmiFunction f;
    f.id = q.allocate();
    f.name = "Ouvrir";
    f.isVirtual = true;
    f.body = "Ouvert := 1;\n";
    sym.functions.push_back(f);
    q.views.push_back(sym);
    View use = makeView(q, "Synoptique");
    Object inst;
    inst.id = q.allocate();
    inst.kind = Kind::SymbolInstance;
    inst.name = "V1";
    inst.props.push_back({"symbol", "Vanne", ""});
    inst.functionOverrides.push_back({"Ouvrir", "Ouvert := 2;\nIHM_POINT_ARRET;\nOuvert := 3;\n", {}});
    use.objects.push_back(inst);
    q.views.push_back(use);
    q.config.startView = q.views.back().id;
    (void)script(q, "Ouvrir_V1", "Synoptique.V1.Ouvrir();\n");
    Runtime r2;
    r2.bind(&q, nullptr);
    r2.setDebugger(true);
    r2.start(0.0);
    r2.tick(1.3);                                          // le debogueur voit IHM_POINT_ARRET dans la redefinition
    (void)r2.callScript("Ouvrir_V1", 1.4);
    check(r2.paused() && r2.debugHit() && r2.debugHit()->line == 2, "IHM_POINT_ARRET dans la redefinition s'arrete, ligne 2");
    if (r2.debugHit()) {
        const auto& hit = *r2.debugHit();
        check(hit.where == "red\xC3\xA9" "finition Synoptique.V1.Ouvrir, ligne 2", "ou : " + hit.where);
        check(hit.stack.back().label == "red\xC3\xA9" "finition Synoptique.V1.Ouvrir", "la pile la nomme : " + stackText(hit));
        check(r2.boundCodeBody(hit.function).find("IHM_POINT_ARRET") != std::string::npos, "le volet a son code : " + r2.boundCodeBody(hit.function));
    }
    const Id bound = r2.debugHit() ? r2.debugHit()->function : kNoId;
    (void)r2.debugContinue();
    check(!r2.paused() && value(r2, "Ouvert") == 3.0 && errors(r2).empty(), "Continuer : la redefinition finit (Ouvert = 3)" + errors(r2));
    // Un point pose dans son code (la marge du volet Debogage : {kNoId, id de la copie}) : il s'arrete aussi.
    r2.setBreakpoints({at(1, kNoId, bound, 3)});
    check(!r2.breakpoints().empty() && r2.breakpoints()[0].note.empty() && r2.breakpoints()[0].effectiveLine == 3,
          "le point de la redefinition se comprend : " + (r2.breakpoints().empty() ? std::string("-") : r2.breakpoints()[0].note));
    (void)r2.callScript("Ouvrir_V1", 1.5);
    if (r2.paused() && r2.debugHit() && r2.debugHit()->why == DebugHit::Why::Code) (void)r2.debugContinue();   // IHM_POINT_ARRET, ligne 2
    check(r2.paused() && r2.debugHit() && r2.debugHit()->why == DebugHit::Why::Breakpoint && r2.debugHit()->line == 3,
          "puis le point de la ligne 3 s'arrete");
    while (r2.paused()) (void)r2.debugContinue();
    check(value(r2, "Ouvert") == 3.0 && errors(r2).empty(), "la redefinition finit" + errors(r2));
}

// ---- D1b-6 : une commande signee - ses actions sur la tache du debogueur : la popup attend la fin du script ----
void signedCommand() {
    std::printf("-- une commande signee : un script de la commande en pause fige la suite (la popup attend)\n");
    Project p;
    p.security = Security::defaults();
    p.security.enabled = true;
    User u;
    u.id = p.allocate();
    u.login = "operateur";
    u.fullName = "Nom de operateur";
    u.group = p.security.groups[3].id;                    // l'administrateur : toutes les permissions (Scripts...)
    u.salt = randomHex(16);
    u.passwordHash = passwordHash(u.salt, "Oper-2026");
    p.security.users.push_back(u);
    var(p, "X", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    View pop = makeView(p, "Pop_Confirmation");
    pop.role = "popup";
    const Id popId = pop.id;
    const Id btn = edit::add(p, v, Kind::Button, 10, 10);
    v.object(btn)->name = "Btn_Purge";
    v.object(btn)->set("signature", "simple");
    v.object(btn)->set("signatureReasons", "Essai");
    v.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Purger"));
    v.object(btn)->actions.push_back(action(Trigger::Click, Operation::Popup, "Pop_Confirmation"));
    p.views = {v, pop};
    p.config.startView = p.views.front().id;
    const Id purge = addGeneral(p, "Purger", "Appel", "X := 1;\n"     // 1
                                                      "X := 2;\n").id; // 2
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    check(rt.login("operateur", "Oper-2026", 0.11), "connecte");
    rt.setBreakpoints({at(1, purge, kNoId, 2)});
    rt.press(btn, 0.2);
    rt.release(btn, 0.25, true);
    check(rt.signatureShown() && !rt.paused(), "le clic demande la signature");
    rt.signaturePart("champ:motdepasse", 0.3);
    rt.typeText("Oper-2026", 0.31);
    rt.signaturePart("bouton:signer", 0.32);

    check(!rt.signatureShown() && rt.paused() && rt.debugHit() && rt.debugHit()->script == purge, "signe : la commande s'arrete dans Purger, ligne 2");
    check(rt.popups().empty(), "la popup de la commande attend la fin du script");
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(value(rt, "X") == 2.0 && rt.popups().size() == 1 && rt.popups().front() == popId, "puis la popup s'ouvre, apres le script (X = 2)");
    check(errors(rt).empty(), "sans erreur" + errors(rt));
}

// ---- D1b-7 : IHM_SCRIPT_ARRETER ne voit que les scripts detaches, comme IHM_SCRIPT_ACTIF ----
void stopScriptDetachedOnly() {
    std::printf("-- IHM_SCRIPT_ARRETER : un script non detache en pause (une tache du debogueur) n'est pas arrete\n");
    Bench b;
    var(b.p, "Arrete", "BOOL", "TRUE");
    var(b.p, "Actif", "BOOL", "TRUE");
    (void)script(b.p, "Arreteur", "Arrete := IHM_SCRIPT_ARRETER('Statistiques_Pression');\nActif := IHM_SCRIPT_ACTIF('Statistiques_Pression');\n");
    (void)addGeneral(b.p, "Arreteur_IHM", "Cyclique", "Arrete := IHM_SCRIPT_ARRETER('IHM');\n", 100);
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    check(b.rt.paused(), "Statistiques_Pression en pause (sur sa tache, qui porte son nom)");
    (void)b.rt.callScript("Arreteur", 0.3);
    check(value(b.rt, "Arrete") == 0.0 && value(b.rt, "Actif") == 0.0, "IHM_SCRIPT_ARRETER et IHM_SCRIPT_ACTIF : FAUX tous deux (il n'est pas detache)");
    check(b.rt.paused(), "la pause tient");
    check(b.rt.debugContinue() && !b.rt.paused() && value(b.rt, "R") > 3.0, "Continuer : le script va au bout" + errors(b.rt));
    // Dans le travail du debogueur du cycle (sa tache s'appelle "IHM") : IHM_SCRIPT_ARRETER('IHM') ne l'arrete pas.
    b.rt.environment().write("Arrete", sim::Value::boolean(true));
    b.rt.tick(0.5);
    check(!b.rt.paused() && value(b.rt, "Arrete") == 0.0 && errors(b.rt).empty(),
          "IHM_SCRIPT_ARRETER('IHM') dans le cycle : FAUX, le cycle n'est pas arrete" + errors(b.rt));
}

// ---- D1b-8 : un bouton relache sur l'objet pendant la pause : son clic part a la reprise ----
void releaseClickReplayed() {
    std::printf("-- le bouton relache sur l'objet pendant la pause : son clic part a la reprise, comme sans debogueur\n");
    Project p;
    var(p, "Clique", "INT", "0");
    var(p, "Tours", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    View& shown = p.views.back();
    p.config.startView = shown.id;
    const Id btn = edit::add(p, shown, Kind::Button, 10, 10);
    shown.object(btn)->name = "Btn";
    shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Assign, "Clique", "Clique + 1"));
    const Id cyc = addGeneral(p, "Cycle", "Cyclique", "Tours := Tours + 1;\n").id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, cyc, kNoId, 1)});
    rt.press(btn, 0.15);                                   // appuye avant la pause
    rt.tick(0.35);
    check(rt.paused(), "le cycle s'arrete (le bouton est tenu)");
    rt.release(btn, 0.36, true);                           // relache SUR le bouton, pendant la pause
    check(value(rt, "Clique") == 0.0, "pendant la pause, le clic ne part pas");
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(value(rt, "Clique") == 1.0, "a la reprise, le clic part (ses actions Clic) : " + std::to_string(value(rt, "Clique")));
    check(errors(rt).empty(), "sans erreur" + errors(rt));
}

// ---- D1b-9 : le double clic lit sa copie, pas l'objet de la vue (qui a pu changer pendant la pause) ----
void doubleClickCopy() {
    std::printf("-- le double clic sur un tableau : apres la pause, il lit sa copie (la vue a pu changer)\n");
    Project p;
    var(p, "Noms", "LIST OF STRING", "['L1', 'L2']");
    var(p, "Vu", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    Object o;
    o.id = p.allocate();
    o.kind = Kind::Table;
    o.name = "Tab";
    o.set("rowsFrom", "Noms");
    o.set(std::string(table::kModeKey), std::string(table::modeLabel(table::Mode::Rows)));
    table::Column col;
    col.title = "Nom";
    col.content = "[*]";
    o.set(std::string(table::kColumnsKey), table::formatColumns({col}));
    o.actions.push_back(action(Trigger::DoubleClick, Operation::CallScript, "Attendre"));
    o.actions.push_back(action(Trigger::CellDoubleClick, Operation::Assign, "Vu", "7"));
    const Id tab = o.id;
    v.objects.push_back(o);
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id wait = addGeneral(p, "Attendre", "Appel", "Vu := 1;\n"     // 1
                                                       "Vu := 2;\n").id; // 2
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.objectPart(tab, "case:0:0", 0.2);                   // la case choisie
    rt.setBreakpoints({at(1, wait, kNoId, 2)});
    rt.doubleClick(tab, 0.3);
    check(rt.paused(), "le double clic s'arrete dans Attendre");
    p.views.back().object(tab)->kind = Kind::Rectangle;    // la vue change pendant la pause (l'objet d'avant n'est plus le meme)
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(value(rt, "Vu") == 7.0, "le double clic finit sur sa copie : Case double-cliquee part (Vu = 7) : " + std::to_string(value(rt, "Vu")));
}

std::set<std::string> processThreads() {
    std::set<std::string> out;
#ifdef __linux__
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/proc/self/task", ec)) out.insert(e.path().filename().string());
#endif
    return out;
}

// ---- D1b-10 : un fil repris d'un travail a l'autre ; les crochets sur le fil de l'IHM ; une exception remonte ----
void threadsHooksFailures() {
    std::printf("-- un point arme : un fil repris d'un tick a l'autre, les crochets sur le fil de l'IHM, une exception remonte\n");
    Project p;
    var(p, "Tours", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id cyc = addGeneral(p, "Cycle", "Cyclique", "Tours := Tours + 1;\nIHM_JOURNAL('battement');\n").id;
    (void)script(p, "Boum", "IHM_JOURNAL('BOOM');\n");
    const Id never = script(p, "Jamais", "Tours := 0;\n");     // un point arme ici, jamais atteint
    Runtime rt;
    std::set<std::thread::id> hookThreads;
    Runtime::Hooks hooks;
    hooks.journaled = [&](const JournalEntry& e) {
        hookThreads.insert(std::this_thread::get_id());
        if (e.message.find("BOOM") != std::string::npos) throw std::runtime_error("boum dans la Console");
    };
    rt.setHooks(hooks);
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, cyc, kNoId, 1)});
    rt.tick(0.25);
    check(rt.paused(), "le cycle s'arrete (son travail sur une tache)");
    const auto first = processThreads();
    (void)rt.debugContinue();
    rt.tick(0.4);
    check(rt.paused(), "le cycle suivant s'arrete aussi");
    const auto second = processThreads();
#ifdef __linux__
    check(!first.empty() && first == second, "le meme fil sert d'un tick a l'autre (pas un fil neuf par image) : " + std::to_string(first.size())
                                                + " / " + std::to_string(second.size()) + " fils");
#endif
    rt.setBreakpoints({at(1, never, kNoId, 1)});           // arme, jamais atteint : chaque tick tourne sur sa tache
    (void)rt.debugContinue();
    hookThreads.clear();
    const double tours = value(rt, "Tours");
    rt.tick(0.55);
    rt.tick(0.7);
    check(!rt.paused() && value(rt, "Tours") > tours, "les cycles tournent (sur la tache du debogueur)");
    check(hookThreads.size() == 1 && *hookThreads.begin() == std::this_thread::get_id(),
          "la Console (le crochet, IHM_JOURNAL du cycle) est appelee sur le fil de l'IHM, meme depuis la tache du cycle");
    bool thrown = false;
    try {
        (void)rt.callScript("Boum", 0.8);
    } catch (const std::runtime_error& e) {
        thrown = std::string(e.what()) == "boum dans la Console";
    }
    check(thrown, "une exception sortie du travail remonte sur le fil de l'IHM (le rapport de plantage), pas avalee");
    check(!rt.paused(), "pas de pause laissee");
}

// ---- D1b-11 : un script detache lance par un geste du debogueur recoit sa premiere tranche tout de suite ----
void detachedFirstSliceFromGesture() {
    std::printf("-- un script detache lance par un clic (sur la tache du debogueur) : sa premiere tranche tout de suite\n");
    Bench b;
    var(b.p, "Debut", "INT", "0");
    var(b.p, "Vu", "INT", "0");
    (void)script(b.p, "Remplir", "Debut := 1;\nWAIT(T#1s);\nDebut := 2;\n", true);
    View& shown = b.p.views.back();
    const Id btn = edit::add(b.p, shown, Kind::Button, 10, 10);
    shown.object(btn)->name = "Btn_Remplir";
    shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Remplir"));
    shown.object(btn)->actions.push_back(action(Trigger::Click, Operation::Assign, "Vu", "Debut"));
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});    // arme ailleurs : le clic tourne sur une tache du debogueur
    b.rt.press(btn, 0.2);
    b.rt.release(btn, 0.25, true);
    check(!b.rt.paused() && value(b.rt, "Vu") == 1.0, "la suite du clic voit la premiere tranche du script detache (Vu = Debut = 1), comme sans debogueur");
    b.rt.stop(0.3);
    check(errors(b.rt).empty(), "sans erreur" + errors(b.rt));
}

// ---- D1b-12 : la touche relachee pendant la pause, rappuyee tout de suite apres Continuer : un nouvel appui ----
void keyRepressedAfterContinue() {
    std::printf("-- une touche relachee pendant la pause puis rappuyee des la reprise : un nouvel appui, pas une repetition\n");
    Project p;
    var(p, "Marche", "BOOL", "FALSE");
    var(p, "Pas", "INT", "0");
    View v = makeView(p, "Vue_Essai");
    v.actions.push_back(action(Trigger::KeyPress, Operation::CallScript, "Pas_A_Pas", {}, "F2"));
    v.actions.push_back(action(Trigger::KeyRelease, Operation::Reset, "Marche", {}, "F2"));
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id pas = addGeneral(p, "Pas_A_Pas", "Appel", "Marche := TRUE;\n"     // 1
                                                        "Pas := Pas + 1;\n").id; // 2
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, pas, kNoId, 2)});
    check(rt.keyDown(*keys::parseChord("F2"), 0.2) && rt.paused(), "F2 : le script s'arrete");
    (void)rt.keyUp("F2", 0.25);                            // relachee pendant la pause : gardee
    rt.setBreakpoints({});
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(!rt.keyHeld("F2") && value(rt, "Marche") == 0.0, "des la reprise, le relachement est parti (F2 n'est plus tenue, Marche = FALSE)");
    check(rt.keyDown(*keys::parseChord("F2"), 0.3) && value(rt, "Pas") == 2.0 && value(rt, "Marche") == 1.0,
          "F2 rappuyee avant tout tick : un nouvel appui (le script repart, Pas = 2)");
    (void)rt.keyUp("F2", 0.35);
    check(value(rt, "Marche") == 0.0 && !rt.keyHeld("F2"), "relachee : Marche = FALSE");
}

// ---- D1b-13 : deux points qui se rejoignent sur une ligne : un seul reste, qui s'arrete la ou l'un des deux s'arretait ----
void mergedBreakpoints() {
    std::printf("-- deux points sur une meme ligne apres une suppression : un seul, avec les conditions des deux\n");
    auto& s = app::HmiDebugSession::instance();
    auto doc = std::make_shared<hmi::Document>();
    const Id sc = script(doc->project, "Calcul", numbered(12));
    s.bindProject(doc);
    s.clear();
    const app::HmiDebugTarget code{sc, kNoId};
    const auto first = s.add(code, 9, "A > 1");
    const auto second = s.add(code, 10, "B > 2");
    s.followText(code, numbered(12), numbered(12, 9));     // la ligne 9 supprimee : le point 10 remonte en 9
    check(s.breakpoints().size() == 1 && s.breakpoints()[0].id == first && s.breakpoints()[0].line == 9,
          "un seul point reste, le premier pose, ligne 9");
    if (!s.breakpoints().empty())
        check(s.breakpoints()[0].condition == "(A > 1) OR (B > 2)" && s.breakpoints()[0].enabled,
              "il s'arrete si l'une des deux conditions est vraie : " + s.breakpoints()[0].condition);
    // Le premier desactive : la condition du seul actif, et le point actif.
    s.clear();
    const auto off = s.add(code, 9, "A > 1");
    (void)s.setEnabled(off, false);
    (void)s.add(code, 10, "B > 2");
    s.shiftLines(code, 8, 10, -1);                         // la meme suppression (la ligne 9)
    check(s.breakpoints().size() == 1 && s.breakpoints()[0].enabled && s.breakpoints()[0].condition == "B > 2",
          "le premier desactive : le point garde l'etat et la condition de l'actif : "
              + (s.breakpoints().empty() ? std::string("-") : s.breakpoints()[0].condition));
    // Un des deux sans condition : sans condition (il s'arretait a chaque passage).
    s.clear();
    (void)s.add(code, 9);
    (void)s.add(code, 10, "B > 2");
    s.shiftLines(code, 8, 10, -1);
    check(s.breakpoints().size() == 1 && s.breakpoints()[0].condition.empty(), "l'un sans condition : sans condition");
    (void)second;
    s.clear();
}

// ---- D1b-2 : l'onglet Simulation detache dans sa propre fenetre : la fenetre principale pilote la pause ----
void keysDetachedWindow() {
    std::printf("-- l'onglet Simulation detache : F10 et F11 de la fenetre principale pilotent sa pause, F11 n'est pas le plein ecran\n");
    Bench b;
    auto doc = std::make_shared<hmi::Document>();
    doc->project = b.p;
    app::HmiSimulationHost host;
    host.fullScreenWindow = [](bool) {};
    ui::Widget root("detached.root");                      // la racine d'une fenetre detachee (DetachedWindows)
    auto& pane = static_cast<app::HmiSimulationPane&>(root.addChild(std::make_unique<app::HmiSimulationPane>("essai.detache", doc, host)));
    auto& rt = pane.runtime();
    rt.bind(&doc->project, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
    (void)rt.callScript("Statistiques_Pression", 0.2);
    check(rt.paused(), "en pause ligne 14 (l'onglet est dans une fenetre detachee)");
    check(app::HmiSimulationPane::claimsKey(ui::Key::F11), "la fenetre principale : F11 est au debogueur, pas au plein ecran");
    check(app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F11, {}, false}) && rt.paused() && rt.debugHit()
              && rt.debugHit()->function == b.moyenne,
          "F11 de la fenetre principale : Entrer dans Moyenne_Pression");
    check(app::HmiSimulationPane::debugKeyAnywhere(ui::KeyDown{ui::Key::F5, {}, false}) && !rt.paused(), "F5 : Continuer, le script finit");
    check(!app::HmiSimulationPane::claimsKey(ui::Key::F11), "hors pause : F11 reste au plein ecran de la fenetre principale");
}

// ---- D1b-5 : la pastille dit ce que fait un clic, meme en pause (elle arrete l'IHM) ----
void pausedChipHint() {
    std::printf("-- la pastille IHM en pause : la seconde ligne dit qu'un clic l'arrete\n");
    app::TopBar bar("essai.barre2");
    bar.setBounds({0.f, 0.f, 1800.f, 48.f});
    bar.setHmi(app::TopBar::Hmi::Running);
    check(bar.hmiHint() == "Maj+F8 l'arr\xC3\xAAte", "en marche : Maj+F8 l'arrete (comme un clic)");
    bar.setHmi(app::TopBar::Hmi::Paused);
    check(bar.hmiHint().find("Maj+F8 l'arr\xC3\xAAte") == 0 && bar.hmiHint().find("F5 continue") != std::string::npos,
          "en pause : un clic (Maj+F8) l'arrete, F5 continue : " + bar.hmiHint());
}


// =============================================================================
//  1.13.0 (les dernieres regressions, D1c) : un script detache arrete dans sa
//  premiere tranche fige le geste qui l'a lance ; une constante calculee a
//  l'ouverture dont le code change pendant la pause ; les passages de deux
//  points fondus, a travers le moteur ; F11 d'une autre fenetre detachee ;
//  Resultat dans la condition d'un point d'operateur.
// =============================================================================

// Un type T_Vec (X : REAL) et son operateur + (deux lignes) : pour D1c-2 et D1c-5.
Id vecWithPlus(Project& p) {
    HmiType t;
    t.id = p.allocate();
    t.name = "T_Vec";
    TypeMember x;
    x.name = "X";
    x.type = "REAL";
    t.members.push_back(x);
    HmiOperator op;
    op.id = p.allocate();
    op.op = "+";
    op.left = "T_Vec";
    op.right = "T_Vec";
    op.result = "T_Vec";
    op.body = "Resultat.X := a.X + b.X;\n"         // 1
              "Resultat.X := Resultat.X + 0.5;\n";  // 2
    const Id oid = op.id;
    t.operators.push_back(op);
    p.programs.types.push_back(t);
    return oid;
}

// ---- D1c-1 : un script detache lance par un clic s'arrete dans sa premiere tranche : la suite du clic attend ----
void detachedPauseHoldsGesture() {
    std::printf("-- un script detache s'arrete dans sa premiere tranche (lancee par un clic) : la suite du clic attend avec lui\n");
    const auto build = [](Project& p, Id& remplir, Id& calcul, Id& btn, Id& popId) {
        var(p, "Debut", "INT", "0");
        var(p, "Calc", "INT", "0");
        View v = makeView(p, "Vue_Essai");
        View pop = makeView(p, "Pop");
        pop.role = "popup";
        popId = pop.id;
        btn = edit::add(p, v, Kind::Button, 10, 10);
        v.object(btn)->name = "Btn_Remplir";
        v.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Remplir"));
        v.object(btn)->actions.push_back(action(Trigger::Click, Operation::CallScript, "Calcul"));
        v.object(btn)->actions.push_back(action(Trigger::Click, Operation::Popup, "Pop"));
        p.views = {v, pop};
        p.config.startView = p.views.front().id;
        remplir = script(p, "Remplir", "Debut := 1;\n"          // 1
                                       "Debut := 2;\n"          // 2
                                       "WAIT(T#1s);\n"          // 3
                                       "Debut := 3;\n", true);  // 4
        calcul = script(p, "Calcul", "Calc := 1;\n"             // 1
                                     "Calc := Debut * 10;\n");  // 2
    };
    {
        Project p;
        Id remplir = kNoId, calcul = kNoId, btn = kNoId, popId = kNoId;
        build(p, remplir, calcul, btn, popId);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
        rt.setBreakpoints({at(1, remplir, kNoId, 2), at(2, calcul, kNoId, 2)});
        rt.press(btn, 0.2);
        rt.release(btn, 0.25, true);
        check(rt.paused() && rt.debugHit() && rt.debugHit()->script == remplir && rt.debugHit()->line == 2,
              "le clic : arret dans Remplir (detache, sa premiere tranche), ligne 2");
        check(value(rt, "Calc") == 0.0 && rt.popups().empty(), "la suite du clic attend : Calcul n'a pas tourne, la popup n'est pas ouverte");
        check(rt.debugContinue(), "Continuer");
        check(rt.paused() && rt.debugHit() && rt.debugHit()->script == calcul && rt.debugHit()->line == 2,
              "la tranche de Remplir finie (son WAIT), la suite du clic part : arret dans Calcul, ligne 2 (pas refuse)");
        check(value(rt, "Debut") == 2.0 && value(rt, "Calc") == 1.0 && rt.popups().empty(), "Remplir attend (Debut = 2) ; la popup attend encore");
        check(rt.breakpoints().size() == 2 && rt.breakpoints()[1].note.empty(),
              "le point de Calcul n'a pas la note 'un autre script est deja en pause' : "
                  + (rt.breakpoints().size() == 2 ? rt.breakpoints()[1].note : std::string("-")));
        check(rt.debugContinue() && !rt.paused(), "Continuer");
        check(value(rt, "Calc") == 20.0 && rt.popups().size() == 1 && rt.popups().front() == popId,
              "Calcul finit (Calc = Debut * 10 = 20), puis la popup s'ouvre : l'ordre d'un clic sans debogueur");
        rt.setBreakpoints({});
        rt.tick(1.6);
        check(!rt.paused() && value(rt, "Debut") == 3.0, "le WAIT fini, Remplir finit en arriere-plan (Debut = 3)");
        check(errors(rt).empty(), "sans erreur" + errors(rt));
    }
    {   // Arreter la simulation pendant cette pause : le script et le clic qui l'attend s'arretent, sans erreur
        Project p;
        Id remplir = kNoId, calcul = kNoId, btn = kNoId, popId = kNoId;
        build(p, remplir, calcul, btn, popId);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
        rt.setBreakpoints({at(1, remplir, kNoId, 2)});
        rt.press(btn, 0.2);
        rt.release(btn, 0.25, true);
        check(rt.paused(), "arret dans Remplir");
        rt.stop(0.3);
        check(!rt.paused() && value(rt, "Calc") == 0.0 && rt.popups().empty(), "Arreter : la suite du clic ne part pas");
        check(errors(rt).empty(), "sans erreur" + errors(rt));
    }
}

// ---- D1c-2 : l'operateur en pause dans le calcul d'une constante a l'ouverture change : sans erreur, l'OnOpen part ----
void changedInConstantAtOpen() {
    std::printf("-- un operateur arrete dans le calcul d'une constante a l'ouverture change : sans erreur, l'OnOpen de la vue part\n");
    Project p;
    const Id oid = vecWithPlus(p);
    var(p, "a", "T_Vec", "");
    var(p, "b", "T_Vec", "");
    var(p, "Ouvert", "INT", "0");
    View va = makeView(p, "Vue_A");
    View vb = makeView(p, "Vue_B");
    const Id vbId = vb.id;
    Declaration c;
    c.id = p.allocate();
    c.kind = DeclKind::Constant;
    c.name = "Somme";
    c.type = "T_Vec";
    c.computed = true;
    c.reopen = true;                                       // calculee a chaque ouverture
    c.calc = "Somme := a + b;\n";
    vb.constants.push_back(c);
    (void)function(p, "Un", "INT", {}, "INT", "Un := 1;\n");
    // Une fonction IHM appelee : la ou un abandon reste leve coupait l'OnOpen sans un mot.
    (void)addViewScript(p, vb, "OnOpen", "Ouvert := Un();\n"
                                         "Ouvert := Ouvert + 1;\n");
    const Id btn = edit::add(p, va, Kind::Button, 10, 10);
    va.object(btn)->name = "Btn_B";
    va.object(btn)->actions.push_back(action(Trigger::Click, Operation::Navigate, "Vue_B"));
    p.views = {va, vb};
    p.config.startView = p.views.front().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, kNoId, oid, 2)});
    rt.press(btn, 0.2);
    rt.release(btn, 0.25, true);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == oid && rt.debugHit()->line == 2,
          "le clic ouvre Vue_B : sa constante Somme (a + b) s'arrete dans l'operateur +, ligne 2");
    for (auto& t : p.programs.types)
        for (auto& o : t.operators) o.body += "Resultat.X := Resultat.X + 100.0;\n";
    check(rt.debugContinue() && !rt.paused(), "Continuer");
    check(errors(rt).empty(), "sans erreur (pas 'arrete : trop long')" + errors(rt));
    check(rt.currentView() == vbId && value(rt, "Ouvert") == 2.0, "Vue_B s'ouvre et son OnOpen va au bout (Ouvert = 2), il n'est pas abandonne");
    const std::string said = console(rt, "D\xC3\xA9" "bogage");
    check(said.find("pas de nouvelle valeur") != std::string::npos, "la Console dit que la constante n'a pas de nouvelle valeur :" + said);
    // Une autre ouverture la recalcule, avec le code change (a + b + 0.5 + 100).
    rt.setBreakpoints({});
    rt.stop(0.3);
}

// ---- D1c-3 : deux points fondus en un : leurs passages s'ajoutent, aussi une fois passes par le moteur ----
void mergedHitsThroughEngine() {
    std::printf("-- deux points fondus en un : leurs passages s'ajoutent (la seance, puis le moteur)\n");
    auto& s = app::HmiDebugSession::instance();
    auto doc = std::make_shared<hmi::Document>();
    for (int i = 1; i <= 12; ++i) var(doc->project, ("L" + std::to_string(i)).c_str(), "INT", "0");
    View v = makeView(doc->project, "Vue_Essai");
    doc->project.views.push_back(v);
    doc->project.config.startView = doc->project.views.back().id;
    const Id sc = script(doc->project, "Calcul", numbered(12));
    s.bindProject(doc);
    s.clear();
    const app::HmiDebugTarget code{sc, kNoId};
    const auto first = s.add(code, 9);
    const auto second = s.add(code, 10);
    Runtime rt;
    rt.bind(&doc->project, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints(s.breakpoints());                    // debugSync : la seance donne ses points au moteur
    double t = 0.2;
    for (int k = 0; k < 3; ++k, t += 0.1) {               // 3 passages a chaque ligne
        (void)rt.callScript("Calcul", t);
        while (rt.paused()) (void)rt.debugContinue();
    }
    (void)s.setEnabled(first, false);                      // le point 9 desactive : 2 passages de plus au point 10
    rt.setBreakpoints(s.breakpoints());
    for (int k = 0; k < 2; ++k, t += 0.1) {
        (void)rt.callScript("Calcul", t);
        while (rt.paused()) (void)rt.debugContinue();
    }
    s.pullFrom(rt);
    const auto* a = s.find(first);
    const auto* b = s.find(second);
    check(a && b && a->hits == 3 && b->hits == 5, "les passages : 3 au point 9, 5 au point 10");
    s.followText(code, numbered(12), numbered(12, 9));     // la ligne 9 supprimee : le point 10 remonte en 9
    check(s.breakpoints().size() == 1 && s.breakpoints()[0].hits == 8, "la seance : un seul point, 8 passages");
    rt.setBreakpoints(s.breakpoints());                    // debugSync : le moteur recoit le point fondu
    s.pullFrom(rt);                                        // ... et la seance relit ce qu'il en dit
    check(!rt.breakpoints().empty() && rt.breakpoints()[0].hits == 8, "le moteur garde la somme (8), pas les 3 du point garde");
    check(s.breakpoints().size() == 1 && s.breakpoints()[0].hits == 8,
          "la seance montre 8 passages apres le moteur : " + (s.breakpoints().empty() ? std::string("-") : std::to_string(s.breakpoints()[0].hits)));
    // Un redemarrage : le moteur repart de zero passage ; la seance aussi, avant de lui redonner sa liste (debugSync :
    // followStart, puis setBreakpoints) - l'ancien compte (8) ne revient pas par max().
    rt.stop(t);
    rt.start(t + 1.0);
    s.followStart(rt);
    (void)s.add(code, 11);                                 // un point pose entre les deux marches (la liste a change)
    rt.setBreakpoints(s.breakpoints());
    s.pullFrom(rt);
    bool zero = !s.breakpoints().empty();
    for (const auto& bp : s.breakpoints()) zero = zero && bp.hits == 0;
    check(zero, "apres un redemarrage : les passages repartent de zero (seance et moteur)");
    rt.stop(t + 2.0);
    s.clear();
}

// ---- D1c-4 : en pause, F11 d'une AUTRE fenetre detachee (un editeur detache) reste son plein ecran ----
void keysOtherDetachedWindow() {
    std::printf("-- en pause : F11 d'une autre fenetre detachee (un editeur) reste son plein ecran ; celle de la simulation et la principale : le debogueur\n");
    Bench b;
    auto doc = std::make_shared<hmi::Document>();
    doc->project = b.p;
    app::HmiSimulationHost host;
    host.fullScreenWindow = [](bool) {};
    ui::Widget simWindow("detached.root");                 // la fenetre de l'onglet Simulation detache
    auto& pane = static_cast<app::HmiSimulationPane&>(simWindow.addChild(std::make_unique<app::HmiSimulationPane>("essai.detache2", doc, host)));
    ui::Widget editorWindow("detached.root");              // une autre fenetre detachee : un editeur, pas de simulation
    auto& rt = pane.runtime();
    rt.bind(&doc->project, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    check(!app::HmiSimulationPane::claimsKey(ui::Key::F11, &editorWindow), "hors pause : F11 de l'editeur detache est son plein ecran");
    rt.setBreakpoints({at(1, b.stats, kNoId, 14)});
    (void)rt.callScript("Statistiques_Pression", 0.2);
    check(rt.paused(), "en pause ligne 14");
    check(!app::HmiSimulationPane::claimsKey(ui::Key::F11, &editorWindow),
          "en pause : l'editeur detache ne pilote pas la pause - F11 y reste son plein ecran (pas une touche morte)");
    check(app::HmiSimulationPane::claimsKey(ui::Key::F11, &simWindow), "la fenetre de l'onglet Simulation : F11 est au debogueur");
    check(app::HmiSimulationPane::claimsKey(ui::Key::F11), "la fenetre principale : F11 est au debogueur (elle pilote la pause)");
    (void)rt.debugContinue();
    check(!rt.paused(), "Continuer");
}

// ---- D1c-5 : Resultat dans la condition d'un point d'operateur ; les locales et les valeurs le nomment ainsi ----
void operatorResultCondition() {
    std::printf("-- un point d'operateur : la condition lit Resultat, les locales et les valeurs de la ligne le nomment ainsi\n");
    Project p;
    const Id oid = vecWithPlus(p);
    var(p, "a", "T_Vec", "");
    var(p, "b", "T_Vec", "");
    var(p, "c", "T_Vec", "");
    var(p, "R", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    (void)script(p, "Avec_Operateur", "a.X := 1.0;\nb.X := 2.0;\nc := a + b;\nR := c.X;\n");
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    rt.setBreakpoints({at(1, kNoId, oid, 2, "Resultat.X > 1.0")});
    check(rt.breakpoints()[0].note.empty(), "pose sans alerte (Resultat est connu dans un operateur) : " + rt.breakpoints()[0].note);
    (void)rt.callScript("Avec_Operateur", 0.2);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == oid && rt.debugHit()->line == 2,
          "Resultat.X > 1.0 (3 > 1) est vraie : arret ligne 2 de l'operateur ; note : " + rt.breakpoints()[0].note);
    if (rt.debugHit()) {
        const auto& hit = *rt.debugHit();
        const DebugFrame& top = hit.stack.back();
        check(local(top, "Resultat") != nullptr && local(top, "OPERATEUR_ADD") == nullptr,
              "les locales nomment le resultat Resultat, pas OPERATEUR_ADD");
        bool shown = false, internal = false;
        for (const auto& [name, val] : hit.values) {
            if (name == "Resultat.X") shown = true;
            if (name.find("OPERATEUR_") != std::string::npos) internal = true;
        }
        check(shown && !internal, "les valeurs de la ligne : Resultat.X, pas OPERATEUR_ADD.X");
        std::string val, why;
        check(rt.debugEvaluate("Resultat.X", val, &why) && val.rfind("3", 0) == 0, "un espion Resultat.X : " + val + why);
    }
    (void)rt.debugContinue();
    check(!rt.paused() && value(rt, "R") == 3.5 && errors(rt).empty(), "Continuer : R = 3.5" + errors(rt));
    rt.setBreakpoints({at(2, kNoId, oid, 2, "Resultat.X > 10.0")});
    (void)rt.callScript("Avec_Operateur", 0.3);
    check(!rt.paused() && rt.breakpoints()[0].note.empty() && rt.breakpoints()[0].hits == 0,
          "Resultat.X > 10.0 est fausse : pas d'arret, et le point reste arme (pas 'condition invalide') : " + rt.breakpoints()[0].note);
    rt.stop(0.4);
}

// =============================================================================
//  Les captures de la 1.13.0 (lot F1) : en pause dans une methode, son retour se
//  nomme comme son code l'ecrit (C4 a) et la Console dit la methode et sa ligne
//  (C4 b) ; un espion hors de portee le dit (R7).
// =============================================================================

// ---- C4 : la methode T_Sonde.Ecart_Relatif - Ecart_Relatif (pas METHODE_T_SONDE_ECART_RELATIF), la Console a sa ligne ----
void methodShownInPause() {
    std::printf("-- captures F1 (C4) : en pause dans une methode, son retour se nomme Ecart_Relatif ; la Console dit la methode\n");
    Project p;
    HmiType t;
    t.id = p.allocate();
    t.name = "T_Sonde";
    for (const char* n : {"Valeur", "Consigne"}) {
        TypeMember m;
        m.name = n;
        m.type = "REAL";
        t.members.push_back(m);
    }
    HmiFunction m;
    m.id = p.allocate();
    m.name = "Ecart_Relatif";
    m.returnType = "REAL";
    m.body = "(* SELF : la sonde *)\n"                                     // 1
             "IF ABS(Consigne) < 1.0 THEN\n"                               // 2
             "    Ecart_Relatif := 0.0;\n"                                 // 3
             "ELSE\n"                                                      // 4
             "    Ecart_Relatif := (Valeur - Consigne) / ABS(Consigne);\n" // 5
             "END_IF;\n";                                                  // 6
    const Id mid = m.id;
    t.methods.push_back(m);
    p.programs.types.push_back(t);
    var(p, "Sonde", "T_Sonde", "");
    var(p, "R", "REAL", "0.0");
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const Id sid = script(p, "Statistiques_Pression", "Sonde.Valeur := 36.7;\nSonde.Consigne := 38.0;\nR := Sonde.Ecart_Relatif();\n");
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.setDebugger(true);
    rt.start(0.0);
    rt.tick(0.1);
    const auto lastDebugLine = [&]() -> const JournalEntry* {
        const JournalEntry* last = nullptr;
        for (const auto& e : rt.journal())
            if (e.kind == "D\xC3\xA9" "bogage" && e.message.find("pause") != std::string::npos) last = &e;
            else if (e.kind == "D\xC3\xA9" "bogage" && e.message.find("arr\xC3\xAAt\xC3\xA9") != std::string::npos) last = &e;
        return last;
    };
    // (a) le point de la ligne 5 de la methode : les locales et les valeurs de la ligne nomment Ecart_Relatif.
    rt.setBreakpoints({at(1, kNoId, mid, 5)});
    (void)rt.callScript("Statistiques_Pression", 0.2);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->function == mid && rt.debugHit()->line == 5,
          "le point de la ligne 5 de la methode s'arrete : " + (rt.debugHit() ? rt.debugHit()->where : std::string("pas d'arret")));
    if (rt.debugHit()) {
        const auto& hit = *rt.debugHit();
        const DebugFrame& top = hit.stack.back();
        std::string names;
        for (const auto& l : top.locals) names += " " + l.name;
        check(local(top, "Ecart_Relatif") != nullptr && names.find("METHODE_") == std::string::npos,
              "les locales nomment le retour Ecart_Relatif, pas METHODE_T_SONDE_ECART_RELATIF :" + names);
        bool shown = false, internal = false;
        std::string vals;
        for (const auto& [name, val] : hit.values) {
            vals += " " + name + "=" + val;
            if (name == "Ecart_Relatif") shown = true;
            if (name.find("METHODE_") != std::string::npos) internal = true;
        }
        check(shown && !internal, "les valeurs de la ligne (la ligne d'etat) : Ecart_Relatif, pas son nom interne :" + vals);
        // (b) la Console : la methode, sa ligne a elle, et le double clic y mene (la fonction : la methode).
        const JournalEntry* e = lastDebugLine();
        check(e && e->source == "m\xC3\xA9thode T_Sonde.Ecart_Relatif" && e->line == 5 && e->function == mid && e->script == kNoId,
              "la Console : Source 'methode T_Sonde.Ecart_Relatif', L. 5, et elle mene a la methode : "
                  + (e ? e->source + " L." + std::to_string(e->line) + " " + e->message : std::string("rien")));
    }
    (void)rt.debugContinue();
    // Un pas dans la methode (Entrer depuis le script) : la Console dit la methode, a la ligne 2.
    rt.setBreakpoints({at(1, sid, kNoId, 3)});
    (void)rt.callScript("Statistiques_Pression", 0.3);
    check(rt.paused() && rt.debugHit() && rt.debugHit()->line == 3, "en pause ligne 3 du script");
    check(rt.debugStep(DebugStep::Into) && rt.paused() && rt.debugHit() && rt.debugHit()->function == mid,
          "Entrer : dans la methode : " + (rt.debugHit() ? rt.debugHit()->where : std::string("-")));
    if (const JournalEntry* e = lastDebugLine(); rt.debugHit())
        check(e && e->source == "m\xC3\xA9thode T_Sonde.Ecart_Relatif" && e->line == static_cast<int>(rt.debugHit()->line) && e->function == mid,
              "le pas : Source la methode, sa ligne (" + std::to_string(rt.debugHit()->line) + ") : "
                  + (e ? e->source + " L." + std::to_string(e->line) : std::string("rien")));
    (void)rt.debugContinue();
    // Le script, lui, reste la source de ses propres arrets.
    rt.setBreakpoints({at(1, sid, kNoId, 3)});
    (void)rt.callScript("Statistiques_Pression", 0.4);
    if (const JournalEntry* e = lastDebugLine())
        check(e->source == "script Statistiques_Pression" && e->line == 3 && e->script == sid, "un arret du script : sa source, sa ligne : " + e->source);
    (void)rt.debugContinue();
    check(!rt.paused() && errors(rt).empty(), "la methode finit sans erreur" + errors(rt));
    rt.stop(0.5);
}

// ---- R7 : un espion lu hors de sa portee (une locale du script appelant, en pause dans la fonction) ----
void watchOutOfScope() {
    std::printf("-- captures F1 (R7) : un espion d'une locale de l'appelant, en pause dans la fonction : hors de portee ici\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, kNoId, b.moyenne, 5)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->function == b.moyenne, "en pause dans Moyenne_Pression");
    std::string v, why;
    check(!b.rt.debugEvaluate("P * 2.0", v, &why) && why.find("hors de port") != std::string::npos
              && why.find("Statistiques_Pression") != std::string::npos && why.find("pas d\xC3\xA9" "clar") == std::string::npos,
          "P * 2.0 (P : locale du script appelant) : hors de portee ici, pas 'non declare' : " + why);
    why.clear();
    check(!b.rt.debugEvaluate("Presion", v, &why) && why.find("n'est pas d\xC3\xA9" "clar") != std::string::npos,
          "Presion (une faute de frappe) : reste 'n'est pas declare' : " + why);
    why.clear();
    check(b.rt.debugEvaluate("Somme", v, &why) || why.find("hors de port") == std::string::npos, "Somme (locale d'ici) se lit : " + v + why);
    // R8 : la colonne Valeur des espions se coupe en trois colonnes - l'infobulle de la ligne dit la valeur entiere.
    {
        auto& s = app::HmiDebugSession::instance();
        auto doc = std::make_shared<hmi::Document>();
        doc->project = b.p;
        s.bindProject(doc);
        s.setRuntime(&b.rt);
        app::HmiDebugPane pane("essai.debogage.espions", doc, [&b] { return &b.rt; });
        pane.setBounds({0.f, 0.f, 1200.f, 600.f});
        (void)pane.addWatch("P * 2.0");
        pane.refresh(0.3);
        const auto& m = pane.watchTable().model();
        const std::string tip = m ? m->rowTooltip(0) : std::string{};
        check(tip.rfind("P * 2.0 = (P est hors de port", 0) == 0 && tip.find("Statistiques_Pression)") != std::string::npos,
              "l'infobulle d'un espion : sa valeur (ou pourquoi) en entier : " + tip);
        s.setRuntime(nullptr);
    }
    (void)b.rt.debugContinue();
}

// ---- C5 : l'arret sur modification - la Console tient une ligne (le saut de ligne de la carte s'y montrait en U+FFFD) ----
void stopForModificationOneLine() {
    std::printf("-- captures F1 (C5) : 'La simulation a ete arretee car le projet IHM a ete modifie' tient une ligne a la Console\n");
    Bench b;
    auto doc = std::make_shared<hmi::Document>();
    doc->project = b.p;
    app::HmiSimulationHost host;
    host.fullScreenWindow = [](bool) {};
    {
        app::HmiSimulationPane pane("essai.simulation.modif", doc, host);
        pane.startHmi("essai");
        check(pane.hmiRunning(), "la simulation de l'IHM demarre");
        pane.stopForModification("1 \xC3\xA9l\xC3\xA9ment modifi\xC3\xA9 : 1 \xC3\xA0 compiler\n\xC2\xB7 Controle_Niveau (script, \xC3\xA0 compiler)");
        std::string said;
        for (const auto& e : pane.runtime().journal())
            if (e.message.find("car le projet IHM a") != std::string::npos) said = e.message;
        check(!said.empty() && said.find('\n') == std::string::npos
                  && said.find("1 \xC3\xA0 compiler \xC2\xB7 Controle_Niveau (script") != std::string::npos,
              "une ligne, les lignes de la carte a la suite : " + said);
        check(!pane.hmiRunning(), "la simulation s'est arretee");
    }
    app::HmiDebugSession::instance().setRuntime(nullptr);
}

} // namespace

int main() {
    methodShownInPause();
    watchOutOfScope();
    stopForModificationOneLine();
    pauseAndLocals();
    conditions();
    shifted();
    stepping();
    richAndInner();
    codeBreak();
    notInExpression();
    detached();
    changedWhilePaused();
    cScript();
    zeroCost();
    sharedWords();
    viewFunction();
    cycleFrozen();
    gestureFrozen();
    gesturesWhilePaused();
    actionsStop();
    probesReadOnly();
    stopUnderTry();
    methodBreak();
    namespacedCondition();
    conditionKinds();
    sameCost();
    quietCounts();
    sessionFollowsCode();
    sessionProject();
    sessionLevelAndPreview();
    paneKeepsAndCancels();
    keysAnywhere();
    pausedChip();
    changedKeepsWork();
    methodOperatorOverride();
    signedCommand();
    stopScriptDetachedOnly();
    releaseClickReplayed();
    doubleClickCopy();
    threadsHooksFailures();
    detachedFirstSliceFromGesture();
    keyRepressedAfterContinue();
    mergedBreakpoints();
    keysDetachedWindow();
    pausedChipHint();
    detachedPauseHoldsGesture();
    changedInConstantAtOpen();
    mergedHitsThroughEngine();
    keysOtherDetachedWindow();
    operatorResultCondition();
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
