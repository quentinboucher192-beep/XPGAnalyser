// =============================================================================
//  ui/widgets/DataViews.hpp — ListView, TreeView, TableView, PropertyGrid
// -----------------------------------------------------------------------------
//  These four are the load-bearing widgets: everything about the 50 000-variable
//  / 10 000-POU requirement is decided here.
//
//  Three rules make that target reachable:
//
//   1. MODEL / VIEW SEPARATION. The view never owns rows. It asks an
//      ITableModel for cell text by (row, column). The model is backed by the
//      domain object graph, so importing a project costs one allocation pass,
//      not one widget per cell.
//
//   2. VIRTUALISATION. Only rows intersecting the viewport are measured and
//      painted — for a 22 px row and a 700 px viewport that is ~32 rows,
//      independent of model size.
//
//   3. INDIRECTION VECTOR. Sorting and filtering never touch the model. They
//      rebuild a std::vector<RowIndex> mapping view rows to model rows. Sorting
//      50 000 rows is then one std::stable_sort over 4-byte indices (~4 ms),
//      and clearing a filter is a vector swap.
// =============================================================================
#pragma once

#include "ScrollBar.hpp"   // 1.11.4 : les barres de defilement qu'on tire
#include "../Icons.hpp"
#include "../TextSearch.hpp"      // lot recherche : la recherche surlignee, les filtres de colonne
#include "../Widget.hpp"
#include "Containers.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

class ColorPalette;   // 1.10 (chantier Q) : ColorPalette.hpp n'est plus inclus ici (moins a recompiler)

using RowIndex  = std::uint32_t;
using NodeId    = std::uint64_t;
constexpr NodeId kInvalidNode = 0;

enum class SortOrder : std::uint8_t { None, Ascending, Descending };
enum class SelectionMode : std::uint8_t { None, Single, Multi, Extended /*shift+ctrl*/ };

// 1.9 : UNE PASTILLE DE COULEUR (le mode d'un parametre de popup : REF, COPIE,
// LES DEUX) - dans la case de la valeur d'une grille (PropertyGrid::Property::
// pill) ou devant le texte d'une case de table (CellStyle::colorLead). Les
// couleurs sont celles de l'hote (rendues lisibles sur le fond par la vue).
struct ColorPill {
    std::string text{};                   // en capitales, gras
    gfx::Color  ink{};                    // le texte
    gfx::Color  border{};                 // le contour
    gfx::Color  fill{};                   // le fond (alpha compris)
    std::optional<gfx::Color> fillTo{};   // pleine : un degrade de `fill` a `fillTo`
};

// Cell decoration returned by the model; keeps colour policy in the domain
// adapter (e.g. "unused variable" -> warning) instead of hard-coded in the view.
struct CellStyle {
    std::optional<gfx::Color> fg, bg;
    bool  bold{false};
    bool  monospace{false};
    Icon  icon{Icon::None};
    // Icons carry state as well as kind: an unused DFB gets the same glyph in
    // the warning colour rather than a second asset.
    std::optional<gfx::Color> iconColor;

    // --- ajoutes a la FIN : les modeles existants ne les voient pas ---------
    //
    // Les couleurs DITES PAR LEUR SENS. Un modele est construit avant la
    // premiere image et ne connait pas le theme ; il dit « famille 2 » ou
    // « avertissement », et la vue resout dans le theme du moment. Un Tone
    // prime sur la couleur fixe correspondante.
    Tone        iconTone{Tone::None};
    Tone        fgTone{Tone::None};

    // Une pastille a droite de la ligne : un compteur, une couverture "9/11".
    // Elle remplace le texte colle au nom, qui se lisait comme une partie du
    // nom et se tronquait avec lui.
    std::string badge{};
    Tone        badgeTone{Tone::None};     // None : pastille neutre

    // Lot 16 : UNE TABLE EN ARBRE. Sur la premiere colonne : un retrait (en px)
    // et une fleche (-1 : aucune ; 0 : repliee ; 1 : depliee) - un clic sur la
    // fleche emet TableView::expanderClicked. `spanRow` (premiere colonne) : la
    // ligne est un titre de groupe, son texte court sur toute la largeur.
    float       indent{0.f};
    int         expander{-1};
    bool        spanRow{false};

    // Lot 21 : UNE ICONE QUE LA TABLE NE CONNAIT PAS (celles de l'IHM : la tuile
    // d'un objet, le chapitre d'un sujet d'aide) - un numero que l'hote dessine
    // (TableView::setIconPainter) ; 0 : aucune. `icon` passe avant.
    int         customIcon{0};

    // Lot API 2 : DES PASTILLES A ICONE, alignees a droite (avant la pastille de
    // texte) - les sept de l'onglet Macros : ce que la macro touche. Une pastille
    // eteinte reste a sa place, a peine visible : les colonnes restent alignees
    // d'une ligne a l'autre et l'absence se lit autant que la presence.
    struct Pip { Icon icon{Icon::None}; Tone tone{Tone::None}; bool on{false}; };
    std::vector<Pip> pips{};

    // Lot API 6 : UNE IMAGE DE 16 x 16 devant le texte (RGBA, 1 024 octets) -
    // l'icone d'un projet dans la liste de l'accueil. Dessinee pixel par pixel :
    // le modele n'a pas de renderer pour en faire une texture. `icon` passe avant.
    std::shared_ptr<const std::vector<std::uint8_t>> image16{};

    // ---- Lot API 8 : l'arbre du projet ----
    // Un texte gris apres le nom (TreeView) : ou est un resultat du filtre de
    // l'arbre ("Section - de Logigrammes_A"). Vide : rien.
    std::string hint{};
    // Des boutons dans la ligne, a la place du texte (TreeView) : la rangee des
    // outils sous le titre d'un domaine. Libelles si tout tient, sinon icones
    // seules (le libelle en infobulle). Un clic : TreeView::chipClicked.
    struct Chip { Icon icon{Icon::None}; std::string label; };
    std::vector<Chip> chips{};
    // 2e partie. LA COULEUR DU DOMAINE (TreeView) : 0 aucune ; 1 API (accent),
    // 2 IHM (info), 3 Simulation (ok), 4 Versions (la couleur des mots-cles du
    // code). Elle teint les guides d'indentation et une barre a gauche de la
    // ligne choisie ; un titre de domaine (domainHead) a son icone et une barre
    // a gauche de cette couleur, et reste colle en haut quand on fait defiler.
    std::uint8_t domain{0};
    bool         domainHead{false};
    // Un point apres le nom (le point orange "modifie depuis V47"), et son
    // infobulle. Tone::None : pas de point.
    Tone         dotTone{Tone::None};
    std::string  dotTip{};
    // Une deuxieme pastille, a gauche de `badge` (TreeView) : l'etat qui
    // s'ajoute au compteur (rouge : "2" expressions impossibles ; orange :
    // "5" mises a jour de bibliotheque). Vide : aucune.
    std::string  pill{};
    Tone         pillTone{Tone::None};
    // ---- fin Lot API 8 : l'arbre du projet ----
    // 1.9 : UNE ETIQUETTE DEVANT LE TEXTE (TableView), bordee de sa couleur :
    // "AUTO" dans la colonne L'IHM lit des equipements. Vide : aucune.
    std::string  lead{};
    Tone         leadTone{Tone::None};
    // 1.9 : UNE VRAIE CASE A COCHER devant le texte (premiere colonne) : -1 aucune,
    // 0 decochee, 1 cochee. Un clic dessus : TableView::checkClicked.
    std::int8_t  check{-1};
    // 1.9 : une pastille de couleur devant le texte (le mode d'un parametre).
    std::optional<ColorPill> colorLead{};
    // 1.11 (chantier T3, C4) : LES ICONES COMPILABLE / GENERABLE, tout a droite
    // de la ligne (TreeView), avant les pastilles : ✓ compile, ✕ n en erreur,
    // ⊘ non compilable ; pour un script de l'IHM, ⇩ genere ou ⇩ barre (non
    // genere). Chacune : son glyphe (texte), son ton, son infobulle (la raison).
    struct Trail { std::string glyph; Tone tone{Tone::None}; bool struck{false}; std::string tip; };
    std::vector<Trail> trail{};
    // ---- 1.11.22 : l'explorateur modernise (TreeView::setModernLook) ----
    // Des etiquettes apres le nom, teintees de leur ton : "E/S", "sortie", "conservee",
    // "3 surcharges", "3 mises a jour". Vide : aucune.
    struct Tag { std::string text; Tone tone{Tone::None}; };
    std::vector<Tag> tags{};
    // La fin du texte en gris, a partir de cet octet (la signature d'une fonction,
    // "ST . Demarrage" d'un script) ; npos : tout le texte dans sa couleur. `mutedMono` :
    // cette fin dans la police du code. Le texte du modele ne change pas.
    std::size_t mutedFrom{std::string::npos};
    bool        mutedMono{false};
    // Le texte dessine a la place de celui du modele (ce que disent les etiquettes en est
    // retire : "RandomSeed : REAL" et l'etiquette "E/S") ; vide : le texte du modele.
    // Le texte du modele (recherche, sessions, essais) ne change pas.
    std::string display{};
    // 1.11.23 : un titre de domaine (le nouveau dessin) - sa ligne d'etat en gris apres le nom
    // ("29 POU \xC2\xB7 891 variables", "arretee \xC2\xB7 cycle 0"), et ses actions au survol (Reanalyser,
    // Generer, Demarrer, Creer une version) : TreeView::headActionClicked (noeud, rang).
    std::string       subtitle{};
    std::vector<Chip> headActions{};
};

