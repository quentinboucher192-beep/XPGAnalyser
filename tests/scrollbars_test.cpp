// =============================================================================
//  tests/scrollbars_test.cpp - 1.46.0 : << ces scrollbars ne bougent pas avec la
//  souris >> (la fenetre Licence, onglet Demander ma licence)
// -----------------------------------------------------------------------------
//  Chaque barre de defilement de la boite a outils, PAR LE VRAI CHEMIN d'une
//  fenetre (MenuManager::HandleEvent -> WidgetMenu::HandleEvent : la souris
//  tenue, Widget::routeCapturedMouse ; puis l'arbre) et sans fenetre :
//   - le pouce se prend, et le glisser le suit EN PROPORTION (20 px, puis 40 px :
//     le double ; la valeur exacte quand la geometrie est connue) ;
//   - la souris tenue : le glisser continue hors du widget et hors de la fenetre,
//     jusqu'au bout du contenu ; le relachement la rend ;
//   - un clic dans la gouttiere avance d'une page (90 % de ce qu'on voit) ;
//   - la molette sur le volet le fait defiler.
//  Les listes, les arbres, les tableaux, l'editeur de code, un panneau qui
//  defile, la liste des suggestions d'un champ (un clic sur sa barre ne choisit
//  plus la ligne d'a cote) et ui::ScrollArea - le volet qui dessine tout et
//  defile lui-meme (la partie gauche de Demander ma licence, les pages Boutique et
//  Commandes de l'Admin, la fenetre d'une mise a jour), dont la barre etait
//  seulement dessinee.
// =============================================================================
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"
#include "../src/ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s %s\n", ok ? "  ok  " : "ECHEC ", what.c_str());
}

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
    return b;
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Rien a l'ecran : des mesures simples (8 px par caractere a la taille 16).
class NullRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return {static_cast<float>(s.size()) * h * 0.5f, h, h * 0.8f, h * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return f.v ? static_cast<float>(f.v) : 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1280.f, 800.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return std::min(s.size(), static_cast<std::size_t>(std::max(0.f, w / (h * 0.5f))));
    }
};

// ---- les modeles ----
struct Lignes final : ui::IListModel {
    std::size_t rowCount() const override { return 200; }
    std::string text(ui::RowIndex r) const override { return "Ligne " + std::to_string(r); }
};
struct Arbre final : ui::ITreeModel {
    ui::NodeId root() const override { return 1; }
    std::size_t childCount(ui::NodeId n) const override { return n == 1 ? 150 : 0; }
    ui::NodeId childAt(ui::NodeId, std::size_t i) const override { return static_cast<ui::NodeId>(10 + i); }
    bool hasChildren(ui::NodeId n) const override { return n == 1; }
    std::string text(ui::NodeId n) const override { return "Noeud " + std::to_string(n); }
};
struct Table final : ui::ITableModel {
    std::size_t rowCount() const override { return 120; }
    std::size_t columnCount() const override { return 2; }
    std::string headerText(std::size_t c) const override { return c == 0 ? "Fonctionnalit\xC3\xA9" : "Logiciel"; }
    std::string cellText(ui::RowIndex r, std::size_t c) const override { return std::to_string(r) + "," + std::to_string(c); }
    bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
};
struct Grand final : ui::Widget {
    Grand() : ui::Widget("grand") {}
    ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {300.f, 2000.f};
        return h;
    }
};

