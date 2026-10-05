// =============================================================================
//  ui/widgets/Containers.hpp — structural widgets
// =============================================================================
#pragma once

#include "../../core/Command.hpp"
#include "../Icons.hpp"
#include "../Layout.hpp"
#include "../Widget.hpp"
#include "Controls.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ui {

// -------------------------------------------------------------- GroupBox ---
class GroupBox : public Widget {
public:
    explicit GroupBox(std::string title, std::string id = {});
    void setTitle(std::string t);
    void setCollapsible(bool c) { collapsible_ = c; }
    void setCollapsed(bool c);
    Widget& body() { return *body_; }
    [[nodiscard]] SizeHint sizeHint() const override;
protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
private:
    std::string title_;
    Widget*     body_{nullptr};      // owned via children_
    bool        collapsible_{false}, collapsed_{false};
};

// -------------------------------------------------------------- Splitter ---
// Resizable panes. Sizes are stored as ratios so that a window resize keeps the
// user's proportions, and are persisted in the workspace layout file.
class Splitter : public Widget {
public:
    explicit Splitter(Orientation o, std::string id = {});
    Widget& addPane(WidgetPtr w, float ratio, float minExtent = 80.f);
    void    setRatios(const std::vector<float>& r);
    [[nodiscard]] std::vector<float> ratios() const;
    void    collapsePane(std::size_t index, bool collapsed);
protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
private:
    struct Pane { Widget* w; float ratio; float minExtent; bool collapsed; };
    [[nodiscard]] int handleAt(gfx::Point local) const;

    Orientation       orientation_;
    std::vector<Pane> panes_;
    int               dragHandle_{-1};
    float             dragOrigin_{0.f};
};

// ------------------------------------------------------------ TabControl ---
class TabControl : public Widget {
public:
    struct Tab {
        std::string title; Icon icon{Icon::None}; bool closable{false}; bool modified{false};
        // --- ajoutes a la FIN : {"Aide", Icon::Document, false, false} reste valide ---
        std::string badge{};             // une pastille apres le titre : "3", "en cours"
        Tone        badgeTone{Tone::None};
        bool        live{false};         // une LED qui pulse : quelque chose tourne ici
    };

