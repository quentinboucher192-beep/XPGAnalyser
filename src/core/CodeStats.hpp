// =============================================================================
//  core/CodeStats.hpp - 1.11.7 : le nombre de lignes de code de l'application
// -----------------------------------------------------------------------------
//  ECRIT PAR outils/compter_lignes.py (ne pas modifier a la main) : les fichiers
//  .h, .hpp, .c et .cpp de src, tests et tools. L'ecran d'accueil et A propos
//  l'affichent.
// =============================================================================
#pragma once

namespace xpg::codestats {

struct ByExtension {
    const char* extension;
    int         files;
    long long   lines;
};

inline constexpr ByExtension kByExtension[] = {
    {".h", 0, 0},
    {".hpp", 392, 64855},
    {".c", 0, 0},
    {".cpp", 521, 442459},
};
inline constexpr int        kTotalFiles = 913;
inline constexpr long long  kTotalLines = 507314;
inline constexpr const char* kCountedOn = "10/10/2026";

} // namespace xpg::codestats
