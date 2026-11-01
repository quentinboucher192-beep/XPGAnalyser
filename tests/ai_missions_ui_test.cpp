// =============================================================================
//  tests/ai_missions_ui_test.cpp - 1.22.0 : L'INTERFACE DES MISSIONS IA PERSISTANTES
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un dossier IA/ jetable ou des missions sont
//  ecrites (mission::save) avant que le panneau ne les relise :
//    0. les regles de l'ecran (missionui) : le geste du moment, l'ecran de reprise,
//       les nombres d'une proposition, la progression d'une etape ;
//    1. l'onglet Missions IA : les trois onglets, la liste (l'etat, la progression,
//       la prochaine action du modele), les filtres, la recherche (les archivees
//       aussi), les boutons, le clic droit, le clavier, renommer, archiver ;
//    2. la bande << Mission IA >> de la conversation ouverte (au-dessus de la
//       discussion), son geste, le tableau de bord en un clic ;
//    3. << Creer une mission IA ? >> : une demande complexe (la carte, rien ne
//       part), une demande simple (envoyee), Continuer en conversation, Creer la
//       mission ;
//    4. le tableau de bord : les etats du modele, la progression = progressOf, la
//       regle en infobulle, les propositions (Previsualiser, Verifier), une
//       proposition ancienne appliquee (sa conversation supprimee) consultable,
//       Pause / Reprendre / Annuler, une mission introuvable ;
//    5. l'ecran de reprise : le meme projet, un projet modifie (Actualiser le
//       contexte d'abord), introuvable (on ne propose pas de reprendre) ;
//    6. la notification << mission:<id> >> ouvre le tableau de bord ;
//    7. aucun dessin ne bloque : rien n'est relu ni refait au dessin ;
//    8. 1.22.1 : chaque etape - son pourcentage (mission::stepProgressOf), ce
//       qu'elle fait maintenant (mission::activityOf) dans le plan, l'en-tete,
//       l'ecran de reprise, la bande et la liste ; son journal (un clic) ; le
//       direct (une correction en cours : << (depuis 0:42) >>, refait chaque
//       seconde, jamais a chaque image) ;
//    9. 1.22.2 : les cartes des propositions depliees / reduites (par defaut,
//       un clic, Tout deplier / Tout replier, le clic droit, gardees pour la
//       seance, jamais dans le fichier) ; forcer la fin d'une mission (le clic
//       droit de la liste, le bouton du tableau de bord, la question, l'etat apres).
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiMemoryManager.hpp"
#include "../src/app/ai/AiMissionBoard.hpp"
#include "../src/app/ai/AiMissionStrip.hpp"
#include "../src/app/ai/AiMissionsView.hpp"
#include "../src/app/ai/AiNotifications.hpp"
#include "../src/app/ai/AiNotificationsView.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPreviewDialog.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace ms = app::ai::mission;
namespace mu = app::ai::missionui;
namespace nt = app::ai::notifs;

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

bool said(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return has(l, part); });
}

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path scratch(const std::string& name) {
    const fs::path p = fs::temp_directory_path() / ("xpg_ai_missions_ui_" + name);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

// Un renderer qui retient les textes (les painters y passent).
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
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
};

// Un faux fournisseur : les reponses en file (un texte, la fin).
struct FakeScript {
    std::deque<std::vector<ai::ProviderEvent>> replies;
    std::vector<ai::ProviderRequest>           requests;
};
class FakeProvider final : public ai::Provider {
public:
    explicit FakeProvider(FakeScript& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "openai"; }
    bool start(ai::ProviderRequest r, std::string*) override {
        s_.requests.push_back(std::move(r));
        if (!s_.replies.empty()) {
            pending_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        } else {
            pending_ = textReply("D'accord.");
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override { busy_ = false; }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }
    static std::vector<ai::ProviderEvent> textReply(const std::string& text) {
        ai::ProviderEvent t;
        t.type = ai::ProviderEvent::Type::Text;
        t.text = text;
        ai::ProviderEvent d;
        d.type = ai::ProviderEvent::Type::Done;
        d.stopReason = "end_turn";
        return {t, d};
    }

private:
    FakeScript&                    s_;
    std::vector<ai::ProviderEvent> pending_;
    bool                           busy_{false};
};

const std::string kRequest = "Construis une station de pompage compl\xC3\xA8te avec deux pompes, une cuve, les \xC3\xA9quipements Modbus, les variables, "
                             "les alarmes, les scripts et les vues";

ms::Step makeStep(std::string id, std::string title, std::string cat, ms::StepState st, std::vector<std::string> deps, int ops,
                  const std::string& now) {
    ms::Step s;
    s.id = std::move(id);
    s.title = std::move(title);
    s.detail = "Ce que fait l'\xC3\xA9tape " + s.id + ".";
    s.category = std::move(cat);
    s.state = st;
    s.deps = std::move(deps);
    s.plannedOps = ops;
    s.updated = now;
    return s;
}

ms::OpLine opLine(std::string id, std::string step, std::string title, std::string state) {
    ms::OpLine l;
    l.id = std::move(id);
    l.step = std::move(step);
    l.title = std::move(title);
    l.verb = "creation";
    l.state = std::move(state);
    return l;   // `creates` vide : rien a verifier dans le projet de l'essai (verifyAgainst ne change rien)
}

// ============================================================================ le banc ==
struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};
    std::vector<std::string>     statuses;
    std::vector<std::pair<std::string, int>> opened;   // les tableaux de bord ouverts chez l'hote (mission, mode)
    int                          panelShown{0};
    FakeScript                   script;
    std::string                  stationId, ligneId, cuvesId;
    const std::string            stationConv = "c-station";

    explicit Fixture(const std::string& name, bool host = true) {
        dir = scratch(name);
        ai::setRoot(U(dir / "IA"));
        auto& p = doc->project;
        p.views = {hmi::makeView(p, "Vue_Station")};
        view = p.views.front().id;
        seed();
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        ui.status = [this](const std::string& s) { statuses.push_back(s); };
        if (host) {
            ui.openMission = [this](const std::string& id, int mode) {
                opened.emplace_back(id, mode);
                return true;
            };
            ui.showPanel = [this] { ++panelShown; };
        }
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 440, 900});
        panel->ensureLoaded();
        ai::Settings st = panel->assistant().settings();
        st.provider = "demo";
        panel->assistant().setSettings(st);
        panel->assistant().setProviderFactory([this](std::string_view) { return std::make_unique<FakeProvider>(script); });
        panel->layout();
    }
    ~Fixture() {
        panel.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Les missions et la conversation de la station, ecrites AVANT que le panneau ne les relise.
    void seed() {
        const std::string now = ai::isoDate(ai::now());
        const std::string projectId = ai::memory::identifyProject("Station", U(dir / "Station"), doc->project.config.created).id;
        // la conversation de la station : la demande, P1 appliquee, la suite, P2 a valider
        ai::Conversation c;
        c.id = stationConv;
        c.title = "Station de pompage";
        c.created = c.modified = now;
        const auto msg = [&](std::string id, std::string role, std::string text) {
            ai::Message m;
            m.id = std::move(id);
            m.role = std::move(role);
            m.date = now;
            m.text = std::move(text);
            m.state = m.role == "assistant" ? "fini" : "";
            return m;
        };
        c.messages.push_back(msg("m1", "utilisateur", kRequest));
        {
            auto m = msg("m2", "assistant", "Les \xC3\xA9quipements et les variables de la station.");
            ai::Proposal pr;
            pr.id = "op-p1";
            pr.state = "appliquee";
            pr.view = "Vue_Station";
            pr.viewId = view;
            pr.request = kRequest;
            pr.summary = "\xC3\x89quipements et variables de la station";
            m.proposal = pr;
            c.messages.push_back(std::move(m));
        }
        c.messages.push_back(msg("m3", "utilisateur", "Propose la suite : la vue de la station"));
        {
            auto m = msg("m4", "assistant", "La vue de la station.");
            ai::Proposal pr;
            pr.id = "op-p2";
            pr.state = "proposee";
            pr.view = "Vue_Station";
            pr.viewId = view;
            pr.fingerprint = ai::fingerprint(doc->project.views.front());
            pr.request = "Propose la suite : la vue de la station";
            pr.summary = "Vue de la station";
            ai::Operation op;
            op.action = ai::Operation::Action::Variables;
            op.variables = {{"Niveau_Haut", "BOOL", "Niveau haut de la cuve", ""}};
            pr.ops = {op};
            m.proposal = pr;
            c.messages.push_back(std::move(m));
        }
        check(ai::saveConversation(c), "la conversation de la station est ecrite");

        // 1. LA STATION : en cours, S3 en erreur, P1 appliquee, P2 a valider
        ms::Mission st = ms::make(kRequest, stationConv, now);
        st.name = "Station de pompage";
        st.description = "Deux pompes, une cuve et leur supervision.";
        st.state = ms::State::Active;
        st.planRevision = 1;
        st.conversations = {stationConv};
        st.project.id = projectId;
        st.project.name = "Station";
        st.project.path = U(dir / "Station");
        ms::stamp(st, ms::printOf(doc->project), now);
        st.coherence = ms::Coherence::Same;
        st.steps = {makeStep("S1", "Cr\xC3\xA9" "er les \xC3\xA9quipements Modbus", "equipements", ms::StepState::Done, {}, 2, now),
                    makeStep("S2", "Cr\xC3\xA9" "er les variables", "variables", ms::StepState::Done, {"S1"}, 6, now),
                    makeStep("S3", "Cr\xC3\xA9" "er les alarmes", "alarmes", ms::StepState::Error, {"S2"}, 4, now),
                    makeStep("S4", "Cr\xC3\xA9" "er la vue de la station", "vues", ms::StepState::AwaitingValidation, {"S2"}, 8, now),
                    makeStep("S5", "Cr\xC3\xA9" "er les scripts", "scripts", ms::StepState::Waiting, {"S3", "S4"}, 3, now)};
        st.steps[2].error = "La variable Niveau_Haut est introuvable.";
        st.steps[0].proposals = st.steps[1].proposals = {"op-p1"};
        st.steps[3].proposals = {"op-p2"};
        {
            ms::ProposalLink p1;
            p1.id = "op-p1";
            p1.order = 1;
            p1.conversation = stationConv;
            p1.message = "m2";
            p1.steps = {"S1", "S2"};
            p1.title = "\xC3\x89quipements et variables de la station";
            p1.state = "appliquee";
            p1.ops = p1.create = p1.applied = 3;
            p1.created = p1.appliedAt = now;
            p1.lines = {opLine("E1", "S1", "Cr\xC3\xA9" "er l'\xC3\xA9quipement Automate_1", "appliquee"),
                        opLine("E2", "S2", "Cr\xC3\xA9" "er la variable Pompe_1_Marche", "appliquee"),
                        opLine("E3", "S2", "Cr\xC3\xA9" "er la variable Pompe_2_Marche", "appliquee")};
            ai::plan::Report rep;
            rep.created = {{"Variables", {"Pompe_1_Marche", "Pompe_2_Marche"}}};
            rep.verdict = "3 op\xC3\xA9rations appliqu\xC3\xA9" "es, aucune erreur bloquante.";
            p1.report = ai::plan::reportToJson(rep);
            p1.next = "les alarmes et la vue de la station";
            ms::ProposalLink p2;
            p2.id = "op-p2";
            p2.order = 2;
            p2.conversation = stationConv;
            p2.message = "m4";
            p2.steps = {"S4"};
            p2.title = "Vue de la station";
            p2.state = "generee";
            p2.ops = p2.create = 1;
            p2.created = now;
            st.proposals = {p1, p2};
        }
        ms::log(st, "plan", "Plan \xC3\xA9tabli : 5 \xC3\xA9tapes.", now);
        ms::log(st, "application", "P1 appliqu\xC3\xA9" "e : 3 op\xC3\xA9rations.", now, "S2", "op-p1");
        ms::log(st, "erreur", "S3 : La variable Niveau_Haut est introuvable.", now, "S3");
        stationId = st.id;
        std::string why;
        check(ms::save(st, &why), "la station est ecrite (" + why + ")");

        // 2. LA LIGNE : terminee, archivee, son rapport final ; sa conversation n'existe plus (une proposition ANCIENNE
        //    appliquee, consultable par ce que la mission en garde)
        ms::Mission li = ms::make("Construis la ligne d'embouteillage compl\xC3\xA8te : convoyeurs, remplisseuse, alarmes et vues", "c-disparue", now);
        li.name = "Ligne d'embouteillage";
        li.state = ms::State::Done;
        li.archived = true;
        li.project = st.project;
        li.steps = {makeStep("S1", "Cr\xC3\xA9" "er les convoyeurs", "equipements", ms::StepState::Done, {}, 3, now)};
        li.steps[0].proposals = {"op-old"};
        {
            ms::ProposalLink p;
            p.id = "op-old";
            p.order = 1;
            p.conversation = "c-disparue";
            p.steps = {"S1"};
            p.title = "Les convoyeurs";
            p.state = "appliquee";
            p.ops = p.create = p.applied = 2;
            p.created = p.appliedAt = now;
            p.lines = {opLine("E1", "S1", "Cr\xC3\xA9" "er le convoyeur C1", "appliquee"), opLine("E2", "S1", "Cr\xC3\xA9" "er le convoyeur C2", "appliquee")};
            ai::plan::Report rep;
            rep.created = {{"\xC3\x89quipements", {"Convoyeur_C1", "Convoyeur_C2"}}};
            rep.verdict = "2 op\xC3\xA9rations appliqu\xC3\xA9" "es.";
            p.report = ai::plan::reportToJson(rep);
            li.proposals = {p};
        }
        ms::FinalReport fr;
        fr.created = {"\xC3\xA9quipement Convoyeur_C1", "\xC3\xA9quipement Convoyeur_C2"};
        fr.finished = {"S1 Cr\xC3\xA9" "er les convoyeurs"};
        fr.auditRun = true;
        fr.complete = true;
        fr.date = now;
        fr.verdict = "Mission termin\xC3\xA9" "e : toutes les \xC3\xA9tapes obligatoires sont faites, l'audit est sans erreur.";
        li.finalReport = ms::reportToJson(fr);
        ms::log(li, "terminee", "Mission : Termin\xC3\xA9" "e.", now);
        ligneId = li.id;
        check(ms::save(li, &why), "la ligne est ecrite (" + why + ")");

        // 3. LES CUVES : en pause, sur un AUTRE projet (introuvable ici)
        ms::Mission cu = ms::make("Ajoute les cuves de stockage, leurs niveaux, leurs alarmes et leur vue", "c-cuves", now);
        cu.name = "Cuves de stockage";
        cu.state = ms::State::Paused;
        cu.project.id = "prj-autre-projet";
        cu.project.name = "Cuves";
        cu.project.path = "/ailleurs/Cuves";
        cu.project.print = "empreinte-ailleurs";
        cu.steps = {makeStep("S1", "Cr\xC3\xA9" "er les variables des cuves", "variables", ms::StepState::Ready, {}, 4, now)};
        ms::log(cu, "pause", "Mission en pause.", now);
        cuvesId = cu.id;
        check(ms::save(cu, &why), "les cuves sont ecrites (" + why + ")");
    }

    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
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
    void click(const gfx::Rect& r) { (void)panel->dispatch(ui::MouseDown{{r.x + r.w / 2.f, r.y + r.h / 2.f}, ui::MouseButton::Left, 1, {}}); }
    void live() {
        ai::Settings st = as().settings();
        st.provider = "openai";
        st.preset = "openrouter";
        st.endpoint = "https://openrouter.ai/api/v1";
        st.model = "modele-essai";
        as().setSettings(st);
        as().setApiKey("cle-essai");
    }
    [[nodiscard]] int rowOf(ai::AiMissionsView& v, const std::string& id) const {
        for (std::size_t i = 0; i < v.rowCount(); ++i)
            if (v.rowId(i) == id) return static_cast<int>(i);
        return -1;
    }
};

