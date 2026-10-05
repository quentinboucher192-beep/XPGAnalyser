// app/ThemeEditor.cpp - l'editeur d'un theme (lot API 8).
//
// Un volet a droite, SANS assombrir l'ecran : chaque couleur changee se voit
// tout de suite sur toute l'application (Hosts::preview). Le volet se dessine
// avec les couleurs du theme d'avant (chrome_) : on peut rendre le texte
// illisible sans perdre l'editeur.
#include "ThemeEditor.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/ThemeFile.hpp"
#include "../ui/widgets/ColorPalette.hpp"   // 1.10 (chantier Q) : la palette et sa pipette
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/PathBrowse.hpp"
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kLabel{15};
const gfx::FontId kTitle{20};

bool inside(const gfx::Rect& r, gfx::Point p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

std::string guil(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }

std::vector<std::string> wrapText(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, f) > w) {
            out.push_back(line);
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
    if (!line.empty()) out.push_back(line);
    return out;
}

// La regle la plus juste ou entre cette couleur (premier plan ou fond).
const ui::ContrastCheck* worstFor(const std::vector<ui::ContrastCheck>& all, const std::string& key) {
    const ui::ContrastCheck* w = nullptr;
    for (const auto& c : all) {
        if (c.fg != key && c.bg != key) continue;
        if (!w || c.ratio / c.need < w->ratio / w->need) w = &c;
    }
    return w;
}

} // namespace

// ------------------------------------------------------------------ le corps ---
class ThemeEditorDialog::Body final : public ui::Widget {
public:
    explicit Body(ThemeEditorDialog& d) : d_(d) {
        const char* group = "";
        for (std::size_t i = 0; i < ui::themeColorKeys().size(); ++i) {
            const auto& k = ui::themeColorKeys()[i];
            if (std::string_view(group) != k.group) {
                group = k.group;
                rows_.push_back({true, k.group, i, {}});
            }
            rows_.push_back({false, k.key, i, {}});
        }
    }
    std::vector<ui::Button*> head;    // Annuler, Enregistrer
    std::vector<ui::Button*> tools;   // Deriver de l'accent, Annuler le dernier changement
    // 1.10 (chantier Q) : la palette de la couleur choisie (ouverte par le bouton
    // a droite de sa valeur, posee sur la case de la valeur) et ce bouton.
    ui::ColorPalette* palette{nullptr};
    ui::Button*       paletteButton{nullptr};