// ============================================================== ListView ====
class IListModel {
public:
    virtual ~IListModel() = default;
    [[nodiscard]] virtual std::size_t rowCount() const = 0;
    [[nodiscard]] virtual std::string text(RowIndex) const = 0;
    [[nodiscard]] virtual CellStyle   style(RowIndex) const { return {}; }
};

class ListView : public Widget {
public:
    explicit ListView(std::string id = {});
    void setModel(std::shared_ptr<IListModel> m);
    void setSelectionMode(SelectionMode m) { selectionMode_ = m; }
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }   // 1.11.4
    [[nodiscard]] const std::vector<RowIndex>& selection() const noexcept { return selection_; }

    const core::SignalPtr<RowIndex> activated        = core::Signal<RowIndex>::create();
    const core::SignalPtr<RowIndex> selectionChanged = core::Signal<RowIndex>::create();
protected:
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
private:
    [[nodiscard]] int rowAt(float globalY) const;

    std::shared_ptr<IListModel> model_;
    std::vector<RowIndex>       selection_;
    SelectionMode               selectionMode_{SelectionMode::Single};
    float                       scrollY_{0.f};
    EdgeScrollBar               vbar_;                // 1.11.4 : la barre qu'on tire
    mutable float               rowHeight_{24.f};   // cached from the theme at paint time
    core::ConnectionScope       modelConnections_;
    int                         hoverRow_{-1};      // la ligne sous la souris
};

// ============================================================== TreeView ====
// Lazy loading contract: hasChildren() may return true before childCount() is
// meaningful. The view calls fetchChildren() exactly once, the first time a node
// is expanded, and shows a spinner row until the model signals completion. That
// is what lets the Project Explorer show 10 000 POUs without walking the whole
// object graph at import time.
class ITreeModel {
public:
    virtual ~ITreeModel() = default;

    [[nodiscard]] virtual NodeId      root() const = 0;
    [[nodiscard]] virtual std::size_t childCount(NodeId) const = 0;
    [[nodiscard]] virtual NodeId      childAt(NodeId parent, std::size_t i) const = 0;
    [[nodiscard]] virtual bool        hasChildren(NodeId) const = 0;
    [[nodiscard]] virtual std::string text(NodeId) const = 0;
    [[nodiscard]] virtual CellStyle   style(NodeId) const { return {}; }
    [[nodiscard]] virtual bool        isLoaded(NodeId) const { return true; }

    virtual void fetchChildren(NodeId) {}                 // async; emits childrenReady
    const core::SignalPtr<NodeId> childrenReady = core::Signal<NodeId>::create();
    const core::SignalPtr<>       modelReset    = core::Signal<>::create();
};

