// =============================================================================
//  hmi/HmiActionKinds.hpp - 1.11.6 : les operations rangees, Maths, le clavier
//                           virtuel
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « trier les actions disponibles avec un
//  treeview utile et efficace » ; « maths : aide aux formules avec mini modal [...]
//  pouvoir ajouter n param qui seront des references obligatoirement avec un mode
//  test » ; « ouvrir un clavier virtuel champ de saisie (et on met des parametres) ».
//
//  LES OPERATIONS EN ARBRE. Chacune a sa famille (Variables, Navigation, Popups,
//  Scripts, Alarmes, Donnees, Medias, Utilisateur et poste) et une phrase qui dit
//  ce qu'elle fait : le choix de l'operation est un arbre qu'on filtre en tapant.
//
//  LES PARAMETRES D'UNE ACTION (Action::params) : "Nom := valeur; ..." comme les
//  arguments d'une popup.
//    - Maths : les REFERENCES de la formule. "Mesure := Armoires[0].ana.PT1.mes;
//      Consigne := Consigne_Four" ; la formule (Action::value) les nomme :
//      "(Mesure - Consigne) * 2". Chaque reference est un chemin de variable (pas
//      un calcul) : en marche, la formule se lit avec les chemins a la place des
//      noms, et son resultat va dans la cible.
//    - Clavier virtuel : les reglages de la saisie. "titre := 'Consigne du four';
//      clavier := numerique; min := 0; max := 1200; unite := 'degC'; masque := FALSE".
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::actionkinds {

// ---- les operations rangees ----------------------------------------------------
struct Group {
    std::string_view       name;    // « Variables »
    std::string_view       text;    // ce que la famille a en commun
    std::vector<Operation> operations;
};
// Les familles, dans l'ordre de l'arbre ; chaque operation de kOperations y est une fois.
[[nodiscard]] const std::vector<Group>& groups();
// La famille d'une operation (« Variables »).
[[nodiscard]] std::string_view groupOf(Operation) noexcept;
// Ce qu'elle fait, en une phrase (l'aide de l'arbre).
[[nodiscard]] std::string_view help(Operation) noexcept;

// ---- les parametres d'une action ------------------------------------------------
using Params = std::vector<std::pair<std::string, std::string>>;
[[nodiscard]] Params      params(const Action&);
[[nodiscard]] std::string formatParams(const Params&);
// Un reglage ("" : absent) ; le poser (une valeur vide le retire).
[[nodiscard]] std::string param(const Action&, std::string_view name);
void                      setParam(Action&, std::string_view name, std::string value);

// ---- Maths ----------------------------------------------------------------------
// La formule avec les chemins a la place des noms : "(Armoires[0].ana.PT1.mes - Consigne_Four) * 2".
[[nodiscard]] std::string mathsExpression(std::string_view formula, const Params& refs);
// Ce qui ne va pas (vide : rien). `badParam` : la reference fautive (-1 : la formule).
struct MathsIssue {
    std::string why;
    int         badParam{-1};
    bool        warning{false};   // a dire, sans empecher (une reference que la formule ne cite pas)
};
[[nodiscard]] std::vector<MathsIssue> checkMaths(const Params& refs, std::string_view formula);
// LE MODE TEST : la formule calculee avec ces valeurs a la place des references
// (une par reference, dans l'ordre ; vide : 0). `result` : la valeur ; sinon `why`.
struct MathsTest {
    bool        ok{false};
    std::string result;
    std::string expression;   // ce qui a ete calcule : "(12.5 - 10) * 2"
    std::string why;
};
[[nodiscard]] MathsTest testMaths(const Params& refs, const std::vector<std::string>& values, std::string_view formula);

// ---- le clavier virtuel ---------------------------------------------------------
struct KeyboardSpec {
    std::string title;          // une expression (un texte entre apostrophes, ou une variable) ; vide : le nom de la cible
    std::string keyboard;       // "auto" (selon le type de la cible), "numerique", "complet"
    std::string min, max;       // des expressions ; vides : pas de limite
    std::string unit;           // un texte : "degC"
    bool        mask{false};    // les caracteres caches (un code)
};
[[nodiscard]] KeyboardSpec keyboardSpec(const Action&);
void                       setKeyboardSpec(Action&, const KeyboardSpec&);
// Le clavier pour une cible de ce type : "numerique" (nombres) ou "complet".
[[nodiscard]] std::string_view keyboardFor(std::string_view type, std::string_view chosen);

} // namespace hmi::actionkinds
