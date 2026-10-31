// =============================================================================
//  tests/ai_reseau_test.cpp - 1.15.0 : l'Assistant IA branche (le chantier RESEAU)
// -----------------------------------------------------------------------------
//  La connexion a une vraie IA, sans cle ni reseau exterieur :
//    - les adresses (http:// seulement vers ce poste), le base64 ;
//    - le lecteur SSE (des morceaux coupes partout, \r\n, plusieurs data:) ;
//    - le corps de la requete (les champs, les pieces jointes, le tool_result en
//      tete, ni tool_choice ni thinking, l'effort) ;
//    - le flux Anthropic (texte, appel d'outil complet par input_json_delta,
//      max_tokens et refusal sans proposition, l'erreur du flux, la coupure) ;
//    - le fournisseur sur un FAUX transport (les erreurs 401, 404, 429, 529, les
//      nouvelles tentatives, une panne du reseau, Arreter) ;
//    - de bout en bout avec libcurl contre un serveur local (Linux) ;
//    - la cle (jamais ecrite en clair ; sous Linux, jamais ecrite) ;
//    - l'Assistant branche sur un faux fournisseur : envoyer -> texte ->
//      proposition validee -> appliquer (un Ctrl+Z) -> le tour suivant porte
//      "appliquee" ; une proposition invalide -> le tour suivant porte l'erreur ;
//      Arreter, Reessayer, la persistance de l'appel d'outil ; et le meme chemin
//      par le vrai fournisseur sur le faux transport ;
//    - les parametres (Lister les modeles, Tester la connexion, l'effort) et le
//      panneau (l'etat de connexion, ce qui part, Reessayer).
// =============================================================================
#include "../src/app/ai/AiAnthropic.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiOpenAI.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiSecret.hpp"
#include "../src/app/ai/AiSettingsDialog.hpp"
#include "../src/app/ai/AiSse.hpp"
#include "../src/app/ai/AiTextParts.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/widgets/Controls.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <poll.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

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

// ---- un flux SSE enregistre, ecrit avec le JSON du logiciel ----------------------------------
Value obj(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value o = Value::object();
    for (const auto& [k, v] : kv) o.set(k, v);
    return o;
}
std::string sseOf(const std::string& event, const Value& data) { return "event: " + event + "\ndata: " + ai::json::write(data, false) + "\n\n"; }
std::string msgStart() {
    return sseOf("message_start", obj({{"type", "message_start"}, {"message", obj({{"id", "msg_1"}, {"role", "assistant"}, {"content", Value::array()}})}}));
}
std::string textStart(int i) { return sseOf("content_block_start", obj({{"type", "content_block_start"}, {"index", i}, {"content_block", obj({{"type", "text"}, {"text", ""}})}})); }
std::string textDelta(int i, const std::string& t) {
    return sseOf("content_block_delta", obj({{"type", "content_block_delta"}, {"index", i}, {"delta", obj({{"type", "text_delta"}, {"text", t}})}}));
}
std::string blockStop(int i) { return sseOf("content_block_stop", obj({{"type", "content_block_stop"}, {"index", i}})); }
std::string thinkingBlock(int i) {
    return sseOf("content_block_start", obj({{"type", "content_block_start"}, {"index", i}, {"content_block", obj({{"type", "thinking"}, {"thinking", ""}})}}))
           + sseOf("content_block_delta", obj({{"type", "content_block_delta"}, {"index", i}, {"delta", obj({{"type", "thinking_delta"}, {"thinking", "hmm"}})}}))
           + sseOf("content_block_delta", obj({{"type", "content_block_delta"}, {"index", i}, {"delta", obj({{"type", "signature_delta"}, {"signature", "sig"}})}}))
           + blockStop(i);
}
std::string toolStart(int i, const std::string& id) {
    return sseOf("content_block_start", obj({{"type", "content_block_start"}, {"index", i},
                                             {"content_block", obj({{"type", "tool_use"}, {"id", id}, {"name", "proposer_operations"}, {"input", Value::object()}})}}));
}
std::string jsonDelta(int i, const std::string& part) {
    return sseOf("content_block_delta", obj({{"type", "content_block_delta"}, {"index", i}, {"delta", obj({{"type", "input_json_delta"}, {"partial_json", part}})}}));
}
std::string msgDelta(const std::string& reason) {
    return sseOf("message_delta", obj({{"type", "message_delta"}, {"delta", obj({{"stop_reason", reason}})}, {"usage", obj({{"output_tokens", 42}})}}));
}
std::string msgStop() { return sseOf("message_stop", obj({{"type", "message_stop"}})); }
std::string ping() { return "event: ping\ndata: {\"type\": \"ping\"}\n\n"; }

const std::string kTankInput =
    "{\"resume\": \"Une cuve\", \"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_A\", "
    "\"proprietes\": {\"x\": \"120\", \"y\": \"120\", \"w\": \"150\", \"h\": \"200\", \"label\": \"Cuve A\"}}]}";
const std::string kInventedInput =
    "{\"resume\": \"Une cuve\", \"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_X\", "
    "\"proprietes\": {\"x\": \"100\", \"y\": \"100\", \"w\": \"150\", \"h\": \"200\", \"couleurInventee\": \"#FF0000\"}}]}";
const std::string kUnreadableInput = "{\"resume\": \"Rien\", \"operations\": [{\"action\": \"executer\", \"code\": \"system('x')\"}]}";

// Du texte puis un appel d'outil dont l'entree arrive en trois morceaux coupes n'importe ou.
std::string streamTextTool(const std::string& toolId, const std::string& input, const std::string& reason = "tool_use", bool thinking = true) {
    std::string s = msgStart() + ping();
    int i = 0;
    if (thinking) s += thinkingBlock(i++);
    s += textStart(i) + textDelta(i, "Je propose ") + textDelta(i, "une cuve.") + blockStop(i);
    ++i;
    s += toolStart(i, toolId) + jsonDelta(i, "");
    const std::size_t a = input.size() / 3, b = 2 * input.size() / 3;
    s += jsonDelta(i, input.substr(0, a)) + jsonDelta(i, input.substr(a, b - a)) + jsonDelta(i, input.substr(b));
    s += blockStop(i) + msgDelta(reason) + msgStop();
    return s;
}
std::string streamText(const std::string& text, const std::string& reason = "end_turn") {
    return msgStart() + textStart(0) + textDelta(0, text) + blockStop(0) + msgDelta(reason) + msgStop();
}
std::string errorJson(const std::string& type, const std::string& message) {
    return ai::json::write(obj({{"type", "error"}, {"error", obj({{"type", type}, {"message", message}})}}), false);
}

std::vector<ai::ProviderEvent> drain(ai::AnthropicStream& s) { return s.take(); }

std::string allText(const std::vector<ai::ProviderEvent>& ev) {
    std::string t;
    for (const auto& e : ev)
        if (e.type == ai::ProviderEvent::Type::Text) t += e.text;
    return t;
}
const ai::ProviderEvent* find(const std::vector<ai::ProviderEvent>& ev, ai::ProviderEvent::Type type) {
    for (const auto& e : ev)
        if (e.type == type) return &e;
    return nullptr;
}
bool hasStatus(const std::vector<ai::ProviderEvent>& ev, const std::string& s) {
    return std::any_of(ev.begin(), ev.end(), [&](const ai::ProviderEvent& e) { return e.type == ai::ProviderEvent::Type::Status && e.text == s; });
}

// =============================================================================
//  Le faux transport : il rejoue des reponses, retient les requetes.
// =============================================================================
struct Scripted {
    int                              status{200};
    ai::HttpHeaders                  headers;
    std::vector<std::string>         chunks;
    ai::HttpResponse::Failure        failure{ai::HttpResponse::Failure::None};
    bool                             hang{false};   // attend Arreter
};
struct FakeNet {
    std::mutex                   m;
    std::deque<Scripted>         replies;
    std::vector<ai::HttpRequest> requests;
    void reset() {
        std::lock_guard<std::mutex> lk(m);
        replies.clear();
        requests.clear();
    }
    void push(Scripted s) {
        std::lock_guard<std::mutex> lk(m);
        replies.push_back(std::move(s));
    }
    std::size_t count() {
        std::lock_guard<std::mutex> lk(m);
        return requests.size();
    }
    ai::HttpRequest at(std::size_t i) {
        std::lock_guard<std::mutex> lk(m);
        return i < requests.size() ? requests[i] : ai::HttpRequest{};
    }
};
FakeNet g_net;

Scripted sseReply(const std::string& stream, std::size_t piece = 37) {
    Scripted s;
    s.headers = {{"content-type", "text/event-stream"}};
    for (std::size_t i = 0; i < stream.size(); i += piece) s.chunks.push_back(stream.substr(i, piece));   // coupe n'importe ou
    return s;
}
Scripted errorReply(int status, const std::string& type, const std::string& message, ai::HttpHeaders headers = {}) {
    Scripted s;
    s.status = status;
    s.headers = std::move(headers);
    s.headers.emplace_back("content-type", "application/json");
    s.chunks = {errorJson(type, message)};
    return s;
}
Scripted netFailure(ai::HttpResponse::Failure f) {
    Scripted s;
    s.status = 0;
    s.failure = f;
    return s;
}

class FakeHttp final : public ai::HttpClient {
public:
    ai::HttpResponse perform(const ai::HttpRequest& req, const ai::HttpSink& sink) override {
        Scripted s;
        {
            std::lock_guard<std::mutex> lk(g_net.m);
            g_net.requests.push_back(req);
            if (!g_net.replies.empty()) {
                s = std::move(g_net.replies.front());
                g_net.replies.pop_front();
            } else {
                s = errorReply(500, "api_error", "pas de reponse prevue");
            }
        }
        ai::HttpResponse r;
        if (cancelled_) return cancelledResponse();
        if (s.status == 0) {
            r.failure = s.failure;
            r.error = ai::failureText(s.failure, "essai", 60);
            return r;
        }
        r.status = s.status;
        r.headers = s.headers;
        if (sink.onHeaders) sink.onHeaders(s.status, s.headers);
        for (const auto& c : s.chunks) {
            if (cancelled_) return cancelledResponse();
            if (sink.onData) sink.onData(c);
        }
        while (s.hang && !cancelled_) pause(2);
        if (cancelled_) return cancelledResponse();
        if (s.failure != ai::HttpResponse::Failure::None) {
            r.failure = s.failure;
            r.error = ai::failureText(s.failure, "essai", 60);
        }
        return r;
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    static ai::HttpResponse cancelledResponse() {
        ai::HttpResponse r;
        r.failure = ai::HttpResponse::Failure::Cancelled;
        return r;
    }
    std::atomic<bool> cancelled_{false};
};

void useFakeNet() {
    g_net.reset();
    ai::setHttpFactory([] { return std::make_unique<FakeHttp>(); });
}

// Les evenements d'un fournisseur jusqu'a sa fin (Done ou Error), au plus `ms`.
std::vector<ai::ProviderEvent> runToEnd(ai::Provider& p, int ms = 5000) {
    std::vector<ai::ProviderEvent> all;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        for (auto& e : p.poll()) all.push_back(std::move(e));
        if (!all.empty() && (all.back().type == ai::ProviderEvent::Type::Done || all.back().type == ai::ProviderEvent::Type::Error) && !p.busy()) break;
        pause(2);
    }
    for (auto& e : p.poll()) all.push_back(std::move(e));
    return all;
}

