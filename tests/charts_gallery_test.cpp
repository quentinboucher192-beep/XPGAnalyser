// =============================================================================
//  tests/charts_gallery_test.cpp - 1.47.0 : LA GALERIE DES GRAPHIQUES DE L'ADMIN
//  (la page cachee << graphes-essai >>, PageChartsGallery.cpp)
// -----------------------------------------------------------------------------
//  - la page n'est pas dans le menu, mais on y va (--page graphes-essai) ;
//  - ses 8 planches, dessinees a blanc a la taille de la fenetre des captures
//    (1373 x 917) et a la plus petite (1100 x 700) : les cartes ne se chevauchent
//    pas, chacune garde son texte dans son cadre, aucun texte sur un autre (les
//    libelles des axes, les reperes, les legendes), rien de NaN ;
//  - un clic, puis Entree au clavier, sur un graphique : la page le dit (l'avis
//    de ce qu'elle filtrerait) ;
//  - la bulle d'exemple (planche 08) : passee au-dessus de tout, dans la fenetre.
// =============================================================================
#include "../src/license_admin/gui/Page.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/ui/widgets/Charts.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace gui = license_admin::gui;
namespace ch = ui::charts;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s %s\n", ok ? "  ok  " : "ECHEC ", what.c_str());
}

std::size_t glyphs(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}
float advance(gfx::FontId f) { return static_cast<float>(f.v ? f.v : 16) * 0.55f; }

class Recorder final : public gfx::IRenderer {
public:
    struct Text {
        gfx::Rect   box;
        std::string s;
    };
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect& r, gfx::Color) override { finite = finite && std::isfinite(r.x + r.y + r.w + r.h); }
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect& r, gfx::Color, float) override { finite = finite && std::isfinite(r.x + r.y + r.w + r.h); }
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color) override {
        finite = finite && std::isfinite(at.x + at.y);
        texts.push_back({{at.x, at.y, measure(s, f).width, lineHeight(f)}, std::string(s)});
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex* v, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i) finite = finite && std::isfinite(v[i].pos.x + v[i].pos.y);
    }
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = static_cast<float>(f.v ? f.v : 16);
        return {static_cast<float>(glyphs(s)) * advance(f), h * 1.25f, h, h * 0.25f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v ? f.v : 16) * 1.25f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, w / advance(f))));
    }
    [[nodiscard]] bool shows(const std::string& part) const {
        for (const auto& t : texts)
            if (t.s.find(part) != std::string::npos) return true;
        return false;
    }
    std::vector<Text> texts;
    bool              finite{true};
    gfx::Size         size{1373.f, 917.f};
};

gfx::Size g_surface{1373.f, 917.f};

void installMetrics() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(glyphs(t)) * advance(f); };
    s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v ? f.v : 16) * 1.25f; };
    s.surfaceSize = [] { return g_surface; };
    ui::installPlatformServices(std::move(s));
}

const ui::Theme& theme() {
    static const ui::Theme t = [] {
        ui::Theme::setProductAccent(ui::Theme::ProductAccent::Violet);
        return ui::Theme::byName("XPG");
    }();
    return t;
}

bool overlapping(const std::vector<Recorder::Text>& texts, std::string* bad) {
    for (std::size_t i = 0; i < texts.size(); ++i)
        for (std::size_t j = i + 1; j < texts.size(); ++j)
            if (!texts[i].box.inset(1.f, 2.f).intersect(texts[j].box.inset(1.f, 2.f)).empty()) {
                *bad = "\xC2\xAB " + texts[i].s + " \xC2\xBB / \xC2\xAB " + texts[j].s + " \xC2\xBB";
                return true;
            }
    return false;
}

} // namespace

