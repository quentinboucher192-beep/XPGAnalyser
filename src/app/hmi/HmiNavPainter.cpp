// =============================================================================
//  app/hmi/HmiNavPainter.cpp - le dessin des objets de la navigation et de la
//                              structure (lot 12)
// -----------------------------------------------------------------------------
//  Barre de navigation, fil d'Ariane, conteneur a onglets, cadre avec titre,
//  panneau defilant, panneau repliable, plan a zones.
//
//  EN MARCHE, ils lisent le moteur (la vue courante, l'historique, les alarmes
//  de chaque zone, la zone choisie) ; DANS L'EDITEUR, la vue editee tient lieu
//  de vue courante. La geometrie est celle du clic (HmiNavigation).
// =============================================================================
#include "HmiPaintKit.hpp"

#include "../../hmi/HmiAlarmViews.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiRuntime.hpp"

#include <cmath>

namespace app::paint {

namespace {

using hmi::Kind;
using Pts = std::vector<gfx::Point>;

gfx::Color withA(gfx::Color c, float a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(c.a * a, 0.f, 255.f))); }
Pts rect(const hmi::Box& b, double radius = 0) {
    return shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(std::max(0.0, b.w)),
                               static_cast<float>(std::max(0.0, b.h)), static_cast<float>(radius));
}
void fillBox(const Ctx& c, const hmi::Box& b, gfx::Color col, double radius = 0) {
    if (col.a && b.w > 0 && b.h > 0) shapes::fillPolygon(c.r, c.map(rect(b, radius)), col);
}
void strokeBox(const Ctx& c, const hmi::Box& b, gfx::Color col, float width = 1.f, double radius = 0) {
    if (col.a && b.w > 0 && b.h > 0) shapes::strokePolyline(c.r, c.map(rect(b, radius)), true, col, std::max(1.f, width * c.vp.zoom));
}
// Un rectangle aux coins du haut arrondis (un onglet).
Pts topRounded(const hmi::Box& b, double r) {
    Pts p;
    const float x = static_cast<float>(b.x), y = static_cast<float>(b.y), w = static_cast<float>(b.w), h = static_cast<float>(b.h);
    const float rr = static_cast<float>(std::min(r, std::min(b.w, b.h) / 2));
    p.push_back({x, y + h});
    for (int i = 0; i <= 6; ++i) {
        const float a = 3.14159265f * (1.f + 0.5f * static_cast<float>(i) / 6.f);
        p.push_back({x + rr + rr * std::cos(a), y + rr + rr * std::sin(a)});
    }
    for (int i = 0; i <= 6; ++i) {
        const float a = 3.14159265f * (1.5f + 0.5f * static_cast<float>(i) / 6.f);
        p.push_back({x + w - rr + rr * std::cos(a), y + rr + rr * std::sin(a)});
    }
    p.push_back({x + w, y + h});
    return p;
}
double fontOf(const Ctx& c, double fallback) { return std::clamp(c.src.number(c.o, "fontSize", fallback), 7.0, 60.0); }
gfx::Color muted(const Ctx& c) { return c.fixed(0x8A96A8); }
void line(const Ctx& c, double x1, double y1, double x2, double y2, gfx::Color col, float width = 1.f) {
    c.r.line(c.map(x1, y1), c.map(x2, y2), col, std::max(1.f, width * c.vp.zoom));
}
// Un triangle plein : une fleche (gauche, droite, haut, bas) dans la boite.
void arrow(const Ctx& c, const hmi::Box& b, char dir, gfx::Color col) {
    const double cx = b.cx(), cy = b.cy(), s = std::min(b.w, b.h) * 0.22;
    Pts p;
    const auto P = [&](double x, double y) { p.push_back({static_cast<float>(x), static_cast<float>(y)}); };
    switch (dir) {
        case 'g': P(cx - s, cy); P(cx + s * 0.7, cy - s); P(cx + s * 0.7, cy + s); break;
        case 'd': P(cx + s, cy); P(cx - s * 0.7, cy - s); P(cx - s * 0.7, cy + s); break;
        case 'b': P(cx, cy + s * 0.8); P(cx - s, cy - s * 0.6); P(cx + s, cy - s * 0.6); break;
        default:  P(cx, cy - s * 0.8); P(cx - s, cy + s * 0.6); P(cx + s, cy + s * 0.6); break;
    }
    shapes::fillPolygon(c.r, c.map(p), col);
}
// Le nom de la vue qui compte comme "courante" : en marche celle du moteur (sous
// les popups), dans l'editeur la vue editee.
std::string currentViewName(const Ctx& c) {
    if (!c.opt.editor && c.opt.runtime && c.opt.project)
        if (const auto* v = c.opt.project->view(c.opt.runtime->currentView())) return v->name;
    return c.view.name;
}

