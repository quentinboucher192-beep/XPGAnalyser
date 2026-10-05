#include "HmiTrails.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>

namespace app {

namespace {

constexpr float kHeaderH = 40.f;
constexpr float kCardH = 138.f;
constexpr float kGap = 12.f;
constexpr float kNoteH = 44.f;
constexpr float kPad = 14.f;
constexpr float kIconBox = 26.f;          // lot API 7 : le carre de l'icone, devant le nom
// La police des legendes du theme (Fonts::caption) : la hauteur voulue se
// calcule sans renderer (ui::measureWidth, ui::lineHeight).
constexpr gfx::FontId kCaptionFont{13};

const char* const kDefaultNote =
    "Le didacticiel retient o\xC3\xB9 tu en es. \xC3\x80 la fin d'un parcours interactif : garder ce qu'il a cr\xC3\xA9\xC3\xA9, "
    "ou tout d\xC3\xA9" "faire (l'historique revient \xC3\xA0 l'\xC3\xA9tat d'avant).";

gfx::Color withAlpha(gfx::Color c, std::uint8_t a) {
    c.a = a;
    return c;
}

// Retirer le dernier caractere entier (UTF-8).
void popCharacter(std::string& s) {
    if (s.empty()) return;
    std::size_t cut = s.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.erase(cut);
}

// Le texte coupe a la largeur, avec des points de suspension.
template <class Measure>
std::string elideWith(const std::string& text, float width, Measure measure) {
    if (measure(text) <= width) return text;
    std::string s = text;
    const std::string dots = "\xE2\x80\xA6";
    while (!s.empty()) {
        popCharacter(s);
        if (measure(s + dots) <= width) return s + dots;
    }
    return dots;
}

std::string elide(const ui::PaintContext& ctx, gfx::FontId font, const std::string& text, float width) {
    return elideWith(text, width, [&](const std::string& t) { return ctx.r.measure(t, font).width; });
}

// Lot API 7 : des lignes qui tiennent dans `width`, coupees aux espaces ; au
// plus `maxLines` (0 : sans limite), la derniere finie par des points de
// suspension s'il reste des mots.
template <class Measure>
std::vector<std::string> wrapWith(const std::string& text, float width, int maxLines, Measure measure) {
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto sp = text.find(' ', at);
        const auto end = sp == std::string::npos ? text.size() : sp;
        if (end > at) words.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    for (; i < words.size(); ++i) {
        const std::string trial = line.empty() ? words[i] : line + " " + words[i];
        if (!line.empty() && measure(trial) > width) {
            lines.push_back(line);
            line = words[i];
            if (maxLines > 0 && static_cast<int>(lines.size()) == maxLines) break;
        } else {
            line = trial;
        }
    }
    if (maxLines > 0 && static_cast<int>(lines.size()) == maxLines) {
        // Plus de place : la derniere ligne prend la suite, elidee.
        std::string rest = lines.back();
        for (; i < words.size(); ++i) rest += " " + words[i];
        lines.back() = elideWith(rest, width, measure);
        return lines;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void drawCheck(const ui::PaintContext& ctx, float x, float y, gfx::Color c) {
    ctx.r.line({x, y + 5.f}, {x + 3.5f, y + 8.5f}, c, 2.f);
    ctx.r.line({x + 3.5f, y + 8.5f}, {x + 10.f, y + 1.f}, c, 2.f);
}

// Un texte en faux gras : trace deux fois, a un pixel (le theme n'a pas de face grasse).
void drawBold(const ui::PaintContext& ctx, gfx::Point at, const std::string& text, gfx::FontId font, gfx::Color c) {
    ctx.r.drawText(at, text, font, c);
    ctx.r.drawText({at.x + 1.f, at.y}, text, font, c);
}

bool started(const TrailCard& t) { return t.running || (t.reached > 0 && t.reached < t.steps); }

} // namespace

HmiTrailCards::HmiTrailCards(std::string id) : ui::Widget(std::move(id)) {}

void HmiTrailCards::setCards(std::vector<TrailCard> cards) {
    cards_ = std::move(cards);
    hover_ = -1;
    invalidateLayout();
    invalidate();
}

void HmiTrailCards::setStyle(Style style) {
    style_ = std::move(style);
    style_.summaryLines = std::clamp(style_.summaryLines, 1, 4);
    invalidateLayout();
    invalidate();
}

std::string HmiTrailCards::buttonLabel(const TrailCard& c) {
    if (c.reached > 0 && c.reached < c.steps) return "Reprendre";
    if (c.done) return "Refaire";
    return "Commencer";
}

std::string HmiTrailCards::statusText(const TrailCard& c) {
    if (c.running)
        return c.reached > 0 ? "ouvert \xC2\xB7 \xC3\xA9tape " + std::to_string(c.reached) : std::string("ouvert en ce moment");
    if (c.reached > 0 && c.reached < c.steps) return "en cours " + std::to_string(c.reached) + " / " + std::to_string(c.steps);
    if (c.done) return c.doneOn.empty() ? std::string("fait") : "fait le " + c.doneOn;
    return {};
}

int HmiTrailCards::columnsFor(float width) const noexcept {
    if (width >= 1020.f) return 3;
    if (width >= 660.f) return 2;
    return 1;
}

float HmiTrailCards::cardHeight() const {
    // Lot API 7 : chaque ligne de resume en plus agrandit la carte.
    return kCardH + static_cast<float>(style_.summaryLines - 1) * (ui::lineHeight(kCaptionFont) + 2.f);
}

std::string HmiTrailCards::noteText() const { return style_.note.empty() ? std::string(kDefaultNote) : style_.note; }

float HmiTrailCards::noteHeight(float width) const {
    if (style_.noteTitle.empty()) return kNoteH;
    const auto lines = wrapWith(style_.noteTitle + " " + noteText(), std::max(80.f, width - 2 * kPad - 32.f), 0,
                                [](const std::string& t) { return ui::measureWidth(t, kCaptionFont); });
    const float lh = ui::lineHeight(kCaptionFont) + 3.f;
    return 12.f + 14.f + static_cast<float>(lines.size()) * lh + 11.f + 8.f;
}

float HmiTrailCards::preferredHeight(float width) const {
    const int cols = columnsFor(width - 2 * kPad);
    const int rows = cards_.empty() ? 0 : (static_cast<int>(cards_.size()) + cols - 1) / cols;
    return kHeaderH + static_cast<float>(rows) * (cardHeight() + kGap) + noteHeight(width);
}

ui::SizeHint HmiTrailCards::sizeHint() const {
    // La largeur n'est pas demandee (un panneau qui defile garde la sienne) ;
    // la hauteur est celle des cartes a la largeur du moment.
    ui::SizeHint h;
    h.preferred = {0.f, preferredHeight(bounds().w > 1.f ? bounds().w : 900.f)};
    h.stretchX = 1.f;
    return h;
}

bool HmiTrailCards::buttonRect(const std::string& key, gfx::Rect& out) const {
    for (std::size_t i = 0; i < cards_.size() && i < buttonRects_.size(); ++i)
        if (cards_[i].key == key) {
            out = buttonRects_[i];
            return !out.empty();
        }
    return false;
}

bool HmiTrailCards::press(const std::string& key) {
    for (const auto& c : cards_)
        if (c.key == key) {
            startRequested->emit(c.key, buttonLabel(c) == "Reprendre");
            return true;
        }
    return false;
}

void HmiTrailCards::onLayout() {
    const auto b = bounds();
    cardRects_.assign(cards_.size(), gfx::Rect{});
    buttonRects_.assign(cards_.size(), gfx::Rect{});
    const int cols = columnsFor(b.w - 2 * kPad);
    const float w = (b.w - 2 * kPad - static_cast<float>(cols - 1) * kGap) / static_cast<float>(cols);
    const float h = cardHeight();
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        const gfx::Rect r{b.x + kPad + static_cast<float>(col) * (w + kGap), b.y + kHeaderH + static_cast<float>(row) * (h + kGap), w, h};
        cardRects_[i] = r;
        buttonRects_[i] = {r.right() - 14.f - 116.f, r.bottom() - 14.f - 30.f, 116.f, 30.f};
    }
}

void HmiTrailCards::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    const auto b = bounds();
    ctx.r.fillRect(b, c.windowBg);
    // Le titre, et combien sont faits (lot API 7 : et commences).
    const float lh = ctx.r.lineHeight(f.lead);
    drawBold(ctx, {b.x + kPad, b.y + (kHeaderH - lh) / 2.f}, style_.title, f.lead, c.text);
    const auto doneCount = std::count_if(cards_.begin(), cards_.end(), [](const TrailCard& t) { return t.done; });
    std::string tally = std::to_string(cards_.size()) + " parcours \xC2\xB7 " + std::to_string(doneCount) + " fait" + (doneCount > 1 ? "s" : "");
    if (style_.countStarted) {
        const auto startedCount = std::count_if(cards_.begin(), cards_.end(), started);
        if (startedCount > 0)
            tally += " \xC2\xB7 " + std::to_string(startedCount) + " commenc\xC3\xA9" + (startedCount > 1 ? "s" : "");
    }
    const float tw = ctx.r.measure(tally, f.caption).width;
    ctx.r.drawText({b.right() - kPad - tw, b.y + (kHeaderH - ctx.r.lineHeight(f.caption)) / 2.f}, tally, f.caption, c.textMuted);

