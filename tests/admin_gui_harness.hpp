// =============================================================================
//  tests/admin_gui_harness.hpp - 1.33.0 : dessiner les pages de l'application de
//  l'editeur A BLANC (sans fenetre, sans SDL) et les manipuler
// -----------------------------------------------------------------------------
//  Un renderer qui note les textes dessines (et rien d'autre), des services de
//  plate-forme qui mesurent (8 px par caractere a la taille 16), un ShellHost
//  d'essai (les resultats notes, les dialogues gardes, les fichiers choisis
//  donnes d'avance, le travail lent execute sur place ou sur un fil), et des
//  aides : poser une page a une taille, la dessiner, chercher un widget par son
//  identifiant, cliquer un bouton, taper dans un champ.
//  Avec admin_test_fixture.hpp (le poste Admin d'essai).
// =============================================================================
#pragma once

#include "admin_test_fixture.hpp"

#include "../src/license_admin/gui/Page.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <chrono>
#include <deque>
#include <thread>
#include <memory>
#include <string>
#include <vector>

namespace admintest {

class TextRenderer final : public gfx::IRenderer {
public:
    struct Draw {
        gfx::Point  at;
        std::string text;
    };
    void beginFrame(gfx::Color) override { draws.clear(); }
    void endFrame() override {}
    void pushClip(const gfx::Rect& r) override { clips_.push_back(clips_.empty() ? r : clips_.back().intersect(r)); }
    void popClip() override {
        if (!clips_.empty()) clips_.pop_back();
    }
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void drawText(gfx::Point at, std::string_view s, gfx::FontId, gfx::Color) override {
        if (!clips_.empty() && clips_.back().empty()) return;
        draws.push_back({at, std::string(s)});
    }
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return {static_cast<float>(chars(s)) * h * 0.5f, h, h * 0.8f, h * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return f.v ? static_cast<float>(f.v) : 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        const std::size_t n = static_cast<std::size_t>(std::max(0.f, w / (h * 0.5f)));
        return std::min(n, s.size());
    }
    static std::size_t chars(std::string_view s) {
        std::size_t n = 0;
        for (char c : s)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
        return n;
    }
    // Tout le texte dessine, une ligne par appel.
    [[nodiscard]] std::string all() const {
        std::string t;
        for (const auto& d : draws) t += d.text + "\n";
        return t;
    }
    [[nodiscard]] bool shows(const std::string& part) const { return all().find(part) != std::string::npos; }

    std::vector<Draw> draws;
    gfx::Size         size{1280.f, 800.f};

private:
    std::vector<gfx::Rect> clips_;
};

// Ce que l'application a mis dans le presse-papiers (les essais le relisent).
inline std::string g_copied;

inline void installMetrics() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return static_cast<float>(TextRenderer::chars(t)) * h * 0.5f;
    };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.clipboardGet = [] { return g_copied; };
    s.clipboardSet = [](std::string_view t) { g_copied = std::string(t); };
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));
}

// Le ShellHost d'essai, et ce qu'il a vu.
struct HostRig {
    Fixture&                          fx;
    std::vector<gui::OpResult>        reports;
    std::vector<std::string>          flashes;
    std::vector<menu::MenuPtr>        dialogs;
    std::deque<std::string>           picks;        // les fichiers "choisis" dans l'ordre ("" : annule)
    std::vector<ui::FilePick>         asked;        // ce qui a ete demande
    std::vector<std::string>          navigations;
    std::vector<std::string>          openedFolders;
    bool                              slowOnThread{false};   // faux : le travail lent tourne sur place (essais simples)
    gui::Background                   bg;
    std::function<void(const gui::OpResult&)> pendingDone;
    std::vector<std::function<void()>>        refreshers;   // les pages a rafraichir apres un report ok

    explicit HostRig(Fixture& f) : fx(f) {}

    gui::ShellHost host() {
        gui::ShellHost h;
        h.workspace = [this]() -> gui::Workspace& { return *fx.ws; };
        h.report = [this](const gui::OpResult& r) {
            reports.push_back(r);
            if (r.ok)
                for (auto& f : refreshers) f();
        };
        h.flash = [this](const std::string& t, gui::Tone) { flashes.push_back(t); };
        h.runSlow = [this](const std::string& label, std::function<gui::OpResult()> work, std::function<void(const gui::OpResult&)> done) {
            if (!slowOnThread) {
                const gui::OpResult r = work();
                if (done) done(r);
                reports.push_back(r);
                if (r.ok)
                    for (auto& f : refreshers) f();
                return true;
            }
            if (!bg.start(label, std::move(work))) return false;
            pendingDone = std::move(done);
            return true;
        };
        h.busy = [this] { return bg.running(); };
        h.navigate = [this](const std::string& id) { navigations.push_back(id); };
        h.openDialog = [this](menu::MenuPtr m) {
            if (m) (void)m->Initialize();
            dialogs.push_back(std::move(m));
        };
        h.pickFile = [this](const ui::FilePick& p, std::function<void(std::string)> done) {
            asked.push_back(p);
            std::string answer;
            if (!picks.empty()) {
                answer = picks.front();
                picks.pop_front();
            }
            if (done) done(answer);
        };
        h.openFolder = [this](const std::string& f) { openedFolders.push_back(f); };
        return h;
    }

    // Le travail lent (slowOnThread) : attendre sa fin et rendre son resultat comme la coquille.
    bool finishSlow(int timeoutMs = 60000) {
        for (int waited = 0; waited < timeoutMs; waited += 20) {
            if (auto r = bg.take()) {
                if (pendingDone) pendingDone(*r);
                reports.push_back(*r);
                if (r->ok)
                    for (auto& f : refreshers) f();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }
    [[nodiscard]] const gui::OpResult* last() const { return reports.empty() ? nullptr : &reports.back(); }
};

// Poser un widget a une taille et le dessiner ; rend le texte dessine.
inline std::string paint(ui::Widget& w, TextRenderer& r, float width = 1100.f, float height = 760.f) {
    static const ui::Theme theme = ui::Theme::byName("Light");
    w.setBounds({0.f, 0.f, width, height});
    w.layout();
    r.beginFrame(theme.color.windowBg);
    ui::PaintContext ctx{r, theme, w.bounds(), 0.0, nullptr};
    w.render(ctx);
    r.endFrame();
    return r.all();
}

// Un dialogue (un WidgetMenu) : le meme, par son arbre.
inline std::string paintMenu(menu::IMenu& m, TextRenderer& r, float width = 1100.f, float height = 760.f) {
    static const ui::Theme theme = ui::Theme::byName("Light");
    r.size = {width, height};
    const menu::FrameContext fc{0.016, 0.0, &theme, r.surfaceSize(), 1.f};
    m.Update(fc);
    r.beginFrame(theme.color.windowBg);
    m.Render(r, fc);
    r.endFrame();
    return r.all();
}

template <class T>
T* find(ui::Widget& root, std::string_view id) {
    return dynamic_cast<T*>(root.findById(id));
}

inline bool click(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    if (!b || !b->enabled()) return false;
    b->clicked->emit();
    return true;
}

inline bool type(ui::Widget& root, std::string_view id, const std::string& text) {
    auto* t = find<ui::InputText>(root, id);
    if (!t) return false;
    t->setText(text);
    return true;
}

} // namespace admintest
