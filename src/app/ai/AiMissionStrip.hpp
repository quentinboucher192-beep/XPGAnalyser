// =============================================================================
//  app/ai/AiMissionStrip.hpp - 1.22.0 : LA MISSION ACTIVE dans le panneau, et << Creer une mission IA ? >>
// -----------------------------------------------------------------------------
//  AiMissionStrip : une bande au-dessus de la conversation (comme << Plan en
//  cours >>, AiPlanStrip), qui ne defile pas avec elle. Elle montre la mission
//  de la conversation ouverte (Assistant::activeMission) : son nom, son etat
//  (sa couleur), les etapes terminees sur le total et la barre de la
//  progression globale (mission::progressOf - jamais un pourcentage invente),
//  la prochaine action, les propositions de la mission et leur etat, les
//  erreurs et les blocages. Son bouton fait le geste du moment (<< Proposer la
//  suite >> quand des etapes sont pretes, Corriger, Actualiser le contexte,
//  Reprendre...) ; un clic ailleurs ouvre le tableau de bord (au centre). Le
//  chevron la replie a deux lignes (le nom, la barre) : elle ne gene pas la
//  discussion.
//
//  AiMissionAsk : la carte de choix au-dessus de la saisie, quand une demande
//  envoyee est complexe (Assistant::suggestsMission) - rien n'est encore parti :
//  << Creer la mission >> ou << Continuer en conversation >> (l'envoi normal) ;
//  la croix (ou Echap) la ferme sans rien envoyer, le texte reste dans la saisie.
//
//  Rien de long au dessin : ce qu'elles montrent est relu quand la revision des
//  missions change (poll, au tick du panneau).
// =============================================================================
#pragma once

#include "AiMissionBoard.hpp"   // missionui : les couleurs, le geste du moment, les ordres de dessin

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiPanel;

class AiMissionStrip final : public ui::Widget {
public:
    AiMissionStrip(std::string id, AiPanel& panel);

    // La hauteur voulue a cette largeur (0 : pas de mission active), au plus `maxH`.
    [[nodiscard]] float heightFor(float width, float maxH) const;
    // Relit la mission active si elle (ou sa revision, ou l'etat de l'IA) a change ; vrai : la bande a change.
    bool poll();
    void refresh();
    [[nodiscard]] const std::string& missionId() const noexcept { return missionId_; }
    [[nodiscard]] bool               expanded() const noexcept { return open_; }
    void                             setExpanded(bool open);
    // Le tableau de bord de la mission (l'ecran de reprise si elle en a besoin).
    bool openBoard();

    // ---- ce qu'on lit (les essais) ----
    [[nodiscard]] std::vector<std::string>  lines() const;   // le nom, l'etat, la progression, la prochaine action...
    [[nodiscard]] const mission::Progress&  progress() const noexcept { return progress_; }
    [[nodiscard]] const missionui::Next&    next() const noexcept { return next_; }
    [[nodiscard]] ui::Button&               actionButton() noexcept { return *action_; }   // le geste du moment
    [[nodiscard]] ui::Button&               boardButton() noexcept { return *board_; }     // << Tableau de bord >>
    [[nodiscard]] gfx::Rect                 headRect() const;
    [[nodiscard]] gfx::Rect                 toggleRect() const;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void layoutOps();   // les ordres de dessin a la largeur (refaits quand elle change, ou ce qu'elle montre)
    [[nodiscard]] bool buttonsOnTwoRows(float width) const;
    void doAction();

    AiPanel&                    panel_;
    std::string                 missionId_;
    std::uint64_t               seenRev_{0};
    bool                        busy_{false}, generating_{false}, open_{true}, resume_{false};
    // Ce qu'elle montre (relu par refresh) :
    std::string                 name_, state_, count_, nextText_, proposals_;
    missionui::Tone             stateTone_{missionui::Tone::Muted};
    std::vector<std::pair<std::string, missionui::Tone>> problems_;
    mission::Progress           progress_;
    missionui::Next             next_;
    // La mise en page :
    std::vector<missionui::Op>  ops_;   // relatifs au coin de la bande
    std::vector<std::pair<gfx::Rect, std::string>> tipsRel_;
    float                       laidW_{-1.f}, buttonsY_{0.f};
    std::uint64_t               laidRev_{~0ull}, rev_{0};
    ui::Button*                 action_{nullptr};
    ui::Button*                 board_{nullptr};
    core::ConnectionScope       links_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

class AiMissionAsk final : public ui::Widget {
public:
    explicit AiMissionAsk(std::string id);

    void                             setRequest(std::string text);
    [[nodiscard]] const std::string& request() const noexcept { return request_; }
    [[nodiscard]] float              heightFor(float width) const;
    [[nodiscard]] ui::Button&        createButton() noexcept { return *create_; }
    [[nodiscard]] ui::Button&        continueButton() noexcept { return *continue_; }
    [[nodiscard]] gfx::Rect          closeRect() const;
    [[nodiscard]] std::vector<std::string> lines() const;
    // Le choix : 0 la croix (rien ne part), 1 << Creer la mission >>, 2 << Continuer en conversation >>.
    const core::SignalPtr<int> chosen = core::Signal<int>::create();

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] std::vector<std::string> textLines(float width) const;
    [[nodiscard]] bool                     twoRows(float width) const;
    std::string           request_;
    std::vector<std::string> laid_;   // l'explication a la ligne, et la demande citee (refaites a la mise en page)
    std::string           quote_;
    ui::Button*           create_{nullptr};
    ui::Button*           continue_{nullptr};
    core::ConnectionScope links_;
};

} // namespace app::ai
