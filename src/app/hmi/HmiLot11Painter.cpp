// =============================================================================
//  app/hmi/HmiLot11Painter.cpp - le dessin des objets des alarmes et de la
//                                production (lot 11)
// -----------------------------------------------------------------------------
//  Bandeau d'alarme, compteur d'alarmes, resume par zone, consigne d'alarme,
//  statistiques d'alarmes ; compteurs de production (TRS), tableau de
//  variables, editeur de recette, bouton d'export.
//
//  EN MARCHE, ils lisent le moteur (les alarmes en cours, les mises de cote,
//  l'alarme choisie, les compteurs du poste, la saisie en cours) ; DANS
//  L'EDITEUR, un exemple plausible (une alarme, trois zones, des chiffres de
//  poste) montre la mise en page et les couleurs.
//
//  La geometrie est celle du clic (HmiAlarmViews, HmiProduction).
// =============================================================================
#include "HmiPaintKit.hpp"
#include "HmiIcons.hpp"

#include "../../hmi/HmiAlarmViews.hpp"
#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiCharts.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiProduction.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiWidgets.hpp"

#include <cmath>
#include <cstdio>

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
std::string hms(const std::string& stamp) { return stamp.size() >= 19 ? stamp.substr(11, 8) : stamp; }
double fontOf(const Ctx& c, double fallback) { return std::clamp(c.src.number(c.o, "fontSize", fallback), 7.0, 60.0); }
gfx::Color muted(const Ctx& c) { return c.fixed(0x8A96A8); }
bool blinkPhase(const Ctx& c) { return !c.opt.editor && std::fmod(c.opt.time, 1.0) >= 0.5; }

std::string shortDuration(double s) {
    if (s < 0) return "-";
    const auto t = static_cast<long long>(std::llround(s));
    char buf[32];
    if (t >= 3600) std::snprintf(buf, sizeof buf, "%lld h %02lld", t / 3600, (t / 60) % 60);
    else if (t >= 60) std::snprintf(buf, sizeof buf, "%lld min %02lld", t / 60, t % 60);
    else std::snprintf(buf, sizeof buf, "%lld s", t);
    return buf;
}

// Un exemple d'alarme pour l'editeur.
hmi::LiveAlarm sampleAlarm() {
    hmi::LiveAlarm a;
    a.name = "Bouteille_Vide_A";
    a.message = "Bouteille A vide (18,2 bar)";
    a.group = "Armoire A";
    a.priority = 2;
    a.appeared = "2026-09-24 14:05:12.350";
    a.instruction = "Changer la bouteille A (poste de gaz, rang 2).\\nV\xC3\xA9rifier le d\xC3\xA9tendeur avant de rouvrir la vanne.";
    return a;
}

// ============================================================ bandeau =======
void drawBanner(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto l = hmi::bannerLayout(c.o, w, h);
    const double fs = l.fontSize;
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    hmi::LiveAlarm sample = sampleAlarm();
    const hmi::LiveAlarm* a = nullptr;
    std::size_t count = 0;
    if (live) {
        const int k = rt->bannerAlarm(c.o);
        if (k >= 0) a = &rt->alarms()[static_cast<std::size_t>(k)];
        count = hmi::bannerCount(rt->alarms(), c.src.text(c.o, "group"));
    } else {
        a = &sample;
        count = 3;
    }
    const gfx::Color base = c.color("fill", gfx::Color::rgb(0x1F252E));
    fillBox(c, {0, 0, w, h}, base, 3);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    if (!a) {
        fillBox(c, l.stripe, c.fixed(0x2E6B45));
        text(c, fitted(c, c.src.text(c.o, "empty", "Aucune alarme en cours"), w - l.stripe.w - 20, fs), l.stripe.w + 10, 0, w - l.stripe.w - 20, h,
             muted(c), fs, "gauche");
        strokeBox(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x3A4556)), 1.f, 3);
        return;
    }
    const gfx::Color pc = fade(c.seen(alarmColor(live ? rt : nullptr, *a)), c.alpha);   // 1.10.2 : la couleur du groupe
    const bool flash = !a->acked && c.src.flag(c.o, "blinkUnacked", true) && blinkPhase(c);
    fillBox(c, {0, 0, w, h}, withA(pc, flash ? 0.42f : 0.2f), 3);
    fillBox(c, l.stripe, pc);
    text(c, hms(a->appeared), l.time.x, l.time.y, l.time.w, l.time.h, txt, fs * 0.92, "gauche");
    std::string msg = a->message;
    if (!a->acked) msg += "  \xE2\x80\x94 \xC3\xA0 acquitter";
    else if (!a->active) msg += "  \xE2\x80\x94 disparue";
    text(c, fitted(c, msg, l.message.w, fs), l.message.x, l.message.y, l.message.w, l.message.h, txt, fs, "gauche");
    if (l.count.w > 0 && count > 1)
        drawButtonBox(c, l.count, "+" + std::to_string(count - 1), fade(gfx::Color{255, 255, 255, 28}, c.alpha), txt, fs * 0.9);
    if (l.ack.w > 0)
        drawButtonBox(c, l.ack, "Acquitter", a->acked ? c.fixed(0x3A4556) : c.fixed(0x2F6FD6),
                      a->acked ? muted(c) : c.fixedOn(0xFFFFFF), fs * 0.9);
    strokeBox(c, {0, 0, w, h}, pc, 1.2f, 3);
}

