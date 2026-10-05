// =============================================================================
//  ui/WidgetHost.hpp - un arbre de widgets hors de la pile des ecrans
// -----------------------------------------------------------------------------
//  Lot API 7 : un onglet detache vit dans une autre fenetre du systeme, avec son
//  propre renderer. Il lui faut ce que WidgetMenu donne a un ecran - la mise en
//  page a la taille de la surface, le dessin et sa passe du dessus (une liste
//  ouverte, un menu contextuel de la page), les evenements (ce qui peint
//  par-dessus les recoit d'abord, Tab promene le focus), les infobulles - sans
//  etre un menu du MenuManager : la pile n'a qu'une surface, celle de la
//  fenetre principale, et un seul focus.
//
//  Pas de SDL ici non plus : le proprietaire (app/DetachedWindows) lui passe les
//  evenements de SA fenetre et SON renderer. Le survol et le focus restent dans
//  cet arbre ; release() les efface d'un sous-arbre qui arrive ou qui s'en va,
//  pour qu'il n'apparaisse pas ailleurs encore surligne, ou avec un deuxieme
//  widget qui croit avoir le focus.
// =============================================================================
#pragma once

#include "Widget.hpp"

#include <string_view>

namespace ui {

// Lot 7 : OU L'INFOBULLE OUVERTE S'EST POSEE. Son texte est relu a chaque image
// (Widget::liveTooltip) : une valeur qui change la fait grandir ou retrecir.
// Le cote choisi a l'ouverture (a droite ou a gauche de la souris, dessous ou
// dessus) est garde tant qu'elle reste ouverte au meme endroit : elle ne saute
// pas d'un cote a l'autre, sauf si elle grandit au point de sortir de l'ecran.
// `valid` faux (ou la souris ailleurs) : le cote se choisit de nouveau.
struct TooltipPlacement {
    bool       valid{false};
    bool       left{false}, above{false};
    gfx::Point mouse{};
};

class WidgetHost {
public:
    explicit WidgetHost(WidgetPtr root);
    ~WidgetHost();
    WidgetHost(const WidgetHost&) = delete;
    WidgetHost& operator=(const WidgetHost&) = delete;

    [[nodiscard]] Widget& root() const noexcept { return *root_; }
    [[nodiscard]] FocusChain& focus() noexcept { return focus_; }

    // La racine prend toute la surface ; ce qui doit l'etre est remis en page.
    void layout(gfx::Size surface);
    // L'arbre, puis ce qui a demande la passe du dessus (dans l'ordre des
    // demandes), puis l'infobulle - apres 0,9 s sur le meme widget, comme dans
    // un ecran. `tooltips` faux : aucune (la fenetre attend une reponse ailleurs).
    void paint(gfx::IRenderer& r, const Theme& theme, gfx::Size surface, double time, bool tooltips = true);
    // Un evenement de la fenetre : ce qui peint par-dessus tout le recoit
    // d'abord ; Tab que personne ne prend promene le focus.
    EventResult dispatch(const InputEvent& e);
    // La souris a quitte la fenetre : plus rien n'est survole, plus d'infobulle.
    void pointerLeft();
    [[nodiscard]] gfx::Point pointer() const noexcept { return mouse_; }

    // Un sous-arbre qui change d'hote : sans survol ni focus.
    static void release(Widget& subtree);
    // Le widget dont l'infobulle vaut sous `p` (ce qui peint par-dessus et
    // couvre `p` cache les infobulles du dessous) ; nul : aucune.
    [[nodiscard]] static const Widget* tooltipTargetAt(Widget& root, gfx::Point p);
    // L'infobulle `text` pres de `mouse`, gardee dans `surface` ; les '\n'
    // coupent les lignes, une ligne trop longue passe a la ligne entre deux mots.
    // Lot 7 : `placement` (facultatif) garde le cote choisi d'une image a
    // l'autre - l'infobulle ouverte ne saute pas quand son texte change.
    static void paintTooltip(gfx::IRenderer& r, const Theme& theme, gfx::Size surface, gfx::Point mouse,
                             std::string_view text, TooltipPlacement* placement = nullptr);

private:
    static void clearHover(Widget& w);

    WidgetPtr         root_;
    FocusChain        focus_;
    gfx::Point        mouse_{-1.f, -1.f};
    bool              pointerInside_{false};
    // Compare seulement (jamais relu d'une image a l'autre) : le widget sous la
    // souris est recherche a chaque dessin, un widget detruit entre-temps ne
    // sert donc jamais.
    const Widget*     tooltipTarget_{nullptr};
    double            hoverSince_{0.0};
    TooltipPlacement  tipPlacement_{};     // lot 7 : l'infobulle ouverte ne saute pas
};

} // namespace ui