// Le volet qui dessine tout et defile lui-meme (comme la partie gauche de << Demander ma licence >>) : 2000 px de contenu
// dans 300 ; sa barre ENTRE deux colonnes, sous un titre (pas au bord) ; la molette sur sa partie gauche seulement.
class Volet final : public ui::Widget {
public:
    Volet() : ui::Widget("volet") {}
    static constexpr float kContent = 2000.f, kTitle = 40.f, kLeftW = 240.f;
    float          scroll{0.f};
    int            layouts{0};
    ui::ScrollArea bar;
    [[nodiscard]] gfx::Rect left() const { return {bounds().x, bounds().y, kLeftW + 20.f, bounds().h}; }

protected:
    void onLayout() override {
        ++layouts;
        const auto b = bounds();
        bar.place(left(), kContent, b.h, {b.x + kLeftW + 5.f, b.y + kTitle, ui::kScrollBarThickness, b.h - kTitle - 4.f});
    }
    void onPaint(const ui::PaintContext& ctx) override { bar.paint(ctx, scroll); }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        bool moved = false;
        if (bar.handle(*this, ev, scroll, &moved)) {
            if (moved) invalidateLayout();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
};

// La racine du banc : chaque widget a sa place.
class Racine final : public ui::Widget {
public:
    Racine() : ui::Widget("banc") {}
    std::vector<std::pair<ui::Widget*, gfx::Rect>> places;

protected:
    void onLayout() override {
        for (auto& [w, r] : places) w->setBounds(r);
    }
};

class Banc final : public menu::WidgetMenu {
public:
    Banc() : menu::WidgetMenu("banc.barres") {}
    ui::ListView*        list{nullptr};
    ui::TreeView*        tree{nullptr};
    ui::TableView*       table{nullptr};
    ui::MultiLineText*   code{nullptr};
    ui::ScrollablePanel* panel{nullptr};
    Volet*               volet{nullptr};
    ui::InputText*       field{nullptr};

protected:
    core::Status buildUi() override {
        auto root = std::make_unique<Racine>();
        auto put = [&](std::unique_ptr<ui::Widget> w, gfx::Rect r) -> ui::Widget& {
            ui::Widget& ref = root->addChild(std::move(w));
            root->places.push_back({&ref, r});
            return ref;
        };
        list = &static_cast<ui::ListView&>(put(std::make_unique<ui::ListView>("liste"), {10, 10, 220, 240}));
        list->setModel(std::make_shared<Lignes>());
        tree = &static_cast<ui::TreeView&>(put(std::make_unique<ui::TreeView>("arbre"), {240, 10, 220, 240}));
        tree->setModel(std::make_shared<Arbre>());
        tree->expand(1);
        table = &static_cast<ui::TableView&>(put(std::make_unique<ui::TableView>("tableau"), {470, 10, 360, 240}));
        table->setColumns({{"Fonctionnalit\xC3\xA9", 230.f}, {"Logiciel", 110.f}});
        table->setModel(std::make_shared<Table>());
        code = &static_cast<ui::MultiLineText&>(put(std::make_unique<ui::MultiLineText>("code"), {840, 10, 400, 240}));
        std::string text;
        for (int i = 0; i < 300; ++i) text += "ligne " + std::to_string(i + 1) + "\n";
        code->setText(text);
        panel = &static_cast<ui::ScrollablePanel&>(put(std::make_unique<ui::ScrollablePanel>("panneau"), {10, 270, 320, 240}));
        panel->setContent(std::make_unique<Grand>());
        volet = &static_cast<Volet&>(put(std::make_unique<Volet>(), {340, 270, 500, 300}));
        field = &static_cast<ui::InputText&>(put(std::make_unique<ui::InputText>("champ"), {860, 270, 260, 28}));
        field->setAssist([](std::string_view, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
            from = 0;
            for (int i = 0; i < 40; ++i) {
                ui::InputText::Suggestion one;
                one.text = "choix " + std::to_string(i);
                one.detail = "essai";
                out.push_back(std::move(one));
            }
        });
        setRoot(std::move(root));
        return core::ok();
    }
};

struct Bench {
    menu::MenuManager  mm{menu::MenuFactory{}};
    NullRenderer       r;
    ui::Theme          theme = ui::Theme::light();
    menu::FrameContext fc{1.0 / 30.0, 1.0, &theme, {1280.f, 800.f}, 1.f};
    Banc*              banc{nullptr};