    explicit TabControl(std::string id = {});
    std::size_t addTab(Tab meta, WidgetPtr page);
    void        removeTab(std::size_t index);
    // Lot API 2 : retirer un onglet SANS detruire sa page - elle est rendue a
    // l'appelant, qui la garde pour la remontrer (l'onglet Configuration et ses
    // widgets relies au projet, le volet Panneaux et ses cases).
    [[nodiscard]] WidgetPtr takeTab(std::size_t index);
    void        setCurrentIndex(std::size_t index);
    void        setTabModified(std::size_t index, bool modified);
    void        setTabBadge(std::size_t index, std::string badge, Tone tone = Tone::None);
    void        setTabLive(std::size_t index, bool live);
    // Le titre suit ce que l'onglet montre : une vue renommee ailleurs.
    void        setTabTitle(std::size_t index, std::string title);
    // 1.8.0 : l'icone suit aussi (l'icone au choix d'une section).
    void        setTabIcon(std::size_t index, Icon icon);
    [[nodiscard]] const Tab* tab(std::size_t index) const noexcept {
        return index < tabs_.size() ? &tabs_[index].meta : nullptr;
    }
    [[nodiscard]] std::size_t currentIndex() const noexcept { return current_; }
    [[nodiscard]] std::size_t tabCount() const noexcept { return tabs_.size(); }
    // La page d'un onglet, et l'onglet d'une page. Fermer un onglet decale les
    // indices de tous ceux de droite ; un pointeur de page, lui, ne bouge pas :
    // c'est lui qu'il faut garder pour retrouver un onglet plus tard.
    [[nodiscard]] Widget* page(std::size_t index) const noexcept {
        return index < tabs_.size() ? tabs_[index].page : nullptr;
    }
    [[nodiscard]] int indexOf(const Widget* page) const noexcept {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].page == page) return static_cast<int>(i);
        return -1;
    }
    // L'en-tete d'un onglet a l'ecran (apres un premier dessin), pour les
    // scripts qui cliquent dessus comme une souris.
    [[nodiscard]] bool headerRect(std::size_t index, gfx::Rect& out) const noexcept {
        if (index >= tabs_.size() || tabs_[index].headerRect.w <= 0.f) return false;
        out = tabs_[index].headerRect;
        return true;
    }

    const core::SignalPtr<std::size_t> currentChanged = core::Signal<std::size_t>::create();
    // La croix d'un onglet, et (lot 7) le clic du milieu sur son en-tete.
    const core::SignalPtr<std::size_t> tabCloseRequested = core::Signal<std::size_t>::create();

    // LOT 7 : LE CLIC DROIT SUR UN EN-TETE. Le proprietaire montre son menu
    // (Fermer, Fermer tout, Fermer tout sauf celui-ci) a `where`, juste sous
    // l'en-tete, en coordonnees de la fenetre. Il vise CET onglet : l'onglet
    // ouvert ne change pas.
    const core::SignalPtr<std::size_t, gfx::Point> contextMenuRequested =
        core::Signal<std::size_t, gfx::Point>::create();
    // Le meme menu sans la souris (un script, un en-tete hors de la bande).
    void requestContextMenu(std::size_t index);
    // L'onglet que vise un menu ouvert : entoure de tirets tant que `menu` est
    // ouvert, pour qu'on voie de quel onglet il parle ; la marque s'efface
    // d'elle-meme a sa fermeture. `menu` vit autant que la bande (un overlay
    // de l'ecran) ; nul : pas de marque.
    void markTab(std::size_t index, const PopupMenu* menu);

    // LOT 19 : QUAND LES ONGLETS NE TIENNENT PAS. Par paliers : tout en entier ;
    // les pastilles raccourcies ("104 + 13 IHM" -> "104+13", "nouveau" -> un
    // point) ; les onglets inactifs reduits a leur icone et leur nombre (l'onglet
    // ouvert garde son nom) ; enfin des fleches pour defiler. Des qu'un onglet
    // est reduit ou cache, un bouton a droite liste TOUS les onglets.
    enum class Fit : std::uint8_t { Full, ShortBadges, Compact, Scroll };
    [[nodiscard]] Fit  fit() const noexcept { return fit_; }
    [[nodiscard]] bool compacted(std::size_t index) const noexcept {
        return index < tabs_.size() && tabs_[index].compact;
    }
    // Le bouton de la liste, les fleches (vides quand ils ne sont pas montres).
    [[nodiscard]] gfx::Rect listButtonRect() const noexcept { return listButton_; }
    [[nodiscard]] gfx::Rect arrowRect(bool right) const noexcept { return right ? arrowRight_ : arrowLeft_; }
    // Ouvrir la liste de tous les onglets (le bouton, ou un script).
    void openTabList();
    [[nodiscard]] bool tabListOpen() const noexcept;
    // La pastille courte d'une pastille : ses chiffres de tete, sans les mots.
    [[nodiscard]] static std::string shortBadge(std::string_view badge);

    // LOT 7 (DISPOSITION) : CE QU'IL FAUT A LA ZONE DES ONGLETS (TabArea) pour
    // passer un onglet d'un groupe a l'autre SANS DETRUIRE SA PAGE, et savoir
    // ce qu'il y a sous la souris quand on en tire un.
    //
    // Inserer a `at` (au-dela de la fin : a la fin). L'onglet ouvert le reste ;
    // le premier onglet d'une bande vide devient l'onglet ouvert.
    std::size_t insertTab(std::size_t at, Tab meta, WidgetPtr page);
    // Deplacer un onglet dans la bande. L'onglet ouvert le reste (sa page, pas
    // son indice).
    void        moveTab(std::size_t from, std::size_t to);
    // L'en-tete sous `p` (coordonnees de la fenetre) dans la partie visible de
    // la bande ; -1 : aucun. `onButton` : sur sa croix, ou sur le bouton
    // agrandir d'une tuile - un appui la qui ne doit pas tirer l'onglet.
    [[nodiscard]] int headerAt(gfx::Point p, bool* onButton = nullptr) const noexcept;
    // La bande des en-tetes (la barre de titre d'une tuile), apres une mise en page.
    [[nodiscard]] gfx::Rect barRect() const noexcept;

    // UNE TUILE (la mosaique automatique) : un seul onglet, son en-tete sur
    // toute la largeur - une barre de titre, plus basse - avec un bouton
    // agrandir a cote de la croix. Ce bouton et le double clic sur la barre
    // demandent maximizeRequested ; `maximized` change son dessin (restaurer).
    void setTileMode(bool tile);
    [[nodiscard]] bool tileMode() const noexcept { return tile_; }
    void setTileMaximized(bool maximized);
    // Plusieurs groupes a l'ecran : celui qui recoit les nouveaux onglets garde
    // le soulignement d'accent, les autres l'ont en gris. Seul, toujours vrai.
    void setGroupFocus(bool focus);
    [[nodiscard]] bool groupFocus() const noexcept { return groupFocus_; }
    const core::SignalPtr<std::size_t> maximizeRequested = core::Signal<std::size_t>::create();

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
    [[nodiscard]] bool hitTest(gfx::Point local) const override;