// ============================================================ compteur ======
void drawCounter(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    const std::string group = c.src.text(c.o, "group"), what = c.src.text(c.o, "count", "\xC3\xA0 acquitter");
    std::size_t n = 3;
    int prio = 2;
    bool unacked = true;
    const hmi::LiveAlarm* top = nullptr;   // 1.11 (R111) : l'alarme la plus forte, sa couleur (celle de son groupe)
    if (live) {
        n = hmi::alarmCount(rt->alarms(), rt->shelvedAlarms(), group, what);
        prio = hmi::strongestPriority(rt->alarms(), group, what.find("acquitter") != std::string::npos);
        top = hmi::strongestAlarm(rt->alarms(), group, what.find("acquitter") != std::string::npos);
        unacked = hmi::alarmCount(rt->alarms(), rt->shelvedAlarms(), group, "\xC3\xA0 acquitter") > 0;
    }
    const bool shelvedMode = what.find("mise") != std::string::npos;
    gfx::Color bg = c.color("fill", gfx::Color::rgb(0x2A313C));
    if (n > 0 && prio > 0 && !shelvedMode) bg = fade(c.seen(top ? alarmColor(rt, *top) : priorityColor(prio)), c.alpha);
    if (n > 0 && shelvedMode) bg = c.fixed(0x6B5B95);
    const bool flash = n > 0 && unacked && !shelvedMode && c.src.flag(c.o, "blinkUnacked", true) && blinkPhase(c);
    if (flash) bg = withA(bg, 0.55f);
    fillBox(c, {0, 0, w, h}, bg, std::min(w, h) * 0.18);
    strokeBox(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x3A4556)), 1.f, std::min(w, h) * 0.18);
    const double fs = fontOf(c, 22);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    double x = 6;
    if (c.src.flag(c.o, "icon", true)) {
        const double s = std::min(h * 0.62, w * 0.3);
        const auto a = c.map(x, (h - s) / 2), b = c.map(x + s, (h + s) / 2);
        drawHmiGlyph(c.r, HmiGlyph::Bell, {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y)}, txt);
        x += s + 6;
    }
    const std::string label = c.src.text(c.o, "label", "Alarmes");
    const double nw = w - x - 6;
    if (!label.empty() && h >= fs * 1.9) {
        text(c, std::to_string(n), x, 2, nw, h * 0.62, txt, fs, "centre");
        text(c, fitted(c, label, nw, fs * 0.45), x, h * 0.58, nw, h * 0.36, withA(txt, 0.85f), std::max(8.0, fs * 0.45), "centre");
    } else {
        text(c, std::to_string(n) + (label.empty() ? "" : " " + label), x, 0, nw, h, txt, fs * 0.8, "centre");
    }
}

