// =============================================================================
//  tests/scripts_c_1501.inl - 1.50.1 : le C et le C++ elargis, le debogueur
// -----------------------------------------------------------------------------
//  Inclus par scripts_c_test.cpp (ses aides : check, run, addVar, obs). Chaque
//  construction : traduite, executee, ses valeurs ; ce qui reste refuse : dit a
//  sa ligne ; le debogueur : un point d'arret a la ligne C, le pas a pas, les
//  variables sous leur nom C (structures, tableaux, pointeurs, this).
// =============================================================================

namespace c1501 {

struct Case {
    ScriptLang  lang;
    const char* type;
    const char* body;
    const char* want;
    const char* what;
};

void cases(const char* title, const std::vector<Case>& list) {
    std::printf("-- %s\n", title);
    for (const auto& c : list) {
        Project p;
        addVar(p, "R", c.type, "");
        const int times = std::string(c.what).find("3 appels") != std::string::npos ? 3 : 1;
        const Run r = run(p, c.lang, c.body, "R", times);
        check(r.value == c.want && r.error.empty(), std::string(c.lang == ScriptLang::C ? "C" : "C++") + " : " + c.what + " -> " + c.want + " : " + obs(r));
    }
}

const ScriptLang C = ScriptLang::C, Cpp = ScriptLang::Cpp;

// ---- struct, enum, typedef, const, sizeof ----------------------------------------------------------
void structures() {
    cases("struct (imbriquees, tableaux, . et ->), enum, typedef, const, sizeof", {
        {C, "DINT",
         "struct Point { int x; int y; };\nstruct Rect { struct Point a, b; int tags[3]; };\n"
         "int aire(const struct Rect* r) { return (r->b.x - r->a.x) * (r->b.y - r->a.y); }\n"
         "struct Rect r = { {1, 2}, {4, 6}, {7, 8, 9} };\nR = aire(&r) * 100 + r.tags[2];\n",
         "1209", "struct imbriquee, un tableau membre, -> par un pointeur"},
        {C, "DINT",
         "struct P { int x, y; };\nstruct P milieu(struct P a, struct P b) {\n  struct P m;\n  m.x = (a.x + b.x) / 2;\n  m.y = (a.y + b.y) / 2;\n"
         "  return m;\n}\nstruct P a = {0, 0}, b = {10, 20};\nstruct P c = milieu(a, b);\nR = c.x * 100 + c.y;\n",
         "520", "struct par valeur, rendue par une fonction"},
        {C, "DINT", "struct P { int x, y; };\nstruct P a = {1, 2};\nstruct P b;\nb = a;\nb.x = 9;\nR = a.x * 100 + b.x * 10 + b.y;\n", "192",
         "struct copiee (b = a), b changee seule"},
        {C, "DINT",
         "struct P { int x, y; };\nstruct P p = {.y = 5, .x = 2};\nint t[6] = {[4] = 29, [1] = 3};\nR = p.x * 1000 + p.y * 100 + t[4] + t[1] + t[5];\n",
         "2532", "initialiseurs designes (.y = 5, [4] = 29), le reste a zero"},
        {C, "DINT",
         "struct P { int x, y; };\nstruct L { struct P pos; int n; };\nstruct L l = { .pos.x = 3, .n = 4 };\nstruct L m[2] = { {{1, 2}, 3}, {{4, 5}, 6} };\n"
         "int g[2][2] = {1, 2, 3, 4};\nR = l.pos.x * 100 + l.n * 10 + m[1].pos.y + g[1][0] * 1000;\n",
         "3345", ".pos.x = 3, une liste de structures, les accolades omises"},
        {C, "REAL",
         "typedef struct { char nom[16]; float niveau; } Cuve;\nCuve cuves[3] = {{\"A\", 1.5f}, {\"B\", 2.5f}, {\"C\", 3.0f}};\n"
         "Cuve *p = &cuves[1];\np->niveau += 1.0f;\nR = p->niveau * 10;\n",
         "35", "typedef struct, un tableau de structures, &cuves[1], ->"},
        {C, "DINT", "struct Noeud { int v; struct Noeud* suivant; };\nstruct Noeud a, b;\na.v = 1;\nb.v = 2;\na.suivant = &b;\nb.suivant = NULL;\n"
                    "int s = 0;\nfor (struct Noeud* n = &a; n != NULL; n = n->suivant) s += n->v;\nR = s;\n",
         "3", "une liste chainee (struct Noeud* suivant)"},
        {C, "DINT", "enum Etat { ARRET, MARCHE = 5, DEFAUT };\nenum Etat e = DEFAUT;\nR = e * 10 + ARRET;\nswitch (e) { case MARCHE: R = 1; break; case DEFAUT: R += 1; break; default: break; }\n",
         "61", "enum (valeurs, case)"},
        {Cpp, "DINT", "enum class Mode : int { Auto = 1, Manu };\nMode m = Mode::Manu;\nif (m == Mode::Manu) R = 2;\nR = R * 10 + static_cast<int>(Mode::Auto);\n",
         "21", "C++ : enum class, Mode::Manu"},
        {C, "DINT", "typedef int Entier;\ntypedef Entier Trois[3];\nTrois t = {1, 2, 3};\nEntier s = 0;\nfor (Entier i = 0; i < 3; i++) s += t[i];\nR = s;\n",
         "6", "typedef d'un entier, d'un tableau"},
        {Cpp, "LREAL", "using Reel = double;\nusing Vec = std::array<Reel, 3>;\nVec v = {1.5, 2.5, 3.0};\nReel s = 0;\nfor (Reel x : v) s += x;\nR = s + v.size();\n",
         "10", "C++ : using, std::array (size, for)"},
        {C, "DINT", "const int N = 4;\nint t[N];\nfor (int i = 0; i < N; i++) t[i] = i * i;\nR = t[N - 1];\n", "9", "const int N : la taille d'un tableau"},
        {Cpp, "DINT", "constexpr int N = 3;\nstd::array<int, N> a{};\na.fill(7);\nR = a[0] + a.back() + static_cast<int>(a.size());\n", "17",
         "C++ : constexpr, std::array fill, back"},
        {C, "DINT", "int t[10];\nR = sizeof(t) / sizeof(t[0]);\n", "10", "sizeof(t) / sizeof(t[0])"},
        {C, "DINT", "struct S { char c; int i; double d; };\nstruct P { int x, y; };\nR = sizeof(struct S) * 100 + sizeof(struct P) * 10 + sizeof(int*) / 8;\n",
         "1681", "sizeof d'une struct (alignee), d'un pointeur"},
        {C, "DINT", "short s;\nlong long q;\nchar texte[12];\nR = sizeof s * 1000 + sizeof(q) * 100 + sizeof texte;\n", "2812", "sizeof d'une expression"},
    });
}

// ---- switch, macros, operateurs ----------------------------------------------------------------------
void controle() {
    cases("switch qui continue, default n'importe ou ; les macros ; ++ au milieu", {
        {C, "DINT",
         "int n = 0;\nfor (int k = 0; k < 4; k++) {\n  switch (k) {\n    case 0: n += 1;\n    case 1: n += 10; break;\n    default: n += 100;\n"
         "    case 3: n += 1000;\n  }\n}\nR = n;\n",
         "2121", "un case qui continue, default avant un case"},
        {C, "DINT",
         "int n = 0;\nfor (int k = 0; k < 5; k++) {\n  switch (k % 3) {\n    case 0:\n      if (k > 2) break;\n      n += 1;\n      break;\n"
         "    case 1:\n      continue;\n    default:\n      n += 100;\n  }\n  n += 10;\n}\nR = n;\n",
         "131", "break dans un if du case, continue dans un switch"},
        {C, "DINT", "#define CARRE(x) ((x) * (x))\n#define MAX(a, b) ((a) > (b) ? (a) : (b))\n#define N 3\nR = CARRE(N + 1) + MAX(2, 7) * 100;\n", "716",
         "#define a parametres (CARRE(N + 1) : 16)"},
        {C, "DINT", "#define DEBUG 1\n#if DEBUG\nR = 1;\n#else\nR = 2;\n#endif\n#ifndef ABSENT\nR = R + 10;\n#endif\n#if 0\nceci n'est pas du C\n#endif\n", "11",
         "#if, #else, #ifndef, #if 0"},
        {C, "DINT", "int t[3];\nint i = 0;\nt[i++] = 5;\nt[i++] = 6;\nR = t[0] * 10 + t[1] + i * 100;\n", "256", "t[i++] = x"},
        {C, "DINT", "int x = 5;\nint y = x++ + 1;\nint z = ++x * 10;\nR = y * 1000 + x * 100 + z;\n", "6770", "x++ et ++x au milieu d'une expression"},
        {C, "DINT", "int n = 3, s = 0;\nwhile (n-- > 0) s += n;\nR = s * 10 + n;\n", "29", "while (n-- > 0)"},
        {C, "DINT", "int v;\nif ((v = 7) > 5) R = v;\n", "7", "une affectation dans une condition"},
        {C, "DINT", "unsigned int f = 0x0F;\nf |= 1 << 6;\nf &= ~0x03;\nf ^= 0x100;\nR = f >> 2;\n", "83", "les operations sur les bits"},
        {C, "DINT", "int a = 7;\na *= 3;\na -= 1;\na /= 4;\na %= 3;\nR = a > 1 ? a : -a;\n", "2", "*= -= /= %= et ?:"},
    });
}

// ---- pointeurs dans un tableau ----------------------------------------------------------------------
void pointeurs() {
    cases("l'arithmetique des pointeurs dans un tableau", {
        {C, "DINT", "int t[5] = {10, 20, 30, 40, 50};\nint *p = t;\nint *q = &t[4];\np++;\nR = *p + *(p + 2) + p[1] + (int)(q - p);\n", "93",
         "p++, *(p + 2), p[1], q - p"},
        {C, "DINT", "int t[5] = {10, 20, 30, 40, 50};\nint s = 0;\nfor (int *it = t; it < t + 5; it++) s += *it;\nR = s;\n", "150", "for (int *it = t; it < t + 5; it++)"},
        {C, "DINT",
         "int somme(const int *v, int n) {\n  int s = 0;\n  for (int i = 0; i < n; i++) s += v[i];\n  return s;\n}\nint a[3] = {1, 2, 3};\nint b[5] = {1, 1, 1, 1, 1};\n"
         "R = somme(a, 3) * 10 + somme(b, 5) + somme(&b[2], 3) * 100;\n",
         "365", "un parametre pointeur recoit des tableaux de tailles differentes"},
        {C, "DINT", "int t[4] = {1, 2, 3, 4};\nint *fin = t + 4;\nint *p = fin;\nwhile (p != t) {\n  p--;\n  *p *= 2;\n}\nR = t[0] + t[3] * 10;\n", "82",
         "le pointeur de fin (t + 4), p--, *p *= 2"},
        {C, "DINT", "void echange(int *a, int *b) {\n  int t = *a;\n  *a = *b;\n  *b = t;\n}\nint x = 1, y = 2;\nechange(&x, &y);\nR = x * 10 + y;\n", "21",
         "echange(&x, &y)"},
        {C, "DINT", "int m[2][3] = {{1, 2, 3}, {4, 5, 6}};\nint *ligne = m[1];\nR = ligne[2] + *(ligne + 1) * 10;\n", "56", "un pointeur sur une ligne d'un tableau de tableaux"},
    });
    // une case hors du tableau : une erreur, a sa ligne
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        const Run r = run(p, C, "int t[3] = {1, 2, 3};\nint *p = t;\np = p + 3;\nR = 1;\nR = *p;\n", "R");
        check(r.error.find("ligne 5") != std::string::npos && r.error.find("hors du tableau") != std::string::npos,
              "*p sur la case qui suit la derniere : erreur ligne 5 : " + obs(r));
        const Run s = run(p, C, "int t[3];\nint *p = t;\nR = 2;\np = p + 4;\n", "R");
        check(s.error.find("ligne 4") != std::string::npos && s.error.find("hors du tableau") != std::string::npos,
              "p + 4 sur un tableau de 3 : erreur ligne 4 : " + obs(s));
        const Run u = run(p, C, "int x = 1;\nint *p = &x;\nR = 3;\np++;\n", "R");
        check(u.error.find("ligne 4") != std::string::npos && u.error.find("tableau") != std::string::npos,
              "p++ sur un pointeur qui ne vise pas un tableau : erreur ligne 4 : " + obs(u));
    }
}

