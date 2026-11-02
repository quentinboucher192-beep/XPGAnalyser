// =============================================================================
//  tests/ai_jetons_test.cpp - 1.22.6 : CE QUI PART AU FOURNISSEUR, MESURE (les jetons), avant / apres
// -----------------------------------------------------------------------------
//  Un banc SANS RESEAU : l'Assistant (AiAssistant) branche sur un faux fournisseur
//  qui retient les requetes, sur un vrai document IHM (un projet moyen : la vue
//  ouverte, 300 variables rangees, des types IHM, d'autres vues). Pour chaque cas,
//  la requete construite (ProviderRequest : les consignes, le contexte, les outils,
//  chaque tour, les pieces jointes, les retours d'outils) est mise dans le CORPS
//  JSON exact qu'Anthropic recevrait (anthropicBody, les outils locaux ajoutes comme
//  le fait le fournisseur), puis comptee :
//    - en octets (UTF-8) et en JETONS ESTIMES : octets / 3,5 (du francais et du
//      JSON : 3 a 4 octets par jeton ; AiTextParts garde 2,5 pour ses budgets,
//      prudent - ici on compare avant et apres, l'estimateur est le meme) ;
//    - par partie : les outils, les consignes stables, le contexte (vue, variables,
//      automate, pieces...), l'historique (les tours d'avant), le dernier tour ;
//    - LE CACHE, simule a la maniere d'Anthropic : les blocs dans l'ordre (outils,
//      systeme, messages) ; un point de cache (cache_control) ecrit le prefixe qui
//      finit a son bloc ; une requete relit le plus long prefixe ecrit par la
//      requete precedente du meme fil, cherche a ses points de cache et jusqu'a 20
//      blocs avant chacun (le << lookback >> du service) ; un prefixe de moins de
//      1 024 jetons n'est pas mis en cache. D'ou : lus (0,1 x le prix), ecrits
//      (1,25 x), pleins (1 x) et le COUT EQUIVALENT = pleins + 1,25 x ecrits + 0,1 x lus.
//  Les cas : (1) la premiere demande sur un projet moyen ; (2) le 10e tour d'une
//  conversation avec 5 propositions de 40 operations (dont 3 appliquees), le 9e tour
//  applique juste avant ; (3) une proposition de mission (mission::batchPrompt : P2,
//  apres P1 appliquee) ; (4) une correction automatique (l'avis du logiciel, 1.22.5) ;
//  (5) le 2e tour d'une conversation avec un .XPG lu (le contexte de l'automate : 891
//  variables, 28 DDT). Les cas (2) a (5) sont compares a la requete d'avant du meme fil
//  (le cache) ; (1) part a froid.
//  AVANT : les chiffres de la 1.22.5 (8ae5f5d), mesures par ce banc et gardes ici ;
//  APRES : mesures a chaque passage, sous un PLAFOND (une regression de taille fait
//  echouer l'essai). Le tableau avant / apres est affiche. Ce qui a fait le gain (1.22.6) :
//  le contexte du projet dans le dernier tour et les points de cache de l'historique
//  (AiAnthropic, AiOpenAI) ; l'historique resume par paquets (Assistant::requestFor) ;
//  les natives et les variables systeme plus evoquees par des mots courants (AiContext).
//  XPG_JETONS_DUMP=<dossier> : les corps mesures y sont ecrits.
// =============================================================================
#include "../src/app/ai/AiAnthropic.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiBuild.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiMission.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiTextParts.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
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

// ---- l'estimateur ------------------------------------------------------------------------------------------------------
constexpr double kBytesPerToken = 3.5;   // du francais et du JSON (UTF-8) : 3 a 4 octets par jeton
long long tokens(std::size_t bytes) { return static_cast<long long>(std::llround(static_cast<double>(bytes) / kBytesPerToken)); }

