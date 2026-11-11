// =============================================================================
//  license_admin/gui/PageSecurity.cpp - 1.33.0 : PARAMETRES DE SECURITE (la cle
//  de l'editeur) dans XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre)
// -----------------------------------------------------------------------------
//  La carte de la cle (identifiant, nom, empreinte, protection, fichier, dossier ;
//  CLE D'ESSAI dite en clair). Sans cle : la creer (une seule fois, apres une
//  confirmation) ou restaurer une sauvegarde. Avec : la sauvegarder (phrase tapee
//  deux fois), verifier une sauvegarde, exporter la cle publique (PublisherKey.hpp,
//  ce qui est compile dans les Studios). Puis ce qui protege le poste.
//
//  LES PHRASES : des champs masques, vides des que le travail est lance ; jamais
//  dans un message. Argon2id (sauvegarder, verifier, restaurer) tourne par
//  runSlow, sur un Workspace a lui (le fil de l'interface peut relire le sien
//  pendant ce temps : rien n'est partage).
// =============================================================================
#include "Page.hpp"

#include "../../deploy/Crypto.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace license_admin::gui {

namespace {

// =============================================================================================================
//  LA PAGE EN BLOCS (le meme petit outil que PageService.cpp) : des cartes, du texte qui passe a la ligne, des
//  rangees de boutons, des champs avec leur etiquette - de haut en bas, dans la largeur donnee.
// =============================================================================================================
std::size_t nextChar(const std::string& s, std::size_t i) {
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

// Les mots d'une ligne ; la ponctuation francaise reste avec son mot (jamais << seul en fin de ligne, ni : ; ? ! >> ou
// le separateur de menu en debut de ligne).
std::vector<std::string> wordsOf(const std::string& para) {
    std::vector<std::string> words;
    bool glueNext = false;
    for (std::size_t i = 0; i < para.size();) {
        std::size_t j = para.find(' ', i);
        if (j == std::string::npos) j = para.size();
        std::string w = para.substr(i, j - i);
        i = j < para.size() ? j + 1 : j;
        if (w.empty()) continue;
        const bool closing = w == ":" || w == ";" || w == "?" || w == "!" || w == "\xC2\xBB" || w == "\xE2\x80\xBA" || w == "=";
        if (!words.empty() && (glueNext || closing)) words.back() += " " + w;
        else words.push_back(std::move(w));
        glueNext = words.back().size() >= 2 && words.back().compare(words.back().size() - 2, 2, "\xC2\xAB") == 0;
    }
    return words;
}

void wrapParagraph(const std::string& para, gfx::FontId font, float width, std::vector<std::string>& out) {
    if (para.empty()) {
        out.emplace_back();
        return;
    }
    std::string cur;
    for (std::string& word : wordsOf(para)) {
        std::string candidate = cur.empty() ? word : cur + " " + word;
        if (ui::measureWidth(candidate, font) <= width) {
            cur = std::move(candidate);
            continue;
        }
        if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
        // Un mot plus long que la ligne (un chemin) : coupe ou il faut.
        while (word.size() > 1 && ui::measureWidth(word, font) > width) {
            std::size_t cut = 0;
            for (std::size_t k = 0; k < word.size();) {
                const std::size_t n = nextChar(word, k);
                if (ui::measureWidth(std::string_view(word).substr(0, n), font) > width) break;
                cut = n;
                k = n;
            }
            if (cut == 0) cut = nextChar(word, 0);
            out.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        cur = std::move(word);
    }
    if (!cur.empty()) out.push_back(std::move(cur));
}

std::vector<std::string> wrapText(const std::string& text, gfx::FontId font, float width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        wrapParagraph(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start), font, std::max(20.f, width), lines);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

struct Block {
    enum class Kind : std::uint8_t { CardBegin, CardEnd, Heading, Text, Note, Bullet, Pair, Code, Row, Field, Space };
    Kind                     kind{Kind::Text};
    std::string              text, value;
    ui::Tone                 tone{ui::Tone::None};
    std::vector<ui::Widget*> widgets;
    float                    width{0.f};
};

class Blocks {
public:
    Blocks& card(std::string title, ui::Tone tone = ui::Tone::None) { return add(Block::Kind::CardBegin, std::move(title), tone); }
    Blocks& end() { return add(Block::Kind::CardEnd, {}, ui::Tone::None); }
    Blocks& heading(std::string t, ui::Tone tone = ui::Tone::None) { return add(Block::Kind::Heading, std::move(t), tone); }
    Blocks& text(std::string t, ui::Tone tone = ui::Tone::None) { return add(Block::Kind::Text, std::move(t), tone); }
    Blocks& note(std::string t, ui::Tone tone = ui::Tone::Muted) { return add(Block::Kind::Note, std::move(t), tone); }
    Blocks& bullet(std::string t, ui::Tone tone = ui::Tone::None) { return add(Block::Kind::Bullet, std::move(t), tone); }
    Blocks& code(std::string t) { return add(Block::Kind::Code, std::move(t), ui::Tone::None); }
    Blocks& pair(std::string label, std::string v, ui::Tone tone = ui::Tone::None) {
        add(Block::Kind::Pair, std::move(label), tone);
        items.back().value = std::move(v);
        return *this;
    }
    Blocks& row(std::vector<ui::Widget*> ws) {
        add(Block::Kind::Row, {}, ui::Tone::None);
        items.back().widgets = std::move(ws);
        return *this;
    }
    Blocks& field(std::string label, ui::Widget* w, float width = 0.f, std::vector<ui::Widget*> after = {}) {
        add(Block::Kind::Field, std::move(label), ui::Tone::None);
        items.back().widgets.push_back(w);
        for (auto* a : after) items.back().widgets.push_back(a);
        items.back().width = width;
        return *this;
    }
    Blocks& space(float h) {
        add(Block::Kind::Space, {}, ui::Tone::None);
        items.back().width = h;
        return *this;
    }
    std::vector<Block> items;

private:
    Blocks& add(Block::Kind k, std::string t, ui::Tone tone) {
        Block b;
        b.kind = k;
        b.text = std::move(t);
        b.tone = tone;
        items.push_back(std::move(b));
        return *this;
    }
};

constexpr float kMarginLeft = 24.f;
constexpr float kMarginRight = 16.f;
constexpr float kRowH = 28.f;
constexpr gfx::FontId kCodeFont{20};

// Le corps d'une page : ses widgets (crees une fois, montres quand un bloc les nomme) et ses blocs.
class FlowBody final : public ui::Widget {
public:
    explicit FlowBody(std::string id) : ui::Widget(std::move(id)) {}

    template <class T, class... A>
    T* make(A&&... args) {
        auto w = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = w.get();
        raw->setVisibility(ui::Visibility::Collapsed);
        addChild(std::move(w));
        return raw;
    }

    void setBlocks(std::vector<Block> blocks) {
        blocks_ = std::move(blocks);
        std::set<const ui::Widget*> shown;
        for (const auto& b : blocks_)
            for (const auto* w : b.widgets) shown.insert(w);
        for (const auto& c : children()) c->setVisibility(shown.count(c.get()) ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        invalidateLayout();
        invalidate();
    }

    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        const float width = parent() ? parent()->contentRect().w - 12.f : bounds().w;
        h.preferred = {0.f, arrange(0.f, 0.f, std::max(200.f, width - kMarginLeft - kMarginRight), nullptr) + 24.f};
        h.minimum = {200.f, 100.f};
        return h;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        laid_ = Laid{};
        (void)arrange(r.x + kMarginLeft, r.y, std::max(200.f, r.w - kMarginLeft - kMarginRight), &laid_);
        for (const auto& [w, rect] : laid_.places) w->setBounds(rect);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        if (th.font.ui.v != fonts_.ui.v || th.font.lead.v != fonts_.lead.v || th.font.caption.v != fonts_.caption.v) {
            fonts_ = th.font;
            invalidateLayout();
        }
        auto& r = ctx.r;
        const gfx::Color card = th.brand.card.a ? th.brand.card : c.panelBg;
        const gfx::Color cardBorder = th.brand.cardBorder.a ? th.brand.cardBorder : c.border;
        for (const auto& box : laid_.boxes) {
            if (box.code) {
                r.fillRoundedRect(box.r, th.brand.codeBg.a ? th.brand.codeBg : c.inputBg, th.metric.radius);
                r.strokeRect(box.r, c.borderStrong, 1.f);
                continue;
            }
            r.fillRoundedRect(box.r, card, th.metric.radius);
            r.strokeRect(box.r, cardBorder, 1.f);
            if (box.tone != ui::Tone::None) r.fillRect({box.r.x, box.r.y, 3.f, box.r.h}, th.tone(box.tone, c.accent));
        }
        for (const auto& run : laid_.runs) {
            const gfx::Color col = run.tone == ui::Tone::None ? c.text : run.tone == ui::Tone::Muted ? c.textMuted : th.onSurface(th.tone(run.tone, c.text));
            r.drawText(run.at, run.text, run.bold ? gfx::semibold(run.font) : run.font, col);   // 1.41.0 : la demi-grasse
        }
    }

private:
    struct Run {
        gfx::Point  at;
        std::string text;
        gfx::FontId font;
        ui::Tone    tone;
        bool        bold;
    };
    struct Box {
        gfx::Rect r;
        ui::Tone  tone;
        bool      code;
    };
    struct Laid {
        std::vector<Run>                                    runs;
        std::vector<Box>                                    boxes;
        std::vector<std::pair<ui::Widget*, gfx::Rect>>      places;
    };

    static gfx::Size widgetSize(ui::Widget* w) {
        if (auto* rb = dynamic_cast<ui::RadioButton*>(w)) return {ui::measureWidth(rb->label(), gfx::FontId{16}) + 30.f, 24.f};
        const auto h = w->sizeHint();
        if (dynamic_cast<ui::Checkbox*>(w)) return {h.preferred.w + 4.f, std::max(24.f, h.preferred.h)};
        return {std::max(h.preferred.w, 60.f), kRowH};
    }

    float arrange(float x0, float y0, float width, Laid* out) const {
        const float pad = 16.f;
        float x = x0, w = width, y = y0 + 8.f;
        float cardTop = 0.f;
        std::size_t cardIndex = 0;
        const auto lh = [](gfx::FontId f) { return ui::lineHeight(f); };
        const auto emit = [&](float px, float py, std::string t, gfx::FontId f, ui::Tone tone, bool bold) {
            if (out) out->runs.push_back({{px, py}, std::move(t), f, tone, bold});
        };
        const auto paragraph = [&](const std::string& t, float px, float pw, gfx::FontId f, ui::Tone tone, bool bold) {
            const float step = lh(f) + 4.f;
            for (auto& l : wrapText(t, f, pw)) {
                emit(px, y, std::move(l), f, tone, bold);
                y += step;
            }
        };
        const auto place = [&](ui::Widget* wd, gfx::Rect rc) {
            if (out) out->places.emplace_back(wd, rc);
        };
        const auto labelWidth = [](float avail) { return std::clamp(avail * 0.22f, 140.f, 200.f); };
        for (const auto& b : blocks_) {
            switch (b.kind) {
                case Block::Kind::CardBegin:
                    cardTop = y;
                    if (out) {
                        cardIndex = out->boxes.size();
                        out->boxes.push_back({{x0, y, width, 0.f}, b.tone, false});
                    }
                    x = x0 + pad;
                    w = width - 2.f * pad;
                    y += pad - 2.f;
                    if (!b.text.empty()) {
                        paragraph(b.text, x, w, fonts_.lead, ui::Tone::None, true);
                        y += 4.f;
                    }
                    break;
                case Block::Kind::CardEnd:
                    y += pad - 6.f;
                    if (out && cardIndex < out->boxes.size()) out->boxes[cardIndex].r.h = y - cardTop;
                    x = x0;
                    w = width;
                    y += 12.f;
                    break;
                case Block::Kind::Heading:
                    paragraph(b.text, x, w, fonts_.ui, b.tone, true);
                    y += 2.f;
                    break;
                case Block::Kind::Text:
                    paragraph(b.text, x, w, fonts_.ui, b.tone, false);
                    y += 6.f;
                    break;
                case Block::Kind::Note:
                    paragraph(b.text, x, w, fonts_.caption, b.tone, false);
                    y += 6.f;
                    break;
                case Block::Kind::Bullet:
                    emit(x + 4.f, y, "\xE2\x80\xA2", fonts_.ui, b.tone, false);
                    paragraph(b.text, x + 20.f, w - 20.f, fonts_.ui, b.tone, false);
                    y += 2.f;
                    break;
                case Block::Kind::Pair: {
                    const std::string v = b.value.empty() ? std::string("\xE2\x80\x94") : b.value;
                    if (w < 460.f) {
                        paragraph(b.text, x, w, fonts_.caption, ui::Tone::Muted, false);
                        paragraph(v, x, w, fonts_.ui, b.tone, false);
                    } else {
                        const float lw = labelWidth(w);
                        const float top = y;
                        paragraph(b.text, x, lw - 12.f, fonts_.ui, ui::Tone::Muted, false);
                        const float afterLabel = y;
                        y = top;
                        paragraph(v, x + lw, w - lw, fonts_.ui, b.tone, false);
                        y = std::max(y, afterLabel);
                    }
                    y += 2.f;
                    break;
                }
                case Block::Kind::Code: {
                    const float inner = w - 24.f;
                    const auto lines = [&] {
                        std::vector<std::string> ls;
                        std::string cur;
                        for (std::size_t i = 0; i < b.text.size();) {
                            const std::size_t j = b.text.find('-', i);
                            const std::string tok = j == std::string::npos ? b.text.substr(i) : b.text.substr(i, j - i + 1);
                            i = j == std::string::npos ? b.text.size() : j + 1;
                            if (!cur.empty() && ui::measureWidth(cur + tok, kCodeFont) > inner) {
                                ls.push_back(cur);
                                cur.clear();
                            }
                            cur += tok;
                        }
                        if (!cur.empty()) ls.push_back(cur);
                        return ls;
                    }();
                    float widest = 0.f;
                    for (const auto& l : lines) widest = std::max(widest, ui::measureWidth(l, kCodeFont));
                    const float step = lh(kCodeFont) + 6.f;
                    const gfx::Rect box{x, y, std::min(w, widest + 28.f), static_cast<float>(lines.size()) * step + 18.f};
                    if (out) out->boxes.push_back({box, ui::Tone::None, true});
                    float ly = y + 10.f;
                    for (const auto& l : lines) {
                        emit(x + 14.f, ly, l, kCodeFont, ui::Tone::None, false);
                        ly += step;
                    }
                    y += box.h + 10.f;
                    break;
                }
                case Block::Kind::Row: {
                    float cx = x, rowH = 0.f;
                    for (auto* wd : b.widgets) {
                        gfx::Size s = widgetSize(wd);
                        s.w = std::min(s.w, w);
                        if (cx > x && cx + s.w > x + w) {
                            y += rowH + 8.f;
                            cx = x;
                            rowH = 0.f;
                        }
                        place(wd, {cx, y, s.w, s.h});
                        cx += s.w + 8.f;
                        rowH = std::max(rowH, s.h);
                    }
                    y += rowH + 8.f;
                    break;
                }
                case Block::Kind::Field: {
                    if (b.widgets.empty()) break;
                    float extras = 0.f;
                    for (std::size_t i = 1; i < b.widgets.size(); ++i) extras += widgetSize(b.widgets[i]).w + 8.f;
                    float fx = x, avail = w, afterLabel = 0.f;
                    if (w < 460.f) {
                        paragraph(b.text, x, w, fonts_.caption, ui::Tone::Muted, false);
                    } else {
                        const float lw = labelWidth(w);
                        const float top = y;
                        y += (kRowH - lh(fonts_.ui)) * 0.5f;
                        paragraph(b.text, x, lw - 12.f, fonts_.ui, ui::Tone::Muted, false);
                        afterLabel = y;   // une etiquette sur deux lignes : la suite passe dessous
                        y = top;
                        fx = x + lw;
                        avail = w - lw;
                    }
                    const float fw = std::max(120.f, std::min(b.width > 0.f ? b.width : 460.f, avail - extras));
                    place(b.widgets[0], {fx, y, fw, kRowH});
                    float cx = fx + fw + 8.f;
                    for (std::size_t i = 1; i < b.widgets.size(); ++i) {
                        const gfx::Size s = widgetSize(b.widgets[i]);
                        place(b.widgets[i], {cx, y + (kRowH - s.h) * 0.5f, s.w, s.h});
                        cx += s.w + 8.f;
                    }
                    y = std::max(y + kRowH + 8.f, afterLabel + 6.f);
                    break;
                }
                case Block::Kind::Space:
                    y += b.width;
                    break;
            }
        }
        return y - y0;
    }

    std::vector<Block> blocks_;
    Laid               laid_;
    ui::Fonts          fonts_{};
};

// =============================================================================================================
//  UNE CONFIRMATION (un dialogue modal) : un titre, le texte, Annuler et le bouton qui fait.
// =============================================================================================================
menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

class ConfirmDialog;

class ConfirmBody final : public ui::Widget {
public:
    ConfirmBody(ConfirmDialog& owner, const std::string& menuId, std::string heading, std::string text, const std::string& yes, bool danger);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    ConfirmDialog&           owner_;
    std::string              heading_, text_;
    std::vector<std::string> lines_;
    gfx::Rect                panel_{}, closeBox_{};
    ui::Button*              yes_{nullptr};
    ui::Button*              no_{nullptr};
    core::ConnectionScope    links_;
};

class ConfirmDialog final : public menu::WidgetMenu {
public:
    ConfirmDialog(std::string id, std::string heading, std::string text, std::string yes, bool danger, std::function<void()> onYes)
        : menu::WidgetMenu(id), menuId_(std::move(id)), heading_(std::move(heading)), text_(std::move(text)), yes_(std::move(yes)), danger_(danger),
          onYes_(std::move(onYes)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return heading_; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            close(false);
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    // Fermer (le dialogue part a la prochaine image), puis faire : ce qui est fait peut ouvrir un autre dialogue.
    void close(bool yes) {
        if (closed_) return;
        closed_ = true;
        if (entered_) manager().CloseDialog(menu::DialogResult{yes ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
        if (yes && onYes_) {
            auto f = std::move(onYes_);
            f();
        }
    }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<ConfirmBody>(*this, menuId_, heading_, text_, yes_, danger_));
        return core::ok();
    }
    // Ouvert par un gestionnaire de menus (la fenetre) : c'est lui qui le ferme. Sans lui (les essais), rien.
    void onEnter() override { entered_ = true; }

private:
    std::string           menuId_, heading_, text_, yes_;
    bool                  danger_{false};
    std::function<void()> onYes_;
    bool                  entered_{false}, closed_{false};
};

ConfirmBody::ConfirmBody(ConfirmDialog& owner, const std::string& menuId, std::string heading, std::string text, const std::string& yes, bool danger)
    : ui::Widget(menuId + ".corps"), owner_(owner), heading_(std::move(heading)), text_(std::move(text)) {
    yes_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(yes, menuId + ".oui")));
    yes_->setStyle(danger ? ui::Button::Style::Danger : ui::Button::Style::Primary);
    no_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", menuId + ".non")));
    links_ += yes_->clicked->connect([this] { owner_.close(true); });
    links_ += no_->clicked->connect([this] { owner_.close(false); });
}

void ConfirmBody::onLayout() {
    const auto r = bounds();
    const gfx::FontId f{16};
    const float w = std::min(640.f, std::max(360.f, r.w - 40.f));
    lines_ = wrapText(text_, f, w - 40.f);
    const float h = 52.f + static_cast<float>(lines_.size()) * (ui::lineHeight(f) + 4.f) + 16.f + 52.f;
    panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
    lines_ = wrapText(text_, f, panel_.w - 40.f);
    closeBox_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
    ui::dialogframe::keep(*this, closeBox_);
    const float yw = std::max(120.f, yes_->sizeHint().preferred.w), nw = std::max(110.f, no_->sizeHint().preferred.w);
    yes_->setBounds({panel_.right() - 20.f - yw, panel_.bottom() - 46.f, yw, 32.f});
    no_->setBounds({panel_.right() - 28.f - yw - nw, panel_.bottom() - 46.f, nw, 32.f});
}

void ConfirmBody::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const gfx::FontId f{16}, ft{17};
    r.fillRect(panel_, c.panelBg);
    r.strokeRect(panel_, c.borderStrong, 1.f);
    r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
    r.drawText({panel_.x + 20.f, panel_.y + (42.f - r.lineHeight(ft)) * 0.5f}, heading_, ft, c.text);
    r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", ft, c.textMuted);
    r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
    float y = panel_.y + 58.f;
    for (const auto& l : lines_) {
        r.drawText({panel_.x + 20.f, y}, l, f, c.text);
        y += r.lineHeight(f) + 4.f;
    }
}

ui::EventResult ConfirmBody::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
        owner_.close(false);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// Un secret tape : efface en place (au mieux), puis le champ est vide.
void clearSecret(ui::InputText* field) {
    if (!field) return;
    auto& t = const_cast<std::string&>(field->text());
    deploy::crypto::wipe(t.data(), t.size());
    field->setText({});
}

void wipe(std::string& s) {
    deploy::crypto::wipe(s.data(), s.size());
    s.clear();
}

std::string trimmed(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// =============================================================================================================
//  LA PAGE
// =============================================================================================================
class SecurityPage final : public Page {
public:
    explicit SecurityPage(ShellHost host);
    ~SecurityPage() override {
        links_.clear();   // plus de rappel vers le Workspace pendant que la fenetre se defait
        clearSecret(restorePhrase_);
        clearSecret(phrase1_);
        clearSecret(phrase2_);
        clearSecret(checkPhrase_);
        clearSecret(bkPhrase1_);   // 1.44.0
        clearSecret(bkPhrase2_);
        clearSecret(rsPhrase_);
    }
    [[nodiscard]] std::string title() const override { return "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9"; }
    void refresh() override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }
    void refuse(const std::string& why) {
        if (host_.report) host_.report(OpResult::fail(why));
    }
    bool refuseIfBusy() {
        if (!busy()) return false;
        refuse("un travail est en cours (une sauvegarde, une synchro) : attends qu'il finisse");
        return true;
    }
    void rebuild();
    void syncEnabled();
    void updateHint();
    void askCreate();
    void doCreate(const std::string& name, bool test);
    void askRestore();
    void askBackup();
    void askCheck();
    void askExport();
    void askKit();
    // 1.44.0 : la sauvegarde complete (docs/ADMINS.md, par. 9)
    void askBackupFolder();
    void askEnableBackup();
    void askBackupNow();
    void askFullRestore();
    void launch(const std::string& label, std::function<OpResult()> work, bool reloadAfter);
    [[nodiscard]] std::function<bool()> aliveCheck() const {
        std::weak_ptr<bool> w = alive_;
        return [w] { return !w.expired(); };
    }

    ui::ScrollablePanel*     scroll_{nullptr};
    FlowBody*                body_{nullptr};
    ui::Button*              folder_{nullptr};
    ui::InputText*           name_{nullptr};
    ui::Checkbox*            test_{nullptr};
    ui::Button*              create_{nullptr};
    ui::InputText*           restorePhrase_{nullptr};
    ui::Button*              restore_{nullptr};
    ui::InputText*           phrase1_{nullptr};
    ui::InputText*           phrase2_{nullptr};
    ui::Button*              backup_{nullptr};
    ui::InputText*           checkPhrase_{nullptr};
    ui::Button*              check_{nullptr};
    ui::Checkbox*            keep_{nullptr};
    ui::Button*              export_{nullptr};
    ui::Button*              kit_{nullptr};
    // 1.44.0 : la sauvegarde de secours (tout le service) et sa restauration
    ui::InputText*           bkFolder_{nullptr};
    ui::Button*              bkChoose_{nullptr};
    ui::InputText*           bkPhrase1_{nullptr};
    ui::InputText*           bkPhrase2_{nullptr};
    ui::Button*              bkEnable_{nullptr};
    ui::Button*              bkNow_{nullptr};
    ui::InputText*           rsPhrase_{nullptr};
    ui::Checkbox*            rsService_{nullptr};
    ui::InputText*           rsUrl_{nullptr};
    ui::Checkbox*            rsReplace_{nullptr};
    ui::Button*              rsGo_{nullptr};
    std::string              bkHint_;
    ui::Tone                 bkHintTone_{ui::Tone::Muted};
    std::string              hint_;
    ui::Tone                 hintTone_{ui::Tone::Muted};
    std::string              running_;   // le travail lent lance d'ici ("" : aucun)
    bool                     lastBusy_{false};
    float                    headerH_{84.f};
    std::vector<std::string> subtitle_;
    core::ConnectionScope    links_;
    std::shared_ptr<bool>    alive_{std::make_shared<bool>(true)};
};

SecurityPage::SecurityPage(ShellHost host) : Page("securite", std::move(host)) {
    auto scroll = std::make_unique<ui::ScrollablePanel>("securite.defilement");
    scroll->setScrollPolicy(false, true);
    auto body = std::make_unique<FlowBody>("securite.corps");
    body_ = body.get();
    scroll->setContent(std::move(body));
    scroll_ = &static_cast<ui::ScrollablePanel&>(addChild(std::move(scroll)));

    folder_ = body_->make<ui::Button>("Ouvrir le dossier", "securite.dossier");
    folder_->setIcon(ui::Icon::FolderOpen);

    name_ = body_->make<ui::InputText>("securite.nom");
    name_->setPlaceholder("\xC3\x89" "diteur XPG 2026");
    name_->setMaxLength(80);
    test_ = body_->make<ui::Checkbox>("cl\xC3\xA9 d'essai (jamais pour des clients)", "securite.essai");
    create_ = body_->make<ui::Button>("Cr\xC3\xA9" "er la cl\xC3\xA9\xE2\x80\xA6", "securite.creer");
    create_->setStyle(ui::Button::Style::Primary);

    restorePhrase_ = body_->make<ui::InputText>("securite.restaurer.phrase");
    restorePhrase_->setMasked(true);
    restorePhrase_->setPlaceholder("la phrase de passe de la sauvegarde");
    restore_ = body_->make<ui::Button>("Restaurer une sauvegarde\xE2\x80\xA6", "securite.restaurer");

    phrase1_ = body_->make<ui::InputText>("securite.sauver.phrase");
    phrase1_->setMasked(true);
    phrase1_->setPlaceholder("12 caract\xC3\xA8res au moins : une phrase de quelques mots");
    phrase2_ = body_->make<ui::InputText>("securite.sauver.phrase2");
    phrase2_->setMasked(true);
    phrase2_->setPlaceholder("la m\xC3\xAAme, encore");
    backup_ = body_->make<ui::Button>("Sauvegarder la cl\xC3\xA9\xE2\x80\xA6", "securite.sauver");
    backup_->setStyle(ui::Button::Style::Primary);
    backup_->setIcon(ui::Icon::Save);

    checkPhrase_ = body_->make<ui::InputText>("securite.verifier.phrase");
    checkPhrase_->setMasked(true);
    checkPhrase_->setPlaceholder("la phrase de passe de la sauvegarde");
    check_ = body_->make<ui::Button>("V\xC3\xA9rifier une sauvegarde\xE2\x80\xA6", "securite.verifier");

    keep_ = body_->make<ui::Checkbox>("garder les cl\xC3\xA9s d\xC3\xA9j\xC3\xA0 dans le fichier (rotation)", "securite.export.garder");
    export_ = body_->make<ui::Button>("Exporter la cl\xC3\xA9 publique\xE2\x80\xA6", "securite.exporter");
    export_->setIcon(ui::Icon::Export);
    kit_ = body_->make<ui::Button>("Pr\xC3\xA9parer les fichiers pour les Studios\xE2\x80\xA6", "securite.kit-studios");
    kit_->setIcon(ui::Icon::FolderOpen);

    // 1.44.0 : la sauvegarde de secours - le dossier, la phrase (deux fois, masquee ; jamais gardee), chaque jour.
    bkFolder_ = body_->make<ui::InputText>("securite.sauvegarde.dossier");
    bkFolder_->setPlaceholder("un dossier HORS de ce PC de pr\xC3\xA9" "f\xC3\xA9rence (un disque, un partage)");
    bkChoose_ = body_->make<ui::Button>("Choisir le dossier\xE2\x80\xA6", "securite.sauvegarde.choisir");
    bkChoose_->setIcon(ui::Icon::FolderOpen);
    bkPhrase1_ = body_->make<ui::InputText>("securite.sauvegarde.phrase");
    bkPhrase1_->setMasked(true);
    bkPhrase1_->setPlaceholder("12 caract\xC3\xA8res au moins : une phrase de quelques mots");
    bkPhrase2_ = body_->make<ui::InputText>("securite.sauvegarde.phrase2");
    bkPhrase2_->setMasked(true);
    bkPhrase2_->setPlaceholder("la m\xC3\xAAme, encore");
    bkEnable_ = body_->make<ui::Button>("Activer la sauvegarde de chaque jour", "securite.sauvegarde.activer");
    bkEnable_->setStyle(ui::Button::Style::Primary);
    bkEnable_->setIcon(ui::Icon::Save);
    bkNow_ = body_->make<ui::Button>("Sauvegarder maintenant", "securite.sauvegarde.maintenant");
    bkNow_->setIcon(ui::Icon::Save);
    rsPhrase_ = body_->make<ui::InputText>("securite.restauration.phrase");
    rsPhrase_->setMasked(true);
    rsPhrase_->setPlaceholder("la phrase de passe de la sauvegarde");
    rsService_ = body_->make<ui::Checkbox>("Remettre aussi le service (un nouveau d\xC3\xA9ploiement)", "securite.restauration.service");
    rsUrl_ = body_->make<ui::InputText>("securite.restauration.adresse");
    rsUrl_->setPlaceholder("https://xpg-licences-secours.exemple.workers.dev (vide : celle de la sauvegarde)");
    rsReplace_ = body_->make<ui::Checkbox>("Remplacer ce qu'il contient (s'il n'est pas vide)", "securite.restauration.remplacer");
    rsGo_ = body_->make<ui::Button>("Restaurer une sauvegarde compl\xC3\xA8te\xE2\x80\xA6", "securite.restauration");
    rsGo_->setIcon(ui::Icon::Open);
    if (const auto s = ws().backupSettings(); !s.folder.empty()) bkFolder_->setText(s.folder);
    links_ += bkChoose_->clicked->connect([this] { askBackupFolder(); });
    links_ += bkEnable_->clicked->connect([this] { askEnableBackup(); });
    links_ += bkNow_->clicked->connect([this] { askBackupNow(); });
    links_ += rsGo_->clicked->connect([this] { askFullRestore(); });
    links_ += rsService_->stateChanged->connect([this](ui::Checkbox::State) { rebuild(); });
    links_ += bkPhrase1_->textChanged->connect([this](const std::string&) { updateHint(); });
    links_ += bkPhrase2_->textChanged->connect([this](const std::string&) { updateHint(); });

    links_ += folder_->clicked->connect([this] {
        if (host_.openFolder) host_.openFolder(ws().folder());
    });
    links_ += create_->clicked->connect([this] { askCreate(); });
    links_ += restore_->clicked->connect([this] { askRestore(); });
    links_ += backup_->clicked->connect([this] { askBackup(); });
    links_ += check_->clicked->connect([this] { askCheck(); });
    links_ += export_->clicked->connect([this] { askExport(); });
    links_ += kit_->clicked->connect([this] { askKit(); });
    links_ += phrase1_->textChanged->connect([this](const std::string&) { updateHint(); });
    links_ += phrase2_->textChanged->connect([this](const std::string&) { updateHint(); });

    updateHint();
    refresh();
}

void SecurityPage::refresh() {
    lastBusy_ = busy();
    if (!lastBusy_) running_.clear();
    rebuild();
    syncEnabled();
}

void SecurityPage::syncEnabled() {
    const bool b = busy();
    for (auto* w : std::initializer_list<ui::Widget*>{create_, restore_, backup_, check_, export_, test_, keep_, kit_, bkChoose_, bkEnable_, bkNow_, rsGo_})
        w->setEnabled(!b);
    bkNow_->setEnabled(!b && ws().backupSettings().configured);   // 1.44.0
}

void SecurityPage::updateHint() {
    const std::string& a = phrase1_->text();
    const std::string& b = phrase2_->text();
    std::string why;
    if (a.empty() && b.empty()) {
        hint_ = "12 caract\xC3\xA8res au moins, vari\xC3\xA9s (pas seulement des chiffres) : une phrase de quelques mots est le plus s\xC3\xBBr.";
        hintTone_ = ui::Tone::Muted;
    } else if (!Workspace::acceptablePhrase(a, &why)) {
        hint_ = "Trop faible : " + why + ".";
        hintTone_ = ui::Tone::Warning;
    } else if (b.empty()) {
        hint_ = "Phrase acceptable : tape-la une seconde fois, \xC3\xA0 l'identique.";
        hintTone_ = ui::Tone::Info;
    } else if (a != b) {
        hint_ = "Les deux phrases ne sont pas les m\xC3\xAAmes.";
        hintTone_ = ui::Tone::Warning;
    } else {
        hint_ = "Phrase acceptable, la m\xC3\xAAme deux fois.";
        hintTone_ = ui::Tone::Ok;
    }
    // 1.44.0 : la phrase de la sauvegarde de secours, jugee de la meme facon.
    if (bkPhrase1_) {
        const std::string& c = bkPhrase1_->text();
        const std::string& d = bkPhrase2_->text();
        std::string w2;
        const auto say = [this](std::string text, ui::Tone tone) {
            bkHint_ = std::move(text);
            bkHintTone_ = tone;
        };
        if (c.empty() && d.empty()) say({}, ui::Tone::Muted);
        else if (!Workspace::acceptablePhrase(c, &w2)) say("Trop faible : " + w2 + ".", ui::Tone::Warning);
        else if (d.empty()) say("Phrase acceptable : tape-la une seconde fois, \xC3\xA0 l'identique.", ui::Tone::Info);
        else if (c != d) say("Les deux phrases ne sont pas les m\xC3\xAAmes.", ui::Tone::Warning);
        else say("Phrase acceptable, la m\xC3\xAAme deux fois.", ui::Tone::Ok);
    }
    if (body_) rebuild();
}

void SecurityPage::rebuild() {
    const KeyStatus& k = ws().key();
    const bool b = busy();
    Blocks bl;
    if (b && !running_.empty()) bl.text(running_ + " en cours\xE2\x80\xA6 (Argon2id : quelques secondes)", ui::Tone::Info);
    else if (b) bl.text("Un travail est en cours : les boutons reviennent quand il a fini.", ui::Tone::Info);

    const std::string protection = k.protectedHere ? std::string("DPAPI : chiffr\xC3\xA9" "e pour ton compte Windows, sur ce PC seulement")
                                                   : std::string("un fichier lisible par ce compte seulement (pas de DPAPI hors de Windows)");
    bl.card("La cl\xC3\xA9 de l'\xC3\xA9" "diteur", k.present ? (k.test ? ui::Tone::Warning : ui::Tone::Ok) : ui::Tone::Warning);
    if (license_admin::classique()) {
        // 1.49.0 (docs/ADMINS.md, par. 12) : XPG Admin ne cree, ne restaure ni ne lit jamais la cle de l'editeur.
        bl.text("XPG Admin n'utilise jamais la cl\xC3\xA9 de l'\xC3\xA9" "diteur : elle reste sur le poste de l'admin+ (XPG License Manager Admin). Ici, ta "
                "cl\xC3\xA9 d'admin signe ce que tes droits permettent.",
                ui::Tone::Muted);
    } else if (k.present) {
        bl.text("Elle signe toutes les licences, le registre des \xC3\xA9" "ditions et la liste des r\xC3\xA9vocations. Elle ne quitte jamais ce PC.");
        if (k.test)
            bl.heading("CL\xC3\x89 D'ESSAI : pour les essais seulement, jamais pour des clients. Les Studios livr\xC3\xA9s ne la reconnaissent pas.",
                       ui::Tone::Warning);
        bl.pair("Identifiant", k.id);
        bl.pair("Nom", k.name);
        bl.pair("Empreinte", k.fingerprint);
        bl.pair("Protection", protection);
        bl.pair("Fichier", k.path);
    } else {
        std::error_code ec;
        const bool fileThere = !k.path.empty() && std::filesystem::exists(std::filesystem::path(std::u8string(k.path.begin(), k.path.end())), ec);
        if (fileThere) {
            bl.text("La cl\xC3\xA9 de ce poste ne se lit pas : " + k.problem, ui::Tone::Error);
            bl.note("Elle est prot\xC3\xA9g\xC3\xA9" "e pour le compte Windows qui l'a cr\xC3\xA9\xC3\xA9" "e : ouvre l'application avec ce compte-l\xC3\xA0, ou restaure une sauvegarde.",
                    ui::Tone::Muted);
            bl.pair("Fichier", k.path);
        } else {
            bl.text("Pas encore de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste : rien ne peut \xC3\xAAtre sign\xC3\xA9. Cr\xC3\xA9" "e-la (une seule fois), ou restaure une sauvegarde.",
                    ui::Tone::Warning);
        }
    }
    bl.pair("Dossier", ws().folder());
    bl.row({folder_});
    bl.end();

    if (!k.present && license_admin::classique()) {
        // 1.49.0 : ni << Creer la cle >> ni << Restaurer une sauvegarde >> dans XPG Admin
    } else if (!k.present) {
        bl.card("Cr\xC3\xA9" "er la cl\xC3\xA9");
        bl.text("Une seule fois, sur le PC qui signera les licences. Elle est prot\xC3\xA9g\xC3\xA9" "e pour ce compte Windows : copi\xC3\xA9" "e sur un autre PC, elle "
                "ne sert \xC3\xA0 rien. Juste apr\xC3\xA8s : sa sauvegarde.");
        bl.field("Nom de la cl\xC3\xA9", name_, 380.f);
        bl.row({test_});
        bl.note("Une cl\xC3\xA9 d'essai sert aux essais de l'\xC3\xA9" "diteur ; les Studios livr\xC3\xA9s ne la reconnaissent pas. Laisse la case vide pour la vraie cl\xC3\xA9.");
        bl.row({create_});
        bl.end();

        bl.card("Restaurer une sauvegarde");
        bl.text("Ce PC remplace l'ancien, ou l'ancien est tomb\xC3\xA9 en panne : la sauvegarde (.xpgcle) et sa phrase de passe redonnent la m\xC3\xAAme "
                "cl\xC3\xA9, prot\xC3\xA9g\xC3\xA9" "e pour ce compte.");
        bl.field("Phrase de passe", restorePhrase_, 380.f);
        bl.row({restore_});
        bl.note("Tape la phrase, puis choisis le fichier. La phrase est effac\xC3\xA9" "e du champ d\xC3\xA8s que la restauration commence.");
        bl.end();
    } else {
        bl.card("Sauvegarder la cl\xC3\xA9");
        bl.text("Une copie de la cl\xC3\xA9 chiffr\xC3\xA9" "e par une phrase de passe (Argon2id : quelques secondes). Range-la HORS de ce PC (une cl\xC3\xA9 USB au "
                "coffre, un second support) et note la phrase \xC3\xA0 part : avec les deux, n'importe qui peut signer des licences.");
        bl.field("Phrase de passe", phrase1_, 380.f);
        bl.field("La m\xC3\xAAme, encore", phrase2_, 380.f);
        bl.note(hint_, hintTone_);
        bl.row({backup_});
        bl.end();

        bl.card("V\xC3\xA9rifier une sauvegarde");
        bl.text("La phrase ouvre-t-elle bien la sauvegarde ? Rien n'est chang\xC3\xA9. \xC3\x80 faire juste apr\xC3\xA8s chaque sauvegarde, puis de temps en temps.");
        bl.field("Phrase de passe", checkPhrase_, 380.f);
        bl.row({check_});
        bl.end();

        bl.card("Exporter la cl\xC3\xA9 publique");
        bl.text(std::string(k.test ? "TestPublisherKey.hpp" : "PublisherKey.hpp")
                + " : la cl\xC3\xA9 PUBLIQUE, en C++. C'est elle qui est compil\xC3\xA9" "e dans API Studio et IHM Studio pour v\xC3\xA9rifier les licences ; elle ne "
                  "permet pas d'en signer. Mets le fichier dans src/licensing/ puis reconstruis les Studios.");
        if (k.test) bl.note("Une cl\xC3\xA9 d'essai va dans TestPublisherKey.hpp, compil\xC3\xA9 seulement avec l'option XPG_LICENCES_CLE_ESSAI.", ui::Tone::Warning);
        bl.row({keep_});
        bl.note("Coche-la quand tu changes de cl\xC3\xA9 : les licences sign\xC3\xA9" "es avec l'ancienne restent valables.");
        bl.row({export_});
        bl.end();

        // 1.34.0 : ce que l'integrateur compile dans les Studios livres (rien de secret). 1.35.0 : l'adresse du service.
        bl.card("Les fichiers pour les Studios");
        bl.text("Les fichiers PUBLICS que le d\xC3\xA9veloppeur compile dans API Studio et IHM Studio livr\xC3\xA9s, pour que les licences, la "
                "D\xC3\xA9" "couverte et le service en ligne y marchent : PublisherKey.hpp (ta cl\xC3\xA9 publique), registre.xpgreg (le registre sign\xC3\xA9 de "
                "l'archive), ServiceAddress.hpp (l'adresse de ton service des licences) et cle-publique.txt (la m\xC3\xAAme cl\xC3\xA9 publique, pour mettre le "
                "service en ligne), avec un LISEZ-MOI.txt qui dit o\xC3\xB9 les mettre.");
        if (k.test)
            bl.note("Refus\xC3\xA9 avec une cl\xC3\xA9 d'essai : les Studios livr\xC3\xA9s ne doivent jamais la reconna\xC3\xAEtre.", ui::Tone::Warning);
        else
            bl.note("Rien de secret dedans : ni la cl\xC3\xA9 priv\xC3\xA9" "e, ni la cl\xC3\xA9 de synchro, ni un code.");
        bl.row({kit_});
        bl.end();
    }

    // 1.44.0 : LA SAUVEGARDE DE SECOURS (docs/ADMINS.md, par. 9) - l'admin+ ; la restauration : un Admin neuf, ou l'admin+.
    if (ws().role() == Role::Plus) {
        const BackupSettings s = ws().backupSettings();
        bl.card("Sauvegarde de secours (si le service tombe)", s.configured && s.problem.empty() ? ui::Tone::Ok : ui::Tone::Warning);
        bl.text("Une sauvegarde compl\xC3\xA8te, au contenu exact : de quoi tout remettre ailleurs - les admins, leurs droits, les profils, le journal, les "
                "licences, les PC activ\xC3\xA9s, les demandes, les r\xC3\xA9glages, la cl\xC3\xA9 de l'\xC3\xA9" "diteur. Chaque jour d'elle-m\xC3\xAAme, dans le dossier "
                "choisi ; les 30 derni\xC3\xA8res gard\xC3\xA9" "es. Chiffr\xC3\xA9" "e par une phrase de passe que tu choisis (Argon2id) : sans elle, la sauvegarde "
                "ne sert \xC3\xA0 rien.");
        bl.pair("\xC3\x89tat", s.configured ? (s.problem.empty() ? std::string("r\xC3\xA9gl\xC3\xA9" "e : chaque jour, les 30 derni\xC3\xA8res gard\xC3\xA9" "es") : s.problem)
                                               : std::string("pas encore r\xC3\xA9gl\xC3\xA9" "e"),
                s.configured && s.problem.empty() ? ui::Tone::Ok : ui::Tone::Warning);
        if (!s.folder.empty()) bl.pair("Dossier", s.folder);
        if (s.last) {
            std::error_code ec;
            const auto size = s.lastPath.empty() ? 0 : std::filesystem::file_size(std::filesystem::path(std::u8string(s.lastPath.begin(), s.lastPath.end())), ec);
            std::string last = dateTimeFr(s.last);
            if (!ec && size) last += " \xC2\xB7 " + std::to_string((size + 1023) / 1024) + " Ko";
            if (!s.lastPath.empty()) last += " \xC2\xB7 " + std::filesystem::path(std::u8string(s.lastPath.begin(), s.lastPath.end())).filename().string();
            bl.pair("Derni\xC3\xA8re", last);
        }
        bl.field("Dossier", bkFolder_, 420.f, {bkChoose_});
        bl.field("Phrase de passe", bkPhrase1_, 380.f);
        bl.field("La m\xC3\xAAme, encore", bkPhrase2_, 380.f);
        if (!bkHint_.empty()) bl.note(bkHint_, bkHintTone_);
        bl.row({bkEnable_, bkNow_});
        bl.note("La phrase n'est jamais gard\xC3\xA9" "e : seule la cl\xC3\xA9 qui en d\xC3\xA9rive, prot\xC3\xA9g\xC3\xA9" "e pour ce compte (sauvegarde de chaque jour sans la retaper). "
                "Ailleurs, la phrase suffit.");
        bl.end();
    }
    if ((ws().role() == Role::Plus || ws().role() == Role::None) && !license_admin::classique()) {   // 1.49.0 : l'Admin+ seul
        bl.card("Restaurer une sauvegarde compl\xC3\xA8te");
        bl.text("Le service est tomb\xC3\xA9, ou ce PC remplace celui de l'admin+ : le fichier (.xpgsauve) et sa phrase redonnent le dossier du poste et la "
                "cl\xC3\xA9 de l'\xC3\xA9" "diteur (prot\xC3\xA9g\xC3\xA9" "e pour CE compte). \xC2\xAB Remettre aussi le service \xC2\xBB remplit un nouveau d\xC3\xA9ploiement : il "
                "retrouve la m\xC3\xAAme cl\xC3\xA9 et le m\xC3\xAAme certificat, les Studios le reconnaissent.");
        bl.field("Phrase de passe", rsPhrase_, 380.f);
        bl.row({rsService_});
        if (rsService_->isChecked()) {
            bl.field("Adresse du service", rsUrl_, 460.f);
            bl.row({rsReplace_});
        }
        bl.row({rsGo_});
        bl.end();
    }

    bl.card("Ce qui prot\xC3\xA8ge ton poste");
    bl.bullet("La cl\xC3\xA9 priv\xC3\xA9" "e ne quitte jamais ce PC : ni sur le service des licences (il ne re\xC3\xA7oit que des appels sign\xC3\xA9s par elle), ni "
              "dans les Studios, ni dans un fichier envoy\xC3\xA9 \xC3\xA0 un client.");
    bl.bullet("Cette application n'est jamais livr\xC3\xA9" "e : elle reste sur le poste de l'\xC3\xA9" "diteur, comme XPG-License-Admin.exe.");
    bl.bullet("La sauvegarde est chiffr\xC3\xA9" "e par ta phrase de passe : sans la phrase, elle ne sert \xC3\xA0 rien. Garde-les s\xC3\xA9par\xC3\xA9ment.");
    bl.bullet(k.protectedHere ? std::string("La cl\xC3\xA9 de synchro de l'archive est prot\xC3\xA9g\xC3\xA9" "e par DPAPI pour ce compte Windows.")
                              : std::string("La cl\xC3\xA9 de synchro de l'archive est dans un fichier lisible par ce compte seulement."));
    bl.bullet("Un code d'activation n'est montr\xC3\xA9 qu'une fois, \xC3\xA0 sa cr\xC3\xA9" "ation : ni ce poste ni le service ne le gardent (le service n'en a que "
              "l'empreinte).");
    bl.end();
    body_->setBlocks(std::move(bl.items));
}

void SecurityPage::launch(const std::string& label, std::function<OpResult()> work, bool reloadAfter) {
    running_ = label;
    const auto alive = aliveCheck();
    const bool started = host_.runSlow && host_.runSlow(label, std::move(work), [this, alive, reloadAfter](const OpResult&) {
                             if (!alive()) return;
                             running_.clear();
                             if (reloadAfter) ws().reload();
                             refresh();
                         });
    if (!started) {
        running_.clear();
        refuse("un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence");
    }
    lastBusy_ = busy();
    rebuild();
    syncEnabled();
}

// ---------------------------------------------------------------------------------------------------- creer
void SecurityPage::askCreate() {
    if (refuseIfBusy()) return;
    if (ws().key().present) {
        refuse("une cl\xC3\xA9 de l'\xC3\xA9" "diteur existe d\xC3\xA9j\xC3\xA0 sur ce poste (" + ws().key().id + ") : elle n'est jamais remplac\xC3\xA9" "e");
        return;
    }
    const std::string name = trimmed(name_->text());
    if (name.empty()) {
        refuse("donne un nom \xC3\xA0 la cl\xC3\xA9 (par exemple \xC2\xAB \xC3\x89" "diteur XPG 2026 \xC2\xBB)");
        return;
    }
    const bool test = test_->isChecked();
    std::string text = "La cl\xC3\xA9 de l'\xC3\xA9" "diteur \xC2\xAB " + name + " \xC2\xBB va \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e sur ce PC.\n\n"
                       "Elle est cr\xC3\xA9\xC3\xA9" "e UNE SEULE FOIS : elle signe toutes les licences, et elle n'est jamais remplac\xC3\xA9" "e.\n\n"
                       "Elle est prot\xC3\xA9g\xC3\xA9" "e pour ce compte Windows (DPAPI) : copi\xC3\xA9" "e sur un autre PC ou un autre compte, elle ne sert \xC3\xA0 rien.\n\n"
                       "Juste apr\xC3\xA8s, fais sa sauvegarde (Sauvegarder la cl\xC3\xA9\xE2\x80\xA6) et range-la hors de ce PC : sans elle, une panne de ce PC et plus "
                       "aucune licence ne peut \xC3\xAAtre sign\xC3\xA9" "e.";
    if (test) text += "\n\nCL\xC3\x89 D'ESSAI : jamais pour des clients.";
    const auto alive = aliveCheck();
    if (host_.openDialog)
        host_.openDialog(std::make_unique<ConfirmDialog>("securite.creer.confirmation", test ? "Cr\xC3\xA9" "er une cl\xC3\xA9 d'essai" : "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur",
                                                         std::move(text), "Cr\xC3\xA9" "er la cl\xC3\xA9", false, [this, alive, name, test] {
                                                             if (alive()) doCreate(name, test);
                                                         }));
}

void SecurityPage::doCreate(const std::string& name, bool test) {
    if (refuseIfBusy()) return;
    const OpResult r = ws().createKey(name, test);
    if (r.ok) {
        name_->setText({});
        test_->setState(ui::Checkbox::State::Unchecked);
    }
    if (host_.report) host_.report(r);
    refresh();
}

// ---------------------------------------------------------------------------------------------------- restaurer
void SecurityPage::askRestore() {
    if (refuseIfBusy()) return;
    if (ws().key().present) {
        refuse("une cl\xC3\xA9 de l'\xC3\xA9" "diteur est d\xC3\xA9j\xC3\xA0 sur ce poste (" + ws().key().id + ")");
        return;
    }
    if (restorePhrase_->text().empty()) {
        refuse("tape d'abord la phrase de passe de la sauvegarde, puis choisis le fichier");
        return;
    }
    ui::FilePick p;
    p.title = "Restaurer la cl\xC3\xA9 de l'\xC3\xA9" "diteur";
    p.filters = {{"Sauvegarde de la cl\xC3\xA9 (.xpgcle)", "xpgcle"}, {"Tous les fichiers", "*"}};
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;   // annule : la phrase reste dans son champ
            if (refuseIfBusy()) return;
            std::string phrase = restorePhrase_->text();
            clearSecret(restorePhrase_);
            if (phrase.empty()) {
                refuse("tape d'abord la phrase de passe de la sauvegarde");
                return;
            }
            launch("Restauration de la cl\xC3\xA9",
                   [folder = ws().folder(), path, phrase = std::move(phrase)]() mutable {
                       Workspace w(folder);
                       OpResult r = w.restoreKey(path, phrase);
                       wipe(phrase);
                       return r;
                   },
                   true);
        });
}

