// =============================================================================
//  ui/widgets/TabArea.hpp - le centre en plusieurs groupes d'onglets
// -----------------------------------------------------------------------------
//  LOT 7 : L'AFFICHAGE MULTI-FENETRE, AVEC UNE DISPOSITION DYNAMIQUE.
//
//  Un TabControl ne montre qu'une page a la fois ; comparer une section et son
//  grafcet, ou garder la table des variables sous les yeux pendant qu'on lit le
//  code, obligeait a passer d'un onglet a l'autre. La zone des onglets en
//  montre plusieurs, de trois facons :
//
//    * ONGLETS    un seul groupe - exactement l'ancien centre ;
//    * GROUPES    des groupes d'onglets cote a cote ou empiles (un arbre de
//                 divisions), separes par des poignees qui se tirent. On tire
//                 un en-tete sur le bord d'un groupe pour le mettre a cote, au
//                 milieu pour l'y ranger, sur une bande pour l'y inserer ; le
//                 dernier onglet d'un groupe ferme, le groupe s'en va ;
//    * MOSAIQUE   automatique : chaque onglet est une tuile avec sa barre de
//                 titre ; 1 plein cadre, 2 cote a cote, 3 un grand a gauche et
//                 deux empiles, 4 en 2 x 2, 5-6 en 3 x 2, puis une grille. Les
//                 tuiles se replacent a chaque ouverture et fermeture ; un
//                 double clic sur une barre agrandit la tuile (et revient).
//
//  LA MEME INTERFACE QUE TabControl, EN INDICES GLOBAUX : groupe apres groupe,
//  dans l'ordre de l'ecran (de gauche a droite, de haut en bas). Tout le code
//  qui parlait au TabControl du centre (addTab, indexOf, page, takeTab...) lui
//  parle sans changer. addTab ajoute au groupe ACTIF - le dernier ou l'on a
//  clique - et currentIndex() est l'onglet ouvert de ce groupe.
//
//  CE QUI FAIT GLISSER LES INDICES EST ANNONCE. Inserer dans un groupe qui
//  n'est pas le dernier decale ceux des groupes suivants ; deplacer un onglet
//  d'un groupe a l'autre aussi. Le proprietaire qui retient des onglets par
//  leur indice les recale sur tabInserted, tabRemoved et tabMoved (un seul
//  onglet a la fois : toute operation est l'une des trois). Changer de mode ne
//  change pas l'ordre global, donc aucun indice.
//
//  LE GLISSER passe par une couche transparente posee sur les groupes (la
//  derniere enfant) : elle voit l'appui avant eux sans le prendre, et, des
//  qu'un en-tete est tire, elle prend la souris (comme une liste deroulante
//  ouverte) jusqu'au lacher, pour dessiner les zones de depot par-dessus tout.
//  Echap annule. Lache HORS de la zone (assez loin), l'onglet demande a partir
//  dans une fenetre a lui (detachRequested) - si le proprietaire le permet.
// =============================================================================
#pragma once

#include "Containers.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

class TabArea : public Widget {
public:
    using Tab = TabControl::Tab;
    enum class Mode : std::uint8_t { Tabs, Groups, Mosaic };
    enum class Side : std::uint8_t { Left, Right, Top, Bottom };
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    explicit TabArea(std::string id = {});
    ~TabArea() override;

