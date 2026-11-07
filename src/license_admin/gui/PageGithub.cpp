// =============================================================================
//  license_admin/gui/PageGithub.cpp - 1.33.0 : SYNCHRONISATION GITHUB dans
//  XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre)
// -----------------------------------------------------------------------------
//  L'etat (le depot, la branche, le dossier, le jeton - enregistre ou absent,
//  jamais sa valeur -, la cle de synchro, le dernier resultat), puis un
//  ASSISTANT EN TROIS ETAPES, avec le chemin exact des menus de GitHub (le client
//  trouvait la console penible a regler) :
//    1. le depot (PRIVE) ; le premier poste cree la cle de synchro et voit son
//       CODE DE SECOURS une fois (Copier sur un clic, une case << je l'ai note >>
//       avant de continuer) ; un autre poste donne ce code ;
//    2. le jeton a grain fin (Contents : Read and write, ce depot seulement) ;
//    3. Verifier (sans rien changer), Synchroniser, Recevoir seulement.
//  Tout le reseau passe par runSlow ; pendant ce temps les boutons sont gris et
//  la page dit << Synchro en cours... >>. Le dernier rapport reste dans la page
//  (recu, envoye, avertissements).
//
//  LES SECRETS : le jeton et le code de secours sont tapes dans des champs
//  masques, vides des leur emploi ; le code de secours n'est montre qu'une fois
//  (le premier reglage) ou sur demande confirmee (Afficher le code de secours).
//
//  1.34.0 : LA CARTE << Passerelle des clients >> (un AUTRE depot, PUBLIC :
//  licensing/Gateway.hpp, license_admin/Passerelle.hpp) - le depot, la branche,
//  le JETON DES DEMANDES (celui des clients : Issues seulement, champ masque),
//  le chemin exact de github.com pour le creer, Verifier et Publier la
//  configuration (lents : runSlow), le dernier resultat. Le jeton de l'editeur
//  doit couvrir aussi ce depot (Contents et Issues : Read and write).
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
#include <utility>
#include <vector>

namespace license_admin::gui {

namespace {

// =============================================================================================================
//  LA PAGE EN BLOCS (le meme petit outil que PageSecurity.cpp) : des cartes, du texte qui passe a la ligne, des
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
            r.drawText(run.at, run.text, run.font, col);
            if (run.bold) r.drawText({run.at.x + 0.6f, run.at.y}, run.text, run.font, col);
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


// Le rapport de la synchro, comme le bandeau le montre : la phrase, puis une ligne par fichier.
OpResult syncResult(const github::SyncReport& s, int mode) {
    OpResult r;
    r.ok = s.ok;
    const int nr = static_cast<int>(s.received.size()), ns = static_cast<int>(s.sent.size());
    const auto count = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    if (!s.ok) r.message = "Refus\xC3\xA9 : " + s.why;
    else if (s.preview)
        r.message = nr == 0 && ns == 0 ? std::string("V\xC3\xA9rifi\xC3\xA9, rien n'a chang\xC3\xA9 : le poste et le d\xC3\xA9p\xC3\xB4t ont les m\xC3\xAAmes fichiers.")
                                       : "V\xC3\xA9rifi\xC3\xA9, rien n'a chang\xC3\xA9 : " + count(nr, "fichier \xC3\xA0 recevoir", "fichiers \xC3\xA0 recevoir") + ", "
                                             + count(ns, "\xC3\xA0 envoyer", "\xC3\xA0 envoyer") + ".";
    else if (mode == 2)
        r.message = nr == 0 ? std::string("Rien \xC3\xA0 recevoir : le poste a d\xC3\xA9j\xC3\xA0 tout ce que le d\xC3\xA9p\xC3\xB4t a.")
                            : "Re\xC3\xA7u du d\xC3\xA9p\xC3\xB4t : " + count(nr, "fichier", "fichiers") + ".";
    else if (nr == 0 && ns == 0 && s.commit.empty())
        r.message = "Synchro faite : d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour, rien \xC3\xA0 recevoir ni \xC3\xA0 envoyer.";
    else
        r.message = "Synchro faite : " + count(nr, "re\xC3\xA7u", "re\xC3\xA7us") + ", " + count(ns, "envoy\xC3\xA9", "envoy\xC3\xA9s")
                    + (s.commit.empty() ? std::string() : " (commit " + s.commit.substr(0, 12) + ")") + ".";
    if (!s.remoteInfo.empty()) r.details.push_back("D\xC3\xA9p\xC3\xB4t : " + s.remoteInfo);
    if (s.initialized) r.details.push_back("Le d\xC3\xA9p\xC3\xB4t \xC3\xA9tait vide : son premier commit (LISEZ-MOI.md) est fait.");
    for (const auto& x : s.received)
        r.details.push_back((s.preview ? "\xC3\x80 recevoir : " : s.ok ? "Re\xC3\xA7u : " : "Re\xC3\xA7u (d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crit sur ce poste) : ") + x);
    for (const auto& x : s.sent) r.details.push_back((s.commit.empty() ? "\xC3\x80 envoyer : " : "Envoy\xC3\xA9 : ") + x);
    for (const auto& x : s.warnings) r.details.push_back("Attention : " + x);
    if (!s.commit.empty()) r.details.push_back("Commit : " + s.commit);
    return r;
}

// "https://github.com/moi/licences-xpg.git" -> "moi/licences-xpg" (l'adresse copiee du navigateur marche aussi).
std::string normalizeRepo(const std::string& in) {
    std::string s = trimmed(in);
    for (const char* prefix : {"https://github.com/", "http://github.com/", "https://www.github.com/", "www.github.com/", "github.com/", "git@github.com:"}) {
        const std::string p = prefix;
        if (s.size() > p.size() && s.compare(0, p.size(), p) == 0) {
            s.erase(0, p.size());
            break;
        }
    }
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) s.resize(s.size() - 4);
    return s;
}

// =============================================================================================================
//  LA PAGE
// =============================================================================================================
class GithubPage final : public Page {
public:
    explicit GithubPage(ShellHost host);
    ~GithubPage() override {
        links_.clear();   // plus de rappel vers le Workspace pendant que la fenetre se defait
        wipe(recovery_);
        clearSecret(code_);
        clearSecret(token_);
        clearSecret(gwToken_);
    }
    [[nodiscard]] std::string title() const override { return "Synchronisation GitHub"; }
    void refresh() override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    enum class Reveal : std::uint8_t { None, First, Asked };
    struct LastSync {
        bool               has{false};
        bool               ok{false};
        int                mode{0};
        std::string        message;
        github::SyncReport report;
        Seconds            at{0};
    };

    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }
    void refuse(const std::string& why) {
        if (host_.report) host_.report(OpResult::fail(why));
    }
    bool refuseIfBusy() {
        if (!busy()) return false;
        refuse("un travail est en cours (une synchro, une sauvegarde) : attends qu'il finisse");
        return true;
    }
    [[nodiscard]] std::function<bool()> aliveCheck() const {
        std::weak_ptr<bool> w = alive_;
        return [w] { return !w.expired(); };
    }
    void rebuild();
    void syncEnabled();
    void prefill();
    void openAssistant(int step);
    void doConfigure();
    void saveToken();
    void eraseToken();
    void sync(int mode);
    void askShowCode();
    void copyCode();
    void acknowledge();
    void hideCode();
    void buildStep(Blocks& bl);
    void buildReport(Blocks& bl);
    // 1.34.0 : la passerelle des clients.
    void buildGateway(Blocks& bl);
    void gatewayConfigure();
    void gatewaySaveToken();
    void gatewayEraseToken();
    void gatewaySlow(int what);   // 0 : verifier, 1 : publier la configuration
    [[nodiscard]] bool gatewayRunning() const;

