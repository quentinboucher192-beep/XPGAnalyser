// =============================================================================
//  tests/ai_rapide_test.cpp - 1.22.7 : RETOUR ET CORRIGER RAPIDEMENT (la previsualisation)
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un dossier IA/ jetable :
//    1. le moteur : la variable NETTEMENT la plus proche (le meme nom aux
//       separateurs pres, les memes mots - un abrege permis -, une faute de frappe ;
//       les chiffres toujours les memes ; une seule candidate) ; la correction rapide
//       de chaque genre de ligne (une variable, une operation en erreur, des objets
//       sous l'en-tete, des objets sans variable, un autre avertissement) ;
//    2. RETOUR : la fenetre se ferme, rien n'a change (l'etat de la proposition, la
//       saisie) ; Refuser... dit qu'il refuse ;
//    3. Corriger rapidement sur une variable introuvable : en tete du menu, la fenetre
//       reste ouverte, l'erreur part, la correction se montre (CORRECTIONS, la bande,
//       l'objet encadre), Revenir la reprend (la bande, a la souris) ; les autres
//       corrections du menu ne ferment plus ; grisee avec la raison (une variable sans
//       candidate nette, des objets sans variable) ;
//    4. un constat du logiciel avec sa correction ; Revenir rend l'operation ;
//    5. une suppression refusee (l'element est cite ailleurs) : << references >> ;
//    6. des objets sous l'en-tete : << decaler : oui >> ;
//    7. Tout corriger rapidement : le compte (<< 3 erreurs corrigees, 2 restent >>) ;
//    8. une grosse proposition : << Reverification... >> sur le fil de fond, la liste
//       d'avant gardee, puis la correction montree.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiBuild.hpp"
#include "../src/app/ai/AiFixes.hpp"
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
namespace fx = app::ai::fixes;
namespace bd = app::ai::build;
using A = ai::Operation::Action;
using K = fx::Action::Kind;

namespace {

int g_ok = 0, g_fail = 0;
std::string g_clipboard;

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

const std::string kO = "\xC2\xAB ";   // << et son espace
const std::string kF = " \xC2\xBB";   // >>
std::string q(const std::string& s) { return kO + s + kF; }
const std::string kArrow = " \xE2\x86\x92 ";
const std::string kQuick = "Corriger rapidement";
const std::string kQuickly = " (corrig\xC3\xA9 rapidement)";
const std::string kAll = "Tout corriger rapidement";
const std::string kRecheck = "Rev\xC3\xA9rification";

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Un renderer qui retient les textes et les cadres.
struct Recorder final : gfx::IRenderer {
    struct Stroke {
        gfx::Rect  r;
        gfx::Color c;
        float      w;
    };
    std::vector<std::string> texts;
    std::vector<Stroke>      strokes;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect& r, gfx::Color c, float w) override { strokes.push_back({r, c, w}); }
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
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
};

// ---- le projet ---------------------------------------------------------------------------------------------------------
// Vue_Station (1920 x 1080), l'en-tete Entete_Usine (80 px) ; les variables Vanne_Commande, Vanne_Ouverte, Pompe_1_Marche,
// Pompe_3_Marche, Ancienne_Cmd (lue par Voyant_Ancien, deja dans la vue) ; Titre_Haut et Btn_Haut en haut de la vue (sous un
// futur en-tete).
hmi::Id seedProject(hmi::Project& p) {
    p.config.width = 1920;
    p.config.height = 1080;
    p.views = {hmi::makeView(p, "Vue_Station"), hmi::makeView(p, "Entete_Usine")};
    p.views[1].role = "entete";
    p.views[1].height = 80;
    for (const char* name : {"Vanne_Commande", "Vanne_Ouverte", "Pompe_1_Marche", "Pompe_3_Marche", "Ancienne_Cmd"}) {
        hmi::Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = "BOOL";
        v.initial = "FALSE";
        p.programs.variables.push_back(v);
    }
    hmi::View& v = p.views[0];
    const hmi::Id layer = v.layers.empty() ? hmi::kNoId : v.layers.front().id;
    const auto obj = [&](hmi::Kind k, const char* name, double x, double y, double w, double h) -> hmi::Object& {
        hmi::Object o = hmi::makeObject(k, p.allocate(), name, x, y, layer);
        o.setNumber("w", w);
        o.setNumber("h", h);
        v.objects.push_back(o);
        return v.objects.back();
    };
    obj(hmi::Kind::Text, "Titre_Haut", 20, 0, 300, 30);
    obj(hmi::Kind::Rectangle, "Btn_Haut", 20, 40, 120, 30);
    obj(hmi::Kind::Indicator, "Voyant_Ancien", 600, 500, 40, 40).set("variable", "Ancienne_Cmd");
    return v.id;
}