// ---- C++ : classes, heritage, new / delete, lambdas, conteneurs --------------------------------------
void classes() {
    cases("C++ : classes, heritage, methodes virtuelles", {
        {Cpp, "DINT",
         "class Compteur {\npublic:\n  Compteur(int depart) : valeur(depart) {}\n  void ajoute(int n) { valeur += n; }\n  int lire() const { return valeur; }\n"
         "private:\n  int valeur;\n};\nCompteur c(10);\nc.ajoute(5);\nR = c.lire();\n",
         "15", "une classe : constructeur, methodes, membre prive"},
        {Cpp, "DINT",
         "struct Pompe {\n  int numero = 0;\n  bool marche = false;\n  void demarrer() { this->marche = true; }\n  int etat() const;\n};\n"
         "int Pompe::etat() const { return marche ? numero : -1; }\nPompe p;\np.numero = 7;\np.demarrer();\nPompe q;\nR = p.etat() * 10 + q.etat();\n",
         "69", "valeurs par defaut, this->, une methode definie hors de la classe"},
        {Cpp, "LREAL",
         "class Forme {\npublic:\n  virtual double aire() const { return 0; }\n};\nclass Carre : public Forme {\npublic:\n  Carre(double c) : cote(c) {}\n"
         "  double aire() const override { return cote * cote; }\nprivate:\n  double cote;\n};\nclass Disque : public Forme {\npublic:\n  Disque(double r) : rayon(r) {}\n"
         "  double aire() const override { return 3.0 * rayon * rayon; }\nprivate:\n  double rayon;\n};\n"
         "Forme* formes[3];\nformes[0] = new Carre(2);\nformes[1] = new Disque(1);\nformes[2] = new Forme();\ndouble total = 0;\n"
         "for (int i = 0; i < 3; i++) total += formes[i]->aire();\nfor (int i = 0; i < 3; i++) delete formes[i];\nR = total;\n",
         "7", "heritage simple, methode virtuelle, new / delete"},
        {Cpp, "DINT",
         "class Base {\npublic:\n  Base(int v) : x(v) {}\n  virtual int f() { return x; }\n  int g() { return f() * 10; }\nprotected:\n  int x;\n};\n"
         "class Derivee : public Base {\npublic:\n  Derivee() : Base(3), y(4) {}\n  int f() override { return x + y + Base::f(); }\nprivate:\n  int y;\n};\n"
         "Derivee d;\nBase& b = d;\nR = b.g();\n",
         "100", "appel virtuel depuis la base, Base::f(), un membre protege"},
        {Cpp, "DINT",
         "class Pile {\n  int t[8];\n  int n = 0;\npublic:\n  void pousse(int v) { t[n++] = v; }\n  int tire() { return t[--n]; }\n  bool vide() const { return n == 0; }\n};\n"
         "Pile p;\np.pousse(1);\np.pousse(2);\np.pousse(3);\nint s = 0;\nwhile (!p.vide()) s = s * 10 + p.tire();\nR = s;\n",
         "321", "une classe pile (t[n++], t[--n])"},
    });
    cases("C++ : new / delete, lambdas, std::vector, std::string", {
        {Cpp, "DINT", "int* t = new int[5];\nfor (int i = 0; i < 5; i++) t[i] = i * 2;\nint s = 0;\nfor (int* p = t; p != t + 5; ++p) s += *p;\ndelete[] t;\nR = s;\n",
         "20", "new int[n], parcours par pointeur, delete[]"},
        {Cpp, "DINT", "struct N { int v; N* suivant; };\nN* tete = nullptr;\nfor (int i = 1; i <= 3; i++) {\n  N* n = new N{i, tete};\n  tete = n;\n}\n"
                      "int s = 0;\nwhile (tete) {\n  N* x = tete;\n  s = s * 10 + x->v;\n  tete = x->suivant;\n  delete x;\n}\nR = s;\n",
         "321", "une liste chainee sur le tas (new N{...}, delete)"},
        {Cpp, "DINT", "auto carre = [](int x) { return x * x; };\nR = carre(5);\n", "25", "une lambda sans capture"},
        {Cpp, "DINT", "int base = 100;\nauto plus = [=](int x) -> int {\n  return base + x;\n};\nR = plus(5);\n", "105", "une lambda [=] sur plusieurs lignes"},
        {Cpp, "DINT", "int applique(std::function<int(int)> f, int v) { return f(v); }\nR = applique([](int x) { return x + 1; }, 41);\n", "42",
         "une lambda passee a std::function"},
        {Cpp, "DINT", "std::vector<int> v;\nfor (int i = 1; i <= 4; i++) v.push_back(i * i);\nint s = 0;\nfor (int x : v) s += x;\nv.pop_back();\nR = s * 10 + (int)v.size();\n",
         "303", "std::vector : push_back, for, pop_back, size"},
        {Cpp, "STRING", "std::string s = \"Pompe\";\ns += \" 12\";\nstd::string n = s.substr(6, 2);\nR = n + \"/\" + std::to_string(s.length()) + (s.empty() ? \"!\" : \"\");\n",
         "12/8", "std::string : +=, substr, length, empty"},
        {Cpp, "DINT", "int f(int a) { return a; }\nint f(int a, int b) { return a * b; }\nR = f(3) + f(4, 5);\n", "23", "une surcharge (par le nombre des parametres)"},
        {C, "DINT", "#include <stdlib.h>\nint *t = (int*)malloc(4 * sizeof(int));\nfor (int i = 0; i < 4; i++) t[i] = i + 1;\nR = t[3] * 10 + t[0];\nfree(t);\n",
         "41", "C : malloc, free"},
    });
    // le tas : delete deux fois, un objet employe apres delete
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        const Run twice = run(p, Cpp, "int* a = new int(3);\nR = *a;\ndelete a;\ndelete a;\n", "R");
        check(twice.error.find("ligne 4") != std::string::npos && twice.error.find("deux fois") != std::string::npos, "delete deux fois : erreur ligne 4 : " + obs(twice));
        const Run after = run(p, Cpp, "struct P { int x; };\nP* q = new P{5};\ndelete q;\nR = 1;\nR = q->x;\n", "R");
        check(after.error.find("ligne 5") != std::string::npos && after.error.find("lib\xC3\xA9r\xC3\xA9") != std::string::npos,
              "un objet employe apres delete : erreur ligne 5 : " + obs(after));
        const Run local = run(p, Cpp, "int x = 1;\nint* q = &x;\nR = 1;\ndelete q;\n", "R");
        check(local.error.find("ligne 4") != std::string::npos && local.error.find("new") != std::string::npos,
              "delete d'une variable du script : erreur ligne 4 : " + obs(local));
        const Run cfree = run(p, C, "int *t = malloc(2 * sizeof(int));\nfree(t);\nR = 2;\nfree(t);\n", "R");
        check(cfree.error.find("ligne 4") != std::string::npos && cfree.error.find("deux fois") != std::string::npos, "free deux fois : erreur ligne 4 : " + obs(cfree));
    }
}