    ui::ScrollablePanel*            scroll_{nullptr};
    FlowBody*                       body_{nullptr};
    ui::Button*                     modify_{nullptr};
    ui::Button*                     showCode_{nullptr};
    ui::Button*                     step1_{nullptr};
    ui::Button*                     step2_{nullptr};
    ui::Button*                     step3_{nullptr};
    ui::InputText*                  repo_{nullptr};
    ui::InputText*                  branch_{nullptr};
    ui::InputText*                  root_{nullptr};
    std::shared_ptr<ui::RadioGroup> mode_{std::make_shared<ui::RadioGroup>()};
    ui::RadioButton*                firstPc_{nullptr};
    ui::RadioButton*                otherPc_{nullptr};
    ui::InputText*                  code_{nullptr};
    ui::Button*                     showTyped_{nullptr};
    ui::Checkbox*                   replace_{nullptr};
    ui::Button*                     configure_{nullptr};
    ui::InputText*                  token_{nullptr};
    ui::Button*                     saveToken_{nullptr};
    ui::Button*                     eraseToken_{nullptr};
    ui::Button*                     verify_{nullptr};
    ui::Button*                     sync_{nullptr};
    ui::Button*                     receive_{nullptr};
    ui::Button*                     back_{nullptr};
    ui::Button*                     next_{nullptr};
    ui::Button*                     finish_{nullptr};
    ui::Button*                     copy_{nullptr};
    ui::Checkbox*                   noted_{nullptr};
    ui::Button*                     continue_{nullptr};
    ui::Button*                     hide_{nullptr};
    ui::InputText*                  gwRepo_{nullptr};
    ui::InputText*                  gwBranch_{nullptr};
    ui::Button*                     gwSave_{nullptr};
    ui::InputText*                  gwToken_{nullptr};
    ui::Button*                     gwSaveToken_{nullptr};
    ui::Button*                     gwEraseToken_{nullptr};
    ui::Button*                     gwVerify_{nullptr};
    ui::Button*                     gwPublish_{nullptr};
    struct GatewayLast {
        bool        has{false}, ok{false};
        std::string what, message;
        std::vector<std::string> details;
        Seconds     at{0};
    } gwLast_;
    bool                     gwRunning_{false};

    bool                     assistant_{false};
    int                      step_{0};
    std::string              recovery_;   // le code de secours montre ("" : aucun)
    Reveal                   reveal_{Reveal::None};
    bool                     syncing_{false};
    std::string              syncLabel_;
    LastSync                 last_;
    bool                     lastBusy_{false};
    float                    headerH_{84.f};
    std::vector<std::string> subtitle_;
    core::ConnectionScope    links_;
    std::shared_ptr<bool>    alive_{std::make_shared<bool>(true)};
};

