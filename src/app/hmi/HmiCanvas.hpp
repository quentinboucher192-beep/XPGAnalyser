// =============================================================================
//  app/hmi/HmiCanvas.hpp — la vue en cours d'edition
// -----------------------------------------------------------------------------
//  CE QUE LA SOURIS FAIT, dans l'ordre ou elle le cherche :
//    - un objet a poser (palette) : clic = on le pose, au pas de la grille ;
//    - une poignee de la selection : redimensionner (Maj : garder les
//      proportions), ou la poignee ronde : tourner (Maj : par 15 degres) ;
//    - un guide : le deplacer, le tirer hors de la vue pour le retirer ;
//    - un objet : le choisir (Maj ou Ctrl : ajouter/retirer), puis le deplacer
//      avec magnetisme (Alt : sans) ; Alt au depart : le dupliquer d'abord ;
//      double-clic sur un groupe : entrer dedans (edition interne) ;
//    - rien : un cadre de selection.
//  Bouton du milieu, ou Espace + glisser : faire defiler. Ctrl + molette :
//  zoomer sous la souris. Fleches : 1 pixel, Maj + fleches : le pas de grille.
//
//  TOUT CE QUI MODIFIE PASSE PAR `apply` : une commande de la pile de
//  l'application. Un glisser est recalcule a chaque image depuis l'etat du
//  depart, avec une cle de fusion : la pile n'en garde qu'un pas.
// =============================================================================
#pragma once

#include "HmiPainter.hpp"
#include "../../core/Command.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiVertices.hpp"   // 1.12.12 (lot H) : les sommets sur le dessin
#include "../../ui/Widget.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace app {