// ---- le faux fournisseur ---------------------------------------------------------------------------------------------
struct FakeScript {
    std::vector<ai::ProviderRequest>           requests;
    std::deque<std::vector<ai::ProviderEvent>> replies;
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
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override { busy_ = false; }
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
std::string proposalInput(const std::string& resume, const std::vector<Value>& ops, const std::string& suite = {}) {
    Value list = Value::array();
    for (const auto& o : ops) list.push(o);
    Value in = O({{"resume", resume}, {"operations", list}});
    if (!suite.empty()) in.set("suite", suite);
    return ai::json::write(in, false);
}

// ---- le projet moyen ---------------------------------------------------------------------------------------------------
// La vue ouverte (30 objets, relies), 300 variables rangees (5 zones x 10 pompes x 6), 10 types IHM, 8 autres vues.
std::string varName(int z, int p, const char* what) {
    char b[64];
    std::snprintf(b, sizeof b, "Z%d_P%02d_%s", z, p, what);
    return b;
}
void buildProject(hmi::Project& p, hmi::Id& view) {
    hmi::View v = hmi::makeView(p, "Vue_Station");
    v.width = 1280;
    v.height = 800;
    const hmi::Id layer = v.layers.empty() ? hmi::kNoId : v.layers.front().id;
    for (int i = 0; i < 30; ++i) {
        const int z = 1 + i / 10, n = 1 + i % 10;
        const hmi::Kind k = i % 3 == 0 ? hmi::Kind::Pump : i % 3 == 1 ? hmi::Kind::Indicator : hmi::Kind::NumericDisplay;
        const std::string name = (k == hmi::Kind::Pump ? "Pompe_" : k == hmi::Kind::Indicator ? "Voyant_" : "Mesure_") + std::to_string(z) + "_" + std::to_string(n);
        hmi::Object o = hmi::makeObject(k, p.allocate(), name, 40.0 + 120.0 * (i % 10), 60.0 + 160.0 * (i / 10), layer);
        o.set("variable", varName(z, n, k == hmi::Kind::NumericDisplay ? "Vitesse" : "Marche"));
        if (k != hmi::Kind::Pump) o.set("label", "P" + std::to_string(z) + std::to_string(n));
        v.objects.push_back(std::move(o));
    }
    p.views.push_back(v);
    view = p.views.front().id;
    for (int k = 1; k <= 8; ++k) p.views.push_back(hmi::makeView(p, "Vue_Zone_" + std::to_string(k)));
    static const std::pair<const char*, const char*> kinds[] = {{"Marche", "BOOL"},  {"Defaut", "BOOL"},  {"Vitesse", "REAL"},
                                                               {"Heures", "DINT"},   {"Consigne", "REAL"}, {"Mode", "INT"}};
    for (int z = 1; z <= 5; ++z)
        for (int n = 1; n <= 10; ++n)
            for (const auto& [what, type] : kinds) {
                hmi::Variable var;
                var.id = p.allocate();
                var.name = varName(z, n, what);
                var.type = type;
                var.description = std::string(what) + " de la pompe " + std::to_string(n) + " de la zone " + std::to_string(z);
                var.folder = "Station/Zone " + std::to_string(z) + "/P" + std::to_string(n);
                p.programs.variables.push_back(var);
            }
    for (int t = 1; t <= 10; ++t) {
        hmi::HmiType ty;
        ty.id = p.allocate();
        ty.name = "T_Equipement_" + std::to_string(t);
        ty.description = "Un \xC3\xA9quipement de la station";
        for (const char* m : {"Marche", "Defaut", "Mesure", "Consigne"}) {
            hmi::TypeMember mem;
            mem.name = m;
            mem.type = std::string(m) == "Marche" || std::string(m) == "Defaut" ? "BOOL" : "REAL";
            ty.members.push_back(mem);
        }
        p.programs.types.push_back(ty);
    }
    p.config.created = "2026-10-01T08:00:00";
}

// L'automate d'un .XPG lu : 891 variables, 28 DDT.
std::vector<std::pair<std::string, std::string>> plcVariables() {
    std::vector<std::pair<std::string, std::string>> out;
    for (int i = 0; i < 891; ++i) {
        char b[64];
        std::snprintf(b, sizeof b, "%s_%03d", i % 3 == 0 ? "Moteur" : i % 3 == 1 ? "Vanne" : "Capteur", i);
        out.emplace_back(b, i % 3 == 2 ? "REAL" : i % 5 == 0 ? "T_Moteur" : "EBOOL");
    }
    return out;
}
std::vector<ai::Env::PlcType> plcTypes() {
    std::vector<ai::Env::PlcType> out;
    for (int t = 0; t < 28; ++t) {
        ai::Env::PlcType ty;
        ty.name = "T_Ddt_" + std::to_string(t);
        ty.comment = "Structure d'\xC3\xA9quipement";
        for (const char* m : {"Cmd", "Etat", "Defaut", "Mesure", "Seuil"})
            ty.members.push_back({m, std::string(m) == "Mesure" || std::string(m) == "Seuil" ? "REAL" : "BOOL", "membre"});
        out.push_back(std::move(ty));
    }
    return out;
}

// ---- l'Assistant sur le projet moyen ----------------------------------------------------------------------------------
struct Bench {
    fs::path                       dir;
    FakeScript                     s;
    hmi::DocumentPtr               doc = std::make_shared<hmi::Document>();
    core::CommandStack             stack;
    hmi::Id                        view{hmi::kNoId};
    std::unique_ptr<ai::Assistant> as;
    double                         t{100.0};
    std::string                    plcName;

