// =============================================================================
//  web/WebRenderer.cpp - 1.12.31 : voir WebRenderer.hpp
// =============================================================================
#ifdef XPG_WEB

#include "WebRenderer.hpp"

#include "../platform/FontAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace app::web {

namespace {
const std::uint8_t* g_ttf = nullptr;
std::size_t         g_ttfSize = 0;

std::uint64_t fnv(const std::uint8_t* d, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    for (std::size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}
std::size_t codePoints(std::string_view s) {
    std::size_t n = 0;
    for (char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}
constexpr float kApproxAdvance = 0.55f;
} // namespace

void setEmbeddedFont(const std::uint8_t* ttf, std::size_t size) {
    g_ttf = ttf;
    g_ttfSize = size;
}

WebRenderer::WebRenderer() = default;
WebRenderer::~WebRenderer() = default;

void WebRenderer::f32(float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    cmd_.push_back(u & 0xFF);
    cmd_.push_back((u >> 8) & 0xFF);
    cmd_.push_back((u >> 16) & 0xFF);
    cmd_.push_back((u >> 24) & 0xFF);
}
void WebRenderer::i32(std::int32_t v) {
    const std::uint32_t u = static_cast<std::uint32_t>(v);
    cmd_.push_back(u & 0xFF);
    cmd_.push_back((u >> 8) & 0xFF);
    cmd_.push_back((u >> 16) & 0xFF);
    cmd_.push_back((u >> 24) & 0xFF);
}
void WebRenderer::col(gfx::Color c) {
    cmd_.push_back(c.r);
    cmd_.push_back(c.g);
    cmd_.push_back(c.b);
    cmd_.push_back(c.a);
}

void WebRenderer::begin(float width, float height) {
    width_ = width;
    height_ = height;
    cmd_.clear();
}

gfx::FontAtlas* WebRenderer::face(gfx::FontId f) const {
    const int px = std::max(1, static_cast<int>(f.v));
    if (const auto it = faces_.find(px); it != faces_.end()) return it->second && it->second->ready() ? it->second.get() : nullptr;
    auto atlas = std::make_unique<gfx::FontAtlas>();
    if (g_ttf && g_ttfSize) atlas->loadFromMemory(g_ttf, g_ttfSize, static_cast<float>(px));
    auto& slot = faces_[px];
    slot = std::move(atlas);
    return slot->ready() ? slot.get() : nullptr;
}

void WebRenderer::pushClip(const gfx::Rect& r) {
    u8(static_cast<std::uint8_t>(Op::ClipPush));
    f32(r.x); f32(r.y); f32(std::max(0.f, r.w)); f32(std::max(0.f, r.h));
}
void WebRenderer::popClip() { u8(static_cast<std::uint8_t>(Op::ClipPop)); }

void WebRenderer::fillRect(const gfx::Rect& r, gfx::Color c) {
    if (r.empty() || c.a == 0) return;
    u8(static_cast<std::uint8_t>(Op::FillRect));
    f32(r.x); f32(r.y); f32(r.w); f32(r.h); col(c);
}
void WebRenderer::strokeRect(const gfx::Rect& r, gfx::Color c, float thickness) {
    if (r.empty() || c.a == 0) return;
    u8(static_cast<std::uint8_t>(Op::StrokeRect));
    f32(r.x); f32(r.y); f32(r.w); f32(r.h); col(c); f32(std::max(1.f, std::floor(thickness)));
}
void WebRenderer::fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) {
    if (r.empty() || c.a == 0) return;
    u8(static_cast<std::uint8_t>(Op::RoundRect));
    f32(r.x); f32(r.y); f32(r.w); f32(r.h); col(c); f32(std::min({radius, r.w * 0.5f, r.h * 0.5f}));
}
void WebRenderer::line(gfx::Point a, gfx::Point b, gfx::Color c, float thickness) {
    if (c.a == 0) return;
    u8(static_cast<std::uint8_t>(Op::Line));
    f32(a.x); f32(a.y); f32(b.x); f32(b.y); col(c); f32(std::max(1.f, std::floor(thickness)));
}

void WebRenderer::text(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c, float angleDeg, gfx::Point pivot) {
    if (utf8.empty() || c.a == 0) return;
    const float px = static_cast<float>(std::max<std::uint16_t>(1, f.v));
    const gfx::TextMetrics m = measure(utf8, f);
    u8(static_cast<std::uint8_t>(Op::Text));
    f32(origin.x); f32(origin.y); f32(px); col(c);
    f32(angleDeg); f32(pivot.x); f32(pivot.y);
    f32(m.width); f32(m.ascent > 0 ? m.ascent : px * 0.8f);
    i32(static_cast<std::int32_t>(utf8.size()));
    cmd_.insert(cmd_.end(), utf8.begin(), utf8.end());
}
void WebRenderer::drawText(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c) {
    text(origin, utf8, f, c, 0.f, {});
}
void WebRenderer::drawTextRotated(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c, float angleDeg, gfx::Point pivot) {
    text(origin, utf8, f, c, angleDeg, pivot);
}

void WebRenderer::drawTexture(const gfx::Rect& dst, gfx::TextureId id, gfx::Color tint) {
    if (id.v == 0) return;
    fillRect(dst, tint);   // comme l'ecran : une icone sans atlas est un carre de sa couleur
}

gfx::TextMetrics WebRenderer::measure(std::string_view utf8, gfx::FontId f) const {
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
float WebRenderer::lineHeight(gfx::FontId f) const {
    if (auto* a = face(f)) return a->lineHeight();
    return static_cast<float>(std::max<std::uint16_t>(1, f.v)) * 1.2f;
}
std::size_t WebRenderer::fitCharacters(std::string_view utf8, gfx::FontId f, float maxWidth) const {
    if (maxWidth <= 0.f) return 0;
    if (auto* a = face(f)) return a->fitBytes(utf8, maxWidth);
    const float advance = static_cast<float>(std::max<std::uint16_t>(1, f.v)) * kApproxAdvance;
    std::size_t glyphs = 0, bytes = 0;
    const auto budget = static_cast<std::size_t>(maxWidth / advance);
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) { if (glyphs == budget) return bytes; ++glyphs; }
        bytes = i + 1;
    }
    return utf8.size();
}

