// =============================================================================
//  app/ai/AiPanel.cpp - voir AiPanel.hpp
// =============================================================================
#include "AiPanel.hpp"

#include "AiPreviewDialog.hpp"
#include "AiSettingsDialog.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../ui/FileKinds.hpp"
#include "../../ui/Icons.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace app::ai {

namespace {

const gfx::FontId kBody{14};
const gfx::FontId kMeta{13};
const gfx::FontId kHead{16};
constexpr float kPad = 10.f;
constexpr float kHeaderH = 38.f;
constexpr float kStatusH = 20.f;
constexpr float kModeH = 34.f;
constexpr float kListHeadH = 26.f;
constexpr float kRowH = 28.f;
constexpr int   kMaxRows = 4;
constexpr float kBottomH = 40.f;
constexpr float kPendingH = 30.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const float dw = r.measure(dots, f).width;
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - dw));
    return text.substr(0, n) + dots;
}

// 1.14.2 (captures, 2e passe) : elide, et le texte entier pour l'infobulle quand il est coupe.
std::string elideTip(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width, gfx::Rect at,
                     std::vector<std::pair<gfx::Rect, std::string>>& tips) {
    std::string shown = elide(r, text, f, width);
    if (shown != text && !text.empty()) tips.emplace_back(at, text);
    return shown;
}

std::string tipAt(const std::vector<std::pair<gfx::Rect, std::string>>& tips, gfx::Point p) {
    for (auto it = tips.rbegin(); it != tips.rend(); ++it)
        if (it->first.contains(p)) return it->second;
    return {};
}

// Un texte trace deux fois a un pixel : le gras du logiciel (il n'a pas de face grasse).
void bold(gfx::IRenderer& r, gfx::Point at, std::string_view t, gfx::FontId f, gfx::Color c) {
    r.drawText(at, t, f, c);
    r.drawText({at.x + 0.8f, at.y}, t, f, c);
}

struct FileLook {
    ui::Icon   icon;
    gfx::Color tint;
    std::string type;
};
FileLook lookOf(const ui::Theme& t, const std::string& name) {
    const auto ext = ui::files::extensionOf(name);
    if (ext == "xihm" || ext == "xapi" || ext == "xpgproj")
        return {ui::Icon::Project, ui::files::tintColor(t, ui::files::Tint::Orange),
                ext == "xihm" ? "Projet IHM (.xihm)" : ext == "xapi" ? "Projet API (.xapi)" : "Projet"};
    const auto fam = ui::files::familyOf(name, false);
    return {ui::files::iconOf(fam), ui::files::tintColor(t, ui::files::tintOf(fam)), ui::files::typeLabel(name, false)};
}

// Le cache des mises en page (un texte a une largeur) : la reponse s'ecrit mot a mot,
// chaque image ne refait que le message qui change.
std::unordered_map<std::string, std::vector<AiChatView::Line>>& layoutCache() {
    static std::unordered_map<std::string, std::vector<AiChatView::Line>> cache;
    if (cache.size() > 400) cache.clear();
    return cache;
}

// 1.15.0 : branche sur un vrai fournisseur, l'accueil ne parle plus de demonstration. 1.16.0 : l'IA
// integree dit ce qu'elle est (sur ce PC, des phrases simples).
const std::string& welcomeText(bool live = false, bool local = false) {
    static const std::string integree = "Je propose des modifications de **la vue ouverte**, avec les vrais objets du logiciel. "
                                        "IA int\xC3\xA9gr\xC3\xA9" "e : je comprends des phrases simples, sans connexion - rien ne quitte ce PC.";
    if (local) return integree;
    static const std::string demo = "Je propose des modifications de **la vue ouverte**, avec les vrais objets du logiciel. "
                                    "D\xC3\xA9monstration : je reconnais les demandes ci-dessous.";
    static const std::string real = "Je propose des modifications de **la vue ouverte**, avec les vrais objets du logiciel : "
                                    "d\xC3\xA9" "cris ce que tu veux, je propose, tu v\xC3\xA9rifies, tu appliques (Ctrl+Z reprend tout).";
    return live ? real : demo;
}

// 1.14.2 : un texte simple a la ligne, au mot (un mot plus large que la place : coupe ; une
// ponctuation seule - le ":" francais - reste sur sa ligne).
std::vector<std::string> wrapPlainLine(const std::string& text, gfx::FontId f, float width);
// 1.16.1 (captures) : un saut de ligne du texte (la note << laissee de cote : >> et sa liste) commence une ligne - il
// s'affichait comme un caractere inconnu.
std::vector<std::string> wrapPlain(const std::string& text, gfx::FontId f, float width) {
    if (text.find('\n') == std::string::npos) return wrapPlainLine(text, f, width);
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        std::string part = text.substr(from, nl - from);
        if (!part.empty() && part.back() == '\r') part.pop_back();
        auto lines = wrapPlainLine(part, f, width);
        if (lines.empty()) lines.emplace_back();
        for (auto& l : lines) out.push_back(std::move(l));
        from = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

std::vector<std::string> wrapPlainLine(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line;
    width = std::max(width, 30.f);
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        std::size_t e = i;
        while (e < text.size() && text[e] != ' ') ++e;
        if (e == i) break;
        const std::string word = text.substr(i, e - i);
        i = e;
        const bool glue = word == ":" || word == ";" || word == "!" || word == "?" || word == "\xC2\xBB";
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && (glue || ui::measureWidth(trial, f) <= width)) {
            line = trial;
            continue;
        }
        if (!line.empty()) out.push_back(line);
        line.clear();
        if (ui::measureWidth(word, f) <= width) {
            line = word;
            continue;
        }
        for (std::size_t k = 0; k < word.size();) {   // le mot seul ne tient pas : coupe
            std::size_t n = k + 1;
            while (n < word.size() && (static_cast<unsigned char>(word[n]) & 0xC0) == 0x80) ++n;
            const std::string next = line + word.substr(k, n - k);
            if (!line.empty() && ui::measureWidth(next, f) > width) {
                out.push_back(line);
                line.clear();
                continue;
            }
            line = next;
            k = n;
        }
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

// Les boutons d'une carte, de gauche a droite, a la ligne quand ils ne tiennent pas (le bord
// droit de la carte n'est jamais depasse : "Refuser" a 1280 x 800).
constexpr float kBtnH = 28.f, kBtnGap = 6.f, kNoteH = 18.f;
constexpr std::size_t kNoteLines = 4;   // les lignes d'une note, au plus (le reste : l'apercu)
std::vector<gfx::Rect> flowButtons(const std::vector<std::string>& labels, float width) {
    std::vector<gfx::Rect> out;
    float x = 0.f, y = 0.f;
    for (const auto& l : labels) {
        const float w = std::min(width, ui::measureWidth(l, gfx::FontId{16}) + 26.f);
        if (x > 0.f && x + w > width) {
            x = 0.f;
            y += kBtnH + kBtnGap;
        }
        out.push_back({x, y, w, kBtnH});
        x += w + kBtnGap;
    }
    return out;
}

} // namespace

// =============================================================================
//  La saisie
// =============================================================================
namespace {

constexpr float kPromptPadX = 8.f, kPromptPadY = 6.f;

bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
std::size_t nextChar(std::string_view s, std::size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && continuation(s[i])) ++i;
    return i;
}
std::size_t prevChar(std::string_view s, std::size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && continuation(s[i])) --i;
    return i;
}
bool blank(char c) { return c == ' ' || c == '\t'; }
bool wordChar(char c) { return !blank(c) && c != '\n'; }

} // namespace

AiPromptEdit::AiPromptEdit(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
}

std::vector<AiPromptEdit::Row> AiPromptEdit::wrap(std::string_view text, float width) {
    // Au mot, ligne par ligne du texte ; un mot plus large que le champ se coupe ou il ne tient plus.
    std::vector<Row> out;
    width = std::max(width, 24.f);
    std::size_t para = 0;
    while (true) {
        std::size_t nl = text.find('\n', para);
        const std::size_t stop = nl == std::string_view::npos ? text.size() : nl;
        std::size_t begin = para;
        while (true) {
            if (begin >= stop) {
                out.push_back({begin, stop, nl == std::string_view::npos ? stop : stop + 1});
                break;
            }
            // Ce qui tient : des mots entiers (et leurs blancs) tant que le texte sans ses blancs de fin tient.
            std::size_t end = begin, fit = begin;
            while (end < stop) {
                std::size_t w = end;
                while (w < stop && blank(text[w])) ++w;
                std::size_t e = w;
                while (e < stop && !blank(text[e])) ++e;
                if (fit > begin && ui::measureWidth(text.substr(begin, e - begin), kBody) > width) break;
                if (fit == begin && ui::measureWidth(text.substr(begin, e - begin), kBody) > width) {
                    // Le premier mot ne tient pas seul : coupe a la largeur (au moins un caractere).
                    std::size_t cut = nextChar(text, begin);
                    while (cut < e && ui::measureWidth(text.substr(begin, nextChar(text, cut) - begin), kBody) <= width) cut = nextChar(text, cut);
                    fit = cut;
                    end = cut;
                    break;
                }
                // Les blancs qui suivent le mot restent sur la ligne.
                while (e < stop && blank(text[e])) ++e;
                fit = e;
                end = e;
            }
            if (fit >= stop) {
                out.push_back({begin, stop, nl == std::string_view::npos ? stop : stop + 1});
                break;
            }
            out.push_back({begin, fit, fit});
            begin = fit;
        }
        if (nl == std::string_view::npos) break;
        para = nl + 1;
    }
    if (out.empty()) out.push_back({0, 0, 0});
    return out;
}

gfx::Rect AiPromptEdit::textArea() const {
    const auto b = bounds();
    return {b.x + kPromptPadX, b.y + kPromptPadY, std::max(0.f, b.w - 2 * kPromptPadX), std::max(0.f, b.h - 2 * kPromptPadY)};
}

float AiPromptEdit::wrapWidth() const { return std::max(24.f, bounds().w - 2 * kPromptPadX - 8.f); }   // 8 : la barre

const std::vector<AiPromptEdit::Row>& AiPromptEdit::rows() const {
    const float w = wrapWidth();
    if (w != rowsW_ || rowsRev_ != rev_) {
        rows_ = wrap(text_, w);
        rowsW_ = w;
        rowsRev_ = rev_;
    }
    return rows_;
}

std::size_t AiPromptEdit::rowsFor(float width) const {
    const float w = std::max(24.f, width - 2 * kPromptPadX - 8.f);
    if (text_.empty()) return std::clamp<std::size_t>(wrap(placeholder_, w).size(), 1, 2);
    return wrap(text_, w).size();
}

float AiPromptEdit::heightFor(float width) const {
    const auto n = static_cast<float>(std::clamp<std::size_t>(rowsFor(width), 1, kMaxRows));
    return n * (ui::lineHeight(kBody) + 3.f) + 2 * kPromptPadY + 6.f;
}

std::size_t AiPromptEdit::rowOf(std::size_t offset) const {
    const auto& r = rows();
    std::size_t row = 0;
    for (std::size_t i = 0; i < r.size(); ++i)
        if (r[i].begin <= offset) row = i;
    return row;
}

std::size_t AiPromptEdit::rowLimit(std::size_t row) const {
    const auto& r = rows();
    if (row >= r.size()) return text_.size();
    const Row& x = r[row];
    // Une ligne coupee (pas de retour a la ligne) : son dernier curseur est AVANT son dernier
    // caractere (le blanc de la coupure) ; sinon il tomberait au debut de la suivante.
    if (x.next == x.end && x.end > x.begin && row + 1 < r.size()) return prevChar(text_, x.end);
    return x.end;
}

std::size_t AiPromptEdit::offsetInRow(std::size_t row, float x) const {
    const auto& r = rows();
    if (row >= r.size()) return text_.size();
    const std::size_t begin = r[row].begin, limit = rowLimit(row);
    std::size_t best = begin;
    float left = 0.f;
    for (std::size_t i = begin; i < limit;) {
        const std::size_t n = nextChar(text_, i);
        const float w = ui::measureWidth(std::string_view(text_).substr(i, n - i), kBody);
        if (x < left + w * 0.5f) return i;
        left += w;
        i = n;
        best = i;
    }
    return std::max(best, begin);
}

std::size_t AiPromptEdit::offsetAt(gfx::Point p) const {
    const auto area = textArea();
    const float lh = ui::lineHeight(kBody) + 3.f;
    const float y = p.y - area.y + scroll_;
    const auto& r = rows();
    const std::size_t row = y < 0.f ? 0 : std::min(r.size() - 1, static_cast<std::size_t>(y / lh));
    return offsetInRow(row, p.x - area.x);
}

std::optional<ui::cursor::Shape> AiPromptEdit::cursorAt(gfx::Point local) const {
    if (!bounds().contains(local)) return std::nullopt;
    return ui::cursor::Shape::Text;
}

void AiPromptEdit::changed() {
    ++rev_;
    goalX_ = -1.f;
    blinkFrom_ = lastTime_;
    ensureCaretVisible();
    if (parent()) parent()->invalidateLayout();   // la hauteur suit le nombre de lignes
    invalidate();
}

void AiPromptEdit::setText(std::string t) {
    std::erase(t, '\r');
    if (t == text_) return;
    text_ = std::move(t);
    caret_ = anchor_ = text_.size();
    scroll_ = 0.f;
    changed();
}

void AiPromptEdit::replaceSelection(std::string_view with) {
    const std::size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
    text_.replace(lo, hi - lo, with);
    caret_ = anchor_ = lo + with.size();
    changed();
}

void AiPromptEdit::insertText(std::string_view utf8) {
    if (readOnly_) return;
    std::string clean(utf8);
    std::erase(clean, '\r');
    replaceSelection(clean);
}

void AiPromptEdit::moveTo(std::size_t offset, bool extend) {
    caret_ = std::min(offset, text_.size());
    if (!extend) anchor_ = caret_;
    blinkFrom_ = lastTime_;
    ensureCaretVisible();
    invalidate();
}

void AiPromptEdit::ensureCaretVisible() {
    const auto area = textArea();
    const float lh = ui::lineHeight(kBody) + 3.f;
    const auto& r = rows();
    const float content = static_cast<float>(r.size()) * lh;
    const float top = static_cast<float>(rowOf(caret_)) * lh;
    if (top < scroll_) scroll_ = top;
    if (top + lh > scroll_ + area.h) scroll_ = top + lh - area.h;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content - area.h));
}

