// =============================================================================
//  app/hmi/HmiPidPainter.cpp - 1.12.24 : le Reglage PID, sa carte et sa fenetre
// -----------------------------------------------------------------------------
//  LA CARTE (un objet de la vue, tourne et zoome avec elle) : le titre de la
//  boucle, sa pastille (Apprend, Saine, Oscille, Saturee...), la mesure, la
//  consigne et la sortie, les 30 dernieres minutes (la bande de tolerance, la
//  consigne en tirets, la mesure), une phrase sur ce qui se passe, et en pied
//  le modele appris et le reglage conseille. Dans l'editeur : un apercu.
//
//  LA FENETRE (par-dessus la vue, une a la fois) : quatre onglets d'apres la
//  maquette validee - Statistiques, Ce qu'il a appris, Reglage propose,
//  Journal. Elle lit hmi::PidLoop ; ses clics sont des parties (pidPanelHit)
//  que le moteur joue (Runtime::pidPart).
// =============================================================================
#include "HmiPidPainter.hpp"

#include "HmiPaintKit.hpp"
#include "../../hmi/HmiNavigation.hpp"   // approxTextWidth
#include "../../hmi/HmiPidLoop.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app {

namespace {

namespace shapes = ui::shapes;
namespace pid = hmi::pid;

constexpr double kPi = 3.14159265358979323846;

// ---- les couleurs de la maquette (une fenetre claire, comme ses captures) -----------------------
const gfx::Color kInk = gfx::Color::rgb(0x1F2733), kMuted = gfx::Color::rgb(0x5B6675), kDim = gfx::Color::rgb(0x8A94A3);
const gfx::Color kPaper = gfx::Color::rgb(0xFFFFFF), kCard = gfx::Color::rgb(0xF9FAFB), kEdge = gfx::Color::rgb(0xE1E6EC);
const gfx::Color kGrid = gfx::Color::rgb(0xE8ECF1), kAccent = gfx::Color::rgb(0x2B67C6), kAccentSoft = gfx::Color::rgb(0xE8F0FB);
const gfx::Color kPv = gfx::Color::rgb(0x2F7FD8), kSp = gfx::Color::rgb(0xE8743B), kOut = gfx::Color::rgb(0x6E56CF);
const gfx::Color kGreen = gfx::Color::rgb(0x1E9E5A), kAmber = gfx::Color::rgb(0xB7791F), kRed = gfx::Color::rgb(0xD64541);
const gfx::Color kModel = gfx::Color::rgb(0x18A07C), kGray = gfx::Color::rgb(0x8C95A3), kBand = gfx::Color::rgb(0xDDE3EA);

gfx::Color toneColor(int tone) {
    switch (tone) {
        case 1: return kGreen;
        case 2: return kAmber;
        case 3: return kRed;
        default: return kAccent;
    }
}

std::string clockOf(double epoch, bool seconds = false) {
    const auto secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[24];
    std::strftime(b, sizeof b, seconds ? "%H:%M:%S" : "%H:%M", &tm);
    return b;
}

int dayOf(double epoch) {
    const auto secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    return tm.tm_year * 400 + tm.tm_yday;
}

std::string dateOf(double epoch) {
    const auto secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[24];
    std::strftime(b, sizeof b, "%d/%m", &tm);
    return b;
}

// "14:02:00", "hier 13:00", "avant-hier 01:00", "17/10 08:27" : l'heure d'un evenement, vue de `now`.
std::string whenOf(double t, double now) {
    const int d = dayOf(now) - dayOf(t);
    if (d <= 0) return clockOf(t, true);
    if (d == 1) return "hier " + clockOf(t);
    if (d == 2) return "avant-hier " + clockOf(t);
    return dateOf(t) + " " + clockOf(t);
}

// Les decimales d'une mesure : selon son etendue.
int decimalsOf(const hmi::PidLoop& L) {
    const double span = L.bind.pvMax > L.bind.pvMin ? L.bind.pvMax - L.bind.pvMin : std::max(1.0, std::abs(L.sp) * 2);
    return span < 5 ? 2 : span < 500 ? 1 : 0;
}
std::string withUnit(const hmi::PidLoop& L, double v, int extra = 0) {
    const std::string n = hmi::pidNumber(v, std::max(0, decimalsOf(L) + extra));
    return L.bind.unit.empty() ? n : n + " " + L.bind.unit;
}
std::string percent(double v, int decimals = 0) { return hmi::pidNumber(v, decimals) + " %"; }
std::string tolText(const hmi::PidLoop& L) { return "\xC2\xB1" + withUnit(L, L.learner.config().tolerance); }

// ---- dessiner a l'ecran : textes (gras simule), cadres, traits ---------------------------------
struct Pen {
    gfx::IRenderer& r;
    float k{1};      // l'echelle de la fenetre

    [[nodiscard]] gfx::FontId font(double px) const {
        return gfx::FontId{static_cast<std::uint16_t>(std::clamp(px * k, 7.0, 60.0))};
    }
    [[nodiscard]] float width(std::string_view s, double px) const { return r.measure(s, font(px)).width; }
    [[nodiscard]] float lineH(double px) const { return r.lineHeight(font(px)); }
    // Un texte, son haut en y ; coupe a `maxW` (0 : entier) par des points de suite.
    float text(float x, float y, std::string_view s, double px, gfx::Color c, bool bold = false, float maxW = 0) const {
        if (s.empty()) return 0;
        std::string cut;
        const gfx::FontId f = font(px);
        if (maxW > 0 && r.measure(s, f).width > maxW) {
            static constexpr std::string_view kDots = "\xE2\x80\xA6";
            std::size_t n = std::min(s.size(), r.fitCharacters(s, f, std::max(0.f, maxW - r.measure(kDots, f).width)));
            while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
            cut = std::string(s.substr(0, n)) + std::string(kDots);
            s = cut;
        }
        r.drawText({x, y}, s, f, c);
        if (bold) r.drawText({x + 0.6f, y}, s, f, c);
        return r.measure(s, f).width;
    }
    // Dans une boite : 0 a gauche, 1 au centre, 2 a droite ; centre verticalement.
    void textIn(const gfx::Rect& b, std::string_view s, double px, gfx::Color c, int align = 0, bool bold = false) const {
        if (s.empty() || b.w <= 2) return;
        const float w = std::min(width(s, px), b.w);
        const float x = align == 0 ? b.x : align == 1 ? b.x + (b.w - w) / 2 : b.right() - w;
        text(x, b.y + (b.h - lineH(px)) / 2, s, px, c, bold, b.w);
    }
    // Un paragraphe coupe a la largeur ; rend la hauteur prise (au plus `maxH`).
    float para(float x, float y, float w, std::string_view s, double px, gfx::Color c, float maxH = 1e9f, bool bold = false) const {
        const float lh = lineH(px) * 1.18f;
        float used = 0;
        std::string line;
        std::size_t i = 0;
        const auto flush = [&](bool last) {
            if (used + lh > maxH + 0.5f) return false;
            text(x, y + used, line, px, c, bold, last ? w : 0);
            used += lh;
            line.clear();
            return true;
        };
        while (i <= s.size()) {
            const std::size_t sp = s.find(' ', i);
            const std::string_view word = s.substr(i, sp == std::string_view::npos ? std::string_view::npos : sp - i);
            const std::string trial = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && width(trial, px) > w) {
                if (used + 2 * lh > maxH + 0.5f) {   // la derniere ligne qui tient : la suite en points
                    line = trial;
                    while (sp != std::string_view::npos && i < s.size()) {
                        i = sp + 1;
                        break;
                    }
                    text(x, y + used, std::string(line) + std::string(sp == std::string_view::npos ? "" : " \xE2\x80\xA6"), px, c, bold, w);
                    return used + lh;
                }
                if (!flush(false)) return used;
                line = std::string(word);
            } else {
                line = trial;
            }
            if (sp == std::string_view::npos) break;
            i = sp + 1;
        }
        if (!line.empty()) flush(true);
        return used;
    }
    void fill(const gfx::Rect& b, gfx::Color c, float radius = 0) const {
        if (b.w <= 0 || b.h <= 0 || !c.a) return;
        if (radius > 0) r.fillRoundedRect(b, c, std::min(radius, std::min(b.w, b.h) / 2));
        else r.fillRect(b, c);
    }
    void frame(const gfx::Rect& b, gfx::Color c, float radius = 0, float t = 1) const {
        if (b.w <= 0 || b.h <= 0) return;
        if (radius <= 0) { r.strokeRect(b, c, t); return; }
        shapes::strokePolyline(r, shapes::roundedRect(b.x, b.y, b.w, b.h, std::min(radius, std::min(b.w, b.h) / 2)), true, c, t);
    }
    void card(const gfx::Rect& b) const {
        fill(b, kCard, 8 * k);
        frame(b, kEdge, 8 * k);
    }
    void line(gfx::Point a, gfx::Point b, gfx::Color c, float t = 1) const { r.line(a, b, c, std::max(1.f, t)); }
    void dashed(gfx::Point a, gfx::Point b, gfx::Color c, float t = 1, float on = 5, float off = 4) const {
        const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
        if (len < 0.5f) return;
        for (float s = 0; s < len; s += on + off) {
            const float e = std::min(len, s + on);
            r.line({a.x + dx * s / len, a.y + dy * s / len}, {a.x + dx * e / len, a.y + dy * e / len}, c, std::max(1.f, t));
        }
    }
    void polyline(const std::vector<gfx::Point>& pts, gfx::Color c, float t = 1.6f) const {
        if (pts.size() >= 2) shapes::strokePolyline(r, pts, false, c, std::max(1.f, t));
    }
    void dashedPolyline(const std::vector<gfx::Point>& pts, gfx::Color c, float t = 1.4f) const {
        for (std::size_t i = 1; i < pts.size(); ++i) dashed(pts[i - 1], pts[i], c, t, 5 * k, 4 * k);
    }
    // Une pastille : un rond de couleur, un texte, un cadre arrondi ; rend sa largeur.
    float chip(float x, float y, std::string_view s, gfx::Color dot, double px = 12) const {
        const float h = lineH(px) + 8 * k, w = width(s, px) + 28 * k;
        const gfx::Rect b{x, y, w, h};
        fill(b, kPaper, h / 2);
        frame(b, kEdge, h / 2);
        r.fillRoundedRect({x + 9 * k, y + h / 2 - 4 * k, 8 * k, 8 * k}, dot, 4 * k);
        text(x + 22 * k, y + (h - lineH(px)) / 2, s, px, kInk);
        return w;
    }
    // Un bouton ; `primary` : bleu plein.
    gfx::Rect button(float right, float y, std::string_view s, bool primary, double px = 13) const {
        const float h = lineH(px) + 14 * k, w = width(s, px) + 30 * k;
        const gfx::Rect b{right - w, y, w, h};
        fill(b, primary ? kAccent : kPaper, 5 * k);
        if (!primary) frame(b, gfx::Color::rgb(0xC9D1DB), 5 * k);
        textIn(b, s, px, primary ? kPaper : kInk, 1, primary);
        return b;
    }
};

// ---- une courbe dans un cadre ---------------------------------------------------------------
struct Plot {
    gfx::Rect r;
    double x0{0}, x1{1}, y0{0}, y1{1};
    [[nodiscard]] float X(double x) const { return r.x + static_cast<float>((x - x0) / std::max(1e-9, x1 - x0)) * r.w; }
    [[nodiscard]] float Y(double y) const { return r.bottom() - static_cast<float>((y - y0) / std::max(1e-9, y1 - y0)) * r.h; }
};

// Une echelle lisible : des pas de 1, 2 ou 5 (x 10^n) ; quelques graduations.
std::vector<double> ticksOf(double lo, double hi, int want = 4) {
    std::vector<double> out;
    if (!(hi > lo)) return out;
    const double raw = (hi - lo) / std::max(1, want);
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    double step = p;
    for (const double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * p >= raw) { step = m * p; break; }
    for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9 * step; v += step) out.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
    return out;
}

void niceRange(double& lo, double& hi, double minSpan) {
    if (!(hi >= lo)) { lo = 0; hi = 1; }
    if (hi - lo < minSpan) {
        const double c = (hi + lo) / 2;
        lo = c - minSpan / 2;
        hi = c + minSpan / 2;
    }
    const double pad = (hi - lo) * 0.08;
    lo -= pad;
    hi += pad;
}

void gridY(const Pen& p, const Plot& pl, const std::vector<double>& ticks, int decimals, std::string_view unit, bool label = true) {
    for (const double v : ticks) {
        const float y = pl.Y(v);
        if (y < pl.r.y - 0.5f || y > pl.r.bottom() + 0.5f) continue;
        p.line({pl.r.x, y}, {pl.r.right(), y}, kGrid, 1);
        if (!label) continue;
        const std::string t = hmi::pidNumber(v, decimals) + (unit.empty() ? std::string{} : " " + std::string(unit));
        p.text(pl.r.x - p.width(t, 11) - 8 * p.k, y - p.lineH(11) / 2, t, 11, kMuted);
    }
}

// Les heures sous l'axe du temps : `n` graduations.
void timeAxis(const Pen& p, const Plot& pl, int n, bool days) {
    for (int i = 0; i <= n; ++i) {
        const double t = pl.x0 + (pl.x1 - pl.x0) * i / n;
        const std::string s = days ? dateOf(t) : clockOf(t);
        const float w = p.width(s, 11);
        float x = pl.X(t) - w / 2;
        x = std::clamp(x, pl.r.x - 4 * p.k, pl.r.right() - w + 4 * p.k);
        p.text(x, pl.r.bottom() + 5 * p.k, s, 11, kMuted);
    }
}

// La partie cliquable.
void part(PidPanelLayout& l, std::string name, const gfx::Rect& r) { l.parts.emplace_back(std::move(name), r); }

// ---- l'en-tete, les onglets, le pied ---------------------------------------------------------------
std::string bindingsLine(const hmi::PidLoop& L) {
    std::string s;
    const auto add = [&](const std::string& label, const std::string& v) {
        if (v.empty()) return;
        if (!s.empty()) s += " \xC2\xB7 ";
        s += label + v;
    };
    add("", L.bind.sp);
    add("", L.bind.pv);
    add("", L.bind.out);
    if (!L.bind.kp.empty()) add("bloc ", L.bind.kp + (L.bind.ti.empty() ? "" : ", " + L.bind.ti) + (L.bind.td.empty() ? "" : ", " + L.bind.td));
    return s;
}

// La phrase de l'oscillation : "periode 2 min 50 s".
std::string oscillationChip(const pid::Oscillation& o) {
    return "Oscille : p\xC3\xA9riode " + hmi::pidTime(o.period);
}

