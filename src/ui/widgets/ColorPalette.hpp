// =============================================================================
//  ui/widgets/ColorPalette.hpp - choisir une couleur sans taper son code
// -----------------------------------------------------------------------------
//  LA PALETTE D'UNE CASE COULEUR. Sous la case, une fenetre (1.10, dessin de
//  la maquette, scene Couleurs : le choix personnalise ouvert d'emblee, grand
//  et complet) :
//
//    +------------------------------------------------------------------+
//    | Remplissage  Btn_Pompe_3                             [/ Pipette] |
//    +----------------------------+-------------------------------------+
//    | Nuancier  gris, puis dix   | +---------------------------------+ |
//    |           teintes en       | |  saturation (x) / valeur (y)    | |
//    |           trois nuances    | |                                 | |
//    | Dans le projet  (10 au +)  | +---------------------------------+ |
//    | Recentes        (10 au +)  | [======== teinte 0 a 360 ========] |
//    | [ Transparent ]            | [::::: transparence -> opaque ::::] |
//    | Avant / apres              | Code         T      S      V        |
//    | [ avant  |  apres ]        | [#RRGGBBAA] [___]  [___]  [___]     |
//    |                            | R     G     B     Opacite %          |
//    |                            | [___] [___] [___] [_______]         |
//    +----------------------------+-------------------------------------+
//    | (ce que veut dire le champ actif)          [ Annuler ] [Appliquer] |
//    +------------------------------------------------------------------+
//
//  Un clic sur une pastille l'applique et ferme. Le carre, la barre de teinte,
//  la barre de transparence (alpha) et les champs preparent une couleur :
//  Appliquer ou Entree l'applique, Annuler ou Echap ferme sans rien changer.
//  Le code s'ecrit aussi a la main (#RRGGBB, #RRGGBBAA, vide = transparent, ou
//  =expression : passe tel quel). Tab passe d'un champ a l'autre (dans l'ordre
//  du dessin) ; Haut/Bas changent un nombre (Maj : de 10). Un clic sur
//  << avant >> remet la couleur d'origine.
//
//  LA PIPETTE (le bouton, ou la touche I) : la palette reste ouverte ; une
//  croix et une loupe suivent la souris (les pixels dessous, agrandis, et leur
//  code) ; un clic n'importe ou dans la fenetre de l'application prend la
//  couleur du pixel (a appliquer ensuite) ; Echap (ou le clic droit) annule.
//  Le pixel est lu dans ce que l'application vient de dessiner
//  (IRenderer::readPixels, relu a chaque image pendant la passe du dessus,
//  apres la palette et avant la loupe) : la vue de l'IHM, une image,
//  l'interface - pas d'appel propre a un systeme.
//
//  La fenetre deborde de la case : elle est dessinee au-dessus de tout
//  (requestsOverlayPass) et recoit les clics de toute sa surface (eventBounds) ;
//  pendant la pipette, et pendant un glisse dans le carre ou une barre, toute
//  la fenetre de l'application.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../Widget.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

class ColorPalette : public Widget {
public:
    explicit ColorPalette(std::string id = {});
    ~ColorPalette() override;

    void setValue(std::string value);                        // "#RRGGBB", "#RRGGBBAA", "" (transparent), "=expr"
    // 1.10 : le titre de la fenetre (la propriete : "Remplissage") et, en
    // gris a cote, ce qu'elle colore (l'objet). Vide : "Couleur".
    void setTitle(std::string title, std::string subject = {});
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    // Les couleurs deja employees (le projet) : une rangee a part, 10 au plus.
    void setProjectColors(std::vector<std::string> colors);

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    // Une couleur choisie (la fenetre se ferme ensuite) ; fermee sans choix.
    const core::SignalPtr<const std::string&> applied = core::Signal<const std::string&>::create();
    const core::SignalPtr<>                   closed  = core::Signal<>::create();

    // Les dernieres couleurs choisies, partagees par toutes les palettes (10).
    [[nodiscard]] static std::vector<std::string>& recent();
    // Le nuancier : 10 gris puis 3 rangees de 10 teintes.
    [[nodiscard]] static const std::vector<std::string>& swatches();

    // 1.10 : les champs du choix personnalise. Hex : le code (#RRGGBB ou
    // #RRGGBBAA, ou une expression) ; R G B de 0 a 255 ; A : l'opacite en %
    // (0 transparent, 100 opaque) ; T en degres (0 a 359), S et V en % (0 a 100).
    enum class Field : std::uint8_t { Hex, R, G, B, A, H, S, V };