// ================================================================ barre ===
void drawNavBar(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto items = hmi::navItems(c.opt.project, c.o);
    const auto l = hmi::navBarLayout(c.o, w, h, items.size());
    const std::string style = c.src.text(c.o, "tabStyle", "onglets");
    const std::string current = currentViewName(c);
    const gfx::Color bg = c.color("fill"), stroke = c.color("stroke");
    const gfx::Color button = c.color("buttonColor", gfx::Color::rgb(0x2A313C));
    const gfx::Color active = c.color("activeColor", gfx::Color::rgb(0x2F6FD6));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xC8D0DC));
    const gfx::Color activeTxt = c.color("activeTextColor", gfx::Color::rgb(0xFFFFFF));
    fillBox(c, {0, 0, w, h}, bg, style == "boutons" ? 6 : 0);
    if (items.empty()) {
        text(c, "Barre de navigation : aucune vue", 0, 0, w, h, muted(c), l.fontSize, "centre");
    }
    for (std::size_t i = 0; i < items.size() && i < l.items.size(); ++i) {
        const auto& b = l.items[i];
        const bool on = items[i].view == current;
        if (style == "liens") {
            text(c, fitted(c, items[i].label, b.w - 8, l.fontSize), b.x, b.y, b.w, b.h, on ? activeTxt : txt, l.fontSize, "centre");
            if (on) {
                const double tw = std::min(b.w - 8, hmi::approxTextWidth(items[i].label, l.fontSize));
                if (!l.vertical) fillBox(c, {b.cx() - tw / 2, b.bottom() - 4, tw, 3}, active, 1);
                else fillBox(c, {b.x, b.y + 4, 3, b.h - 8}, active, 1);
            }
        } else if (style == "boutons") {
            fillBox(c, b, on ? active : button, 5);
            text(c, fitted(c, items[i].label, b.w - 10, l.fontSize), b.x, b.y, b.w, b.h, on ? activeTxt : txt, l.fontSize, "centre");
        } else {
            // Des onglets : la vue courante prend la couleur active, un filet dessous.
            if (!l.vertical) {
                const hmi::Box tb{b.x, b.y + 4, b.w, b.h - 4};
                shapes::fillPolygon(c.r, c.map(topRounded(tb, 6)), on ? active : button);
            } else {
                fillBox(c, b, on ? active : button, 0);
                if (on) fillBox(c, {b.x, b.y, 4, b.h}, fade(gfx::Color::rgb(0xFFFFFF), c.alpha * 0.8f));
            }
            text(c, fitted(c, items[i].label, b.w - 10, l.fontSize), b.x, b.y + (l.vertical ? 0 : 2), b.w, b.h - (l.vertical ? 0 : 2),
                 on ? activeTxt : txt, l.fontSize, "centre");
        }
    }
    if (style == "onglets" && !l.vertical && !items.empty()) fillBox(c, {0, h - 2, w, 2}, active);
    // Precedent / Suivant : estompes quand l'historique ne permet pas d'y aller.
    if (l.back.w > 0) {
        const auto* rt = !c.opt.editor ? c.opt.runtime : nullptr;
        const bool canBack = !rt || !rt->backHistory().empty(), canFwd = !rt || !rt->forwardHistory().empty();
        fillBox(c, l.back, button, 5);
        fillBox(c, l.forward, button, 5);
        arrow(c, l.back, l.vertical ? 'g' : 'g', canBack ? activeTxt : withA(txt, 0.35f));
        arrow(c, l.forward, 'd', canFwd ? activeTxt : withA(txt, 0.35f));
    }
    if (stroke.a && style != "liens") strokeBox(c, {0, 0, w, h}, stroke, 1.f, style == "boutons" ? 6 : 0);
}

