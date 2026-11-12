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
constexpr float kQuoteBar = 8.f;  // 1.50.0 : le filet de l'extrait cite et l'espace
constexpr float kHoverW = 24.f, kHoverH = 20.f, kHoverGap = 2.f;   // 1.50.0 : les pictogrammes du survol
float lineStep() { return std::ceil(ui::lineHeight(kText)) + 3.f; }

std::string textOf(const Row& r) { return r.count > 1 ? r.text + "  (\xC3\x97" + std::to_string(r.count) + ")" : r.text; }

const std::string kSeeAll = "\xE2\x80\xA6 voir tout";

// 1.50.0 : le debut d'un message (sa premiere ligne qui a du texte, 80 caracteres au plus, << ... >> s'il y a la suite) -
// comme session::chatExcerpt (l'extrait qui voyage avec la reponse) ; ici sans la bibliotheque des sessions (l'editeur
// de vues et ses essais compilent ce fichier sans elle).
std::string excerptOf(std::string_view text) {
    constexpr std::size_t kMax = 80;
    const auto blank = [](char ch) { return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t'; };
    std::size_t start = 0;
    while (start < text.size() && blank(text[start])) ++start;
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string out;
    std::size_t chars = 0, i = start;
    while (i < end && chars < kMax) {
        const std::size_t n = std::min(charLen(text, i), end - i);
        const auto u = static_cast<unsigned char>(text[i]);
        if (u < 0x20 || u == 0x7F) out.push_back(' ');
        else out.append(text.substr(i, n));
        i += n;
        ++chars;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    bool more = i < end;
    for (std::size_t j = end; !more && j < text.size(); ++j) more = !blank(text[j]);
    if (more && !out.empty()) out += kEllipsis;
    return out;
}
const std::string kReduce = "R\xC3\xA9" "duire";
} // namespace

// ---- 1.50.0 : repondre, reduire, copier ----
std::string copyText(const Row& r, bool withAuthor) { return withAuthor ? rowText(r) : textOf(r); }

std::string replyLabel(const std::string& who, const std::string& excerpt) {
    return "En r\xC3\xA9ponse \xC3\xA0 " + (who.empty() ? std::string("un message") : who) + " : \xC2\xAB " + excerpt + " \xC2\xBB";
}

std::string rowKey(const Row& r) {
    if (!r.id.empty()) return r.id;
    return std::string(r.journal ? "j|" : r.system ? "s|" : "c|") + r.time + "|" + r.who + "|" + r.text;
}

bool canReply(const Row& r) { return !r.journal && !r.system && !r.id.empty(); }

LineList::LineList(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }   // 1.50.0 : Ctrl+C, les fleches

void LineList::setRows(std::vector<Row> rows) {
    const bool wasBottom = stick_ || atBottom();
    rows_ = std::move(rows);
    placedWidth_ = -1.f;
    stick_ = wasBottom;
    if (hover_ >= static_cast<int>(rows_.size())) hover_ = -1;
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
            p.key = rowKey(r);
            const std::string who = r.system ? std::string("Session") : r.who;
            const float whoRoom = std::max(0.f, whoW_ - (r.color && !r.system && whoW_ >= kDotW + 8.f ? kDotW : 0.f));
            p.who = elide(who, r.mine ? kTextBold : kText, whoRoom);
            p.whoCut = p.who != who;
            const float iconW = r.journal || r.system ? kIconW : 0.f;
            p.lines = wrap(textOf(r), kText, std::max(10.f, textW_ - iconW));
            if (p.lines.empty()) p.lines.emplace_back();
            // 1.50.0 : reduit au-dela de kCollapseLines + 1 lignes (sauf deballe a la main) ; l'extrait d'une reponse
            // au-dessus du texte ; << ... voir tout >> / << Reduire >> dessous. Chacun sur une ligne, dans la colonne.
            p.collapsible = p.lines.size() > kCollapseLines + 1;
            p.collapsed = p.collapsible && expanded_.count(p.key) == 0;
            p.shown = p.collapsed ? kCollapseLines : p.lines.size();
            float cy = columns_ ? 0.f : step;   // etroit : qui sur la premiere ligne, le texte dessous
            if (!r.replyText.empty() || !r.replyWho.empty()) {
                const std::string quoted = r.replyWho.empty() ? r.replyText : r.replyWho + " : " + r.replyText;
                p.quote = elide(quoted, kText, std::max(0.f, textW_ - kQuoteBar));
                if (!p.quote.empty()) {
                    p.quoteY = cy;
                    cy += step;
                }
            }
            p.textX = textX_;
            p.textY = cy;
            cy += static_cast<float>(p.shown) * step;
            if (p.collapsible) {
                p.toggle = elide(p.collapsed ? kSeeAll : kReduce, kText, textW_);
                p.toggleY = cy;
                cy += step;
            }
            p.y = y;
            p.h = cy + 6.f;
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

int LineList::rowAt(gfx::Point pt) const {
    const auto a = area();
    if (!a.contains(pt)) return -1;
    const float y = pt.y - a.y + offset_;
    for (std::size_t i = 0; i < placed_.size() && i < rows_.size(); ++i)
        if (y >= placed_[i].y && y < placed_[i].y + placed_[i].h) return static_cast<int>(i);
    return -1;
}

int LineList::hoverButtonCount(std::size_t i) const {
    if (i >= rows_.size()) return 0;
    return replies_ && onReply && canReply(rows_[i]) ? 2 : 1;
}

gfx::Rect LineList::hoverButton(std::size_t i, int which) const {
    if (i >= placed_.size() || i >= rows_.size() || which < 0 || which > 1) return {};
    const int n = hoverButtonCount(i);
    if (which == 0 && n < 2) return {};
    const auto a = area();
    const float top = a.y + placed_[i].y - offset_ + 2.f;
    const float right = a.right() - 4.f;
    const float x = which == 1 ? right - kHoverW : right - 2.f * kHoverW - kHoverGap;
    if (x < a.x + 2.f) return {};   // trop etroit : le clic droit reste
    return {x, top, kHoverW, kHoverH};
}

gfx::Rect LineList::quoteRect(std::size_t i) const {
    if (i >= placed_.size() || placed_[i].quote.empty()) return {};
    const auto a = area();
    const auto& p = placed_[i];
    return {a.x + p.textX, a.y + p.y - offset_ + 3.f + p.quoteY, kQuoteBar + ui::measureWidth(p.quote, kText), lineStep()};
}

gfx::Rect LineList::toggleRect(std::size_t i) const {
    if (i >= placed_.size() || placed_[i].toggle.empty()) return {};
    const auto a = area();
    const auto& p = placed_[i];
    return {a.x + p.textX, a.y + p.y - offset_ + 3.f + p.toggleY, ui::measureWidth(p.toggle, kText), lineStep()};
}

void LineList::onPaint(const ui::PaintContext& ctx) {
    if (placedWidth_ != bounds().w) onLayout();
    const auto& c = ctx.theme.color;
    auto& r = ctx.r;
    const auto a = area();
    const float step = lineStep();
    const float th = r.lineHeight(kText);
    const int sel = selected();
    const int hov = hovered() ? hover_ : -1;
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
        // 1.50.0 : le message choisi (un clic, ou celui qu'une citation montre) ; celui sous la souris.
        if (static_cast<int>(i) == sel) {
            r.fillRect({a.x, top, a.w, std::max(0.f, p.h - 1.f)}, c.selectionBg.withAlpha(90));
            r.fillRect({a.x, top, 3.f, std::max(0.f, p.h - 1.f)}, c.accent);
        } else if (static_cast<int>(i) == hov) {
            r.fillRect({a.x, top, a.w, std::max(0.f, p.h - 1.f)}, ctx.theme.brand.hover);
        }
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
        // 1.50.0 : l'extrait cite (un filet a la couleur d'accent, le texte discret) au-dessus du texte.
        if (!p.quote.empty()) {
            const float qy = top + 3.f + p.quoteY;
            r.fillRect({tx, qy + 2.f, 2.f, std::max(0.f, step - 4.f)}, c.accent);
            r.drawText({tx + kQuoteBar, qy + (step - th) * 0.5f}, p.quote, kText, c.textMuted);
        }
        const float ly0 = top + 3.f + p.textY;
        if (row.journal || row.system) {
            ui::drawIcon(r, row.journal ? ui::Icon::History : ui::Icon::Info, {tx, ly0 + std::round((step - 14.f) * 0.5f), 14.f, 14.f},
                         row.journal ? c.accent : c.textMuted);
            tx += kIconW;
        }
        float ly = ly0 + (step - th) * 0.5f;
        for (std::size_t k = 0; k < p.shown && k < p.lines.size(); ++k) {
            if (!p.lines[k].empty()) r.drawText({tx, ly}, p.lines[k], kText, row.system ? c.textMuted : c.text);
            ly += step;
        }
        if (!p.toggle.empty()) r.drawText({a.x + p.textX, top + 3.f + p.toggleY + (step - th) * 0.5f}, p.toggle, kText, c.accent);
    }
    // 1.50.0 : Repondre et Copier sur la ligne survolee (en haut a droite, sur un fond : ils passent sur le texte).
    if (hov >= 0)
        for (int k = 0; k < 2; ++k) {
            const auto b = hoverButton(static_cast<std::size_t>(hov), k);
            if (b.w <= 0.f) continue;
            r.fillRoundedRect(b, c.panelBg, 4.f);
            r.strokeRect(b, c.border, 1.f);
            ui::drawIcon(r, k == 0 ? ui::Icon::Undo : ui::Icon::Copy, b.inset(4.f, 3.f), c.text);
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
    // ---- 1.50.0 : survoler, choisir, repondre, copier, deballer, le menu du clic droit ----
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::Widget::onEvent(ev);
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!area().contains(d->pos)) return ui::Widget::onEvent(ev);
        grabFocus();
        const int i = rowAt(d->pos);
        if (d->button == ui::MouseButton::Right) {
            if (i >= 0) select(i);
            openMenu(i, d->pos);
            return ui::EventResult::Consumed;
        }
        if (d->button != ui::MouseButton::Left) return ui::Widget::onEvent(ev);
        if (i < 0) {
            select(-1);
            return ui::EventResult::Consumed;
        }
        const auto row = static_cast<std::size_t>(i);
        hover_ = i;
        if (hoverButton(row, 0).contains(d->pos)) {
            (void)reply(row);
            return ui::EventResult::Consumed;
        }
        if (hoverButton(row, 1).contains(d->pos)) {
            select(i);
            (void)copy(row, false);
            return ui::EventResult::Consumed;
        }
        if (quoteRect(row).contains(d->pos)) {
            if (!goToQuoted(row)) select(i);
            return ui::EventResult::Consumed;
        }
        if (toggleRect(row).contains(d->pos)) {
            setExpanded(row, collapsed(row));
            return ui::EventResult::Consumed;
        }
        select(i);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
        const int s = selected();
        if (k->key == ui::Key::C && k->mods.ctrl && !k->mods.shift && !k->mods.alt && s >= 0) {
            (void)copy(static_cast<std::size_t>(s), false);
            return ui::EventResult::Consumed;
        }
        if (ui::isContextMenuKey(*k)) {
            const auto a = area();
            gfx::Point at{a.x + 20.f, a.y + 10.f};
            if (s >= 0 && static_cast<std::size_t>(s) < placed_.size())
                at.y = std::clamp(a.y + placed_[static_cast<std::size_t>(s)].y - offset_ + 10.f, a.y, a.bottom());
            openMenu(s, at);
            return ui::EventResult::Consumed;
        }
        if ((k->key == ui::Key::Up || k->key == ui::Key::Down) && k->mods.none() && !rows_.empty()) {
            const int n = static_cast<int>(rows_.size());
            const int next = s < 0 ? n - 1 : std::clamp(s + (k->key == ui::Key::Up ? -1 : 1), 0, n - 1);
            select(next);
            reveal(static_cast<std::size_t>(next));
            return ui::EventResult::Consumed;
        }
    }
    return ui::Widget::onEvent(ev);
}

