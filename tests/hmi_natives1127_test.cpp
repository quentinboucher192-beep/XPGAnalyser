// =============================================================================
//  tests/hmi_natives1127_test.cpp - 1.12.7 : LES NATIVES DES CONTENEURS, DE
//  std::numeric, LES TEMPLATES ET <bit>
// -----------------------------------------------------------------------------
//  Chaque native de la 1.12.7 dans le moteur de l'IHM (un script ST) : le cas
//  ordinaire, le bord (une liste vide, un indice hors de la liste, une borne), la
//  faute dite comme la maquette ; les 10 natives LIST_ qui lisent aussi un ARRAY
//  (a ses indices, un tableau IHM ecrit en place) ; les templates (NUMERIC_LIMITS<T>,
//  NUMBERS[<T>], SATURATE_CAST<T>, ADD_SAT<T>...) et leurs fautes ; les enumerations
//  COMPARAISON, OPERATION, TRANSFORMATION ; ce qui tournait en 1.12.6 tourne pareil
//  (a < b, une fonction du projet nommee comme une native). Puis Compiler : les
//  fautes des nouvelles fonctions sont des erreurs, celles du code d'avant des
//  avertissements.
//
//  NATIVES_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNatives.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/sim/DialectLib.hpp"
#include "../src/sim/Interpreter.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("NATIVES_OBS"); return d && *d == '1'; }();

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

// Le projet d'essai : R (le resultat), des variables IHM simples, un tableau et une liste IHM.
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
        var("Niveau", "REAL", "42.5");
        var("Compteur", "INT", "7");
        var("Mot", "WORD", "16#0012");
        var("TabIhm", "ARRAY[1..5] OF INT", "[4, 9, 1, 9, 6]");
        var("ListeIhm", "LIST OF REAL", "[3.0, 1.0, 2.0]");
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

// Les variables d'essai de la maquette (v3_cont.js : cvarsInit).
const char* kDecl =
    "VAR\n"
    "  Mesures : LIST OF REAL := [12.5, 8.0, 15.2, 8.0, 3.1];\n"
    "  Autres : LIST OF REAL := [1.0, 2.0];\n"
    "  Triee : LIST OF REAL := [1.0, 3.0, 4.5, 7.0];\n"
    "  Pile : VECTOR OF STRING := ['a', 'b', 'c'];\n"
    "  Tab : ARRAY[0..4] OF INT := [4, 9, 1, 9, 6];\n"
    "  Stock : MAP[STRING] OF REAL := ['ecrou' := 80.0, 'goujon' := 45.0, 'vis' := 120.0];\n"
    "  Livraison : MAP[STRING] OF REAL := ['clou' := 500.0, 'vis' := 60.0];\n"
    "  Paliers : MAP[INT] OF REAL := [0 := 1.0, 100 := 1.2, 500 := 1.5];\n"
    "  Point : TUPLE(STRING, REAL) := ('four', 850.0);\n"
    "  Noms : LIST OF TUPLE(STRING, REAL);\n"
    "  Nom : STRING;\n"
    "  Valeur : REAL;\n"
    "  Vide : LIST OF REAL;\n"
    "  i : DINT;\n"
    "  x : REAL;\n"
    "  s : STRING;\n"
    "  b : BOOL;\n"
    "  w : WORD := 16#0012;\n"
    "  si : SINT := 100;\n"
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

// ------------------------------------------------------------------- les constantes ---
// Chaque native qui ecrit son premier argument (ou une variable) refuse une constante : « NOM : une
// constante ne s'écrit pas » - MAP_REMOVE et MAP_CLEAR aussi (avant : la MAP constante etait changee).
// Les natives qui lisent la lisent.
void constantes() {
    std::printf("-- les constantes : chaque native qui ecrit les refuse\n");
    const std::string decl =
        "VAR CONSTANT\n"
        "  Cm : MAP[STRING] OF INT := ['a' := 1, 'b' := 1];\n"
        "  Cl : LIST OF REAL := [2.0, 1.0, 2.0];\n"
        "  Ct : TUPLE(STRING, REAL) := ('x', 1.0);\n"
        "  Ca : ARRAY[0..2] OF INT := [3, 1, 2];\n"
        "  Cs : STRING := 'k';\n"
        "  Cr : REAL := 1.0;\n"
        "END_VAR\n";
    for (const char* call : {"MAP_REMOVE(Cm, 'a')", "MAP_CLEAR(Cm)", "MAP_SET(Cm, 'c', 2)", "MAP_INSERT(Cm, 'c', 2)", "MAP_INCREMENT(Cm, 'a')",
                             "MAP_REMOVE_IF(Cm, COMPARAISON#Egal, 1)", "MAP_MERGE(Cm, Cm)", "LIST_SET(Cl, 0, 5.0)", "LIST_FILL(Cl, 0.0)",
                             "LIST_ROTATE(Cl, 1)", "LIST_MOVE(Cl, 0, 1)", "LIST_SWAP(Cl, 0, 1)", "LIST_REMOVE_IF(Cl, COMPARAISON#Egal, 1.0)",
                             "LIST_REMOVE_ALL(Cl, 1.0)", "LIST_DISTINCT(Cl)", "LIST_INSERT_SORTED(Cl, 1.5)", "LIST_POP_FIRST(Cl)",
                             "LIST_APPEND(Cl, Mesures)", "IOTA(Cl, 1.0)", "LIST_COPY(Mesures, Ca)", "LIST_SORT(Ca)", "LIST_REVERSE(Ca)",
                             "LIST_ADD(Cl, 1.0)", "TUPLE_SET(Ct, 1, 'y')", "TUPLE_UNPACK(Point, Cs, Cr)"}) {
        const std::string c(call);
        const std::string name = c.substr(0, c.find('('));
        fails(decl + c + ";", name + " : une constante ne s'\xC3\xA9" "crit pas", c + " sur une constante");
    }
    same("STRING", decl + "R := TO_STRING(MAP_GET(Cm, 'a')) + ' ' + TO_STRING(LIST_SUM(Cl)) + ' ' + TO_STRING(TUPLE_GET(Ct, 2)) + ' ' "
                          "+ TO_STRING(LIST_COUNT_IF(Ca, COMPARAISON#Superieur, 1)) + ' ' + JOIN(LIST_SLICE(Cl, 0, 2), ';');",
         "1 5 1 2 2;1", "les natives qui lisent lisent une constante");
}

