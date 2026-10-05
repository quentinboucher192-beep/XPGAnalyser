// app/hmi/HmiCyclicWidgets.cpp - les morceaux de la lecture cyclique (1.9) : le
// graphique en pistes, les bandes de titre, le tableau aux cases a cocher, les
// menus et les dialogues (Lire des variables, Exporter), la boite sous la grille.
#include "HmiCyclicPage.hpp"

#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

namespace shapes = ui::shapes;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kTiny{12};
const gfx::FontId kUi{15};

// En capitales, les lettres accentuees comprises (UTF-8 : c3 a0..be -> c3 80..9e,
// sauf c3 b7, la division) : le titre "Apercu" de la boite d'export gardait son c
// cedille en minuscule.
std::string upperAscii(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto n = static_cast<unsigned char>(s[i + 1]);
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7) s[i + 1] = static_cast<char>(n - 0x20);
            ++i;
        } else if (c < 0x80) {
            s[i] = static_cast<char>(std::toupper(c));
        }
    }
    return s;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Un texte gras (pas de face grasse : trace deux fois, a un demi-pixel).
void boldText(gfx::IRenderer& r, gfx::Point at, std::string_view text, gfx::FontId f, gfx::Color c) {
    r.drawText(at, text, f, c);
    r.drawText({at.x + 0.6f, at.y}, text, f, c);
}

// Ce qui tient dans `width` (des points de suite sinon).
std::string fitted(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float width) {
    if (width <= 0) return {};
    if (r.measure(s, f).width <= width) return s;
    const std::string dots = "\xE2\x80\xA6";
    const float dw = r.measure(dots, f).width;
    const std::size_t n = r.fitCharacters(s, f, std::max(0.f, width - dw));
    std::size_t cut = std::min(n, s.size());
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + dots;
}

// Un texte coupe en lignes de `width` pixels au plus, mot a mot.
std::vector<std::string> wrap(std::string_view text, gfx::FontId f, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flushWord = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else if (c == ' ') {
            flushWord();
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void circle(gfx::IRenderer& r, gfx::Point c, float rad, gfx::Color col, bool fill = true) {
    const auto v = shapes::ellipse(c, rad, rad, 20);
    if (fill) shapes::fillPolygon(r, v, col);
    else shapes::strokePolyline(r, v, true, col, 1.2f);
}

} // namespace

gfx::Color simViolet() { return gfx::Color::rgb(0x8B7CF6); }
gfx::Color simVioletBg() { return gfx::Color::rgb(0x2A2346); }

gfx::Color cyclicSeriesColor(int index) {
    static const std::uint32_t k[] = {0x3B82F6, 0x22C55E, 0xF59E0B, 0xEC4899, 0x8B5CF6, 0x14B8A6, 0xEF4444, 0x64748B,
                                      0x60A5FA, 0x84CC16, 0xF97316, 0xC084FC, 0x06B6D4, 0xF43F5E, 0xEAB308, 0x94A3B8};
    const int n = static_cast<int>(std::size(k));
    return gfx::Color::rgb(k[((index % n) + n) % n]);
}

void drawCyclicGlyph(gfx::IRenderer& r, int glyph, const gfx::Rect& b, gfx::Color c) {
    const float s = std::min(b.w, b.h) / 16.f;
    const float ox = b.x + (b.w - 16.f * s) / 2, oy = b.y + (b.h - 16.f * s) / 2;
    const auto P = [&](float x, float y) { return gfx::Point{ox + x * s, oy + y * s}; };
    const auto L = [&](float x0, float y0, float x1, float y1, float t = 1.4f) { r.line(P(x0, y0), P(x1, y1), c, std::max(1.f, t * s)); };
    const auto R = [&](float x, float y, float w, float h) { r.strokeRect({ox + x * s, oy + y * s, w * s, h * s}, c, std::max(1.f, 1.2f * s)); };
    if (glyph >= GChipBase) {
        const gfx::Color chip = cyclicSeriesColor(glyph - GChipBase);
        r.fillRoundedRect({ox + 3 * s, oy + 3 * s, 10 * s, 10 * s}, chip, 2.5f * s);
        return;
    }
    switch (glyph) {
        case GPlus: L(8, 3, 8, 13, 1.7f); L(3, 8, 13, 8, 1.7f); break;
        case GReadWrite:          // une fleche qui monte, une qui descend
            L(5, 2.5f, 5, 13.5f); L(2.5f, 5, 5, 2.5f); L(5, 2.5f, 7.5f, 5);
            L(11, 13.5f, 11, 2.5f); L(8.5f, 11, 11, 13.5f); L(11, 13.5f, 13.5f, 11);
            break;
        case GVariables:          // des lignes, un point
            L(4, 3.5f, 12, 3.5f); L(4, 8, 12, 8); L(4, 12.5f, 9, 12.5f);
            circle(r, P(12.5f, 12.5f), 1.3f * s, c, false);
            break;
        case GMemory: R(3, 2.5f, 10, 11); L(5.5f, 5, 10.5f, 5); L(5.5f, 8, 10.5f, 8); L(5.5f, 11, 8.5f, 11); break;
        case GTable: R(2.5f, 3, 11, 10); L(2.5f, 6.5f, 13.5f, 6.5f); L(6.5f, 6.5f, 6.5f, 13); break;
        case GFolder: {
            const std::vector<gfx::Point> v{P(2, 4.5f), P(6.5f, 4.5f), P(8, 6), P(14, 6), P(14, 13), P(2, 13)};
            shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.2f * s));
            break;
        }
        case GSave: {
            const std::vector<gfx::Point> v{P(3, 2.5f), P(11, 2.5f), P(13.5f, 5), P(13.5f, 13.5f), P(3, 13.5f)};
            shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.2f * s));
            R(5.5f, 2.5f, 5, 3.5f);
            R(5, 9.5f, 6, 4);
            break;
        }
        case GExport: L(8, 10, 8, 2.5f); L(5, 5.5f, 8, 2.5f); L(8, 2.5f, 11, 5.5f); L(3, 9, 3, 13); L(3, 13, 13, 13); L(13, 13, 13, 9); break;
        case GFlask: {            // un flacon : le col, le corps, le liquide
            const std::vector<gfx::Point> v{P(6, 2), P(10, 2), P(10, 6), P(14, 13.5f), P(2, 13.5f), P(6, 6)};
            shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.3f * s));
            const std::vector<gfx::Point> liquid{P(4.3f, 10), P(11.7f, 10), P(13.2f, 12.9f), P(2.8f, 12.9f)};
            shapes::fillPolygon(r, liquid, c.withAlpha(150));
            L(5, 2, 11, 2, 1.6f);
            break;
        }
        case GBoxOn: {
            const gfx::Rect box{ox + 2 * s, oy + 2 * s, 12 * s, 12 * s};
            r.fillRoundedRect(box, c, 2.5f * s);
            const gfx::Color ink = gfx::Color::rgb(0xFDF6E3);
            r.line(P(4.6f, 8.2f), P(7, 10.6f), ink, std::max(1.f, 1.7f * s));
            r.line(P(7, 10.6f), P(11.6f, 5.4f), ink, std::max(1.f, 1.7f * s));
            break;
        }
        case GBoxOff: r.strokeRect({ox + 2 * s, oy + 2 * s, 12 * s, 12 * s}, c, std::max(1.f, 1.4f * s)); break;
        case GBoxDisabled: r.strokeRect({ox + 2 * s, oy + 2 * s, 12 * s, 12 * s}, c.withAlpha(70), std::max(1.f, 1.2f * s)); break;
        case GDotOk: case GDotWarn: case GDotErr: case GDotOff: case GDotSim:
            circle(r, P(8, 8), 4.f * s, c);
            break;
        case GForced: {           // une pastille orange "F"
            const gfx::Rect pill{ox + 1 * s, oy + 2 * s, 14 * s, 12 * s};
            r.fillRoundedRect(pill, c, 3 * s);
            const gfx::FontId f{static_cast<std::uint16_t>(std::max(9.f, 11 * s))};
            const float w = r.measure("F", f).width;
            r.drawText({pill.x + (pill.w - w) / 2, pill.y + (pill.h - r.lineHeight(f)) / 2}, "F", f, gfx::Color::rgb(0x1B1B1B));
            break;
        }
        default: break;
    }
}

// ================================================================ le graphique ===
HmiLaneChart::HmiLaneChart(std::string id) : ui::Widget(std::move(id)) {}

void HmiLaneChart::setData(std::vector<Series> series, double from, double to, bool lanes, std::string empty) {
    series_ = std::move(series);
    from_ = from;
    to_ = to;
    lanes_ = lanes;
    empty_ = std::move(empty);
    invalidate();
}

std::size_t HmiLaneChart::laneCount() const {
    if (series_.empty()) return 0;
    if (!lanes_) return 1;
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < series_.size(); ++i) {
        const std::string k = series_[i].lane.empty() ? "#" + std::to_string(i) : series_[i].lane;
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    }
    return keys.size();
}

