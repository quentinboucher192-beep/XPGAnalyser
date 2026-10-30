// =============================================================================
//  app/hmi/HmiRecordPainter.cpp - 1.14.0 (M4) : l'Enregistreur et le Lecteur
//  d'enregistrements
// -----------------------------------------------------------------------------
//  L'ENREGISTREUR : le titre, un bouton rond (rouge : Enregistrer ; un carre
//  blanc qui bat : Arreter), le chrono, l'etat. Pendant une relecture, il montre
//  ce qu'il montrait pendant l'enregistrement : il tournait, depuis la position.
//  LE LECTEUR : les enregistrements du dossier (ceux de son prefixe), chacun
//  avec sa duree, sa date, un cadenas s'il est protege, et Rejouer ; la ligne
//  choisie surlignee ; l'Enregistrement fixe : un seul grand bouton. Dans
//  l'editeur, deux lignes d'exemple.
//  Les rectangles sont ceux de hmi::rec (recorderLayout, playerLayout) : le clic
//  (HmiSimulation.cpp) touche ce que le dessin montre.
//  LA RELECTURE A L'ECRAN (paintReplayChrome) : le bandeau, la barre du bas et
//  leurs parties (le canevas en marche les dessine et les clique), le pointeur
//  fantome et les cercles des clics.
// =============================================================================
#include "HmiPaintKit.hpp"
#include "HmiSimulation.hpp"   // la relecture a l'ecran (ReplayChrome)
#include "../../hmi/HmiRecording.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace app::paint {

namespace {

std::vector<gfx::Point> circleLocal(double cx, double cy, double r, int n = 32) {
    std::vector<gfx::Point> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double a = 6.283185307179586 * k / n;
        out.push_back({static_cast<float>(cx + r * std::cos(a)), static_cast<float>(cy + r * std::sin(a))});
    }
    return out;
}
std::vector<gfx::Point> boxLocal(double x, double y, double w, double h) {
    return {{static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(x + w), static_cast<float>(y)},
            {static_cast<float>(x + w), static_cast<float>(y + h)}, {static_cast<float>(x), static_cast<float>(y + h)}};
}
// Un cadenas : le corps et l'anse.
void padlock(const Ctx& c, double x, double y, double size, gfx::Color col) {
    const double bw = size * 0.8, bh = size * 0.55;
    shapes::fillPolygon(c.r, c.map(boxLocal(x, y + size - bh, bw, bh)), col);
    std::vector<gfx::Point> arc;
    for (int k = 0; k <= 12; ++k) {
        const double a = 3.141592653589793 * k / 12.0;
        arc.push_back({static_cast<float>(x + bw / 2 - std::cos(a) * bw * 0.32), static_cast<float>(y + size - bh - std::sin(a) * size * 0.42)});
    }
    shapes::strokePolyline(c.r, c.map(arc), false, col, std::max(1.f, static_cast<float>(size * 0.12 * c.vp.zoom)));
}
// Le triangle Rejouer.
void playTriangle(const Ctx& c, double cx, double cy, double size, gfx::Color col) {
    shapes::fillPolygon(c.r,
                        c.map(std::vector<gfx::Point>{{static_cast<float>(cx - size * 0.35), static_cast<float>(cy - size * 0.42)},
                                                      {static_cast<float>(cx + size * 0.45), static_cast<float>(cy)},
                                                      {static_cast<float>(cx - size * 0.35), static_cast<float>(cy + size * 0.42)}}),
                        col);
}