// ---------------------------------------------------------------- les conteneurs ---
void lireEtEcrire() {
    std::printf("-- lire et ecrire : LIST_SET, MAP_SET, TUPLE_GET, TUPLE_SET, MAP_FIRST_KEY, MAP_LAST_KEY, TUPLE_SIZE\n");
    same("BOOL", "R := LIST_SET(Mesures, 1, 9.5) AND Mesures[1] = 9.5;", "TRUE", "LIST_SET ecrit l'indice 1");
    same("STRING", "b := LIST_SET(Mesures, 9, 9.5);\nR := TO_STRING(b) + ' ' + JOIN(Mesures, ';');", "FALSE 12.5;8;15.2;8;3.1",
         "LIST_SET hors de la liste : FALSE, rien n'est ecrit");
    same("STRING", "LIST_SET(Tab, 0, 7);\nR := JOIN(Tab, ',');", "7,9,1,9,6", "LIST_SET sur un ARRAY[0..4] (a ses indices)");
    same("STRING", "b := LIST_SET(TabIhm, 1, 70);\nR := TO_STRING(TabIhm[1]);", "70", "LIST_SET sur un tableau IHM ARRAY[1..5] : ecrit en place, indice 1");
    fails("LIST_SET(Pile, 0, 5);", "LIST_SET : 5 n'est pas une STRING", "LIST_SET : un nombre dans une VECTOR OF STRING");
    same("STRING", "b := MAP_SET(Stock, 'rondelle', 12.0);\nR := TO_STRING(b) + ' ' + JOIN(MAP_KEYS(Stock), ',');", "TRUE ecrou,goujon,rondelle,vis",
         "MAP_SET : une cle nouvelle (TRUE), les cles restent rangees");
    same("BOOL", "R := MAP_SET(Stock, 'vis', 100.0) OR Stock['vis'] <> 100.0;", "FALSE", "MAP_SET : une cle deja la (FALSE), sa valeur ecrite");
    fails("MAP_SET(Stock, 'vis', 'abc');", "MAP_SET : 'abc' n'est pas un REAL", "MAP_SET : un texte dans une MAP de REAL");
    same("REAL", "R := TUPLE_GET(Point, 2);", "850", "TUPLE_GET(Point, 2)");
    same("STRING", "R := TUPLE_GET(Point, 1);", "four", "TUPLE_GET(Point, 1)");
    fails("x := TUPLE_GET(Point, 3);", "TUPLE_GET : TUPLE(STRING, REAL) a 2 champs, pas 3", "TUPLE_GET hors des champs");
    same("STRING", "TUPLE_SET(Point, 1, 'etuve');\nR := Point.Item1;", "etuve", "TUPLE_SET(Point, 1, 'etuve')");
    fails("TUPLE_SET(Point, 2, 'abc');", "TUPLE_SET : 'abc' n'est pas un REAL", "TUPLE_SET : un texte dans un champ REAL");
    same("STRING", "R := MAP_FIRST_KEY(Stock) + ' ' + MAP_LAST_KEY(Stock);", "ecrou vis", "MAP_FIRST_KEY, MAP_LAST_KEY");
    same("INT", "R := MAP_LAST_KEY(Paliers);", "500", "MAP_LAST_KEY d'une MAP[INT]");
    same("STRING", "MAP_CLEAR(Stock);\nR := MAP_FIRST_KEY(Stock, '?');", "?", "MAP_FIRST_KEY d'une MAP vide, avec un defaut");
    fails("MAP_CLEAR(Stock);\ns := MAP_FIRST_KEY(Stock);", "MAP_FIRST_KEY : la MAP est vide", "MAP_FIRST_KEY d'une MAP vide, sans defaut");
    same("DINT", "R := TUPLE_SIZE(Point);", "2", "TUPLE_SIZE");
    fails("i := TUPLE_SIZE(Mesures);", "TUPLE_SIZE : un TUPLE est attendu, pas une LIST", "TUPLE_SIZE d'une liste");
    // MAP_GET : sans defaut, une cle absente s'arrete (la fiche corrigee le dit)
    same("REAL", "R := MAP_GET(Stock, 'vis');", "120", "MAP_GET(m, cle) : une cle presente");
    fails("x := MAP_GET(Stock, 'clou');", "MAP_GET : cl\xC3\xA9 absente : 'clou'", "MAP_GET(m, cle) : une cle absente");
    fails("x := Stock['clou'];", "cl\xC3\xA9 absente de la MAP", "Stock['clou'] en lecture : la cle n'est pas creee, le script s'arrete");
}

void ajouterEtRetirer() {
    std::printf("-- ajouter et retirer\n");
    same("BOOL", "R := MAP_INSERT(Stock, 'rondelle', 0.0);", "TRUE", "MAP_INSERT d'une cle nouvelle");
    same("REAL", "b := MAP_INSERT(Stock, 'vis', 0.0);\nR := Stock['vis'];", "120", "MAP_INSERT n'ecrase pas");
    same("STRING", "i := LIST_INSERT_SORTED(Triee, 4.5);\nR := TO_STRING(i) + ' ' + JOIN(Triee, ';');", "3 1;3;4.5;4.5;7",
         "LIST_INSERT_SORTED : apres ses egales");
    same("DINT", "R := LIST_INSERT_SORTED(Triee, 0.5);", "0", "LIST_INSERT_SORTED en tete");
    same("DINT", "LIST_SORT(Triee, TRUE);\nR := LIST_INSERT_SORTED(Triee, 2.0, TRUE);", "3", "LIST_INSERT_SORTED decroissant (7, 4.5, 3, puis 2)");
    fails("i := LIST_INSERT_SORTED(Tab, 3);", "LIST_INSERT_SORTED : un ARRAY a une taille fixe (5 cases) \xE2\x80\x94 une LIST ou un VECTOR est attendu",
          "LIST_INSERT_SORTED sur un ARRAY");
    same("STRING", "R := LIST_POP_FIRST(Pile) + JOIN(Pile, '');", "abc", "LIST_POP_FIRST rend le premier et le retire");
    fails("x := LIST_POP_FIRST(Vide);", "LIST_POP_FIRST : la liste est vide", "LIST_POP_FIRST d'une liste vide");
    same("STRING", "i := LIST_REMOVE_ALL(Mesures, 8.0);\nR := TO_STRING(i) + ' ' + JOIN(Mesures, ';');", "2 12.5;15.2;3.1", "LIST_REMOVE_ALL");
    fails("i := LIST_REMOVE_ALL(Tab, 9);", "LIST_REMOVE_ALL : un ARRAY a une taille fixe (5 cases)", "LIST_REMOVE_ALL sur un ARRAY");
    same("STRING", "i := LIST_REMOVE_IF(Mesures, COMPARAISON#Inferieur, 9.0);\nR := TO_STRING(i) + ' ' + JOIN(Mesures, ';');", "3 12.5;15.2",
         "LIST_REMOVE_IF < 9");
    same("DINT", "R := MAP_REMOVE_IF(Stock, COMPARAISON#Inferieur, 100.0);", "2", "MAP_REMOVE_IF < 100");
    same("STRING", "MAP_REMOVE_IF(Stock, COMPARAISON#Inferieur, 100.0);\nR := JOIN(MAP_KEYS(Stock), ',');", "vis", "MAP_REMOVE_IF : la cle qui reste");
    same("STRING", "i := LIST_DISTINCT(Mesures);\nR := TO_STRING(i) + ' ' + JOIN(Mesures, ';');", "1 12.5;8;15.2;3.1", "LIST_DISTINCT garde la premiere");
    same("REAL", "R := MAP_INCREMENT(Stock, 'clou');", "1", "MAP_INCREMENT d'une cle absente : 0 + 1");
    same("REAL", "R := MAP_INCREMENT(Stock, 'vis', 10.0);", "130", "MAP_INCREMENT de 10");
    fails("MAP_INCREMENT(Stock, 'vis', 'x');", "MAP_INCREMENT : 'x' n'est pas un REAL", "MAP_INCREMENT d'un texte");
    same("STRING", "i := LIST_APPEND(Mesures, Autres);\nR := TO_STRING(i) + ' ' + JOIN(Mesures, ';');", "7 12.5;8;15.2;8;3.1;1;2", "LIST_APPEND d'une liste");
    same("DINT", "R := LIST_APPEND(Mesures, Tab);", "10", "LIST_APPEND d'un ARRAY (des INT dans des REAL)");
    same("DINT", "R := LIST_APPEND(Autres, Autres);", "4", "LIST_APPEND d'elle-meme");
    same("STRING", "i := MAP_MERGE(Stock, Livraison);\nR := TO_STRING(i) + ' ' + TO_STRING(Stock['vis']) + ' ' + TO_STRING(Stock['clou']);",
         "1 120 500", "MAP_MERGE sans remplacer");
    same("REAL", "MAP_MERGE(Stock, Livraison, TRUE);\nR := Stock['vis'];", "60", "MAP_MERGE en remplacant");
    fails("i := MAP_MERGE(Stock, Paliers);", "MAP_MERGE : MAP[INT] OF REAL n'est pas un MAP[STRING] OF REAL", "MAP_MERGE de deux MAP de types differents");
    same("STRING", "i := LIST_FILL(Tab, 0);\nR := TO_STRING(i) + ' ' + JOIN(Tab, ',');", "5 0,0,0,0,0", "LIST_FILL d'un ARRAY");
    same("STRING", "LIST_FILL(TabIhm, 3);\nR := JOIN(TabIhm, ',');", "3,3,3,3,3", "LIST_FILL d'un tableau IHM (ecrit en place)");
    same("STRING", "LIST_FILL(Mesures, 1.0);\nR := JOIN(Mesures, ',');", "1,1,1,1,1", "LIST_FILL d'une liste");
    fails("LIST_ADD(Tab, 3);", "LIST_ADD : un ARRAY a une taille fixe (5 cases) \xE2\x80\x94 une LIST ou un VECTOR est attendu", "LIST_ADD sur un ARRAY");
}

