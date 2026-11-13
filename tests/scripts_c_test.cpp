// =============================================================================
//  tests/scripts_c_test.cpp - 1.50.0 : les scripts C et C++ compiles et executes
// -----------------------------------------------------------------------------
//  Un script C ou C++ (general, OnOpen, OnCycle...) est traduit ligne pour ligne
//  dans le ST de l'IHM puis execute par le moteur des scripts (HmiScriptC.hpp).
//  Ces essais : la traduction (autant de lignes que le texte C), Compiler (F7 :
//  checkScript, l'etat de compilation, une faute a sa ligne), l'execution (un
//  script general, un OnCycle de vue, des fonctions et main(), les variables
//  globales gardees), le sous-ensemble (boucles, switch, tableaux, pointeurs,
//  references, printf, std::...), et les refus dits a leur ligne.
//
//  SCRC_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiBuildState.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptC.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("SCRC_OBS"); return d && *d == '1'; }();

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

Variable& addVar(Project& p, const std::string& n, const std::string& t, const std::string& init = "") {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}

std::string errorsOf(const std::vector<ScriptDiagnostic>& d, int* line = nullptr) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == ScriptDiagnostic::Severity::Error) {
            if (line && out.empty()) *line = x.line;
            out += (out.empty() ? "" : " | ") + std::to_string(x.line) + ": " + x.message;
        }
    return out;
}

