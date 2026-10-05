#pragma once
// =============================================================================
//  platform/AtlasSymbols.hpp — ce que l'atlas a TOUJOURS (1.11.2, T2, decision 155)
// -----------------------------------------------------------------------------
//  POURQUOI. L'atlas (FontAtlas) n'a qu'une police, sans repli : sous Windows,
//  segoeui.ttf (sinon tahoma.ttf) ; sous Linux, DejaVu Sans. Un code que la
//  police n'a pas dessine U+FFFD, le losange au « ? » : c'est ce que le client a
//  vu dans le centre d'aide de la 1.11.1 (▶ ▸ ▾ ★ ☆, que Segoe UI n'a pas, alors
//  que « › ‹ • » de Windows-1252 passent). Une appli Windows ordinaire ne le voit
//  pas, parce que le systeme lui prete une autre police ; stb_truetype ne prete
//  rien.
//
//  LA REGLE. Une chaine de l'interface ne contient que :
//    * le repertoire que toute police chargee a toujours (alwaysInFace) :
//      Windows-1252, le latin etendu A, le grec et le cyrillique de base
//      (Segoe UI et Tahoma couvrent les pages 1250 a 1253 ; DejaVu Sans et
//      Liberation Sans aussi) ;
//    * ou un symbole que l'ATLAS DESSINE LUI-MEME (drawnByAtlas), au trait, a la
//      taille de la police, sur toutes les machines de la meme facon (comme les
//      quatre symboles de T3 dans l'arbre, mais pour toute l'appli) : la police
//      n'est jamais consultee pour eux, donc une capture Linux montre ce que
//      Windows montre.
//  L'essai glyphesWindows1112 (tests/hmi_editor_test.cpp) relit toutes les
//  chaines des sources et du guide, et echoue sur tout autre caractere.
//
//  UN SYMBOLE DE PLUS : l'ajouter a kDrawnSymbols ET a son trace dans
//  FontAtlas.cpp (symbolShape) ; un symbole liste sans trace dessinerait la
//  boite (l'essai de l'atlas le dit).
// =============================================================================
#include <cstddef>

namespace gfx::atlas {

// Windows-1252 : ASCII, Latin-1 et les 27 de 0x80-0x9F.
constexpr bool inWindows1252(char32_t c) noexcept {
    if (c >= 0x20 && c <= 0x7E) return true;
    if (c >= 0xA0 && c <= 0xFF) return true;
    switch (c) {
    case 0x0152: case 0x0153: case 0x0160: case 0x0161: case 0x0178: case 0x017D: case 0x017E:
    case 0x0192: case 0x02C6: case 0x02DC:
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201A: case 0x201C: case 0x201D:
    case 0x201E: case 0x2020: case 0x2021: case 0x2022: case 0x2026: case 0x2030: case 0x2039:
    case 0x203A: case 0x20AC: case 0x2122:
        return true;
    default:
        return false;
    }
}

// Ce que toute police que l'atlas peut charger a toujours.
constexpr bool alwaysInFace(char32_t c) noexcept {
    if (inWindows1252(c)) return true;
    if (c >= 0x0100 && c <= 0x017F) return true;    // latin etendu A (1250, 1257)
    if (c >= 0x0384 && c <= 0x03CE) return true;    // grec (1253)
    if (c >= 0x0401 && c <= 0x045F) return true;    // cyrillique (1251)
    if (c == 0x0490 || c == 0x0491) return true;
    return false;
}

// Les symboles que l'atlas dessine lui-meme (FontAtlas::glyph ne consulte pas la
// police pour eux). Les espaces fines ont une avance et rien a dessiner ; les
// codes de largeur nulle (U+200B, U+FEFF, U+0336 que TrailSymbols trace
// lui-meme...) n'ont ni l'une ni l'autre.
inline constexpr char32_t kDrawnSymbols[] = {
    // triangles et formes
    0x25B6, 0x25BA, 0x25B8, 0x25C0, 0x25C4, 0x25C2, 0x25BC, 0x25BE, 0x25B2, 0x25B4,
    0x25CF, 0x25CB, 0x25C6, 0x25A0, 0x25A1,
    // etoiles (favoris)
    0x2605, 0x2606,
    // coches et croix
    0x2713, 0x2714, 0x2715, 0x2716, 0x2717, 0x2718, 0x2298,
    // fleches
    0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x2195, 0x2197, 0x2198,
    0x21B3, 0x21B5, 0x21BA, 0x21BB, 0x27F2, 0x27F3, 0x21C4, 0x21E7, 0x21E9,
    // divers
    0x26A0, 0x270E, 0x2212, 0x2264, 0x2265, 0x2260, 0x2261, 0x22EF, 0x232B,
    // espaces fines (avance seule)
    0x2009, 0x200A, 0x202F,
    // largeur nulle
    0x200B, 0x200C, 0x200D, 0x2060, 0xFEFF, 0x0336,
};

constexpr bool drawnByAtlas(char32_t c) noexcept {
    for (const char32_t s : kDrawnSymbols)
        if (s == c) return true;
    return false;
}

// Ce qu'une chaine de l'interface peut contenir : les controles que la mise en
// page traite (tabulation, fins de ligne), le repertoire sur, les symboles
// dessines.
constexpr bool safeForUi(char32_t c) noexcept {
    return c == U'\t' || c == U'\n' || c == U'\r' || alwaysInFace(c) || drawnByAtlas(c);
}

} // namespace gfx::atlas
