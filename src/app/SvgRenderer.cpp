// =============================================================================
//  app/SvgRenderer.cpp - 1.12.30 : voir SvgRenderer.hpp
// =============================================================================
#include "SvgRenderer.hpp"

#include "Capture.hpp"
#include "../platform/FontAtlas.hpp"
#include "../project/ProjectArchive.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace app {

namespace {

std::string num(float v) {
    if (!std::isfinite(v)) return "0";
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0" || s.empty()) s = "0";
    return s;
}

// Une echelle : assez de chiffres (une grande image reduite : 0,00431).
std::string fine(float v) {
    if (!std::isfinite(v)) return "1";
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", static_cast<double>(v));
    return b;
}

std::string hex(gfx::Color c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

// fill="#rrggbb" fill-opacity=".5" (l'opacite : seulement quand il y en a une).
std::string paint(const char* what, gfx::Color c) {
    std::string out = std::string(" ") + what + "=\"" + hex(c) + "\"";
    if (c.a < 255) out += std::string(" ") + what + "-opacity=\"" + num(static_cast<float>(c.a) / 255.f) + "\"";
    return out;
}

std::string esc(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    return out;
}

std::size_t codePoints(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

std::uint64_t fnv(const std::uint8_t* d, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

// La famille CSS d'un fichier de police connu.
std::string familyOf(const std::string& path) {
    std::string leaf = std::filesystem::path(path).filename().string();
    for (auto& c : leaf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (leaf.rfind("segoeui", 0) == 0) return "Segoe UI";
    if (leaf.rfind("tahoma", 0) == 0) return "Tahoma";
    if (leaf.rfind("dejavusans", 0) == 0) return "DejaVu Sans";
    if (leaf.rfind("liberationsans", 0) == 0) return "Liberation Sans";
    if (leaf.rfind("inter", 0) == 0) return "Inter";
    if (leaf.rfind("arial", 0) == 0) return "Arial";
    return {};
}

constexpr float kApproxAdvance = 0.55f;   // sans police : la largeur moyenne d'un caractere, en hauteurs

} // namespace

// ================================================================== les images ==
std::string SvgImages::idFor(const std::vector<std::uint8_t>& rgba, int w, int h, gfx::Color tint) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) return {};
    const bool tinted = tint.r != 255 || tint.g != 255 || tint.b != 255;
    std::uint64_t key = fnv(rgba.data(), rgba.size());
    key = fnv(reinterpret_cast<const std::uint8_t*>(&w), sizeof w, key);
    key = fnv(reinterpret_cast<const std::uint8_t*>(&h), sizeof h, key);
    if (tinted) {
        const std::uint8_t t[3] = {tint.r, tint.g, tint.b};
        key = fnv(t, 3, key);
    }
    if (const auto it = ids_.find(key); it != ids_.end()) return it->second;
    // Une couleur de texte posee sur une image blanche (une police importee) : teinte une fois.
    std::vector<std::uint8_t> pixels = rgba;
    bool opaque = true;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (tinted) {
            pixels[i] = static_cast<std::uint8_t>(pixels[i] * tint.r / 255);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] * tint.g / 255);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] * tint.b / 255);
        }
        opaque = opaque && pixels[i + 3] == 255;
    }
    // Une grande image opaque (une photo) : en JPEG ; le reste en PNG (la transparence).
    std::vector<std::uint8_t> encoded;
    std::string mime = "image/png";
    if (opaque && w * h >= 96 * 96 && encodeJpeg(pixels, w, h, 88, encoded)) {
        mime = "image/jpeg";
    } else if (!encodePng(pixels, w, h, encoded)) {
        return {};
    }
    const std::string id = "xi" + std::to_string(ids_.size() + 1);
    defs_ += "<image id=\"" + id + "\" width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h)
           + "\" preserveAspectRatio=\"none\" href=\"data:" + mime + ";base64,"
           + project::archive::base64(encoded.data(), encoded.size()) + "\"/>\n";
    ids_[key] = id;
    return id;
}

// ================================================================== le renderer ==
SvgRenderer::SvgRenderer(SvgImages& images) : images_(images) {}
SvgRenderer::~SvgRenderer() = default;

void SvgRenderer::begin(float width, float height, std::string idPrefix) {
    width_ = width;
    height_ = height;
    prefix_ = std::move(idPrefix);
    out_.clear();
    defs_.clear();
    clips_ = 0;
    open_ = 0;
    elements_ = 0;
}

std::string SvgRenderer::body() const {
    std::string out;
    if (!defs_.empty()) out += "<defs>" + defs_ + "</defs>";
    out += out_;
    for (int i = 0; i < open_; ++i) out += "</g>";
    return out;
}

