// =============================================================================
//  app/GoToPanel.cpp - Aller a... (Ctrl+K) (lot 20 ; lot recherche : les
//  categories, leurs pastilles, le delai de frappe, le defilement) : voir l'en-tete.
// =============================================================================
#include "GoToPanel.hpp"

#include "../ui/TextSearch.hpp"
#include "../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace app {

namespace {

const gfx::FontId kSmall{13};
constexpr float kHeader = 44.f, kGroupH = 26.f, kRowH = 40.f, kWidth = 760.f, kChipH = 24.f, kChipGap = 6.f;

std::string thousands(std::size_t n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

// Les mots cherches, surlignes sous un texte (le titre, la ligne du dessous).
void markTerms(const ui::PaintContext& ctx, const ui::SearchQuery& query, const std::string& text, gfx::Point at,
               gfx::FontId font, float maxWidth) {
    if (query.empty() || text.empty()) return;
    for (const auto& [a, b] : query.ranges(text)) {
        const float x0 = at.x + ctx.r.measure(std::string_view(text).substr(0, a), font).width;
        if (x0 >= at.x + maxWidth) break;
        const float x1 = std::min(at.x + maxWidth, at.x + ctx.r.measure(std::string_view(text).substr(0, b), font).width);
        if (x1 > x0) ctx.r.fillRect({x0, at.y, x1 - x0, ctx.r.lineHeight(font) + 2.f}, gfx::Color{230, 180, 60, 90});
    }
}

// Un texte qui tient dans `maxW` (coupe avec "..." sinon, sur un debut de caractere).
void drawFit(gfx::IRenderer& r, gfx::Point at, const std::string& text, gfx::FontId f, gfx::Color c, float maxW) {
    if (maxW <= 0.f || text.empty()) return;
    const auto fits = r.fitCharacters(text, f, maxW);
    if (fits >= text.size()) {
        r.drawText(at, text, f, c);
        return;
    }
    std::size_t keep = fits > 3 ? fits - 3 : 0;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    r.drawText(at, text.substr(0, keep) + "...", f, c);
}

} // namespace

int GoToPanel::matchAt(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    std::vector<std::size_t> at;
    const std::string h = ui::foldForSearch(hay, &at);
    const std::string n = ui::foldForSearch(needle);
    const auto pos = h.find(n);
    return pos == std::string::npos ? -1 : static_cast<int>(at[pos]);
}

GoToPanel::GoToPanel(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
    setVisibility(ui::Visibility::Collapsed);
}

void GoToPanel::open(gfx::Rect anchor) {
    anchor_ = anchor;
    open_ = true;
    text_.clear();
    current_ = 0;
    category_ = -1;
    scroll_ = 0.f;
    pending_ = false;
    setVisibility(ui::Visibility::Visible);
    grabFocus();
    refresh();
    invalidate();
}

void GoToPanel::close() {
    if (!open_) return;
    open_ = false;
    pending_ = false;
    releaseFocus();
    setVisibility(ui::Visibility::Collapsed);
    invalidate();
}

void GoToPanel::setText(const std::string& text) {
    text_ = text;
    current_ = 0;
    scroll_ = 0.f;
    refresh();
    invalidate();
}

void GoToPanel::textEdited() {
    current_ = 0;
    scroll_ = 0.f;
    // Sans delai (ou sans la recherche par categories) : tout de suite.
    if (debounce_ <= 0.0 || !searchAll_) {
        refresh();
    } else {
        pending_ = true;
        pendingSince_ = -1.0;            // l'heure est prise au prochain dessin
    }
    invalidate();
}

void GoToPanel::flush() {
    if (pending_) refresh();
}

void GoToPanel::refresh() {
    pending_ = false;
    if (searchAll_) {
        auto out = searchAll_(text_, category_);
        results_ = std::move(out.results);
        categories_ = std::move(out.categories);
    } else {
        results_ = search_ ? search_(text_) : std::vector<Result>{};
        std::stable_sort(results_.begin(), results_.end(), [](const Result& a, const Result& b) {
            return a.group != b.group ? a.group < b.group : a.score < b.score;
        });
        // Les categories d'apres les resultats (la forme d'avant n'en dit pas plus).
        categories_.clear();
        for (const auto& r : results_) {
            if (categories_.empty() || categories_.back().group != r.group) categories_.push_back({r.group, r.groupTitle, r.groupTitle, 0});
            ++categories_.back().total;
        }
        if (category_ >= 0)
            std::erase_if(results_, [&](const Result& r) { return r.group != category_; });
    }
    if (current_ >= static_cast<int>(results_.size())) current_ = results_.empty() ? 0 : static_cast<int>(results_.size()) - 1;
    invalidate();
}

void GoToPanel::moveCurrent(int delta) {
    if (results_.empty()) return;
    current_ = std::clamp(current_ + delta, 0, static_cast<int>(results_.size()) - 1);
    ensureVisible(current_);
    invalidate();
}

void GoToPanel::activate(int index) {
    if (pending_) {
        refresh();                       // Entree tout de suite apres la frappe : le texte tape
        index = std::min(index, static_cast<int>(results_.size()) - 1);
    }
    if (index < 0 || index >= static_cast<int>(results_.size())) return;
    const Result r = results_[static_cast<std::size_t>(index)];
    close();
    chosen->emit(r);
}

void GoToPanel::setCategory(int group) {
    if (group == category_ && !pending_) return;
    category_ = group;
    current_ = 0;
    scroll_ = 0.f;
    refresh();
}

void GoToPanel::nextCategory(int step) {
    std::vector<int> order{-1};
    for (const auto& c : categories_) order.push_back(c.group);
    if (order.size() < 2) return;
    auto it = std::find(order.begin(), order.end(), category_);
    const auto n = static_cast<int>(order.size());
    const int at = it == order.end() ? 0 : static_cast<int>(it - order.begin());
    setCategory(order[static_cast<std::size_t>(((at + step) % n + n) % n)]);
}

gfx::Rect GoToPanel::resultRect(int index) const {
    for (const auto& [i, r] : rowRects_)
        if (i == index) return r;
    return {};
}

gfx::Rect GoToPanel::chipRect(int group) const {
    for (const auto& [g, r] : chipRects_)
        if (g == group) return r;
    return {};
}

float GoToPanel::rowTop(int index) const {
    float y = 0.f;
    int last = -2;
    for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
        const int g = results_[static_cast<std::size_t>(i)].group;
        if (g != last) {
            y += kGroupH;
            last = g;
        }
        if (i == index) return y;
        y += kRowH;
    }
    return y;
}

