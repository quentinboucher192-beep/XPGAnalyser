// =============================================================================
//  ui/Zoom.hpp - 1.12.30 : le zoom de chaque page
// -----------------------------------------------------------------------------
//  LE CONTRAT est dans Widget.hpp : un widget se zoome ou non (zoomSupport).
//  Scaled : la base le met en page dans un repere agrandi et le dessine a
//  l'echelle ; Native : il zoome lui-meme. Ce fichier donne ce qu'il faut pour
//  le faire :
//
//    ZoomMap       la correspondance entre deux reperes : p dehors = p dedans
//                  x s + t (le zoom d'un widget, ou celui de tous ses parents) ;
//    ZoomRenderer  un renderer qui dessine dans le repere agrandi : il passe
//                  chaque forme au renderer de dessous, a l'echelle. Le texte
//                  part a la taille dessinee (une police par taille : pas de
//                  flou), les bords des rectangles tombent sur des pixels
//                  entiers (pas de jour entre deux cases), un trait fin reste
//                  fin. Ce qui se dessine hors de l'ecran (une vignette) ne
//                  passe pas par l'echelle ;
//    les CRANS     50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200 % (et
//                  au-dela pour un zoom Native : de 10 a 800 %) ;
//    zoomableAt    le widget qu'un geste vise : le widget Native le plus
//                  profond sous la souris, sinon le widget Scaled le plus haut
//                  (le zoom d'un panneau couvre ses tableaux).
// =============================================================================
#pragma once

#include "../platform/Renderer.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace ui {

class Widget;

struct ZoomMap {
    float      s{1.f};
    gfx::Point t{0.f, 0.f};
    [[nodiscard]] bool identity() const noexcept { return s == 1.f && t.x == 0.f && t.y == 0.f; }
    [[nodiscard]] gfx::Point toOuter(gfx::Point p) const noexcept { return {p.x * s + t.x, p.y * s + t.y}; }
    [[nodiscard]] gfx::Point toInner(gfx::Point p) const noexcept { return {(p.x - t.x) / s, (p.y - t.y) / s}; }
    [[nodiscard]] gfx::Rect  toOuter(const gfx::Rect& r) const noexcept { return {r.x * s + t.x, r.y * s + t.y, r.w * s, r.h * s}; }
    [[nodiscard]] gfx::Rect  toInner(const gfx::Rect& r) const noexcept { return {(r.x - t.x) / s, (r.y - t.y) / s, r.w / s, r.h / s}; }
    // Le zoom `z` d'un cadre dont le coin haut-gauche ne bouge pas.
    [[nodiscard]] static ZoomMap about(gfx::Point corner, float z) noexcept {
        return {z, {corner.x * (1.f - z), corner.y * (1.f - z)}};
    }
    // `inner` puis `outer` (inner : d'un widget vers son parent ; outer : du parent vers le sien).
    [[nodiscard]] static ZoomMap then(const ZoomMap& inner, const ZoomMap& outer) noexcept {
        return {inner.s * outer.s, {inner.t.x * outer.s + outer.t.x, inner.t.y * outer.s + outer.t.y}};
    }
};

class ZoomRenderer final : public gfx::IRenderer {
public:
    ZoomRenderer(gfx::IRenderer& inner, ZoomMap map) noexcept : r_(inner), m_(map) {}
    [[nodiscard]] const ZoomMap& map() const noexcept { return m_; }

    void beginFrame(gfx::Color clear) override { r_.beginFrame(clear); }
    void endFrame() override { r_.endFrame(); }
    void pushClip(const gfx::Rect& r) override;
    void popClip() override { r_.popClip(); }

    void fillRect(const gfx::Rect&, gfx::Color) override;
    void strokeRect(const gfx::Rect&, gfx::Color, float thickness) override;
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float radius) override;
    void line(gfx::Point a, gfx::Point b, gfx::Color, float thickness) override;
    void drawText(gfx::Point origin, std::string_view utf8, gfx::FontId, gfx::Color) override;
    void drawTexture(const gfx::Rect& dst, gfx::TextureId, gfx::Color tint) override;

    [[nodiscard]] gfx::TextMetrics measure(std::string_view utf8, gfx::FontId) const override;
    [[nodiscard]] float            lineHeight(gfx::FontId) const override;
    [[nodiscard]] gfx::Size        surfaceSize() const override;
    [[nodiscard]] float            dpiScale() const override { return r_.dpiScale(); }
    [[nodiscard]] std::size_t      fitCharacters(std::string_view utf8, gfx::FontId, float maxWidth) const override;

    void fillTriangles(const gfx::Vertex* vertices, std::size_t count) override;
    void drawTextRotated(gfx::Point origin, std::string_view utf8, gfx::FontId font, gfx::Color c,
                         float angleDeg, gfx::Point pivot) override;
    [[nodiscard]] gfx::TextureId createImage(const std::uint8_t* rgba, int w, int h) override { return r_.createImage(rgba, w, h); }
    void releaseImage(gfx::TextureId id) override { r_.releaseImage(id); }
    void drawImage(gfx::TextureId id, const gfx::Rect& dst, float angleDeg, bool flipH, bool flipV, gfx::Color tint) override;
    [[nodiscard]] bool readPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override { return r_.readPixels(rgba, w, h); }
    [[nodiscard]] bool beginOffscreen(int w, int h, gfx::Color clear) override;
    [[nodiscard]] bool endOffscreen(std::vector<std::uint8_t>& rgba, int& w, int& h) override;

    // Un cadre du repere agrandi, sur des pixels entiers (les bords ; un trait fin : sa largeur).
    [[nodiscard]] gfx::Rect snapped(const gfx::Rect& r) const noexcept;

private:
    [[nodiscard]] bool scaling() const noexcept { return offscreen_ == 0; }
    [[nodiscard]] gfx::FontId font(gfx::FontId f) const noexcept;
    gfx::IRenderer& r_;
    ZoomMap         m_;
    int             offscreen_{0};   // un dessin hors de l'ecran en cours : pas d'echelle
};

// LES CRANS. Le cran au-dessus (step > 0) ou au-dessous de `current`, dans
// [lo, hi] ; `current` hors des crans : le plus proche dans ce sens.
[[nodiscard]] const std::vector<float>& zoomSteps();
[[nodiscard]] float zoomStep(float current, int step, float lo, float hi) noexcept;
// Un pourcentage lisible : 150 pour 1,5 (arrondi).
[[nodiscard]] int zoomPercent(float zoom) noexcept;

// Le widget zoomable qu'un geste vise au point `surface` sous `root` (sa
// surface) : le widget Native le plus profond, sinon le Scaled le plus haut ;
// nul : aucun.
[[nodiscard]] Widget* zoomableAt(Widget& root, gfx::Point surface);
// Les widgets Scaled les plus hauts sous `root` (montres ou non : une page
// d'onglet cachee suit le zoom de sa page) ; ceux qui sont sous un autre
// Scaled sont couverts par lui.
[[nodiscard]] std::vector<Widget*> scaledUnder(Widget& root);
// Les widgets Native sous `root`, montres ; le plus grand d'abord.
[[nodiscard]] std::vector<Widget*> nativesUnder(Widget& root);

} // namespace ui