void AiPromptEdit::onFocusChanged(bool focused) {
    if (focused) undoText_ = text_;
    selecting_ = false;
    invalidate();
}

void AiPromptEdit::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    lastTime_ = ctx.time;
    if (selecting_) ui::cursor::hold(ui::cursor::Shape::Text);
    ctx.r.fillRoundedRect(b, enabled() ? c.inputBg : c.panelBg, 4.f);
    ctx.r.strokeRect(b, focused() ? c.accent : c.border, 1.f);
    const auto area = textArea();
    const float lh = ctx.r.lineHeight(kBody) + 3.f;
    const float textDy = 1.5f;
    const auto& r = rows();
    const float content = static_cast<float>(r.size()) * lh;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content - area.h));
    ctx.r.pushClip({area.x - 2.f, area.y, area.w + 4.f, area.h});
    if (text_.empty()) {
        // L'indication : a la ligne elle aussi, jamais recouverte (le curseur est a gauche).
        const auto ph = wrap(placeholder_, wrapWidth());
        for (std::size_t i = 0; i < ph.size() && static_cast<float>(i) * lh < area.h; ++i) {
            std::string line = placeholder_.substr(ph[i].begin, ph[i].end - ph[i].begin);
            if (static_cast<float>(i + 2) * lh > area.h + 1.f && i + 1 < ph.size()) line = elide(ctx.r, line + " \xE2\x80\xA6", kBody, area.w);
            ctx.r.drawText({area.x + 2.f, area.y + textDy + static_cast<float>(i) * lh}, line, kBody, c.textMuted);
        }
    } else {
        const std::size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
        const std::string_view all(text_);
        for (std::size_t i = 0; i < r.size(); ++i) {
            const float y = area.y + static_cast<float>(i) * lh - scroll_;
            if (y + lh < area.y || y > area.bottom()) continue;
            const std::string_view line = all.substr(r[i].begin, r[i].end - r[i].begin);
            if (lo != hi && focused() && hi > r[i].begin && lo <= r[i].end) {
                const std::size_t a = std::max(lo, r[i].begin), z = std::min(hi, r[i].end);
                const float xa = ctx.r.measure(all.substr(r[i].begin, a - r[i].begin), kBody).width;
                float xb = ctx.r.measure(all.substr(r[i].begin, z - r[i].begin), kBody).width;
                if (hi > r[i].end && r[i].next > r[i].end) xb += 5.f;   // le retour a la ligne choisi
                if (xb > xa) ctx.r.fillRect({area.x + xa, y, xb - xa, lh}, c.selectionBg);
            }
            ctx.r.drawText({area.x, y + textDy}, line, kBody, readOnly_ || !enabled() ? c.textDisabled : c.text);
        }
    }
    if (focused() && !readOnly_ && std::fmod(std::max(0.0, ctx.time - blinkFrom_), 1.0) < 0.55) {
        const std::size_t row = text_.empty() ? 0 : rowOf(caret_);
        const float x = text_.empty() ? 0.f : ctx.r.measure(std::string_view(text_).substr(r[row].begin, caret_ - r[row].begin), kBody).width;
        const float y = area.y + static_cast<float>(row) * lh - scroll_;
        ctx.r.fillRect({area.x + x, y + 1.f, 1.f, lh - 2.f}, c.text);
    }
    ctx.r.popClip();
    bar_.paint(ctx, {b.x, area.y, b.w - 1.f, area.h}, content, area.h, scroll_);
}

ui::EventResult AiPromptEdit::onEvent(const ui::InputEvent& ev) {
    const auto b = bounds();
    const auto area = textArea();
    const float lh = ui::lineHeight(kBody) + 3.f;
    const float content = static_cast<float>(rows().size()) * lh;
    if (bar_.handle(*this, ev, {b.x, area.y, b.w - 1.f, area.h}, content, area.h, scroll_)) {
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && b.contains(w->pos) && !w->mods.ctrl) {
        if (content <= area.h) return ui::EventResult::Ignored;   // rien a defiler : la discussion la prend
        scroll_ = std::clamp(scroll_ - w->dy * lh, 0.f, std::max(0.f, content - area.h));
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!b.contains(d->pos) || d->button != ui::MouseButton::Left) return b.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        grabFocus();
        const std::size_t at = offsetAt(d->pos);
        if (d->clickCount >= 2) {   // le mot sous la souris
            std::size_t s = at, e = at;
            while (s > 0 && wordChar(text_[s - 1])) --s;
            while (e < text_.size() && wordChar(text_[e])) ++e;
            anchor_ = s;
            caret_ = e;
            invalidate();
            return ui::EventResult::Consumed;
        }
        moveTo(at, d->mods.shift);
        goalX_ = -1.f;
        selecting_ = true;
        captureMouse();
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (!selecting_) return ui::EventResult::Ignored;
        moveTo(offsetAt(m->pos), true);
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::MouseUp>(&ev)) {
        if (!selecting_) return ui::EventResult::Ignored;
        selecting_ = false;
        releaseMouse();
        return ui::EventResult::Consumed;
    }
    if (!focused()) return ui::EventResult::Ignored;
    if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
        insertText(t->utf8);
        return ui::EventResult::Consumed;
    }
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k) return ui::EventResult::Ignored;
    const bool shift = k->mods.shift, ctrl = k->mods.ctrl;
    const auto wordLeft = [this](std::size_t i) {
        while (i > 0 && !wordChar(text_[i - 1])) --i;
        while (i > 0 && wordChar(text_[i - 1])) --i;
        return i;
    };
    const auto wordRight = [this](std::size_t i) {
        while (i < text_.size() && !wordChar(text_[i])) ++i;
        while (i < text_.size() && wordChar(text_[i])) ++i;
        return i;
    };
    const auto vertical = [&](int dir) {
        const auto& r = rows();
        const std::size_t row = rowOf(caret_);
        if (goalX_ < 0.f) goalX_ = ui::measureWidth(std::string_view(text_).substr(r[row].begin, caret_ - r[row].begin), kBody);
        const float goal = goalX_;
        if (dir < 0 && row == 0) moveTo(0, shift);
        else if (dir > 0 && row + 1 >= r.size()) moveTo(text_.size(), shift);
        else moveTo(offsetInRow(dir < 0 ? row - 1 : row + 1, goal), shift);
        goalX_ = goal;
    };
    switch (k->key) {
        case ui::Key::Return:
            if (k->mods.alt) return ui::EventResult::Ignored;
            if (shift) insertText("\n");   // Maj+Entree : une ligne de plus
            else submitted->emit();        // Entree (ou Ctrl+Entree) : envoyer
            return ui::EventResult::Consumed;
        case ui::Key::Backspace:
            if (readOnly_) return ui::EventResult::Consumed;
            if (caret_ == anchor_) anchor_ = ctrl ? wordLeft(caret_) : prevChar(text_, caret_);
            replaceSelection({});
            return ui::EventResult::Consumed;
        case ui::Key::Delete:
            if (readOnly_) return ui::EventResult::Consumed;
            if (caret_ == anchor_) anchor_ = ctrl ? wordRight(caret_) : nextChar(text_, caret_);
            replaceSelection({});
            return ui::EventResult::Consumed;
        case ui::Key::Left:
            if (!shift && caret_ != anchor_) moveTo(std::min(caret_, anchor_), false);
            else moveTo(ctrl ? wordLeft(caret_) : prevChar(text_, caret_), shift);
            goalX_ = -1.f;
            return ui::EventResult::Consumed;
        case ui::Key::Right:
            if (!shift && caret_ != anchor_) moveTo(std::max(caret_, anchor_), false);
            else moveTo(ctrl ? wordRight(caret_) : nextChar(text_, caret_), shift);
            goalX_ = -1.f;
            return ui::EventResult::Consumed;
        case ui::Key::Up: vertical(-1); return ui::EventResult::Consumed;
        case ui::Key::Down: vertical(1); return ui::EventResult::Consumed;
        case ui::Key::Home:
            moveTo(ctrl ? 0 : rows()[rowOf(caret_)].begin, shift);
            goalX_ = -1.f;
            return ui::EventResult::Consumed;
        case ui::Key::End:
            moveTo(ctrl ? text_.size() : rowLimit(rowOf(caret_)), shift);
            goalX_ = -1.f;
            return ui::EventResult::Consumed;
        case ui::Key::A:
            if (!ctrl) break;
            anchor_ = 0;
            caret_ = text_.size();
            invalidate();
            return ui::EventResult::Consumed;
        case ui::Key::C:
        case ui::Key::X:
            if (!ctrl) break;
            if (caret_ != anchor_) {
                const std::size_t lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
                ui::setClipboardText(std::string_view(text_).substr(lo, hi - lo));
                if (k->key == ui::Key::X && !readOnly_) replaceSelection({});
            }
            return ui::EventResult::Consumed;
        case ui::Key::V:
            if (!ctrl) break;
            insertText(ui::clipboardText());
            return ui::EventResult::Consumed;
        case ui::Key::Z:
            // Ctrl+Z : le texte d'avant (a la prise du clavier) ; rien a defaire : l'ecran le prend (le projet).
            if (!ctrl || shift || readOnly_ || text_ == undoText_) break;
            setText(undoText_);
            return ui::EventResult::Consumed;
        case ui::Key::Escape:
            releaseFocus();
            return ui::EventResult::Consumed;
        default: break;
    }
    return ui::EventResult::Ignored;
}

float defaultPanelWidth(float windowWidth) noexcept { return std::clamp(0.22f * windowWidth, 280.f, 460.f); }

// =============================================================================
//  Le panneau
// =============================================================================
AiPanel::AiPanel(std::string id, Assistant::Host core, UiHost uiHost)
    : ui::Widget(std::move(id)), assistant_(std::make_unique<Assistant>(std::move(core))), ui_(std::move(uiHost)) {
    // 1.17.0 : << Essayer une phrase >> (l'onglet Apprentissage) passe par l'Assistant de ce panneau (le projet ouvert)
    learning::setPhraseTester([a = assistant_.get()](const std::string& p) { return a->tryPhrase(p); });
    setFocusPolicy(true);
    setZoomSupport(ui::ZoomSupport::Scaled);
    const std::string base = this->id();
    const auto iconButton = [&](const char* label, ui::Icon icon, const char* idSuffix, const char* tip) {
        auto b = std::make_unique<ui::Button>(label, base + idSuffix);
        b->setIcon(icon);
        b->setCompact(true);
        b->setStyle(ui::Button::Style::Flat);
        b->setTooltip(tip);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    };
    newBtn_ = iconButton("Nouvelle conversation", ui::Icon::Plus, ".nouvelle", "Nouvelle conversation");
    settingsBtn_ = iconButton("Param\xC3\xA8tres de l'Assistant IA", ui::Icon::Settings, ".parametres",
                              "Param\xC3\xA8tres de l'Assistant IA : fournisseur, mod\xC3\xA8le, cl\xC3\xA9, confidentialit\xC3\xA9, historique");
    closeBtn_ = iconButton("Replier l'Assistant", ui::Icon::Close, ".replier", "Replier l'Assistant (Ctrl+Maj+I le rouvre)");
    modePreview_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Pr\xC3\xA9visualisation", base + ".mode.apercu")));
    modePreview_->setTooltip("Chaque proposition passe par l'aper\xC3\xA7u : rien ne change tant que tu n'as pas dit Appliquer.");
    modeDirect_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Direct", base + ".mode.direct")));
    modeDirect_->setTooltip("Chaque proposition s'applique d\xC3\xA8s la r\xC3\xA9ponse (une suppression multiple demande d'abord) ; Ctrl+Z la reprend.");
    auto rename = std::make_unique<ui::InputText>(base + ".renommer");
    rename->setVisibility(ui::Visibility::Hidden);
    rename_ = &static_cast<ui::InputText&>(addChild(std::move(rename)));
    chat_ = &static_cast<AiChatView&>(addChild(std::make_unique<AiChatView>(base + ".discussion", *this)));
    prompt_ = &static_cast<AiPromptEdit&>(addChild(std::make_unique<AiPromptEdit>(base + ".saisie")));
    prompt_->setTooltip("Ta demande. Entr\xC3\xA9" "e : envoyer ; Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne ; un fichier l\xC3\xA2" "ch\xC3\xA9 ici se joint.");
    attachBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Joindre\xE2\x80\xA6", base + ".joindre")));
    attachBtn_->setIcon(ui::Icon::Link);
    attachBtn_->setCompact(true);   // l'icone seule (le libelle : l'infobulle, les seances de script)
    attachBtn_->setStyle(ui::Button::Style::Flat);
    attachBtn_->setTooltip("Joindre un fichier : image (PNG, JPG), PDF, texte, CSV, Excel, XML, JSON, projet (.xihm, .xapi). Il est copi\xC3\xA9 dans le dossier de l'Assistant.");
    sendBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer", base + ".envoyer")));
    sendBtn_->setStyle(ui::Button::Style::Primary);
    auto menu = std::make_unique<ui::PopupMenu>(base + ".menu");
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::move(menu)));

    links_ += newBtn_->clicked->connect([this] { newConversation(); });
    links_ += settingsBtn_->clicked->connect([this] { openSettings(); });
    links_ += closeBtn_->clicked->connect([this] { if (ui_.close) ui_.close(); });
    links_ += modePreview_->toggled->connect([this](bool) { if (!syncing_) setDirect(false); });
    links_ += modeDirect_->toggled->connect([this](bool) { if (!syncing_) setDirect(true); });
    links_ += rename_->editingDone->connect([this](const std::string& t) { if (renaming()) (void)commitRename(t); });
    links_ += prompt_->submitted->connect([this] { (void)send(); });
    links_ += attachBtn_->clicked->connect([this] { askAttach(); });
    links_ += sendBtn_->clicked->connect([this] {
        if (assistant_->generating()) stop();
        else (void)send();
    });
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    refreshButtons();
}

AiPanel::~AiPanel() { learning::setPhraseTester(nullptr); }

