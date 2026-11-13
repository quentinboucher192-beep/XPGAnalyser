// =============================================================================
//  app/ai/AiPanel.cpp - voir AiPanel.hpp
// =============================================================================
#include "AiPanel.hpp"
#include "AiChatReply.hpp"   // 1.50.1 : repondre, reduire, copier
#include "AiDelete.hpp"   // 1.21.0 : la confirmation des suppressions

#include "AiLoop.hpp"   // 1.22.4 : ne pas tourner en rond (loop::choiceLabel)
#include "AiMarkdown.hpp"
#include "AiPlanStrip.hpp"
#include "AiContextPane.hpp"   // 1.21.0
#include "AiFileModelDialog.hpp"   // 1.21.0 : << Modele... >> d'un classeur joint
#include "AiNotifications.hpp"       // 1.21.2 : l'onglet Notifications (le journal)
#include "AiNotificationsView.hpp"   // 1.21.2 : ... et sa liste
#include "AiMissionBoard.hpp"        // 1.22.0 : les missions IA (missionui)
#include "AiMissionStrip.hpp"        // 1.22.0 : la mission active, << Creer une mission IA ? >>
#include "AiMissionsView.hpp"        // 1.22.0 : l'onglet Missions IA
#include "../Dossiers.hpp"
#include "../screens/Screens.hpp"   // MessageDialog

#include "AiPreviewDialog.hpp"
#include "AiSettingsDialog.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../hmi/HmiPanels.hpp"   // 1.24.1 : app::several (plusieurs conversations d'un coup : les noms, le compte rendu)
#include "../../hmi/HmiEdit.hpp"
#include "../../ui/FileKinds.hpp"
#include "../../ui/Icons.hpp"
#include "../../core/Perf.hpp"   // 1.22.2 : ce qui consomme (l'onglet Performances) - des portees de mesure seulement

#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <map>
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
constexpr float kTabsH = 30.f;   // 1.21.2 : les onglets Conversation | Notifications
const gfx::FontId kTab{14};

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

// 1.19.0 : les phases REELLES d'une proposition (celles qui ont eu lieu ; aucune n'est montree a l'avance).
std::string phasesOf(const Proposal& p, const plan::Plan& pl, Assistant::Applied applied, bool ok) {
    const std::string sep = " \xE2\x80\xBA ";
    std::string s = "Analyse" + sep + "Planification (" + std::to_string(pl.steps.size()) + " \xC3\xA9tapes)" + sep + "V\xC3\xA9rification";
    if (p.state == "proposee") return s + (ok ? " sans erreur bloquante" : " : erreurs") + sep + "Proposition (en attente de ta validation)";
    s += sep + "Application";
    if (applied == Assistant::Applied::Undone) return s + " annul\xC3\xA9" "e (Ctrl+Z)";
    return s + sep + "Compte rendu";
}

// L'en-tete du plan : le nombre d'etapes et l'avancement reel (les etats confirmes).
std::string planHeadOf(const Proposal& p, const plan::Plan& pl) {
    using plan::State;
    std::string s = "Plan d'action \xE2\x80\x94 " + std::to_string(pl.steps.size()) + " \xC3\xA9tapes";
    const auto part = [&](State st, const char* one, const char* many) {
        const std::size_t n = pl.count(st);
        if (n > 0) s += ", " + std::to_string(n) + " " + (n > 1 ? many : one);
    };
    if (p.state == "appliquee") {
        part(State::Applied, "appliqu\xC3\xA9" "e", "appliqu\xC3\xA9" "es");
        part(State::Cancelled, "annul\xC3\xA9" "e", "annul\xC3\xA9" "es");
    } else {
        part(State::Validated, "valid\xC3\xA9" "e", "valid\xC3\xA9" "es");
    }
    part(State::Failed, "en erreur", "en erreur");
    part(State::Skipped, "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es");
    return s;
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
constexpr float kActRow = 22.f;   // 1.22.6 : la ligne d'activite et son trait
constexpr const char* kDetectText = "D\xC3\xA9tecter ce qui r\xC3\xA9pond\xE2\x80\xA6";   // 1.18.0
constexpr const char* kNextText = "Proposer la suite";   // 1.21.3
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
            if (onEscape && onEscape()) return ui::EventResult::Consumed;   // 1.50.1 : la reponse annulee, le clavier reste
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
    strip_ = &static_cast<AiPlanStrip&>(addChild(std::make_unique<AiPlanStrip>(base + ".plan", *this)));   // 1.20.2
    // 1.21.0 : LE CONTEXTE DE LA DISCUSSION - au-dessus du plan ; ses gestes reviennent ici (le panneau confirme, ouvre).
    {
        AiContextPane::Callbacks cb;
        cb.open = [this](const std::string& att) { contextAction(0, att); };
        cb.details = [this](const std::string& att) { contextAction(1, att); };
        cb.analyses = [this](const std::string& att) { contextAction(2, att); };
        cb.remove = [this](const std::string& att) { contextAction(3, att); };
        cb.restore = [this](const std::string& att) { contextAction(4, att); };
        cb.model = [this](const std::string& att) { contextAction(5, att); };   // 1.21.0 : << Modele... >> d'un classeur
        cb.toggled = [this](bool) { invalidateLayout(); };
        ctxPane_ = &static_cast<AiContextPane&>(addChild(std::make_unique<AiContextPane>(base + ".contexte", std::move(cb))));
    }
    prompt_ = &static_cast<AiPromptEdit&>(addChild(std::make_unique<AiPromptEdit>(base + ".saisie")));
    // 1.50.1 : << En reponse a ... >> au-dessus de la saisie (la barre de la discussion des sessions) ; sa croix l'annule.
    replyBar_ = &static_cast<sessionui::ReplyBar&>(addChild(std::make_unique<sessionui::ReplyBar>(base + ".reponse")));
    replyBar_->setVisibility(ui::Visibility::Collapsed);
    prompt_->onEscape = [this] {   // Echap dans la saisie : la reponse d'abord
        if (!assistant_->replyTo()) return false;
        cancelReply();
        say("R\xC3\xA9ponse annul\xC3\xA9" "e : la demande ne cite plus de message.");
        return true;
    };
    replyBar_->onCancel = [this] {
        assistant_->clearReply();
        syncReply();
        prompt_->takeFocus();
        say("R\xC3\xA9ponse annul\xC3\xA9" "e : la demande ne cite plus de message.");
    };
    prompt_->setTooltip("Ta demande. Entr\xC3\xA9" "e : envoyer ; Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne ; un fichier l\xC3\xA2" "ch\xC3\xA9 ici se joint.");
    attachBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Joindre\xE2\x80\xA6", base + ".joindre")));
    attachBtn_->setIcon(ui::Icon::Link);
    attachBtn_->setCompact(true);   // l'icone seule (le libelle : l'infobulle, les seances de script)
    attachBtn_->setStyle(ui::Button::Style::Flat);
    attachBtn_->setTooltip("Joindre un fichier : image (PNG, JPG), PDF, texte, CSV, Excel, XML, JSON, projet (.xihm, .xapi). Il est copi\xC3\xA9 dans le dossier de l'Assistant.");
    sendBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer", base + ".envoyer")));
    sendBtn_->setStyle(ui::Button::Style::Primary);
    // 1.22.6 : ENVOYER EN MISSION - a cote d'Envoyer : la demande tapee devient une mission IA (l'icone seule, le libelle en infobulle).
    missionSendBtn_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer en mission", base + ".envoyer-en-mission")));
    missionSendBtn_->setIcon(ui::Icon::Task);
    missionSendBtn_->setCompact(true);
    missionSendBtn_->setStyle(ui::Button::Style::Flat);
    missionSendBtn_->setTooltip("Envoyer en mission : la demande devient une mission IA (un plan d'\xC3\xA9tapes v\xC3\xA9rifi\xC3\xA9" "es, reprenables) au "
                                "lieu d'un message. Rien n'est modifi\xC3\xA9 avant ta validation.");
    // 1.21.2 : L'ONGLET NOTIFICATIONS - le journal de l'Assistant, a la place de la conversation quand il est choisi.
    {
        AiNotificationsView::Callbacks cb;
        cb.follow = [this](const std::string& action) { return followNotification(action); };
        cb.status = [this](const std::string& text) { say("Assistant IA : " + text); };
        notifView_ = &static_cast<AiNotificationsView&>(addChild(std::make_unique<AiNotificationsView>(base + ".notifications", std::move(cb))));
        notifView_->setVisibility(ui::Visibility::Collapsed);
    }
    // 1.22.0 : LES MISSIONS IA - l'onglet (la liste), la bande de la mission active, la carte << Creer une mission IA ? >>.
    missionsView_ = &static_cast<AiMissionsView&>(addChild(std::make_unique<AiMissionsView>(base + ".missions", *this)));
    missionsView_->setVisibility(ui::Visibility::Collapsed);
    missionStrip_ = &static_cast<AiMissionStrip&>(addChild(std::make_unique<AiMissionStrip>(base + ".mission", *this)));
    missionStrip_->setVisibility(ui::Visibility::Collapsed);
    missionAsk_ = &static_cast<AiMissionAsk&>(addChild(std::make_unique<AiMissionAsk>(base + ".mission-ou-conversation")));
    missionAsk_->setVisibility(ui::Visibility::Collapsed);
    links_ += missionAsk_->chosen->connect([this](int what) {
        if (what == 1) (void)chooseMission(true);
        else if (what == 2) (void)chooseMission(false);
        else {
            const bool reply = askMessage_ >= 0;   // 1.22.6 : le conseil d'une reponse (rien dans la saisie)
            dismissMissionAsk();
            prompt_->takeFocus();
            say(reply ? std::string("Conseil ferm\xC3\xA9 : aucune mission n'est cr\xC3\xA9\xC3\xA9" "e.") : std::string("Rien n'est parti : la demande reste dans la saisie."));
        }
    });
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
    links_ += missionSendBtn_->clicked->connect([this] { (void)sendAsMission(); });   // 1.22.6
    links_ += menu_->itemChosen->connect([this](int item) { menuChosen(item); });
    refreshButtons();
    syncNotifications();   // 1.21.2 : la pastille des non lues des le depart
    syncMissions();        // 1.22.0
}

AiPanel::~AiPanel() { learning::setPhraseTester(nullptr); }

void AiPanel::say(const std::string& text) {
    if (text.empty()) return;
    lastStatus_ = text;
    if (ui_.status) ui_.status(text);
}

// ---- 1.50.1 : repondre a un message (AiChatReply.hpp) ----------------------------------------------------------------------
bool AiPanel::replyTo(int message) {
    std::string why;
    if (!assistant_->setReplyTo(message, &why)) {
        say(why);
        return false;
    }
    syncReply();
    prompt_->takeFocus();
    if (const auto* q = assistant_->replyTo())
        say("Tu r\xC3\xA9ponds \xC3\xA0 " + (q->who == "Moi" ? std::string("ta demande") : q->who == "Assistant" ? std::string("l'Assistant") : q->who)
            + " : ta demande citera ce message (\xC3\x89" "chap : annuler).");
    return true;
}

void AiPanel::cancelReply() {
    assistant_->clearReply();
    syncReply();
}

void AiPanel::syncReply() {
    const chatreply::Quote* q = assistant_->replyTo();
    const bool shown = replyBar_->active();
    if (!q) {
        if (shown) {
            replyBar_->clear();
            invalidateLayout();
        }
        return;
    }
    if (shown && replyBar_->replyId() == q->id) return;
    sessionui::Row row;
    row.id = q->id;
    row.who = q->who;
    row.text = q->excerpt;
    replyBar_->set(row);
    invalidateLayout();
    invalidate();
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
    missionSendBtn_->setEnabled(!gen && a.settings().enabled && !a.processing() && a.live());   // 1.22.6
    attachBtn_->setEnabled(!gen);
    prompt_->setReadOnly(!a.settings().enabled);
    prompt_->setPlaceholder(a.settings().enabled ? "D\xC3\xA9" "cris ta demande\xE2\x80\xA6 (ex. : Ajoute une cuve avec un indicateur de niveau)"
                                                 : "L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres)");
}

void AiPanel::tick(double now) {
    (void)assistant_->tick(now);
    core::perf::Lap lap;   // 1.22.2 : l'onglet Performances
    lap.to(XPG_PARTIE("ia.panneau.sync"));
    sync();
    // 1.20.0 : les verifications de fond des grosses propositions - leur avancement se redessine, leur resultat arrive.
    lap.to(XPG_PARTIE("ia.panneau.vignettes"));
    if (chat_) {
        if (chat_->pollThumbs()) {
            chat_->rebuild();
            invalidateLayout();
        }
        if (chat_->anyChecking() || assistant_->applyingAny() || chat_->copyFlash()) invalidate();
        // 1.20.2 : le plan en cours apparait, change de hauteur ou s'en va : la discussion se replace.
        if (strip_ && !notifsShown_ && !missionsShown_) {   // 1.21.2 : l'onglet Notifications le cache (1.22.0 : Missions IA aussi)
            const float chatTop = (listOpen_ ? list_.bottom() : listHead_.bottom()) + 1.f;
            const float want = strip_->heightFor(bounds().w, std::max(0.f, (chatBottom_ - chatTop - missionStripH_ - ctxH_) * 0.45f));
            if (std::fabs(want - stripH_) > 0.5f) invalidateLayout();
        }
    }
    // 1.16.1 : une reponse vient de finir - l'ecran avertit (s'il le faut : l'utilisateur ne regarde pas).
    lap.to(XPG_PARTIE("ia.panneau.reponse"));
    if (assistant_->generating()) {
        watchingReply_ = assistant_->generatingConversationId();
    } else if (!watchingReply_.empty()) {
        const std::string id = std::exchange(watchingReply_, std::string{});
        // 1.21.2 : l'onglet Notifications - une reponse (ou une proposition) prete quand sa conversation n'est pas a
        // l'ecran (une autre conversation, le panneau cache, l'onglet Notifications ouvert, la fenetre derriere une autre).
        // Une erreur du service : l'Assistant l'a deja notee (toujours) ; une reponse arretee par Arreter : rien.
        if (!looking(id))
            for (const auto& c : assistant_->conversations())
                if (c.id == id && !c.messages.empty() && c.messages.back().role == "assistant") notifyReply(c);
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
    lap.to(XPG_PARTIE("ia.panneau.notifications"));
    syncNotifications();   // 1.21.2 : le journal a change (une source de n'importe quel fil) : la liste, la pastille
    lap.to(XPG_PARTIE("ia.panneau.missions"));
    syncMissions();        // 1.22.0 : les missions (leur revision) : la liste, la bande, la pastille
    lap.to(XPG_PARTIE("ia.panneau.sync"));
    refreshBand(now);   // 1.16.0 : la limite en direct (le compte a rebours avance chaque seconde)
    // Ctrl+Z, Ctrl+Y, un autre geste sur la vue : la carte d'une proposition appliquee dit l'etat
    // REEL (relu quatre fois par seconde : une empreinte de la vue par proposition appliquee).
    lap.to(XPG_PARTIE("ia.panneau.etat-applique"));
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

// 1.21.0 : un geste du contexte de la discussion (0 ouvrir, 1 detail, 2 analyses, 3 retirer, 4 remettre, 5 modele d'un classeur).
void AiPanel::contextAction(int what, const std::string& attachmentId) {
    const attach::ContextFile* f = ctxPane_ ? ctxPane_->context().find(attachmentId) : nullptr;
    if (!f) return;
    const std::string name = f->attachment.name;
    const auto show = [this](const std::string& title, const std::string& text) {
        if (ui_.showDialog) ui_.showDialog(std::make_unique<MessageDialog>(title, text, MessageDialog::Icon::Info), [](const menu::DialogResult&) {});
        else say(text);
    };
    switch (what) {
        case 0: {
            if (f->availability == attach::Availability::Missing) {
                say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB est manquant : sa copie n'est plus dans le dossier de l'Assistant.");
                return;
            }
            // Seulement la copie (sous IA/pieces-jointes/) ou l'original que l'utilisateur a joint : jamais un autre chemin.
            if (!insideAttachments(f->path) && (f->attachment.originalPath.empty() || f->path != f->attachment.originalPath)) {
                say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB n'est pas ouvert : son chemin sort du dossier de l'Assistant.");
                return;
            }
            if (const std::string why = dossiers::ouvrirDansLeSysteme(f->path); !why.empty()) say("Assistant IA : " + why);
            return;
        }
        case 1: {
            std::string text = f->line() + "\n\n" + f->detail();
            if (!f->attachment.sha256.empty()) text += "\nEmpreinte SHA-256 : " + f->attachment.sha256;
            if (!f->path.empty()) text += "\nCopie : " + f->path;
            if (f->textMissing) text += "\nLe texte extrait n'est plus dans le cache : il sera refait \xC3\xA0 la prochaine ouverture de la conversation.";
            show("Fichier joint", text);
            return;
        }
        case 2: {
            std::string text;
            for (auto it = f->attachment.analyses.rbegin(); it != f->attachment.analyses.rend(); ++it) {
                text += (text.empty() ? "" : "\n\n") + it->date + " \xE2\x80\x94 " + it->kind + " \xE2\x80\x94 " + it->state;
                if (!it->summary.empty()) text += "\n" + it->summary;
                if (!it->error.empty()) text += "\n" + it->error;
                for (const auto& d : it->details) text += "\n\xE2\x80\xA2 " + d;
            }
            show("Analyses de " + name, text.empty() ? std::string("Aucune analyse pour l'instant.") : text);
            return;
        }
        case 3: {
            const auto run = [this, attachmentId] {
                std::string why;
                if (!assistant_->removeFromDiscussion(attachmentId, &why)) say("Assistant IA : " + why);
                sync();
            };
            if (!ui_.showDialog || f->pending) {   // pas encore envoyee : elle part simplement
                run();
                return;
            }
            HmiAskDialog::Spec spec;
            spec.id = "dialog.ai.retirer-fichier";
            spec.title = "Retirer de la discussion";
            spec.text = "Retirer \xC2\xAB " + name + " \xC2\xBB de la discussion ?\nIl ne partira plus avec les demandes ; sa copie reste dans "
                        "le dossier de l'Assistant et il se remet quand tu veux. L'historique garde son nom.";
            spec.confirm = "Retirer";
            spec.width = 520.f;
            ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
                if (r.accepted()) run();
            });
            return;
        }
        case 4: {
            std::string why;
            if (!assistant_->restoreToDiscussion(attachmentId, &why)) say("Assistant IA : " + why);
            sync();
            return;
        }
        case 5: {   // 1.21.0 : << Modele... >> - le genre, la correspondance, les modeles de fichiers ; le choix reste avec la piece
            // 1.22.8 : un PDF, une image - << Modele de vues >> : la lire (ou ne plus la lire) comme un modele de vues a reproduire
            if (!sheets::workbookExt(f->attachment.ext) && viewtpl::candidate(f->attachment)) {
                const bool on = !viewtpl::templateOf(f->attachment).has_value();
                std::string why;
                if (!assistant_->setViewTemplateChoice(attachmentId, on, &why)) say("Assistant IA : " + why);
                else if (on)
                    say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB est lu comme un mod\xC3\xA8le de vues (ses vues, leurs images, la charte) : "
                        "demande \xC2\xAB Reproduis ce mod\xC3\xA8le avec les donn\xC3\xA9" "es du projet \xC2\xBB.");
                else
                    say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB n'est plus lu comme un mod\xC3\xA8le de vues.");
                seen_ = 0;
                sync();
                return;
            }
            if (f->availability == attach::Availability::Missing) {
                say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB est manquant : son classeur ne peut pas \xC3\xAAtre relu.");
                return;
            }
            if (!ui_.showDialog) return;
            ui_.showDialog(std::make_unique<AiFileModelDialog>(*assistant_, attachmentId, [this](const std::string& s) { say(s); }),
                           [this](const menu::DialogResult&) {
                               seen_ = 0;   // le choix garde avec la piece : le contexte se redessine
                               sync();
                           });
            return;
        }
        default: return;
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
    syncMissions();              // 1.22.0 : les missions relues avec le reste
    chat_->showLastExchange();   // la premiere ouverture : le debut de la conversation, pas sa fin
}

void AiPanel::sync() {
    if (assistant_->revision() != seen_) {
        seen_ = assistant_->revision();
        if (ctxPane_) ctxPane_->setContext(assistant_->discussionContext());   // 1.21.0 (un stat par fichier)
        refreshButtons();
        chat_->rebuild();
        syncAdvice();   // 1.22.6 : le conseil de la derniere reponse (passer en mission IA ?)
        syncReply();    // 1.50.1 : la citation envoyee, ou une autre conversation ouverte : la barre se retire
        invalidateLayout();
        invalidate();
    }
    if (assistant_->verdictRevision() != verdictSeen_) pushVerdict();   // 1.22.5 : un avis du logiciel vient d'arriver
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

// 1.25.1 : SUPPRIMER DES CONVERSATIONS DU PANNEAU - leurs fichiers dans la corbeille de l'IA (IA/corbeille), puis UNE
// commande : un seul Ctrl+Z les rend (leurs fichiers, puis la liste du panneau), Ctrl+Y les y remet. Le panneau parti
// entretemps : les fichiers reviennent quand meme (l'Assistant les relira).
namespace {
void trashConversations(AiPanel& panel, const std::vector<std::string>& ids, const std::vector<std::string>& titles,
                        std::vector<std::string>& gone, std::vector<std::pair<std::string, std::string>>& refused) {
    struct Undo {
        std::vector<core::trash::Moved> moved;
        std::vector<ai::Conversation>   removed;
    };
    auto undo = std::make_shared<Undo>();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ai::Conversation c;
        if (panel.assistant().remove(ids[i], &undo->moved, &c)) {
            gone.push_back(i < titles.size() ? titles[i] : ids[i]);
            undo->removed.push_back(std::move(c));
        } else {
            refused.emplace_back(i < titles.size() ? titles[i] : ids[i], "introuvable");
        }
    }
    if (undo->removed.empty()) return;
    AiPanel* p = &panel;
    const std::weak_ptr<const void> alive = panel.prompt().submitted;   // le champ de la demande vit avec le panneau
    const auto again = [p, alive] {
        if (alive.expired()) return;
        p->chat().rebuild();
        p->invalidateLayout();
    };
    const std::size_t n = undo->removed.size();
    auto cmd = std::make_unique<core::trash::LambdaCommand>(
        n > 1 ? "Supprimer " + std::to_string(n) + " conversations" : "Supprimer la conversation " + undo->removed.front().title,
        [p, alive, undo]() -> core::Status {   // Ctrl+Y : elles repartent
            if (alive.expired()) return core::ok();
            undo->moved.clear();
            for (const auto& c : undo->removed) (void)p->assistant().remove(c.id, &undo->moved);
            return core::ok();
        },
        [p, alive, undo]() -> core::Status {   // Ctrl+Z : les fichiers, puis la liste du panneau
            std::string why;
            const bool all = core::trash::takeBackAll(undo->moved, &why);
            if (!alive.expired())
                for (const auto& c : undo->removed) (void)p->assistant().restoreConversation(c);
            if (!all) return core::fail(core::ErrorCode::FileUnreadable, "rendre " + why);
            return core::ok();
        },
        again, true);
    (void)core::trash::apply(std::move(cmd));
}
} // namespace

void AiPanel::askDelete(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Conversation& c = list[static_cast<std::size_t>(index)];
    const std::string id = c.id, title = c.title;
    std::size_t files = 0;
    for (const auto& m : c.messages) files += m.attachments.size();
    const auto run = [this, id, title] {
        std::vector<std::string> gone;
        std::vector<std::pair<std::string, std::string>> refused;
        trashConversations(*this, {id}, {title}, gone, refused);   // 1.25.1 : la corbeille de l'IA, Ctrl+Z la rend
        if (!gone.empty()) say("Conversation supprim\xC3\xA9" "e : " + title + " (Ctrl+Z la rend)");
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
                + " ?\nLe projet n'est pas touch\xC3\xA9 : seule la conversation part, dans la corbeille de l'IA (Ctrl+Z la rend).";
    spec.confirm = "Supprimer";
    spec.danger = true;
    spec.width = 520.f;
    ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
        if (r.accepted()) run();
    });
}