void WebRenderer::fillTriangles(const gfx::Vertex* v, std::size_t n) {
    if (!v || n < 3) return;
    const std::size_t tris = n / 3;
    u8(static_cast<std::uint8_t>(Op::Tris));
    i32(static_cast<std::int32_t>(tris));
    for (std::size_t i = 0; i + 2 < n + 2 && i < tris * 3; ++i) { f32(v[i].pos.x); f32(v[i].pos.y); col(v[i].color); }
}

gfx::TextureId WebRenderer::createImage(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return {};
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.assign(rgba, rgba + static_cast<std::size_t>(w) * h * 4);
    const std::uint32_t id = nextTexture_++;
    textures_[id] = std::move(img);
    return gfx::TextureId{id};
}
void WebRenderer::releaseImage(gfx::TextureId id) { textures_.erase(id.v); }

void WebRenderer::drawImage(gfx::TextureId id, const gfx::Rect& dst, float angleDeg, bool flipH, bool flipV, gfx::Color tint) {
    const auto it = textures_.find(id.v);
    if (it == textures_.end() || dst.w <= 0.f || dst.h <= 0.f || tint.a == 0) return;
    const auto& img = it->second;
    // L'image, identifiee par son contenu, n'est donnee au JS qu'une fois.
    std::uint64_t key = fnv(img.rgba.data(), img.rgba.size());
    key = fnv(reinterpret_cast<const std::uint8_t*>(&img.w), sizeof img.w, key);
    key = fnv(reinterpret_cast<const std::uint8_t*>(&img.h), sizeof img.h, key);
    std::uint32_t imageId;
    if (const auto f = imageIds_.find(key); f != imageIds_.end()) {
        imageId = f->second;
    } else {
        imageId = nextImageId_++;
        imageIds_[key] = imageId;
        // (id, w, h) puis les octets RGBA, dans le tampon des images neuves.
        auto push = [this](std::uint32_t u) { assets_.push_back(u & 0xFF); assets_.push_back((u >> 8) & 0xFF); assets_.push_back((u >> 16) & 0xFF); assets_.push_back((u >> 24) & 0xFF); };
        push(imageId);
        push(static_cast<std::uint32_t>(img.w));
        push(static_cast<std::uint32_t>(img.h));
        assets_.insert(assets_.end(), img.rgba.begin(), img.rgba.end());
    }
    u8(static_cast<std::uint8_t>(Op::Image));
    i32(static_cast<std::int32_t>(imageId));
    f32(dst.x); f32(dst.y); f32(dst.w); f32(dst.h);
    f32(angleDeg);
    u8(flipH ? 1 : 0);
    u8(flipV ? 1 : 0);
    col(tint);
}

} // namespace app::web

#endif // XPG_WEB
