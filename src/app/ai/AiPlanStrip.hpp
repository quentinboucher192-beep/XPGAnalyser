// =============================================================================
//  app/ai/AiPlanStrip.hpp - 1.20.2 : LE PLAN EN COURS, en haut de la conversation
// -----------------------------------------------------------------------------
//  Le plan d'action d'une proposition (1.19.0) etait dans sa carte : une longue
//  conversation le faisait sortir de l'ecran pendant qu'il travaillait. Une bande
//  au-dessus de la discussion, qui ne defile pas, montre maintenant LE PLAN EN
//  COURS : celui de la proposition qui se verifie ou s'applique sur un fil de
//  fond, sinon celui de la derniere proposition de la conversation (a valider,
//  appliquee). Repliee : une ligne (le plan, son etat) et la barre d'avancement ;
//  depliee (un clic) : les phases et chaque etape avec son etat - En attente,
//  Verifiee, En cours, Reussie, Echec, Ignoree, Annulee, Retiree. Un clic sur une
//  etape amene a la carte (son plan deplie) ; la croix cache la bande pour cette
//  proposition (la suivante la remontre).
//
//  Les etats sont REELS : pendant l'application sur un fil de fond, seul
//  l'avancement global est connu - les etapes disent << En cours >>, jamais une
//  reussite qui n'a pas eu lieu ; le compte rendu les remplace a la fin.
// =============================================================================
#pragma once

#include "../../ui/Widget.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiPanel;

class AiPlanStrip final : public ui::Widget {
public:
    AiPlanStrip(std::string id, AiPanel& panel);

    // La hauteur voulue a cette largeur (0 : rien a montrer), au plus `maxH`.
    [[nodiscard]] float heightFor(float width, float maxH) const;
    [[nodiscard]] bool  expanded() const noexcept { return open_; }
    void                setExpanded(bool open);
    // Cache la bande pour la proposition montree (la croix) ; la suivante la remontre.
    void closeCurrent();
    // Ce que la bande dit (les essais) : l'en-tete, l'etat, l'avancement, puis une ligne par etape montree.
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] gfx::Rect headRect() const;
    [[nodiscard]] gfx::Rect closeRect() const;
    // La ligne de l'etape `index` (depliee ; vide : pas montree).
    [[nodiscard]] gfx::Rect stepRect(std::size_t index) const;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] std::size_t visibleSteps() const;
    [[nodiscard]] float       stepsTop() const;
    AiPanel&              panel_;
    bool                  open_{false};
    std::set<std::string> closed_;   // les propositions dont la bande est cachee
    float                 scroll_{0.f};
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

} // namespace app::ai