    const float uh = ctx.r.lineHeight(f.ui), ch = ctx.r.lineHeight(f.caption);
    const auto measureCaption = [&](const std::string& t) { return ctx.r.measure(t, f.caption).width; };
    for (std::size_t i = 0; i < cards_.size() && i < cardRects_.size(); ++i) {
        const auto& t = cards_[i];
        const auto& r = cardRects_[i];
        const bool resumable = buttonLabel(t) == "Reprendre";
        ctx.r.fillRoundedRect(r, t.running ? withAlpha(c.accent, 26) : c.panelBg, 6.f);
        ctx.r.strokeRect(r, t.running ? c.accent : c.border, 1.f);
        float y = r.y + 12.f;
        // Lot API 7 : l'icone, dans un carre teinte, devant le nom.
        float tx = r.x + 14.f;
        if (t.icon != ui::Icon::None) {
            const gfx::Rect box{tx, y + (uh - kIconBox) / 2.f, kIconBox, kIconBox};
            ctx.r.fillRoundedRect(box, withAlpha(c.accent, 40), 5.f);
            ui::drawIcon(ctx.r, t.icon, {box.x + 5.f, box.y + 5.f, box.w - 10.f, box.h - 10.f}, c.accent);
            tx += kIconBox + 10.f;
        }
        // Le nom, et NOUVEAU.
        const std::string title = elide(ctx, f.uiBold, t.title, r.right() - 14.f - tx - (t.isNew ? 84.f : 0.f));
        if (t.icon != ui::Icon::None) drawBold(ctx, {tx, y}, title, f.uiBold, c.text);
        else ctx.r.drawText({tx, y}, title, f.uiBold, c.text);
        if (t.isNew) {
            const float x = tx + ctx.r.measure(title, f.uiBold).width + 10.f;
            const std::string badge = "NOUVEAU";
            const float bw = ctx.r.measure(badge, f.caption).width + 12.f;
            const gfx::Rect pill{x, y + (uh - ch - 4.f) / 2.f, bw, ch + 4.f};
            ctx.r.fillRoundedRect(pill, withAlpha(c.warning, 60), 3.f);
            ctx.r.drawText({pill.x + 6.f, pill.y + 2.f}, badge, f.caption, c.warning);
        }
        y += std::max(uh, t.icon != ui::Icon::None ? kIconBox - 4.f : 0.f) + 4.f;
        const std::string sub = t.kind + " \xC2\xB7 " + std::to_string(t.steps) + " \xC3\xA9tapes \xC2\xB7 " + std::to_string(t.minutes) + " min";
        ctx.r.drawText({r.x + 14.f, y}, sub, f.caption, c.textMuted);
        y += ch + 4.f;
        // Le resume : une ligne (lot 21), ou plusieurs (lot API 7).
        if (!t.summary.empty()) {
            if (style_.summaryLines <= 1) {
                ctx.r.drawText({r.x + 14.f, y}, elide(ctx, f.caption, t.summary, r.w - 28.f), f.caption, c.textMuted);
            } else {
                const auto lines = wrapWith(t.summary, r.w - 28.f, style_.summaryLines, measureCaption);
                for (std::size_t k = 0; k < lines.size(); ++k)
                    ctx.r.drawText({r.x + 14.f, y + static_cast<float>(k) * (ch + 2.f)}, lines[k], f.caption, c.text);
            }
        }
        y += ch + 8.f + static_cast<float>(style_.summaryLines - 1) * (ch + 2.f);
        // La barre de progression.
        const gfx::Rect track{r.x + 14.f, y, r.w - 28.f, 5.f};
        ctx.r.fillRoundedRect(track, c.border, 2.5f);
        float frac = 0.f;
        if (t.steps > 0) frac = resumable ? static_cast<float>(t.reached) / static_cast<float>(t.steps) : (t.done ? 1.f : 0.f);
        if (frac > 0.f)
            ctx.r.fillRoundedRect({track.x, track.y, track.w * std::clamp(frac, 0.f, 1.f), track.h}, t.done && !resumable ? c.ok : c.accent, 2.5f);
        // L'etat, et le bouton.
        const gfx::Rect& btn = buttonRects_[i];
        const std::string status = statusText(t);
        if (!status.empty()) {
            const float sy = btn.y + (btn.h - ch) / 2.f;
            if (t.done && !resumable && !t.running) {
                drawCheck(ctx, r.x + 14.f, sy + 1.f, c.ok);
                ctx.r.drawText({r.x + 30.f, sy}, status, f.caption, c.ok);
            } else {
                ctx.r.drawText({r.x + 14.f, sy}, status, f.caption, resumable ? c.accent : c.textMuted);
            }
        }
        const std::string label = buttonLabel(t);
        const bool hot = hover_ == static_cast<int>(i);
        if (resumable) ctx.r.fillRoundedRect(btn, hot ? c.accentHover : c.accent, 5.f);
        else {
            ctx.r.fillRoundedRect(btn, hot ? c.selectionBg : c.inputBg, 5.f);
            ctx.r.strokeRect(btn, c.border, 1.f);
        }
        const float lw = ctx.r.measure(label, f.ui).width;
        ctx.r.drawText({btn.x + (btn.w - lw) / 2.f, btn.y + (btn.h - uh) / 2.f}, label, f.ui, resumable ? c.textInverted : c.text);
    }
    // La note, sous les cartes.
    if (!cardRects_.empty()) {
        float bottom = 0.f;
        for (const auto& r : cardRects_) bottom = std::max(bottom, r.bottom());
        if (style_.noteTitle.empty()) {
            ctx.r.drawText({b.x + kPad, bottom + 12.f}, noteText(), f.caption, c.textMuted);
        } else {
            // Lot API 7 : un encadre, son titre en gras au debut de la premiere ligne.
            const auto lines = wrapWith(style_.noteTitle + " " + noteText(), std::max(80.f, b.w - 2 * kPad - 32.f), 0, measureCaption);
            const float lineH = ch + 3.f;
            const gfx::Rect box{b.x + kPad, bottom + 12.f, b.w - 2 * kPad, 14.f + static_cast<float>(lines.size()) * lineH + 11.f};
            ctx.r.fillRoundedRect(box, c.panelBg, 6.f);
            ctx.r.strokeRect(box, c.border, 1.f);
            float ly = box.y + 14.f;
            for (std::size_t k = 0; k < lines.size(); ++k) {
                ctx.r.drawText({box.x + 16.f, ly}, lines[k], f.caption, c.textMuted);
                if (k == 0 && lines[k].rfind(style_.noteTitle, 0) == 0) drawBold(ctx, {box.x + 16.f, ly}, style_.noteTitle, f.caption, c.text);
                ly += lineH;
            }
        }
    }
}

ui::EventResult HmiTrailCards::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < buttonRects_.size(); ++i)
            if (buttonRects_[i].contains(m->pos)) h = static_cast<int>(i);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        for (std::size_t i = 0; i < buttonRects_.size() && i < cards_.size(); ++i)
            if (buttonRects_[i].contains(d->pos)) {
                const std::string key = cards_[i].key;
                startRequested->emit(key, buttonLabel(cards_[i]) == "Reprendre");
                return ui::EventResult::Consumed;
            }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