std::size_t lineCount(const std::string& s) { return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')) + 1; }

// Un script general "Essai" (C ou C++) appele `times` fois ; la valeur de `readVar` ensuite ("#" : illisible).
struct Run {
    std::string value, error, st;
    bool        journal{false};
};
Run run(Project p, ScriptLang lang, const std::string& body, const std::string& readVar = "R", int times = 1,
        const std::string& journalText = {}) {
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.lang = lang;
    s.event = "Appel";
    s.body = body;
    p.programs.scripts.push_back(s);
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    Run r;
    for (int k = 0; k < times; ++k) {
        std::string e;
        if (!rt.callScript("Essai", 0.2 + k * 0.1, &e) && r.error.empty()) r.error = e.empty() ? std::string("(echec sans raison)") : e;
    }
    sim::Value out;
    r.value = rt.environment().read(readVar, out) ? shown(out) : std::string("#");
    if (!journalText.empty())
        for (const auto& e : rt.journal())
            if (e.message.find(journalText) != std::string::npos) r.journal = true;
    r.st = cscript::translate(body, lang).st;
    return r;
}

std::string obs(const Run& r) { return r.value + (r.error.empty() ? std::string{} : " / " + r.error) + "\n" + r.st; }

// ---- la traduction : autant de lignes, la faute a sa ligne -------------------------------------
void traduction() {
    std::printf("-- la traduction : ligne pour ligne\n");
    const std::string c = "/* un script C */\n#include <math.h>\nint i;\nfor (i = 0; i < 3; i++) {\n    Compteur += i;\n}\n\nNiveau = sqrt(16.0);\n";
    const auto t = cscript::translate(c, ScriptLang::C);
    check(t.ok() && lineCount(t.st) == lineCount(c), "C : traduit, autant de lignes que le texte (" + std::to_string(lineCount(t.st)) + ")\n" + t.st);
    check(t.st.find("WHILE") != std::string::npos && t.st.find("SQRT(16.0)") != std::string::npos, "for -> WHILE, sqrt -> SQRT\n" + t.st);
    // une faute de syntaxe a sa ligne : x = ; a la ligne 3
    for (const ScriptLang lang : {ScriptLang::C, ScriptLang::Cpp}) {
        const std::string name = lang == ScriptLang::C ? "C" : "C++";
        const auto bad = cscript::translate("int a = 1;\na = a + 1;\nCompteur = ;\na = 2;\n", lang);
        check(!bad.ok() && bad.errors.front().line == 3, name + " : 'Compteur = ;' : faute ligne 3 ("
                                                             + (bad.ok() ? std::string("aucune") : std::to_string(bad.errors.front().line) + " " + bad.errors.front().message) + ")");
        const auto missing = cscript::translate("int a = 1;\nif (a > 0) {\n    a = 2;\n\na = 3;\n", lang);
        check(!missing.ok(), name + " : une accolade jamais fermee : refuse (" + (missing.ok() ? std::string("?") : missing.errors.front().message) + ")");
        const auto semi = cscript::translate("int a = 1\na = 2;\n", lang);
        check(!semi.ok() && semi.errors.front().line <= 2 && semi.errors.front().message.find("';'") != std::string::npos,
              name + " : un ; oublie dit (" + (semi.ok() ? std::string("?") : std::to_string(semi.errors.front().line) + " " + semi.errors.front().message) + ")");
    }
    // Compiler (F7) : checkScript, l'etat de compilation
    {
        int line = 0;
        const auto d = checkScript(ScriptLang::C, "int a = 0;\na = 1;\nwhile (a < 3 {\n  a++;\n}\n", "Essai");
        const std::string e = errorsOf(d, &line);
        check(!e.empty() && line == 3, "Compiler : while (a < 3 { -> faute ligne 3 (" + e + ")");
        check(!d.empty() && d.back().severity == ScriptDiagnostic::Severity::Info && d.back().message.find("compil") != std::string::npos,
              "Compiler : une note dit compile pour le moteur (" + (d.empty() ? std::string("?") : d.back().message) + ")");
        const auto ok = checkScript(ScriptLang::Cpp, "int a = 0;\nfor (int k = 0; k < 4; ++k) a += k;\n", "Essai");
        check(errorsOf(ok).empty(), "Compiler : un C++ juste, aucune faute (" + errorsOf(ok) + ")");
        Script s;
        s.id = 7;
        s.name = "Calcul";
        s.lang = ScriptLang::C;
        s.body = "int a = 0;\na = a + 1;\n";
        const auto st = build::ofScript(s, {});
        check(st.compile == build::Compile::Ok && st.generate == build::Generate::Yes, "l'etat : C sans faute -> compile, genere (" + st.reason + ")");
        s.body = "int a = 0;\n\na = a +;\n";
        const auto st2 = build::ofScript(s, {});
        check(st2.compile == build::Compile::Error && st2.firstLine == 3, "l'etat : C en faute -> erreur ligne 3 (" + st2.reason + ")");
        // une faute que le moteur trouve (un type inconnu) : a la ligne de la declaration, pas en tete
        int l2 = 0;
        const auto unknown = checkScript(ScriptLang::C, "int a = 0;\na = 1;\nT_Inconnu x;\n", "Essai", [](std::string_view) { return false; });
        const std::string eu = errorsOf(unknown, &l2);
        check(!eu.empty() && l2 == 3, "un type inconnu (T_Inconnu x;) : dit a sa ligne 3 (" + eu + ")");
    }
}

// ---- l'execution : un script general, un OnCycle ----------------------------------------------
void execution() {
    std::printf("-- l'execution : une variable IHM changee\n");
    {
        Project p;
        addVar(p, "R", "DINT", "5");
        addVar(p, "Etat", "BOOL", "FALSE");
        const Run r = run(p, ScriptLang::C, "/* script C */\nR = R + 1;\nif (R > 5) {\n    Etat = true;\n}\n", "R");
        check(r.value == "6" && r.error.empty(), "C : R = R + 1 -> 6 : " + obs(r));
        const Run e = run(p, ScriptLang::C, "R = R + 1;\nif (R > 5) Etat = 1;\n", "Etat");
        check(e.value == "TRUE", "C : Etat = 1 (un BOOL) -> TRUE : " + obs(e));
    }
    {
        Project p;
        addVar(p, "R", "REAL", "0");
        const Run r = run(p, ScriptLang::Cpp,
                          "// script C++\n#include <cmath>\nusing namespace std;\nauto n = 4;\nREAL total = 0.0f;\nfor (int k = 1; k <= n; ++k) {\n"
                          "    total += static_cast<float>(k);\n}\nR = std::sqrt(total * 10.0);\n",
                          "R");
        check(r.value == "10" && r.error.empty(), "C++ : auto, REAL, for, static_cast, std::sqrt -> 10 : " + obs(r));
    }
    // un OnCycle de vue en C : il tourne a chaque cycle et lit / ecrit les variables
    {
        Project p;
        addVar(p, "Cycles", "DINT", "0");
        addVar(p, "Double", "DINT", "0");
        View v = makeView(p, "Vue_A");
        Script sc;
        sc.id = p.allocate();
        sc.name = "A_cycle";
        sc.lang = ScriptLang::C;
        sc.event = "OnCycle";
        sc.body = "Cycles++;\nDouble = Cycles * 2;\n";
        v.scripts.push_back(sc);
        Script cpp;
        cpp.id = p.allocate();
        cpp.name = "A_ouverture";
        cpp.lang = ScriptLang::Cpp;
        cpp.event = "OnOpen";
        cpp.body = "void OnOpen() {\n    Cycles = 100;\n}\n";
        v.scripts.push_back(cpp);
        p.views = {v};
        p.config.startView = v.id;
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        for (int i = 0; i <= 10; ++i) rt.tick(i * 0.1);
        const auto* c = rt.variable("Cycles");
        const auto* d = rt.variable("Double");
        const long long n = c ? c->asInteger() : -1;
        check(n > 100 && n <= 111, "OnOpen (C++, sa fonction OnOpen() appelee) puis OnCycle (C) a chaque cycle : Cycles = " + std::to_string(n));
        check(d && d->asInteger() == n * 2, "OnCycle : Double = Cycles * 2 (" + std::string(d ? shown(*d) : "#") + ")");
    }
    // une erreur d'execution : a la ligne du texte C
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        const Run r = run(p, ScriptLang::C, "int z = 0;\nR = 1;\nR = 10 / z;\n", "R");
        check(r.error.find("ligne 3") != std::string::npos && r.error.find("z\xC3\xA9ro") != std::string::npos, "C : division par zero, ligne 3 : " + obs(r));
        const Run u = run(p, ScriptLang::C, "R = 1;\nInconnue = 2;\n", "R");
        check(u.error.find("ligne 2") != std::string::npos, "C : une variable inconnue ecrite, ligne 2 : " + obs(u));
    }
}

