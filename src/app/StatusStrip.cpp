// =============================================================================
//  app/StatusStrip.cpp - Lot API 8 : le bandeau bas (voir StatusStrip.hpp)
// =============================================================================
#include "StatusStrip.hpp"
#include "../core/CallTrail.hpp"   // 1.10.2 (CR) : avertissements et erreurs, dans le journal interne

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <numeric>

namespace app {

using ui::StatusBar;

// ============================================================ le journal ===
void MessageJournal::push(std::string text, StatusBar::Severity severity, bool transient, std::string target,
                          double nowSeconds, std::string clock) {
    if (text.empty()) return;
    // 1.10.2 (CR) : les avertissements et les erreurs vont aussi au journal interne.
    if (severity == StatusBar::Severity::Warning) core::trail::note(core::trail::Kind::Warning, text);
    else if (severity == StatusBar::Severity::Error) core::trail::note(core::trail::Kind::Error, text);
    const bool plain = !transient && severity == StatusBar::Severity::None;
    if (!entries_.empty() && entries_.front().text == text && entries_.front().severity == severity) {
        entries_.front().time = std::move(clock);          // le meme, redit : son heure seulement
    } else if (plain && lastPlain_ && nowSeconds - lastPlainAt_ < 1.5 && !entries_.empty()) {
        entries_.front() = JournalEntry{std::move(clock), std::move(text), severity, std::move(target)};
    } else {
        entries_.push_front(JournalEntry{std::move(clock), std::move(text), severity, std::move(target)});
        while (entries_.size() > kMax) entries_.pop_back();
    }
    lastPlain_ = plain;
    lastPlainAt_ = nowSeconds;
    ++revision_;
}

void MessageJournal::clear() {
    entries_.clear();
    lastPlain_ = false;
    ++revision_;
}

std::string MessageJournal::toText() const {
    std::string out;
    for (const auto& e : entries_) {
        out += e.time;
        const auto word = severityWord(e.severity);
        if (!word.empty()) { out += "  ["; out += word; out += "]"; }
        out += "  " + e.text + "\n";
    }
    return out;
}

std::string wallClock(std::int64_t wallMs) {
    const std::time_t t = static_cast<std::time_t>(wallMs / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string groupedCount(std::uint64_t n) {
    const std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += "\xC2\xA0";
        out += digits[i];
    }
    return out;
}

std::string_view severityWord(StatusBar::Severity s) noexcept {
    switch (s) {
        case StatusBar::Severity::Info:    return "info";
        case StatusBar::Severity::Success: return "ok";
        case StatusBar::Severity::Warning: return "attention";
        case StatusBar::Severity::Error:   return "erreur";
        case StatusBar::Severity::None:    break;
    }
    return {};
}

// ========================================================== une pastille ===
namespace {
constexpr gfx::FontId kChipFont{13};     // la police de la barre d'etat (theme : smallUi)
constexpr float kPad = 7.f, kGap = 5.f, kIcon = 13.f, kDot = 8.f, kCaret = 8.f;
}

StatusChip::StatusChip(std::string id) : Widget(std::move(id)) {
    // Appuyee puis relachee ailleurs : le relachement ne lui arrive pas. Le
    // survol perdu annule l'appui (sinon un relachement plus tard la cliquerait).
    links_ += hoverChanged->connect([this](bool inside) {
        if (!inside && pressed_) { pressed_ = false; invalidate(); }
    });
}

void StatusChip::setText(std::string text) {
    if (text == text_) return;
    const bool resize = ui::measureWidth(text, kChipFont) != ui::measureWidth(text_, kChipFont);
    text_ = std::move(text);
    if (resize && parent()) parent()->invalidateLayout();
    invalidate();
}
void StatusChip::setIcon(ui::Icon icon) {
    if (icon == icon_) return;
    icon_ = icon;
    if (parent()) parent()->invalidateLayout();
    invalidate();
}
void StatusChip::setTone(ui::Tone tone) { if (tone != tone_) { tone_ = tone; invalidate(); } }
void StatusChip::setDot(bool on, ui::Tone tone) {
    if (on == dot_ && tone == dotTone_) return;
    if (on != dot_ && parent()) parent()->invalidateLayout();
    dot_ = on; dotTone_ = tone;
    invalidate();
}
void StatusChip::setKey(std::string key) {
    if (key == key_) return;
    key_ = std::move(key);
    if (parent()) parent()->invalidateLayout();
    invalidate();
}
void StatusChip::setMuted(bool muted) { muted_ = muted; invalidate(); }
void StatusChip::setCaret(bool caret) {
    if (caret == caret_) return;
    caret_ = caret;
    if (parent()) parent()->invalidateLayout();
    invalidate();
}
void StatusChip::setShown(bool shown) {
    if (shown == shown_) return;
    shown_ = shown;
    if (parent()) parent()->invalidateLayout();
    invalidate();
}

ui::SizeHint StatusChip::sizeHint() const {
    float w = kPad * 2.f;
    if (dot_) w += kDot + kGap;
    if (icon_ != ui::Icon::None) w += kIcon + kGap;
    w += ui::measureWidth(text_, kChipFont);
    if (!key_.empty()) w += kGap + ui::measureWidth(key_, kChipFont) + 8.f;
    if (caret_) w += kGap + kCaret;
    ui::SizeHint h;
    h.preferred = {w, ui::lineHeight(kChipFont) + 6.f};
    h.minimum = h.preferred;
    h.stretchX = 0.f;
    h.stretchY = 0.f;
    return h;
}

void StatusChip::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto r = bounds();
    const bool clickable = static_cast<bool>(onClick_) && !muted_;
    if (clickable && ((hovered() && !quiet_) || pressed_))
        ctx.r.fillRoundedRect({r.x, r.y + 1.f, r.w, r.h - 2.f}, c.border.withAlpha(pressed_ ? 150 : 90), 4.f);

    gfx::Color fg = muted_ ? c.textMuted : c.text;
    if (tone_ != ui::Tone::None && !muted_) fg = th.onSurface(th.tone(tone_, fg));
    const float lh = ctx.r.lineHeight(kChipFont);
    const float cy = r.y + r.h * 0.5f;
    float x = r.x + kPad;
    if (dot_) {
        ctx.r.fillRoundedRect({x, cy - kDot * 0.5f, kDot, kDot}, th.tone(dotTone_, c.textMuted), kDot * 0.5f);
        x += kDot + kGap;
    }
    if (icon_ != ui::Icon::None) {
        ui::drawIcon(ctx.r, icon_, {x, cy - kIcon * 0.5f, kIcon, kIcon}, fg);
        x += kIcon + kGap;
    }
    // Le texte se coupe (...) s'il ne tient pas : une pastille elastique, etroite.
    float after = 0.f;
    if (!key_.empty()) after += kGap + ctx.r.measure(key_, kChipFont).width + 8.f;
    if (caret_) after += kGap + kCaret;
    const float maxW = std::max(0.f, r.right() - kPad - after - x);
    std::string shown = text_;
    if (ctx.r.measure(shown, kChipFont).width > maxW + 1.f) {     // 1 px : les arrondis de la mise en page
        std::size_t keep = ctx.r.fitCharacters(shown, kChipFont, std::max(0.f, maxW - ctx.r.measure("\xE2\x80\xA6", kChipFont).width));
        keep = std::min(keep, shown.size());
        while (keep > 0 && keep < shown.size() && (static_cast<unsigned char>(shown[keep]) & 0xC0) == 0x80) --keep;
        shown = shown.substr(0, keep) + "\xE2\x80\xA6";
    }
    ctx.r.drawText({x, cy - lh * 0.5f}, shown, kChipFont, fg);
    x += ctx.r.measure(shown, kChipFont).width;
    if (!key_.empty()) {
        x += kGap;
        const float kw = ctx.r.measure(key_, kChipFont).width + 8.f;
        ctx.r.strokeRect({x, cy - lh * 0.5f - 1.f, kw, lh + 2.f}, c.border, 1.f);
        ctx.r.drawText({x + 4.f, cy - lh * 0.5f}, key_, kChipFont, c.textMuted);
        x += kw;
    }
    if (caret_) {
        x += kGap;
        const gfx::Color k = c.textMuted;
        ctx.r.line({x, cy + 2.f}, {x + kCaret * 0.5f, cy - 2.f}, k, 1.5f);
        ctx.r.line({x + kCaret * 0.5f, cy - 2.f}, {x + kCaret, cy + 2.f}, k, 1.5f);
    }
}

ui::EventResult StatusChip::onEvent(const ui::InputEvent& ev) {
    if (!onClick_ || muted_) return ui::EventResult::Ignored;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        if (!bounds().contains(d->pos)) return ui::EventResult::Ignored;
        pressed_ = true;
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Left && pressed_) {
        pressed_ = false;
        invalidate();
        if (bounds().contains(u->pos)) click();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// =================================================== la rangee du bandeau ===
namespace {
constexpr float kSeparator = 9.f;        // la place d'un trait entre deux pastilles
constexpr float kMessageRoom = 260.f;    // ce qui reste au message, a gauche, au moins
}

StatusStrip::StatusStrip(std::string id) : Widget(std::move(id)) {}

StatusChip& StatusStrip::add(std::unique_ptr<StatusChip> chip, int priority) {
    auto& ref = static_cast<StatusChip&>(addChild(std::move(chip)));
    slots_.push_back(Slot{&ref, priority});
    invalidateLayout();
    return ref;
}

float StatusStrip::room() const {
    if (const auto* bar = parent(); bar && bar->bounds().w > 0.f) return std::max(0.f, bar->bounds().w - 16.f - kMessageRoom);
    return 1.0e9f;
}

float StatusStrip::needed(const StatusChip& chip) const {
    const float w = chip.sizeHint().preferred.w;
    return chip.elastic() ? std::min(w, 140.f) : w;
}

std::vector<float> StatusStrip::widths() const {
    std::vector<float> out(slots_.size(), 0.f);
    const float space = room();
    // Les plus utiles d'abord, tant qu'elles tiennent (l'ordre d'affichage ne change pas).
    std::vector<std::size_t> order(slots_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return slots_[a].priority > slots_[b].priority; });
    float used = 0.f;
    for (const auto i : order) {
        const auto* chip = slots_[i].chip;
        if (!chip->shown()) continue;
        const float w = needed(*chip) + (used > 0.f ? kSeparator : 0.f);
        if (used + w > space) continue;
        used += w;
        out[i] = needed(*chip);
    }
    // Une pastille elastique prend ce qui reste, jusqu'a sa largeur voulue.
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (out[i] <= 0.f || !slots_[i].chip->elastic()) continue;
        const float extra = std::min(slots_[i].chip->sizeHint().preferred.w - out[i], std::max(0.f, space - used));
        out[i] += extra;
        used += extra;
    }
    return out;
}

