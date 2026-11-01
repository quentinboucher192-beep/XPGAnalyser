// =============================================================================
//  tests/ai_vues_test.cpp - 1.22.6 : LA PREVISUALISATION DE TOUTES LES VUES
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un dossier IA/ jetable : une proposition qui cree
//  trois vues (une vue, une popup, un symbole) et modifie un objet d'une vue
//  existante (Vue_Station) ; une variable introuvable sur la popup (une erreur).
//    1. le moteur (ai::previews) : quatre apercus dans l'ordre de la proposition
//       (la vue principale d'abord, a l'identique de ai::preview), neuves ou
//       modifiee, leur role, ce que chacune change ; une proposition d'une vue ;
//       une proposition du projet seul ;
//    2. la previsualisation : la barre << Vue 1/4 : Vue_Armoires (neuve) >>, les
//       fleches (clic, PageUp / PageDown, Ctrl+gauche / droite), la liste des vues
//       (ce que chacune change, ses erreurs) ; a gauche, la vue de chaque objet
//       (marquee pour la vue montree, grisee sinon) ; choisir un objet d'une autre
//       vue y fait passer l'apercu et l'encadre ; << Cette vue seulement >> ; les
//       titres << (5, 1 sur cette vue) >> ; l'erreur de la popup : son cadre sur la
//       popup seulement, la barre le dit ; un clic sur un objet de l'apercu ;
//    3. une proposition d'une seule vue : << Vue 1/1 >>, des fleches grisees, rien
//       de plus a gauche ;
//    4. une grosse proposition (au moins 40 operations) sur deux vues : ses vues
//       se calculent sur un fil de fond, la carte d'abord.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
bool listed(const std::vector<std::string>& lines, const std::string& exact) { return std::find(lines.begin(), lines.end(), exact) != lines.end(); }
int indexHaving(const std::vector<std::string>& lines, const std::string& part) {
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (has(lines[i], part)) return static_cast<int>(i);
    return -1;
}

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const std::string kDot = " \xC2\xB7 ";
const std::string kModified = "modifi\xC3\xA9" "e";
std::string q(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }
std::string missingText(const std::string& name) {
    return "La variable " + q(name) + " n'existe pas dans le projet : l'Assistant ne l'invente pas. "
           "Choisis une variable existante, ou demande sa cr\xC3\xA9" "ation.";
}

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Un renderer qui retient les textes, leurs couleurs, et les cadres (place, couleur, epaisseur).
struct Recorder final : gfx::IRenderer {
    struct Stroke {
        gfx::Rect  r;
        gfx::Color c;
        float      w;
    };
    std::vector<std::string>                        texts;
    std::vector<std::pair<std::string, gfx::Color>> coloured;
    std::vector<Stroke>                             strokes;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect& r, gfx::Color c, float w) override { strokes.push_back({r, c, w}); }
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color c) override {
        texts.emplace_back(t);
        coloured.emplace_back(std::string(t), c);
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
    [[nodiscard]] bool framed(gfx::Point at, gfx::Color c, float w) const {
        return std::any_of(strokes.begin(), strokes.end(), [&](const Stroke& s) { return same(s.c, c) && s.w == w && s.r.contains(at); });
    }
    [[nodiscard]] std::size_t frames(gfx::Color c, float w) const {
        return static_cast<std::size_t>(std::count_if(strokes.begin(), strokes.end(), [&](const Stroke& s) { return same(s.c, c) && s.w == w; }));
    }
    [[nodiscard]] bool inColour(std::string_view exact, gfx::Color c) const {
        return std::any_of(coloured.begin(), coloured.end(), [&](const auto& t) { return t.first == exact && same(t.second, c); });
    }
};

// ---- le projet et les propositions -----------------------------------------------------------------------------------
hmi::Id seedProject(hmi::Project& p) {
    p.views = {hmi::makeView(p, "Vue_Station")};
    const auto add = [&](const char* name, const char* type) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = std::string(type) == "BOOL" ? "FALSE" : "0";
        p.programs.variables.push_back(v);
    };
    add("Pompe_1_Marche", "BOOL");
    add("Vanne_Ouverte", "BOOL");
    hmi::View& v = p.views.front();
    v.objects.push_back(hmi::makeObject(hmi::Kind::Indicator, p.allocate(), "Voyant_Existant", 100, 100, v.layers.empty() ? hmi::kNoId : v.layers.front().id));
    return v.id;
}