// ============================================================ resume ========
void drawSummary(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 3));
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    std::vector<hmi::ZoneSummary> zones;
    if (live && c.opt.project) {
        zones = hmi::zoneSummaries(*c.opt.project, rt->alarms(), rt->shelvedAlarms(), c.src.text(c.o, "groups"));
    } else {
        std::vector<std::string> names = c.opt.project ? hmi::zonesOf(*c.opt.project, c.src.text(c.o, "groups")) : std::vector<std::string>{};
        if (names.empty()) names = {"Zone A", "Zone B", "Zone C"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            hmi::ZoneSummary z;
            z.name = names[i];
            if (i % 3 == 1) { z.current = 2; z.active = 2; z.unacked = 1; z.highest = 2; }
            if (i % 3 == 2) { z.current = 1; z.active = 0; z.unacked = 0; z.highest = 3; }
            zones.push_back(z);
        }
    }
    const double fs = fontOf(c, 14);
    if (zones.empty()) {
        text(c, "Aucune zone : les groupes des alarmes (Configuration > Alarmes) ou la propri\xC3\xA9t\xC3\xA9 Zones", 8, 0, w - 16, h, muted(c), fs * 0.9,
             "centre", true);
        return;
    }
    const auto tiles = hmi::summaryTiles(c.o, w, h, zones.size());
    const gfx::Color ok = c.color("colorOk", gfx::Color::rgb(0x2E6B45));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    const std::string chosen = live ? rt->selectedZone() : std::string{};
    for (std::size_t i = 0; i < zones.size() && i < tiles.size(); ++i) {
        const auto& z = zones[i];
        const auto& t = tiles[i];
        gfx::Color col = z.highest > 0 ? fade(c.seen(priorityColor(z.highest)), c.alpha) : ok;
        // 1.11 (R111) : la couleur du groupe de l'alarme la plus forte de la zone, s'il en a une.
        if (live && c.opt.project && z.highest > 0)
            if (const auto* top = hmi::strongestZoneAlarm(*c.opt.project, rt->alarms(), z.name)) col = fade(c.seen(alarmColor(rt, *top)), c.alpha);
        if (z.current > 0 && z.active == 0 && z.unacked == 0) col = withA(col, 0.55f);
        const bool flash = z.unacked > 0 && c.src.flag(c.o, "blinkUnacked", true) && blinkPhase(c);
        fillBox(c, t, flash ? withA(col, 0.5f) : col, 5);
        if (z.name == chosen) strokeBox(c, t, c.fixed(0xFFFFFF), 2.5f, 5);
        const double nameH = std::min(t.h * 0.45, fs * 1.8);
        text(c, fitted(c, z.name, t.w - 12, fs), t.x + 6, t.y + 4, t.w - 12, nameH, txt, fs, "gauche");
        std::string line;
        if (z.current == 0) line = "aucune alarme";
        else {
            line = std::to_string(z.current) + " en cours";
            if (z.unacked) line += " \xC2\xB7 " + std::to_string(z.unacked) + " \xC3\xA0 acquitter";
        }
        if (z.shelved) line += " \xC2\xB7 " + std::to_string(z.shelved) + " de c\xC3\xB4t\xC3\xA9";
        text(c, fitted(c, line, t.w - 12, fs * 0.78), t.x + 6, t.y + nameH, t.w - 12, std::max(0.0, t.h - nameH - 4), withA(txt, 0.9f), fs * 0.78,
             "gauche", true);
    }
}

// ============================================================ consigne ======
void drawInstruction(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 4));
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    const double fs = fontOf(c, 14);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    hmi::LiveAlarm sample = sampleAlarm();
    const hmi::LiveAlarm* a = live ? hmi::instructionAlarm(rt->alarms(), c.src.text(c.o, "alarm"), rt->selectedAlarm()) : &sample;
    const double head = fs * 2.2;
    if (!a) {
        text(c, c.src.text(c.o, "empty", "Aucune alarme choisie"), 12, 0, w - 24, h, muted(c), fs, "centre", true);
        return;
    }
    const gfx::Color pc = fade(c.seen(alarmColor(live ? rt : nullptr, *a)), c.alpha);   // 1.10.2 : la couleur du groupe
    fillBox(c, {0, 0, w, head}, withA(pc, 0.3f), 4);
    fillBox(c, {0, 0, 6, head}, pc);
    const std::string title = a->name + "  \xC2\xB7  " + std::string(hmi::alarmPriorityLabel(a->priority)) + "  \xC2\xB7  " + hms(a->appeared);
    text(c, fitted(c, title, w - 24, fs), 14, 0, w - 24, head, txt, fs, "gauche");
    double y = head + 4;
    if (c.src.flag(c.o, "showMessage", true)) {
        text(c, fitted(c, a->message, w - 24, fs * 0.9), 12, y, w - 24, fs * 1.6, muted(c), fs * 0.9, "gauche");
        y += fs * 1.7;
    }
    fillBox(c, {12, y, w - 24, 1}, c.fixed(0x3A4556));
    y += 6;
    const std::string body = a->instruction.empty() ? std::string("Pas de consigne pour cette alarme (Configuration > Alarmes : Consigne).")
                                                    : a->instruction;
    text(c, "Consigne", 12, y, w - 24, fs * 1.5, c.fixedText(0x7FB8FF), fs * 0.85, "gauche");
    y += fs * 1.5;
    // Un paragraphe, en haut de sa boite : on le decoupe ligne a ligne.
    const std::string wrapped = wrapText(c, hmi::unescapeText(body), gfx::FontId{static_cast<std::uint16_t>(std::clamp(fs * c.vp.zoom, 6.0, 200.0))},
                                         static_cast<float>(std::max(1.0, (w - 32) * c.vp.zoom)));
    double ly = y;
    std::size_t from = 0;
    while (from <= wrapped.size() && ly + fs * 1.35 <= h - 4) {
        const auto nl = wrapped.find('\n', from);
        const std::string line = wrapped.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        text(c, line, 14, ly, w - 28, fs * 1.35, a->instruction.empty() ? muted(c) : txt, fs, "gauche");
        ly += fs * 1.35;
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
}

