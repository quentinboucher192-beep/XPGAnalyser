// =============================================================================
//  app/HeadlessRenderer.hpp - 1.50.1 : LE STUDIO SANS ECRAN (une session rejouee)
// -----------------------------------------------------------------------------
//  La construction Linux des essais n'a pas SDL (XPG_WITH_SDL=OFF) : jusqu'ici
//  IHM-Studio / API-Studio y refusaient de demarrer (<< compiled without SDL3 >>),
//  seul --cli marchait. L'essai de bout en bout des sessions (deux Studio sur ce
//  PC, l'un hote, l'autre equipier, sur la boucle locale : sessione2e) a besoin
//  de deux VRAIS Studio qui rejouent un script.
//
//  SANS ECRAN (XPG_SANS_ECRAN=1, avec --script, dans une construction sans SDL) :
//  l'application tourne entiere - les ecrans, les widgets, la mise en page, le
//  script, les sessions - a l'horloge fixe d'une session rejouee (1/30 s par
//  image), une image tous les 1/60 s de temps reel (comme une fenetre a 60 Hz :
//  les attentes du script en images gardent leur duree). Ce renderer MESURE les
//  textes avec la vraie police (resources/fonts, sinon celles du systeme : les
//  lignes se coupent comme a l'ecran) et ne dessine rien ; le presse-papiers est
//  en memoire (Copier puis le lire : session-presse-papiers). Une capture n'est
//  pas possible (aucun pixel) : la commande le dit.
// =============================================================================
#pragma once

#include "../platform/FontAtlas.hpp"
#include "../platform/Renderer.hpp"

#include <map>
#include <string>

namespace app {

// Le Studio doit-il tourner sans ecran ? (XPG_SANS_ECRAN=1 et une session rejouee)
[[nodiscard]] bool headlessWanted(const std::string& script);

class HeadlessRenderer final : public gfx::IRenderer {
public:
    HeadlessRenderer(int w, int h) : w_(w), h_(h) {}

    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}

    [[nodiscard]] gfx::TextMetrics measure(std::string_view utf8, gfx::FontId) const override;
    [[nodiscard]] float            lineHeight(gfx::FontId) const override;
    [[nodiscard]] gfx::Size        surfaceSize() const override { return {static_cast<float>(w_), static_cast<float>(h_)}; }
    [[nodiscard]] float            dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t      fitCharacters(std::string_view utf8, gfx::FontId, float maxWidth) const override;
    void resetFonts() override { faces_.clear(); }

private:
    gfx::FontAtlas* faceFor(gfx::FontId f) const;
    int w_, h_;
    mutable std::map<int, gfx::FontAtlas> faces_;
};

} // namespace app
