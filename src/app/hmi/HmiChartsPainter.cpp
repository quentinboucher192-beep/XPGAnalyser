// =============================================================================
//  app/hmi/HmiChartsPainter.cpp - le dessin des graphiques (lot 11)
// -----------------------------------------------------------------------------
//  Barres, courbe XY, chronogramme d'etats, camembert (anneau), radar,
//  histogramme : un cadre, un titre (texte a trous), une zone de trace avec ses
//  graduations. En marche, les valeurs viennent de "liveValues" (barres,
//  camembert, radar) ou des mesures du moteur (Runtime::chartSeries) ; dans
//  l'editeur, un apercu realiste montre les couleurs, les seuils et la mise en
//  page avant la premiere mesure.
// =============================================================================
#include "HmiPaintKit.hpp"

#include "../../hmi/HmiCharts.hpp"
#include "../../hmi/HmiControls.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiWidgets.hpp"

#include <cmath>
#include <optional>
#include <tuple>

namespace app::paint {

namespace {

constexpr double kPi = 3.14159265358979323846;
using hmi::Kind;
using Pts = std::vector<gfx::Point>;

gfx::Point P(double x, double y) { return {static_cast<float>(x), static_cast<float>(y)}; }
gfx::Color withA(gfx::Color c, float a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(c.a * a, 0.f, 255.f))); }

void fillLocal(const Ctx& c, const Pts& pts, gfx::Color col) {
    if (col.a && pts.size() >= 3) shapes::fillPolygon(c.r, c.map(pts), col);
}
void lineLocal(const Ctx& c, double x0, double y0, double x1, double y1, gfx::Color col, float width = 1.f) {
    if (col.a) c.r.line(c.map(x0, y0), c.map(x1, y1), col, std::max(1.f, width * c.vp.zoom));
}
void polyLocal(const Ctx& c, const Pts& pts, bool closed, gfx::Color col, float width) {
    if (col.a && pts.size() >= 2) shapes::strokePolyline(c.r, c.map(pts), closed, col, std::max(1.f, width * c.vp.zoom));
}
// Un trait tirete, dans le repere de l'objet (6 pleins, 4 vides, a l'ecran).
void dashLocal(const Ctx& c, double x0, double y0, double x1, double y1, gfx::Color col, float width = 1.f) {
    const gfx::Point a = c.map(x0, y0), b = c.map(x1, y1);
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f || !col.a) return;
    for (float t = 0; t < len; t += 10.f) {
        const float t1 = std::min(len, t + 6.f);
        c.r.line({a.x + dx * t / len, a.y + dy * t / len}, {a.x + dx * t1 / len, a.y + dy * t1 / len}, col, std::max(1.f, width * c.vp.zoom));
    }
}
Pts box(double x, double y, double w, double h) {
    return {P(x, y), P(x + std::max(0.0, w), y), P(x + std::max(0.0, w), y + std::max(0.0, h)), P(x, y + std::max(0.0, h))};
}
Pts disc(double cx, double cy, double r, int n = 20) { return shapes::ellipse(P(cx, cy), static_cast<float>(r), static_cast<float>(r), n); }

gfx::Color colorOf(const Ctx& c, const std::string& spec, gfx::Color fallback = gfx::Color::rgb(0x4FA3FF)) {
    gfx::Color col;
    return fade(tryParseColor(spec, col) ? col : c.seen(fallback), c.alpha);
}

std::string number(const Ctx& c, double v, const char* fallbackFormat = "0.0") {
    std::string s = hmi::formatValue(sim::Value::real(v), c.src.text(c.o, "format", fallbackFormat));
    const std::string unit = c.src.text(c.o, "unit");
    return unit.empty() ? s : s + " " + unit;
}

std::string tickText(double v, double step) {
    const double r = step >= 1 ? std::round(v) : step >= 0.1 ? std::round(v * 10) / 10 : std::round(v * 100) / 100;
    return hmi::formatNumber(r);
}

// Le cadre, le titre ; rend le haut de la zone libre.
double frame(const Ctx& c) {
    fillAndStroke(c, rectLocal(c.w(), c.h(), 3));
    const std::string title = c.src.text(c.o, "text");
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 12), 7.0, 40.0);
    if (title.empty()) return 6;
    const double th = fs * 1.35 * 1.6;
    text(c, fitted(c, title, c.w() - 16, fs * 1.35), 8, 2, c.w() - 16, th, c.color("textColor", gfx::Color::rgb(0xC8D0DC)), fs * 1.35, "gauche");
    return th + 2;
}

double fontOf(const Ctx& c) { return std::clamp(c.src.number(c.o, "fontSize", 12), 7.0, 40.0); }
gfx::Color labelColor(const Ctx& c) { return c.color("textColor", gfx::Color::rgb(0x9AA6B8)); }