// ================================================================== 0. les regles de l'ecran ==
void testRules() {
    std::printf("[0. les regles de l'ecran : le geste du moment, l'ecran de reprise]\n");
    const std::string now = "2026-11-01T10:00:00";
    ms::Mission m = ms::make("Construis une station compl\xC3\xA8te", "c-1", now);
    m.state = ms::State::Active;
    m.coherence = ms::Coherence::Same;
    m.steps = {makeStep("S1", "Variables", "variables", ms::StepState::Done, {}, 2, now), makeStep("S2", "Vue", "vues", ms::StepState::Ready, {"S1"}, 3, now)};
    auto n = mu::nextOf(m, false);
    check(n.kind == mu::Next::Kind::Continue && n.label == "Proposer la suite" && std::find(n.steps.begin(), n.steps.end(), "S2") != n.steps.end(),
          "une etape prete : Proposer la suite (S2)");
    check(mu::nextOf(m, true).kind == mu::Next::Kind::None && !mu::nextOf(m, true).why.empty(), "l'IA travaille deja pour elle : aucun geste, et pourquoi");
    m.steps[1].state = ms::StepState::Error;
    m.steps[1].error = "un nom pris";
    n = mu::nextOf(m, false);
    check(n.kind == mu::Next::Kind::Correct && n.step == "S2" && n.label == "Corriger S2" && has(n.tooltip, "un nom pris"), "une etape en erreur : Corriger S2");
    m.coherence = ms::Coherence::Changed;
    check(mu::nextOf(m, false).kind == mu::Next::Kind::Refresh && mu::nextOf(m, false).label == "Actualiser le contexte", "le projet a change : Actualiser le contexte");
    check(mu::wantsResume(m, ms::Coherence::Changed, false) && !mu::wantsResume(m, ms::Coherence::Same, false), "modifie : l'ecran de reprise d'abord");
    m.coherence = ms::Coherence::Missing;
    check(mu::nextOf(m, false).kind == mu::Next::Kind::None && has(mu::nextOf(m, false).why, "ouvre-le"), "introuvable : rien a faire ici (et pourquoi)");
    m.coherence = ms::Coherence::Same;
    m.state = ms::State::Paused;
    check(mu::nextOf(m, false).kind == mu::Next::Kind::Resume && mu::wantsResume(m, ms::Coherence::Same, false), "en pause : Reprendre, l'ecran de reprise");
    m.state = ms::State::Active;
    m.steps[1].state = ms::StepState::Done;
    n = mu::nextOf(m, false);
    check(n.kind == mu::Next::Kind::Audit && n.label == "Lancer l'audit final", "tout est fait : l'audit final");
    m.state = ms::State::Done;
    check(mu::nextOf(m, false).kind == mu::Next::Kind::None && !mu::wantsResume(m, ms::Coherence::Changed, false), "terminee : rien a reprendre");
    // les nombres d'une proposition, la progression d'une etape (ses operations reelles)
    ms::ProposalLink p;
    p.id = "op-1";
    p.order = 1;
    p.steps = {"S1"};
    p.ops = 12;
    p.create = 8;
    p.modify = 3;
    p.reuse = 1;
    p.lines = {opLine("E1", "S1", "a", "appliquee"), opLine("E2", "S1", "b", "echouee"), opLine("E3", "", "c", "appliquee")};
    check(mu::proposalCounts(p) == "12 op\xC3\xA9rations \xC2\xB7 8 cr\xC3\xA9" "ations \xC2\xB7 3 modifications \xC2\xB7 1 r\xC3\xA9utilisation",
          "les nombres : " + mu::proposalCounts(p));
    m.proposals = {p};
    const auto ops = mu::stepOps(m, m.steps[0]);
    check(ops.first == 2 && ops.second == 3, "S1 : 2 operations appliquees sur 3 (celle sans etape compte pour les etapes de la proposition)");
    check(mu::proposalStateLabel("generee") == "\xC3\xA0 valider" && mu::proposalStateLabel("appliquee") == "appliqu\xC3\xA9" "e", "les etats des propositions");
    check(mu::title(ms::Mission{}) == "Mission sans nom", "une mission sans nom ni demande : un titre quand meme");
}

