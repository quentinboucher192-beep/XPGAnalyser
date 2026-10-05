// =============================================================================
//  tests/fold_test.cpp — folding derived from indentation
// -----------------------------------------------------------------------------
//  The rule under test: a line is a fold header when the next non-blank line is
//  indented further, and its region ends at the last line still indented past
//  it. No grammar, no per-language block table - which is why the same code
//  folds Structured Text and C++ without knowing anything about either.
//
//  The cases that matter are the ones that would silently produce wrong regions:
//  a blank line inside a block (must not close it), nesting (inner region must
//  end before the outer one), and a trailing dedent.
// =============================================================================
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Mirrors the SDL3 backend's metrics: the built-in 8x8 debug font at an integer
// scale, so a 16 px face advances exactly 16 px per glyph.
class RecordingRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override { clips_.clear(); }
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
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}

    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float adv = 8.f * scaleFor(f);
        std::size_t glyphs = 0;
        for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
        return {static_cast<float>(glyphs) * adv, adv, adv * 0.8f, adv * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return 8.f * scaleFor(f); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1536.f, 1024.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f,
                                            float maxWidth) const override {
        const float adv = 8.f * scaleFor(f);
        if (maxWidth <= 0.f || adv <= 0.f) return 0;
        return std::min<std::size_t>(s.size(), static_cast<std::size_t>(maxWidth / adv));
    }
private:
    static float scaleFor(gfx::FontId f) {
        return std::max(1.f, std::round(static_cast<float>(f.v ? f.v : 16) / 8.f));
    }
    std::vector<gfx::Rect> clips_;
};

} // namespace

using namespace ui;

