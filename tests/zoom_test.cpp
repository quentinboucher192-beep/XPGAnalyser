// =============================================================================
//  tests/zoom_test.cpp - 1.12.30 : le contrat du zoom (ui/Widget.hpp, ui/Zoom.hpp)
// -----------------------------------------------------------------------------
//  Sans fenetre : un renderer qui enregistre ce qu'on lui demande. On verifie
//  que le repere agrandi d'un widget Scaled tient debout - la mise en page, le
//  dessin, la souris, la souris tenue, la passe du dessus, les infobulles -, que
//  rien ne change a 100 %, les crans, Ctrl + molette, et le geste qui vise le bon
//  widget (le Native le plus profond, sinon le Scaled le plus haut).
//  1.12.31 : la page zoomee entiere et ses ILOTS (un document y garde son zoom,
//  se dessine et recoit la souris a l'echelle de la surface), un menu ouvert a un
//  point de la surface, les touches du zoom sur un AZERTY (FR, BE), un QWERTZ, un
//  QWERTY et le pave numerique, et le zoom garde dans les reglages (ecrire, relire).
// =============================================================================
#include "../src/ui/Widget.hpp"
#include "../src/ui/Zoom.hpp"
#include "../src/ui/KeyMap.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"
// 1.12.31 : les touches du zoom sur tous les claviers (les en-tetes de SDL3, sans le lier),
// et le zoom garde d'une seance a l'autre (les reglages de l'utilisateur).
#include "../src/platform/KeyTranslate.hpp"
#include "../src/app/ZoomMemory.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
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

// ---- 1.12.31 : la page zoomee entiere, et ses ilots ----------------------------
// Une page : sa barre (un Probe), un document (Native) range dessous, dans SON repere.
class Page : public Probe {
public:
    explicit Page(std::string id) : Probe(std::move(id), ZoomSupport::Scaled) {}
    Probe* bar{nullptr};
    Probe* doc{nullptr};
protected:
    void onLayout() override {
        const auto b = bounds();
        if (bar) bar->setBounds({b.x, b.y, b.w, 30.f});
        if (doc) doc->setBounds({b.x + 10.f, b.y + 40.f, b.w - 20.f, b.h - 50.f});
    }
};