class TreeView : public Widget {
public:
    explicit TreeView(std::string id = {});
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }   // 1.11.4

    void setModel(std::shared_ptr<ITreeModel> m);
    void expand(NodeId, bool recursive = false);
    void collapse(NodeId);
    void toggle(NodeId);
    void expandToDepth(int depth);
    // Lot API 7 : sans deplier ce que `stop` refuse (les membres des variables,
    // qui se deplient a l'infini) - les noeuds refuses restent replies.
    void expandToDepth(int depth, const std::function<bool(NodeId)>& stop);
    void ensureVisible(NodeId);
    void setFilter(std::function<bool(NodeId)> predicate);   // keeps ancestors of matches
    // Lot API 2 : les icones que l'arbre ne connait pas (CellStyle::customIcon) -
    // les glyphes de famille des macros : l'hote les dessine.
    using IconPainter = std::function<void(gfx::IRenderer&, int icon, const gfx::Rect& box, gfx::Color color)>;
    void setIconPainter(IconPainter painter) { iconPainter_ = std::move(painter); }
    void setShowRootNode(bool s);
    void setSelectionMode(SelectionMode m) { selectionMode_ = m; }

    // Deplie-t-on ce noeud ? Expose pour que l'etat de l'arbre survive a un
    // rechargement de projet : sans ca, chaque analyse replie tout et l'endroit
    // ou on travaillait est a retrouver a la main.
    [[nodiscard]] bool isExpanded(NodeId n) const noexcept {
        return std::binary_search(expanded_.begin(), expanded_.end(), n);
    }

    [[nodiscard]] NodeId currentNode() const noexcept { return current_; }
    [[nodiscard]] const std::vector<NodeId>& selection() const noexcept { return selection_; }

    // Les lignes montrees, et ou elles sont a l'ecran : ce qu'il faut a un
    // script qui clique dans l'arbre comme le ferait une souris (captures,
    // tests de bout en bout). rowRect rend false pour un noeud qui n'est pas
    // montre - replie, filtre - ou qui est hors de la zone visible.
    [[nodiscard]] std::vector<NodeId> visibleNodes() const;
    [[nodiscard]] bool rowRect(NodeId, gfx::Rect& out) const;
    [[nodiscard]] const std::shared_ptr<ITreeModel>& model() const noexcept { return model_; }

    const core::SignalPtr<NodeId> selectionChanged = core::Signal<NodeId>::create();
    const core::SignalPtr<NodeId> activated        = core::Signal<NodeId>::create();  // double-click / Enter
    const core::SignalPtr<NodeId, gfx::Point> contextMenuRequested = core::Signal<NodeId, gfx::Point>::create();
    // Lot 7 : F2 et Suppr sur le noeud courant, l'arbre ayant le focus - l'ecran
    // renomme ou supprime ce que le noeud designe (son menu du clic droit montre
    // ces touches). Rien de branche : la touche passe, comme avant.
    const core::SignalPtr<NodeId> renameRequested  = core::Signal<NodeId>::create();
    const core::SignalPtr<NodeId> deleteRequested  = core::Signal<NodeId>::create();

    // Lot 7 : L'INFOBULLE D'UN NOEUD, relue tant qu'elle est ouverte (une valeur
    // en simulation, un etat qui change). Le fournisseur rend le texte du noeud
    // sous la souris ; "" : celle de l'arbre (tooltip()).
    using NodeTooltip = std::function<std::string(NodeId)>;
    void setNodeTooltip(NodeTooltip provider) { nodeTooltip_ = std::move(provider); }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return Widget::hasTooltip() || static_cast<bool>(nodeTooltip_); }

    // ---- glisser-deposer ----------------------------------------------------
    //
    //  Le widget ne sait pas ce qu'un depot VEUT DIRE : deplacer une section
    //  dans l'ordre d'execution est une affaire de projet, pas d'arbre. Il se
    //  contente de reconnaitre le geste, de dessiner ou ca tomberait, et de
    //  demander. C'est `canDrag` / `canDrop` qui donnent leur sens aux deux, et
    //  ils sont fournis par l'ecran.
    //
    //  SANS `canDrop`, RIEN N'EST DEPLACABLE. Un arbre ou tout se traine par
    //  defaut est un arbre ou on deplace par accident - et dans un projet
    //  automate, un deplacement accidentel change l'ordre d'execution.
    enum class DropWhere : std::uint8_t { Before, After, Into };

    void setDragPredicate(std::function<bool(NodeId)> canDrag) { canDrag_ = std::move(canDrag); }
    void setDropPredicate(std::function<bool(NodeId dragged, NodeId target, DropWhere)> canDrop) {
        canDrop_ = std::move(canDrop);
    }
    const core::SignalPtr<NodeId, NodeId, DropWhere> dropped =
        core::Signal<NodeId, NodeId, DropWhere>::create();
    // Lot 21 : les predicats en place (l'ecran compose les siens avec ceux de
    // l'ordre d'execution) ; les noeuds traines - la selection quand le noeud
    // saisi en fait partie, sinon lui seul (un depot les deplace tous).
    [[nodiscard]] const std::function<bool(NodeId)>& dragPredicate() const noexcept { return canDrag_; }
    [[nodiscard]] const std::function<bool(NodeId, NodeId, DropWhere)>& dropPredicate() const noexcept { return canDrop_; }
    [[nodiscard]] const std::vector<NodeId>& draggedNodes() const noexcept { return dragNodes_; }
    // Lot API 3 : un glisser qui SORT de l'arbre (une variable lachee sur une
    // table d'animation). L'arbre ne recoit plus la souris une fois dehors :
    // l'ecran regarde si un glisser est en cours, depose lui-meme, et remet
    // l'arbre au repos.
    [[nodiscard]] bool dragInProgress() const noexcept { return dragging_ && pressed_ != kInvalidNode; }
    [[nodiscard]] NodeId dragSource() const noexcept { return pressed_; }
    void cancelDrag();

    // ---- Lot API 8 : l'arbre du projet ----
    //  Le filtre de l'arbre : les lettres trouvees surlignees (comme dans les
    //  tableaux, ui::SearchQuery) ; les noeuds deplies lus et remis d'un coup
    //  (le filtre deplie ce qu'il faut, puis rend l'arbre comme il etait).
    void setHighlight(std::string_view query);
    [[nodiscard]] const std::vector<NodeId>& expandedNodes() const noexcept { return expanded_; }
    void setExpandedNodes(std::vector<NodeId> nodes);
    // La profondeur d'une ligne montree (0 : la premiere) ; -1 : pas montree.
    [[nodiscard]] int visibleDepth(NodeId n) const noexcept;
    // Les boutons d'une ligne (CellStyle::chips) : un clic gauche dessus emet
    // (noeud, rang du bouton) ; la ligne n'est alors ni choisie ni ouverte.
    // chipRect : ou est le bouton a l'ecran (au dernier dessin) ; faux sinon.
    const core::SignalPtr<NodeId, std::size_t> chipClicked = core::Signal<NodeId, std::size_t>::create();
    [[nodiscard]] bool chipRect(NodeId n, std::size_t chip, gfx::Rect& out) const;
    // 2e partie. La densite : la hauteur des lignes (0 : celle du theme).
    void setRowHeightOverride(float h) { rowOverride_ = h; invalidate(); }
    [[nodiscard]] float rowHeightOverride() const noexcept { return rowOverride_; }
    // Les actions au survol d'une ligne (Epingler, Detacher, "..."), a droite :
    // un clic emet (noeud, rang) ; la ligne n'est alors ni choisie ni ouverte.
    // Vide : aucune. Le titre colle en haut (domainHead) : stickyNode.
    void setHoverActions(std::vector<CellStyle::Chip> actions) { hoverActions_ = std::move(actions); invalidate(); }
    // 1.11.22 : L'EXPLORATEUR MODERNISE (la maquette validee le 09/10) - les compteurs dans
    // une colonne de largeur fixe tout a droite (un nombre gris), les etats du build dans
    // une colonne fixe juste avant (alignes d'une ligne a l'autre), les actions au survol
    // AVANT les etats (le compteur reste lisible), les etiquettes (CellStyle::tags) apres le
    // nom, la fin grise du texte (mutedFrom), l'icone d'un titre de domaine sur une pastille
    // de sa couleur. Faux (le defaut) : le dessin d'avant, pour les autres arbres.
    void setModernLook(bool on) { modern_ = on; invalidate(); }
    [[nodiscard]] bool modernLook() const noexcept { return modern_; }
    [[nodiscard]] bool hasHoverActions() const noexcept { return !hoverActions_.empty(); }
    const core::SignalPtr<NodeId, std::size_t> hoverActionClicked = core::Signal<NodeId, std::size_t>::create();
    // 1.11.23 : une action au survol d'un titre de domaine (CellStyle::headActions) ; son cadre.
    const core::SignalPtr<NodeId, std::size_t> headActionClicked = core::Signal<NodeId, std::size_t>::create();
    [[nodiscard]] bool headActionRect(NodeId n, std::size_t action, gfx::Rect& out) const;
    [[nodiscard]] NodeId stickyNode() const noexcept { return stickyRow_ >= 0 && static_cast<std::size_t>(stickyRow_) < rows_.size() ? rows_[static_cast<std::size_t>(stickyRow_)].node : kInvalidNode; }
    // "Suivre l'onglet actif" : ce noeud devient le noeud choisi et se montre,
    // sans rien emettre (rien ne s'ouvre).
    void setCurrentNode(NodeId n) { current_ = n; selection_.assign(1, n); ensureVisible(n); invalidate(); }
    // Les ancetres montres d'une ligne, du plus haut au parent (la carte au
    // survol : le chemin) ; vide : la ligne n'est pas montree. Un seul parcours.
    [[nodiscard]] std::vector<NodeId> visibleAncestors(NodeId n) const;
    // ---- fin Lot API 8 : l'arbre du projet ----

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    // Expanded state flattened into a linear list once per structural change;
    // painting and hit testing are then O(visible rows).
    struct VisualRow { NodeId node; std::uint16_t depth; bool expandable; bool expanded; };
    void rebuildVisibleRows();
    [[nodiscard]] int rowAt(float localY) const;

    IconPainter                  iconPainter_;       // lot API 2
    NodeTooltip                  nodeTooltip_;       // lot 7 : l'infobulle d'un noeud
    std::shared_ptr<ITreeModel>  model_;
    std::vector<VisualRow>       rows_;
    std::vector<NodeId>          expanded_;      // sorted; binary-searched
    std::vector<NodeId>          selection_;
    NodeId                       current_{kInvalidNode};
    std::function<bool(NodeId)>  filter_;
    SelectionMode                selectionMode_{SelectionMode::Single};
    float                        scrollY_{0.f};
    EdgeScrollBar                vbar_;               // 1.11.4 : la barre qu'on tire
    // Geometry the event handlers need but cannot look up: cached each paint.
    mutable float                rowHeight_{24.f};
    mutable float                indent_{20.f};
    bool                         showRoot_{true};
    core::ConnectionScope        modelConnections_;

    // ---- l'etat du glisser-deposer -----------------------------------------
    // `pressed_` retient ce qu'on a saisi sans encore trainer : un clic est un
    // clic tant que la souris n'a pas franchi le seuil. Sans ce seuil, un clic
    // avec un doigt qui tremble devient un deplacement.
    std::function<bool(NodeId)> canDrag_;
    std::function<bool(NodeId, NodeId, DropWhere)> canDrop_;
    NodeId    pressed_{kInvalidNode};
    gfx::Point pressedAt_{};
    bool      dragging_{false};
    NodeId    dropTarget_{kInvalidNode};
    DropWhere dropWhere_{DropWhere::Before};
    bool      dropAllowed_{false};
    std::vector<NodeId> dragNodes_;        // lot 21 : ce qu'on traine (plusieurs noeuds choisis)
    NodeId    deferSelect_{kInvalidNode};  // lot 21 : un clic sur la selection la reduit au relacher

    // Le survol, et le depliage anime : les enfants d'un noeud qu'on vient
    // d'ouvrir apparaissent en fondu plutot que d'un coup. L'heure de depart
    // est prise a la premiere image qui les peint - le widget n'a pas d'horloge.
    int       hoverRow_{-1};
    NodeId    animNode_{kInvalidNode};
    double    animStart_{-1.0};
    // ensureVisible() demande avant la premiere mise en page : la vue n'a pas
    // encore de hauteur, et le calcul poussait la ligne juste AU-DESSUS du
    // haut. On retient la demande, et onLayout() la rejoue.
    NodeId    pendingVisible_{kInvalidNode};
    SearchQuery treeHighlight_;          // lot API 8 : l'arbre du projet (le filtre)
    struct ChipHit { NodeId node; std::size_t chip; gfx::Rect rect; std::string label; };
    mutable std::vector<ChipHit> chipHits_;   // lot API 8 : l'arbre du projet (les boutons dessines)
    // ---- Lot API 8 : l'arbre du projet (2e partie) ----
    float                        rowOverride_{0.f};          // la densite
    std::vector<CellStyle::Chip> hoverActions_;              // les actions au survol
    mutable std::vector<ChipHit> actionHits_;                // ... dessinees
    mutable std::vector<ChipHit> headHits_;                  // 1.11.23 : les actions d'un titre de domaine
    mutable std::vector<ChipHit> dotHits_;                   // les points (leur infobulle)
    mutable int                  stickyRow_{-1};             // le titre colle en haut
    mutable float                stickyY_{0.f};
    mutable bool                 hasHeads_{false};           // des titres de domaine : ensureVisible garde une ligne pour eux
    // ---- fin Lot API 8 ----
    bool                         modern_{false};             // 1.11.22 : l'explorateur modernise
};

