// =============================================================================
//  platform/Renderer.hpp — abstract 2D renderer + SDL3 implementation
// -----------------------------------------------------------------------------
//  Widgets never call SDL directly; they emit primitives into a DrawList. The
//  renderer consumes the DrawList once per frame. Two consequences:
//    * widget code is unit-testable against a RecordingRenderer;
//    * the whole UI can be repainted from a cached DrawList when nothing
//      changed, which is what keeps a 50 000-row table at 60 fps.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "Geometry.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct SDL_Renderer;
struct SDL_Window;

namespace gfx {

struct TextMetrics { float width{}, height{}, ascent{}, descent{}; };

// Un sommet de triangle colore : ce que l'editeur IHM envoie pour une forme
// tournee, une ellipse, un polygone.
struct Vertex { Point pos; Color color; };

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // LA COULEUR D'EFFACEMENT EST UN ARGUMENT, PAS UN REGLAGE INTERNE.
    //
    // Elle etait ecrite en dur - 30, 30, 30, l'ancien gris sombre - et ignorait
    // donc le theme. En clair, tout ce qu'aucun widget ne peignait restait
    // presque noir : la bande du haut, les marges, le tour des panneaux. On
    // voyait une application claire posee sur un fond de theme sombre.
    //
    // En la passant a chaque trame, il n'existe plus d'etat a tenir a jour, et
    // changer de theme suffit.
    virtual void beginFrame(Color clear)  = 0;
    virtual void endFrame()              = 0;
    virtual void pushClip(const Rect&)   = 0;
    virtual void popClip()               = 0;

    virtual void fillRect(const Rect&, Color)                          = 0;
    virtual void strokeRect(const Rect&, Color, float thickness = 1.f) = 0;
    virtual void fillRoundedRect(const Rect&, Color, float radius)     = 0;
    virtual void line(Point a, Point b, Color, float thickness = 1.f)  = 0;
    virtual void drawText(Point origin, std::string_view utf8, FontId, Color) = 0;
    virtual void drawTexture(const Rect& dst, TextureId, Color tint = {255,255,255,255}) = 0;

    [[nodiscard]] virtual TextMetrics measure(std::string_view utf8, FontId) const = 0;
    [[nodiscard]] virtual float       lineHeight(FontId) const = 0;
    [[nodiscard]] virtual Size        surfaceSize() const = 0;
    [[nodiscard]] virtual float       dpiScale() const = 0;

    // Glyph-accurate horizontal truncation used by every cell renderer.
    [[nodiscard]] virtual std::size_t fitCharacters(std::string_view utf8, FontId, float maxWidth) const = 0;

