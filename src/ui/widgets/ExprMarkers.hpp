// =============================================================================
//  ui/widgets/ExprMarkers.hpp - les REPERES ($Vanne$) d'une case de PropertyGrid
// -----------------------------------------------------------------------------
//  1.10.3 (demande du client : "quand je mets un placeholder avec les '$', je ne
//  le sais pas vraiment"). Une case dont la valeur ou l'expression contient un
//  repere se voit :
//    - sa pastille (fx pleine, ou fx en creux d'un texte) passe en AMBRE et dit "$" ;
//    - chaque repere est SURLIGNE en ambre dans la valeur ;
//    - l'infobulle : "Repere $Vanne$ : Dupliquer... (Ctrl+D) le remplace. Tant
//      qu'il reste, Compiler le signale et la simulation ne calcule pas cette case."
//  ui/ ne connait pas le modele : l'application donne le lecteur de reperes
//  (hmi::dup::markersIn, HmiEditor) ; sans lui, aucune case n'a de repere.
// =============================================================================
#pragma once

#include "DataViews.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ui::exprfield {

struct MarkerSpan {
    std::size_t at{0};          // le premier $
    std::size_t length{0};      // `$Nom$` entier
    std::string name;           // sans les $
};
// 1.11 (REP) : `expression` - un $ entre apostrophes n'y est jamais un repere ; un
// texte (libelle, message) : seulement dans ses trous {...}.
using MarkerFinder = std::vector<MarkerSpan> (*)(std::string_view text, bool expression);
void setMarkerFinder(MarkerFinder finder);

// Les reperes d'un texte (vide sans lecteur).
[[nodiscard]] std::vector<MarkerSpan> markersIn(std::string_view text, bool expression = true);
// Ceux de la case : son expression, sinon sa valeur (ce que la grille montre).
[[nodiscard]] std::vector<MarkerSpan> markersOf(const PropertyGrid::Property&);
[[nodiscard]] bool hasMarker(const PropertyGrid::Property&);
// L'infobulle ; "" : pas de repere.
[[nodiscard]] std::string markerTip(const PropertyGrid::Property&);

} // namespace ui::exprfield
