// =============================================================================
//  app/hmi/HmiCycleStages.hpp - 1.27.0 : la section Execution des scripts d'une vue
// -----------------------------------------------------------------------------
//  LE CYCLE DE LA VUE, DEBALLE (hmi/HmiViewCycle.hpp) : les 10 etapes, dans l'ordre ou
//  le moteur les fait, chacune avec sa ligne (ce qu'elle fait) et sa duree mesuree en
//  simulation ; sous chacune (1 a 9), les scripts de cycle qui tournent apres elle,
//  dans leur rang - une pastille par script.
//
//      (1) Lecture des equipements                  0,40 ms
//          [:: ST Lire_Consignes             0,08 ms]
//      (2) Entrees                                  0,05 ms
//      ...
//      (8) Formules des objets (fx)                 0,70 ms
//          [ Deposer ici : apres 8 Formules, avant 9 Dessin - ... ]
//      (9) Dessin de la vue                         0,95 ms
//     (10) Ecritures envoyees                     3 ecritures
//      le cycle suivant reprend en 1 . OnOpen avant le 1er cycle . OnClose apres le dernier
//
//  GLISSER-DEPOSER : une pastille se tire entre deux etapes ; une ligne pointillee dit
//  ou le script va tourner et ce que ca change. Le volet des scripts y amene aussi une
//  ligne de sa liste, ou le bouton + Script de cycle (un glisser venu d'ailleurs). Le
//  depot : dropped(script, moment, rang) - kNoId : un nouveau script. La souris est
//  tenue pendant le glisser : lachee hors de la section, rien ne change.
// =============================================================================
#pragma once

#include "../../hmi/HmiViewCycle.hpp"
#include "../../ui/Widget.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace app {

class HmiCycleStages final : public ui::Widget {
public:
    struct Chip {
        hmi::Id     id{hmi::kNoId};
        std::string name;
        std::string lang;          // "ST", "C", "C++"
        int         moment{hmi::viewcycle::kLegacyMoment};
        std::string note;          // a droite : "0,08 ms", "1 cycle sur 5", "si =Mode_Auto"
        std::string tip;           // l'infobulle : son moment, sa frequence, sa condition, sa duree
    };
    explicit HmiCycleStages(std::string id);

    // Les scripts de cycle de la vue (dans l'ordre du moteur), la mesure de la derniere simulation, le choisi.
    void setChips(std::vector<Chip> chips);
    [[nodiscard]] const std::vector<Chip>& chips() const noexcept { return chips_; }
    void setTiming(const hmi::viewcycle::Timing& t);
    // La duree de ses scripts de cycle a la derniere simulation (le total du cycle les compte, en plus des etapes).
    void setScriptsMs(double ms) { scriptsMs_ = ms; invalidate(); }
    void setSelected(hmi::Id id);
    [[nodiscard]] hmi::Id selected() const noexcept { return selected_; }

    // Un clic sur une pastille : la choisir.
    const core::SignalPtr<hmi::Id> chipClicked = core::Signal<hmi::Id>::create();
    // Un depot : le script (kNoId : un nouveau script de cycle), le moment (1..9), le rang dans ce moment (0 : le premier).
    const core::SignalPtr<hmi::Id, int, std::size_t> dropped = core::Signal<hmi::Id, int, std::size_t>::create();

    // ---- un glisser venu d'ailleurs ----
    // Le bouton + Script de cycle : la section tient la souris jusqu'au lacher (kNoId : un nouveau script).
    void beginExternalDrag(hmi::Id script, std::string label);
    // La liste des scripts (sa table tient la souris) : ou est la souris ; relachee : le depot. Vrai : depose ici.
    bool externalDragAt(hmi::Id script, const std::string& label, gfx::Point at, bool released);
    void cancelDrag();

    // ---- pour les essais et les captures ----
    [[nodiscard]] bool stepRect(int number, gfx::Rect& out) const;      // la ligne de l'etape (1..10)
    [[nodiscard]] bool slotRect(int moment, gfx::Rect& out) const;      // la zone sous l'etape `moment` (1..9)
    [[nodiscard]] bool chipRect(hmi::Id, gfx::Rect& out) const;
    [[nodiscard]] bool dragging() const noexcept { return drag_.active; }
    [[nodiscard]] int  dropMoment() const noexcept { return drop_.moment; }    // 0 : aucun
    [[nodiscard]] std::size_t dropRank() const noexcept { return drop_.rank; }
    [[nodiscard]] std::string dropText() const;                         // la ligne pointillee ("" : aucune)
    [[nodiscard]] static constexpr int stepCount() noexcept { return hmi::viewcycle::kSteps; }   // 10
    [[nodiscard]] std::string stepTitle(int number) const;              // "Lecture des \xC3\xA9quipements"
    [[nodiscard]] std::string stepTime(int number) const;               // "0,40 ms", "\xE2\x80\x94", "3 \xC3\xA9" "critures"
    [[nodiscard]] std::string totalText() const;                        // "simulation : 3,8 ms sur 100 ms"
    [[nodiscard]] std::string footerText() const;
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

    static constexpr float kStepH = 40.f;
    static constexpr float kChipH = 24.f;
    static constexpr float kDropH = 44.f;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Geometry {
        std::array<gfx::Rect, hmi::viewcycle::kSteps + 1> steps{};   // [1..10]
        std::array<gfx::Rect, hmi::viewcycle::kSteps> slots{};       // [1..9]
        std::vector<std::pair<hmi::Id, gfx::Rect>> chips;
        gfx::Rect drop{};                                            // la ligne pointillee (w = 0 : aucune)
        gfx::Rect footer{};
        float     height{0};
    };
    struct Drag {
        bool        active{false};
        bool        external{false};
        hmi::Id     script{hmi::kNoId};
        std::string label;
        gfx::Point  at{};
    };
    struct Drop {
        int         moment{0};
        std::size_t rank{0};
    };
    void relayout();
    [[nodiscard]] Geometry build() const;      // la place de chaque chose (la ligne pointillee comprise)
    void aim(gfx::Point at);                   // le depot vise sous la souris
    bool finishDrag(gfx::Point at);            // vrai : depose
    [[nodiscard]] float contentTop() const noexcept;

    std::vector<Chip>          chips_;
    hmi::viewcycle::Timing     timing_{};
    double                     scriptsMs_{0};
    hmi::Id                    selected_{hmi::kNoId};
    Geometry                   geo_;
    Drag                       drag_;
    Drop                       drop_;
    hmi::Id                    pressed_{hmi::kNoId};
    gfx::Point                 pressAt_{};
    float                      scrollY_{0};
};

// LE BOUTON << + Script de cycle >> sous la liste des scripts d'une vue, et sa phrase : un clic cree un script de cycle,
// un glisser l'amene a la section Execution, a l'etape voulue (startDrag : le volet la previent).
class HmiCycleAdd final : public ui::Widget {
public:
    explicit HmiCycleAdd(std::string id);
    const core::SignalPtr<> clicked = core::Signal<>::create();
    std::function<void()>   startDrag;           // le glisser commence (la section tient alors la souris)
    [[nodiscard]] gfx::Rect buttonRect() const;
    static constexpr float kHeight = 34.f;
protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    bool       pressed_{false};
    gfx::Point pressAt_{};
};

} // namespace app
