// =============================================================================
//  tests/donnees_test.cpp - 1.13.0 (lot J3) : JSON et expressions regulieres
// -----------------------------------------------------------------------------
//  Le lot DONNEES DANS LES SCRIPTS, en local (aucun reseau) : JSON_LIRE /
//  JSON_ECRIRE et les acces par cle / indice, les accesseurs surs JSON_NOMBRE /
//  TEXTE / BOOL / ENTIER, JSON_TYPE / A / CLES, le type ANY (une valeur JSON /
//  dynamique), et les natives REGEX_* (grammaire ECMAScript). Les erreurs JSON
//  et regex sont rattrapables par TRY ... CATCH (E_JSON, E_REGEX). Tout cela
//  n'est QUE du dialecte IHM : le ST de l'automate ne le connait pas.
//
//  DONNEES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/sim/Interpreter.hpp"     // le ST de l'automate (sans le dialecte)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("DONNEES_OBS"); return d && *d == '1'; }();

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

// Un petit projet d'essai : une variable de resultat R, une vue, un script appele (non detache).
struct Bench {
    Project p;
    Runtime rt;
    explicit Bench(const std::string& resultType, const std::string& body) {
        Variable v;
        v.id = p.allocate();
        v.name = "R";
        v.type = resultType.empty() ? "STRING" : resultType;
        v.initial = "";
        p.programs.variables.push_back(v);
        View view = makeView(p, "Vue_Essai");
        p.views.push_back(view);
        p.config.startView = p.views.back().id;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
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

std::string run(const std::string& type, const std::string& body, std::string* err = nullptr) {
    Bench b(type, body);
    return b.runScript(err);
}

// ---- JSON : lire un objet, un tableau, l'imbrication, l'acces --------------
void jsonLire() {
    std::printf("-- JSON_LIRE : objet, tableau, imbrication, acces ['cle'] / [i]\n");

    check(run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"gaz\":\"Helium\",\"pression\":7.5}');\n"
                        "R := JSON_TEXTE(o['gaz']);") == "Helium",
          "JSON_LIRE objet : o['gaz'] -> JSON_TEXTE = 'Helium'");
    check(run("DINT", "VAR o : MAP[STRING] OF ANY; END_VAR\no := JSON_LIRE('{\"n\":42}');\n"
                      "R := JSON_ENTIER(o['n']);") == "42",
          "JSON_LIRE objet -> MAP[STRING] OF ANY : o['n'] = 42");
    check(run("DINT", "VAR t : LIST OF ANY; END_VAR\nt := JSON_LIRE('[10,20,30]');\n"
                      "R := JSON_ENTIER(t[1]);") == "20",
          "JSON_LIRE tableau -> LIST OF ANY : t[1] = 20");
    check(run("DINT", "VAR t : LIST OF ANY; END_VAR\nt := JSON_LIRE('[10,20,30]');\n"
                      "R := t.Count;") == "3",
          "JSON_LIRE tableau : t.Count = 3");
    check(run("DINT", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":{\"b\":[1,2,3]}}');\n"
                      "R := JSON_ENTIER(o['a']['b'][2]);") == "3",
          "JSON imbrique : o['a']['b'][2] = 3");

    // Les accents : une valeur UTF-8 lue telle quelle.
    check(run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"gaz\":\"H\xC3\xA9lium\"}');\n"
                        "R := JSON_TEXTE(o['gaz']);") == "H\xC3\xA9lium",
          "JSON : une valeur accentuee (UTF-8) lue telle quelle");

    // Une cle absente -> JSON_Absent (pas d'erreur) ; un accesseur avec defaut la remplace.
    check(run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":1}');\n"
                        "R := JSON_TEXTE(o['manque'], 'defaut');") == "defaut",
          "cle absente : JSON_TEXTE(o['manque'], 'defaut') = 'defaut' (pas d'erreur)");
}