// ============================================================ 1. l'onglet Missions IA ==
void testList() {
    std::printf("[1. l'onglet Missions IA : la liste, la recherche, les filtres, les gestes]\n");
    nt::resetForTests();
    Fixture f("liste");
    f.step();
    check(f.as().missions().size() == 3, "les trois missions sont relues (" + std::to_string(f.as().missions().size()) + ")");
    const auto conv = f.panel->conversationTabRect(), notes = f.panel->notificationsTabRect(), miss = f.panel->missionsTabRect();
    check(miss.w > 0.f && miss.x >= notes.right() && notes.x >= conv.right() && miss.right() <= 440.f + 0.5f && miss.y == conv.y,
          "trois onglets sous l'en-tete : Conversation, Notifications, Missions IA");
    {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Conversation") && rec.saw("Notifications") && rec.saw("Missions IA"), "dessines : les trois onglets");
        const std::string tip = f.panel->liveTooltip({miss.x + 10.f, miss.y + 10.f});
        check(has(tip, "Missions IA"), "l'infobulle de l'onglet (" + tip + ")");
    }
    check(f.panel->missionsBadge() >= 1, "la pastille : au moins une mission attend une decision (la station : S3 en erreur, P2 a valider)");
    f.click(miss);
    f.panel->layout();
    auto& v = f.panel->missions();
    check(f.panel->missionsShown() && v.visible() && !f.panel->chat().visible() && !f.panel->prompt().visible() && !f.panel->notificationsShown(),
          "un clic : la liste des missions a la place de la conversation");
    check(v.bounds().y >= miss.bottom() - 0.5f, "la liste sous les onglets");
    using F = ai::AiMissionsView::Filter;
    check(v.count(F::All) == 2 && v.count(F::Open) == 2 && v.count(F::Archived) == 1, "Toutes 2, En cours 2, Archivees 1");
    check(v.rowCount() == 2 && f.rowOf(v, f.stationId) >= 0 && f.rowOf(v, f.cuvesId) >= 0 && f.rowOf(v, f.ligneId) < 0, "Toutes : les deux non archivees");
    const int rs = f.rowOf(v, f.stationId);
    if (const auto* st = f.as().findMission(f.stationId); st && rs >= 0) {
        const auto p = ms::progressOf(*st);
        const std::string row = v.rowText(static_cast<std::size_t>(rs));
        check(has(row, "Station de pompage") && has(row, std::string(ms::stateLabel(st->state))), "une ligne : le nom, l'etat du modele (" + row + ")");
        check(has(row, std::to_string(p.done) + "/" + std::to_string(p.steps)) && has(row, std::to_string(p.percent()) + " %"),
              "la progression = progressOf (" + std::to_string(p.percent()) + " %)");
        check(has(row, ms::nextAction(*st)) && has(row, "en erreur"), "la prochaine action du modele, l'erreur");
        check(v.secondLabel(static_cast<std::size_t>(rs)) == "Pause", "en cours : Pause");
    }
    const int rc = f.rowOf(v, f.cuvesId);
    check(rc >= 0 && v.secondLabel(static_cast<std::size_t>(rc)) == "Reprendre", "en pause : Reprendre (l'ecran de reprise)");
    {
        Recorder rec;
        v.render(ui::PaintContext{rec, ui::Theme::dark(), v.bounds(), f.t, nullptr});
        check(rec.saw("Station de pompage") && rec.saw("Cuves de stockage") && rec.saw("Tableau de bord"), "dessinee : les lignes et leurs boutons");
    }
    // la recherche : le nom, la demande, les etapes (les archivees aussi)
    v.setSearch("alarmes");
    check(v.rowCount() >= 1 && v.rowId(0) == f.stationId, "recherche \xC2\xAB alarmes \xC2\xBB : la station d'abord");
    v.setSearch("embouteillage");
    check(v.rowCount() == 1 && v.rowId(0) == f.ligneId && has(v.rowText(0), "archiv"), "recherche : une mission archivee se trouve aussi");
    v.setSearch("zzz-introuvable");
    check(v.rowCount() == 0, "rien ne correspond : vide");
    {
        Recorder rec;
        v.render(ui::PaintContext{rec, ui::Theme::dark(), v.bounds(), f.t, nullptr});
        check(rec.saw("Aucune mission ne correspond."), "vide : le dit");
    }
    v.setSearch({});
    // les filtres (au clic)
    f.panel->layout();
    const auto ar = v.filterRect(F::Archived);
    (void)v.dispatch(ui::MouseDown{{ar.x + 5.f, ar.y + 5.f}, ui::MouseButton::Left, 1, {}});
    check(v.filter() == F::Archived && v.rowCount() == 1 && v.rowId(0) == f.ligneId, "Archivees : la ligne");
    v.setFilter(F::Open);
    check(v.rowCount() == 2, "En cours : la station et les cuves");
    v.setFilter(F::All);
    f.panel->layout();
    // les boutons : le tableau de bord (au centre), Reprendre (l'ecran de reprise)
    const int rs2 = f.rowOf(v, f.stationId), rc2 = f.rowOf(v, f.cuvesId);
    const auto b0 = v.buttonRect(static_cast<std::size_t>(rs2), 0);
    check(b0.w > 0.f, "le bouton Tableau de bord");
    (void)v.dispatch(ui::MouseDown{{b0.x + 4.f, b0.y + 4.f}, ui::MouseButton::Left, 1, {}});
    check(!f.opened.empty() && f.opened.back() == std::make_pair(f.stationId, 0), "Tableau de bord : l'hote l'ouvre au centre");
    const auto b1 = v.buttonRect(static_cast<std::size_t>(rc2), 1);
    if (b1.w > 0.f) (void)v.dispatch(ui::MouseDown{{b1.x + 4.f, b1.y + 4.f}, ui::MouseButton::Left, 1, {}});
    check(!f.opened.empty() && f.opened.back() == std::make_pair(f.cuvesId, 1), "Reprendre : l'ecran de reprise d'abord");
    // le clic droit
    const auto rr = v.rowRect(static_cast<std::size_t>(rs2));
    (void)v.dispatch(ui::MouseDown{{rr.x + 60.f, rr.y + 10.f}, ui::MouseButton::Right, 1, {}});
    bool open = false, resume = false, conversation = false, cont = false, pause = false, rename = false, history = false, archive = false, cancel = false;
    for (const auto& it : v.menu()->items()) {
        open |= it.id == ai::AiMissionsView::kMenuOpen;
        resume |= it.id == ai::AiMissionsView::kMenuResumeScreen;
        conversation |= it.id == ai::AiMissionsView::kMenuConversation;
        cont |= it.id == ai::AiMissionsView::kMenuContinue && !it.enabled && !it.disabledReason.empty();   // S3 en erreur : pas de suite
        pause |= it.id == ai::AiMissionsView::kMenuPause;
        rename |= it.id == ai::AiMissionsView::kMenuRename && it.shortcut == "F2";
        history |= it.id == ai::AiMissionsView::kMenuHistory;
        archive |= it.id == ai::AiMissionsView::kMenuArchive;
        cancel |= it.id == ai::AiMissionsView::kMenuCancel && it.enabled;
    }
    check(v.menu()->isOpen() && open && resume && conversation && cont && pause && rename && history && archive && cancel,
          "le menu : tableau de bord, reprise, conversation, Proposer la suite (grise, et pourquoi), pause, renommer, historique, archiver, annuler");
    v.menu()->itemChosen->emit(ai::AiMissionsView::kMenuHistory);
    v.menu()->close();
    check(f.opened.back() == std::make_pair(f.stationId, 2), "Historique : le tableau de bord, l'historique en vue");
    // le clavier : Entree (le tableau de bord), F2 (renommer), Suppr (archiver)
    v.takeFocus();
    (void)v.dispatch(ui::KeyDown{ui::Key::Home, {}, false});
    check(v.selectedRow() == 0, "Debut : la premiere");
    (void)v.dispatch(ui::KeyDown{ui::Key::Return, {}, false});
    check(f.opened.back() == std::make_pair(v.rowId(0), 0), "Entree : son tableau de bord");
    (void)v.select(f.rowOf(v, f.stationId));
    (void)v.dispatch(ui::KeyDown{ui::Key::F2, {}, false});
    check(v.renaming() && v.renameField().text() == "Station de pompage", "F2 : le champ, le nom choisi");
    check(v.commitRename("Station de pompage Nord") && f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->name == "Station de pompage Nord",
          "renommee (l'Assistant l'enregistre)");
    f.step();
    check(has(v.rowText(static_cast<std::size_t>(f.rowOf(v, f.stationId))), "Station de pompage Nord"), "la ligne suit (la revision des missions)");
    (void)v.select(f.rowOf(v, f.cuvesId));
    v.takeFocus();
    (void)v.dispatch(ui::KeyDown{ui::Key::Delete, {}, false});
    f.step();
    check(f.as().findMission(f.cuvesId) && f.as().findMission(f.cuvesId)->archived && f.rowOf(v, f.cuvesId) < 0, "Suppr : archivee (elle quitte Toutes)");
    check(f.panel->missionAct(f.cuvesId, ai::AiPanel::MissionAct::Unarchive) && !f.as().findMission(f.cuvesId)->archived, "Desarchiver la remet");
    // la creation : le +, la demande ; sans IA connectee, l'Assistant dit pourquoi (rien n'est cree)
    const std::size_t before = f.as().missions().size();
    v.newButton().clicked->emit();
    f.panel->layout();
    check(v.composerOpen() && v.createButton().visible(), "+ : la demande et Creer la mission");
    check(!v.create(), "une demande vide : rien");
    v.composer().setText(kRequest);
    const std::size_t said0 = f.statuses.size();
    v.createButton().clicked->emit();
    check(f.as().missions().size() == before && f.statuses.size() > said0, "sans IA connectee : pas de mission, la barre d'etat dit pourquoi");
    v.cancelButton().clicked->emit();
    check(!v.composerOpen(), "Annuler la ferme");
    // un autre clic : la conversation revient
    f.click(f.panel->missionsTabRect());
    f.panel->layout();
    check(!f.panel->missionsShown() && f.panel->chat().visible(), "un autre clic : la conversation revient");
    f.click(f.panel->missionsTabRect());
    f.click(f.panel->notificationsTabRect());
    check(f.panel->notificationsShown() && !f.panel->missionsShown(), "un onglet a la fois : Notifications quitte Missions IA");
    f.click(f.panel->conversationTabRect());
    check(!f.panel->notificationsShown() && !f.panel->missionsShown(), "l'onglet Conversation");
}

