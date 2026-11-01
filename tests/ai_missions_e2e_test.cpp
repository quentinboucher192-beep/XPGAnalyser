// =============================================================================
//  tests/ai_missions_e2e_test.cpp - 1.22.0 : les missions IA persistantes, de bout en bout
// -----------------------------------------------------------------------------
//  L'Assistant (AiAssistant) branche sur un FAUX fournisseur (il retient les
//  requetes, rend des evenements prepares), sur un vrai document IHM et une vraie
//  pile d'annulation, avec un dossier IA/ jetable :
//    - la suggestion (une demande simple : non ; complexe : << Creer une mission IA ? >>) ;
//    - la creation : l'appel ecrit AVANT de partir, la consigne de planification,
//      l'outil planifier_mission ; le plan (dependances implicites, ordre calcule,
//      jamais celui du modele), son resultat pour le tour suivant ;
//    - les propositions successives liees aux etapes (le lot a la mesure de la
//      longueur d'une reponse), la suite prevue, les noms reels reutilises sans
//      doublon, une commande Ctrl+Z par proposition, Ctrl+Z puis Ctrl+Y ;
//    - la persistance (une autre seance relit la mission) et l'arret brutal (l'appel
//      en cours retrouve : interrompu, rien de rejoue, la reprise sans doublon) ;
//    - le projet modifie pendant la preparation (la proposition obsolete ne
//      s'applique pas, rien n'est ecrase) ;
//    - l'audit final et le rapport ; une erreur d'etape (les dependantes bloquees,
//      les independantes continuent), sa correction ; la pause (la reponse en cours
//      arretee, l'application refusee), la reprise, l'annulation ; un autre projet
//      ouvert (la mission ne se poursuit pas) ;
//    - une autre conversation retrouve la mission (chercher_missions, etat_mission :
//      les donnees enregistrees, jamais inventees) et la reprend ;
//    - 1.22.2 : forcer la fin (pendant un appel a l'IA, une proposition en attente,
//      une application de fond).
//  Scenarios de la demande : A (station de pompage complete), B (interruption
//  pendant la 3e proposition), C (modification externe pendant la preparation),
//  D (une autre conversation).
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiMission.hpp"
#include "../src/app/ai/AiMissionStore.hpp"
#include "../src/app/ai/AiNotifications.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace ms = app::ai::mission;
using ai::json::Value;

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

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::time_t fixedNow() {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 30;
    tm.tm_hour = 21;
    tm.tm_min = 5;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

// ---- le faux fournisseur : il retient les requetes et rend des evenements prepares ----------------------------------
struct FakeScript {
    std::vector<ai::ProviderRequest>           requests;
    std::deque<std::vector<ai::ProviderEvent>> replies;
    bool                                       hang{false};   // la reponse ne vient pas (une reponse en cours)
    int                                        cancels{0};
};
class FakeProvider final : public ai::Provider {
public:
    explicit FakeProvider(FakeScript& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ai::ProviderRequest r, std::string*) override {
        s_.requests.push_back(std::move(r));
        if (!s_.replies.empty()) {
            pending_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        if (s_.hang) return {};
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override {
        ++s_.cancels;
        busy_ = false;
    }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    FakeScript&                    s_;
    std::vector<ai::ProviderEvent> pending_;
    bool                           busy_{false};
};

ai::ProviderEvent textEv(std::string text) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Text;
    e.text = std::move(text);
    return e;
}
ai::ProviderEvent toolEv(const std::string& id, const std::string& name, const std::string& input) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::ToolUse;
    e.toolUseId = id;
    e.toolName = name;
    e.toolInputJson = input;
    return e;
}
ai::ProviderEvent doneEv(const std::string& reason) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Done;
    e.stopReason = reason;
    return e;
}

// ---- les entrees des outils, ecrites avec le JSON du logiciel ----------------------------------------------------------
Value O(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value o = Value::object();
    for (const auto& [k, v] : kv) o.set(k, v);
    return o;
}
Value strs(const std::vector<std::string>& v) {
    Value a = Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}

// Une etape de planifier_mission (le schema du noyau : id, titre, detail, categorie, depend_de, operations, obligatoire,
// produit) ; le plan : nom, description, analyse, etapes.
struct PlanStep {
    std::string              id, title, category;
    std::vector<std::string> deps;
    int                      ops{1};
    std::vector<std::string> produces;
};
std::string planInput(const std::string& name, const std::vector<PlanStep>& steps, const std::vector<std::string>& conform = {},
                      const std::vector<std::string>& toCreate = {}) {
    Value list = Value::array();
    for (const auto& s : steps)
        list.push(O({{"id", s.id},
                     {"titre", s.title},
                     {"detail", "D\xC3\xA9tail de " + s.id},
                     {"categorie", s.category},
                     {"depend_de", strs(s.deps)},
                     {"operations", s.ops},
                     {"obligatoire", true},
                     {"produit", strs(s.produces)}}));
    Value analysis = O({{"conforme", strs(conform)},
                        {"a_modifier", Value::array()},
                        {"a_completer", Value::array()},
                        {"a_creer", strs(toCreate)},
                        {"impossible", Value::array()},
                        {"resume", "Le projet a une vue et une variable ; le reste est \xC3\xA0 cr\xC3\xA9" "er."}});
    return ai::json::write(O({{"nom", name}, {"description", "Une mission d'essai."}, {"analyse", analysis}, {"etapes", list}}), false);
}

std::string proposalInput(const std::string& resume, const std::vector<Value>& ops, const std::string& suite = {}) {
    Value list = Value::array();
    for (const auto& o : ops) list.push(o);
    Value in = O({{"resume", resume}, {"operations", list}});
    if (!suite.empty()) in.set("suite", suite);
    return ai::json::write(in, false);
}

// La station de pompage : P1 (S1 equipement, S2 variables), P2 (S3 alarmes - Cuve_Niveau redemandee : reutilisee -, S4 le
// script), P3 (S5 la vue ouverte completee).
std::string p1Input() {
    Value vars = Value::array();
    for (const auto& [n, t] : std::vector<std::pair<const char*, const char*>>{
             {"Pompe_1_Defaut", "BOOL"}, {"Pompe_2_Marche", "BOOL"}, {"Pompe_2_Defaut", "BOOL"}, {"Cuve_Niveau", "REAL"}})
        vars.push(O({{"nom", n}, {"type", t}}));
    return proposalInput("\xC3\x89quipement et variables de la station",
                         {O({{"action", "creer_equipement"}, {"nom", "Station_Modbus"}, {"type", "modbus_tcp"}, {"adresse", "127.0.0.1"}, {"port", 502},
                             {"esclave", 1}, {"simule", true}, {"etape", "S1"}}),
                          O({{"action", "creer_variables"}, {"variables", vars}, {"etape", "S2"}})},
                         "Les alarmes et le script de s\xC3\xA9" "curit\xC3\xA9 (S3, S4), puis la vue (S5).");
}
std::string p2Input() {
    Value again = Value::array();
    again.push(O({{"nom", "Cuve_Niveau"}, {"type", "REAL"}}));
    return proposalInput(
        "Alarmes et script de la station",
        {O({{"action", "creer_variables"}, {"variables", again}, {"etape", "S3"}}),
         O({{"action", "creer_alarme"}, {"nom", "Defaut_Pompe_1"}, {"condition", "Pompe_1_Defaut"}, {"message", "Pompe 1 en d\xC3\xA9" "faut"}, {"priorite", "2"},
            {"etape", "S3"}}),
         O({{"action", "creer_alarme"}, {"nom", "Defaut_Pompe_2"}, {"condition", "Pompe_2_Defaut"}, {"message", "Pompe 2 en d\xC3\xA9" "faut"}, {"priorite", "2"},
            {"etape", "S3"}}),
         O({{"action", "creer_alarme"}, {"nom", "Niveau_Haut"}, {"condition", "Cuve_Niveau > 90"}, {"message", "Niveau haut"}, {"etape", "S3"}}),
         O({{"action", "creer_script"}, {"nom", "Securite_Pompes"}, {"langage", "ST"}, {"evenement", "Cyclique"}, {"periode", "1000"},
            {"corps", "IF Pompe_1_Defaut THEN Pompe_1_Marche := FALSE; END_IF;"}, {"etape", "S4"}})},
        "La vue de la station (S5).");
}
std::string p3Input() {
    return proposalInput("La vue de la station",
                         {O({{"action", "creer"}, {"genre", "Indicator"}, {"nom", "Voyant_P1"},
                             {"proprietes", O({{"x", "40"}, {"y", "40"}, {"variable", "Pompe_1_Marche"}})}, {"etape", "S5"}}),
                          O({{"action", "creer"}, {"genre", "Indicator"}, {"nom", "Voyant_P2"},
                             {"proprietes", O({{"x", "120"}, {"y", "40"}, {"variable", "Pompe_2_Marche"}})}, {"etape", "S5"}}),
                          O({{"action", "creer"}, {"genre", "Tank"}, {"nom", "Cuve_Station"},
                             {"proprietes", O({{"x", "220"}, {"y", "40"}, {"w", "150"}, {"h", "200"}, {"label", "Cuve"}})}, {"etape", "S5"}})});
}

