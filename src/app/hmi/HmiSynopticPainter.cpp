// =============================================================================
//  app/hmi/HmiSynopticPainter.cpp - le dessin des symboles de synoptique
//                                   (lot 10)
// -----------------------------------------------------------------------------
//  Vingt-quatre symboles de process, d'electricite et de stockage, dessines en
//  vectoriel (nets a toutes les tailles, tournes et retournes avec l'objet) :
//
//    process      vanne, pompe, moteur, tuyauterie, ventilateur, compresseur,
//                 echangeur, filtre, chaudiere, convoyeur, verin, melangeur,
//                 clapet anti-retour, fleche de flux, instrument ISA ;
//    stockage     cuve, bouteille de gaz, silo, tremie ;
//    electrique   disjoncteur, sectionneur, contacteur, lampe, transformateur.
//
//  CE QU'ILS MONTRENT : "value" - l'etat (en marche, ouvert, ferme, sous
//  tension) ou le niveau (entre min et max) ; "fault" - le defaut, qui
//  clignote. Leurs couleurs : colorOn, colorOff, colorFault. En marche, ce qui
//  tourne tourne (pompe, moteur, ventilateur, melangeur...), ce qui coule
//  coule (tuyauterie, convoyeur, fleche) - "animate" ; dans l'editeur, rien ne
//  bouge. Un libelle ("label") dessous ou dessus.
// =============================================================================
#include "HmiPaintKit.hpp"

#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace app::paint {

namespace {

constexpr double kPi = 3.14159265358979323846;
using hmi::Kind;
using Pts = std::vector<gfx::Point>;

gfx::Point P(double x, double y) { return {static_cast<float>(x), static_cast<float>(y)}; }
gfx::Color withA(gfx::Color c, float a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(c.a * a, 0.f, 255.f))); }
gfx::Color shade(gfx::Color c, float f) {
    const auto m = [&](std::uint8_t v) { return static_cast<std::uint8_t>(std::clamp(v * f, 0.f, 255.f)); };
    return {m(c.r), m(c.g), m(c.b), c.a};
}
void fill(const Ctx& c, const Pts& local, gfx::Color col) {
    if (col.a && local.size() >= 3) shapes::fillPolygon(c.r, c.map(local), col);
}
void outline(const Ctx& c, const Pts& local, bool closed, gfx::Color col, double width) {
    if (col.a && local.size() >= 2) shapes::strokePolyline(c.r, c.map(local), closed, col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
void seg(const Ctx& c, double x0, double y0, double x1, double y1, gfx::Color col, double width) {
    if (col.a) c.r.line(c.map(x0, y0), c.map(x1, y1), col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
Pts circle(double cx, double cy, double r, int n = 40) {
    return shapes::ellipse(P(cx, cy), static_cast<float>(r), static_cast<float>(r), n);
}
Pts box(double x, double y, double w, double h, double rad = 0) {
    return shapes::roundedRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(std::max(0.0, w)),
                               static_cast<float>(std::max(0.0, h)), static_cast<float>(std::max(0.0, rad)));
}
gfx::Point turn(double cx, double cy, double x, double y, double deg) {
    const double a = deg * kPi / 180, dx = x - cx, dy = y - cy;
    return P(cx + dx * std::cos(a) - dy * std::sin(a), cy + dx * std::sin(a) + dy * std::cos(a));
}
// La partie d'un polygone sous la ligne y = level (le contenu d'une cuve).
Pts below(const Pts& poly, double level) {
    Pts out;
    const auto n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        const bool ain = a.y >= level, bin = b.y >= level;
        if (ain) out.push_back(a);
        if (ain != bin) {
            const double t = (level - a.y) / (b.y - a.y);
            out.push_back(P(a.x + (b.x - a.x) * t, level));
        }
    }
    return out;
}

// ---- l'etat d'un symbole ----------------------------------------------------------------
struct Sym {
    double x{0}, y{0}, w{0}, h{0};     // la place du symbole (sans son libelle)
    double value{0}, frac{0};          // la valeur ; son rang entre min et max (0..1)
    bool   on{false}, fault{false}, moving{false};
    double t{0};                       // secondes, pour ce qui bouge
    gfx::Color col, off, line, txt;   // la couleur de l'etat ; a l'arret ; le trait ; le texte
    double lw{2};
    double cx() const { return x + w / 2; }
    double cy() const { return y + h / 2; }
    double r() const { return std::min(w, h) / 2; }
};

bool truthy(const std::string& s, double& number) {
    if (hmi::parseNumber(s, number)) return number != 0;
    const bool b = hmi::parseBool(s, false);
    number = b ? 1 : 0;
    return b;
}

Sym prepare(const Ctx& c) {
    Sym s;
    const double w = c.w(), h = c.h();
    s.x = 0;
    s.y = 0;
    s.w = w;
    s.h = h;
    // Le libelle : une bande dessous (ou dessus) ; le symbole prend le reste.
    const std::string label = c.src.text(c.o, "label");
    const std::string where = c.src.text(c.o, "labelPosition", "dessous");
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 6.0, 60.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xC8D0DC));
    if (!label.empty() && where != "aucun") {
        const double band = fs * 1.5;
        if (h > fs * 3) {
            s.h = h - band;
            if (where == "dessus") s.y = band;
            text(c, label, 0, where == "dessus" ? 0 : h - band, w, band, txt, fs, "centre");
        } else {
            // Un symbole mince (tube, fleche) : le libelle a cote de sa boite.
            text(c, label, 0, where == "dessus" ? -band - 1 : h + 1, w, band, txt, fs, "centre");
        }
    }
    const std::string value = c.src.text(c.o, "value");
    s.on = truthy(value, s.value);
    double lo = c.src.number(c.o, "min", 0), hi = c.src.number(c.o, "max", 100);
    if (hi <= lo) hi = lo + 1;
    s.frac = std::clamp((s.value - lo) / (hi - lo), 0.0, 1.0);
    double f = 0;
    s.fault = truthy(c.src.text(c.o, "fault"), f);
    s.t = c.opt.editor ? 0.0 : c.opt.time;
    s.moving = !c.opt.editor && s.on && !s.fault && c.src.flag(c.o, "animate", true);
    const gfx::Color on = c.color("colorOn", gfx::Color::rgb(0x2ECC71));
    s.off = c.color("colorOff", gfx::Color::rgb(0x5A6577));
    const gfx::Color bad = c.color("colorFault", gfx::Color::rgb(0xE5534B));
    // Le defaut clignote : sa couleur, puis la meme, assombrie.
    const bool blink = !c.opt.editor && std::fmod(c.opt.time, 1.0) >= 0.5;
    s.col = s.fault ? (blink ? shade(bad, 0.45f) : bad) : s.on ? on : s.off;
    s.line = c.color("stroke", gfx::Color::rgb(0xC8D0DC));
    if (s.fault) s.line = s.col;
    s.txt = txt;
    s.lw = std::max(1.0, c.src.number(c.o, "strokeWidth", 2));
    return s;
}

std::string shownValue(const Ctx& c, double v) {
    std::string out = hmi::formatValue(sim::Value::real(v), c.src.text(c.o, "format", "0"));
    const std::string unit = c.src.text(c.o, "unit");
    return unit.empty() ? out : out + " " + unit;
}