// ============================================================ 2. la bande Mission IA ==
void testStrip() {
    std::printf("[2. la bande Mission IA de la conversation ouverte]\n");
    Fixture f("bande");
    check(f.as().select(f.stationConv), "la conversation de la station");
    f.panel->sync();
    f.step();
    f.step();
    const auto* m = f.as().activeMission();
    check(m && m->id == f.stationId, "la mission active : celle de la conversation ouverte");
    auto& s = f.panel->missionStrip();
    check(s.visible() && s.missionId() == f.stationId && s.bounds().h > 0.f, "la bande est montree");
    check(s.bounds().bottom() <= f.panel->chatRect().y + 0.5f && s.bounds().y >= f.panel->conversationTabRect().bottom(),
          "au-dessus de la discussion (elle ne la recouvre pas)");
    if (m) {
        const auto lines = s.lines();
        const auto p = ms::progressOf(*m);
        check(said(lines, "Station de pompage") && said(lines, std::string(ms::stateLabel(m->state))), "le nom, l'etat");
        check(said(lines, std::to_string(p.done) + "/" + std::to_string(p.steps) + " \xC3\xA9tapes") && said(lines, std::to_string(p.percent()) + " %"),
              "les etapes terminees sur le total, la progression = progressOf");
        check(s.progress().global == p.global, "la barre : la progression globale du modele");
        check(said(lines, "Prochaine : " + ms::nextAction(*m)), "la prochaine action");
        check(said(lines, "P1 appliqu\xC3\xA9" "e") && said(lines, "P2 \xC3\xA0 valider"), "les propositions et leur etat");
        check(said(lines, "S3 en erreur"), "les erreurs");
    }
    check(s.actionButton().visible() && s.actionButton().text() == "Corriger S3", "le geste du moment : Corriger S3");
    check(s.boardButton().visible(), "Tableau de bord");
    {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Mission IA") && rec.saw("Station de pompage"), "dessinee");
        const auto h = s.headRect();
        check(has(s.liveTooltip({h.x + 120.f, h.y + 10.f}), "tableau de bord"), "l'infobulle : un clic ouvre le tableau de bord");
    }
    s.boardButton().clicked->emit();
    check(!f.opened.empty() && f.opened.back() == std::make_pair(f.stationId, 0), "Tableau de bord : au centre");
    const auto h = s.headRect();
    (void)f.panel->dispatch(ui::MouseDown{{h.x + 150.f, h.y + 10.f}, ui::MouseButton::Left, 1, {}});
    check(f.opened.size() >= 2 && f.opened.back().first == f.stationId, "un clic sur la bande : le tableau de bord");
    // repliee : deux lignes, plus basse
    const float tall = s.bounds().h;
    s.setExpanded(false);
    f.step();
    check(s.bounds().h < tall && s.lines().size() == 2 && !s.actionButton().visible(), "repliee : le nom et la progression seulement");
    s.setExpanded(true);
    f.step();
    // une autre conversation : plus de bande
    f.as().newConversation();
    f.panel->sync();
    f.step();
    check(s.missionId().empty() && !s.visible(), "une conversation sans mission : pas de bande");
}

// ================================================== 3. << Creer une mission IA ? >> ==
void testAsk() {
    std::printf("[3. Creer une mission IA ? (une demande complexe)]\n");
    Fixture f("choix");
    f.live();
    f.as().newConversation();
    f.panel->sync();
    f.step();
    check(f.as().suggestsMission(kRequest), "l'Assistant juge la demande complexe");
    check(!f.as().suggestsMission("Bonjour") && !f.as().suggestsMission("O\xC3\xB9 en est la station de pompage ?"), "une question, une demande simple : non");
    const std::size_t before = f.as().current() ? f.as().current()->messages.size() : 0;
    const std::size_t requests = f.script.requests.size();
    f.panel->prompt().setText(kRequest);
    check(!f.panel->send(), "Entree : rien ne part");
    f.panel->layout();
    check(f.panel->missionAskShown() && f.panel->missionAsk().visible() && !f.as().generating() && f.script.requests.size() == requests
              && (f.as().current() ? f.as().current()->messages.size() : 0) == before,
          "la carte de choix ; aucune demande n'est partie, aucun message ajoute");
    check(f.panel->prompt().text() == kRequest, "le texte reste dans la saisie");
    const auto ask = f.panel->missionAsk().bounds();
    check(ask.bottom() <= f.panel->promptRect().y + 0.5f && ask.y >= f.panel->chatRect().bottom() - 0.5f, "au-dessus de la saisie, sous la discussion");
    check(said(f.panel->missionAsk().lines(), "Cr\xC3\xA9" "er une mission IA ?") && f.panel->missionAsk().createButton().text() == "Cr\xC3\xA9" "er la mission"
              && f.panel->missionAsk().continueButton().text() == "Continuer en conversation",
          "Creer une mission IA ? : Creer la mission, Continuer en conversation");
    {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("Cr\xC3\xA9" "er une mission IA ?"), "dessinee");
    }
    // la croix : rien ne part
    f.click(f.panel->missionAsk().closeRect());
    f.panel->layout();
    check(!f.panel->missionAskShown() && !f.panel->missionAsk().visible() && f.panel->prompt().text() == kRequest && !f.as().generating(),
          "la croix : la carte part, rien n'est envoye, le texte reste");
    // une demande simple : envoyee tout de suite
    f.panel->prompt().setText("Bonjour");
    check(f.panel->send() && !f.panel->missionAskShown() && f.panel->prompt().text().empty(), "une demande simple : envoyee, sans carte");
    f.finish();
    // Continuer en conversation : l'envoi normal
    f.panel->prompt().setText(kRequest);
    (void)f.panel->send();
    check(f.panel->missionAskShown(), "de nouveau la carte");
    const std::size_t sent = f.script.requests.size();
    f.panel->missionAsk().continueButton().clicked->emit();
    check(!f.panel->missionAskShown() && f.script.requests.size() == sent + 1 && f.panel->prompt().text().empty(), "Continuer en conversation : envoyee comme avant");
    f.finish();
    {
        bool found = false;
        if (const auto* c = f.as().current())
            for (const auto& msg : c->messages) found |= msg.role == "utilisateur" && msg.text == kRequest;
        check(found, "la demande est dans la conversation");
    }
    // Creer la mission
    f.as().newConversation();
    f.panel->sync();
    const std::size_t missions = f.as().missions().size();
    f.panel->prompt().setText(kRequest);
    (void)f.panel->send();
    check(f.panel->missionAskShown(), "une conversation neuve : la carte");
    f.panel->missionAsk().createButton().clicked->emit();
    check(f.as().missions().size() == missions + 1, "Creer la mission : une mission de plus");
    check(!f.panel->missionAskShown() && f.panel->prompt().text().empty() && !f.panel->missionsShown(), "la carte part, la saisie se vide, la conversation reste devant");
    const auto* m = f.as().activeMission();
    check(m && m->request == kRequest, "la mission active : la demande exacte");
    f.finish();
    check(!f.as().suggestsMission(kRequest), "une mission dans la conversation : plus de carte");
    f.step();
    check(f.panel->missions().count(ai::AiMissionsView::Filter::All) == 3, "l'onglet Missions IA la liste");
    check(!f.panel->missionStrip().missionId().empty(), "la bande la montre");
}