// ---- 1.24.1 : plusieurs conversations d'un coup ----
void AiPanel::toggleChosen(int index) {
    const auto& list = assistant_->conversations();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    if (chosenIds_.empty())   // le premier Ctrl+clic : la conversation ouverte entre dans le choix
        if (const int cur = assistant_->currentIndex(); cur >= 0 && cur < static_cast<int>(list.size())) chosenIds_.push_back(list[static_cast<std::size_t>(cur)].id);
    const std::string id = list[static_cast<std::size_t>(index)].id;
    if (const auto at = std::find(chosenIds_.begin(), chosenIds_.end(), id); at != chosenIds_.end()) chosenIds_.erase(at);
    else chosenIds_.push_back(id);
    invalidate();
}

std::vector<int> AiPanel::chosenConversations() const {
    std::vector<int> out;
    const auto& list = assistant_->conversations();
    for (std::size_t i = 0; i < list.size(); ++i)
        if (std::find(chosenIds_.begin(), chosenIds_.end(), list[i].id) != chosenIds_.end()) out.push_back(static_cast<int>(i));
    if (out.empty())
        if (const int cur = assistant_->currentIndex(); cur >= 0 && cur < static_cast<int>(list.size())) out.push_back(cur);
    return out;
}

void AiPanel::askDeleteChosen() {
    const auto rows = chosenConversations();
    if (rows.size() <= 1) {
        if (!rows.empty()) askDelete(rows.front());
        return;
    }
    const auto& list = assistant_->conversations();
    std::vector<std::string> ids, titles;
    for (const int r : rows) {
        ids.push_back(list[static_cast<std::size_t>(r)].id);
        titles.push_back(list[static_cast<std::size_t>(r)].title);
    }
    const auto run = [this, ids, titles] {
        std::vector<std::string> gone;
        std::vector<std::pair<std::string, std::string>> refused;
        trashConversations(*this, ids, titles, gone, refused);
        chosenIds_.clear();
        several::Batch b;
        b.noun = "conversation";
        b.nouns = "conversations";
        b.feminine = true;
        say(several::summary(b, gone, refused) + ".");
        chat_->rebuild();
        invalidateLayout();
    };
    if (!ui_.showDialog) {   // sans hote (un essai) : on supprime
        run();
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.ai.supprimer";
    spec.title = "Supprimer les " + std::to_string(ids.size()) + " conversations";
    spec.text = "Supprimer les " + std::to_string(ids.size()) + " conversations " + several::names(titles)
                + " et leurs messages ?\nElles vont dans la corbeille de l'IA (l'Assistant ne la lit pas ; elle se vide de ce qui y est "
                  "depuis plus de 7 jours) : un seul Ctrl+Z les rend. Le projet n'est pas touch\xC3\xA9.";
    spec.confirm = several::label("Supprimer", ids.size());
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
    // 1.24.1 : plusieurs conversations choisies - Supprimer les N ; Ouvrir, Renommer : une a la fois.
    if (const auto chosen = chosenConversations(); chosen.size() > 1) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = std::to_string(chosen.size()) + " conversations";
        items.push_back(head);
        for (const auto& [label, id] : std::vector<std::pair<std::string, int>>{{"Ouvrir", kMenuOpen}, {"Renommer\xE2\x80\xA6", kMenuRename}}) {
            ui::PopupMenu::Item it;
            it.label = label;
            it.id = id;
            it.enabled = false;
            it.disabledReason = several::kOneAtATime;
            items.push_back(it);
        }
        ui::PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
        ui::PopupMenu::Item del;
        del.label = several::label("Supprimer", chosen.size()) + "\xE2\x80\xA6";
        del.icon = ui::Icon::Delete;
        del.shortcut = "Suppr";
        del.id = kMenuDelete;
        items.push_back(del);
        menu_->setItems(std::move(items));
        menu_->openAtSurface(toSurface(at));
        return;
    }
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
        case kMenuDelete:
            if (chosenConversations().size() > 1) askDeleteChosen();   // 1.24.1 : plusieurs d'un coup
            else askDelete(row);
            break;
        case kMenuNew: newConversation(); break;
        default: break;
    }
}

// ---- envoyer --------------------------------------------------------------------------------
bool AiPanel::send() {
    const std::string text = prompt_->text();
    // 1.22.6 : UNE GROSSE DEMANDE (des etapes enumerees, la suite annoncee une fois de trop...) - le conseil d'abord : << Cette
    // demande est grosse : passer en mission IA ? >> (Creer la mission, Continuer ici) ; rien ne part encore.
    if (!assistant_->generating())
        if (Assistant::MissionAdvice adv; assistant_->adviseMission(text, &adv)) {
            if (askText_ == text && missionAsk_->advice()) {
                say("Choisis au-dessus de la saisie : \xC2\xAB Cr\xC3\xA9" "er la mission \xC2\xBB, ou \xC2\xAB Continuer ici \xC2\xBB.");
                return false;
            }
            offerAdvice(text, adv.reason, -1);
            return false;
        }
    // 1.22.0 : UNE DEMANDE COMPLEXE - << Creer une mission IA ? >> d'abord, rien ne part encore (c'est l'Assistant qui
    // juge : une IA connectee, une demande complexe, aucune mission dans cette conversation ; jamais une demande simple).
    if (!assistant_->generating() && assistant_->suggestsMission(text)) {
        if (askText_ == text) {
            say("Choisis au-dessus de la saisie : \xC2\xAB Cr\xC3\xA9" "er la mission \xC2\xBB, ou \xC2\xAB Continuer en conversation \xC2\xBB.");
            return false;
        }
        offerMission(text);
        return false;
    }
    if (!sendText(text)) return false;
    prompt_->setText({});
    if (!askText_.empty()) dismissMissionAsk();   // une autre demande est partie : la carte n'a plus d'objet
    return true;
}

bool AiPanel::sendText(const std::string& text) {
    cancelRename();
    if (ui_.licenceGate && !ui_.licenceGate("ia.envoyer")) return false;   // 1.39.0 : pas dans la licence - sa fenetre
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
    pick.filters = {{"Fichiers pris en charge", attachmentFilter()}, {"Images", "png;jpg;jpeg"}, {"Documents", "pdf;txt;csv;xlsx;xlsm;xml;json"},
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
        say(p->state == "appliquee"   ? "Cette proposition est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9" "e (Ctrl+Z la reprend)."
            : p->state == "remplacee" ? "Cette proposition est remplac\xC3\xA9" "e par sa correction (G\xC3\xA9n\xC3\xA9rer et Compiler) : elle ne s'applique plus."
                                      : "Cette proposition a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e.");
        return;
    }
    if (!ui_.showDialog) return;
    auto dialog = std::make_unique<AiPreviewDialog>(*this, message);
    trackPreview(dialog.get(), p->id);   // 1.22.5 : l'avis du logiciel lui sera passe
    ui_.showDialog(std::move(dialog), [alive = previewAlive_](const menu::DialogResult&) { *alive = false; });
}

std::vector<std::string> groupedErrors(const std::vector<std::string>& lines) {
    static const std::string kOpen = "\xC2\xAB ", kClose = " \xC2\xBB";
    struct Group {
        std::string head, tail;           // avant et apres le premier nom cite
        std::vector<std::string> names;
        std::string line;                 // la ligne telle quelle (un groupe d'un ou deux)
        std::vector<std::string> all;
    };
    std::vector<Group> groups;
    std::map<std::string, std::size_t> byKey;
    for (const auto& l : lines) {
        const auto a = l.find(kOpen);
        const auto b = a == std::string::npos ? std::string::npos : l.find(kClose, a + kOpen.size());
        std::string key = l, head, tail, name;
        if (b != std::string::npos) {
            head = l.substr(0, a);
            name = l.substr(a + kOpen.size(), b - a - kOpen.size());
            tail = l.substr(b + kClose.size());
            key = head + "\x01" + tail;
        }
        const auto it = byKey.find(key);
        if (it == byKey.end()) {
            byKey[key] = groups.size();
            groups.push_back({head, tail, name.empty() ? std::vector<std::string>{} : std::vector<std::string>{name}, l, {l}});
        } else {
            groups[it->second].names.push_back(name);
            groups[it->second].all.push_back(l);
        }
    }
    std::vector<std::string> out;
    for (const auto& g : groups) {
        if (g.all.size() < 3 || g.names.size() != g.all.size()) {
            for (const auto& l : g.all) out.push_back(l);
            continue;
        }
        std::string s = g.head;
        for (std::size_t i = 0; i < 3; ++i) s += (i ? ", " : "") + kOpen + g.names[i] + kClose;
        if (g.all.size() > 3) s += " et " + std::to_string(g.all.size() - 3) + (g.all.size() - 3 > 1 ? " autres" : " autre");
        s += " (" + std::to_string(g.all.size()) + " erreurs)" + g.tail;
        out.push_back(std::move(s));
    }
    return out;
}

// ---- 1.22.5 : l'avis du logiciel passe a la previsualisation ouverte ---------------------------------------------------------
namespace {
// AiPreviewDialog::setVerdict (la previsualisation, lot de l'apercu) : appelee si elle existe - sinon rien (la fenetre relit
// la carte a sa prochaine mise a jour).
template <class D>
void giveVerdict(D& dialog, const build::Verdict& v) {
    if constexpr (requires { dialog.setVerdict(v); }) dialog.setVerdict(v);
}
} // namespace

void AiPanel::trackPreview(AiPreviewDialog* dialog, const std::string& proposalId) {
    previewAlive_ = std::make_shared<bool>(dialog != nullptr);
    preview_ = dialog;
    previewProposal_ = proposalId;
    pushVerdict();   // deja la : tout de suite
}

void AiPanel::pushVerdict() {
    verdictSeen_ = assistant_->verdictRevision();
    if (!preview_ || !previewAlive_ || !*previewAlive_) return;
    const int m = assistant_->messageOfProposal(previewProposal_);
    if (const auto v = m >= 0 ? assistant_->verdictOf(m) : std::nullopt) giveVerdict(*preview_, *v);
}

