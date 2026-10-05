// =============================================================================
//  menu/MenuManager.hpp — the navigation stack
// -----------------------------------------------------------------------------
//  ON THE SINGLETON IN THE BRIEF
//  ----------------------------
//  The brief asks for a MenuManager singleton, and the brief also asks for
//  dependency injection and testability. Those two pull in opposite directions:
//  a global mutable stack makes it impossible to run two windows, to test a
//  navigation sequence in isolation, or to reason about shutdown order.
//
//  The compromise implemented here: MenuManager is an ordinary class with an
//  ordinary constructor, owned by App and injected into whoever needs it. A
//  static instance() accessor is provided and is *populated by App* through the
//  ScopedInstance guard, for the handful of call sites where threading a
//  reference through would be gratuitous. Nothing in the framework itself uses
//  instance(); remove it and everything still builds.
//
//  DEFERRED MUTATION
//  -----------------
//  Stack operations issued from inside HandleEvent/Update are queued and applied
//  at the next frame boundary. Without this, a menu that pops itself in response
//  to a click would destroy the object whose stack frame is still executing.
// =============================================================================
#pragma once

#include "IMenu.hpp"

#include "../core/Result.hpp"
#include "../core/Signal.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace menu {

// Factory registration: unlimited nesting comes from the ids, not from the code.
//   registry.add("settings",          [] { return std::make_unique<SettingsMenu>(); });
//   registry.add("settings.graphics", [] { return std::make_unique<GraphicsMenu>(); });
class MenuFactory {
public:
    using Creator = std::function<MenuPtr()>;
    void add(MenuId id, Creator c) { creators_.emplace(std::move(id), std::move(c)); }
    [[nodiscard]] bool contains(const MenuId& id) const { return creators_.count(id) != 0; }
    [[nodiscard]] core::Result<MenuPtr> create(const MenuId& id) const;
    [[nodiscard]] std::vector<MenuId> childrenOf(std::string_view parentId) const;
private:
    std::unordered_map<MenuId, Creator> creators_;
};

class MenuManager {
public:
    explicit MenuManager(MenuFactory factory);
    ~MenuManager();

    MenuManager(const MenuManager&)            = delete;
    MenuManager& operator=(const MenuManager&) = delete;

    // ---- navigation (queued; applied by applyPending()) -------------------
    void PushMenu(const MenuId& id);
    void PushMenu(MenuPtr menu);
    void PopMenu();
    void PopToRoot();
    void PopTo(const MenuId& id);
    void ReplaceMenu(const MenuId& id);          // pop current, push new: same depth
    void ReplaceMenu(MenuPtr menu);
    void SwitchMenu(const MenuId& id);           // clear the whole stack, start over
    void PreviousMenu();                         // history-based back, not stack-based

    // ---- modal / dialog / overlay ----------------------------------------
    void ShowModal(MenuPtr menu);
    void ShowDialog(MenuPtr dialog, std::function<void(const DialogResult&)> onClose);
    void CloseDialog(const DialogResult& result);
    void ShowOverlay(MenuPtr overlay, std::string tag);   // dropdown popup, tooltip, toast
    void CloseOverlay(std::string_view tag);
    void CloseAllOverlays();

    // ---- frame -----------------------------------------------------------
    void applyPending();                                  // call first, once per frame
    void Update(const FrameContext&);
    void Render(gfx::IRenderer&, const FrameContext&);
    ui::EventResult HandleEvent(const ui::InputEvent&);

    // ---- introspection ---------------------------------------------------
    [[nodiscard]] IMenu*      top() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept { return stack_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return stack_.empty(); }
    [[nodiscard]] bool        contains(const MenuId&) const noexcept;
    [[nodiscard]] std::vector<MenuId> path() const;       // breadcrumb: root -> top
    [[nodiscard]] const MenuFactory&  factory() const noexcept { return factory_; }

    const core::SignalPtr<const MenuId&> menuEntered = core::Signal<const MenuId&>::create();
    const core::SignalPtr<const MenuId&> menuExited  = core::Signal<const MenuId&>::create();

    // ---- Lot API 8 : les dialogues dans la fenetre detachee -----------------
    //  CHAQUE COUCHE A SA FENETRE. kMainWindow (0) : la fenetre principale ; un
    //  autre numero : une autre fenetre du systeme, un onglet detache (le numero
    //  que lui donne app/DetachedWindows). La pile reste UNE : un dialogue ouvert
    //  dans une fenetre detachee est modal pour toute l'application.
    //
    //  LA FENETRE D'UN DIALOGUE est celle "en cours" quand on le demande
    //  (ShowDialog, ShowModal, PushMenu d'un dialogue) : celle dont on traite un
    //  evenement - WindowScope, pose par DetachedWindows le temps d'un de ses
    //  evenements, et par la pile quand elle passe un evenement, une mise a jour
    //  ou le rappel onClose a une couche d'une autre fenetre. Sinon la
    //  principale : un script, un minuteur, l'appli. Un ecran (Screen) et un
    //  Overlay sont toujours a la principale ; une fenetre qui n'est plus aussi.
    //
    //  Update() met a jour chaque couche a la taille de SA surface (celle que
    //  rend setWindowSurfaces) ; Render() ne dessine que celles de la
    //  principale, RenderWindow() celles d'une autre fenetre, par-dessus sa page.
    //  HandleEvent() va, comme avant, au haut de la pile OU QU'IL SOIT (les
    //  scripts, les essais, la fenetre ou la question attend) : une fenetre ne
    //  passe pas ses evenements quand la question est ailleurs (routeFor). Une
    //  fenetre qui se ferme rend ses couches a la principale, intactes
    //  (moveWindow ; a defaut, la mise a jour suivante le fait).
    using WindowId = std::uint32_t;
    static constexpr WindowId kMainWindow = 0;
    // La taille de la surface d'une fenetre ; nullopt : elle n'existe pas (plus).
    using WindowSurfaces = std::function<std::optional<gfx::Size>(WindowId)>;

