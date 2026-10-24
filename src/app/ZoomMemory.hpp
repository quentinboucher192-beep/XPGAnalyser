// =============================================================================
//  app/ZoomMemory.hpp - 1.12.31 : le zoom garde d'une seance a l'autre
// -----------------------------------------------------------------------------
//  Le zoom d'une page (et celui de l'arbre du projet, du panneau du bas) se
//  garde dans les reglages de l'utilisateur (app/Settings : settings.txt de
//  chaque application), sous "zoom.<identifiant du widget>" : "zoom.hmi.view.176",
//  "zoom.analysis.api.variables", "zoom.analysis.explorerColumn". La page rouverte
//  (le meme identifiant) le retrouve. Revenir a 100 % l'oublie : la cle s'en va.
//
//  Pur : zoom_test l'ecrit et le relit (un fichier de reglages enregistre, puis
//  recharge). L'ecran (StatusStripWorkspace.cpp) s'en sert ; App dit dans quels
//  reglages (ceux de l'utilisateur ; une session rejouee : les siens).
// =============================================================================
#pragma once

#include "Settings.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace app::zoommemory {

// Les bornes du zoom d'une page (la puce : 50 a 200 %).
inline constexpr float kMin = 0.5f;
inline constexpr float kMax = 2.f;

// La cle d'un widget ; "" : il n'en a pas (pas d'identifiant, il ne se garde pas).
[[nodiscard]] inline std::string keyFor(std::string_view id) {
    if (id.empty()) return {};
    return "zoom." + std::string(id);
}

// Le zoom retenu pour ce widget ; 1 : rien (ou une valeur illisible, hors bornes : 1 aussi).
// `lo`, `hi` : ses bornes (une page : 50 a 200 % ; un code : jusqu'a 300 %).
[[nodiscard]] inline float read(const Settings& s, std::string_view id, float lo = kMin, float hi = kMax) {
    const auto key = keyFor(id);
    if (key.empty()) return 1.f;
    const float z = s.getFloat(key, 1.f);
    if (!std::isfinite(z) || z < lo - 1e-3f || z > hi + 1e-3f) return 1.f;
    return std::clamp(z, lo, hi);
}

// Retenir le zoom de ce widget ; 100 % l'oublie. Vrai : les reglages ont change (a enregistrer).
inline bool write(Settings& s, std::string_view id, float zoom, float lo = kMin, float hi = kMax) {
    const auto key = keyFor(id);
    if (key.empty() || !std::isfinite(zoom)) return false;
    const float z = std::clamp(zoom, lo, hi);
    if (std::fabs(z - 1.f) < 1e-3f) {
        if (s.getString(key).empty()) return false;
        s.remove(key);
        return true;
    }
    if (std::fabs(s.getFloat(key, 1.f) - z) < 1e-4f && !s.getString(key).empty()) return false;
    s.set(key, z);
    return true;
}

} // namespace app::zoommemory
