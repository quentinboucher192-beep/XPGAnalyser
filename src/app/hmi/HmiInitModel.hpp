// =============================================================================
//  app/hmi/HmiInitModel.hpp - 1.12.16 (T4) : LA VALEUR INITIALE, SANS ECRAN
// -----------------------------------------------------------------------------
//  Ce que la fenetre " Valeur initiale de ... " (HmiInitDialog) sait d'un type,
//  sans rien dessiner - les essais le lisent directement :
//
//    LE BRUT : un editeur par genre de type, bati sur le vrai type (typeform,
//    les membres d'une structure, les valeurs d'une enumeration) :
//      MAP[K] OF V        une ligne par entree : cle, valeur (+ Entree, Ranger par cle)
//      LIST / VECTOR OF T un element par ligne ; une fiche par element structure
//      une structure      un membre par ligne (vide : la valeur du type, en gris)
//      ARRAY[a..b] OF T   une ligne par case, leur nombre fixe (Tout a 0, Suite)
//      ARRAY 2D           une ligne par premiere dimension, une case par colonne
//      SET OF T           une valeur par ligne ; un doublon est dit
//      RING[N] OF T       une valeur par ligne ; celles au-dela de N partiront
//      une enumeration    ses valeurs (#Nom, nombre, texte) : une a choisir
//      TIME               heures, minutes, secondes, millisecondes
//      TUPLE(A, B...)     un element par ligne (Item1, son type)
//      un autre scalaire  une case
//    lu d'une valeur ecrite (load), rendu en litteral (literal : celui que le moteur
//    range - les cles d'une MAP rangees, un SET range sans doublon, un RING garde
//    ses N dernieres), avec ses problemes (problems).
//
//    LE CODE : les exemples a partir desquels ecrire, faits du type (templates) ;
//    " Passer en code " (toCode).
//
//    LE TYPE EXPLIQUE : son arbre (chaque niveau et une phrase), une explication,
//    comment le lire, son equivalent C++, les natives du catalogue qui vont avec.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::initvalue {

enum class Kind : std::uint8_t { Map, List, Struct, Array, Matrix, Set, Ring, Enum, Time, Tuple, Scalar };
[[nodiscard]] std::string_view kindKey(Kind) noexcept;     // "map", "liste", "structure"... (les essais)

// Une case nommee : un membre d'une structure (d'un element structure), un element de tuple.
struct Field {
    std::string name;          // "Nom", "Item1"
    std::string type;          // "STRING"
    std::string zero;          // la valeur du type (la case vide, en gris) : "''", "0.0", la valeur initiale du membre
};

struct Option {                // une valeur d'enumeration
    std::string  name;
    std::int64_t number{0};
    std::string  text;
};

// Le genre de type et ce qu'en montre l'editeur.
struct Shape {
    Kind                     kind{Kind::Scalar};
    std::string              type;                 // tel qu'ecrit
    std::string              element;              // l'element (List, Array, Matrix, Set, Ring) ; la valeur (Map) ; le scalaire
    std::string              key{"STRING"};        // Map : le type des cles
    bool                     textKeys{true};       // Map : des cles texte (sinon des nombres)
    std::string              container{"LIST"};    // List : "LIST" ou "VECTOR"
    std::vector<Field>       fields;               // Struct, Tuple ; un element structure (List, Array : une fiche)
    bool                     structElements{false};
    std::string              elementName;          // l'element structure : son type ("T_Cuve")
    long long                low{0}, high{-1};     // Array ; Matrix : la premiere dimension
    long long                low2{0}, high2{-1};   // Matrix : la seconde
    std::size_t              capacity{0};          // Ring
    std::string              enumName;             // Enum
    std::vector<Option>      options;              // Enum
    std::string              zero;                 // la valeur du type de l'element (Map : de la valeur ; Scalar : la sienne)
};
[[nodiscard]] Shape shapeOf(const hmi::Project&, std::string_view type);
// La valeur du type (ce que vaut une case vide) : "0", "0.0", "FALSE", "''", "T#0s", "[]", T_MODE#Premiere...
[[nodiscard]] std::string zeroOf(const hmi::Project&, std::string_view type);