// ================================================================== 4. le tableau de bord ==
void testBoard() {
    std::printf("[4. le tableau de bord d'une mission]\n");
    Fixture f("tableau");
    (void)f.as().select(f.stationConv);
    f.panel->sync();
    f.step();
    ai::AiMissionBoard board("mission.essai", *f.panel, f.stationId);
    board.setBounds({0, 0, 1100, 3000});
    board.layout();
    const auto* m = f.as().findMission(f.stationId);
    check(m && board.found() && board.mode() == ai::AiMissionBoard::Mode::Board && has(board.tabTitle(), "Station de pompage"), "trouvee ; l'onglet : son nom");
    if (!m) return;
    const auto p = ms::progressOf(*m);
    check(board.progress().global == p.global && board.progress().planning == p.planning && board.progress().generation == p.generation
              && board.progress().validation == p.validation && board.progress().application == p.application,
          "les progressions = progressOf (jamais un pourcentage invente)");
    auto lines = board.lines();
    check(said(lines, std::to_string(p.percent()) + " %") && said(lines, p.text()), "le pourcentage et le compte du modele");
    for (const char* sec : {"entete", "progression", "objectif", "prochaine", "plan", "propositions", "suite", "historique"})
        check(board.sectionRect(sec).h > 0.f, std::string("la section ") + sec);
    check(board.sectionRect("rapport").h <= 0.f && board.sectionRect("reprise").h <= 0.f, "pas de rapport final, pas d'ecran de reprise");
    // les etats : ceux du modele, avec leur libelle
    for (const auto& s : m->steps)
        check(said(lines, s.id) && said(lines, std::string(ms::stepLabel(s.state))), s.id + " : " + std::string(ms::stepLabel(s.state)));
    check(said(lines, "La variable Niveau_Haut est introuvable."), "l'erreur exacte de S3");
    check(said(lines, ms::nextAction(*m)), "la prochaine action du modele");
    check(board.button("action") && board.button("action")->text() == "Corriger S3", "son bouton : Corriger S3");
    check(said(lines, "Demande initiale") && said(lines, "Deux pompes, une cuve et leur supervision."), "l'objectif : la description, la demande");
    // les propositions : P1 appliquee (consultable : son compte rendu), P2 a valider (Previsualiser, Verifier, Appliquer)
    check(said(lines, "P1") && said(lines, "P2") && said(lines, "\xC3\xA0 valider") && said(lines, "appliqu\xC3\xA9" "e"), "P1 et P2, leurs etats");
    // 1.22.2 : P1 (appliquee) est reduite par defaut, P2 (a valider) depliee - P1 depliee ici pour la lire en entier
    check(!board.cardOpen("op-p1") && board.cardOpen("op-p2"), "par defaut : P1 reduite, P2 depliee");
    board.setAllCards(true);
    lines = board.lines();
    check(said(lines, mu::proposalCounts(m->proposals[0])), "les nombres de P1 (" + mu::proposalCounts(m->proposals[0]) + ")");
    check(said(lines, "Compte rendu") && said(lines, "Pompe_1_Marche"), "P1 appliquee : son compte rendu reel reste consultable");
    check(said(lines, "Suite pr\xC3\xA9vue par l'IA : les alarmes et la vue de la station"), "la suite dite par l'IA");
    check(board.button("P2.apercu") && board.button("P2.verifier") && board.button("P2.appliquer") && board.button("P1.voir") && !board.button("P1.apercu"),
          "P2 : Previsualiser, Verifier, Appliquer ; P1 : Voir dans la conversation");
    // la regle de progression en infobulle
    {
        Recorder rec;
        board.render(ui::PaintContext{rec, ui::Theme::dark(), board.bounds(), f.t, nullptr});
        check(rec.saw("PLAN D'ACTION") && rec.saw("PROPOSITIONS (2)") && rec.saw("Station de pompage"), "dessine");
        const auto r = board.sectionRect("progression");
        check(has(board.liveTooltip({r.x + 200.f, r.y + 40.f}), ms::progressRule()), "la regle de progression en infobulle");
    }
    // Previsualiser P2 : par la conversation de la mission, la fenetre d'apercu du panneau
    f.as().newConversation();
    f.panel->sync();
    board.button("P2.apercu")->clicked->emit();
    f.mm.applyPending();
    check(f.as().current() && f.as().current()->id == f.stationConv, "la conversation de la mission est rouverte");
    check(dynamic_cast<ai::AiPreviewDialog*>(f.mm.top()) != nullptr, "Previsualiser : la fenetre d'apercu");
    if (f.mm.top()) {
        f.mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        f.mm.applyPending();
    }
    // Verifier P2 : sa carte, dans la discussion, le panneau montre
    const int shown = f.panelShown;
    board.button("P2.verifier")->clicked->emit();
    f.panel->layout();
    const int msg = f.as().messageOfProposal("op-p2");
    check(msg >= 0 && f.panel->chat().cardRect(msg).h > 0.f && f.panelShown > shown && !f.panel->missionsShown(), "Verifier : sa carte, le panneau montre");
    // Pause, Reprendre, Annuler (une question d'abord)
    board.button("pause")->clicked->emit();
    f.step();
    (void)board.poll();
    check(f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->state == ms::State::Paused && board.button("reprendre") && !board.button("pause"),
          "Pause : en pause ; le bouton dit Reprendre");
    board.button("reprendre")->clicked->emit();
    f.step();
    (void)board.poll();
    check(f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->state != ms::State::Paused, "Reprendre : plus en pause");
    board.button("annuler")->clicked->emit();
    f.mm.applyPending();
    check(f.mm.top() && f.mm.top()->id() == "dialog.ai.mission.annuler", "Annuler la mission : une question d'abord");
    f.mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});   // Garder
    f.mm.applyPending();
    check(f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->state != ms::State::Cancelled, "Garder : rien ne change");
    // UNE PROPOSITION ANCIENNE APPLIQUEE, sa conversation supprimee : consultable par ce que la mission en garde ; le rapport final
    ai::AiMissionBoard old("mission.ligne", *f.panel, f.ligneId);
    old.setBounds({0, 0, 1100, 3000});
    old.layout();
    lines = old.lines();
    check(old.sectionRect("rapport").h > 0.f && said(lines, "RAPPORT FINAL") && said(lines, "Mission termin\xC3\xA9" "e : toutes les \xC3\xA9tapes"),
          "le rapport final : le verdict");
    check(!old.cardOpen("op-old"), "1.22.2 : sa proposition appliquee est reduite par defaut");
    old.setAllCards(true);   // depliee pour la lire en entier
    lines = old.lines();
    check(said(lines, "Convoyeur_C1") && said(lines, "Sa conversation a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9" "e") && !old.button("P1.voir"),
          "P1 (ancienne, appliquee) : son compte rendu, sa conversation supprimee le dit");
    check(!old.button("action") && old.button("pause") && !old.button("pause")->enabled() && old.button("annuler") && !old.button("annuler")->enabled(),
          "terminee : rien a faire (Pause et Annuler grises)");
    // une mission introuvable
    ai::AiMissionBoard none("mission.rien", *f.panel, "mission-inexistante");
    none.setBounds({0, 0, 900, 600});
    none.layout();
    check(!none.found() && said(none.lines(), "Mission introuvable"), "une mission introuvable : le dit");
    // etroit : le plan en lignes compactes, rien ne deborde
    board.setBounds({0, 0, 560, 3000});
    board.layout();
    check(board.sectionRect("plan").h > 0.f && said(board.lines(), "S3"), "etroit : le plan reste lisible");
}

// ================================================================== 5. l'ecran de reprise ==
void testResume() {
    std::printf("[5. l'ecran de reprise : meme projet, modifie, introuvable]\n");
    Fixture f("reprise");
    (void)f.as().select(f.stationConv);
    f.panel->sync();
    f.step();
    // A. le meme projet, la mission en pause : Reprendre la mission
    check(f.panel->missionAct(f.stationId, ai::AiPanel::MissionAct::Pause), "la station en pause");
    ai::AiMissionBoard board("mission.reprise", *f.panel, f.stationId);
    board.setBounds({0, 0, 1000, 2400});
    board.setMode(ai::AiMissionBoard::Mode::Resume);
    board.layout();
    const auto rec0 = f.as().inspectMission(f.stationId);
    check(board.recovery().coherence == rec0.coherence && board.recovery().coherence == ms::Coherence::Same, "le constat = inspectMission : le meme projet");
    auto lines = board.lines();
    check(board.sectionRect("reprise").h > 0.f && said(lines, "REPRISE DE LA MISSION") && said(lines, "Dernier \xC3\xA9tat sauvegard\xC3\xA9"), "l'ecran de reprise");
    check(said(lines, std::string(ms::coherenceLabel(ms::Coherence::Same))), "l'etat du projet : inchange");
    check(said(lines, "Termin\xC3\xA9" "es :") && said(lines, "En cours :") && said(lines, "Restantes :"), "les etapes terminees, en cours, restantes");
    check(said(lines, "Derni\xC3\xA8res op\xC3\xA9rations appliqu\xC3\xA9" "es") && said(lines, "Pompe_1_Marche"), "les dernieres operations appliquees");
    check(said(lines, "Prochaine action"), "la prochaine action");
    check(board.button("reprise.reprendre") && board.button("reprise.reprendre")->text() == "Reprendre la mission" && !board.button("reprise.actualiser"),
          "le bouton principal : Reprendre la mission");
    check(board.sectionRect("historique").h > 0.f, "l'historique reste consultable");
    board.button("reprise.reprendre")->clicked->emit();
    f.step();
    check(f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->state != ms::State::Paused && board.mode() == ai::AiMissionBoard::Mode::Board,
          "Reprendre : la mission repart, le tableau de bord");
    // B. le projet a change : Actualiser le contexte d'abord, la liste des changements
    f.doc->project.views.push_back(hmi::makeView(f.doc->project, "Vue_Accueil"));
    ++f.doc->revision;
    board.setMode(ai::AiMissionBoard::Mode::Resume);
    lines = board.lines();
    check(board.recovery().coherence == ms::Coherence::Changed && !board.recovery().changes.empty(), "modifie : le constat le dit, avec ce qui a change");
    check(said(lines, std::string(ms::coherenceLabel(ms::Coherence::Changed))) && said(lines, board.recovery().changes.empty() ? std::string("?") : board.recovery().changes.front()),
          "la liste des changements");
    check(board.button("reprise.actualiser") && board.button("reprise.actualiser")->text() == "Actualiser le contexte" && !board.button("reprise.reprendre"),
          "Actualiser le contexte d'abord (la mission n'est pas en pause : pas de Reprendre)");
    // le constat n'est refait que sur un geste (ou au tick, au plus toutes les 2 s) - jamais au dessin
    const auto changes = board.recovery().changes;
    f.doc->project.views.pop_back();
    ++f.doc->revision;
    {
        Recorder rec;
        board.render(ui::PaintContext{rec, ui::Theme::dark(), board.bounds(), f.t, nullptr});
        check(board.recovery().coherence == ms::Coherence::Changed && board.recovery().changes == changes, "le dessin ne refait pas le constat");
    }
    check(board.button("reprise.verifier") != nullptr, "Verifier de nouveau");
    if (auto* again = board.button("reprise.verifier")) again->clicked->emit();
    check(board.recovery().coherence == f.as().inspectMission(f.stationId).coherence, "Verifier de nouveau : le constat du moment");
    // C. introuvable (les cuves : un autre projet) - on l'explique, on ne propose pas de reprendre, l'historique reste
    ai::AiMissionBoard cuves("mission.cuves", *f.panel, f.cuvesId);
    cuves.setBounds({0, 0, 1000, 400});
    cuves.setMode(ai::AiMissionBoard::Mode::Resume);
    cuves.layout();
    lines = cuves.lines();
    check(cuves.recovery().coherence == ms::Coherence::Missing && said(lines, std::string(ms::coherenceLabel(ms::Coherence::Missing))), "introuvable : le dit");
    check(said(lines, "La mission ne peut pas reprendre ici") && said(lines, "Cuves"), "l'explication (son projet, son nom)");
    check(!cuves.button("reprise.reprendre") && !cuves.button("reprise.actualiser") && !cuves.button("action"), "pas de Reprendre, pas d'Actualiser");
    check(!cuves.button("reprendre") || !cuves.button("reprendre")->enabled(), "l'en-tete non plus ne propose pas de reprendre");
    check(cuves.button("reprise.historique") && cuves.button("reprise.historique")->text() == "Consulter l'historique", "Consulter l'historique");
    cuves.button("reprise.historique")->clicked->emit();
    cuves.layout();
    const auto h = cuves.sectionRect("historique");
    check(h.h > 0.f && cuves.scrollOffset() > 0.f && h.y < cuves.bounds().bottom() && h.bottom() > cuves.bounds().y, "l'historique en vue");
    // le panneau ouvre l'ecran de reprise de lui-meme pour une mission en pause
    check(f.panel->openMission(f.cuvesId) && f.opened.back() == std::make_pair(f.cuvesId, 1), "une mission en pause : l'ecran de reprise d'abord");
}