ai::Operation voyant(const std::string& name, const std::string& variable, int y) {
    ai::Operation op;
    op.action = A::Create;
    op.kind = "Indicator";
    op.name = name;
    op.props = {{"x", "40"}, {"y", std::to_string(y)}, {"variable", variable}};
    return op;
}

ai::Proposal base(hmi::Id view, const std::string& id) {
    ai::Proposal p;
    p.id = id;
    p.state = "proposee";
    p.view = "Vue_Station";
    p.viewId = view;
    p.request = "Ajoute la commande de la vanne";
    p.summary = "Commande de la vanne";
    return p;
}

// Cinq erreurs : Vanne.Cmd (Vanne_Commande : nettement), Pompe_1_Marhce (une faute de frappe : Pompe_1_Marche), Vanne.F0 (rien
// de net), Pompe_2.Marche (Pompe_1 et Pompe_3 : jamais un autre numero), un code brut illisible ; un reservoir sans variable
// (un avertissement).
ai::Proposal vanneProposal(hmi::Id view) {
    ai::Proposal p = base(view, "op-vanne");
    p.ops.push_back(voyant("Voyant_Cmd", "Vanne.Cmd", 200));
    p.ops.push_back(voyant("Voyant_Marche", "Pompe_1_Marhce", 260));
    p.ops.push_back(voyant("Voyant_F0", "Vanne.F0", 320));
    p.ops.push_back(voyant("Voyant_P2", "Pompe_2.Marche", 380));
    ai::Operation code;
    code.action = A::Code;
    code.code = "ceci n'est pas un code brut\nfin\nune ligne apres fin";
    p.ops.push_back(code);
    ai::Operation tank;
    tank.action = A::Create;
    tank.kind = "Tank";
    tank.name = "Cuve_Libre";
    tank.props = {{"x", "400"}, {"y", "300"}};
    p.ops.push_back(tank);
    return p;
}

// Un texte qui cite << Cuve.Inexistant >> : le logiciel propose << Cuve.Niveau >> (son verdict, garde dans la proposition).
ai::Proposal cuveProposal(hmi::Id view) {
    ai::Proposal p = base(view, "op-cuve");
    ai::Operation txt;
    txt.action = A::Create;
    txt.kind = "Text";
    txt.name = "Txt_Cuve";
    txt.props = {{"x", "40"}, {"y", "300"}, {"text", "Cuve.Inexistant"}};
    p.ops.push_back(txt);
    bd::Verdict v;
    v.ran = true;
    bd::Finding f;
    f.error = true;
    f.step = "Compiler";
    f.category = "Objet";
    f.message = "Cuve.Inexistant : nom inconnu";
    f.where = "Vue_Station . Txt_Cuve . text";
    f.view = "Vue_Station";
    f.objectName = "Txt_Cuve";
    f.property = "text";
    f.op = 0;
    f.fixFrom = "Cuve.Inexistant";
    f.fixTo = "Cuve.Niveau";
    v.findings = {f};
    v.errors = 1;
    p.extra = ai::json::Value::object();
    p.extra.set("logiciel", bd::toJson(v));
    return p;
}

// Supprimer la variable Ancienne_Cmd (lue par Voyant_Ancien) : refusee seule.
ai::Proposal removalProposal(hmi::Id view) {
    ai::Proposal p = base(view, "op-suppression");
    ai::Operation o;
    o.action = A::Remove;
    o.kind = "variable";
    o.objects = {"Ancienne_Cmd"};
    p.ops.push_back(o);
    return p;
}

// L'en-tete Entete_Usine sur Vue_Station, << decaler : non >> : Titre_Haut et Btn_Haut restent dessous (un avertissement).
ai::Proposal headerProposal(hmi::Id view) {
    ai::Proposal p = base(view, "op-entete");
    ai::Operation o;
    o.action = A::ViewEdit;
    o.name = "Vue_Station";
    o.props = {{"entete", "Entete_Usine"}, {"decaler", "non"}};
    p.ops.push_back(o);
    return p;
}