ai::ProviderRequest simpleRequest(const std::string& text = "Ajoute une cuve") {
    ai::ProviderRequest r;
    r.model = "modele-essai";
    r.apiKey = "cle-essai-SECRETE";
    r.system = "Les consignes.";
    r.toolsJson = ai::operationsTool();
    ai::ProviderTurn t;
    t.text = text;
    r.turns.push_back(t);
    return r;
}

ai::AnthropicProvider::Options fastRetries() {
    ai::AnthropicProvider::Options o;
    o.firstDelay = 0.01;
    o.factor = 2.0;
    return o;
}

// =============================================================================
//  1. Les adresses, le base64
// =============================================================================
void testUrls() {
    std::printf("[adresses]\n");
    ai::Url u;
    std::string why;
    check(ai::parseUrl("https://api.anthropic.com", u, &why) && u.https && u.host == "api.anthropic.com" && u.port == 443 && u.path == "/" && !u.hasPath,
          "https://hote : sans chemin");
    check(ai::parseUrl("http://127.0.0.1:8765/relais/v1/messages?x=1", u, &why) && !u.https && u.port == 8765 && u.path == "/relais/v1/messages?x=1"
              && u.hasPath && u.loopback(),
          "http://127.0.0.1:port/chemin : accepte (ce poste)");
    check(ai::parseUrl("http://localhost:9000", u) && u.loopback() && ai::parseUrl("http://[::1]:9000", u) && u.loopback() && u.host == "::1",
          "localhost et [::1] : ce poste");
    check(!ai::parseUrl("http://relais.entreprise.fr", u, &why) && has(why, "https://"), "http:// vers un autre poste : refuse (" + why + ")");
    check(!ai::parseUrl("ftp://x", u, &why) && !ai::parseUrl("https://", u, &why) && !ai::parseUrl("https://h:99999", u, &why)
              && !ai::parseUrl("https://user:mdp@h", u, &why),
          "un autre protocole, sans hote, un port faux, un identifiant : refuses");
    std::string msg, models;
    check(ai::anthropicUrls("", msg, models) && msg == "https://api.anthropic.com/v1/messages" && models == "https://api.anthropic.com/v1/models",
          "vide : l'adresse du fournisseur");
    check(ai::anthropicUrls("https://relais.entreprise.fr", msg, models) && msg == "https://relais.entreprise.fr/v1/messages",
          "un relais sans chemin : <adresse>/v1/messages");
    check(ai::anthropicUrls("https://relais.fr/ia/v1/messages", msg, models) && msg == "https://relais.fr/ia/v1/messages"
              && models == "https://relais.fr/ia/v1/models",
          "une adresse qui finit par /v1/messages : telle quelle");
    check(ai::anthropicUrls("http://127.0.0.1:8765/relais/", msg, models) && msg == "http://127.0.0.1:8765/relais/v1/messages",
          "un autre chemin : un prefixe");
    check(!ai::anthropicUrls("http://serveur:80", msg, models, &why), "http:// distant : refuse aussi pour l'IA");
    check(ai::base64("") == "" && ai::base64("f") == "Zg==" && ai::base64("fo") == "Zm8=" && ai::base64("foo") == "Zm9v"
              && ai::base64("foobar") == "Zm9vYmFy" && ai::base64(std::string("\xFF\x00\x10", 3)) == "/wAQ",
          "base64 (les bourrages, les octets nuls)");
}

// =============================================================================
//  2. Le lecteur SSE
// =============================================================================
void testSse() {
    std::printf("[sse]\n");
    const std::string stream = "\xEF\xBB\xBF" ": un commentaire\r\nevent: alpha\r\ndata: un\r\ndata: deux\r\n\r\n"
                               "data: sans type\n\nevent: vide\n\nid: 7\rdata:sans espace\r\r"
                               "event: dernier\ndata: {\"a\": 1}\n\n";
    std::vector<ai::SseEvent> whole;
    {
        ai::SseReader r;
        r.feed(stream, whole);
    }
    check(whole.size() == 4, "quatre evenements (l'evenement sans data ne compte pas) : " + std::to_string(whole.size()));
    if (whole.size() == 4) {
        check(whole[0].event == "alpha" && whole[0].data == "un\ndeux", "deux lignes data: jointes par \\n, \\r\\n compris");
        check(whole[1].event == "message" && whole[1].data == "sans type", "le type par defaut : message");
        check(whole[2].data == "sans espace" && whole[2].id == "7", "\\r seul ; data: sans espace ; id");
        check(whole[3].event == "dernier" && whole[3].data == "{\"a\": 1}", "le BOM et le commentaire ignores");
    }
    // Coupe a chaque octet, puis en morceaux de toutes les tailles : toujours les memes evenements.
    bool same = true;
    for (std::size_t step = 1; step <= 9 && same; ++step) {
        ai::SseReader r;
        std::vector<ai::SseEvent> out;
        for (std::size_t i = 0; i < stream.size(); i += step) r.feed(stream.substr(i, step), out);
        same = out.size() == whole.size();
        for (std::size_t k = 0; same && k < out.size(); ++k) same = out[k].event == whole[k].event && out[k].data == whole[k].data;
    }
    check(same, "des morceaux coupes partout (1 a 9 octets) : les memes evenements");
    // Un evenement sans sa ligne vide : perdu a la fin (le flux a ete coupe).
    ai::SseReader r;
    std::vector<ai::SseEvent> out;
    r.feed("event: a\ndata: 1\n\nevent: b\ndata: 2\n", out);
    check(out.size() == 1 && r.pending(), "un evenement commence reste en attente");
    r.finish(out);
    check(out.size() == 1, "finish : l'evenement inacheve n'est pas rendu");
}

