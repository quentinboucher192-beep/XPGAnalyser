// =============================================================================
//  app/Brand.hpp - le logo de l'application (lot 12)
// -----------------------------------------------------------------------------
//  1.12.20 : DEUX LOGOS DE LA MEME FAMILLE (le cadre arrondi, les traits clairs, le
//  point jaune) : API Studio, un barreau de schema a contacts (un contact et sa
//  bobine) ; IHM Studio, un ecran de supervision (un cadran, son aiguille). Un SVG
//  chacun, UNE SEULE SOURCE pour tout -
//
//    la fenetre      l'icone posee a l'ouverture (SDL_SetWindowIcon), a
//                    plusieurs tailles ;
//    l'accueil       la marque a gauche du titre (LogoMark) ;
//    l'executable    Windows : resources/windows/xpg_analyzer.rc.in lie
//                    resources/logo/api_studio.ico a API-Studio.exe et
//                    ihm_studio.ico a IHM-Studio.exe (l'Explorateur, la barre
//                    des taches, Alt+Tab) ;
//    les fichiers    resources/logo/ : les SVG, les PNG de 16 a 512, les ICO, et
//                    les images de l'installateur (tools/logo/generer_logo.py
//                    les refait depuis les SVG).
// =============================================================================
#pragma once

#include "../hmi/HmiMedia.hpp"
#include "../hmi/HmiModel.hpp"
#include "../platform/Renderer.hpp"
#include "../ui/Widget.hpp"
#include "../core/Edition.hpp"

#include <string_view>

namespace app::brand {

// Le SVG du logo de l'application lancee (256 x 256), tel qu'embarque dans l'executable ;
// celui d'une edition (Both : celui d'API Studio).
[[nodiscard]] std::string_view logoSvg();
[[nodiscard]] std::string_view logoSvg(core::Edition);
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