bool AiPanel::applyProposal(int message) {
    const Proposal* p = assistant_->proposal(message);
    if (!p) return false;
    // 1.20.1 : une grosse proposition s'applique sur un fil de fond (la carte montre l'avancement) ; une petite, tout de suite.
    const auto doApply = [this, message] {
        std::string why;
        if (assistant_->applyInBackground(message, false, &why)) {
            sync();
            return true;
        }
        if (!why.empty()) {
            say(why);
            return false;
        }
        const bool ok = assistant_->apply(message, &why);
        if (!ok) say(why);
        sync();
        return ok;
    };
    if (assistant_->applying(message)) return false;   // deja en cours
    // 1.20.1 : la verification de la carte (faite sur le fil de fond) - pas une nouvelle validation sur ce fil.
    Check ck;
    if (chat_ && chat_->checking(message)) {
        say("La proposition se v\xC3\xA9rifie encore (sur un fil de fond) : Appliquer revient \xC3\xA0 la fin.");
        return false;
    }
    if (!chat_ || !chat_->resultReady(message, &ck, nullptr, nullptr)) {
        // 1.22.1 : une proposition lourde pas encore verifiee par la carte : jamais une validation sur ce fil (le blocage du
        // 01/11) - l'application de fond la fait, et dit ses erreurs.
        if (checkInBackground(*p)) return doApply();
        ck = assistant_->check(message);
    }
    // 1.22.5 : LES ERREURS DE GENERER ET COMPILER (l'avis du logiciel, verse dans la verification de la carte) - elles bloquent
    // comme les autres ; << Appliquer malgre les erreurs >> les dit a part (elles restent dans le projet : rien ne les retire).
    std::vector<std::string> buildErrors, otherErrors;
    {
        std::set<std::string> fromBuild;
        if (const auto v = assistant_->verdictOf(message); v && v->ran)
            for (const auto& f : v->findings)
                if (f.error) fromBuild.insert(build::lineOf(f));
        for (const auto& e : ck.errors) (fromBuild.count(e) ? buildErrors : otherErrors).push_back(e);
    }
    if (!buildErrors.empty() && otherErrors.empty() && !ck.stale && !ck.viewMissing && !assistant_->settings().applyWithErrors) {
        say("G\xC3\xA9n\xC3\xA9rer et Compiler trouvent " + std::to_string(buildErrors.size())
            + (buildErrors.size() > 1 ? " erreurs nouvelles" : " erreur nouvelle") + " : la proposition ne s'applique pas telle quelle. Corrige-la "
            "(Pr\xC3\xA9visualiser), ou coche \xC2\xAB Appliquer malgr\xC3\xA9 les erreurs \xC2\xBB (Param\xC3\xA8tres de l'Assistant IA).");
        return false;
    }
    // 1.16.1 : APPLIQUER MALGRE LES ERREURS (l'option des parametres) - toujours une confirmation qui les liste.
    if (!ck.ok() && !ck.stale && !ck.viewMissing && assistant_->settings().applyWithErrors && ui_.showDialog) {
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.malgre";
        spec.title = "Appliquer malgr\xC3\xA9 " + std::to_string(ck.errors.size()) + (ck.errors.size() > 1 ? " erreurs" : " erreur");
        spec.text = "Ce qui est valide s'applique ; ce qui est en erreur est laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 (comme l'aper\xC3\xA7u le montre) "
                    "et not\xC3\xA9 sous la r\xC3\xA9ponse et dans le journal.\nCtrl+Z reprend tout d'un coup.";
        // 1.16.1 (captures) : les erreurs dans le paragraphe, qui va a la ligne (une ligne de liste les coupait a droite) ;
        // 1.22.8 : le paragraphe defile (HmiAskDialog), et les erreurs qui ne different que par un nom tiennent en une ligne.
        if (!otherErrors.empty()) {
            spec.text += "\n\nLaiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 :";
            for (const auto& e : groupedErrors(otherErrors)) spec.text += "\n\xE2\x80\xA2 " + e;
        }
        if (!buildErrors.empty()) {   // 1.22.5 : elles ne se laissent pas de cote - elles seront dans le projet
            spec.text += "\n\nG\xC3\xA9n\xC3\xA9rer et Compiler (ces erreurs resteront dans le projet) :";
            for (const auto& e : groupedErrors(buildErrors)) spec.text += "\n\xE2\x80\xA2 " + e;
        }
        spec.confirm = "Appliquer quand m\xC3\xAAme";
        spec.width = 620.f;
        ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, message](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            std::string why;
            if (assistant_->applyInBackground(message, true, &why)) {   // 1.20.1 : une grosse proposition, sur un fil de fond
                sync();
                return;
            }
            if (!why.empty() || !assistant_->applyIgnoringErrors(message, &why)) say(why);
            sync();
        });
        return false;   // la reponse viendra
    }
    if (ck.confirmRemoval && ui_.showDialog) {
        // 1.21.0 : la confirmation dit ce qui part vraiment (des objets de la vue, ou des elements du projet : AiDelete).
        const del::Confirmation conf = del::confirmation(ck, *p);
        HmiAskDialog::Spec spec;
        spec.id = "dialog.ai.confirmer";
        spec.title = conf.title;
        spec.text = conf.text;
        spec.listTitle = ck.projectRemovals > 0 ? "Ce qui part" : "Objets supprim\xC3\xA9s";
        for (const auto& [name, detail] : conf.items) spec.items.push_back({name, detail, true});
        spec.plainItems = true;
        spec.confirm = conf.confirm;
        spec.danger = true;
        spec.width = 620.f;
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

bool AiPanel::canDetect(int message) const {
    const Proposal* p = assistant_->proposal(message);
    if (!p || p->state != "appliquee" || !ui_.openScan) return false;
    const auto st = assistant_->appliedState(message);
    return st != Assistant::Applied::Undone && !proposedDetections(*p).empty();
}

bool AiPanel::detectFromProposal(int message) {
    // 1.18.0 : la detection des equipements est une action de l'utilisateur. L'IA l'a PROPOSEE ;
    // ce bouton ouvre la confirmation du scanner (1.21.0), la plage remplie, et c'est l'utilisateur qui clique Lancer.
    if (!canDetect(message)) return false;
    const Proposal* p = assistant_->proposal(message);
    const auto found = proposedDetections(*p);
    const auto& [equipment, range] = found.front();
    std::string why;
    if (!ui_.openScan(equipment, range, &why)) {
        say(why.empty() ? std::string("Le scanner n'a pas pu s'ouvrir.") : why);
        return false;
    }
    assistant_->expectScan();   // 1.21.2 : sa fin ira dans l'onglet Notifications
    say("Communication \xE2\x80\xBA Scanner IP : la confirmation est ouverte" + (range.empty() ? std::string{} : ", la plage " + range + " remplie")
        + ". Rien n'est parti sur le r\xC3\xA9seau : v\xC3\xA9rifie et clique Lancer.");   // 1.21.0 : la confirmation obligatoire
    return true;
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

// 1.22.4 : LE CHOIX sur la carte d'une proposition qui tourne en rond.
bool AiPanel::chooseLoop(int message, Assistant::LoopChoice choice) {
    const Assistant::LoopInfo info = assistant_->loopOf(message);
    if (choice == Assistant::LoopChoice::ForceFinish) {   // la question d'abord (missionAct), puis la fin forcee
        if (info.mission.empty() || !info.held) {
            say("Assistant IA : cette proposition n'appartient pas \xC3\xA0 une mission, ou elle n'attend plus de choix.");
            return false;
        }
        return missionAct(info.mission, MissionAct::ForceFinish);
    }
    std::string why, request;
    if (!assistant_->chooseLoop(message, choice, &why, &request)) {
        say("Assistant IA : " + (why.empty() ? std::string("ce choix n'a pas pu se faire.") : why));
        sync();
        return false;
    }
    const std::string said = assistant_->takeNotice();   // dit ici, une fois
    switch (choice) {
        case Assistant::LoopChoice::ByHand:
            openPreview(message);
            say("Corrige la proposition dans la pr\xC3\xA9visualisation (le clic droit sur une erreur), puis applique-la.");
            break;
        case Assistant::LoopChoice::ProposeAnyway:
            say("Propos\xC3\xA9" "e quand m\xC3\xAAme : pr\xC3\xA9visualise-la, puis applique-la ou refuse-la.");
            break;
        case Assistant::LoopChoice::EditRequest:
            if (!request.empty()) {
                prompt_->setText(request);
                prompt_->takeFocus();
            }
            say(info.mission.empty() ? std::string("La demande est dans la saisie : modifie-la, puis Entr\xC3\xA9" "e.")
                                     : "\xC3\x89" "cris ta consigne apr\xC3\xA8s \xC2\xAB " + request + "\xC2\xBB, puis Entr\xC3\xA9" "e : la mission repart avec elle "
                                       "(elle est gard\xC3\xA9" "e et dite \xC3\xA0 l'IA).");
            break;
        case Assistant::LoopChoice::SkipStep:
            say(said.empty() ? std::string("\xC3\x89tape ignor\xC3\xA9" "e : la mission continue sans elle.") : said);
            syncMissions();
            break;
        case Assistant::LoopChoice::ForceFinish:
            break;
    }
    sync();
    return true;
}

void AiPanel::askFix(const std::string& text) {
    if (text.empty()) return;
    prompt_->setText(text);
    prompt_->takeFocus();
    sync();
    say("La demande de correction est dans la saisie : relis-la, puis Entr\xC3\xA9" "e. Rien n'est envoy\xC3\xA9 seul.");
}

void AiPanel::notice(const std::string& text) {
    if (!text.empty()) say(text);
}

void AiPanel::openPreviewFor(int message, const std::string& variable) {
    const Proposal* p = assistant_->proposal(message);
    if (!p || p->state != "proposee" || !ui_.showDialog) {
        openPreview(message);
        return;
    }
    auto dialog = std::make_unique<AiPreviewDialog>(*this, message);
    dialog->focusVariable(variable, true);   // le choix du haut vise cette variable, sa liste ouverte
    trackPreview(dialog.get(), p->id);       // 1.22.5
    ui_.showDialog(std::move(dialog), [alive = previewAlive_](const menu::DialogResult&) { *alive = false; });
}

void AiPanel::askUnit(int message, const std::string& symbol, std::function<void(bool)> done) {
    const Proposal* p = assistant_->proposal(message);
    const auto doc = assistant_->host().document ? assistant_->host().document() : nullptr;
    if (!p || p->state != "proposee" || !doc || !ui_.showDialog) {
        if (done) done(false);
        return;
    }
    const fixes::UnitForm form = fixes::unitForm(doc->project, symbol);
    std::vector<FormDialog::Field> fields;
    for (std::size_t i = 0; i < form.labels.size(); ++i) {
        FormDialog::Field f;
        f.label = form.labels[i];
        f.value = i < form.values.size() ? form.values[i] : std::string{};
        if (i == 1) f.choices = form.families;
        if (i == 2) f.placeholder = "9.80665";
        fields.push_back(std::move(f));
    }
    auto dialog = std::make_unique<FormDialog>("dialog.ai.unite", "Cr\xC3\xA9" "er l'unit\xC3\xA9 \xC2\xAB " + form.values[0] + " \xC2\xBB",
                                               form.explanation, std::move(fields), "Ajouter \xC3\xA0 la proposition");
    const hmi::Config cfg = doc->project.config;
    // Le symbole (grise) dit la conversion lue a chaque frappe, ou ce qui ne va pas ; une famille neuve ouvre ses trois champs.
    dialog->setRules([cfg](const std::vector<std::string>& values, std::vector<FormDialog::FieldState>& state) {
        const bool fresh = values.size() > 1 && values[1] == fixes::kNewFamily;
        for (std::size_t i = 0; i < state.size(); ++i) state[i].error.clear();
        if (!state.empty()) {
            state[0].enabled = false;
            state[0].hint = fixes::unitFormCheck(cfg, values);
        }
        for (std::size_t i = 5; i < state.size(); ++i) {
            state[i].enabled = fresh;
            state[i].hint = fresh ? std::string{} : std::string("seulement pour une famille neuve");
        }
    });
    dialog->setValidator([cfg](const std::vector<std::string>& values, std::size_t& field) -> std::string {
        int at = -1;
        bool ok = false;
        const std::string said = fixes::unitFormCheck(cfg, values, &at, &ok);
        if (ok) return {};
        field = at < 0 ? 1 : static_cast<std::size_t>(at);
        return said;
    });
    ui_.showDialog(std::move(dialog), [this, message, done](const menu::DialogResult& r) {
        bool added = false;
        if (r.accepted()) {
            std::string why;
            added = assistant_->addUnit(message, fixes::unitFromForm(FormDialog::split(r.payload)), &why);
            say(added ? assistant_->takeNotice() : "Unit\xC3\xA9 non ajout\xC3\xA9" "e : " + why);
            sync();
        }
        if (done) done(added);
    });
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

// ---- 1.21.2 : l'onglet Notifications ----------------------------------------------------------
void AiPanel::syncNotifications() {
    if (!notifView_) return;
    const std::uint64_t rev = notifs::revision();
    if (rev == notifsSeen_) return;
    notifsSeen_ = rev;
    notifView_->refresh();
    const std::size_t before = badge_;
    badge_ = notifView_->unreadCount();
    badgeError_ = std::any_of(notifView_->items().begin(), notifView_->items().end(), [](const notifs::Item& n) { return !n.read && n.tone == "error"; });
    latest_ = notifView_->items().empty() ? std::string{} : notifView_->items().front().title;
    if ((before == 0) != (badge_ == 0) || std::to_string(before).size() != std::to_string(badge_).size()) invalidateLayout();   // la largeur de l'onglet
    invalidate();
}

bool AiPanel::looking(const std::string& conversation) const {
    for (const ui::Widget* w = this; w; w = w->parent())
        if (!w->visible()) return false;   // le panneau replie, cache
    if (notifsShown_ || missionsShown_) return false;   // l'onglet Notifications (ou Missions IA) est devant la conversation
    if (ui_.windowFocused && !ui_.windowFocused()) return false;
    const Conversation* c = assistant_->current();
    return c && c->id == conversation;
}

void AiPanel::notifyReply(const Conversation& c) {
    const Message& m = c.messages.back();
    if (m.state == "erreur" || m.state == "arrete") return;   // l'erreur : deja notee par l'Assistant ; arretee : par toi
    const int index = static_cast<int>(c.messages.size()) - 1;
    notifs::Item n;
    if (m.proposal) {
        if (m.proposal->state != "proposee") return;   // deja appliquee (le mode Direct) : sa notification le dit
        n.category = "Proposition";
        n.title = "Proposition pr\xC3\xAAte" + (m.proposal->summary.empty() ? std::string{} : " : " + notifs::oneLine(m.proposal->summary, 90));
        n.detail = "\xC2\xAB " + c.title + " \xC2\xBB : elle attend ta d\xC3\xA9" "cision sur sa carte (Pr\xC3\xA9visualiser, Appliquer, Refuser). Rien n'a chang\xC3\xA9 dans le projet.";
        n.button = "Voir la carte";
        n.action = notifs::toCard(c.id, index);
        n.key = "prete:" + m.proposal->id;
    } else {
        n.category = "R\xC3\xA9ponse";
        n.title = "R\xC3\xA9ponse pr\xC3\xAAte dans \xC2\xAB " + c.title + " \xC2\xBB";
        n.detail = notifs::oneLine(m.text.empty() ? m.note : m.text, 220);
        n.button = "Voir la r\xC3\xA9ponse";
        n.action = notifs::toConversation(c.id, index);
        n.key = "reponse:" + c.id + ":" + (m.id.empty() ? std::to_string(index) : m.id);
    }
    (void)notifs::post(std::move(n));
}

void AiPanel::showNotifications(bool on) {
    if (on == notifsShown_) return;
    if (on) {
        cancelRename();
        missionsShown_ = false;   // 1.22.0 : un onglet a la fois
    }
    notifsShown_ = on;
    syncNotifications();
    if (on && notifView_) {
        notifView_->refresh();
        notifView_->takeFocus();
    }
    invalidateLayout();
    invalidate();
    if (on) say(badge_ > 0 ? "Notifications de l'Assistant : " + std::to_string(badge_) + (badge_ > 1 ? " non lues." : " non lue.")
                           : std::string("Notifications de l'Assistant : tout est lu."));
}

bool AiPanel::followNotification(const std::string& action) {
    const notifs::Target t = notifs::parseAction(action);
    if (t.kind == "conv" || t.kind == "carte") {
        const int index = assistant_->indexOf(t.id);
        if (index < 0) {
            say("Assistant IA : cette conversation n'existe plus (elle a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9" "e).");
            return false;
        }
        showConversation();
        (void)selectConversation(index);
        const Conversation* c = assistant_->current();
        const int msg = c && t.message >= 0 && t.message < static_cast<int>(c->messages.size()) ? t.message : -1;
        if (t.kind == "carte" && (msg < 0 || !assistant_->proposal(msg))) {
            say("Assistant IA : la proposition n'est plus dans la conversation ; la conversation est ouverte.");
            return true;
        }
        if (msg >= 0) {
            layout();   // la discussion a sa largeur : le message se place
            chat_->revealMessage(msg, t.kind == "carte");
        }
        return true;
    }
    if (t.kind == "piece" || t.kind == "modele") {
        std::string conv;
        const Attachment* a = assistant_->findAttachment(t.id, &conv);
        if (!a) {
            say("Assistant IA : cette pi\xC3\xA8" "ce jointe n'est plus dans les conversations (supprim\xC3\xA9" "e avec la sienne).");
            return false;
        }
        const std::string name = a->name;
        showConversation();
        if (!conv.empty() && (!assistant_->current() || assistant_->current()->id != conv)) (void)selectConversation(assistant_->indexOf(conv));
        sync();   // le contexte de la discussion de cette conversation
        if (t.kind == "modele") {
            contextAction(5, t.id);
            return true;
        }
        if (ctxPane_) {
            ctxPane_->setExpanded(true);
            if (ctxPane_->context().files.size() >= 3) ctxPane_->setSearch(name);   // la recherche n'est montree qu'a partir de trois
        }
        invalidateLayout();
        say("Assistant IA : \xC2\xAB " + name + " \xC2\xBB est dans le contexte de la discussion, en haut de la conversation.");
        return true;
    }
    if (t.kind == "souvenirs") {
        if (ui_.openSouvenirs) {
            ui_.openSouvenirs(t.id);
            return true;
        }
        if (ui_.openMemory && (!ui_.memoryAvailable || ui_.memoryAvailable())) {
            ui_.openMemory();
            return true;
        }
        say("Assistant IA : la m\xC3\xA9moire de l'IA (Souvenirs) n'est pas disponible ici.");
        return false;
    }
    if (t.kind == "scan") {
        if (ui_.showScanReport && ui_.showScanReport()) return true;
        say("Assistant IA : le compte rendu du scan n'est plus ouvert (Communication \xE2\x80\xBA Scanner IP le refait).");
        return false;
    }
    if (t.kind == "incident") {
        if (ui_.openIncident && ui_.openIncident(t.id)) return true;
        say("Assistant IA : l'incident \xC2\xAB " + t.id + " \xC2\xBB est introuvable (valid\xC3\xA9 et d\xC3\xA9plac\xC3\xA9, ou supprim\xC3\xA9).");
        return false;
    }
    if (t.kind == "parametres") {
        openSettings();
        return true;
    }
    if (t.kind == "mission") {   // 1.22.0 : le tableau de bord de la mission (au centre)
        if (!assistant_->findMission(t.id)) {
            say("Assistant IA : cette mission n'existe plus (son fichier a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9).");
            return false;
        }
        return openMission(t.id, -1);
    }
    say("Assistant IA : ce lien n'est pas connu de cette version.");
    return false;
}

// ---- 1.22.0 : les missions IA -------------------------------------------------------------------
namespace {
std::string quoted(const std::string& name) { return "\xC2\xAB " + name + " \xC2\xBB"; }
} // namespace

void AiPanel::showMissions(bool on) {
    if (on == missionsShown_) return;
    if (on) {
        cancelRename();
        notifsShown_ = false;   // un onglet a la fois
    }
    missionsShown_ = on;
    syncMissions();
    if (on && missionsView_) {
        missionsView_->refresh();
        missionsView_->takeFocus();
    }
    invalidateLayout();
    invalidate();
    if (on && missionsView_) {
        const std::size_t open = missionsView_->count(AiMissionsView::Filter::Open);
        say("Missions IA : " + (open == 0 ? std::string("aucune en cours") : std::to_string(open) + " en cours")
            + (missionsBadge_ > 0 ? ", " + std::to_string(missionsBadge_) + (missionsBadge_ > 1 ? " attendent" : " attend") + " une d\xC3\xA9" "cision." : std::string(".")));
    }
}

void AiPanel::showConversation() {
    showMissions(false);
    showNotifications(false);
}

void AiPanel::syncMissions() {
    if (!missionsView_ || !missionStrip_) return;
    (void)missionsView_->poll();
    const std::size_t before = missionsBadge_;
    const bool alertBefore = missionsAlert_;
    missionsBadge_ = missionsView_->attentionCount();
    missionsAlert_ = missionsView_->attentionAlert();
    if (before != missionsBadge_ || alertBefore != missionsAlert_) {
        invalidateLayout();   // la largeur de l'onglet
        invalidate();
    }
    if (missionStrip_->poll()) invalidateLayout();
    if (!notifsShown_ && !missionsShown_) {
        const float chatTop = (listOpen_ ? list_.bottom() : listHead_.bottom()) + 1.f;
        const float want = missionStrip_->heightFor(bounds().w, std::max(0.f, (chatBottom_ - chatTop) * 0.30f));
        if (std::fabs(want - missionStripH_) > 0.5f) invalidateLayout();
    }
}

bool AiPanel::missionBusy(const mission::Mission& m) const {
    if (assistant_->auditing(m.id)) return true;
    if (!assistant_->generating()) return false;
    const std::string c = assistant_->generatingConversationId();
    if (c.empty()) return false;
    return c == m.conversation || std::find(m.conversations.begin(), m.conversations.end(), c) != m.conversations.end();
}

std::optional<bool> AiPanel::missionCard(const std::string& missionId, const std::string& proposalId) const {
    const auto m = missionCards_.find(missionId);
    if (m == missionCards_.end()) return std::nullopt;
    const auto p = m->second.find(proposalId);
    if (p == m->second.end()) return std::nullopt;
    return p->second;
}

void AiPanel::setMissionCard(const std::string& missionId, const std::string& proposalId, bool open) { missionCards_[missionId][proposalId] = open; }

void AiPanel::offerMission(const std::string& text) {
    askText_ = text;
    missionAsk_->setRequest(text);
    invalidateLayout();
    invalidate();
    say("Assistant IA : cette demande peut devenir une mission IA - choisis au-dessus de la saisie (rien n'est encore parti).");
}

void AiPanel::dismissMissionAsk() {
    if (askText_.empty()) return;
    askText_.clear();
    askMessage_ = -1;   // 1.22.6
    missionAsk_->setRequest({});
    invalidateLayout();
    invalidate();
}

bool AiPanel::chooseMission(bool create) {
    if (askText_.empty()) return false;
    const std::string text = askText_;   // une copie : le choix ferme la carte
    // 1.22.6 : LE CONSEIL - celui d'une reponse (le message de l'utilisateur qu'il vise), ou celui d'avant l'envoi.
    if (missionAsk_->advice()) {
        const int message = askMessage_;
        if (create) {
            if (message >= 0) return createMissionFromMessage(message);
            std::string why;
            cancelRename();
            if (ui_.licenceGate && !ui_.licenceGate("ia.mission")) return false;   // 1.39.0
            if (!assistant_->sendAsMission(text, &why)) {
                say(why.empty() ? std::string("Mission IA : elle n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e.") : "Mission IA : " + why);
                return false;
            }
            if (prompt_->text() == text) prompt_->setText({});
            dismissMissionAsk();
            watchingReply_ = assistant_->generatingConversationId();
            showConversation();
            seen_ = 0;
            sync();
            syncMissions();
            chat_->scrollToBottom();
            say("Mission IA cr\xC3\xA9\xC3\xA9" "e : l'IA analyse le projet et pr\xC3\xA9pare le plan (rien n'est modifi\xC3\xA9).");
            return true;
        }
        assistant_->declineMissionAdvice();   // << Continuer ici >> : retenu pour cette conversation
        dismissMissionAsk();
        if (message >= 0) {
            say("D'accord : la conversation continue ici (elle ne te propose plus de mission).");
            return true;
        }
        if (!sendText(text)) return false;
        if (prompt_->text() == text) prompt_->setText({});
        return true;
    }
    if (create) {
        if (!createMission(text)) return false;
        if (prompt_->text() == text) prompt_->setText({});
        return true;
    }
    dismissMissionAsk();
    if (!sendText(text)) return false;   // l'envoi normal (le budget, l'avis de fin de reponse...)
    if (prompt_->text() == text) prompt_->setText({});
    return true;
}

bool AiPanel::createMission(const std::string& request) {
    cancelRename();
    std::string why;
    if (!assistant_->createMission(request, &why)) {
        say(why.empty() ? std::string("Mission IA : elle n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e.") : "Mission IA : " + why);
        return false;
    }
    dismissMissionAsk();
    watchingReply_ = assistant_->generatingConversationId();   // l'avis de fin de reponse, comme un envoi
    showConversation();
    seen_ = 0;
    sync();
    syncMissions();
    chat_->scrollToBottom();
    say("Mission IA cr\xC3\xA9\xC3\xA9" "e : l'IA analyse le projet et pr\xC3\xA9pare le plan (rien n'est modifi\xC3\xA9). Son tableau de bord : l'onglet Missions IA, "
        "ou un clic sur la bande \xC2\xAB Mission IA \xC2\xBB.");
    return true;
}

// ---- 1.22.6 : DE LA CONVERSATION A LA MISSION IA ----------------------------------------------------------------------
bool AiPanel::missionAdviceShown() const noexcept { return !askText_.empty() && missionAsk_->advice(); }

bool AiPanel::createMissionFromMessage(int message) {
    cancelRename();
    std::string why;
    if (!assistant_->createMissionFromMessage(message, &why)) {
        say(why.empty() ? std::string("Mission IA : elle n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e.") : "Mission IA : " + why);
        return false;
    }
    dismissMissionAsk();
    watchingReply_ = assistant_->generatingConversationId();
    showConversation();
    seen_ = 0;
    sync();
    syncMissions();
    chat_->scrollToBottom();
    say("Mission IA cr\xC3\xA9\xC3\xA9" "e avec ce message, dans une conversation neuve : l'IA analyse le projet et pr\xC3\xA9pare le plan (rien n'est "
        "modifi\xC3\xA9). La conversation d'origine garde le lien vers son tableau de bord.");
    return true;
}

bool AiPanel::sendAsMission() {
    const std::string text = prompt_->text();
    if (assistant_->generating()) {
        say("Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attends-la, ou arr\xC3\xAAte-la.");
        return false;
    }
    cancelRename();
    if (ui_.licenceGate && !ui_.licenceGate("ia.mission")) return false;   // 1.39.0 : pas dans la licence - sa fenetre
    std::string why;
    if (!assistant_->sendAsMission(text, &why)) {
        say(why.empty() ? std::string("Mission IA : elle n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e.") : "Mission IA : " + why);
        return false;
    }
    prompt_->setText({});
    dismissMissionAsk();
    watchingReply_ = assistant_->generatingConversationId();
    showConversation();
    seen_ = 0;
    sync();
    syncMissions();
    chat_->scrollToBottom();
    say("Mission IA cr\xC3\xA9\xC3\xA9" "e : l'IA analyse le projet et pr\xC3\xA9pare le plan (rien n'est modifi\xC3\xA9). Son tableau de bord : l'onglet "
        "Missions IA.");
    return true;
}

bool AiPanel::sendOrAdvise(const std::string& text) {
    if (!assistant_->generating())
        if (Assistant::MissionAdvice adv; assistant_->adviseMission(text, &adv)) {
            offerAdvice(text, adv.reason, -1);
            return false;
        }
    return sendText(text);
}

void AiPanel::offerAdvice(const std::string& text, const std::string& reason, int message) {
    askText_ = text;
    askMessage_ = message;
    missionAsk_->setAdvice(text, reason);
    invalidateLayout();
    invalidate();
    say("Assistant IA : cette demande est grosse - elle peut passer en mission IA (choisis au-dessus de la saisie ; rien n'est cr\xC3\xA9\xC3\xA9).");
}

void AiPanel::syncAdvice() {
    if (!askText_.empty() && askMessage_ < 0) return;   // le choix d'un envoi attend : il passe d'abord
    const auto adv = assistant_->pendingAdvice();
    const Conversation* c = assistant_->current();
    if (!adv || !c || adv->message < 0 || adv->message >= static_cast<int>(c->messages.size())) {
        if (!askText_.empty() && askMessage_ >= 0) dismissMissionAsk();   // traite, ou une autre conversation
        return;
    }
    const std::string key = c->id + "#" + std::to_string(adv->message);
    if (key == adviceSeen_) return;   // deja montre (ou ferme par la croix) : il ne revient pas
    adviceSeen_ = key;
    offerAdvice(c->messages[static_cast<std::size_t>(adv->message)].text, adv->reason, adv->message);
}

bool AiPanel::openMission(const std::string& missionId, int mode) {
    const mission::Mission* m = assistant_->findMission(missionId);
    if (!m) {
        say("Assistant IA : cette mission n'existe plus (son fichier a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9).");
        return false;
    }
    // -1 : le tableau de bord, ou l'ecran de reprise si la mission en a besoin (en pause, un appel interrompu, le projet
    // modifie ou introuvable a sa derniere verification).
    if (mode < 0) mode = missionui::wantsResume(*m, m->coherence, missionBusy(*m)) ? 1 : 0;
    if (ui_.openMission) {
        if (ui_.openMission(missionId, mode)) return true;
        say("Assistant IA : le tableau de bord de la mission n'a pas pu s'ouvrir (un projet IHM est-il ouvert ?).");
        return false;
    }
    // Sans hote (un essai) : l'onglet Missions IA, la mission choisie.
    showMissions(true);
    return missionsView_->selectMission(missionId);
}

bool AiPanel::missionAct(const std::string& missionId, MissionAct act, const std::string& step) {
    const mission::Mission* m = assistant_->findMission(missionId);
    if (!m) {
        say("Assistant IA : cette mission n'existe plus (son fichier a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9).");
        return false;
    }
    const std::string name = missionui::title(*m);   // une copie : la mission change sous le geste
    m = nullptr;
    std::string why;
    bool ok = false;
    switch (act) {
        case MissionAct::Continue:
        case MissionAct::Correct:
        case MissionAct::Refresh: {
            ok = act == MissionAct::Continue  ? assistant_->continueMission(missionId, &why)
                 : act == MissionAct::Correct ? assistant_->correctStep(missionId, step, &why)
                                              : assistant_->refreshMission(missionId, &why);
            if (!ok) break;
            // la reponse s'ecrit dans la conversation de la mission (rouverte) : elle est montree
            watchingReply_ = assistant_->generatingConversationId();
            showConversation();
            if (ui_.showPanel) ui_.showPanel();
            seen_ = 0;
            sync();
            chat_->scrollToBottom();
            say(act == MissionAct::Continue
                    ? "Mission " + quoted(name) + " : la suite est demand\xC3\xA9" "e \xC3\xA0 l'IA, avec l'\xC3\xA9tat r\xC3\xA9" "el du projet (une carte \xC3\xA0 v\xC3\xA9rifier et appliquer)."
                : act == MissionAct::Correct
                    ? "Mission " + quoted(name) + " : une correction de " + step + " est demand\xC3\xA9" "e \xC3\xA0 l'IA (une nouvelle proposition, \xC3\xA0 valider)."
                    : "Mission " + quoted(name) + " : l'IA relit le projet et revoit le plan (les \xC3\xA9tapes termin\xC3\xA9" "es sont gard\xC3\xA9" "es).");
            break;
        }
        case MissionAct::Audit:
            ok = assistant_->auditMission(missionId, &why);
            if (ok) say("Mission " + quoted(name) + " : l'audit final tourne sur un fil de fond (une copie du projet) ; le rapport arrive dans le tableau de bord.");
            break;
        case MissionAct::Pause:
            ok = assistant_->pauseMission(missionId, &why);
            if (ok) say("Mission " + quoted(name) + " en pause : plus rien ne s'applique jusqu'\xC3\xA0 Reprendre.");
            break;
        case MissionAct::Resume:
            ok = assistant_->resumeMission(missionId, &why);
            if (ok) {
                const mission::Mission* now = assistant_->findMission(missionId);
                say("Mission " + quoted(name) + " reprise" + (now ? " : " + mission::nextAction(*now) : std::string(".")));
            }
            break;
        case MissionAct::Cancel: {
            const auto run = [this, missionId, name] {
                std::string w;
                if (assistant_->cancelMission(missionId, &w))
                    say("Mission " + quoted(name) + " annul\xC3\xA9" "e : ses propositions ne s'appliqueront plus ; son historique reste consultable.");
                else say("Mission " + quoted(name) + " : " + (w.empty() ? std::string("elle n'a pas pu \xC3\xAAtre annul\xC3\xA9" "e.") : w));
                syncMissions();
            };
            if (!ui_.showDialog) {   // sans hote (un essai) : on annule
                run();
                return true;
            }
            HmiAskDialog::Spec spec;
            spec.id = "dialog.ai.mission.annuler";
            spec.title = "Annuler la mission";
            spec.text = "Annuler la mission " + quoted(name) + " ?\nSes propositions ne s'appliqueront plus et ses \xC3\xA9tapes non finies passent \xC3\xA0 "
                        "\xC2\xAB annul\xC3\xA9" "e \xC2\xBB. Ce qui est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9 reste dans le projet (Ctrl+Z le reprend comme d'habitude). "
                        "Son historique est gard\xC3\xA9 : elle reste consultable.";
            spec.confirm = "Annuler la mission";
            spec.cancel = "Garder";
            spec.danger = true;
            spec.width = 560.f;
            ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
                if (r.accepted()) run();
            });
            return false;   // la reponse viendra
        }
        case MissionAct::ForceFinish: {
            // 1.22.2 : refusee tout de suite, et pourquoi (terminee, annulee, en lecture seule, une application de fond)
            if (!assistant_->canForceFinish(missionId, &why)) break;
            const auto run = [this, missionId, name] {
                std::string w;
                if (assistant_->forceFinishMission(missionId, &w)) {
                    const mission::Mission* now = assistant_->findMission(missionId);
                    const std::size_t n = now ? now->forced.steps.size() : 0;
                    say("Mission " + quoted(name) + " termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : "
                        + (n == 0 ? std::string("aucune \xC3\xA9tape \xC3\xA0 ignorer") : missionui::count(static_cast<int>(n), "\xC3\xA9tape ignor\xC3\xA9" "e", "\xC3\xA9tapes ignor\xC3\xA9" "es"))
                        + ", rien n'est d\xC3\xA9" "fait dans le projet ; "
                        + (assistant_->auditing(missionId) ? std::string("l'audit final tourne en arri\xC3\xA8re-plan (le rapport suit).")
                                                           : std::string("le rapport final : l'audit final, sur le projet de la mission.")));
                } else {
                    say("Mission " + quoted(name) + " : " + (w.empty() ? std::string("sa fin n'a pas pu \xC3\xAAtre forc\xC3\xA9" "e.") : w));
                }
                syncMissions();
            };
            if (!ui_.showDialog) {   // sans hote (un essai) : on force
                run();
                return true;
            }
            const mission::Mission* mm = assistant_->findMission(missionId);
            HmiAskDialog::Spec spec;
            spec.id = "dialog.ai.mission.fin-forcee";
            spec.title = "Forcer la fin de la mission";
            spec.text = mm ? missionui::forceFinishQuestion(*mm, missionui::liveOf(*assistant_, *mm).calling) : "Forcer la fin de la mission " + quoted(name) + " ?";
            spec.confirm = "Forcer la fin";
            spec.cancel = "Garder";
            spec.danger = true;
            spec.width = 580.f;
            ui_.showDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [run](const menu::DialogResult& r) {
                if (r.accepted()) run();
            });
            return false;   // la reponse viendra
        }
        case MissionAct::Archive:
        case MissionAct::Unarchive:
            ok = assistant_->archiveMission(missionId, act == MissionAct::Archive, &why);
            if (ok)
                say(act == MissionAct::Archive ? "Mission " + quoted(name) + " archiv\xC3\xA9" "e : rien n'est supprim\xC3\xA9 (le filtre Archiv\xC3\xA9" "es la montre, D\xC3\xA9sarchiver la remet)."
                                               : "Mission " + quoted(name) + " d\xC3\xA9sarchiv\xC3\xA9" "e.");
            break;
        case MissionAct::Conversation:
            ok = assistant_->openMissionConversation(missionId);
            if (!ok) {
                why = "sa conversation n'existe plus (elle a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9" "e) : le tableau de bord garde l'historique de la mission.";
                break;
            }
            showConversation();
            if (ui_.showPanel) ui_.showPanel();
            seen_ = 0;
            sync();
            chat_->showLastExchange();
            say("La conversation de la mission " + quoted(name) + " est ouverte.");
            break;
    }
    if (!ok && !why.empty()) say("Mission " + quoted(name) + " : " + why);
    syncMissions();
    return ok;
}

bool AiPanel::renameMission(const std::string& missionId, const std::string& name) {
    std::string why;
    if (!assistant_->renameMission(missionId, name, &why)) {
        say(why.empty() ? std::string("La mission n'a pas pu \xC3\xAAtre renomm\xC3\xA9" "e.") : "Mission IA : " + why);
        return false;
    }
    say("Mission renomm\xC3\xA9" "e : " + name);
    syncMissions();
    return true;
}