// Les valeurs d'une liste : en marche, celles evaluees ; dans l'editeur, un apercu.
std::vector<std::optional<double>> listValues(const Ctx& c, std::size_t n, const char* liveKey, double lo, double hi, int salt) {
    if (!c.opt.editor) {
        auto v = hmi::liveNumbers(c.src.text(c.o, liveKey));
        v.resize(n);
        return v;
    }
    std::vector<std::optional<double>> out;
    for (std::size_t i = 0; i < n; ++i) {
        const double f = 0.35 + 0.45 * (0.5 + 0.5 * std::sin(static_cast<double>(i) * 1.7 + salt));
        out.emplace_back(lo + (hi - lo) * f);
    }
    return out;
}

void emptyNote(const Ctx& c, double top, const std::string& what) {
    text(c, what, 0, top, c.w(), c.h() - top, c.fixedText(0x8A96A8), std::max(11.0, fontOf(c)), "centre", true);
}

std::optional<double> optionalNumber(const Ctx& c, const char* key) {
    double v = 0;
    const std::string t = c.src.text(c.o, key);
    if (t.empty() || !hmi::parseNumber(t, v)) return std::nullopt;
    return v;
}

// ============================================================ barres ======
void drawBarChart(const Ctx& c) {
    const double top = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const auto items = hmi::chartItems(c.o);
    if (items.empty()) { emptyNote(c, top, "Graphique en barres : aucune valeur (Valeurs)"); return; }
    double lo = c.src.number(c.o, "min", 0), hi = c.src.number(c.o, "max", 100);
    const auto values = listValues(c, items.size(), "liveValues", lo, hi, 1);
    if (c.src.text(c.o, "scale", "fixe") == "auto") {
        std::vector<double> vs;
        for (const auto& v : values) if (v) vs.push_back(*v);
        std::tie(lo, hi) = hmi::autoRange(vs, lo, hi, true);
    }
    if (hi <= lo) hi = lo + 1;
    const bool horizontal = c.src.text(c.o, "orientation", "verticale") == "horizontale";
    const auto low = optionalNumber(c, "low"), high = optionalNumber(c, "high");
    const gfx::Color alarm = c.color("colorAlarm", gfx::Color::rgb(0xE5534B));
    const gfx::Color grid = c.fixed(0x323A47), axis = c.fixed(0x5A6678);
    const gfx::Color lab = labelColor(c), warn = c.fixedText(0xF2C94C);
    const bool showValue = c.src.flag(c.o, "showValue", true);
    const auto ticks = hmi::niceTicks(lo, hi, 5);
    const double step = ticks.size() >= 2 ? ticks[1] - ticks[0] : hi - lo;
    const std::size_t n = items.size();
    if (!horizontal) {
        const double left = 46, right = 10, bottom = fs * 2.2 + 4, pt = top + (showValue ? fs * 1.4 : 6);
        const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - pt - bottom);
        const auto Y = [&](double v) { return pt + (1 - (std::clamp(v, lo, hi) - lo) / (hi - lo)) * ph; };
        for (double t : ticks) {
            lineLocal(c, left, Y(t), left + pw, Y(t), grid);
            text(c, tickText(t, step), 0, Y(t) - 8, left - 5, 16, lab, fs * 0.9, "droite");
        }
        lineLocal(c, left, pt, left, pt + ph, axis);
        const double base = Y(std::clamp(0.0, lo, hi));
        lineLocal(c, left, base, left + pw, base, axis);
        const double slot = pw / static_cast<double>(n), bw = slot * 0.62;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = left + slot * static_cast<double>(i) + (slot - bw) / 2;
            const bool bad = values[i] && ((high && *values[i] > *high) || (low && *values[i] < *low));
            const gfx::Color col = bad ? alarm : colorOf(c, items[i].color);
            if (values[i]) {
                const double y = Y(*values[i]);
                fillLocal(c, box(x, std::min(y, base), bw, std::fabs(base - y)), col);
                if (showValue)
                    text(c, fitted(c, number(c, *values[i]), slot, fs), x - (slot - bw) / 2, std::min(y, base) - fs * 1.4, slot, fs * 1.4,
                         bad ? alarm : lab, fs, "centre");
            } else {
                text(c, "###", x - (slot - bw) / 2, base - fs * 1.5, slot, fs * 1.4, alarm, fs, "centre");
            }
            text(c, fitted(c, hmi::chartItemLabel(items[i]), slot - 2, fs), x - (slot - bw) / 2, pt + ph + 3, slot, fs * 1.6, lab, fs, "centre");
        }
        for (const auto& [lim, name] : {std::pair{low, "bas"}, std::pair{high, "haut"}}) {
            if (!lim || *lim < lo || *lim > hi) continue;
            dashLocal(c, left, Y(*lim), left + pw, Y(*lim), warn, 1.2f);
            text(c, name, left + pw - 40, Y(*lim) - fs * 1.3, 38, fs * 1.2, warn, fs * 0.85, "droite");
        }
        return;
    }
    // Horizontales : les noms a gauche, les barres vers la droite.
    double labelW = 0;
    for (const auto& it : items) labelW = std::max(labelW, c.r.measure(hmi::chartItemLabel(it), gfx::FontId{static_cast<std::uint16_t>(std::clamp(fs * c.vp.zoom, 6.0, 200.0))}).width / static_cast<double>(std::max(0.01f, c.vp.zoom)));
    labelW = std::clamp(labelW + 12, 40.0, w * 0.34);
    const double left = labelW, right = showValue ? fs * 5.5 : 12, bottom = fs * 1.8 + 4;
    const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - top - bottom - 4);
    const auto X = [&](double v) { return left + (std::clamp(v, lo, hi) - lo) / (hi - lo) * pw; };
    for (double t : ticks) {
        lineLocal(c, X(t), top + 2, X(t), top + 2 + ph, grid);
        text(c, tickText(t, step), X(t) - 30, top + 4 + ph, 60, fs * 1.6, lab, fs * 0.9, "centre");
    }
    const double base = X(std::clamp(0.0, lo, hi));
    lineLocal(c, base, top + 2, base, top + 2 + ph, axis);
    const double slot = ph / static_cast<double>(n), bh = slot * 0.62;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = top + 2 + slot * static_cast<double>(i) + (slot - bh) / 2;
        const bool bad = values[i] && ((high && *values[i] > *high) || (low && *values[i] < *low));
        const gfx::Color col = bad ? alarm : colorOf(c, items[i].color);
        text(c, fitted(c, hmi::chartItemLabel(items[i]), labelW - 10, fs), 2, y, labelW - 8, bh, lab, fs, "droite");
        if (!values[i]) { text(c, "###", base + 4, y, 60, bh, alarm, fs, "gauche"); continue; }
        const double x = X(*values[i]);
        fillLocal(c, box(std::min(x, base), y, std::fabs(x - base), bh), col);
        if (showValue) text(c, number(c, *values[i]), std::max(x, base) + 4, y, right + 40, bh, bad ? alarm : lab, fs, "gauche");
    }
    for (const auto& [lim, name] : {std::pair{low, "bas"}, std::pair{high, "haut"}}) {
        if (!lim || *lim < lo || *lim > hi) continue;
        dashLocal(c, X(*lim), top + 2, X(*lim), top + 2 + ph, warn, 1.2f);
        text(c, name, X(*lim) + 3, top + 2, 40, fs * 1.2, warn, fs * 0.85, "gauche");
    }
}