    // La fenetre "en cours" le temps d'une portee (les portees s'imbriquent).
    class WindowScope {
    public:
        WindowScope(MenuManager& m, WindowId w) noexcept;
        ~WindowScope();
        WindowScope(const WindowScope&) = delete;
        WindowScope& operator=(const WindowScope&) = delete;
    private:
        MenuManager& manager_;
        WindowId     previous_;
    };

    // Ce que devient un evenement de la fenetre `w` (routeFor).
    enum class Route : std::uint8_t {
        Content,     // aucune question n'attend : a ce que montre la fenetre
        Layers,      // la question attend DANS cette fenetre : a la pile (HandleEvent)
        Elsewhere,   // la question attend dans une autre fenetre : a personne
    };

    void setWindowSurfaces(WindowSurfaces surfaces) { windowSurfaces_ = std::move(surfaces); }
    [[nodiscard]] WindowId currentWindow() const noexcept { return currentWindow_; }
    // Une question attend : le haut de la pile est un dialogue ou un modal qui
    // bloque (un ecran au-dessus : rien n'attend, comme avant).
    [[nodiscard]] bool     questionWaiting() const noexcept;
    // La fenetre de la question (kMainWindow quand rien n'attend).
    [[nodiscard]] WindowId questionWindow() const noexcept;
    [[nodiscard]] Route    routeFor(WindowId w) const noexcept;
    // La fenetre de la couche `m` (kMainWindow : pas dans la pile, ou a la principale).
    [[nodiscard]] WindowId windowOf(const IMenu* m) const noexcept;
    // Les couches de `w` : dans la pile, et demandees mais pas encore posees.
    [[nodiscard]] std::size_t layersIn(WindowId w) const noexcept;
    // Les couches de `from` (et ses demandes en attente) passent a `to`,
    // intactes ; remises en page a la taille de sa surface. Rend combien.
    std::size_t moveWindow(WindowId from, WindowId to);
    // Les couches de `w`, par-dessus ce que la fenetre a deja dessine (sa page) :
    // pour chacune son voile, puis elle. fc.surface : la taille de SA surface.
    void RenderWindow(WindowId w, gfx::IRenderer& r, const FrameContext& fc);

    // ---- optional global access (see header comment) ---------------------
    class ScopedInstance {
    public:
        explicit ScopedInstance(MenuManager& m);
        ~ScopedInstance();
        ScopedInstance(const ScopedInstance&) = delete;
    };
    [[nodiscard]] static MenuManager* instance() noexcept { return s_instance; }

private:
    enum class Op : std::uint8_t { Push, Pop, PopToRoot, PopTo, Replace, Switch, Previous,
                                   ShowModal, ShowDialog, CloseDialog, ShowOverlay, CloseOverlay };
    struct Request {
        Op                                          op;
        MenuId                                      id;
        MenuPtr                                     menu;
        std::function<void(const DialogResult&)>    onClose;
        DialogResult                                result;
        std::string                                 tag;
        WindowId                                    window{kMainWindow};   // lot API 8 : la fenetre en cours a la demande
    };
    struct Entry {
        MenuPtr                                  menu;
        MenuTraits                               traits;
        std::function<void(const DialogResult&)> onClose;
        std::string                              tag;
        // Lot API 8 : sa fenetre, et la taille de sa surface la derniere fois
        // qu'elle y a ete mise a jour ou dessinee (hors de la principale).
        WindowId                                 window{kMainWindow};
        gfx::Size                                surface{};
    };

    void enqueue(Request r);
    core::Status pushEntry(MenuPtr menu, std::function<void(const DialogResult&)> onClose = {},
                           std::string tag = {}, WindowId window = kMainWindow);
    void popEntry();
    // Index of the lowest layer that must be rendered this frame.
    [[nodiscard]] std::size_t firstRenderedLayer() const noexcept;
    [[nodiscard]] std::size_t firstUpdatedLayer() const noexcept;
    // ---- Lot API 8 : les dialogues dans la fenetre detachee ----
    // La taille de la surface de `w` (nullopt : elle n'existe pas ou plus).
    [[nodiscard]] std::optional<gfx::Size> surfaceOf(WindowId w) const;
    // Les couches de `w` a dessiner, de bas en haut.
    [[nodiscard]] std::vector<std::size_t> renderedLayers(WindowId w) const;
    // Les couches (et demandes) d'une fenetre qui n'est plus reviennent a la principale.
    void adoptLostWindows();
    // ---- fin lot API 8 ----

    MenuFactory           factory_;
    std::vector<Entry>    stack_;
    std::vector<Entry>    overlays_;      // always above the stack, painted last
    std::deque<Request>   pending_;
    std::vector<MenuId>   history_;       // for PreviousMenu()
    bool                  applying_{false};
    // Lot API 8 : la fenetre en cours (WindowScope), ou trouver la taille des
    // autres, et celle de la principale (le dernier Update / Render).
    WindowId              currentWindow_{kMainWindow};
    WindowSurfaces        windowSurfaces_;
    gfx::Size             mainSurface_{};

    static inline MenuManager* s_instance{nullptr};
};

} // namespace menu