    void frame() {
        mm.applyPending();
        mm.Update(fc);
        mm.Render(r, fc);
    }
    void send(const ui::InputEvent& e) { (void)mm.HandleEvent(e); }
    void move(gfx::Point p) { send(ui::MouseMove{p, {}, {}}); }
    void down(gfx::Point p) { send(ui::MouseDown{p, ui::MouseButton::Left, 1, {}}); }
    void up(gfx::Point p) { send(ui::MouseUp{p, ui::MouseButton::Left, {}}); }
    void wheel(gfx::Point p, float dy) { send(ui::MouseWheel{p, 0.f, dy, {}}); }
    void click(gfx::Point p) {
        move(p);
        down(p);
        up(p);
        frame();
    }
    [[nodiscard]] bool captured() { return ui::Widget::routeCapturedMouse(*mm.top()->widgetRoot(), ui::MouseMove{{1.f, 1.f}, {}, {}}); }
};

// Une barre a l'essai : le widget, son decalage (en pixels), le haut du pouce quand le decalage est nul, le bas de la
// gouttiere, le bout du contenu (le decalage le plus grand), ce qu'on voit, et la tolerance (une ligne pour l'editeur).
struct Probe {
    std::string              name;
    ui::Widget*              w{nullptr};
    std::function<float()>   offset;
    std::function<void()>    toTop;
    gfx::Point               thumb{};      // un point du pouce, le decalage nul
    gfx::Point               trackLow{};   // un point de la gouttiere, sous le pouce
    gfx::Point               inside{};     // un point du volet (la molette)
    float                    max{0.f};
    float                    viewport{0.f};
    float                    tol{1.f};
    float                    exact20{-1.f};   // le decalage attendu apres 20 px (< 0 : non calcule)
};

void probe(Bench& b, const Probe& p) {
    std::printf("-- %s\n", p.name.c_str());
    p.toTop();
    b.frame();
    check(p.offset() < 0.5f, p.name + " : en haut (" + num(p.offset()) + ")");
    // Le pouce pris, tire de 20 px puis de 40 px : le decalage suit en proportion.
    b.move(p.thumb);
    b.down(p.thumb);
    check(p.w->holdsMouse(), p.name + " : le pouce se prend (la souris est tenue)");
    b.move({p.thumb.x, p.thumb.y + 20.f});
    b.frame();
    const float o1 = p.offset();
    b.move({p.thumb.x, p.thumb.y + 40.f});
    b.frame();
    const float o2 = p.offset();
    check(o1 > 1.f && near(o2, 2.f * o1, p.tol + 0.02f * o2), p.name + " : 20 px -> " + num(o1) + ", 40 px -> " + num(o2) + " (le double)");
    if (p.exact20 >= 0.f) check(near(o1, p.exact20, p.tol), p.name + " : 20 px -> " + num(o1) + " (attendu " + num(p.exact20) + ")");
    // Hors du widget, hors de la fenetre : le glisser continue, jusqu'au bout du contenu.
    b.move({p.thumb.x + 600.f, p.thumb.y + 3000.f});
    b.frame();
    check(near(p.offset(), p.max, p.tol), p.name + " : tire hors du widget, jusqu'au bout (" + num(p.offset()) + " / " + num(p.max) + ")");
    b.up({p.thumb.x + 600.f, p.thumb.y + 3000.f});
    b.frame();
    check(!p.w->holdsMouse() && !b.captured(), p.name + " : relache, la souris est rendue");
    const float after = p.offset();
    b.move({p.thumb.x, p.thumb.y - 500.f});
    b.frame();
    check(near(p.offset(), after, 0.01f), p.name + " : relache, la souris ne le tire plus");
    // Un clic dans la gouttiere, sous le pouce : une page.
    p.toTop();
    b.frame();
    b.click(p.trackLow);
    const float page = std::min(p.max, std::max(1.f, p.viewport * 0.9f));
    check(near(p.offset(), page, p.tol) && !p.w->holdsMouse(), p.name + " : un clic dans la gouttiere -> " + num(p.offset()) + " (une page : " + num(page) + ")");
    // La molette sur le volet.
    p.toTop();
    b.frame();
    b.move(p.inside);
    b.wheel(p.inside, -1.f);
    b.frame();
    check(p.offset() > 1.f, p.name + " : la molette -> " + num(p.offset()));
    p.toTop();
    b.frame();
}

// Le decalage apres `px` pixels tires : px / (gouttiere - pouce) x le bout (le pouce : au moins 24 px).
float expectedAfter(float px, float trackLen, float content, float viewport) {
    const float thumb = std::min(trackLen, std::max(24.f, trackLen * viewport / content));
    return px / (trackLen - thumb) * (content - viewport);
}

} // namespace