// ============================================================= TableView ====
class ITableModel {
public:
    virtual ~ITableModel() = default;

    [[nodiscard]] virtual std::size_t rowCount() const = 0;
    [[nodiscard]] virtual std::size_t columnCount() const = 0;
    [[nodiscard]] virtual std::string headerText(std::size_t col) const = 0;
    [[nodiscard]] virtual std::string cellText(RowIndex row, std::size_t col) const = 0;
    [[nodiscard]] virtual CellStyle   cellStyle(RowIndex, std::size_t) const { return {}; }

    // Comparison lives in the model: sorting "%MW1174" as an address is not the
    // same as sorting it as a string, and only the model knows which it is.
    [[nodiscard]] virtual bool less(RowIndex a, RowIndex b, std::size_t col) const = 0;

    // Optional in-place editing (Retain column checkbox, comment cell).
    [[nodiscard]] virtual bool editable(RowIndex, std::size_t) const { return false; }
    virtual bool setCellText(RowIndex, std::size_t, std::string_view) { return false; }
    // Lot 16 : une case editable qui se choisit dans une liste (vide : un champ).
    [[nodiscard]] virtual std::vector<std::string> cellChoices(RowIndex, std::size_t) const { return {}; }
    // Lot 16 : l'infobulle d'une ligne, au survol (vide : celle de la table).
    [[nodiscard]] virtual std::string rowTooltip(RowIndex) const { return {}; }
    // Lot 16 : glisser la ligne `from` sur la ligne `to` (lignes du modele) est-il
    // permis ? `to` = kNoRow : sous les lignes (a la racine). Faux : pas de depot.
    [[nodiscard]] virtual bool canDrop(RowIndex /*from*/, RowIndex /*to*/) const { return false; }
    // Lot 21 : glisser PLUSIEURS lignes (`from` : lignes du modele, dans l'ordre
    // de la vue) avant, dans ou apres la ligne `to` (kNoRow : sous les lignes,
    // "dans" la racine). Par defaut : une seule ligne, dans (canDrop).
    [[nodiscard]] virtual bool canDropRows(const std::vector<RowIndex>& from, RowIndex to, TreeView::DropWhere where) const {
        return from.size() == 1 && where == TreeView::DropWhere::Into && canDrop(from.front(), to);
    }

    const core::SignalPtr<> modelReset = core::Signal<>::create();
};

// Composable filter chain: a text term, per-column terms, and predicates such
// as "unused only". Each stage narrows the previous index vector.
class FilterChain {
public:
    // Lot recherche : une recherche (ui::SearchQuery) - chaque mot dans l'une des
    // colonnes, "une phrase", -mot exclu, sans casse ni accents.
    void setGlobalTerm(std::string term);
    [[nodiscard]] const std::string& globalTerm() const noexcept;
    void setColumnTerm(std::size_t col, std::string term);
    void addPredicate(std::string key, std::function<bool(RowIndex)> p);
    void removePredicate(std::string_view key);
    void clear();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::vector<RowIndex> apply(const ITableModel&) const;
private:
    struct Named { std::string key; std::function<bool(RowIndex)> fn; };
    std::string                              global_;
    std::vector<std::pair<std::size_t, std::string>> columnTerms_;
    std::vector<Named>                       predicates_;
};

// ---- lot recherche : LE FILTRE D'UNE COLONNE ------------------------------------
//  Une condition sur le texte de la case (contient, commence par, =, different
//  de, vide, non vide, entre a et b pour un nombre) ET/OU une liste de valeurs
//  (celles cochees - Only - ou toutes sauf celles decochees - Except). Sans
//  casse ni accents ; "vide" accepte aussi le tiret que montrent les cases sans
//  valeur. Plusieurs colonnes filtrees : toutes doivent accepter (ET).
struct ColumnFilter {
    enum class Op : std::uint8_t { None, Contains, StartsWith, Equals, NotEquals, Empty, NotEmpty, Between };
    enum class List : std::uint8_t { None, Only, Except };
    std::size_t              column{0};
    Op                       op{Op::None};
    std::string              value, value2;    // Between : les bornes (vide : ouverte)
    List                     list{List::None};
    std::vector<std::string> values;           // Only : les valeurs gardees ; Except : les ecartees

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool accepts(std::string_view cell) const;
    // "Type = BOOL", "Commentaire contient pompe", "Adresse non vide",
    // "Utilisee entre 1 et 5", "Type : 3 valeurs"...
    [[nodiscard]] std::string label(std::string_view columnTitle) const;
    // Le libelle d'une condition ("contient", "commence par", "=", "diff\xC3\xA9rent de"...).
    [[nodiscard]] static std::string opLabel(Op op);
    // Toutes les colonnes filtrees acceptent-elles ? `cell(col)` : le texte de la
    // case ; `skipColumn` : une colonne a ignorer (sa liste de valeurs se calcule
    // sur les lignes que les AUTRES filtres gardent) ; -1 : aucune.
    [[nodiscard]] static bool acceptsAll(const std::vector<ColumnFilter>& filters,
                                         const std::function<std::string(std::size_t)>& cell, int skipColumn = -1);
    // Un nombre lu dans une case ("12", "-3,5", "1 234", "12 ms" : 12) ; faux sinon.
    [[nodiscard]] static bool parseNumber(std::string_view text, double& out);
};

class ColumnFilterPopup;

class TableView : public Widget {
public:
    struct Column {
        std::string title;
        float       width{120.f};
        float       minWidth{40.f};
        bool        resizable{true};
        bool        sortable{true};
        bool        visible{true};
        Align       align{Align::Start};
        int         headerIcon{0};      // lot 21 : une icone de l'hote (setIconPainter) a la place du titre
        bool        filterable{true};   // lot recherche : l'entonnoir (setColumnFiltersEnabled)
    };

    explicit TableView(std::string id = {});

    void setModel(std::shared_ptr<ITableModel> m);
    void setColumns(std::vector<Column> cols);
    void setSelectionMode(SelectionMode m) { selectionMode_ = m; }
    void setAlternatingRowColors(bool a) { altRows_ = a; invalidate(); }
    void setFrozenColumns(std::size_t n) { frozen_ = n; invalidateLayout(); }