// ============================================================ statistiques ==
void drawStats(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 3));
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    const double fs = fontOf(c, 13);
    const std::string range = c.src.text(c.o, "range", "depuis le lancement");
    std::vector<hmi::AlarmStatRow> rows;
    if (live) {
        rows = hmi::alarmStatistics(rt->closedAlarms(), c.opt.history ? &c.opt.history->alarms : nullptr, rt->alarms(), range,
                                    c.src.text(c.o, "group"), c.src.text(c.o, "sort", "nombre"),
                                    static_cast<std::size_t>(std::max(0.0, c.src.number(c.o, "top", 5))), rt->dateStampOf(rt->now()));
    } else {
        rows = {{"Bouteille_Vide_A", "Armoire A", 2, 7, 1860, false}, {"Pression_Basse_B", "Armoire B", 3, 4, 620, true},
                {"Porte_Ouverte", "Local", 4, 3, 95, false}, {"Defaut_Detendeur", "Armoire A", 1, 1, 310, false}};
        rows.resize(std::min<std::size_t>(rows.size(), static_cast<std::size_t>(std::max(1.0, c.src.number(c.o, "top", 5)))));
    }
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const double head = fs * 2;
    shapes::fillPolygon(c.r, c.map(rectLocal(w, head)), c.fixed(0x323A47));
    const bool byDuration = c.src.text(c.o, "sort", "nombre").find("dur") != std::string::npos;
    text(c, fitted(c, std::string("Alarmes les plus ") + (byDuration ? "longues" : "fr\xC3\xA9quentes") + " \xE2\x80\x94 " + range, w - 16, fs), 8, 0,
         w - 16, head, c.fixed(0xFFFFFF), fs, "gauche");
    if (rows.empty()) {
        text(c, "aucune alarme sur la p\xC3\xA9riode", 0, head, w, h - head, muted(c), fs, "centre");
        return;
    }
    const auto colors = hmi::splitSemicolons(c.src.text(c.o, "colors", "#F2994A"));
    const gfx::Color bar = fade(parseColor(colors.empty() ? std::string("#F2994A") : colors.front(), gfx::Color::rgb(0xF2994A)), c.alpha);
    double maxv = 0;
    for (const auto& r : rows) maxv = std::max(maxv, byDuration ? r.seconds : static_cast<double>(r.count));
    const double rowH = std::min(fs * 2.1, (h - head - 6) / static_cast<double>(rows.size()));
    const double nameW = w * 0.36, valueW = w * 0.2, barX = 10 + nameW, barW = std::max(10.0, w - barX - valueW - 10);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const double y = head + 4 + rowH * static_cast<double>(i);
        if (y + rowH > h) break;
        fillBox(c, {4, y + rowH * 0.2, 4, rowH * 0.6}, fade(c.seen(priorityColor(r.priority)), c.alpha));
        text(c, fitted(c, std::to_string(i + 1) + ". " + r.name, nameW - 6, fs), 12, y, nameW - 6, rowH, txt, fs, "gauche");
        const double v = byDuration ? r.seconds : static_cast<double>(r.count);
        const double f = maxv > 0 ? v / maxv : 0;
        fillBox(c, {barX, y + rowH * 0.22, barW, rowH * 0.56}, fade(gfx::Color{255, 255, 255, 14}, c.alpha), 2);
        fillBox(c, {barX, y + rowH * 0.22, barW * f, rowH * 0.56}, r.current ? withA(bar, 1.f) : withA(bar, 0.8f), 2);
        const std::string value = std::to_string(r.count) + (r.count > 1 ? " fois" : " fois") + " \xC2\xB7 " + shortDuration(r.seconds);
        text(c, fitted(c, value, valueW, fs * 0.9), barX + barW + 6, y, valueW, rowH, txt, fs * 0.9, "gauche");
    }
}