// ---- le sous-ensemble ----------------------------------------------------------------------------
void sousEnsemble() {
    std::printf("-- le sous-ensemble du C et du C++\n");
    struct Case {
        ScriptLang  lang;
        const char* type;
        const char* body;
        const char* want;
        const char* what;
    };
    const ScriptLang C = ScriptLang::C, Cpp = ScriptLang::Cpp;
    const Case cases[] = {
        {C, "DINT", "int s = 0, i = 0;\nwhile (i < 5) { s += i; i++; }\nR = s;", "10", "while"},
        {C, "DINT", "int s = 0, i = 0;\ndo { s += 2; i++; } while (i < 3);\nR = s;", "6", "do ... while"},
        {C, "DINT", "int s = 0;\nfor (int i = 0; i < 10; i++) {\n  if (i % 2) continue;\n  if (i > 6) break;\n  s += i;\n}\nR = s;", "12",
         "for avec continue (le pas fait) et break"},
        {C, "DINT", "int m = 2;\nswitch (m) {\n  case 1: R = 10; break;\n  case 2:\n  case 3: R = 20; break;\n  default: R = 30;\n}\n", "20",
         "switch : deux case, default"},
        {C, "DINT", "int m = 9;\nswitch (m) {\n  case 1: R = 10; break;\n  default: R = 30; break;\n}\n", "30", "switch : default"},
        {C, "DINT", "int t[5] = {1, 2, 3, 4, 5};\nint s = 0;\nfor (int i = 0; i < 5; i++) s += t[i];\nR = s;", "15", "un tableau local et sa liste"},
        {C, "DINT", "#define N 3\nint m[N][2];\nfor (int i = 0; i < N; i++) { m[i][0] = i; m[i][1] = i * 10; }\nR = m[2][1] + m[1][0];", "21",
         "#define, un tableau de tableaux local m[i][j]"},
        {C, "DINT", "void ajoute(int *p, int n) {\n  *p = *p + n;\n}\nint x = 1;\najoute(&x, 4);\nR = x;", "5", "un parametre pointeur (&x, *p)"},
        {Cpp, "DINT", "void ajoute(int& r, int n) { r += n; }\nint x = 1;\najoute(x, 6);\nR = x;", "7", "C++ : une reference"},
        {C, "DINT", "int carre(int v) { return v * v; }\nR = carre(4) + carre(2);", "20", "une fonction du script"},
        {C, "DINT", "int a = 3, b = 4;\nint *p = &a;\n*p = *p + 10;\np = &b;\n*p += 1;\nR = a * 100 + b;", "1305",
         "un pointeur local (POINTER TO) : *p lit et ecrit, p = &b"},
        {C, "DINT", "int *p = NULL;\nif (p == NULL) R = 1;\nint x = 7;\np = &x;\nif (p != NULL) R = R + *p;", "8", "NULL, == et != sur un pointeur"},
        {C, "DINT", "void plus(int *q) { *q += 5; }\nint x = 1;\nint *p = &x;\nplus(p);\nplus(&x);\nR = x;", "11",
         "un pointeur local passe a un parametre pointeur"},
        {C, "DINT", "R = (5 > 3 ? 1 : 2) + (7 & 3) + (1 << 4) + (~0 & 1) + 17 % 5;", "23", "?: & << ~ %"},
        {C, "DINT", "int a = 3, b = 0;\nif (a && !b) R = 1; else R = 2;", "1", "&& ! sur des entiers"},
        {C, "DINT", "R = (int)2.7 + (int)(-2.7);", "0", "(int) tronque vers zero"},
        {C, "LREAL", "R = (double)7 / 2;", "3.5", "(double) puis division"},
        {C, "DINT", "R = 7 / 2;", "3", "division entiere"},
        {C, "DINT", "R = 0x1F + 010 + 'A';", "104", "hexadecimal, octal, caractere"},
        {C, "DINT", "int a = 5;\na <<= 2;\na |= 1;\na ^= 3;\na %= 7;\nR = a;", "1", "<<= |= ^= %= (5 << 2 = 20, | 1 = 21, ^ 3 = 22, % 7 = 1)"},
        {C, "DINT", "int a, b;\na = b = 4;\nR = a + b;", "8", "a = b = 4"},
        {C, "STRING", "const char* nom = \"Pompe\";\nR = nom;", "Pompe", "un texte const char*"},
        {Cpp, "STRING", "std::string s = \"Cuve \";\ns = s + std::to_string(3);\nR = s;", "Cuve 3", "C++ : std::string, std::to_string"},
        {Cpp, "DINT", "int t[4] = {1, 2, 3, 4};\nint s = 0;\nfor (int v : t) s += v;\nR = s;", "10", "C++ : for (int v : t)"},
        {Cpp, "DINT", "R = std::max(3, 9) - std::min(4, 2) + std::abs(-1);", "8", "C++ : std::max, std::min, std::abs"},
        {C, "DINT", "static int n = 0;\nn++;\nR = n;", "3", "static : gardee d'une execution a l'autre (3 appels)"},
        {C, "DINT", "int compteur = 10;\nvoid main(void) {\n  compteur++;\n  R = compteur;\n}\n", "13",
         "un fichier C : main() appelee, sa globale gardee (3 appels)"},
        {C, "DINT", "int n = 0;\nn++;\nR = n;", "1", "une variable du script repart a chaque execution (3 appels)"},
        {C, "DINT", "R = 0;\n{\n  int i = 5;\n  R += i;\n}\n{\n  int i = 7;\n  R += i;\n}\n", "12", "deux blocs, deux i"},
        {C, "DINT", "int i = 1;\n{\n  int i = 2;\n  R = i;\n}\nR = R * 10 + i;", "21", "un i qui en cache un autre"},
        {C, "DINT", "int end = 4;\nint mod = 3;\nR = end * mod;", "12", "des noms reserves en ST (end, mod)"},
        {Cpp, "DINT", "int x = 0;\nx++;\n++x;\nx--;\nR = x;", "1", "++ et -- sur leur ligne"},
        {C, "BOOL", "R = !(3 < 2) && (1 || 0);", "TRUE", "un BOOL calcule"},
    };
    for (const auto& c : cases) {
        Project p;
        addVar(p, "R", c.type, "");
        const int times = std::string(c.what).find("3 appels") != std::string::npos ? 3 : 1;
        const Run r = run(p, c.lang, c.body, "R", times);
        check(r.value == c.want && r.error.empty(), std::string(c.lang == C ? "C" : "C++") + " : " + c.what + " -> " + c.want + " : " + obs(r));
    }
    // un pointeur NULL dereference : l'erreur du moteur, a sa ligne C
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        const Run r = run(p, ScriptLang::C, "int *p;\nR = 1;\nR = *p;\n", "R");
        check(r.error.find("ligne 3") != std::string::npos && r.error.find("nul") != std::string::npos, "C : *p sur un pointeur NULL, dit ligne 3 : " + obs(r));
    }
    // printf : au journal
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        const Run r = run(p, ScriptLang::C, "int n = 42;\nprintf(\"Niveau %d atteint\\n\", n);\nR = 1;\n", "R", 1, "Niveau 42 atteint");
        check(r.journal && r.error.empty(), "printf -> le journal (Niveau 42 atteint) : " + obs(r));
    }
    // les variables IHM : un tableau a deux dimensions lu m[i][j], une structure
    {
        Project p;
        addVar(p, "M", "ARRAY[0..2, 0..3] OF DINT", "");
        addVar(p, "T", "ARRAY[0..2] OF ARRAY[0..3] OF DINT", "");
        addVar(p, "R", "DINT", "0");
        const Run r = run(p, ScriptLang::C, "M[1][2] = 7;\nT[2][3] = 5;\nR = M[1][2] * 10 + T[2][3];\n", "R");
        check(r.value == "75" && r.error.empty(), "C : M[1][2] (deux dimensions) et T[2][3] (tableau de tableaux) -> 75 : " + obs(r));
    }
    // une enumeration du projet : T_MODE::Auto (C++), Auto (C)
    {
        Project p;
        HmiType mode;
        mode.id = p.allocate();
        mode.name = "T_MODE";
        mode.kind = HmiTypeKind::Enumeration;
        mode.values = {HmiEnumValue{"Arret", 0, "Arr\xC3\xAAt", {}}, HmiEnumValue{"Auto", 1, "Auto", {}}};
        p.programs.types.push_back(mode);
        addVar(p, "Mode", "T_MODE", "");
        addVar(p, "R", "DINT", "0");
        const Run cpp = run(p, ScriptLang::Cpp, "Mode = T_MODE::Auto;\nif (Mode == T_MODE::Auto) R = 1;\n", "R");
        check(cpp.value == "1" && cpp.error.empty(), "C++ : T_MODE::Auto : " + obs(cpp));
        const Run c = run(p, ScriptLang::C, "Mode = Auto;\nswitch (Mode) { case Auto: R = 2; break; case Arret: R = 3; break; }\n", "R");
        check(c.value == "2" && c.error.empty(), "C : Auto, case Auto : " + obs(c));
    }
}