    void sortBy(std::size_t column, SortOrder order);
    void setFilter(FilterChain chain);                 // rebuilds the index vector
    // Lot API 7 : le defilement vertical, lu et remis (une table qui se refait
    // quand on deplie une ligne garde sa place au lieu de remonter en haut).
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }
    [[nodiscard]] float scrollOffsetX() const noexcept { return scrollX_; }   // 1.11.4
    void setScrollOffset(float y);
    void autoSizeColumn(std::size_t col);              // samples visible rows only

    [[nodiscard]] std::size_t visibleRowCount() const noexcept { return view_.size(); }
    // View row -> model row. Exporters iterate this so the file matches the
    // sort and filter the user is actually looking at.
    [[nodiscard]] RowIndex viewRow(std::size_t i) const { return view_[i]; }
    [[nodiscard]] const std::vector<RowIndex>& selection() const noexcept { return selection_; }
    [[nodiscard]] std::vector<RowIndex> selectedModelRows() const;
    // Choisir des lignes (du modele) par programme : sans clic, donc sans voler
    // le focus a un champ en cours de saisie. La premiere est amenee en vue.
    void selectModelRows(std::vector<RowIndex> rows, bool notify = true);

    // Pour les scripts de capture et les tests : le modele, et la ligne de VUE
    // `i` a l'ecran (false si elle est hors de la zone visible).
    [[nodiscard]] const std::shared_ptr<ITableModel>& model() const noexcept { return model_; }
    [[nodiscard]] bool rowRect(std::size_t viewRowIndex, gfx::Rect& out) const;
    // La ligne de VUE sous la souris (-1 : aucune) - un apercu au survol.
    [[nodiscard]] int hoveredRow() const noexcept { return hoverRow_; }
    // Lot 7 : l'infobulle de la ligne survolee (ITableModel::rowTooltip) est
    // relue tant qu'elle est ouverte : une valeur qui change se voit sans
    // bouger la souris. Une case refusee au collage garde sa raison.
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

    const core::SignalPtr<RowIndex>              activated        = core::Signal<RowIndex>::create();
    const core::SignalPtr<const std::vector<RowIndex>&> selectionChanged =
        core::Signal<const std::vector<RowIndex>&>::create();
    // 1.9 : un clic sur la case a cocher d'une ligne (CellStyle::check) : la
    // ligne du modele. La table ne change rien : l'hote coche ou decoche.
    const core::SignalPtr<RowIndex>              checkClicked     = core::Signal<RowIndex>::create();
    // La case a cocher de la ligne de VUE `i` a l'ecran (tests, scripts) ; false
    // si la ligne n'en a pas ou n'est pas visible.
    [[nodiscard]] bool checkRect(std::size_t viewRowIndex, gfx::Rect& out) const;

    // ---- lot 16 : l'edition dans la case, et la table en arbre ---------------
    //  Double-clic (ou F2 sur la ligne choisie) sur une case que le modele dit
    //  editable : un champ, ou une liste deroulee (cellChoices). Entree ou un clic
    //  ailleurs valide, Echap annule ; le modele recoit setCellText, et
    //  cellEdited dit (ligne du modele, colonne, texte, accepte).
    bool beginCellEdit(RowIndex modelRow, std::size_t col);
    void finishCellEdit(bool commit, const std::string& text);
    [[nodiscard]] bool      cellEditing() const noexcept { return cellEditor_ != nullptr; }
    [[nodiscard]] InputText* activeCellField() const noexcept;
    [[nodiscard]] DropDown*  activeCellList() const noexcept;
    [[nodiscard]] RowIndex   editedRow() const noexcept { return editRow_; }
    [[nodiscard]] std::size_t editedColumn() const noexcept { return editCol_; }
    // La case (ligne de VUE, colonne) a l'ecran ; faux hors de la zone visible.
    [[nodiscard]] bool cellRect(std::size_t viewRowIndex, std::size_t col, gfx::Rect& out) const;
    // La fleche de cette ligne de VUE (une table en arbre) : ou cliquer.
    [[nodiscard]] bool expanderRect(std::size_t viewRowIndex, gfx::Rect& out) const;
    const core::SignalPtr<RowIndex, std::size_t, const std::string&, bool> cellEdited =
        core::Signal<RowIndex, std::size_t, const std::string&, bool>::create();
    const core::SignalPtr<RowIndex> expanderClicked = core::Signal<RowIndex>::create();
    // Lot 16 : glisser une ligne sur une autre (ranger une variable dans un
    // dossier) : quand le modele le permet (canDrop), rowDropped(tiree, visee) ;
    // visee = kNoRow : lachee sous les lignes.
    static constexpr RowIndex kNoRow = static_cast<RowIndex>(-1);
    void setRowDragEnabled(bool on) noexcept { rowDrag_ = on; }
    [[nodiscard]] bool rowDragging() const noexcept { return dragActive_; }
    const core::SignalPtr<RowIndex, RowIndex> rowDropped = core::Signal<RowIndex, RowIndex>::create();
    // Lot 21 : PLUSIEURS LIGNES, ET ENTRE DEUX LIGNES. Saisir une ligne de la
    // selection les traine toutes ; lachees sur une ligne (dans), ou entre deux
    // (avant / apres : reordonner) - le modele dit ce qui est permis
    // (canDropRows), et le quart haut / bas d'une ligne propose avant / apres.
    // rowsDropped(tirees, visee, ou) ; rowDropped suit encore pour une ligne
    // lachee dans une autre.
    using DropWhere = TreeView::DropWhere;
    const core::SignalPtr<const std::vector<RowIndex>&, RowIndex, DropWhere> rowsDropped =
        core::Signal<const std::vector<RowIndex>&, RowIndex, DropWhere>::create();
    [[nodiscard]] const std::vector<RowIndex>& draggedRows() const noexcept { return dragRows_; }
    [[nodiscard]] DropWhere dropWhere() const noexcept { return dropWhere_; }
    // Lot 21 : les icones que la table ne connait pas (CellStyle::customIcon,
    // Column::headerIcon) : l'hote les dessine.
    using IconPainter = std::function<void(gfx::IRenderer&, int icon, const gfx::Rect& box, gfx::Color color)>;
    void setIconPainter(IconPainter painter) { iconPainter_ = std::move(painter); }

    // ---- lot 20 : copier, coller (Excel) ---------------------------------------
    //  Ctrl+C : les lignes choisies, dans l'ordre de la vue, colonnes visibles,
    //  AVEC leurs titres - separees par des tabulations, ce qu'Excel colle en
    //  cases. Ctrl+Maj+C : sans les titres. Le clic droit propose les memes
    //  gestes. Toutes les tables savent copier.
    //  Ctrl+V (Ctrl+Maj+V : en nouvelles lignes) : la table passe le texte du
    //  presse-papiers a son volet (setPasteHandler), qui sait ce qu'une ligne
    //  veut dire ; une table sans volet ne colle pas.
    [[nodiscard]] std::string copyText(bool withTitles) const;
    std::size_t copySelection(bool withTitles);          // rend le nombre de lignes copiees
    struct PasteRequest {
        std::string text;
        std::size_t anchorViewRow{0};    // la ligne de VUE choisie ; visibleRowCount() : aucune
        int         anchorColumn{-1};    // la colonne du dernier clic ; -1 : aucune
        bool        asNewRows{false};
    };
    void setPasteHandler(std::function<void(const PasteRequest&)> h) { pasteHandler_ = std::move(h); }
    [[nodiscard]] bool pasteEnabled() const noexcept { return static_cast<bool>(pasteHandler_); }
    bool pasteFromClipboard(bool asNewRows);
    // ---- Lot API 8 : glisser un classeur dans la fenetre ----
    //  Le meme collage que Ctrl+V (le gestionnaire du volet : sa cible, un seul
    //  Ctrl+Z, le bandeau), avec ce texte au lieu du presse-papiers - qui n'est
    //  pas touche. Des titres reconnus : chaque colonne va dans la sienne. Faux :
    //  pas de collage dans cette table, ou rien a coller.
    bool pasteText(std::string text, bool asNewRows);
    // ---- fin Lot API 8 ----
    [[nodiscard]] int lastClickedColumn() const noexcept { return lastClickColumn_; }
    void setLastClickedColumn(int c) noexcept { lastClickColumn_ = c; }
    // CE QUE LE COLLAGE A FAIT : un bandeau au-dessus des titres ; une barre au
    // bord des lignes creees (verte) ou mises a jour (bleue) ; un cadre rouge
    // sur les cases refusees, la raison en infobulle. Une ligne est reperee par
    // le texte de sa colonne `keyColumn` (le nom ; -1 : la premiere visible) :
    // la marque survit au rafraichissement du volet. La croix du bandeau, ou
    // Echap dans la table, efface tout (clearPasteResult).
    enum class MarkKind : std::uint8_t { Created, Updated, Refused };
    struct Mark { std::string key; int column{-1}; MarkKind kind{MarkKind::Updated}; std::string why; };
    void setPasteResult(std::string banner, std::vector<Mark> marks, int keyColumn = -1);
    void clearPasteResult();
    [[nodiscard]] const std::string& pasteBanner() const noexcept { return banner_; }
    [[nodiscard]] const std::vector<Mark>& pasteMarks() const noexcept { return marks_; }
    // Le menu du clic droit : Copier, Copier sans les titres, Coller, Coller en
    // nouvelles lignes, Tout choisir (les scripts le choisissent par libelle).
    [[nodiscard]] PopupMenu* contextMenu() const noexcept { return context_; }
    void openContextMenu(gfx::Point at);
    // 1.11.14 : DES ENTREES EN PLUS, en tete de ce menu (la Console : aller a la source,
    // filtrer sur cette source...). Leurs id (1000 et plus) reviennent par contextAction.
    void setExtraContextItems(std::function<std::vector<PopupMenu::Item>()> provider) { extraContext_ = std::move(provider); }
    const core::SignalPtr<int> contextAction = core::Signal<int>::create();
    // (lignes copiees, avec les titres)
    const core::SignalPtr<std::size_t, bool> copied = core::Signal<std::size_t, bool>::create();

    // ---- lot recherche : LA RECHERCHE SURLIGNEE ----------------------------------
    //  Les termes d'une recherche (SearchQuery : mots, "phrases", -exclus) sont
    //  surlignes dans les cases montrees - le commentaire qui a fait garder la
    //  ligne se voit. Vide : rien.
    void setHighlight(std::string_view query);
    [[nodiscard]] const SearchQuery& highlight() const noexcept { return highlight_; }

    // ---- lot recherche : LES FILTRES PAR COLONNE ---------------------------------
    //  setColumnFiltersEnabled : un entonnoir dans le titre de chaque colonne
    //  filtrable (Column::filterable) ; un clic ouvre la fenetre du filtre - une
    //  condition, la liste des valeurs de la colonne (200 au plus, avec sa propre
    //  recherche), Appliquer / Effacer. Les filtres actifs : une bande au-dessus
    //  des titres, une pastille chacun ("Type = BOOL" et sa croix), "+ Filtre",
    //  "37 sur 251" et "Tout effacer". Ils s'ajoutent (ET) a setFilter.
    //  UNE TABLE EN ARBRE (des lignes en retrait sous d'autres, CellStyle::indent) :
    //  les filtres choisissent les lignes du premier niveau, leurs enfants les
    //  suivent ; un titre de groupe (spanRow) reste s'il garde une ligne.
    //  ColumnFilterMode::Host : la table NE FILTRE PAS - le volet qui fabrique
    //  ses lignes applique columnFilters() (ColumnFilter::acceptsAll) quand
    //  columnFiltersChanged part, donne les valeurs d'une colonne
    //  (setColumnValuesProvider) et ses nombres (setColumnFilterCounts).
    enum class ColumnFilterMode : std::uint8_t { Table, Host };
    void setColumnFiltersEnabled(bool on);
    [[nodiscard]] bool columnFiltersEnabled() const noexcept { return filtersEnabled_; }
    void setColumnFilterMode(ColumnFilterMode mode);
    [[nodiscard]] ColumnFilterMode columnFilterMode() const noexcept { return filterMode_; }
    [[nodiscard]] const std::vector<ColumnFilter>& columnFilters() const noexcept { return columnFilters_; }
    [[nodiscard]] const ColumnFilter* columnFilter(std::size_t column) const noexcept;
    void setColumnFilter(ColumnFilter filter);          // remplace celui de sa colonne ; inactif : l'enleve
    void removeColumnFilter(std::size_t column);
    void clearColumnFilters();
    // La ligne du modele passe-t-elle les filtres des colonnes (ses cases a elle) ?
    [[nodiscard]] bool columnFiltersAccept(RowIndex modelRow, int skipColumn = -1) const;
    // Host : les textes d'une colonne, un par element (les doublons comptent ;
    // `emit` pour chacun). Sans : la table les lit dans son modele.
    using ColumnValues = std::function<void(std::size_t column, const std::function<void(const std::string&)>& emit)>;
    void setColumnValuesProvider(ColumnValues provider) { valuesProvider_ = std::move(provider); }
    void setColumnFilterCounts(std::size_t shown, std::size_t total);
    // Les valeurs distinctes d'une colonne et combien de fois (triees ; 200 au
    // plus, `more` : il y en a d'autres), celles des lignes que les autres filtres gardent.
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> columnValues(std::size_t column, bool* more = nullptr) const;
    // La fenetre du filtre d'une colonne (l'entonnoir, "+ Filtre", les scripts).
    bool openColumnFilter(std::size_t column);
    [[nodiscard]] ColumnFilterPopup* columnFilterPopup() const noexcept { return filterPopup_; }
    // Pour les scripts : l'entonnoir d'une colonne, la pastille i, "+ Filtre".
    [[nodiscard]] gfx::Rect filterIconRect(std::size_t column) const;
    [[nodiscard]] gfx::Rect filterChipRect(std::size_t index) const;
    [[nodiscard]] gfx::Rect addFilterRect() const;
    // La colonne dont le titre commence ainsi (sans casse ni accents) ; -1 : aucune.
    [[nodiscard]] int columnByTitle(std::string_view title) const;
    [[nodiscard]] const std::vector<Column>& columns() const noexcept { return columns_; }
    const core::SignalPtr<> columnFiltersChanged = core::Signal<>::create();

    // ---- Lot API 8 : les filtres retenus d'une seance a l'autre ----
    //  Les filtres des colonnes sont retenus par table (son id) et par projet
    //  (ui::FilterMemory, que l'application branche) : relus quand l'entonnoir
    //  s'allume et que les colonnes sont la - avant le premier dessin -, ecrits
    //  a chaque changement ; "Tout effacer" les efface et les oublie. Un filtre
    //  dont la colonne n'existe plus (renommee) est ignore sans bruit. Faux :
    //  cette table ne retient rien (a dire avant setColumnFiltersEnabled).
    void setRemembersColumnFilters(bool on) noexcept { remembers_ = on; }
    [[nodiscard]] bool remembersColumnFilters() const noexcept { return remembers_; }
    void recallColumnFilters();            // relire (et poser) ce qui est retenu - un autre projet
    // ---- fin Lot API 8 ----

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    void rebuildView();                 // filter -> sort -> view_
    void revealSelection();             // la premiere ligne choisie en vue (au placement si pas encore placee)
    [[nodiscard]] int columnAtX(float localX) const;
    [[nodiscard]] std::size_t firstVisibleColumn() const noexcept;
    // 1.11.4 : ou se posent les deux barres (le corps sous les titres, chacune laissant
    // le coin a l'autre), et ce qui defile : la largeur des colonnes, la hauteur des lignes.
    void barAreas(gfx::Rect& vArea, gfx::Rect& hArea, float& contentW, float& contentH) const;
    std::string  tableTooltip_;         // lot 16 : l'infobulle de la table, sous celles des lignes
    bool         rowTooltipShown_{false};
    bool         revealPending_{false};
    // Lot 16 : le glisser d'une ligne.
    bool         rowDrag_{false}, dragArmed_{false}, dragActive_{false};
    RowIndex     dragFrom_{0}, dropTo_{0};
    bool         dropValid_{false};
    std::vector<RowIndex> dragRows_;     // lot 21 : les lignes trainees (du modele, ordre de la vue)
    DropWhere    dropWhere_{DropWhere::Into};
    bool         deferSelect_{false};    // lot 21 : un clic sur la selection la reduit au relacher
    RowIndex     deferRow_{0};
    IconPainter  iconPainter_;
    gfx::Point   dragOrigin_{}, dragPos_{};
    WidgetPtr    activeCellEditor_;     // un champ ferme, detruit au dessin suivant
    Widget*      cellEditor_{nullptr};
    bool         cellIsList_{false};
    RowIndex     editRow_{0};
    std::size_t  editCol_{0};

    std::shared_ptr<ITableModel> model_;
    std::vector<Column>          columns_;
    std::vector<RowIndex>        view_;        // view row -> model row
    std::vector<RowIndex>        selection_;   // model rows, stable across sorting
    FilterChain                  filter_;
    std::size_t                  sortColumn_{0}, frozen_{0};
    SortOrder                    sortOrder_{SortOrder::None};
    SelectionMode                selectionMode_{SelectionMode::Extended};
    bool                         altRows_{true};
    int                          resizingColumn_{-1};
    float                        resizeOrigin_{0.f};
    float                        scrollX_{0.f}, scrollY_{0.f};
    EdgeScrollBar                vbar_, hbar_{true};  // 1.11.4 : les barres qu'on tire
    mutable float                rowHeight_{24.f}, headerHeight_{28.f};
    float                        titleHeight_{28.f};   // lot 20 : les titres seuls (headerHeight_ = titres + bandeau)
    core::ConnectionScope        modelConnections_;
    int                          hoverRow_{-1};    // ligne de VUE sous la souris
    // Lot 20 : copier, coller.
    [[nodiscard]] const Mark* markOf(RowIndex row, int column, MarkKind kind) const;
    [[nodiscard]] float bannerHeight() const noexcept { return banner_.empty() ? 0.f : 28.f; }
    std::function<void(const PasteRequest&)> pasteHandler_;
    PopupMenu*                   context_{nullptr};
    std::function<std::vector<PopupMenu::Item>()> extraContext_;   // 1.11.14
    int                          lastClickColumn_{-1};
    std::string                  banner_;
    std::vector<Mark>            marks_;
    int                          markKeyColumn_{-1};
    int                          hoverCol_{-1};    // la colonne sous la souris (l'infobulle d'une case refusee)
    bool                         cellTipShown_{false};
    gfx::Size                    surface_{};       // la fenetre (le menu reste a l'ecran)
    core::ConnectionScope        contextLinks_;

    // ---- lot recherche : la recherche surlignee, les filtres par colonne (TableFilters.cpp) ----
    [[nodiscard]] float stripHeight() const noexcept;
    [[nodiscard]] float headerStripTop() const noexcept;      // le haut de la bande des filtres
    // Les lignes que gardent les filtres des colonnes (Table), dans l'ordre du
    // modele ; compte les lignes du premier niveau gardees et en tout.
    [[nodiscard]] std::vector<RowIndex> applyColumnFilters(const std::vector<RowIndex>& rows, std::size_t& shown,
                                                           std::size_t& total) const;
    [[nodiscard]] gfx::Rect headerCellRect(std::size_t column) const;
    void paintFilterStrip(const PaintContext& ctx, const gfx::Rect& band) const;
    void paintHighlights(const PaintContext& ctx, std::string_view text, float x, float y, float h, float maxWidth,
                         gfx::FontId font) const;
    bool filterStripClick(gfx::Point p);                      // vrai : le clic etait pour la bande
    void openAddFilterMenu(gfx::Point at);
    void columnFiltersEdited();                                // la vue refaite, le signal emis
    SearchQuery                  highlight_;
    bool                         filtersEnabled_{false};
    ColumnFilterMode             filterMode_{ColumnFilterMode::Table};
    std::vector<ColumnFilter>    columnFilters_;
    ColumnValues                 valuesProvider_;
    std::size_t                  filterShown_{0}, filterTotal_{0};   // "37 sur 251"
    ColumnFilterPopup*           filterPopup_{nullptr};
    core::ConnectionScope        filterLinks_;
    // ---- Lot API 8 : les filtres retenus (FilterMemory.cpp, TableFilters.cpp) ----
    void rememberColumnFilters();          // ecrire ce qui est montre (chaque changement)
    bool                         remembers_{true};
    bool                         memoryPending_{false};   // allume, mais pas encore de colonnes : relu a setColumns
    bool                         memoryRestoring_{false}; // une relecture n'ecrit rien
    std::shared_ptr<void>        memoryToken_;            // inscrite aupres de FilterMemory (un autre projet : relire)
    // ---- fin Lot API 8 ----
};