    // `before` : ecrit dans IA/ avant que l'Assistant ne relise (une conversation deja longue).
    explicit Bench(const std::string& name, const std::function<void(Bench&)>& before = {}) {
        dir = fs::temp_directory_path() / ("xpg_ai_jetons_" + name);
        fs::remove_all(dir);
        fs::create_directories(dir / "Station");
        ai::setRoot(U(dir / "IA"));
        buildProject(doc->project, view);
        if (before) before(*this);
        ai::Assistant::Host h;
        h.document = [this] { return doc; };
        h.activeView = [this] { return view; };
        h.selection = [] { return std::vector<hmi::Id>{}; };
        h.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        h.projectName = [] { return std::string("Station"); };
        h.projectPath = [this] { return U(dir / "Station"); };
        h.readPlc = [this](const std::string& path, std::string* said) {
            const auto u8 = fs::path(std::u8string(path.begin(), path.end())).filename().u8string();
            plcName.assign(u8.begin(), u8.end());
            if (said) *said = "891 variables globales et 28 types (DDT)";
            return true;
        };
        h.env.plcSource = [this] { return plcName; };
        h.env.plcVariables = [this] { return plcName.empty() ? std::vector<std::pair<std::string, std::string>>{} : plcVariables(); };
        h.env.plcTypes = [this] { return plcName.empty() ? std::vector<ai::Env::PlcType>{} : plcTypes(); };
        as = std::make_unique<ai::Assistant>(std::move(h));
        as->setProviderFactory([this](std::string_view) { return std::make_unique<FakeProvider>(s); });
        as->load();
        ai::Settings st = as->settings();
        st.provider = "anthropic";
        st.model = "modele-essai";
        st.applyWithErrors = true;
        st.localFirst = false;
        st.buildCheck = false;
        as->setSettings(st);
        as->setApiKey("cle-essai");
    }
    void step(double dt = 0.05) {
        t += dt;
        (void)as->tick(t);
    }
    void waitBuilds() {
        for (int k = 0; k < 2500 && as->buildingAny(); ++k) {
            step(0.002);
            pause(2);
        }
        step(0.002);
    }
    [[nodiscard]] int lastIndex() const { return static_cast<int>(as->current()->messages.size()) - 1; }
    void reply(const std::string& text) { s.replies.push_back({textEv(text), doneEv("end_turn")}); }
    void replyOps(const std::string& id, const std::string& input) {
        s.replies.push_back({textEv("Voici la proposition."), toolEv(id, "proposer_operations", input), doneEv("tool_use")});
    }
};

// ---- la mesure --------------------------------------------------------------------------------------------------------
// Le corps tel qu'Anthropic le recoit : les outils locaux ajoutes comme le fait le fournisseur (AiAnthropic : run).
std::string bodyOf(ai::ProviderRequest r) {
    for (const auto& t : r.localTools) r.toolsJson = ai::textparts::withTool(r.toolsJson, t.definitionJson);
    return ai::anthropicBody(r);
}

struct Block {
    std::string key;     // le bloc, sans son cache_control
    std::size_t bytes{0};
    bool        mark{false};
};
std::vector<Block> blocksOf(const std::string& body) {
    std::vector<Block> out;
    Value v;
    if (!ai::json::parse(body, v) || !v.isObject()) return out;
    const auto add = [&](Value b) {
        Block k;
        k.mark = b.isObject() && b.has("cache_control");
        if (b.isObject()) (void)b.erase("cache_control");
        k.key = ai::json::write(b, false);
        k.bytes = k.key.size();
        out.push_back(std::move(k));
    };
    for (const auto& tool : v.at("tools").items()) add(tool);
    const Value& sys = v.at("system");
    if (sys.isString()) add(sys);
    else
        for (const auto& b : sys.items()) add(b);
    for (const auto& m : v.at("messages").items()) {
        const Value& c = m.at("content");
        if (c.isString()) add(c);
        else
            for (const auto& b : c.items()) add(b);
    }
    return out;
}

struct Mesure {
    long long total{0};
    long long outils{0}, consignes{0}, contexte{0}, historique{0}, dernier{0};
    long long lus{0}, ecrits{0}, pleins{0};
    double    cout{0};
};

constexpr long long kMinCached = 1024;   // jetons : un prefixe plus court n'est pas mis en cache

// XPG_JETONS_DUMP=<dossier> : chaque corps mesure y est ecrit (le relire, chercher ce qui pese).
void dump(const std::string& body) {
    static int n = 0;
    const char* d = std::getenv("XPG_JETONS_DUMP");
    if (!d || !*d) return;
    std::ofstream o(fs::path(d) / ("corps" + std::to_string(++n) + ".json"), std::ios::binary);
    o << body;
}

Mesure measure(const ai::ProviderRequest& r, const ai::ProviderRequest* prev) {
    Mesure m;
    const std::string body = bodyOf(r);
    dump(body);
    m.total = tokens(body.size());
    // les parties
    std::string tools = r.toolsJson;
    for (const auto& t : r.localTools) tools = ai::textparts::withTool(tools, t.definitionJson);
    m.outils = tokens(tools.size());
    m.consignes = tokens(r.systemStable.size());
    m.contexte = tokens(r.systemDynamic.size());
    Value v;
    std::size_t lastBytes = 0, histBytes = 0;
    bool contextInSystem = false;
    if (ai::json::parse(body, v) && v.isObject()) {
        const Value& sys = v.at("system");
        contextInSystem = sys.isArray() && sys.items().size() > 1;
        const auto& msgs = v.at("messages").items();
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            const std::size_t b = ai::json::write(msgs[i], false).size();
            (i + 1 == msgs.size() ? lastBytes : histBytes) += b;
        }
    }
    m.historique = tokens(histBytes);
    const long long last = tokens(lastBytes);
    m.dernier = contextInSystem ? last : std::max<long long>(0, last - m.contexte);
    // le cache
    const std::vector<Block> cur = blocksOf(body);
    std::vector<std::size_t> hashes(cur.size()), cum(cur.size());
    {
        std::size_t h = 0, c = 0;
        for (std::size_t i = 0; i < cur.size(); ++i) {
            h = std::hash<std::string>{}(std::to_string(h) + "|" + cur[i].key);
            c += cur[i].bytes;
            hashes[i] = h;
            cum[i] = c;
        }
    }
    std::set<std::size_t> written;
    if (prev) {
        const std::vector<Block> before = blocksOf(bodyOf(*prev));
        std::size_t h = 0, c = 0;
        for (const auto& b : before) {
            h = std::hash<std::string>{}(std::to_string(h) + "|" + b.key);
            c += b.bytes;
            if (b.mark && tokens(c) >= kMinCached) written.insert(h);
        }
    }
    long long hit = -1, lastMark = -1;
    for (std::size_t i = 0; i < cur.size(); ++i) {
        if (!cur[i].mark || tokens(cum[i]) < kMinCached) continue;
        lastMark = static_cast<long long>(i);
        for (long long j = static_cast<long long>(i); j >= 0 && j >= static_cast<long long>(i) - 20; --j)
            if (written.count(hashes[static_cast<std::size_t>(j)])) {
                hit = std::max(hit, j);
                break;
            }
    }
    const std::size_t readBytes = hit >= 0 ? cum[static_cast<std::size_t>(hit)] : 0;
    const std::size_t markBytes = lastMark >= 0 ? cum[static_cast<std::size_t>(lastMark)] : 0;
    m.lus = tokens(readBytes);
    m.ecrits = std::max<long long>(0, tokens(markBytes) - m.lus);
    m.pleins = std::max<long long>(0, m.total - m.lus - m.ecrits);
    m.cout = static_cast<double>(m.pleins) + 1.25 * static_cast<double>(m.ecrits) + 0.1 * static_cast<double>(m.lus);
    return m;
}

