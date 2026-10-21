// =============================================================================
//  ui/DialogFrame.hpp - 1.12.21 : une fenetre modale se deplace et s'agrandit
// -----------------------------------------------------------------------------
//  Chaque dialogue calcule son cadre (centre a l'ecran) dans son onLayout() et
//  place tout le reste depuis lui. Il le passe a place(), qui rend le cadre a
//  employer : le sien, ou celui que l'utilisateur a choisi - deplace par la
//  barre de titre, agrandi par un bord ou un coin, mis en grand par un double
//  clic sur le titre (un autre double clic le rend). Une seule ligne change donc
//  par dialogue.
//
//  WidgetMenu passe les evenements de la souris a handle() avant les widgets du
//  dialogue et dessine la poignee du coin (paint) ; App montre le curseur de
//  cursor(). Un dialogue ne devient jamais plus petit que son cadre a lui : son
//  contenu y est place pour cette taille. La place choisie est retenue pour la
//  seance, sous le nom du menu, tant que le dialogue demande la meme taille.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"
#include "../platform/InputEvent.hpp"
#include "../platform/Renderer.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ui {

class Widget;
struct Theme;

namespace dialogframe {

// Ce que la souris prend : la barre de titre (deplacer), un bord ou un coin.
enum class Grip : std::uint8_t { None, Move, W, E, N, S, NW, NE, SW, SE };

// La bande du haut qui deplace le dialogue, et la largeur laissee a droite
// (la croix qui ferme, dessinee par le dialogue).
inline constexpr float kTitleBand = 34.f;
inline constexpr float kTitleRightKeep = 48.f;

// Le cadre a employer pour le dialogue dont `body` dessine le panneau ;
// `wanted` : le sien (centre).
[[nodiscard]] gfx::Rect place(Widget& body, gfx::Rect wanted);

// Un rectangle de la barre de titre que le dialogue garde pour lui (un onglet
// dessine dans le titre) : un clic dessus ne deplace pas. Apres place().
void keep(Widget& body, gfx::Rect r);

// Un evenement de l'arbre `root` (un dialogue) : vrai s'il est pris (glisser
// le titre ou un bord, double clic sur le titre).
bool handle(Widget& root, const InputEvent& ev);

// Ce que la souris montre : ce qu'il y a dessous, ou ce qui se glisse.
[[nodiscard]] Grip cursor() noexcept;

// La poignee du coin en bas a droite, par-dessus le dialogue de `root`.
void paint(Widget& root, gfx::IRenderer& r, const Theme& theme);

// Le nom sous lequel les dialogues mis en page pendant sa vie retiennent leur
// place (WidgetMenu : l'id du menu). Les portees s'imbriquent.
class KeyScope {
public:
    explicit KeyScope(std::string_view key);
    ~KeyScope();
    KeyScope(const KeyScope&) = delete;
    KeyScope& operator=(const KeyScope&) = delete;
private:
    std::string previous_;
};

// `body` s'en va (son destructeur) : sa place est retenue, son etat oublie.
void forget(const Widget* body) noexcept;

// Pour les essais : le cadre en cours de `root` (vide : aucun), et tout oublier.
[[nodiscard]] gfx::Rect frameOf(Widget& root);
void reset() noexcept;

} // namespace dialogframe
} // namespace ui