// ---------------------------------------------------------------------------------------------------- sauvegarder
void SecurityPage::askBackup() {
    if (refuseIfBusy()) return;
    if (!ws().key().present) {
        refuse(ws().key().problem);
        return;
    }
    std::string why;
    if (!Workspace::acceptablePhrase(phrase1_->text(), &why)) {
        refuse(why);
        return;
    }
    if (phrase2_->text() != phrase1_->text()) {
        refuse("les deux phrases ne sont pas les m\xC3\xAAmes : tape-la deux fois, \xC3\xA0 l'identique");
        return;
    }
    ui::FilePick p;
    p.title = "Sauvegarder la cl\xC3\xA9 de l'\xC3\xA9" "diteur";
    p.filters = {{"Sauvegarde de la cl\xC3\xA9 (.xpgcle)", "xpgcle"}};
    p.start = "cle-editeur.xpgcle";
    p.save = true;
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;   // annule : les champs restent
            if (refuseIfBusy()) return;
            std::string phrase = phrase1_->text();
            std::string again = phrase2_->text();
            const bool same = phrase == again;
            wipe(again);
            clearSecret(phrase1_);
            clearSecret(phrase2_);
            std::string why2;
            if (!same || !Workspace::acceptablePhrase(phrase, &why2)) {
                wipe(phrase);
                refuse(same ? why2 : std::string("les deux phrases ne sont pas les m\xC3\xAAmes"));
                return;
            }
            const std::string ext = license_admin::kKeyBackupExtension;
            if (path.size() < ext.size() || path.compare(path.size() - ext.size(), ext.size(), ext) != 0) path += ext;
            launch("Sauvegarde de la cl\xC3\xA9",
                   [folder = ws().folder(), path, phrase = std::move(phrase)]() mutable {
                       Workspace w(folder);
                       OpResult r = w.backupKey(phrase, path);
                       wipe(phrase);
                       return r;
                   },
                   false);
        });
}

