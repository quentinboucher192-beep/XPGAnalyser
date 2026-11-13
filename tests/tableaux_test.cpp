// =============================================================================
//  tests/tableaux_test.cpp - 1.50.0 : les tableaux de tableaux et les tableaux
//  a deux dimensions, partout ou un type s'ecrit ou s'emploie
// -----------------------------------------------------------------------------
//  ARRAY[0..3] OF ARRAY[0..15] OF BOOL (un tableau de tableaux) et
//  ARRAY[0..3, 0..15] OF BOOL (deux dimensions) : les deux formes acceptees par
//  la meme lecture des types (hmi::types::parseSpec pour les variables IHM et
//  les membres des types IHM, le registre des types pour les declarations, le
//  moteur pour les locales), et lues a[i][j] comme a[i, j] - dans un script,
//  une formule (fx), une locale, un membre. Les bornes fausses : dites, a leur
//  niveau.
//
//  Avant 1.50.0 : une variable IHM ou un membre ARRAY OF ARRAY etait refuse
//  (<< ecrire ARRAY[0..3, 0..9] >>) alors qu'une locale l'acceptait ; M[i][j] sur
//  deux dimensions et T[i, j] sur un tableau de tableaux etaient refuses.
//
//  1.50.1 (tableaux_ndim.inl) : autant de dimensions qu'on veut par niveau (ARRAY[0..3, 0..7, 0..15] OF BOOL),
//  melangees aux tableaux de tableaux ; a[i] et a[i, j] sur trois dimensions : une couche, une ligne. Les grands
//  tableaux (10 000 000 de BOOL) ranges serres, mesures (temps, memoire) ; refuse seulement ce qui ne tient pas.
//
//  TAB_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiDecl.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPopupParams.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiTypeForms.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/sim/Rich.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("TAB_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

const char* const kNested = "ARRAY[0..3] OF ARRAY[0..15] OF BOOL";
const char* const kTwoD = "ARRAY[0..3, 0..15] OF BOOL";
const char* const kForms[] = {kNested, kTwoD};
std::string formName(const char* t) { return t == kNested ? "tableau de tableaux" : "deux dimensions"; }

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

std::string errorsOf(const std::vector<ScriptDiagnostic>& d) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == ScriptDiagnostic::Severity::Error) out += (out.empty() ? "" : " | ") + std::to_string(x.line) + ": " + x.message;
    return out;
}

std::string problemsOf(const std::vector<types::PathProblem>& v) {
    std::string out;
    for (const auto& q : v) out += (out.empty() ? "" : " | ") + q.message;
    return out;
}

std::string findingsOf(const std::vector<scriptcheck::Finding>& v) {
    std::string out;
    for (const auto& q : v)
        if (q.severity == scriptcheck::Finding::Severity::Error) out += (out.empty() ? "" : " | ") + q.message;
    return out;
}

// Le projet demarre avec un script "Essai" ; ce qu'il laisse dans `readVar` (ou "#") et son erreur.
struct Sim {
    Project p;
    Runtime rt;
    std::string error;
    explicit Sim(Project project, const std::string& body, std::vector<Declaration> decls = {}) : p(std::move(project)) {
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        s.decls = std::move(decls);
        p.programs.scripts.push_back(s);
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        if (!rt.callScript("Essai", 0.2, &error) && error.empty()) error = "(echec)";
    }
    std::string read(const std::string& name) {
        sim::Value out;
        return rt.environment().read(name, out) ? shown(out) : std::string("#");
    }
    std::string fx(const std::string& text) {
        const auto e = Expression::compile(text);
        if (!e.valid()) return "(illisible) " + e.error();
        const auto r = e.evaluate(rt.environment());
        return r ? shown(*r) : "(erreur) " + r.error().message();
    }
};

