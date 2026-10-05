// app/hmi/HmiLot14Painter.cpp - les objets de la communication (lot 14) :
// l'etat de la communication (un voyant, une ligne) et le diagnostic automate.
#include "HmiIcons.hpp"
#include "HmiPaintKit.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace app::paint {

namespace {

std::vector<gfx::Point> ellipseLocal(double cx, double cy, double rx, double ry, int n = 28) {
    std::vector<gfx::Point> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double a = 6.283185307179586 * k / n;
        out.push_back({static_cast<float>(cx + rx * std::cos(a)), static_cast<float>(cy + ry * std::sin(a))});
    }
    return out;
}

gfx::Color toneColor(const Ctx& c, int tone) {
    switch (tone) {
        case 1: return c.color("colorGood", gfx::Color::rgb(0x2ECC71));
        case 2: return c.color("colorWarn", gfx::Color::rgb(0xF2C94C));
        case 3: return c.color("colorBad", gfx::Color::rgb(0xE5534B));
        case 4: return c.color("colorSim", gfx::Color::rgb(0x56CCF2));
        default: return c.fixed(0x6B7686);
    }
}

// Un voyant rond, son halo quand il est allume, un reflet.
void led(const Ctx& c, double cx, double cy, double r, gfx::Color col, bool halo) {
    if (r <= 0.5) return;
    if (halo) shapes::fillPolygon(c.r, c.map(ellipseLocal(cx, cy, r * 1.65, r * 1.65)), col.withAlpha(static_cast<std::uint8_t>(col.a * 0.22f)));
    shapes::fillPolygon(c.r, c.map(ellipseLocal(cx, cy, r, r)), col);
    shapes::strokePolyline(c.r, c.map(ellipseLocal(cx, cy, r, r)), true, c.fixed(0x10141A).withAlpha(static_cast<std::uint8_t>(120 * c.alpha)),
                           std::max(1.f, c.vp.zoom));
    shapes::fillPolygon(c.r, c.map(ellipseLocal(cx - r * 0.32, cy - r * 0.36, r * 0.34, r * 0.22)),
                        gfx::Color{255, 255, 255, static_cast<std::uint8_t>(110 * c.alpha)});
}

// Une glyphe dans une boite locale (mise a l'ecran par ses deux coins).
void glyph(const Ctx& c, HmiGlyph g, double x, double y, double s, gfx::Color col) {
    const auto a = c.map(x, y), z = c.map(x + s, y + s);
    drawHmiGlyph(c.r, g, {std::min(a.x, z.x), std::min(a.y, z.y), std::fabs(z.x - a.x), std::fabs(z.y - a.y)}, col);
}

void fillBox(const Ctx& c, const hmi::Box& b, gfx::Color col, double radius = 0) {
    if (b.w <= 0 || b.h <= 0 || !col.a) return;
    auto pts = rectLocal(b.w, b.h, radius);
    for (auto& p : pts) {
        p.x += static_cast<float>(b.x);
        p.y += static_cast<float>(b.y);
    }
    shapes::fillPolygon(c.r, c.map(pts), col);
}

bool drawCommStatus(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double radius = std::max(0.0, c.src.number(c.o, "radius", 4));
    fillAndStroke(c, rectLocal(w, h, radius));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 14), 6.0, 60.0);
    const bool compact = c.src.flag(c.o, "compact", false);
    const bool showAddress = c.src.flag(c.o, "showAddress", true);
    const bool showTime = c.src.flag(c.o, "showTime", true);
    static const hmi::Communication kPlain;
    const hmi::Communication& comm = c.opt.project ? c.opt.project->comm : kPlain;
    hmi::comm::StatusLine line;
    if (!c.opt.editor && c.opt.runtime) {
        line = hmi::comm::commStatusLine(comm, c.opt.runtime->link(), c.opt.runtime->simulatorAttached(), showAddress, showTime, compact);
    } else if (comm.modbus()) {
        // Dans l'editeur : ce qu'elle dira, sans la liaison.
        line.tone = 0;
        line.text = compact ? std::string("Modbus TCP")
                            : "Modbus TCP" + (showAddress ? " \xE2\x80\x94 " + comm.host + ":" + std::to_string(comm.port) : std::string{});
    } else {
        line.tone = 4;
        line.text = "Simulateur";
    }
    const double r = std::clamp(h * 0.24, 3.0, 14.0);
    const double cx = std::min(w / 2, r + std::max(6.0, h * 0.28)), cy = h / 2;
    const gfx::Color col = toneColor(c, line.tone);
    // Le voyant clignote doucement quand la liaison est coupee.
    gfx::Color shown = col;
    if (line.tone == 3 && !c.opt.editor) {
        const double k = 0.55 + 0.45 * std::fabs(std::sin(c.opt.time * 3.0));
        shown = col.withAlpha(static_cast<std::uint8_t>(col.a * k));
    }
    led(c, cx, cy, r, shown, line.tone == 1 || line.tone == 4);
    const double tx = cx + r + 10;
    if (w - tx > 8) text(c, fitted(c, line.text, w - tx - 8, fs), tx, 0, w - tx - 6, h, c.color("textColor", gfx::Color::rgb(0xE6EAF0)), fs, "gauche");
    return true;
}

