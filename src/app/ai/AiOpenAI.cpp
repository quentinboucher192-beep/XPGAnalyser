// =============================================================================
//  app/ai/AiOpenAI.cpp - un service compatible OpenAI (1.16.0) : voir AiOpenAI.hpp
// =============================================================================
#include "AiOpenAI.hpp"
#include "AiTextParts.hpp"

#include "AiJson.hpp"
#include "AiPrompt.hpp"
#include "AiStore.hpp"
#include "AiUsage.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <filesystem>

namespace app::ai {

namespace fs = std::filesystem;

const std::vector<ServicePreset>& servicePresets() {
    // Les adresses publiques des services ; aucune cle, aucun modele.
    static const std::vector<ServicePreset> k = {
        {"ollama", "Ollama (sur ce PC, gratuit)", "http://localhost:11434/v1", true, false},
        {"lmstudio", "LM Studio (sur ce PC, gratuit)", "http://localhost:1234/v1", true, false},
        {"openrouter", "OpenRouter (mod\xC3\xA8les gratuits ou payants)", "https://openrouter.ai/api/v1", false, true},
        {"groq", "Groq", "https://api.groq.com/openai/v1", false, true},
        {"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai", false, true},
        {"mistral", "Mistral", "https://api.mistral.ai/v1", false, true},
        {"autre", "Autre service compatible", "", false, false},
    };
    return k;
}

const ServicePreset* findServicePreset(std::string_view key) noexcept {
    for (const auto& p : servicePresets())
        if (p.key == key) return &p;
    return nullptr;
}

std::unique_ptr<Provider> makeOpenAIProvider() { return std::make_unique<OpenAIProvider>(); }

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool blank(std::string_view s) { return trim(s).empty(); }

std::string guillemets(std::string_view name) { return "\xC2\xAB " + std::string(name) + " \xC2\xBB"; }

ProviderEvent statusEvent(std::string text, std::string detail = {}) {
    ProviderEvent e;
    e.type = ProviderEvent::Type::Status;
    e.text = std::move(text);
    e.detail = std::move(detail);
    return e;
}
ProviderEvent errorEvent(std::string text, int status, bool retryable) {
    ProviderEvent e;
    e.type = ProviderEvent::Type::Error;
    e.text = std::move(text);
    e.httpStatus = status;
    e.retryable = retryable;
    return e;
}
ProviderEvent doneEvent(std::string reason) {
    ProviderEvent e;
    e.type = ProviderEvent::Type::Done;
    e.stopReason = std::move(reason);
    return e;
}

std::string secondsText(double s) {
    const int n = std::max(1, static_cast<int>(std::lround(s)));
    return std::to_string(n) + "\xC2\xA0s";
}

bool readFile(const std::string& path, std::string& out) {
    if (path.empty()) return false;
    return core::readFileAll(fs::path(std::u8string(path.begin(), path.end())), out);
}

// Le type reel d'une image d'apres ses premiers octets.
std::string imageType(std::string_view bytes, const std::string& fallback) {
    if (bytes.size() >= 8 && static_cast<unsigned char>(bytes[0]) == 0x89 && bytes.substr(1, 3) == "PNG") return "image/png";
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xD8) return "image/jpeg";
    return fallback;
}

// 1.20.0 : la fenetre supposee d'un modele dont la liste ne dit rien - un service sur ce PC (Ollama, LM Studio :
// souvent 8 a 32 k), un service en ligne (128 k).
int openAIWindow(const ProviderRequest& req) {
    if (req.contextTokens > 0) return req.contextTokens;
    return localService(req.endpoint, req.preset) ? 32768 : textparts::kUnknownWindow;
}

// Le contenu d'un message en cours de construction : du texte, des images.
struct Part {
    bool        image{false};
    std::string text;   // le texte, ou l'adresse data: de l'image
};

json::Value contentOf(const std::vector<Part>& parts) {
    const bool images = std::any_of(parts.begin(), parts.end(), [](const Part& p) { return p.image; });
    if (!images) {
        std::string t;
        for (const auto& p : parts) t += (t.empty() ? "" : "\n\n") + p.text;
        return json::Value(t);
    }
    json::Value a = json::Value::array();
    for (const auto& p : parts) {
        json::Value o = json::Value::object();
        if (p.image) {
            o.set("type", "image_url");
            json::Value u = json::Value::object();
            u.set("url", p.text);
            o.set("image_url", std::move(u));
        } else {
            o.set("type", "text");
            o.set("text", p.text);
        }
        a.push(std::move(o));
    }
    return a;
}

// La fin d'un flux : sa raison, au vocabulaire du logiciel (celui d'Anthropic).
std::string stopReasonOf(std::string_view finish) {
    if (finish == "tool_calls" || finish == "function_call") return "tool_use";
    if (finish == "length") return "max_tokens";
    if (finish == "content_filter") return "refusal";
    return "end_turn";
}

} // namespace