void print(const char* name, const Mesure& m) {
    std::printf("  %-34s total %6lld | outils %5lld  consignes %5lld  contexte %5lld  historique %6lld  dernier %5lld | cache lus %6lld  ecrits %6lld  "
                "pleins %6lld | cout %8.0f\n",
                name, m.total, m.outils, m.consignes, m.contexte, m.historique, m.dernier, m.lus, m.ecrits, m.pleins, m.cout);
}

// ---- les cas -----------------------------------------------------------------------------------------------------------
// (1) la premiere demande sur le projet moyen
Mesure case1() {
    Bench b("premiere");
    std::string why;
    b.as->newConversation();
    b.reply("D'accord.");
    check(b.as->send("Ajoute une cuve de 5000 L et deux pompes sur la vue, reli\xC3\xA9" "es aux variables de la zone 2.", &why), "(1) envoyer (" + why + ")");
    check(b.s.requests.size() == 1, "(1) une requete");
    b.step();
    if (b.s.requests.empty()) return {};
    const ai::ProviderRequest& r = b.s.requests.back();
    // 1.22.6 : pas de natives ni de variables systeme pour des mots de tous les jours (ajoute, variables, zone, reliees)
    check(!has(r.systemDynamic, "Fonctions natives que la demande") && !has(r.systemDynamic, "SYS.VariableCount"),
          "(1) le contexte : pas de natives ni de variables systeme evoquees par des mots courants");
    check(r.contextTurn == 0, "(1) le contexte part dans le tour de la demande");
    return measure(r, nullptr);
}