// ---- les types : la meme lecture pour les deux formes ---------------------------------------------
void lesTypes() {
    std::printf("-- les types : ARRAY OF ARRAY et ARRAY[a, b] acceptes partout\n");
    Project p;
    const auto reg = typereg::Registry::build(p);
    for (const char* t : kForms) {
        const std::string f = formName(t);
        std::string why;
        types::Spec s;
        check(types::parseSpec(t, s, &why) && s.array(), f + " : parseSpec le lit (" + why + ")");
        check(types::normalized(t) == t, f + " : remis en forme a l'identique (" + types::normalized(t) + ")");
        why.clear();
        check(types::validType(p, t, &why), f + " : une variable IHM peut l'avoir (" + why + ")");
        why.clear();
        check(types::validMemberType(p, t, &why), f + " : un membre d'un type IHM aussi (" + why + ")");
        for (unsigned use : {typereg::UseVariable, typereg::UseDeclaration, typereg::UseParameter, typereg::UseReturn}) {
            const auto r = reg->resolve(t, use);
            check(r.ok && r.text == t, f + " : le registre l'accepte pour l'usage " + std::to_string(use) + " (" + r.text + r.why + ")");
        }
        const auto sh = typeform::decompose(t);
        check(typeform::compose(sh) == t, f + " : le selecteur de types le decompose et le recompose (" + typeform::compose(sh) + ")");
        check(types::weightOf(p, t).words == 4, f + " : 64 BOOL ranges 16 par mot : 4 mots (" + std::to_string(types::weightOf(p, t).words) + ")");
        const auto fl = types::flatten(p, "X", t);
        check(fl.ok() && fl.leaves.size() == 64, f + " : deplie en 64 cases (" + std::to_string(fl.leaves.size()) + " " + fl.error + ")");
        // la place Modbus : la case [1][2] (ou [1,2]) est le bit 2 du mot 1, dans les deux formes
        bool placed = false;
        for (const auto& l : fl.leaves)
            if ((l.path == "X[1][2]" || l.path == "X[1,2]") && l.word == 1 && l.bit == 2) placed = true;
        check(placed, f + " : la case 1, 2 est le bit 2 du mot 1 (meme place Modbus)");
        check(!types::summary(p, t).empty(), f + " : resume " + types::summary(p, t));
    }
    // trois niveaux, et un tableau de structures dans un tableau
    {
        HmiType four;
        four.id = p.allocate();
        four.name = "T_Four";
        four.members = {TypeMember{"Temperature", "REAL"}, TypeMember{"Grille", kNested}};
        p.programs.types.push_back(four);
        std::string why;
        check(types::validType(p, "ARRAY[1..2] OF ARRAY[0..3] OF T_Four", &why), "un tableau de tableaux de structures (" + why + ")");
        check(types::validType(p, "ARRAY[0..1] OF ARRAY[0..1, 0..2] OF ARRAY[0..3] OF INT", &why), "trois niveaux, deux dimensions au milieu (" + why + ")");
        const auto fl = types::flatten(p, "F", "T_Four");
        check(fl.ok() && fl.leaves.size() == 65, "T_Four : son membre Grille (un tableau de tableaux) deplie : 1 + 64 cases ("
                                                    + std::to_string(fl.leaves.size()) + " " + fl.error + ")");
    }
    // la valeur d'une constante (un litteral) : une liste par ligne, verifiee par le moteur
    {
        std::string why;
        check(typeform::valueFits(p, "ARRAY[0..1] OF ARRAY[0..2] OF INT", "[[1, 2, 3], [4, 5, 6]]", &why),
              "un litteral [[1, 2, 3], [4, 5, 6]] va dans ARRAY[0..1] OF ARRAY[0..2] OF INT (" + why + ")");
        why.clear();
        check(!typeform::valueFits(p, "ARRAY[0..1] OF ARRAY[0..2] OF INT", "[[1, 2, 3, 4], [4, 5, 6]]", &why) && !why.empty(),
              "... une ligne de trop de valeurs : refusee (" + why + ")");
    }
    // les fautes : dites a leur niveau
    {
        struct Bad {
            const char* type;
            const char* word;
        };
        const Bad bad[] = {
            {"ARRAY[9..0] OF ARRAY[0..1] OF INT", "haute avant la basse"},
            {"ARRAY[0..3] OF ARRAY[9..0] OF INT", "ses cases"},
            {"ARRAY[0..3] OF ARRAY[0..x] OF INT", "deux entiers"},
            {"ARRAY[0..3] OF ARRAY[0..1] REAL", "OF"},
            // 1.50.1 : 1 000 x 1 000 INT et trois dimensions sont acceptes (tableaux_ndim.inl) ; seul ce qui ne tient pas en
            // memoire est refuse, avec l'estimation.
            {"ARRAY[0..99999] OF ARRAY[0..99999] OF INT", "trop grand"},
        };
        for (const auto& b : bad) {
            std::string why;
            types::Spec s;
            check(!types::parseSpec(b.type, s, &why) && why.find(b.word) != std::string::npos,
                  std::string("refuse : ") + b.type + " -> " + why);
        }
        const auto r = reg->resolve("ARRAY[0..3] OF ARRAY[5..1] OF BOOL", typereg::UseDeclaration);
        check(!r.ok && r.why.find("borne") != std::string::npos, "le registre : une borne fausse au second niveau (" + r.why + ")");
        // une locale : la faute du moteur, en francais, a sa ligne
        const auto d = checkScript(ScriptLang::ST, "VAR\n  a : ARRAY[0..3] OF ARRAY[5..1] OF BOOL;\nEND_VAR\na[0][5] := TRUE;\n", "S");
        check(errorsOf(d).rfind("2: a : tableau : la borne haute", 0) == 0, "une locale aux bornes fausses : refusee a sa ligne, en francais (" + errorsOf(d) + ")");
        Project p2;
        Sim s(p2, "VAR\n  a : ARRAY[0..3] OF ARRAY[5..1] OF BOOL;\nEND_VAR\na[0][5] := TRUE;\n");
        check(s.error.find("borne haute") != std::string::npos, "... et a l'execution (" + s.error + ")");
    }
}