// Un contenant : sa forme, son contenu jusqu'au niveau, sa valeur ecrite.
void drawVessel(const Ctx& c, const Sym& s, const Pts& shape, double top, double bottom, double textY) {
    fill(c, shape, c.color("fill", gfx::Color::rgb(0x1F252E)));
    const double level = bottom - (bottom - top) * s.frac;
    if (s.frac > 0) fill(c, below(shape, level), c.color("fillColor", gfx::Color::rgb(0x3C8DDB)));
    // Une ligne de surface, plus claire.
    if (s.frac > 0.01 && s.frac < 0.999) {
        const Pts cut = below(shape, level);
        double l = 1e9, r = -1e9;
        for (const auto& p : cut)
            if (std::fabs(p.y - level) < 0.01) { l = std::min<double>(l, p.x); r = std::max<double>(r, p.x); }
        if (r > l) seg(c, l, level, r, level, withA(gfx::Color{255, 255, 255, 255}, 0.35f * c.alpha), 1.2);
    }
    outline(c, shape, true, s.fault ? s.col : s.line, s.lw);
    // Lot 11 : les seuils (tres bas, bas, haut, tres haut) - un trait tirete a
    // leur niveau et leur etiquette a droite ; franchis, l'etiquette se remplit.
    {
        double lo = c.src.number(c.o, "min", 0), hi = c.src.number(c.o, "max", 100);
        if (hi <= lo) hi = lo + 1;
        const gfx::Color warn = c.color("colorWarning", gfx::Color::rgb(0xF2C94C)), alarm = c.color("colorAlarm", gfx::Color::rgb(0xE5534B));
        const struct { const char* key; const char* tag; bool high; bool strong; } limits[] = {
            {"lowAlarm", "LL", false, true}, {"low", "L", false, false}, {"high", "H", true, false}, {"highAlarm", "HH", true, true}};
        // Le trait de chaque seuil a son niveau ; les etiquettes ensuite, ecartees
        // si deux seuils sont trop proches pour qu'elles se lisent (80 et 88 sur
        // une tremie basse) - le trait, lui, reste a sa place.
        struct Tag { double y, x; const char* text; gfx::Color col; bool crossed; };
        std::vector<Tag> tags;
        const double fs = std::clamp(std::min(s.w * 0.14, 12.0), 7.0, 12.0), th = fs * 1.35;
        for (const auto& lim : limits) {
            double v = 0;
            const std::string t = c.src.text(c.o, lim.key);
            if (t.empty() || !hmi::parseNumber(t, v) || v < lo || v > hi) continue;
            const double y = bottom - (bottom - top) * (v - lo) / (hi - lo);
            // La corde de la forme a ce niveau.
            const Pts cut = below(shape, y);
            double l = 1e9, r = -1e9;
            for (const auto& p : cut)
                if (std::fabs(p.y - y) < 0.01) { l = std::min<double>(l, p.x); r = std::max<double>(r, p.x); }
            if (r <= l) continue;
            const bool crossed = lim.high ? s.value >= v : s.value <= v;
            const gfx::Color col = lim.strong ? alarm : warn;
            for (double x = l + 2; x < r - 2; x += 7) seg(c, x, y, std::min(x + 4, r - 2), y, col, 1.3);
            tags.push_back({y, r + 2, lim.tag, col, crossed});
        }
        std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.y < b.y; });
        for (std::size_t k = 1; k < tags.size(); ++k)
            if (tags[k].y - tags[k - 1].y < th + 1) tags[k].y = tags[k - 1].y + th + 1;
        for (const auto& tg : tags) {
            const double tw = fs * (tg.text[1] ? 1.9 : 1.3);
            const Pts tag = box(tg.x, tg.y - th / 2, tw, th, 2);
            fill(c, tag, tg.crossed ? tg.col : c.fixed(0x1B2028));
            outline(c, tag, true, tg.col, 1);
            text(c, tg.text, tg.x, tg.y - th / 2, tw, th, tg.crossed ? c.fixedOn(0x111418) : tg.col, fs, "centre");
        }
    }
    if (c.src.flag(c.o, "showValue", true)) {
        // Sur une pastille sombre : lisible sur tous les contenus (grain clair, gaz).
        const double fs = std::clamp(std::min(s.w * 0.2, 18.0), 8.0, 18.0);
        const std::string shown = shownValue(c, s.value);
        const double bw = std::min(s.w - 6, fs * 0.62 * static_cast<double>(shown.size()) + fs * 0.9);
        fill(c, box(s.cx() - bw / 2, textY - fs * 0.75, bw, fs * 1.5, fs * 0.35), withA(c.seen(gfx::Color{16, 20, 27, 255}), 0.6f * c.alpha));
        text(c, shown, s.x, textY - fs, s.w, fs * 2, s.txt, fs, "centre");
    }
}

// L'actionneur d'une vanne, au-dessus de sa tige (1.10.4 : la vanne et la vanne
// 3 voies le partagent) : volant, moteur M, membrane, membrane et positionneur.
void drawActuator(const Ctx& c, const Sym& s, const std::string& type, double hw, double stemTop) {
    if (type == "r\xC3\xA9glante") {
        // Une membrane et son positionneur : l'actionneur d'une vanne de regulation.
        const double r = std::min(s.h * 0.22, hw * 0.7);
        Pts dome;
        for (int k = 0; k <= 20; ++k) {
            const double ang = kPi + kPi * k / 20.0;
            dome.push_back(P(s.cx() + r * std::cos(ang), stemTop + r * 0.2 + r * std::sin(ang)));
        }
        fill(c, dome, c.fixed(0x2B333F));
        outline(c, dome, true, s.line, s.lw * 0.8);
        const Pts pos = box(s.cx() - r - r * 0.55, stemTop + r * 0.35, r * 0.5, r * 0.5, 1);
        fill(c, pos, c.fixed(0x3A4556));
        outline(c, pos, true, s.line, 1);
    } else if (type == "motoris\xC3\xA9" "e") {
        const double r = std::min(s.h * 0.2, hw * 0.55);
        fill(c, circle(s.cx(), s.y + r + 1, r), c.fixed(0x2B333F));
        outline(c, circle(s.cx(), s.y + r + 1, r), true, s.line, s.lw * 0.8);
        text(c, "M", s.cx() - r, s.y + 1, 2 * r, 2 * r, s.txt, r * 1.2, "centre");
    } else if (type == "pneumatique") {
        const double r = std::min(s.h * 0.22, hw * 0.7);
        Pts dome;
        for (int k = 0; k <= 20; ++k) {
            const double ang = kPi + kPi * k / 20.0;
            dome.push_back(P(s.cx() + r * std::cos(ang), stemTop + r * 0.2 + r * std::sin(ang)));
        }
        fill(c, dome, c.fixed(0x2B333F));
        outline(c, dome, true, s.line, s.lw * 0.8);
    } else {
        const double r = std::min(hw * 0.6, s.h * 0.25);
        seg(c, s.cx() - r, stemTop, s.cx() + r, stemTop, s.line, s.lw * 1.4);
    }
}