// ---- ce qui reste refuse ----------------------------------------------------------------------------
void refusEncore() {
    std::printf("-- ce qui reste refuse : dit a sa ligne\n");
    struct R {
        ScriptLang  lang;
        const char* body;
        int         line;
        const char* word;
    };
    const R list[] = {
        {C, "int a = 0;\ngoto fin;\n", 2, "goto"},
        {Cpp, "int a = 0;\ntemplate <typename T> T f(T x) { return x; }\n", 2, "template"},
        {Cpp, "int a = 0;\ntry {\n  a = 1;\n} catch (...) {}\n", 2, "exceptions"},
        {Cpp, "int a = 0;\nthrow 3;\n", 2, "exceptions"},
        {Cpp, "int a = 0;\nauto f = [&](int x) { return x + a; };\n", 2, "[&]"},
        {C, "int a = 0;\nunion U { int i; float f; };\n", 2, "union"},
        {C, "int *t = malloc(4);\nt = realloc(t, 8);\n", 2, "realloc"},
        {Cpp, "class A {};\nclass B {};\nclass C : public A, public B {};\n", 3, "multiple"},
        {Cpp, "struct V { int x; };\nV operator+(V a, V b) { return a; }\n", 2, "surcharge des op"},
        {C, "int a = 1;\nconst int k = 2;\nk = 3;\n", 3, "const"},
        {Cpp, "class P {\n  int secret;\n};\nP p;\np.secret = 1;\n", 5, "priv"},
        {Cpp, "class F {\npublic:\n  virtual int f() = 0;\n};\nF x;\n", 5, "abstraite"},
        {C, "int x = 1;\nint *p = &x;\nfloat *f = (float*)p;\n", 3, "conversion de pointeur"},
    };
    for (const auto& c : list) {
        const auto t = cscript::translate(c.body, c.lang);
        const std::string what = t.ok() ? std::string("accepte\n") + t.st : std::to_string(t.errors.front().line) + " : " + t.errors.front().message;
        check(!t.ok() && t.errors.front().line == c.line && t.errors.front().message.find(c.word) != std::string::npos,
              std::string("refus ligne ") + std::to_string(c.line) + " (" + c.word + ") : " + what);
    }
    const std::string sub = cscript::supportedSubset(ScriptLang::Cpp);
    check(sub.find("goto") != std::string::npos && sub.find("templates") != std::string::npos && sub.find("exceptions") != std::string::npos
              && sub.find("classes") != std::string::npos && sub.find("pas \xC3\xA0 pas") != std::string::npos,
          "le sous-ensemble dit ce qui est pris et ce qui reste refuse (C++) : " + sub);
}

