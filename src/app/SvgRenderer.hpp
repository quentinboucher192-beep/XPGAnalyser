// =============================================================================
//  app/SvgRenderer.hpp - 1.12.30 : le dessin de l'appli, ecrit en SVG
// -----------------------------------------------------------------------------
//  LE VRAI DESSIN, PAS UNE COPIE. Le renderer est l'unique passage entre le
//  dessin d'une vue IHM (app/hmi/HmiPainter : paintHmiView, le meme que
//  l'editeur et la simulation) et l'ecran. Celui-ci, au lieu d'allumer des
//  pixels, ecrit chaque primitive en SVG : un rectangle, un trait, des triangles
//  (une forme tournee, une ellipse, un polygone : un chemin par couleur), un
//  texte (sa police, sa taille, sa largeur mesuree - textLength : le navigateur
//  le pose exactement ou l'appli l'aurait pose), une image (une fois dans la
//  page, citee par <use>). Les decoupes deviennent des clipPath.
//
//  LES MESURES DU TEXTE viennent de la police de l'interface (gfx::FontAtlas, sans
//  SDL : Segoe UI sous Windows) ; sans police, une estimation.
//
//  LES IMAGES, COMMUNES A TOUTE LA PAGE (SvgImages) : un logo cite par vingt vues
//  n'est ecrit qu'une fois, en PNG (ou en JPEG pour une grande image opaque).
// =============================================================================
#pragma once

#include "../platform/Renderer.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gfx { class FontAtlas; }

namespace app {

// Les images d'une page : chacune une fois, identifiee par son contenu (et sa teinte).
class SvgImages {
public:
    // L'identifiant de l'image (son <image> dans les defs) ; l'encode au premier appel.
    [[nodiscard]] std::string idFor(const std::vector<std::uint8_t>& rgba, int w, int h, gfx::Color tint);
    // Les <image> a mettre dans un <defs> commun a la page.
    [[nodiscard]] const std::string& defs() const noexcept { return defs_; }
    [[nodiscard]] std::size_t count() const noexcept { return ids_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return defs_.size(); }

private:
    std::map<std::uint64_t, std::string> ids_;
    std::string                          defs_;
};

class SvgRenderer final : public gfx::IRenderer {
public:
    explicit SvgRenderer(SvgImages& images);
    ~SvgRenderer() override;

    // Une nouvelle image : sa taille, et le prefixe de ses identifiants (unique dans la page).
    void begin(float width, float height, std::string idPrefix);
    // Ce qui a ete dessine depuis begin : le contenu d'un <svg> (ses defs locales comprises).
    [[nodiscard]] std::string body() const;
    // La police des textes, telle que le CSS la nomme ("'Segoe UI', Tahoma, sans-serif").
    [[nodiscard]] std::string fontFamily() const;
    [[nodiscard]] std::size_t elements() const noexcept { return elements_; }

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
    [[nodiscard]] float ascent(gfx::FontId) const;
    void text(gfx::Point origin, std::string_view utf8, gfx::FontId, gfx::Color, const std::string& transform);

    SvgImages&                                             images_;
    float                                                  width_{0}, height_{0};
    std::string                                            prefix_;
    std::string                                            out_, defs_;
    int                                                    clips_{0}, open_{0};
    std::size_t                                            elements_{0};
    std::map<std::uint32_t, Image>                         textures_;
    std::uint32_t                                          nextTexture_{1};
    mutable std::map<int, std::unique_ptr<gfx::FontAtlas>> faces_;
    mutable std::string                                    family_;
};

} // namespace app
