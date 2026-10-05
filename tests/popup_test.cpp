// =============================================================================
//  tests/popup_test.cpp — the right-click menu, without a window
// -----------------------------------------------------------------------------
//  A context menu is mostly edge cases, and every one of them is invisible on
//  the developer's screen size:
//
//   * opened near an edge it must flip, not hang off the display. That defect
//     only appears on a smaller window than the one it was built on;
//   * a click anywhere else must dismiss it. Widget::dispatch tests
//     eventBounds() before onEvent() ever runs, so this is not a matter of
//     writing the handler correctly - the rectangle has to be right first;
//   * a dismissing click must NOT also reach what is underneath. Otherwise
//     closing the menu over a tree deletes whatever it landed on;
//   * arrowing must skip separators and dead entries;
//   * AND IT MUST ACTUALLY REACH THE SCREEN. The first version of this file
//     checked geometry and event routing and never rendered anything, so it
//     passed while the menu was invisible in the running application: an
//     overlay child was left with zero bounds, Widget::render returned early on
//     the empty clip, and the widget therefore never reached the
//     `overlays->push_back(this)` at the end of render(). It still claimed the
//     pointer, so it opened, swallowed every click and could not be seen.
//     A widget test that never paints is testing half a widget.
//
//  All of it is geometry, event routing and a recording renderer, so none of it
//  needs a window.
// =============================================================================
#include "../src/menu/IMenu.hpp"
#include "../src/ui/Layout.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace ui;

namespace {

    constexpr float kCell = 8.f;

    // Records what was asked for, and honours the clip stack the way the SDL
    // backend does - the whole point is that a clipped-away draw is not a draw.
    class RecordingRenderer final : public gfx::IRenderer {
    public:
        struct Draw { gfx::Point at; std::string text; gfx::Rect clip; };

        void beginFrame(gfx::Color) override { draws.clear(); }
        void endFrame() override {}
        void pushClip(const gfx::Rect& r) override {
            clips_.push_back(clips_.empty() ? r : clips_.back().intersect(r));
        }
        void popClip() override { if (!clips_.empty()) clips_.pop_back(); }
        void fillRect(const gfx::Rect&, gfx::Color) override {}
        void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
        void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
        void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
        void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
        void drawText(gfx::Point at, std::string_view s, gfx::FontId, gfx::Color) override {
            draws.push_back({ at, std::string(s),
                             clips_.empty() ? gfx::Rect{0, 0, 1e6f, 1e6f} : clips_.back() });
        }
        [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
            const float adv = f.v ? static_cast<float>(f.v) : 16.f;
            return { static_cast<float>(s.size()) * adv, adv, adv * 0.8f, adv * 0.2f };
        }
        [[nodiscard]] float lineHeight(gfx::FontId f) const override {
            return f.v ? static_cast<float>(f.v) : 16.f;
        }
        [[nodiscard]] gfx::Size surfaceSize() const override { return { 800.f, 600.f }; }
        [[nodiscard]] float dpiScale() const override { return 1.f; }
        [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId,
            float) const override {
            return s.size();
        }

        [[nodiscard]] int countVisible(std::string_view text) const {
            int n = 0;
            for (const auto& d : draws)
                if (d.text == text && !d.clip.empty()) ++n;
            return n;
        }

        std::vector<Draw> draws;
    private:
        std::vector<gfx::Rect> clips_;
    };

    // Enough of the platform services for sizeHint() and popupRect() to measure
    // rather than guess; the same 8x8-at-integer-scale metrics as SDL3.
    void installFakeMetrics() {
        installPlatformServices(PlatformServices{
            [](std::string_view s, gfx::FontId f) {
                const float adv = kCell * (f.v ? static_cast<float>(f.v) / kCell : 2.f);
                std::size_t n = 0;
                for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
                return static_cast<float>(n) * adv;
            },
            [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; },
            [] { return std::string{}; },
            [](std::string_view) {},
            });
    }

    std::vector<PopupMenu::Item> sampleItems() {
        return {
            PopupMenu::Item{"Open", "Enter", "", Icon::Open, true, false, 10},
            PopupMenu::Item{"Export to library", "", "", Icon::Export, true, false, 11},
            PopupMenu::Item{"", "", "", Icon::None, true, true, -1},          // separator
            PopupMenu::Item{"Delete", "Del", "still used by Gaz_1.Step",
                            Icon::Close, false, false, 12},
        };
    }