// =============================================================================
//  3. Le corps de la requete
// =============================================================================
void testBody() {
    std::printf("[corps de la requete]\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_reseau_corps";
    fs::remove_all(dir);
    const std::string png = std::string("\x89PNG\r\n\x1A\n", 8) + std::string(40, '\x01');
    writeFile(dir / "capture.png", png);
    writeFile(dir / "photo.jpg", png);   // une .jpg qui est un PNG : part en image/png
    writeFile(dir / "plan.pdf", "%PDF-1.4 un faux pdf");
    writeFile(dir / "mesures.csv", "Pompe;D\xE9" "bit\nP1;12\n");   // Windows-1252 (Excel)
    writeFile(dir / "liste.xlsx", "PK...");
    const auto att = [&](const std::string& name, std::uintmax_t bytes = 0) {
        ai::ProviderAttachment a;
        a.name = name;
        a.mediaType = ai::mediaTypeOf(name);
        a.path = U(dir / name);
        a.bytes = bytes ? bytes : fs::file_size(dir / name);
        return a;
    };
    check(ai::planAttachment(att("capture.png")).as == ai::AttachmentPlan::As::Image && ai::planAttachment(att("plan.pdf")).as == ai::AttachmentPlan::As::Document
              && ai::planAttachment(att("mesures.csv")).as == ai::AttachmentPlan::As::Text
              && ai::planAttachment(att("liste.xlsx")).as == ai::AttachmentPlan::As::NotRead,
          "PNG/JPG en image, PDF en document, CSV en texte, XLSX : pas lu");
    const auto big = ai::planAttachment(att("capture.png", 9ull * 1024 * 1024));
    check(big.as == ai::AttachmentPlan::As::TooBig && has(big.note, "au plus"), "une image trop lourde : pas envoyee, dit (" + big.note + ")");
    check(has(ai::planAttachment(att("liste.xlsx")).note, "n'est pas lu par l'IA"), "le classeur : la limite est dite");

    ai::ProviderRequest r;
    r.model = "modele-essai";
    r.maxTokens = 16000;
    r.effort = "high";
    r.system = "Les consignes de l'Assistant.";
    r.toolsJson = ai::operationsTool();
    ai::ProviderTurn u1;
    u1.text = "Ajoute une cuve";
    u1.attachments = {att("capture.png"), att("photo.jpg"), att("plan.pdf"), att("mesures.csv"), att("liste.xlsx")};
    ai::ProviderTurn a1;
    a1.role = ai::ProviderTurn::Role::Assistant;
    a1.text = "Je propose une cuve.";
    a1.toolUseId = "toolu_01";
    a1.toolName = "proposer_operations";
    a1.toolInputJson = kTankInput;
    ai::ProviderTurn u2;
    u2.text = "Elle est appliqu\xC3\xA9" "e ?";
    u2.toolResultFor = "toolu_01";
    u2.toolResultText = "Erreur de validation : propriete inconnue";
    u2.toolResultError = true;
    ai::ProviderTurn a2;   // un tour vide (arrete sans texte) : saute
    a2.role = ai::ProviderTurn::Role::Assistant;
    ai::ProviderTurn u3;
    u3.text = "Et maintenant ?";
    ai::ProviderTurn a3;   // un dernier tour de l'assistant : jamais de "prefill"
    a3.role = ai::ProviderTurn::Role::Assistant;
    a3.text = "Je";
    r.turns = {u1, a1, u2, a2, u3, a3};
    std::vector<std::string> notes;
    const std::string body = ai::anthropicBody(r, &notes);
    Value v;
    std::string why;
    check(ai::json::parse(body, v, &why) && v.isObject(), "le corps est du JSON (" + why + ")");
    check(v.str("model") == "modele-essai" && v.integer("max_tokens") == 16000 && v.flag("stream"), "model, max_tokens 16000, stream");
    check(v.str("system") == "Les consignes de l'Assistant." && v.at("tools").isArray() && v.at("tools").items().size() == 1
              && v.at("tools").items()[0].str("name") == "proposer_operations",
          "system, tools (l'outil des operations)");
    check(!v.has("temperature") && !v.has("top_p") && !v.has("top_k") && !v.has("thinking") && !v.has("tool_choice"),
          "ni temperature, ni thinking, ni tool_choice");
    check(v.at("output_config").str("effort") == "high", "l'effort choisi : output_config.effort");
    const auto& msgs = v.at("messages").items();
    check(msgs.size() == 3, "trois messages (le tour vide saute, les deux tours de l'utilisateur joints, le dernier de l'assistant retire) : "
                                + std::to_string(msgs.size()));
    if (msgs.size() == 3) {
        check(msgs[0].str("role") == "user" && msgs[1].str("role") == "assistant" && msgs[2].str("role") == "user", "user, assistant, user");
        const auto& c0 = msgs[0].at("content").items();
        check(c0.size() == 6, "les cinq pieces puis le texte : " + std::to_string(c0.size()));
        if (c0.size() == 6) {
            check(c0[0].str("type") == "image" && c0[0].at("source").str("type") == "base64" && c0[0].at("source").str("media_type") == "image/png"
                      && c0[0].at("source").str("data") == ai::base64(png),
                  "l'image : base64, image/png, sans retour a la ligne");
            check(c0[1].at("source").str("media_type") == "image/png", "la .jpg qui est un PNG : image/png (d'apres ses octets)");
            check(c0[2].str("type") == "document" && c0[2].at("source").str("media_type") == "application/pdf", "le PDF : document");
            check(c0[3].str("type") == "text" && has(c0[3].str("text"), "mesures.csv") && has(c0[3].str("text"), "D\xC3\xA9" "bit"),
                  "le CSV : en texte avec son nom, Windows-1252 relu en UTF-8");
            check(c0[4].str("type") == "text" && has(c0[4].str("text"), "liste.xlsx") && has(c0[4].str("text"), "pas lu"),
                  "le classeur : une ligne qui dit qu'il n'est pas lu");
            check(c0[5].str("type") == "text" && c0[5].str("text") == "Ajoute une cuve", "le texte en dernier");
        }
        const auto& c1 = msgs[1].at("content").items();
        check(c1.size() == 2 && c1[0].str("type") == "text" && c1[1].str("type") == "tool_use" && c1[1].str("id") == "toolu_01"
                  && c1[1].str("name") == "proposer_operations" && c1[1].at("input").isObject()
                  && c1[1].at("input").at("operations").items().size() == 1,
              "l'assistant : son texte, puis son appel d'outil (l'entree en objet)");
        const auto& c2 = msgs[2].at("content").items();
        check(!c2.empty() && c2[0].str("type") == "tool_result" && c2[0].str("tool_use_id") == "toolu_01" && c2[0].flag("is_error")
                  && has(c2[0].str("content"), "Erreur de validation"),
              "le tour suivant : le tool_result EN TETE, is_error");
        check(c2.size() == 3 && c2[1].str("text") == "Elle est appliqu\xC3\xA9" "e ?" && c2[2].str("text") == "Et maintenant ?",
              "les deux tours de l'utilisateur de suite : joints");
    }
    check(std::any_of(notes.begin(), notes.end(), [](const std::string& n) { return has(n, "liste.xlsx"); }), "les notes : le classeur");
    // Un appel d'outil sans resultat au tour suivant : l'API l'exige, le corps le met.
    ai::ProviderRequest r2;
    r2.model = "m";
    ai::ProviderTurn q;
    q.text = "Une cuve";
    ai::ProviderTurn rep = a1;
    ai::ProviderTurn next;
    next.text = "Merci";
    r2.turns = {q, rep, next};
    Value v2;
    (void)ai::json::parse(ai::anthropicBody(r2), v2);
    const auto& last = v2.at("messages").items().back().at("content").items();
    check(!last.empty() && last[0].str("type") == "tool_result" && last[0].str("tool_use_id") == "toolu_01", "un tool_result manquant : ajoute");
    check(!v2.has("output_config"), "pas d'effort choisi : pas d'output_config");
    // Les consignes en deux morceaux : le premier mis en cache chez le fournisseur.
    ai::ProviderRequest r3 = r2;
    r3.system = "ignore";
    r3.systemStable = "Les consignes stables.";
    r3.systemDynamic = "Le contexte de la vue.";
    Value v3;
    (void)ai::json::parse(ai::anthropicBody(r3), v3);
    const auto& sys = v3.at("system").items();
    check(sys.size() == 2 && sys[0].str("text") == "Les consignes stables." && sys[0].at("cache_control").str("type") == "ephemeral"
              && sys[1].str("text") == "Le contexte de la vue." && !sys[1].has("cache_control"),
          "system en deux blocs : le stable avec cache_control ephemeral, puis le dynamique");
}

// =============================================================================
//  4. Le flux Anthropic, sans reseau
// =============================================================================
void testStream() {
    std::printf("[flux]\n");
    {
        ai::AnthropicStream s;
        const std::string stream = streamTextTool("toolu_01", kTankInput);
        std::vector<ai::ProviderEvent> ev;
        for (std::size_t i = 0; i < stream.size(); i += 5) {   // coupe tous les cinq octets
            s.feed(stream.substr(i, 5));
            for (auto& e : drain(s)) ev.push_back(std::move(e));
        }
        s.end();
        for (auto& e : drain(s)) ev.push_back(std::move(e));
        check(allText(ev) == "Je propose une cuve.", "le texte au fil de l'eau");
        check(hasStatus(ev, "reflexion") && hasStatus(ev, "ecriture") && hasStatus(ev, "operations"), "les etats : reflexion, ecriture, operations");
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        Value in;
        check(tool && tool->toolUseId == "toolu_01" && tool->toolName == "proposer_operations" && ai::json::parse(tool->toolInputJson, in)
                  && in.at("operations").items().size() == 1,
              "l'appel d'outil COMPLET : les morceaux input_json_delta concatenes, lus strictement");
        const auto* done = find(ev, ai::ProviderEvent::Type::Done);
        check(done && done->stopReason == "tool_use" && s.finished() && !s.failed(), "la fin : tool_use");
        check(ev.back().type == ai::ProviderEvent::Type::Done, "Done en dernier (apres l'outil)");
    }
    {
        ai::AnthropicStream s;
        s.feed(streamTextTool("toolu_02", kTankInput, "max_tokens"));
        auto ev = drain(s);
        check(!find(ev, ai::ProviderEvent::Type::ToolUse) && find(ev, ai::ProviderEvent::Type::Done)->stopReason == "max_tokens",
              "max_tokens : l'appel d'outil n'est pas rendu (son entree peut etre tronquee)");
    }
    {
        ai::AnthropicStream s;
        s.feed(streamTextTool("toolu_03", kTankInput, "refusal", false));
        auto ev = drain(s);
        check(!find(ev, ai::ProviderEvent::Type::ToolUse) && find(ev, ai::ProviderEvent::Type::Done)->stopReason == "refusal",
              "refusal : rien n'est propose");
    }
    {
        ai::AnthropicStream s;
        s.feed(msgStart() + "event: error\ndata: " + errorJson("overloaded_error", "Overloaded") + "\n\n");
        check(s.failed() && s.error().status == 529 && s.error().retryable && !s.emitted() && has(s.error().text, "surcharg"),
              "event: error (overloaded) : 529, reessayable");
    }
    {
        ai::AnthropicStream s;
        s.feed(msgStart() + textStart(0) + textDelta(0, "Je comm"));
        auto ev = drain(s);
        s.end();
        check(allText(ev) == "Je comm" && s.failed() && s.error().retryable && has(s.error().text, "coup") && s.emitted(),
              "la connexion finie sans message_stop : une coupure, dite");
    }
    {
        ai::AnthropicStream s;
        s.feed(msgStart() + textStart(0) + textDelta(0, "ok") + blockStop(0) + msgStop());
        auto ev = drain(s);
        check(find(ev, ai::ProviderEvent::Type::Done) && find(ev, ai::ProviderEvent::Type::Done)->stopReason == "end_turn",
              "sans stop_reason : end_turn");
    }
}

// =============================================================================
//  5. Les erreurs du service
// =============================================================================
void testApiErrors() {
    std::printf("[erreurs]\n");
    const auto e401 = ai::apiError(401, errorJson("authentication_error", "invalid x-api-key"), "m");
    check(has(e401.text, "Cl\xC3\xA9 API refus\xC3\xA9" "e") && !e401.retryable, "401 : Cle API refusee (" + e401.text + ")");
    const auto e404 = ai::apiError(404, errorJson("not_found_error", "model: modele-x"), "modele-x");
    check(has(e404.text, "Mod\xC3\xA8le introuvable") && has(e404.text, "modele-x"), "404 : modele introuvable, nomme (" + e404.text + ")");
    check(has(ai::apiError(404, "<html>Not Found</html>", "m").text, "Adresse introuvable"), "404 sans JSON : l'adresse (un relais)");
    const auto e400 = ai::apiError(400, errorJson("invalid_request_error", "messages: text content blocks must be non-empty"), "m");
    check(has(e400.text, "400") && has(e400.text, "non-empty") && !e400.retryable, "400 : le message du service est montre");
    check(ai::apiError(429, errorJson("rate_limit_error", "x"), "m").retryable && ai::apiError(529, errorJson("overloaded_error", "x"), "m").retryable
              && ai::apiError(500, errorJson("api_error", "x"), "m").retryable && ai::apiError(503, "", "m").retryable,
          "429, 529, 500, 5xx : reessayables");
    check(!ai::apiError(403, errorJson("permission_error", "x"), "m").retryable && has(ai::apiError(413, "", "m").text, "trop lourde"),
          "403 : non ; 413 : la demande est trop lourde");
}

// =============================================================================
//  6. Le fournisseur, sur le faux transport
// =============================================================================
void testProvider() {
    std::printf("[fournisseur]\n");
    useFakeNet();
    {
        ai::AnthropicProvider p(fastRetries());
        std::string why;
        auto bad = simpleRequest();
        bad.model.clear();
        check(!p.start(bad, &why) && has(why, "mod\xC3\xA8le"), "sans modele : refuse, dit (" + why + ")");
        bad = simpleRequest();
        bad.apiKey.clear();
        check(!p.start(bad, &why) && has(why, "cl\xC3\xA9"), "sans cle ni relais : refuse");
        bad = simpleRequest();
        bad.endpoint = "http://serveur.distant:8080";
        check(!p.start(bad, &why) && has(why, "http://"), "http:// distant : refuse avant tout envoi");
        check(g_net.count() == 0, "rien n'est parti");
    }
    {
        g_net.push(sseReply(streamTextTool("toolu_01", kTankInput)));
        ai::AnthropicProvider p(fastRetries());
        check(p.start(simpleRequest()) && p.busy(), "start rend la main, le fil travaille");
        const auto ev = runToEnd(p);
        check(allText(ev) == "Je propose une cuve." && find(ev, ai::ProviderEvent::Type::ToolUse) && !p.busy(), "texte, appel d'outil, fin");
        check(!ev.empty() && ev.front().type == ai::ProviderEvent::Type::Status && ev.front().text == "connexion", "le premier etat : connexion");
        const auto req = g_net.at(0);
        const auto header = [&](const std::string& k) {
            for (const auto& [n, v] : req.headers)
                if (n == k) return v;
            return std::string{};
        };
        check(req.method == "POST" && req.url == "https://api.anthropic.com/v1/messages", "POST https://api.anthropic.com/v1/messages");
        check(header("x-api-key") == "cle-essai-SECRETE" && header("anthropic-version") == "2026-10-21" && header("content-type") == "application/json",
              "les en-tetes : x-api-key, anthropic-version 2026-10-21, content-type");
        check(has(req.body, "\"stream\":true") && !has(req.body, "SECRETE"), "le corps : stream ; la cle n'y est pas");
        check(req.idleTimeoutSeconds == 60, "le delai d'inactivite : celui de la requete");
    }
    {
        g_net.reset();
        g_net.push(errorReply(401, "authentication_error", "invalid x-api-key"));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        const auto* e = find(ev, ai::ProviderEvent::Type::Error);
        check(e && e->httpStatus == 401 && !e->retryable && has(e->text, "Cl\xC3\xA9 API refus"), "401 : l'erreur, sans nouvelle tentative");
        check(g_net.count() == 1, "une seule requete");
    }
    {
        g_net.reset();
        g_net.push(errorReply(404, "not_found_error", "model: modele-x"));
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest();
        r.model = "modele-x";
        (void)p.start(r);
        const auto all1 = runToEnd(p);
        const auto* e = find(all1, ai::ProviderEvent::Type::Error);
        check(e && has(e->text, "modele-x") && e->httpStatus == 404, "404 : modele introuvable");
    }
    {
        g_net.reset();
        g_net.push(errorReply(529, "overloaded_error", "Overloaded"));
        g_net.push(errorReply(429, "rate_limit_error", "slow down", {{"retry-after", "0"}}));
        g_net.push(sseReply(streamText("Enfin.")));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        int reprises = 0;
        std::string detail;
        for (const auto& e : ev)
            if (e.type == ai::ProviderEvent::Type::Status && e.text == "reprise") {
                ++reprises;
                detail = e.detail;
            }
        check(g_net.count() == 3 && reprises == 2 && allText(ev) == "Enfin." && find(ev, ai::ProviderEvent::Type::Done),
              "529 puis 429 : deux nouvelles tentatives, puis la reponse");
        check(has(detail, "Nouvelle tentative (2/2)") && has(detail, "429"), "l'etat dit la tentative et sa cause (" + detail + ")");
    }
    {
        g_net.reset();
        for (int k = 0; k < 4; ++k) g_net.push(errorReply(529, "overloaded_error", "Overloaded"));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto all2 = runToEnd(p);
        const auto* e = find(all2, ai::ProviderEvent::Type::Error);
        check(g_net.count() == 3 && e && e->httpStatus == 529 && e->retryable, "toujours 529 : deux tentatives au plus, puis l'erreur (Reessayer)");
    }
    {
        g_net.reset();
        g_net.push(errorReply(429, "rate_limit_error", "x", {{"retry-after", "300"}}));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto all3 = runToEnd(p);
        const auto* e = find(all3, ai::ProviderEvent::Type::Error);
        check(g_net.count() == 1 && e && has(e->text, "300\xC2\xA0s"), "un retry-after trop long : pas d'attente, dit (" + (e ? e->text : std::string{}) + ")");
    }
    {
        g_net.reset();
        g_net.push(sseReply(msgStart() + "event: error\ndata: " + errorJson("overloaded_error", "Overloaded") + "\n\n"));
        g_net.push(sseReply(streamText("Reprise.")));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        check(g_net.count() == 2 && allText(ev) == "Reprise.", "la surcharge annoncee dans le flux avant tout texte : reessayee");
    }
    {
        g_net.reset();
        g_net.push(netFailure(ai::HttpResponse::Failure::Connect));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto all4 = runToEnd(p);
        const auto* e = find(all4, ai::ProviderEvent::Type::Error);
        check(g_net.count() == 1 && e && e->retryable && has(e->text, "hors ligne"), "hors ligne : l'erreur claire, Reessayer (" + (e ? e->text : "") + ")");
    }
    {
        g_net.reset();
        Scripted cut = sseReply(msgStart() + textStart(0) + textDelta(0, "Je commence"));
        cut.failure = ai::HttpResponse::Failure::Cut;
        g_net.push(cut);
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        const auto* e = find(ev, ai::ProviderEvent::Type::Error);
        check(allText(ev) == "Je commence" && e && e->retryable && has(e->text, "coup") && g_net.count() == 1,
              "coupure pendant le flux : le texte recu, puis l'erreur ; pas de tentative automatique");
    }
    {
        g_net.reset();
        Scripted hang = sseReply(msgStart() + textStart(0) + textDelta(0, "Un mot"));
        hang.hang = true;
        g_net.push(hang);
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(simpleRequest());
        std::vector<ai::ProviderEvent> ev;
        for (int k = 0; k < 1000 && allText(ev).empty(); ++k) {
            for (auto& e : p.poll()) ev.push_back(std::move(e));
            pause(2);
        }
        const auto t0 = std::chrono::steady_clock::now();
        p.cancel();
        for (auto& e : runToEnd(p, 2000)) ev.push_back(std::move(e));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const auto* d = find(ev, ai::ProviderEvent::Type::Done);
        check(d && d->stopReason == "arrete" && !p.busy() && ms < 1000, "Arreter : la connexion se ferme, un Done \"arrete\" suit (" + std::to_string(ms) + " ms)");
    }
    {
        // Arreter pendant l'attente d'une nouvelle tentative.
        g_net.reset();
        g_net.push(errorReply(529, "overloaded_error", "x"));
        ai::AnthropicProvider::Options slow;
        slow.firstDelay = 30.0;
        ai::AnthropicProvider p(slow);
        (void)p.start(simpleRequest());
        std::vector<ai::ProviderEvent> ev;
        for (int k = 0; k < 1000 && !hasStatus(ev, "reprise"); ++k) {
            for (auto& e : p.poll()) ev.push_back(std::move(e));
            pause(2);
        }
        p.cancel();
        for (auto& e : runToEnd(p, 2000)) ev.push_back(std::move(e));
        const auto* d = find(ev, ai::ProviderEvent::Type::Done);
        check(hasStatus(ev, "reprise") && d && d->stopReason == "arrete" && !p.busy(), "Arreter pendant l'attente d'une tentative : tout de suite");
    }
    {
        // La liste des modeles.
        g_net.reset();
        Scripted ok;
        ok.chunks = {"{\"data\": [{\"id\": \"modele-essai\", \"display_name\": \"Mod\xC3\xA8le d'essai\"}, {\"id\": \"modele-rapide\"}], \"has_more\": false}"};
        g_net.push(ok);
        ai::ModelQuery q;
        std::string why;
        check(q.start("", "cle-essai", 30, &why), "Lister : part sur un fil");
        std::optional<ai::ModelsResult> r;
        for (int k = 0; k < 2000 && !r; ++k) {
            r = q.poll();
            pause(1);
        }
        check(r && r->ok && r->models.size() == 2 && r->models[0].name == "Mod\xC3\xA8le d'essai" && r->models[1].name == "modele-rapide",
              "la liste : les identifiants et les noms");
        const auto req = g_net.at(0);
        check(req.method == "GET" && req.url == "https://api.anthropic.com/v1/models?limit=1000", "GET /v1/models (gratuit, sans jetons)");
        check(!q.start("", "", 30, &why), "sans cle ni relais : refuse");
        const auto bad = ai::parseModels(401, errorJson("authentication_error", "x"));
        check(!bad.ok && has(bad.error, "Cl\xC3\xA9 API refus"), "401 : Cle API refusee");
        check(!ai::parseModels(200, "<html>").ok, "une reponse qui n'est pas la liste : dite");
    }
    ai::setHttpFactory(nullptr);
}

// =============================================================================
//  6 bis. 1.20.0 : les textes joints a la mesure du modele (plus de coupure a 200 Ko)
// =============================================================================
void testTextParts() {
    std::printf("[textes joints decoupes]\n");
    namespace tp = ai::textparts;
    // le decoupage : aux fins de ligne, les lignes comptees, rien de perdu ; une ligne trop longue : sur un caractere entier
    {
        std::string t;
        for (int i = 1; i <= 40; ++i) t += "ligne " + std::to_string(i) + " ; d\xC3\xA9" "bit " + std::to_string(i * 3) + "\n";
        const auto parts = tp::split(t, 200);
        std::string back;
        bool lines = true, ends = true;
        std::size_t next = 1;
        for (const auto& p : parts) {
            back += p.text;
            lines = lines && p.firstLine == next && p.lastLine >= p.firstLine && p.text.size() <= 200;
            ends = ends && (p.text.empty() || p.text.back() == '\n');
            next = p.lastLine + 1;
        }
        check(parts.size() > 3 && back == t && lines && ends && parts.back().lastLine == 40,
              "les parties : aux fins de ligne, lignes 1 a 40 suivies, rien de perdu (" + std::to_string(parts.size()) + " parties)");
        std::string longLine;
        for (int i = 0; i < 300; ++i) longLine += "\xC3\xA9";   // 600 octets, sans fin de ligne
        const auto cut = tp::split(longLine, 101);
        bool whole = true;
        std::string back2;
        for (const auto& p : cut) {
            back2 += p.text;
            whole = whole && !p.text.empty() && (static_cast<unsigned char>(p.text[0]) >> 6) != 0x2;
        }
        check(back2 == longLine && whole && cut.size() >= 6, "une ligne trop longue : coupee sur des caracteres entiers");
        check(tp::normalize(std::string("\xEF\xBB\xBF" "D\xE9" "bit")) == "D\xC3\xA9" "bit", "BOM retire, Windows-1252 converti");
    }
    // le budget : d'apres la fenetre du modele
    {
        ai::ProviderRequest r = simpleRequest();
        r.contextTokens = 1000;
        check(tp::budgetBytes(r, 0) == 0, "une fenetre de 1 000 jetons : aucune place pour un texte");
        r.contextTokens = 200000;
        const auto b200 = tp::budgetBytes(r, 0);
        r.contextTokens = 1000000;
        const auto b1m = tp::budgetBytes(r, 0);
        check(b200 > 300 * 1024 && b1m > 5 * b200, "200 000 jetons : plus de 300 Ko ; 1 000 000 : bien plus (" + std::to_string(b200 / 1024) + " Ko, "
                                                    + std::to_string(b1m / 1024) + " Ko)");
    }
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_reseau_parties";
    fs::remove_all(dir);
    std::string big;   // ~ 420 Ko, une derniere ligne reconnaissable
    for (int i = 1; big.size() < 420 * 1024; ++i) big += "Pompe_" + std::to_string(i) + ";marche;" + std::to_string(i % 97) + ";defaut " + std::to_string(i % 13) + "\n";
    big += "DERNIERE_LIGNE;fin\n";
    writeFile(dir / "mesures.csv", big);
    const auto att = [&] {
        ai::ProviderAttachment a;
        a.name = "mesures.csv";
        a.mediaType = "text/csv";
        a.path = U(dir / "mesures.csv");
        a.bytes = fs::file_size(dir / "mesures.csv");
        return a;
    };
    check(ai::planAttachment(att()).note.empty(), "un texte de 420 Ko : plus d'annonce << coupe >>");
    // le corps : un grand modele (1 000 000 de jetons) - le fichier part ENTIER, en parties
    {
        ai::ProviderRequest r = simpleRequest("Quelles pompes sont en defaut ?");
        r.contextTokens = 1000000;
        r.turns[0].attachments = {att()};
        std::vector<std::string> notes;
        const std::string body = ai::anthropicBody(r, &notes);
        check(has(body, "DERNIERE_LIGNE") && has(body, "partie 1/5") && has(body, "partie 5/5") && notes.empty() && !has(body, "coup\xC3\xA9"),
              "1 000 000 de jetons : 420 Ko partent entiers, en 5 parties, sans note");
        const std::string oa = ai::openAIBody(r, true, true, &notes);
        check(has(oa, "DERNIERE_LIGNE") && has(oa, "partie 5/5") && notes.empty(), "compatible OpenAI : pareil");
    }
    // le corps sans lecture prealable : une petite fenetre - les premieres parties, le reste DIT (a l'IA et a l'utilisateur)
    {
        ai::ProviderRequest r = simpleRequest("Quelles pompes sont en defaut ?");
        r.contextTokens = 120000;
        r.turns[0].attachments = {att()};
        std::vector<std::string> notes;
        const std::string body = ai::anthropicBody(r, &notes);
        check(has(body, "partie 1/5") && !has(body, "DERNIERE_LIGNE") && has(body, "ne sont pas envoy") && notes.size() == 1
                  && has(notes[0], "ne tient pas entier"),
              "120 000 jetons, sans lecture prealable : les parties qui tiennent, le reste dit (" + (notes.empty() ? std::string{} : notes[0]) + ")");
    }
    // le fournisseur : les parties en trop LUES A PART (une demande chacune, sur le fil), leurs extraits a leur place
    useFakeNet();
    tp::forgetExtracts();
    {
        ai::ProviderRequest r = simpleRequest("Quelles pompes sont en defaut ?");
        r.contextTokens = 138000;   // ~ 255 Ko de place : 2 parties telles quelles (192 Ko), 3 lues a part
        r.turns[0].attachments = {att()};
        const auto room = tp::budgetBytes(r, 0);
        check(room > 225 * 1024 && room < 300 * 1024, "la place de l'essai : " + std::to_string(room / 1024) + " Ko");
        for (int i = 3; i <= 5; ++i) g_net.push(sseReply(streamText("EXTRAIT_" + std::to_string(i) + " : Pompe_9 en defaut")));
        g_net.push(sseReply(streamText("Pompe_9 est en defaut.")));
        ai::AnthropicProvider p(fastRetries());
        check(p.start(r), "start : la main rendue tout de suite (la lecture se fait sur le fil)");
        const auto ev = runToEnd(p, 10000);
        const std::size_t n = g_net.count();
        const std::string first = g_net.at(0).body, last = g_net.at(n - 1).body;
        int lectures = 0, prereadUsage = 0;
        std::string note;
        for (const auto& e : ev) {
            if (e.type == ai::ProviderEvent::Type::Status && e.text == "lecture") ++lectures;
            if (e.type == ai::ProviderEvent::Type::Status && e.text == "note") note = e.detail;
            if (e.type == ai::ProviderEvent::Type::Usage && e.preread) ++prereadUsage;
        }
        check(n == 4, "3 lectures prealables puis la demande : 4 requetes (" + std::to_string(n) + ")");
        check(has(first, "partie 3/5") && has(first, "Quelles pompes sont en defaut") && !has(first, "proposer_operations"),
              "une lecture prealable : la partie, la demande, sans outil");
        check(has(last, "partie 1/5") && has(last, "partie 2/5") && has(last, "EXTRAIT_3") && has(last, "EXTRAIT_5") && has(last, "lue \xC3\xA0 part")
                  && !has(last, "DERNIERE_LIGNE"),
              "la demande : 2 parties telles quelles + 3 extraits (rien de coupe en silence)");
        check(lectures == 3 && prereadUsage == 3 && allText(ev) == "Pompe_9 est en defaut.", "l'etat << Lecture de ... partie i/5 >> et les jetons des lectures");
        check(has(note, "ne tient pas entier") && has(note, "lue(s) \xC3\xA0 part"), "la note pour l'utilisateur : " + note);
        // la meme demande encore (un tour de plus, la piece dans un tour ancien) : les extraits gardes, pas relus
        g_net.reset();
        ai::ProviderRequest r2 = r;
        ai::ProviderTurn a1;
        a1.role = ai::ProviderTurn::Role::Assistant;
        a1.text = "Pompe_9 est en defaut.";
        ai::ProviderTurn u2;
        u2.text = "Et la pompe 12 ?";
        r2.turns.push_back(a1);
        r2.turns.push_back(u2);
        g_net.push(sseReply(streamText("Non.")));
        ai::AnthropicProvider p2(fastRetries());
        check(p2.start(r2), "le tour suivant : start");
        (void)runToEnd(p2, 10000);
        check(g_net.count() == 1 && has(g_net.at(0).body, "EXTRAIT_4"), "le tour suivant : les extraits gardes, aucune nouvelle lecture");
    }
    // une lecture qui echoue : la partie est DITE non lue, la demande part quand meme
    tp::forgetExtracts();
    g_net.reset();
    {
        ai::ProviderRequest r = simpleRequest("Quelles pompes sont en defaut ?");
        r.contextTokens = 138000;
        r.turns[0].attachments = {att()};
        g_net.push(errorReply(400, "invalid_request_error", "mauvaise demande"));
        g_net.push(errorReply(400, "invalid_request_error", "mauvaise demande"));
        g_net.push(errorReply(400, "invalid_request_error", "mauvaise demande"));
        g_net.push(sseReply(streamText("Je n'ai pas tout lu.")));
        ai::AnthropicProvider p(fastRetries());
        (void)p.start(r);
        const auto ev = runToEnd(p, 10000);
        const std::string last = g_net.at(g_net.count() - 1).body;
        check(allText(ev) == "Je n'ai pas tout lu." && has(last, "ne sont pas envoy") && has(last, "partie 1/5"),
              "les lectures echouent : la demande part, les parties non lues sont dites");
    }
    tp::forgetExtracts();
    fs::remove_all(dir);
}

// =============================================================================
//  7. De bout en bout : libcurl contre un serveur local (Linux)
// =============================================================================
#if !defined(_WIN32)
struct Served {
    std::string              head;
    std::vector<std::string> chunks;
    int                      delayMs{0};
    bool                     chunked{true};
    bool                     cut{false};
};

class LocalServer {
public:
    using Handler = std::function<Served(const std::string& method, const std::string& path, const std::string& body)>;
    explicit LocalServer(Handler h) : handler_(std::move(h)) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        ok_ = ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(fd_, 8) == 0;
        socklen_t len = sizeof a;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
        port_ = ntohs(a.sin_port);
        if (ok_) thread_ = std::thread([this] { loop(); });
    }
    ~LocalServer() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        ::close(fd_);
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<std::string> received() {
        std::lock_guard<std::mutex> lk(m_);
        return received_;
    }

private:
    static bool sendAll(int c, const std::string& s) {
        std::size_t at = 0;
        while (at < s.size()) {
            const ssize_t n = ::send(c, s.data() + at, s.size() - at, MSG_NOSIGNAL);
            if (n <= 0) return false;
            at += static_cast<std::size_t>(n);
        }
        return true;
    }
    void loop() {
        while (!stop_) {
            pollfd p{fd_, POLLIN, 0};
            if (::poll(&p, 1, 20) <= 0) continue;
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            serve(c);
            ::close(c);
        }
    }
    void serve(int c) {
        std::string in;
        char buf[4096];
        std::size_t end;
        while ((end = in.find("\r\n\r\n")) == std::string::npos) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            in.append(buf, static_cast<std::size_t>(n));
        }
        const std::string head = in.substr(0, end);
        std::string body = in.substr(end + 4);
        std::size_t length = 0;
        {
            std::string low = head;
            for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (const auto at = low.find("content-length:"); at != std::string::npos) length = std::strtoul(low.c_str() + at + 15, nullptr, 10);
        }
        while (body.size() < length) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            body.append(buf, static_cast<std::size_t>(n));
        }
        const auto sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
        const std::string method = head.substr(0, sp1), path = head.substr(sp1 + 1, sp2 - sp1 - 1);
        {
            std::lock_guard<std::mutex> lk(m_);
            received_.push_back(head + "\r\n\r\n" + body);
        }
        const Served s = handler_(method, path, body);
        if (!sendAll(c, s.head)) return;
        for (const auto& ch : s.chunks) {
            if (stop_) return;
            if (s.delayMs) pause(s.delayMs);
            char size[32];
            std::snprintf(size, sizeof size, "%zx\r\n", ch.size());
            if (!sendAll(c, s.chunked ? std::string(size) + ch + "\r\n" : ch)) return;
        }
        if (s.chunked && !s.cut) (void)sendAll(c, "0\r\n\r\n");
    }

    Handler                  handler_;
    int                      fd_{-1};
    int                      port_{0};
    bool                     ok_{false};
    std::atomic<bool>        stop_{false};
    std::thread              thread_;
    std::mutex               m_;
    std::vector<std::string> received_;
};