// ---- les chemins : a[i][j] et a[i, j] -------------------------------------------------------------
void lesChemins() {
    std::printf("-- les chemins : X[1][2] et X[1, 2], les deux formes\n");
    for (const char* t : kForms) {
        const std::string f = formName(t);
        Project p;
        addVar(p, "X", t);
        addVar(p, "R", "BOOL", "FALSE");
        check(types::typeOfPath(p, "X[1][2]") == "BOOL" && types::typeOfPath(p, "X[1, 2]") == "BOOL",
              f + " : X[1][2] et X[1, 2] sont des BOOL (" + types::typeOfPath(p, "X[1][2]") + ", " + types::typeOfPath(p, "X[1, 2]") + ")");
        for (const char* code : {"X[1][2] := TRUE;", "X[1, 2] := TRUE;", "R := X[3][15];", "R := X[3, 15];"}) {
            check(problemsOf(types::pathProblems(p, code)).empty(), f + " : " + code + " sans faute (" + problemsOf(types::pathProblems(p, code)) + ")");
            scriptcheck::Scope sc;
            sc.project = &p;
            check(findingsOf(scriptcheck::check(sc, code)).empty(), f + " : Compiler accepte " + code + " (" + findingsOf(scriptcheck::check(sc, code)) + ")");
        }
        const std::string out = problemsOf(types::pathProblems(p, "R := X[4][0];"));
        check(out.find("hors des bornes") != std::string::npos, f + " : X[4][0] hors des bornes (" + out + ")");
        const std::string in = problemsOf(types::pathProblems(p, "R := X[0][16];"));
        check(in.find("hors des bornes") != std::string::npos, f + " : X[0][16] hors des bornes, second indice (" + in + ")");
        // fx : la verification d'une formule
        exprcheck::Context ctx;
        ctx.project = &p;
        std::string fx;
        for (const auto& q : exprcheck::check(ctx, "X[1][2] AND X[3, 15]")) fx += q.message + " ; ";
        check(fx.empty(), f + " : une formule X[1][2] AND X[3, 15] sans faute (" + fx + ")");
    }
    // 1.50.1 : M[1] seul est la ligne 1 (un sous-tableau) - plus une faute ; ses indices se verifient.
    Project p;
    addVar(p, "M", kTwoD);
    const std::string row = problemsOf(types::pathProblems(p, "R := M[1].Length;"));
    check(row.empty() && types::typeOfPath(p, "M[1]") == "ARRAY[0..15] OF BOOL", "deux dimensions : M[1] est une ligne (" + row + ")");
    const std::string outRow = problemsOf(types::pathProblems(p, "R := M[4].Length;"));
    check(outRow.find("hors des bornes") != std::string::npos, "... M[4] : hors des bornes (" + outRow + ")");
}

