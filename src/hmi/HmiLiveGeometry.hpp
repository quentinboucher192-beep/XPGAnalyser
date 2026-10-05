// =============================================================================
//  hmi/HmiLiveGeometry.hpp - 1.11.4 : la position, la taille et la rotation
//                            calculees EN MARCHE suivent jusqu'au dessin
// -----------------------------------------------------------------------------
//  LA CAPTURE DU CLIENT DU 05/10 : X d'une instance de symbole = =$UINTS[0]$ ;
//  en simulation, l'instance ne bouge pas. LiveView calculait bien x (l'onglet
//  Expressions le montrait), mais en marche une instance ne dessine rien : ce
//  sont les objets de son symbole, poses UNE FOIS par l'expansion
//  (expandInstances) a la place STATIQUE de l'instance, qu'on voit. Un groupe
//  avait le meme defaut (ses enfants sont en coordonnees de la vue), et un objet
//  du symbole qui avait sa propre formule de position recevait la valeur du
//  repere du symbole comme si c'etait celle de la vue.
//
//  APRES LE CALCUL DES FORMULES, relayout() REPOSE :
//   - une INSTANCE dont la geometrie calculee (x, y, w, h, rot, flipH, flipV)
//     differe de la statique, ou dont un objet a sa propre formule de geometrie :
//     chaque objet de son symbole repart de sa geometrie DANS LE SYMBOLE (notee
//     par l'expansion, kLocalGeometryKey), prend la valeur de sa formule s'il en
//     a une (dans le repere du symbole), puis est pose dans l'instance du moment
//     par la meme regle que l'expansion (placeInInstance : echelle, miroirs,
//     rotation, tailles de texte et de trait) ;
//   - un GROUPE dont la geometrie calculee differe : tout ce qu'il contient suit
//     (deplace, mis a l'echelle dans son repere, tourne autour de son pivot).
//  Des parents vers les enfants : une instance dans un groupe, un groupe dans un
//  symbole, une instance dans un symbole se composent.
//
//  Sans formule de geometrie, rien n'est fait (la vue reste celle de l'expansion).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace hmi {

// Notes posees par l'expansion dans la vue composee (jamais enregistrees) :
// sur chaque objet d'une instance, sa geometrie dans le repere du symbole ;
// sur l'instance, la taille du symbole ("largeur;hauteur").
inline constexpr std::string_view kLocalGeometryKey = "@symLocal";
inline constexpr std::string_view kSymbolSizeKey = "@symSize";

// Les proprietes geometriques qu'une formule peut piloter.
[[nodiscard]] bool isLiveGeometryKey(std::string_view key) noexcept;

// La geometrie d'un objet du symbole, avant sa pose (pour kLocalGeometryKey).
[[nodiscard]] std::string encodeLocalGeometry(const Object&);
// Remet sur `o` la geometrie notee (les cles absentes de la note sont retirees).
void restoreLocalGeometry(Object& o, std::string_view note);

// Pose un objet du symbole (repere du symbole) dans l'instance, comme l'expansion
// (HmiSymbols.cpp) : `symbolWidth` x `symbolHeight` est la taille du symbole.
void placeInInstance(Object& o, const Object& instance, double symbolWidth, double symbolHeight);

// `out` : la vue evaluee ; `source` : la vue liee (statique) ; `applied` : les
// (objet, propriete) dont la formule de geometrie a donne une valeur.
void relayoutLive(View& out, const View& source, const std::set<std::pair<Id, std::string>>& applied);

} // namespace hmi
