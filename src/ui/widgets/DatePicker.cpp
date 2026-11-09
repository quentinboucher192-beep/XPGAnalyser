// =============================================================================
//  ui/widgets/DatePicker.cpp - 1.42.0 : le mini calendrier (voir DatePicker.hpp)
// =============================================================================
#include "DatePicker.hpp"

#include "../Shapes.hpp"
#include "../Theme.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ui {

namespace {

constexpr gfx::FontId kFont{16};
constexpr gfx::FontId kSmall{13};
constexpr float kPopupW = 300.f;
constexpr float kPad = 12.f;
constexpr float kHeadH = 34.f;
constexpr float kWeekH = 24.f;
constexpr float kCell = 32.f;
constexpr float kChipH = 26.f;
constexpr float kChipGap = 6.f;

const char* const kMonths[] = {"Janvier", "F\xC3\xA9vrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Ao\xC3\xBBt", "Septembre", "Octobre", "Novembre", "D\xC3\xA9" "cembre"};
const char* const kWeek[] = {"L", "M", "M", "J", "V", "S", "D"};
const char* const kDayShort[] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};

std::string two(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

} // namespace

// ================================================================ le calendrier ==
DatePicker::Seconds DatePicker::dayOf(Seconds t) noexcept {
    Seconds d = t / kDay;
    if (t < 0 && t % kDay) --d;
    return d * kDay;
}

DatePicker::Seconds DatePicker::dayFromCivil(int y, int m, int d) noexcept {   // Howard Hinnant, days_from_civil
    y -= m <= 2;
    const Seconds era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + static_cast<Seconds>(doe) - 719468) * kDay;
}

void DatePicker::civilFromDay(Seconds day, int& y, int& m, int& d) noexcept {
    Seconds z = dayOf(day) / kDay + 719468;
    const Seconds era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(static_cast<Seconds>(yoe) + era * 400 + (m <= 2));
}

int DatePicker::weekdayMonday0(Seconds day) noexcept {
    const Seconds n = dayOf(day) / kDay;   // le 1er janvier 1970 : un jeudi (3)
    return static_cast<int>(((n % 7) + 7 + 3) % 7);
}

int DatePicker::daysInMonth(int y, int m) noexcept {
    static const int k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    return k[(m - 1) % 12];
}

std::string DatePicker::frenchDay(Seconds day) {
    int y = 0, m = 0, d = 0;
    civilFromDay(day, y, m, d);
    return std::string(kDayShort[weekdayMonday0(day)]) + " " + two(d) + "/" + two(m) + "/" + std::to_string(y);
}

// ==================================================================== le widget ==
DatePicker::DatePicker(std::string id) : Widget(std::move(id)) {
    setFocusPolicy(true);
    showMonthOf(today());
}

DatePicker::Seconds DatePicker::today() const {
    if (today_) return today_;
    return dayOf(static_cast<Seconds>(std::time(nullptr)));
}

void DatePicker::setDate(Seconds day) {
    day_ = day ? dayOf(day) : 0;
    if (day_) showMonthOf(day_);
    invalidate();
}

void DatePicker::showMonthOf(Seconds day) {
    int d = 0;
    civilFromDay(day, year_, month_, d);
}

void DatePicker::shiftMonth(int delta) {
    int m = month_ - 1 + delta;
    year_ += m >= 0 ? m / 12 : -((-m + 11) / 12);
    month_ = ((m % 12) + 12) % 12 + 1;
    invalidate();
}

std::string DatePicker::monthTitle() const { return std::string(kMonths[month_ - 1]) + " " + std::to_string(year_); }

std::string DatePicker::fieldText() const { return day_ ? frenchDay(day_) : placeholder_; }

void DatePicker::open() {
    if (open_) return;
    open_ = true;
    showMonthOf(day_ ? day_ : (rangeStart_ ? rangeStart_ : today()));
    hot_ = day_;
    invalidate();
}

void DatePicker::close() {
    if (!open_) return;
    open_ = false;
    hot_ = 0;
    invalidate();
}

SizeHint DatePicker::sizeHint() const {
    SizeHint h;
    h.preferred = {190.f, lineHeight(kFont) + 14.f};
    h.minimum = {150.f, lineHeight(kFont) + 10.f};
    return h;
}

