// =============================================================================
//  tests/zoom_test.cpp - 1.12.30 : le contrat du zoom (ui/Widget.hpp, ui/Zoom.hpp)
// -----------------------------------------------------------------------------
//  Sans fenetre : un renderer qui enregistre ce qu'on lui demande. On verifie
//  que le repere agrandi d'un widget Scaled tient debout - la mise en page, le
//  dessin, la souris, la souris tenue, la passe du dessus, les infobulles -, que
//  rien ne change a 100 %, les crans, Ctrl + molette, et le geste qui vise le bon
//  widget (le Native le plus profond, sinon le Scaled le plus haut).
// =============================================================================
#include "../src/ui/Widget.hpp"
#include "../src/ui/Zoom.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ui;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "ECHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

class Recorder final : public gfx::IRenderer {
public:
    struct Fill { gfx::Rect r; };
    struct Text { gfx::Point at; gfx::FontId font; std::string text; };
    std::vector<Fill> fills;
    std::vector<Text> texts;
    std::vector<gfx::Rect> clips;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect& r) override { clips.push_back(r); }
    void popClip() override {}
    void fillRect(const gfx::Rect& r, gfx::Color) override { fills.push_back({r}); }
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect& r, gfx::Color, float) override { fills.push_back({r}); }
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color) override { texts.push_back({at, f, std::string(s)}); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float px = f.v ? f.v : 16.f;
        return {static_cast<float>(s.size()) * px * 0.5f, px, px * 0.8f, px * 0.2f};
    }
    float lineHeight(gfx::FontId f) const override { return (f.v ? f.v : 16.f) * 1.25f; }
    gfx::Size surfaceSize() const override { return {1000.f, 800.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view s, gfx::FontId f, float maxW) const override {
        const float per = (f.v ? f.v : 16.f) * 0.5f;
        return std::min<std::size_t>(s.size(), static_cast<std::size_t>(maxW / per));
    }
};

// Un widget qui peint un carre a son coin bas-droit et retient ce qu'il recoit.
class Probe : public Widget {
public:
    explicit Probe(std::string id, ZoomSupport z = ZoomSupport::None) : Widget(std::move(id)) { setZoomSupport(z); }
    std::vector<gfx::Point> downs, moves;
    gfx::Rect painted{};
    bool overlay{false};
    std::string tip;
    [[nodiscard]] bool requestsOverlayPass() const override { return overlay; }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        char b[64];
        std::snprintf(b, sizeof b, "%.0f,%.0f", mouse.x, mouse.y);
        return b;
    }
    [[nodiscard]] bool hasTooltip() const override { return true; }
protected:
    void onPaint(const PaintContext& ctx) override {
        const auto b = bounds();
        painted = {b.right() - 10.f, b.bottom() - 10.f, 10.f, 10.f};
        ctx.r.fillRect(painted, {});
        ctx.r.drawText({b.x + 2.f, b.y + 2.f}, "ab", gfx::FontId{16}, {});
    }
    void onPaintOverlay(const PaintContext& ctx) override {
        if (overlay) ctx.r.fillRect({bounds().x, bounds().y, 4.f, 4.f}, {});
    }
    EventResult onEvent(const InputEvent& ev) override {
        if (const auto* d = std::get_if<MouseDown>(&ev); d && bounds().contains(d->pos)) {
            downs.push_back(d->pos);
            captureMouse();
            return EventResult::Consumed;
        }
        if (const auto* m = std::get_if<MouseMove>(&ev)) moves.push_back(m->pos);
        return EventResult::Ignored;
    }
};

// Un parent qui range un enfant dans son coin bas-droit (40 x 30), dans SON repere.
class Holder : public Probe {
public:
    using Probe::Probe;
    Probe* child{nullptr};
protected:
    void onLayout() override {
        const auto b = bounds();
        if (child) child->setBounds({b.right() - 40.f, b.bottom() - 30.f, 40.f, 30.f});
    }
};

