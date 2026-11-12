// =============================================================================
//  app/SessionWindow.hpp - 1.47.0 : LA FENETRE DE LA SESSION, DANS SON CADRE
// -----------------------------------------------------------------------------
//  La fenetre de la session (la pastille du haut, Projet > Session ;
//  app/SessionDialogs) : l'equipe (ou chacun est, Suivre, son role, Retirer de
//  la session...), ceux qui veulent entrer, le code, la discussion ou le
//  journal, Envoyer, Arreter / Quitter, Fermer.
//
//  Le client (10/11), sur une capture de cette fenetre (vue Journal, une
//  fenetre de 943 x 609) : << bug a corriger, le texte sort du cadre, attention
//  au redimensionnement >>. Avant : un cadre de 560 x 420 au moins (plus grand
//  que la fenetre si elle etait petite), des textes poses a une position fixe
//  (le texte vide du journal, le rappel du bas : sur une ligne, sortis a
//  droite), des noms coupes au milieu d'un mot, Suivre / Regarder seulement /
//  Retirer de la session... poses sur le nom dans une colonne etroite, et la
//  discussion qui ne defilait pas (les dernieres lignes seulement).
//
//  MAINTENANT, A TOUTE TAILLE (de la plus petite fenetre a la plus grande, le
//  cadre agrandi a la main aussi) :
//   - le cadre tient dans la fenetre ; deux colonnes (l'equipe, la discussion)
//     quand il y a la place, sinon l'une sous l'autre ;
//   - chaque texte tient dans sa largeur : retourne a la ligne (la discussion,
//     le journal, le texte vide, le rappel du bas, ce qui se passe quand on
//     entre), ou raccourci avec << ... >> sur une ligne (un nom, un PC, le
//     titre) - le texte entier dans l'infobulle ;
//   - les boutons d'un equipier : a droite de son nom s'il reste la place de le
//     lire, sinon dessous ; ils passent a la ligne plutot que de se chevaucher,
//     et un libelle qui ne tient pas se raccourcit (<< Retirer... >>), entier
//     dans l'infobulle ;
//   - l'equipe et la discussion defilent (la molette, la barre qu'on tire).
//
//  1.50.0 : REPONDRE, REDUIRE, COPIER dans la discussion (la liste de
//  SessionText) ; << En reponse a Lea : << ... >> >> au-dessus du champ
//  (replyBar), dans le cadre comme le reste.
//
//  CE QU'ELLE MONTRE est un Snapshot (des donnees simples), rempli par
//  SessionDialogs depuis le service des sessions : les essais posent la fenetre
//  a toutes les tailles sans session (tests/session_ui_test.cpp).
// =============================================================================
#pragma once

#include "../ui/DialogFrame.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/ScrollBar.hpp"
#include "hmi/SessionText.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace app::sessionwin {

struct Person {
    int           number{0};
    std::string   name;      // "Lea Essai"
    std::string   tag;       // "(toi) . hote" : apres le nom, jamais raccourci avant lui
    std::string   initials;  // "LE"
    std::string   where;     // "PC-ATELIER-02 . M-7F21 . modifie . Vue_Accueil . 2 objets"
    std::uint32_t color{0};
};
struct Knock {
    std::string name, detail;
};
struct Snapshot {
    bool        active{false};
    std::string title;                 // la barre de titre : << Session << X >> - tu es l'hote >>
    std::string progress;              // hors session : ce qui se passe (on entre, la raison d'un refus)
    std::string teamTitle;             // << L'equipe . 3 >>
    bool        code{false};
    std::string codeLabel, codeText;   // << Le code de la session >>, << 482 913 >>
    std::vector<Person> people;
    std::vector<Knock>  knocks;
    std::vector<sessionui::Row> chat, journal;
    bool        showJournal{false};
    std::string hint;                  // le rappel du bas
};

class Body final : public ui::Widget {
public:
    explicit Body(std::string id);
    void setSnapshot(Snapshot s);
    [[nodiscard]] const Snapshot& snapshot() const noexcept { return snap_; }