// 44 voyants (une verification de fond) ; deux lisent Vanne.Cmd.
ai::Proposal bigProposal(hmi::Id view) {
    ai::Proposal p = base(view, "op-grosse");
    for (int i = 0; i < 44; ++i)
        p.ops.push_back(voyant("Voyant_" + std::to_string(i + 1), i == 7 || i == 30 ? "Vanne.Cmd" : "Pompe_1_Marche", 100 + (i % 20) * 30));
    return p;
}

// ---- le panneau --------------------------------------------------------------------------------------------------------
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    const ui::Theme              theme = ui::Theme::dark();
    const std::string            convId = "c-rapide";

    Fixture(const std::string& name, const std::function<ai::Proposal(hmi::Id)>& make) {
        dir = fs::temp_directory_path() / ("xpg_ai_rapide_" + name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        view = seedProject(doc->project);
        ai::Conversation c;
        c.id = convId;
        c.title = "La vanne";
        c.created = c.modified = ai::isoDate(ai::now());
        ai::Message ask;
        ask.id = "m1";
        ask.role = "utilisateur";
        ask.date = c.created;
        ask.text = "Ajoute la commande de la vanne";
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
        st.applyWithErrors = true;
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
    void settle(int m, ai::AiPreviewDialog* pv = nullptr) {
        for (int guard = 0; guard < 4000 && panel->chat().checking(m); ++guard) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            step();
        }
        step();
        if (pv) update(*pv);
    }
    void update(ai::AiPreviewDialog& pv) {
        t += 0.05;
        pv.Update(menu::FrameContext{0.05, t, &theme, {1600.f, 900.f}, 1.f});
    }
    Recorder draw(ai::AiPreviewDialog& pv) const {
        Recorder rec;
        pv.widgetRoot()->setBounds({0, 0, 1600, 900});
        pv.widgetRoot()->layout();
        pv.widgetRoot()->render(ui::PaintContext{rec, theme, pv.widgetRoot()->bounds(), t, nullptr});
        return rec;
    }
    ai::AiPreviewDialog* preview(int m) {
        panel->openPreview(m);
        mm.applyPending();
        auto* pv = dynamic_cast<ai::AiPreviewDialog*>(mm.top());
        if (pv && pv->widgetRoot()) (void)draw(*pv);
        return pv;
    }
    [[nodiscard]] ai::AiPreviewDialog* top() { return dynamic_cast<ai::AiPreviewDialog*>(mm.top()); }
};

gfx::Point centre(gfx::Rect r) { return {r.x + r.w / 2.f, r.y + r.h / 2.f}; }