void testSteps() {
    CHECK(near(zoomStep(1.f, 1, 0.5f, 2.f), 1.1f));
    CHECK(near(zoomStep(1.f, -1, 0.5f, 2.f), 0.9f));
    CHECK(near(zoomStep(2.f, 1, 0.5f, 2.f), 2.f));          // borne haute
    CHECK(near(zoomStep(0.5f, -1, 0.5f, 2.f), 0.5f));       // borne basse
    CHECK(near(zoomStep(1.3f, 1, 0.5f, 2.f), 1.5f));        // hors des crans : le suivant
    CHECK(near(zoomStep(1.3f, -1, 0.5f, 2.f), 1.25f));
    CHECK(near(zoomStep(3.f, 1, 0.05f, 16.f), 4.f));
    CHECK(zoomPercent(1.5f) == 150 && zoomPercent(0.67f) == 67);
}

void testHundredUntouched() {
    // A 100 %, rien ne change : le cadre est les bornes, les evenements passent tels quels.
    Holder root("root", ZoomSupport::Scaled);
    auto probe = std::make_unique<Probe>("p");
    root.child = static_cast<Probe*>(&root.addChild(std::move(probe)));
    root.setBounds({100.f, 50.f, 300.f, 200.f});
    root.layout();
    CHECK(near(root.bounds().w, 300.f) && near(root.zoomFrame().w, 300.f));
    Recorder r;
    Theme th = Theme::dark();
    root.render(PaintContext{r, th, {0.f, 0.f, 1000.f, 800.f}, 0.0, nullptr});
    CHECK(!r.fills.empty() && near(r.fills.front().r.x, 390.f) && near(r.fills.front().r.w, 10.f));
    (void)root.dispatch(MouseDown{{380.f, 230.f}, MouseButton::Left, 1, {}});
    CHECK(root.child->downs.size() == 1 && near(root.child->downs.front().x, 380.f));
    root.child->releaseMouse();
}