Served sseServed(const std::string& stream, std::size_t piece = 64, int delayMs = 0) {
    Served s;
    s.head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    for (std::size_t i = 0; i < stream.size(); i += piece) s.chunks.push_back(stream.substr(i, piece));
    s.delayMs = delayMs;
    return s;
}
Served jsonServed(int status, const std::string& reason, const std::string& body) {
    Served s;
    s.head = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size())
             + "\r\nConnection: close\r\n\r\n";
    s.chunks = {body};
    s.chunked = false;
    return s;
}
#endif

void testCurl() {
    std::printf("[libcurl, de bout en bout]\n");
#if defined(_WIN32)
    std::printf("  (sous Windows : WinHTTP, essaye dans l'exe)\n");
#else
    ai::setHttpFactory(nullptr);
    std::string why;
    if (!ai::httpAvailable(&why)) {
        std::printf("  (saute : %s)\n", why.c_str());
        return;
    }
    check(ai::httpTransportName().rfind("libcurl", 0) == 0, "le transport : " + ai::httpTransportName());
    LocalServer server([](const std::string& method, const std::string& path, const std::string& body) -> Served {
        if (method == "GET" && path.rfind("/v1/models", 0) == 0)
            return jsonServed(200, "OK", "{\"data\": [{\"id\": \"modele-essai\", \"display_name\": \"Essai\"}], \"has_more\": false}");
        if (has(body, "cle refusee")) return jsonServed(401, "Unauthorized", errorJson("authentication_error", "invalid x-api-key"));
        if (has(body, "coupure")) {
            Served s = sseServed(msgStart() + textStart(0) + textDelta(0, "Je commence"), 40);
            s.cut = true;
            return s;
        }
        if (has(body, "lentement")) {
            std::string stream = msgStart() + textStart(0);
            for (int k = 0; k < 60; ++k) stream += textDelta(0, "mot ");
            stream += blockStop(0) + msgDelta("end_turn") + msgStop();
            return sseServed(stream, 120, 60);
        }
        return sseServed(streamTextTool("toolu_curl", kTankInput), 23, 1);
    });
    check(server.ok(), "le serveur local ecoute (" + server.url() + ")");
    if (!server.ok()) return;
    {
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest();
        r.endpoint = server.url();
        check(p.start(r, &why), "start vers http://127.0.0.1 (" + why + ")");
        const auto ev = runToEnd(p, 10000);
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        check(allText(ev) == "Je propose une cuve." && tool && tool->toolUseId == "toolu_curl" && has(tool->toolInputJson, "Cuve_A"),
              "libcurl : le flux en morceaux (chunked), le texte et l'appel d'outil complet");
        const auto got = server.received();
        check(!got.empty() && has(got.front(), "POST /v1/messages") && has(got.front(), "x-api-key: cle-essai-SECRETE")
                  && has(got.front(), "anthropic-version: 2026-10-21") && has(got.front(), "\"stream\":true"),
              "la requete recue : POST /v1/messages, les en-tetes, le corps");
    }
    {
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest("cle refusee");
        r.endpoint = server.url();
        (void)p.start(r);
        const auto all5 = runToEnd(p, 10000);
        const auto* e = find(all5, ai::ProviderEvent::Type::Error);
        check(e && e->httpStatus == 401 && has(e->text, "Cl\xC3\xA9 API refus"), "libcurl : 401 lu (le corps JSON de l'erreur)");
    }
    {
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest("coupure");
        r.endpoint = server.url();
        (void)p.start(r);
        const auto ev = runToEnd(p, 10000);
        const auto* e = find(ev, ai::ProviderEvent::Type::Error);
        check(allText(ev) == "Je commence" && e && e->retryable && has(e->text, "coup"), "libcurl : la connexion coupee au milieu du flux, dite");
    }
    {
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest("lentement");
        r.endpoint = server.url();
        (void)p.start(r);
        std::vector<ai::ProviderEvent> ev;
        for (int k = 0; k < 3000 && allText(ev).size() < 8; ++k) {
            for (auto& e : p.poll()) ev.push_back(std::move(e));
            pause(2);
        }
        const auto t0 = std::chrono::steady_clock::now();
        p.cancel();
        for (auto& e : runToEnd(p, 3000)) ev.push_back(std::move(e));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const auto* d = find(ev, ai::ProviderEvent::Type::Done);
        check(d && d->stopReason == "arrete" && !p.busy() && ms < 1500 && allText(ev).size() < 240,
              "libcurl : Arreter ferme la connexion au milieu du flux (" + std::to_string(ms) + " ms)");
    }
    {
        ai::ModelQuery q;
        check(q.start(server.url(), "cle-essai", 30), "libcurl : Lister les modeles");
        std::optional<ai::ModelsResult> res;
        for (int k = 0; k < 5000 && !res; ++k) {
            res = q.poll();
            pause(1);
        }
        check(res && res->ok && res->models.size() == 1 && res->models[0].id == "modele-essai", "libcurl : GET /v1/models lu");
    }
    {
        // Un port ou personne n'ecoute : hors ligne, dit clairement.
        ai::AnthropicProvider p(fastRetries());
        auto r = simpleRequest();
        r.endpoint = "http://127.0.0.1:9";
        (void)p.start(r);
        const auto all6 = runToEnd(p, 10000);
        const auto* e = find(all6, ai::ProviderEvent::Type::Error);
        check(e && e->retryable && has(e->text, "Connexion impossible"), "libcurl : personne n'ecoute -> " + (e ? e->text : std::string("rien")));
    }
#endif
}

