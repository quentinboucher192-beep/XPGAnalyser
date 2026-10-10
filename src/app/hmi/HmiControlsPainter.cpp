// =============================================================================
//  app/hmi/HmiControlsPainter.cpp - le dessin des commandes et des afficheurs
//                                    (lot 9)
// -----------------------------------------------------------------------------
//  Meme regle que le reste du dessin : un seul dessin pour l'editeur et pour la
//  simulation. Les valeurs viennent de HmiPropertySource (statiques dans
//  l'editeur, evaluees en marche : "value" y porte deja le retour d'etat ou la
//  variable) ; ce qui n'est pas une propriete - l'impulsion enfoncee, la
//  poignee qu'on tire, la liste ouverte, le temps compte, la tendance, l'heure
//  - vient du moteur (HmiPaintOptions::runtime), absent dans l'editeur.
//
//  La geometrie des parties cliquables est celle de hmi/HmiControls : ce qui se
//  dessine ici est ce qui se clique.
// =============================================================================
#include "HmiPaintKit.hpp"

#include "../../hmi/HmiControls.hpp"
#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiQr.hpp"
#include "../../hmi/HmiRuntime.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

namespace app::paint {

namespace {

constexpr double kPi = 3.14159265358979323846;
using hmi::Kind;

double fsOf(const Ctx& c, double fallback) { return std::clamp(c.src.number(c.o, "fontSize", fallback), 6.0, 120.0); }
gfx::Color colorOf(const Ctx& c, std::string_view key, std::uint32_t fallback) { return c.color(key, gfx::Color::rgb(fallback)); }
gfx::Color hexColor(const Ctx& c, std::string_view hex, std::uint32_t fallback) {
    return fade(parseColor(hex, gfx::Color::rgb(fallback)), c.alpha);
}
gfx::Color withAlpha(gfx::Color col, float a) { return col.withAlpha(static_cast<std::uint8_t>(std::clamp(col.a * a, 0.f, 255.f))); }
gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto m = [&](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), a.a};
}

