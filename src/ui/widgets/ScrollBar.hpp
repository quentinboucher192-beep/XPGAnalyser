// =============================================================================
//  ui/widgets/ScrollBar.hpp - 1.11.4 : une barre de defilement qu'on tire
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « verifier que toutes les scrollbars de
//  l'appli soient deplacables avec la souris ». Avant : la plupart se
//  dessinaient seulement (la molette les faisait bouger), et les listes, les
//  arbres, les tableaux et l'inspecteur n'en avaient pas du tout.
//
//  LA MEME REGLE PARTOUT :
//   - le POUCE se prend et se tire (la souris est tenue : le glisser continue
//     hors du widget, Widget::captureMouse) ;
//   - un clic dans la GOUTTIERE avance d'une page vers le clic ;
//   - le pouce s'eclaire au survol et pendant le glisser.
//
//  Deux formes :
//   - ScrollAxis + ScrollBarDrag + paintScrollBar : pour un widget qui dessine
//     tout lui-meme (TreeView, ListView, TableView, PropertyGrid, un volet) -
//     il garde son decalage, la barre le lit et le change ;
//   - ScrollBar : un widget enfant (ScrollablePanel, dont le contenu couvrirait
//     sinon la gouttiere et prendrait ses clics).
// =============================================================================
#pragma once

#include "../Widget.hpp"

#include <functional>

namespace ui {

inline constexpr float kScrollBarThickness = 10.f;

// Un axe de defilement : la gouttiere, la longueur du contenu et celle qu'on en voit.
struct ScrollAxis {
    gfx::Rect track{};
    float     content{0.f};
    float     viewport{0.f};
    bool      horizontal{false};
    float     minThumb{24.f};

    [[nodiscard]] bool  needed() const noexcept { return content > viewport + 0.5f && track.w > 0.f && track.h > 0.f; }
    [[nodiscard]] float maxOffset() const noexcept { return content > viewport ? content - viewport : 0.f; }
    [[nodiscard]] float length() const noexcept { return horizontal ? track.w : track.h; }
    [[nodiscard]] gfx::Rect thumb(float offset) const noexcept;
};

// Le glisser d'une barre (un par axe). Les points sont ceux des evenements.
class ScrollBarDrag {
public:
    // Un appui gauche dans la gouttiere : sur le pouce, il le prend (la souris est
    // tenue par `owner`) ; ailleurs, une page vers le clic. Vrai : l'appui est pris.
    bool press(Widget& owner, const ScrollAxis&, gfx::Point p, float& offset);
    // Pendant le glisser : le nouveau decalage. Vrai : il a change.
    bool move(const ScrollAxis&, gfx::Point p, float& offset) const;
    // Le relachement : la souris rendue. Vrai : un glisser se terminait.
    bool release(Widget& owner);
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Les trois a la fois, pour un onEvent : vrai si l'evenement est pris (le
    // decalage, borne, est alors dans `offset` ; `changed` dit s'il a bouge).
    bool handle(Widget& owner, const ScrollAxis&, const InputEvent&, float& offset, bool* changed = nullptr);

private:
    bool  active_{false};
    float grab_{0.f};   // ou le pouce a ete pris, depuis son debut
};

// La barre : la gouttiere discrete, le pouce arrondi (plus clair au survol ou tire).
void paintScrollBar(const PaintContext&, const ScrollAxis&, float offset, bool hot = false, bool dragging = false);
// Le pouce sous la souris (pour l'eclairer).
[[nodiscard]] bool overThumb(const ScrollAxis&, float offset, gfx::Point p) noexcept;

// ---- la barre au bord d'une zone (un widget qui dessine tout lui-meme) ------------
// Posee au bord droit (verticale) ou bas (horizontale) de la zone qui defile ; space()
// dit la place a laisser aux lignes pour qu'elle ne couvre pas leur bout (un compte,
// une pastille). handle() se met en tete de onEvent.
class EdgeScrollBar {
public:
    explicit EdgeScrollBar(bool horizontal = false) noexcept : horizontal_(horizontal) {}
    [[nodiscard]] ScrollAxis axis(gfx::Rect area, float content, float viewport) const noexcept;
    [[nodiscard]] float      space(gfx::Rect area, float content, float viewport) const noexcept;
    void paint(const PaintContext&, gfx::Rect area, float content, float viewport, float offset) const;
    // Vrai : l'evenement est pris (le decalage, borne, est dans `offset` ; owner invalide).
    bool handle(Widget& owner, const InputEvent&, gfx::Rect area, float content, float viewport, float& offset);
    [[nodiscard]] bool active() const noexcept { return drag_.active(); }

private:
    bool          horizontal_;
    ScrollBarDrag drag_;
    bool          hot_{false};
};

// Une barre dont la place ne se sait qu'au dessin (un volet qui calcule sa liste en se
// dessinant) : paint() la note, handle() la relit.
class PaintedScrollBar {
public:
    void paint(const PaintContext&, gfx::Rect area, float content, float viewport, float offset);
    bool handle(Widget& owner, const InputEvent&, float& offset);
    [[nodiscard]] bool active() const noexcept { return bar_.active(); }

private:
    EdgeScrollBar bar_;
    gfx::Rect     area_{};
    float         content_{0.f}, viewport_{0.f};
};

// ---- le widget (un enfant) -----------------------------------------------------
class ScrollBar : public Widget {
public:
    explicit ScrollBar(std::string id, bool horizontal);
    // Ce qu'il montre : la longueur du contenu, celle qu'on voit, le decalage.
    void setRange(float content, float viewport, float offset);
    [[nodiscard]] bool needed() const noexcept;
    // Le decalage voulu par l'utilisateur (tirer, cliquer dans la gouttiere).
    std::function<void(float)> onScroll;

protected:
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    [[nodiscard]] ScrollAxis axis() const noexcept;
    bool          horizontal_;
    float         content_{0.f}, viewport_{0.f}, offset_{0.f};
    bool          hot_{false};
    ScrollBarDrag drag_;
};

} // namespace ui