// ============================================================ fil d'Ariane ===
void drawBreadcrumb(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillBox(c, {0, 0, w, h}, c.color("fill"), 4);
    const std::string current = currentViewName(c);
    std::vector<std::string> history;
    std::string home;
    if (c.opt.project) {
        if (!c.opt.editor && c.opt.runtime) {
            history = c.opt.runtime->historyNames();
            if (const auto* hv = c.opt.project->view(c.opt.runtime->homeView())) home = hv->name;
        } else if (const auto* hv = c.opt.project->view(c.opt.project->config.startView)) {
            home = hv->name;
        }
    }
    std::vector<hmi::Crumb> trail;
    if (c.opt.project) trail = hmi::breadcrumbTrail(*c.opt.project, c.o, current, history, home);
    // Dans l'editeur, un historique d'exemple (l'accueil, puis la vue editee).
    if (c.opt.editor && c.src.text(c.o, "trail").rfind("hist", 0) == 0) {
        trail.clear();
        if (!home.empty() && home != current) trail.push_back({home, hmi::viewCaption(home), 1});
        trail.push_back({current, hmi::viewCaption(current), 0});
    }
    if (trail.empty()) trail.push_back({current, hmi::viewCaption(current), 0});
    const auto l = hmi::breadcrumbLayout(c.o, w, h, trail);
    const gfx::Color link = c.color("linkColor", gfx::Color::rgb(0x6FB1FF));
    const gfx::Color here = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    const std::string sep = c.src.text(c.o, "separator", "\xE2\x80\xBA");
    for (std::size_t i = 0; i < trail.size() && i < l.crumbs.size(); ++i) {
        const auto& b = l.crumbs[i];
        if (b.w <= 0) continue;
        const bool last = i + 1 == trail.size();
        text(c, trail[i].label, b.x, b.y, b.w + 20, b.h, last ? here : link, l.fontSize, "gauche");
        if (!last) {
            // Le soulignement, a la largeur vraie du texte.
            const auto px = static_cast<std::uint16_t>(std::clamp(l.fontSize * c.vp.zoom, 6.0, 200.0));
            const double tw = c.r.measure(trail[i].label, gfx::FontId{px}).width / std::max(0.01f, c.vp.zoom);
            line(c, b.x + 4, b.cy() + l.fontSize * 0.62, b.x + 4 + tw, b.cy() + l.fontSize * 0.62, withA(link, 0.45f));
        }
    }
    for (const auto& s : l.separators)
        if (s.w > 0) text(c, sep, s.x, s.y, s.w, s.h, muted(c), l.fontSize, "centre");
    strokeBox(c, {0, 0, w, h}, c.color("stroke"), 1.f, 4);
}