void testIslands() {
    Page page("page");
    page.bar = static_cast<Probe*>(&page.addChild(std::make_unique<Probe>("bar")));
    page.doc = static_cast<Probe*>(&page.addChild(std::make_unique<Probe>("doc", ZoomSupport::Native)));
    page.setBounds({100.f, 50.f, 600.f, 400.f});
    page.layout();
    // A 100 % : pas d'ilot, rien ne change.
    CHECK(!page.doc->zoomIsland());
    CHECK(near(page.doc->bounds().x, 110.f) && near(page.doc->bounds().w, 580.f));

    page.setZoom(2.f);
    page.layout();
    // La barre suit la page : 30 px de son repere, 60 a l'ecran.
    CHECK(near(page.bar->bounds().h, 30.f));
    const auto barOnScreen = page.bar->toSurface(page.bar->bounds());
    CHECK(near(barOnScreen.h, 60.f) && near(barOnScreen.w, 600.f));
    // Le document est un ilot : son repere est la surface (son cadre vu de la surface).
    CHECK(page.doc->zoomIsland());
    const auto f = page.doc->zoomFrame();          // dans le repere de la page : 10, 40 sous son coin
    CHECK(near(f.x, 110.f) && near(f.y, 90.f) && near(f.w, 280.f) && near(f.h, 150.f));
    const auto d = page.doc->bounds();
    CHECK(near(d.x, 120.f) && near(d.y, 130.f) && near(d.w, 560.f) && near(d.h, 300.f));
    CHECK(near(page.doc->surfaceScale(), 1.f));
    const auto sp = page.doc->toSurface(gfx::Point{d.x + 7.f, d.y + 9.f});
    CHECK(near(sp.x, d.x + 7.f) && near(sp.y, d.y + 9.f));
    const auto back = page.doc->fromSurface(gfx::Point{300.f, 200.f});
    CHECK(near(back.x, 300.f) && near(back.y, 200.f));
    CHECK(page.doc->mapToSurface().identity());

    // Le dessin : la barre a l'echelle (le texte a 32 px), le document a la sienne (16 px), a sa place.
    Recorder r;
    Theme th = Theme::dark();
    page.render(PaintContext{r, th, {0.f, 0.f, 1000.f, 800.f}, 0.0, nullptr});
    bool barText32 = false, docText16 = false, docSquare = false;
    for (const auto& t : r.texts) {
        if (t.text == "ab" && t.font.v == 32 && near(t.at.y, 50.f + 2.f * 2.f, 1.f)) barText32 = true;
        if (t.text == "ab" && t.font.v == 16 && near(t.at.x, d.x + 2.f, 0.6f) && near(t.at.y, d.y + 2.f, 0.6f)) docText16 = true;
    }
    for (const auto& fl : r.fills)
        if (near(fl.r.x, d.right() - 10.f, 0.6f) && near(fl.r.y, d.bottom() - 10.f, 0.6f) && near(fl.r.w, 10.f, 0.6f)) docSquare = true;
    CHECK(barText32);
    CHECK(docText16);
    CHECK(docSquare);

    // La souris : un clic dans le document lui arrive a l'endroit de la surface.
    (void)page.dispatch(MouseDown{{d.x + 50.f, d.y + 60.f}, MouseButton::Left, 1, {}});
    CHECK(page.doc->downs.size() == 1);
    if (!page.doc->downs.empty()) CHECK(near(page.doc->downs.front().x, d.x + 50.f) && near(page.doc->downs.front().y, d.y + 60.f));
    // La souris tenue : un deplacement hors de tout, dans la surface.
    CHECK(Widget::routeCapturedMouse(page, MouseMove{{900.f, 700.f}, {5.f, 0.f}, {}}));
    if (!page.doc->moves.empty()) CHECK(near(page.doc->moves.back().x, 900.f) && near(page.doc->moves.back().y, 700.f));
    page.doc->releaseMouse();
    // Un clic sur la barre : dans le repere de la page (agrandi).
    (void)page.dispatch(MouseDown{{120.f, 60.f}, MouseButton::Left, 1, {}});
    CHECK(page.bar->downs.size() == 1);
    if (!page.bar->downs.empty()) CHECK(near(page.bar->downs.front().x, 110.f) && near(page.bar->downs.front().y, 55.f));
    page.bar->releaseMouse();
    // L'infobulle du document : la souris de la surface.
    const Widget* tipped = page.tooltipAt({d.x + 3.f, d.y + 4.f});
    CHECK(tipped == page.doc);
    // Ctrl + molette sur le document : son zoom a lui (pas celui de la page) ; ailleurs, la page.
    int asked = 0;
    Widget::setZoomRequestHandler([&asked](Widget&, int step) { asked += step; });
    (void)page.dispatch(MouseWheel{{d.x + 20.f, d.y + 20.f}, 0.f, 1.f, KeyMods{true, false, false, false}});
    CHECK(asked == 0);
    (void)page.dispatch(MouseWheel{{120.f, 60.f}, 0.f, 1.f, KeyMods{true, false, false, false}});
    CHECK(asked == 1);
    Widget::setZoomRequestHandler({});
    // Le geste vise le document sous la souris, la page ailleurs ; le document est compte meme sous la page.
    CHECK(zoomableAt(page, {d.x + 5.f, d.y + 5.f}) == page.doc);
    CHECK(zoomableAt(page, {120.f, 60.f}) == &page);
    const auto natives = nativesUnder(page);
    CHECK(natives.size() == 1 && natives.front() == page.doc);

    // Retour a 100 % : plus d'ilot, le cadre est les bornes.
    page.setZoom(1.f);
    page.layout();
    CHECK(!page.doc->zoomIsland());
    CHECK(near(page.doc->bounds().x, 110.f) && near(page.doc->bounds().w, 580.f));
}

