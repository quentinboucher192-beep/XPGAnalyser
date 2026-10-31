// =============================================================================
//  tests/ai_compatible_test.cpp - 1.16.0 : les IA gratuites (compatibles OpenAI) et la limite en direct
// -----------------------------------------------------------------------------
//  Sans cle ni reseau exterieur (un faux transport, un serveur local) :
//    - les services et leurs adresses (http:// vers ce PC ; vers l'usine sans cle) ;
//    - le corps compatible OpenAI (messages, outils, images, tool_calls / tool) ;
//    - le flux (texte, tool_calls coupes partout et concatenes par index, usage,
//      [DONE], une coupure, une erreur) ; les erreurs ;
//    - le modele sans outil (la reprise sans outil, le bloc JSON lu strictement,
//      un bloc illisible refuse) ;
//    - les en-tetes de limite (Anthropic, OpenAI, durees, dates RFC 3339) ;
//    - l'usage cumule (AiUsage : jour, mois, conversation, le fichier relu, ni
//      cle ni demande dedans) ;
//    - la bande du panneau (les textes, le compte a rebours qui avance sans envoi,
//      le budget a 80 % et depasse -> la confirmation) ;
//    - les parametres (le service remplit l'adresse ; providerNeedsKey) ;
//    - de bout en bout avec libcurl contre un serveur local compatible OpenAI.
// =============================================================================
#include "../src/app/ai/AiAnthropic.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiOpenAI.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/app/ai/AiSettingsDialog.hpp"
#include "../src/app/ai/AiUsage.hpp"
#include "../src/app/hmi/HmiAskDialog.hpp"
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

std::time_t g_now = 0;
std::time_t at(int year, int month, int day, int hour = 12) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

ui::Button* findButton(ui::Widget& root, std::string_view text) {
    if (auto* b = dynamic_cast<ui::Button*>(&root); b && b->text() == text) return b;
    for (const auto& c : root.children())
        if (auto* f = findButton(*c, text)) return f;
    return nullptr;
}

// ---- le flux compatible OpenAI, ecrit avec le JSON du logiciel ---------------------------------
Value obj(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value o = Value::object();
    for (const auto& [k, v] : kv) o.set(k, v);
    return o;
}
std::string data(const Value& v) { return "data: " + ai::json::write(v, false) + "\n\n"; }
std::string chunkOf(const Value& delta, const char* finish = nullptr) {
    Value c = obj({{"index", 0}, {"delta", delta}});
    c.set("finish_reason", finish ? Value(finish) : Value{});
    Value choices = Value::array();
    choices.push(c);
    return data(obj({{"id", "chatcmpl-1"}, {"object", "chat.completion.chunk"}, {"choices", choices}}));
}
std::string textChunk(const std::string& t) { return chunkOf(obj({{"content", t}})); }
std::string callChunk(int index, const std::string& id, const std::string& name, const std::string& args) {
    Value fn = Value::object();
    if (!name.empty()) fn.set("name", name);
    fn.set("arguments", args);
    Value call = obj({{"index", index}, {"function", fn}});
    if (!id.empty()) {
        call.set("id", id);
        call.set("type", "function");
    }
    Value calls = Value::array();
    calls.push(call);
    return chunkOf(obj({{"tool_calls", calls}}));
}
std::string finishChunk(const char* reason) { return chunkOf(Value::object(), reason); }
std::string usageChunk(int prompt, int completion, int cached = 0) {
    Value u = obj({{"prompt_tokens", prompt}, {"completion_tokens", completion}, {"total_tokens", prompt + completion}});
    if (cached) u.set("prompt_tokens_details", obj({{"cached_tokens", cached}}));
    return data(obj({{"id", "chatcmpl-1"}, {"choices", Value::array()}, {"usage", u}}));
}
const std::string kDone = "data: [DONE]\n\n";

const std::string kTankInput =
    "{\"resume\": \"Une cuve\", \"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_A\", "
    "\"proprietes\": {\"x\": \"120\", \"y\": \"120\", \"w\": \"150\", \"h\": \"200\", \"label\": \"Cuve A\"}}]}";

// Du texte, un appel d'outil dont les arguments arrivent en morceaux, l'usage, [DONE].
std::string streamTool(const std::string& input = kTankInput, const char* reason = "tool_calls", bool done = true) {
    std::string s = chunkOf(obj({{"role", "assistant"}, {"content", ""}})) + textChunk("Je propose ") + textChunk("une cuve.");
    s += callChunk(0, "call_1", "proposer_operations", "");
    const std::size_t a = input.size() / 3, b = 2 * input.size() / 3;
    s += callChunk(0, "", "", input.substr(0, a)) + callChunk(0, "", "", input.substr(a, b - a)) + callChunk(0, "", "", input.substr(b));
    s += finishChunk(reason) + usageChunk(1200, 300, 200);
    if (done) s += kDone;
    return s;
}
std::string streamText(const std::string& text) { return textChunk(text) + finishChunk("stop") + usageChunk(500, 20) + kDone; }

std::vector<ai::ProviderEvent> feedAll(ai::OpenAIStream& s, const std::string& stream, std::size_t step) {
    std::vector<ai::ProviderEvent> out;
    for (std::size_t i = 0; i < stream.size(); i += step) {
        s.feed(stream.substr(i, step));
        for (auto& e : s.take()) out.push_back(std::move(e));
    }
    s.end();
    for (auto& e : s.take()) out.push_back(std::move(e));
    return out;
}
std::string allText(const std::vector<ai::ProviderEvent>& ev) {
    std::string t;
    for (const auto& e : ev)
        if (e.type == ai::ProviderEvent::Type::Text) t += e.text;
    return t;
}
const ai::ProviderEvent* find(const std::vector<ai::ProviderEvent>& ev, ai::ProviderEvent::Type type, bool last = false) {
    const ai::ProviderEvent* f = nullptr;
    for (const auto& e : ev)
        if (e.type == type) {
            f = &e;
            if (!last) return f;
        }
    return f;
}
bool hasStatus(const std::vector<ai::ProviderEvent>& ev, const std::string& s) {
    return std::any_of(ev.begin(), ev.end(), [&](const ai::ProviderEvent& e) { return e.type == ai::ProviderEvent::Type::Status && e.text == s; });
}

// =============================================================================
//  Le faux transport : il rejoue des reponses, retient les requetes.
// =============================================================================
struct Scripted {
    int                      status{200};
    ai::HttpHeaders          headers;
    std::vector<std::string> chunks;
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
    ai::HttpRequest at(std::size_t i) {
        std::lock_guard<std::mutex> lk(m);
        return i < requests.size() ? requests[i] : ai::HttpRequest{};
    }
    std::size_t count() {
        std::lock_guard<std::mutex> lk(m);
        return requests.size();
    }
};
FakeNet g_net;

Scripted sseReply(const std::string& stream, ai::HttpHeaders headers = {}, std::size_t piece = 29) {
    Scripted s;
    s.headers = std::move(headers);
    s.headers.emplace_back("content-type", "text/event-stream");
    for (std::size_t i = 0; i < stream.size(); i += piece) s.chunks.push_back(stream.substr(i, piece));
    return s;
}
Scripted errorReply(int status, const std::string& message, ai::HttpHeaders headers = {}) {
    Scripted s;
    s.status = status;
    s.headers = std::move(headers);
    s.headers.emplace_back("content-type", "application/json");
    s.chunks = {ai::json::write(obj({{"error", obj({{"message", message}, {"type", "invalid_request_error"}})}}), false)};
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
                s = errorReply(500, "pas de reponse prevue");
            }
        }
        ai::HttpResponse r;
        if (cancelled_) {
            r.failure = ai::HttpResponse::Failure::Cancelled;
            return r;
        }
        r.status = s.status;
        r.headers = s.headers;
        if (sink.onHeaders) sink.onHeaders(s.status, s.headers);
        for (const auto& c : s.chunks)
            if (sink.onData) sink.onData(c);
        return r;
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    std::atomic<bool> cancelled_{false};
};