    // A tree under the menu, standing in for the project explorer: it must not see
    // the click that dismisses the menu.
    struct Rig {
        BoxLayout root{ Orientation::Vertical, "root" };
        TreeView* tree{ nullptr };
        PopupMenu* menu{ nullptr };
        int treeClicks{ 0 };
        int chosen{ -99 };
        int dismissals{ 0 };
        core::ConnectionScope links;

        Rig() {
            auto t = std::make_unique<TreeView>("tree");
            tree = t.get();
            root.addChild(std::move(t));
            auto m = std::make_unique<PopupMenu>("menu");
            menu = m.get();
            root.addChild(std::move(m));

            root.setBounds({ 0.f, 0.f, 800.f, 600.f });
            root.layout();

            links += menu->itemChosen->connect([this](int id) { chosen = id; });
            links += menu->dismissed->connect([this] { ++dismissals; });
            // The tree has no model, so it ignores everything; count the attempt by
            // watching the hover instead, which dispatch() updates unconditionally.
            links += tree->hoverChanged->connect([this](bool on) { if (on) ++treeClicks; });
        }

        // Mirrors WidgetMenu::HandleEvent: the overlay owner gets first refusal.
        EventResult send(const InputEvent& ev) {
            if (Widget* overlay = root.findOverlayOwner())
                if (overlay->dispatch(ev) == EventResult::Consumed) return EventResult::Consumed;
            return root.dispatch(ev);
        }
    };

} // namespace