void AiPanel::say(const std::string& text) {
    if (text.empty()) return;
    lastStatus_ = text;
    if (ui_.status) ui_.status(text);
}

void AiPanel::refreshButtons() {
    const auto& a = *assistant_;
    syncing_ = true;
    modePreview_->setChecked(!a.directMode());
    modeDirect_->setChecked(a.directMode());
    syncing_ = false;
    const bool gen = a.generating();
    sendBtn_->setText(gen ? "Arr\xC3\xAAter" : "Envoyer");
    sendBtn_->setIcon(gen ? ui::Icon::Stop : ui::Icon::None);
    sendBtn_->setStyle(gen ? ui::Button::Style::Default : ui::Button::Style::Primary);
    sendBtn_->setTooltip(gen ? "Arr\xC3\xAAter la g\xC3\xA9n\xC3\xA9ration : la r\xC3\xA9ponse est gard\xC3\xA9" "e telle quelle, sans proposition."
                             : "Envoyer la demande (Entr\xC3\xA9" "e)");
    sendBtn_->setEnabled(gen || (a.settings().enabled && !a.processing()));
    attachBtn_->setEnabled(!gen);
    prompt_->setReadOnly(!a.settings().enabled);
    prompt_->setPlaceholder(a.settings().enabled ? "D\xC3\xA9" "cris ta demande\xE2\x80\xA6 (ex. : Ajoute une cuve avec un indicateur de niveau)"
                                                 : "L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres)");
}

void AiPanel::tick(double now) {
    (void)assistant_->tick(now);
    sync();
    // 1.16.1 : une reponse vient de finir - l'ecran avertit (s'il le faut : l'utilisateur ne regarde pas).
    if (assistant_->generating()) {
        watchingReply_ = assistant_->generatingConversationId();
    } else if (!watchingReply_.empty()) {
        const std::string id = std::exchange(watchingReply_, std::string{});
        if (assistant_->settings().notifyReplies && ui_.replied)
            for (const auto& c : assistant_->conversations())
                if (c.id == id && !c.messages.empty() && c.messages.back().role == "assistant") {
                    const Message& m = c.messages.back();
                    std::string excerpt = m.text.empty() ? m.note : m.text;
                    for (char& ch : excerpt)
                        if (ch == '\n' || ch == '\r' || ch == '*' || ch == '`') ch = ' ';
                    if (excerpt.size() > 110) {
                        std::size_t cut = 110;
                        while (cut > 0 && (static_cast<unsigned char>(excerpt[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
                        excerpt = excerpt.substr(0, cut) + "\xE2\x80\xA6";
                    }
                    ui_.replied(id, c.title, excerpt, m.state == "erreur");
                }
    }
    refreshBand(now);   // 1.16.0 : la limite en direct (le compte a rebours avance chaque seconde)
    // Ctrl+Z, Ctrl+Y, un autre geste sur la vue : la carte d'une proposition appliquee dit l'etat
    // REEL (relu quatre fois par seconde : une empreinte de la vue par proposition appliquee).
    if (now - appliedCheck_ >= 0.25 || now < appliedCheck_) {
        appliedCheck_ = now;
        std::string key;
        if (const auto* c = assistant_->current()) {
            key = c->id + "|";
            for (std::size_t i = 0; i < c->messages.size(); ++i)
                if (c->messages[i].proposal && c->messages[i].proposal->state == "appliquee")
                    key += static_cast<char>('0' + static_cast<int>(assistant_->appliedState(static_cast<int>(i))));
        }
        // 1.15.0 : "ce qui part" suit la vue et la selection (sans changement de la seance).
        if (std::string out = assistant_->outgoingSummary(); out != outgoingText_) {
            outgoingText_ = std::move(out);
            invalidateLayout();
            invalidate();
        }
        if (key != appliedKey_) {
            const bool first = appliedKey_.empty();
            appliedKey_ = std::move(key);
            if (!first) {
                chat_->rebuild();
                invalidateLayout();
                invalidate();
            }
        }
    }
}

void AiPanel::ensureLoaded() {
    if (loaded_) return;
    loaded_ = true;
    std::vector<std::string> problems;
    assistant_->load(&problems);
    for (const auto& p : problems) say("Assistant IA : conversation illisible, laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 (" + p + ")");
    seen_ = 0;
    sync();
    chat_->showLastExchange();   // la premiere ouverture : le debut de la conversation, pas sa fin
}

void AiPanel::sync() {
    if (assistant_->revision() != seen_) {
        seen_ = assistant_->revision();
        refreshButtons();
        chat_->rebuild();
        invalidateLayout();
        invalidate();
    }
    if (const auto n = assistant_->takeNotice(); !n.empty()) say(n);
    // Le mode Direct et une suppression multiple : on demande d'abord.
    if (const int m = assistant_->takeDirectConfirm(); m >= 0) (void)applyProposal(m);
}

// ---- les conversations ------------------------------------------------------------------
void AiPanel::newConversation() {
    cancelRename();
    assistant_->newConversation();
    seen_ = 0;
    sync();
    prompt_->setText({});
    prompt_->takeFocus();
    say("Nouvelle conversation : d\xC3\xA9" "cris ta demande, ou choisis une suggestion.");
}

bool AiPanel::selectConversation(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return false;
    cancelRename();
    const bool ok = assistant_->select(list[static_cast<std::size_t>(index)].id);
    chat_->rebuild();
    // Une reponse qui s'ecrit dans cette conversation : suivre le bas ; sinon le dernier echange.
    if (assistant_->generatingMessage() >= 0) chat_->scrollToBottom();
    else chat_->showLastExchange();
    invalidateLayout();
    return ok;
}

void AiPanel::startRename(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    renameIndex_ = index;
    rename_->setText(list[static_cast<std::size_t>(index)].title);
    rename_->setVisibility(ui::Visibility::Visible);
    invalidateLayout();
    layout();
    (void)rename_->focusAndSelectAll();
}

bool AiPanel::commitRename(const std::string& title) {
    if (!renaming()) return false;
    const auto& list = assistant_->conversations();
    const int i = renameIndex_;
    renameIndex_ = -1;
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
    if (i < 0 || i >= static_cast<int>(list.size())) return false;
    const std::string id = list[static_cast<std::size_t>(i)].id;
    const bool ok = assistant_->rename(id, title);
    if (ok) say("Conversation renomm\xC3\xA9" "e : " + title);
    takeFocus();
    return ok;
}

void AiPanel::cancelRename() {
    if (!renaming()) return;
    renameIndex_ = -1;
    rename_->setVisibility(ui::Visibility::Hidden);
    invalidateLayout();
}

void AiPanel::askDelete(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Conversation& c = list[static_cast<std::size_t>(index)];
    const std::string id = c.id, title = c.title;
    std::size_t files = 0;
    for (const auto& m : c.messages) files += m.attachments.size();
    const auto run = [this, id, title] {
        if (assistant_->remove(id)) say("Conversation supprim\xC3\xA9" "e : " + title);
        chat_->rebuild();
        invalidateLayout();
    };
    if (!ui_.showDialog) {   // sans hote (un essai) : on supprime
        run();
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.ai.supprimer";
    spec.title = "Supprimer la conversation";
    spec.text = "Supprimer \xC2\xAB " + title + " \xC2\xBB et ses " + std::to_string(c.messages.size()) + " messages"
                + (files ? " (et la copie de ses " + std::to_string(files) + " pi\xC3\xA8" "ces jointes)" : std::string{})
                + " ?\nLe projet n'est pas touch\xC3\xA9 : seule la conversation part du dossier de l'Assistant.";
    spec.confirm = "Supprimer";
    spec.danger = true;
    spec.width = 520.f;
    ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
        if (r.accepted()) run();
    });
}

void AiPanel::setConversationsOpen(bool open) {
    listOpen_ = open;
    invalidateLayout();
}

void AiPanel::openRowMenu(int index, gfx::Point at) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    menuRow_ = index;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = list[static_cast<std::size_t>(index)].title;
    items.push_back(head);
    ui::PopupMenu::Item open;
    open.label = "Ouvrir";
    open.icon = ui::Icon::Open;
    open.id = kMenuOpen;
    items.push_back(open);
    ui::PopupMenu::Item ren;
    ren.label = "Renommer\xE2\x80\xA6";
    ren.icon = ui::Icon::Rename;
    ren.shortcut = "F2";
    ren.id = kMenuRename;
    items.push_back(ren);
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    ui::PopupMenu::Item del;
    del.label = "Supprimer\xE2\x80\xA6";
    del.icon = ui::Icon::Delete;
    del.shortcut = "Suppr";
    del.id = kMenuDelete;
    items.push_back(del);
    items.push_back(sep);
    ui::PopupMenu::Item fresh;
    fresh.label = "Nouvelle conversation";
    fresh.icon = ui::Icon::Plus;
    fresh.id = kMenuNew;
    items.push_back(fresh);
    menu_->setItems(std::move(items));
    menu_->openAtSurface(toSurface(at));
}

void AiPanel::menuChosen(int id) {
    const int row = menuRow_;
    menuRow_ = -1;
    switch (id) {
        case kMenuOpen: (void)selectConversation(row); break;
        case kMenuRename: startRename(row); break;
        case kMenuDelete: askDelete(row); break;
        case kMenuNew: newConversation(); break;
        default: break;
    }
}

// ---- envoyer --------------------------------------------------------------------------------
bool AiPanel::send() {
    const std::string text = prompt_->text();
    if (!sendText(text)) return false;
    prompt_->setText({});
    return true;
}

bool AiPanel::sendText(const std::string& text) {
    cancelRename();
    std::string why;
    // 1.16.0 : le budget atteint - une fenetre du logiciel demande confirmation (rien n'est bloque sans le dire).
    if (std::string question; assistant_->budgetBlocks(&question)) {
        if (!ui_.showDialog) {
            say(question);
            return false;
        }
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.budget";
        spec.title = "Budget de l'Assistant IA";
        spec.text = question + "\nLe budget se r\xC3\xA8gle dans les Param\xC3\xA8tres de l'Assistant IA (0 : aucun).";
        spec.confirm = "Envoyer quand m\xC3\xAAme";
        spec.width = 520.f;
        ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, text](const menu::DialogResult& r) {
            if (!r.accepted()) {
                say("Envoi annul\xC3\xA9 : le budget est atteint.");
                return;
            }
            assistant_->allowOverBudget();
            if (sendText(text) && prompt_->text() == text) prompt_->setText({});
        });
        return false;
    }
    if (!assistant_->send(text, &why)) {
        say(why);
        return false;
    }
    watchingReply_ = assistant_->generatingConversationId();   // 1.16.1 : une reponse d'un seul pas se voit aussi
    seen_ = 0;
    sync();
    chat_->scrollToBottom();
    return true;
}

void AiPanel::stop() {
    assistant_->stop();
    sync();
}

bool AiPanel::clickSuggestion(std::size_t index) {
    const auto& s = assistant_->suggestions();
    if (index >= s.size()) return false;
    const std::string pick = s[index];   // 1.16.0 : une copie (l'envoi refait la liste de l'IA integree)
    return sendText(pick);
}

void AiPanel::askAttach() {
    if (assistant_->generating()) {
        say("Attends la fin de la r\xC3\xA9ponse pour joindre un fichier.");
        return;
    }
    ui::FilePick pick;
    pick.title = "Joindre un fichier \xC3\xA0 la demande";
    pick.filters = {{"Fichiers pris en charge", attachmentFilter()}, {"Images", "png;jpg;jpeg"}, {"Documents", "pdf;txt;csv;xlsx;xml;json"},
                    {"Projets", "xihm;xapi;xpgproj"}, {"Automate (Control Expert)", "xpg;xef;zef"}};
    if (!ui::pickFile(pick, [this](std::string path) {
            if (!path.empty()) (void)attachFile(path);
        }))
        say("Pas de s\xC3\xA9lecteur de fichiers ici : glisse le fichier sur le panneau.");
}

bool AiPanel::attachFile(const std::string& path) {
    std::string why;
    if (!assistant_->attach(path, &why)) {
        say(why);
        return false;
    }
    const auto& a = assistant_->pending().back().attachment;
    say("Pi\xC3\xA8" "ce jointe : " + a.name + " (" + formatBytes(a.bytes) + ")" + (a.file.empty() ? std::string{} : ", copi\xC3\xA9" "e dans le dossier de l'Assistant"));
    sync();
    return true;
}

void AiPanel::removePending(std::size_t index) {
    assistant_->detach(index);
    sync();
}

// ---- les propositions ----------------------------------------------------------------------
void AiPanel::openPreview(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return;
    if (p->state != "proposee") {
        say(p->state == "appliquee" ? "Cette proposition est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9" "e (Ctrl+Z la reprend)."
                                    : "Cette proposition a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e.");
        return;
    }
    if (!ui_.showDialog) return;
    ui_.showDialog(std::make_unique<AiPreviewDialog>(*this, message), [](const menu::DialogResult&) {});
}

bool AiPanel::applyProposal(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return false;
    const auto doApply = [this, message] {
        std::string why;
        const bool ok = assistant_->apply(message, &why);
        if (!ok) say(why);
        sync();
        return ok;
    };
    const Check ck = assistant_->check(message);
    // 1.16.1 : APPLIQUER MALGRE LES ERREURS (l'option des parametres) - toujours une confirmation qui les liste.
    if (!ck.ok() && !ck.stale && !ck.viewMissing && assistant_->settings().applyWithErrors && ui_.showDialog) {
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.malgre";
        spec.title = "Appliquer malgr\xC3\xA9 " + std::to_string(ck.errors.size()) + (ck.errors.size() > 1 ? " erreurs" : " erreur");
        spec.text = "Ce qui est valide s'applique ; ce qui est en erreur est laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 (comme l'aper\xC3\xA7u le montre) "
                    "et not\xC3\xA9 sous la r\xC3\xA9ponse et dans le journal.\nCtrl+Z reprend tout d'un coup.";
        // 1.16.1 (captures) : les erreurs dans le paragraphe, qui va a la ligne (une ligne de liste les coupait a droite)
        spec.text += "\n\nLaiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 :";
        for (const auto& e : ck.errors) spec.text += "\n\xE2\x80\xA2 " + e;
        spec.confirm = "Appliquer quand m\xC3\xAAme";
        spec.width = 620.f;
        ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, message](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            std::string why;
            if (!assistant_->applyIgnoringErrors(message, &why)) say(why);
            sync();
        });
        return false;   // la reponse viendra
    }
    if (ck.confirmRemoval && ui_.showDialog) {
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.confirmer";
        spec.title = "Supprimer " + std::to_string(ck.remove) + " objets";
        spec.text = "L'Assistant propose de supprimer " + std::to_string(ck.remove) + " objets de la vue \xC2\xAB " + p->view
                    + " \xC2\xBB. Une suppression multiple se confirme.\nCtrl+Z la reprend d'un coup.";
        spec.listTitle = "Objets supprim\xC3\xA9s";
        for (const auto& it : ck.items)
            if (it.what == Item::What::Remove) spec.items.push_back({it.name, std::string(hmi::kindLabel(it.kind)), true});
        spec.plainItems = true;
        spec.confirm = "Supprimer les " + std::to_string(ck.remove) + " objets";
        spec.danger = true;
        spec.width = 560.f;
        ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [doApply](const menu::DialogResult& r) {
            if (r.accepted()) (void)doApply();
        });
        return false;   // la reponse viendra
    }
    return doApply();
}