using A = ai::Operation::Action;
ai::Operation viewOp(const std::string& name, const std::string& role) {
    ai::Operation op;
    op.action = A::View;
    op.name = name;
    if (!role.empty()) op.props = {{"role", role}};
    return op;
}
ai::Operation create(const std::string& kind, const std::string& name, std::vector<std::pair<std::string, std::string>> props) {
    ai::Operation op;
    op.action = A::Create;
    op.kind = kind;
    op.name = name;
    op.props = std::move(props);
    return op;
}

// Trois vues neuves (une vue, une popup, un symbole) et un objet modifie dans Vue_Station ; Voyant_Popup lit une variable
// introuvable (une erreur, sur la popup).
ai::Proposal multiProposal(hmi::Id) {
    ai::Proposal p;
    p.id = "op-vues";
    p.state = "proposee";
    p.view = "Vue_Armoires";
    p.request = "Cree la vue des armoires, sa popup et le symbole de la pompe";
    p.summary = "Armoires";
    p.ops.push_back(viewOp("Vue_Armoires", ""));
    p.ops.push_back(create("Indicator", "Voyant_Armoire", {{"x", "40"}, {"y", "40"}, {"variable", "Pompe_1_Marche"}}));
    p.ops.push_back(viewOp("Popup_Vanne", "popup"));
    p.ops.push_back(create("Indicator", "Voyant_Popup", {{"x", "40"}, {"y", "40"}, {"variable", "Vanne.Inconnue"}}));
    p.ops.push_back(create("Indicator", "Voyant_Popup_Ok", {{"x", "40"}, {"y", "160"}, {"variable", "Vanne_Ouverte"}}));
    p.ops.push_back(viewOp("Sym_Pompe", "symbole"));
    p.ops.push_back(create("Rectangle", "Corps", {{"x", "20"}, {"y", "20"}, {"w", "120"}, {"h", "120"}}));
    ai::Operation modify;
    modify.action = A::Modify;
    modify.name = "Voyant_Existant";
    modify.target = "Vue_Station";
    modify.props = {{"x", "300"}};
    p.ops.push_back(modify);
    return p;
}

// Deux objets dans la vue ouverte : une seule vue.
ai::Proposal singleProposal(hmi::Id view) {
    ai::Proposal p;
    p.id = "op-une";
    p.state = "proposee";
    p.view = "Vue_Station";
    p.viewId = view;
    p.request = "Ajoute deux voyants";
    p.summary = "Voyants";
    p.ops.push_back(create("Indicator", "Voyant_A", {{"x", "40"}, {"y", "300"}, {"variable", "Pompe_1_Marche"}}));
    p.ops.push_back(create("Indicator", "Voyant_B", {{"x", "40"}, {"y", "360"}, {"variable", "Vanne_Ouverte"}}));
    return p;
}

// Une grosse proposition (44 operations) : une vue neuve, 42 voyants, un objet modifie dans Vue_Station.
ai::Proposal bigProposal(hmi::Id) {
    ai::Proposal p;
    p.id = "op-grosse";
    p.state = "proposee";
    p.view = "Vue_Grande";
    p.request = "Une grande page de voyants";
    p.summary = "Voyants";
    p.ops.push_back(viewOp("Vue_Grande", ""));
    for (int i = 0; i < 42; ++i)
        p.ops.push_back(create("Indicator", "Voyant_" + std::to_string(i + 1), {{"x", std::to_string(20 + (i % 10) * 60)}, {"y", std::to_string(20 + (i / 10) * 60)}, {"variable", "Pompe_1_Marche"}}));
    ai::Operation modify;
    modify.action = A::Modify;
    modify.name = "Voyant_Existant";
    modify.target = "Vue_Station";
    modify.props = {{"y", "500"}};
    p.ops.push_back(modify);
    return p;
}

