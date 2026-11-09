// =============================================================================
//  license_admin/gui/AdminsKit.hpp - 1.44.0 : CE QUE PARTAGENT LES PAGES DES
//  ADMINS (Administrateurs, Droits, Journal des admins ; docs/ADMINS.md)
// -----------------------------------------------------------------------------
//  Les trois pages de la maquette 1.43.0 (planches 01 a 04 et 08) dessinent les
//  memes choses : une carte blanche, l'avatar d'un admin (son initiale sur sa
//  couleur), une pastille (le role, l'etat, la criticite), l'empreinte d'une cle
//  en groupes de 4, une date relative (<< il y a 12 min >>, << hier 18:20 >>).
//  Et les memes petits widgets : un interrupteur (un droit permis ou non), une
//  pastille de filtre (<< Tous 248 >>, << Lea 96 >>), un dialogue au cadre
//  arrondi, un petit dialogue << un nom >> (un profil). Tout est ici, en ligne,
//  pour que les trois pages se ressemblent sans se copier.
//
//  Pourquoi un en-tete et pas une page : rien ici ne lit le Workspace ; ce sont
//  des briques de dessin et d'interface, sans etat partage.
// =============================================================================
#pragma once

#include "Page.hpp"

#include "../../licensing/Admins.hpp"
#include "../../menu/IMenu.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui::kit {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
namespace adm = licensing::admins;

// ---------------------------------------------------------------- le texte ----
inline std::string fit(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Couper un texte en lignes de `w` pixels au plus (aux espaces ; '\n' force une ligne).
inline std::vector<std::string> wrap(const std::string& text, FontId f, float w) {
    std::vector<std::string> out;
    std::size_t a = 0;
    for (;;) {
        std::size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        const std::string para = text.substr(a, b - a);
        std::string line;
        std::size_t i = 0;
        while (i <= para.size()) {
            std::size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            const std::string word = para.substr(i, j - i);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(tried, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            i = j + 1;
        }
        out.push_back(line);
        if (b >= text.size()) break;
        a = b + 1;
    }
    return out;
}

// Un texte coupe, dessine ; rend la hauteur prise. `maxLines` 0 : toutes.
inline float drawWrapped(gfx::IRenderer& r, float x, float y, float w, const std::string& text, FontId f, Color c, float lineH, int maxLines = 0) {
    const auto lines = wrap(text, f, w);
    float h = 0.f;
    int n = 0;
    for (const auto& l : lines) {
        if (maxLines > 0 && n >= maxLines) break;
        r.drawText({x, y + h}, fit(r, l, f, w), f, c);
        h += lineH;
        ++n;
    }
    return h;
}
inline float wrappedHeight(const std::string& text, FontId f, float w, float lineH, int maxLines = 0) {
    const auto n = wrap(text, f, w).size();
    return lineH * static_cast<float>(maxLines > 0 ? std::min<std::size_t>(n, static_cast<std::size_t>(maxLines)) : n);
}

// --------------------------------------------------------------- les dates ----
inline std::tm localTm(Seconds t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}
inline std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}
inline std::string clockFr(Seconds t) {
    if (t <= 0) return {};
    const std::tm tm = localTm(t);
    return two(tm.tm_hour) + ":" + two(tm.tm_min);
}
inline bool sameDay(Seconds a, Seconds b) {
    const std::tm x = localTm(a), y = localTm(b);
    return x.tm_year == y.tm_year && x.tm_yday == y.tm_yday;
}
// << auj. 11:42 >>, << hier 17:52 >>, << 07/11 15:40 >>, << 07/11/2025 >> (une autre annee).
inline std::string whenFr(Seconds now, Seconds t) {
    if (t <= 0) return "\xE2\x80\x94";
    if (sameDay(now, t)) return "auj. " + clockFr(t);
    if (sameDay(now - 86400, t)) return "hier " + clockFr(t);
    const std::tm tm = localTm(t), n = localTm(now);
    if (tm.tm_year != n.tm_year) return two(tm.tm_mday) + "/" + two(tm.tm_mon + 1) + "/" + std::to_string(tm.tm_year + 1900);
    return two(tm.tm_mday) + "/" + two(tm.tm_mon + 1) + " " + clockFr(t);
}
// << a l'instant >>, << il y a 4 min >>, << il y a 2 h >>, puis comme whenFr.
inline std::string agoFr(Seconds now, Seconds t) {
    if (t <= 0) return "\xE2\x80\x94";
    const Seconds d = now - t;
    if (d < 60) return "\xC3\xA0 l'instant";
    if (d < 3600) return "il y a " + std::to_string(d / 60) + " min";
    if (d < 6 * 3600 && sameDay(now, t)) return "il y a " + std::to_string(d / 3600) + " h";
    return whenFr(now, t);
}

// ------------------------------------------------------------ l'empreinte ----
// "B7E2 48A9 1D3C 90F6 5A2E C471" -> les 6 groupes.
inline std::vector<std::string> groups(const std::string& fingerprint) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : fingerprint) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// Les deux premiers groupes (la colonne << Sa cle >>) : "A41C 0E7B".
inline std::string shortFingerprint(const std::string& fingerprint) {
    const auto g = groups(fingerprint);
    if (g.size() < 2) return fingerprint;
    return g[0] + " " + g[1];
}