// ---------------------------------------------------------------------------------------------------- verifier
void SecurityPage::askCheck() {
    if (refuseIfBusy()) return;
    if (checkPhrase_->text().empty()) {
        refuse("tape d'abord la phrase de passe de la sauvegarde, puis choisis le fichier");
        return;
    }
    ui::FilePick p;
    p.title = "V\xC3\xA9rifier une sauvegarde de la cl\xC3\xA9";
    p.filters = {{"Sauvegarde de la cl\xC3\xA9 (.xpgcle)", "xpgcle"}, {"Tous les fichiers", "*"}};
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;
            if (refuseIfBusy()) return;
            std::string phrase = checkPhrase_->text();
            clearSecret(checkPhrase_);
            if (phrase.empty()) {
                refuse("tape d'abord la phrase de passe de la sauvegarde");
                return;
            }
            launch("V\xC3\xA9rification de la sauvegarde",
                   [folder = ws().folder(), path, phrase = std::move(phrase)]() mutable {
                       Workspace w(folder);
                       OpResult r = w.checkBackup(path, phrase);
                       wipe(phrase);
                       return r;
                   },
                   false);
        });
}

// ---------------------------------------------------------------------------------------------------- exporter
void SecurityPage::askExport() {
    if (refuseIfBusy()) return;
    const KeyStatus& k = ws().key();
    if (!k.present) {
        refuse(k.problem);
        return;
    }
    ui::FilePick p;
    p.title = "Exporter la cl\xC3\xA9 publique";
    p.filters = {{"En-t\xC3\xAAte C++ (.hpp)", "hpp"}};
    p.start = k.test ? license_admin::kTestPublisherHeaderName : license_admin::kPublisherHeaderName;
    p.save = true;
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;
            if (refuseIfBusy()) return;
            const OpResult r = ws().exportPublicKey(path, keep_->isChecked());
            if (host_.report) host_.report(r);
            refresh();
        });
}