std::vector<gfx::Point> boxLocal(const hmi::Box& b, double radius = 0) {
    return shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h),
                               static_cast<float>(radius));
}
std::vector<gfx::Point> circleLocal(double cx, double cy, double r, int segments = 40) {
    return shapes::ellipse({static_cast<float>(cx), static_cast<float>(cy)}, static_cast<float>(r), static_cast<float>(r), segments);
}
void fillLocal(const Ctx& c, const std::vector<gfx::Point>& local, gfx::Color col) {
    if (col.a) shapes::fillPolygon(c.r, c.map(local), col);
}
void strokeLocal(const Ctx& c, const std::vector<gfx::Point>& local, bool closed, gfx::Color col, double width = 1) {
    if (col.a) shapes::strokePolyline(c.r, c.map(local), closed, col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
void lineLocal(const Ctx& c, double x0, double y0, double x1, double y1, gfx::Color col, double width = 1) {
    if (col.a) c.r.line(c.map(x0, y0), c.map(x1, y1), col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
// Une bande d'arc (en degres ecran : 0 a droite, sens horaire).
void arcBand(const Ctx& c, double cx, double cy, double rIn, double rOut, double from, double to, gfx::Color col, int seg = 40) {
    if (!col.a || to <= from) return;
    const auto outer = shapes::arc({static_cast<float>(cx), static_cast<float>(cy)}, static_cast<float>(rOut), static_cast<float>(from),
                                   static_cast<float>(to), seg);
    const auto inner = shapes::arc({static_cast<float>(cx), static_cast<float>(cy)}, static_cast<float>(rIn), static_cast<float>(from),
                                   static_cast<float>(to), seg);
    std::vector<gfx::Point> poly = outer;
    poly.insert(poly.end(), inner.rbegin(), inner.rend());
    fillLocal(c, poly, col);
}
void triangle(const Ctx& c, double x0, double y0, double x1, double y1, double x2, double y2, gfx::Color col) {
    fillLocal(c, {{static_cast<float>(x0), static_cast<float>(y0)}, {static_cast<float>(x1), static_cast<float>(y1)},
                  {static_cast<float>(x2), static_cast<float>(y2)}},
              col);
}
double measureLocal(const Ctx& c, const std::string& s, double sizePx) {
    const auto px = static_cast<std::uint16_t>(std::clamp(sizePx * c.vp.zoom, 6.0, 200.0));
    return c.r.measure(s, gfx::FontId{px}).width / c.vp.zoom;
}

std::string value(const Ctx& c) { return c.src.text(c.o, "value"); }
bool valueNumber(const Ctx& c, double& out) { return hmi::parseNumber(value(c), out); }
bool valueTruthy(const Ctx& c) {
    const std::string v = value(c);
    double x = 0;
    if (hmi::parseNumber(v, x)) return x != 0;
    return hmi::parseBool(v, false);
}
bool badQuality(const Ctx& c) { return !c.opt.editor && c.src.flag(c.o, "quality_bad", false); }
// La valeur mise en forme ("0.0" : une decimale) et son unite.
std::string formatted(const Ctx& c, double v) {
    std::string s = hmi::formatValue(sim::Value::real(v), c.src.text(c.o, "format", "0"));
    const std::string unit = c.src.text(c.o, "unit");
    return unit.empty() ? s : s + " " + unit;
}
bool blinkOff(const Ctx& c) { return !c.opt.editor && std::fmod(c.opt.time, 1.0) >= 0.5; }
bool pressedNow(const Ctx& c) { return c.opt.runtime && !c.opt.editor && c.opt.runtime->pressed() == c.o.id; }

// Le texte d'une graduation : court (12, 12.5, -20).
std::string tickLabel(double v) {
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-6) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// ---------------------------------------------------------------- commandes ----
void drawPushButton(const Ctx& c) {
    const double w = c.w(), h = c.h(), rad = c.src.number(c.o, "radius", 4);
    const bool down = pressedNow(c);
    const auto shape = rectLocal(w, h, rad);
    fillLocal(c, shape, down ? colorOf(c, "pressedColor", 0x173F80) : colorOf(c, "fill", 0x2F6FD6));
    if (!down) fillLocal(c, boxLocal({2, 2, w - 4, h * 0.45}, rad), fade(gfx::Color{255, 255, 255, 22}, c.alpha));
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x1D4FA3), std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
    // Une impulsion : un petit creneau a gauche.
    const double g = std::min(h * 0.32, 14.0);
    if (w > g * 5) {
        const double x0 = 8, y0 = h / 2 + g / 2, y1 = h / 2 - g / 2;
        const gfx::Color gc = withAlpha(colorOf(c, "textColor", 0xFFFFFF), 0.7f);
        strokeLocal(c, {{static_cast<float>(x0), static_cast<float>(y0)}, {static_cast<float>(x0 + g * 0.4), static_cast<float>(y0)},
                        {static_cast<float>(x0 + g * 0.4), static_cast<float>(y1)}, {static_cast<float>(x0 + g), static_cast<float>(y1)},
                        {static_cast<float>(x0 + g), static_cast<float>(y0)}, {static_cast<float>(x0 + g * 1.4), static_cast<float>(y0)}},
                    false, gc, 1.4);
    }
    const double dy = down ? 1.5 : 0;
    text(c, c.src.text(c.o, "text"), 0, dy, w, h, colorOf(c, "textColor", 0xFFFFFF), fsOf(c, 16), "centre", c.src.flag(c.o, "wrap"));
}

void drawSwitch(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const bool on = valueTruthy(c);
    const double th = std::max(8.0, h * 0.66), tw = std::min(w * 0.55, th * 1.9);
    const double ty = (h - th) / 2;
    const auto track = boxLocal({0, ty, tw, th}, th / 2);
    fillLocal(c, track, on ? colorOf(c, "colorOn", 0x2ECC71) : colorOf(c, "colorOff", 0x4A5261));
    strokeLocal(c, track, true, fade(gfx::Color{0, 0, 0, 90}, c.alpha));
    const double kr = th / 2 - 3;
    const double kx = on ? tw - th / 2 : th / 2;
    fillLocal(c, circleLocal(kx + 1, h / 2 + 1.5, kr), fade(gfx::Color{0, 0, 0, 70}, c.alpha));
    fillLocal(c, circleLocal(kx, h / 2, kr), colorOf(c, "knobColor", 0xF4F6FA));
    // La commande differe du retour d'etat : une discordance, en orange.
    const std::string command = c.src.text(c.o, "command");
    if (!c.opt.editor && !command.empty() && hmi::parseBool(command, false) != on) {
        const auto ring = boxLocal({-3, ty - 3, tw + 6, th + 6}, th / 2 + 3);
        strokeLocal(c, ring, true, blinkOff(c) ? fade(gfx::Color{242, 153, 74, 90}, c.alpha) : c.fixed(0xF2994A), 2);
    }
    const std::string label = on ? c.src.text(c.o, "textOn") : c.src.text(c.o, "textOff");
    const double fs = fsOf(c, 15);
    if (!label.empty() && w - tw > 12) text(c, fitted(c, label, w - tw - 12, fs), tw + 6, 0, w - tw - 6, h, colorOf(c, "textColor", 0xE6EAF0), fs, "gauche");
}

void drawIlluminatedButton(const Ctx& c) {
    const double w = c.w(), h = c.h(), rad = c.src.number(c.o, "radius", 8);
    const bool lit = valueTruthy(c);
    const gfx::Color face = lit ? colorOf(c, "colorOn", 0x2ECC71) : colorOf(c, "colorOff", 0x3A4556);
    if (lit)   // le halo d'un voyant allume
        for (int k = 3; k >= 1; --k)
            fillLocal(c, boxLocal({-k * 2.0, -k * 2.0, w + k * 4.0, h + k * 4.0}, rad + k * 2.0), withAlpha(face, 0.10f));
    const auto shape = rectLocal(w, h, rad);
    fillLocal(c, shape, pressedNow(c) ? mix(face, gfx::Color{0, 0, 0, 255}, 0.25f) : face);
    // La calotte : un reflet en haut.
    fillLocal(c, boxLocal({4, 3, w - 8, h * 0.38}, std::max(2.0, rad - 2)), fade(gfx::Color{255, 255, 255, static_cast<std::uint8_t>(lit ? 60 : 25)}, c.alpha));
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x1B1F26), std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
    const gfx::Color txt = lit ? mix(colorOf(c, "textColor", 0xFFFFFF), gfx::Color{14, 18, 24, 255}, 0.9f) : colorOf(c, "textColor", 0xFFFFFF);
    text(c, c.src.text(c.o, "text"), 0, pressedNow(c) ? 1.5 : 0, w, h, txt, fsOf(c, 16), "centre", c.src.flag(c.o, "wrap"));
}

void drawSelector(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto choices = hmi::choicesOf(c.o);
    int idx = hmi::choiceIndexOf(choices, value(c));
    if (idx < 0 && c.opt.editor && !choices.empty()) idx = 0;
    const auto l = hmi::selectorLayout(c.o, w, h, choices.size());
    const double fs = fsOf(c, 14);
    const gfx::Color accent = colorOf(c, "accent", 0x2F6FD6), txt = colorOf(c, "textColor", 0xE6EAF0);
    const gfx::Color fill = colorOf(c, "fill", 0x2A313C), stroke = colorOf(c, "stroke", 0x4A5568);
    if (!l.rotary) {
        for (std::size_t i = 0; i < l.labels.size() && i < choices.size(); ++i) {
            const auto& b = l.labels[i];
            const bool on = static_cast<int>(i) == idx;
            const auto shape = boxLocal(b, 4);
            fillLocal(c, shape, on ? accent : fill);
            strokeLocal(c, shape, true, on ? accent : stroke);
            text(c, fitted(c, choices[i].label, b.w - 6, fs), b.x, b.y, b.w, b.h, on ? c.fixedOn(0xFFFFFF) : txt, fs, "centre");
        }
        return;
    }
    // Rotatif : les reperes, le bouton, son index.
    for (std::size_t i = 0; i < l.angles.size() && i < choices.size(); ++i) {
        const double a = l.angles[i] * kPi / 180.0;
        const bool on = static_cast<int>(i) == idx;
        lineLocal(c, l.cx + std::cos(a) * (l.radius + 2), l.cy + std::sin(a) * (l.radius + 2), l.cx + std::cos(a) * (l.radius + fs * 0.45),
                  l.cy + std::sin(a) * (l.radius + fs * 0.45), on ? accent : stroke, on ? 2.5 : 1.5);
        // La boite suit la largeur estimee du texte : on dessine autour de son
        // centre, avec du jeu (le texte n'est pas coupe pour quelques pixels).
        const auto& b = l.labels[i];
        text(c, fitted(c, choices[i].label, b.w + 24, fs), b.x - 12, b.y, b.w + 24, b.h, on ? accent : txt, fs, "centre");
    }
    fillLocal(c, circleLocal(l.cx + 1.5, l.cy + 2, l.radius), fade(gfx::Color{0, 0, 0, 80}, c.alpha));
    fillLocal(c, circleLocal(l.cx, l.cy, l.radius), fill);
    strokeLocal(c, circleLocal(l.cx, l.cy, l.radius), true, stroke, 1.5);
    const double a = (idx >= 0 && static_cast<std::size_t>(idx) < l.angles.size() ? l.angles[static_cast<std::size_t>(idx)] : 270.0) * kPi / 180.0;
    // La manette : une barre qui traverse le bouton, son bout vers la position.
    const double bw = l.radius * 0.34;
    const double ex = std::cos(a), ey = std::sin(a), nx = -ey, ny = ex;
    fillLocal(c, {{static_cast<float>(l.cx + ex * l.radius * 0.92 + nx * bw / 2), static_cast<float>(l.cy + ey * l.radius * 0.92 + ny * bw / 2)},
                  {static_cast<float>(l.cx + ex * l.radius * 0.92 - nx * bw / 2), static_cast<float>(l.cy + ey * l.radius * 0.92 - ny * bw / 2)},
                  {static_cast<float>(l.cx - ex * l.radius * 0.92 - nx * bw / 2), static_cast<float>(l.cy - ey * l.radius * 0.92 - ny * bw / 2)},
                  {static_cast<float>(l.cx - ex * l.radius * 0.92 + nx * bw / 2), static_cast<float>(l.cy - ey * l.radius * 0.92 + ny * bw / 2)}},
              mix(fill, gfx::Color{255, 255, 255, 255}, 0.18f));
    fillLocal(c, circleLocal(l.cx + ex * l.radius * 0.66, l.cy + ey * l.radius * 0.66, std::max(2.5, bw * 0.32)), accent);
}

void drawSlider(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto l = hmi::sliderLayout(c.o, w, h);
    const double mn = c.src.number(c.o, "min", 0), mx = c.src.number(c.o, "max", 100);
    double v = mn;
    const auto preview = c.opt.runtime && !c.opt.editor ? c.opt.runtime->dragPreview(c.o.id) : std::nullopt;
    if (preview) v = *preview;
    else if (!valueNumber(c, v)) v = mn;
    const double f = hmi::fractionOf(v, mn, mx);
    const gfx::Color accent = colorOf(c, "accent", 0x2F6FD6);
    fillLocal(c, boxLocal(l.track, 3), colorOf(c, "fill", 0x2A313C));
    double kx = 0, ky = 0;
    if (!l.vertical) {
        kx = l.track.x + l.track.w * f;
        ky = l.track.cy();
        fillLocal(c, boxLocal({l.track.x, l.track.y, l.track.w * f, l.track.h}, 3), accent);
    } else {
        kx = l.track.cx();
        ky = l.track.bottom() - l.track.h * f;
        fillLocal(c, boxLocal({l.track.x, ky, l.track.w, l.track.bottom() - ky}, 3), accent);
    }
    // Les graduations : "ticks" divisions (5 : un repere tous les 20 %), comme le cadran.
    const int ticks = static_cast<int>(std::clamp(c.src.number(c.o, "ticks", 5), 0.0, 50.0));
    const gfx::Color tickCol = c.fixed(0x6B7686);
    if (ticks >= 1)
        for (int k = 0; k <= ticks; ++k) {
            const double t = static_cast<double>(k) / ticks;
            if (!l.vertical) {
                const double x = l.track.x + l.track.w * t;
                lineLocal(c, x, l.track.bottom() + l.knob * 0.6, x, l.track.bottom() + l.knob * 0.6 + 5, tickCol);
            } else {
                const double y = l.track.bottom() - l.track.h * t;
                lineLocal(c, l.track.right() + l.knob * 0.6, y, l.track.right() + l.knob * 0.6 + 5, y, tickCol);
            }
        }
    fillLocal(c, circleLocal(kx + 1, ky + 1.5, l.knob), fade(gfx::Color{0, 0, 0, 80}, c.alpha));
    fillLocal(c, circleLocal(kx, ky, l.knob), colorOf(c, "knobColor", 0xF4F6FA));
    strokeLocal(c, circleLocal(kx, ky, l.knob), true, preview ? accent : fade(gfx::Color{0, 0, 0, 80}, c.alpha), preview ? 2.5 : 1.0);
    if (l.value.w > 0 && l.value.h > 0)
        text(c, formatted(c, v), l.value.x, l.value.y, l.value.w, l.value.h, colorOf(c, "textColor", 0xE6EAF0), fsOf(c, 13),
             l.vertical ? "centre" : "droite");
}

void drawKnob(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double cx = w / 2, cy = h / 2, R = std::max(8.0, std::min(w, h) / 2 - 4);
    const double mn = c.src.number(c.o, "min", 0), mx = c.src.number(c.o, "max", 100);
    double v = mn;
    const auto preview = c.opt.runtime && !c.opt.editor ? c.opt.runtime->dragPreview(c.o.id) : std::nullopt;
    if (preview) v = *preview;
    else if (!valueNumber(c, v)) v = mn;
    const double f = hmi::fractionOf(v, mn, mx);
    const gfx::Color accent = colorOf(c, "accent", 0x2F6FD6);
    arcBand(c, cx, cy, R * 0.80, R * 0.94, hmi::kKnobStartDeg, hmi::kKnobStartDeg + hmi::kKnobSweepDeg, colorOf(c, "fill", 0x2A313C), 60);
    arcBand(c, cx, cy, R * 0.80, R * 0.94, hmi::kKnobStartDeg, hmi::kKnobStartDeg + hmi::kKnobSweepDeg * f, accent, 60);
    const int ticks = static_cast<int>(std::clamp(c.src.number(c.o, "ticks", 10), 0.0, 60.0));
    if (ticks >= 1)
        for (int k = 0; k <= ticks; ++k) {
            const double a = (hmi::kKnobStartDeg + hmi::kKnobSweepDeg * k / ticks) * kPi / 180.0;
            lineLocal(c, cx + std::cos(a) * R * 0.97, cy + std::sin(a) * R * 0.97, cx + std::cos(a) * R, cy + std::sin(a) * R,
                      c.fixed(0x6B7686));
        }
    const bool showValue = c.src.flag(c.o, "showValue", true);
    const double kr = R * (showValue ? 0.58 : 0.66);
    const gfx::Color body = colorOf(c, "knobColor", 0xC8D0DC);
    const double ky = showValue ? cy - R * 0.06 : cy;
    fillLocal(c, circleLocal(cx + 1.5, ky + 2.5, kr), fade(gfx::Color{0, 0, 0, 90}, c.alpha));
    fillLocal(c, circleLocal(cx, ky, kr), mix(body, gfx::Color{30, 34, 42, 255}, 0.55f));
    fillLocal(c, circleLocal(cx, ky, kr * 0.86), mix(body, gfx::Color{30, 34, 42, 255}, 0.35f));
    const double a = (hmi::kKnobStartDeg + hmi::kKnobSweepDeg * f) * kPi / 180.0;
    lineLocal(c, cx + std::cos(a) * kr * 0.35, ky + std::sin(a) * kr * 0.35, cx + std::cos(a) * kr * 0.82, ky + std::sin(a) * kr * 0.82, accent, 3);
    // La valeur dans l'ouverture du bas de la course (entre 45 et 135 degres).
    if (showValue)
        text(c, fitted(c, formatted(c, v), R * 1.3, fsOf(c, 14)), cx - R * 0.65, ky + kr + 1, R * 1.3, std::max(4.0, cy + R - ky - kr - 1),
             colorOf(c, "textColor", 0xE6EAF0), fsOf(c, 14), "centre");
}

void drawComboBox(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const bool open = c.opt.runtime && !c.opt.editor && c.opt.runtime->comboOpen(c.o.id);
    const auto shape = rectLocal(w, h, c.src.number(c.o, "radius", 3));
    fillLocal(c, shape, colorOf(c, "fill", 0x141820));
    strokeLocal(c, shape, true, open ? accentOf(c) : colorOf(c, "stroke", 0x4A5568), open ? 2 : std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
    const auto choices = hmi::choicesOf(c.o);
    const int idx = hmi::choiceIndexOf(choices, value(c));
    const double fs = fsOf(c, 16);
    const double arrowW = std::min(h, 28.0);
    std::string shown = idx >= 0 ? choices[static_cast<std::size_t>(idx)].label : std::string{};
    const gfx::Color muted = c.fixed(0x6B7686);
    if (shown.empty()) {
        shown = c.opt.editor && !c.src.text(c.o, "variable").empty() ? "{" + c.src.text(c.o, "variable") + "}" : c.src.text(c.o, "placeholder");
        text(c, fitted(c, shown, w - arrowW - 10, fs), 4, 0, w - arrowW - 6, h, muted, fs, c.src.text(c.o, "align", "gauche"));
    } else {
        text(c, fitted(c, shown, w - arrowW - 10, fs), 4, 0, w - arrowW - 6, h, colorOf(c, "textColor", 0xE6EAF0), fs, c.src.text(c.o, "align", "gauche"));
    }
    lineLocal(c, w - arrowW, h * 0.2, w - arrowW, h * 0.8, c.fixed(0x3A4556));
    const double ax = w - arrowW / 2, ay = h / 2, s = std::min(arrowW, h) * 0.18;
    if (open) triangle(c, ax - s, ay + s * 0.5, ax + s, ay + s * 0.5, ax, ay - s * 0.6, colorOf(c, "textColor", 0xE6EAF0));
    else triangle(c, ax - s, ay - s * 0.5, ax + s, ay - s * 0.5, ax, ay + s * 0.6, colorOf(c, "textColor", 0xE6EAF0));
    drawFormMessage(c, {0, h + 2, std::max(w, 200.0), fs * 1.3}, c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr, fs * 0.9);
}

// 1.12.2 : LA LISTE - ses lignes (items, ou celles d'une source : itemsFrom), la ligne choisie
// surlignee (la valeur lue : son retour d'etat, sinon sa variable), les bandes de defilement en
// haut et en bas quand tout ne tient pas (hmi::listLayout, le meme que listHit).
void drawList(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto shape = rectLocal(w, h, c.src.number(c.o, "radius", 0));
    fillLocal(c, shape, colorOf(c, "fill", 0x262C36));
    const auto choices = hmi::choicesOf(c.o);
    const std::size_t first = c.opt.runtime && !c.opt.editor ? c.opt.runtime->listFirst(c.o.id) : 0;
    const auto l = hmi::listLayout(c.o, w, h, choices.size(), first);
    const int idx = hmi::choiceIndexOf(choices, value(c));
    const double fs = fsOf(c, 14);
    const gfx::Color tc = colorOf(c, "textColor", 0xDDE3EA);
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const std::size_t k = l.first + i;
        if (k >= choices.size()) break;
        const auto& b = l.rows[i];
        if (static_cast<int>(k) == idx) fillLocal(c, boxLocal({b.x + 2, b.y + 1, b.w - 4, b.h - 2}, 2), withAlpha(accentOf(c), 0.55f));
        text(c, fitted(c, choices[k].label, b.w - 14, fs), b.x + 6, b.y, b.w - 10, b.h, tc, fs, "gauche");
    }
    if (l.up.w > 0) {
        const gfx::Color arrow = c.fixed(0xC8D0DC);
        const double s = l.up.h * 0.32;
        triangle(c, l.up.cx() - s, l.up.cy() + s * 0.5, l.up.cx() + s, l.up.cy() + s * 0.5, l.up.cx(), l.up.cy() - s * 0.6,
                 l.first > 0 ? arrow : withAlpha(arrow, 0.3f));
        triangle(c, l.down.cx() - s, l.down.cy() - s * 0.5, l.down.cx() + s, l.down.cy() - s * 0.5, l.down.cx(), l.down.cy() + s * 0.6,
                 l.first + l.rows.size() < choices.size() ? arrow : withAlpha(arrow, 0.3f));
    }
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x3A4556), std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
}

void drawCheckBox(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const bool on = valueTruthy(c);
    const double s = std::clamp(h * 0.62, 10.0, 26.0);
    const hmi::Box b{2, (h - s) / 2, s, s};
    const auto shape = boxLocal(b, 3);
    fillLocal(c, shape, on ? accentOf(c) : c.fixed(0x141820));
    strokeLocal(c, shape, true, on ? accentOf(c) : colorOf(c, "stroke", 0x8A9BB0), 1.5);
    if (on)
        strokeLocal(c, {{static_cast<float>(b.x + s * 0.22), static_cast<float>(b.y + s * 0.52)},
                        {static_cast<float>(b.x + s * 0.43), static_cast<float>(b.y + s * 0.72)},
                        {static_cast<float>(b.x + s * 0.80), static_cast<float>(b.y + s * 0.28)}},
                    false, c.fixedOn(0xFFFFFF), std::max(1.5, s * 0.12));
    const double fs = fsOf(c, 16);
    text(c, fitted(c, c.src.text(c.o, "text"), w - s - 12, fs), s + 8, 0, w - s - 8, h, colorOf(c, "textColor", 0xE6EAF0), fs, "gauche");
    drawFormMessage(c, {0, h + 1, std::max(w, 200.0), fs * 1.2}, c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr, fs * 0.85);
}

void drawRadioGroup(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto choices = hmi::choicesOf(c.o);
    int idx = hmi::choiceIndexOf(choices, value(c));
    if (idx < 0 && c.opt.editor && !choices.empty()) idx = 0;
    const auto boxes = hmi::radioBoxes(c.o, w, h, choices.size());
    const double fs = fsOf(c, 15);
    for (std::size_t i = 0; i < boxes.size() && i < choices.size(); ++i) {
        const auto& b = boxes[i];
        const double r = std::clamp(std::min(b.h, 30.0) * 0.28, 5.0, 11.0);
        const double cx = b.x + r + 3, cy = b.cy();
        const bool on = static_cast<int>(i) == idx;
        fillLocal(c, circleLocal(cx, cy, r), c.fixed(0x141820));
        strokeLocal(c, circleLocal(cx, cy, r), true, on ? accentOf(c) : colorOf(c, "stroke", 0x8A9BB0), 1.5);
        if (on) fillLocal(c, circleLocal(cx, cy, r * 0.55), accentOf(c));
        text(c, fitted(c, choices[i].label, b.w - 2 * r - 12, fs), b.x + 2 * r + 8, b.y, b.w - 2 * r - 8, b.h,
             colorOf(c, "textColor", 0xE6EAF0), fs, "gauche");
    }
}

void drawDateTimePicker(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 6)));
    const auto l = hmi::pickerLayout(c.o, w, h);
    bool pending = false;
    hmi::DateTime d{2026, 9, 23, 14, 30, 0};
    if (c.opt.runtime && !c.opt.editor) d = c.opt.runtime->pickerValue(c.o.id, &pending);
    const double fs = fsOf(c, 16);
    const gfx::Color txt = colorOf(c, "textColor", 0xE6EAF0), accent = accentOf(c);
    const gfx::Color arrowFill = c.fixed(0x2A313C), arrowCol = c.fixed(0xC8D0DC);
    const hmi::PickerLayout::Field* prev = nullptr;
    for (const auto& f : l.fields) {
        int v = 0;
        const char* pattern = "%02d";
        if (f.name == "jour") v = d.day;
        else if (f.name == "mois") v = d.month;
        else if (f.name == "annee") { v = d.year; pattern = "%04d"; }
        else if (f.name == "heure") v = d.hour;
        else if (f.name == "minute") v = d.minute;
        else v = d.second;
        char b[16];
        std::snprintf(b, sizeof b, pattern, v);
        for (const auto* arrow : {&f.up, &f.down}) {
            fillLocal(c, boxLocal({arrow->x + 1, arrow->y + 1, arrow->w - 2, arrow->h - 2}, 3), arrowFill);
            const double ax = arrow->cx(), ay = arrow->cy(), s = std::min(arrow->h, arrow->w) * 0.26;
            if (arrow == &f.up) triangle(c, ax - s, ay + s * 0.45, ax + s, ay + s * 0.45, ax, ay - s * 0.55, arrowCol);
            else triangle(c, ax - s, ay - s * 0.45, ax + s, ay - s * 0.45, ax, ay + s * 0.55, arrowCol);
        }
        const auto field = boxLocal({f.box.x + 1, f.box.y + 1, f.box.w - 2, f.box.h - 2}, 2);
        fillLocal(c, field, c.fixed(0x141820));
        strokeLocal(c, field, true, pending ? accent : c.fixed(0x3A4556));
        text(c, b, f.box.x, f.box.y, f.box.w, f.box.h, txt, std::min(fs, f.box.h * 0.62), "centre");
        // Les separateurs : / dans la date, : dans l'heure.
        if (prev) {
            const bool date = f.name == "mois" || f.name == "annee";
            const bool time = f.name == "minute" || f.name == "seconde";
            if (date || time) text(c, date ? "/" : ":", prev->box.right(), f.box.y, f.box.x - prev->box.right(), f.box.h,
                                  c.fixed(0x8A94A3), std::min(fs, f.box.h * 0.62), "centre");
        }
        prev = &f;
    }
    drawButtonBox(c, l.now, "Maintenant", c.fixed(0x323A47), txt, std::min(fs * 0.8, l.now.h * 0.5));
    drawButtonBox(c, l.ok, pending ? "Valider \xE2\x80\xA2" : "Valider", pending ? accent : c.fixed(0x2A4A7A),
                  c.fixedOn(0xFFFFFF), std::min(fs * 0.8, l.ok.h * 0.5));
    drawFormMessage(c, {0, h + 1, std::max(w, 220.0), fs * 1.2}, c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr, fs * 0.8);
}