const hmi::Object* named(const hmi::View& v, const std::string& name) {
    for (const auto& o : v.objects)
        if (o.name == name) return &o;
    return nullptr;
}
bool holds(const std::vector<hmi::Id>& ids, hmi::Id id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// ---- 1. le moteur -----------------------------------------------------------------------------------------------------
void testEngine() {
    std::printf("[moteur]\n");
    hmi::Project project;
    const hmi::Id station = seedProject(project);
    const ai::Proposal p = multiProposal(station);
    const auto all = ai::previews(project, p);
    std::vector<std::string> names;
    for (const auto& v : all) names.push_back(v.preview.view.name);
    check(names == std::vector<std::string>{"Vue_Armoires", "Popup_Vanne", "Sym_Pompe", "Vue_Station"},
          "quatre vues, dans l'ordre de la proposition (la principale d'abord)");
    if (all.size() != 4) return;
    check(all[0].created && all[1].created && all[2].created && !all[3].created, "trois neuves, Vue_Station modifiee");
    check(all[0].preview.view.role == "vue" && all[1].preview.view.role == "popup" && all[2].preview.view.role == "symbole", "leur role : vue, popup, symbole");
    check(std::all_of(all.begin(), all.end(), [](const ai::ViewPreview& v) { return v.preview.ok; }), "chacune se dessine");
    const ai::Preview main = ai::preview(project, p);
    check(main.ok && main.view.name == all[0].preview.view.name && main.view.objects.size() == all[0].preview.view.objects.size()
              && main.touched.created == all[0].preview.touched.created,
          "la premiere : celle de ai::preview, a l'identique");
    const auto* popupOk = named(all[1].preview.view, "Voyant_Popup_Ok");
    const auto* popupBad = named(all[1].preview.view, "Voyant_Popup");
    check(popupOk && popupBad && holds(all[1].preview.touched.created, popupOk->id) && holds(all[1].preview.touched.created, popupBad->id),
          "la popup : ses deux voyants crees (celui en erreur aussi, comme l'apercu)");
    const auto* corps = named(all[2].preview.view, "Corps");
    check(corps && all[2].preview.touched.created == std::vector<hmi::Id>{corps->id}, "le symbole : Corps cree");
    const auto* existing = named(all[3].preview.view, "Voyant_Existant");
    check(existing && all[3].preview.touched.modified == std::vector<hmi::Id>{existing->id} && all[3].preview.touched.created.empty()
              && existing->box().x == 300.0,
          "Vue_Station : Voyant_Existant modifie (x 300), rien de cree");
    check(project.views.size() == 1 && project.views.front().objects.front().box().x == 100.0, "le projet n'a pas change (une copie)");
    // Une seule vue : un apercu, celui de preview.
    const ai::Proposal one = singleProposal(station);
    const auto single = ai::previews(project, one);
    check(single.size() == 1 && single.front().preview.ok && single.front().preview.view.name == "Vue_Station" && !single.front().created
              && single.front().preview.touched.created.size() == 2,
          "une proposition d'une vue : un apercu (Vue_Station, deux objets crees)");
    // Le projet seul (des variables) : rien a dessiner, la raison de preview.
    ai::Proposal vars;
    vars.id = "op-vars";
    vars.state = "proposee";
    ai::Operation v;
    v.action = A::Variables;
    v.variables = {{"Debit", "REAL", "le debit", "0"}};
    vars.ops.push_back(v);
    const auto none = ai::previews(project, vars);
    check(none.size() == 1 && !none.front().preview.ok && has(none.front().preview.why, "Rien \xC3\xA0 dessiner"), "le projet seul : rien a dessiner (et pourquoi)");
}

// ---- le panneau -------------------------------------------------------------------------------------------------------
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    const std::string            convId = "c-vues";
    const ui::Theme              theme = ui::Theme::dark();

    Fixture(const std::string& name, const std::function<ai::Proposal(hmi::Id)>& make) {
        dir = fs::temp_directory_path() / ("xpg_ai_vues_" + name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        view = seedProject(doc->project);
        ai::Conversation c;
        c.id = convId;
        c.title = "Les armoires";
        c.created = c.modified = ai::isoDate(ai::now());
        ai::Message ask;
        ask.id = "m1";
        ask.role = "utilisateur";
        ask.date = c.created;
        ask.text = "Cree la vue des armoires";
        c.messages.push_back(ask);
        ai::Message answer;
        answer.id = "m2";
        answer.role = "assistant";
        answer.date = c.created;
        answer.state = "fini";
        answer.text = "Voici la proposition.";
        answer.proposal = make(view);
        c.messages.push_back(std::move(answer));
        check(ai::saveConversation(c), "la conversation est ecrite");
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [](const std::string&) {};
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 900, 900});
        panel->ensureLoaded();
        ai::Settings st = panel->assistant().settings();
        st.provider = "demo";
        panel->assistant().setSettings(st);
        panel->layout();
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    void step() {
        t += 0.05;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    int open() {
        const int i = as().indexOf(convId);
        check(i >= 0 && panel->selectConversation(i), "la conversation se rouvre");
        step();
        return as().proposal(1) ? 1 : -1;
    }
    ai::AiPreviewDialog* preview(int m) {
        panel->openPreview(m);
        mm.applyPending();
        auto* pv = dynamic_cast<ai::AiPreviewDialog*>(mm.top());
        if (pv) {
            update(*pv);
            (void)draw(*pv);
        }
        return pv;
    }
    void update(ai::AiPreviewDialog& pv) {
        t += 0.05;
        pv.Update(menu::FrameContext{0.05, t, &theme, {1600.f, 900.f}, 1.f});
    }
    Recorder draw(ai::AiPreviewDialog& pv) const {
        Recorder rec;
        pv.widgetRoot()->layout();
        pv.widgetRoot()->render(ui::PaintContext{rec, theme, pv.widgetRoot()->bounds(), t, nullptr});
        return rec;
    }
};

gfx::Point centre(gfx::Rect r) { return {r.x + r.w / 2.f, r.y + r.h / 2.f}; }
ui::KeyDown key(ui::Key k, bool ctrl = false) {
    ui::KeyDown e{k, {}, false};
    e.mods.ctrl = ctrl;
    return e;
}

// ---- 2. la previsualisation -------------------------------------------------------------------------------------------
void testDialog() {
    std::printf("[previsualisation]\n");
    Fixture f("quatre", multiProposal);
    const int m = f.open();
    check(m == 1, "la proposition des armoires");
    if (m < 0) return;
    auto* pv = f.preview(m);
    check(pv != nullptr, "la previsualisation s'ouvre");
    if (!pv) return;
    const auto& th = f.theme;
    // LA BARRE DES VUES
    check(!pv->viewsPending() && pv->viewCount() == 4 && pv->shownView() == 0, "quatre vues, la premiere montree (" + std::to_string(pv->viewCount()) + ")");
    check(pv->viewBarText() == "Vue 1/4 : Vue_Armoires (neuve)", "la barre : << " + pv->viewBarText() + " >>");
    check(pv->viewTitle(1) == "Vue 2/4 : Popup_Vanne (neuve, popup)" + kDot + "1 erreur", "la popup : son role, son erreur (" + pv->viewTitle(1) + ")");
    check(pv->viewTitle(2) == "Vue 3/4 : Sym_Pompe (neuve, symbole)" && pv->viewTitle(3) == "Vue 4/4 : Vue_Station (" + kModified + ")",
          "le symbole, la vue modifiee");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.saw("Vue 1/4 : Vue_Armoires (neuve)"), "la barre se dessine");
        check(pv->preview().view.name == "Vue_Armoires" && pv->objectId("Voyant_Armoire") != hmi::kNoId, "l'apercu : Vue_Armoires");
    }
    // LES FLECHES : le clic, PageUp / PageDown, Ctrl+gauche / droite ; au bout : rien.
    check(!pv->stepView(-1) && pv->shownView() == 0, "au debut : pas de precedente");
    (void)f.mm.HandleEvent(ui::MouseDown{centre(pv->viewBarRect(1)), ui::MouseButton::Left, 1, {}});
    check(pv->shownView() == 1 && pv->preview().view.name == "Popup_Vanne", "la fleche suivante : la popup");
    (void)f.mm.HandleEvent(ui::MouseDown{centre(pv->viewBarRect(-1)), ui::MouseButton::Left, 1, {}});
    check(pv->shownView() == 0, "la fleche precedente : la vue principale");
    (void)f.mm.HandleEvent(key(ui::Key::PageDown));
    check(pv->shownView() == 1, "PageDown : la suivante");
    (void)f.mm.HandleEvent(key(ui::Key::PageUp));
    check(pv->shownView() == 0, "PageUp : la precedente");
    (void)f.mm.HandleEvent(key(ui::Key::Right, true));
    (void)f.mm.HandleEvent(key(ui::Key::Right, true));
    check(pv->shownView() == 2 && pv->preview().view.name == "Sym_Pompe", "Ctrl+droite deux fois : le symbole");
    (void)f.mm.HandleEvent(key(ui::Key::Left, true));
    check(pv->shownView() == 1, "Ctrl+gauche : la popup");
    check(pv->stepView(2) && pv->shownView() == 3 && !pv->stepView(1), "la derniere : pas de suivante");
    check(pv->showView(0) && !pv->showView(4), "montrer une vue par son rang (hors de la liste : non)");
    // LE ZOOM : Ajuster vaut pour la vue montree (une autre vue : la vue entiere).
    pv->setPreviewZoom(3.f);
    check(pv->showView(1) && pv->previewZoom() == 1.f, "une autre vue : la vue entiere (Ajuster)");
    check(pv->showView(0), "retour a la principale");
    // LA LISTE DES VUES (un clic sur le titre)
    (void)f.draw(*pv);
    (void)f.mm.HandleEvent(ui::MouseDown{centre(pv->viewBarRect(0)), ui::MouseButton::Left, 1, {}});
    const auto items = pv->viewMenuItems();
    check(pv->viewMenuOpen() && items.size() == 4, "un clic sur le titre : la liste des vues (" + std::to_string(items.size()) + ")");
    if (items.size() == 4) {
        check(items[0] == "(o) Vue 1/4 : Vue_Armoires (neuve) | 1 objet cr\xC3\xA9\xC3\xA9", "la vue montree, ce qu'elle change : " + items[0]);
        check(items[1] == "Vue 2/4 : Popup_Vanne (neuve, popup)" + kDot + "1 erreur | 2 objets cr\xC3\xA9\xC3\xA9s", "la popup : ses objets, son erreur : " + items[1]);
        check(items[3] == "Vue 4/4 : Vue_Station (" + kModified + ") | 1 objet modifi\xC3\xA9", "la vue existante : un objet modifie : " + items[3]);
    }
    // Choisir au clavier dans la liste : la troisieme vue.
    for (int i = 0; i < 3; ++i) (void)f.mm.HandleEvent(key(ui::Key::Down));
    (void)f.mm.HandleEvent(key(ui::Key::Return));
    f.mm.applyPending();
    check(!pv->viewMenuOpen() && pv->shownView() == 2 && f.mm.top() == pv, "la liste : la troisieme vue choisie (Sym_Pompe), la fenetre reste");
    check(pv->showView(0), "retour a la principale");
    // A GAUCHE : la vue de chaque objet, marquee pour la vue montree.
    check(pv->rowView("Voyant_Armoire") == "Vue_Armoires" && pv->rowOnShownView("Voyant_Armoire"), "Voyant_Armoire : sur Vue_Armoires (montree)");
    check(pv->rowView("Corps") == "Sym_Pompe" && !pv->rowOnShownView("Corps"), "Corps : sur Sym_Pompe (une autre vue)");
    check(pv->rowView("Voyant_Existant") == "Vue_Station" && pv->rowView("Voyant_Popup") == "Popup_Vanne", "Voyant_Existant, Voyant_Popup : leur vue");
    check(pv->rowView(missingText("Vanne.Inconnue")) == "Popup_Vanne", "l'erreur dit sa vue (la popup)");
    pv->setSearch("Voyant_Existant");   // sa ligne en haut de la liste (elle est en bas)
    {
        const Recorder rec = f.draw(*pv);
        check(rec.inColour("Vue_Station", th.color.textDisabled), "une autre vue : son nom grise");
        check(!rec.inColour("Vue_Station", th.color.text), "pas marquee");
    }
    check(pv->showView(3), "Vue_Station montree");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.inColour("Vue_Station", th.color.text) && pv->rowOnShownView("Voyant_Existant"), "la vue montree : son nom marque");
    }
    pv->setSearch("Sym_Pompe");   // la recherche lit aussi la vue de chaque ligne
    check(listed(pv->visibleRows(), "Corps") && !listed(pv->visibleRows(), "Voyant_Existant"), "chercher une vue : ses lignes");
    pv->setSearch({});
    // LES TITRES : << (5, 1 sur cette vue) >>
    check(indexHaving(pv->visibleRows(), "OBJETS CONCERN\xC3\x89S (5, 1 sur cette vue)") >= 0, "le titre des objets : 5, 1 sur cette vue");
    check(pv->showView(1) && indexHaving(pv->visibleRows(), "OBJETS CONCERN\xC3\x89S (5, 2 sur cette vue)") >= 0, "sur la popup : 2 sur cette vue");
    // CHOISIR UN OBJET D'UNE AUTRE VUE : l'apercu y passe, puis l'encadre.
    check(pv->showView(0), "la principale");
    check(pv->selectRow("Corps") && pv->shownView() == 2 && pv->preview().view.name == "Sym_Pompe", "choisir Corps : l'apercu passe sur Sym_Pompe");
    const hmi::Id corps = pv->objectId("Corps");
    check(corps != hmi::kNoId && pv->highlighted() == std::vector<hmi::Id>{corps}, "Corps est encadre");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.framed(pv->objectPoint("Corps"), th.color.accent, 3.f), "le cadre de la ligne choisie, sur le symbole");
        check(rec.saw("Vue 3/4 : Sym_Pompe (neuve, symbole)"), "la barre le dit");
    }
    check(pv->selectRow("Voyant_Armoire") && pv->shownView() == 0 && pv->highlighted() == std::vector<hmi::Id>{pv->objectId("Voyant_Armoire")},
          "et retour : Voyant_Armoire sur la principale");
    // UN CLIC SUR UN OBJET DE L'APERCU : sa ligne.
    check(pv->showView(3), "Vue_Station");
    (void)f.draw(*pv);
    (void)f.mm.HandleEvent(ui::MouseDown{pv->objectPoint("Voyant_Existant"), ui::MouseButton::Left, 1, {}});
    check(pv->selectedRow() == "Voyant_Existant" && pv->shownView() == 3, "un clic sur Voyant_Existant : sa ligne (la vue reste)");
    // CETTE VUE SEULEMENT
    check(pv->showView(2), "le symbole");
    (void)f.draw(*pv);
    const gfx::Rect chip = pv->viewChipRect();
    (void)f.mm.HandleEvent(ui::MouseDown{centre(chip), ui::MouseButton::Left, 1, {}});
    auto shown = pv->visibleRows();
    check(chip.w > 0.f && pv->viewFilter() && listed(shown, "Corps") && !listed(shown, "Voyant_Armoire") && !listed(shown, "Voyant_Existant")
              && !listed(shown, "Voyant_Popup"),
          "Cette vue seulement : les lignes du symbole");
    check(indexHaving(shown, "OBJETS CONCERN\xC3\x89S") >= 0 && indexHaving(shown, "VARIABLES ASSOCI") < 0, "les titres de ses lignes, pas les autres");
    check(pv->showView(3) && listed(pv->visibleRows(), "Voyant_Existant") && !listed(pv->visibleRows(), "Corps"), "une autre vue : le filtre suit");
    (void)f.mm.HandleEvent(key(ui::Key::Escape));
    f.mm.applyPending();
    check(!pv->viewFilter() && listed(pv->visibleRows(), "Corps") && f.mm.top() == pv, "Echap : tout remontre (la fenetre reste)");
    // L'ERREUR DE LA POPUP : son cadre sur la popup seulement ; la barre le dit.
    check(pv->showView(0), "la principale");
    {
        const Recorder rec = f.draw(*pv);
        bool none = true;
        for (const auto& o : pv->preview().view.objects) none = none && pv->issueOf(o.id) == 0;
        check(none && rec.frames(th.color.error, 3.f) == 1, "sur Vue_Armoires : aucun cadre d'erreur (la legende seule)");
        check(!has(pv->viewBarText(), "erreur") && rec.saw("Erreurs (1)"), "la barre ne dit pas d'erreur ; la puce compte toute la proposition");
        check(has(pv->previewTooltip(centre(pv->chipRect(true))), "Sur : Popup_Vanne (1)"), "l'infobulle de la puce : ou sont les erreurs");
        check(rec.saw("Vue neuve " + q("Vue_Armoires") + " et 3 autres vues"), "l'en-tete : la vue principale et 3 autres vues");
        check(has(pv->previewTooltip(centre(pv->viewBarRect(0))), "la liste des vues"), "l'infobulle du titre de la barre");
    }
    check(pv->selectRow(missingText("Vanne.Inconnue")) && pv->shownView() == 1, "choisir l'erreur : l'apercu passe sur la popup");
    {
        const Recorder rec = f.draw(*pv);
        const hmi::Id bad = pv->objectId("Voyant_Popup");
        check(pv->issueOf(bad) == 2 && rec.framed(pv->objectPoint("Voyant_Popup"), th.color.error, 3.f), "Voyant_Popup encadre de rouge, sur la popup");
        check(!rec.framed(pv->objectPoint("Voyant_Popup_Ok"), th.color.error, 3.f), "son voisin, non");
        check(rec.saw("Vue 2/4 : Popup_Vanne (neuve, popup)" + kDot + "1 erreur"), "la barre : << ... 1 erreur >>");
    }
    (void)f.mm.HandleEvent(key(ui::Key::Escape));
    f.mm.applyPending();
    check(dynamic_cast<ai::AiPreviewDialog*>(f.mm.top()) == nullptr && f.as().proposal(m)->state == "proposee", "Echap : la fenetre se ferme, rien n'est decide");
}