// ---- Compiler (F7) : des struct, des classes, sans faute ------------------------------------------------
void compiler() {
    std::printf("-- Compiler : les struct et les classes du script sont des types connus\n");
    const std::string code = "struct P { int x; int y; };\nclass Q {\npublic:\n  int v = 1;\n  int get() const { return v; }\n};\nP p = {1, 2};\nQ q;\nint n = p.x + q.get();\n";
    const auto d = checkScript(ScriptLang::Cpp, code, "Essai", [](std::string_view) { return false; });
    check(errorsOf(d).empty(), "Compiler : struct et classe du script, aucune faute (" + errorsOf(d) + ")");
}

// ---- le debogueur ---------------------------------------------------------------------------------------
const sim::LineProbe::Local* local(const std::vector<sim::LineProbe::Local>& list, const std::string& name) {
    for (const auto& l : list)
        if (l.name == name) return &l;
    return nullptr;
}
std::string localsText(const std::vector<sim::LineProbe::Local>& list) {
    std::string out;
    for (const auto& l : list) {
        out += (out.empty() ? "" : ", ") + l.name + ":" + l.type + "=" + l.value;
        if (!l.children.empty()) {
            out += "{";
            for (const auto& c : l.children) out += c.name + "=" + c.value + " ";
            out += "}";
        }
    }
    return out;
}
std::string stackText(const DebugHit& h) {
    std::string out;
    for (const auto& f : h.stack) out += (out.empty() ? "" : " > ") + f.label + (f.line ? ":" + std::to_string(f.line) : std::string{});
    return out;
}
DebugBreakpoint point(std::uint32_t id, Id script, std::uint32_t line, const std::string& condition = {}) {
    DebugBreakpoint b;
    b.id = id;
    b.script = script;
    b.line = line;
    b.condition = condition;
    return b;
}