GithubPage::GithubPage(ShellHost host) : Page("github", std::move(host)) {
    auto scroll = std::make_unique<ui::ScrollablePanel>("github.defilement");
    scroll->setScrollPolicy(false, true);
    auto body = std::make_unique<FlowBody>("github.corps");
    body_ = body.get();
    scroll->setContent(std::move(body));
    scroll_ = &static_cast<ui::ScrollablePanel&>(addChild(std::move(scroll)));

    modify_ = body_->make<ui::Button>("Modifier\xE2\x80\xA6", "github.modifier");
    modify_->setIcon(ui::Icon::Settings);
    showCode_ = body_->make<ui::Button>("Afficher le code de secours\xE2\x80\xA6", "github.code.afficher");
    showCode_->setIcon(ui::Icon::Lock);

    step1_ = body_->make<ui::Button>("1. Le d\xC3\xA9p\xC3\xB4t", "github.etape1");
    step2_ = body_->make<ui::Button>("2. Le jeton", "github.etape2");
    step3_ = body_->make<ui::Button>("3. V\xC3\xA9rifier, synchroniser", "github.etape3");

    repo_ = body_->make<ui::InputText>("github.depot");
    repo_->setPlaceholder("ton-compte/licences-xpg");
    repo_->setMaxLength(200);
    branch_ = body_->make<ui::InputText>("github.branche");
    branch_->setPlaceholder("main");
    branch_->setMaxLength(100);
    root_ = body_->make<ui::InputText>("github.dossier");
    root_->setPlaceholder("facultatif : la racine du d\xC3\xA9p\xC3\xB4t");
    root_->setMaxLength(200);
    firstPc_ = body_->make<ui::RadioButton>("Premier poste (ou garder la cl\xC3\xA9 de synchro de ce poste)", mode_, 0, "github.premier");
    otherPc_ = body_->make<ui::RadioButton>("J'ai d\xC3\xA9j\xC3\xA0 un code de secours (un autre PC Admin)", mode_, 1, "github.secours");
    mode_->setValue(0);
    code_ = body_->make<ui::InputText>("github.code");
    code_->setMasked(true);
    code_->setPlaceholder("XPGS1-\xE2\x80\xA6");
    code_->setMaxLength(200);
    showTyped_ = body_->make<ui::Button>("Montrer", "github.code.montrer");
    replace_ = body_->make<ui::Checkbox>("remplacer la cl\xC3\xA9 de synchro de ce poste", "github.remplacer");
    configure_ = body_->make<ui::Button>("Enregistrer le d\xC3\xA9p\xC3\xB4t", "github.configurer");
    configure_->setStyle(ui::Button::Style::Primary);

    token_ = body_->make<ui::InputText>("github.jeton");
    token_->setMasked(true);
    token_->setPlaceholder("github_pat_\xE2\x80\xA6");
    token_->setMaxLength(300);
    saveToken_ = body_->make<ui::Button>("Enregistrer le jeton", "github.jeton.enregistrer");
    saveToken_->setStyle(ui::Button::Style::Primary);
    eraseToken_ = body_->make<ui::Button>("Retirer le jeton", "github.jeton.retirer");

    verify_ = body_->make<ui::Button>("V\xC3\xA9rifier (sans rien changer)", "github.verifier");
    verify_->setIcon(ui::Icon::Search);
    sync_ = body_->make<ui::Button>("Synchroniser", "github.synchroniser");
    sync_->setStyle(ui::Button::Style::Primary);
    sync_->setIcon(ui::Icon::Refresh);
    receive_ = body_->make<ui::Button>("Recevoir seulement", "github.recevoir");

    back_ = body_->make<ui::Button>("Retour", "github.retour");
    next_ = body_->make<ui::Button>("Continuer", "github.continuer");
    finish_ = body_->make<ui::Button>("Terminer l'assistant", "github.terminer");

    copy_ = body_->make<ui::Button>("Copier", "github.code.copier");
    copy_->setIcon(ui::Icon::Copy);
    noted_ = body_->make<ui::Checkbox>("Je l'ai not\xC3\xA9 hors de ce PC", "github.code.note");
    continue_ = body_->make<ui::Button>("Continuer", "github.code.continuer");
    continue_->setStyle(ui::Button::Style::Primary);
    hide_ = body_->make<ui::Button>("Masquer le code", "github.code.masquer");

    gwRepo_ = body_->make<ui::InputText>("github.passerelle.depot");
    gwRepo_->setPlaceholder("ton-compte/passerelle-licences");
    gwRepo_->setMaxLength(200);
    gwBranch_ = body_->make<ui::InputText>("github.passerelle.branche");
    gwBranch_->setPlaceholder("main");
    gwBranch_->setMaxLength(100);
    gwSave_ = body_->make<ui::Button>("Enregistrer la passerelle", "github.passerelle.enregistrer");
    gwSave_->setStyle(ui::Button::Style::Primary);
    gwToken_ = body_->make<ui::InputText>("github.passerelle.jeton");
    gwToken_->setMasked(true);
    gwToken_->setPlaceholder("github_pat_\xE2\x80\xA6 (Issues seulement)");
    gwToken_->setMaxLength(300);
    gwSaveToken_ = body_->make<ui::Button>("Enregistrer le jeton des demandes", "github.passerelle.jeton.enregistrer");
    gwEraseToken_ = body_->make<ui::Button>("Retirer", "github.passerelle.jeton.retirer");
    gwVerify_ = body_->make<ui::Button>("V\xC3\xA9rifier", "github.passerelle.verifier");
    gwVerify_->setIcon(ui::Icon::Search);
    gwPublish_ = body_->make<ui::Button>("Publier la configuration", "github.passerelle.publier");
    gwPublish_->setStyle(ui::Button::Style::Primary);
    gwPublish_->setIcon(ui::Icon::Export);

    links_ += modify_->clicked->connect([this] { openAssistant(0); });
    links_ += showCode_->clicked->connect([this] { askShowCode(); });
    links_ += step1_->clicked->connect([this] { openAssistant(0); });
    links_ += step2_->clicked->connect([this] { openAssistant(1); });
    links_ += step3_->clicked->connect([this] { openAssistant(2); });
    links_ += mode_->valueChanged->connect([this](int) {
        rebuild();
        syncEnabled();
    });
    links_ += showTyped_->clicked->connect([this] {
        code_->setMasked(!code_->masked());
        showTyped_->setText(code_->masked() ? "Montrer" : "Masquer");
    });
    links_ += configure_->clicked->connect([this] { doConfigure(); });
    links_ += saveToken_->clicked->connect([this] { saveToken(); });
    links_ += eraseToken_->clicked->connect([this] { eraseToken(); });
    links_ += verify_->clicked->connect([this] { sync(0); });
    links_ += sync_->clicked->connect([this] { sync(1); });
    links_ += receive_->clicked->connect([this] { sync(2); });
    links_ += back_->clicked->connect([this] { openAssistant(std::max(0, step_ - 1)); });
    links_ += next_->clicked->connect([this] { openAssistant(std::min(2, step_ + 1)); });
    links_ += finish_->clicked->connect([this] {
        assistant_ = false;
        refresh();
    });
    links_ += copy_->clicked->connect([this] { copyCode(); });
    links_ += noted_->stateChanged->connect([this](ui::Checkbox::State) { syncEnabled(); });
    links_ += continue_->clicked->connect([this] { acknowledge(); });
    links_ += hide_->clicked->connect([this] { hideCode(); });
    links_ += gwSave_->clicked->connect([this] { gatewayConfigure(); });
    links_ += gwSaveToken_->clicked->connect([this] { gatewaySaveToken(); });
    links_ += gwEraseToken_->clicked->connect([this] { gatewayEraseToken(); });
    links_ += gwVerify_->clicked->connect([this] { gatewaySlow(0); });
    links_ += gwPublish_->clicked->connect([this] { gatewaySlow(1); });

    prefill();
    refresh();
}

void GithubPage::prefill() {
    if (const auto& p = ws().passerelle(); p.configured && p.problem.empty()) {
        gwRepo_->setText(p.config.repo);
        gwBranch_->setText(p.config.branch);
    } else if (gwBranch_->text().empty()) {
        gwBranch_->setText("main");
    }
    const auto& g = ws().github();
    if (g.configured) {
        repo_->setText(g.config.repo);
        branch_->setText(g.config.branch);
        root_->setText(g.config.root);
    } else if (branch_->text().empty()) {
        branch_->setText("main");
    }
}

void GithubPage::refresh() {
    const auto& g = ws().github();
    if ((!g.configured || !g.token) && !assistant_) {
        assistant_ = true;
        step_ = !g.configured ? 0 : 1;
    }
    if (reveal_ == Reveal::Asked) {   // montre sur demande : cache au retour sur la page, apres une operation
        wipe(recovery_);
        reveal_ = Reveal::None;
    }
    lastBusy_ = busy();
    if (!lastBusy_) {
        syncing_ = false;
        gwRunning_ = false;
    }
    if (const auto& p = ws().passerelle(); p.configured && p.problem.empty() && gwRepo_->text().empty()) {
        gwRepo_->setText(p.config.repo);
        gwBranch_->setText(p.config.branch);
    }
    rebuild();
    syncEnabled();
}

void GithubPage::syncEnabled() {
    const bool b = busy();
    const auto& g = ws().github();
    for (auto* w : std::initializer_list<ui::Widget*>{configure_, saveToken_, verify_, sync_, receive_, firstPc_, otherPc_, replace_, showCode_, modify_})
        w->setEnabled(!b);
    eraseToken_->setEnabled(!b && g.token);
    continue_->setEnabled(noted_->isChecked());
    const auto& p = ws().passerelle();
    for (auto* w : std::initializer_list<ui::Widget*>{gwSave_, gwSaveToken_}) w->setEnabled(!b);
    gwEraseToken_->setEnabled(!b && p.requestToken);
    gwVerify_->setEnabled(!b && p.configured);
    gwPublish_->setEnabled(!b && p.configured && p.requestToken && ws().key().present);
}