// L'etat de l'editeur brut. Chaque ligne a ses cases : une valeur (List, Set, Ring, Array, Tuple, Struct,
// Scalar), un membre par case (un element structure), une case par colonne (Matrix), les quatre parts
// d'une duree (Time : heures, minutes, secondes, millisecondes) ; Map : la cle a part.
struct Row {
    std::string              key;
    std::vector<std::string> cells;
};
struct Raw {
    Shape            shape;
    std::vector<Row> rows;
    int              choice{-1};       // Enum : la valeur choisie (-1 : aucune)
    // La valeur ecrite ne se lit pas comme un litteral de ce type (un nom, une expression) : une seule
    // case, telle quelle (" Repartir de l'editeur du type " la quitte).
    bool             freeText{false};
};
[[nodiscard]] Raw load(const hmi::Project&, std::string_view type, std::string_view value);
// Le litteral que la fenetre enregistre.
[[nodiscard]] std::string literal(const Raw&);
struct Problem {
    bool        error{false};          // faux : un avertissement
    std::string text;
    int         row{-1};               // la ligne de l'editeur (-1 : le tout)
};
[[nodiscard]] std::vector<Problem> problems(const hmi::Project&, const Raw&);
// La marque d'une ligne : "doublon" (SET), "partira" (RING), "" sinon.
[[nodiscard]] std::string rowMark(const Raw&, std::size_t row);
// Les intitules : une ligne ("[0]", "'bas'", ".Nom", "Item1", "3"), ses cases ("Nom", "[.., 1]", "heures").
[[nodiscard]] std::string rowLabel(const Raw&, std::size_t row);
[[nodiscard]] std::vector<std::string> cellLabels(const Raw&);
// La case vide, en gris : la valeur du type ("0.0 (du type)").
[[nodiscard]] std::string placeholder(const Raw&, std::size_t row, std::size_t cell);

// Les gestes. Faux : refuse (une taille fixe, une ligne inconnue...).
[[nodiscard]] bool resizable(const Raw&) noexcept;       // Map, List, Set, Ring
bool addRow(Raw&);
bool removeRow(Raw&, std::size_t row);
bool moveRow(Raw&, std::size_t row, int delta);          // List, Ring (un SET, une MAP se rangent)
bool sortByKey(Raw&);                                    // Map : " Ranger par cle "
bool fillZero(Raw&);                                     // Array, Matrix : " Tout a 0 "
bool sequence(Raw&, double start, double step);          // Array (des nombres) : " Suite "
bool setCell(Raw&, std::size_t row, std::size_t cell, std::string text);
bool setKey(Raw&, std::size_t row, std::string text);    // Map ; une cle texte s'ecrit sans apostrophes
bool choose(Raw&, int option);                           // Enum
bool leaveFreeText(Raw&);                                // l'editeur du type, vide

// ---- le code ----------------------------------------------------------------------------
// Un nom que le code d'une valeur calculee peut lire (une constante du script, Reglages.X, une
// variable IHM), et son type : les exemples s'en servent ; sans lui, des litteraux.
struct Name {
    std::string name, type;
};
struct Template {
    std::string label;                 // " Ecrire les entrees "
    std::string calc;                  // le corps (sans l'en-tete fixe)
    std::string locals;                // ses locales, une par ligne (l'onglet Locales)
};
[[nodiscard]] std::vector<Template> templates(const hmi::Project&, std::string_view name, std::string_view type,
                                              const std::vector<Name>& readable = {});
// " Passer en code " : le brut en code, a ajuster.
[[nodiscard]] std::string toCode(std::string_view literal);
// L'en-tete fixe : "FUNCTION Init_Seuils : MAP[STRING] OF REAL".
[[nodiscard]] std::string header(std::string_view name, std::string_view type);

// ---- le type, explique ------------------------------------------------------------------
struct TreeLine {
    int         depth{0};
    std::string label;                 // "MAP", "[STRING]", "OF REAL", ".Nom", "#Auto"
    std::string phrase;                // "un dictionnaire", "la cle : un texte, unique"
};
struct Explanation {
    std::vector<TreeLine>    tree;
    std::string              plain;    // ce qu'il est, en deux phrases
    std::string              read;     // " Le lire "
    std::string              cpp;      // " En C++ "
    std::vector<std::string> natives;  // " Les natives qui vont avec " (celles du catalogue)
    std::string              code;     // ce que le code peut lire (le mode Code)
};
// `owner` : le script de la declaration (Reglages.Nom, lu d'ailleurs) ; vide : aucun.
[[nodiscard]] Explanation explain(const hmi::Project&, std::string_view name, std::string_view type, std::string_view owner = {},
                                  const std::vector<Name>& readable = {});
// L'equivalent C++ d'un type : "std::map<std::string, float>", "std::chrono::milliseconds".
[[nodiscard]] std::string cppOf(const hmi::Project&, std::string_view type);

} // namespace app::initvalue