void chercher() {
    std::printf("-- chercher\n");
    same("BOOL", "R := LIST_CONTAINS(Tab, 1);", "TRUE", "LIST_CONTAINS dans un ARRAY");
    same("DINT", "R := LIST_INDEX_OF(Tab, 9);", "1", "LIST_INDEX_OF dans un ARRAY[0..4]");
    same("DINT", "R := LIST_INDEX_OF(TabIhm, 9);", "2", "LIST_INDEX_OF dans un tableau IHM ARRAY[1..5] (a ses indices)");
    same("DINT", "R := LIST_LAST_INDEX_OF(Mesures, 8.0);", "3", "LIST_LAST_INDEX_OF");
    same("DINT", "R := LIST_LAST_INDEX_OF(TabIhm, 9);", "4", "LIST_LAST_INDEX_OF d'un ARRAY[1..5]");
    same("DINT", "R := LIST_LAST_INDEX_OF(Mesures, 99.0);", "-1", "LIST_LAST_INDEX_OF absente");
    same("STRING", "R := MAP_KEY_OF(Stock, 45.0, '');", "goujon", "MAP_KEY_OF");
    same("STRING", "R := MAP_KEY_OF(Stock, 1.0, '?');", "?", "MAP_KEY_OF absente : le defaut");
    same("DINT", "R := LIST_COUNT_OF(Mesures, 8.0) * 10 + LIST_COUNT_OF(Tab, 9);", "22", "LIST_COUNT_OF");
    same("DINT", "R := LIST_COUNT_OF(Mesures, 8);", "2", "LIST_COUNT_OF : un entier compare a des REAL");
    fails("i := LIST_COUNT_OF(Mesures, 'abc');", "LIST_COUNT_OF : 'abc' n'est pas un REAL", "LIST_COUNT_OF d'un texte dans des REAL");
    fails("i := LIST_LAST_INDEX_OF(Tab, 9.5);", "LIST_LAST_INDEX_OF : 9.5 n'est pas un entier (INT)", "LIST_LAST_INDEX_OF de 9.5 dans des INT");
    fails("s := MAP_KEY_OF(Stock, 'x', '');", "MAP_KEY_OF : 'x' n'est pas un REAL", "MAP_KEY_OF d'un texte dans une MAP de REAL");
    fails("i := MAP_REMOVE_IF(Stock, COMPARAISON#Egal, 'x');", "MAP_REMOVE_IF : 'x' n'est pas un REAL", "MAP_REMOVE_IF d'un texte");
    same("DINT", "R := LIST_FIND_IF(Mesures, COMPARAISON#Superieur, 13.0);", "2", "LIST_FIND_IF > 13");
    same("DINT", "R := LIST_FIND_IF(TabIhm, COMPARAISON#Egal, 9);", "2", "LIST_FIND_IF dans un ARRAY[1..5]");
    same("DINT", "R := LIST_FIND_IF(Mesures, COMPARAISON#Superieur, 99.0);", "-1", "LIST_FIND_IF : aucun");
    same("DINT", "R := LIST_COUNT_IF(Mesures, COMPARAISON#Superieur, 10.0);", "2", "LIST_COUNT_IF > 10");
    same("DINT", "R := LIST_COUNT_IF(Tab, COMPARAISON#Egal, 9);", "2", "LIST_COUNT_IF sur un ARRAY");
    same("DINT", "R := LIST_COUNT_IF(Mesures, COMPARAISON#Different, 8.0);", "3", "LIST_COUNT_IF <>");
    same("DINT", "R := LIST_COUNT_IF(Mesures, COMPARAISON#InferieurOuEgal, 8.0) + 10 * LIST_COUNT_IF(Mesures, COMPARAISON#SuperieurOuEgal, 12.5);",
         "23", "LIST_COUNT_IF <= et >=");
    same("STRING", "R := TO_STRING(LIST_ALL(Mesures, COMPARAISON#Superieur, 3.5)) + TO_STRING(LIST_ALL(Mesures, COMPARAISON#Superieur, 3.0));",
         "FALSETRUE", "LIST_ALL");
    same("BOOL", "R := LIST_ALL(Vide, COMPARAISON#Egal, 1.0);", "TRUE", "LIST_ALL d'une liste vide : vrai");
    same("STRING", "R := TO_STRING(LIST_ANY(Mesures, COMPARAISON#Superieur, 15.0)) + TO_STRING(LIST_ANY(Tab, COMPARAISON#Egal, 5));",
         "TRUEFALSE", "LIST_ANY");
    same("BOOL", "R := LIST_NONE(Mesures, COMPARAISON#Inferieur, 0.0);", "TRUE", "LIST_NONE");
    fails("i := LIST_FIND_IF(Mesures, OPERATION#Somme, 1.0);", "LIST_FIND_IF : une COMPARAISON est attendue (COMPARAISON#Superieur\xE2\x80\xA6), pas OPERATION#Somme",
          "LIST_FIND_IF avec une OPERATION");
    fails("i := LIST_FIND_IF(Mesures, 9, 1.0);", "une COMPARAISON est attendue", "LIST_FIND_IF avec 9");
    same("DINT", "R := LIST_MIN_INDEX(Mesures) * 10 + LIST_MAX_INDEX(Mesures);", "42", "LIST_MIN_INDEX, LIST_MAX_INDEX");
    same("DINT", "R := LIST_MAX_INDEX(Tab);", "1", "LIST_MAX_INDEX d'un ARRAY : le premier des plus grands");
    same("DINT", "R := LIST_MIN_INDEX(Vide);", "-1", "LIST_MIN_INDEX d'une liste vide");
    same("INT", "R := MAP_FLOOR_KEY(Paliers, 230, -1);", "100", "MAP_FLOOR_KEY : 230 tombe sur 100");
    same("INT", "R := MAP_FLOOR_KEY(Paliers, 500, -1);", "500", "MAP_FLOOR_KEY : egal");
    same("INT", "R := MAP_FLOOR_KEY(Paliers, -5, -1);", "-1", "MAP_FLOOR_KEY : rien dessous");
    same("INT", "R := MAP_CEIL_KEY(Paliers, 230, -1) + MAP_CEIL_KEY(Paliers, 900, -1);", "499", "MAP_CEIL_KEY");
    same("STRING", "R := MAP_FLOOR_KEY(Stock, 'h', '?') + MAP_CEIL_KEY(Stock, 'h', '?');", "goujonvis", "MAP_FLOOR_KEY et MAP_CEIL_KEY sur des textes");
    // Les 10 natives d'avant sur un ARRAY
    same("INT", "R := LIST_GET(Tab, 4, 0) + LIST_GET(Tab, 5, -100);", "-94", "LIST_GET sur un ARRAY (dans et hors des bornes)");
    same("INT", "R := LIST_GET(TabIhm, 1, 0);", "4", "LIST_GET sur un ARRAY[1..5] : l'indice 1 est la premiere case");
    same("INT", "R := LIST_MIN(Tab) * 100 + LIST_MAX(Tab);", "109", "LIST_MIN, LIST_MAX d'un ARRAY");
    same("DINT", "R := LIST_SUM(Tab);", "29", "LIST_SUM d'un ARRAY");
    same("REAL", "R := LIST_AVG(Tab);", "5.8", "LIST_AVG d'un ARRAY");
    same("STRING", "R := JOIN(Tab);", "4, 9, 1, 9, 6", "JOIN d'un ARRAY");
}