// ---------------------------------------------------------------------------------------------------- le kit des Studios
// ---------------------------------------------------------------------------------------------------- 1.44.0 : la sauvegarde de secours
void SecurityPage::askBackupFolder() {
    ui::FilePick p;
    p.title = "Le dossier des sauvegardes de secours";
    p.folder = true;
    p.start = bkFolder_->text();
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;
            bkFolder_->setText(path);
            rebuild();
        });
}

void SecurityPage::askEnableBackup() {
    if (refuseIfBusy()) return;
    const std::string folder = trimmed(bkFolder_->text());
    if (folder.empty()) {
        refuse("choisis d'abord le dossier des sauvegardes (Choisir le dossier\xE2\x80\xA6)");
        return;
    }
    std::string why;
    if (!Workspace::acceptablePhrase(bkPhrase1_->text(), &why)) {
        refuse(why);
        return;
    }
    if (bkPhrase2_->text() != bkPhrase1_->text()) {
        refuse("les deux phrases ne sont pas les m\xC3\xAAmes : tape-la deux fois, \xC3\xA0 l'identique");
        return;
    }
    std::string phrase = bkPhrase1_->text();
    clearSecret(bkPhrase1_);
    clearSecret(bkPhrase2_);
    updateHint();
    // Ces fonctions sont const (elles relisent le dossier) : le Workspace de la page, son heure aussi.
    launch("R\xC3\xA9glage de la sauvegarde de secours",
           [w = static_cast<const Workspace*>(&ws()), dir = folder, phrase = std::move(phrase)]() mutable {
               OpResult r = w->configureBackup(dir, phrase);
               wipe(phrase);
               if (r.ok) {   // et la premiere, tout de suite
                   std::string path;
                   const OpResult n = w->backupNow(&path);
                   r.details.push_back(n.ok ? "Premi\xC3\xA8re sauvegarde : " + path : "La premi\xC3\xA8re sauvegarde n'a pas pu se faire : " + n.message);
                   if (!n.ok) r.ok = n.ok;
               }
               return r;
           },
           false);
}