void drawWeeklySchedule(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 4));
    const int res = hmi::resolutionMinutes(c.src.text(c.o, "resolution", "30 min"));
    const hmi::WeekSchedule* live = c.opt.runtime && !c.opt.editor ? c.opt.runtime->schedule(c.o.id) : nullptr;
    hmi::WeekSchedule parsed;
    if (!live && !hmi::parseSchedule(c.src.text(c.o, "schedule"), res, parsed)) parsed = hmi::emptySchedule(res);
    const hmi::WeekSchedule& ws = live ? *live : parsed;
    const auto l = hmi::scheduleLayout(c.o, w, h, ws.slotMinutes);
    const double fs = fsOf(c, 12);
    const gfx::Color txt = colorOf(c, "textColor", 0xC8D0DC), on = colorOf(c, "accent", 0x2ECC71);
    const gfx::Color grid = c.fixed(0x2E3643), hourLine = c.fixed(0x46505F);
    int today = -1, minute = -1;
    if (c.opt.runtime && !c.opt.editor) {
        const double epoch = c.opt.runtime->epochOf(c.opt.runtime->now());
        today = hmi::weekdayFromEpoch(epoch);
        const auto dt = hmi::dateTimeFromEpoch(epoch);
        minute = dt.hour * 60 + dt.minute;
    }
    // Les cases allumees, jour par jour (les suites de cases en une seule bande).
    for (int d = 0; d < 7; ++d) {
        const auto& day = ws.days[static_cast<std::size_t>(d)];
        const double y = l.grid.y + l.cellH * d;
        if (d == today) fillLocal(c, boxLocal({l.grid.x, y, l.grid.w, l.cellH}), fade(gfx::Color{255, 255, 255, 14}, c.alpha));
        std::size_t s = 0;
        while (s < day.size()) {
            if (!day[s]) { ++s; continue; }
            std::size_t e = s;
            while (e < day.size() && day[e]) ++e;
            fillLocal(c, boxLocal({l.grid.x + l.cellW * static_cast<double>(s) + 0.5, y + 2, l.cellW * static_cast<double>(e - s) - 1, l.cellH - 4}, 2), on);
            s = e;
        }
        text(c, std::string(hmi::kDayNames[d]), 0, y, l.labelW - 4, l.cellH, d == today ? c.fixedText(0xFFFFFF) : txt,
             std::min(fs, l.cellH * 0.7), "droite");
        lineLocal(c, l.grid.x, y, l.grid.right(), y, grid);
    }
    lineLocal(c, l.grid.x, l.grid.bottom(), l.grid.right(), l.grid.bottom(), grid);
    // Les heures : un trait par heure, une etiquette toutes les 2, 3 ou 6 heures.
    const double hourW = l.grid.w / 24.0;
    const int labelEvery = hourW * 2 >= fs * 2.6 ? 2 : hourW * 3 >= fs * 2.6 ? 3 : 6;
    for (int hr = 0; hr <= 24; ++hr) {
        const double x = l.grid.x + hourW * hr;
        lineLocal(c, x, l.grid.y, x, l.grid.bottom(), hr % 6 == 0 ? hourLine : grid);
        if (hr % labelEvery == 0 && hr < 24) text(c, std::to_string(hr) + "h", x - hourW * 2, 0, hourW * 4, l.headerH, txt, std::min(fs, l.headerH * 0.7), "centre");
    }
    // Maintenant : un trait dans le jour courant.
    if (today >= 0 && minute >= 0) {
        const double x = l.grid.x + l.grid.w * minute / 1440.0;
        const double y = l.grid.y + l.cellH * today;
        lineLocal(c, x, y - 2, x, y + l.cellH + 2, colorOf(c, "nowColor", 0xF2994A), 2);
    }
}

