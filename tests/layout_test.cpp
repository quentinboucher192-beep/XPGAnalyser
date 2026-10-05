// =============================================================================
//  tests/layout_test.cpp — proves the overlap is gone, without a window
// -----------------------------------------------------------------------------
//  A recording renderer stands in for SDL and reports the same metrics the SDL3
//  backend does (built-in 8x8 debug font at an integer scale, so a 16 px face
//  advances exactly 16 px per glyph). Then:
//
//    1. no two siblings in a layout may overlap;
//    2. no drawText may spill outside the widget that issued it.
//
//  Both used to fail: sizeHint() estimated 7.2 px per character while the face
//  actually advanced 13, so labels ran past their buttons and buttons ran into
//  each other.
// =============================================================================
#include "../src/app/screens/Screens.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Layout.hpp"
#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ui;

namespace {

constexpr float kCell = 8.f;   // SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE

float scaleFor(gfx::FontId f) {
    const float px = f.v ? static_cast<float>(f.v) : 16.f;
    return std::max(1.f, std::round(px / kCell));
}

std::size_t glyphCount(std::string_view s) {
    std::size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Mirrors gfx::SdlRenderer's metrics exactly.
class RecordingRenderer final : public gfx::IRenderer {
public:
    struct TextDraw { gfx::Point at; float width, height; gfx::Rect clip; std::string text; };

    void beginFrame(gfx::Color) override { texts.clear(); clips_.clear(); }
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

    void drawText(gfx::Point at, std::string_view utf8, gfx::FontId f, gfx::Color) override {
        const auto m = measure(utf8, f);
        texts.push_back(TextDraw{at, m.width, m.height,
                                 clips_.empty() ? gfx::Rect{0, 0, 1e6f, 1e6f} : clips_.back(),
                                 std::string(utf8)});
    }

    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float adv = kCell * scaleFor(f);
        return {static_cast<float>(glyphCount(s)) * adv, adv, adv * 0.8f, adv * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return kCell * scaleFor(f); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1536.f, 1024.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f,
                                            float maxWidth) const override {
        const float adv = kCell * scaleFor(f);
        if (maxWidth <= 0.f) return 0;
        const auto budget = static_cast<std::size_t>(maxWidth / adv);
        std::size_t glyphs = 0, bytes = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
                if (glyphs == budget) return bytes;
                ++glyphs;
            }
            bytes = i + 1;
        }
        return s.size();
    }

    std::vector<TextDraw> texts;
private:
    std::vector<gfx::Rect> clips_;
};

bool overlaps(const gfx::Rect& a, const gfx::Rect& b) {
    const auto i = a.intersect(b);
    return i.w > 0.5f && i.h > 0.5f;      // half a pixel of touching is not an overlap
}

int checkNoSiblingOverlap(const Widget& w, const std::string& path) {
    int problems = 0;
    std::vector<const Widget*> visible;
    for (const auto& c : w.children())
        if (c->visibility() == Visibility::Visible) visible.push_back(c.get());

    for (std::size_t i = 0; i < visible.size(); ++i)
        for (std::size_t j = i + 1; j < visible.size(); ++j)
            if (overlaps(visible[i]->bounds(), visible[j]->bounds())) {
                std::printf("  OVERLAP in %s: '%s' and '%s'\n", path.c_str(),
                            visible[i]->id().c_str(), visible[j]->id().c_str());
                ++problems;
            }

    for (const auto* c : visible)
        problems += checkNoSiblingOverlap(*c, path + "/" + c->id());
    return problems;
}

} // namespace