// ===================================================================== Statistiques ===
void paintStats(const Pen& p, PidPanelLayout& l, const hmi::Runtime& rt, const hmi::PidLoop& L, const gfx::Rect& body) {
    const float k = p.k, gap = 12 * k;
    const auto& w = rt.pidWindow();
    const double window = hmi::pidRangeSeconds(w.range);
    const double now = L.learner.lastT();
    const auto st = L.learner.stats(window);
    const auto osc = L.learner.oscillation(window);
    const auto steps = L.learner.steps(window);
    float y = body.y;
    // Les pastilles, la duree a droite.
    {
        float x = body.x;
        const auto badge = hmi::pidBadge(L);
        x += p.chip(x, y, osc.found ? oscillationChip(osc) : badge.text, toneColor(osc.found ? 3 : badge.tone)) + 8 * k;
        const auto& m = L.learner.model();
        if (m.valid) x += p.chip(x, y, "Mod\xC3\xA8le fiable : " + percent(m.fitLearn), kGreen) + 8 * k;
        else p.chip(x, y, "Mod\xC3\xA8le : il apprend (" + std::to_string(L.learner.learnedCount()) + " \xC3\xA9" "chelon(s) sur 2)", kAmber);
        // 30 min | 8 h | 24 h | 7 jours
        float right = body.right();
        for (int i = 3; i >= 0; --i) {
            const std::string s(hmi::pidRangeName(i));
            const float bw = p.width(s, 13) + 24 * k, bh = p.lineH(13) + 12 * k;
            const gfx::Rect b{right - bw, y, bw, bh};
            p.fill(b, i == w.range ? kAccent : kPaper, 4 * k);
            p.frame(b, i == w.range ? kAccent : kEdge, 4 * k);
            p.textIn(b, s, 13, i == w.range ? kPaper : kInk, 1, i == w.range);
            part(l, "duree:" + std::to_string(i), b);
            right -= bw - 1;
        }
    }
    y += 40 * k;
    // Les six chiffres.
    {
        const float cw = (body.w - 5 * gap) / 6, ch = 92 * k;
        struct Kpi { std::string label, value, note; };
        std::vector<Kpi> kpis;
        kpis.push_back({"\xC3\x89" "cart moyen", st.samples ? withUnit(L, st.meanAbs) : "\xE2\x80\x94", "|consigne \xE2\x88\x92 mesure|"});
        kpis.push_back({"Dans " + tolText(L), st.samples ? percent(st.within) : "\xE2\x80\x94", "du temps"});
        if (!steps.empty()) {
            const auto& s = steps.back();
            kpis.push_back({"D\xC3\xA9passement", percent(s.overshoot), "\xC3\xA0 " + clockOf(s.t) + ", consigne " + withUnit(L, s.from) + " \xE2\x86\x92 " + withUnit(L, s.to)});
        } else {
            kpis.push_back({"D\xC3\xA9passement", "\xE2\x80\x94", "aucun changement de consigne sur " + std::string(hmi::pidRangeName(w.range))});
        }
        kpis.push_back({"Temps de r\xC3\xA9ponse", st.settle > 0 ? hmi::pidTime(st.settle) : "\xE2\x80\x94", "pour rester \xC3\xA0 " + tolText(L)});
        kpis.push_back({"Oscillation", osc.found ? hmi::pidTime(osc.period) : "aucune",
                        osc.found ? "de p\xC3\xA9riode ; chaque vague fait " + percent(osc.decay * 100) + " de la pr\xC3\xA9" "c\xC3\xA9" "dente" : "sur " + std::string(hmi::pidRangeName(w.range))});
        kpis.push_back({"Sortie", hmi::pidNumber(st.travel, 0) + " %/h", "de course, " + hmi::pidNumber(st.reversals, 0) + " inversions par heure"});
        for (std::size_t i = 0; i < kpis.size(); ++i) {
            const gfx::Rect b{body.x + (cw + gap) * static_cast<float>(i), y, cw, ch};
            p.card(b);
            p.text(b.x + 14 * k, b.y + 12 * k, kpis[i].label, 12, kMuted, false, b.w - 20 * k);
            p.text(b.x + 14 * k, b.y + 30 * k, kpis[i].value, 22, kInk, true, b.w - 20 * k);
            p.para(b.x + 14 * k, b.y + 62 * k, b.w - 24 * k, kpis[i].note, 11, kMuted, b.bottom() - b.y - 64 * k);
        }
        y += ch + gap;
    }
    // La courbe de la fenetre : mesure, consigne, bande, sortie (en dessous).
    const float chartH = std::max(160.f, body.bottom() - y - 250 * k - gap);
    {
        const gfx::Rect card{body.x, y, body.w, chartH};
        p.card(card);
        const std::string title = window <= 1800 ? std::string("Les 30 derni\xC3\xA8res minutes")
                                                 : "Les " + std::string(w.range == 3 ? "7 derniers jours" : hmi::pidRangeName(w.range) == std::string_view("8 h") ? "8 derni\xC3\xA8res heures" : "24 derni\xC3\xA8res heures");
        float lx = card.x + 16 * k + p.text(card.x + 16 * k, card.y + 12 * k, title, 14, kInk, true) + 18 * k;
        const auto legend = [&](std::string_view s, gfx::Color c, int style) {
            const float yy = card.y + 12 * k + p.lineH(12) / 2;
            if (style == 1) p.dashed({lx, yy}, {lx + 18 * k, yy}, c, 2 * k, 4 * k, 3 * k);
            else if (style == 2) p.fill({lx, yy - 4 * k, 18 * k, 8 * k}, kBand);
            else p.line({lx, yy}, {lx + 18 * k, yy}, c, 2 * k);
            lx += 24 * k + p.text(lx + 24 * k, card.y + 12 * k, s, 12, kInk) + 16 * k;
        };
        legend("Mesure", kPv, 0);
        legend("Consigne", kSp, 1);
        legend(tolText(L), kBand, 2);
        legend("Sortie", kOut, 0);
        const float left = card.x + 70 * k, right = card.right() - 22 * k;
        const float top = card.y + 44 * k, bottom = card.bottom() - 30 * k;
        const float split = top + (bottom - top) * 0.66f;
        Plot pv{{left, top, right - left, split - top - 16 * k}};
        Plot out{{left, split + 8 * k, right - left, bottom - split - 8 * k}};
        const double t1 = now > 0 ? now : 0, t0 = t1 - window;
        pv.x0 = out.x0 = t0;
        pv.x1 = out.x1 = t1;
        l.chart = {left, top, right - left, bottom - top};
        l.t0 = t0;
        l.t1 = t1;
        // Les points : bruts sur 30 min, par minute au-dela (la mesure : sa moyenne, et son enveloppe).
        struct Pt { double t; float sp, pv, lo, hi, out; };
        std::vector<Pt> pts;
        if (window <= 1800) {
            for (const auto& q : L.learner.recent())
                if (q.t >= t0) pts.push_back({q.t, q.sp, q.pv, q.pv, q.pv, q.out});
        } else {
            for (const auto& m : L.learner.minutes(window)) pts.push_back({m.t, m.sp, m.pv, m.pvMin, m.pvMax, m.out});
        }
        // Pas plus de deux points par pixel.
        if (pts.size() > static_cast<std::size_t>(pv.r.w * 2)) {
            std::vector<Pt> thin;
            const double perPx = (t1 - t0) / std::max(1.f, pv.r.w * 2);
            double next = t0;
            for (const auto& q : pts)
                if (q.t >= next) { thin.push_back(q); next = q.t + perPx; }
            pts.swap(thin);
        }
        double lo = 1e300, hi = -1e300;
        const double tol = L.learner.config().tolerance;
        for (const auto& q : pts) {
            lo = std::min({lo, static_cast<double>(q.lo), q.sp - tol});
            hi = std::max({hi, static_cast<double>(q.hi), q.sp + tol});
        }
        if (pts.empty()) { lo = L.sp - 1; hi = L.sp + 1; }
        niceRange(lo, hi, tol * 4);
        pv.y0 = lo;
        pv.y1 = hi;
        out.y0 = L.learner.config().outMin;
        out.y1 = L.learner.config().outMax;
        gridY(p, pv, ticksOf(lo, hi, 4), decimalsOf(L), L.bind.unit);
        gridY(p, out, ticksOf(out.y0, out.y1, 2), 0, "%");
        if (!pts.empty()) {
            // La bande de tolerance autour de la consigne, puis la consigne (en marches), la mesure.
            for (std::size_t i = 1; i < pts.size(); ++i) {
                const float xa = pv.X(pts[i - 1].t), xb = pv.X(pts[i].t);
                const float ya = pv.Y(pts[i - 1].sp + tol), yb = pv.Y(pts[i - 1].sp - tol);
                p.fill({xa, ya, std::max(1.f, xb - xa), yb - ya}, kBand.withAlpha(190));
            }
            std::vector<gfx::Point> sp, mes, op, env;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                if (i > 0) sp.push_back({pv.X(pts[i].t), pv.Y(pts[i - 1].sp)});
                sp.push_back({pv.X(pts[i].t), pv.Y(pts[i].sp)});
                mes.push_back({pv.X(pts[i].t), pv.Y(pts[i].pv)});
                op.push_back({out.X(pts[i].t), out.Y(pts[i].out)});
            }
            if (window > 1800)
                for (const auto& q : pts)
                    if (q.hi - q.lo > 0) p.line({pv.X(q.t), pv.Y(q.lo)}, {pv.X(q.t), pv.Y(q.hi)}, kPv.withAlpha(60), 1);
            p.dashedPolyline(sp, kSp, 1.6f * k);
            p.polyline(mes, kPv, 2.0f * k);
            p.polyline(op, kOut, 1.8f * k);
        }
        p.line({left, out.r.bottom()}, {right, out.r.bottom()}, kEdge, 1);
        timeAxis(p, out, 6, w.range == 3);
        // Les changements de consigne et les perturbations : un trait pointille et ce qui s'est passe.
        float labelY = pv.r.bottom() - p.lineH(12) - 4 * k;
        for (const auto& e : L.learner.events()) {
            if (e.t < t0 || e.t > t1) continue;
            if (e.kind != pid::EventKind::SetpointStep && e.kind != pid::EventKind::Disturbance) continue;
            const float x = pv.X(e.t);
            p.dashed({x, pv.r.y}, {x, out.r.bottom()}, kGray, 1, 3 * k, 3 * k);
            const std::string s = e.kind == pid::EventKind::SetpointStep
                                      ? clockOf(e.t) + " consigne " + withUnit(L, e.a) + " \xE2\x86\x92 " + withUnit(L, e.b)
                                      : clockOf(e.t) + " perturbation";
            if (window <= 8 * 3600) p.text(x + 4 * k, labelY, s, 12, kMuted, false, right - x - 6 * k);
        }
        // Le survol : l'instant, les valeurs.
        if (w.hoverT >= t0 && w.hoverT <= t1 && !pts.empty()) {
            const auto it = std::min_element(pts.begin(), pts.end(), [&](const Pt& a, const Pt& b) { return std::abs(a.t - w.hoverT) < std::abs(b.t - w.hoverT); });
            const float x = pv.X(it->t);
            p.line({x, pv.r.y}, {x, out.r.bottom()}, kMuted, 1);
            p.fill({x - 4 * k, pv.Y(it->pv) - 4 * k, 8 * k, 8 * k}, kPv, 4 * k);
            p.fill({x - 4 * k, pv.Y(it->sp) - 4 * k, 8 * k, 8 * k}, kSp, 4 * k);
            const std::vector<std::pair<std::string, gfx::Color>> rows = {
                {"Mesure " + withUnit(L, it->pv), kPv}, {"Consigne " + withUnit(L, it->sp), kSp}, {"Sortie " + percent(it->out, 1), kOut}};
            float tw = p.width(clockOf(it->t, true), 14);
            for (const auto& r2 : rows) tw = std::max(tw, p.width(r2.first, 14) + 22 * k);
            const float bw = tw + 28 * k, bh = 4 * p.lineH(14) + 22 * k;
            float bx = x + 14 * k;
            if (bx + bw > right) bx = x - 14 * k - bw;
            const gfx::Rect tip{bx, pv.r.y + 6 * k, bw, bh};
            p.fill(tip, kPaper, 6 * k);
            p.frame(tip, gfx::Color::rgb(0xC9D1DB), 6 * k);
            p.text(tip.x + 14 * k, tip.y + 10 * k, clockOf(it->t, true), 14, kInk, true);
            float ry = tip.y + 10 * k + p.lineH(14);
            for (const auto& r2 : rows) {
                p.fill({tip.x + 14 * k, ry + p.lineH(14) / 2 - 1.5f * k, 12 * k, 3 * k}, r2.second);
                p.text(tip.x + 32 * k, ry, r2.first, 14, kInk);
                ry += p.lineH(14);
            }
        }
        y += chartH + gap;
    }
    // Ou se trouve l'ecart (l'histogramme), et le tableau par periode.
    {
        const float h = body.bottom() - y;
        const float wl = (body.w - gap) * 0.42f;
        const gfx::Rect hc{body.x, y, wl, h};
        p.card(hc);
        p.text(hc.x + 16 * k, hc.y + 12 * k, "O\xC3\xB9 se trouve l'\xC3\xA9" "cart", 14, kInk, true);
        Plot hp{{hc.x + 52 * k, hc.y + 44 * k, hc.w - 72 * k, hc.h - 100 * k}};
        double top = 0;
        for (const double v : st.hist) top = std::max(top, v);
        hp.x0 = 0;
        hp.x1 = pid::kHistBins;
        hp.y0 = 0;
        hp.y1 = std::max(10.0, std::ceil(top / 10.0) * 10.0);
        gridY(p, hp, ticksOf(0, hp.y1, 3), 0, "%");
        for (int i = 0; i < pid::kHistBins; ++i) {
            const float x0 = hp.X(i) + 1.5f * k, x1 = hp.X(i + 1) - 1.5f * k;
            const float yy = hp.Y(st.hist[static_cast<std::size_t>(i)]);
            const bool centre = i == pid::kHistBins / 2 - 1 || i == pid::kHistBins / 2;
            if (centre) p.fill({hp.X(i), hp.r.y, hp.X(i + 1) - hp.X(i), hp.r.h}, kAccentSoft);
            p.fill({x0, yy, x1 - x0, hp.r.bottom() - yy}, kPv, 2 * k);
            if (st.hist[static_cast<std::size_t>(i)] >= 10) {
                const std::string s = percent(st.hist[static_cast<std::size_t>(i)]);
                p.text((x0 + x1) / 2 - p.width(s, 11) / 2, yy - p.lineH(11) - 1, s, 11, kInk);
            }
        }
        // L'axe : de -4 a +4 tolerances.
        const double tolv = L.learner.config().tolerance;
        for (int j = -4; j <= 4; j += 2) {
            const std::string s = (j > 0 ? "+" : "") + hmi::pidNumber(j * tolv, decimalsOf(L));
            const float x = hp.X(pid::kHistBins / 2.0 + j * 2.0);
            p.text(x - p.width(s, 11) / 2, hp.r.bottom() + 5 * k, s, 11, kMuted);
        }
        p.para(hc.x + 16 * k, hp.r.bottom() + 24 * k, hc.w - 32 * k,
               "\xC3\xA9" "cart = consigne \xE2\x88\x92 mesure" + std::string(L.bind.unit.empty() ? "" : " (" + L.bind.unit + ")") + " ; n\xC3\xA9gatif : la mesure d\xC3\xA9passe",
               11, kMuted, 30 * k);
        // Par periode.
        const gfx::Rect tc{hc.right() + gap, y, body.right() - hc.right() - gap, h};
        p.card(tc);
        p.text(tc.x + 16 * k, tc.y + 12 * k, "Par p\xC3\xA9riode", 14, kInk, true);
        std::array<pid::WindowStats, 4> ws;
        for (int i = 0; i < 4; ++i) ws[static_cast<std::size_t>(i)] = i == w.range ? st : L.learner.stats(hmi::pidRangeSeconds(i));
        struct Row { std::string label; std::array<std::string, 4> cells; };
        std::vector<Row> rows;
        const auto per = [&](const std::string& label, auto f) {
            Row r2{label, {}};
            for (std::size_t i = 0; i < 4; ++i) r2.cells[i] = ws[i].samples ? f(ws[i]) : std::string("\xE2\x80\x94");
            rows.push_back(std::move(r2));
        };
        per("\xC3\x89" "cart moyen", [&](const pid::WindowStats& s) { return withUnit(L, s.meanAbs); });
        per("Dans " + tolText(L), [&](const pid::WindowStats& s) { return percent(s.within, 1); });
        per("D\xC3\xA9passement moyen (\xC3\xA9" "chelons)",
            [&](const pid::WindowStats& s) { return s.steps > 0 && s.overshoot >= 0 ? percent(s.overshoot) + " (" + std::to_string(s.steps) + ")" : std::string("\xE2\x80\x94"); });
        per("Temps de r\xC3\xA9ponse moyen", [&](const pid::WindowStats& s) { return s.settle > 0 ? hmi::pidTime(s.settle) : std::string("\xE2\x80\x94"); });
        per("Course de la sortie", [&](const pid::WindowStats& s) { return hmi::pidNumber(s.travel, 0) + " %/h"; });
        per("Inversions de la sortie (> 1 %)", [&](const pid::WindowStats& s) { return hmi::pidNumber(s.reversals, 1) + " /h"; });
        per("Sortie satur\xC3\xA9" "e", [&](const pid::WindowStats& s) { return percent(s.saturated, 2); });
        per("En AUTO", [&](const pid::WindowStats& s) { return percent(s.autoPct, 1); });
        per("Indice de performance (1 = le mieux possible)", [&](const pid::WindowStats& s) { return s.harris >= 0 ? hmi::pidNumber(s.harris, 2) : std::string("\xE2\x80\x94"); });
        const float rowH = std::min(30 * k, (tc.h - 100 * k) / static_cast<float>(rows.size() + 1));
        const float x0 = tc.x + 16 * k, labelW = (tc.w - 32 * k) * 0.43f, colW = (tc.w - 32 * k - labelW) / 4;
        float ry = tc.y + 40 * k;
        p.fill({x0, ry, tc.w - 32 * k, rowH}, gfx::Color::rgb(0xEEF2F6));
        p.fill({x0 + labelW + colW * static_cast<float>(w.range), ry, colW, rowH * static_cast<float>(rows.size() + 1)}, kAccentSoft);
        p.textIn({x0 + 8 * k, ry, labelW, rowH}, "Indicateur", 12, kInk, 0, true);
        for (int i = 0; i < 4; ++i) p.textIn({x0 + labelW + colW * static_cast<float>(i), ry, colW - 8 * k, rowH}, hmi::pidRangeName(i), 12, kInk, 2, true);
        ry += rowH;
        for (const auto& r2 : rows) {
            p.line({x0, ry}, {x0 + tc.w - 32 * k, ry}, kGrid, 1);
            p.textIn({x0 + 8 * k, ry, labelW - 8 * k, rowH}, r2.label, 12, kInk);
            for (std::size_t i = 0; i < 4; ++i) p.textIn({x0 + labelW + colW * static_cast<float>(i), ry, colW - 8 * k, rowH}, r2.cells[i], 12, kInk, 2);
            ry += rowH;
        }
        p.para(x0, ry + 8 * k, tc.w - 32 * k,
               "L'indice compare la variance de l'\xC3\xA9" "cart \xC3\xA0 la plus petite possible avec ce retard (indice de Harris), hors changements "
               "de consigne. Il faut au moins 20 min de r\xC3\xA9gime \xC3\xA9tabli.",
               11, kMuted, tc.bottom() - ry - 12 * k);
    }
}

