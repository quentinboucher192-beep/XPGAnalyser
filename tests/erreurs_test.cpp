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
#include "../src/hmi/HmiCheck.hpp"         // relecture : Generer (une fonction nommee Lever)
#include "../src/sim/Interpreter.hpp"     // le ST de l'automate (sans le dialecte)

#include <cctype>
#include <cstdio>
#include <map>
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
    // LEVER : une instruction du dialecte ; dans l'automate, un appel ordinaire (0 argument y passe a l'analyse,
    // comme n'importe quelle fonction). 1.13.0 (relecture) : dans le dialecte aussi, LEVER() se LIT (une fonction
    // nommee Lever d'un projet d'avant le prend a l'execution) ; sans elle, Compiler et le moteur le refusent.
    check(parses("LEVER();", false), "automate : LEVER() passe comme un appel ordinaire");
    check(parses("LEVER();", true), "dialecte : LEVER() se lit (une fonction Lever d'avant peut le prendre)");
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

// ---- la relecture de la 1.13.0 (groupe B, les erreurs) ------------------------
//  Chaque essai suit le scenario d'un defaut confirme : il echouait avant la correction.
namespace {
// Une fonction du projet (le corps porte ses declarations, VAR_IN_OUT : une fonction aux types riches, qui tourne
// dans le dialecte comme une methode).
void addFunction(Project& p, const std::string& name, const std::string& body, const std::string& ret = {}) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    f.body = body;
    p.programs.functions.push_back(f);
}
void addVariable(Project& p, const char* n, const char* t, const char* init) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
}
// Un environnement minimal pour le moteur seul : des variables, l'attente immediate (WAIT_UNTIL non detache).
struct WaitEnv final : sim::Environment {
    std::map<std::string, sim::Value> vars;
    std::vector<sim::Diagnostic> diags;
    std::vector<std::int64_t> waits;
    static std::string key(std::string_view n) {
        std::string u(n);
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return u;
    }
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = vars.find(key(n));
        if (it == vars.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        vars[key(n)] = v;
        return true;
    }
    bool exists(std::string_view n) override { return vars.count(key(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic d) override { diags.push_back(std::move(d)); }
    bool wait(std::int64_t ms, std::string&) override {
        waits.push_back(ms);
        return true;
    }
};
} // namespace

void relecture() {
    std::printf("-- relecture 1.13.0 : erreurs (CATCH imbriques, codes, LEVER dans une fonction, Lever/Relever)\n");
    std::string err;
    const auto run = [](const std::string& type, const std::string& body, const std::function<void(Project&)>& extra = {},
                        std::string* error = nullptr) {
        Bench b(type, body, extra);
        return b.runScript(error);
    };

    // n27 : deux CATCH e imbriques - le CATCH interieur s'execute, puis l'exterieur revient.
    check(run("STRING", "VAR\n    n : INT;\nEND_VAR\nn := 0;\nTRY\n    Compteur := 10 / n;\nCATCH e\n    TRY\n        LEVER('E_Log', 'x');\n"
                        "    CATCH e\n        R := e.Code;\n    END_TRY\n    R := CONCAT(R, '/', e.Code);\nEND_TRY", {}, &err) == "E_Log/E_DIVISION",
          "n27 : CATCH e dans le handler d'un CATCH e : l'interieur voit E_Log, puis l'exterieur E_DIVISION (" + err + ")");
    // ... et une fonction interne du script, appelee depuis le handler, avec son propre TRY ... CATCH e.
    check(run("STRING", "FUNCTION Journaliser : STRING\n    TRY\n        LEVER('E_Interne', 'y');\n    CATCH e\n        Journaliser := e.Code;\n    END_TRY\n"
                        "END_FUNCTION\nTRY\n    LEVER('E_Ext', 'x');\nCATCH e\n    R := CONCAT(Journaliser(), '/', e.Code);\nEND_TRY", {}, &err)
              == "E_Interne/E_Ext",
          "n27 : une fonction interne au TRY ... CATCH e, appelee depuis un CATCH e (" + err + ")");

    // n28 : e.Code juste pour une erreur nee dans une fonction isolee (une fonction du projet aux types riches).
    const auto withFunctions = [](Project& p) {
        addVariable(p, "Diviseur", "INT", "0");
        addFunction(p, "Charger_JSON", "VAR_IN_OUT\n    c : INT;\nEND_VAR\nc := 10 / Diviseur;");
        addFunction(p, "Verifier_Haut", "VAR_IN_OUT\n    c : INT;\nEND_VAR\nASSERT(c < 5, 'haut');");
        addFunction(p, "Charger_Simple_JSON", "Compteur := 10 / Diviseur;");
    };
    check(run("STRING", "TRY\n    Charger_JSON(Compteur);\nCATCH e\n    R := e.Code;\nEND_TRY", withFunctions, &err) == "E_DIVISION",
          "n28 : une division par zero dans Charger_JSON : e.Code = E_DIVISION (pas E_JSON)");
    check(run("STRING", "TRY\n    Verifier_Haut(Compteur);\nCATCH e\n    R := e.Code;\nEND_TRY", withFunctions, &err) == "E_ASSERT",
          "n28 : ASSERT faux dans une fonction : e.Code = E_ASSERT (pas E_MOTEUR)");
    check(run("STRING", "TRY\n    Charger_Simple_JSON();\nCATCH e\n    R := e.Code;\nEND_TRY", withFunctions, &err) == "E_DIVISION",
          "n28 : une division par zero dans une fonction simple nommee ..._JSON : E_DIVISION (pas E_JSON)");

    // n44 : LEVER dans une fonction isolee : e.Ligne = la ligne de l'appel ; la Console nomme la fonction.
    const auto withVerif = [](Project& p) {
        addFunction(p, "Verif", "VAR_IN_OUT\n    c : INT;\nEND_VAR\nIF c > 0 THEN\n    LEVER('E_N', 'trop');\nEND_IF;");
    };
    check(run("DINT", "Compteur := 7;\nTRY\n    Verif(Compteur);\nCATCH e\n    R := e.Ligne;\nEND_TRY", withVerif, &err) == "3",
          "n44 : LEVER dans Verif, attrape : e.Ligne = 3 (la ligne de l'appel), comme une erreur du moteur");
    (void)run("STRING", "R := 'a';\nVerif(Compteur);", withVerif, &err);
    check(err.find("Verif") != std::string::npos && err.find("E_N") != std::string::npos,
          "n44 : LEVER non attrape dans Verif : la Console nomme la fonction (" + err + ")");

    // n45 : LEVER a gauche de ?? : le repli est pris, le script continue (comme une erreur du moteur).
    const auto withSeuil = [](Project& p) {
        addFunction(p, "Lire_Seuil", "VAR_IN_OUT\n    c : INT;\nEND_VAR\nLEVER('E_Seuil', 'inconnu');\nLire_Seuil := 1.0;", "REAL");
        addFunction(p, "Seuil_Sur", "VAR_IN_OUT\n    c : INT;\nEND_VAR\nTRY\n    LEVER('E_X', 'x');\nCATCH e\n    Seuil_Sur := 5.0;\nEND_TRY;", "REAL");
    };
    check(run("REAL", "R := Lire_Seuil(Compteur) ?? 80.0;\nR := R + 1.0;", withSeuil, &err) == "81" && err.empty(),
          "n45 : Lire_Seuil(...) ?? 80.0 avec un LEVER : 80 et le script continue (" + err + ")");
    check(run("REAL", "R := Seuil_Sur(Compteur) ?? 80.0;", withSeuil, &err) == "5",
          "n45 : un TRY dans la fonction a gauche de ?? attrape encore son LEVER (5)");

    // n14 : une variable nommee Relever ou Lever (un projet d'avant) : Compiler ne la prend plus pour RELEVER / LEVER.
    {
        Project p;
        addVariable(p, "Relever", "BOOL", "FALSE");
        addVariable(p, "Lever", "INT", "0");
        addVariable(p, "Marche", "BOOL", "FALSE");
        addVariable(p, "Compteur", "INT", "0");
        scriptcheck::Scope sc;
        sc.project = &p;
        sc.plcKnown = [](std::string_view) { return false; };
        std::string all;
        for (const auto& f : scriptcheck::check(sc, "Relever := TRUE;\nMarche := Relever;\nLever := 3;\nCompteur := Lever + 1;"))
            if (f.severity == scriptcheck::Finding::Severity::Error) all += " | " + f.message;
        check(all.empty(), "n14 : Relever := TRUE; Marche := Relever; Lever := 3 : aucune erreur" + all);
        // le moteur aussi.
        check(run("STRING", "Relever := TRUE;\nIF Relever THEN R := 'ok'; END_IF;",
                  [](Project& q) { addVariable(q, "Relever", "BOOL", "FALSE"); }, &err) == "ok",
              "n14 : le moteur lit Relever comme une variable (" + err + ")");
        // RELEVER seul hors CATCH reste refuse, et un ';' oublie avant RELEVER se lit encore.
        std::string relever;
        for (const auto& f : scriptcheck::check(sc, "RELEVER;"))
            if (f.severity == scriptcheck::Finding::Severity::Error) relever += f.message;
        check(relever.find("RELEVER") != std::string::npos, "n14 : RELEVER seul hors CATCH reste refuse");
    }

    // n49 : une fonction du projet nommee Lever (lever une barriere) : Lever(Barriere1); l'appelle encore.
    {
        const auto withLever = [](Project& p) {
            addVariable(p, "Barriere1", "BOOL", "FALSE");
            HmiFunction f;
            f.id = p.allocate();
            f.name = "Lever";
            f.body = "VAR_INPUT\n    b : BOOL;\nEND_VAR\nR := 'barriere levee';";
            p.programs.functions.push_back(f);
        };
        check(run("STRING", "Lever(Barriere1);", withLever, &err) == "barriere levee" && err.empty(),
              "n49 : Lever(Barriere1); appelle la fonction Lever du projet (" + err + ")");
        Project p;
        withLever(p);
        const auto is = generate(p, [](std::string_view) { return false; });
        bool warned = false;
        for (const auto& i : is) warned = warned || (i.severity == Issue::Severity::Warning && i.message.find("Lever_Barriere") != std::string::npos);
        check(warned, "n49 : Generer conseille de renommer la fonction Lever");
        scriptcheck::Scope sc;
        sc.project = &p;
        std::string errs;
        for (const auto& f : scriptcheck::check(sc, "Lever(Barriere1);"))
            if (f.severity == scriptcheck::Finding::Severity::Error) errs += " | " + f.message;
        check(errs.find("LEVER") == std::string::npos, "n49 : Compiler lit Lever(Barriere1) comme un appel" + errs);
        // sans fonction Lever, LEVER() reste refuse (Compiler et le moteur).
        scriptcheck::Scope none;
        std::string zero;
        for (const auto& f : scriptcheck::check(none, "LEVER();")) zero += f.message;
        check(zero.find("il manque le code") != std::string::npos, "n49 : LEVER() sans fonction Lever : il manque le code");
        (void)run("STRING", "LEVER();", {}, &err);
        check(err.find("LEVER") != std::string::npos, "n49 : LEVER() sans fonction Lever : le moteur le refuse (" + err + ")");
    }

    // n34 : le nom d'un CATCH ne vaut que dans son handler : FOR EACH e ailleurs n'est pas un T_Erreur.
    {
        scriptcheck::Scope sc;
        std::string all;
        for (const auto& f : scriptcheck::check(sc, "VAR\n    L : LIST OF REAL;\n    Total : REAL;\nEND_VAR\nTRY\n    Total := 1.0;\nCATCH e\n    Total := 0.0;\nEND_TRY\n"
                                                    "FOR EACH e IN L DO\n    Total := Total + e;\nEND_FOR;"))
            if (f.message.find("T_Erreur") != std::string::npos || f.message.find("lecture seule") != std::string::npos) all += " | " + f.message;
        check(all.empty(), "n34 : FOR EACH e apres un CATCH e : e n'y est pas l'erreur attrapee" + all);
    }

    // n40 / n51 : WAIT_UNTIL non detache, delai depasse : le message dit le temps vraiment attendu (10 s au plus).
    {
        const auto timeout = [](const char* src) {
            WaitEnv env;
            env.vars["PRET"] = sim::Value::boolean(false);
            auto prog = sim::parse(src, "essai", sim::ParseOptions{true});
            if (!prog) return std::string("illisible : ") + prog.error().message();
            (void)sim::execute(**prog, env);
            std::string all;
            for (const auto& d : env.diags) all += d.message;
            return all;
        };
        const std::string sans = timeout("WAIT_UNTIL(Pret);");
        check(sans.find("E_ATTENTE") != std::string::npos && sans.find("10 s") != std::string::npos && sans.find("0 ms") == std::string::npos,
              "n40 : WAIT_UNTIL(Pret) non detache : 'en 10 s' (pas 'en 0 ms') : " + sans);
        const std::string long2 = timeout("WAIT_UNTIL(Pret, T#2m);");
        check(long2.find("10 s") != std::string::npos && long2.find("2 min") == std::string::npos,
              "n40 : WAIT_UNTIL(Pret, T#2m) non detache : 'en 10 s' (pas 'en 2 min') : " + long2);
    }

    // n41 : WAIT_UNTIL dans un script non detache : Compiler l'avertit (coche Detache) ; detache : rien.
    {
        Project p;
        addVariable(p, "Pret", "BOOL", "FALSE");
        Script a;
        a.id = p.allocate();
        a.name = "Attente";
        a.event = "Appel";
        a.body = "WAIT_UNTIL(Pret, T#2m);";
        p.programs.scripts.push_back(a);
        const auto warnings = [](const Project& q) {
            std::string out;
            for (const auto& i : scriptcheck::projectIssues(q, [](std::string_view) { return false; }, {}, nullptr))
                if (i.severity == Issue::Severity::Warning && i.message.find("WAIT_UNTIL dans un script non d") != std::string::npos) out += i.message;
            return out;
        };
        check(!warnings(p).empty(), "n41 : WAIT_UNTIL dans un script non detache : Compiler avertit");
        p.programs.scripts.back().detached = true;
        check(warnings(p).empty(), "n41 : detache : pas d'avertissement");
    }
}

// La seconde relecture (lot D2).
void seconde() {
    std::printf("-- seconde relecture : la portee d'un CATCH (R17), ?? imbrique dans un TRY (R21)\n");
    // R21 : a ?? b dans un TRY d'une fonction appelee a gauche d'un autre ?? : ?? choisit b, le CATCH ne s'execute pas.
    {
        std::string err;
        Bench b("INT", "FUNCTION F : INT\nVAR\n    x : INT;\n    d : INT;\nEND_VAR\nd := 0;\nTRY\n    x := (10 / d) ?? 1;\nCATCH e\n    x := -1;\n"
                       "END_TRY\nF := x;\nEND_FUNCTION\nR := F() ?? 0;");
        const std::string r = b.runScript(&err);
        check(r == "1", "R21 : x := (10 / d) ?? 1 dans le TRY de F, appelee a gauche de ?? : x = 1, pas le CATCH (-1) [" + r + " " + err + "]");
    }
    // R17 : hors de son handler, le nom d'un CATCH ne masque plus un nom inconnu (dans une boucle FOR EACH aussi), et
    // n'est pas un STRING pour la lecture du dialecte.
    {
        scriptcheck::Scope sc;
        sc.plcKnown = [](std::string_view) { return false; };
        std::string all;
        for (const auto& f : scriptcheck::check(sc, "VAR\n    L : LIST OF REAL;\n    Total : REAL;\nEND_VAR\nTRY\n    Total := 1.0;\nCATCH Ecart\n    Total := 0.0;\nEND_TRY\n"
                                                    "FOR EACH x IN L DO\n    Total := Total + Ecart;\nEND_FOR;"))
            all += " | " + f.message;
        check(all.find("Ecart") != std::string::npos, "R17 : Ecart (le nom du CATCH) lu hors de son handler, dans un FOR EACH : dit" + all);
    }
    // R24 : une fonction Lever de l'utilisateur : son appel LEVER(x, y) est controle comme un appel ; sans elle, LEVER(...)
    // n'est pas une « fonction LEVER inconnue » (les appels de ses arguments restent controles).
    {
        struct LeverEnv final : sim::Environment {
            int lever{0};
            bool read(std::string_view, sim::Value&) override { return false; }
            bool write(std::string_view, const sim::Value&) override { return true; }
            bool exists(std::string_view) override { return false; }
            bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
            void report(sim::Diagnostic) override {}
            int overloads(std::string_view n) override {
                std::string u(n);
                for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return u == "LEVER" ? lever : 0;
            }
        };
        const auto calleesOf = [](const char* src, int lever) {
            auto prog = sim::parse(src, "essai", sim::ParseOptions{true});
            std::string out;
            if (!prog) return std::string("illisible");
            LeverEnv env;
            env.lever = lever;
            for (const auto& c : sim::callSites(**prog, env)) out += c.callee + ";";
            return out;
        };
        check(calleesOf("LEVER('E_Barriere', 2);", 1).find("LEVER;") != std::string::npos,
              "R24 : une fonction Lever existe : LEVER('E_Barriere', 2) est un appel controle (" + calleesOf("LEVER('E_Barriere', 2);", 1) + ")");
        check(calleesOf("LEVER('E_X', 'm');", 0).find("LEVER") == std::string::npos, "R24 : sans fonction Lever : LEVER('E_X', 'm') n'est pas un appel");
        const std::string bad = calleesOf("LEVER(F(), 2, 3);", 0);
        check(bad.find("LEVER") == std::string::npos && bad.find("F;") != std::string::npos,
              "R24 : sans fonction Lever, LEVER(F(), 2, 3) : pas de \"fonction LEVER inconnue\", F() reste controle (" + bad + ")");
    }
    // R17 : names::check (knownOtherwise) - un nom d'un espace, ecrit court hors du handler d'un CATCH du meme nom : dit.
    {
        Project p;
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Ecart";
        f.ns = "Gaz";
        f.returnType = "REAL";
        f.body = "Ecart := 1.0;";
        p.programs.functions.push_back(f);
        scriptcheck::Scope sc;
        sc.project = &p;
        sc.plcKnown = [](std::string_view) { return false; };
        std::string all;
        for (const auto& fd : scriptcheck::check(sc, "VAR\n    Total : REAL;\nEND_VAR\nTRY\n    Total := 1.0;\nCATCH Ecart\n    Total := 0.0;\nEND_TRY\n"
                                                     "Total := Ecart();"))
            all += " | " + fd.message;
        check(all.find("Ecart est dans l'espace Gaz") != std::string::npos, "R17 : hors du handler, Ecart() (Gaz.Ecart) est dit, pas masque par CATCH Ecart" + all);
        std::string inside;
        for (const auto& fd : scriptcheck::check(sc, "VAR\n    T : STRING;\nEND_VAR\nTRY\n    T := 'a';\nCATCH Ecart\n    T := Ecart.Message;\nEND_TRY"))
            if (fd.severity == scriptcheck::Finding::Severity::Error) inside += " | " + fd.message;
        check(inside.empty(), "R17 : dans son handler, Ecart.Message reste l'erreur attrapee" + inside);
    }
}

// ---- les captures de la 1.13.0 (lot F1) ---------------------------------------
//  C1 : RELEVER refuse par Generer (« variable inexistante : RELEVER »).
//  C2 : un LEVER dans une fonction IHM simple, appelee sous TRY, arrivait au CATCH en E_MOTEUR
//  (« 'Verifier_Niveau' : fonction ou bloc inconnu du simulateur ») et ecrivait une ERROR a la Console.
namespace {
// Une fonction du projet comme l'editeur la range : ses parametres dans ses declarations.
void addSimpleFunction(Project& p, const std::string& name, const std::string& ret,
                       const std::vector<std::pair<std::string, std::string>>& params, const std::string& body) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    for (const auto& [pn, pt] : params) {
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = pn;
        d.type = pt;
        d.mode = PassMode::In;
        f.decls.push_back(std::move(d));
    }
    f.body = body;
    p.programs.functions.push_back(std::move(f));
}
std::string journalOf(const Runtime& rt, const std::string& kind) {
    std::string out;
    for (const auto& e : rt.journal())
        if (e.kind == kind) out += " | " + e.message;
    return out;
}
const char* kVerifierNiveau =
    "(* Leve E_Niveau quand le niveau sort de 0..100 % *)\n"            // 1
    "IF n < 0.0 OR n > 100.0 THEN\n"                                     // 2
    "    LEVER('E_Niveau', 'niveau hors plage : {n:0.0} %');\n"          // 3
    "END_IF;\n"                                                          // 4
    "Verifier_Niveau := TRUE;\n";                                        // 5
// Le CATCH ecrit tout ce qu'il recoit : code | ligne | message.
const char* kCatchAll = "CATCH e\n    R := CONCAT(e.Code, '|', DINT_TO_STRING(e.Ligne), '|', e.Message);\nEND_TRY";
} // namespace