void testMenuAtSurface() {
    // Un menu d'une page zoomee s'ouvre la ou dit le signal (un point de la surface).
    Page page("page");
    auto* menu = static_cast<PopupMenu*>(&page.addChild(std::make_unique<PopupMenu>("menu")));
    menu->setItems({{"Un", {}, {}, Icon::None, true, false, 1}, {"Deux", {}, {}, Icon::None, true, false, 2}});
    page.setBounds({0.f, 0.f, 800.f, 600.f});
    page.setZoom(1.5f);
    page.layout();
    menu->setBounds(page.bounds());
    {
        const SurfaceScope window(gfx::Size{1000.f, 800.f});
        CHECK(near(windowSurfaceSize().w, 1000.f));
        {
            const SurfaceScope zoomed(gfx::Size{500.f, 400.f}, true);   // la portee d'un zoom : la fenetre ne change pas
            CHECK(near(surfaceSize().w, 500.f) && near(windowSurfaceSize().w, 1000.f));
        }
        menu->openAtSurface({300.f, 150.f});
    }
    CHECK(menu->isOpen());
    const auto pr = menu->popupRect();                      // dans le repere de la page
    const auto onScreen = menu->toSurface(gfx::Point{pr.x, pr.y});
    CHECK(near(onScreen.x, 300.f, 2.f) && near(onScreen.y, 150.f, 2.f));
    menu->close();
}

