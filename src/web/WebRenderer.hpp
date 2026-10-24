// =============================================================================
//  web/WebRenderer.hpp - 1.12.31 : le dessin de l'appli, rejoue sur un <canvas>
// -----------------------------------------------------------------------------
//  LE VRAI DESSIN, COMME SvgRenderer. Les memes peintres (app/hmi/HmiPainter :
//  paintHmiView) ecrivent ici ; au lieu d'allumer des pixels, on remplit un
//  TAMPON DE COMMANDES (des octets) que le JS rejoue sur un <canvas> 2D. Les
//  textes sont mesures DANS le wasm avec la police embarquee (gfx::FontAtlas), et
//  leur largeur est passee au JS : la mise en page est la meme que dans l'appli.
//
//  Les images (ressources IHM deja decodees en RGBA) sont donnees une fois,
//  citees par un identifiant ; le JS en fait des ImageBitmap.
//
//  Compile seulement pour la page (XPG_WEB).
// =============================================================================
#pragma once
#ifdef XPG_WEB

#include "../platform/Renderer.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gfx { class FontAtlas; }

namespace app::web {

// Les codes des commandes (un octet), lus par le JS (voir web_host.js).
enum class Op : std::uint8_t {
    End = 0, ClipPush, ClipPop, FillRect, StrokeRect, RoundRect, Line, Tris, Text, Image
};

// La police embarquee dans la page (un TTF) : posee une fois au demarrage du
// module. Sert aux mesures (meme metrique que le dessin du JS, qui charge le
// meme TTF par une FontFace).
void setEmbeddedFont(const std::uint8_t* ttf, std::size_t size);

class WebRenderer final : public gfx::IRenderer {
public:
    WebRenderer();
    ~WebRenderer() override;

    // Une nouvelle image : sa taille (pixels de la vue) et l'echelle de l'ecran (HiDPI).
    void begin(float width, float height);
    // Le tampon des commandes de dessin (a rejouer sur le canvas).
    [[nodiscard]] const std::vector<std::uint8_t>& commands() const noexcept { return cmd_; }
    // Les images nouvelles depuis le dernier envoi (id, w, h, puis w*h*4 octets RGBA).
    [[nodiscard]] const std::vector<std::uint8_t>& newImages() const noexcept { return assets_; }
    void clearNewImages() { assets_.clear(); }

    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override;
    void popClip() override;
    void fillRect(const gfx::Rect&, gfx::Color) override;
    void strokeRect(const gfx::Rect&, gfx::Color, float thickness = 1.f) override;
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float radius) override;
    void line(gfx::Point a, gfx::Point b, gfx::Color, float thickness = 1.f) override;
    void drawText(gfx::Point origin, std::string_view utf8, gfx::FontId, gfx::Color) override;
    void drawTexture(const gfx::Rect& dst, gfx::TextureId, gfx::Color tint) override;
    [[nodiscard]] gfx::TextMetrics measure(std::string_view utf8, gfx::FontId) const override;
    [[nodiscard]] float lineHeight(gfx::FontId) const override;
    [[nodiscard]] gfx::Size surfaceSize() const override { return {width_, height_}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view utf8, gfx::FontId, float maxWidth) const override;
    void fillTriangles(const gfx::Vertex* vertices, std::size_t count) override;
    void drawTextRotated(gfx::Point origin, std::string_view utf8, gfx::FontId, gfx::Color, float angleDeg, gfx::Point pivot) override;
    [[nodiscard]] gfx::TextureId createImage(const std::uint8_t* rgba, int w, int h) override;
    void releaseImage(gfx::TextureId) override;
    void drawImage(gfx::TextureId, const gfx::Rect& dst, float angleDeg, bool flipH, bool flipV, gfx::Color tint) override;

private:
    struct Image { int w{0}, h{0}; std::vector<std::uint8_t> rgba; };
    [[nodiscard]] gfx::FontAtlas* face(gfx::FontId) const;
    void text(gfx::Point origin, std::string_view utf8, gfx::FontId, gfx::Color, float angleDeg, gfx::Point pivot);
    void u8(std::uint8_t v) { cmd_.push_back(v); }
    void f32(float v);
    void i32(std::int32_t v);
    void col(gfx::Color c);

    std::vector<std::uint8_t>                             cmd_;
    std::vector<std::uint8_t>                             assets_;
    float                                                 width_{0}, height_{0};
    std::map<std::uint32_t, Image>                        textures_;
    std::uint32_t                                         nextTexture_{1};
    std::map<std::uint64_t, std::uint32_t>                imageIds_;   // contenu -> id d'image du JS
    std::uint32_t                                         nextImageId_{1};
    mutable std::map<int, std::unique_ptr<gfx::FontAtlas>> faces_;
};

} // namespace app::web

#endif // XPG_WEB
