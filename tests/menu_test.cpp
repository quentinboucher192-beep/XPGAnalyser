// Behavioural test for the navigation stack. No SDL, no window: the menu layer
// is deliberately free of platform dependencies.
#include "../src/menu/MenuManager.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

using namespace menu;

static std::ostringstream g_log;

class FakeMenu : public IMenu {
public:
    FakeMenu(MenuId id, MenuKind kind = MenuKind::Screen, bool closable = true)
        : id_(std::move(id)), closable_(closable) {
        traits_.kind = kind;
        traits_.rendersBelow = (kind != MenuKind::Screen);
        traits_.blocksInput  = (kind != MenuKind::Overlay);
        traits_.dimsBelow    = (kind == MenuKind::Modal || kind == MenuKind::Dialog);
    }
    core::Status Initialize() override { g_log << "init(" << id_ << ") "; return core::ok(); }
    void Update(const FrameContext&) override { ++updates; }
    void Render(gfx::IRenderer&, const FrameContext&) override { ++renders; }
    ui::EventResult HandleEvent(const ui::InputEvent&) override {
        ++events;
        return consume ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    void OnEnter() override { g_log << "enter(" << id_ << ") "; }
    void OnExit()  override { g_log << "exit("  << id_ << ") "; }
    [[nodiscard]] MenuId     id() const override { return id_; }
    [[nodiscard]] MenuTraits traits() const override { return traits_; }
    [[nodiscard]] bool canClose() const override { return closable_; }

    int updates{0}, renders{0}, events{0};
    bool consume{false};
private:
    MenuId     id_;
    MenuTraits traits_;
    bool       closable_;
};

static MenuFactory buildFactory() {
    MenuFactory f;
    auto screen = [](const char* id) { return [id] { return MenuPtr(std::make_unique<FakeMenu>(id)); }; };
    f.add("main",                     screen("main"));
    f.add("main.settings",            screen("main.settings"));
    f.add("main.settings.graphics",   screen("main.settings.graphics"));
    f.add("main.settings.audio",      screen("main.settings.audio"));
    f.add("main.settings.theme",      screen("main.settings.theme"));
    f.add("main.settings.graphics.advanced", screen("main.settings.graphics.advanced"));
    f.add("main.project",             screen("main.project"));
    f.add("main.analysis",            screen("main.analysis"));
    f.add("startup",                  screen("startup"));
    return f;
}

int main() {
    MenuManager mm(buildFactory());

    // --- direct children only, sorted: drives generated submenu lists -------
    auto kids = mm.factory().childrenOf("main.settings");
    assert(kids.size() == 3);
    assert(kids[0] == "main.settings.audio");
    assert(kids[2] == "main.settings.theme");

    // --- unlimited nesting --------------------------------------------------
    mm.PushMenu("main");
    mm.PushMenu("main.settings");
    mm.PushMenu("main.settings.graphics");
    mm.PushMenu("main.settings.graphics.advanced");
    mm.applyPending();
    assert(mm.depth() == 4);
    assert(mm.path().back() == "main.settings.graphics.advanced");
    assert(mm.contains("main.settings"));

    // --- pop / popTo --------------------------------------------------------
    mm.PopMenu();
    mm.applyPending();
    assert(mm.top()->id() == "main.settings.graphics");
    mm.PopTo("main");
    mm.applyPending();
    assert(mm.depth() == 1 && mm.top()->id() == "main");

    // --- replace keeps depth ------------------------------------------------
    mm.PushMenu("main.project");
    mm.applyPending();
    mm.ReplaceMenu("main.analysis");
    mm.applyPending();
    assert(mm.depth() == 2 && mm.top()->id() == "main.analysis");

    // --- previous walks the visit history, not the stack --------------------
    mm.PreviousMenu();
    mm.applyPending();
    assert(mm.top()->id() == "main.project");

    // --- switch clears everything ------------------------------------------
    mm.SwitchMenu("startup");
    mm.applyPending();
    assert(mm.depth() == 1 && mm.top()->id() == "startup");

    // --- canClose() vetoes a pop -------------------------------------------
    {
        MenuManager guarded(buildFactory());
        guarded.PushMenu("main");
        guarded.PushMenu(std::make_unique<FakeMenu>("dirty", MenuKind::Screen, /*closable*/ false));
        guarded.applyPending();
        guarded.PopMenu();
        guarded.applyPending();
        assert(guarded.depth() == 2 && "a menu that vetoes closing must stay");
    }

    // --- modal blocks input, overlay does not ------------------------------
    {
        MenuManager m2(buildFactory());
        auto  base    = std::make_unique<FakeMenu>("base");
        auto* basePtr = base.get();
        m2.PushMenu(std::move(base));
        m2.applyPending();

        auto  overlay    = std::make_unique<FakeMenu>("tooltip", MenuKind::Overlay);
        auto* overlayPtr = overlay.get();
        m2.ShowOverlay(std::move(overlay), "tooltip");
        m2.applyPending();

        m2.HandleEvent(ui::KeyDown{ui::Key::A, {}, false});
        assert(overlayPtr->events == 1 && basePtr->events == 1 && "overlay lets events through");

        auto  modal    = std::make_unique<FakeMenu>("confirm", MenuKind::Modal);
        auto* modalPtr = modal.get();
        m2.ShowModal(std::move(modal));
        m2.applyPending();

        m2.HandleEvent(ui::KeyDown{ui::Key::A, {}, false});
        assert(modalPtr->events == 1);
        assert(basePtr->events == 1 && "modal must swallow input aimed at layers below");
    }

    // --- dialog result reaches the caller ----------------------------------
    {
        MenuManager m3(buildFactory());
        m3.PushMenu("main");
        m3.applyPending();
        DialogResult captured;
        m3.ShowDialog(std::make_unique<FakeMenu>("save?", MenuKind::Dialog),
                      [&](const DialogResult& r) { captured = r; });
        m3.applyPending();
        assert(m3.depth() == 2);
        m3.CloseDialog(DialogResult{DialogResult::Button::Yes, "/tmp/MAST.XPG"});
        m3.applyPending();
        assert(m3.depth() == 1);
        assert(captured.accepted() && captured.payload == "/tmp/MAST.XPG");
    }

    // --- mutation from inside a callback is deferred, not immediate --------
    {
        MenuManager m4(buildFactory());
        m4.PushMenu("main");
        m4.applyPending();
        auto* top = m4.top();
        m4.PopMenu();                       // queued
        assert(m4.top() == top && "stack must not change mid-frame");
        m4.applyPending();
        assert(m4.depth() == 0);
    }

    std::cout << "menu lifecycle log: " << g_log.str().substr(0, 120) << "...\n";
    std::cout << "menu_test: all assertions passed\n";
    return 0;
}