// Le plan de la station, ecrit dans le desordre (S4, S3, S5, S2, S1) : l'ordre vient des dependances.
std::string stationPlan() {
    return planInput("Station de pompage",
                     {{"S4", "Cr\xC3\xA9" "er le script de s\xC3\xA9" "curit\xC3\xA9", "scripts", {}, 2, {"script:Securite_Pompes"}},
                      {"S3", "Cr\xC3\xA9" "er les alarmes", "alarmes", {}, 3, {"alarme:Defaut_Pompe_1", "alarme:Niveau_Haut"}},
                      {"S5", "Compl\xC3\xA9ter la vue Vue_Station", "vues", {}, 6, {"objet:Voyant_P1"}},
                      {"S2", "Cr\xC3\xA9" "er les variables des pompes et de la cuve", "variables", {"S1"}, 4, {"variable:Cuve_Niveau"}},
                      {"S1", "Cr\xC3\xA9" "er l'\xC3\xA9quipement Modbus", "equipements", {}, 1, {"equipement:Station_Modbus"}}},
                     {"variable:Pompe_1_Marche - d\xC3\xA9j\xC3\xA0 conforme (BOOL)"}, {"equipement:Station_Modbus", "alarme:Niveau_Haut"});
}

const char* const kStationRequest =
    "Construis une station de pompage compl\xC3\xA8te avec deux pompes, une cuve, les \xC3\xA9quipements Modbus, les variables, les alarmes, les "
    "scripts et les vues.";

// ---- l'Assistant sur un vrai document -------------------------------------------------------------------------------------
struct Fixture {
    fs::path                       dir;
    FakeScript                     s;   // avant l'Assistant : il lui survit
    hmi::DocumentPtr               doc = std::make_shared<hmi::Document>();
    core::CommandStack             stack;
    hmi::Id                        view{hmi::kNoId};
    std::string                    projectName;
    std::unique_ptr<ai::Assistant> as;
    double                         t{100.0};

