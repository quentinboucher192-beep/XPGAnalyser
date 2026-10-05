// app/PixelIconEditor.cpp - l'icone du projet et son editeur en pixel art (lot API 6).
#include "PixelIconEditor.hpp"

#include "../menu/MenuManager.hpp"
#include "../project/ProjectIcon.hpp"
#include "../ui/widgets/ColorPalette.hpp"   // 1.10 (chantier Q) : la palette et sa pipette
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace ic = project::icon;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kTiny{11};
const gfx::FontId kLabel{15};

gfx::Color rgbColor(std::uint32_t v, std::uint8_t a = 255) {
    return gfx::Color{static_cast<std::uint8_t>((v >> 16) & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0xFF), static_cast<std::uint8_t>(v & 0xFF), a};
}

bool inside(const gfx::Rect& r, gfx::Point p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

// Une icone dessinee dans `dst` (32 x 32 cases), pixel par pixel.
void paintIcon(gfx::IRenderer& r, const domain::ProjectIcon& icon, const gfx::Rect& dst) {
    if (icon.pixels.size() != 32u * 32u) return;
    const float s = dst.w / 32.f;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const int v = icon.pixels[static_cast<std::size_t>(y * 32 + x)];
            if (!v) continue;
            r.fillRect({dst.x + std::floor(static_cast<float>(x) * s), dst.y + std::floor(static_cast<float>(y) * s), std::ceil(s), std::ceil(s)},
                       rgbColor(ic::colorOf(icon, v)));
        }
}

// La reduction en 16 x 16 (la liste de l'accueil).
void paintIcon16(gfx::IRenderer& r, const domain::ProjectIcon& icon, gfx::Point at) {
    const auto px = ic::rgba16(icon);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const auto* p = &px[static_cast<std::size_t>((y * 16 + x) * 4)];
            if (!p[3]) continue;
            r.fillRect({at.x + static_cast<float>(x), at.y + static_cast<float>(y), 1.f, 1.f}, gfx::Color{p[0], p[1], p[2], p[3]});
        }
}

// Le glyphe d'un outil, dessine au trait.
void toolGlyph(gfx::IRenderer& r, int which, const gfx::Rect& b, gfx::Color c) {
    const float x = b.x, y = b.y, w = b.w, h = b.h;
    const auto L = [&](float x0, float y0, float x1, float y1, float t = 1.6f) { r.line({x + x0 * w, y + y0 * h}, {x + x1 * w, y + y1 * h}, c, t); };
    switch (which) {
        case 0:   // crayon
            L(0.2f, 0.8f, 0.72f, 0.28f); L(0.28f, 0.88f, 0.8f, 0.36f); L(0.72f, 0.28f, 0.8f, 0.36f);
            L(0.2f, 0.8f, 0.14f, 0.94f); L(0.14f, 0.94f, 0.28f, 0.88f);
            break;
        case 1:   // gomme
            L(0.45f, 0.2f, 0.85f, 0.6f); L(0.45f, 0.2f, 0.15f, 0.5f); L(0.15f, 0.5f, 0.45f, 0.8f); L(0.45f, 0.8f, 0.85f, 0.6f);
            L(0.3f, 0.35f, 0.6f, 0.65f); L(0.45f, 0.86f, 0.9f, 0.86f, 1.2f);
            break;
        case 2:   // pot
            L(0.2f, 0.45f, 0.5f, 0.15f); L(0.5f, 0.15f, 0.82f, 0.47f); L(0.82f, 0.47f, 0.5f, 0.8f); L(0.5f, 0.8f, 0.2f, 0.45f);
            L(0.2f, 0.45f, 0.82f, 0.47f); r.fillRoundedRect({x + 0.78f * w, y + 0.62f * h, 0.14f * w, 0.2f * h}, c, 0.07f * w);
            break;
        case 3:   // pipette
            L(0.2f, 0.82f, 0.62f, 0.4f, 2.f); r.fillRoundedRect({x + 0.58f * w, y + 0.14f * h, 0.28f * w, 0.28f * h}, c, 0.14f * w);
            L(0.46f, 0.3f, 0.72f, 0.56f);
            break;
        case 4:   // ligne
            L(0.18f, 0.82f, 0.82f, 0.18f); r.fillRect({x + 0.1f * w, y + 0.74f * h, 0.16f * w, 0.16f * h}, c);
            r.fillRect({x + 0.74f * w, y + 0.1f * h, 0.16f * w, 0.16f * h}, c);
            break;
        case 5:   // rectangle
            r.strokeRect({x + 0.16f * w, y + 0.22f * h, 0.68f * w, 0.56f * h}, c, 1.6f);
            break;
        case 6:   // symetrie
            for (float t = 0.08f; t < 0.92f; t += 0.16f) L(0.5f, t, 0.5f, t + 0.08f, 1.2f);
            L(0.4f, 0.3f, 0.12f, 0.5f); L(0.12f, 0.5f, 0.4f, 0.7f); L(0.4f, 0.7f, 0.4f, 0.3f);
            L(0.6f, 0.3f, 0.88f, 0.5f); L(0.88f, 0.5f, 0.6f, 0.7f); L(0.6f, 0.7f, 0.6f, 0.3f);
            break;
        case 7:   // grille
            r.strokeRect({x + 0.15f * w, y + 0.15f * h, 0.7f * w, 0.7f * h}, c, 1.4f);
            L(0.38f, 0.15f, 0.38f, 0.85f, 1.2f); L(0.62f, 0.15f, 0.62f, 0.85f, 1.2f); L(0.15f, 0.38f, 0.85f, 0.38f, 1.2f); L(0.15f, 0.62f, 0.85f, 0.62f, 1.2f);
            break;
        case 10:  // effacer tout
            for (float t = 0.15f; t < 0.85f; t += 0.14f) { L(t, 0.15f, t + 0.07f, 0.15f, 1.2f); L(t, 0.85f, t + 0.07f, 0.85f, 1.2f); L(0.15f, t, 0.15f, t + 0.07f, 1.2f); L(0.85f, t, 0.85f, t + 0.07f, 1.2f); }
            L(0.33f, 0.33f, 0.67f, 0.67f); L(0.67f, 0.33f, 0.33f, 0.67f);
            break;
        default: break;
    }
}

