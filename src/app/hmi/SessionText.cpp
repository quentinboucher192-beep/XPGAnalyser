// =============================================================================
//  app/hmi/SessionText.cpp - 1.47.0 : voir SessionText.hpp
// =============================================================================
#include "SessionText.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace app::sessionui {

namespace {

const std::string kEllipsis = "\xE2\x80\xA6";

// La longueur (en octets) du caractere UTF-8 qui commence a `i`.
std::size_t charLen(std::string_view s, std::size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + n > s.size()) n = s.size() - i;
    for (std::size_t k = 1; k < n; ++k)
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return k;
    return n;
}

// Les octets du plus long debut de `s` qui tient dans `width` (au moins un caractere si `atLeastOne`).
std::size_t prefixFitting(std::string_view s, gfx::FontId font, float width, bool atLeastOne) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t next = i + charLen(s, i);
        if (ui::measureWidth(s.substr(0, next), font) > width) break;
        best = next;
        i = next;
    }
    if (best == 0 && atLeastOne && !s.empty()) best = charLen(s, 0);
    return best;
}

void wrapParagraph(std::string_view para, gfx::FontId font, float width, std::vector<std::string>& out) {
    std::string line;
    std::size_t i = 0;
    const auto flushLine = [&] {
        out.push_back(line);
        line.clear();
    };
    bool any = false;
    while (i < para.size()) {
        while (i < para.size() && para[i] == ' ') ++i;
        if (i >= para.size()) break;
        std::size_t j = i;
        while (j < para.size() && para[j] != ' ') ++j;
        std::string_view word = para.substr(i, j - i);
        i = j;
        any = true;
        const std::string trial = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (ui::measureWidth(trial, font) <= width) {
            line = trial;
            continue;
        }
        if (!line.empty()) flushLine();
        // Un mot plus long que la ligne : coupe entre deux caracteres, jamais au milieu d'un caractere.
        while (!word.empty() && ui::measureWidth(word, font) > width) {
            const std::size_t n = prefixFitting(word, font, width, true);
            out.emplace_back(word.substr(0, n));
            word.remove_prefix(n);
        }
        line = std::string(word);
    }
    if (!line.empty() || !any) out.push_back(line);
}

} // namespace

std::vector<std::string> wrap(std::string_view text, gfx::FontId font, float width) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string_view para = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        wrapParagraph(para, font, width, out);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

bool fits(std::string_view text, gfx::FontId font, float width) { return ui::measureWidth(text, font) <= width + 0.01f; }

std::string elide(std::string_view text, gfx::FontId font, float width) {
    if (fits(text, font, width)) return std::string(text);
    const float ell = ui::measureWidth(kEllipsis, font);
    if (ell > width) return {};
    std::size_t n = prefixFitting(text, font, width - ell, false);
    while (n > 0 && text[n - 1] == ' ') --n;
    return std::string(text.substr(0, n)) + kEllipsis;
}

float buttonWidth(std::string_view text, float minimum) {
    return std::max(minimum, std::ceil(ui::measureWidth(text, kButtonFont) + 2.f * kButtonPad));
}

void fitButton(ui::Button& b, const std::string& full, float width, const std::string& brief, const std::string& tip) {
    std::string label;
    if (buttonWidth(full) <= width + 0.5f) label = full;
    else if (!brief.empty() && buttonWidth(brief) <= width + 0.5f) label = brief;
    else {
        label = elide(brief.empty() ? full : brief, kButtonFont, std::max(0.f, width - 2.f * kButtonPad));
        if (label.empty()) label = elide(brief.empty() ? full : brief, kButtonFont, std::max(0.f, width - 8.f));
    }
    if (label != b.text()) b.setText(label);
    const std::string t = label == full ? tip : (tip.empty() ? full : full + " \xE2\x80\x94 " + tip);
    if (t != b.tooltip()) b.setTooltip(t);
}