// ---- 3. une seule vue ---------------------------------------------------------------------------------------------------
void testSingle() {
    std::printf("[une seule vue]\n");
    Fixture f("une", singleProposal);
    const int m = f.open();
    if (m < 0) return;
    auto* pv = f.preview(m);
    check(pv != nullptr, "la previsualisation s'ouvre");
    if (!pv) return;
    check(pv->viewCount() == 1 && pv->viewBarText() == "Vue 1/1 : Vue_Station (" + kModified + ")", "Vue 1/1 : << " + pv->viewBarText() + " >>");
    const Recorder rec = f.draw(*pv);
    check(rec.saw("Vue 1/1 : Vue_Station"), "la barre se dessine");
    check(!pv->stepView(1) && !pv->stepView(-1), "pas d'autre vue");
    (void)f.mm.HandleEvent(key(ui::Key::PageDown));
    check(pv->shownView() == 0 && f.mm.top() == pv, "PageDown : rien");
    (void)f.mm.HandleEvent(ui::MouseDown{centre(pv->viewBarRect(0)), ui::MouseButton::Left, 1, {}});
    check(!pv->viewMenuOpen(), "le titre : pas de liste");
    check(pv->viewChipRect().w <= 0.f && pv->rowView("Voyant_A").empty(), "ni la puce Cette vue, ni la vue de chaque ligne");
    check(indexHaving(pv->visibleRows(), "OBJETS CONCERN\xC3\x89S (2)") >= 0 && indexHaving(pv->visibleRows(), "sur cette vue") < 0, "les titres comme avant");
    check(pv->selectRow("Voyant_A") && pv->highlighted() == std::vector<hmi::Id>{pv->objectId("Voyant_A")}, "choisir une ligne l'encadre, comme avant");
}