// ========================================================== PropertyGrid ====
// Name/value pairs in collapsible categories. Values are edited through typed
// editors resolved by a factory, so adding a new property type does not touch
// the grid.
class PropertyGrid : public Widget {
public:
    enum class ValueType : std::uint8_t {
        Text, Integer, Real, Boolean, Enum, Address, Color, ReadOnly
    };

    // 1.11.3 : le carre de legende d'une case (voir Property::legend).
    enum class LegendStyle : std::uint8_t { Constant, Formula, Markers, Api, Hmi, System, Local, Error, Empty };
    struct Legend {
        LegendStyle              style{LegendStyle::Constant};
        std::string              text;      // "C", "fx", "$", "A", "I", "S", "V", "!"
        std::vector<LegendStyle> dots{};    // une formule : les zones qu'elle lit (des points sous la lettre)
        std::string              tip{};     // l'infobulle du carre
        std::string              expected{};   // le type que la case attend (le selecteur filtre dessus)
    };

    struct Property {
        std::string  name;
        std::string  value;
        ValueType    type{ValueType::ReadOnly};
        std::string  description;             // shown in the help strip
        std::vector<std::string> enumValues;
        std::function<bool(std::string_view)> commit;   // null => read-only
        std::string  key{};           // 1.12.1 : la cle du modele, quand l'hote en a une ("align") ; vide : name
        // ---- Lot API 8 : les expressions impossibles ----
        // Non vide : la valeur est pilotee par cette expression - pastille "fx"
        // pleine, case teintee et barre d'accent, l'expression en chasse fixe.
        std::string  expression{};
        std::string  exprError{};     // non vide : elle ne peut pas marcher (pastille rouge, souligne rouge)
        std::string  exprValue{};     // sa valeur pendant la simulation (infobulle) ; vide : aucune
        // ---- 1.9 : l'esclave simule lie ----
        // REPRISE D'AILLEURS (le vrai appareil la donne a son esclave) : en lecture
        // seule, le nom estompe, un petit cadenas a droite de la valeur (lockTint :
        // sa couleur ; rien : celle du texte estompe).
        bool                      locked{false};
        std::optional<gfx::Color> lockTint{};
        // La couleur de la valeur, rendue lisible sur le fond (Theme::onSurface) ;
        // un Tone passe avant. Rien : celle de toujours.
        Tone                      valueTone{Tone::None};
        std::optional<gfx::Color> valueColor{};
        // ---- 1.9 : la valeur SURCHARGEE sur un objet (alarmes des objets) ----
        // Elle remplace, pour cet objet seulement, celle du symbole ou de la
        // bibliotheque : un lisere de 3 px a l'accent clair a gauche de la
        // valeur, un fond accent leger, l'etiquette "SURCHARGE" a droite de la
        // valeur ; l'infobulle ajoute "<overrideFrom> : <overrideDefault>". Avec
        // une expression (ligne pilotee) : la barre et la pastille fx a gauche de
        // la ligne, le lisere et l'etiquette sur la valeur.
        bool                      overridden{false};
        std::string               overrideDefault{};   // la valeur d'origine (infobulle)
        std::string               overrideFrom{};      // "Valeur du symbole Sym_Pompe" ; vide : "Valeur du symbole"
        // Un petit bouton (fleche de retour) au bout de la ligne : revenir a la
        // valeur d'origine ; revertTip : son infobulle. Nul : pas de bouton.
        std::function<void()>     revert{};
        std::string               revertTip{};
        // ---- 1.9 : une PASTILLE DE COULEUR au debut de la case de la valeur
        // (le mode d'un parametre) ; pillOnly : elle tient lieu de valeur.
        std::optional<ColorPill>  pill{};
        bool                      pillOnly{false};
        // 1.9 (chantier U) : le texte d'attente d'une case vide, en gris (la valeur
        // par defaut d'un argument pas donne) ; il ne se tape pas.
        std::string               placeholder{};
        // 1.12.3 : PLUSIEURS OBJETS CHOISIS NE S'ACCORDENT PAS - une case a cocher montre
        // « - » (un clic la coche pour tous) ; une case de texte, son texte d'attente.
        bool                      mixed{false};
        // 1.11.3 : LE CARRE DE LEGENDE au bout de la case - d'ou vient la valeur
        // (C constante, fx formule, $ reperes, A API, I IHM, S systeme, V symbole
        // ou vue, ! erreur). Un clic : legendClicked (l'hote ouvre la liste des
        // carres, puis le selecteur). Rien : pas de carre.
        std::optional<Legend>     legend{};
        // 1.11.7 : UN BOUTON « … » au bout de la case (a gauche du carre de legende) :
        // l'hote ouvre son editeur (le script d'une action, la formule de Maths). Avec
        // openOnClick, un clic sur la case l'ouvre aussi (le choix de l'operation en arbre).
        std::function<void()>     open{};
        std::string               openTip{};
        bool                      openOnClick{false};
    };