int main() {
    installFakeMetrics();

    // --- closed: invisible to the pointer -----------------------------------
    {
        PopupMenu m("m");
        m.setItems(sampleItems());
        assert(!m.isOpen());
        assert(m.eventBounds().empty() && "a closed menu must not swallow clicks");
        assert(!m.requestsOverlayPass() && "and must not ask to be painted");
        assert(m.sizeHint().preferred.w == 0.f && m.sizeHint().preferred.h == 0.f
            && "and must claim no space in a layout");
    }

    // --- it stays on screen -------------------------------------------------
    {
        PopupMenu m("m");
        m.setItems(sampleItems());
        const gfx::Size surface{ 800.f, 600.f };

        m.openAt({ 100.f, 100.f }, surface);
        const auto normal = m.popupRect();
        assert(normal.x == 100.f && normal.y == 100.f && "room to spare: open at the cursor");
        assert(normal.w > 0.f && normal.h > 0.f);

        // Bottom-right corner: it must flip about the cursor, both ways.
        m.openAt({ 795.f, 595.f }, surface);
        const auto flipped = m.popupRect();
        assert(flipped.right() <= surface.w + 0.5f && "must not run off the right edge");
        assert(flipped.bottom() <= surface.h + 0.5f && "must not run off the bottom edge");
        assert(flipped.w == normal.w && flipped.h == normal.h && "same menu, moved not resized");

        // A surface too small to flip into must still start on screen rather
        // than at a negative coordinate.
        m.openAt({ 10.f, 10.f }, { 60.f, 40.f });
        const auto tiny = m.popupRect();
        assert(tiny.x >= 0.f && tiny.y >= 0.f && "never off the top-left either");
    }

    // --- open: it owns the pointer ------------------------------------------
    {
        PopupMenu m("m");
        m.setItems(sampleItems());
        m.openAt({ 100.f, 100.f }, { 800.f, 600.f });
        const auto whole = m.eventBounds();
        assert(whole.w >= 800.f && whole.h >= 600.f
            && "while open, every click must reach the menu, not just the ones on it");
        assert(m.requestsOverlayPass());
    }

    // --- choosing an entry --------------------------------------------------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });
        const auto box = rig.menu->popupRect();

        // Second entry: past the frame padding and one full row.
        const float rowH = 24.f;   // Metrics::rowHeight, the paint-time default
        rig.send(MouseDown{ {box.x + 20.f, box.y + 4.f + rowH + rowH * 0.5f},
                           MouseButton::Left, 1, {} });
        assert(rig.chosen == 11 && "the id travels, not the row number");
        assert(!rig.menu->isOpen() && "choosing closes the menu");
    }

    // --- a disabled entry eats its click ------------------------------------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });
        const auto box = rig.menu->popupRect();
        const float rowH = 24.f;
        // Fourth entry: two rows, a separator, then half of the fourth.
        const float y = box.y + 4.f + rowH * 2.f + 7.f + rowH * 0.5f;

        rig.send(MouseDown{ {box.x + 20.f, y}, MouseButton::Left, 1, {} });
        assert(rig.chosen == -99 && "a greyed entry must not fire");
        assert(rig.menu->isOpen() && "and must not close the menu either");
    }

    // --- clicking away dismisses, and goes no further -----------------------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 400.f, 400.f }, { 800.f, 600.f });
        rig.treeClicks = 0;

        const auto r = rig.send(MouseDown{ {20.f, 20.f}, MouseButton::Left, 1, {} });
        assert(r == EventResult::Consumed
            && "the dismissing click must not fall through to what is underneath");
        assert(!rig.menu->isOpen() && "a click outside dismisses");
        assert(rig.dismissals == 1);
        assert(rig.chosen == -99);
    }

    // --- Escape dismisses ---------------------------------------------------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });
        rig.send(KeyDown{ Key::Escape, {}, false });
        assert(!rig.menu->isOpen());
        assert(rig.dismissals == 1);
    }

    // --- arrowing skips separators and dead entries -------------------------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });

        rig.send(KeyDown{ Key::Down, {}, false });    // -> Open
        rig.send(KeyDown{ Key::Down, {}, false });    // -> Export
        rig.send(KeyDown{ Key::Down, {}, false });    // separator and Delete are skipped,
        // so it wraps to Open
        rig.send(KeyDown{ Key::Return, {}, false });
        assert(rig.chosen == 10 && "Down must never land on a separator or a dead entry");
    }

    // --- an open menu does not leak shortcuts to the window behind it -------
    {
        Rig rig;
        rig.menu->setItems(sampleItems());
        rig.menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });
        const auto r = rig.send(KeyDown{ Key::S, KeyMods{true, false, false, false}, false });
        assert(r == EventResult::Consumed
            && "Ctrl+S while a menu is open must not reach the application");
        assert(rig.menu->isOpen());
    }

    // --- it reaches the screen, through the real host and the real pass -----
    //
    //  This is the case the first version of this file missed entirely, and the
    //  one the application actually failed on. It walks the whole path the
    //  screen walks: an OverlayHost with content and a floating menu, laid out,
    //  rendered, then the top-most pass - exactly what WidgetMenu::Render does.
    {
        RecordingRenderer r;
        const Theme theme = Theme::dark();

        auto host = std::make_unique<OverlayHost>("host");
        host->setContent(std::make_unique<BoxLayout>(Orientation::Vertical, "content"));
        auto owned = std::make_unique<PopupMenu>("menu");
        auto* menu = owned.get();
        host->addOverlay(std::move(owned));

        host->setBounds({ 0.f, 0.f, 800.f, 600.f });
        host->layout();

        auto renderFrame = [&] {
            r.beginFrame(gfx::Color{});
            std::vector<Widget*> overlays;
            const PaintContext ctx{ r, theme, {0.f, 0.f, 800.f, 600.f}, 0.0, &overlays };
            host->layout();
            host->render(ctx);
            for (auto* w : overlays) w->paintTopMost(ctx);   // as WidgetMenu::Render does
            return overlays.size();
            };

        // Closed: nothing of it on screen.
        renderFrame();
        assert(r.countVisible("Publish") == 0);

        menu->setItems({ {"Open", "", "", Icon::Open, true, false, 1},
                        {"Publish", "", "", Icon::Export, true, false, 2} });
        menu->openAt({ 100.f, 100.f }, { 800.f, 600.f });

        const auto overlayCount = renderFrame();
        assert(overlayCount == 1 && "an open menu must reach the top-most pass");
        assert(menu->bounds().w > 0.f && menu->bounds().h > 0.f
            && "a floating child with no bounds is clipped out of existence");
        assert(r.countVisible("Open") == 1 && "and its entries must actually be drawn");
        assert(r.countVisible("Publish") == 1 && "exactly once - a shadow drawn twice is "
            "twice as dark");

        // Opened at the far corner it still lands on screen, and still draws.
        menu->openAt({ 798.f, 598.f }, { 800.f, 600.f });
        renderFrame();
        assert(r.countVisible("Publish") == 1 && "a flipped menu is still a visible menu");
        for (const auto& d : r.draws)
            assert(d.at.x >= 0.f && d.at.y >= 0.f && d.at.x < 800.f && d.at.y < 600.f
                && "nothing may be drawn off the surface");
    }

    std::printf("popup_test: ok\n");
    return 0;
}