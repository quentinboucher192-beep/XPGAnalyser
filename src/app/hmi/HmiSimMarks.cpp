// app/hmi/HmiSimMarks.cpp - 1.9 : le violet du simule et ses reperes (le cadre
// et la pastille des lectures simulees, le bandeau, l'infobulle, la fiole).
#include "HmiSimMarks.hpp"

#include "../../platform/Renderer.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace app::simmark {

namespace {

constexpr gfx::Color kRibbonA = gfx::Color::rgb(0x3B3F8A);
constexpr gfx::Color kRibbonB = gfx::Color::rgb(0x33377A);
constexpr gfx::Color kRibbonText = gfx::Color::rgb(0xE7E8FF);
constexpr gfx::Color kRibbonMore = gfx::Color::rgb(0xC9CBF5);
constexpr gfx::Color kTagText = gfx::Color::rgb(0x2B2F6B);
constexpr gfx::Color kPastilleBg = gfx::Color::rgb(0x171A3D);
constexpr gfx::Color kTipBg = gfx::Color::rgb(0x0B1218);
constexpr gfx::Color kTipText = gfx::Color::rgb(0xDFE2FF);

gfx::Color faded(gfx::Color c, float alpha) {
    return c.withAlpha(static_cast<std::uint8_t>(std::clamp(alpha, 0.f, 1.f) * static_cast<float>(c.a)));
}

// Le gras simule : le texte deux fois, a un pixel (le renderer n'a pas de face grasse).
void bold(gfx::IRenderer& r, gfx::Point at, const std::string& text, gfx::FontId font, gfx::Color c) {
    r.drawText(at, text, font, c);
    r.drawText({at.x + 0.6f, at.y}, text, font, c);
}

// Des mots, a la ligne a `width`.
std::vector<std::string> wrap(gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(tried, font).width > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            lines.push_back(line);
            line.clear();
        } else {
            word.push_back(c);
        }
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

} // namespace

gfx::Color text(bool dark) { return dark ? kText : gfx::Color::rgb(0x4F3FC0); }

gfx::Color text(const ui::Theme& theme) { return theme.onSurface(text(theme.isDark())); }

gfx::Color rowTint(bool dark) { return dark ? gfx::Color{108, 113, 196, 26} : gfx::Color{108, 113, 196, 30}; }

gfx::Color headTint(bool dark) { return dark ? kHead : gfx::Color::rgb(0xE4E3FA); }

std::string fitText(gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width) {
    if (width <= 0.f) return {};
    if (r.measure(text, font).width <= width) return text;
    std::size_t keep = r.fitCharacters(text, font, std::max(0.f, width - r.measure("...", font).width));
    while (keep > 0 && keep < text.size() && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    return text.substr(0, keep) + "...";
}

void drawFlask(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color c) {
    // La fiole de la maquette (une grille de 16) : le col, les flancs, le fond, le liquide.
    const float s = std::min(box.w, box.h) / 16.f;
    const float ox = box.x + (box.w - 16.f * s) * 0.5f, oy = box.y + (box.h - 16.f * s) * 0.5f;
    const auto P = [&](float x, float y) { return gfx::Point{ox + x * s, oy + y * s}; };
    const float t = std::max(1.f, 1.6f * s);
    r.line(P(6.f, 2.f), P(10.f, 2.f), c, t);
    r.line(P(6.5f, 2.f), P(6.5f, 6.f), c, t);
    r.line(P(9.5f, 2.f), P(9.5f, 6.f), c, t);
    r.line(P(6.5f, 6.f), P(3.f, 12.6f), c, t);
    r.line(P(9.5f, 6.f), P(13.f, 12.6f), c, t);
    r.line(P(3.f, 12.6f), P(3.9f, 14.f), c, t);
    r.line(P(13.f, 12.6f), P(12.1f, 14.f), c, t);
    r.line(P(3.9f, 14.f), P(12.1f, 14.f), c, t);
    r.line(P(4.6f, 10.f), P(11.4f, 10.f), c, t);
}

void drawLock(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color c) {
    // Le corps (plein) et l'anse (un demi-cercle au trait).
    const float s = std::min(box.w, box.h) / 16.f;
    const float ox = box.x + (box.w - 16.f * s) * 0.5f, oy = box.y + (box.h - 16.f * s) * 0.5f;
    const float t = std::max(1.f, 1.7f * s);
    r.fillRoundedRect({ox + 3.5f * s, oy + 7.f * s, 9.f * s, 6.5f * s}, c, 1.f * s);
    const float cx = ox + 8.f * s, cy = oy + 5.f * s, rad = 2.5f * s;
    r.line({cx - rad, oy + 7.f * s}, {cx - rad, cy}, c, t);
    r.line({cx + rad, oy + 7.f * s}, {cx + rad, cy}, c, t);
    gfx::Point prev{cx - rad, cy};
    for (int k = 1; k <= 8; ++k) {
        const float a = 3.14159265f * (1.f - static_cast<float>(k) / 8.f);
        const gfx::Point p{cx + rad * std::cos(a), cy - rad * std::sin(a)};
        r.line(prev, p, c, t);
        prev = p;
    }
}

void drawLockBadge(gfx::IRenderer& r, const gfx::Rect& box) {
    r.fillRoundedRect(box, kLine, 4.f);
    r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, kBg, 3.f);
    const float m = box.w * 0.18f;
    drawLock(r, {box.x + m, box.y + m, box.w - 2.f * m, box.h - 2.f * m}, kText);
}

