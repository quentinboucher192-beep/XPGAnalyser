#include "HistoryPanel.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : les filtres de chaque application

#include "../ui/Icons.hpp"
#include "../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace app {

std::string historyClock(std::int64_t wallMs, bool seconds) {
    if (wallMs <= 0) return {};
    const auto t = static_cast<std::time_t>(wallMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    if (seconds) std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    else std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

namespace {

constexpr float kWidth = 440.f;
constexpr float kHeaderH = 38.f;
constexpr float kFooterH = 80.f;
constexpr float kEntryH = 46.f;
constexpr float kMarkH = 26.f;

std::string lowerAscii(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Le texte coupe a `width`, avec des points de suspension.
std::string elide(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width) {
    if (width <= 0.f) return {};
    if (r.measure(text, font).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const float dw = r.measure(dots, font).width;
    auto n = r.fitCharacters(text, font, std::max(0.f, width - dw));
    // Ne pas couper un caractere UTF-8 en deux.
    while (n > 0 && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    return text.substr(0, n) + dots;
}

gfx::Color alpha(gfx::Color c, std::uint8_t a) {
    c.a = a;
    return c;
}

} // namespace

HistoryPanel::HistoryPanel(std::string id) : ui::Widget(std::move(id)) {
    setVisibility(ui::Visibility::Collapsed);
    setFocusPolicy(true);
}

void HistoryPanel::setTimes(std::int64_t openedMs, std::int64_t savedMs) {
    if (openedMs == openedMs_ && savedMs == savedMs_) return;
    openedMs_ = openedMs;
    savedMs_ = savedMs;
    dirtyRows_ = true;
    invalidate();
}

void HistoryPanel::setCurrentPlace(std::string placeKey, std::string placeName) {
    // "hmi:equipements#7" : l'onglet, sans son sous-onglet.
    if (const auto hash = placeKey.find('#'); hash != std::string::npos) placeKey.resize(hash);
    if (placeKey == hereKey_ && placeName == hereName_) return;
    hereKey_ = std::move(placeKey);
    hereName_ = std::move(placeName);
    if (filter_ == Filter::Here) dirtyRows_ = true;
    invalidate();
}

void HistoryPanel::open() {
    if (open_) return;
    open_ = true;
    setVisibility(ui::Visibility::Visible);
    dirtyRows_ = true;
    selected_ = -1;
    scroll_ = 0.f;
    invalidate();
}

void HistoryPanel::close() {
    if (!open_) return;
    open_ = false;
    if (focused()) releaseFocus();
    setVisibility(ui::Visibility::Collapsed);
    invalidate();
}

void HistoryPanel::setFilter(Filter f) {
    if (filter_ == f) return;
    filter_ = f;
    dirtyRows_ = true;
    selected_ = -1;
    scroll_ = 0.f;
    invalidate();
}

void HistoryPanel::setSearch(std::string text) {
    if (search_ == text) return;
    search_ = std::move(text);
    dirtyRows_ = true;
    selected_ = -1;
    scroll_ = 0.f;
    invalidate();
}

bool HistoryPanel::keep(const core::CommandInfo& info) const {
    switch (filter_) {
        case Filter::All: break;
        case Filter::Api: if (info.area != 1) return false; break;
        case Filter::Hmi: if (info.area != 2) return false; break;
        case Filter::Here: {
            std::string key = info.placeKey;
            if (const auto hash = key.find('#'); hash != std::string::npos) key.resize(hash);
            if (hereKey_.empty() || key != hereKey_) return false;
            break;
        }
    }
    if (search_.empty()) return true;
    const auto needle = lowerAscii(search_);
    return lowerAscii(info.label).find(needle) != std::string::npos
        || lowerAscii(info.place).find(needle) != std::string::npos;
}

void HistoryPanel::rebuildIfNeeded() const {
    if (!stack_) {
        rows_.clear();
        return;
    }
    if (!dirtyRows_ && built_ == stack_->revision()) return;
    built_ = stack_->revision();
    dirtyRows_ = false;
    rows_.clear();

    const auto& done = stack_->done();
    const auto& undone = stack_->undone();
    const auto nDone = static_cast<std::ptrdiff_t>(done.size());
    const auto total = nDone + static_cast<std::ptrdiff_t>(undone.size());
    const auto saved = stack_->savedDepth();
    // La frise : done, puis undone a l'envers (le prochain a retablir d'abord).
    const auto entryAt = [&](std::ptrdiff_t t) -> const core::CommandStack::Entry& {
        if (t < nDone) return done[static_cast<std::size_t>(t)];
        return undone[undone.size() - 1 - static_cast<std::size_t>(t - nDone)];
    };

    if (!undone.empty()) {
        Row h;
        h.kind = Row::Kind::Header;
        h.label = "ANNUL\xC3\x89" "ES \xE2\x80\x94 UN DOUBLE CLIC LES R\xC3\x89TABLIT";
        rows_.push_back(std::move(h));
    }
    for (std::ptrdiff_t p = total; p >= 0; --p) {
        if (p == nDone) {
            Row r;
            r.kind = Row::Kind::Now;
            r.label = "\xC3\x89TAT ACTUEL";
            rows_.push_back(std::move(r));
        }
        if (p == saved && (p != 0 || savedMs_ > 0)) {
            Row r;
            r.kind = Row::Kind::Saved;
            r.label = "ENREGISTR\xC3\x89";
            r.time = historyClock(savedMs_);
            rows_.push_back(std::move(r));
        }
        if (p == 0) {
            if (stack_->dropped() > 0) {
                Row r;
                r.kind = Row::Kind::Dropped;
                r.label = std::to_string(stack_->dropped()) + " action(s) plus anciennes : la pile les a oubli\xC3\xA9" "es";
                rows_.push_back(std::move(r));
            }
            Row r;
            r.kind = Row::Kind::Opened;
            r.label = "PROJET OUVERT";
            r.time = historyClock(openedMs_);
            rows_.push_back(std::move(r));
            break;
        }
        const auto& e = entryAt(p - 1);
        if (!keep(e.info)) continue;
        Row r;
        r.kind = Row::Kind::Entry;
        r.serial = e.info.serial;
        r.undone = p - 1 >= nDone;
        r.label = e.info.label;
        r.place = e.info.place;
        r.placeKey = e.info.placeKey;
        r.area = e.info.area;
        r.time = historyClock(e.info.lastMs);
        if (e.info.merged > 1) r.merged = "\xC3\x97" + std::to_string(e.info.merged);
        rows_.push_back(std::move(r));
    }
}

int HistoryPanel::rowByLabel(const std::string& prefix) const {
    rebuildIfNeeded();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].label.rfind(prefix, 0) == 0) return static_cast<int>(i);
    return -1;
}

gfx::Rect HistoryPanel::rowRect(int i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= rowRects_.size()) return {};
    const auto r = rowRects_[static_cast<std::size_t>(i)];
    const auto vis = r.intersect(list_);
    return vis.empty() ? gfx::Rect{} : r;
}

gfx::Rect HistoryPanel::partRect(Part p) const noexcept {
    switch (p) {
        case Part::Close:        return close_;
        case Part::ChipAll:      return chips_[0];
        case Part::ChipApi:      return chips_[1];
        case Part::ChipHmi:      return chips_[2];
        case Part::ChipHere:     return chips_[3];
        case Part::Search:       return searchBox_;
        case Part::SavedButton:  return savedBtn_;
        case Part::OpenedButton: return openedBtn_;
    }
    return {};
}

float HistoryPanel::rowHeight(const Row& r) const noexcept {
    return r.kind == Row::Kind::Entry ? kEntryH : kMarkH;
}

void HistoryPanel::reveal(int i) {
    rebuildIfNeeded();
    if (i < 0 || static_cast<std::size_t>(i) >= rows_.size()) return;
    float y = 0.f;
    for (int k = 0; k < i; ++k) y += rowHeight(rows_[static_cast<std::size_t>(k)]);
    const float h = rowHeight(rows_[static_cast<std::size_t>(i)]);
    if (y < scroll_) scroll_ = y;
    else if (list_.h > 0.f && y + h > scroll_ + list_.h) scroll_ = y + h - list_.h;
    clampScroll();
    invalidate();
}

void HistoryPanel::clampScroll() {
    const float maxScroll = std::max(0.f, contentH_ - list_.h);
    scroll_ = std::clamp(scroll_, 0.f, maxScroll);
}

int HistoryPanel::rowAt(gfx::Point p) const {
    if (!list_.contains(p)) return -1;
    for (std::size_t i = 0; i < rowRects_.size(); ++i)
        if (rowRects_[i].contains(p)) return static_cast<int>(i);
    return -1;
}

void HistoryPanel::onPaint(const ui::PaintContext& ctx) {
    if (!open_) return;
    rebuildIfNeeded();
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    const auto& m = ctx.theme.metric;
    const auto b = bounds();
    const float w = std::clamp(kWidth, 320.f, std::max(320.f, b.w * 0.45f));
    drawer_ = {b.right() - w, b.y + m.toolbarHeight, w, std::max(200.f, b.h - m.toolbarHeight - m.statusBarHeight)};
    const auto d = drawer_;

    // L'ombre a gauche (des bandes de plus en plus claires), le fond, le lisere.
    for (int k = 0; k < 10; ++k)
        ctx.r.fillRect({d.x - 10.f + static_cast<float>(k), d.y, 1.f, d.h}, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(6 + 7 * k)});
    ctx.r.fillRect(d, c.panelBg);
    ctx.r.fillRect({d.x, d.y, 1.f, d.h}, c.borderStrong);

    const float lhUi = ctx.r.lineHeight(f.ui);
    const float lhCap = ctx.r.lineHeight(f.caption);

    // ---- l'en-tete
    ctx.r.fillRect({d.x + 1.f, d.y, d.w - 1.f, kHeaderH}, c.headerBg);
    ctx.r.fillRect({d.x + 1.f, d.y + kHeaderH - 1.f, d.w - 1.f, 1.f}, c.border);
    const std::string title = "HISTORIQUE";
    ctx.r.drawText({d.x + 14.f, d.y + (kHeaderH - lhUi) / 2.f}, title, f.uiBold, c.text);
    const float tw = ctx.r.measure(title, f.uiBold).width;
    std::size_t count = stack_ ? stack_->done().size() + stack_->undone().size() : 0;
    std::string sub = std::to_string(count) + (count > 1 ? " actions" : " action");
    if (openedMs_ > 0) sub += " depuis l'ouverture (" + historyClock(openedMs_) + ")";
    ctx.r.drawText({d.x + 24.f + tw, d.y + (kHeaderH - lhCap) / 2.f}, elide(ctx.r, sub, f.caption, d.w - tw - 80.f), f.caption, c.textMuted);
    close_ = {d.right() - 34.f, d.y + 4.f, 30.f, kHeaderH - 8.f};
    if (hoverPart_ == 0) ctx.r.fillRoundedRect(close_, ctx.theme.brand.hover, 4.f);
    ui::drawIcon(ctx.r, ui::Icon::Close, {close_.x + 7.f, close_.y + (close_.h - 16.f) / 2.f, 16.f, 16.f}, c.textMuted);

    // ---- les filtres
    const char* chipLabels[4] = {"Tout", "API", "IHM", "Cet onglet"};
    float cx = d.x + 12.f;
    const float cy = d.y + kHeaderH + 8.f;
    for (int i = 0; i < 4; ++i) {
        // 1.12.0 : une application, un domaine - ni API ni IHM a choisir (XPGAnalyser API ou IHM).
        if ((i == 1 || i == 2) && core::edition() != core::Edition::Both) {
            chips_[i] = {};
            continue;
        }
        const float cw = ctx.r.measure(chipLabels[i], f.caption).width + 20.f;
        chips_[i] = {cx, cy, cw, 22.f};
        const bool on = static_cast<int>(filter_) == i;
        if (on) ctx.r.fillRoundedRect(chips_[i], c.accent, 11.f);
        else {
            ctx.r.fillRoundedRect(chips_[i], hoverPart_ == 1 + i ? ctx.theme.brand.hover : c.inputBg, 11.f);
            ctx.r.strokeRect(chips_[i], c.border, 1.f);
        }
        ctx.r.drawText({cx + 10.f, cy + (22.f - lhCap) / 2.f}, chipLabels[i], f.caption, on ? c.textInverted : c.text);
        cx += cw + 6.f;
    }
    if (filter_ == Filter::Here && !hereName_.empty())
        ctx.r.drawText({cx + 4.f, cy + (22.f - lhCap) / 2.f}, elide(ctx.r, hereName_, f.caption, d.right() - cx - 16.f), f.caption, c.textMuted);

    // ---- chercher
    searchBox_ = {d.x + 12.f, cy + 30.f, d.w - 24.f, 26.f};
    ctx.r.fillRoundedRect(searchBox_, c.inputBg, 4.f);
    ctx.r.strokeRect(searchBox_, focused() ? c.accent : c.border, 1.f);
    ui::drawIcon(ctx.r, ui::Icon::Search, {searchBox_.x + 6.f, searchBox_.y + 5.f, 16.f, 16.f}, c.textMuted);
    const float sy = searchBox_.y + (searchBox_.h - lhUi) / 2.f;
    if (search_.empty() && !focused())
        ctx.r.drawText({searchBox_.x + 28.f, sy}, "Chercher dans l'historique", f.ui, c.textMuted);
    else {
        const auto shown = elide(ctx.r, search_, f.ui, searchBox_.w - 40.f);
        ctx.r.drawText({searchBox_.x + 28.f, sy}, shown, f.ui, c.text);
        if (focused() && std::fmod(ctx.time, 1.0) < 0.6) {
            const float caretX = searchBox_.x + 28.f + ctx.r.measure(shown, f.ui).width + 1.f;
            ctx.r.fillRect({caretX, searchBox_.y + 5.f, 1.5f, searchBox_.h - 10.f}, c.text);
        }
    }

    // ---- la liste
    const float listTop = searchBox_.bottom() + 8.f;
    list_ = {d.x + 1.f, listTop, d.w - 1.f, std::max(40.f, d.bottom() - kFooterH - listTop)};
    ctx.r.fillRect({list_.x, list_.y - 1.f, list_.w, 1.f}, c.border);
    contentH_ = 0.f;
    for (const auto& r : rows_) contentH_ += rowHeight(r);
    clampScroll();
    rowRects_.assign(rows_.size(), gfx::Rect{});
    ctx.r.pushClip(list_);
    float y = list_.y - scroll_;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        const float h = rowHeight(row);
        const gfx::Rect rr{list_.x, y, list_.w, h};
        rowRects_[i] = rr;
        y += h;
        if (rr.bottom() < list_.y || rr.y > list_.bottom()) continue;
        const float ty = rr.y + (h - lhCap) / 2.f;
        switch (row.kind) {
            case Row::Kind::Header:
                ctx.r.fillRect(rr, c.rowAltBg);
                ctx.r.drawText({rr.x + 14.f, ty}, row.label, f.caption, c.textMuted);
                break;
            case Row::Kind::Now: {
                ctx.r.fillRect(rr, alpha(c.accent, 46));
                ctx.r.fillRect({rr.x, rr.y, rr.w, 2.f}, c.accent);
                // Un petit triangle devant le texte.
                const float mx = rr.x + 14.f, my = rr.y + h / 2.f;
                for (int k = 0; k < 6; ++k)
                    ctx.r.fillRect({mx + static_cast<float>(k), my - 6.f + static_cast<float>(k), 1.f, 12.f - 2.f * static_cast<float>(k)}, c.accent);
                ctx.r.drawText({rr.x + 26.f, rr.y + (h - lhCap) / 2.f}, row.label, f.caption, c.text);
                ctx.r.drawText({rr.x + 27.f, rr.y + (h - lhCap) / 2.f}, row.label, f.caption, c.text);
                break;
            }
            case Row::Kind::Saved:
                ctx.r.fillRect(rr, alpha(c.ok, 34));
                ui::drawIcon(ctx.r, ui::Icon::Save, {rr.x + 12.f, rr.y + 5.f, 16.f, 16.f}, c.ok);
                ctx.r.drawText({rr.x + 34.f, ty}, row.label + (row.time.empty() ? std::string() : " \xE2\x80\x94 " + row.time), f.caption, c.text);
                break;
            case Row::Kind::Opened:
            case Row::Kind::Dropped:
                if (static_cast<int>(i) == hover_) ctx.r.fillRect(rr, ctx.theme.brand.hover);
                ui::drawIcon(ctx.r, row.kind == Row::Kind::Opened ? ui::Icon::FolderOpen : ui::Icon::Info,
                             {rr.x + 12.f, rr.y + 5.f, 16.f, 16.f}, c.textMuted);
                ctx.r.drawText({rr.x + 34.f, ty}, row.label + (row.time.empty() ? std::string() : " \xE2\x80\x94 " + row.time), f.caption, c.textMuted);
                break;
            case Row::Kind::Entry: {
                if (static_cast<int>(i) == selected_) ctx.r.fillRect(rr, c.selectionBg);
                else if (static_cast<int>(i) == hover_) ctx.r.fillRect(rr, ctx.theme.brand.hover);
                ctx.r.fillRect({rr.x, rr.bottom() - 1.f, rr.w, 1.f}, c.gridLine);
                // L'IHM en sarcelle, l'automate en violet (les teintes des familles).
                const gfx::Color tint = row.area == 2 ? ctx.theme.onSurface(ctx.theme.brand.family[2])
                                      : row.area == 1 ? ctx.theme.onSurface(ctx.theme.brand.family[1]) : c.textMuted;
                const gfx::Rect icon{rr.x + 12.f, rr.y + 11.f, 24.f, 24.f};
                ctx.r.fillRoundedRect(icon, alpha(tint, row.undone ? 30 : 50), 5.f);
                ui::drawIcon(ctx.r, row.area == 2 ? ui::Icon::Screen : ui::Icon::Program,
                             {icon.x + 4.f, icon.y + 4.f, 16.f, 16.f}, row.undone ? alpha(tint, 150) : tint);
                const float textX = rr.x + 46.f;
                const float timeW = ctx.r.measure(row.time, f.caption).width;
                const gfx::Color main = row.undone ? c.textMuted : (static_cast<int>(i) == selected_ ? c.selectionText : c.text);
                ctx.r.drawText({textX, rr.y + 5.f}, elide(ctx.r, row.label, f.ui, rr.right() - textX - timeW - 20.f), f.ui, main);
                ctx.r.drawText({textX, rr.y + 7.f + lhUi}, elide(ctx.r, row.place, f.caption, rr.right() - textX - 60.f), f.caption, c.textMuted);
                ctx.r.drawText({rr.right() - 12.f - timeW, rr.y + 6.f}, row.time, f.caption, c.textMuted);
                if (!row.merged.empty()) {
                    const float mw = ctx.r.measure(row.merged, f.caption).width + 10.f;
                    const gfx::Rect pill{rr.right() - 12.f - mw, rr.y + 8.f + lhCap, mw, lhCap + 1.f};
                    ctx.r.fillRoundedRect(pill, alpha(ctx.theme.brand.family[2], 60), 7.f);
                    ctx.r.drawText({pill.x + 5.f, pill.y}, row.merged, f.caption, c.text);
                }
                break;
            }
        }
    }
    if (rows_.empty() || (rows_.size() <= 3 && stack_ && stack_->done().empty() && stack_->undone().empty()))
        ctx.r.drawText({list_.x + 14.f, list_.y + 90.f}, "Rien \xC3\xA0 annuler pour l'instant.", f.ui, c.textMuted);
    ctx.r.popClip();
    // La barre de defilement (1.11.4 : elle se tire).
    sbar_.paint(ctx, list_, contentH_, list_.h, scroll_);

    // ---- le pied : revenir a l'etat enregistre, a l'ouverture
    const float fy = d.bottom() - kFooterH + 10.f;
    ctx.r.fillRect({d.x + 1.f, d.bottom() - kFooterH, d.w - 1.f, 1.f}, c.border);
    const float bw = (d.w - 36.f) / 2.f;
    savedBtn_ = {d.x + 12.f, fy, bw, 28.f};
    openedBtn_ = {d.x + 24.f + bw, fy, bw, 28.f};
    const bool canSaved = stack_ && stack_->savedDepth() >= 0 && stack_->isModified();
    const auto button = [&](const gfx::Rect& r, const std::string& label, bool enabled, bool hot) {
        ctx.r.fillRoundedRect(r, hot && enabled ? ctx.theme.brand.hover : c.inputBg, 5.f);
        ctx.r.strokeRect(r, c.border, 1.f);
        const auto txt = elide(ctx.r, label, f.caption, r.w - 12.f);
        const float lw = ctx.r.measure(txt, f.caption).width;
        ctx.r.drawText({r.x + (r.w - lw) / 2.f, r.y + (r.h - lhCap) / 2.f}, txt, f.caption, enabled ? c.text : c.textDisabled);
    };
    button(savedBtn_, "\xC3\x89tat enregistr\xC3\xA9" + (savedMs_ > 0 ? " (" + historyClock(savedMs_) + ")" : std::string()), canSaved, hoverPart_ == 6);
    button(openedBtn_, "Ouverture" + (openedMs_ > 0 ? " (" + historyClock(openedMs_) + ")" : std::string()),
           stack_ && stack_->canUndo(), hoverPart_ == 7);
    ctx.r.drawText({d.x + 12.f, fy + 36.f},
                   elide(ctx.r, "Double clic : revenir \xC3\xA0 cet \xC3\xA9tat \xC2\xB7 clic droit : aller \xC3\xA0 l'endroit", f.caption, d.w - 24.f),
                   f.caption, c.textMuted);
}