// ================================================================= Ce qu'il a appris ===
std::string verdictText(pid::Verdict v) {
    switch (v) {
        case pid::Verdict::Learned: return "Appris";
        case pid::Verdict::Check: return "V\xC3\xA9rification";
        case pid::Verdict::Discarded: return "\xC3\x89" "cart\xC3\xA9";
        case pid::Verdict::StatsOnly: return "Statistiques";
        default: return {};
    }
}
gfx::Color verdictColor(pid::Verdict v) {
    switch (v) {
        case pid::Verdict::Learned: return kGreen;
        case pid::Verdict::Check: return kAccent;
        case pid::Verdict::Discarded: return kAmber;
        default: return kGray;
    }
}

// Ce qu'il a vu, en deux lignes : le titre, l'explication.
std::pair<std::string, std::string> seenText(const hmi::PidLoop& L, const pid::Event& e) {
    using K = pid::EventKind;
    using R = pid::Reason;
    const auto dur = [&] { return e.t2 > e.t ? hmi::pidTime(e.t2 - e.t) : std::string("un instant"); };
    std::string title, why;
    switch (e.kind) {
        case K::SetpointStep: title = "Consigne " + withUnit(L, e.a) + " \xE2\x86\x92 " + withUnit(L, e.b); break;
        case K::ManualStep: title = "MANU : sortie " + percent(e.a, 1) + " \xE2\x86\x92 " + percent(e.b, 1); break;
        case K::Disturbance:
            title = "Perturbation : la sortie a compens\xC3\xA9 " + std::string(e.b >= 0 ? "+" : "") + percent(e.b, 1);
            why = "Non mesur\xC3\xA9" "e : elle ne dit rien du proc\xC3\xA9" "d\xC3\xA9. Compt\xC3\xA9" "e dans les statistiques.";
            break;
        case K::Saturation: title = "Sortie en but\xC3\xA9" "e (" + percent(e.a) + ") pendant " + dur(); why = "Le r\xC3\xA9glage n'y peut rien : il manque de la puissance."; break;
        case K::Gap: title = dur() + " sans donn\xC3\xA9" "es"; why = "La communication n'a rien rendu : le trou coupe l'enregistrement en deux."; break;
        case K::Frozen: title = "Mesure fig\xC3\xA9" "e \xC3\xA0 " + withUnit(L, e.a); why = "La sortie bougeait, la mesure non : capteur ou liaison ?"; break;
        default: return {};
    }
    if (why.empty())
        switch (e.reason) {
            case R::Clean: why = "Propre : " + dur() + " sans perturbation."; break;
            case R::OpenLoop: why = "Boucle ouverte : le plus riche \xC3\xA0 apprendre, le r\xC3\xA9gulateur ne s'en m\xC3\xAAle pas."; break;
            case R::KeptForCheck: why = "Gard\xC3\xA9 de c\xC3\xB4t\xC3\xA9 pour v\xC3\xA9rifier le mod\xC3\xA8le" + (e.fit >= 0 ? " : il le pr\xC3\xA9" "dit \xC3\xA0 " + percent(e.fit, 1) + "." : std::string(".")); break;
            case R::DisturbanceInProgress:
                why = "Une perturbation \xC3\xA9tait en cours" + (e.since > 0 ? " (depuis " + clockOf(e.since) + ")" : std::string{})
                    + " : on ne sait pas ce qui vient de la consigne.";
                break;
            case R::Saturated: why = "La sortie \xC3\xA9tait en but\xC3\xA9" "e : la r\xC3\xA9ponse ne dit rien du proc\xC3\xA9" "d\xC3\xA9."; break;
            case R::Gap: why = "Un trou de donn\xC3\xA9" "es le coupe."; break;
            case R::Frozen: why = "La mesure \xC3\xA9tait fig\xC3\xA9" "e."; break;
            case R::PoorFit: why = "Le mod\xC3\xA8le ne l'explique pas" + (e.fit >= 0 ? " (" + percent(e.fit) + ")" : std::string{}) + " : gard\xC3\xA9 hors apprentissage."; break;
            case R::TooSmall: why = "Trop petit devant le bruit de mesure."; break;
            case R::TooShort: why = "Trop court : la r\xC3\xA9ponse n'\xC3\xA9tait pas finie."; break;
            case R::NotLearning: why = "Apprendre est r\xC3\xA9gl\xC3\xA9 pour ne pas apprendre ici : les statistiques seules."; break;
            default: break;
        }
    return {title, why};
}

void paintSegment(const Pen& p, const gfx::Rect& area, const hmi::PidLoop& L, const pid::Learner::SegmentView& seg) {
    const float k = p.k;
    const float left = area.x + 52 * k, right = area.right() - 14 * k;
    const float split = area.y + area.h * 0.62f;
    Plot py{{left, area.y + 26 * k, right - left, split - area.y - 34 * k}};
    Plot pu{{left, split + 10 * k, right - left, area.bottom() - split - 32 * k}};
    const double dur = seg.dt * static_cast<double>(std::max<std::size_t>(1, seg.y.size()));
    py.x0 = pu.x0 = seg.t0;
    py.x1 = pu.x1 = seg.t0 + dur;
    double lo = 1e300, hi = -1e300;
    for (const float v : seg.y) { lo = std::min(lo, static_cast<double>(v)); hi = std::max(hi, static_cast<double>(v)); }
    for (const float v : seg.yhat) { lo = std::min(lo, static_cast<double>(v)); hi = std::max(hi, static_cast<double>(v)); }
    niceRange(lo, hi, L.learner.config().tolerance * 2);
    py.y0 = lo;
    py.y1 = hi;
    pu.y0 = L.learner.config().outMin;
    pu.y1 = L.learner.config().outMax;
    gridY(p, py, ticksOf(lo, hi, 3), decimalsOf(L), L.bind.unit);
    gridY(p, pu, ticksOf(pu.y0, pu.y1, 2), 0, "%");
    std::vector<gfx::Point> y, yh, u;
    for (std::size_t i = 0; i < seg.y.size(); ++i) y.push_back({py.X(seg.t0 + seg.dt * static_cast<double>(i)), py.Y(seg.y[i])});
    for (std::size_t i = 0; i < seg.yhat.size(); ++i) yh.push_back({py.X(seg.t0 + seg.dt * static_cast<double>(i)), py.Y(seg.yhat[i])});
    for (std::size_t i = 0; i < seg.u.size(); ++i) u.push_back({pu.X(seg.t0 + seg.dt * static_cast<double>(i)), pu.Y(seg.u[i])});
    p.polyline(y, kPv, 2 * k);
    p.dashedPolyline(yh, kModel, 1.8f * k);
    p.polyline(u, kOut, 1.6f * k);
    timeAxis(p, pu, 5, false);
    // Le pic mesure et prevu.
    if (!seg.y.empty() && seg.y.size() == seg.yhat.size()) {
        const bool up = seg.y.back() >= seg.y.front();
        std::size_t im = 0;
        for (std::size_t i = 1; i < seg.y.size(); ++i)
            if (up ? seg.y[i] > seg.y[im] : seg.y[i] < seg.y[im]) im = i;
        double err = 0;
        for (std::size_t i = 0; i < seg.y.size(); ++i) err += std::abs(seg.y[i] - seg.yhat[i]);
        err /= static_cast<double>(seg.y.size());
        const std::string s = "pic : mesur\xC3\xA9 " + withUnit(L, seg.y[im]) + ", pr\xC3\xA9vu " + withUnit(L, seg.yhat[im]) + " \xC2\xB7 \xC3\xA9" "cart moyen " + withUnit(L, err, 1);
        const float x = std::min(py.X(seg.t0 + seg.dt * static_cast<double>(im)) + 6 * k, right - p.width(s, 11));
        p.text(x, py.r.y - p.lineH(11) - 2 * k, s, 11, kInk);
    }
}