void drawDot(gfx::IRenderer& r, gfx::Point c, float radius) {
    const float halo = radius + 3.f;
    r.fillRoundedRect({c.x - halo, c.y - halo, 2.f * halo, 2.f * halo}, gfx::Color{108, 113, 196, 64}, halo);
    r.fillRoundedRect({c.x - radius, c.y - radius, 2.f * radius, 2.f * radius}, kLine, radius);
}

void dashedRect(gfx::IRenderer& r, const gfx::Rect& rc, gfx::Color c, float t, float dash, float gap) {
    const auto run = [&](gfx::Point a, gfx::Point b) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.f) return;
        for (float d = 0.f; d < len; d += dash + gap) {
            const float e = std::min(len, d + dash);
            r.line({a.x + dx * d / len, a.y + dy * d / len}, {a.x + dx * e / len, a.y + dy * e / len}, c, t);
        }
    };
    run({rc.x, rc.y}, {rc.right(), rc.y});
    run({rc.right(), rc.y}, {rc.right(), rc.bottom()});
    run({rc.right(), rc.bottom()}, {rc.x, rc.bottom()});
    run({rc.x, rc.bottom()}, {rc.x, rc.y});
}

gfx::Rect markFrame(const gfx::Rect& o) { return {o.x - 6.f, o.y - 6.f, o.w + 12.f, o.h + 12.f}; }

gfx::Rect markPastille(const gfx::Rect& o, bool besideQuality) {
    const gfx::Rect f = markFrame(o);
    const float s = std::clamp(std::min(f.w, f.h) * 0.42f, 16.f, 22.f);
    // A cheval sur le coin haut droit ; a cote de la pastille de qualite (qui y est deja).
    const float x = besideQuality ? f.right() - s * 1.9f - 4.f : f.right() - s * 0.5f;
    return {x, f.y - s * 0.5f, s, s};
}

void drawMark(gfx::IRenderer& r, const gfx::Rect& o, bool besideQuality, float alpha) {
    dashedRect(r, markFrame(o), faded(kMark, alpha), 1.6f);
    const gfx::Rect p = markPastille(o, besideQuality);
    r.fillRoundedRect(p, faded(kMark, alpha), p.w * 0.5f);
    r.fillRoundedRect({p.x + 2.f, p.y + 2.f, p.w - 4.f, p.h - 4.f}, faded(kPastilleBg, alpha), p.w * 0.5f - 2.f);
    const float m = p.w * 0.22f;
    drawFlask(r, {p.x + m, p.y + m, p.w - 2.f * m, p.h - 2.f * m}, faded(kText, alpha));
}