// ---- 4. une grosse proposition : sur un fil de fond ---------------------------------------------------------------------
void testBackground() {
    std::printf("[grosse proposition]\n");
    Fixture f("grosse", bigProposal);
    const int m = f.open();
    if (m < 0) return;
    auto* pv = f.preview(m);
    check(pv != nullptr, "la previsualisation s'ouvre");
    if (!pv) return;
    for (int guard = 0; guard < 2000 && pv->waiting(); ++guard) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        f.step();
        f.update(*pv);
    }
    check(!pv->waiting(), "la verification de la carte (fil de fond) finie");
    check(pv->viewsPending() && pv->viewCount() == 1 && has(pv->viewBarText(), "Vue 1 : Vue_Grande"), "les vues se calculent sur un fil de fond : la carte d'abord");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.saw("les autres vues se calculent"), "la barre le dit");
    }
    for (int guard = 0; guard < 2000 && pv->viewsPending(); ++guard) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        f.update(*pv);
    }
    check(!pv->viewsPending() && pv->viewCount() == 2 && pv->viewTitle(1) == "Vue 2/2 : Vue_Station (" + kModified + ")", "puis les deux vues");
    check(pv->rowView("Voyant_Existant") == "Vue_Station" && pv->rowView("Voyant_7") == "Vue_Grande", "a gauche, la vue de chaque objet");
}

} // namespace

int main() {
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        ui::installPlatformServices(std::move(s));
    }
    testEngine();
    testDialog();
    testSingle();
    testBackground();
    std::printf("ai_vues_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
