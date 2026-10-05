// =============================================================================
//  app/hmi/HmiValueKind.hpp - 1.11.3 : le carre de legende, et ce qu'il sait
// -----------------------------------------------------------------------------
//  A DROITE DE CHAQUE CASE DE L'INSPECTEUR, UN CARRE DIT D'OU VIENT LA VALEUR :
//    C   une constante, convertie dans le type de la case (Voiture -> 'Voiture')
//    fx  une formule : un calcul, un appel, plusieurs sources (des points sous
//        la lettre : les zones qu'elle lit)
//    $   une formule ou un texte avec des reperes $...$
//    A   une variable de l'automate        I  une variable de l'IHM
//    S   une variable systeme (SYS.)       V  un parametre du symbole ou de la
//                                             vue, une variable publique, THIS
//    !   une erreur : un nom inconnu, une conversion impossible, un type qui ne
//        convient pas, une valeur hors des bornes
//  Une formule qui n'est qu'une variable montre le carre de la variable (UINTS
//  -> I), pas fx.
//
//  classify() LE CALCULE QUAND LA VALEUR CHANGE (l'inspecteur refait ses cases),
//  jamais a chaque image. Il rend aussi les erreurs et LEURS CORRECTIONS :
//  remplacer un nom mal tape par le plus proche, prendre un mot comme texte,
//  convertir (INT_TO_STRING), ramener dans les bornes, creer la variable qui
//  manque. Le selecteur de valeur (HmiValuePicker) les propose d'un clic.
// =============================================================================
#pragma once

#include "../../ui/widgets/DataViews.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }
namespace hmi { struct Project; struct View; }

namespace app::valuekind {

using Style = ui::PropertyGrid::LegendStyle;

struct KindInfo {
    Style            style;
    std::string_view letter;    // "C", "fx", "$", "A", "I", "S", "V", "!"
    std::string_view name;      // "Constante", "Variable API"...
    std::string_view meaning;   // ce qu'il veut dire
};
// Les huit carres, dans l'ordre de la liste (C, fx, $, A, I, S, V, !).
[[nodiscard]] const std::vector<KindInfo>& kinds();
[[nodiscard]] const KindInfo& info(Style);
// Le carre seul (la liste des carres, la legende du selecteur).
[[nodiscard]] ui::PropertyGrid::Legend legendOf(Style);

struct Fix {
    std::string label;          // "Remplacer par Pression_Max (API, REAL)"
    std::string value;          // le texte entier apres la correction
    bool        fx{true};       // une formule (vrai) ou une constante
    std::string create;         // non vide : creer cette variable (le dialogue), rien d'autre
};
struct Diag {
    std::string      message;   // "Pressoin_Max n'existe pas."
    std::vector<Fix> fixes;
};
struct Result {
    bool                     empty{true};
    Style                    style{Style::Empty};
    std::string              info;       // "Variable IHM : UINTS - ARRAY[0..9] OF UINT"
    std::string              literal;    // une constante : son litteral ST ('Voiture')
    std::string              type;       // une variable seule : son type
    std::vector<Style>       sources;    // une formule : les zones lues (A, I, S, V)
    std::vector<Diag>        diags;
    std::vector<std::string> unknown;    // les noms inconnus qu'on peut creer
    [[nodiscard]] bool error() const noexcept { return style == Style::Error; }
};

struct Env {
    const hmi::Project*    project{nullptr};
    const hmi::View*       view{nullptr};    // la vue de la case : ses parametres, ses objets
    const domain::Project* plc{nullptr};
};

// La zone (Api, Hmi, System, Local) et le type d'un chemin : UINTS, Armoires[1].Pression,
// SYS.UserName, API.Mode, Name (un parametre de la vue). Faux : inconnu. `detail` :
// l'adresse, la valeur initiale, le commentaire (une ligne).
bool pathInfo(const Env&, std::string_view path, Style& zone, std::string& type, std::string* detail = nullptr);
// Le type `actual` convient-il a `expected` ? "" ou ANY : tout ; TEXTE (un texte a
// trous) : tout ; COULEUR : un texte ; NOMBRE : un nombre ; sinon la regle des
// parametres (hmi::params::typeAccepts : INT dans REAL, les memes bornes d'un tableau).
[[nodiscard]] bool compatible(std::string_view expected, std::string_view actual);
// Le texte d'une case : une constante (fx faux) ou une formule (fx vrai), pour le
// type attendu (BOOL, ARRAY[0..9] OF UINT, STRING, TEXTE, COULEUR, NOMBRE, "" : tout).
[[nodiscard]] Result classify(const Env&, std::string_view text, bool fx, std::string_view expected);
// Le carre de la grille pour ce resultat, avec son infobulle.
[[nodiscard]] ui::PropertyGrid::Legend legendOf(const Result&);
// Les noms connus les plus proches de `name` (pour "Remplacer par...") ; au plus `limit`.
[[nodiscard]] std::vector<std::string> closestNames(const Env&, std::string_view name, std::size_t limit = 2);
// Le type attendu d'une case de l'inspecteur, d'apres ce qu'elle accepte : BOOL,
// NOMBRE, STRING, TEXTE (texte a trous), COULEUR, TIME ; "" : tout.
[[nodiscard]] std::string expectedOfProperty(const ui::PropertyGrid::Property&);
// Le libelle d'un type attendu pour l'utilisateur : "texte", "nombre", "couleur" ou le type.
[[nodiscard]] std::string expectedLabel(std::string_view expected);
// Une phrase sans balise : le premier diagnostic (la note rouge sous la case).
[[nodiscard]] std::string firstProblem(const Result&);

} // namespace app::valuekind