    // La couleur choisie amenee dans la vue de la liste.
    void reveal() {
        for (const auto& r : rows_) {
            if (r.heading || r.key != d_.selected_) continue;
            if (r.rect.y - scroll_ < 0.f) scroll_ = r.rect.y - 30.f;
            else if (r.rect.y + r.rect.h - scroll_ > list_.h) scroll_ = r.rect.y + r.rect.h - list_.h + 6.f;
            clampScroll();
            return;
        }
    }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(920.f, b.w - 20.f);
        panel_ = {b.x + b.w - w, b.y + 48.f, w, std::max(360.f, b.h - 72.f)};
        const float x0 = panel_.x + 16.f;
        float bx = panel_.x + panel_.w - 16.f;
        for (auto it = head.rbegin(); it != head.rend(); ++it) {
            const float bw = ui::measureWidth((*it)->text(), gfx::FontId{16}) + 32.f;
            bx -= bw;
            (*it)->setBounds({bx, panel_.y + 10.f, bw, 30.f});
            bx -= 8.f;
        }
        // Le nom, la famille, l'auteur.
        metaY_ = panel_.y + 52.f;
        float x = x0 + ui::measureWidth("Nom", kSmall) + 8.f;
        if (d_.name_) d_.name_->setBounds({x, metaY_, 220.f, 28.f});
        x += 236.f;
        familyX_ = x;
        x += ui::measureWidth("Famille", kSmall) + 8.f;
        if (d_.family_) {
            const float fw = std::max(130.f, ui::measureWidth(d_.family_->text(), gfx::FontId{16}) + 28.f);
            d_.family_->setBounds({x, metaY_, fw, 28.f});
            x += fw + 16.f;
        }
        authorX_ = x;
        x += ui::measureWidth("Auteur", kSmall) + 8.f;
        if (d_.author_) d_.author_->setBounds({x, metaY_, std::max(120.f, panel_.x + panel_.w - 16.f - x), 28.f});
        const float top = metaY_ + 44.f;
        const float leftW = std::floor((panel_.w - 48.f) * 0.5f);
        list_ = {x0, top, leftW, std::max(120.f, panel_.y + panel_.h - 40.f - top)};
        right_ = {x0 + leftW + 16.f, top, panel_.w - leftW - 48.f, list_.h};
        if (d_.hex_) d_.hex_->setBounds({right_.x + right_.w - 154.f, right_.y + 6.f, 120.f, 28.f});
        if (paletteButton) paletteButton->setBounds({right_.x + right_.w - 28.f, right_.y + 6.f, 28.f, 28.f});
        if (palette && d_.hex_) palette->setBounds(d_.hex_->bounds());
        for (int i = 0; i < 3; ++i)
            track_[i] = {right_.x + 118.f, right_.y + 96.f + 34.f * static_cast<float>(i), right_.w - 124.f, 16.f};
        // Les contrastes : un encadre dont la hauteur suit le nombre de regles fautives.
        const auto fails = ui::contrastFailures(d_.theme_);
        const std::size_t shown = std::min<std::size_t>(fails.size(), 5);
        boxY_ = right_.y + 252.f;
        float y = boxY_ + 12.f + (fails.empty() ? 40.f : 44.f + 18.f * static_cast<float>(shown + (fails.size() > 5 ? 1 : 0)));
        if (d_.fix_) {
            d_.fix_->setVisibility(fails.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            d_.fix_->setBounds({right_.x + 12.f, y, ui::measureWidth(d_.fix_->text(), gfx::FontId{16}) + 30.f, 28.f});
            if (!fails.empty()) y += 36.f;
        }
        boxH_ = y - boxY_;
        y += 12.f;
        for (auto* t : tools) {
            const float tw = ui::measureWidth(t->text(), gfx::FontId{16}) + 30.f;
            t->setBounds({right_.x, y, std::min(tw, right_.w), 30.f});
            y += 36.f;
        }
        previewY_ = y + 4.f;
        placeRows();
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;       // le theme d'avant (chrome_)
        auto& r = ctx.r;
        const ui::Theme& t = d_.theme_;          // celui qu'on modifie
        r.fillRect({panel_.x - 1.f, panel_.y, 1.f, panel_.h}, c.borderStrong);
        r.fillRect(panel_, c.panelBg);
        const std::string title = "\xC3\x89" "diteur de th\xC3\xA8me";
        r.drawText({panel_.x + 18.f, panel_.y + 14.f}, title, kTitle, c.text);
        r.drawText({panel_.x + 18.6f, panel_.y + 14.f}, title, kTitle, c.text);
        if (!t.base.empty())
            r.drawText({panel_.x + 30.f + ui::measureWidth(title, kTitle), panel_.y + 19.f},
                       "\xC3\xA0 partir de " + guil(ui::Theme::labelOf(t.base)), kSmall, c.textMuted);
        r.drawText({panel_.x + 16.f, metaY_ + 6.f}, "Nom", kSmall, c.textMuted);
        r.drawText({familyX_, metaY_ + 6.f}, "Famille", kSmall, c.textMuted);
        r.drawText({authorX_, metaY_ + 6.f}, "Auteur", kSmall, c.textMuted);

        // --- la liste des couleurs, par groupe ---
        const auto checks = ui::checkContrasts(t);
        r.pushClip(list_);
        for (const auto& row : rows_) {
            const gfx::Rect rc{row.rect.x, row.rect.y + list_.y - scroll_, row.rect.w, row.rect.h};
            if (rc.y + rc.h < list_.y || rc.y > list_.y + list_.h) continue;
            if (row.heading) {
                r.drawText({rc.x + 2.f, rc.y + 8.f}, row.key, kLabel, c.text);
                r.fillRect({rc.x, rc.y + rc.h - 2.f, rc.w, 1.f}, c.border);
                continue;
            }
            const auto& k = ui::themeColorKeys()[row.index];
            const gfx::Color* col = ui::themeColor(t, k.key);
            const bool on = d_.selected_ == k.key;
            if (on) r.fillRoundedRect(rc, c.selectionBg, 4.f);
            else if (static_cast<int>(row.index) == hover_) r.fillRoundedRect(rc, ctx.theme.brand.hover, 4.f);
            const gfx::Color fg = on ? c.selectionText : c.text;
            const gfx::Rect sw{rc.x + 6.f, rc.y + 4.f, 18.f, 18.f};
            if (col) r.fillRoundedRect(sw, *col, 4.f);
            r.strokeRect(sw, c.borderStrong, 1.f);
            r.drawText({rc.x + 32.f, rc.y + 5.f}, k.label, kSmall, fg);
            if (col) r.drawText({rc.x + rc.w - 172.f, rc.y + 5.f}, ui::themeColorText(*col, k.alpha), gfx::FontId{13}, on ? fg : c.textMuted);
            if (const auto* w = worstFor(checks, k.key)) {
                const std::string ratio = ui::contrastText(w->ratio);
                const gfx::Color rc2 = w->pass ? (on ? fg : c.textMuted) : ctx.theme.onSurface(c.warning);
                r.drawText({rc.x + rc.w - 78.f, rc.y + 5.f}, ratio, gfx::FontId{13}, rc2);
                ui::drawIcon(r, w->pass ? ui::Icon::Ok : ui::Icon::Warning, {rc.x + rc.w - 20.f, rc.y + 6.f, 14.f, 14.f},
                             w->pass ? (on ? fg : ctx.theme.onSurface(c.ok)) : ctx.theme.onSurface(c.warning));
            }
        }
        r.popClip();
        // 1.11.4 : la barre se tire (a droite de la liste, ou elle etait).
        sbar_.paint(ctx, {list_.x, list_.y, list_.w + 12.f, list_.h}, contentH_, list_.h, scroll_);

        // --- la couleur choisie ---
        const auto* key = ui::themeColorKey(d_.selected_);
        const gfx::Color* sel = ui::themeColor(t, d_.selected_);
        if (key && sel) {
            const gfx::Rect big{right_.x, right_.y, 40.f, 40.f};
            r.fillRoundedRect(big, *sel, 6.f);
            r.strokeRect(big, c.borderStrong, 1.f);
            r.drawText({right_.x + 50.f, right_.y + 2.f}, key->label, kLabel, c.text);
            r.drawText({right_.x + 50.f, right_.y + 22.f}, key->key, kSmall, c.textMuted);
            float hy = right_.y + 46.f;
            for (const auto& l : wrapText(key->hint, kSmall, right_.w)) {
                if (hy > right_.y + 80.f) break;
                r.drawText({right_.x, hy}, l, kSmall, c.textMuted);
                hy += r.lineHeight(kSmall) + 1.f;
            }
            const ui::Hsl hsl = drag_ >= 0 ? dragHsl_ : ui::toHsl(*sel);
            const char* names[3] = {"Teinte", "Saturation", "Luminosit\xC3\xA9"};
            char value[3][24];
            std::snprintf(value[0], sizeof value[0], " %d\xC2\xB0", static_cast<int>(std::lround(hsl.h)));
            std::snprintf(value[1], sizeof value[1], " %d %%", static_cast<int>(std::lround(hsl.s * 100.0)));
            std::snprintf(value[2], sizeof value[2], " %d %%", static_cast<int>(std::lround(hsl.l * 100.0)));
            for (int i = 0; i < 3; ++i) {
                const gfx::Rect& tr = track_[i];
                r.drawText({right_.x, tr.y - 1.f}, std::string(names[i]) + value[i], kSmall, c.text);
                constexpr int kSteps = 24;
                for (int s = 0; s < kSteps; ++s) {
                    const double f = (static_cast<double>(s) + 0.5) / kSteps;
                    ui::Hsl h2 = hsl;
                    if (i == 0) {
                        h2.h = f * 360.0;
                        h2.s = std::max(h2.s, 0.35);
                        h2.l = std::clamp(h2.l, 0.3, 0.7);
                    } else if (i == 1) {
                        h2.s = f;
                    } else {
                        h2.l = f;
                    }
                    const float sx = tr.x + tr.w * static_cast<float>(s) / kSteps;
                    r.fillRect({sx, tr.y, tr.w / kSteps + 0.5f, tr.h}, ui::fromHsl(h2));
                }
                r.strokeRect(tr, c.border, 1.f);
                const double v = i == 0 ? hsl.h / 360.0 : i == 1 ? hsl.s : hsl.l;
                const float kx = tr.x + tr.w * static_cast<float>(std::clamp(v, 0.0, 1.0));
                r.fillRect({kx - 2.f, tr.y - 3.f, 4.f, tr.h + 6.f}, c.text);
                r.strokeRect({kx - 3.f, tr.y - 4.f, 6.f, tr.h + 8.f}, c.panelBg, 1.f);
            }
            // Les couples ou elle entre.
            float py = right_.y + 200.f;
            int n = 0;
            for (const auto& ch : checks) {
                if (ch.fg != key->key && ch.bg != key->key) continue;
                if (++n > 2) break;
                r.drawText({right_.x, py}, ch.label + " : " + ui::contrastText(ch.ratio) + (ch.pass ? "" : ", il faut " + ui::contrastText(ch.need)),
                           kSmall, ch.pass ? c.textMuted : ctx.theme.onSurface(c.warning));
                py += r.lineHeight(kSmall) + 2.f;
            }
        }

        // --- les contrastes du theme ---
        const auto fails = ui::contrastFailures(t);
        const gfx::Rect box{right_.x, boxY_, right_.w, boxH_};
        const gfx::Color tone = fails.empty() ? c.ok : c.warning;
        r.fillRoundedRect(box, tone.withAlpha(ctx.theme.isDark() ? 40 : 28), 6.f);
        r.strokeRect(box, tone.withAlpha(120), 1.f);
        float y = boxY_ + 10.f;
        if (fails.empty()) {
            r.drawText({box.x + 12.f, y}, "Tous les contrastes passent.", kLabel, ctx.theme.onSurface(c.ok));
            y += r.lineHeight(kLabel) + 2.f;
            r.drawText({box.x + 12.f, y}, std::string("Texte 7:1 sur les fonds, le reste ") + (t.isHighContrast() ? "7:1." : "4,5:1."), kSmall, c.textMuted);
        } else {
            r.drawText({box.x + 12.f, y}, std::to_string(fails.size()) + (fails.size() > 1 ? " contrastes trop faibles." : " contraste trop faible."),
                       kLabel, ctx.theme.onSurface(c.warning));
            y += r.lineHeight(kLabel) + 2.f;
            r.drawText({box.x + 12.f, y}, "Le th\xC3\xA8me reste utilisable, mais ces textes se liront mal :", kSmall, c.textMuted);
            y += 20.f;
            for (std::size_t i = 0; i < fails.size() && i < 5; ++i) {
                r.drawText({box.x + 18.f, y}, fails[i].label + " : " + ui::contrastText(fails[i].ratio) + ", il faut " + ui::contrastText(fails[i].need),
                           kSmall, c.text);
                y += 18.f;
            }
            if (fails.size() > 5) r.drawText({box.x + 18.f, y}, "et " + std::to_string(fails.size() - 5) + " autre(s)...", kSmall, c.textMuted);
        }

        // --- l'apercu : le theme modifie, en petit (la maquette), s'il y a la place ---
        const float pvRoom = panel_.y + panel_.h - 44.f - previewY_;
        if (pvRoom >= 150.f) {
            const auto& p = t.color;
            r.drawText({right_.x, previewY_}, "Aper\xC3\xA7u", kLabel, c.text);
            const gfx::Rect pv{right_.x, previewY_ + 22.f, right_.w, std::min(pvRoom - 22.f, 176.f)};
            r.fillRoundedRect(pv, p.panelBg, 6.f);
            r.strokeRect(pv, p.border, 1.f);
            float py = pv.y + 10.f;
            const gfx::Rect b1{pv.x + 10.f, py, ui::measureWidth("Simuler", kSmall) + 24.f, 24.f};
            r.fillRoundedRect(b1, p.accent, 4.f);
            r.drawText({b1.x + 12.f, b1.y + 4.f}, "Simuler", kSmall, p.textInverted);
            const gfx::Rect b2{b1.x + b1.w + 8.f, py, ui::measureWidth("Exporter", kSmall) + 24.f, 24.f};
            r.fillRoundedRect(b2, t.brand.card, 4.f);
            r.strokeRect(b2, p.borderStrong, 1.f);
            r.drawText({b2.x + 12.f, b2.y + 4.f}, "Exporter", kSmall, p.text);
            py += 32.f;
            struct Line { const char* name; const char* value; gfx::Color state; const char* word; };
            const Line lines[] = {{"Vitesse_Moteur", "1 452 tr/min", p.ok, "marche"},
                                  {"Pression_Gaz", "3,8 bar", p.warning, "alerte"},
                                  {"T_Armoire", "61 \xC2\xB0" "C", p.error, "d\xC3\xA9" "faut"}};
            for (int i = 0; i < 3; ++i) {
                const bool selRow = i == 1;
                if (selRow) r.fillRect({pv.x + 1.f, py - 2.f, pv.w - 2.f, 20.f}, p.selectionBg);
                const gfx::Color fg = selRow ? p.selectionText : p.text;
                r.drawText({pv.x + 10.f, py}, lines[i].name, kSmall, fg);
                r.drawText({pv.x + pv.w * 0.45f, py}, lines[i].value, kSmall, selRow ? fg : p.textMuted);
                const float bw = ui::measureWidth(lines[i].word, kSmall) + 12.f;
                const gfx::Rect badge{pv.x + pv.w - bw - 10.f, py - 1.f, bw, 18.f};
                r.fillRoundedRect(badge, lines[i].state.withAlpha(t.isDark() ? 60 : 40), 3.f);
                r.drawText({badge.x + 6.f, py}, lines[i].word, kSmall, t.onSurface(lines[i].state));
                py += 22.f;
            }
            const gfx::Rect code{pv.x + 8.f, py + 2.f, pv.w - 16.f, std::max(0.f, pv.y + pv.h - py - 10.f)};
            if (code.h >= 40.f) {
                r.fillRoundedRect(code, t.brand.codeBg, 4.f);
                float cx = code.x + 8.f;
                const auto word = [&](const std::string& s, gfx::Color col, float wy) {
                    r.drawText({cx, wy}, s, kSmall, col);
                    cx += ui::measureWidth(s + " ", kSmall);
                };
                const float y1 = code.y + 5.f, y2 = y1 + 18.f;
                word("IF", p.syntaxKeyword, y1);
                word("Vitesse_Moteur", p.syntaxFunction, y1);
                word(">", p.syntaxOperator, y1);
                word("1450", p.syntaxNumber, y1);
                word("THEN", p.syntaxKeyword, y1);
                cx = code.x + 22.f;
                word("Message :=", p.text, y2);
                word("'Survitesse';", p.syntaxString, y2);
                word("(* coupe *)", p.syntaxComment, y2);
            }
        }

        // --- le message ---
        if (!d_.message_.empty()) {
            const gfx::Color mc = d_.messageError_ ? ctx.theme.onSurface(c.error) : c.textMuted;
            const auto lines = wrapText(d_.message_, kSmall, panel_.w - 36.f);
            float my = panel_.y + panel_.h - 30.f - (lines.size() > 1 ? 8.f : 0.f);
            for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
                r.drawText({panel_.x + 18.f, my}, lines[i], kSmall, mc);
                my += r.lineHeight(kSmall) + 1.f;
            }
        } else {
            r.drawText({panel_.x + 18.f, panel_.y + panel_.h - 30.f},
                       "Chaque couleur change toute l'appli pendant que tu la choisis. Ctrl+Z : le changement d'avant ; \xC3\x89" "chap : annuler.",
                       kSmall, c.textMuted);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        {
            float off = scroll_;   // 1.11.4 : la barre de defilement se tire
            if (sbar_.handle(*this, ev, off)) {
                scroll_ = off;
                clampScroll();
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!inside(list_, w->pos)) return ui::EventResult::Ignored;
            scroll_ -= w->dy * 52.f;
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            if (drag_ >= 0) {
                dragTo(m->pos.x);
                return ui::EventResult::Consumed;
            }
            const int h = rowAt(m->pos);
            if (h != hover_) {
                hover_ = h;
                const auto& keys = ui::themeColorKeys();
                setTooltip(h >= 0 ? std::string(keys[static_cast<std::size_t>(h)].label) + " : " + keys[static_cast<std::size_t>(h)].hint : std::string{});
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
            for (int i = 0; i < 3; ++i) {
                const gfx::Rect hit{track_[i].x - 4.f, track_[i].y - 6.f, track_[i].w + 8.f, track_[i].h + 12.f};
                if (!inside(hit, m->pos)) continue;
                const gfx::Color* sel = ui::themeColor(d_.theme_, d_.selected_);
                if (!sel) return ui::EventResult::Ignored;
                d_.snapshot();                  // un geste, un seul retour en arriere
                dragHsl_ = ui::toHsl(*sel);
                drag_ = i;
                dragTo(m->pos.x);
                return ui::EventResult::Consumed;
            }
            const int row = rowAt(m->pos);
            if (row >= 0) {
                d_.select(ui::themeColorKeys()[static_cast<std::size_t>(row)].key);
                if (d_.hex_) d_.focus().focus(d_.hex_);      // la valeur se tape tout de suite
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }
        if (std::get_if<ui::MouseUp>(&ev) && drag_ >= 0) {
            drag_ = -1;
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Row {
        bool        heading{false};
        std::string key;          // la cle de la couleur, ou le nom du groupe
        std::size_t index{0};     // dans themeColorKeys()
        gfx::Rect   rect{};       // dans la liste, avant le defilement
    };

    void placeRows() {
        float y = 0.f;
        for (auto& row : rows_) {
            const float h = row.heading ? 32.f : 26.f;
            row.rect = {list_.x, y, list_.w, h};
            y += h;
        }
        contentH_ = y + 6.f;
        clampScroll();
    }
    void clampScroll() { scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH_ - list_.h)); }
    [[nodiscard]] int rowAt(gfx::Point p) const {
        if (!inside(list_, p)) return -1;
        for (const auto& row : rows_) {
            if (row.heading) continue;
            const gfx::Rect rc{row.rect.x, row.rect.y + list_.y - scroll_, row.rect.w, row.rect.h};
            if (inside(rc, p)) return static_cast<int>(row.index);
        }
        return -1;
    }
    void dragTo(float x) {
        gfx::Color* sel = ui::themeColor(d_.theme_, d_.selected_);
        if (!sel || drag_ < 0) return;
        const gfx::Rect& tr = track_[drag_];
        const double f = std::clamp(static_cast<double>((x - tr.x) / std::max(1.f, tr.w)), 0.0, 1.0);
        if (drag_ == 0) dragHsl_.h = std::min(f * 360.0, 359.9);
        else if (drag_ == 1) dragHsl_.s = f;
        else dragHsl_.l = f;
        *sel = ui::fromHsl(dragHsl_, sel->a);
        d_.changed(false);
    }

    ThemeEditorDialog& d_;
    std::vector<Row>   rows_;
    gfx::Rect          panel_{}, list_{}, right_{};
    gfx::Rect          track_[3]{};
    float              metaY_{0.f}, familyX_{0.f}, authorX_{0.f}, boxY_{0.f}, boxH_{0.f}, previewY_{0.f};
    float              scroll_{0.f}, contentH_{0.f};
    ui::PaintedScrollBar sbar_;   // 1.11.4
    int                hover_{-1}, drag_{-1};
    ui::Hsl            dragHsl_{};
};

// ------------------------------------------------------------- le dialogue ---
ThemeEditorDialog::ThemeEditorDialog(ui::Theme theme, Hosts hosts)
    : menu::WidgetMenu("dialog.themeeditor"), theme_(std::move(theme)), hosts_(std::move(hosts)) {
    chrome_ = theme_;
    theme_.user = true;
    if (!ui::themeColorKeys().empty()) selected_ = ui::themeColorKeys().front().key;
}

menu::MenuTraits ThemeEditorDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = false;       // l'apercu en direct : on regarde l'ecran
    return t;
}

std::string ThemeEditorDialog::title() const { return "\xC3\x89" "diteur de th\xC3\xA8me"; }

core::Status ThemeEditorDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const auto button = [&](const std::string& text, const std::string& id, const std::string& tip) {
        auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, id)));
        if (!tip.empty()) b->setTooltip(tip);
        return b;
    };
    const auto field = [&](const std::string& id, const std::string& text) {
        auto* f = &static_cast<ui::InputText&>(body->addChild(std::make_unique<ui::InputText>(id)));
        f->setText(text);
        return f;
    };
    name_ = field("dialog.themeeditor.name", theme_.name);
    name_->setMaxLength(60);
    author_ = field("dialog.themeeditor.author", theme_.author);
    author_->setMaxLength(60);
    author_->setPlaceholder("Toi, ton service...");
    family_ = button(theme_.family.empty() ? std::string(theme_.isDark() ? "Sombres" : "Clairs") : theme_.family, "dialog.themeeditor.family",
                     "Un clic : la famille suivante. Contraste \xC3\xA9lev\xC3\xA9 demande 7:1 partout.");
    hex_ = field("dialog.themeeditor.hex", {});
    hex_->setMaxLength(9);
    // 1.10 (chantier Q) : la palette de la couleur choisie - nuancier, choix
    // personnalise, transparence (si la couleur l'admet), pipette (une couleur
    // prise n'importe ou dans la fenetre). Cachee tant qu'elle est fermee.
    body->palette = &static_cast<ui::ColorPalette&>(body->addChild(std::make_unique<ui::ColorPalette>("dialog.themeeditor.palette")));
    body->palette->setVisibility(ui::Visibility::Collapsed);
    body->paletteButton = button("\xE2\x80\xA6", "dialog.themeeditor.paletteButton",
                                 "La palette : nuancier, choix personnalis\xC3\xA9, transparence, pipette (I).");
    {
        ui::ColorPalette* palette = body->palette;
        links_ += body->paletteButton->clicked->connect([this, palette] {
            const auto* k = ui::themeColorKey(selected_);
            const gfx::Color* c = ui::themeColor(theme_, selected_);
            if (!k || !c) return;
            palette->setValue(ui::themeColorText(*c, k->alpha));
            palette->setTitle(k->label, k->group);
            palette->setVisibility(ui::Visibility::Visible);
            palette->open();
        });
        links_ += palette->applied->connect([this, palette](const std::string& code) {
            palette->setVisibility(ui::Visibility::Collapsed);
            if (code.empty() || code.front() == '=') return;          // ni vide ni expression dans un theme
            std::string v = code;
            if (const auto* k = ui::themeColorKey(selected_); k && !k->alpha && v.size() == 9) v.resize(7);   // sans transparence ici
            (void)setColor(selected_, v);
        });
        links_ += palette->closed->connect([palette] { palette->setVisibility(ui::Visibility::Collapsed); });
    }
    cancel_ = button("Annuler", "dialog.themeeditor.cancel", "Le th\xC3\xA8me d'avant revient (\xC3\x89" "chap).");
    save_ = button("Enregistrer", "dialog.themeeditor.save", "Dans tes th\xC3\xA8mes, et appliqu\xC3\xA9 (Ctrl+S).");
    save_->setStyle(ui::Button::Style::Primary);
    export_ = button("Exporter...", "dialog.themeeditor.export",
                     "Un fichier .xpgtheme de ce th\xC3\xA8me tel qu'il est (nom, famille, auteur, couleurs), \xC3\xA0 envoyer \xC3\xA0 un coll\xC3\xA8gue.");
    body->head = {export_, cancel_, save_};
    fix_ = button("Corriger les contrastes", "dialog.themeeditor.fix", "Change la luminosit\xC3\xA9 des couleurs fautives, pas leur teinte.");
    derive_ = button("D\xC3\xA9river de l'accent", "dialog.themeeditor.derive",
                     "Garde l'accent et calcule le reste : fonds, traits, textes, s\xC3\xA9lection - contrastes tenus.");
    undoButton_ = button("Annuler le dernier changement", "dialog.themeeditor.undo", "Ctrl+Z");
    body->tools = {derive_, undoButton_};

    links_ += name_->textChanged->connect([this](const std::string& t) { theme_.name = t; });
    links_ += author_->textChanged->connect([this](const std::string& t) { theme_.author = t; });
    links_ += family_->clicked->connect([this] {
        const auto& fams = ui::Theme::familyLabels();
        std::size_t i = 0;
        while (i < fams.size() && ui::Theme::fold(fams[i]) != ui::Theme::fold(theme_.family)) ++i;
        setFamily(fams.empty() ? std::string{} : fams[(i + 1) % fams.size()]);
    });
    links_ += hex_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;                     // la valeur ecrite par l'editeur
        const auto* k = ui::themeColorKey(selected_);
        gfx::Color c{};
        if (!k || !ui::parseThemeColor(text, c, k->alpha)) return;   // on attend la suite
        gfx::Color* slot = ui::themeColor(theme_, selected_);
        if (!slot || (slot->r == c.r && slot->g == c.g && slot->b == c.b && slot->a == c.a)) return;
        snapshot();
        *slot = c;
        syncing_ = true;                          // ce qui est tape reste tel quel
        changed(false);
        syncing_ = false;
    });
    links_ += export_->clicked->connect([this] {
        const auto spec = ui::saveFile("Th\xC3\xA8mes XpgAnalyzer|*.xpgtheme", ui::pathIn(ui::UserThemes::folder(), theme_.name + ".xpgtheme"),
                                       "Exporter le th\xC3\xA8me");
        std::weak_ptr<char> alive = alive_;
        if (!ui::browsePath(spec, {}, [this, alive](std::string path) {
                if (!alive.expired()) exportTo(path);
            }))
            say("Pas d'explorateur ici : la commande de script theme-exporter \"chemin\" fait la m\xC3\xAAme chose.", true);
    });
    links_ += cancel_->clicked->connect([this] { cancel(); });
    links_ += save_->clicked->connect([this] { save(); });
    links_ += fix_->clicked->connect([this] { fixContrasts(); });
    links_ += derive_->clicked->connect([this] { derive(); });
    links_ += undoButton_->clicked->connect([this] {
        if (!undo()) say("Rien \xC3\xA0 annuler.");
    });
    setRoot(std::move(body));
    syncHex();
    if (hosts_.preview) hosts_.preview(theme_);
    return core::ok();
}

