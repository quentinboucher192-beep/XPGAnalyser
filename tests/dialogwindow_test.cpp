// tests/dialogwindow_test.cpp - lot API 8 : les dialogues dans la fenetre detachee.
//
//   dialogwindow_test
//
//  Sans ecran : la pile (MenuManager) et des dialogues d'essai, des fenetres
//  "detachees" simulees par leur numero et la taille de leur surface.
//    1. le choix de la fenetre d'un dialogue : celle en cours a la demande
//       (WindowScope), la principale sans elle ; un ecran, un overlay, une
//       fenetre inconnue : la principale ; sans fenetres declarees, comme avant ;
//    2. la route des evenements : a ce que montre la fenetre, a la pile, a
//       personne (la question attend ailleurs) ;
//    3. chacun a sa taille : la mise a jour a la taille de SA surface (le
//       dialogue s'y centre), le dessin de la principale sans lui, celui de sa
//       fenetre avec son voile ;
//    4. un evenement (un script) va au dialogue ou qu'il soit, traite dans sa
//       fenetre (sa surface, et un dialogue qu'il demande reste chez elle) ;
//       Echap l'annule ; le rappel onClose se fait dans sa fenetre ;
//    5. la migration : la fenetre se ferme, le dialogue revient dans la
//       principale, intact et remis en page ; les demandes en attente suivent ;
//       une fenetre disparue sans le dire rend ses dialogues a la mise a jour.
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Widget.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

using menu::MenuManager;
using WindowId = MenuManager::WindowId;
using Route = MenuManager::Route;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

constexpr gfx::Color kScrim{0, 0, 0, 120};

// Un renderer qui ne dessine rien : il compte les voiles (le noir a 120 sur toute la surface).
class FakeRenderer final : public gfx::IRenderer {
public:
    explicit FakeRenderer(gfx::Size s) : size_(s) {}
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect& r, gfx::Color c) override {
        if (c.r == kScrim.r && c.g == kScrim.g && c.b == kScrim.b && c.a == kScrim.a) {
            ++scrims;
            lastScrim = r;
        }
    }
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId) const override {
        return {8.f * static_cast<float>(s.size()), 16.f, 12.f, 4.f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId) const override { return 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size_; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId, float maxWidth) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, maxWidth) / 8.f));
    }

    int       scrims{0};
    gfx::Rect lastScrim{};

private:
    gfx::Size size_;
};

// Le corps d'un dialogue d'essai : un cadre de 300 x 200 centre dans la surface
// (comme DialogFrame), qui note ce qu'il voit quand on clique.
class Card final : public ui::Widget {
public:
    explicit Card(MenuManager& mm) : mm_(mm) {}
    gfx::Rect             panel{};
    int                   clicks{0};
    WindowId              clickedIn{999};
    gfx::Size             surfaceAtClick{};
    std::function<void()> onClick;

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(300.f, r.w - 24.f), h = std::min(200.f, r.h - 24.f);
        panel = {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h};
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (!std::holds_alternative<ui::MouseDown>(ev)) return ui::EventResult::Ignored;
        ++clicks;
        clickedIn = mm_.currentWindow();
        surfaceAtClick = ui::surfaceSize();
        if (onClick) onClick();
        return ui::EventResult::Consumed;
    }

private:
    MenuManager& mm_;
};

class TestDialog final : public menu::WidgetMenu {
public:
    TestDialog(std::string id, MenuManager& mm) : menu::WidgetMenu(std::move(id)), mm_(mm) {}
    [[nodiscard]] menu::MenuTraits traits() const override {
        menu::MenuTraits t;
        t.kind = menu::MenuKind::Dialog;
        t.rendersBelow = true;
        t.blocksInput = true;
        t.dimsBelow = true;
        return t;
    }
    void Update(const menu::FrameContext& f) override {
        ++updates;
        updatedAt = f.surface;
        updatedIn = mm_.currentWindow();
        menu::WidgetMenu::Update(f);
    }
    void Render(gfx::IRenderer& r, const menu::FrameContext& f) override {
        ++renders;
        renderedAt = f.surface;
        menu::WidgetMenu::Render(r, f);
    }
    Card*       card{nullptr};
    int         updates{0}, renders{0};
    gfx::Size   updatedAt{}, renderedAt{};
    WindowId    updatedIn{999};
    std::string typed;          // ce que "l'utilisateur" y a mis : doit survivre au retour

protected:
    core::Status buildUi() override {
        auto c = std::make_unique<Card>(mm_);
        card = c.get();
        setRoot(std::move(c));
        return core::ok();
    }

private:
    MenuManager& mm_;
};