// ---------------------------------------------------------------- dessiner ----
inline Color rgb(std::uint32_t v) { return Color::rgb(v); }

// La carte de la maquette (blanche, coins de 12, un trait, une ombre a peine).
inline void cardBox(gfx::IRenderer& r, const ui::Theme& th, const Rect& box, float radius = 12.f) {
    if (box.empty()) return;
    r.fillRoundedRect({box.x, box.y + 1.f, box.w, box.h + 1.f}, th.brand.cardShadow, radius);
    r.fillRoundedRect(box, th.brand.cardBorder, radius);
    r.fillRoundedRect(box.inset(1.f, 1.f), th.brand.card, radius - 1.f);
}

// La couleur d'un admin : 0 l'admin+ (le violet de l'Admin), puis une par admin ; -1 : retire (gris).
inline Color avatarColor(const ui::Theme& th, int index) {
    if (index < 0) return rgb(0xA3ADBF);
    if (index == 0) return th.color.accent;
    static const std::uint32_t palette[] = {0x0E7490, 0xEA580C, 0xDB2777, 0x2563EB, 0x059669, 0xB45309, 0x7C3AED, 0x0F766E};
    return rgb(palette[static_cast<std::size_t>(index - 1) % (sizeof palette / sizeof palette[0])]);
}

// L'initiale d'un nom (le premier caractere, en majuscule s'il est ASCII ; << Lea >> -> << L >>).
inline std::string initialOf(const std::string& name) {
    std::size_t i = 0;
    while (i < name.size() && (name[i] == ' ' || name[i] == '(')) ++i;
    if (i >= name.size()) return "?";
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x80) return std::string(1, static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    std::size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    std::string s = name.substr(i, n);
    if (s == "\xC3\xA9" || s == "\xC3\xA8") return "\xC3\x89";   // e accentue -> E accentue
    return s;
}

inline void avatar(gfx::IRenderer& r, const Rect& box, const std::string& name, Color fill, float radius = -1.f) {
    r.fillRoundedRect(box, fill, radius < 0.f ? box.w * 0.28f : radius);
    const FontId f = gfx::semibold(FontId{static_cast<std::uint16_t>(std::max(10.f, std::min(18.f, box.h * 0.42f)))});
    const std::string ini = initialOf(name);
    r.drawText({box.x + (box.w - r.measure(ini, f).width) * 0.5f, box.y + (box.h - r.lineHeight(f)) * 0.5f}, ini, f, rgb(0xFFFFFF));
}

inline float pillWidth(const gfx::IRenderer& r, const std::string& text, FontId f, bool dot = false) {
    return r.measure(text, f).width + (dot ? 28.f : 18.f);
}
// Une pastille (le role, l'etat, la criticite) ; rend sa largeur. `dot` : un point de la couleur devant.
inline float pill(gfx::IRenderer& r, float x, float y, const std::string& text, FontId f, Color fg, Color bg, float h = 22.f, bool dot = false) {
    const float w = pillWidth(r, text, f, dot);
    r.fillRoundedRect({x, y, w, h}, bg, h * 0.5f);
    float tx = x + 9.f;
    if (dot) {
        r.fillRoundedRect({x + 9.f, y + (h - 7.f) * 0.5f, 7.f, 7.f}, fg, 3.5f);
        tx += 12.f;
    }
    r.drawText({tx, y + (h - r.lineHeight(f)) * 0.5f}, text, f, fg);
    return w;
}

