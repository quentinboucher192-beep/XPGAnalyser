// =============================================================================
//  tests/ai_assistant_test.cpp - 1.14.2 : l'Assistant IA (la maquette), sans ecran
// -----------------------------------------------------------------------------
//  Le panneau "Assistant IA" et sa seance, pilotes comme dans l'appli (les
//  boutons, le clavier, les fenetres par un MenuManager), sur un vrai document
//  IHM et une vraie pile d'annulation, dans un dossier IA/ jetable :
//    - le JSON (lire, ecrire, garder l'inconnu) ;
//    - la premiere ouverture (les conversations de demonstration) ;
//    - creer, choisir, renommer (le champ, F2, Echap), supprimer (la confirmation) ;
//    - envoyer ; en attente, reflexion, ecriture mot a mot ; Arreter ;
//    - la discussion qui defile, la saisie toujours visible ; Entree, Maj+Entree ;
//    - joindre (la copie dans IA/pieces-jointes/, le refus d'un format, un fichier lache) ;
//    - les parametres (relus ; la cle API jamais sur le disque) ;
//    - la proposition (les comptes, les vrais genres et leurs vraies cles, une
//      propriete inventee refusee, la variable inventee refusee) ;
//    - Appliquer (le projet change, UN Ctrl+Z le defait), Refuser, le mode Direct,
//      la confirmation d'une suppression multiple, la variable choisie ou creee ;
//    - la persistance (ecrire, relire, un champ inconnu garde) ; une proposition
//      perimee refusee ;
//    - l'apercu dessine par les vrais painters (la fenetre, la carte du panneau).
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/app/ai/AiSettingsDialog.hpp"
#include "../src/app/hmi/HmiAskDialog.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
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

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string readAll(const fs::path& p) {
    std::string s;
    (void)core::readFileAll(p, s);
    return s;
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << content;
}

// Un renderer qui retient les textes et compte les formes (les painters y passent).
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    std::size_t shapes{0}, triangles{0};
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override { ++shapes; }
    void strokeRect(const gfx::Rect&, gfx::Color, float) override { ++shapes; }
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override { ++shapes; }
    void line(gfx::Point, gfx::Point, gfx::Color, float) override { ++shapes; }
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex*, std::size_t n) override { triangles += n; }
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
};

ui::Button* findButton(ui::Widget& root, std::string_view text) {
    if (auto* b = dynamic_cast<ui::Button*>(&root); b && b->text() == text) return b;
    for (const auto& c : root.children())
        if (auto* f = findButton(*c, text)) return f;
    return nullptr;
}