const char* kDebugC =
    "struct Point { int x; int y; };\n"                       // 1
    "int somme(const int *v, int n) {\n"                      // 2
    "    int s = 0;\n"                                         // 3
    "    for (int i = 0; i < n; i++)\n"                        // 4
    "        s += v[i];\n"                                     // 5
    "    return s;\n"                                          // 6
    "}\n"                                                      // 7
    "int main(void) {\n"                                       // 8
    "    int t[4] = {1, 2, 3, 4};\n"                           // 9
    "    struct Point pt = {7, 8};\n"                          // 10
    "    int *p = &t[1];\n"                                    // 11
    "    p++;\n"                                               // 12
    "    R = somme(t, 4) + pt.x + *p;\n"                       // 13
    "    return 0;\n"                                          // 14
    "}\n";                                                     // 15

struct DebugBench {
    Project p;
    Runtime rt;
    Id sc{kNoId};
    DebugBench(ScriptLang lang, const std::string& body) {
        addVar(p, "R", "DINT", "0");
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.lang = lang;
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
        sc = s.id;
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        rt.bind(&p, nullptr);
        rt.setDebugger(true);
        rt.start(0.0);
        rt.tick(0.1);
    }
};

void debogueur() {
    std::printf("-- le debogueur : un point d'arret a la ligne C, le pas a pas, les noms C\n");
    {
        DebugBench b(C, kDebugC);
        const auto lines = Runtime::breakableLines(b.p, b.sc, kNoId);
        const bool has13 = std::find(lines.begin(), lines.end(), 13u) != lines.end();
        const bool has5 = std::find(lines.begin(), lines.end(), 5u) != lines.end();
        check(has13 && has5, "les lignes ou un point se pose sont celles du C (5, 13 en sont)");
        b.rt.setBreakpoints({point(1, b.sc, 13)});
        check(b.rt.breakpoints()[0].effectiveLine == 13 && b.rt.breakpoints()[0].note.empty(), "le point de la ligne 13 est compris : " + b.rt.breakpoints()[0].note);
        std::string err;
        (void)b.rt.callScript("Essai", 0.2, &err);
        check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->line == 13, "en pause a la ligne C 13 : " + err);
        if (!b.rt.debugHit()) return;
        const auto& hit = *b.rt.debugHit();
        check(hit.stack.size() >= 3 && hit.stack.back().label.rfind("main", 0) == 0, "la pile : main en haut : " + stackText(hit));
        const auto& top = hit.stack.back().locals;
        const auto* t = local(top, "t");
        const auto* pt = local(top, "pt");
        const auto* p = local(top, "p");
        check(t && t->type == "int[4]" && t->children.size() == 4 && t->children[2].value == "3", "t : int[4], ses cases : " + localsText(top));
        check(pt && pt->type == "struct Point" && local(pt->children, "x") && local(pt->children, "x")->value == "7" && local(pt->children, "y"),
              "pt : struct Point {x = 7, y = 8} : " + localsText(top));
        check(p && p->type == "int*", "p : int* : " + localsText(top));
        bool hidden = true;
        for (const auto& l : top) hidden = hidden && !(l.name.size() > 1 && l.name.back() == '_');
        check(hidden, "aucun temporaire de la traduction n'est montre : " + localsText(top));
        std::string v, why;
        check(b.rt.debugEvaluate("pt.x + *p", v, &why) && v == "10", "un espion en C : pt.x + *p = 10 : " + v + why);
        check(b.rt.debugEvaluate("t[1] == 2 && p != NULL", v, &why) && v == "TRUE", "un espion en C : t[1] == 2 && p != NULL : " + v + why);
        check(b.rt.debugEvaluate("p[0]", v, &why) && v == "3", "un espion en C : p[0] (l'arithmetique) = 3 : " + v + why);
        // Entrer dans somme, Suivant, Sortir
        check(b.rt.debugStep(DebugStep::Into) && b.rt.paused() && b.rt.debugHit()->line == 3, "Entrer : somme, ligne 3 : "
              + (b.rt.debugHit() ? stackText(*b.rt.debugHit()) : std::string("?")));
        if (b.rt.debugHit()) {
            const auto& h2 = *b.rt.debugHit();
            check(h2.stack.back().label.rfind("somme", 0) == 0, "la pile : somme en haut : " + stackText(h2));
            const auto* n = local(h2.stack.back().locals, "n");
            const auto* vv = local(h2.stack.back().locals, "v");
            check(n && n->value == "4" && vv && vv->type == "const int*", "les parametres de somme : n = 4, v : const int* : " + localsText(h2.stack.back().locals));
        }
        check(b.rt.debugStep(DebugStep::Over) && b.rt.paused() && b.rt.debugHit()->line == 4, "Suivant : ligne 4");
        check(b.rt.debugStep(DebugStep::Out) && b.rt.paused() && b.rt.debugHit()->line == 13, "Sortir : de retour ligne 13 de main");
        check(b.rt.debugContinue() && !b.rt.paused(), "Continuer");
        sim::Value r;
        check(b.rt.environment().read("R", r) && r.asInteger() == 20, "le script va au bout : R = 10 + 7 + 3");
    }
    {   // une condition en C : i == 2
        DebugBench b(C, kDebugC);
        b.rt.setBreakpoints({point(1, b.sc, 5, "i == 2")});
        check(b.rt.breakpoints()[0].note.empty(), "la condition C i == 2 se lit : " + b.rt.breakpoints()[0].note);
        (void)b.rt.callScript("Essai", 0.2);
        check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->line == 5, "arret ligne 5 quand i == 2");
        std::string v, why;
        check(b.rt.debugEvaluate("v[i]", v, &why) && v == "3", "v[i] = 3 (i = 2) : " + v + why);
        (void)b.rt.debugContinue();
        check(!b.rt.paused() && b.rt.breakpoints()[0].hits == 1, "un seul passage (i == 2)");
    }
    {   // C++ : une methode, this, ses membres
        const char* code =
            "class Compteur {\n"                         // 1
            "public:\n"                                  // 2
            "    void ajoute(int n) {\n"                 // 3
            "        valeur += n;\n"                     // 4
            "    }\n"                                    // 5
            "    int valeur = 1;\n"                      // 6
            "};\n"                                       // 7
            "Compteur c;\n"                              // 8
            "c.ajoute(4);\n"                             // 9
            "R = c.valeur;\n";                           // 10
        DebugBench b(Cpp, code);
        b.rt.setBreakpoints({point(1, b.sc, 4)});
        (void)b.rt.callScript("Essai", 0.2);
        check(b.rt.paused() && b.rt.debugHit() && b.rt.debugHit()->line == 4, "C++ : en pause dans la methode, ligne 4");
        if (b.rt.debugHit()) {
            const auto& h = *b.rt.debugHit();
            check(h.stack.back().label.rfind("Compteur::ajoute", 0) == 0, "la pile : Compteur::ajoute : " + stackText(h));
            const auto* self = local(h.stack.back().locals, "this");
            check(self && local(self->children, "valeur") && local(self->children, "valeur")->value == "1", "this, son membre valeur = 1 : " + localsText(h.stack.back().locals));
            std::string v, why;
            check(b.rt.debugEvaluate("this->valeur + n", v, &why) && v == "5", "un espion : this->valeur + n = 5 : " + v + why);
        }
        (void)b.rt.debugContinue();
        sim::Value r;
        check(b.rt.environment().read("R", r) && r.asInteger() == 5, "R = 5");
    }
}

void all() {
    structures();
    controle();
    pointeurs();
    classes();
    refusEncore();
    compiler();
    debogueur();
}

} // namespace c1501