bool AiPanel::reapplyProposal(int message) {
    std::string why;
    const bool ok = assistant_->reapply(message, &why);
    if (!ok) say(why);
    sync();
    return ok;
}

bool AiPanel::refuseProposal(int message) {
    const bool ok = assistant_->refuse(message);
    sync();
    if (ok) say("Proposition refus\xC3\xA9" "e : le projet n'a pas chang\xC3\xA9. \xC2\xAB Revenir sur le refus \xC2\xBB, sur sa carte, la remet.");
    return ok;
}

void AiPanel::askRefuse(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p || p->state != "proposee") return;
    if (!ui_.showDialog) {
        (void)refuseProposal(message);
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.ai.refuser";
    spec.title = "Refuser la proposition";
    spec.text = "Refuser \xC2\xAB " + (p->summary.empty() ? std::string("cette proposition") : p->summary)
                + " \xC2\xBB ? Le projet ne change pas.\nTu pourras revenir sur ce refus depuis sa carte (\xC2\xAB Revenir sur le refus \xC2\xBB).";
    spec.confirm = "Refuser";
    spec.cancel = "Garder";
    spec.width = 520.f;
    ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, message](const menu::DialogResult& r) {
        if (r.accepted()) (void)refuseProposal(message);
    });
}

bool AiPanel::unrefuseProposal(int message) {
    const bool ok = assistant_->unrefuse(message);
    sync();
    return ok;
}

void AiPanel::editRequest(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return;
    const std::string request = p->request;
    // 1.16.1 : la proposition n'est plus refusee en silence - elle reste a decider (sa carte la garde).
    prompt_->setText(request);
    prompt_->takeFocus();
    sync();
    say("La demande est dans la saisie : modifie-la, puis Entr\xC3\xA9" "e.");
}

void AiPanel::openSettings() {
    if (!ui_.showDialog) return;
    ui_.showDialog(std::make_unique<AiSettingsDialog>(*this), [this](const menu::DialogResult&) {
        seen_ = 0;
        sync();
    });
}

bool AiPanel::retry() {
    std::string why;
    const bool ok = assistant_->retry(&why);
    if (!ok) say(why);
    if (ok) watchingReply_ = assistant_->generatingConversationId();   // 1.16.1 : l'avis de fin de reponse
    seen_ = 0;
    sync();
    if (ok) chat_->scrollToBottom();
    return ok;
}

void AiPanel::setDirect(bool direct) {
    assistant_->setDirectMode(direct);
    refreshButtons();
    say(direct ? "Mode Direct : chaque proposition s'applique d\xC3\xA8s la r\xC3\xA9ponse (Ctrl+Z la reprend)."
               : "Mode Pr\xC3\xA9visualisation : chaque proposition passe par l'aper\xC3\xA7u.");
}

// ---- la mise en page --------------------------------------------------------------------------
gfx::Rect AiPanel::rowRect(int index) const {
    return index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(index)] : gfx::Rect{};
}
gfx::Rect AiPanel::promptRect() const { return prompt_->bounds(); }
gfx::Rect AiPanel::chatRect() const { return chat_->bounds(); }

void AiPanel::layoutRows() {
    const auto& list = assistant_->conversations();
    rows_.assign(list.size(), gfx::Rect{});
    if (!listOpen_) return;
    const float content = static_cast<float>(list.size()) * kRowH;
    listScroll_ = std::clamp(listScroll_, 0.f, std::max(0.f, content - list_.h));
    const float space = listBar_.space(list_, content, list_.h);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const gfx::Rect r{list_.x, list_.y + static_cast<float>(i) * kRowH - listScroll_, list_.w - space, kRowH};
        if (r.bottom() <= list_.y || r.y >= list_.bottom()) continue;
        rows_[i] = r;
    }
}