// ---- process ------------------------------------------------------------------------------
void drawValve(const Ctx& c, const Sym& s) {
    const std::string type = c.src.text(c.o, "valveType", "manuelle");
    const double top = s.y + s.h * 0.42, bh = s.h - (top - s.y), bcy = top + bh / 2;
    const double hw = std::min(s.w / 2, bh * 1.1);
    const double x0 = s.cx() - hw, x1 = s.cx() + hw;
    const double a = bh * 0.42;
    const Pts left{P(x0, bcy - a), P(x0, bcy + a), P(s.cx(), bcy)}, right{P(x1, bcy - a), P(x1, bcy + a), P(s.cx(), bcy)};
    // Lot 11 : la vanne reglante - son ouverture (0 a 100 %) et son mouvement.
    const std::string openingText = c.src.text(c.o, "opening");
    double opening = 0;
    const bool regulating = !openingText.empty() && hmi::parseNumber(openingText, opening);
    opening = std::clamp(opening, 0.0, 100.0);
    double moved = 0;
    const bool moving = !c.opt.editor && truthy(c.src.text(c.o, "moving"), moved);
    gfx::Color body = s.col;
    if (regulating && !s.fault) body = opening > 0.5 ? c.color("colorOn", gfx::Color::rgb(0x2ECC71)) : s.off;
    fill(c, left, body);
    fill(c, right, body);
    outline(c, left, true, s.line, s.lw);
    outline(c, right, true, s.line, s.lw);
    const double stemTop = s.y + s.h * 0.2;
    const bool blinkStem = moving && std::fmod(c.opt.time, 0.6) >= 0.3;
    seg(c, s.cx(), bcy, s.cx(), stemTop, blinkStem ? c.color("colorOn", gfx::Color::rgb(0x2ECC71)) : s.line, s.lw);
    if (regulating) {
        // L'ouverture : une jauge sous le corps, et le pourcentage a droite de la tige.
        const double gy = std::min(s.y + s.h - 4, bcy + a + 3), gw = x1 - x0;
        fill(c, box(x0, gy, gw, 4, 1), fade(gfx::Color{255, 255, 255, 30}, c.alpha));
        fill(c, box(x0, gy, gw * opening / 100.0, 4, 1), c.color("colorOn", gfx::Color::rgb(0x2ECC71)));
        // Le pourcentage, a droite de l'actionneur (sa membrane, son volant...),
        // a la hauteur de la tige : il ne le recouvre pas.
        const double fs = std::clamp(std::min(s.h * 0.2, 12.0), 7.0, 12.0);
        const double act = std::min(s.h * 0.22, hw * 0.7);
        const double tx = s.cx() + act + 3;
        text(c, hmi::formatNumber(std::round(opening)) + " %", tx, stemTop - fs * 0.2, std::max(fs * 3.2, s.x + s.w - tx), fs * 1.5, s.txt, fs,
             "gauche");
        // Le repere de position sur la tige.
        const double py = bcy - (bcy - stemTop) * opening / 100.0;
        seg(c, s.cx() - 4, py, s.cx() + 4, py, s.txt, 1.4);
    }
    drawActuator(c, s, type, hw, stemTop);
}

// 1.10.4 : la vanne 3 voies. Trois triangles tete-beche qui se rejoignent au
// centre : la voie 1 a gauche, la voie 2 a droite, la voie 3 en bas ; au
// centre, le boisseau (en T ou en L) tourne vers la voie active. Comme la
// maquette 1.10.4 : les deux voies du passage prennent la couleur du passage, la
// troisieme reste eteinte ; fermee, tout est eteint et une croix rouge barre le
// boisseau ; en mouvement, le passage est ambre et clignote ; en defaut, il est
// rouge, dans un cadre tirete. Une fleche sur chaque voie du passage dit le
// sens : melangeuse (1 et 2 entrent, 3 sort), repartitrice (3 entre, 1 et 2
// sortent). L'actionneur, la tige qui clignote en mouvement, comme la vanne.
//  La voie active : 0 fermee, 1 la voie 1-2, 2 la voie 1-3, 3 la voie 2-3 -
//  "value" (un entier : 0 a 3, ou 12, 13, 23 ; un texte "1-2"...), ou deux
//  booleens A + 2 x B ("positionMode" = "deux booleens").
int threeWayFromText(std::string t) {
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
    double n = 0;
    if (hmi::parseNumber(t, n)) {
        const long v = std::lround(n);
        if (v == 1 || v == 12 || v == 21) return 1;
        if (v == 2 || v == 13 || v == 31) return 2;
        if (v == 3 || v == 23 || v == 32) return 3;
        return 0;
    }
    if (t.find("1-2") != std::string::npos || t.find("2-1") != std::string::npos) return 1;
    if (t.find("1-3") != std::string::npos || t.find("3-1") != std::string::npos) return 2;
    if (t.find("2-3") != std::string::npos || t.find("3-2") != std::string::npos) return 3;
    return hmi::parseBool(t, false) ? 1 : 0;
}