void testScaledFrame() {
    Holder root("root", ZoomSupport::Scaled);
    auto probe = std::make_unique<Probe>("p");
    root.child = static_cast<Probe*>(&root.addChild(std::move(probe)));
    root.setBounds({100.f, 50.f, 300.f, 200.f});
    root.setZoom(1.5f);
    root.layout();
    // Le repere agrandi : le coin ne bouge pas, la taille est divisee par le zoom.
    CHECK(near(root.zoom(), 1.5f));
    CHECK(near(root.bounds().x, 100.f) && near(root.bounds().y, 50.f));
    CHECK(near(root.bounds().w, 200.f) && near(root.bounds().h, 133.333f));
    CHECK(near(root.zoomFrame().w, 300.f) && near(root.zoomFrame().h, 200.f));
    // L'enfant est range dans ce repere (son coin bas-droit).
    const auto c = root.child->bounds();
    CHECK(near(c.x, 260.f) && near(c.y, 153.333f) && near(c.w, 40.f));
    // Ses points vers la surface, et retour.
    const auto s = root.child->toSurface(gfx::Point{c.x, c.y});
    CHECK(near(s.x, 100.f + 160.f * 1.5f) && near(s.y, 50.f + 103.333f * 1.5f));
    const auto back = root.child->fromSurface(s);
    CHECK(near(back.x, c.x) && near(back.y, c.y));
    CHECK(near(root.child->surfaceScale(), 1.5f));

    // Le dessin : a l'echelle, a sa place dans la surface ; le texte a la taille dessinee.
    Recorder r;
    Theme th = Theme::dark();
    root.render(PaintContext{r, th, {0.f, 0.f, 1000.f, 800.f}, 0.0, nullptr});
    // Le carre de l'enfant : son coin bas-droit tombe sur celui du cadre de la racine.
    bool found = false;
    for (const auto& f : r.fills)
        if (near(f.r.x + f.r.w, 400.f, 1.f) && near(f.r.y + f.r.h, 250.f, 1.f) && near(f.r.w, 15.f, 1.f)) found = true;
    CHECK(found);
    bool bigText = false;
    for (const auto& t : r.texts) if (t.font.v == 24) bigText = true;
    CHECK(bigText);

    // Un clic dans la surface, sur le carre de l'enfant : il le recoit dans son repere.
    (void)root.dispatch(MouseDown{{395.f, 245.f}, MouseButton::Left, 1, {}});
    CHECK(root.child->downs.size() == 1);
    if (!root.child->downs.empty()) {
        const auto d = root.child->downs.front();
        CHECK(near(d.x, 100.f + 295.f / 1.5f) && near(d.y, 50.f + 195.f / 1.5f));
        CHECK(root.child->bounds().contains(d));
    }
    // La souris tenue : un deplacement hors de tout arrive dans son repere.
    CHECK(Widget::routeCapturedMouse(root, MouseMove{{700.f, 50.f}, {30.f, 0.f}, {}}));
    CHECK(!root.child->moves.empty());
    if (!root.child->moves.empty()) CHECK(near(root.child->moves.back().x, 100.f + 600.f / 1.5f));
    root.child->releaseMouse();

    // L'infobulle : le widget sous la souris, et sa souris dans son repere.
    const Widget* t = root.tooltipAt({395.f, 245.f});
    CHECK(t == root.child);
    if (t) CHECK(t->liveTooltipAt({395.f, 245.f}) == "297,180");

    // La passe du dessus : a la meme echelle que le widget zoome.
    root.child->overlay = true;
    std::vector<Widget*> overlays;
    Recorder r2;
    root.render(PaintContext{r2, th, {0.f, 0.f, 1000.f, 800.f}, 0.0, &overlays});
    CHECK(overlays.size() == 1 && overlays.front() == root.child);
    Recorder r3;
    if (!overlays.empty()) overlays.front()->paintTopMost(PaintContext{r3, th, {0.f, 0.f, 1000.f, 800.f}, 0.0, nullptr});
    CHECK(r3.fills.size() == 1);
    if (!r3.fills.empty()) {
        const auto top = root.child->toSurface(gfx::Point{root.child->bounds().x, root.child->bounds().y});
        CHECK(near(r3.fills.front().r.x, top.x, 1.f) && near(r3.fills.front().r.w, 6.f, 1.f));
    }
    // Un evenement de l'hote pour cette passe : ramene au repere de son parent.
    root.child->downs.clear();
    (void)root.child->dispatchFromSurface(MouseDown{{395.f, 245.f}, MouseButton::Left, 1, {}});
    CHECK(root.child->downs.size() == 1);
    root.child->releaseMouse();
}

void testMeasureAndRenderer() {
    Recorder r;
    ZoomRenderer z(r, ZoomMap::about({0.f, 0.f}, 2.f));
    // La mesure a la taille dessinee, ramenee au repere : 16 px a 200 % se mesure comme 32 px / 2.
    CHECK(near(z.measure("abcd", gfx::FontId{16}).width, 4.f * 32.f * 0.5f / 2.f));
    CHECK(near(z.lineHeight(gfx::FontId{16}), 32.f * 1.25f / 2.f));
    CHECK(z.fitCharacters("abcdef", gfx::FontId{16}, 24.f) == 3);
    // Les bords sur des pixels entiers ; un trait fin garde sa largeur.
    z.fillRect({10.3f, 10.f, 20.f, 0.6f}, {});
    CHECK(!r.fills.empty());
    if (!r.fills.empty()) { CHECK(near(r.fills.back().r.h, 1.f)); CHECK(near(r.fills.back().r.x, 21.f)); }
    // La mesure de mise en page suit la portee de zoom.
    const float plain = measureWidth("abcd", gfx::FontId{16});
    {
        const MeasureZoomScope scope(1.5f);
        CHECK(near(measureZoom(), 1.5f));
        CHECK(zoomedFont(gfx::FontId{16}, 1.5f).v == 24);
        (void)measureWidth("abcd", gfx::FontId{16});
    }
    CHECK(near(measureZoom(), 1.f));
    CHECK(near(measureWidth("abcd", gfx::FontId{16}), plain));
}

