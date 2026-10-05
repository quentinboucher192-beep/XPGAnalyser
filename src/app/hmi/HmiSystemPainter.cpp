#include "HmiSystemPainter.hpp"

#include "HmiIcons.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {

namespace {

namespace shapes = ui::shapes;

const gfx::Color kText = gfx::Color::rgb(0xE6EAF0), kMuted = gfx::Color::rgb(0x9AA6B8), kDim = gfx::Color::rgb(0x6B7686);
const gfx::Color kField = gfx::Color::rgb(0x141820), kButton = gfx::Color::rgb(0x2A313C), kButtonEdge = gfx::Color::rgb(0x3A4556);
const gfx::Color kOrange = gfx::Color::rgb(0xF2994A), kRed = gfx::Color::rgb(0xE5534B), kGreen = gfx::Color::rgb(0x2ECC71);

gfx::FontId fontOf(double px) { return gfx::FontId{static_cast<std::uint16_t>(std::clamp(px, 8.0, 40.0))}; }
gfx::Color faded(gfx::Color c, bool on) { return on ? c : c.withAlpha(static_cast<std::uint8_t>(c.a * 0.42f)); }

struct Painter {
    gfx::IRenderer& r;
    const ui::Theme& th;
    float ox, oy;

    [[nodiscard]] gfx::Rect at(const hmi::Box& b) const {
        return {ox + static_cast<float>(b.x), oy + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    }
    // Un texte dans une case : 0 a gauche, 1 au centre, 2 a droite ; s'il deborde,
    // 1.9 : coupe et termine par ... (jamais au milieu d'un caractere).
    void text(const gfx::Rect& box, std::string_view s, gfx::FontId f, gfx::Color c, int align = 0) const {
        if (box.w <= 2.f || s.empty()) return;
        std::string cut;
        std::string_view shown = s.substr(0, std::min(s.size(), r.fitCharacters(s, f, box.w)));
        if (shown.size() < s.size()) {
            static constexpr std::string_view kDots = "\xE2\x80\xA6";
            std::size_t n = std::min(s.size(), r.fitCharacters(s, f, std::max(0.f, box.w - r.measure(kDots, f).width)));
            while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
            while (n > 0 && s[n - 1] == ' ') --n;
            cut = std::string(s.substr(0, n)) + std::string(kDots);
            shown = cut;
        }
        const float w = r.measure(shown, f).width;
        const float x = align == 0 ? box.x : align == 1 ? box.x + (box.w - w) / 2.f : box.right() - w;
        r.drawText({x, box.y + (box.h - r.lineHeight(f)) / 2.f}, shown, f, c);
    }
    void button(const gfx::Rect& b, gfx::Color fill, bool on) const {
        r.fillRoundedRect(b, faded(fill, on), std::min(5.f, b.h / 3.f));
        r.strokeRect(b, faded(kButtonEdge, on), 1.f);
    }
    // Un triangle : vers le haut (0), le bas (1), la gauche (2), la droite (3).
    void arrow(const gfx::Rect& b, int dir, gfx::Color c) const {
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.24f;
        std::vector<gfx::Point> p;
        switch (dir) {
            case 0: p = {{cx - s, cy + s * 0.5f}, {cx + s, cy + s * 0.5f}, {cx, cy - s * 0.6f}}; break;
            case 1: p = {{cx - s, cy - s * 0.5f}, {cx + s, cy - s * 0.5f}, {cx, cy + s * 0.6f}}; break;
            case 2: p = {{cx + s * 0.5f, cy - s}, {cx + s * 0.5f, cy + s}, {cx - s * 0.6f, cy}}; break;
            default: p = {{cx - s * 0.5f, cy - s}, {cx - s * 0.5f, cy + s}, {cx + s * 0.6f, cy}}; break;
        }
        shapes::fillPolygon(r, p, c);
    }
    void plusMinus(const gfx::Rect& b, bool plus, gfx::Color c) const {
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.22f;
        const float t = std::max(1.5f, std::min(b.w, b.h) * 0.08f);
        r.line({cx - s, cy}, {cx + s, cy}, c, t);
        if (plus) r.line({cx, cy - s}, {cx, cy + s}, c, t);
    }
};

// 1.9 : le violet, reserve au simule (la page Simulation, ce qui est lu sur un esclave).
const gfx::Color kViolet = gfx::Color::rgb(0x8B7CF6), kVioletText = gfx::Color::rgb(0xB4A9FF), kVioletDark = gfx::Color::rgb(0x2A2346);
const gfx::Color kVioletSel = gfx::Color::rgb(0x4A3F8F), kSegOn = gfx::Color::rgb(0x2C5A78), kCardBg = gfx::Color::rgb(0x1F2731);
const gfx::Color kCardEdge = gfx::Color::rgb(0x34404F), kListBg = gfx::Color::rgb(0x171D26), kRule = gfx::Color::rgb(0x2A323E);
const gfx::Color kCheck = gfx::Color::rgb(0x3B8FD9);

gfx::Color toneColor(int tone) {
    switch (tone) {
        case 1: return kOrange;
        case 2: return kRed;
        case 3: return kGreen;
        case 4: return kVioletText;      // 1.9 : lu en simule
        default: return kText;
    }
}

// +00:05:00
std::string offsetText(double seconds) {
    const long long total = std::llround(seconds);
    long long a = total < 0 ? -total : total;
    char b[48];
    if (a >= 86400) std::snprintf(b, sizeof b, "%c%lld j %02lld:%02lld", total < 0 ? '-' : '+', a / 86400, (a % 86400) / 3600, (a / 60) % 60);
    else std::snprintf(b, sizeof b, "%c%02lld:%02lld:%02lld", total < 0 ? '-' : '+', a / 3600, (a / 60) % 60, a % 60);
    return b;
}

void paintSettings(const Painter& p, const hmi::Runtime& rt, const hmi::SystemMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto& s = rt.settings();
    // "Selon le projet (15 min)" : la deconnexion du projet, telle qu'en vigueur.
    const int projectMin = s.autoLogoutMin < 0 ? rt.autoLogoutMinutes() : 0;
    for (std::size_t i = 0; i < l.settings.size(); ++i) {
        const auto& row = l.settings[i];
        const auto* spec = hmi::settingSpec(row.key);
        if (!spec) continue;
        const bool allowed = rt.settingAllowed(row.key);
        const gfx::Rect rowR = p.at(row.row);
        if (i % 2 == 1) p.r.fillRect(rowR, gfx::Color{255, 255, 255, 9});
        // Le libelle ; un cadenas quand le reglage est protege et refuse.
        gfx::Rect label = p.at(row.label);
        if (!allowed) {
            const float g = std::min(16.f, label.h * 0.6f);
            drawHmiGlyph(p.r, HmiGlyph::Lock, {label.x, label.y + (label.h - g) / 2.f, g, g}, kOrange);
            label.x += g + 6.f;
            label.w -= g + 6.f;
        }
        p.text(label, spec->label, f, allowed ? gfx::Color::rgb(0xC8D0DC) : kDim);
        const std::string value = hmi::settingText(s, row.key, projectMin);
        const gfx::Rect v = p.at(row.value);
        if (spec->kind == hmi::SettingKind::Toggle) {
            const gfx::Rect t = p.at(row.toggle);
            const bool on = s.soundOn;
            p.r.fillRoundedRect(t, faded(on ? kGreen : gfx::Color::rgb(0x4A5568), allowed), t.h / 2.f);
            const float k = t.h - 4.f;
            const float kx = on ? t.right() - k - 2.f : t.x + 2.f;
            shapes::fillPolygon(p.r, shapes::ellipse({kx + k / 2.f, t.y + t.h / 2.f}, k / 2.f, k / 2.f, 24), faded(kText, allowed));
            p.text(v, value, f, faded(on ? kText : kMuted, allowed));
            continue;
        }
        const bool percent = spec->kind == hmi::SettingKind::Percent;
        const gfx::Rect minus = p.at(row.minus), plus = p.at(row.plus);
        p.button(minus, kButton, allowed);
        p.button(plus, kButton, allowed);
        if (percent) {
            p.plusMinus(minus, false, faded(kText, allowed));
            p.plusMinus(plus, true, faded(kText, allowed));
        } else {
            p.arrow(minus, 2, faded(kText, allowed));
            p.arrow(plus, 3, faded(kText, allowed));
        }
        p.r.fillRoundedRect(v, kField, 3.f);
        if (percent) {
            const int pc = row.key == "luminosite" ? s.brightness : s.volume;
            const gfx::Color bar = row.key == "luminosite" ? gfx::Color::rgb(0xF2C94C) : p.th.color.accent;
            p.r.fillRoundedRect({v.x + 1.f, v.y + 1.f, std::max(0.f, (v.w - 2.f) * static_cast<float>(pc) / 100.f), v.h - 2.f},
                                faded(bar.withAlpha(150), allowed), 3.f);
        }
        p.r.strokeRect(v, kButtonEdge, 1.f);
        p.text(v, value, f, faded(kText, allowed), 1);
    }

    // ---- la date et l'heure de l'IHM
    if (!l.settings.empty()) {
        const bool allowed = rt.settingAllowed("heure");
        bool pending = false;
        const hmi::DateTime d = rt.systemClock(&pending);
        const gfx::Rect row = p.at(l.clockRow);
        const auto& first = l.settings.front();
        gfx::Rect label{p.ox + static_cast<float>(first.label.x), row.y, static_cast<float>(first.label.w), static_cast<float>(l.rowH)};
        if (!allowed) {
            const float g = std::min(16.f, label.h * 0.6f);
            drawHmiGlyph(p.r, HmiGlyph::Lock, {label.x, label.y + (label.h - g) / 2.f, g, g}, kOrange);
            label.x += g + 6.f;
            label.w -= g + 6.f;
        }
        p.text(label, "Date et heure", f, allowed ? gfx::Color::rgb(0xC8D0DC) : kDim);
        const double offset = s.clockOffset;
        std::string note = "celle du poste";
        gfx::Color noteColor = kMuted;
        if (pending) { note = "\xC3\xA0 appliquer"; noteColor = p.th.color.accent; }
        else if (std::fabs(offset) >= 0.5) { note = "\xC3\xA9" "cart " + offsetText(offset); noteColor = kOrange; }
        p.text({label.x, label.y + label.h * 0.85f, label.w, label.h * 0.8f}, note, small, noteColor);
        const hmi::PickerLayout::Field* prev = nullptr;
        const gfx::FontId big = fontOf(std::min(l.fontSize * 1.25, l.clock.fields.empty() ? 16.0 : l.clock.fields.front().box.h * 0.6));
        for (const auto& fl : l.clock.fields) {
            int value = 0;
            const char* pattern = "%02d";
            if (fl.name == "jour") value = d.day;
            else if (fl.name == "mois") value = d.month;
            else if (fl.name == "annee") { value = d.year; pattern = "%04d"; }
            else if (fl.name == "heure") value = d.hour;
            else if (fl.name == "minute") value = d.minute;
            else value = d.second;
            char b[16];
            std::snprintf(b, sizeof b, pattern, value);
            const gfx::Rect up = p.at(fl.up), down = p.at(fl.down), box = p.at(fl.box);
            p.button({up.x + 1.f, up.y + 1.f, up.w - 2.f, up.h - 2.f}, kButton, allowed);
            p.button({down.x + 1.f, down.y + 1.f, down.w - 2.f, down.h - 2.f}, kButton, allowed);
            p.arrow(up, 0, faded(gfx::Color::rgb(0xC8D0DC), allowed));
            p.arrow(down, 1, faded(gfx::Color::rgb(0xC8D0DC), allowed));
            const gfx::Rect field{box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f};
            p.r.fillRoundedRect(field, kField, 2.f);
            p.r.strokeRect(field, pending ? p.th.color.accent : kButtonEdge, 1.f);
            p.text(field, b, big, faded(kText, allowed), 1);
            if (prev) {
                const bool date = fl.name == "mois" || fl.name == "annee";
                const bool time = fl.name == "minute" || fl.name == "seconde";
                const gfx::Rect pb = p.at(prev->box);
                if (date || time) p.text({pb.right(), box.y, box.x - pb.right(), box.h}, date ? "/" : ":", big, kMuted, 1);
            }
            prev = &fl;
        }
        const gfx::Rect now = p.at(l.clock.now), ok = p.at(l.clock.ok);
        p.button(now, gfx::Color::rgb(0x323A47), allowed);
        p.text(now.inset(4.f, 0.f), "Heure du poste", small, faded(kText, allowed), 1);
        p.button(ok, pending ? p.th.color.accent : gfx::Color::rgb(0x2A4A7A), allowed);
        p.text(ok.inset(4.f, 0.f), pending ? "Appliquer \xE2\x80\xA2" : "Appliquer", small, faded(gfx::Color::rgb(0xFFFFFF), allowed), 1);
    }

    // ---- la maintenance
    if (!l.buttons.empty() && !l.settings.empty()) {
        const gfx::Rect b0 = p.at(l.buttons.front().box);
        const auto& first = l.settings.front();
        const gfx::Rect label{p.ox + static_cast<float>(first.label.x), b0.y, static_cast<float>(first.label.w), b0.h};
        p.text(label, "Maintenance", f, gfx::Color::rgb(0xC8D0DC));
        for (const auto& b : l.buttons) {
            const auto* spec = hmi::maintenanceSpec(b.key);
            if (!spec) continue;
            const bool allowed = rt.settingAllowed(b.key);
            gfx::Rect box = p.at(b.box);
            const gfx::Color fill = b.key == "eteindre" ? gfx::Color::rgb(0x2A4A7A)
                                    : b.key == "redemarrer" ? gfx::Color::rgb(0x5A3A2A) : gfx::Color::rgb(0x323A47);
            p.button(box, fill, allowed);
            gfx::Rect inner = box.inset(5.f, 0.f);
            if (!allowed) {
                const float g = std::min(14.f, box.h * 0.55f);
                drawHmiGlyph(p.r, HmiGlyph::Lock, {inner.x, box.y + (box.h - g) / 2.f, g, g}, kOrange);
                inner.x += g + 3.f;
                inner.w -= g + 3.f;
            }
            p.text(inner, spec->label, small, faded(kText, allowed), 1);
        }
    }
}

void paintDiagnostics(const Painter& p, const hmi::Runtime& rt, const hmi::SystemMenuLayout& l, gfx::FontId f, gfx::FontId small) {
    const auto groups = rt.diagnostics();
    std::size_t maxScroll = 0;
    const auto lines = hmi::diagLines(groups, l, rt.systemScroll(), &maxScroll);
    // Des lignes serrees : le texte a leur mesure.
    f = fontOf(std::min(l.fontSize, l.diagRowH * 0.64));
    const gfx::FontId head = fontOf(std::min(l.fontSize * 1.05, l.diagRowH * 0.7));
    for (const auto& line : lines) {
        const gfx::Rect box = p.at(line.box);
        const auto& g = groups[line.group];
        if (line.row < 0) {
            p.text({box.x + 2.f, box.y, box.w, box.h}, g.title, head, p.th.color.accent);
            p.r.line({box.x, box.bottom() - 2.f}, {box.right(), box.bottom() - 2.f}, p.th.color.accent.withAlpha(90), 1.f);
            continue;
        }
        const auto& row = g.rows[static_cast<std::size_t>(line.row)];
        if (line.row % 2 == 1) p.r.fillRect(box, gfx::Color{255, 255, 255, 7});
        const float split = std::min(box.w * 0.42f, 190.f);
        p.text({box.x + 8.f, box.y, split - 12.f, box.h}, row.label, f, kMuted);
        p.text({box.x + split, box.y, box.w - split - 4.f, box.h}, row.value, f, toneColor(row.tone));
    }
    (void)small;
    if (l.up.w > 0) {
        const gfx::Rect up = p.at(l.up), down = p.at(l.down);
        const bool canUp = rt.systemScroll() > 0 && maxScroll > 0, canDown = rt.systemScroll() < maxScroll;
        p.button(up, kButton, canUp);
        p.button(down, kButton, canDown);
        p.arrow(up, 0, faded(kText, canUp));
        p.arrow(down, 1, faded(kText, canDown));
    }
}

// ================================================ 1.9 : la page Simulation ===
bool visible(const hmi::Box& b) { return b.w > 0 && b.h > 0; }

// Du gras simule : le texte trace deux fois, a un demi-pixel.
void boldText(const Painter& p, const gfx::Rect& box, std::string_view s, gfx::FontId f, gfx::Color c, int align = 0) {
    p.text(box, s, f, c, align);
    p.text({box.x + 0.6f, box.y, box.w, box.h}, s, f, c, align);
}

// Les mots d'un texte en lignes de `width` pixels au plus (au plus `maxLines`).
std::vector<std::string> wrapped(gfx::IRenderer& r, std::string_view s, gfx::FontId f, float width, std::size_t maxLines) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = s.find(' ', i);
        if (j == std::string_view::npos) j = s.size();
        const std::string word(s.substr(i, j - i));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(candidate, f).width > width) {
            out.push_back(line);
            line = word;
            if (out.size() + 1 >= maxLines) {
                // La derniere ligne prend le reste (le dessin la coupe s'il le faut).
                line = std::string(s.substr(i));
                i = s.size();
                break;
            }
        } else {
            line = candidate;
        }
        i = j + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// Une fiole (comme celle de la maquette) dans un carre de 16 unites.
void drawFlask(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float s = std::min(b.w, b.h) / 16.f;
    const float x0 = b.x + (b.w - 16.f * s) / 2.f, y0 = b.y + (b.h - 16.f * s) / 2.f;
    const auto P = [&](float x, float y) { return gfx::Point{x0 + x * s, y0 + y * s}; };
    const float t = std::max(1.f, 1.3f * s);
    shapes::strokePolyline(r, {P(6.5f, 2.f), P(6.5f, 6.f), P(3.f, 12.5f), P(3.6f, 14.f), P(12.4f, 14.f), P(13.f, 12.5f), P(9.5f, 6.f), P(9.5f, 2.f)},
                           false, c, t);
    r.line(P(5.6f, 2.f), P(10.4f, 2.f), c, t);
    shapes::fillPolygon(r, {P(4.6f, 10.f), P(11.4f, 10.f), P(12.6f, 12.6f), P(12.1f, 13.4f), P(3.9f, 13.4f), P(3.4f, 12.6f)}, c.withAlpha(110));
}

// Le cadenas violet d'un esclave lie (une pastille).
void lockBadge(gfx::IRenderer& r, const gfx::Rect& b) {
    r.fillRoundedRect(b, kVioletDark, 3.f);
    shapes::strokePolyline(r, shapes::roundedRect(b.x, b.y, b.w, b.h, 3.f), true, kViolet.withAlpha(200), 1.f);
    drawHmiGlyph(r, HmiGlyph::Lock, b.inset(b.w * 0.18f, b.h * 0.18f), kVioletText);
}

void outline(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c, float radius, float width = 1.f) {
    shapes::strokePolyline(r, shapes::roundedRect(b.x, b.y, b.w, b.h, radius), true, c, width);
}

// Une case de 26 px : cochee (bleue), forcee (orange, un F), vide.
void bigBox(const Painter& p, const gfx::Rect& b, int state, gfx::FontId f) {
    auto& r = p.r;
    if (state == 0) {
        r.fillRoundedRect(b, gfx::Color::rgb(0x10151C), 4.f);
        outline(r, b, gfx::Color::rgb(0x4A5568), 4.f, 2.f);
        return;
    }
    r.fillRoundedRect(b, state == 2 ? kOrange : kCheck, 4.f);
    if (state == 2) {
        boldText(p, b, "F", f, gfx::Color::rgb(0x2B1505), 1);
        return;
    }
    const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, k = b.w / 26.f;
    r.line({cx - 6.f * k, cy}, {cx - 1.5f * k, cy + 5.f * k}, gfx::Color::rgb(0xFFFFFF), 2.6f * k);
    r.line({cx - 1.5f * k, cy + 5.f * k}, {cx + 7.f * k, cy - 5.f * k}, gfx::Color::rgb(0xFFFFFF), 2.6f * k);
}

// Une bascule (34 x 18) : verte en marche.
void toggleSwitch(gfx::IRenderer& r, const gfx::Rect& t, bool on) {
    r.fillRoundedRect(t, on ? gfx::Color::rgb(0x7FA319) : gfx::Color::rgb(0x3A4A52), t.h / 2.f);
    const float k = t.h - 4.f;
    const float kx = on ? t.right() - k - 2.f : t.x + 2.f;
    shapes::fillPolygon(r, shapes::ellipse({kx + k / 2.f, t.y + t.h / 2.f}, k / 2.f, k / 2.f, 24), gfx::Color::rgb(0xFFFFFF));
}

// Un pas a pas : [-] valeur [+] (ou < valeur > pour un choix).
void stepper(const Painter& p, const hmi::Box& minus, const hmi::Box& value, const hmi::Box& plus, std::string_view text, gfx::FontId f,
             bool arrows, bool on = true) {
    if (!visible(value)) return;
    const gfx::Rect a = p.at(minus), v = p.at(value), b = p.at(plus);
    const gfx::Rect all{a.x, a.y, b.right() - a.x, a.h};
    p.r.fillRoundedRect(all, kField, 4.f);
    outline(p.r, all, faded(kButtonEdge, on), 4.f);
    p.r.line({a.right(), a.y + 3.f}, {a.right(), a.bottom() - 3.f}, faded(kButtonEdge, on), 1.f);
    p.r.line({b.x, b.y + 3.f}, {b.x, b.bottom() - 3.f}, faded(kButtonEdge, on), 1.f);
    if (arrows) {
        p.arrow(a, 2, faded(kMuted, on));
        p.arrow(b, 3, faded(kMuted, on));
    } else {
        p.plusMinus(a, false, faded(kText, on));
        p.plusMinus(b, true, faded(kText, on));
    }
    p.text(v.inset(2.f, 0.f), text, f, faded(kText, on), 1);
}

// Un bouton du bas de la page.
void simButton(const Painter& p, const hmi::Box& box, std::string_view label, gfx::FontId f, bool on = true, bool violet = false) {
    if (!visible(box)) return;
    const gfx::Rect b = p.at(box);
    p.r.fillRoundedRect(b, faded(violet ? gfx::Color::rgb(0x23264F) : gfx::Color::rgb(0x223040), on), 6.f);
    outline(p.r, b, faded(violet ? gfx::Color::rgb(0x6C71C4) : gfx::Color::rgb(0x3A5566), on), 6.f);
    p.text(b.inset(6.f, 0.f), label, f, faded(violet ? gfx::Color::rgb(0xB3B6F5) : kText, on), 1);
}

// "1 450", "12,6", "TRUE" : la valeur en clair (a la francaise).
std::string pageNumber(double v) {
    char b[48];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(std::llround(v)));
    else std::snprintf(b, sizeof b, "%.6g", v);
    std::string s = b;
    if (s.find('e') != std::string::npos) return s;
    std::replace(s.begin(), s.end(), '.', ',');
    const std::size_t start = (!s.empty() && s[0] == '-') ? 1 : 0;
    std::size_t end = s.find(',');
    if (end == std::string::npos) end = s.size();
    for (std::size_t i = end; i > start + 3; i -= 3) s.insert(i - 3, " ");
    return s;
}