class FakeScreen final : public menu::IMenu {
public:
    explicit FakeScreen(std::string id) : id_(std::move(id)) {}
    core::Status Initialize() override { return core::ok(); }
    void Update(const menu::FrameContext&) override { ++updates; }
    void Render(gfx::IRenderer&, const menu::FrameContext&) override { ++renders; }
    ui::EventResult HandleEvent(const ui::InputEvent&) override {
        ++events;
        return ui::EventResult::Ignored;
    }
    void OnEnter() override {}
    void OnExit() override {}
    [[nodiscard]] menu::MenuId id() const override { return id_; }
    int updates{0}, renders{0}, events{0};

private:
    menu::MenuId id_;
};

menu::MenuFactory factory() {
    menu::MenuFactory f;
    f.add("analysis", [] { return menu::MenuPtr(std::make_unique<FakeScreen>("analysis")); });
    f.add("help", [] { return menu::MenuPtr(std::make_unique<FakeScreen>("help")); });
    return f;
}

bool near(gfx::Rect r, float x, float y, float w, float h) {
    return std::fabs(r.x - x) < 0.5f && std::fabs(r.y - y) < 0.5f && std::fabs(r.w - w) < 0.5f && std::fabs(r.h - h) < 0.5f;
}

bool sameSize(gfx::Size a, float w, float h) { return std::fabs(a.w - w) < 0.5f && std::fabs(a.h - h) < 0.5f; }

gfx::Point centreOf(gfx::Rect r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

} // namespace

