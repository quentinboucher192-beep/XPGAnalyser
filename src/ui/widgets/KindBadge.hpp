#pragma once
// =============================================================================
//  ui/widgets/KindBadge.hpp - l'icone d'une proposition de l'aide a la saisie
// -----------------------------------------------------------------------------
//  1.11.2 (API-V, decisions 161 et 163) : devant chaque proposition, une icone
//  qui dit CE QUE C'EST (la nature : par la forme, un pictogramme au trait) et
//  D'OU CA VIENT (la provenance : par la couleur du trait). C'est la scene 4 de
//  la maquette de MQ5 (« Icones de saisie ») : des pictogrammes de 16 x 16, un
//  trait de 1,3, quelques points pleins, aucune lettre. Le widget ne sait rien
//  des natures ni des provenances : l'appli (app/hmi/HmiAssist) remplit l'icone
//  et la cle des couleurs, la liste les trace.
//
//  TRACEE, JAMAIS ECRITE (decision 155, les « ? » du centre d'aide) : des traits,
//  des disques et des carres. Aucune police n'y intervient : ni Segoe UI sous
//  Windows, ni une autre, ne peut la remplacer par une boite.
// =============================================================================
#include "../../platform/Geometry.hpp"
#include "../../platform/Renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ui {

struct KindBadge {
    // Un trait du pictogramme, dans un carre de 16 x 16 (le viewBox de la maquette).
    struct Seg { float x0, y0, x1, y1; };
    // Un point : un disque plein de rayon r, un carre plein de demi-cote r, ou un cercle au trait.
    enum class DotKind : std::uint8_t { Disc, Square, Ring };
    struct Dot { float x, y, r; DotKind kind; };
    struct Picto {
        const Seg*  segs;
        std::size_t segCount;
        const Dot*  dots;
        std::size_t dotCount;
    };

    const Picto* picto{ nullptr };   // nullptr : pas d'icone (la liste trace l'icone d'avant)
    gfx::Color   color{};            // la provenance
    std::string  legend;             // en mots : « methode · objet »
    bool         accent{ false };    // le nom aussi dans la couleur (une methode d'objet : violet)

    [[nodiscard]] bool set() const noexcept { return picto != nullptr; }
};

// L'icone dans `box` (un carre ; centree sur le plus petit cote sinon).
void drawKindBadge(gfx::IRenderer& r, const KindBadge& badge, const gfx::Rect& box);

// La cle des couleurs, au pied de la liste (« La couleur dit d'ou ca vient : »
// puis une pastille et un mot par provenance). Posee par l'appli ; vide : le pied
// ne dit que la ligne choisie.
struct BadgeKeyEntry { std::string_view label; gfx::Color color; };
void setBadgeKey(const BadgeKeyEntry* entries, std::size_t count) noexcept;
[[nodiscard]] std::size_t badgeKeyCount() noexcept;
[[nodiscard]] const BadgeKeyEntry* badgeKey() noexcept;

} // namespace ui