// ---- 1. le moteur --------------------------------------------------------------------------------------------------------
void testEngine() {
    std::printf("[moteur]\n");
    const std::vector<std::string> vars = {"Vanne_Commande", "Vanne_Ouverte", "Pompe_1_Marche", "Pompe_3_Marche", "Ancienne_Cmd"};
    std::string why;
    check(fx::clearlyClosest("Vanne.Cmd", vars, &why) == "Vanne_Commande", "Vanne.Cmd : Vanne_Commande (Cmd abrege Commande)");
    check(fx::clearlyClosest("vanne_commande", vars) == "Vanne_Commande", "le meme nom, a la casse pres");
    check(fx::clearlyClosest("Vanne.Ouverte", vars) == "Vanne_Ouverte", "le meme nom, aux separateurs pres");
    check(fx::clearlyClosest("Pompe_1_Marhce", vars) == "Pompe_1_Marche", "une faute de frappe (deux lettres)");
    why.clear();
    check(fx::clearlyClosest("Pompe_2_Marche", vars, &why).empty() && has(why, "Aucune"), "Pompe_2 : jamais Pompe_1 ni Pompe_3 (" + why + ")");
    why.clear();
    check(fx::clearlyClosest("Vanne.F0", vars, &why).empty() && has(why, "nettement"), "Vanne.F0 : rien de net (" + why + ")");
    why.clear();
    check(fx::clearlyClosest("Vanne.Cmd", {"Vanne_Commande", "Vanne_Cmde"}, &why).empty() && has(why, "Plusieurs"),
          "deux candidates au meme rang : aucune (" + why + ")");
    check(fx::clearlyClosest("Vanne.Cmd", {"Vanne_Cmd", "Vanne_Commande"}) == "Vanne_Cmd", "le meme nom passe devant un abrege");

    // La correction rapide de chaque genre de ligne, lue dans la verification.
    auto doc = std::make_shared<hmi::Document>();
    const hmi::Id view = seedProject(doc->project);
    {
        const ai::Proposal p = vanneProposal(view);
        const ai::Check ck = ai::validate(doc->project, p);
        const auto lines = fx::errorLines(ck);
        const auto quickOf = [&](const std::string& part) {
            for (const auto& l : lines)
                if (has(l.text, part)) return fx::quick(p, ck, l, &doc->project);
            return fx::Action{};
        };
        const fx::Action cmd = quickOf("Vanne.Cmd");
        check(cmd.kind == K::Quick && cmd.enabled && cmd.does == K::Use && cmd.name == "Vanne.Cmd" && cmd.value == "Vanne_Commande",
              "une variable introuvable : Utiliser la nettement plus proche");
        check(has(cmd.note, "Vanne_Commande"), "le menu dit ce qui changera : " + cmd.note);
        const fx::Action f0 = quickOf("Vanne.F0");
        check(!f0.enabled && has(f0.why, "nettement") && has(f0.why, "Choisir une variable"), "sans candidate nette : grisee, et pourquoi (" + f0.why + ")");
        const fx::Action code = quickOf("Code brut");
        check(code.enabled && code.does == K::RemoveOps && code.ops == std::vector<std::size_t>{4}, "une operation en erreur : la retirer");
        const auto menu = fx::menuFor(p, ck, lines.front(), &doc->project);
        check(!menu.actions.empty() && menu.actions.front().kind == K::Quick && menu.actions.front().label == kQuick, "<< Corriger rapidement >> en tete du menu");
        // un avertissement : les objets sans variable - rien de sur
        const auto warns = fx::warningLines(ck);
        bool unbound = false;
        for (const auto& w : warns)
            if (has(w.text, "aucune variable")) {
                const fx::Action a = fx::quick(p, ck, w, nullptr);
                unbound = !a.enabled && has(a.why, "Rien de s\xC3\xBBr") && has(a.why, "n'en invente pas");
            }
        check(unbound, "des objets sans variable : grisee, l'Assistant n'invente pas de variable");
    }
    {   // des objets sous l'en-tete (decaler : non) : decaler : oui
        const ai::Proposal p = headerProposal(view);
        const ai::Check ck = ai::validate(doc->project, p);
        const auto warns = fx::warningLines(ck);
        check(!warns.empty(), "l'en-tete recouvre des objets : un avertissement (" + std::to_string(ck.warnings.size()) + ")");
        if (!warns.empty()) {
            const fx::Action a = fx::quick(p, ck, warns.front(), nullptr);
            check(a.enabled && a.does == K::Prop && a.name == "decaler" && a.value == "oui" && a.ops == std::vector<std::size_t>{0},
                  "decaler : oui (l'operation sait les deplacer)");
        }
    }
    {   // une suppression refusee (l'element est cite ailleurs) : references
        const ai::Proposal p = removalProposal(view);
        const ai::Check ck = ai::validate(doc->project, p);
        const auto lines = fx::errorLines(ck);
        check(!lines.empty() && has(lines.front().text, "impossible seule"), "supprimer Ancienne_Cmd seule : refusee (" + (lines.empty() ? std::string() : lines.front().text) + ")");
        if (!lines.empty()) {
            const fx::Action a = fx::quick(p, ck, lines.front(), &doc->project);
            check(a.enabled && a.does == K::Strategy && a.value == "references" && a.ops == std::vector<std::size_t>{0}, "sa correction rapide : la strategie references");
        }
    }
}