void AiPanel::onLayout() {
    const auto b = bounds();
    // Le menu du clic droit : le cadre du panneau (ferme, il ne prend aucun clic ; ouvert, il deborde).
    menu_->setBounds(b);
    const float x = b.x, w = b.w;
    header_ = {x, b.y, w, kHeaderH};
    const float bs = 28.f;
    closeBtn_->setBounds({b.right() - kPad - bs, b.y + (kHeaderH - bs) / 2.f, bs, bs});
    settingsBtn_->setBounds({closeBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    newBtn_->setBounds({settingsBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    statusLine_ = {x, header_.bottom(), w, kStatusH};
    // 1.16.0 : la bande de la limite, sous l'etat de connexion - chaque ligne longue si elle tient,
    // sinon courte (le detail est dans l'infobulle).
    {
        if (bandKey_.empty()) refreshBand(0.0);
        bandLines_.clear();
        const float tw = w - 2 * kPad - 22.f;
        for (std::size_t i = 0; i < band_.lines.size(); ++i) {
            const std::string& longer = band_.lines[i];
            const std::string& shorter = i < band_.shortLines.size() ? band_.shortLines[i] : longer;
            bandLines_.push_back(ui::measureWidth(longer, kMeta) <= tw ? longer : shorter);
        }
        const float lh = ui::lineHeight(kMeta) + 2.f;
        const float bh = static_cast<float>(bandLines_.size()) * lh + 4.f + (band_.ratio >= 0 ? 4.f : 0.f);
        limitLine_ = {x, statusLine_.bottom(), w, bh};
    }
    modeRow_ = {x, limitLine_.bottom(), w, kModeH};
    const float pw = ui::measureWidth("Pr\xC3\xA9visualisation", gfx::FontId{16}) + 22.f;
    const float dw = ui::measureWidth("Direct", gfx::FontId{16}) + 26.f;
    modePreview_->setBounds({x + kPad, modeRow_.y + 3.f, pw, kModeH - 8.f});
    modeDirect_->setBounds({x + kPad + pw - 1.f, modeRow_.y + 3.f, dw, kModeH - 8.f});
    // Les conversations : l'en-tete, puis au plus kMaxRows lignes.
    listHead_ = {x, modeRow_.bottom() + 4.f, w, kListHeadH};
    const auto count = static_cast<int>(assistant_->conversations().size());
    const int shown = listOpen_ ? std::clamp(count, 1, kMaxRows) : 0;
    list_ = {x, listHead_.bottom(), w, static_cast<float>(shown) * kRowH};
    layoutRows();
    if (renaming() && renameIndex_ < static_cast<int>(rows_.size()) && rows_[static_cast<std::size_t>(renameIndex_)].h > 0.f) {
        const auto r = rows_[static_cast<std::size_t>(renameIndex_)];
        rename_->setBounds({r.x + 30.f, r.y + 2.f, r.w - 40.f, r.h - 4.f});
    } else if (renaming()) {
        rename_->setBounds({list_.x + 30.f, list_.y, list_.w - 40.f, kRowH - 4.f});
    }
    // Le bas : la barre (Joindre, Envoyer), la saisie, les pieces en attente ; la discussion prend le reste.
    bottomBar_ = {x, b.bottom() - kBottomH, w, kBottomH};
    // Etroit (1280 x 800) : Envoyer moins large, l'aide des touches garde sa place.
    const float sw = std::max(w < 340.f ? 84.f : 110.f, ui::measureWidth(sendBtn_->text(), gfx::FontId{16}) + (w < 340.f ? 28.f : 40.f));
    sendBtn_->setBounds({bottomBar_.right() - kPad - sw, bottomBar_.y + 5.f, sw, kBottomH - 10.f});
    attachBtn_->setBounds({x + kPad - 4.f, bottomBar_.y + 5.f, 32.f, kBottomH - 10.f});
    // 1.15.0 : CE QUI PART, sous la saisie, avant le premier envoi d'une conversation a un vrai
    // fournisseur (deux lignes au plus ; le reste dans l'infobulle).
    outgoingLines_.clear();
    outgoing_ = {};
    if (const std::string out = assistant_->outgoingSummary(); !out.empty()) {
        outgoingLines_ = wrapPlain(out, kMeta, w - 2 * kPad - 20.f);
        if (outgoingLines_.size() > 2) {
            outgoingLines_.resize(2);
            outgoingLines_.back() += " \xE2\x80\xA6";
        }
        const float oh = static_cast<float>(outgoingLines_.size()) * (ui::lineHeight(kMeta) + 2.f) + 4.f;
        outgoing_ = {x + kPad, bottomBar_.y - oh, w - 2 * kPad, oh};
    }
    // La saisie : de 1 a 5 lignes selon le texte (comme une messagerie), puis elle defile.
    const float ph = prompt_->heightFor(w - 2 * kPad);
    promptArea_ = {x + kPad, (outgoing_.h > 0.f ? outgoing_.y : bottomBar_.y) - ph - 2.f, w - 2 * kPad, ph};
    prompt_->setBounds(promptArea_);
    const float pendH = static_cast<float>(assistant_->pending().size()) * kPendingH;
    pendingArea_ = {x + kPad, promptArea_.y - pendH - (pendH > 0 ? 6.f : 0.f), w - 2 * kPad, pendH};
    const float chatTop = (listOpen_ ? list_.bottom() : listHead_.bottom()) + 1.f;
    const float chatBottom = pendingArea_.y - 6.f;
    chat_->setBounds({x, chatTop, w, std::max(40.f, chatBottom - chatTop)});
}

void AiPanel::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    const auto b = bounds();
    tips_.clear();
    ctx.r.fillRect(b, c.panelBg);
    ctx.r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
    // ---- l'en-tete
    ctx.r.fillRect({header_.x + 1.f, header_.y, header_.w - 1.f, header_.h}, c.headerBg);
    ui::drawIcon(ctx.r, ui::Icon::Assistant, {header_.x + kPad + 2.f, header_.y + (header_.h - 20.f) / 2.f, 20.f, 20.f}, c.accent);
    const float lhUi = ctx.r.lineHeight(f.uiBold);
    bold(ctx.r, {header_.x + kPad + 30.f, header_.y + (header_.h - lhUi) / 2.f}, "Assistant IA", f.uiBold, c.text);
    // ---- l'etat de connexion
    const auto& a = *assistant_;
    const bool on = a.settings().enabled;
    const float lhCap = ctx.r.lineHeight(f.caption);
    const float dotY = statusLine_.y + statusLine_.h / 2.f;
    // 1.15.0 : vert, un vrai fournisseur pret ; orange, la demonstration ou a configurer ; gris, desactive.
    // 1.16.0 : vert aussi, l'IA integree (toujours prete).
    const gfx::Color dot = !on ? c.textDisabled : (a.live() && a.ready()) || a.local() ? c.ok : c.warning;
    ctx.r.fillRoundedRect({statusLine_.x + kPad + 6.f, dotY - 4.f, 8.f, 8.f}, dot, 4.f);
    ctx.r.drawText({statusLine_.x + kPad + 22.f, statusLine_.y + (statusLine_.h - lhCap) / 2.f},
                   elideTip(ctx.r, a.connectionText(), f.caption, statusLine_.w - 2 * kPad - 24.f, statusLine_, tips_), f.caption, c.textMuted);
    // ---- 1.16.0 : la limite et la consommation, en direct
    if (limitLine_.h > 0.f) {
        const float lh = ctx.r.lineHeight(kMeta) + 2.f;
        const gfx::Color tone = band_.waiting ? c.warning : c.textMuted;
        ui::drawIcon(ctx.r, ui::Icon::Info, {limitLine_.x + kPad + 3.f, limitLine_.y + 3.f, 13.f, 13.f}, band_.waiting ? c.warning : c.textMuted);
        float ly = limitLine_.y + 2.f;
        for (const auto& l : bandLines_) {
            ctx.r.drawText({limitLine_.x + kPad + 22.f, ly}, elide(ctx.r, l, kMeta, limitLine_.w - 2 * kPad - 22.f), kMeta, tone);
            ly += lh;
        }
        if (band_.ratio >= 0) {   // la jauge du budget : orange a 80 %, rouge au-dela
            const gfx::Rect g{limitLine_.x + kPad + 22.f, limitLine_.bottom() - 5.f, limitLine_.w - 2 * kPad - 22.f, 3.f};
            const gfx::Color fill = band_.level >= 2 ? c.error : band_.level == 1 ? c.warning : c.ok;
            ctx.r.fillRect(g, alpha(c.border, 160));
            ctx.r.fillRect({g.x, g.y, g.w * static_cast<float>(std::clamp(band_.ratio, 0.0, 1.0)), g.h}, fill);
        }
        tips_.emplace_back(limitLine_, band_.tooltip);
    }
    // ---- le mode et la vue visee
    {
        const auto ctx2 = a.context();
        const float vx = modeDirect_->bounds().right() + 10.f;
        std::string viewText = ctx2.view ? "Vue : " + ctx2.view->name : std::string("Aucune vue ouverte");
        const std::string full = viewText;
        if (ctx2.view && ctx.r.measure(viewText, f.caption).width > modeRow_.right() - kPad - vx) viewText = ctx2.view->name;
        const gfx::Rect viewAt{vx, modeRow_.y, std::max(0.f, modeRow_.right() - kPad - vx), modeRow_.h};
        if (viewText != full) tips_.emplace_back(viewAt, full);
        ctx.r.drawText({vx, modeRow_.y + (modeRow_.h - lhCap) / 2.f}, elideTip(ctx.r, viewText, f.caption, viewAt.w, viewAt, tips_),
                       f.caption, ctx2.view ? c.textMuted : c.warning);
    }
    // ---- les conversations
    ctx.r.fillRect({listHead_.x + 1.f, listHead_.y, listHead_.w - 1.f, listHead_.h}, c.rowAltBg);
    ctx.r.fillRect({listHead_.x + 1.f, listHead_.y, listHead_.w - 1.f, 1.f}, c.border);
    ui::drawIcon(ctx.r, listOpen_ ? ui::Icon::Collapse : ui::Icon::Expand, {listHead_.x + kPad, listHead_.y + 5.f, 16.f, 16.f}, c.textMuted);
    const std::string head = "CONVERSATIONS (" + std::to_string(a.conversations().size()) + ")";
    ctx.r.drawText({listHead_.x + kPad + 22.f, listHead_.y + (listHead_.h - lhCap) / 2.f}, head, f.caption, c.textMuted);
    if (listOpen_) {
        ctx.r.pushClip(list_);
        const auto& list = a.conversations();
        if (list.empty())
            ctx.r.drawText({list_.x + kPad + 22.f, list_.y + (kRowH - lhCap) / 2.f}, "Aucune conversation : Nouvelle conversation (+).", f.caption,
                           c.textMuted);
        for (std::size_t i = 0; i < list.size() && i < rows_.size(); ++i) {
            const auto r = rows_[i];
            if (r.h <= 0.f) continue;
            const bool sel = static_cast<int>(i) == a.currentIndex();
            if (sel) ctx.r.fillRect(r, c.selectionBg);
            else if (static_cast<int>(i) == hoverRow_) ctx.r.fillRect(r, ctx.theme.brand.hover);
            if (sel) ctx.r.fillRect({r.x + 1.f, r.y, 3.f, r.h}, c.accent);
            const gfx::Color ink = sel ? c.selectionText : c.text;
            ui::drawIcon(ctx.r, ui::Icon::Assistant, {r.x + kPad + 2.f, r.y + (r.h - 14.f) / 2.f, 14.f, 14.f}, sel ? ink : c.textMuted);
            if (static_cast<int>(i) == renameIndex_) continue;   // le champ est dessus
            const auto& conv = list[i];
            const std::string date = shortDate(conv.modified);
            const float dw = ctx.r.measure(date, f.caption).width;
            float right = r.right() - kPad - dw;
            ctx.r.drawText({right, r.y + (r.h - lhCap) / 2.f}, date, f.caption, sel ? ink : c.textMuted);
            if (conv.demo) {
                const float bw = ctx.r.measure("d\xC3\xA9mo", f.caption).width + 10.f;
                right -= bw + 6.f;
                const gfx::Rect chip{right, r.y + 6.f, bw, r.h - 12.f};
                ctx.r.fillRoundedRect(chip, alpha(c.info, 40), 3.f);
                ctx.r.drawText({chip.x + 5.f, chip.y + (chip.h - lhCap) / 2.f}, "d\xC3\xA9mo", f.caption, sel ? ink : c.info);
            }
            const float tx = r.x + kPad + 24.f;
            ctx.r.drawText({tx, r.y + (r.h - ctx.r.lineHeight(kBody)) / 2.f},
                           elideTip(ctx.r, conv.title, kBody, right - tx - 8.f, {r.x, r.y, right - r.x, r.h}, tips_), kBody, ink);
        }
        ctx.r.popClip();
        listBar_.paint(ctx, list_, static_cast<float>(a.conversations().size()) * kRowH, list_.h, listScroll_);
    }
    ctx.r.fillRect({b.x + 1.f, (listOpen_ ? list_.bottom() : listHead_.bottom()), b.w - 1.f, 1.f}, c.border);
    // ---- les pieces jointes en attente
    pendingRemove_.clear();
    const auto& pend = a.pending();
    for (std::size_t i = 0; i < pend.size(); ++i) {
        const gfx::Rect r{pendingArea_.x, pendingArea_.y + static_cast<float>(i) * kPendingH, pendingArea_.w, kPendingH - 4.f};
        ctx.r.fillRoundedRect(r, c.inputBg, 4.f);
        ctx.r.strokeRect(r, c.border, 1.f);
        const auto look = lookOf(ctx.theme, pend[i].attachment.name);
        ui::drawIcon(ctx.r, look.icon, {r.x + 6.f, r.y + (r.h - 16.f) / 2.f, 16.f, 16.f}, look.tint);
        const gfx::Rect x{r.right() - 24.f, r.y + (r.h - 18.f) / 2.f, 18.f, 18.f};
        pendingRemove_.push_back(x);
        ui::drawIcon(ctx.r, ui::Icon::Close, {x.x + 2.f, x.y + 2.f, 14.f, 14.f}, c.textMuted);
        const std::string size = formatBytes(pend[i].attachment.bytes);
        const bool done = pend[i].progress >= 1.f;
        const std::string state = done ? size : "traitement " + std::to_string(static_cast<int>(pend[i].progress * 100.f)) + " %";
        const float sw = ctx.r.measure(state, f.caption).width;
        ctx.r.drawText({x.x - 8.f - sw, r.y + (r.h - lhCap) / 2.f}, state, f.caption, done ? c.textMuted : c.info);
        const float nx = r.x + 28.f;
        const std::string label = pend[i].attachment.name + "  \xC2\xB7  " + look.type;
        ctx.r.drawText({nx, r.y + (r.h - lhCap) / 2.f}, elideTip(ctx.r, label, f.caption, x.x - 16.f - sw - nx, {r.x, r.y, x.x - r.x, r.h}, tips_),
                       f.caption, c.text);
        if (!done) {
            ctx.r.fillRect({r.x + 1.f, r.bottom() - 3.f, r.w - 2.f, 2.f}, alpha(c.info, 50));
            ctx.r.fillRect({r.x + 1.f, r.bottom() - 3.f, (r.w - 2.f) * pend[i].progress, 2.f}, c.info);
        }
    }
    // ---- 1.15.0 : ce qui part (avant le premier envoi a un vrai fournisseur)
    if (outgoing_.h > 0.f) {
        ui::drawIcon(ctx.r, ui::Icon::Info, {outgoing_.x, outgoing_.y + 2.f, 14.f, 14.f}, c.info);
        float oy = outgoing_.y + 1.f;
        for (const auto& l : outgoingLines_) {
            ctx.r.drawText({outgoing_.x + 20.f, oy}, l, kMeta, c.textMuted);
            oy += ctx.r.lineHeight(kMeta) + 2.f;
        }
        tips_.emplace_back(outgoing_, a.outgoingSummary());
    }
    tips_.emplace_back(statusLine_, a.connectionText() + " (un clic : les Param\xC3\xA8tres de l'Assistant IA)");
    // ---- la barre du bas : l'aide des touches entre Joindre et Envoyer
    ctx.r.fillRect({b.x + 1.f, bottomBar_.y, b.w - 1.f, 1.f}, c.border);
    const float hx = attachBtn_->bounds().right() + 8.f;
    const float hw = sendBtn_->bounds().x - 8.f - hx;
    if (a.generating()) {
        ctx.r.drawText({hx, bottomBar_.y + (bottomBar_.h - lhCap) / 2.f}, elide(ctx.r, std::string(phaseLabel(a.phase())), f.caption, hw), f.caption,
                       c.textMuted);
    } else {
        // L'aide des touches : sur une ligne si elle tient, sinon sur deux.
        const std::string one = "Entr\xC3\xA9" "e : envoyer \xC2\xB7 Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne";
        if (ctx.r.measure(one, f.caption).width <= hw) {
            ctx.r.drawText({hx, bottomBar_.y + (bottomBar_.h - lhCap) / 2.f}, one, f.caption, c.textMuted);
        } else {
            const float y0 = bottomBar_.y + (bottomBar_.h - 2.f * lhCap - 1.f) / 2.f;
            ctx.r.drawText({hx, y0}, elide(ctx.r, "Entr\xC3\xA9" "e : envoyer", f.caption, hw), f.caption, c.textMuted);
            ctx.r.drawText({hx, y0 + lhCap + 1.f}, elide(ctx.r, "Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne", f.caption, hw), f.caption, c.textMuted);
        }
    }
}

std::string AiPanel::liveTooltip(gfx::Point mouse) const { return tipAt(tips_, mouse); }

// 1.16.0 : ce que dit la bande de la limite, a l'instant `now` ; la mise en page suit si le texte change.
void AiPanel::refreshBand(double now) {
    band_ = assistant_->usageBand(now);
    std::string key = std::to_string(band_.level) + "|" + std::to_string(band_.ratio) + "|";
    for (const auto& l : band_.lines) key += l + "\n";
    for (const auto& l : band_.shortLines) key += l + "\n";
    if (key == bandKey_) return;
    const bool linesChanged = bandKey_.empty() || band_.lines.size() != bandLines_.size();
    bandKey_ = std::move(key);
    if (linesChanged) invalidateLayout();
    else {
        // Les memes lignes, d'autres mots : la longueur choisie peut changer (longue ou courte).
        bandLines_.clear();
        const float tw = bounds().w - 2 * kPad - 22.f;
        for (std::size_t i = 0; i < band_.lines.size(); ++i)
            bandLines_.push_back(ui::measureWidth(band_.lines[i], kMeta) <= tw || i >= band_.shortLines.size() ? band_.lines[i] : band_.shortLines[i]);
    }
    invalidate();
}

int AiPanel::rowAt(gfx::Point p) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].h > 0.f && rows_[i].contains(p)) return static_cast<int>(i);
    return -1;
}

ui::EventResult AiPanel::onEvent(const ui::InputEvent& ev) {
    // La barre de la liste se tire.
    if (listOpen_ && listBar_.handle(*this, ev, list_, static_cast<float>(assistant_->conversations().size()) * kRowH, list_.h, listScroll_)) {
        layoutRows();
        return ui::EventResult::Consumed;
    }
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        if (!drop->path.empty()) (void)attachFile(drop->path);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        if (h != hoverRow_) {
            hoverRow_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos) && listOpen_) {
        listScroll_ -= w->dy * kRowH;
        layoutRows();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        for (std::size_t i = 0; i < pendingRemove_.size(); ++i)
            if (pendingRemove_[i].contains(d->pos) && d->button == ui::MouseButton::Left) {
                removePending(i);
                return ui::EventResult::Consumed;
            }
        if (listHead_.contains(d->pos) && d->button == ui::MouseButton::Left) {
            setConversationsOpen(!listOpen_);
            return ui::EventResult::Consumed;
        }
        const int row = rowAt(d->pos);
        if (row >= 0) {
            takeFocus();
            if (d->button == ui::MouseButton::Right) {
                (void)selectConversation(row);
                openRowMenu(row, d->pos);
            } else if (d->clickCount >= 2) {
                startRename(row);
            } else {
                (void)selectConversation(row);
            }
            return ui::EventResult::Consumed;
        }
        // 1.15.0 : l'etat de connexion ouvre les parametres ("A configurer" : un clic).
        if ((statusLine_.contains(d->pos) || limitLine_.contains(d->pos)) && d->button == ui::MouseButton::Left) {
            openSettings();
            return ui::EventResult::Consumed;
        }
        if (header_.contains(d->pos) || statusLine_.contains(d->pos) || modeRow_.contains(d->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && renaming()) {
            cancelRename();
            takeFocus();
            return ui::EventResult::Consumed;
        }
        // La liste a le clavier (le panneau lui-meme) : F2, Suppr, les fleches.
        if (focused()) {
            const int cur = assistant_->currentIndex();
            if (k->key == ui::Key::F2 && cur >= 0) { startRename(cur); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Delete && cur >= 0) { askDelete(cur); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Up && cur > 0) { (void)selectConversation(cur - 1); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Down && cur + 1 < static_cast<int>(assistant_->conversations().size())) {
                (void)selectConversation(cur + 1);
                return ui::EventResult::Consumed;
            }
        }
    }
    return ui::EventResult::Ignored;
}