void paintLearned(const Pen& p, PidPanelLayout& l, const hmi::Runtime& rt, const hmi::PidLoop& L, const gfx::Rect& body) {
    (void)l;
    (void)rt;
    const float k = p.k, gap = 12 * k;
    const auto& m = L.learner.model();
    const float topH = std::min(400 * k, body.h * 0.52f);
    // ---- Le procede, tel qu'il l'a appris
    const gfx::Rect left{body.x, body.y, (body.w - gap) * 0.42f, topH};
    p.card(left);
    p.text(left.x + 16 * k, left.y + 14 * k, "Le proc\xC3\xA9" "d\xC3\xA9, tel qu'il l'a appris", 14, kInk, true);
    float y = left.y + 40 * k;
    if (!m.valid) {
        const int n = L.learner.learnedCount();
        p.para(left.x + 16 * k, y, left.w - 32 * k,
               "Il ne l'a pas encore appris : " + std::to_string(n) + " \xC3\xA9" "chelon(s) appris sur les 2 qu'il lui faut. Un changement de consigne "
               "de quelques degr\xC3\xA9s, ou un mouvement de la sortie en MANU, sans perturbation, lui suffit. Les statistiques, elles, sont d\xC3\xA9j\xC3\xA0 l\xC3\xA0.",
               13, kInk, left.bottom() - y - 16 * k);
    } else {
        const bool second = m.tau2 > 0;
        const std::string sens = m.k >= 0 ? "sens direct" : "sens inverse";
        p.para(left.x + 16 * k, y, left.w - 32 * k,
               std::string(second ? "Deux constantes de temps et un retard" : "Premier ordre avec retard") + " \xC2\xB7 " +
                   (m.integrating() ? "int\xC3\xA9grateur" : "stable") + " \xC2\xB7 " + sens +
                   ". Il pr\xC3\xA9" "dit la mesure \xC3\xA0 partir de la sortie ; c'est ce mod\xC3\xA8le qui calcule le r\xC3\xA9glage.",
               12, kMuted, 40 * k);
        y += 40 * k;
        const gfx::Rect formula{left.x + 16 * k, y, left.w - 32 * k, 30 * k};
        p.fill(formula, kPaper, 4 * k);
        p.frame(formula, kEdge, 4 * k);
        p.textIn({formula.x + 10 * k, formula.y, formula.w - 20 * k, formula.h},
                 second ? "mesure = K \xC2\xB7 sortie retard\xC3\xA9" "e de \xCE\xB8, liss\xC3\xA9" "e par \xCF\x84 puis \xCF\x84" "2"
                        : "mesure = K \xC2\xB7 sortie retard\xC3\xA9" "e de \xCE\xB8, liss\xC3\xA9" "e par \xCF\x84",
                 12, kInk);
        y += 40 * k;
        struct Box { std::string label, value, note; };
        std::vector<Box> boxes = {
            {"Gain K", hmi::pidNumber(m.k, 3), (L.bind.unit.empty() ? std::string("mesure") : L.bind.unit) + " par % de sortie \xC2\xB7 \xC2\xB1 " + percent(std::abs(m.k) > 0 ? 100 * m.kHalf / std::abs(m.k) : 0, 1)},
            {"Constante de temps \xCF\x84", hmi::pidTime(m.tau), "\xC2\xB1 " + hmi::pidTime(m.tauHalf)},
            {"Retard \xCE\xB8", hmi::pidTime(m.theta), "\xC2\xB1 " + hmi::pidTime(m.thetaHalf)}};
        if (second) boxes.push_back({"Seconde constante \xCF\x84" "2", hmi::pidTime(m.tau2), "\xC2\xB1 " + hmi::pidTime(m.tau2Half)});
        const float bw = (left.w - 32 * k - gap) / 2, bh = 70 * k;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const gfx::Rect b{left.x + 16 * k + (bw + gap) * static_cast<float>(i % 2), y + (bh + 10 * k) * static_cast<float>(i / 2), bw, bh};
            p.fill(b, kPaper, 6 * k);
            p.frame(b, kEdge, 6 * k);
            p.text(b.x + 12 * k, b.y + 8 * k, boxes[i].label, 12, kMuted, false, b.w - 16 * k);
            p.text(b.x + 12 * k, b.y + 24 * k, boxes[i].value, 19, kInk, true, b.w - 16 * k);
            p.text(b.x + 12 * k, b.y + 50 * k, boxes[i].note, 11, kMuted, false, b.w - 16 * k);
        }
        y += (bh + 10 * k) * static_cast<float>((boxes.size() + 1) / 2) + 4 * k;
        const auto bar = [&](const std::string& label, double v) {
            if (y + 30 * k > left.bottom()) return;
            p.text(left.x + 16 * k, y, label, 12, kInk, false, left.w - 90 * k);
            const std::string s = percent(v, 1);
            p.text(left.right() - 16 * k - p.width(s, 12), y, s, 12, kInk, true);
            const gfx::Rect track{left.x + 16 * k, y + 20 * k, left.w - 32 * k, 6 * k};
            p.fill(track, kGrid, 3 * k);
            p.fill({track.x, track.y, track.w * static_cast<float>(std::clamp(v, 0.0, 100.0) / 100.0), track.h}, kAccent, 3 * k);
            y += 34 * k;
        };
        bar("Colle \xC3\xA0 ce qu'il a appris", m.fitLearn);
        if (m.fitCheck >= 0) bar("Sur un \xC3\xA9" "chelon gard\xC3\xA9 pour v\xC3\xA9rifier (" + clockOf(m.checkAt, true) + ")", m.fitCheck);
    }
    // ---- Mesure et modele : le dernier echelon appris
    const gfx::Rect right{left.right() + gap, body.y, body.right() - left.right() - gap, topH};
    p.card(right);
    const auto seg = L.learner.lastLearned();
    float lx = right.x + 16 * k;
    lx += p.text(lx, right.y + 14 * k, seg ? "Mesure et mod\xC3\xA8le : l'\xC3\xA9" "chelon de " + clockOf(seg->t0 + seg->onset) : std::string("Mesure et mod\xC3\xA8le"), 14, kInk, true) + 16 * k;
    {
        const float yy = right.y + 14 * k + p.lineH(12) / 2;
        p.line({lx, yy}, {lx + 16 * k, yy}, kPv, 2 * k);
        lx += 22 * k + p.text(lx + 22 * k, right.y + 14 * k, "Mesure", 12, kInk) + 14 * k;
        p.dashed({lx, yy}, {lx + 16 * k, yy}, kModel, 2 * k, 4 * k, 3 * k);
        lx += 22 * k + p.text(lx + 22 * k, right.y + 14 * k, "Mod\xC3\xA8le (pr\xC3\xA9vu depuis la sortie)", 12, kInk) + 14 * k;
        p.line({lx, yy}, {lx + 16 * k, yy}, kOut, 2 * k);
        p.text(lx + 22 * k, right.y + 14 * k, "Sortie", 12, kInk);
    }
    if (seg && !seg->y.empty()) paintSegment(p, {right.x, right.y + 40 * k, right.w, right.h - 46 * k}, L, *seg);
    else p.para(right.x + 16 * k, right.y + 60 * k, right.w - 32 * k, "Le premier \xC3\xA9" "chelon appris se montrera ici : la mesure, et ce que le mod\xC3\xA8le en pr\xC3\xA9" "dit depuis la sortie.", 13, kMuted);
    // ---- Ce qu'il a vu
    const float by = body.y + topH + gap, bh = body.bottom() - by;
    const gfx::Rect seen{body.x, by, (body.w - gap) * 0.64f, bh};
    p.card(seen);
    p.text(seen.x + 16 * k, seen.y + 14 * k, "Ce qu'il a vu", 14, kInk, true);
    {
        float ry = seen.y + 42 * k;
        const float rowH = 46 * k;
        int shownRows = 0, stepsSeen = 0, learned = 0, discarded = 0;
        const double nowT = L.learner.lastT();
        for (auto it = L.learner.events().rbegin(); it != L.learner.events().rend(); ++it) {
            const auto [title, why] = seenText(L, *it);
            if (title.empty()) continue;
            if (it->kind == pid::EventKind::SetpointStep || it->kind == pid::EventKind::ManualStep) {
                ++stepsSeen;
                if (it->verdict == pid::Verdict::Learned) ++learned;
                if (it->verdict == pid::Verdict::Discarded) ++discarded;
            }
            if (ry + rowH > seen.bottom() - 30 * k) continue;
            ++shownRows;
            p.text(seen.x + 16 * k, ry, whenOf(it->t, nowT), 12, kMuted, false, 110 * k);
            const std::string badge = verdictText(it->verdict);
            const float badgeW = badge.empty() ? 0 : p.width(badge, 12) + 34 * k;
            p.text(seen.x + 132 * k, ry, title, 13, kInk, false, seen.w - 160 * k - badgeW);
            p.para(seen.x + 132 * k, ry + p.lineH(13) + 1, seen.w - 160 * k - badgeW, why, 12, kMuted, rowH - p.lineH(13) - 4 * k);
            if (!badge.empty()) {
                const float bhh = p.lineH(12) + 6 * k;
                const gfx::Rect b{seen.right() - 16 * k - badgeW, ry - 2 * k, badgeW, bhh};
                p.fill(b, kPaper, bhh / 2);
                p.frame(b, gfx::Color::rgb(0xD5DCE4), bhh / 2);
                p.fill({b.x + 9 * k, b.y + bhh / 2 - 3.5f * k, 7 * k, 7 * k}, verdictColor(it->verdict), 3.5f * k);
                p.text(b.x + 22 * k, b.y + 3 * k, badge, 12, kInk);
            }
            p.line({seen.x + 16 * k, ry + rowH - 6 * k}, {seen.right() - 16 * k, ry + rowH - 6 * k}, kGrid, 1);
            ry += rowH;
        }
        if (shownRows == 0)
            p.para(seen.x + 16 * k, ry, seen.w - 32 * k, "Rien encore : les changements de consigne, les mouvements en MANU, les perturbations et les trous s'\xC3\xA9" "crivent ici.", 13, kMuted);
        const std::string sum = std::to_string(static_cast<int>(L.learner.config().memoryDays)) + " jours : " + std::to_string(stepsSeen)
                              + " \xC3\xA9" "chelon(s) vu(s), " + std::to_string(learned) + " appris, " + std::to_string(discarded) + " \xC3\xA9" "cart\xC3\xA9(s).";
        p.text(seen.x + 16 * k, seen.bottom() - 24 * k, sum, 12, kMuted, false, seen.w - 32 * k);
    }
    // ---- Le gain jour apres jour, la qualite des donnees
    const gfx::Rect gainCard{seen.right() + gap, by, body.right() - seen.right() - gap, bh * 0.56f};
    p.card(gainCard);
    const auto days = L.learner.dailyGains();
    std::string trend = "Le gain du proc\xC3\xA9" "d\xC3\xA9";
    double change = 0;
    if (days.size() >= 2 && std::abs(days.front().second) > 1e-12) {
        change = 100.0 * (days.back().second - days.front().second) / std::abs(days.front().second);
        trend += std::abs(change) < 3 ? " est stable" : change > 0 ? " monte" : " baisse";
    }
    p.text(gainCard.x + 16 * k, gainCard.y + 14 * k, trend, 14, kInk, true, gainCard.w - 32 * k);
    if (days.size() >= 2) {
        Plot gp{{gainCard.x + 52 * k, gainCard.y + 44 * k, gainCard.w - 72 * k, gainCard.h - 110 * k}};
        double lo = 1e300, hi = -1e300;
        for (const auto& d : days) { lo = std::min(lo, d.second); hi = std::max(hi, d.second); }
        niceRange(lo, hi, std::max(1e-6, std::abs(hi) * 0.05));
        gp.x0 = days.front().first;
        gp.x1 = std::max(days.back().first, days.front().first + 86400.0);
        gp.y0 = lo;
        gp.y1 = hi;
        gridY(p, gp, ticksOf(lo, hi, 3), 2, {});
        std::vector<gfx::Point> pts;
        for (const auto& d : days) pts.push_back({gp.X(d.first), gp.Y(d.second)});
        p.polyline(pts, kPv, 2 * k);
        for (const auto& q : pts) p.fill({q.x - 3.5f * k, q.y - 3.5f * k, 7 * k, 7 * k}, kPv, 3.5f * k);
        const int nd = static_cast<int>(days.size());
        for (int i = 0; i < nd; ++i) {
            if (nd > 4 && i % 2 == 1 && i != nd - 1) continue;
            const std::string s = i == nd - 1 ? std::string("auj.") : "J-" + std::to_string(nd - 1 - i);
            p.text(pts[static_cast<std::size_t>(i)].x - p.width(s, 11) / 2, gp.r.bottom() + 5 * k, s, 11, kMuted);
        }
        p.para(gainCard.x + 16 * k, gp.r.bottom() + 24 * k, gainCard.w - 32 * k,
               std::string(change >= 0 ? "+" : "") + percent(change, 1) + " en " + std::to_string(nd) + " jours (" + hmi::pidNumber(days.front().second, 3) + " \xE2\x86\x92 "
                   + hmi::pidNumber(days.back().second, 3) + ")" + (std::abs(change) >= 3 ? " : le proc\xC3\xA9" "d\xC3\xA9 change ; le r\xC3\xA9glage propos\xC3\xA9 le suit." : "."),
               12, kMuted, gainCard.bottom() - gp.r.bottom() - 28 * k);
    } else {
        p.para(gainCard.x + 16 * k, gainCard.y + 44 * k, gainCard.w - 32 * k, "Jour apr\xC3\xA8s jour, le gain appris se montrera ici (un point par jour o\xC3\xB9 il a vu un \xC3\xA9" "chelon).", 12, kMuted);
    }
    const gfx::Rect quality{gainCard.x, gainCard.bottom() + gap, gainCard.w, body.bottom() - gainCard.bottom() - gap};
    p.card(quality);
    p.text(quality.x + 16 * k, quality.y + 14 * k, "La qualit\xC3\xA9 des donn\xC3\xA9" "es", 14, kInk, true);
    const auto s24 = L.learner.stats(86400.0);
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"P\xC3\xA9riode des donn\xC3\xA9" "es", hmi::pidTime(L.learner.config().period)},
        {"R\xC3\xA9solution de la mesure", L.learner.resolution() > 0 ? withUnit(L, L.learner.resolution(), 1) : std::string("continue")},
        {"Bruit de mesure", "\xC2\xB1" + withUnit(L, L.learner.noise(), 1)},
        {"Trous en 24 h", std::to_string(s24.gaps)},
        {"Proc\xC3\xA9" "d\xC3\xA9", m.valid ? std::string(m.integrating() ? "int\xC3\xA9grateur" : "stable") + ", sens " + (m.k >= 0 ? "direct" : "inverse") : std::string("pas encore appris")}};
    float ry = quality.y + 40 * k;
    for (const auto& [a, b] : rows) {
        if (ry + p.lineH(12) > quality.bottom() - 6 * k) break;
        p.text(quality.x + 16 * k, ry, a, 12, kInk, false, quality.w * 0.55f);
        p.text(quality.right() - 16 * k - p.width(b, 12), ry, b, 12, kInk);
        ry += p.lineH(12) + 6 * k;
    }
}

// ================================================================== Reglage propose ===
struct Preview {
    std::array<std::optional<pid::Trace>, 4> traces;   // Actuel, Robuste, Equilibre, Rapide
    std::array<pid::Metrics, 4> metrics{};
    std::array<pid::Analysis, 4> an{};
    std::array<std::optional<pid::Gains>, 4> gains;
    double band{0};
    pid::Scenario sc;
};