// ---- les adresses --------------------------------------------------------------------------------
bool openAITarget(std::string_view endpoint, std::string_view preset, bool hasKey, OpenAITarget& out, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    std::string text = trim(endpoint);
    const ServicePreset* sp = findServicePreset(preset);
    if (text.empty() && sp) text = std::string(sp->endpoint);
    if (text.empty())
        return fail("Entre l'adresse du service dans les Param\xC3\xA8tres de l'Assistant IA (par exemple http://localhost:11434/v1 pour Ollama).");
    Url u;
    std::string w;
    if (!parseUrl(text, u, &w, true)) return fail(w);
    if (!u.https && !u.loopback() && u.privateLan() && hasKey)
        return fail(guillemets(text) + " : http:// vers une autre machine passe en clair, la cl\xC3\xA9 voyagerait sur le r\xC3\xA9seau : "
                    "accept\xC3\xA9 seulement SANS cl\xC3\xA9 (un serveur Ollama de l'usine). Avec une cl\xC3\xA9, utilise https://.");
    std::string path = u.path;
    if (const auto q = path.find('?'); q != std::string::npos) path.resize(q);
    while (!path.empty() && path.back() == '/') path.pop_back();
    const std::string tail = "/chat/completions";
    if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) path.resize(path.size() - tail.size());
    while (!path.empty() && path.back() == '/') path.pop_back();
    const std::string base = u.base() + path;
    out.chat = base + "/chat/completions";
    out.models = base + "/models";
    out.privateHttp = !u.https && u.privateLan();
    out.local = u.loopback() || u.privateLan() || (sp && sp->local && trim(endpoint).empty());
    return true;
}

bool localService(std::string_view endpoint, std::string_view preset) {
    OpenAITarget t;
    if (openAITarget(endpoint, preset, false, t)) return t.local;
    const ServicePreset* sp = findServicePreset(preset);
    return sp && sp->local;
}

HttpHeaders openAIHeaders(const std::string& apiKey) {
    HttpHeaders h;
    h.emplace_back("content-type", "application/json");
    if (!apiKey.empty()) h.emplace_back("authorization", "Bearer " + apiKey);
    return h;
}