struct Tint {
    Color fg, bg;
};
// Les couleurs d'une criticite (la maquette : Info gris, Moyenne bleu, Haute ambre, Critique rouge).
inline Tint levelTint(adm::Level l) {
    switch (l) {
        case adm::Level::Info: return {rgb(0x475569), rgb(0xF1F5F9)};
        case adm::Level::Medium: return {rgb(0x2563EB), rgb(0xEFF6FF)};
        case adm::Level::High: return {rgb(0xC2410C), rgb(0xFFF4E5)};
        case adm::Level::Critical: return {rgb(0xDC2626), rgb(0xFEF2F2)};
    }
    return {rgb(0x475569), rgb(0xF1F5F9)};
}
// La legende d'une criticite (planche 04).
inline const char* levelCaption(adm::Level l) {
    switch (l) {
        case adm::Level::Info: return "pour m\xC3\xA9moire";
        case adm::Level::Medium: return "\xC3\xA0 relire de temps en temps";
        case adm::Level::High: return "\xC3\xA0 regarder dans la journ\xC3\xA9" "e";
        case adm::Level::Critical: return "\xC3\xA0 regarder tout de suite";
    }
    return "";
}
inline const adm::Level kLevels[4] = {adm::Level::Info, adm::Level::Medium, adm::Level::High, adm::Level::Critical};

// Les quatre pouvoirs que l'admin+ garde (docs/ADMINS.md, par. 1) : jamais donnes.
inline const std::vector<std::string>& plusOnly() {
    static const std::vector<std::string> v = {"Ajouter, retirer un admin ; leurs droits et les profils", "Le journal des admins et sa criticit\xC3\xA9",
                                               "Publier une version, envoyer un fichier", "Le certificat du service"};
    return v;
}

// Une coche (un droit permis) et un cadenas (reserve a l'admin+), dessines.
inline void drawTick(gfx::IRenderer& r, float x, float y, Color c) {
    r.line({x, y + 5.f}, {x + 4.f, y + 9.f}, c, 1.8f);
    r.line({x + 4.f, y + 9.f}, {x + 11.f, y + 1.f}, c, 1.8f);
}
inline void drawCross(gfx::IRenderer& r, float x, float y, Color c) {
    r.line({x + 1.f, y + 1.f}, {x + 9.f, y + 9.f}, c, 1.5f);
    r.line({x + 9.f, y + 1.f}, {x + 1.f, y + 9.f}, c, 1.5f);
}

// ============================================================== les widgets ====
// UN INTERRUPTEUR (un droit permis, << Me prevenir tout de suite >>) : un ToggleButton (les scripts le cliquent :
// --cliquer), dessine en interrupteur ; son texte (le droit) n'est pas dessine - la page l'ecrit a sa gauche.
class Switch final : public ui::ToggleButton {
public:
    Switch(std::string text, std::string id) : ui::ToggleButton(std::move(text), std::move(id)) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {36.f, 22.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const Rect t{b.x + (b.w - 34.f) * 0.5f, b.y + (b.h - 20.f) * 0.5f, 34.f, 20.f};
        const bool on = checked();
        Color fill = on ? ctx.theme.color.accent : rgb(0xE2E8F0);
        if (!enabled()) fill = on ? ctx.theme.color.accent.withAlpha(110) : rgb(0xF1F5F9);
        ctx.r.fillRoundedRect(t, fill, 10.f);
        const float kx = on ? t.right() - 18.f : t.x + 2.f;
        ctx.r.fillRoundedRect({kx, t.y + 2.f, 16.f, 16.f}, rgb(0xFFFFFF), 8.f);
    }
};