bool ThemeEditorDialog::select(const std::string& key) {
    const ui::ThemeColorKey* k = ui::themeColorKey(key);
    if (!k)
        for (const auto& c : ui::themeColorKeys())
            if (ui::Theme::fold(c.label) == ui::Theme::fold(key)) {
                k = &c;
                break;
            }
    if (!k) return false;
    selected_ = k->key;
    syncHex();
    if (body_) body_->reveal();
    root().invalidate();
    return true;
}

bool ThemeEditorDialog::setColor(const std::string& key, const std::string& hex) {
    if (!select(key)) {
        say("Couleur inconnue : " + key, true);
        return false;
    }
    const auto* k = ui::themeColorKey(selected_);
    gfx::Color c{};
    if (!k || !ui::parseThemeColor(hex, c, k->alpha)) {
        say(hex + " n'est pas une couleur (#RRGGBB).", true);
        return false;
    }
    snapshot();
    *ui::themeColor(theme_, selected_) = c;
    changed(false);
    return true;
}

bool ThemeEditorDialog::setHsl(double hue, double saturation, double lightness) {
    gfx::Color* slot = ui::themeColor(theme_, selected_);
    if (!slot) return false;
    snapshot();
    *slot = ui::fromHsl({std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0), std::clamp(saturation / 100.0, 0.0, 1.0),
                         std::clamp(lightness / 100.0, 0.0, 1.0)},
                        slot->a);
    changed(false);
    return true;
}