bool AiPanel::missionProposal(const std::string& missionId, const std::string& proposalId, int what) {
    const mission::Mission* m = assistant_->findMission(missionId);
    const mission::ProposalLink* link = m ? m->proposal(proposalId) : nullptr;
    if (!link) {
        say("Assistant IA : cette proposition n'est plus dans la mission.");
        return false;
    }
    const std::string tag = "P" + std::to_string(link->order), conversation = link->conversation;   // des copies
    std::string why;
    if (what == 2 && !mission::mayApply(*m, proposalId, &why)) {
        say(tag + " : " + why);
        return false;
    }
    m = nullptr;
    link = nullptr;
    // la conversation de la mission (rouverte), puis celle qui porte la proposition si c'en est une autre
    (void)assistant_->openMissionConversation(missionId);
    int msg = assistant_->messageOfProposal(proposalId);
    if (msg < 0 && !conversation.empty())
        if (assistant_->indexOf(conversation) >= 0) {
            (void)assistant_->select(conversation);
            msg = assistant_->messageOfProposal(proposalId);
        }
    seen_ = 0;
    sync();
    if (msg < 0) {
        say(tag + " : sa conversation n'existe plus (supprim\xC3\xA9" "e) ; son compte rendu reste dans le tableau de bord de la mission.");
        return false;
    }
    switch (what) {
        case 0:
            openPreview(msg);   // la fenetre d'apercu (une proposition qui n'est plus a decider : la barre d'etat le dit)
            return true;
        case 2:
            showConversation();
            return applyProposal(msg);   // les confirmations de la carte (suppressions, erreurs)
        default:
            showConversation();
            if (ui_.showPanel) ui_.showPanel();
            layout();   // la discussion a sa largeur : la carte se place
            chat_->revealMessage(msg, true);
            say(tag + " : sa carte, dans la conversation de la mission (le plan d\xC3\xA9pli\xC3\xA9).");
            return true;
    }
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
    XPG_MESURE("ia.panneau.mise-en-page");   // 1.22.2
    const auto b = bounds();
    // Le menu du clic droit : le cadre du panneau (ferme, il ne prend aucun clic ; ouvert, il deborde).
    menu_->setBounds(b);
    const float x = b.x, w = b.w;
    header_ = {x, b.y, w, kHeaderH};
    const float bs = 28.f;
    closeBtn_->setBounds({b.right() - kPad - bs, b.y + (kHeaderH - bs) / 2.f, bs, bs});
    settingsBtn_->setBounds({closeBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    newBtn_->setBounds({settingsBtn_->bounds().x - bs - 2.f, closeBtn_->bounds().y, bs, bs});
    // 1.21.2 : LES ONGLETS sous l'en-tete - Conversation | Notifications (la cloche, la pastille des non lues).
    // 1.22.0 : | Missions IA (sa pastille : celles qui attendent une decision). Etroit (1280 x 800) : Notifications, puis
    // Missions IA, ne gardent que leur icone et leur pastille (le libelle : l'infobulle).
    {
        tabs_ = {x, header_.bottom(), w, kTabsH};
        const float convW = ui::measureWidth("Conversation", kTab) + 24.f;
        tabConv_ = {x + kPad - 4.f, tabs_.y, convW, kTabsH};
        const auto pillW = [](std::size_t n) {
            const std::string count = n > 99 ? std::string("99+") : std::to_string(n);
            return n > 0 ? std::max(18.f, ui::measureWidth(count, kMeta) + 10.f) + 6.f : 0.f;
        };
        const float nBadge = pillW(badge_), mBadge = pillW(missionsBadge_);
        const float notifFull = 12.f + 16.f + 6.f + ui::measureWidth("Notifications", kTab) + nBadge + 12.f;
        const float notifShort = 12.f + 16.f + (nBadge > 0.f ? nBadge : 0.f) + 12.f;
        const float missFull = 12.f + 16.f + 6.f + ui::measureWidth("Missions IA", kTab) + mBadge + 12.f;
        const float missShort = 12.f + 16.f + (mBadge > 0.f ? mBadge : 0.f) + 12.f;
        const float room = b.right() - kPad - (tabConv_.right() + 2.f);
        notifsCompact_ = notifFull + 2.f + missFull > room;
        missionsCompact_ = notifsCompact_ && notifShort + 2.f + missFull > room;
        float nw = notifsCompact_ ? notifShort : notifFull;
        float mw = missionsCompact_ ? missShort : missFull;
        nw = std::min(nw, std::max(30.f, room - mw - 2.f));
        tabNotifs_ = {tabConv_.right() + 2.f, tabs_.y, nw, kTabsH};
        mw = std::min(mw, std::max(30.f, b.right() - kPad - (tabNotifs_.right() + 2.f)));
        tabMissions_ = {tabNotifs_.right() + 2.f, tabs_.y, mw, kTabsH};
    }
    // Les onglets Notifications et Missions IA : leur liste prend toute la place sous les onglets ; la conversation se
    // cache (rien n'est perdu).
    {
        const bool notes = notifsShown_, miss = missionsShown_ && !notifsShown_, other = notes || miss;
        notifView_->setVisibility(notes ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        missionsView_->setVisibility(miss ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        for (ui::Widget* wgt : std::initializer_list<ui::Widget*>{chat_, prompt_, attachBtn_, sendBtn_, modePreview_, modeDirect_})
            wgt->setVisibility(other ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        // 1.22.6 : Envoyer en mission - avec une IA connectee seulement (une mission travaille avec elle)
        missionSendBtn_->setVisibility(other || !assistant_->live() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        if (other) {
            replyBar_->setVisibility(ui::Visibility::Collapsed);   // 1.50.1
            strip_->setVisibility(ui::Visibility::Collapsed);
            if (ctxPane_) ctxPane_->setVisibility(ui::Visibility::Collapsed);
            missionStrip_->setVisibility(ui::Visibility::Collapsed);
            missionAsk_->setVisibility(ui::Visibility::Collapsed);
            statusLine_ = limitLine_ = modeRow_ = listHead_ = list_ = pendingArea_ = promptArea_ = bottomBar_ = outgoing_ = gfx::Rect{};
            rows_.assign(assistant_->conversations().size(), gfx::Rect{});
            pendingRemove_.clear();
            outgoingLines_.clear();
            stripH_ = ctxH_ = missionStripH_ = askH_ = 0.f;
            const gfx::Rect area{x, tabs_.bottom(), w, std::max(40.f, b.bottom() - tabs_.bottom())};
            if (notes) notifView_->setBounds(area);
            else missionsView_->setBounds(area);
            return;
        }
    }
    statusLine_ = {x, tabs_.bottom(), w, kStatusH};
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
    missionSendBtn_->setBounds({sendBtn_->bounds().x - 36.f, bottomBar_.y + 5.f, 32.f, kBottomH - 10.f});   // 1.22.6
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
    // 1.50.1 : << En reponse a ... >> juste au-dessus de la saisie (la demande citera ce message).
    float aboveY = promptArea_.y;
    if (assistant_->replyTo()) {
        replyBar_->setBounds({x + kPad, promptArea_.y - sessionui::ReplyBar::kHeight - 4.f, w - 2 * kPad, sessionui::ReplyBar::kHeight});
        replyBar_->setVisibility(ui::Visibility::Visible);
        aboveY = replyBar_->bounds().y;
    } else {
        replyBar_->setVisibility(ui::Visibility::Collapsed);
    }
    const float pendH = static_cast<float>(assistant_->pending().size()) * kPendingH;
    pendingArea_ = {x + kPad, aboveY - pendH - (pendH > 0 ? 6.f : 0.f), w - 2 * kPad, pendH};
    // 1.22.0 : << Creer une mission IA ? >> - au-dessus des pieces en attente et de la saisie (rien n'est encore parti).
    askH_ = 0.f;
    if (!askText_.empty()) {
        askH_ = missionAsk_->heightFor(w - 2 * kPad);
        missionAsk_->setBounds({x + kPad, pendingArea_.y - askH_ - 6.f, w - 2 * kPad, askH_});
        missionAsk_->setVisibility(ui::Visibility::Visible);
    } else {
        missionAsk_->setVisibility(ui::Visibility::Collapsed);
    }
    const float chatTop = (listOpen_ ? list_.bottom() : listHead_.bottom()) + 1.f;
    const float chatBottom = (askH_ > 0.f ? missionAsk_->bounds().y : pendingArea_.y) - 6.f;
    chatBottom_ = chatBottom;
    // 1.22.0 : LA MISSION ACTIVE, tout en haut (la mission de la conversation ouverte) ; au plus 30 % de la place.
    missionStripH_ = missionStrip_->heightFor(w, std::max(0.f, (chatBottom - chatTop) * 0.30f));
    missionStrip_->setBounds({x, chatTop, w, missionStripH_});
    missionStrip_->setVisibility(missionStripH_ > 0.f ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    const float ctxTop = chatTop + missionStripH_;
    // 1.21.0 : le contexte de la discussion, tout en haut (seulement s'il y a des fichiers joints) ; au plus 35 % de la place.
    ctxH_ = ctxPane_ && !ctxPane_->context().files.empty() ? ctxPane_->heightFor(w, std::max(28.f, (chatBottom - ctxTop) * 0.35f)) : 0.f;
    if (ctxPane_) {
        ctxPane_->setBounds({x, ctxTop, w, ctxH_});
        ctxPane_->setVisibility(ctxH_ > 0.f ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    }
    const float planTop = ctxTop + ctxH_;
    // 1.20.2 : le plan en cours, au-dessus de la discussion (il ne defile pas avec elle) ; au plus 45 % de la place.
    stripH_ = strip_->heightFor(w, std::max(0.f, (chatBottom - planTop) * 0.45f));
    strip_->setBounds({x, planTop, w, stripH_});
    strip_->setVisibility(stripH_ > 0.f ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    chat_->setBounds({x, planTop + stripH_, w, std::max(40.f, chatBottom - planTop - stripH_)});
}

void AiPanel::onPaint(const ui::PaintContext& ctx) {
    XPG_MESURE("ia.peinture.panneau");   // 1.22.2
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
    // ---- 1.21.2 : les onglets Conversation | Notifications (la pastille des non lues : rouge s'il y a une erreur)
    {
        ctx.r.fillRect({tabs_.x + 1.f, tabs_.y, tabs_.w - 1.f, tabs_.h}, c.headerBg);
        ctx.r.fillRect({tabs_.x + 1.f, tabs_.bottom() - 1.f, tabs_.w - 1.f, 1.f}, c.border);
        const float ty = tabs_.y + (tabs_.h - ctx.r.lineHeight(kTab)) / 2.f - 1.f;
        const auto tab = [&](const gfx::Rect& r, bool on, bool hot) {
            if (hot && !on) ctx.r.fillRoundedRect({r.x, r.y + 3.f, r.w, r.h - 6.f}, ctx.theme.brand.hover, 4.f);
            if (on) ctx.r.fillRect({r.x + 4.f, r.bottom() - 3.f, r.w - 8.f, 2.f}, c.accent);
        };
        const bool convOn = !notifsShown_ && !missionsShown_;
        tab(tabConv_, convOn, hoverTab_ == 0);
        const auto convInk = convOn ? c.text : c.textMuted;
        if (convOn) bold(ctx.r, {tabConv_.x + 12.f, ty}, "Conversation", kTab, convInk);
        else ctx.r.drawText({tabConv_.x + 12.f, ty}, "Conversation", kTab, convInk);
        tab(tabNotifs_, notifsShown_, hoverTab_ == 1);
        const auto ink = notifsShown_ ? c.text : c.textMuted;
        ctx.r.pushClip(tabNotifs_);
        AiNotificationsView::drawBell(ctx.r, {tabNotifs_.x + 12.f, tabNotifs_.y + (tabNotifs_.h - 16.f) / 2.f - 1.f, 16.f, 16.f},
                                      badge_ > 0 ? (badgeError_ ? c.error : c.accent) : ink);
        const float lx = tabNotifs_.x + 12.f + 16.f + 6.f;
        if (!notifsCompact_) {   // 1.22.0 : etroit - la cloche et la pastille seules
            if (notifsShown_) bold(ctx.r, {lx, ty}, "Notifications", kTab, ink);
            else ctx.r.drawText({lx, ty}, "Notifications", kTab, ink);
        }
        if (badge_ > 0) {
            const std::string n = badge_ > 99 ? std::string("99+") : std::to_string(badge_);
            const float bw = std::max(18.f, ctx.r.measure(n, kMeta).width + 10.f);
            const float px = notifsCompact_ ? lx - 2.f : lx + ctx.r.measure("Notifications", kTab).width + 6.f;
            const gfx::Rect pill{px, tabNotifs_.y + (tabNotifs_.h - 17.f) / 2.f - 1.f, bw, 17.f};
            const gfx::Color fill = badgeError_ ? c.error : c.accent;
            ctx.r.fillRoundedRect(pill, fill, 8.5f);
            ctx.r.drawText({pill.x + (pill.w - ctx.r.measure(n, kMeta).width) / 2.f, pill.y + (pill.h - ctx.r.lineHeight(kMeta)) / 2.f}, n, kMeta,
                           ctx.theme.onSurface(fill));
        }
        ctx.r.popClip();
        tips_.emplace_back(tabConv_, "La conversation : les conversations, la discussion, la saisie");
        std::string tip = badge_ > 0 ? std::to_string(badge_) + (badge_ > 1 ? " notifications non lues" : " notification non lue")
                                     : std::string("Aucune notification non lue");
        if (badgeError_) tip += " (dont une erreur)";
        if (!latest_.empty()) tip += "\nLa plus r\xC3\xA9" "cente : " + latest_;
        tip += "\nCe que l'Assistant a fait ou appris pendant que tu regardais ailleurs ; un clic l'ouvre, un autre revient \xC3\xA0 la conversation.";
        if (notifsCompact_) tip = "Notifications : " + tip;
        tips_.emplace_back(tabNotifs_, tip);
        // ---- 1.22.0 : l'onglet Missions IA (sa pastille : celles qui attendent une decision ; rouge : une erreur, un blocage)
        tab(tabMissions_, missionsShown_, hoverTab_ == 2);
        const auto mink = missionsShown_ ? c.text : c.textMuted;
        ctx.r.pushClip(tabMissions_);
        ui::drawIcon(ctx.r, ui::Icon::Task, {tabMissions_.x + 12.f, tabMissions_.y + (tabMissions_.h - 16.f) / 2.f - 1.f, 16.f, 16.f},
                     missionsBadge_ > 0 ? (missionsAlert_ ? c.error : c.accent) : mink);
        const float mx = tabMissions_.x + 12.f + 16.f + 6.f;
        if (!missionsCompact_) {
            if (missionsShown_) bold(ctx.r, {mx, ty}, "Missions IA", kTab, mink);
            else ctx.r.drawText({mx, ty}, "Missions IA", kTab, mink);
        }
        if (missionsBadge_ > 0) {
            const std::string n = missionsBadge_ > 99 ? std::string("99+") : std::to_string(missionsBadge_);
            const float bw = std::max(18.f, ctx.r.measure(n, kMeta).width + 10.f);
            const float px = missionsCompact_ ? mx - 2.f : mx + ctx.r.measure("Missions IA", kTab).width + 6.f;
            const gfx::Rect pill{px, tabMissions_.y + (tabMissions_.h - 17.f) / 2.f - 1.f, bw, 17.f};
            const gfx::Color fill = missionsAlert_ ? c.error : c.accent;
            ctx.r.fillRoundedRect(pill, fill, 8.5f);
            ctx.r.drawText({pill.x + (pill.w - ctx.r.measure(n, kMeta).width) / 2.f, pill.y + (pill.h - ctx.r.lineHeight(kMeta)) / 2.f}, n, kMeta,
                           ctx.theme.onSurface(fill));
        }
        ctx.r.popClip();
        std::string mtip = "Missions IA : les demandes complexes suivies (un plan d'\xC3\xA9tapes, des propositions successives, la reprise apr\xC3\xA8s une fermeture).";
        if (missionsBadge_ > 0)
            mtip += "\n" + std::to_string(missionsBadge_) + (missionsBadge_ > 1 ? " attendent" : " attend") + " une d\xC3\xA9" "cision (en pause, une proposition \xC3\xA0 valider, une erreur"
                    + (missionsAlert_ ? std::string(" - dont une erreur ou un blocage)") : std::string(")"));
        mtip += "\nUn clic les montre, un autre revient \xC3\xA0 la conversation.";
        tips_.emplace_back(tabMissions_, mtip);
    }
    if (notifsShown_ || missionsShown_) return;   // leurs listes se dessinent elles-memes (AiNotificationsView, AiMissionsView)
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
            // 1.24.1 : les conversations choisies a plusieurs (Ctrl+clic) se voient comme la choisie
            const bool sel = static_cast<int>(i) == a.currentIndex()
                          || std::find(chosenIds_.begin(), chosenIds_.end(), list[i].id) != chosenIds_.end();
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
    const float hw = (missionSendBtn_->visible() ? missionSendBtn_->bounds().x : sendBtn_->bounds().x) - 8.f - hx;   // 1.22.6
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
    // 1.22.6 : pendant une reponse, les compteurs bougent a chaque morceau recu : la bande est refaite quatre fois par seconde
    // au plus (l'onglet Performances mesure le panneau).
    if (assistant_->generating() && now >= bandAt_ && now - bandAt_ < activity::kRefresh) return;
    bandAt_ = now;
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
        const int t = tabConv_.contains(m->pos) ? 0 : tabNotifs_.contains(m->pos) ? 1 : tabMissions_.contains(m->pos) ? 2 : -1;   // 1.21.2, 1.22.0
        if (h != hoverRow_ || t != hoverTab_) {
            hoverRow_ = h;
            hoverTab_ = t;
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
        // 1.21.2 : les onglets - Notifications (un autre clic revient a la conversation), Conversation.
        if (d->button == ui::MouseButton::Left && tabNotifs_.contains(d->pos)) {
            showNotifications(!notifsShown_);
            return ui::EventResult::Consumed;
        }
        if (d->button == ui::MouseButton::Left && tabMissions_.contains(d->pos)) {   // 1.22.0
            showMissions(!missionsShown_);
            return ui::EventResult::Consumed;
        }
        if (d->button == ui::MouseButton::Left && tabConv_.contains(d->pos)) {
            showConversation();
            return ui::EventResult::Consumed;
        }
        if (tabs_.contains(d->pos)) return ui::EventResult::Consumed;
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
            // 1.24.1 : Ctrl+clic ajoute la conversation au choix (ou l'en retire) ; un clic droit dans le choix le garde.
            const auto chosen = chosenConversations();
            const bool inChoice = std::find(chosen.begin(), chosen.end(), row) != chosen.end();
            if (d->button == ui::MouseButton::Left && d->mods.ctrl && d->clickCount < 2) {
                toggleChosen(row);
                return ui::EventResult::Consumed;
            }
            if (d->button == ui::MouseButton::Right && chosen.size() > 1 && inChoice) {
                openRowMenu(row, d->pos);
                return ui::EventResult::Consumed;
            }
            chosenIds_.clear();
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
        if (k->key == ui::Key::Escape && !askText_.empty() && !notifsShown_ && !missionsShown_) {   // 1.22.0 : la carte de choix
            dismissMissionAsk();
            prompt_->takeFocus();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && assistant_->replyTo() && !notifsShown_ && !missionsShown_) {   // 1.50.1 : la reponse
            cancelReply();
            prompt_->takeFocus();
            return ui::EventResult::Consumed;
        }
        // La liste a le clavier (le panneau lui-meme) : F2, Suppr, les fleches. 1.21.2 : pas sous l'onglet Notifications.
        if (focused() && !notifsShown_ && !missionsShown_) {
            const int cur = assistant_->currentIndex();
            if (k->key == ui::Key::F2 && cur >= 0) { startRename(cur); return ui::EventResult::Consumed; }
            if (k->key == ui::Key::Delete && cur >= 0) { askDeleteChosen(); return ui::EventResult::Consumed; }   // 1.24.1 : une ou plusieurs
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
AiChatView::AiChatView(std::string id, AiPanel& panel) : ui::Widget(std::move(id)), panel_(panel) {
    setFocusPolicy(true);   // 1.50.1 : un message choisi (un clic) se copie par Ctrl+C
}

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

namespace {
// 1.20.2 : un bloc de code mis en page - l'en-tete (la langue, Copier), puis chaque ligne du code, coupee a la largeur
// sur un caractere entier (rien n'est perdu : le bouton copie le code exact, pas ce qui est montre), puis une marge.
void layoutCode(std::vector<AiChatView::Line>& out, const markdown::Segment& seg, int index, float width, bool first) {
    AiChatView::Line head;
    head.code = index;
    head.codeHead = true;
    head.codeOpen = !seg.closed;
    head.codeLang = markdown::languageLabel(seg.lang);
    head.codeText = seg.text;
    head.h = ui::lineHeight(kMeta) + 12.f;
    head.gapBefore = first ? 0.f : 8.f;
    out.push_back(std::move(head));
    const float lh = ui::lineHeight(kBody) + 2.f;
    const float room = std::max(40.f, width - 20.f);
    std::size_t from = 0;
    while (from <= seg.text.size()) {
        std::size_t nl = seg.text.find('\n', from);
        if (nl == std::string::npos) nl = seg.text.size();
        std::string raw;
        for (const char ch : std::string_view(seg.text).substr(from, nl - from)) {
            if (ch == '\t') raw += "    ";
            else if (ch != '\r') raw += ch;
        }
        from = nl + 1;
        bool firstPiece = true;
        while (firstPiece || !raw.empty()) {
            // la plus longue tete qui tient (sur une frontiere de caractere UTF-8), au moins un caractere
            std::vector<std::size_t> ends;
            for (std::size_t i = 1; i <= raw.size(); ++i)
                if (i == raw.size() || (static_cast<unsigned char>(raw[i]) >> 6) != 0x2) ends.push_back(i);
            std::size_t take = raw.size();
            if (!raw.empty() && ui::measureWidth(raw, kBody) > room) {
                std::size_t lo = 0, hi = ends.size() - 1;
                while (lo < hi) {
                    const std::size_t mid = (lo + hi + 1) / 2;
                    if (ui::measureWidth(raw.substr(0, ends[mid]), kBody) <= room) lo = mid;
                    else hi = mid - 1;
                }
                take = ends[lo];
            }
            AiChatView::Line line;
            line.code = index;
            line.h = lh;
            const std::string piece = raw.substr(0, take);
            line.runs.push_back({piece, firstPiece ? 10.f : 22.f, ui::measureWidth(piece, kBody), 5});
            out.push_back(std::move(line));
            raw = raw.substr(take);
            firstPiece = false;
        }
        if (nl >= seg.text.size()) break;
    }
    AiChatView::Line tail;   // la marge du bas du bloc
    tail.code = index;
    tail.h = 6.f;
    out.push_back(std::move(tail));
}
} // namespace

std::vector<AiChatView::Line> AiChatView::layoutText(const std::string& md, float width) {
    XPG_MESURE("ia.discussion.markdown");   // 1.22.2
    const std::string key = std::to_string(static_cast<int>(width)) + "|" + md;
    auto& cache = layoutCache();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    // 1.20.2 : les blocs de code a part ; le texte autour, comme avant.
    if (markdown::hasCode(md)) {
        std::vector<Line> out;
        int index = 0;
        bool first = true;
        for (const auto& seg : markdown::split(md)) {
            if (seg.kind == markdown::Segment::Kind::Code) {
                layoutCode(out, seg, index++, width, first);
            } else {
                auto part = layoutText(seg.text, width);
                if (!part.empty() && !first) part.front().gapBefore = std::max(part.front().gapBefore, 8.f);
                for (auto& l : part) out.push_back(std::move(l));
            }
            first = false;
        }
        cache.emplace(key, out);
        return out;
    }
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

namespace {
// 1.20.0 : une grosse proposition se verifie sur un fil de fond (la carte montre l'avancement). 1.22.1 : checkInBackground
// (AiOperations) - 40 operations et plus, ou une operation << adresser >> (le blocage du 01/11).
double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

const AiChatView::Thumb& AiChatView::thumb(int message) const {
    XPG_MESURE("ia.carte");   // 1.22.2
    auto& t = thumbs_[message];
    const auto& as = panel_.assistant();
    const Proposal* p = as.proposal(message);
    const auto doc = as.host().document ? as.host().document() : nullptr;
    std::string key;
    if (p) {
        std::string base = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
        for (const auto x : p->excluded) base += "x" + std::to_string(x);   // 1.19.0 : les etapes retirees
        base += fixes::key(*p);   // 1.22.1 : les corrections (un choix par variable introuvable) - la carte se revalide
        // 1.22.5 : l'avis du logiciel arrive (Generer et Compiler, une fois par proposition) - la carte le verse dans sa verification
        if (p->extra.isObject() && p->extra.has("logiciel")) base += "|logiciel";
        // 1.16.0 : l'empreinte de ce que la proposition touche (la vue visee, la vue neuve, les variables et les types).
        // 1.20.0 : relue seulement si le projet a change (sa revision) ou chaque seconde - pas a chaque peinture.
        if (doc) {
            const double now = steadySeconds();
            if (base != t.base || doc->revision != t.revision || now - t.at >= 1.0 || now < t.at) {
                XPG_MESURE("ia.carte.empreinte");   // 1.22.2
                t.print = proposalPrint(doc->project, *p);
                t.base = base;
                t.revision = doc->revision;
                t.at = now;
            }
            key = base + "|" + t.print;
        } else {
            key = base;
        }
    }
    if (t.job && t.job->key == key && !key.empty()) return t;   // deja en verification pour cette empreinte
    if (t.key == key && !key.empty()) {
        if (t.job) retiring_.push_back(std::move(t.job));          // une verification d'une empreinte depassee
        return t;
    }
    if (t.job) retiring_.push_back(std::move(t.job));
    // Une carte close (appliquee, refusee) : ni apercu ni validation - le plan garde a l'application.
    if (p && p->state != "proposee") {
        t.key = key;
        t.preview = Preview{};
        t.check = Check{};
        t.plan = p->state == "appliquee" ? as.planOf(message) : plan::Plan{};
        return t;
    }
    // 1.20.0 : une grosse proposition - sur un fil de fond, sur une copie du projet et un environnement fige.
    Env frozen;
    if (p && doc && checkInBackground(*p) && as.frozenEnvironment(frozen)) {
        auto job = std::make_shared<ThumbJob>();
        job->key = key;
        job->total = 2 * p->ops.size();   // la validation puis l'apercu
        auto project = [&] {
            XPG_MESURE("ia.copie-projet");   // 1.22.2 : la copie se fait sur ce fil
            return std::make_shared<const hmi::Project>(doc->project);
        }();
        job->worker = std::thread([job, project, prop = *p, env = std::move(frozen), verdict = Assistant::verdictOf(*p)] {
            XPG_TRAVAIL("ia.verification-carte");   // 1.22.2 : l'onglet Performances, travaux de fond
            setProgressCounter(&job->done);
            job->check = validate(*project, prop, env);
            if (verdict) build::merge(job->check, *verdict);   // 1.22.5 : l'avis du logiciel, garde avec elle
            job->preview = preview(*project, prop, env);
            job->plan = plan::build(*project, prop, job->check);
            setProgressCounter(nullptr);
            job->finished.store(true, std::memory_order_release);
        });
        t.job = std::move(job);
        return t;
    }
    t.key = key;
    XPG_MESURE("ia.carte.verification");   // 1.22.2 : une petite proposition, verifiee sur ce fil
    t.preview = as.previewOf(message);
    t.check = as.check(message);
    // 1.19.0 : le plan - celui de la validation (sans la refaire), ou celui garde a l'application.
    t.plan = p && doc ? plan::build(doc->project, *p, t.check) : as.planOf(message);
    return t;
}

std::optional<AiChatView::LivePlan> AiChatView::livePlan() const {
    XPG_MESURE("ia.plan-en-cours");   // 1.22.2
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    if (!conv) return std::nullopt;
    const int n = static_cast<int>(conv->messages.size());
    int found = -1;
    for (int i = n - 1; i >= 0 && found < 0; --i)   // d'abord celle qui travaille sur un fil de fond (1.22.5 : son processus aussi)
        if (conv->messages[static_cast<std::size_t>(i)].proposal && (a.applying(i) || checking(i) || a.processWorking(i))) found = i;
    for (int i = n - 1; i >= 0 && found < 0; --i)   // sinon la derniere proposition
        if (conv->messages[static_cast<std::size_t>(i)].proposal) found = i;
    if (found < 0) return std::nullopt;
    const Proposal* p = a.proposal(found);
    if (!p || p->state == "refusee") return std::nullopt;
    const Thumb& t = thumb(found);
    LivePlan lp;
    lp.message = found;
    lp.id = p->id;
    lp.plan = &t.plan;
    lp.applying = a.applying(found);
    lp.checking = !lp.applying && t.pending();
    lp.open = p->state == "proposee";
    lp.ops = p->ops.size();
    // 1.22.5 : le processus de developpement (sa derniere etape) ; il montre la bande tant qu'il travaille
    lp.working = a.processWorking(found);
    lp.process = a.processHeadline(found);
    if (!lp.applying && !lp.checking && !lp.working && !t.plan.worthShowing()) return std::nullopt;
    const auto applied = p->state == "appliquee" ? a.appliedState(found) : Assistant::Applied::NotApplied;
    lp.head = lp.checking ? "Plan d'action \xE2\x80\x94 " + std::to_string(lp.ops) + " op\xC3\xA9rations en v\xC3\xA9rification"
                          : planHeadOf(*p, t.plan);
    if (!lp.checking && !lp.applying && !t.plan.worthShowing())   // 1.22.5 : le processus seul (pas de plan a montrer)
        lp.head = (p->summary.empty() ? std::string("Proposition") : p->summary) + " \xE2\x80\x94 " + std::to_string(lp.ops) + " op\xC3\xA9rations";
    if (!lp.checking && p->state != "remplacee") lp.phases = phasesOf(*p, t.plan, applied, t.check.ok());
    if (lp.applying) {
        lp.status = "Application\xE2\x80\xA6";
        lp.progress = a.applyProgress();
    } else if (lp.checking) {
        lp.status = "V\xC3\xA9rification\xE2\x80\xA6";
        lp.progress = checkProgress(found);
    } else if (p->state == "remplacee") {   // 1.22.5 : remplacee par sa correction (l'IA la corrige, ou c'est fait)
        lp.status = lp.working ? "Correction\xE2\x80\xA6" : "Remplac\xC3\xA9" "e";
        lp.tone = lp.working ? 0 : 2;
    } else if (p->state == "proposee" && a.building(p->id)) {   // 1.22.5 : l'avis du logiciel, sur une copie
        lp.status = "G\xC3\xA9n\xC3\xA9rer et Compiler\xE2\x80\xA6";
    } else if (p->state == "appliquee" && applied == Assistant::Applied::Undone) {
        lp.status = "Annul\xC3\xA9 (Ctrl+Z)";
        lp.tone = 2;
    } else if (p->state == "appliquee") {
        const bool failed = t.plan.count(plan::State::Failed) > 0;
        lp.status = failed ? "Appliqu\xC3\xA9 avec des \xC3\xA9" "checs" : "Appliqu\xC3\xA9";
        lp.tone = failed ? 2 : 1;
    } else if (t.check.stale || t.check.viewMissing) {
        lp.status = "P\xC3\xA9rim\xC3\xA9";
        lp.tone = 2;
    } else if (!t.check.ok()) {
        lp.status = t.check.missingVariable.empty() ? "Non valide" : "\xC3\x80 compl\xC3\xA9ter";
        lp.tone = 3;
    } else {
        lp.status = "Validation requise";   // a toi de dire Appliquer (ou Refuser)
    }
    return lp;
}

void AiChatView::revealMessage(int message, bool openPlan) {
    const Proposal* p = panel_.assistant().proposal(message);
    if (openPlan && p) planOpen_.insert(p->id);
    measure();
    for (const auto& b : blocks_)
        if (b.message == message) {
            const float target = b.card.h > 0.f ? b.y + b.card.y + b.planY - 8.f : b.y;
            offset_ = std::clamp(target, 0.f, std::max(0.f, content_ - viewHeight()));
            break;
        }
    stick_ = atBottom();
    anchor_ = false;
    anchored_ = false;
    placeCards();
    invalidate();
}

bool AiChatView::pollThumbs() {
    bool changed = false;
    for (auto& [message, t] : thumbs_)
        if (t.job && t.job->finished.load(std::memory_order_acquire)) {
            if (t.job->worker.joinable()) t.job->worker.join();
            t.key = t.job->key;
            t.preview = std::move(t.job->preview);
            t.check = std::move(t.job->check);
            t.plan = std::move(t.job->plan);
            t.job.reset();
            changed = true;
        }
    std::erase_if(retiring_, [](const std::shared_ptr<ThumbJob>& j) {
        if (!j->finished.load(std::memory_order_acquire)) return false;
        if (j->worker.joinable()) j->worker.join();
        return true;
    });
    return changed;
}

bool AiChatView::resultReady(int message, Check* check, Preview* preview, plan::Plan* plan) const {
    const Thumb& t = thumb(message);
    if (t.pending()) return false;
    if (check) *check = t.check;
    if (preview) *preview = t.preview;
    if (plan) *plan = t.plan;
    return true;
}

bool AiChatView::checking(int message) const {
    const auto it = thumbs_.find(message);
    return it != thumbs_.end() && it->second.pending();
}

float AiChatView::checkProgress(int message) const {
    const auto it = thumbs_.find(message);
    if (it == thumbs_.end() || !it->second.job || it->second.job->total == 0) return 0.f;
    return std::min(1.f, static_cast<float>(it->second.job->done.load()) / static_cast<float>(it->second.job->total));
}

AiChatView::~AiChatView() {
    // Les verifications de fond finissent (elles ne touchent que leurs copies) : attendues ici, a la fermeture.
    for (auto& [message, t] : thumbs_)
        if (t.job && t.job->worker.joinable()) t.job->worker.join();
    for (auto& j : retiring_)
        if (j->worker.joinable()) j->worker.join();
}

void AiChatView::measure() {
    XPG_MESURE("ia.discussion.mesure");   // 1.22.2
    const float w = bounds().w;
    blocks_.clear();
    suggestionRects_.clear();
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    const float inner = std::max(80.f, w - 2 * kPad - 12.f);
    float y = 10.f;
    const int generating = a.generatingMessage();
    const float meta = ui::lineHeight(kMeta) + 4.f;
    // 1.22.6 : CE QUE FAIT L'IA (la ligne, son contexte, les etapes passees) et LE BILAN d'une reponse (sa ligne, son detail
    // deplie : une ligne par appel, coupee a la largeur - le nombre de lignes ne bouge pas quand les chiffres changent).
    const activity::Snapshot& act = activityNow(true);
    const auto placeActivity = [&](Block& b) {
        b.actY = b.h;
        b.actContext = !act.context.empty();
        b.actRows = std::clamp<std::size_t>(wrapPlain(act.line, kMeta, inner - 28.f).size(), 1, 2);
        b.actHistory = historyOpen_ ? act.history.size() : 0;
        b.h += (b.actContext ? kNoteH : 0.f) + kActRow + static_cast<float>(b.actRows - 1) * kNoteH + static_cast<float>(b.actHistory) * kNoteH + 4.f;
    };
    const auto placeBill = [&](Block& b, const activity::Bill& bill, bool liveOne) {
        b.billY = b.h + 2.f;
        b.billText = (liveOne ? std::string("Cette r\xC3\xA9ponse : ") : std::string{}) + activity::billLine(bill);
        b.h += kNoteH + 6.f;
        if (billOpen(b.message)) {
            b.billLines = activity::billDetail(bill);
            b.h += static_cast<float>(b.billLines.size()) * kNoteH + 2.f;
        }
    };
    // 1.50.1 : REDUIRE - le dernier echange (la derniere demande et ce qui la suit) reste deballe tant qu'on ne le reduit pas.
    std::size_t lastAsk = 0;
    if (conv)
        for (std::size_t i = 0; i < conv->messages.size(); ++i)
            if (conv->messages[i].role == "utilisateur" && !(conv->messages[i].extra.isObject() && conv->messages[i].extra.has("logiciel"))) lastAsk = i;
    const float toggleH = ui::lineHeight(kMeta) + 6.f;
    const auto fold = [&](Block& b, const Message& m, bool live) {
        const std::size_t cut = collapseCut(b.lines);
        b.key = conv->id + "|" + m.id;
        b.collapsible = cut < b.lines.size() && !live;
        bool open = static_cast<std::size_t>(b.message) >= lastAsk;
        if (const auto it = fold_.find(b.key); it != fold_.end()) open = it->second;
        b.collapsed = b.collapsible && !open;
        b.shown = b.collapsed ? cut : b.lines.size();
        b.toggle = !b.collapsible ? std::string{} : b.collapsed ? std::string("\xE2\x80\xA6 voir tout") : std::string("R\xC3\xA9" "duire");
    };
    if (conv) {
        for (std::size_t i = 0; i < conv->messages.size(); ++i) {
            const auto& m = conv->messages[i];
            Block b;
            b.message = static_cast<int>(i);
            // 1.22.5 : un message du LOGICIEL (la correction demandee a l'IA, l'avis de Generer et Compiler) n'est pas une bulle
            // de l'utilisateur : il se lit a gauche, sous << Logiciel >>.
            b.user = m.role == "utilisateur" && !(m.extra.isObject() && m.extra.has("logiciel"));
            b.y = y;
            const bool live = static_cast<int>(i) == generating;
            const std::string text = live ? a.shownText() : m.text;
            b.headW = ui::measureWidth((b.user ? std::string("Moi \xC2\xB7 ") : std::string("Assistant \xC2\xB7 ")) + shortDate(m.date, true), kMeta);
            if (b.user) {
                const float maxW = inner * 0.86f - 20.f;
                b.lines = layoutText(text, maxW);
                fold(b, m, live);   // 1.50.1
                float tw = 0.f;
                for (const auto& l : b.lines)
                    for (const auto& r : l.runs) tw = std::max(tw, r.x + r.w);
                for (const auto& at : m.attachments) tw = std::max(tw, std::min(maxW, ui::measureWidth(at.name, kMeta) + 110.f));
                // 1.50.1 : l'extrait cite au-dessus du texte (une demande qui repond a un message), << ... voir tout >> dessous.
                std::string quoted;
                if (const auto q = chatreply::readQuote(m)) {
                    quoted = (q->who.empty() ? std::string{} : q->who + " : ") + "\xC2\xAB " + q->excerpt + " \xC2\xBB";
                    tw = std::max(tw, std::min(maxW, ui::measureWidth(quoted, kMeta) + 10.f));
                }
                if (!b.toggle.empty()) tw = std::max(tw, std::min(maxW, ui::measureWidth(b.toggle, kMeta)));
                b.textW = tw;
                float th = 0.f;
                for (std::size_t k = 0; k < b.shown && k < b.lines.size(); ++k) th += b.lines[k].h + b.lines[k].gapBefore;
                if (text.empty()) th = 0.f;
                b.attachH = static_cast<float>(m.attachments.size()) * 24.f;
                const float quoteH = quoted.empty() ? 0.f : toggleH;
                const float bw = tw + 20.f, bh = quoteH + th + (b.toggle.empty() ? 0.f : toggleH) + b.attachH + 14.f;
                b.bubble = {w - kPad - 6.f - bw, meta, bw, bh};
                b.textX = b.bubble.x + 10.f;
                if (!quoted.empty()) {
                    b.quote = sessionui::elide(quoted, kMeta, std::max(0.f, tw - 10.f));
                    b.quoteY = meta + 7.f;
                }
                b.toggleY = meta + 7.f + quoteH + th;
                b.h = meta + bh;
            } else {
                b.lines = layoutText(text, inner - 8.f);
                fold(b, m, live);   // 1.50.1
                float th = 0.f;
                for (std::size_t k = 0; k < b.shown && k < b.lines.size(); ++k) th += b.lines[k].h + b.lines[k].gapBefore;
                b.h = meta + th + 4.f;
                b.textX = kPad + 4.f;
                if (!b.toggle.empty()) {   // 1.50.1 : << ... voir tout >> / << Reduire >> sous le texte
                    b.toggleY = b.h;
                    b.h += toggleH;
                }
                if (live) {   // l'indicateur, et ce qu'il dit en plus (une nouvelle tentative)
                    b.h += 26.f;
                    // 1.22.6 : la ligne d'activite dit deja la lecture, l'outil, la longueur : l'etat en plus reste pour la cause
                    // d'une nouvelle tentative.
                    const bool here = !act.empty() && act.message == static_cast<int>(i);
                    b.showDetail = !a.statusDetail().empty() && (!here || act.keepDetail);
                    if (b.showDetail)
                        b.h += static_cast<float>(std::min<std::size_t>(3, wrapPlain(a.statusDetail(), kMeta, inner - 30.f).size())) * kNoteH + 2.f;
                    if (here) placeActivity(b);
                    if (!liveBill_.empty()) placeBill(b, liveBill_, true);
                }
                // 1.15.0 : la note du logiciel (une erreur, une reponse coupee, une entree illisible).
                if (!live && !m.note.empty()) {
                    b.noteLines = wrapPlain(m.note, kMeta, inner - 30.f);
                    b.noteError = m.state == "erreur" || (m.tool && !m.tool->error.empty());
                    b.noteY = b.h + 2.f;
                    b.h += static_cast<float>(b.noteLines.size()) * kNoteH + 6.f;
                }
                // 1.22.6 : apres la reponse, le travail de fond sur sa proposition (la verification, Generer et Compiler).
                if (!live && !act.empty() && act.message == static_cast<int>(i)) placeActivity(b);
                if (!live && m.proposal) {
                    // Une carte close (appliquee, refusee) n'a plus d'apercu : la vue a change depuis.
                    const bool open = m.proposal->state == "proposee";
                    const Thumb& t = thumb(static_cast<int>(i));
                    const float cw = inner - 4.f;
                    float ch = 8.f + 24.f + 22.f;
                    // 1.22.4 : LE CERCLE vu a son arrivee (elle refait une proposition precedente, les memes erreurs...) - sous
                    // les comptes ; retenue tant que l'utilisateur n'a pas choisi.
                    const Assistant::LoopInfo lp = a.loopOf(static_cast<int>(i));
                    b.loopHeld = lp.held && open;
                    if (!lp.text.empty()) {
                        std::string said = b.loopHeld ? "Tourne en rond : " + lp.text + ". Choisis ci-dessous : rien ne repart avant ton choix."
                                                      : "A tourn\xC3\xA9 en rond : " + lp.text
                                                            + (lp.choice.empty() ? std::string{} : " \xE2\x80\x94 " + std::string(loop::choiceLabel(lp.choice))) + ".";
                        b.loopLines = wrapPlain(said, kMeta, cw - 24.f);
                        if (b.loopLines.size() > 5) {
                            b.loopLines.resize(5);
                            b.loopLines.back() += " \xE2\x80\xA6";
                        }
                        ch += static_cast<float>(b.loopLines.size()) * kNoteH + 4.f;
                    }
                    // 1.16.0 : la vue, les variables, les types crees - une ligne sous les comptes.
                    if (const std::string made = creationSummary(*m.proposal); !made.empty()) {
                        b.createLines = wrapPlain("Cr\xC3\xA9" "e aussi : " + made, kMeta, cw - 24.f);
                        ch += static_cast<float>(b.createLines.size()) * kNoteH + 2.f;
                    }
                    // 1.21.3 : LA SUITE d'une grande demande decoupee par l'IA (le bouton Proposer la suite, une fois appliquee).
                    if (!m.proposal->next.empty() && m.proposal->state != "refusee") {
                        auto more = wrapPlain("Suite pr\xC3\xA9vue : " + m.proposal->next, kMeta, cw - 24.f);
                        if (more.size() > 3) {
                            more.resize(3);
                            more.back() += " \xE2\x80\xA6";
                        }
                        ch += static_cast<float>(more.size()) * kNoteH + (b.createLines.empty() ? 2.f : 0.f);
                        for (auto& l : more) b.createLines.push_back(std::move(l));
                    }
                    // 1.22.1 : LES CORRECTIONS faites au clic droit sur une erreur (la proposition seule) - en une ligne.
                    if (open)
                        if (const std::string fixed = fixes::summary(*m.proposal); !fixed.empty()) {
                            auto more = wrapPlain("Corrig\xC3\xA9" "e : " + fixed + ".", kMeta, cw - 24.f);
                            ch += static_cast<float>(more.size()) * kNoteH + (b.createLines.empty() ? 2.f : 0.f);
                            for (auto& l : more) b.createLines.push_back(std::move(l));
                        }
                    // 1.22.5 : LE PROCESSUS DE DEVELOPPEMENT (l'avis du logiciel, les corrections) - l'en-tete (un clic le deplie ;
                    // deplie pendant le travail), puis ses lignes et les constats de Generer et Compiler.
                    if (const auto proc = a.processOf(static_cast<int>(i)); !proc.empty()) {
                        b.procOpen = processOpen(static_cast<int>(i));
                        b.procHead = "Processus de d\xC3\xA9veloppement (" + std::to_string(proc.size()) + ")";
                        if (!b.procOpen) b.procHead += " \xE2\x80\x94 " + proc.back().time + " " + proc.back().text;
                        b.procY = ch;
                        ch += kNoteH + 6.f;
                        if (b.procOpen) {
                            for (const auto& l : proc) {
                                std::uint8_t tone = 0;
                                if (l.what == "logiciel") tone = l.errors > 0 ? 3 : l.warnings > 0 ? 2 : l.text.find(" : aucun constat") != std::string::npos ? 1 : 2;
                                else if (l.what == "verification") tone = l.errors > 0 ? 3 : 0;
                                else if (l.what == "arret" || l.what == "cercle") tone = 2;
                                else if (l.what == "direct" || l.what == "attente") tone = 4;
                                for (auto& line : wrapPlain(l.time + "  " + l.text, kMeta, cw - 30.f)) b.procLines.push_back({std::move(line), tone, false, l.proposal});
                                for (const auto& f : l.findings) {
                                    auto lines = wrapPlain(f.text, kMeta, cw - 50.f);
                                    if (lines.size() > 2) {
                                        lines.resize(2);
                                        lines.back() += " \xE2\x80\xA6";
                                    }
                                    for (auto& line : lines) b.procLines.push_back({std::move(line), static_cast<std::uint8_t>(f.object.empty() && f.view.empty() && f.text.rfind("\xE2\x80\xA6", 0) == 0 ? 4 : f.error ? 3 : 2), true, l.proposal});
                                }
                            }
                            ch += static_cast<float>(b.procLines.size()) * kNoteH + 4.f;
                        }
                    }
                    // 1.19.0 : LE PLAN D'ACTION - seulement pour une demande qui en merite un (pas une simple question).
                    const plan::Plan& pl = t.plan;
                    const bool showPlan = m.proposal->state != "refusee" && pl.worthShowing();
                    if (showPlan) {
                        const auto applied = m.proposal->state == "appliquee" ? a.appliedState(static_cast<int>(i)) : Assistant::Applied::NotApplied;
                        b.phaseLines = wrapPlain(phasesOf(*m.proposal, pl, applied, t.check.ok()), kMeta, cw - 24.f);
                        ch += static_cast<float>(b.phaseLines.size()) * kNoteH + 2.f;
                        b.planY = ch;
                        b.planHead = planHeadOf(*m.proposal, pl);
                        ch += kNoteH + 6.f;
                        if (planOpen_.count(m.proposal->id)) {
                            constexpr std::size_t kMaxSteps = 80;
                            b.stepsY = ch;
                            for (const auto& st : pl.steps) {
                                if (b.steps.size() == kMaxSteps) {
                                    b.stepsMore = "\xE2\x80\xA6 et " + std::to_string(pl.steps.size() - kMaxSteps)
                                                  + " autres \xC3\xA9tapes (Pr\xC3\xA9visualiser les montre toutes)";
                                    break;
                                }
                                Block::StepLine sl;
                                sl.text = st.id + "  " + st.title + "  \xE2\x80\x94 "
                                          + (st.excluded ? std::string("retir\xC3\xA9" "e") : std::string(plan::stateLabel(st.state)));
                                if (!st.errors.empty()) sl.text += " : " + st.errors.front();
                                else if (!st.note.empty() && !st.excluded) sl.text += " (" + st.note + ")";
                                sl.state = st.state;
                                sl.op = st.op;
                                sl.excluded = st.excluded;
                                sl.removable = open && (st.optional || st.excluded);
                                const plan::Ref* ref = !st.creates.empty() ? &st.creates.front() : !st.modifies.empty() ? &st.modifies.front() : nullptr;
                                if (ref) {
                                    sl.kind = ref->kind;
                                    sl.name = ref->name;
                                    sl.view = !ref->view.empty() ? ref->view : !st.target.empty() ? st.target : m.proposal->view;
                                }
                                b.steps.push_back(std::move(sl));
                            }
                            ch += static_cast<float>(b.steps.size()) * kNoteH + (b.stepsMore.empty() ? 0.f : kNoteH) + 4.f;
                        }
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
                        if (b.loopHeld && !stale) {   // 1.22.4 : retenue - les choix
                            b.buttons = {"Corriger \xC3\xA0 la main\xE2\x80\xA6", "Proposer quand m\xC3\xAAme", "Modifier la demande"};
                            if (!lp.mission.empty()) {
                                b.buttons.push_back(lp.steps.size() > 1 ? "Ignorer ces \xC3\xA9tapes" : "Ignorer cette \xC3\xA9tape");
                                b.buttons.push_back("Forcer la fin de la mission\xE2\x80\xA6");
                            }
                        }
                        // Les erreurs puis les avertissements (trois au plus ; le reste dans l'apercu), a la ligne.
                        std::size_t lines = 0;
                        // 1.22.1 : les doublons regroupes (<< ... (2 operations) >>) ; le clic droit sur une note la corrige.
                        const auto note = [&](const fixes::Line& l) {
                            if (b.notes.size() >= 3) return;
                            Block::Note n;
                            n.error = l.error;
                            n.message = l.message;
                            n.lines = wrapPlain(l.text, kMeta, cw - 44.f);
                            if (n.lines.size() > kNoteLines) {
                                n.lines.resize(kNoteLines);
                                n.lines.back() += " \xE2\x80\xA6";
                            }
                            lines += n.lines.size();
                            b.notes.push_back(std::move(n));
                        };
                        for (const auto& l : fixes::errorLines(t.check)) note(l);
                        if (b.notes.size() < 3)
                            for (const auto& l : fixes::warningLines(t.check)) note(l);
                        ch += 100.f + 6.f + static_cast<float>(lines) * kNoteH + 6.f;
                    } else {
                        b.applied = a.appliedState(static_cast<int>(i));
                        std::string done;
                        // 1.22.5 : remplacee par sa correction (l'avis du logiciel) - elle ne s'applique plus ; la carte mene a la corrigee.
                        if (m.proposal->state == "remplacee") {
                            const std::string next = m.proposal->extra.isObject() ? m.proposal->extra.str("remplaceePar") : std::string{};
                            b.replacedBy = next.empty() ? -1 : a.messageOfProposal(next);
                            done = b.replacedBy >= 0 ? std::string("Remplac\xC3\xA9" "e par sa correction (plus bas) : elle ne s'applique plus.")
                                   : a.processWorking(static_cast<int>(i))
                                       ? std::string("Remplac\xC3\xA9" "e : l'IA la corrige d'apr\xC3\xA8s G\xC3\xA9n\xC3\xA9rer et Compiler\xE2\x80\xA6")
                                       : std::string("Remplac\xC3\xA9" "e : elle ne s'applique plus.");
                            if (b.replacedBy >= 0) b.buttons = {"Voir la correction"};
                            b.applied = Assistant::Applied::NotApplied;
                        } else
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
                                if (panel_.canDetect(static_cast<int>(i))) b.buttons = {kDetectText};   // 1.18.0
                                else if (!a.nextRequest(static_cast<int>(i)).empty()) b.buttons = {kNextText};   // 1.21.3
                                break;
                            case Assistant::Applied::Applied:
                                done = "Appliqu\xC3\xA9" "e au projet : Ctrl+Z la reprend d'un coup.";
                                if (panel_.canDetect(static_cast<int>(i))) b.buttons = {kDetectText};   // 1.18.0
                                else if (!a.nextRequest(static_cast<int>(i)).empty()) b.buttons = {kNextText};   // 1.21.3
                                break;
                            default:
                                done = "Refus\xC3\xA9" "e : le projet n'a pas chang\xC3\xA9.";
                                if (m.proposal->state == "refusee") b.buttons = {"Revenir sur le refus"};   // 1.16.1
                                break;
                        }
                        // 1.19.0 : LE COMPTE RENDU REEL (le projet d'avant et d'apres Appliquer), garde avec la proposition.
                        if (const auto rep = a.reportOf(static_cast<int>(i)); rep && showPlan) {
                            const auto add = [&](const std::string& said, std::uint8_t tone, std::size_t maxLines) {
                                auto lines = wrapPlain(said, kMeta, cw - 24.f);
                                if (lines.size() > maxLines) {
                                    lines.resize(maxLines);
                                    lines.back() += " \xE2\x80\xA6";
                                }
                                for (auto& l : lines) b.reportLines.push_back({std::move(l), tone});
                            };
                            const auto names = [](const plan::Line& l) {
                                std::string out = std::to_string(l.names.size()) + " " + l.category + " : ";
                                for (std::size_t k = 0; k < l.names.size(); ++k) out += (k ? ", " : "") + l.names[k];
                                return out;
                            };
                            add(b.applied == Assistant::Applied::Undone
                                    ? std::string("Compte rendu (repris depuis par Ctrl+Z : ce qui suit n'est plus dans le projet)")
                                    : std::string("Compte rendu"),
                                b.applied == Assistant::Applied::Undone ? 2 : 4, 2);
                            for (const auto& l : rep->created) add("Cr\xC3\xA9\xC3\xA9 \xE2\x80\x94 " + names(l), 1, 3);
                            for (const auto& l : rep->modified) add("Modifi\xC3\xA9 \xE2\x80\x94 " + names(l), 0, 3);
                            for (const auto& l : rep->reused) add("R\xC3\xA9utilis\xC3\xA9 \xE2\x80\x94 " + names(l), 0, 2);
                            for (const auto& x : rep->todo) add("\xC3\x80 faire \xE2\x80\x94 " + x, 2, 3);
                            for (const auto& x : rep->failures) add("Erreur \xE2\x80\x94 " + x, 3, 3);
                            for (const auto& x : rep->external) add(x, 2, 3);
                            add(rep->verdict, rep->failed + rep->skipped > 0 ? 2 : 1, 3);
                            ch += static_cast<float>(b.reportLines.size()) * kNoteH + 4.f;
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
                // 1.22.6 : LE BILAN de la reponse (garde avec elle) - sa ligne, son << + >>.
                if (!live)
                    if (const auto bill = a.billOf(static_cast<int>(i))) placeBill(b, *bill, false);
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
    XPG_MESURE("ia.discussion.refaire");   // 1.22.2
    const bool wasBottom = stick_ || atBottom();
    measure();
    // Les boutons des cartes : un trio par proposition encore "proposee". Ils sont GARDES d'une
    // fois sur l'autre (caches quand ils ne servent pas) : un clic sur l'un d'eux peut refaire la
    // discussion pendant qu'il emet son signal - il ne doit pas etre detruit sous lui.
    for (auto& cb : cards_) {
        cb.message = -1;
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third, cb.fourth, cb.fifth}) btn->setVisibility(ui::Visibility::Collapsed);
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
            cb.fourth = mk(".ignorer-etape", ui::Button::Style::Flat);       // 1.22.4 : retenue (elle tourne en rond)
            cb.fifth = mk(".forcer-fin", ui::Button::Style::Flat);
            const std::size_t k = slot;
            links_ += cb.preview->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].loop) (void)panel_.chooseLoop(cards_[k].message, Assistant::LoopChoice::ByHand);   // 1.22.4
                else panel_.openPreview(cards_[k].message);
            });
            links_ += cb.fourth->clicked->connect([this, k] {
                if (k < cards_.size() && cards_[k].message >= 0 && cards_[k].loop) (void)panel_.chooseLoop(cards_[k].message, Assistant::LoopChoice::SkipStep);
            });
            links_ += cb.fifth->clicked->connect([this, k] {
                if (k < cards_.size() && cards_[k].message >= 0 && cards_[k].loop) (void)panel_.chooseLoop(cards_[k].message, Assistant::LoopChoice::ForceFinish);
            });
            links_ += cb.apply->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].replacedBy >= 0) revealMessage(cards_[k].replacedBy, false);   // 1.22.5 : la carte de sa correction
                else if (cards_[k].loop) (void)panel_.chooseLoop(cards_[k].message, Assistant::LoopChoice::ProposeAnyway);   // 1.22.4
                else if (cards_[k].detect) (void)panel_.detectFromProposal(cards_[k].message);   // 1.18.0
                else if (cards_[k].next) {   // 1.21.3 : << Propose la suite : ... >>, envoye comme une demande
                    const std::string ask = panel_.assistant().nextRequest(cards_[k].message);
                    if (!ask.empty()) (void)panel_.sendOrAdvise(ask);   // 1.22.6 : une suite trop grosse : le conseil d'abord
                }
                else if (cards_[k].refused) (void)panel_.unrefuseProposal(cards_[k].message);   // 1.16.1
                else if (cards_[k].undone) (void)panel_.reapplyProposal(cards_[k].message);
                else (void)panel_.applyProposal(cards_[k].message);
            });
            links_ += cb.third->clicked->connect([this, k] {
                if (k >= cards_.size() || cards_[k].message < 0) return;
                if (cards_[k].loop) (void)panel_.chooseLoop(cards_[k].message, Assistant::LoopChoice::EditRequest);   // 1.22.4
                else if (cards_[k].stale) panel_.editRequest(cards_[k].message);
                else panel_.askRefuse(cards_[k].message);   // 1.16.1 (captures) : une question d'abord, comme l'apercu
            });
            cards_.push_back(cb);
        }
        CardButtons& cb = cards_[slot++];
        cb.message = b.message;
        cb.refused = p->state == "refusee";
        cb.detect = false;
        cb.next = false;
        cb.loop = false;
        cb.replacedBy = -1;
        cb.preview->setEnabled(true);
        // 1.22.5 : REMPLACEE par sa correction (l'avis du logiciel) - << Voir la correction >>, seul.
        if (p->state == "remplacee") {
            cb.replacedBy = b.replacedBy;
            cb.undone = false;
            cb.stale = false;
            cb.apply->setText("Voir la correction");
            cb.apply->setTooltip("Aller \xC3\xA0 la carte de la proposition corrig\xC3\xA9" "e (G\xC3\xA9n\xC3\xA9rer et Compiler avaient trouv\xC3\xA9 des erreurs dans celle-ci).");
            cb.apply->setEnabled(b.replacedBy >= 0);
            cb.apply->setVisibility(ui::Visibility::Visible);
            continue;
        }
        // 1.22.4 : RETENUE (elle tourne en rond) - les choix, rien d'autre ; une mission : deux de plus.
        if (b.loopHeld && p->state == "proposee" && !(t.check.stale || t.check.viewMissing)) {
            const Assistant::LoopInfo lp = panel_.assistant().loopOf(b.message);
            const bool busy = t.pending() || panel_.assistant().applying(b.message);
            cb.loop = true;
            cb.stale = false;
            cb.undone = false;
            cb.preview->setText("Corriger \xC3\xA0 la main\xE2\x80\xA6");
            cb.preview->setTooltip("Ouvrir la pr\xC3\xA9visualisation pour la corriger toi-m\xC3\xAAme (le clic droit sur une erreur, retirer une op\xC3\xA9ration), "
                                   "puis l'appliquer. Rien ne change dans le projet avant Appliquer.");
            cb.preview->setEnabled(!busy);
            cb.apply->setText("Proposer quand m\xC3\xAAme");
            cb.apply->setTooltip("La garder telle quelle : elle redevient une proposition comme les autres (pr\xC3\xA9visualiser, appliquer ou refuser).");
            cb.apply->setEnabled(!busy);
            cb.third->setText("Modifier la demande");
            cb.third->setTooltip(lp.mission.empty() ? std::string("Remettre la demande dans la saisie pour la reformuler.")
                                                    : std::string("Refuser cette proposition et \xC3\xA9" "crire ta consigne pour l'IA (elle est gard\xC3\xA9" "e "
                                                                  "dans la mission) : la suite repart avec elle."));
            cb.preview->setVisibility(ui::Visibility::Visible);
            cb.apply->setVisibility(ui::Visibility::Visible);
            cb.third->setVisibility(ui::Visibility::Visible);
            if (!lp.mission.empty()) {
                cb.fourth->setText(lp.steps.size() > 1 ? "Ignorer ces \xC3\xA9tapes" : "Ignorer cette \xC3\xA9tape");
                cb.fourth->setTooltip("Refuser cette proposition et ignorer " + (lp.steps.empty() ? std::string("ses \xC3\xA9tapes") : lp.steps.front()
                                      + (lp.steps.size() > 1 ? " et les autres \xC3\xA9tapes en cause" : std::string{})) + " : la mission continue sans.");
                cb.fourth->setEnabled(!busy);
                cb.fifth->setText("Forcer la fin de la mission\xE2\x80\xA6");
                cb.fifth->setTooltip("Terminer la mission l\xC3\xA0 o\xC3\xB9 elle en est (une question d'abord) : rien n'est d\xC3\xA9" "fait dans le projet.");
                cb.fifth->setEnabled(!busy);
                cb.fourth->setVisibility(ui::Visibility::Visible);
                cb.fifth->setVisibility(ui::Visibility::Visible);
            }
            continue;
        }
        if (p->state == "appliquee" && panel_.canDetect(b.message)) {   // 1.18.0 : appliquee, une detection proposee
            cb.detect = true;
            cb.undone = false;
            cb.stale = false;
            cb.apply->setText(kDetectText);
            cb.apply->setTooltip("Ouvrir Communication \xE2\x80\xBA Scanner IP, la plage propos\xC3\xA9" "e remplie. Rien ne part sur le r\xC3\xA9seau "
                                 "tant que tu n'as pas cliqu\xC3\xA9 D\xC3\xA9marrer.");
            cb.apply->setEnabled(true);
            cb.apply->setVisibility(ui::Visibility::Visible);
            continue;
        }
        // 1.21.3 : appliquee (et pas annulee), une suite prevue par l'IA : la demander.
        if (p->state == "appliquee" && !b.buttons.empty() && b.buttons.front() == kNextText) {
            cb.next = true;
            cb.undone = false;
            cb.stale = false;
            cb.apply->setText(kNextText);
            cb.apply->setTooltip("Envoyer \xC2\xAB Propose la suite : " + p->next + " \xC2\xBB : l'IA propose la partie suivante de la demande "
                                 "(une nouvelle carte, \xC3\xA0 v\xC3\xA9rifier et appliquer comme celle-ci).");
            // 1.22.4 : une proposition plus recente tourne en rond (retenue) - la suite attend le choix sur sa carte.
            bool laterHeld = false;
            if (const Conversation* conv = panel_.assistant().current())
                for (int j = b.message + 1; j < static_cast<int>(conv->messages.size()) && !laterHeld; ++j)
                    laterHeld = conv->messages[static_cast<std::size_t>(j)].proposal && panel_.assistant().loopOf(j).held;
            if (laterHeld) cb.apply->setTooltip("Une proposition plus r\xC3\xA9" "cente tourne en rond : choisis d'abord sur sa carte.");
            cb.apply->setEnabled(!panel_.assistant().generating() && !laterHeld);
            cb.apply->setVisibility(ui::Visibility::Visible);
            continue;
        }
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
        const bool busy = t.pending() || panel_.assistant().applying(b.message);   // 1.20.0, 1.20.1 : un fil de fond travaille
        cb.apply->setEnabled((t.check.ok() || anyway) && !busy);
        cb.preview->setEnabled(!busy);
        if (t.pending()) cb.apply->setTooltip("La proposition se v\xC3\xA9rifie (sur un fil de fond) : Appliquer revient \xC3\xA0 la fin.");
        if (panel_.assistant().applying(b.message)) cb.apply->setTooltip("La proposition s'applique (sur un fil de fond).");
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
        if (cb.message == message && message >= 0)
            return which == 0 ? cb.preview : which == 1 ? cb.apply : which == 2 ? cb.third : which == 3 ? cb.fourth : cb.fifth;
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
    if (bounds().w != measuredW_ || std::exchange(actRemeasure_, false)) {   // 1.22.6 : la ligne d'activite demande une rangee de plus
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
        for (ui::Button* btn : {cb.preview, cb.apply, cb.third, cb.fourth, cb.fifth}) {
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

// ---- 1.22.6 : CE QUE FAIT L'IA, EN DIRECT, ET CE QUE CA COUTE (Assistant::activity, AiActivity.hpp) ---------------------
//  La ligne est relue au plus quatre fois par seconde (kRefresh) : son texte bouge (le compteur, le temps), un trait fin avance
//  dessous ; les etapes passees, en gris, se replient d'un clic sur l'icone. Le bilan : une ligne et son << + >>.
const activity::Snapshot& AiChatView::activityNow(bool fresh) const {
    const auto& a = panel_.assistant();
    const double now = a.lastTick();
    if (fresh || now - actAt_ >= activity::kRefresh || now < actAt_) {
        act_ = a.activity(now);
        liveBill_ = a.liveBill(now);
        actAt_ = now;
    }
    return act_;
}

void AiChatView::paintActivity(const ui::PaintContext& ctx, const Block& b, float top) {
    const auto& c = ctx.theme.color;
    const auto bb = bounds();
    const activity::Snapshot& act = activityNow(false);
    if (act.empty() || act.message != b.message) return;   // finie depuis la mise en page : la prochaine la retire
    const float tx = bb.x + kPad + 4.f;
    const float width = std::max(80.f, bb.w - 2 * kPad - 12.f) - 28.f;
    const float lh = ctx.r.lineHeight(kMeta);
    float y = top + b.actY;
    if (b.actContext) {
        ctx.r.drawText({tx + 20.f, y}, elideTip(ctx.r, act.context, kMeta, width, {tx + 20.f, y, width, kNoteH}, tips_), kMeta, c.accent);
        y += kNoteH;
    }
    historySpot_ = {tx - 2.f, y, 20.f, kActRow};
    ui::drawIcon(ctx.r, ui::Icon::History, {tx, y + (lh - 14.f) / 2.f + 1.f, 14.f, 14.f}, act.history.empty() ? alpha(c.textMuted, 110) : c.textMuted);
    tips_.emplace_back(historySpot_, historyOpen_ ? std::string("Replier les \xC3\xA9tapes pass\xC3\xA9" "es")
                                                  : std::string("Montrer les derni\xC3\xA8res \xC3\xA9tapes et leur dur\xC3\xA9" "e"));
    // La ligne, a la ligne sur deux rangees au plus (l'operation en cours, le classeur lu : la fin compte) ; une rangee de plus
    // que la mise en page n'en a prevu : elle est refaite (la prochaine image), coupee d'ici la.
    std::vector<std::string> rows = wrapPlain(act.line, kMeta, width);
    if (rows.empty()) rows.push_back(act.line);
    if (rows.size() > b.actRows && b.actRows < 2 && !actRemeasure_ && actRemeasureAt_ != actAt_) {   // une fois par lecture de la ligne
        actRemeasure_ = true;
        actRemeasureAt_ = actAt_;
        invalidateLayout();
    }
    if (rows.size() > b.actRows) {
        std::string rest;
        for (std::size_t k = b.actRows - 1; k < rows.size(); ++k) rest += (rest.empty() ? std::string{} : std::string(" ")) + rows[k];
        rows.resize(b.actRows);
        rows.back() = rest;
    }
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const float ry = y + 1.f + static_cast<float>(k) * kNoteH;
        ctx.r.drawText({tx + 20.f, ry}, k + 1 == rows.size() ? elideTip(ctx.r, rows[k], kMeta, width, {tx + 20.f, ry, width, kNoteH}, tips_) : rows[k], kMeta, c.text);
    }
    y += static_cast<float>(rows.size() - 1) * kNoteH;
    // Le trait qui avance (doucement) sous la ligne : un travail en cours.
    const gfx::Rect track{tx + 20.f, y + lh + 4.f, std::min(width, 240.f), 2.f};
    ctx.r.fillRect(track, alpha(c.accent, 26));
    if (act.working) {
        const float seg = track.w * 0.3f;
        const float at = track.x - seg + static_cast<float>(std::fmod(std::max(0.0, ctx.time) * 0.45, 1.0)) * (track.w + seg);
        const float x0 = std::max(track.x, at), x1 = std::min(track.right(), at + seg);
        if (x1 > x0) ctx.r.fillRect({x0, track.y, x1 - x0, track.h}, alpha(c.accent, 120));
    }
    y += kActRow;
    const std::size_t n = std::min(b.actHistory, act.history.size());
    for (std::size_t k = 0; k < n; ++k) {
        const auto& st = act.history[k];
        const std::string t = "\xE2\x9C\x93 " + st.text + " \xC2\xB7 " + activity::shortDuration(st.seconds);
        ctx.r.drawText({tx + 20.f, y}, elideTip(ctx.r, t, kMeta, width, {tx + 20.f, y, width, kNoteH}, tips_), kMeta, alpha(c.textMuted, 210));
        y += kNoteH;
    }
}

void AiChatView::paintBill(const ui::PaintContext& ctx, const Block& b, float top) {
    const auto& c = ctx.theme.color;
    const auto& a = panel_.assistant();
    const auto bb = bounds();
    const float tx = bb.x + kPad + 4.f;
    const float width = std::max(80.f, bb.w - 2 * kPad - 12.f) - 8.f;
    const float lh = ctx.r.lineHeight(kMeta);
    // La reponse en cours : ses chiffres relus avec l'activite (quatre fois par seconde au plus) ; le nombre de lignes, celui
    // de la mise en page.
    const bool live = b.message == a.generatingMessage();
    std::string text = b.billText;
    std::vector<std::string> lines = b.billLines;
    if (live) {
        (void)activityNow(false);
        if (!liveBill_.empty()) {
            text = "Cette r\xC3\xA9ponse : " + activity::billLine(liveBill_);
            if (!lines.empty()) {
                auto fresh = activity::billDetail(liveBill_);
                fresh.resize(lines.size());
                lines = std::move(fresh);
            }
        }
    }
    const float y = top + b.billY;
    const std::string shown = elide(ctx.r, text, kMeta, width - 34.f);
    ctx.r.drawText({tx, y}, shown, kMeta, c.textMuted);
    const bool open = billOpen(b.message);
    const gfx::Rect pill{tx + ctx.r.measure(shown, kMeta).width + 8.f, y - 1.f, 22.f, lh + 2.f};
    ctx.r.fillRoundedRect(pill, alpha(c.accent, open ? 64 : 36), 4.f);
    const std::string sign = open ? "\xE2\x80\x93" : "+";
    ctx.r.drawText({pill.x + (pill.w - ctx.r.measure(sign, kMeta).width) / 2.f, pill.y + 1.f}, sign, kMeta, c.accent);
    billSpots_.emplace_back(pill, b.message);
    tips_.emplace_back(pill, open ? std::string("Replier le d\xC3\xA9tail")
                                  : std::string("Le d\xC3\xA9tail par action : chaque appel au service, ses jetons (entr\xC3\xA9" "e, cache, sortie, "
                                                "r\xC3\xA9" "flexion) et sa dur\xC3\xA9" "e. Le logiciel ne conna\xC3\xAEt pas le prix du mod\xC3\xA8le : "
                                                "les jetons seulement ; env. : une estimation, remplac\xC3\xA9" "e par le compte du service."));
    float ly = y + kNoteH + 2.f;
    for (const auto& l : lines) {
        ctx.r.drawText({tx + 12.f, ly}, elideTip(ctx.r, l, kMeta, width - 12.f, {tx + 12.f, ly, width - 12.f, kNoteH}, tips_), kMeta,
                       l.rfind("Total", 0) == 0 ? c.text : c.textMuted);
        ly += kNoteH;
    }
}

std::string AiChatView::billKey(int message) const {
    const auto* conv = panel_.assistant().current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size())) return {};
    return conv->id + "|" + conv->messages[static_cast<std::size_t>(message)].id;
}

bool AiChatView::billOpen(int message) const {
    const std::string k = billKey(message);
    return !k.empty() && billOpen_.count(k) > 0;
}

void AiChatView::setBillOpen(int message, bool open) {
    const std::string k = billKey(message);
    if (k.empty()) return;
    if (open) billOpen_.insert(k);
    else billOpen_.erase(k);
    stick_ = false;
    measure();
    placeCards();
    invalidate();
}

void AiChatView::setActivityHistoryOpen(bool open) {
    if (historyOpen_ == open) return;
    historyOpen_ = open;
    measure();
    placeCards();
    invalidate();
}

std::vector<std::string> AiChatView::activityLines() const {
    std::vector<std::string> out;
    const activity::Snapshot& act = activityNow(false);
    if (act.empty()) return out;
    if (!act.context.empty()) out.push_back(act.context);
    out.push_back(act.line);
    if (historyOpen_)
        for (const auto& st : act.history) out.push_back("\xE2\x9C\x93 " + st.text + " \xC2\xB7 " + activity::shortDuration(st.seconds));
    return out;
}

std::string AiChatView::billText(int message) const {
    if (message >= 0 && message == panel_.assistant().generatingMessage()) {
        (void)activityNow(false);
        return liveBill_.empty() ? std::string{} : "Cette r\xC3\xA9ponse : " + activity::billLine(liveBill_);
    }
    for (const auto& b : blocks_)
        if (b.message == message) return b.billText;
    return {};
}

std::vector<std::string> AiChatView::billDetailLines(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.billLines;
    return {};
}

gfx::Rect AiChatView::billRect(int message) const {
    for (const auto& [at, m] : billSpots_)
        if (m == message) return at;
    return {};
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
    XPG_MESURE("ia.peinture.carte");   // 1.22.2
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
    else if (p->state == "remplacee") { st = "Remplac\xC3\xA9" "e"; tone = c.textMuted; }   // 1.22.5 : par sa correction
    else if (stale) { st = "P\xC3\xA9rim\xC3\xA9" "e"; tone = c.warning; }
    else if (a.applying(b.message)) { st = "Application\xE2\x80\xA6"; tone = c.info; }   // 1.20.1 : sur un fil de fond
    else if (t.pending()) { st = "V\xC3\xA9rification\xE2\x80\xA6"; tone = c.info; }   // 1.20.0 : sur un fil de fond
    else if (a.building(p->id)) { st = "G\xC3\xA9n\xC3\xA9rer et Compiler\xE2\x80\xA6"; tone = c.info; }   // 1.22.5 : l'avis du logiciel
    else if (b.loopHeld) { st = "Tourne en rond"; tone = c.warning; }   // 1.22.4 : retenue, le choix d'abord
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
    // 1.22.4 : le cercle vu a son arrivee (retenue : en couleur d'attention ; un choix fait : discret).
    for (const auto& l : b.loopLines) {
        ctx.r.drawText({card.x + 12.f, y}, l, kMeta, b.loopHeld ? c.warning : c.textMuted);
        y += kNoteH;
    }
    if (!b.loopLines.empty()) y += 4.f;
    for (const auto& l : b.createLines) {   // 1.16.0 : la vue, les variables, les types crees
        ctx.r.drawText({card.x + 12.f, y}, l, kMeta, c.ok);
        y += kNoteH;
    }
    if (!b.createLines.empty()) y += 2.f;
    // 1.22.5 : LE PROCESSUS DE DEVELOPPEMENT - l'en-tete (un clic le deplie), ses lignes ; sous l'avis de Generer et Compiler,
    // ses constats (rouge : une erreur, ambre : un avertissement ; un clic : la previsualisation).
    if (b.procY > 0.f) {
        y = card.y + b.procY;
        const gfx::Rect head{card.x + 10.f, y, card.w - 20.f, kNoteH + 2.f};
        const bool working = a.processWorking(b.message);
        ctx.r.fillRoundedRect(head, alpha(working ? c.info : c.accent, 26), 4.f);
        ui::drawIcon(ctx.r, b.procOpen ? ui::Icon::Collapse : ui::Icon::Expand, {head.x + 4.f, head.y + 2.f, 14.f, 14.f}, c.textMuted);
        ctx.r.drawText({head.x + 22.f, head.y + (head.h - lhCap) / 2.f},
                       elideTip(ctx.r, b.procHead + (working ? std::string("  \xE2\x80\xA6") : std::string{}), kMeta, head.w - 28.f, head, tips_), kMeta, c.text);
        tips_.emplace_back(head, b.procOpen ? std::string("Replier le processus de d\xC3\xA9veloppement.")
                                            : std::string("D\xC3\xA9plier : ce que le logiciel et l'IA ont fait pour cette demande (G\xC3\xA9n\xC3\xA9rer et Compiler, "
                                                          "les corrections)."));
        y += kNoteH + 6.f;
        if (b.procOpen) {
            for (const auto& l : b.procLines) {
                const gfx::Color ink = l.tone == 1 ? c.ok : l.tone == 2 ? c.warning : l.tone == 3 ? c.error : l.tone == 4 ? c.textMuted : c.text;
                if (l.finding && l.tone != 4) {
                    ctx.r.fillRoundedRect({card.x + 20.f, y + 6.f, 5.f, 5.f}, ink, 2.5f);
                    tips_.emplace_back(gfx::Rect{card.x + 10.f, y, card.w - 20.f, kNoteH}, l.text + "\nClic : la pr\xC3\xA9visualisation.");
                }
                ctx.r.drawText({card.x + (l.finding ? 30.f : 14.f), y}, l.text, kMeta, ink);
                y += kNoteH;
            }
            y += 4.f;
        }
    }
    // 1.19.0 : les phases reelles, le plan d'action (un clic le deplie), ses etapes et leur etat.
    if (b.planY > 0.f) {
        for (const auto& l : b.phaseLines) {
            ctx.r.drawText({card.x + 12.f, y}, l, kMeta, c.textMuted);
            y += kNoteH;
        }
        y = card.y + b.planY;
        const gfx::Rect head{card.x + 10.f, y, card.w - 20.f, kNoteH + 2.f};
        ctx.r.fillRoundedRect(head, alpha(c.info, 26), 4.f);
        const bool unfolded = !b.steps.empty();
        ui::drawIcon(ctx.r, unfolded ? ui::Icon::Collapse : ui::Icon::Expand, {head.x + 4.f, head.y + 2.f, 14.f, 14.f}, c.textMuted);
        ctx.r.drawText({head.x + 22.f, head.y + (head.h - lhCap) / 2.f},
                       elideTip(ctx.r, b.planHead + (unfolded ? "" : "  (clic : d\xC3\xA9plier)"), kMeta, head.w - 28.f, head, tips_), kMeta,
                       c.text);
        y += kNoteH + 6.f;
        if (unfolded) {
            y = card.y + b.stepsY;
            for (const auto& sl : b.steps) {
                gfx::Color dot = c.textDisabled;
                switch (sl.state) {
                    case plan::State::Validated: dot = c.info; break;
                    case plan::State::Applied: dot = c.ok; break;
                    case plan::State::Failed: dot = c.error; break;
                    case plan::State::Skipped: dot = c.warning; break;
                    case plan::State::Cancelled: dot = c.textMuted; break;
                    default: break;
                }
                if (sl.excluded) dot = c.textDisabled;
                ctx.r.fillRoundedRect({card.x + 14.f, y + 6.f, 7.f, 7.f}, dot, 3.5f);
                const float linkW = sl.removable ? 70.f : 0.f;
                const gfx::Rect at{card.x + 26.f, y, card.w - 36.f - linkW, kNoteH};
                ctx.r.drawText({at.x, y}, elideTip(ctx.r, sl.text, kMeta, at.w, at, tips_), kMeta, sl.excluded ? c.textMuted : c.text);
                if (sl.removable) {
                    const std::string link = sl.excluded ? "remettre" : "retirer";
                    ctx.r.drawText({card.right() - 10.f - ctx.r.measure(link, kMeta).width, y}, link, kMeta, c.info);
                    tips_.emplace_back(gfx::Rect{card.right() - 80.f, y, 70.f, kNoteH},
                                       sl.excluded ? std::string("Remettre cette \xC3\xA9tape dans le plan.")
                                                   : std::string("Retirer cette \xC3\xA9tape facultative (rien n'en d\xC3\xA9pend) : le plan est recalcul\xC3\xA9."));
                } else if (p->state == "appliquee" && !sl.kind.empty()) {
                    tips_.emplace_back(at, sl.text + "\nClic : ouvrir " + sl.name + ".");
                }
                y += kNoteH;
            }
            if (!b.stepsMore.empty()) {
                ctx.r.drawText({card.x + 26.f, y}, b.stepsMore, kMeta, c.textMuted);
                y += kNoteH;
            }
            y += 4.f;
        }
    }
    if (p->state != "proposee") {
        for (const auto& rl : b.reportLines) {
            const gfx::Color ink = rl.tone == 1 ? c.ok : rl.tone == 2 ? c.warning : rl.tone == 3 ? c.error : c.text;
            if (rl.tone == 4) bold(ctx.r, {card.x + 12.f, y}, rl.text, kMeta, ink);
            else ctx.r.drawText({card.x + 12.f, y}, rl.text, kMeta, ink);
            y += kNoteH;
        }
        if (!b.reportLines.empty()) y += 4.f;
        for (const auto& l : b.doneLines) {
            ctx.r.drawText({card.x + 12.f, y + 2.f}, l, kMeta, b.applied == Assistant::Applied::Undone ? c.warning : c.textMuted);
            y += kNoteH;
        }
        return;
    }
    // L'apercu : la copie de la vue, dessinee par les painters du logiciel.
    const gfx::Rect area{card.x + 10.f, y, card.w - 20.f, 100.f};
    ctx.r.fillRect(area, c.windowBg);
    if ((t.pending() && t.job) || a.applying(b.message)) {
        // 1.20.0 : une grosse proposition se verifie (1.20.1 : s'applique) sur un fil de fond - l'avancement, l'interface
        // reste libre.
        const bool applyingNow = a.applying(b.message);
        float frac = applyingNow ? a.applyProgress() : 0.f;
        if (!applyingNow) {
            const std::size_t done = std::min(t.job->done.load(), t.job->total), total = std::max<std::size_t>(1, t.job->total);
            frac = static_cast<float>(done) / static_cast<float>(total);
        }
        const std::string line = std::string(applyingNow ? "Application" : "V\xC3\xA9rification") + " de la proposition ("
                                 + std::to_string(p->ops.size()) + " op\xC3\xA9rations)\xE2\x80\xA6 " + std::to_string(static_cast<int>(frac * 100.f + 0.5f)) + " %";
        ctx.r.drawText({area.x + 10.f, area.y + area.h / 2.f - kNoteH - 2.f}, line, kMeta, c.text);
        const gfx::Rect track{area.x + 10.f, area.y + area.h / 2.f + 2.f, area.w - 20.f, 8.f};
        ctx.r.fillRoundedRect(track, alpha(c.info, 40), 4.f);
        if (frac > 0.f) ctx.r.fillRoundedRect({track.x, track.y, std::max(8.f, track.w * frac), track.h}, c.info, 4.f);
        ctx.r.drawText({area.x + 10.f, track.bottom() + 4.f}, "Tu peux continuer \xC3\xA0 travailler : la carte se met \xC3\xA0 jour seule.", kMeta, c.textMuted);
    } else if (t.preview.ok && t.preview.view.width > 0 && t.preview.view.height > 0) {
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
    for (std::size_t k = 0; k < b.notes.size(); ++k) {
        const auto& n = b.notes[k];
        const float top = y;
        ui::drawIcon(ctx.r, n.error ? ui::Icon::Error : ui::Icon::Warning, {card.x + 12.f, y + 1.f, 14.f, 14.f}, n.error ? c.error : c.warning);
        for (const auto& l : n.lines) {
            ctx.r.drawText({card.x + 32.f, y}, l, kMeta, n.error ? c.text : c.textMuted);
            y += kNoteH;
        }
        // 1.22.1 : sa place (le clic droit : les corrections possibles), et le dire a qui la survole.
        const gfx::Rect spot{card.x + 8.f, top, card.w - 16.f, std::max(kNoteH, y - top)};
        noteSpots_.push_back({spot, b.message, k});
        tips_.emplace_back(spot, n.message + "\nClic droit : les corrections possibles.");
    }
}

// ---- 1.22.5 : le processus de developpement d'une carte -------------------------------------------------------------------
bool AiChatView::processOpen(int message) const {
    const Proposal* p = panel_.assistant().proposal(message);
    if (!p) return false;
    if (const auto it = procOpen_.find(p->id); it != procOpen_.end()) return it->second;
    return panel_.assistant().processWorking(message);   // par defaut : deplie pendant le travail
}

void AiChatView::setProcessOpen(int message, bool open) {
    const Proposal* p = panel_.assistant().proposal(message);
    if (!p) return;
    procOpen_[p->id] = open;
    stick_ = false;
    measure();
    placeCards();
    invalidate();
}

std::vector<std::string> AiChatView::processLines(int message) const {
    std::vector<std::string> out;
    for (const auto& b : blocks_)
        if (b.message == message && b.procY > 0.f) {
            out.push_back(b.procHead);
            for (const auto& l : b.procLines) out.push_back(l.text);
        }
    return out;
}

gfx::Rect AiChatView::processRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && b.procY > 0.f) {
            const gfx::Rect card = cardRect(message);
            return {card.x + 10.f, card.y + b.procY, card.w - 20.f, kNoteH + 2.f};
        }
    return {};
}

bool AiChatView::clickProcessLine(int message, std::size_t line) {
    const Block* bl = nullptr;
    for (const auto& b : blocks_)
        if (b.message == message) bl = &b;
    if (!bl || line >= bl->procLines.size() || !bl->procLines[line].finding) return false;
    // la proposition du constat, ou celle qui l'a remplacee (la corrigee) : la previsualisation de celle qui est a decider
    const auto& as = panel_.assistant();
    int target = as.messageOfProposal(bl->procLines[line].proposal);
    for (int hops = 0; target >= 0 && hops < 8; ++hops) {
        const Proposal* p = as.proposal(target);
        if (!p || p->state != "remplacee") break;
        const std::string next = p->extra.isObject() ? p->extra.str("remplaceePar") : std::string{};
        if (next.empty()) break;
        target = as.messageOfProposal(next);
    }
    const Proposal* p = target >= 0 ? as.proposal(target) : nullptr;
    if (!p || p->state != "proposee") {
        panel_.say("Ce constat vient d'une proposition qui n'est plus \xC3\xA0 d\xC3\xA9" "cider : sa correction le reprend (ou elle est appliqu\xC3\xA9" "e).");
        return true;
    }
    panel_.openPreview(target);
    return true;
}

std::string AiChatView::liveTooltip(gfx::Point mouse) const { return tipAt(tips_, mouse); }

void AiChatView::onPaint(const ui::PaintContext& ctx) {
    XPG_MESURE("ia.peinture.discussion");   // 1.22.2
    const auto& c = ctx.theme.color;
    const auto bb = bounds();
    tips_.clear();
    copySpots_.clear();
    noteSpots_.clear();   // 1.22.1
    billSpots_.clear();   // 1.22.6
    historySpot_ = {};
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    ctx.r.pushClip(bb);
    const float meta = ctx.r.lineHeight(kMeta);
    const int generating = a.generatingMessage();
    codeSpots_.clear();   // 1.50.1
    const float metaStep = ui::lineHeight(kMeta) + 6.f;
    for (const auto& b : blocks_) {
        const float top = bb.y + b.y - offset_;
        if (top > bb.bottom() || top + b.h < bb.y) continue;
        const auto& m = conv->messages[static_cast<std::size_t>(b.message)];
        const bool live = b.message == generating;
        // 1.50.1 : le message choisi (un clic, ou celui qu'une citation montre).
        if (!selected_.empty() && b.key == selected_) {
            const float right = bb.right() - (content_ > viewHeight() ? 12.f : 2.f);
            ctx.r.fillRect({bb.x + 2.f, top - 4.f, std::max(0.f, right - bb.x - 2.f), b.h + 8.f}, c.selectionBg.withAlpha(70));
            ctx.r.fillRect({bb.x + 2.f, top - 4.f, 3.f, b.h + 8.f}, c.accent);
        }
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
            // 1.50.1 : l'extrait cite (un filet a la couleur d'accent, le texte discret) au-dessus du texte.
            if (!b.quote.empty()) {
                ctx.r.fillRect({bub.x + 10.f, ly + 2.f, 2.f, std::max(0.f, metaStep - 6.f)}, c.accent);
                ctx.r.drawText({bub.x + 18.f, ly + 1.f}, b.quote, kMeta, c.textMuted);
                ly += metaStep;
            }
            if (!m.text.empty())
                for (std::size_t li = 0; li < b.shown && li < b.lines.size(); ++li) {
                    const auto& l = b.lines[li];
                    ly += l.gapBefore;
                    for (const auto& r : l.runs) ctx.r.drawText({bub.x + 10.f + r.x, ly}, r.text, kBody, c.text);
                    ly += l.h;
                }
            if (!b.toggle.empty()) {   // 1.50.1
                ctx.r.drawText({bub.x + 10.f, top + b.toggleY + 2.f}, b.toggle, kMeta, c.accent);
                ly = top + b.toggleY + metaStep;
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
        // 1.22.5 : un message du LOGICIEL (la correction demandee d'apres Generer et Compiler) - dit comme tel.
        const bool soft = m.role == "utilisateur";
        ui::drawIcon(ctx.r, soft ? ui::Icon::Wrench : ui::Icon::Assistant, {bb.x + kPad + 2.f, top + 1.f, 14.f, 14.f}, soft ? c.textMuted : c.accent);
        std::string who = (soft ? std::string("Logiciel \xC2\xB7 ") : std::string("Assistant \xC2\xB7 ")) + (live ? std::string("maintenant") : when);
        if (m.state == "arrete") who += " \xC2\xB7 arr\xC3\xAAt\xC3\xA9" "e";
        if (m.state == "erreur") who += " \xC2\xB7 erreur";
        ctx.r.drawText({bb.x + kPad + 22.f, top}, who, kMeta, m.state == "erreur" ? c.error : c.textMuted);
        float ly = top + meta + 4.f;
        const float tx = bb.x + kPad + 4.f;
        const float textW = std::max(80.f, bb.w - 2 * kPad - 12.f) - 8.f;
        for (std::size_t li = 0; li < b.shown && li < b.lines.size(); ++li) {   // 1.50.1 : reduit, les lignes montrees
            const auto& l = b.lines[li];
            ly += l.gapBefore;
            // 1.20.2 : un bloc de code - son fond (tout le bloc), sa langue, Copier.
            if (l.codeHead) {
                float blockH = l.h;
                std::size_t codeRows = 0;
                for (std::size_t k = li + 1; k < b.lines.size() && b.lines[k].code == l.code && !b.lines[k].codeHead; ++k) {
                    if (k < b.shown) blockH += b.lines[k].gapBefore + b.lines[k].h;
                    if (!b.lines[k].runs.empty()) ++codeRows;
                }
                const gfx::Rect box{tx - 2.f, ly, textW + 4.f, blockH};
                codeSpots_.push_back({box, {b.message, l.code}});   // 1.50.1 : le clic droit << Copier le code >>
                const gfx::Color bg = ctx.theme.brand.codeBg.a ? ctx.theme.brand.codeBg : c.inputBg;
                ctx.r.fillRoundedRect(box, bg, 6.f);
                ctx.r.strokeRect(box, alpha(c.border, 160), 1.f);
                ctx.r.fillRect({box.x + 1.f, box.y + l.h - 1.f, box.w - 2.f, 1.f}, alpha(c.border, 140));
                const float capY = ly + (l.h - meta) / 2.f;
                std::string lang = l.codeLang + (l.codeOpen ? std::string("  \xC2\xB7  en cours\xE2\x80\xA6") : std::string());
                if (li + 1 >= b.shown && codeRows > 0) lang += "  \xC2\xB7  " + std::to_string(codeRows) + (codeRows > 1 ? " lignes" : " ligne");   // 1.50.1 : reduit
                ctx.r.drawText({box.x + 10.f, capY}, lang, kMeta, c.textMuted);
                if (!copied_.empty() && copiedAt_ < 0.0) copiedAt_ = ctx.time;
                if (!copied_.empty() && ctx.time - copiedAt_ > 2.0) {
                    copied_.clear();
                    copiedAt_ = -1.0;
                }
                const bool done = !copied_.empty() && copied_ == l.codeText;
                const std::string label = done ? std::string("\xE2\x9C\x93 Copi\xC3\xA9") : std::string("Copier");
                const float pw = ctx.r.measure(label, kMeta).width + 30.f;
                const gfx::Rect pill{box.right() - 8.f - pw, ly + 3.f, pw, l.h - 6.f};
                ctx.r.fillRoundedRect(pill, done ? alpha(c.ok, 46) : alpha(c.accent, 40), 5.f);
                ui::drawIcon(ctx.r, ui::Icon::Copy, {pill.x + 6.f, pill.y + (pill.h - 14.f) / 2.f, 14.f, 14.f}, done ? c.ok : c.accent);
                ctx.r.drawText({pill.x + 24.f, pill.y + (pill.h - meta) / 2.f}, label, kMeta, done ? c.ok : c.text);
                copySpots_.emplace_back(pill, l.codeText);
                tips_.emplace_back(pill, "Copier ce code dans le presse-papiers (le code exact, sans les balises).");
                ly += l.h;
                continue;
            }
            if (l.code >= 0) {   // une ligne du bloc
                for (const auto& r : l.runs) ctx.r.drawText({tx + r.x, ly + 1.f}, r.text, kBody, c.syntaxString.a ? c.syntaxString : c.text);
                ly += l.h;
                continue;
            }
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
        if (!b.toggle.empty()) ctx.r.drawText({tx, top + b.toggleY + 2.f}, b.toggle, kMeta, c.accent);   // 1.50.1
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
            if (b.showDetail && !a.statusDetail().empty()) {
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
        // 1.22.6 : ce que fait l'IA (sous l'indicateur, ou sous la reponse dont la proposition se verifie), le bilan.
        if (b.actY > 0.f) paintActivity(ctx, b, top);
        if (b.billY > 0.f) paintBill(ctx, b, top);
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
    // 1.50.1 : Repondre et Copier sur le message survole (sur sa ligne d'en-tete, sur un fond).
    if (hover_ >= 0 && hovered())
        for (int k = 0; k < 2; ++k) {
            const gfx::Rect hb = hoverButton(hover_, k);
            if (hb.w <= 0.f) continue;
            ctx.r.fillRoundedRect(hb, c.panelBg, 4.f);
            ctx.r.strokeRect(hb, c.border, 1.f);
            ui::drawIcon(ctx.r, k == 0 ? ui::Icon::Undo : ui::Icon::Copy, hb.inset(4.f, 2.f), c.text);
            tips_.emplace_back(hb, k == 0 ? std::string("R\xC3\xA9pondre \xC3\xA0 ce message (ta demande le citera)") : std::string("Copier le message (Ctrl+C)"));
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

bool AiChatView::copyCode(const std::string& code) {
    ui::setClipboardText(code);
    copied_ = code;
    copiedAt_ = -1.0;   // l'heure : au prochain dessin
    panel_.say("Code copi\xC3\xA9 (" + std::to_string(std::count(code.begin(), code.end(), '\n') + (code.empty() ? 0 : 1)) + " lignes).");
    invalidate();
    return true;
}

// ---- 1.22.1 : le clic droit sur une erreur de la carte (AiFixes) --------------------------------------------------------
std::vector<std::string> AiChatView::cardNotes(int message) const {
    std::vector<std::string> out;
    for (const auto& b : blocks_) {
        if (b.message != message) continue;
        for (const auto& n : b.notes) {
            std::string text;
            for (const auto& l : n.lines) text += (text.empty() ? "" : " ") + l;
            out.push_back(std::move(text));
        }
    }
    return out;
}

bool AiChatView::openNoteMenu(int message, std::size_t note) {
    for (const auto& spot : noteSpots_)
        if (spot.message == message && spot.note == note) return openNoteMenuAt(message, note, {spot.at.x + 40.f, spot.at.y + spot.at.h / 2.f});
    const auto bb = bounds();
    return openNoteMenuAt(message, note, {bb.x + bb.w / 2.f, bb.y + bb.h / 2.f});
}

bool AiChatView::openNoteMenuAt(int message, std::size_t note, gfx::Point at) {
    auto& a = panel_.assistant();
    const Proposal* p = a.proposal(message);
    if (!p || p->state != "proposee") return false;
    const Block* block = nullptr;
    for (const auto& b : blocks_)
        if (b.message == message) block = &b;
    if (!block || note >= block->notes.size()) return false;
    const Thumb& t = thumb(message);
    if (t.pending()) return false;   // une grosse proposition encore en verification sur son fil de fond
    const auto& n = block->notes[note];
    const auto lines = n.error ? fixes::errorLines(t.check) : fixes::warningLines(t.check);
    const auto line = std::find_if(lines.begin(), lines.end(), [&](const fixes::Line& l) { return l.message == n.message; });
    if (line == lines.end()) return false;
    const auto doc = a.host().document ? a.host().document() : nullptr;
    const fixes::Menu m = fixes::menuFor(*p, t.check, *line, n.error && doc ? &doc->project : nullptr);
    if (!fixMenu_) {
        fixMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".corriger")));
        links_ += fixMenu_->itemChosen->connect([this](int item) { runNoteFix(item); });
    }
    fixes::fill(*fixMenu_, m, fixActions_);
    fixMessage_ = message;
    fixMenu_->setBounds(bounds());
    fixMenu_->openAtSurface(toSurface(at));
    return true;
}

// ---- 1.22.6 : le clic droit sur un message envoye ---------------------------------------------------------------------------
gfx::Rect AiChatView::bubbleRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && b.user && b.bubble.h > 0.f)
            return {bounds().x + b.bubble.x, bounds().y + b.y - offset_ + b.bubble.y, b.bubble.w, b.bubble.h};
    return {};
}

gfx::Rect AiChatView::messageRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return {bounds().x, bounds().y + b.y - offset_, bounds().w, b.h};
    return {};
}

// 1.50.1 : LE CLIC DROIT SUR UN MESSAGE (une demande, une reponse) - Repondre, Copier, Copier avec l'auteur et l'heure, Copier le
// code, Deballer / Reduire, Tout reduire ; sur une demande envoyee, Creer une mission avec ce message (1.22.6).
bool AiChatView::openMessageMenuAt(int message, gfx::Point at, int codeBlock) {
    const auto* conv = panel_.assistant().current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size())) return false;
    const auto& m = conv->messages[static_cast<std::size_t>(message)];
    if (!msgMenu_) {
        msgMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".message")));
        links_ += msgMenu_->itemChosen->connect([this](int item) { runMessageMenu(item); });
    }
    const auto& a = panel_.assistant();
    const bool live = message == a.generatingMessage();
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
    const std::string writing = "La r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture.";
    std::vector<ui::PopupMenu::Item> items;
    const bool quotable = !chatreply::excerptOf(m.text).empty();
    items.push_back(item("R\xC3\xA9pondre", 11, ui::Icon::Undo, !live && quotable, live ? writing : std::string("Ce message n'a pas de texte \xC3\xA0 citer.")));
    items.push_back(item("Copier", 12, ui::Icon::Copy, !live, writing, "Ctrl+C"));
    items.push_back(item("Copier avec l'auteur et l'heure", 13, ui::Icon::None, !live, writing));
    std::size_t blocks = 0;
    if (markdown::hasCode(m.text))
        for (const auto& seg : markdown::split(m.text)) blocks += seg.kind == markdown::Segment::Kind::Code ? 1 : 0;
    if (blocks > 0 && codeBlock < 0 && blocks == 1) codeBlock = 0;
    if (blocks > 0)
        items.push_back(item("Copier le code", 14, ui::Icon::Copy, codeBlock >= 0 && !live,
                             live ? writing : "Ce message a " + std::to_string(blocks) + " blocs de code : fais le clic droit sur celui \xC3\xA0 copier."));
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    const bool coll = collapsible(message);
    items.push_back(item(coll && !collapsed(message) ? std::string("R\xC3\xA9" "duire") : std::string("D\xC3\xA9" "baller"), 15, ui::Icon::None, coll,
                         "Le message se lit en entier."));
    bool anyOpen = false;
    for (const auto& b : blocks_) anyOpen = anyOpen || (b.collapsible && !b.collapsed);
    items.push_back(item("Tout r\xC3\xA9" "duire", 16, ui::Icon::Collapse, anyOpen, "Aucun message d\xC3\xA9" "ball\xC3\xA9."));
    // 1.22.6 : une demande envoyee - << Creer une mission avec ce message >>.
    if (m.role == "utilisateur" && !(m.extra.isObject() && m.extra.has("logiciel"))) {
        items.push_back(sep);
        ui::PopupMenu::Item create;
        create.label = "Cr\xC3\xA9" "er une mission avec ce message";
        create.icon = ui::Icon::Task;
        create.id = 1;
        if (m.extra.isObject() && m.extra.has("mission")) {
            create.enabled = false;
            create.disabledReason = "Ce message fait d\xC3\xA9j\xC3\xA0 partie d'une mission.";
        } else if (!a.live()) {
            create.enabled = false;
            create.disabledReason = "Une mission travaille avec une IA connect\xC3\xA9" "e (Param\xC3\xA8tres de l'Assistant IA).";
        } else if (a.generating()) {
            create.enabled = false;
            create.disabledReason = "Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture.";
        }
        items.push_back(std::move(create));
    }
    msgMenu_->setItems(std::move(items));
    msgMenuMessage_ = message;
    msgMenuCode_ = codeBlock;
    msgMenu_->setBounds(bounds());
    msgMenu_->openAtSurface(toSurface(at));
    return true;
}

void AiChatView::runMessageMenu(int item) {
    const int msg = std::exchange(msgMenuMessage_, -1);
    const int code = std::exchange(msgMenuCode_, -1);
    if (msg < 0) return;
    switch (item) {
        case 1: (void)panel_.createMissionFromMessage(msg); break;
        case 11: (void)panel_.replyTo(msg); break;
        case 12: (void)copyMessage(msg, false); break;
        case 13: (void)copyMessage(msg, true); break;
        case 14: (void)copyCodeOf(msg, code); break;
        case 15: setExpanded(msg, collapsed(msg)); break;
        case 16: collapseAll(); break;
        default: break;
    }
}

bool AiChatView::openMessageMenu(int message) {
    const gfx::Rect r = bubbleRect(message);
    return openMessageMenuAt(message, r.h > 0.f ? gfx::Point{r.x + r.w / 2.f, r.y + r.h / 2.f} : gfx::Point{bounds().x + bounds().w / 2.f, bounds().y + 40.f});
}

bool AiChatView::openMessageMenu(int message, int codeBlock) {
    const gfx::Rect r = messageRect(message);
    const gfx::Point at = r.h > 0.f ? gfx::Point{r.x + r.w / 2.f, std::clamp(r.y + 20.f, bounds().y, bounds().bottom())} : gfx::Point{bounds().x + bounds().w / 2.f, bounds().y + 40.f};
    return openMessageMenuAt(message, at, codeBlock);
}

// ---- 1.50.1 : repondre, reduire / deballer, copier ---------------------------------------------------------------------------
std::size_t AiChatView::collapseCut(const std::vector<Line>& lines) {
    // Une rangee : une ligne de texte, l'en-tete d'un bloc de code, une ligne de code (pas la marge du bas d'un bloc).
    const auto counts = [](const Line& l) { return !(l.code >= 0 && !l.codeHead && l.runs.empty()); };
    std::size_t rows = 0;
    for (const auto& l : lines) rows += counts(l) ? 1 : 0;
    if (rows <= sessionui::kCollapseLines + 1) return lines.size();
    std::size_t cut = 0, seen = 0;
    while (cut < lines.size() && seen < sessionui::kCollapseLines) seen += counts(lines[cut++]) ? 1 : 0;
    while (cut < lines.size() && !counts(lines[cut])) ++cut;   // la marge du bas d'un bloc montre en entier
    // Jamais au milieu d'un bloc de code : la coupe passe avant lui ; un message qui commence par un bloc garde son en-tete.
    if (cut < lines.size() && lines[cut].code >= 0 && !lines[cut].codeHead) {
        std::size_t head = cut;
        while (head > 0 && !(lines[head].codeHead && lines[head].code == lines[cut].code)) --head;
        cut = head > 0 ? head : 1;
    }
    return std::min(cut, lines.size());
}

std::string AiChatView::keyOf(int message) const {
    const auto* conv = panel_.assistant().current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size())) return {};
    return conv->id + "|" + conv->messages[static_cast<std::size_t>(message)].id;
}

int AiChatView::selectedMessage() const {
    if (selected_.empty()) return -1;
    for (const auto& b : blocks_)
        if (b.key == selected_) return b.message;
    return -1;
}

void AiChatView::selectMessage(int message) {
    const std::string k = keyOf(message);
    if (k == selected_) return;
    selected_ = k;
    invalidate();
}

bool AiChatView::collapsible(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.collapsible;
    return false;
}

bool AiChatView::collapsed(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.collapsed;
    return false;
}

void AiChatView::refold() {
    stick_ = false;
    anchor_ = false;
    anchored_ = false;
    measure();
    offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - viewHeight()));
    placeCards();
    invalidateLayout();
    invalidate();
}