// UNE PASTILLE DE FILTRE : << Tous 248 >>, << Lea 96 >> (avec l'avatar), << Info 171 >> (cochee). Choisie : l'encre
// pleine ; sinon blanche, bordee. Le texte du bouton (ce que lisent les scripts) : << Lea (96) >>.
class Chip final : public ui::Button {
public:
    explicit Chip(std::string id) : ui::Button("", std::move(id)) {}
    void set(std::string label, int count, bool on) {
        label_ = std::move(label);
        count_ = count;
        on_ = on;
        setText(count_ >= 0 ? label_ + " (" + std::to_string(count_) + ")" : label_);
        invalidateLayout();
    }
    void setAvatar(bool show, Color c) {
        avatar_ = show;
        avatarColor_ = c;
    }
    void setNote(std::string n) { note_ = std::move(n); }   // << retire >> apres le nombre
    void setTint(bool tinted, Tint t) {
        tinted_ = tinted;
        tint_ = t;
    }
    void setMuted(bool m) { muted_ = m; }
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] float wanted() const {
        const FontId fl = gfx::semibold(FontId{13}), fc{12};
        float w = std::min(kMaxLabel, ui::measureWidth(label_, fl)) + 24.f;
        if (count_ >= 0) w += ui::measureWidth(std::to_string(count_), fc) + 7.f;
        if (!note_.empty()) w += ui::measureWidth(" \xC2\xB7 " + note_, fc);
        if (avatar_) w += 24.f;
        if (tinted_) w += 16.f;
        return w;
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {wanted(), 32.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect r = bounds();
        const FontId fl = gfx::semibold(FontId{13}), fc{12};
        Color fill = on_ ? c.text : hovered() ? c.rowAltBg : c.panelBg;
        Color line = on_ ? c.text : c.border;
        Color ink = on_ ? c.textInverted : muted_ ? c.textMuted : c.text;
        if (tinted_) {
            fill = on_ ? tint_.bg : hovered() ? c.rowAltBg : c.panelBg;
            line = on_ ? tint_.fg.withAlpha(90) : c.border;
            ink = on_ ? tint_.fg : c.textMuted;
        }
        ui::shapes::fillFramedRect(ctx.r, r, fill, line, r.h * 0.5f);
        float x = r.x + 12.f;
        if (tinted_) {
            if (on_) drawTick(ctx.r, x - 1.f, r.y + (r.h - 11.f) * 0.5f, ink);
            x += 16.f;
        }
        if (avatar_) {
            kit::avatar(ctx.r, {x - 4.f, r.y + (r.h - 20.f) * 0.5f, 20.f, 20.f}, label_, muted_ ? rgb(0xA3ADBF) : avatarColor_, 10.f);
            x += 20.f;
        }
        const std::string shownLabel = fit(ctx.r, label_, fl, kMaxLabel);   // un nom tres long (la cle d'essai) : coupe
        ctx.r.drawText({x, r.y + (r.h - ctx.r.lineHeight(fl)) * 0.5f}, shownLabel, fl, ink);
        x += ctx.r.measure(shownLabel, fl).width + 7.f;
        if (count_ >= 0) {
            std::string n = std::to_string(count_);
            if (!note_.empty()) n += " \xC2\xB7 " + note_;
            ctx.r.drawText({x, r.y + (r.h - ctx.r.lineHeight(fc)) * 0.5f + 1.f}, n, fc, on_ && !tinted_ ? c.textInverted.withAlpha(190) : c.textMuted);
        }
    }

private:
    static constexpr float kMaxLabel = 150.f;
    std::string label_, note_;
    int         count_{-1};
    bool        on_{false}, avatar_{false}, tinted_{false}, muted_{false};
    Color       avatarColor_{};
    Tint        tint_{};
};

// ============================================================ les dialogues ====
inline menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// Un dialogue (un WidgetMenu par-dessus la coquille) : pose sur un MenuManager, ou seul (les essais).
class Dialog : public menu::WidgetMenu {
public:
    explicit Dialog(std::string id) : menu::WidgetMenu(std::move(id)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }

    void Update(const menu::FrameContext& fc) override {
        onTick();
        if (hosted_) {
            menu::WidgetMenu::Update(fc);
            return;
        }
        if (ui::Widget* r = widgetRoot()) {
            r->setBounds({0.f, 0.f, fc.surface.w, fc.surface.h});
            const ui::dialogframe::KeyScope key(id());
            r->layout();
        }
    }
    void Render(gfx::IRenderer& r, const menu::FrameContext& fc) override {
        if (hosted_) {
            menu::WidgetMenu::Render(r, fc);
            return;
        }
        ui::Widget* root = widgetRoot();
        if (!root || !fc.theme) return;
        std::vector<ui::Widget*> overlays;
        const ui::PaintContext ctx{r, *fc.theme, {0.f, 0.f, fc.surface.w, fc.surface.h}, fc.totalSeconds, &overlays};
        root->render(ctx);
        for (auto* w : overlays) w->paintTopMost(ctx);
    }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        const ui::EventResult r = menu::WidgetMenu::HandleEvent(ev);
        if (r == ui::EventResult::Consumed) return r;
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            close();
            return ui::EventResult::Consumed;
        }
        return r;
    }
    void close() {
        if (closed_) return;
        closed_ = true;
        if (hosted_)
            manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        else if (ui::Widget* r = widgetRoot())
            r->setVisibility(ui::Visibility::Hidden);
    }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

protected:
    void onEnter() override { hosted_ = true; }
    // A chaque image (un releve qui revient, une minuterie) : le dialogue qui en a besoin le prend.
    virtual void onTick() {}

private:
    bool hosted_{false};
    bool closed_{false};
};

// Le cadre d'un dialogue de la maquette : le panneau arrondi, le titre (et sa ligne dessous), la croix.
struct Frame {
    Rect panel{}, closeBox{};
    void place(ui::Widget& body, float wantW, float wantH) {
        const auto b = body.bounds();
        const float w = std::min(wantW, std::max(420.f, b.w - 40.f));
        const float h = std::min(wantH, std::max(300.f, b.h - 40.f));
        panel = ui::dialogframe::place(body, {std::floor(b.x + (b.w - w) * 0.5f), std::floor(b.y + (b.h - h) * 0.5f), w, h});
        closeBox = {panel.right() - 40.f, panel.y + 14.f, 26.f, 26.f};
        ui::dialogframe::keep(body, closeBox);
    }
    // Rend le bas de l'en-tete.
    float paint(gfx::IRenderer& r, const ui::Theme& th, const std::string& title, const std::string& subtitle, ui::Icon icon = ui::Icon::None) const {
        const auto& c = th.color;
        r.fillRoundedRect({panel.x, panel.y + 2.f, panel.w, panel.h + 2.f}, rgb(0x0B1222).withAlpha(40), 16.f);
        r.fillRoundedRect(panel, c.border, 16.f);
        r.fillRoundedRect(panel.inset(1.f, 1.f), c.panelBg, 15.f);
        float x = panel.x + 24.f;
        if (icon != ui::Icon::None) {
            const Rect ib{x, panel.y + 20.f, 40.f, 40.f};
            r.fillRoundedRect(ib, c.accent, 11.f);
            ui::drawIcon(r, icon, {ib.x + 11.f, ib.y + 11.f, 18.f, 18.f}, rgb(0xFFFFFF));
            x += 54.f;
        }
        const FontId ft = gfx::semibold(FontId{19});
        r.drawText({x, panel.y + 20.f}, fit(r, title, ft, panel.right() - 60.f - x), ft, c.text);
        if (!subtitle.empty()) r.drawText({x, panel.y + 46.f}, fit(r, subtitle, FontId{14}, panel.right() - 60.f - x), FontId{14}, c.textMuted);
        const float bottom = panel.y + (subtitle.empty() && icon == ui::Icon::None ? 56.f : 76.f);
        r.fillRect({panel.x + 1.f, bottom, panel.w - 2.f, 1.f}, c.border);
        r.drawText({closeBox.x + 8.f, closeBox.y + 2.f}, "\xC3\x97", FontId{19}, c.textMuted);
        return bottom;
    }
    [[nodiscard]] bool hitClose(const ui::InputEvent& ev) const {
        const auto* d = std::get_if<ui::MouseDown>(&ev);
        return d && d->button == ui::MouseButton::Left && closeBox.contains(d->pos);
    }
};

template <class T>
T& adopt(ui::Widget& parent, std::unique_ptr<T> w) {
    return static_cast<T&>(parent.addChild(std::move(w)));
}

inline ui::Button& button(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style = ui::Button::Style::Default,
                          ui::Icon icon = ui::Icon::None) {
    auto& b = adopt(parent, std::make_unique<ui::Button>(std::move(text), std::move(id)));
    b.setStyle(style);
    if (icon != ui::Icon::None) b.setIcon(icon);
    return b;
}
inline float buttonWidth(const ui::Widget& b, float minimum = 90.f) { return std::max(minimum, b.sizeHint().preferred.w + 8.f); }
inline void show(ui::Widget* w, bool on) {
    if (w) w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
}

