#include "Brand.hpp"

#include "hmi/HmiImages.hpp"
#include "../core/Edition.hpp"   // 1.12.20 : un logo par application

#include <algorithm>
#include <cmath>
#include <memory>

namespace app::brand {

std::string_view logoSvg(core::Edition edition) {
    // 1.12.20 : deux logos de la meme famille - resources/logo/api_studio.svg (API Studio : un barreau
    // de schema a contacts) et ihm_studio.svg (IHM Studio : un ecran de supervision), sans leur commentaire.
    static const char* kApi =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">\n"
        "  <defs>\n"
        "    <linearGradient id=\"fond\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#2A64AB\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#0B182B\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"accent\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#8ED0FF\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#3478DC\"/>\n"
        "    </linearGradient>\n"
        "  </defs>\n"
        "  <rect x=\"8\" y=\"8\" width=\"240\" height=\"240\" rx=\"54\" ry=\"54\" fill=\"url(#fond)\"/>\n"
        "  <rect x=\"10\" y=\"10\" width=\"236\" height=\"236\" rx=\"52\" ry=\"52\" fill=\"none\" stroke=\"#FFFFFF\" stroke-opacity=\"0.14\" stroke-width=\"4\"/>\n"
        "  <rect x=\"36\" y=\"50\" width=\"24\" height=\"156\" rx=\"12\" ry=\"12\" fill=\"#E9EFF7\"/>\n"
        "  <rect x=\"196\" y=\"50\" width=\"24\" height=\"156\" rx=\"12\" ry=\"12\" fill=\"#E9EFF7\"/>\n"
        "  <path d=\"M60 128H82M132 128H150M186 128H196\" stroke=\"#E9EFF7\" stroke-width=\"12\" fill=\"none\"/>\n"
        "  <rect x=\"80\" y=\"88\" width=\"20\" height=\"80\" rx=\"6\" ry=\"6\" fill=\"url(#accent)\"/>\n"
        "  <rect x=\"114\" y=\"88\" width=\"20\" height=\"80\" rx=\"6\" ry=\"6\" fill=\"url(#accent)\"/>\n"
        "  <circle cx=\"168\" cy=\"128\" r=\"19\" fill=\"#F2C94C\" stroke=\"#0B182B\" stroke-width=\"7\"/>\n"
        "</svg>\n";
    static const char* kIhm =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">\n"
        "  <defs>\n"
        "    <linearGradient id=\"fond\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#1E8C86\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#08201F\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"accent\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">\n"
        "      <stop offset=\"0\" stop-color=\"#5BE08F\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#2FC1D6\"/>\n"
        "    </linearGradient>\n"
        "  </defs>\n"
        "  <rect x=\"8\" y=\"8\" width=\"240\" height=\"240\" rx=\"54\" ry=\"54\" fill=\"url(#fond)\"/>\n"
        "  <rect x=\"10\" y=\"10\" width=\"236\" height=\"236\" rx=\"52\" ry=\"52\" fill=\"none\" stroke=\"#FFFFFF\" stroke-opacity=\"0.14\" stroke-width=\"4\"/>\n"
        "  <rect x=\"36\" y=\"48\" width=\"184\" height=\"132\" rx=\"18\" ry=\"18\" fill=\"#E9EFF7\"/>\n"
        "  <rect x=\"50\" y=\"62\" width=\"156\" height=\"104\" rx=\"9\" ry=\"9\" fill=\"#0E2B2D\"/>\n"
        "  <rect x=\"116\" y=\"180\" width=\"24\" height=\"16\" fill=\"#E9EFF7\"/>\n"
        "  <rect x=\"80\" y=\"192\" width=\"96\" height=\"16\" rx=\"8\" ry=\"8\" fill=\"#E9EFF7\"/>\n"
        "  <path d=\"M82 150A46 46 0 0 1 174 150\" stroke=\"url(#accent)\" stroke-width=\"13\" stroke-linecap=\"round\" fill=\"none\"/>\n"
        "  <line x1=\"128\" y1=\"150\" x2=\"156\" y2=\"116\" stroke=\"#E9EFF7\" stroke-width=\"9\" stroke-linecap=\"round\"/>\n"
        "  <circle cx=\"128\" cy=\"150\" r=\"13\" fill=\"#F2C94C\" stroke=\"#0E2B2D\" stroke-width=\"6\"/>\n"
        "</svg>\n";
    return edition == core::Edition::Ihm ? kIhm : kApi;
}

std::string_view logoSvg() { return logoSvg(core::edition()); }

const hmi::Resource& logoResource() {
    // Une ressource par logo (l'edition est posee au lancement ; les essais passent de l'une a l'autre).
    const auto make = [](core::Edition e, const char* name) {
        hmi::Resource r;
        r.name = name;
        r.format = "SVG";
        const std::string_view svg = logoSvg(e);
        r.data = std::make_shared<const hmi::Bytes>(svg.begin(), svg.end());
        return r;
    };
    static const hmi::Resource api = make(core::Edition::Api, "api_studio.svg");
    static const hmi::Resource ihm = make(core::Edition::Ihm, "ihm_studio.svg");
    return core::edition() == core::Edition::Ihm ? ihm : api;
}

bool logoPixels(int size, hmi::Rgba& out) {
    const auto& res = logoResource();
    return res.data && hmi::rasterizeSvg(*res.data, std::max(1, size), std::max(1, size), out);
}

void drawLogo(gfx::IRenderer& r, const gfx::Rect& box) {
    const float side = std::floor(std::min(box.w, box.h));
    if (side < 4.f) return;
    const gfx::Rect dst{std::round(box.x + (box.w - side) / 2.f), std::round(box.y + (box.h - side) / 2.f), side, side};
    // Rasterise a la taille des pixels de l'ecran : net sur un ecran haute densite.
    const float scale = std::max(1.f, r.dpiScale());
    const int px = static_cast<int>(std::lround(side * scale));
    const auto img = HmiImageCache::instance().svg(r, logoResource(), px, px, hmi::SvgRecolor{});
    if (img.valid()) r.drawImage(img.tex, dst);
    else r.fillRoundedRect(dst, gfx::Color::rgb(core::edition() == core::Edition::Ihm ? 0x1E8C86 : 0x2A64AB), side * 0.2f);   // un renderer sans images
}

} // namespace app::brand