bool ThemeEditorDialog::derive(const std::string& accentHex, int dark) {
    gfx::Color accent = theme_.color.accent;
    if (!accentHex.empty() && !ui::parseThemeColor(accentHex, accent)) {
        say(accentHex + " n'est pas une couleur (#RRGGBB).", true);
        return false;
    }
    snapshot();
    ui::Theme t = ui::deriveFromAccent(theme_, accent, dark < 0 ? theme_.isDark() : dark != 0);
    t.name = theme_.name;
    t.user = true;
    t.family = theme_.family;
    t.base = theme_.base;
    t.description = theme_.description;
    t.author = theme_.author;
    theme_ = std::move(t);
    changed(false);
    say("Tout est d\xC3\xA9riv\xC3\xA9 de l'accent " + ui::themeColorText(accent) + " : les contrastes tiennent. Ctrl+Z revient en arri\xC3\xA8re.");
    return true;
}

int ThemeEditorDialog::fixContrasts() {
    snapshot();
    const auto keys = ui::fixContrasts(theme_);
    if (keys.empty()) {
        undo_.pop_back();
        say("Rien \xC3\xA0 corriger : tous les contrastes tiennent.");
        return 0;
    }
    changed(false);
    std::string names;
    for (std::size_t i = 0; i < keys.size() && i < 4; ++i) {
        const auto* k = ui::themeColorKey(keys[i]);
        names += (i ? ", " : "") + std::string(k ? k->label : keys[i].c_str());
    }
    if (keys.size() > 4) names += "...";
    say(std::to_string(keys.size()) + " couleur(s) corrig\xC3\xA9" "e(s) (" + names + ") : leur luminosit\xC3\xA9 seulement.");
    return static_cast<int>(keys.size());
}