void HmiLaneChart::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    r.fillRect(b, c.inputBg);
    const float left = b.x + 10, right = b.x + b.w - 12, top = b.y + 4, bottom = b.y + b.h - 20;
    const double span = std::max(1e-6, to_ - from_);
    const auto X = [&](double t) { return left + static_cast<float>((t - from_) / span) * (right - left); };
    // Le temps : sept reperes, "-60 s" ... "maintenant".
    for (int k = 0; k <= 6; ++k) {
        const double t = from_ + span * k / 6.0;
        const float x = std::round(X(t)) + 0.5f;
        r.line({x, top}, {x, bottom}, c.gridLine, 1.f);
        const int ago = static_cast<int>(std::lround(span * (6 - k) / 6.0));
        const std::string label = k == 6 ? std::string("maintenant") : "\xE2\x88\x92" + std::to_string(ago) + " s";
        const float w = r.measure(label, kTiny).width;
        r.drawText({std::clamp(x - w / 2, left, right - w), bottom + 3}, label, kTiny, c.textMuted);
    }
    if (series_.empty()) {
        const std::string msg = empty_.empty() ? std::string("Coche \xC2\xAB Tracer \xC2\xBB sur une valeur, en bas : sa courbe vient ici.") : empty_;
        const float w = r.measure(msg, kSmall).width;
        r.drawText({b.x + (b.w - w) / 2, top + (bottom - top) / 2 - 8}, msg, kSmall, c.textMuted);
        return;
    }
    // Les pistes : une par cle (la valeur, ou son unite) ; une seule echelle : une piste.
    std::vector<std::vector<std::size_t>> groups;
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < series_.size(); ++i) {
        const std::string k = !lanes_ ? std::string("*") : series_[i].lane.empty() ? "#" + std::to_string(i) : series_[i].lane;
        const auto it = std::find(keys.begin(), keys.end(), k);
        if (it == keys.end()) {
            keys.push_back(k);
            groups.push_back({i});
        } else {
            groups[static_cast<std::size_t>(it - keys.begin())].push_back(i);
        }
    }
    const float lh = (bottom - top) / static_cast<float>(groups.size());
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const float y0 = top + lh * static_cast<float>(gi), y1 = y0 + lh;
        double lo = 0, hi = 0;
        bool any = false;
        for (const auto i : groups[gi]) {
            for (const auto& [t, v] : series_[i].points) {
                if (t < from_ - 1) continue;
                if (!any) lo = hi = v;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                any = true;
            }
            if (series_[i].band && any) {
                lo = std::min(lo, series_[i].band->first);
                hi = std::max(hi, series_[i].band->second);
            }
        }
        if (!any) { lo = 0; hi = 1; }
        const double dlo = lo, dhi = hi;
        if (hi - lo < 1e-9) { lo -= 1; hi += 1; }
        const double pad = (hi - lo) * 0.18;
        lo -= pad;
        hi += pad;
        const float plotTop = y0 + 18, plotBottom = y1 - 4;
        const auto Y = [&](double v) { return plotBottom - static_cast<float>((v - lo) / (hi - lo)) * std::max(4.f, plotBottom - plotTop); };
        if (gi > 0) r.line({left, std::round(y0) + 0.5f}, {right, std::round(y0) + 0.5f}, c.border, 1.f);
        // La ligne du milieu, en pointilles.
        const float ym = std::round(Y((lo + hi) / 2)) + 0.5f;
        for (float x = left; x < right; x += 6) r.line({x, ym}, {std::min(right, x + 2), ym}, c.gridLine, 1.f);
        r.pushClip({left, y0 + 1, right - left, lh - 2});
        for (const auto i : groups[gi]) {
            const auto& s = series_[i];
            const gfx::Color col = s.forced ? gfx::Color{236, 132, 38, 255} : s.color;
            if (s.band) {
                const float ya = Y(s.band->second), yb = Y(s.band->first);
                gfx::Color bc = s.simulated ? simViolet() : col;
                bc.a = 34;
                r.fillRect({left, std::min(ya, yb), right - left, std::max(1.f, std::fabs(yb - ya))}, bc);
            }
            std::vector<gfx::Point> pts;
            for (const auto& [t, v] : s.points)
                if (t >= from_ - 1) pts.push_back({X(t), Y(v)});
            if (pts.empty()) continue;
            // L'aire sous la courbe, a peine teintee.
            std::vector<gfx::Vertex> tris;
            gfx::Color fill = col;
            fill.a = 24;
            for (std::size_t k = 1; k < pts.size(); ++k) {
                const gfx::Point a = pts[k - 1], d = pts[k];
                const gfx::Point a0{a.x, plotBottom}, d0{d.x, plotBottom};
                tris.push_back({a, fill}); tris.push_back({d, fill}); tris.push_back({d0, fill});
                tris.push_back({a, fill}); tris.push_back({d0, fill}); tris.push_back({a0, fill});
            }
            if (!tris.empty()) r.fillTriangles(tris.data(), tris.size());
            for (std::size_t k = 1; k < pts.size(); ++k) r.line(pts[k - 1], pts[k], col, 1.6f);
            circle(r, pts.back(), 2.8f, col);
        }
        r.popClip();
        // La legende : "nom = valeur" ; l'echelle a droite.
        float lx = left + 6;
        const float limit = right - 170;
        for (const auto i : groups[gi]) {
            const auto& s = series_[i];
            const gfx::Color col = s.forced ? gfx::Color{236, 132, 38, 255} : s.color;
            std::string text = (s.forced ? "F " : s.animated ? "~ " : "") + s.name;
            if (s.simulated) {
                text += " (esclave simul\xC3\xA9";
                if (s.band) text += ", zone " + mbtool::frenchNumber(s.band->first) + " - " + mbtool::frenchNumber(s.band->second);
                text += ")";
            }
            if (!s.last.empty()) text += " = " + s.last;
            const float w = r.measure(text, kTiny).width;
            if (lx + w > limit && lx > left + 6) {
                r.drawText({lx, y0 + 3}, "\xE2\x80\xA6", kTiny, c.textMuted);
                break;
            }
            r.fillRect({lx, y0 + 9, 10, 3}, col);
            r.drawText({lx + 14, y0 + 3}, text, kTiny, s.simulated ? simViolet() : c.text);
            lx += 14 + w + 14;
        }
        std::string scale = mbtool::frenchNumber(dlo) + " \xE2\x80\x93 " + mbtool::frenchNumber(dhi);
        const auto& first = series_[groups[gi].front()];
        if (!first.unit.empty()) scale += " " + first.unit;
        const float sw = r.measure(scale, kTiny).width;
        r.drawText({right - sw - 2, y0 + 3}, scale, kTiny, c.textMuted);
    }
}

// ============================================================ bande de titre ===
HmiBlockHead::HmiBlockHead(std::string id) : ui::Widget(std::move(id)) {}

void HmiBlockHead::setCaption(std::string caption, std::string summary) {
    if (caption == caption_ && summary == summary_) return;
    caption_ = std::move(caption);
    summary_ = std::move(summary);
    invalidate();
}

void HmiBlockHead::setParts(std::vector<Part> parts) {
    bool same = parts.size() == parts_.size();
    for (std::size_t i = 0; same && i < parts.size(); ++i)
        same = parts[i].id == parts_[i].id && parts[i].text == parts_[i].text && parts[i].on == parts_[i].on && parts[i].count == parts_[i].count;
    if (same) return;
    parts_ = std::move(parts);
    invalidate();
}

void HmiBlockHead::place(const gfx::IRenderer* r) const {
    const auto b = bounds();
    const auto measure = [&](const std::string& s, gfx::FontId f) { return r ? r->measure(s, f).width : ui::measureWidth(s, f); };
    rects_.assign(parts_.size(), gfx::Rect{});
    // A gauche : apres le titre et le resume.
    float x = b.x + 10;
    if (!caption_.empty()) x += measure(upperAscii(caption_), kTiny) * 1.08f + 12;
    if (!summary_.empty()) x += measure(summary_, kSmall) + 12;
    const auto width = [&](const Part& p) {
        switch (p.kind) {
            case Kind::Link: return measure(p.text, kSmall) + 24;
            case Kind::Segment: return measure(p.text, kSmall) + (p.count.empty() ? 0.f : measure(p.count, kTiny) + 6) + 20;
            case Kind::Check: return measure(p.text, kSmall) + 24;
            case Kind::Tag: return measure(p.text, kTiny) + 14;
            case Kind::Text: return measure(p.text, kSmall) + 4;
        }
        return 0.f;
    };
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        if (parts_[i].right) continue;
        const float w = width(parts_[i]);
        rects_[i] = {x, b.y + 4, w, b.h - 8};
        const bool nextJoined = i + 1 < parts_.size() && !parts_[i + 1].right && parts_[i].kind == Kind::Segment
                                && parts_[i + 1].kind == Kind::Segment && parts_[i + 1].group == parts_[i].group;
        x += w + (nextJoined ? 0.f : 10.f);
    }
    // A droite : de la droite vers la gauche.
    float rx = b.x + b.w - 10;
    for (std::size_t k = parts_.size(); k-- > 0;) {
        if (!parts_[k].right) continue;
        const float w = width(parts_[k]);
        rx -= w;
        rects_[k] = {rx, b.y + 4, w, b.h - 8};
        const bool prevJoined = k > 0 && parts_[k - 1].right && parts_[k].kind == Kind::Segment && parts_[k - 1].kind == Kind::Segment
                                && parts_[k - 1].group == parts_[k].group;
        rx -= prevJoined ? 0.f : 12.f;
    }
}

gfx::Rect HmiBlockHead::partRect(int id) const {
    place(nullptr);
    for (std::size_t i = 0; i < parts_.size(); ++i)
        if (parts_[i].id == id) return rects_[i];
    return {};
}