bool drawRecorder(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 14), 7.0, 40.0);
    fillAndStroke(c, rectLocal(w, h, 6));
    const std::string title = c.src.text(c.o, "title");
    const auto l = hmi::rec::recorderLayout(w, h, fs, !title.empty());
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color red = c.color("accent", gfx::Color::rgb(0xE5484D));
    if (!title.empty()) text(c, title, l.title.x, l.title.y, l.title.w, l.title.h, txt, fs, "gauche");
    // L'etat : en marche, celui du moteur ; pendant une relecture, celui d'alors (il tournait, depuis la position).
    bool recording = false, stopping = false;
    double seconds = 0;
    const hmi::Runtime* rt = c.opt.editor ? nullptr : c.opt.runtime;
    if (rt) {
        if (rt->replaying()) {
            recording = true;
            seconds = rt->replayInfo().position;
        } else {
            const auto info = rt->recordingInfo();
            recording = info.active;
            stopping = info.stopping;
            seconds = info.seconds;
        }
    }
    const bool pilotable = c.src.flag(c.o, "pilotable", true);
    const bool beat = std::fmod(c.opt.time, 1.0) < 0.5;
    gfx::Color ring = recording ? (beat ? red : fade(red, 0.55f)) : red;
    if (!pilotable) ring = c.fixed(0x6B7686);
    shapes::fillPolygon(c.r, c.map(circleLocal(l.cx, l.cy, l.radius)), ring);
    shapes::strokePolyline(c.r, c.map(circleLocal(l.cx, l.cy, l.radius)), true, c.fixed(0x10141A).withAlpha(static_cast<std::uint8_t>(110 * c.alpha)),
                           std::max(1.f, c.vp.zoom));
    const gfx::Color ink = c.fixedOn(0xFFFFFF);
    if (recording || stopping) {
        const double s = l.radius * 0.72;
        shapes::fillPolygon(c.r, c.map(boxLocal(l.cx - s / 2, l.cy - s / 2, s, s)), ink);     // Arreter : un carre
    } else {
        shapes::fillPolygon(c.r, c.map(circleLocal(l.cx, l.cy, l.radius * 0.38)), ink);   // Enregistrer : un point
    }
    const std::string label = recording ? "Arr\xC3\xAAter" : stopping ? "Arr\xC3\xAAt\xE2\x80\xA6" : "Enregistrer";
    text(c, label, l.label.x, l.label.y, l.label.w, l.label.h, txt, fs * 1.05, "gauche");
    std::string status;
    if (recording) status = "\xE2\x97\x8F enregistre depuis " + hmi::rec::clock(seconds);
    else if (stopping) status = "le nom est demand\xC3\xA9 (" + hmi::rec::clock(seconds) + ")";
    else if (!pilotable) status = "non pilotable";
    else {
        status = "pr\xC3\xAAt";
        if (const double m = c.src.number(c.o, "maxMinutes", 0); m > 0) status += " \xC2\xB7 " + hmi::formatNumber(m) + " min au plus";
    }
    text(c, status, l.status.x, l.status.y, l.status.w, l.status.h, recording ? red : fade(txt, 0.7f), fs * 0.85, "gauche");
    return true;
}