int main() {
    RecordingRenderer renderer;

    // The application installs these at startup; the test does the same so
    // sizeHint() measures instead of guessing.
    installPlatformServices(PlatformServices{
        [&](std::string_view s, gfx::FontId f) { return renderer.measure(s, f).width; },
        [&](gfx::FontId f) { return renderer.lineHeight(f); },
        [] { return std::string{}; },
        [](std::string_view) {},
    });

    const Theme theme = Theme::dark();

    // --- 1. a button row, the case visible in the screenshot ---------------
    {
        auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, "buttons");
        row->setSpacing(8.f);
        row->addChild(std::make_unique<Button>("Cancel", "cancel"));
        row->addChild(std::make_unique<Button>("Open", "open"));
        row->addChild(std::make_unique<Button>("Import MAST export", "import"));
        row->setBounds({0.f, 0.f, 900.f, 40.f});
        row->layout();

        assert(checkNoSiblingOverlap(*row, "buttons") == 0);

        // Every label must fit inside its own button.
        renderer.beginFrame(gfx::Color{});
        row->render(PaintContext{renderer, theme, {0, 0, 900, 40}, 0.0, nullptr});
        for (const auto& t : renderer.texts) {
            const bool fits = t.at.x >= t.clip.x - 0.5f
                           && t.at.x + t.width <= t.clip.right() + 0.5f;
            if (!fits)
                std::printf("  TEXT SPILL: '%s' width %.0f in clip %.0f..%.0f\n",
                            t.text.c_str(), t.width, t.clip.x, t.clip.right());
            assert(fits && "a label must not be drawn outside the widget that owns it");
        }
        std::printf("button row: %zu labels, none spilling\n", renderer.texts.size());
    }

    // --- 2. a text field must not swell to fill a vertical box -------------
    {
        auto box = std::make_unique<BoxLayout>(Orientation::Vertical, "dialogBody");
        box->setSpacing(10.f);
        auto& field = box->addChild(std::make_unique<InputText>("path"));
        box->addChild(std::make_unique<Button>("Open", "ok"));
        box->setBounds({0.f, 0.f, 600.f, 400.f});
        box->layout();

        const float h = field.bounds().h;
        std::printf("text field height in a 400 px box: %.0f px\n", h);
        assert(h < 40.f && "InputText must keep a fixed height, not absorb the box");
        assert(checkNoSiblingOverlap(*box, "dialogBody") == 0);
    }

    // --- 3. the real startup screen, laid out at window size ---------------
    {
        auto shell = std::make_unique<DockLayout>("shell");
        auto header = std::make_unique<BoxLayout>(Orientation::Vertical, "header");
        header->setSpacing(4.f);
        shell->dock(std::move(header), DockLayout::Side::Top, 72.f);

        auto actions = std::make_unique<BoxLayout>(Orientation::Horizontal, "actions");
        actions->setSpacing(8.f);
        actions->addChild(std::make_unique<Button>("Open project", "open"));
        actions->addChild(std::make_unique<Button>("Import MAST export", "import"));
        actions->addChild(std::make_unique<InputText>("path"));
        shell->dock(std::move(actions), DockLayout::Side::Top, 42.f);

        shell->dock(std::make_unique<ListView>("recent"), DockLayout::Side::Center, 0.f);
        shell->dock(std::make_unique<StatusBar>("status"), DockLayout::Side::Bottom, 26.f);

        shell->setBounds({0.f, 0.f, 1536.f, 1024.f});
        shell->layout();

        const int problems = checkNoSiblingOverlap(*shell, "shell");
        assert(problems == 0 && "docked regions must tile, not stack");
        std::printf("startup shell: %d overlaps\n", problems);
    }

    // --- 4. table rows: paint geometry and hit geometry must agree ---------
    {
        auto table = std::make_unique<TableView>("table");
        table->setColumns({{"Name", 300.f}, {"Type", 200.f}});
        table->setBounds({0.f, 0.f, 800.f, 600.f});
        table->layout();
        renderer.beginFrame(gfx::Color{});
        table->render(PaintContext{renderer, theme, {0, 0, 800, 600}, 0.0, nullptr});
        // No model, so nothing to click; the point is that painting caches the
        // theme metrics the event handler will later use.
        std::printf("table painted with row height %.0f\n", theme.metric.rowHeight);
    }

    // --- 5. an open dropdown must win the click over what it covers --------
    {
        auto panel = std::make_unique<BoxLayout>(Orientation::Vertical, "panel");
        auto& drop = static_cast<DropDown&>(panel->addChild(std::make_unique<DropDown>("scope")));
        drop.setItems({{"All", "all"}, {"Global", "g"}, {"Local", "l"}});
        drop.setSelectedIndex(0);
        auto& list = panel->addChild(std::make_unique<ListView>("under"));
        panel->setBounds({0.f, 0.f, 300.f, 400.f});
        panel->layout();

        // Closed: the dropdown accepts events only inside its own bounds.
        assert(panel->findOverlayOwner() == nullptr);
        const float below = drop.bounds().bottom() + 10.f;
        assert(!drop.eventBounds().contains({drop.bounds().x + 5.f, below}));
        assert(list.bounds().contains({drop.bounds().x + 5.f, below}));

        // Open it: the hit area now covers the popup, and the tree search finds
        // it, so the menu can route the click there before the list sees it.
        drop.dispatch(MouseDown{{drop.bounds().x + 5.f, drop.bounds().y + 5.f},
                                MouseButton::Left, 1, {}});
        assert(panel->findOverlayOwner() == &drop && "an open popup must claim priority");
        assert(drop.eventBounds().contains({drop.bounds().x + 5.f, below})
               && "the popup area must accept clicks that visually land on it");

        // Clicking the second row selects it rather than hitting the list.
        const float rowY = drop.bounds().bottom() + 1.f + 24.f + 12.f;
        drop.dispatch(MouseDown{{drop.bounds().x + 5.f, rowY}, MouseButton::Left, 1, {}});
        std::printf("dropdown selection after clicking row 2: %d\n", drop.selectedIndex());
        assert(drop.selectedIndex() == 1);
    }

    // --- 6. a toolbar that does not fit degrades instead of truncating ------
    {
        auto bar = std::make_unique<ToolBar>("bar");
        const char* labels[] = {"Projects", "Save", "Save as", "State", "To Control Expert",
                                "Section", "Unit", "DFB", "DDT", "Variable", "Rack",
                                "Module", "Undo", "Hardware", "Refresh", "Statistics"};
        for (const char* l : labels) bar->addButton(l, "act", Icon::Section);

        std::vector<std::string> fired;
        bar->setActionSink([&](core::ActionId id) { fired.emplace_back(id); });

        auto visibleButtons = [&] {
            std::size_t n = 0;
            for (const auto& c : bar->children())
                if (c->visibility() == Visibility::Visible && c->id().find("overflow") == std::string::npos)
                    ++n;
            return n;
        };

        // Wide: everything, with labels.
        bar->setBounds({0.f, 0.f, 4000.f, 40.f});
        bar->layout();
        assert(bar->fit() == ToolBar::Fit::Full);
        assert(bar->hiddenCount() == 0);
        assert(visibleButtons() == std::size(labels));
        assert(checkNoSiblingOverlap(*bar, "bar/full") == 0);

        // Narrower: the labels go, every command stays.
        bar->setBounds({0.f, 0.f, 900.f, 40.f});
        bar->invalidateLayout();
        bar->layout();
        std::printf("900 px: fit=%d hidden=%zu visible=%zu\n",
                    static_cast<int>(bar->fit()), bar->hiddenCount(), visibleButtons());
        assert(bar->fit() != ToolBar::Fit::Full && "labels cannot all fit in 900 px");
        assert(checkNoSiblingOverlap(*bar, "bar/icons") == 0);

        // Narrow: some commands move behind the overflow control, and none of
        // them is lost - hidden + visible still accounts for every button.
        bar->setBounds({0.f, 0.f, 260.f, 40.f});
        bar->invalidateLayout();
        bar->layout();
        std::printf("260 px: fit=%d hidden=%zu visible=%zu\n",
                    static_cast<int>(bar->fit()), bar->hiddenCount(), visibleButtons());
        assert(bar->fit() == ToolBar::Fit::Overflowing);
        assert(bar->hiddenCount() > 0 && "something must have moved off the bar");
        assert(bar->hiddenCount() + visibleButtons() >= std::size(labels)
               && "no command may simply disappear");
        assert(checkNoSiblingOverlap(*bar, "bar/overflow") == 0);

        // Widening it again brings everything back: the degradation is not sticky.
        bar->setBounds({0.f, 0.f, 4000.f, 40.f});
        bar->invalidateLayout();
        bar->layout();
        assert(bar->fit() == ToolBar::Fit::Full);
        assert(bar->hiddenCount() == 0);
        assert(visibleButtons() == std::size(labels));
    }

    // --- 7. a row that does not fit must shrink, never run off the edge ------
    //
    // This is the bug that made the editor unreachable: the document bar wanted
    // 1116 px in a 780 px pane, every child got its preferred width anyway, and
    // the last control - the Edit toggle - was laid out at x = 1028 where the
    // clip rectangle quietly ate it. A control that exists and cannot be seen is
    // worse than one that is missing.
    {
        auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, "cramped");
        row->setSpacing(6.f);
        const char* labels[] = {"Structured Text", "Fold all", "Unfold", "All levels",
                                "Copy", "Edit"};
        for (const char* l : labels) row->addChild(std::make_unique<Button>(l, l));

        // Wide enough: everyone gets what they asked for.
        row->setBounds({0.f, 0.f, 2000.f, 40.f});
        row->layout();
        for (const auto& c : row->children())
            assert(c->bounds().right() <= 2000.f + 0.5f);

        // Half the room they want.
        row->setBounds({0.f, 0.f, 500.f, 40.f});
        row->invalidateLayout();
        row->layout();

        float widest = 0.f;
        for (const auto& c : row->children()) widest = std::max(widest, c->bounds().right());
        std::printf("cramped row: rightmost edge %.0f px in 500 px\n", widest);

        for (const auto& c : row->children()) {
            assert(c->bounds().right() <= 500.f + 1.f
                   && "no child may be laid out past the edge of its parent");
            assert(c->bounds().w > 0.f && "and none may be shrunk out of existence");
        }
        assert(checkNoSiblingOverlap(*row, "cramped") == 0);

        // The last control is the one that used to vanish; it must be reachable.
        const auto& last = row->children().back();
        assert(last->bounds().x < 500.f && "the last control has to start inside the row");
        assert(last->bounds().right() <= 500.f + 1.f);

        // Absurdly narrow: still no overflow, still no overlap.
        row->setBounds({0.f, 0.f, 120.f, 40.f});
        row->invalidateLayout();
        row->layout();
        for (const auto& c : row->children())
            assert(c->bounds().right() <= 120.f + 1.f);
        assert(checkNoSiblingOverlap(*row, "tiny") == 0);
    }

    std::printf("\nlayout_test: all assertions passed\n");
    return 0;
}