// --------------------------------------------------------------- afficheurs ----
void drawNumericDisplay(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 4)));
    const std::string label = c.src.text(c.o, "label");
    const double fs = fsOf(c, 24);
    double top = 0;
    if (!label.empty()) {
        const double ls = std::max(9.0, std::min(fs * 0.5, h * 0.28));
        text(c, fitted(c, label, w - 10, ls), 4, 2, w - 8, ls * 1.4, c.fixedText(0x8A94A3), ls, "gauche");
        top = ls * 1.3;
    }
    const std::string align = c.src.text(c.o, "align", "droite");
    if (badQuality(c)) {
        // Qualite mauvaise : des hachures et ###.
        for (double x = -h; x < w; x += 10) {
            const double x0 = std::max(0.0, x), y0 = x < 0 ? -x : 0.0, x1 = std::min(w, x + h), y1 = x + h > w ? h - (x + h - w) : h;
            lineLocal(c, x0, h - y0, x1, h - y1, fade(gfx::Color{120, 130, 145, 60}, c.alpha));
        }
        text(c, "###", 4, top, w - 8, h - top, c.fixedText(0x8A94A3), std::min(fs, (h - top) * 0.8), align);
        return;
    }
    double v = 0;
    const bool known = valueNumber(c, v);
    gfx::Color col = colorOf(c, "textColor", 0xE6EAF0);
    if (known) {
        double t = 0;
        const auto has = [&](const char* key) { return hmi::parseNumber(c.src.text(c.o, key), t); };
        if ((has("lowAlarm") && v < t) || (has("highAlarm") && v > t)) col = colorOf(c, "colorAlarm", 0xE5534B);
        else if ((has("low") && v < t) || (has("high") && v > t)) col = colorOf(c, "colorWarning", 0xF2C94C);
    }
    const std::string shown = known ? formatted(c, v) : value(c);
    text(c, fitted(c, shown, w - 10, std::min(fs, (h - top) * 0.8)), 4, top, w - 8, h - top, col, std::min(fs, (h - top) * 0.8), align);
}

