#pragma once
// 1.11 (chantier T3, C4) : les quatre symboles des icones compilable / generable,
// ✓ ✕ ⊘ ⇩, TRACES AU TRAIT et non ecrits avec la police. L'interface n'a qu'une
// police, sans repli (platform/FontAtlas) ; DejaVu Sans les a, mais rien ne
// garantit que la police du poste (Segoe UI sous Windows) les ait : on verrait la
// boite de remplacement. L'arbre (DataViews.cpp) et les infobulles
// (WidgetHost::paintTooltip : la legende du « ? », les resumes d'une unite) les
// dessinent de la meme facon. Tranche 11 (decision du chef) : la legende aussi,
// sans police de plus.
// Un symbole suivi de U+0336 (trait long couvrant, kStruck) est dessine barre :
// « ⇩ barre », un script qui ne part pas avec Generer.
#include "../../platform/Renderer.hpp"
#include "../Theme.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ui::trailsym {

enum class Sym : std::uint8_t { None, Check, Cross, Ban, Down };

inline constexpr std::string_view kStruck = "\xCC\xB6";    // U+0336 apres le symbole : barre

[[nodiscard]] Sym at(std::string_view s, std::size_t pos);   // le symbole qui commence a pos, ou None
[[nodiscard]] bool contains(std::string_view s);              // au moins un des quatre
// Le ton d'un symbole, comme dans l'arbre : ✓ Ok, ✕ Error, ⊘ Muted, ⇩ Info, ⇩ barre Warning.
[[nodiscard]] Tone toneOf(Sym sym, bool struck);
void drawSymbol(gfx::IRenderer& r, Sym sym, float x, float cy, float side, gfx::Color col);
// La largeur d'un texte ou les symboles sont traces (side + 2 chacun) et le reste ecrit.
[[nodiscard]] float width(gfx::IRenderer& r, std::string_view text, gfx::FontId font, float side);
// Le trace : le texte en col ; un symbole en col, ou, si own est donne, dans la couleur de son ton.
void draw(gfx::IRenderer& r, std::string_view text, gfx::FontId font, float x, float textY, float cy, float side,
          gfx::Color col, const Theme* own = nullptr);

} // namespace ui::trailsym