bool drawPlayer(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 7.0, 40.0);
    fillAndStroke(c, rectLocal(w, h, 6));
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color accent = c.color("accent", gfx::Color::rgb(0x2F6FD6));
    const std::string title = c.src.text(c.o, "title");
    const std::string fixed = c.src.text(c.o, "fixed");
    const std::string prefix = c.src.text(c.o, "prefix");
    struct Row { std::string name, duration, date; bool locked{false}; };
    std::vector<Row> rows;
    const hmi::Runtime* rt = c.opt.editor ? nullptr : c.opt.runtime;
    if (rt) {
        for (const auto& it : hmi::rec::withPrefix(rt->recordings(), prefix))
            rows.push_back(Row{it.name, hmi::rec::clock(it.header.duration), it.header.date.substr(0, 16), it.header.locked()});
    } else {
        rows.push_back(Row{"D\xC3\xA9marrage station", "02:15", "2026-10-29 14:02", false});
        rows.push_back(Row{"Essai FAT client", "01:30", "2026-10-28 09:41", true});
    }
    const auto l = hmi::rec::playerLayout(w, h, fs, rows.size(), !fixed.empty(), !title.empty());
    if (!title.empty()) {
        text(c, title, l.title.x, l.title.y, l.title.w, l.title.h, txt, fs * 1.1, "gauche");
        c.r.line(c.map(0, l.title.h), c.map(w, l.title.h), fade(c.color("stroke", gfx::Color::rgb(0x3A4556)), 0.9f), std::max(1.f, c.vp.zoom));
    }
    if (!fixed.empty()) {
        const auto& b = l.fixedButton;
        drawButtonBox(c, hmi::Box{b.x, b.y, b.w, b.h}, "\xE2\x96\xB6  Rejouer \xC2\xAB " + fixed + " \xC2\xBB", accent, c.fixedOn(0xFFFFFF), fs);
        return true;
    }
    if (rows.empty()) {
        text(c, prefix.empty() ? "Aucun enregistrement" : "Aucun enregistrement \xC2\xAB " + prefix + "\xE2\x80\xA6 \xC2\xBB", 8, l.list.y + 4,
             w - 16, std::max(fs * 2.0, 20.0), fade(txt, 0.6f), fs, "gauche");
        return true;
    }
    const int chosen = rt ? rt->recordingPlayerRow(c.o.id) : -1;
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const auto& r = l.rows[i];
        const auto& p = l.plays[i];
        if (static_cast<int>(i) == chosen) shapes::fillPolygon(c.r, c.map(boxLocal(r.x, r.y, r.w, r.h)), fade(accent, 0.28f));
        else if (i % 2 == 1) shapes::fillPolygon(c.r, c.map(boxLocal(r.x, r.y, r.w, r.h)), fade(txt, 0.05f));
        const double pad = std::max(6.0, fs * 0.5);
        const double right = p.x - pad;
        const double metaW = std::min(right * 0.5, fs * 11.0);
        const double lockW = rows[i].locked ? fs * 1.2 : 0.0;
        text(c, rows[i].name, pad, r.y, std::max(0.0, right - metaW - lockW - pad * 2), r.h, txt, fs, "gauche");
        if (rows[i].locked) padlock(c, right - metaW - lockW, r.y + (r.h - fs) / 2, fs, fade(txt, 0.8f));
        text(c, rows[i].duration + "  " + rows[i].date.substr(0, 10), right - metaW, r.y, metaW, r.h, fade(txt, 0.65f), fs * 0.85, "droite");
        // Rejouer : un rond de la couleur d'accent, le triangle blanc.
        const double rr = std::min(p.w, p.h) * 0.38;
        shapes::fillPolygon(c.r, c.map(circleLocal(p.x + p.w / 2, p.y + p.h / 2, rr)), accent);
        playTriangle(c, p.x + p.w / 2 + rr * 0.08, p.y + p.h / 2, rr * 1.1, c.fixedOn(0xFFFFFF));
    }
    if (rows.size() > l.rows.size()) {
        const std::string more = "+ " + std::to_string(rows.size() - l.rows.size()) + " autres";
        text(c, more, 8, h - fs * 1.4, w - 16, fs * 1.3, fade(txt, 0.55f), fs * 0.8, "droite");
    }
    return true;
}

} // namespace

bool drawRecordings(const Ctx& c) {
    if (c.o.kind == hmi::Kind::Recorder) return drawRecorder(c);
    if (c.o.kind == hmi::Kind::RecordingPlayer) return drawPlayer(c);
    return false;
}

} // namespace app::paint

// ============================================================== la relecture a l'ecran ==
namespace app {

namespace {

// "2026-10-29 14:02:33" -> "29/10/2026 14:02" (autre chose : tel quel).
std::string frenchDate(const std::string& iso) {
    if (iso.size() < 16 || iso[4] != '-' || iso[7] != '-') return iso;
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4) + " " + iso.substr(11, 5);
}
std::string speedText(double v) {
    std::string t = hmi::formatNumber(v);
    for (auto& c : t)
        if (c == '.') c = ',';
    return "\xC3\x97" + t;
}
// Un texte tenu dans sa largeur (coupe d'un ...).
void fitText(gfx::IRenderer& r, gfx::Point at, const std::string& text, gfx::FontId font, gfx::Color col, float width) {
    if (width <= 4.f) return;
    std::size_t n = r.fitCharacters(text, font, width);
    if (n >= text.size()) {
        r.drawText(at, text, font, col);
        return;
    }
    const std::string dots = "\xE2\x80\xA6";
    n = r.fitCharacters(text, font, std::max(0.f, width - r.measure(dots, font).width));
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;   // pas au milieu d'un caractere
    r.drawText(at, text.substr(0, n) + dots, font, col);
}

} // namespace