void drawThreeWayValve(const Ctx& c, const Sym& s) {
    int way = 0;
    if (c.src.text(c.o, "positionMode", "entier").rfind("deux", 0) == 0) {
        double n = 0;
        const bool a = truthy(c.src.text(c.o, "positionA"), n), b = truthy(c.src.text(c.o, "positionB"), n);
        way = (a ? 1 : 0) + (b ? 2 : 0);
    } else {
        way = threeWayFromText(c.src.text(c.o, "value"));
    }
    // Les voies du passage : 1-2, 1-3, 2-3.
    const bool p1 = way == 1 || way == 2, p2 = way == 1 || way == 3, p3 = way == 2 || way == 3;
    const std::string type = c.src.text(c.o, "valveType", "manuelle");
    const bool mixing = c.src.text(c.o, "valve3Function", "m\xC3\xA9langeuse").rfind("r\xC3\xA9", 0) != 0;
    const bool lBore = c.src.text(c.o, "valve3Bore", "T") == "L";
    double moved = 0;
    const bool moving = !c.opt.editor && truthy(c.src.text(c.o, "moving"), moved);
    const gfx::Color on = c.color("colorOn", gfx::Color::rgb(0x2ECC71));
    // Comme la maquette 1.10.4 (scene 4) : les voies du passage prennent la couleur
    // du passage ; en defaut, le rouge (fixe, et un cadre tirete) ; en mouvement,
    // l'ambre qui clignote (0,9 s). Les autres voies restent eteintes.
    const gfx::Color bad = c.color("colorFault", gfx::Color::rgb(0xE5534B));
    const bool dim = moving && !s.fault && std::fmod(c.opt.time, 0.9) >= 0.45;
    const gfx::Color pass = s.fault ? bad : moving ? withA(c.color("colorMoving", gfx::Color::rgb(0xF0B429)), dim ? 0.35f : 1.f) : on;
    const gfx::Color line = c.color("stroke", gfx::Color::rgb(0xC8D0DC));
    const auto wayColor = [&](bool active) { return active ? pass : s.off; };

    const double cx = s.cx(), cy = s.y + s.h * 0.52;
    const double arm = std::max(4.0, std::min(s.w / 2, s.y + s.h - cy));   // du centre au bout d'une voie
    const double d = arm * 0.74, a = d * 0.52;                              // la base d'un triangle, sa demi-hauteur
    const Pts left{P(cx - d, cy - a), P(cx - d, cy + a), P(cx, cy)};
    const Pts right{P(cx + d, cy - a), P(cx + d, cy + a), P(cx, cy)};
    const Pts bottom{P(cx - a, cy + d), P(cx + a, cy + d), P(cx, cy)};
    fill(c, left, wayColor(p1));
    fill(c, right, wayColor(p2));
    fill(c, bottom, wayColor(p3));
    outline(c, left, true, line, s.lw);
    outline(c, right, true, line, s.lw);
    outline(c, bottom, true, line, s.lw);
    // Les bouts de tuyau ; sur ceux du passage, la fleche du sens (maquette 1.10.4) :
    // melangeuse, les voies 1 et 2 entrent, la voie 3 sort ; repartitrice, la voie
    // 3 entre, les voies 1 et 2 sortent.
    const double stub = arm - d, ahMax = std::max(1.0, a * 0.7), ah = std::clamp(stub * 0.75, std::min(3.0, ahMax), ahMax);
    const gfx::Color arrow = c.fixed(0xF4F6FA);
    const auto port = [&](double ux, double uy, bool active, bool in) {
        const double bx = cx + ux * d, by = cy + uy * d, ex = cx + ux * arm, ey = cy + uy * arm;
        seg(c, bx, by, ex, ey, wayColor(active), s.lw * 1.6);
        if (!active) return;
        // La pointe : vers le centre (entree) ou vers le dehors (sortie).
        const double mx = (bx + ex) / 2, my = (by + ey) / 2, dir = in ? -1.0 : 1.0;
        const double tx = mx + ux * dir * ah * 0.5, ty = my + uy * dir * ah * 0.5;
        const double bxx = mx - ux * dir * ah * 0.5, byy = my - uy * dir * ah * 0.5;
        fill(c, {P(tx, ty), P(bxx - uy * ah * 0.6, byy + ux * ah * 0.6), P(bxx + uy * ah * 0.6, byy - ux * ah * 0.6)}, arrow);
    };
    if (stub >= 3) {
        port(-1, 0, p1, mixing);            // la voie 1 : entre (melangeuse), sort (repartitrice)
        port(1, 0, p2, mixing);             // la voie 2 : de meme
        port(0, 1, p3, !mixing);            // la voie 3 : sort (melangeuse), entre (repartitrice)
    }
    // Le boisseau : un disque sombre, son passage vers les voies actives.
    const double rb = a * 0.62;
    fill(c, circle(cx, cy, rb), c.fixed(0x1F252E));
    outline(c, circle(cx, cy, rb), true, line, std::max(1.0, s.lw * 0.8));
    const gfx::Color bore = way ? pass : s.off;
    const double bw = std::max(1.5, rb * 0.42);
    if (way == 0) {
        // Fermee : une croix rouge sur le boisseau (maquette 1.10.4).
        seg(c, cx - rb * 0.62, cy - rb * 0.62, cx + rb * 0.62, cy + rb * 0.62, bad, bw);
        seg(c, cx + rb * 0.62, cy - rb * 0.62, cx - rb * 0.62, cy + rb * 0.62, bad, bw);
    } else {
        if (p1) seg(c, cx, cy, cx - rb * 0.9, cy, bore, bw);
        if (p2) seg(c, cx, cy, cx + rb * 0.9, cy, bore, bw);
        if (p3) seg(c, cx, cy, cx, cy + rb * 0.9, bore, bw);
        // En T : la troisieme branche du boisseau, vers le haut (sans voie).
        if (!lBore) seg(c, cx, cy, cx, cy - rb * 0.9, withA(bore, 0.55f), bw);
        fill(c, circle(cx, cy, bw * 0.55), bore);
    }
    // La tige et l'actionneur, comme la vanne.
    const double stemTop = s.y + s.h * 0.2;
    const bool blinkStem = moving && std::fmod(c.opt.time, 0.6) >= 0.3;
    seg(c, cx, cy - rb, cx, stemTop, blinkStem ? pass : line, s.lw);
    Sym sa = s;
    sa.line = line;
    drawActuator(c, sa, type, d, stemTop);
    // En defaut : un cadre tirete rouge autour du symbole (maquette 1.10.4).
    if (s.fault) {
        const double x0 = s.x + 1, y0 = s.y + 1, x1 = s.x + s.w - 1, y1 = s.y + s.h - 1;
        const auto dashes = [&](double ax, double ay, double bx, double by) {
            const double len = std::hypot(bx - ax, by - ay);
            for (double t = 0; t < len; t += 8) {
                const double u = t / len, v = std::min(t + 5, len) / len;
                seg(c, ax + (bx - ax) * u, ay + (by - ay) * u, ax + (bx - ax) * v, ay + (by - ay) * v, bad, 1.6);
            }
        };
        dashes(x0, y0, x1, y0);
        dashes(x1, y0, x1, y1);
        dashes(x1, y1, x0, y1);
        dashes(x0, y1, x0, y0);
    }
    // Le numero de chaque voie, pres de sa base.
    if (c.src.flag(c.o, "showPorts", true)) {
        const double fs = std::clamp(arm * 0.26, 7.0, 12.0);
        text(c, "1", cx - arm, cy - a - fs * 1.5, arm * 0.5, fs * 1.4, s.txt, fs, "centre");
        text(c, "2", cx + arm * 0.5, cy - a - fs * 1.5, arm * 0.5, fs * 1.4, s.txt, fs, "centre");
        text(c, "3", cx + a + 2, cy + arm - fs * 1.5, fs * 1.4, fs * 1.4, s.txt, fs, "gauche");
    }
}

void drawPump(const Ctx& c, const Sym& s) {
    const double r = std::min(s.w * 0.4, s.h * 0.4), cx = s.x + s.w * 0.45, cy = s.y + s.h * 0.45;
    // Le socle, la tubulure de refoulement.
    fill(c, {P(cx - r * 0.75, s.y + s.h), P(cx + r * 0.75, s.y + s.h), P(cx + r * 0.45, cy + r * 0.6), P(cx - r * 0.45, cy + r * 0.6)},
         c.fixed(0x3A4556));
    const Pts nozzle = box(cx, cy - r, std::min(s.x + s.w - cx, r * 1.25), r * 0.5);
    fill(c, nozzle, c.fixed(0x3A4556));
    outline(c, nozzle, true, s.line, s.lw * 0.8);
    fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    fill(c, circle(cx, cy, r * 0.92), withA(s.col, 0.28f));
    outline(c, circle(cx, cy, r), true, s.col, s.lw * 1.3);
    // La roue : trois aubes qui tournent en marche.
    const double ang = s.moving ? s.t * 360.0 * 1.2 : 0;
    for (int k = 0; k < 3; ++k) {
        const double a = ang + k * 120.0;
        const auto p0 = turn(cx, cy, cx + r * 0.15, cy, a), p1 = turn(cx, cy, cx + r * 0.72, cy - r * 0.18, a);
        const auto p2 = turn(cx, cy, cx + r * 0.72, cy + r * 0.12, a);
        fill(c, {p0, p1, p2}, s.col);
    }
    fill(c, circle(cx, cy, r * 0.16), s.line);
}

void drawMotor(const Ctx& c, const Sym& s) {
    const double r = std::min(s.w * 0.36, s.h * 0.45), cx = s.x + s.w * 0.42, cy = s.cy();
    // L'arbre, a droite ; la boite a bornes, dessus.
    const Pts shaft = box(cx + r, cy - r * 0.12, std::min(s.x + s.w - cx - r, r * 0.6), r * 0.24);
    fill(c, shaft, c.fixed(0x8A9BB0));
    fill(c, box(cx - r * 0.3, cy - r - r * 0.18, r * 0.6, r * 0.22), c.fixed(0x3A4556));
    fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    fill(c, circle(cx, cy, r * 0.92), withA(s.col, 0.3f));
    outline(c, circle(cx, cy, r), true, s.col, s.lw * 1.3);
    text(c, "M", cx - r, cy - r, 2 * r, 2 * r, s.txt, r * 0.95, "centre");
    // En marche : deux reperes tournent sur la carcasse.
    if (s.moving || s.on) {
        const double a = s.moving ? s.t * 240.0 : 30.0;
        for (int k = 0; k < 2; ++k) {
            const auto arc = shapes::arc(P(cx, cy), static_cast<float>(r * 1.12), static_cast<float>(a + k * 180.0),
                                         static_cast<float>(a + k * 180.0 + 50.0), 10);
            outline(c, arc, false, s.col, s.lw);
        }
    }
}

