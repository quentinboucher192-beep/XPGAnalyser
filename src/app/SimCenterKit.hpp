// =============================================================================
//  app/SimCenterKit.hpp - lot API 8 : le dessin commun des volets du Centre
// -----------------------------------------------------------------------------
//  Interne aux volets du Centre de simulation (SimCenterOverview.cpp,
//  SimCenterPanes.cpp) : les cartes, les titres, les boutons, le texte coupe
//  ou replie, les couleurs d'un etat - dessines comme ceux des Statistiques.
// =============================================================================
#pragma once

#include "SimJournal.hpp"
#include "SimStatus.hpp"
#include "../ui/Icons.hpp"
#include "../ui/Widget.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace app::simkit {

inline const gfx::FontId kTiny{11};
inline const gfx::FontId kSmall{12};
inline const gfx::FontId kBody{13};
inline const gfx::FontId kValue{19};
inline const gfx::FontId kTitle{18};
inline const char* const kDots = "\xE2\x80\xA6";
inline const char* const kMid = " \xC2\xB7 ";

inline float textWidth(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f) { return ctx.r.measure(s, f).width; }

inline void drawBold(const ui::PaintContext& ctx, gfx::Point at, const std::string& s, gfx::FontId f, gfx::Color c) {
    ctx.r.drawText(at, s, f, c);
    ctx.r.drawText({at.x + 0.6f, at.y}, s, f, c);
}

// Le texte coupe a la largeur, termine par ... ; jamais au milieu d'un caractere.
inline std::string fit(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (textWidth(ctx, s, f) <= w) return s;
    const float dots = textWidth(ctx, kDots, f);
    auto n = ctx.r.fitCharacters(s, f, std::max(0.f, w - dots));
    n = std::min(n, s.size());
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return s.substr(0, n) + kDots;
}

// Mot a mot, au plus `maxLines` lignes ; la derniere finit par ... si le reste
// ne tient pas.
inline std::vector<std::string> wrap(const ui::PaintContext& ctx, const std::string& text, gfx::FontId f, float width, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    bool cut = false;
    while (i < text.size()) {
        auto j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(ctx, candidate, f) > width) {
            lines.push_back(line);
            line = word;
            if (lines.size() == maxLines) {
                cut = true;
                break;
            }
        } else {
            line = candidate;
        }
        i = j + 1;
    }
    if (!cut && !line.empty()) {
        if (lines.size() < maxLines) lines.push_back(line);
        else cut = true;
    }
    if (cut && !lines.empty()) lines.back() = fit(ctx, lines.back() + kDots, f, width);
    for (auto& l : lines) l = fit(ctx, l, f, width);
    return lines;
}

// La meme chose, la premiere ligne plus courte (elle suit un libelle).
inline std::vector<std::string> wrapIndented(const ui::PaintContext& ctx, const std::string& text, gfx::FontId f, float firstWidth,
                                             float width, std::size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    bool cut = false;
    const auto limit = [&] { return lines.empty() ? firstWidth : width; };
    while (i < text.size()) {
        auto j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(ctx, candidate, f) > limit()) {
            lines.push_back(line);
            line = word;
            if (lines.size() == maxLines) {
                cut = true;
                break;
            }
        } else {
            line = candidate;
        }
        i = j + 1;
    }
    if (!cut && !line.empty()) {
        if (lines.size() < maxLines) lines.push_back(line);
        else cut = true;
    }
    if (cut && !lines.empty()) lines.back() = fit(ctx, lines.back() + kDots, f, lines.size() == 1 ? firstWidth : width);
    for (std::size_t k = 0; k < lines.size(); ++k) lines[k] = fit(ctx, lines[k], f, k == 0 ? firstWidth : width);
    return lines;
}

// La couleur d'un etat : gris, vert, orange, rouge, bleu.
inline gfx::Color toneColor(const ui::PaintContext& ctx, simstatus::Tone t) {
    const auto& c = ctx.theme.color;
    switch (t) {
        case simstatus::Tone::Ok:      return c.ok;
        case simstatus::Tone::Warning: return c.warning;
        case simstatus::Tone::Error:   return c.error;
        case simstatus::Tone::Info:    return c.info;
        case simstatus::Tone::Off:     break;
    }
    return c.textMuted;
}

inline gfx::Color severityColor(const ui::PaintContext& ctx, SimSeverity s) {
    const auto& c = ctx.theme.color;
    switch (s) {
        case SimSeverity::Ok:      return c.ok;
        case SimSeverity::Warning: return c.warning;
        case SimSeverity::Error:   return c.error;
        case SimSeverity::Info:    break;
    }
    return c.info;
}

inline ui::Icon sourceIcon(SimSource s) {
    switch (s) {
        case SimSource::Automate:    return ui::Icon::Cpu;
        case SimSource::Ihm:         return ui::Icon::Screen;
        case SimSource::Equipements: return ui::Icon::Network;
        case SimSource::Debogage:    return ui::Icon::Halt;
        case SimSource::Simulation:  break;
    }
    return ui::Icon::Play;
}

inline void card(const ui::PaintContext& ctx, const gfx::Rect& r, bool hot, gfx::Color border) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(r, hot ? c.borderStrong : border, 8.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 7.f);
}
inline void card(const ui::PaintContext& ctx, const gfx::Rect& r, bool hot) { card(ctx, r, hot, ctx.theme.color.border); }

// Le titre d'un bloc : une icone et des capitales, en gris. Rend sa largeur.
inline float caption(const ui::PaintContext& ctx, float x, float y, ui::Icon icon, const std::string& text) {
    const auto col = ctx.theme.color.textMuted;
    ui::drawIcon(ctx.r, icon, {x, y, 13.f, 13.f}, col);
    ctx.r.drawText({x + 19.f, y}, text, kTiny, col);
    return 19.f + textWidth(ctx, text, kTiny);
}