// ---- 1.50.0 : choisir, repondre, reduire / deballer, copier ----
void LineList::setRepliesEnabled(bool on) {
    if (on == replies_) return;
    replies_ = on;
    invalidate();
}

int LineList::selected() const {
    if (selectedKey_.empty()) return -1;
    if (placed_.size() == rows_.size()) {
        for (std::size_t i = 0; i < placed_.size(); ++i)
            if (placed_[i].key == selectedKey_) return static_cast<int>(i);
        return -1;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rowKey(rows_[i]) == selectedKey_) return static_cast<int>(i);
    return -1;
}

void LineList::select(int index) {
    const std::string key = index >= 0 && static_cast<std::size_t>(index) < rows_.size() ? rowKey(rows_[static_cast<std::size_t>(index)]) : std::string{};
    if (key == selectedKey_) return;
    selectedKey_ = key;
    invalidate();
}

int LineList::indexOfId(const std::string& id) const {
    if (id.empty()) return -1;
    for (std::size_t i = rows_.size(); i-- > 0;)
        if (!rows_[i].journal && !rows_[i].system && rows_[i].id == id) return static_cast<int>(i);
    return -1;
}

bool LineList::collapsible(std::size_t i) const { return i < placed_.size() && placed_[i].collapsible; }
bool LineList::collapsed(std::size_t i) const { return i < placed_.size() && placed_[i].collapsed; }