// ---- un nom a taper (un profil : Nouveau profil..., Renommer) ----
class NameBody final : public ui::Widget {
public:
    NameBody(Dialog& owner, std::string title, std::string text, std::string initial, std::string okLabel, std::function<bool(const std::string&)> onOk)
        : ui::Widget("dialog.nom.corps"), owner_(owner), title_(std::move(title)), text_(std::move(text)), onOk_(std::move(onOk)) {
        name_ = &adopt(*this, std::make_unique<ui::InputText>("nom.texte"));
        name_->setText(std::move(initial));
        name_->setMaxLength(60);
        cancel_ = &button(*this, "Annuler", "nom.annuler");
        ok_ = &button(*this, std::move(okLabel), "nom.ok", ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.close(); });
        links_ += ok_->clicked->connect([this] { accept(); });
        links_ += name_->editingDone->connect([this](const std::string&) { accept(); });
    }

protected:
    void onLayout() override {
        frame_.place(*this, 480.f, 250.f);
        const auto& p = frame_.panel;
        name_->setBounds({p.x + 24.f, p.y + 108.f, p.w - 48.f, 34.f});
        const float by = p.bottom() - 54.f;
        const float ow = buttonWidth(*ok_, 120.f), cw = buttonWidth(*cancel_, 100.f);
        ok_->setBounds({p.right() - 24.f - ow, by, ow, 34.f});
        cancel_->setBounds({ok_->bounds().x - 8.f - cw, by, cw, 34.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& p = frame_.panel;
        const float y = frame_.paint(ctx.r, ctx.theme, title_, {});
        ctx.r.drawText({p.x + 24.f, y + 16.f}, fit(ctx.r, text_, FontId{14}, p.w - 48.f), FontId{14}, ctx.theme.color.textMuted);
        if (!error_.empty()) ctx.r.drawText({p.x + 24.f, p.y + 150.f}, fit(ctx.r, error_, FontId{13}, p.w - 48.f), FontId{13}, ctx.theme.color.error);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void accept() {
        if (done_) return;
        std::string n = name_->text();
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(n.begin());
        if (n.empty()) {
            error_ = "Donne un nom (60 caract\xC3\xA8res au plus).";
            invalidate();
            return;
        }
        done_ = true;
        auto run = onOk_;
        owner_.close();
        if (run) (void)run(n);
    }
    Dialog&                                 owner_;
    std::string                             title_, text_, error_;
    std::function<bool(const std::string&)> onOk_;
    bool                                    done_{false};
    ui::InputText*                          name_{nullptr};
    ui::Button*                             cancel_{nullptr};
    ui::Button*                             ok_{nullptr};
    Frame                                   frame_;
    core::ConnectionScope                   links_;
};

class NameDialog final : public Dialog {
public:
    NameDialog(std::string id, std::string title, std::string text, std::string initial, std::string okLabel, std::function<bool(const std::string&)> onOk)
        : Dialog("dialog." + std::move(id)), title_(std::move(title)), text_(std::move(text)), initial_(std::move(initial)), ok_(std::move(okLabel)),
          onOk_(std::move(onOk)) {}
    [[nodiscard]] std::string title() const override { return title_; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<NameBody>(*this, title_, text_, initial_, ok_, onOk_));
        return core::ok();
    }

private:
    std::string                             title_, text_, initial_, ok_;
    std::function<bool(const std::string&)> onOk_;
};

inline menu::MenuPtr makeNameDialog(std::string id, std::string title, std::string text, std::string initial, std::string okLabel,
                                    std::function<bool(const std::string&)> onOk) {
    return std::make_unique<NameDialog>(std::move(id), std::move(title), std::move(text), std::move(initial), std::move(okLabel), std::move(onOk));
}

// Un travail lent sans bruit quand la coquille le permet (un releve automatique), sinon comme les autres.
inline bool runQuietly(const ShellHost& h, const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
    if (h.runQuiet) return h.runQuiet(label, std::move(work), std::move(done));
    return h.runSlow && h.runSlow(label, std::move(work), std::move(done));
}

inline double steadySeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

} // namespace license_admin::gui::kit