// Un bouton au contour (`primary` : rempli de la couleur `tone`), aligne a
// gauche a `x` (ou a droite a `right` si `rightAligned`). Rend son cadre.
inline gfx::Rect button(const ui::PaintContext& ctx, float x, float cy, const std::string& label, bool hot, bool primary = false,
                        gfx::Color tone = {}, bool rightAligned = false, gfx::FontId font = kSmall, float h = 26.f) {
    const auto& c = ctx.theme.color;
    const float w = textWidth(ctx, label, font) + 24.f;
    const gfx::Rect r{rightAligned ? x - w : x, cy - h * 0.5f, w, h};
    if (primary) {
        ctx.r.fillRoundedRect(r, tone.withAlpha(hot ? 110 : 75), 6.f);
        ctx.r.strokeRect(r, tone, 1.f);
        const auto text = ctx.theme.isDark() ? c.text : ctx.theme.onSurface(tone);
        drawBold(ctx, {r.x + 12.f, r.y + (r.h - ctx.r.lineHeight(font)) * 0.5f}, label, font, text);
    } else {
        ctx.r.fillRoundedRect(r, hot ? c.borderStrong : c.border, 6.f);
        ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, hot ? c.rowAltBg : c.panelBg, 5.f);
        ctx.r.drawText({r.x + 12.f, r.y + (r.h - ctx.r.lineHeight(font)) * 0.5f}, label, font, c.text);
    }
    return r;
}

// Un voyant : un disque, et son halo s'il vit.
inline void led(const ui::PaintContext& ctx, float cx, float cy, float radius, gfx::Color colour, bool halo) {
    if (halo) ctx.r.fillRoundedRect({cx - radius - 4.f, cy - radius - 4.f, 2.f * radius + 8.f, 2.f * radius + 8.f}, colour.withAlpha(55), radius + 4.f);
    ctx.r.fillRoundedRect({cx - radius, cy - radius, 2.f * radius, 2.f * radius}, colour, radius);
}

// Une pastille de texte : fond teinte, texte lisible sur le fond.
inline gfx::Rect pill(const ui::PaintContext& ctx, float right, float y, const std::string& text, gfx::Color tone) {
    const float w = textWidth(ctx, text, kTiny) + 14.f;
    const gfx::Rect r{right - w, y, w, 18.f};
    ctx.r.fillRoundedRect(r, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 9.f);
    ctx.r.drawText({r.x + 7.f, r.y + (r.h - ctx.r.lineHeight(kTiny)) * 0.5f}, text, kTiny, ctx.theme.onSurface(tone));
    return r;
}

// Un trait en pointilles (horizontal ou quelconque).
inline void dashed(const ui::PaintContext& ctx, gfx::Point a, gfx::Point b, gfx::Color colour, float thickness = 1.5f, float dash = 6.f) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.f) return;
    for (float t = 0.f; t < len; t += 2.f * dash) {
        const float t1 = std::min(len, t + dash);
        ctx.r.line({a.x + dx * t / len, a.y + dy * t / len}, {a.x + dx * t1 / len, a.y + dy * t1 / len}, colour, thickness);
    }
}

// Une pointe de fleche pleine, vers `dir` (+1 : vers la droite, -1 : la gauche).
inline void arrowHead(const ui::PaintContext& ctx, float x, float y, float dir, gfx::Color colour) {
    const gfx::Vertex v[3] = {{{x, y}, colour}, {{x - dir * 8.f, y - 5.f}, colour}, {{x - dir * 8.f, y + 5.f}, colour}};
    ctx.r.fillTriangles(v, 3);
}

// Ce que dit une cle "aller a", pour un bouton ou une infobulle.
inline std::string goLabel(const std::string& key) {
    if (key.rfind("ligne:", 0) == 0) {
        const auto rest = key.substr(6);
        const auto colon = rest.rfind(':');
        if (colon == std::string::npos) return rest;
        const auto line = rest.substr(colon + 1);
        return rest.substr(0, colon) + (line.empty() || line == "0" ? std::string{} : ", ligne " + line);
    }
    if (key.rfind("variable:", 0) == 0) return key.substr(9);
    if (key.rfind("vue:", 0) == 0) return "la vue " + key.substr(4);
    if (key == "automate") return "Simulation \xE2\x80\xBA Automate";
    if (key == "ihm") return "Simulation \xE2\x80\xBA IHM";
    if (key == "equipements") return "Simulation \xE2\x80\xBA \xC3\x89quipements";
    if (key == "debogage") return "Simulation \xE2\x80\xBA D\xC3\xA9" "bogage";
    if (key == "forcages") return "Simulation \xE2\x80\xBA For\xC3\xA7" "ages";
    if (key == "courbes") return "Simulation \xE2\x80\xBA Courbes";
    if (key == "journal") return "Simulation \xE2\x80\xBA Journal";
    if (key == "ensemble") return "la vue d'ensemble";
    if (key == "alarmes") return "les alarmes de l'IHM";
    if (key == "bibliotheque") return "la mise \xC3\xA0 jour depuis la biblioth\xC3\xA8que";
    if (key == "relancer") return "relancer depuis z\xC3\xA9ro";
    if (key == "sim.run") return "Simuler";
    if (key == "sim.pause") return "Pause";
    if (key == "sim.step") return "Un cycle";
    if (key == "sim.stop") return "Arr\xC3\xAAter";
    if (key == "forcages:tout-relacher") return "rendre toutes les variables et les cases forc\xC3\xA9" "es au programme";
    if (key.rfind("reconnecter:", 0) == 0) return "reconnecter " + key.substr(12) + " et le tester";
    return key;
}

} // namespace app::simkit