// ---- 1.12.31 : les touches du zoom, sur tous les claviers ---------------------------
//  Un clavier simule : la touche de chaque emplacement (scancode) sans Maj, et ce que
//  SDL met dans l'evenement avec son option par defaut (french_numbers : la rangee des
//  chiffres d'un AZERTY en chiffres).
struct Layout {
    std::map<SDL_Scancode, SDL_Keycode> plain;    // SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false)
    bool frenchNumbers{false};
    [[nodiscard]] SDL_Keycode eventKey(SDL_Scancode sc) const {
        if (frenchNumbers && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
            return sc == SDL_SCANCODE_0 ? SDLK_0 : static_cast<SDL_Keycode>(SDLK_1 + (sc - SDL_SCANCODE_1));
        const auto it = plain.find(sc);
        return it == plain.end() ? SDLK_UNKNOWN : it->second;
    }
    [[nodiscard]] KeyDown press(SDL_Scancode sc, SDL_Keymod mod, bool repeat = false) const {
        return app::sdlkeys::keyDownFrom(sc, eventKey(sc), mod, repeat, [this](SDL_Scancode s) {
            const auto it = plain.find(s);
            return it == plain.end() ? SDLK_UNKNOWN : it->second;
        });
    }
};

Layout common() {
    Layout l;
    l.plain[SDL_SCANCODE_KP_MINUS] = SDLK_KP_MINUS;
    l.plain[SDL_SCANCODE_KP_PLUS] = SDLK_KP_PLUS;
    l.plain[SDL_SCANCODE_KP_0] = SDLK_KP_0;
    l.plain[SDL_SCANCODE_A] = SDLK_A;
    return l;
}

void testZoomKeys() {
    const SDL_Keymod ctrl = SDL_KMOD_LCTRL, ctrlShift = static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT);
    // AZERTY francais : (le carre) & e " ' ( - e _ c a ) = ; le - sur la touche 6, le = a cote de la ).
    Layout fr = common();
    fr.frenchNumbers = true;
    const SDL_Keycode frRow[10] = {'&', 0xE9, '"', '\'', '(', '-', 0xE8, '_', 0xE7, 0xE0};
    for (int i = 0; i < 10; ++i) fr.plain[static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)] = frRow[i];
    fr.plain[SDL_SCANCODE_MINUS] = ')';
    fr.plain[SDL_SCANCODE_EQUALS] = SDLK_EQUALS;
    const auto minus = fr.press(SDL_SCANCODE_6, ctrl);
    CHECK(minus.key == Key::Num6 && minus.layout == Key::Minus);       // SDL dit 6, la disposition dit -
    CHECK(zoomKeyStep(minus) == -1);                                    // Ctrl + - : un cran de moins
    CHECK(commandKey(minus) == Key::Minus);                             // et Revenir dans un code (Ctrl+-)
    CHECK(keymap::strokeOf(minus).key == Key::Minus);
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_6, ctrlShift)) == kNotZoomKey);   // Ctrl+Maj : pas le zoom
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_EQUALS, ctrl)) == 1);       // Ctrl + = : un cran de plus
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_EQUALS, ctrlShift)) == 1);  // Ctrl + + (Maj + =) aussi
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_0, ctrl)) == 0);            // Ctrl + 0 : 100 %
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_0, ctrl, true)) == kNotZoomKey);   // tenu, il ne repete pas
    // Les chiffres des autres raccourcis restent des chiffres : Ctrl+1, Ctrl+5, la touche 6 sans Ctrl.
    CHECK(fr.press(SDL_SCANCODE_1, ctrl).key == Key::Num1 && zoomKeyStep(fr.press(SDL_SCANCODE_1, ctrl)) == kNotZoomKey);
    CHECK(fr.press(SDL_SCANCODE_5, ctrl).key == Key::Num5);
    const auto six = fr.press(SDL_SCANCODE_6, SDL_KMOD_NONE);
    CHECK(six.key == Key::Num6 && commandKey(six) == Key::Num6 && zoomKeyStep(six) == kNotZoomKey);
    // AltGr (Ctrl+Alt) n'est pas Ctrl : pas le zoom.
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_6, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_RALT))) == kNotZoomKey);
    // Le pave numerique.
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_KP_MINUS, ctrl)) == -1);
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_KP_PLUS, ctrl)) == 1);
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_KP_0, ctrl)) == 0);
    // Sans Ctrl : rien (le - et le + d'un champ s'ecrivent).
    CHECK(zoomKeyStep(fr.press(SDL_SCANCODE_KP_MINUS, SDL_KMOD_NONE)) == kNotZoomKey);

    // AZERTY belge : & e " ' ( (le paragraphe) e ! c a ) - ; le - a la place du = d'un QWERTY, le = sous le /.
    Layout be = common();
    be.frenchNumbers = true;
    const SDL_Keycode beRow[10] = {'&', 0xE9, '"', '\'', '(', 0xA7, 0xE8, '!', 0xE7, 0xE0};
    for (int i = 0; i < 10; ++i) be.plain[static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)] = beRow[i];
    be.plain[SDL_SCANCODE_MINUS] = ')';
    be.plain[SDL_SCANCODE_EQUALS] = SDLK_MINUS;
    be.plain[SDL_SCANCODE_SLASH] = SDLK_EQUALS;
    CHECK(zoomKeyStep(be.press(SDL_SCANCODE_EQUALS, ctrl)) == -1);
    CHECK(zoomKeyStep(be.press(SDL_SCANCODE_SLASH, ctrlShift)) == 1);
    CHECK(zoomKeyStep(be.press(SDL_SCANCODE_6, ctrl)) == kNotZoomKey);   // le paragraphe : rien

    // QWERTZ allemand : le eszett et l'accent aigu a droite du 0, le + a cote du U (sa touche a lui), le - pres du point.
    Layout de = common();
    for (int i = 0; i < 9; ++i) de.plain[static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)] = static_cast<SDL_Keycode>('1' + i);
    de.plain[SDL_SCANCODE_0] = SDLK_0;
    de.plain[SDL_SCANCODE_MINUS] = 0xDF;
    de.plain[SDL_SCANCODE_RIGHTBRACKET] = SDLK_PLUS;
    de.plain[SDL_SCANCODE_SLASH] = SDLK_MINUS;
    CHECK(zoomKeyStep(de.press(SDL_SCANCODE_RIGHTBRACKET, ctrl)) == 1);
    CHECK(zoomKeyStep(de.press(SDL_SCANCODE_SLASH, ctrl)) == -1);
    CHECK(zoomKeyStep(de.press(SDL_SCANCODE_0, ctrl)) == 0);
    CHECK(zoomKeyStep(de.press(SDL_SCANCODE_6, ctrl)) == kNotZoomKey);

    // QWERTY : Ctrl + - , Ctrl + = , Ctrl + + (Maj + =) ; Ctrl+Maj+- (Ctrl+_) n'est pas le zoom.
    Layout us = common();
    for (int i = 0; i < 9; ++i) us.plain[static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)] = static_cast<SDL_Keycode>('1' + i);
    us.plain[SDL_SCANCODE_0] = SDLK_0;
    us.plain[SDL_SCANCODE_MINUS] = SDLK_MINUS;
    us.plain[SDL_SCANCODE_EQUALS] = SDLK_EQUALS;
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_MINUS, ctrl)) == -1);
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_MINUS, ctrlShift)) == kNotZoomKey);
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_EQUALS, ctrl)) == 1);
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_EQUALS, ctrlShift)) == 1);
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_EQUALS, ctrl, true)) == 1);   // tenu, il repete
    CHECK(zoomKeyStep(us.press(SDL_SCANCODE_6, ctrl)) == kNotZoomKey);
    CHECK(us.press(SDL_SCANCODE_A, ctrl).key == Key::A);
}