int main() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * (f.v ? static_cast<float>(f.v) : 16.f) * 0.5f; };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));

    // ---- ui::ScrollArea seul : sa geometrie ----
    std::printf("-- ui::ScrollArea : la geometrie\n");
    {
        ui::ScrollArea a;
        a.place({100.f, 50.f, 400.f, 300.f}, 1500.f);
        check(a.needed() && near(a.maxOffset(), 1200.f, 0.01f) && a.track().x == 500.f - ui::kScrollBarThickness && a.track().h == 300.f,
              "par defaut : la gouttiere au bord droit de la zone, toute sa hauteur ; le bout : 1200");
        a.place({100.f, 50.f, 400.f, 300.f}, 250.f);
        check(!a.needed() && a.maxOffset() == 0.f, "un contenu plus court que la zone : pas de barre");
        a.place({100.f, 50.f, 400.f, 300.f}, 1500.f, 0.f, {300.f, 90.f, 10.f, 200.f});
        check(a.track().x == 300.f && a.track().h == 200.f && near(a.thumb(0.f).h, 40.f, 0.01f) && near(a.thumb(1200.f).bottom(), 290.f, 0.01f),
              "une gouttiere choisie (sous un titre, entre deux colonnes) : le pouce y va du haut au bas");
        check(a.clamp(-5.f) == 0.f && a.clamp(5000.f) == 1200.f, "le decalage borne");
    }

    Bench b;
    b.mm.PushMenu(std::make_unique<Banc>());
    b.frame();
    b.banc = dynamic_cast<Banc*>(b.mm.top());
    check(b.banc != nullptr, "le banc d'essai : une fenetre (un WidgetMenu sur un MenuManager)");
    if (!b.banc) return 1;
    b.frame();
    Banc& k = *b.banc;

    {   // la liste
        const gfx::Rect cr = k.list->contentRect();
        Probe p;
        p.name = "la liste";
        p.w = k.list;
        p.offset = [&] { return k.list->scrollOffset(); };
        p.toTop = [&] {
            for (int i = 0; i < 80; ++i) b.wheel({cr.x + 20.f, cr.y + 20.f}, 5.f);
        };
        p.thumb = {cr.right() - ui::kScrollBarThickness * 0.5f, cr.y + 4.f};
        p.trackLow = {p.thumb.x, cr.bottom() - 4.f};
        p.inside = {cr.x + 40.f, cr.y + 40.f};
        // le bout : tire tout en bas une premiere fois
        b.move(p.thumb);
        b.down(p.thumb);
        b.move({p.thumb.x, p.thumb.y + 5000.f});
        b.up({p.thumb.x, p.thumb.y + 5000.f});
        b.frame();
        p.max = k.list->scrollOffset();
        p.viewport = cr.h;
        p.exact20 = expectedAfter(20.f, cr.h, p.max + cr.h, cr.h);
        probe(b, p);
    }
    {   // l'arbre
        const gfx::Rect cr = k.tree->contentRect();
        Probe p;
        p.name = "l'arbre";
        p.w = k.tree;
        p.offset = [&] { return k.tree->scrollOffset(); };
        p.toTop = [&] {
            for (int i = 0; i < 80; ++i) b.wheel({cr.x + 20.f, cr.y + 20.f}, 5.f);
        };
        p.thumb = {cr.right() - ui::kScrollBarThickness * 0.5f, cr.y + 4.f};
        p.trackLow = {p.thumb.x, cr.bottom() - 4.f};
        p.inside = {cr.x + 40.f, cr.y + 40.f};
        b.move(p.thumb);
        b.down(p.thumb);
        b.move({p.thumb.x, p.thumb.y + 5000.f});
        b.up({p.thumb.x, p.thumb.y + 5000.f});
        b.frame();
        p.max = k.tree->scrollOffset();
        p.viewport = cr.h;
        p.exact20 = expectedAfter(20.f, cr.h, p.max + cr.h, cr.h);
        probe(b, p);
    }
    {   // le tableau (sa barre sous les titres)
        const gfx::Rect cr = k.table->contentRect();
        Probe p;
        p.name = "le tableau";
        p.w = k.table;
        p.offset = [&] { return k.table->scrollOffset(); };
        p.toTop = [&] { k.table->setScrollOffset(0.f); };
        p.thumb = {cr.right() - ui::kScrollBarThickness * 0.5f, cr.y + 34.f};
        p.trackLow = {p.thumb.x, cr.bottom() - 4.f};
        p.inside = {cr.x + 40.f, cr.y + 60.f};
        b.move(p.thumb);
        b.down(p.thumb);
        b.move({p.thumb.x, p.thumb.y + 5000.f});
        b.up({p.thumb.x, p.thumb.y + 5000.f});
        b.frame();
        p.max = k.table->scrollOffset();
        // le corps sous la ligne des titres (28 px, comme l'essai de la 1.11.4) : ce qu'on voit, et la gouttiere
        const float body = cr.h - 28.f;
        p.viewport = body;
        p.exact20 = expectedAfter(20.f, body, p.max + body, body);
        p.tol = 1.f;
        probe(b, p);
    }
    {   // l'editeur de code (un decalage en lignes : une ligne de tolerance)
        const gfx::Rect r = k.code->bounds();
        const gfx::Rect inner = k.code->contentRect();
        Probe p;
        p.name = "l'editeur de code";
        p.w = k.code;
        const float lh = b.r.lineHeight(b.theme.font.mono);   // la hauteur d'une ligne, comme l'editeur la dessine
        p.offset = [&, lh] { return static_cast<float>(k.code->firstVisibleLine()) * lh; };
        p.toTop = [&] { k.code->scrollToLine(0); };
        p.thumb = {r.right() - 6.f, inner.y + 4.f};
        p.trackLow = {p.thumb.x, inner.bottom() - 4.f};
        p.inside = {inner.x + 60.f, inner.y + 40.f};
        p.tol = lh;
        b.move(p.thumb);
        b.down(p.thumb);
        b.move({p.thumb.x, p.thumb.y + 5000.f});
        b.up({p.thumb.x, p.thumb.y + 5000.f});
        b.frame();
        p.max = p.offset();
        p.viewport = inner.h;
        probe(b, p);
    }
    {   // un panneau qui defile (les dialogues)
        const gfx::Rect pb = k.panel->bounds();
        Probe p;
        p.name = "un panneau";
        p.w = nullptr;
        p.offset = [&] { return k.panel->scrollOffset().y; };
        p.toTop = [&] {
            for (int i = 0; i < 80; ++i) b.wheel({pb.x + 20.f, pb.y + 20.f}, 5.f);
        };
        // sa barre est un enfant (ui::ScrollBar) : c'est lui qui tient la souris
        ui::Widget* bar = nullptr;
        for (const auto& c : k.panel->children())
            if (dynamic_cast<ui::ScrollBar*>(c.get()) && c->bounds().h > c->bounds().w) bar = c.get();
        check(bar != nullptr, "un panneau : sa barre verticale");
        if (bar) {
            p.w = bar;
            p.thumb = {bar->bounds().x + bar->bounds().w * 0.5f, bar->bounds().y + 4.f};
            p.trackLow = {p.thumb.x, bar->bounds().bottom() - 4.f};
            p.inside = {pb.x + 40.f, pb.y + 40.f};
            b.move(p.thumb);
            b.down(p.thumb);
            b.move({p.thumb.x, p.thumb.y + 5000.f});
            b.up({p.thumb.x, p.thumb.y + 5000.f});
            b.frame();
            p.max = p.offset();
            p.viewport = bar->bounds().h;
            p.tol = 1.5f;
            probe(b, p);
        }
    }
    {   // ui::ScrollArea : le volet qui dessine tout (sa barre entre deux colonnes, sous un titre)
        Volet& v = *k.volet;
        const gfx::Rect vb = v.bounds();
        const gfx::Rect tr = v.bar.track();
        check(tr.x == vb.x + Volet::kLeftW + 5.f && tr.y == vb.y + Volet::kTitle, "le volet : sa gouttiere entre ses deux colonnes, sous le titre");
        Probe p;
        p.name = "un volet qui defile lui-meme (ui::ScrollArea)";
        p.w = &v;
        p.offset = [&] { return v.scroll; };
        p.toTop = [&] {
            v.scroll = 0.f;
            v.invalidateLayout();
        };
        p.thumb = {tr.x + tr.w * 0.5f, tr.y + 4.f};
        p.trackLow = {p.thumb.x, tr.bottom() - 4.f};
        p.inside = {vb.x + 60.f, vb.y + 60.f};
        p.max = Volet::kContent - vb.h;
        p.viewport = vb.h;
        p.exact20 = expectedAfter(20.f, tr.h, Volet::kContent, vb.h);
        p.tol = 0.5f;
        probe(b, p);
        // le volet refait sa mise en page quand il defile (ses enfants suivent)
        const int before = v.layouts;
        b.move(p.thumb);
        b.down(p.thumb);
        b.move({p.thumb.x, p.thumb.y + 30.f});
        b.up({p.thumb.x, p.thumb.y + 30.f});
        b.frame();
        check(v.layouts > before && v.scroll > 0.f, "le volet : tire, sa mise en page est refaite");
        // la molette hors de sa partie gauche (sur sa colonne de droite) : pas pour lui
        const float at = v.scroll;
        b.wheel({vb.x + Volet::kLeftW + 120.f, vb.y + 100.f}, -2.f);
        b.frame();
        check(near(v.scroll, at, 0.01f), "le volet : la molette sur sa colonne de droite ne le fait pas defiler");
        // le survol du pouce l'eclaire
        const gfx::Rect th = v.bar.thumb(v.scroll);
        b.move({th.x + th.w * 0.5f, th.y + th.h * 0.5f});
        check(v.bar.hot(), "le volet : le pouce survole s'eclaire");
        b.move({vb.x + 30.f, vb.y + 30.f});
        check(!v.bar.hot(), "... et s'eteint quand la souris le quitte");
        // cache pendant le glisser : la souris rendue, le glisser fini
        b.move({th.x + th.w * 0.5f, th.y + 4.f});
        b.down({th.x + th.w * 0.5f, th.y + 4.f});
        check(v.bar.dragging(), "le volet : le glisser commence");
        v.setVisibility(ui::Visibility::Collapsed);
        b.move({th.x, th.y + 80.f});
        v.setVisibility(ui::Visibility::Visible);
        b.frame();
        const float hidden = v.scroll;
        b.move({th.x, th.y + 160.f});
        b.frame();
        check(!v.bar.dragging() && near(v.scroll, hidden, 0.01f) && !b.captured(), "le volet cache pendant le glisser : le glisser est fini, la souris rendue");
    }
    {   // la liste des suggestions d'un champ
        std::printf("-- la liste des suggestions d'un champ\n");
        ui::InputText& f = *k.field;
        b.click({f.bounds().x + 20.f, f.bounds().y + 10.f});   // le champ prend le clavier
        f.openSuggestions();
        b.frame();
        const ui::ScrollArea& bar = f.suggestionBar();
        check(f.suggestionsOpen() && f.suggestions().size() == 40 && bar.needed(), "40 choix, 10 montres : la barre");
        const gfx::Rect tr = bar.track();
        const gfx::Point thumb{tr.x + tr.w * 0.5f, tr.y + 4.f};
        b.move(thumb);
        b.down(thumb);
        check(f.holdsMouse() && f.suggestionsOpen() && f.text().empty(), "le pouce se prend - le clic ne choisit pas la ligne d'a cote");
        b.move({thumb.x, thumb.y + 40.f});
        b.frame();
        const std::size_t first1 = f.suggestionFirst();
        b.move({thumb.x, thumb.y + 80.f});
        b.frame();
        const std::size_t first2 = f.suggestionFirst();
        check(first1 > 0 && first2 > first1 && (first2 + 1 >= 2 * first1 && first2 <= 2 * first1 + 1),
              "tire de 40 px -> la ligne " + std::to_string(first1) + ", de 80 px -> " + std::to_string(first2) + " (le double, a une ligne pres)");
        check(f.suggestionIndex() >= first2 && f.suggestionIndex() < first2 + 10, "la ligne choisie reste dans ce qu'on voit");
        b.move({thumb.x + 400.f, thumb.y + 2000.f});
        b.up({thumb.x + 400.f, thumb.y + 2000.f});
        b.frame();
        check(f.suggestionFirst() == 30 && !f.holdsMouse() && f.suggestionsOpen(), "tire jusqu'en bas (la ligne 30, la derniere page), relache : la liste reste ouverte");
        // remonter, puis un clic dans la gouttiere sous le pouce : une page (9 lignes)
        b.move({thumb.x, tr.y + 2.f});
        b.down({thumb.x, bar.thumb(30.f * (tr.h / 10.f)).y + 4.f});
        b.move({thumb.x, tr.y - 500.f});
        b.up({thumb.x, tr.y - 500.f});
        b.frame();
        check(f.suggestionFirst() == 0, "tire jusqu'en haut : la premiere ligne");
        b.click({thumb.x, tr.bottom() - 4.f});
        check(f.suggestionFirst() == 9 && f.text().empty() && f.suggestionsOpen(), "un clic dans la gouttiere : une page (la ligne 9), rien de choisi");
        f.closeSuggestions();
        b.frame();
    }

    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