gfx::FontAtlas* SvgRenderer::face(gfx::FontId f) const {
    const int px = std::max(1, static_cast<int>(f.v));
    if (const auto it = faces_.find(px); it != faces_.end()) return it->second && it->second->ready() ? it->second.get() : nullptr;
    auto atlas = std::make_unique<gfx::FontAtlas>();
    for (const auto& path : gfx::defaultUiFontCandidates())
        if (atlas->load(path, static_cast<float>(px))) {
            if (family_.empty()) family_ = familyOf(path);
            break;
        }
    auto& slot = faces_[px];
    slot = std::move(atlas);
    return slot->ready() ? slot.get() : nullptr;
}

float SvgRenderer::ascent(gfx::FontId f) const {
    if (auto* a = face(f)) return a->ascent();
    return static_cast<float>(f.v) * 0.8f;
}

std::string SvgRenderer::fontFamily() const {
    std::string out;
    if (!family_.empty()) out += "'" + family_ + "', ";
    out += "'Segoe UI', Tahoma, 'DejaVu Sans', Arial, sans-serif";
    return out;
}

void SvgRenderer::pushClip(const gfx::Rect& r) {
    const std::string id = prefix_ + "c" + std::to_string(++clips_);
    defs_ += "<clipPath id=\"" + id + "\"><rect x=\"" + num(r.x) + "\" y=\"" + num(r.y) + "\" width=\"" + num(std::max(0.f, r.w))
           + "\" height=\"" + num(std::max(0.f, r.h)) + "\"/></clipPath>";
    out_ += "<g clip-path=\"url(#" + id + ")\">";
    ++open_;
}

void SvgRenderer::popClip() {
    if (open_ <= 0) return;
    out_ += "</g>";
    --open_;
}

void SvgRenderer::fillRect(const gfx::Rect& r, gfx::Color c) {
    if (r.empty() || c.a == 0) return;
    out_ += "<rect x=\"" + num(r.x) + "\" y=\"" + num(r.y) + "\" width=\"" + num(r.w) + "\" height=\"" + num(r.h) + "\"" + paint("fill", c) + "/>";
    ++elements_;
}

void SvgRenderer::strokeRect(const gfx::Rect& r, gfx::Color c, float thickness) {
    if (r.empty() || c.a == 0) return;
    // Comme l'ecran : le trait est a l'interieur du rectangle.
    const float t = std::max(1.f, std::floor(thickness));
    out_ += "<rect x=\"" + num(r.x + t / 2) + "\" y=\"" + num(r.y + t / 2) + "\" width=\"" + num(std::max(0.f, r.w - t)) + "\" height=\""
          + num(std::max(0.f, r.h - t)) + "\" fill=\"none\"" + paint("stroke", c) + " stroke-width=\"" + num(t) + "\"/>";
    ++elements_;
}

void SvgRenderer::fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) {
    if (r.empty() || c.a == 0) return;
    const float rad = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    out_ += "<rect x=\"" + num(r.x) + "\" y=\"" + num(r.y) + "\" width=\"" + num(r.w) + "\" height=\"" + num(r.h) + "\" rx=\"" + num(rad)
          + "\"" + paint("fill", c) + "/>";
    ++elements_;
}

void SvgRenderer::line(gfx::Point a, gfx::Point b, gfx::Color c, float thickness) {
    if (c.a == 0) return;
    out_ += "<line x1=\"" + num(a.x) + "\" y1=\"" + num(a.y) + "\" x2=\"" + num(b.x) + "\" y2=\"" + num(b.y) + "\"" + paint("stroke", c)
          + " stroke-width=\"" + num(std::max(1.f, std::floor(thickness))) + "\"/>";
    ++elements_;
}

void SvgRenderer::text(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c, const std::string& transform) {
    if (utf8.empty() || c.a == 0) return;
    const float px = static_cast<float>(std::max<std::uint16_t>(1, f.v));
    const float width = measure(utf8, f).width;
    out_ += "<text x=\"" + num(origin.x) + "\" y=\"" + num(origin.y + ascent(f)) + "\" font-size=\"" + num(px) + "\"" + paint("fill", c);
    // La largeur que l'appli a mesuree : le navigateur pose le texte la ou l'appli l'aurait pose.
    if (codePoints(utf8) >= 2 && width > 1.f) out_ += " textLength=\"" + num(width) + "\" lengthAdjust=\"spacingAndGlyphs\"";
    if (!transform.empty()) out_ += " transform=\"" + transform + "\"";
    out_ += ">" + esc(utf8) + "</text>";
    ++elements_;
}

void SvgRenderer::drawText(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c) { text(origin, utf8, f, c, {}); }

void SvgRenderer::drawTextRotated(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c, float angleDeg, gfx::Point pivot) {
    if (std::fabs(angleDeg) < 0.01f) {
        text(origin, utf8, f, c, {});
        return;
    }
    text(origin, utf8, f, c, "rotate(" + num(angleDeg) + " " + num(pivot.x) + " " + num(pivot.y) + ")");
}