float GoToPanel::contentHeight() const {
    return results_.empty() ? 0.f : rowTop(static_cast<int>(results_.size()) - 1) + kRowH;
}

void GoToPanel::ensureVisible(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size()) || listH_ <= 0.f) return;
    const float top = rowTop(index);
    // Le premier d'un groupe : son titre avec lui.
    const bool first = index == 0 || results_[static_cast<std::size_t>(index - 1)].group != results_[static_cast<std::size_t>(index)].group;
    const float want = first ? top - kGroupH : top;
    if (want < scroll_) scroll_ = want;
    else if (top + kRowH > scroll_ + listH_) scroll_ = top + kRowH - listH_;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentHeight() - listH_));
}

void GoToPanel::onPaint(const ui::PaintContext& ctx) {
    if (!open_) return;
    // La frappe s'est arretee assez longtemps : chercher.
    if (pending_) {
        if (pendingSince_ < 0.0) pendingSince_ = ctx.time;
        if (ctx.time - pendingSince_ >= debounce_) refresh();
    }
    const auto& c = ctx.theme.color;
    const auto area = bounds();
    const float width = std::min(kWidth, std::max(320.f, area.w - 16.f));
    const float x = std::clamp(anchor_.x + anchor_.w * 0.5f - width * 0.5f, area.x + 8.f, std::max(area.x + 8.f, area.right() - width - 8.f));
    const float top = anchor_.bottom() + 4.f;

    // Les pastilles des categories : "Tout", puis chacune, avec son nombre.
    chipRects_.clear();
    std::vector<std::pair<std::string, std::string>> chipTexts;
    float chipsH = 0.f;
    if (!categories_.empty()) {
        std::size_t all = 0;
        for (const auto& cat : categories_) all += cat.total;
        chipTexts.emplace_back("Tout", thousands(all));
        for (const auto& cat : categories_) chipTexts.emplace_back(cat.chip, thousands(cat.total));
        float cx = x + 14.f, cy = top + kHeader + 6.f;
        int lines = 1;
        for (std::size_t i = 0; i < chipTexts.size(); ++i) {
            const float w = ctx.r.measure(chipTexts[i].first, kSmall).width + ctx.r.measure(chipTexts[i].second, kSmall).width + 30.f;
            if (cx + w > x + width - 14.f && cx > x + 14.f) {
                cx = x + 14.f;
                cy += kChipH + kChipGap;
                ++lines;
            }
            chipRects_.emplace_back(i == 0 ? -1 : categories_[i - 1].group, gfx::Rect{cx, cy, w, kChipH});
            cx += w + kChipGap;
        }
        chipsH = static_cast<float>(lines) * (kChipH + kChipGap) + 6.f;
    }
    const float listTop = top + kHeader + chipsH;
    const float content = contentHeight();
    const float maxH = std::max(kHeader + chipsH + 60.f, area.bottom() - top - 12.f);
    const float h = std::min(kHeader + chipsH + (results_.empty() ? 40.f : content) + 6.f, maxH);
    box_ = {x, top, width, h};
    listH_ = std::max(0.f, h - (kHeader + chipsH) - 4.f);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content - listH_));

    // L'ombre, le panneau.
    ctx.r.fillRoundedRect({box_.x + 3.f, box_.y + 5.f, box_.w, box_.h}, gfx::Color{0, 0, 0, 90}, 6.f);
    ctx.r.fillRoundedRect(box_, c.panelBg, 6.f);
    ctx.r.strokeRect(box_, c.borderStrong, 1.f);
    // Le champ.
    const auto& big = ctx.theme.font.uiBold;
    const float ty = box_.y + (kHeader - ctx.r.lineHeight(big)) * 0.5f;
    const std::string keys = "\xE2\x86\x91\xE2\x86\x93 choisir \xC2\xB7 Entr\xC3\xA9" "e : aller \xC2\xB7 Tab : cat\xC3\xA9gorie \xC2\xB7 \xC3\x89" "chap";
    const float kw = ctx.r.measure(keys, kSmall).width;
    // Lot API 8 : bandeau haut - "> une commande" (compiler, theme, nouvelle vue...).
    const std::string shown = text_.empty() ? std::string("Aller \xC3\xA0\xE2\x80\xA6 (une variable, une ligne de code, une vue ; > une commande)") : text_;
    drawFit(ctx.r, {box_.x + 16.f, ty}, shown, big, text_.empty() ? c.textMuted : c.text, box_.w - kw - 46.f);
    if (focused() && !text_.empty()) {
        const float cx = std::min(box_.right() - kw - 28.f, box_.x + 16.f + ctx.r.measure(text_, big).width + 1.f);
        ctx.r.line({cx, ty}, {cx, ty + ctx.r.lineHeight(big)}, c.accent, 1.5f);
    }
    ctx.r.drawText({box_.right() - 14.f - kw, box_.y + (kHeader - ctx.r.lineHeight(kSmall)) * 0.5f}, keys, kSmall, c.textMuted);
    ctx.r.line({box_.x, box_.y + kHeader}, {box_.right(), box_.y + kHeader}, c.border, 1.f);

    // Les pastilles.
    for (std::size_t i = 0; i < chipRects_.size() && i < chipTexts.size(); ++i) {
        const auto& [g, rr] = chipRects_[i];
        if (rr.bottom() > box_.bottom()) break;
        const bool on = g == category_;
        ctx.r.fillRoundedRect(rr, on ? c.accent.withAlpha(60) : c.headerBg, kChipH * 0.5f);
        if (on) ctx.r.strokeRect(rr, c.accent, 1.f);
        const float cty = rr.y + (kChipH - ctx.r.lineHeight(kSmall)) * 0.5f;
        const float lw = ctx.r.measure(chipTexts[i].first, kSmall).width;
        ctx.r.drawText({rr.x + 11.f, cty}, chipTexts[i].first, kSmall, on ? c.text : c.textMuted);
        ctx.r.drawText({rr.x + 19.f + lw, cty}, chipTexts[i].second, kSmall, on ? c.accent : c.textMuted);
    }
    if (chipsH > 0.f) ctx.r.line({box_.x, listTop - 1.f}, {box_.right(), listTop - 1.f}, c.border, 1.f);

    // Les resultats, par categorie.
    std::map<int, std::size_t> totals, shownIn;
    for (const auto& cat : categories_) totals[cat.group] = cat.total;
    for (const auto& r : results_) ++shownIn[r.group];
    const ui::SearchQuery query(text_);
    rowRects_.clear();
    const gfx::Rect list{box_.x, listTop, box_.w, std::max(0.f, box_.bottom() - listTop - 2.f)};
    ctx.r.pushClip(list);
    float y = listTop - scroll_;
    int lastGroup = -2;
    const auto& f = ctx.theme.font.ui;
    for (std::size_t i = 0; i < results_.size(); ++i) {
        const auto& r = results_[i];
        if (r.group != lastGroup) {
            if (y + kGroupH >= list.y && y <= list.bottom()) {
                ctx.r.fillRect({box_.x, y, box_.w, kGroupH}, c.headerBg);
                std::string head = r.groupTitle + "  \xC2\xB7  " + thousands(totals.count(r.group) ? totals[r.group] : shownIn[r.group]);
                const std::size_t total = totals.count(r.group) ? totals[r.group] : 0;
                if (total > shownIn[r.group])
                    head += "   (" + std::to_string(shownIn[r.group]) + " montr\xC3\xA9s" + (category_ < 0 ? std::string(" \xC2\xB7 Tab : la cat\xC3\xA9gorie seule)") : std::string(")"));
                ctx.r.drawText({box_.x + 16.f, y + (kGroupH - ctx.r.lineHeight(kSmall)) * 0.5f}, head, kSmall, c.textMuted);
            }
            y += kGroupH;
            lastGroup = r.group;
        }
        const gfx::Rect row{box_.x, y, box_.w, kRowH};
        y += kRowH;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        rowRects_.emplace_back(static_cast<int>(i), row);
        const bool on = static_cast<int>(i) == current_;
        if (on) ctx.r.fillRect(row, c.selectionBg);
        // L'icone dans un carre.
        const gfx::Rect ic{row.x + 14.f, row.y + 8.f, 24.f, 24.f};
        ctx.r.fillRoundedRect(ic, c.headerBg, 4.f);
        if (r.icon != ui::Icon::None) {
            const int code = ui::codeIconIndex(r.icon);      // 1.8.0 : une icone au choix garde sa couleur
            ui::drawIcon(ctx.r, r.icon, {ic.x + 4.f, ic.y + 4.f, 16.f, 16.f}, code >= 0 ? ui::codeIconColor(code) : on ? c.text : c.textMuted);
        }
        const float tx = row.x + 50.f;
        const std::string hint = on && r.hint.empty() ? std::string("Entr\xC3\xA9" "e") : r.hint;
        const float hw = hint.empty() ? 0.f : ctx.r.measure(hint, kSmall).width + 24.f;
        const float textW = row.right() - tx - 14.f - hw;
        // Les mots tapes, surlignes : dans le titre et dans la ligne du dessous
        // (le commentaire, la ligne de code ou ils ont ete trouves).
        markTerms(ctx, query, r.title, {tx, row.y + 5.f}, f, textW);
        drawFit(ctx.r, {tx, row.y + 5.f}, r.title, f, c.text, textW);
        markTerms(ctx, query, r.subtitle, {tx, row.y + 23.f}, kSmall, textW);
        drawFit(ctx.r, {tx, row.y + 23.f}, r.subtitle, kSmall, c.textMuted, textW);
        if (!hint.empty())
            ctx.r.drawText({row.right() - 14.f - (hw - 24.f), row.y + (kRowH - ctx.r.lineHeight(kSmall)) * 0.5f}, hint, kSmall, c.textMuted);
    }
    if (results_.empty()) {
        std::string msg;
        if (text_.empty()) msg = "Tape quelques lettres : un nom, un commentaire, du code\xE2\x80\xA6 (\"une phrase\", -mot pour l'exclure)";
        else if (pending_) msg = "Recherche\xE2\x80\xA6";
        else if (category_ >= 0) msg = "Rien dans cette cat\xC3\xA9gorie ne contient \xC2\xAB " + text_ + " \xC2\xBB (Tab : les autres)";
        else msg = "Rien ne contient \xC2\xAB " + text_ + " \xC2\xBB";
        drawFit(ctx.r, {box_.x + 16.f, listTop + 10.f}, msg, ctx.theme.font.ui, c.textMuted, box_.w - 32.f);
    }
    ctx.r.popClip();
    // La barre de defilement, quand la liste depasse.
    // 1.11.4 : elle se tire.
    if (listH_ > 0.f) sbar_.paint(ctx, {box_.x, listTop, box_.w - 1.f, listH_}, content, listH_, scroll_);
}

