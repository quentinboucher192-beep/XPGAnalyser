// =============================================================================
//  tests/tableaux_ndim.inl - 1.50.1 : AUTANT DE DIMENSIONS QU'ON VEUT, et les
//  GRANDS TABLEAUX (inclus par tableaux_test.cpp)
// -----------------------------------------------------------------------------
//  ARRAY[0..3, 0..7, 0..15] OF BOOL (trois dimensions), ARRAY[0..1, 0..2, 0..3,
//  0..4] OF INT (quatre), ARRAY[0..1, 0..2] OF ARRAY[0..3, 0..4, 0..5] OF INT
//  (melange avec un tableau de tableaux) : acceptes partout ou deux dimensions
//  l'etaient (types, registre, selecteur, declarations, valeurs initiales,
//  chemins, Compiler, fx, moteur, locales, membres, parametres, en-tete C), lus
//  a[i, j, k], a[i][j][k], a[i] (une couche), a[i, j] (une ligne).
//
//  LES GRANDS TABLEAUX : plus de << 1 000 000 de cases au plus >>. Un tableau de
//  valeurs simples d'au moins sim::packFrom() cases se range serre (un BOOL : 1
//  bit) ; une variable IHM de plus de 100 000 cases vit entiere dans la memoire
//  de l'IHM. Mesures : temps et memoire (TAB_OBS=1 les ecrit toutes).
// =============================================================================
#include <chrono>
#include <fstream>