void drawMultiStateIndicator(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto states = hmi::parseStateList(c.src.text(c.o, "stateList"));
    const int idx = hmi::stateIndexOf(states, value(c));
    const hmi::StateEntry* st = idx >= 0 ? &states[static_cast<std::size_t>(idx)] : nullptr;
    gfx::Color col = st && !st->color.empty() ? hexColor(c, st->color, 0x4A5261) : colorOf(c, "colorOff", 0x4A5261);
    // Le clignotement : la phase eteinte garde la teinte, assombrie (on la reconnait
    // encore : un defaut reste rouge).
    if (st && st->blink && blinkOff(c)) col = mix(col, gfx::Color{24, 28, 35, col.a}, 0.55f);
    const bool showText = c.src.flag(c.o, "showText", true);
    const double d = std::max(4.0, std::min(h - 2, showText ? w * 0.4 : w - 2));
    const double cx = showText ? d / 2 + 1 : w / 2, cy = h / 2;
    const auto shape = c.src.text(c.o, "shape", "rond") == "rond" ? circleLocal(cx, cy, d / 2, 32) : boxLocal({cx - d / 2, cy - d / 2, d, d}, 3);
    fillLocal(c, shape, col);
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x1B1F26), std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
    fillLocal(c, shapes::ellipse({static_cast<float>(cx - d * 0.13), static_cast<float>(cy - d * 0.17)}, static_cast<float>(d * 0.14),
                                 static_cast<float>(d * 0.1), 16),
              fade(gfx::Color{255, 255, 255, 110}, c.alpha));
    // Lot 13 : l'accessibilite - le symbole de l'etat (d'apres sa couleur).
    if (c.src.flag(c.o, "a11ySymbols", false)) {
        int sym = st && !st->color.empty() ? hmi::statusSymbolOf(st->color) : 4;
        if (sym == 0) sym = 4;
        drawStatusSymbol(c, sym, cx, cy, d * 0.55, col);
    }
    if (showText) {
        const std::string label = st ? st->text : std::string("?");
        const double fs = fsOf(c, 15);
        text(c, fitted(c, label, w - d - 10, fs), d + 8, 0, w - d - 8, h, colorOf(c, "textColor", 0xE6EAF0), fs, "gauche");
    }
}

void drawMultiStateText(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto fill = colorOf(c, "fill", 0x000000);
    const auto stroke = colorOf(c, "stroke", 0x000000);
    const auto shape = rectLocal(w, h, 3);
    if (!c.src.text(c.o, "fill").empty()) fillLocal(c, shape, fill);
    if (!c.src.text(c.o, "stroke").empty()) strokeLocal(c, shape, true, stroke);
    const auto states = hmi::parseStateList(c.src.text(c.o, "stateList"));
    const int idx = hmi::stateIndexOf(states, value(c));
    const hmi::StateEntry* st = idx >= 0 ? &states[static_cast<std::size_t>(idx)] : nullptr;
    const std::string label = st ? st->text : c.src.text(c.o, "unknownText");
    gfx::Color col = st && !st->color.empty() ? hexColor(c, st->color, 0xE6EAF0) : colorOf(c, "textColor", 0xE6EAF0);
    if (st && st->blink && blinkOff(c)) col = withAlpha(col, 0.35f);
    const double fs = fsOf(c, 18);
    text(c, fitted(c, label, w - 8, fs), 0, 0, w, h, col, fs, c.src.text(c.o, "align", "centre"));
}

// Les zones, la valeur, la consigne et l'echelle d'une barre (bargraphe, thermometre).
struct Scale {
    double mn{0}, mx{100};
    [[nodiscard]] double frac(double v) const { return hmi::fractionOf(v, mn, mx); }
};

void drawBargraph(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const Scale s{c.src.number(c.o, "min", 0), c.src.number(c.o, "max", 100)};
    double v = s.mn;
    const bool known = valueNumber(c, v);
    const double f = s.frac(v);
    const bool vertical = c.src.text(c.o, "orientation", "verticale") != "horizontale";
    const double fs = fsOf(c, 12);
    const bool showValue = c.src.flag(c.o, "showValue", true);
    const auto zones = hmi::parseZones(c.src.text(c.o, "zones"));
    const auto* zone = known ? hmi::zoneOf(zones, v) : nullptr;
    const gfx::Color barCol = zone ? hexColor(c, zone->color, 0x2F6FD6) : colorOf(c, "fill", 0x2F6FD6);
    const gfx::Color txt = colorOf(c, "textColor", 0xC8D0DC);
    const int ticks = static_cast<int>(std::clamp(c.src.number(c.o, "ticks", 5), 0.0, 40.0));
    double sp = 0;
    const bool hasSetpoint = hmi::parseNumber(c.src.text(c.o, "setpoint"), sp);
    if (vertical) {
        const double valueH = showValue ? fs * 1.9 : 0;
        const hmi::Box bar{6, 6, std::max(8.0, w * 0.4), std::max(10.0, h - 12 - valueH)};
        fillLocal(c, boxLocal(bar, 2), colorOf(c, "background", 0x2A313C));
        if (f > 0) fillLocal(c, boxLocal({bar.x, bar.bottom() - bar.h * f, bar.w, bar.h * f}, 2), barCol);
        // L'echelle : la bande des zones, les graduations.
        const double zx = bar.right() + 3;
        for (const auto& z : zones) {
            const double y0 = bar.bottom() - bar.h * s.frac(z.to), y1 = bar.bottom() - bar.h * s.frac(z.from);
            if (y1 > y0) fillLocal(c, boxLocal({zx, y0, 4, y1 - y0}), hexColor(c, z.color, 0x6B7686));
        }
        if (ticks >= 1)
            for (int k = 0; k <= ticks; ++k) {
                const double t = static_cast<double>(k) / ticks, y = bar.bottom() - bar.h * t;
                lineLocal(c, zx + 5, y, zx + 10, y, txt);
                text(c, tickLabel(s.mn + (s.mx - s.mn) * t), zx + 11, y - fs * 0.7, w - zx - 11, fs * 1.4, txt, fs, "gauche");
            }
        if (hasSetpoint) {
            const double y = bar.bottom() - bar.h * s.frac(sp);
            const gfx::Color sc = colorOf(c, "setpointColor", 0xFFFFFF);
            lineLocal(c, bar.x, y, bar.right(), y, sc, 1.5);
            triangle(c, 0, y - 5, 0, y + 5, bar.x - 1, y, sc);
        }
        if (showValue) text(c, known ? formatted(c, v) : std::string("###"), 0, h - valueH, w, valueH, known ? txt : c.fixedText(0x8A94A3), fs * 1.1, "centre");
    } else {
        const double valueW = showValue ? std::min(w * 0.3, fs * 5.5) : 0;
        const hmi::Box bar{6, 4, std::max(10.0, w - 12 - valueW), std::max(8.0, h * 0.42)};
        fillLocal(c, boxLocal(bar, 2), colorOf(c, "background", 0x2A313C));
        if (f > 0) fillLocal(c, boxLocal({bar.x, bar.y, bar.w * f, bar.h}, 2), barCol);
        const double zy = bar.bottom() + 3;
        for (const auto& z : zones) {
            const double x0 = bar.x + bar.w * s.frac(z.from), x1 = bar.x + bar.w * s.frac(z.to);
            if (x1 > x0) fillLocal(c, boxLocal({x0, zy, x1 - x0, 4}), hexColor(c, z.color, 0x6B7686));
        }
        if (ticks >= 1)
            for (int k = 0; k <= ticks; ++k) {
                const double t = static_cast<double>(k) / ticks, x = bar.x + bar.w * t;
                lineLocal(c, x, zy + 5, x, zy + 9, txt);
                text(c, tickLabel(s.mn + (s.mx - s.mn) * t), x - fs * 2, zy + 9, fs * 4, fs * 1.4, txt, fs, "centre");
            }
        if (hasSetpoint) {
            const double x = bar.x + bar.w * s.frac(sp);
            const gfx::Color sc = colorOf(c, "setpointColor", 0xFFFFFF);
            lineLocal(c, x, bar.y, x, bar.bottom(), sc, 1.5);
            triangle(c, x - 5, 0, x + 5, 0, x, bar.y - 1, sc);
        }
        if (showValue) text(c, known ? formatted(c, v) : std::string("###"), w - valueW, 0, valueW, bar.bottom() + 2, txt, fs * 1.1, "droite");
    }
}

