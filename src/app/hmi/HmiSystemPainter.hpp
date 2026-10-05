// =============================================================================
//  app/hmi/HmiSystemPainter.hpp - le dessin du menu natif "Parametres
//                                 systeme", de la veille et de la luminosite
//                                 (lot 10)
// -----------------------------------------------------------------------------
//  Ce que l'IHM dessine d'elle-meme, par-dessus ses vues, dans son ECRAN (le
//  cadre de la vue courante a l'ecran) : le menu (hmi/HmiSystemMenu : la meme
//  geometrie pour le dessin et le clic), l'ecran eteint, le voile de la
//  luminosite. La simulation et les exemples animes de l'aide s'en servent.
// =============================================================================
#pragma once

#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSystemMenu.hpp"
#include "../../platform/Renderer.hpp"

namespace ui { struct Theme; }

namespace app {

// Le menu, centre dans `screen` ; rend la geometrie dessinee (dans le repere
// de `screen` : ajouter screen.x, screen.y). 1.9 : `slaves` - les esclaves
// simules (avec leurs lignes) que montre la page Simulation ; nul : le moteur
// les lit lui-meme (Runtime::simSlaves).
hmi::SystemMenuLayout paintSystemMenu(gfx::IRenderer&, const ui::Theme&, const hmi::Runtime&, const gfx::Rect& screen,
                                      const std::vector<hmi::SimSlave>* slaves = nullptr);
// L'ecran eteint (veille) : noir, et une phrase discrete.
void paintSleepScreen(gfx::IRenderer&, const ui::Theme&, const gfx::Rect& screen);
// La luminosite : un voile noir (100 % : rien).
void paintBrightness(gfx::IRenderer&, const gfx::Rect& screen, int brightness);

} // namespace app