// ---- la requete -------------------------------------------------------------------------------------
std::string openAIBody(const ProviderRequest& req, bool withTools, bool streamUsage, std::vector<std::string>* notes) {
    const auto note = [&](std::string n) {
        if (notes && !n.empty() && std::find(notes->begin(), notes->end(), n) == notes->end()) notes->push_back(std::move(n));
    };
    const bool hasTool = !req.toolsJson.empty();
    const bool textTool = hasTool && !withTools;
    std::uintmax_t heavy = 0;
    std::size_t textBudget = textparts::budgetBytes(req, openAIWindow(req));   // les textes que prepare n'a pas vus
    const auto attachmentParts = [&](const ProviderAttachment& a, std::vector<Part>& parts) {
        const AttachmentPlan plan = planAttachment(a);
        if (plan.as == AttachmentPlan::As::Text && a.prepared) {   // 1.20.0 : decoupe sur le fil (ses parties)
            for (const auto& b : a.blocks) parts.push_back({false, b});
            return;
        }
        const std::string head = "Pi\xC3\xA8" "ce jointe " + guillemets(a.name);
        if (plan.as == AttachmentPlan::As::NotRead || plan.as == AttachmentPlan::As::TooBig) {
            note(plan.note);
            parts.push_back({false, "[" + head + " : pas envoy\xC3\xA9" "e. Si son contenu compte, demande-le \xC3\xA0 l'utilisateur.]"});
            return;
        }
        if (plan.as == AttachmentPlan::As::Document) {   // un PDF : ces services ne le lisent pas
            note(guillemets(a.name) + " (PDF) n'est pas lu par ce service : seul son nom est envoy\xC3\xA9.");
            parts.push_back({false, "[" + head + " (PDF) : ce service ne lit pas les PDF. Si son contenu compte, demande-le \xC3\xA0 l'utilisateur.]"});
            return;
        }
        std::string bytes;
        if (!readFile(a.path, bytes)) {
            note(guillemets(a.name) + " est introuvable dans le dossier de l'Assistant : seul son nom est envoy\xC3\xA9.");
            parts.push_back({false, "[" + head + " : le fichier est introuvable sur le poste.]"});
            return;
        }
        if (plan.as == AttachmentPlan::As::Text) {
            // 1.20.0 : la limite verifiee (la fenetre du modele) ; le texte part en parties, ce qui ne tient pas est dit
            auto prep = textparts::prepareOne(a, textparts::normalize(std::move(bytes)), textBudget);
            note(prep.note);
            for (auto& b : prep.blocks) {
                textBudget = textBudget > b.size() ? textBudget - b.size() : 0;
                parts.push_back({false, std::move(b)});
            }
            return;
        }
        std::string data = base64(bytes);
        if (heavy + data.size() > kMaxRequestBytes) {
            note(guillemets(a.name) + " n'est pas envoy\xC3\xA9" "e : la demande serait trop lourde.");
            parts.push_back({false, "[" + head + " : pas envoy\xC3\xA9" "e, la demande serait trop lourde.]"});
            return;
        }
        heavy += data.size();
        parts.push_back({true, "data:" + imageType(bytes, plan.mediaType) + ";base64," + data});
    };

    struct Msg {
        std::string       role;   // user, assistant, tool
        std::vector<Part> parts;
        std::string       toolId, toolName, toolArgs;   // assistant : son appel ; tool : l'id
    };
    std::vector<Msg> msgs;
    std::string pendingTool;   // l'id de l'appel du tour precedent (son resultat suit)
    for (const auto& t : req.turns) {
        if (t.role == ProviderTurn::Role::User) {
            Msg m;
            m.role = "user";
            if (!pendingTool.empty()) {
                const bool matches = t.toolResultFor == pendingTool;
                const std::string result = matches && !blank(t.toolResultText) ? t.toolResultText : std::string("sans suite connue");
                if (textTool) {
                    m.parts.push_back({false, "[R\xC3\xA9sultat de ta proposition pr\xC3\xA9" "c\xC3\xA9" "dente" + std::string(matches && t.toolResultError ? " (refus\xC3\xA9" "e)" : "")
                                                  + " : " + result + "]"});
                } else {
                    Msg r;
                    r.role = "tool";
                    r.toolId = pendingTool;
                    r.parts.push_back({false, (matches && t.toolResultError ? "Erreur : " : "") + result});
                    msgs.push_back(std::move(r));
                }
                pendingTool.clear();
            }
            for (const auto& a : t.attachments) attachmentParts(a, m.parts);
            if (!blank(t.text)) m.parts.push_back({false, t.text});
            if (m.parts.empty()) m.parts.push_back({false, "(message sans texte)"});
            if (!msgs.empty() && msgs.back().role == "user") {
                for (auto& p : m.parts) msgs.back().parts.push_back(std::move(p));
            } else {
                msgs.push_back(std::move(m));
            }
        } else {
            Msg m;
            m.role = "assistant";
            if (!blank(t.text)) m.parts.push_back({false, t.text});
            const bool call = !t.toolUseId.empty() && !t.toolName.empty();
            if (call) {
                if (textTool) {
                    m.parts.push_back({false, proposalBlock(t.toolInputJson)});
                } else {
                    m.toolId = t.toolUseId;
                    m.toolName = t.toolName;
                    json::Value input;
                    m.toolArgs = json::parse(t.toolInputJson, input) && input.isObject() ? json::write(input, false) : std::string("{}");
                }
                pendingTool = t.toolUseId;
            } else {
                pendingTool.clear();
            }
            if (m.parts.empty() && m.toolId.empty()) continue;   // un tour vide : rien
            bool hasUser = false;
            for (const auto& x : msgs) hasUser = hasUser || x.role == "user";
            if (!hasUser) continue;   // la conversation commence par l'utilisateur
            if (!msgs.empty() && msgs.back().role == "assistant" && msgs.back().toolId.empty()) {
                for (auto& p : m.parts) msgs.back().parts.push_back(std::move(p));
                msgs.back().toolId = m.toolId;
                msgs.back().toolName = m.toolName;
                msgs.back().toolArgs = m.toolArgs;
            } else {
                msgs.push_back(std::move(m));
            }
        }
    }
    while (!msgs.empty() && msgs.back().role != "user") msgs.pop_back();   // le dernier tour : l'utilisateur

    json::Value body = json::Value::object();
    body.set("model", req.model);
    body.set("max_tokens", req.maxTokens > 0 ? req.maxTokens : 16000);
    body.set("stream", true);
    if (streamUsage) {
        json::Value so = json::Value::object();
        so.set("include_usage", true);
        body.set("stream_options", std::move(so));
    }
    json::Value list = json::Value::array();
    std::string system = !req.systemStable.empty() ? req.systemStable + req.systemDynamic : req.system;
    if (textTool) system += textToolInstructions();
    if (!blank(system)) {
        json::Value o = json::Value::object();
        o.set("role", "system");
        o.set("content", system);
        list.push(std::move(o));
    }
    for (auto& m : msgs) {
        json::Value o = json::Value::object();
        o.set("role", m.role);
        if (m.role == "tool") {
            o.set("tool_call_id", m.toolId);
            o.set("content", m.parts.empty() ? std::string{} : m.parts.front().text);
        } else if (m.role == "assistant") {
            if (m.parts.empty()) o.set("content", json::Value{});
            else o.set("content", contentOf(m.parts));
            if (!m.toolId.empty()) {
                json::Value fn = json::Value::object();
                fn.set("name", m.toolName);
                fn.set("arguments", m.toolArgs);
                json::Value call = json::Value::object();
                call.set("id", m.toolId);
                call.set("type", "function");
                call.set("function", std::move(fn));
                json::Value calls = json::Value::array();
                calls.push(std::move(call));
                o.set("tool_calls", std::move(calls));
            }
        } else {
            o.set("content", contentOf(m.parts));
        }
        list.push(std::move(o));
    }
    body.set("messages", std::move(list));
    if (withTools && hasTool) {
        // L'outil au format "function" : le schema de l'outil d'Anthropic (input_schema) en parametres.
        json::Value tools;
        json::Value out = json::Value::array();
        if (json::parse(req.toolsJson, tools) && tools.isArray())
            for (const auto& t : tools.items()) {
                json::Value fn = json::Value::object();
                fn.set("name", t.str("name"));
                fn.set("description", t.str("description"));
                fn.set("parameters", t.at("input_schema").isObject() ? t.at("input_schema") : json::Value::object());
                json::Value o = json::Value::object();
                o.set("type", "function");
                o.set("function", std::move(fn));
                out.push(std::move(o));
            }
        if (!out.items().empty()) body.set("tools", std::move(out));
    }
    return json::write(body, false);
}