void ranger() {
    std::printf("-- ranger\n");
    same("STRING", "LIST_SORT(Tab, TRUE);\nR := JOIN(Tab, ',');", "9,9,6,4,1", "LIST_SORT d'un ARRAY, decroissant");
    same("STRING", "LIST_SORT(TabIhm);\nR := JOIN(TabIhm, ',');", "1,4,6,9,9", "LIST_SORT d'un tableau IHM : ecrit en place");
    same("STRING", "LIST_REVERSE(Tab);\nR := JOIN(Tab, ',');", "6,9,1,9,4", "LIST_REVERSE d'un ARRAY");
    const std::string lots = "TYPE_LOTS";
    (void)lots;
    same("STRING", "Noms := [('B12', 2.5), ('A07', 4.0), ('C03', 1.2)];\nLIST_SORT_BY(Noms, 'Item2');\nR := Noms[0].Item1 + Noms[2].Item1;", "C03A07",
         "LIST_SORT_BY d'une liste de TUPLE par 'Item2'");
    same("STRING", "Noms := [('B12', 2.5), ('A07', 4.0), ('C03', 1.2)];\nLIST_SORT_BY(Noms, 'Item1', TRUE);\nR := Noms[0].Item1;", "C03",
         "LIST_SORT_BY decroissant par 'Item1'");
    same("STRING", "Noms := [('x', 1.0), ('y', 1.0), ('z', 0.0)];\nLIST_SORT_BY(Noms, 'Item2');\nR := Noms[0].Item1 + Noms[1].Item1 + Noms[2].Item1;", "zxy",
         "LIST_SORT_BY est stable (a valeur egale, l'ordre reste)");
    fails("Noms := [('B12', 2.5)];\nLIST_SORT_BY(Noms, 'Item3');", "LIST_SORT_BY : un TUPLE(STRING, REAL) a Item1 \xC3\xA0 Item2, pas 'Item3'", "LIST_SORT_BY : un champ qui n'existe pas");
    fails("LIST_SORT_BY(Mesures, 'Poids');", "LIST_SORT_BY : des REAL n'ont pas de membres", "LIST_SORT_BY d'une liste de nombres");
    same("STRING", "R := TO_STRING(LIST_IS_SORTED(Triee)) + TO_STRING(LIST_IS_SORTED(Mesures)) + TO_STRING(LIST_IS_SORTED(Vide));", "TRUEFALSETRUE",
         "LIST_IS_SORTED");
    same("BOOL", "LIST_SORT(Mesures, TRUE);\nR := LIST_IS_SORTED(Mesures, TRUE);", "TRUE", "LIST_IS_SORTED decroissant");
    same("STRING", "b := LIST_SWAP(Pile, 0, 2);\nR := JOIN(Pile, '');", "cba", "LIST_SWAP");
    same("BOOL", "R := LIST_SWAP(Tab, 0, 9);", "FALSE", "LIST_SWAP hors de la liste : FALSE");
    same("STRING", "LIST_MOVE(Mesures, 4, 0);\nR := JOIN(Mesures, ';');", "3.1;12.5;8;15.2;8", "LIST_MOVE vers le debut");
    same("STRING", "LIST_MOVE(Mesures, 0, 2);\nR := JOIN(Mesures, ';');", "8;15.2;12.5;8;3.1", "LIST_MOVE vers la fin");
    same("STRING", "LIST_ROTATE(Tab, 1);\nR := JOIN(Tab, ',');", "9,1,9,6,4", "LIST_ROTATE de 1");
    same("STRING", "LIST_ROTATE(Pile, -1);\nR := JOIN(Pile, '');", "cab", "LIST_ROTATE de -1");
    same("STRING", "LIST_ROTATE(Vide, 3);\nR := 'ok';", "ok", "LIST_ROTATE d'une liste vide");
}

void extraire() {
    std::printf("-- extraire et convertir\n");
    same("STRING", "R := JOIN(LIST_SLICE(Mesures, 1, 3), ';');", "8;15.2;8", "LIST_SLICE");
    same("STRING", "R := JOIN(LIST_SLICE(Mesures, 3, 10), ';');", "8;3.1", "LIST_SLICE : ce qui depasse est ignore");
    same("STRING", "R := JOIN(LIST_SLICE(Tab, 2, 2), ',');", "1,9", "LIST_SLICE d'un ARRAY a ses indices");
    same("STRING", "R := JOIN(LIST_SLICE(TabIhm, 1, 2), ',');", "4,9", "LIST_SLICE d'un ARRAY[1..5]");
    same("STRING", "R := JOIN(LIST_CONCAT(Mesures, Autres), ';');", "12.5;8;15.2;8;3.1;1;2", "LIST_CONCAT");
    fails("s := JOIN(LIST_CONCAT(Pile, Mesures));", "LIST_CONCAT : des REAL ne vont pas apr\xC3\xA8s des STRING", "LIST_CONCAT de deux types");
    same("STRING", "i := LIST_COPY(Autres, Mesures);\nR := TO_STRING(i) + ' ' + JOIN(Mesures, ';');", "2 1;2;15.2;8;3.1", "LIST_COPY : la destination garde sa taille");
    fails("i := LIST_COPY(Mesures, Tab);", "LIST_COPY : 12.5 n'est pas un entier (INT)", "LIST_COPY de REAL a decimales vers des INT : refuse");
    same("STRING", "Mesures := [1.0, 2.0];\ni := LIST_COPY(Mesures, Tab);\nR := TO_STRING(i) + ' ' + JOIN(Tab, ',');", "2 1,2,1,9,6",
         "LIST_COPY d'une liste vers un ARRAY (le reste garde sa valeur)");
    same("STRING", "i := LIST_COPY(Autres, TabIhm);\nR := JOIN(TabIhm, ',');", "1,2,1,9,6", "LIST_COPY vers un tableau IHM (ecrit en place)");
    same("DINT", "R := LIST_COUNT(TO_LIST(Tab));", "5", "TO_LIST d'un ARRAY");
    same("STRING", "R := JOIN(TO_LIST(Pile), '');", "abc", "TO_LIST d'un VECTOR");
    same("REAL", "R := LIST_SUM(MAP_VALUES(Stock));", "245", "MAP_VALUES puis LIST_SUM");
    same("STRING", "Noms := MAP_ITEMS(Stock);\nLIST_SORT_BY(Noms, 'Item2', TRUE);\nR := Noms[0].Item1;", "vis", "MAP_ITEMS trie par 'Item2' : un classement");
    same("STRING", "R := TUPLE_TO_STRING(Point);", "('four', 850.0)", "TUPLE_TO_STRING");
    same("STRING", "TUPLE_UNPACK(Point, Nom, Valeur);\nR := Nom + ' ' + TO_STRING(Valeur);", "four 850", "TUPLE_UNPACK");
    fails("TUPLE_UNPACK(Point, Valeur, Nom);", "TUPLE_UNPACK : le champ 1 est un STRING, Valeur un REAL", "TUPLE_UNPACK : les types dans le desordre");
    fails("TUPLE_UNPACK(Point, Nom);", "TUPLE_UNPACK : TUPLE(STRING, REAL) a 2 champs, 1 variable donn\xC3\xA9" "e", "TUPLE_UNPACK : une variable de moins");
    fails("i := LIST_SET(Mesures, 1);", "mauvais nombre d'arguments : LIST_SET(L, i, x)", "LIST_SET : il manque un argument");
}