void drawThermometer(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const Scale s{c.src.number(c.o, "min", -20), c.src.number(c.o, "max", 60)};
    double v = s.mn;
    const bool known = valueNumber(c, v);
    const double f = s.frac(v);
    const double fs = fsOf(c, 11);
    const bool showValue = c.src.flag(c.o, "showValue", true);
    const double valueH = showValue ? fs * 1.9 : 0;
    const double tw = std::clamp(w * 0.22, 8.0, 20.0);
    const double br = tw * 1.05;
    const double cx = std::max(br + 3, w * 0.32);
    const double tubeTop = 6, bulbCy = h - valueH - br - 3;
    const double tubeBottom = bulbCy - br * 0.6;
    const gfx::Color bg = colorOf(c, "background", 0x2A313C), liquid = colorOf(c, "fill", 0xE5534B), stroke = colorOf(c, "stroke", 0x8A9BB0);
    const auto tube = boxLocal({cx - tw / 2, tubeTop, tw, bulbCy - tubeTop}, tw / 2);
    fillLocal(c, tube, bg);
    fillLocal(c, circleLocal(cx, bulbCy, br), liquid);
    const double level = tubeBottom - (tubeBottom - tubeTop - tw * 0.3) * f;
    if (known) fillLocal(c, boxLocal({cx - tw * 0.3, level, tw * 0.6, bulbCy - level}, tw * 0.3), liquid);
    strokeLocal(c, tube, true, stroke, 1.2);
    strokeLocal(c, circleLocal(cx, bulbCy, br), true, stroke, 1.2);
    fillLocal(c, circleLocal(cx - br * 0.3, bulbCy - br * 0.3, br * 0.22), fade(gfx::Color{255, 255, 255, 90}, c.alpha));
    const int ticks = static_cast<int>(std::clamp(c.src.number(c.o, "ticks", 8), 0.0, 40.0));
    const gfx::Color txt = colorOf(c, "textColor", 0xC8D0DC);
    if (ticks >= 1)
        for (int k = 0; k <= ticks; ++k) {
            const double t = static_cast<double>(k) / ticks;
            const double y = tubeBottom - (tubeBottom - tubeTop - tw * 0.3) * t;
            lineLocal(c, cx + tw / 2 + 2, y, cx + tw / 2 + 7, y, txt);
            text(c, tickLabel(s.mn + (s.mx - s.mn) * t), cx + tw / 2 + 8, y - fs * 0.7, w - cx - tw / 2 - 8, fs * 1.4, txt, fs, "gauche");
        }
    if (showValue) text(c, known ? formatted(c, v) : std::string("###"), 0, h - valueH, w, valueH, txt, fs * 1.25, "centre");
}

void drawDial(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double cx = w / 2, cy = h / 2, R = std::max(10.0, std::min(w, h) / 2 - 3);
    const Scale s{c.src.number(c.o, "min", 0), c.src.number(c.o, "max", 100)};
    double v = s.mn;
    const bool known = valueNumber(c, v);
    fillLocal(c, circleLocal(cx, cy, R, 64), colorOf(c, "fill", 0x1F252E));
    strokeLocal(c, circleLocal(cx, cy, R, 64), true, colorOf(c, "stroke", 0x3A4556), 1.5);
    constexpr double a0 = 135, sweep = 270;
    for (const auto& z : hmi::parseZones(c.src.text(c.o, "zones")))
        arcBand(c, cx, cy, R * 0.80, R * 0.90, a0 + sweep * s.frac(z.from), a0 + sweep * s.frac(z.to), hexColor(c, z.color, 0x6B7686), 48);
    const int ticks = static_cast<int>(std::clamp(c.src.number(c.o, "ticks", 10), 0.0, 40.0));
    const double fs = fsOf(c, 13);
    const gfx::Color txt = colorOf(c, "textColor", 0xE6EAF0);
    if (ticks >= 1)
        for (int k = 0; k <= ticks; ++k) {
            const double t = static_cast<double>(k) / ticks, a = (a0 + sweep * t) * kPi / 180.0;
            lineLocal(c, cx + std::cos(a) * R * 0.70, cy + std::sin(a) * R * 0.70, cx + std::cos(a) * R * 0.78, cy + std::sin(a) * R * 0.78, txt, 1.5);
            if (ticks <= 12 || k % 2 == 0) {
                const double lr = R * 0.56, lx = cx + std::cos(a) * lr, ly = cy + std::sin(a) * lr;
                const double ts = std::min(fs * 0.8, R * 0.13);
                text(c, tickLabel(s.mn + (s.mx - s.mn) * t), lx - ts * 2, ly - ts * 0.7, ts * 4, ts * 1.4, withAlpha(txt, 0.8f), ts, "centre");
            }
        }
    const std::string label = c.src.text(c.o, "label");
    if (!label.empty()) text(c, fitted(c, label, R * 1.2, fs * 0.85), cx - R * 0.6, cy - R * 0.42, R * 1.2, fs * 1.4, withAlpha(txt, 0.8f), fs * 0.85, "centre");
    if (known) {
        const double a = (a0 + sweep * s.frac(v)) * kPi / 180.0;
        const double ex = std::cos(a), ey = std::sin(a), nx = -ey, ny = ex, base = std::max(2.0, R * 0.04);
        fillLocal(c, {{static_cast<float>(cx + ex * R * 0.82), static_cast<float>(cy + ey * R * 0.82)},
                      {static_cast<float>(cx + nx * base - ex * R * 0.12), static_cast<float>(cy + ny * base - ey * R * 0.12)},
                      {static_cast<float>(cx - nx * base - ex * R * 0.12), static_cast<float>(cy - ny * base - ey * R * 0.12)}},
                  colorOf(c, "needleColor", 0xF4F6FA));
    }
    fillLocal(c, circleLocal(cx, cy, std::max(3.0, R * 0.07)), colorOf(c, "needleColor", 0xF4F6FA));
    text(c, known ? formatted(c, v) : std::string("###"), cx - R * 0.6, cy + R * 0.28, R * 1.2, fs * 1.6, txt, fs * 1.15, "centre");
}

void drawClock(const Ctx& c) {
    const double w = c.w(), h = c.h();
    hmi::DateTime t{2026, 9, 23, 10, 8, 30};
    if (c.opt.runtime && !c.opt.editor) t = hmi::dateTimeFromEpoch(c.opt.runtime->epochOf(c.opt.runtime->now()));
    const bool seconds = c.src.flag(c.o, "seconds", true), showDate = c.src.flag(c.o, "showDate", true);
    const double fs = fsOf(c, 20);
    const gfx::Color txt = colorOf(c, "textColor", 0xE6EAF0);
    char date[16];
    std::snprintf(date, sizeof date, "%02d/%02d/%04d", t.day, t.month, t.year);
    if (c.src.text(c.o, "clockStyle", "analogique") == "num\xC3\xA9rique") {
        fillAndStroke(c, rectLocal(w, h, 6));
        char time[16];
        if (seconds) std::snprintf(time, sizeof time, "%02d:%02d:%02d", t.hour, t.minute, t.second);
        else std::snprintf(time, sizeof time, "%02d:%02d", t.hour, t.minute);
        const double big = std::min(fs * 1.6, h * (showDate ? 0.45 : 0.62));
        text(c, time, 0, showDate ? h * 0.08 : 0, w, showDate ? h * 0.58 : h, txt, big, "centre");
        if (showDate) text(c, date, 0, h * 0.62, w, h * 0.3, withAlpha(txt, 0.7f), std::min(fs * 0.7, h * 0.2), "centre");
        return;
    }
    const double cx = w / 2, cy = h / 2, R = std::max(10.0, std::min(w, h) / 2 - 3);
    fillLocal(c, circleLocal(cx, cy, R, 64), colorOf(c, "fill", 0x1F252E));
    strokeLocal(c, circleLocal(cx, cy, R, 64), true, colorOf(c, "stroke", 0x3A4556), 2);
    for (int k = 0; k < 60; ++k) {
        const double a = k * 6 * kPi / 180.0;
        const bool major = k % 5 == 0;
        if (!major && R < 50) continue;
        const double r0 = R * (major ? (k % 15 == 0 ? 0.78 : 0.83) : 0.9);
        lineLocal(c, cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * R * 0.95, cy + std::sin(a) * R * 0.95,
                  major ? txt : withAlpha(txt, 0.5f), major ? 2 : 1);
    }
    if (showDate) text(c, date, cx - R * 0.6, cy + R * 0.28, R * 1.2, R * 0.2, withAlpha(txt, 0.7f), std::min(fs * 0.6, R * 0.14), "centre");
    // Les aiguilles (angles : 0 en haut, sens horaire).
    const auto hand = [&](double turns, double len, double width, gfx::Color col) {
        const double a = (turns * 360.0 - 90.0) * kPi / 180.0;
        lineLocal(c, cx - std::cos(a) * len * 0.15, cy - std::sin(a) * len * 0.15, cx + std::cos(a) * len, cy + std::sin(a) * len, col, width);
    };
    hand(((t.hour % 12) + t.minute / 60.0) / 12.0, R * 0.52, std::max(2.5, R * 0.06), txt);
    hand((t.minute + t.second / 60.0) / 60.0, R * 0.76, std::max(2.0, R * 0.04), txt);
    if (seconds) hand(t.second / 60.0, R * 0.84, 1.2, accentOf(c));
    fillLocal(c, circleLocal(cx, cy, std::max(2.5, R * 0.05)), seconds ? accentOf(c) : txt);
}