namespace {

const char* const k3D = "ARRAY[0..3, 0..7, 0..15] OF BOOL";
const char* const k4D = "ARRAY[0..1, 0..2, 0..3, 0..4] OF INT";
const char* const kMix = "ARRAY[0..1, 0..2] OF ARRAY[0..3, 0..4, 0..5] OF INT";

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// La memoire residente du processus (Linux : /proc/self/statm) ; 0 ailleurs.
long long residentBytes() {
#ifdef __linux__
    std::ifstream f("/proc/self/statm");
    long long pages = 0, resident = 0;
    if (f >> pages >> resident) return resident * 4096;
#endif
    return 0;
}

void measure(const std::string& what) { std::printf("  mesure %s\n", what.c_str()); }

// ---- les types --------------------------------------------------------------------------------------
void lesTypesND() {
    std::printf("-- 1.50.1 : trois, quatre dimensions, et melangees a un tableau de tableaux\n");
    Project p;
    const auto reg = typereg::Registry::build(p);
    for (const char* t : {k3D, k4D, kMix}) {
        const std::string f = t;
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
        check(sh.form == typeform::Form::Array2D && typeform::compose(sh) == t,
              f + " : le selecteur le decompose (Tableau a N dimensions) et le recompose (" + typeform::compose(sh) + ")");
        check(!types::summary(p, t).empty(), f + " : resume " + types::summary(p, t));
        const Project empty;
        check(params::typeKnown(empty, t) && params::typeAccepts(t, t), f + " : un parametre de popup le connait");
    }
    types::Spec s3;
    (void)types::parseSpec(k3D, s3);
    check(s3.dims == 3 && s3.length() == 512 && s3.count(2) == 16, "trois dimensions : 3, 512 cases, 16 sur la troisieme");
    check(types::weightOf(p, k3D).words == 32, "512 BOOL ranges 16 par mot : 32 mots (" + std::to_string(types::weightOf(p, k3D).words) + ")");
    const auto f3 = types::flatten(p, "X", k3D);
    check(f3.ok() && f3.leaves.size() == 512, "trois dimensions : deplie en 512 cases (" + std::to_string(f3.leaves.size()) + " " + f3.error + ")");
    // la case [1,2,3] : la 1*128 + 2*16 + 3 = 163e - le bit 3 du mot 10
    bool placed = false;
    for (const auto& l : f3.leaves)
        if (l.path == "X[1,2,3]" && l.word == 10 && l.bit == 3) placed = true;
    check(placed, "la case X[1,2,3] est le bit 3 du mot 10 (la derniere dimension la plus vite)");
    check(f3.leaves.back().path == "X[3,7,15]", "la derniere case : X[3,7,15] (" + f3.leaves.back().path + ")");
    const auto f4 = types::flatten(p, "Q", k4D);
    check(f4.ok() && f4.leaves.size() == 120 && f4.leaves[119].path == "Q[1,2,3,4]" && f4.leaves[1].path == "Q[0,0,0,1]",
          "quatre dimensions : 120 cases, Q[0,0,0,1] puis ... Q[1,2,3,4] (" + std::to_string(f4.leaves.size()) + ")");
    const auto fm = types::flatten(p, "M", kMix);
    check(fm.ok() && fm.leaves.size() == 720 && fm.leaves.back().path == "M[1,2][3,4,5]",
          "melange : 6 x 120 = 720 cases, la derniere M[1,2][3,4,5] (" + std::to_string(fm.leaves.size()) + " " + (fm.leaves.empty() ? "" : fm.leaves.back().path) + ")");
    check(types::totalCells(kMix) == 720 && types::leafElement(kMix) == "INT", "totalCells, leafElement d'un melange (720, INT)");
    // le selecteur : + Dimension, une de moins
    check(typeform::label(typeform::Form::Array2D) == "Tableau \xC3\xA0 N dimensions", "la forme s'appelle << Tableau a N dimensions >>");
    check(typeform::fromText("Tableau 2D") == typeform::Form::Array2D && typeform::fromText("tableau2d") == typeform::Form::Array2D,
          "l'ancien libelle (Tableau 2D) et la cle (tableau2d) la designent encore");
    check(typeform::withDimension("0..3, 0..9") == "0..3, 0..9, 0..9" && typeform::withDimension("0..9") == "0..9, 0..9",
          "+ Dimension : la derniere recopiee (" + typeform::withDimension("0..3, 0..9") + ")");
    check(typeform::withoutDimension("0..3, 0..7, 0..15") == "0..3, 0..7" && typeform::dimensionsOf("0..3, 0..7, 0..15") == 3,
          "une dimension de moins (" + typeform::withoutDimension("0..3, 0..7, 0..15") + ")");
    // la valeur d'une constante, d'une valeur initiale (un litteral a trois niveaux)
    {
        std::string why;
        check(typeform::valueFits(p, "ARRAY[0..1, 0..1, 0..1] OF INT", "[[[1, 2], [3, 4]], [[5, 6], [7, 8]]]", &why),
              "un litteral [[[1, 2], [3, 4]], [[5, 6], [7, 8]]] va dans trois dimensions (" + why + ")");
        why.clear();
        check(!typeform::valueFits(p, "ARRAY[0..1, 0..1, 0..1] OF INT", "[[[1, 2, 3], [3, 4]], [[5, 6], [7, 8]]]", &why) && !why.empty(),
              "... une valeur de trop : refusee (" + why + ")");
        const auto fl = types::flatten(p, "C", "ARRAY[0..1, 0..1, 0..1] OF INT", "[[[1, 2], [3, 4]], [[5, 6], [7, 8]]]");
        check(fl.ok() && fl.leaves.size() == 8 && fl.leaves[5].path == "C[1,0,1]" && fl.leaves[5].initial == "6",
              "les valeurs initiales a trois niveaux : C[1,0,1] = 6 (" + (fl.leaves.size() > 5 ? fl.leaves[5].initial : std::string("?")) + ")");
    }
    // le texte d'une case, d'une couche ; le type d'une partie
    check(types::cellText(s3, 163) == "[1,2,3]" && types::cellText(s3, 0) == "[0,0,0]", "cellText : [1,2,3] pour la 163e case");
    check(types::subArrayType(k3D, 1) == "ARRAY[0..7, 0..15] OF BOOL" && types::subArrayType(k3D, 2) == "ARRAY[0..15] OF BOOL",
          "le sous-tableau : une couche, une ligne (" + types::subArrayType(k3D, 1) + ")");
    // les fautes, a leur niveau
    {
        std::string why;
        types::Spec bad;
        check(!types::parseSpec("ARRAY[0..3, 0..x, 0..1] OF INT", bad, &why) && why.find("deux entiers") != std::string::npos,
              "une borne illisible de la deuxieme dimension sur trois : " + why);
        why.clear();
        check(!types::parseSpec("ARRAY[0..3, 0..7, 9..1] OF INT", bad, &why) && why.find("haute avant la basse") != std::string::npos,
              "une borne fausse de la troisieme dimension : " + why);
    }
    // l'en-tete C du projet : la declaration a toutes ses dimensions
    {
        Project q;
        addVar(q, "Cube", k3D);
        addVar(q, "Mix", kMix);
        const std::string h = projectHeader(q, ScriptLang::C);
        check(h.find("Cube[4][8][16]") != std::string::npos, "l'en-tete C : bool Cube[4][8][16]");
        check(h.find("Mix[2][3][4][5][6]") != std::string::npos, "l'en-tete C : le melange, int16_t Mix[2][3][4][5][6]");
    }
}

// ---- les chemins ------------------------------------------------------------------------------------
void lesCheminsND() {
    std::printf("-- 1.50.1 : X[1, 2, 3], X[1][2][3], X[1] (une couche), X[1, 2] (une ligne)\n");
    Project p;
    addVar(p, "X", k3D);
    addVar(p, "Q", k4D);
    addVar(p, "M", kMix);
    addVar(p, "R", "BOOL", "FALSE");
    addVar(p, "N", "DINT", "0");
    check(types::typeOfPath(p, "X[1,2,3]") == "BOOL" && types::typeOfPath(p, "X[1][2][3]") == "BOOL" && types::typeOfPath(p, "X[1, 2][3]") == "BOOL",
          "X[1,2,3], X[1][2][3], X[1, 2][3] : des BOOL");
    check(types::typeOfPath(p, "X[1]") == "ARRAY[0..7, 0..15] OF BOOL", "X[1] : une couche (" + types::typeOfPath(p, "X[1]") + ")");
    check(types::typeOfPath(p, "X[1, 2]") == "ARRAY[0..15] OF BOOL" && types::typeOfPath(p, "X[1][2]") == "ARRAY[0..15] OF BOOL",
          "X[1, 2] et X[1][2] : une ligne (" + types::typeOfPath(p, "X[1, 2]") + ")");
    check(types::typeOfPath(p, "X[1].Length") == "DINT", "X[1].Length : la propriete de la couche");
    check(types::typeOfPath(p, "M[1, 2][3, 4, 5]") == "INT" && types::typeOfPath(p, "M[1][2][3][4][5]") == "INT"
              && types::typeOfPath(p, "M[1, 2, 3, 4, 5]") == "INT" && types::typeOfPath(p, "M[1, 2]") == "ARRAY[0..3, 0..4, 0..5] OF INT",
          "le melange : M[1, 2][3, 4, 5], M[1][2][3][4][5], M[1, 2, 3, 4, 5] ; M[1, 2] (" + types::typeOfPath(p, "M[1, 2]") + ")");
    for (const char* code : {"X[1,2,3] := TRUE;", "X[1][2][3] := TRUE;", "R := X[3, 7][15];", "N := X[1].Length;", "N := X[1, 2].Length;",
                             "Q[1, 2, 3, 4] := 5;", "Q[1][2][3][4] := 5;", "M[1, 2][3, 4, 5] := 7;", "M[1][2][3][4][5] := 7;", "N := M[1, 2, 3, 4, 5];"}) {
        check(problemsOf(types::pathProblems(p, code)).empty(), std::string("sans faute : ") + code + " (" + problemsOf(types::pathProblems(p, code)) + ")");
        scriptcheck::Scope sc;
        sc.project = &p;
        check(findingsOf(scriptcheck::check(sc, code)).empty(), std::string("Compiler accepte ") + code + " (" + findingsOf(scriptcheck::check(sc, code)) + ")");
    }
    const auto said = [&](const char* code, const char* word) {
        const std::string out = problemsOf(types::pathProblems(p, code));
        check(out.find(word) != std::string::npos, std::string(code) + " -> " + out);
    };
    said("R := X[4, 0, 0];", "hors des bornes");
    said("R := X[0, 8, 0];", "hors des bornes");
    said("R := X[0][0][16];", "hors des bornes");
    said("R := X[1, 2, 3, 4];", "X a 3 dimensions : 3 indices entre crochets (ARRAY[0..3, 0..7, 0..15] OF BOOL), pas 4");
    said("N := Q[1, 2, 3, 4, 5];", "Q a 4 dimensions : 4 indices");
    said("R := X[1].Foo;", ".Foo n'existe pas");
    // fx : une formule
    exprcheck::Context ctx;
    ctx.project = &p;
    std::string fx;
    for (const auto& q : exprcheck::check(ctx, "X[1][2][3] AND X[3, 7, 15]")) fx += q.message + " ; ";
    check(fx.empty(), "une formule X[1][2][3] AND X[3, 7, 15] sans faute (" + fx + ")");
}

// ---- le moteur --------------------------------------------------------------------------------------
void leMoteurND() {
    std::printf("-- 1.50.1 : le moteur - variables IHM, locales, membres, declarations, fonctions, a N dimensions\n");
    {
        Project p;
        addVar(p, "X", k3D);
        addVar(p, "R", "DINT", "0");
        Sim s(p, "X[1][2][3] := TRUE;\nX[3, 7, 15] := TRUE;\nX[2, 5][9] := TRUE;\n"
                 "IF X[1, 2, 3] AND X[3][7][15] AND X[2][5][9] THEN R := 1; END_IF;\n");
        check(s.error.empty() && s.read("R") == "1", "variable IHM : X[1][2][3], X[3, 7, 15], X[2, 5][9] ecrits et relus de l'autre facon (R = " + s.read("R") + " " + s.error + ")");
        check(s.fx("X.Length") == "512" && s.fx("X.Dimensions") == "3" && s.fx("X.Rows") == "4" && s.fx("X.Columns") == "8",
              "X.Length 512, X.Dimensions 3, Rows 4, Columns 8 (" + s.fx("X.Length") + ", " + s.fx("X.Dimensions") + ")");
        check(s.fx("X[1][2][3]") == "TRUE" && s.fx("X[1, 2, 3]") == "TRUE" && s.fx("X[0, 0, 0]") == "FALSE", "une formule (fx) lit les deux ecritures");
        check(s.fx("X[1].Length") == "128" && s.fx("X[1, 2].Length") == "16", "fx : une couche X[1] (128 cases), une ligne X[1, 2] (16) (" + s.fx("X[1].Length") + ", " + s.fx("X[1, 2].Length") + ")");
        Sim bad(p, "X[0, 0, 16] := TRUE;\n");
        check(!bad.error.empty(), "X[0, 0, 16] := TRUE refuse (" + bad.error + ")");
        // une ligne passee entiere : compter ses cases vraies
        Sim row(p, "VAR\n  i : INT;\n  n : DINT;\nEND_VAR\nX[1, 2, 0] := TRUE;\nX[1, 2, 15] := TRUE;\nFOR i := 0 TO 15 DO\n"
                   "  IF X[1, 2][i] THEN n := n + 1; END_IF;\nEND_FOR;\nR := n + X[1, 2].Length * 100;\n");
        check(row.error.empty() && row.read("R") == "1602", "une ligne X[1, 2][i] parcourue, X[1, 2].Length (R = " + row.read("R") + " " + row.error + ")");
    }
    // des locales de trois et quatre dimensions, un melange
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        Sim loc(p, "VAR\n  a : ARRAY[0..3, 0..7, 0..15] OF BOOL;\n  q : ARRAY[0..1, 0..2, 0..3, 0..4] OF INT;\n"
                   "  m : ARRAY[0..1, 0..2] OF ARRAY[0..3, 0..4, 0..5] OF INT;\n  ligne : ARRAY[0..15] OF BOOL;\n  couche : ARRAY[0..7, 0..15] OF BOOL;\nEND_VAR\n"
                   "a[1][2][3] := TRUE;\nq[1, 2, 3, 4] := 40;\nq[0][1][2][3] := 2;\nm[1, 2][3, 4, 5] := 7;\nm[0][1][2][3][4] := 3;\n"
                   "ligne := a[1, 2];\ncouche := a[1];\n"
                   "IF a[1, 2, 3] AND ligne[3] AND couche[2, 3] AND couche[2][3] THEN R := 1; END_IF;\n"
                   "R := R + q[1][2][3][4] + q[0, 1, 2, 3] * 10 + m[1][2][3][4][5] * 100 + m[0, 1, 2, 3, 4] * 1000;\n"
                   "R := R + LOWER_BOUND(q, 4) + UPPER_BOUND(q, 4) * 100000 + a.Dimensions * 1000000 + q.Length * 10000000;\n");
        check(loc.error.empty() && loc.read("R") == "1203403761",
              "des locales a 3 et 4 dimensions, un melange, une ligne et une couche copiees (R = " + loc.read("R") + " " + loc.error + ")");
        Sim locBad(p, "VAR\n  q : ARRAY[0..1, 0..2, 0..3, 0..4] OF INT;\nEND_VAR\nq[1, 2, 3, 5] := 1;\n");
        check(locBad.error.find("hors des bornes") != std::string::npos && locBad.error.find("dimension 4") != std::string::npos,
              "une locale : q[1, 2, 3, 5] hors des bornes, dimension 4 (" + locBad.error + ")");
        // une ecriture dans une ligne ecrit le tableau (une vue, pas une copie)
        Sim view(p, "VAR\n  a : ARRAY[0..3, 0..7, 0..15] OF BOOL;\nEND_VAR\na[2][3][4] := TRUE;\nIF a[2, 3][4] AND a[2][3, 4] THEN R := 5; END_IF;\n");
        check(view.error.empty() && view.read("R") == "5", "a[2][3][4] ecrit, a[2, 3][4] et a[2][3, 4] le relisent (R = " + view.read("R") + " " + view.error + ")");
    }
    // un membre d'un type IHM, une declaration du modele, une fonction aux parametres a trois dimensions
    {
        Project p;
        HmiType t;
        t.id = p.allocate();
        t.name = "T_Silo";
        t.members = {TypeMember{"Cube", k3D}, TypeMember{"Mix", kMix}};
        p.programs.types.push_back(t);
        addVar(p, "S", "T_Silo");
        addVar(p, "C", k3D);
        addVar(p, "R", "DINT", "0");
        Sim s(p, "S.Cube[1][2][3] := TRUE;\nS.Mix[1, 2][3, 4, 5] := 9;\nIF S.Cube[1, 2, 3] THEN R := S.Mix[1][2][3][4][5]; END_IF;\n");
        check(s.read("R") == "9" && s.error.empty(), "un membre de type IHM a trois dimensions, un membre melange (R = " + s.read("R") + " " + s.error + ")");
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Variable;
        d.name = "Bloc";
        d.type = k4D;
        const TypeKnown known = [](std::string_view) { return false; };
        Script sc;
        sc.name = "Essai";
        sc.body = "Bloc[1][2][3][4] := 6;\nR := Bloc[1, 2, 3, 4];\n";
        sc.decls = {d};
        check(errorsOf(checkScript(sc, known)).empty(), "une declaration du modele a quatre dimensions (" + errorsOf(checkScript(sc, known)) + ")");
        Sim sd(p, sc.body, sc.decls);
        check(sd.read("R") == "6" && sd.error.empty(), "... et le moteur (R = " + sd.read("R") + " " + sd.error + ")");
        HmiFunction fn;
        fn.id = p.allocate();
        fn.name = "Compter3";
        fn.returnType = "DINT";
        Declaration a;
        a.id = p.allocate();
        a.kind = DeclKind::Parameter;
        a.name = "c";
        a.type = k3D;
        a.mode = PassMode::InOut;
        fn.decls = {a};
        fn.body = "VAR\n  i : INT;\n  j : INT;\n  k : INT;\n  n : DINT;\nEND_VAR\nFOR i := 0 TO 3 DO\n  FOR j := 0 TO 7 DO\n    FOR k := 0 TO 15 DO\n"
                  "      IF c[i, j, k] THEN n := n + 1; END_IF;\n    END_FOR;\n  END_FOR;\nEND_FOR;\nc[0][0][0] := TRUE;\nCompter3 := n;\n";
        p.programs.functions.push_back(fn);
        check(errorsOf(checkFunction(fn, known)).empty(), "une fonction au parametre a trois dimensions : sans faute (" + errorsOf(checkFunction(fn, known)) + ")");
        Sim sf(p, "C[1][2][3] := TRUE;\nC[3, 7, 15] := TRUE;\nR := Compter3(C);\nIF C[0, 0, 0] THEN R := R + 10; END_IF;\n");
        check(sf.read("R") == "12" && sf.error.empty(), "... appelee avec la variable IHM, par reference (R = " + sf.read("R") + " " + sf.error + ")");
    }
    // la valeur initiale d'une variable IHM a trois dimensions
    {
        Project p;
        addVar(p, "C", "ARRAY[0..1, 0..1, 0..1] OF INT", "[[[1, 2], [3, 4]], [[5, 6], [7, 8]]]");
        addVar(p, "R", "DINT", "0");
        Sim s(p, "R := C[1, 0, 1] * 10 + C[0][1][1];\n");
        check(s.read("R") == "64" && s.error.empty(), "valeurs initiales a trois niveaux : C[1,0,1] = 6, C[0][1][1] = 4 (R = " + s.read("R") + " " + s.error + ")");
    }
}

