// =============================================================================
//  tests/hmi_setring_test.cpp - 1.12.7 (choix 5 de la maquette v3) : SET OF T
//  ET RING[N] OF T
// -----------------------------------------------------------------------------
//  Le SET (std::set : chaque valeur une fois, rangee) et le RING (un tampon
//  circulaire : les N dernieres valeurs) dans le moteur de l'IHM : les
//  declarations et les litteraux (un SET se range et perd ses doublons, un RING
//  garde les N dernieres), chaque native (SET_ADD, SET_REMOVE, SET_HAS,
//  SET_UNION, SET_INTERSECTION, SET_DIFFERENCE, RING_ADD) avec ses bords et ses
//  fautes, les membres (.Count, .Capacity, .Full), l'ordre de FOR EACH, LIST_ADD
//  sur un RING et sur un SET, les natives LIST_ qui lisent, les affectations
//  entre LIST, SET et RING, ce qu'un SET refuse (ecrire a une place), les
//  constantes, une variable IHM de type SET et RING ; puis Compiler (les fautes
//  des declarations et des natives) et ce qui tournait en 1.12.6 (une variable
//  nommee Set ou Ring, une fonction du projet nommee SET_ADD).
//
//  SETRING_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/sim/DialectLib.hpp"
#include "../src/sim/Interpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("SETRING_OBS"); return d && *d == '1'; }();

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