// ---- les erreurs ----------------------------------------------------------------------------------
ApiError openAIError(int status, std::string_view body, std::string_view model) {
    ApiError e;
    json::Value v;
    const bool isJson = json::parse(body, v) && v.isObject();
    if (isJson) {
        const auto& err = v.at("error");
        if (err.isObject()) {
            e.message = err.str("message");
            e.type = err.str("type");
            if (status == 0) {
                const auto& code = err.at("code");
                status = code.isNumber() ? static_cast<int>(code.asInt()) : std::atoi(code.asString().c_str());
            }
        } else if (err.isString()) {
            e.message = err.asString();   // Ollama : {"error": "model \"x\" not found"}
        } else {
            e.message = v.str("message", v.str("detail"));
        }
    } else if (!blank(body) && body.size() < 300 && body.find('<') == std::string_view::npos) {
        e.message = trim(body);
    }
    if (e.message.size() > 300) e.message = e.message.substr(0, 300) + "...";
    if (status <= 0) status = 500;
    e.status = status;
    const std::string said = e.message.empty() ? std::string{} : " : " + e.message;
    const std::string low = lower(e.message);
    switch (status) {
        case 400: e.text = "Demande refus\xC3\xA9" "e par le service (400)" + said; break;
        case 401: e.text = "Cl\xC3\xA9 API refus\xC3\xA9" "e ou absente (401) : v\xC3\xA9rifie-la dans les Param\xC3\xA8tres de l'Assistant IA" + said; break;
        case 402: e.text = "Le service demande un paiement ou des cr\xC3\xA9" "dits (402) : choisis un mod\xC3\xA8le gratuit, ou v\xC3\xA9rifie ton compte" + said; break;
        case 403: e.text = "Acc\xC3\xA8s refus\xC3\xA9 (403) : cette cl\xC3\xA9 n'a pas le droit de faire cette demande" + said; break;
        case 404:
            if (!isJson && e.message.empty()) e.text = "Adresse introuvable (404) : v\xC3\xA9rifie l'adresse du service dans les Param\xC3\xA8tres (elle finit en g\xC3\xA9n\xC3\xA9ral par /v1).";
            else if (low.find("model") != std::string::npos || e.message.empty())
                e.text = "Mod\xC3\xA8le introuvable : " + guillemets(model) + " (404). Choisis-en un avec \xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB dans les Param\xC3\xA8tres "
                         "(Ollama : il faut d'abord le t\xC3\xA9l\xC3\xA9" "charger, ollama pull)" + said;
            else e.text = "Introuvable (404)" + said;
            break;
        case 413: e.text = "La demande est trop lourde (413) : retire des pi\xC3\xA8" "ces jointes, ou commence une nouvelle conversation."; break;
        case 429:
            e.text = "Trop de demandes (429) : la limite du service est atteinte" + said;
            e.retryable = true;
            break;
        case 503:
            e.text = "Le service est surcharg\xC3\xA9 ou indisponible (503)" + said;
            e.retryable = true;
            break;
        default:
            if (status >= 500) {
                e.text = "Le service ne r\xC3\xA9pond pas correctement (" + std::to_string(status) + ")" + said;
                e.retryable = true;
            } else {
                e.text = "Le service a r\xC3\xA9pondu " + std::to_string(status) + said;
            }
    }
    if (!e.text.empty() && e.text.back() != '.' && e.text.back() != ')') e.text += ".";
    return e;
}

bool toolsRefused(const ApiError& e) {
    if (e.status != 400 && e.status != 404 && e.status != 422 && e.status != 501) return false;
    const std::string m = lower(e.message);
    if (m.find("tool") == std::string::npos && m.find("function") == std::string::npos) return false;
    return m.find("support") != std::string::npos || m.find("not available") != std::string::npos || m.find("not enabled") != std::string::npos
           || m.find("does not") != std::string::npos || m.find("unsupported") != std::string::npos;
}

bool streamOptionsRefused(const ApiError& e) {
    return e.status >= 400 && e.status < 500 && lower(e.message).find("stream_options") != std::string::npos;
}

