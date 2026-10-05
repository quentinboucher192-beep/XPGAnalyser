// app/ThemeGallery.cpp - la galerie des themes (lot API 6, refaite au lot API 8 :
// quarante-trois themes par famille, les tiens, chercher, creer, modifier,
// exporter, importer, corriger les contrastes).
#include "ThemeGallery.hpp"
#include "TutorialsLot8.hpp"                // Lot API 8 : didacticiels et aide (F1)

#include "../menu/MenuManager.hpp"
#include "../ui/ThemeFile.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/PathBrowse.hpp"
#include "ThemeEditor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kLabel{15};
const gfx::FontId kTitle{20};
const gfx::FontId kHead{15};

// La famille des themes de l'utilisateur, en tete de la galerie.
const std::string kMine = "Mes th\xC3\xA8mes";
// Le bouton de chaque carte (la maquette du lot API 8).
const std::string kCreate = "Cr\xC3\xA9" "er \xC3\xA0 partir de celui-ci";

std::string guil(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }

std::vector<std::string> wrap(const std::string& s, gfx::FontId f, float w) {
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

// L'ecran en miniature, dans les couleurs de `th` : la barre du haut, l'arbre
// (une ligne choisie), le code (mots-cles, chaines, commentaires), le volet de
// droite (les quatre etats) et un bouton a l'accent.
void paintMini(gfx::IRenderer& r, const gfx::Rect& m, const ui::Theme& th) {
    const auto& c = th.color;
    const auto bar = [&](float x, float y, float w, gfx::Color col) { r.fillRoundedRect({m.x + x, m.y + y, w, 3.f}, col, 1.5f); };
    r.fillRect(m, c.windowBg);
    r.fillRect({m.x, m.y, m.w, 13.f}, c.headerBg);
    r.fillRect({m.x, m.y + 13.f, m.w, 1.f}, c.border);
    r.fillRoundedRect({m.x + 4.f, m.y + 2.f, 9.f, 9.f}, c.accent, 2.f);
    bar(17.f, 5.f, m.w * 0.16f, c.text);
    bar(21.f + m.w * 0.16f, 5.f, m.w * 0.11f, c.textMuted);
    const float treeW = std::floor(m.w * 0.28f), sideW = std::floor(m.w * 0.24f);
    r.fillRect({m.x, m.y + 14.f, treeW, m.h - 14.f}, c.panelBg);
    r.fillRect({m.x + treeW, m.y + 14.f, 1.f, m.h - 14.f}, c.border);
    for (int i = 0; i < 7; ++i) {
        const float y = 20.f + 12.f * static_cast<float>(i);
        if (y + 6.f > m.h) break;
        if (i == 2) r.fillRect({m.x, m.y + y - 4.f, treeW, 11.f}, c.selectionBg);
        const float indent = i % 3 == 0 ? 4.f : 9.f;
        bar(indent, y, treeW * (0.62f - 0.08f * static_cast<float>(i % 3)), i == 2 ? c.selectionText : i == 0 ? c.text : c.textMuted);
    }
    r.fillRect({m.x + treeW + 1.f, m.y + 14.f, m.w - treeW - sideW - 1.f, m.h - 14.f}, th.brand.codeBg);
    const struct { gfx::Color a; float wa; gfx::Color b; float wb; } rows[] = {
        {c.syntaxKeyword, 0.08f, c.text, 0.14f}, {c.syntaxFunction, 0.12f, c.syntaxString, 0.10f}, {c.syntaxComment, 0.22f, c.text, 0.f},
        {c.syntaxKeyword, 0.06f, c.syntaxNumber, 0.05f}, {c.syntaxType, 0.10f, c.text, 0.12f}, {c.syntaxKeyword, 0.09f, c.text, 0.f}};
    float y = 21.f;
    for (const auto& row : rows) {
        if (y + 4.f > m.h) break;
        const float x = treeW + 6.f;
        bar(x, y, m.w * row.wa, row.a);
        if (row.wb > 0.f) bar(x + m.w * row.wa + 3.f, y, m.w * row.wb, row.b);
        y += 12.f;
    }
    const float sx = m.w - sideW;
    r.fillRect({m.x + sx, m.y + 14.f, sideW, m.h - 14.f}, c.panelBg);
    r.fillRect({m.x + sx, m.y + 14.f, 1.f, m.h - 14.f}, c.border);
    const gfx::Color states[] = {c.ok, c.warning, c.error, c.info};
    for (int i = 0; i < 4; ++i) {
        const float yy = 20.f + 12.f * static_cast<float>(i);
        if (yy + 7.f > m.h - 14.f) break;
        r.fillRoundedRect({m.x + sx + 6.f, m.y + yy, 7.f, 7.f}, states[i], 3.5f);
        bar(sx + 17.f, yy + 2.f, sideW - 24.f, c.textMuted);
    }
    r.fillRoundedRect({m.x + m.w - sideW * 0.7f - 4.f, m.y + m.h - 13.f, sideW * 0.7f, 9.f}, c.accent, 3.f);
    r.strokeRect(m, c.borderStrong.withAlpha(90), 1.f);
}

bool contains(const gfx::Rect& r, gfx::Point p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

} // namespace

// ------------------------------------------------------------------ le corps ---
// Le volet : l'en-tete (titre, recherche, familles), la liste qui defile (un
// intertitre par famille, trois cartes par rang), ce que dit la galerie (un
// message, les contrastes du theme choisi), les gestes, Revenir / Garder.
class ThemeGalleryDialog::Body final : public ui::Widget {
public:
    struct Card {
        ui::Theme::Entry entry;
        ui::Theme        theme;
    };
    struct Item {            // une ligne de la liste : un intertitre, ou une carte
        bool        heading{false};
        std::string text;    // l'intertitre
        std::size_t card{0};
        gfx::Rect   rect{};  // dans la liste, avant le defilement
    };

    explicit Body(ThemeGalleryDialog& owner) : d_(owner) { reload(); }

    std::string              family;     // "" : toutes ; kMine : les tiens
    std::string              search;
    std::vector<ui::Button*> bottom;     // Revenir, Garder
    std::vector<ui::Button*> actions;    // les gestes
    ui::InputText*           searchField{nullptr};
    ui::Button*              fix{nullptr};
    // La question du nom (Nouveau, Dupliquer, Renommer, Modifier un integre).
    ui::InputText*           nameField{nullptr};
    ui::Button*              nameOk{nullptr};
    ui::Button*              nameCancel{nullptr};
    std::string              askTitle;
    std::function<void(const std::string&)> askDone;
    // Supprimer demande un second clic.
    std::string              deletePending;
    double                   deleteUntil{0};

    void reload() {
        cards_.clear();
        for (auto& e : ui::Theme::all()) {
            Card c;
            c.theme = ui::Theme::byName(e.key);
            c.entry = std::move(e);
            cards_.push_back(std::move(c));
        }
        refilter();
    }

    [[nodiscard]] bool asking() const noexcept { return static_cast<bool>(askDone); }

    [[nodiscard]] std::vector<std::string> familyChips() const {
        std::vector<std::string> out{"Toutes"};
        if (!ui::Theme::userThemes().empty()) out.push_back(kMine);
        for (const auto& f : ui::Theme::familyLabels()) out.push_back(f);
        return out;
    }
    // Le nombre de themes d'une pastille (la maquette : "Sombres 9").
    [[nodiscard]] std::size_t chipCount(const std::string& f) const {
        if (f == "Toutes") return cards_.size();
        if (f == kMine) return ui::Theme::userThemes().size();
        std::size_t n = 0;
        for (const auto& c : cards_)
            if (!c.entry.user && ui::Theme::fold(c.entry.family) == ui::Theme::fold(f)) ++n;
        return n;
    }

    // Les cartes montrees, rangees par famille (les tiens d'abord).
    void refilter() {
        items_.clear();
        const std::string needle = ui::Theme::fold(search);
        std::vector<std::string> groups{kMine};
        for (const auto& f : ui::Theme::familyLabels()) groups.push_back(f);
        for (const auto& g : groups) {
            if (!family.empty() && family != g) continue;
            std::vector<std::size_t> in;
            for (std::size_t i = 0; i < cards_.size(); ++i) {
                const auto& e = cards_[i].entry;
                const bool mine = g == kMine;
                if (mine != e.user) continue;
                if (!mine && ui::Theme::fold(e.family) != ui::Theme::fold(g)) continue;
                if (!needle.empty()
                    && ui::Theme::fold(e.label + " " + e.key + " " + e.family + " " + e.description).find(needle) == std::string::npos)
                    continue;
                in.push_back(i);
            }
            if (in.empty()) continue;
            Item h;
            h.heading = true;
            h.text = g + "  (" + std::to_string(in.size()) + ")";
            items_.push_back(h);
            for (const auto i : in) {
                Item c;
                c.card = i;
                items_.push_back(c);
            }
        }
        scroll_ = 0.f;
        placeItems();
        invalidate();
    }

    [[nodiscard]] std::vector<std::string> visibleKeys() const {
        std::vector<std::string> out;
        for (const auto& it : items_)
            if (!it.heading) out.push_back(cards_[it.card].entry.key);
        return out;
    }
    [[nodiscard]] const Card* card(const std::string& key) const {
        for (const auto& c : cards_)
            if (c.entry.key == key) return &c;
        return nullptr;
    }
    // Amene la carte `key` dans la vue.
    void reveal(const std::string& key) {
        if (list_.h <= 0.f) {            // pas encore mise en page : a la premiere
            pendingReveal_ = key;
            return;
        }
        for (const auto& it : items_) {
            if (it.heading || cards_[it.card].entry.key != key) continue;
            if (it.rect.y - scroll_ < 0.f) scroll_ = it.rect.y - 30.f;
            else if (it.rect.y + it.rect.h - scroll_ > list_.h) scroll_ = it.rect.y + it.rect.h - list_.h + 8.f;
            clampScroll();
            invalidate();
            return;
        }
    }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(900.f, b.w - 20.f);
        panel_ = {b.x + b.w - w, b.y + 48.f, w, std::max(260.f, b.h - 48.f - 24.f)};
        const float x0 = panel_.x + 16.f;
        if (searchField) searchField->setBounds({x0, panel_.y + 66.f, 240.f, 28.f});
        // Les pastilles des familles, a droite de la recherche, sur une ou deux lignes.
        chipRects_.clear();
        float cx = x0 + 252.f, cy = panel_.y + 68.f;
        for (const auto& f : familyChips()) {
            const float cw = ui::measureWidth(f + "  " + std::to_string(chipCount(f)), kSmall) + 20.f;
            if (cx + cw > panel_.x + panel_.w - 16.f) {
                cx = x0 + 252.f;
                cy += 28.f;
            }
            chipRects_.push_back({{cx, cy, cw, 24.f}, f});
            cx += cw + 6.f;
        }
        const float top = std::max(panel_.y + 104.f, cy + 34.f);
        // Du bas vers le haut : Revenir / Garder, les gestes, ce que dit la galerie.
        const float bottomY = panel_.y + panel_.h - 46.f;
        float bx = panel_.x + panel_.w - 16.f;
        for (auto it = bottom.rbegin(); it != bottom.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, bottomY, bw, 32.f});
            bx -= 10.f;
        }
        const float actionY = bottomY - 44.f;
        float ax = x0;
        for (auto* a : actions) {
            const float aw = ui::measureWidth(a->text(), gfx::FontId{16}) + 26.f;
            a->setBounds({ax, actionY, aw, 30.f});
            a->setVisibility(asking() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            ax += aw + 6.f;
        }
        if (nameField && nameOk && nameCancel) {
            const auto v = asking() ? ui::Visibility::Visible : ui::Visibility::Collapsed;
            const float lw = ui::measureWidth(askTitle, kLabel) + 14.f;
            const float okW = ui::measureWidth(nameOk->text(), gfx::FontId{16}) + 30.f;
            const float caW = ui::measureWidth(nameCancel->text(), gfx::FontId{16}) + 30.f;
            const float fw = std::max(160.f, panel_.w - 32.f - lw - okW - caW - 20.f);
            nameField->setBounds({x0 + lw, actionY, fw, 30.f});
            nameOk->setBounds({x0 + lw + fw + 10.f, actionY, okW, 30.f});
            nameCancel->setBounds({x0 + lw + fw + 16.f + okW, actionY, caW, 30.f});
            nameField->setVisibility(v);
            nameOk->setVisibility(v);
            nameCancel->setVisibility(v);
        }
        messageY_ = actionY - 34.f;
        if (fix) {
            const float fw = ui::measureWidth(fix->text(), gfx::FontId{16}) + 30.f;
            fix->setBounds({panel_.x + panel_.w - 16.f - fw, messageY_ - 2.f, fw, 28.f});
        }
        list_ = {panel_.x, top, panel_.w, std::max(80.f, messageY_ - 8.f - top)};
        placeItems();
        if (!pendingReveal_.empty()) {
            const std::string key = std::move(pendingReveal_);
            pendingReveal_.clear();
            reveal(key);
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect({panel_.x - 1.f, panel_.y, 1.f, panel_.h}, c.borderStrong);
        r.fillRect(panel_, c.panelBg);
        const std::string title = "Th\xC3\xA8mes";
        r.drawText({panel_.x + 18.f, panel_.y + 12.f}, title, kTitle, c.text);
        r.drawText({panel_.x + 18.6f, panel_.y + 12.f}, title, kTitle, c.text);
        const std::size_t mine = ui::Theme::userThemes().size();
        std::string count = std::to_string(cards_.size() - mine) + " int\xC3\xA9gr\xC3\xA9s";
        if (mine > 0) count += "  \xC2\xB7  " + std::to_string(mine) + " dans " + kMine;
        r.drawText({panel_.x + 30.f + ui::measureWidth(title, kTitle), panel_.y + 17.f}, count, kSmall, c.textMuted);
        r.drawText({panel_.x + 18.f, panel_.y + 42.f},
                   "Un clic l'applique \xC3\xA0 tout l'\xC3\xA9" "cran. Garder le retient ; Revenir (\xC3\x89" "chap) remet le pr\xC3\xA9" "c\xC3\xA9"
                   "dent. Un fichier .xpgtheme l\xC3\xA2" "ch\xC3\xA9 ici est import\xC3\xA9.",
                   kSmall, c.textMuted);
        // Les familles.
        for (const auto& [rc, f] : chipRects_) {
            const bool on = f == "Toutes" ? family.empty() : family == f;
            r.fillRoundedRect(rc, on ? c.selectionBg : ctx.theme.brand.card, 12.f);
            if (!on) r.strokeRect(rc, c.border, 1.f);
            r.drawText({rc.x + 10.f, rc.y + 4.f}, f, kSmall, on ? c.selectionText : c.text);
            r.drawText({rc.x + 10.f + ui::measureWidth(f + "  ", kSmall), rc.y + 4.f}, std::to_string(chipCount(f)), kSmall,
                       on ? c.selectionText : c.textMuted);
        }
        // La liste.
        const std::string cur = d_.hosts_.current ? d_.hosts_.current() : std::string{};
        r.pushClip(list_);
        if (items_.empty())
            r.drawText({list_.x + 18.f, list_.y + 12.f}, "Aucun th\xC3\xA8me ne correspond : change la famille ou la recherche.", kSmall, c.textMuted);
        for (const auto& it : items_) {
            const gfx::Rect rc{it.rect.x, it.rect.y + list_.y - scroll_, it.rect.w, it.rect.h};
            if (rc.y + rc.h < list_.y || rc.y > list_.y + list_.h) continue;
            if (it.heading) {
                r.drawText({rc.x, rc.y + 6.f}, it.text, kHead, c.text);
                r.fillRect({rc.x, rc.y + rc.h - 3.f, rc.w, 1.f}, c.border);
                continue;
            }
            paintCard(ctx, rc, cards_[it.card], cur == cards_[it.card].entry.key, static_cast<int>(it.card) == hover_);
        }
        r.popClip();
        // Ou l'on est dans la liste (1.11.4 : la barre se tire).
        sbar_.paint(ctx, {panel_.x, list_.y, panel_.w, list_.h}, contentH_, list_.h, scroll_);
        r.fillRect({panel_.x, list_.y + list_.h + 2.f, panel_.w, 1.f}, c.border);
        // Ce que dit la galerie : le message, sinon les contrastes du theme choisi.
        std::string line = d_.message_;
        gfx::Color lineColor = d_.messageError_ ? ctx.theme.onSurface(c.error) : c.textMuted;
        if (line.empty()) {
            if (const Card* k = card(cur)) {
                const auto fails = ui::contrastFailures(k->theme);
                if (!fails.empty()) {
                    line = std::to_string(fails.size()) + (fails.size() > 1 ? " contrastes insuffisants : " : " contraste insuffisant : ");
                    for (std::size_t i = 0; i < fails.size() && i < 3; ++i)
                        line += (i ? ", " : "") + fails[i].label + " " + ui::contrastText(fails[i].ratio) + " (il faut " + ui::contrastText(fails[i].need) + ")";
                    if (fails.size() > 3) line += "...";
                    lineColor = ctx.theme.onSurface(c.warning);
                } else {
                    line = guil(k->entry.label) + " : " + (k->entry.user ? kMine : k->entry.family) + ", tous les contrastes tenus (texte "
                         + ui::contrastText(ui::contrastRatio(k->theme.color.text, k->theme.color.panelBg)) + "). Script : theme \"" + k->entry.label + "\"";
                }
            }
        }
        const float maxW = fix && fix->visible() ? fix->bounds().x - panel_.x - 30.f : panel_.w - 36.f;
        const auto lines = wrap(line, kSmall, maxW);
        float ly = messageY_ + (lines.size() > 1 ? -4.f : 4.f);
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
            r.drawText({panel_.x + 18.f, ly}, lines[i], kSmall, lineColor);
            ly += r.lineHeight(kSmall) + 1.f;
        }
        if (asking()) r.drawText({panel_.x + 16.f, actions.empty() ? 0.f : actions.front()->bounds().y + 6.f}, askTitle, kLabel, c.text);
        r.fillRect({panel_.x, panel_.y + panel_.h - 58.f, panel_.w, 1.f}, c.border);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        {
            float off = scroll_;   // 1.11.4 : la barre de defilement se tire
            if (sbar_.handle(*this, ev, {panel_.x, list_.y, panel_.w, list_.h}, contentH_, list_.h, off)) {
                scroll_ = off;
                clampScroll();
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!contains(list_, w->pos)) return ui::EventResult::Ignored;
            scroll_ -= w->dy * 60.f;
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = cardAt(m->pos);
            if (const int part = h >= 0 ? partAt(h, m->pos) : 0; part != hoverPart_) {
                hoverPart_ = part;
                invalidate();
            }
            if (h != hover_) {
                hover_ = h;
                const auto* k = h >= 0 ? &cards_[static_cast<std::size_t>(h)] : nullptr;
                setTooltip(k ? k->entry.label + " (" + (k->entry.user ? kMine : k->entry.family) + ") : " + k->entry.description : std::string{});
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
            for (const auto& [rc, f] : chipRects_) {
                if (!contains(rc, m->pos)) continue;
                family = f == "Toutes" ? std::string{} : f;
                refilter();
                return ui::EventResult::Consumed;
            }
            pressed_ = cardAt(m->pos);
            pressedPart_ = pressed_ >= 0 ? partAt(pressed_, m->pos) : 0;
            return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        if (const auto* m = std::get_if<ui::MouseUp>(&ev)) {
            const int was = pressed_;
            pressed_ = -1;
            if (was >= 0 && cardAt(m->pos) == was && partAt(was, m->pos) == pressedPart_) {
                const auto& e = cards_[static_cast<std::size_t>(was)].entry;
                const std::string key = e.key, label = e.label;     // la liste peut changer dessous
                if (pressedPart_ == 1) {
                    // Creer a partir de celui-ci : une copie a toi, et l'editeur s'ouvre.
                    d_.createFrom(ui::freeThemeName(label + " (copie)"), key, true);
                } else if (pressedPart_ == 2) {
                    d_.choose(key);
                    d_.onExport();
                } else {
                    d_.choose(key);
                }
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    void placeItems() {
        const float pad = 16.f, gap = 10.f;
        const float w = std::floor((list_.w - 2.f * pad - 2.f * gap - 8.f) / 3.f);
        const float h = 232.f;       // la miniature, le nom, la description, les contrastes, les boutons
        float y = 6.f;
        int col = 0;
        for (auto& it : items_) {
            if (it.heading) {
                if (col != 0) y += h + gap;
                col = 0;
                it.rect = {list_.x + pad, y + 4.f, list_.w - 2.f * pad - 8.f, 28.f};
                y += 38.f;
                continue;
            }
            it.rect = {list_.x + pad + static_cast<float>(col) * (w + gap), y, w, h};
            if (++col == 3) {
                col = 0;
                y += h + gap;
            }
        }
        if (col != 0) y += h + gap;
        contentH_ = y + 6.f;
        clampScroll();
    }
    void clampScroll() { scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH_ - list_.h)); }
    [[nodiscard]] int cardAt(gfx::Point p) const {
        if (!contains(list_, p)) return -1;
        for (const auto& it : items_) {
            if (it.heading) continue;
            const gfx::Rect rc{it.rect.x, it.rect.y + list_.y - scroll_, it.rect.w, it.rect.h};
            if (contains(rc, p)) return static_cast<int>(it.card);
        }
        return -1;
    }
    void paintCard(const ui::PaintContext& ctx, const gfx::Rect& rc, const Card& k, bool on, bool hot) const {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        if (on) r.fillRoundedRect({rc.x - 3.f, rc.y - 3.f, rc.w + 6.f, rc.h + 6.f}, c.accent.withAlpha(70), 10.f);
        r.fillRoundedRect(rc, ctx.theme.brand.card, 8.f);
        r.strokeRect(rc, on ? c.accent : hot ? c.borderStrong : c.border, on ? 2.f : 1.f);
        const gfx::Rect mini{rc.x + 7.f, rc.y + 7.f, rc.w - 14.f, 84.f};
        paintMini(r, mini, k.theme);
        if (on) {
            const gfx::Rect dot{rc.x + rc.w - 28.f, rc.y + 12.f, 18.f, 18.f};
            r.fillRoundedRect(dot, c.accent, 9.f);
            ui::drawIcon(r, ui::Icon::Ok, {dot.x + 3.f, dot.y + 3.f, 12.f, 12.f}, c.selectionText);
        }
        float y = mini.y + mini.h + 6.f;
        std::string label = k.entry.label;
        while (label.size() > 4 && ui::measureWidth(label, kLabel) > rc.w - 20.f) {
            label.pop_back();
            while (!label.empty() && (static_cast<unsigned char>(label.back()) & 0xC0) == 0x80) label.pop_back();
        }
        if (label != k.entry.label) label += "...";
        r.drawText({rc.x + 10.f, y}, label, kLabel, c.text);
        r.drawText({rc.x + 10.6f, y}, label, kLabel, c.text);
        y += r.lineHeight(kLabel) + 3.f;
        // Lot API 8 (la maquette) : la description, les deux contrastes, "lisible",
        // et sur chaque carte "Creer a partir de celui-ci" et Exporter.
        const auto desc = wrap(k.entry.user && k.entry.description.empty() ? std::string("Un de tes th\xC3\xA8mes.") : k.entry.description,
                               gfx::FontId{12}, rc.w - 20.f);
        for (std::size_t i = 0; i < desc.size() && i < 2; ++i) {
            r.drawText({rc.x + 10.f, y}, i == 1 && desc.size() > 2 ? desc[i] + "..." : desc[i], gfx::FontId{12}, c.textMuted);
            y += r.lineHeight(gfx::FontId{12}) + 1.f;
        }
        y = std::max(y, mini.y + mini.h + 6.f + r.lineHeight(kLabel) + 3.f + 2.f * (r.lineHeight(gfx::FontId{12}) + 1.f)) + 3.f;
        const auto fails = ui::contrastFailures(k.theme).size();
        const std::string ratios = "Texte " + ui::contrastText(ui::contrastRatio(k.theme.color.text, k.theme.color.panelBg)) + "   Secondaire "
                                 + ui::contrastText(ui::contrastRatio(k.theme.color.textMuted, k.theme.color.panelBg));
        r.drawText({rc.x + 10.f, y}, ratios, gfx::FontId{12}, c.text);
        const std::string badge = fails ? std::to_string(fails) + (fails > 1 ? " contrastes faibles" : " contraste faible") : std::string("lisible");
        const gfx::Color tone = fails ? c.warning : c.ok;
        const float bw = ui::measureWidth(badge, gfx::FontId{12}) + 12.f;
        float bxx = rc.x + 18.f + ui::measureWidth(ratios, gfx::FontId{12});
        float by = y - 1.f;
        if (bxx + bw > rc.x + rc.w - 8.f) {      // pas la place : a la ligne
            bxx = rc.x + 10.f;
            by += r.lineHeight(gfx::FontId{12}) + 3.f;
        }
        r.fillRoundedRect({bxx, by, bw, 17.f}, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 3.f);
        r.drawText({bxx + 6.f, by + 1.f}, badge, gfx::FontId{12}, ctx.theme.onSurface(tone));
        const gfx::Rect cr = createRect(rc), er = exportRect(rc);
        const bool hotCreate = hot && hoverPart_ == 1, hotExport = hot && hoverPart_ == 2;
        r.fillRoundedRect(cr, hotCreate ? ctx.theme.brand.hover : c.panelBg, 5.f);
        r.strokeRect(cr, hotCreate ? c.accent : c.borderStrong, 1.f);
        r.fillRect({cr.x + 9.f, cr.y + cr.h * 0.5f - 0.75f, 10.f, 1.5f}, c.text);
        r.fillRect({cr.x + 13.25f, cr.y + cr.h * 0.5f - 5.f, 1.5f, 10.f}, c.text);
        r.drawText({cr.x + 26.f, cr.y + 5.f}, kCreate, kSmall, c.text);
        r.fillRoundedRect(er, hotExport ? ctx.theme.brand.hover : c.panelBg, 5.f);
        r.strokeRect(er, hotExport ? c.accent : c.borderStrong, 1.f);
        ui::drawIcon(r, ui::Icon::Export, {er.x + 5.f, er.y + 5.f, er.w - 10.f, er.h - 10.f}, c.text);
    }
    // Les deux boutons du bas d'une carte.
    [[nodiscard]] static gfx::Rect createRect(const gfx::Rect& rc) {
        return {rc.x + 10.f, rc.y + rc.h - 36.f, std::min(rc.w - 56.f, ui::measureWidth(kCreate, kSmall) + 36.f), 26.f};
    }
    [[nodiscard]] static gfx::Rect exportRect(const gfx::Rect& rc) {
        const gfx::Rect cr = createRect(rc);
        return {cr.x + cr.w + 8.f, cr.y, 26.f, 26.f};
    }
    // 0 : la carte ; 1 : Creer a partir de celui-ci ; 2 : Exporter.
    [[nodiscard]] int partAt(int card, gfx::Point p) const {
        for (const auto& it : items_) {
            if (it.heading || static_cast<int>(it.card) != card) continue;
            const gfx::Rect rc{it.rect.x, it.rect.y + list_.y - scroll_, it.rect.w, it.rect.h};
            if (contains(createRect(rc), p)) return 1;
            if (contains(exportRect(rc), p)) return 2;
            return 0;
        }
        return 0;
    }

    ThemeGalleryDialog& d_;
    std::vector<Card>   cards_;
    std::vector<Item>   items_;
    std::vector<std::pair<gfx::Rect, std::string>> chipRects_;
    std::string pendingReveal_;
    gfx::Rect panel_{}, list_{};
    float     scroll_{0.f}, contentH_{0.f}, messageY_{0.f};
    ui::EdgeScrollBar sbar_;   // 1.11.4
    int       hover_{-1}, pressed_{-1};
    int       hoverPart_{0}, pressedPart_{0};    // 1 : Creer a partir de celui-ci ; 2 : Exporter
};

// ------------------------------------------------------------- le dialogue ---
double ThemeGalleryDialog::contrast(gfx::Color text, gfx::Color background) { return ui::contrastRatio(text, background); }

std::string ThemeGalleryDialog::contrastText(double ratio) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f:1", ratio);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

ThemeGalleryDialog::ThemeGalleryDialog(Hosts hosts)
    : menu::WidgetMenu("dialog.theme"), hosts_(std::move(hosts)), alive_(std::make_shared<char>('t')) {
    original_ = hosts_.current ? hosts_.current() : std::string("Light");
    shown_ = original_;
}

ThemeGalleryDialog::~ThemeGalleryDialog() = default;

menu::MenuTraits ThemeGalleryDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = false;       // on regarde l'ecran prendre le theme
    return t;
}

std::string ThemeGalleryDialog::title() const { return "Th\xC3\xA8mes"; }

core::Status ThemeGalleryDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    search_ = &static_cast<ui::InputText&>(body->addChild(std::make_unique<ui::InputText>("dialog.theme.search")));
    search_->setPlaceholder("Chercher : nord, atelier...");
    body->searchField = search_;
    links_ += search_->textChanged->connect([this](const std::string& t) {
        body_->search = t;
        body_->refilter();
    });
    const struct { const char* text; const char* id; void (ThemeGalleryDialog::*act)(); const char* tip; } acts[] = {
        {"Nouveau", "dialog.theme.new", &ThemeGalleryDialog::onNew, "Un nouveau th\xC3\xA8me \xC3\xA0 partir de celui-ci, ouvert dans l'\xC3\xA9" "diteur."},
        {"Modifier", "dialog.theme.edit", &ThemeGalleryDialog::onEdit, "Changer ses couleurs. Un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9 est d'abord dupliqu\xC3\xA9 : il suffit de lui donner un nom."},
        {"Dupliquer", "dialog.theme.duplicate", &ThemeGalleryDialog::onDuplicate, "Une copie \xC3\xA0 toi, sous un autre nom."},
        {"Renommer", "dialog.theme.rename", &ThemeGalleryDialog::onRename, "Les tiens seulement."},
        {"Supprimer", "dialog.theme.delete", &ThemeGalleryDialog::onDelete, "Les tiens seulement : le fichier est retir\xC3\xA9."},
        {"Exporter...", "dialog.theme.export", &ThemeGalleryDialog::onExport, "Un fichier .xpgtheme, \xC3\xA0 envoyer \xC3\xA0 un coll\xC3\xA8gue."},
        {"Importer...", "dialog.theme.import", &ThemeGalleryDialog::onImport, "Un fichier .xpgtheme (tu peux aussi le l\xC3\xA2" "cher sur la galerie)."},
    };
    for (const auto& a : acts) {
        auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(a.text, a.id)));
        b->setTooltip(a.tip);
        const auto act = a.act;
        links_ += b->clicked->connect([this, act] { (this->*act)(); });
        actions_.push_back(b);
    }
    body->actions = actions_;
    fix_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Corriger les contrastes", "dialog.theme.fix")));
    fix_->setTooltip("Les couleurs fautives \xC3\xA9" "claircies ou assombries, leur teinte gard\xC3\xA9" "e.");
    links_ += fix_->clicked->connect([this] { fixContrastsOf({}); });
    body->fix = fix_;
    body->nameField = &static_cast<ui::InputText&>(body->addChild(std::make_unique<ui::InputText>("dialog.theme.name")));
    body->nameField->setMaxLength(60);
    body->nameOk = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Valider", "dialog.theme.name.ok")));
    body->nameOk->setStyle(ui::Button::Style::Primary);
    body->nameCancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", "dialog.theme.name.cancel")));
    links_ += body->nameOk->clicked->connect([this] {
        auto done = std::move(body_->askDone);
        body_->askDone = nullptr;
        const std::string name = body_->nameField->text();
        root().invalidateLayout();
        if (done) done(name);
    });
    links_ += body->nameCancel->clicked->connect([this] {
        body_->askDone = nullptr;
        root().invalidateLayout();
    });
    back_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("", "dialog.theme.back")));
    keep_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("", "dialog.theme.keep")));
    keep_->setStyle(ui::Button::Style::Primary);
    body->bottom = {back_, keep_};
    links_ += back_->clicked->connect([this] { finish(false); });
    links_ += keep_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    reload();
    return core::ok();
}