std::time_t fixedNow() {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 30;
    tm.tm_hour = 17;
    tm.tm_min = 35;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

// Un projet IHM, une vue ouverte, la pile de l'application, un MenuManager et le panneau.
struct Fixture {
    fs::path                       dir;
    hmi::DocumentPtr               doc = std::make_shared<hmi::Document>();
    core::CommandStack             stack;
    hmi::Id                        view{hmi::kNoId};
    std::vector<hmi::Id>           selection;
    menu::MenuManager              mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel>   panel;
    std::vector<std::string>       statuses;
    double                         t{100.0};
    bool                           memory{false};

    explicit Fixture(const std::string& name, bool fresh = true) {
        dir = fs::temp_directory_path() / ("xpg_ai_" + name);
        if (fresh) fs::remove_all(dir);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        auto& p = doc->project;
        hmi::View v = hmi::makeView(p, "Vue_Station");
        p.views = {v};
        view = p.views.front().id;
        hmi::Variable var;
        var.id = p.allocate();
        var.name = "Pompe_1_Marche";
        var.type = "BOOL";
        var.initial = "FALSE";
        p.programs.variables.push_back(var);
        hmi::Variable lvl;
        lvl.id = p.allocate();
        lvl.name = "Niveau_Cuve_1";
        lvl.type = "REAL";
        p.programs.variables.push_back(lvl);
        build();
    }
    void build() {
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [this] { return selection; };
        core.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        core.projectName = [] { return std::string("Station_Pompage"); };
        core.projectPath = [this] { return U(dir / "Station_Pompage"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [this](const std::string& s) { statuses.push_back(s); };
        ui.memoryAvailable = [this] { return memory; };
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 440, 900});
        panel->ensureLoaded();
        panel->layout();
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    [[nodiscard]] hmi::View& v() { return *doc->project.view(view); }
    void step(double dt = 0.05) {
        t += dt;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    void finish() {
        for (int guard = 0; (as().generating() || as().processing()) && guard < 4000; ++guard) step();
        step();
    }
    bool sendAndWait(const std::string& text) {
        const bool ok = panel->sendText(text);
        finish();
        return ok;
    }
    [[nodiscard]] int lastProposal() {
        const auto* c = as().current();
        if (!c) return -1;
        for (int i = static_cast<int>(c->messages.size()) - 1; i >= 0; --i)
            if (c->messages[static_cast<std::size_t>(i)].proposal) return i;
        return -1;
    }
    [[nodiscard]] std::size_t count(hmi::Kind k) {
        return static_cast<std::size_t>(std::count_if(v().objects.begin(), v().objects.end(), [k](const hmi::Object& o) { return o.kind == k; }));
    }
    [[nodiscard]] const hmi::Object* object(const std::string& name) {
        for (const auto& o : v().objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    [[nodiscard]] std::size_t files(const fs::path& sub) const {
        std::size_t n = 0;
        std::error_code ec;
        if (fs::is_directory(dir / "IA" / sub, ec))
            for (const auto& e : fs::recursive_directory_iterator(dir / "IA" / sub, ec)) n += e.is_regular_file() ? 1 : 0;
        return n;
    }
    void render(Recorder& rec, const ui::Theme& theme = ui::Theme::dark()) {
        panel->layout();
        panel->render(ui::PaintContext{rec, theme, panel->bounds(), t, nullptr});
    }
};

// ---------------------------------------------------------------- le JSON ----
void testJson() {
    std::printf("[json]\n");
    ai::json::Value v;
    std::string why;
    const std::string text = "{\"b\": 2, \"a\": [1, 2.5, true, null, \"x\\u00e9\\ud83d\\ude00\\n\"], \"o\": {\"k\": \"v\"}}";
    check(ai::json::parse(text, v, &why), "un objet se lit (" + why + ")");
    check(v.members().size() == 3 && v.members()[0].first == "b" && v.members()[1].first == "a", "l'ordre des cles est garde (b, a, o)");
    check(v.at("a").items().size() == 5 && v.at("a").items()[1].asNumber() == 2.5 && v.at("a").items()[2].asBool() && v.at("a").items()[3].isNull(),
          "les nombres, les booleens, nul");
    check(v.at("a").items()[4].asString() == "x\xC3\xA9\xF0\x9F\x98\x80\n", "\\u00e9, une paire de substitution, \\n : en UTF-8");
    ai::json::Value again;
    check(ai::json::parse(ai::json::write(v), again) && again == v, "ecrire puis relire : la meme valeur");
    check(!ai::json::parse("{\"a\": [1, 2,]}", again, &why) && why.find("ligne 1") != std::string::npos, "mal forme : refuse, avec la ligne (" + why + ")");
    const auto extra = ai::json::unknownMembers(v, {"a", "o"});
    check(extra.members().size() == 1 && extra.members()[0].first == "b", "les membres inconnus se mettent de cote");
}

// ---------------------------------------------- la premiere ouverture ----
void testFirstOpening() {
    std::printf("[premiere ouverture]\n");
    Fixture f("premiere");
    const auto& list = f.as().conversations();
    check(list.size() == 2, "deux conversations de demonstration (" + std::to_string(list.size()) + ")");
    check(std::all_of(list.begin(), list.end(), [](const ai::Conversation& c) { return c.demo; }), "marquees demo");
    check(f.files("conversations") == 2, "ecrites dans IA/conversations/");
    check(fs::exists(f.dir / "IA" / "parametres.json"), "IA/parametres.json ecrit");
    check(f.as().current() && f.as().current()->title == "D\xC3\xA9" "couvrir l'Assistant",
          "1.14.2 : la premiere ouverture montre Decouvrir l'Assistant, pas la conversation perimee");
    check(readAll(f.dir / "IA" / "activite.jsonl").find("\"type\":\"conversation\"") != std::string::npos, "une ligne dans activite.jsonl");
    // La seconde ouverture ne les recree pas.
    Fixture g("premiere", false);
    check(g.as().conversations().size() == 2, "rouvert : toujours deux (pas de doublon)");
}

// ---------------------------------------------------- les conversations ----
void testConversations() {
    std::printf("[conversations]\n");
    Fixture f("conversations");
    const std::size_t before = f.as().conversations().size();
    f.panel->newConversation();
    check(f.as().conversations().size() == before + 1 && f.as().currentIndex() == 0, "Nouvelle conversation : en tete, ouverte");
    check(f.as().current()->title == "Nouvelle conversation", "son titre d'attente");
    f.panel->newConversation();
    check(f.as().conversations().size() == before + 1, "une seconde fois sur une conversation vide : on y reste");
    check(f.sendAndWait(ai::demoSuggestions()[1]), "envoyer la premiere demande");
    check(f.as().current()->title == "Cuve et niveau", "le titre vient de la demande (" + f.as().current()->title + ")");
    const std::string id = f.as().current()->id;
    check(fs::exists(f.dir / "IA" / "conversations" / (id + ".json")), "le fichier de la conversation existe");
    // Choisir.
    check(f.panel->selectConversation(1) && f.as().currentIndex() == 1, "choisir la deuxieme");
    check(f.panel->selectConversation(0) && f.as().current()->id == id, "revenir a la premiere");
    // Renommer : le champ sur la ligne, Entree.
    f.panel->startRename(0);
    check(f.panel->renaming() && f.panel->renameField().visible(), "renommer : le champ s'ouvre sur la ligne");
    check(f.panel->renameField().text() == "Cuve et niveau", "le champ porte le titre");
    check(f.panel->commitRename("Ma cuve de test"), "Entree : renommee");
    check(f.as().current()->title == "Ma cuve de test" && !f.panel->renaming(), "le titre change, le champ se ferme");
    check(readAll(f.dir / "IA" / "conversations" / (id + ".json")).find("Ma cuve de test") != std::string::npos, "le nouveau titre est ecrit");
    // F2 puis Echap : rien ne change.
    f.panel->takeFocus();
    (void)f.panel->dispatch(ui::KeyDown{ui::Key::F2, {}, false});
    check(f.panel->renaming(), "F2 : le champ s'ouvre");
    (void)f.panel->dispatch(ui::KeyDown{ui::Key::Escape, {}, false});
    check(!f.panel->renaming() && f.as().current()->title == "Ma cuve de test", "Echap : annule");
    // Le clic droit : le menu (Renommer, Supprimer...).
    f.panel->openRowMenu(0, {100, 200});
    bool rename = false, del = false;
    for (const auto& it : f.panel->menu()->items()) {
        rename |= it.label.rfind("Renommer", 0) == 0 && it.shortcut == "F2";
        del |= it.label.rfind("Supprimer", 0) == 0 && it.shortcut == "Suppr";
    }
    check(rename && del, "le clic droit : Renommer (F2), Supprimer (Suppr)");
    {
        Recorder rec;
        std::vector<ui::Widget*> overlays;
        f.panel->layout();
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, &overlays});
        for (auto* o : overlays) o->paintTopMost(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Renommer\xE2\x80\xA6") && rec.saw("Supprimer\xE2\x80\xA6"), "le menu se dessine (la passe du dessus)");
    }
    f.panel->menu()->close();
    // Supprimer : la confirmation, puis le fichier part.
    const std::size_t n = f.as().conversations().size();
    f.panel->askDelete(0);
    f.mm.applyPending();
    auto* ask = dynamic_cast<app::HmiAskDialog*>(f.mm.top());
    check(ask != nullptr, "Supprimer : la confirmation s'ouvre");
    check(f.as().conversations().size() == n, "rien n'est supprime avant la reponse");
    if (ask) {
        auto* yes = findButton(*ask->widgetRoot(), "Supprimer");
        check(yes != nullptr, "son bouton Supprimer");
        if (yes) yes->clicked->emit();
        f.mm.applyPending();
    }
    check(f.as().conversations().size() == n - 1, "confirmee : la conversation part");
    check(!fs::exists(f.dir / "IA" / "conversations" / (id + ".json")), "son fichier aussi");
    check(readAll(f.dir / "IA" / "activite.jsonl").find("\"type\":\"suppression\"") != std::string::npos, "et l'activite le dit");
}

// ------------------------------------------- envoyer, la generation, Arreter ----
void testGeneration() {
    std::printf("[generation]\n");
    Fixture f("generation");
    f.panel->newConversation();
    check(f.panel->sendText(ai::demoSuggestions()[0]), "envoyer");
    check(f.as().generating() && f.as().phase() == ai::Assistant::Phase::Waiting, "en attente");
    f.step(0.05);
    f.step(0.3);
    check(f.as().phase() == ai::Assistant::Phase::Waiting, "toujours en attente a 0,35 s");
    f.step(0.5);
    check(f.as().phase() == ai::Assistant::Phase::Thinking, "reflexion");
    f.step(1.1);
    check(f.as().phase() == ai::Assistant::Phase::Writing, "ecriture");
    f.step(0.3);
    const std::string partial = f.as().shownText();
    check(!partial.empty() && partial.size() < 300, "le texte s'ecrit mot a mot (" + std::to_string(partial.size()) + " octets)");
    Recorder rec;
    f.render(rec);
    check(rec.saw("Arr\xC3\xAAter"), "le bouton du bas devient Arreter");
    check(rec.saw("\xC3\x89" "criture"), "l'indicateur dit Ecriture...");
    f.panel->stop();
    const auto& m = f.as().current()->messages.back();
    check(!f.as().generating() && m.state == "arrete" && !m.proposal, "Arreter : la reponse est gardee, sans proposition");
    check(m.text.rfind(partial.substr(0, 20), 0) == 0, "elle garde ce qui etait ecrit");
    // Une autre, jusqu'au bout.
    check(f.sendAndWait(ai::demoSuggestions()[0]), "renvoyer");
    const auto& done = f.as().current()->messages.back();
    check(done.state == "fini" && done.proposal && done.proposal->state == "proposee", "finie : une proposition proposee");
    check(done.proposal && done.proposal->create == 31, "31 objets a creer");
    // Pendant une generation, rien d'autre ne part.
    (void)f.panel->sendText("Ajoute une cuve avec un indicateur de niveau");
    std::string why;
    check(!f.as().send("encore", &why) && !why.empty(), "une seconde demande attend la fin (" + why + ")");
    f.finish();
}

// ---------------------------------------------- defiler, la saisie visible ----
void testScrollAndInput() {
    std::printf("[defilement et saisie]\n");
    Fixture f("defilement");
    f.panel->newConversation();
    for (int k = 0; k < 4; ++k) (void)f.sendAndWait(ai::demoSuggestions()[k % 2 == 0 ? 0 : 1]);
    auto& chat = f.panel->chat();
    check(chat.contentHeight() > chat.bounds().h, "la discussion depasse : elle defile");
    const auto prompt = f.panel->promptRect();
    check(prompt.bottom() <= f.panel->bounds().bottom() && prompt.h >= 28.f, "la saisie est en bas du panneau");
    check(chat.bounds().bottom() <= prompt.y, "la discussion s'arrete au-dessus de la saisie");
    check(chat.atBottom(), "apres une reponse : en bas");
    chat.scrollBy(-600.f);
    f.panel->layout();
    check(!chat.atBottom() && chat.scrollOffset() < chat.contentHeight() - chat.bounds().h, "la molette remonte l'historique");
    check(f.panel->promptRect().y == prompt.y && f.panel->promptRect().h == prompt.h, "la saisie ne bouge pas");
    (void)chat.dispatch(ui::MouseWheel{{chat.bounds().x + 50.f, chat.bounds().y + 50.f}, 0.f, -20.f, {}});
    check(chat.atBottom(), "la molette redescend jusqu'en bas");
    // Entree envoie, Maj+Entree passe a la ligne.
    auto& in = f.panel->prompt();
    in.takeFocus();
    in.setText("Ajoute une cuve");
    (void)in.dispatch(ui::KeyDown{ui::Key::Return, {false, true, false, false}, false});
    check(in.text().find('\n') != std::string::npos && !f.as().generating(), "Maj+Entree : une nouvelle ligne, rien n'est envoye");
    in.setText("Ajoute une cuve avec un indicateur de niveau");
    (void)in.dispatch(ui::KeyDown{ui::Key::Return, {}, false});
    check(f.as().generating() && in.text().empty(), "Entree : envoye, la saisie se vide");
    f.finish();
    // 1.14.2 (les captures) : un champ de message - le texte passe a la ligne au mot, le champ
    // grandit de 1 a 5 lignes puis defile ; l'indication du champ vide se lit, rien ne la recouvre.
    {
        in.setText({});
        f.panel->layout();
        const float h1 = f.panel->promptRect().h;
        check(!in.placeholder().empty() && in.placeholder().find("D\xC3\xA9" "cris ta demande") == 0, "vide : l'indication (tutoyee)");
        Recorder rec;
        f.render(rec);
        check(rec.saw("D\xC3\xA9" "cris ta demande") && !rec.saw("modifi\xC3\xA9"), "l'indication est dessinee, sans marque modifie");
        const std::string longText = ai::demoSuggestions()[0];
        for (const char ch : longText) (void)in.dispatch(ui::TextInput{std::string(1, ch)});
        f.panel->layout();
        check(in.text() == longText, "tape au clavier : le texte entier");
        const auto rows = ai::AiPromptEdit::wrap(in.text(), in.bounds().w - 24.f);
        check(rows.size() >= 2 && in.lineCount() >= 2, "la longue demande passe a la ligne (" + std::to_string(in.lineCount()) + " lignes)");
        bool fits = true;
        for (const auto& r : rows)
            if (ui::measureWidth(std::string_view(in.text()).substr(r.begin, r.end - r.begin), gfx::FontId{14}) > in.bounds().w - 16.f + 8.f) fits = false;
        check(fits, "aucune ligne ne depasse le champ (pas de defilement de cote)");
        check(rows.size() > 1 && in.text()[rows[1].begin - 1] == ' ', "la coupure tombe entre deux mots");
        check(f.panel->promptRect().h > h1, "le champ grandit avec le texte");
        for (int k = 0; k < 6; ++k) (void)in.dispatch(ui::KeyDown{ui::Key::Return, {false, true, false, false}, false});
        f.panel->layout();
        const float h5 = f.panel->promptRect().h;
        check(in.lineCount() > ai::AiPromptEdit::kMaxRows && std::abs(h5 - in.heightFor(in.bounds().w)) < 0.5f, "au-dela de 5 lignes : la hauteur s'arrete");
        check(in.scrollOffset() > 0.f, "puis le champ defile (le curseur reste en vue)");
        (void)in.dispatch(ui::KeyDown{ui::Key::A, {true, false, false, false}, false});
        (void)in.dispatch(ui::KeyDown{ui::Key::Backspace, {}, false});
        check(in.text().empty(), "Ctrl+A puis Retour arriere : vide");
        f.panel->layout();
        check(std::abs(f.panel->promptRect().h - h1) < 0.5f, "vide : la hauteur revient");
        in.setText("une deux");
        (void)in.dispatch(ui::KeyDown{ui::Key::Left, {true, false, false, false}, false});
        (void)in.dispatch(ui::TextInput{"x"});
        check(in.text() == "une xdeux", "Ctrl+Gauche : le mot precedent");
        in.setText({});
    }
    // Les suggestions : une conversation vide les montre ; un clic envoie.
    f.panel->newConversation();
    f.panel->layout();
    check(chat.suggestionCount() == ai::demoSuggestions().size(), "une conversation vide montre les suggestions");
    const auto r = chat.suggestionRect(3);
    (void)chat.dispatch(ui::MouseDown{{r.x + 20.f, r.y + 8.f}, ui::MouseButton::Left, 1, {}});
    check(f.as().generating() && f.as().current()->messages.front().text == ai::demoSuggestions()[3], "un clic sur une suggestion l'envoie");
    f.finish();
}

// ------------------------------------------------------------ joindre ----
void testAttachments() {
    std::printf("[pieces jointes]\n");
    Fixture f("pieces");
    const fs::path src = f.dir / "source";
    writeFile(src / "plan.pdf", std::string(4096, 'p'));
    writeFile(src / "notes.txt", "Pompe 1 : marche par impulsion.");
    writeFile(src / "mesures.csv", "t;niveau\n0;12\n");
    writeFile(src / "capture.png", std::string(512, 'i'));
    writeFile(src / "Station.xihm", std::string(256, 'z'));
    writeFile(src / "outil.exe", "MZ");
    f.panel->newConversation();
    f.step();
    check(f.panel->attachFile(U(src / "plan.pdf")), "joindre plan.pdf");
    const auto& pend = f.as().pending();
    check(pend.size() == 1 && pend[0].attachment.type == "pdf" && pend[0].attachment.bytes == 4096, "la liste : nom, type, taille");
    const std::string copy = ai::attachmentPath(pend[0].attachment);
    check(!copy.empty() && fs::exists(fs::path(std::u8string(copy.begin(), copy.end()))), "copie dans IA/pieces-jointes/<id>/");
    check(pend[0].attachment.file == "pieces-jointes/" + pend[0].attachment.id + "/plan.pdf", "le chemin du contrat (" + pend[0].attachment.file + ")");
    check(readAll(fs::path(std::u8string(copy.begin(), copy.end()))) == std::string(4096, 'p'), "a l'identique");
    check(f.as().processing(), "le traitement commence");
    std::string why;
    check(!f.as().send("Voici le plan", &why), "pendant le traitement, Envoyer attend");
    f.step(0.6);
    check(pend[0].progress > 0.2f && pend[0].progress < 1.f, "la progression avance");
    Recorder rec;
    f.render(rec);
    check(rec.saw("traitement"), "le panneau montre le traitement");
    f.step(1.0);
    check(!f.as().processing(), "traite");
    // Un format refuse.
    check(!f.panel->attachFile(U(src / "outil.exe")), "un .exe est refuse");
    check(f.panel->lastStatus().find("format") != std::string::npos, "et le panneau le dit (" + f.panel->lastStatus() + ")");
    // Les autres formats ; retirer.
    for (const char* n : {"notes.txt", "mesures.csv", "capture.png", "Station.xihm"}) check(f.panel->attachFile(U(src / n)), std::string("joindre ") + n);
    const std::string removedId = f.as().pending()[1].attachment.id;
    f.panel->removePending(1);
    check(f.as().pending().size() == 4 && !fs::exists(f.dir / "IA" / "pieces-jointes" / removedId), "retirer : la ligne et sa copie partent");
    // Un fichier lache sur le panneau.
    (void)f.panel->dispatch(ui::FileDropped{U(src / "notes.txt"), {200.f, 400.f}});
    check(f.as().pending().size() == 5, "un fichier lache sur le panneau se joint");
    f.finish();
    check(f.sendAndWait("Voici les documents de la station"), "envoyer avec les pieces");
    const auto& msgs = f.as().current()->messages;
    check(msgs.size() >= 2 && msgs[msgs.size() - 2].attachments.size() == 5, "le message porte ses 5 pieces jointes");
    const std::string json = readAll(f.dir / "IA" / "conversations" / (f.as().current()->id + ".json"));
    check(json.find("\"pieces\"") != std::string::npos && json.find("\"fichier\": \"pieces-jointes/") != std::string::npos,
          "elles sont dans le fichier de la conversation");
    check(msgs.back().text.find("plan.pdf") != std::string::npos, "la reponse de demonstration dit ce qu'elle a recu");
    check(readAll(f.dir / "IA" / "activite.jsonl").find("\"type\":\"fichier\"") != std::string::npos, "l'activite les compte");
}

// ------------------------------------------------------------ parametres ----
void testSettings() {
    std::printf("[parametres]\n");
    Fixture f("parametres");
    f.panel->openSettings();
    f.mm.applyPending();
    auto* d = dynamic_cast<ai::AiSettingsDialog*>(f.mm.top());
    check(d != nullptr, "Parametres : la fenetre s'ouvre");
    if (!d) return;
    auto* model = dynamic_cast<ui::InputText*>(d->field("modele"));
    auto* key = dynamic_cast<ui::InputText*>(d->field("cle"));
    auto* mode = dynamic_cast<ui::DropDown*>(d->field("mode"));
    auto* days = dynamic_cast<ui::DropDown*>(d->field("jours"));
    auto* privacy = dynamic_cast<ui::Checkbox*>(d->field("variables"));
    check(model && key && mode && days && privacy, "ses champs");
    check(model && model->text().empty(), "le modele : une case libre, vide (rien en dur)");
    check(key && key->masked(), "la cle est masquee");
    check(d->field("memoire") && !d->field("memoire")->enabled(), "Memoire et apprentissage... : grise sans la commande ia.memoire");
    if (!model || !key || !mode || !days || !privacy) return;
    model->setText("modele-de-test");
    key->setText("sk-CLE-TRES-SECRETE-42");
    d->showKey(true);
    check(!key->masked(), "Afficher : la cle se lit");
    mode->setSelectedIndex(1);
    days->setSelectedIndex(2);
    privacy->setState(ui::Checkbox::State::Unchecked);
    d->save();
    f.mm.applyPending();
    check(f.mm.top() != d, "Enregistrer ferme la fenetre");
    const std::string json = readAll(f.dir / "IA" / "parametres.json");
    check(json.find("modele-de-test") != std::string::npos && json.find("\"mode\": \"direct\"") != std::string::npos, "parametres.json ecrit");
    check(json.find("SECRETE") == std::string::npos, "la cle n'est PAS dans parametres.json");
    bool anywhere = false;
    for (const auto& e : fs::recursive_directory_iterator(f.dir / "IA"))
        if (e.is_regular_file() && readAll(e.path()).find("SECRETE") != std::string::npos) anywhere = true;
    check(!anywhere, "ni nulle part dans IA/");
    check(f.as().apiKey() == "sk-CLE-TRES-SECRETE-42", "elle est gardee en memoire pour la seance");
    check(f.as().directMode(), "le mode par defaut : Direct");
    const auto relu = ai::loadSettings();
    check(relu.model == "modele-de-test" && relu.mode == "direct" && relu.keepDays == 30 && !relu.sendVariables, "relus tels quels");
    // Une cle ecrite a la main dans le fichier n'est ni relue ni reecrite.
    writeFile(f.dir / "IA" / "parametres.json", "{\"format\": 1, \"modele\": \"m\", \"cle\": \"sk-A-LA-MAIN\", \"futur\": 3}");
    const auto hand = ai::loadSettings();
    (void)ai::saveSettings(hand);
    const std::string again = readAll(f.dir / "IA" / "parametres.json");
    check(again.find("sk-A-LA-MAIN") == std::string::npos && again.find("\"futur\": 3") != std::string::npos,
          "une cle ecrite a la main ne revient pas ; un champ inconnu reste");
    // Effacer.
    f.panel->openSettings();
    f.mm.applyPending();
    if (auto* d2 = dynamic_cast<ai::AiSettingsDialog*>(f.mm.top())) {
        check(d2->keyText() == "sk-CLE-TRES-SECRETE-42", "rouverte : la cle de la seance");
        d2->clearKey();
        check(d2->keyText().empty() && f.as().apiKey().empty(), "Effacer : partie");
        d2->cancel();
        f.mm.applyPending();
    }
    Fixture g("parametres_memoire");
    g.memory = true;
    g.panel->openSettings();
    g.mm.applyPending();
    if (auto* d3 = dynamic_cast<ai::AiSettingsDialog*>(g.mm.top())) check(d3->field("memoire")->enabled(), "avec ia.memoire : le bouton est actif");
}

// ------------------------------------------------------------ la proposition ----
void testProposal() {
    std::printf("[proposition]\n");
    Fixture f("proposition");
    f.panel->newConversation();
    check(f.sendAndWait(ai::demoSuggestions()[0]), "la station de pompage");
    const int m = f.lastProposal();
    check(m >= 0, "une proposition");
    const auto ck = f.as().check(m);
    check(ck.ok() && ck.create == 31 && ck.modify == 0 && ck.remove == 0, "les comptes : 31 / 0 / 0");
    std::map<hmi::Kind, int> kinds;
    for (const auto& it : ck.items) ++kinds[it.kind];
    check(kinds[hmi::Kind::Tank] == 3 && kinds[hmi::Kind::Bargraph] == 3 && kinds[hmi::Kind::Valve] == 3 && kinds[hmi::Kind::Pipe] == 3
              && kinds[hmi::Kind::Pump] == 6 && kinds[hmi::Kind::PushButton] == 12 && kinds[hmi::Kind::Text] == 1,
          "de vrais genres : 3 cuves, 3 bargraphes, 3 vannes, 3 tuyauteries, 6 pompes, 12 boutons, un titre");
    // Chaque cle donnee existe dans l'objet neuf de son genre (hmi::makeObject).
    bool realKeys = true;
    const auto* p = f.as().proposal(m);
    for (const auto& op : p->ops) {
        const auto kind = hmi::kindFromKey(op.kind);
        if (!kind) { realKeys = false; continue; }
        const hmi::Object fresh = hmi::makeObject(*kind, 1, "x", 0, 0, 0);
        for (const auto& [k, val] : op.props)
            if (!fresh.find(k)) realKeys = false;
    }
    check(realKeys, "chaque propriete est une vraie cle de son genre");
    check(std::any_of(ck.warnings.begin(), ck.warnings.end(), [](const std::string& w) { return w.find("aucune variable") != std::string::npos; }),
          "l'avertissement : aucun objet relie a une variable (aucune inventee)");
    check(f.v().objects.empty(), "rien n'a change dans le projet");
    // Une propriete inventee, un genre inconnu : refuses.
    ai::Proposal bad;
    bad.view = "Vue_Station";
    bad.viewId = f.view;
    ai::Operation op;
    op.action = ai::Operation::Action::Create;
    op.kind = "Tank";
    op.props = {{"couleurMagique", "#FF0000"}};
    bad.ops.push_back(op);
    auto bc = ai::validate(f.doc->project, bad);
    check(!bc.ok() && bc.errors.front().find("couleurMagique") != std::string::npos, "une propriete inventee : refusee");
    bad.ops.front().kind = "Licorne";
    bc = ai::validate(f.doc->project, bad);
    check(!bc.ok() && bc.errors.front().find("Licorne") != std::string::npos, "un genre inconnu : refuse");
    bad.ops.front().kind = "Tank";
    bad.ops.front().props = {{"fillColor", "rouge"}};
    bc = ai::validate(f.doc->project, bad);
    check(!bc.ok(), "une couleur illisible : refusee");
    // Le format des operations : JSON, et une operation inconnue refusee a la lecture.
    std::vector<ai::Operation> ops;
    std::string why;
    check(ai::operationsFromJson(ai::operationsToJson(p->ops), ops, &why) && ops.size() == p->ops.size(), "les operations : JSON aller-retour");
    ai::json::Value evil;
    (void)ai::json::parse("[{\"action\": \"executer\", \"code\": \"system('rm')\"}]", evil);
    check(!ai::operationsFromJson(evil, ops, &why) && why.find("executer") != std::string::npos, "une operation inconnue (du code) : refusee");
}

// --------------------------------------- Appliquer, Ctrl+Z, Refuser, Direct ----
void testApplyRefuseDirect() {
    std::printf("[appliquer, refuser, direct]\n");
    Fixture f("appliquer");
    f.panel->newConversation();
    (void)f.sendAndWait(ai::demoSuggestions()[0]);
    const int m = f.lastProposal();
    auto* applyBtn = f.panel->chat().cardButton(m, 1);
    check(applyBtn && applyBtn->text() == "Appliquer" && applyBtn->enabled(), "la carte : Appliquer");
    const std::size_t steps = f.stack.done().size();
    if (applyBtn) applyBtn->clicked->emit();
    f.step();
    check(f.v().objects.size() == 31, "applique : 31 objets dans la vue");
    check(f.stack.done().size() == steps + 1, "UNE commande sur la pile");
    check(f.stack.undoLabel().find("Assistant IA") != std::string::npos, "son libelle : Assistant IA : ...");
    check(f.as().proposal(m)->state == "appliquee", "la carte passe a appliquee");
    check(f.count(hmi::Kind::Pump) == 6 && f.object("Cuve_2") && f.object("Cuve_2")->text("label") == "Cuve 2", "les objets et leurs proprietes");
    using A = ai::Assistant::Applied;
    check(f.as().appliedState(m) == A::Applied && !f.as().proposal(m)->fingerprintAfter.empty(), "l'empreinte d'apres est gardee : appliquee");
    (void)f.stack.undo();
    check(f.v().objects.empty(), "un Ctrl+Z defait tout");
    // 1.14.2 (les captures) : la carte ne dit plus "Appliquee" apres un Ctrl+Z.
    f.step(0.3);
    check(f.as().appliedState(m) == A::Undone, "apres Ctrl+Z : annulee");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("Annul\xC3\xA9" "e") && !rec.saw("Appliqu\xC3\xA9" "e au projet"), "la carte dit Annulee (Ctrl+Z)");
        auto* again = f.panel->chat().cardButton(m, 1);
        check(again && again->text() == "R\xC3\xA9" "appliquer" && again->visible(), "et propose Reappliquer");
    }
    (void)f.stack.redo();
    check(f.v().objects.size() == 31, "Ctrl+Y le refait");
    f.step(0.3);
    check(f.as().appliedState(m) == A::Applied, "apres Ctrl+Y : appliquee de nouveau");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("Appliqu\xC3\xA9" "e au projet") && !rec.saw("Annul\xC3\xA9" "e"), "la carte le dit");
    }
    // Reappliquer apres Ctrl+Z : une NOUVELLE commande (la vue est celle de la proposition).
    (void)f.stack.undo();
    f.step(0.3);
    if (auto* again = f.panel->chat().cardButton(m, 1)) again->clicked->emit();
    f.step(0.3);
    check(f.v().objects.size() == 31 && f.as().appliedState(m) == A::Applied, "Reappliquer : les 31 objets reviennent, appliquee");
    (void)f.stack.undo();
    check(f.v().objects.empty(), "et un Ctrl+Z la reprend");
    (void)f.stack.redo();
    {
        // Le projet rouvert sans avoir ete enregistre (la vue d'avant) : la relecture dit annulee.
        const std::string id = f.as().current()->id;
        Fixture g("appliquer", false);
        const int i = g.as().indexOf(id);
        check(i >= 0 && g.panel->selectConversation(i), "la conversation relue (le projet rouvert, vide)");
        int gm = -1;
        if (const auto* gc = g.as().current())
            for (std::size_t k = 0; k < gc->messages.size(); ++k)
                if (gc->messages[k].proposal && gc->messages[k].proposal->id == f.as().proposal(m)->id) gm = static_cast<int>(k);
        check(gm >= 0 && g.as().appliedState(gm) == A::Undone, "la vue est celle d'avant : annulee (pas Appliquee)");
    }
    {
        // Un autre geste ensuite : appliquee, la vue a change depuis.
        (void)f.stack.push(hmi::changeView(f.doc, f.view, "Ajouter un texte", [](hmi::Project& p, hmi::View& v) {
            (void)hmi::edit::add(p, v, hmi::Kind::Text, 10, 10);
        }));
        f.step(0.3);
        check(f.as().appliedState(m) == A::Changed, "un autre geste ensuite : la vue a change depuis");
        Recorder rec;
        f.render(rec);
        check(rec.saw("la vue a chang\xC3\xA9 depuis"), "la carte le dit");
        (void)f.stack.undo();
        f.step(0.3);
    }
    // Refuser.
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    const int r = f.lastProposal();
    {
        // 1.14.2 (les captures, 1280 x 800) : le volet etroit - les boutons ne debordent jamais de la carte.
        f.panel->setBounds({0, 0, ai::defaultPanelWidth(1280.f), 760});
        f.panel->layout();
        f.panel->chat().scrollToBottom();
        f.panel->layout();
        const auto card = f.panel->chat().cardRect(r);
        bool inside = card.w > 0.f;
        int shown = 0;
        for (int k = 0; k < 3; ++k)
            if (auto* b = f.panel->chat().cardButton(r, k); b && b->visible()) {
                ++shown;
                inside = inside && b->bounds().x >= card.x && b->bounds().right() <= card.right() - 4.f && b->bounds().bottom() <= card.bottom();
            }
        check(inside && shown == 3, "a 282 px : Previsualiser, Appliquer, Refuser dans la carte");
        check(std::abs(ai::defaultPanelWidth(1280.f) - 281.6f) < 0.5f && ai::defaultPanelWidth(1920.f) > 400.f && ai::defaultPanelWidth(4000.f) == 460.f
                  && ai::defaultPanelWidth(800.f) == 280.f,
              "la largeur par defaut : 22 % de la fenetre, entre 280 et 460");
        f.panel->setBounds({0, 0, 440, 900});
        f.panel->layout();
    }
    auto* refuseBtn = f.panel->chat().cardButton(r, 2);
    check(refuseBtn && refuseBtn->text() == "Refuser", "la carte : Refuser");
    if (refuseBtn) refuseBtn->clicked->emit();
    f.step();
    check(f.as().proposal(r)->state == "refusee" && f.v().objects.size() == 31, "refusee : le projet n'a pas change");
    std::string why;
    check(!f.as().apply(r, &why), "une proposition refusee ne s'applique plus");
    // La selection : "celle de droite" en rouge.
    f.selection = {f.object("Cuve_1")->id, f.object("Cuve_3")->id, f.object("Cuve_2")->id};
    (void)f.sendAndWait("Mets celle de droite en rouge");
    const int red = f.lastProposal();
    const auto ck = f.as().check(red);
    check(ck.ok() && ck.modify == 1 && ck.items.size() == 1 && ck.items[0].name == "Cuve_3", "celle de droite : Cuve_3");
    check(!ck.items.empty() && !ck.items[0].changes.empty() && ck.items[0].changes[0].key == "fillColor" && ck.items[0].changes[0].after == "#E5534B",
          "fillColor : avant -> #E5534B");
    check(f.panel->applyProposal(red) && f.object("Cuve_3")->text("fillColor") == "#E5534B", "appliquee");
    // Sans selection : l'Assistant la demande, et ne propose rien.
    f.selection.clear();
    (void)f.sendAndWait("Mets celle de droite en rouge");
    check(!f.as().current()->messages.back().proposal && f.as().current()->messages.back().text.find("s\xC3\xA9lection") != std::string::npos,
          "sans selection : une question, pas de proposition");
    // Aligner les quatre boutons (la selection).
    f.selection = {f.object("Marche_P11")->id, f.object("Arret_P11")->id, f.object("Marche_P12")->id, f.object("Arret_P12")->id};
    (void)f.sendAndWait("Aligne les quatre boutons");
    const int al = f.lastProposal();
    check(al >= 0 && f.as().check(al).ok() && f.as().check(al).modify >= 3, "aligner les quatre boutons : une proposition");
    check(f.panel->applyProposal(al), "appliquee");
    // Direct : la cuve s'applique des la reponse.
    f.panel->setDirect(true);
    const std::size_t n = f.v().objects.size();
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    check(f.v().objects.size() == n + 2 && f.as().proposal(f.lastProposal())->state == "appliquee", "Direct : appliquee sans l'apercu");
    (void)f.stack.undo();
    check(f.v().objects.size() == n, "et un Ctrl+Z la defait");
    f.panel->setDirect(false);
}