void AiChatView::setExpanded(int message, bool expanded) {
    const std::string k = keyOf(message);
    if (k.empty()) return;
    fold_[k] = expanded;
    refold();
    panel_.say(expanded ? "Message d\xC3\xA9" "ball\xC3\xA9 : il se lit en entier." : "Message r\xC3\xA9" "duit : \xC2\xAB \xE2\x80\xA6 voir tout \xC2\xBB le d\xC3\xA9" "balle.");
}

void AiChatView::collapseAll() {
    std::size_t n = 0;
    for (const auto& b : blocks_)
        if (b.collapsible) {
            n += b.collapsed ? 0 : 1;
            fold_[b.key] = false;
        }
    refold();
    panel_.say(n == 0 ? std::string("Aucun message d\xC3\xA9" "ball\xC3\xA9.") : n == 1 ? std::string("Un message r\xC3\xA9" "duit.") : std::to_string(n) + " messages r\xC3\xA9" "duits.");
}

bool AiChatView::copyMessage(int message, bool withAuthor) {
    const auto& a = panel_.assistant();
    const auto* conv = a.current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size())) return false;
    if (message == a.generatingMessage()) {
        panel_.say("La r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : copie-la quand elle est finie.");
        return false;
    }
    ui::setClipboardText(chatreply::copyText(conv->messages[static_cast<std::size_t>(message)], withAuthor));
    panel_.say(withAuthor ? "Message copi\xC3\xA9 avec l'auteur et l'heure." : "Message copi\xC3\xA9.");
    return true;
}