// =============================================================================
//  La discussion
// =============================================================================
AiChatView::AiChatView(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {}

float AiChatView::viewHeight() const noexcept { return bounds().h; }
bool AiChatView::atBottom() const noexcept { return offset_ >= std::max(0.f, content_ - viewHeight()) - 2.f; }

void AiChatView::scrollToBottom() {
    stick_ = true;
    anchor_ = false;
    anchored_ = false;
    measure();
    offset_ = std::max(0.f, content_ - viewHeight());
    placeCards();
    invalidateLayout();
}

void AiChatView::scrollBy(float dy) {
    anchor_ = false;
    anchored_ = false;
    offset_ = std::clamp(offset_ + dy, 0.f, std::max(0.f, content_ - viewHeight()));
    stick_ = atBottom();
    placeCards();
    invalidateLayout();
}

std::vector<AiChatView::Line> AiChatView::layoutText(const std::string& md, float width) {
    const std::string key = std::to_string(static_cast<int>(width)) + "|" + md;
    auto& cache = layoutCache();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::vector<Line> out;
    const float lhBody = ui::lineHeight(kBody) + 4.f;
    const float lhHead = ui::lineHeight(kHead) + 6.f;
    std::size_t from = 0;
    bool firstPara = true, prevBlank = false;
    while (from <= md.size()) {
        std::size_t nl = md.find('\n', from);
        if (nl == std::string::npos) nl = md.size();
        std::string raw = md.substr(from, nl - from);
        from = nl + 1;
        if (raw.empty()) {
            prevBlank = true;
            if (nl >= md.size()) break;
            continue;
        }
        Line proto;
        std::uint8_t base = 0;
        float indent = 0.f;
        if (raw.rfind("## ", 0) == 0 || raw.rfind("# ", 0) == 0) {
            raw = raw.substr(raw.find(' ') + 1);
            base = 3;
        } else if (raw.rfind("- ", 0) == 0 || raw.rfind("* ", 0) == 0) {
            raw = raw.substr(2);
            proto.bullet = "\xE2\x80\xA2";
            indent = 16.f;
        } else if (raw.size() > 3 && std::isdigit(static_cast<unsigned char>(raw[0])) && raw[1] == '.' && raw[2] == ' ') {
            proto.bullet = raw.substr(0, 2);
            raw = raw.substr(3);
            indent = 18.f;
        }
        // Les morceaux : **gras**, `code`, *attenue*.
        struct Tok { std::string text; std::uint8_t style; bool space; };
        std::vector<Tok> toks;
        std::uint8_t style = base;
        std::string word;
        bool pendingSpace = false;
        const auto flush = [&] {
            if (word.empty()) return;   // l'espace en attente reste (un mot en gras apres un blanc)
            toks.push_back({word, style, pendingSpace});
            word.clear();
            pendingSpace = false;
        };
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const char ch = raw[i];
            if (ch == '*' && i + 1 < raw.size() && raw[i + 1] == '*' && base != 3) {
                flush();
                style = style == 1 ? 0 : 1;
                ++i;
                continue;
            }
            if (ch == '`' && base != 3) {
                flush();
                style = style == 2 ? 0 : 2;
                continue;
            }
            if (ch == '*' && base != 3 && style != 2) {
                flush();
                style = style == 4 ? 0 : 4;
                continue;
            }
            if (ch == ' ' && style != 2) {   // dans du `code`, l'espace fait partie du morceau
                flush();
                pendingSpace = true;
                continue;
            }
            word += ch;
        }
        flush();
        // 1.16.0 (captures) : un `code` plus large que la ligne (une formulation longue a cliquer) se coupe a
        // ses espaces, en morceaux qui tiennent - sinon il depassait le bord du panneau.
        {
            const float room = width - indent;
            std::vector<Tok> fitted;
            for (auto& t : toks) {
                if (t.style != 2 || ui::measureWidth(t.text, kBody) + 8.f <= room) {
                    fitted.push_back(std::move(t));
                    continue;
                }
                std::string cur;
                bool first = true;
                std::size_t p = 0;
                while (p <= t.text.size()) {
                    std::size_t sp = t.text.find(' ', p);
                    if (sp == std::string::npos) sp = t.text.size();
                    const std::string w = t.text.substr(p, sp - p);
                    const std::string cand = cur.empty() ? w : cur + " " + w;
                    if (!cur.empty() && ui::measureWidth(cand, kBody) + 8.f > room) {
                        fitted.push_back({cur, 2, first ? t.space : true});
                        first = false;
                        cur = w;
                    } else {
                        cur = cand;
                    }
                    p = sp + 1;
                }
                if (!cur.empty()) fitted.push_back({cur, 2, first ? t.space : true});
            }
            toks = std::move(fitted);
        }
        // Le placement, mot a mot.
        const float lh = base == 3 ? lhHead : lhBody;
        Line line = proto;
        line.h = lh;
        line.bulletX = indent > 0.f ? 4.f : 0.f;
        line.gapBefore = base == 3 ? (firstPara ? 0.f : 8.f) : (prevBlank && !firstPara ? 6.f : 0.f);
        float x = indent;
        bool lineStart = true;
        const float space = ui::measureWidth(" ", kBody);
        for (const auto& t : toks) {
            const gfx::FontId f = t.style == 3 ? kHead : kBody;
            const float tw = ui::measureWidth(t.text, f) + (t.style == 2 ? 8.f : 0.f) + (t.style == 1 || t.style == 3 ? 1.f : 0.f);
            const float gap = (!lineStart && t.space) ? space : 0.f;
            // Une ponctuation seule (le ; d'une liste, le : francais) ne passe pas seule a la ligne.
            const bool glue = t.text == ";" || t.text == ":" || t.text == "!" || t.text == "?" || t.text == "\xC2\xBB";
            if (!lineStart && !glue && x + gap + tw > width) {
                out.push_back(std::move(line));
                line = Line{};
                line.h = lh;
                x = indent;
                lineStart = true;
            }
            const float at = x + (lineStart ? 0.f : gap);
            line.runs.push_back({t.text, at, tw, t.style});
            x = at + tw;
            lineStart = false;
        }
        out.push_back(std::move(line));
        firstPara = false;
        prevBlank = false;
        if (nl >= md.size()) break;
    }
    cache.emplace(key, out);
    return out;
}

const AiChatView::Thumb& AiChatView::thumb(int message) const {
    auto& t = thumbs_[message];
    const Proposal* p = panel_.assistant().proposal(message);
    std::string key;
    if (p) {
        key = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
        // 1.16.0 : l'empreinte de ce que la proposition touche (la vue visee, la vue neuve, les variables et les types).
        if (auto doc = panel_.assistant().host().document ? panel_.assistant().host().document() : nullptr)
            key += "|" + proposalPrint(doc->project, *p);
    }
    if (t.key != key || key.empty()) {
        t.key = key;
        t.preview = panel_.assistant().previewOf(message);
        t.check = panel_.assistant().check(message);
    }
    return t;
}

void AiChatView::measure() {
    const float w = bounds().w;
    blocks_.clear();
    suggestionRects_.clear();
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    const float inner = std::max(80.f, w - 2 * kPad - 12.f);
    float y = 10.f;
    const int generating = a.generatingMessage();
    const float meta = ui::lineHeight(kMeta) + 4.f;
    if (conv) {
        for (std::size_t i = 0; i < conv->messages.size(); ++i) {
            const auto& m = conv->messages[i];
            Block b;
            b.message = static_cast<int>(i);
            b.user = m.role == "utilisateur";
            b.y = y;
            const bool live = static_cast<int>(i) == generating;
            const std::string text = live ? a.shownText() : m.text;
            if (b.user) {
                const float maxW = inner * 0.86f - 20.f;
                b.lines = layoutText(text, maxW);
                float tw = 0.f;
                for (const auto& l : b.lines)
                    for (const auto& r : l.runs) tw = std::max(tw, r.x + r.w);
                for (const auto& at : m.attachments) tw = std::max(tw, std::min(maxW, ui::measureWidth(at.name, kMeta) + 110.f));
                b.textW = tw;
                float th = 0.f;
                for (const auto& l : b.lines) th += l.h + l.gapBefore;
                if (text.empty()) th = 0.f;
                b.attachH = static_cast<float>(m.attachments.size()) * 24.f;
                const float bw = tw + 20.f, bh = th + b.attachH + 14.f;
                b.bubble = {w - kPad - 6.f - bw, meta, bw, bh};
                b.h = meta + bh;
            } else {
                b.lines = layoutText(text, inner - 8.f);
                float th = 0.f;
                for (const auto& l : b.lines) th += l.h + l.gapBefore;
                b.h = meta + th + 4.f;
                if (live) {   // l'indicateur, et ce qu'il dit en plus (une nouvelle tentative)
                    b.h += 26.f;
                    if (!a.statusDetail().empty())
                        b.h += static_cast<float>(std::min<std::size_t>(3, wrapPlain(a.statusDetail(), kMeta, inner - 30.f).size())) * kNoteH + 2.f;
                }
                // 1.15.0 : la note du logiciel (une erreur, une reponse coupee, une entree illisible).
                if (!live && !m.note.empty()) {
                    b.noteLines = wrapPlain(m.note, kMeta, inner - 30.f);
                    b.noteError = m.state == "erreur" || (m.tool && !m.tool->error.empty());
                    b.noteY = b.h + 2.f;
                    b.h += static_cast<float>(b.noteLines.size()) * kNoteH + 6.f;
                }
                if (!live && m.proposal) {
                    // Une carte close (appliquee, refusee) n'a plus d'apercu : la vue a change depuis.
                    const bool open = m.proposal->state == "proposee";
                    const Thumb& t = thumb(static_cast<int>(i));
                    const float cw = inner - 4.f;
                    float ch = 8.f + 24.f + 22.f;
                    // 1.16.0 : la vue, les variables, les types crees - une ligne sous les comptes.
                    if (const std::string made = creationSummary(*m.proposal); !made.empty()) {
                        b.createLines = wrapPlain("Cr\xC3\xA9" "e aussi : " + made, kMeta, cw - 24.f);
                        ch += static_cast<float>(b.createLines.size()) * kNoteH + 2.f;
                    }
                    if (open) {
                        const bool stale = t.check.stale || t.check.viewMissing;
                        b.buttons = {"Pr\xC3\xA9visualiser\xE2\x80\xA6"};
                        if (!stale) {   // 1.16.1 : le meme texte que le bouton (malgre les erreurs)
                            const bool anyway = !t.check.ok() && a.settings().applyWithErrors;
                            b.buttons.push_back(anyway ? "Appliquer malgr\xC3\xA9 " + std::to_string(t.check.errors.size())
                                                             + (t.check.errors.size() > 1 ? " erreurs\xE2\x80\xA6" : " erreur\xE2\x80\xA6")
                                                       : std::string("Appliquer"));
                        }
                        b.buttons.push_back(stale ? "Redemander" : "Refuser\xE2\x80\xA6");
                        // Les erreurs puis les avertissements (trois au plus ; le reste dans l'apercu), a la ligne.
                        std::size_t lines = 0;
                        const auto note = [&](const std::string& what, bool error) {
                            if (b.notes.size() >= 3) return;
                            Block::Note n;
                            n.error = error;
                            n.lines = wrapPlain(what, kMeta, cw - 44.f);
                            if (n.lines.size() > kNoteLines) {
                                n.lines.resize(kNoteLines);
                                n.lines.back() += " \xE2\x80\xA6";
                            }
                            lines += n.lines.size();
                            b.notes.push_back(std::move(n));
                        };
                        for (const auto& e : t.check.errors) note(e, true);
                        for (const auto& wv : t.check.warnings) note(wv, false);
                        ch += 100.f + 6.f + static_cast<float>(lines) * kNoteH + 6.f;
                    } else {
                        b.applied = a.appliedState(static_cast<int>(i));
                        std::string done;
                        switch (b.applied) {
                            case Assistant::Applied::Undone:
                                done = usesOpenView(*m.proposal)
                                           ? "Annul\xC3\xA9" "e : la vue est revenue \xC3\xA0 son \xC3\xA9tat d'avant (Ctrl+Z, ou le projet rouvert sans "
                                             "avoir \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9). R\xC3\xA9" "appliquer la remet."
                                           : "Annul\xC3\xA9" "e : ce qu'elle a cr\xC3\xA9\xC3\xA9 n'est plus dans le projet (Ctrl+Z, ou le projet rouvert sans "
                                             "avoir \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9). R\xC3\xA9" "appliquer le remet.";
                                b.buttons = {"R\xC3\xA9" "appliquer"};
                                break;
                            case Assistant::Applied::Changed:
                                done = "Appliqu\xC3\xA9" "e ; la vue a chang\xC3\xA9 depuis (d'autres modifications, ou elle n'existe plus).";
                                break;
                            case Assistant::Applied::Applied:
                                done = "Appliqu\xC3\xA9" "e au projet : Ctrl+Z la reprend d'un coup.";
                                break;
                            default:
                                done = "Refus\xC3\xA9" "e : le projet n'a pas chang\xC3\xA9.";
                                if (m.proposal->state == "refusee") b.buttons = {"Revenir sur le refus"};   // 1.16.1
                                break;
                        }
                        b.doneLines = wrapPlain(done, kMeta, cw - 24.f);
                        ch += static_cast<float>(b.doneLines.size()) * kNoteH + 2.f;
                    }
                    b.buttonRects = flowButtons(b.buttons, cw - 20.f);
                    b.buttonsH = b.buttonRects.empty() ? 0.f : b.buttonRects.back().bottom();
                    ch += (b.buttonsH > 0.f ? b.buttonsH + 4.f : 0.f) + 10.f;
                    b.card = {kPad + 2.f, b.h + 6.f, cw, ch};
                    b.h += ch + 10.f;
                }
                // 1.15.0 : Reessayer, sous le dernier message (une erreur, un arret).
                if (!live && i + 1 == conv->messages.size() && a.canRetry()) {
                    b.retryY = b.h + 4.f;
                    b.h += 38.f;
                }
            }
            y += b.h + 16.f;
            blocks_.push_back(std::move(b));
        }
    }
    // Les suggestions : une conversation vide, ou apres une demande non reconnue.
    const bool empty = !conv || conv->messages.empty();
    if (empty || a.suggestionsAfterLast()) {
        if (empty) {   // l'accueil : "Bonjour." puis son texte
            y += 6.f + 30.f;
            for (const auto& l : layoutText(welcomeText(a.live(), a.local()), w - 2 * kPad - 12.f)) y += l.h;
            y += 14.f;
        }
        suggestionsTop_ = y;
        y += ui::lineHeight(kMeta) + 8.f;
        for (const auto& s : a.suggestions()) {
            const auto lines = layoutText(s, inner - 26.f);
            const float h = std::min<std::size_t>(lines.size(), 2) * (ui::lineHeight(kBody) + 4.f) + 10.f;
            suggestionRects_.push_back({kPad + 2.f, y, inner - 4.f, h});
            y += h + 6.f;
        }
    }
    content_ = y + 8.f;
    measuredW_ = w;
}