    // ---- formes libres et texte tourne (editeur de vues IHM) ----------------
    //  AJOUTES AVEC UN CORPS PAR DEFAUT, ET PAS EN VIRTUELLES PURES : les
    //  renderers des tests (enregistreurs, mesures a huit pixels) compilent
    //  sans changement ; tant qu'ils ne les redefinissent pas, ils ne dessinent
    //  simplement pas ces formes.
    //
    //  Des triangles colores, trois sommets par triangle.
    virtual void fillTriangles(const Vertex* vertices, std::size_t count) {
        (void)vertices; (void)count;
    }
    //  Du texte dans un repere tourne : `origin` est le coin haut-gauche du
    //  texte avant rotation ; le tout tourne de `angleDeg` degres (sens horaire
    //  a l'ecran) autour de `pivot`. Le texte n'est jamais retourne en miroir :
    //  un symbole se retourne, son libelle reste lisible.
    virtual void drawTextRotated(Point origin, std::string_view utf8, FontId font, Color c,
                                 float angleDeg, Point pivot) {
        (void)angleDeg; (void)pivot;
        drawText(origin, utf8, font, c);
    }
    // ---- des images (ressources IHM : PNG, JPEG, SVG... deja decodees) -------
    //  Une image RGBA (4 octets par pixel) devient une texture du renderer ;
    //  l'identifiant reste valable jusqu'a releaseImage. {0} : pas d'image (un
    //  renderer de test) - l'appelant dessine alors un cadre a la place.
    [[nodiscard]] virtual TextureId createImage(const std::uint8_t* rgba, int w, int h) {
        (void)rgba; (void)w; (void)h;
        return {};
    }
    virtual void releaseImage(TextureId id) { (void)id; }
    //  L'image dans `dst`, tournee de `angleDeg` (sens horaire) autour du centre
    //  de `dst`, retournee au besoin ; `tint` multiplie ses couleurs et son
    //  alpha (opacite, ou couleur d'un texte ecrit en blanc).
    virtual void drawImage(TextureId id, const Rect& dst, float angleDeg = 0.f, bool flipH = false,
                           bool flipV = false, Color tint = {255, 255, 255, 255}) {
        (void)id; (void)dst; (void)angleDeg; (void)flipH; (void)flipV; (void)tint;
    }
    //  L'image deja dessinee, en RGBA, pour les captures (F12, scripts). Rend
    //  faux quand le renderer ne sait pas relire ses pixels.
    [[nodiscard]] virtual bool readPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) {
        (void)rgba; w = h = 0;
        return false;
    }
    //  Lot 13 : DESSINER HORS DE L'ECRAN (les vignettes du dossier de l'IHM). Ce
    //  qui se dessine entre begin et end va dans une image w x h, effacee a
    //  `clear` ; end la rend en RGBA et revient a l'ecran (sa decoupe comprise).
    //  Faux : ce renderer ne sait pas (les renderers des tests).
    [[nodiscard]] virtual bool beginOffscreen(int w, int h, Color clear) {
        (void)w; (void)h; (void)clear;
        return false;
    }
    [[nodiscard]] virtual bool endOffscreen(std::vector<std::uint8_t>& rgba, int& w, int& h) {
        (void)rgba; w = h = 0;
        return false;
    }
};

// --- SDL3 backend ----------------------------------------------------------
class SdlRenderer final : public IRenderer {
public:
    static core::Result<std::unique_ptr<SdlRenderer>> create(SDL_Window* window);
    ~SdlRenderer() override;

    void beginFrame(Color clear) override;
    void endFrame() override;
    void pushClip(const Rect&) override;
    void popClip() override;

    void fillRect(const Rect&, Color) override;
    void strokeRect(const Rect&, Color, float) override;
    void fillRoundedRect(const Rect&, Color, float) override;
    void line(Point, Point, Color, float) override;
    void drawText(Point, std::string_view, FontId, Color) override;
    void drawTexture(const Rect&, TextureId, Color) override;

    [[nodiscard]] TextMetrics measure(std::string_view, FontId) const override;
    [[nodiscard]] float       lineHeight(FontId) const override;
    [[nodiscard]] Size        surfaceSize() const override;
    [[nodiscard]] float       dpiScale() const override;
    [[nodiscard]] std::size_t fitCharacters(std::string_view, FontId, float) const override;

    void fillTriangles(const Vertex*, std::size_t) override;
    void drawTextRotated(Point, std::string_view, FontId, Color, float, Point) override;
    [[nodiscard]] bool readPixels(std::vector<std::uint8_t>&, int&, int&) override;
    [[nodiscard]] bool beginOffscreen(int, int, Color) override;
    [[nodiscard]] bool endOffscreen(std::vector<std::uint8_t>&, int&, int&) override;
    [[nodiscard]] TextureId createImage(const std::uint8_t*, int, int) override;
    void releaseImage(TextureId) override;
    void drawImage(TextureId, const Rect&, float, bool, bool, Color) override;

private:
    struct Impl;                          // SDL_Renderer*, glyph atlas, clip stack
    explicit SdlRenderer(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;          // pimpl: SDL headers stay out of the UI layer
};

} // namespace gfx