// ---- les refus : a leur ligne, en francais ---------------------------------------------------------
void refus() {
    std::printf("-- ce qui n'est pas pris en charge : dit a sa ligne\n");
    struct Case {
        ScriptLang  lang;
        const char* body;
        int         line;
        const char* word;
    };
    // 1.50.1 : l'arithmetique des pointeurs (dans un tableau), les classes, les lambdas, malloc, ++ au milieu, std::vector,
    // std::cout, un case qui continue, static dans une fonction sont pris (scripts_c_1501.inl) ; restent refuses :
    const Case cases[] = {
        {ScriptLang::C, "int a = 1;\ngoto fin;\n", 2, "goto"},
        {ScriptLang::Cpp, "#include <map>\nstd::map<int, int> m;\n", 2, "std::map"},
        {ScriptLang::Cpp, "int a = 0;\nauto f = [&](int x) { return x; };\n", 2, "[&]"},
        {ScriptLang::C, "int a = 0;\na = 1;\nint *t = malloc(10);\nt = realloc(t, 20);\n", 4, "realloc"},
        {ScriptLang::C, "int a = 0;\nif (a == 1) {\n  a = 2;\n}\nfin:\na = 3;\n", 5, "goto"},
    };
    for (const auto& c : cases) {
        const auto t = cscript::translate(c.body, c.lang);
        const std::string what = t.ok() ? std::string("accepte\n") + t.st : std::to_string(t.errors.front().line) + " : " + t.errors.front().message;
        check(!t.ok() && t.errors.front().line == c.line && t.errors.front().message.find(c.word) != std::string::npos,
              std::string("refus ligne ") + std::to_string(c.line) + " (" + c.word + ") : " + what);
    }
    // les textes a l'ecran disent ce qui est pris
    check(cscript::supportedSubset(ScriptLang::Cpp).find("pointeurs") != std::string::npos
              && cscript::supportedSubset(ScriptLang::Cpp).find("Pas de goto") != std::string::npos
              && cscript::supportedSubset(ScriptLang::Cpp).find("std::vector") != std::string::npos,
          "le sous-ensemble dit (C++) : " + cscript::supportedSubset(ScriptLang::Cpp));
    Project p;
    addVar(p, "M", "ARRAY[0..3] OF ARRAY[0..15] OF BOOL", "");
    const std::string hc = projectHeader(p, ScriptLang::C);
    check(hc.find("compil") != std::string::npos && hc.find("non ex\xC3\xA9" "cut\xC3\xA9") == std::string::npos,
          "l'en-tete du projet dit compile et execute");
    check(hc.find("bool M[4][16];") != std::string::npos, "l'en-tete C : un tableau de tableaux, bool M[4][16]\n" + hc);
    // la frappe : chaque debut d'un script ne fait jamais planter la traduction
    const std::string all = "#define N 4\nint t[N][2] = {{1, 2}, {3, 4}};\nvoid f(int *p) { *p += 1; }\nint main(void) {\n  for (int i = 0; i < N; i++) {\n"
                            "    switch (i) { case 0: f(&t[i][0]); break; default: break; }\n  }\n  printf(\"%d\\n\", t[0][0]);\n  return 0;\n}\n";
    std::size_t prefixes = 0;
    for (std::size_t n = 0; n <= all.size(); ++n) {
        (void)cscript::translate(all.substr(0, n), ScriptLang::Cpp);
        (void)checkScript(ScriptLang::C, all.substr(0, n), "Essai");
        ++prefixes;
    }
    check(prefixes == all.size() + 1, "la frappe : " + std::to_string(prefixes) + " debuts traduits sans exception");
    const auto whole = cscript::translate(all, ScriptLang::C);
    check(whole.ok() && whole.entry == "main", "un fichier C complet : traduit, main() en point d'entree (" + (whole.ok() ? whole.entry : whole.errors.front().message) + ")");
}