void AiChatView::rebuild() {
    const bool wasBottom = stick_ || atBottom();
    measure();
    // Les boutons des cartes : un trio par proposition encore "proposee". Ils sont GARDES d'une
    // fois sur l'autre (caches quand ils ne servent pas) : un clic sur l'un d'eux peut refaire la
    // discussion pendant qu'il emet son signal - il ne doit pas etre detruit sous lui.
    for (auto& cb : cards_) {
        cb.message = -1;
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third}) btn->setVisibility(ui::Visibility::Collapsed);
    }
    const auto& a = panel_.assistant();
    std::size_t slot = 0;
    for (const auto& b : blocks_) {
        if (b.card.h <= 0.f || b.buttons.empty()) continue;
        const Proposal* p = a.proposal(b.message);
        if (!p) continue;
        const Thumb& t = thumb(b.message);
        if (slot == cards_.size()) {
            CardButtons cb;
            const std::string base = id() + ".carte" + std::to_string(slot + 1);
            auto mk = [&](const std::string& suffix, ui::Button::Style st) {
                auto btn = std::make_unique<ui::Button>(std::string{}, base + suffix);
                btn->setStyle(st);
                return &static_cast<ui::Button&>(addChild(std::move(btn)));
            };
            cb.preview = mk(".apercu", ui::Button::Style::Primary);
            cb.apply = mk(".appliquer", ui::Button::Style::Default);
            cb.third = mk(".refuser", ui::Button::Style::Flat);
            const std::size_t k = slot;
            links_ += cb.preview->clicked->connect([this, k] {
                if (k < cards_.size() && cards_[k].message >= 0) panel_.openPreview(cards_[k].message);
            });
            links_ += cb.apply->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].refused) (void)panel_.unrefuseProposal(cards_[k].message);   // 1.16.1
                else if (cards_[k].undone) (void)panel_.reapplyProposal(cards_[k].message);
                else (void)panel_.applyProposal(cards_[k].message);
            });
            links_ += cb.third->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].stale) panel_.editRequest(cards_[k].message);
                else panel_.askRefuse(cards_[k].message);   // 1.16.1 (captures) : une question d'abord, comme l'apercu
            });
            cards_.push_back(cb);
        }
        CardButtons& cb = cards_[slot++];
        cb.message = b.message;
        cb.refused = p->state == "refusee";
        cb.undone = p->state != "proposee" && !cb.refused;
        if (cb.refused) {   // 1.16.1 : refusee (par megarde, peut-etre) : Revenir sur le refus, seul
            cb.stale = false;
            cb.apply->setText("Revenir sur le refus");
            cb.apply->setTooltip("Remettre la proposition \xC3\xA0 d\xC3\xA9" "cider : la pr\xC3\xA9visualiser, l'appliquer ou la refuser. Rien ne change dans le projet.");
            cb.apply->setEnabled(true);
            cb.apply->setVisibility(ui::Visibility::Visible);
            continue;
        }
        if (cb.undone) {   // appliquee puis annulee (Ctrl+Z) : Reappliquer, seul
            cb.stale = false;
            cb.apply->setText("R\xC3\xA9" "appliquer");
            cb.apply->setTooltip("La vue est exactement celle de la proposition : l'appliquer de nouveau (un pas d'annulation, Ctrl+Z la reprend).");
            cb.apply->setEnabled(true);
            cb.apply->setVisibility(ui::Visibility::Visible);
            continue;
        }
        cb.stale = t.check.stale || t.check.viewMissing;
        cb.preview->setText("Pr\xC3\xA9visualiser\xE2\x80\xA6");
        cb.preview->setTooltip("L'aper\xC3\xA7u : les objets, les propri\xC3\xA9t\xC3\xA9s avant \xE2\x86\x92 apr\xC3\xA8s, les variables, les avertissements, et la vue dessin\xC3\xA9" "e.");
        // 1.16.1 : des erreurs, et l'option << Appliquer malgre les erreurs >> : le bouton le dit (une confirmation suit).
        const bool anyway = !t.check.ok() && panel_.assistant().settings().applyWithErrors;
        cb.apply->setText(anyway ? "Appliquer malgr\xC3\xA9 " + std::to_string(t.check.errors.size()) + (t.check.errors.size() > 1 ? " erreurs\xE2\x80\xA6" : " erreur\xE2\x80\xA6")
                                 : std::string("Appliquer"));
        cb.apply->setTooltip(anyway ? "Appliquer ce qui est valide ; les erreurs sont laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9 et not\xC3\xA9" "es au journal (une confirmation les liste)."
                                    : "Appliquer au projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
        cb.apply->setEnabled(t.check.ok() || anyway);
        cb.third->setText(cb.stale ? "Redemander" : "Refuser\xE2\x80\xA6");
        cb.third->setTooltip(cb.stale ? "La vue a chang\xC3\xA9 : remettre la demande dans la saisie."
                                      : "Refuser (une question d'abord) : le projet ne change pas ; la carte pourra revenir sur ce refus.");
        cb.preview->setVisibility(ui::Visibility::Visible);
        cb.apply->setVisibility(cb.stale ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        cb.third->setVisibility(ui::Visibility::Visible);
    }
    // 1.15.0 : Reessayer (un seul bouton, garde d'une fois sur l'autre comme ceux des cartes).
    if (!retry_) {
        auto btn = std::make_unique<ui::Button>("R\xC3\xA9" "essayer", id() + ".reessayer");
        btn->setIcon(ui::Icon::Refresh);
        btn->setTooltip("Renvoyer la m\xC3\xAAme demande (le message en erreur est retir\xC3\xA9).");
        retry_ = &static_cast<ui::Button&>(addChild(std::move(btn)));
        links_ += retry_->clicked->connect([this] { (void)panel_.retry(); });
    }
    retry_->setVisibility(ui::Visibility::Collapsed);
    if (wasBottom) offset_ = std::max(0.f, content_ - viewHeight());
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - viewHeight()));
    placeCards();
    invalidateLayout();
    invalidate();
}

gfx::Rect AiChatView::cardRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && b.card.h > 0.f)
            return {bounds().x + b.card.x, bounds().y + b.y - offset_ + b.card.y, b.card.w, b.card.h};
    return {};
}

ui::Button* AiChatView::cardButton(int message, int which) const {
    for (const auto& cb : cards_)
        if (cb.message == message && message >= 0) return which == 0 ? cb.preview : which == 1 ? cb.apply : cb.third;
    return nullptr;
}

gfx::Rect AiChatView::suggestionRect(std::size_t index) const {
    if (index >= suggestionRects_.size()) return {};
    const auto r = suggestionRects_[index];
    return {bounds().x + r.x, bounds().y + r.y - offset_, r.w, r.h};
}

std::vector<std::string> AiChatView::renderedLines(int message) const {
    std::vector<std::string> out;
    for (const auto& b : blocks_) {
        if (b.message != message) continue;
        for (const auto& l : b.lines) {
            std::string s = l.bullet.empty() ? std::string{} : l.bullet + " ";
            for (std::size_t k = 0; k < l.runs.size(); ++k) s += (k ? " " : "") + l.runs[k].text;
            out.push_back(s);
        }
    }
    return out;
}

void AiChatView::onLayout() {
    if (bounds().w != measuredW_) {
        measure();
        if (anchored_) anchor_ = true;   // la largeur a change depuis le placement : le refaire
    }
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - viewHeight()));
    if (stick_) offset_ = std::max(0.f, content_ - viewHeight());
    if (anchor_) applyAnchor();
    placeCards();
}

void AiChatView::showLastExchange() {
    anchor_ = true;
    stick_ = false;
    if (bounds().w > 40.f) {
        if (bounds().w != measuredW_) measure();
        applyAnchor();
        placeCards();
    }
    invalidateLayout();
}

void AiChatView::applyAnchor() {
    if (bounds().w <= 40.f) return;   // pas encore de largeur : au prochain onLayout
    anchor_ = false;
    anchored_ = true;
    // Le debut de la derniere demande en haut (tout tient : le debut de la conversation).
    float top = 0.f;
    for (const auto& b : blocks_)
        if (b.user) top = b.y - 8.f <= 10.f ? 0.f : b.y - 8.f;   // la premiere demande : tout en haut
    offset_ = std::clamp(top, 0.f, std::max(0.f, content_ - viewHeight()));
    // 1.14.2 (captures, 3e passe) : ne pas suivre le bas. Quand tout tenait a la premiere largeur,
    // atBottom() etait vrai : le volet retreci (1280 x 800) restait colle en bas, la question cachee.
    stick_ = false;
}

void AiChatView::placeCards() {
    // 1.14.2 (captures, 2e passe) : rappele a chaque changement du decalage (une reponse qui s'ecrit
    // fait defiler la discussion entre deux mises en page : le bouton restait a l'ancienne place).
    const auto b = bounds();
    if (retry_) {
        const Block* last = blocks_.empty() ? nullptr : &blocks_.back();
        if (last && last->retryY > 0.f) {
            const float bw = ui::measureWidth(retry_->text(), gfx::FontId{16}) + 46.f;
            const float y = b.y + last->y - offset_ + last->retryY;
            retry_->setBounds({b.x + kPad + 4.f, y, bw, kBtnH});
            retry_->setVisibility(y >= b.y && y + kBtnH <= b.bottom() ? ui::Visibility::Visible : ui::Visibility::Hidden);
        } else {
            retry_->setVisibility(ui::Visibility::Collapsed);
        }
    }
    for (auto& cb : cards_) {
        if (cb.message < 0) continue;
        const Block* bl = nullptr;
        for (const auto& x : blocks_)
            if (x.message == cb.message) bl = &x;
        if (!bl) continue;
        const gfx::Rect card{b.x + bl->card.x, b.y + bl->y - offset_ + bl->card.y, bl->card.w, bl->card.h};
        const float top = card.bottom() - 10.f - bl->buttonsH;
        std::size_t k = 0;
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third}) {
            if (btn->visibility() == ui::Visibility::Collapsed) continue;
            if (k >= bl->buttonRects.size()) {
                btn->setVisibility(ui::Visibility::Hidden);
                continue;
            }
            const auto r = bl->buttonRects[k++];
            btn->setBounds({card.x + 10.f + r.x, top + r.y, r.w, r.h});
            // Hors de la discussion : cache (la decoupe le couperait, le clic le trouverait).
            const bool inside = top + r.y >= b.y && top + r.bottom() <= b.bottom();
            btn->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
        }
    }
}

void AiChatView::paintIndicator(const ui::PaintContext& ctx, gfx::Rect at) {
    const auto& c = ctx.theme.color;
    const auto phase = panel_.assistant().phase();
    // Trois points qui s'allument tour a tour, puis l'etat (en attente, reflexion, ecriture).
    for (int k = 0; k < 3; ++k) {
        const double t = std::fmod(ctx.time * 2.4 - k * 0.33, 1.0);
        const int a = t < 0.5 ? 90 + static_cast<int>(t * 300) : 240 - static_cast<int>((t - 0.5) * 300);
        ctx.r.fillRoundedRect({at.x + static_cast<float>(k) * 10.f, at.y + 8.f, 6.f, 6.f}, alpha(c.accent, a), 3.f);
    }
    ctx.r.drawText({at.x + 38.f, at.y + (22.f - ctx.r.lineHeight(kMeta)) / 2.f}, std::string(phaseLabel(phase)) + "  (Arr\xC3\xAAter : le bouton en bas)",
                   kMeta, c.textMuted);
}

void AiChatView::paintCard(const ui::PaintContext& ctx, const Block& b, gfx::Rect card) {
    const auto& c = ctx.theme.color;
    const auto& a = panel_.assistant();
    const Proposal* p = a.proposal(b.message);
    if (!p) return;
    const Thumb& t = thumb(b.message);
    const bool stale = p->state == "proposee" && (t.check.stale || t.check.viewMissing);
    ctx.r.fillRoundedRect(card, ctx.theme.brand.card.a ? ctx.theme.brand.card : c.inputBg, 6.f);
    ctx.r.strokeRect(card, c.border, 1.f);
    const float lhCap = ctx.r.lineHeight(kMeta);
    // L'en-tete : le titre, l'etat.
    float y = card.y + 8.f;
    ui::drawIcon(ctx.r, ui::Icon::Wrench, {card.x + 10.f, y, 16.f, 16.f}, c.textMuted);
    std::string st;
    gfx::Color tone = c.info;
    if (p->state == "appliquee" && b.applied == Assistant::Applied::Undone) { st = "Annul\xC3\xA9" "e"; tone = c.warning; }
    else if (p->state == "appliquee") { st = "Appliqu\xC3\xA9" "e"; tone = c.ok; }
    else if (p->state == "refusee") { st = "Refus\xC3\xA9" "e"; tone = c.textMuted; }
    else if (stale) { st = "P\xC3\xA9rim\xC3\xA9" "e"; tone = c.warning; }
    // 1.15.0 : "A completer" seulement quand l'utilisateur peut completer (une variable a choisir dans
    // l'apercu) ; une proposition que le logiciel refuse (une propriete inventee...) est "Non valide".
    else if (!t.check.ok()) { st = t.check.missingVariable.empty() ? "Non valide" : "\xC3\x80 compl\xC3\xA9ter"; tone = c.error; }
    else st = "Propos\xC3\xA9" "e";
    const float sw = ctx.r.measure(st, kMeta).width + 14.f;
    const gfx::Rect chip{card.right() - 10.f - sw, y - 1.f, sw, 18.f};
    ctx.r.fillRoundedRect(chip, alpha(tone, 46), 9.f);
    ctx.r.drawText({chip.x + 7.f, chip.y + (chip.h - lhCap) / 2.f}, st, kMeta, tone);
    // Le titre : la place que laisse la pastille (un volet etroit : "Operations..." plutot qu'un chevauchement).
    bold(ctx.r, {card.x + 32.f, y + (16.f - ctx.r.lineHeight(kBody)) / 2.f},
         elideTip(ctx.r, "Op\xC3\xA9rations propos\xC3\xA9" "es", kBody, chip.x - 8.f - card.x - 32.f, {card.x, y - 2.f, chip.x - card.x, 20.f}, tips_), kBody,
         c.text);
    y += 24.f;
    // Les comptes.
    // Les comptes : resserres quand ils ne tiennent pas a l'aise (un volet etroit).
    float countGap = 24.f;
    {
        float need = 0.f;
        for (const char* l : {"Cr\xC3\xA9" "er 00", "Modifier 00", "Supprimer 00"}) need += ctx.r.measure(l, kMeta).width + 10.f;
        if (need + 2.f * countGap > card.w - 24.f) countGap = std::max(8.f, (card.w - 24.f - need) / 2.f);
    }
    const auto count = [&](const char* label, int n, gfx::Color col, float& x) {
        const std::string s = std::string(label) + " " + std::to_string(n);
        ctx.r.fillRoundedRect({x, y + 5.f, 6.f, 6.f}, n > 0 ? col : c.textDisabled, 3.f);
        ctx.r.drawText({x + 10.f, y}, s, kMeta, n > 0 ? c.text : c.textMuted);
        x += ctx.r.measure(s, kMeta).width + 10.f + countGap;
    };
    float cx = card.x + 12.f;
    // Proposee et lisible : les comptes de la validation ; close, ou perimee (la vue a change, ou
    // n'existe plus) : ceux de la proposition - ce que son texte annonce (la conversation "demo").
    const bool live = p->state == "proposee" && !stale;
    const int nc = live ? t.check.create : p->create;
    const int nm = live ? t.check.modify : p->modify;
    const int nr = live ? t.check.remove : p->remove;
    count("Cr\xC3\xA9" "er", nc, c.ok, cx);
    count("Modifier", nm, c.info, cx);
    count("Supprimer", nr, c.error, cx);
    y += 22.f;
    for (const auto& l : b.createLines) {   // 1.16.0 : la vue, les variables, les types crees
        ctx.r.drawText({card.x + 12.f, y}, l, kMeta, c.ok);
        y += kNoteH;
    }
    if (!b.createLines.empty()) y += 2.f;
    if (p->state != "proposee") {
        for (const auto& l : b.doneLines) {
            ctx.r.drawText({card.x + 12.f, y + 2.f}, l, kMeta, b.applied == Assistant::Applied::Undone ? c.warning : c.textMuted);
            y += kNoteH;
        }
        return;
    }
    // L'apercu : la copie de la vue, dessinee par les painters du logiciel.
    const gfx::Rect area{card.x + 10.f, y, card.w - 20.f, 100.f};
    ctx.r.fillRect(area, c.windowBg);
    if (t.preview.ok && t.preview.view.width > 0 && t.preview.view.height > 0) {
        const auto& v = t.preview.view;
        const float z = std::min(area.w / static_cast<float>(v.width), area.h / static_cast<float>(v.height));
        const HmiViewport vp{area.x + (area.w - static_cast<float>(v.width) * z) / 2.f, area.y + (area.h - static_cast<float>(v.height) * z) / 2.f, z};
        const gfx::Rect page{vp.originX, vp.originY, static_cast<float>(v.width) * z, static_cast<float>(v.height) * z};
        ctx.r.fillRect(page, parseColor(v.background, gfx::Color::rgb(0x20242B)));
        HmiPaintOptions opt;
        const auto doc = a.host().document ? a.host().document() : nullptr;
        if (doc) {
            opt.project = &doc->project;
            opt.assets = &doc->project.assets;
        }
        ctx.r.pushClip(page);
        paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
        if (p->state == "proposee") {
            const auto outline = [&](const std::vector<hmi::Id>& ids, gfx::Color col) {
                for (const auto id : ids)
                    for (const auto& o : v.objects)
                        if (o.id == id) {
                            const auto bx = hmi::edit::rotatedBounds(o);
                            const auto p0 = vp.toScreen(bx.x, bx.y);
                            ctx.r.strokeRect({p0.x - 1.f, p0.y - 1.f, static_cast<float>(bx.w) * z + 2.f, static_cast<float>(bx.h) * z + 2.f}, col, 1.f);
                        }
            };
            outline(t.preview.touched.created, c.ok);
            outline(t.preview.touched.modified, c.info);
            outline(t.preview.touched.removed, c.error);
        }
        ctx.r.popClip();
        ctx.r.strokeRect(page, c.border, 1.f);
    } else {
        // Pourquoi il n'y a pas d'apercu : a la ligne, centre dans la place de l'apercu.
        auto why = wrapPlain(t.preview.why.empty() ? std::string("Pas d'aper\xC3\xA7u") : t.preview.why, kMeta, area.w - 16.f);
        if (why.size() > 5) why.resize(5);
        float wy = area.y + (area.h - static_cast<float>(why.size()) * kNoteH) / 2.f;
        for (const auto& l : why) {
            ctx.r.drawText({area.x + 8.f, wy}, l, kMeta, c.textMuted);
            wy += kNoteH;
        }
    }
    y = area.bottom() + 6.f;
    // Les erreurs puis les avertissements (trois au plus ; le reste dans l'apercu), a la ligne.
    for (const auto& n : b.notes) {
        ui::drawIcon(ctx.r, n.error ? ui::Icon::Error : ui::Icon::Warning, {card.x + 12.f, y + 1.f, 14.f, 14.f}, n.error ? c.error : c.warning);
        for (const auto& l : n.lines) {
            ctx.r.drawText({card.x + 32.f, y}, l, kMeta, n.error ? c.text : c.textMuted);
            y += kNoteH;
        }
    }
}