// 40 operations : des voyants et des afficheurs relies (une grosse proposition), la k-ieme.
std::string bigInput(int k) {
    std::vector<Value> ops;
    for (int i = 0; i < 40; ++i) {
        const int z = 1 + (k + i) % 5, n = 1 + i % 10;
        const bool lamp = i % 2 == 0;
        const std::string name = std::string(lamp ? "Voyant_L" : "Afficheur_L") + std::to_string(k) + "_" + std::to_string(i);
        ops.push_back(O({{"action", "creer"},
                         {"genre", lamp ? "Indicator" : "NumericDisplay"},
                         {"nom", name},
                         {"proprietes", O({{"x", std::to_string(20 + 60 * (i % 20))}, {"y", std::to_string(300 + 40 * k + 20 * (i / 20))},
                                           {"variable", varName(z, n, lamp ? "Marche" : "Vitesse")}, {"label", "L" + std::to_string(k) + "." + std::to_string(i)}})}}));
    }
    return proposalInput("Lot " + std::to_string(k) + " de la supervision", ops, k < 5 ? "Le lot " + std::to_string(k + 1) + " de la supervision." : std::string{});
}

// (2) le 10e tour : 8 tours deja dans IA/conversations (5 propositions de 40 operations, dont 3 appliquees), le 9e (une
// petite proposition, appliquee), puis le 10e.
Mesure case2(Mesure* turn9) {
    std::string convId;
    Bench b("dixieme", [&convId](Bench& bb) {
        ai::Conversation c;
        c.id = "conv-jetons-10";
        c.title = "La supervision de la station";
        c.created = c.modified = "2026-11-01T10:00:00";
        c.project.name = "Station";
        c.project.view = "Vue_Station";
        ai::PromptInput in;
        in.project = &bb.doc->project;
        in.view = bb.doc->project.view(bb.view);
        int n = 0;
        const auto user = [&](const std::string& text) {
            ai::Message m;
            m.id = "m" + std::to_string(++n);
            m.role = "utilisateur";
            m.date = "2026-11-01T10:00:00";
            m.text = text;
            c.messages.push_back(std::move(m));
        };
        const auto answer = [&](const std::string& text) {
            ai::Message m;
            m.id = "m" + std::to_string(++n);
            m.role = "assistant";
            m.date = "2026-11-01T10:00:00";
            m.state = "fini";
            m.provider = "anthropic";
            m.model = "modele-essai";
            m.text = text;
            c.messages.push_back(std::move(m));
        };
        const char* states[] = {"appliquee", "appliquee", "refusee", "appliquee", "refusee"};
        for (int k = 1; k <= 5; ++k) {
            user(k == 1 ? std::string("Pose les voyants et les afficheurs de toutes les pompes, par lots.")
                        : "Propose la suite : le lot " + std::to_string(k) + " de la supervision.");
            answer("Je pose le lot " + std::to_string(k) + " : 20 voyants de marche et 20 afficheurs de vitesse, align\xC3\xA9s sous les pompes.");
            const std::string input = bigInput(k);
            std::string why;
            auto p = ai::proposalFromTool(input, in, c.messages[c.messages.size() - 2].text, &why);
            check(p.has_value(), "(2) la proposition " + std::to_string(k) + " se lit (" + why + ")");
            ai::ToolCall tc;
            tc.id = "toolu_lot" + std::to_string(k);
            tc.name = "proposer_operations";
            tc.input = input;
            c.messages.back().tool = tc;
            if (p) {
                p->state = states[k - 1];
                p->fingerprint.clear();
                c.messages.back().proposal = std::move(*p);
            }
            if (std::string(states[k - 1]) == "appliquee")   // ses objets sont dans la vue
                if (hmi::View* v = bb.doc->project.view(bb.view))
                    for (const auto& op : c.messages.back().proposal->ops) {
                        auto kind = hmi::kindFromKey(op.kind);
                        hmi::Object o = hmi::makeObject(kind ? *kind : hmi::Kind::Indicator, bb.doc->project.allocate(), op.name, 20, 300,
                                                        v->layers.empty() ? hmi::kNoId : v->layers.front().id);
                        for (const auto& [key, val] : op.props) o.set(key, val);
                        v->objects.push_back(std::move(o));
                    }
            if (k == 2 || k == 3 || k == 4) {
                user(k == 2 ? "Pourquoi les afficheurs de la zone 3 sont-ils plus hauts ?" : k == 3 ? "Combien de voyants restent \xC3\xA0 poser ?" : "Les libell\xC3\xA9s sont-ils lisibles ?");
                answer(k == 2 ? "Ils suivent la rang\xC3\xA9" "e des pompes de la zone 3, plac\xC3\xA9" "e plus bas sur la vue."
                       : k == 3 ? "Il en reste 60 : les lots 4 et 5." : "Oui : 11 px au moins, sur le fond sombre de la vue.");
            }
        }
        // Les empreintes d'apres Appliquer : la derniere appliquee (P4) est intacte ; P1 et P2, la vue a change depuis.
        for (auto& msg : c.messages)
            if (msg.proposal && msg.proposal->state == "appliquee")
                msg.proposal->fingerprintAfter = msg.tool && msg.tool->id == "toolu_lot4" ? ai::proposalPrint(bb.doc->project, *msg.proposal) : std::string("avant");
        check(c.messages.size() == 16, "(2) 16 messages deja dans la conversation");
        std::string why;
        check(ai::saveConversation(c, &why), "(2) la conversation est ecrite (" + why + ")");
        convId = c.id;
    });
    std::string why;
    check(b.as->select(convId), "(2) la conversation se rouvre");
    // le 9e tour : une petite proposition, appliquee
    b.replyOps("toolu_9", proposalInput("Le titre de la vue", {O({{"action", "creer"}, {"genre", "Text"}, {"nom", "Titre_Station"},
                                                                   {"proprietes", O({{"x", "20"}, {"y", "10"}, {"text", "Station"}})}})}));
    check(b.as->send("Ajoute le titre de la vue en haut \xC3\xA0 gauche.", &why), "(2) le 9e tour (" + why + ")");
    b.step();
    const int m9 = b.lastIndex();
    check(b.as->apply(m9, &why), "(2) le 9e tour s'applique (" + why + ")");
    b.step();
    b.reply("D'accord.");
    check(b.as->send("Maintenant, aligne les afficheurs du lot 4 sur ceux du lot 2.", &why), "(2) le 10e tour (" + why + ")");
    b.step();
    check(b.s.requests.size() == 2, "(2) deux requetes (9e, 10e)");
    if (b.s.requests.size() < 2) return {};
    {
        const std::string body = bodyOf(b.s.requests[1]);
        Value v;
        check(ai::json::parse(body, v) && v.at("system").items().size() == 1, "(2) system : les consignes seules (le contexte est dans le dernier tour)");
        check(!has(body, "\"toolu_lot1\"") && !has(body, "\"toolu_lot3\"") && has(body, "R\xC3\xA9sum\xC3\xA9 du logiciel") && has(body, "Voyant_L1_0")
                  && has(body, "appliqu\xC3\xA9" "e au projet") && has(body, "refus\xC3\xA9" "e par l'utilisateur"),
              "(2) les propositions d'avant, reglees : resumees (leurs noms, leur sort), leur JSON n'est plus renvoye");
        check(has(body, "\"toolu_lot4\"") && has(body, "\"toolu_lot5\"") && has(body, "\"toolu_9\""), "(2) les derniers appels partent entiers");
        check(!has(body, "la vue a chang\xC3\xA9 depuis"), "(2) l'historique ne dit plus << la vue a change depuis >> (il ne bouge plus)");
    }
    if (turn9) *turn9 = measure(b.s.requests[0], nullptr);
    return measure(b.s.requests[1], &b.s.requests[0]);
}

