// =============================================================================
//  tests/erreurs_test.cpp - 1.13.0 : LEVER ses erreurs, RELEVER, WAIT_UNTIL
// -----------------------------------------------------------------------------
//  Le lot FONDATEUR des erreurs : LEVER('E_Code', 'message') leve une erreur a
//  soi, CATCH e la relit (e.Code, e.Message, e.Ligne ; e seul = e.Message),
//  RELEVER la reemet, les erreurs du moteur recoivent un code stable E_*, et
//  WAIT_UNTIL(condition [, delai]) attend qu'une condition devienne vraie. Tout
//  cela n'est QUE du dialecte IHM : le ST de l'automate ne le connait pas.
//
//  ERREURS_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/sim/Interpreter.hpp"     // le ST de l'automate (sans le dialecte)

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("ERREURS_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

// Un petit projet d'essai : des variables, une vue, un script appele (non detache).
struct Bench {
    Project p;
    Runtime rt;
    explicit Bench(const std::string& resultType, const std::string& body, const std::function<void(Project&)>& extra = {}) {
        const auto var = [this](const char* n, const std::string& t, const char* init) {
            Variable v;
            v.id = p.allocate();
            v.name = n;
            v.type = t;
            v.initial = init;
            p.programs.variables.push_back(v);
        };
        var("R", resultType.empty() ? "STRING" : resultType, "");
        var("Niveau", "REAL", "42.5");
        var("Compteur", "INT", "7");
        var("Marche", "BOOL", "TRUE");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
        if (extra) extra(p);
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
    }
    std::string runScript(std::string* error) {
        std::string err;
        const bool ok = rt.callScript("Essai", 0.2, &err);
        if (error) *error = ok ? std::string{} : err;
        sim::Value out;
        return rt.environment().read("R", out) ? shown(out) : std::string("#");
    }
};

// ---- LEVER, CATCH e, l'interpolation figee, RELEVER -------------------------
void lever() {
    std::printf("-- LEVER : une erreur a soi, attrapee par CATCH e\n");
    const auto run = [](const std::string& type, const std::string& body, std::string* err = nullptr) {
        Bench b(type, body);
        return b.runScript(err);
    };
    std::string err;

    // LEVER hors d'un TRY : le script s'arrete, la Console montre le code en tete.
    (void)run("STRING", "R := 'avant';\nLEVER('E_Niveau', 'niveau hors plage');\nR := 'apres';", &err);
    check(err.find("E_Niveau") != std::string::npos && err.find("niveau hors plage") != std::string::npos,
          "LEVER hors TRY : arrete le script, le code en tete (" + err + ")");

    // LEVER attrapee par CATCH : le code, le message, la ligne.
    check(run("STRING", "TRY\n    LEVER('E_Niveau', 'niveau hors plage');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_Niveau",
          "CATCH e : e.Code = le code leve");
    check(run("STRING", "TRY\n    LEVER('E_Niveau', 'niveau hors plage');\nCATCH e\n    R := e.Message;\nEND_TRY") == "niveau hors plage",
          "CATCH e : e.Message = le message leve");
    check(run("STRING", "TRY\n    LEVER('E_Niveau', 'niveau hors plage');\nCATCH e\n    R := e;\nEND_TRY") == "niveau hors plage",
          "CATCH e : e seul se lit comme e.Message");
    check(run("DINT", "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    R := e.Ligne;\nEND_TRY") == "2",
          "CATCH e : e.Ligne = la ligne du LEVER (2)");

    // LEVER sans message : le code seul, le message vide.
    check(run("STRING", "TRY\n    LEVER('E_Vide');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_Vide",
          "LEVER('E_Vide') : un code sans message");

    // L'interpolation est figee au moment du LEVER : la valeur passee a la fonction.
    check(run("STRING",
              "FUNCTION Verif(n : REAL)\n    LEVER('E_N', 'niveau {n:0.0} pour cent');\nEND_FUNCTION\n"
              "TRY\n    Verif(3.0);\nCATCH e\n    R := e.Message;\nEND_TRY") == "niveau 3.0 pour cent",
          "LEVER : le message interpole {n:0.0}, fige a la valeur passee (3.0)");

    // Un CATCH e en lecture seule : ecrire e entier (le T_Erreur) est refuse avec le bon message ;
    // ecrire un membre est refuse aussi (le controle dit « lecture seule », le moteur le refuse).
    (void)run("STRING", "VAR\n    g : STRING;\nEND_VAR\ng := 'z';\nTRY\n    LEVER('E_X', 'x');\nCATCH e\n    e := g;\nEND_TRY", &err);
    check(err.find("lecture seule") != std::string::npos, "e := ... (le T_Erreur) : refuse, lecture seule (" + err + ")");
    (void)run("STRING", "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    e.Code := 'autre';\nEND_TRY", &err);
    check(!err.empty(), "e.Code := ... : refuse par le moteur (" + err + ")");

    // RELEVER : reemet l'erreur courante vers le TRY englobant (meme code, meme message).
    check(run("STRING",
              "TRY\n    TRY\n        LEVER('E_Grave', 'panne');\n    CATCH e\n        RELEVER;\n    END_TRY\n"
              "CATCH f\n    R := f.Code;\nEND_TRY") == "E_Grave",
          "RELEVER : le TRY englobant recoit le meme code");
    check(run("STRING",
              "TRY\n    TRY\n        LEVER('E_Grave', 'panne');\n    CATCH e\n        RELEVER;\n    END_TRY\n"
              "CATCH f\n    R := f.Message;\nEND_TRY") == "panne",
          "RELEVER : le meme message");

    // RELEVER hors d'un CATCH : le moteur l'arrete en le disant (le controle le refuse aussi).
    (void)run("STRING", "R := 'avant';\nRELEVER;\nR := 'apres';", &err);
    check(err.find("RELEVER") != std::string::npos && err.find("CATCH") != std::string::npos,
          "RELEVER hors CATCH : refuse (" + err + ")");
}

// ---- les codes E_* des erreurs du moteur ------------------------------------
void codesMoteur() {
    std::printf("-- les erreurs du moteur recoivent un code stable E_*\n");
    const auto code = [](const std::string& fautif) {
        Bench b("STRING", "VAR\n    n : INT;\n    m : MAP[STRING] OF REAL;\n    t : ARRAY[1..3] OF INT;\nEND_VAR\n"
                          "n := 0;\nTRY\n" + fautif + "\nCATCH e\n    R := e.Code;\nEND_TRY");
        std::string err;
        return b.runScript(&err);
    };
    check(code("    R := INT_TO_STRING(10 / n);") == "E_DIVISION", "division par zero -> E_DIVISION");
    check(code("    n := DYNAMIC_CAST<INT>(3.5);") == "E_CONVERSION", "DYNAMIC_CAST refuse -> E_CONVERSION");
    check(code("    R := REAL_TO_STRING(m['absente']);") == "E_CLE", "cle de MAP absente -> E_CLE");
    check(code("    ASSERT(FALSE, 'non');") == "E_ASSERT", "ASSERT faux -> E_ASSERT");
    check(code("    n := t[9];") == "E_BORNE", "indice hors des bornes -> E_BORNE");

    // e.Message reste le message francais de la 1.12.1 (pas un code).
    Bench b("STRING", "VAR\n    n : INT;\nEND_VAR\nn := 0;\nTRY\n    R := INT_TO_STRING(10 / n);\nCATCH e\n    R := e.Message;\nEND_TRY");
    std::string err;
    check(b.runScript(&err).find("division par z") != std::string::npos, "e.Message : le message francais existant");
}

// ---- WAIT_UNTIL, dans un script detache -------------------------------------
void waitUntil() {
    std::printf("-- WAIT_UNTIL : attendre qu'une condition devienne vraie\n");
    const auto var = [](Project& p, const char* n, const char* t, const char* init) {
        Variable v;
        v.id = p.allocate();
        v.name = n;
        v.type = t;
        v.initial = init;
        p.programs.variables.push_back(v);
    };
    const auto script = [](Project& p, const char* n, const std::string& body, bool detached) {
        Script sc;
        sc.id = p.allocate();
        sc.name = n;
        sc.event = "Appel";
        sc.body = body;
        sc.detached = detached;
        p.programs.scripts.push_back(sc);
    };
    const auto value = [](Runtime& rt, const char* n) {
        sim::Value v;
        return rt.environment().read(n, v) ? v.asInteger() : -1;
    };
    const auto errors = [](const Runtime& rt) {
        std::string out;
        for (const auto& e : rt.journal())
            if (e.kind == "Erreur") out += " | " + e.message;
        return out;
    };

    // Comme BOOL, sans attente : une condition deja vraie rend TRUE tout de suite (non detache).
    {
        Project p;
        var(p, "A", "INT", "0");
        var(p, "Go", "BOOL", "TRUE");
        script(p, "Vrai", "IF WAIT_UNTIL(Go, T#5s) THEN A := 1; ELSE A := 2; END_IF", false);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        std::string err;
        const bool ok = rt.callScript("Vrai", 0.1, &err);
        check(ok && value(rt, "A") == 1 && err.empty(), "WAIT_UNTIL comme BOOL : condition deja vraie -> TRUE" + err);
    }

    // Detache : la condition devient vraie plus tard, WAIT_UNTIL reprend et continue.
    {
        Project p;
        var(p, "A", "INT", "0");
        var(p, "Go", "BOOL", "FALSE");
        script(p, "Seq", "A := 1;\nWAIT_UNTIL(Go, T#10s);\nA := 2;", true);
        script(p, "Lancer", "Go := TRUE;", false);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        check(rt.callScript("Seq", 0.1) && value(rt, "A") == 1, "WAIT_UNTIL (instruction) : avant que ce soit vrai, A = 1");
        rt.tick(0.5);
        check(value(rt, "A") == 1 && rt.scriptRunning("Seq"), "WAIT_UNTIL : il attend toujours (A = 1)");
        (void)rt.callScript("Lancer", 0.6);                      // la condition devient vraie
        rt.tick(0.7);
        rt.tick(0.9);
        check(value(rt, "A") == 2 && !rt.scriptRunning("Seq"), "WAIT_UNTIL : la condition vraie -> la suite (A = 2)");
        check(errors(rt).empty(), "WAIT_UNTIL : aucune erreur quand la condition devient vraie" + errors(rt));
    }

    // Detache, en instruction, delai depasse : leve E_ATTENTE (le script s'arrete).
    {
        Project p;
        var(p, "A", "INT", "0");
        var(p, "Go", "BOOL", "FALSE");
        script(p, "Delai", "A := 1;\nWAIT_UNTIL(Go, T#1s);\nA := 2;", true);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        (void)rt.callScript("Delai", 0.1);
        rt.tick(0.5);
        rt.tick(1.3);                                            // au-dela de 1 s
        rt.tick(1.4);
        check(value(rt, "A") == 1 && !rt.scriptRunning("Delai"), "WAIT_UNTIL (instruction) delai depasse : la suite ne tourne pas (A = 1)");
        check(errors(rt).find("E_ATTENTE") != std::string::npos, "WAIT_UNTIL (instruction) delai depasse : E_ATTENTE" + errors(rt));
    }

    // Detache, comme BOOL, delai depasse : rend FALSE sans erreur.
    {
        Project p;
        var(p, "A", "INT", "0");
        var(p, "Go", "BOOL", "FALSE");
        script(p, "Faux", "A := 5;\nIF WAIT_UNTIL(Go, T#1s) THEN\n    A := 1;\nELSE\n    A := 2;\nEND_IF", true);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        (void)rt.callScript("Faux", 0.1);
        rt.tick(0.5);
        rt.tick(1.3);
        rt.tick(1.4);
        check(value(rt, "A") == 2, "WAIT_UNTIL comme BOOL : delai depasse -> FALSE (A = 2)");
        check(errors(rt).empty(), "WAIT_UNTIL comme BOOL : delai depasse ne leve pas d'erreur" + errors(rt));
    }

    // Enchainer WAIT_UNTIL (E_ATTENTE) et LEVER d'un autre code, dans un TRY.
    {
        Project p;
        var(p, "A", "INT", "0");
        var(p, "Go", "BOOL", "FALSE");
        var(p, "Code", "STRING", "");
        script(p, "Securite",
               "TRY\n    WAIT_UNTIL(Go, T#1s);\nCATCH e\n    Code := e.Code;\n    LEVER('E_Securite', 'vanne non confirmee');\nEND_TRY", true);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        (void)rt.callScript("Securite", 0.1);
        rt.tick(0.5);
        rt.tick(1.3);
        rt.tick(1.4);
        sim::Value c;
        check(rt.environment().read("Code", c) && c.asString() == "E_ATTENTE", "WAIT_UNTIL en instruction dans un TRY : son CATCH voit E_ATTENTE");
    }
}

// ---- le controle de l'editeur (Compiler) ------------------------------------
void controle() {
    std::printf("-- le controle : RELEVER hors CATCH, WAIT_UNTIL en fx, condition non BOOL, CATCH e\n");
    scriptcheck::Scope sc;
    const auto has = [](const std::vector<scriptcheck::Finding>& f, const std::string& s) {
        for (const auto& x : f)
            if (x.severity == scriptcheck::Finding::Severity::Error && x.message.find(s) != std::string::npos) return true;
        return false;
    };
    const auto clean = [](const std::vector<scriptcheck::Finding>& f) {
        for (const auto& x : f)
            if (x.severity == scriptcheck::Finding::Severity::Error) return false;
        return true;
    };

    // RELEVER hors d'un CATCH : une erreur du controle ; dans un CATCH : rien.
    check(has(scriptcheck::check(sc, "RELEVER;"), "RELEVER"), "controle : RELEVER hors CATCH -> erreur");
    check(clean(scriptcheck::check(sc, "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    RELEVER;\nEND_TRY")), "controle : RELEVER dans un CATCH -> rien");

    // CATCH e : e, e.Code, e.Message, e.Ligne connus ; un autre membre refuse.
    check(clean(scriptcheck::check(sc, "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    VAR s : STRING; END_VAR\n    s := e.Code;\nEND_TRY")),
          "controle : e.Code connu (pas 'inconnu')");
    check(has(scriptcheck::check(sc, "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    VAR s : STRING; END_VAR\n    s := e.Truc;\nEND_TRY"), "T_Erreur"),
          "controle : e.Truc -> T_Erreur n'a que Code, Message et Ligne");
    check(has(scriptcheck::check(sc, "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    e.Message := 'autre';\nEND_TRY"), "lecture seule"),
          "controle : e.Message := ... -> lecture seule");

    // WAIT_UNTIL : la condition doit etre un BOOL - le moteur le refuse strictement (comme ASSERT).
    {
        Bench b("STRING", "WAIT_UNTIL(Compteur, T#5ms);");
        std::string err;
        (void)b.runScript(&err);
        check(err.find("BOOL") != std::string::npos, "WAIT_UNTIL : condition non BOOL refusee par le moteur (" + err + ")");
    }

    // WAIT_UNTIL dans une expression de vue (fx) : refuse, comme WAIT.
    exprcheck::Context c;
    c.known = [](std::string_view) { return true; };
    bool fxRefuse = false;
    for (const auto& pb : exprcheck::check(c, "WAIT_UNTIL(Marche, T#1s)"))
        if (pb.message.find("WAIT_UNTIL") != std::string::npos) fxRefuse = true;
    check(fxRefuse, "controle : WAIT_UNTIL dans une expression (fx) -> refuse");
}

// ---- le ST de l'automate ne connait rien de tout cela -----------------------
void automateInchange() {
    std::printf("-- le ST de l'automate (hmiDialect=false) : LEVER/RELEVER/WAIT_UNTIL ne sont pas des mots-cles\n");
    const auto parses = [](const char* src, bool dialect) {
        return static_cast<bool>(sim::parse(src, "essai", sim::ParseOptions{dialect}));
    };
    // LEVER : une instruction du dialecte (LEVER() sans code y est refuse) ; dans l'automate,
    // c'est un appel ordinaire (0 argument y passe a l'analyse, comme n'importe quelle fonction).
    check(parses("LEVER();", false), "automate : LEVER() passe comme un appel ordinaire");
    check(!parses("LEVER();", true), "dialecte : LEVER() sans code est refuse (une instruction)");
    check(parses("LEVER('E_X', 'msg');", true), "dialecte : LEVER('E_X', 'msg') se lit (une instruction)");

    // RELEVER : une instruction du dialecte ; dans l'automate, c'est un nom ordinaire
    // (RELEVER; seul n'est ni un appel ni une affectation : une erreur de l'automate).
    check(parses("RELEVER;", true), "dialecte : RELEVER; se lit (une instruction)");
    check(!parses("RELEVER;", false), "automate : RELEVER; seul n'est pas une instruction (une erreur)");

    // WAIT_UNTIL : une native du dialecte ; dans l'automate, un appel ordinaire (a l'analyse,
    // rien ne distingue WAIT_UNTIL d'une autre fonction : la grammaire n'a pas de mot-cle de plus).
    check(parses("x := WAIT_UNTIL(y);", false), "automate : WAIT_UNTIL(...) passe comme un appel ordinaire");
}

} // namespace

int main() {
    std::printf("1.13.0 : lever ses erreurs (LEVER, RELEVER, CATCH e, codes E_*) et WAIT_UNTIL\n");
    lever();
    codesMoteur();
    waitUntil();
    controle();
    automateInchange();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