// ---- 2. RETOUR -------------------------------------------------------------------------------------------------------------
void testBack() {
    std::printf("[retour]\n");
    Fixture f("retour", vanneProposal);
    const int m = f.open();
    if (m < 0) return;
    f.panel->prompt().setText("Ma saisie en cours");
    auto* pv = f.preview(m);
    check(pv != nullptr, "la previsualisation s'ouvre");
    if (!pv) return;
    ui::Button* back = pv->backButton();
    check(back && back->text() == "Retour" && back->tooltip() == "Fermer la pr\xC3\xA9visualisation sans rien d\xC3\xA9" "cider (\xC3\x89" "chap)",
          "le bouton Retour et son infobulle");
    check(pv->cancelButton() && pv->cancelButton()->text() == "Refuser\xE2\x80\xA6" && has(pv->cancelButton()->tooltip(), "Refuser la proposition"),
          "<< Refuser... >> dit qu'il refuse");
    check(back && pv->applyButton() && pv->cancelButton() && back->bounds().x < pv->cancelButton()->bounds().x && back->bounds().x < pv->applyButton()->bounds().x,
          "Retour : a gauche des autres");
    const ai::Proposal before = *f.as().proposal(m);
    const gfx::Point at = back ? centre(back->bounds()) : gfx::Point{};
    (void)f.mm.HandleEvent(ui::MouseDown{at, ui::MouseButton::Left, 1, {}});
    (void)f.mm.HandleEvent(ui::MouseUp{at, ui::MouseButton::Left, {}});
    f.mm.applyPending();
    const ai::Proposal* after = f.as().proposal(m);
    check(f.top() == nullptr, "un clic sur Retour : la fenetre se ferme");
    check(after && after->state == "proposee" && after->choices == before.choices && after->excluded == before.excluded && after->ops == before.ops,
          "rien n'a change : la proposition reste a decider, telle quelle");
    check(f.panel->prompt().text() == "Ma saisie en cours", "la saisie n'est pas touchee");
}