void LineList::setExpanded(std::size_t i, bool expanded) {
    if (i >= rows_.size()) return;
    const std::string key = rowKey(rows_[i]);
    if (expanded) expanded_.insert(key);
    else expanded_.erase(key);
    placedWidth_ = -1.f;
    invalidateLayout();
    invalidate();
    layout();   // la place des lignes suivantes change : refaite tout de suite (un clic suivant la lit)
}

void LineList::collapseAll() {
    if (expanded_.empty()) return;
    expanded_.clear();
    placedWidth_ = -1.f;
    invalidateLayout();
    invalidate();
    layout();
}

bool LineList::copy(std::size_t i, bool withAuthor) {
    if (i >= rows_.size()) return false;
    ui::setClipboardText(copyText(rows_[i], withAuthor));
    return true;
}

bool LineList::reply(std::size_t i) {
    if (i >= rows_.size() || !replies_ || !onReply || !canReply(rows_[i])) return false;
    onReply(rows_[i]);
    return true;
}

void LineList::reveal(std::size_t i) {
    if (i >= placed_.size()) return;
    const auto a = area();
    const auto& p = placed_[i];
    if (p.y - 2.f < offset_) offset_ = p.y - 2.f;
    else if (p.y + p.h + 2.f > offset_ + a.h) offset_ = std::min(p.y - 2.f, p.y + p.h + 2.f - a.h);
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - a.h));
    stick_ = atBottom();
    invalidate();
}