// Les reponses prevues, gardees tant que le modele et les reglages ne changent pas.
const Preview& previewOf(const hmi::PidLoop& L) {
    static std::map<std::string, std::pair<std::string, Preview>> cache;
    const auto& m = L.learner.model();
    const auto der = L.learner.config().derivative;
    char sig[512];
    std::snprintf(sig, sizeof sig, "%.6g|%.6g|%.6g|%.6g|%d|%.6g|%.6g|%.6g|%.4g|%.4g", m.k, m.tau, m.theta, m.tau2, static_cast<int>(der),
                  L.gains ? L.gains->kp : 0, L.gains ? L.gains->ti : 0, L.gains ? L.gains->td : 0, L.learner.typicalStep(), L.learner.outRest());
    auto& slot = cache[L.key];
    if (slot.first == sig) return slot.second;
    Preview pv;
    const auto ctx = L.learner.context();
    pv.sc.sp0 = L.sp;
    double step = L.learner.typicalStep();
    if (!(std::abs(step) > 0)) step = L.learner.config().pvSpan > 0 ? L.learner.config().pvSpan * 0.1 : 1.0;
    pv.sc.step = step;
    pv.sc.u0 = L.learner.outRest();
    pv.sc.outMin = L.learner.config().outMin;
    pv.sc.outMax = L.learner.config().outMax;
    pv.sc.load = -0.05 * L.learner.config().outSpan();
    pv.band = std::max(L.learner.config().tolerance * 0.2, std::abs(step) * 0.02);
    if (m.valid) {
        if (L.gains) pv.gains[0] = *L.gains;
        for (int g = 0; g < 3; ++g) {
            const auto& pr = L.learner.proposal(static_cast<pid::Goal>(g), der, L.bind.form);
            if (pr.ok) pv.gains[static_cast<std::size_t>(g + 1)] = pr.gains;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (!pv.gains[i]) continue;
            pv.traces[i] = pid::simulate(m, *pv.gains[i], pv.sc, ctx);
            pv.metrics[i] = pid::measure(*pv.traces[i], pv.band);
            pv.an[i] = pid::analyze(m, *pv.gains[i], ctx);
        }
    }
    slot = {sig, std::move(pv)};
    return slot.second;
}

// Le diagnostic en tete : ce qui ne va pas dans le reglage du bloc (ou que tout va bien).
std::pair<std::string, std::string> diagnosis(const hmi::PidLoop& L, const Preview& pv, int& tone) {
    const auto& m = L.learner.model();
    const auto osc = L.learner.oscillation(1800.0);
    tone = 1;
    if (!m.valid) {
        tone = 0;
        return {"Il apprend encore le proc\xC3\xA9" "d\xC3\xA9", "Le r\xC3\xA9glage se proposera d\xC3\xA8s deux \xC3\xA9" "chelons appris (" + std::to_string(L.learner.learnedCount()) + " pour l'instant)."};
    }
    if (!L.gains) {
        tone = 0;
        return {"Le r\xC3\xA9glage du bloc n'est pas lu", "Kp, Ti et Td ne sont pas li\xC3\xA9s : voici ce qu'il propose, sans comparer au r\xC3\xA9glage actuel."};
    }
    const auto& g = *L.gains;
    const auto& cur = pv.an[0];
    const double over = pv.metrics[0].overshoot;
    std::string head, more;
    if (!cur.stable || cur.ms > 2.0 || osc.found) {
        tone = 3;
        if (g.ti > 0 && g.ti < 0.5 * std::max(m.tau, 1e-9))
            head = "Ti est trop court : " + hmi::pidTime(g.ti) + ", pour un proc\xC3\xA9" "d\xC3\xA9 qui met " + hmi::pidTime(m.tau + m.theta) + " \xC3\xA0 r\xC3\xA9pondre.";
        else head = "Kp est trop fort pour ce proc\xC3\xA9" "d\xC3\xA9 (robustesse Ms " + hmi::pidNumber(cur.ms, 2) + ", 2 au plus).";
        more = std::string(osc.found ? "La boucle oscille (p\xC3\xA9riode " + hmi::pidTime(osc.period) + ")" : std::string("Elle est au bord de l'oscillation"))
             + " et d\xC3\xA9passe de " + percent(over) + " \xC3\xA0 chaque changement de consigne.";
    } else if (over > pid::limitsOf(L.learner.config().goal).overshoot * 1.5) {
        tone = 2;
        head = "Le r\xC3\xA9glage d\xC3\xA9passe trop : " + percent(over) + " \xC3\xA0 chaque changement de consigne.";
        more = "Un r\xC3\xA9glage plus doux tient la consigne aussi vite, sans d\xC3\xA9passer.";
    } else if (L.learner.state() == pid::State::Sluggish) {
        tone = 2;
        head = "Le r\xC3\xA9glage est lent : la boucle met " + hmi::pidTime(pv.metrics[0].settle) + " \xC3\xA0 se poser.";
        more = "Le proc\xC3\xA9" "d\xC3\xA9 permet d'aller plus vite sans perdre de marge.";
    } else {
        head = "Le r\xC3\xA9glage du bloc tient : rien d'urgent \xC3\xA0 changer.";
        more = "Les propositions ci-dessous comparent ce qu'on gagnerait.";
    }
    // Le procede qui change : il le dit.
    const auto days = L.learner.dailyGains();
    if (days.size() >= 2 && std::abs(days.front().second) > 1e-12) {
        const double change = 100.0 * (days.back().second - days.front().second) / std::abs(days.front().second);
        if (change >= 3) more += " Et le gain du proc\xC3\xA9" "d\xC3\xA9 monte (" + std::string("+") + percent(change, 1) + " en " + std::to_string(days.size()) + " jours) : elle oscillera de plus en plus.";
    }
    return {head, more};
}