// ============================================================ production ====
void drawProduction(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 4));
    const auto l = hmi::productionLayout(c.o, w, h);
    const double fs = fontOf(c, 14);
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    hmi::ProductionFigures f;
    if (live) f = rt->production(c.o.id);
    else {
        f = hmi::productionFigures(1180, 36, 6.4 * 3600, 7 * 3600, std::max(1.0, c.src.number(c.o, "idealRate", 600)) * 0.3, 176, c.src.number(c.o, "target", 0));
        f.shift = "Poste de 14:00 (aper\xC3\xA7u)";
        f.running = true;
    }
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const std::string title = c.src.text(c.o, "text");
    text(c, fitted(c, (title.empty() ? std::string{} : title + "  \xC2\xB7  ") + f.shift + (f.running ? "" : "  \xC2\xB7  \xC3\xA0 l'arr\xC3\xAAt"), l.title.w, fs),
         l.title.x, l.title.y, l.title.w, l.title.h, txt, fs, "gauche");
    if (l.reset.w > 0) drawButtonBox(c, l.reset, "RAZ", c.fixed(0x3A4556), txt, fs * 0.85);
    // Les quatre tuiles.
    double lowTrs = 60, highTrs = 85;
    (void)hmi::parseNumber(c.src.text(c.o, "low", "60"), lowTrs);
    (void)hmi::parseNumber(c.src.text(c.o, "high", "85"), highTrs);
    const gfx::Color trsCol = fade(c.seen(f.oee * 100 < lowTrs ? gfx::Color::rgb(0xE5534B) : f.oee * 100 < highTrs ? gfx::Color::rgb(0xF2994A)
                                                                                                              : gfx::Color::rgb(0x2ECC71)),
                                   c.alpha);
    const struct { const char* label; std::string value; gfx::Color col; } tiles[4] = {
        {"Bonnes", hmi::formatNumber(std::round(f.good)), c.fixed(0x2ECC71)},
        {"Rebuts", hmi::formatNumber(std::round(f.bad)), c.fixed(0xE5534B)},
        {"Cadence (p/h)", hmi::formatNumber(std::round(f.rate)), c.fixed(0x4FA3FF)},
        {"TRS", hmi::formatNumber(std::round(f.oee * 1000) / 10) + " %", trsCol}};
    for (int k = 0; k < 4; ++k) {
        const auto& t = l.tiles[k];
        fillBox(c, t, fade(gfx::Color{255, 255, 255, 12}, c.alpha), 5);
        fillBox(c, {t.x, t.y, 4, t.h}, tiles[k].col);
        const double vfs = std::clamp(t.h * 0.42, 10.0, fs * 2.4);
        text(c, fitted(c, tiles[k].value, t.w - 12, vfs), t.x + 8, t.y + 2, t.w - 12, t.h * 0.62, k == 3 ? trsCol : txt, vfs, "gauche");
        text(c, tiles[k].label, t.x + 8, t.y + t.h * 0.58, t.w - 12, t.h * 0.38, muted(c), fs * 0.8, "gauche");
    }
    // Disponibilite, performance, qualite (et l'objectif).
    const struct { const char* label; double value; } bars[3] = {
        {"Disponibilit\xC3\xA9", f.availability}, {"Performance", f.performance}, {"Qualit\xC3\xA9", f.quality}};
    for (int k = 0; k < 3; ++k) {
        const auto& b = l.bars[k];
        if (b.h <= 1) continue;
        text(c, bars[k].label, 8, b.y - b.h * 0.4, b.x - 12, b.h * 1.8, muted(c), fs * 0.85, "gauche");
        fillBox(c, b, fade(gfx::Color{255, 255, 255, 18}, c.alpha), 3);
        fillBox(c, {b.x, b.y, b.w * std::clamp(bars[k].value, 0.0, 1.0), b.h}, c.fixed(0x4FA3FF), 3);
        text(c, hmi::formatNumber(std::round(bars[k].value * 1000) / 10) + " %", b.right() + 6, b.y - b.h * 0.4, w - b.right() - 12, b.h * 1.8, txt,
             fs * 0.85, "gauche");
    }
    if (l.progress.w > 0 && f.target > 0) {
        const auto& b = l.progress;
        text(c, "Objectif", 8, b.y - b.h * 0.4, b.x - 12, b.h * 1.8, muted(c), fs * 0.85, "gauche");
        fillBox(c, b, fade(gfx::Color{255, 255, 255, 18}, c.alpha), 3);
        const double frac = std::clamp(f.good / f.target, 0.0, 1.0);
        fillBox(c, {b.x, b.y, b.w * frac, b.h}, c.fixed(0x2ECC71), 3);
        text(c, hmi::formatNumber(std::round(f.good)) + " / " + hmi::formatNumber(f.target), b.right() + 6, b.y - b.h * 0.4, w - b.right() - 12,
             b.h * 1.8, txt, fs * 0.85, "gauche");
    }
}