void HmiBlockHead::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    r.fillRect(b, c.headerBg);
    r.line({b.x, b.y + 0.5f}, {b.x + b.w, b.y + 0.5f}, c.border, 1.f);
    r.line({b.x, b.y + b.h - 0.5f}, {b.x + b.w, b.y + b.h - 0.5f}, c.border, 1.f);
    place(&r);
    float x = b.x + 10;
    const float ty = b.y + (b.h - r.lineHeight(kSmall)) / 2;
    if (!caption_.empty()) {
        const std::string cap = upperAscii(caption_);
        // Les lettres un peu espacees, en gras.
        float cx = x;
        for (std::size_t i = 0; i < cap.size(); ++i) {
            std::size_t n = 1;
            while (i + n < cap.size() && (static_cast<unsigned char>(cap[i + n]) & 0xC0) == 0x80) ++n;
            const std::string ch = cap.substr(i, n);
            boldText(r, {cx, b.y + (b.h - r.lineHeight(kTiny)) / 2}, ch, kTiny, c.textMuted);
            cx += r.measure(ch, kTiny).width * 1.08f;
            i += n - 1;
        }
        x = cx + 12;
    }
    if (!summary_.empty()) r.drawText({x, ty}, summary_, kSmall, c.textMuted);
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const auto& p = parts_[i];
        const auto pr = rects_[i];
        const bool hot = static_cast<int>(i) == hover_;
        switch (p.kind) {
            case Kind::Link: {
                const gfx::Color col = hot ? c.accentHover : c.accent;
                drawCyclicGlyph(r, GPlus, {pr.x, pr.y + (pr.h - 14) / 2, 14, 14}, col);
                r.drawText({pr.x + 18, ty}, p.text, kSmall, col);
                break;
            }
            case Kind::Segment: {
                const bool firstOf = i == 0 || parts_[i - 1].kind != Kind::Segment || parts_[i - 1].group != p.group || parts_[i - 1].right != p.right;
                const bool lastOf = i + 1 >= parts_.size() || parts_[i + 1].kind != Kind::Segment || parts_[i + 1].group != p.group
                                    || parts_[i + 1].right != p.right;
                if (p.on) r.fillRect(pr, c.selectionBg);
                else if (hot) r.fillRect(pr, c.text.withAlpha(20));
                r.line({pr.x, pr.y}, {pr.x + pr.w, pr.y}, c.border, 1.f);
                r.line({pr.x, pr.y + pr.h}, {pr.x + pr.w, pr.y + pr.h}, c.border, 1.f);
                if (firstOf) r.line({pr.x, pr.y}, {pr.x, pr.y + pr.h}, c.border, 1.f);
                r.line({pr.x + pr.w, pr.y}, {pr.x + pr.w, pr.y + pr.h}, c.border, 1.f);
                (void)lastOf;
                const gfx::Color col = p.on ? c.text : c.textMuted;
                r.drawText({pr.x + 10, ty}, p.text, kSmall, col);
                if (!p.count.empty())
                    r.drawText({pr.x + 10 + r.measure(p.text, kSmall).width + 6, b.y + (b.h - r.lineHeight(kTiny)) / 2}, p.count, kTiny, c.textMuted);
                break;
            }
            case Kind::Check:
                drawCyclicGlyph(r, p.on ? GBoxOn : GBoxOff, {pr.x, pr.y + (pr.h - 16) / 2, 16, 16}, p.on ? c.accent : c.textMuted);
                r.drawText({pr.x + 22, ty}, p.text, kSmall, hot ? c.text : c.textMuted);
                break;
            case Kind::Tag:
                r.fillRoundedRect({pr.x, pr.y + 2, pr.w, pr.h - 4}, c.accent, 3);
                boldText(r, {pr.x + 7, b.y + (b.h - r.lineHeight(kTiny)) / 2}, p.text, kTiny, c.textInverted);
                break;
            case Kind::Text: r.drawText({pr.x, ty}, p.text, kSmall, c.textMuted); break;
        }
    }
}

ui::EventResult HmiBlockHead::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        place(nullptr);
        int h = -1;
        for (std::size_t i = 0; i < parts_.size(); ++i)
            if (parts_[i].kind != Kind::Text && parts_[i].kind != Kind::Tag && rects_[i].contains(m->pos)) h = static_cast<int>(i);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        place(nullptr);
        for (std::size_t i = 0; i < parts_.size(); ++i)
            if (parts_[i].kind != Kind::Text && parts_[i].kind != Kind::Tag && rects_[i].contains(d->pos)) {
                clicked->emit(parts_[i].id);
                return ui::EventResult::Consumed;
            }
    }
    return ui::EventResult::Ignored;
}

// ============================================================== le tableau ===
class HmiCyclicTable::Model final : public ui::ITableModel {
public:
    std::vector<std::string>                 headers;
    std::vector<std::vector<std::string>>    cells;
    std::vector<std::vector<ui::CellStyle>>  styles;
    std::vector<std::string>                 tips;
    [[nodiscard]] std::size_t rowCount() const override { return cells.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers.size() ? headers[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < cells.size() && c < cells[r].size() ? cells[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return r < styles.size() && c < styles[r].size() ? styles[r][c] : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips.size() ? tips[r] : std::string{}; }
};

HmiCyclicTable::HmiCyclicTable(std::string id) : ui::TableView(std::move(id)) {
    model_ = std::make_shared<Model>();
    setModel(model_);
    setSelectionMode(ui::SelectionMode::Single);
    setIconPainter([](gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color) { drawCyclicGlyph(r, icon, box, color); });
}

void HmiCyclicTable::setContent(std::vector<std::string> headers, std::vector<std::vector<std::string>> cells,
                                std::vector<std::vector<ui::CellStyle>> styles, std::vector<std::string> tips) {
    const bool reshape = cells.size() != model_->cells.size() || headers != model_->headers;
    model_->headers = std::move(headers);
    model_->cells = std::move(cells);
    model_->styles = std::move(styles);
    model_->tips = std::move(tips);
    if (reshape) {
        // Des lignes en plus ou en moins : la vue se refait ; le defilement reste.
        const float y = scrollOffset();
        model_->modelReset->emit();
        setScrollOffset(y);
    }
    invalidate();
}

std::string HmiCyclicTable::cell(std::size_t row, std::size_t col) const {
    return row < model_->cells.size() && col < model_->cells[row].size() ? model_->cells[row][col] : std::string{};
}

std::size_t HmiCyclicTable::rows() const noexcept { return model_->cells.size(); }

ui::EventResult HmiCyclicTable::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        for (std::size_t i = 0; i < visibleRowCount(); ++i) {
            gfx::Rect rr;
            if (!rowRect(i, rr) || !rr.contains(d->pos)) continue;
            const ui::RowIndex row = viewRow(i);
            int col = -1;
            for (std::size_t c = 0; c < columns().size(); ++c) {
                gfx::Rect cr;
                if (cellRect(i, c, cr) && cr.contains(d->pos)) {
                    col = static_cast<int>(c);
                    break;
                }
            }
            const auto result = ui::TableView::onEvent(ev);
            if (d->clickCount == 1) {
                if (col >= 0 && col == boxColumn_) boxClicked->emit(row, col);
                else rowClicked->emit(row);
            }
            return result;
        }
    }
    return ui::TableView::onEvent(ev);
}

// ============================================================ au-dessus du volet ===
HmiCyclicOverlay::HmiCyclicOverlay(std::string id, bool dim) : ui::Widget(std::move(id)), dim_(dim) {}

void HmiCyclicOverlay::layoutIn(const gfx::Rect& host) {
    host_ = host;
    setBounds(host);
    placePanel(host);
    invalidateLayout();
}

void HmiCyclicOverlay::onLayout() { placePanel(bounds()); }

void HmiCyclicOverlay::close() {
    if (closed_) return;
    closed_ = true;
    setVisibility(ui::Visibility::Collapsed);
    closedSignal->emit();
}

gfx::Rect HmiCyclicOverlay::paintFrame(const ui::PaintContext& ctx, const std::string& title, int glyph, float footer) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto p = panel_;
    r.fillRoundedRect({p.x + 3, p.y + 6, p.w, p.h}, gfx::Color{0, 0, 0, 90}, 9);
    r.fillRoundedRect(p, ctx.theme.brand.card, 8);
    r.strokeRect(p, ctx.theme.brand.cardBorder, 1.f);
    // L'en-tete.
    const gfx::Rect head{p.x + 1, p.y + 1, p.w - 2, 40};
    r.fillRect(head, c.headerBg);
    r.line({p.x, head.y + head.h}, {p.x + p.w, head.y + head.h}, ctx.theme.brand.cardBorder, 1.f);
    float tx = head.x + 14;
    if (glyph) {
        drawCyclicGlyph(r, glyph, {tx, head.y + 12, 16, 16}, c.text);
        tx += 24;
    }
    boldText(r, {tx, head.y + (head.h - r.lineHeight(kUi)) / 2}, title, kUi, c.text);
    closeBox_ = {head.x + head.w - 34, head.y + 8, 24, 24};
    r.line({closeBox_.x + 7, closeBox_.y + 7}, {closeBox_.x + 17, closeBox_.y + 17}, c.textMuted, 1.5f);
    r.line({closeBox_.x + 7, closeBox_.y + 17}, {closeBox_.x + 17, closeBox_.y + 7}, c.textMuted, 1.5f);
    if (footer > 0) {
        const gfx::Rect foot{p.x + 1, p.y + p.h - footer - 1, p.w - 2, footer};
        r.fillRect(foot, c.headerBg);
        r.line({p.x, foot.y}, {p.x + p.w, foot.y}, ctx.theme.brand.cardBorder, 1.f);
    }
    return {p.x + 14, p.y + 52, p.w - 28, p.h - 52 - footer - 10};
}