std::string AiChatView::liveTooltip(gfx::Point mouse) const { return tipAt(tips_, mouse); }

void AiChatView::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto bb = bounds();
    tips_.clear();
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    ctx.r.pushClip(bb);
    const float meta = ctx.r.lineHeight(kMeta);
    const int generating = a.generatingMessage();
    for (const auto& b : blocks_) {
        const float top = bb.y + b.y - offset_;
        if (top > bb.bottom() || top + b.h < bb.y) continue;
        const auto& m = conv->messages[static_cast<std::size_t>(b.message)];
        const bool live = b.message == generating;
        // L'en-tete : qui, quand.
        const std::string when = shortDate(m.date, true);
        if (b.user) {
            const std::string who = "Moi \xC2\xB7 " + when;
            const float w = ctx.r.measure(who, kMeta).width;
            ctx.r.drawText({bb.right() - kPad - 6.f - w, top}, who, kMeta, c.textMuted);
            const gfx::Rect bub{bb.x + b.bubble.x, top + b.bubble.y, b.bubble.w, b.bubble.h};
            ctx.r.fillRoundedRect(bub, alpha(c.accent, 34), 6.f);
            ctx.r.strokeRect(bub, alpha(c.accent, 80), 1.f);
            float ly = bub.y + 7.f;
            if (!m.text.empty())
                for (const auto& l : b.lines) {
                    ly += l.gapBefore;
                    for (const auto& r : l.runs) ctx.r.drawText({bub.x + 10.f + r.x, ly}, r.text, kBody, c.text);
                    ly += l.h;
                }
            for (const auto& at : m.attachments) {
                const auto look = lookOf(ctx.theme, at.name);
                ui::drawIcon(ctx.r, look.icon, {bub.x + 10.f, ly + 3.f, 16.f, 16.f}, look.tint);
                const std::string s = at.name + "  \xC2\xB7  " + formatBytes(at.bytes);
                ctx.r.drawText({bub.x + 32.f, ly + 3.f}, elideTip(ctx.r, s, kMeta, bub.w - 42.f, {bub.x, ly, bub.w, 24.f}, tips_), kMeta, c.text);
                ly += 24.f;
            }
            continue;
        }
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {bb.x + kPad + 2.f, top + 1.f, 14.f, 14.f}, c.accent);
        std::string who = "Assistant \xC2\xB7 " + (live ? std::string("maintenant") : when);
        if (m.state == "arrete") who += " \xC2\xB7 arr\xC3\xAAt\xC3\xA9" "e";
        if (m.state == "erreur") who += " \xC2\xB7 erreur";
        ctx.r.drawText({bb.x + kPad + 22.f, top}, who, kMeta, m.state == "erreur" ? c.error : c.textMuted);
        float ly = top + meta + 4.f;
        const float tx = bb.x + kPad + 4.f;
        for (const auto& l : b.lines) {
            ly += l.gapBefore;
            if (!l.bullet.empty()) ctx.r.drawText({tx + l.bulletX, ly}, l.bullet, kBody, c.textMuted);
            for (const auto& r : l.runs) {
                const gfx::Point at{tx + r.x, ly + (r.style == 3 ? 0.f : 0.f)};
                switch (r.style) {
                    case 1: bold(ctx.r, at, r.text, kBody, c.text); break;
                    case 2: {
                        const float ch = ctx.r.lineHeight(kBody);
                        ctx.r.fillRoundedRect({at.x, at.y - 1.f, r.w, ch + 2.f}, ctx.theme.brand.codeBg.a ? ctx.theme.brand.codeBg : c.inputBg, 3.f);
                        ctx.r.drawText({at.x + 4.f, at.y}, r.text, kBody, c.syntaxString.a ? c.syntaxString : c.text);
                        break;
                    }
                    case 3: bold(ctx.r, at, r.text, kHead, c.text); break;
                    case 4: ctx.r.drawText(at, r.text, kBody, c.textMuted); break;
                    default: ctx.r.drawText(at, r.text, kBody, c.text); break;
                }
            }
            ly += l.h;
        }
        if (live) {
            // Le curseur d'ecriture au bout du texte, puis l'indicateur.
            // 1.15.0 : un vrai fournisseur passe a l'ecriture avant le premier morceau de texte (le bloc
            // de texte s'ouvre, les mots suivent) : sans ligne, pas de curseur (lire la derniere ligne
            // d'une liste vide faisait planter l'application).
            if (a.phase() == Assistant::Phase::Writing && !b.lines.empty() && std::fmod(ctx.time, 1.0) < 0.6) {
                const auto& last = b.lines.back();
                float endX = tx;
                if (!last.runs.empty()) endX = tx + last.runs.back().x + last.runs.back().w + 2.f;
                ctx.r.fillRect({endX, ly - last.h + 2.f, 7.f, ctx.r.lineHeight(kBody) - 2.f}, alpha(c.accent, 160));
            }
            paintIndicator(ctx, {tx, ly + 2.f, bb.w - 2 * kPad, 22.f});
            // 1.15.0 : une nouvelle tentative (la cause, l'attente), sous l'indicateur.
            if (!a.statusDetail().empty()) {
                auto lines = wrapPlain(a.statusDetail(), kMeta, std::max(80.f, bb.w - 2 * kPad - 12.f) - 30.f);
                if (lines.size() > 3) lines.resize(3);
                float dy = ly + 28.f;
                ui::drawIcon(ctx.r, ui::Icon::Refresh, {tx, dy + 1.f, 14.f, 14.f}, c.warning);
                for (const auto& l : lines) {
                    ctx.r.drawText({tx + 20.f, dy}, l, kMeta, c.textMuted);
                    dy += kNoteH;
                }
            }
        }
        // 1.15.0 : la note du logiciel (une erreur : en rouge ; une reponse coupee, refusee : en gris).
        if (!b.noteLines.empty()) {
            float ny = top + b.noteY;
            ui::drawIcon(ctx.r, b.noteError ? ui::Icon::Error : ui::Icon::Info, {tx, ny + 1.f, 14.f, 14.f}, b.noteError ? c.error : c.textMuted);
            for (const auto& l : b.noteLines) {
                ctx.r.drawText({tx + 20.f, ny}, l, kMeta, b.noteError ? c.text : c.textMuted);
                ny += kNoteH;
            }
        }
        if (b.card.h > 0.f) paintCard(ctx, b, {bb.x + b.card.x, top + b.card.y, b.card.w, b.card.h});
    }
    // L'accueil et les suggestions.
    const bool empty = !conv || conv->messages.empty();
    if (!suggestionRects_.empty()) {
        if (empty) {
            float y = bb.y + 16.f - offset_;
            ui::drawIcon(ctx.r, ui::Icon::Assistant, {bb.x + kPad + 2.f, y, 22.f, 22.f}, c.accent);   // le haut : 10 + 6 (measure)
            bold(ctx.r, {bb.x + kPad + 32.f, y + 2.f}, "Bonjour.", kHead, c.text);
            y += 30.f;
            for (const auto& l : layoutText(welcomeText(a.live(), a.local()), bb.w - 2 * kPad - 12.f)) {
                for (const auto& r : l.runs) {
                    if (r.style == 1) bold(ctx.r, {bb.x + kPad + 4.f + r.x, y}, r.text, kBody, c.textMuted);
                    else ctx.r.drawText({bb.x + kPad + 4.f + r.x, y}, r.text, kBody, c.textMuted);
                }
                y += l.h;
            }
        }
        ctx.r.drawText({bb.x + kPad + 4.f, bb.y + suggestionsTop_ - offset_}, "ESSAIE", kMeta, c.textMuted);
        const auto& s = a.suggestions();
        for (std::size_t i = 0; i < suggestionRects_.size() && i < s.size(); ++i) {
            const auto r0 = suggestionRects_[i];
            const gfx::Rect r{bb.x + r0.x, bb.y + r0.y - offset_, r0.w, r0.h};
            if (r.y > bb.bottom() || r.bottom() < bb.y) continue;
            ctx.r.fillRoundedRect(r, static_cast<int>(i) == hoverSuggestion_ ? ctx.theme.brand.hover : c.inputBg, 6.f);
            ctx.r.strokeRect(r, static_cast<int>(i) == hoverSuggestion_ ? c.accent : c.border, 1.f);
            ui::drawIcon(ctx.r, ui::Icon::Play, {r.x + 7.f, r.y + 7.f, 12.f, 12.f}, c.accent);
            const auto lines = layoutText(s[i], r.w - 26.f);
            float ly = r.y + 5.f;
            for (std::size_t k = 0; k < lines.size() && k < 2; ++k) {
                std::string t;
                for (std::size_t q = 0; q < lines[k].runs.size(); ++q) t += (q ? " " : "") + lines[k].runs[q].text;
                if (k == 1 && lines.size() > 2) {
                    t = elide(ctx.r, t + " \xE2\x80\xA6", kBody, r.w - 30.f);
                    tips_.emplace_back(r, s[i]);   // la suggestion entiere
                }
                ctx.r.drawText({r.x + 24.f, ly}, t, kBody, c.text);
                ly += ui::lineHeight(kBody) + 4.f;
            }
        }
    }
    ctx.r.popClip();
    bar_.paint(ctx, bb, content_, viewHeight(), offset_);
}

ui::EventResult AiChatView::onEvent(const ui::InputEvent& ev) {
    if (bar_.handle(*this, ev, bounds(), content_, viewHeight(), offset_)) {
        stick_ = atBottom();
        anchor_ = false;
        anchored_ = false;
        placeCards();
        invalidateLayout();
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos) && !w->mods.ctrl) {
        scrollBy(-w->dy * 48.f);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < suggestionRects_.size(); ++i)
            if (suggestionRect(i).contains(m->pos)) h = static_cast<int>(i);
        if (h != hoverSuggestion_) {
            hoverSuggestion_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        for (std::size_t i = 0; i < suggestionRects_.size(); ++i)
            if (suggestionRect(i).contains(d->pos)) {
                (void)panel_.clickSuggestion(i);
                return ui::EventResult::Consumed;
            }
        // Un clic sur une carte (hors de ses boutons) : l'apercu.
        for (const auto& b : blocks_)
            if (b.card.h > 0.f && cardRect(b.message).contains(d->pos) && d->clickCount >= 2) {
                panel_.openPreview(b.message);
                return ui::EventResult::Consumed;
            }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
