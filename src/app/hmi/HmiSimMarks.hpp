// =============================================================================
//  app/hmi/HmiSimMarks.hpp - 1.9 : le violet du simule et ses reperes
// -----------------------------------------------------------------------------
//  LE VIOLET EST RESERVE A CE QUI EST SIMULE, partout : la ligne d'un esclave
//  simule dans Configuration > Equipements et sa fiche, l'onglet Esclaves
//  simules, et en marche les LECTURES SIMULEES (une valeur lue sur l'esclave
//  simule d'un equipement, pas sur le vrai appareil) :
//
//    * sur l'objet, un cadre violet en tirets et une pastille ronde (une fiole)
//      a cheval sur le coin haut droit - elle ne cache ni le libelle ni la
//      valeur, et se pousse a gauche si l'objet a deja un repere de qualite ;
//    * en haut de l'ecran de l'IHM, le bandeau LECTURES SIMULEES (raye) : quels
//      equipements, et pourquoi ;
//    * au survol (ou un appui long au doigt), une infobulle violette ;
//    * dans une barre d'etat, un texte violet a fiole (Indicator).
//
//  Ici : les couleurs, la fiole, le cadenas du lien, le cadre et sa pastille,
//  le bandeau, l'infobulle, l'indicateur. Le dessin seul : qui est simule, le
//  moteur le dit (Runtime::slaveReadOf, simulatedReads).
// =============================================================================
#pragma once

#include "../../platform/Geometry.hpp"
#include "../../ui/Widget.hpp"

#include <string>

namespace gfx { class IRenderer; }
namespace ui { struct Theme; }

namespace app::simmark {

// Les couleurs de la maquette (fond sombre) : le trait et le cadre, le texte,
// les bordures, les fonds (pastilles, cartes, titres de groupe).
inline constexpr gfx::Color kMark = gfx::Color::rgb(0x8B8FE8);
inline constexpr gfx::Color kText = gfx::Color::rgb(0xB3B6F5);
inline constexpr gfx::Color kLine = gfx::Color::rgb(0x6C71C4);
inline constexpr gfx::Color kBg   = gfx::Color::rgb(0x23264F);
inline constexpr gfx::Color kHead = gfx::Color::rgb(0x1D2150);
inline constexpr gfx::Color kCard = gfx::Color::rgb(0x1A1D45);

// Le violet d'un texte sur un fond de ce theme : clair sur un theme sombre,
// fonce sur un theme clair (le contraste reste lisible).
[[nodiscard]] gfx::Color text(const ui::Theme& theme);
[[nodiscard]] gfx::Color text(bool dark);
// Le fond violet d'une ligne simulee (une teinte legere), d'un titre de groupe.
[[nodiscard]] gfx::Color rowTint(bool dark);
[[nodiscard]] gfx::Color headTint(bool dark);

// La fiole (le simule) et le cadenas (lie au vrai appareil), au trait, dans `box`.
void drawFlask(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color color);
void drawLock(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color color);
// Le cadenas dans son carre violet (la ligne de l'esclave, la carte de sa fiche).
void drawLockBadge(gfx::IRenderer& r, const gfx::Rect& box);
// Le petit point violet, a halo ("lu par l'IHM").
void drawDot(gfx::IRenderer& r, gfx::Point centre, float radius = 4.f);
// Un rectangle en tirets.
void dashedRect(gfx::IRenderer& r, const gfx::Rect& rect, gfx::Color color, float thickness = 1.6f, float dash = 5.f, float gap = 3.f);

// LE REPERE D'UN OBJET : `object` est son cadre a l'ecran. Le cadre en tirets
// l'entoure (6 px dehors, au-dela du cadre de qualite) ; la pastille est sur
// le coin haut droit, a gauche de celle de la qualite si `besideQuality`.
[[nodiscard]] gfx::Rect markFrame(const gfx::Rect& object);
[[nodiscard]] gfx::Rect markPastille(const gfx::Rect& object, bool besideQuality);
void drawMark(gfx::IRenderer& r, const gfx::Rect& object, bool besideQuality, float alpha = 1.f);

// LE BANDEAU "LECTURES SIMULEES" : raye, la fiole, l'etiquette, le texte, et a
// droite `right` (gris clair). Hauteur conseillee : kRibbonH.
inline constexpr float kRibbonH = 26.f;
void drawRibbon(gfx::IRenderer& r, const gfx::Rect& area, const std::string& text, const std::string& right);

// L'INFOBULLE violette (un titre, un texte qui passe a la ligne) pres de
// `mouse`, gardee dans `inside`.
void drawTip(gfx::IRenderer& r, gfx::Point mouse, const gfx::Rect& inside, const std::string& title, const std::string& body);

// Un texte coupe a la largeur (avec "...").
[[nodiscard]] std::string fitText(gfx::IRenderer& r, const std::string& text, gfx::FontId font, float width);

// UN TEXTE A FIOLE pour une barre d'etat (StatusBar::addIndicator, a droite) :
// "2 equipements lus en simule". Vide : il ne prend pas de place.
class Indicator final : public ui::Widget {
public:
    explicit Indicator(std::string id = {});
    void setText(std::string text);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
protected:
    void onPaint(const ui::PaintContext&) override;
private:
    std::string text_;
    mutable float width_{0};
};

} // namespace app::simmark