class HmiCanvas final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;

    HmiCanvas(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply);

    [[nodiscard]] ui::SizeHint     sizeHint() const override;
    [[nodiscard]] hmi::Id          viewId() const noexcept { return viewId_; }
    [[nodiscard]] const hmi::View* view() const;

    // ---- selection ----------------------------------------------------------
    [[nodiscard]] const std::vector<hmi::Id>& selection() const noexcept { return selection_; }
    void setSelection(std::vector<hmi::Id> ids, bool notify = true);
    void selectAll();
    [[nodiscard]] hmi::Id insideGroup() const noexcept { return insideGroup_; }
    void enterGroup(hmi::Id group);
    void leaveGroup();

    // ---- outils -------------------------------------------------------------
    void setPlaceKind(std::optional<hmi::Kind> kind);
    [[nodiscard]] std::optional<hmi::Kind> placeKind() const noexcept { return placeKind_; }
    // Lot 10 : poser une instance d'un symbole du projet (le clic suivant).
    void setPlaceSymbol(std::string symbol);
    [[nodiscard]] const std::string& placeSymbol() const noexcept { return placeSymbol_; }
    // Lot 12 : poser l'objet d'une variable (la bibliotheque, onglet Variables) :
    // au clic suivant, ou la ou elle est lachee (glissee depuis la bibliotheque).
    void setPlaceVariable(std::optional<hmi::design::VarInfo> variable);
    [[nodiscard]] const std::optional<hmi::design::VarInfo>& placeVariable() const noexcept { return placeVariable_; }
    void setShowHidden(bool on) { showHidden_ = on; invalidate(); }
    [[nodiscard]] bool showHidden() const noexcept { return showHidden_; }

    // ---- zoom ---------------------------------------------------------------
    void  setZoom(float z);
    void  zoomBy(float factor, std::optional<gfx::Point> anchor = std::nullopt);
    void  zoomToFit();
    // Lot 12 : le point (x, y) de la vue en haut a gauche de la zone de dessin
    // (a `margin` pixels du bord), au zoom courant.
    void  scrollTo(double x, double y, float margin = 24.f);
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    [[nodiscard]] HmiViewport viewport() const noexcept;

    // ---- ce que font les barres d'outils ------------------------------------
    // Une modification de la vue, en commande. Rend faux si rien n'a change.
    bool edit(const std::string& label, const hmi::ViewChange& fn, std::string mergeKey = {});
    void deleteSelection();
    void duplicateSelection(double offset = 20.0);   // Ctrl+D (1.12.18) : a cote ; Alt+glisser : dessus
    // 1.11 (R111) : apres "Dupliquer", la selection va aux copies, qui sont
    // retenues : Ctrl+Z les retire, Ctrl+Y les remet - et la selection y revient.
    void selectCopies(std::vector<hmi::Id> ids);
    void copySelection();
    void cutSelection();
    void paste();
    // Lot 20 : une colonne de noms (Excel, un tableau du projet) : une etiquette
    // et l'objet de chaque variable, alignes depuis le dernier clic ; un seul
    // Ctrl+Z. Rend le nombre de variables posees.
    std::size_t pasteNames(const std::string& text);
    // Les variables connues (IHM et automate), pour savoir quel objet poser.
    void setVariableSource(std::function<std::vector<hmi::design::VarInfo>()> f) { variableSource_ = std::move(f); }
    void copyStyle();
    void pasteStyle();
    void groupSelection();
    void ungroupSelection();
    void align(hmi::edit::Align);
    void distribute(bool horizontal);
    void zorder(hmi::edit::ZMove);
    void rotateSelection(double delta);
    void mirrorSelection(bool horizontal);
    void lockSelection(bool on);
    void hideSelection(bool on);
    void nudge(double dx, double dy);
    void addGuide(bool vertical);
    void toggleGrid();
    void toggleSnap();

    // Le document a change ailleurs (annulation, explorateur, proprietes) :
    // la selection oublie ce qui a disparu.
    void documentChanged();

    // ---- 1.12.12 (lot H) : les sommets sur le dessin ------------------------
    //  Une ligne, un polygone choisis seuls : une poignee ronde par sommet. La
    //  glisser deplace le sommet (grille, magnetisme ; Alt : sans) ; un double-clic
    //  sur un segment en insere un ; un clic choisit le sommet, les fleches le
    //  deplacent, Suppr (ou le menu « Retirer le sommet ») le retire. La zone d'un
    //  plan choisie dans Contenu et la reference d'une courbe XY (echelle fixe) se
    //  reglent de meme ; « Tracer une zone » : chaque clic pose un point, un
    //  double-clic (ou un clic sur le premier point) la ferme, Echap l'abandonne.
    //  Chaque geste : un pas d'annulation. Le cadre d'une forme suit ses points.
    [[nodiscard]] hmi::vertices::Target vertexTarget() const;
    [[nodiscard]] int  selectedVertex() const noexcept { return vertex_; }
    void selectVertex(int index);                       // -1 : aucun
    bool removeSelectedVertex();                        // faux : aucun, ou il n'en resterait pas assez
    void setEditedZone(hmi::Id map, int zone);          // la zone choisie dans Contenu ; -1 : aucune
    [[nodiscard]] int  editedZone() const noexcept { return zone_; }
    void startZoneTrace(hmi::Id map);                   // les clics suivants posent une zone du plan
    [[nodiscard]] bool tracing() const noexcept { return traceMap_ != hmi::kNoId; }
    const core::SignalPtr<int> vertexChosen = core::Signal<int>::create();   // le sommet choisi (-1 : aucun)
    const core::SignalPtr<int> zoneTraced   = core::Signal<int>::create();   // une zone tracee : son rang

    const core::SignalPtr<>                   selectionChanged = core::Signal<>::create();
    const core::SignalPtr<const std::string&> status          = core::Signal<const std::string&>::create();
    const core::SignalPtr<>                   placed          = core::Signal<>::create();
    // Lot 10 : double-clic sur une instance de symbole - le nom de son symbole.
    const core::SignalPtr<const std::string&> symbolOpened    = core::Signal<const std::string&>::create();
    // Ctrl+Z (false) / Ctrl+Y ou Ctrl+Maj+Z (true) : la pile est celle de
    // l'application, c'est l'hote qui annule.
    const core::SignalPtr<bool>               historyRequested = core::Signal<bool>::create();
    // 1.10.2 (chantier D) : la fenetre "Dupliquer..." (reperes, indices, pose), ouverte
    // par l'hote - Ctrl+Maj+D depuis la 1.12.18 ; Ctrl+D fait la copie decalee de 20 px.
    const core::SignalPtr<>                   duplicateRequested = core::Signal<>::create();
    // 1.10.2 (chantier D) : le clic droit (l'objet sous le pointeur, hors de la
    // selection, la remplace) - l'editeur ouvre son menu a ce point.
    const core::SignalPtr<gfx::Point>         contextRequested = core::Signal<gfx::Point>::create();
    // 1.12.18 : F2 sur la vue - l'editeur renomme l'objet choisi (dans l'explorateur).
    const core::SignalPtr<>                   renameRequested = core::Signal<>::create();

    // ---- 1.12.18 : CE QUE VISE LE CLIC DROIT ---------------------------------
    //  Un objet libre : il rejoint la selection (gardee s'il y est deja) - le menu
    //  vise la selection. Un objet verrouille : le menu le vise lui seul, et la
    //  selection est videe (bug 4 : avant, le clic le traversait et Supprimer
    //  supprimait l'ancienne selection). Le vide : la selection est videe, le
    //  menu est celui de la vue. Pendant une pose ou le trace d'une zone, le clic
    //  droit l'annule, comme Echap, sans menu. Maj+F10 / Menu : le menu de la
    //  selection, sous elle (ou celui de la vue).
    enum class ContextTarget : std::uint8_t { View, Selection, Locked };
    [[nodiscard]] ContextTarget contextTarget() const noexcept { return contextTarget_; }
    [[nodiscard]] hmi::Id       contextLocked() const noexcept { return contextLocked_; }
    [[nodiscard]] gfx::Point    contextScreen() const noexcept { return contextScreen_; }   // le point du clic
    [[nodiscard]] hmi::edit::Pt contextPoint() const;                                       // ... dans la vue
    void cancelPlacement();                       // la pose (ou le trace) abandonnee, comme Echap
    void pasteAt(gfx::Point screen);              // « Coller ici » : le coin de la copie au point
    void insertAt(hmi::Kind kind, gfx::Point screen);   // « Inserer » : un objet neuf au point
    void addGuideAt(bool vertical, gfx::Point screen);  // « Ajouter un guide » : par le point

    // ---- la souris pendant un glisser ---------------------------------------
    // Un cadre de selection, un deplacement ou un guide tire peuvent sortir de
    // la vue : jusqu'au relachement, la vue garde la souris (comme la liste
    // ouverte d'une DropDown), sinon le relachement tombe sur le panneau d'a
    // cote et le glisser reste pendu.
    [[nodiscard]] bool      requestsOverlayPass() const override { return drag_ != Drag::None; }
    [[nodiscard]] gfx::Rect eventBounds() const override;
    [[nodiscard]] bool      dragging() const noexcept { return drag_ != Drag::None; }

    // ---- pour les tests et les captures -------------------------------------
    void setHoverForTest(hmi::Id id) { hover_ = id; }
    void setMarqueeForTest(const hmi::Box& b) { marquee_ = b; marqueeShown_ = true; }
    void setSnapLinesForTest(std::vector<double> xs, std::vector<double> ys) { snapX_ = std::move(xs); snapY_ = std::move(ys); }
    void setPointerForTest(gfx::Point p) { pointer_ = p; pointerIn_ = true; }

    // ---- 1.12.26 : LE SUIVI DES MODIFICATIONS SUR LA VUE ----------------------
    //  L'infobulle de suivi : la souris arretee sur un objet (une demi-seconde) - son nom, son
    //  genre, sa derniere modification (quand, sur quel PC, quel compte), ce qui n'est pas
    //  enregistre. Le point orange : un objet change depuis le dernier enregistrement. L'editeur
    //  donne les textes et les objets ; le clic droit sur le fond de la vue les allume ou les eteint.
    void setEditTip(std::function<std::string(hmi::Id)> tip) { editTip_ = std::move(tip); }
    void setEditTipOn(bool on) noexcept { editTipOn_ = on; }
    [[nodiscard]] bool editTipOn() const noexcept { return editTipOn_; }
    void setPendingMarks(std::vector<hmi::Id> ids) { pendingMarks_ = std::move(ids); invalidate(); }
    [[nodiscard]] const std::vector<hmi::Id>& pendingMarks() const noexcept { return pendingMarks_; }
    void setPendingDotsOn(bool on) { pendingDots_ = on; invalidate(); }
    [[nodiscard]] bool pendingDotsOn() const noexcept { return pendingDots_; }
    // L'infobulle de suivi a ce point de l'ecran, sans attendre (essais, scripts) ; vide : aucun objet.
    [[nodiscard]] std::string editTipAt(gfx::Point screen) const;
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return (editTipOn_ && static_cast<bool>(editTip_)) || Widget::hasTooltip(); }