bool LineList::goToQuoted(std::size_t i) {
    if (i >= rows_.size() || rows_[i].replyId.empty()) return false;
    const int j = indexOfId(rows_[i].replyId);
    if (j < 0) return false;
    select(j);
    reveal(static_cast<std::size_t>(j));
    return true;
}

void LineList::setViewState(ViewState v) {
    expanded_ = std::move(v.expanded);
    selectedKey_ = std::move(v.selected);
    placedWidth_ = -1.f;
    invalidateLayout();
    invalidate();
}

void LineList::openMenu(int row, gfx::Point at) {
    if (!menu_) {
        menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".menu")));
        menu_->itemChosen->connect([this](int a) { runMenu(a); }).release();
    }
    const Row* r = row >= 0 && static_cast<std::size_t>(row) < rows_.size() ? &rows_[static_cast<std::size_t>(row)] : nullptr;
    menuKey_ = r ? rowKey(*r) : std::string{};
    const std::string none = "aucun message choisi";
    const auto item = [](std::string label, int id, ui::Icon icon, bool enabled, std::string why, std::string key = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.id = id;
        it.icon = icon;
        it.enabled = enabled;
        it.disabledReason = enabled ? std::string{} : std::move(why);
        it.shortcut = std::move(key);
        return it;
    };
    std::vector<ui::PopupMenu::Item> items;
    const bool canAnswer = r && replies_ && onReply && canReply(*r);
    const std::string noReply = !r ? none : r->journal ? std::string("une ligne du journal") : r->system ? std::string("une ligne de la session")
                                                                                                         : std::string("pas ici");
    items.push_back(item("R\xC3\xA9pondre", 1, ui::Icon::Undo, canAnswer, noReply));
    items.push_back(item("Copier", 2, ui::Icon::Copy, r != nullptr, none, "Ctrl+C"));
    items.push_back(item("Copier avec l'auteur et l'heure", 3, ui::Icon::None, r != nullptr, none));
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    const auto at_ = static_cast<std::size_t>(std::max(0, row));
    const bool coll = r && collapsible(at_);
    items.push_back(item(coll && !collapsed(at_) ? kReduce : std::string("D\xC3\xA9" "baller"), 4, ui::Icon::None, coll,
                         r ? std::string("le message se lit en entier") : none));
    bool anyOpen = false;
    for (const auto& p : placed_) anyOpen = anyOpen || (p.collapsible && !p.collapsed);
    items.push_back(item("Tout r\xC3\xA9" "duire", 5, ui::Icon::Collapse, anyOpen, "aucun message d\xC3\xA9" "ball\xC3\xA9"));
    menu_->setItems(std::move(items));
    menu_->openInWindow(at);
}

