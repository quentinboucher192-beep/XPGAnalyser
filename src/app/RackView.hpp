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

#include <memory>
#include <optional>
#include <vector>

namespace app {

class RackView final : public ui::Widget {
public:
    explicit RackView(std::string id = {});

    void setHardware(std::shared_ptr<const domain::Project> project);
    void setZoom(float zoom);
    [[nodiscard]] float zoom() const noexcept { return zoom_; }

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
    float                zoom_{1.f};
    int                  selectedRack_{-1};
    int                  selectedSlot_{-32768};
    bool                 dirtyGeometry_{true};
    std::vector<Usage>   usage_;
};

} // namespace app