void GithubPage::openAssistant(int step) {
    if (!assistant_) prefill();
    assistant_ = true;
    step_ = std::clamp(step, 0, 2);
    rebuild();
    syncEnabled();
}

void GithubPage::rebuild() {
    const auto& g = ws().github();
    const bool b = busy();
    const bool ready = g.configured && g.token && g.syncKey;
    Blocks bl;
    if (b && syncing_) bl.text("Synchro en cours\xE2\x80\xA6 (" + syncLabel_ + ") Le r\xC3\xA9seau peut prendre quelques secondes.", ui::Tone::Info);
    else if (b) bl.text("Un travail est en cours : les boutons reviennent quand il a fini.", ui::Tone::Info);

    if (!recovery_.empty()) {
        bl.card("Code de secours de la cl\xC3\xA9 de synchro", ui::Tone::Warning);
        if (reveal_ == Reveal::First)
            bl.text("Note-le MAINTENANT hors de ce PC : sur papier rang\xC3\xA9 au coffre, ou dans ton gestionnaire de mots de passe. Il redonne la "
                    "cl\xC3\xA9 de synchro sur un autre PC Admin (ou sur celui-ci, r\xC3\xA9install\xC3\xA9) ; sans lui, les fichiers chiffr\xC3\xA9s du d\xC3\xA9p\xC3\xB4t sont "
                    "illisibles. Il ne sera plus montr\xC3\xA9, sauf si tu le demandes (Afficher le code de secours\xE2\x80\xA6).");
        else
            bl.text("Avec ce code et le jeton, on lit tout le d\xC3\xA9p\xC3\xB4t des licences : ne le montre \xC3\xA0 personne, ne l'envoie pas par e-mail. "
                    "Masque-le d\xC3\xA8s qu'il est not\xC3\xA9.");
        bl.code(recovery_);
        if (reveal_ == Reveal::First) {
            bl.row({copy_});
            bl.row({noted_});
            bl.row({continue_});
        } else {
            bl.row({copy_, hide_});
        }
        bl.end();
    }

    bl.card("\xC3\x89tat", ready ? ui::Tone::Ok : ui::Tone::Warning);
    bl.pair("D\xC3\xA9p\xC3\xB4t", g.configured ? g.config.repo : std::string("pas encore r\xC3\xA9gl\xC3\xA9"), g.configured ? ui::Tone::None : ui::Tone::Warning);
    if (g.configured) {
        bl.pair("Branche", g.config.branch);
        bl.pair("Dossier", g.config.root.empty() ? std::string("la racine du d\xC3\xA9p\xC3\xB4t") : g.config.root);
        if (!g.config.api.empty() && g.config.api != github::kDefaultApi) bl.pair("API", g.config.api + " (pas celle de GitHub : un essai)", ui::Tone::Warning);
    }
    bl.pair("Jeton", g.token ? std::string("enregistr\xC3\xA9 sur ce poste (prot\xC3\xA9g\xC3\xA9 pour ce compte Windows)") : std::string("absent"),
            g.token ? ui::Tone::None : ui::Tone::Warning);
    bl.pair("Cl\xC3\xA9 de synchro", g.syncKey ? g.syncKeyId : std::string("pas encore sur ce poste"), g.syncKey ? ui::Tone::None : ui::Tone::Warning);
    bl.pair("Dossier local", ws().folder());
    if (last_.has)
        bl.pair("Dernier r\xC3\xA9sultat", last_.message + " (" + dateTimeFr(last_.at) + ")", last_.ok ? ui::Tone::Ok : ui::Tone::Error);
    else
        bl.pair("Dernier r\xC3\xA9sultat", "aucune synchro depuis l'ouverture de l'application", ui::Tone::Muted);
    if (!g.problem.empty()) bl.text("Probl\xC3\xA8me : " + g.problem, ui::Tone::Error);
    {
        std::vector<ui::Widget*> row;
        if (g.configured && !assistant_) row.push_back(modify_);
        if (g.syncKey && recovery_.empty()) row.push_back(showCode_);
        if (!row.empty()) bl.row(std::move(row));
    }
    bl.end();

    if (assistant_ && reveal_ != Reveal::First) {
        bl.card("Assistant : trois \xC3\xA9tapes");
        step1_->setText(std::string("1. Le d\xC3\xA9p\xC3\xB4t") + (g.configured ? " \xE2\x9C\x93" : ""));
        step2_->setText(std::string("2. Le jeton") + (g.token ? " \xE2\x9C\x93" : ""));
        step3_->setText("3. V\xC3\xA9rifier, synchroniser");
        step1_->setStyle(step_ == 0 ? ui::Button::Style::Primary : ui::Button::Style::Default);
        step2_->setStyle(step_ == 1 ? ui::Button::Style::Primary : ui::Button::Style::Default);
        step3_->setStyle(step_ == 2 ? ui::Button::Style::Primary : ui::Button::Style::Default);
        bl.row({step1_, step2_, step3_});
        bl.space(4.f);
        buildStep(bl);
        std::vector<ui::Widget*> nav;
        if (step_ > 0) nav.push_back(back_);
        if ((step_ == 0 && g.configured) || (step_ == 1 && g.token)) nav.push_back(next_);   // l'etape faite : la suivante
        if (step_ == 2 && g.configured && g.token) nav.push_back(finish_);
        bl.space(4.f);
        bl.row(std::move(nav));
        bl.end();
    } else if (!assistant_) {
        bl.card("Synchroniser");
        bl.text("V\xC3\xA9rifier lit le d\xC3\xA9p\xC3\xB4t et dit ce qui serait re\xC3\xA7u et envoy\xC3\xA9, sans rien changer. Synchroniser re\xC3\xA7oit d'abord ce qui est plus "
                "r\xC3\xA9" "cent sur le d\xC3\xA9p\xC3\xB4t, puis publie ce qu'il n'a pas (un commit). Recevoir seulement : un poste neuf, ou apr\xC3\xA8s une restauration.");
        bl.row({verify_, sync_, receive_});
        bl.end();
    }

    buildReport(bl);
    buildGateway(bl);

    bl.card("\xC3\x80 savoir");
    bl.bullet("Pas encore essay\xC3\xA9 contre le vrai GitHub : la synchro est \xC3\xA9prouv\xC3\xA9" "e contre un faux GitHub dans les essais, comme \xC3\xA0 la livraison "
              "1.31. Premier essai conseill\xC3\xA9 : un d\xC3\xA9p\xC3\xB4t priv\xC3\xA9 de test, puis \xC2\xAB V\xC3\xA9rifier (sans rien changer) \xC2\xBB.");
    bl.bullet("Le d\xC3\xA9p\xC3\xB4t de l'archive doit \xC3\xAAtre priv\xC3\xA9 : un d\xC3\xA9p\xC3\xB4t public est refus\xC3\xA9, rien n'y est envoy\xC3\xA9. (La passerelle des clients, "
              "elle, est un AUTRE d\xC3\xA9p\xC3\xB4t, public : sa carte est plus haut.)");
    bl.bullet("Le jeton ne va jamais dans le d\xC3\xA9p\xC3\xB4t, ni la cl\xC3\xA9 de l'\xC3\xA9" "diteur, ni la cl\xC3\xA9 de synchro. Les licences, les r\xC3\xA9vocations et le "
              "registre des activations y sont chiffr\xC3\xA9s par la cl\xC3\xA9 de synchro ; le registre des \xC3\xA9" "ditions (sans donn\xC3\xA9" "e de client) y est en clair.");
    bl.bullet("Un envoi = un commit. Si un autre poste a publi\xC3\xA9 entre-temps, l'outil relit, fusionne et recommence : rien n'est \xC3\xA9" "cras\xC3\xA9 en silence.");
    bl.end();
    body_->setBlocks(std::move(bl.items));
}