// 1.14.0 (captures finales) : la legende de la barre de relecture, la plus longue qui tient dans `width` ; rien si
// meme la plus courte n'entre pas (une legende coupee ne se lit pas : l'infobulle de la place reste).
std::string replayLegend(gfx::IRenderer& r, gfx::FontId font, float width) {
    static const char* const kForms[] = {
        "Espace pause \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 10 s \xC2\xB7 + \xE2\x88\x92 vitesse \xC2\xB7 rouge : alarme \xC2\xB7 couleur d'accent : clic",
        "Espace pause \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 10 s \xC2\xB7 + \xE2\x88\x92 vitesse",
        "Espace \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 \xC2\xB7 + \xE2\x88\x92",
    };
    for (const char* form : kForms)
        if (r.measure(form, font).width <= width) return form;
    return {};
}

std::string ReplayChrome::partAt(gfx::Point p) const {
    for (const auto& [name, rect] : parts)
        if (rect.contains(p)) return name;
    if (progress.w > 0 && gfx::Rect{progress.x - 6.f, progress.y - 4.f, progress.w + 12.f, progress.h + 8.f}.contains(p)) return "progress";
    return {};
}

gfx::Rect ReplayChrome::part(std::string_view name) const {
    if (name == "progress") return progress;
    for (const auto& [n, rect] : parts)
        if (n == name) return rect;
    return {};
}

double ReplayChrome::fractionAt(float x) const {
    if (progress.w <= 0.f) return 0.0;
    return std::clamp(static_cast<double>((x - progress.x) / progress.w), 0.0, 1.0);
}

gfx::Rect replayViewArea(const gfx::Rect& whole) {
    const float h = std::max(40.f, whole.h - kReplayBannerH - kReplayBarH);
    return {whole.x, whole.y + kReplayBannerH, whole.w, h};
}