// ======================================================= conteneur a onglets ===
void drawTabContainer(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto labels = hmi::tabLabels(c.o);
    const auto l = hmi::tabLayout(c.o, w, h, labels.size());
    const int page = hmi::shownTabPage(c.o);
    const gfx::Color body = c.color("fill", gfx::Color::rgb(0x262C36));
    const gfx::Color stroke = c.color("stroke", gfx::Color::rgb(0x3A4556));
    const gfx::Color tab = c.color("tabColor", gfx::Color::rgb(0x232A34));
    const gfx::Color active = c.color("activeColor", gfx::Color::rgb(0x2F6FD6));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xC8D0DC));
    const gfx::Color activeTxt = c.color("activeTextColor", gfx::Color::rgb(0xFFFFFF));
    const bool bottom = c.src.text(c.o, "tabPosition", "haut") == "bas";
    fillBox(c, l.content, body);
    for (std::size_t i = 0; i < l.tabs.size(); ++i) {
        const auto& b = l.tabs[i];
        const bool on = static_cast<int>(i) + 1 == page;
        if (!bottom) {
            shapes::fillPolygon(c.r, c.map(topRounded(b, 6)), on ? body : tab);
            if (on) fillBox(c, {b.x + 2, b.y, b.w - 4, 3}, active, 1);
        } else {
            fillBox(c, b, on ? body : tab, 0);
            if (on) fillBox(c, {b.x + 2, b.bottom() - 3, b.w - 4, 3}, active, 1);
        }
        text(c, fitted(c, labels[i], b.w - 12, l.fontSize), b.x, b.y, b.w, b.h, on ? activeTxt : txt, l.fontSize, "centre");
    }
    strokeBox(c, l.content, stroke);
    // Dans l'editeur : la page editee, rappelee dans un coin.
    if (c.opt.editor && labels.size() > 1)
        text(c, "page " + std::to_string(page) + "/" + std::to_string(labels.size()), l.content.x, l.content.bottom() - 20,
             l.content.w - 8, 18, withA(muted(c), 0.8f), 11, "droite");
}

// ================================================================= cadre ===
void drawFrame(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto l = hmi::frameLayout(c.o, w, h);
    const double fs = fontOf(c, 14);
    const double radius = std::clamp(c.src.number(c.o, "radius", 6), 0.0, std::min(w, h) / 2);
    const gfx::Color stroke = c.color("stroke", gfx::Color::rgb(0x4A5568));
    const float sw = std::max(1.f, c.stroke());
    const std::string title = c.src.text(c.o, "title");
    const bool band = l.band.w > 0;
    fillBox(c, {0, l.top, w, h - l.top}, c.color("fill"), radius);
    if (band) {
        shapes::fillPolygon(c.r, c.map(topRounded(l.band, radius)), c.color("titleFill", gfx::Color::rgb(0x2F3B4C)));
        strokeBox(c, {0, 0, w, h}, stroke, sw / c.vp.zoom, radius);
        text(c, fitted(c, title, l.title.w, fs), l.title.x, l.title.y, l.title.w, l.title.h,
             c.color("titleColor", gfx::Color::rgb(0xC8D0DC)), fs, c.src.text(c.o, "titleAlign", "gauche"));
        return;
    }
    // Le contour, ouvert a l'endroit du titre.
    Pts p;
    const auto P = [&](double x, double y) { p.push_back({static_cast<float>(x), static_cast<float>(y)}); };
    const double top = l.top, r = std::min(radius, (h - top) / 2);
    const auto arc = [&](double cx, double cy, double a0) {
        for (int i = 0; i <= 5; ++i) {
            const double a = a0 + 1.5707963 * i / 5.0;
            P(cx + r * std::cos(a), cy + r * std::sin(a));
        }
    };
    const bool gap = !title.empty() && l.title.w > 0;
    P(gap ? l.title.right() + 2 : r, top);
    P(w - r, top);
    arc(w - r, top + r, -1.5707963);
    P(w, h - r);
    arc(w - r, h - r, 0);
    P(r, h);
    arc(r, h - r, 1.5707963);
    P(0, top + r);
    arc(r, top + r, 3.1415926);
    P(gap ? l.title.x - 2 : r, top);
    shapes::strokePolyline(c.r, c.map(p), !gap, stroke, sw);
    if (gap)
        text(c, fitted(c, title, l.title.w, fs), l.title.x, l.title.y, l.title.w, l.title.h,
             c.color("titleColor", gfx::Color::rgb(0xC8D0DC)), fs, "centre");
}

