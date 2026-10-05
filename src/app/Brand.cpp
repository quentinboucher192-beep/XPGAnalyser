#include "Brand.hpp"

#include "hmi/HmiImages.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace app::brand {

std::string_view logoSvg() {
    // resources/logo/xpg_analyzer.svg, sans son commentaire.
    static const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">\n"
        "  <defs>\n"
        "    <linearGradient id=\"fond\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#23527F\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#0C1826\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"bleu\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#7CC4FF\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#2F6FD6\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"vert\" x1=\"1\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
        "      <stop offset=\"0\" stop-color=\"#5BE08F\"/>\n"
        "      <stop offset=\"1\" stop-color=\"#1E9E57\"/>\n"
        "    </linearGradient>\n"
        "  </defs>\n"
        "  <rect x=\"8\" y=\"8\" width=\"240\" height=\"240\" rx=\"54\" ry=\"54\" fill=\"url(#fond)\"/>\n"
        "  <rect x=\"10\" y=\"10\" width=\"236\" height=\"236\" rx=\"52\" ry=\"52\" fill=\"none\" stroke=\"#FFFFFF\" stroke-opacity=\"0.14\" stroke-width=\"4\"/>\n"
        "  <rect x=\"50\" y=\"52\" width=\"24\" height=\"152\" rx=\"12\" ry=\"12\" fill=\"#E9EFF7\"/>\n"
        "  <rect x=\"182\" y=\"52\" width=\"24\" height=\"152\" rx=\"12\" ry=\"12\" fill=\"#E9EFF7\"/>\n"
        "  <line x1=\"92\" y1=\"82\" x2=\"164\" y2=\"174\" stroke=\"url(#bleu)\" stroke-width=\"26\" stroke-linecap=\"round\"/>\n"
        "  <line x1=\"164\" y1=\"82\" x2=\"92\" y2=\"174\" stroke=\"url(#vert)\" stroke-width=\"26\" stroke-linecap=\"round\"/>\n"
        "  <circle cx=\"128\" cy=\"128\" r=\"19\" fill=\"#F2C94C\" stroke=\"#0C1826\" stroke-width=\"7\"/>\n"
        "</svg>\n";
    return kSvg;
}

const hmi::Resource& logoResource() {
    static const hmi::Resource res = [] {
        hmi::Resource r;
        r.name = "xpg_analyzer.svg";
        r.format = "SVG";
        const std::string_view svg = logoSvg();
        r.data = std::make_shared<const hmi::Bytes>(svg.begin(), svg.end());
        return r;
    }();
    return res;
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
    else r.fillRoundedRect(dst, gfx::Color::rgb(0x23527F), side * 0.2f);   // un renderer sans images
}

} // namespace app::brand