// La tuyauterie : droite, coude, te, croix ; le fluide coule en marche.
void drawPipe(const Ctx& c, const Sym& s) {
    const std::string shape = c.src.text(c.o, "pipeShape", "droit");
    const bool horizontal = s.w >= s.h;
    double th = std::clamp(c.src.number(c.o, "thickness", 14), 2.0, std::max(2.0, std::min(s.w, s.h)));
    if (shape == "droit") th = std::min(th, horizontal ? s.h : s.w);
    struct Run { double x0, y0, x1, y1; };
    std::vector<Run> runs;
    const double cx = s.cx(), cy = s.cy();
    if (shape == "coude") runs = {{s.x, cy, cx + th / 2, cy}, {cx, cy, cx, s.y + s.h}};
    else if (shape == "t\xC3\xA9") runs = {{s.x, cy, s.x + s.w, cy}, {cx, cy, cx, s.y + s.h}};
    else if (shape == "croix") runs = {{s.x, cy, s.x + s.w, cy}, {cx, s.y, cx, s.y + s.h}};
    else if (horizontal) runs = {{s.x, cy, s.x + s.w, cy}};
    else runs = {{cx, s.y, cx, s.y + s.h}};
    const gfx::Color body = s.fault ? s.col : c.color("fill", gfx::Color::rgb(0x4A5568));
    for (const auto& r : runs) {
        const bool hz = std::fabs(r.y1 - r.y0) < 1e-6;
        const Pts b = hz ? box(std::min(r.x0, r.x1), r.y0 - th / 2, std::fabs(r.x1 - r.x0), th)
                         : box(r.x0 - th / 2, std::min(r.y0, r.y1), th, std::fabs(r.y1 - r.y0));
        fill(c, b, body);
    }
    for (const auto& r : runs) {
        const bool hz = std::fabs(r.y1 - r.y0) < 1e-6;
        const gfx::Color edge = shade(body, 0.6f);
        if (hz) {
            seg(c, std::min(r.x0, r.x1), r.y0 - th / 2, std::max(r.x0, r.x1), r.y0 - th / 2, edge, 1);
            seg(c, std::min(r.x0, r.x1), r.y0 + th / 2, std::max(r.x0, r.x1), r.y0 + th / 2, edge, 1);
        } else {
            seg(c, r.x0 - th / 2, std::min(r.y0, r.y1), r.x0 - th / 2, std::max(r.y0, r.y1), edge, 1);
            seg(c, r.x0 + th / 2, std::min(r.y0, r.y1), r.x0 + th / 2, std::max(r.y0, r.y1), edge, 1);
        }
    }
    // Le fluide : des traits qui avancent (en arriere si la valeur est negative).
    if (!s.on || s.fault) return;
    const gfx::Color fluid = c.color("fluidColor", gfx::Color::rgb(0x4FA3FF));
    const double dash = th * 1.3, gap = th * 0.9, period = dash + gap;
    const double speed = std::max(0.0, c.src.number(c.o, "speed", 60));
    const double phase = s.moving ? std::fmod(s.t * speed * (s.value < 0 ? -1.0 : 1.0), period) : period * 0.3;
    for (const auto& r : runs) {
        const double len = std::hypot(r.x1 - r.x0, r.y1 - r.y0);
        if (len <= 0) continue;
        const double ux = (r.x1 - r.x0) / len, uy = (r.y1 - r.y0) / len;
        for (double d = -period + std::fmod(phase + period, period); d < len; d += period) {
            const double a = std::max(0.0, d), b = std::min(len, d + dash);
            if (b <= a) continue;
            const double t2 = th * 0.22;
            const double ax = r.x0 + ux * a, ay = r.y0 + uy * a, bx = r.x0 + ux * b, by = r.y0 + uy * b;
            fill(c, {P(ax - uy * t2, ay + ux * t2), P(bx - uy * t2, by + ux * t2), P(bx + uy * t2, by - ux * t2), P(ax + uy * t2, ay - ux * t2)},
                 fluid);
        }
    }
}

void drawFan(const Ctx& c, const Sym& s) {
    const double r = s.r() * 0.94, cx = s.cx(), cy = s.cy();
    fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    outline(c, circle(cx, cy, r), true, s.line, s.lw);
    const double a0 = s.moving ? s.t * 360.0 * 1.5 : 20.0;
    for (int k = 0; k < 4; ++k) {
        const double a = a0 + k * 90.0;
        Pts blade;
        for (int i = 0; i <= 12; ++i) {
            const double u = i / 12.0;
            blade.push_back(turn(cx, cy, cx + r * (0.14 + 0.72 * u), cy - r * 0.2 * std::sin(u * kPi), a));
        }
        for (int i = 12; i >= 0; --i) {
            const double u = i / 12.0;
            blade.push_back(turn(cx, cy, cx + r * (0.14 + 0.72 * u), cy + r * 0.06 * std::sin(u * kPi), a));
        }
        fill(c, blade, s.col);
    }
    fill(c, circle(cx, cy, r * 0.16), s.line);
}

void drawCompressor(const Ctx& c, const Sym& s) {
    const double r = std::min(s.w * 0.42, s.h * 0.42), cx = s.cx(), cy = s.y + s.h * 0.45;
    fill(c, {P(cx - r * 0.8, s.y + s.h), P(cx + r * 0.8, s.y + s.h), P(cx + r * 0.5, cy + r * 0.6), P(cx - r * 0.5, cy + r * 0.6)},
         c.fixed(0x3A4556));
    fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    fill(c, circle(cx, cy, r * 0.92), withA(s.col, 0.28f));
    outline(c, circle(cx, cy, r), true, s.col, s.lw * 1.3);
    // Le symbole : deux lignes qui se resserrent (l'air comprime).
    const auto a = turn(cx, cy, cx - r * 0.62, cy - r * 0.78, 0), b = turn(cx, cy, cx + r * 0.78, cy - r * 0.36, 0);
    const auto d = turn(cx, cy, cx - r * 0.62, cy + r * 0.78, 0), e = turn(cx, cy, cx + r * 0.78, cy + r * 0.36, 0);
    seg(c, a.x, a.y, b.x, b.y, s.line, s.lw);
    seg(c, d.x, d.y, e.x, e.y, s.line, s.lw);
    if (s.moving) {
        const double ang = s.t * 300.0;
        const auto p = turn(cx, cy, cx + r * 0.78, cy, ang);
        fill(c, circle(p.x, p.y, r * 0.1), s.col);
    }
}

void drawExchanger(const Ctx& c, const Sym& s) {
    const double r = s.r() * 0.8, cx = s.cx(), cy = s.cy();
    // Les piquages : chaud (gauche, droite), froid (haut, bas).
    seg(c, s.x, cy, cx - r, cy, s.line, s.lw);
    seg(c, cx + r, cy, s.x + s.w, cy, s.line, s.lw);
    seg(c, cx, s.y, cx, cy - r, s.line, s.lw);
    seg(c, cx, cy + r, cx, s.y + s.h, s.line, s.lw);
    fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    fill(c, circle(cx, cy, r * 0.94), withA(s.col, 0.25f));
    outline(c, circle(cx, cy, r), true, s.col, s.lw * 1.3);
    Pts zig;
    const int n = 6;
    for (int k = 0; k <= n; ++k) {
        const double u = static_cast<double>(k) / n;
        const double wave = s.moving ? std::sin(s.t * 6 + k) * 0.08 : 0;
        zig.push_back(P(cx - r * 0.78 + r * 1.56 * u, cy + (k % 2 ? -1 : 1) * r * (0.36 + wave)));
    }
    outline(c, zig, false, s.on && !s.fault ? c.fixed(0xF2994A) : s.line, s.lw);
}

void drawFilter(const Ctx& c, const Sym& s) {
    const double cx = s.cx(), cy = s.cy(), rx = s.w * 0.42, ry = s.h * 0.46;
    seg(c, s.x, cy, cx - rx, cy, s.line, s.lw);
    seg(c, cx + rx, cy, s.x + s.w, cy, s.line, s.lw);
    const Pts diamond{P(cx, cy - ry), P(cx + rx, cy), P(cx, cy + ry), P(cx - rx, cy)};
    fill(c, diamond, c.fixed(0x1F252E));
    fill(c, {P(cx, cy - ry * 0.9), P(cx + rx * 0.9, cy), P(cx, cy + ry * 0.9), P(cx - rx * 0.9, cy)}, withA(s.col, 0.3f));
    outline(c, diamond, true, s.col, s.lw * 1.3);
    // Le media filtrant : un trait tirete, de haut en bas.
    for (double y = cy - ry * 0.85; y < cy + ry * 0.85; y += ry * 0.24)
        seg(c, cx, y, cx, std::min(y + ry * 0.13, cy + ry * 0.85), s.line, s.lw);
}