void LineList::runMenu(int action) {
    if (menu_ && menu_->isOpen()) menu_->close();   // un choix ferme le menu (meme rendu par programme)
    int i = -1;
    if (!menuKey_.empty())
        for (std::size_t k = 0; k < rows_.size(); ++k)
            if (rowKey(rows_[k]) == menuKey_) {
                i = static_cast<int>(k);
                break;
            }
    if (action == 5) {
        collapseAll();
        return;
    }
    if (i < 0) return;
    const auto row = static_cast<std::size_t>(i);
    switch (action) {
        case 1: (void)reply(row); break;
        case 2: (void)copy(row, false); break;
        case 3: (void)copy(row, true); break;
        case 4: setExpanded(row, collapsed(row)); break;
        default: break;
    }
}

std::string LineList::liveTooltip(gfx::Point mouse) const {
    const auto a = area();
    if (!a.contains(mouse)) return {};
    const int at = rowAt(mouse);
    if (at < 0) return {};
    const auto i = static_cast<std::size_t>(at);
    const auto& r = rows_[i];
    if (at == hover_) {
        if (hoverButton(i, 0).contains(mouse)) return "R\xC3\xA9pondre \xC3\xA0 ce message";
        if (hoverButton(i, 1).contains(mouse)) return "Copier le message (Ctrl+C)";
    }
    if (quoteRect(i).contains(mouse))
        return replyLabel(r.replyWho, r.replyText)
             + (indexOfId(r.replyId) >= 0 ? std::string(" \xE2\x80\x94 clic : aller au message") : std::string(" \xE2\x80\x94 le message n'est plus dans la liste"));
    if (toggleRect(i).contains(mouse)) return collapsed(i) ? "Voir tout le message" : "R\xC3\xA9" "duire le message";
    if (r.system) return "La session \xC2\xB7 " + r.time;
    std::string t = (r.journal ? "Journal de la session \xC2\xB7 " : "Discussion \xC2\xB7 ") + r.who + " \xC2\xB7 " + r.time;
    if (r.journal && r.count > 1) t += " \xC2\xB7 " + std::to_string(r.count) + " gestes";
    return t;
}

// ------------------------------------------------------------------- 1.50.0 : ReplyBar ---
ReplyBar::ReplyBar(std::string id) : ui::Widget(std::move(id)) {
    auto b = std::make_unique<ui::Button>("Annuler la r\xC3\xA9ponse", this->id() + ".annuler");
    b->setIcon(ui::Icon::Close);
    b->setCompact(true);
    b->setStyle(ui::Button::Style::Flat);
    b->setTooltip("Annuler la r\xC3\xA9ponse (\xC3\x89" "chap)");
    cancel_ = &static_cast<ui::Button&>(addChild(std::move(b)));
    cancel_->clicked->connect([this] {
        clear();
        if (onCancel) onCancel();
    }).release();
}

void ReplyBar::set(const Row& row) {
    replyId_ = row.id;
    label_ = replyLabel(row.who, excerptOf(row.text));
    invalidateLayout();
    invalidate();
}

void ReplyBar::clear() {
    if (replyId_.empty() && label_.empty()) return;
    replyId_.clear();
    label_.clear();
    shown_.clear();
    invalidateLayout();
    invalidate();
}

void ReplyBar::onLayout() {
    const auto b = bounds();
    const bool button = b.w >= 60.f && b.h >= 18.f;
    cancel_->setVisibility(button ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    const float bw = std::min(24.f, b.h);
    if (button) cancel_->setBounds({b.right() - bw - 1.f, b.y + std::floor((b.h - bw) * 0.5f), bw, bw});
    const float left = b.x + 10.f;
    const float right = button ? cancel_->bounds().x - 6.f : b.right() - 4.f;
    textRect_ = {left, b.y, std::max(0.f, right - left), b.h};
    shown_ = elide(label_, kText, textRect_.w);
}

void ReplyBar::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    ctx.r.fillRoundedRect(b, c.inputBg, 4.f);
    ctx.r.fillRect({b.x + 2.f, b.y + 4.f, 3.f, std::max(0.f, b.h - 8.f)}, c.accent);
    if (!shown_.empty()) {
        ctx.r.pushClip(textRect_);
        ctx.r.drawText({textRect_.x, textRect_.y + (textRect_.h - ctx.r.lineHeight(kText)) * 0.5f}, shown_, kText, c.textMuted);
        ctx.r.popClip();
    }
}

std::string ReplyBar::liveTooltip(gfx::Point mouse) const {
    if (textRect_.contains(mouse)) return label_;
    return {};
}

} // namespace app::sessionui