void paintTuning(const Pen& p, PidPanelLayout& l, const hmi::Runtime& rt, const hmi::PidLoop& L, const gfx::Rect& body) {
    const float k = p.k, gap = 12 * k;
    const auto& w = rt.pidWindow();
    const auto& cfg = L.learner.config();
    const int conseil = static_cast<int>(cfg.goal);
    const int chosen = w.goal >= 0 ? w.goal : conseil;
    const Preview& pv = previewOf(L);
    float y = body.y;
    // ---- le diagnostic
    {
        int tone = 1;
        const auto [head, more] = diagnosis(L, pv, tone);
        const gfx::Color bg = tone == 3 ? gfx::Color::rgb(0xFDF1EC) : tone == 2 ? gfx::Color::rgb(0xFDF6E7) : tone == 1 ? gfx::Color::rgb(0xEDF7F0) : kAccentSoft;
        const gfx::Color edge = tone == 3 ? gfx::Color::rgb(0xF2C4B3) : tone == 2 ? gfx::Color::rgb(0xF0D9A8) : tone == 1 ? gfx::Color::rgb(0xBFE3CC) : gfx::Color::rgb(0xC6D8F2);
        const gfx::Rect b{body.x, y, body.w, 62 * k};
        p.fill(b, bg, 6 * k);
        p.frame(b, edge, 6 * k);
        p.fill({b.x + 16 * k, b.y + 16 * k, 10 * k, 10 * k}, toneColor(tone), 5 * k);
        p.text(b.x + 40 * k, b.y + 10 * k, head, 13, kInk, true, b.w - 56 * k);
        p.para(b.x + 40 * k, b.y + 12 * k + p.lineH(13), b.w - 56 * k, more, 12, kInk, b.h - p.lineH(13) - 14 * k);
        y += b.h + gap;
    }
    // ---- les trois propositions
    {
        const float cw = (body.w - 2 * gap) / 3, ch = 88 * k;
        const std::string unit = hmi::pidTimeUnitName(L.bind.timeSeconds);
        for (int g = 0; g < 3; ++g) {
            const gfx::Rect b{body.x + (cw + gap) * static_cast<float>(g), y, cw, ch};
            const bool sel = g == chosen;
            p.fill(b, sel ? gfx::Color::rgb(0xF2F7FE) : kPaper, 8 * k);
            p.frame(b, sel ? kAccent : kEdge, 8 * k, sel ? 2 * k : 1);
            part(l, "objectif:" + std::to_string(g), b);
            const auto lim = pid::limitsOf(static_cast<pid::Goal>(g));
            float x = b.x + 14 * k;
            x += p.text(x, b.y + 10 * k, pid::goalName(static_cast<pid::Goal>(g)), 14, kInk, true) + 8 * k;
            p.text(x, b.y + 12 * k, "Ms \xE2\x89\xA4 " + hmi::pidNumber(lim.ms, 1), 11, kMuted);
            if (g == conseil) {
                const std::string s = "conseill\xC3\xA9";
                const float bw = p.width(s, 11) + 16 * k, bh = p.lineH(11) + 4 * k;
                const gfx::Rect tag{b.right() - 12 * k - bw, b.y + 10 * k, bw, bh};
                p.fill(tag, kAccent, bh / 2);
                p.textIn(tag, s, 11, kPaper, 1, true);
            }
            const auto& gg = pv.gains[static_cast<std::size_t>(g + 1)];
            if (!gg) {
                p.para(b.x + 14 * k, b.y + 34 * k, b.w - 28 * k, "Pas encore : il apprend le proc\xC3\xA9" "d\xC3\xA9.", 12, kMuted);
                continue;
            }
            const auto blk = hmi::pidBlockOf(L, *gg);
            const bool par = L.bind.form == pid::Form::Parallel;
            const std::string line = std::string("Kp ") + hmi::pidNumber(blk.p, 2) + (par ? " \xC2\xB7 Ki " : " \xC2\xB7 Ti ") + hmi::pidNumber(blk.i, par ? 4 : 1)
                                   + (par ? "" : " " + unit) + (par ? " \xC2\xB7 Kd " : " \xC2\xB7 Td ") + hmi::pidNumber(blk.d, par ? 3 : 1) + (par ? "" : " " + unit);
            p.text(b.x + 14 * k, b.y + 34 * k, line, 13, kInk, false, b.w - 28 * k);
            const auto& mt = pv.metrics[static_cast<std::size_t>(g + 1)];
            std::string note = "D\xC3\xA9passement " + percent(mt.overshoot, 1) + " \xC2\xB7 r\xC3\xA9ponse " + hmi::pidTime(mt.settle) + ".";
            if (g == 0) note += " Tient m\xC3\xAAme si le proc\xC3\xA9" "d\xC3\xA9 change beaucoup.";
            if (g == 2) note += " Moins de marge si le proc\xC3\xA9" "d\xC3\xA9 change.";
            if (g == 1 && pv.metrics[0].settle > 0 && mt.settle > 0) note += " " + hmi::pidNumber(pv.metrics[0].settle / mt.settle, 1) + " fois plus vite qu'aujourd'hui.";
            p.para(b.x + 14 * k, b.y + 54 * k, b.w - 28 * k, note, 12, kMuted, b.bottom() - b.y - 56 * k);
        }
        y += ch + gap;
    }
    // ---- la prevision
    const float bottomH = std::max(250 * k, body.bottom() - y - 280 * k - gap);
    const float chartH = body.bottom() - y - bottomH - gap;
    {
        const gfx::Rect card{body.x, y, body.w, chartH};
        p.card(card);
        const double loadT = pv.traces[0] ? pv.traces[0]->loadAt : pv.traces[2] ? pv.traces[2]->loadAt : 0;
        const std::string title = "Pr\xC3\xA9vision : consigne " + withUnit(L, pv.sc.sp0) + " \xE2\x86\x92 " + withUnit(L, pv.sc.sp0 + pv.sc.step) + ", puis perturbation "
                                + percent(pv.sc.load / std::max(1e-9, cfg.outSpan()) * 100) + " \xC3\xA0 " + hmi::pidTime(loadT);
        float lx = card.x + 16 * k + p.text(card.x + 16 * k, card.y + 12 * k, title, 13, kInk, true, card.w * 0.55f) + 16 * k;
        const gfx::Color cols[4] = {kGray, kModel, kAccent, kOut};
        const char* names[4] = {"Actuel", "Robuste", "\xC3\x89quilibr\xC3\xA9", "Rapide"};
        {
            const float yy = card.y + 12 * k + p.lineH(12) / 2;
            p.dashed({lx, yy}, {lx + 16 * k, yy}, kSp, 2 * k, 4 * k, 3 * k);
            lx += 22 * k + p.text(lx + 22 * k, card.y + 12 * k, "Consigne", 12, kInk) + 12 * k;
            for (int i = 0; i < 4; ++i) {
                if (!pv.traces[static_cast<std::size_t>(i)]) continue;
                p.line({lx, yy}, {lx + 16 * k, yy}, cols[i], (i - 1 == chosen ? 3.f : 2.f) * k);
                lx += 22 * k + p.text(lx + 22 * k, card.y + 12 * k, names[i], 12, kInk) + 12 * k;
            }
        }
        const float left = card.x + 64 * k, right = card.right() - 20 * k, top = card.y + 40 * k, bottom = card.bottom() - 28 * k;
        const float split = top + (bottom - top) * 0.68f;
        Plot py{{left, top, right - left, split - top - 12 * k}};
        Plot pu{{left, split + 8 * k, right - left, bottom - split - 8 * k}};
        const pid::Trace* any = nullptr;
        for (const auto& t : pv.traces) if (t) { any = &*t; break; }
        if (!any) {
            p.para(left, top + 20 * k, right - left, "La pr\xC3\xA9vision se dessinera d\xC3\xA8s que le proc\xC3\xA9" "d\xC3\xA9 sera appris.", 13, kMuted);
        } else {
            const double dur = any->dt * static_cast<double>(any->pv.size());
            py.x0 = pu.x0 = 0;
            py.x1 = pu.x1 = dur;
            double lo = 1e300, hi = -1e300;
            for (const auto& t : pv.traces)
                if (t) for (const float v : t->pv) { lo = std::min(lo, static_cast<double>(v)); hi = std::max(hi, static_cast<double>(v)); }
            niceRange(lo, hi, std::abs(pv.sc.step));
            py.y0 = lo;
            py.y1 = hi;
            pu.y0 = cfg.outMin;
            pu.y1 = cfg.outMax;
            gridY(p, py, ticksOf(lo, hi, 4), decimalsOf(L), L.bind.unit);
            gridY(p, pu, ticksOf(pu.y0, pu.y1, 2), 0, "%");
            // La consigne : en marche, a l'instant de l'echelon.
            p.dashedPolyline({{py.X(0), py.Y(any->sp0)}, {py.X(any->stepAt), py.Y(any->sp0)}, {py.X(any->stepAt), py.Y(any->sp1)}, {py.X(dur), py.Y(any->sp1)}}, kSp, 1.6f * k);
            p.dashed({py.X(any->loadAt), py.r.y}, {py.X(any->loadAt), pu.r.bottom()}, kGray, 1, 3 * k, 3 * k);
            p.text(py.X(any->loadAt) + 6 * k, py.r.bottom() - p.lineH(12) - 2 * k, "perturbation " + percent(pv.sc.load / std::max(1e-9, cfg.outSpan()) * 100), 12, kMuted);
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < 4; ++i) {
                    const auto& t = pv.traces[static_cast<std::size_t>(i)];
                    if (!t || (pass == 1) != (i - 1 == chosen)) continue;   // le choisi par-dessus
                    std::vector<gfx::Point> a, b;
                    const std::size_t stepN = std::max<std::size_t>(1, t->pv.size() / static_cast<std::size_t>(std::max(50.f, py.r.w)));
                    for (std::size_t j = 0; j < t->pv.size(); j += stepN) {
                        a.push_back({py.X(t->dt * static_cast<double>(j)), py.Y(t->pv[j])});
                        b.push_back({pu.X(t->dt * static_cast<double>(j)), pu.Y(t->out[j])});
                    }
                    const float wdt = (i - 1 == chosen ? 3.f : 1.8f) * k;
                    p.polyline(a, cols[i], wdt);
                    p.polyline(b, cols[i], wdt * 0.8f);
                }
            // Le pic du choisi, et celui d'aujourd'hui.
            const auto peak = [&](int i) {
                const auto& t = pv.traces[static_cast<std::size_t>(i)];
                if (!t || t->pv.empty()) return;
                const bool up = t->sp1 >= t->sp0;
                std::size_t im = 0;
                const auto end = static_cast<std::size_t>(std::min<double>(static_cast<double>(t->pv.size()), t->loadAt / std::max(1e-9, t->dt)));
                for (std::size_t j = 0; j < end; ++j)
                    if (up ? t->pv[j] > t->pv[im] : t->pv[j] < t->pv[im]) im = j;
                const std::string s = std::string(names[i]) + " : " + withUnit(L, t->pv[im]) + (i - 1 == chosen ? ", pos\xC3\xA9 en " + hmi::pidTime(pv.metrics[static_cast<std::size_t>(i)].settle) : std::string{});
                const float x = std::min(py.X(t->dt * static_cast<double>(im)) + 8 * k, right - p.width(s, 12));
                p.text(x, std::max(py.r.y, py.Y(t->pv[im]) - p.lineH(12) - 4 * k), s, 12, kInk, i - 1 == chosen);
            };
            peak(0);
            peak(chosen + 1);
            for (int i = 0; i <= 5; ++i) {
                const double t = dur * i / 5;
                const std::string s = t <= 0 ? std::string("0") : hmi::pidTime(t);
                p.text(pu.X(t) - p.width(s, 11) / 2, pu.r.bottom() + 5 * k, s, 11, kMuted);
            }
        }
        y += chartH + gap;
    }
    // ---- Comparer, et ce qui s'ecrit dans le bloc
    const float wl = (body.w - gap) * 0.62f;
    const gfx::Rect cmp{body.x, y, wl, body.bottom() - y};
    p.card(cmp);
    p.text(cmp.x + 16 * k, cmp.y + 12 * k, "Comparer", 14, kInk, true);
    {
        const std::string unit = hmi::pidTimeUnitName(L.bind.timeSeconds);
        struct Row { std::string label; std::array<std::string, 4> v; };
        std::vector<Row> rows;
        const auto add = [&](std::string label, auto f) {
            Row r2{std::move(label), {}};
            for (std::size_t i = 0; i < 4; ++i) r2.v[i] = pv.gains[i] ? f(i) : std::string("\xE2\x80\x94");
            rows.push_back(std::move(r2));
        };
        const auto blkOf = [&](std::size_t i) { return hmi::pidBlockOf(L, *pv.gains[i]); };
        add("Kp", [&](std::size_t i) { return hmi::pidNumber(blkOf(i).p, 2); });
        add(L.bind.form == pid::Form::Parallel ? "Ki" : "Ti", [&](std::size_t i) { return hmi::pidNumber(blkOf(i).i, 2) + (L.bind.form == pid::Form::Parallel ? "" : " " + unit); });
        add(L.bind.form == pid::Form::Parallel ? "Kd" : "Td", [&](std::size_t i) { return hmi::pidNumber(blkOf(i).d, 2) + (L.bind.form == pid::Form::Parallel ? "" : " " + unit); });
        add("D\xC3\xA9passement (consigne " + std::string(pv.sc.step >= 0 ? "+" : "") + withUnit(L, pv.sc.step) + ")", [&](std::size_t i) { return percent(pv.metrics[i].overshoot, 1); });
        add("Temps de r\xC3\xA9ponse (\xC2\xB1" + withUnit(L, pv.band) + ")", [&](std::size_t i) { return hmi::pidTime(pv.metrics[i].settle); });
        add("Perturbation : \xC3\xA9" "cart maximal", [&](std::size_t i) { return withUnit(L, pv.metrics[i].loadDev); });
        add("Retour \xC3\xA0 \xC2\xB1" + withUnit(L, pv.band), [&](std::size_t i) { return hmi::pidTime(pv.metrics[i].loadRecover); });
        add("Robustesse Ms (2 au plus)", [&](std::size_t i) { return hmi::pidNumber(pv.an[i].ms, 2); });
        add("Marge de gain", [&](std::size_t i) { return pv.an[i].gm > 0 ? "\xC3\x97" + hmi::pidNumber(pv.an[i].gm, 1) : std::string("sans limite"); });
        add("Marge de phase", [&](std::size_t i) { return pv.an[i].pm > 0 ? hmi::pidNumber(pv.an[i].pm, 0) + "\xC2\xB0" : std::string("\xE2\x80\x94"); });
        add("Bruit envoy\xC3\xA9 \xC3\xA0 la sortie", [&](std::size_t i) { return "\xC2\xB1" + percent(pv.an[i].noiseOut / std::max(1e-9, cfg.outSpan()) * 100, 2); });
        const float x0 = cmp.x + 16 * k, tw = cmp.w - 32 * k, labelW = tw * 0.40f, colW = (tw - labelW) / 4;
        const float rowH = std::min(28 * k, (cmp.h - 96 * k) / static_cast<float>(rows.size() + 1));
        float ry = cmp.y + 38 * k;
        p.fill({x0, ry, tw, rowH}, gfx::Color::rgb(0xEEF2F6));
        p.fill({x0 + labelW + colW * static_cast<float>(chosen + 1), ry, colW, rowH * static_cast<float>(rows.size() + 1)}, kAccentSoft);
        p.textIn({x0 + 8 * k, ry, labelW, rowH}, "Pr\xC3\xA9vu sur le mod\xC3\xA8le appris", 12, kInk, 0, true);
        const char* heads[4] = {"Actuel", "Robuste", "\xC3\x89quilibr\xC3\xA9", "Rapide"};
        for (int i = 0; i < 4; ++i) p.textIn({x0 + labelW + colW * static_cast<float>(i), ry, colW - 8 * k, rowH}, heads[i], 12, kInk, 2, true);
        ry += rowH;
        for (const auto& r2 : rows) {
            p.line({x0, ry}, {x0 + tw, ry}, kGrid, 1);
            p.textIn({x0 + 8 * k, ry, labelW - 8 * k, rowH}, r2.label, 12, kInk);
            for (std::size_t i = 0; i < 4; ++i) p.textIn({x0 + labelW + colW * static_cast<float>(i), ry, colW - 8 * k, rowH}, r2.v[i], 12, kInk, 2);
            ry += rowH;
        }
        const auto lim = pid::limitsOf(static_cast<pid::Goal>(chosen));
        p.para(x0, ry + 8 * k, tw,
               "Comment il choisit : le r\xC3\xA9glage le plus rapide, pour la consigne et pour les perturbations, qui tient dans les limites (Ms \xE2\x89\xA4 "
                   + hmi::pidNumber(lim.ms, 1) + ", d\xC3\xA9passement \xE2\x89\xA4 " + percent(lim.overshoot) + ", bruit envoy\xC3\xA9 \xC3\xA0 la sortie \xE2\x89\xA4 " + percent(lim.noise, 1)
                   + "). La d\xC3\xA9riv\xC3\xA9" "e, seulement si elle aide sans trop de bruit.",
               11, kMuted, cmp.bottom() - ry - 14 * k);
    }
    // ---- A ecrire dans le bloc PID
    const gfx::Rect wr{cmp.right() + gap, y, body.right() - cmp.right() - gap, body.bottom() - y};
    p.card(wr);
    p.text(wr.x + 16 * k, wr.y + 12 * k, "\xC3\x80 \xC3\xA9" "crire dans le bloc PID", 14, kInk, true);
    float ry = wr.y + 42 * k;
    const auto& target = pv.gains[static_cast<std::size_t>(chosen + 1)];
    const std::string unit = hmi::pidTimeUnitName(L.bind.timeSeconds);
    const bool par = L.bind.form == pid::Form::Parallel;
    if (target) {
        const auto now = hmi::pidBlockOf(L, *target);
        const auto before = L.block;
        const std::array<std::tuple<std::string, double, double, std::string, std::string>, 3> lines = {{
            {"KP", before ? before->p : NAN, now.p, "", L.bind.kp},
            {par ? "KI" : "TI", before ? before->i : NAN, now.i, par ? "" : " " + unit, L.bind.ti},
            {par ? "KD" : "TD", before ? before->d : NAN, now.d, par ? "" : " " + unit, L.bind.td}}};
        for (const auto& [name, a, b, u, path] : lines) {
            p.text(wr.x + 16 * k, ry, name, 12, kInk);
            const float x = wr.x + 46 * k;
            float xx = x + p.text(x, ry, (std::isfinite(a) ? hmi::pidNumber(a, 2) + u : std::string("?")) + " \xE2\x86\x92 ", 13, kInk);
            p.text(xx, ry, hmi::pidNumber(b, 2) + u, 13, kInk, true);
            p.text(x, ry + p.lineH(13), path.empty() ? std::string("(pas de variable)") : path, 11, kMuted, false, wr.right() - x - 16 * k);
            ry += p.lineH(13) + p.lineH(11) + 10 * k;
            p.line({wr.x + 16 * k, ry - 5 * k}, {wr.right() - 16 * k, ry - 5 * k}, kGrid, 1);
        }
    } else {
        ry += p.para(wr.x + 16 * k, ry, wr.w - 32 * k, "Rien \xC3\xA0 \xC3\xA9" "crire tant que le proc\xC3\xA9" "d\xC3\xA9 n'est pas appris.", 13, kMuted) + 8 * k;
    }
    ry += 4 * k;
    if (w.confirm && target) {
        const gfx::Rect box{wr.x + 16 * k, ry, wr.w - 32 * k, 74 * k};
        p.fill(box, gfx::Color::rgb(0xFFF8E6), 6 * k);
        p.frame(box, gfx::Color::rgb(0xF0D9A8), 6 * k);
        p.para(box.x + 12 * k, box.y + 8 * k, box.w - 24 * k, "\xC3\x89" "crire le r\xC3\xA9glage " + std::string(pid::goalName(static_cast<pid::Goal>(chosen))) + " dans le bloc ?", 13, kInk, 22 * k, true);
        const gfx::Rect no = p.button(box.right() - 12 * k, box.bottom() - 40 * k, "Annuler", false, 13);
        const gfx::Rect yes = p.button(no.x - 8 * k, box.bottom() - 40 * k, "\xC3\x89" "crire", true, 13);
        part(l, "appliquer:oui", yes);
        part(l, "appliquer:non", no);
        ry = box.bottom() + 10 * k;
    } else {
        const std::string label = L.bind.writes ? "Appliquer " + std::string(pid::goalName(static_cast<pid::Goal>(chosen))) + "\xE2\x80\xA6" : std::string("\xC3\x89" "criture d\xC3\xA9sactiv\xC3\xA9" "e");
        const float bw = p.width(label, 13) + 30 * k;
        const gfx::Rect apply = p.button(wr.x + 16 * k + bw, ry, label, L.bind.writes && target, 13);
        if (L.bind.writes && target) part(l, "appliquer", apply);
        const float cw2 = p.width("Copier les valeurs", 13) + 30 * k;
        const gfx::Rect copy = p.button(apply.right() + 8 * k + cw2, ry, "Copier les valeurs", false, 13);
        part(l, "copier", copy);
        ry = apply.bottom() + 12 * k;
    }
    const auto& prc = target ? pv.metrics[static_cast<std::size_t>(chosen + 1)] : pid::Metrics{};
    std::string notes = "\xC3\x89" "crit apr\xC3\xA8s ta confirmation (le niveau d'acc\xC3\xA8s de l'objet), sans \xC3\xA0-coup : la sortie ne saute pas. L'ancien r\xC3\xA9glage reste dans le Journal : Revenir le remet d'un clic.";
    if (target) notes += " Ensuite, il v\xC3\xA9rifie au prochain changement de consigne : pr\xC3\xA9vu " + percent(prc.overshoot, 1) + " de d\xC3\xA9passement, il dira ce qu'il a mesur\xC3\xA9.";
    if (!L.bind.writes) notes = "\xC3\x89" "crire le r\xC3\xA9glage : jamais (r\xC3\xA9glage de l'objet) - copie les valeurs pour les r\xC3\xA9gler \xC3\xA0 la main.";
    p.para(wr.x + 16 * k, ry, wr.w - 32 * k, notes, 12, kMuted, wr.bottom() - ry - 10 * k);
}