// ============================================================ tableau ========
void drawVariableTable(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 3));
    const auto g = hmi::variableTableLayout(c.o, w, h);
    const double fs = fontOf(c, 13);
    const auto rows = hmi::variableRows(c.o);
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    const auto values = live ? hmi::splitLiveValues(c.src.text(c.o, "liveValues")) : std::vector<std::string>{};
    const hmi::FormState* form = live ? rt->formState(c.o.id) : nullptr;
    const bool focusedObj = live && rt->focusedObject() == c.o.id && form;
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA)), head = c.fixed(0xFFFFFF);
    const gfx::Color grid = c.fixed(0x4A5566);
    shapes::fillPolygon(c.r, c.map(rectLocal(w, g.headerH)), c.fixed(0x323A47));
    const char* headers[3] = {"Variable", "Valeur", "Unit\xC3\xA9"};
    for (std::size_t k = 0; k < 3; ++k) {
        text(c, headers[k], g.xs[k] + 4, 0, g.xs[k + 1] - g.xs[k] - 8, g.headerH, head, fs, k == 1 ? "droite" : "gauche");
        if (k > 0) c.r.line(c.map(g.xs[k], 0), c.map(g.xs[k], h), grid, 1);
    }
    const bool writable = c.src.flag(c.o, "writable", true);
    const std::string format = c.src.text(c.o, "format", "0.00");
    for (std::size_t i = 0; i < rows.size() && i < g.visible; ++i) {
        const auto cellName = g.cell(i, 0), cellValue = g.cell(i, 1), cellUnit = g.cell(i, 2);
        if (i % 2 == 1) fillBox(c, {0, cellName.y, w, g.rowH}, fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, cellName.bottom()), c.map(w, cellName.bottom()), c.fixed(0x3A4556), 1);
        const auto& r = rows[i];
        text(c, fitted(c, r.name.empty() ? r.expression : r.name, cellName.w - 10, fs), cellName.x + 4, cellName.y, cellName.w - 8, cellName.h, txt, fs,
             "gauche");
        text(c, fitted(c, r.unit, cellUnit.w - 10, fs), cellUnit.x + 4, cellUnit.y, cellUnit.w - 8, cellUnit.h, c.fixedText(0x9AA6B8),
             fs, "gauche");
        const std::string field = "ligne:" + std::to_string(i);
        if (focusedObj && form->focus == field) {
            const auto it = form->text.find(field);
            const std::string shown = it == form->text.end() ? std::string{} : it->second;
            const auto ct = form->caret.find(field);
            const std::string before = shown.substr(0, std::min(shown.size(), ct == form->caret.end() ? shown.size() : ct->second));
            drawField(c, {cellValue.x + 2, cellValue.y + 2, cellValue.w - 4, cellValue.h - 4}, shown, &before, fs, "droite", txt);
            continue;
        }
        std::string shown;
        if (live) {
            shown = i < values.size() ? values[i] : std::string("###");
            double x = 0;
            if (shown != "TRUE" && shown != "FALSE" && hmi::parseNumber(shown, x) && shown.find('.') != std::string::npos)
                shown = hmi::formatValue(sim::Value::real(x), hmi::rowFormat(nullptr, c.o, i));   // lot 13 : le format de la ligne
        } else {
            shown = "{" + r.expression + "}";
        }
        const bool editable = writable && hmi::isVariablePath(r.expression);
        text(c, fitted(c, shown, cellValue.w - 10, fs), cellValue.x + 4, cellValue.y, cellValue.w - 8, cellValue.h,
             shown == "###" ? c.fixed(0xE5534B) : editable ? c.fixed(0x7FB8FF) : txt, fs, "droite");
    }
    if (rows.empty()) text(c, "Tableau de variables : aucune variable (Variables)", 0, g.headerH, w, h - g.headerH, muted(c), fs, "centre");
    if (form && form->error && live && rt->now() - form->messageAt <= 8.0)
        drawFormMessage(c, {4, h - fs * 1.6, w - 8, fs * 1.5}, form, fs);
}