    // `fresh` : le dossier IA/ repart de zero ; `project` : le projet ouvert (une copie : << enregistre puis rouvert >>) ;
    // `name` : le projet (un autre nom : un autre projet, un autre dossier).
    explicit Fixture(const std::string& dirName, bool fresh = true, const hmi::Project* project = nullptr, std::string name = "Station_Pompage")
        : projectName(std::move(name)) {
        dir = fs::temp_directory_path() / ("xpg_ai_missions_" + dirName);
        if (fresh) fs::remove_all(dir);
        fs::create_directories(dir / projectName);
        ai::setRoot(U(dir / "IA"));
        if (project) {
            doc->project = *project;
            if (const hmi::View* v = doc->project.viewByName("Vue_Station")) view = v->id;
        } else {
            auto& p = doc->project;
            hmi::View v = hmi::makeView(p, "Vue_Station");
            p.views = {v};
            view = p.views.front().id;
            hmi::Variable var;
            var.id = p.allocate();
            var.name = "Pompe_1_Marche";
            var.type = "BOOL";
            p.programs.variables.push_back(var);
            p.config.created = "2026-10-01T08:00:00";
        }
        ai::Assistant::Host h;
        h.document = [this] { return doc; };
        h.activeView = [this] { return view; };
        h.selection = [] { return std::vector<hmi::Id>{}; };
        h.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        h.projectName = [this] { return projectName; };
        h.projectPath = [this] { return U(dir / projectName); };
        as = std::make_unique<ai::Assistant>(std::move(h));
        as->setProviderFactory([this](std::string_view) { return std::make_unique<FakeProvider>(s); });
        as->load();
        configure();
    }
    // Une IA connectee (le faux Anthropic), une reponse courte (1 024 jetons : des lots de 6 operations estimees au plus),
    // << appliquer malgre les erreurs >> permis.
    void configure() {
        ai::Settings st = as->settings();
        st.provider = "anthropic";
        st.model = "modele-essai";
        st.outputTokens = ai::kMinOutputTokens;
        st.applyWithErrors = true;
        st.localFirst = false;
        as->setSettings(st);
        as->setApiKey("cle-essai");
        as->newConversation();
    }
    void step(double dt = 0.05) {
        t += dt;
        (void)as->tick(t);
    }
    // Le projet vient de changer (Ctrl+Z, un geste) : l'Assistant le verifie une fois pose (Assistant::kMissionSettle).
    void settle() {
        step();
        step(0.3);
    }
    [[nodiscard]] const ai::Message& last() { return as->current()->messages.back(); }
    [[nodiscard]] int lastIndex() { return static_cast<int>(as->current()->messages.size()) - 1; }
    [[nodiscard]] const ms::Mission* mission(const std::string& id) { return as->findMission(id); }
    [[nodiscard]] ms::StepState stepState(const std::string& id, const std::string& stepId) {
        const ms::Mission* m = mission(id);
        const ms::Step* st = m ? m->step(stepId) : nullptr;
        return st ? st->state : ms::StepState::ToPlan;
    }
    [[nodiscard]] std::string linkState(const std::string& id, const std::string& proposalId) {
        const ms::Mission* m = mission(id);
        const ms::ProposalLink* l = m ? m->proposal(proposalId) : nullptr;
        return l ? l->state : std::string("(aucune)");
    }
    [[nodiscard]] const hmi::Object* object(const std::string& name) {
        const hmi::View* v = doc->project.view(view);
        if (!v) return nullptr;
        for (const auto& o : v->objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    // L'IA repond : du texte, puis l'appel d'un outil.
    void answerPlan(const std::string& toolId, const std::string& input) {
        s.replies.push_back({textEv("J'ai analys\xC3\xA9 le projet r\xC3\xA9" "el."), toolEv(toolId, "planifier_mission", input), doneEv("tool_use")});
    }
    void answerOps(const std::string& toolId, const std::string& input) {
        s.replies.push_back({textEv("Voici la proposition."), toolEv(toolId, "proposer_operations", input), doneEv("tool_use")});
    }
    // Une proposition de la mission arrivee : son message (dans la conversation ouverte) et son identifiant.
    [[nodiscard]] std::string lastProposalId() {
        const auto* c = as->current();
        if (!c) return {};
        for (auto m = c->messages.rbegin(); m != c->messages.rend(); ++m)
            if (m->proposal) return m->proposal->id;
        return {};
    }
    void waitAudit(const std::string& id) {
        for (int k = 0; k < 2500 && as->auditing(id); ++k) {
            step(0.002);
            pause(2);
        }
        step();
    }
};

int indexOfStep(const ms::Mission& m, const std::string& id) {
    for (std::size_t i = 0; i < m.steps.size(); ++i)
        if (m.steps[i].id == id) return static_cast<int>(i);
    return -1;
}

bool listedIn(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// Une notification de la mission (son bouton ouvre la mission) dont le titre commence ainsi.
bool notified(const std::string& missionId, const std::string& titleStart) {
    for (const auto& n : ai::notifs::list())
        if (n.category == "Mission" && n.action == ai::notifs::toMission(missionId) && n.title.rfind(titleStart, 0) == 0) return true;
    return false;
}

std::size_t countVariables(const hmi::Project& p, const std::string& name) {
    return static_cast<std::size_t>(
        std::count_if(p.programs.variables.begin(), p.programs.variables.end(), [&](const hmi::Variable& v) { return v.name == name; }));
}
std::size_t countObjects(const hmi::View& v, const std::string& name) {
    return static_cast<std::size_t>(std::count_if(v.objects.begin(), v.objects.end(), [&](const hmi::Object& o) { return o.name == name; }));
}

const ai::LocalTool* localTool(const ai::ProviderRequest& r, const std::string& name) {
    for (const auto& t : r.localTools)
        if (t.name == name) return &t;
    return nullptr;
}

// ---- 0. le schema du plan (celui du noyau) : les entrees de ces essais le suivent ----------------------------------------
void testPlanSchema() {
    std::printf("[le schema de planifier_mission]\n");
    Value tool;
    check(ai::json::parse(ms::planToolJson(), tool) && tool.isObject() && tool.str("name") == "planifier_mission", "la definition se lit");
    const Value& props = tool.at("input_schema").at("properties");
    const Value& step = props.at("etapes").at("items").at("properties");
    check(props.has("nom") && props.has("analyse") && props.has("etapes"), "le plan : nom, analyse, etapes");
    check(step.has("id") && step.has("titre") && step.has("categorie") && step.has("depend_de") && step.has("operations") && step.has("produit"),
          "une etape : id, titre, categorie, depend_de, operations, produit");
    Value ops;
    check(ai::json::parse(ai::operationsTool(), ops) && ops.isArray() && !ops.items().empty()
              && ops.items().front().at("input_schema").at("properties").at("operations").at("items").at("properties").has("etape"),
          "proposer_operations : chaque operation peut porter << etape >>");
}

// ---- A, B, C, D : la station de pompage ---------------------------------------------------------------------------------
void testStation() {
    std::printf("[A - la station de pompage complete]\n");
    Fixture f("station");
    std::string why;

    // LA SUGGESTION : une demande simple, une question, la demonstration - non ; une demande complexe - oui.
    check(!f.as->suggestsMission("Ajoute un voyant vert"), "une demande simple : pas de mission proposee");
    check(!f.as->suggestsMission("O\xC3\xB9 en est la station de pompage ?"), "une question : pas de mission proposee");
    check(f.as->suggestsMission(kStationRequest), "une demande complexe : << Creer une mission IA ? >>");
    {
        ai::Settings st = f.as->settings();
        st.provider = "demo";
        f.as->setSettings(st);
        check(!f.as->suggestsMission(kStationRequest), "sans IA connectee (la demonstration) : pas de mission");
        f.configure();
    }

    // LA CREATION : l'appel ecrit AVANT de partir, la consigne de planification, l'outil planifier_mission.
    f.answerPlan("toolu_plan", stationPlan());
    const std::size_t sent = f.s.requests.size();
    f.s.hang = true;   // la reponse ne vient pas tout de suite : l'etat << en cours >> se voit
    check(f.as->createMission(kStationRequest, &why), "creer la mission (" + why + ")");
    check(f.as->missions().size() == 1, "une mission");
    const std::string id = f.as->missions().empty() ? std::string{} : f.as->missions().front().id;
    const std::string convId = f.as->current() ? f.as->current()->id : std::string{};
    check(f.s.requests.size() == sent + 1 && f.as->generating(), "la demande est partie");
    if (const ms::Mission* m = f.mission(id)) {
        check(m->state == ms::State::Planning && m->pending.active && m->pending.purpose == "plan" && m->request == kStationRequest,
              "planification : l'appel en cours, la demande exacte");
        check(m->conversation == convId && listedIn(m->conversations, convId) && !m->project.id.empty() && m->project.name == "Station_Pompage"
                  && !m->project.print.empty() && m->provider == "anthropic" && m->model == "modele-essai",
              "la conversation, le projet (identite, empreinte), le fournisseur et le modele");
        const auto onDisk = ms::load(id);
        check(onDisk && onDisk->pending.active && onDisk->pending.purpose == "plan", "l'appel est ecrit sur le disque AVANT la reponse");
        const ai::ProviderRequest& r = f.s.requests.back();
        check(has(r.toolsJson, "planifier_mission") && has(r.toolsJson, "proposer_operations"), "la requete : l'outil planifier_mission en plus");
        check(has(r.systemDynamic, ms::planningPrompt(*m)), "la requete : la consigne de planification (analyser le projet reel, planifier)");
        check(!r.turns.empty() && r.turns.back().text == kStationRequest, "le tour : la demande");
        check(localTool(r, "chercher_missions") && localTool(r, "etat_mission"), "les outils locaux des missions sont offerts");
    }
    check(notified(id, "Mission cr\xC3\xA9\xC3\xA9" "e"), "notification : mission creee");
    check(!f.as->suggestsMission(kStationRequest), "une mission est en cours dans cette conversation : plus de suggestion");

    // LE PLAN.
    f.s.hang = false;
    f.step();
    check(!f.as->generating(), "la reponse est finie");
    const ms::Mission* m = f.mission(id);
    check(m && m->state == ms::State::Active && !m->pending.active && m->name == "Station de pompage" && m->steps.size() == 5, "le plan : 5 etapes, en cours");
    if (m && m->steps.size() == 5) {
        const int s1 = indexOfStep(*m, "S1"), s2 = indexOfStep(*m, "S2"), s3 = indexOfStep(*m, "S3"), s4 = indexOfStep(*m, "S4"), s5 = indexOfStep(*m, "S5");
        check(s1 < s2 && s2 < s3 && s2 < s4 && s2 < s5, "l'ordre vient des dependances (S1, S2, puis le reste), pas de l'ordre ecrit");
        const ms::Step* a = m->step("S3");
        const ms::Step* v = m->step("S5");
        const ms::Step* b = m->step("S2");
        check(a && listedIn(a->deps, "S2") && listedIn(a->implicit, "S2"), "dependance implicite : une alarme lit des variables (S3 -> S2)");
        check(v && listedIn(v->deps, "S2") && listedIn(v->implicit, "S2"), "dependance implicite : une vue montre des variables (S5 -> S2)");
        check(b && listedIn(b->deps, "S1") && !listedIn(b->implicit, "S1"), "la dependance ecrite reste ecrite (S2 -> S1)");
        check(f.stepState(id, "S1") == ms::StepState::Ready && f.stepState(id, "S2") == ms::StepState::Waiting, "S1 prete, S2 en attente");
        check(m->analysis.conform.size() == 1 && has(m->analysis.conform.front(), "Pompe_1_Marche"), "l'analyse : l'existant conforme");
    }
    {
        const auto& msgs = f.as->current()->messages;
        check(msgs.size() >= 3 && msgs.back().extra.str("logiciel") == "mission" && msgs.back().extra.str("mission") == id && has(msgs.back().text, "S5"),
              "le plan en clair : un message du logiciel, lie a la mission");
        const ai::Message& planMsg = msgs[msgs.size() - 2];
        check(planMsg.tool && planMsg.tool->name == "planifier_mission" && planMsg.tool->error.empty() && planMsg.extra.str("mission") == id,
              "le message de l'IA garde son appel de planifier_mission");
        const auto [text, error] = f.as->toolResult(planMsg);
        check(!error && text.rfind("plan enregistr\xC3\xA9 : 5 \xC3\xA9tapes", 0) == 0, "le resultat de l'outil pour le tour suivant : " + text);
    }
    check(notified(id, "Plan termin\xC3\xA9"), "notification : plan termine");

    // P1 : LE LOT A LA MESURE D'UNE REPONSE (1 024 jetons : 6 operations estimees) - S1 et S2.
    check(m && ms::nextBatch(*m, 6).steps == std::vector<std::string>({"S1", "S2"}), "le prochain lot : S1 et S2 (S3 ne tient plus)");
    f.answerOps("toolu_p1", p1Input());
    check(f.as->continueMission(id, &why), "proposer la suite (" + why + ")");
    {
        const ai::ProviderRequest& r = f.s.requests.back();
        check(has(r.systemDynamic, "PROPOSITION P1") && !has(r.toolsJson, "planifier_mission"), "la consigne du lot ; plus l'outil du plan");
        check(has(r.turns.back().text, "S1") && has(r.turns.back().text, "S2") && !has(r.turns.back().text, "S3"), "la demande cite S1 et S2 seulement");
        bool planResult = false;
        for (const auto& t : r.turns)
            if (t.toolResultFor == "toolu_plan" && has(t.toolResultText, "plan enregistr\xC3\xA9")) planResult = true;
        check(planResult, "le tour porte le resultat de planifier_mission");
        check(f.stepState(id, "S1") == ms::StepState::Running && f.mission(id)->pending.purpose == "proposition", "S1 en cours, l'appel ecrit");
    }
    f.step();
    const std::string p1 = f.lastProposalId();
    const int m1 = f.as->messageOfProposal(p1);
    check(!p1.empty() && m1 >= 0, "P1 est arrivee");
    if (m1 >= 0) {
        const ai::Proposal* p = f.as->proposal(m1);
        check(p && p->extra.str("mission") == id && p->extra.at("etapes").items().size() == 2, "P1 est liee a la mission et a ses etapes");
        check(f.as->missionOfProposal(p1) && f.as->missionOfProposal(p1)->id == id, "missionOfProposal");
        check(f.as->check(m1).ok(), "P1 validee (" + (f.as->check(m1).errors.empty() ? std::string{} : f.as->check(m1).errors.front()) + ")");
    }
    if (const ms::Mission* mm = f.mission(id); mm && mm->proposal(p1)) {
        check(mm->proposal(p1)->order == 1 && mm->proposal(p1)->state == "validee", "P1 : verifiee sur une copie, validee");
        check(f.stepState(id, "S1") == ms::StepState::AwaitingValidation && f.stepState(id, "S2") == ms::StepState::AwaitingValidation,
              "S1, S2 : en attente de validation (jamais << terminees >> parce que l'IA l'a propose)");
        check(!mm->pending.active, "l'appel est efface");
    }
    check(notified(id, "Proposition disponible"), "notification : proposition disponible");
    check(!f.as->continueMission(id, &why) && has(why, "attend"), "une proposition attend : pas de suite tant qu'elle n'est pas decidee (" + why + ")");

    // APPLIQUER P1 : UNE commande ; le compte rendu reel termine S1 et S2.
    const std::size_t undo0 = f.stack.done().size();
    check(f.as->apply(m1, &why), "appliquer P1 (" + why + ")");
    check(f.stack.done().size() == undo0 + 1, "P1 : une seule commande d'annulation");
    check(f.doc->project.equipmentByName("Station_Modbus") && f.doc->project.variable("Cuve_Niveau"), "l'equipement et les variables sont dans le projet");
    check(f.linkState(id, p1) == "appliquee" && f.stepState(id, "S1") == ms::StepState::Done && f.stepState(id, "S2") == ms::StepState::Done,
          "P1 appliquee ; S1, S2 terminees (verifiees dans le projet)");
    check(f.stepState(id, "S3") == ms::StepState::Ready && f.stepState(id, "S4") == ms::StepState::Ready && f.stepState(id, "S5") == ms::StepState::Ready,
          "S3, S4, S5 pretes");
    check(notified(id, "Proposition appliqu\xC3\xA9" "e") && notified(id, "\xC3\x89tape termin\xC3\xA9" "e"), "notifications : appliquee, etape terminee");

    // LA SUITE PREVUE, PERSISTANTE : relue du disque.
    if (const auto disk = ms::load(id)) {
        const ms::ProposalLink* l = disk->proposal(p1);
        check(l && l->state == "appliquee" && has(l->next, "S3") && !l->lines.empty(), "P1 sur le disque : appliquee, sa suite, ses operations");
        check(disk->step("S1") && disk->step("S1")->state == ms::StepState::Done && disk->step("S3") && disk->step("S3")->state == ms::StepState::Ready,
              "les etats sur le disque");
        check(has(ms::nextAction(*disk), "S4") || has(ms::nextAction(*disk), "S3"), "la prochaine action : la suite (" + ms::nextAction(*disk) + ")");
    } else {
        check(false, "la mission se relit du disque");
    }

    // CTRL+Z PUIS CTRL+Y : le projet fait foi.
    (void)f.stack.undo();
    f.settle();
    check(!f.doc->project.equipmentByName("Station_Modbus"), "Ctrl+Z : l'equipement est reparti");
    check(f.linkState(id, p1) == "annulee" && f.stepState(id, "S1") == ms::StepState::Ready && f.stepState(id, "S2") == ms::StepState::Waiting,
          "Ctrl+Z : P1 annulee apres coup, S1 de nouveau prete, S2 attend S1 (elle en depend)");
    (void)f.stack.redo();
    f.settle();
    check(f.doc->project.equipmentByName("Station_Modbus") && countVariables(f.doc->project, "Cuve_Niveau") == 1, "Ctrl+Y : de retour, sans doublon");
    check(f.linkState(id, p1) == "appliquee" && f.stepState(id, "S1") == ms::StepState::Done && f.stepState(id, "S2") == ms::StepState::Done,
          "Ctrl+Y : P1 retablie, S1 et S2 terminees");

    // P2 : S4 et S3, sur le projet REEL (les noms crees par P1, verifies) ; Cuve_Niveau redemandee : reutilisee, pas doublee.
    f.answerOps("toolu_p2", p2Input());
    check(f.as->continueMission(id, &why), "la suite : P2 (" + why + ")");
    {
        const ai::ProviderRequest& r = f.s.requests.back();
        check(has(r.systemDynamic, "PROPOSITION P2") && has(r.systemDynamic, "R\xC3\x89" "ELLEMENT DANS LE PROJET") && has(r.systemDynamic, "Station_Modbus")
                  && has(r.systemDynamic, "Cuve_Niveau"),
              "P2 connait ce que P1 a REELLEMENT cree (verifie dans le projet)");
        check(has(r.turns.back().text, "S3") && has(r.turns.back().text, "S4") && !has(r.turns.back().text, "S5"), "le lot : S3 et S4");
    }
    f.step();
    const std::string p2 = f.lastProposalId();
    const int m2 = f.as->messageOfProposal(p2);
    check(p2 != p1 && m2 >= 0 && f.as->check(m2).ok(), "P2 arrivee, validee");
    check(f.as->apply(m2, &why) && f.stack.done().size() == undo0 + 2, "P2 appliquee : une commande de plus (" + why + ")");
    check(countVariables(f.doc->project, "Cuve_Niveau") == 1 && f.doc->project.equipments.size() == 1, "aucun doublon (Cuve_Niveau reutilisee)");
    check(f.doc->project.alarmByName("Niveau_Haut") && f.doc->project.generalScript("Securite_Pompes"), "les alarmes et le script sont la");
    check(f.stepState(id, "S3") == ms::StepState::Done && f.stepState(id, "S4") == ms::StepState::Done && f.linkState(id, p2) == "appliquee",
          "S3, S4 terminees ; P2 appliquee");
    if (const ms::Mission* mm = f.mission(id); mm && mm->proposal(p2)) check(mm->proposal(p2)->order == 2 && mm->proposal(p2)->reuse >= 1, "P2 : une reutilisation");

    // LA PERSISTANCE (fermeture normale) : une autre seance relit la mission, telle quelle.
    {
        Fixture g("station", false, &f.doc->project);
        g.step();
        const ms::Mission* gm = g.mission(id);
        check(gm && gm->steps.size() == 5 && gm->proposals.size() == 2 && !gm->pending.active, "relue : 5 etapes, P1 et P2, aucun appel en cours");
        check(g.stepState(id, "S4") == ms::StepState::Done && g.stepState(id, "S5") == ms::StepState::Ready && g.linkState(id, p2) == "appliquee",
              "relue : les etats");
        check(!notified(id, "Mission interrompue"), "fermeture normale : rien d'interrompu");
        check(g.as->inspectMission(id).coherence == ms::Coherence::Same, "le projet rouvert est identique (cas A)");
    }

    // B - L'ARRET BRUTAL PENDANT LA 3e PROPOSITION : l'appel est ecrit, la reponse ne vient jamais.
    std::printf("[B - interruption pendant la 3e proposition]\n");
    f.answerOps("toolu_p3_perdue", p3Input());
    f.s.hang = true;
    check(f.as->continueMission(id, &why), "la 3e proposition part (" + why + ")");
    f.step();
    check(f.as->generating(), "elle se prepare");
    if (const auto disk = ms::load(id))
        check(disk->pending.active && disk->pending.purpose == "proposition" && disk->pending.steps == std::vector<std::string>({"S5"})
                  && disk->step("S5") && disk->step("S5")->state == ms::StepState::Running,
              "sur le disque : l'appel en cours, S5 en cours");
    // ... la seance s'arrete la (rien n'est enregistre de plus) ; une nouvelle seance rouvre le projet enregistre.
    Fixture h("station", false, &f.doc->project);
    h.step();
    {
        const ms::Mission* hm = h.mission(id);
        check(hm && !hm->pending.active && h.stepState(id, "S5") == ms::StepState::Ready, "reprise : l'appel est marque interrompu, S5 redevient prete");
        bool logged = false;
        if (hm)
            for (const auto& e : hm->history) logged = logged || e.kind == "interruption";
        check(logged, "l'historique dit l'interruption");
        check(hm && hm->coherence == ms::Coherence::Same, "le projet est identique a la derniere sauvegarde");
        check(h.linkState(id, p1) == "appliquee" && h.linkState(id, p2) == "appliquee" && hm && hm->proposals.size() == 2,
              "P1 et P2 sont gardees ; la proposition perdue n'existe pas (rien n'est rejoue)");
        check(notified(id, "Mission interrompue"), "notification : mission interrompue");
        const auto disk = ms::load(id);
        check(disk && !disk->pending.active, "le constat est enregistre");
    }

    // C - LA MODIFICATION EXTERNE PENDANT LA PREPARATION : la proposition arrive obsolete, elle ne s'applique pas.
    std::printf("[C - modification externe pendant la preparation]\n");
    h.answerOps("toolu_p3", p3Input());
    h.s.hang = true;
    check(h.as->continueMission(id, &why), "la reprise : la suite ne redemande que S5 (" + why + ")");
    check(has(h.s.requests.back().turns.back().text, "S5") && !has(h.s.requests.back().turns.back().text, "S1"), "sans duplication : S5 seulement");
    h.step();
    check(h.as->generating(), "P3 se prepare");
    (void)h.stack.push(hmi::changeView(h.doc, h.view, "Un geste de l'utilisateur", [](hmi::Project& p, hmi::View& v) {
        (void)hmi::edit::add(p, v, hmi::Kind::Text, 5, 5);
    }));
    const std::size_t objectsBefore = h.doc->project.view(h.view) ? h.doc->project.view(h.view)->objects.size() : 0;
    h.s.hang = false;
    h.step();
    const std::string p3 = h.lastProposalId();
    const int m3 = h.as->messageOfProposal(p3);
    check(!p3.empty() && m3 >= 0, "P3 est arrivee");
    check(h.linkState(id, p3) == "obsolete" && h.stepState(id, "S5") == ms::StepState::Ready, "P3 obsolete (preparee sur un autre etat) ; S5 prete");
    check(m3 >= 0 && !h.as->apply(m3, &why), "P3 ne s'applique pas (" + why + ")");
    check(!h.object("Voyant_P1") && h.doc->project.view(h.view) && h.doc->project.view(h.view)->objects.size() == objectsBefore,
          "rien n'est ecrase : le geste de l'utilisateur reste, rien de P3 n'est pose");
    check(notified(id, "Proposition obsol\xC3\xA8te"), "notification : proposition obsolete");
    // ... la suite repart du projet tel qu'il est.
    h.answerOps("toolu_p4", p3Input());
    check(h.as->continueMission(id, &why), "la suite, sur le projet reel (" + why + ")");
    check(has(h.s.requests.back().systemDynamic, "Vue_Station"), "elle connait la vue telle qu'elle est");
    h.step();
    const std::string p4 = h.lastProposalId();
    const int m4 = h.as->messageOfProposal(p4);
    const std::size_t undo1 = h.stack.done().size();
    check(m4 >= 0 && h.as->apply(m4, &why) && h.stack.done().size() == undo1 + 1, "P4 appliquee, une commande (" + why + ")");
    check(h.stepState(id, "S5") == ms::StepState::Done && h.object("Voyant_P1") && countObjects(*h.doc->project.view(h.view), "Voyant_P1") == 1,
          "S5 terminee ; les voyants une fois");

    // L'AUDIT FINAL ET LE RAPPORT.
    check(!h.as->continueMission(id, &why) && has(why, "audit"), "tout est fait : la suite propose l'audit (" + why + ")");
    check(h.as->auditMission(id, &why), "l'audit final part (" + why + ")");
    check(h.as->auditing(id) || h.mission(id)->finalReport.isObject(), "l'audit tourne sur une copie");
    h.waitAudit(id);
    check(!h.as->auditing(id), "l'audit est fini");
    if (const ms::Mission* hm = h.mission(id); hm && hm->finalReport.isObject()) {
        const ms::FinalReport rep = ms::reportFromJson(hm->finalReport);
        check(rep.auditRun, "l'audit global a pu etre fait (verifie, pas suppose)");
        check(rep.finished.size() == 5 && rep.notDone.empty(), "rapport : 5 etapes terminees, rien de non realise");
        bool eq = false;
        for (const auto& c : rep.created) eq = eq || has(c, "Station_Modbus");
        check(eq, "rapport : l'equipement cree (verifie dans le projet)");
        bool conformFlag = false;
        for (const auto& c : rep.toCheck) conformFlag = conformFlag || has(c, "L'analyse disait conforme");
        check(!conformFlag, "l'existant dit conforme (Pompe_1_Marche) est bien dans le projet");
        check(rep.complete == (rep.auditErrors == 0), "complet seulement sans erreur bloquante de l'audit");
        if (rep.complete) {
            check(hm->state == ms::State::Done && notified(id, "Mission termin\xC3\xA9" "e"), "terminee : l'etat et la notification");
        } else {
            check(hm->state != ms::State::Done && !notified(id, "Mission termin\xC3\xA9" "e"), "l'audit a des erreurs : jamais << terminee >> (" + rep.verdict + ")");
        }
    } else {
        check(false, "le rapport final est enregistre");
    }

    // D - UNE AUTRE CONVERSATION : << Ou en est la station de pompage ? >> - les donnees enregistrees.
    std::printf("[D - une autre conversation]\n");
    h.as->newConversation();
    const std::string other = h.as->current() ? h.as->current()->id : std::string{};
    check(!other.empty() && !h.as->activeMission(), "une conversation neuve : aucune mission");
    h.s.replies.push_back({textEv("Je regarde les missions enregistr\xC3\xA9" "es."), doneEv("end_turn")});
    check(h.as->send("O\xC3\xB9 en est la station de pompage ?", &why), "la question part (" + why + ")");
    const ai::ProviderRequest r = h.s.requests.back();
    check(has(r.systemDynamic, "MISSIONS IA DE CE PROJET") && has(r.systemDynamic, "Station de pompage") && has(r.systemDynamic, id),
          "toute conversation recoit l'apercu des missions du projet");
    h.step();
    if (const ai::LocalTool* search = localTool(r, "chercher_missions"); search && search->run) {
        const std::atomic<bool> no{false};
        const std::string found = search->run("{\"texte\": \"station de pompage\"}", [](const std::string&) {}, no);
        check(has(found, id) && has(found, "Station de pompage"), "chercher_missions cite la mission exacte (relue du disque)");
        const std::string none = search->run("{\"texte\": \"raffinerie\"}", [](const std::string&) {}, no);
        check(has(none, "Aucune mission") && !has(none, id), "rien ne correspond : dit, rien d'invente");
    } else {
        check(false, "chercher_missions est offert dans une autre conversation");
    }
    if (const ai::LocalTool* detail = localTool(r, "etat_mission"); detail && detail->run) {
        const std::atomic<bool> no{false};
        const std::string fiche = detail->run("{\"mission\": \"" + id + "\"}", [](const std::string&) {}, no);
        check(has(fiche, "S5") && has(fiche, "Station_Modbus") && has(fiche, "Station de pompage"), "etat_mission : la fiche reelle (etapes, elements crees)");
    } else {
        check(false, "etat_mission est offert");
    }
    // << Reprends la mission d'hier >> : retenue dans cette conversation.
    check(!h.as->suggestsMission("Reprends la mission d'hier"), "reprendre n'est pas creer");
    check(h.as->attachMission(id, &why), "reprendre la mission ici (" + why + ")");
    check(h.as->activeMission() && h.as->activeMission()->id == id, "la mission de cette conversation");
    check(h.last().extra.str("mission") == id && h.last().extra.str("logiciel") == "mission", "un message du logiciel dit ou en est la mission");
    if (const ms::Mission* hm = h.mission(id)) check(listedIn(hm->conversations, other), "la mission retient cette conversation");
}

// ---- une erreur d'etape, sa correction ; un autre projet ouvert ------------------------------------------------------------
void testErrorAndCorrection() {
    std::printf("[erreur d'etape, correction, projet introuvable]\n");
    Fixture f("erreur");
    std::string why;
    // S1 les variables de debit ; S2 l'alarme de debit (depend de S1) ; S3 un script independant.
    f.answerPlan("toolu_plan", planInput("Surveillance du d\xC3\xA9" "bit",
                                         {{"S1", "Cr\xC3\xA9" "er la variable de d\xC3\xA9" "bit", "variables", {}, 2, {"variable:Debit"}},
                                          {"S2", "Cr\xC3\xA9" "er l'alarme de d\xC3\xA9" "bit", "alarmes", {"S1"}, 2, {"alarme:Debit_Haut"}},
                                          {"S3", "Cr\xC3\xA9" "er le script d'initialisation", "autre", {}, 1, {"script:Initialisation"}}}));
    check(f.as->createMission("Cr\xC3\xA9" "e la surveillance du d\xC3\xA9" "bit compl\xC3\xA8te : les variables, les alarmes, le script et la vue.", &why),
          "creer (" + why + ")");
    const std::string id = f.as->missions().empty() ? std::string{} : f.as->missions().front().id;
    f.step();
    check(f.mission(id) && f.mission(id)->steps.size() == 3, "le plan : 3 etapes");
    // P1 : la variable a un type inconnu (erreur), l'alarme en depend, le script est sain.
    Value bad = Value::array();
    bad.push(O({{"nom", "Debit"}, {"type", "REEL_INCONNU"}}));
    f.answerOps("toolu_p1", proposalInput("D\xC3\xA9" "bit", {O({{"action", "creer_variables"}, {"variables", bad}, {"etape", "S1"}}),
                                                              O({{"action", "creer_alarme"}, {"nom", "Debit_Haut"}, {"condition", "Debit > 80"},
                                                                 {"message", "D\xC3\xA9" "bit haut"}, {"etape", "S2"}}),
                                                              O({{"action", "creer_script"}, {"nom", "Initialisation"}, {"langage", "ST"}, {"evenement", "Cyclique"},
                                                                 {"periode", "1000"}, {"corps", "Pompe_1_Marche := FALSE;"}, {"etape", "S3"}})}));
    check(f.as->continueMission(id, &why), "la suite (" + why + ")");
    f.step();
    const std::string p1 = f.lastProposalId();
    const int m1 = f.as->messageOfProposal(p1);
    check(m1 >= 0 && !f.as->check(m1).ok(), "P1 : refusee par la validation (un type inconnu)");
    check(m1 >= 0 && !f.as->apply(m1, &why), "elle ne s'applique pas telle quelle");
    check(m1 >= 0 && f.as->applyIgnoringErrors(m1, &why), "appliquee malgre les erreurs (" + why + ")");
    check(f.stepState(id, "S1") == ms::StepState::Error, "S1 en erreur");
    if (const ms::Mission* m = f.mission(id); m && m->step("S1")) check(!m->step("S1")->error.empty(), "l'erreur exacte est gardee : " + m->step("S1")->error);
    check(f.stepState(id, "S2") == ms::StepState::Blocked, "S2 (depend de S1) bloquee");
    check(f.stepState(id, "S3") == ms::StepState::Done && f.doc->project.generalScript("Initialisation"), "S3 (independante) terminee");
    check(f.mission(id) && f.mission(id)->state == ms::State::Blocked && notified(id, "Mission bloqu\xC3\xA9" "e"), "la mission est bloquee, et c'est dit");
    check(notified(id, "Erreur dans la mission"), "notification : erreur");
    check(!f.as->continueMission(id, &why) && has(why, "erreur"), "la suite : une etape est en erreur (" + why + ")");
    // L'audit maintenant : jamais << terminee >> avec une etape obligatoire en erreur.
    check(f.as->auditMission(id, &why), "l'audit (" + why + ")");
    f.waitAudit(id);
    if (const ms::Mission* m = f.mission(id); m && m->finalReport.isObject()) {
        const ms::FinalReport rep = ms::reportFromJson(m->finalReport);
        check(!rep.complete && m->state != ms::State::Done && !rep.errors.empty() && !rep.notDone.empty(), "rapport : non terminee, l'erreur et le reste dits");
    }
    // LA CORRECTION : une nouvelle proposition, verifiee sur une copie, a valider.
    check(!f.as->correctStep(id, "S3", &why), "une etape terminee ne se corrige pas (" + why + ")");
    Value good = Value::array();
    good.push(O({{"nom", "Debit"}, {"type", "REAL"}}));
    f.answerOps("toolu_c1", proposalInput("Correction du d\xC3\xA9" "bit", {O({{"action", "creer_variables"}, {"variables", good}, {"etape", "S1"}})}));
    check(f.as->correctStep(id, "S1", &why), "corriger S1 (" + why + ")");
    check(has(f.s.requests.back().systemDynamic, "CORRECTION") && has(f.s.requests.back().systemDynamic, "REEL_INCONNU"), "la consigne : l'etape, l'erreur exacte");
    f.step();
    const std::string c1 = f.lastProposalId();
    const int mc = f.as->messageOfProposal(c1);
    check(mc >= 0 && f.as->check(mc).ok() && f.stepState(id, "S1") == ms::StepState::AwaitingValidation, "la correction attend la validation");
    check(mc >= 0 && f.as->apply(mc, &why), "appliquer la correction (" + why + ")");
    check(f.stepState(id, "S1") == ms::StepState::Done && f.stepState(id, "S2") == ms::StepState::Ready, "S1 terminee ; S2 debloquee");
    f.answerOps("toolu_p2", proposalInput("Alarme de d\xC3\xA9" "bit", {O({{"action", "creer_alarme"}, {"nom", "Debit_Haut"}, {"condition", "Debit > 80"},
                                                                          {"message", "D\xC3\xA9" "bit haut"}, {"etape", "S2"}})}));
    check(f.as->continueMission(id, &why), "la suite (" + why + ")");
    f.step();
    const int m2 = f.as->messageOfProposal(f.lastProposalId());
    check(m2 >= 0 && f.as->apply(m2, &why) && f.stepState(id, "S2") == ms::StepState::Done, "S2 terminee (" + why + ")");

    // UN AUTRE PROJET OUVERT : la mission ne se poursuit pas (cas C de la reprise).
    const hmi::Project saved = f.doc->project;
    {
        Fixture o("erreur", false, nullptr, "Autre_Projet");
        o.step();
        check(o.as->inspectMission(id).coherence == ms::Coherence::Missing, "un autre projet : la mission dit son projet introuvable");
        check(!o.as->resumeMission(id, &why) && has(why, "pas celui"), "reprendre : refuse et dit (" + why + ")");
        check(!o.as->continueMission(id, &why) && has(why, "projet"), "la suite : refusee (" + why + ")");
        check(!o.as->auditMission(id, &why), "l'audit : refuse sur un autre projet");
    }
    {
        Fixture back("erreur", false, &saved);
        back.step();
        check(back.as->inspectMission(id).coherence == ms::Coherence::Same, "le bon projet rouvert : identique");
        check(back.as->resumeMission(id, &why), "la mission reprend (" + why + ")");
        check(back.mission(id) && back.mission(id)->coherence != ms::Coherence::Missing, "la coherence est retrouvee");
    }
}

// ---- la pause, la reprise, l'annulation ---------------------------------------------------------------------------------------
void testPauseResumeCancel() {
    std::printf("[pause, reprise, annulation]\n");
    Fixture f("pause");
    std::string why;
    f.answerPlan("toolu_plan", planInput("D\xC3\xA9" "bit d'entr\xC3\xA9" "e",
                                         {{"S1", "Cr\xC3\xA9" "er la variable", "variables", {}, 1, {"variable:Debit_Entree"}},
                                          {"S2", "Cr\xC3\xA9" "er l'alarme", "alarmes", {}, 1, {"alarme:Debit_Entree_Haut"}},
                                          {"S3", "Compl\xC3\xA9ter la vue", "vues", {}, 6, {"objet:Voyant_Debit"}}}));
    check(f.as->createMission("Cr\xC3\xA9" "e la surveillance compl\xC3\xA8te du d\xC3\xA9" "bit d'entr\xC3\xA9" "e : variable, alarme et vue.", &why),
          "creer (" + why + ")");
    const std::string id = f.as->missions().empty() ? std::string{} : f.as->missions().front().id;
    f.step();
    Value vars = Value::array();
    vars.push(O({{"nom", "Debit_Entree"}, {"type", "REAL"}}));
    const std::string p1In = proposalInput("Variable et alarme", {O({{"action", "creer_variables"}, {"variables", vars}, {"etape", "S1"}}),
                                                                  O({{"action", "creer_alarme"}, {"nom", "Debit_Entree_Haut"}, {"condition", "Debit_Entree > 50"},
                                                                     {"message", "D\xC3\xA9" "bit haut"}, {"etape", "S2"}})});
    // LA PAUSE ARRETE LA REPONSE EN COURS.
    f.answerOps("toolu_p1_coupee", p1In);
    f.s.hang = true;
    check(f.as->continueMission(id, &why), "la suite part (" + why + ")");
    f.step();
    check(f.as->generating() && f.stepState(id, "S1") == ms::StepState::Running, "elle se prepare");
    const int cancels = f.s.cancels;
    check(f.as->pauseMission(id, &why), "pause (" + why + ")");
    f.s.hang = false;
    check(!f.as->generating() && f.s.cancels == cancels + 1 && f.last().state == "arrete", "la reponse est vraiment arretee");
    check(f.mission(id) && f.mission(id)->state == ms::State::Paused && !f.mission(id)->pending.active, "en pause ; l'appel efface");
    check(f.stepState(id, "S1") == ms::StepState::Ready && f.stepState(id, "S2") == ms::StepState::Waiting,
          "S1 redevient prete (S2 attend toujours S1)");
    check(f.mission(id) && f.mission(id)->proposals.empty(), "aucune proposition n'est nee de la reponse arretee");
    check(!f.as->continueMission(id, &why) && has(why, "pause"), "en pause : pas de suite (" + why + ")");
    // LA REPRISE, puis une proposition ; la pause refuse son application.
    check(f.as->resumeMission(id, &why) && f.mission(id)->state == ms::State::Active, "reprise (" + why + ")");
    f.answerOps("toolu_p1", p1In);
    check(f.as->continueMission(id, &why), "la suite (" + why + ")");
    f.step();
    const std::string p1 = f.lastProposalId();
    const int m1 = f.as->messageOfProposal(p1);
    check(m1 >= 0, "P1 est arrivee");
    check(f.as->pauseMission(id, &why), "pause (" + why + ")");
    const std::size_t undo0 = f.stack.done().size();
    check(m1 >= 0 && !f.as->apply(m1, &why) && has(why, "pause"), "en pause : l'application est refusee (" + why + ")");
    check(f.stack.done().size() == undo0 && !f.doc->project.variable("Debit_Entree"), "le projet n'a pas change");
    check(f.as->resumeMission(id, &why), "reprise (" + why + ")");
    check(m1 >= 0 && f.as->apply(m1, &why) && f.stepState(id, "S1") == ms::StepState::Done, "reprise : P1 s'applique (" + why + ")");
    // L'ANNULATION EMPECHE TOUTE APPLICATION ULTERIEURE.
    f.answerOps("toolu_p2", proposalInput("Voyant", {O({{"action", "creer"}, {"genre", "Indicator"}, {"nom", "Voyant_Debit"},
                                                        {"proprietes", O({{"x", "40"}, {"y", "40"}, {"variable", "Debit_Entree"}})}, {"etape", "S3"}})}));
    check(f.as->continueMission(id, &why), "la suite : P2 (" + why + ")");
    f.step();
    const std::string p2 = f.lastProposalId();
    const int m2 = f.as->messageOfProposal(p2);
    check(m2 >= 0 && p2 != p1, "P2 est arrivee");
    check(f.as->cancelMission(id, &why), "annuler la mission (" + why + ")");
    check(f.mission(id) && f.mission(id)->state == ms::State::Cancelled && f.linkState(id, p2) == "annulee", "annulee ; P2 annulee");
    check(m2 >= 0 && !f.as->apply(m2, &why) && has(why, "annul"), "une proposition d'une mission annulee ne s'applique plus (" + why + ")");
    check(!f.object("Voyant_Debit") && f.doc->project.variable("Debit_Entree"), "ce qui etait applique reste ; rien de plus");
    check(!f.as->continueMission(id, &why) && has(why, "annul"), "plus de suite (" + why + ")");
    check(!f.as->resumeMission(id, &why), "une mission annulee ne reprend pas");
    if (const auto disk = ms::load(id)) check(disk->state == ms::State::Cancelled, "l'annulation est enregistree");
}

// ---- un plan revu : les etapes faites sont gardees -------------------------------------------------------------------------
void testRefresh() {
    std::printf("[actualiser : le plan revu garde le travail fait]\n");
    Fixture f("actualiser");
    std::string why;
    f.answerPlan("toolu_plan", planInput("Pompes", {{"S1", "Cr\xC3\xA9" "er la variable", "variables", {}, 1, {"variable:Pompe_3_Marche"}},
                                                    {"S2", "Cr\xC3\xA9" "er l'alarme", "alarmes", {}, 6, {"alarme:Defaut_P3"}},
                                                    {"S3", "Cr\xC3\xA9" "er le script", "scripts", {}, 6, {"script:Calcul_P3"}}}));
    check(f.as->createMission("Cr\xC3\xA9" "e la pompe 3 compl\xC3\xA8te : variable, alarme, script et vue.", &why), "creer (" + why + ")");
    const std::string id = f.as->missions().empty() ? std::string{} : f.as->missions().front().id;
    f.step();
    Value vars = Value::array();
    vars.push(O({{"nom", "Pompe_3_Marche"}, {"type", "BOOL"}}));
    f.answerOps("toolu_p1", proposalInput("Variable", {O({{"action", "creer_variables"}, {"variables", vars}, {"etape", "S1"}})}));
    check(f.as->continueMission(id, &why), "la suite (" + why + ")");
    f.step();
    const std::string p1 = f.lastProposalId();
    check(f.as->apply(f.as->messageOfProposal(p1), &why) && f.stepState(id, "S1") == ms::StepState::Done, "S1 terminee (" + why + ")");
    // Le projet change a la main ; l'actualisation le dit a l'IA, et l'IA revoit le plan (S3 retiree, S4 ajoutee).
    (void)f.stack.push(hmi::changeView(f.doc, f.view, "Un geste", [](hmi::Project& p, hmi::View& v) { (void)hmi::edit::add(p, v, hmi::Kind::Text, 9, 9); }));
    f.step();
    f.answerPlan("toolu_plan2", planInput("Pompe 3", {{"S1", "Cr\xC3\xA9" "er la variable", "variables", {}, 1, {"variable:Pompe_3_Marche"}},
                                                      {"S2", "Cr\xC3\xA9" "er l'alarme de d\xC3\xA9" "faut", "alarmes", {}, 2, {"alarme:Defaut_P3"}},
                                                      {"S4", "Compl\xC3\xA9ter la vue", "vues", {"S2"}, 3, {"objet:Voyant_P3"}}}));
    check(f.as->refreshMission(id, &why), "actualiser (" + why + ")");
    check(has(f.s.requests.back().toolsJson, "planifier_mission") && has(f.s.requests.back().systemDynamic, "ACTUALISATION")
              && has(f.s.requests.back().systemDynamic, "PLAN ACTUEL"),
          "l'IA relit le projet et recoit le plan actuel");
    f.step();
    const ms::Mission* m = f.mission(id);
    check(m && m->planRevision == 1, "le plan est revu (revision 1)");
    check(f.stepState(id, "S1") == ms::StepState::Done && m && m->step("S1") && listedIn(m->step("S1")->proposals, p1),
          "l'etape terminee est gardee, avec sa proposition");
    check(m && !m->step("S3") && m->step("S4") && listedIn(m->step("S4")->deps, "S2"), "S3 retiree (pas commencee), S4 ajoutee");
    check(m && m->proposals.size() == 1 && m->proposals.front().state == "appliquee", "les operations appliquees sont gardees");
    check(notified(id, "Plan revu"), "notification : plan revu");
    // Un appel de planifier_mission hors d'une planification est refuse (rien n'est enregistre).
    f.s.replies.push_back({textEv("Je replanifie."), toolEv("toolu_x", "planifier_mission", planInput("X", {{"S9", "Rien", "autre", {}, 1, {}}})), doneEv("tool_use")});
    check(f.as->send("Change tout le plan.", &why), "une demande ordinaire (" + why + ")");
    f.step();
    check(f.last().tool && !f.last().tool->error.empty() && f.mission(id) && !f.mission(id)->step("S9"), "planifier_mission hors mission : refuse, rien n'est change");
}

// 1.22.1 : UNE PROPOSITION LOURDE (une operation << adresser >>) - verifiee sur un fil de fond, jamais sur celui de
// l'interface (le blocage de 8 s du 01/11 sous Windows : la validation a l'arrivee de la reponse, puis celle de la mission).
// A l'arrivee : la verification tourne ; le resultat pour le modele dit << en cours >> ; a la fin (au tick) : les comptes,
// et pour une mission, la proposition << validee >> et sa notification - pas avant.
void testHeavyInBackground() {
    std::printf("[verification de fond : une operation adresser]\n");
    Fixture f("lourde");
    hmi::Equipment e;
    e.id = f.doc->project.allocate();
    e.name = "Automate_1";
    e.host = "127.0.0.1";
    e.port = 502;
    f.doc->project.equipments.push_back(e);
    const auto addressOp = [](const char* step) {
        Value op = O({{"action", "adresser"}, {"variable", "Pompe_1_Marche"}, {"equipement", "Automate_1"}, {"adresse", "%M10"}});
        if (step) op.set("etape", std::string(step));
        return op;
    };
    const auto wait = [&f](const std::string& pid) {
        for (int k = 0; k < 1000 && f.as->checkingProposal(pid); ++k) {
            pause(2);
            f.step(0.01);
        }
    };
    std::string why;
    // Une demande ordinaire.
    f.answerOps("toolu_lourde", proposalInput("Adresser la pompe 1", {addressOp(nullptr),
                                                                     O({{"action", "creer"}, {"genre", "Indicator"}, {"nom", "Voyant_Lourd"},
                                                                        {"proprietes", O({{"x", "40"}, {"y", "40"}, {"variable", "Pompe_1_Marche"}})}})}));
    check(f.as->send("Adresse la pompe 1 sur l'automate.", &why), "la demande part (" + why + ")");
    f.step();
    const std::string pid = f.lastProposalId();
    const int mi = f.as->messageOfProposal(pid);
    check(!pid.empty() && mi >= 0, "la proposition est arrivee");
    check(f.as->checkingProposal(pid), "sa verification tourne sur un fil de fond (pas sur celui de l'interface)");
    if (const ai::Proposal* p = mi >= 0 ? f.as->proposal(mi) : nullptr)
        check(p->create == 0, "a l'arrivee : ses comptes ne sont pas encore la (aucune validation sur ce fil)");
    if (mi >= 0) {
        const auto [text, error] = f.as->toolResult(f.as->current()->messages[static_cast<std::size_t>(mi)]);
        check(!error && has(text, "en cours"), "pour le modele, pendant la verification : en attente, verification en cours (" + text + ")");
    }
    wait(pid);
    check(!f.as->checkingProposal(pid), "la verification de fond se termine");
    if (const ai::Proposal* p = mi >= 0 ? f.as->proposal(mi) : nullptr) {
        check(p->create == 1, "ses comptes sont poses a la fin (" + std::to_string(p->create) + " a creer : le voyant)");
        const auto [text, error] = f.as->toolResult(f.as->current()->messages[static_cast<std::size_t>(mi)]);
        check(!error && has(text, "en attente") && !has(text, "en cours"), "pour le modele ensuite : la verification connue (" + text + ")");
        check(f.as->check(mi).ok(), "la proposition est valide");
    }
    // Une mission : la proposition n'est << validee >> (et notifiee) qu'a la fin de sa verification de fond.
    f.answerPlan("toolu_plan_l", planInput("Adresses", {{"S1", "Adresser la pompe 1", "variables", {}, 1, {"variable:Pompe_1_Marche"}}}));
    check(f.as->createMission("Adresse la pompe 1 sur l'automate, en mission.", &why), "creer la mission (" + why + ")");
    f.step();
    const std::string id = f.as->missions().empty() ? std::string{} : f.as->missions().front().id;
    f.answerOps("toolu_m_l", proposalInput("Adresser la pompe 1", {addressOp("S1")}));
    check(f.as->continueMission(id, &why), "proposer la suite (" + why + ")");
    f.step();
    const std::string mp = f.lastProposalId();
    check(!mp.empty() && mp != pid, "la proposition de la mission est arrivee");
    check(f.as->checkingProposal(mp) && f.linkState(id, mp) != "validee", "verifiee sur un fil de fond : pas encore << validee >>");
    check(f.stepState(id, "S1") == ms::StepState::AwaitingValidation, "S1 attend la validation des le debut");
    wait(mp);
    check(f.linkState(id, mp) == "validee", "a la fin de la verification : validee (" + f.linkState(id, mp) + ")");
    check(notified(id, "Proposition disponible"), "notification : proposition disponible");
}


// 1.22.2 : FORCER LA FIN D'UNE MISSION, de bout en bout.
//  A. pendant un appel a l'IA : l'appel est arrete (rien n'est propose ni applique), les etapes non terminees ignorees,
//     la mission terminee (fin forcee), notifiee, enregistree ; l'audit final se fait de lui-meme et son rapport le dit ;
//     il ne la rouvre pas ;
//  B. une proposition en attente : refusee (sa carte aussi), elle ne s'applique plus, le projet n'a pas change ;
//  C. une proposition de la mission qui s'applique sur un fil de fond : refuse tant qu'elle s'applique (et pourquoi).
void testForceFinish() {
    std::printf("[1.22.2 : forcer la fin d'une mission]\n");
    Fixture f("fin_forcee");
    hmi::Equipment eq;   // pour C (une operation << adresser >> : verifiee et appliquee sur un fil de fond)
    eq.id = f.doc->project.allocate();
    eq.name = "Automate_1";
    eq.host = "127.0.0.1";
    eq.port = 502;
    f.doc->project.equipments.push_back(eq);
    const auto missionOther = [&f](const std::vector<std::string>& known) {
        for (const auto& m : f.as->missions())
            if (!listedIn(known, m.id)) return m.id;
        return std::string{};
    };
    std::string why;

    // A. PENDANT UN APPEL A L'IA
    f.answerPlan("toolu_plan_ff", planInput("D\xC3\xA9" "bit de sortie",
                                            {{"S1", "Cr\xC3\xA9" "er la variable", "variables", {}, 1, {"variable:Debit_Sortie"}},
                                             {"S2", "Cr\xC3\xA9" "er l'alarme", "alarmes", {}, 1, {"alarme:Debit_Sortie_Haut"}},
                                             {"S3", "Compl\xC3\xA9ter la vue", "vues", {}, 6, {"objet:Voyant_Sortie"}}}));
    check(f.as->createMission("Cr\xC3\xA9" "e la surveillance compl\xC3\xA8te du d\xC3\xA9" "bit de sortie : variable, alarme et vue.", &why),
          "creer (" + why + ")");
    const std::string id = missionOther({});
    f.step();
    check(f.mission(id) && f.mission(id)->steps.size() == 3, "le plan : trois etapes");
    Value vars = Value::array();
    vars.push(O({{"nom", "Debit_Sortie"}, {"type", "REAL"}}));
    f.answerOps("toolu_ff_p1", proposalInput("La variable", {O({{"action", "creer_variables"}, {"variables", vars}, {"etape", "S1"}})}));
    check(f.as->continueMission(id, &why), "la suite : P1 (" + why + ")");
    f.step();
    const std::string p1 = f.lastProposalId();
    const int m1 = f.as->messageOfProposal(p1);
    check(m1 >= 0 && f.as->apply(m1, &why) && f.stepState(id, "S1") == ms::StepState::Done, "P1 appliquee : S1 terminee (" + why + ")");
    // la suite part, et la reponse ne vient pas : un appel a l'IA en cours
    f.answerOps("toolu_ff_p2", proposalInput("L'alarme", {O({{"action", "creer_alarme"}, {"nom", "Debit_Sortie_Haut"}, {"condition", "Debit_Sortie > 50"},
                                                             {"message", "D\xC3\xA9" "bit haut"}, {"etape", "S2"}})}));
    f.s.hang = true;
    check(f.as->continueMission(id, &why), "la suite part (" + why + ")");
    f.step();
    check(f.as->generating() && f.mission(id) && f.mission(id)->pending.active && f.stepState(id, "S2") == ms::StepState::Running,
          "l'IA prepare la suite (S2 en cours)");
    const std::size_t undo0 = f.stack.done().size();
    const int cancels = f.s.cancels;
    check(f.as->canForceFinish(id, &why), "forcer la fin : permis pendant un appel (" + why + ")");
    check(f.as->forceFinishMission(id, &why), "forcer la fin (" + why + ")");
    f.s.hang = false;
    check(!f.as->generating() && f.s.cancels == cancels + 1 && f.last().state == "arrete", "l'appel a l'IA est vraiment arrete");
    f.step();
    const ms::Mission* m = f.mission(id);
    check(m && m->state == ms::State::Done && m->forced.on() && ms::stateText(*m) == "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)", "terminee (fin forcee)");
    check(m && !m->pending.active && m->proposals.size() == 1, "l'appel efface ; aucune proposition n'est nee de la reponse arretee");
    check(f.stepState(id, "S1") == ms::StepState::Done && f.stepState(id, "S2") == ms::StepState::Skipped && f.stepState(id, "S3") == ms::StepState::Skipped,
          "S1 reste terminee ; S2, S3 ignorees");
    check(m && m->step("S2") && m->step("S2")->note == "Fin forc\xC3\xA9" "e par l'utilisateur.", "la raison : fin forcee par l'utilisateur");
    check(f.stack.done().size() == undo0 && f.doc->project.variable("Debit_Sortie"), "rien n'est applique ni defait dans le projet");
    check(notified(id, "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)"), "notification : terminee (fin forcee)");
    check(!f.as->continueMission(id, &why) && has(why, "termin"), "plus de suite (" + why + ")");
    check(!f.as->forceFinishMission(id, &why) && has(why, "d\xC3\xA9j\xC3\xA0 termin"), "deja terminee : refuse (" + why + ")");
    // LE RAPPORT FINAL, comme pour une fin normale : l'audit global (son projet est ouvert) ; il dit la fin forcee
    check(f.as->auditing(id) || (f.mission(id) && f.mission(id)->finalReport.isObject()), "l'audit final part de lui-meme");
    f.waitAudit(id);
    m = f.mission(id);
    const ms::FinalReport fr = m && m->finalReport.isObject() ? ms::reportFromJson(m->finalReport) : ms::FinalReport{};
    check(m && m->finalReport.isObject() && fr.forced && fr.skipped.size() == 2 && !fr.complete && has(fr.verdict, "fin forc"),
          "le rapport final : fin forcee, 2 etapes ignorees (" + fr.verdict + ")");
    check(m && m->state == ms::State::Done, "l'audit ne la rouvre pas (toujours terminee)");
    if (const auto disk = ms::load(id))
        check(disk->state == ms::State::Done && disk->forced.on() && disk->forced.steps.size() == 2 && disk->finalReport.isObject(),
              "enregistree : la fin forcee et le rapport relus du disque");

    // B. UNE PROPOSITION EN ATTENTE : refusee (sa carte aussi), elle ne s'applique plus
    f.as->newConversation();
    f.answerPlan("toolu_plan_ff2", planInput("Pression", {{"S1", "Cr\xC3\xA9" "er la variable", "variables", {}, 1, {"variable:Pression_Sortie"}},
                                                          {"S2", "Cr\xC3\xA9" "er l'alarme", "alarmes", {}, 1, {"alarme:Pression_Haute"}}}));
    check(f.as->createMission("Cr\xC3\xA9" "e la surveillance compl\xC3\xA8te de la pression de sortie : variable et alarme.", &why),
          "une deuxieme mission (" + why + ")");
    const std::string id2 = missionOther({id});
    f.step();
    Value vars2 = Value::array();
    vars2.push(O({{"nom", "Pression_Sortie"}, {"type", "REAL"}}));
    f.answerOps("toolu_ff2_p1", proposalInput("La variable", {O({{"action", "creer_variables"}, {"variables", vars2}, {"etape", "S1"}})}));
    check(f.as->continueMission(id2, &why), "la suite (" + why + ")");
    f.step();
    const std::string q1 = f.lastProposalId();
    const int qm = f.as->messageOfProposal(q1);
    check(qm >= 0 && f.mission(id2) && ms::awaitingProposals(*f.mission(id2)) == std::vector<std::string>{q1}, "P1 attend une decision");
    const std::size_t undo1 = f.stack.done().size();
    check(f.as->forceFinishMission(id2, &why), "forcer la fin de la deuxieme (" + why + ")");
    check(f.linkState(id2, q1) == "refusee", "sa proposition en attente : refusee (jamais appliquee)");
    const ai::Proposal* card = qm >= 0 ? f.as->proposal(qm) : nullptr;
    check(card && card->state == "refusee", "sa carte dans la conversation aussi");
    check(qm >= 0 && !f.as->apply(qm, &why) && f.stack.done().size() == undo1 && !f.doc->project.variable("Pression_Sortie"),
          "elle ne s'applique plus ; le projet n'a pas change (" + why + ")");
    check(f.stepState(id2, "S1") == ms::StepState::Skipped && f.stepState(id2, "S2") == ms::StepState::Skipped, "ses deux etapes ignorees");
    f.waitAudit(id2);

    // C. UNE PROPOSITION QUI S'APPLIQUE SUR UN FIL DE FOND : la fin ne se force pas tant qu'elle s'applique
    f.as->newConversation();
    f.answerPlan("toolu_plan_ff3", planInput("Adresses", {{"S1", "Adresser la pompe 1", "variables", {}, 1, {"variable:Pompe_1_Marche"}}}));
    check(f.as->createMission("Adresse la pompe 1 sur l'automate, en mission compl\xC3\xA8te.", &why), "une troisieme mission (" + why + ")");
    const std::string id3 = missionOther({id, id2});
    f.step();
    f.answerOps("toolu_ff3_p1", proposalInput("Adresser la pompe 1", {O({{"action", "adresser"}, {"variable", "Pompe_1_Marche"}, {"equipement", "Automate_1"},
                                                                         {"adresse", "%M10"}, {"etape", "S1"}})}));
    check(f.as->continueMission(id3, &why), "la suite (" + why + ")");
    f.step();
    const std::string r1 = f.lastProposalId();
    for (int k = 0; k < 1000 && f.as->checkingProposal(r1); ++k) {
        pause(2);
        f.step(0.01);
    }
    const int rm = f.as->messageOfProposal(r1);
    check(rm >= 0 && f.as->applyInBackground(rm, false, &why) && f.as->applyingAny(), "elle s'applique sur un fil de fond (" + why + ")");
    check(!f.as->canForceFinish(id3, &why) && has(why, "arri\xC3\xA8re-plan"), "pendant l'application : refuse, et pourquoi (" + why + ")");
    check(!f.as->forceFinishMission(id3, &why) && f.mission(id3) && f.mission(id3)->state != ms::State::Done && !f.mission(id3)->forced.on(),
          "rien n'est change");
    for (int k = 0; k < 2500 && f.as->applyingAny(); ++k) {
        pause(2);
        f.step(0.01);
    }
    f.step();
    check(!f.as->applyingAny(), "l'application de fond est finie (son compte rendu est arrive a la mission)");
    check(f.as->canForceFinish(id3, &why), "ensuite : permis (" + why + ")");
    check(f.as->forceFinishMission(id3, &why) && f.mission(id3) && f.mission(id3)->forced.on() && f.mission(id3)->state == ms::State::Done,
          "ensuite : la fin se force (" + why + ")");
    if (f.stepState(id3, "S1") == ms::StepState::Done)
        check(f.mission(id3) && f.mission(id3)->forced.steps.empty(), "S1 terminee par l'application de fond : aucune etape a ignorer");
    f.waitAudit(id3);
}

} // namespace

int main() {
    ai::setClock([] { return fixedNow(); });
    ai::notifs::resetForTests();
    testPlanSchema();
    testStation();
    testErrorAndCorrection();
    testPauseResumeCancel();
    testRefresh();
    testHeavyInBackground();   // 1.22.1
    testForceFinish();         // 1.22.2
    std::printf("ai_missions_e2e_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