bool ThemeEditorDialog::undo() {
    if (undo_.empty()) return false;
    theme_ = std::move(undo_.back());
    undo_.pop_back();
    changed(false);
    if (name_ && name_->text() != theme_.name) name_->setText(theme_.name);
    if (family_) family_->setText(theme_.family);
    return true;
}

bool ThemeEditorDialog::save() {
    std::string error;
    if (const auto problem = ui::themeNameProblem(theme_.name); !problem.empty()) {
        say(problem, true);
        return false;
    }
    if (!hosts_.save || !hosts_.save(theme_, error)) {
        say("Impossible d'enregistrer : " + error, true);
        return false;
    }
    finish(true);
    return true;
}

void ThemeEditorDialog::cancel() { finish(false); }

bool ThemeEditorDialog::exportTo(const std::string& path) {
    std::string error;
    if (!ui::writeThemeFile(theme_, path, &error)) {
        say("Export impossible : " + error, true);
        return false;
    }
    say(guil(theme_.name) + " export\xC3\xA9 : " + path);
    return true;
}

void ThemeEditorDialog::setName(const std::string& name) {
    theme_.name = name;
    if (name_ && name_->text() != name) name_->setText(name);
}

void ThemeEditorDialog::setAuthor(const std::string& author) {
    theme_.author = author;
    if (author_ && author_->text() != author) author_->setText(author);
}

