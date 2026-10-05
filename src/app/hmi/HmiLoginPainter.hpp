// =============================================================================
//  app/hmi/HmiLoginPainter.hpp - le dessin du menu natif de connexion (lot 12)
// -----------------------------------------------------------------------------
//  Comme Parametres systeme (HmiSystemPainter) : par-dessus les vues, dans
//  l'ECRAN de l'IHM ; la geometrie vient de hmi/HmiLoginMenu (la meme pour le
//  dessin et le clic). Quand le clavier virtuel est la, le menu se range au-
//  dessus de lui (`areaH` : la hauteur libre, depuis le haut de l'ecran).
// =============================================================================
#pragma once

#include "../../hmi/HmiLoginMenu.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../platform/Renderer.hpp"

namespace ui { struct Theme; }

namespace app {

// Les lignes des onglets Comptes et Journal, les groupes et les roles d'Acces :
// ce que le moteur montre (le dessin et le clic comptent pareil).
[[nodiscard]] hmi::LoginMenuLayout loginMenuLayoutFor(const hmi::Runtime&, float w, float areaH);
// Le menu ; rend la geometrie dessinee (dans le repere de `screen`).
hmi::LoginMenuLayout paintLoginMenu(gfx::IRenderer&, const ui::Theme&, const hmi::Runtime&, const gfx::Rect& screen, float areaH);
// La derniere ligne montree possible (les fleches, la molette).
[[nodiscard]] std::size_t loginMaxScroll(const hmi::Runtime&, const hmi::LoginMenuLayout&);

} // namespace app