class IconBody final : public ui::Widget {
public:
    explicit IconBody(PixelIconEditor& ed) : ed_(ed) {}
    std::vector<ui::Button*> buttons;     // Annuler, Appliquer
    ui::InputText*           hex{nullptr};
    // 1.10 (chantier Q) : la palette de la couleur choisie (ouverte par le bouton
    // a droite de son code, posee sur la case du code) et ce bouton.
    ui::ColorPalette*        palette{nullptr};
    ui::Button*              paletteButton{nullptr};

    // Les zones, pour les clics et les scripts.
    gfx::Rect panel{}, canvas{}, closeBox{};
    std::array<gfx::Rect, 2>  tabs{};
    std::array<gfx::Rect, 11> tools{};    // 0..5 outils, 6 symetrie, 7 grille, 8 annuler, 9 retablir, 10 effacer
    std::array<gfx::Rect, 17> swatches{};
    std::vector<gfx::Rect>    presetBoxes;  // la galerie ; le dernier : "Aucune"
    float                     rightX{0.f}, rightW{0.f}, bodyY{0.f};

protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(1180.f, b.w - 40.f), h = std::min(820.f, b.h - 40.f);
        panel = {std::floor(b.x + (b.w - w) * 0.5f), std::floor(b.y + (b.h - h) * 0.5f), w, h};
        bodyY = panel.y + 54.f;
        const float bodyH = h - 54.f - 60.f;
        const float titleW = ui::measureWidth(title(), gfx::FontId{17}) + 30.f;
        tabs[0] = {panel.x + 18.f + titleW, panel.y + 12.f, 110.f, 30.f};
        tabs[1] = {tabs[0].x + tabs[0].w, panel.y + 12.f, 120.f, 30.f};
        closeBox = {panel.x + w - 40.f, panel.y + 14.f, 26.f, 26.f};
        float ty = bodyY + 10.f;
        for (std::size_t i = 0; i < tools.size(); ++i) {
            if (i == 6 || i == 8) ty += 10.f;
            tools[i] = {panel.x + 9.f, ty, 46.f, 40.f};
            ty += 44.f;
        }
        canvas = {panel.x + 64.f + 36.f, std::floor(bodyY + (bodyH - 448.f) * 0.5f), 448.f, 448.f};
        rightX = panel.x + 64.f + 520.f + 20.f;
        rightW = panel.x + w - rightX - 18.f;
        float sx = rightX, sy = bodyY + 36.f;
        for (std::size_t k = 0; k < swatches.size(); ++k) {
            swatches[k] = {sx, sy, 34.f, 34.f};
            sx += 40.f;
            if (k % 9 == 8) { sx = rightX; sy += 40.f; }
        }
        if (hex) hex->setBounds({rightX + 56.f + 60.f, bodyY + 36.f + 92.f + 18.f, 110.f, 28.f});
        if (hex && paletteButton) paletteButton->setBounds({hex->bounds().right() + 6.f, hex->bounds().y, 28.f, 28.f});
        if (hex && palette) palette->setBounds(hex->bounds());
        presetBoxes.clear();
        const float gx = panel.x + 18.f, gw = rightX - 20.f - gx;
        const float cw = std::floor((gw - 3.f * 12.f) / 4.f), ch = std::min(150.f, std::floor((bodyH - 16.f - 3.f * 12.f) / 4.f));
        for (std::size_t i = 0; i <= ic::presets().size(); ++i) {
            const auto col = static_cast<float>(i % 4), row = static_cast<float>(i / 4);
            presetBoxes.push_back({gx + col * (cw + 12.f), bodyY + 16.f + row * (ch + 12.f), cw, ch});
        }
        float bx = panel.x + w - 18.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + h - 46.f, bw, 32.f});
            bx -= 10.f;
        }
    }
    [[nodiscard]] std::string title() const { return "Ic\xC3\xB4ne du projet \xC2\xB7 " + ed_.state().projectName; }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& st = ed_.state();
        auto& r = ctx.r;
        r.fillRect({panel.x + 3.f, panel.y + 4.f, panel.w, panel.h}, gfx::Color{0, 0, 0, 70});
        r.fillRect(panel, c.panelBg);
        r.strokeRect(panel, c.borderStrong, 1.f);
        r.fillRect({panel.x, bodyY - 1.f, panel.w, 1.f}, c.border);
        r.drawText({panel.x + 18.f, panel.y + 16.f}, title(), gfx::FontId{17}, c.text);
        r.drawText({panel.x + 18.6f, panel.y + 16.f}, title(), gfx::FontId{17}, c.text);
        // les onglets
        const char* names[2] = {"Galerie", "Dessiner"};
        for (int i = 0; i < 2; ++i) {
            const bool on = static_cast<int>(st.tab) == i;
            const auto t = tabs[static_cast<std::size_t>(i)];
            r.fillRect(t, on ? c.selectionBg : c.inputBg);
            r.strokeRect(t, c.border, 1.f);
            if (on) r.fillRect({t.x, t.y + t.h - 2.f, t.w, 2.f}, c.accent);
            const float tw = ui::measureWidth(names[i], kLabel);
            r.drawText({t.x + (t.w - tw) * 0.5f, t.y + (t.h - r.lineHeight(kLabel)) * 0.5f}, names[i], kLabel, on ? c.selectionText : c.textMuted);
        }
        r.line({closeBox.x + 7.f, closeBox.y + 7.f}, {closeBox.x + 19.f, closeBox.y + 19.f}, c.textMuted, 1.6f);
        r.line({closeBox.x + 19.f, closeBox.y + 7.f}, {closeBox.x + 7.f, closeBox.y + 19.f}, c.textMuted, 1.6f);
        // le pied
        r.fillRect({panel.x, panel.y + panel.h - 60.f, panel.w, 60.f}, c.headerBg);
        r.fillRect({panel.x, panel.y + panel.h - 60.f, panel.w, 1.f}, c.border);
        const std::string foot = st.tab == PixelIconEditor::Tab::Draw
            ? "Clic droit : gomme \xC2\xB7 Maj : ligne droite \xC2\xB7 Ctrl+Z / Ctrl+Y \xC2\xB7 rang\xC3\xA9" "e dans config/icone.txt"
            : "Un clic choisit le dessin ; double clic : l'ouvrir dans Dessiner \xC2\xB7 rang\xC3\xA9" "e dans config/icone.txt";
        r.drawText({panel.x + 18.f, panel.y + panel.h - 38.f}, foot, kSmall, c.textMuted);
        if (st.tab == PixelIconEditor::Tab::Draw) paintDraw(ctx);
        else paintGallery(ctx);
        paintRight(ctx);
    }

    void paintDraw(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        auto& st = ed_.state();
        auto& r = ctx.r;
        r.fillRect({panel.x, bodyY, 64.f, panel.h - 54.f - 60.f}, c.headerBg);
        r.fillRect({panel.x + 64.f, bodyY, 1.f, panel.h - 54.f - 60.f}, c.border);
        static const char* kKeys[11] = {"P", "E", "F", "I", "L", "R", "M", "G", "", "", ""};
        for (std::size_t i = 0; i < tools.size(); ++i) {
            const auto t = tools[i];
            const bool on = (i < 6 && static_cast<std::size_t>(st.tool) == i) || (i == 6 && st.mirror) || (i == 7 && st.grid);
            if (on) {
                r.fillRoundedRect(t, c.selectionBg, 6.f);
                r.strokeRect(t, c.accent, 1.f);
            } else if (hover_ == static_cast<int>(i)) {
                r.fillRoundedRect(t, c.text.withAlpha(20), 6.f);
            }
            const auto col = on ? c.selectionText : c.text;
            const gfx::Rect g{t.x + 12.f, t.y + 9.f, 22.f, 22.f};
            if (i == 8) ui::drawIcon(r, ui::Icon::Undo, g, st.undo.empty() ? c.textDisabled : col);
            else if (i == 9) ui::drawIcon(r, ui::Icon::Redo, g, st.redo.empty() ? c.textDisabled : col);
            else toolGlyph(r, static_cast<int>(i), g, col);
            if (kKeys[i][0]) r.drawText({t.x + t.w - 10.f, t.y + t.h - 14.f}, kKeys[i], gfx::FontId{10}, on ? c.selectionText : c.textMuted);
        }
        // le damier sous la toile (le transparent), la toile, la grille
        const gfx::Rect area{panel.x + 65.f, bodyY, 519.f, panel.h - 54.f - 60.f};
        r.fillRect(area, c.windowBg);
        const bool dark = ctx.theme.isDark();
        const gfx::Color chk1 = dark ? gfx::Color{52, 56, 64, 255} : gfx::Color{226, 229, 233, 255};
        const gfx::Color chk2 = dark ? gfx::Color{40, 43, 50, 255} : gfx::Color{246, 247, 249, 255};
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                r.fillRect({canvas.x + static_cast<float>(x) * 14.f, canvas.y + static_cast<float>(y) * 14.f, 14.f, 14.f}, (x + y) % 2 ? chk1 : chk2);
        paintIcon(r, st.icon, canvas);
        if (st.grid) {
            const auto gc = dark ? gfx::Color{255, 255, 255, 34} : gfx::Color{0, 0, 0, 38};
            for (int k = 1; k < 32; ++k) {
                r.fillRect({canvas.x + static_cast<float>(k) * 14.f, canvas.y, 1.f, canvas.h}, k == 16 ? gc.withAlpha(90) : gc);
                r.fillRect({canvas.x, canvas.y + static_cast<float>(k) * 14.f, canvas.w, 1.f}, k == 16 ? gc.withAlpha(90) : gc);
            }
        }
        if (st.mirror)
            for (float yy = 0.f; yy < canvas.h; yy += 10.f) r.fillRect({canvas.x + 16.f * 14.f - 1.f, canvas.y + yy, 2.f, 5.f}, c.warning);
        r.strokeRect(canvas, c.borderStrong, 1.f);
        if (st.hoverX >= 0 && st.hoverY >= 0)
            r.strokeRect({canvas.x + static_cast<float>(st.hoverX) * 14.f, canvas.y + static_cast<float>(st.hoverY) * 14.f, 14.f, 14.f}, c.accent, 1.5f);
        std::string pos = "32 \xC3\x97 32";
        if (st.hoverX >= 0) pos = "x " + std::to_string(st.hoverX) + ", y " + std::to_string(st.hoverY) + " \xC2\xB7 "
                                  + std::string(ic::colorName(ic::at(st.icon, st.hoverX, st.hoverY)));
        r.drawText({area.x + 12.f, area.y + area.h - 24.f}, pos, kSmall, c.textMuted);
        r.drawText({area.x + 12.f, area.y + 10.f}, std::string("Outil : ") + PixelIconEditor::toolName(st.tool) + (st.mirror ? " \xC2\xB7 sym\xC3\xA9trie" : ""), kSmall, c.textMuted);
    }

    void paintGallery(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        auto& st = ed_.state();
        auto& r = ctx.r;
        const auto& all = ic::presets();
        for (std::size_t i = 0; i < presetBoxes.size(); ++i) {
            const auto b = presetBoxes[i];
            const bool none = i == all.size();
            const std::string key = none ? "aucune" : all[i].key;
            const bool on = st.preset == key;
            r.fillRoundedRect(b, ctx.theme.brand.card, 8.f);
            r.strokeRect(b, on ? c.accent : hover_ == 100 + static_cast<int>(i) ? c.borderStrong : c.border, on ? 2.f : 1.f);
            const gfx::Rect pic{b.x + (b.w - 64.f) * 0.5f, b.y + 14.f, 64.f, 64.f};
            if (none) {
                r.fillRoundedRect(pic, c.accent, 12.f);
                const float xw = ui::measureWidth("X", gfx::FontId{30});
                r.drawText({pic.x + (64.f - xw) * 0.5f, pic.y + (64.f - r.lineHeight(gfx::FontId{30})) * 0.5f}, "X", gfx::FontId{30}, c.selectionText);
            } else {
                paintIcon(r, ic::preset(all[i].key, st.projectName), pic);
            }
            const std::string label = none ? std::string("Aucune (le X)") : std::string(all[i].label);
            const float lw = ui::measureWidth(label, kSmall);
            r.drawText({b.x + (b.w - lw) * 0.5f, pic.y + 64.f + 10.f}, label, kSmall, c.text);
        }
    }

    void paintRight(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        auto& st = ed_.state();
        auto& r = ctx.r;
        float y = bodyY + 14.f;
        if (st.tab == PixelIconEditor::Tab::Draw) {
            r.drawText({rightX, y}, "COULEURS \xC2\xB7 clic : crayon \xC2\xB7 clic droit : gomme", kTiny, c.textMuted);
            for (std::size_t k = 0; k < swatches.size(); ++k) {
                const auto s = swatches[k];
                if (k == 0) {
                    for (int q = 0; q < 4; ++q)
                        r.fillRect({s.x + static_cast<float>(q % 2) * 17.f, s.y + static_cast<float>(q / 2) * 17.f, 17.f, 17.f},
                                   q % 3 == 0 ? gfx::Color{200, 200, 200, 255} : gfx::Color{255, 255, 255, 255});
                } else {
                    r.fillRect(s, rgbColor(ic::colorOf(st.icon, static_cast<int>(k))));
                }
                r.strokeRect(s, c.border, 1.f);
                if (st.color == k) {
                    r.strokeRect({s.x - 3.f, s.y - 3.f, s.w + 6.f, s.h + 6.f}, c.accent, 2.f);
                }
            }
            y = swatches[16].y + 50.f;
            const gfx::Rect big{rightX, y, 44.f, 44.f};
            if (st.color == 0) r.strokeRect(big, c.border, 1.f);
            else r.fillRect(big, rgbColor(ic::colorOf(st.icon, st.color)));
            r.drawText({rightX + 56.f, y}, std::string(ic::colorName(st.color)), kLabel, c.text);
            if (st.color) r.drawText({rightX + 56.f, y + 26.f}, "#", kSmall, c.textMuted);
            y += 70.f;
        } else {
            r.drawText({rightX, y}, "LE DESSIN CHOISI", kTiny, c.textMuted);
            y += 22.f;
        }
        // l'apercu : 64, 32, 16 ; sombre puis clair
        r.drawText({rightX, y}, "APER\xC3\x87U \xC2\xB7 FOND SOMBRE, FOND CLAIR", kTiny, c.textMuted);
        y += 20.f;
        float x = rightX;
        for (int bg = 0; bg < 2; ++bg)
            for (const float s : {64.f, 32.f, 16.f}) {
                const gfx::Rect box{x, y + (80.f - s - 16.f), s + 16.f, s + 16.f};
                r.fillRoundedRect(box, bg == 0 ? gfx::Color{28, 31, 36, 255} : gfx::Color{238, 240, 243, 255}, 6.f);
                if (s >= 32.f) paintIcon(r, st.icon, {box.x + 8.f, box.y + 8.f, s, s});
                else paintIcon16(r, st.icon, {box.x + 8.f, box.y + 8.f});
                const std::string lab = std::to_string(static_cast<int>(s)) + " px";
                r.drawText({box.x + (box.w - ui::measureWidth(lab, kTiny)) * 0.5f, y + 84.f}, lab, kTiny, c.textMuted);
                x += s + 16.f + 12.f;
            }
        y += 110.f;
        // ou elle apparait
        r.drawText({rightX, y}, "O\xC3\x99 ELLE APPARA\xC3\x8ET", kTiny, c.textMuted);
        y += 20.f;
        const gfx::Rect bar{rightX, y, rightW, 48.f};
        r.fillRoundedRect(bar, c.headerBg, 7.f);
        r.strokeRect(bar, c.border, 1.f);
        paintIcon(r, st.icon, {bar.x + 8.f, bar.y + 8.f, 32.f, 32.f});
        r.drawText({bar.x + 50.f, bar.y + 6.f}, st.projectName, kLabel, c.text);
        r.drawText({bar.x + 50.f, bar.y + 26.f}, "la barre du haut", kTiny, c.textMuted);
        y += 58.f;
        const char* rows[3] = {"", "Station_Pompage", "Banc_Essai"};
        for (int i = 0; i < 3; ++i) {
            const gfx::Rect row{rightX, y, rightW, 28.f};
            r.fillRect(row, i == 0 ? c.selectionBg : c.inputBg);
            r.fillRect({row.x, row.y + row.h - 1.f, row.w, 1.f}, c.gridLine);
            if (i == 0) paintIcon16(r, st.icon, {row.x + 8.f, row.y + 6.f});
            else r.fillRoundedRect({row.x + 8.f, row.y + 6.f, 16.f, 16.f}, i == 1 ? c.accent : c.textMuted, 3.f);
            r.drawText({row.x + 32.f, row.y + (28.f - r.lineHeight(kSmall)) * 0.5f}, i == 0 ? st.projectName : std::string(rows[i]), kSmall,
                       i == 0 ? c.selectionText : c.text);
            if (i == 0) {
                const std::string where = "la liste de l'accueil";
                r.drawText({row.x + row.w - ui::measureWidth(where, kTiny) - 8.f, row.y + 8.f}, where, kTiny, c.selectionText);
            }
            y += 28.f;
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        auto& st = ed_.state();
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            int h = -1;
            for (std::size_t i = 0; i < tools.size(); ++i) if (st.tab == PixelIconEditor::Tab::Draw && inside(tools[i], m->pos)) h = static_cast<int>(i);
            for (std::size_t i = 0; i < presetBoxes.size(); ++i) if (st.tab == PixelIconEditor::Tab::Gallery && inside(presetBoxes[i], m->pos)) h = 100 + static_cast<int>(i);
            const int hx = inside(canvas, m->pos) && st.tab == PixelIconEditor::Tab::Draw ? static_cast<int>((m->pos.x - canvas.x) / 14.f) : -1;
            const int hy = hx >= 0 ? static_cast<int>((m->pos.y - canvas.y) / 14.f) : -1;
            if (h != hover_ || hx != st.hoverX || hy != st.hoverY) {
                hover_ = h;
                st.hoverX = hx;
                st.hoverY = hy;
                if (h >= 0 && h < 100) setTooltip(PixelIconEditor::toolName(static_cast<PixelIconEditor::Tool>(std::min(h, 5))));
                invalidate();
            }
            if (drawing_ && st.tab == PixelIconEditor::Tab::Draw) {
                const int cx = std::clamp(static_cast<int>((m->pos.x - canvas.x) / 14.f), 0, 31);
                const int cy = std::clamp(static_cast<int>((m->pos.y - canvas.y) / 14.f), 0, 31);
                ed_.strokeAt(cx, cy, false, rightDown_, m->mods.shift);
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            if (inside(closeBox, m->pos)) { closeRequested = true; return ui::EventResult::Consumed; }
            for (int i = 0; i < 2; ++i)
                if (inside(tabs[static_cast<std::size_t>(i)], m->pos)) {
                    st.tab = static_cast<PixelIconEditor::Tab>(i);
                    invalidateLayout();
                    invalidate();
                    return ui::EventResult::Consumed;
                }
            if (st.tab == PixelIconEditor::Tab::Gallery) {
                const auto& all = ic::presets();
                for (std::size_t i = 0; i < presetBoxes.size(); ++i)
                    if (inside(presetBoxes[i], m->pos)) {
                        ed_.choosePreset(i == all.size() ? std::string("aucune") : std::string(all[i].key));
                        if (m->clickCount >= 2 && i < all.size()) {
                            st.tab = PixelIconEditor::Tab::Draw;
                            invalidateLayout();
                        }
                        invalidate();
                        return ui::EventResult::Consumed;
                    }
                return ui::EventResult::Ignored;
            }
            for (std::size_t i = 0; i < tools.size(); ++i)
                if (inside(tools[i], m->pos)) {
                    if (i < 6) ed_.setTool(static_cast<PixelIconEditor::Tool>(i));
                    else if (i == 6) st.mirror = !st.mirror;
                    else if (i == 7) st.grid = !st.grid;
                    else if (i == 8) ed_.undoStep();
                    else if (i == 9) ed_.redoStep();
                    else ed_.clearAll();
                    invalidate();
                    return ui::EventResult::Consumed;
                }
            for (std::size_t k = 0; k < swatches.size(); ++k)
                if (inside(swatches[k], m->pos)) {
                    ed_.setColor(static_cast<int>(k));
                    invalidate();
                    return ui::EventResult::Consumed;
                }
            if (inside(canvas, m->pos)) {
                const int cx = std::clamp(static_cast<int>((m->pos.x - canvas.x) / 14.f), 0, 31);
                const int cy = std::clamp(static_cast<int>((m->pos.y - canvas.y) / 14.f), 0, 31);
                rightDown_ = m->button == ui::MouseButton::Right;
                drawing_ = true;
                ed_.strokeAt(cx, cy, true, rightDown_, m->mods.shift);
                invalidate();
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }
        if (std::get_if<ui::MouseUp>(&ev)) {
            if (drawing_) {
                drawing_ = false;
                ed_.strokeEnd();
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

public:
    bool closeRequested{false};

private:
    PixelIconEditor& ed_;
    int              hover_{-1};
    bool             drawing_{false}, rightDown_{false};
};

} // namespace

const char* PixelIconEditor::toolName(Tool t) {
    switch (t) {
        case Tool::Pencil: return "Crayon (P)";
        case Tool::Eraser: return "Gomme (E)";
        case Tool::Bucket: return "Pot de peinture (F)";
        case Tool::Picker: return "Pipette (I)";
        case Tool::Line:   return "Ligne (L)";
        case Tool::Rect:   return "Rectangle (R)";
    }
    return "";
}

PixelIconEditor::PixelIconEditor(domain::ProjectIcon current, std::string projectName) : menu::WidgetMenu("dialog.projectIcon") {
    state_.projectName = projectName.empty() ? std::string("Projet") : std::move(projectName);
    if (current.empty()) {
        // Pas encore d'icone : la galerie d'abord, les initiales du projet choisies.
        state_.icon = ic::preset("initiales", state_.projectName);
        state_.preset = "initiales";
        state_.tab = Tab::Gallery;
    } else {
        state_.icon = std::move(current);
        if (state_.icon.pixels.size() != 32u * 32u) state_.icon = ic::blank();
        state_.tab = Tab::Draw;
    }
}

menu::MenuTraits PixelIconEditor::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string PixelIconEditor::title() const { return "Ic\xC3\xB4ne du projet"; }

core::Status PixelIconEditor::buildUi() {
    auto body = std::make_unique<IconBody>(*this);
    cancel_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", "dialog.projectIcon.cancel")));
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Appliquer", "dialog.projectIcon.ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons = {cancel_, ok_};
    auto field = std::make_unique<ui::InputText>("dialog.projectIcon.hex");
    field->setMaxLength(7);
    hex_ = &static_cast<ui::InputText&>(body->addChild(std::move(field)));
    body->hex = hex_;
    links_ += cancel_->clicked->connect([this] { finish(false); });
    links_ += ok_->clicked->connect([this] { finish(true); });
    links_ += hex_->textChanged->connect([this](const std::string& text) {
        std::uint32_t rgb = 0;
        if (state_.color == 0 || !ic::parseHex(text, rgb)) return;
        if (state_.icon.pixels.empty()) state_.icon = ic::blank();
        state_.icon.palette[static_cast<std::size_t>(state_.color - 1)] = rgb;
        state_.preset.clear();
        root().invalidate();
    });
    // 1.10 (chantier Q) : la palette de la couleur choisie - nuancier, choix
    // personnalise, pipette (une couleur prise n'importe ou dans la fenetre).
    // Une couleur d'icone est opaque : la transparence de la palette est ignoree.
    body->palette = &static_cast<ui::ColorPalette&>(body->addChild(std::make_unique<ui::ColorPalette>("dialog.projectIcon.palette")));
    body->palette->setVisibility(ui::Visibility::Collapsed);
    body->paletteButton = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("\xE2\x80\xA6", "dialog.projectIcon.paletteButton")));
    body->paletteButton->setTooltip("La palette : nuancier, choix personnalis\xC3\xA9, pipette (I).");
    {
        ui::ColorPalette* palette = body->palette;
        links_ += body->paletteButton->clicked->connect([this, palette] {
            if (state_.color == 0) return;
            palette->setValue(ic::hex(ic::colorOf(state_.icon, state_.color)));
            palette->setTitle(std::string(ic::colorName(state_.color)), "ic\xC3\xB4ne du projet");
            palette->setVisibility(ui::Visibility::Visible);
            palette->open();
        });
        links_ += palette->applied->connect([this, palette](const std::string& code) {
            palette->setVisibility(ui::Visibility::Collapsed);
            gfx::Color c;
            if (state_.color == 0 || !ui::parseHexColor(code, c)) return;   // ni vide ni expression ici
            if (state_.icon.pixels.empty()) state_.icon = ic::blank();
            state_.icon.palette[static_cast<std::size_t>(state_.color - 1)] =
                (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
            state_.preset.clear();
            syncHex();
            root().invalidate();
        });
        links_ += palette->closed->connect([palette] { palette->setVisibility(ui::Visibility::Collapsed); });
    }
    setRoot(std::move(body));
    syncHex();
    return core::ok();
}

void PixelIconEditor::syncHex() {
    if (!hex_) return;
    hex_->setVisibility(state_.tab == Tab::Draw && state_.color != 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (auto* body = static_cast<IconBody*>(&root()); body->paletteButton) body->paletteButton->setVisibility(hex_->visibility());
    if (state_.color) hex_->setText(ic::hex(ic::colorOf(state_.icon, state_.color)).substr(1));
}

void PixelIconEditor::snapshot() {
    state_.undo.push_back(state_.icon.pixels);
    if (state_.undo.size() > 80) state_.undo.erase(state_.undo.begin());
    state_.redo.clear();
}

void PixelIconEditor::choosePreset(const std::string& key) {
    if (key == "aucune") {
        noIcon_ = true;
        state_.preset = key;
        return;
    }
    auto made = ic::preset(key, state_.projectName);
    if (made.empty()) return;
    noIcon_ = false;
    snapshot();
    state_.icon = std::move(made);
    state_.preset = key;
    syncHex();
}

void PixelIconEditor::setTool(Tool t) { state_.tool = t; }

void PixelIconEditor::setColor(int index) {
    state_.color = static_cast<std::uint8_t>(std::clamp(index, 0, 16));
    if (state_.tool == Tool::Picker || (state_.tool == Tool::Eraser && state_.color != 0)) state_.tool = Tool::Pencil;
    syncHex();
}

void PixelIconEditor::strokeAt(int x, int y, bool begin, bool rightButton, bool shift) {
    auto& icon = state_.icon;
    if (icon.pixels.size() != 32u * 32u) icon = ic::blank();
    noIcon_ = false;
    const Tool tool = rightButton ? Tool::Eraser : state_.tool;
    const std::uint8_t color = tool == Tool::Eraser ? std::uint8_t{0} : state_.color;
    if (begin) {
        if (tool == Tool::Picker) {
            setColor(ic::at(icon, x, y));
            if (state_.color == 0) state_.tool = Tool::Eraser;
            return;
        }
        snapshot();
        stroking_ = true;
        state_.preset.clear();
        if (tool == Tool::Bucket) {
            ic::fill(icon, x, y, color, state_.mirror);
            stroking_ = false;
            return;
        }
        if (tool == Tool::Line || tool == Tool::Rect) {
            strokeBase_ = icon.pixels;
            strokeX_ = x;
            strokeY_ = y;
            if (tool == Tool::Line) ic::line(icon, x, y, x, y, color, state_.mirror);
            else ic::frame(icon, x, y, x, y, color, state_.mirror);
            return;
        }
        if (shift && strokeX_ >= 0) ic::line(icon, strokeX_, strokeY_, x, y, color, state_.mirror);
        else ic::put(icon, x, y, color, state_.mirror);
        strokeX_ = x;
        strokeY_ = y;
        return;
    }
    if (!stroking_) return;
    if (tool == Tool::Line || tool == Tool::Rect) {
        icon.pixels = strokeBase_;
        if (tool == Tool::Line) ic::line(icon, strokeX_, strokeY_, x, y, color, state_.mirror);
        else ic::frame(icon, strokeX_, strokeY_, x, y, color, state_.mirror);
        return;
    }
    ic::line(icon, strokeX_, strokeY_, x, y, color, state_.mirror);
    strokeX_ = x;
    strokeY_ = y;
}

void PixelIconEditor::strokeEnd() {
    if (state_.tool == Tool::Line || state_.tool == Tool::Rect) strokeX_ = strokeY_ = -1;
    stroking_ = false;
}

void PixelIconEditor::undoStep() {
    if (state_.undo.empty()) return;
    state_.redo.push_back(state_.icon.pixels);
    state_.icon.pixels = state_.undo.back();
    state_.undo.pop_back();
}

void PixelIconEditor::redoStep() {
    if (state_.redo.empty()) return;
    state_.undo.push_back(state_.icon.pixels);
    state_.icon.pixels = state_.redo.back();
    state_.redo.pop_back();
}

void PixelIconEditor::clearAll() {
    snapshot();
    if (state_.icon.pixels.size() != 32u * 32u) state_.icon = ic::blank();
    std::fill(state_.icon.pixels.begin(), state_.icon.pixels.end(), std::uint8_t{0});
    state_.preset.clear();
}

ui::EventResult PixelIconEditor::HandleEvent(const ui::InputEvent& ev) {
    auto* body = static_cast<IconBody*>(&root());
    // 1.10 (chantier Q) : la palette ouverte (ou sa pipette) a les touches -
    // Echap la ferme, elle, pas l'editeur ; I y lance sa pipette, pas l'outil.
    if (body->palette && body->palette->isOpen()) {
        const auto r = menu::WidgetMenu::HandleEvent(ev);
        if (body->closeRequested) finish(false);
        return r;
    }
    const bool typing = hex_ && hex_->focused();
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) { finish(false); return ui::EventResult::Consumed; }
        if (k->key == ui::Key::Return && !typing) { finish(true); return ui::EventResult::Consumed; }
        if (k->mods.ctrl && k->key == ui::Key::Z && !typing) { undoStep(); root().invalidate(); return ui::EventResult::Consumed; }
        if (k->mods.ctrl && k->key == ui::Key::Y && !typing) { redoStep(); root().invalidate(); return ui::EventResult::Consumed; }
    }
    if (const auto* t = std::get_if<ui::TextInput>(&ev); t && !typing && t->utf8.size() == 1 && state_.tab == Tab::Draw) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(t->utf8[0])));
        const auto set = [&](Tool tool) { setTool(tool); root().invalidate(); return ui::EventResult::Consumed; };
        switch (c) {
            case 'p': return set(Tool::Pencil);
            case 'e': return set(Tool::Eraser);
            case 'f': return set(Tool::Bucket);
            case 'i': return set(Tool::Picker);
            case 'l': return set(Tool::Line);
            case 'r': return set(Tool::Rect);
            case 'm': state_.mirror = !state_.mirror; root().invalidate(); return ui::EventResult::Consumed;
            case 'g': state_.grid = !state_.grid; root().invalidate(); return ui::EventResult::Consumed;
            default: break;
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (body->closeRequested) finish(false);
    syncHex();
    return r;
}

void PixelIconEditor::finish(bool ok) {
    if (done_) return;
    done_ = true;
    std::string payload;
    if (ok && !noIcon_ && !ic::isBlank(state_.icon)) payload = ic::toText(state_.icon);
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

} // namespace app
