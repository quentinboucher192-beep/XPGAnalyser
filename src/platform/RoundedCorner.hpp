// =============================================================================
//  platform/RoundedCorner.hpp — la geometrie d'un coin arrondi
// -----------------------------------------------------------------------------
//  Une fonction, sortie du renderer pour une seule raison : elle est verifiable
//  sans ecran, et le renderer ne l'est pas. Les machines de test n'ont pas SDL,
//  donc tout ce qui vit dans SdlRenderer.cpp n'y est jamais compile - et une
//  formule fausse s'y cacherait jusqu'a ce que quelqu'un regarde un bouton.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace gfx {

// De combien le bord rentre, a la hauteur y comptee depuis le haut du coin.
//
// Le coin est un quart de cercle de rayon `radius`, centre a (radius, radius).
// A la hauteur y, le cercle est a la distance sqrt(r^2 - (r-y)^2) de la gauche
// du carre, donc le bord rentre de r moins cette distance.
//
// y EST UN CENTRE DE PIXEL, pas son bord. L'appelant passe y + 0.5 : mesurer la
// couverture au bord du pixel decale toute la courbe d'un demi-pixel vers le
// haut, ce qui se voit comme un aplatissement du coin.
[[nodiscard]] inline float cornerInset(float radius, float y) noexcept {
    if (radius <= 0.f) return 0.f;
    const float dy = radius - std::clamp(y, 0.f, radius);
    const float dedans = std::sqrt(std::max(0.f, radius * radius - dy * dy));
    return std::max(0.f, radius - dedans);
}

} // namespace gfx
