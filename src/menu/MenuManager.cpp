#include "MenuManager.hpp"

#include "../ui/Widget.hpp"      // lot API 8 : ui::SurfaceScope, la remise en page d'une couche
#include "../core/CallTrail.hpp"  // 1.10.2 (CR) : les ecrans et dialogues ouverts / fermes, dans le journal interne

#include <algorithm>
#include <cassert>
#include <optional>

namespace menu {

// ---- Lot API 8 : les dialogues dans la fenetre detachee ----
namespace {
// Une couche traitee dans SA fenetre : ce qu'elle demande (un autre dialogue)
// y va, et ce qui s'ouvre dans ses bords (une liste deroulante) prend la
// taille de sa surface, pas celle de la fenetre principale.
class LayerScope {
public:
    LayerScope(MenuManager& m, MenuManager::WindowId w, gfx::Size surface) : window_(m, w) {
        if (w != MenuManager::kMainWindow && surface.w >= 1.f && surface.h >= 1.f) surface_.emplace(surface);
    }
private:
    MenuManager::WindowScope        window_;
    std::optional<ui::SurfaceScope> surface_;
};

// Une couche qui change de fenetre se remet en page a la taille de la
// nouvelle surface (sans attendre sa mise a jour : sous un autre dialogue,
// elle n'en a pas).
void relayout(IMenu& menu, gfx::Size surface) {
    if (surface.w < 1.f || surface.h < 1.f) return;
    if (ui::Widget* root = menu.widgetRoot()) {
        root->setBounds({0.f, 0.f, surface.w, surface.h});
        root->layout();
    }
}
} // namespace
// ---- fin lot API 8 ----

// ---------------------------------------------------------------- factory ---
core::Result<MenuPtr> MenuFactory::create(const MenuId& id) const {
    auto it = creators_.find(id);
    if (it == creators_.end())
        return core::fail(core::ErrorCode::InvalidArgument, "no menu registered as '" + id + "'");
    auto m = it->second();
    if (!m) return core::fail(core::ErrorCode::InvalidArgument, "factory for '" + id + "' returned null");
    return m;
}

std::vector<MenuId> MenuFactory::childrenOf(std::string_view parentId) const {
    // "settings" -> {"settings.graphics", "settings.audio", "settings.theme"},
    // but not "settings.graphics.advanced": direct children only.
    std::vector<MenuId> out;
    const std::string prefix = std::string(parentId) + ".";
    for (const auto& [id, _] : creators_) {
        if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) continue;
        if (id.find('.', prefix.size()) != std::string::npos) continue;
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// -------------------------------------------------------- scoped instance ---
MenuManager::ScopedInstance::ScopedInstance(MenuManager& m) {
    assert(s_instance == nullptr && "a second MenuManager tried to claim the global slot");
    s_instance = &m;
}
MenuManager::ScopedInstance::~ScopedInstance() { s_instance = nullptr; }

// ---------------------------------------------------------------- manager ---
MenuManager::MenuManager(MenuFactory factory) : factory_(std::move(factory)) {}

MenuManager::~MenuManager() {
    // Unwind top-down so OnExit() runs in the reverse order of OnEnter().
    CloseAllOverlays();
    while (!stack_.empty()) popEntry();
}

void MenuManager::enqueue(Request r) {
    r.window = currentWindow_;      // lot API 8 : la fenetre ou le geste a ete fait
    pending_.push_back(std::move(r));
}

void MenuManager::PushMenu(const MenuId& id)   { enqueue({Op::Push, id, nullptr, {}, {}, {}}); }
void MenuManager::PushMenu(MenuPtr m)          { enqueue({Op::Push, {}, std::move(m), {}, {}, {}}); }
void MenuManager::PopMenu()                    { enqueue({Op::Pop, {}, nullptr, {}, {}, {}}); }
void MenuManager::PopToRoot()                  { enqueue({Op::PopToRoot, {}, nullptr, {}, {}, {}}); }
void MenuManager::PopTo(const MenuId& id)      { enqueue({Op::PopTo, id, nullptr, {}, {}, {}}); }
void MenuManager::ReplaceMenu(const MenuId& id){ enqueue({Op::Replace, id, nullptr, {}, {}, {}}); }
void MenuManager::ReplaceMenu(MenuPtr m)       { enqueue({Op::Replace, {}, std::move(m), {}, {}, {}}); }
void MenuManager::SwitchMenu(const MenuId& id) { enqueue({Op::Switch, id, nullptr, {}, {}, {}}); }
void MenuManager::PreviousMenu()               { enqueue({Op::Previous, {}, nullptr, {}, {}, {}}); }

void MenuManager::ShowModal(MenuPtr m) { enqueue({Op::ShowModal, {}, std::move(m), {}, {}, {}}); }

void MenuManager::ShowDialog(MenuPtr d, std::function<void(const DialogResult&)> onClose) {
    enqueue({Op::ShowDialog, {}, std::move(d), std::move(onClose), {}, {}});
}
void MenuManager::CloseDialog(const DialogResult& r) {
    enqueue({Op::CloseDialog, {}, nullptr, {}, r, {}});
}
void MenuManager::ShowOverlay(MenuPtr o, std::string tag) {
    enqueue({Op::ShowOverlay, {}, std::move(o), {}, {}, std::move(tag)});
}
void MenuManager::CloseOverlay(std::string_view tag) {
    enqueue({Op::CloseOverlay, {}, nullptr, {}, {}, std::string(tag)});
}
void MenuManager::CloseAllOverlays() {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        it->menu->OnExit();
        menuExited->emit(it->menu->id());
    }
    overlays_.clear();
}

core::Status MenuManager::pushEntry(MenuPtr m, std::function<void(const DialogResult&)> onClose,
                                    std::string tag, WindowId window) {
    if (!m) return core::fail(core::ErrorCode::InvalidArgument, "null menu");
    XPG_TRACE(Dialog, "ouvre %s%s%s", m->id().c_str(), tag.empty() ? "" : " #", tag.c_str());   // 1.10.2 (CR)
    m->attach(this);
    {
        // Lot API 8 : construit a la taille de la fenetre ou il va (un formulaire
        // se tient dans une fenetre detachee plus etroite que la principale).
        const auto size = window != kMainWindow ? surfaceOf(window) : std::nullopt;
        const LayerScope in(*this, size ? window : kMainWindow, size ? *size : gfx::Size{});
        if (auto r = m->Initialize(); !r) return r;
    }

    const auto traits = m->traits();
    if (!stack_.empty() && traits.kind == MenuKind::Screen) stack_.back().menu->OnExit();

    Entry e{std::move(m), traits, std::move(onClose), std::move(tag)};
    // Lot API 8 : la fenetre de la couche - celle de la demande, sauf pour un
    // ecran ou un overlay (ils remplissent la principale) et pour une fenetre
    // qui n'est plus.
    if (window != kMainWindow && traits.kind != MenuKind::Screen && traits.kind != MenuKind::Overlay)
        if (const auto surface = surfaceOf(window)) {
            e.window  = window;
            e.surface = *surface;
        }
    if (traits.kind == MenuKind::Overlay) {
        overlays_.push_back(std::move(e));
        overlays_.back().menu->OnEnter();
        menuEntered->emit(overlays_.back().menu->id());
    } else {
        stack_.push_back(std::move(e));
        history_.push_back(stack_.back().menu->id());
        {
            const LayerScope in(*this, stack_.back().window, stack_.back().surface);   // lot API 8
            stack_.back().menu->OnEnter();
        }
        menuEntered->emit(stack_.back().menu->id());
    }
    return core::ok();
}

void MenuManager::popEntry() {
    if (stack_.empty()) return;
    auto entry = std::move(stack_.back());
    stack_.pop_back();
    XPG_TRACE(Dialog, "ferme %s", entry.menu->id().c_str());   // 1.10.2 (CR)
    entry.menu->OnExit();
    menuExited->emit(entry.menu->id());
    // Entry (and the menu) dies here, after the stack no longer references it.
    if (!stack_.empty() && stack_.back().traits.kind == MenuKind::Screen)
        stack_.back().menu->OnEnter();
}

void MenuManager::applyPending() {
    if (applying_) return;                 // re-entrancy guard
    applying_ = true;

    // Requests enqueued *by* a request are handled on the next frame, keeping
    // each frame's navigation deterministic and bounded.
    std::deque<Request> batch;
    batch.swap(pending_);

    for (auto& r : batch) {
        switch (r.op) {
        case Op::Push:
            if (r.menu) (void)pushEntry(std::move(r.menu), {}, {}, r.window);   // lot API 8 : un dialogue pousse garde sa fenetre
            else if (auto m = factory_.create(r.id)) (void)pushEntry(std::move(*m));
            break;

        case Op::Pop:
            if (!stack_.empty() && stack_.back().menu->canClose()) popEntry();
            break;

        case Op::PopToRoot:
            while (stack_.size() > 1 && stack_.back().menu->canClose()) popEntry();
            break;

        case Op::PopTo:
            while (stack_.size() > 1 && stack_.back().menu->id() != r.id
                   && stack_.back().menu->canClose())
                popEntry();
            break;

        case Op::Replace: {
            auto next = r.menu ? std::move(r.menu)
                               : (factory_.create(r.id) ? std::move(*factory_.create(r.id)) : nullptr);
            if (!next) break;
            if (!stack_.empty()) {
                if (!stack_.back().menu->canClose()) break;
                popEntry();
            }
            (void)pushEntry(std::move(next));
            break;
        }

        case Op::Switch: {
            auto next = factory_.create(r.id);
            if (!next) break;                       // keep the current stack on a bad id
            CloseAllOverlays();
            while (!stack_.empty()) popEntry();
            history_.clear();
            (void)pushEntry(std::move(*next));
            break;
        }

        case Op::Previous: {
            // history_ is the visit log; back() is the current menu.
            if (history_.size() < 2) break;
            history_.pop_back();
            const MenuId target = history_.back();
            history_.pop_back();                    // pushEntry re-appends it
            if (!stack_.empty()) {
                if (!stack_.back().menu->canClose()) break;
                popEntry();
            }
            if (auto m = factory_.create(target)) (void)pushEntry(std::move(*m));
            break;
        }

        case Op::ShowModal:
        case Op::ShowDialog:
            (void)pushEntry(std::move(r.menu), std::move(r.onClose), {}, r.window);   // lot API 8 : sa fenetre
            break;

        case Op::CloseDialog: {
            // UN DIALOGUE, PAS UN ECRAN. Deux fermetures demandees dans la meme
            // image (un double-clic sur OK) fermaient le dialogue puis l'ecran
            // qui le portait : une fenetre vide, sans rien pour en sortir.
            if (stack_.empty() || stack_.back().traits.kind == MenuKind::Screen) break;
            auto onClose = stack_.back().onClose;
            // Lot API 8 : le rappel se fait dans la fenetre du dialogue ferme -
            // ce qu'il ouvre a son tour (une erreur, la question suivante) s'y ouvre aussi.
            const WindowId  from    = stack_.back().window;
            const gfx::Size surface = stack_.back().surface;
            popEntry();
            if (onClose) {
                const LayerScope in(*this, from, surface);
                onClose(r.result);
            }
            break;
        }

        case Op::ShowOverlay:
            (void)pushEntry(std::move(r.menu), {}, std::move(r.tag));
            break;

        case Op::CloseOverlay: {
            auto it = std::find_if(overlays_.begin(), overlays_.end(),
                                   [&](const Entry& e) { return e.tag == r.tag; });
            if (it != overlays_.end()) {
                it->menu->OnExit();
                menuExited->emit(it->menu->id());
                overlays_.erase(it);
            }
            break;
        }
        }
    }
    applying_ = false;
}

std::size_t MenuManager::firstRenderedLayer() const noexcept {
    if (stack_.empty()) return 0;
    std::size_t i = stack_.size() - 1;
    while (i > 0 && stack_[i].traits.rendersBelow) --i;
    return i;
}

std::size_t MenuManager::firstUpdatedLayer() const noexcept {
    if (stack_.empty()) return 0;
    std::size_t i = stack_.size() - 1;
    while (i > 0 && stack_[i].traits.updatesBelow) --i;
    return i;
}

void MenuManager::Update(const FrameContext& fc) {
    // Lot API 8 : la taille de la principale (une couche qui y revient s'y remet
    // en page) ; une fenetre fermee sans rendre ses couches les rend ici.
    mainSurface_ = fc.surface;
    adoptLostWindows();
    for (std::size_t i = firstUpdatedLayer(); i < stack_.size(); ++i) {
        Entry& e = stack_[i];
        if (e.window == kMainWindow) {
            e.menu->Update(fc);
            continue;
        }
        // Lot API 8 : une couche d'une autre fenetre, a la taille de SA surface
        // (un dialogue s'y centre et s'y met en page) et dans cette fenetre.
        if (const auto s = surfaceOf(e.window); s && s->w >= 1.f && s->h >= 1.f) e.surface = *s;
        FrameContext own = fc;
        if (e.surface.w >= 1.f && e.surface.h >= 1.f) own.surface = e.surface;
        const LayerScope in(*this, e.window, own.surface);
        e.menu->Update(own);
    }
    for (auto& o : overlays_) o.menu->Update(fc);
}

void MenuManager::Render(gfx::IRenderer& r, const FrameContext& fc) {
    // Lot API 8 : les couches de la fenetre principale seulement ; celles d'une
    // autre fenetre se dessinent dans la leur (RenderWindow).
    mainSurface_ = fc.surface;
    for (const std::size_t i : renderedLayers(kMainWindow)) {
        if (stack_[i].traits.dimsBelow && i > 0)
            r.fillRect({0, 0, fc.surface.w, fc.surface.h}, gfx::Color{0, 0, 0, 120});
        stack_[i].menu->Render(r, fc);
    }
    for (auto& o : overlays_) o.menu->Render(r, fc);   // overlays always on top
}

ui::EventResult MenuManager::HandleEvent(const ui::InputEvent& ev) {
    // Overlays first, newest to oldest: a dropdown popup must see the click
    // before the screen underneath it does.
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it)
        if (it->menu->HandleEvent(ev) == ui::EventResult::Consumed)
            return ui::EventResult::Consumed;