bool ThemeEditorDialog::setFamily(const std::string& family) {
    const std::string label = ui::Theme::familyOf(family);
    if (label.empty()) return false;
    snapshot();
    theme_.family = label;
    if (family_) family_->setText(label);
    changed(false);
    return true;
}

void ThemeEditorDialog::snapshot() {
    undo_.push_back(theme_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
}

void ThemeEditorDialog::changed(bool snapshotFirst) {
    if (snapshotFirst) snapshot();
    if (hosts_.preview) hosts_.preview(theme_);
    syncHex();
    root().invalidateLayout();      // l'encadre des contrastes change de hauteur
    root().invalidate();
}

void ThemeEditorDialog::syncHex() {
    if (!hex_ || syncing_) return;
    const auto* k = ui::themeColorKey(selected_);
    const gfx::Color* c = ui::themeColor(theme_, selected_);
    if (!k || !c) return;
    syncing_ = true;
    hex_->setText(ui::themeColorText(*c, k->alpha));
    syncing_ = false;
}

void ThemeEditorDialog::say(std::string text, bool error) {
    message_ = std::move(text);
    messageError_ = error;
    messageUntil_ = now_ + (error ? 12.0 : 8.0);
    root().invalidate();
}

void ThemeEditorDialog::finish(bool keep) {
    if (done_) return;
    done_ = true;
    if (!keep && hosts_.cancel) hosts_.cancel();
    manager().CloseDialog(menu::DialogResult{keep ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, theme_.name});
}

void ThemeEditorDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    if (!message_.empty() && now_ > messageUntil_) {
        message_.clear();
        messageError_ = false;
        root().invalidate();
    }
}

// Le volet garde les couleurs du theme d'avant : on peut rendre le texte
// illisible sans perdre l'editeur.
void ThemeEditorDialog::Render(gfx::IRenderer& r, const menu::FrameContext& f) {
    menu::FrameContext g = f;
    g.theme = &chrome_;
    menu::WidgetMenu::Render(r, g);
}

ui::EventResult ThemeEditorDialog::HandleEvent(const ui::InputEvent& ev) {
    // 1.10 (chantier Q) : la palette ouverte (ou sa pipette) a les touches -
    // Echap la ferme, elle, pas l'editeur.
    if (body_ && body_->palette && body_->palette->isOpen()) return menu::WidgetMenu::HandleEvent(ev);
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            cancel();
            return ui::EventResult::Consumed;
        }
        if (k->mods.ctrl && k->key == ui::Key::S) {
            save();
            return ui::EventResult::Consumed;
        }
        if (k->mods.ctrl && k->key == ui::Key::Z) {
            if (!undo()) say("Rien \xC3\xA0 annuler.");
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app