// ============================================================ editeur recette
void drawRecipeEditor(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, 3));
    const auto l = hmi::recipeEditorLayout(c.o, w, h);
    const double fs = fontOf(c, 13);
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    const hmi::Recipe* recipe = c.opt.project ? c.opt.project->recipeByName(c.src.text(c.o, "recipe")) : nullptr;
    const hmi::RecipeEditorState* st = live ? rt->recipeEditor(c.o.id) : nullptr;
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color accent = c.fixed(0x2F6FD6), dim = c.fixed(0x3A4556);
    // La barre : < jeu >, puis les boutons.
    std::string recordName = "(aucun jeu)";
    const hmi::RecipeRecord* rec = nullptr;
    if (recipe) {
        if (st && st->record != hmi::kNoId) rec = recipe->record(st->record);
        else if (!recipe->records.empty() && !st) rec = &recipe->records.front();
        if (rec) recordName = rec->name;
    }
    const bool dirty = st && st->dirty();
    drawButtonBox(c, l.prev, "<", dim, txt, fs);
    drawButtonBox(c, l.next, ">", dim, txt, fs);
    fillBox(c, l.name, c.fixed(0x141820), 3);
    text(c, fitted(c, recordName + (dirty ? " *" : ""), l.name.w - 10, fs), l.name.x + 5, l.name.y, l.name.w - 10, l.name.h, dirty ? c.fixedText(0xF2C94C) : txt, fs,
         "gauche");
    for (const auto& b : l.buttons) {
        const bool strong = (b.label == "Enregistrer" && dirty) || b.label == "Appliquer";
        drawButtonBox(c, b.box, b.label, strong ? accent : dim, strong ? c.fixedOn(0xFFFFFF) : c.fixedText(0xFFFFFF), fs * 0.92);
    }
    const auto& g = l.grid;
    const gfx::Color grid = c.fixed(0x4A5566), head = c.fixed(0xFFFFFF);
    fillBox(c, {0, g.table.y, w, g.headerH}, c.fixed(0x323A47));
    std::vector<std::string> headers = l.compare ? std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Jeu", "Installation", "\xC3\x89" "cart", "Unit\xC3\xA9"}
                                                 : std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Jeu", "Unit\xC3\xA9"};
    for (std::size_t k = 0; k < headers.size() && k + 1 < g.xs.size(); ++k) {
        text(c, headers[k], g.xs[k] + 4, g.table.y, g.xs[k + 1] - g.xs[k] - 8, g.headerH, head, fs, "gauche");
        if (k > 0) c.r.line(c.map(g.xs[k], g.table.y), c.map(g.xs[k], h), grid, 1);
    }
    if (!recipe) {
        text(c, "\xC3\x89" "diteur de recette : choisir la recette (propri\xC3\xA9t\xC3\xA9 Recette)", 0, g.table.y + g.headerH, w, h - g.table.y - g.headerH,
             muted(c), fs, "centre");
        return;
    }
    const hmi::FormState* form = live ? rt->formState(c.o.id) : nullptr;
    const bool focusedObj = live && rt->focusedObject() == c.o.id && form;
    for (std::size_t i = 0; i < recipe->fields.size() && i < g.visible; ++i) {
        const auto& fd = recipe->fields[i];
        const auto rowBox = g.cell(i, 0);
        if (i % 2 == 1) fillBox(c, {0, rowBox.y, w, g.rowH}, fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, rowBox.bottom()), c.map(w, rowBox.bottom()), c.fixed(0x3A4556), 1);
        text(c, fitted(c, fd.name, rowBox.w - 10, fs), rowBox.x + 4, rowBox.y, rowBox.w - 8, rowBox.h, txt, fs, "gauche");
        std::string value = st && i < st->values.size() ? st->values[i] : rec && i < rec->values.size() ? rec->values[i] : std::string{};
        const bool edited = st && i < st->edited.size() && st->edited[i];
        const auto valueBox = g.cell(i, 1);
        const std::string field = "ligne:" + std::to_string(i);
        if (focusedObj && form->focus == field) {
            const auto it = form->text.find(field);
            const std::string shown = it == form->text.end() ? std::string{} : it->second;
            const auto ct = form->caret.find(field);
            const std::string before = shown.substr(0, std::min(shown.size(), ct == form->caret.end() ? shown.size() : ct->second));
            drawField(c, {valueBox.x + 2, valueBox.y + 2, valueBox.w - 4, valueBox.h - 4}, shown, &before, fs, "gauche", txt);
        } else {
            if (edited) fillBox(c, {valueBox.x + 1, valueBox.y + 1, valueBox.w - 2, valueBox.h - 2}, fade(gfx::Color{242, 201, 76, 40}, c.alpha));
            text(c, fitted(c, hmi::unescapeText(hmi::recipeValueShown(value)), valueBox.w - 10, fs), valueBox.x + 4, valueBox.y, valueBox.w - 8, valueBox.h,
                 edited ? c.fixed(0xF2C94C) : c.fixed(0x7FB8FF), fs, "gauche");
        }
        if (l.compare) {
            const std::string installed = st && i < st->installed.size() ? st->installed[i] : std::string(live ? "?" : "-");
            const auto instBox = g.cell(i, 2), gapBox = g.cell(i, 3);
            text(c, fitted(c, installed, instBox.w - 10, fs), instBox.x + 4, instBox.y, instBox.w - 8, instBox.h, txt, fs, "gauche");
            // L'ecart : la difference des nombres, sinon = ou different.
            std::string gap = "-";
            gfx::Color gapCol = muted(c);
            double a = 0, b = 0;
            std::string va = value;
            if (va.size() >= 2 && va.front() == '\'' && va.back() == '\'') va = va.substr(1, va.size() - 2);
            if (live && !installed.empty() && installed != "?") {
                if (hmi::parseNumber(va, a) && hmi::parseNumber(installed, b)) {
                    const double d = b - a;
                    gap = std::fabs(d) < 1e-9 ? std::string("=") : (d > 0 ? "+" : "") + hmi::formatNumber(std::round(d * 1000) / 1000);
                    gapCol = std::fabs(d) < 1e-9 ? c.fixedText(0x4CD08A) : c.fixedText(0xF2994A);
                } else {
                    const bool same = va == installed;
                    gap = same ? "=" : "\xE2\x89\xA0";
                    gapCol = same ? c.fixedText(0x4CD08A) : c.fixedText(0xF2994A);
                }
            }
            text(c, gap, gapBox.x + 4, gapBox.y, gapBox.w - 8, gapBox.h, gapCol, fs, "gauche");
        }
        const auto unitBox = g.cell(i, l.compare ? 4 : 2);
        text(c, fitted(c, fd.unit, unitBox.w - 10, fs), unitBox.x + 4, unitBox.y, unitBox.w - 8, unitBox.h, muted(c), fs, "gauche");
    }
    // Le message de l'editeur (enregistre, refuse...) ou de la saisie.
    if (live) {
        std::string msg;
        bool error = false;
        if (form && !form->message.empty() && rt->now() - form->messageAt <= 8.0) { msg = form->message; error = form->error; }
        else if (st && !st->message.empty() && rt->now() - st->messageAt <= 8.0) { msg = st->message; error = st->error; }
        if (!msg.empty())
            text(c, fitted(c, msg, w - 16, fs * 0.9), 8, h - fs * 1.7, w - 16, fs * 1.6,
                 (error ? c.fixed(0xFF6B6B) : c.fixed(0x4CD08A)), fs * 0.9, "gauche");
    }
}

