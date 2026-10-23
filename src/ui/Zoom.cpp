// =============================================================================
//  ui/Zoom.cpp - 1.12.30 : le zoom de chaque page (voir Zoom.hpp)
// =============================================================================
#include "Zoom.hpp"
#include "Widget.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

// ============================================================ le renderer ===
gfx::FontId ZoomRenderer::font(gfx::FontId f) const noexcept { return scaling() ? zoomedFont(f, m_.s) : f; }

gfx::Rect ZoomRenderer::snapped(const gfx::Rect& r) const noexcept {
    if (!scaling()) return r;
    const auto o = m_.toOuter(r);
    // Les bords sur des pixels entiers : deux cases voisines se touchent sans jour.
    // Un trait fin (moins de 2,5 px une fois agrandi) garde sa largeur, arrondie :
    // une bordure d'un pixel reste d'un pixel partout.
    gfx::Rect out;
    if (o.w < 2.5f && o.w > 0.f) {
        out.x = std::round(o.x);
        out.w = std::max(1.f, std::round(o.w));
    } else {
        out.x = std::round(o.x);
        out.w = std::round(o.x + o.w) - out.x;
    }
    if (o.h < 2.5f && o.h > 0.f) {
        out.y = std::round(o.y);
        out.h = std::max(1.f, std::round(o.h));
    } else {
        out.y = std::round(o.y);
        out.h = std::round(o.y + o.h) - out.y;
    }
    return out;
}

void ZoomRenderer::pushClip(const gfx::Rect& r) {
    if (!scaling()) { r_.pushClip(r); return; }
    // La decoupe englobe les pixels entames (rien de ce qui est dedans n'est coupe).
    const auto o = m_.toOuter(r);
    const float x0 = std::floor(o.x), y0 = std::floor(o.y);
    r_.pushClip({x0, y0, std::max(0.f, std::ceil(o.x + o.w) - x0), std::max(0.f, std::ceil(o.y + o.h) - y0)});
}

void ZoomRenderer::fillRect(const gfx::Rect& r, gfx::Color c) { r_.fillRect(snapped(r), c); }

void ZoomRenderer::strokeRect(const gfx::Rect& r, gfx::Color c, float thickness) {
    r_.strokeRect(snapped(r), c, scaling() ? std::max(1.f, thickness * m_.s) : thickness);
}

void ZoomRenderer::fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) {
    r_.fillRoundedRect(snapped(r), c, scaling() ? radius * m_.s : radius);
}

void ZoomRenderer::line(gfx::Point a, gfx::Point b, gfx::Color c, float thickness) {
    if (!scaling()) { r_.line(a, b, c, thickness); return; }
    const auto pa = m_.toOuter(a), pb = m_.toOuter(b);
    // Un trait horizontal ou vertical sur une rangee de pixels (net) ; un trait en biais tel quel.
    gfx::Point qa = pa, qb = pb;
    if (std::fabs(pa.y - pb.y) < 0.01f) qa.y = qb.y = std::round(pa.y);
    if (std::fabs(pa.x - pb.x) < 0.01f) qa.x = qb.x = std::round(pa.x);
    r_.line(qa, qb, c, std::max(1.f, thickness * m_.s));
}

void ZoomRenderer::drawText(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c) {
    if (!scaling()) { r_.drawText(origin, utf8, f, c); return; }
    const auto o = m_.toOuter(origin);
    r_.drawText({std::round(o.x), std::round(o.y)}, utf8, font(f), c);
}

void ZoomRenderer::drawTexture(const gfx::Rect& dst, gfx::TextureId id, gfx::Color tint) { r_.drawTexture(snapped(dst), id, tint); }

gfx::TextMetrics ZoomRenderer::measure(std::string_view utf8, gfx::FontId f) const {
    if (!scaling()) return r_.measure(utf8, f);
    auto m = r_.measure(utf8, font(f));
    m.width /= m_.s;
    m.height /= m_.s;
    m.ascent /= m_.s;
    m.descent /= m_.s;
    return m;
}

float ZoomRenderer::lineHeight(gfx::FontId f) const {
    if (!scaling()) return r_.lineHeight(f);
    return r_.lineHeight(font(f)) / m_.s;
}

gfx::Size ZoomRenderer::surfaceSize() const {
    const auto s = r_.surfaceSize();
    if (!scaling()) return s;
    const auto p = m_.toInner(gfx::Point{s.w, s.h});
    return {std::max(0.f, p.x), std::max(0.f, p.y)};
}

std::size_t ZoomRenderer::fitCharacters(std::string_view utf8, gfx::FontId f, float maxWidth) const {
    if (!scaling()) return r_.fitCharacters(utf8, f, maxWidth);
    return r_.fitCharacters(utf8, font(f), maxWidth * m_.s);
}

void ZoomRenderer::fillTriangles(const gfx::Vertex* vertices, std::size_t count) {
    if (!scaling() || !vertices) { r_.fillTriangles(vertices, count); return; }
    std::vector<gfx::Vertex> out(vertices, vertices + count);
    for (auto& v : out) v.pos = m_.toOuter(v.pos);
    r_.fillTriangles(out.data(), out.size());
}

