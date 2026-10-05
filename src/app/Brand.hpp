// =============================================================================
//  app/Brand.hpp - le logo de l'application (lot 12)
// -----------------------------------------------------------------------------
//  Deux rails de schema a contacts, un X de deux barreaux croises, le point
//  d'analyse ou ils se rencontrent : un SVG, UNE SEULE SOURCE pour tout -
//
//    la fenetre      l'icone posee a l'ouverture (SDL_SetWindowIcon), a
//                    plusieurs tailles ;
//    l'accueil       la marque a gauche du titre (LogoMark) ;
//    l'executable    Windows : resources/windows/xpg_analyzer.rc lie
//                    resources/logo/xpg_analyzer.ico a xpg_analyzer.exe
//                    (l'Explorateur, la barre des taches, Alt+Tab) ;
//    les fichiers    resources/logo/ : le SVG, les PNG de 16 a 512, l'ICO
//                    (tools/logo/generer_logo.py les refait depuis le SVG).
// =============================================================================
#pragma once

#include "../hmi/HmiMedia.hpp"
#include "../hmi/HmiModel.hpp"
#include "../platform/Renderer.hpp"
#include "../ui/Widget.hpp"

#include <string_view>

namespace app::brand {

// Le SVG du logo (256 x 256), tel qu'embarque dans l'executable.
[[nodiscard]] std::string_view logoSvg();
// Une ressource IHM qui le porte (pour le cache des images SVG).
[[nodiscard]] const hmi::Resource& logoResource();
// Le logo en pixels RGBA, carre de `size` pixels ; faux si le SVG ne se lit pas.
[[nodiscard]] bool logoPixels(int size, hmi::Rgba& out);
// Le logo dans une boite (un carre centre), net a sa taille.
void drawLogo(gfx::IRenderer&, const gfx::Rect& box);

// La marque, a sa taille (un carre) : l'accueil la pose devant son titre.
class LogoMark final : public ui::Widget {
public:
    LogoMark(std::string id, float side) : ui::Widget(std::move(id)), side_(side) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {side_, side_};
        h.minimum = {side_, side_};
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override { drawLogo(ctx.r, bounds()); }
private:
    float side_;
};

} // namespace app::brand
