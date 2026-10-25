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
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/sim/DebugShared.hpp"
#include "../src/sim/Interpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
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
    }
    (void)rt.debugContinue();
    check(rt.paused() && rt.debugHit()->script == s && rt.debugHit()->line == 3, "arret dans la fonction interne Double, ligne 3");
    if (rt.debugHit())
        check(rt.debugHit()->stack.back().label.find("Double (fonction interne") == 0, "le niveau dit la fonction interne : " + stackText(*rt.debugHit()));
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
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