std::string planInput() {
    Value list = Value::array();
    const auto step = [&](const char* id, const char* title, const char* cat, std::vector<std::string> deps, int ops) {
        list.push(O({{"id", id}, {"titre", title}, {"detail", std::string("D\xC3\xA9tail de ") + id}, {"categorie", cat}, {"depend_de", strs(deps)},
                     {"operations", ops}, {"obligatoire", true}, {"produit", strs({})}}));
    };
    step("S1", "Cr\xC3\xA9" "er les variables de la zone 6", "variables", {}, 4);
    step("S2", "Cr\xC3\xA9" "er les alarmes de la zone 6", "alarmes", {"S1"}, 4);
    step("S3", "Cr\xC3\xA9" "er la vue de la zone 6", "vues", {"S1"}, 8);
    step("S4", "Relier la navigation", "vues", {"S3"}, 2);
    Value analysis = O({{"conforme", strs({})}, {"a_modifier", Value::array()}, {"a_completer", Value::array()}, {"a_creer", strs({"vue:Vue_Zone_6"})},
                        {"impossible", Value::array()}, {"resume", "Le projet a 5 zones ; la 6e est \xC3\xA0 cr\xC3\xA9" "er."}});
    return ai::json::write(O({{"nom", "Zone 6"}, {"description", "La zone 6 compl\xC3\xA8te."}, {"analyse", analysis}, {"etapes", list}}), false);
}