// Le projet d'essai : R (le resultat), un SET et un RING de l'IHM (des variables du projet).
struct Bench {
    Project p;
    Runtime rt;
    Bench(const std::string& resultType, const std::string& body) {
        const auto var = [this](const char* n, const std::string& t, const char* init) {
            Variable v;
            v.id = p.allocate();
            v.name = n;
            v.type = t;
            v.initial = init;
            p.programs.variables.push_back(v);
        };
        var("R", resultType.empty() ? "STRING" : resultType, "");
        var("SetIhm", "SET OF STRING", "['b', 'a', 'b']");
        var("RingIhm", "RING[3] OF INT", "[1, 2, 3, 4]");
        View v = makeView(p, "Vue_Essai");
        p.views.push_back(v);
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
    std::string run(std::string* error) {
        std::string err;
        const bool ok = rt.callScript("Essai", 0.2, &err);
        if (error) *error = ok ? std::string{} : err;
        sim::Value out;
        return rt.environment().read("R", out) ? shown(out) : std::string("#");
    }
};

// Les variables d'essai (la maquette : Postes_Actifs, Autre_Choix, Dernieres).
const char* kDecl =
    "VAR\n"
    "  Postes : SET OF STRING := ['P3', 'P1', 'P3'];\n"
    "  Autre : SET OF STRING := ['P2', 'P3', 'P4'];\n"
    "  Nombres : SET OF INT := [5, 1, 3, 1];\n"
    "  VideS : SET OF REAL;\n"
    "  Dernieres : RING[5] OF REAL;\n"
    "  Petit : RING[3] OF INT := [1, 2, 3, 4, 5];\n"
    "  Liste : LIST OF STRING := ['b', 'a', 'b'];\n"
    "  Mesures : LIST OF REAL := [12.0, 12.4, 13.1, 12.8, 13.5, 14.2, 13.9, 14.6];\n"
    "  Tab : ARRAY[0..3] OF INT := [4, 2, 4, 1];\n"
    "  i : DINT;\n"
    "  b : BOOL;\n"
    "  s : STRING;\n"
    "  x : REAL;\n"
    "END_VAR\n";

std::string run(const std::string& type, const std::string& body, std::string* err = nullptr) {
    Bench b(type, std::string(kDecl) + body);
    return b.run(err);
}

// Le resultat attendu (et pas d'erreur).
void same(const std::string& type, const std::string& body, const std::string& want, const std::string& what) {
    std::string err;
    const std::string got = run(type, body, &err);
    check(err.empty() && got == want, what + " : " + got + (err.empty() ? "" : " [" + err + "]") + ", attendu " + want);
}
// Une erreur qui dit `part`.
void fails(const std::string& body, const std::string& part, const std::string& what) {
    std::string err;
    (void)run("STRING", body, &err);
    check(!err.empty() && err.find(part) != std::string::npos, what + " : [" + err + "], attendu \xC2\xAB " + part + " \xC2\xBB");
}

const std::string kSetRefused = " : un SET se range tout seul (chaque valeur une fois, dans l'ordre) \xE2\x80\x94 SET_ADD et SET_REMOVE l'\xC3\xA9" "crivent";
const std::string kItemRefused = "un \xC3\xA9l\xC3\xA9ment d'un SET ne s'\xC3\xA9" "crit pas en place";

// ------------------------------------------------------------- les declarations ---
void declarations() {
    std::printf("-- les declarations et les litteraux\n");
    same("STRING", "R := JOIN(Postes, ',') + ' ' + TO_STRING(Postes.Count);", "P1,P3 2", "un SET litteral : range, sans doublon");
    same("STRING", "R := JOIN(Nombres, ',');", "1,3,5", "un SET OF INT : [5, 1, 3, 1] devient [1, 3, 5]");
    same("STRING", "R := JOIN(Petit, ',') + ' ' + TO_STRING(Petit.Count) + ' ' + TO_STRING(Petit.Capacity) + ' ' + TO_STRING(Petit.Full);",
         "3,4,5 3 3 TRUE", "un RING[3] litteral de 5 valeurs : les 3 dernieres");
    same("STRING", "R := TO_STRING(Dernieres.Count) + ' ' + TO_STRING(Dernieres.Capacity) + ' ' + TO_STRING(Dernieres.Full) + ' ' + TO_STRING(VideS.Count);",
         "0 5 FALSE 0", "un RING et un SET sans valeur : vides");
    same("STRING", "R := JOIN(SetIhm, ',') + ' ' + JOIN(RingIhm, ',');", "a,b 2,3,4", "les variables IHM SET et RING : leur valeur initiale mise en forme");
    same("STRING", "VAR CONSTANT N : DINT := 4; END_VAR\nVAR Rc : RING[N] OF INT := [1, 2, 3, 4, 5, 6]; END_VAR\nR := JOIN(Rc, ',');", "3,4,5,6",
         "RING[N] : une constante declaree plus haut");
    same("BOOL", "R := Postes = ['P3', 'P1'];", "TRUE", "un SET compare a un litteral (range comme lui)");
    fails("VAR Rx : RING[0] OF REAL; END_VAR", "RING[0] : la capacit\xC3\xA9 va de 1 \xC3\xA0 100000", "RING[0] refuse");
    fails("VAR Rx : RING[100001] OF REAL; END_VAR", "RING[100001] : la capacit\xC3\xA9 va de 1 \xC3\xA0 100000", "RING[100001] refuse");
    fails("VAR Rx : RING OF REAL; END_VAR", "RING : sa capacit\xC3\xA9 est attendue", "RING sans capacite refuse");
    fails("VAR Rx : RING[i] OF REAL; END_VAR", "RING[N] : N est une constante enti\xC3\xA8re ('i' n'est pas une constante", "RING[i] refuse");
}

// ----------------------------------------------------------------- les natives ---
void natives() {
    std::printf("-- SET_ADD, SET_REMOVE, SET_HAS, SET_UNION, SET_INTERSECTION, SET_DIFFERENCE, RING_ADD\n");
    same("STRING", "b := SET_ADD(Postes, 'P2');\nR := TO_STRING(b) + ' ' + JOIN(Postes, ',');", "TRUE P1,P2,P3", "SET_ADD d'une valeur nouvelle : TRUE, rangee");
    same("STRING", "b := SET_ADD(Postes, 'P3');\nR := TO_STRING(b) + ' ' + JOIN(Postes, ',');", "FALSE P1,P3", "SET_ADD d'une valeur deja la : FALSE, rien ne change");
    same("STRING", "b := SET_ADD(VideS, 2);\nR := TO_STRING(b) + ' ' + JOIN(VideS, ',');", "TRUE 2", "SET_ADD d'un entier dans un SET OF REAL vide");
    same("STRING", "b := SET_REMOVE(Postes, 'P1');\nR := TO_STRING(b) + ' ' + JOIN(Postes, ',');", "TRUE P3", "SET_REMOVE d'une valeur presente");
    same("STRING", "b := SET_REMOVE(Postes, 'P9');\nR := TO_STRING(b) + ' ' + JOIN(Postes, ',');", "FALSE P1,P3", "SET_REMOVE d'une valeur absente");
    same("STRING", "R := TO_STRING(SET_HAS(Postes, 'P1')) + TO_STRING(SET_HAS(Postes, 'P2')) + TO_STRING(SET_HAS(Nombres, 3));", "TRUEFALSETRUE",
         "SET_HAS");
    same("STRING", "R := JOIN(SET_UNION(Postes, Autre), ',');", "P1,P2,P3,P4", "SET_UNION (std::set_union)");
    same("STRING", "R := JOIN(SET_INTERSECTION(Postes, Autre), ',');", "P3", "SET_INTERSECTION");
    same("STRING", "R := JOIN(SET_DIFFERENCE(Postes, Autre), ',');", "P1", "SET_DIFFERENCE");
    same("STRING", "R := JOIN(SET_DIFFERENCE(Autre, Postes), ',') + ' ' + TO_STRING(SET_INTERSECTION(Postes, VideS).Count);", "P2,P4 0",
         "SET_DIFFERENCE dans l'autre sens ; l'intersection d'un SET vide");
    same("STRING", "R := JOIN(SET_UNION(Postes, Liste), ',');", "P1,P3,a,b", "SET_UNION d'un SET et d'une LIST (rangee, sans doublon)");
    same("STRING", "R := JOIN(SET_UNION(Tab, Nombres), ',');", "1,2,3,4,5", "SET_UNION d'un ARRAY et d'un SET");
    same("STRING", "VAR U : SET OF STRING; END_VAR\nU := SET_UNION(Postes, Autre);\nb := SET_ADD(U, 'P0');\nR := JOIN(U, ',');", "P0,P1,P2,P3,P4",
         "le SET rendu s'ecrit dans un SET");
    fails("b := SET_ADD(Nombres, 'abc');", "SET_ADD : 'abc' n'est pas un INT", "SET_ADD d'un texte dans un SET OF INT");
    fails("b := SET_ADD(Postes, 5);", "SET_ADD : 5 n'est pas une STRING", "SET_ADD d'un nombre dans un SET OF STRING");
    fails("b := SET_ADD(Liste, 'c');", "SET_ADD : un SET est attendu (SET OF T), pas une LIST", "SET_ADD sur une LIST");
    fails("b := SET_HAS(Tab, 1);", "SET_HAS : un SET est attendu (SET OF T), pas un ARRAY", "SET_HAS sur un ARRAY");
    fails("s := JOIN(SET_UNION(Postes, Nombres), ',');", "SET_UNION : 1 n'est pas une STRING", "SET_UNION de STRING et d'INT");
    fails("s := JOIN(SET_UNION(Postes, 3), ',');", "SET_UNION : un SET est attendu (ou une LIST, un ARRAY), pas un nombre", "SET_UNION d'un nombre");
    fails("b := SET_ADD(Postes);", "mauvais nombre d'arguments : SET_ADD(S, x)", "SET_ADD a un argument");
    // RING_ADD : la maquette (Dernieres : RING[5] OF REAL, 8 mesures)
    same("STRING", "FOR i := 0 TO 7 DO\n  RING_ADD(Dernieres, Mesures[i]);\nEND_FOR\nR := JOIN(Dernieres, ',') + ' ' + TO_STRING(Dernieres.Count) + ' ' + TO_STRING(Dernieres.Full);",
         "12.8,13.5,14.2,13.9,14.6 5 TRUE", "RING_ADD : 8 mesures dans un RING[5] - les 5 dernieres, de la plus ancienne a la plus recente");
    same("STRING", "i := RING_ADD(Dernieres, 1.5);\nR := TO_STRING(i) + ' ' + TO_STRING(Dernieres.Full);", "1 FALSE", "RING_ADD rend le nombre de valeurs");
    same("DINT", "i := RING_ADD(Petit, 6);\nR := RING_ADD(Petit, 7) * 100 + Petit[0] * 10 + Petit[2];", "357", "RING_ADD sur un RING plein : la plus ancienne part");
    fails("i := RING_ADD(Mesures, 1.0);", "RING_ADD : un RING est attendu (RING[N] OF T), pas une LIST \xE2\x80\x94 LIST_ADD ajoute \xC3\xA0 une liste", "RING_ADD sur une LIST");
    fails("i := RING_ADD(Petit, 'abc');", "RING_ADD : 'abc' n'est pas un INT", "RING_ADD d'un texte dans un RING OF INT");
    fails("i := RING_ADD(Petit, 2.5);", "RING_ADD : 2.5 n'est pas un entier (INT)", "RING_ADD d'un reel dans un RING OF INT");
}

// ------------------------------------------------------ les membres, FOR EACH, les LIST_ ---
void commeUneListe() {
    std::printf("-- les membres, FOR EACH, LIST_ADD et les natives LIST_ qui lisent\n");
    same("STRING", "FOR i := 0 TO 7 DO\n  LIST_ADD(Dernieres, Mesures[i]);\nEND_FOR\nR := JOIN(Dernieres, ',');", "12.8,13.5,14.2,13.9,14.6",
         "LIST_ADD sur un RING : comme RING_ADD");
    same("DINT", "i := LIST_ADD(Petit, 9);\nR := i * 10 + Petit[2];", "39", "LIST_ADD sur un RING plein : rend 3 (la capacite), 9 a la fin");
    same("STRING", "VECTOR_PUSH(Petit, 9);\nR := JOIN(Petit, ',');", "4,5,9", "VECTOR_PUSH sur un RING");
    same("STRING", "FOR i := 0 TO 7 DO\n  RING_ADD(Dernieres, Mesures[i]);\nEND_FOR\n"
                   "R := TO_STRING(LIST_MIN(Dernieres)) + ' ' + TO_STRING(LIST_MAX(Dernieres)) + ' ' + TO_STRING(LIST_SUM(Dernieres)) + ' '"
                   " + TO_STRING(LIST_CONTAINS(Dernieres, 12.0)) + ' ' + TO_STRING(LIST_GET(Dernieres, 0, -1.0)) + ' ' + TO_STRING(LIST_INDEX_OF(Dernieres, 14.6))"
                   " + ' ' + TO_STRING(LIST_FIRST(Dernieres)) + ' ' + TO_STRING(Dernieres.Last);",
         "12.8 14.6 69 FALSE 12.8 4 12.8 14.6", "LIST_MIN, LIST_MAX, LIST_SUM, LIST_CONTAINS, LIST_GET, LIST_INDEX_OF, LIST_FIRST, .Last sur un RING");
    same("REAL", "FOR i := 0 TO 4 DO\n  RING_ADD(Dernieres, TO_REAL(i));\nEND_FOR\nR := LIST_AVG(Dernieres);", "2", "LIST_AVG d'un RING (une moyenne glissante)");
    same("STRING", "FOR EACH x IN Petit DO\n  R := R + TO_STRING(x) + ';';\nEND_FOR", "3;4;5;", "FOR EACH d'un RING : de la plus ancienne a la plus recente");
    same("STRING", "RING_ADD(Petit, 6);\nFOR EACH i, x IN Petit DO\n  R := R + TO_STRING(i) + '=' + TO_STRING(x) + ';';\nEND_FOR", "0=4;1=5;2=6;",
         "FOR EACH i, x d'un RING : l'indice 0 est la plus ancienne");
    same("STRING", "SET_ADD(Postes, 'P0');\nFOR EACH s IN Postes DO\n  R := R + s + ';';\nEND_FOR", "P0;P1;P3;", "FOR EACH d'un SET : dans l'ordre");
    same("STRING", "i := LIST_ADD(Postes, 'P2');\ni := LIST_ADD(Postes, 'P2') * 10 + i;\nR := TO_STRING(i) + ' ' + JOIN(Postes, ',');", "33 P1,P2,P3",
         "LIST_ADD sur un SET : la valeur a sa place, une seule fois ; rend le nombre");
    same("STRING", "R := TO_STRING(LIST_CONTAINS(Postes, 'P3')) + ' ' + TO_STRING(LIST_INDEX_OF(Postes, 'P3')) + ' ' + Postes[0] + ' ' + Postes.Last"
                   " + ' ' + TO_STRING(LIST_COUNT_IF(Nombres, COMPARAISON#Superieur, 2)) + ' ' + TO_STRING(LIST_IS_SORTED(Nombres));",
         "TRUE 1 P1 P3 2 TRUE", "les natives LIST_ qui lisent un SET, S[0], S.Last");
    same("STRING", "b := LIST_REMOVE(Postes, 'P1');\ni := LIST_REMOVE_ALL(Nombres, 3);\nR := JOIN(Postes, ',') + ' ' + JOIN(Nombres, ',') + ' ' + LIST_POP_FIRST(Autre);",
         "P3 1,5 P2", "LIST_REMOVE, LIST_REMOVE_ALL, LIST_POP_FIRST sur un SET");
    same("STRING", "i := LIST_APPEND(Postes, Autre);\nR := TO_STRING(i) + ' ' + JOIN(Postes, ',');", "4 P1,P2,P3,P4", "LIST_APPEND sur un SET : l'union");
    same("STRING", "i := LIST_APPEND(Petit, Tab);\nR := TO_STRING(i) + ' ' + JOIN(Petit, ',');", "3 2,4,1", "LIST_APPEND sur un RING : les 3 dernieres");
    same("STRING", "i := LIST_INSERT(Petit, 0, 9);\nR := TO_STRING(i) + ' ' + JOIN(Petit, ',');", "3 3,4,5", "LIST_INSERT en tete d'un RING plein : 9 part aussitot");
    same("STRING", "LIST_SORT(Petit, TRUE);\nLIST_SET(Petit, 0, 7);\nR := JOIN(Petit, ',');", "7,4,3", "un RING se trie et s'ecrit a une place");
    same("STRING", "i := LIST_INSERT_SORTED(Petit, 4);\nR := TO_STRING(i) + ' ' + JOIN(Petit, ',');", "1 4,4,5", "LIST_INSERT_SORTED dans un RING plein (3,4,5 + 4 : 3 part, le 4 ajoute est a l'indice 1)");
    same("STRING", "R := JOIN(PARTIAL_SUM(Nombres), ',') + ' ' + JOIN(LIST_SLICE(Nombres, 1, 2), ',') + ' ' + JOIN(TO_LIST(Postes), ',');", "1,4,9 3,5 P1,P3",
         "PARTIAL_SUM, LIST_SLICE, TO_LIST d'un SET : des LIST");
    fails("i := Postes.Capacity;", "'Capacity' : un SET OF STRING n'a pas ce membre", "S.Capacity : un SET n'a pas de capacite");
    fails("b := Liste.Full;", "'Full' : un LIST OF STRING n'a pas ce membre", "L.Full : une LIST n'est jamais pleine");
    fails("VECTOR_RESIZE(Petit, 4);", "VECTOR_RESIZE : un RING[3] OF INT garde au plus 3 valeurs", "VECTOR_RESIZE au-dela de la capacite");
    same("STRING", "VECTOR_RESIZE(Petit, 1);\nR := JOIN(Petit, ',');", "3", "VECTOR_RESIZE en dessous de la capacite");
}

// --------------------------------------------------------- ce qu'un SET refuse ---
void refus() {
    std::printf("-- un SET se range tout seul : ce qu'il refuse\n");
    fails("Postes[0] := 'Z';", kItemRefused, "S[0] := 'Z' refuse");
    fails("FOR EACH s IN Postes DO\n  s := 'Z';\nEND_FOR", kItemRefused, "FOR EACH s IN S : s := 'Z' refuse");
    fails("Postes.First := 'Z';", kItemRefused, "S.First := 'Z' refuse");
    fails("VAR p : POINTER TO STRING; END_VAR\np := ADR(Postes[0]);", "ADR : " + kItemRefused, "ADR(S[0]) refuse");
    fails("FUNCTION Changer(VAR_IN_OUT t : STRING) : BOOL\n  t := 'Z';\n  Changer := TRUE;\nEND_FUNCTION\nb := Changer(Postes[0]);", kItemRefused,
          "S[0] passe a un VAR_IN_OUT refuse");
    same("STRING", "FUNCTION Lire(t : STRING) : STRING\n  Lire := t + '!';\nEND_FUNCTION\nR := Lire(Postes[0]);", "P1!", "S[0] passe par valeur : lu");
    for (const char* call : {"LIST_INSERT(Postes, 0, 'Z')", "VECTOR_INSERT(Postes, 0, 'Z')", "LIST_SORT(Postes)", "LIST_REVERSE(Postes)",
                             "VECTOR_RESIZE(Postes, 5)", "LIST_SET(Postes, 0, 'Z')", "LIST_FILL(Postes, 'Z')", "LIST_INSERT_SORTED(Postes, 'Z')",
                             "LIST_SWAP(Postes, 0, 1)", "LIST_MOVE(Postes, 0, 1)", "LIST_ROTATE(Postes, 1)", "LIST_COPY(Liste, Postes)", "IOTA(Nombres, 1)"}) {
        const std::string c(call);
        const std::string name = c.substr(0, c.find('('));
        fails(c + ";", name + kSetRefused, c + " sur un SET");
    }
    fails("VAR Lots : SET OF TUPLE(STRING, REAL); END_VAR", "un SET range des valeurs simples", "SET OF TUPLE refuse");
}

// ----------------------------------------------------------- les affectations ---
void affectations() {
    std::printf("-- les affectations entre LIST, SET et RING\n");
    same("STRING", "Postes := Liste;\nR := JOIN(Postes, ',') + ' ' + JOIN(Liste, ',');", "a,b b,a,b", "une LIST dans un SET : rangee, sans doublon (la LIST ne change pas)");
    same("STRING", "Liste := Postes;\nb := LIST_ADD(Liste, 'P1') > 0;\nR := JOIN(Liste, ',');", "P1,P3,P1", "un SET dans une LIST : une LIST ordinaire (les doublons reviennent)");
    same("STRING", "Petit := Tab;\nR := JOIN(Petit, ',');", "2,4,1", "un ARRAY dans un RING[3] : les 3 dernieres");
    same("STRING", "VAR Grand : RING[10] OF INT; END_VAR\nGrand := Petit;\nRING_ADD(Grand, 6);\nPetit := Grand;\nR := JOIN(Grand, ',') + ' ' + JOIN(Petit, ',');",
         "3,4,5,6 4,5,6", "d'un RING a l'autre : la capacite de la destination");
    same("STRING", "Postes := ['Z', 'A', 'Z'];\nR := JOIN(Postes, ',');", "A,Z", "un litteral affecte a un SET");
    same("STRING", "Postes := Postes + ['A', 'P1'];\nR := JOIN(Postes, ',');", "A,P1,P3", "S + [x] reste un SET");
    same("STRING", "VAR Copie : SET OF STRING; END_VAR\nCopie := Postes;\nSET_ADD(Copie, 'X');\nR := JOIN(Postes, ',') + ' ' + JOIN(Copie, ',');", "P1,P3 P1,P3,X",
         "un SET copie : deux SET");
}

// ---------------------------------------------------------------- les constantes ---
void constantes() {
    std::printf("-- les constantes\n");
    const std::string decl = "VAR CONSTANT\n  Cs : SET OF STRING := ['b', 'a'];\n  Cr : RING[2] OF INT := [1, 2, 3];\nEND_VAR\n";
    for (const char* call : {"SET_ADD(Cs, 'c')", "SET_REMOVE(Cs, 'a')", "RING_ADD(Cr, 4)", "LIST_ADD(Cs, 'c')", "LIST_ADD(Cr, 4)", "LIST_CLEAR(Cs)"}) {
        const std::string c(call);
        const std::string name = c.substr(0, c.find('('));
        fails(decl + c + ";", name + " : une constante ne s'\xC3\xA9" "crit pas", c + " sur une constante");
    }
    same("STRING", decl + "R := TO_STRING(SET_HAS(Cs, 'a')) + ' ' + JOIN(Cs, ',') + ' ' + JOIN(Cr, ',') + ' ' + TO_STRING(Cr.Full);", "TRUE a,b 2,3 TRUE",
         "une constante SET, RING : se lit");
}

// --------------------------------------------------------- les variables de l'IHM ---
void variablesIhm() {
    std::printf("-- les variables IHM de type SET et RING\n");
    same("STRING", "b := SET_ADD(SetIhm, 'c');\nb := SET_ADD(SetIhm, 'a');\nRING_ADD(RingIhm, 5);\nR := JOIN(SetIhm, ',') + ' ' + JOIN(RingIhm, ',') + ' ' + TO_STRING(RingIhm.Full);",
         "a,b,c 3,4,5 TRUE", "SET_ADD et RING_ADD sur des variables IHM (ecrites en place)");
    same("STRING", "SetIhm := ['z', 'y', 'z'];\nR := JOIN(SetIhm, ',');", "y,z", "un litteral dans une variable IHM SET");
    Bench b("STRING", "SET_ADD(SetIhm, 'c');\nRING_ADD(RingIhm, 9);");
    std::string err;
    (void)b.run(&err);
    sim::Value v;
    const bool count = b.rt.environment().read("SetIhm.Count", v);
    (void)count;
    auto o = b.rt.environment().richVariable("SetIhm");
    auto r = b.rt.environment().richVariable("RingIhm");
    check(err.empty() && o && o->items.size() == 3 && r && r->items.size() == 3 && r->type && sim::isRingType(*r->type) && r->type->capacity == 3,
          "les variables IHM gardent leur forme (SET de 3, RING[3] de 3) " + err);
    std::string text;
    check(o && sim::literalText(*o, text) && text == "['a', 'b', 'c']", "le texte d'un SET IHM : ['a', 'b', 'c'] (" + text + ")");
    check(r && sim::literalText(*r, text) && text == "[3, 4, 9]", "le texte d'un RING IHM : [3, 4, 9] (" + text + ")");
    // Compiler sur le projet entier : des variables IHM SET et RING, un script qui les ecrit et les lit.
    b.p.programs.scripts.back().body = "VAR\n  Loc : SET OF INT := [3, 1];\n  Anneau : RING[4] OF REAL;\n  b : BOOL;\nEND_VAR\n"
                                       "SET_ADD(SetIhm, 'c');\nRING_ADD(RingIhm, 9);\nRING_ADD(Anneau, 1.5);\nb := SET_ADD(Loc, 2);\n"
                                       "R := JOIN(SetIhm, ',') + TO_STRING(RingIhm.Count) + TO_STRING(RingIhm.Full) + JOIN(Loc, ',')"
                                       " + TO_STRING(Anneau.Capacity);";
    std::string errors;
    for (const auto& i : compileWith(b.p, [](std::string_view) { return false; }))
        if (i.severity == Issue::Severity::Error) errors += " | " + i.message;
    check(errors.empty(), "Compiler : un projet avec des variables IHM SET et RING, sans erreur" + errors);
    check(types::isDynamic("SET OF STRING") && types::isDynamic("RING[3] OF INT") && !types::unbindableReason("RING[3] OF INT").empty(),
          "une variable IHM SET ou RING : de taille variable, sans equipement ni adresse (" + types::unbindableReason("RING[3] OF INT") + ")");
}

// ------------------------------------------------- ce qui tournait en 1.12.6 tourne pareil ---
void avant() {
    std::printf("-- la 1.12.6 : une variable nommee Set ou Ring, une fonction du projet nommee SET_ADD\n");
    same("STRING", "VAR Set : INT := 3; Ring : REAL := 1.5; END_VAR\nSet := Set + 1;\nR := TO_STRING(Set * 2) + ' ' + TO_STRING(Ring);", "8 1.5",
         "des variables nommees Set et Ring");
    Project p;
    Variable r;
    r.id = p.allocate();
    r.name = "R";
    r.type = "DINT";
    p.programs.variables.push_back(r);
    HmiFunction f;
    f.id = p.allocate();
    f.name = "SET_ADD";
    f.returnType = "DINT";
    f.body = "VAR_INPUT a : DINT; b : DINT; END_VAR\nSET_ADD := a + b;";
    p.programs.functions.push_back(f);
    View v = makeView(p, "Vue");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = "R := SET_ADD(120, 84);";
    p.programs.scripts.push_back(s);
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string err;
    (void)rt.callScript("Essai", 0.2, &err);
    sim::Value out;
    check(rt.environment().read("R", out) && out.asInteger() == 204, "SET_ADD du projet (a + b = 204), pas la native (" + shown(out) + ") " + err);
}

// ------------------------------------------------------------------------- Compiler ---
bool says(const std::vector<scriptcheck::Finding>& fs, const std::string& part, scriptcheck::Finding::Severity sev) {
    for (const auto& f : fs)
        if (f.severity == sev && f.message.find(part) != std::string::npos) return true;
    return false;
}
std::string all(const std::vector<scriptcheck::Finding>& fs) {
    std::string s;
    for (const auto& f : fs) s += std::string(" | ") + (f.severity == scriptcheck::Finding::Severity::Error ? "E " : "A ") + f.message;
    return s;
}
std::vector<scriptcheck::Finding> walk(const std::string& body) {
    Project p;
    scriptcheck::Scope sc;
    sc.project = &p;
    return scriptcheck::check(sc, std::string(kDecl) + body);
}
std::string diagnostics(const std::string& body) {
    std::string s;
    for (const auto& d : checkScript(ScriptLang::ST, std::string(kDecl) + body, "Essai"))
        if (d.severity == ScriptDiagnostic::Severity::Error) s += " | " + d.message;
    return s;
}
void compiler() {
    std::printf("-- Compiler\n");
    using S = scriptcheck::Finding::Severity;
    {
        const auto fs = walk("b := SET_ADD(Postes, 'P2');\nb := SET_REMOVE(Postes, 'P1') AND SET_HAS(Nombres, 3);\nPostes := SET_UNION(Postes, Autre);\n"
                             "i := RING_ADD(Dernieres, 1.5) + LIST_ADD(Petit, 4) + Petit.Capacity + Postes.Count;\nb := Dernieres.Full;\n"
                             "x := LIST_AVG(Dernieres);\nFOR EACH s IN Postes DO\n  i := i + 1;\nEND_FOR");
        bool error = false;
        for (const auto& f : fs) error = error || f.severity == S::Error;
        check(!error, "un script qui emploie SET et RING se lit sans erreur" + all(fs));
        check(diagnostics("b := SET_ADD(Postes, 'P2');\ni := RING_ADD(Dernieres, 1.5);").empty(), "les declarations SET et RING se lisent (checkScript)");
    }
    {
        const auto fs = walk("b := SET_ADD(Postes, 'a', 'b');");
        check(says(fs, "SET_ADD prend 2 arguments, pas 3", S::Error), "SET_ADD a 3 arguments : une erreur" + all(fs));
    }
    {
        const auto fs = walk("b := SET_ADD(Liste, 'a');\ni := RING_ADD(Mesures, 1.0);\nb := SET_HAS(Dernieres, 1.0);");
        check(says(fs, "SET_ADD : un SET est attendu (SET OF T), pas une LIST OF STRING", S::Error)
                  && says(fs, "RING_ADD : un RING est attendu (RING[N] OF T), pas une LIST OF REAL", S::Error)
                  && says(fs, "SET_HAS : un SET est attendu (SET OF T), pas un RING[5] OF REAL", S::Error),
              "SET_ADD sur une LIST, RING_ADD sur une LIST, SET_HAS sur un RING : des erreurs" + all(fs));
    }
    {
        const auto fs = walk("LIST_INSERT(Postes, 0, 'Z');\nLIST_SORT(Nombres);\nb := LIST_SET(Postes, 0, 'Z');\nPostes[0] := 'Z';");
        check(says(fs, "LIST_INSERT" + kSetRefused, S::Error) && says(fs, "LIST_SORT" + kSetRefused, S::Error) && says(fs, "LIST_SET" + kSetRefused, S::Error)
                  && says(fs, kItemRefused, S::Error),
              "ce qu'un SET refuse : des erreurs" + all(fs));
    }
    {
        const auto fs = walk("b := SET_ADD(Nombres, 'abc');\ni := RING_ADD(Petit, 'x');\nb := SET_ADD(Postes, 5);");
        check(says(fs, "SET_ADD : 'abc' n'est pas un INT", S::Error) && says(fs, "RING_ADD : 'x' n'est pas un INT", S::Error)
                  && says(fs, "SET_ADD : 5 n'est pas une STRING", S::Error),
              "une valeur du mauvais type : une erreur" + all(fs));
    }
    {
        const auto fs = walk("VAR CONSTANT\n  Cs : SET OF STRING := ['a'];\nEND_VAR\nb := SET_ADD(Cs, 'b');\nb := SET_HAS(Cs, 'a');");
        check(says(fs, "Cs est une constante : SET_ADD ne peut pas la changer", S::Error) && !says(fs, "SET_HAS ne peut pas", S::Error),
              "SET_ADD sur une constante : une erreur" + all(fs));
    }
    {
        // La capacite d'un RING : une constante entiere de 1 a 100000 (le moteur le dit aussi au demarrage).
        const auto fs = walk("VAR\n  R0 : RING[0] OF REAL;\n  R1 : RING OF REAL;\n  R2 : RING[i] OF REAL;\nEND_VAR\n"
                             "VAR CONSTANT\n  N : DINT := 10;\nEND_VAR\nVAR\n  R3 : RING[N] OF REAL;\n  R4 : RING[100000] OF INT;\nEND_VAR\ni := 0;");
        check(says(fs, "RING[0] : la capacit\xC3\xA9 va de 1 \xC3\xA0 100000", S::Error) && says(fs, "RING : sa capacit\xC3\xA9 est attendue", S::Error)
                  && says(fs, "RING[N] : N est une constante enti\xC3\xA8re ('i' n'est pas une constante", S::Error) && !says(fs, "'N' n'est pas", S::Error)
                  && !says(fs, "RING[100000]", S::Error),
              "RING[0], RING sans capacite, RING[i] : des erreurs ; RING[N] (une constante), RING[100000] : bons" + all(fs));
    }
    {
        Project p;
        HmiType lot;
        lot.id = p.allocate();
        lot.name = "T_Lot";
        lot.members = {TypeMember{"Nom", "STRING"}, TypeMember{"Poids", "REAL"}};
        p.programs.types.push_back(lot);
        scriptcheck::Scope sc;
        sc.project = &p;
        const auto fs = scriptcheck::check(sc, "VAR\n  Lots : SET OF T_Lot;\n  Bons : SET OF STRING;\nEND_VAR\nBons := ['a'];");
        check(says(fs, "SET OF T_Lot : un SET range des valeurs simples", S::Error) && !says(fs, "SET OF STRING", S::Error),
              "SET OF T_Lot (une structure) : une erreur" + all(fs));
    }
    {
        // Un SET passe par valeur a un parametre LIST OF INT d'une fonction du projet : le moteur le copie
        // en LIST ; Compiler ne le refuse pas (une LIST, un SET, un RING de meme element s'echangent).
        Project p;
        Variable r;
        r.id = p.allocate();
        r.name = "R";
        r.type = "DINT";
        p.programs.variables.push_back(r);
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Somme";
        f.returnType = "DINT";
        f.body = "VAR_INPUT L : LIST OF INT; END_VAR\nSomme := LIST_SUM(L) * 10 + LIST_COUNT(L);";
        p.programs.functions.push_back(f);
        scriptcheck::Scope sc;
        sc.project = &p;
        const auto fs = scriptcheck::check(sc, std::string(kDecl) + "R := Somme(Nombres);");
        bool error = false;
        for (const auto& x : fs) error = error || x.severity == S::Error;
        check(!error, "un SET OF INT passe a un parametre LIST OF INT : pas d'erreur" + all(fs));
        View v = makeView(p, "Vue");
        p.views.push_back(v);
        p.config.startView = p.views.back().id;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = std::string(kDecl) + "R := Somme(Nombres);";
        p.programs.scripts.push_back(s);
        Runtime rt;
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        std::string err;
        (void)rt.callScript("Essai", 0.2, &err);
        sim::Value out;
        check(rt.environment().read("R", out) && out.asInteger() == 93, "Somme(Nombres) : 1 + 3 + 5 = 9, 3 valeurs (" + shown(out) + ") " + err);
        check(typereg::conversion("SET OF INT", "LIST OF INT").kind == typereg::Conversion::Widening
                  && typereg::conversion("LIST OF INT", "SET OF INT").kind == typereg::Conversion::Lossy
                  && typereg::conversion("LIST OF REAL", "RING[5] OF REAL").kind == typereg::Conversion::Lossy
                  && typereg::conversion("SET OF INT", "LIST OF REAL").kind == typereg::Conversion::Forbidden
                  && typereg::conversion("LIST OF INT", "VECTOR OF INT").kind == typereg::Conversion::Forbidden,
              "le registre : SET et RING s'echangent avec une LIST de meme element (une LIST et un VECTOR : comme avant)");
    }
    {
        // Le registre des types : une variable IHM SET ou RING (de taille variable : dans la memoire de l'IHM).
        const auto& reg = typereg::baseRegistry();
        const auto a = reg.resolve("SET OF STRING", typereg::UseVariable);
        const auto b = reg.resolve("RING[100] OF REAL", typereg::UseVariable);
        const auto c = reg.resolve("RING[0] OF REAL", typereg::UseVariable);
        const auto d = reg.resolve("RING OF REAL", typereg::UseVariable);
        check(a.ok && a.dynamic && a.rich && a.text == "SET OF STRING", "le registre : SET OF STRING (" + a.why + ")");
        check(b.ok && b.dynamic && b.rich && b.text == "RING[100] OF REAL", "le registre : RING[100] OF REAL (" + b.why + ")");
        check(!c.ok && c.why.find("RING[0]") != std::string::npos, "le registre : RING[0] refuse (" + c.why + ")");
        check(!d.ok && d.why.find("RING[100] OF REAL") != std::string::npos, "le registre : RING sans capacite refuse (" + d.why + ")");
    }
}

} // namespace

int main() {
    std::printf("1.12.7 : SET OF T et RING[N] OF T\n");
    declarations();
    natives();
    commeUneListe();
    refus();
    affectations();
    constantes();
    variablesIhm();
    avant();
    compiler();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
