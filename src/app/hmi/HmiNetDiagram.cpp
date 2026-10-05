// app/hmi/HmiNetDiagram.cpp - le schema du reseau du PC (lot 15).
#include "HmiNetDiagram.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

constexpr float kPad = 24.f;
constexpr float kPcW = 210.f;
constexpr float kGapPcPort = 36.f;
constexpr float kBus = 40.f;           // du port au bus
constexpr float kBusToEquip = 60.f;
constexpr float kEquipH = 68.f;
constexpr float kEquipGap = 14.f;
constexpr float kPillH = 22.f;
constexpr float kLegendH = 34.f;

const gfx::FontId kSmall{13};
const gfx::FontId kBody{16};
const gfx::FontId kBig{18};

// Une couleur par reseau (pas une couleur d'etat : ni rouge, ni ambre, ni vert).
gfx::Color netColor(int i) {
    static const std::uint32_t k[] = {0x3B82F6, 0xA78BFA, 0x2DD4BF, 0xF472B6, 0x818CF8, 0x94A3B8};
    return gfx::Color::rgb(k[static_cast<std::size_t>(std::max(0, i)) % std::size(k)]);
}

gfx::Color alpha(gfx::Color c, int a) {
    c.a = static_cast<std::uint8_t>(std::clamp(a, 0, 255));
    return c;
}

gfx::Color toneColor(const ui::Theme& t, int tone) {
    switch (tone) {
        case 1: return t.color.ok;
        case 2: return t.color.warning;
        case 3: return t.color.error;
        case 4: return t.color.info;
        default: return t.color.textMuted;
    }
}

float textW(gfx::IRenderer& r, std::string_view s, gfx::FontId f) { return r.measure(s, f).width; }

void bold(gfx::IRenderer& r, gfx::Point p, std::string_view s, gfx::FontId f, gfx::Color c) {
    r.drawText(p, s, f, c);
    r.drawText({p.x + 0.7f, p.y}, s, f, c);
}

// Ce qui tient dans `w` pixels, avec des points de suite.
std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (s.empty() || textW(r, s, f) <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(s, f, std::max(0.f, w - textW(r, dots, f)));
    std::size_t cut = std::min(n, s.size());
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;     // pas au milieu d'un caractere
    return s.substr(0, cut) + dots;
}