    // Escape backs out of the top layer, unless it claims the key itself. The
    // traits flag existed but nothing acted on it, so a pushed screen could be
    // entered and never left without a mouse.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && k->key == ui::Key::Escape && !stack_.empty()) {
        auto& top = stack_.back();
        {
            const LayerScope in(*this, top.window, top.surface);   // lot API 8 : dans SA fenetre
            if (top.menu->HandleEvent(ev) == ui::EventResult::Consumed)
                return ui::EventResult::Consumed;
        }
        if (top.traits.closableWithEscape) {
            if (top.traits.kind == MenuKind::Dialog)
                CloseDialog(DialogResult{DialogResult::Button::Cancel, {}});
            else if (stack_.size() > 1)
                PopMenu();
        }
        return ui::EventResult::Consumed;
    }

    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        const LayerScope in(*this, it->window, it->surface);   // lot API 8 : chaque couche dans SA fenetre
        if (it->menu->HandleEvent(ev) == ui::EventResult::Consumed)
            return ui::EventResult::Consumed;
        if (it->traits.blocksInput) return ui::EventResult::Consumed;   // modal barrier
    }
    return ui::EventResult::Ignored;
}

IMenu* MenuManager::top() const noexcept {
    return stack_.empty() ? nullptr : stack_.back().menu.get();
}