// -------------------------------------------------------------------- std::numeric ---
void numeric() {
    std::printf("-- std::numeric\n");
    same("REAL", "R := ACCUMULATE(Mesures, 0.0);", "46.8", "ACCUMULATE : la somme");
    same("REAL", "R := ACCUMULATE(Autres, 1.0, OPERATION#Produit);", "2", "ACCUMULATE : le produit");
    same("REAL", "R := ACCUMULATE(Mesures, 100.0, OPERATION#Min);", "3.1", "ACCUMULATE : le minimum");
    same("DINT", "R := ACCUMULATE(Tab, 0, OPERATION#Max);", "9", "ACCUMULATE d'un ARRAY : le maximum");
    same("DINT", "R := ACCUMULATE(Tab, 0, OPERATION#Ou);", "15", "ACCUMULATE : OPERATION#Ou (4 OR 9 OR 1 OR 9 OR 6)");
    same("REAL", "R := REDUCE(Mesures);", "46.8", "REDUCE sans init");
    same("DINT", "R := REDUCE(Tab, 1);", "30", "REDUCE avec init");
    same("REAL", "R := INNER_PRODUCT(Autres, Autres, 0.0);", "5", "INNER_PRODUCT");
    fails("x := INNER_PRODUCT(Mesures, Autres, 0.0);", "INNER_PRODUCT : B est plus court que A : B[2] manque", "INNER_PRODUCT : B plus court");
    same("REAL", "R := TRANSFORM_REDUCE(Autres, 0.0, OPERATION#Somme, TRANSFORMATION#Carre);", "5", "TRANSFORM_REDUCE : la somme des carres");
    same("STRING", "R := JOIN(PARTIAL_SUM(Tab), ',');", "4,13,14,23,29", "PARTIAL_SUM d'un ARRAY");
    same("STRING", "R := JOIN(INCLUSIVE_SCAN(Tab, OPERATION#Max), ',');", "4,9,9,9,9", "INCLUSIVE_SCAN : le maximum atteint");
    same("STRING", "R := JOIN(EXCLUSIVE_SCAN(Tab, 0), ',');", "0,4,13,14,23", "EXCLUSIVE_SCAN");
    same("STRING", "x := -1.0;\nMesures := [3.0, x, 4.0, x, 5.0];\nR := JOIN(TRANSFORM_INCLUSIVE_SCAN(Mesures, OPERATION#Somme, TRANSFORMATION#Abs), ',');",
         "3,4,8,9,14", "TRANSFORM_INCLUSIVE_SCAN : le chemin parcouru");
    same("STRING", "x := -1.0;\nMesures := [3.0, x, 4.0, x, 5.0];\nR := JOIN(TRANSFORM_EXCLUSIVE_SCAN(Mesures, 0.0, OPERATION#Somme, TRANSFORMATION#Abs), ',');",
         "0,3,4,8,9", "TRANSFORM_EXCLUSIVE_SCAN");
    same("STRING", "R := JOIN(ADJACENT_DIFFERENCE(Tab), ',');", "4,5,-8,8,-3", "ADJACENT_DIFFERENCE");
    same("STRING", "i := IOTA(Tab, 1);\nR := TO_STRING(i) + ' ' + JOIN(Tab, ',');", "5 1,2,3,4,5", "IOTA d'un ARRAY");
    same("STRING", "IOTA(Mesures, 0.0, 2.5);\nR := JOIN(Mesures, ',');", "0,2.5,5,7.5,10", "IOTA avec un pas");
    same("STRING", "IOTA(TabIhm, 10, 10);\nR := JOIN(TabIhm, ',');", "10,20,30,40,50", "IOTA d'un tableau IHM (ecrit en place)");
    same("DINT", "R := GCD(120, 84);", "12", "GCD");
    same("DINT", "R := LCM(120, 84);", "840", "LCM");
    same("DINT", "R := GCD(0, 0) + LCM(0, 5);", "0", "GCD(0, 0), LCM(0, 5)");
    fails("i := GCD(1.5, 3);", "GCD attend deux entiers", "GCD d'un reel");
    same("INT", "R := MIDPOINT<INT>(32000, 32002);", "32001", "MIDPOINT<INT> sans debordement");
    same("DINT", "R := MIDPOINT(3, 8) * 10 + MIDPOINT(8, 3);", "56", "MIDPOINT d'entiers : arrondi vers a");
    same("REAL", "R := MIDPOINT(0.5, 2.5);", "1.5", "MIDPOINT de reels");
    same("INT", "R := ADD_SAT<INT>(32000, 1000);", "32767", "ADD_SAT<INT>");
    same("INT", "R := SUB_SAT<INT>(-32000, 1000);", "-32768", "SUB_SAT<INT>");
    same("UINT", "R := SUB_SAT<UINT>(5, 8);", "0", "SUB_SAT<UINT> : 0");
    same("INT", "R := MUL_SAT<INT>(300, 200);", "32767", "MUL_SAT<INT>");
    same("INT", "R := DIV_SAT<INT>(-32768, -1);", "32767", "DIV_SAT<INT> : le seul cas qui deborde");
    fails("i := DIV_SAT<INT>(5, 0);", "DIV_SAT : division par z\xC3\xA9ro", "DIV_SAT : division par zero");
    same("INT", "R := ADD_SAT(Compteur, 32760);", "32767", "ADD_SAT sans <T> : le type de a (INT)");
    same("DINT", "R := ADD_SAT(2147483647, 1);", "2147483647", "ADD_SAT d'un litteral : DINT");
    same("INT", "R := ADD_SAT(si, 100);", "127", "ADD_SAT d'une SINT : ses bornes");
    same("INT", "R := SATURATE_CAST<INT>(40000);", "32767", "SATURATE_CAST<INT>(40000)");
    same("UINT", "R := SATURATE_CAST<UINT>(-5);", "0", "SATURATE_CAST<UINT>(-5)");
    same("INT", "R := SATURATE_CAST<SINT>(300.6);", "127", "SATURATE_CAST<SINT> d'un reel");
    same("DINT", "R := SATURATE_CAST<DINT>(12.5);", "13", "SATURATE_CAST<DINT>(12.5) : arrondi");
    fails("i := SATURATE_CAST(40000);", "SATURATE_CAST attend son type : SATURATE_CAST<INT>(x)", "SATURATE_CAST sans son type");
    fails("x := ACCUMULATE(Pile, 0.0);", "ACCUMULATE : des nombres sont attendus, pas un VECTOR OF STRING", "ACCUMULATE de textes");
    fails("x := ACCUMULATE(Mesures, 0.0, COMPARAISON#Egal);", "ACCUMULATE : une OPERATION est attendue (OPERATION#Somme\xE2\x80\xA6), pas COMPARAISON#Egal",
          "ACCUMULATE avec une COMPARAISON");
}