void HmiCyclicOverlay::onPaint(const ui::PaintContext& ctx) {
    if (dim_) ctx.r.fillRect(bounds(), gfx::Color{0, 10, 13, 140});
    paintPanel(ctx);
}

ui::EventResult HmiCyclicOverlay::onEvent(const ui::InputEvent& ev) {
    if (closed_) return ui::EventResult::Ignored;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!panel_.contains(d->pos)) {
            close();
            return ui::EventResult::Consumed;
        }
        if (closeBox_.w > 0 && closeBox_.contains(d->pos)) {
            close();
            return ui::EventResult::Consumed;
        }
        (void)panelEvent(ev);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            close();
            return ui::EventResult::Consumed;
        }
        (void)panelEvent(ev);
        return ui::EventResult::Consumed;      // modal : le reste du volet ne voit pas les touches
    }
    if (std::get_if<ui::MouseMove>(&ev) || std::get_if<ui::MouseUp>(&ev) || std::get_if<ui::MouseWheel>(&ev)) {
        (void)panelEvent(ev);
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::TextInput>(&ev)) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

// ------------------------------------------------------------------ le menu ---
HmiCyclicMenu::HmiCyclicMenu(std::string id, std::vector<Item> items, gfx::Point anchor, float width)
    : HmiCyclicOverlay(std::move(id), false), items_(std::move(items)), anchor_(anchor), width_(width) {}

float HmiCyclicMenu::itemHeight(const Item& it) const {
    if (it.separator) return 9;
    if (it.heading) return 24;
    return (it.detail.empty() && it.why.empty()) ? 30 : 44;
}

void HmiCyclicMenu::placePanel(const gfx::Rect& host) {
    float h = 10;
    for (const auto& it : items_) h += itemHeight(it);
    float x = anchor_.x, y = anchor_.y;
    if (host.w > 0) {
        x = std::clamp(x, host.x + 4, std::max(host.x + 4, host.x + host.w - width_ - 4));
        if (y + h > host.y + host.h - 4) y = std::max(host.y + 4, host.y + host.h - h - 4);
    }
    panel_ = {x, y, width_, h};
    rects_.assign(items_.size(), gfx::Rect{});
    float yy = panel_.y + 5;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const float ih = itemHeight(items_[i]);
        rects_[i] = {panel_.x + 5, yy, panel_.w - 10, ih};
        yy += ih;
    }
}

gfx::Rect HmiCyclicMenu::itemRect(int id) const {
    for (std::size_t i = 0; i < items_.size() && i < rects_.size(); ++i)
        if (items_[i].id == id && !items_[i].separator && !items_[i].heading) return rects_[i];
    return {};
}

bool HmiCyclicMenu::choose(int id) {
    for (const auto& it : items_) {
        if (it.id != id || it.separator || it.heading) continue;
        if (!it.enabled) return false;
        auto cb = chosen;
        close();
        if (cb) cb(id);
        return true;
    }
    return false;
}

void HmiCyclicMenu::paintPanel(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    if (rects_.size() != items_.size()) placePanel(bounds());
    r.fillRoundedRect({panel_.x + 2, panel_.y + 8, panel_.w, panel_.h}, gfx::Color{0, 0, 0, 110}, 8);
    r.fillRoundedRect(panel_, ctx.theme.brand.card, 7);
    r.strokeRect(panel_, ctx.theme.brand.cardBorder, 1.f);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        const auto ir = rects_[i];
        if (it.separator) {
            r.line({ir.x + 6, ir.y + ir.h / 2}, {ir.x + ir.w - 6, ir.y + ir.h / 2}, ctx.theme.brand.cardBorder, 1.f);
            continue;
        }
        if (it.heading) {
            boldText(r, {ir.x + 10, ir.y + 6}, upperAscii(it.label), kTiny, c.textMuted);
            continue;
        }
        if (static_cast<int>(i) == hover_ && it.enabled) r.fillRoundedRect(ir, c.selectionBg, 5);
        else if (it.current) r.fillRoundedRect(ir, c.headerBg, 5);
        const gfx::Color ink = it.enabled ? c.text : c.textDisabled;
        float x = ir.x + 10;
        if (it.check) r.drawText({x, ir.y + 6}, "\xE2\x9C\x93", kUi, c.accent);
        if (it.glyph) drawCyclicGlyph(r, it.glyph, {x, ir.y + 7, 16, 16}, it.enabled ? c.text : c.textDisabled);
        x += 26;
        r.drawText({x, ir.y + 6}, it.label, kUi, ink);
        const std::string sub = it.enabled ? it.detail : it.why;
        if (!sub.empty()) r.drawText({x, ir.y + 25}, fitted(r, sub, kTiny, ir.x + ir.w - x - 8), kTiny, c.textMuted);
        if (!it.shortcut.empty()) {
            const float w = r.measure(it.shortcut, kTiny).width + 10;
            const gfx::Rect kbd{ir.x + ir.w - w - 8, ir.y + 7, w, 18};
            r.strokeRect(kbd, c.border, 1.f);
            r.drawText({kbd.x + 5, kbd.y + 1}, it.shortcut, kTiny, c.textMuted);
        }
    }
}

ui::EventResult HmiCyclicMenu::panelEvent(const ui::InputEvent& ev) {
    const auto at = [&](gfx::Point p) {
        for (std::size_t i = 0; i < items_.size() && i < rects_.size(); ++i)
            if (!items_[i].separator && !items_[i].heading && rects_[i].contains(p)) return static_cast<int>(i);
        return -1;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = at(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const int i = at(d->pos);
        if (i >= 0) (void)choose(items_[static_cast<std::size_t>(i)].id);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        const auto step = [&](int dir) {
            int i = hover_;
            for (std::size_t n = 0; n < items_.size(); ++n) {
                i = (i + dir + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
                const auto& it = items_[static_cast<std::size_t>(i)];
                if (!it.separator && !it.heading && it.enabled) break;
            }
            hover_ = i;
            invalidate();
        };
        if (k->key == ui::Key::Down) step(1);
        else if (k->key == ui::Key::Up) step(-1);
        else if (k->key == ui::Key::Return && hover_ >= 0) (void)choose(items_[static_cast<std::size_t>(hover_)].id);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ------------------------------------------------------------- un nom a taper ---
HmiCyclicPrompt::HmiCyclicPrompt(std::string id, std::string title, std::string text, std::string value, std::string confirm)
    : HmiCyclicOverlay(id, true), title_(std::move(title)), text_(std::move(text)) {
    field_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(id + ".nom")));
    field_->setText(std::move(value));
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", id + ".annuler")));
    ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(std::move(confirm), id + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { (void)accept(); });
    links_ += cancel_->clicked->connect([this] { close(); });
    links_ += field_->editingDone->connect([this](const std::string&) {
        if (field_->focused() && !closed()) (void)accept();
    });
}

bool HmiCyclicPrompt::accept() {
    std::string why;
    if (done && !done(field_->text(), &why)) {
        error_ = why;
        invalidate();
        return false;
    }
    close();
    return true;
}

void HmiCyclicPrompt::placePanel(const gfx::Rect& host) {
    const float w = std::min(560.f, host.w - 40), h = 230;
    panel_ = {host.x + (host.w - w) / 2, host.y + std::max(30.f, (host.h - h) / 3), w, h};
    field_->setBounds({panel_.x + 16, panel_.y + 106, panel_.w - 32, 32});
    const float by = panel_.y + panel_.h - 42;
    ok_->setBounds({panel_.x + panel_.w - 16 - 150, by, 150, 32});
    cancel_->setBounds({panel_.x + panel_.w - 16 - 150 - 10 - 110, by, 110, 32});
}

void HmiCyclicPrompt::paintPanel(const ui::PaintContext& ctx) {
    const auto body = paintFrame(ctx, title_, GSave, 52);
    float y = body.y;
    for (const auto& line : wrap(text_, kSmall, body.w)) {
        ctx.r.drawText({body.x + 2, y}, line, kSmall, ctx.theme.color.textMuted);
        y += 18;
    }
    if (!error_.empty()) ctx.r.drawText({body.x + 2, panel_.y + 144}, fitted(ctx.r, error_, kSmall, body.w), kSmall, ctx.theme.color.error);
}

ui::EventResult HmiCyclicPrompt::panelEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Return) {
        (void)accept();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ======================================================= Lire des variables ===
HmiCyclicVarsDialog::HmiCyclicVarsDialog(std::string id, std::vector<CyclicVarTab> tabs, int nextId)
    : HmiCyclicOverlay(id, true), tabs_(std::move(tabs)), nextId_(nextId) {
    checked_.resize(tabs_.size());
    for (std::size_t t = 0; t < tabs_.size(); ++t) {
        checked_[t].resize(tabs_[t].groups.size());
        for (std::size_t g = 0; g < tabs_[t].groups.size(); ++g) checked_[t][g].assign(tabs_[t].groups[g].items.size(), false);
    }
    search_field_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(id + ".chercher")));
    search_field_->setPlaceholder("Chercher un nom, une adresse (%MW1110, 41111)\xE2\x80\xA6");
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", id + ".annuler")));
    ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ajouter", id + ".ajouter")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += search_field_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    links_ += ok_->clicked->connect([this] { (void)accept(); });
    links_ += cancel_->clicked->connect([this] { close(); });
    rebuildLines();
    refreshPlan();
}