// Un texte coupe mot a mot a la largeur `w`.
std::vector<std::string> wrap(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(r, trial, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

void circle(gfx::IRenderer& r, float cx, float cy, float radius, gfx::Color c) {
    r.fillRoundedRect({cx - radius, cy - radius, radius * 2, radius * 2}, c, radius);
}

void ring(gfx::IRenderer& r, float cx, float cy, float radius, gfx::Color c, gfx::Color inside) {
    circle(r, cx, cy, radius, c);
    circle(r, cx, cy, radius - 1.4f, inside);
}

// Le rond numerote d'un port.
void numberBadge(gfx::IRenderer& r, float x, float y, int n, gfx::Color c, gfx::Color bg) {
    ring(r, x + 8, y + 9, 8, c, bg);
    const std::string t = std::to_string(n);
    r.drawText({x + 8 - textW(r, t, gfx::FontId{11}) / 2, y + 9 - r.lineHeight(gfx::FontId{11}) / 2}, t, gfx::FontId{11}, c);
}

// Le symbole d'etat d'un equipement : coche, croix, point d'interrogation, vague.
void stateIcon(gfx::IRenderer& r, float cx, float cy, int tone, gfx::Color c) {
    circle(r, cx, cy, 11, alpha(c, 50));
    if (tone == 1) {
        r.line({cx - 5, cy + 0.5f}, {cx - 1.5f, cy + 4}, c, 2.2f);
        r.line({cx - 1.5f, cy + 4}, {cx + 5.5f, cy - 4}, c, 2.2f);
    } else if (tone == 3) {
        r.line({cx - 4.5f, cy - 4.5f}, {cx + 4.5f, cy + 4.5f}, c, 2.2f);
        r.line({cx + 4.5f, cy - 4.5f}, {cx - 4.5f, cy + 4.5f}, c, 2.2f);
    } else if (tone == 2) {
        r.line({cx, cy - 5}, {cx, cy + 1.5f}, c, 2.2f);
        circle(r, cx, cy + 4.5f, 1.3f, c);
    } else if (tone == 4) {
        r.line({cx - 5, cy + 1}, {cx - 2, cy - 2}, c, 2.f);
        r.line({cx - 2, cy - 2}, {cx + 1.5f, cy + 2}, c, 2.f);
        r.line({cx + 1.5f, cy + 2}, {cx + 5, cy - 1.5f}, c, 2.f);
    } else if (tone == 5) {
        r.line({cx - 5, cy}, {cx + 5, cy}, c, 2.2f);
    } else {
        r.drawText({cx - textW(r, "?", kSmall) / 2, cy - r.lineHeight(kSmall) / 2}, "?", kSmall, c);
    }
}

// La prise RJ45 d'un port sur le bord du PC (verte : cable branche).
void jack(gfx::IRenderer& r, float cx, float cy, bool up, gfx::Color on, gfx::Color off, gfx::Color bg) {
    const gfx::Color c = up ? on : off;
    r.fillRoundedRect({cx - 14, cy - 14, 28, 28}, bg, 4);
    r.strokeRect({cx - 14, cy - 14, 28, 28}, c, 2.f);
    r.fillRect({cx - 8, cy - 6, 16, 11}, c);
    r.fillRect({cx - 4, cy + 5, 8, 4}, c);
    r.fillRect({cx - 6, cy - 6, 12, 3}, bg);
}

// Le symbole Wi-Fi : trois arcs et un point.
void wifi(gfx::IRenderer& r, float cx, float cy, gfx::Color c) {
    for (int k = 1; k <= 3; ++k) {
        const float rad = 4.f * static_cast<float>(k);
        gfx::Point prev{};
        for (int i = 0; i <= 12; ++i) {
            const float a = 3.14159265f * (1.25f + 0.5f * static_cast<float>(i) / 12.f);
            const gfx::Point p{cx + rad * std::cos(a), cy + 4 + rad * std::sin(a)};
            if (i) r.line(prev, p, c, 2.f);
            prev = p;
        }
    }
    circle(r, cx, cy + 5, 2.f, c);
}

void dashedRect(gfx::IRenderer& r, gfx::Rect b, gfx::Color c) {
    const float dash = 7, gap = 5;
    for (float x = b.x; x < b.x + b.w; x += dash + gap) {
        const float e = std::min(x + dash, b.x + b.w);
        r.line({x, b.y}, {e, b.y}, c, 1.4f);
        r.line({x, b.y + b.h}, {e, b.y + b.h}, c, 1.4f);
    }
    for (float y = b.y; y < b.y + b.h; y += dash + gap) {
        const float e = std::min(y + dash, b.y + b.h);
        r.line({b.x, y}, {b.x, e}, c, 1.4f);
        r.line({b.x + b.w, y}, {b.x + b.w, e}, c, 1.4f);
    }
}

void dashedLine(gfx::IRenderer& r, gfx::Point a, gfx::Point b, gfx::Color c) {
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (len <= 0) return;
    const float dx = (b.x - a.x) / len, dy = (b.y - a.y) / len;
    for (float t = 0; t < len; t += 8) {
        const float e = std::min(t + 4, len);
        r.line({a.x + dx * t, a.y + dy * t}, {a.x + dx * e, a.y + dy * e}, c, 1.6f);
    }
}

bool inside(const gfx::Rect& r, gfx::Point p) { return r.w > 0 && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

} // namespace

HmiNetDiagram::HmiNetDiagram(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);           // lot 17 : Suppr, Echap (un glisser)
}

namespace {
// Ce qui place le schema : le mode (reel, simule), les ports, les equipements, les
// reseaux, l'encadre Hors reseau, dans l'ordre. Le meme : les zones de clic valent encore.
std::string layoutKey(const NetDiagram& d) {
    std::string k = d.simulated ? "S" : "R";
    k += d.empty.empty() ? "|" : "|vide|";
    for (const auto& p : d.ports) k += p.key + "\x1F";
    k += "|";
    for (const auto& e : d.equips) k += e.key + "\x1F" + std::to_string(e.net) + "\x1F";
    k += "|" + std::to_string(d.nets.size());
    for (const auto& n : d.nets) k += "," + std::to_string(n.port);
    k += "|";
    for (const auto& o : d.outside) k += o.equip + "\x1F" + (o.give.empty() ? "0" : "1") + (o.edit.empty() ? "0" : "1");
    return k;
}
} // namespace

void HmiNetDiagram::setData(NetDiagram d) {
    // Un glisser en cours garde son equipement (la liste peut avoir change d'ordre).
    std::string pressed = pressEquip_ >= 0 && static_cast<std::size_t>(pressEquip_) < data_.equips.size() ? data_.equips[static_cast<std::size_t>(pressEquip_)].key : std::string{};
    const bool sameLayout = layoutKey(data_) == layoutKey(d);
    data_ = std::move(d);
    if (!pressed.empty()) {
        pressEquip_ = -1;
        for (std::size_t i = 0; i < data_.equips.size(); ++i)
            if (data_.equips[i].key == pressed) pressEquip_ = static_cast<int>(i);
        if (pressEquip_ < 0) dragging_ = false;
    }
    // Le meme schema (seulement des etats, des textes qui changent - le
    // rafraichissement de chaque seconde) : les zones de clic valent encore
    // jusqu'au prochain dessin. Lot 18 : les oublier aussi cassait un glisser en
    // cours (le port vise n'etait plus trouve, le lacher ne deplacait rien).
    if (sameLayout) {
        placed_ = false;          // replace au prochain dessin ; d'ici la, les anciennes zones servent
        invalidate();
        return;
    }
    // Un autre schema : ses zones de clic ne valent plus rien - les oublier. Sans
    // cela, le reseau simule sans port (qui ne se place pas) gardait celles des
    // ports du vrai reseau, et un clic la ou etait un port lisait un port absent.
    portRects_.clear();
    equipRects_.clear();
    pillRects_.clear();
    giveRects_.clear();
    editRects_.clear();
    outsideTitleRects_.clear();
    outsideRect_ = {};
    hoverPort_ = hoverEquip_ = hoverButton_ = -1;
    if (dropPort_ >= static_cast<int>(data_.ports.size())) dropPort_ = -1;
    placed_ = false;
    invalidate();
}

ui::SizeHint HmiNetDiagram::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {900.f, 600.f};
    h.minimum = {300.f, 200.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void HmiNetDiagram::place(gfx::IRenderer& r) const {
    const auto b = bounds();
    const float bar = data_.toggle ? 40.f : 0.f;           // lot 17 : Reel | Simule
    const float top = b.y + 12 + bar - scroll_;
    const float x0 = b.x + kPad;
    if (data_.toggle) {
        const float w0 = textW(r, "R\xC3\xA9" "el", kSmall) + 28, w1 = textW(r, "Simul\xC3\xA9", kSmall) + 28;
        toggleRects_[0] = {b.x + kPad, b.y + 8, w0, 26};
        toggleRects_[1] = {b.x + kPad + w0, b.y + 8, w1, 26};
    } else {
        toggleRects_[0] = toggleRects_[1] = gfx::Rect{};
    }
    const float portX = x0 + kPcW + kGapPcPort;
    const float avail = b.w - (portX - b.x) - kPad;
    const float portW = std::clamp(avail * 0.34f, 200.f, 250.f);
    const float busX = portX + portW + kBus;
    const float eqX = busX + kBusToEquip;
    const float eqW = std::clamp(b.x + b.w - kPad - eqX - 30.f, 240.f, 420.f);
    portRects_.assign(data_.ports.size(), gfx::Rect{});
    equipRects_.assign(data_.equips.size(), gfx::Rect{});
    pillRects_.assign(data_.nets.size(), gfx::Rect{});
    giveRects_.assign(data_.outside.size(), gfx::Rect{});
    editRects_.assign(data_.outside.size(), gfx::Rect{});
    outsideTitleRects_.assign(data_.outside.size(), gfx::Rect{});
    const auto portH = [&](const NetDiagram::Port& p) { return p.ip.empty() ? 76.f : (p.mac.empty() ? 112.f : 132.f); };
    float y = top;
    std::vector<bool> placedPort(data_.ports.size(), false);
    // Chaque reseau : son etiquette, ses equipements, son port en face.
    for (std::size_t n = 0; n < data_.nets.size(); ++n) {
        const auto& net = data_.nets[n];
        const float pillW = textW(r, net.label, kSmall) + 24;
        pillRects_[n] = {eqX + (eqW - pillW) / 2 - 40, y, pillW, kPillH};
        y += kPillH + 8;
        const float groupTop = y;
        float ey = y;
        for (std::size_t i = 0; i < data_.equips.size(); ++i) {
            const auto& e = data_.equips[i];
            if (e.simulated || e.net != static_cast<int>(n)) continue;
            equipRects_[i] = {eqX, ey, eqW, kEquipH};
            ey += kEquipH + kEquipGap;
        }
        float groupH = std::max(0.f, ey - groupTop - kEquipGap);
        if (net.port >= 0 && static_cast<std::size_t>(net.port) < data_.ports.size()) {
            const auto& p = data_.ports[static_cast<std::size_t>(net.port)];
            const float h = portH(p);
            portRects_[static_cast<std::size_t>(net.port)] = {portX, groupTop, portW, h};
            placedPort[static_cast<std::size_t>(net.port)] = true;
            groupH = std::max(groupH, h);
        }
        y = groupTop + groupH + 28;
    }
    // Les ports sans equipement, dessous ; l'encadre Hors reseau en face.
    const float restTop = y;
    float py = y;
    for (std::size_t i = 0; i < data_.ports.size(); ++i) {
        if (placedPort[i]) continue;
        const float h = portH(data_.ports[i]);
        portRects_[i] = {portX, py, portW, h};
        py += h + 14;
    }
    float oy = restTop;
    outsideRect_ = {};
    if (!data_.outside.empty()) {
        const float ow = std::min(eqX + eqW + 60 - busX, b.x + b.w - kPad - busX);
        float h = 44;
        for (std::size_t i = 0; i < data_.outside.size(); ++i) {
            const auto& o = data_.outside[i];
            outsideTitleRects_[i] = {busX + 12, oy + h - 3, std::max(40.f, ow - 24), 26};
            h += 24;
            h += static_cast<float>(wrap(r, o.text, kSmall, ow - 36).size()) * 18 + 8;
            if (!o.give.empty()) {
                giveRects_[i] = {busX + 18, oy + h, textW(r, o.give, kSmall) + 26, 28};
                h += 36;
            }
            if (!o.edit.empty()) {
                editRects_[i] = {busX + 18, oy + h, textW(r, o.edit, kSmall) + 26, 28};
                h += 36;
            }
            h += 10;
        }
        outsideRect_ = {busX, oy, ow, h};
        oy += h + 14;
    }
    // Le PC : de haut en bas.
    const float bottom = std::max({py, oy, y, top + 420.f});
    pcRect_ = {x0, top, kPcW, bottom - top};
    contentH_ = bottom - top + kLegendH + 24 + bar;
    placed_ = true;
}

void HmiNetDiagram::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& t = ctx.theme;
    const auto b = bounds();
    r.fillRect(b, t.color.panelBg);
    // Lot 17 : le bouton Reel | Simule (fixe, en haut a gauche).
    const auto drawToggle = [&] {
        if (!data_.toggle) return;
        if (toggleRects_[0].w <= 0) {
            const float w0 = textW(r, "R\xC3\xA9" "el", kSmall) + 28, w1 = textW(r, "Simul\xC3\xA9", kSmall) + 28;
            toggleRects_[0] = {b.x + kPad, b.y + 8, w0, 26};
            toggleRects_[1] = {b.x + kPad + w0, b.y + 8, w1, 26};
        }
        r.fillRect({b.x, b.y, b.w, 40}, t.color.panelBg);
        for (int k = 0; k < 2; ++k) {
            const bool on = (k == 1) == data_.simulated;
            const gfx::Rect tr = toggleRects_[k];
            r.fillRoundedRect(tr, on ? alpha(t.color.accent, 200) : t.color.inputBg, 4);
            r.strokeRect(tr, on ? t.color.accent : t.color.borderStrong, 1.f);
            const std::string label = k == 0 ? std::string("R\xC3\xA9" "el") : std::string("Simul\xC3\xA9");
            r.drawText({tr.x + (tr.w - textW(r, label, kSmall)) / 2, tr.y + (tr.h - r.lineHeight(kSmall)) / 2}, label, kSmall, t.color.text);
        }
        if (!data_.toggleNote.empty())
            r.drawText({toggleRects_[1].x + toggleRects_[1].w + 14, b.y + 8 + (26 - r.lineHeight(kSmall)) / 2},
                       fit(r, data_.toggleNote, kSmall, b.x + b.w - toggleRects_[1].x - toggleRects_[1].w - 30), kSmall, t.color.textMuted);
    };
    if (!data_.empty.empty() || data_.ports.empty()) {
        const std::string msg = data_.empty.empty() ? std::string(data_.simulated ? "Aucun port simul\xC3\xA9 : \xC2\xAB Copier le r\xC3\xA9seau du PC \xC2\xBB ou \xC2\xAB Port simul\xC3\xA9 \xC2\xBB (barre du haut)"
                                                                                               : "Aucun port r\xC3\xA9seau")
                                                    : data_.empty;
        r.drawText({b.x + (b.w - textW(r, msg, kBody)) / 2, b.y + b.h / 2 - 10}, msg, kBody, t.color.textMuted);
        drawToggle();
        return;
    }
    // Le defilement borne a ce qui existe.
    place(r);
    const float maxScroll = std::max(0.f, contentH_ - b.h);
    if (scroll_ > maxScroll) {
        scroll_ = maxScroll;
        place(r);
    }
    r.pushClip(b);
    const gfx::Color card = t.brand.card.a ? t.brand.card : t.color.headerBg;
    const gfx::Color cardBorder = t.brand.cardBorder.a ? t.brand.cardBorder : t.color.border;

    // ---- le PC ---------------------------------------------------------------
    r.fillRoundedRect(pcRect_, alpha(t.color.windowBg, 255), 8);
    r.strokeRect(pcRect_, cardBorder, 1.f);
    {
        const float cx = pcRect_.x + pcRect_.w / 2;
        float y = pcRect_.y + 16;
        // L'ecran : un moniteur sur son pied.
        r.fillRoundedRect({cx - 26, y, 52, 36}, t.color.textMuted, 4);
        r.fillRect({cx - 22, y + 4, 44, 26}, t.color.accent.a ? alpha(t.color.accent, 170) : t.color.info);
        r.fillRect({cx - 4, y + 36, 8, 6}, t.color.textMuted);
        r.fillRoundedRect({cx - 14, y + 42, 28, 4}, t.color.textMuted, 2);
        y += 58;
        const std::string name = fit(r, data_.computer, kBody, pcRect_.w - 16);
        bold(r, {cx - textW(r, name, kBody) / 2, y}, name, kBody, t.color.text);
        y += 22;
        const std::string sys = fit(r, data_.system, kSmall, pcRect_.w - 16);
        r.drawText({cx - textW(r, sys, kSmall) / 2, y}, sys, kSmall, t.color.textMuted);
        y += 26;
        r.fillRect({pcRect_.x, y, pcRect_.w, 1}, cardBorder);
        y += 14;
        // Les equipements simules : dans le PC.
        bool anySim = false;
        for (std::size_t i = 0; i < data_.equips.size(); ++i) {
            const auto& e = data_.equips[i];
            if (!e.simulated) continue;
            if (!anySim) {
                r.drawText({pcRect_.x + 12, y}, "DANS CE PC (SIMUL\xC3\x89S)", gfx::FontId{12}, t.color.textMuted);
                y += 22;
                anySim = true;
            }
            const auto lines = wrap(r, e.line2.empty() ? std::string("sa m\xC3\xA9moire se r\xC3\xA8gle \xC3\xA0 la main") : e.line2, kSmall, pcRect_.w - 44);
            const float h = 48 + static_cast<float>(lines.size()) * 17;
            const gfx::Rect cr{pcRect_.x + 12, y, pcRect_.w - 24, h};
            equipRects_[i] = cr;
            r.fillRoundedRect(cr, card, 6);
            const gfx::Color edge = e.selected ? t.color.accent : (static_cast<int>(i) == hoverEquip_ ? t.color.text : t.color.info);
            r.strokeRect(cr, edge, e.selected ? 2.f : 1.2f);
            bold(r, {cr.x + 10, cr.y + 8}, fit(r, e.name, kBody, cr.w - 20), kBody, t.color.text);
            r.drawText({cr.x + 10, cr.y + 28}, fit(r, e.line1, kSmall, cr.w - 20), kSmall, t.color.info);
            float ly = cr.y + 46;
            for (const auto& l : lines) {
                r.drawText({cr.x + 10, ly}, l, kSmall, t.color.textMuted);
                ly += 17;
            }
            // Le fil vers la prise du PC.
            jack(r, pcRect_.x + pcRect_.w, cr.y + cr.h / 2, true, t.color.info, t.color.textMuted, t.color.panelBg);
            y += h + 10;
        }
        if (anySim) y += 8;
        if (!data_.portsSummary.empty()) {
            r.drawText({pcRect_.x + 12, y}, "PORTS", gfx::FontId{12}, t.color.textMuted);
            y += 22;
            for (const auto& l : data_.portsSummary) {
                r.drawText({pcRect_.x + 12, y}, fit(r, l, kSmall, pcRect_.w - 24), kSmall, t.color.textMuted);
                y += 18;
            }
        }
    }

    // ---- les liens des reseaux (sous les cartes) -------------------------------
    for (std::size_t n = 0; n < data_.nets.size(); ++n) {
        const auto& net = data_.nets[n];
        if (net.port < 0 || static_cast<std::size_t>(net.port) >= portRects_.size()) continue;
        const gfx::Rect pr = portRects_[static_cast<std::size_t>(net.port)];
        const gfx::Color c = netColor(static_cast<int>(n));
        const float busX = pr.x + pr.w + kBus;
        const float py = pr.y + pr.h / 2;
        float lo = py, hi = py;
        for (std::size_t i = 0; i < data_.equips.size(); ++i) {
            if (data_.equips[i].simulated || data_.equips[i].net != static_cast<int>(n) || equipRects_[i].w <= 0) continue;
            const float ey = equipRects_[i].y + equipRects_[i].h / 2;
            lo = std::min(lo, ey);
            hi = std::max(hi, ey);
            r.line({busX, ey}, {equipRects_[i].x, ey}, c, 2.5f);
        }
        r.line({pr.x + pr.w, py}, {busX, py}, c, 2.5f);
        if (hi > lo) r.line({busX, lo}, {busX, hi}, c, 2.5f);
    }

    // ---- les ports -------------------------------------------------------------
    for (std::size_t i = 0; i < data_.ports.size(); ++i) {
        const auto& p = data_.ports[i];
        const gfx::Rect pr = portRects_[i];
        if (pr.w <= 0) continue;
        const float cy = pr.y + pr.h / 2;
        // Le fil du PC au port.
        const float jx = pcRect_.x + pcRect_.w;
        if (p.wifi) {
            wifi(r, jx, cy - 4, p.up ? t.color.text : t.color.textMuted);
            dashedLine(r, {jx + 16, cy}, {pr.x, cy}, t.color.textMuted);
        } else {
            if (p.up) r.line({jx + 14, cy}, {pr.x, cy}, p.net >= 0 ? netColor(p.net) : t.color.ok, 2.f);
            else dashedLine(r, {jx + 14, cy}, {pr.x, cy}, t.color.textMuted);
            jack(r, jx, cy, p.up, t.color.ok, t.color.textMuted, t.color.panelBg);
        }
        const bool dim = !p.up;
        r.fillRoundedRect(pr, card, 8);
        const gfx::Color edge = p.selected ? t.color.accent : (static_cast<int>(i) == hoverPort_ ? t.color.text : cardBorder);
        r.strokeRect(pr, edge, p.selected ? 2.f : 1.f);
        if (p.selected) r.strokeRect({pr.x - 3, pr.y - 3, pr.w + 6, pr.h + 6}, alpha(t.color.accent, 70), 2.f);
        const gfx::Color text = dim ? t.color.textMuted : t.color.text;
        float y = pr.y + 10;
        numberBadge(r, pr.x + 12, y, p.number, text, card);
        bold(r, {pr.x + 34, y}, fit(r, p.title, kBody, pr.w - 46), kBody, text);
        y += 22;
        {
            std::string cardName = fit(r, p.card, kSmall, pr.w * 0.5f);
            float x = pr.x + 12;
            r.drawText({x, y}, cardName, kSmall, t.color.textMuted);
            x += textW(r, cardName, kSmall);
            r.drawText({x, y}, " \xC2\xB7 ", kSmall, t.color.textMuted);
            x += textW(r, " \xC2\xB7 ", kSmall);
            circle(r, x + 5, y + r.lineHeight(kSmall) / 2, 4, p.up ? t.color.ok : t.color.textMuted);
            x += 13;
            r.drawText({x, y}, fit(r, p.link, kSmall, pr.x + pr.w - x - 8), kSmall, p.up ? t.color.ok : t.color.textMuted);
        }
        y += 22;
        if (!p.ip.empty()) {
            const gfx::FontId ipFont = p.up ? kBig : kSmall;
            const gfx::Color ipColor = p.pending ? t.color.warning : text;
            r.drawText({pr.x + 12, y}, p.ip, ipFont, ipColor);
            const float ipw = textW(r, p.ip, ipFont);
            r.drawText({pr.x + 18 + ipw, y + (p.up ? 3.f : 0.f)}, "/ " + p.prefix + (p.automatic ? "  (auto)" : ""), kSmall, t.color.textMuted);
            y += p.up ? 28 : 20;
            if (!p.detail.empty()) {
                r.drawText({pr.x + 12, y}, fit(r, p.detail, kSmall, pr.w - 24), kSmall, t.color.textMuted);
                y += 20;
            }
            if (!p.mac.empty()) r.drawText({pr.x + 12, y}, fit(r, p.mac, kSmall, pr.w - 24), kSmall, t.color.textMuted);
        } else {
            r.drawText({pr.x + 12, y}, "sans adresse IPv4", kSmall, t.color.textMuted);
        }
    }

    // ---- les etiquettes des reseaux ----------------------------------------------
    for (std::size_t n = 0; n < data_.nets.size(); ++n) {
        const gfx::Rect pill = pillRects_[n];
        const gfx::Color c = netColor(static_cast<int>(n));
        r.fillRoundedRect(pill, alpha(c, 40), kPillH / 2);
        r.strokeRect(pill, alpha(c, 160), 1.f);
        r.drawText({pill.x + 12, pill.y + (kPillH - r.lineHeight(kSmall)) / 2}, data_.nets[n].label, kSmall, t.color.text);
    }

    // ---- les equipements --------------------------------------------------------
    for (std::size_t i = 0; i < data_.equips.size(); ++i) {
        const auto& e = data_.equips[i];
        if (e.simulated) continue;
        const gfx::Rect er = equipRects_[i];
        if (er.w <= 0) continue;
        const gfx::Color tc = toneColor(t, e.tone);
        r.fillRoundedRect(er, e.tone == 3 ? alpha(t.color.error, 22) : card, 8);
        const gfx::Color edge = e.selected ? t.color.accent : static_cast<int>(i) == hoverEquip_ ? t.color.text
                                                             : e.tone == 3 ? alpha(t.color.error, 150) : cardBorder;
        r.strokeRect(er, edge, e.selected ? 2.f : 1.f);
        stateIcon(r, er.x + 22, er.y + er.h / 2, e.tone, tc);
        const gfx::Color nameColor = e.tone == 3 ? t.color.error : e.disabled ? t.color.textMuted : t.color.text;
        const std::string name = fit(r, e.name, kBody, er.w - 60 - (e.tag.empty() ? 0.f : textW(r, e.tag, gfx::FontId{12}) + 20));
        bold(r, {er.x + 44, er.y + 9}, name, kBody, nameColor);
        if (!e.tag.empty()) {
            const float x = er.x + 52 + textW(r, name, kBody);
            const gfx::Rect chip{x, er.y + 10, textW(r, e.tag, gfx::FontId{12}) + 14, 18};
            r.fillRoundedRect(chip, alpha(t.color.info, 45), 9);
            r.drawText({chip.x + 7, chip.y + (18 - r.lineHeight(gfx::FontId{12})) / 2}, e.tag, gfx::FontId{12}, t.color.text);
        }
        r.drawText({er.x + 44, er.y + 30}, fit(r, e.line1, kSmall, er.w - 54), kSmall, e.disabled ? t.color.textMuted : t.color.text);
        r.drawText({er.x + 44, er.y + 48}, fit(r, e.line2, kSmall, er.w - 54), kSmall, e.tone == 0 || e.tone == 5 ? t.color.textMuted : tc);
    }

    // ---- hors reseau ---------------------------------------------------------------
    if (outsideRect_.w > 0) {
        const gfx::Rect o = outsideRect_;
        r.fillRoundedRect(o, alpha(t.color.error, 18), 8);
        dashedRect(r, o, alpha(t.color.error, 200));
        float y = o.y + 14;
        // Le triangle d'avertissement.
        const float tx = o.x + 18;
        r.line({tx, y + 14}, {tx + 8, y}, t.color.error, 2.f);
        r.line({tx + 8, y}, {tx + 16, y + 14}, t.color.error, 2.f);
        r.line({tx, y + 14}, {tx + 16, y + 14}, t.color.error, 2.f);
        r.line({tx + 8, y + 5}, {tx + 8, y + 9}, t.color.error, 2.f);
        bold(r, {o.x + 42, y - 1}, data_.outsideTitle, kBody, t.color.error);
        y += 30;
        for (std::size_t i = 0; i < data_.outside.size(); ++i) {
            const auto& e = data_.outside[i];
            bold(r, {o.x + 18, y}, e.title, kBody, t.color.text);
            r.drawText({o.x + 26 + textW(r, e.title, kBody), y + 2}, "\xC2\xB7 " + e.line, kSmall, t.color.text);
            y += 24;
            for (const auto& l : wrap(r, e.text, kSmall, o.w - 36)) {
                r.drawText({o.x + 18, y}, l, kSmall, t.color.text);
                y += 18;
            }
            y += 8;
            const auto button = [&](const gfx::Rect& br, const std::string& label, bool hot) {
                r.fillRoundedRect(br, hot ? t.color.selectionBg : t.color.inputBg, 5);
                r.strokeRect(br, hot ? t.color.accent : t.color.borderStrong, 1.f);
                r.drawText({br.x + 13, br.y + (br.h - r.lineHeight(kSmall)) / 2}, label, kSmall, t.color.text);
            };
            if (giveRects_[i].w > 0) {
                button(giveRects_[i], e.give, hoverButton_ == static_cast<int>(i) && hoverGive_);
                y += 36;
            }
            if (editRects_[i].w > 0) {
                button(editRects_[i], e.edit, hoverButton_ == static_cast<int>(i) && !hoverGive_);
                y += 36;
            }
            y += 10;
        }
    }

    // ---- la legende ----------------------------------------------------------------
    {
        float x = portRects_.empty() ? b.x + kPad : std::min(b.x + kPad + kPcW + kGapPcPort, b.x + b.w - 200);
        const float y = std::max(b.y + b.h - 26, pcRect_.y + pcRect_.h + 10);
        const auto dot = [&](gfx::Color c, const std::string& label) {
            circle(r, x + 5, y + r.lineHeight(kSmall) / 2, 4.5f, c);
            r.drawText({x + 13, y}, label, kSmall, t.color.textMuted);
            x += 13 + textW(r, label, kSmall) + 14;
        };
        if (data_.simulated) {
            dot(t.color.info, "en marche");
            dot(t.color.warning, "panne simul\xC3\xA9" "e");
            dot(t.color.textMuted, "arr\xC3\xAAt\xC3\xA9");
        } else {
            dot(t.color.ok, "joignable");
            dot(t.color.error, "injoignable");
            dot(t.color.info, "simul\xC3\xA9");
        }
        if (!data_.hint.empty()) r.drawText({x + 6, y}, fit(r, "\xC2\xB7  " + data_.hint, kSmall, b.x + b.w - x - 12), kSmall, t.color.textMuted);
    }
    // ---- lot 17 : le glisser (le port vise s'allume, la carte suit la souris) ----
    if (dragging_ && pressEquip_ >= 0 && static_cast<std::size_t>(pressEquip_) < equipRects_.size()) {
        const gfx::Rect from = dragOrigin(pressEquip_);
        if (dropPort_ >= 0 && static_cast<std::size_t>(dropPort_) < portRects_.size()) {
            const gfx::Rect pr = portRects_[static_cast<std::size_t>(dropPort_)];
            r.fillRoundedRect(pr, alpha(t.color.accent, 40), 8);
            r.strokeRect({pr.x - 3, pr.y - 3, pr.w + 6, pr.h + 6}, t.color.accent, 2.5f);
            r.drawText({pr.x + 12, pr.y + pr.h + 6}, "L\xC3\xA2" "cher ici : " + data_.ports[static_cast<std::size_t>(dropPort_)].title, kSmall, t.color.accent);
        }
        dashedLine(r, {from.x + from.w / 2, from.y + from.h / 2}, dragAt_, alpha(t.color.accent, 200));
        const gfx::Rect ghost{dragAt_.x - from.w / 2, dragAt_.y - 20, from.w, 40};
        r.fillRoundedRect(ghost, alpha(t.color.headerBg, 230), 8);
        r.strokeRect(ghost, t.color.accent, 1.5f);
        const auto& e = data_.equips[static_cast<std::size_t>(pressEquip_)];
        bold(r, {ghost.x + 12, ghost.y + (40 - r.lineHeight(kBody)) / 2}, fit(r, e.name, kBody, ghost.w - 24), kBody, t.color.text);
    }
    r.popClip();
    drawToggle();
}

// Les zones de clic viennent du dernier placement : bornees par les donnees
// d'aujourd'hui (elles ont pu changer depuis, sans nouveau dessin).
int HmiNetDiagram::portAt(gfx::Point p) const {
    for (std::size_t i = 0; i < portRects_.size() && i < data_.ports.size(); ++i)
        if (inside(portRects_[i], p)) return static_cast<int>(i);
    return -1;
}

int HmiNetDiagram::equipAt(gfx::Point p) const {
    for (std::size_t i = 0; i < equipRects_.size() && i < data_.equips.size(); ++i)
        if (inside(equipRects_[i], p)) return static_cast<int>(i);
    return -1;
}

int HmiNetDiagram::buttonAt(gfx::Point p, bool& give) const {
    for (std::size_t i = 0; i < giveRects_.size() && i < editRects_.size() && i < data_.outside.size(); ++i) {
        if (inside(giveRects_[i], p)) {
            give = true;
            return static_cast<int>(i);
        }
        if (inside(editRects_[i], p)) {
            give = false;
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Ou commence le glisser d'un equipement : sa carte, ou son nom dans l'encadre Hors reseau.
gfx::Rect HmiNetDiagram::dragOrigin(int equip) const {
    if (equip < 0 || static_cast<std::size_t>(equip) >= data_.equips.size()) return {};
    if (static_cast<std::size_t>(equip) < equipRects_.size() && equipRects_[static_cast<std::size_t>(equip)].w > 0) return equipRects_[static_cast<std::size_t>(equip)];
    for (std::size_t i = 0; i < data_.outside.size() && i < outsideTitleRects_.size(); ++i)
        if (data_.outside[i].equip == data_.equips[static_cast<std::size_t>(equip)].key) return outsideTitleRects_[i];
    return {};
}

int HmiNetDiagram::toggleAt(gfx::Point p) const {
    for (int k = 0; k < 2; ++k)
        if (inside(toggleRects_[k], p)) return k;
    return -1;
}

void HmiNetDiagram::cancelDrag() {
    pressEquip_ = -1;
    dragging_ = false;
    dropPort_ = -1;
    invalidate();
}

bool HmiNetDiagram::toggleRect(bool simulated, gfx::Rect& out) const {
    const gfx::Rect r = toggleRects_[simulated ? 1 : 0];
    if (r.w <= 0) return false;
    out = r;
    return true;
}

// Lot 7 : l'equipement survole dit son etat du moment (setData le refait),
// pas celui de l'arrivee de la souris.
std::string HmiNetDiagram::liveTooltip(gfx::Point mouse) const {
    if (hoverEquip_ >= 0 && static_cast<std::size_t>(hoverEquip_) < data_.equips.size())
        return data_.equips[static_cast<std::size_t>(hoverEquip_)].tip;
    return ui::Widget::liveTooltip(mouse);
}

ui::EventResult HmiNetDiagram::onEvent(const ui::InputEvent& ev) {
    // Lot 17 : glisser un equipement sur un port.
    if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && pressEquip_ >= 0) {
        if (!dragging_ && std::hypot(m->pos.x - pressAt_.x, m->pos.y - pressAt_.y) > 6.f) dragging_ = true;
        if (dragging_) {
            dragAt_ = m->pos;
            dropPort_ = portAt(m->pos);
            if (dropPort_ >= 0 && data_.ports[static_cast<std::size_t>(dropPort_)].wifi) dropPort_ = -1;
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && m->button == ui::MouseButton::Left && pressEquip_ >= 0) {
        const bool was = dragging_;
        const int port = dropPort_;
        const int equip = pressEquip_;
        cancelDrag();
        if (was && port >= 0 && static_cast<std::size_t>(equip) < data_.equips.size() && static_cast<std::size_t>(port) < data_.ports.size()) {
            equipmentDropped->emit(data_.equips[static_cast<std::size_t>(equip)].key, data_.ports[static_cast<std::size_t>(port)].key);
            return ui::EventResult::Consumed;
        }
        return was ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && dragging_) {
            cancelDrag();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Delete && k->mods.none()) {
            deleteRequested->emit();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int hp = portAt(m->pos), he = equipAt(m->pos);
        bool give = false;
        const int hb = buttonAt(m->pos, give);
        if (hp != hoverPort_ || he != hoverEquip_ || hb != hoverButton_ || give != hoverGive_) {
            hoverPort_ = hp;
            hoverEquip_ = he;
            hoverButton_ = hb;
            hoverGive_ = give;
            std::string tip;
            if (he >= 0) tip = data_.equips[static_cast<std::size_t>(he)].tip;
            else if (hp >= 0) tip = "Choisir ce port : son adresse, \xC3\xA0 droite";
            setTooltip(tip);
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        const float before = scroll_;
        scroll_ = std::clamp(scroll_ - w->dy * 48.f, 0.f, std::max(0.f, contentH_ - bounds().h));
        if (scroll_ != before) {
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        grabFocus();
        if (const int k = toggleAt(m->pos); k >= 0) {
            if ((k == 1) != data_.simulated) viewToggled->emit(k == 1);
            return ui::EventResult::Consumed;
        }
        bool give = false;
        if (const int i = buttonAt(m->pos, give); i >= 0) {
            (give ? giveClicked : editClicked)->emit(i);
            return ui::EventResult::Consumed;
        }
        if (const int i = portAt(m->pos); i >= 0) {
            portClicked->emit(data_.ports[static_cast<std::size_t>(i)].key);
            return ui::EventResult::Consumed;
        }
        // Lot 17 : un equipement hors reseau se tire par son nom (dans l'encadre).
        for (std::size_t k = 0; k < outsideTitleRects_.size() && k < data_.outside.size(); ++k) {
            if (!inside(outsideTitleRects_[k], m->pos)) continue;
            const std::string key = data_.outside[k].equip;
            pressEquip_ = -1;
            for (std::size_t j = 0; j < data_.equips.size(); ++j)
                if (data_.equips[j].key == key) pressEquip_ = static_cast<int>(j);
            pressAt_ = m->pos;
            dragAt_ = m->pos;
            dragging_ = false;
            equipmentClicked->emit(key);
            pressEquip_ = -1;
            for (std::size_t j = 0; j < data_.equips.size(); ++j)
                if (data_.equips[j].key == key) pressEquip_ = static_cast<int>(j);
            return ui::EventResult::Consumed;
        }
        if (const int i = equipAt(m->pos); i >= 0) {
            const std::string key = data_.equips[static_cast<std::size_t>(i)].key;
            // Lot 17 : on peut le tirer sur un port (pas une carte dans le PC).
            if (!data_.equips[static_cast<std::size_t>(i)].simulated || data_.simulated) {
                pressEquip_ = i;
                pressAt_ = m->pos;
                dragAt_ = m->pos;
                dragging_ = false;
            }
            equipmentClicked->emit(key);
            // La liste a pu etre refaite (le clic la redessine) : retrouver l'equipement.
            if (pressEquip_ >= 0) {
                pressEquip_ = -1;
                for (std::size_t j = 0; j < data_.equips.size(); ++j)
                    if (data_.equips[j].key == key && (!data_.equips[j].simulated || data_.simulated)) pressEquip_ = static_cast<int>(j);
            }
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

bool HmiNetDiagram::portRect(std::string_view key, gfx::Rect& out) const {
    for (std::size_t i = 0; i < data_.ports.size() && i < portRects_.size(); ++i)
        if (data_.ports[i].key == key && portRects_[i].w > 0) {
            out = portRects_[i];
            return true;
        }
    return false;
}

bool HmiNetDiagram::equipmentRect(std::string_view key, gfx::Rect& out) const {
    for (std::size_t i = 0; i < data_.equips.size() && i < equipRects_.size(); ++i)
        if (data_.equips[i].key == key && equipRects_[i].w > 0) {
            out = equipRects_[i];
            return true;
        }
    // Lot 17 : hors reseau, son nom dans l'encadre.
    for (std::size_t i = 0; i < data_.outside.size() && i < outsideTitleRects_.size(); ++i)
        if (data_.outside[i].equip == key && outsideTitleRects_[i].w > 0) {
            out = outsideTitleRects_[i];
            return true;
        }
    return false;
}

bool HmiNetDiagram::buttonRect(int outside, bool give, gfx::Rect& out) const {
    if (outside < 0 || static_cast<std::size_t>(outside) >= giveRects_.size()) return false;
    const gfx::Rect r = give ? giveRects_[static_cast<std::size_t>(outside)] : editRects_[static_cast<std::size_t>(outside)];
    if (r.w <= 0) return false;
    out = r;
    return true;
}

// ================================================================ verifications ===
HmiCheckList::HmiCheckList(std::string id) : ui::Widget(std::move(id)) {}

void HmiCheckList::setItems(std::string title, std::vector<Item> items, std::string note) {
    title_ = std::move(title);
    items_ = std::move(items);
    note_ = std::move(note);
    invalidate();
}

namespace {
// Le nombre de lignes d'un texte coupe a `w` (mesure hors dessin).
int lineCount(const std::string& s, gfx::FontId f, float w) {
    int lines = 1;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            ++lines;
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : s) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    return lines;
}
} // namespace

float HmiCheckList::heightFor(float width) const {
    float h = 30;
    for (const auto& it : items_) h += static_cast<float>(lineCount(it.text, kSmall, width - 40)) * 18 + 8;
    if (!note_.empty()) h += static_cast<float>(lineCount(note_, kSmall, width - 20)) * 18 + 10;
    return h + 6;
}

void HmiCheckList::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& t = ctx.theme;
    const auto b = bounds();
    r.fillRect(b, t.color.panelBg);
    if (items_.empty() && note_.empty()) return;
    r.pushClip(b);
    float y = b.y + 6;
    // L'en-tete, comme une categorie de la grille.
    r.fillRect({b.x, y - 6, b.w, 26}, t.color.headerBg);
    bold(r, {b.x + 26, y - 1}, title_, kSmall, t.color.text);
    r.line({b.x + 10, y + 3}, {b.x + 14, y + 8}, t.color.textMuted, 1.6f);
    r.line({b.x + 14, y + 8}, {b.x + 18, y + 3}, t.color.textMuted, 1.6f);
    y += 28;
    for (const auto& it : items_) {
        const gfx::Color c = it.tone == 1 ? t.color.ok : it.tone == 2 ? t.color.warning : it.tone == 3 ? t.color.error : it.tone == 4 ? t.color.info : t.color.textMuted;
        const float cx = b.x + 16, cy = y + 8;
        if (it.tone == 1) {
            r.line({cx - 5, cy}, {cx - 1.5f, cy + 4}, c, 2.f);
            r.line({cx - 1.5f, cy + 4}, {cx + 5, cy - 4}, c, 2.f);
        } else if (it.tone == 3) {
            r.line({cx - 4.5f, cy - 4.5f}, {cx + 4.5f, cy + 4.5f}, c, 2.f);
            r.line({cx + 4.5f, cy - 4.5f}, {cx - 4.5f, cy + 4.5f}, c, 2.f);
        } else if (it.tone == 2) {
            r.line({cx, cy - 6}, {cx, cy + 2}, c, 2.2f);
            circle(r, cx, cy + 5, 1.3f, c);
        } else if (it.tone == 4) {
            circle(r, cx, cy - 5, 1.3f, c);
            r.line({cx, cy - 1}, {cx, cy + 6}, c, 2.2f);
        } else {
            for (int k = 0; k < 3; ++k) circle(r, cx - 5 + 5.f * static_cast<float>(k), cy + 1, 1.4f, c);
        }
        for (const auto& l : wrap(r, it.text, kSmall, b.w - 40)) {
            r.drawText({b.x + 32, y}, l, kSmall, it.tone == 3 ? t.color.error : t.color.text);
            y += 18;
        }
        y += 8;
    }
    if (!note_.empty()) {
        y += 4;
        for (const auto& l : wrap(r, note_, kSmall, b.w - 20)) {
            r.drawText({b.x + 10, y}, l, kSmall, t.color.textMuted);
            y += 18;
        }
    }
    r.popClip();
}

// ================================================================== scanner ===
HmiScanPage::HmiScanPage(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".table")));
}

void HmiScanPage::setProgress(std::string title, std::string detail, float fraction, bool running) {
    if (title == title_ && detail == detail_ && fraction == fraction_ && running == running_) return;
    title_ = std::move(title);
    detail_ = std::move(detail);
    fraction_ = fraction;
    running_ = running;
    invalidate();
}

void HmiScanPage::onLayout() {
    const auto b = bounds();
    table_->setBounds({b.x, b.y + 58, b.w, std::max(0.f, b.h - 58)});
}

void HmiScanPage::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& t = ctx.theme;
    const auto b = bounds();
    const gfx::Rect head{b.x, b.y, b.w, 58};
    r.fillRect(head, t.color.panelBg);
    r.fillRect({b.x, b.y + 57, b.w, 1}, t.color.border);
    bold(r, {b.x + 14, b.y + 8}, fit(r, title_, kBody, b.w - 28), kBody, t.color.text);
    r.drawText({b.x + 14, b.y + 32}, fit(r, detail_, kSmall, b.w * 0.55f), kSmall, t.color.textMuted);
    if (fraction_ >= 0) {
        const float w = std::min(360.f, b.w * 0.35f);
        const gfx::Rect bar{b.x + b.w - w - 16, b.y + 34, w, 10};
        r.fillRoundedRect(bar, t.color.inputBg, 5);
        r.fillRoundedRect({bar.x, bar.y, std::max(10.f, bar.w * std::clamp(fraction_, 0.f, 1.f)), bar.h}, running_ ? t.color.accent : t.color.ok, 5);
        const std::string pc = std::to_string(static_cast<int>(std::lround(std::clamp(fraction_, 0.f, 1.f) * 100))) + " %";
        r.drawText({bar.x - textW(r, pc, kSmall) - 10, bar.y - 4}, pc, kSmall, t.color.textMuted);
    }
}

} // namespace app
