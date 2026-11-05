// =============================================================================
//  app/RackView.hpp — the hardware configuration, drawn
// -----------------------------------------------------------------------------
//  Racks as rectangles, slots as cells, modules as cards. Everything comes from
//  primitives for the same reason the icons do: no image decoder in the build,
//  and a drawn rack scales, re-themes and tints by state without a second asset.
//
//  What the drawing has to communicate, in order of importance to the person
//  looking at it:
//
//    1. WHERE a module is - rack number and slot, matching the physical cabinet;
//    2. WHAT it is - reference, and enough colour to tell input from output at a
//       glance without reading;
//    3. HOW MUCH of it is used - the point count, and whether that figure came
//       out of the file or out of the catalog, because those are not the same
//       claim;
//    4. WHICH slots are empty, since a gap in a rack is information too.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace app {

class RackView final : public ui::Widget {
public:
    explicit RackView(std::string id = {});

    void setHardware(std::shared_ptr<const domain::Project> project);
    // 1.12.30 : un zoom Native (le contrat de Widget), de 50 a 250 %.
    void setZoom(float zoom) override;
    [[nodiscard]] float zoom() const noexcept override { return zoom_; }
    [[nodiscard]] float maxZoom() const override { return 2.5f; }

    // Emitted with the rack index and slot; slot -1 is the power supply.
    const core::SignalPtr<std::uint16_t, std::int16_t> moduleSelected =
        core::Signal<std::uint16_t, std::int16_t>::create();

    // Lot API 4 : les voies que le programme emploie, par module (« 14/16 »,
    // dessine au bas de la carte), et un module a mettre en avant (faute).
    struct Usage { std::uint16_t rack{0}; std::int16_t slot{0}; std::size_t used{0}, total{0}; bool faulty{false}; };
    void setUsage(std::vector<Usage> usage);
    // Choisir un module par programme (l'arbre, un script) ; ou il est a l'ecran.
    void select(std::uint16_t rack, std::int16_t slot);
    [[nodiscard]] bool slotRect(std::uint16_t rack, std::int16_t slot, gfx::Rect& out) const;
    [[nodiscard]] int selectedRack() const noexcept { return selectedRack_; }
    [[nodiscard]] int selectedSlot() const noexcept { return selectedSlot_; }
    // 1.24.1 : PLUSIEURS MODULES CHOISIS, pour les supprimer d'un coup. Ctrl+clic (ou Maj+clic) sur un module l'ajoute
    // (ou le retire) ; un clic simple revient a un seul. Les modules choisis (le choisi compris, une alimentation aussi),
    // rack par rack, emplacement par emplacement ; `setChosen` les choisit (les essais, les scripts).
    using Place = std::pair<std::uint16_t, std::int16_t>;
    [[nodiscard]] std::vector<Place> chosenModules() const;
    void setChosen(std::vector<Place> modules);
    const core::SignalPtr<> chosenChanged = core::Signal<>::create();
    // 1.24.1 : le clic droit sur un module (le point, dans la surface : un menu s'y ouvre) et la touche Suppr.
    const core::SignalPtr<gfx::Point> contextRequested = core::Signal<gfx::Point>::create();
    const core::SignalPtr<> deleteRequested = core::Signal<>::create();

    [[nodiscard]] ui::SizeHint sizeHint() const override;

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct SlotBox {
        gfx::Rect            rect;
        std::uint16_t        rack{0};
        std::int16_t         slot{0};
        const domain::Module* module{nullptr};   // null: empty slot
    };

    void rebuild(const ui::PaintContext&);
    void drawModule(const ui::PaintContext&, const SlotBox&) const;
    void drawEmptySlot(const ui::PaintContext&, const SlotBox&) const;
    [[nodiscard]] gfx::Color colorFor(domain::ModuleKind, const ui::Palette&) const;

    std::shared_ptr<const domain::Project> project_;
    std::vector<SlotBox> slots_;
    gfx::Size            content_{};
    gfx::Point           scroll_{};
    ui::PaintedScrollBar sbar_;   // 1.11.4 : la barre se tire
    float                zoom_{1.f};
    int                  selectedRack_{-1};
    int                  selectedSlot_{-32768};
    std::vector<Place>   extra_;          // 1.24.1 : les autres modules choisis (Ctrl+clic)
    bool                 dirtyGeometry_{true};
    std::vector<Usage>   usage_;
};

} // namespace app