ui::EventResult HistoryPanel::onEvent(const ui::InputEvent& ev) {
    if (!open_) return ui::EventResult::Ignored;
    {
        float off = scroll_;   // 1.11.4 : la barre de defilement se tire
        if (sbar_.handle(*this, ev, list_, contentH_, list_.h, off)) {
            scroll_ = off;
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* mw = std::get_if<ui::MouseWheel>(&ev)) {
        if (!drawer_.contains(mw->pos)) return ui::EventResult::Ignored;
        scroll_ -= mw->dy * 48.f;
        clampScroll();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* mm = std::get_if<ui::MouseMove>(&ev)) {
        if (!drawer_.contains(mm->pos)) {
            if (hover_ != -1 || hoverPart_ != -1) { hover_ = -1; hoverPart_ = -1; invalidate(); }
            return ui::EventResult::Ignored;
        }
        const int was = hover_, wasPart = hoverPart_;
        hover_ = rowAt(mm->pos);
        hoverPart_ = close_.contains(mm->pos) ? 0
                   : chips_[0].contains(mm->pos) ? 1 : chips_[1].contains(mm->pos) ? 2
                   : chips_[2].contains(mm->pos) ? 3 : chips_[3].contains(mm->pos) ? 4
                   : savedBtn_.contains(mm->pos) ? 6 : openedBtn_.contains(mm->pos) ? 7 : -1;
        if (was != hover_ || wasPart != hoverPart_) invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* md = std::get_if<ui::MouseDown>(&ev)) {
        if (!drawer_.contains(md->pos)) return ui::EventResult::Ignored;
        rebuildIfNeeded();
        if (md->button == ui::MouseButton::Left) {
            if (close_.contains(md->pos)) { close(); return ui::EventResult::Consumed; }
            for (int i = 0; i < 4; ++i)
                if (chips_[i].contains(md->pos)) { setFilter(static_cast<Filter>(i)); return ui::EventResult::Consumed; }
            if (searchBox_.contains(md->pos)) { grabFocus(); invalidate(); return ui::EventResult::Consumed; }
            if (savedBtn_.contains(md->pos)) {
                if (stack_ && stack_->savedDepth() >= 0 && stack_->isModified()) savedStateRequested->emit();
                return ui::EventResult::Consumed;
            }
            if (openedBtn_.contains(md->pos)) {
                if (stack_ && stack_->canUndo()) goToRequested->emit(0);
                return ui::EventResult::Consumed;
            }
            const int i = rowAt(md->pos);
            if (i >= 0) {
                const auto& row = rows_[static_cast<std::size_t>(i)];
                if (row.kind == Row::Kind::Entry || row.kind == Row::Kind::Opened) selected_ = i;
                invalidate();
                if (md->clickCount >= 2) {
                    if (row.kind == Row::Kind::Entry) goToRequested->emit(row.serial);
                    else if (row.kind == Row::Kind::Opened) goToRequested->emit(0);
                    else if (row.kind == Row::Kind::Saved) savedStateRequested->emit();
                }
            }
        } else if (md->button == ui::MouseButton::Right) {
            const int i = rowAt(md->pos);
            if (i >= 0 && rows_[static_cast<std::size_t>(i)].kind == Row::Kind::Entry) {
                selected_ = i;
                invalidate();
                placeRequested->emit(rows_[static_cast<std::size_t>(i)].placeKey);
            }
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* mu = std::get_if<ui::MouseUp>(&ev))
        return drawer_.contains(mu->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;

    // Le clavier : seulement quand le tiroir a le focus (la case Chercher).
    if (!focused()) return ui::EventResult::Ignored;
    if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
        setSearch(search_ + t->utf8);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        rebuildIfNeeded();
        switch (k->key) {
            case ui::Key::Escape:
                if (!search_.empty()) setSearch({});
                else close();
                return ui::EventResult::Consumed;
            case ui::Key::Backspace: {
                if (search_.empty()) return ui::EventResult::Consumed;
                std::string s = search_;
                std::size_t n = s.size() - 1;
                while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
                s.resize(n);
                setSearch(std::move(s));
                return ui::EventResult::Consumed;
            }
            case ui::Key::Down:
            case ui::Key::Up: {
                int i = selected_;
                const int step = k->key == ui::Key::Down ? 1 : -1;
                for (int guard = 0; guard < static_cast<int>(rows_.size()); ++guard) {
                    i += step;
                    if (i < 0 || i >= static_cast<int>(rows_.size())) break;
                    if (rows_[static_cast<std::size_t>(i)].kind == Row::Kind::Entry) { selected_ = i; reveal(i); break; }
                }
                invalidate();
                return ui::EventResult::Consumed;
            }
            case ui::Key::Return:
                if (selected_ >= 0 && static_cast<std::size_t>(selected_) < rows_.size()
                    && rows_[static_cast<std::size_t>(selected_)].kind == Row::Kind::Entry)
                    goToRequested->emit(rows_[static_cast<std::size_t>(selected_)].serial);
                return ui::EventResult::Consumed;
            default:
                break;
        }
        // Les raccourcis (Ctrl+Z, Ctrl+H...) passent a l'ecran.
        if (k->mods.ctrl) return ui::EventResult::Ignored;
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