void drawBoiler(const Ctx& c, const Sym& s) {
    const double bw = s.w * 0.78, bx = s.x + (s.w - bw) / 2 - s.w * 0.06, top = s.y + s.h * 0.14;
    // La cheminee, a droite.
    const Pts chimney = box(bx + bw * 0.72, s.y, bw * 0.16, s.h * 0.2);
    fill(c, chimney, c.fixed(0x3A4556));
    outline(c, chimney, true, s.line, s.lw * 0.8);
    const Pts body = box(bx, top, bw, s.y + s.h - top, bw * 0.12);
    fill(c, body, c.fixed(0x2B333F));
    outline(c, body, true, s.fault ? s.col : s.line, s.lw);
    // Le foyer, et sa flamme.
    const double fx = bx + bw / 2, fy = s.y + s.h * 0.88, fw = bw * 0.5, fh = (s.h - top) * 0.46;
    const Pts hearth = box(fx - fw / 2 - 4, fy - fh - 6, fw + 8, fh + 10, 4);
    fill(c, hearth, c.fixed(0x141820));
    const double flick = s.moving ? 0.85 + 0.15 * std::sin(s.t * 11) + 0.06 * std::sin(s.t * 23) : (s.on ? 1.0 : 0.35);
    const double hh = fh * flick;
    const auto flame = [&](double scale) {
        Pts f;
        for (int k = 0; k <= 24; ++k) {
            const double u = k / 24.0, ang = u * 2 * kPi;
            const double rx = fw / 2 * scale * (0.6 + 0.4 * std::sin(u * kPi));
            f.push_back(P(fx + rx * std::sin(ang), fy - hh * scale * 0.5 - hh * scale * 0.5 * std::cos(ang) * (std::cos(ang) > 0 ? 1.0 : 0.6)));
        }
        return f;
    };
    const gfx::Color outer = s.on && !s.fault ? c.fixed(0xF2994A) : c.fixed(0x5A6577);
    const gfx::Color inner = s.on && !s.fault ? c.fixed(0xF2C94C) : c.fixed(0x46505F);
    fill(c, flame(1.0), outer);
    fill(c, flame(0.55), inner);
    // Un voyant d'etat, en haut a gauche du corps.
    fill(c, circle(bx + bw * 0.16, top + bw * 0.16, std::max(3.0, bw * 0.06)), s.col);
}

void drawConveyor(const Ctx& c, const Sym& s) {
    const double bh = s.h * 0.44, top = s.y + s.h * 0.08, rr = bh / 2;
    const double x0 = s.x + 2, x1 = s.x + s.w - 2;
    // Les pieds.
    for (const double lx : {x0 + rr, x1 - rr})
        seg(c, lx, top + bh, lx, s.y + s.h, c.fixed(0x8A9BB0), s.lw);
    const Pts belt = box(x0, top, x1 - x0, bh, rr);
    fill(c, belt, c.fixed(0x1F252E));
    outline(c, belt, true, s.col, s.lw * 1.6);
    // Les rouleaux.
    const int rollers = std::max(2, static_cast<int>((x1 - x0) / (bh * 1.6)));
    for (int k = 0; k <= rollers; ++k) {
        const double rx = x0 + rr + (x1 - x0 - 2 * rr) * k / rollers;
        fill(c, circle(rx, top + rr, rr * 0.55), c.fixed(0x3A4556));
        outline(c, circle(rx, top + rr, rr * 0.55), true, s.line, 1);
    }
    // Des chevrons qui avancent sur la bande, en marche.
    if (!s.on || s.fault) return;
    const double speed = std::max(0.0, c.src.number(c.o, "speed", 40)), dir = s.value < 0 ? -1.0 : 1.0;
    const double step = bh * 1.1, phase = s.moving ? std::fmod(s.t * speed * dir, step) : 0;
    for (double x = x0 + rr + std::fmod(phase + step, step); x < x1 - rr; x += step) {
        const double a = bh * 0.22;
        const double tip = x + dir * a;
        outline(c, {P(x - dir * a * 0.2, top - a * 0.2), P(tip, top + rr * 0.05), P(x - dir * a * 0.2, top + a * 0.8 - a * 0.2)}, false, s.col, s.lw);
    }
}

void drawCylinder(const Ctx& c, const Sym& s) {
    const double bh = s.h * 0.62, cy = s.cy(), bx = s.x + 2, bw = s.w * 0.56;
    const Pts barrel = box(bx, cy - bh / 2, bw, bh, 3);
    // Les orifices, dessus.
    fill(c, box(bx + bw * 0.08, cy - bh / 2 - s.h * 0.14, bw * 0.08, s.h * 0.14), c.fixed(0x3A4556));
    fill(c, box(bx + bw * 0.84, cy - bh / 2 - s.h * 0.14, bw * 0.08, s.h * 0.14), c.fixed(0x3A4556));
    fill(c, barrel, c.fixed(0x1F252E));
    // Le piston et la tige : leur course suit la valeur (min : rentre, max : sorti).
    const double travel = bw * 0.78, px = bx + bw * 0.08 + travel * s.frac;
    const double rodEnd = std::min(s.x + s.w - 2, px + bw * 0.1 + (s.w - bw - 6));
    const gfx::Color rod = s.fault ? s.col : s.frac > 0.001 ? c.color("colorOn", gfx::Color::rgb(0x2ECC71)) : s.off;
    fill(c, box(px, cy - bh * 0.1, rodEnd - px, bh * 0.2), rod);
    fill(c, box(rodEnd - s.w * 0.03, cy - bh * 0.3, s.w * 0.03, bh * 0.6), rod);
    fill(c, box(px - bw * 0.04, cy - bh / 2 + 2, bw * 0.08, bh - 4), withA(rod, 0.9f));
    outline(c, barrel, true, s.fault ? s.col : s.line, s.lw);
}

void drawMixer(const Ctx& c, const Sym& s) {
    const double top = s.y + s.h * 0.22, vw = s.w * 0.9, vx = s.x + (s.w - vw) / 2;
    const Pts vessel = box(vx, top, vw, s.y + s.h - top, vw * 0.14);
    fill(c, vessel, c.color("fill", gfx::Color::rgb(0x1F252E)));
    fill(c, below(vessel, top + (s.y + s.h - top) * 0.35), withA(c.color("fillColor", gfx::Color::rgb(0x3C8DDB)), 0.55f));
    outline(c, vessel, true, s.fault ? s.col : s.line, s.lw);
    // Le moteur, l'arbre, les pales qui tournent (vues de profil).
    const double mw = s.w * 0.3, mh = s.h * 0.16, mx = s.cx() - mw / 2;
    fill(c, box(mx, s.y, mw, mh, 3), s.col);
    outline(c, box(mx, s.y, mw, mh, 3), true, s.line, s.lw * 0.8);
    text(c, "M", mx, s.y, mw, mh, s.txt, mh * 0.8, "centre");
    const double shaftBottom = s.y + s.h * 0.78;
    seg(c, s.cx(), s.y + mh, s.cx(), shaftBottom, s.line, s.lw * 1.2);
    const double ang = s.moving ? s.t * 2 * kPi * 1.2 : 0.6;
    const double span = vw * 0.34 * std::cos(ang), bladeH = s.h * 0.05;
    for (const double sgn : {1.0, -1.0}) {
        const double ex = s.cx() + sgn * span;
        fill(c, {P(s.cx(), shaftBottom - bladeH), P(ex, shaftBottom - bladeH * 0.4), P(ex, shaftBottom + bladeH * 0.4), P(s.cx(), shaftBottom + bladeH)},
             s.col);
    }
}