// ---- la liste des modeles ----------------------------------------------------------------------------
ModelsResult parseOpenAIModels(int status, std::string_view body) {
    ModelsResult r;
    r.httpStatus = status;
    if (status != 200) {
        r.error = openAIError(status, body, "").text;
        if (status == 404) r.error = "Adresse introuvable (404) : v\xC3\xA9rifie l'adresse du service dans les Param\xC3\xA8tres (elle finit en g\xC3\xA9n\xC3\xA9ral par /v1).";
        return r;
    }
    json::Value v;
    const json::Value* list = nullptr;
    if (json::parse(body, v)) {
        if (v.isArray()) list = &v;
        else if (v.isObject() && v.at("data").isArray()) list = &v.at("data");
        else if (v.isObject() && v.at("models").isArray()) list = &v.at("models");
    }
    if (!list) {
        r.error = "R\xC3\xA9ponse illisible : ce n'est pas la liste des mod\xC3\xA8les (l'adresse est-elle la bonne ? elle finit en g\xC3\xA9n\xC3\xA9ral par /v1).";
        return r;
    }
    for (const auto& m : list->items()) {
        ModelInfo info;
        info.id = m.str("id", m.str("name"));
        info.name = m.str("name", m.str("display_name", info.id));
        for (const char* k : {"context_length", "context_window", "max_context_length", "max_input_tokens", "contextLength"})
            if (info.contextTokens <= 0 && m.at(k).isNumber()) info.contextTokens = std::max<long long>(0, m.integer(k));
        if (info.contextTokens <= 0 && m.at("top_provider").isObject()) info.contextTokens = std::max<long long>(0, m.at("top_provider").integer("context_length"));
        if (!info.id.empty()) r.models.push_back(std::move(info));
    }
    r.ok = true;
    return r;
}

// ---- le flux ------------------------------------------------------------------------------------------
void OpenAIStream::status(const char* s) {
    if (phase_ == s) return;
    phase_ = s;
    out_.push_back(statusEvent(s));
}

void OpenAIStream::feed(std::string_view chunk) {
    if (finished_ || failed_) return;
    std::vector<SseEvent> events;
    sse_.feed(chunk, events);
    for (const auto& e : events) {
        handle(e.data);
        if (finished_ || failed_) break;
    }
}

void OpenAIStream::end() {
    if (finished_ || failed_) return;
    std::vector<SseEvent> events;
    sse_.finish(events);
    for (const auto& e : events) {
        handle(e.data);
        if (finished_ || failed_) return;
    }
    // Sans [DONE] : une fin donnee (finish_reason) suffit ; sinon la connexion a ete coupee.
    if (!finishReason_.empty()) {
        finish();
        return;
    }
    failed_ = true;
    error_ = {};
    error_.text = "La connexion a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e avant la fin de la r\xC3\xA9ponse.";
    error_.retryable = true;
}

std::vector<ProviderEvent> OpenAIStream::take() { return std::exchange(out_, {}); }

void OpenAIStream::handle(const std::string& data) {
    const std::string d = trim(data);
    if (d == "[DONE]") {
        finish();
        return;
    }
    json::Value v;
    if (!json::parse(d, v) || !v.isObject()) return;   // une ligne illisible : ignoree
    if (v.has("error")) {
        error_ = openAIError(0, d, model_);
        failed_ = true;
        return;
    }
    if (phase_.empty()) status("reflexion");
    if (const auto& u = v.at("usage"); u.isObject()) {
        // L'usage (le dernier morceau) : les jetons d'entree dont ceux lus du cache, la sortie.
        const long long prompt = u.integer("prompt_tokens");
        const long long cached = std::clamp<long long>(u.at("prompt_tokens_details").integer("cached_tokens"), 0, prompt);
        ProviderUsage nu;
        nu.input = prompt - cached;
        nu.cacheRead = cached;
        nu.output = u.integer("completion_tokens");
        if (!nu.empty() && (nu.input != usage_.input || nu.output != usage_.output || nu.cacheRead != usage_.cacheRead)) {
            usage_ = nu;
            ProviderEvent ue;
            ue.type = ProviderEvent::Type::Usage;
            ue.usage = usage_;
            out_.push_back(std::move(ue));
        }
    }
    const auto& choices = v.at("choices");
    if (!choices.isArray() || choices.items().empty()) return;
    const auto& c = choices.items().front();
    const auto& delta = c.at("delta");
    if (delta.isObject()) {
        if (delta.at("reasoning").isString() || delta.at("reasoning_content").isString()) status("reflexion");
        if (const auto& t = delta.at("content"); t.isString() && !t.asString().empty()) {
            status("ecriture");
            text_ += t.asString();
            ProviderEvent te;
            te.type = ProviderEvent::Type::Text;
            te.text = t.asString();
            out_.push_back(std::move(te));
            emitted_ = true;
        }
        if (const auto& calls = delta.at("tool_calls"); calls.isArray()) {
            status("operations");
            for (const auto& tc : calls.items()) {
                const int index = static_cast<int>(tc.integer("index", static_cast<std::int64_t>(calls_.size())));
                Call& call = calls_[index];
                if (const std::string id = tc.str("id"); !id.empty()) call.id = id;
                const auto& fn = tc.at("function");
                if (const std::string name = fn.str("name"); !name.empty()) call.name = name;
                const auto& args = fn.at("arguments");
                if (args.isString()) call.args += args.asString();
                else if (args.isObject()) call.args += json::write(args, false);   // certains services : deja un objet
            }
        }
    }
    if (const auto& f = c.at("finish_reason"); f.isString() && !f.asString().empty()) finishReason_ = f.asString();
}