void drawHourMeter(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 4)));
    const double secs = c.opt.runtime && !c.opt.editor ? c.opt.runtime->hourMeterSeconds(c.o.id) : 450245.0;
    const bool running = c.opt.runtime && !c.opt.editor && c.opt.runtime->hourMeterRunning(c.o.id);
    const std::string label = c.src.text(c.o, "label");
    const double fs = fsOf(c, 22);
    double top = 0;
    if (!label.empty()) {
        const double ls = std::max(9.0, std::min(fs * 0.5, h * 0.28));
        text(c, fitted(c, label, w - 10, ls), 4, 2, w - 8, ls * 1.4, c.fixedText(0x8A94A3), ls, "gauche");
        top = ls * 1.3;
    }
    // Un petit sablier : il tourne quand le compteur compte.
    const double g = std::min(h - top - 6, 20.0);
    double left = 6;
    if (g >= 10 && w > g * 6) {
        const double gx = 6, gy = top + (h - top - g) / 2;
        const gfx::Color gc = running ? colorOf(c, "colorOn", 0x2ECC71) : c.fixed(0x6B7686);
        triangle(c, gx, gy, gx + g * 0.7, gy, gx + g * 0.35, gy + g * 0.5, gc);
        triangle(c, gx, gy + g, gx + g * 0.7, gy + g, gx + g * 0.35, gy + g * 0.5, withAlpha(gc, running && blinkOff(c) ? 0.4f : 1.f));
        left = gx + g * 0.7 + 6;
    }
    const std::string shown = hmi::formatRunTime(secs, c.src.text(c.o, "durationFormat", "h:mm:ss"));
    const double size = std::min(fs, (h - top) * 0.75);
    text(c, fitted(c, shown, w - left - 6, size), left, top, w - left - 4, h - top,
         running ? colorOf(c, "colorOn", 0x2ECC71) : colorOf(c, "textColor", 0xE6EAF0), size, "droite");
}

// Un chiffre 7 segments dans la case (x, y, w, h), incline de `skew` (pixels de decalage en haut).
void segmentDigit(const Ctx& c, double x, double y, double w, double h, std::uint8_t segs, bool dot, gfx::Color on, gfx::Color off) {
    const double t = std::max(1.5, w * 0.16), g = t * 0.12;
    const double skew = w * 0.12;
    const auto P = [&](double px, double py) {
        // L'inclinaison : plus haut, plus a droite.
        return gfx::Point{static_cast<float>(x + px + skew * (1.0 - py / h)), static_cast<float>(y + py)};
    };
    const double W = w - skew - t * 0.6, H = h, mid = H / 2;
    const auto horiz = [&](double cy) {
        return std::vector<gfx::Point>{P(t / 2 + g, cy), P(t + g, cy - t / 2), P(W - t - g, cy - t / 2), P(W - t / 2 - g, cy), P(W - t - g, cy + t / 2),
                                       P(t + g, cy + t / 2)};
    };
    const auto vert = [&](double cx, double y0, double y1) {
        return std::vector<gfx::Point>{P(cx, y0 + g), P(cx + t / 2, y0 + t / 2 + g), P(cx + t / 2, y1 - t / 2 - g), P(cx, y1 - g),
                                       P(cx - t / 2, y1 - t / 2 - g), P(cx - t / 2, y0 + t / 2 + g)};
    };
    const std::vector<gfx::Point> shapes7[7] = {
        horiz(t / 2),                              // a
        vert(W - t / 2, t / 2, mid),               // b
        vert(W - t / 2, mid, H - t / 2),           // c
        horiz(H - t / 2),                          // d
        vert(t / 2, mid, H - t / 2),               // e
        vert(t / 2, t / 2, mid),                   // f
        horiz(mid),                                // g
    };
    for (int k = 0; k < 7; ++k) fillLocal(c, shapes7[k], (segs >> k) & 1 ? on : off);
    const double dr = t * 0.55;
    fillLocal(c, circleLocal(x + W + dr * 0.9, y + H - dr, dr, 12), dot ? on : off);
}

void drawSevenSegment(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 4));
    const int digits = static_cast<int>(std::clamp(c.src.number(c.o, "digits", 4), 1.0, 16.0));
    const int decimals = static_cast<int>(std::clamp(c.src.number(c.o, "decimals", 1), 0.0, static_cast<double>(digits - 1)));
    double v = 0;
    const bool ok = valueNumber(c, v) && !badQuality(c);
    const auto cells = hmi::sevenSegmentDigits(v, digits, decimals, c.src.flag(c.o, "leadingZeros", false), ok);
    const double pad = std::max(4.0, h * 0.12);
    const double cw = (w - 2 * pad) / digits, ch = h - 2 * pad;
    const gfx::Color on = colorOf(c, "colorOn", 0xFF3B30), off = colorOf(c, "colorOff", 0x3A1E1E);
    for (int k = 0; k < digits; ++k)
        segmentDigit(c, pad + cw * k + cw * 0.06, pad, cw * 0.82, ch, hmi::segmentsOf(cells[static_cast<std::size_t>(k)].c),
                     cells[static_cast<std::size_t>(k)].dot, on, off);
}

void drawTrendArrow(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const int dir = c.opt.runtime && !c.opt.editor ? c.opt.runtime->trendArrow(c.o.id) : 1;
    const double s = std::min(w, h), cx = w / 2, cy = h / 2;
    const gfx::Color col = dir > 0 ? colorOf(c, "colorUp", 0x2ECC71) : dir < 0 ? colorOf(c, "colorDown", 0xE5534B) : colorOf(c, "colorSteady", 0x9AA6B8);
    // Une fleche vers le haut, le bas, ou la droite (stable) : tete + corps.
    const double head = s * 0.42, body = s * 0.18, len = s * 0.86;
    std::vector<gfx::Point> arrow = {{0.f, static_cast<float>(-len / 2)}, {static_cast<float>(head), static_cast<float>(-len / 2 + head)},
                                     {static_cast<float>(body / 2), static_cast<float>(-len / 2 + head)}, {static_cast<float>(body / 2), static_cast<float>(len / 2)},
                                     {static_cast<float>(-body / 2), static_cast<float>(len / 2)}, {static_cast<float>(-body / 2), static_cast<float>(-len / 2 + head)},
                                     {static_cast<float>(-head), static_cast<float>(-len / 2 + head)}};
    const double a = dir > 0 ? 0.0 : dir < 0 ? kPi : kPi / 2;
    for (auto& p : arrow) {
        const double x = p.x * std::cos(a) - p.y * std::sin(a), y = p.x * std::sin(a) + p.y * std::cos(a);
        p = {static_cast<float>(cx + x), static_cast<float>(cy + y)};
    }
    fillLocal(c, arrow, col);
}

void drawMarquee(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto shape = rectLocal(w, h, 3);
    fillLocal(c, shape, colorOf(c, "fill", 0x1B2028));
    const std::string msg = c.src.text(c.o, "text");
    const double fs = std::min(fsOf(c, 18), h * 0.75);
    const double tw = measureLocal(c, msg, fs);
    const double gap = std::max(60.0, w * 0.25);
    const double speed = std::max(0.0, c.src.number(c.o, "speed", 60));
    const bool right = c.src.text(c.o, "direction", "gauche") == "droite";
    // Le texte defile dans la boite : rogne a l'ecran (sauf objet tourne).
    const bool clip = c.o.rotation() == 0 && c.vp.angle == 0.f;
    if (clip) {
        const auto a = c.map(0, 0), e = c.map(w, h);
        c.r.pushClip({std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)});
    }
    const gfx::Color col = colorOf(c, "textColor", 0xF2C94C);
    if (c.opt.editor || speed <= 0) {
        text(c, msg, 0, 0, std::max(w, tw + 12), h, col, fs, "gauche");
    } else {
        const double cycle = tw + gap;
        const double off = std::fmod(c.opt.time * speed, cycle);
        double x = right ? -tw + off : w - off;
        // Les passages successifs, pour que le texte revienne sans trou.
        for (int k = -1; k <= static_cast<int>(w / cycle) + 1; ++k) {
            const double px = right ? x - k * cycle : x + k * cycle;
            if (px > w || px + tw < 0) continue;
            text(c, msg, px - 4, 0, tw + 8, h, col, fs, "gauche");
        }
    }
    if (clip) c.r.popClip();
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x3A4556));
}

