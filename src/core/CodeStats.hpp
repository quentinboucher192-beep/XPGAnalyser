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
    {".hpp", 376, 62037},
    {".c", 0, 0},
    {".cpp", 492, 417834},
};
inline constexpr int        kTotalFiles = 868;
inline constexpr long long  kTotalLines = 479871;
inline constexpr const char* kCountedOn = "09/10/2026";

} // namespace xpg::codestats