// =============================================================================
//  8. La cle
// =============================================================================
void testSecret() {
    std::printf("[cle]\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_reseau_cle";
    fs::remove_all(dir);
    ai::setRoot(U(dir / "IA"));
    const std::string key = "sk-ant-CLE-TRES-SECRETE-77";
#if !defined(_WIN32)
    check(!ai::keyProtectionAvailable() && has(ai::keyProtectionText(), "s\xC3\xA9" "ance"), "Linux : pas de coffre, la cle est gardee pour la seance");
    std::string why;
    check(!ai::storeKey(key, &why) && has(why, "jamais"), "Linux : memoriser refuse, dit (" + why + ")");
    check(!fs::exists(ai::keyFile()) && !ai::hasStoredKey() && !ai::loadStoredKey(), "Linux : aucun fichier");
#endif
    // Le chemin du fichier, avec une protection d'essai (reversible) : la cle n'y est pas en clair.
    ai::KeyProtector fake;
    fake.protect = [](const std::string& s) -> std::optional<std::string> {
        std::string o(s.rbegin(), s.rend());
        for (auto& c : o) c = static_cast<char>(c ^ 0x5A);
        return o;
    };
    fake.unprotect = [](const std::string& s) -> std::optional<std::string> {
        std::string o = s;
        for (auto& c : o) c = static_cast<char>(c ^ 0x5A);
        return std::string(o.rbegin(), o.rend());
    };
    ai::setKeyProtector(fake);
    check(ai::keyProtectionAvailable(), "une protection : memoriser est possible");
    check(ai::storeKey(key) && ai::hasStoredKey() && fs::exists(dir / "IA" / "cle.bin"), "memoriser : IA/cle.bin");
    const std::string file = readAll(dir / "IA" / "cle.bin");
    check(!file.empty() && !has(file, key) && !has(file, "SECRETE"), "le fichier relu : la cle n'y est PAS en clair");
    auto back = ai::loadStoredKey();
    check(back && *back == key, "relue : la meme cle");
    writeFile(dir / "IA" / "cle.bin", "n'importe quoi");
    std::string why2;
    check(!ai::loadStoredKey(&why2) && has(why2, "illisible"), "un fichier abime : illisible, dit");
    check(ai::forgetStoredKey() && !fs::exists(dir / "IA" / "cle.bin"), "oublier : le fichier part");
    ai::setKeyProtector({});
}


// =============================================================================
//  9. L'Assistant branche : le faux fournisseur, le panneau, les parametres
// =============================================================================
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

// Le faux fournisseur : il retient les requetes et rend des evenements prepares.
struct FakeScript {
    std::vector<ai::ProviderRequest>           requests;
    std::deque<std::vector<ai::ProviderEvent>> replies;
    bool                                       hang{false};
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

ai::ProviderEvent ev(ai::ProviderEvent::Type t, std::string text = {}) {
    ai::ProviderEvent e;
    e.type = t;
    e.text = std::move(text);
    return e;
}
ai::ProviderEvent toolEv(const std::string& id, const std::string& input) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::ToolUse;
    e.toolUseId = id;
    e.toolName = "proposer_operations";
    e.toolInputJson = input;
    return e;
}
ai::ProviderEvent doneEv(const std::string& reason) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Done;
    e.stopReason = reason;
    return e;
}
ai::ProviderEvent errEv(const std::string& text, int status, bool retryable) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Error;
    e.text = text;
    e.httpStatus = status;
    e.retryable = retryable;
    return e;
}

struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    std::vector<hmi::Id>         selection;
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    std::vector<std::string>     statuses;
    double                       t{100.0};

    explicit Fixture(const std::string& name, bool fresh = true) {
        dir = fs::temp_directory_path() / ("xpg_ai_reseau_" + name);
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
        p.programs.variables.push_back(var);
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
    // Un vrai fil (le fournisseur reel) : jusqu'a la fin de la reponse, au plus 5 s.
    void waitLive() {
        for (int k = 0; k < 2500 && as().generating(); ++k) {
            step(0.002);
            pause(2);
        }
        step();
    }
    void configure(const std::string& model = "modele-essai", const std::string& key = "cle-essai-SECRETE", const std::string& endpoint = {}) {
        ai::Settings st = as().settings();
        st.provider = "anthropic";
        st.model = model;
        st.endpoint = endpoint;
        as().setSettings(st);
        as().setApiKey(key);
        as().newConversation();
        panel->sync();
        panel->layout();
    }
    [[nodiscard]] const hmi::Object* object(const std::string& name) {
        for (const auto& o : v().objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    [[nodiscard]] const ai::Message& last() { return as().current()->messages.back(); }
    [[nodiscard]] int lastIndex() { return static_cast<int>(as().current()->messages.size()) - 1; }
    void render(Recorder& rec) {
        panel->layout();
        panel->render(ui::PaintContext{rec, ui::Theme::dark(), panel->bounds(), t, nullptr});
    }
};

void testAssistantLive() {
    std::printf("[assistant branche]\n");
    Fixture f("live");
    FakeScript s;
    f.as().setProviderFactory([&s](std::string_view) { return std::make_unique<FakeProvider>(s); });
    // A configurer.
    {
        ai::Settings st = f.as().settings();
        st.provider = "anthropic";
        f.as().setSettings(st);
        f.panel->sync();
    }
    check(f.as().live() && f.as().needsSetup() && has(f.as().connectionText(), "configurer"), "Anthropic sans modele : " + f.as().connectionText());
    check(f.as().outgoingSummary().empty(), "a configurer : ce qui part n'est pas dit (rien ne peut partir)");
    check(!f.panel->sendText("Une cuve") && has(f.panel->lastStatus(), "mod\xC3\xA8le") && s.requests.empty(), "envoyer : refuse, dit pourquoi");
    f.configure();
    check(!f.as().needsSetup() && f.as().connectionText() == "Anthropic \xE2\x80\x93 modele-essai", "configure : " + f.as().connectionText());
    check(has(f.as().outgoingSummary(), "Vue_Station") && has(f.as().outgoingSummary(), "1 variable") && f.panel->outgoingRect().h > 0.f,
          "avant le premier envoi : ce qui part (" + f.as().outgoingSummary() + ")");

    // Envoyer -> texte -> proposition validee.
    s.replies.push_back({ev(ai::ProviderEvent::Type::Status, "reflexion"), ev(ai::ProviderEvent::Type::Text, "Je propose "),
                         ev(ai::ProviderEvent::Type::Text, "une cuve."), toolEv("toolu_1", kTankInput), doneEv("tool_use")});
    check(f.panel->sendText("Ajoute une cuve"), "envoyer : part");
    check(s.requests.size() == 1 && f.as().generating() && f.as().phase() == ai::Assistant::Phase::Waiting, "le fournisseur demarre ; en attente");
    check(f.as().outgoingSummary().empty(), "pendant la reponse : ce qui part n'est plus dit");
    if (!s.requests.empty()) {
        const auto& r = s.requests[0];
        check(r.model == "modele-essai" && r.apiKey == "cle-essai-SECRETE" && r.maxTokens == 16000 && r.timeoutSeconds == 60,
              "la requete : le modele, la cle (en memoire), 16000, le delai");
        check(!r.system.empty() && has(r.toolsJson, "proposer_operations"), "les consignes (systemPrompt) et l'outil (operationsTool)");
        check(!r.systemStable.empty() && r.system == r.systemStable + r.systemDynamic && has(r.systemDynamic, "Vue_Station"),
              "les consignes en deux morceaux : le stable (en cache), le dynamique (le contexte de la vue)");
        check(r.turns.size() == 1 && r.turns[0].text == "Ajoute une cuve" && r.turns[0].toolResultFor.empty(), "un tour : la demande");
    }
    f.step();
    check(!f.as().generating(), "la reponse est finie");
    const int m1 = f.lastIndex();
    {
        const auto& m = f.last();
        check(m.text == "Je propose une cuve." && m.state == "fini" && m.provider == "anthropic" && m.model == "modele-essai", "le texte, fini, qui a repondu");
        check(m.tool && m.tool->id == "toolu_1" && m.tool->name == "proposer_operations" && has(m.tool->input, "Cuve_A") && m.tool->error.empty(),
              "l'appel d'outil garde");
        check(m.proposal && m.proposal->state == "proposee" && m.proposal->create == 1 && m.proposal->request == "Ajoute une cuve",
              "la proposition : 1 objet a creer");
        check(f.as().check(m1).ok(), "validee");
    }
    check(f.as().outgoingSummary().empty() && f.panel->outgoingRect().h == 0.f, "ce qui part : dit une fois (avant le premier envoi)");
    // Appliquer : un Ctrl+Z.
    check(f.as().apply(m1) && f.object("Cuve_A") && f.stack.canUndo(), "Appliquer : la cuve est dans la vue");
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Parfait."), doneEv("end_turn")});
    (void)f.panel->sendText("Merci");
    f.step();
    if (s.requests.size() == 2) {
        const auto& r = s.requests[1];
        check(r.turns.size() == 3, "trois tours");
        if (r.turns.size() == 3) {
            check(r.turns[1].role == ai::ProviderTurn::Role::Assistant && r.turns[1].toolUseId == "toolu_1" && has(r.turns[1].toolInputJson, "Cuve_A"),
                  "le tour de l'assistant porte son appel d'outil");
            check(r.turns[2].toolResultFor == "toolu_1" && r.turns[2].toolResultText.rfind("appliqu\xC3\xA9" "e", 0) == 0 && !r.turns[2].toolResultError,
                  "le tour suivant porte le resultat reel : \"" + r.turns[2].toolResultText + "\"");
        }
    } else {
        check(false, "la seconde requete n'est pas partie");
    }
    (void)f.stack.undo();
    {
        const auto req = f.as().requestFor(*f.as().current(), f.as().current()->messages.size());
        check(req.turns.size() == 4 && has(req.turns[2].toolResultText, "annul\xC3\xA9" "e (Ctrl+Z)"), "apres Ctrl+Z : \"annulee (Ctrl+Z)\"");
    }
    (void)f.stack.redo();

    // Une proposition invalide : le tour suivant porte l'erreur de validation.
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Voici."), toolEv("toolu_2", kInventedInput), doneEv("tool_use")});
    (void)f.panel->sendText("Une cuve rouge");
    f.step();
    const int m2 = f.lastIndex();
    check(f.last().proposal && !f.as().check(m2).ok(), "une propriete inventee : la proposition ne passe pas la validation");
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Pardon."), doneEv("end_turn")});
    (void)f.panel->sendText("Pourquoi ?");
    f.step();
    check(!s.requests.empty() && s.requests.back().turns.back().toolResultFor == "toolu_2" && s.requests.back().turns.back().toolResultError
              && has(s.requests.back().turns.back().toolResultText, "refus\xC3\xA9" "e par le logiciel"),
          "le tour suivant : tool_result en erreur (" + s.requests.back().turns.back().toolResultText + ")");

    // Une entree illisible : pas de proposition, la note le dit, le tour suivant porte l'erreur.
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Voici."), toolEv("toolu_3", kUnreadableInput), doneEv("tool_use")});
    (void)f.panel->sendText("Fais quelque chose");
    f.step();
    check(!f.last().proposal && f.last().tool && !f.last().tool->error.empty() && has(f.last().note, "illisible"),
          "une operation inconnue : refusee a la lecture, dite (" + f.last().note + ")");
    {
        const auto req = f.as().requestFor(*f.as().current(), f.as().current()->messages.size());
        ai::ProviderRequest withNext = req;
        const auto [text, error] = f.as().toolResult(f.last());
        check(error && has(text, "invalide"), "son resultat pour le modele : une erreur (" + text + ")");
    }

    // max_tokens, refusal : dits, rien n'est propose.
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Je commence"), doneEv("max_tokens")});
    (void)f.panel->sendText("Fais tout");
    f.step();
    check(!f.last().proposal && has(f.last().note, "coup\xC3\xA9" "e"), "max_tokens : la reponse coupee est dite");
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Non."), doneEv("refusal")});
    (void)f.panel->sendText("Autre chose");
    f.step();
    check(has(f.last().note, "d\xC3\xA9" "clin"), "refusal : la demande declinee est dite");

    // Une erreur : le message "erreur", Reessayer.
    s.replies.push_back({errEv("Cl\xC3\xA9 API refus\xC3\xA9" "e (401) : v\xC3\xA9rifie-la.", 401, false)});
    (void)f.panel->sendText("Bonjour");
    f.step();
    check(f.last().state == "erreur" && has(f.last().note, "401") && f.as().canRetry(), "401 : un message en erreur, Reessayer possible");
    check(f.panel->chat().retryButton() && f.panel->chat().retryButton()->visibility() == ui::Visibility::Visible, "le bouton Reessayer est la");
    {
        Recorder rec;
        f.render(rec);
        check(rec.saw("401") && rec.saw("R\xC3\xA9" "essayer"), "dessine : l'erreur et Reessayer");
    }
    const std::size_t before = f.as().current()->messages.size();
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Bonjour !"), doneEv("end_turn")});
    f.panel->chat().retryButton()->clicked->emit();
    f.step();
    check(f.as().current()->messages.size() == before && f.last().text == "Bonjour !" && f.last().state == "fini", "Reessayer : le message en erreur remplace");
    check(!s.requests.empty() && s.requests.back().turns.back().text == "Bonjour", "la meme demande, sans le tour en erreur");

    // Arreter.
    s.hang = true;
    s.replies.push_back({});
    (void)f.panel->sendText("Une longue reponse");
    f.step();
    check(f.as().generating(), "en cours");
    const int cancelsBefore = s.cancels;
    f.panel->stop();
    s.hang = false;
    check(!f.as().generating() && s.cancels == cancelsBefore + 1 && f.last().state == "arrete" && f.as().canRetry(),
          "Arreter : le fournisseur annule, le message arrete, Reessayer possible");

    // La cle n'est ecrite nulle part ; l'appel d'outil est sur le disque.
    bool keyAnywhere = false;
    for (const auto& e : fs::recursive_directory_iterator(f.dir / "IA"))
        if (e.is_regular_file() && has(readAll(e.path()), "SECRETE")) keyAnywhere = true;
    check(!keyAnywhere, "la cle API n'est dans aucun fichier de IA/");
    // La persistance : relu d'un redemarrage.
    const std::string convId = f.as().current()->id;
    Fixture g("live", false);
    const int gi = g.as().indexOf(convId);
    check(gi >= 0, "la conversation relue");
    if (gi >= 0) {
        g.as().select(convId);
        const auto& conv = *g.as().current();
        const auto& m = conv.messages[static_cast<std::size_t>(m1)];
        check(m.tool && m.tool->id == "toolu_1" && m.tool->name == "proposer_operations" && has(m.tool->input, "Cuve_A") && m.provider == "anthropic"
                  && m.model == "modele-essai",
              "l'appel d'outil relu (id, nom, entree), qui a repondu");
        ai::Settings st = g.as().settings();
        check(st.provider == "anthropic" && st.model == "modele-essai", "les parametres relus");
        const auto req = g.as().requestFor(conv, conv.messages.size());
        bool carried = false;
        for (const auto& t : req.turns)
            if (t.toolUseId == "toolu_1") carried = true;
        check(carried, "relue, la conversation renvoie l'appel d'outil au modele");
        check(g.as().apiKey().empty() && g.as().needsSetup(), "Linux : la cle n'est pas revenue (gardee pour la seance seulement)");
    }
}

void testDirectAndDemo() {
    std::printf("[mode direct, demonstration]\n");
    Fixture f("direct");
    FakeScript s;
    int made = 0;
    f.as().setProviderFactory([&s, &made](std::string_view) {
        ++made;
        return std::make_unique<FakeProvider>(s);
    });
    // La demonstration : son chemin (aucun fournisseur cree).
    check(!f.as().live(), "par defaut : la demonstration");
    f.as().newConversation();
    (void)f.panel->sendText("Ajoute une cuve avec un indicateur de niveau");
    for (int k = 0; k < 4000 && f.as().generating(); ++k) f.step();
    check(made == 0 && f.last().proposal && f.last().provider.empty(), "la demonstration repond sans fournisseur");
    // Le mode Direct : la proposition s'applique des la reponse.
    f.configure();
    f.as().setDirectMode(true);
    s.replies.push_back({ev(ai::ProviderEvent::Type::Text, "Voici."), toolEv("toolu_d", kTankInput), doneEv("tool_use")});
    (void)f.panel->sendText("Une cuve");
    f.step();
    check(made == 1 && f.last().proposal && f.last().proposal->state == "appliquee" && f.object("Cuve_A"), "mode Direct : appliquee des la reponse");
}

void testAssistantRealProvider() {
    std::printf("[assistant, vrai fournisseur sur le faux transport]\n");
    useFakeNet();
    Fixture f("vrai");
    f.configure("modele-essai", "cle-essai-SECRETE", "http://127.0.0.1:8765");
    check(f.as().connectionText() == "Anthropic \xE2\x80\x93 modele-essai (relais)", "un relais : " + f.as().connectionText());
    g_net.push(sseReply(streamTextTool("toolu_h1", kTankInput)));
    check(f.panel->sendText("Ajoute une cuve"), "envoyer");
    f.waitLive();
    const int m = f.lastIndex();
    check(f.last().proposal && f.last().tool && f.last().tool->id == "toolu_h1", "la proposition, par le vrai fournisseur (fil, poll)");
    check(g_net.count() == 1 && g_net.at(0).url == "http://127.0.0.1:8765/v1/messages", "vers le relais : <adresse>/v1/messages");
    check(f.as().apply(m) && f.object("Cuve_A"), "appliquee");
    g_net.push(sseReply(streamText("Merci.")));
    (void)f.panel->sendText("Merci");
    f.waitLive();
    const std::string body = g_net.at(1).body;
    Value v;
    check(ai::json::parse(body, v), "le second corps est du JSON");
    const auto& msgs = v.at("messages").items();
    bool ok = false;
    if (msgs.size() == 3) {
        const auto& c = msgs[2].at("content").items();
        ok = !c.empty() && c[0].str("type") == "tool_result" && c[0].str("tool_use_id") == "toolu_h1" && c[0].str("content").rfind("appliqu", 0) == 0;
    }
    check(ok, "le corps du tour suivant : tool_result \"appliquee\" en tete");
    // Une erreur du service, puis Reessayer.
    g_net.push(errorReply(404, "not_found_error", "model: modele-essai"));
    (void)f.panel->sendText("Encore");
    f.waitLive();
    check(f.last().state == "erreur" && has(f.last().note, "Mod\xC3\xA8le introuvable"), "404 : le modele introuvable, dans la conversation");
    g_net.push(sseReply(streamText("Voila.")));
    check(f.panel->retry(), "Reessayer");
    f.waitLive();
    check(f.last().text == "Voila.", "la reponse apres Reessayer");
    // Arreter : le fournisseur finit sur son fil sans bloquer.
    Scripted hang = sseReply(msgStart() + textStart(0) + textDelta(0, "Un"));
    hang.hang = true;
    g_net.push(hang);
    (void)f.panel->sendText("Longue");
    for (int k = 0; k < 1000 && f.as().shownText().empty(); ++k) {
        f.step(0.002);
        pause(2);
    }
    const auto t0 = std::chrono::steady_clock::now();
    f.panel->stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(!f.as().generating() && f.last().state == "arrete" && has(f.last().text, "Un") && ms < 200,
          "Arreter : tout de suite (" + std::to_string(ms) + " ms), le texte recu garde");
    for (int k = 0; k < 200; ++k) {
        f.step(0.002);
        pause(2);
    }
    ai::setHttpFactory(nullptr);
}

// =============================================================================
//  1.20.0 : une GROSSE proposition se verifie sur un fil de fond - la carte montre l'avancement, l'interface ne gele pas
// =============================================================================
void testBackgroundCard() {
    std::printf("[grosse proposition : verification de fond]\n");
    useFakeNet();
    Fixture f("fond");
    f.configure();
    std::string input = "{\"resume\": \"Beaucoup de rectangles\", \"operations\": [";
    for (int i = 0; i < 80; ++i) {
        if (i) input += ", ";
        input += "{\"action\": \"creer\", \"genre\": \"Rectangle\", \"nom\": \"R_" + std::to_string(i) + "\", \"proprietes\": {\"x\": \""
                 + std::to_string(10 + (i % 10) * 60) + "\", \"y\": \"" + std::to_string(10 + (i / 10) * 50) + "\", \"w\": \"40\", \"h\": \"30\"}}";
    }
    input += "]}";
    g_net.push(sseReply(streamTextTool("toolu_big", input)));
    check(f.panel->sendText("Ajoute 80 rectangles"), "envoyer");
    for (int k = 0; k < 2500 && f.as().generating(); ++k) {
        f.step(0.002);
        pause(2);
    }
    const int m = f.lastIndex();
    check(f.last().proposal && f.last().proposal->ops.size() == 80, "la proposition : 80 operations");
    // La mise en page qui suit la reponse a lance la verification sur un fil de fond : la carte attend, l'interface est libre.
    check(f.panel->chat().checking(m), "80 operations : la carte se verifie sur un fil de fond");
    Recorder rec;
    f.render(rec);   // la carte en verification : l'avancement se dessine (sans plantage)
    bool said = false;
    for (const auto& tx : rec.texts) said = said || has(tx, "rification");
    check(said, "la carte dit << Verification... >> pendant le travail de fond");
    for (int k = 0; k < 2500 && f.panel->chat().checking(m); ++k) {
        f.step(0.002);
        pause(2);
    }
    check(!f.panel->chat().checking(m), "la verification de fond se termine (le resultat arrive au tick)");
    f.panel->chat().rebuild();
    f.panel->layout();
    auto* apply = f.panel->chat().cardButton(m, 1);
    check(apply && apply->enabled(), "le resultat est la : Appliquer est disponible");
    check(f.as().apply(m) && f.object("R_79"), "appliquee : les 80 rectangles");
    // Une petite proposition reste verifiee tout de suite (pas de fil pour si peu).
    g_net.push(sseReply(streamTextTool("toolu_small", kTankInput)));
    (void)f.panel->sendText("Ajoute une cuve");
    f.waitLive();
    check(!f.panel->chat().checking(f.lastIndex()), "une petite proposition : verifiee tout de suite");
}

void testSettingsDialog() {
    std::printf("[parametres]\n");
    useFakeNet();
    Fixture f("parametres");
    f.panel->openSettings();
    f.mm.applyPending();
    auto* d = dynamic_cast<ai::AiSettingsDialog*>(f.mm.top());
    check(d != nullptr, "la fenetre s'ouvre");
    if (!d) return;
    auto* provider = dynamic_cast<ui::DropDown*>(d->field("fournisseur"));
    auto* model = dynamic_cast<ui::InputText*>(d->field("modele"));
    auto* effort = dynamic_cast<ui::DropDown*>(d->field("effort"));
    auto* key = dynamic_cast<ui::InputText*>(d->field("cle"));
    auto* remember = dynamic_cast<ui::Checkbox*>(d->field("memoriser"));
    check(provider && model && effort && key && remember && d->field("lister") && d->field("tester"), "les champs (modele, lister, effort, memoriser, tester)");
    if (!provider || !model || !effort || !key || !remember) return;
    int anthropic = -1;
    for (std::size_t i = 0; i < provider->items().size(); ++i)
        if (provider->items()[i].value == "anthropic") anthropic = static_cast<int>(i);
    check(anthropic >= 0 && provider->items()[static_cast<std::size_t>(anthropic)].enabled
              && provider->items()[static_cast<std::size_t>(anthropic)].label == "Anthropic (Claude)",
          "le fournisseur Anthropic (Claude) : disponible");
    check(model->text().empty(), "le modele : vide par defaut (rien en dur)");
    {
        Recorder rec;
        d->widgetRoot()->setBounds({0, 0, 1600, 900});
        d->widgetRoot()->layout();
        d->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), {0, 0, 1600, 900}, 0.0, nullptr});
        check(rec.saw("ex. claude-opus-5-5"), "son indication grise : un exemple");
    }
    check(effort->items().size() == 4 && effort->selectedIndex() == 0, "l'effort : Auto, Bas, Moyen, Haut (Auto par defaut)");