// ---- les grands tableaux ----------------------------------------------------------------------------
void lesGrandsTableaux() {
    std::printf("-- 1.50.1 : les grands tableaux - 10 000 000 de BOOL, ranges serres\n");
    const char* const kBig = "ARRAY[0..999, 0..9999] OF BOOL";
    Project p;
    {
        std::string why;
        types::Spec s;
        check(types::parseSpec(kBig, s, &why) && s.length() == 10000000, "ARRAY[0..999, 0..9999] OF BOOL : 10 000 000 de cases, accepte (" + why + ")");
        why.clear();
        check(types::validType(p, kBig, &why), "... une variable IHM peut l'avoir (" + why + ")");
        check(types::isBigArray(kBig) && types::isRich(kBig) && !types::isDynamic(kBig), "... un grand tableau : un objet de la memoire de l'IHM, de taille fixe");
        const long long mem = types::memoryOf(kBig);
        check(mem > 1000000 && mem < 2000000, "... sa memoire estimee : " + types::memoryText(mem) + " (1 bit par BOOL)");
        const std::string reason = types::unbindableReason(kBig);
        check(reason.find("m\xC3\xA9moire de l'IHM") != std::string::npos && reason.find("10 000 000") != std::string::npos, "... ne se lie pas : " + reason);
        check(types::summary(p, kBig).find("m\xC3\xA9moire de l'IHM") != std::string::npos, "... resume : " + types::summary(p, kBig));
        why.clear();
        check(!types::parseSpec("ARRAY[0..99999, 0..99999] OF BOOL", s, &why) && why.find("trop grand") != std::string::npos
                  && why.find("Go") != std::string::npos && why.find("10 000 000 000") != std::string::npos,
              "10 000 000 000 de BOOL : refuse, avec l'estimation (" + why + ")");
        why.clear();
        check(!types::validType(p, "ARRAY[0..199999999] OF LREAL", &why) && why.find("trop grand") != std::string::npos,
              "200 000 000 de LREAL : refuse (" + why + ")");
        why.clear();
        check(types::validType(p, "ARRAY[0..999] OF ARRAY[0..999] OF INT", &why) && types::isBigArray("ARRAY[0..999] OF ARRAY[0..999] OF INT"),
              "1 000 x 1 000 INT (un tableau de tableaux) : accepte, un grand tableau (" + why + ")");
        const auto reg = typereg::Registry::build(p);
        check(reg->resolve(kBig, typereg::UseVariable).ok, "le registre l'accepte pour une variable IHM");
    }
    // le moteur seul : 10 000 000 de BOOL ecrits et relus, chronometres
    {
        auto t = std::make_shared<sim::TypeDesc>();
        t->kind = sim::TypeDesc::Kind::Array;
        t->bounds = {{0, 999}, {0, 9999}};
        t->element = sim::scalarType(sim::Type::Bool);
        const long long before = residentBytes();
        auto t0 = std::chrono::steady_clock::now();
        const sim::ObjRef big = sim::makeObj(t);
        const double made = secondsSince(t0);
        check(big && big->packed && big->items.empty() && sim::cellCount(*big) == 10000000, "makeObj : un bloc serre, pas 10 000 000 d'objets");
        const std::size_t held = sim::heldMemory(*big);
        check(held < 2000000, "sa memoire : " + sim::memoryText(static_cast<std::int64_t>(held)) + " (avant 1.50.1 : ~"
                                  + sim::memoryText(sim::objectBytes() * 10000000) + ", refuse)");
        t0 = std::chrono::steady_clock::now();
        for (std::size_t k = 0; k < 10000000; k += 3) sim::setCellValue(*big, k, sim::Value::boolean(true));
        const double wrote = secondsSince(t0);
        t0 = std::chrono::steady_clock::now();
        std::size_t trues = 0;
        for (std::size_t k = 0; k < 10000000; ++k) trues += sim::cellValue(*big, k).isTruthy() ? 1 : 0;
        const double read = secondsSince(t0);
        check(trues == 3333334, "un tiers des cases ecrites, toutes relues (" + std::to_string(trues) + ")");
        const long long after = residentBytes();
        measure("10 000 000 BOOL : cree en " + std::to_string(made) + " s, 3 333 334 ecritures en " + std::to_string(wrote) + " s, 10 000 000 lectures en "
                + std::to_string(read) + " s ; memoire tenue " + sim::memoryText(static_cast<std::int64_t>(held))
                + (before && after ? ", processus +" + sim::memoryText(after - before) : std::string{}));
        check(made + wrote + read < 20.0, "le tout en moins de 20 s (" + std::to_string(made + wrote + read) + " s)");
        // des cases sorties en objet (une ecriture par cellAt) : elles rentrent dans le bloc quand on les lache
        for (std::size_t k = 1; k < 100000; k += 3) {
            const sim::ObjRef c = sim::cellAt(*big, k);
            c->value.assignFrom(sim::Value::boolean(true));
        }
        check(big->packed->live.size() < 2000 && sim::cellValue(*big, 4).isTruthy() && sim::cellValue(*big, 99997).isTruthy(),
              "33 333 cases sorties puis lachees : rentrees dans le bloc (" + std::to_string(big->packed->live.size()) + " encore sorties)");
        // une copie, une comparaison
        t0 = std::chrono::steady_clock::now();
        const sim::ObjRef copy = sim::deepCopy(*big);
        const bool same = sim::deepEquals(*big, *copy);
        sim::setCellValue(*copy, 9999999, sim::Value::boolean(false));
        sim::setCellValue(*copy, 9999998, sim::Value::boolean(true));
        const bool differs = !sim::deepEquals(*big, *copy);
        measure("copie et deux comparaisons de 10 000 000 BOOL : " + std::to_string(secondsSince(t0)) + " s");
        check(copy->packed && copy->packed != big->packed && same && differs, "une copie a son propre bloc ; egale, puis differente");
    }
    // 2 000 000 d'INT, 1 000 000 de REAL, de STRING : la memoire de chacun
    for (const auto& [text, most] : {std::pair<const char*, long long>{"ARRAY[0..1999, 0..999] OF INT", 5000000},
                                     {"ARRAY[0..999999] OF REAL", 9000000}, {"ARRAY[0..999, 0..999] OF STRING", 40000000},
                                     {"ARRAY[0..9, 0..99, 0..99, 0..99] OF DINT", 41000000}}) {
        const long long est = types::memoryOf(text);
        check(est < most, std::string(text) + " : " + types::memoryText(est));
    }
}