// ------------------------------------ la suppression multiple se confirme ----
void testMultipleRemoval() {
    std::printf("[suppression multiple]\n");
    Fixture f("suppression");
    f.panel->newConversation();
    (void)f.sendAndWait(ai::demoSuggestions()[0]);
    (void)f.panel->applyProposal(f.lastProposal());
    check(f.count(hmi::Kind::Valve) == 3, "trois vannes");
    f.panel->setDirect(true);
    (void)f.sendAndWait("Supprime les vannes");
    const int m = f.lastProposal();
    check(m >= 0 && f.as().check(m).remove == 3 && f.as().check(m).confirmRemoval, "3 a supprimer : a confirmer");
    auto* ask = dynamic_cast<app::HmiAskDialog*>(f.mm.top());
    check(ask != nullptr, "meme en Direct : la confirmation s'ouvre");
    check(f.count(hmi::Kind::Valve) == 3, "rien n'est supprime avant la reponse");
    if (ask) {
        auto* yes = findButton(*ask->widgetRoot(), "Supprimer les 3 objets");
        check(yes != nullptr, "le bouton dit combien");
        if (yes) yes->clicked->emit();
        f.mm.applyPending();
    }
    check(f.count(hmi::Kind::Valve) == 0 && f.as().proposal(m)->state == "appliquee", "confirmee : les vannes partent");
    (void)f.stack.undo();
    check(f.count(hmi::Kind::Valve) == 3, "un Ctrl+Z les rend toutes");
}

