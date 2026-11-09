// =============================================================================
//  license_admin/gui/PageService.cpp - 1.35.0 : LA PAGE << Service en ligne >>
//  dans XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre). Elle
//  remplace << Synchronisation GitHub >> : la 1.35.0 a quitte GitHub.
// -----------------------------------------------------------------------------
//  Le service des licences de l'editeur (son Worker Cloudflare,
//  docs/SERVICE-LICENCES.md, ServiceClient.hpp), en cartes de haut en bas :
//    - l'ETAT : l'adresse (https://..., a regler ici), Verifier (le service
//      repond, la cle de l'editeur est acceptee, son heure, le registre depose) ;
//    - LA CLE DU SERVICE ET SON CERTIFICAT : l'empreinte de la cle que le
//      service a creee, le certificat (valable jusqu'au...), Certifier /
//      Renouveler : la cle est d'abord lue et MONTREE (son empreinte, a comparer),
//      puis le certificat est signe par la cle de l'editeur et depose ;
//    - L'ARCHIVE EN LIGNE : la cle de synchro (le premier poste la cree et voit
//      son CODE DE SECOURS une fois - Copier sur un clic, une case << je l'ai
//      note >> ; un autre poste donne ce code), la derniere synchro,
//      Synchroniser l'archive, Recevoir seulement, Verifier sans rien changer ;
//    - LES LICENCES EN LIGNE : le dernier releve, Relever, les compteurs ;
//    - METTRE LE SERVICE EN LIGNE : les etapes (outils/service/LISEZ-MOI-SERVICE.txt,
//      resumees).
//  1.45.0 : << Deployer le service >> (la carte << Le service >> et celle de la mise
//  en ligne) lance le kit installe avec l'Admin, sa sortie en direct, puis l'etat
//  relu (DeployDialog.cpp) ; un service en ligne plus ancien que ce qu'il faut a
//  l'Admin (ou que le kit) : un bandeau en haut de la page, avec le meme bouton.
//  Tout le reseau passe par runSlow ; pendant ce temps les boutons sont gris et
//  la page le dit. Rien n'est simule : hors ligne, c'est dit.
//
//  LES SECRETS : le code de secours est tape dans un champ masque, vide des son
//  emploi ; il n'est montre qu'une fois (la creation de la cle) ou sur demande
//  confirmee (Afficher le code de secours).
// =============================================================================
#include "Page.hpp"

#include "../Mail.hpp"   // 1.37.0 : le mail au client

#include "../../deploy/Crypto.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
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


// La synchro de l'archive, comme le bandeau la montre : la phrase, puis une ligne par fichier.
OpResult syncResult(const archive::SyncReport& s, int mode) {
    OpResult r;
    r.ok = s.ok;
    const int nr = static_cast<int>(s.received.size()), ns = static_cast<int>(s.sent.size());
    const auto count = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    if (!s.ok) r.message = "Refus\xC3\xA9 : " + s.why;
    else if (s.preview)
        r.message = nr == 0 && ns == 0 ? std::string("V\xC3\xA9rifi\xC3\xA9, rien n'a chang\xC3\xA9 : le poste et l'archive en ligne ont les m\xC3\xAAmes fichiers.")
                                       : "V\xC3\xA9rifi\xC3\xA9, rien n'a chang\xC3\xA9 : " + count(nr, "fichier \xC3\xA0 recevoir", "fichiers \xC3\xA0 recevoir") + ", " +
                                             count(ns, "\xC3\xA0 envoyer", "\xC3\xA0 envoyer") + ".";
    else if (mode == 2)
        r.message = nr == 0 ? std::string("Rien \xC3\xA0 recevoir : le poste a d\xC3\xA9j\xC3\xA0 tout ce que l'archive en ligne a.")
                            : "Re\xC3\xA7u de l'archive en ligne : " + count(nr, "fichier", "fichiers") + ".";
    else if (nr == 0 && ns == 0)
        r.message = "Archive synchronis\xC3\xA9" "e : d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour, rien \xC3\xA0 recevoir ni \xC3\xA0 envoyer.";
    else
        r.message = "Archive synchronis\xC3\xA9" "e : " + count(nr, "re\xC3\xA7u", "re\xC3\xA7us") + ", " + count(ns, "envoy\xC3\xA9", "envoy\xC3\xA9s") + " (version " +
                    std::to_string(s.version) + ").";
    if (!s.remoteInfo.empty()) r.details.push_back("Archive en ligne : " + s.remoteInfo);
    for (const auto& x : s.received)
        r.details.push_back((s.preview ? "\xC3\x80 recevoir : " : s.ok ? "Re\xC3\xA7u : " : "Re\xC3\xA7u (d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crit sur ce poste) : ") + x);
    for (const auto& x : s.sent) r.details.push_back((s.preview ? "\xC3\x80 envoyer : " : "Envoy\xC3\xA9 : ") + x);
    for (const auto& x : s.warnings) r.details.push_back("Attention : " + x);
    return r;
}

// =============================================================================================================
//  LA PAGE
// =============================================================================================================
class ServicePage final : public Page {
public:
    explicit ServicePage(ShellHost host);
    ~ServicePage() override {
        links_.clear();   // plus de rappel vers le Workspace pendant que la fenetre se defait
        wipe(recovery_);
        clearSecret(code_);
        clearSecret(mailPassword_);
    }
    [[nodiscard]] std::string title() const override { return "Service en ligne"; }
    void refresh() override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    enum class Reveal : std::uint8_t { None, First, Asked };
    struct Last {
        bool                     has{false}, ok{false};
        std::string              what, message;
        std::vector<std::string> details;
        Seconds                  at{0};
    };

    [[nodiscard]] bool busy() const { return host_.busy && host_.busy(); }
    void refuse(const std::string& why) {
        if (host_.report) host_.report(OpResult::fail(why));
    }
    bool refuseIfBusy() {
        if (!busy()) return false;
        refuse("un travail est en cours (une synchro, une v\xC3\xA9rification) : attends qu'il finisse");
        return true;
    }
    [[nodiscard]] std::function<bool()> aliveCheck() const {
        std::weak_ptr<bool> w = alive_;
        return [w] { return !w.expired(); };
    }
    // Lancer un travail lent de la page ; `done` (sur le fil de l'interface) recoit le resultat.
    bool slow(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);
    void rebuild();
    void syncEnabled();
    void saveUrl();
    void verify();
    void askCertify();
    void certify(const std::string& keyHex);
    void syncKey();
    void syncArchive(int mode);   // 0 : verifier, 1 : synchroniser, 2 : recevoir seulement
    void listLicences();
    void stats();
    void writePublicKey();
    void askShowCode();
    void copyCode();
    void acknowledge();
    void hideCode();
    void note(Last& l, const std::string& what, const OpResult& r);
    // 1.37.0 : le mail au client (a l'acceptation), par ta boite.
    void loadMail();
    void saveMail();
    void testMail();
    void forgetMail();
    void mailBlocks(Blocks& bl);
    // 1.38.0 : une boite Microsoft (Outlook.com, Hotmail, Microsoft 365) - la connexion par code (OAuth), sans mot de passe.
    [[nodiscard]] bool microsoftChosen() const;
    [[nodiscard]] mail::Config mailFromFields() const;
    void connectMicrosoft();
    void waitMicrosoft(mail::Config cfg, mail::DeviceCode dc);
    void disconnectMicrosoft();
    // 1.44.0 : les adresses de secours (le registre : fallback_services ; l'admin+ ou le droit << offres >>).
    void fallbackBlocks(Blocks& bl);
    void saveFallback();
    // 1.45.0 : deployer le service (le dialogue, DeployDialog.cpp)
    void openDeploy();

    ui::ScrollablePanel*            scroll_{nullptr};
    FlowBody*                       body_{nullptr};
    ui::InputText*                  fbShown_[3]{};      // 1.44.0 : les adresses de secours (lues)
    ui::Button*                     fbRemove_[3]{};
    ui::InputText*                  fbNew_{nullptr};
    ui::Button*                     fbAdd_{nullptr};
    ui::Button*                     fbSave_{nullptr};
    std::vector<std::string>        fbDraft_;
    bool                            fbDirty_{false};
    ui::InputText*                  url_{nullptr};
    ui::Button*                     saveUrl_{nullptr};
    ui::Button*                     verify_{nullptr};
    ui::Button*                     certify_{nullptr};
    std::shared_ptr<ui::RadioGroup> mode_{std::make_shared<ui::RadioGroup>()};
    ui::RadioButton*                firstPc_{nullptr};
    ui::RadioButton*                otherPc_{nullptr};
    ui::InputText*                  code_{nullptr};
    ui::Button*                     showTyped_{nullptr};
    ui::Checkbox*                   replace_{nullptr};
    ui::Button*                     keyButton_{nullptr};
    ui::Button*                     sync_{nullptr};
    ui::Button*                     receive_{nullptr};
    ui::Button*                     dry_{nullptr};
    ui::Button*                     showCode_{nullptr};
    ui::Button*                     copy_{nullptr};
    ui::Checkbox*                   noted_{nullptr};
    ui::Button*                     continue_{nullptr};
    ui::Button*                     hide_{nullptr};
    ui::Button*                     list_{nullptr};
    ui::Button*                     stats_{nullptr};
    ui::Button*                     publicKey_{nullptr};
    // 1.45.0 : << Deployer le service >> (la carte << Le service >>, le bandeau, la carte de la mise en ligne)
    ui::Button*                     deploy_{nullptr};
    ui::Button*                     deployBanner_{nullptr};
    ui::Button*                     deploySteps_{nullptr};
    // 1.37.0 : le mail
    ui::Button*                     mailGmail_{nullptr};
    ui::InputText*                  mailHost_{nullptr};
    ui::InputText*                  mailPort_{nullptr};
    ui::DropDown*                   mailSecurity_{nullptr};
    ui::InputText*                  mailFrom_{nullptr};
    ui::InputText*                  mailName_{nullptr};
    ui::InputText*                  mailUser_{nullptr};
    ui::InputText*                  mailPassword_{nullptr};
    ui::Button*                     mailShow_{nullptr};
    ui::Button*                     mailSave_{nullptr};
    ui::Button*                     mailTest_{nullptr};
    ui::Button*                     mailForget_{nullptr};
    Last                            mail_;
    // 1.38.0 : Microsoft
    ui::DropDown*                   mailAuth_{nullptr};
    ui::Button*                     mailOutlook_{nullptr};
    ui::Button*                     mailM365_{nullptr};
    ui::InputText*                  mailClientId_{nullptr};
    ui::Button*                     mailConnect_{nullptr};
    ui::Button*                     mailCancel_{nullptr};
    ui::Button*                     mailCopyCode_{nullptr};
    ui::Button*                     mailDisconnect_{nullptr};
    std::shared_ptr<std::atomic<bool>> msStop_;
    std::string                     msCode_, msUri_;   // le code a taper et son adresse (pendant l'attente)
    bool                            msWaiting_{false};

