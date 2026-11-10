// =============================================================================
//  app/hmi/SessionText.hpp - 1.47.0 : LE TEXTE DE LA SESSION, DANS SON CADRE
// -----------------------------------------------------------------------------
//  Le client (10/11), sur une capture de la fenetre de la session (vue Journal,
//  une fenetre de 943 x 609 : << Aucun changement encore : chaque changement de
//  la session s'inscrit ici... >> sortait a droite) : << bug a corriger, le
//  texte sort du cadre, attention au redimensionnement >>.
//
//  CE QUI SERT A TOUT CE QUI MONTRE LA SESSION (l'onglet << Session >> du
//  panneau du bas, la fenetre de la session) :
//   - wrap() : un texte en lignes qui tiennent dans une largeur - coupe aux
//     espaces ; un mot plus long que la ligne (un nom d'objet, un chemin) est
//     coupe entre deux caracteres, jamais au milieu d'un caractere UTF-8 ;
//   - elide() : une ligne qui ne tient pas finit par << ... >> (le texte entier
//     va dans l'infobulle) ;
//   - fitButtons() : une rangee de boutons dans une largeur - leur libelle
//     entier, sinon le court, sinon raccourci avec << ... >> (l'infobulle dit le
//     libelle entier) ; ce qui ne tient toujours pas passe a la ligne suivante ;
//   - LineList : la discussion et le journal - chaque ligne retournee a la ligne
//     a la largeur du moment, une barre de defilement (ui::ScrollArea : la
//     molette, le pouce qu'on tire), la derniere ligne en bas tant qu'on n'est
//     pas remonte lire plus haut ; l'infobulle d'une ligne : qui (le nom entier),
//     quand, la discussion ou le journal.
//  Les mesures : ui::measureWidth (celles du dessin).
// =============================================================================
#pragma once

#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::sessionui {

inline constexpr gfx::FontId kText{13};       // les lignes, les textes
inline constexpr gfx::FontId kTextBold{13, 1};
inline constexpr gfx::FontId kButtonFont{16};  // celle de ui::Button
inline constexpr float kButtonPad = 12.f;      // la marge du libelle, de chaque cote

// Les lignes de `text` dans `width` (les retours a la ligne gardes ; un mot trop long coupe). Vide : texte vide.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, gfx::FontId font, float width);
// Le texte s'il tient, sinon son debut et << ... >> ; vide si meme << ... >> ne tient pas.
[[nodiscard]] std::string elide(std::string_view text, gfx::FontId font, float width);
[[nodiscard]] bool fits(std::string_view text, gfx::FontId font, float width);

// La largeur d'un bouton qui montre tout `text`.
[[nodiscard]] float buttonWidth(std::string_view text, float minimum = 0.f);

// Un bouton d'une rangee : son libelle entier, et un plus court (facultatif : << Exporter (CSV) >>).
struct ButtonSpec {
    ui::Button* button{nullptr};
    std::string full, brief;
    float       minWidth{44.f};   // en dessous : le libelle raccourci avec << ... >>
    std::string tip;              // l'infobulle d'origine (le libelle entier s'y ajoute quand il est raccourci)
};
// Pose les boutons de gauche a droite (`rightAligned` : contre le bord droit, le dernier tout a droite) dans
// `width`, sur des lignes de `height` separees de `gap` ; rend la hauteur prise (0 : aucun bouton). Un bouton
// dont meme la largeur minimale ne tient pas sur une ligne vide est pose a la largeur de la ligne.
float fitButtons(std::vector<ButtonSpec>& specs, float x, float y, float width, float height, float gap = 6.f, bool rightAligned = false);
// Un seul bouton a `width` : le libelle entier, sinon le court, sinon raccourci ; l'infobulle dit le libelle entier.
void fitButton(ui::Button& b, const std::string& full, float width, const std::string& brief = {}, const std::string& tip = {});

// ---- une ligne de la discussion ou du journal ----
struct Row {
    std::string   time, who, text;      // "14:32", "Lea", "bonjour"
    std::uint32_t color{0};             // 0xRRGGBB (0 : aucune)
    bool          journal{false};       // une ligne du journal (sinon la discussion)
    bool          system{false};        // une ligne de la session (entree, depart...)
    bool          mine{false};          // ecrite par ce PC
    int           count{1};             // le journal : les gestes fondus (x3)
};
// "14:32 Lea : bonjour (x3)" - ce que les essais lisent.
[[nodiscard]] std::string rowText(const Row&);

class LineList final : public ui::Widget {
public:
    explicit LineList(std::string id);
    // Les lignes ; la liste reste en bas si elle y etait (ou si c'est la premiere fois).
    void setRows(std::vector<Row> rows);
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    // Ce que la liste dit quand elle est vide (retourne a la ligne aussi).
    void setEmptyText(std::string text);
    [[nodiscard]] const std::string& emptyText() const noexcept { return empty_; }
    void scrollToBottom();
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    [[nodiscard]] float scrollOffset() const noexcept { return offset_; }
    [[nodiscard]] bool  atBottom() const noexcept;
    // Les essais : les lignes de texte posees pour la ligne `i` (apres la mise en page).
    [[nodiscard]] std::vector<std::string> wrappedText(std::size_t i) const;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Placed {
        float                    y{0}, h{0};      // dans le contenu (avant le decalage)
        std::string              who;             // le nom tel que montre (raccourci : << ... >>)
        bool                     whoCut{false};
        std::vector<std::string> lines;           // le texte, retourne a la ligne
        float                    textX{0}, textY{0};   // ou commence le texte (relatif a la ligne)
    };
    void place();
    [[nodiscard]] gfx::Rect area() const noexcept;   // la zone des lignes (sans la barre)

    std::vector<Row>    rows_;
    std::vector<Placed> placed_;
    std::string         empty_;
    std::vector<std::string> emptyLines_;
    ui::ScrollArea      scroll_;
    float               offset_{0.f}, content_{0.f}, placedWidth_{-1.f};
    bool                stick_{true};
    bool                columns_{false};          // l'heure, qui et le texte en colonnes (assez large)
    float               whoX_{0}, whoW_{0}, textX_{0}, textW_{0};
};

} // namespace app::sessionui