// ============================================================ camembert ===
Pts arcPts(double cx, double cy, double r, double fromDeg, double toDeg) {
    Pts out;
    const int steps = std::max(2, static_cast<int>(std::ceil((toDeg - fromDeg) / 4.0)));
    for (int k = 0; k <= steps; ++k) {
        const double a = (fromDeg + (toDeg - fromDeg) * k / steps) * kPi / 180.0;
        out.push_back(P(cx + r * std::sin(a), cy - r * std::cos(a)));
    }
    return out;
}

void drawLegend(const Ctx& c, const std::vector<hmi::ChartItem>& items, const std::vector<std::string>& extra, double x, double y,
                double w, double h, double fs) {
    const double rowH = fs * 1.6;
    const gfx::Color lab = labelColor(c);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const double ry = y + rowH * static_cast<double>(i);
        if (ry + rowH > y + h + 1) break;
        fillLocal(c, box(x, ry + rowH * 0.25, fs * 0.9, fs * 0.9), colorOf(c, items[i].color));
        std::string label = hmi::chartItemLabel(items[i]);
        if (i < extra.size() && !extra[i].empty()) label += "  " + extra[i];
        text(c, fitted(c, label, w - fs * 1.4, fs), x + fs * 1.3, ry, w - fs * 1.3, rowH, lab, fs, "gauche");
    }
}