// ======================================================== panneau defilant ===
void drawScrollPanel(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillBox(c, {0, 0, w, h}, c.color("fill", gfx::Color::rgb(0x1F252E)));
    const auto l = hmi::scrollLayout(c.view, c.o);
    const gfx::Color bar = c.color("barColor", gfx::Color::rgb(0x5B6B82));
    if (l.vbar.w > 0) {
        fillBox(c, l.vbar, fade(gfx::Color{0, 0, 0, 70}, c.alpha));
        fillBox(c, l.vthumb, bar, 3);
    }
    if (l.hbar.w > 0) {
        fillBox(c, l.hbar, fade(gfx::Color{0, 0, 0, 70}, c.alpha));
        fillBox(c, l.hthumb, bar, 3);
    }
    strokeBox(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x3A4556)), static_cast<float>(c.src.number(c.o, "strokeWidth", 1)));
    // Dans l'editeur, entre dans le panneau : le contenu entier, en pointilles.
    if (c.opt.editor && c.opt.insideGroup == c.o.id && (l.contentW > w + 1 || l.contentH > h + 1)) {
        const gfx::Color dash = c.fixed(0xB18CFF);
        const double cw = l.contentW, ch = l.contentH;
        const auto dashed = [&](double x1, double y1, double x2, double y2) {
            const double len = std::hypot(x2 - x1, y2 - y1);
            for (double t = 0; t < len; t += 10) {
                const double t1 = std::min(len, t + 6);
                line(c, x1 + (x2 - x1) * t / len, y1 + (y2 - y1) * t / len, x1 + (x2 - x1) * t1 / len, y1 + (y2 - y1) * t1 / len, dash);
            }
        };
        dashed(0, 0, cw, 0);
        dashed(cw, 0, cw, ch);
        dashed(cw, ch, 0, ch);
        dashed(0, ch, 0, 0);
        text(c, "contenu " + std::to_string(static_cast<int>(cw)) + " x " + std::to_string(static_cast<int>(ch)), 4, ch - 18, 220, 16, dash, 11,
             "gauche");
    }
}