void GithubPage::buildStep(Blocks& bl) {
    const auto& g = ws().github();
    if (step_ == 0) {
        bl.heading("1. Le d\xC3\xA9p\xC3\xB4t");
        bl.text("Sur github.com, cr\xC3\xA9" "e un d\xC3\xA9p\xC3\xB4t PRIV\xC3\x89 (par exemple licences-xpg) : le bouton + en haut \xC3\xA0 droite \xE2\x80\xBA New repository \xE2\x80\xBA "
                "Repository name : licences-xpg \xE2\x80\xBA Private \xE2\x80\xBA Create repository. Il peut rester vide : le premier envoi y met un LISEZ-MOI.");
        bl.text("Puis \xC3\xA9" "cris-le ici sous la forme PROPRIETAIRE/NOM : ton compte GitHub, une barre, le nom du d\xC3\xA9p\xC3\xB4t. L'adresse copi\xC3\xA9" "e du "
                "navigateur marche aussi.");
        bl.field("D\xC3\xA9p\xC3\xB4t", repo_, 380.f);
        bl.field("Branche", branch_, 200.f);
        bl.field("Dossier du d\xC3\xA9p\xC3\xB4t", root_, 300.f);
        bl.note("Le dossier est facultatif : vide, l'archive est \xC3\xA0 la racine du d\xC3\xA9p\xC3\xB4t.");
        bl.heading("Ce poste");
        bl.row({firstPc_});
        bl.row({otherPc_});
        if (mode_->value() != 1) {
            bl.note(g.syncKey ? "Ce poste a d\xC3\xA9j\xC3\xA0 sa cl\xC3\xA9 de synchro (" + g.syncKeyId + ") : elle est gard\xC3\xA9" "e."
                              : std::string("Une cl\xC3\xA9 de synchro est cr\xC3\xA9\xC3\xA9" "e sur ce poste ; son code de secours s'affiche une seule fois : note-le "
                                            "hors de ce PC."));
        } else {
            bl.field("Code de secours", code_, 560.f, {showTyped_});
            bl.note("Le code (XPGS1-\xE2\x80\xA6) a \xC3\xA9t\xC3\xA9 not\xC3\xA9 quand le premier poste a \xC3\xA9t\xC3\xA9 r\xC3\xA9gl\xC3\xA9 ; sur ce poste-l\xC3\xA0 : Afficher le code de secours\xE2\x80\xA6");
            if (g.syncKey) {
                bl.row({replace_});
                bl.note("Ce poste a d\xC3\xA9j\xC3\xA0 une cl\xC3\xA9 de synchro (" + g.syncKeyId + ") : case coch\xC3\xA9" "e, elle est mise de c\xC3\xB4t\xC3\xA9 (jamais effac\xC3\xA9" "e) et "
                        "remplac\xC3\xA9" "e par celle du code.");
            }
        }
        bl.row({configure_});
    } else if (step_ == 1) {
        bl.heading("2. Le jeton");
        bl.text("Un jeton \xC3\xA0 grain fin, limit\xC3\xA9 \xC3\xA0 ce d\xC3\xA9p\xC3\xB4t. Sur github.com : ta photo en haut \xC3\xA0 droite \xE2\x80\xBA Settings \xE2\x80\xBA Developer settings \xE2\x80\xBA "
                "Personal access tokens \xE2\x80\xBA Fine-grained tokens \xE2\x80\xBA Generate new token. Puis :");
        bl.bullet("Token name : XPG License Admin, et le nom de ce PC.");
        bl.bullet("Expiration : 1 an (note la date : il faudra le renouveler).");
        bl.bullet("Repository access : Only select repositories \xE2\x80\xBA le d\xC3\xA9p\xC3\xB4t des licences seulement.");
        bl.bullet("Permissions \xE2\x80\xBA Repository permissions \xE2\x80\xBA Contents : Read and write (Metadata : Read-only, ajout\xC3\xA9 par GitHub).");
        bl.bullet("Generate token, puis copie-le tout de suite (github_pat_\xE2\x80\xA6) : GitHub ne le remontre plus.");
        bl.field("Jeton GitHub", token_, 460.f);
        bl.row({saveToken_, eraseToken_});
        bl.pair("Sur ce poste", g.token ? std::string("un jeton est enregistr\xC3\xA9 (prot\xC3\xA9g\xC3\xA9 pour ce compte Windows)") : std::string("aucun jeton"),
                g.token ? ui::Tone::Ok : ui::Tone::Warning);
        bl.note("Le jeton reste sur ce PC, prot\xC3\xA9g\xC3\xA9 : il n'est jamais \xC3\xA9" "crit dans le d\xC3\xA9p\xC3\xB4t, ni montr\xC3\xA9 ici. Retir\xC3\xA9 d'ici, pense \xC3\xA0 le r\xC3\xA9voquer aussi "
                "sur GitHub (m\xC3\xAAme menu, Delete).");
    } else {
        bl.heading("3. V\xC3\xA9rifier, puis synchroniser");
        bl.text("V\xC3\xA9rifier lit le d\xC3\xA9p\xC3\xB4t et dit ce qui serait re\xC3\xA7u et envoy\xC3\xA9, sans rien changer. Synchroniser re\xC3\xA7oit d'abord ce qui est plus "
                "r\xC3\xA9" "cent sur le d\xC3\xA9p\xC3\xB4t, puis publie ce qu'il n'a pas (un commit). Recevoir seulement : un poste neuf, ou apr\xC3\xA8s une restauration.");
        if (!g.configured) bl.note("Il manque le d\xC3\xA9p\xC3\xB4t : \xC3\xA9tape 1.", ui::Tone::Warning);
        else if (!g.token) bl.note("Il manque le jeton : \xC3\xA9tape 2.", ui::Tone::Warning);
        else if (!g.syncKey) bl.note("Il manque la cl\xC3\xA9 de synchro : \xC3\xA9tape 1 (premier poste, ou ton code de secours).", ui::Tone::Warning);
        bl.row({verify_, sync_, receive_});
    }
}