float DatePicker::footerHeight() const {
    float h = 0.f;
    if (!shortcuts_.empty()) h += kChipH + 10.f + 1.f;   // la ligne des raccourcis et son trait
    if (clearable_) h += kChipH + 8.f;
    return h;
}

gfx::Rect DatePicker::popupRect() const {
    const gfx::Rect b = bounds();
    const float h = kPad + kHeadH + kWeekH + 6.f * kCell + 6.f + footerHeight() + kPad;
    float x = b.x, y = b.bottom() + 4.f;
    flipped_ = false;
    if (const auto s = surfaceSize(); s.w > 0.f && s.h > 0.f) {
        if (y + h > s.h) {
            if (b.y - h - 4.f >= 0.f) {
                y = b.y - h - 4.f;
                flipped_ = true;
            } else {
                y = std::max(0.f, s.h - h);
            }
        }
        if (x + kPopupW > s.w) x = std::max(0.f, s.w - kPopupW);
    }
    return {x, y, kPopupW, h};
}

gfx::Rect DatePicker::gridRect() const {
    const gfx::Rect p = popupRect();
    return {p.x + kPad, p.y + kPad + kHeadH + kWeekH, p.w - 2.f * kPad, 6.f * kCell};
}

gfx::Rect DatePicker::prevRect() const {
    const gfx::Rect p = popupRect();
    return {p.x + kPad, p.y + kPad + 3.f, 28.f, 28.f};
}

gfx::Rect DatePicker::nextRect() const {
    const gfx::Rect p = popupRect();
    return {p.right() - kPad - 28.f, p.y + kPad + 3.f, 28.f, 28.f};
}

DatePicker::Seconds DatePicker::firstShown() const {
    const Seconds first = dayFromCivil(year_, month_, 1);
    return first - static_cast<Seconds>(weekdayMonday0(first)) * kDay;
}

gfx::Rect DatePicker::dayRect(Seconds day) const {
    const Seconds k = (dayOf(day) - firstShown()) / kDay;
    if (k < 0 || k >= 42) return {};
    const gfx::Rect g = gridRect();
    const float cw = g.w / 7.f;
    return {g.x + cw * static_cast<float>(k % 7), g.y + kCell * static_cast<float>(k / 7), cw, kCell};
}

DatePicker::Seconds DatePicker::dayAt(gfx::Point p) const {
    const gfx::Rect g = gridRect();
    if (!g.contains(p)) return 0;
    const int col = std::clamp(static_cast<int>((p.x - g.x) / (g.w / 7.f)), 0, 6);
    const int row = std::clamp(static_cast<int>((p.y - g.y) / kCell), 0, 5);
    return firstShown() + static_cast<Seconds>(row * 7 + col) * kDay;
}

gfx::Rect DatePicker::shortcutRect(std::size_t i) const {
    if (i >= shortcuts_.size()) return {};
    const gfx::Rect g = gridRect();
    float x = g.x;
    const float y = g.bottom() + 6.f + 11.f;
    for (std::size_t k = 0; k < shortcuts_.size(); ++k) {
        const float w = measureWidth(shortcuts_[k].label, kSmall) + 16.f;
        if (k == i) return {x, y, w, kChipH};
        x += w + kChipGap;
    }
    return {};
}

gfx::Rect DatePicker::clearRect() const {
    if (!clearable_) return {};
    const gfx::Rect p = popupRect();
    const float w = measureWidth("Effacer", kSmall) + 20.f;
    return {p.right() - kPad - w, p.bottom() - kPad - kChipH, w, kChipH};
}

DatePicker::Seconds DatePicker::shortcutDay(std::size_t i) const {
    if (i >= shortcuts_.size()) return 0;
    const Shortcut& s = shortcuts_[i];
    const Seconds base = rangeStart_ ? rangeStart_ : today();
    int y = 0, m = 0, d = 0;
    civilFromDay(base, y, m, d);
    if (s.endOfMonth) return dayFromCivil(y, m, daysInMonth(y, m));
    if (s.months) {
        int mm = m - 1 + s.months;
        int yy = y + mm / 12;
        mm = mm % 12 + 1;
        const int dd = std::min(d, daysInMonth(yy, mm));
        return dayFromCivil(yy, mm, dd) - kDay;   // << 1 mois >> du 10 : jusqu'au 9 du mois suivant, compris
    }
    return base + static_cast<Seconds>(std::max(0, s.days - 1)) * kDay;   // << 1 semaine >> du 10 : jusqu'au 16, compris
}