void ThemeGalleryDialog::relabel() {
    if (!back_ || !keep_) return;
    shown_ = hosts_.current ? hosts_.current() : shown_;
    back_->setText("Revenir \xC3\xA0 " + guil(ui::Theme::labelOf(original_)));
    keep_->setText("Garder " + guil(ui::Theme::labelOf(shown_)));
    // Ce qui ne vaut que pour les tiens.
    bool mine = false, failing = false;
    for (const auto& t : ui::Theme::userThemes()) {
        if (t.name != shown_) continue;
        mine = true;
        failing = !ui::contrastFailures(t).empty();
    }
    if (actions_.size() == 7) {
        actions_[3]->setEnabled(mine);
        actions_[4]->setEnabled(mine);
    }
    if (fix_) fix_->setVisibility(mine && failing ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (body_) body_->reveal(shown_);
    root().invalidateLayout();
    root().invalidate();
}

void ThemeGalleryDialog::reload() {
    userCount_ = ui::Theme::userThemes().size();
    if (body_) body_->reload();
    relabel();
}

void ThemeGalleryDialog::say(std::string text, bool error) {
    message_ = std::move(text);
    messageError_ = error;
    messageUntil_ = now_ + (error ? 12.0 : 8.0);
    root().invalidate();
}

std::string ThemeGalleryDialog::chosenKey() const { return hosts_.current ? hosts_.current() : shown_; }

std::string ThemeGalleryDialog::startFolder() const {
    const std::string f = hosts_.folder ? hosts_.folder() : std::string{};
    return f.empty() ? ui::UserThemes::folder() : f;
}

bool ThemeGalleryDialog::choose(const std::string& name) {
    const auto key = ui::Theme::keyOf(name);
    if (key.empty() || !hosts_.apply) return false;
    hosts_.apply(key);
    if (body_) body_->deletePending.clear();
    relabel();
    return true;
}

bool ThemeGalleryDialog::setFamily(const std::string& family) {
    if (!body_) return false;
    const std::string f = ui::Theme::fold(family);
    if (f.empty() || f == "tous" || f == "toutes") {
        body_->family.clear();
    } else if (f == ui::Theme::fold(kMine) || f == "mesthemes" || f == "lestiens" || f == "atoi") {
        body_->family = kMine;
    } else {
        const std::string label = ui::Theme::familyOf(family);
        if (label.empty()) return false;
        body_->family = label;
    }
    body_->refilter();
    return true;
}

void ThemeGalleryDialog::setSearch(const std::string& text) {
    if (search_) search_->setText(text);
    if (body_) {
        body_->search = text;
        body_->refilter();
    }
}

std::vector<std::string> ThemeGalleryDialog::visibleKeys() const { return body_ ? body_->visibleKeys() : std::vector<std::string>{}; }

bool ThemeGalleryDialog::createFrom(const std::string& name, const std::string& source, bool edit) {
    const std::string before = chosenKey();      // ce que l'editeur remet s'il est annule
    const std::string from = ui::Theme::keyOf(source.empty() ? chosenKey() : source);
    if (from.empty()) {
        say("Aucun th\xC3\xA8me ne s'appelle " + guil(source) + ".", true);
        return false;
    }
    if (const auto problem = ui::themeNameProblem(name); !problem.empty()) {
        say(problem, true);
        return false;
    }
    ui::Theme t = ui::Theme::byName(from);
    if (!t.user) t.base = from;
    t.user = true;
    t.name = ui::freeThemeName(name);
    std::string error;
    if (!ui::UserThemes::save(t, &error)) {
        say("Impossible d'enregistrer " + guil(t.name) + " : " + error, true);
        return false;
    }
    reload();
    if (hosts_.apply) hosts_.apply(t.name);
    relabel();
    say(guil(t.name) + " cr\xC3\xA9\xC3\xA9 \xC3\xA0 partir de " + guil(ui::Theme::labelOf(from))
        + (t.name != name ? " (" + guil(name) + " \xC3\xA9tait pris)" : std::string{}) + ".");
    if (edit) openEditor(t.name, before, true);
    return true;
}

bool ThemeGalleryDialog::renameTheme(const std::string& from, const std::string& to) {
    const std::string key = ui::Theme::keyOf(from);
    std::string error;
    if (key.empty() || !ui::UserThemes::rename(key, to, &error)) {
        say(error.empty() ? "Aucun de tes th\xC3\xA8mes ne s'appelle " + guil(from) + "." : error, true);
        return false;
    }
    const bool wasShown = chosenKey() == key;
    reload();
    if (wasShown && hosts_.apply) hosts_.apply(to);
    if (original_ == key) original_ = to;
    relabel();
    say(guil(key) + " s'appelle maintenant " + guil(to) + ".");
    return true;
}

bool ThemeGalleryDialog::deleteTheme(const std::string& name) {
    const std::string key = ui::Theme::keyOf(name);
    std::string error;
    const bool wasShown = chosenKey() == key;
    if (key.empty() || !ui::UserThemes::remove(key, &error)) {
        say(error.empty() ? "Aucun de tes th\xC3\xA8mes ne s'appelle " + guil(name) + "." : error, true);
        return false;
    }
    if (original_ == key) original_ = "Light";
    reload();
    if (wasShown && hosts_.apply) hosts_.apply(original_);
    relabel();
    say(guil(key) + " supprim\xC3\xA9, et son fichier retir\xC3\xA9.");
    return true;
}

bool ThemeGalleryDialog::exportTo(const std::string& name, const std::string& path) {
    std::string error;
    if (!ui::UserThemes::exportTheme(name, path, &error)) {
        say("Export impossible : " + error, true);
        return false;
    }
    say(guil(ui::Theme::labelOf(name)) + " export\xC3\xA9 : " + path);
    return true;
}

bool ThemeGalleryDialog::importFrom(const std::string& path) {
    const auto r = ui::UserThemes::importFile(path);
    if (!r.ok) {
        say("Fichier refus\xC3\xA9" + (r.line > 0 ? " (ligne " + std::to_string(r.line) + ")" : std::string{}) + " : " + r.error, true);
        return false;
    }
    reload();
    if (hosts_.apply) hosts_.apply(r.name);
    relabel();
    std::string text = guil(r.name) + " import\xC3\xA9";
    if (!r.renamedFrom.empty()) text += " (" + guil(r.renamedFrom) + " existait d\xC3\xA9j\xC3\xA0)";
    if (r.missing > 0) text += ", " + std::to_string(r.missing) + " couleur(s) prise(s) \xC3\xA0 sa base";
    if (!r.notes.empty()) text += ", " + std::to_string(r.notes.size()) + " ligne(s) ignor\xC3\xA9" "e(s)";
    const ui::Theme t = ui::Theme::byName(r.name);
    const auto fails = ui::contrastFailures(t);
    if (!fails.empty()) text += ". " + std::to_string(fails.size()) + " contraste(s) insuffisant(s) : Corriger les contrastes les ajuste";
    say(text + ".", false);
    return true;
}

bool ThemeGalleryDialog::fixContrastsOf(const std::string& name) {
    const std::string key = ui::Theme::keyOf(name.empty() ? chosenKey() : name);
    for (const auto& u : ui::Theme::userThemes()) {
        if (u.name != key) continue;
        ui::Theme t = u;
        const auto changed = ui::fixContrasts(t);
        std::string error;
        if (!changed.empty() && !ui::UserThemes::save(t, &error)) {
            say("Impossible d'enregistrer " + guil(key) + " : " + error, true);
            return false;
        }
        reload();
        if (hosts_.apply && chosenKey() == key) hosts_.apply(key);   // les couleurs corrigees, a l'ecran
        relabel();
        const auto left = ui::contrastFailures(ui::Theme::byName(key)).size();
        say(changed.empty() ? "Rien \xC3\xA0 corriger." :
            std::to_string(changed.size()) + " couleur(s) corrig\xC3\xA9" "e(s), teinte gard\xC3\xA9" "e"
                + (left ? " ; " + std::to_string(left) + " r\xC3\xA8gle(s) encore \xC3\xA0 revoir." : std::string(".")));
        return true;
    }
    say("Seuls tes th\xC3\xA8mes se corrigent : duplique d'abord " + guil(ui::Theme::labelOf(key)) + ".", true);
    return false;
}

bool ThemeGalleryDialog::edit(const std::string& name, const std::string& copyName) {
    const std::string key = ui::Theme::keyOf(name.empty() ? chosenKey() : name);
    if (key.empty()) {
        say("Aucun th\xC3\xA8me ne s'appelle " + guil(name) + ".", true);
        return false;
    }
    if (!ui::Theme::isBuiltIn(key)) {
        if (hosts_.apply) hosts_.apply(key);
        relabel();
        openEditor(key);
        return true;
    }
    return createFrom(copyName.empty() ? ui::freeThemeName(ui::Theme::labelOf(key) + " (perso)") : copyName, key, true);
}

void ThemeGalleryDialog::openEditor(const std::string& name, const std::string& before, bool fresh) {
    const auto& mine = ui::Theme::userThemes();
    const auto it = std::find_if(mine.begin(), mine.end(), [&](const ui::Theme& u) { return u.name == name; });
    if (it == mine.end()) {
        say("Seuls tes th\xC3\xA8mes se modifient : duplique d'abord " + guil(ui::Theme::labelOf(name)) + ".", true);
        return;
    }
    const std::string restore = before.empty() ? name : before;
    std::weak_ptr<char> alive = alive_;
    ThemeEditorDialog::Hosts h;
    // L'apercu en direct : toute l'application, sans rien retenir.
    h.preview = [this, alive](const ui::Theme& t) {
        if (!alive.expired() && hosts_.preview) hosts_.preview(t);
    };
    // Enregistrer : le nom verifie (un autre theme ne le porte pas), le fichier
    // suit un nouveau nom, puis le theme est ecrit et applique.
    h.save = [this, alive, name](const ui::Theme& t, std::string& error) -> bool {
        if (alive.expired()) {
            error = "la galerie est ferm\xC3\xA9" "e";
            return false;
        }
        if (auto problem = ui::themeNameProblem(t.name); !problem.empty()) {
            error = problem;
            return false;
        }
        if (t.name != name) {
            if (ui::freeThemeName(t.name, name) != t.name) {
                error = guil(t.name) + " est d\xC3\xA9j\xC3\xA0 pris : choisis un autre nom.";
                return false;
            }
            if (!ui::UserThemes::rename(name, t.name, &error)) return false;
            if (original_ == name) original_ = t.name;
        }
        ui::Theme copy = t;
        copy.user = true;
        if (!ui::UserThemes::save(copy, &error)) return false;
        reload();
        if (hosts_.apply) hosts_.apply(copy.name);
        relabel();
        say(guil(copy.name) + " est enregistr\xC3\xA9 dans tes th\xC3\xA8mes, et appliqu\xC3\xA9.");
        return true;
    };
    // Annuler : le theme d'avant revient ; une copie faite pour l'editeur est retiree.
    h.cancel = [this, alive, name, restore, fresh] {
        if (alive.expired()) return;
        if (fresh) {
            std::string ignored;
            (void)ui::UserThemes::remove(name, &ignored);
        }
        reload();
        if (hosts_.apply) hosts_.apply(fresh && restore == name ? original_ : restore);
        relabel();
        say(fresh ? "Rien n'est gard\xC3\xA9 : le th\xC3\xA8me d'avant revient." : "Modifications annul\xC3\xA9" "es : le th\xC3\xA8me d'avant revient.");
    };
    manager().ShowDialog(std::make_unique<ThemeEditorDialog>(*it, std::move(h)), [](const menu::DialogResult&) {});
}

void ThemeGalleryDialog::askName(const std::string& title, const std::string& explanation, const std::string& proposed,
                                 const std::string& confirm, std::function<void(const std::string&)> done) {
    if (!body_ || !body_->nameField) return;
    body_->askTitle = title;
    body_->askDone = std::move(done);
    body_->nameField->setText(proposed);
    body_->nameOk->setText(confirm);
    say(explanation);
    // Visible tout de suite : le champ doit pouvoir prendre le clavier.
    body_->nameField->setVisibility(ui::Visibility::Visible);
    body_->nameOk->setVisibility(ui::Visibility::Visible);
    body_->nameCancel->setVisibility(ui::Visibility::Visible);
    root().invalidateLayout();
    focus().focus(body_->nameField);
}

void ThemeGalleryDialog::onNew() {
    const std::string from = chosenKey();
    askName("Nom du nouveau th\xC3\xA8me :", "Il part des couleurs de " + guil(ui::Theme::labelOf(from)) + " ; tu les changes ensuite.",
            ui::freeThemeName(ui::Theme::labelOf(from) + " (perso)"), "Cr\xC3\xA9" "er",
            [this, from](const std::string& n) { createFrom(n, from, true); });
}

void ThemeGalleryDialog::onEdit() {
    const std::string key = chosenKey();
    if (!ui::Theme::isBuiltIn(key)) {
        edit(key);
        return;
    }
    askName("Nom de ta copie :", guil(ui::Theme::labelOf(key)) + " est int\xC3\xA9gr\xC3\xA9 : il est d'abord dupliqu\xC3\xA9, et c'est la copie que tu modifies.",
            ui::freeThemeName(ui::Theme::labelOf(key) + " (perso)"), "Dupliquer et modifier",
            [this, key](const std::string& n) { createFrom(n, key, true); });
}

void ThemeGalleryDialog::onDuplicate() {
    const std::string key = chosenKey();
    askName("Nom de la copie :", "Une copie de " + guil(ui::Theme::labelOf(key)) + ", \xC3\xA0 toi.",
            ui::freeThemeName(ui::Theme::labelOf(key) + " (copie)"), "Dupliquer",
            [this, key](const std::string& n) { createFrom(n, key, false); });
}

void ThemeGalleryDialog::onRename() {
    const std::string key = chosenKey();
    if (ui::Theme::isBuiltIn(key)) {
        say("Un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9 ne se renomme pas : duplique-le.", true);
        return;
    }
    askName("Nouveau nom :", "Le fichier du th\xC3\xA8me suit son nom.", key, "Renommer",
            [this, key](const std::string& n) { renameTheme(key, n); });
}

void ThemeGalleryDialog::onDelete() {
    const std::string key = chosenKey();
    if (ui::Theme::isBuiltIn(key)) {
        say("Un th\xC3\xA8me int\xC3\xA9gr\xC3\xA9 ne se supprime pas.", true);
        return;
    }
    if (body_ && (body_->deletePending != key || now_ > body_->deleteUntil)) {
        body_->deletePending = key;
        body_->deleteUntil = now_ + 6.0;
        say("Clique encore sur Supprimer pour retirer " + guil(key) + " et son fichier.", true);
        return;
    }
    if (body_) body_->deletePending.clear();
    deleteTheme(key);
}

void ThemeGalleryDialog::onExport() {
    const std::string key = chosenKey();
    const auto spec = ui::saveFile("Th\xC3\xA8mes XpgAnalyzer|*.xpgtheme", ui::pathIn(startFolder(), ui::Theme::labelOf(key) + ".xpgtheme"),
                                   "Exporter le th\xC3\xA8me");
    std::weak_ptr<char> alive = alive_;
    if (!ui::browsePath(spec, {}, [this, alive, key](std::string path) {
            if (alive.expired()) return;
            exportTo(key, path);
        }))
        say("Pas d'explorateur ici : la commande de script theme-exporter \"Nom\" \"chemin\" fait la m\xC3\xAAme chose.", true);
}

void ThemeGalleryDialog::onImport() {
    const auto spec = ui::openFile("Th\xC3\xA8mes XpgAnalyzer|*.xpgtheme", startFolder(), "Importer un th\xC3\xA8me");
    std::weak_ptr<char> alive = alive_;
    if (!ui::browsePath(spec, {}, [this, alive](std::string path) {
            if (alive.expired()) return;
            importFrom(path);
        }))
        say("Pas d'explorateur ici : la commande de script theme-importer \"chemin\" fait la m\xC3\xAAme chose.", true);
}

void ThemeGalleryDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    if (!message_.empty() && now_ > messageUntil_ && !(body_ && body_->asking())) {
        message_.clear();
        messageError_ = false;
        root().invalidate();
    }
    // Le theme a change d'ailleurs (un script "theme Nuit") : les libelles suivent.
    if (hosts_.current && hosts_.current() != shown_) relabel();
    if (ui::Theme::userThemes().size() != userCount_) reload();
}