#include "scripts_c_1501.inl"   // 1.50.1 : struct, enum, pointeurs dans un tableau, classes, new / delete, lambdas, le debogueur

} // namespace

// SCRC_RUN=fichier.c|.cpp|.st : traduit (C, C++) et execute le fichier, ecrit la traduction, R (DINT), les erreurs et le
// journal - un banc d'essai a la main.
int runFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 2;
    std::string body;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) body.append(buf, n);
    std::fclose(f);
    const std::string p(path);
    const ScriptLang lang = p.size() > 4 && p.substr(p.size() - 4) == ".cpp" ? ScriptLang::Cpp : p.size() > 2 && p.substr(p.size() - 2) == ".c" ? ScriptLang::C : ScriptLang::ST;
    Project pr;
    const char* rtype = std::getenv("SCRC_RTYPE");
    addVar(pr, "R", rtype ? rtype : "DINT", "");
    Script s;
    s.id = pr.allocate();
    s.name = "Essai";
    s.lang = lang;
    s.event = "Appel";
    s.body = body;
    pr.programs.scripts.push_back(s);
    if (lang != ScriptLang::ST) {
        const auto t = cscript::translate(body, lang);
        std::printf("---- ST ----\n%s\n", t.st.c_str());
        for (const auto& e : t.errors) std::printf("faute ligne %d : %s\n", e.line, e.message.c_str());
    }
    for (const auto& d : checkScript(lang, body, "Essai"))
        std::printf("Compiler : %d ligne %d : %s\n", static_cast<int>(d.severity), d.line, d.message.c_str());
    View v = makeView(pr, "Vue_Essai");
    pr.views.push_back(v);
    pr.config.startView = pr.views.back().id;
    for (const auto& is : compile(pr))
        if (is.severity != Issue::Severity::Info) std::printf("compile : %s ligne %d : %s\n", is.category.c_str(), is.line, is.message.c_str());
    Runtime rt;
    rt.bind(&pr, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    sim::Value out;
    std::printf("---- R = %s  (%s) %s\n", rt.environment().read("R", out) ? shown(out).c_str() : "#", ok ? "ok" : "ECHEC", e.c_str());
    for (const auto& j : rt.journal()) std::printf("journal %s : %s\n", j.kind.c_str(), j.message.c_str());
    return ok ? 0 : 1;
}

int main() {
    if (const char* f = std::getenv("SCRC_RUN"); f && *f) return runFile(f);
    std::printf("scripts C et C++ : compiles et executes\n");
    traduction();
    execution();
    sousEnsemble();
    refus();
    c1501::all();
    std::printf("%d controles, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