    // Les boutons (enfants de ce widget, crees et relies par la fenetre) ; nuls : absents.
    struct MemberButtons {
        ui::Button* follow{nullptr};
        ui::Button* role{nullptr};
        ui::Button* remove{nullptr};
    };
    struct KnockButtons {
        ui::Button* yes{nullptr};
        ui::Button* no{nullptr};
    };
    std::vector<MemberButtons> rows;       // une par membre, dans l'ordre de snapshot().people
    std::vector<KnockButtons>  knockRows;  // une par entree, dans l'ordre de snapshot().knocks
    ui::Button*    copy{nullptr};
    ui::Button*    chatView{nullptr};
    ui::Button*    journalView{nullptr};
    ui::Button*    exportJournal{nullptr};
    ui::InputText* field{nullptr};
    ui::Button*    send{nullptr};
    ui::Button*    stop{nullptr};
    ui::Button*    close{nullptr};

    // La discussion ou le journal (enfant, cree ici).
    [[nodiscard]] sessionui::LineList* lines() noexcept { return lines_; }
    // 1.50.0 : << En reponse a ... >> au-dessus du champ (enfant, cree ici ; cache sans reponse en cours).
    [[nodiscard]] sessionui::ReplyBar* replyBar() noexcept { return reply_; }
    // Les essais : le cadre, s'il est en deux colonnes ; les textes raccourcis avec << ... >> (le texte entier, celui
    // de l'infobulle).
    [[nodiscard]] gfx::Rect panel() const noexcept { return panel_; }
    [[nodiscard]] bool twoColumns() const noexcept { return two_; }
    [[nodiscard]] std::vector<std::string> cutTexts() const;
    [[nodiscard]] float teamOffset() const noexcept { return teamOffset_; }
    [[nodiscard]] float teamContent() const noexcept { return teamContent_; }
    [[nodiscard]] gfx::Rect teamView() const noexcept { return teamView_; }

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    // Une ligne de texte posee (deja raccourcie ou retournee).
    struct Label {
        gfx::Point    at;
        std::string   text;
        gfx::FontId   font{};
        int           tone{0};       // 0 texte, 1 discret, 2 attention, 4 blanc (les initiales)
        bool          team{false};   // dans la zone de l'equipe qui defile
        std::string   full;          // le texte entier quand `text` est raccourci (l'infobulle)
    };
    struct Dot {
        gfx::Rect     rect;
        std::uint32_t color{0};
    };
    // Une ligne raccourcie a `width` ; rend la largeur dessinee.
    float addLine(gfx::Point at, const std::string& full, gfx::FontId font, float width, int tone, bool team = false);
    // Un texte retourne a la ligne a `width` (au plus `maxLines` lignes, la derniere raccourcie) ; rend la hauteur.
    float addWrapped(float x, float y, const std::string& text, gfx::FontId font, float width, int tone, std::size_t maxLines = 0);
    [[nodiscard]] const std::string& fullOf(ui::Button* b);
    float teamPass(const gfx::Rect& view, float rowW);   // pose l'equipe ; rend la hauteur du contenu
    void  applyTeamOffset();
    void  layoutChat(const gfx::Rect& area);

    Snapshot snap_;
    sessionui::LineList* lines_{nullptr};
    sessionui::ReplyBar* reply_{nullptr};   // 1.50.0
    gfx::Rect panel_{}, teamArea_{}, teamView_{}, chatBox_{}, codeBox_{};
    bool two_{true};
    std::vector<Label> labels_;   // les textes poses (fixes, et ceux de l'equipe qui defilent)
    std::vector<Dot> dots_;
    std::vector<std::pair<ui::Button*, std::string>> fullText_;   // le libelle entier de chaque bouton
    struct TeamButton { ui::Button* b; gfx::Rect at; };
    std::vector<TeamButton> teamButtons_;   // leur place dans le contenu de l'equipe (avant le decalage)
    ui::ScrollArea teamScroll_;
    float teamOffset_{0.f}, teamContent_{0.f};
};

// ---------------------------------------------------------------------- les fenetres Demarrer et Rejoindre ---
//  1.44.0 : des lignes posees de haut en bas (un titre, une phrase, un widget, des widgets cote a cote, le code dans sa
//  boite), puis les boutons en bas a droite. La fenetre les recompose quand un choix change (Ou, Sur le reseau du
//  bureau / Par Internet...). 1.47.0 : ici (avant : app/SessionDialogs), pour que les essais la posent a toutes les
//  tailles ; le texte dans son cadre comme la fenetre de la session.
class FlowBody final : public ui::Widget {
public:
    struct Item {
        enum class Kind { Title, Text, Muted, Warning, Widget, Row, Gap, Code };
        Kind kind{Kind::Text};
        std::string text;
        std::vector<ui::Widget*> widgets;
        float height{0};   // Widget, Row : la hauteur ; Gap : l'espace
        float width{0};    // Widget : 0, toute la largeur
        float indent{0};
    };
    std::string title;
    std::vector<Item> items;
    std::vector<ui::Button*> footer;   // en bas a droite (le dernier : le plus a droite)
    std::string error;                 // en rouge, au-dessus des boutons
    float wanted{620.f};