// ============================================================ 6. la notification mission:<id> ==
void testNotification() {
    std::printf("[6. la notification mission:<id>]\n");
    nt::resetForTests();
    {
        Fixture f("notification");
        check(nt::parseAction(nt::toMission(f.stationId)).kind == "mission", "le lien mission:<id>");
        check(f.panel->followNotification(nt::toMission(f.stationId)) && !f.opened.empty() && f.opened.back() == std::make_pair(f.stationId, 0),
              "mission:<id> : le tableau de bord, au centre");
        check(f.panel->followNotification(nt::toMission(f.cuvesId)) && f.opened.back() == std::make_pair(f.cuvesId, 1), "en pause : l'ecran de reprise");
        check(!f.panel->followNotification(nt::toMission("mission-disparue")) && has(f.statuses.back(), "n'existe plus"), "une mission disparue : le dit");
        // par l'onglet Notifications et le bouton de la ligne
        nt::Item n;
        n.category = "Mission";
        n.title = "Mission \xC2\xAB Station de pompage \xC2\xBB : une proposition est pr\xC3\xAAte";
        n.button = "Ouvrir la mission";
        n.action = nt::toMission(f.stationId);
        (void)nt::post(n);
        f.step();
        f.panel->showNotifications(true);
        f.panel->layout();
        const std::size_t before = f.opened.size();
        check(f.panel->notifications().rowCount() >= 1 && f.panel->notifications().follow(0) && f.opened.size() == before + 1
                  && f.opened.back().first == f.stationId,
              "le bouton de la notification : le tableau de bord");
    }
    nt::resetForTests();
    {
        Fixture f("notification-sans-hote", false);
        check(f.panel->followNotification(nt::toMission(f.stationId)) && f.panel->missionsShown()
                  && f.panel->missions().selectedRow() >= 0 && f.panel->missions().rowId(static_cast<std::size_t>(f.panel->missions().selectedRow())) == f.stationId,
              "sans hote : l'onglet Missions IA, la mission choisie");
    }
    nt::resetForTests();
}

// ================================================================== 7. aucun dessin ne bloque ==
void testNoBlocking() {
    std::printf("[7. aucun dessin ne bloque]\n");
    Fixture f("dessin");
    (void)f.as().select(f.stationConv);
    f.panel->sync();
    f.step();
    ai::AiMissionBoard board("mission.dessin", *f.panel, f.stationId);
    board.setBounds({0, 0, 1100, 900});
    board.layout();
    const auto rev = f.as().missionsRevision();
    const auto lines = board.lines();
    check(!board.poll(), "rien n'a change : le tableau de bord ne se refait pas");
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        board.render(ui::PaintContext{rec, ui::Theme::dark(), board.bounds(), f.t, nullptr});
    }
    f.panel->showMissions(true);
    f.panel->layout();
    for (int i = 0; i < 200; ++i) {
        Recorder rec;
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(ms < 4000.0, "600 dessins (panneau, bande, liste, tableau de bord) : " + std::to_string(static_cast<int>(ms)) + " ms");
    check(f.as().missionsRevision() == rev && board.lines() == lines && !board.poll(), "le dessin ne relit ni ne change rien");
    // le defilement ne refait pas la page
    board.scrollTo(400.f);
    check(board.scrollOffset() > 0.f && board.lines() == lines, "defiler : la meme page, decalee");
}


// ================================== 8. 1.22.1 : ce que fait chaque etape (le pourcentage, l'activite, le journal) ==
void testSteps() {
    std::printf("[8. 1.22.1 : chaque etape - son pourcentage, ce qu'elle fait, son journal ; en direct]\n");
    nt::resetForTests();
    Fixture f("etapes");
    (void)f.as().select(f.stationConv);
    f.panel->sync();
    f.step();
    f.step();
    const auto* m = f.as().findMission(f.stationId);
    check(m != nullptr, "la station");
    if (!m) return;
    // le modele : S1 terminee (100 %), S3 en erreur, S4 attend la validation de P2, S5 attend S3 et S4
    const std::string a1 = ms::activityOf(*m, *m->step("S1")).text;
    check(ms::stepProgressOf(*m, *m->step("S1")).percent == 100 && has(a1, "Termin\xC3\xA9" "e \xE2\x80\x94 1 op\xC3\xA9ration appliqu\xC3\xA9" "e le "),
          "S1 : 100 %, " + a1);
    const std::string kS4 = "P2 : 1 op\xC3\xA9ration \xC3\xA0 valider \xE2\x80\x94 Pr\xC3\xA9visualiser puis Appliquer";
    check(ms::activityOf(*m, *m->step("S4")).text == kS4 && ms::stepProgressOf(*m, *m->step("S4")).percent == 0, "S4 : 0 %, " + kS4);
    check(ms::activityOf(*m, *m->step("S3")).text == "En erreur : La variable Niveau_Haut est introuvable.", "S3 : en erreur, l'erreur exacte");
    const std::string a5 = ms::activityOf(*m, *m->step("S5")).text;
    check(a5 == "Attend S3, S4 (d\xC3\xA9pendances)" || a5 == "Bloqu\xC3\xA9" "e : d\xC3\xA9pend de S3 (en erreur)", "S5 : " + a5);
    // LE TABLEAU DE BORD : le pourcentage et l'activite de chaque etape, l'etape du moment dans l'en-tete
    ai::AiMissionBoard board("mission.etapes", *f.panel, f.stationId);
    board.setBounds({0, 0, 1100, 3000});
    board.layout();
    auto lines = board.lines();
    for (const auto& s : m->steps) {
        const std::string act = ms::activityOf(*m, s).text;
        check(said(lines, act), "le plan : ce que fait " + s.id + " (" + act + ")");
    }
    check(said(lines, "100 %") && said(lines, "1/1 op.") && said(lines, "estim. 8 op."), "les pourcentages : S1 100 % (1/1 op.), S4 estime");
    check(said(lines, "En ce moment : S4 \xC2\xB7 0 % \xE2\x80\x94 " + kS4), "l'en-tete : en ce moment, S4 et ce qu'elle fait");
    {
        Recorder rec;
        board.render(ui::PaintContext{rec, ui::Theme::dark(), board.bounds(), f.t, nullptr});
        check(rec.saw(kS4) && rec.saw("100 %"), "dessine : le pourcentage et l'activite");
        const gfx::Rect r = board.stepRect("S1");
        const std::string tip = board.liveTooltip({r.x + 300.f, r.y + r.h / 2.f});
        check(has(tip, "En ce moment : " + a1) && has(tip, ms::stepProgressRule()), "l'infobulle d'une etape : ce qu'elle fait, la regle par etape");
    }
    // LE JOURNAL : un clic sur la ligne de S1 le deplie (les propositions, les operations faites), un autre le replie
    check(!board.stepOpen("S1"), "le journal de S1 : replie");
    const gfx::Rect r1 = board.stepRect("S1");
    check(r1.h > 0.f, "la ligne de S1 dans le plan");
    (void)board.dispatch(ui::MouseDown{{r1.x + 300.f, r1.y + r1.h / 2.f}, ui::MouseButton::Left, 1, {}});
    check(board.stepOpen("S1"), "un clic sur sa ligne : son journal");
    lines = board.lines();
    check(said(lines, "Journal de S1") && said(lines, "P1 appliqu\xC3\xA9" "e : 3 op\xC3\xA9rations.") && said(lines, "\xE2\x9C\x93 Cr\xC3\xA9" "er l'\xC3\xA9quipement Automate_1"),
          "le journal : l'application de P1 et l'operation faite (son libelle)");
    check(said(lines, "Voir la carte de P1 \xE2\x80\xBA"), "le lien vers la carte de P1");
    check(board.toggleStep("S1") && !board.stepOpen("S1") && !said(board.lines(), "Journal de S1"), "un autre clic : replie");
    check(board.toggleStep("S3") && said(board.lines(), "S3 : La variable Niveau_Haut est introuvable."), "le journal de S3 : son erreur");
    // L'ECRAN DE REPRISE : ou en est chaque etape
    {
        ai::AiMissionBoard resume("mission.etapes.reprise", *f.panel, f.stationId);
        resume.setBounds({0, 0, 1100, 2400});
        resume.setMode(ai::AiMissionBoard::Mode::Resume);
        resume.layout();
        const auto rl = resume.lines();
        check(said(rl, "O\xC3\xB9 en est chaque \xC3\xA9tape") && said(rl, "S1 \xC2\xB7 100 % \xE2\x80\x94 Termin\xC3\xA9" "e") && said(rl, "S4 \xC2\xB7 0 % \xE2\x80\x94 " + kS4),
              "l'ecran de reprise : chaque etape, son pourcentage, ce qu'elle fait");
    }
    // LA BANDE : l'etape du moment (S4), son pourcentage, ce qu'elle fait
    auto& strip = f.panel->missionStrip();
    {
        const auto sl = strip.lines();
        check(strip.moment().step == "S4" && said(sl, "S4 Cr\xC3\xA9" "er la vue de la station \xC2\xB7 0 %") && said(sl, kS4), "la bande : S4, 0 %, " + kS4);
        check(strip.actionButton().visible(), "la bande garde son bouton (le geste du moment)");
    }
    // L'ONGLET MISSIONS IA : l'etape du moment sur la ligne ; la ligne choisie montre toutes ses etapes
    auto& v = f.panel->missions();
    f.panel->showMissions(true);
    f.panel->layout();
    const int rs = f.rowOf(v, f.stationId);
    check(rs >= 0 && has(v.rowText(static_cast<std::size_t>(rs)), "En ce moment : S4 \xC2\xB7 0 % \xE2\x80\x94 " + kS4), "la ligne : en ce moment, S4");
    check(v.select(rs), "la station choisie");
    f.panel->layout();
    {
        const std::string row = v.rowText(static_cast<std::size_t>(f.rowOf(v, f.stationId)));
        check(has(row, "S1 \xC2\xB7 100 % \xE2\x80\x94 " + a1) && has(row, "S3 \xC2\xB7 0 % \xE2\x80\x94 En erreur : La variable Niveau_Haut") && has(row, "S4 \xC2\xB7 0 % \xE2\x80\x94 " + kS4),
              "choisie : toutes ses etapes (" + row + ")");
        Recorder rec;
        v.render(ui::PaintContext{rec, ui::Theme::dark(), v.bounds(), f.t, nullptr});
        check(rec.saw("S1 \xC2\xB7 100 %") && rec.saw("S3 \xC2\xB7 0 %"), "dessinee : S1 a 100 %, S3 a 0 %");
    }
    f.panel->showMissions(false);
    f.panel->layout();
    // EN DIRECT : une correction de S3 demandee - l'IA repond, le temps ecoule se compte (refait une fois par seconde)
    const std::time_t t0 = ai::now();
    ai::setClock([t0] { return t0; });
    f.live();
    check(f.panel->missionAct(f.stationId, ai::AiPanel::MissionAct::Correct, "S3"), "Corriger S3 : la demande part");
    const auto* mm = f.as().findMission(f.stationId);
    check(f.as().generating() && mm && mm->pending.active && mm->pending.purpose == "correction", "l'IA repond ; l'appel est ecrit avant de partir");
    ai::setClock([t0] { return t0 + 42; });
    const std::string live = "L'IA pr\xC3\xA9pare une correction (P3)\xE2\x80\xA6 (depuis 0:42)";
    (void)strip.poll();
    check(said(strip.lines(), live) && strip.moment().step == "S3" && strip.moment().activity.live, "la bande : S3, " + live);
    check(board.poll() && said(board.lines(), live) && said(board.lines(), "En ce moment : S3"), "le tableau de bord se refait : " + live);
    check(!board.poll(), "la meme seconde : rien a refaire");
    ai::setClock([t0] { return t0 + 43; });
    check(board.poll() && said(board.lines(), "(depuis 0:43)"), "une seconde de plus : refait (0:43)");
    (void)v.poll();
    const int rs2 = f.rowOf(v, f.stationId);
    check(rs2 >= 0 && has(v.rowText(static_cast<std::size_t>(rs2)), "L'IA pr\xC3\xA9pare une correction (P3)\xE2\x80\xA6 (depuis 0:43)"), "la liste aussi");
    ai::setClock({});
    f.finish();
    const bool redone = board.poll();
    check(!f.as().generating() && redone, "la reponse finie : le tableau de bord se refait");
    if (const auto* after = f.as().findMission(f.stationId)) {
        std::vector<std::string> t3;
        for (const auto& l : ms::stepJournal(*after, *after->step("S3"))) t3.push_back(l.text);
        check(said(t3, "Correction demand\xC3\xA9" "e \xC3\xA0 l'IA pour S3") && said(t3, "La correction de S3 a \xC3\xA9" "chou\xC3\xA9"),
              "le journal de S3 : la correction demandee, son echec (l'IA n'a rien propose)");
        check(!after->pending.active && !ms::activityOf(*after, *after->step("S3")).live, "plus rien en direct");
    }
    nt::resetForTests();
}