std::string periodText(double s) { return pageNumber(std::round(s * 10.0) / 10.0) + " s"; }

void paintLockedPage(const Painter& p, const hmi::SimPageLayout& s, gfx::FontId f, gfx::FontId small) {
    auto& r = p.r;
    const gfx::Rect card = p.at(s.lockCard);
    r.fillRoundedRect(card, kCardBg, 8.f);
    outline(r, card, gfx::Color::rgb(0x2F5560), 8.f);
    const gfx::Rect icon = p.at(s.lockIcon);
    r.fillRoundedRect(icon, kVioletDark, icon.w * 0.22f);
    outline(r, icon, kViolet.withAlpha(170), icon.w * 0.22f);
    drawHmiGlyph(r, HmiGlyph::Lock, icon.inset(icon.w * 0.24f, icon.h * 0.24f), kVioletText);
    const float k = static_cast<float>(s.lockCard.h / 250.0);
    const gfx::FontId head = fontOf(std::max(11.0, 16.0 * k)), body = fontOf(std::max(9.0, 12.5 * k));
    float y = icon.bottom() + 10.f * k;
    const float lh = r.lineHeight(head);
    boldText(p, {card.x + 10.f, y, card.w - 20.f, lh}, "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 un administrateur", head, kText, 1);
    y += lh + 4.f * k;
    const std::string say = "La page Simulation montre les esclaves simul\xC3\xA9s et peut faire lire un \xC3\xA9quipement sur son esclave : "
                            "il faut la permission Administrer. Connecte-toi avec un compte administrateur.";
    const float bl = r.lineHeight(body);
    const gfx::Rect button = p.at(s.connect);
    for (const auto& line : wrapped(r, say, body, card.w - 48.f, 4)) {
        if (y + bl > button.y - 4.f) break;
        p.text({card.x + 24.f, y, card.w - 48.f, bl}, line, body, kMuted, 1);
        y += bl + 1.f;
    }
    r.fillRoundedRect(button, gfx::Color::rgb(0x1A4F80), 6.f);
    outline(r, button, gfx::Color::rgb(0x2A7FBF), 6.f);
    const float g = std::min(16.f, button.h * 0.5f);
    const std::string label = "Se connecter\xE2\x80\xA6";
    const float tw = r.measure(label, f).width;
    const float x0 = button.x + (button.w - tw - g - 8.f) / 2.f;
    drawHmiGlyph(r, HmiGlyph::User, {x0, button.y + (button.h - g) / 2.f, g, g}, kText);
    p.text({x0 + g + 8.f, button.y, button.right() - x0 - g - 8.f, button.h}, label, f, kText);
    (void)small;
}

