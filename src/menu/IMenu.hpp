// =============================================================================
//  menu/IMenu.hpp — the screen/menu contract
// -----------------------------------------------------------------------------
//  A "menu" here is any full-screen or floating unit of interaction: the startup
//  screen, the main analysis workspace, a settings page, a modal dialog, a
//  context popup. They all obey the same six-method lifecycle, so the manager
//  does not care which is which.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../platform/InputEvent.hpp"
#include "../platform/Renderer.hpp"
#include "../ui/Theme.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ui { class Widget; class FocusChain; }

namespace menu {

// Stable identity used by the factory and by PushMenu("project.settings.graphics").
using MenuId = std::string;

// How a menu interacts with the ones below it on the stack.
enum class MenuKind : std::uint8_t {
    Screen,   // opaque, full window: hides everything below
    Modal,    // floating, dims the layer below, swallows all input
    Dialog,   // like Modal but sized to content and returns a result
    Overlay,  // floating, does NOT swallow input outside its bounds (dropdown popup, tooltip, toast)
};

struct MenuTraits {
    MenuKind kind{MenuKind::Screen};
    bool     rendersBelow{false};   // draw the layer underneath before me
    bool     updatesBelow{false};   // tick the layer underneath (animations keep running)
    bool     blocksInput{true};     // stop event propagation to lower layers
    bool     dimsBelow{false};      // paint a scrim
    bool     closableWithEscape{true};
};

// Result handed back when a Dialog closes. std::string keeps it type-erased at
// the manager boundary; concrete dialogs expose a typed accessor.
struct DialogResult {
    enum class Button : std::uint8_t { None, Ok, Cancel, Yes, No, Apply, Retry } button{Button::None};
    std::string payload;                 // e.g. the chosen file path
    [[nodiscard]] bool accepted() const noexcept {
        return button == Button::Ok || button == Button::Yes || button == Button::Apply;
    }
};

struct FrameContext {
    double          deltaSeconds{};
    double          totalSeconds{};
    const ui::Theme* theme{};
    gfx::Size        surface{};
    float            dpiScale{1.f};
};

class MenuManager;   // forward: menus talk back to their manager

class IMenu {
public:
    virtual ~IMenu() = default;

    // ---- the six lifecycle methods from the brief -------------------------
    virtual core::Status Initialize()                        = 0;  // once, on creation
    virtual void         Update(const FrameContext&)         = 0;  // per frame, if active
    virtual void         Render(gfx::IRenderer&,
                                const FrameContext&)         = 0;  // per frame, if visible
    virtual ui::EventResult HandleEvent(const ui::InputEvent&) = 0;
    virtual void         OnEnter()                           = 0;  // became top of stack
    virtual void         OnExit()                            = 0;  // left the top of stack

    // ---- identity & behaviour --------------------------------------------
    [[nodiscard]] virtual MenuId      id() const = 0;
    [[nodiscard]] virtual std::string title() const { return id(); }
    [[nodiscard]] virtual MenuTraits  traits() const { return {}; }

    // Called before the manager pops the menu. Returning false vetoes the pop,
    // which is how "You have unsaved analysis — discard?" is implemented without
    // the manager knowing anything about analysis.
    [[nodiscard]] virtual bool canClose() const { return true; }
    // L'arbre de widgets, pour qui doit y chercher un widget par son id (les
    // scripts de capture). nullptr pour un menu qui n'en a pas.
    [[nodiscard]] virtual ui::Widget* widgetRoot() noexcept { return nullptr; }

    // The manager injects itself so a menu can push a submenu without a global.
    void attach(MenuManager* m) noexcept { manager_ = m; }

protected:
    [[nodiscard]] MenuManager& manager() const { return *manager_; }

private:
    MenuManager* manager_{nullptr};   // non-owning; outlives every menu by construction
};

using MenuPtr = std::unique_ptr<IMenu>;

// ---------------------------------------------------------------------------
// Convenience base: holds a widget tree and forwards the lifecycle to it, so a
// concrete screen only writes buildUi() and its own signal wiring.
// ---------------------------------------------------------------------------
class WidgetMenu : public IMenu {
public:
    explicit WidgetMenu(MenuId id);
    ~WidgetMenu() override;

    core::Status    Initialize() override;
    void            Update(const FrameContext&) override;
    void            Render(gfx::IRenderer&, const FrameContext&) override;
    ui::EventResult HandleEvent(const ui::InputEvent&) override;
    void            OnEnter() override;
    void            OnExit() override;

    [[nodiscard]] MenuId id() const override { return id_; }
    [[nodiscard]] ui::Widget* widgetRoot() noexcept override { return root_.get(); }

    // Le widget dont l'infobulle vaut sous la souris. Un widget qui peint
    // par-dessus tout (une liste ouverte, le didacticiel de l'IHM) et couvre
    // la souris ne laisse pas percer les infobulles du dessous. Public pour
    // les tests ; Update s'en sert.
    [[nodiscard]] static const ui::Widget* tooltipTargetAt(ui::Widget& root, gfx::Point mouse);

    // 1.11.2 (T1, R1112-6) : pendant la demonstration d'un tutoriel, le pointeur est au
    // tutoriel ; arrete 0,9 s sur une ligne de l'arbre, il ouvrait son infobulle, qui cachait
    // ce qu'il montrait (« IHM · Clic : ouvrir… » sur Generer et Compiler). Le lecteur les fait
    // taire (lecture, pause, fin d'etape) et les rend a l'« A toi » et a la fin.
    static void muteTooltips(bool on) noexcept;
    [[nodiscard]] static bool tooltipsMuted() noexcept;

protected:
    virtual core::Status buildUi() = 0;      // create the widget tree
    virtual void         onEnter() {}        // subclass hooks, called after the base work
    virtual void         onExit()  {}

    ui::Widget&     root();
    void            setRoot(std::unique_ptr<ui::Widget> w);
    ui::FocusChain& focus();

private:
    // Lot 7 : plus const - le cote ou l'infobulle ouverte s'est posee est garde
    // d'une image a l'autre (son texte, relu a chaque image, peut changer).
    void drawTooltip(gfx::IRenderer&, const FrameContext&);

    MenuId                           id_;
    std::unique_ptr<ui::Widget>      root_;
    std::unique_ptr<ui::FocusChain>  focus_;
    bool                             initialized_{false};

    // Tooltip state. Kept at the menu rather than the widget so that only one
    // can ever be on screen and so it paints above everything, including a
    // dropdown popup.
    gfx::Point                       mouse_{};
    const ui::Widget*                tooltipTarget_{nullptr};
    double                           hoverSince_{0.0};
    // Lot 7 : ou l'infobulle ouverte s'est posee (ui::TooltipPlacement, a plat
    // pour ne pas tirer ui/WidgetHost.hpp dans cet en-tete).
    bool                             tipPlaced_{false}, tipLeft_{false}, tipAbove_{false};
    gfx::Point                       tipMouse_{};
};

} // namespace menu