private:
    struct Entry { Tab meta; Widget* page; gfx::Rect headerRect; bool compact{false}; bool shortBadge{false}; };
    // Lot 7 : ou poser le menu d'un onglet (sous son en-tete, dans la bande).
    [[nodiscard]] gfx::Point contextAnchor(std::size_t index) const;
    // Lot 7 (disposition) : la hauteur de la bande (plus basse pour une tuile),
    // les boutons d'une tuile, et sa peinture.
    [[nodiscard]] float barHeight() const noexcept;
    void tileButtons(const gfx::Rect& header, bool closable, gfx::Rect& close, gfx::Rect& maximize) const noexcept;
    void paintTile(const PaintContext& ctx);
    bool               tile_{false};
    bool               tileMaximized_{false};
    bool               groupFocus_{true};
    std::vector<Entry> tabs_;
    Fit                fit_{Fit::Full};
    gfx::Rect          listButton_{}, arrowLeft_{}, arrowRight_{}, viewport_{};
    PopupMenu*         tabList_{nullptr};
    gfx::Size          surface_{};
    mutable gfx::Point lastPointer_{-1.f, -1.f};   // local, mis a jour a chaque evenement de souris
    float              lastWidth_{-1.f};             // la largeur de la derniere mise en page
    std::size_t        current_{0};
    float              scrollOffset_{0.f};   // <= 0 : la barre defile quand les onglets debordent (molette)
    bool               revealCurrent_{true}; // l'onglet courant a change : le montrer
    float              stripWidth_{0.f};      // la largeur de tous les en-tetes
    mutable float      tabHeight_{30.f};   // cached from the theme at paint time
    gfx::FontId        tabFont_{16};       // idem : la police des titres
    int                hoverTab_{-1};
    // Le soulignement glisse d'un onglet a l'autre au lieu de sauter : on voit
    // d'ou l'on vient. `underlineX_` < 0 : pas encore peint.
    float              underlineX_{-1.f}, underlineW_{0.f};
    // Lot 7 : l'onglet (par sa page : les indices glissent) que vise un menu
    // ouvert, et ce menu.
    const Widget*      markPage_{nullptr};
    const PopupMenu*   markMenu_{nullptr};
    core::ConnectionScope links_;
};

// ------------------------------------------------------- ScrollablePanel ---
// The one place scrolling is implemented. TreeView/TableView/ListView compose
// it rather than reimplementing wheel handling, kinetic damping and scrollbars.
class ScrollablePanel : public Widget {
public:
    explicit ScrollablePanel(std::string id = {});
    void setContent(WidgetPtr w);
    void setScrollPolicy(bool horizontal, bool vertical);
    void scrollTo(gfx::Point offset);
    void ensureVisible(const gfx::Rect& localRect);
    [[nodiscard]] gfx::Point scrollOffset() const noexcept { return offset_; }
    [[nodiscard]] gfx::Size  viewportSize() const noexcept;

    const core::SignalPtr<gfx::Point> scrolled = core::Signal<gfx::Point>::create();

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    void        onPaintOverlay(const PaintContext&) override;   // scrollbars on top
    EventResult onEvent(const InputEvent&) override;
private:
    Widget*    content_{nullptr};
    gfx::Point offset_{}, target_{};
    bool       hScroll_{true}, vScroll_{true};
    int        dragBar_{-1};
};