void GithubPage::buildReport(Blocks& bl) {
    if (!last_.has) return;
    const auto& s = last_.report;
    const char* what = last_.mode == 0 ? "v\xC3\xA9rification" : last_.mode == 2 ? "r\xC3\xA9" "ception" : "synchronisation";
    bl.card(std::string("Dernier r\xC3\xA9sultat : ") + what + " du " + dateTimeFr(last_.at), last_.ok ? (s.preview ? ui::Tone::Info : ui::Tone::Ok) : ui::Tone::Error);
    bl.text(last_.message, last_.ok ? ui::Tone::None : ui::Tone::Error);
    if (!s.remoteInfo.empty()) bl.pair("D\xC3\xA9p\xC3\xB4t", s.remoteInfo);
    if (s.initialized) bl.note("Le d\xC3\xA9p\xC3\xB4t \xC3\xA9tait vide : son premier commit (LISEZ-MOI.md) est fait.");
    const auto list = [&bl](const std::vector<std::string>& lines, ui::Tone tone) {
        constexpr std::size_t kMax = 40;
        for (std::size_t i = 0; i < lines.size() && i < kMax; ++i) bl.bullet(lines[i], tone);
        if (lines.size() > kMax) bl.note("\xE2\x80\xA6 et " + std::to_string(lines.size() - kMax) + " autres.");
    };
    if (!s.received.empty()) {
        bl.heading(s.preview ? "\xC3\x80 recevoir" : s.ok ? "Re\xC3\xA7u" : "Re\xC3\xA7u (d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crit sur ce poste)");
        list(s.received, ui::Tone::None);
    }
    if (!s.sent.empty()) {
        bl.heading(!s.commit.empty() ? "Envoy\xC3\xA9 (commit " + s.commit.substr(0, 12) + ")" : std::string("\xC3\x80 envoyer"));
        list(s.sent, ui::Tone::None);
    }
    if (!s.warnings.empty()) {
        bl.heading("Attention", ui::Tone::Warning);
        list(s.warnings, ui::Tone::Warning);
    }
    if (s.ok && s.received.empty() && s.sent.empty() && s.warnings.empty()) bl.note("Le poste et le d\xC3\xA9p\xC3\xB4t ont les m\xC3\xAAmes fichiers.");
    bl.end();
}

// ---------------------------------------------------------------------------------------------------- la passerelle
void GithubPage::buildGateway(Blocks& bl) {
    const auto& p = ws().passerelle();
    const bool ready = p.configured && p.problem.empty() && p.requestToken && p.publisherToken;
    bl.card("Passerelle des clients", ready ? ui::Tone::Ok : ui::Tone::Warning);
    bl.text("Un AUTRE d\xC3\xA9p\xC3\xB4t GitHub, PUBLIC (jamais celui de l'archive) : les logiciels des clients y envoient leurs demandes de licence et y "
            "r\xC3\xA9" "cup\xC3\xA8rent leur licence, seuls. Ce qui nomme un client y est scell\xC3\xA9 ; tout ce que tu y publies est sign\xC3\xA9 par ta cl\xC3\xA9.");
    if (gatewayRunning()) bl.text("Passerelle : " + gwLast_.what + " en cours\xE2\x80\xA6", ui::Tone::Info);
    bl.pair("D\xC3\xA9p\xC3\xB4t public", p.configured && p.problem.empty() ? p.config.repo : std::string("pas encore r\xC3\xA9gl\xC3\xA9"),
            p.configured ? ui::Tone::None : ui::Tone::Warning);
    if (p.configured && p.problem.empty()) {
        bl.pair("Branche", p.config.branch);
        if (p.config.api != licensing::gateway::kDefaultApi) bl.pair("API", p.config.api + " (pas celle de GitHub : un essai)", ui::Tone::Warning);
    }
    if (!p.problem.empty()) bl.text("Probl\xC3\xA8me : " + p.problem, ui::Tone::Error);
    bl.pair("Jeton des demandes", p.requestToken ? std::string("enregistr\xC3\xA9 sur ce poste (prot\xC3\xA9g\xC3\xA9 pour ce compte Windows)") : std::string("absent"),
            p.requestToken ? ui::Tone::None : ui::Tone::Warning);
    bl.pair("Ton jeton GitHub", p.publisherToken ? std::string("enregistr\xC3\xA9 (il doit couvrir aussi ce d\xC3\xA9p\xC3\xB4t)") : std::string("absent : \xC3\xA9tape 2 de l'assistant ci-dessus"),
            p.publisherToken ? ui::Tone::None : ui::Tone::Warning);
    if (gwLast_.has)
        bl.pair("Dernier r\xC3\xA9sultat", gwLast_.message + " (" + dateTimeFr(gwLast_.at) + ")", gwLast_.ok ? ui::Tone::Ok : ui::Tone::Error);
    else
        bl.pair("Dernier r\xC3\xA9sultat", "ni v\xC3\xA9rifi\xC3\xA9" "e ni publi\xC3\xA9" "e depuis l'ouverture de l'application", ui::Tone::Muted);
    if (gwLast_.has)
        for (std::size_t i = 0; i < gwLast_.details.size() && i < 12; ++i) bl.bullet(gwLast_.details[i], gwLast_.ok ? ui::Tone::None : ui::Tone::Warning);

    bl.heading("1. Le d\xC3\xA9p\xC3\xB4t public");
    bl.text("Sur github.com : le bouton + en haut \xC3\xA0 droite \xE2\x80\xBA New repository \xE2\x80\xBA Repository name : passerelle-licences \xE2\x80\xBA Public \xE2\x80\xBA "
            "Create repository. Puis \xC3\xA9" "cris-le ici sous la forme PROPRIETAIRE/NOM.");
    bl.field("D\xC3\xA9p\xC3\xB4t public", gwRepo_, 380.f);
    bl.field("Branche", gwBranch_, 200.f);
    bl.row({gwSave_});
    bl.heading("2. Le jeton des demandes (celui des clients)");
    bl.text("Un SECOND jeton, distinct du tien : il part chez les clients (masqu\xC3\xA9, dans la configuration publi\xC3\xA9" "e) et ne doit permettre QUE les "
            "issues de ce d\xC3\xA9p\xC3\xB4t. Sur github.com : ta photo \xE2\x80\xBA Settings \xE2\x80\xBA Developer settings \xE2\x80\xBA Personal access tokens \xE2\x80\xBA "
            "Fine-grained tokens \xE2\x80\xBA Generate new token, puis :");
    bl.bullet("Token name : XPG demandes des clients.");
    bl.bullet("Expiration : la plus longue propos\xC3\xA9" "e ; note la date (\xC3\xA0 son terme, les clients ne peuvent plus demander : enregistre un nouveau jeton et republie).");
    bl.bullet("Repository access : Only select repositories \xE2\x80\xBA le d\xC3\xA9p\xC3\xB4t de la passerelle seulement.");
    bl.bullet("Permissions \xE2\x80\xBA Repository permissions \xE2\x80\xBA Issues : Read and write. RIEN d'autre (Contents : No access ; Metadata : Read-only, ajout\xC3\xA9 par GitHub).");
    bl.field("Jeton des demandes", gwToken_, 460.f);
    bl.row({gwSaveToken_, gwEraseToken_});
    bl.heading("3. Ton propre jeton");
    bl.note("Ton jeton GitHub (\xC3\xA9tape 2 de l'assistant ci-dessus) publie les licences : sur github.com, ajoute-lui ce d\xC3\xA9p\xC3\xB4t (Repository access) avec "
            "Contents : Read and write et Issues : Read and write. Il ne part jamais chez les clients.",
            p.publisherToken ? ui::Tone::Muted : ui::Tone::Warning);
    bl.heading("4. V\xC3\xA9rifier, publier");
    bl.text("V\xC3\xA9rifier : le d\xC3\xA9p\xC3\xB4t est public, ton jeton peut y \xC3\xA9" "crire, le jeton des demandes est accept\xC3\xA9 (rien n'est chang\xC3\xA9). Publier la "
            "configuration : le jeton des demandes (masqu\xC3\xA9), le registre et la liste de r\xC3\xA9vocation, sign\xC3\xA9s par ta cl\xC3\xA9. Ensuite, les demandes "
            "arrivent dans Demandes en ligne.");
    if (!ws().key().present) bl.note("Il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur pour publier (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9).", ui::Tone::Warning);
    bl.row({gwVerify_, gwPublish_});
    bl.end();
}