// ======================================================= panneau repliable ===
void drawCollapsible(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double hh = std::min(h, hmi::collapsibleHeaderHeight(c.o));
    const bool collapsed = c.src.flag(c.o, "collapsed");
    const double fs = fontOf(c, 14);
    const gfx::Color stroke = c.color("stroke", gfx::Color::rgb(0x3A4556));
    const double bodyH = collapsed ? 0 : h - hh;
    if (bodyH > 0) fillBox(c, {0, hh, w, bodyH}, c.color("fill", gfx::Color::rgb(0x262C36)));
    fillBox(c, {0, 0, w, hh}, c.color("headerColor", gfx::Color::rgb(0x2F3B4C)), collapsed ? 5 : 0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    arrow(c, {4, 0, hh, hh}, collapsed ? 'd' : 'b', txt);
    text(c, fitted(c, c.src.text(c.o, "title"), w - hh - 12, fs), hh + 2, 0, w - hh - 8, hh, txt, fs, "gauche");
    strokeBox(c, {0, 0, w, collapsed ? hh : h}, stroke, 1.f, collapsed ? 5 : 0);
    // Replie dans l'editeur : la place du contenu, en pointilles.
    if (collapsed && c.opt.editor && h > hh + 2) {
        const gfx::Color dash = withA(muted(c), 0.6f);
        for (double x = 0; x < w; x += 12) line(c, x, h, std::min(w, x + 6), h, dash);
        for (double y = hh; y < h; y += 12) {
            line(c, 0, y, 0, std::min(h, y + 6), dash);
            line(c, w, y, w, std::min(h, y + 6), dash);
        }
    }
}

// ============================================================= plan a zones ===
void drawZoneMap(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillBox(c, {0, 0, w, h}, c.color("fill", gfx::Color::rgb(0x1B2028)));
    const std::string image = c.src.text(c.o, "image");
    if (!image.empty() && !drawNamedImage(c, image, "\xC3\xA9tirer", 0, 0, w, h))
        text(c, image + " (absente)", 0, 0, w, 24, muted(c), 12, "centre");
    const auto zones = hmi::parseMapZones(c.src.text(c.o, "mapZones"));
    const double fs = fontOf(c, 13);
    const auto* rt = !c.opt.editor ? c.opt.runtime : nullptr;
    const gfx::Color ok = c.color("colorOk", gfx::Color::rgb(0x2E6B45));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    const bool blink = !c.opt.editor && c.src.flag(c.o, "blinkUnacked", true) && std::fmod(c.opt.time, 1.0) >= 0.5;
    for (const auto& z : zones) {
        if (z.points.size() < 3) continue;
        Pts poly;
        for (const auto& [px, py] : z.points) poly.push_back({static_cast<float>(px * w / 100.0), static_cast<float>(py * h / 100.0)});
        int highest = 0;
        std::size_t count = 0, unacked = 0;
        if (rt)
            for (const auto& a : rt->alarms()) {
                if (!hmi::inGroup(a, z.alarmGroup())) continue;
                ++count;
                if (!a.acked) ++unacked;
                if (highest == 0 || a.priority < highest) highest = a.priority;
            }
        gfx::Color col = z.color.empty() ? ok : fade(parseColor(z.color, gfx::Color::rgb(0x2E6B45)), c.alpha);
        float fillA = 0.32f;
        if (highest > 0) {
            col = fade(c.seen(priorityColor(highest)), c.alpha);
            fillA = unacked > 0 && blink ? 0.22f : 0.5f;
        }
        shapes::fillPolygon(c.r, c.map(poly), withA(col, fillA));
        const bool chosen = rt && !rt->selectedZone().empty() && rt->selectedZone() == z.alarmGroup();
        shapes::strokePolyline(c.r, c.map(poly), true, chosen ? c.color("selectedColor", gfx::Color::rgb(0xFFFFFF)) : col,
                               (chosen ? 3.f : 1.6f) * c.vp.zoom);
        const auto [cx, cy] = hmi::zoneCenter(z, w, h);
        const auto zb = hmi::zoneBounds(z, w, h);
        if (c.src.flag(c.o, "showNames", true))
            text(c, fitted(c, z.name, std::max(40.0, zb.w - 6), fs), cx - zb.w / 2, cy - fs, zb.w, fs * 2, txt, fs, "centre");
        if (c.src.flag(c.o, "showCounts", true) && count > 0) {
            const double r = fs * 0.85;
            const hmi::Box badge{cx + std::min(zb.w / 2 - r * 2, hmi::approxTextWidth(z.name, fs) / 2 + 4), cy - fs - r, r * 2, r * 2};
            shapes::fillPolygon(c.r, c.map(shapes::ellipse({static_cast<float>(badge.cx()), static_cast<float>(badge.cy())},
                                                            static_cast<float>(r), static_cast<float>(r), 20)), col);
            text(c, std::to_string(count), badge.x, badge.y, badge.w, badge.h, c.fixedOn(0x111111), fs * 0.85,
                 "centre");
        }
    }
    if (zones.empty() && c.opt.editor)
        text(c, "Plan \xC3\xA0 zones : ses zones se r\xC3\xA8glent dans l'onglet Contenu", 0, 0, w, h, muted(c), 13, "centre");
    strokeBox(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x3A4556)));
}