// ========================================================================== Journal ===
void paintJournal(const Pen& p, PidPanelLayout& l, const hmi::Runtime& rt, const hmi::PidLoop& L, const gfx::Rect& body) {
    const float k = p.k;
    const auto& w = rt.pidWindow();
    const gfx::Rect card = body;
    p.card(card);
    p.text(card.x + 16 * k, card.y + 14 * k, "Ce qui a chang\xC3\xA9 dans la boucle", 14, kInk, true);
    using K = pid::EventKind;
    std::vector<const pid::Event*> list;
    for (auto it = L.learner.events().rbegin(); it != L.learner.events().rend(); ++it)
        if (it->kind == K::Applied || it->kind == K::Verified || it->kind == K::GainsChanged || it->kind == K::ModelChanged || it->kind == K::Forgotten)
            list.push_back(&*it);
    const std::string unit = hmi::pidTimeUnitName(L.bind.timeSeconds);
    const auto gainsText = [&](const pid::Gains& g) {
        const auto b = hmi::pidBlockOf(L, g);
        const bool par = L.bind.form == pid::Form::Parallel;
        return std::string("Kp ") + hmi::pidNumber(b.p, 2) + (par ? ", Ki " : ", Ti ") + hmi::pidNumber(b.i, 2) + (par ? "" : " " + unit) + (par ? ", Kd " : ", Td ")
             + hmi::pidNumber(b.d, 2) + (par ? "" : " " + unit);
    };
    const double now = L.learner.lastT();
    float ry = card.y + 46 * k;
    const float rowH = 52 * k;
    const int maxRows = std::max(1, static_cast<int>((card.bottom() - ry - 20 * k) / rowH));
    const int first = std::clamp(w.scroll, 0, std::max(0, static_cast<int>(list.size()) - maxRows));
    int appliedIndex = -1;
    std::vector<int> appliedAt(list.size(), -1);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i]->kind == K::Applied) appliedAt[i] = ++appliedIndex;
    if (list.empty())
        p.para(card.x + 16 * k, ry, card.w - 32 * k,
               "Rien encore : les r\xC3\xA9glages \xC3\xA9" "crits, leurs v\xC3\xA9rifications, les r\xC3\xA9glages chang\xC3\xA9s dans le bloc et les changements du proc\xC3\xA9" "d\xC3\xA9 s'\xC3\xA9" "crivent ici.",
               13, kMuted);
    for (int i = first; i < static_cast<int>(list.size()) && i < first + maxRows; ++i) {
        const auto& e = *list[static_cast<std::size_t>(i)];
        std::string title, detail;
        switch (e.kind) {
            case K::Applied:
                title = "R\xC3\xA9glage \xC3\xA9" "crit par l'objet";
                detail = gainsText(e.before) + " \xE2\x86\x92 " + gainsText(e.after) + (e.predicted >= 0 ? " \xC2\xB7 d\xC3\xA9passement pr\xC3\xA9vu " + percent(e.predicted, 1) : std::string{});
                break;
            case K::Verified:
                title = "V\xC3\xA9rifi\xC3\xA9 sur la consigne " + withUnit(L, e.a) + " \xE2\x86\x92 " + withUnit(L, e.b);
                detail = "D\xC3\xA9passement pr\xC3\xA9vu " + percent(e.predicted, 1) + ", mesur\xC3\xA9 " + percent(e.measured, 1) + (e.since > 0 ? " \xC2\xB7 pos\xC3\xA9 en " + hmi::pidTime(e.since) : std::string{});
                break;
            case K::GainsChanged:
                title = "R\xC3\xA9glage chang\xC3\xA9 dans le bloc";
                detail = gainsText(e.before) + " \xE2\x86\x92 " + gainsText(e.after);
                break;
            case K::ModelChanged:
                title = "Le proc\xC3\xA9" "d\xC3\xA9 a chang\xC3\xA9";
                detail = "K " + hmi::pidNumber(e.before.kp, 3) + " \xE2\x86\x92 " + hmi::pidNumber(e.after.kp, 3) + ", \xCF\x84 " + hmi::pidTime(e.before.ti) + " \xE2\x86\x92 " + hmi::pidTime(e.after.ti)
                       + ", \xCE\xB8 " + hmi::pidTime(e.before.td) + " \xE2\x86\x92 " + hmi::pidTime(e.after.td);
                break;
            default:
                title = "Oubli\xC3\xA9 : il r\xC3\xA9" "apprend";
                detail = "Le mod\xC3\xA8le et ses \xC3\xA9" "chelons effac\xC3\xA9s ; les statistiques restent.";
                break;
        }
        p.text(card.x + 16 * k, ry, whenOf(e.t, now), 12, kMuted, false, 120 * k);
        const float tx = card.x + 150 * k;
        const int ai = appliedAt[static_cast<std::size_t>(i)];
        float buttonsW = 0;
        if (ai >= 0) {
            if (w.revertAsk == ai) {
                const gfx::Rect no = p.button(card.right() - 16 * k, ry - 4 * k, "Annuler", false, 12);
                const gfx::Rect yes = p.button(no.x - 6 * k, ry - 4 * k, "Remettre l'ancien", true, 12);
                part(l, "revenir:oui", yes);
                part(l, "revenir:non", no);
                buttonsW = card.right() - yes.x;
            } else {
                const gfx::Rect b = p.button(card.right() - 16 * k, ry - 4 * k, "Revenir", false, 12);
                part(l, "revenir:" + std::to_string(ai), b);
                buttonsW = card.right() - b.x;
            }
        }
        p.text(tx, ry, title, 13, kInk, true, card.right() - tx - buttonsW - 24 * k);
        p.text(tx, ry + p.lineH(13) + 2 * k, detail, 12, kMuted, false, card.right() - tx - buttonsW - 24 * k);
        p.line({card.x + 16 * k, ry + rowH - 8 * k}, {card.right() - 16 * k, ry + rowH - 8 * k}, kGrid, 1);
        ry += rowH;
    }
    if (static_cast<int>(list.size()) > maxRows) {
        const gfx::Rect up{card.right() - 70 * k, card.y + 10 * k, 26 * k, 24 * k}, down{card.right() - 40 * k, card.y + 10 * k, 26 * k, 24 * k};
        p.fill(up, kPaper, 4 * k);
        p.frame(up, kEdge, 4 * k);
        p.fill(down, kPaper, 4 * k);
        p.frame(down, kEdge, 4 * k);
        p.textIn(up, "\xE2\x96\xB2", 11, kInk, 1);
        p.textIn(down, "\xE2\x96\xBC", 11, kInk, 1);
        part(l, "journal:haut", up);
        part(l, "journal:bas", down);
    }
}

} // namespace

// ============================================================================ la fenetre ===
PidPanelLayout paintPidPanel(gfx::IRenderer& r, const ui::Theme& theme, const hmi::Runtime& rt, const gfx::Rect& screen) {
    (void)theme;
    PidPanelLayout l;
    const hmi::PidLoop* L = rt.pidShownLoop();
    const auto& w = rt.pidWindow();
    const float k = std::clamp(std::min(screen.w / 1240.f, screen.h / 900.f), 0.56f, 1.15f);
    Pen p{r, k};
    // Le voile sur la vue.
    r.fillRect(screen, gfx::Color{10, 14, 20, 150});
    const float pw = std::min(screen.w - 16.f, 1220 * k), ph = std::min(screen.h - 16.f, 900 * k);
    l.panel = {screen.x + (screen.w - pw) / 2, screen.y + (screen.h - ph) / 2, pw, ph};
    const gfx::Rect& P = l.panel;
    r.fillRoundedRect({P.x + 2, P.y + 4, P.w, P.h}, gfx::Color{0, 0, 0, 70}, 10 * k);
    p.fill(P, kPaper, 10 * k);
    // L'en-tete : le titre, les liaisons, la croix.
    const std::string title = "R\xC3\xA9glage PID \xC2\xB7 " + (L ? L->bind.title : std::string("?"));
    p.text(P.x + 18 * k, P.y + 12 * k, title, 17, kInk, true, P.w - 80 * k);
    if (L) p.text(P.x + 18 * k, P.y + 14 * k + p.lineH(17), bindingsLine(*L), 12, kMuted, false, P.w - 80 * k);
    const gfx::Rect close{P.right() - 46 * k, P.y + 12 * k, 30 * k, 30 * k};
    p.line({close.x + 9 * k, close.y + 9 * k}, {close.right() - 9 * k, close.bottom() - 9 * k}, kInk, 2 * k);
    p.line({close.right() - 9 * k, close.y + 9 * k}, {close.x + 9 * k, close.bottom() - 9 * k}, kInk, 2 * k);
    part(l, "fermer", close);
    // Les onglets.
    const float tabsY = P.y + 58 * k, tabsH = 36 * k;
    p.line({P.x, tabsY + tabsH}, {P.right(), tabsY + tabsH}, kEdge, 1);
    const char* tabs[4] = {"Statistiques", "Ce qu'il a appris", "R\xC3\xA9glage propos\xC3\xA9", "Journal"};
    float tx = P.x + 12 * k;
    for (int i = 0; i < 4; ++i) {
        const float tw = p.width(tabs[i], 14) + 32 * k;
        const gfx::Rect b{tx, tabsY, tw, tabsH};
        p.textIn(b, tabs[i], 14, i == w.tab ? kInk : kMuted, 1, i == w.tab);
        if (i == w.tab) p.fill({b.x + 4 * k, b.bottom() - 3 * k, b.w - 8 * k, 3 * k}, kAccent);
        part(l, "onglet:" + std::to_string(i), b);
        tx += tw;
    }
    // Le pied : ce qu'il dit, les boutons de l'onglet.
    const float footH = 54 * k;
    const gfx::Rect foot{P.x, P.bottom() - footH, P.w, footH};
    r.fillRect({foot.x + 1, foot.y, foot.w - 2, 1}, kEdge);
    r.fillRoundedRect({foot.x, foot.y + 1, foot.w, foot.h - 1}, gfx::Color::rgb(0xF6F8FA), 10 * k);
    const gfx::Rect body{P.x + 18 * k, tabsY + tabsH + 14 * k, P.w - 36 * k, foot.y - (tabsY + tabsH + 14 * k) - 14 * k};
    if (!L) {
        p.para(body.x, body.y, body.w, "Cette boucle n'a pas encore \xC3\xA9t\xC3\xA9 lue.", 14, kMuted);
        return l;
    }
    r.pushClip(body);
    switch (w.tab) {
        case 1: paintLearned(p, l, rt, *L, body); break;
        case 2: paintTuning(p, l, rt, *L, body); break;
        case 3: paintJournal(p, l, rt, *L, body); break;
        default: paintStats(p, l, rt, *L, body); break;
    }
    r.popClip();
    const float by = foot.y + (footH - (p.lineH(13) + 14 * k)) / 2;
    const gfx::Rect closeB = p.button(foot.right() - 18 * k, by, "Fermer", true, 13);
    part(l, "fermer", closeB);
    float left = closeB.x - 10 * k;
    std::string info;
    if (w.tab == 0) {
        const gfx::Rect ex = p.button(left, by, "Exporter (CSV)", false, 13);
        part(l, "exporter", ex);
        left = ex.x - 10 * k;
        const auto st = L->learner.stats(hmi::pidRangeSeconds(w.range));
        info = "Mesures toutes les " + hmi::pidTime(L->learner.config().period) + " \xC2\xB7 " + std::to_string(st.samples) + " sur " + std::string(hmi::pidRangeName(w.range))
             + " \xC2\xB7 mis \xC3\xA0 jour en continu";
    } else if (w.tab == 1) {
        if (w.forgetAsk) {
            const gfx::Rect no = p.button(left, by, "Annuler", false, 13);
            const gfx::Rect yes = p.button(no.x - 8 * k, by, "Oublier", true, 13);
            part(l, "oublier:non", no);
            part(l, "oublier:oui", yes);
            left = yes.x - 10 * k;
            info = "Oublier le mod\xC3\xA8le et ses \xC3\xA9" "chelons ? Les statistiques restent ; il r\xC3\xA9" "apprend aux prochains \xC3\xA9" "chelons.";
        } else {
            const gfx::Rect fg = p.button(left, by, "Oublier et r\xC3\xA9" "apprendre", false, 13);
            part(l, "oublier", fg);
            left = fg.x - 10 * k;
            info = "Il continue d'apprendre en marche ; un mod\xC3\xA8le qui change se voit ici et dans le Journal.";
        }
    } else if (w.tab == 2) {
        const auto& m = L->learner.model();
        info = "Pr\xC3\xA9visions calcul\xC3\xA9" "es sur le mod\xC3\xA8le appris" + (m.valid ? " (" + percent(m.fitLearn) + ")" : std::string{}) + " \xC2\xB7 bloc de forme "
             + (L->bind.form == pid::Form::Parallel ? std::string("parall\xC3\xA8le") : L->bind.form == pid::Form::Series ? std::string("s\xC3\xA9rie") : std::string("standard"))
             + ", Ti et Td en " + (L->bind.timeSeconds == 60 ? std::string("minutes") : L->bind.timeSeconds == 0.001 ? std::string("millisecondes")
                                   : L->bind.timeSeconds == 0.1 ? std::string("dixi\xC3\xA8mes de seconde") : std::string("secondes"));
    } else {
        info = "L'ancien r\xC3\xA9glage de chaque \xC3\xA9" "criture : Revenir le remet (apr\xC3\xA8s confirmation).";
    }
    // Le message du dernier geste passe devant (ecrit, refuse, copie).
    gfx::Color infoCol = kMuted;
    if (!w.message.empty() && rt.now() < w.messageUntil) {
        info = w.message;
        infoCol = w.messageBad ? kRed : kGreen;
    }
    p.text(foot.x + 18 * k, foot.y + (footH - p.lineH(12)) / 2, info, 12, infoCol, !w.message.empty() && rt.now() < w.messageUntil, left - foot.x - 30 * k);
    return l;
}

std::string pidPanelHit(const PidPanelLayout& l, gfx::Point pt) {
    for (auto it = l.parts.rbegin(); it != l.parts.rend(); ++it)
        if (it->second.contains(pt)) return it->first;
    return l.panel.contains(pt) ? std::string{} : std::string("dehors");
}

double pidPanelHoverTime(const PidPanelLayout& l, gfx::Point pt) {
    if (l.chart.w <= 0 || !l.chart.contains(pt) || l.t1 <= l.t0) return -1;
    return l.t0 + (l.t1 - l.t0) * (pt.x - l.chart.x) / l.chart.w;
}

// ============================================================================== la carte ===
namespace paint {

namespace {

// Entre deux couleurs (t : la part de b), avec l'opacite de a.
gfx::Color between(gfx::Color a, gfx::Color b, double t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (static_cast<double>(y) - x) * t)); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), a.a};
}

void boldText(const Ctx& c, std::string_view s, double x, double y, double w, double h, gfx::Color col, double px, std::string_view align) {
    text(c, s, x, y, w, h, col, px, align);
    text(c, s, x + 0.6 / std::max(0.2f, c.vp.zoom), y, w, h, col, px, align);
}

