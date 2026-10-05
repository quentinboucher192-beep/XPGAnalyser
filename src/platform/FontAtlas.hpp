// =============================================================================
//  platform/FontAtlas.hpp — real text, rasterised from a TrueType face
// -----------------------------------------------------------------------------
//  WHY THIS EXISTS. drawText called SDL_RenderDebugText: SDL's built-in 8x8
//  face, scaled by an integer factor. Three consequences, all visible:
//
//    it is MONOSPACE, so every label in the interface has the spacing of a
//    terminal - no kerning, no proportional widths;
//
//    it has 190 GLYPHS, which is why the theme carried a rule saying UI strings
//    must be ASCII: no em dash, no ellipsis, no French quotes;
//
//    sizes must be MULTIPLES OF 8, so a dense interface cannot have its 13 px.
//
//  In a tool where text covers most of the pixels, no amount of palette work
//  compensates for that.
//
//  NO NEW DEPENDENCY. stb_truetype is a single public-domain header, vendored
//  in third_party/. The project's claim - nothing to install outside SDL - is
//  kept, where SDL_ttf would have been a library to link.
//
//  NO SDL HERE EITHER. This file rasterises into plain memory and answers
//  questions about metrics; the renderer turns the result into a texture. That
//  separation is not tidiness: it means the hard half - UTF-8 walking, advances,
//  line breaking - is testable without a display, and it IS tested.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gfx {

struct Glyph {
    // Where the glyph sits in the atlas bitmap.
    std::uint16_t x{0}, y{0}, w{0}, h{0};
    // Where to draw it, relative to the pen position on the baseline.
    float bearingX{0.f}, bearingY{0.f};
    float advance{0.f};
    bool  loaded{false};
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    FontAtlas(FontAtlas&&) noexcept;
    FontAtlas& operator=(FontAtlas&&) noexcept;

    // Loads a face from a file. Returns false and leaves the atlas unusable if
    // the file is absent or is not a font.
    //
    // ONLY LOAD FONTS YOU CHOSE. stb_truetype says so in its own header, in
    // capitals: it does no range checking, so a malformed file can read
    // arbitrary memory. The renderer therefore tries a fixed list of known
    // locations and never a path that arrived from a project file.
    [[nodiscard]] bool load(const std::string& path, float pixelHeight);

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] float pixelHeight() const noexcept { return pixelHeight_; }

    // Vertical metrics, in pixels, at the loaded size.
    [[nodiscard]] float ascent() const noexcept { return ascent_; }
    [[nodiscard]] float descent() const noexcept { return descent_; }
    [[nodiscard]] float lineHeight() const noexcept { return lineHeight_; }

    // The glyph for a code point, rasterised on first use. An unknown code
    // point yields the replacement box rather than nothing: a missing glyph
    // that draws nothing is a missing glyph nobody reports.
    [[nodiscard]] const Glyph& glyph(char32_t code);

    // The atlas bitmap: one byte of coverage per pixel. The renderer uploads it
    // as an alpha texture and tints at draw time, so one atlas serves every
    // colour.
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::uint16_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint16_t height() const noexcept { return height_; }

    // True when a glyph was added since the last time the renderer uploaded.
    // Rasterising on demand means the texture has to be refreshed, and doing it
    // every frame would waste the whole point of an atlas.
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void markUploaded() noexcept { dirty_ = false; }

    // ---- what the renderer needs to lay text out ------------------------
    [[nodiscard]] float measure(std::string_view utf8);

    // How many BYTES of utf8 fit in maxWidth. Bytes and not characters,
    // because the caller slices the string with it and a cut inside a multi-byte
    // sequence produces a broken glyph.
    [[nodiscard]] std::size_t fitBytes(std::string_view utf8, float maxWidth);

    // Decodes one UTF-8 sequence. Advances `at`. Invalid bytes yield U+FFFD and
    // advance by one, so a corrupt string costs a box rather than a hang.
    static char32_t decode(std::string_view utf8, std::size_t& at);

    // 1.11.2 (decision 155) : vrai si ce code est un symbole de AtlasSymbols.hpp
    // ET que l'atlas sait le tracer. glyph() le dessine au trait, sans consulter
    // la police : Segoe UI n'a ni ▶ ni ★, et le losange « ? » s'affichait.
    [[nodiscard]] static bool drawsSymbol(char32_t code) noexcept;

private:
    struct Face;
    std::unique_ptr<Face> face_;

    bool        ready_{false};
    bool        dirty_{false};
    std::string path_;
    float       pixelHeight_{0.f};
    float       scale_{0.f};
    float       ascent_{0.f}, descent_{0.f}, lineHeight_{0.f};

    std::vector<std::uint8_t> pixels_;
    std::uint16_t width_{0}, height_{0};
    std::uint16_t penX_{0}, penY_{0}, rowHeight_{0};

    std::unordered_map<char32_t, Glyph> glyphs_;

    [[nodiscard]] Glyph rasterise(char32_t code);
    [[nodiscard]] Glyph synthesise(char32_t code);   // un symbole dessine (AtlasSymbols.hpp)
};

// The faces the renderer looks for, in order. A bundled font wins - a project
// that ships its own face looks the same on every machine - then the system
// ones, then nothing, and the caller falls back to the built-in face.
[[nodiscard]] std::vector<std::string> defaultUiFontCandidates();
[[nodiscard]] std::vector<std::string> defaultMonoFontCandidates();

} // namespace gfx