// --------------------------------------------------------------- ToolBar ---
class ToolBar : public Widget {
public:
    explicit ToolBar(std::string id = {});
    Button& addButton(std::string text, core::ActionId action, Icon icon = Icon::None);
    void    addSeparator();
    Widget& addCustom(WidgetPtr w);          // e.g. the Build-configuration DropDown
    void    setOverflowEnabled(bool e) { overflow_ = e; }
    // Where a toolbar button's ActionId goes when it is clicked. The toolbar
    // does not own an ActionRegistry; the screen supplies the sink.
    void    setActionSink(std::function<void(core::ActionId)> sink);

    // How the bar behaved when it last laid out. Public because it is what the
    // layout test asserts on, and because a status line may want to say it.
    enum class Fit : std::uint8_t { Full, IconsOnly, Overflowing };
    [[nodiscard]] Fit         fit() const noexcept { return fit_; }
    [[nodiscard]] std::size_t hiddenCount() const noexcept { return hidden_; }
protected:
    void onLayout() override;
    void onPaint(const PaintContext&) override;
private:
    struct Slot { Widget* w; bool separator; Button* button; };
    void rebuildOverflow();

    std::vector<Slot>                     slots_;
    std::function<void(core::ActionId)>   sink_;
    core::ConnectionScope                 links_;
    DropDown*                             overflowMenu_{nullptr};
    std::vector<core::ActionId>           overflowActions_;
    mutable Fit                           fit_{Fit::Full};
    mutable std::size_t                   hidden_{0};
    bool                                  overflow_{true};
};

// ------------------------------------------------------------- StatusBar ---
class StatusBar : public Widget {
public:
    enum class Slot : std::uint8_t { Left, Right };
    // La gravite d'un message : un pictogramme et une couleur, pour que
    // « enregistre » et « echec de l'enregistrement » ne se lisent pas pareil.
    enum class Severity : std::uint8_t { None, Info, Success, Warning, Error };

    explicit StatusBar(std::string id = {});
    // L'accent par defaut est TRANSPARENT, donc « pas d'accent » : il valait
    // {} - un noir opaque, puisque gfx::Color a 255 d'alpha par defaut - et
    // chaque message sans couleur s'ecrivait en noir, invisible en theme sombre.
    void setMessage(std::string text, gfx::Color accent = gfx::Color{0, 0, 0, 0});
    void setMessage(std::string text, Severity severity);
    // Un message qui s'efface de lui-meme apres `seconds`, et rend la place au
    // message durable qui etait la avant lui.
    void setTransientMessage(std::string text, double seconds, Severity severity = Severity::Info);
    // Retirer le transitoire tout de suite (l'utilisateur est passe a autre
    // chose) : le message durable revient. Sans transitoire, ne fait rien.
    void dismissTransient();
    Widget& addIndicator(WidgetPtr w, Slot slot);

    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] Severity severity() const noexcept { return severity_; }
    // ---- Lot API 8 : le bandeau bas ----
    //  LE POINT DE PASSAGE UNIQUE : chaque message pose (durable ou transitoire)
    //  est rendu a `hook` - le journal des messages (app/StatusStrip) les garde.
    //  Un clic sur le texte du message appelle `click` (le journal s'ouvre).
    void setMessageHook(std::function<void(const std::string&, Severity, bool transient)> hook) { hook_ = std::move(hook); }
    void setMessageClick(std::function<void()> click) { click_ = std::move(click); }
    // ---- fin Lot API 8 : le bandeau bas ----
protected:
    void onLayout() override;
    void onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;   // Lot API 8 : le clic sur le message
private:
    // ---- Lot API 8 : le bandeau bas ----
    std::function<void(const std::string&, Severity, bool)> hook_;
    std::function<void()> click_;
    Severity             hookSeverity_{Severity::None};
    // ---- fin Lot API 8 : le bandeau bas ----
    std::string          message_;
    gfx::Color           accent_{0, 0, 0, 0};
    Severity             severity_{Severity::None};
    double               expiry_{0.0};          // duree demandee, en secondes
    double               expiresAt_{-1.0};      // fixe a la premiere image
    double               shownAt_{-1.0};        // pour le fondu d'entree
    // Le message durable, rendu quand un transitoire s'efface.
    std::string          steady_;
    gfx::Color           steadyAccent_{0, 0, 0, 0};
    Severity             steadySeverity_{Severity::None};
    std::vector<Widget*> left_, right_;
};

} // namespace ui