void SvgRenderer::drawTexture(const gfx::Rect& dst, gfx::TextureId id, gfx::Color tint) {
    if (id.v == 0) return;
    fillRect(dst, tint);           // comme l'ecran : une icone sans atlas est un carre de sa couleur
}

gfx::TextMetrics SvgRenderer::measure(std::string_view utf8, gfx::FontId f) const {
    if (auto* a = face(f)) {
        gfx::TextMetrics m;
        m.width = a->measure(utf8);
        m.height = a->lineHeight();
        m.ascent = a->ascent();
        m.descent = a->descent();
        return m;
    }
    const float px = static_cast<float>(std::max<std::uint16_t>(1, f.v));
    return {static_cast<float>(codePoints(utf8)) * px * kApproxAdvance, px * 1.2f, px * 0.8f, px * 0.2f};
}

float SvgRenderer::lineHeight(gfx::FontId f) const {
    if (auto* a = face(f)) return a->lineHeight();
    return static_cast<float>(std::max<std::uint16_t>(1, f.v)) * 1.2f;
}

std::size_t SvgRenderer::fitCharacters(std::string_view utf8, gfx::FontId f, float maxWidth) const {
    if (maxWidth <= 0.f) return 0;
    if (auto* a = face(f)) return a->fitBytes(utf8, maxWidth);
    const float advance = static_cast<float>(std::max<std::uint16_t>(1, f.v)) * kApproxAdvance;
    std::size_t glyphs = 0, bytes = 0;
    const auto budget = static_cast<std::size_t>(maxWidth / advance);
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) {
            if (glyphs == budget) return bytes;
            ++glyphs;
        }
        bytes = i + 1;
    }
    return utf8.size();
}

void SvgRenderer::fillTriangles(const gfx::Vertex* v, std::size_t n) {
    if (!v || n < 3) return;
    // Les triangles qui se suivent, de la meme couleur : un seul chemin (pas de joint
    // visible entre deux triangles voisins, et une page plus courte).
    std::size_t i = 0;
    while (i + 2 < n) {
        const gfx::Color c = v[i].color;
        std::string d;
        std::size_t j = i;
        for (; j + 2 < n; j += 3) {
            const gfx::Color k = v[j].color;
            if (k.r != c.r || k.g != c.g || k.b != c.b || k.a != c.a) break;
            d += "M" + num(v[j].pos.x) + " " + num(v[j].pos.y) + "L" + num(v[j + 1].pos.x) + " " + num(v[j + 1].pos.y) + "L"
               + num(v[j + 2].pos.x) + " " + num(v[j + 2].pos.y) + "Z";
        }
        if (c.a != 0 && !d.empty()) {
            out_ += "<path d=\"" + d + "\"" + paint("fill", c) + "/>";
            ++elements_;
        }
        i = j;
    }
}

gfx::TextureId SvgRenderer::createImage(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return {};
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.assign(rgba, rgba + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const std::uint32_t id = nextTexture_++;
    textures_[id] = std::move(img);
    return gfx::TextureId{id};
}

void SvgRenderer::releaseImage(gfx::TextureId id) { textures_.erase(id.v); }

void SvgRenderer::drawImage(gfx::TextureId id, const gfx::Rect& dst, float angleDeg, bool flipH, bool flipV, gfx::Color tint) {
    const auto it = textures_.find(id.v);
    if (it == textures_.end() || dst.w <= 0.f || dst.h <= 0.f || tint.a == 0) return;
    const auto& img = it->second;
    const std::string ref = images_.idFor(img.rgba, img.w, img.h, tint);
    if (ref.empty()) return;
    // L'image (sa taille propre) posee dans dst, tournee autour du centre de dst, retournee au besoin.
    const float sx = dst.w / static_cast<float>(img.w) * (flipH ? -1.f : 1.f);
    const float sy = dst.h / static_cast<float>(img.h) * (flipV ? -1.f : 1.f);
    std::string tr = "translate(" + num(dst.x + dst.w / 2) + " " + num(dst.y + dst.h / 2) + ")";
    if (std::fabs(angleDeg) >= 0.01f) tr += " rotate(" + num(angleDeg) + ")";
    tr += " scale(" + fine(sx) + " " + fine(sy) + ") translate(" + num(-static_cast<float>(img.w) / 2) + " " + num(-static_cast<float>(img.h) / 2) + ")";
    out_ += "<use href=\"#" + ref + "\" transform=\"" + tr + "\"";
    if (tint.a < 255) out_ += " opacity=\"" + num(static_cast<float>(tint.a) / 255.f) + "\"";
    out_ += "/>";
    ++elements_;
}

} // namespace app