    // Pour les tests et les scripts de capture.
    [[nodiscard]] gfx::Rect popupRect() const;
    [[nodiscard]] bool      swatchRect(std::string_view color, gfx::Rect& out) const;   // la premiere pastille de ce code
    [[nodiscard]] gfx::Rect squareRect() const;      // saturation (x) / valeur (y)
    [[nodiscard]] gfx::Rect hueRect() const;         // teinte : 0 a gauche, 360 a droite (sous le carre)
    [[nodiscard]] gfx::Rect alphaRect() const;       // 1.10 : transparence, opaque a droite
    // 1.10 : le point du carre, des barres, pour une couleur (tests, scripts).
    [[nodiscard]] gfx::Point squarePoint(double saturation, double value) const;   // 0 a 1
    [[nodiscard]] gfx::Point huePoint(double degrees) const;
    [[nodiscard]] gfx::Point alphaPoint(double opacity) const;                     // 0 a 1
    [[nodiscard]] gfx::Rect okRect() const;          // Appliquer
    [[nodiscard]] gfx::Rect cancelRect() const;      // 1.10
    [[nodiscard]] gfx::Rect transparentRect() const;
    [[nodiscard]] gfx::Rect fieldRect(Field f) const;   // 1.10 : la case d'un champ
    [[nodiscard]] gfx::Rect beforeRect() const;      // 1.10 : << avant >>, la couleur d'origine (un clic la remet)
    [[nodiscard]] gfx::Rect afterRect() const;       // 1.10 : << apres >>, la couleur preparee
    [[nodiscard]] gfx::Rect pipetteRect() const;     // 1.10 : le bouton Pipette, en haut a droite
    // 1.10 : le bas de la fenetre : ce que veut dire le champ actif, ou ce que
    // la pipette attend, ou la couleur qu'elle vient de prendre.
    [[nodiscard]] std::string hint() const;
    [[nodiscard]] const std::string& draft() const noexcept { return draft_; }   // le code en cours
    [[nodiscard]] Field     field() const noexcept { return field_; }          // le champ qui a le clavier
    [[nodiscard]] std::string fieldText(Field f) const;                        // ce que montre ce champ
    void focusField(Field f);                                                  // comme un clic dans sa case
    [[nodiscard]] gfx::Color color() const noexcept { return color_; }         // la couleur preparee

    // 1.10 : LA PIPETTE.
    void startPipette();
    void cancelPipette();
    [[nodiscard]] bool picking() const noexcept { return picking_; }
    // Le nombre de pipettes actives (l'hote montre alors un curseur en croix).
    [[nodiscard]] static int pickingCount() noexcept;
    // Une image connue a la place de celle du renderer (tests) : RGBA, w x h
    // pixels, `scale` pixels par point de la surface. Vide : relire le renderer.
    void setPipetteImage(std::vector<std::uint8_t> rgba, int w, int h, float scale = 1.f);
    // Le pixel sous ce point (coordonnees de la surface) dans la derniere image lue.
    [[nodiscard]] bool pixelAt(gfx::Point p, gfx::Color& out) const;
    [[nodiscard]] gfx::Rect loupeRect() const;       // la loupe, pres de la souris

    [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;

protected:
    void        onPaint(const PaintContext&) override;
    void        onPaintOverlay(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
    void        onFocusChanged(bool gained) override;

private:
    struct Cell { gfx::Rect r; std::string color; };
    [[nodiscard]] std::vector<Cell> cells() const;           // toutes les pastilles, a leur place
    [[nodiscard]] float     sectionY(int section) const;    // 0 nuancier, 1 projet, 2 recentes, 3 transparent
    [[nodiscard]] gfx::Point rightColumn() const;           // le coin du choix personnalise
    void paintBody(const PaintContext& ctx);
    void choose(const std::string& color);                   // appliquer, retenir, fermer
    void pickSquare(gfx::Point p);
    void pickHue(gfx::Point p);
    void pickAlpha(gfx::Point p);
    void setColor(gfx::Color c, bool keepHue);               // la couleur preparee ; le code suit
    void syncFromHsv();                                      // teinte/saturation/valeur -> couleur et code
    void typed(const std::string& text);                     // une frappe dans le champ actif
    void erase();                                            // Retour arriere dans le champ actif
    void fieldEdited();                                      // le texte du champ actif relu
    void stepField(int delta);                               // Haut/Bas
    [[nodiscard]] bool draftValid() const;
    [[nodiscard]] bool editingExpression() const;
    void paintPipette(const PaintContext& ctx);

    std::string              value_;
    std::string              origin_;         // la valeur a l'ouverture (l'apercu Avant)
    std::string              draft_;          // le code montre dans le champ
    std::vector<std::string> project_;
    std::string              title_, subject_;
    std::string              picked_;         // la derniere couleur prise a la pipette (le bas le dit)
    bool                     open_{false};
    double                   hue_{210.0}, sat_{0.6}, val_{0.8};
    gfx::Color               color_{82, 143, 204, 255};
    enum class Drag : std::uint8_t { None, Square, Hue, Alpha } drag_{Drag::None};
    std::string              hover_;          // la pastille sous la souris
    Field                    field_{Field::Hex};
    std::string              fieldEdit_;      // le texte d'un champ nombre en cours de frappe
    // Le code est "tout selectionne" a l'ouverture (et apres Ctrl+A) : la
    // premiere frappe le remplace, comme dans un champ qu'on vient d'ouvrir.
    bool                     replaceOnType_{true};
    // La pipette : l'image relue, la souris.
    bool                     picking_{false};
    bool                     shotFixed_{false};
    std::vector<std::uint8_t> shot_;
    int                      shotW_{0}, shotH_{0};
    float                    shotScale_{1.f};
    gfx::Point               mouse_{};
};

// "#RRGGBB" ou "#RRGGBBAA" -> couleur ; faux sinon.
[[nodiscard]] bool parseHexColor(std::string_view text, gfx::Color& out) noexcept;
[[nodiscard]] std::string hexOf(gfx::Color c, bool withAlpha = false);
// 1.10 : le code d'une couleur, #RRGGBB si elle est opaque, #RRGGBBAA sinon.
[[nodiscard]] std::string colorCode(gfx::Color c);
// 1.10 : teinte (degres), saturation et valeur (0 a 1) <-> rouge, vert, bleu.
[[nodiscard]] gfx::Color colorFromHsv(double h, double s, double v, std::uint8_t alpha = 255) noexcept;
// Un gris garde la teinte recue (h n'est pas touche).
void hsvOf(gfx::Color c, double& h, double& s, double& v) noexcept;

} // namespace ui