int main() {
    const std::string st =
        "PROGRAM Main\n"                       // 0  indent 0, header
        "  IF gStart THEN\n"                   // 1  indent 2, header
        "    counter := counter + 1;\n"        // 2  indent 4
        "    IF counter > 10 THEN\n"           // 3  indent 4, header
        "      counter := 0;\n"                // 4  indent 6
        "      done := TRUE;\n"                // 5  indent 6
        "    END_IF;\n"                        // 6  indent 4
        "  END_IF;\n"                          // 7  indent 2
        "\n"                                   // 8  blank: must not close a region
        "  FOR i := 0 TO 9 DO\n"               // 9  indent 2, header
        "    table[i] := 0;\n"                 // 10 indent 4
        "  END_FOR;\n"                         // 11 indent 2
        "END_PROGRAM\n";                       // 12 indent 0

    MultiLineText view("probe");
    view.setLanguage(Language::StructuredText);
    view.setText(st);

    // 13 lines of text plus the empty line after the final newline, which is
    // what every editor shows and what a caret at end-of-file needs.
    assert(view.lineCount() == 14);

    // --- which lines open a region ----------------------------------------
    assert(view.isFoldable(0) && "PROGRAM opens a region");
    assert(view.isFoldable(1) && "the outer IF opens a region");
    assert(view.isFoldable(3) && "the inner IF opens a region");
    assert(view.isFoldable(9) && "the FOR opens a region");
    assert(!view.isFoldable(2)  && "a plain statement is not a header");
    assert(!view.isFoldable(4));
    assert(!view.isFoldable(12) && "the last line closes nothing");

    std::printf("max nesting depth: %d\n", view.maxFoldDepth());
    assert(view.maxFoldDepth() >= 2 && "PROGRAM > IF > IF is three levels");

    // --- toggling ----------------------------------------------------------
    view.toggleFold(3);
    assert(view.isFolded(3));
    view.toggleFold(3);
    assert(!view.isFolded(3));

    view.foldAll();
    assert(view.isFolded(0) && view.isFolded(1) && view.isFolded(9));
    assert(!view.isFolded(2) && "a non-header cannot be folded");
    view.unfoldAll();
    assert(!view.isFolded(0));

    // --- fold to depth ------------------------------------------------------
    view.foldToDepth(1);
    assert(!view.isFolded(0) && "the outermost header stays readable");
    assert(view.isFolded(1)  && "everything one level in collapses");
    view.foldToDepth(-1);
    assert(!view.isFolded(1));

    // --- the blank line must not have broken the following region -----------
    assert(view.isFoldable(9) && "a blank line between blocks is not a dedent");

    // --- selection and clipboard --------------------------------------------
    view.selectAll();
    assert(view.hasSelection());
    assert(view.selectedText().size() > 100);
    view.clearSelection();
    assert(!view.hasSelection());
    assert(view.selectedText() == st && "no selection means the whole document");

    // --- the same rule, on braces -------------------------------------------
    MultiLineText cpp("cpp");
    cpp.setLanguage(Language::Cpp);
    cpp.setText("int main() {\n"
                "    if (x) {\n"
                "        run();\n"
                "    }\n"
                "    return 0;\n"
                "}\n");
    assert(cpp.isFoldable(0) && cpp.isFoldable(1));
    assert(!cpp.isFoldable(2) && "the call is a leaf");
    assert(!cpp.isFoldable(4));

    // --- a document with no indentation at all folds nothing, and must not
    //     crash or report a header ------------------------------------------
    MultiLineText flat("flat");
    flat.setText("a\nb\nc\n");
    for (std::size_t i = 0; i < flat.lineCount(); ++i) assert(!flat.isFoldable(i));
    flat.foldAll();          // must be a no-op rather than a crash
    assert(flat.maxFoldDepth() == 0);

    // --- the caret must land where the pointer actually is ------------------
    //
    // The bug this pins: the painter offsets text by the whole gutter (line
    // numbers + fold markers) while the hit test subtracted the marker column
    // only, so every click resolved one line-number-column too far right. The
    // two calculations are now the same one, recorded by the painter.
    {
        RecordingRenderer renderer;
        installPlatformServices(PlatformServices{
            [&](std::string_view t, gfx::FontId f) { return renderer.measure(t, f).width; },
            [&](gfx::FontId f) { return renderer.lineHeight(f); },
            [] { return std::string{}; },
            [](std::string_view) {},
        });
        const Theme theme = Theme::dark();

        MultiLineText code("caret");
        code.setLanguage(Language::StructuredText);
        code.setText("IF a THEN\n  b := 1;\n  c := 2;\nEND_IF;\n");
        code.setBounds({0.f, 0.f, 800.f, 300.f});
        code.layout();

        // Paint once so the geometry is recorded, exactly as at runtime.
        renderer.beginFrame(gfx::Color{});
        code.render(PaintContext{renderer, theme, {0, 0, 800, 300}, 0.0, nullptr});

        const float advance = renderer.measure("M", gfx::FontId{16}).width;
        const float lh      = renderer.lineHeight(gfx::FontId{16});
        const float gutter  = code.gutterForTest();
        std::printf("advance %.0f px, line height %.0f px, gutter %.0f px\n",
                    advance, lh, gutter);
        assert(gutter > 18.f && "the gutter includes the line numbers, not just the markers");

        const float originX = code.contentRectForTest().x + gutter;
        const float y = code.contentRectForTest().y + 1.5f * lh;   // second visible line

        // Left part of glyph 4 -> the caret goes before it.
        code.dispatch(MouseDown{{originX + 4.2f * advance, y}, MouseButton::Left, 1, {}});
        std::printf("click at 4.2 glyphs -> line %u column %u\n",
                    code.caretLineForTest(), code.caretColumnForTest());
        assert(code.caretLineForTest() == 1 && "second line of the document");
        assert(code.caretColumnForTest() == 4 && "the caret must land under the pointer");

        // Right part of the same glyph -> after it. Half-glyph rounding is what
        // makes click-and-drag select what the eye expects.
        code.dispatch(MouseDown{{originX + 4.8f * advance, y}, MouseButton::Left, 1, {}});
        assert(code.caretColumnForTest() == 5);

        // Column 0: clicking the very start of the text area.
        code.dispatch(MouseDown{{originX + 1.f, y}, MouseButton::Left, 1, {}});
        assert(code.caretColumnForTest() == 0);

        // Past the end of a short line clamps to its length rather than running
        // on. The point stays inside the widget: a click outside it is rejected
        // before onEvent, which would have made this assertion vacuous.
        code.dispatch(MouseDown{{originX + 30.f * advance, y}, MouseButton::Left, 1, {}});
        assert(code.caretColumnForTest() == 9 && "'  b := 1;' is 9 characters, so the caret "
                                                 "stops at the end of the line");
    }

    std::printf("\nfold_test: all assertions passed\n");
    return 0;
}
