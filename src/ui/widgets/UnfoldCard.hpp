// =============================================================================
//  ui/widgets/UnfoldCard.hpp - 1.47.0 (planches 01, 07) : CE QU'UNE LIGNE DEPLIEE
//  MONTRE
// -----------------------------------------------------------------------------
//  Le widget qu'une page rend a TableView::setUnfold pour une ligne (ou pose
//  elle-meme sous une ligne de sa liste peinte : le journal). Des donnees, pas
//  de dessin a ecrire :
//
//    +---------------------------------------------------------------------+
//    |  L'OBJET TOUCHE        | AVANT . APRES          | LA SIGNATURE       |  des boites (Box)
//    |  [i] Licence **XPG..** | ce qui change  avant   | [o] cle de Lea  OK |  - des lignes (icone,
//    |  [i] Client Atelier  ^ | Options  aucune  Exp.. | [o] ligne n. 2409  |    **gras**, OK, lien ^)
//    |                        |                        |                    |  - un tableau (avant / apres)
//    |------------------------+------------------------+--------------------|  - une frise (son histoire)
//    |  [Ouvrir la licence]  (L Lea Essai >)  Tout son journal    Criticite >  la rangee d'actions
//    +---------------------------------------------------------------------+
//
//  - Box : un titre en petites capitales (vide : sans cadre, les lignes a plat
//    - Clients, Activations), des lignes, ou un tableau (head + table), ou une
//    frise (timeline : un point devant chaque ligne, l'heure en gris) ; sa part
//    de la largeur (weight).
//  - Line : une icone, l'heure (frise), un texte ou **ceci** est en gras, une
//    coche verte (check), un lien (link : la ligne se clique, une fleche a
//    droite), une PASTILLE D'ADMIN (person : son initiale sur sa couleur, son
//    nom, >), un texte apres elle.
//  - la rangee d'actions : des boutons (de vrais ui::Button : leurs id se
//    cliquent dans les scripts), des pastilles d'admin, des mots ; a gauche ou
//    a droite.
//  - LA PLACE D'UNE PETITE COURBE (setCurve) : une boite de plus, dessinee par
//    l'hote (les graphiques de src/ui, un autre lot) ; sans dessin, rien.
//
//  La hauteur suit le contenu (heightFor, sizeHint) ; la table pose la largeur.
//  Le fond (le violet tres pale, la barre a gauche) : la table le peint ;
//  setOwnBackground(true) pour un hote qui ne le fait pas.
// =============================================================================
#pragma once

#include "Controls.hpp"
#include "../Theme.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

class UnfoldCard final : public Widget {
public:
    struct Person {
        std::string           name;            // << Lea Essai >>
        gfx::Color            color{};         // la couleur de l'admin (extra.css : Camille, Lea, Hugo, Nina)
        std::function<void()> open{};          // sa fiche ; vide : pas de >
        std::string           count{};         // << 7 >> : << Lea Essai \xC2\xB7 7 >>
        bool                  muted{false};    // retire : pali
    };
    struct Line {
        Icon                  icon{Icon::None};
        std::string           time{};          // la frise : l'heure, en gris, devant
        std::string           text{};          // **gras** permis
        Tone                  tone{Tone::None};
        bool                  check{false};    // une coche verte apres
        bool                  mono{false};
        std::function<void()> link{};          // la ligne se clique (une fleche a droite)
        std::optional<Person> person{};        // une pastille apres le texte
        std::string           after{};         // un texte apres la pastille
        std::vector<Person>   people{};        // plusieurs pastilles a la suite (<< servi par >>)
    };
    struct Box {
        std::string                           title{};
        std::vector<Line>                     lines{};
        bool                                  timeline{false};
        std::vector<std::string>              head{};    // un tableau : ses titres
        std::vector<std::vector<std::string>> table{};   // ... ses lignes
        float                                 weight{1.f};
        std::function<void(const PaintContext&, gfx::Rect)> paint{};   // une boite dessinee par l'hote
        float                                 height{0.f};                // ... sa hauteur (sans le titre)
    };