// ---- les accesseurs surs : avec et sans defaut -----------------------------
void jsonAccesseurs() {
    std::printf("-- JSON_NOMBRE / ENTIER / TEXTE / BOOL : avec et sans defaut\n");
    const std::string obj = "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"p\":7.5,\"n\":12,\"s\":\"ok\",\"b\":true}');\n";

    check(run("REAL", obj + "R := JSON_NOMBRE(o['p']);") == "7.5", "JSON_NOMBRE(o['p']) = 7.5");
    check(run("DINT", obj + "R := JSON_ENTIER(o['n']);") == "12", "JSON_ENTIER(o['n']) = 12");
    check(run("STRING", obj + "R := JSON_TEXTE(o['s']);") == "ok", "JSON_TEXTE(o['s']) = 'ok'");
    check(run("BOOL", obj + "R := JSON_BOOL(o['b']);") == "TRUE", "JSON_BOOL(o['b']) = TRUE");

    // Avec defaut : rendu si le genre ne correspond pas (un texte demande en nombre).
    check(run("REAL", obj + "R := JSON_NOMBRE(o['s'], 9.5);") == "9.5",
          "JSON_NOMBRE(o['s'], 9.5) : un texte -> le defaut 9.5");
    // Sans defaut : une erreur claire et rattrapable (E_JSON).
    std::string err;
    check(run("STRING", obj + "TRY\n    R := JSON_TEXTE(o['n']);\nCATCH e\n    R := e.Code;\nEND_TRY", &err) == "E_JSON",
          "JSON_TEXTE(o['n']) sans defaut (un nombre) : leve E_JSON, rattrapable");
    check(run("STRING", obj + "TRY\n    R := JSON_TEXTE(o['n']);\nCATCH e\n    R := e.Message;\nEND_TRY").find("JSON") != std::string::npos,
          "e.Message cite la valeur JSON");
}

// ---- JSON_TYPE, JSON_A, JSON_CLES, JSON_EST_NUL ----------------------------
void jsonInterroge() {
    std::printf("-- JSON_TYPE (chaque genre), JSON_A, JSON_CLES, JSON_EST_NUL\n");
    const auto genre = [](const char* json) {
        return run("DINT", std::string("VAR o : ANY; END_VAR\no := JSON_LIRE('") + json + "');\nR := JSON_TYPE(o);");
    };
    check(genre("{\"a\":1}") == "0", "JSON_TYPE objet = JSON_Objet (0)");
    check(genre("[1,2]") == "1", "JSON_TYPE tableau = JSON_Liste (1)");
    check(genre("\"x\"") == "2", "JSON_TYPE texte = JSON_Texte (2)");
    check(genre("12") == "3", "JSON_TYPE nombre = JSON_Nombre (3)");
    check(genre("true") == "4", "JSON_TYPE bool = JSON_Bool (4)");
    check(genre("null") == "5", "JSON_TYPE null = JSON_Nul (5)");
    // Une cle absente : JSON_Absent (6).
    check(run("DINT", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":1}');\nR := JSON_TYPE(o['manque']);") == "6",
          "JSON_TYPE d'une cle absente = JSON_Absent (6)");
    // Le nom nu JSON_Objet compare au rang.
    check(run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":1}');\n"
                        "IF JSON_TYPE(o) = JSON_Objet THEN\n    R := 'objet';\nEND_IF;") == "objet",
          "JSON_TYPE(o) = JSON_Objet (nom nu) : vrai pour un objet");

    check(run("BOOL", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"version\":1}');\nR := JSON_A(o, 'version');") == "TRUE",
          "JSON_A(o, 'version') = TRUE");
    check(run("BOOL", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"version\":1}');\nR := JSON_A(o, 'absente');") == "FALSE",
          "JSON_A(o, 'absente') = FALSE");
    check(run("BOOL", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":null}');\nR := JSON_EST_NUL(o['a']);") == "TRUE",
          "JSON_EST_NUL(o['a']) = TRUE (un null)");

    check(run("STRING", "VAR o : ANY; Cles : LIST OF STRING; END_VAR\no := JSON_LIRE('{\"gaz\":1,\"pression\":2}');\n"
                        "Cles := JSON_CLES(o);\nR := JOIN(Cles, ',');") == "gaz,pression",
          "JSON_CLES(o) = ['gaz','pression'] (rangees)");
}

