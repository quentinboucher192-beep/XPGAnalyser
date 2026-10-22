// =============================================================================
//  app/hmi/HmiPidPainter.hpp - 1.12.24 : la fenetre du Reglage PID
// -----------------------------------------------------------------------------
//  Ce que l'IHM dessine d'elle-meme par-dessus ses vues quand une carte Reglage
//  PID est cliquee : sa fenetre, quatre onglets (Statistiques, Ce qu'il a
//  appris, Reglage propose, Journal), d'apres la maquette validee. Elle lit la
//  boucle du moteur (hmi::PidLoop) sans rien changer ; ses clics reviennent par
//  les parties de PidPanelLayout (Runtime::pidPart). La carte elle-meme se
//  dessine avec les objets de la vue (drawPidTuner, HmiPaintKit.hpp).
// =============================================================================
#pragma once

#include "../../hmi/HmiRuntime.hpp"
#include "../../platform/Renderer.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ui { struct Theme; }

namespace app {

struct PidPanelLayout {
    gfx::Rect panel;                                           // la fenetre, a l'ecran
    std::vector<std::pair<std::string, gfx::Rect>> parts;      // ce qui se clique (dans l'ordre du dessin)
    // La courbe des Statistiques : son cadre et l'heure de ses bords (le survol la lit).
    gfx::Rect chart;
    double    t0{0}, t1{0};
};

// La fenetre, centree dans `screen` (le cadre de la vue a l'ecran).
PidPanelLayout paintPidPanel(gfx::IRenderer&, const ui::Theme&, const hmi::Runtime&, const gfx::Rect& screen);
// La partie sous le point (du dessus vers le dessous) ; "dehors" : hors de la fenetre ; "" : rien.
[[nodiscard]] std::string pidPanelHit(const PidPanelLayout&, gfx::Point);
// L'heure survolee sur la courbe des Statistiques ; < 0 : hors d'elle.
[[nodiscard]] double pidPanelHoverTime(const PidPanelLayout&, gfx::Point);

} // namespace app