    explicit UnfoldCard(std::string id);

    void addBox(Box box);
    // La place d'une petite courbe (un autre lot ecrit les graphiques) : une boite de plus, `paint` la dessine dans son
    // cadre (sous le titre) ; `paint` vide : pas de boite.
    void setCurve(std::string title, std::function<void(const PaintContext&, gfx::Rect)> paint, float height = 86.f, float weight = 1.f);
    // La rangee d'actions. `id` : celui du bouton (les scripts). `why` non vide : grise, la raison en infobulle.
    Button& addButton(std::string label, std::string id, Icon icon, std::function<void()> run, std::string why = {}, bool right = false);
    Button& addPerson(Person person, std::string id, bool right = false);
    void    addText(std::string text, bool right = false);
    void    setOwnBackground(bool on) { ownBackground_ = on; invalidate(); }

    [[nodiscard]] float heightFor(float width) const;
    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] const std::vector<Box>& boxes() const noexcept { return boxes_; }
    // Les essais : cliquer le lien de la ligne `line` de la boite `box` ; tout le texte des boites (une ligne chacun).
    bool clickLink(std::size_t box, std::size_t line);
    [[nodiscard]] std::string text() const;
    // 1.47.0 : rien ne deborde - un morceau trop long (un texte, une pastille, un titre) est raccourci (<< ... >>) ; sous la
    // souris, l'infobulle le dit en entier.
    [[nodiscard]] bool        hasTooltip() const override { return !tips_.empty() || Widget::hasTooltip(); }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    struct RowItem {
        int         kind{0};            // 0 un bouton, 1 une pastille (un bouton aussi), 2 des mots
        Widget*     widget{nullptr};
        std::string text;
        bool        right{false};
        gfx::Rect   rect{};
    };
    struct Hit {
        gfx::Rect             rect;
        std::function<void()> run;
    };
    struct Tip {
        gfx::Rect   rect;
        std::string text;
    };
    [[nodiscard]] float boxHeight(const Box& b) const;
    [[nodiscard]] float actionsHeight() const;
    [[nodiscard]] std::vector<gfx::Rect> boxRects(float width, float top) const;
    float paintLine(const PaintContext& ctx, const Line& l, float x, float y, float w, bool timeline) const;

    std::vector<Box>     boxes_;
    std::vector<RowItem> items_;
    bool                 ownBackground_{false};
    mutable std::vector<Hit> hits_;
    mutable std::vector<Tip> tips_;   // 1.47.0 : ce qui est raccourci, et son texte entier
    // Dessiner un texte sur une ligne, raccourci a `w` (son texte entier en infobulle s'il l'est) ; rend sa largeur.
    float drawFitted(const PaintContext& ctx, gfx::Point at, const std::string& text, gfx::FontId f, gfx::Color ink, float w) const;
};

// Une pastille d'admin seule (la rangee d'actions, une ligne) : son initiale sur sa couleur, son nom, << > >>.
class PersonChip final : public Button {
public:
    PersonChip(UnfoldCard::Person person, std::string id);
    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] const UnfoldCard::Person& person() const noexcept { return person_; }
    // 1.47.0 : plus etroite que son nom (sa place), le nom raccourci ; l'infobulle le dit en entier.
    [[nodiscard]] bool        hasTooltip() const override { return true; }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

protected:
    void onPaint(const PaintContext&) override;

private:
    UnfoldCard::Person person_;
    mutable bool       cut_{false};
};

// Dessiner une pastille d'admin a (x, y) ; rend sa largeur. Pour les listes peintes (le journal) et les cartes. 1.47.0 :
// `maxW` - jamais plus large : le nom raccourci (<< ... >>) ; `cut` (facultatif) : vrai s'il l'a ete.
float paintPerson(const PaintContext& ctx, float x, float y, float h, const UnfoldCard::Person& p, bool hovered = false, float maxW = 1e9f,
                  bool* cut = nullptr);
[[nodiscard]] float personWidth(const UnfoldCard::Person& p, gfx::FontId font, float maxW = 1e9f);

} // namespace ui