// ---- JSON_ECRIRE : objet, indente, aller-retour, structure -----------------
void jsonEcrire() {
    std::printf("-- JSON_ECRIRE : compact, indente, structure, aller-retour\n");

    check(run("STRING", "VAR o : MAP[STRING] OF ANY; END_VAR\no['gaz'] := 'A';\no['n'] := 3;\n"
                        "R := JSON_ECRIRE(o);") == "{\"gaz\":\"A\",\"n\":3}",
          "JSON_ECRIRE objet compact");
    check(run("STRING", "VAR o : MAP[STRING] OF ANY; END_VAR\no['n'] := 1;\n"
                        "R := JSON_ECRIRE(o, TRUE);") == "{\n  \"n\": 1\n}",
          "JSON_ECRIRE objet indente (TRUE)");
    // Une liste dans un objet (construite avec un litteral).
    check(run("STRING", "VAR o : MAP[STRING] OF ANY; END_VAR\no['t'] := [1, 2, 3];\n"
                        "R := JSON_ECRIRE(o);") == "{\"t\":[1,2,3]}",
          "JSON_ECRIRE : une liste litterale dans un objet");
    // Une structure IHM (un litteral structure) : ses membres.
    check(run("STRING", "VAR o : ANY; END_VAR\no := (nom := 'A', niveau := 3);\n"
                        "R := JSON_ECRIRE(o);") == "{\"nom\":\"A\",\"niveau\":3}",
          "JSON_ECRIRE : une structure -> un objet JSON (ses membres)");
    // Aller-retour : LIRE(ECRIRE(x)) garde la valeur.
    check(run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":1,\"b\":[2,3]}');\n"
                        "R := JSON_ECRIRE(JSON_LIRE(JSON_ECRIRE(o)));") == "{\"a\":1,\"b\":[2,3]}",
          "aller-retour : JSON_ECRIRE(JSON_LIRE(JSON_ECRIRE(o))) = l'original");
}