ui::EventResult ThemeGalleryDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        std::string ext = drop->path.size() >= 9 ? drop->path.substr(drop->path.size() - 9) : std::string{};
        for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ext == ".xpgtheme") importFrom(drop->path);
        else say("Seul un fichier .xpgtheme s'importe ici.", true);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // ---- Lot API 8 : didacticiels et aide ---- F1 : la page "Les themes" de l'aide.
        if (k->key == ui::Key::F1 && k->mods.none() && !k->repeat) {
            lot8::setPendingHelpAnchor(lot8::helpAnchorFor("themes"));
            manager().PushMenu("help");
            return ui::EventResult::Consumed;
        }
        // ---- fin Lot API 8 : didacticiels et aide ----
        const bool asking = body_ && body_->asking();
        if (k->key == ui::Key::Escape) {
            if (asking) {
                body_->askDone = nullptr;
                message_.clear();
                root().invalidateLayout();
            } else {
                finish(false);
            }
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            if (asking) {
                auto done = std::move(body_->askDone);
                body_->askDone = nullptr;
                root().invalidateLayout();
                if (done) done(body_->nameField->text());
            } else {
                finish(true);
            }
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void ThemeGalleryDialog::finish(bool keep) {
    if (done_) return;
    done_ = true;
    if (!keep && hosts_.apply && hosts_.current && hosts_.current() != original_) hosts_.apply(original_);
    manager().CloseDialog(menu::DialogResult{keep ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             hosts_.current ? hosts_.current() : std::string{}});
}

} // namespace app