void drawPieChart(const Ctx& c) {
    const double top = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const auto items = hmi::chartItems(c.o);
    if (items.empty()) { emptyNote(c, top, "Camembert : aucune valeur (Valeurs)"); return; }
    const auto values = listValues(c, items.size(), "liveValues", 0, 100, 3);
    std::vector<double> vs;
    for (const auto& v : values) vs.push_back(v ? *v : 0.0);
    const auto slices = hmi::pieSlices(vs);
    const bool legend = c.src.flag(c.o, "legend", true);
    const bool side = w >= h * 1.15;
    double pw = w - 12, ph = h - top - 8;
    double lx = 0, ly = 0, lw = 0, lh = 0;
    if (legend) {
        if (side) { lw = std::min(w * 0.42, 220.0); pw -= lw; lx = 6 + pw + 6; ly = top + 6; lh = ph; }
        else { lh = std::min(ph * 0.35, fs * 1.6 * static_cast<double>(items.size()) + 4); ph -= lh; lx = 12; ly = top + 4 + ph; lw = w - 24; }
    }
    const double r = std::max(8.0, std::min(pw, ph) / 2 - 4), cx = 6 + pw / 2, cy = top + 4 + ph / 2;
    const double hole = std::clamp(c.src.number(c.o, "hole", 0), 0.0, 90.0) / 100.0;
    const std::string mode = c.src.text(c.o, "labelMode", "pourcentage");
    std::vector<std::string> extras(items.size());
    if (slices.empty()) {
        fillLocal(c, disc(cx, cy, r, 48), c.fixed(0x2A313C));
        text(c, "aucune valeur", cx - r, cy - fs, 2 * r, 2 * fs, labelColor(c), fs, "centre");
    }
    for (std::size_t i = 0; i < slices.size(); ++i) {
        const auto& s = slices[i];
        if (s.to - s.from < 0.05) continue;
        Pts poly = arcPts(cx, cy, r, s.from, s.to);
        if (hole > 0) {
            Pts inner = arcPts(cx, cy, r * hole, s.from, s.to);
            poly.insert(poly.end(), inner.rbegin(), inner.rend());
        } else {
            poly.push_back(P(cx, cy));
        }
        fillLocal(c, poly, colorOf(c, items[i].color));
        polyLocal(c, poly, true, c.fixed(0x1B2028), 1.2f);
        const std::string pct = hmi::formatNumber(std::round(s.fraction * 1000) / 10) + " %";
        const std::string val = number(c, s.value, "0");
        std::string label = mode == "valeur" ? val : mode == "les deux" ? val + " (" + pct + ")" : mode == "aucune" ? std::string{} : pct;
        extras[i] = mode == "aucune" ? std::string{} : label;
        if (!label.empty() && s.fraction >= 0.05) {
            const double mid = (s.from + s.to) / 2 * kPi / 180.0, rr = hole > 0 ? r * (1 + hole) / 2 : r * 0.64;
            const double tx = cx + rr * std::sin(mid), ty = cy - rr * std::cos(mid);
            text(c, label, tx - 40, ty - fs, 80, 2 * fs, c.fixedOn(0xFFFFFF), fs, "centre");
        }
    }
    if (hole > 0.25 && !slices.empty()) {
        double sum = 0;
        for (double v : vs) sum += std::max(0.0, v);
        text(c, number(c, sum, "0"), cx - r * hole, cy - fs * 1.1, 2 * r * hole, fs * 2.2, c.color("textColor", gfx::Color::rgb(0xE6EAF0)),
             std::min(fs * 1.6, r * hole * 0.5), "centre");
    }
    if (legend) drawLegend(c, items, {}, lx, ly, lw, lh, fs);
}

// ============================================================ radar =======
void drawRadarChart(const Ctx& c) {
    const double top = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const auto items = hmi::chartItems(c.o);
    if (items.empty()) { emptyNote(c, top, "Radar : aucun axe (Valeurs)"); return; }
    double lo = c.src.number(c.o, "min", 0), hi = c.src.number(c.o, "max", 100);
    if (hi <= lo) hi = lo + 1;
    const auto values = listValues(c, items.size(), "liveValues", lo, hi, 5);
    const auto refs = hmi::chartItems(c.o, "references");
    std::vector<std::optional<double>> refValues;
    if (!refs.empty()) {
        if (c.opt.editor) for (const auto& r : refs) { double v = 0; refValues.emplace_back(hmi::parseNumber(r.expression, v) ? std::optional<double>(v) : std::optional<double>(lo + (hi - lo) * 0.7)); }
        else { refValues = hmi::liveNumbers(c.src.text(c.o, "liveReferences")); }
        refValues.resize(items.size());
    }
    const std::size_t n = items.size();
    const double cx = w / 2, cy = top + (h - top) / 2 + 2;
    const double R = std::max(10.0, std::min(w / 2 - fs * 5.5, (h - top) / 2 - fs * 1.8));
    const int rings = static_cast<int>(std::clamp(c.src.number(c.o, "rings", 4), 1.0, 10.0));
    const gfx::Color grid = c.fixed(0x3A4556), lab = labelColor(c);
    const auto at = [&](std::size_t i, double f) {
        const double a = 2 * kPi * static_cast<double>(i) / static_cast<double>(n);
        return P(cx + R * f * std::sin(a), cy - R * f * std::cos(a));
    };
    for (int k = 1; k <= rings; ++k) {
        Pts ring;
        for (std::size_t i = 0; i < n; ++i) ring.push_back(at(i, static_cast<double>(k) / rings));
        polyLocal(c, ring, true, grid, 1.f);
        // La graduation, juste sous le cercle, a droite de l'axe du haut : elle ne
        // touche pas le nom de cet axe, ecrit au-dessus de la toile.
        text(c, hmi::formatNumber(std::round((lo + (hi - lo) * k / rings) * 10) / 10), cx + 3, cy - R * k / rings + 1, 60, fs * 1.2,
             c.fixed(0x6B7686), fs * 0.8, "gauche");
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto e = at(i, 1.0);
        lineLocal(c, cx, cy, e.x, e.y, grid);
        const auto l = at(i, 1.0 + (fs * 1.25) / R);
        const double tw = fs * 9;
        const std::string align = std::fabs(l.x - cx) < 4 ? "centre" : l.x > cx ? "gauche" : "droite";
        const double bx = align == "centre" ? l.x - tw / 2 : align == "gauche" ? l.x : l.x - tw;
        text(c, fitted(c, hmi::chartItemLabel(items[i]), tw, fs), bx, l.y - fs * 0.8, tw, fs * 1.6, lab, fs, align);
    }
    const auto polygon = [&](const std::vector<std::optional<double>>& vals) {
        Pts poly;
        for (std::size_t i = 0; i < n; ++i) {
            const double v = i < vals.size() && vals[i] ? *vals[i] : lo;
            poly.push_back(at(i, std::clamp((v - lo) / (hi - lo), 0.0, 1.0)));
        }
        return poly;
    };
    if (!refs.empty()) {
        const auto poly = polygon(refValues);
        const gfx::Color rc = c.color("referenceColor", gfx::Color::rgb(0xF2C94C));
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1) % poly.size()];
            dashLocal(c, a.x, a.y, b.x, b.y, rc, 1.4f);
        }
    }
    const gfx::Color col = colorOf(c, items.front().color);
    const auto poly = polygon(values);
    fillLocal(c, poly, withA(col, 0.28f));
    polyLocal(c, poly, true, col, 2.f);
    const bool showValue = c.src.flag(c.o, "showValue", false);
    for (std::size_t i = 0; i < n; ++i) {
        fillLocal(c, disc(poly[i].x, poly[i].y, 3.2), col);
        if (showValue && values[i]) text(c, hmi::formatNumber(std::round(*values[i] * 10) / 10), poly[i].x + 4, poly[i].y - fs * 1.4, 60, fs * 1.3, lab, fs * 0.9, "gauche");
    }
}