// ----------------------------------------- la variable n'est jamais inventee ----
void testVariable() {
    std::printf("[variable]\n");
    Fixture f("variable");
    f.panel->newConversation();
    (void)f.sendAndWait(ai::demoSuggestions()[0]);
    (void)f.panel->applyProposal(f.lastProposal());
    f.selection = {f.object("Pompe_11")->id};
    (void)f.sendAndWait("Associe cette pompe \xC3\xA0 la variable PUMP_01_RUN");
    const int m = f.lastProposal();
    auto ck = f.as().check(m);
    check(!ck.ok() && ck.missingVariable == "PUMP_01_RUN", "PUMP_01_RUN n'existe pas : une erreur, pas d'invention");
    check(f.as().current()->messages.back().text.find("n'existe pas") != std::string::npos, "la reponse le dit");
    check(f.as().current()->messages.back().text.find("approchante") == std::string::npos
              && f.as().current()->messages.back().text.find("Pompe_1_Marche") != std::string::npos,
          "1.14.2 : la reponse cite les variables du projet que l'apercu propose");
    for (const auto& n : ck.nearVariables) check(f.doc->project.variable(n) != nullptr, "propose une variable qui existe : " + n);
    std::string why;
    check(!f.as().apply(m, &why) && f.object("Pompe_11")->text("variable").empty(), "Appliquer est refuse (" + why + ")");
    check(f.panel->chat().cardButton(m, 1) && !f.panel->chat().cardButton(m, 1)->enabled(), "le bouton Appliquer de la carte est grise");
    // L'apercu : choisir une variable existante.
    f.panel->openPreview(m);
    f.mm.applyPending();
    auto* pv = dynamic_cast<ai::AiPreviewDialog*>(f.mm.top());
    check(pv != nullptr, "l'apercu s'ouvre");
    if (!pv) return;
    const auto items = pv->variableItems();
    check(!items.empty() && items.back().find("PUMP_01_RUN") != std::string::npos && items.back().rfind("Cr\xC3\xA9" "er", 0) == 0,
          "la liste : les variables du projet, puis Creer PUMP_01_RUN");
    check(std::any_of(items.begin(), items.end(), [](const std::string& s) { return s.rfind("Pompe_1_Marche", 0) == 0; }), "Pompe_1_Marche y est");
    check(pv->applyButton() && !pv->applyButton()->enabled(), "Appliquer grise tant que rien n'est choisi");
    int idx = -1;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].rfind("Pompe_1_Marche", 0) == 0) idx = static_cast<int>(i);
    check(pv->chooseVariable(idx) && pv->check().ok() && pv->applyButton()->enabled(), "Pompe_1_Marche choisie : Appliquer possible");
    {
        // 1.14.2 (les captures) : le libelle garde le nom introuvable (plus de guillemets vides).
        Recorder rec;
        auto* root = pv->widgetRoot();
        if (root) {
            root->setBounds({0, 0, 1600, 900});
            root->layout();
            root->render(ui::PaintContext{rec, ui::Theme::dark(), root->bounds(), f.t, nullptr});
        }
        check(rec.saw("\xC2\xAB PUMP_01_RUN \xC2\xBB n'existe pas") && !rec.saw("\xC2\xAB  \xC2\xBB"), "le libelle : PUMP_01_RUN, remplacee par Pompe_1_Marche");
    }
    check(pv->apply(), "Appliquer");
    f.mm.applyPending();
    check(f.object("Pompe_11")->text("variable") == "Pompe_1_Marche", "la pompe lit Pompe_1_Marche");
    check(f.doc->project.variable("PUMP_01_RUN") == nullptr, "aucune variable inventee");
    // Une autre fois : la creer, a la demande de l'utilisateur (un seul Ctrl+Z pour les deux).
    f.selection = {f.object("Pompe_12")->id};
    (void)f.sendAndWait("Associe cette pompe \xC3\xA0 la variable PUMP_01_RUN");
    const int m2 = f.lastProposal();
    ai::VariableChoice create;
    create.create = true;
    check(f.as().chooseVariable(m2, create) && f.as().check(m2).ok(), "Creer PUMP_01_RUN : choisi");
    const std::size_t steps = f.stack.done().size();
    check(f.panel->applyProposal(m2), "appliquee");
    const auto* var = f.doc->project.variable("PUMP_01_RUN");
    check(var && var->type == "BOOL" && f.object("Pompe_12")->text("variable") == "PUMP_01_RUN", "la variable est creee, la pompe la lit");
    check(f.stack.done().size() == steps + 1, "une seule commande");
    (void)f.stack.undo();
    check(f.doc->project.variable("PUMP_01_RUN") == nullptr && f.object("Pompe_12")->text("variable").empty(), "un Ctrl+Z defait les deux");
}