std::string HmiCyclicVarsDialog::targetOf(const CyclicVarGroup& g) const {
    const auto& t = tabs_[tab_];
    if (!t.targets.empty() && target_ >= 0 && static_cast<std::size_t>(target_) < t.targets.size()) return t.targets[static_cast<std::size_t>(target_)];
    return g.target;
}

std::string HmiCyclicVarsDialog::target() const {
    if (tabs_.empty()) return {};
    const auto& t = tabs_[tab_];
    if (!t.targets.empty() && static_cast<std::size_t>(target_) < t.targets.size()) return t.targets[static_cast<std::size_t>(target_)];
    return {};
}

bool HmiCyclicVarsDialog::setTarget(const std::string& label) {
    if (tabs_.empty()) return false;
    const auto& t = tabs_[tab_].targets;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (t[i] == label || lowerAscii(t[i]).rfind(lowerAscii(label), 0) == 0) {
            target_ = static_cast<int>(i);
            refreshPlan();
            invalidate();
            return true;
        }
    return false;
}

bool HmiCyclicVarsDialog::matches(const CyclicVarItem& it) const {
    if (search_.empty()) return true;
    const std::string s = lowerAscii(search_);
    return lowerAscii(it.name).find(s) != std::string::npos || lowerAscii(it.place).find(s) != std::string::npos;
}

void HmiCyclicVarsDialog::rebuildLines() {
    lines_.clear();
    if (tabs_.empty()) return;
    const auto& groups = tabs_[tab_].groups;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const auto& gr = groups[g];
        bool any = gr.items.empty() ? search_.empty() || lowerAscii(gr.title).find(lowerAscii(search_)) != std::string::npos : false;
        for (const auto& it : gr.items) any = any || matches(it);
        if (!any) continue;
        lines_.push_back({static_cast<int>(g), -1});
        if ((gr.open || !search_.empty()) && gr.items.size() > 1)
            for (std::size_t i = 0; i < gr.items.size(); ++i)
                if (matches(gr.items[i])) lines_.push_back({static_cast<int>(g), static_cast<int>(i)});
    }
    scroll_ = std::min(scroll_, std::max(0.f, static_cast<float>(lines_.size()) * 26.f - list_.h));
}

void HmiCyclicVarsDialog::refreshPlan() {
    planned_.clear();
    count_ = 0;
    if (tabs_.empty()) return;
    // Par cible : les variables cochees des DEUX onglets (Automate, Variables IHM
    // liees), regroupees. Avant : seulement l'onglet ouvert, et ce qui etait coche
    // sur l'autre ne comptait plus. La cible choisie vaut pour l'onglet ouvert ;
    // l'autre garde la sienne (la premiere du choix, ou celle de chaque groupe).
    std::map<std::string, std::vector<mbtool::ReadItem>> byTarget;
    std::vector<std::string> order;
    for (std::size_t t = 0; t < tabs_.size(); ++t) {
        const auto& groups = tabs_[t].groups;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            if (!groups[g].disabled.empty()) continue;
            for (std::size_t i = 0; i < groups[g].items.size(); ++i) {
                if (!checked_[t][g][i]) continue;
                const std::string target = t == tab_ ? targetOf(groups[g])
                                                     : tabs_[t].targets.empty() ? groups[g].target : tabs_[t].targets.front();
                if (!byTarget.count(target)) order.push_back(target);
                byTarget[target].push_back(groups[g].items[i].read);
                ++count_;
            }
        }
    }
    for (const auto& target : order) {
        auto& items = byTarget[target];
        for (auto& pr : mbtool::planReads(items, merge_, gap_)) planned_.push_back({target, std::move(pr), items});
    }
    ok_->setText(planned_.empty() ? std::string("Ajouter") : "Ajouter " + std::to_string(planned_.size()) + (planned_.size() > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte"));
    ok_->setEnabled(!planned_.empty());
    invalidate();
}

std::vector<HmiCyclicVarsDialog::Planned> HmiCyclicVarsDialog::plan() const { return planned_; }
std::size_t HmiCyclicVarsDialog::checkedCount() const { return count_; }

bool HmiCyclicVarsDialog::check(const std::string& name, bool on) {
    if (tabs_.empty()) return false;
    // L'onglet ouvert d'abord, puis l'autre : une variable IHM liee cochee depuis
    // l'onglet Automate fait passer sur son onglet, et son groupe se deplie (avant :
    // rien n'etait coche, et le dialogue disait "0 variable -> 0 requete").
    std::vector<std::size_t> order{tab_};
    for (std::size_t t = 0; t < tabs_.size(); ++t)
        if (t != tab_) order.push_back(t);
    for (const std::size_t t : order) {
        bool found = false;
        auto& groups = tabs_[t].groups;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            if (!groups[g].disabled.empty()) continue;
            if (groups[g].title == name) {
                for (std::size_t i = 0; i < groups[g].items.size(); ++i) checked_[t][g][i] = on;
                found = true;
                continue;
            }
            for (std::size_t i = 0; i < groups[g].items.size(); ++i)
                if (groups[g].items[i].name == name) {
                    checked_[t][g][i] = on;
                    if (on) groups[g].open = true;
                    found = true;
                }
        }
        if (!found) continue;
        if (t != tab_) {
            showTab(t);
        } else {
            rebuildLines();
            refreshPlan();
        }
        return true;
    }
    return false;
}

void HmiCyclicVarsDialog::checkAll(bool on) {
    if (tabs_.empty()) return;
    for (std::size_t g = 0; g < tabs_[tab_].groups.size(); ++g)
        if (tabs_[tab_].groups[g].disabled.empty()) std::fill(checked_[tab_][g].begin(), checked_[tab_][g].end(), on);
    refreshPlan();
}

void HmiCyclicVarsDialog::showTab(std::size_t index) {
    if (index >= tabs_.size()) return;
    tab_ = index;
    target_ = 0;
    scroll_ = 0;
    rebuildLines();
    refreshPlan();
    invalidateLayout();
}

void HmiCyclicVarsDialog::setMerge(bool on) {
    merge_ = on;
    refreshPlan();
}

void HmiCyclicVarsDialog::setGap(int words) {
    gap_ = std::clamp(words, 0, 100);
    refreshPlan();
}

void HmiCyclicVarsDialog::setSearch(const std::string& text) {
    if (text == search_) return;
    search_ = text;
    if (search_field_->text() != text) search_field_->setText(text);
    scroll_ = 0;
    rebuildLines();
    invalidate();
}

void HmiCyclicVarsDialog::openGroup(const std::string& title, bool open) {
    if (tabs_.empty()) return;
    for (auto& g : tabs_[tab_].groups)
        if (g.title == title) g.open = open;
    rebuildLines();
    invalidate();
}

bool HmiCyclicVarsDialog::accept() {
    if (planned_.empty()) return false;
    auto cb = added;
    auto planned = planned_;
    close();
    if (cb) cb(planned);
    return true;
}

void HmiCyclicVarsDialog::placePanel(const gfx::Rect& host) {
    const float w = std::min(1040.f, host.w - 40), h = std::min(680.f, host.h - 40);
    panel_ = {host.x + (host.w - w) / 2, host.y + (host.h - h) / 2, w, h};
    const float rightW = 330;
    const gfx::Rect body{panel_.x + 14, panel_.y + 52, panel_.w - 28, panel_.h - 52 - 54 - 8};
    const float leftW = body.w - rightW - 16;
    tabRects_[0] = {body.x, body.y, 0, 30};
    tabRects_[1] = {body.x, body.y, 0, 30};
    float tx = body.x;
    for (std::size_t t = 0; t < tabs_.size() && t < 2; ++t) {
        const float tw = ui::measureWidth(tabs_[t].label, kUi) + 24;
        tabRects_[t] = {tx, body.y, tw, 30};
        tx += tw + 4;
    }
    const bool targets = !tabs_.empty() && !tabs_[tab_].targets.empty();
    const float targetW = targets ? 280.f : 0.f;
    search_field_->setBounds({body.x, body.y + 38, leftW - targetW - (targets ? 8 : 0), 30});
    targetBox_ = targets ? gfx::Rect{body.x + leftW - targetW, body.y + 38, targetW, 30} : gfx::Rect{};
    list_ = {body.x, body.y + 76, leftW, body.h - 76};
    planBox_ = {body.x + leftW + 16, body.y + 46, rightW, body.h - 46 - 112};
    const float oy = planBox_.y + planBox_.h + 30;
    mergeBox_ = {planBox_.x, oy, 220, 22};
    minus_ = {planBox_.x + 100, oy + 28, 26, 24};
    plus_ = {planBox_.x + 130, oy + 28, 26, 24};
    const float by = panel_.y + panel_.h - 44;
    ok_->setBounds({panel_.x + panel_.w - 14 - 190, by, 190, 32});
    cancel_->setBounds({panel_.x + panel_.w - 14 - 190 - 10 - 110, by, 110, 32});
}