#if !defined(_WIN32)
    check(!remember->enabled(), "Linux : Memoriser la cle est grisee");
#endif
    // La demonstration : pas de connexion a tester.
    check(!d->testConnection() && has(d->resultText(), "d\xC3\xA9monstration"), "Tester en demonstration : dit");
    provider->setSelectedIndex(anthropic);
    key->setText("cle-essai");
    Scripted list;
    list.chunks = {"{\"data\": [{\"id\": \"modele-essai\", \"display_name\": \"Essai\"}, {\"id\": \"modele-rapide\", \"display_name\": \"Rapide\"},"
                   " {\"id\": \"modele-soigne\", \"display_name\": \"Soign\xC3\xA9\"}]}"};
    g_net.push(list);
    check(d->listModels(), "Lister les modeles : part");
    bool got = false;
    for (int k = 0; k < 2000 && !got; ++k) {
        got = d->pollQuery();
        pause(1);
    }
    check(got && d->resultOk() && has(d->resultText(), "3 mod\xC3\xA8les") && d->modelsMenu()->isOpen(), "la liste arrive, la liste a choisir s'ouvre");
    check(d->chooseModel(1) && model->text() == "modele-rapide" && !d->modelsMenu()->isOpen(), "choisir : le modele dans la case");
    g_net.push(errorReply(401, "authentication_error", "invalid x-api-key"));
    (void)d->testConnection();
    got = false;
    for (int k = 0; k < 2000 && !got; ++k) {
        got = d->pollQuery();
        pause(1);
    }
    check(got && !d->resultOk() && has(d->resultText(), "Cl\xC3\xA9 API refus"), "Tester : 401 dit clairement (" + d->resultText() + ")");
    g_net.push(list);
    (void)d->testConnection();
    got = false;
    for (int k = 0; k < 2000 && !got; ++k) {
        got = d->pollQuery();
        pause(1);
    }
    check(got && d->resultOk() && d->resultText() == "Connexion r\xC3\xA9ussie : 3 mod\xC3\xA8les.", "Tester : " + d->resultText());
    {
        Recorder rec;
        d->widgetRoot()->setBounds({0, 0, 1600, 900});
        d->widgetRoot()->layout();
        d->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), {0, 0, 1600, 900}, 0.0, nullptr});
        check(rec.saw("Connexion r\xC3\xA9ussie") && rec.saw("Tester la connexion") && rec.saw("Lister les mod\xC3\xA8les"), "dessine : le resultat, les boutons");
    }
    effort->setSelectedIndex(3);
    d->save();
    f.mm.applyPending();
    const auto st = f.as().settings();
    check(st.provider == "anthropic" && st.model == "modele-rapide" && st.effort == "high" && f.as().apiKey() == "cle-essai",
          "Enregistrer : le fournisseur, le modele, l'effort ; la cle en memoire");
    const std::string json = readAll(f.dir / "IA" / "parametres.json");
    check(has(json, "\"effort\": \"high\"") && has(json, "\"fournisseur\": \"anthropic\"") && !has(json, "cle-essai"), "parametres.json : sans la cle");
    check(!fs::exists(f.dir / "IA" / "cle.bin"), "Linux : pas de cle.bin");
    // Le panneau : l'etat de connexion ; un clic dessus ouvre les parametres.
    Recorder rec;
    f.render(rec);
    check(rec.saw("Anthropic \xE2\x80\x93 modele-rapide"), "l'en-tete du panneau dit l'etat reel");
    const auto r = f.panel->statusRect();
    (void)f.panel->dispatch(ui::MouseDown{{r.x + 30.f, r.y + r.h / 2.f}, ui::MouseButton::Left, 1, {}});
    f.mm.applyPending();
    check(dynamic_cast<ai::AiSettingsDialog*>(f.mm.top()) != nullptr, "un clic sur l'etat de connexion : les parametres");
    ai::setHttpFactory(nullptr);
}

