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
//  HMIDEBUG_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
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
    std::printf("-- le script change pendant la pause : il reprendra au prochain cycle\n");
    Bench b;
    b.go();
    b.rt.setBreakpoints({at(1, b.stats, kNoId, 10)});
    (void)b.rt.callScript("Statistiques_Pression", 0.2);
    check(b.rt.paused(), "en pause");
    for (auto& sc : b.p.programs.scripts)
        if (sc.id == b.stats) sc.body += "R := 99.0;\n";
    (void)b.rt.debugContinue();
    check(!b.rt.paused() && value(b.rt, "R") == 0.0, "il ne reprend pas au milieu d'un code qui a change (R = 0)");
    check(console(b.rt, "D\xC3\xA9" "bogage").find("a chang\xC3\xA9 pendant la pause : il reprendra au prochain cycle") != std::string::npos,
          "la Console le dit");
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
    check(value(rt, "Marche") == 1.0, "le relachement attend le prochain tick (dans l'ordre)");
    rt.tick(0.5);
    check(value(rt, "Marche") == 0.0 && !rt.keyHeld("F2"), "au tick suivant, Touche relachee part (Marche = FALSE), F2 n'est plus tenue");
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

} // namespace

int main() {
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
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