ReplayChrome paintReplayChrome(gfx::IRenderer& r, const ui::Theme& theme, const gfx::Rect& whole, const hmi::ReplayInfo& info,
                               const std::vector<hmi::rec::Player::Marker>& markers, gfx::Point mouse) {
    ReplayChrome c;
    const auto& col = theme.color;
    const auto& f = theme.font;
    const gfx::Color accent = col.accent;
    const gfx::Color red = gfx::Color::rgb(0xE5484D);
    c.banner = {whole.x, whole.y, whole.w, kReplayBannerH};
    c.view = replayViewArea(whole);
    c.bar = {whole.x, c.view.bottom(), whole.w, whole.bottom() - c.view.bottom()};
    // ---- le lisere : la vue n'est pas en direct
    r.strokeRect({c.view.x + 1.5f, c.view.y + 1.5f, c.view.w - 3.f, c.view.h - 3.f}, accent, 3.f);
    // ---- le bandeau
    r.fillRect(c.banner, accent);
    const gfx::Color onAccent = col.textInverted;
    const float ty = c.banner.y + (c.banner.h - r.lineHeight(f.ui)) / 2.f;
    float x = c.banner.x + 12.f;
    const std::string head = "\xE2\x96\xB6 RELECTURE";
    r.drawText({x, ty}, head, f.ui, onAccent);
    r.drawText({x + 0.6f, ty}, head, f.ui, onAccent);   // en gras : deux passes
    x += r.measure(head, f.ui).width + 16.f;
    const std::string quit = "\xC3\x89" "chap quitte";
    const float quitW = r.measure(quit, f.ui).width;
    std::string who = info.header.name;
    std::string when = "enregistr\xC3\xA9 le " + frenchDate(info.header.date);
    if (!info.header.by.empty()) when += " par " + info.header.by;
    if (!info.header.where.empty()) when += " \xC2\xB7 " + info.header.where;
    fitText(r, {x, ty}, who + "  \xE2\x80\x94  " + when, f.ui, onAccent, c.banner.right() - quitW - 24.f - x);
    r.drawText({c.banner.right() - quitW - 12.f, ty}, quit, f.ui, onAccent);
    // ---- la barre du bas
    r.fillRect(c.bar, col.panelBg);
    r.line({c.bar.x, c.bar.y + 0.5f}, {c.bar.right(), c.bar.y + 0.5f}, col.border, 1.f);
    const float pad = 12.f;
    // La progression : la piste, le rempli, les reperes, le bouton.
    c.progress = {c.bar.x + pad + 7.f, c.bar.y + 10.f, std::max(10.f, c.bar.w - 2 * pad - 14.f), 16.f};
    const float trackY = c.progress.y + 6.f;
    r.fillRoundedRect({c.progress.x, trackY, c.progress.w, 4.f}, col.inputBg, 2.f);
    const double frac = info.length > 0 ? std::clamp(info.position / info.length, 0.0, 1.0) : 0.0;
    const float knobX = c.progress.x + static_cast<float>(frac) * c.progress.w;
    r.fillRoundedRect({c.progress.x, trackY, knobX - c.progress.x, 4.f}, accent, 2.f);
    std::string hover;
    for (const auto& m : markers) {
        if (info.length <= 0) break;
        const float mx = c.progress.x + static_cast<float>(std::clamp(m.t / info.length, 0.0, 1.0)) * c.progress.w;
        const gfx::Color mc = m.kind == 'a' ? red : m.kind == 'c' ? accent : col.textMuted;
        const gfx::Rect mark{mx - 1.f, c.progress.y + 2.f, 2.f, 12.f};
        r.fillRect(mark, mc);
        if (std::fabs(mouse.x - mx) <= 3.f && mouse.y >= c.progress.y - 2.f && mouse.y <= c.progress.bottom() + 2.f)
            hover = hmi::rec::clock(m.t) + "  " + (m.kind == 'a' ? "alarme " : m.kind == 'c' ? "clic " : "vue ") + m.text;
    }
    r.fillRoundedRect({knobX - 7.f, c.progress.y + 1.f, 14.f, 14.f}, accent, 7.f);
    r.fillRoundedRect({knobX - 5.f, c.progress.y + 3.f, 10.f, 10.f}, col.text, 5.f);
    // Les commandes.
    const float rowY = c.progress.bottom() + 8.f;
    const float rowH = std::max(24.f, c.bar.bottom() - rowY - 8.f);
    const float textY = rowY + (rowH - r.lineHeight(f.ui)) / 2.f;
    x = c.bar.x + pad;
    const auto button = [&](const std::string& name, float w, bool primary, bool on, const std::function<void(const gfx::Rect&, gfx::Color)>& inside) {
        const gfx::Rect b{x, rowY, w, rowH};
        const bool over = b.contains(mouse);
        const gfx::Color bg = primary || on ? (over ? col.accentHover : accent) : (over ? col.selectionBg : col.headerBg);
        r.fillRoundedRect(b, bg, 4.f);
        if (!primary && !on) r.strokeRect(b, col.border, 1.f);
        inside(b, primary || on ? col.textInverted : col.text);
        c.parts.emplace_back(name, b);
        x += w + 6.f;
    };
    const auto label = [&](const std::string& t) {
        return [&r, &f, t, textY](const gfx::Rect& b, gfx::Color ink) {
            const float w = r.measure(t, f.ui).width;
            r.drawText({b.x + (b.w - w) / 2.f, textY}, t, f.ui, ink);
        };
    };
    const auto tri = [&r](float cx, float cy, float s, bool right, gfx::Color ink) {
        const float d = right ? 1.f : -1.f;
        ui::shapes::fillPolygon(r, std::vector<gfx::Point>{{cx - d * s * 0.45f, cy - s * 0.55f}, {cx + d * s * 0.55f, cy}, {cx - d * s * 0.45f, cy + s * 0.55f}}, ink);
    };
    const float s = std::min(12.f, rowH * 0.45f);
    button("start", 34.f, false, false, [&](const gfx::Rect& b, gfx::Color ink) {          // |<
        r.fillRect({b.x + b.w / 2.f - s * 0.65f, b.y + b.h / 2.f - s * 0.55f, 2.f, s * 1.1f}, ink);
        tri(b.x + b.w / 2.f + s * 0.15f, b.y + b.h / 2.f, s, false, ink);
    });
    button("back", std::max(52.f, r.measure("\xE2\x88\x92" "10 s", f.ui).width + 16.f), false, false, label("\xE2\x88\x92" "10 s"));
    const std::string playText = info.paused ? "Lecture" : "Pause";
    button("play", std::max(92.f, r.measure(playText, f.ui).width + 40.f), true, false, [&](const gfx::Rect& b, gfx::Color ink) {
        const float tw = r.measure(playText, f.ui).width;
        const float gx = b.x + (b.w - tw - s - 8.f) / 2.f;
        if (info.paused) tri(gx + s / 2.f, b.y + b.h / 2.f, s, true, ink);
        else {
            r.fillRect({gx, b.y + b.h / 2.f - s * 0.5f, s * 0.32f, s}, ink);
            r.fillRect({gx + s * 0.6f, b.y + b.h / 2.f - s * 0.5f, s * 0.32f, s}, ink);
        }
        r.drawText({gx + s + 8.f, textY}, playText, f.ui, ink);
    });
    button("fwd", std::max(52.f, r.measure("+10 s", f.ui).width + 16.f), false, false, label("+10 s"));
    button("end", 34.f, false, false, [&](const gfx::Rect& b, gfx::Color ink) {            // >|
        tri(b.x + b.w / 2.f - s * 0.15f, b.y + b.h / 2.f, s, true, ink);
        r.fillRect({b.x + b.w / 2.f + s * 0.45f, b.y + b.h / 2.f - s * 0.55f, 2.f, s * 1.1f}, ink);
    });
    // Le temps.
    const std::string time = hmi::rec::clock(info.position) + " / " + hmi::rec::clock(info.length) + (info.ended ? "  (fin)" : "");
    x += 4.f;
    r.drawText({x, textY}, time, f.ui, col.text);
    x += std::max(r.measure(time, f.ui).width, r.measure("00:00 / 00:00", f.ui).width) + 12.f;
    // Quitter, a droite ; les vitesses entre les deux, puis le bouton Sons (1.14.1, N3).
    const float quitBtnW = std::max(80.f, r.measure("Quitter", f.ui).width + 24.f);
    const std::string soundsText = info.soundsMuted ? "Coup\xC3\xA9s" : info.alarmSoundsOnly ? "Alarmes" : "Sons";
    const float right = c.bar.right() - pad - quitBtnW - 6.f;
    // Son libelle (Sons, Coupes, Alarmes) seulement si la legende des touches garde sa place (sa zone, son infobulle) et
    // ses touches (une forme plus courte, si) ; sinon le haut-parleur seul - son etat se lit a l'icone et au fond rouge,
    // le detail en infobulle.
    const float labelW = std::max({r.measure("Coup\xC3\xA9s", f.ui).width, r.measure("Alarmes", f.ui).width, r.measure("Sons", f.ui).width})
                         + rowH * 0.7f + 22.f;
    const float iconW = rowH * 0.6f + 18.f;
    const float speedsEnd = x + static_cast<float>(std::size(hmi::rec::kSpeeds)) * (44.f + 2.f) + 10.f;
    const float helpWith = right - (speedsEnd + labelW + 6.f), helpWithout = right - (speedsEnd + iconW + 6.f);
    const bool soundsLabel = helpWith > 4.f && replayLegend(r, f.smallUi, helpWith).empty() == replayLegend(r, f.smallUi, helpWithout).empty();
    const float soundsW = soundsLabel ? labelW : iconW;
    const float speedsRight = right - soundsW - 16.f;   // les vitesses s'arretent avant le bouton Sons
    float speedW = 44.f;
    const float speedsW = static_cast<float>(std::size(hmi::rec::kSpeeds)) * (speedW + 2.f);
    if (x + speedsW > speedsRight) speedW = std::max(30.f, (speedsRight - x) / static_cast<float>(std::size(hmi::rec::kSpeeds)) - 2.f);
    if (x + static_cast<float>(std::size(hmi::rec::kSpeeds)) * (speedW + 2.f) <= speedsRight + 1.f) {
        for (const double v : hmi::rec::kSpeeds) {
            const bool on = std::fabs(v - info.speed) < 1e-6;
            const std::string t = speedText(v);
            const gfx::Rect b{x, rowY, speedW, rowH};
            const bool over = b.contains(mouse);
            r.fillRect(b, on ? accent : over ? col.selectionBg : col.headerBg);
            r.strokeRect(b, col.border, 1.f);
            const float w = r.measure(t, f.smallUi).width;
            r.drawText({b.x + (b.w - w) / 2.f, rowY + (rowH - r.lineHeight(f.smallUi)) / 2.f}, t, f.smallUi, on ? col.textInverted : col.textMuted);
            c.parts.emplace_back("speed:" + hmi::formatNumber(v), b);
            x += speedW + 2.f;
        }
        x += 10.f;
    }
    // 1.14.1 (N3) : LE BOUTON SONS - le canal Relecture (le M de sa piste au Melangeur, SYS.MuteReplay ; la touche S).
    // Actif : le haut-parleur et ses ondes ; coupe : barre, sur le fond rouge ; tout le son coupe (le General) : barre,
    // estompe. A x4 et x8 : "Alarmes" (seuls les sons d'alarme se rejouent).
    if (x + soundsW <= right) {
        const bool muted = info.soundsMuted, silenced = !muted && info.soundsGeneralMuted;
        const gfx::Rect b{x, rowY, soundsW, rowH};
        const bool over = b.contains(mouse);
        const gfx::Color bg = muted ? (over ? red.withAlpha(220) : red) : silenced ? red.withAlpha(90) : over ? col.selectionBg : col.headerBg;
        r.fillRoundedRect(b, bg, 4.f);
        if (!muted) r.strokeRect(b, col.border, 1.f);
        const gfx::Color ink = muted ? col.textInverted : silenced ? col.textMuted : col.text;
        const float g = rowH * 0.6f, k = g / 16.f, gx = soundsLabel ? b.x + 9.f : b.x + (b.w - g) / 2.f, gy = b.y + (b.h - g) / 2.f;
        const auto P = [&](float px, float py) { return gfx::Point{gx + px * k, gy + py * k}; };
        ui::shapes::fillPolygon(r, std::vector<gfx::Point>{P(1.5f, 6), P(4.5f, 6), P(8.5f, 2.5f), P(8.5f, 13.5f), P(4.5f, 10), P(1.5f, 10)}, ink);
        const float t = std::max(1.f, 1.5f * k);
        if (muted || silenced) {
            r.line(P(10.5f, 5.5f), P(15, 10.5f), ink, t);
            r.line(P(15, 5.5f), P(10.5f, 10.5f), ink, t);
        } else {
            for (const float rad : {3.5f, 6.2f})
                ui::shapes::strokePolyline(r, ui::shapes::arc(P(8.5f, 8), rad * k, -48.f, 48.f, 16), false, ink, t);
        }
        if (soundsLabel) r.drawText({gx + g + 6.f, textY}, soundsText, f.ui, ink);
        c.parts.emplace_back("sons", b);
        x += soundsW + 6.f;
        c.soundsTip = std::string(muted ? "Sons de la relecture : coup\xC3\xA9s"
                                  : silenced ? "Sons de la relecture : actifs, mais tout le son est coup\xC3\xA9 (G\xC3\xA9n\xC3\xA9ral)"
                                  : info.alarmSoundsOnly ? "Sons de la relecture : les sons d'alarme seulement (\xC3\x97" "4, \xC3\x97" "8)"
                                                         : "Sons de la relecture : actifs")
                      + " - un clic (ou S) les coupe ou les r\xC3\xA9tablit : le canal Relecture du M\xC3\xA9langeur (SYS.MuteReplay).\n"
                      + "Se rejouent : les alarmes et leur r\xC3\xA9p\xC3\xA9tition, IHM_SON, l'action Jouer un son - de \xC3\x97" "0,25 \xC3\xA0 \xC3\x97" "2 ; "
                      + "\xC3\xA0 \xC3\x97" "4 et \xC3\x97" "8, les sons d'alarme seulement. En pause, apr\xC3\xA8s un saut, en quittant : ce qui jouait se tait.";
        if (!info.header.soundsKept)
            c.soundsTip += "\nCet enregistrement a \xC3\xA9t\xC3\xA9 fait sans les sons (Configuration \xE2\x80\xBA Enregistrements \xE2\x80\xBA Sons).";
    }
    // L'aide des touches (ou ce qui a change dans le projet depuis l'enregistrement), dans ce qui reste.
    // 1.14.0 (captures finales) : LA LEGENDE TIENT DANS SA PLACE - la plus longue de ses formes qui y entre (les touches
    // et les couleurs des reperes ; les touches seules ; les touches en bref) ; le tout, en infobulle sur elle (la
    // partie "help"). Avant, une seule phrase, coupee au milieu ("... rouge : alarme - co...").
    c.helpTip = "Les touches : Espace, pause ou lecture ; \xE2\x86\x90 \xE2\x86\x92, 10 s en arri\xC3\xA8re ou en avant ; D\xC3\xA9" "but, Fin ; "
                "+ \xE2\x88\x92, la vitesse ; S, les sons ; \xC3\x89" "chap quitte la relecture.\n"
                "Les rep\xC3\xA8res de la barre : rouge, une alarme ; couleur d'accent, un clic ; gris, une autre vue "
                "(survoler un rep\xC3\xA8re le d\xC3\xA9" "crit).";
    std::string help;
    if (!hover.empty()) help = hover;
    else if (!info.note.empty()) help = "\xE2\x9A\xA0 " + info.note;
    else help = replayLegend(r, f.smallUi, right - x);
    if (!info.note.empty()) c.helpTip = "\xE2\x9A\xA0 " + info.note + "\n" + c.helpTip;
    if (right - x > 4.f) c.parts.emplace_back("help", gfx::Rect{x, rowY, right - x, rowH});
    if (!help.empty())
        fitText(r, {x, rowY + (rowH - r.lineHeight(f.smallUi)) / 2.f}, help, f.smallUi,
                !hover.empty() ? col.text : !info.note.empty() ? col.warning : col.textMuted, right - x);
    x = right + 6.f;
    button("quit", quitBtnW, false, false, label("Quitter"));
    return c;
}