int main() {
    const ui::Theme theme = ui::Theme::light();
    constexpr WindowId kVariables = 7, kSimulation = 9;
    const gfx::Size mainSize{1536.f, 1024.f};
    // Les fenetres "detachees" : leur numero, la taille de leur surface.
    std::map<WindowId, gfx::Size> windows{{kVariables, {800.f, 600.f}}, {kSimulation, {640.f, 480.f}}};
    const auto surfaces = [&windows](WindowId id) -> std::optional<gfx::Size> {
        const auto it = windows.find(id);
        if (it == windows.end()) return std::nullopt;
        return it->second;
    };
    const menu::FrameContext mainFc{1.0 / 30.0, 1.0, &theme, mainSize, 1.f};

    std::printf("1. Le choix de la fenetre\n");
    MenuManager mm(factory());
    mm.setWindowSurfaces(surfaces);
    mm.PushMenu("analysis");
    mm.applyPending();
    mm.Update(mainFc);
    auto* screen = dynamic_cast<FakeScreen*>(mm.top());
    check(screen != nullptr && mm.currentWindow() == MenuManager::kMainWindow, "au depart : l'ecran d'analyse, la fenetre principale en cours");
    check(mm.routeFor(MenuManager::kMainWindow) == Route::Content && mm.routeFor(kVariables) == Route::Content,
          "rien n'attend : chaque fenetre a ce qu'elle montre");

    auto owned = std::make_unique<TestDialog>("dialog.csv", mm);
    TestDialog* csv = owned.get();
    int csvClosed = 0;
    menu::DialogResult csvResult;
    {
        const MenuManager::WindowScope in(mm, kVariables);
        check(mm.currentWindow() == kVariables, "WindowScope : la fenetre des variables est en cours");
        {
            const MenuManager::WindowScope nested(mm, kSimulation);
            check(mm.currentWindow() == kSimulation, "une portee dans une portee");
        }
        check(mm.currentWindow() == kVariables, "... qui rend la precedente en sortant");
        mm.ShowDialog(std::move(owned), [&](const menu::DialogResult& r) {
            ++csvClosed;
            csvResult = r;
        });
    }
    check(mm.currentWindow() == MenuManager::kMainWindow, "hors de la portee : la principale");
    check(mm.layersIn(kVariables) == 1, "la demande (pas encore appliquee) compte deja pour sa fenetre");
    mm.applyPending();
    check(mm.top() == csv && mm.windowOf(csv) == kVariables, "le dialogue demande depuis la fenetre detachee est a elle");
    check(mm.windowOf(screen) == MenuManager::kMainWindow, "l'ecran reste a la principale");

    std::printf("2. La route des evenements\n");
    check(mm.questionWaiting() && mm.questionWindow() == kVariables, "une question attend, dans la fenetre des variables");
    check(mm.routeFor(kVariables) == Route::Layers, "ses evenements vont a la pile (au dialogue)");
    check(mm.routeFor(MenuManager::kMainWindow) == Route::Elsewhere, "la principale attend (ses evenements ne vont a personne)");
    check(mm.routeFor(kSimulation) == Route::Elsewhere, "une autre fenetre detachee attend aussi");

    std::printf("3. Chacun a sa taille, chacun chez soi\n");
    mm.Update(mainFc);
    check(csv->updates == 1 && sameSize(csv->updatedAt, 800.f, 600.f), "mis a jour a la taille de SA surface (800 x 600)");
    check(csv->updatedIn == kVariables, "... et dans sa fenetre");
    check(csv->card && near(csv->card->panel, 250.f, 200.f, 300.f, 200.f), "centre sur sa fenetre : (250, 200) dans 800 x 600");
    check(screen->updates == 1, "l'ecran dessous ne tourne pas pendant la question (comme sous un dialogue ordinaire)");
    FakeRenderer mainR(mainSize);
    mm.Render(mainR, mainFc);
    check(screen->renders == 1 && csv->renders == 0 && mainR.scrims == 0,
          "la principale dessine son ecran, sans le dialogue ni son voile");
    FakeRenderer varsR(windows[kVariables]);
    const menu::FrameContext varsFc{1.0 / 30.0, 1.0, &theme, windows[kVariables], 1.f};
    mm.RenderWindow(kVariables, varsR, varsFc);
    check(csv->renders == 1 && sameSize(csv->renderedAt, 800.f, 600.f), "sa fenetre le dessine, a sa taille");
    check(varsR.scrims == 1 && near(varsR.lastScrim, 0.f, 0.f, 800.f, 600.f), "... par-dessus sa page, avec son voile");
    FakeRenderer simR(windows[kSimulation]);
    mm.RenderWindow(kSimulation, simR, {1.0 / 30.0, 1.0, &theme, windows[kSimulation], 1.f});
    check(simR.scrims == 0 && csv->renders == 1, "une autre fenetre ne le dessine pas");

    std::printf("4. Les evenements, les scripts, Echap, le rappel\n");
    TestDialog* sub = nullptr;
    csv->card->onClick = [&] {
        auto d = std::make_unique<TestDialog>("dialog.sous", mm);
        sub = d.get();
        mm.ShowDialog(std::move(d), {});
    };
    const gfx::Point inside = centreOf(csv->card->panel);
    // Un evenement sans fenetre (un script, un essai) : au haut de la pile, ou qu'il soit.
    (void)mm.HandleEvent(ui::MouseDown{inside, ui::MouseButton::Left, 1, {}});
    check(csv->card->clicks == 1, "un clic du script va au dialogue de la fenetre detachee");
    check(csv->card->clickedIn == kVariables, "... traite dans SA fenetre (fenetre en cours)");
    check(sameSize(csv->card->surfaceAtClick, 800.f, 600.f), "... a la taille de SA surface (une liste s'y ouvre dans ses bords)");
    check(screen->events == 0, "l'ecran dessous n'a rien recu (le dialogue reste modal)");
    mm.applyPending();
    check(sub != nullptr && mm.top() == sub && mm.windowOf(sub) == kVariables,
          "un dialogue demande par le dialogue de la fenetre s'y ouvre aussi");
    mm.Update(mainFc);
    // Echap : il ferme le dialogue du dessus (le sous-dialogue), dans sa fenetre.
    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
    mm.applyPending();
    check(mm.top() == csv, "Echap ferme le sous-dialogue ; le premier reprend");
    // Le rappel onClose : ce qu'il ouvre va dans la fenetre du dialogue ferme.
    auto next = std::make_unique<TestDialog>("dialog.suite", mm);
    TestDialog* suite = next.get();
    auto again = std::make_unique<TestDialog>("dialog.question", mm);
    TestDialog* question = again.get();
    {
        const MenuManager::WindowScope in(mm, kVariables);
        mm.ShowDialog(std::move(again), [&](const menu::DialogResult&) { mm.ShowDialog(std::move(next), {}); });
    }
    mm.applyPending();
    check(mm.top() == question && mm.windowOf(question) == kVariables, "une question de plus, dans la fenetre des variables");
    mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
    mm.applyPending();       // ferme la question ; son rappel demande la suite
    mm.applyPending();       // la suite
    check(mm.top() == suite && mm.windowOf(suite) == kVariables, "le rappel onClose ouvre la suite dans la meme fenetre");
    mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    mm.applyPending();
    check(mm.top() == csv, "la suite fermee, le dialogue des variables est de nouveau au-dessus");

    std::printf("5. La fenetre se ferme : le dialogue revient, intact\n");
    csv->typed = "exports/variables.csv";
    const int updatesBefore = csv->updates;
    // Une demande faite dans la fenetre, pas encore appliquee : elle suit aussi.
    auto late = std::make_unique<TestDialog>("dialog.tard", mm);
    TestDialog* lateDialog = late.get();
    {
        const MenuManager::WindowScope in(mm, kVariables);
        mm.ShowDialog(std::move(late), {});
    }
    windows.erase(kVariables);     // la fenetre se ferme (sa croix, "Ramener", la sortie)...
    const std::size_t moved = mm.moveWindow(kVariables, MenuManager::kMainWindow);   // ...et rend ses couches
    check(moved == 2, "moveWindow : le dialogue et la demande en attente (2)");
    check(mm.windowOf(csv) == MenuManager::kMainWindow, "le dialogue est dans la principale");
    check(csv->typed == "exports/variables.csv" && csv->card && csv->card->clicks == 1, "... intact (le meme objet, ce qu'on y avait mis)");
    check(near(csv->card->panel, 618.f, 412.f, 300.f, 200.f), "... remis en page tout de suite : centre dans 1536 x 1024");
    check(mm.layersIn(kVariables) == 0, "plus rien dans la fenetre fermee");
    mm.applyPending();
    check(mm.top() == lateDialog && mm.windowOf(lateDialog) == MenuManager::kMainWindow, "la demande en attente s'ouvre dans la principale");
    check(mm.routeFor(MenuManager::kMainWindow) == Route::Layers, "la principale a de nouveau la question");
    mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    mm.applyPending();
    mm.Update(mainFc);
    check(csv->updates == updatesBefore + 1 && sameSize(csv->updatedAt, 1536.f, 1024.f) && csv->updatedIn == MenuManager::kMainWindow,
          "mis a jour a la taille de la principale, dans la principale");
    FakeRenderer mainR2(mainSize);
    mm.Render(mainR2, mainFc);
    check(csv->renders == 2 && mainR2.scrims == 1, "dessine dans la principale, avec son voile");
    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
    mm.applyPending();
    check(csvClosed == 1 && csvResult.button == menu::DialogResult::Button::Cancel, "Echap l'annule ; son rappel est appele une fois");
    check(!mm.questionWaiting() && mm.routeFor(MenuManager::kMainWindow) == Route::Content, "plus de question : chaque fenetre reprend");

    std::printf("6. Une fenetre disparue sans le dire ; les ecrans ; une fenetre inconnue\n");
    auto simDialog = std::make_unique<TestDialog>("dialog.forcages", mm);
    TestDialog* forcing = simDialog.get();
    {
        const MenuManager::WindowScope in(mm, kSimulation);
        mm.ShowDialog(std::move(simDialog), {});
        mm.PushMenu("help");                          // un ecran demande depuis la fenetre
    }
    mm.applyPending();
    check(mm.windowOf(forcing) == kSimulation, "un dialogue dans la fenetre de la simulation");
    check(mm.top() && mm.top()->id() == "help" && mm.windowOf(mm.top()) == MenuManager::kMainWindow,
          "un ecran demande depuis une fenetre detachee va a la principale (il la remplit)");
    check(mm.routeFor(kSimulation) == Route::Content, "un ecran au-dessus : rien n'attend (comme avant)");
    {
        FakeRenderer simR2(windows[kSimulation]);
        mm.RenderWindow(kSimulation, simR2, {1.0 / 30.0, 1.0, &theme, windows[kSimulation], 1.f});
        check(forcing->renders == 0 && simR2.scrims == 0, "... son dialogue, reste sous l'ecran, n'y est pas montre (la page est servie)");
    }
    mm.PopMenu();
    mm.applyPending();
    check(mm.top() == forcing && mm.routeFor(kSimulation) == Route::Layers, "l'ecran parti, la question est a la simulation");
    {
        FakeRenderer simR3(windows[kSimulation]);
        mm.RenderWindow(kSimulation, simR3, {1.0 / 30.0, 1.0, &theme, windows[kSimulation], 1.f});
        check(forcing->renders == 1 && simR3.scrims == 1, "... et de nouveau montre dans sa fenetre, avec son voile");
    }
    windows.erase(kSimulation);                       // fermee sans rendre ses couches
    mm.Update(mainFc);
    check(mm.windowOf(forcing) == MenuManager::kMainWindow && sameSize(forcing->updatedAt, 1536.f, 1024.f),
          "la mise a jour suivante le rend a la principale, a sa taille");
    check(mm.routeFor(MenuManager::kMainWindow) == Route::Layers, "la principale n'est plus bloquee");
    mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    mm.applyPending();
    {
        const MenuManager::WindowScope in(mm, 42);    // une fenetre que personne ne connait
        mm.ShowDialog(std::make_unique<TestDialog>("dialog.perdu", mm), {});
    }
    mm.applyPending();
    check(mm.top() && mm.top()->id() == "dialog.perdu" && mm.windowOf(mm.top()) == MenuManager::kMainWindow,
          "une fenetre inconnue : le dialogue s'ouvre dans la principale");
    mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    mm.applyPending();

    std::printf("7. Sans fenetre detachee : comme avant\n");
    {
        MenuManager plain(factory());               // aucune fenetre declaree
        plain.PushMenu("analysis");
        plain.applyPending();
        auto d = std::make_unique<TestDialog>("dialog.message", plain);
        TestDialog* message = d.get();
        {
            const MenuManager::WindowScope in(plain, kVariables);
            plain.ShowDialog(std::move(d), {});
        }
        plain.applyPending();
        check(plain.windowOf(message) == MenuManager::kMainWindow, "une fenetre que la pile ne connait pas : la principale");
        plain.Update(mainFc);
        FakeRenderer r(mainSize);
        plain.Render(r, mainFc);
        check(message->renders == 1 && r.scrims == 1 && sameSize(message->updatedAt, 1536.f, 1024.f),
              "dessine dans la principale, voile compris, a sa taille");
        check(plain.routeFor(MenuManager::kMainWindow) == Route::Layers, "la question est a la principale");
        (void)plain.HandleEvent(ui::MouseDown{centreOf(message->card->panel), ui::MouseButton::Left, 1, {}});
        check(message->card->clicks == 1 && message->card->clickedIn == MenuManager::kMainWindow, "un clic : a lui, dans la principale");
    }

    std::printf("\n%s : %d echec(s)\n", failures ? "ECHEC" : "OK", failures);
    return failures ? 1 : 0;
}
