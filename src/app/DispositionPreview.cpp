// =============================================================================
//  app/DispositionPreview.cpp - 1.12.4 : voir DispositionPreview.hpp
// =============================================================================
#include "DispositionPreview.hpp"

#include "../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace dp = disposition;

namespace {

const gfx::FontId kLabel{11};
const gfx::FontId kTab{11};
const char* const kEllipsis = "\xE2\x80\xA6";

// Un texte dans une largeur : coupe d'un « … » s'il depasse.
void drawFit(const ui::PaintContext& ctx, gfx::Point at, const std::string& text, gfx::FontId font, gfx::Color color, float maxW) {
    if (maxW <= 4.f || text.empty()) return;
    if (ctx.r.measure(text, font).width <= maxW) {
        ctx.r.drawText(at, text, font, color);
        return;
    }
    const float dots = ctx.r.measure(kEllipsis, font).width;
    const std::size_t n = ctx.r.fitCharacters(text, font, std::max(0.f, maxW - dots));
    if (n == 0) return;
    ctx.r.drawText(at, text.substr(0, n) + kEllipsis, font, color);
}

} // namespace

DispositionPreview::DispositionPreview(const dp::Layout& layout, std::string id) : ui::Widget(std::move(id)), layout_(layout) {}

gfx::Rect DispositionPreview::frame() const {
    const auto b = bounds();
    float w = b.w, h = w * 0.625f;
    if (h > b.h) {
        h = b.h;
        w = h / 0.625f;
    }
    return {std::floor(b.x + (b.w - w) * 0.5f), std::floor(b.y + (b.h - h) * 0.5f), std::floor(w), std::floor(h)};
}

gfx::Rect DispositionPreview::rectOf(const dp::SketchBox& s) const {
    const auto f = frame();
    return {f.x + s.x * f.w, f.y + s.y * f.h, s.w * f.w, s.h * f.h};
}

void DispositionPreview::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const gfx::Rect f = frame();
    if (f.w < 40.f || f.h < 30.f) return;
    ctx.r.fillRoundedRect(f, c.windowBg, 4.f);
    const auto boxes = this->boxes();
    // Un element montre seulement dans ce mode : vert en simulation, bleu en edition.
    const gfx::Color modeTint = simulating_ ? c.ok : c.info;
    const float lh = ctx.r.lineHeight(kLabel);
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& b : boxes) {
            if (b.floating != (pass == 1)) continue;
            gfx::Rect r = rectOf(b);
            r = {r.x + 1.f, r.y + 1.f, std::max(0.f, r.w - 2.f), std::max(0.f, r.h - 2.f)};
            if (r.w < 3.f || r.h < 3.f) continue;
            const bool canvas = b.id == "page.vue" || b.id == "page.simulation";
            gfx::Color fill = c.panelBg;
            if (b.id == "barre" || b.id == "etat") fill = c.headerBg;
            else if (canvas) fill = c.inputBg;
            if (b.floating) {
                ctx.r.fillRoundedRect({r.x + 4.f, r.y + 5.f, r.w, r.h}, gfx::Color{0, 0, 0, 90}, 4.f);
                ctx.r.fillRoundedRect(r, fill, 4.f);
                ctx.r.strokeRect(r, c.accent, 1.f);
            } else {
                ctx.r.fillRect(r, fill);
                ctx.r.strokeRect(r, c.border, 1.f);
            }
            if (b.modeOnly) ctx.r.strokeRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, modeTint, 2.f);
            ctx.r.pushClip(r);
            float y = r.y + 3.f;
            // Le nom de la boite (une page au milieu, sauf ses onglets) ; un panneau replie : sur sa barre.
            if (!b.label.empty()) {
                if (canvas || (b.tabs.empty() && (b.id == "centre" || b.id == "cote" || b.id.rfind("page.", 0) == 0))) {
                    const float w = std::min(ctx.r.measure(b.label, kLabel).width, r.w - 8.f);
                    drawFit(ctx, {r.x + (r.w - w) * 0.5f, r.y + (r.h - lh) * 0.5f}, b.label, kLabel, c.textMuted, r.w - 8.f);
                } else {
                    drawFit(ctx, {r.x + 5.f, y}, b.label, kLabel, b.collapsed ? c.text : c.textMuted, r.w - 10.f);
                    y += lh + 2.f;
                }
            }
            // Ses onglets : celui de devant en couleur ; seulement dans ce mode, liseré.
            float x = r.x + 4.f;
            const float th = lh + 2.f;
            for (const auto& t : b.tabs) {
                if (y + th > r.bottom() - 1.f) break;
                const float tw = ctx.r.measure(t.title, kTab).width + 10.f;
                if (x + tw > r.right() - 4.f) {
                    if (x + 14.f <= r.right() - 2.f) ctx.r.drawText({x + 2.f, y}, kEllipsis, kTab, c.textMuted);
                    break;
                }
                const gfx::Rect pill{x, y, tw, th};
                ctx.r.fillRoundedRect(pill, t.active ? c.accent : c.inputBg, 3.f);
                if (t.modeOnly) ctx.r.strokeRect(pill, modeTint, 1.5f);
                ctx.r.drawText({x + 5.f, y + 1.f}, t.title, kTab, t.active ? c.selectionText : c.text);
                x += tw + 4.f;
            }
            ctx.r.popClip();
        }
    ctx.r.strokeRect(f, c.borderStrong, 1.f);
}

} // namespace app