int main() {
    installMetrics();
    std::printf("-- la page cachee\n");
    bool inMenu = false;
    for (const auto& e : gui::Shell::entries()) inMenu = inMenu || std::string(e.id) == "graphes-essai";
    check(!inMenu, "\xC2\xAB graphes-essai \xC2\xBB n'est pas dans le menu");
    check(gui::Shell::entry("graphes-essai") != nullptr && gui::Shell::parentOf("graphes-essai").empty(), "mais la page existe (--page graphes-essai)");

    std::vector<std::string> flashes;
    gui::ShellHost host;
    host.flash = [&](const std::string& t, gui::Tone) { flashes.push_back(t); };
    gui::PagePtr page = gui::makeChartsGalleryPage(host);
    check(page && page->title() == "Graphiques (essai)", "la galerie se construit sans Workspace");

    struct Size {
        const char* name;
        gfx::Rect   area;
        gfx::Size   window;
    };
    // La zone d'une page dans la coquille : le menu de 232 px a gauche, la barre de 64 px en haut, le bandeau de 40 px.
    const Size sizes[] = {{"1373 x 917", {232.f, 64.f, 1141.f, 813.f}, {1373.f, 917.f}}, {"1100 x 700", {232.f, 64.f, 868.f, 596.f}, {1100.f, 700.f}}};
    for (const Size& sz : sizes) {
        std::printf("-- les planches a %s\n", sz.name);
        g_surface = sz.window;
        for (int p = 1; p <= 8; ++p) {
            auto* tab = dynamic_cast<ui::Button*>(page->findById("graphes.p" + std::to_string(p)));
            if (!tab) {
                check(false, "le bouton de la planche " + std::to_string(p));
                continue;
            }
            tab->clicked->emit();
            page->setBounds(sz.area);
            page->layout();
            // La planche montree et ses cartes.
            ui::Widget* board = nullptr;
            for (const auto& c : page->children())
                if (c->visible() && !dynamic_cast<ui::Button*>(c.get())) board = c.get();
            if (!board) {
                check(false, "planche " + std::to_string(p) + " : montree");
                continue;
            }
            const std::string tag = "planche " + std::to_string(p) + " (" + sz.name + ")";
            bool inside = true, apart = true, cardsOk = true, noNan = true;
            std::string why;
            const auto& cards = board->children();
            for (std::size_t i = 0; i < cards.size(); ++i) {
                const gfx::Rect a = cards[i]->bounds();
                if (a.x < sz.area.x || a.right() > sz.area.right() + 0.5f || a.y < sz.area.y || a.bottom() > sz.area.bottom() + 0.5f || a.w < 120.f || a.h < 80.f) {
                    inside = false;
                    why = cards[i]->id();
                }
                for (std::size_t j = i + 1; j < cards.size(); ++j)
                    if (!a.inset(1.f, 1.f).intersect(cards[j]->bounds().inset(1.f, 1.f)).empty()) apart = false;
                // Chaque carte a part : son texte dans son cadre, aucun texte sur un autre.
                Recorder r;
                r.size = sz.window;
                const ui::PaintContext ctx{r, theme(), {0.f, 0.f, sz.window.w, sz.window.h}, 0.0, nullptr};
                cards[i]->render(ctx);
                noNan = noNan && r.finite;
                for (const auto& t : r.texts)
                    if (t.box.x < a.x - 1.f || t.box.right() > a.right() + 1.f || t.box.y < a.y - 1.f || t.box.bottom() > a.bottom() + 2.f) {
                        cardsOk = false;
                        why = cards[i]->id() + " : \xC2\xAB " + t.s + " \xC2\xBB sort du cadre";
                    }
                std::string bad;
                if (overlapping(r.texts, &bad)) {
                    cardsOk = false;
                    why = cards[i]->id() + " : " + bad;
                }
            }
            check(!cards.empty() && inside && apart, tag + " : " + std::to_string(cards.size()) + " cartes dans la page, sans se chevaucher" + (inside ? "" : " - " + why));
            check(cardsOk, tag + " : chaque carte garde son texte, sans chevauchement" + (cardsOk ? "" : " - " + why));
            check(noNan, tag + " : rien de NaN");
        }
    }

    std::printf("-- le clic, le clavier, la bulle d'exemple\n");
    g_surface = {1373.f, 917.f};
    static_cast<ui::Button*>(page->findById("graphes.p1"))->clicked->emit();
    page->setBounds(sizes[0].area);
    page->layout();
    auto* montant = dynamic_cast<ch::TimeChart*>(page->findById("graphes.montant"));
    check(montant != nullptr, "le graphique \xC2\xAB Montant encaiss\xC3\xA9 \xC2\xBB");
    if (montant) {
        const auto f = montant->frame();
        const gfx::Point at{f.xAt(3), f.plot.bottom() - 3.f};
        (void)page->dispatch(ui::MouseMove{at, {0.f, 0.f}, {}});
        (void)page->dispatch(ui::MouseDown{at, ui::MouseButton::Left, 1, {}});
        check(!flashes.empty() && flashes.back().find("Montant encaiss\xC3\xA9") != std::string::npos && flashes.back().find("Semaine du 07/09 au 13/09") != std::string::npos,
              "un clic sur une barre : la page dit ce qu'elle filtrerait (la semaine du 07/09)");
        (void)page->dispatch(ui::KeyDown{ui::Key::Right, {}, false, ui::Key::Unknown});
        (void)page->dispatch(ui::KeyDown{ui::Key::Return, {}, false, ui::Key::Unknown});
        check(flashes.back().find("Semaine du 14/09 au 20/09") != std::string::npos, "au clavier : droite puis Entree, la semaine suivante");
        (void)page->dispatch(ui::KeyDown{ui::Key::Escape, {}, false, ui::Key::Unknown});
        (void)page->dispatch(ui::MouseMove{{5.f, 5.f}, {0.f, 0.f}, {}});
        check(montant->requestsOverlayPass(), "la bulle d'exemple (la semaine du 26/10) passe au-dessus de tout");
        Recorder r;
        std::vector<ui::Widget*> overlays;
        const ui::PaintContext ctx{r, theme(), {0.f, 0.f, 1373.f, 917.f}, 0.0, &overlays};
        page->render(ctx);
        r.texts.clear();
        for (ui::Widget* w : overlays) w->paintTopMost(ctx);
        bool inWindow = !r.texts.empty();
        for (const auto& t : r.texts) inWindow = inWindow && t.box.x >= 0.f && t.box.right() <= 1373.f && t.box.y >= 0.f && t.box.bottom() <= 917.f;
        check(r.shows("Semaine du 26/10 au 01/11") && r.shows("Clic : ces commandes") && inWindow, "la bulle dessinee dans la fenetre : la semaine, \xC2\xAB Clic : ces commandes \xC2\xBB");
    }
    std::printf("\n%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