// ============================================================ courbe XY ===
void drawXYChart(const Ctx& c, const hmi::View& view) {
    const double top0 = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const auto items = hmi::chartItems(c.o);
    std::vector<std::vector<std::pair<double, double>>> series(items.size());
    if (!c.opt.editor && c.opt.runtime) {
        if (const auto* s = c.opt.runtime->chartSeries(view.id, c.o.id))
            for (std::size_t i = 0; i < s->size() && i < series.size(); ++i) series[i].assign((*s)[i].points.begin(), (*s)[i].points.end());
    } else if (c.opt.editor) {
        // L'apercu : une courbe de pompe (la hauteur baisse avec le debit).
        const double x0 = c.src.number(c.o, "xmin", 0), x1 = c.src.number(c.o, "xmax", 100);
        const double y0 = c.src.number(c.o, "ymin", 0), y1 = c.src.number(c.o, "ymax", 100);
        for (std::size_t i = 0; i < series.size(); ++i)
            for (int k = 0; k <= 30; ++k) {
                const double f = k / 30.0;
                series[i].emplace_back(x0 + (x1 - x0) * f * 0.9, y0 + (y1 - y0) * (0.85 - 0.55 * f * f - 0.12 * static_cast<double>(i)));
            }
    }
    std::vector<std::pair<double, double>> reference;
    (void)hmi::parseXYPoints(c.src.text(c.o, "reference"), reference);
    double x0 = c.src.number(c.o, "xmin", 0), x1 = c.src.number(c.o, "xmax", 100);
    double y0 = c.src.number(c.o, "ymin", 0), y1 = c.src.number(c.o, "ymax", 100);
    const double tol = std::max(0.0, c.src.number(c.o, "tolerance", 0));
    if (c.src.text(c.o, "scale", "fixe") == "auto") {
        std::vector<double> xs, ys;
        for (const auto& s : series) for (const auto& [x, y] : s) { xs.push_back(x); ys.push_back(y); }
        for (const auto& [x, y] : reference) { xs.push_back(x); ys.push_back(y + tol); ys.push_back(y - tol); }
        std::tie(x0, x1) = hmi::autoRange(xs, x0, x1);
        std::tie(y0, y1) = hmi::autoRange(ys, y0, y1);
    }
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    const bool legend = c.src.flag(c.o, "legend", true) && !items.empty();
    const double top = top0 + (legend ? fs * 1.6 : 4);
    const std::string xLabel = c.src.text(c.o, "xLabel"), yLabel = c.src.text(c.o, "yLabel");
    const double left = 50, right = 12, bottom = fs * 1.6 + (xLabel.empty() ? 6 : fs * 1.6 + 4);
    const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - top - bottom - (yLabel.empty() ? 0 : fs * 1.4));
    const double pt = top + (yLabel.empty() ? 0 : fs * 1.4);
    const auto X = [&](double x) { return left + (std::clamp(x, x0, x1) - x0) / (x1 - x0) * pw; };
    const auto Y = [&](double y) { return pt + (1 - (std::clamp(y, y0, y1) - y0) / (y1 - y0)) * ph; };
    const gfx::Color grid = c.fixed(0x323A47), axis = c.fixed(0x5A6678), lab = labelColor(c);
    const auto xt = hmi::niceTicks(x0, x1, 6), yt = hmi::niceTicks(y0, y1, 5);
    const double xs = xt.size() >= 2 ? xt[1] - xt[0] : x1 - x0, ys = yt.size() >= 2 ? yt[1] - yt[0] : y1 - y0;
    for (double t : yt) {
        lineLocal(c, left, Y(t), left + pw, Y(t), grid);
        text(c, tickText(t, ys), 0, Y(t) - 8, left - 5, 16, lab, fs * 0.9, "droite");
    }
    for (double t : xt) {
        lineLocal(c, X(t), pt, X(t), pt + ph, grid);
        text(c, tickText(t, xs), X(t) - 30, pt + ph + 2, 60, fs * 1.5, lab, fs * 0.9, "centre");
    }
    lineLocal(c, left, pt, left, pt + ph, axis);
    lineLocal(c, left, pt + ph, left + pw, pt + ph, axis);
    if (!xLabel.empty()) text(c, xLabel, left, pt + ph + fs * 1.5, pw, fs * 1.6, lab, fs, "centre");
    if (!yLabel.empty()) text(c, yLabel, 4, top, pw, fs * 1.4, lab, fs, "gauche");
    // La reference et sa tolerance.
    if (!reference.empty()) {
        const gfx::Color rc = c.color("referenceColor", gfx::Color::rgb(0x9AA6B8));
        if (tol > 0 && reference.size() >= 2) {
            Pts band;
            for (const auto& [x, y] : reference) band.push_back(P(X(x), Y(y + tol)));
            for (auto it = reference.rbegin(); it != reference.rend(); ++it) band.push_back(P(X(it->first), Y(it->second - tol)));
            fillLocal(c, band, withA(rc, 0.16f));
        }
        Pts ref;
        for (const auto& [x, y] : reference) ref.push_back(P(X(x), Y(y)));
        for (std::size_t k = 1; k < ref.size(); ++k) dashLocal(c, ref[k - 1].x, ref[k - 1].y, ref[k].x, ref[k].y, rc, 1.5f);
    }
    const bool dots = c.src.text(c.o, "interpolation", "lin\xC3\xA9" "aire") == "points";
    for (std::size_t i = 0; i < series.size(); ++i) {
        if (series[i].empty()) continue;
        const gfx::Color col = colorOf(c, items[i].color);
        Pts pts;
        for (const auto& [x, y] : series[i]) pts.push_back(P(X(x), Y(y)));
        if (dots) for (const auto& p : pts) fillLocal(c, disc(p.x, p.y, 2.2, 10), col);
        else polyLocal(c, pts, false, col, 1.8f);
        // Le point du moment : plus gros, entoure.
        const auto& last = pts.back();
        fillLocal(c, disc(last.x, last.y, 5, 16), col);
        polyLocal(c, disc(last.x, last.y, 7, 16), true, c.fixed(0xFFFFFF), 1.2f);
        if (!c.opt.editor) {
            const auto& [lx, ly] = series[i].back();
            text(c, hmi::formatNumber(std::round(lx * 10) / 10) + " ; " + hmi::formatNumber(std::round(ly * 10) / 10), last.x + 9, last.y - fs * 1.6,
                 120, fs * 1.4, lab, fs * 0.9, "gauche");
        }
    }
    if (legend) {
        double x = left;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string name = hmi::chartItemLabel(items[i]);
            const double tw = std::min(w - x - 8, 28.0 + static_cast<double>(name.size()) * fs * 0.55);
            if (tw < 40) break;
            fillLocal(c, box(x, top0 + fs * 0.35, fs * 0.9, fs * 0.9), colorOf(c, items[i].color));
            text(c, fitted(c, name, tw - fs * 1.4, fs), x + fs * 1.2, top0, tw - fs * 1.2, fs * 1.6, lab, fs, "gauche");
            x += tw + 8;
        }
    }
    if (items.empty()) emptyNote(c, top, "Courbe XY : aucune courbe (Valeurs Y) ni X (Variable X)");
    else if (!c.opt.editor && std::all_of(series.begin(), series.end(), [](const auto& s) { return s.empty(); }))
        text(c, "en attente des mesures", left, pt, pw, ph, lab, fs, "centre");
}