void capturesF1() {
    std::printf("-- captures 1.13.0 (lot F1) : RELEVER passe Generer ; un LEVER traverse l'appel d'une fonction\n");
    // ---- C1 : Generer (HmiCheck) ne prend plus RELEVER pour une variable inexistante ----
    {
        Project p;
        addVariable(p, "D_NIVEAU_MESURE", "REAL", "0.0");
        addVariable(p, "Compteur", "INT", "0");
        Script sc;
        sc.id = p.allocate();
        sc.name = "Controle_Niveau";
        sc.event = "Appel";
        sc.body = "(* Controle du niveau : une erreur levee, rattrapee, puis relevee *)\n"
                  "n := D_NIVEAU_MESURE;\n"
                  "TRY\n    IF n > 100.0 THEN\n        LEVER('E_Niveau', 'niveau hors plage : {n:0.0} %');\n    END_IF;\n"
                  "CATCH e\n    IHM_LOG(WARNING, 'rattrape : {e.Code} - {e.Message}');\n    RELEVER;   (* l erreur repart *)\nEND_TRY;";
        Declaration n;
        n.id = p.allocate();
        n.kind = DeclKind::Variable;
        n.name = "n";
        n.type = "REAL";
        sc.decls.push_back(n);
        p.programs.scripts.push_back(sc);
        // Une fonction du projet aussi : RELEVER dans son CATCH, sur la meme ligne qu'un appel (un ';' oublie).
        addSimpleFunction(p, "Relire", "BOOL", {{"x", "REAL"}},
                          "TRY\n    LEVER('E_X', 'x');\nCATCH e\n    IHM_LOG(INFO, 'vu') RELEVER\nEND_TRY;\nRelire := TRUE;");
        std::string all;
        for (const auto& i : generate(p, [](std::string_view) { return false; }))
            if (i.severity == Issue::Severity::Error) all += " | " + i.message;
        check(all.find("RELEVER") == std::string::npos, "C1 : Generer ne dit plus 'variable inexistante : RELEVER'" + all);
        // Un nom Relever LU dans une expression reste un nom : inconnu, il est dit.
        p.programs.scripts.back().body = "Compteur := Relever + 1;";
        p.programs.scripts.back().decls.clear();
        p.programs.functions.clear();
        all.clear();
        for (const auto& i : generate(p, [](std::string_view) { return false; }))
            if (i.severity == Issue::Severity::Error) all += " | " + i.message;
        check(all.find("variable inexistante : Relever") != std::string::npos, "C1 : Compteur := Relever + 1 (Relever inconnu) reste dit" + all);
    }

    // ---- C2 : un LEVER dans une fonction IHM simple, appelee sous TRY ----
    const auto withVerifier = [](Project& p) {
        addVariable(p, "Mesure", "REAL", "120.0");
        addVariable(p, "Ok", "BOOL", "FALSE");
        addSimpleFunction(p, "Verifier_Niveau", "BOOL", {{"n", "REAL"}}, kVerifierNiveau);
    };
    {
        Bench b("STRING", std::string("TRY\n    Ok := Verifier_Niveau(Mesure);\n    R := 'dans la plage';\n") + kCatchAll, withVerifier);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r == "E_Niveau|2|niveau hors plage : 120.0 %", "C2 : le CATCH recoit E_Niveau, la ligne de l'appel (2) et le message [" + r + "]");
        check(err.empty(), "C2 : rattrapee, l'erreur n'arrete pas le script (" + err + ")");
        check(journalOf(b.rt, "Erreur").empty(), "C2 : rattrapee, pas de ligne ERROR a la Console" + journalOf(b.rt, "Erreur"));
    }
    // Non rattrapee : la Console nomme la fonction, sa ligne et le code (une seule ligne).
    {
        Bench b("STRING", "R := 'a';\nOk := Verifier_Niveau(Mesure);\nR := 'b';", withVerifier);
        std::string err;
        const std::string r = b.runScript(&err);
        const std::string errs = journalOf(b.rt, "Erreur");
        check(r == "a" && err.find("E_Niveau") != std::string::npos && err.find("Verifier_Niveau") != std::string::npos,
              "C2 : non rattrapee, le script s'arrete et le dit (" + err + ")");
        check(errs.find("E_Niveau") != std::string::npos && errs.find("ligne 3") != std::string::npos
                  && errs.find(" | ", 3) == std::string::npos,
              "C2 : non rattrapee, une seule ERROR, la ligne 3 de la fonction" + errs);
    }
    // Deux niveaux : Controle appelle Verifier_Niveau (sans TRY) ; le TRY du script recoit E_Niveau a SA ligne.
    {
        const auto twoLevels = [&](Project& p) {
            withVerifier(p);
            addSimpleFunction(p, "Controle", "BOOL", {{"v", "REAL"}}, "Controle := Verifier_Niveau(v);\nControle := FALSE;");
        };
        Bench b("STRING", std::string("R := 'x';\nTRY\n    Ok := Controle(Mesure);\n") + kCatchAll, twoLevels);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r == "E_Niveau|3|niveau hors plage : 120.0 %" && err.empty(), "C2 : deux niveaux d'appel : E_Niveau a la ligne 3 du script [" + r + "] " + err);
        check(journalOf(b.rt, "Erreur").empty(), "C2 : deux niveaux, rattrapee : pas d'ERROR" + journalOf(b.rt, "Erreur"));
    }
    // Le TRY est dans la fonction intermediaire : elle l'attrape, le script n'en sait rien.
    {
        const auto inner = [&](Project& p) {
            withVerifier(p);
            addSimpleFunction(p, "Controle_Sur", "STRING", {{"v", "REAL"}},
                              "TRY\n    Ok := Verifier_Niveau(v);\n    Controle_Sur := 'ok';\nCATCH e\n    Controle_Sur := e.Code;\nEND_TRY;");
        };
        Bench b("STRING", "R := Controle_Sur(Mesure);", inner);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r == "E_Niveau" && err.empty() && journalOf(b.rt, "Erreur").empty(),
              "C2 : le TRY d'une fonction attrape le LEVER de celle qu'elle appelle [" + r + "] " + err + journalOf(b.rt, "Erreur"));
    }
    // RELEVER dans la fonction : l'erreur repart vers le TRY du script, telle quelle.
    {
        const auto relay = [&](Project& p) {
            withVerifier(p);
            addSimpleFunction(p, "Relais", "BOOL", {{"v", "REAL"}},
                              "TRY\n    Relais := Verifier_Niveau(v);\nCATCH e\n    IHM_LOG(INFO, 'vu {e.Code}');\n    RELEVER;\nEND_TRY;");
        };
        Bench b("STRING", std::string("TRY\n    Ok := Relais(Mesure);\n") + kCatchAll, relay);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r == "E_Niveau|2|niveau hors plage : 120.0 %" && err.empty() && journalOf(b.rt, "Erreur").empty(),
              "C2 : RELEVER dans une fonction : le TRY du script recoit l'erreur telle quelle [" + r + "] " + err + journalOf(b.rt, "Erreur"));
    }
    // Une erreur du moteur dans une fonction simple : son code ET son message francais (pas « fonction inconnue »).
    {
        const auto div = [](Project& p) {
            addVariable(p, "Diviseur", "INT", "0");
            addSimpleFunction(p, "Diviser", "INT", {{"a", "INT"}}, "Diviser := a / Diviseur;");
        };
        Bench b("STRING", std::string("TRY\n    Compteur := Diviser(10);\n") + kCatchAll, div);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r.rfind("E_DIVISION|2|", 0) == 0 && r.find("division par z") != std::string::npos && r.find("inconnu") == std::string::npos,
              "C2 : division par zero dans une fonction simple : E_DIVISION, son message [" + r + "]");
        check(err.empty() && journalOf(b.rt, "Erreur").empty(), "C2 : rattrapee : ni echec ni ERROR" + err + journalOf(b.rt, "Erreur"));
    }
    // ?? : a gauche, le LEVER d'une fonction simple choisit la valeur de droite, sans ERROR.
    {
        Bench b("REAL", "R := TO_REAL(Verifier_Niveau(Mesure)) ?? 80.0;", withVerifier);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r == "80" && err.empty() && journalOf(b.rt, "Erreur").empty(), "C2 : Verifier_Niveau(...) ?? : 80, sans ERROR [" + r + "] " + err + journalOf(b.rt, "Erreur"));
    }
    // Une methode de type, un operateur : le LEVER arrive au CATCH du script.
    {
        const auto typed = [](Project& p) {
            HmiType t;
            t.id = p.allocate();
            t.name = "T_Cuve";
            TypeMember m;
            m.name = "Niveau";
            m.type = "REAL";
            t.members.push_back(m);
            HmiFunction meth;
            meth.id = p.allocate();
            meth.name = "Verifier";
            meth.body = "IF Niveau > 100.0 THEN\n    LEVER('E_Cuve', 'cuve pleine : {Niveau:0.0}');\nEND_IF;\n";
            t.methods.push_back(meth);
            HmiOperator op;
            op.id = p.allocate();
            op.op = "+";
            op.left = "T_Cuve";
            op.right = "T_Cuve";
            op.result = "T_Cuve";
            op.body = "Resultat.Niveau := a.Niveau + b.Niveau;\nIF Resultat.Niveau > 100.0 THEN\n    LEVER('E_Somme', 'trop');\nEND_IF;\n";
            t.operators.push_back(op);
            p.programs.types.push_back(t);
            addVariable(p, "c1", "T_Cuve", "");
            addVariable(p, "c2", "T_Cuve", "");
            addVariable(p, "c3", "T_Cuve", "");
        };
        Bench m("STRING", std::string("c1.Niveau := 150.0;\nTRY\n    c1.Verifier();\n") + kCatchAll, typed);
        std::string err;
        std::string r = m.runScript(&err);
        check(r == "E_Cuve|3|cuve pleine : 150.0" && err.empty() && journalOf(m.rt, "Erreur").empty(),
              "C2 : une methode : E_Cuve, la ligne de l'appel (3) [" + r + "] " + err + journalOf(m.rt, "Erreur"));
        Bench o("STRING", std::string("c1.Niveau := 60.0;\nc2.Niveau := 50.0;\nTRY\n    c3 := c1 + c2;\n") + kCatchAll, typed);
        r = o.runScript(&err);
        check(r == "E_Somme|4|trop" && err.empty() && journalOf(o.rt, "Erreur").empty(),
              "C2 : un operateur : E_Somme, la ligne de l'appel (4) [" + r + "] " + err + journalOf(o.rt, "Erreur"));
    }
    // Une LAMBDA qui appelle la fonction : le LEVER la traverse aussi.
    {
        Bench b("STRING", std::string("VAR\n    L : LIST OF REAL;\nEND_VAR\nLIST_ADD(L, Mesure);\nTRY\n"
                                      "    Ok := LIST_ANY(L, LAMBDA (x : REAL) : BOOL => Verifier_Niveau(x));\n") + kCatchAll, withVerifier);
        std::string err;
        const std::string r = b.runScript(&err);
        check(r.rfind("E_Niveau|", 0) == 0 && r.find("niveau hors plage : 120.0 %") != std::string::npos && journalOf(b.rt, "Erreur").empty(),
              "C2 : une LAMBDA qui appelle Verifier_Niveau : E_Niveau au CATCH, sans ERROR [" + r + "] " + err + journalOf(b.rt, "Erreur"));
    }
    // Une fonction de vue, appelee par un script de la vue sous TRY.
    {
        Project p;
        addVariable(p, "Mesure", "REAL", "120.0");
        addVariable(p, "R", "STRING", "");
        View v = makeView(p, "Vue_Cuve");
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Verifier_Vue";
        f.returnType = "BOOL";
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = "n";
        d.type = "REAL";
        f.decls.push_back(d);
        f.body = "IF n > 100.0 THEN\n    LEVER('E_Vue', 'hors plage {n:0.0}');\nEND_IF;\nVerifier_Vue := TRUE;\n";
        v.functions.push_back(f);
        Script sc;
        sc.id = p.allocate();
        sc.name = "OnOpen";
        sc.event = "OnOpen";
        sc.body = "TRY\n    IF Verifier_Vue(Mesure) THEN R := 'ok'; END_IF;\nCATCH e\n    R := CONCAT(e.Code, '|', DINT_TO_STRING(e.Ligne), '|', e.Message);\nEND_TRY";
        v.scripts.push_back(sc);
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        sim::Value out;
        const std::string r = rt.environment().read("R", out) ? out.asString() : std::string("#");
        check(r == "E_Vue|2|hors plage 120.0" && journalOf(rt, "Erreur").empty(),
              "C2 : une fonction de vue : E_Vue au CATCH de l'OnOpen, sans ERROR [" + r + "]" + journalOf(rt, "Erreur"));
    }

    // ---- C3 : WAIT_UNTIL lue comme BOOL (IF, WHILE, une affectation) dans un script DETACHE : aucun avertissement,
    // ni de Compiler (Saisie, l'arbre) ni de Generer ; non detache : le seul avertissement juste (coche Detache) ----
    {
        Project p;
        addVariable(p, "D_T", "INT", "0");
        addVariable(p, "A", "INT", "0");
        Script sc;
        sc.id = p.allocate();
        sc.name = "Sequence_Remplissage";
        sc.event = "Appel";
        sc.detached = true;
        sc.body = "IF WAIT_UNTIL(D_T > 0, T#2s) THEN\n    A := 1;\nEND_IF;\n"
                  "WHILE NOT WAIT_UNTIL(D_T > 5, T#1s) DO\n    A := A + 1;\nEND_WHILE;\n"
                  "A := BOOL_TO_INT(WAIT_UNTIL(D_T > 0, T#1s));\nWAIT_UNTIL(D_T > 0, T#1s);";
        p.programs.scripts.push_back(sc);
        const auto said = [](const std::vector<Issue>& is) {
            std::string out;
            for (const auto& i : is)
                if (i.message.find("WAIT_UNTIL") != std::string::npos) out += " | " + i.message;
            return out;
        };
        const NameExists none = [](std::string_view) { return false; };
        const std::string compiler = said(scriptcheck::projectIssues(p, none, {}, nullptr));
        const std::string generer = said(generate(p, none));
        check(compiler.empty(), "C3 : detache, IF/WHILE/affectation WAIT_UNTIL : Compiler n'avertit pas" + compiler);
        check(generer.empty(), "C3 : detache : Generer non plus (les deux disent la meme chose)" + generer);
        scriptcheck::Scope scope;
        scope.project = &p;
        scope.plcKnown = none;
        std::string saisie;
        for (const auto& f : scriptcheck::check(scope, sc.body))
            if (f.message.find("WAIT_UNTIL") != std::string::npos) saisie += " | " + f.message;
        check(saisie.empty(), "C3 : la Saisie (l'editeur) ne souligne plus IF WAIT_UNTIL" + saisie);
        // Non detache : l'avertissement juste (coche Detache), pas « pas dans une expression ».
        p.programs.scripts.back().detached = false;
        const std::string notDetached = said(scriptcheck::projectIssues(p, none, {}, nullptr));
        check(notDetached.find("coche D") != std::string::npos && notDetached.find("pas dans une expression") == std::string::npos,
              "C3 : non detache : 'coche Detache', pas 'pas dans une expression'" + notDetached);
        const std::string generer2 = said(generate(p, none));
        check(generer2 == notDetached, "C3 : non detache : Generer dit la meme chose que Compiler" + generer2 + " / " + notDetached);
    }
}

int main() {
    std::printf("1.13.0 : lever ses erreurs (LEVER, RELEVER, CATCH e, codes E_*) et WAIT_UNTIL\n");
    capturesF1();
    lever();
    codesMoteur();
    waitUntil();
    controle();
    automateInchange();
    relecture();
    seconde();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