void useFakeNet() {
    g_net.reset();
    ai::setHttpFactory([] { return std::make_unique<FakeHttp>(); });
}

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
    r.endpoint = "https://service.exemple/v1";
    r.system = "Les consignes.";
    r.toolsJson = ai::operationsTool();
    ai::ProviderTurn t;
    t.text = text;
    r.turns.push_back(t);
    return r;
}

ai::OpenAIProvider::Options fastRetries() {
    ai::OpenAIProvider::Options o;
    o.firstDelay = 0.01;
    o.factor = 2.0;
    return o;
}

// =============================================================================
//  1. Les services et leurs adresses
// =============================================================================
void testServices() {
    std::printf("[services et adresses]\n");
    check(ai::servicePresets().size() == 7 && ai::findServicePreset("ollama") && ai::findServicePreset("ollama")->local
              && !ai::findServicePreset("openrouter")->local && ai::findServicePreset("inconnu") == nullptr,
          "les services : Ollama et LM Studio sur ce PC, les autres en ligne");
    for (const auto& p : ai::servicePresets())
        check(p.endpoint.empty() || p.endpoint.rfind("https://", 0) == 0 || p.endpoint.rfind("http://localhost", 0) == 0,
              "l'adresse de " + std::string(p.key) + " : https://, ou ce PC");
    ai::OpenAITarget t;
    std::string why;
    check(ai::openAITarget("", "ollama", false, t, &why) && t.chat == "http://localhost:11434/v1/chat/completions" && t.models == "http://localhost:11434/v1/models"
              && t.local,
          "Ollama, l'adresse vide : celle du service (" + t.chat + ")");
    check(ai::openAITarget("https://openrouter.ai/api/v1/chat/completions/", "openrouter", true, t) && t.chat == "https://openrouter.ai/api/v1/chat/completions"
              && t.models == "https://openrouter.ai/api/v1/models" && !t.local,
          "une adresse qui finit par /chat/completions : la base retrouvee");
    check(ai::openAITarget("http://192.168.1.20:11434/v1", "autre", false, t, &why) && t.privateHttp && t.local, "http:// vers l'usine SANS cle : accepte");
    check(!ai::openAITarget("http://192.168.1.20:11434/v1", "autre", true, t, &why) && has(why, "cl\xC3\xA9") && has(why, "https://"),
          "http:// vers l'usine AVEC cle : refuse et dit (" + why + ")");
    check(!ai::openAITarget("http://serveur.exemple.fr/v1", "autre", false, t, &why), "http:// vers Internet : refuse");
    check(!ai::openAITarget("", "autre", false, t, &why) && has(why, "adresse"), "Autre sans adresse : dit");
    ai::Url u;
    check(ai::parseUrl("http://10.1.2.3:8080", u, nullptr, true) && u.privateLan() && ai::parseUrl("http://172.20.0.5", u, nullptr, true) && u.privateLan()
              && !ai::parseUrl("http://172.40.0.5", u, nullptr, true) && !ai::parseUrl("http://10.1.2.3", u),
          "les adresses privees (10.x, 172.16-31.x) ; sans permission : refusees comme avant");
    check(ai::localService("", "lmstudio") && ai::localService("http://127.0.0.1:9000/v1", "autre") && !ai::localService("https://api.groq.com/openai/v1", "groq"),
          "localService : LM Studio, 127.0.0.1 ; pas Groq");
    ai::Settings s;
    s.provider = "openai";
    s.preset = "ollama";
    check(!ai::providerNeedsKey(s), "providerNeedsKey : Ollama, non");
    s.preset = "openrouter";
    check(ai::providerNeedsKey(s), "providerNeedsKey : OpenRouter, oui");
    s.preset = "autre";
    s.endpoint = "http://127.0.0.1:8765/v1";
    check(!ai::providerNeedsKey(s), "providerNeedsKey : une adresse de ce PC, non");
    s.provider = "integree";
    check(!ai::providerNeedsKey(s) && !ai::providerUsesNetwork("integree") && ai::providerUsesNetwork("openai"), "l'IA integree : ni cle ni reseau");
    const auto h = ai::openAIHeaders("cle");
    check(std::any_of(h.begin(), h.end(), [](const auto& kv) { return kv.first == "authorization" && kv.second == "Bearer cle"; }), "Authorization: Bearer <cle>");
    const auto h0 = ai::openAIHeaders("");
    check(std::none_of(h0.begin(), h0.end(), [](const auto& kv) { return kv.first == "authorization"; }), "sans cle : pas d'Authorization");
}