void DatePicker::choose(Seconds day) {
    day_ = day ? dayOf(day) : 0;
    close();
    changed->emit(day_);
}

void DatePicker::pick(Seconds day) { choose(day); }

void DatePicker::pickShortcut(std::size_t i) {
    if (i < shortcuts_.size()) choose(shortcutDay(i));
}

gfx::Rect DatePicker::eventBounds() const {
    if (!open_) return bounds();
    if (const auto s = surfaceSize(); s.w > 0.f && s.h > 0.f) return {0.f, 0.f, s.w, s.h};
    const gfx::Rect b = bounds(), p = popupRect();
    const float top = std::min(b.y, p.y), bot = std::max(b.bottom(), p.bottom());
    return {std::min(b.x, p.x), top, std::max(b.w, p.w), bot - top};
}

// ==================================================================== le dessin ==
namespace {

// Une petite icone de calendrier, tracee.
void calendarGlyph(const PaintContext& ctx, gfx::Point o, gfx::Color c) {
    const gfx::Rect r{o.x, o.y + 2.f, 14.f, 12.f};
    shapes::strokeRoundedRect(ctx.r, r, c, 2.f, 1.4f);
    ctx.r.fillRect({r.x, r.y + 3.5f, r.w, 1.4f}, c);
    ctx.r.fillRect({r.x + 3.5f, o.y, 1.4f, 4.f}, c);
    ctx.r.fillRect({r.right() - 4.9f, o.y, 1.4f, 4.f}, c);
}

} // namespace

void DatePicker::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const gfx::Rect r = bounds();
    const float rad = std::min(ctx.theme.metric.radius, r.h * 0.25f);
    shapes::fillFramedRect(ctx.r, r, enabled() ? c.inputBg : c.panelBg, (focused() || open_) ? c.accent : c.border, rad);
    const float lh = ctx.r.lineHeight(kFont);
    calendarGlyph(ctx, {r.x + 10.f, r.y + (r.h - 14.f) * 0.5f}, open_ ? c.accent : c.textMuted);
    const std::string text = fieldText();
    float room = r.w - 34.f - 22.f;
    std::string shown = text;
    if (measureWidth(shown, kFont) > room) shown = shown.substr(0, ctx.r.fitCharacters(shown, kFont, std::max(0.f, room - 12.f))) + "...";
    ctx.r.drawText({r.x + 32.f, r.y + (r.h - lh) * 0.5f}, shown, kFont, !enabled() ? c.textDisabled : day_ ? c.text : c.textMuted);
    const float cx = r.right() - 13.f, cy = r.y + r.h * 0.5f;
    const float dir = (open_ && flipped_) ? -1.f : 1.f;
    const gfx::Color arrow = enabled() ? c.textMuted : c.textDisabled;
    ctx.r.line({cx - 4.f, cy - 2.f * dir}, {cx, cy + 2.f * dir}, arrow, 1.5f);
    ctx.r.line({cx, cy + 2.f * dir}, {cx + 4.f, cy - 2.f * dir}, arrow, 1.5f);
}