void OpenAIStream::finish() {
    if (finished_) return;
    finished_ = true;
    const std::string reason = finishReason_.empty() && !calls_.empty() ? std::string("tool_use") : stopReasonOf(finishReason_);
    // Un appel n'est propose que si la reponse s'est finie normalement (coupee : l'entree peut etre tronquee).
    const bool complete = reason == "end_turn" || reason == "tool_use";
    if (complete) {
        int n = 0;
        for (auto& [i, call] : calls_) {
            ProviderEvent te;
            te.type = ProviderEvent::Type::ToolUse;
            te.toolUseId = call.id.empty() ? "appel-" + std::to_string(++n) : call.id;
            te.toolName = call.name;
            te.toolInputJson = blank(call.args) ? std::string("{}") : call.args;
            out_.push_back(std::move(te));
            emitted_ = true;
        }
        // Un modele sans appel d'outil : la proposition est un bloc ```json du texte.
        std::string block;
        if (calls_.empty() && textTool_ && extractProposalBlock(text_, &block)) {
            ProviderEvent te;
            te.type = ProviderEvent::Type::ToolUse;
            te.toolUseId = "texte-" + std::to_string(std::hash<std::string>{}(block) % 100000000);
            te.toolName = std::string(kOperationsTool);
            te.toolInputJson = block;
            out_.push_back(std::move(te));
        }
    }
    out_.push_back(doneEvent(calls_.empty() || !complete ? reason : std::string("tool_use")));
}

// ---- le fournisseur -------------------------------------------------------------------------------------
OpenAIProvider::OpenAIProvider() : OpenAIProvider(Options{}) {}
OpenAIProvider::OpenAIProvider(Options o) : opt_(o) {}

OpenAIProvider::~OpenAIProvider() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

bool OpenAIProvider::start(ProviderRequest request, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (busy_) return fail("Une r\xC3\xA9ponse est d\xC3\xA9j\xC3\xA0 en cours.");
    if (blank(request.model))
        return fail("Choisis un mod\xC3\xA8le dans les Param\xC3\xA8tres de l'Assistant IA (\xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB).");
    OpenAITarget target;
    std::string w;
    if (!openAITarget(request.endpoint, request.preset, !request.apiKey.empty(), target, &w)) return fail(w);
    if (!httpAvailable(&w)) return fail("Pas de connexion possible sur ce poste : " + w + ".");
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lk(m_);
        events_.clear();
        http_ = std::shared_ptr<HttpClient>(makeHttpClient());
    }
    cancelled_ = false;
    busy_ = true;
    thread_ = std::thread([this, r = std::move(request), t = std::move(target)]() mutable { run(std::move(r), std::move(t)); });
    return true;
}

std::vector<ProviderEvent> OpenAIProvider::poll() {
    std::lock_guard<std::mutex> lk(m_);
    return std::exchange(events_, {});
}

void OpenAIProvider::cancel() {
    cancelled_ = true;
    std::shared_ptr<HttpClient> http;
    {
        std::lock_guard<std::mutex> lk(m_);
        http = http_;
    }
    if (http) http->cancel();
    cv_.notify_all();
}

void OpenAIProvider::push(ProviderEvent e) {
    std::lock_guard<std::mutex> lk(m_);
    events_.push_back(std::move(e));
}

bool OpenAIProvider::waitFor(double seconds) {
    std::unique_lock<std::mutex> lk(m_);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
    cv_.wait_until(lk, until, [this] { return cancelled_.load(); });
    return !cancelled_;
}