bool AiChatView::copyCodeOf(int message, int block) {
    const auto* conv = panel_.assistant().current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size()) || block < 0) return false;
    int k = 0;
    for (const auto& seg : markdown::split(conv->messages[static_cast<std::size_t>(message)].text))
        if (seg.kind == markdown::Segment::Kind::Code && k++ == block) return copyCode(seg.text);
    return false;
}

bool AiChatView::goToQuoted(int message) {
    const auto* conv = panel_.assistant().current();
    if (!conv || message < 0 || message >= static_cast<int>(conv->messages.size())) return false;
    const auto q = chatreply::readQuote(conv->messages[static_cast<std::size_t>(message)]);
    if (!q) return false;
    for (std::size_t i = 0; i < conv->messages.size(); ++i)
        if (conv->messages[i].id == q->id && static_cast<int>(i) != message) {
            revealMessage(static_cast<int>(i), false);
            selectMessage(static_cast<int>(i));
            panel_.say("Le message cit\xC3\xA9 (" + q->who + ").");
            return true;
        }
    panel_.say("Le message cit\xC3\xA9 n'est plus dans la conversation.");
    return false;
}

std::string AiChatView::quoteText(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.quote;
    return {};
}

std::string AiChatView::toggleText(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.toggle;
    return {};
}