void paintSimulation(const Painter& p, const hmi::Runtime& rt, const hmi::SystemMenuLayout& l, const std::vector<hmi::SimSlave>& slaves,
                     gfx::FontId small) {
    const auto& s = l.sim;
    auto& r = p.r;
    const double fs = s.fontSize;
    const gfx::FontId f = fontOf(fs), tiny = fontOf(fs * 0.86), caps = fontOf(fs * 0.8), big = fontOf(fs * 1.3), name = fontOf(fs * 1.12);
    if (s.locked) {
        paintLockedPage(p, s, f, small);
        return;
    }
    // ---- a gauche : les esclaves
    const gfx::Rect list = p.at(s.list);
    r.fillRect(list, kListBg);
    r.line({list.right(), list.y}, {list.right(), list.bottom()}, kRule, 1.f);
    std::size_t running = 0;
    for (const auto& sl : slaves) running += sl.running ? 1 : 0;
    {
        const gfx::Rect h = p.at(s.listHead);
        boldText(p, h, "ESCLAVES SIMUL\xC3\x89S (" + std::to_string(slaves.size()) + ")", caps, kMuted);
        boldText(p, h, std::to_string(running) + " EN MARCHE", caps, kMuted, 2);
    }
    const std::size_t chosen = rt.simChosenIndex(slaves);
    if (slaves.empty()) {
        const gfx::Rect at{list.x + 12.f, list.y + 40.f, list.w - 24.f, 60.f};
        float y = at.y;
        for (const auto& line : wrapped(r, "Aucun esclave simul\xC3\xA9 : coche \xC2\xAB Cloner en esclave simul\xC3\xA9 \xC2\xBB dans la fiche d'un "
                                           "\xC3\xA9quipement Modbus (Configuration \xE2\x80\xBA \xC3\x89quipements).", f, at.w, 5)) {
            p.text({at.x, y, at.w, r.lineHeight(f)}, line, f, kMuted);
            y += r.lineHeight(f) + 2.f;
        }
    }
    for (const auto& card : s.cards) {
        if (card.index >= slaves.size()) continue;
        const auto& sl = slaves[card.index];
        const bool on = card.index == chosen;
        const gfx::Rect b = p.at(card.box);
        r.fillRoundedRect(b, on ? kVioletDark : kCardBg, 6.f);
        outline(r, b, on ? kViolet : kCardEdge, 6.f, on ? 1.6f : 1.f);
        gfx::Rect nm = p.at(card.name);
        const float g = std::min(18.f, nm.h);
        if (sl.linked) {
            lockBadge(r, {nm.x, nm.y + (nm.h - g) / 2.f, g, g});
            nm.x += g + 6.f;
            nm.w -= g + 6.f;
        }
        drawFlask(r, {nm.x, nm.y + (nm.h - 16.f) / 2.f, 16.f, 16.f}, on ? kVioletText : kMuted);
        nm.x += 22.f;
        nm.w -= 22.f;
        boldText(p, nm, sl.equipment, name, sl.enabled ? kText : kDim);
        const std::string kind = sl.linked ? "esclave li\xC3\xA9" : "seulement simul\xC3\xA9";
        p.text(p.at(card.sub), kind + " \xC2\xB7 " + sl.state, tiny, kMuted);
        if (sl.linked) {
            const int mode = sl.mode == "esclave" ? 1 : sl.mode == "auto" ? 2 : 0;
            const bool can = !sl.modeLocked;
            static const char* const kLabels[3] = {"Le vrai", "L'esclave", "Auto"};
            const gfx::Rect all{p.at(card.seg[0]).x, p.at(card.seg[0]).y, p.at(card.seg[2]).right() - p.at(card.seg[0]).x, p.at(card.seg[0]).h};
            r.fillRoundedRect(all, gfx::Color::rgb(0x141A22), 5.f);
            for (int k = 0; k < 3; ++k) {
                const gfx::Rect sb = p.at(card.seg[k]);
                if (k == mode) r.fillRect(sb.inset(1.f, 1.f), faded(k == 1 ? kVioletSel : kSegOn, can));
                if (k > 0) r.line({sb.x, sb.y + 1.f}, {sb.x, sb.bottom() - 1.f}, gfx::Color::rgb(0x2F4552), 1.f);
                if (k == mode) boldText(p, sb, kLabels[k], tiny, faded(gfx::Color::rgb(0xFFFFFF), can), 1);
                else p.text(sb, kLabels[k], tiny, faded(kMuted, can), 1);
            }
            outline(r, all, faded(gfx::Color::rgb(0x2F6470), can), 5.f);
        }
        if (visible(card.why)) {
            // Violet : ce que lit l'IHM ; gris : pourquoi le segment ne se change pas ici.
            const std::string why = hmi::Runtime::simCardWhy(sl);
            if (!why.empty()) p.text(p.at(card.why), why, tiny, kVioletText);
            else p.text(p.at(card.why), sl.modeWhy, tiny, kMuted);
        }
    }
    if (visible(s.listUp)) {
        const gfx::Rect up = p.at(s.listUp), down = p.at(s.listDown);
        const bool canUp = s.listFirst > 0, canDown = s.listFirst < s.listMax;
        p.button(up, kButton, canUp);
        p.button(down, kButton, canDown);
        p.arrow(up, 0, faded(kText, canUp));
        p.arrow(down, 1, faded(kText, canDown));
        const std::size_t last = s.cards.empty() ? 0 : s.cards.back().index + 1;
        p.text({list.x + 12.f, up.y, up.x - list.x - 16.f, up.h}, std::to_string(s.listFirst + 1) + "-" + std::to_string(last) + " sur " + std::to_string(slaves.size()),
               tiny, kMuted);
    }
    boldText(p, p.at(s.allHead), "TOUS", caps, kMuted);
    simButton(p, s.allAnimate, "Tout animer", f, !slaves.empty());
    simButton(p, s.allStop, "Tout arr\xC3\xAAter", f, !slaves.empty());
    simButton(p, s.allUnforce, "D\xC3\xA9" "forcer tout", f, !slaves.empty());

    // ---- a droite : l'esclave choisi
    if (slaves.empty() || chosen >= slaves.size()) return;
    const auto& sl = slaves[chosen];
    {
        gfx::Rect nm = p.at(s.name);
        if (sl.linked) {
            const float g = 18.f;
            lockBadge(r, {nm.x, nm.y + (std::min(nm.h, 44.f) - g) / 2.f + 2.f, g, g});
            nm.x += g + 8.f;
            nm.w -= g + 8.f;
        }
        const auto lines = wrapped(r, sl.name, big, nm.w, 2);
        const float lh = r.lineHeight(big);
        float y = nm.y + (nm.h - lh * static_cast<float>(lines.size())) / 2.f;
        for (const auto& line : lines) {
            boldText(p, {nm.x, y, nm.w, lh}, line, big, kText);
            y += lh;
        }
        p.text(p.at(s.address), sl.port > 0 ? "127.0.0.1:" + std::to_string(sl.port) : std::string("arr\xC3\xAAt\xC3\xA9"), f, kMuted);
        toggleSwitch(r, p.at(s.runningSwitch), sl.running);
        const gfx::Rect rs = p.at(s.runningSwitch), ps = p.at(s.respondsSwitch);
        p.text({rs.right() + 6.f, rs.y - 4.f, p.at(s.running).right() - rs.right() - 6.f, rs.h + 8.f}, "En marche", f, kText);
        toggleSwitch(r, ps, sl.responds);
        p.text({ps.right() + 6.f, ps.y - 4.f, p.at(s.responds).right() - ps.right() - 6.f, ps.h + 8.f}, "R\xC3\xA9pond", f, kText);
        p.text(p.at(s.excLabel), "Exception", f, kText, 2);
        stepper(p, s.excPrev, s.excValue, s.excNext, hmi::simExceptionText(sl.exception), f, true);
    }
    const gfx::Rect right = p.at(s.right);
    r.line({right.x, p.at(s.head).bottom()}, {right.right(), p.at(s.head).bottom()}, kRule, 1.f);
    // Les titres du tableau.
    {
        const auto title = [&](const hmi::Box& b, std::string_view t, int align = 0) {
            if (visible(b)) boldText(p, p.at(b), t, caps, kMuted, align);
        };
        title(s.colAnimate, "ANIMER");
        title(s.colName, s.twoLines ? "VARIABLE \xC2\xB7 MOUVEMENT, ZONE, P\xC3\x89RIODE, FORCER" : "VARIABLE");
        title(s.colKind, "MOUVEMENT");
        title(s.colZone, "ZONE");
        title(s.colPeriod, "P\xC3\x89RIODE");
        title(s.colForce, "FORCER");
        title(s.colValue, "VALEUR", 2);
        const gfx::Rect th = p.at(s.tableHead);
        r.line({th.x, th.bottom()}, {th.right(), th.bottom()}, kRule, 1.f);
    }
    if (sl.values.empty()) {
        const gfx::Rect t = p.at(s.table);
        p.text({t.x + 16.f, t.y + 20.f, t.w - 32.f, 24.f}, "Aucune valeur : lie des variables \xC3\xA0 cet \xC3\xA9quipement (Configuration \xE2\x80\xBA \xC3\x89quipements).",
               f, kMuted);
    }
    for (const auto& row : s.rows) {
        if (row.index >= sl.values.size()) continue;
        const auto& v = sl.values[row.index];
        const gfx::Rect rb = p.at(row.box);
        if (row.index % 2 == 1) r.fillRect(rb, gfx::Color{255, 255, 255, 6});
        r.line({rb.x, rb.bottom()}, {rb.right(), rb.bottom()}, gfx::Color::rgb(0x222A35), 1.f);
        bigBox(p, p.at(row.animate), v.animated ? 1 : 0, f);
        // Le nom, puis l'adresse et ce qu'on sait de la ligne.
        {
            const gfx::Rect nb = p.at(row.name);
            const std::string label = v.variable.empty() ? v.address : v.variable;
            std::string note = v.address;
            const bool written = v.note.find("crite") != std::string::npos;
            if (written || v.unit.empty()) note += " \xC2\xB7 " + (v.boolean && v.note == "bit d'un mot" ? std::string("bit") : v.note);
            if (!v.unit.empty()) note += " \xC2\xB7 " + v.unit;
            const float lh = r.lineHeight(f), th = r.lineHeight(tiny);
            if (s.twoLines) {
                const float cy = nb.y + nb.h / 2.f;
                const float w0 = std::min(nb.w * 0.55f, r.measure(label, f).width + 2.f);
                boldText(p, {nb.x, cy - lh / 2.f, w0, lh}, label, f, kText);
                p.text({nb.x + w0 + 10.f, cy - th / 2.f, nb.w - w0 - 10.f, th}, note, tiny, kMuted);
            } else {
                const auto lines = wrapped(r, note, tiny, nb.w, 2);
                const float total = lh + th * static_cast<float>(lines.size());
                float y = nb.y + (nb.h - total) / 2.f;
                boldText(p, {nb.x, y, nb.w, lh}, label, f, kText);
                y += lh;
                for (const auto& line : lines) {
                    p.text({nb.x, y, nb.w, th}, line, tiny, kMuted);
                    y += th;
                }
            }
        }
        // Le mouvement : < sinus >.
        {
            const gfx::Rect a = p.at(row.kindPrev), b = p.at(row.kindNext);
            const gfx::Rect all{a.x, a.y, b.right() - a.x, a.h};
            r.fillRoundedRect(all, kField, 5.f);
            outline(r, all, kButtonEdge, 5.f);
            p.arrow(a, 2, kMuted);
            p.arrow(b, 3, kMuted);
            std::string kind = v.kindLabel.empty() ? std::string("aucun") : v.kindLabel;
            p.text(p.at(row.kindValue), kind, f, v.animated || kind == "aucun" ? kText : kMuted, 1);
        }
        const auto dash = [&](double x, double w) {
            const gfx::Rect c = p.at(row.kindValue);
            p.text({p.ox + static_cast<float>(x), c.y, static_cast<float>(w), c.h}, "\xE2\x80\x94", f, kDim, s.twoLines ? 0 : 0);
        };
        if (visible(row.zoneMinValue)) {
            stepper(p, row.zoneMinMinus, row.zoneMinValue, row.zoneMinPlus, pageNumber(v.low), f, false, v.animated);
            p.text(p.at(row.zoneTo), "\xC3\xA0", f, kMuted, 1);
            stepper(p, row.zoneMaxMinus, row.zoneMaxValue, row.zoneMaxPlus, pageNumber(v.high), f, false, v.animated);
        } else if (!s.twoLines) {
            dash(s.colZone.x, s.colZone.w);
        }
        if (visible(row.periodValue)) stepper(p, row.periodMinus, row.periodValue, row.periodPlus, periodText(v.period), f, false, v.animated);
        else if (!s.twoLines) dash(s.colPeriod.x + 30, s.colPeriod.w);
        bigBox(p, p.at(row.force), v.forced ? 2 : 0, f);
        if (visible(row.forcedValue)) stepper(p, row.forcedMinus, row.forcedValue, row.forcedPlus, pageNumber(v.forcedNumber), f, false);
        // La valeur en direct.
        {
            std::string shown = v.value.empty() ? std::string("\xE2\x80\x94") : v.boolean ? v.value : pageNumber(v.number);
            boldText(p, p.at(row.value), shown, big, v.value.empty() ? kDim : kText, 2);
        }
    }
    if (visible(s.up)) {
        const gfx::Rect up = p.at(s.up), down = p.at(s.down);
        const bool canUp = s.first > 0, canDown = s.first < s.maxScroll;
        // La piste entre les deux fleches, et ce qui se voit.
        const float tx = up.x + up.w / 2.f - 3.f, ty = up.bottom() + 4.f, th = std::max(4.f, down.y - 4.f - ty);
        r.fillRoundedRect({tx, ty, 6.f, th}, gfx::Color::rgb(0x222A35), 3.f);
        const double n = static_cast<double>(sl.values.size());
        const float thumbH = std::max(10.f, static_cast<float>(th * std::min(1.0, static_cast<double>(s.visible) / std::max(1.0, n))));
        const float thumbY = ty + static_cast<float>((th - thumbH) * (s.maxScroll > 0 ? static_cast<double>(s.first) / static_cast<double>(s.maxScroll) : 0.0));
        r.fillRoundedRect({tx, thumbY, 6.f, thumbH}, kMuted, 3.f);
        p.button(up, kButton, canUp);
        p.button(down, kButton, canDown);
        p.arrow(up, 0, faded(kText, canUp));
        p.arrow(down, 1, faded(kText, canDown));
    }
    // ---- en bas : les boutons de l'esclave choisi
    {
        const gfx::Rect a = p.at(s.actions);
        r.line({a.x, a.y}, {a.right(), a.y}, kRule, 1.f);
        const std::size_t n = sl.values.size();
        std::size_t forced = 0;
        for (const auto& v : sl.values) forced += v.forced ? 1 : 0;
        simButton(p, s.animateSlave, n > 0 ? "Animer les " + std::to_string(n) : std::string("Animer"), f, n > 0);
        simButton(p, s.stopSlave, "Arr\xC3\xAAter", f, n > 0);
        simButton(p, s.unforceSlave, forced > 0 ? "D\xC3\xA9" "forcer les " + std::to_string(forced) : std::string("D\xC3\xA9" "forcer"), f, forced > 0);
        if (visible(s.back))
            simButton(p, s.back, s.back.w >= 200 ? "Revenir au vrai maintenant" : "Revenir au vrai", f, true, true);
    }
}

} // namespace