// Le dessin de la carte : en local (0,0 en haut a gauche), tourne et zoome avec l'objet.
void card(const Ctx& c, const hmi::PidLoop* L) {
    const double w = c.w(), h = c.h();
    const double radius = std::max(0.0, c.src.number(c.o, "radius", 8));
    fillAndStroke(c, rectLocal(w, h, radius));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 7.0, 40.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    // Les gris de la carte : entre son texte et son fond (lisibles sur une carte claire comme sombre).
    const gfx::Color bg = c.color("fill", gfx::Color::rgb(0x1B2230));
    const bool opaque = bg.a >= 128;
    const gfx::Color muted = opaque ? between(txt, bg, 0.42) : c.fixedText(0x9AA6B8);
    const gfx::Color cPv = c.color("colorPv", gfx::Color::rgb(0x3B8FD9)), cSp = c.color("colorSp", gfx::Color::rgb(0xE8743B));
    const double pad = std::max(8.0, fs * 1.1);
    // Le titre : celui de l'objet, de sa boucle, ou son nom.
    std::string title = L ? L->bind.title : c.src.text(c.o, "title");
    if (title.empty()) title = c.src.text(c.o, "pidLoop");
    if (title.empty()) title = c.o.name;
    double y = pad * 0.8;
    const double headH = fs * 1.9;
    // La pastille, a droite.
    std::string badge = "aper\xC3\xA7u";
    int tone = -1;
    if (L) {
        const auto b = hmi::pidBadge(*L);
        badge = b.text;
        tone = b.tone;
    } else if (!c.opt.editor) {
        badge = "en attente";
    }
    const double bfs = fs * 0.92;
    const double bw = hmi::approxTextWidth(badge, bfs) + bfs * (tone >= 0 ? 2.6 : 1.6);
    const double bx = w - pad - bw, bh = bfs * 1.7, by = y + (headH - bh) / 2;
    {
        auto pts = rectLocal(bw, bh, bh / 2);
        for (auto& q : pts) { q.x += static_cast<float>(bx); q.y += static_cast<float>(by); }
        const gfx::Color edge = tone < 0 ? muted : tone == 1 ? c.fixed(0x2ECC71) : tone == 2 ? c.fixed(0xF2C94C) : tone == 3 ? c.fixed(0xE5534B) : c.fixed(0x4F8FD9);
        shapes::strokePolyline(c.r, c.map(pts), true, edge, std::max(1.f, c.vp.zoom));
        double tx = bx + bfs * 0.5;
        if (tone >= 0) {
            const double r = bfs * 0.32;
            shapes::fillPolygon(c.r, c.map(shapes::ellipse({static_cast<float>(bx + bfs * 0.85), static_cast<float>(by + bh / 2)}, static_cast<float>(r), static_cast<float>(r), 16)), edge);
            tx = bx + bfs * 1.45;
        }
        text(c, badge, tx - 4, by, bw, bh, txt, bfs, "gauche");
    }
    const double titleW = std::max(10.0, bx - pad - 6);
    const std::string kindWord = "R\xC3\xA9glage PID";
    const double tw = std::min(titleW, hmi::approxTextWidth(title, fs * 1.15) + 4);
    boldText(c, fitted(c, title, titleW, fs * 1.15), pad - 4, y, titleW, headH, txt, fs * 1.15, "gauche");
    if (tw + hmi::approxTextWidth(kindWord, fs * 0.92) + 12 < titleW)
        text(c, kindWord, pad + tw + 6 - 4, y, titleW - tw - 6, headH, muted, fs * 0.92, "gauche");
    y += headH;
    // Les trois valeurs.
    const auto valueText = [&](double v, bool out) -> std::string {
        if (!L || !L->read) return "\xE2\x80\x94";
        if (out) return hmi::pidNumber(v, 1) + " %";
        const double span = L->bind.pvMax > L->bind.pvMin ? L->bind.pvMax - L->bind.pvMin : std::max(1.0, std::abs(L->sp) * 2);
        const int d = span < 5 ? 2 : span < 500 ? 1 : 0;
        return hmi::pidNumber(v, d) + (L->bind.unit.empty() ? std::string{} : " " + L->bind.unit);
    };
    const double colW = (w - 2 * pad) / 3;
    const double vfs = fs * 1.35;
    if (h > y + fs * 3.2) {
        const char* labels[3] = {"Mesure", "Consigne", "Sortie"};
        const std::string values[3] = {valueText(L ? L->pv : 0, false), valueText(L ? L->sp : 0, false), valueText(L ? L->out : 0, true)};
        for (int i = 0; i < 3; ++i) {
            const double x = pad + colW * i;
            text(c, labels[i], x - 4, y, colW, fs * 1.2, muted, fs * 0.85, "gauche");
            boldText(c, fitted(c, values[i], colW - 6, vfs), x - 4, y + fs * 1.1, colW, vfs * 1.3, txt, vfs, "gauche");
        }
        y += fs * 1.1 + vfs * 1.3 + fs * 0.4;
    }
    // Le pied et la phrase, de bas en haut ; la courbe prend le reste.
    const double footH = fs * 1.9;
    const double footY = h - pad * 0.6 - footH;
    std::string left, right;
    bool rightBold = false;
    std::string say;
    if (!L) {
        say = c.opt.editor ? "En marche : ses statistiques, ce qu'il apprend, le r\xC3\xA9glage conseill\xC3\xA9." : "En attente de la premi\xC3\xA8re lecture de la boucle.";
    } else if (!L->read) {
        say = L->error;
    } else {
        const auto& m = L->learner.model();
        const auto st30 = L->learner.stats(1800.0);
        const double tol = L->learner.config().tolerance;
        const std::string tolS = "\xC2\xB1" + hmi::pidNumber(tol, tol < 1 ? 2 : 1) + (L->bind.unit.empty() ? std::string{} : " " + L->bind.unit);
        const auto state = L->learner.state();
        switch (state) {
            case hmi::pid::State::Learning: {
                const int n = L->learner.learnedCount();
                say = std::to_string(n) + (n > 1 ? " changements" : " changement") + " de consigne appris sur les 2 qu'il lui faut. Change la consigne de quelques degr\xC3\xA9s, ou passe en MANU et bouge la sortie : il apprendra plus vite.";
                break;
            }
            case hmi::pid::State::Saturated:
                say = "Sortie \xC3\xA0 " + hmi::pidNumber(L->out, 0) + " % depuis " + hmi::pidTime(L->learner.saturatedFor()) + " : le r\xC3\xA9glage n'y peut rien, il manque de la puissance (vanne, ventilateur, proc\xC3\xA9" "d\xC3\xA9).";
                left = "Pas d'apprentissage pendant la saturation";
                break;
            case hmi::pid::State::Manual: say = "En MANU : il apprend des mouvements de la sortie (la boucle est ouverte)."; break;
            case hmi::pid::State::Frozen: say = "La mesure ne bouge plus alors que la sortie bouge : capteur ou liaison ?"; break;
            case hmi::pid::State::NoData: say = "Il attend ses premi\xC3\xA8res mesures."; break;
            default: {
                std::string s = "30 min : dans " + tolS + " " + hmi::pidNumber(st30.within, 0) + " % du temps";
                const auto steps = L->learner.steps(1800.0);
                if (!steps.empty() && state == hmi::pid::State::Oscillating)
                    s += " \xC2\xB7 d\xC3\xA9passement " + hmi::pidNumber(steps.back().overshoot, 0) + " % \xC3\xA0 " + clockOf(steps.back().t);
                else s += " \xC2\xB7 \xC3\xA9" "cart moyen " + hmi::pidNumber(st30.meanAbs, 2) + (L->bind.unit.empty() ? std::string{} : " " + L->bind.unit);
                say = s;
                break;
            }
        }
        if (left.empty() && m.valid) left = "Mod\xC3\xA8le appris : " + hmi::pidNumber(m.fitLearn, 0) + " %";
        if (m.valid) {
            const bool change = state == hmi::pid::State::Oscillating || state == hmi::pid::State::Sluggish;
            right = change ? "R\xC3\xA9glage conseill\xC3\xA9 : " + std::string(hmi::pid::goalName(L->learner.config().goal)) + " \xE2\x80\xBA" : std::string("R\xC3\xA9glage : rien \xC3\xA0 changer");
            rightBold = change;
        } else if (state == hmi::pid::State::Saturated) {
            right = "Ce qu'il a vu \xE2\x80\xBA";
        }
    }
    const bool hasFoot = !left.empty() || !right.empty();
    double sayH = 0;
    const double sfs = fs * 0.92;
    if (!say.empty()) {
        const int lines = std::clamp(static_cast<int>(std::ceil(hmi::approxTextWidth(say, sfs) / std::max(10.0, w - 2 * pad))), 1, 3);
        sayH = sfs * 1.25 * lines + 4;
    }
    const double chartTop = y + fs * 0.2;
    const double chartBottom = (hasFoot ? footY - fs * 0.5 : h - pad * 0.6) - sayH - fs * 1.4;
    if (chartBottom - chartTop > fs * 2) {
        const double cx0 = pad, cx1 = w - pad;
        // Les points de la carte : 30 min ; dans l'editeur, un echelon et sa reponse.
        std::vector<std::pair<double, double>> pv, sp;
        double t0 = 0, t1 = 1800, tol = 1;
        if (L && L->read && !L->learner.recent().empty()) {
            t1 = L->learner.lastT();
            t0 = t1 - 1800;
            tol = L->learner.config().tolerance;
            const auto& rec = L->learner.recent();
            const double perPt = 1800.0 / std::max(20.0, (cx1 - cx0) * std::max(0.5f, c.vp.zoom));
            double next = t0;
            for (const auto& q : rec) {
                if (q.t < t0 || q.t < next) continue;
                next = q.t + perPt;
                pv.emplace_back(q.t, q.pv);
                sp.emplace_back(q.t, q.sp);
            }
        } else {
            tol = 1.0;
            for (int i = 0; i <= 120; ++i) {
                const double t = 1800.0 * i / 120;
                const double step = t >= 450 ? 10.0 : 0.0;
                const double since = std::max(0.0, t - 520);
                const double resp = t >= 520 ? 10.0 * (1 - std::exp(-since / 110.0) * (std::cos(since / 70.0) + 0.4 * std::sin(since / 70.0))) : 0.0;
                sp.emplace_back(t, 60 + step);
                pv.emplace_back(t, 60 + resp);
            }
        }
        double lo = 1e300, hi = -1e300;
        for (const auto& q : pv) { lo = std::min(lo, q.second); hi = std::max(hi, q.second); }
        for (const auto& q : sp) { lo = std::min(lo, q.second - tol); hi = std::max(hi, q.second + tol); }
        if (pv.empty()) { lo = 0; hi = 1; }
        if (hi - lo < tol * 3) { const double m = (hi + lo) / 2; lo = m - tol * 1.5; hi = m + tol * 1.5; }
        const double pad2 = (hi - lo) * 0.1;
        lo -= pad2;
        hi += pad2;
        const auto X = [&](double t) { return cx0 + (cx1 - cx0) * (t - t0) / std::max(1e-9, t1 - t0); };
        const auto Y = [&](double v) { return chartBottom - (chartBottom - chartTop) * (v - lo) / std::max(1e-9, hi - lo); };
        // La bande, la consigne (tirets), la mesure.
        const gfx::Color band = opaque ? between(txt, bg, 0.86) : c.fixed(0x5A6577).withAlpha(static_cast<std::uint8_t>(70 * c.alpha));
        for (std::size_t i = 1; i < sp.size(); ++i) {
            const double xa = X(sp[i - 1].first), xb = X(sp[i].first);
            std::vector<gfx::Point> q{{static_cast<float>(xa), static_cast<float>(Y(sp[i - 1].second + tol))}, {static_cast<float>(xb), static_cast<float>(Y(sp[i - 1].second + tol))},
                                      {static_cast<float>(xb), static_cast<float>(Y(sp[i - 1].second - tol))}, {static_cast<float>(xa), static_cast<float>(Y(sp[i - 1].second - tol))}};
            shapes::fillPolygon(c.r, c.map(q), band);
        }
        std::vector<gfx::Point> spPts, pvPts;
        for (std::size_t i = 0; i < sp.size(); ++i) {
            if (i > 0) spPts.push_back({static_cast<float>(X(sp[i].first)), static_cast<float>(Y(sp[i - 1].second))});
            spPts.push_back({static_cast<float>(X(sp[i].first)), static_cast<float>(Y(sp[i].second))});
        }
        for (const auto& q : pv) pvPts.push_back({static_cast<float>(X(q.first)), static_cast<float>(Y(q.second))});
        const auto spScreen = c.map(spPts);
        for (std::size_t i = 1; i < spScreen.size(); ++i) {
            const gfx::Point a = spScreen[i - 1], b = spScreen[i];
            const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy), on = 5 * c.vp.zoom, off = 4 * c.vp.zoom;
            for (float s = 0; s < len; s += on + off) {
                const float e = std::min(len, s + on);
                c.r.line({a.x + dx * s / len, a.y + dy * s / len}, {a.x + dx * e / len, a.y + dy * e / len}, cSp, std::max(1.f, 1.5f * c.vp.zoom));
            }
        }
        if (pvPts.size() >= 2) shapes::strokePolyline(c.r, c.map(pvPts), false, cPv, std::max(1.f, 2.f * c.vp.zoom));
        // Les heures aux deux bouts.
        const std::string a = L && L->read && !L->learner.recent().empty() ? clockOf(t0) : std::string("\xE2\x88\x92" "30 min");
        const std::string b = L && L->read && !L->learner.recent().empty() ? clockOf(t1) : std::string("maintenant");
        text(c, a, pad - 4, chartBottom + 1, w / 2, fs * 1.3, muted, fs * 0.8, "gauche");
        text(c, b, w / 2, chartBottom + 1, w / 2 - pad + 4, fs * 1.3, muted, fs * 0.8, "droite");
    }
    if (!say.empty()) {
        const double sy = (hasFoot ? footY - fs * 0.4 : h - pad * 0.6) - sayH;
        text(c, say, pad - 4, sy, w - 2 * pad + 8, sayH, txt, sfs, "gauche", true);
    }
    if (hasFoot) {
        const gfx::Color rule = opaque ? between(txt, bg, 0.78) : c.fixed(0x3A4350);
        c.r.line(c.map(pad, footY), c.map(w - pad, footY), rule, std::max(1.f, c.vp.zoom));
        const double rw = right.empty() ? 0 : std::min(w * 0.62, hmi::approxTextWidth(right, sfs) + 8);
        if (!left.empty()) text(c, fitted(c, left, w - 2 * pad - rw - 8, sfs), pad - 4, footY, w - 2 * pad - rw, footH, muted, sfs, "gauche");
        if (!right.empty()) {
            if (rightBold) boldText(c, fitted(c, right, rw, sfs), w - pad - rw, footY, rw + 4, footH, txt, sfs, "droite");
            else text(c, fitted(c, right, rw, sfs), w - pad - rw, footY, rw + 4, footH, muted, sfs, "droite");
        }
    }
}

} // namespace

bool drawPidTuner(const Ctx& c) {
    if (c.o.kind != hmi::Kind::PidTuner) return false;
    const hmi::PidLoop* L = !c.opt.editor && c.opt.runtime ? c.opt.runtime->pidLoop(c.o.id) : nullptr;
    card(c, L);
    return true;
}

} // namespace paint

} // namespace app