std::size_t AiChatView::shownLineCount(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message) return b.shown;
    return 0;
}

gfx::Rect AiChatView::quoteRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && !b.quote.empty())
            return {bounds().x + b.textX, bounds().y + b.y - offset_ + b.quoteY, ui::measureWidth(b.quote, kMeta) + 8.f, ui::lineHeight(kMeta) + 6.f};
    return {};
}

gfx::Rect AiChatView::toggleRect(int message) const {
    for (const auto& b : blocks_)
        if (b.message == message && !b.toggle.empty())
            return {bounds().x + b.textX, bounds().y + b.y - offset_ + b.toggleY, ui::measureWidth(b.toggle, kMeta) + 4.f, ui::lineHeight(kMeta) + 6.f};
    return {};
}

gfx::Rect AiChatView::hoverButton(int message, int which) const {
    if (message < 0 || message != hover_ || which < 0 || which > 1) return {};
    const auto& a = panel_.assistant();
    if (message == a.generatingMessage()) return {};
    const auto* conv = a.current();
    if (!conv || message >= static_cast<int>(conv->messages.size())) return {};
    if (which == 0 && chatreply::excerptOf(conv->messages[static_cast<std::size_t>(message)].text).empty()) return {};
    constexpr float kW = 24.f, kH = 18.f, kGap = 2.f;
    const auto bb = bounds();
    for (const auto& b : blocks_) {
        if (b.message != message) continue;
        const float top = bb.y + b.y - offset_;
        const float right = b.user ? bb.right() - kPad - 6.f - b.headW - 8.f : bb.right() - kPad - 6.f - (content_ > viewHeight() ? 8.f : 0.f);
        const float x = which == 1 ? right - kW : right - 2.f * kW - kGap;
        const float minX = b.user ? bb.x + kPad : bb.x + kPad + 22.f + b.headW + 6.f;
        const float y = std::max(top - 1.f, bb.y);   // le haut du message passe sous le bord : les pictogrammes restent dans la vue
        if (x < minX || y + kH > std::min(bb.bottom(), top + b.h)) return {};   // trop etroit, hors de la vue : le clic droit reste
        return {x, y, kW, kH};
    }
    return {};
}