bool GithubPage::gatewayRunning() const { return gwRunning_ && busy(); }

void GithubPage::gatewayConfigure() {
    if (refuseIfBusy()) return;
    const std::string repo = normalizeRepo(gwRepo_->text());
    const OpResult r = ws().passerelleConfigure(repo, trimmed(gwBranch_->text()));
    if (r.ok) gwRepo_->setText(repo);
    if (host_.report) host_.report(r);
    refresh();
}

void GithubPage::gatewaySaveToken() {
    if (refuseIfBusy()) return;
    std::string t = trimmed(gwToken_->text());
    clearSecret(gwToken_);
    if (t.empty()) {
        refuse("colle d'abord le jeton des demandes (github_pat_\xE2\x80\xA6)");
        return;
    }
    const OpResult r = ws().passerelleSaveRequestToken(t);
    wipe(t);
    if (host_.report) host_.report(r);
    refresh();
}

void GithubPage::gatewayEraseToken() {
    if (refuseIfBusy()) return;
    if (!ws().passerelle().requestToken) {
        refuse("aucun jeton des demandes sur ce poste");
        return;
    }
    const OpResult r = ws().passerelleEraseRequestToken();
    if (host_.report) host_.report(r);
    refresh();
}

void GithubPage::gatewaySlow(int what) {
    if (refuseIfBusy()) return;
    const auto& p = ws().passerelle();
    if (!p.configured || !p.problem.empty()) {
        refuse("la passerelle n'est pas encore r\xC3\xA9gl\xC3\xA9" "e : son d\xC3\xA9p\xC3\xB4t public d'abord (1.)");
        return;
    }
    if (!p.publisherToken) {
        refuse("pas encore de jeton GitHub sur ce poste : \xC3\xA9tape 2 de l'assistant ci-dessus (il doit couvrir aussi la passerelle)");
        return;
    }
    if (what == 1 && !p.requestToken) {
        refuse("enregistre d'abord le jeton des demandes (2.) : il part chez les clients avec la configuration");
        return;
    }
    if (what == 1 && !ws().key().present) {
        refuse("il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur pour signer la configuration (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9)");
        return;
    }
    gwLast_.what = what == 0 ? "v\xC3\xA9rification" : "publication";
    gwRunning_ = true;
    const Workspace* w = &ws();
    const auto alive = aliveCheck();
    const bool started = host_.runSlow
                         && host_.runSlow(what == 0 ? "Passerelle : v\xC3\xA9rification" : "Passerelle : publication de la configuration",
                                          [w, what] { return what == 0 ? w->passerelleVerify() : w->passerellePublishConfig(); },
                                          [this, alive](const OpResult& r) {
                                              if (!alive()) return;
                                              gwRunning_ = false;
                                              gwLast_.has = true;
                                              gwLast_.ok = r.ok;
                                              gwLast_.message = r.message;
                                              gwLast_.details = r.details;
                                              gwLast_.at = ws().now();
                                              refresh();
                                          });
    if (!started) {
        gwRunning_ = false;
        refuse("un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence");
    }
    lastBusy_ = busy();
    rebuild();
    syncEnabled();
}

// ---------------------------------------------------------------------------------------------------- etape 1
void GithubPage::doConfigure() {
    if (refuseIfBusy()) return;
    const std::string repo = normalizeRepo(repo_->text());
    const bool withCode = mode_->value() == 1;
    std::string code = withCode ? trimmed(code_->text()) : std::string();
    clearSecret(code_);
    code_->setMasked(true);
    showTyped_->setText("Montrer");
    if (withCode && code.empty()) {
        refuse("colle le code de secours (XPGS1-\xE2\x80\xA6), ou choisis \xC2\xAB Premier poste \xC2\xBB");
        return;
    }
    std::string recovery;
    const OpResult r = ws().githubConfigure(repo, trimmed(branch_->text()), trimmed(root_->text()), code,
                                            withCode && replace_->isChecked() && ws().github().syncKey, &recovery);
    wipe(code);
    if (r.ok) {
        repo_->setText(repo);
        replace_->setState(ui::Checkbox::State::Unchecked);
        mode_->setValue(0);
        if (!recovery.empty()) {
            wipe(recovery_);
            recovery_ = std::move(recovery);
            reveal_ = Reveal::First;
            noted_->setState(ui::Checkbox::State::Unchecked);
        } else {
            step_ = ws().github().token ? 2 : 1;
        }
    }
    wipe(recovery);
    if (host_.report) host_.report(r);
    refresh();
}