void drawRibbon(gfx::IRenderer& r, const gfx::Rect& a, const std::string& text, const std::string& right) {
    r.pushClip(a);
    r.fillRect(a, kRibbonA);
    // Les rayures (-45 degres), puis le filet du bas.
    for (float x = a.x - a.h; x < a.right() + a.h; x += 20.f) r.line({x, a.bottom()}, {x + a.h, a.y}, kRibbonB, 7.f);
    r.fillRect({a.x, a.bottom() - 2.f, a.w, 2.f}, kLine);
    const gfx::FontId font{13}, tagFont{11};
    const float lh = r.lineHeight(font);
    const float y = a.y + (a.h - 2.f - lh) * 0.5f;
    float x = a.x + 12.f;
    const float fs = std::min(16.f, a.h - 8.f);
    drawFlask(r, {x, a.y + (a.h - 2.f - fs) * 0.5f, fs, fs}, kRibbonText);
    x += fs + 9.f;
    // L'etiquette : blanche, le texte violet fonce (lettres espacees).
    const std::string tag = "LECTURES SIMUL\xC3\x89" "ES";
    const float tagW = r.measure(tag, tagFont).width + 14.f, tagH = std::min(a.h - 8.f, r.lineHeight(tagFont) + 4.f);
    r.fillRoundedRect({x, a.y + (a.h - 2.f - tagH) * 0.5f, tagW, tagH}, kRibbonText, 3.f);
    bold(r, {x + 7.f, a.y + (a.h - 2.f - r.lineHeight(tagFont)) * 0.5f}, tag, tagFont, kTagText);
    x += tagW + 10.f;
    float rightW = 0.f;
    if (!right.empty()) {
        rightW = r.measure(right, font).width;
        r.drawText({a.right() - 12.f - rightW, y}, right, font, kRibbonMore);
    }
    bold(r, {x, y}, fitText(r, text, font, std::max(0.f, a.right() - x - rightW - 28.f)), font, kRibbonText);
    r.popClip();
}

void drawTip(gfx::IRenderer& r, gfx::Point mouse, const gfx::Rect& inside, const std::string& title, const std::string& body) {
    const gfx::FontId font{12};
    const float lh = r.lineHeight(font) + 2.f;
    const float w = std::min(300.f, std::max(160.f, inside.w - 16.f));
    const auto lines = wrap(r, body, font, w - 18.f);
    const float h = 12.f + lh * static_cast<float>(lines.size() + 1);
    float x = mouse.x + 16.f, y = mouse.y + 18.f;
    if (x + w > inside.right() - 4.f) x = mouse.x - 16.f - w;
    if (y + h > inside.bottom() - 4.f) y = mouse.y - 12.f - h;
    x = std::max(inside.x + 4.f, x);
    y = std::max(inside.y + 4.f, y);
    r.fillRoundedRect({x + 3.f, y + 5.f, w, h}, gfx::Color{0, 0, 0, 110}, 6.f);
    r.fillRoundedRect({x, y, w, h}, kLine, 5.f);
    r.fillRoundedRect({x + 1.f, y + 1.f, w - 2.f, h - 2.f}, kTipBg, 4.f);
    bold(r, {x + 9.f, y + 6.f}, title, font, kText);
    float ly = y + 6.f + lh;
    for (const auto& l : lines) {
        r.drawText({x + 9.f, ly}, l, font, kTipText);
        ly += lh;
    }
}

// ------------------------------------------------------------- l'indicateur ---
Indicator::Indicator(std::string id) : ui::Widget(std::move(id)) {}

void Indicator::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    invalidate();
    if (auto* p = parent()) p->invalidateLayout();
}

ui::SizeHint Indicator::sizeHint() const {
    ui::SizeHint h;
    if (text_.empty()) return h;
    width_ = ui::measureWidth(text_, gfx::FontId{13}) + 26.f;
    h.preferred = {width_, 20.f};
    h.minimum = h.preferred;
    return h;
}

void Indicator::onPaint(const ui::PaintContext& ctx) {
    if (text_.empty()) return;
    const auto b = bounds();
    const gfx::Color c = simmark::text(ctx.theme);   // (la fonction libre : text() est aussi un membre)
    const float s = std::min(14.f, b.h - 6.f);
    drawFlask(ctx.r, {b.x + 2.f, b.y + (b.h - s) * 0.5f, s, s}, c);
    const gfx::FontId font = ctx.theme.font.smallUi;
    ctx.r.drawText({b.x + s + 8.f, b.y + (b.h - ctx.r.lineHeight(font)) * 0.5f}, fitText(ctx.r, text_, font, b.w - s - 10.f), font, c);
}

} // namespace app::simmark