    struct Category {
        std::string           name;
        std::vector<Property> properties;
        std::vector<Category> children;       // nested categories
        bool                  expanded{true};
        // 1.9 : une partie teintee de cette couleur (le violet du simule) : son
        // titre et le fond de sa ligne. Rien : comme les autres.
        std::optional<gfx::Color> accent{};
        // 1.9 (chantier U) : une pastille de couleur apres le titre (le mode d'un
        // parametre) ; le titre montre perd la fin "  .  <pastille>".
        std::optional<ColorPill>  pill{};
    };

    explicit PropertyGrid(std::string id = {});
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }   // 1.11.4

    void setCategories(std::vector<Category> cats);
    void clearProperties();
    void setNameColumnRatio(float r);          // splitter between Name and Value
    void setShowDescriptionPane(bool s);
    // L'AIDE A LA SAISIE des cases : l'hote dit, pour une propriete qu'on
    // commence a editer, quelle liste ouvrir sous le champ (une variable, une
    // expression, un texte a trous) ; une fonction vide : aucune. Demande a
    // chaque edition, donc toujours a jour du projet.
    using FieldAssistFor = std::function<InputText::Assist(std::string_view category, const Property&)>;
    void setFieldAssist(FieldAssistFor f) { fieldAssist_ = std::move(f); }
    // Une case Color s'edite dans une PALETTE (nuancier, couleurs recentes, carre
    // de teinte, code #RRGGBB). L'hote peut y ajouter les couleurs deja
    // employees ("Dans le projet"), demandees a chaque ouverture.
    void setPaletteColors(std::function<std::vector<std::string>()> f) { paletteColors_ = std::move(f); }
    // La palette ouverte (tests, scripts de capture) ; nullptr sinon.
    [[nodiscard]] ColorPalette* activePalette() const noexcept;
    // Le champ ouvert (tests, scripts de capture) ; nullptr si aucune case n'est editee.
    [[nodiscard]] InputText* activeField() const noexcept;
    void expandAll();
    void collapseAll();
    // Pour les scripts et les tests : la case VALEUR d'une propriete, a
    // l'ecran (la premiere de ce nom). false si elle n'est pas montree.
    [[nodiscard]] bool valueRect(std::string_view name, gfx::Rect& out) const;
    // Faire defiler jusqu'a cette propriete ; false si elle n'existe pas.
    bool revealValue(std::string_view name);
    // 1.9 : le bouton "revenir a la valeur d'origine" d'une propriete surchargee
    // (Property::revert), a l'ecran (tests, scripts) ; false s'il n'est pas montre.
    [[nodiscard]] bool revertRect(std::string_view name, gfx::Rect& out) const;
    // 1.11.7 : le bouton « … » d'une propriete (Property::open), a l'ecran ; faux : pas montre.
    [[nodiscard]] bool openRect(std::string_view name, gfx::Rect& out) const;
    // Ce que la grille montre (tests, scripts).
    [[nodiscard]] const std::vector<Category>& categories() const noexcept { return categories_; }

    const core::SignalPtr<const std::string&, const std::string&> propertyChanged =
        core::Signal<const std::string&, const std::string&>::create();
    // Une valeur tapee que `commit` a refusee (nom, texte) : l'hote dit pourquoi.
    const core::SignalPtr<const std::string&, const std::string&> propertyRejected =
        core::Signal<const std::string&, const std::string&>::create();
    // 1.11.3 : un clic sur le carre de legende d'une case (categorie, nom, le carre a l'ecran).
    const core::SignalPtr<const std::string&, const std::string&, gfx::Rect> legendClicked =
        core::Signal<const std::string&, const std::string&, gfx::Rect>::create();
    // ... et ou il est (tests, scripts) ; false s'il n'est pas montre.
    [[nodiscard]] bool legendRect(std::string_view name, gfx::Rect& out) const;
    // Le carre repond-il au clic (l'hote sait ouvrir la liste des carres) ? Faux : il
    // se lit seulement (son infobulle).
    void setLegendClickable(bool on) { legendClickable_ = on; }
    [[nodiscard]] bool legendClickable() const noexcept { return legendClickable_; }
    [[nodiscard]] bool editing() const noexcept { return editor_ != nullptr; }
    // ---- Lot API 8 : les expressions impossibles ----
    // L'infobulle d'une propriete pilotee : l'expression, sa valeur en
    // simulation, son erreur. Ailleurs : celle de la grille.
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override;   // la sienne, ou une propriete pilotee montree
    // La valeur actuelle d'une propriete pilotee (la simulation en marche), relue
    // tant que l'infobulle est ouverte ; "" : rien a dire (exprValue sinon).
    void setExprValueProvider(std::function<std::string(const Property&)> f) { exprValueFor_ = std::move(f); }

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    struct VisualRow { const Category* cat; const Property* prop; std::uint16_t depth; };
    void rebuildRows();
    [[nodiscard]] int rowAt(float globalY) const;

    // L'EDITION SUR PLACE. Une case modifiable (commit non nul, type autre que
    // ReadOnly) s'edite d'un clic : un champ pour le texte et les nombres, une
    // liste pour Enum, une bascule immediate pour Boolean. Entree ou quitter le
    // champ valide, Echap annule. Le champ est retrouve par (categorie, nom) :
    // la grille est souvent refaite (setCategories) pendant qu'on tape.
    void beginEdit(std::size_t row);
    void finishEdit(bool commit, const std::string& text);
    [[nodiscard]] int       rowOf(std::string_view category, std::string_view name) const;
    [[nodiscard]] gfx::Rect valueCell(std::size_t row) const;

    std::vector<Category>  categories_;
    std::vector<VisualRow> rows_;
    float                  nameRatio_{0.45f};
    bool                   descriptionPane_{true};
    float                  scrollY_{0.f};
    EdgeScrollBar          vbar_;                     // 1.11.4 : la barre qu'on tire
    mutable float          rowHeight_{24.f};
    WidgetPtr              activeEditor_;      // un champ ferme, detruit au dessin suivant :
                                               // il peut etre en train d'emettre quand on le ferme
    Widget*                editor_{nullptr};   // le champ ouvert (un enfant), ou nullptr
    bool                   editIsList_{false};
    std::string            editCategory_, editName_;
    std::function<bool(std::string_view)> editCommit_;
    FieldAssistFor         fieldAssist_;
    std::function<std::vector<std::string>()> paletteColors_;
    bool                   editIsPalette_{false};
    std::function<std::string(const Property&)> exprValueFor_;   // ---- Lot API 8 : les expressions impossibles ----
    bool                   legendClickable_{false};               // 1.11.3 : le carre de legende repond au clic
};

// 1.11.3 : la couleur d'un carre de legende (le meme partout : la grille, la liste
// des carres, le selecteur de valeur) et son dessin dans `box`.
[[nodiscard]] gfx::Color legendColor(PropertyGrid::LegendStyle, bool dark) noexcept;
void paintLegend(const PaintContext&, gfx::Rect box, const PropertyGrid::Legend&);

} // namespace ui