void DatePicker::onPaintOverlay(const PaintContext& ctx) {
    if (!open_) return;
    const auto& c = ctx.theme.color;
    const gfx::Rect p = popupRect();
    const float rad = std::max(6.f, ctx.theme.metric.radius * 2.f);
    // l'ombre, puis la carte
    ctx.r.fillRoundedRect({p.x + 1.f, p.y + 4.f, p.w, p.h}, gfx::Color{15, 23, 42, 40}, rad);
    shapes::fillFramedRect(ctx.r, p, c.panelBg, c.border, rad);
    const float lh = ctx.r.lineHeight(kFont), lhs = ctx.r.lineHeight(kSmall);
    // le mois et ses fleches
    const std::string title = monthTitle();
    const gfx::FontId bold = gfx::semibold(kFont);
    ctx.r.drawText({p.x + (p.w - measureWidth(title, bold)) * 0.5f, p.y + kPad + (kHeadH - lh) * 0.5f - 3.f}, title, bold, c.text);
    for (const auto& [rr, left] : {std::pair{prevRect(), true}, std::pair{nextRect(), false}}) {
        const float cx = rr.x + rr.w * 0.5f, cy = rr.y + rr.h * 0.5f, s = left ? 1.f : -1.f;
        ctx.r.line({cx + 2.f * s, cy - 4.f}, {cx - 2.f * s, cy}, c.textMuted, 1.6f);
        ctx.r.line({cx - 2.f * s, cy}, {cx + 2.f * s, cy + 4.f}, c.textMuted, 1.6f);
    }
    // les jours de la semaine
    const gfx::Rect g = gridRect();
    const float cw = g.w / 7.f;
    for (int i = 0; i < 7; ++i)
        ctx.r.drawText({g.x + cw * static_cast<float>(i) + (cw - measureWidth(kWeek[i], kSmall)) * 0.5f, g.y - kWeekH + (kWeekH - lhs) * 0.5f}, kWeek[i], kSmall, c.textMuted);
    // les jours
    const Seconds first = firstShown(), now = today();
    const Seconds lo = rangeStart_ && day_ && rangeStart_ <= day_ ? rangeStart_ : 0;
    const Seconds hi = lo ? day_ : 0;
    const gfx::Color band = c.accent.withAlpha(36);
    for (int k = 0; k < 42; ++k) {
        const Seconds d = first + static_cast<Seconds>(k) * kDay;
        int y = 0, m = 0, dd = 0;
        civilFromDay(d, y, m, dd);
        const gfx::Rect cell{g.x + cw * static_cast<float>(k % 7), g.y + kCell * static_cast<float>(k / 7), cw, kCell};
        const bool inMonth = m == month_ && y == year_;
        const bool disabled = min_ && d < min_;
        if (lo && d >= lo && d <= hi) {   // la periode : une bande, arrondie a ses bouts
            gfx::Rect bandR{cell.x, cell.y + 2.f, cell.w, cell.h - 4.f};
            if (d == lo) bandR = {cell.x + cell.w * 0.5f, bandR.y, cell.w * 0.5f, bandR.h};
            if (d == hi) bandR = d == lo ? gfx::Rect{} : gfx::Rect{cell.x, bandR.y, cell.w * 0.5f, bandR.h};
            if (bandR.w > 0.f) ctx.r.fillRect(bandR, band);
        }
        const float dia = std::min(cell.w, cell.h) - 4.f;
        const gfx::Rect disc{cell.x + (cell.w - dia) * 0.5f, cell.y + (cell.h - dia) * 0.5f, dia, dia};
        const bool chosen = d == day_ || (lo && d == lo);
        gfx::Color ink = !inMonth || disabled ? c.textDisabled : c.text;
        gfx::FontId font = kSmall;
        if (chosen) {
            ctx.r.fillRoundedRect(disc, c.accent, dia * 0.5f);
            ink = c.textInverted;
            font = gfx::semibold(kSmall);
        } else if (d == hot_ && !disabled) {
            ctx.r.fillRoundedRect(disc, c.accent.withAlpha(30), dia * 0.5f);
        }
        if (d == now && !chosen) {
            shapes::strokeRoundedRect(ctx.r, disc, c.accent, dia * 0.5f, 1.5f);
            ink = c.accent;
            font = gfx::semibold(kSmall);
        }
        const std::string label = std::to_string(dd);
        ctx.r.drawText({cell.x + (cell.w - measureWidth(label, font)) * 0.5f, cell.y + (cell.h - lhs) * 0.5f}, label, font, ink);
    }
    // les raccourcis
    if (!shortcuts_.empty()) {
        ctx.r.fillRect({p.x + kPad, g.bottom() + 6.f, p.w - 2.f * kPad, 1.f}, c.border);
        for (std::size_t i = 0; i < shortcuts_.size(); ++i) {
            const gfx::Rect cr = shortcutRect(i);
            if (cr.right() > p.right() - kPad + 0.5f) break;   // ce qui ne tient pas : pas montre
            const bool on = day_ && shortcutDay(i) == day_;
            shapes::fillFramedRect(ctx.r, cr, on ? c.accent.withAlpha(30) : c.panelBg, on ? c.accent.withAlpha(90) : c.border, kChipH * 0.5f);
            ctx.r.drawText({cr.x + 8.f, cr.y + (cr.h - lhs) * 0.5f}, shortcuts_[i].label, kSmall, on ? c.accent : c.text);
        }
    }
    if (clearable_) {
        const gfx::Rect cr = clearRect();
        shapes::fillFramedRect(ctx.r, cr, c.panelBg, c.border, ctx.theme.metric.radius);
        ctx.r.drawText({cr.x + 10.f, cr.y + (cr.h - lhs) * 0.5f}, "Effacer", kSmall, c.text);
        if (lo && hi) {
            const long long n = (hi - lo) / kDay + 1;
            const std::string note = std::to_string(n) + (n == 1 ? " jour" : " jours");
            ctx.r.drawText({p.x + kPad, cr.y + (cr.h - lhs) * 0.5f}, note, kSmall, c.textMuted);
        }
    }
}