// ====================================================== menu de connexion ===
//  L'objet Menu de connexion : un bouton, une silhouette, son libelle - en
//  marche, qui est connecte ("showUser" : son nom, son groupe et son niveau).
//  Un clic ouvre le menu natif (le moteur le fait, sans action).
void person(const Ctx& c, double cx, double cy, double size, gfx::Color col, bool connected) {
    const double head = size * 0.2;
    shapes::fillPolygon(c.r, c.map(shapes::ellipse({static_cast<float>(cx), static_cast<float>(cy - size * 0.2)},
                                                   static_cast<float>(head), static_cast<float>(head), 20)), col);
    Pts body;
    const int n = 12;
    for (int i = 0; i <= n; ++i) {
        const double a = 3.14159265 * (1.0 + static_cast<double>(i) / n);
        body.push_back({static_cast<float>(cx + std::cos(a) * size * 0.36), static_cast<float>(cy + size * 0.4 + std::sin(a) * size * 0.3)});
    }
    shapes::fillPolygon(c.r, c.map(body), col);
    if (connected)
        shapes::fillPolygon(c.r, c.map(shapes::ellipse({static_cast<float>(cx + size * 0.36), static_cast<float>(cy + size * 0.3)},
                                                       static_cast<float>(size * 0.13), static_cast<float>(size * 0.13), 16)),
                            c.fixed(0x2ECC71));
}

void drawLoginMenuButton(const Ctx& c) {
    const double w = c.w(), h = c.h(), rad = c.src.number(c.o, "radius", 4);
    const auto* rt = !c.opt.editor ? c.opt.runtime : nullptr;
    const bool down = rt && rt->pressed() == c.o.id;
    const gfx::Color base = c.color("fill", gfx::Color::rgb(0x2B4C6F));
    const gfx::Color fill = down ? withA(gfx::Color{static_cast<std::uint8_t>(base.r * 3 / 4), static_cast<std::uint8_t>(base.g * 3 / 4),
                                                    static_cast<std::uint8_t>(base.b * 3 / 4), base.a}, 1.f)
                                 : base;
    fillBox(c, {0, 0, w, h}, fill, rad);
    if (!down) fillBox(c, {2, 2, w - 4, h * 0.45}, fade(gfx::Color{255, 255, 255, 18}, c.alpha), rad);
    strokeBox(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x4F7AA8)), static_cast<float>(std::max(1.0, c.src.number(c.o, "strokeWidth", 1))), rad);
    const gfx::Color tc = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    const double dy = down ? 1.5 : 0;
    const hmi::User* u = rt ? rt->user() : nullptr;
    double tx = 0, tw = w;
    if (c.src.flag(c.o, "icon", true) && w > h * 1.2) {
        const double g = std::min(h * 0.66, 32.0);
        person(c, 10 + g / 2, h / 2 + dy, g, tc, u != nullptr);
        tx = g + 16;
        tw = std::max(4.0, w - tx - 6);
    }
    const double fs = fontOf(c, 16);
    std::string label = c.src.text(c.o, "text");
    std::string sub;
    if (u && c.src.flag(c.o, "showUser", true)) {
        label = u->fullName.empty() ? u->login : u->fullName;
        const auto* g = c.opt.project ? c.opt.project->group(u->group) : nullptr;
        sub = (g ? g->name + ", " : std::string{}) + "niveau " + std::to_string(rt->level());
    }
    const std::string align = c.src.text(c.o, "align", "centre");
    if (sub.empty() || h < fs * 2.2) {
        text(c, fitted(c, label, tw, fs), tx, dy, tw, h, tc, fs, align);
        return;
    }
    text(c, fitted(c, label, tw, fs), tx, dy + h * 0.08, tw, h * 0.5, tc, fs, align);
    text(c, fitted(c, sub, tw, fs * 0.75), tx, dy + h * 0.5, tw, h * 0.4, withA(tc, 0.72f), fs * 0.75, align);
}

} // namespace

bool drawLot12(const Ctx& c) {
    switch (c.o.kind) {
        case Kind::NavBar:           drawNavBar(c); return true;
        case Kind::Breadcrumb:       drawBreadcrumb(c); return true;
        case Kind::TabContainer:     drawTabContainer(c); return true;
        case Kind::Frame:            drawFrame(c); return true;
        case Kind::ScrollPanel:      drawScrollPanel(c); return true;
        case Kind::CollapsiblePanel: drawCollapsible(c); return true;
        case Kind::ZoneMap:          drawZoneMap(c); return true;
        case Kind::LoginMenuButton:  drawLoginMenuButton(c); return true;
        default:                     return false;
    }
}

} // namespace app::paint