void SecurityPage::askBackupNow() {
    if (refuseIfBusy()) return;
    launch("Sauvegarde de secours",
           [w = static_cast<const Workspace*>(&ws())] {
               std::string path;
               OpResult r = w->backupNow(&path);
               if (r.ok && !path.empty()) r.output = path;
               return r;
           },
           false);
}

void SecurityPage::askFullRestore() {
    if (refuseIfBusy()) return;
    if (rsPhrase_->text().empty()) {
        refuse("tape d'abord la phrase de passe de la sauvegarde, puis choisis le fichier");
        return;
    }
    ui::FilePick p;
    p.title = "Restaurer une sauvegarde compl\xC3\xA8te";
    p.filters = {{"Sauvegarde compl\xC3\xA8te (.xpgsauve)", "xpgsauve"}, {"Tous les fichiers", "*"}};
    const auto alive = aliveCheck();
    if (!host_.pickFile) return;
    host_.pickFile(p, [this, alive](std::string path) {
        if (!alive() || path.empty()) return;
        BackupInfo info;
        const OpResult d = Workspace::describeBackup(path, &info);
        if (!d.ok) {
            refuse(d.message);
            return;
        }
        RestoreOptions o;
        o.restoreFolder = true;
        o.replaceKey = false;
        o.restoreService = rsService_->isChecked();
        o.serviceUrl = trimmed(rsUrl_->text());
        o.replaceService = rsReplace_->isChecked();
        std::string text = d.message + "\n\n";
        for (const auto& x : d.details) text += x + "\n";
        text += "\nLe dossier du poste revient tel qu'il \xC3\xA9tait ce jour-l\xC3\xA0 ; la cl\xC3\xA9 de l'\xC3\xA9" "diteur est r\xC3\xA9\xC3\xA9" "crite pour ce compte.";
        if (o.restoreService) text += "\n\nLe service " + (o.serviceUrl.empty() ? info.serviceUrl : o.serviceUrl) + " est rempli avec son contenu" +
                                      (o.replaceService ? " (ce qu'il contient est remplac\xC3\xA9)." : " (il doit \xC3\xAAtre vide).");
        const auto alive2 = aliveCheck();
        if (host_.openDialog)
            host_.openDialog(std::make_unique<ConfirmDialog>("securite.restauration.confirmation", "Restaurer la sauvegarde compl\xC3\xA8te", std::move(text), "Restaurer",
                                                             true, [this, alive2, path, o] {
                                                                 if (!alive2() || refuseIfBusy()) return;
                                                                 std::string phrase = rsPhrase_->text();
                                                                 clearSecret(rsPhrase_);
                                                                 launch("Restauration compl\xC3\xA8te",
                                                                        [w = static_cast<const Workspace*>(&ws()), path, o, phrase = std::move(phrase)]() mutable {
                                                                            RestoreReport rep;
                                                                            OpResult r = w->restoreBackup(path, phrase, o, &rep);
                                                                            wipe(phrase);
                                                                            if (rep.serviceNotEmpty)
                                                                                r.details.push_back("Le service n'est pas vide : coche \xC2\xAB Remplacer ce qu'il contient \xC2\xBB pour le remplacer.");
                                                                            if (rep.filesToResend)
                                                                                r.details.push_back(std::to_string(rep.filesToResend) + " installateur(s) \xC3\xA0 renvoyer : la page Versions les demande.");
                                                                            return r;
                                                                        },
                                                                        true);
                                                             }));
    });
}