// ---------------------------------------------------------------------- templates ---
void templates() {
    std::printf("-- les templates : NUMERIC_LIMITS<T>, NUMBERS[<T>]\n");
    same("UINT", "R := NUMERIC_LIMITS<UINT>.MAX();", "65535", "NUMERIC_LIMITS<UINT>.MAX()");
    same("INT", "R := NUMERIC_LIMITS<INT>.MIN();", "-32768", "NUMERIC_LIMITS<INT>.MIN()");
    same("DINT", "R := NUMERIC_LIMITS<REAL>.DIGITS;", "24", "NUMERIC_LIMITS<REAL>.DIGITS");
    same("DINT", "R := NUMERIC_LIMITS<INT>.DIGITS10;", "4", "NUMERIC_LIMITS<INT>.DIGITS10");
    same("BOOL", "R := NUMERIC_LIMITS<UINT>.IS_SIGNED;", "FALSE", "NUMERIC_LIMITS<UINT>.IS_SIGNED");
    same("BOOL", "R := NUMERIC_LIMITS<WORD>.IS_MODULO;", "TRUE", "NUMERIC_LIMITS<WORD>.IS_MODULO");
    same("BOOL", "R := NUMERIC_LIMITS<REAL>.EPSILON() < 0.0001 AND NUMERIC_LIMITS<REAL>.EPSILON() > 0.0;", "TRUE", "NUMERIC_LIMITS<REAL>.EPSILON()");
    same("BOOL", "R := NUMERIC_LIMITS<LREAL>.LOWEST() < -1.0E300;", "TRUE", "NUMERIC_LIMITS<LREAL>.LOWEST()");
    same("UDINT", "R := NUMERIC_LIMITS<UDINT>.MAX();", "4294967295", "NUMERIC_LIMITS<UDINT>.MAX()");
    same("DINT", "R := NUMERIC_LIMITS<LREAL>.MAX_EXPONENT10;", "308", "NUMERIC_LIMITS<LREAL>.MAX_EXPONENT10");
    same("BOOL", "R := NUMERIC_LIMITS<BOOL>.MAX();", "TRUE", "NUMERIC_LIMITS<BOOL>.MAX()");
    same("BOOL", "R := NUMBERS.PI > 3.14159 AND NUMBERS.PI < 3.1416;", "TRUE", "NUMBERS.PI (LREAL)");
    same("BOOL", "R := NUMBERS<REAL>.PI <> NUMBERS.PI;", "TRUE", "NUMBERS<REAL>.PI arrondi au float");
    same("BOOL", "R := ABS(NUMBERS.SQRT2 * NUMBERS.SQRT2 - 2.0) < 1.0E-12;", "TRUE", "NUMBERS.SQRT2");
    // Les fautes : dites a la lecture du script (comme Compiler les dit).
    fails("i := NUMERIC_LIMITS<STRING>.MAX();", "NUMERIC_LIMITS<STRING> : un type num\xC3\xA9rique est attendu (SINT \xE2\x80\xA6 ULINT, BYTE \xE2\x80\xA6 LWORD, REAL, LREAL, BOOL)",
          "NUMERIC_LIMITS<STRING>");
    fails("i := NUMERIC_LIMITS<UINT>.MAXX();", "NUMERIC_LIMITS<UINT> n'a pas de membre MAXX \xE2\x80\x94 veux-tu dire MAX ?", "NUMERIC_LIMITS<UINT>.MAXX()");
    fails("i := NUMERIC_LIMITS<UINT>.DIGITS();", "DIGITS est une constante : sans parenth\xC3\xA8ses", "DIGITS()");
    fails("i := NUMERIC_LIMITS<UINT>.MIN;", "MIN est une fonction : NUMERIC_LIMITS<UINT>.MIN()", "MIN sans parentheses");
    fails("i := NUMERIC_LIMITS.MAX();", "NUMERIC_LIMITS attend son type : NUMERIC_LIMITS<UINT>.MAX()", "NUMERIC_LIMITS sans son type");
    fails("x := NUMBERS<INT>.PI;", "NUMBERS<INT> : REAL ou LREAL est attendu", "NUMBERS<INT>");
    fails("x := NUMBERS.PII;", "NUMBERS n'a pas de membre PII \xE2\x80\x94 veux-tu dire PI ?", "NUMBERS.PII");
    fails("i := ADD_SAT<STRING>(1, 2);", "ADD_SAT<STRING> : un type entier est attendu", "ADD_SAT<STRING>");
    // Un nom qui n'est pas un type, proche d'un type accepte : la maquette (« UNIT n'est pas un type »).
    // (UNIT : INT et UINT a deux lettres, le premier des types acceptes, comme la maquette)
    fails("i := NUMERIC_LIMITS<UNIT>.MAX();", "UNIT n'est pas un type \xE2\x80\x94 veux-tu dire INT ?", "NUMERIC_LIMITS<UNIT>");
    fails("i := NUMERIC_LIMITS<UINTT>.MAX();", "UINTT n'est pas un type \xE2\x80\x94 veux-tu dire UINT ?", "NUMERIC_LIMITS<UINTT>");
    fails("x := NUMBERS<LREEL>.PI;", "LREEL n'est pas un type \xE2\x80\x94 veux-tu dire LREAL ?", "NUMBERS<LREEL>");
    fails("i := SATURATE_CAST<T_LOT>(x);", "SATURATE_CAST<T_LOT> : un type entier est attendu", "SATURATE_CAST<T_LOT> : un type du projet");
    // Ce qui tournait en 1.12.6 : a < b, et une variable NUMBERS (une structure) garde son sens.
    same("BOOL", "i := 3;\nR := i < 5 AND 5 > i;", "TRUE", "a < b et b > a ne sont pas des templates");
    same("BOOL", "R := Compteur<10;", "TRUE", "Compteur<10 : une comparaison");
}

// -------------------------------------------------------------------------- <bit> ---
void bits() {
    std::printf("-- <bit> : BIT_COUNT, BIT_FIRST, BIT_TEST, BIT_SET, BIT_RESET\n");
    same("DINT", "R := BIT_COUNT(Mot);", "2", "BIT_COUNT(16#0012)");
    same("DINT", "R := BIT_FIRST(Mot);", "1", "BIT_FIRST(16#0012)");
    same("DINT", "w := 0;\nR := BIT_FIRST(w);", "-1", "BIT_FIRST(0) : -1");
    same("BOOL", "R := BIT_TEST(Mot, 4);", "TRUE", "BIT_TEST(Mot, 4)");
    same("BOOL", "R := BIT_TEST(Mot, 0);", "FALSE", "BIT_TEST(Mot, 0)");
    same("WORD", "R := BIT_SET(Mot, 0);", "19", "BIT_SET(16#0012, 0) = 16#0013");
    same("WORD", "R := BIT_RESET(Mot, 4);", "2", "BIT_RESET(16#0012, 4) = 16#0002");
    same("WORD", "x := TO_REAL(BIT_SET(Mot, 0));\nR := Mot;", "18", "BIT_SET ne change pas le mot");
    same("DINT", "R := BIT_COUNT(-1);", "32", "BIT_COUNT(-1) d'un DINT : 32");
    same("INT", "R := BIT_SET(Compteur, 15);", "-32761", "BIT_SET d'un INT : le bit de signe");
    fails("b := BIT_TEST(Mot, 16);", "BIT_TEST : le bit 16 n'existe pas dans un WORD (0..15)", "BIT_TEST hors du mot");
    fails("i := BIT_COUNT(Niveau);", "BIT_COUNT : un mot ou un entier est attendu", "BIT_COUNT d'un REAL");
}

// ------------------------------------------------------------ les enumerations natives ---
void enumerations() {
    std::printf("-- les enumerations COMPARAISON, OPERATION, TRANSFORMATION\n");
    same("DINT", "R := COMPARAISON#Superieur;", "4", "COMPARAISON#Superieur vaut 4");
    same("DINT", "R := OPERATION#Produit + TRANSFORMATION#Inverse;", "6", "OPERATION#Produit (1) + TRANSFORMATION#Inverse (5)");
    std::int64_t n = -1;
    check(sim::lib::dialectEnumValue(" comparaison#inferieurouegal ", n) && n == 3, "dialectEnumValue sans casse");
    check(!sim::lib::dialectEnumValue("COMPARAISON#Plus", n), "COMPARAISON#Plus n'existe pas");
    same("DINT", "i := 2;\nR := LIST_COUNT_IF(Tab, i, 5);", "2", "une COMPARAISON dans une variable (2 : Inferieur)");
}