void HmiCyclicVarsDialog::onLayout() { placePanel(bounds()); }

void HmiCyclicVarsDialog::paintPanel(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    (void)paintFrame(ctx, "Lire des variables", GVariables, 54);
    // Les onglets.
    for (std::size_t t = 0; t < tabs_.size() && t < 2; ++t) {
        const auto tr = tabRects_[t];
        const bool on = t == tab_;
        r.drawText({tr.x + 12, tr.y + (tr.h - r.lineHeight(kUi)) / 2}, tabs_[t].label, kUi, on ? c.text : c.textMuted);
        if (on) r.fillRect({tr.x, tr.y + tr.h - 2, tr.w, 2}, c.accent);
    }
    r.line({list_.x, tabRects_[0].y + 30}, {list_.x + list_.w, tabRects_[0].y + 30}, ctx.theme.brand.cardBorder, 1.f);
    // La cible.
    if (targetBox_.w > 0) {
        r.fillRoundedRect(targetBox_, c.inputBg, 4);
        r.strokeRect(targetBox_, c.border, 1.f);
        const std::string t = "Cible : " + target() + " \xE2\x96\xBE";
        r.drawText({targetBox_.x + 8, targetBox_.y + (targetBox_.h - r.lineHeight(kSmall)) / 2}, fitted(r, t, kSmall, targetBox_.w - 14), kSmall, c.text);
    }
    // La liste.
    r.fillRoundedRect(list_, c.inputBg, 5);
    r.strokeRect(list_, c.border, 1.f);
    r.pushClip(list_);
    const float lh = 26;
    const auto& groups = tabs_.empty() ? std::vector<CyclicVarGroup>{} : tabs_[tab_].groups;
    if (lines_.empty()) {
        const std::string e = tabs_.empty() || tabs_[tab_].empty.empty() ? std::string("Aucune variable.") : tabs_[tab_].empty;
        float y = list_.y + 16;
        for (const auto& l : wrap(e, kSmall, list_.w - 32)) {
            r.drawText({list_.x + 16, y}, l, kSmall, c.textMuted);
            y += 20;
        }
    }
    for (std::size_t k = 0; k < lines_.size(); ++k) {
        const float y = list_.y + static_cast<float>(k) * lh - scroll_;
        if (y + lh < list_.y || y > list_.y + list_.h) continue;
        const auto& ln = lines_[k];
        const auto& g = groups[static_cast<std::size_t>(ln.group)];
        const gfx::Rect row{list_.x + 1, y, list_.w - 2, lh};
        if (static_cast<int>(k) == hoverLine_ && g.disabled.empty()) r.fillRect(row, ctx.theme.brand.hover);
        r.line({row.x, row.y + lh - 0.5f}, {row.x + row.w, row.y + lh - 0.5f}, c.gridLine, 1.f);
        const float ty = y + (lh - r.lineHeight(kSmall)) / 2;
        const float colType = row.x + row.w - 300;
        if (ln.item < 0) {
            const bool dis = !g.disabled.empty();
            std::size_t on = 0;
            for (const bool b : checked_[tab_][static_cast<std::size_t>(ln.group)]) on += b ? 1 : 0;
            if (g.items.size() > 1 && !dis) {
                const std::string arrow = g.open || !search_.empty() ? "\xE2\x96\xBE" : "\xE2\x96\xB8";
                r.drawText({row.x + 6, ty}, arrow, kSmall, c.textMuted);
            }
            const int box = dis ? GBoxDisabled : on == g.items.size() && on > 0 ? GBoxOn : GBoxOff;
            drawCyclicGlyph(r, box, {row.x + 22, y + 5, 16, 16}, dis ? c.textMuted : on ? c.accent : c.textMuted);
            if (!dis && on > 0 && on < g.items.size()) r.fillRect({row.x + 27, y + 12, 6, 2}, c.accent);
            std::string title = g.title;
            if (g.items.size() > 1) title += "  (" + std::to_string(g.items.size()) + ")";
            float tx = row.x + 46;
            r.drawText({tx, ty}, fitted(r, title, kSmall, colType - tx - 8), kSmall, dis ? c.textDisabled : c.text);
            tx += r.measure(title, kSmall).width + 8;
            // Ce qui est deja lu.
            std::vector<std::string> already;
            for (const auto& it : g.items)
                if (!it.already.empty() && std::find(already.begin(), already.end(), it.already) == already.end()) already.push_back(it.already);
            std::size_t n = 0;
            for (const auto& it : g.items) n += it.already.empty() ? 0 : 1;
            if (n && !dis) {
                const std::string al = "\xC2\xB7 " + (n == g.items.size() ? std::string("d\xC3\xA9j\xC3\xA0 lue") + (n > 1 ? "s" : "")
                                                                           : std::to_string(n) + " d\xC3\xA9j\xC3\xA0 lues")
                                       + " (" + already.front() + ")";
                if (tx + r.measure(al, kTiny).width < colType - 4) r.drawText({tx, ty + 1}, al, kTiny, c.warning);
            }
            if (dis) {
                r.drawText({colType, ty}, fitted(r, g.disabled, kTiny, row.x + row.w - colType - 8), kTiny, c.textDisabled);
            } else {
                r.drawText({colType, ty}, fitted(r, g.type, kTiny, 120), kTiny, c.textMuted);
                const float w = r.measure(g.range, kTiny).width;
                r.drawText({row.x + row.w - 10 - w, ty}, g.range, kTiny, c.textMuted);
            }
        } else {
            const auto& it = g.items[static_cast<std::size_t>(ln.item)];
            const bool on = checked_[tab_][static_cast<std::size_t>(ln.group)][static_cast<std::size_t>(ln.item)];
            drawCyclicGlyph(r, on ? GBoxOn : GBoxOff, {row.x + 46, y + 5, 16, 16}, on ? c.accent : c.textMuted);
            float tx = row.x + 70;
            r.drawText({tx, ty}, fitted(r, it.name, kSmall, colType - tx - 8), kSmall, c.text);
            tx += r.measure(it.name, kSmall).width + 8;
            if (!it.already.empty()) {
                const std::string al = "d\xC3\xA9j\xC3\xA0 dans " + it.already;
                if (tx + r.measure(al, kTiny).width < colType - 4) r.drawText({tx, ty + 1}, al, kTiny, c.warning);
            }
            r.drawText({colType, ty}, fitted(r, it.type, kTiny, 120), kTiny, c.textMuted);
            const float w = r.measure(it.place, kTiny).width;
            r.drawText({row.x + row.w - 10 - w, ty}, it.place, kTiny, c.textMuted);
        }
    }
    r.popClip();
    // A droite : ce que ca donne.
    boldText(r, {planBox_.x, panel_.y + 52}, "CE QUE \xC3\x87" "A DONNE", kTiny, c.textMuted);
    {
        const std::string head = std::to_string(count_) + (count_ > 1 ? " variables" : " variable") + " \xE2\x86\x92 " + std::to_string(planned_.size())
                                 + (planned_.size() > 1 ? " requ\xC3\xAAtes" : " requ\xC3\xAAte");
        r.drawText({planBox_.x, planBox_.y - 22}, head, kSmall, c.text);
    }
    r.pushClip(planBox_);
    float py = planBox_.y - planScroll_;
    if (planned_.empty()) {
        const gfx::Rect card{planBox_.x, py, planBox_.w, 44};
        r.fillRoundedRect(card, c.panelBg, 6);
        r.strokeRect(card, ctx.theme.brand.cardBorder, 1.f);
        r.drawText({card.x + 10, card.y + 13}, "Coche des variables \xC3\xA0 gauche : les requ\xC3\xAAtes viennent ici.", kTiny, c.textMuted);
    }
    for (std::size_t k = 0; k < planned_.size(); ++k) {
        const auto& p = planned_[k];
        const auto& pr = p.read;
        const bool bits = pr.function == 1 || pr.function == 2;
        const float ch = pr.hidden > 0 ? 92.f : 74.f;
        const gfx::Rect card{planBox_.x, py, planBox_.w, ch};
        r.fillRoundedRect(card, c.panelBg, 6);
        r.strokeRect(card, ctx.theme.brand.cardBorder, 1.f);
        const std::string id = "R" + std::to_string(nextId_ + static_cast<int>(k));
        boldText(r, {card.x + 10, card.y + 8}, id, kSmall, c.text);
        float x = card.x + 16 + r.measure(id, kSmall).width;
        const std::string place = mbtool::modiconOf(pr.function, pr.address);
        r.drawText({x, card.y + 8}, place, kSmall, c.text);
        x += r.measure(place, kSmall).width + 10;
        const std::string size = std::to_string(pr.count) + (bits ? (pr.count > 1 ? " bits" : " bit") : " mots");
        r.drawText({x, card.y + 8}, size, kSmall, c.textMuted);
        const std::string nv = std::to_string(pr.items.size()) + (pr.items.size() > 1 ? " variables" : " variable");
        r.drawText({card.x + card.w - 10 - r.measure(nv, kTiny).width, card.y + 9}, nv, kTiny, c.textMuted);
        // Les noms, le format.
        std::string names;
        if (!pr.items.empty()) {
            names = p.items[pr.items.front()].name;
            if (pr.items.size() > 1) names += " \xE2\x80\xA6 " + p.items[pr.items.back()].name;
        }
        std::set<std::string> types;
        for (const auto i : pr.items) types.insert(p.items[i].type);
        std::string fmt = bits ? std::string("bits") : types.size() > 1 ? "par variable" : types.empty() ? std::string{} : lowerAscii(*types.begin());
        r.drawText({card.x + 10, card.y + 30}, fitted(r, names + " \xC2\xB7 " + fmt + " \xC2\xB7 " + p.target, kTiny, card.w - 20), kTiny, c.textMuted);
        float sy = card.y + 50;
        if (pr.hidden > 0) {
            const std::string h = std::to_string(pr.hidden) + (bits ? " bits lus" : " mots lus") + " au passage, pas montr\xC3\xA9s";
            r.drawText({card.x + 10, sy}, fitted(r, h, kTiny, card.w - 20), kTiny, c.textMuted);
            sy += 18;
        }
        // La bande memoire : ce qui est montre, ce qui est lu au passage.
        const gfx::Rect strip{card.x + 10, sy + 2, card.w - 20, 10};
        r.fillRect(strip, c.gridLine);
        if (pr.count > 0)
            for (const auto i : pr.items) {
                const auto& it = p.items[i];
                const float a = static_cast<float>(it.offset - pr.address) / static_cast<float>(pr.count);
                const float wd = static_cast<float>(bits ? 1 : std::max(1, it.words)) / static_cast<float>(pr.count);
                r.fillRect({strip.x + a * strip.w, strip.y, std::max(1.5f, wd * strip.w), strip.h}, c.accent);
            }
        py += ch + 8;
    }
    r.popClip();
    // Le regroupement.
    const float oy = planBox_.y + planBox_.h + 8;
    boldText(r, {planBox_.x, oy}, "REGROUPEMENT", kTiny, c.textMuted);
    drawCyclicGlyph(r, merge_ ? GBoxOn : GBoxOff, {mergeBox_.x, mergeBox_.y + 3, 16, 16}, merge_ ? c.accent : c.textMuted);
    r.drawText({mergeBox_.x + 22, mergeBox_.y + 2}, "Regrouper les voisines", kSmall, c.text);
    const gfx::Color dimText = merge_ ? c.text : c.textDisabled;
    r.drawText({planBox_.x, minus_.y + 3}, "trou de", kSmall, dimText);
    const std::string gap = std::to_string(gap_);
    r.fillRoundedRect({planBox_.x + 58, minus_.y, 36, 24}, c.inputBg, 3);
    r.drawText({planBox_.x + 90 - r.measure(gap, kSmall).width, minus_.y + 3}, gap, kSmall, dimText);
    for (const auto& [box, sign] : {std::pair{minus_, std::string("\xE2\x88\x92")}, std::pair{plus_, std::string("+")}}) {
        r.fillRoundedRect(box, c.panelBg, 3);
        r.strokeRect(box, c.border, 1.f);
        r.drawText({box.x + (box.w - r.measure(sign, kSmall).width) / 2, box.y + 3}, sign, kSmall, dimText);
    }
    r.drawText({plus_.x + plus_.w + 8, minus_.y + 3}, "mots au plus", kSmall, dimText);
    r.drawText({planBox_.x, minus_.y + 32}, "125 mots au plus par requ\xC3\xAAte (la limite de Modbus).", kTiny, c.textMuted);
    // Le pied.
    const std::string note = "Chaque variable garde son nom, son type et son format ; la requ\xC3\xAAte s'appelle d'apr\xC3\xA8s la premi\xC3\xA8re et la derni\xC3\xA8re.";
    r.drawText({panel_.x + 16, panel_.y + panel_.h - 36}, fitted(r, note, kTiny, panel_.w - 360), kTiny, c.textMuted);
}