void OpenAIProvider::run(ProviderRequest request, OpenAITarget target) {
    std::shared_ptr<HttpClient> http;
    {
        std::lock_guard<std::mutex> lk(m_);
        http = http_;
    }
    const auto finish = [this](ProviderEvent last) {
        push(std::move(last));
        busy_ = false;
    };
    const auto autoRetry = [](const ApiError& e) { return e.retryable && (e.status == 429 || e.status >= 500); };
    bool withTools = !request.toolsJson.empty() && !request.toolsInText;
    bool streamUsage = true;
    push(statusEvent("connexion"));
    HttpRequest hr;
    hr.method = "POST";
    hr.url = target.chat;
    hr.headers = openAIHeaders(request.apiKey);
    hr.connectTimeoutSeconds = opt_.connectTimeout;
    hr.idleTimeoutSeconds = std::clamp(request.timeoutSeconds, 5, 600);
    hr.allowPrivateHttp = target.privateHttp && request.apiKey.empty();
    request.apiKey.clear();
    // 1.20.0 : les textes joints, a la mesure de la fenetre du modele - entiers en parties, ou les parties en trop lues a
    // part (une demande chacune, sans outil) ; ici, sur le fil, jamais sur celui de l'interface. 1.20.1 : et leur texte
    // exact lisible a la demande (l'outil lire_piece_jointe, repondu ici ; seulement avec l'appel d'outil natif).
    textparts::Library library;
    {
        std::vector<std::string> notes;
        const auto ask = [&](const ProviderRequest& mini, std::string* err) -> std::optional<std::string> {
            HttpRequest q = hr;
            q.body = openAIBody(mini, false, streamUsage);
            double wait = opt_.firstDelay;
            for (int tries = 0;; ++tries) {
                if (cancelled_) return std::nullopt;
                OpenAIStream st;
                st.setModel(mini.model);
                int status = 0;
                std::string errBody, text;
                ProviderUsage used;
                const auto take = [&] {
                    for (auto& e : st.take()) {
                        if (e.type == ProviderEvent::Type::Text) text += e.text;
                        else if (e.type == ProviderEvent::Type::Usage && !e.usage.empty()) used = e.usage;
                    }
                };
                HttpSink sink;
                sink.onHeaders = [&](int s, const HttpHeaders&) { status = s; };
                sink.onData = [&](std::string_view chunk) {
                    if (status == 200) {
                        st.feed(chunk);
                        take();
                    } else if (errBody.size() < 64 * 1024) {
                        errBody.append(chunk.data(), std::min<std::size_t>(chunk.size(), 64 * 1024 - errBody.size()));
                    }
                };
                HttpResponse r = http->perform(q, sink);
                if (cancelled_ || r.failure == HttpResponse::Failure::Cancelled) return std::nullopt;
                if (status == 0) status = r.status;
                if (status == 200) {
                    st.end();
                    take();
                    if (!used.empty()) {
                        ProviderEvent u;
                        u.type = ProviderEvent::Type::Usage;
                        u.usage = used;
                        u.preread = true;
                        push(std::move(u));
                    }
                    if (st.finished() && !text.empty()) return text;
                    if (err) *err = !r.ok() ? r.error : st.error().text.empty() ? std::string("r\xC3\xA9ponse vide") : st.error().text;
                    if (!autoRetry(st.error()) || tries >= opt_.maxRetries) return std::nullopt;
                } else if (status == 0) {
                    if (err) *err = r.error;
                    return std::nullopt;
                } else {
                    const ApiError e = openAIError(status, errBody, mini.model);
                    if (err) *err = e.text;
                    if (!autoRetry(e) || tries >= opt_.maxRetries) return std::nullopt;
                }
                if (!waitFor(wait)) return std::nullopt;
                wait *= opt_.factor;
            }
        };
        textparts::prepare(
            request, openAIWindow(request), ask, [this](const std::string& detail) { push(statusEvent("lecture", detail)); },
            [this] { return cancelled_.load(); }, &notes, withTools ? &library : nullptr);
        if (cancelled_) return finish(doneEvent("arrete"));
        if (library.active()) request.toolsJson = textparts::withReadTool(request.toolsJson);
        if (!notes.empty()) {
            std::string all;
            for (const auto& n : notes) all += (all.empty() ? "" : " ") + n;
            push(statusEvent("note", all));
            push(statusEvent("connexion"));
        }
    }
    double delay = opt_.firstDelay;
    int attempt = 0;
    // 1.20.1 : un appel de l'outil de lecture est repondu ICI (le texte exact), puis la demande continue.
    std::string turnText;
    std::optional<ProviderEvent> readCall, heldDone;
    bool otherTool = false;
    int reads = 0;
    const auto route = [&](ProviderEvent e) {
        if (library.active() && withTools) {
            if (e.type == ProviderEvent::Type::ToolUse && e.toolName == textparts::kReadTool) {
                if (!readCall) readCall = std::move(e);
                return;
            }
            if (e.type == ProviderEvent::Type::ToolUse) otherTool = true;
            if (e.type == ProviderEvent::Type::Done && readCall && !otherTool) {
                if (reads < textparts::kMaxReads) {
                    heldDone = std::move(e);
                    return;
                }
                ProviderEvent t;
                t.type = ProviderEvent::Type::Text;
                t.text = "\n\n[Lectures exactes des pi\xC3\xA8" "ces jointes : la limite de cette demande est atteinte.]";
                push(std::move(t));
            }
            if (e.type == ProviderEvent::Type::Text) turnText += e.text;
        }
        push(std::move(e));
    };
    for (int round = 0; round < 8 + textparts::kMaxReads; ++round) {
        if (cancelled_) return finish(doneEvent("arrete"));
        if (round > 0) push(statusEvent("connexion"));
        hr.body = openAIBody(request, withTools, streamUsage);   // lit les pieces jointes : ici, sur le fil
        OpenAIStream stream;
        stream.setModel(request.model);
        stream.setTextTool(!withTools && !request.toolsJson.empty());
        int status = 0;
        std::string errBody;
        HttpSink sink;
        sink.onHeaders = [&](int s, const HttpHeaders& h) {
            status = s;
            // La limite du service (une 429 : l'attente) ; un modele sur ce PC : pas de limite.
            ProviderEvent le;
            le.type = ProviderEvent::Type::Usage;
            le.limits = limitsFromHeaders(h, now());
            if (!le.limits.known() && target.local) le.limits.local = true;
            if (le.limits.known() || le.limits.retryAfterSeconds >= 0) push(std::move(le));
        };
        sink.onData = [&](std::string_view chunk) {
            if (status == 200) {
                stream.feed(chunk);
                for (auto& e : stream.take()) route(std::move(e));
            } else if (errBody.size() < 64 * 1024) {
                errBody.append(chunk.data(), std::min<std::size_t>(chunk.size(), 64 * 1024 - errBody.size()));
            }
        };
        HttpResponse r = http->perform(hr, sink);
        if (cancelled_ || r.failure == HttpResponse::Failure::Cancelled) return finish(doneEvent("arrete"));
        if (status == 0) status = r.status;
        std::optional<ApiError> retry;
        if (status == 200) {
            stream.end();
            for (auto& e : stream.take()) route(std::move(e));
            if (stream.finished() && heldDone && readCall) {
                // 1.20.1 : l'IA demande le texte exact de lignes d'une piece jointe : la reponse, puis la suite de la demande.
                ++reads;
                std::string detail;
                const std::string result = library.read(readCall->toolInputJson, &detail);
                push(statusEvent("lecture", detail));
                ProviderTurn a;
                a.role = ProviderTurn::Role::Assistant;
                a.text = turnText;
                a.toolUseId = readCall->toolUseId;
                a.toolName = readCall->toolName;
                a.toolInputJson = readCall->toolInputJson;
                ProviderTurn u;
                u.toolResultFor = readCall->toolUseId;
                u.toolResultText = result;
                request.turns.push_back(std::move(a));
                request.turns.push_back(std::move(u));
                turnText.clear();
                readCall.reset();
                heldDone.reset();
                otherTool = false;
                continue;   // la boucle refait le corps (la reponse et la suite)
            }
            if (stream.finished()) {
                busy_ = false;
                return;
            }
            if (!r.ok()) return finish(errorEvent(r.error, 200, true));
            const ApiError& e = stream.error();
            if (!autoRetry(e) || stream.emitted() || attempt >= opt_.maxRetries) return finish(errorEvent(e.text, e.status, e.retryable));
            retry = e;
        } else if (status == 0) {
            const bool canRetry = r.failure != HttpResponse::Failure::Unavailable && r.failure != HttpResponse::Failure::BadUrl;
            return finish(errorEvent(r.error, 0, canRetry));
        } else {
            const ApiError e = openAIError(status, errBody, request.model);
            // Un modele sans appel d'outil : aussitot sans outil, la proposition en texte (Outil : Auto).
            if (withTools && toolsRefused(e)) {
                if (!request.textFallback)
                    return finish(errorEvent(e.text + " Ce mod\xC3\xA8le n'a pas d'appel d'outil : choisis \xC2\xAB Auto \xC2\xBB ou \xC2\xAB JSON dans le texte \xC2\xBB "
                                             "(Param\xC3\xA8tres, Outil), ou un autre mod\xC3\xA8le.", e.status, false));
                withTools = false;
                push(statusEvent("sans-outil", "Ce mod\xC3\xA8le n'a pas d'appel d'outil : la proposition est demand\xC3\xA9" "e en texte (un bloc JSON)."));
                continue;
            }
            if (streamUsage && streamOptionsRefused(e)) {   // un service qui ne connait pas stream_options
                streamUsage = false;
                continue;
            }
            if (!autoRetry(e) || attempt >= opt_.maxRetries) {
                std::string text = e.text;
                if (e.status == 429)
                    if (const std::string ra = r.header("retry-after"); !ra.empty() && std::atof(ra.c_str()) > 0)
                        text += " Le service demande d'attendre " + secondsText(std::atof(ra.c_str())) + ".";
                return finish(errorEvent(text, e.status, e.retryable));
            }
            retry = e;
        }
        double wait = delay;
        if (const std::string ra = r.header("retry-after"); !ra.empty() && std::all_of(ra.begin(), ra.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
            const double asked = std::atof(ra.c_str());
            if (asked > opt_.maxRetryAfter)
                return finish(errorEvent(retry->text + " Le service demande d'attendre " + secondsText(asked) + " : r\xC3\xA9" "essaie plus tard.", retry->status, true));
            wait = std::max(wait, asked);
        }
        ++attempt;
        push(statusEvent("reprise", retry->text + " Nouvelle tentative (" + std::to_string(attempt) + "/" + std::to_string(opt_.maxRetries) + ") dans "
                                        + secondsText(wait) + "\xE2\x80\xA6"));
        if (!waitFor(wait)) return finish(doneEvent("arrete"));
        delay *= opt_.factor;
    }
    finish(errorEvent("Le service refuse la demande (trop de nouvelles tentatives).", 0, true));
}

} // namespace app::ai