    void add(Item::Kind k, std::string text = {}, float indent = 0) {
        Item i;
        i.kind = k;
        i.text = std::move(text);
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void widget(ui::Widget* w, float h = 30.f, float width = 0, float indent = 0) {
        Item i;
        i.kind = Item::Kind::Widget;
        i.widgets = {w};
        i.height = h;
        i.width = width;
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void row(std::vector<ui::Widget*> ws, float h = 30.f, float indent = 0) {
        Item i;
        i.kind = Item::Kind::Row;
        i.widgets = std::move(ws);
        i.height = h;
        i.indent = indent;
        items.push_back(std::move(i));
    }
    void gap(float h = 10.f) {
        Item i;
        i.kind = Item::Kind::Gap;
        i.height = h;
        items.push_back(std::move(i));
    }
    void code(std::string text, ui::Button* copy) {
        Item i;
        i.kind = Item::Kind::Code;
        i.text = std::move(text);
        if (copy) i.widgets = {copy};
        i.height = 44.f;
        items.push_back(std::move(i));
    }

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        if (titleCut_ && mouse.y >= panel_.y && mouse.y < panel_.y + ui::dialogframe::kTitleBand && panel_.contains(mouse)) return title;
        return {};
    }
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    // 1.47.0 : LE TEXTE DANS SON CADRE (comme la fenetre de la session) - le cadre tient dans la fenetre (plus de
    // 420 px au moins) ; les textes et les titres retournes a la ligne (un mot trop long coupe) ; une ligne de
    // widgets trop large pour le cadre se pose en colonne ; le code raccourci avant son bouton ; les boutons du bas
    // se raccourcissent ou passent a la ligne ; et ce qui ne tient pas en hauteur DEFILE (la molette, la barre)
    // au lieu de sortir du cadre.
    static constexpr gfx::FontId kSmall{13};
    static constexpr gfx::FontId kButton{16};
    static constexpr float kSmallLine = 18.f;
    static constexpr float kTitleLine = 22.f;
    static constexpr gfx::FontId kTitleFont{16, 1};

    static float widthOf(ui::Widget* w) {
        if (auto* b = dynamic_cast<ui::Button*>(w)) return std::max(88.f, ui::measureWidth(b->text(), kButton) + 32.f);
        if (auto* rb = dynamic_cast<ui::RadioButton*>(w)) return ui::measureWidth(rb->label(), kButton) + 34.f;
        return 0.f;
    }

    // Une ligne de widgets trop large pour `w` : en colonne.
    bool stacked(const Item& i, float w) const {
        if (i.kind != Item::Kind::Row) return false;
        float fixed = 0;
        int stretch = 0;
        for (auto* wd : i.widgets) {
            const float f = widthOf(wd);
            if (f > 0) fixed += f;
            else ++stretch;
        }
        return fixed + 60.f * static_cast<float>(stretch) + 10.f * static_cast<float>(i.widgets.size() - 1) > w - i.indent;
    }

    float itemHeight(const Item& i, float w) const {
        switch (i.kind) {
            case Item::Kind::Title: return static_cast<float>(std::max<std::size_t>(1, sessionui::wrap(i.text, kTitleFont, w - i.indent).size())) * kTitleLine + 4.f;
            case Item::Kind::Text:
            case Item::Kind::Muted:
            case Item::Kind::Warning: return static_cast<float>(sessionui::wrap(i.text, kSmall, w - i.indent).size()) * kSmallLine + 4.f;
            case Item::Kind::Row:
                return stacked(i, w) ? (i.height + 6.f) * static_cast<float>(i.widgets.size()) - 6.f : i.height;
            default: return i.height;
        }
    }

    float contentHeight(float w) const {
        float h = 0;
        for (const auto& i : items) h += itemHeight(i, w);
        return h;
    }

    // Pose les widgets d'un element a `y` (dans le cadre, le decalage deja retire) ; cache ceux qui sortent de la vue.
    void placeItem(Item& i, float x, float y, float w) {
        const auto show = [this](ui::Widget* wd, gfx::Rect at) {
            wd->setBounds(at);
            const bool inside = at.y >= view_.y - 0.5f && at.bottom() <= view_.bottom() + 0.5f;
            wd->setVisibility(inside ? ui::Visibility::Visible : ui::Visibility::Hidden);
            // Un choix dont le libelle ne tient pas : le libelle entier dans l'infobulle (le bouton le raccourcit).
            if (auto* rb = dynamic_cast<ui::RadioButton*>(wd)) rb->setTooltip(ui::measureWidth(rb->label(), kButton) + 24.f > at.w ? rb->label() : std::string{});
        };
        if (i.kind == Item::Kind::Widget && !i.widgets.empty()) {
            const float ww = i.width > 0 ? std::min(i.width, w - i.indent) : w - i.indent;
            show(i.widgets[0], {x + i.indent, y, std::max(0.f, ww), i.height});
        } else if (i.kind == Item::Kind::Row) {
            if (stacked(i, w)) {
                float ry = y;
                for (auto* wd : i.widgets) {
                    const float f = widthOf(wd);
                    show(wd, {x + i.indent, ry, f > 0 ? std::min(f, w - i.indent) : w - i.indent, i.height});
                    if (auto* b = dynamic_cast<ui::Button*>(wd)) sessionui::fitButton(*b, fullOf(b), std::min(f, w - i.indent));
                    ry += i.height + 6.f;
                }
                return;
            }
            float bx = x + i.indent;
            // Les champs (sans largeur propre) se partagent ce qui reste.
            float fixed = 0;
            int stretch = 0;
            for (auto* wd : i.widgets) {
                const float f = widthOf(wd);
                if (f > 0) fixed += f;
                else ++stretch;
            }
            const float gapW = 10.f;
            const float rest = std::max(60.f, w - i.indent - fixed - gapW * static_cast<float>(i.widgets.size() - 1));
            for (auto* wd : i.widgets) {
                float ww = widthOf(wd);
                if (ww <= 0) ww = stretch ? rest / static_cast<float>(stretch) : rest;
                show(wd, {bx, y, ww, i.height});
                bx += ww + gapW;
            }
        } else if (i.kind == Item::Kind::Code && !i.widgets.empty()) {
            auto* b = dynamic_cast<ui::Button*>(i.widgets[0]);
            const float bw = std::min(widthOf(i.widgets[0]), std::max(44.f, w * 0.4f));
            show(i.widgets[0], {x + w - bw - 8.f, y + 7.f, bw, 30.f});
            if (b) sessionui::fitButton(*b, fullOf(b), bw);
        }
    }

    const std::string& fullOf(ui::Button* b) {
        for (const auto& [k, t] : fullText_)
            if (k == b) return t;
        fullText_.emplace_back(b, b->text());
        return fullText_.back().second;
    }

    void onLayout() override {
        const auto r = bounds();
        const float mx = r.w >= 500.f ? 20.f : 8.f, my = r.h >= 420.f ? 20.f : 8.f;
        const float w = std::max(0.f, std::min(wanted, r.w - 2.f * mx));
        pad_ = w >= 420.f ? 20.f : 12.f;
        float cw = std::max(0.f, w - 2.f * pad_);
        // Le pied : les boutons (raccourcis ou sur deux lignes s'il le faut), l'erreur au-dessus.
        foot_.clear();
        for (auto* b : footer) foot_.push_back({b, fullOf(b), {}, 60.f, {}});
        const float footH = foot_.empty() ? 0.f : sessionui::fitButtons(foot_, 0.f, 0.f, cw, 30.f, 10.f, true);
        const float errorH = error.empty() ? 0.f : static_cast<float>(sessionui::wrap(error, kSmall, cw).size()) * kSmallLine + 8.f;
        const float band = ui::dialogframe::kTitleBand;
        const float chrome = band + 14.f + errorH + footH + 32.f;
        const float h = std::max(0.f, std::min(r.h - 2.f * my, chrome + contentHeight(cw)));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        cw = std::max(0.f, panel_.w - 2.f * pad_);
        const float x = panel_.x + pad_;
        view_ = {x, panel_.y + band + 14.f, cw, std::max(0.f, panel_.bottom() - 16.f - footH - errorH - 6.f - (panel_.y + band + 14.f))};
        // Ce qui ne tient pas en hauteur defile : la barre a droite, les elements a sa gauche.
        content_ = contentHeight(cw);
        itemsW_ = cw;
        if (content_ > view_.h + 0.5f) {
            itemsW_ = std::max(0.f, cw - ui::kScrollBarThickness - 6.f);
            content_ = contentHeight(itemsW_);
        }
        scroll_.place(view_, content_, view_.h, {panel_.right() - pad_ * 0.5f - ui::kScrollBarThickness * 0.5f, view_.y, ui::kScrollBarThickness, view_.h});
        offset_ = std::clamp(offset_, 0.f, std::max(0.f, content_ - view_.h));
        at_.clear();
        float y = view_.y;
        for (auto& i : items) {
            at_.push_back(y);
            placeItem(i, x, y - offset_, itemsW_);
            y += itemHeight(i, itemsW_);
        }
        errorAt_ = panel_.bottom() - 16.f - footH - errorH;
        (void)sessionui::fitButtons(foot_, x, panel_.bottom() - 16.f - footH, cw, 30.f, 10.f, true);
        titleShown_ = sessionui::elide(title, kTitleFont, std::max(0.f, panel_.w - 24.f));
        titleCut_ = titleShown_ != title;
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        bool changed = false;
        if (view_.h > 0.f && scroll_.handle(*this, ev, offset_, &changed)) {
            if (changed) invalidateLayout();
            return ui::EventResult::Consumed;
        }
        return ui::Widget::onEvent(ev);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        const float band = ui::dialogframe::kTitleBand;
        r.fillRect({panel_.x, panel_.y, panel_.w, band}, c.headerBg);
        r.drawText({panel_.x + 12.f, panel_.y + (band - r.lineHeight(kTitleFont)) * 0.5f}, titleShown_, kTitleFont, c.text);
        const float x = panel_.x + pad_, cw = itemsW_;
        r.pushClip(view_);
        for (std::size_t n = 0; n < items.size() && n < at_.size(); ++n) {
            const auto& i = items[n];
            const float y = at_[n] - offset_;
            if (y > view_.bottom() || y + itemHeight(i, cw) < view_.y) continue;
            switch (i.kind) {
                case Item::Kind::Title: {
                    float ly = y + 4.f;
                    for (const auto& l : sessionui::wrap(i.text, kTitleFont, cw - i.indent)) {
                        r.drawText({x + i.indent, ly}, l, kTitleFont, c.text);
                        ly += kTitleLine;
                    }
                    break;
                }
                case Item::Kind::Text:
                case Item::Kind::Muted:
                case Item::Kind::Warning: {
                    const auto col = i.kind == Item::Kind::Text ? c.text : i.kind == Item::Kind::Warning ? c.warning : c.textMuted;
                    float ly = y + 1.f;
                    for (const auto& l : sessionui::wrap(i.text, kSmall, cw - i.indent)) {
                        r.drawText({x + i.indent, ly}, l, kSmall, col);
                        ly += kSmallLine;
                    }
                    break;
                }
                case Item::Kind::Code: {
                    const gfx::Rect box{x, y, cw, 44.f};
                    r.fillRoundedRect(box, c.inputBg, 6.f);
                    const float bw = i.widgets.empty() ? 0.f : i.widgets[0]->bounds().w + 16.f;
                    const std::string shown = sessionui::elide(i.text, ctx.theme.font.mono, std::max(0.f, cw - 24.f - bw));
                    r.drawText({box.x + 12.f, box.y + (44.f - r.lineHeight(ctx.theme.font.mono)) * 0.5f}, shown, ctx.theme.font.mono, c.text);
                    break;
                }
                default: break;
            }
        }
        r.popClip();
        scroll_.paint(ctx, offset_);
        if (!error.empty()) {
            float ly = errorAt_;
            for (const auto& l : sessionui::wrap(error, kSmall, panel_.w - 2.f * pad_)) {
                r.drawText({x, ly}, l, kSmall, c.warning);
                ly += kSmallLine;
            }
        }
    }

private:
    gfx::Rect panel_{}, view_{};
    std::vector<float> at_;
    float errorAt_{0}, pad_{20.f}, offset_{0.f}, content_{0.f}, itemsW_{0.f};
    std::vector<sessionui::ButtonSpec> foot_;
    std::vector<std::pair<ui::Button*, std::string>> fullText_;
    ui::ScrollArea scroll_;
    std::string titleShown_;
    bool titleCut_{false};
};

} // namespace app::sessionwin