void drawCheckValve(const Ctx& c, const Sym& s) {
    const double cy = s.cy(), a = s.h * 0.42;
    seg(c, s.x, cy, s.x + s.w, cy, s.line, s.lw);
    const double x0 = s.cx() - s.w * 0.22, x1 = s.cx() + s.w * 0.12;
    const Pts tri{P(x0, cy - a), P(x0, cy + a), P(x1, cy)};
    fill(c, tri, s.col);
    outline(c, tri, true, s.line, s.lw);
    // Le siege : la barre qui arrete le retour.
    seg(c, x1 + s.w * 0.04, cy - a, x1 + s.w * 0.04, cy + a, s.line, s.lw * 1.6);
}

void drawFlowArrow(const Ctx& c, const Sym& s) {
    const double cy = s.cy(), sh = s.h * 0.34, head = std::min(s.w * 0.4, s.h * 0.9);
    const double x0 = s.x + 1, x1 = s.x + s.w - 1, xh = x1 - head;
    const Pts arrow{P(x0, cy - sh / 2), P(xh, cy - sh / 2), P(xh, s.y + 1), P(x1, cy), P(xh, s.y + s.h - 1), P(xh, cy + sh / 2), P(x0, cy + sh / 2)};
    fill(c, arrow, withA(s.col, s.on ? 0.95f : 0.6f));
    outline(c, arrow, true, s.fault ? s.col : shade(s.col, 0.7f), 1);
    // En marche : une lueur qui file vers la pointe.
    if (s.moving) {
        const double u = std::fmod(s.t * 0.8, 1.0), gx = x0 + (xh - x0) * u, gw = (xh - x0) * 0.22;
        fill(c, box(std::max(x0, gx - gw / 2), cy - sh / 2 + 1, std::min(gw, xh - std::max(x0, gx - gw / 2)), sh - 2),
             withA(gfx::Color{255, 255, 255, 255}, 0.35f * c.alpha));
    }
}

void drawIsa(const Ctx& c, const Sym& s) {
    const bool showValue = c.src.flag(c.o, "showValue", true);
    const double fs = std::clamp(std::min(s.w, s.h) * 0.18, 7.0, 22.0);
    const double valueH = showValue ? fs * 1.6 : 0;
    const double r = std::min(s.w, s.h - valueH) / 2 * 0.96, cx = s.cx(), cy = s.y + r + 1;
    fill(c, circle(cx, cy, r), c.color("fill", gfx::Color::rgb(0x1F252E)));
    outline(c, circle(cx, cy, r), true, s.fault ? s.col : s.line, s.lw);
    // Le montage : terrain (rien), tableau (un trait), tableau arriere (tirete).
    const std::string mount = c.src.text(c.o, "mounting", "terrain");
    if (mount == "tableau") seg(c, cx - r, cy, cx + r, cy, s.line, s.lw * 0.8);
    else if (mount == "tableau arri\xC3\xA8re")
        for (double x = cx - r; x < cx + r; x += r * 0.3) seg(c, x, cy, std::min(x + r * 0.16, cx + r), cy, s.line, s.lw * 0.8);
    text(c, c.src.text(c.o, "function", "PT"), cx - r, cy - r * 0.78, 2 * r, r * 0.72, s.txt, r * 0.52, "centre");
    text(c, c.src.text(c.o, "loop", "101"), cx - r, cy + r * 0.06, 2 * r, r * 0.72, s.txt, r * 0.44, "centre");
    if (showValue) {
        const double by = cy + r + 2, bw2 = std::min(s.w, r * 2.6);
        const Pts badge = box(cx - bw2 / 2, by, bw2, valueH - 2, 3);
        fill(c, badge, c.fixed(0x141820));
        outline(c, badge, true, s.fault ? s.col : c.fixed(0x3A4556), 1);
        text(c, shownValue(c, s.value), cx - bw2 / 2, by, bw2, valueH - 2, s.fault ? s.col : c.fixedText(0x4FA3FF), fs,
             "centre");
    }
}

// ---- stockage ------------------------------------------------------------------------------
void drawTank(const Ctx& c, const Sym& s) {
    const double rad = std::min(s.w * 0.28, s.h * 0.14);
    const Pts shape = box(s.x + 1, s.y + 1, s.w - 2, s.h - 2, rad);
    drawVessel(c, s, shape, s.y + 1, s.y + s.h - 1, s.cy());
    // Des graduations a droite : 25, 50, 75 %.
    for (int k = 1; k < 4; ++k) {
        const double gy = s.y + s.h - (s.h * k / 4.0);
        seg(c, s.x + s.w - s.w * 0.12, gy, s.x + s.w - 2, gy, withA(s.line, 0.6f), 1);
    }
}

void drawGasBottle(const Ctx& c, const Sym& s) {
    const double neckH = s.h * 0.08, cx = s.cx();
    // Le robinet et le col.
    fill(c, box(cx - s.w * 0.14, s.y, s.w * 0.28, neckH * 0.55, 2), c.fixed(0x8A9BB0));
    fill(c, box(cx - s.w * 0.1, s.y + neckH * 0.5, s.w * 0.2, neckH * 0.6), c.fixed(0x6B7686));
    const double top = s.y + neckH, x0 = s.x + 1, x1 = s.x + s.w - 1, bottom = s.y + s.h - 1, rr = (x1 - x0) / 2;
    Pts shape;
    shape.push_back(P(x0, bottom - 3));
    shape.push_back(P(x0, top + rr));
    for (int k = 1; k < 20; ++k) {
        const double a = kPi + kPi * k / 20.0;
        shape.push_back(P(cx + rr * std::cos(a), top + rr + rr * std::sin(a)));
    }
    shape.push_back(P(x1, top + rr));
    shape.push_back(P(x1, bottom - 3));
    shape.push_back(P(x1 - 3, bottom));
    shape.push_back(P(x0 + 3, bottom));
    drawVessel(c, s, shape, top + rr, bottom, top + rr + (bottom - top - rr) * 0.55);
    // L'ogive : la couleur du gaz (norme), par-dessus le contenu.
    Pts dome;
    for (int k = 0; k <= 20; ++k) {
        const double a = kPi + kPi * k / 20.0;
        dome.push_back(P(cx + rr * std::cos(a), top + rr + rr * std::sin(a)));
    }
    fill(c, dome, c.color("gasColor", gfx::Color::rgb(0x1E6B3A)));
    outline(c, dome, true, s.fault ? s.col : s.line, s.lw);
}

void drawSilo(const Ctx& c, const Sym& s) {
    const double coneTop = s.y + s.h * 0.7, outW = s.w * 0.18, cx = s.cx();
    const Pts shape{P(s.x + 1, s.y + s.h * 0.08), P(cx, s.y + 1), P(s.x + s.w - 1, s.y + s.h * 0.08), P(s.x + s.w - 1, coneTop),
                    P(cx + outW / 2, s.y + s.h - s.h * 0.06), P(cx - outW / 2, s.y + s.h - s.h * 0.06), P(s.x + 1, coneTop)};
    drawVessel(c, s, shape, s.y + s.h * 0.08, s.y + s.h - s.h * 0.06, s.y + s.h * 0.42);
    fill(c, box(cx - outW / 2, s.y + s.h - s.h * 0.06, outW, s.h * 0.06), c.fixed(0x3A4556));
}