// (3) une proposition de mission (mission::batchPrompt) : la mission creee, son plan recu, P1 appliquee, P2 demandee (le
// regime de croisiere d'une mission ; P1 suit le plan, qui a l'outil planifier_mission en plus).
std::string zoneVariables() {
    Value vars = Value::array();
    for (const char* n : {"Z6_Marche", "Z6_Defaut", "Z6_Niveau", "Z6_Consigne"})
        vars.push(O({{"nom", n}, {"type", std::string(n) == "Z6_Niveau" || std::string(n) == "Z6_Consigne" ? "REAL" : "BOOL"}, {"dossier", "Station/Zone 6"}}));
    return proposalInput("Les variables de la zone 6", {O({{"action", "creer_variables"}, {"variables", vars}, {"etape", "S1"}})},
                         "Les alarmes et la vue de la zone 6 (S2, S3).");
}
Mesure case3() {
    Bench b("mission");
    std::string why;
    b.as->newConversation();
    b.s.replies.push_back({textEv("J'ai analys\xC3\xA9 le projet r\xC3\xA9" "el."), toolEv("toolu_plan", "planifier_mission", planInput()), doneEv("tool_use")});
    check(b.as->createMission("Cr\xC3\xA9" "e la zone 6 compl\xC3\xA8te : les variables, les alarmes, la vue et la navigation.", &why), "(3) la mission (" + why + ")");
    b.step();
    const std::string id = b.as->missions().empty() ? std::string{} : b.as->missions().front().id;
    b.replyOps("toolu_p1", zoneVariables());
    check(b.as->continueMission(id, &why), "(3) P1 demandee (" + why + ")");
    b.step();
    check(b.as->apply(b.lastIndex(), &why), "(3) P1 appliquee (" + why + ")");
    b.step();
    b.step(0.3);
    b.reply("D'accord.");
    check(b.as->continueMission(id, &why), "(3) P2 demandee (" + why + ")");
    check(b.s.requests.size() == 3, "(3) trois requetes (plan, P1, P2)");
    b.step();
    return b.s.requests.size() < 3 ? Mesure{} : measure(b.s.requests[2], &b.s.requests[1]);
}

// (4) une correction automatique : Generer et Compiler trouvent une erreur, l'IA corrige seule.
ai::build::Verdict fakeVerdict(const hmi::Project&, const ai::Proposal& p) {
    ai::build::Verdict v;
    v.ran = true;
    v.ms = 900.0;
    for (std::size_t i = 0; i < p.ops.size(); ++i)
        for (const auto& [key, val] : p.ops[i].props) {
            if (key != "label" || val.rfind("Mauvais", 0) != 0) continue;
            ai::build::Finding f;
            f.step = "Compiler";
            f.category = "Expression";
            f.where = "Vue_Station . " + p.ops[i].name + " . label";
            f.view = "Vue_Station";
            f.objectName = p.ops[i].name;
            f.property = "label";
            f.op = i;
            f.error = true;
            f.message = "valeur refus\xC3\xA9" "e : " + val;
            v.findings.push_back(f);
            ++v.errors;
        }
    return v;
}
std::string cuveInput(const std::string& label) {
    return proposalInput("La cuve de pression", {O({{"action", "creer"}, {"genre", "Tank"}, {"nom", "Cuve_P"},
                                                    {"proprietes", O({{"x", "300"}, {"y", "40"}, {"w", "120"}, {"h", "160"}, {"label", label}})}})});
}
Mesure case4() {
    ai::build::setVerdictForTests(fakeVerdict);
    Mesure out;
    {
        Bench b("correction");
        ai::Settings st = b.as->settings();
        st.buildCheck = true;
        st.autoCorrect = true;
        b.as->setSettings(st);
        std::string why;
        b.as->newConversation();
        b.replyOps("toolu_c1", cuveInput("Mauvais"));
        b.replyOps("toolu_c2", cuveInput("Bon"));
        check(b.as->send("Ajoute une cuve de pression.", &why), "(4) envoyer (" + why + ")");
        b.step();
        b.waitBuilds();
        check(b.s.requests.size() >= 2 && has(b.s.requests[1].turns.back().text, "1 sur 2"), "(4) la correction automatique est partie");
        if (b.s.requests.size() >= 2) out = measure(b.s.requests[1], &b.s.requests[0]);
        b.step();
        b.waitBuilds();
    }
    ai::build::setVerdictForTests(nullptr);
    return out;
}

// (5) une conversation avec un .XPG lu : le 2e tour (l'automate de la seance dans le contexte).
Mesure case5() {
    Bench b("xpg");
    std::string why;
    b.as->newConversation();
    const fs::path xpg = b.dir / "programme.XPG";
    {
        std::ofstream o(xpg);
        o << "<?xml version=\"1.0\"?><FBExchangeFile></FBExchangeFile>\n";
    }
    check(b.as->attach(U(xpg), &why), "(5) joindre programme.XPG (" + why + ")");
    for (int k = 0; k < 500 && b.as->processing(); ++k) {
        pause(2);
        b.step(0.01);
    }
    b.reply("L'automate a 891 variables : 297 moteurs, 297 vannes et 297 capteurs.");
    check(b.as->send("Lis l'automate joint : quelles sont les variables des moteurs ?", &why), "(5) le 1er tour (" + why + ")");
    b.step();
    b.reply("D'accord.");
    check(b.as->send("Cr\xC3\xA9" "e les variables IHM des 12 premiers moteurs et de leurs d\xC3\xA9" "fauts.", &why), "(5) le 2e tour (" + why + ")");
    b.step();
    check(b.s.requests.size() == 2 && has(b.s.requests.back().systemDynamic, "Moteur_000 :"), "(5) l'automate est dans le contexte");
    check(b.s.requests.size() == 2 && !has(b.s.requests.back().systemDynamic, "Vanne_001 :") && has(b.s.requests.back().systemDynamic, "891"),
          "(5) celles que la demande evoque d'abord (les moteurs), et le total est dit");
    return b.s.requests.size() < 2 ? Mesure{} : measure(b.s.requests[1], &b.s.requests[0]);
}