void ZoomRenderer::drawTextRotated(gfx::Point origin, std::string_view utf8, gfx::FontId f, gfx::Color c, float angleDeg,
                                   gfx::Point pivot) {
    if (!scaling()) { r_.drawTextRotated(origin, utf8, f, c, angleDeg, pivot); return; }
    r_.drawTextRotated(m_.toOuter(origin), utf8, font(f), c, angleDeg, m_.toOuter(pivot));
}

void ZoomRenderer::drawImage(gfx::TextureId id, const gfx::Rect& dst, float angleDeg, bool flipH, bool flipV, gfx::Color tint) {
    if (!scaling()) { r_.drawImage(id, dst, angleDeg, flipH, flipV, tint); return; }
    r_.drawImage(id, angleDeg == 0.f ? snapped(dst) : m_.toOuter(dst), angleDeg, flipH, flipV, tint);
}

bool ZoomRenderer::beginOffscreen(int w, int h, gfx::Color clear) {
    if (!r_.beginOffscreen(w, h, clear)) return false;
    ++offscreen_;
    return true;
}

bool ZoomRenderer::endOffscreen(std::vector<std::uint8_t>& rgba, int& w, int& h) {
    const bool ok = r_.endOffscreen(rgba, w, h);
    if (offscreen_ > 0) --offscreen_;
    return ok;
}

// ================================================================ les crans ===
const std::vector<float>& zoomSteps() {
    static const std::vector<float> steps{0.1f, 0.25f, 0.33f, 0.5f, 0.67f, 0.75f, 0.8f, 0.9f, 1.f, 1.1f, 1.25f,
                                          1.5f, 1.75f, 2.f, 2.5f, 3.f, 4.f, 6.f, 8.f};
    return steps;
}

float zoomStep(float current, int step, float lo, float hi) noexcept {
    if (step == 0) return std::clamp(current, lo, hi);
    const auto& steps = zoomSteps();
    float out = current;
    if (step > 0) {
        for (const float s : steps)
            if (s > current + 0.004f) { out = s; break; }
        if (out == current) out = current * 1.25f;
    } else {
        for (auto it = steps.rbegin(); it != steps.rend(); ++it)
            if (*it < current - 0.004f) { out = *it; break; }
        if (out == current) out = current / 1.25f;
    }
    return std::clamp(out, lo, hi);
}

int zoomPercent(float zoom) noexcept { return static_cast<int>(std::lround(zoom * 100.f)); }

// ========================================================= ce que vise un geste ===
namespace {

Widget* hit(Widget& w, gfx::Point p, Widget* scaled) {
    // p : dans le repere du parent de w.
    if (!w.visible() || !w.zoomFrame().contains(p)) return scaled;
    if (w.zoomSupport() == ZoomSupport::Native) return &w;
    Widget* outer = scaled ? scaled : (w.zoomSupport() == ZoomSupport::Scaled ? &w : nullptr);
    // Le point dans le repere de w : ses enfants y sont ranges.
    const float z = w.zoomSupport() == ZoomSupport::Scaled ? w.zoom() : 1.f;
    const auto f = w.zoomFrame();
    const gfx::Point q = z == 1.f ? p : ZoomMap::about({f.x, f.y}, z).toInner(p);
    const auto& kids = w.children();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it)
        if ((*it)->visible() && (*it)->zoomFrame().contains(q)) return hit(**it, q, outer);
    return outer;
}

void collectScaled(Widget& w, std::vector<Widget*>& out) {
    if (w.zoomSupport() == ZoomSupport::Scaled) { out.push_back(&w); return; }
    for (const auto& c : w.children()) collectScaled(*c, out);
}

void collectNatives(Widget& w, std::vector<Widget*>& out) {
    if (!w.visible()) return;
    if (w.zoomSupport() == ZoomSupport::Native) { out.push_back(&w); return; }
    if (w.zoomSupport() == ZoomSupport::Scaled) return;    // couvert par son zoom
    for (const auto& c : w.children()) collectNatives(*c, out);
}

} // namespace

Widget* zoomableAt(Widget& root, gfx::Point surface) {
    // La racine est dans le repere de la surface : son parent n'existe pas.
    const gfx::Point p = root.parent() ? root.parent()->fromSurface(surface) : surface;
    return hit(root, p, nullptr);
}

std::vector<Widget*> scaledUnder(Widget& root) {
    std::vector<Widget*> out;
    collectScaled(root, out);
    return out;
}

std::vector<Widget*> nativesUnder(Widget& root) {
    std::vector<Widget*> out;
    collectNatives(root, out);
    std::stable_sort(out.begin(), out.end(), [](const Widget* a, const Widget* b) {
        return a->zoomFrame().w * a->zoomFrame().h > b->zoomFrame().w * b->zoomFrame().h;
    });
    return out;
}

} // namespace ui