// Le faux fournisseur Python (tests/fixtures/ia/faux_fournisseur.py), s'il tourne : XPG_FAUX_FOURNISSEUR=http://127.0.0.1:8765.
// Sa station de pompage passe la validation du logiciel ; ses erreurs sont dites ; Reessayer apres une surcharge.
void testFauxFournisseur() {
    const char* url = std::getenv("XPG_FAUX_FOURNISSEUR");
    if (!url || !*url) return;
    std::printf("[faux fournisseur local : %s]\n", url);
    ai::setHttpFactory(nullptr);
    Fixture f("faux");
    f.configure("modele-essai", "cle-essai", url);
    const auto wait = [&] {
        for (int k = 0; k < 5000 && f.as().generating(); ++k) {
            f.step(0.002);
            pause(2);
        }
        f.step();
    };
    (void)f.panel->sendText("Cr\xC3\xA9" "e-moi un synoptique de station de pompage");
    wait();
    const int m = f.lastIndex();
    check(f.last().proposal && f.last().proposal->ops.size() == 31, "la station : 31 operations (" + f.last().note + ")");
    const auto ck = f.as().check(m);
    std::string errs;
    for (const auto& e : ck.errors) errs += e + " ; ";
    check(ck.ok() && ck.create == 31, "elle passe la validation (" + errs + ")");
    check(f.as().apply(m) && f.object("Cuve_1") && f.object("Arret_P32"), "appliquee : les objets dans la vue");
    (void)f.panel->sendText("Merci");
    wait();
    check(has(f.last().text, "appliqu"), "le faux fournisseur a recu \"appliquee\" : " + f.last().text);
    (void)f.panel->sendText("une propriete inventee");
    wait();
    check(f.last().proposal && !f.as().check(f.lastIndex()).ok(), "une propriete inventee : refusee par la validation");
    (void)f.panel->sendText("Et alors ?");
    wait();
    check(has(f.last().text, "refus"), "le faux fournisseur a recu le refus : " + f.last().text);
    (void)f.panel->sendText("illisible");
    wait();
    check(!f.last().proposal && !f.last().note.empty(), "une operation inconnue : " + f.last().note);
    (void)f.panel->sendText("coupure");
    wait();
    check(f.last().state == "erreur" && f.as().canRetry(), "la coupure : " + f.last().note);
    (void)f.panel->sendText("refus");
    wait();
    check(has(f.last().note, "d\xC3\xA9" "clin"), "refusal : " + f.last().note);
    ai::Settings st = f.as().settings();
    st.model = "introuvable";
    f.as().setSettings(st);
    (void)f.panel->sendText("Bonjour");
    wait();
    check(has(f.last().note, "introuvable"), "404 : " + f.last().note);
}

} // namespace

// 1.15.0 (captures sous Wine) : un vrai fournisseur passe a l'ecriture AVANT le premier mot (le bloc de
// texte s'ouvre, les mots suivent ; un flux lent les espace). Le panneau dessinait alors le curseur
// d'ecriture en lisant la derniere ligne d'une liste vide : l'application plantait (au premier envoi d'une
// reponse lente, et a Reessayer apres Arreter).
class SlowStartProvider final : public ai::Provider {
public:
    explicit SlowStartProvider(int& starts) : starts_(starts) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ai::ProviderRequest, std::string*) override {
        ++starts_;
        sent_ = false;
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        std::vector<ai::ProviderEvent> out;
        if (busy_ && !sent_) out.push_back(ev(ai::ProviderEvent::Type::Status, "ecriture"));
        sent_ = true;
        return out;
    }
    void cancel() override { busy_ = false; }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    int& starts_;
    bool sent_{false}, busy_{false};
};

void testWritingBeforeText() {
    std::printf("[l'ecriture avant le premier mot]\n");
    Fixture f("ecriture_vide");
    int starts = 0;
    f.as().setProviderFactory([&starts](std::string_view) { return std::make_unique<SlowStartProvider>(starts); });
    f.configure();
    check(f.panel->sendText("lent"), "envoyer");
    f.step();
    f.step();
    check(f.as().phase() == ai::Assistant::Phase::Writing && f.as().shownText().empty(), "l'ecriture commence, aucun mot encore");
    int frames = 0;
    for (int k = 0; k < 24; ++k) {   // une seconde et plus : le curseur clignote (visible, puis cache)
        f.step(0.05);
        Recorder rec;
        f.render(rec);
        ++frames;
    }
    check(frames == 24, "le panneau se dessine pendant l'ecriture sans mot, curseur visible puis cache (" + std::to_string(frames) + " images)");
    f.as().stop();
    f.step();
    check(!f.as().generating() && f.last().state == "arrete", "Arreter : la reponse vide est gardee, arretee");
    check(f.panel->retry() && starts == 2, "Reessayer : la demande repart");
    f.step();
    f.step();
    check(f.as().phase() == ai::Assistant::Phase::Writing && f.as().shownText().empty(), "Reessayer : l'ecriture recommence, aucun mot encore");
    for (int k = 0; k < 24; ++k) {
        f.step(0.05);
        Recorder rec;
        f.render(rec);
    }
    check(f.as().generating(), "apres Reessayer : le panneau se dessine, la reponse attend ses mots");
    f.as().stop();
    f.step();
}

int main() {
    ai::setClock([] { return fixedNow(); });
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        ui::installPlatformServices(std::move(s));
    }
    testUrls();
    testSse();
    testBody();
    testStream();
    testApiErrors();
    testProvider();
    testTextParts();
    testCurl();
    testSecret();
    testAssistantLive();
    testWritingBeforeText();
    testDirectAndDemo();
    testAssistantRealProvider();
    testBackgroundCard();
    testSettingsDialog();
    testFauxFournisseur();
    std::printf("ai_reseau_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