float fitButtons(std::vector<ButtonSpec>& specs, float x, float y, float width, float height, float gap, bool rightAligned, bool wrapFirst) {
    std::vector<ButtonSpec*> items;
    for (auto& s : specs)
        if (s.button) items.push_back(&s);
    if (items.empty()) return 0.f;
    const std::size_t n = items.size();
    std::vector<float> full(n), brief(n), minimum(n), w(n);
    for (std::size_t i = 0; i < n; ++i) {
        full[i] = buttonWidth(items[i]->full);
        brief[i] = items[i]->brief.empty() ? full[i] : std::min(full[i], buttonWidth(items[i]->brief));
        minimum[i] = std::min(brief[i], items[i]->minWidth);
        w[i] = full[i];
    }
    const auto sum = [&](const std::vector<float>& v) {
        float s = 0.f;
        for (const float f : v) s += f;
        return s;
    };
    const float avail = width - gap * static_cast<float>(n - 1);
    std::vector<std::vector<std::size_t>> lines;
    if (sum(w) > avail) {
        // Les libelles courts, en commencant par la fin.
        for (std::size_t k = n; k-- > 0 && sum(w) > avail;) w[k] = brief[k];
    }
    if (sum(w) > avail && sum(minimum) <= avail && !wrapFirst) {
        // Raccourcis : chacun au plus `cap`, jamais moins que son minimum (la plus grande borne qui tient).
        float lo = 0.f, hi = *std::max_element(w.begin(), w.end());
        const std::vector<float> start = w;
        for (int it = 0; it < 30; ++it) {
            const float cap = (lo + hi) * 0.5f;
            float s = 0.f;
            for (std::size_t i = 0; i < n; ++i) s += std::min(start[i], std::max(cap, minimum[i]));
            (s <= avail ? lo : hi) = cap;
        }
        for (std::size_t i = 0; i < n; ++i) w[i] = std::floor(std::min(start[i], std::max(lo, minimum[i])));
    }
    if (sum(w) <= avail + 0.5f) {
        lines.emplace_back();
        for (std::size_t i = 0; i < n; ++i) lines.back().push_back(i);
    } else {
        // Meme raccourcis, ils ne tiennent pas sur une ligne : plusieurs lignes (chacun a son libelle court).
        for (std::size_t i = 0; i < n; ++i) w[i] = std::min(brief[i], width);
        float used = 0.f;
        for (std::size_t i = 0; i < n; ++i) {
            if (lines.empty() || (used > 0.f && used + gap + w[i] > width + 0.5f)) {
                lines.emplace_back();
                used = 0.f;
            }
            used += (used > 0.f ? gap : 0.f) + w[i];
            lines.back().push_back(i);
        }
    }
    float ly = y;
    for (const auto& line : lines) {
        float lw = 0.f;
        for (const auto i : line) lw += w[i];
        lw += gap * static_cast<float>(line.size() - 1);
        float bx = rightAligned ? x + width - lw : x;
        for (const auto i : line) {
            items[i]->button->setBounds({std::round(bx), ly, w[i], height});
            fitButton(*items[i]->button, items[i]->full, w[i], items[i]->brief, items[i]->tip);
            bx += w[i] + gap;
        }
        ly += height + gap;
    }
    return ly - y - gap;
}

void fitPlaceholder(ui::InputText& field, float width, const std::vector<std::string>& choices) {
    if (choices.empty()) return;
    std::string pick = choices.back();
    for (const auto& c : choices)
        if (ui::measureWidth(c, kButtonFont) + 18.f <= width) {
            pick = c;
            break;
        }
    if (ui::measureWidth(pick, kButtonFont) + 18.f > width) pick = elide(pick, kButtonFont, std::max(0.f, width - 18.f));
    field.setPlaceholder(pick);
}

const std::vector<std::string>& messagePlaceholders() {
    static const std::vector<std::string> v{"\xC3\x89" "cris \xC3\xA0 l'\xC3\xA9quipe\xE2\x80\xA6 (Entr\xC3\xA9" "e)", "\xC3\x89" "cris \xC3\xA0 l'\xC3\xA9quipe\xE2\x80\xA6",
                                            "\xC3\x89" "cris\xE2\x80\xA6"};
    return v;
}

std::string rowText(const Row& r) {
    std::string s = r.time + " " + (r.system ? std::string("Session") : r.who) + " : " + r.text;
    if (r.count > 1) s += " (\xC3\x97" + std::to_string(r.count) + ")";
    return s;
}