// ---- 3. Corriger rapidement : une variable introuvable -------------------------------------------------------------------
void testVariable() {
    std::printf("[corriger rapidement : une variable]\n");
    Fixture f("variable", vanneProposal);
    const int m = f.open();
    if (m < 0) return;
    auto* pv = f.preview(m);
    if (!pv) return;
    const std::size_t vars = f.doc->project.programs.variables.size();
    const int k = indexHaving(pv->errorRows(), "Vanne.Cmd");
    check(k >= 0 && pv->openErrorMenu(static_cast<std::size_t>(k)), "le clic droit sur Vanne.Cmd");
    const auto items = pv->menuItems();
    check(!items.empty() && items.front() == kQuick, "<< Corriger rapidement >> en tete");
    check(pv->menuWhy(kQuick).empty() && pv->chooseMenuItem(kQuick), "Corriger rapidement (choisissable)");
    f.mm.applyPending();
    check(f.top() == pv && !pv->menuOpen(), "la fenetre reste ouverte, le menu se referme");
    check(indexHaving(pv->errorRows(), "Vanne.Cmd") < 0, "l'erreur part");
    const std::string row = q("Vanne.Cmd") + kArrow + "Vanne_Commande" + kQuickly;
    check(listed(pv->correctionRows(), row + " | remplac\xC3\xA9" "e"), "CORRECTIONS : << Vanne.Cmd -> Vanne_Commande (corrige rapidement) >>");
    check(listed(pv->flashedRows(), row), "sa ligne est mise en evidence");
    const hmi::Id voyant = pv->objectId("Voyant_Cmd");
    const std::vector<hmi::Id> lit = pv->highlighted();
    check(voyant != hmi::kNoId && std::find(lit.begin(), lit.end(), voyant) != lit.end(), "l'objet touche est encadre (Voyant_Cmd)");
    check(has(pv->bandText(), "Corrig\xC3\xA9 : " + q("Vanne.Cmd") + kArrow + "Vanne_Commande") && has(pv->bandText(), "Revenir"),
          "la bande : << Corrige : ... - Revenir >> (" + pv->bandText() + ")");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.saw("Corrig\xC3\xA9 : " + q("Vanne.Cmd")) && rec.saw("Revenir"), "la bande se dessine");
        check(rec.framed(pv->objectPoint("Voyant_Cmd"), f.theme.color.accent, 3.f), "le cadre de selection (la couleur d'accent) autour de Voyant_Cmd");
    }
    const ai::Proposal* p = f.as().proposal(m);
    check(p && p->choices.size() == 1 && p->choices.front().use == "Vanne_Commande", "la PROPOSITION change");
    check(f.doc->project.programs.variables.size() == vars && !f.doc->project.viewByName("Vue_Station")->objectByName("Voyant_Cmd"), "le projet, non");
    // REVENIR (la bande, a la souris)
    {
        (void)f.draw(*pv);
        const gfx::Rect r = pv->bandUndoRect();
        check(r.w > 0.f, "le lien Revenir de la bande");
        (void)f.mm.HandleEvent(ui::MouseDown{centre(r), ui::MouseButton::Left, 1, {}});
        f.mm.applyPending();
    }
    check(f.top() == pv && f.as().proposal(m)->choices.empty() && indexHaving(pv->errorRows(), "Vanne.Cmd") >= 0 && pv->correctionRows().empty()
              && pv->bandText().empty(),
          "Revenir : la correction est reprise, l'erreur revient, la bande s'en va");
    // UNE AUTRE CORRECTION DU MENU : Utiliser << Vanne_Ouverte >> - la fenetre reste, la meme mise en evidence (sans << rapidement >>)
    const int k2 = indexHaving(pv->errorRows(), "Vanne.Cmd");
    check(k2 >= 0 && pv->openErrorMenu(static_cast<std::size_t>(k2)) && pv->chooseMenuItem("Utiliser " + q("Vanne_Ouverte")), "Utiliser Vanne_Ouverte");
    f.mm.applyPending();
    check(f.top() == pv && listed(pv->correctionRows(), q("Vanne.Cmd") + kArrow + "Vanne_Ouverte | remplac\xC3\xA9" "e")
              && listed(pv->flashedRows(), q("Vanne.Cmd") + kArrow + "Vanne_Ouverte") && has(pv->bandText(), "Vanne_Ouverte")
              && !has(pv->bandText(), "rapidement"),
          "les autres corrections ne ferment plus : la meme bande, la meme mise en evidence");
    check(pv->undoBand() && f.as().proposal(m)->choices.empty(), "Revenir (le geste)");
    // GRISEE, AVEC LA RAISON
    const int f0 = indexHaving(pv->errorRows(), "Vanne.F0");
    check(f0 >= 0 && pv->openErrorMenu(static_cast<std::size_t>(f0)), "le clic droit sur Vanne.F0");
    check(has(pv->menuWhy(kQuick), "nettement") && !pv->chooseMenuItem(kQuick), "Vanne.F0 : grisee, avec la raison (" + pv->menuWhy(kQuick) + ")");
    check(listed(pv->menuItems(), "Choisir une variable\xE2\x80\xA6"), "<< Choisir une variable... >> reste juste dessous");
    const int p2 = indexHaving(pv->errorRows(), "Pompe_2.Marche");
    check(p2 >= 0 && !pv->quickFor(1, static_cast<std::size_t>(p2)).enabled, "Pompe_2.Marche : jamais Pompe_1 ni Pompe_3");
    const int w = indexHaving(pv->warningRows(), "aucune variable");
    check(w >= 0 && pv->openWarningMenu(static_cast<std::size_t>(w)) && listed(pv->menuItems(), kQuick) && has(pv->menuWhy(kQuick), "n'en invente pas"),
          "un avertissement sans correction sure : grisee, avec la raison");
    // Demander a l'IA : ferme toujours (le texte va dans la saisie)
    const int c = indexHaving(pv->errorRows(), "Code brut");
    check(c >= 0 && pv->openErrorMenu(static_cast<std::size_t>(c)) && pv->chooseMenuItem("Demander \xC3\xA0 l'IA de corriger"), "Demander a l'IA");
    f.mm.applyPending();
    check(f.top() == nullptr && has(f.panel->prompt().text(), "Corrige ta proposition"), "Demander a l'IA : la fenetre se ferme (comme avant), la saisie le dit");
}