ui::EventResult GoToPanel::onEvent(const ui::InputEvent& ev) {
    if (!open_) return ui::EventResult::Ignored;
    {
        float off = scroll_;   // 1.11.4 : la barre de defilement se tire
        if (sbar_.handle(*this, ev, off)) {
            scroll_ = std::max(0.f, off);
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* t = std::get_if<ui::TextInput>(&ev); t && focused()) {
        text_ += t->utf8;
        textEdited();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
        switch (k->key) {
            case ui::Key::Escape: close(); return ui::EventResult::Consumed;
            case ui::Key::Down: moveCurrent(1); return ui::EventResult::Consumed;
            case ui::Key::Up: moveCurrent(-1); return ui::EventResult::Consumed;
            case ui::Key::PageDown: moveCurrent(8); return ui::EventResult::Consumed;
            case ui::Key::PageUp: moveCurrent(-8); return ui::EventResult::Consumed;
            case ui::Key::Return: activate(current_); return ui::EventResult::Consumed;
            case ui::Key::Tab: nextCategory(k->mods.shift ? -1 : 1); return ui::EventResult::Consumed;
            case ui::Key::Backspace:
                if (!text_.empty()) {
                    // Un caractere UTF-8 entier.
                    std::size_t n = text_.size() - 1;
                    while (n > 0 && (static_cast<unsigned char>(text_[n]) & 0xC0) == 0x80) --n;
                    text_.resize(n);
                    textEdited();
                }
                return ui::EventResult::Consumed;
            default: break;
        }
        // Les autres touches (Ctrl+Z...) ne passent pas aux volets pendant la saisie.
        if (!k->mods.ctrl) return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && box_.contains(m->pos)) {
        for (const auto& [i, r] : rowRects_)
            if (r.contains(m->pos) && current_ != i) { current_ = i; invalidate(); }
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && box_.contains(d->pos)) {
        for (const auto& [g, r] : chipRects_)
            if (r.contains(d->pos)) {
                setCategory(g);
                return ui::EventResult::Consumed;
            }
        for (const auto& [i, r] : rowRects_)
            if (r.contains(d->pos)) {
                activate(i);
                return ui::EventResult::Consumed;
            }
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!box_.contains(w->pos)) return ui::EventResult::Ignored;
        scroll_ = std::clamp(scroll_ - w->dy * kRowH * 2.f, 0.f, std::max(0.f, contentHeight() - listH_));
        invalidate();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ===================================================================== GoToBox ==
GoToBox::GoToBox(std::string id) : ui::Widget(std::move(id)) {
    setTooltip("Aller \xC3\xA0 n'importe quoi : une variable, un membre, une ligne de code, une vue, une alarme, une macro, "
               "un volet ou une action (Ctrl+K) ; > puis une commande : >compiler, >th\xC3\xA8me nord, >nouvelle vue");   // Lot API 8 : bandeau haut
}

ui::SizeHint GoToBox::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {240.f, 28.f};
    h.minimum = {140.f, 24.f};
    return h;
}

void GoToBox::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const gfx::Rect r{b.x + 2.f, b.y + (b.h - 28.f) * 0.5f, b.w - 4.f, 28.f};
    ctx.r.fillRoundedRect(r, c.inputBg, 4.f);
    ctx.r.strokeRect(r, hovered() ? c.accent : c.border, 1.f);
    ui::drawIcon(ctx.r, ui::Icon::Search, {r.x + 8.f, r.y + 6.f, 16.f, 16.f}, c.textMuted);
    const std::string k = "Ctrl+K";
    const float kw = ctx.r.measure(k, kSmall).width;
    // Lot API 8 : bandeau haut - la palette cherche ET fait (">" : les commandes).
    // Etroite : "Aller a..." seul, puis sans Ctrl+K, puis la loupe seule - le
    // texte ne touche jamais la touche.
    const std::string full = "Aller \xC3\xA0 / Faire\xE2\x80\xA6", brief = "Aller \xC3\xA0\xE2\x80\xA6";
    const float briefW = ctx.r.measure(brief, ctx.theme.font.ui).width;
    const bool badge = 30.f + briefW + 10.f + kw + 18.f <= r.w;
    const std::string shown = badge && 30.f + ctx.r.measure(full, ctx.theme.font.ui).width + 10.f + kw + 18.f <= r.w ? full
                            : 30.f + briefW + 6.f <= r.w ? brief : std::string{};
    if (!shown.empty())
        ctx.r.drawText({r.x + 30.f, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, shown, ctx.theme.font.ui, c.textMuted);
    if (!badge) return;
    const gfx::Rect kb{r.right() - kw - 18.f, r.y + 5.f, kw + 10.f, 18.f};
    ctx.r.strokeRect(kb, c.border, 1.f);
    ctx.r.drawText({kb.x + 5.f, kb.y + (kb.h - ctx.r.lineHeight(kSmall)) * 0.5f}, k, kSmall, c.textMuted);
}

ui::EventResult GoToBox::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        clicked->emit();
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::MouseMove>(&ev)) invalidate();
    return ui::EventResult::Ignored;
}

} // namespace app