// ------------------------------------------------------------------------------------- LineList ---
namespace {
constexpr float kPadX = 8.f;
constexpr float kTimeW = 46.f;    // "14:32" et l'espace
constexpr float kDotW = 14.f;     // la pastille de couleur et l'espace
constexpr float kIconW = 18.f;    // l'icone devant le texte (journal, session)
float lineStep() { return std::ceil(ui::lineHeight(kText)) + 3.f; }

std::string textOf(const Row& r) { return r.count > 1 ? r.text + "  (\xC3\x97" + std::to_string(r.count) + ")" : r.text; }
} // namespace

LineList::LineList(std::string id) : ui::Widget(std::move(id)) {}

void LineList::setRows(std::vector<Row> rows) {
    const bool wasBottom = stick_ || atBottom();
    rows_ = std::move(rows);
    placedWidth_ = -1.f;
    stick_ = wasBottom;
    invalidateLayout();
    invalidate();
}

void LineList::setEmptyText(std::string text) {
    if (text == empty_) return;
    empty_ = std::move(text);
    placedWidth_ = -1.f;
    invalidateLayout();
    invalidate();
}

void LineList::scrollToBottom() {
    stick_ = true;
    offset_ = std::max(0.f, content_ - area().h);
    invalidate();
}

bool LineList::atBottom() const noexcept { return offset_ >= std::max(0.f, content_ - area().h) - 1.f; }

gfx::Rect LineList::area() const noexcept {
    const auto b = bounds();
    const bool bar = content_ > b.h + 0.5f;
    return {b.x, b.y, std::max(0.f, b.w - (bar ? ui::kScrollBarThickness + 2.f : 0.f)), b.h};
}

std::vector<std::string> LineList::wrappedText(std::size_t i) const { return i < placed_.size() ? placed_[i].lines : std::vector<std::string>{}; }

void LineList::place() {
    const auto b = bounds();
    const float step = lineStep();
    // Deux passes au plus : sans la barre, puis avec (si le contenu deborde).
    for (int pass = 0; pass < 2; ++pass) {
        const float w = pass == 0 ? b.w : std::max(0.f, b.w - ui::kScrollBarThickness - 2.f);
        columns_ = w >= 460.f;
        placed_.clear();
        float y = 4.f;
        if (columns_) {
            whoX_ = kPadX + kTimeW;
            whoW_ = std::clamp(std::floor(w * 0.24f), 90.f, 220.f);
            textX_ = whoX_ + whoW_ + 10.f;
            textW_ = std::max(20.f, w - textX_ - kPadX);
        } else {
            whoX_ = kPadX + kTimeW;
            whoW_ = std::max(0.f, w - whoX_ - kPadX);
            textX_ = kPadX;
            textW_ = std::max(20.f, w - 2.f * kPadX);
        }
        for (const auto& r : rows_) {
            Placed p;
            const std::string who = r.system ? std::string("Session") : r.who;
            const float whoRoom = std::max(0.f, whoW_ - (r.color && !r.system && whoW_ >= kDotW + 8.f ? kDotW : 0.f));
            p.who = elide(who, r.mine ? kTextBold : kText, whoRoom);
            p.whoCut = p.who != who;
            const float iconW = r.journal || r.system ? kIconW : 0.f;
            p.lines = wrap(textOf(r), kText, std::max(10.f, textW_ - iconW));
            if (p.lines.empty()) p.lines.emplace_back();
            p.y = y;
            if (columns_) {
                p.textX = textX_;
                p.textY = 0.f;
                p.h = static_cast<float>(p.lines.size()) * step + 6.f;
            } else {
                p.textX = textX_;
                p.textY = step;
                p.h = static_cast<float>(p.lines.size() + 1) * step + 6.f;
            }
            y += p.h;
            placed_.push_back(std::move(p));
        }
        emptyLines_ = rows_.empty() ? wrap(empty_, kText, std::max(10.f, w - 2.f * kPadX)) : std::vector<std::string>{};
        content_ = rows_.empty() ? 8.f + static_cast<float>(emptyLines_.size()) * step : y + 4.f;
        if (content_ <= b.h + 0.5f) break;
    }
    placedWidth_ = b.w;
}