// ============================================================ chronogramme
void drawStateChart(const Ctx& c, const hmi::View& view) {
    const double top = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const auto items = hmi::chartItems(c.o);
    if (items.empty()) { emptyNote(c, top, "Chronogramme : aucune ligne (Valeurs)"); return; }
    const auto states = hmi::parseStateList(c.src.text(c.o, "stateList"));
    const double duration = std::max(1.0, c.src.number(c.o, "duration", 60));
    double end = duration;
    std::vector<std::deque<std::pair<double, double>>> rows(items.size());
    if (!c.opt.editor && c.opt.runtime) {
        end = c.opt.runtime->now();
        if (const auto* s = c.opt.runtime->chartSeries(view.id, c.o.id))
            for (std::size_t i = 0; i < s->size() && i < rows.size(); ++i) rows[i] = (*s)[i].points;
    } else {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const int nStates = std::max<int>(2, static_cast<int>(states.size()));
            double t = 0;
            int k = static_cast<int>(i);
            while (t < duration) {
                double v = 0;
                (void)hmi::parseNumber(states.empty() ? std::to_string(k % 2) : states[static_cast<std::size_t>(k % nStates)].match, v);
                rows[i].emplace_back(t, v);
                t += duration * (0.12 + 0.07 * ((k * 7 + static_cast<int>(i) * 3) % 4));
                ++k;
            }
        }
    }
    const double start = end - duration;
    const bool legend = c.src.flag(c.o, "legend", true) && !states.empty();
    double labelW = 0;
    for (const auto& it : items) labelW = std::max(labelW, static_cast<double>(hmi::chartItemLabel(it).size()) * fs * 0.58);
    labelW = std::clamp(labelW + 14, 50.0, w * 0.3);
    const double left = labelW, right = 10, bottom = fs * 1.6 + 4 + (legend ? fs * 1.8 : 0);
    const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - top - bottom - 2);
    const double rowH = ph / static_cast<double>(items.size());
    const gfx::Color grid = c.fixed(0x323A47), lab = labelColor(c);
    const auto X = [&](double t) { return left + (std::clamp(t, start, end) - start) / duration * pw; };
    for (int k = 0; k <= 4; ++k) {
        const double x = left + pw * k / 4.0;
        lineLocal(c, x, top + 2, x, top + 2 + ph, grid);
        const std::string label = k == 4 ? std::string(c.opt.editor ? "aper\xC3\xA7u" : "0 s") : "-" + hmi::formatNumber(std::round(duration * (4 - k) / 4.0)) + " s";
        // La premiere graduation commence au bord gauche du trace, la derniere
        // finit au bord droit : aucune ne deborde de l'objet.
        const double lx = k == 0 ? x : k == 4 ? x - 80 : x - 40;
        text(c, label, lx, top + 3 + ph, 80, fs * 1.5, lab, fs * 0.9, k == 0 ? "gauche" : k == 4 ? "droite" : "centre");
    }
    const bool showText = c.src.flag(c.o, "showText", true);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const double y = top + 2 + rowH * static_cast<double>(i);
        text(c, fitted(c, hmi::chartItemLabel(items[i]), left - 10, fs), 2, y, left - 8, rowH, lab, fs, "droite");
        lineLocal(c, left, y + rowH, left + pw, y + rowH, grid);
        const double bh = std::min(rowH * 0.62, fs * 2.2), by = y + (rowH - bh) / 2;
        for (const auto& seg : hmi::stateSegments(rows[i], start, end)) {
            const std::string shown = hmi::formatNumber(seg.value);
            const int k = hmi::stateIndexOf(states, shown);
            gfx::Color col = c.seen(seg.value != 0 ? gfx::Color::rgb(0x2ECC71) : gfx::Color::rgb(0x4A5261));
            std::string label = shown;
            if (k >= 0) {
                col = parseColor(states[static_cast<std::size_t>(k)].color, col);
                label = states[static_cast<std::size_t>(k)].text;
            }
            const double x0 = X(seg.from), x1 = X(seg.to);
            fillLocal(c, box(x0, by, std::max(1.0, x1 - x0), bh), fade(col, c.alpha));
            if (showText && x1 - x0 > fs * 3) text(c, fitted(c, label, x1 - x0 - 6, fs * 0.9), x0 + 3, by, x1 - x0 - 6, bh, c.fixedOn(0xFFFFFF), fs * 0.9, "gauche");
        }
    }
    if (legend) {
        double x = left;
        const double ly = h - fs * 1.9;
        for (const auto& st : states) {
            const double tw = 26.0 + static_cast<double>(st.text.size()) * fs * 0.55;
            if (x + tw > w - 6) break;
            fillLocal(c, box(x, ly + fs * 0.35, fs * 0.9, fs * 0.9), fade(parseColor(st.color, gfx::Color::rgb(0x4A5261)), c.alpha));
            text(c, st.text, x + fs * 1.2, ly, tw, fs * 1.6, lab, fs * 0.9, "gauche");
            x += tw + 8;
        }
    }
}