// Les grands tableaux dans l'IHM : une variable, une locale, un membre ; une reference, une fonction de liste.
void lesGrandsTableauxIHM() {
    std::printf("-- 1.50.1 : un grand tableau dans l'IHM (variable, locale, membre) et ses scripts\n");
    Project p;
    addVar(p, "Big", "ARRAY[0..999, 0..9999] OF BOOL", "FALSE");
    addVar(p, "Grand", "ARRAY[0..199999] OF INT", "7");
    addVar(p, "R", "DINT", "0");
    const long long before = residentBytes();
    auto t0 = std::chrono::steady_clock::now();
    Sim s(p, "VAR\n  j : INT;\n  n : DINT;\nEND_VAR\nBig[999, 9999] := TRUE;\nBig[5][7] := TRUE;\n"
             "FOR j := 0 TO 9999 DO\n  Big[3, j] := TRUE;\nEND_FOR;\nFOR j := 0 TO 9999 DO\n  IF Big[3][j] THEN n := n + 1; END_IF;\nEND_FOR;\n"
             "Grand[199999] := Grand[0] + 1;\nR := n + Big.Length / 1000 + Grand[199999] * 100000000;\n");
    const double ran = secondsSince(t0);
    check(s.error.empty() && s.read("R") == "800020000", "Big[999, 9999], Big[5][7], une ligne entiere ecrite et relue ; Grand initialise a 7 (R = " + s.read("R") + " " + s.error + ")");
    const sim::ObjRef big = s.rt.richVariable("Big");
    check(big && big->packed && big->items.empty(), "la variable Big : un objet de la memoire de l'IHM, range serre");
    if (big) {
        measure("variable IHM Big (10 000 000 BOOL) + Grand (200 000 INT) : demarrage et script en " + std::to_string(ran) + " s ; Big tient "
                + sim::memoryText(static_cast<std::int64_t>(sim::heldMemory(*big)))
                + (before && residentBytes() ? ", processus +" + sim::memoryText(residentBytes() - before) : std::string{}));
        check(sim::heldMemory(*big) < 2000000, "Big tient moins de 2 Mo");
    }
    check(s.fx("Big[5, 7]") == "TRUE" && s.fx("Big[5][8]") == "FALSE" && s.fx("Big.Length") == "10000000" && s.fx("Big[3].Length") == "10000",
          "une formule (fx) : Big[5, 7], Big[5][8], Big.Length, Big[3].Length (" + s.fx("Big.Length") + ")");
    check(problemsOf(types::pathProblems(p, "Big[1000, 0] := TRUE;")).find("hors des bornes") != std::string::npos,
          "Compiler : Big[1000, 0] hors des bornes, comme un tableau ordinaire");
    // une locale grande, une reference vers une de ses cases (gardee pendant que d'autres cases passent)
    Sim loc(p, "VAR\n  a : ARRAY[0..999, 0..9999] OF BOOL;\n  vise : REF_TO BOOL;\n  j : INT;\n  n : DINT;\nEND_VAR\n"
               "vise := REF(a[5, 7]);\nFOR j := 0 TO 9999 DO\n  a[6, j] := TRUE;\nEND_FOR;\nvise^ := TRUE;\n"
               "FOR j := 0 TO 9999 DO\n  IF a[6][j] THEN n := n + 1; END_IF;\nEND_FOR;\nIF a[5][7] AND vise^ THEN n := n + 1000000; END_IF;\nR := n;\n");
    check(loc.error.empty() && loc.read("R") == "1010000", "une locale de 10 000 000 de BOOL, une REF_TO vers a[5, 7] gardee (R = " + loc.read("R") + " " + loc.error + ")");
    // une fonction de liste : jusqu'a 1 000 000 de cases (comme avant), au-dela dite
    Sim sum(p, "VAR\n  t : ARRAY[0..99999] OF INT;\nEND_VAR\nt[5] := 3;\nt[99999] := 4;\nR := LIST_SUM(t);\n");
    check(sum.error.empty() && sum.read("R") == "7", "LIST_SUM sur 100 000 INT ranges serres (R = " + sum.read("R") + " " + sum.error + ")");
    Sim tooMany(p, "R := LIST_SUM(Big);\n");
    check(tooMany.error.find("1 000 000 au plus") != std::string::npos, "LIST_SUM sur 10 000 000 de cases : dit (" + tooMany.error + ")");
    // un membre grand dans une structure : un membre objet
    {
        Project q;
        HmiType t;
        t.id = q.allocate();
        t.name = "T_Carte";
        t.members = {TypeMember{"Nom", "STRING"}, TypeMember{"Pixels", "ARRAY[0..1023, 0..767] OF BOOL"}};
        q.programs.types.push_back(t);
        addVar(q, "Ecran", "T_Carte");
        addVar(q, "R", "DINT", "0");
        const auto fl = types::flatten(q, "Ecran", "T_Carte");
        check(fl.ok() && fl.leaves.size() == 1 && fl.objects.size() == 1 && fl.objects[0].path == "Ecran.Pixels",
              "un membre de 786 432 BOOL : un membre objet (" + std::to_string(fl.leaves.size()) + " case, " + std::to_string(fl.objects.size()) + " objet " + fl.error + ")");
        Sim sq(q, "Ecran.Pixels[1023, 767] := TRUE;\nIF Ecran.Pixels[1023][767] THEN R := Ecran.Pixels.Length; END_IF;\n");
        check(sq.error.empty() && sq.read("R") == "786432", "... ecrit et relu (R = " + sq.read("R") + " " + sq.error + ")");
    }
}

// Les memes essais, chaque tableau range serre (packFrom = 1) : le bloc et ses vues font comme des cases ordinaires.
void toutSerre() {
    std::printf("-- 1.50.1 : les memes essais, tous les tableaux ranges serres\n");
    const auto before = g_failures;
    sim::setPackFrom(1);
    leMoteur();
    leMoteurND();
    sim::setPackFrom(65536);
    check(g_failures == before, "les essais du moteur passent aussi ranges serres");
}

} // namespace