void drawHopper(const Ctx& c, const Sym& s) {
    const double cx = s.cx(), outW = s.w * 0.22, bottom = s.y + s.h * 0.84;
    const Pts shape{P(s.x + 1, s.y + 1), P(s.x + s.w - 1, s.y + 1), P(s.x + s.w - 1, s.y + s.h * 0.24), P(cx + outW / 2, bottom),
                    P(cx - outW / 2, bottom), P(s.x + 1, s.y + s.h * 0.24)};
    drawVessel(c, s, shape, s.y + 1, bottom, s.y + s.h * 0.3);
    const Pts chute = box(cx - outW / 2, bottom, outW, s.y + s.h - bottom);
    fill(c, chute, c.fixed(0x3A4556));
    outline(c, chute, true, s.line, s.lw * 0.8);
}

// ---- electrique -----------------------------------------------------------------------------
void drawBreaker(const Ctx& c, const Sym& s) {
    const double cx = s.cx(), side = std::min(s.w * 0.7, s.h * 0.36), top = s.cy() - side / 2;
    seg(c, cx, s.y, cx, top, s.col, s.lw);
    seg(c, cx, top + side, cx, s.y + s.h, s.col, s.lw);
    const Pts sq = box(cx - side / 2, top, side, side, 2);
    if (s.on && !s.fault) fill(c, sq, s.col);
    else fill(c, sq, c.fixed(0x1F252E));
    outline(c, sq, true, s.col, s.lw * 1.2);
    // Declenche (defaut) : une croix dans le carre.
    if (s.fault) {
        seg(c, cx - side * 0.3, top + side * 0.2, cx + side * 0.3, top + side * 0.8, s.col, s.lw);
        seg(c, cx + side * 0.3, top + side * 0.2, cx - side * 0.3, top + side * 0.8, s.col, s.lw);
    }
}

void drawDisconnector(const Ctx& c, const Sym& s) {
    const double cx = s.cx(), top = s.y + s.h * 0.3, pivot = s.y + s.h * 0.72;
    seg(c, cx, s.y, cx, top, s.col, s.lw);
    seg(c, cx - s.w * 0.22, top, cx + s.w * 0.22, top, s.col, s.lw);        // la marque du sectionneur
    seg(c, cx, pivot, cx, s.y + s.h, s.col, s.lw);
    const double ang = s.on ? 0.0 : -32.0;
    const auto tip = turn(cx, pivot, cx, top, ang);
    seg(c, cx, pivot, tip.x, tip.y, s.col, s.lw * 1.3);
    fill(c, circle(cx, pivot, std::max(2.0, s.w * 0.05)), s.col);
}

void drawContactor(const Ctx& c, const Sym& s) {
    const double cx = s.x + s.w * 0.62, top = s.y + s.h * 0.3, pivot = s.y + s.h * 0.72;
    seg(c, cx, s.y, cx, top, s.col, s.lw);
    Pts half;                                                                // la marque du contacteur
    for (int k = 0; k <= 12; ++k) {
        const double a = kPi * k / 12.0;
        half.push_back(P(cx + s.w * 0.1 * std::cos(a), top + s.w * 0.1 * std::sin(a) * -1 + s.w * 0.1));
    }
    outline(c, half, false, s.col, s.lw);
    seg(c, cx, pivot, cx, s.y + s.h, s.col, s.lw);
    const double ang = s.on ? 0.0 : -30.0;
    const auto tip = turn(cx, pivot, cx, top + s.w * 0.1, ang);
    seg(c, cx, pivot, tip.x, tip.y, s.col, s.lw * 1.3);
    // La bobine, et sa liaison mecanique (tiretee) avec le contact.
    const double bx = s.x + 2, bw = s.w * 0.3, by = s.cy() - s.h * 0.08;
    const Pts coil = box(bx, by, bw, s.h * 0.16, 2);
    fill(c, coil, s.on && !s.fault ? s.col : c.fixed(0x1F252E));
    outline(c, coil, true, s.col, s.lw);
    const double midY = (pivot + top + s.w * 0.1) / 2;
    for (double x = bx + bw; x < cx - 2; x += 6) seg(c, x, midY, std::min(x + 3, cx - 2), midY, s.line, 1);
}

void drawLamp(const Ctx& c, const Sym& s) {
    const double r = s.r() * 0.8, cx = s.cx(), cy = s.cy();
    if (s.on && !s.fault) {
        fill(c, circle(cx, cy, r * 1.22), withA(s.col, 0.25f));
        fill(c, circle(cx, cy, r), s.col);
    } else {
        fill(c, circle(cx, cy, r), c.fixed(0x1F252E));
    }
    outline(c, circle(cx, cy, r), true, s.fault ? s.col : s.line, s.lw);
    const double d = r * 0.7071;
    const gfx::Color cross = s.on && !s.fault ? shade(s.col, 0.45f) : s.line;
    seg(c, cx - d, cy - d, cx + d, cy + d, cross, s.lw);
    seg(c, cx + d, cy - d, cx - d, cy + d, cross, s.lw);
}

void drawTransformer(const Ctx& c, const Sym& s) {
    const double r = std::min(s.w * 0.4, s.h * 0.22), cx = s.cx();
    const double c1 = s.cy() - r * 0.6, c2 = s.cy() + r * 0.6;
    seg(c, cx, s.y, cx, c1 - r, s.col, s.lw);
    seg(c, cx, c2 + r, cx, s.y + s.h, s.col, s.lw);
    fill(c, circle(cx, c1, r), withA(s.col, 0.18f));
    fill(c, circle(cx, c2, r), withA(s.col, 0.18f));
    outline(c, circle(cx, c1, r), true, s.col, s.lw * 1.2);
    outline(c, circle(cx, c2, r), true, s.col, s.lw * 1.2);
}

} // namespace

bool drawSynoptic(const Ctx& c) {
    if (!hmi::kindIsSynoptic(c.o.kind)) return false;
    const Sym s = prepare(c);
    if (s.w <= 2 || s.h <= 2) return true;
    switch (c.o.kind) {
        case Kind::Valve:          drawValve(c, s); break;
        case Kind::ThreeWayValve:  drawThreeWayValve(c, s); break;   // 1.10.4
        case Kind::Pump:           drawPump(c, s); break;
        case Kind::Motor:          drawMotor(c, s); break;
        case Kind::Pipe:           drawPipe(c, s); break;
        case Kind::Tank:           drawTank(c, s); break;
        case Kind::GasBottle:      drawGasBottle(c, s); break;
        case Kind::Fan:            drawFan(c, s); break;
        case Kind::Compressor:     drawCompressor(c, s); break;
        case Kind::HeatExchanger:  drawExchanger(c, s); break;
        case Kind::Filter:         drawFilter(c, s); break;
        case Kind::Boiler:         drawBoiler(c, s); break;
        case Kind::Conveyor:       drawConveyor(c, s); break;
        case Kind::Cylinder:       drawCylinder(c, s); break;
        case Kind::IsaInstrument:  drawIsa(c, s); break;
        case Kind::CircuitBreaker: drawBreaker(c, s); break;
        case Kind::Disconnector:   drawDisconnector(c, s); break;
        case Kind::Contactor:      drawContactor(c, s); break;
        case Kind::Lamp:           drawLamp(c, s); break;
        case Kind::Transformer:    drawTransformer(c, s); break;
        case Kind::Silo:           drawSilo(c, s); break;
        case Kind::Hopper:         drawHopper(c, s); break;
        case Kind::Mixer:          drawMixer(c, s); break;
        case Kind::CheckValve:     drawCheckValve(c, s); break;
        case Kind::FlowArrow:      drawFlowArrow(c, s); break;
        default:                   return false;
    }
    // Lot 13 : l'accessibilite - un defaut se lit aussi a son triangle.
    if (s.fault && c.src.flag(c.o, "a11ySymbols", false)) drawFaultBadge(c, s.x + s.w, s.y, std::clamp(s.r() * 0.55, 10.0, 26.0));
    return true;
}

} // namespace app::paint