// ================================================================== les gestes ==
EventResult DatePicker::onEvent(const InputEvent& ev) {
    if (!enabled()) return EventResult::Ignored;
    if (!open_) {
        if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left && bounds().contains(d->pos)) {
            grabFocus();
            open();
            return EventResult::Consumed;
        }
        if (const auto* k = std::get_if<KeyDown>(&ev); k && focused() && (k->key == Key::Return || k->key == Key::Space || (k->key == Key::Down && k->mods.alt))) {
            open();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const Seconds d = dayAt(m->pos);
        if (d != hot_) {
            hot_ = d;
            invalidate();
        }
        return EventResult::Consumed;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (popupRect().contains(w->pos) && w->dy != 0.f) shiftMonth(w->dy > 0.f ? -1 : 1);
        return EventResult::Consumed;
    }
    if (std::get_if<MouseUp>(&ev)) return EventResult::Consumed;
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        const gfx::Rect p = popupRect();
        if (!p.contains(d->pos)) {
            close();   // un clic a cote (ou sur le champ) ferme
            return EventResult::Consumed;
        }
        if (prevRect().contains(d->pos)) {
            shiftMonth(-1);
            return EventResult::Consumed;
        }
        if (nextRect().contains(d->pos)) {
            shiftMonth(1);
            return EventResult::Consumed;
        }
        if (const Seconds day = dayAt(d->pos); day && !(min_ && day < min_)) {
            choose(day);
            return EventResult::Consumed;
        }
        for (std::size_t i = 0; i < shortcuts_.size(); ++i)
            if (shortcutRect(i).contains(d->pos) && shortcutRect(i).right() <= p.right() - kPad + 0.5f) {
                pickShortcut(i);
                return EventResult::Consumed;
            }
        if (clearable_ && clearRect().contains(d->pos)) {
            choose(0);
            return EventResult::Consumed;
        }
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        const Seconds base = hot_ ? hot_ : (day_ ? day_ : today());
        Seconds next = 0;
        switch (k->key) {
            case Key::Escape: close(); return EventResult::Consumed;
            case Key::Return:
            case Key::Space:
                if (hot_ && !(min_ && hot_ < min_)) choose(hot_);
                return EventResult::Consumed;
            case Key::Left: next = base - kDay; break;
            case Key::Right: next = base + kDay; break;
            case Key::Up: next = base - 7 * kDay; break;
            case Key::Down: next = base + 7 * kDay; break;
            case Key::PageUp: shiftMonth(-1); return EventResult::Consumed;
            case Key::PageDown: shiftMonth(1); return EventResult::Consumed;
            default: return EventResult::Consumed;
        }
        hot_ = next;
        int y = 0, m = 0, dd = 0;
        civilFromDay(hot_, y, m, dd);
        year_ = y;
        month_ = m;
        invalidate();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

} // namespace ui