void SecurityPage::askKit() {
    if (refuseIfBusy()) return;
    const KeyStatus& k = ws().key();
    if (!k.present) {
        refuse(k.problem);
        return;
    }
    if (k.test) {
        refuse("c'est une cl\xC3\xA9 d'ESSAI : elle ne va jamais dans les Studios livr\xC3\xA9s. Cr\xC3\xA9" "e la vraie cl\xC3\xA9 de l'\xC3\xA9" "diteur (ou restaure sa sauvegarde) sur ce poste");
        return;
    }
    ui::FilePick p;
    p.title = "Le dossier o\xC3\xB9 \xC3\xA9" "crire les fichiers pour les Studios";
    p.folder = true;
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;
            if (refuseIfBusy()) return;
            OpResult r = ws().studioKit(path);
            if (r.ok) r.output = path + "/LISEZ-MOI.txt";
            if (host_.report) host_.report(r);
            refresh();
        });
}

// ---------------------------------------------------------------------------------------------------- dessin
void SecurityPage::onLayout() {
    const auto r = bounds();
    const ui::Fonts f{};
    subtitle_ = wrapText("La cl\xC3\xA9 qui signe toutes les licences : la voir, la sauvegarder, la restaurer, et ce qui prot\xC3\xA8ge ce poste.", f.ui,
                         std::max(200.f, r.w - kMarginLeft - kMarginRight));
    headerH_ = 16.f + ui::lineHeight(f.title) + 6.f + static_cast<float>(subtitle_.size()) * (ui::lineHeight(f.ui) + 2.f) + 10.f;
    scroll_->setBounds({r.x, r.y + headerH_, r.w, std::max(0.f, r.h - headerH_)});
}

void SecurityPage::onPaint(const ui::PaintContext& ctx) {
    if (const bool b = busy(); b != lastBusy_) {
        lastBusy_ = b;
        if (!b) running_.clear();
        syncEnabled();
        rebuild();
    }
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto b = bounds();
    r.fillRect(b, th.color.panelBg);
    float y = b.y + 16.f;
    r.drawText({b.x + kMarginLeft, y}, title(), th.font.title, th.color.text);
    y += r.lineHeight(th.font.title) + 6.f;
    for (const auto& l : subtitle_) {
        r.drawText({b.x + kMarginLeft, y}, l, th.font.ui, th.color.textMuted);
        y += r.lineHeight(th.font.ui) + 2.f;
    }
    r.fillRect({b.x, b.y + headerH_ - 1.f, b.w, 1.f}, th.color.border);
}

} // namespace

PagePtr makeSecurityPage(ShellHost host) { return std::make_unique<SecurityPage>(std::move(host)); }

} // namespace license_admin::gui