// =================== 9. 1.22.2 : les propositions depliees / reduites ; forcer la fin d'une mission ==
void testCardsAndForce() {
    std::printf("[9. 1.22.2 : les propositions depliees / reduites ; forcer la fin d'une mission]\n");
    nt::resetForTests();
    Fixture f("cartes");
    (void)f.as().select(f.stationConv);
    f.panel->sync();
    f.step();
    const fs::path file = f.dir / "IA" / "missions" / (f.stationId + ".json");
    const auto readFile = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string onDisk = readFile(file);
    check(!onDisk.empty(), "le fichier de la station");
    ai::AiMissionBoard board("mission.cartes", *f.panel, f.stationId);
    board.setBounds({0, 0, 1100, 3000});
    board.layout();
    const auto* m = f.as().findMission(f.stationId);
    check(m != nullptr, "la station");
    if (!m) return;
    const auto clickAt = [&board](const gfx::Rect& r, ui::MouseButton b) {
        (void)board.dispatch(ui::MouseDown{{r.x + std::min(300.f, r.w / 2.f), r.y + r.h / 2.f}, b, 1, {}});
    };
    // LES CHOIX PAR DEFAUT : depliees celles qui attendent une decision, reduites les autres
    check(mu::cardOpenByDefault("generee") && mu::cardOpenByDefault("validee") && mu::cardOpenByDefault("echouee") && !mu::cardOpenByDefault("appliquee")
              && !mu::cardOpenByDefault("refusee") && !mu::cardOpenByDefault("annulee") && !mu::cardOpenByDefault("obsolete")
              && !mu::cardOpenByDefault("interrompue"),
          "par defaut : depliees a valider, verifiee, en echec ; reduites appliquee, refusee, annulee, obsolete, interrompue");
    check(!board.cardOpen("op-p1") && board.cardOpen("op-p2"), "P1 (appliquee) reduite, P2 (a valider) depliee");
    check(mu::proposalLine(m->proposals[0]) == "3 op\xC3\xA9rations \xC2\xB7 3 appliqu\xC3\xA9" "es", "la ligne de P1 : " + mu::proposalLine(m->proposals[0]));
    {
        ms::ProposalLink big = m->proposals[0];
        big.ops = 302;
        big.applied = 293;
        big.failed = 9;
        check(mu::proposalLine(big) == "302 op\xC3\xA9rations \xC2\xB7 293 appliqu\xC3\xA9" "es, 9 en \xC3\xA9" "chec", "celle de la capture : " + mu::proposalLine(big));
    }
    const std::string p1Line = "\xC3\x89quipements et variables de la station \xE2\x80\x94 3 op\xC3\xA9rations \xC2\xB7 3 appliqu\xC3\xA9" "es";
    auto lines = board.lines();
    const float tall = board.sectionRect("propositions").h;
    check(said(lines, p1Line), "P1 tient sur une ligne : son titre et ses nombres");
    check(!said(lines, "Compte rendu") && !said(lines, "Suite pr\xC3\xA9vue par l'IA : les alarmes") && !board.button("P1.voir"),
          "P1 reduite : ni compte rendu, ni suite, ni bouton");
    check(said(lines, "Vue de la station") && !said(lines, "Vue de la station \xE2\x80\x94") && board.button("P2.apercu") && board.button("P2.verifier")
              && board.button("P2.appliquer"),
          "P2 depliee : sa carte entiere, ses gestes");
    check(said(lines, "Tout d\xC3\xA9plier") && said(lines, "Tout replier") && board.allCardsRect(true).w > 0.f && board.allCardsRect(false).w > 0.f,
          "l'intertitre : Tout deplier, Tout replier");
    {
        Recorder rec;
        board.render(ui::PaintContext{rec, ui::Theme::dark(), board.bounds(), f.t, nullptr});
        check(rec.saw(p1Line) && rec.saw("\xE2\x96\xB8") && rec.saw("\xE2\x96\xBE"), "dessine : la ligne de P1, les chevrons (reduite, depliee)");
        const gfx::Rect h1 = board.cardHeadRect("op-p1");
        const std::string tip = board.liveTooltip({h1.x + 300.f, h1.y + h1.h / 2.f});
        check(has(tip, "Un clic : la d\xC3\xA9plier") && has(tip, "Clic droit"), "l'infobulle de l'en-tete : " + tip);
    }
    // UN CLIC SUR L'EN-TETE : P1 depliee (son compte rendu), un autre : reduite
    check(board.cardHeadRect("op-p1").h > 0.f && board.cardHeadRect("op-p2").h > 0.f, "les en-tetes des cartes");
    clickAt(board.cardHeadRect("op-p1"), ui::MouseButton::Left);
    lines = board.lines();
    check(board.cardOpen("op-p1") && said(lines, "Compte rendu") && said(lines, "Pompe_1_Marche") && board.button("P1.voir"),
          "un clic sur son en-tete : P1 depliee (son compte rendu, Voir dans la conversation)");
    check(f.panel->missionCard(f.stationId, "op-p1") == std::optional<bool>(true) && !f.panel->missionCard(f.stationId, "op-p2"),
          "le choix est garde par le panneau (pour la seance) ; P2 : rien de choisi");
    clickAt(board.cardHeadRect("op-p1"), ui::MouseButton::Left);
    check(!board.cardOpen("op-p1") && !said(board.lines(), "Compte rendu"), "un autre clic : reduite");
    // TOUT DEPLIER / TOUT REPLIER (l'intertitre)
    clickAt(board.allCardsRect(true), ui::MouseButton::Left);
    check(board.cardOpen("op-p1") && board.cardOpen("op-p2"), "Tout deplier : les deux cartes");
    clickAt(board.allCardsRect(false), ui::MouseButton::Left);
    lines = board.lines();
    check(!board.cardOpen("op-p1") && !board.cardOpen("op-p2") && !board.button("P2.appliquer")
              && said(lines, "Vue de la station \xE2\x80\x94 1 op\xC3\xA9ration \xC2\xB7 1 cr\xC3\xA9" "ation"),
          "Tout replier : les deux (P2 en une ligne ; ses gestes partent avec sa carte)");
    check(board.sectionRect("propositions").h < tall, "la section est plus courte");
    // LE CLIC DROIT sur l'en-tete : Deplier, Reduire, Tout deplier, Tout replier (le menu de l'application)
    clickAt(board.cardHeadRect("op-p2"), ui::MouseButton::Right);
    bool unfold = false, fold = false, allOpen = false, allClose = false;
    for (const auto& it : board.menu()->items()) {
        unfold |= it.id == ai::AiMissionBoard::kMenuCardOpen && it.enabled;
        fold |= it.id == ai::AiMissionBoard::kMenuCardClose && !it.enabled && !it.disabledReason.empty();
        allOpen |= it.id == ai::AiMissionBoard::kMenuAllOpen && it.enabled;
        allClose |= it.id == ai::AiMissionBoard::kMenuAllClose && it.enabled;
    }
    check(board.menu()->isOpen() && unfold && fold && allOpen && allClose, "le clic droit : Deplier, Reduire (grise : deja reduite), Tout deplier, Tout replier");
    board.menu()->itemChosen->emit(ai::AiMissionBoard::kMenuCardOpen);
    board.menu()->close();
    check(board.cardOpen("op-p2") && !board.cardOpen("op-p1"), "Deplier : P2 seulement");
    clickAt(board.cardHeadRect("op-p1"), ui::MouseButton::Right);
    board.menu()->itemChosen->emit(ai::AiMissionBoard::kMenuAllOpen);
    board.menu()->close();
    check(board.cardOpen("op-p1") && board.cardOpen("op-p2"), "Tout deplier (le menu) : les deux");
    clickAt(board.cardHeadRect("op-p2"), ui::MouseButton::Right);
    board.menu()->itemChosen->emit(ai::AiMissionBoard::kMenuCardClose);
    board.menu()->close();
    check(board.cardOpen("op-p1") && !board.cardOpen("op-p2"), "Reduire (le menu) : P2 seulement");
    // LE CHOIX EST GARDE POUR LA SEANCE : un autre tableau de bord de la meme mission (l'onglet ferme puis rouvert)
    {
        ai::AiMissionBoard again("mission.cartes.encore", *f.panel, f.stationId);
        again.setBounds({0, 0, 1100, 3000});
        again.layout();
        check(again.cardOpen("op-p1") && !again.cardOpen("op-p2"), "rouvert : les cartes comme tu les as laissees");
    }
    check(readFile(file) == onDisk, "rien de cela n'est dans le fichier de la mission (il n'a pas change)");

    // FORCER LA FIN : le clic droit de l'onglet Missions IA - une question d'abord ; Garder : rien ne change
    {
        const auto* now = f.as().findMission(f.stationId);
        const std::string q = now ? mu::forceFinishQuestion(*now, false) : std::string{};
        check(has(q, "3 \xC3\xA9tapes ne sont pas termin\xC3\xA9" "es (S3, S4, S5)") && has(q, "\xC2\xAB ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) \xC2\xBB")
                  && has(q, "Rien n'est d\xC3\xA9" "fait dans le projet") && has(q, "(P2) ne sera pas appliqu\xC3\xA9" "e") && !has(q, "La r\xC3\xA9ponse de l'IA"),
              "la question dit ce qui va se passer : " + q);
        check(now && has(mu::forceFinishQuestion(*now, true), "La r\xC3\xA9ponse de l'IA en cours pour elle est arr\xC3\xAAt\xC3\xA9" "e"), "pendant un appel : la reponse arretee, dit");
    }
    auto& v = f.panel->missions();
    f.panel->showMissions(true);
    f.panel->layout();
    const int row = f.rowOf(v, f.stationId);
    const gfx::Rect rr = row >= 0 ? v.rowRect(static_cast<std::size_t>(row)) : gfx::Rect{};
    check(rr.h > 0.f, "la ligne de la station");
    (void)v.dispatch(ui::MouseDown{{rr.x + 60.f, rr.y + 10.f}, ui::MouseButton::Right, 1, {}});
    bool force = false;
    for (const auto& it : v.menu()->items())
        force |= it.id == ai::AiMissionsView::kMenuForceFinish && it.enabled && it.label == "Forcer la fin de la mission\xE2\x80\xA6";
    check(v.menu()->isOpen() && force, "le clic droit d'une mission : Forcer la fin de la mission...");
    v.menu()->itemChosen->emit(ai::AiMissionsView::kMenuForceFinish);
    v.menu()->close();
    f.mm.applyPending();
    check(f.mm.top() && f.mm.top()->id() == "dialog.ai.mission.fin-forcee", "une question d'abord");
    f.mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});   // Garder
    f.mm.applyPending();
    check(f.as().findMission(f.stationId) && f.as().findMission(f.stationId)->state == ms::State::Active && !f.as().findMission(f.stationId)->forced.on(),
          "Garder : rien ne change");
    f.panel->showMissions(false);
    f.panel->layout();
    // ... puis le bouton du tableau de bord : Forcer la fin
    check(board.button("forcer") && board.button("forcer")->enabled() && board.button("forcer")->text() == "Forcer la fin de la mission\xE2\x80\xA6",
          "le bouton de l'en-tete du tableau de bord");
    board.button("forcer")->clicked->emit();
    f.mm.applyPending();
    check(f.mm.top() && f.mm.top()->id() == "dialog.ai.mission.fin-forcee", "le bouton : la meme question");
    f.mm.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});   // Forcer la fin
    f.mm.applyPending();
    const auto* after = f.as().findMission(f.stationId);
    check(after && after->state == ms::State::Done && after->forced.on() && after->forced.steps.size() == 3, "Forcer la fin : terminee (fin forcee), trois etapes ignorees");
    check(after && after->step("S1")->state == ms::StepState::Done && after->step("S2")->state == ms::StepState::Done
              && after->step("S3")->state == ms::StepState::Skipped && after->step("S4")->state == ms::StepState::Skipped
              && after->step("S5")->state == ms::StepState::Skipped,
          "S1, S2 restent terminees ; S3, S4, S5 ignorees");
    check(after && after->proposal("op-p2") && after->proposal("op-p2")->state == "refusee", "P2 refusee (jamais appliquee)");
    const int msg2 = f.as().messageOfProposal("op-p2");
    check(msg2 >= 0 && f.as().proposal(msg2) && f.as().proposal(msg2)->state == "refusee", "sa carte dans la conversation aussi");
    check(!f.statuses.empty() && has(f.statuses.back(), "fin forc"), "la barre d'etat le dit (" + (f.statuses.empty() ? std::string{} : f.statuses.back()) + ")");
    bool notified = false;
    for (const auto& n : nt::list()) notified |= n.category == "Mission" && n.title.rfind("Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)", 0) == 0;
    check(notified, "une notification : Mission terminee (fin forcee)");
    // l'audit final (son projet est ouvert), comme pour une fin normale ; il ne la rouvre pas
    for (int k = 0; k < 2500 && f.as().auditing(f.stationId); ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        f.step(0.002);
    }
    f.step();
    after = f.as().findMission(f.stationId);
    const ms::FinalReport fr = after && after->finalReport.isObject() ? ms::reportFromJson(after->finalReport) : ms::FinalReport{};
    check(after && after->state == ms::State::Done && fr.forced && fr.skipped.size() == 3 && has(fr.verdict, "fin forc"),
          "l'audit final : son rapport dit la fin forcee (" + fr.verdict + ")");
    (void)board.poll();
    lines = board.lines();
    check(said(lines, "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)") && said(lines, "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)"),
          "le tableau de bord : Terminee (fin forcee) ; ses etapes : Ignoree (fin forcee)");
    check(said(lines, "En ce moment : Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : 3 \xC3\xA9tapes ignor\xC3\xA9" "es (S3, S4, S5)."), "l'activite : Terminee (fin forcee)");
    check(board.progress().steps == 2 && board.progress().done == 2 && board.progress().skipped == 3 && board.progress().percent() == 100,
          "la progression sans les etapes ignorees : " + board.progress().text());
    check(said(lines, "RAPPORT FINAL") && said(lines, "Ignor\xC3\xA9" "es (fin forc\xC3\xA9" "e) (3)"), "le rapport final : les etapes ignorees");
    check(board.button("forcer") && !board.button("forcer")->enabled() && has(board.button("forcer")->tooltip(), "d\xC3\xA9j\xC3\xA0 termin")
              && board.button("annuler") && !board.button("annuler")->enabled(),
          "Forcer la fin, Annuler : grises (et pourquoi)");
    if (after) {
        ms::Mission copy = *after;
        copy.finalReport = ai::json::Value();
        check(mu::nextOf(copy, false).kind == mu::Next::Kind::Audit, "une fin forcee sans rapport : Lancer l'audit final");
        check(mu::nextOf(*after, false).kind == mu::Next::Kind::None, "avec son rapport : plus rien a faire");
    }
    // la bande, la liste : << Terminee (fin forcee) >> ; le clic droit ne la propose plus (et pourquoi)
    auto& strip = f.panel->missionStrip();
    (void)strip.poll();
    check(said(strip.lines(), "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)"), "la bande le dit");
    f.panel->showMissions(true);
    f.panel->layout();
    (void)v.poll();
    f.panel->layout();
    const int row2 = f.rowOf(v, f.stationId);
    check(row2 >= 0 && has(v.rowText(static_cast<std::size_t>(row2)), "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)"), "la liste aussi");
    const gfx::Rect rr2 = row2 >= 0 ? v.rowRect(static_cast<std::size_t>(row2)) : gfx::Rect{};
    if (rr2.h > 0.f) (void)v.dispatch(ui::MouseDown{{rr2.x + 60.f, rr2.y + 10.f}, ui::MouseButton::Right, 1, {}});
    bool greyed = false;
    for (const auto& it : v.menu()->items()) greyed |= it.id == ai::AiMissionsView::kMenuForceFinish && !it.enabled && !it.disabledReason.empty();
    check(greyed, "le clic droit : Forcer la fin grise (deja terminee)");
    v.menu()->close();
    check(!f.panel->missionAct(f.stationId, ai::AiPanel::MissionAct::ForceFinish) && !f.mm.top() && has(f.statuses.back(), "d\xC3\xA9j\xC3\xA0 termin"),
          "deja terminee : refusee tout de suite, sans question (et pourquoi)");
    nt::resetForTests();
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
    testRules();
    testList();
    testStrip();
    testAsk();
    testBoard();
    testResume();
    testNotification();
    testNoBlocking();
    testSteps();   // 1.22.1
    testCardsAndForce();   // 1.22.2
    std::printf("ai_missions_ui_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