// ---------------------------------------------------------------------------------------------------- le code de secours
void GithubPage::copyCode() {
    if (recovery_.empty()) return;
    ui::setClipboardText(recovery_);
    if (host_.flash)
        host_.flash("Code de secours copi\xC3\xA9. Colle-le dans ton gestionnaire de mots de passe, puis copie autre chose pour le retirer du "
                    "presse-papiers.",
                    Tone::Info);
}

void GithubPage::acknowledge() {
    if (!noted_->isChecked()) {
        refuse("coche d'abord \xC2\xAB Je l'ai not\xC3\xA9 hors de ce PC \xC2\xBB");
        return;
    }
    wipe(recovery_);
    reveal_ = Reveal::None;
    noted_->setState(ui::Checkbox::State::Unchecked);
    assistant_ = true;
    step_ = ws().github().token ? 2 : 1;
    refresh();
}

void GithubPage::hideCode() {
    wipe(recovery_);
    reveal_ = Reveal::None;
    rebuild();
    syncEnabled();
}

void GithubPage::askShowCode() {
    if (!ws().github().syncKey) {
        refuse("pas de cl\xC3\xA9 de synchro sur ce poste");
        return;
    }
    const auto alive = aliveCheck();
    if (host_.openDialog)
        host_.openDialog(std::make_unique<ConfirmDialog>(
            "github.code.confirmation", "Afficher le code de secours",
            "Le code de secours redonne la cl\xC3\xA9 de synchro. Avec lui et le jeton GitHub, on lit tout le d\xC3\xA9p\xC3\xB4t des licences (les clients, "
            "leurs PC).\n\nMontre-le seulement si personne ne regarde ton \xC3\xA9" "cran, pour le noter hors de ce PC ; masque-le juste apr\xC3\xA8s.",
            "Afficher le code", false, [this, alive] {
                if (!alive()) return;
                std::string why;
                std::string code = ws().githubRecoveryCode(&why);
                if (code.empty()) {
                    refuse(why.empty() ? std::string("le code de secours ne se lit pas") : why);
                    return;
                }
                wipe(recovery_);
                recovery_ = std::move(code);
                reveal_ = Reveal::Asked;
                if (host_.flash) host_.flash("Code de secours affich\xC3\xA9 : masque-le d\xC3\xA8s qu'il est not\xC3\xA9.", Tone::Warning);
                rebuild();
                syncEnabled();
            }));
}

// ---------------------------------------------------------------------------------------------------- etape 2
void GithubPage::saveToken() {
    if (refuseIfBusy()) return;
    std::string t = trimmed(token_->text());
    clearSecret(token_);
    if (t.empty()) {
        refuse("colle d'abord le jeton (github_pat_\xE2\x80\xA6)");
        return;
    }
    const OpResult r = ws().githubSaveToken(t);
    wipe(t);
    if (r.ok && assistant_ && step_ == 1) step_ = 2;
    if (host_.report) host_.report(r);
    refresh();
}

void GithubPage::eraseToken() {
    if (refuseIfBusy()) return;
    if (!ws().github().token) {
        refuse("aucun jeton GitHub sur ce poste");
        return;
    }
    const OpResult r = ws().githubEraseToken();
    if (host_.report) host_.report(r);
    refresh();
}

// ---------------------------------------------------------------------------------------------------- etape 3
void GithubPage::sync(int mode) {
    if (refuseIfBusy()) return;
    const auto& g = ws().github();
    if (!g.configured) {
        refuse("le d\xC3\xA9p\xC3\xB4t n'est pas encore r\xC3\xA9gl\xC3\xA9 : \xC3\xA9tape 1 de l'assistant");
        return;
    }
    if (!g.token) {
        refuse("pas encore de jeton GitHub sur ce poste : \xC3\xA9tape 2 de l'assistant");
        return;
    }
    if (!g.syncKey) {
        refuse("pas de cl\xC3\xA9 de synchro sur ce poste : \xC3\xA9tape 1 (premier poste, ou ton code de secours)");
        return;
    }
    const bool dry = mode == 0, receiveOnly = mode == 2;
    syncLabel_ = dry ? "v\xC3\xA9rification" : receiveOnly ? "r\xC3\xA9" "ception" : "synchronisation";
    syncing_ = true;
    auto holder = std::make_shared<github::SyncReport>();
    const Workspace* w = &ws();
    const auto alive = aliveCheck();
    const bool started = host_.runSlow
                         && host_.runSlow(
                             "Synchro GitHub : " + syncLabel_,
                             [w, holder, receiveOnly, dry, mode] {
                                 *holder = w->githubSync(receiveOnly, dry);
                                 return syncResult(*holder, mode);
                             },
                             [this, alive, holder, mode, dry](const OpResult& r) {
                                 if (!alive()) return;
                                 syncing_ = false;
                                 last_.has = true;
                                 last_.ok = r.ok;
                                 last_.mode = mode;
                                 last_.message = r.message;
                                 last_.report = *holder;
                                 last_.at = ws().now();
                                 if (!dry) ws().reload();   // recu en partie avant un refus : la page le montre aussi
                                 refresh();
                             });
    if (!started) {
        syncing_ = false;
        refuse("un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence");
    }
    lastBusy_ = busy();
    rebuild();
    syncEnabled();
}

// ---------------------------------------------------------------------------------------------------- dessin
void GithubPage::onLayout() {
    const auto r = bounds();
    const ui::Fonts f{};
    subtitle_ = wrapText("Le d\xC3\xA9p\xC3\xB4t GitHub priv\xC3\xA9 o\xC3\xB9 l'archive des licences est publi\xC3\xA9" "e, chiffr\xC3\xA9" "e, pour tes autres PC Admin ; "
                         "et la passerelle des clients (un d\xC3\xA9p\xC3\xB4t public, plus bas).", f.ui,
                         std::max(200.f, r.w - kMarginLeft - kMarginRight));
    headerH_ = 16.f + ui::lineHeight(f.title) + 6.f + static_cast<float>(subtitle_.size()) * (ui::lineHeight(f.ui) + 2.f) + 10.f;
    scroll_->setBounds({r.x, r.y + headerH_, r.w, std::max(0.f, r.h - headerH_)});
}

void GithubPage::onPaint(const ui::PaintContext& ctx) {
    if (const bool b = busy(); b != lastBusy_) {
        lastBusy_ = b;
        if (!b) {
            syncing_ = false;
            gwRunning_ = false;
        }
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

PagePtr makeGithubPage(ShellHost host) { return std::make_unique<GithubPage>(std::move(host)); }

} // namespace license_admin::gui