// ---- 4. un constat du logiciel avec sa correction ------------------------------------------------------------------------
void testBuild() {
    std::printf("[corriger rapidement : le logiciel]\n");
    bd::setVerdictForTests([](const hmi::Project&, const ai::Proposal&) {
        bd::Verdict v;
        v.ran = true;
        return v;
    });
    {
        Fixture f("logiciel", cuveProposal);
        const int m = f.open();
        auto* pv = m < 0 ? nullptr : f.preview(m);
        check(pv && pv->buildRows().size() == 1 && pv->openBuildMenu(0), "le constat du logiciel, son menu");
        if (pv) {
            const auto items = pv->menuItems();
            check(items.size() >= 2 && items[0] == kQuick && items[1] == "Appliquer la correction du logiciel", "en tete : Corriger rapidement, puis la correction du logiciel");
            check(pv->quickFor(4, 0).does == K::BuildFix, "Corriger rapidement : la correction du logiciel");
            check(pv->chooseMenuItem(kQuick), "Corriger rapidement");
            f.mm.applyPending();
            const auto text = [&]() -> std::string {   // relue a chaque fois (la conversation peut changer de place)
                const ai::Proposal* p = f.as().proposal(m);
                if (p && !p->ops.empty())
                    for (const auto& [key, value] : p->ops.front().props)
                        if (key == "text") return value;
                return {};
            };
            check(f.top() == pv && text() == "Cuve.Niveau", "la fenetre reste, l'operation lit Cuve.Niveau (la proposition seule)");
            const int r = indexHaving(pv->correctionRows(), "Cuve.Niveau");
            check(r >= 0 && has(pv->correctionRows()[static_cast<std::size_t>(r)], kQuickly) && has(pv->correctionRows()[static_cast<std::size_t>(r)], "corrig\xC3\xA9" "e (logiciel)"),
                  "CORRECTIONS : la correction du logiciel, corrigee rapidement");
            check(has(pv->bandText(), "Cuve.Niveau") && !pv->flashedRows().empty(), "la bande et la mise en evidence");
            check(pv->undoBand() && text() == "Cuve.Inexistant" && pv->correctionRows().empty(), "Revenir : l'operation reprend sa forme d'avant");
            const ai::Proposal* p = f.as().proposal(m);
            check(p && !(p->extra.isObject() && p->extra.has("retouches")), "plus de retouche gardee");
        }
    }
    bd::setVerdictForTests(nullptr);
}

// ---- 5. une suppression refusee : references ---------------------------------------------------------------------------------
void testRemoval() {
    std::printf("[corriger rapidement : une suppression]\n");
    Fixture f("suppression", removalProposal);
    const int m = f.open();
    auto* pv = m < 0 ? nullptr : f.preview(m);
    if (!pv) return;
    const int k = indexHaving(pv->errorRows(), "impossible seule");
    check(k >= 0 && pv->quickFor(1, static_cast<std::size_t>(k)).does == K::Strategy, "la suppression refusee : sa correction rapide est la strategie");
    check(k >= 0 && pv->quickFix(1, static_cast<std::size_t>(k)), "Corriger rapidement");
    const auto strategy = [&]() -> std::string {   // relue a chaque fois
        const ai::Proposal* p = f.as().proposal(m);
        if (p && !p->ops.empty())
            for (const auto& [key, value] : p->ops.front().props)
                if (key == "strategie") return value;
        return {};
    };
    check(f.top() == pv && strategy() == "references", "la strategie passe a references (la proposition seule)");
    check(indexHaving(pv->errorRows(), "impossible seule") < 0, "l'erreur part (" + std::to_string(pv->errorRows().size()) + " erreurs)");
    check(indexHaving(pv->correctionRows(), "strat\xC3\xA9gie seul" + kArrow + "r\xC3\xA9" "f\xC3\xA9rences") >= 0, "CORRECTIONS : << strategie seul -> references >>");
    check(f.doc->project.variable("Ancienne_Cmd") != nullptr, "le projet n'a pas bouge");
    check(pv->undoBand() && strategy().empty(), "Revenir : la strategie d'avant");
    std::string why;
    check(!f.as().setDeleteStrategy(m, 0, "nimporte", &why) && has(why, "inconnue"), "une strategie inconnue : refusee (" + why + ")");
    check(!f.as().setDeleteStrategy(m, 3, "references", &why), "pas une suppression : refuse");
}

// ---- 6. des objets sous l'en-tete : decaler ---------------------------------------------------------------------------------
void testHeader() {
    std::printf("[corriger rapidement : l'en-tete]\n");
    Fixture f("entete", headerProposal);
    const int m = f.open();
    auto* pv = m < 0 ? nullptr : f.preview(m);
    if (!pv) return;
    const int w = indexHaving(pv->warningRows(), "recouvre");
    check(w >= 0 && pv->quickFor(2, static_cast<std::size_t>(w)).enabled, "l'avertissement de l'en-tete : une correction sure");
    check(w >= 0 && pv->quickFix(2, static_cast<std::size_t>(w)), "Corriger rapidement");
    const ai::Proposal* p = f.as().proposal(m);
    std::string shift;
    for (const auto& [key, value] : p->ops.front().props)
        if (key == "decaler") shift = value;
    check(shift == "oui" && indexHaving(pv->warningRows(), "recouvre") < 0, "decaler : oui - les objets se deplacent, l'avertissement part");
    check(indexHaving(pv->correctionRows(), "decaler : non" + kArrow + "oui") >= 0, "CORRECTIONS : << decaler : non -> oui >>");
}