void testCtrlWheel() {
    Holder root("root", ZoomSupport::Scaled);
    auto native = std::make_unique<Probe>("code", ZoomSupport::Native);
    root.child = static_cast<Probe*>(&root.addChild(std::move(native)));
    root.setBounds({0.f, 0.f, 400.f, 300.f});
    root.layout();
    // Sur le fond : un cran (sans hote, le widget zoome seul).
    (void)root.dispatch(MouseWheel{{50.f, 50.f}, 0.f, 1.f, KeyMods{true, false, false, false}});
    CHECK(near(root.zoom(), 1.1f));
    (void)root.dispatch(MouseWheel{{50.f, 50.f}, 0.f, -1.f, KeyMods{true, false, false, false}});
    CHECK(near(root.zoom(), 1.f));
    // Sur un widget Native : c'est le sien, pas celui du parent.
    const auto c = root.child->zoomFrame();
    (void)root.dispatch(MouseWheel{{c.x + 5.f, c.y + 5.f}, 0.f, 1.f, KeyMods{true, false, false, false}});
    CHECK(near(root.zoom(), 1.f));
    // Sans Ctrl : rien.
    (void)root.dispatch(MouseWheel{{50.f, 50.f}, 0.f, 1.f, {}});
    CHECK(near(root.zoom(), 1.f));
    // L'hote prend la demande (la page entiere).
    int asked = 0;
    Widget::setZoomRequestHandler([&asked](Widget&, int step) { asked += step; });
    (void)root.dispatch(MouseWheel{{50.f, 50.f}, 0.f, 1.f, KeyMods{true, false, false, false}});
    CHECK(asked == 1 && near(root.zoom(), 1.f));
    Widget::setZoomRequestHandler({});
    // Les bornes.
    root.setZoom(9.f);
    CHECK(near(root.zoom(), 2.f));
    root.setZoom(1.f);
    CHECK(near(root.bounds().w, 400.f));
}

void testTargets() {
    // Une page : un panneau Scaled qui contient un tableau (couvert) et un code (Native).
    Holder page("page");
    auto panel = std::make_unique<Holder>("panel", ZoomSupport::Scaled);
    Holder* p = static_cast<Holder*>(&page.addChild(std::move(panel)));
    auto code = std::make_unique<Probe>("code", ZoomSupport::Native);
    p->child = static_cast<Probe*>(&p->addChild(std::move(code)));
    page.setBounds({0.f, 0.f, 500.f, 400.f});
    p->setBounds({0.f, 0.f, 300.f, 200.f});     // le code au coin bas-droit du panneau
    page.layout();
    const auto c = p->child->zoomFrame();
    CHECK(zoomableAt(page, {c.x + 3.f, c.y + 3.f}) == p->child);   // le Native le plus profond
    const auto f = p->zoomFrame();
    CHECK(zoomableAt(page, {f.x + 2.f, f.y + 2.f}) == p);           // sinon le Scaled le plus haut
    const auto scaled = scaledUnder(page);
    CHECK(scaled.size() == 1 && scaled.front() == p);
    auto table = std::make_unique<TableView>("t");
    CHECK(table->zoomSupport() == ZoomSupport::Scaled);
    TreeView tree("tr");
    CHECK(tree.zoomSupport() == ZoomSupport::Scaled);
}

} // namespace

int main() {
    testSteps();
    testHundredUntouched();
    testScaledFrame();
    testMeasureAndRenderer();
    testCtrlWheel();
    testTargets();
    if (g_failures) {
        std::fprintf(stderr, "zoom_test : %d echec(s)\n", g_failures);
        return 1;
    }
    std::printf("zoom_test : tout est bon\n");
    return 0;
}