bool MenuManager::contains(const MenuId& id) const noexcept {
    return std::any_of(stack_.begin(), stack_.end(),
                       [&](const Entry& e) { return e.menu->id() == id; });
}

std::vector<MenuId> MenuManager::path() const {
    std::vector<MenuId> p;
    p.reserve(stack_.size());
    for (const auto& e : stack_) p.push_back(e.menu->id());
    return p;
}

// =============================================================================
//  Lot API 8 : les dialogues dans la fenetre detachee
// =============================================================================
MenuManager::WindowScope::WindowScope(MenuManager& m, WindowId w) noexcept
    : manager_(m), previous_(m.currentWindow_) {
    m.currentWindow_ = w;
}

MenuManager::WindowScope::~WindowScope() { manager_.currentWindow_ = previous_; }

bool MenuManager::questionWaiting() const noexcept {
    if (stack_.empty()) return false;
    const MenuTraits& t = stack_.back().traits;
    return t.kind != MenuKind::Screen && t.blocksInput;
}

MenuManager::WindowId MenuManager::questionWindow() const noexcept {
    return questionWaiting() ? stack_.back().window : kMainWindow;
}

MenuManager::Route MenuManager::routeFor(WindowId w) const noexcept {
    if (!questionWaiting()) return Route::Content;
    return stack_.back().window == w ? Route::Layers : Route::Elsewhere;
}