void LineList::onLayout() {
    place();
    const auto a = area();
    scroll_.place(bounds(), content_, bounds().h, {bounds().right() - ui::kScrollBarThickness, bounds().y, ui::kScrollBarThickness, bounds().h});
    const float maxOffset = std::max(0.f, content_ - a.h);
    offset_ = stick_ ? maxOffset : std::clamp(offset_, 0.f, maxOffset);
}

void LineList::onPaint(const ui::PaintContext& ctx) {
    if (placedWidth_ != bounds().w) onLayout();
    const auto& c = ctx.theme.color;
    auto& r = ctx.r;
    const auto a = area();
    const float step = lineStep();
    const float th = r.lineHeight(kText);
    r.pushClip(a);
    if (rows_.empty()) {
        float y = a.y + 6.f - offset_;
        for (const auto& l : emptyLines_) {
            r.drawText({a.x + kPadX, y}, l, kText, c.textMuted);
            y += step;
        }
    }
    for (std::size_t i = 0; i < rows_.size() && i < placed_.size(); ++i) {
        const auto& row = rows_[i];
        const auto& p = placed_[i];
        const float top = a.y + p.y - offset_;
        if (top > a.bottom() || top + p.h < a.y) continue;
        if (i > 0) r.fillRect({a.x + kPadX, std::round(top - 1.f), std::max(0.f, a.w - 2.f * kPadX), 1.f}, c.border.withAlpha(110));
        const float ty = top + 3.f + (step - th) * 0.5f;
        r.drawText({a.x + kPadX, ty}, elide(row.time, kText, std::max(0.f, a.w - 2.f * kPadX)), kText, c.textMuted);
        float wx = a.x + whoX_;
        if (row.color && !row.system && whoW_ >= kDotW + 8.f) {
            r.fillRoundedRect({wx, ty + std::round((th - 8.f) * 0.5f), 8.f, 8.f}, gfx::Color::rgb(row.color), 4.f);
            wx += kDotW;
        }
        if (!p.who.empty()) r.drawText({wx, ty}, p.who, row.mine ? kTextBold : kText, row.system ? c.textMuted : c.text);
        float tx = a.x + p.textX;
        const float ly0 = top + 3.f + p.textY;
        if (row.journal || row.system) {
            ui::drawIcon(r, row.journal ? ui::Icon::History : ui::Icon::Info, {tx, ly0 + std::round((step - 14.f) * 0.5f), 14.f, 14.f},
                         row.journal ? c.accent : c.textMuted);
            tx += kIconW;
        }
        float ly = ly0 + (step - th) * 0.5f;
        for (const auto& l : p.lines) {
            if (!l.empty()) r.drawText({tx, ly}, l, kText, row.system ? c.textMuted : c.text);
            ly += step;
        }
    }
    r.popClip();
    scroll_.paint(ctx, offset_);
}

ui::EventResult LineList::onEvent(const ui::InputEvent& ev) {
    bool changed = false;
    if (scroll_.handle(*this, ev, offset_, &changed)) {
        if (changed) stick_ = atBottom();
        return ui::EventResult::Consumed;
    }
    return ui::Widget::onEvent(ev);
}

std::string LineList::liveTooltip(gfx::Point mouse) const {
    const auto a = area();
    if (!a.contains(mouse)) return {};
    const float y = mouse.y - a.y + offset_;
    for (std::size_t i = 0; i < placed_.size() && i < rows_.size(); ++i) {
        if (y < placed_[i].y || y >= placed_[i].y + placed_[i].h) continue;
        const auto& r = rows_[i];
        if (r.system) return "La session \xC2\xB7 " + r.time;
        std::string t = (r.journal ? "Journal de la session \xC2\xB7 " : "Discussion \xC2\xB7 ") + r.who + " \xC2\xB7 " + r.time;
        if (r.journal && r.count > 1) t += " \xC2\xB7 " + std::to_string(r.count) + " gestes";
        return t;
    }
    return {};
}

} // namespace app::sessionui