// --------------------------------------------------- ce qui tournait en 1.12.6 tourne pareil ---
void avant() {
    std::printf("-- la 1.12.6 : une fonction du projet nommee comme une native passe avant\n");
    Project p;
    Variable r;
    r.id = p.allocate();
    r.name = "R";
    r.type = "DINT";
    p.programs.variables.push_back(r);
    HmiFunction f;
    f.id = p.allocate();
    f.name = "GCD";
    f.returnType = "DINT";
    f.body = "VAR_INPUT a : DINT; b : DINT; END_VAR\nGCD := a + b;";
    p.programs.functions.push_back(f);
    View v = makeView(p, "Vue");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = "R := GCD(120, 84);";
    p.programs.scripts.push_back(s);
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string err;
    (void)rt.callScript("Essai", 0.2, &err);
    sim::Value out;
    check(rt.environment().read("R", out) && out.asInteger() == 204, "GCD du projet (a + b = 204), pas la native (" + shown(out) + ") " + err);
}

// ------------------------------------------------------------------------- Compiler ---
std::vector<ScriptDiagnostic> compile(const std::string& body) { return checkScript(ScriptLang::ST, std::string(kDecl) + body, "Essai"); }
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
void compiler() {
    std::printf("-- Compiler : les fautes des nouvelles natives (erreurs), du code d'avant (avertissements)\n");
    using S = scriptcheck::Finding::Severity;
    {
        const auto fs = walk("i := LIST_FIND_IF(Mesures, COMPARAISON#Superieur, 13.0);\nx := ACCUMULATE(Tab, 0.0);\ni := GCD(4, 6);\nb := BIT_TEST(Mot, 3);\n"
                             "i := NUMERIC_LIMITS<UINT>.MAX();\nx := NUMBERS.PI;\ni := SATURATE_CAST<INT>(x);\ni := ADD_SAT<INT>(1, 2);\n"
                             "TUPLE_UNPACK(Point, Nom, Valeur);\nLIST_SORT(Tab);\ni := LIST_INDEX_OF(Tab, 9);");
        bool error = false;
        for (const auto& f : fs) error = error || f.severity == S::Error;
        check(!error, "un script qui emploie les natives de la 1.12.7 se lit sans erreur" + all(fs));
    }
    {
        const auto fs = walk("i := LIST_SET(Mesures, 1);");
        check(says(fs, "LIST_SET prend 3 arguments, pas 2", S::Error), "LIST_SET a 2 arguments : une erreur" + all(fs));
    }
    {
        const auto fs = walk("LIST_ADD(Mesures);");
        check(says(fs, "LIST_ADD prend 2 arguments, pas 1", S::Warning) && !says(fs, "LIST_ADD prend", S::Error),
              "LIST_ADD a 1 argument (du code d'avant) : un avertissement" + all(fs));
    }
    {
        const auto fs = walk("LIST_ADD(Tab, 3);");
        check(says(fs, "LIST_ADD : un ARRAY a une taille fixe (5 cases)", S::Warning), "LIST_ADD sur un ARRAY (du code d'avant) : un avertissement" + all(fs));
    }
    {
        const auto fs = walk("i := LIST_REMOVE_ALL(Tab, 3);");
        check(says(fs, "LIST_REMOVE_ALL : un ARRAY a une taille fixe (5 cases)", S::Error), "LIST_REMOVE_ALL sur un ARRAY : une erreur" + all(fs));
    }
    {
        const auto fs = walk("x := TUPLE_GET(Point, 3);");
        check(says(fs, "TUPLE_GET : TUPLE(STRING, REAL) a 2 champs, pas 3", S::Error), "TUPLE_GET(Point, 3) : une erreur" + all(fs));
    }
    {
        const auto fs = walk("Noms := MAP_ITEMS(Stock);\nLIST_SORT_BY(Noms, 'Item5');");
        check(says(fs, "Item5", S::Error), "LIST_SORT_BY(Noms, 'Item5') : une erreur" + all(fs));
    }
    {
        const auto ds = compile("i := NUMERIC_LIMITS<STRING>.MAX();");
        bool said = false;
        for (const auto& d : ds) said = said || (d.severity == ScriptDiagnostic::Severity::Error && d.message.find("NUMERIC_LIMITS<STRING>") != std::string::npos);
        check(said, "Compiler : NUMERIC_LIMITS<STRING> est une erreur");
    }
    {
        const auto fs = walk("i := SATURATE_CAST(x);");
        check(says(fs, "SATURATE_CAST attend son type : SATURATE_CAST<INT>(x)", S::Error), "SATURATE_CAST sans type : une erreur" + all(fs));
    }
    {
        const auto fs = walk("i := LIST_COUNT_IF(Mesures, COMPARAISON#Plus, 1.0);");
        check(says(fs, "COMPARAISON#Plus n'existe pas", S::Error), "COMPARAISON#Plus : une erreur" + all(fs));
    }
    {
        // NOM<TYPE sans son > : la maquette - « le type, puis > ».
        const auto fs = walk("i := NUMERIC_LIMITS<UINT.MAX();\ni := SATURATE_CAST<INT(x);");
        check(says(fs, "NUMERIC_LIMITS : le type, puis > \xE2\x80\x94 NUMERIC_LIMITS<UINT>.MAX()", S::Error)
                  && says(fs, "SATURATE_CAST : le type, puis > \xE2\x80\x94 SATURATE_CAST<INT>(x)", S::Error),
              "NUMERIC_LIMITS<UINT.MAX(), SATURATE_CAST<INT(x) : le type, puis >" + all(fs));
        // Une variable nommee comme un template garde son sens (a < b).
        const auto old = walk("VAR\n  Midpoint : INT;\nEND_VAR\nb := Midpoint < 5 AND Midpoint > i;");
        check(!says(old, "le type, puis", S::Error) && !says(old, "MIDPOINT", S::Error), "Midpoint < 5 (une variable) : pas un template" + all(old));
    }
    {
        // Une native qui ecrirait une constante (VAR CONSTANT) : la maquette - une erreur pour une nouvelle
        // native, un avertissement pour une d'avant ; une native qui la lit, rien.
        const auto fs = walk("VAR CONSTANT\n  Seuils : LIST OF REAL := [10.0, 20.0];\n  Limite : REAL := 5.0;\nEND_VAR\n"
                             "LIST_SET(Seuils, 0, 1.0);\nLIST_ADD(Seuils, 3.0);\nLIST_COPY(Mesures, Seuils);\nTUPLE_UNPACK(Point, Nom, Limite);\n"
                             "i := LIST_COUNT_OF(Seuils, 1.0);\nx := LIST_SUM(Seuils);");
        check(says(fs, "Seuils est une constante : LIST_SET ne peut pas la changer", S::Error)
                  && says(fs, "Seuils est une constante : LIST_ADD ne peut pas la changer", S::Warning)
                  && !says(fs, "LIST_ADD ne peut pas", S::Error)
                  && says(fs, "Seuils est une constante : LIST_COPY ne peut pas la changer", S::Error)
                  && says(fs, "Limite est une constante : TUPLE_UNPACK ne peut pas la changer", S::Error)
                  && !says(fs, "LIST_COUNT_OF ne peut pas", S::Error) && !says(fs, "LIST_SUM ne peut pas", S::Error),
              "une constante ecrite par une native : dite par Compiler" + all(fs));
    }
}