bool drawPlcDiagnostic(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double radius = std::max(0.0, c.src.number(c.o, "radius", 4));
    fillAndStroke(c, rectLocal(w, h, radius));
    static const hmi::Communication kPlain;
    const hmi::Communication& comm = c.opt.project ? c.opt.project->comm : kPlain;
    const hmi::Runtime* rt = !c.opt.editor ? c.opt.runtime : nullptr;
    const hmi::comm::Link* link = rt ? rt->link() : nullptr;
    const auto demo = rt ? rt->commDemoState() : std::pair<bool, int>{false, comm.demoPort};
    auto rows = hmi::comm::diagnosticRows(comm, link, rt && rt->simulatorAttached(), demo.first, demo.second);
    if (!rt && comm.modbus()) {
        // Dans l'editeur : les reglages, sans la liaison.
        rows = {{"Liaison", "Modbus TCP", 0},
                {"Adresse", comm.host + ":" + std::to_string(comm.port) + " \xC2\xB7 esclave " + std::to_string(comm.unit), 0},
                {"\xC3\x89tat", "en marche : connect\xC3\xA9" "e, d\xC3\xA9" "connect\xC3\xA9" "e...", 0},
                {"Scrutation", "toutes les " + std::to_string(comm.periodMs) + " ms, d\xC3\xA9lai " + std::to_string(comm.timeoutMs) + " ms", 0}};
    }
    const auto l = hmi::comm::diagnosticLayout(c.o, w, h, rows.size());
    const double fs = l.fontSize;
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const gfx::Color label = c.color("labelColor", gfx::Color::rgb(0x9AA6B8));
    // Le bandeau : la glyphe, le titre, l'etat a droite.
    fillBox(c, l.title, c.color("headerColor", gfx::Color::rgb(0x2F3B4C)), radius);
    const double g = std::min(l.title.h * 0.56, 22.0);
    glyph(c, HmiGlyph::Diagnostic, 10, l.title.cy() - g / 2, g, txt);
    int stateTone = 4;
    std::string state = "Simulateur";
    if (link) {
        const auto d = link->diagnostics();
        stateTone = d.connected ? (d.bad || d.stale ? 2 : 1) : (d.state.rfind("Connexion", 0) == 0 ? 2 : 3);
        state = d.connected ? "Connect\xC3\xA9" "e" : d.state;
    } else if (comm.modbus()) {
        stateTone = rt ? 3 : 0;
        state = rt ? "\xC3\xA0 l'arr\xC3\xAAt" : "Modbus TCP";
    }
    const double stateW = std::min(w * 0.36, hmi::approxTextWidth(state, fs) + 34);
    const double sx = w - stateW - 8;
    led(c, sx + 10, l.title.cy(), std::min(7.0, l.title.h * 0.2), toneColor(c, stateTone), stateTone == 1);
    text(c, fitted(c, state, stateW - 24, fs), sx + 22, 0, stateW - 22, l.title.h, txt, fs, "gauche");
    const std::string title = c.src.text(c.o, "title", "Diagnostic automate");
    text(c, fitted(c, title, sx - g - 30, fs * 1.15), g + 18, 0, sx - g - 24, l.title.h, txt, fs * 1.15, "gauche");
    // Les lignes : libelle a gauche, valeur a droite, colorees par leur ton.
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const double y = l.rows.y + static_cast<double>(i) * l.rowH;
        if (y + l.rowH > l.rows.bottom() + 0.5) break;
        if (i % 2 == 0) fillBox(c, {l.rows.x - 4, y, l.rows.w + 8, l.rowH}, gfx::Color{255, 255, 255, static_cast<std::uint8_t>(10 * c.alpha)});
        text(c, fitted(c, rows[i].label, l.labelW - 10, fs), l.rows.x, y, l.labelW - 6, l.rowH, label, fs, "gauche");
        const gfx::Color vc = rows[i].tone == 0 ? txt : toneColor(c, rows[i].tone);
        text(c, fitted(c, rows[i].value, l.rows.w - l.labelW - 4, fs), l.rows.x + l.labelW, y, l.rows.w - l.labelW, l.rowH, vc, fs, "gauche");
    }
    // Les variables en defaut.
    if (l.list.h > 0) {
        const double lh = l.rowH;
        fillBox(c, l.list, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(40 * c.alpha)}, 3);
        std::vector<std::pair<std::string, std::string>> bad;
        if (link) bad = link->diagnostics().badPoints;
        text(c, bad.empty() ? std::string("Variables en d\xC3\xA9" "faut : aucune") : "Variables en d\xC3\xA9" "faut (" + std::to_string(bad.size()) + ")",
             l.list.x + 8, l.list.y, l.list.w - 16, lh, bad.empty() ? label : toneColor(c, 3), fs, "gauche");
        for (std::size_t i = 0; i < bad.size(); ++i) {
            const double y = l.list.y + lh * static_cast<double>(i + 1);
            if (y + lh > l.list.bottom() + 0.5) break;
            const std::string row = bad[i].first + " \xE2\x80\x94 " + bad[i].second;
            text(c, fitted(c, row, l.list.w - 24, fs * 0.95), l.list.x + 16, y, l.list.w - 24, lh, toneColor(c, bad[i].second.rfind("ancienne", 0) == 0 ? 2 : 3),
                 fs * 0.95, "gauche");
        }
    }
    if (l.reconnect.w > 0) {
        const gfx::Color button = c.color("buttonColor", gfx::Color::rgb(0x2A313C));
        drawButtonBox(c, l.reconnect, "Reconnecter", link ? button : button.withAlpha(static_cast<std::uint8_t>(button.a * 0.5f)),
                      link ? txt : label, fs);
        drawButtonBox(c, l.reset, "Remettre \xC3\xA0 z\xC3\xA9ro les compteurs", link ? button : button.withAlpha(static_cast<std::uint8_t>(button.a * 0.5f)),
                      link ? txt : label, fs);
    }
    return true;
}

} // namespace

bool drawLot14(const Ctx& c) {
    if (c.o.kind == hmi::Kind::CommStatus) return drawCommStatus(c);
    if (c.o.kind == hmi::Kind::PlcDiagnostic) return drawPlcDiagnostic(c);
    return false;
}

} // namespace app::paint