// ---- 7. Tout corriger rapidement ------------------------------------------------------------------------------------------------
void testAll() {
    std::printf("[tout corriger rapidement]\n");
    Fixture f("tout", vanneProposal);
    const int m = f.open();
    auto* pv = m < 0 ? nullptr : f.preview(m);
    if (!pv) return;
    check(pv->errorRows().size() == 5 && pv->quickCount(1) == 3, "5 erreurs, 3 corrections sures (" + std::to_string(pv->quickCount(1)) + ")");
    check(pv->openHeadingMenu(1) && listed(pv->menuItems(), kAll) && pv->menuWhy(kAll).empty(), "le clic droit du titre ERREURS : Tout corriger rapidement");
    check(pv->chooseMenuItem(kAll) && f.as().proposal(m)->choices.size() == 2 && pv->errorRows().size() == 2, "par le menu du titre : 3 corrigees");
    check(pv->undoBand() && pv->errorRows().size() == 5, "Revenir");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.saw(kAll) && pv->quickAllRect(1).w > 0.f, "le lien du titre ERREURS");
        (void)f.mm.HandleEvent(ui::MouseDown{centre(pv->quickAllRect(1)), ui::MouseButton::Left, 1, {}});
        f.mm.applyPending();
    }
    const ai::Proposal* p = f.as().proposal(m);
    check(f.top() == pv && pv->errorRows().size() == 2, "2 erreurs restent (" + std::to_string(pv->errorRows().size()) + ")");
    check(p->choices.size() == 2 && p->excluded == std::vector<std::size_t>{4}, "deux variables choisies, le code brut retire");
    check(has(pv->bandText(), "3 erreurs corrig\xC3\xA9" "es, 2 restent : " + q("Vanne.F0")) && has(pv->bandText(), q("Pompe_2.Marche")),
          "le compte : << " + pv->bandText() + " >>");
    check(pv->flashedRows().size() == 3, "les trois corrections mises en evidence");
    check(pv->undoBand() && f.as().proposal(m)->choices.empty() && f.as().proposal(m)->excluded.empty() && pv->errorRows().size() == 5,
          "Revenir : les trois reprises d'un coup");
    check(pv->quickFixAll(1) == 3, "Tout corriger rapidement (le geste) : 3");
}

// ---- 8. une grosse proposition : la reverification de fond ------------------------------------------------------------------------
void testBig() {
    std::printf("[grosse proposition]\n");
    Fixture f("grosse", bigProposal);
    const int m = f.open();
    if (m < 0) return;
    f.settle(m);
    auto* pv = f.preview(m);
    if (!pv) return;
    f.update(*pv);
    check(!pv->waiting() && pv->errorRows().size() == 1, "44 operations, verifiees sur le fil de fond : une ligne d'erreur");
    check(pv->quickFix(1, 0), "Corriger rapidement");
    check(pv->waiting() && pv->rechecking() && has(pv->bandText(), kRecheck) && pv->errorRows().size() == 1 && f.top() == pv,
          "la proposition se reverifie en fond : << Reverification... >>, la liste d'avant reste, la fenetre garde la main");
    {
        const Recorder rec = f.draw(*pv);
        check(rec.saw(kRecheck), "<< Reverification... >> se dessine");
    }
    f.settle(m, pv);
    for (int guard = 0; guard < 20 && pv->waiting(); ++guard) f.settle(m, pv);
    check(!pv->waiting() && !pv->rechecking() && pv->errorRows().empty(), "la reverification finie : l'erreur part");
    check(listed(pv->flashedRows(), q("Vanne.Cmd") + kArrow + "Vanne_Commande" + kQuickly), "puis la correction est montree");
    check(has(pv->bandText(), "Vanne_Commande") && !has(pv->bandText(), kRecheck), "la bande : la correction, sans << Reverification >>");
}

} // namespace

int main() {
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        s.clipboardSet = [](std::string_view t) { g_clipboard = std::string(t); };
        s.clipboardGet = [] { return g_clipboard; };
        ui::installPlatformServices(std::move(s));
    }
    testEngine();
    testBack();
    testVariable();
    testBuild();
    testRemoval();
    testHeader();
    testAll();
    testBig();
    std::printf("ai_rapide_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