    std::string              recovery_;   // le code de secours montre ("" : aucun)
    Reveal                   reveal_{Reveal::None};
    std::string              running_;    // ce que la page fait en ce moment ("" : rien)
    Last                     service_, archive_, licences_;
    bool                     lastBusy_{false};
    float                    headerH_{84.f};
    std::vector<std::string> subtitle_;
    core::ConnectionScope    links_;
    std::shared_ptr<bool>    alive_{std::make_shared<bool>(true)};
};

ServicePage::ServicePage(ShellHost host) : Page("service", std::move(host)) {
    auto scroll = std::make_unique<ui::ScrollablePanel>("service.defilement");
    scroll->setScrollPolicy(false, true);
    auto body = std::make_unique<FlowBody>("service.corps");
    body_ = body.get();
    scroll->setContent(std::move(body));
    scroll_ = &static_cast<ui::ScrollablePanel&>(addChild(std::move(scroll)));

    url_ = body_->make<ui::InputText>("service.adresse");
    url_->setPlaceholder("https://xpg-licences.ton-compte.workers.dev");
    url_->setMaxLength(220);
    saveUrl_ = body_->make<ui::Button>("Enregistrer l'adresse", "service.adresse.enregistrer");
    saveUrl_->setStyle(ui::Button::Style::Primary);
    verify_ = body_->make<ui::Button>("V\xC3\xA9rifier", "service.verifier");
    verify_->setIcon(ui::Icon::Search);
    certify_ = body_->make<ui::Button>("Certifier la cl\xC3\xA9 du service\xE2\x80\xA6", "service.certifier");
    certify_->setIcon(ui::Icon::Lock);

    firstPc_ = body_->make<ui::RadioButton>("Premier poste : cr\xC3\xA9" "er la cl\xC3\xA9 de synchro", mode_, 0, "service.archive.premier");
    otherPc_ = body_->make<ui::RadioButton>("Un autre poste : j'ai le code de secours", mode_, 1, "service.archive.secours");
    mode_->setValue(0);
    code_ = body_->make<ui::InputText>("service.archive.code");
    code_->setMasked(true);
    code_->setPlaceholder("XPGS1-\xE2\x80\xA6");
    code_->setMaxLength(200);
    showTyped_ = body_->make<ui::Button>("Montrer", "service.archive.code.montrer");
    replace_ = body_->make<ui::Checkbox>("remplacer la cl\xC3\xA9 de synchro de ce poste", "service.archive.remplacer");
    keyButton_ = body_->make<ui::Button>("Cr\xC3\xA9" "er la cl\xC3\xA9 de synchro", "service.archive.cle");
    keyButton_->setStyle(ui::Button::Style::Primary);
    sync_ = body_->make<ui::Button>("Synchroniser l'archive", "service.archive.synchroniser");
    sync_->setStyle(ui::Button::Style::Primary);
    sync_->setIcon(ui::Icon::Refresh);
    receive_ = body_->make<ui::Button>("Recevoir seulement", "service.archive.recevoir");
    dry_ = body_->make<ui::Button>("V\xC3\xA9rifier (sans rien changer)", "service.archive.verifier");
    showCode_ = body_->make<ui::Button>("Afficher le code de secours\xE2\x80\xA6", "service.archive.code.afficher");
    showCode_->setIcon(ui::Icon::Lock);

    copy_ = body_->make<ui::Button>("Copier", "service.code.copier");
    copy_->setIcon(ui::Icon::Copy);
    noted_ = body_->make<ui::Checkbox>("Je l'ai not\xC3\xA9 hors de ce PC", "service.code.note");
    continue_ = body_->make<ui::Button>("Continuer", "service.code.continuer");
    continue_->setStyle(ui::Button::Style::Primary);
    hide_ = body_->make<ui::Button>("Masquer le code", "service.code.masquer");

    list_ = body_->make<ui::Button>("Relever les licences en ligne", "service.licences.relever");
    list_->setIcon(ui::Icon::Refresh);
    stats_ = body_->make<ui::Button>("Les compteurs du service", "service.stats");
    stats_->setIcon(ui::Icon::Chart);
    publicKey_ = body_->make<ui::Button>("\xC3\x89" "crire cle-publique.txt\xE2\x80\xA6", "service.cle-publique");
    publicKey_->setIcon(ui::Icon::Export);
    // 1.45.0 : deployer le service depuis l'Admin (le kit installe avec lui ; sans fenetre de console)
    {
        const char* const ids[] = {"service.deployer", "service.deployer.bandeau", "service.deployer.etapes"};
        ui::Button** const slots[] = {&deploy_, &deployBanner_, &deploySteps_};
        for (int i = 0; i < 3; ++i) {
            ui::Button* b = body_->make<ui::Button>("D\xC3\xA9ployer le service", ids[i]);
            b->setIcon(ui::Icon::ArrowUp);
            b->setTooltip("Lance le kit install\xC3\xA9 avec l'Admin (Node.js, wrangler) : ton service part en ligne, ou \xC3\xA0 jour, sur ton compte "
                          "Cloudflare. La sortie s'affiche en direct ; rien n'est perdu (les licences, les activations, l'archive restent chez Cloudflare).");
            *slots[i] = b;
        }
        deployBanner_->setStyle(ui::Button::Style::Primary);
        for (ui::Button* b : {deploy_, deployBanner_, deploySteps_}) links_ += b->clicked->connect([this] { openDeploy(); });
    }

    // 1.37.0 : le mail au client
    mailGmail_ = body_->make<ui::Button>("Gmail", "service.mail.gmail");
    mailGmail_->setTooltip("Remplit le serveur de Gmail : smtp.gmail.com, port 587, STARTTLS");
    mailHost_ = body_->make<ui::InputText>("service.mail.hote");
    mailHost_->setPlaceholder("smtp.gmail.com");
    mailHost_->setMaxLength(253);
    mailPort_ = body_->make<ui::InputText>("service.mail.port");
    mailPort_->setPlaceholder("587");
    mailPort_->setMaxLength(5);
    mailPort_->setValidator([](std::string_view t) { return std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
    mailSecurity_ = body_->make<ui::DropDown>("service.mail.securite");
    mailSecurity_->setItems({{"STARTTLS (port 587)", "starttls"}, {"TLS d\xC3\xA8s la connexion (port 465)", "tls"}});
    mailSecurity_->setSelectedIndex(0);
    mailFrom_ = body_->make<ui::InputText>("service.mail.expediteur");
    mailFrom_->setPlaceholder("ton-adresse@gmail.com");
    mailFrom_->setMaxLength(254);
    mailName_ = body_->make<ui::InputText>("service.mail.nom");
    mailName_->setPlaceholder("\xC3\x89" "diteur XPG (facultatif : le nom que voit le client)");
    mailName_->setMaxLength(100);
    mailUser_ = body_->make<ui::InputText>("service.mail.identifiant");
    mailUser_->setPlaceholder("l'identifiant SMTP (vide : l'adresse d'exp\xC3\xA9" "dition)");
    mailUser_->setMaxLength(254);
    mailPassword_ = body_->make<ui::InputText>("service.mail.mot-de-passe");
    mailPassword_->setMasked(true);
    mailPassword_->setMaxLength(200);
    mailShow_ = body_->make<ui::Button>("Montrer", "service.mail.mot-de-passe.montrer");
    mailSave_ = body_->make<ui::Button>("Enregistrer le mail", "service.mail.enregistrer");
    mailSave_->setStyle(ui::Button::Style::Primary);
    mailTest_ = body_->make<ui::Button>("Envoyer un mail d'essai", "service.mail.essai");
    mailTest_->setIcon(ui::Icon::Mail);
    mailTest_->setTooltip("Un mail d'essai \xC3\xA0 ta propre adresse d'exp\xC3\xA9" "dition : il dit tout de suite si le r\xC3\xA9glage et le mot de passe marchent");
    mailForget_ = body_->make<ui::Button>("Oublier le r\xC3\xA9glage", "service.mail.oublier");
    // 1.38.0 : Outlook, Hotmail, Microsoft 365 - la connexion Microsoft (un code a taper dans le navigateur, jamais de mot de passe).
    mailAuth_ = body_->make<ui::DropDown>("service.mail.connexion");
    mailAuth_->setItems({{"Mot de passe d'application (Gmail, autres)", mail::kAuthPassword},
                         {"Compte Microsoft (Outlook, Hotmail, Microsoft 365)", mail::kAuthMicrosoft}});
    mailAuth_->setSelectedIndex(0);
    mailOutlook_ = body_->make<ui::Button>("Outlook / Hotmail", "service.mail.outlook");
    mailOutlook_->setTooltip("Une bo\xC3\xAEte Outlook.com, Hotmail ou Live : smtp-mail.outlook.com, port 587, STARTTLS, connexion Microsoft");
    mailM365_ = body_->make<ui::Button>("Microsoft 365", "service.mail.m365");
    mailM365_->setTooltip("Une bo\xC3\xAEte professionnelle Microsoft 365 : smtp.office365.com, port 587, STARTTLS, connexion Microsoft");
    mailClientId_ = body_->make<ui::InputText>("service.mail.id-application");
    mailClientId_->setPlaceholder("00000000-0000-0000-0000-000000000000");
    mailClientId_->setTooltip("L'ID d'application (client) de ton inscription sur le portail Microsoft Entra");
    mailClientId_->setMaxLength(36);
    mailConnect_ = body_->make<ui::Button>("Se connecter \xC3\xA0 Microsoft\xE2\x80\xA6", "service.mail.microsoft.connecter");
    mailConnect_->setStyle(ui::Button::Style::Primary);
    mailConnect_->setTooltip("Demande un code \xC3\xA0 Microsoft : tu le tapes dans ton navigateur (microsoft.com/devicelogin), tu te connectes \xC3\xA0 ta bo\xC3\xAEte, "
                             "et l'outil garde la connexion chiffr\xC3\xA9" "e sur ce poste - jamais ton mot de passe");
    mailCancel_ = body_->make<ui::Button>("Annuler la connexion", "service.mail.microsoft.annuler");
    mailCopyCode_ = body_->make<ui::Button>("Copier le code", "service.mail.microsoft.copier");
    mailDisconnect_ = body_->make<ui::Button>("Se d\xC3\xA9" "connecter de Microsoft", "service.mail.microsoft.oublier");
    links_ += mailAuth_->selectionChanged->connect([this](int) {
        // Compte Microsoft sans serveur : la boite Outlook / Hotmail toute faite (le cas le plus courant).
        if (mailAuth_->selectedIndex() == 1 && mailHost_->text().empty()) {
            const auto c = mail::microsoftPreset(true);
            mailHost_->setText(c.host);
            mailPort_->setText(std::to_string(c.port));
            mailSecurity_->setSelectedIndex(c.security == "tls" ? 1 : 0);
        }
        refresh();
    });
    links_ += mailOutlook_->clicked->connect([this] {
        const auto c = mail::microsoftPreset(true);
        mailHost_->setText(c.host);
        mailPort_->setText(std::to_string(c.port));
        mailSecurity_->setSelectedIndex(c.security == "tls" ? 1 : 0);
        mailAuth_->setSelectedIndex(1);
        refresh();
    });
    links_ += mailM365_->clicked->connect([this] {
        const auto c = mail::microsoftPreset(false);
        mailHost_->setText(c.host);
        mailPort_->setText(std::to_string(c.port));
        mailSecurity_->setSelectedIndex(c.security == "tls" ? 1 : 0);
        mailAuth_->setSelectedIndex(1);
        refresh();
    });
    links_ += mailConnect_->clicked->connect([this] { connectMicrosoft(); });
    links_ += mailCancel_->clicked->connect([this] {
        if (msStop_) msStop_->store(true);
    });
    links_ += mailCopyCode_->clicked->connect([this] {
        if (msCode_.empty()) return;
        ui::setClipboardText(msCode_);
        if (host_.flash) host_.flash("Code copi\xC3\xA9 : colle-le dans la page de Microsoft.", Tone::Ok);
    });
    links_ += mailDisconnect_->clicked->connect([this] { disconnectMicrosoft(); });
    links_ += mailGmail_->clicked->connect([this] {
        mailHost_->setText("smtp.gmail.com");
        mailPort_->setText("587");
        mailSecurity_->setSelectedIndex(0);
    });
    links_ += mailShow_->clicked->connect([this] {
        mailPassword_->setMasked(!mailPassword_->masked());
        mailShow_->setText(mailPassword_->masked() ? "Montrer" : "Masquer");
    });
    links_ += mailSave_->clicked->connect([this] { saveMail(); });
    links_ += mailTest_->clicked->connect([this] { testMail(); });
    links_ += mailForget_->clicked->connect([this] { forgetMail(); });
    loadMail();

    links_ += saveUrl_->clicked->connect([this] { saveUrl(); });
    // 1.44.0 : les adresses de secours
    for (int i = 0; i < 3; ++i) {
        fbShown_[i] = body_->make<ui::InputText>("service.secours." + std::to_string(i + 1));
        fbShown_[i]->setReadOnly(true);
        fbRemove_[i] = body_->make<ui::Button>("Retirer", "service.secours.retirer." + std::to_string(i + 1));
        links_ += fbRemove_[i]->clicked->connect([this, i] {
            if (static_cast<std::size_t>(i) < fbDraft_.size()) fbDraft_.erase(fbDraft_.begin() + i);
            fbDirty_ = true;
            rebuild();
            syncEnabled();
        });
    }
    fbNew_ = body_->make<ui::InputText>("service.secours.nouvelle");
    fbNew_->setPlaceholder("https://xpg-licences-secours.ton-compte.workers.dev");
    fbNew_->setMaxLength(220);
    fbAdd_ = body_->make<ui::Button>("Ajouter\xE2\x80\xA6", "service.secours.ajouter");
    fbAdd_->setIcon(ui::Icon::Plus);
    fbSave_ = body_->make<ui::Button>("Enregistrer (signer le registre)", "service.secours.enregistrer");
    fbSave_->setStyle(ui::Button::Style::Primary);
    links_ += fbAdd_->clicked->connect([this] {
        std::string u = trimmed(fbNew_->text());
        if (u.empty()) return refuse("tape l'adresse du service de secours (https://...)");
        if (u.rfind("https://", 0) != 0) return refuse("une adresse de secours commence par https://");
        while (!u.empty() && u.back() == '/') u.pop_back();
        if (std::find(fbDraft_.begin(), fbDraft_.end(), u) != fbDraft_.end()) return refuse("cette adresse est d\xC3\xA9j\xC3\xA0 dans la liste");
        if (fbDraft_.size() >= 3) return refuse("3 adresses de secours au plus");
        fbDraft_.push_back(u);
        fbNew_->setText({});
        fbDirty_ = true;
        rebuild();
        syncEnabled();
    });
    links_ += fbSave_->clicked->connect([this] { saveFallback(); });
    links_ += verify_->clicked->connect([this] { verify(); });
    links_ += certify_->clicked->connect([this] { askCertify(); });
    links_ += mode_->valueChanged->connect([this](int) {
        rebuild();
        syncEnabled();
    });
    links_ += showTyped_->clicked->connect([this] {
        code_->setMasked(!code_->masked());
        showTyped_->setText(code_->masked() ? "Montrer" : "Masquer");
    });
    links_ += keyButton_->clicked->connect([this] { syncKey(); });
    links_ += sync_->clicked->connect([this] { syncArchive(1); });
    links_ += receive_->clicked->connect([this] { syncArchive(2); });
    links_ += dry_->clicked->connect([this] { syncArchive(0); });
    links_ += showCode_->clicked->connect([this] { askShowCode(); });
    links_ += copy_->clicked->connect([this] { copyCode(); });
    links_ += noted_->stateChanged->connect([this](ui::Checkbox::State) { syncEnabled(); });
    links_ += continue_->clicked->connect([this] { acknowledge(); });
    links_ += hide_->clicked->connect([this] { hideCode(); });
    links_ += list_->clicked->connect([this] { listLicences(); });
    links_ += stats_->clicked->connect([this] { stats(); });
    links_ += publicKey_->clicked->connect([this] { writePublicKey(); });

    if (ws().service().configured && ws().service().problem.empty()) url_->setText(ws().service().config.url);
    refresh();
}

void ServicePage::refresh() {
    if (reveal_ == Reveal::Asked) {   // montre sur demande : cache au retour sur la page, apres une operation
        wipe(recovery_);
        reveal_ = Reveal::None;
    }
    lastBusy_ = busy();
    if (!lastBusy_) running_.clear();
    if (url_->text().empty() && ws().service().ready()) url_->setText(ws().service().config.url);
    rebuild();
    syncEnabled();
}

// ---------------------------------------------------------------------------------------------------- 1.44.0 : les adresses de secours
void ServicePage::fallbackBlocks(Blocks& bl) {
    if (!fbDirty_) fbDraft_ = ws().fallbackServices();
    const bool may = ws().can("offres");
    if (!may && fbDraft_.empty()) return;   // un admin sans le droit : rien a montrer s'il n'y en a pas
    bl.card("Les adresses de secours", fbDraft_.empty() ? ui::Tone::None : ui::Tone::Ok);
    bl.text("Un Studio qui ne joint pas l'adresse principale essaie celles-ci ; chaque service de secours se certifie comme le principal. Les Studios 1.43 "
            "et avant refusent un registre qui en contient : mets-en seulement quand tes clients sont en 1.44.");
    for (int i = 0; i < 3; ++i) {
        if (static_cast<std::size_t>(i) < fbDraft_.size()) {
            if (fbShown_[i]->text() != fbDraft_[static_cast<std::size_t>(i)]) fbShown_[i]->setText(fbDraft_[static_cast<std::size_t>(i)]);
            bl.field("Secours " + std::to_string(i + 1), fbShown_[i], 460.f, {fbRemove_[i]});
        }
    }
    if (fbDraft_.empty()) bl.note("Aucune adresse de secours : un Studio qui ne joint pas le service attend qu'il revienne.");
    if (fbDraft_.size() < 3 && may) bl.field("Nouvelle adresse", fbNew_, 460.f, {fbAdd_});
    if (fbDirty_) bl.note("Pas encore enregistr\xC3\xA9" "es : \xC2\xAB Enregistrer \xC2\xBB signe un nouveau registre (n\xC2\xB0 + 1) et le publie.", ui::Tone::Warning);
    if (!may) bl.note(ws().whyNot("offres"), ui::Tone::Warning);
    bl.row({fbSave_});
    bl.end();
}

void ServicePage::saveFallback() {
    if (refuseIfBusy()) return;
    const OpResult r = ws().setFallbackServices(fbDraft_);
    if (r.ok) fbDirty_ = false;
    if (host_.report) host_.report(r);
    if (r.ok) (void)publishToService(host_);   // le registre signe part sur le service, comme toute signature du registre
    rebuild();
    syncEnabled();
}

void ServicePage::syncEnabled() {
    const bool b = busy();
    const auto& s = ws().service();
    // 1.44.0 : un admin (sa cle signe ses appels) verifie, synchronise, releve ; certifier reste a l'admin+ (la cle de l'editeur).
    const bool key = ws().signer() != nullptr;
    const bool plus = ws().key().present;
    saveUrl_->setEnabled(!b);
    // 1.44.0 : les adresses de secours - l'admin+ ou le droit << offres >> (le registre est signe).
    const bool mayFallback = ws().can("offres");
    for (int i = 0; i < 3; ++i) fbRemove_[i]->setEnabled(!b && mayFallback);
    fbAdd_->setEnabled(!b && mayFallback);
    fbSave_->setEnabled(!b && mayFallback && fbDirty_ && ws().hasRegistry());
    if (!mayFallback) fbSave_->setTooltip(ws().whyNot("offres"));
    verify_->setEnabled(!b && s.ready() && key);
    certify_->setEnabled(!b && s.ready() && plus);
    if (!plus && key) certify_->setTooltip("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+ : seule la cl\xC3\xA9 de l'\xC3\xA9" "diteur certifie le service.");
    for (auto* w : std::initializer_list<ui::Widget*>{firstPc_, otherPc_, replace_, keyButton_, showCode_}) w->setEnabled(!b);
    sync_->setEnabled(!b && s.ready() && key && s.syncKey);
    receive_->setEnabled(!b && s.ready() && key && s.syncKey);
    dry_->setEnabled(!b && s.ready() && key && s.syncKey);
    list_->setEnabled(!b && s.ready() && key);
    stats_->setEnabled(!b && s.ready() && key);
    // 1.37.0 : le mail
    const bool mailSet = mail::configured(ws().folder());
    mailSave_->setEnabled(!b);
    mailTest_->setEnabled(!b && mailSet && mail::readyToSend(ws().folder()));   // 1.38.0 : un mot de passe OU Microsoft
    mailForget_->setEnabled(!b && (mailSet || mail::hasPassword(ws().folder()) || mail::microsoftConnected(ws().folder())));
    mailConnect_->setEnabled(!b);
    mailCancel_->setEnabled(msWaiting_);   // l'attente de la connexion : le seul bouton qui reste
    mailDisconnect_->setEnabled(!b && mail::microsoftConnected(ws().folder()));
    publicKey_->setEnabled(key);
    continue_->setEnabled(noted_->isChecked());
    // 1.45.0 : deployer - pas pendant un travail lent (le deploiement en est un), un seul a la fois.
    for (ui::Button* d : {deploy_, deployBanner_, deploySteps_}) d->setEnabled(!b && !deployer::running());
}

void ServicePage::note(Last& l, const std::string& what, const OpResult& r) {
    l.has = true;
    l.ok = r.ok;
    l.what = what;
    l.message = r.message;
    l.details = r.details;
    l.at = ws().now();
}

bool ServicePage::slow(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
    if (refuseIfBusy()) return false;
    const auto alive = aliveCheck();
    running_ = label;
    const bool started = host_.runSlow && host_.runSlow(label, std::move(work), [this, alive, done](const OpResult& r) {
                             if (!alive()) return;
                             running_.clear();
                             if (done) done(r);
                             if (alive()) refresh();
                         });
    if (!started) {
        running_.clear();
        refuse("un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence");
    }
    if (alive()) {
        lastBusy_ = busy();
        rebuild();
        syncEnabled();
    }
    return started;
}

void ServicePage::rebuild() {
    const auto& s = ws().service();
    const auto& k = ws().key();
    const Seconds now = ws().now();
    Blocks bl;
    if (busy()) bl.text((running_.empty() ? std::string("Un travail est en cours") : running_) + "\xE2\x80\xA6 Le r\xC3\xA9seau peut prendre quelques secondes ; les boutons "
                        "reviennent quand il a fini.", ui::Tone::Info);

    if (!recovery_.empty()) {
        bl.card("Code de secours de la cl\xC3\xA9 de synchro", ui::Tone::Warning);
        if (reveal_ == Reveal::First)
            bl.text("Note-le MAINTENANT hors de ce PC : sur papier rang\xC3\xA9 au coffre, ou dans ton gestionnaire de mots de passe. Il redonne la "
                    "cl\xC3\xA9 de synchro sur un autre PC Admin (ou sur celui-ci, r\xC3\xA9install\xC3\xA9) ; sans lui, l'archive en ligne est illisible. "
                    "Il ne sera plus montr\xC3\xA9, sauf si tu le demandes (Afficher le code de secours\xE2\x80\xA6).");
        else
            bl.text("Avec ce code et ta cl\xC3\xA9 d'\xC3\xA9" "diteur, on lit toute l'archive des licences : ne le montre \xC3\xA0 personne, ne l'envoie pas par "
                    "e-mail. Masque-le d\xC3\xA8s qu'il est not\xC3\xA9.");
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

    // ---- 1.45.0 : le service en ligne est plus ancien que ce qu'il faut a cet Admin (ou que le kit qu'il porte) ----
    const deployer::VersionCheck ver = serviceVersion(ws());
    if (ver.outdated) {
        bl.card("Ton service en ligne est \xC3\xA0 mettre \xC3\xA0 jour", ui::Tone::Warning);
        bl.text(deployer::outdatedMessage(ver), ui::Tone::Warning);
        bl.note("La mise en ligne remplace le programme du service, rien d'autre : les licences, les activations et ton archive restent chez "
                "Cloudflare. Compte une minute (quelques-unes la premi\xC3\xA8re fois).");
        bl.row({deployBanner_});
        bl.end();
    }

    // ---- l'etat ----
    const bool reached = s.hasState && s.state.reached;
    bl.card("Le service", !s.problem.empty() ? ui::Tone::Error : s.ready() && reached && s.state.adminOk ? ui::Tone::Ok : ui::Tone::Warning);
    bl.field("Adresse", url_, 460.f, {saveUrl_});
    bl.note("L'adresse que la mise en ligne t'a donn\xC3\xA9" "e (https://xpg-licences.ton-compte.workers.dev). Ce n'est pas un secret : elle va aussi dans les Studios.");
    if (!s.problem.empty()) bl.text("Probl\xC3\xA8me : " + s.problem, ui::Tone::Error);
    if (!s.configured) {
        bl.pair("\xC3\x89tat", "pas encore r\xC3\xA9gl\xC3\xA9 : mets le service en ligne (les \xC3\xA9tapes plus bas), puis donne son adresse", ui::Tone::Warning);
    } else if (!s.hasState) {
        bl.pair("\xC3\x89tat", "pas encore v\xC3\xA9rifi\xC3\xA9 dans cette s\xC3\xA9" "ance : V\xC3\xA9rifier", ui::Tone::Muted);
    } else if (!s.state.reached) {
        bl.pair("\xC3\x89tat", "le service n'a pas r\xC3\xA9pondu (" + dateTimeFr(s.stateAt) + ")", ui::Tone::Error);
    } else {
        // 1.45.0 : sa version, et s'il est a mettre a jour
        bl.pair("\xC3\x89tat", "il r\xC3\xA9pond (version " + (s.state.version.empty() ? std::string("?") : s.state.version) +
                                    (ver.outdated ? " \xE2\x80\x94 \xC3\xA0 mettre \xC3\xA0 jour : la " + ver.target + " est pr\xC3\xAAte" : std::string()) + ", v\xC3\xA9rifi\xC3\xA9 le " +
                                    dateTimeFr(s.stateAt) + ")",
                ver.outdated ? ui::Tone::Warning : ui::Tone::Ok);
        bl.pair("Ta cl\xC3\xA9 d'\xC3\xA9" "diteur", s.state.adminOk ? k.id + " : accept\xC3\xA9" "e par le service"
                                                    : std::string("REFUS\xC3\x89" "E : sa cl\xC3\xA9 publique doit \xC3\xAAtre dans PUBLISHER_KEYS du Worker (cle-publique.txt)"),
                s.state.adminOk ? ui::Tone::None : ui::Tone::Error);
        bl.pair("Registre d\xC3\xA9pos\xC3\xA9", s.state.registry ? "n\xC2\xB0 " + std::to_string(s.state.registry) + (ws().hasRegistry() && ws().registry().number > s.state.registry
                                                                                                         ? " (l'archive a le n\xC2\xB0 " + std::to_string(ws().registry().number) + " : il part \xC3\xA0 la prochaine signature)"
                                                                                                         : std::string())
                                                              : std::string("aucun (\xC3\x89" "ditions : signe le registre, il part sur le service)"),
                s.state.registry ? ui::Tone::None : ui::Tone::Warning);
    }
    if (!k.present && !ws().signer())   // 1.44.0 : un admin signe ses appels avec sa cle d'admin
        bl.text("Il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9), ou \xC3\xAAtre admin (Administrateurs) : elle signe chaque appel au service.",
                ui::Tone::Warning);
    if (service_.has)
        bl.pair("Dernier r\xC3\xA9sultat", service_.message + " (" + dateTimeFr(service_.at) + ")", service_.ok ? ui::Tone::Ok : ui::Tone::Error);
    bl.row({verify_, deploy_});   // 1.45.0 : Deployer le service
    bl.end();

    mailBlocks(bl);   // 1.37.0

    // ---- la cle du service et son certificat ----
    const bool certOk = s.hasState && s.state.certOk;
    const bool renew = s.hasState && s.state.needsRenewal(now);
    bl.card("La cl\xC3\xA9 du service et son certificat", !s.hasState ? ui::Tone::None : !certOk ? ui::Tone::Error : renew ? ui::Tone::Warning : ui::Tone::Ok);
    bl.text("Le service a cr\xC3\xA9\xC3\xA9 sa propre cl\xC3\xA9 : elle signe les activations, les jetons flottants et l'heure. Les Studios ne la croient que si "
            "TA cl\xC3\xA9 la certifie : un certificat d'un an, sign\xC3\xA9 ici, d\xC3\xA9pos\xC3\xA9 sur le service. Renouvelle-le avant sa fin (un mois avant, la page le dit).");
    if (s.hasState && s.state.reached && !s.state.serviceKey.empty()) bl.pair("Cl\xC3\xA9 du service", online::keyFingerprint(s.state.serviceKey));
    if (!s.hasState) bl.pair("Certificat", "V\xC3\xA9rifier d'abord", ui::Tone::Muted);
    else if (certOk)
        bl.pair("Certificat", "valable jusqu'au " + dateFr(s.state.certExpires) + (renew ? " \xE2\x80\x94 \xC3\xA0 renouveler maintenant" : std::string()),
                renew ? ui::Tone::Warning : ui::Tone::Ok);
    else if (s.state.reached)
        bl.pair("Certificat", s.state.certProblem.empty() ? std::string("absent") : s.state.certProblem, ui::Tone::Error);
    certify_->setText(certOk ? "Renouveler le certificat\xE2\x80\xA6" : "Certifier la cl\xC3\xA9 du service\xE2\x80\xA6");
    bl.row({certify_});
    bl.note("Certifier lit d'abord la cl\xC3\xA9 du service et te montre son empreinte : compare-la avec le champ cle_service de "
            "<adresse>/v1/etat ouvert dans ton navigateur. Rien n'est sign\xC3\xA9 sans ton accord.");
    bl.end();

    fallbackBlocks(bl);   // 1.44.0

    // ---- l'archive en ligne ----
    const auto& last = s.lastSync;
    bl.card("L'archive en ligne", s.syncKey ? (last.at ? ui::Tone::Ok : ui::Tone::Warning) : ui::Tone::Warning);
    bl.text("Tes licences, ton registre, tes r\xC3\xA9vocations et le registre des activations, gard\xC3\xA9s aussi sur le service pour tes autres PC Admin "
            "(ou apr\xC3\xA8s une panne). Tout ce qui nomme un client part CHIFFR\xC3\x89 par la cl\xC3\xA9 de synchro : le service ne lit rien.");
    bl.pair("Cl\xC3\xA9 de synchro", s.syncKey ? s.syncKeyId : std::string("pas encore sur ce poste"), s.syncKey ? ui::Tone::None : ui::Tone::Warning);
    bl.pair("Derni\xC3\xA8re synchro", last.at ? dateTimeFr(last.at) + " (version " + std::to_string(last.version) + ")" : std::string("jamais depuis ce poste"),
            last.at ? ui::Tone::None : ui::Tone::Muted);
    if (!s.syncKey) {
        bl.row({firstPc_});
        bl.row({otherPc_});
        if (mode_->value() == 1) {
            bl.field("Code de secours", code_, 560.f, {showTyped_});
            bl.note("Le code (XPGS1-\xE2\x80\xA6) a \xC3\xA9t\xC3\xA9 not\xC3\xA9 quand le premier poste a cr\xC3\xA9\xC3\xA9 sa cl\xC3\xA9 ; sur ce poste-l\xC3\xA0 : Afficher le code de secours\xE2\x80\xA6");
            keyButton_->setText("Redonner la cl\xC3\xA9 de synchro");
        } else {
            bl.note("La cl\xC3\xA9 est tir\xC3\xA9" "e sur ce poste ; son code de secours s'affiche une seule fois : note-le hors de ce PC.");
            keyButton_->setText("Cr\xC3\xA9" "er la cl\xC3\xA9 de synchro");
        }
        bl.row({keyButton_});
    } else {
        bl.row({sync_, receive_, dry_});
        if (recovery_.empty()) bl.row({showCode_});
    }
    if (archive_.has) {
        bl.pair("Dernier r\xC3\xA9sultat", archive_.message + " (" + dateTimeFr(archive_.at) + ")", archive_.ok ? ui::Tone::Ok : ui::Tone::Error);
        for (std::size_t i = 0; i < archive_.details.size() && i < 30; ++i) bl.bullet(archive_.details[i]);
        if (archive_.details.size() > 30) bl.note("\xE2\x80\xA6 et " + std::to_string(archive_.details.size() - 30) + " autres.");
    }
    bl.end();

    // ---- les licences en ligne ----
    bl.card("Les licences en ligne");
    const auto& snap = s.snapshot;
    if (snap.at == 0) {
        bl.pair("Dernier relev\xC3\xA9", "jamais sur ce poste", ui::Tone::Muted);
    } else {
        int floating = 0;
        for (const auto& l : snap.licences) floating += l.floating() ? 1 : 0;
        bl.pair("Dernier relev\xC3\xA9", dateTimeFr(snap.at));
        bl.pair("Licences en ligne", std::to_string(snap.licences.size()) + (floating ? " (dont " + std::to_string(floating) + " flottante(s))" : std::string()));
        bl.pair("PC activ\xC3\xA9s", std::to_string(snap.devices()));
        if (floating) bl.pair("Jetons en cours", std::to_string(snap.leases()));
    }
    bl.row({list_, stats_});
    if (licences_.has) {
        bl.pair("Dernier r\xC3\xA9sultat", licences_.message + " (" + dateTimeFr(licences_.at) + ")", licences_.ok ? ui::Tone::Ok : ui::Tone::Error);
        for (std::size_t i = 0; i < licences_.details.size() && i < 30; ++i) bl.bullet(licences_.details[i]);
    }
    bl.note("Le d\xC3\xA9tail (les PC de chaque licence, Retirer ce PC) : la page Licences ; les jetons : Licences flottantes.");
    bl.end();

    // ---- mettre le service en ligne (outils/service/LISEZ-MOI-SERVICE.txt, resume) ----
    bl.card("Mettre le service en ligne (une fois)");
    bl.text("Le service est un petit programme (un Worker) que tu mets en ligne toi-m\xC3\xAAme, sur ton compte Cloudflare gratuit ; compte une "
            "demi-heure la premi\xC3\xA8re fois. Le pas \xC3\xA0 pas complet : LISEZ-MOI-SERVICE.txt, avec le dossier du service. En bref :");
    bl.bullet("1. Installe Node.js (la version LTS, 22 ou plus) : nodejs.org.");
    bl.bullet("2. Cr\xC3\xA9" "e ton compte Cloudflare (gratuit) : dash.cloudflare.com/sign-up ; dans \xC2\xAB Workers et Pages \xC2\xBB, choisis ton sous-domaine.");
    bl.bullet("3. Le dossier du service, install\xC3\xA9 avec l'Admin (menu D\xC3\xA9marrer \xE2\x80\xBA D\xC3\xA9ployer le service en ligne), contient d\xC3\xA9j\xC3\xA0 "
              "tout ; le bouton ci-dessous y \xC3\xA9" "crit cle-publique.txt (ta cl\xC3\xA9 PUBLIQUE).");
    // 1.45.0 : le bouton << Deployer le service >> fait la connexion et la mise en ligne (deployer.bat reste la).
    bl.bullet("4. Une fois par PC, connecte-le \xC3\xA0 Cloudflare : \xC2\xAB D\xC3\xA9ployer le service \xC2\xBB te le propose (Se connecter \xC3\xA0 Cloudflare\xE2\x80\xA6) ; ou, dans "
              "un terminal ouvert dans le dossier service : npx wrangler login.");
    bl.bullet("5. \xC2\xAB D\xC3\xA9ployer le service \xC2\xBB (ci-dessous) le met en ligne, et \xC3\xA0 jour apr\xC3\xA8s chaque mise \xC3\xA0 jour de l'Admin ; c'est ce que fait le menu "
              "D\xC3\xA9marrer \xE2\x80\xBA D\xC3\xA9ployer le service en ligne (deployer.bat). Il donne l'adresse https://xpg-licences.<ton-compte>.workers.dev (gard\xC3\xA9" "e "
              "aussi dans adresse-du-service.txt) et la r\xC3\xA8gle ici si l'Admin n'en a pas.");
    bl.bullet("6. Colle-la plus haut, Enregistrer l'adresse, V\xC3\xA9rifier, puis Certifier la cl\xC3\xA9 du service (et Renouveler, chaque ann\xC3\xA9" "e).");
    bl.bullet("7. Signe le registre (\xC3\x89" "ditions : il part sur le service), synchronise l'archive, puis cr\xC3\xA9" "e tes licences en ligne (Licences).");
    bl.bullet("8. Pour les Studios livr\xC3\xA9s : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Pr\xC3\xA9parer les fichiers pour les Studios (ServiceAddress.hpp porte "
              "l'adresse), puis reconstruire les Studios.");
    bl.row({deploySteps_, publicKey_});   // 1.45.0
    bl.note("Ce qui ne va jamais sur le service : ta cl\xC3\xA9 priv\xC3\xA9" "e, la cl\xC3\xA9 de synchro, son code de secours, un code d'activation (le service n'en "
            "garde que l'empreinte).");
    bl.end();
    body_->setBlocks(std::move(bl.items));
}

// ---------------------------------------------------------------------------------------------------- 1.37.0 : le mail
bool ServicePage::microsoftChosen() const {
    const auto* a = mailAuth_->selectedItem();
    return a && a->value == mail::kAuthMicrosoft;
}

void ServicePage::mailBlocks(Blocks& bl) {
    const std::string folder = ws().folder();
    const bool set = mail::configured(folder);
    const bool ms = microsoftChosen();
    bl.card("Le mail au client", set && mail::readyToSend(folder) ? ui::Tone::Ok : ui::Tone::None);
    bl.text("Quand tu acceptes une demande, un mail part tout seul au client (s'il a donn\xC3\xA9 son courriel) depuis TA bo\xC3\xAEte : sa licence, "
            "ce qu'elle ouvre, jusqu'\xC3\xA0 quand, et le code d'une licence en ligne. Pas parti (une adresse fausse, ta bo\xC3\xAEte injoignable) : le "
            "Studio du client le lui dit, lui montre son code et lui demande de v\xC3\xA9rifier son adresse. Rien de r\xC3\xA9gl\xC3\xA9 : aucun mail (le "
            "Studio montre le code).");
    mail::Config cfg;
    const bool loaded = set && mail::loadConfig(folder, cfg, nullptr);
    mail::MicrosoftLink link;
    const bool linked = mail::microsoftConnected(folder) && mail::microsoftLink(folder, link, nullptr);
    if (loaded && cfg.auth == mail::kAuthMicrosoft)
        bl.pair("\xC3\x89tat", "r\xC3\xA9gl\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) +
                                    (linked ? " (connexion Microsoft" + (link.account.empty() ? std::string() : " : " + link.account) + ", gard\xC3\xA9" "e chiffr\xC3\xA9" "e sur ce poste)"
                                            : std::string(" \xE2\x80\x94 pas encore connect\xC3\xA9 \xC3\xA0 Microsoft")),
                linked ? ui::Tone::Ok : ui::Tone::Warning);
    else if (loaded)
        bl.pair("\xC3\x89tat", "r\xC3\xA9gl\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) +
                                    (mail::hasPassword(folder) ? std::string(" (mot de passe gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste)") : std::string(" \xE2\x80\x94 il manque le mot de passe")),
                mail::hasPassword(folder) ? ui::Tone::Ok : ui::Tone::Warning);
    else
        bl.pair("\xC3\x89tat", "pas r\xC3\xA9gl\xC3\xA9 : aucun mail ne part", ui::Tone::Muted);
    bl.field("Connexion", mailAuth_, 380.f);
    bl.field("Serveur", mailHost_, 300.f, {mailGmail_, mailOutlook_, mailM365_});
    bl.field("Port", mailPort_, 90.f);
    bl.field("S\xC3\xA9" "curit\xC3\xA9", mailSecurity_, 300.f);
    bl.field("Adresse d'exp\xC3\xA9" "dition", mailFrom_, 360.f);
    bl.field("Nom montr\xC3\xA9", mailName_, 360.f);
    bl.field("Identifiant", mailUser_, 360.f);
    if (!ms) {
        bl.field("Mot de passe d'application", mailPassword_, 300.f, {mailShow_});
        bl.note(mail::hasPassword(folder)
                    ? "Un mot de passe est d\xC3\xA9j\xC3\xA0 gard\xC3\xA9 (chiffr\xC3\xA9 pour ton compte Windows) : laisse le champ vide pour le garder, ou tape le nouveau."
                    : "Gmail : active la validation en deux \xC3\xA9tapes de ton compte Google, puis cr\xC3\xA9" "e un \xC2\xAB mot de passe d'application \xC2\xBB "
                      "(myaccount.google.com \xE2\x80\xBA S\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Mots de passe des applications) et colle-le ici. Jamais ton mot de "
                      "passe Google. Outlook, Hotmail, Microsoft 365 : choisis Connexion \xE2\x80\xBA Compte Microsoft.");
        bl.row({mailSave_, mailTest_, mailForget_});
    } else {
        // 1.38.0 : la connexion Microsoft (un code a taper dans le navigateur ; l'outil ne voit jamais ton mot de passe).
        bl.field("ID d'application", mailClientId_, 380.f);
        bl.note("Une fois pour toutes, sur portal.azure.com (gratuit) : Microsoft Entra ID \xE2\x80\xBA Inscriptions d'applications \xE2\x80\xBA Nouvelle "
                "inscription (\xC2\xAB Comptes dans un annuaire et comptes Microsoft personnels \xC2\xBB) ; Authentification \xE2\x80\xBA Autoriser les flux "
                "clients publics : Oui ; Autorisations d'API \xE2\x80\xBA Ajouter \xE2\x80\xBA Office 365 Exchange Online (ou Microsoft Graph) \xE2\x80\xBA "
                "D\xC3\xA9l\xC3\xA9gu\xC3\xA9" "es : SMTP.Send et offline_access. Copie ici son \xC2\xAB ID d'application (client) \xC2\xBB, Enregistrer le mail, "
                "puis Se connecter. Microsoft 365 : l'administrateur doit permettre \xC2\xAB SMTP authentifi\xC3\xA9 \xC2\xBB pour ta bo\xC3\xAEte.");
        if (msWaiting_ && !msCode_.empty()) {
            bl.pair("Code \xC3\xA0 taper", msCode_, ui::Tone::Warning);
            bl.pair("Adresse", msUri_.empty() ? std::string("https://microsoft.com/devicelogin") : msUri_, ui::Tone::None);
            bl.note("Ouvre cette adresse dans ton navigateur, tape le code, puis connecte-toi \xC3\xA0 la bo\xC3\xAEte d'exp\xC3\xA9" "dition : l'outil attend ta "
                    "connexion et la garde chiffr\xC3\xA9" "e sur ce poste (jamais ton mot de passe).");
            bl.row({mailCopyCode_, mailCancel_});
        } else if (linked) {
            bl.pair("Connect\xC3\xA9", (link.account.empty() ? std::string("compte Microsoft") : link.account) + (link.since ? " depuis le " + dateTimeFr(link.since) : std::string()),
                    ui::Tone::Ok);
        }
        bl.row({mailSave_, mailConnect_, mailTest_});
        bl.row({mailDisconnect_, mailForget_});
    }
    if (mail_.has) {
        bl.pair("Dernier r\xC3\xA9sultat", mail_.message + " (" + dateTimeFr(mail_.at) + ")", mail_.ok ? ui::Tone::Ok : ui::Tone::Error);
        for (std::size_t i = 0; i < mail_.details.size() && i < 6; ++i) bl.bullet(mail_.details[i]);
    }
    bl.end();
}

void ServicePage::loadMail() {
    mail::Config cfg;
    if (!mail::configured(ws().folder()) || !mail::loadConfig(ws().folder(), cfg, nullptr)) return;
    mailHost_->setText(cfg.host);
    mailPort_->setText(std::to_string(cfg.port));
    mailSecurity_->setSelectedIndex(cfg.security == "tls" ? 1 : 0);
    mailFrom_->setText(cfg.from);
    mailName_->setText(cfg.fromName);
    mailUser_->setText(cfg.user == cfg.from ? std::string() : cfg.user);
    mailAuth_->setSelectedIndex(cfg.auth == mail::kAuthMicrosoft ? 1 : 0);   // 1.38.0
    mailClientId_->setText(cfg.clientId);
}

// 1.38.0 : le reglage tel que la page le montre (Microsoft : les reglages tout faits de la boite, son ID d'application).
mail::Config ServicePage::mailFromFields() const {
    mail::Config cfg;
    const auto trim = [](std::string t) {
        const auto b = t.find_first_not_of(" \t");
        if (b == std::string::npos) return std::string();
        return t.substr(b, t.find_last_not_of(" \t") - b + 1);
    };
    cfg.host = trim(mailHost_->text());
    const auto* sec = mailSecurity_->selectedItem();
    cfg.security = sec ? sec->value : std::string("starttls");
    const std::string port = trim(mailPort_->text());
    cfg.port = port.empty() ? mail::defaultPort(cfg.security) : std::atoi(port.c_str());
    cfg.from = trim(mailFrom_->text());
    cfg.fromName = trim(mailName_->text());
    cfg.user = trim(mailUser_->text());
    if (cfg.user.empty()) cfg.user = cfg.from;
    if (microsoftChosen()) {
        std::string id = trim(mailClientId_->text());
        for (char& ch : id) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        cfg.clientId = id;
        const bool office = cfg.host.find("office365") != std::string::npos;
        const std::string host = cfg.host;
        const int prt = cfg.port;
        const std::string security = cfg.security;
        cfg = mail::microsoftPreset(!office, cfg);
        if (!host.empty() && !mail::microsoftHost(host)) {   // un autre serveur (un relais) : le sien reste
            cfg.host = host;
            cfg.port = prt;
            cfg.security = security;
        }
    }
    return cfg;
}

void ServicePage::connectMicrosoft() {
    if (refuseIfBusy()) return;
    mail::Config cfg = mailFromFields();
    std::string why;
    if (!mail::validate(cfg, &why)) {
        refuse("la connexion Microsoft ne peut pas commencer : " + why);
        return;
    }
    if (!mail::saveConfig(ws().folder(), cfg, &why)) {   // le reglage d'abord : l'envoi saura qu'il passe par Microsoft
        refuse("le mail n'est pas enregistr\xC3\xA9 : " + why);
        return;
    }
    auto code = std::make_shared<mail::DeviceCode>();
    (void)slow("Microsoft : demande d'un code de connexion",
               [cfg, code] {
                   std::string w;
                   if (!mail::microsoftStart(cfg, *code, &w)) return OpResult::fail(w);
                   return OpResult::done("Code re\xC3\xA7u : " + mail::microsoftInstructions(*code));
               },
               [this, cfg, code](const OpResult& r) {
                   if (!r.ok) {
                       note(mail_, "mail", r);
                       if (host_.report) host_.report(r);
                       return;
                   }
                   waitMicrosoft(cfg, *code);
               });
}

void ServicePage::waitMicrosoft(mail::Config cfg, mail::DeviceCode dc) {
    msStop_ = std::make_shared<std::atomic<bool>>(false);
    msCode_ = dc.userCode;
    msUri_ = dc.verificationUri;
    msWaiting_ = true;
    const std::string folder = ws().folder();
    auto stop = msStop_;
    const bool started = slow("Microsoft : en attente de ta connexion (le code " + dc.userCode + ")",
                              [folder, cfg, dc, stop] {
                                  const auto p = mail::microsoftWait(folder, cfg, dc, [stop] { return stop->load(); });
                                  switch (p.state) {
                                      case mail::PollState::Done:
                                          return OpResult::done("Connect\xC3\xA9 \xC3\xA0 Microsoft" + (p.account.empty() ? std::string(".") : " : " + p.account + ".") +
                                                                " Essaie-le : Envoyer un mail d'essai.");
                                      case mail::PollState::Cancelled: return OpResult::fail("connexion Microsoft annul\xC3\xA9" "e");
                                      default: return OpResult::fail(p.message.empty() ? std::string("la connexion Microsoft n'a pas abouti") : p.message);
                                  }
                              },
                              [this](const OpResult& r) {
                                  msWaiting_ = false;
                                  msCode_.clear();
                                  msUri_.clear();
                                  msStop_.reset();
                                  note(mail_, "mail", r);
                                  if (host_.report) host_.report(r);
                              });
    if (!started) {
        msWaiting_ = false;
        msCode_.clear();
    }
    refresh();
}

void ServicePage::disconnectMicrosoft() {
    if (refuseIfBusy()) return;
    std::string why;
    if (!mail::microsoftForget(ws().folder(), &why)) {
        refuse("la connexion Microsoft n'est pas oubli\xC3\xA9" "e : " + why);
        return;
    }
    const OpResult r = OpResult::done("Connexion Microsoft oubli\xC3\xA9" "e sur ce poste : aucun mail ne part plus tant que tu ne te reconnectes pas.");
    note(mail_, "mail", r);
    if (host_.report) host_.report(r);
    refresh();
}

void ServicePage::saveMail() {
    if (refuseIfBusy()) return;
    const mail::Config cfg = mailFromFields();   // 1.38.0 : Microsoft compris
    std::string why;
    const std::string folder = ws().folder();
    if (!mail::saveConfig(folder, cfg, &why)) {
        refuse("le mail n'est pas enregistr\xC3\xA9 : " + why);
        return;
    }
    OpResult r = OpResult::done("Mail enregistr\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) + ".");
    std::string pw = cfg.auth == mail::kAuthMicrosoft ? std::string() : mailPassword_->text();
    if (cfg.auth == mail::kAuthMicrosoft) {   // 1.38.0
        clearSecret(mailPassword_);
        r.details.push_back(mail::microsoftConnected(folder) ? std::string("La connexion Microsoft de ce poste est gard\xC3\xA9" "e.")
                                                             : std::string("Il reste \xC3\xA0 te connecter : Se connecter \xC3\xA0 Microsoft\xE2\x80\xA6"));
    } else if (!pw.empty()) {
        const bool ok = mail::savePassword(folder, pw, &why);
        std::fill(pw.begin(), pw.end(), '\0');
        clearSecret(mailPassword_);
        if (!ok) {
            refuse("le mot de passe n'est pas gard\xC3\xA9 : " + why);
            return;
        }
        r.details.push_back("Le mot de passe d'application est gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste (pour ton compte Windows).");
    } else if (!mail::hasPassword(folder)) {
        r.details.push_back("Il manque le mot de passe d'application : tape-le, puis Enregistrer le mail.");
    }
    r.details.push_back("Essaie-le : Envoyer un mail d'essai (\xC3\xA0 ta propre adresse).");
    note(mail_, "mail", r);
    if (host_.report) host_.report(r);
    refresh();
}

void ServicePage::testMail() {
    const std::string folder = ws().folder();
    mail::Config cfg;
    std::string why;
    if (!mail::loadConfig(folder, cfg, &why)) {
        refuse("le mail n'est pas r\xC3\xA9gl\xC3\xA9 : " + why);
        return;
    }
    mail::Message m;
    m.to = cfg.from;
    m.toName = cfg.fromName;
    m.subject = "Essai du mail de l'outil de l'\xC3\xA9" "diteur XPG";
    m.body = "Bonjour,\n\nCe mail d'essai est parti de ton outil de l'\xC3\xA9" "diteur (XPG-License-Admin, page Service en ligne).\n"
             "Le r\xC3\xA9glage marche : \xC3\xA0 chaque demande accept\xC3\xA9" "e, le client recevra le sien (sa licence et, pour une licence en ligne, son "
             "code d'activation).\n";
    slow("Mail : un essai vers " + cfg.from, [folder, m] {
             const mail::SendResult r = mail::send(folder, m);
             return r.ok ? OpResult::done(r.message) : OpResult::fail(r.message);
         },
         [this](const OpResult& r) {
             note(mail_, "mail", r);
             if (host_.report) host_.report(r);
         });
}

void ServicePage::forgetMail() {
    if (refuseIfBusy()) return;
    std::string why;
    if (!mail::forget(ws().folder(), &why)) {
        refuse("le r\xC3\xA9glage du mail n'est pas oubli\xC3\xA9 : " + why);
        return;
    }
    for (auto* f : {mailHost_, mailPort_, mailFrom_, mailName_, mailUser_}) f->setText({});
    clearSecret(mailPassword_);
    const OpResult r = OpResult::done("R\xC3\xA9glage du mail oubli\xC3\xA9 (et le mot de passe) : aucun mail ne part plus.");
    note(mail_, "mail", r);
    if (host_.report) host_.report(r);
    refresh();
}

// ---------------------------------------------------------------------------------------------------- le service
void ServicePage::saveUrl() {
    if (refuseIfBusy()) return;
    const std::string url = trimmed(url_->text());
    if (url.empty()) {
        refuse("colle d'abord l'adresse du service (https://xpg-licences.ton-compte.workers.dev)");
        return;
    }
    const OpResult r = ws().serviceConfigure(url);
    if (r.ok) url_->setText(ws().service().config.url);
    if (host_.report) host_.report(r);
    refresh();
}

void ServicePage::verify() {
    if (!ws().service().ready()) {
        refuse("enregistre d'abord l'adresse du service");
        return;
    }
    const Workspace* w = &ws();
    auto st = std::make_shared<online::ServiceState>();
    (void)slow("Service : v\xC3\xA9rification", [w, st] { return w->serviceVerify(st.get()); }, [this, st](const OpResult& r) {
        note(service_, "v\xC3\xA9rification", r);
        ws().adoptServiceState(*st);
    });
}

void ServicePage::askCertify() {
    if (!ws().service().ready()) {
        refuse("enregistre d'abord l'adresse du service");
        return;
    }
    if (!ws().key().present) {
        refuse("il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur pour certifier (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9)");
        return;
    }
    const Workspace* w = &ws();
    auto keyHex = std::make_shared<std::string>();
    const auto alive = aliveCheck();
    (void)slow("Service : lecture de sa cl\xC3\xA9", [w, keyHex] { return w->serviceReadKey(*keyHex); }, [this, alive, keyHex](const OpResult& r) {
        note(service_, "lecture de la cl\xC3\xA9", r);
        if (!r.ok || keyHex->empty() || !host_.openDialog) return;
        const std::string hex = *keyHex;
        host_.openDialog(std::make_unique<ConfirmDialog>(
            "service.certification", "Certifier la cl\xC3\xA9 du service",
            "La cl\xC3\xA9 que ton service a cr\xC3\xA9\xC3\xA9" "e :\n\n" + online::keyFingerprint(hex) + "\n(cle_service commence par " + hex.substr(0, 16) +
                "\xE2\x80\xA6)\n\nCompare-la avec <adresse>/v1/etat ouvert dans ton navigateur. Ta cl\xC3\xA9 d'\xC3\xA9" "diteur va signer un certificat d'un an : les Studios "
                "croiront ce que cette cl\xC3\xA9 signe (activations, jetons, heure). Si ce n'est pas la bonne, Annuler.",
            "Certifier", false, [this, alive, hex] {
                if (alive()) certify(hex);
            }));
    });
}

void ServicePage::certify(const std::string& keyHex) {
    const Workspace* w = &ws();
    const auto alive = aliveCheck();
    (void)slow("Service : certification de sa cl\xC3\xA9", [w, keyHex] { return w->serviceCertify(keyHex); }, [this, alive](const OpResult& r) {
        note(service_, "certification", r);
        if (!r.ok) return;
        // L'etat relu (le certificat neuf), aussitot.
        const Workspace* wp = &ws();
        auto st = std::make_shared<online::ServiceState>();
        ShellHost h = host_;
        if (h.runSlow)
            (void)h.runSlow("Service : v\xC3\xA9rification", [wp, st] { return wp->serviceVerify(st.get()); }, [this, alive, st](const OpResult&) {
                if (!alive()) return;
                ws().adoptServiceState(*st);
                refresh();
            });
    });
}

// ---------------------------------------------------------------------------------------------------- l'archive
void ServicePage::syncKey() {
    if (refuseIfBusy()) return;
    const bool withCode = mode_->value() == 1;
    std::string code = withCode ? trimmed(code_->text()) : std::string();
    clearSecret(code_);
    code_->setMasked(true);
    showTyped_->setText("Montrer");
    if (withCode && code.empty()) {
        refuse("colle le code de secours (XPGS1-\xE2\x80\xA6), ou choisis \xC2\xAB Premier poste \xC2\xBB");
        return;
    }
    OpResult r;
    if (withCode) {
        r = ws().syncKeyRestore(code, replace_->isChecked());
        wipe(code);
        if (r.ok) {
            replace_->setState(ui::Checkbox::State::Unchecked);
            mode_->setValue(0);
        }
    } else {
        std::string recovery;
        r = ws().syncKeyCreate(&recovery);
        if (r.ok && !recovery.empty()) {
            wipe(recovery_);
            recovery_ = std::move(recovery);
            reveal_ = Reveal::First;
            noted_->setState(ui::Checkbox::State::Unchecked);
        }
        wipe(recovery);
    }
    if (host_.report) host_.report(r);
    refresh();
}

void ServicePage::syncArchive(int mode) {
    const auto& s = ws().service();
    if (!s.ready()) {
        refuse("enregistre d'abord l'adresse du service");
        return;
    }
    if (!s.syncKey) {
        refuse("pas de cl\xC3\xA9 de synchro sur ce poste : cr\xC3\xA9" "e-la (premier poste) ou redonne-la par ton code de secours");
        return;
    }
    const bool dry = mode == 0, receiveOnly = mode == 2;
    const std::string label = dry ? "v\xC3\xA9rification" : receiveOnly ? "r\xC3\xA9" "ception" : "synchronisation";
    auto holder = std::make_shared<archive::SyncReport>();
    const Workspace* w = &ws();
    (void)slow("Archive : " + label,
               [w, holder, receiveOnly, dry, mode] {
                   *holder = w->archiveSync(receiveOnly, dry);
                   return syncResult(*holder, mode);
               },
               [this, label, dry](const OpResult& r) {
                   note(archive_, label, r);
                   if (!dry) ws().reload();   // recu en partie avant un refus : la page le montre aussi
               });
}

// ---------------------------------------------------------------------------------------------------- les licences
void ServicePage::listLicences() {
    if (refuseIfBusy()) return;
    const auto alive = aliveCheck();
    running_ = "Relev\xC3\xA9 des licences en ligne";
    const bool started = refreshOnlineLicences(host_, [this, alive](const OpResult& r) {
        if (!alive()) return;
        running_.clear();
        note(licences_, "relev\xC3\xA9", r);
        refresh();
    });
    if (!started) {
        running_.clear();
        refuse("un travail est d\xC3\xA9j\xC3\xA0 en cours : attends qu'il finisse, puis recommence");
    }
    if (alive()) {
        rebuild();
        syncEnabled();
    }
}

void ServicePage::stats() {
    const Workspace* w = &ws();
    (void)slow("Service : les compteurs", [w] { return w->serviceStats(); }, [this](const OpResult& r) { note(licences_, "compteurs", r); });
}

// ---------------------------------------------------------------------------------------------------- 1.45.0 : deployer
void ServicePage::openDeploy() {
    if (refuseIfBusy()) return;
    if (deployer::running()) {
        refuse("une mise en ligne du service tourne d\xC3\xA9j\xC3\xA0 : attends qu'elle finisse");
        return;
    }
    if (!host_.openDialog) return;
    const auto alive = aliveCheck();
    host_.openDialog(makeDeployDialog(host_, [this, alive] {
        if (alive()) refresh();
    }));
}

// 1.40.0 : le dossier du service installe avec l'Admin (l'installateur : %LOCALAPPDATA%\XPG\ServiceLicences, avec
// deployer.bat) ; vide : pas installe (l'ancien zip, un autre PC) - on demande alors le dossier.
std::string installedServiceKit() {
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    if (!la || !*la) return {};
    const std::string dir = std::string(la) + "\\XPG\\ServiceLicences";
    std::error_code ec;
    const std::filesystem::path bat = std::filesystem::path(std::u8string(dir.begin(), dir.end())) / "deployer.bat";
    return std::filesystem::is_regular_file(bat, ec) ? dir : std::string();
#else
    return {};
#endif
}

void ServicePage::writePublicKey() {
    if (!ws().key().present) {
        refuse("il faut la cl\xC3\xA9 de l'\xC3\xA9" "diteur (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9)");
        return;
    }
    if (const std::string kit = installedServiceKit(); !kit.empty()) {   // 1.40.0 : le dossier installe, sans question
        if (host_.report) host_.report(ws().writePublicKeyFile(kit));
        return;
    }
    ui::FilePick p;
    p.title = "Le dossier o\xC3\xB9 \xC3\xA9" "crire cle-publique.txt (celui de deployer.bat)";
    p.folder = true;
    const auto alive = aliveCheck();
    if (host_.pickFile)
        host_.pickFile(p, [this, alive](std::string path) {
            if (!alive() || path.empty()) return;
            if (host_.report) host_.report(ws().writePublicKeyFile(path));
        });
}

// ---------------------------------------------------------------------------------------------------- le code de secours
void ServicePage::copyCode() {
    if (recovery_.empty()) return;
    ui::setClipboardText(recovery_);
    if (host_.flash)
        host_.flash("Code de secours copi\xC3\xA9. Colle-le dans ton gestionnaire de mots de passe, puis copie autre chose pour le retirer du "
                    "presse-papiers.",
                    Tone::Info);
}

void ServicePage::acknowledge() {
    if (!noted_->isChecked()) {
        refuse("coche d'abord \xC2\xAB Je l'ai not\xC3\xA9 hors de ce PC \xC2\xBB");
        return;
    }
    wipe(recovery_);
    reveal_ = Reveal::None;
    noted_->setState(ui::Checkbox::State::Unchecked);
    refresh();
}

void ServicePage::hideCode() {
    wipe(recovery_);
    reveal_ = Reveal::None;
    rebuild();
    syncEnabled();
}

void ServicePage::askShowCode() {
    if (!ws().service().syncKey) {
        refuse("pas de cl\xC3\xA9 de synchro sur ce poste");
        return;
    }
    const auto alive = aliveCheck();
    if (host_.openDialog)
        host_.openDialog(std::make_unique<ConfirmDialog>(
            "service.code.confirmation", "Afficher le code de secours",
            "Le code de secours redonne la cl\xC3\xA9 de synchro. Avec lui et ta cl\xC3\xA9 d'\xC3\xA9" "diteur, on lit toute l'archive des licences (les "
            "clients, leurs PC).\n\nMontre-le seulement si personne ne regarde ton \xC3\xA9" "cran, pour le noter hors de ce PC ; masque-le juste apr\xC3\xA8s.",
            "Afficher le code", false, [this, alive] {
                if (!alive()) return;
                std::string why;
                std::string code = ws().syncRecoveryCode(&why);
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

// ---------------------------------------------------------------------------------------------------- dessin
void ServicePage::onLayout() {
    const auto r = bounds();
    const ui::Fonts f{};
    subtitle_ = wrapText("Ton service des licences (un Worker Cloudflare \xC3\xA0 ton nom) : il active les licences en ligne par un code, tient les licences "
                         "flottantes, re\xC3\xA7oit les demandes des clients et garde l'archive, chiffr\xC3\xA9" "e, pour tes autres PC Admin.",
                         f.ui, std::max(200.f, r.w - kMarginLeft - kMarginRight));
    headerH_ = 16.f + ui::lineHeight(f.title) + 6.f + static_cast<float>(subtitle_.size()) * (ui::lineHeight(f.ui) + 2.f) + 10.f;
    scroll_->setBounds({r.x, r.y + headerH_, r.w, std::max(0.f, r.h - headerH_)});
}

void ServicePage::onPaint(const ui::PaintContext& ctx) {
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

PagePtr makeServicePage(ShellHost host) { return std::make_unique<ServicePage>(std::move(host)); }

} // namespace license_admin::gui
