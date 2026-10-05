#include "IMenu.hpp"

#include "MenuManager.hpp"

#include "../ui/Widget.hpp"
#include "../ui/WidgetHost.hpp"     // lot 7 : le dessin de l'infobulle, partage

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace menu {

namespace {
bool g_tooltipsMuted = false;   // 1.11.2 (R1112-6) : voir IMenu.hpp
}

void WidgetMenu::muteTooltips(bool on) noexcept { g_tooltipsMuted = on; }
bool WidgetMenu::tooltipsMuted() noexcept { return g_tooltipsMuted; }

WidgetMenu::WidgetMenu(MenuId id)
    : id_(std::move(id)), focus_(std::make_unique<ui::FocusChain>()) {}

WidgetMenu::~WidgetMenu() = default;

core::Status WidgetMenu::Initialize() {
    if (initialized_) return core::ok();
    if (auto r = buildUi(); !r) return r;
    if (!root_)
        return core::fail(core::ErrorCode::NotImplemented, "buildUi() left '" + id_ + "' without a root");
    focus_->setRoot(root_.get());
    initialized_ = true;
    return core::ok();
}

void WidgetMenu::setRoot(std::unique_ptr<ui::Widget> w) {
    root_ = std::move(w);
    focus_->setRoot(root_.get());
}

ui::Widget&     WidgetMenu::root()  { return *root_; }
ui::FocusChain& WidgetMenu::focus() { return *focus_; }

void WidgetMenu::Update(const FrameContext& fc) {
    if (!root_) return;
    // Layout runs here rather than in Render so that a widget that resizes in
    // response to new data is laid out before anything is painted.
    root_->setBounds({0.f, 0.f, fc.surface.w, fc.surface.h});
    root_->layout();

    // Lot 21 : un ecran qui vit sous un dialogue (pendant un parcours du
    // didacticiel) ne montre pas d'infobulle - la souris est au dialogue, et
    // l'ecran ne la voit plus bouger : il croyait qu'elle restait sur un bouton.
    if (manager().top() != this) {
        tooltipTarget_ = nullptr;
        return;
    }
    const ui::Widget* under = tooltipTargetAt(*root_, mouse_);
    if (under != tooltipTarget_) {
        tooltipTarget_ = under;
        hoverSince_    = fc.totalSeconds;      // restart the dwell timer
        tipPlaced_     = false;                // lot 7 : a new tooltip picks its side afresh
    }
}

const ui::Widget* WidgetMenu::tooltipTargetAt(ui::Widget& root, gfx::Point mouse) {
    // Sans cela, l'infobulle d'un bouton cache sous le voile du didacticiel
    // (ou sous une liste ouverte) s'affichait par-dessus : les infobulles se
    // dessinent apres tout le reste.
    if (ui::Widget* overlay = root.findOverlayOwner(); overlay && overlay->eventBounds().contains(mouse)
                                                       && overlay->overlayCovers(mouse))
        return overlay->tooltipAt(mouse);
    return root.tooltipAt(mouse);
}

void WidgetMenu::drawTooltip(gfx::IRenderer& r, const FrameContext& fc) {
    // Dwell before showing. Half a second turned out to be too eager: tooltips
    // fired while the pointer was merely crossing the window. 0.9 s is close to
    // what Windows itself uses and stops the interface twitching.
    // Lot API 8 : corrections des captures - UN DIALOGUE QUI S'OUVRE FERME LES
    // INFOBULLES. La couche sous un dialogue n'a plus d'Update (la pile ne met a
    // jour que le haut), mais elle est toujours dessinee : l'infobulle retenue
    // avant le dialogue restait affichee dessous, la souris ailleurs.
    if (manager().top() != this) {
        tooltipTarget_ = nullptr;
        return;
    }
    // 1.11.2 (R1112-6) : muettes pendant la demonstration d'un tutoriel. L'attente repart de
    // zero : rendues a l'« A toi », elles ne s'ouvrent pas d'un coup sous le pointeur laisse la.
    if (g_tooltipsMuted) {
        hoverSince_ = fc.totalSeconds;
        return;
    }
    if (!tooltipTarget_ || fc.totalSeconds - hoverSince_ < 0.9 || !fc.theme || !root_) return;
    // Le widget retenu a l'Update a pu partir depuis (un onglet ferme par
    // l'Update de l'ecran) : l'arbre est redemande avant de le lire.
    if (tooltipTargetAt(*root_, mouse_) != tooltipTarget_) return;

    // Lot 7 : L'INFOBULLE SUIT CE QU'ELLE MONTRE. Le texte est redemande au
    // widget a chaque image (Widget::liveTooltip) au lieu d'etre celui du moment
    // ou elle s'est ouverte : une valeur en simulation, le numero de cycle, "en
    // marche" bougent sans qu'on bouge la souris. Le dessin est celui de
    // WidgetHost (les fenetres detachees) : plusieurs lignes ('\n'), une ligne
    // trop longue coupee entre deux mots (lot 16), a l'ecran, et le cote choisi
    // a l'ouverture garde tant qu'elle reste ouverte - elle ne saute pas quand
    // son texte change de largeur.
    const std::string text = tooltipTarget_->liveTooltip(mouse_);
    if (text.empty()) return;
    ui::TooltipPlacement placed{tipPlaced_, tipLeft_, tipAbove_, tipMouse_};
    ui::WidgetHost::paintTooltip(r, *fc.theme, fc.surface, mouse_, text, &placed);
    tipPlaced_ = placed.valid;
    tipLeft_   = placed.left;
    tipAbove_  = placed.above;
    tipMouse_  = placed.mouse;
}

void WidgetMenu::Render(gfx::IRenderer& r, const FrameContext& fc) {
    if (!root_) return;
    // Un ecran pousse sous un dialogue dans la meme image n'a jamais eu
    // d'Update (le dialogue ne laisse pas passer les mises a jour) : sans
    // taille, il se dessinait vide derriere le voile du dialogue.
    if (root_->bounds().empty()) {
        root_->setBounds({0.f, 0.f, fc.surface.w, fc.surface.h});
        root_->layout();
    }

    std::vector<ui::Widget*> overlays;
    const ui::PaintContext ctx{r, *fc.theme, {0.f, 0.f, fc.surface.w, fc.surface.h},
                               fc.totalSeconds, &overlays};
    root_->render(ctx);

    // Second pass: dropdown popups and anything else that must escape its
    // parent's clip rectangle, painted in the order they were requested.
    for (auto* w : overlays) w->paintTopMost(ctx);

    drawTooltip(r, fc);      // above everything, including the popups
}

ui::EventResult WidgetMenu::HandleEvent(const ui::InputEvent& ev) {
    if (!root_) return ui::EventResult::Ignored;

    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) mouse_ = m->pos;

    // A widget painting on top of everything - an open dropdown list - also
    // gets the event first. Without this the popup is drawn over the table
    // below it but the table receives the click, because dispatch order is
    // tree order and the table happens to come later.
    if (ui::Widget* overlay = root_->findOverlayOwner())
        if (overlay->dispatch(ev) == ui::EventResult::Consumed)
            return ui::EventResult::Consumed;

    // Tab traversal is a menu-level concern: the focused widget gets first
    // refusal, and only unclaimed Tab presses move the focus.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Tab) {
        if (root_->dispatch(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
        focus_->focusNext(k->mods.shift);
        return ui::EventResult::Consumed;
    }
    return root_->dispatch(ev);
}

void WidgetMenu::OnEnter() { onEnter(); }

void WidgetMenu::OnExit() {
    onExit();
    focus_->clear();
}

} // namespace menu