// AVANT : la 1.22.5 (8ae5f5d), mesuree par ce banc le 01/11/2026 (jetons estimes).
struct Ref {
    long long total;
    double    cout;
};
constexpr Ref kAvant[5] = {{18274, 21679}, {28920, 16663}, {19514, 7257}, {18981, 6724}, {19992, 7735}};
// PLAFONDS : l'APRES de la 1.22.6, plus 5 % - au-dela, une regression de taille fait echouer l'essai.
// 1.22.6 (integration) : les plafonds suivent les consignes reunies - les operations des variables, de la securite et des vues
// (regle 23, kRulesOptions : environ 1 000 jetons de plus, dans la partie mise en cache), sur les memes cas.
// 1.22.7 : + 64 jetons (la description de supprimer : << references >> modifie ce qui cite) - le cas 2 suit.
constexpr Ref kPlafond[5] = {{19100, 22900}, {26300, 8500}, {20750, 6900}, {20150, 6500}, {20800, 7200}};

std::string pct(double before, double after) {
    char b[32];
    std::snprintf(b, sizeof b, "%+.0f %%", before > 0 ? (after - before) * 100.0 / before : 0.0);
    return b;
}

} // namespace

int main() {
    std::printf("[1.22.6 : les jetons de ce qui part au fournisseur]\n");
    Mesure m9;
    const Mesure m[5] = {case1(), case2(&m9), case3(), case4(), case5()};
    const char* names[5] = {"(1) premiere demande", "(2) 10e tour, 5 x 40 operations", "(3) proposition de mission", "(4) correction automatique",
                            "(5) conversation avec un .XPG lu"};
    std::printf("APRES, le detail (jetons estimes : octets / 3,5) :\n");
    for (int i = 0; i < 5; ++i) print(names[i], m[i]);
    print("    (le 9e tour, pour memoire)", m9);
    std::printf("\nAVANT / APRES (total : tout ce qui part ; cout : pleins + 1,25 x ecrits + 0,1 x lus, en jetons) :\n");
    std::printf("  %-34s | %13s %13s | %13s %13s | %8s %8s\n", "cas", "AVANT total", "APRES total", "AVANT cout", "APRES cout", "total", "cout");
    long long ta = 0, tb = 0;
    double ca = 0, cb = 0;
    for (int i = 0; i < 5; ++i) {
        std::printf("  %-34s | %13lld %13lld | %13.0f %13.0f | %8s %8s\n", names[i], kAvant[i].total, m[i].total, kAvant[i].cout, m[i].cout,
                    pct(static_cast<double>(kAvant[i].total), static_cast<double>(m[i].total)).c_str(), pct(kAvant[i].cout, m[i].cout).c_str());
        ta += kAvant[i].total;
        tb += m[i].total;
        ca += kAvant[i].cout;
        cb += m[i].cout;
        check(m[i].total > 0 && m[i].total <= kPlafond[i].total, std::string(names[i]) + " : total sous le plafond (" + std::to_string(m[i].total) + " <= "
                                                                    + std::to_string(kPlafond[i].total) + ")");
        check(m[i].cout > 0 && m[i].cout <= kPlafond[i].cout, std::string(names[i]) + " : cout sous le plafond (" + std::to_string(static_cast<long long>(m[i].cout))
                                                                  + " <= " + std::to_string(static_cast<long long>(kPlafond[i].cout)) + ")");
    }
    std::printf("  %-34s | %13lld %13lld | %13.0f %13.0f | %8s %8s\n", "ensemble", ta, tb, ca, cb, pct(static_cast<double>(ta), static_cast<double>(tb)).c_str(),
                pct(ca, cb).c_str());
    // le 10e tour relit l'historique en cache (plus que les outils et les consignes)
    check(m[1].lus > m[1].outils + m[1].consignes, "(2) le 10e tour relit l'historique en cache (" + std::to_string(m[1].lus) + " lus)");
    std::printf("ai_jetons_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