// ---- JSON mal forme -> E_JSON rattrapable ----------------------------------
void jsonErreur() {
    std::printf("-- JSON_LIRE mal forme : leve E_JSON, rattrapable par TRY ... CATCH\n");
    std::string err;
    (void)run("STRING", "VAR o : ANY; END_VAR\no := JSON_LIRE('{\"a\":}');", &err);
    // Hors TRY, une erreur du moteur s'ecrit a la Console avec son message (le code E_JSON se lit dans e.Code au CATCH).
    check(err.find("JSON") != std::string::npos && err.find("position") != std::string::npos,
          "JSON mal forme hors TRY : arrete le script, le message cite la position (" + err + ")");
    check(run("STRING", "VAR o : ANY; END_VAR\nTRY\n    o := JSON_LIRE('{oups');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_JSON",
          "CATCH e : e.Code = 'E_JSON' sur un texte mal forme");
    check(run("STRING", "VAR o : ANY; END_VAR\nTRY\n    o := JSON_LIRE('[1,2');\nCATCH e\n    R := e.Message;\nEND_TRY").find("position") != std::string::npos,
          "e.Message cite la position de la faute");
    // Un repli propre apres l'erreur (comme l'exemple de la conception).
    check(run("STRING", "VAR o : ANY; END_VAR\nTRY\n    o := JSON_LIRE('abc');\nCATCH e\n    o := JSON_LIRE('{}');\nEND_TRY\n"
                        "R := JSON_ECRIRE(o);") == "{}",
          "apres une erreur, on repart propre (o := JSON_LIRE('{}'))");
}

// ---- REGEX : tester, trouver (groupe), remplacer, decouper -----------------
void regex() {
    std::printf("-- REGEX_TESTE / TROUVE(groupe) / REMPLACE / DECOUPE, ^ $\n");

    check(run("BOOL", "R := REGEX_TESTE('PRES=7.5bar', 'PRES=[0-9.]+bar');") == "TRUE",
          "REGEX_TESTE : trouve le motif quelque part");
    check(run("STRING", "R := REGEX_TROUVE('PRES=7.5bar;DEBIT=12.3', 'PRES=([0-9.]+)bar', 1);") == "7.5",
          "REGEX_TROUVE groupe 1 = '7.5'");
    check(run("STRING", "R := REGEX_TROUVE('PRES=7.5bar', 'PRES=([0-9.]+)bar');") == "PRES=7.5bar",
          "REGEX_TROUVE groupe 0 = la correspondance entiere");
    check(run("STRING", "R := REGEX_TROUVE('rien ici', '[0-9]+');") == "",
          "REGEX_TROUVE : '' si rien");
    check(run("STRING", "R := REGEX_REMPLACE('a b-c', '[^A-Za-z0-9]', '_');") == "a_b_c",
          "REGEX_REMPLACE : remplace toutes les occurrences");
    check(run("STRING", "R := REGEX_REMPLACE('2026-10-25', '([0-9]+)-([0-9]+)-([0-9]+)', '$3/$2/$1');") == "25/10/2026",
          "REGEX_REMPLACE : $1 $2 $3 rappellent les groupes");
    check(run("STRING", "VAR c : LIST OF STRING; END_VAR\nc := REGEX_DECOUPE('a;b;c', ';');\nR := JOIN(c, '|');") == "a|b|c",
          "REGEX_DECOUPE : coupe au motif");
    check(run("STRING", "VAR c : LIST OF STRING; END_VAR\nc := REGEX_TROUVE_TOUT('a1b22c333', '[0-9]+');\nR := JOIN(c, '|');") == "1|22|333",
          "REGEX_TROUVE_TOUT : toutes les correspondances");

    // ^ et $ : exiger tout le texte. En ST, $ est le caractere d'echappement : on ecrit $$ pour l'ancre $.
    check(run("BOOL", "R := REGEX_TESTE('1234567890123', '^[0-9]{13}$$');") == "TRUE",
          "^[0-9]{13}$ : un EAN-13 complet (ancre $ ecrite $$ en ST)");
    check(run("BOOL", "R := REGEX_TESTE('123', '^[0-9]{13}$$');") == "FALSE",
          "^[0-9]{13}$ : refuse un texte trop court");
    check(run("BOOL", "R := REGEX_VALIDE('[0-9]+');") == "TRUE", "REGEX_VALIDE : un motif correct");
    check(run("BOOL", "R := REGEX_VALIDE('[0-9');") == "FALSE", "REGEX_VALIDE : un motif invalide");
    // Un groupe non capturant (?:...) reste valide (pas un quantificateur apres la parenthese).
    check(run("BOOL", "R := REGEX_TESTE('abcabc', '(?:abc)+');") == "TRUE", "(?:...) : un groupe non capturant est accepte");
    check(run("BOOL", "R := REGEX_VALIDE('(?:ab)+');") == "TRUE", "REGEX_VALIDE : (?:...) est valide");
}

// ---- un motif invalide -> E_REGEX rattrapable (SANS planter) ---------------
void regexErreur() {
    std::printf("-- un motif invalide : leve E_REGEX, rattrapable (aucun plantage)\n");
    check(run("STRING", "TRY\n    R := REGEX_TROUVE('x', '[0-9');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_REGEX",
          "CATCH e : e.Code = 'E_REGEX' sur une classe '[' non fermee");
    check(run("STRING", "TRY\n    IF REGEX_TESTE('x', '(abc') THEN R := 'oui'; END_IF;\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_REGEX",
          "CATCH e : E_REGEX sur une parenthese non fermee");
    std::string err;
    (void)run("BOOL", "R := REGEX_TESTE('x', '*oops');", &err);
    // Hors TRY : le message a la Console (le code E_REGEX se lit dans e.Code au CATCH), et aucun plantage.
    check(err.find("motif invalide") != std::string::npos,
          "motif invalide hors TRY : le message a la Console, pas de plantage (" + err + ")");
}

// ---- le ST de l'automate ne connait ni JSON_ ni REGEX_ ni ANY --------------
struct MapEnv : sim::Environment {
    std::map<std::string, sim::Value, std::less<>> vars;
    std::vector<sim::Diagnostic> diags;
    static std::string up(std::string_view s) { std::string o(s); for (auto& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return o; }
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = vars.find(up(n));
        if (it == vars.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override { vars[up(n)] = v; return true; }
    bool exists(std::string_view n) override { return vars.count(up(n)) > 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic d) override { diags.push_back(std::move(d)); }
};

bool automateReports(const char* src) {
    auto program = sim::parse(src, "essai", sim::ParseOptions{false});   // hmiDialect = false : le ST de l'automate
    if (!program) return true;      // ne compile meme pas : traite comme un refus
    MapEnv env;
    env.vars["X"] = sim::Value::integer(sim::Type::DInt, 0);
    (void)sim::execute(**program, env);
    return !env.diags.empty();      // une erreur a ete signalee (fonction inconnue)
}

void automateInchange() {
    std::printf("-- le ST de l'automate (hmiDialect=false) : JSON_ / REGEX_ ne sont pas des natives\n");
    // Dans le dialecte, ces appels marchent (couverts plus haut). Dans l'automate, ils ne sont pas reconnus :
    // l'appel d'une fonction inconnue arrete le cycle et signale une erreur.
    check(automateReports("X := JSON_LIRE('{}');"), "automate : JSON_LIRE n'est pas une native (fonction inconnue)");
    check(automateReports("X := REGEX_TESTE('a', 'a');"), "automate : REGEX_TESTE n'est pas une native (fonction inconnue)");
    // Une affectation ST ordinaire passe des deux cotes (le ST de l'automate n'a rien perdu).
    const auto parses = [](const char* src, bool dialect) { return static_cast<bool>(sim::parse(src, "essai", sim::ParseOptions{dialect})); };
    check(parses("x := 1 + 2;", true) && parses("x := 1 + 2;", false), "une affectation ST ordinaire : lue par le dialecte ET l'automate");
}

// ---- la relecture de la 1.13.0 (groupe B, les donnees) ------------------------
//  Chaque essai suit le scenario d'un defaut confirme : il echouait avant la correction.
// Le moteur seul (le dialecte), un texte T donne : R lu apres.
std::string engine(const std::string& src, const std::string& text, std::string* diags = nullptr) {
    auto program = sim::parse(src, "essai", sim::ParseOptions{true});
    if (!program) return "illisible : " + program.error().message();
    MapEnv env;
    env.vars["T"] = sim::Value::text(text);
    env.vars["R"] = sim::Value::text(std::string{});
    (void)sim::execute(**program, env);
    if (diags) {
        diags->clear();
        for (const auto& d : env.diags) *diags += d.message;
    }
    sim::Value r;
    return env.read("R", r) ? shown(r) : std::string("#");
}

void relecture() {
    std::printf("-- relecture 1.13.0 : donnees (cle JSON absente, ANY, regex longues, REGEX_VALIDE)\n");
    std::string err;

    // n26 : une cle absente dans une MAP[STRING] OF ANY (l'objet JSON range, ou lu directement) : JSON_Absent.
    check(run("REAL", "VAR Params : MAP[STRING] OF ANY; END_VAR\nParams := JSON_LIRE('{\"a\":1}');\nR := JSON_NOMBRE(Params['seuil'], 80.0);", &err) == "80",
          "n26 : MAP[STRING] OF ANY, cle absente : JSON_NOMBRE(..., 80.0) = 80, pas E_CLE (" + err + ")");
    check(run("REAL", "R := JSON_NOMBRE(JSON_LIRE('{\"a\":1}')['manque'], 80.0);", &err) == "80",
          "n26 : JSON_LIRE(t)['manque'] : le defaut sert (" + err + ")");
    check(run("STRING", "VAR m : MAP[STRING] OF REAL; END_VAR\nTRY\n    R := REAL_TO_STRING(m['absente']);\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_CLE",
          "n26 : une MAP ordinaire (MAP[STRING] OF REAL) garde E_CLE sur une cle absente");

    // n30 : une case ANY affectee a elle-meme garde sa valeur (x := x, L[w] := L[r] avec w = r).
    check(run("REAL", "VAR x : ANY; END_VAR\nx := JSON_LIRE('{\"a\":5}');\nx := x;\nR := JSON_NOMBRE(x['a'], -1.0);", &err) == "5",
          "n30 : x := x (x : ANY) garde la valeur (" + err + ")");
    check(run("REAL", "VAR L : LIST OF ANY; w : DINT; END_VAR\nL := JSON_LIRE('[1,2,3]');\nw := 0;\nL[w] := L[0];\nR := JSON_NOMBRE(L[0], -1.0);", &err) == "1",
          "n30 : L[w] := L[r] avec w = r (LIST OF ANY) garde la valeur (" + err + ")");
    check(run("DINT", "VAR m : MAP[STRING] OF REAL; END_VAR\nm['a'] := 1.0;\nm := m;\nR := m.Count;", &err) == "1",
          "n30 : m := m (une MAP) garde ses entrees (" + err + ")");

    // n29 : un long texte ne fait plus deborder la pile ; un motif exponentiel ne fige plus ; au-dela de 1 Mo : E_REGEX.
    {
        const std::string big = "\"m\":[" + std::string(300000, 'x') + "]";
        std::string d;
        const std::string r = engine("R := REGEX_TROUVE(T, '\"m\":\\[(.*)\\]', 1);", big, &d);
        check(r.size() == 300000 && r.find_first_not_of('x') == std::string::npos,
              "n29 : REGEX_TROUVE sur 300 Ko (.*) : pas de debordement de pile [" + std::to_string(r.size()) + d.substr(0, 80) + "]");
        const auto t0 = std::chrono::steady_clock::now();
        const std::string e = engine("IF REGEX_TESTE(T, '^(\\w+\\s?)*$$') THEN R := 'oui'; ELSE R := 'non'; END_IF;", std::string(28, 'a') + "!", &d);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        check(e == "non" && ms < 2000, "n29 : (\\w+\\s?)*$ sur 28 lettres et '!' : rendu en " + std::to_string(ms) + " ms [" + e + d + "]");
        (void)engine("R := REGEX_TROUVE(T, 'x');", std::string((1u << 20) + 10, 'x'), &d);
        check(d.find("texte trop long") != std::string::npos, "n29 : un texte de plus de 1 Mo : E_REGEX, texte trop long (" + d.substr(0, 80) + ")");
#ifdef __GLIBCXX__
        check(run("STRING", "TRY\n    R := REGEX_TROUVE('abab', '(ab)\\1');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_REGEX",
              "n29 : une reference arriere (\\1) : E_REGEX (le mode sans retour arriere ne la prend pas)");
#endif
    }

    // n46 : REGEX_VALIDE dit FALSE pour un motif que REGEX_TROUVE refuserait.
    check(run("STRING", "IF REGEX_VALIDE('(?<=PRES=)[0-9.]+') THEN R := 'oui'; ELSE R := 'non'; END_IF;") == "non",
          "n46 : REGEX_VALIDE('(?<=PRES=)...') (lookbehind) = FALSE");
    check(run("STRING", "IF REGEX_VALIDE('a{3,1}') THEN R := 'oui'; ELSE R := 'non'; END_IF;") == "non",
          "n46 : REGEX_VALIDE('a{3,1}') = FALSE");
    check(run("STRING", "IF REGEX_VALIDE('PRES=([0-9.]+)bar') THEN R := 'oui'; ELSE R := 'non'; END_IF;") == "oui",
          "n46 : REGEX_VALIDE d'un motif correct = TRUE");

    // n52 : beaucoup de motifs differents (le cache est borne, puis vide) : les resultats restent justes.
    check(run("DINT", "VAR i : DINT; END_VAR\nR := 0;\nFOR i := 0 TO 199 DO\n"
                      "    IF REGEX_TESTE(CONCAT('ID=', DINT_TO_STRING(i), ';'), CONCAT('^ID=', DINT_TO_STRING(i), ';')) THEN R := R + 1; END_IF;\nEND_FOR;\n"
                      "IF REGEX_TESTE('ID=0;', '^ID=0;') THEN R := R + 1; END_IF;", &err) == "201",
          "n52 : 200 motifs differents puis le premier : 201 correspondances (le cache borne se vide sans rien casser) " + err);
}

// La seconde relecture (lot D2) : R22 - le sens des motifs est celui que l'aide decrit, le meme partout.
void seconde() {
    std::printf("-- seconde relecture : le sens des motifs (R22)\n");
    std::string err;
#ifdef __GLIBCXX__
    // L'exe livre (MinGW, libstdc++) : la recherche sans retour arriere - entre deux choix au meme depart, le plus long.
    check(run("STRING", "R := REGEX_TROUVE('Automatique', 'Auto|Automatique');", &err) == "Automatique",
          "R22 : deux choix au meme depart : le plus long l'emporte (Auto|Automatique -> Automatique), comme l'aide le dit (" + err + ")");
    check(run("STRING", "R := REGEX_TROUVE('Pompe12', 'Pompe(1|12)', 1);", &err) == "12", "R22 : Pompe(1|12) sur Pompe12 : le groupe 12 (le plus long)");
#else
    // D2b-4 : une construction Visual Studio (MSVC) suit la regle ECMAScript - le premier choix qui convient : l'aide le dit.
    check(run("STRING", "R := REGEX_TROUVE('Automatique', 'Auto|Automatique');", &err) == "Auto",
          "R22 : hors MinGW (MSVC), le premier choix qui convient l'emporte (Auto|Automatique -> Auto), comme l'aide le dit (" + err + ")");
#endif
    // D2b-4 : ce qui ne change pas selon la cible - un quantificateur paresseux prend le plus court, l'ordre ab|a garde ab.
    check(run("STRING", "R := REGEX_TROUVE('<a><b>', '<.+?>');", &err) == "<a>", "D2b-4 : <.+?> sur <a><b> : <a> partout (" + err + ")");
    check(run("STRING", "R := REGEX_TROUVE('x=12;y=3', '[a-z]=[0-9]+?');", &err) == "x=1", "D2b-4 : [0-9]+? : le plus court partout (" + err + ")");
    check(run("STRING", "R := REGEX_TROUVE('Automatique', 'Automatique|Auto');", &err) == "Automatique",
          "D2b-4 : le choix le plus long ecrit d'abord : le meme resultat partout (" + err + ")");
    check(run("STRING", "TRY\n    R := REGEX_TROUVE('aa', '(a)\\1');\nCATCH e\n    R := e.Code;\nEND_TRY") == "E_REGEX",
          "R22 : une reference arriere (\\1) est refusee (E_REGEX) - sur toutes les cibles");
    check(run("BOOL", "R := REGEX_VALIDE('(a)\\1');", &err) == "FALSE", "R22 : REGEX_VALIDE('(a)\\1') = FALSE (" + err + ")");
}

} // namespace

int main() {
    std::printf("=== donnees_test : JSON, expressions regulieres, le type ANY (1.13.0) ===\n");
    jsonLire();
    jsonAccesseurs();
    jsonInterroge();
    jsonEcrire();
    jsonErreur();
    regex();
    regexErreur();
    automateInchange();
    relecture();
    seconde();
    std::printf("=== %d verifications, %d echec(s) ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