int AiChatView::messageAt(gfx::Point p) const {
    const auto bb = bounds();
    if (!bb.contains(p)) return -1;
    for (const auto& b : blocks_) {
        const float top = bb.y + b.y - offset_;
        if (p.y >= top - 4.f && p.y < top + b.h + 4.f) return b.message;
    }
    return -1;
}

std::vector<std::string> AiChatView::messageMenuItems() const {
    std::vector<std::string> out;
    if (msgMenu_ && msgMenu_->isOpen())
        for (const auto& it : msgMenu_->items())
            if (!it.separator && !it.heading) out.push_back(it.label);
    return out;
}

bool AiChatView::chooseMessageMenuItem(const std::string& label) {
    if (!msgMenu_ || !msgMenu_->isOpen()) return false;
    for (const auto& it : msgMenu_->items())
        if (it.label == label && it.enabled) {
            const int item = it.id;
            msgMenu_->close();
            msgMenu_->itemChosen->emit(item);   // le meme chemin qu'un clic
            return true;
        }
    return false;
}

std::vector<std::string> AiChatView::noteMenuItems() const {
    return fixMenu_ && fixMenu_->isOpen() ? fixes::labels(*fixMenu_) : std::vector<std::string>{};
}

bool AiChatView::chooseNoteMenuItem(const std::string& label) {
    if (!fixMenu_ || !fixMenu_->isOpen()) return false;
    const int item = fixes::idOf(*fixMenu_, label);
    if (item < 0) return false;
    fixMenu_->close();
    fixMenu_->itemChosen->emit(item);   // le meme chemin qu'un clic
    return true;
}

void AiChatView::runNoteFix(int item) {
    const int message = fixMessage_;
    if (item < 0 || static_cast<std::size_t>(item) >= fixActions_.size() || message < 0) return;
    const fixes::Action act = fixActions_[static_cast<std::size_t>(item)];
    using K = fixes::Action::Kind;
    switch (act.kind) {
        case K::Copy:
            ui::setClipboardText(act.value);
            panel_.say("Message copi\xC3\xA9.");
            return;
        case K::AskAi:   // dans la saisie : rien ne part seul
            panel_.askFix(act.value);
            return;
        case K::Pick:    // la previsualisation, son choix du haut sur cette variable
            panel_.openPreviewFor(message, act.name);
            return;
        case K::UnitAsk:   // 1.22.8 : la petite fenetre (l'unite va dans la proposition)
            panel_.askUnit(message, act.name);
            return;
        default: {
            std::string said;
            (void)fixes::apply(panel_.assistant(), message, act, &said);
            panel_.say(said);
            stick_ = false;
            measure();
            placeCards();
            invalidate();
            return;
        }
    }
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
        // 1.50.1 : le message survole (ses pictogrammes Repondre, Copier).
        if (const int mh = messageAt(m->pos); mh != hover_) {
            hover_ = mh;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    // ---- 1.50.1 : le clavier sur le message choisi - Ctrl+C le copie, la touche Menu (Maj+F10) ouvre son clic droit, Haut et
    // Bas en choisissent un autre, Echap rend le clavier a la saisie ; une lettre tapee va dans la saisie. ----
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
        const int sel = selectedMessage();
        if (k->key == ui::Key::C && k->mods.ctrl && !k->mods.shift && !k->mods.alt && sel >= 0) {
            (void)copyMessage(sel, false);
            return ui::EventResult::Consumed;
        }
        if (ui::isContextMenuKey(*k) && sel >= 0) {
            (void)openMessageMenu(sel, -1);
            return ui::EventResult::Consumed;
        }
        if ((k->key == ui::Key::Up || k->key == ui::Key::Down) && k->mods.none() && !blocks_.empty()) {
            const int n = static_cast<int>(blocks_.size());
            const int next = sel < 0 ? n - 1 : std::clamp(sel + (k->key == ui::Key::Up ? -1 : 1), 0, n - 1);
            revealMessage(next, false);
            selectMessage(next);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && sel >= 0) {
            selectMessage(-1);
            panel_.prompt().takeFocus();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* t = std::get_if<ui::TextInput>(&ev); t && focused() && !t->utf8.empty()) {
        panel_.prompt().takeFocus();
        panel_.prompt().insertText(t->utf8);
        return ui::EventResult::Consumed;
    }
    // 1.22.1 : LE CLIC DROIT sur une erreur (ou un avertissement) d'une carte : le menu des corrections.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right)
        for (const auto& spot : noteSpots_)
            if (spot.at.contains(d->pos)) {
                (void)openNoteMenuAt(spot.message, spot.note, d->pos);
                return ui::EventResult::Consumed;
            }
    // 1.22.6 : LE CLIC DROIT sur un message envoye : << Creer une mission avec ce message >>. 1.50.1 : sur tout message (une
    // demande, une reponse) - Repondre, Copier, Copier le code (sur un bloc), Deballer / Reduire, Tout reduire.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right)
        if (const int at = messageAt(d->pos); at >= 0) {
            int code = -1;
            for (const auto& [box, which] : codeSpots_)
                if (which.first == at && box.contains(d->pos)) code = which.second;
            grabFocus();
            selectMessage(at);
            if (openMessageMenuAt(at, d->pos, code)) return ui::EventResult::Consumed;
        }
    // 1.22.6 : << Mission << X >> creee - Ouvrir >> : un clic ouvre son tableau de bord.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left)
        if (const auto* conv = panel_.assistant().current())
            for (const auto& b : blocks_) {
                if (b.message < 0 || b.message >= static_cast<int>(conv->messages.size())) continue;
                const auto& msg = conv->messages[static_cast<std::size_t>(b.message)];
                if (!msg.extra.isObject() || msg.extra.str("logiciel") != "mission-creee") continue;
                if (messageRect(b.message).contains(d->pos)) {
                    (void)panel_.openMission(msg.extra.str("mission"));
                    return ui::EventResult::Consumed;
                }
            }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        for (std::size_t i = 0; i < suggestionRects_.size(); ++i)
            if (suggestionRect(i).contains(d->pos)) {
                (void)panel_.clickSuggestion(i);
                return ui::EventResult::Consumed;
            }
        // 1.20.2 : Copier un bloc de code.
        for (const auto& [at, code] : copySpots_)
            if (at.contains(d->pos)) {
                (void)copyCode(code);
                return ui::EventResult::Consumed;
            }
        // 1.50.1 : les pictogrammes du survol (Repondre, Copier), l'extrait cite (le message cite), << ... voir tout >> / << Reduire >>.
        for (const auto& b : blocks_) {
            if (hoverButton(b.message, 0).contains(d->pos)) {
                (void)panel_.replyTo(b.message);
                return ui::EventResult::Consumed;
            }
            if (hoverButton(b.message, 1).contains(d->pos)) {
                selectMessage(b.message);
                (void)copyMessage(b.message, false);
                return ui::EventResult::Consumed;
            }
            if (quoteRect(b.message).contains(d->pos)) {
                if (!goToQuoted(b.message)) selectMessage(b.message);
                return ui::EventResult::Consumed;
            }
            if (toggleRect(b.message).contains(d->pos)) {
                const int msg = b.message;   // setExpanded refait blocks_
                setExpanded(msg, collapsed(msg));
                return ui::EventResult::Consumed;
            }
        }
        // 1.22.6 : le << + >> d'un bilan (le detail par action) ; l'icone de l'activite (les etapes passees).
        for (const auto& [at, message] : billSpots_)
            if (at.contains(d->pos)) {
                setBillOpen(message, !billOpen(message));
                return ui::EventResult::Consumed;
            }
        if (historySpot_.w > 0.f && historySpot_.contains(d->pos)) {
            setActivityHistoryOpen(!historyOpen_);
            return ui::EventResult::Consumed;
        }
        // 1.22.5 : le processus de developpement - l'en-tete le deplie (ou le replie) ; un constat ouvre la previsualisation.
        for (const auto& b : blocks_) {
            if (b.procY <= 0.f) continue;
            const gfx::Rect card = cardRect(b.message);
            if (card.h <= 0.f || !card.contains(d->pos)) continue;
            const gfx::Rect head{card.x + 10.f, card.y + b.procY, card.w - 20.f, kNoteH + 2.f};
            if (head.contains(d->pos)) {
                setProcessOpen(b.message, !b.procOpen);
                return ui::EventResult::Consumed;
            }
            const float top = head.bottom() + 4.f;
            if (b.procOpen && d->pos.y >= top && d->pos.y < top + static_cast<float>(b.procLines.size()) * kNoteH
                && clickProcessLine(b.message, static_cast<std::size_t>((d->pos.y - top) / kNoteH)))
                return ui::EventResult::Consumed;
        }
        // 1.19.0 : le plan d'action - l'en-tete le deplie ; << retirer >> / << remettre >> une etape facultative ; une
        // etape appliquee ouvre ce qu'elle a cree.
        for (const auto& b : blocks_) {
            if (b.planY <= 0.f) continue;
            const gfx::Rect card = cardRect(b.message);
            if (card.h <= 0.f || !card.contains(d->pos)) continue;
            const Proposal* p = panel_.assistant().proposal(b.message);
            if (!p) continue;
            const gfx::Rect head{card.x + 10.f, card.y + b.planY, card.w - 20.f, kNoteH + 2.f};
            if (head.contains(d->pos)) {
                if (!planOpen_.erase(p->id)) planOpen_.insert(p->id);
                stick_ = false;
                measure();
                placeCards();
                invalidate();
                return ui::EventResult::Consumed;
            }
            const float top = card.y + b.stepsY;
            if (b.steps.empty() || d->pos.y < top || d->pos.y >= top + static_cast<float>(b.steps.size()) * kNoteH) continue;
            const auto& sl = b.steps[static_cast<std::size_t>((d->pos.y - top) / kNoteH)];
            if (sl.removable && d->pos.x >= card.right() - 80.f) {
                (void)panel_.assistant().setExcluded(b.message, sl.op, !sl.excluded);
                panel_.say(sl.excluded ? "\xC3\x89tape remise dans le plan." : "\xC3\x89tape retir\xC3\xA9" "e : le plan est recalcul\xC3\xA9 (ce qui en d\xC3\xA9pendait est ignor\xC3\xA9).");
                stick_ = false;
                measure();
                placeCards();
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (p->state == "appliquee" && !sl.kind.empty()) {
                const auto& reveal = panel_.assistant().host().reveal;
                if (!reveal || !reveal(sl.kind, sl.name, sl.view))
                    panel_.say("\xC2\xAB " + sl.name + " \xC2\xBB n'est plus dans le projet (supprim\xC3\xA9, renomm\xC3\xA9 ou annul\xC3\xA9 depuis).");
                return ui::EventResult::Consumed;
            }
        }
        // Un clic sur une carte (hors de ses boutons) : l'apercu.
        for (const auto& b : blocks_)
            if (b.card.h > 0.f && cardRect(b.message).contains(d->pos) && d->clickCount >= 2) {
                panel_.openPreview(b.message);
                return ui::EventResult::Consumed;
            }
        // 1.50.1 : un clic sur un message (hors de sa carte) le choisit : Ctrl+C le copie, le clic droit le reprend.
        if (const int at = messageAt(d->pos); at >= 0 && !cardRect(at).contains(d->pos)) {
            grabFocus();
            selectMessage(at);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app::ai