ui::EventResult HmiCyclicVarsDialog::panelEvent(const ui::InputEvent& ev) {
    const float lh = 26;
    const auto lineAt = [&](gfx::Point p) {
        if (!list_.contains(p)) return -1;
        const int k = static_cast<int>((p.y - list_.y + scroll_) / lh);
        return k >= 0 && k < static_cast<int>(lines_.size()) ? k : -1;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = lineAt(m->pos);
        if (h != hoverLine_) {
            hoverLine_ = h;
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (list_.contains(w->pos)) {
            scroll_ = std::clamp(scroll_ - w->dy * lh * 3, 0.f, std::max(0.f, static_cast<float>(lines_.size()) * lh - list_.h));
            invalidate();
        } else if (planBox_.contains(w->pos)) {
            planScroll_ = std::max(0.f, planScroll_ - w->dy * 40);
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        for (std::size_t t = 0; t < tabs_.size() && t < 2; ++t)
            if (tabRects_[t].contains(d->pos)) {
                showTab(t);
                return ui::EventResult::Consumed;
            }
        if (targetBox_.w > 0 && targetBox_.contains(d->pos) && !tabs_.empty() && !tabs_[tab_].targets.empty()) {
            target_ = (target_ + 1) % static_cast<int>(tabs_[tab_].targets.size());      // la cible suivante
            refreshPlan();
            return ui::EventResult::Consumed;
        }
        if (mergeBox_.contains(d->pos)) {
            setMerge(!merge_);
            return ui::EventResult::Consumed;
        }
        if (minus_.contains(d->pos)) {
            setGap(gap_ - 2);
            return ui::EventResult::Consumed;
        }
        if (plus_.contains(d->pos)) {
            setGap(gap_ + 2);
            return ui::EventResult::Consumed;
        }
        const int k = lineAt(d->pos);
        if (k >= 0 && !tabs_.empty()) {
            const auto& ln = lines_[static_cast<std::size_t>(k)];
            auto& g = tabs_[tab_].groups[static_cast<std::size_t>(ln.group)];
            if (!g.disabled.empty()) return ui::EventResult::Consumed;
            auto& marks = checked_[tab_][static_cast<std::size_t>(ln.group)];
            if (ln.item < 0) {
                const float x = d->pos.x - list_.x;
                if (x < 20 && g.items.size() > 1) {                          // la fleche : deplier
                    g.open = !g.open;
                    rebuildLines();
                } else {
                    const bool all = std::all_of(marks.begin(), marks.end(), [](bool b) { return b; });
                    std::fill(marks.begin(), marks.end(), !all);
                }
            } else {
                marks[static_cast<std::size_t>(ln.item)] = !marks[static_cast<std::size_t>(ln.item)];
            }
            refreshPlan();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Return) {
        (void)accept();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================= Exporter ===
HmiCyclicExportDialog::HmiCyclicExportDialog(std::string id, int commonMs, bool recording, std::string recordNote)
    : HmiCyclicOverlay(id, true), commonMs_(commonMs), record_(recording), recordNote_(std::move(recordNote)) {
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", id + ".annuler")));
    other_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ailleurs\xE2\x80\xA6", id + ".ailleurs")));
    ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Exporter", id + ".exporter")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { (void)accept(); });
    links_ += cancel_->clicked->connect([this] { close(); });
    links_ += other_->clicked->connect([this] {
        auto cb = elsewhere;
        if (!cb) return;
        close();
        cb();
    });
}

void HmiCyclicExportDialog::setOneFile(bool on) {
    oneFile_ = on;
    if (changed) changed();
    invalidate();
}

void HmiCyclicExportDialog::setRaw(bool on) {
    raw_ = on;
    if (changed) changed();
    invalidate();
}

void HmiCyclicExportDialog::setRecord(bool on) {
    record_ = on;
    invalidate();
}

void HmiCyclicExportDialog::setPreview(std::string text) {
    preview_ = std::move(text);
    invalidate();
}

bool HmiCyclicExportDialog::accept() {
    auto cb = exported;
    const bool one = oneFile_, raw = raw_, rec = record_;
    close();
    if (cb) cb(one, raw, rec);
    return true;
}

void HmiCyclicExportDialog::placePanel(const gfx::Rect& host) {
    const float w = std::min(880.f, host.w - 40), h = std::min(640.f, host.h - 30);
    panel_ = {host.x + (host.w - w) / 2, host.y + (host.h - h) / 2, w, h};
    const float by = panel_.y + panel_.h - 44;
    ok_->setBounds({panel_.x + panel_.w - 14 - 130, by, 130, 32});
    cancel_->setBounds({panel_.x + panel_.w - 14 - 130 - 10 - 110, by, 110, 32});
    other_->setBounds({panel_.x + panel_.w - 14 - 130 - 10 - 110 - 10 - 130, by, 130, 32});
    other_->setVisibility(elsewhere ? ui::Visibility::Visible : ui::Visibility::Collapsed);
}

void HmiCyclicExportDialog::onLayout() { placePanel(bounds()); }

void HmiCyclicExportDialog::paintPanel(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto body = paintFrame(ctx, "Exporter la lecture cyclique", GExport, 54);
    float y = body.y;
    const auto label = [&](const std::string& t) {
        boldText(r, {body.x, y}, upperAscii(t), kTiny, c.textMuted);
        y += 22;
    };
    const auto radio = [&](int index, bool on, const std::string& title, const std::string& detail) {
        const gfx::Rect box{body.x, y, body.w, 42};
        radios_[index] = box;
        circle(r, {box.x + 8, box.y + 10}, 6.5f, on ? c.accent : c.border, false);
        if (on) circle(r, {box.x + 8, box.y + 10}, 3.2f, c.accent);
        r.drawText({box.x + 24, box.y + 1}, title, kUi, c.text);
        r.drawText({box.x + 24, box.y + 21}, fitted(r, detail, kTiny, box.w - 30), kTiny, c.textMuted);
        y += 46;
    };
    label("Contenu");
    radio(0, oneFile_, "Toutes les requ\xC3\xAAtes dans un fichier",
          "une ligne par tour (" + std::to_string(commonMs_) + " ms), une colonne par valeur ; une requ\xC3\xAAte plus lente garde sa derni\xC3\xA8re valeur");
    radio(1, !oneFile_, "Un fichier par requ\xC3\xAAte", "comme avant : l'heure, la dur\xC3\xA9" "e, l'\xC3\xA9tat, puis ses valeurs");
    y += 4;
    label("Valeurs");
    radio(2, !raw_, "Dans le format de chaque valeur", "un DINT en entier sign\xC3\xA9, un REAL en flottant, un texte en texte ; virgule d\xC3\xA9" "cimale");
    radio(3, raw_, "Brutes", "les registres tels que lus : une colonne par registre (ou par bit)");
    y += 4;
    label("Aper\xC3\xA7u");
    const float boxH = 150;
    const gfx::Rect pv{body.x, y, body.w, boxH};
    r.fillRoundedRect(pv, c.inputBg, 5);
    r.strokeRect(pv, c.border, 1.f);
    r.pushClip(pv);
    float py = pv.y + 8;
    const gfx::FontId mono = ctx.theme.font.mono;
    const gfx::FontId pf{static_cast<std::uint16_t>(std::min<int>(mono.v, 13))};
    std::size_t from = 0;
    if (preview_.rfind("\xEF\xBB\xBF", 0) == 0) from = 3;
    while (from < preview_.size() && py < pv.y + pv.h - 12) {
        const auto eol = preview_.find('\n', from);
        const std::string line = preview_.substr(from, eol == std::string::npos ? std::string::npos : eol - from);
        r.drawText({pv.x + 10, py}, fitted(r, line, pf, pv.w - 20), pf, c.text);
        py += r.lineHeight(pf) + 3;
        if (eol == std::string::npos) break;
        from = eol + 1;
    }
    r.popClip();
    y += boxH + 12;
    label("Et ensuite");
    recordBox_ = {body.x, y - 2, body.w, 22};
    drawCyclicGlyph(r, record_ ? GBoxOn : GBoxOff, {body.x, y, 16, 16}, record_ ? c.accent : c.textMuted);
    r.drawText({body.x + 24, y - 1}, "Enregistrer aussi en continu sur le disque, tant que la lecture tourne", kSmall, c.text);
    r.drawText({body.x + 24, y + 20}, fitted(r, recordNote_, kTiny, body.w - 30), kTiny, c.textMuted);
    const std::string foot = "Dans exports/modbus/ du projet, ou ailleurs avec \xC2\xAB Ailleurs\xE2\x80\xA6 \xC2\xBB (comme les autres exports)";
    r.drawText({panel_.x + 16, panel_.y + panel_.h - 36}, fitted(r, foot, kTiny, panel_.w - 560), kTiny, c.textMuted);
}

ui::EventResult HmiCyclicExportDialog::panelEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        if (radios_[0].contains(d->pos)) setOneFile(true);
        else if (radios_[1].contains(d->pos)) setOneFile(false);
        else if (radios_[2].contains(d->pos)) setRaw(false);
        else if (radios_[3].contains(d->pos)) setRaw(true);
        else if (recordBox_.contains(d->pos)) setRecord(!record_);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Return) {
        (void)accept();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// =========================================================== sous la grille ===
HmiCyclicSide::HmiCyclicSide(std::string id) : ui::Widget(id) {
    resume_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Reprendre", id + ".reprendre")));
    resume_->setStyle(ui::Button::Style::Primary);
    map_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Carte m\xC3\xA9moire", id + ".carte")));
    detect_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("D\xC3\xA9tecter les zones", id + ".detecter")));
    for (auto* b : {resume_, map_, detect_}) b->setVisibility(ui::Visibility::Collapsed);
}

void HmiCyclicSide::setHelp(std::string title, std::string text, bool resume, bool map, bool detect) {
    const bool same = title == helpTitle_ && text == helpText_ && resume_->visible() == resume && map_->visible() == map && detect_->visible() == detect;
    helpTitle_ = std::move(title);
    helpText_ = std::move(text);
    resume_->setVisibility(resume && !helpTitle_.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    map_->setVisibility(map && !helpTitle_.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    detect_->setVisibility(detect && !helpTitle_.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (!same) {
        invalidateLayout();
        invalidate();
    }
}

void HmiCyclicSide::setNote(std::string title, std::string text) {
    if (title == noteTitle_ && text == noteText_) return;
    noteTitle_ = std::move(title);
    noteText_ = std::move(text);
    invalidateLayout();
    invalidate();
}

namespace {
// Les boutons de la boite d'aide, a la file entre `left` et `right` ; celui qui
// ne tient plus passe a la ligne (a 1920 x 1080, "Detecter les zones" sortait
// de la boite rouge). Rend le nombre de lignes ; `place` recoit chaque case.
int placeButtons(const std::vector<ui::Button*>& buttons, float left, float right, float top, std::vector<gfx::Rect>* place) {
    float x = left, y = top;
    int rows = 0;
    for (auto* btn : buttons) {
        if (!btn->visible()) continue;
        const float w = ui::measureWidth(btn->text(), gfx::FontId{16}) + 24;
        if (rows == 0) rows = 1;
        else if (x + w > right) {
            x = left;
            y += 38;
            ++rows;
        }
        if (place) place->push_back({x, y, w, 30});
        x += w + 8;
    }
    return rows;
}
float helpHeight(const std::string& title, const std::string& text, float width, int buttonRows) {
    if (title.empty()) return 0;
    const auto lines = wrap(title + " " + text, kSmall, width - 24);
    return 16 + static_cast<float>(lines.size()) * 19 + (buttonRows > 0 ? 44 + static_cast<float>(buttonRows - 1) * 38 : 4);
}
float noteHeight(const std::string& title, const std::string& text, float width) {
    if (title.empty()) return 0;
    const auto lines = wrap(title + " " + text, kSmall, width - 24);
    return 16 + static_cast<float>(lines.size()) * 19 + 4;
}
} // namespace

float HmiCyclicSide::heightFor(float width) const {
    const int rows = placeButtons({resume_, map_, detect_}, 20, width - 20, 0, nullptr);
    const float h = helpHeight(helpTitle_, helpText_, width - 16, rows), n = noteHeight(noteTitle_, noteText_, width - 16);
    return h + n + (h > 0 ? 8 : 0) + (n > 0 ? 8 : 0) + ((h > 0 || n > 0) ? 4 : 0);
}

void HmiCyclicSide::onLayout() {
    const auto b = bounds();
    const std::vector<ui::Button*> buttons = {resume_, map_, detect_};
    const int rows = placeButtons(buttons, b.x + 20, b.x + b.w - 20, 0, nullptr);
    const float h = helpHeight(helpTitle_, helpText_, b.w - 16, rows);
    std::vector<gfx::Rect> boxes;
    (void)placeButtons(buttons, b.x + 20, b.x + b.w - 20, b.y + 8 + h - 40 - static_cast<float>(std::max(0, rows - 1)) * 38, &boxes);
    std::size_t i = 0;
    for (auto* btn : buttons)
        if (btn->visible() && i < boxes.size()) btn->setBounds(boxes[i++]);
}

void HmiCyclicSide::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    r.fillRect(b, c.panelBg);
    float y = b.y + 8;
    const int buttons = placeButtons({resume_, map_, detect_}, b.x + 20, b.x + b.w - 20, 0, nullptr);
    const auto paragraph = [&](float top, const std::string& title, const std::string& text, gfx::Color titleColor, float width) {
        float ty = top + 9;
        bool first = true;
        for (const auto& line : wrap(title + " " + text, kSmall, width - 24)) {
            if (first && line.rfind(title, 0) == 0) {
                boldText(r, {b.x + 20, ty}, title, kSmall, titleColor);
                r.drawText({b.x + 20 + r.measure(title + " ", kSmall).width, ty}, line.substr(std::min(line.size(), title.size() + 1)), kSmall, c.text);
            } else {
                r.drawText({b.x + 20, ty}, line, kSmall, c.text);
            }
            first = false;
            ty += 19;
        }
    };
    if (!helpTitle_.empty()) {
        const float h = helpHeight(helpTitle_, helpText_, b.w - 16, buttons);
        const gfx::Rect box{b.x + 8, y, b.w - 16, h};
        r.fillRoundedRect(box, gfx::Color::rgb(0x3E1C1F), 6);
        r.strokeRect(box, gfx::Color::rgb(0x6A2A2D), 1.f);
        paragraph(y, helpTitle_, helpText_, gfx::Color::rgb(0xFF8F8A), box.w);
        y += h + 8;
    }
    if (!noteTitle_.empty()) {
        const float h = noteHeight(noteTitle_, noteText_, b.w - 16);
        const gfx::Rect box{b.x + 8, y, b.w - 16, h};
        r.fillRoundedRect(box, simVioletBg(), 6);
        r.strokeRect(box, simViolet(), 1.f);
        paragraph(y, noteTitle_, noteText_, gfx::Color::rgb(0xC4B8FF), box.w);
    }
}

} // namespace app