// Compiler sur un projet entier (compileWith) : les templates, les natives et les enumerations ne sont
// pas des « variables inexistantes » ; une structure du projet et le membre d'un tri.
void compilerProjet() {
    std::printf("-- Compiler : un projet qui emploie les natives de la 1.12.7\n");
    Project p;
    HmiType lot;
    lot.id = p.allocate();
    lot.name = "T_Lot";
    lot.members = {TypeMember{"Nom", "STRING"}, TypeMember{"Poids", "REAL"}};
    p.programs.types.push_back(lot);
    const auto var = [&](const char* n, const char* t, const char* init) {
        Variable v;
        v.id = p.allocate();
        v.name = n;
        v.type = t;
        v.initial = init;
        p.programs.variables.push_back(v);
    };
    var("Lots", "LIST OF T_Lot", "");
    var("Mot", "WORD", "16#0012");
    var("Compte", "UINT", "0");
    var("Rayon", "REAL", "1.0");
    View v = makeView(p, "Vue");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    const auto script = [&](const char* name, const std::string& body) {
        Script sc;
        sc.id = p.allocate();
        sc.name = name;
        sc.event = "Appel";
        sc.body = body;
        p.programs.scripts.push_back(sc);
    };
    script("Bon", "VAR\n    x : REAL;\n    i : DINT;\n    b : BOOL;\nEND_VAR\n"
                  "IF Compte = NUMERIC_LIMITS<UINT>.MAX() THEN Compte := 0; END_IF\n"
                  "x := NUMBERS.PI * Rayon * Rayon + NUMBERS<REAL>.E;\n"
                  "i := SATURATE_CAST<INT>(x) + ADD_SAT<INT>(1, 2) + MIDPOINT(2, 8) + GCD(4, 6);\n"
                  "b := BIT_TEST(Mot, 4) AND NUMERIC_LIMITS<REAL>.IS_SIGNED;\n"
                  "LIST_SORT_BY(Lots, 'Poids', TRUE);\n"
                  "i := LIST_FIND_IF(Lots, COMPARAISON#Egal, 'A07', 'Nom');");
    script("Faux", "LIST_SORT_BY(Lots, 'Poid');");
    const auto issues = compileWith(p, [](std::string_view) { return false; });
    std::string bon, faux;
    for (const auto& i : issues) {
        if (i.severity != Issue::Severity::Error) continue;
        (i.property == "Bon" ? bon : faux) += " | " + i.message;
    }
    check(bon.empty(), "le script qui emploie les natives de la 1.12.7 compile sans erreur" + bon);
    check(faux.find("T_Lot n'a pas de membre Poid \xE2\x80\x94 veux-tu dire Poids ?") != std::string::npos,
          "LIST_SORT_BY(Lots, 'Poid') : T_Lot n'a pas de membre Poid (veux-tu dire Poids ?)" + faux);
    // En marche : le tri par un membre d'une structure du projet, la recherche par 'Nom'.
    Script run;
    run.id = p.allocate();
    run.name = "Essai";
    run.event = "Appel";
    run.body = "VAR i : DINT; l : T_Lot; END_VAR\nLots := [];\nl.Nom := 'B12';\nl.Poids := 2.5;\nLIST_ADD(Lots, l);\n"
               "l.Nom := 'A07';\nl.Poids := 4.0;\nLIST_ADD(Lots, l);\nl.Nom := 'C03';\nl.Poids := 1.2;\nLIST_ADD(Lots, l);\n"
               "LIST_SORT_BY(Lots, 'Poids', TRUE);\ni := LIST_FIND_IF(Lots, COMPARAISON#Egal, 'C03', 'Nom');\n"
               "Compte := TO_UINT(i * 10 + LIST_MAX_INDEX(Lots, 'Poids'));";
    p.programs.scripts.push_back(run);
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string err;
    (void)rt.callScript("Essai", 0.2, &err);
    sim::Value out;
    check(rt.environment().read("Compte", out) && out.asInteger() == 20,
          "une LIST OF T_Lot triee par 'Poids' (decroissant) : C03 au rang 2, le plus lourd au rang 0 (" + shown(out) + ") " + err);
    // LIST_SORT d'une liste de structures : LIST_SORT_BY (la maquette) ; LIST_FIND_IF sans membre : lequel.
    p.programs.scripts.back().body = "LIST_SORT(Lots);";
    Runtime rt2;
    rt2.bind(&p, nullptr);
    rt2.start(0.0);
    rt2.tick(0.1);
    (void)rt2.callScript("Essai", 0.2, &err);
    check(err.find("LIST_SORT : des T_Lot \xE2\x80\x94 LIST_SORT_BY(L, 'Poids') trie par un membre") != std::string::npos,
          "LIST_SORT d'une LIST OF T_Lot : LIST_SORT_BY (" + err + ")");
    p.programs.scripts.back().body = "VAR i : DINT; l : T_Lot; END_VAR\nLIST_ADD(Lots, l);\ni := LIST_FIND_IF(Lots, COMPARAISON#Egal, 'A07');";
    Runtime rt3;
    rt3.bind(&p, nullptr);
    rt3.start(0.0);
    rt3.tick(0.1);
    (void)rt3.callScript("Essai", 0.2, &err);
    check(err.find("LIST_FIND_IF : des T_Lot \xE2\x80\x94 dis quel membre comparer : 'Poids'") != std::string::npos,
          "LIST_FIND_IF d'une LIST OF T_Lot sans membre : lequel comparer (" + err + ")");
}

// Une expression de vue (fx) : les templates et les natives pures s'y lisent ; une native qui ecrit
// son argument y est dite (une expression lit une copie).
void expressions() {
    std::printf("-- les expressions fx : templates, natives pures, natives qui ecrivent\n");
    Project p;
    Variable mot;
    mot.id = p.allocate();
    mot.name = "Mot";
    mot.type = "WORD";
    p.programs.variables.push_back(mot);
    Variable liste;
    liste.id = p.allocate();
    liste.name = "Liste";
    liste.type = "LIST OF REAL";
    p.programs.variables.push_back(liste);
    exprcheck::Context c;
    c.project = &p;
    c.known = [&p](std::string_view n) { return p.variable(n) != nullptr; };
    const auto said = [&](const std::string& e, exprcheck::Want want = exprcheck::Want::Any) {
        std::string all;
        for (const auto& pb : exprcheck::check(c, e, want)) all += " | " + pb.message;
        return all;
    };
    for (const char* e : {"NUMBERS.PI * 2.0", "NUMERIC_LIMITS<UINT>.MAX() - 1", "NUMBERS<REAL>.SQRT2 + 1.0", "GCD(12, 8) + BIT_COUNT(Mot)",
                          "SATURATE_CAST<INT>(40000.0) + ADD_SAT<INT>(1, 2)", "LIST_COUNT_IF(Liste, COMPARAISON#Superieur, 1.0)"}) {
        const std::string m = said(e, exprcheck::Want::Number);
        check(m.empty(), std::string("fx : ") + e + " se lit (un nombre)" + m);
    }
    check(said("NUMERIC_LIMITS<REAL>.IS_SIGNED AND BIT_TEST(Mot, 4)", exprcheck::Want::Bool).empty(), "fx : NUMERIC_LIMITS<REAL>.IS_SIGNED et BIT_TEST : des BOOL");
    check(said("LIST_SET(Liste, 0, 1.0)").find("LIST_SET change son objet") != std::string::npos, "fx : LIST_SET change son objet - dit");
    check(said("GCD(1, 2, 3)").find("GCD prend 2 arguments, pas 3") != std::string::npos, "fx : GCD a trois arguments - dit");
    const auto roots = scanRoots("NUMERIC_LIMITS<UINT>.MAX() + NUMBERS.PI + Mot");
    check(roots.size() == 1 && roots.front() == "Mot", "fx : les racines - Mot seul (pas NUMERIC_LIMITS, UINT, NUMBERS)");
    // En marche, dans une expression : la valeur.
    Bench b("REAL", "R := 0;");
    const auto v = Expression::compile("NUMERIC_LIMITS<INT>.MAX() + NUMBERS.PI").evaluate(b.rt.environment());
    check(v && std::fabs(v->asReal() - (32767.0 + 3.141592653589793)) < 1e-9, "fx en marche : NUMERIC_LIMITS<INT>.MAX() + NUMBERS.PI");
}

} // namespace

int main() {
    std::printf("1.12.7 : les natives des conteneurs, de std::numeric, les templates et <bit>\n");
    lireEtEcrire();
    constantes();
    ajouterEtRetirer();
    chercher();
    ranger();
    extraire();
    numeric();
    templates();
    bits();
    enumerations();
    avant();
    compiler();
    compilerProjet();
    expressions();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