protected:
    void               onLayout() override;
    void               onPaint(const ui::PaintContext&) override;
    ui::EventResult    onEvent(const ui::InputEvent&) override;

private:
    enum class Drag : std::uint8_t { None, Move, Resize, Rotate, Marquee, Pan, Guide, Vertex };

    [[nodiscard]] hmi::edit::Pt toView(gfx::Point screen) const;
    [[nodiscard]] int  handleAt(gfx::Point screen) const;          // -1 : aucune ; 8 : rotation
    [[nodiscard]] int  guideAt(gfx::Point screen) const;
    [[nodiscard]] std::vector<gfx::Point> handlePoints() const;   // 8 poignees + rotation
    [[nodiscard]] bool singleTransformable() const;
    void emitStatus(gfx::Point screen);
    void paintSelection(const ui::PaintContext&, const HmiViewport&);
    void paintGrid(const ui::PaintContext&, const HmiViewport&, const hmi::View&);

    hmi::DocumentPtr       doc_;
    hmi::Id                viewId_;
    Apply                  apply_;
    std::vector<hmi::Id>   selection_;
    hmi::Id                insideGroup_{hmi::kNoId};
    ContextTarget          contextTarget_{ContextTarget::View};   // 1.12.18
    hmi::Id                contextLocked_{hmi::kNoId};
    gfx::Point             contextScreen_{};
    std::vector<hmi::Id>   copies_;            // 1.11 (R111) : les dernieres copies de Dupliquer
    bool                   copiesHere_{false}; // etaient-elles dans la vue au dernier changement ?
    std::optional<hmi::Kind> placeKind_;
    std::string            placeSymbol_;       // lot 10 : placeKind_ = SymbolInstance
    std::optional<hmi::design::VarInfo> placeVariable_;   // lot 12 : placeKind_ = l'objet de la variable
    bool                   downHere_{false};   // lot 12 : l'appui a eu lieu ici (sinon : un lacher venu de la bibliotheque)
    void placeAt(gfx::Point screen, bool keep);
    bool                   showHidden_{false};
    std::function<std::vector<hmi::design::VarInfo>()> variableSource_;   // lot 20

    float                  zoom_{1.f};
    float                  panX_{24.f}, panY_{24.f};     // le coin de la vue, depuis le coin du widget
    bool                   fitted_{false};

    Drag                   drag_{Drag::None};
    int                    dragHandle_{-1};
    int                    dragGuide_{-1};
    unsigned               dragSerial_{0};
    gfx::Point             pressScreen_{};
    hmi::edit::Pt          pressView_{};
    hmi::View              snapshot_;
    hmi::Box               startBox_{};
    double                 startAngle_{0};
    bool                   spaceDown_{false};
    bool                   middlePan_{false};

    hmi::Id                hover_{hmi::kNoId};
    gfx::Point             pointer_{};
    bool                   pointerIn_{false};
    hmi::Box               marquee_{};
    bool                   marqueeShown_{false};
    std::vector<double>    snapX_, snapY_;
    std::vector<hmi::edit::SpacingMark> spacing_;     // lot 12 : les ecarts egaux, pendant qu'on deplace
    // 1.12.26 : le suivi des modifications (l'infobulle, le point orange)
    std::function<std::string(hmi::Id)> editTip_;
    bool                   editTipOn_{true};
    bool                   pendingDots_{true};
    std::vector<hmi::Id>   pendingMarks_;
    double                 paintTime_{0};
    double                 hoverSince_{-1};             // -1 : a poser au prochain dessin
    void paintPendingDots(const ui::PaintContext&, const HmiViewport&);

    // 1.12.12 (lot H) : les sommets.
    void traceClick(gfx::Point screen, int clicks, bool free);
    void paintVertices(const ui::PaintContext&, const HmiViewport&);
    int                    vertex_{-1};                 // le sommet choisi de la cible
    hmi::Id                vertexOwner_{hmi::kNoId};    // son objet (une autre selection l'oublie)
    hmi::vertices::Target  dragTarget_;                 // la cible du sommet glisse
    hmi::edit::Pt          grab_{};                     // l'ecart entre la souris et le sommet saisi
    hmi::Id                zoneMap_{hmi::kNoId};        // le plan dont Contenu montre une zone
    int                    zone_{-1};                   // ... et cette zone
    hmi::Id                traceMap_{hmi::kNoId};       // le plan ou se trace une zone
    std::vector<hmi::edit::Pt> trace_;                  // ses points poses (dans la vue)
};

} // namespace app