hmi::SystemMenuLayout paintSystemMenu(gfx::IRenderer& r, const ui::Theme& th, const hmi::Runtime& rt, const gfx::Rect& screen,
                                      const std::vector<hmi::SimSlave>* slaves) {
    const int tab = rt.systemTab();
    std::size_t lines = 0;
    if (tab == 1)
        for (const auto& g : rt.diagnostics()) lines += 1 + g.rows.size();
    // 1.9 : la page Simulation - ses esclaves (ceux de l'ecran, sinon lus ici).
    std::vector<hmi::SimSlave> own;
    if (tab == hmi::kSimulationTab && !slaves && rt.simPageAllowed()) own = rt.simSlaves(true);
    const std::vector<hmi::SimSlave>& sim = slaves ? *slaves : own;
    const hmi::SimPageShape shape = rt.simPageShape(sim);
    const auto l = hmi::systemMenuLayout(screen.w, screen.h, tab, lines, rt.systemScroll(), &shape);
    const Painter p{r, th, screen.x, screen.y};
    const gfx::FontId f = fontOf(l.fontSize), small = fontOf(l.fontSize * 0.86), title = fontOf(l.fontSize * 1.22);
    r.pushClip(screen);
    r.fillRect(screen, gfx::Color{0, 0, 0, 125});
    const gfx::Rect panel = p.at(l.panel);
    r.fillRect({panel.x + 6.f, panel.y + 8.f, panel.w, panel.h}, gfx::Color{0, 0, 0, 100});
    r.fillRoundedRect(panel, gfx::Color::rgb(0x1B222C), 6.f);
    // La barre de titre : l'engrenage, le titre, qui est connecte, la croix.
    const gfx::Rect bar = p.at(l.title);
    r.fillRect(bar, gfx::Color::rgb(0x273142));
    const float g = std::min(22.f, bar.h * 0.56f);
    drawHmiGlyph(r, HmiGlyph::Gear, {bar.x + 12.f, bar.y + (bar.h - g) / 2.f, g, g}, th.color.accent);
    const float titleW = r.measure("Param\xC3\xA8tres syst\xC3\xA8me", title).width;
    p.text({bar.x + g + 22.f, bar.y, bar.w * 0.6f, bar.h}, "Param\xC3\xA8tres syst\xC3\xA8me", title, kText);
    const gfx::Rect close = p.at(l.close);
    {
        std::string who = rt.userLogin().empty() ? std::string("personne n'est connect\xC3\xA9") : "connect\xC3\xA9 : " + rt.userLogin();
        // 1.9 : son groupe et son niveau (la page Simulation demande la permission Administrer).
        if (const auto* u = rt.user(); u && rt.project())
            if (const auto* grp = rt.project()->group(u->group))
                who += " (" + grp->name + ", niveau " + std::to_string(grp->level) + ")";
        const float left = bar.x + g + 34.f + titleW;
        p.text({left, bar.y, std::max(0.f, close.x - left - 10.f), bar.h}, who, small, kMuted, 2);
        const float m = close.h * 0.34f;
        r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, gfx::Color::rgb(0xC8D0DC), 1.8f);
        r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, gfx::Color::rgb(0xC8D0DC), 1.8f);
    }
    // Les onglets (1.9 : Simulation, au cadenas sans la permission Administrer).
    const bool simTab = tab == hmi::kSimulationTab && l.tabCount > hmi::kSimulationTab;
    for (int k = 0; k < l.tabCount; ++k) {
        const gfx::Rect t = p.at(l.tabs[k]);
        const bool on = k == tab;
        const gfx::Color mark = k == hmi::kSimulationTab ? kViolet : th.color.accent;
        if (on) r.fillRoundedRect(t, gfx::Color::rgb(0x2C3646), 4.f);
        if (on) r.fillRect({t.x + 4.f, t.bottom() - 3.f, t.w - 8.f, 3.f}, mark);
        const float gs = std::min(16.f, t.h * 0.55f);
        const gfx::Rect icon{t.x + 10.f, t.y + (t.h - gs) / 2.f, gs, gs};
        if (k == hmi::kSimulationTab && !rt.simPageAllowed()) drawHmiGlyph(r, HmiGlyph::Lock, icon, on ? kVioletText : kMuted);
        else if (k == hmi::kSimulationTab) drawFlask(r, icon, on ? kVioletText : kMuted);
        else drawHmiGlyph(r, k == 0 ? HmiGlyph::Gear : HmiGlyph::Gauge, icon, on ? th.color.accent : kMuted);
        p.text({t.x + gs + 18.f, t.y, t.w - gs - 24.f, t.h}, hmi::systemTabLabel(k), f, on ? kText : kMuted);
    }
    r.line({panel.x + 8.f, p.at(l.tabs[0]).bottom() + 1.f}, {panel.right() - 8.f, p.at(l.tabs[0]).bottom() + 1.f},
           gfx::Color::rgb(0x2E3643), 1.f);
    if (simTab) paintSimulation(p, rt, l, sim, small);
    else if (tab == 0) paintSettings(p, rt, l, f, small);
    else paintDiagnostics(p, rt, l, f, small);
    // La ligne d'etat : ce qui vient de changer, un refus, sinon un conseil.
    const gfx::Rect status = p.at(l.status);
    r.fillRect(status, gfx::Color::rgb(0x151A22));
    std::string msg = rt.systemMessage();
    gfx::Color mc = rt.systemMessageIsError() ? kRed : kGreen;
    if (msg.empty() && simTab) {
        // 1.9 : la page Simulation - qui y a acces, et que ses choix ne vont pas dans le projet.
        const gfx::Rect in = status.inset(12.f, 0.f);
        const bool security = rt.project() && rt.project()->security.enabled;
        if (!rt.simPageAllowed()) {
            p.text(in, "Ctrl+Alt+S ouvre cette page sur le poste ; elle est toujours ferm\xC3\xA9" "e aux comptes sans la permission Administrer.", small,
                   gfx::Color::rgb(0x7F8A9A));
        } else {
            const std::string lead = security ? "R\xC3\xA9serv\xC3\xA9 \xC3\xA0 la permission Administrer." : "S\xC3\xA9" "curit\xC3\xA9 inactive : la page est \xC3\xA0 tous.";
            boldText(p, in, lead, small, kVioletText);
            const float w = r.measure(lead, small).width + 10.f;
            p.text({in.x + w, in.y, std::max(0.f, in.w - w), in.h},
                   "Ces r\xC3\xA9glages durent jusqu'au red\xC3\xA9marrage de l'IHM : ils ne vont pas dans le projet.", small, gfx::Color::rgb(0x7F8A9A));
        }
    } else if (msg.empty()) {
        mc = gfx::Color::rgb(0x7F8A9A);
        if (tab == 1) msg = "En lecture seule : chaque ligne se lit aussi en variable SYS. (Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables syst\xC3\xA8me).";
        else if (rt.permitted("Administrer"))
            msg = "R\xC3\xA9glages du poste : ils restent au red\xC3\xA9marrage de l'IHM et ne vont pas dans le projet.";
        else msg = "Les r\xC3\xA9glages marqu\xC3\xA9s d'un cadenas demandent la permission Administrer.";
    }
    if (!msg.empty()) p.text(status.inset(12.f, 0.f), msg, small, simTab && !rt.systemMessageIsError() ? kVioletText : mc);
    r.strokeRect(panel, th.color.accent, 1.5f);
    r.popClip();
    return l;
}

void paintSleepScreen(gfx::IRenderer& r, const ui::Theme& th, const gfx::Rect& screen) {
    r.fillRect(screen, gfx::Color{0, 0, 0, 255});
    const auto f = th.font.smallUi;
    const std::string hint = "\xC3\x89" "cran en veille : touche-le pour le rallumer";
    const float w = r.measure(hint, f).width;
    r.drawText({screen.x + (screen.w - w) / 2.f, screen.y + screen.h / 2.f - r.lineHeight(f) / 2.f}, hint, f, gfx::Color::rgb(0x2E3643));
}

void paintBrightness(gfx::IRenderer& r, const gfx::Rect& screen, int brightness) {
    if (brightness >= 100) return;
    const float a = std::clamp((100.f - static_cast<float>(brightness)) / 100.f * 0.8f, 0.f, 0.8f);
    r.fillRect(screen, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(a * 255.f)});
}

} // namespace app