// ---- le moteur : variables IHM, formules, locales, membres, declarations, fonctions ----------------
void leMoteur() {
    std::printf("-- le moteur : ecrire et lire des deux facons\n");
    for (const char* t : kForms) {
        const std::string f = formName(t);
        Project p;
        addVar(p, "X", t);
        addVar(p, "R", "DINT", "0");
        Sim s(p, "X[1][2] := TRUE;\nX[3, 15] := TRUE;\nIF X[1, 2] AND X[3][15] THEN R := 1; END_IF;\n");
        check(s.error.empty() && s.read("R") == "1", f + " : un script ecrit X[1][2] et X[3, 15], les relit de l'autre facon (R = " + s.read("R") + " " + s.error + ")");
        // X.Length : les cases du premier niveau (4 lignes d'un tableau de tableaux ; 64 cases a deux dimensions)
        check(s.fx("X.Length") == (t == kNested ? "4" : "64"), f + " : X.Length = " + s.fx("X.Length"));
        check(s.fx("X[1][2]") == "TRUE" && s.fx("X[1, 2]") == "TRUE" && s.fx("X[0][0]") == "FALSE",
              f + " : une formule (fx) lit X[1][2] et X[1, 2] (" + s.fx("X[1][2]") + ", " + s.fx("X[1, 2]") + ")");
        // une case hors des bornes : refusee et dite
        Sim bad(p, "X[4][0] := TRUE;\n");
        check(!bad.error.empty(), f + " : X[4][0] := TRUE refuse (" + bad.error + ")");
        // une locale de chacune des formes
        Sim loc(p, std::string("VAR\n  a : ") + t + ";\nEND_VAR\na[1][2] := TRUE;\na[3, 4] := TRUE;\nIF a[1, 2] AND a[3][4] THEN R := 7; END_IF;\n");
        check(loc.error.empty() && loc.read("R") == "7", f + " : une locale, a[1][2] et a[1, 2] (R = " + loc.read("R") + " " + loc.error + ")");
        Sim locBad(p, std::string("VAR\n  a : ") + t + ";\nEND_VAR\na[1][16] := TRUE;\n");
        check(locBad.error.find("hors des bornes") != std::string::npos, f + " : une locale, a[1][16] hors des bornes (" + locBad.error + ")");
    }
    // les valeurs initiales d'une variable IHM tableau de tableaux : une liste par ligne
    {
        Project p;
        addVar(p, "T", "ARRAY[0..1] OF ARRAY[0..2] OF INT", "[[1, 2, 3], [4, 5, 6]]");
        addVar(p, "R", "DINT", "0");
        Sim s(p, "R := T[0][0] + T[1][2] * 10;\n");
        check(s.read("R") == "61" && s.error.empty(), "valeurs initiales [[1, 2, 3], [4, 5, 6]] : T[0][0] = 1, T[1][2] = 6 (R = " + s.read("R") + ")");
        check(s.fx("T[1].Length") == "3", "T[1].Length : 3 (" + s.fx("T[1].Length") + ")");
    }
    // un membre d'un type IHM
    {
        Project p;
        HmiType four;
        four.id = p.allocate();
        four.name = "T_Four";
        four.members = {TypeMember{"Grille", kNested}, TypeMember{"Plan", kTwoD}};
        p.programs.types.push_back(four);
        addVar(p, "F", "T_Four");
        addVar(p, "R", "DINT", "0");
        Sim s(p, "F.Grille[2][3] := TRUE;\nF.Plan[2][3] := TRUE;\nIF F.Grille[2, 3] AND F.Plan[2, 3] THEN R := 1; END_IF;\n");
        check(s.read("R") == "1" && s.error.empty(), "un membre de type IHM : Grille (tableau de tableaux) et Plan (deux dimensions) (R = " + s.read("R") + " " + s.error + ")");
    }
    // une declaration du modele (l'onglet Variables d'un script)
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Variable;
        d.name = "Grille";
        d.type = kNested;
        Declaration e = d;
        e.id = p.allocate();
        e.name = "Plan";
        e.type = kTwoD;
        const TypeKnown known = [](std::string_view) { return false; };
        Script sc;
        sc.name = "Essai";
        sc.body = "Grille[1][2] := TRUE;\nPlan[1][2] := TRUE;\nIF Grille[1, 2] AND Plan[1, 2] THEN R := 3; END_IF;\n";
        sc.decls = {d, e};
        check(errorsOf(checkScript(sc, known)).empty(), "les declarations du modele : les deux formes (" + errorsOf(checkScript(sc, known)) + ")");
        Sim s(p, sc.body, sc.decls);
        check(s.read("R") == "3" && s.error.empty(), "... et le moteur (R = " + s.read("R") + " " + s.error + ")");
    }
    // une fonction IHM : un parametre de chaque forme (par reference) et une variable IHM passee
    {
        Project p;
        addVar(p, "G", kNested);
        addVar(p, "M", kTwoD);
        addVar(p, "R", "DINT", "0");
        HmiFunction fn;
        fn.id = p.allocate();
        fn.name = "Compter";
        fn.returnType = "DINT";
        Declaration a;
        a.id = p.allocate();
        a.kind = DeclKind::Parameter;
        a.name = "g";
        a.type = kNested;
        a.mode = PassMode::InOut;
        Declaration b = a;
        b.id = p.allocate();
        b.name = "m";
        b.type = kTwoD;
        fn.decls = {a, b};
        fn.body = "VAR\n  i : INT;\n  j : INT;\n  n : DINT;\nEND_VAR\nFOR i := 0 TO 3 DO\n  FOR j := 0 TO 15 DO\n"
                  "    IF g[i][j] THEN n := n + 1; END_IF;\n    IF m[i, j] THEN n := n + 100; END_IF;\n  END_FOR;\nEND_FOR;\nCompter := n;\n";
        p.programs.functions.push_back(fn);
        const TypeKnown known = [](std::string_view) { return false; };
        check(errorsOf(checkFunction(fn, known)).empty(), "une fonction aux parametres des deux formes : sans faute (" + errorsOf(checkFunction(fn, known)) + ")");
        Sim s(p, "G[0][1] := TRUE;\nG[2, 5] := TRUE;\nM[3][3] := TRUE;\nR := Compter(G, M);\n");
        check(s.read("R") == "102" && s.error.empty(), "... appelee avec les variables IHM : 2 + 100 (R = " + s.read("R") + " " + s.error + ")");
    }
    // un parametre de popup ou de symbole
    {
        Project p;
        check(params::typeKnown(p, kNested) && params::typeKnown(p, kTwoD), "un parametre de popup : les deux formes sont des types connus");
        check(params::typeAccepts(kNested, kNested) && params::typeAccepts(kTwoD, kTwoD), "... et chacune passe a elle-meme");
    }
}

} // namespace

#include "tableaux_ndim.inl"     // 1.50.1 : N dimensions, les grands tableaux

int main() {
    std::printf("tableaux : ARRAY OF ARRAY et ARRAY[a, b], partout\n");
    lesTypes();
    lesChemins();
    leMoteur();
    lesTypesND();
    lesCheminsND();
    leMoteurND();
    lesGrandsTableaux();
    lesGrandsTableauxIHM();
    toutSerre();
    std::printf("%d controles, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