// ============================================================ histogramme =
void drawHistogram(const Ctx& c, const hmi::View& view) {
    const double top = frame(c);
    const double w = c.w(), h = c.h(), fs = fontOf(c);
    const double lo = c.src.number(c.o, "min", 0), hi0 = c.src.number(c.o, "max", 100), hi = hi0 > lo ? hi0 : lo + 1;
    const int bins = static_cast<int>(std::clamp(c.src.number(c.o, "bins", 10), 1.0, 200.0));
    const auto low = optionalNumber(c, "low"), high = optionalNumber(c, "high");
    std::vector<double> samples;
    if (!c.opt.editor && c.opt.runtime) {
        if (const auto* s = c.opt.runtime->chartSeries(view.id, c.o.id); s && !s->empty())
            for (const auto& p : (*s)[0].points) samples.push_back(p.second);
    } else if (c.opt.editor) {
        // L'apercu : une cloche (des mesures pseudo-aleatoires, toujours les memes).
        unsigned seed = 12345;
        const auto rnd = [&] { seed = seed * 1103515245u + 12345u; return static_cast<double>((seed >> 8) & 0xFFFF) / 65536.0; };
        for (int k = 0; k < 400; ++k) {
            const double u1 = std::max(1e-6, rnd()), u2 = rnd();
            const double z = std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
            samples.push_back(lo + (hi - lo) * (0.5 + 0.13 * z));
        }
    }
    const auto stats = hmi::histogram(samples, lo, hi, bins, low, high);
    const bool showStats = c.src.flag(c.o, "showStats", true);
    // Les statistiques : sur une ligne, ou sur deux quand l'objet est etroit
    // (le nombre, la moyenne, l'ecart type ; puis Cp, Cpk et les hors tolerance).
    std::vector<std::string> statLines;
    if (showStats) {
        std::string head = "n = " + std::to_string(stats.n), tail;
        if (stats.n > 0) {
            head += "   moyenne " + number(c, stats.mean) + "   \xCF\x83 " + hmi::formatNumber(std::round(stats.sigma * 100) / 100);
            if (stats.cp >= 0) tail += "Cp " + hmi::formatNumber(std::round(stats.cp * 100) / 100);
            if (stats.cpk > -1) tail += (tail.empty() ? "" : "   ") + std::string("Cpk ") + hmi::formatNumber(std::round(stats.cpk * 100) / 100);
            if (low || high) tail += (tail.empty() ? "" : "   ") + std::string("hors tol\xC3\xA9rance ") + std::to_string(stats.outOfTolerance);
        }
        if (c.opt.editor) head = "aper\xC3\xA7u  \xC2\xB7  " + head;
        const std::string one = tail.empty() ? head : head + "   " + tail;
        if (tail.empty() || fitted(c, one, w - 16, fs) == one) statLines = {one};
        else statLines = {head, tail};
    }
    const double statsH = showStats ? fs * 1.6 * static_cast<double>(statLines.size()) + 2 : 0;
    const double left = 40, right = 10, bottom = fs * 1.6 + 4 + statsH;
    const double pt = top + 4;
    const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - pt - bottom);
    const auto X = [&](double v) { return left + (std::clamp(v, lo, hi) - lo) / (hi - lo) * pw; };
    const double tallest = std::max<double>(1.0, static_cast<double>(stats.tallest()));
    const auto yt = hmi::niceTicks(0, tallest, 4);
    const gfx::Color grid = c.fixed(0x323A47), axis = c.fixed(0x5A6678), lab = labelColor(c);
    const double yMax = yt.empty() ? tallest : std::max(tallest, yt.back());
    const auto Y = [&](double n) { return pt + (1 - n / yMax) * ph; };
    for (double t : yt) {
        lineLocal(c, left, Y(t), left + pw, Y(t), grid);
        text(c, tickText(t, 1), 0, Y(t) - 8, left - 5, 16, lab, fs * 0.9, "droite");
    }
    const auto xt = hmi::niceTicks(lo, hi, 6);
    const double xs = xt.size() >= 2 ? xt[1] - xt[0] : hi - lo;
    for (double t : xt) text(c, tickText(t, xs), X(t) - 30, pt + ph + 2, 60, fs * 1.5, lab, fs * 0.9, "centre");
    const auto colorList = hmi::splitSemicolons(c.src.text(c.o, "colors", "#4FA3FF"));
    const gfx::Color barCol = colorOf(c, colorList.empty() || colorList.front().empty() ? std::string("#4FA3FF") : colorList.front());
    const gfx::Color bad = c.fixed(0xE5534B);
    const double step = (hi - lo) / bins;
    for (int k = 0; k < bins; ++k) {
        const double a = lo + step * k, b = a + step;
        const double n = static_cast<double>(stats.counts[static_cast<std::size_t>(k)]);
        if (n <= 0) continue;
        const bool outside = (low && b <= *low + 1e-12) || (high && a >= *high - 1e-12);
        fillLocal(c, box(X(a) + 1, Y(n), std::max(1.0, X(b) - X(a) - 2), pt + ph - Y(n)), outside ? withA(bad, 0.85f) : barCol);
    }
    lineLocal(c, left, pt + ph, left + pw, pt + ph, axis);
    for (const auto& [lim, name] : {std::pair{low, "LSL"}, std::pair{high, "USL"}}) {
        if (!lim || *lim < lo || *lim > hi) continue;
        dashLocal(c, X(*lim), pt, X(*lim), pt + ph, bad, 1.5f);
        text(c, name, X(*lim) + 3, pt, 40, fs * 1.3, bad, fs * 0.85, "gauche");
    }
    if (stats.n > 0 && stats.mean >= lo && stats.mean <= hi) {
        dashLocal(c, X(stats.mean), pt, X(stats.mean), pt + ph, c.fixed(0xE6EAF0), 1.2f);
    }
    for (std::size_t k = 0; k < statLines.size(); ++k)
        text(c, fitted(c, statLines[k], w - 16, fs), 8, h - statsH - 2 + fs * 1.6 * static_cast<double>(k), w - 16, fs * 1.6, lab, fs, "gauche");
    if (!c.opt.editor && stats.n == 0) text(c, c.src.text(c.o, "variable").empty() ? "Histogramme : aucune variable" : "en attente des mesures", left, pt, pw, ph, lab, fs, "centre");
}

} // namespace

bool drawCharts(const Ctx& c) {
    if (!hmi::kindIsChart(c.o.kind)) return false;
    if (c.w() <= 4 || c.h() <= 4) return true;
    switch (c.o.kind) {
        case Kind::BarChart:   drawBarChart(c); break;
        case Kind::PieChart:   drawPieChart(c); break;
        case Kind::RadarChart: drawRadarChart(c); break;
        case Kind::XYChart:    drawXYChart(c, c.view); break;
        case Kind::StateChart: drawStateChart(c, c.view); break;
        case Kind::Histogram:  drawHistogram(c, c.view); break;
        default:               return false;
    }
    return true;
}

} // namespace app::paint