// ------------------------------------------------------ la persistance ----
void testPersistence() {
    std::printf("[persistance]\n");
    std::string convId;
    {
        Fixture f("persistance");
        f.panel->newConversation();
        (void)f.sendAndWait(ai::demoSuggestions()[0]);
        (void)f.panel->applyProposal(f.lastProposal());
        (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
        (void)f.panel->refuseProposal(f.lastProposal());
        (void)f.sendAndWait("Bonjour, peux-tu me faire un caf\xC3\xA9 ?");
        convId = f.as().current()->id;
    }
    // L'application rouverte : la meme racine, une nouvelle seance.
    Fixture g("persistance", false);
    const int i = g.as().indexOf(convId);
    check(i >= 0, "la conversation est retrouvee");
    if (i < 0) return;
    check(g.panel->selectConversation(i), "et se rouvre");
    const auto& c = *g.as().current();
    check(c.messages.size() == 6, "ses 6 messages (" + std::to_string(c.messages.size()) + ")");
    check(c.messages[1].proposal && c.messages[1].proposal->state == "appliquee", "la premiere proposition : appliquee");
    check(c.messages[3].proposal && c.messages[3].proposal->state == "refusee", "la deuxieme : refusee");
    check(c.messages[5].text.find("d\xC3\xA9monstration") != std::string::npos, "la reponse honnete a une demande inconnue");
    check(c.project.name == "Station_Pompage" && c.project.view == "Vue_Station", "le projet et la vue de la conversation");
    // Un champ inconnu, a chaque niveau, reste a la reecriture.
    const fs::path file = g.dir / "IA" / "conversations" / (convId + ".json");
    ai::json::Value v;
    (void)ai::json::parse(readAll(file), v);
    v.set("futurConversation", "garde");
    v["messages"].items()[0].set("futurMessage", 42);
    v["messages"].items()[1]["proposition"].set("futurProposition", true);
    writeFile(file, ai::json::write(v));
    Fixture h("persistance", false);
    (void)h.as().rename(convId, "Station (relue)");
    const std::string again = readAll(file);
    check(again.find("\"futurConversation\": \"garde\"") != std::string::npos && again.find("\"futurMessage\": 42") != std::string::npos
              && again.find("\"futurProposition\": true") != std::string::npos,
          "les champs inconnus sont gardes (conversation, message, proposition)");
    check(again.find("Station (relue)") != std::string::npos, "et le nouveau titre est la");
    // Un fichier abime : dit, laisse de cote, pas efface.
    writeFile(g.dir / "IA" / "conversations" / "abime.json", "{\"id\": \"abime\", ");
    std::vector<std::string> problems;
    const auto all = ai::loadConversations(&problems);
    check(!problems.empty() && fs::exists(g.dir / "IA" / "conversations" / "abime.json"), "un fichier abime : dit, et laisse tel quel");
}

// --------------------------------------------- une proposition perimee ----
void testStale() {
    std::printf("[proposition perimee]\n");
    Fixture f("perimee");
    f.panel->newConversation();
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    const int m = f.lastProposal();
    check(f.as().check(m).ok(), "proposee sur la vue telle qu'elle est");
    // La vue change ensuite (un autre geste de l'utilisateur).
    (void)f.stack.push(hmi::changeView(f.doc, f.view, "Ajouter un texte", [](hmi::Project& p, hmi::View& v) {
        (void)hmi::edit::add(p, v, hmi::Kind::Text, 10, 10);
    }));
    f.step();
    const auto ck = f.as().check(m);
    check(ck.stale && !ck.ok(), "la vue a change : perimee");
    std::string why;
    const std::size_t n = f.v().objects.size();
    check(!f.as().apply(m, &why) && f.v().objects.size() == n, "elle ne s'applique pas (" + why + ")");
    f.panel->chat().rebuild();
    auto* third = f.panel->chat().cardButton(m, 2);
    check(third && third->text() == "Redemander", "la carte propose de la redemander");
    if (third) third->clicked->emit();
    f.step();
    check(f.panel->prompt().text() == "Ajoute une cuve avec un indicateur de niveau", "Redemander : la demande revient dans la saisie");
    // La conversation de demonstration sur une vue qui n'existe pas.
    int demo = -1;
    for (std::size_t i = 0; i < f.as().conversations().size(); ++i)
        if (f.as().conversations()[i].title.rfind("Station de pompage (ancienne", 0) == 0) demo = static_cast<int>(i);
    check(demo >= 0 && f.panel->selectConversation(demo), "la conversation de demonstration (ancienne vue)");
    const auto dc = f.as().check(1);
    check(dc.viewMissing && !dc.ok(), "sa vue n'existe pas : rien ne s'applique");
    check(!f.as().apply(1, &why), "refusee (" + why + ")");
    {
        // 1.14.2 (les captures) : son texte annonce 31 objets, sa carte aussi (pas "Creer 0") ;
        // la note se lit en entier (a la ligne), sans "...".
        f.panel->layout();
        Recorder rec;
        f.render(rec);
        check(rec.saw("Cr\xC3\xA9" "er 31") && !rec.saw("Cr\xC3\xA9" "er 0"), "la carte perimee : Creer 31, comme son texte");
        check(!rec.saw("s'ap\xE2\x80\xA6"), "la note n'est pas coupee par ...");
        check(rec.saw("appliquer."), "la note se lit jusqu'au bout");
    }
}

// ------------------------------------------ l'apercu : les vrais painters ----
void testPreviewPainters() {
    std::printf("[apercu]\n");
    Fixture f("apercu");
    f.panel->newConversation();
    (void)f.sendAndWait(ai::demoSuggestions()[0]);
    const int m = f.lastProposal();
    const auto pv = f.as().previewOf(m);
    check(pv.ok && pv.touched.created.size() == 31 && pv.view.objects.size() == 31, "la copie de la vue porte les 31 objets");
    check(f.v().objects.empty(), "la vraie vue, aucun");
    // La carte du panneau : son apercu dessine par paintHmiView.
    Recorder rec;
    f.render(rec);
    check(rec.saw("Op\xC3\xA9rations propos\xC3\xA9" "es") && rec.saw("Cr\xC3\xA9" "er 31"), "la carte : Operations proposees, Creer 31");
    check(rec.saw("Cuve 1") && rec.saw("P11") && rec.saw("Marche"), "la miniature : les libelles dessines par les painters (Cuve 1, P11, Marche)");
    check(rec.saw("Assistant IA") && rec.saw("D\xC3\xA9monstration \xE2\x80\x93 aucune IA connect\xC3\xA9" "e") && rec.saw("CONVERSATIONS"),
          "l'en-tete : le titre, l'etat de connexion, les conversations");
    // La fenetre de previsualisation.
    auto* btn = f.panel->chat().cardButton(m, 0);
    check(btn && btn->text() == "Pr\xC3\xA9visualiser\xE2\x80\xA6", "la carte : Previsualiser...");
    if (btn) btn->clicked->emit();
    f.mm.applyPending();
    auto* d = dynamic_cast<ai::AiPreviewDialog*>(f.mm.top());
    check(d != nullptr, "la fenetre s'ouvre");
    if (!d) return;
    check(d->check().create == 31 && d->preview().ok, "ses comptes et son apercu");
    d->widgetRoot()->setBounds({0, 0, 1600, 900});
    d->widgetRoot()->layout();
    Recorder big;
    d->widgetRoot()->render(ui::PaintContext{big, ui::Theme::light(), {0, 0, 1600, 900}, 0.0, nullptr});
    check(big.saw("Pr\xC3\xA9visualisation des modifications") && big.saw("OBJETS CONCERN") && big.saw("Cuve_1"), "la liste des objets");
    check(big.saw("Cuve 1") && big.saw("P32") && big.shapes > 200, "l'apercu : la vue dessinee par les painters (" + std::to_string(big.shapes) + " formes)");
    check(big.saw("nouveau") && big.saw("modifi\xC3\xA9") && big.saw("supprim\xC3\xA9"), "la legende");
    // Modifier la demande : le texte revient, la proposition est mise de cote.
    d->editRequest();
    f.mm.applyPending();
    check(f.mm.top() != d && f.panel->prompt().text() == ai::demoSuggestions()[0], "Modifier la demande : la saisie la reprend");
    check(f.as().proposal(m)->state == "refusee" && f.v().objects.empty(), "et rien ne s'applique");
    // Annuler (dans l'apercu) : refusee.
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    f.panel->openPreview(f.lastProposal());
    f.mm.applyPending();
    if (auto* d2 = dynamic_cast<ai::AiPreviewDialog*>(f.mm.top())) {
        d2->cancel();
        f.mm.applyPending();
    }
    check(f.as().proposal(f.lastProposal())->state == "refusee", "Annuler : refusee");
    // Appliquer depuis l'apercu.
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    f.panel->openPreview(f.lastProposal());
    f.mm.applyPending();
    if (auto* d3 = dynamic_cast<ai::AiPreviewDialog*>(f.mm.top())) {
        check(d3->apply(), "Appliquer (dans l'apercu)");
        f.mm.applyPending();
    }
    check(f.v().objects.size() == 2 && f.count(hmi::Kind::Bargraph) == 1, "la cuve et son bargraphe sont dans la vue");
}

// ------------------------------------------ la mise en forme du texte ----
void testMarkdown() {
    std::printf("[mise en forme]\n");
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
    s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
    ui::installPlatformServices(std::move(s));
    const auto lines = ai::AiChatView::layoutText("## Titre court\nUn **mot gras** et du `code`.\n- une puce\n1. un", 400.f);
    check(lines.size() == 4, "quatre lignes (" + std::to_string(lines.size()) + ")");
    if (lines.size() < 4) return;
    check(!lines[0].runs.empty() && lines[0].runs[0].style == 3, "## : un titre");
    bool bold = false, code = false;
    for (const auto& r : lines[1].runs) {
        bold |= r.style == 1 && r.text == "gras";
        code |= r.style == 2 && r.text == "code";
    }
    check(bold && code, "**gras** et `code`");
    check(lines[2].bullet == "\xE2\x80\xA2" && lines[3].bullet == "1.", "les puces et la liste numerotee");
    const auto wrapped = ai::AiChatView::layoutText(std::string(40, 'a') + " " + std::string(40, 'b'), 300.f);
    check(wrapped.size() == 2, "un texte trop long passe a la ligne");
}

} // namespace