    // ---- l'interface de TabControl, en indices GLOBAUX ----------------------
    std::size_t addTab(Tab meta, WidgetPtr page);
    void        removeTab(std::size_t index);
    [[nodiscard]] WidgetPtr takeTab(std::size_t index);
    void        setCurrentIndex(std::size_t index);
    void        setTabModified(std::size_t index, bool modified);
    void        setTabBadge(std::size_t index, std::string badge, Tone tone = Tone::None);
    void        setTabLive(std::size_t index, bool live);
    void        setTabTitle(std::size_t index, std::string title);
    void        setTabIcon(std::size_t index, Icon icon);          // 1.8.0
    [[nodiscard]] const Tab* tab(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t currentIndex() const noexcept;
    [[nodiscard]] std::size_t tabCount() const noexcept;
    [[nodiscard]] Widget* page(std::size_t index) const noexcept;
    [[nodiscard]] int indexOf(const Widget* page) const noexcept;
    [[nodiscard]] bool headerRect(std::size_t index, gfx::Rect& out) const noexcept;
    // Le menu d'un onglet sans la souris ; la marque de l'onglet vise (lot 7).
    void requestContextMenu(std::size_t index);
    void markTab(std::size_t index, const PopupMenu* menu);

    const core::SignalPtr<std::size_t> currentChanged = core::Signal<std::size_t>::create();
    const core::SignalPtr<std::size_t> tabCloseRequested = core::Signal<std::size_t>::create();
    const core::SignalPtr<std::size_t, gfx::Point> contextMenuRequested =
        core::Signal<std::size_t, gfx::Point>::create();

    // ---- les indices qui glissent -------------------------------------------
    //  Un onglet est entre a `index` (ceux a partir de la ont avance d'un cran) ;
    //  est sorti de `index` (ceux d'apres ont recule) ; est passe de `from` a
    //  `to` (ceux d'entre les deux ont glisse d'un cran vers `from`).
    const core::SignalPtr<std::size_t> tabInserted = core::Signal<std::size_t>::create();
    const core::SignalPtr<std::size_t> tabRemoved = core::Signal<std::size_t>::create();
    const core::SignalPtr<std::size_t, std::size_t> tabMoved = core::Signal<std::size_t, std::size_t>::create();

    // ---- la disposition ------------------------------------------------------
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    // Changer de mode garde l'ordre des onglets et l'onglet ouvert. Vers
    // Onglets : tous les groupes en un. Vers Mosaique : une tuile par onglet.
    // Vers Groupes : depuis Onglets rien ne bouge (on divise ensuite) ; depuis
    // la mosaique, un seul groupe.
    void setMode(Mode m);
    // Emis quand le mode change - aussi quand un glisser au bord d'un groupe
    // fait passer d'Onglets a Groupes.
    const core::SignalPtr<Mode> modeChanged = core::Signal<Mode>::create();
    // "onglets", "groupes", "mosaique" (les reglages, les scripts).
    [[nodiscard]] static const char* modeName(Mode m) noexcept;
    [[nodiscard]] static bool modeFromName(std::string_view name, Mode& out) noexcept;

    [[nodiscard]] std::size_t groupCount() const noexcept { return groups_.size(); }
    [[nodiscard]] std::size_t activeGroup() const noexcept;
    // Le groupe d'un onglet (npos : hors bornes), et sa bande ; `local` recoit
    // l'indice de l'onglet dans sa bande.
    [[nodiscard]] std::size_t groupOf(std::size_t index) const noexcept;
    [[nodiscard]] TabControl* group(std::size_t g) const noexcept;
    [[nodiscard]] TabControl* stripOf(std::size_t index, std::size_t* local = nullptr) const noexcept;
    [[nodiscard]] gfx::Rect groupRect(std::size_t g) const noexcept;

    // Mettre un onglet dans un NOUVEAU groupe, sur le cote `side` du sien :
    // "Diviser a droite / en bas". Depuis Onglets ou la mosaique, passe en
    // Groupes. Faux quand l'onglet est seul dans son groupe (rien a laisser).
    bool splitTab(std::size_t index, Side side);
    // Le meme geste vers le groupe `target` : ce qu'un depot au bord fait.
    bool dockTab(std::size_t index, std::size_t target, Side side);
    // Ranger un onglet dans le groupe `target`, a la place `at` de sa bande
    // (npos : a la fin) ; dans son propre groupe : le deplacer dans la bande.
    bool moveTabToGroup(std::size_t index, std::size_t target, std::size_t at = npos);
    // Le groupe voisin : le suivant, ou le precedent pour le dernier ; npos
    // quand il n'y en a pas (un seul groupe, la mosaique).
    [[nodiscard]] std::size_t neighbourGroup(std::size_t index) const noexcept;
    bool moveTabToNeighbour(std::size_t index);
    // Deplacer un onglet pour qu'il arrive a l'indice global `to` (l'ordre des
    // tuiles de la mosaique, l'ordre d'une bande, ou d'un groupe a l'autre).
    bool moveTab(std::size_t from, std::size_t to);

    // La mosaique : agrandir une tuile (les autres se cachent) ou revenir.
    // Faux hors de la mosaique. L'onglet agrandi suit l'onglet ouvert.
    bool toggleMaximized(std::size_t index);
    [[nodiscard]] int maximizedIndex() const noexcept;

    // Lacher un onglet hors de la zone demande de le detacher (detachRequested) ;
    // faux par defaut : rien ne se passe hors de la zone.
    void setDetachEnabled(bool on) noexcept { detachEnabled_ = on; }
    const core::SignalPtr<std::size_t> detachRequested = core::Signal<std::size_t>::create();

    // Pour les tests et les scripts : un glisser ou une poignee en cours, et
    // les poignees entre les groupes.
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }
    [[nodiscard]] std::vector<gfx::Rect> splitHandles() const;

protected:
    void onLayout() override;
    void onPaint(const PaintContext&) override;

private:
    class Layer;                 // la couche du glisser (TabArea.cpp)
    struct Node;                 // un noeud de l'arbre des groupes
    enum class DropKind : std::uint8_t { None, Strip, Edge, Centre, Tile, Outside };
    struct Drop {
        DropKind    kind{DropKind::None};
        TabControl* group{nullptr};
        Side        side{Side::Right};
        std::size_t at{0};          // Strip : la place dans la bande
        gfx::Rect   highlight{};    // la zone montree
        float       caretX{0.f};    // Strip : ou s'insere l'en-tete
    };
    struct Press { const Widget* page{nullptr}; gfx::Point at{}; bool armed{false}; };