// Les codes QR deja calcules (un texte, un niveau) : pas d'encodage a chaque image.
const hmi::QrCode& qrOf(const std::string& textValue, char level) {
    static std::mutex m;
    static std::map<std::string, hmi::QrCode> cache;
    const std::lock_guard<std::mutex> lock(m);
    const std::string key = std::string(1, level) + textValue;
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 64) cache.clear();
        it = cache.emplace(key, hmi::encodeQr(textValue, level)).first;
    }
    return it->second;
}

void drawQrCode(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const std::string lv = c.src.text(c.o, "ecLevel", "M");
    const hmi::QrCode qr = qrOf(c.src.text(c.o, "text"), lv.empty() ? 'M' : lv[0]);
    fillLocal(c, rectLocal(w, h), colorOf(c, "fill", 0xFFFFFF));
    if (!qr.ok()) {
        text(c, "Code QR", 0, 0, w, h * 0.5, c.fixedText(0x8A3A3A), 13, "centre");
        text(c, fitted(c, qr.error, w - 6, 10), 0, h * 0.5, w, h * 0.4, c.fixedText(0x5A6678), 10, "centre", true);
        return;
    }
    const int margin = static_cast<int>(std::clamp(c.src.number(c.o, "margin", 2), 0.0, 10.0));
    const double n = qr.size + 2.0 * margin;
    const double ms = std::min(w, h) / n;
    const double ox = (w - ms * qr.size) / 2, oy = (h - ms * qr.size) / 2;
    const gfx::Color dark = colorOf(c, "darkColor", 0x000000);
    for (int y = 0; y < qr.size; ++y) {
        int x = 0;
        while (x < qr.size) {
            if (!qr.at(x, y)) { ++x; continue; }
            int e = x;
            while (e < qr.size && qr.at(e, y)) ++e;
            // Un recouvrement d'un quart de pixel : pas de fil clair entre les modules.
            fillLocal(c, boxLocal({ox + ms * x, oy + ms * y, ms * (e - x) + 0.25, ms + 0.25}), dark);
            x = e;
        }
    }
}

// ------------------------------------------------------------------ lot 10 ----
// Un engrenage plein : huit dents, un moyeu de la couleur du fond.
void gearLocal(const Ctx& c, double cx, double cy, double r, gfx::Color col, gfx::Color hole) {
    const double r1 = r * 0.7, half = r * 0.19;
    for (int k = 0; k < 8; ++k) {
        const double a = k * kPi / 4 + kPi / 8, ca = std::cos(a), sa = std::sin(a);
        const auto P = [&](double along, double side) {
            return gfx::Point{static_cast<float>(cx + ca * along - sa * side), static_cast<float>(cy + sa * along + ca * side)};
        };
        fillLocal(c, {P(r1 * 0.85, -half), P(r, -half * 0.75), P(r, half * 0.75), P(r1 * 0.85, half)}, col);
    }
    fillLocal(c, circleLocal(cx, cy, r1), col);
    fillLocal(c, circleLocal(cx, cy, r * 0.3), hole);
}

// L'objet Parametres systeme : un bouton, un engrenage, son libelle ; un clic
// ouvre le menu natif (le moteur le fait, sans action).
void drawSystemButton(const Ctx& c) {
    const double w = c.w(), h = c.h(), rad = c.src.number(c.o, "radius", 4);
    const bool down = pressedNow(c);
    const auto shape = rectLocal(w, h, rad);
    const gfx::Color base = colorOf(c, "fill", 0x3A4556);
    const gfx::Color fill = down ? mix(base, gfx::Color{0, 0, 0, base.a}, 0.25f) : base;
    fillLocal(c, shape, fill);
    if (!down) fillLocal(c, boxLocal({2, 2, w - 4, h * 0.45}, rad), fade(gfx::Color{255, 255, 255, 18}, c.alpha));
    strokeLocal(c, shape, true, colorOf(c, "stroke", 0x5B6B82), std::max(1.0, c.src.number(c.o, "strokeWidth", 1)));
    const gfx::Color tc = colorOf(c, "textColor", 0xFFFFFF);
    const double dy = down ? 1.5 : 0;
    double tx = 0, tw = w;
    if (c.src.flag(c.o, "icon", true) && w > h * 1.2) {
        const double g = std::min(h * 0.58, 28.0);
        gearLocal(c, 10 + g / 2, h / 2 + dy, g / 2, tc, fill);
        tx = g + 14;
        tw = std::max(4.0, w - tx - 6);
    }
    text(c, c.src.text(c.o, "text"), tx, dy, tw, h, tc, fsOf(c, 16), c.src.text(c.o, "align", "centre"), c.src.flag(c.o, "wrap"));
}

} // namespace

bool drawLot9(const Ctx& c) {
    switch (c.o.kind) {
        case Kind::PushButton:          drawPushButton(c); return true;
        case Kind::Switch:              drawSwitch(c); return true;
        case Kind::IlluminatedButton:   drawIlluminatedButton(c); return true;
        case Kind::Selector:            drawSelector(c); return true;
        case Kind::Slider:              drawSlider(c); return true;
        case Kind::Knob:                drawKnob(c); return true;
        case Kind::ComboBox:            drawComboBox(c); return true;
        case Kind::List:                drawList(c); return true;           // 1.12.2
        case Kind::CheckBox:            drawCheckBox(c); return true;
        case Kind::RadioGroup:          drawRadioGroup(c); return true;
        case Kind::DateTimePicker:      drawDateTimePicker(c); return true;
        case Kind::WeeklySchedule:      drawWeeklySchedule(c); return true;
        case Kind::NumericDisplay:      drawNumericDisplay(c); return true;
        case Kind::MultiStateIndicator: drawMultiStateIndicator(c); return true;
        case Kind::MultiStateText:      drawMultiStateText(c); return true;
        case Kind::Bargraph:            drawBargraph(c); return true;
        case Kind::Thermometer:         drawThermometer(c); return true;
        case Kind::Dial:                drawDial(c); return true;
        case Kind::Clock:               drawClock(c); return true;
        case Kind::HourMeter:           drawHourMeter(c); return true;
        case Kind::SevenSegment:        drawSevenSegment(c); return true;
        case Kind::TrendArrow:          drawTrendArrow(c); return true;
        case Kind::Marquee:             drawMarquee(c); return true;
        case Kind::QrCode:              drawQrCode(c); return true;
        case Kind::SystemButton:        drawSystemButton(c); return true;     // lot 10
        default:                        return false;
    }
}

void drawComboList(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto choices = hmi::choicesOf(c.o);
    std::size_t first = 0;
    if (c.opt.runtime) (void)c.opt.runtime->comboOpen(c.o.id, &first);
    const auto l = hmi::comboLayout(c.o, w, h, choices.size(), first);
    const int idx = hmi::choiceIndexOf(choices, value(c));
    const auto panel = boxLocal(l.list, 3);
    fillLocal(c, boxLocal({l.list.x + 3, l.list.y + 4, l.list.w, l.list.h}, 3), fade(gfx::Color{0, 0, 0, 90}, c.alpha));
    fillLocal(c, panel, c.fixed(0x1F252E));
    const double fs = fsOf(c, 16);
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const std::size_t k = l.first + i;
        if (k >= choices.size()) break;
        const auto& b = l.rows[i];
        if (static_cast<int>(k) == idx) fillLocal(c, boxLocal({b.x + 2, b.y + 1, b.w - 4, b.h - 2}, 2), withAlpha(accentOf(c), 0.55f));
        text(c, fitted(c, choices[k].label, b.w - 12, fs), b.x + 4, b.y, b.w - 8, b.h, colorOf(c, "textColor", 0xE6EAF0), fs, "gauche");
    }
    const gfx::Color arrow = c.fixed(0xC8D0DC);
    if (l.up.w > 0) {
        const double s = l.up.h * 0.3;
        triangle(c, l.up.cx() - s, l.up.cy() + s * 0.5, l.up.cx() + s, l.up.cy() + s * 0.5, l.up.cx(), l.up.cy() - s * 0.6,
                 l.first > 0 ? arrow : withAlpha(arrow, 0.3f));
        triangle(c, l.down.cx() - s, l.down.cy() - s * 0.5, l.down.cx() + s, l.down.cy() - s * 0.5, l.down.cx(), l.down.cy() + s * 0.6,
                 l.first + l.rows.size() < choices.size() ? arrow : withAlpha(arrow, 0.3f));
    }
    strokeLocal(c, panel, true, accentOf(c), 1.5);
}

} // namespace app::paint
