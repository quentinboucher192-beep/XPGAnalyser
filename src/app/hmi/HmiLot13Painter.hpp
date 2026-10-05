// =============================================================================
//  app/hmi/HmiLot13Painter.hpp - ce que l'IHM dessine d'elle-meme au lot 13
// -----------------------------------------------------------------------------
//  LE PANNEAU DE SIGNATURE (hmi/HmiSignature.hpp) : par-dessus la vue, comme le
//  menu de connexion ; la geometrie est celle du clic (signatureLayout). Quand
//  le clavier virtuel est la, le panneau se range au-dessus (`areaH`).
//
//  L'AVERTISSEMENT DE DECONNEXION : un bandeau en haut de l'ecran de l'IHM
//  compte les dernieres secondes ; son bouton (ou n'importe quel toucher)
//  garde l'utilisateur connecte.
// =============================================================================
#pragma once

#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSignature.hpp"
#include "../../platform/Renderer.hpp"

namespace ui { struct Theme; }

namespace app {

[[nodiscard]] hmi::SignatureLayout signatureLayoutFor(const hmi::Runtime&, float w, float areaH);
// Le panneau ; rend la geometrie dessinee (dans le repere de `screen`).
hmi::SignatureLayout paintSignaturePanel(gfx::IRenderer&, const ui::Theme&, const hmi::Runtime&, const gfx::Rect& screen, float areaH);

// Le bandeau et son bouton, a l'ecran (vides : pas d'avertissement).
struct LogoutWarningRects {
    gfx::Rect bar, button;
};
LogoutWarningRects paintLogoutWarning(gfx::IRenderer&, const ui::Theme&, const hmi::Runtime&, const gfx::Rect& screen);

} // namespace app