    // Les bandes : creer (enfant AVANT la couche, qui reste la derniere),
    // brancher, retirer une bande vide.
    TabControl* makeStrip(bool tile);
    void        wire(TabControl* strip);
    void        retire(TabControl* strip);
    void        purgeRetired();
    void        dropGroup(TabControl* strip);
    void        dropIfEmpty(TabControl* strip);
    void        rebuild(Mode target);
    void        rebuildOrder();
    void        ensureActive(std::size_t hint);
    void        refreshFocus();
    void        applyVisibility();
    void        emitCurrentIfChanged();
    [[nodiscard]] Widget* currentPage() const noexcept;
    [[nodiscard]] std::size_t globalOf(const TabControl* strip, std::size_t local) const noexcept;
    [[nodiscard]] std::size_t positionOf(const TabControl* strip) const noexcept;
    [[nodiscard]] TabControl* locate(std::size_t index, std::size_t& local) const noexcept;
    // Deplacer la page d'une bande a l'autre (retiree puis inseree a `at`).
    void transfer(TabControl* from, std::size_t local, TabControl* to, std::size_t at);
    void finishMove(const Widget* page, std::size_t from, TabControl* active);

    // L'arbre des groupes (modes Onglets et Groupes).
    [[nodiscard]] Node* findLeaf(Node* n, const TabControl* strip) const noexcept;
    [[nodiscard]] std::unique_ptr<Node>& slotOf(Node* n);
    void splitLeaf(Node* leaf, TabControl* fresh, Side side);
    void removeLeaf(Node* leaf);
    void layoutNode(Node& n, const gfx::Rect& r);
    [[nodiscard]] Node* handleAt(gfx::Point p) const noexcept;
    void resizeTo(gfx::Point p);
    void layoutMosaic(const gfx::Rect& area);

    // La souris, vue par la couche avant les groupes.
    [[nodiscard]] bool capturing() const noexcept { return press_.armed || dragging_ || resizing_ != nullptr; }
    EventResult layerEvent(const InputEvent& ev);
    [[nodiscard]] TabControl* groupAt(gfx::Point p) const noexcept;
    [[nodiscard]] Drop dropAt(gfx::Point p) const;
    void finishDrag(bool apply);
    void activate(TabControl* strip);
    void paintDrag(const PaintContext& ctx) const;

    Mode                      mode_{Mode::Tabs};
    std::unique_ptr<Node>     root_;              // Onglets, Groupes ; nul en mosaique
    std::vector<TabControl*>  groups_;            // l'ordre global : feuilles de l'arbre, ou tuiles
    TabControl*               active_{nullptr};   // ou vont les nouveaux onglets
    const Widget*             maximized_{nullptr};   // la tuile agrandie (sa page)
    const Widget*             lastCurrent_{nullptr}; // pour currentChanged
    Widget*                   layer_{nullptr};
    int                       internal_{0};       // > 0 : les signaux des bandes sont les notres
    unsigned                  serial_{0};         // les identifiants des bandes
    bool                      detachEnabled_{false};
    Press                     press_;
    bool                      dragging_{false};
    gfx::Point                pointer_{};
    Drop                      drop_;
    Node*                     resizing_{nullptr};
    // UNE BANDE VIDE NE SE DETRUIT PAS TOUT DE SUITE. La croix du dernier
    // onglet d'un groupe ferme ce groupe PENDANT que sa bande emet
    // tabCloseRequested - et un signal detruit au milieu de son emission, c'est
    // la memoire liberee sous le code qui tourne. Retiree de l'ecran aussitot,
    // la bande attend ici la prochaine peinture (aucun evenement en cours) ;
    // ses abonnements partent avec elle.
    std::vector<WidgetPtr>                              retired_;
    std::map<const TabControl*, core::ConnectionScope> stripLinks_;
};

} // namespace ui