void paintReplayPointer(gfx::IRenderer& r, const ui::Theme& theme, gfx::Point at, bool pressed) {
    // Une fleche blanche bordee de sombre, a moitie transparente : le pointeur d'alors, pas celui d'ici.
    const std::vector<gfx::Point> arrow{{at.x, at.y},           {at.x, at.y + 17.f},        {at.x + 4.5f, at.y + 13.f},
                                        {at.x + 7.5f, at.y + 19.5f}, {at.x + 10.f, at.y + 18.5f}, {at.x + 7.f, at.y + 12.f},
                                        {at.x + 12.5f, at.y + 12.f}};
    ui::shapes::fillPolygon(r, arrow, gfx::Color{255, 255, 255, 215});
    ui::shapes::strokePolyline(r, arrow, true, gfx::Color{16, 18, 22, 230}, 1.2f);
    if (pressed) {
        const gfx::Color a = theme.color.accent;
        r.fillRoundedRect({at.x - 6.f, at.y - 6.f, 12.f, 12.f}, gfx::Color{a.r, a.g, a.b, 150}, 6.f);
    }
}

void paintReplayRipple(gfx::IRenderer& r, const ui::Theme& theme, gfx::Point at, float age) {
    const float k = std::clamp(age, 0.f, 1.f);
    const gfx::Color a = theme.color.accent;
    const float rad = 8.f + 26.f * k;
    const auto ring = ui::shapes::ellipse(at, rad, rad, 32);
    ui::shapes::strokePolyline(r, ring, true, gfx::Color{a.r, a.g, a.b, static_cast<std::uint8_t>(230.f * (1.f - k))}, 2.5f);
    r.fillRoundedRect({at.x - 5.f, at.y - 5.f, 10.f, 10.f}, gfx::Color{a.r, a.g, a.b, static_cast<std::uint8_t>(200.f * (1.f - k))}, 5.f);
}

} // namespace app
