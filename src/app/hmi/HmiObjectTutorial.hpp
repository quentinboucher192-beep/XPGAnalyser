// =============================================================================
//  app/hmi/HmiObjectTutorial.hpp - le tutoriel d'un objet, joue par le vrai moteur
// -----------------------------------------------------------------------------
//  L'ONGLET TUTORIEL DE LA PAGE D'AIDE D'UN OBJET (lot 16). L'exemple de l'objet
//  (hmi::examples : sa vue, son scenario) tourne sur le VRAI moteur de l'IHM,
//  decoupe en chapitres (hmi::examples::tutorialFor) :
//
//    +-- CHAPITRES --+------------------ la scene --------------------------+
//    | 1 Le GIF anime|   la vue de l'exemple, le pointeur qui fait les      |
//    | 2 Jouer       |   gestes, et la BULLE du chapitre pointee sur        |
//    | 3 Pause       |   l'objet dont il parle                               |
//    | ...           |                                                       |
//    | 8 A toi       +-------------------------------------------------------+
//    |               | |< > [] | x0,5 x1 x2 | ---o--|---|---- 3,2 s / 14 s | A toi |
//    +---------------+-------------------------------------------------------+
//
//  Lecture, pause, arret (au debut), vitesse, la frise (un clic ou un glisser
//  y va : reculer rejoue le tour depuis le debut, a l'identique), les chapitres
//  (un clic y va). "A toi" rend la main : le scenario se tait, les clics et le
//  clavier vont au moteur, comme en simulation.
// =============================================================================
#pragma once

#include "HmiExampleView.hpp"
#include "../../hmi/HmiExamples.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app {

class HmiObjectTutorial final : public ui::Widget {
public:
    explicit HmiObjectTutorial(std::string id);

    // Le tutoriel de ce genre d'objet ; nullopt : aucun.
    void setKind(std::optional<hmi::Kind> kind);
    [[nodiscard]] std::optional<hmi::Kind> kind() const noexcept { return kind_; }
    [[nodiscard]] bool hasTutorial() const noexcept { return stage_ && stage_->hasExample() && !chapters_.empty(); }

    void play();
    void pause();
    void togglePlay();
    void stop();                              // au debut du tour, a l'arret
    void setSpeed(double factor);             // 0,5 ; 1 ; 2
    void seek(double seconds);
    void goToChapter(std::size_t index);      // y aller, et jouer
    void yourTurn();                          // "A toi"
    void guided();                            // revenir au tutoriel guide (chapitre 1)
    [[nodiscard]] bool playing() const noexcept { return stage_->playing(); }
    [[nodiscard]] bool yourTurnActive() const noexcept { return stage_->interactive(); }
    [[nodiscard]] double position() const noexcept { return stage_->position(); }
    [[nodiscard]] double speed() const noexcept { return stage_->speed(); }
    [[nodiscard]] std::size_t currentChapter() const;
    [[nodiscard]] const std::vector<hmi::examples::Chapter>& chapters() const noexcept { return chapters_; }
    [[nodiscard]] HmiExampleView& stage() noexcept { return *stage_; }

    // Les commandes a l'ecran (scripts, essais) : "debut", "lecture", "arret",
    // "vitesse:0.5", "vitesse:1", "vitesse:2", "frise", "a-toi", "chapitre:N" (1..).
    [[nodiscard]] bool controlRect(std::string_view name, gfx::Rect& out) const;
    // La bulle du chapitre a l'ecran (vide : aucune).
    [[nodiscard]] gfx::Rect bubbleRect() const noexcept { return bubble_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] double timeAtX(float x) const;
    [[nodiscard]] float  xAtTime(double t) const;

    std::optional<hmi::Kind>                  kind_;
    HmiExampleView*                           stage_{nullptr};
    std::vector<hmi::examples::Chapter>       chapters_;
    // Les zones, au dernier placement.
    gfx::Rect list_{}, bar_{}, frise_{}, start_{}, playBtn_{}, stopBtn_{}, yourTurn_{};
    gfx::Rect speeds_[3]{};
    std::vector<gfx::Rect>                    rows_;
    gfx::Rect                                 bubble_{};
    bool                                      dragging_{false};
    int                                       hoverRow_{-1};
};

} // namespace app