// =============================================================================
//  2. Le corps compatible OpenAI
// =============================================================================
void testBody() {
    std::printf("[corps compatible OpenAI]\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_compatible_corps";
    fs::remove_all(dir);
    const std::string png = std::string("\x89PNG\r\n\x1A\n", 8) + std::string(20, '\x01');
    writeFile(dir / "capture.png", png);
    writeFile(dir / "plan.pdf", "%PDF-1.4");
    ai::ProviderRequest r = simpleRequest("Ajoute une cuve");
    r.systemStable = "STABLE.";
    r.systemDynamic = "DYNAMIQUE.";
    r.turns.clear();
    ai::ProviderTurn u1;
    u1.text = "Regarde";
    u1.attachments.push_back({"capture.png", "image/png", U(dir / "capture.png"), png.size()});
    u1.attachments.push_back({"plan.pdf", "application/pdf", U(dir / "plan.pdf"), 8});
    ai::ProviderTurn a1;
    a1.role = ai::ProviderTurn::Role::Assistant;
    a1.text = "Je propose une cuve.";
    a1.toolUseId = "call_9";
    a1.toolName = "proposer_operations";
    a1.toolInputJson = kTankInput;
    ai::ProviderTurn u2;
    u2.text = "Merci";
    u2.toolResultFor = "call_9";
    u2.toolResultText = "appliqu\xC3\xA9" "e au projet.";
    r.turns = {u1, a1, u2};
    std::vector<std::string> notes;
    const std::string body = ai::openAIBody(r, true, true, &notes);
    Value v;
    check(ai::json::parse(body, v) && v.isObject(), "le corps est du JSON");
    check(v.str("model") == "modele-essai" && v.flag("stream") && v.at("stream_options").flag("include_usage") && v.integer("max_tokens") == 16000,
          "model, stream, stream_options.include_usage, max_tokens");
    check(!v.has("tool_choice") && !v.has("temperature"), "ni tool_choice ni temperature");
    const auto& msgs = v.at("messages").items();
    check(msgs.size() == 5, "system, user, assistant, tool, user : " + std::to_string(msgs.size()));
    if (msgs.size() == 5) {
        check(msgs[0].str("role") == "system" && msgs[0].str("content") == "STABLE.DYNAMIQUE.", "system : les consignes en un texte");
        const auto& c1 = msgs[1].at("content");
        bool image = false, pdfLine = false;
        for (const auto& p : c1.items()) {
            image = image || (p.str("type") == "image_url" && p.at("image_url").str("url").rfind("data:image/png;base64,", 0) == 0);
            pdfLine = pdfLine || (p.str("type") == "text" && has(p.str("text"), "PDF"));
        }
        check(c1.isArray() && image && pdfLine, "user : l'image en image_url data:base64, le PDF dit en texte");
        check(!notes.empty() && has(notes.front(), "PDF"), "la note du PDF pour l'utilisateur");
        const auto& calls = msgs[2].at("tool_calls");
        check(msgs[2].str("role") == "assistant" && msgs[2].str("content") == "Je propose une cuve." && calls.isArray() && calls.items().size() == 1
                  && calls.items()[0].str("id") == "call_9" && calls.items()[0].at("function").str("name") == "proposer_operations"
                  && calls.items()[0].at("function").at("arguments").isString() && has(calls.items()[0].at("function").str("arguments"), "Cuve_A"),
              "assistant : son texte et tool_calls (arguments en texte)");
        check(msgs[3].str("role") == "tool" && msgs[3].str("tool_call_id") == "call_9" && has(msgs[3].str("content"), "appliqu"),
              "tool : le resultat, avant le message suivant");
        check(msgs[4].str("role") == "user" && msgs[4].str("content") == "Merci", "user : le texte seul (une chaine)");
    }
    const auto& tools = v.at("tools").items();
    check(tools.size() == 1 && tools[0].str("type") == "function" && tools[0].at("function").str("name") == "proposer_operations"
              && tools[0].at("function").at("parameters").at("properties").has("operations"),
          "tools : la fonction proposer_operations et son schema en parameters");
    // Sans outil : la proposition en texte.
    Value t;
    check(ai::json::parse(ai::openAIBody(r, false, false), t), "le corps sans outil est du JSON");
    const auto& tm = t.at("messages").items();
    check(!t.has("tools") && !t.has("stream_options"), "sans outil : ni tools, ni stream_options (refuse)");
    check(!tm.empty() && has(tm[0].str("content"), "```json") && has(tm[0].str("content"), "EN TEXTE"), "les consignes demandent le bloc json");
    bool block = false, bracket = false, toolRole = false;
    for (const auto& m : tm) {
        block = block || (m.str("role") == "assistant" && has(m.str("content"), "```json") && has(m.str("content"), "Cuve_A"));
        bracket = bracket || (m.str("role") == "user" && has(m.str("content"), "[R\xC3\xA9sultat de ta proposition"));
        toolRole = toolRole || m.str("role") == "tool" || m.has("tool_calls");
    }
    check(block && bracket && !toolRole, "l'appel precedent ecrit en bloc, son resultat entre crochets ; aucun role tool");
}

// =============================================================================
//  3. Le flux compatible OpenAI
// =============================================================================
void testStream() {
    std::printf("[flux compatible OpenAI]\n");
    const std::string stream = streamTool();
    bool same = true;
    std::vector<ai::ProviderEvent> ref;
    for (std::size_t step : {1u, 2u, 3u, 7u, 64u, 4096u}) {
        ai::OpenAIStream s;
        const auto ev = feedAll(s, stream, step);
        if (ref.empty()) ref = ev;
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        same = same && s.finished() && allText(ev) == "Je propose une cuve." && tool && tool->toolInputJson == kTankInput && tool->toolUseId == "call_1";
    }
    check(same, "coupe partout (1 a 4096 octets) : le texte, l'appel complet (arguments concatenes)");
    const auto* usage = find(ref, ai::ProviderEvent::Type::Usage, true);
    check(usage && usage->usage.input == 1000 && usage->usage.cacheRead == 200 && usage->usage.output == 300, "l'usage final (dont le cache lu)");
    const auto* done = find(ref, ai::ProviderEvent::Type::Done);
    check(done && done->stopReason == "tool_use" && ref.back().type == ai::ProviderEvent::Type::Done, "la fin : tool_use, apres [DONE]");
    check(hasStatus(ref, "reflexion") && hasStatus(ref, "ecriture") && hasStatus(ref, "operations"), "les etats : reflexion, ecriture, operations");
    {   // deux appels entremeles par index
        std::string s2 = callChunk(0, "c0", "proposer_operations", "{\"a\":") + callChunk(1, "c1", "proposer_operations", "{\"b\":") + callChunk(0, "", "", "1}")
                         + callChunk(1, "", "", "2}") + finishChunk("tool_calls") + kDone;
        ai::OpenAIStream s;
        const auto ev = feedAll(s, s2, 5);
        std::vector<std::string> inputs;
        for (const auto& e : ev)
            if (e.type == ai::ProviderEvent::Type::ToolUse) inputs.push_back(e.toolUseId + "=" + e.toolInputJson);
        check(inputs.size() == 2 && inputs[0] == "c0={\"a\":1}" && inputs[1] == "c1={\"b\":2}", "deux appels entremeles : concatenes par index");
    }
    {   // coupe par la longueur : pas d'appel
        ai::OpenAIStream s;
        const auto ev = feedAll(s, streamTool(kTankInput.substr(0, 40), "length"), 11);
        const auto* d = find(ev, ai::ProviderEvent::Type::Done);
        check(!find(ev, ai::ProviderEvent::Type::ToolUse) && d && d->stopReason == "max_tokens", "length : aucun appel propose, max_tokens");
    }
    {   // la connexion coupee
        ai::OpenAIStream s;
        const auto ev = feedAll(s, textChunk("Je commence"), 4);
        check(s.failed() && !s.finished() && s.error().retryable && has(s.error().text, "coup") && allText(ev) == "Je commence", "coupe avant la fin : dit");
    }
    {   // sans [DONE], mais une fin donnee
        ai::OpenAIStream s;
        (void)feedAll(s, textChunk("Bonjour") + finishChunk("stop"), 8);
        check(s.finished() && !s.failed(), "sans [DONE] mais avec finish_reason : fini");
    }
    {   // une erreur dans le flux
        ai::OpenAIStream s;
        (void)feedAll(s, data(obj({{"error", obj({{"message", "Overloaded"}, {"code", 503}})}})), 16);
        check(s.failed() && s.error().status == 503 && s.error().retryable && has(s.error().text, "Overloaded"), "une erreur du flux : " + s.error().text);
    }
    {   // la proposition en texte
        ai::OpenAIStream s;
        s.setTextTool(true);
        const auto ev = feedAll(s, streamText("Voici une cuve.\n\n```json\n" + kTankInput + "\n```"), 13);
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        check(tool && tool->toolUseId.rfind("texte-", 0) == 0 && tool->toolName == "proposer_operations" && tool->toolInputJson == kTankInput,
              "en texte : le bloc json rendu en appel d'outil");
    }
}

// =============================================================================
//  4. Les erreurs ; le JSON dans le texte
// =============================================================================
void testErrorsAndText() {
    std::printf("[erreurs, JSON en texte]\n");
    auto e = ai::openAIError(401, "{\"error\": {\"message\": \"Incorrect API key\"}}", "m");
    check(e.status == 401 && has(e.text, "Cl\xC3\xA9 API refus") && !e.retryable, "401 : " + e.text);
    e = ai::openAIError(404, "{\"error\": \"model \\\"llama9\\\" not found, try pulling it first\"}", "llama9");
    check(has(e.text, "Mod\xC3\xA8le introuvable") && has(e.text, "llama9") && has(e.text, "ollama pull"), "404 Ollama (erreur en chaine) : " + e.text);
    e = ai::openAIError(404, "<html>nope</html>", "m");
    check(has(e.text, "Adresse introuvable"), "404 sans JSON : l'adresse");
    e = ai::openAIError(429, "{\"error\": {\"message\": \"Rate limit reached\"}}", "m");
    check(e.retryable && has(e.text, "429"), "429 : reessayable");
    e = ai::openAIError(503, "", "m");
    check(e.retryable && has(e.text, "503"), "503 : reessayable");
    e = ai::openAIError(400, "{\"error\": \"registry.ollama.ai/library/gemma does not support tools\"}", "gemma");
    check(ai::toolsRefused(e) && has(e.text, "does not support tools"), "400 does not support tools : l'outil refuse");
    check(!ai::toolsRefused(ai::openAIError(400, "{\"error\": {\"message\": \"bad\"}}", "m")), "un autre 400 : pas l'outil");
    check(ai::toolsRefused(ai::openAIError(404, "{\"error\": {\"message\": \"No endpoints found that support tool use\"}}", "m")),
          "OpenRouter (404 tool use) : l'outil refuse");
    check(ai::streamOptionsRefused(ai::openAIError(400, "{\"error\": {\"message\": \"Unrecognized request argument: stream_options\"}}", "m")),
          "stream_options refuse");
    std::string json, rest;
    check(ai::extractProposalBlock("Voici.\n\n```json\n" + kTankInput + "\n```\nBonne suite.", &json, &rest) && json == kTankInput && rest == "Voici.\n\n\nBonne suite.",
          "le bloc lu, le texte sans lui");
    check(ai::extractProposalBlock("```json\n{\"x\": 1}\n```\npuis\n```json\n" + kTankInput + "\n```\n```json\n{\"y\": 2}\n```", &json) && json == kTankInput,
          "plusieurs blocs : celui qui porte les operations");
    check(!ai::extractProposalBlock("Pas de bloc ici.", &json) && !ai::extractProposalBlock("```python\nprint(1)\n```", &json), "aucun bloc json : faux");
    check(ai::extractProposalBlock("Voici :\n```json\n{\"resume\": \"x\"", &json) && json == "{\"resume\": \"x\"", "un bloc sans fermeture : jusqu'au bout");
    check(ai::proposalBlock("{\"a\": 1}") == "```json\n{\"a\": 1}\n```", "proposalBlock");
    // La meme lecture stricte que l'outil.
    hmi::Project p;
    hmi::View v = hmi::makeView(p, "Vue");
    p.views = {v};
    ai::PromptInput in;
    in.project = &p;
    in.view = &p.views.front();
    std::string why;
    check(ai::proposalFromTool(kTankInput, in, "cuve", &why).has_value(), "le bloc : la proposition lue (proposalFromTool)");
    check(!ai::proposalFromTool("{\"resume\": \"x\", \"operations\": [{\"action\": \"executer\"}]}", in, "x", &why) && !why.empty(),
          "un bloc avec une operation inconnue : refuse (" + why + ")");
    check(!ai::proposalFromTool("{\"resume\": \"Casse\", \"operations\": [{\"action\": \"creer\",", in, "x", &why) && has(why, "illisible"), "un bloc illisible : refuse");
    check(has(ai::textToolInstructions(), "```json") && has(ai::textToolInstructions(), "operations"), "les consignes du JSON en texte");
    const auto compact = ai::compactPromptParts(in, 4096);
    const auto full = ai::promptParts(in);
    check(compact.stable.size() + compact.dynamic.size() < (full.stable.size() + full.dynamic.size()) / 2 && has(compact.dynamic, "Genres d'objets"),
          "une petite fenetre : les consignes resserrees (" + std::to_string(compact.stable.size() + compact.dynamic.size()) + " octets contre "
              + std::to_string(full.stable.size() + full.dynamic.size()) + ")");
}

// =============================================================================
//  5. Les en-tetes de limite
// =============================================================================
void testLimits() {
    std::printf("[limites]\n");
    check(ai::parseDuration("1s") == 1.0 && ai::parseDuration("6m0s") == 360.0 && std::abs(ai::parseDuration("59.5ms") - 0.0595) < 1e-9
              && ai::parseDuration("1h2m3s") == 3723.0 && ai::parseDuration("12") == 12.0 && ai::parseDuration("abc") < 0 && ai::parseDuration("") < 0,
          "les durees : 1s, 6m0s, 59.5ms, 1h2m3s, 12 ; illisibles");
    const std::time_t base = 1793448000;   // 2026-10-31T12:00:00Z
    check(std::abs(ai::secondsUntil("2026-10-31T12:00:30Z", base) - 30.0) < 1e-6, "RFC 3339 en Z : 30 s");
    check(std::abs(ai::secondsUntil("2026-10-31T13:01:00+01:00", base) - 60.0) < 1e-6, "RFC 3339 avec un decalage : 60 s");
    check(std::abs(ai::secondsUntil("2026-10-31T12:00:01.5Z", base) - 1.5) < 1e-6 && ai::secondsUntil("2026-10-31T11:00:00Z", base) == 0.0
              && ai::secondsUntil("demain", base) < 0 && ai::secondsUntil("2026-10-31T12:00:00", base) < 0,
          "les fractions, une date passee (0), illisible, sans fuseau");
    ai::HttpHeaders anth = {{"anthropic-ratelimit-requests-limit", "50"},          {"anthropic-ratelimit-requests-remaining", "47"},
                            {"anthropic-ratelimit-requests-reset", "2026-10-31T12:00:12Z"}, {"anthropic-ratelimit-tokens-limit", "80000"},
                            {"anthropic-ratelimit-tokens-remaining", "70000"},     {"anthropic-ratelimit-tokens-reset", "2026-10-31T12:00:05Z"},
                            {"anthropic-ratelimit-input-tokens-limit", "40000"},   {"anthropic-ratelimit-input-tokens-remaining", "20000"},
                            {"anthropic-ratelimit-input-tokens-reset", "2026-10-31T12:00:03Z"}, {"anthropic-ratelimit-output-tokens-limit", "16000"},
                            {"anthropic-ratelimit-output-tokens-remaining", "15900"}, {"retry-after", "7"}};
    auto l = ai::limitsFromHeaders(anth, base);
    check(l.requestsLimit == 50 && l.requestsRemaining == 47 && std::abs(l.requestsResetSeconds - 12) < 1e-6, "Anthropic : 47/50 requetes, remise dans 12 s");
    check(l.tokensLimit == 40000 && l.tokensRemaining == 20000 && std::abs(l.tokensResetSeconds - 3) < 1e-6,
          "les jetons : la plus contraignante (l'entree, 50 % restant, devant 87,5 % et 99 %) : " + std::to_string(l.tokensRemaining));
    check(l.retryAfterSeconds == 7 && l.known() && !l.local, "retry-after : 7 s");
    ai::HttpHeaders oa = {{"X-RateLimit-Limit-Requests", "30"}, {"x-ratelimit-remaining-requests", "29"}, {"x-ratelimit-reset-requests", "2s"},
                          {"x-ratelimit-limit-tokens", "6000"}, {"x-ratelimit-remaining-tokens", "5400"}, {"x-ratelimit-reset-tokens", "6m0s"}};
    l = ai::limitsFromHeaders(oa, base);
    check(l.requestsLimit == 30 && l.requestsRemaining == 29 && l.requestsResetSeconds == 2 && l.tokensLimit == 6000 && l.tokensRemaining == 5400
              && l.tokensResetSeconds == 360 && l.retryAfterSeconds < 0,
          "OpenAI : 29/30, 5400/6000, les durees (les cles sans souci de casse)");
    l = ai::limitsFromHeaders({{"x-ratelimit-limit", "20"}, {"x-ratelimit-remaining", "19"}, {"x-ratelimit-reset", "1793448010000"}}, base);
    check(l.requestsLimit == 20 && l.requestsRemaining == 19 && std::abs(l.requestsResetSeconds - 10) < 1e-6, "OpenRouter : la remise en millisecondes depuis 1970");
    check(!ai::limitsFromHeaders({{"content-type", "text/event-stream"}}, base).known(), "sans en-tete : inconnue");
    check(ai::tokensText(980) == "980" && ai::tokensText(9800) == "9,8\xC2\xA0k" && ai::tokensText(38400) == "38,4\xC2\xA0k" && ai::tokensText(40000) == "40\xC2\xA0k"
              && ai::tokensText(200000) == "200\xC2\xA0k" && ai::tokensText(1200000) == "1,2\xC2\xA0M",
          "les jetons a la francaise : 980, 9,8 k, 38,4 k, 40 k, 200 k, 1,2 M");
    check(ai::waitText(12) == "12\xC2\xA0s" && ai::waitText(360) == "6\xC2\xA0min" && ai::waitText(3900) == "1\xC2\xA0h\xC2\xA0" "05", "les attentes : 12 s, 6 min, 1 h 05");
}

// =============================================================================
//  6. L'usage cumule (IA/usage.json)
// =============================================================================
void testUsage() {
    std::printf("[usage cumule]\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_compatible_usage";
    fs::remove_all(dir);
    ai::setRoot(U(dir / "IA"));
    g_now = at(2026, 10, 31);
    ai::ProviderUsage u;
    u.input = 1000;
    u.output = 200;
    u.cacheRead = 50;
    ai::recordUsage("anthropic", u, "c-1");
    ai::recordUsage("anthropic", u, "c-1");
    ai::recordUsage("openai", u);
    auto t = ai::usageTotals("anthropic");
    check(t.tokensToday == 2500 && t.requestsToday == 2 && t.tokensMonth == 2500, "Anthropic : 2 500 jetons, 2 demandes aujourd'hui");
    t = ai::usageTotals();
    check(t.tokensToday == 3750 && t.requestsToday == 3, "tous les fournisseurs : 3 750");
    check(ai::conversationTokens("c-1") == 2500 && ai::conversationTokens("c-2") == 0, "la conversation c-1 : 2 500");
    ai::recordUsage("anthropic", ai::ProviderUsage{}, "c-1");
    check(ai::usageTotals().requestsToday == 3, "un usage vide n'est pas compte");
    g_now = at(2026, 11, 1);
    t = ai::usageTotals();
    check(t.tokensToday == 0 && t.tokensMonth == 0, "le lendemain, un autre mois : 0 aujourd'hui, 0 ce mois");
    g_now = at(2026, 10, 30);
    ai::recordUsage("openai", u);
    g_now = at(2026, 10, 31);
    t = ai::usageTotals();
    check(t.tokensToday == 3750 && t.tokensMonth == 5000, "le jour et le mois : 3 750 aujourd'hui, 5 000 en octobre");
    const std::string file = readAll(dir / "IA" / "usage.json");
    Value v;
    check(ai::json::parse(file, v) && v.at("jours").has("2026-10-31") && v.at("mois").has("2026-10") && v.at("conversations").integer("c-1") == 2500,
          "le fichier relu : jours, mois, conversations");
    check(!has(file, "cle") && !has(file, "Ajoute") && !has(file, "SECRETE"), "ni cle ni demande dans usage.json");
    ai::Settings s;
    s.provider = "anthropic";
    s.budgetTokens = 3000;
    std::string q;
    check(ai::budgetExceeded(s, t, &q) && has(q, "budget du jour est atteint") && has(q, "3,8\xC2\xA0k / 3\xC2\xA0k"), "le budget du jour atteint : " + q);
    s.budgetPeriod = "mois";
    s.budgetTokens = 6000;
    check(!ai::budgetExceeded(s, t) && (s.budgetTokens = 5000, ai::budgetExceeded(s, t, &q)) && has(q, "du mois"), "le budget du mois");
    ai::setRoot({});
}

// =============================================================================
//  7. Le fournisseur sur le faux transport
// =============================================================================
const ai::HttpHeaders kOpenAILimits = {{"x-ratelimit-limit-requests", "50"}, {"x-ratelimit-remaining-requests", "47"}, {"x-ratelimit-reset-requests", "12s"},
                                       {"x-ratelimit-limit-tokens", "40000"}, {"x-ratelimit-remaining-tokens", "38400"}, {"x-ratelimit-reset-tokens", "3s"}};

void testProvider() {
    std::printf("[fournisseur compatible, faux transport]\n");
    useFakeNet();
    {
        g_net.push(sseReply(streamTool(), kOpenAILimits));
        ai::OpenAIProvider p(fastRetries());
        std::string why;
        check(p.key() == "openai" && p.start(simpleRequest(), &why), "start (" + why + ")");
        const auto ev = runToEnd(p);
        const auto* lim = find(ev, ai::ProviderEvent::Type::Usage);
        check(lim && lim->limits.requestsRemaining == 47 && lim->limits.tokensRemaining == 38400, "un evenement Usage : la limite des en-tetes");
        const auto* use = find(ev, ai::ProviderEvent::Type::Usage, true);
        check(use && use->usage.total() == 1500, "un evenement Usage : les jetons de la reponse");
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        check(tool && tool->toolInputJson == kTankInput && ev.back().type == ai::ProviderEvent::Type::Done, "le texte, l'appel, la fin");
        const auto req = g_net.at(0);
        check(req.url == "https://service.exemple/v1/chat/completions" && req.method == "POST", "POST <adresse>/chat/completions");
        check(std::any_of(req.headers.begin(), req.headers.end(), [](const auto& kv) { return kv.second == "Bearer cle-essai-SECRETE"; }), "la cle en Authorization");
    }
    {   // un modele sans outil : la reprise sans outil, la proposition en texte
        g_net.reset();
        g_net.push(errorReply(400, "registry.ollama.ai/library/sans-outil does not support tools"));
        g_net.push(sseReply(streamText("Voici une cuve.\n\n```json\n" + kTankInput + "\n```")));
        ai::OpenAIProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        check(hasStatus(ev, "sans-outil") && tool && tool->toolUseId.rfind("texte-", 0) == 0 && tool->toolInputJson == kTankInput,
              "400 does not support tools -> sans outil, le bloc json rendu en appel");
        check(g_net.count() == 2 && has(g_net.at(0).body, "\"tools\"") && !has(g_net.at(1).body, "\"tools\"") && has(g_net.at(1).body, "```json"),
              "la seconde demande : sans tools, les consignes du bloc");
    }
    {   // Outil : Appel d'outil (pas de reprise)
        g_net.reset();
        g_net.push(errorReply(400, "model does not support tools"));
        ai::OpenAIProvider p(fastRetries());
        auto r = simpleRequest();
        r.textFallback = false;
        (void)p.start(r);
        const auto ev = runToEnd(p);
        const auto* e = find(ev, ai::ProviderEvent::Type::Error);
        check(e && has(e->text, "JSON dans le texte") && g_net.count() == 1, "Outil force : l'erreur dit quoi choisir");
    }
    {   // toolsInText : sans outil d'emblee
        g_net.reset();
        g_net.push(sseReply(streamText("Une question ?")));
        ai::OpenAIProvider p(fastRetries());
        auto r = simpleRequest();
        r.toolsInText = true;
        (void)p.start(r);
        (void)runToEnd(p);
        check(g_net.count() == 1 && !has(g_net.at(0).body, "\"tools\"") && has(g_net.at(0).body, "EN TEXTE"), "JSON dans le texte : sans tools d'emblee");
    }
    {   // 429 puis la reponse ; stream_options refuse
        g_net.reset();
        g_net.push(errorReply(400, "Unrecognized request argument supplied: stream_options"));
        g_net.push(errorReply(429, "Rate limit reached", {{"retry-after", "0"}, {"x-ratelimit-remaining-requests", "0"}, {"x-ratelimit-limit-requests", "50"}}));
        g_net.push(sseReply(streamText("Bonjour")));
        ai::OpenAIProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        bool waited = false;
        for (const auto& e : ev) waited = waited || (e.type == ai::ProviderEvent::Type::Usage && e.limits.retryAfterSeconds == 0 && e.limits.requestsRemaining == 0);
        check(hasStatus(ev, "reprise") && allText(ev) == "Bonjour" && waited, "429 : la limite epuisee dite, une nouvelle tentative, la reponse");
        check(g_net.count() == 3 && has(g_net.at(0).body, "stream_options") && !has(g_net.at(1).body, "stream_options"), "stream_options refuse : retire");
    }
    {   // 401, une adresse de ce PC sans en-tete : pas de limite
        g_net.reset();
        g_net.push(errorReply(401, "Incorrect API key provided"));
        ai::OpenAIProvider p(fastRetries());
        (void)p.start(simpleRequest());
        const auto ev = runToEnd(p);
        const auto* e = find(ev, ai::ProviderEvent::Type::Error);
        check(e && e->httpStatus == 401 && has(e->text, "Cl\xC3\xA9 API"), "401 dit");
        g_net.push(sseReply(streamText("Bonjour")));
        ai::OpenAIProvider q(fastRetries());
        auto r = simpleRequest();
        r.endpoint = "http://127.0.0.1:11434/v1";
        r.apiKey.clear();
        (void)q.start(r);
        const auto ev2 = runToEnd(q);
        const auto* lim = find(ev2, ai::ProviderEvent::Type::Usage);
        const auto req = g_net.at(g_net.count() - 1);
        check(lim && lim->limits.local, "ce PC, sans en-tete : ProviderLimits::local");
        check(std::none_of(req.headers.begin(), req.headers.end(), [](const auto& kv) { return kv.first == "authorization"; }), "sans cle : pas d'Authorization");
    }
    {   // Anthropic : les en-tetes et l'usage du flux
        g_net.reset();
        const std::string stream = "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":100,\"cache_read_input_tokens\":40,"
                                   "\"output_tokens\":1}}}\n\nevent: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
                                   "{\"type\":\"text\",\"text\":\"\"}}\n\nevent: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
                                   "{\"type\":\"text_delta\",\"text\":\"Bonjour\"}}\n\nevent: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                                   "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":42}}\n\n"
                                   "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
        g_net.push(sseReply(stream, {{"anthropic-ratelimit-requests-limit", "50"}, {"anthropic-ratelimit-requests-remaining", "47"},
                                     {"anthropic-ratelimit-requests-reset", "2026-10-31T12:00:12Z"}}));
        ai::AnthropicProvider::Options o;
        o.firstDelay = 0.01;
        ai::AnthropicProvider p(o);
        auto r = simpleRequest();
        r.endpoint.clear();
        (void)p.start(r);
        const auto ev = runToEnd(p);
        const auto* lim = find(ev, ai::ProviderEvent::Type::Usage);
        const auto* use = find(ev, ai::ProviderEvent::Type::Usage, true);
        check(lim && lim->limits.requestsLimit == 50 && lim->limits.requestsRemaining == 47, "Anthropic : la limite des en-tetes");
        check(use && use->usage.input == 100 && use->usage.cacheRead == 40 && use->usage.output == 42, "Anthropic : l'usage (message_start, message_delta)");
    }
    ai::setHttpFactory(nullptr);
}

// =============================================================================
//  8. L'Assistant, la bande du panneau, le budget, les parametres
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

struct FakeScript {
    std::vector<ai::ProviderRequest>           requests;
    std::deque<std::vector<ai::ProviderEvent>> replies;
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

ai::ProviderEvent textEv(const std::string& t) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Text;
    e.text = t;
    return e;
}
ai::ProviderEvent doneEv() {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Done;
    e.stopReason = "end_turn";
    return e;
}
ai::ProviderEvent usageEv(long long in, long long out) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Usage;
    e.usage.input = in;
    e.usage.output = out;
    return e;
}
ai::ProviderEvent limitEv(long long req, long long reqMax, long long tok, long long tokMax, double reset) {
    ai::ProviderEvent e;
    e.type = ai::ProviderEvent::Type::Usage;
    e.limits.requestsRemaining = req;
    e.limits.requestsLimit = reqMax;
    e.limits.tokensRemaining = tok;
    e.limits.tokensLimit = tokMax;
    e.limits.requestsResetSeconds = reset;
    e.limits.tokensResetSeconds = reset;
    return e;
}

struct Fixture {
    fs::path                     dir;
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};

    explicit Fixture(const std::string& name) {
        dir = fs::temp_directory_path() / ("xpg_ai_compatible_" + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
        ai::setRoot(U(dir / "IA"));
        auto& p = doc->project;
        p.views = {hmi::makeView(p, "Vue_Station")};
        view = p.views.front().id;
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        core.projectName = [] { return std::string("Station"); };
        core.projectPath = [this] { return U(dir / "Station"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 440, 900});
        panel->ensureLoaded();
        panel->layout();
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    void step(double dt = 0.05) {
        t += dt;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    void waitLive() {
        for (int k = 0; k < 2500 && as().generating(); ++k) {
            step(0.002);
            pause(2);
        }
        step();
    }
    void configure(const std::string& preset, const std::string& endpoint, const std::string& model = "modele-essai", const std::string& key = "cle-essai") {
        ai::Settings st = as().settings();
        st.provider = "openai";
        st.preset = preset;
        st.endpoint = endpoint;
        st.model = model;
        as().setSettings(st);
        as().setApiKey(key);
        as().newConversation();
        panel->sync();
        panel->layout();
    }
    [[nodiscard]] std::string lines() const {
        std::string s;
        for (const auto& l : panel->limitLines()) s += l + " | ";
        return s;
    }
};

void testPanelBand() {
    std::printf("[assistant, bande du panneau, budget]\n");
    g_now = at(2026, 10, 31);
    Fixture f("bande");
    // La demonstration (sans reseau).
    {
        ai::Settings st = f.as().settings();
        st.provider = "demo";
        f.as().setSettings(st);
        f.step();
        check(has(f.lines(), "Sans connexion : gratuit, sans limite") && f.panel->limitRect().h > 0.f, "la demonstration : " + f.lines());
    }
    FakeScript s;
    f.as().setProviderFactory([&s](std::string_view) { return std::make_unique<FakeProvider>(s); });
    f.configure("openrouter", "https://openrouter.ai/api/v1");
    f.step();
    check(has(f.lines(), "s'affichera apr\xC3\xA8s la premi\xC3\xA8re r\xC3\xA9ponse"), "avant la premiere reponse : " + f.lines());
    check(f.as().connectionText() == "OpenRouter \xE2\x80\x93 modele-essai", "l'etat de connexion nomme le service : " + f.as().connectionText());
    s.replies.push_back({limitEv(47, 50, 38400, 40000, 12), textEv("Bonjour."), usageEv(9800, 2500), doneEv()});
    check(f.panel->sendText("Bonjour"), "envoyer");
    f.step();
    f.step();
    check(!f.as().generating(), "la reponse est arrivee");
    const std::string l1 = f.lines();
    check(has(l1, "47/50") && has(l1, "38,4\xC2\xA0k/40\xC2\xA0k") && has(l1, "12\xC2\xA0s"), "la limite : " + l1);
    check(has(l1, "12,3\xC2\xA0k"), "la conversation : 12,3 k jetons");
    f.step(5.0);
    check(has(f.lines(), "7\xC2\xA0s") && !has(f.lines(), "12\xC2\xA0s"), "le compte a rebours avance sans envoi : " + f.lines());
    f.step(10.0);
    check(has(f.lines(), "remise faite"), "la remise passee : " + f.lines());
    {
        Recorder rec;
        f.panel->layout();
        f.panel->render(ui::PaintContext{rec, ui::Theme::dark(), f.panel->bounds(), f.t, nullptr});
        check(rec.saw("47/50"), "dessinee dans le panneau");
        const auto r = f.panel->limitRect();
        const std::string tip = f.panel->liveTooltip({r.x + 40.f, r.y + 4.f});
        check(has(tip, "requ\xC3\xAA" "tes : 47 restantes sur 50") && has(tip, "usage.json") && has(tip, "Derni\xC3\xA8re r\xC3\xA9ponse : 12\xC2\xA0" "300"),
              "l'infobulle : le detail exact");
    }
    const auto& msg = f.as().current()->messages.back();
    check(msg.extra.integer("jetons") == 12300, "le message garde ses jetons (la conversation relue)");
    check(f.as().usageTotals().tokensToday == 12300, "le cumul du jour");
    // Le budget : 80 %, puis depasse -> la confirmation.
    {
        ai::Settings st = f.as().settings();
        st.budgetTokens = 15000;
        f.as().setSettings(st);
        f.step();
        check(f.panel->usageBand().level == 1 && f.panel->usageBand().ratio > 0.8 && has(f.lines(), "12,3\xC2\xA0k / 15\xC2\xA0k"), "le budget a 80 % : orange (" + f.lines() + ")");
    }
    s.replies.push_back({textEv("Encore."), usageEv(3000, 500), doneEv()});
    check(f.panel->sendText("Encore"), "envoyer sous le budget");
    f.step();
    f.step();
    check(f.panel->usageBand().level == 2, "le budget depasse : rouge");
    std::string q;
    check(f.as().budgetBlocks(&q) && has(q, "Ton budget du jour est atteint (15,8\xC2\xA0k / 15\xC2\xA0k jetons)"), "la question : " + q);
    std::string why;
    check(!f.as().send("Direct", &why) && has(why, "budget"), "l'Assistant n'envoie pas sans confirmation");
    const std::size_t before = s.requests.size();
    check(!f.panel->sendText("Encore une"), "le panneau demande d'abord");
    f.mm.applyPending();
    auto* ask = dynamic_cast<app::HmiAskDialog*>(f.mm.top());
    check(ask && ask->id() == "dialog.ai.budget" && s.requests.size() == before, "la fenetre du logiciel s'ouvre ; rien n'est parti");
    if (ask) {
        s.replies.push_back({textEv("Ok."), doneEv()});
        auto* yes = findButton(*ask->widgetRoot(), "Envoyer quand m\xC3\xAAme");
        check(yes != nullptr, "son bouton Envoyer quand meme");
        if (yes) yes->clicked->emit();
        f.mm.applyPending();
        f.step();
        check(s.requests.size() == before + 1, "confirme : la demande part");
        check(f.as().budgetBlocks(), "le budget reste atteint : la prochaine demande redemande");
    }
    // Un modele sur ce PC.
    f.configure("ollama", "http://localhost:11434/v1", "llama3.2", "");
    f.step();
    check(has(f.lines(), "Sur ce PC : pas de limite"), "Ollama : " + f.lines());
    check(f.as().ready(), "Ollama : pret sans cle");
    // 1.16.0 (captures) : un serveur de ce PC ou de l'usine qui ANNONCE une limite (un relais qui limite le debit) -
    // la bande et l'infobulle disent la meme chose : la limite annoncee.
    s.replies.push_back({[] {
                             ai::ProviderEvent e;
                             e.type = ai::ProviderEvent::Type::Usage;
                             e.limits.requestsRemaining = 16;
                             e.limits.requestsLimit = 20;
                             e.limits.requestsResetSeconds = 25;
                             return e;
                         }(),
                         textEv("Ok."), doneEv()});
    f.as().allowOverBudget();
    (void)f.panel->sendText("Une cuve");
    f.step();
    f.step();
    check(has(f.lines(), "16/20") && !has(f.lines(), "Sur ce PC") && has(f.panel->usageBand().tooltip, "annonc\xC3\xA9" "e par le serveur"),
          "un serveur local qui annonce sa limite : la bande et l'infobulle la disent (" + f.lines() + ")");
    // Un autre service : la limite lue sur l'ancien ne vaut plus.
    f.configure("groq", "https://api.groq.com/openai/v1");
    f.step();
    check(!has(f.lines(), "16/20"), "un autre service : l'ancienne limite s'efface (" + f.lines() + ")");
    // Une 429 : l'attente.
    s.replies.push_back({[] {
                             ai::ProviderEvent e;
                             e.type = ai::ProviderEvent::Type::Usage;
                             e.limits.requestsRemaining = 0;
                             e.limits.requestsLimit = 50;
                             e.limits.retryAfterSeconds = 20;
                             return e;
                         }(),
                         [] {
                             ai::ProviderEvent e;
                             e.type = ai::ProviderEvent::Type::Error;
                             e.text = "Trop de demandes (429).";
                             e.httpStatus = 429;
                             return e;
                         }()});
    f.configure("groq", "https://api.groq.com/openai/v1");
    f.as().allowOverBudget();
    (void)f.panel->sendText("Encore");
    f.step();
    check(has(f.lines(), "Limite atteinte") && has(f.lines(), "20\xC2\xA0s") && f.panel->usageBand().waiting, "une 429 : le temps d'attente (" + f.lines() + ")");
    // La fenetre de contexte : les consignes resserrees, l'outil en texte.
    {
        ai::Settings st = f.as().settings();
        st.contextTokens = 4096;
        st.toolMode = "texte";
        st.budgetTokens = 0;
        f.as().setSettings(st);
        s.replies.push_back({textEv("Ok."), usageEv(3000, 100), doneEv()});
        (void)f.panel->sendText("Ajoute une cuve");
        f.step();
        f.step();
        const auto& r = s.requests.back();
        check(r.contextTokens == 4096 && r.toolsInText && r.preset == "groq" && r.systemStable.size() < 4000, "une fenetre de 4096 : contextTokens, consignes resserrees, outil en texte");
        check(has(f.lines(), "Contexte") && has(f.lines(), "4,1\xC2\xA0k"), "la bande : le contexte de la derniere demande (" + f.lines() + ")");
    }
    ai::setRoot({});
}

// L'Assistant et le vrai fournisseur sur le faux transport : un modele sans outil.
void testAssistantTextTool() {
    std::printf("[assistant : modele sans outil]\n");
    g_now = at(2026, 10, 31);
    useFakeNet();
    Fixture f("texte");
    f.configure("autre", "https://service.exemple/v1", "sans-outil", "cle");
    g_net.push(errorReply(400, "registry.ollama.ai/library/sans-outil does not support tools"));
    g_net.push(sseReply(streamText("Je propose une cuve.\n\n```json\n" + kTankInput + "\n```"), kOpenAILimits));
    check(f.panel->sendText("Ajoute une cuve"), "envoyer");
    f.waitLive();
    const auto& m = f.as().current()->messages.back();
    check(m.proposal.has_value() && m.tool && m.tool->id.rfind("texte-", 0) == 0 && m.tool->error.empty(), "le bloc json devient une proposition validee");
    check(m.text == "Je propose une cuve." && !has(m.text, "```"), "la bulle sans le bloc : " + m.text);
    check(f.as().toolsInText(), "l'Assistant se souvient : ce modele propose en texte");
    check(has(f.lines(), "47/50"), "la bande : la limite du service (" + f.lines() + ")");
    // Le tour suivant : directement sans outil, le resultat entre crochets.
    g_net.push(sseReply(streamText("Bien recu.")));
    check(f.panel->sendText("Merci"), "envoyer la suite");
    f.waitLive();
    const auto last = g_net.at(g_net.count() - 1);
    check(!has(last.body, "\"tools\"") && has(last.body, "```json") && has(last.body, "[R\xC3\xA9sultat de ta proposition"),
          "la suite part sans outil, l'appel precedent en bloc, son resultat entre crochets");
    // Un bloc illisible : refuse.
    g_net.push(sseReply(streamText("Voici.\n\n```json\n{\"resume\": \"Casse\", \"operations\": [{\"action\": \"creer\",\n```")));
    (void)f.panel->sendText("json casse");
    f.waitLive();
    const auto& bad = f.as().current()->messages.back();
    check(!bad.proposal && bad.tool && !bad.tool->error.empty() && has(bad.note, "illisible"), "un bloc illisible : rien n'est propose, la note le dit");
    ai::setHttpFactory(nullptr);
    ai::setRoot({});
}

void testSettings() {
    std::printf("[parametres]\n");
    useFakeNet();
    Fixture f("parametres");
    f.panel->openSettings();
    f.mm.applyPending();
    auto* d = dynamic_cast<ai::AiSettingsDialog*>(f.mm.top());
    check(d != nullptr, "la fenetre s'ouvre");
    if (!d) return;
    auto* provider = dynamic_cast<ui::DropDown*>(d->field("fournisseur"));
    auto* service = dynamic_cast<ui::DropDown*>(d->field("service"));
    auto* endpoint = dynamic_cast<ui::InputText*>(d->field("adresse"));
    auto* context = dynamic_cast<ui::InputText*>(d->field("contexte"));
    auto* tool = dynamic_cast<ui::DropDown*>(d->field("outil"));
    auto* budget = dynamic_cast<ui::InputText*>(d->field("budget"));
    auto* period = dynamic_cast<ui::DropDown*>(d->field("periode"));
    auto* model = dynamic_cast<ui::InputText*>(d->field("modele"));
    auto* key = dynamic_cast<ui::InputText*>(d->field("cle"));
    check(provider && service && endpoint && context && tool && budget && period && model && key, "les champs : service, contexte, outil, budget, periode");
    if (!provider || !service || !endpoint || !context || !tool || !budget || !period || !model || !key) return;
    int openai = -1, integree = -1;
    for (std::size_t i = 0; i < provider->items().size(); ++i) {
        if (provider->items()[i].value == "openai") openai = static_cast<int>(i);
        if (provider->items()[i].value == "integree") integree = static_cast<int>(i);
    }
    check(openai >= 0 && integree >= 0 && provider->items()[static_cast<std::size_t>(openai)].enabled, "les fournisseurs : integree, compatible OpenAI");
    check(!service->visible() || provider->selectedItem()->value == "openai", "le Service cache hors compatible OpenAI");
    provider->setSelectedIndex(openai);
    check(service->visible() && !d->field("effort")->visible(), "compatible OpenAI : le Service a la place de l'Effort");
    check(service->items().size() == 7 && endpoint->text() == "http://localhost:11434/v1", "Ollama par defaut : l'adresse remplie (" + endpoint->text() + ")");
    check(d->chooseService("groq") && endpoint->text() == "https://api.groq.com/openai/v1" && service->selectedItem()->value == "groq", "Groq : son adresse");
    check(d->chooseService("autre") && endpoint->text().empty(), "Autre : l'adresse a la main (celle d'un service s'efface)");
    endpoint->setText("http://192.168.1.20:11434/v1");
    check(d->chooseService("autre") && endpoint->text() == "http://192.168.1.20:11434/v1", "Autre : une adresse ecrite reste");
    (void)d->chooseService("mistral");
    context->setText("8192");
    tool->setSelectedIndex(2);
    budget->setText("200000");
    period->setSelectedIndex(1);
    model->setText("mistral-small-latest");
    const ai::Settings draft = d->draft();
    check(draft.provider == "openai" && draft.preset == "mistral" && draft.endpoint == "https://api.mistral.ai/v1" && draft.contextTokens == 8192
              && draft.toolMode == "texte" && draft.budgetTokens == 200000 && draft.budgetPeriod == "mois",
          "le brouillon : le service, l'adresse, la fenetre, l'outil, le budget");
    check(ai::providerNeedsKey(draft), "Mistral : une cle");
    {
        Recorder rec;
        d->widgetRoot()->setBounds({0, 0, 1280, 800});
        d->widgetRoot()->layout();
        d->widgetRoot()->render(ui::PaintContext{rec, ui::Theme::dark(), {0, 0, 1280, 800}, 0.0, nullptr});
        check(rec.saw("Service") && rec.saw("Fen\xC3\xAAtre de contexte") && rec.saw("Budget (jetons)") && rec.saw("Outil"), "dessinee a 1280 x 800 : les nouvelles lignes");
        const auto ok = findButton(*d->widgetRoot(), "Enregistrer");
        bool inside = ok != nullptr;
        for (ui::Widget* w : {d->field("budget"), d->field("periode"), d->field("memoire"), d->field("tester")})
            inside = inside && w && w->bounds().bottom() <= ok->bounds().y;
        check(inside, "a 1280 x 800 : tout tient au-dessus des boutons");
    }
    // Lister les modeles : GET <adresse>/models, la cle en Authorization, la fenetre de contexte retenue.
    (void)d->chooseService("openrouter");
    model->setText("modele-essai");
    key->setText("cle-or");
    Scripted list;
    list.chunks = {"{\"data\": [{\"id\": \"modele-essai\", \"name\": \"Essai (gratuit)\", \"context_length\": 32768}, {\"id\": \"petit\", \"context_length\": 4096}]}"};
    g_net.push(list);
    check(d->listModels(), "Lister les modeles : part");
    bool got = false;
    for (int k = 0; k < 2000 && !got; ++k) {
        got = d->pollQuery();
        pause(1);
    }
    const auto req = g_net.at(0);
    check(got && d->resultOk() && req.method == "GET" && req.url == "https://openrouter.ai/api/v1/models"
              && std::any_of(req.headers.begin(), req.headers.end(), [](const auto& kv) { return kv.second == "Bearer cle-or"; }),
          "GET <adresse>/models avec la cle (" + req.url + ")");
    context->setText({});
    d->save();
    f.mm.applyPending();
    check(f.as().settings().preset == "openrouter" && f.as().contextWindow() == 32768, "la fenetre Auto : celle que le service dit (32 768)");
    const std::string json = readAll(f.dir / "IA" / "parametres.json");
    check(has(json, "\"service\": \"openrouter\"") && has(json, "\"budget\"") && !has(json, "cle-or"), "parametres.json : le service, le budget, sans la cle");
    ai::setHttpFactory(nullptr);
    ai::setRoot({});
}

// =============================================================================
//  9. De bout en bout : libcurl contre un serveur local compatible OpenAI
// =============================================================================
#if !defined(_WIN32)
class LocalServer {
public:
    using Handler = std::function<std::string(const std::string& head, const std::string& body)>;   // la reponse entiere
    explicit LocalServer(Handler h) : handler_(std::move(h)) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/v1"; }
    std::vector<std::string> received() {
        std::lock_guard<std::mutex> lk(m_);
        return received_;
    }

private:
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
        std::string low = head;
        for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::size_t length = 0;
        if (const auto p = low.find("content-length:"); p != std::string::npos) length = std::strtoul(low.c_str() + p + 15, nullptr, 10);
        while (body.size() < length) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            body.append(buf, static_cast<std::size_t>(n));
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            received_.push_back(head + "\r\n\r\n" + body);
        }
        const std::string out = handler_(head, body);
        std::size_t sent = 0;
        while (sent < out.size()) {
            const ssize_t n = ::send(c, out.data() + sent, std::min<std::size_t>(out.size() - sent, 41), MSG_NOSIGNAL);   // en petits morceaux
            if (n <= 0) return;
            sent += static_cast<std::size_t>(n);
        }
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
#endif

void testCurl() {
    std::printf("[libcurl, compatible OpenAI de bout en bout]\n");
#if defined(_WIN32)
    std::printf("  (sous Windows : WinHTTP, essaye dans l'exe)\n");
#else
    ai::setHttpFactory(nullptr);
    std::string why;
    if (!ai::httpAvailable(&why)) {
        std::printf("  (saute : %s)\n", why.c_str());
        return;
    }
    LocalServer server([](const std::string& head, const std::string& body) -> std::string {
        if (head.rfind("GET ", 0) == 0) {
            const std::string b = "{\"object\": \"list\", \"data\": [{\"id\": \"modele-essai\", \"context_length\": 8192}]}";
            return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(b.size()) + "\r\nConnection: close\r\n\r\n" + b;
        }
        if (has(body, "\"tools\"") && has(body, "sans-outil")) {
            const std::string b = "{\"error\": {\"message\": \"sans-outil does not support tools\"}}";
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(b.size()) + "\r\nConnection: close\r\n\r\n" + b;
        }
        const std::string stream = has(body, "sans-outil") ? streamText("Voici.\n\n```json\n" + kTankInput + "\n```") : streamTool();
        return "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nx-ratelimit-limit-requests: 50\r\nx-ratelimit-remaining-requests: 49\r\n"
               "x-ratelimit-reset-requests: 1m12s\r\nConnection: close\r\n\r\n" + stream;
    });
    check(server.ok(), "le serveur local ecoute (" + server.url() + ")");
    if (!server.ok()) return;
    {
        ai::OpenAIProvider p(fastRetries());
        auto r = simpleRequest();
        r.endpoint = server.url();
        r.apiKey.clear();
        check(p.start(r, &why), "start vers " + server.url() + " (" + why + ")");
        const auto ev = runToEnd(p, 10000);
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        const auto* lim = find(ev, ai::ProviderEvent::Type::Usage);
        check(allText(ev) == "Je propose une cuve." && tool && tool->toolInputJson == kTankInput, "libcurl : le texte, l'appel complet");
        check(lim && lim->limits.requestsRemaining == 49 && std::abs(lim->limits.requestsResetSeconds - 72) < 1e-6, "libcurl : la limite des en-tetes (49/50, 1m12s)");
        const auto got = server.received();
        check(!got.empty() && has(got.front(), "POST /v1/chat/completions") && !has(got.front(), "uthorization"), "la requete recue : sans cle, sans Authorization");
    }
    {
        ai::OpenAIProvider p(fastRetries());
        auto r = simpleRequest();
        r.endpoint = server.url();
        r.model = "sans-outil";
        r.apiKey.clear();
        (void)p.start(r);
        const auto ev = runToEnd(p, 10000);
        const auto* tool = find(ev, ai::ProviderEvent::Type::ToolUse);
        check(hasStatus(ev, "sans-outil") && tool && tool->toolInputJson == kTankInput, "libcurl : le modele sans outil, la proposition en texte");
    }
    {
        ai::ModelQuery q;
        check(q.startUrl(server.url() + "/models", ai::openAIHeaders(""), 30, true, false, &why), "libcurl : Lister les modeles");
        std::optional<ai::ModelsResult> res;
        for (int k = 0; k < 5000 && !res; ++k) {
            res = q.poll();
            pause(2);
        }
        check(res && res->ok && res->models.size() == 1 && res->models[0].contextTokens == 8192, "libcurl : GET /v1/models et sa fenetre de contexte");
    }
#endif
}

} // namespace

int main() {
    ai::setClock([] { return g_now ? g_now : std::time(nullptr); });
    g_now = at(2026, 10, 31);
    {
        ui::PlatformServices s;
        s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(t.size()) * f.v * 0.5f; };
        s.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v); };
        ui::installPlatformServices(std::move(s));
    }
    testServices();
    testBody();
    testStream();
    testErrorsAndText();
    testLimits();
    testUsage();
    testProvider();
    testPanelBand();
    testAssistantTextTool();
    testSettings();
    testCurl();
    std::printf("ai_compatible_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