MenuManager::WindowId MenuManager::windowOf(const IMenu* m) const noexcept {
    for (const auto& e : stack_)
        if (e.menu.get() == m) return e.window;
    return kMainWindow;
}

std::size_t MenuManager::layersIn(WindowId w) const noexcept {
    std::size_t n = 0;
    for (const auto& e : stack_)
        if (e.window == w) ++n;
    // Une demande qui posera une couche (pas encore appliquee : l'image suivante).
    for (const auto& r : pending_)
        if (r.window == w && r.menu && (r.op == Op::ShowDialog || r.op == Op::ShowModal || r.op == Op::Push)) ++n;
    return n;
}

std::optional<gfx::Size> MenuManager::surfaceOf(WindowId w) const {
    if (w == kMainWindow) return mainSurface_;
    if (!windowSurfaces_) return std::nullopt;
    return windowSurfaces_(w);
}

std::vector<std::size_t> MenuManager::renderedLayers(WindowId w) const {
    // Du haut vers le bas, les couches de `w` : on s'arrete a la premiere qui
    // cache ce qui est dessous (un ecran). Comme firstRenderedLayer, par fenetre.
    // Un ecran de la principale pousse par-dessus (l'aide) suspend les
    // dialogues des autres fenetres restes dessous : ni montres ni servis
    // (routeFor), jusqu'a ce qu'il parte - comme dans la principale.
    std::vector<std::size_t> out;
    for (std::size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i].window != w) {
            if (stack_[i].traits.kind == MenuKind::Screen) break;
            continue;
        }
        out.push_back(i);
        if (!stack_[i].traits.rendersBelow) break;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::size_t MenuManager::moveWindow(WindowId from, WindowId to) {
    if (from == to) return 0;
    // Vers une fenetre qui n'est pas (plus) la : la principale.
    auto size = surfaceOf(to);
    if (!size) {
        to = kMainWindow;
        size = mainSurface_;
    }
    std::size_t moved = 0;
    for (auto& e : stack_) {
        if (e.window != from) continue;
        e.window = to;
        if (size->w >= 1.f && size->h >= 1.f) e.surface = *size;
        const LayerScope in(*this, to, e.surface);
        relayout(*e.menu, *size);
        ++moved;
    }
    for (auto& r : pending_)
        if (r.window == from) {
            r.window = to;
            ++moved;
        }
    // Ce qui sera demande dans la foulee (le rappel d'un dialogue de la
    // fenetre qui part) va a la fenetre d'arrivee.
    if (currentWindow_ == from) currentWindow_ = to;
    return moved;
}

void MenuManager::adoptLostWindows() {
    for (auto& e : stack_) {
        if (e.window == kMainWindow || surfaceOf(e.window)) continue;
        e.window = kMainWindow;
        relayout(*e.menu, mainSurface_);
    }
    for (auto& r : pending_)
        if (r.window != kMainWindow && !surfaceOf(r.window)) r.window = kMainWindow;
}

void MenuManager::RenderWindow(WindowId w, gfx::IRenderer& r, const FrameContext& fc) {
    if (w == kMainWindow) {
        Render(r, fc);
        return;
    }
    for (const std::size_t i : renderedLayers(w)) {
        Entry& e = stack_[i];
        if (fc.surface.w >= 1.f && fc.surface.h >= 1.f) e.surface = fc.surface;
        const LayerScope in(*this, w, fc.surface);
        // Dessous, la page de la fenetre : le voile, toujours (ce n'est pas le
        // bas de la pile, comme l'ecran de la principale).
        if (e.traits.dimsBelow) r.fillRect({0, 0, fc.surface.w, fc.surface.h}, gfx::Color{0, 0, 0, 120});
        e.menu->Render(r, fc);
    }
}

} // namespace menu