ui::SizeHint StatusStrip::sizeHint() const {
    const auto sizes = widths();
    float w = 0.f;
    for (const auto s : sizes) {
        if (s <= 0.f) continue;
        if (w > 0.f) w += kSeparator;
        w += s;
    }
    ui::SizeHint h;
    h.preferred = {w, ui::lineHeight(kChipFont) + 6.f};
    h.minimum = {0.f, h.preferred.h};
    h.stretchX = 0.f;
    h.stretchY = 0.f;
    return h;
}

void StatusStrip::onLayout() {
    const auto sizes = widths();
    const auto area = bounds();
    float x = area.x;
    bool first = true;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto* chip = slots_[i].chip;
        if (sizes[i] <= 0.f) {
            if (chip->visibility() != ui::Visibility::Hidden) chip->setVisibility(ui::Visibility::Hidden);
            chip->setBounds({area.x, area.y, 0.f, 0.f});
            continue;
        }
        if (chip->visibility() != ui::Visibility::Visible) chip->setVisibility(ui::Visibility::Visible);
        if (!first) x += kSeparator;
        first = false;
        const float w = sizes[i];
        chip->setBounds({x, area.y, w, area.h});
        x += w;
    }
}

void StatusStrip::onPaint(const ui::PaintContext& ctx) {
    // Un trait fin entre deux pastilles montrees.
    const auto& c = ctx.theme.color;
    bool first = true;
    for (const auto& s : slots_) {
        if (!s.chip->visible() || s.chip->bounds().w <= 0.f) continue;
        if (!first) {
            const float x = s.chip->bounds().x - kSeparator * 0.5f;
            ctx.r.line({x, bounds().y + 5.f}, {x, bounds().bottom() - 5.f}, c.border, 1.f);
        }
        first = false;
    }
}

std::vector<const StatusChip*> StatusStrip::visibleChips() const {
    std::vector<const StatusChip*> out;
    for (const auto& s : slots_)
        if (s.chip->visible() && s.chip->bounds().w > 0.f) out.push_back(s.chip);
    return out;
}

} // namespace app