// ============================================================ export =========
void drawExportButton(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 4)));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 16), 6.0, 120.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xFFFFFF));
    std::string label = c.src.text(c.o, "text");
    const auto format = hmi::exportFormatFrom(c.src.text(c.o, "fileFormat", "CSV")).value_or(hmi::ExportFormat::Csv);
    double x = 0;
    if (c.src.flag(c.o, "icon", true)) {
        const double s = std::min(h * 0.56, 26.0);
        const auto a = c.map(10, (h - s) / 2), b = c.map(10 + s, (h + s) / 2);
        drawHmiGlyph(c.r, HmiGlyph::Export, {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y)}, txt);
        x = 10 + s;
    }
    const std::string badge = std::string(hmi::exportFormatLabel(format));
    const double bw = std::min(w * 0.3, fs * 0.62 * static_cast<double>(badge.size()) + 12);
    text(c, fitted(c, label, w - x - bw - 12, fs), x, 0, w - x - bw - 6, h, txt, fs, "centre", c.src.flag(c.o, "wrap"));
    fillBox(c, {w - bw - 6, h * 0.26, bw, h * 0.48}, fade(gfx::Color{0, 0, 0, 70}, c.alpha), 3);
    text(c, badge, w - bw - 6, h * 0.26, bw, h * 0.48, txt, fs * 0.7, "centre");
}

} // namespace

bool drawLot11(const Ctx& c) {
    switch (c.o.kind) {
        case Kind::AlarmBanner:       drawBanner(c); return true;
        case Kind::AlarmCounter:      drawCounter(c); return true;
        case Kind::AlarmSummary:      drawSummary(c); return true;
        case Kind::AlarmInstruction:  drawInstruction(c); return true;
        case Kind::AlarmStats:        drawStats(c); return true;
        case Kind::ProductionCounter: drawProduction(c); return true;
        case Kind::VariableTable:     drawVariableTable(c); return true;
        case Kind::RecipeEditor:      drawRecipeEditor(c); return true;
        case Kind::ExportButton:      drawExportButton(c); return true;
        default:                      return false;
    }
}

} // namespace app::paint