// ------------------------- 1.14.2 : la seconde passe des captures reelles ----
void testSecondPass() {
    std::printf("[captures, seconde passe]\n");
    Fixture f("seconde");
    auto& chat = f.panel->chat();
    // 2. La premiere ouverture : Decouvrir l'Assistant montre son DEBUT (pas sa fin) quand elle ne tient pas.
    f.panel->setBounds({0, 0, 440, 520});
    f.panel->layout();
    check(f.panel->selectConversation(f.as().currentIndex()), "Decouvrir l'Assistant rouverte");
    f.panel->layout();
    check(chat.contentHeight() > chat.bounds().h && chat.scrollOffset() == 0.f, "elle ne tient pas : son debut (la question en haut)");
    // 3e passe : elle tient a la premiere largeur (grande), puis le volet retrecit (1280 x 800) :
    // la discussion ne reste pas collee en bas, la question reste en haut.
    f.panel->setBounds({0, 0, 900, 1400});
    f.panel->layout();
    check(f.panel->selectConversation(f.as().currentIndex()), "Decouvrir l'Assistant rouverte, grande");
    f.panel->layout();
    check(chat.contentHeight() <= chat.bounds().h + 1.f, "a la premiere largeur, elle tient");
    f.panel->setBounds({0, 0, 282, 520});
    f.panel->layout();
    f.panel->layout();
    check(chat.contentHeight() > chat.bounds().h && chat.scrollOffset() == 0.f && !chat.atBottom(),
          "le volet retreci : toujours son debut, pas collee en bas");
    f.panel->setBounds({0, 0, 440, 520});
    f.panel->layout();
    // Plusieurs echanges : la derniere demande en haut, pas la fin de la derniere reponse.
    f.panel->newConversation();
    (void)f.sendAndWait("Bonjour, peux-tu me faire un caf\xC3\xA9 ?");
    (void)f.sendAndWait(ai::demoSuggestions()[0]);
    const std::string id = f.as().current()->id;
    (void)f.panel->selectConversation(f.as().currentIndex() == 0 ? 1 : 0);
    check(f.panel->selectConversation(f.as().indexOf(id)) && f.as().current()->id == id, "la conversation rouverte");
    f.panel->layout();
    check(chat.scrollOffset() > 0.f && !chat.atBottom(), "rouverte : le debut du dernier echange, pas la fin");
    // Une reponse qui s'ecrit : la discussion suit le bas.
    (void)f.panel->sendText("Ajoute une cuve avec un indicateur de niveau");
    f.step(0.05);
    f.step(2.2);
    check(chat.atBottom(), "pendant l'ecriture : le bas");
    f.finish();
    // 1. Une carte Annulee au-dessus d'une reponse qui s'ecrit : son bouton reste dans la carte,
    //    meme quand la discussion defile entre deux mises en page (l'ordre d'une image : la mise en
    //    page, puis tick, puis le dessin).
    const int m = f.lastProposal();
    (void)f.panel->applyProposal(m);
    (void)f.stack.undo();
    f.step(0.3);
    auto* again = chat.cardButton(m, 1);
    check(again && again->text() == "R\xC3\xA9" "appliquer", "la carte Annulee et son bouton");
    (void)f.panel->sendText("Aligne les quatre boutons");
    bool inside = true;
    int seen = 0;
    for (int k = 0; k < 120 && f.as().generating(); ++k) {
        f.t += 0.05;
        f.panel->tick(f.t);   // sans mise en page apres : comme dans l'appli
        const auto card = chat.cardRect(m);
        if (again && again->visible() && card.h > 0.f) {
            ++seen;
            inside = inside && again->bounds().y >= card.y && again->bounds().bottom() <= card.bottom();
        }
        f.panel->layout();
    }
    f.finish();
    check(seen > 0 && inside, "pendant l'ecriture : Reappliquer reste dans sa carte (" + std::to_string(seen) + " images)");
    // 3. A 282 px : un texte coupe se lit en entier dans son infobulle.
    (void)f.as().rename(f.as().current()->id, "Station de pompage avec trois cuves et leurs pompes");
    f.panel->setBounds({0, 0, ai::defaultPanelWidth(1280.f), 760});
    f.panel->layout();
    {
        Recorder rec;
        f.render(rec);
        const auto row = f.panel->rowRect(f.as().currentIndex());
        check(f.panel->liveTooltip({row.x + 60.f, row.y + row.h / 2.f}) == "Station de pompage avec trois cuves et leurs pompes",
              "le titre coupe d'une conversation : en entier dans l'infobulle");
        check(f.panel->liveTooltip({row.x + 60.f, row.y - 400.f}).empty(), "ailleurs : pas d'infobulle");
        // (les mesures grossieres de l'essai coupent la fin : le debut suffit a dire le mot)
        check(!rec.saw("nouvelle ligne") && rec.saw("Maj+Entr\xC3\xA9" "e : \xC3\xA0"), "4. l'aide : a la ligne (a 1280)");
    }
    (void)f.sendAndWait("Ajoute une cuve avec un indicateur de niveau");
    chat.scrollToBottom();
    f.panel->layout();
    {
        Recorder rec;
        f.render(rec);
        const auto card = chat.cardRect(f.lastProposal());
        check(card.h > 0.f && chat.liveTooltip({card.x + 60.f, card.y + 14.f}) == "Op\xC3\xA9rations propos\xC3\xA9" "es",
              "le titre coupe de la carte : en entier dans l'infobulle");
    }
    f.panel->setBounds({0, 0, 1000, 900});
    f.panel->layout();
    {
        Recorder rec;
        f.render(rec);
        check(!rec.saw("nouvelle ligne") && rec.saw("Maj+Entr\xC3\xA9" "e : \xC3\xA0 la ligne"), "4. l'aide : a la ligne (large aussi)");
    }
}

int main() {
    ai::setClock([] { return fixedNow(); });
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        ui::installPlatformServices(std::move(s));
    }
    testJson();
    testFirstOpening();
    testConversations();
    testGeneration();
    testScrollAndInput();
    testAttachments();
    testSettings();
    testProposal();
    testApplyRefuseDirect();
    testMultipleRemoval();
    testVariable();
    testPersistence();
    testStale();
    testPreviewPainters();
    testMarkdown();
    testSecondPass();
    std::printf("ai_assistant_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