// ---- 1.12.31 : le zoom garde d'une seance a l'autre ---------------------------------
void testMemory() {
    namespace fs = std::filesystem;
    const auto file = fs::temp_directory_path() / "xpg_zoom_test_reglages.txt";
    std::error_code ec;
    fs::remove(file, ec);
    {
        app::Settings s;
        (void)s.load(file.string());                       // absent : vide
        CHECK(near(app::zoommemory::read(s, "analysis.api.variables"), 1.f));
        CHECK(app::zoommemory::write(s, "analysis.api.variables", 1.5f));
        CHECK(app::zoommemory::write(s, "hmi.view.176", 1.25f));
        CHECK(app::zoommemory::write(s, "analysis.explorerColumn", 0.75f));
        CHECK(!app::zoommemory::write(s, "analysis.api.variables", 1.5f));   // rien ne change : rien a ecrire
        CHECK(!app::zoommemory::write(s, "", 1.5f));                         // sans identifiant : pas de cle
        CHECK(app::zoommemory::write(s, "hmi.alarmes", 9.f));               // hors bornes : 200 %
        CHECK(s.save());
    }
    {
        app::Settings s;                                   // l'appli rouverte
        CHECK(s.load(file.string()));
        CHECK(near(app::zoommemory::read(s, "analysis.api.variables"), 1.5f));
        CHECK(near(app::zoommemory::read(s, "hmi.view.176"), 1.25f));
        CHECK(near(app::zoommemory::read(s, "analysis.explorerColumn"), 0.75f));
        CHECK(near(app::zoommemory::read(s, "hmi.alarmes"), 2.f));
        CHECK(near(app::zoommemory::read(s, "hmi.recettes"), 1.f));
        CHECK(s.getString("zoom.hmi.view.176") == "1.2500");
        // Revenir a 100 % l'oublie : la cle s'en va.
        CHECK(app::zoommemory::write(s, "hmi.view.176", 1.f));
        CHECK(s.getString("zoom.hmi.view.176").empty());
        CHECK(!app::zoommemory::write(s, "hmi.view.176", 1.f));
        s.set("zoom.abime", std::string("n'importe quoi"));
        CHECK(near(app::zoommemory::read(s, "abime"), 1.f));
        CHECK(s.save());
    }
    {
        app::Settings s;
        CHECK(s.load(file.string()));
        CHECK(near(app::zoommemory::read(s, "hmi.view.176"), 1.f));
        CHECK(near(app::zoommemory::read(s, "analysis.api.variables"), 1.5f));
        CHECK(s.keysWithPrefix("zoom.hmi.view").empty());
    }
    fs::remove(file, ec);
}

} // namespace

int main() {
    testSteps();
    testHundredUntouched();
    testScaledFrame();
    testMeasureAndRenderer();
    testCtrlWheel();
    testTargets();
    testIslands();
    testMenuAtSurface();
    testZoomKeys();
    testMemory();
    if (g_failures) {
        std::fprintf(stderr, "zoom_test : %d echec(s)\n", g_failures);
        return 1;
    }
    std::printf("zoom_test : tout est bon\n");
    return 0;
}
