// =============================================================================
//  hmi/HmiTypeForms.hpp - 1.12.2 : LA FORME D'UN TYPE, pour tous les createurs
//  de variables (une variable IHM, une constante, une locale, un parametre, le
//  retour d'une fonction)
// -----------------------------------------------------------------------------
//  Un type se compose d'une FORME et d'un ELEMENT :
//
//      Simple        REAL
//      Tableau       ARRAY[0..9] OF REAL            (parametre : les bornes)
//      Tableau 2D    ARRAY[0..3, 0..9] OF INT       (parametre : les bornes des deux dimensions)
//      Liste         LIST OF REAL                   (taille variable, L[0], LIST_ADD)
//      Vecteur       VECTOR OF REAL                 (taille variable, V[0], VECTOR_PUSH)
//      Dictionnaire  MAP[STRING] OF REAL            (parametre : le type des cles)
//      Tuple         TUPLE(REAL, STRING, BOOL)      (parametre : les types qui suivent)
//      Ensemble      SET OF STRING                  (1.12.7 : rangee, chaque valeur une fois)
//      Tampon        RING[100] OF REAL              (1.12.7 : parametre : la capacite, les N dernieres)
//
//  et, la ou l'usage le permet (une declaration, un retour), une REFERENCE vers
//  l'element (REF_TO REAL). Les dialogues (Nouvelle variable IHM, Creer la
//  variable, Nouvelle fonction, Tableau...) et le selecteur de types ecrivent le
//  type par compose() et relisent celui d'une variable par decompose().
//
//  LA TAILLE VARIABLE : une liste, un vecteur, un dictionnaire (et un tuple, qui
//  n'a pas de plan memoire) vivent dans la memoire de l'IHM - jamais dans celle
//  d'un equipement (hmi::types::isRich, unbindableReason).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::typeform {

enum class Form : std::uint8_t { Simple, Array, Array2D, List, Vector, Map, Tuple, Set, Ring };     // 1.12.7 : Set, Ring
inline constexpr Form kForms[] = {Form::Simple, Form::Array, Form::Array2D, Form::List, Form::Vector, Form::Map, Form::Tuple, Form::Set, Form::Ring};

[[nodiscard]] std::string_view label(Form) noexcept;          // "Simple", "Tableau", "Tableau 2D", "Liste", "Vecteur", "Dictionnaire (MAP)", "Tuple"
[[nodiscard]] std::string_view key(Form) noexcept;            // "simple", "tableau", "tableau2d", "liste", "vecteur", "map", "tuple"
[[nodiscard]] std::optional<Form> fromText(std::string_view labelOrKey) noexcept;   // sans casse, le libelle ou la cle
// Le parametre de la forme : son nom ("Bornes", "Cl\xC3\xA9", "Puis"), une aide, sa valeur par defaut ; vide : aucun.
[[nodiscard]] std::string_view parameterLabel(Form) noexcept;
[[nodiscard]] std::string_view parameterHint(Form) noexcept;
[[nodiscard]] std::string      defaultParameter(Form);
// Une phrase : ce qu'est la forme, et ce qu'elle permet (pour l'aide d'un dialogue).
[[nodiscard]] std::string_view summary(Form) noexcept;
// L'exemple de valeur initiale de cette forme ("[1, 2, 3]", "['a' := 1]").
[[nodiscard]] std::string_view valueHint(Form) noexcept;
// Taille variable (Liste, Vecteur, Dictionnaire) ; objet de l'IHM (et Tuple) : sans equipement.
[[nodiscard]] bool dynamic(Form) noexcept;
[[nodiscard]] bool memoryOnly(Form) noexcept;

// Les formes permises pour un usage (typereg::Use) : un operande, Simple seulement ; une variable
// IHM, une declaration, un retour et (1.12.4) un parametre de popup ou de symbole, toutes.
[[nodiscard]] bool allowed(Form, unsigned use) noexcept;
[[nodiscard]] std::vector<Form> forms(unsigned use);
[[nodiscard]] std::vector<std::string> labels(unsigned use);
// Une reference (REF_TO) : une declaration, un retour.
[[nodiscard]] bool referenceAllowed(unsigned use) noexcept;

struct Shape {
    Form        form{Form::Simple};
    std::string element{"INT"};      // le type des elements ("REAL", "T_Four", "ARRAY[0..2] OF INT"...)
    std::string parameter{};         // les bornes, la cle, les types suivants (vide : celui par defaut)
    bool        reference{false};    // REF_TO devant l'element
};
// Le texte du type : "ARRAY[0..9] OF REAL", "MAP[STRING] OF REF_TO T_Four", "TUPLE(INT, STRING)".
[[nodiscard]] std::string compose(const Shape&);
// L'inverse (la forme la plus exterieure) ; un type sans forme : Simple, l'element tel quel.
[[nodiscard]] Shape decompose(std::string_view type);

// LA VALEUR D'UN TYPE OBJET (une liste, un vecteur, une MAP, un tuple, ou un tableau ecrit
// [1, 2, 3]) : lue et rangee dans son type par le moteur, sans l'executer ailleurs - une
// valeur initiale, la valeur d'une constante. Faux et `why` (en francais) : illisible, ou qui
// ne va pas dans le type (trop de valeurs, une cle du mauvais type...). Vide : vrai.
bool valueFits(const Project&, std::string_view type, std::string_view value, std::string* why = nullptr);
// 1.12.19 : la valeur initiale d'un rappel s'ecrit =Fonction (la regle des cases : une formule qui la vise),
// Fonction, 'Fonction' ou NULL ; le moteur la lit sans le '=' de tete. Un autre type : telle quelle.
[[nodiscard]] std::string callbackInitial(std::string_view type, std::string_view value);
// 1.12.19 : un rappel de la forme `got` va-t-il ou `want` est attendu (la regle du moteur : le meme nombre de
// parametres, des types qui vont - les nombres entre eux -, le meme retour) ? Faux et `why` : non (un texte de type
// illisible : vrai, Generer le dit ailleurs).
bool callbackFitsText(const Project&, std::string_view want, std::string_view got, std::string* why = nullptr);
// 1.12.20 : les parties d'une signature de rappel (CALLBACK (x : REAL; n : INT) : BOOL) - ses parametres (nom, type ;
// un nom vide : par position) et son retour (vide : aucun). Faux et `why` : illisible, ou pas un rappel.
struct CallbackParts {
    std::vector<std::pair<std::string, std::string>> params;
    std::string                                      result;
};
bool callbackParts(const Project&, std::string_view text, CallbackParts& out, std::string* why = nullptr);
// Les elements d'un litteral de liste ("[1, 2, 'a,b']" -> {"1", "2", "'a,b'"}) ; faux : pas un
// litteral [..] ou (..). Une MAP : les entrees "cle := valeur". Pour l'editeur de liste.
bool literalItems(std::string_view text, std::vector<std::string>& out);
// L'inverse : "[1, 2, 3]" ; `tuple` : "(1, 'x')".
[[nodiscard]] std::string literalOf(const std::vector<std::string>& items, bool tuple = false);

// 1.12.2 : LES COLONNES D'UN TABLEAU DYNAMIQUE (rowsFrom) relie a une variable de ce type : une MAP,
// Cle et Valeur ; une liste (un vecteur, un tableau) de tuples, Item1, Item2... ; de structures, leurs
// membres ; de valeurs, Valeur. Les memes que le moteur (Runtime::tableRows), sans le faire tourner.
[[nodiscard]] std::vector<std::string> columnsOf(const Project&, std::string_view type);

} // namespace hmi::typeform
