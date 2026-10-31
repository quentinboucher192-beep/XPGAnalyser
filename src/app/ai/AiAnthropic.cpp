// =============================================================================
//  app/ai/AiAnthropic.cpp - voir AiAnthropic.hpp
// =============================================================================
#include "AiAnthropic.hpp"

#include "AiJson.hpp"
#include "AiOpenAI.hpp"
#include "AiUsage.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>

namespace app::ai {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string extOf(std::string_view name) {
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string{} : lower(name.substr(dot + 1));
}

std::string sizeText(std::uintmax_t b) {
    char buf[32];
    if (b < 1024) return std::to_string(b) + " o";
    if (b < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(b) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(b) / (1024.0 * 1024.0));
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string guillemets(std::string_view name) { return "\xC2\xAB " + std::string(name) + " \xC2\xBB"; }

bool blank(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
}

// Le texte est-il de l'UTF-8 valide ?
bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c >> 5) == 0x6) n = 1;
        else if ((c >> 4) == 0xE) n = 2;
        else if ((c >> 3) == 0x1E) n = 3;
        else return false;
        if (i + n >= s.size()) return false;
        for (std::size_t k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
        i += n + 1;
    }
    return true;
}

void putUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Un texte qui n'est pas de l'UTF-8 (un CSV d'Excel francais) : lu comme du Windows-1252.
std::string fromCp1252(std::string_view s) {
    static const unsigned k80[32] = {0x20AC, 0x81,   0x201A, 0x192,  0x201E, 0x2026, 0x2020, 0x2021, 0x2C6,  0x2030, 0x160,
                                     0x2039, 0x152,  0x8D,   0x17D,  0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x2DC,  0x2122, 0x161,  0x203A, 0x153,  0x9D,   0x17E,  0x178};
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        putUtf8(out, c >= 0x80 && c < 0xA0 ? k80[c - 0x80] : c);
    }
    return out;
}

// Coupe a `max` octets sans couper un caractere UTF-8.
std::string cutUtf8(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) >> 6) == 0x2) --n;
    s.resize(n);
    return s;
}

bool readFile(const std::string& path, std::string& out) {
    if (path.empty()) return false;
    return core::readFileAll(fs::path(std::u8string(path.begin(), path.end())), out);
}

// Le type reel d'une image d'apres ses premiers octets (une .jpg qui est un PNG part en PNG).
// Les signatures en octets (pas en chaines : l'essai des glyphes lit chaque chaine des sources).
bool startsWith(std::string_view bytes, std::initializer_list<unsigned> sig) {
    if (bytes.size() < sig.size()) return false;
    std::size_t i = 0;
    for (const unsigned b : sig)
        if (static_cast<unsigned char>(bytes[i++]) != b) return false;
    return true;
}

std::string imageType(std::string_view bytes, const std::string& fallback) {
    if (startsWith(bytes, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A})) return "image/png";
    if (startsWith(bytes, {0xFF, 0xD8, 0xFF})) return "image/jpeg";
    if (bytes.size() >= 4 && bytes.substr(0, 4) == "GIF8") return "image/gif";
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") return "image/webp";
    return fallback;
}

std::string kindLabel(std::string_view ext) {
    if (ext == "xlsx") return "classeur Excel";
    if (ext == "xihm") return "projet IHM .xihm";
    if (ext == "xapi") return "projet API .xapi";
    if (ext == "xpgproj") return "projet";
    return "fichier ." + std::string(ext);
}

json::Value obj(const char* key, std::string value) {
    json::Value o = json::Value::object();
    o.set(key, std::move(value));
    return o;
}

json::Value textBlock(std::string text) {
    json::Value b = json::Value::object();
    b.set("type", "text");
    b.set("text", std::move(text));
    return b;
}

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
    return std::to_string(n) + "\xC2\xA0s";   // insecable : "20 s" ne se coupe pas a la ligne
}

} // namespace

// ---- les adresses ---------------------------------------------------------------------------
bool anthropicUrls(std::string_view endpoint, std::string& messages, std::string& models, std::string* why) {
    Url u;
    const std::string text = endpoint.empty() || blank(endpoint) ? std::string(kAnthropicBase) : std::string(endpoint);
    if (!parseUrl(text, u, why)) return false;
    std::string path = u.path;
    if (const auto q = path.find('?'); q != std::string::npos) path.resize(q);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    const std::string tail = "/v1/messages";
    std::string prefix;
    if (!u.hasPath || path == "/") prefix.clear();
    else if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) prefix = path.substr(0, path.size() - tail.size());
    else prefix = path;
    messages = u.base() + prefix + "/v1/messages";
    models = u.base() + prefix + "/v1/models";
    return true;
}

HttpHeaders anthropicHeaders(const std::string& apiKey) {
    HttpHeaders h;
    h.emplace_back("content-type", "application/json");
    if (!apiKey.empty()) h.emplace_back("x-api-key", apiKey);
    h.emplace_back("anthropic-version", std::string(kAnthropicVersion));
    return h;
}

// ---- les pieces jointes ---------------------------------------------------------------------
std::string mediaTypeOf(std::string_view fileName) {
    const std::string e = extOf(fileName);
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "pdf") return "application/pdf";
    if (e == "txt") return "text/plain";
    if (e == "csv") return "text/csv";
    if (e == "json") return "application/json";
    if (e == "xml") return "application/xml";
    if (e == "xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    return "application/octet-stream";
}

AttachmentPlan planAttachment(const ProviderAttachment& a) {
    AttachmentPlan p;
    const std::string e = extOf(a.name);
    if (e == "png" || e == "jpg" || e == "jpeg") {
        p.as = AttachmentPlan::As::Image;
        p.mediaType = e == "png" ? "image/png" : "image/jpeg";
        if (a.bytes > kMaxImageBytes) {
            p.as = AttachmentPlan::As::TooBig;
            p.note = guillemets(a.name) + " n'est pas envoy\xC3\xA9" "e : " + sizeText(a.bytes) + ", une image fait au plus " + sizeText(kMaxImageBytes) + ".";
        }
        return p;
    }
    if (e == "pdf") {
        p.as = AttachmentPlan::As::Document;
        p.mediaType = "application/pdf";
        if (a.bytes > kMaxDocumentBytes) {
            p.as = AttachmentPlan::As::TooBig;
            p.note = guillemets(a.name) + " n'est pas envoy\xC3\xA9 : " + sizeText(a.bytes) + ", un PDF fait au plus " + sizeText(kMaxDocumentBytes) + ".";
        }
        return p;
    }
    if (e == "txt" || e == "csv" || e == "json" || e == "xml") {
        p.as = AttachmentPlan::As::Text;
        p.mediaType = "text/plain";
        if (a.bytes > kMaxTextBytes)
            p.note = guillemets(a.name) + " est coup\xC3\xA9 : seuls ses " + sizeText(kMaxTextBytes) + " premiers sont envoy\xC3\xA9s (" + sizeText(a.bytes) + " en tout).";
        return p;
    }
    p.as = AttachmentPlan::As::NotRead;
    p.note = guillemets(a.name) + " (" + kindLabel(e) + ") n'est pas lu par l'IA dans cette version : seul son nom est envoy\xC3\xA9.";
    return p;
}

// ---- la requete --------------------------------------------------------------------------------
std::string anthropicBody(const ProviderRequest& req, std::vector<std::string>* notes) {
    std::uintmax_t heavy = 0;   // les octets en base64 deja mis
    const auto note = [&](std::string n) {
        if (notes && !n.empty() && std::find(notes->begin(), notes->end(), n) == notes->end()) notes->push_back(std::move(n));
    };
    const auto attachmentBlock = [&](const ProviderAttachment& a) -> json::Value {
        const AttachmentPlan plan = planAttachment(a);
        const std::string head = "Pi\xC3\xA8" "ce jointe " + guillemets(a.name) + " (" + sizeText(a.bytes) + ")";
        if (plan.as == AttachmentPlan::As::NotRead || plan.as == AttachmentPlan::As::TooBig) {
            note(plan.note);
            return textBlock("[" + head + " : " + (plan.as == AttachmentPlan::As::NotRead ? "ce format n'est pas lu par l'IA dans cette version"
                                                                                           : "trop lourde, elle n'est pas envoy\xC3\xA9" "e")
                             + ". Si son contenu compte, demande-le \xC3\xA0 l'utilisateur.]");
        }
        std::string bytes;
        if (!readFile(a.path, bytes)) {
            note(guillemets(a.name) + " est introuvable dans le dossier de l'Assistant : seul son nom est envoy\xC3\xA9.");
            return textBlock("[" + head + " : le fichier est introuvable sur le poste.]");
        }
        if (plan.as == AttachmentPlan::As::Text) {
            if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
            if (!validUtf8(bytes)) bytes = fromCp1252(bytes);
            const bool cut = bytes.size() > kMaxTextBytes;
            if (cut) {
                bytes = cutUtf8(std::move(bytes), static_cast<std::size_t>(kMaxTextBytes));
                note(plan.note.empty() ? guillemets(a.name) + " est coup\xC3\xA9." : plan.note);
            }
            std::string text = head + " :\n" + bytes;
            if (cut) text += "\n[... coup\xC3\xA9" "e : seuls les " + sizeText(kMaxTextBytes) + " premiers sont envoy\xC3\xA9s.]";
            return textBlock(std::move(text));
        }
        std::string data = base64(bytes);
        if (heavy + data.size() > kMaxRequestBytes) {
            note(guillemets(a.name) + " n'est pas envoy\xC3\xA9" "e : la demande serait trop lourde (" + sizeText(kMaxRequestBytes) + " au plus en tout).");
            return textBlock("[" + head + " : pas envoy\xC3\xA9" "e, la demande serait trop lourde.]");
        }
        heavy += data.size();
        json::Value src = json::Value::object();
        src.set("type", "base64");
        src.set("media_type", plan.as == AttachmentPlan::As::Image ? imageType(bytes, plan.mediaType) : plan.mediaType);
        src.set("data", std::move(data));
        json::Value b = json::Value::object();
        b.set("type", plan.as == AttachmentPlan::As::Image ? "image" : "document");
        b.set("source", std::move(src));
        if (plan.as == AttachmentPlan::As::Document) b.set("title", a.name);
        return b;
    };

    // Les messages : les tours, dans l'ordre, rendus valides pour l'API (le premier et le dernier
    // de l'utilisateur ; un appel d'outil toujours suivi de son resultat, en tete ; deux tours de
    // suite du meme role joints ; aucun bloc de texte vide).
    struct Msg {
        std::string role;
        json::Value content = json::Value::array();
        std::string toolUse;   // assistant : l'id de son appel (vide : aucun)
    };
    std::vector<Msg> msgs;
    for (const auto& t : req.turns) {
        Msg m;
        if (t.role == ProviderTurn::Role::User) {
            m.role = "user";
            const std::string pendingTool = !msgs.empty() && msgs.back().role == "assistant" ? msgs.back().toolUse : std::string{};
            if (!pendingTool.empty()) {
                json::Value r = json::Value::object();
                r.set("type", "tool_result");
                r.set("tool_use_id", pendingTool);
                const bool matches = t.toolResultFor == pendingTool;
                const std::string text = matches && !blank(t.toolResultText) ? t.toolResultText : std::string("sans suite connue");
                r.set("content", text);
                if (matches && t.toolResultError) r.set("is_error", true);
                m.content.push(std::move(r));
            }
            for (const auto& a : t.attachments) m.content.push(attachmentBlock(a));
            if (!blank(t.text)) m.content.push(textBlock(t.text));
            if (m.content.items().empty()) m.content.push(textBlock("(message sans texte)"));
        } else {
            m.role = "assistant";
            if (!blank(t.text)) m.content.push(textBlock(t.text));
            if (!t.toolUseId.empty() && !t.toolName.empty()) {
                json::Value input;
                if (!json::parse(t.toolInputJson, input) || !input.isObject()) input = json::Value::object();
                json::Value u = json::Value::object();
                u.set("type", "tool_use");
                u.set("id", t.toolUseId);
                u.set("name", t.toolName);
                u.set("input", std::move(input));
                m.content.push(std::move(u));
                m.toolUse = t.toolUseId;
            }
            if (m.content.items().empty()) continue;   // un tour vide (arrete, en erreur) : rien
            if (msgs.empty()) continue;                // l'API commence par l'utilisateur
        }
        if (!msgs.empty() && msgs.back().role == m.role) {
            for (auto& b : m.content.items()) msgs.back().content.push(std::move(b));
            if (!m.toolUse.empty()) msgs.back().toolUse = m.toolUse;
        } else {
            msgs.push_back(std::move(m));
        }
    }
    while (!msgs.empty() && msgs.back().role == "assistant") msgs.pop_back();   // pas de "prefill"

    json::Value body = json::Value::object();
    body.set("model", req.model);
    body.set("max_tokens", req.maxTokens > 0 ? req.maxTokens : 16000);
    body.set("stream", true);
    if (!req.systemStable.empty()) {
        // Deux blocs : ce qui change peu (les consignes, le registre, les connaissances), mis en cache
        // chez le fournisseur ; puis ce qui suit la demande (les genres utiles, le contexte de la vue).
        json::Value sys = json::Value::array();
        json::Value stable = textBlock(req.systemStable);
        stable.set("cache_control", obj("type", "ephemeral"));
        sys.push(std::move(stable));
        if (!blank(req.systemDynamic)) sys.push(textBlock(req.systemDynamic));
        body.set("system", std::move(sys));
    } else if (!req.system.empty()) {
        body.set("system", req.system);
    }
    if (!req.toolsJson.empty()) {
        json::Value tools;
        if (json::parse(req.toolsJson, tools) && tools.isArray() && !tools.items().empty()) body.set("tools", std::move(tools));
    }
    json::Value list = json::Value::array();
    for (auto& m : msgs) {
        json::Value o = json::Value::object();
        o.set("role", m.role);
        o.set("content", std::move(m.content));
        list.push(std::move(o));
    }
    body.set("messages", std::move(list));
    if (req.effort == "low" || req.effort == "medium" || req.effort == "high") {
        json::Value oc = json::Value::object();
        oc.set("effort", req.effort);
        body.set("output_config", std::move(oc));
    }
    return json::write(body, false);
}

// ---- les erreurs ------------------------------------------------------------------------------
ApiError apiError(int status, std::string_view body, std::string_view model) {
    ApiError e;
    json::Value v;
    const bool isJson = json::parse(body, v) && v.isObject();
    if (isJson) {
        const auto& err = v.at("error");
        e.type = err.isObject() ? err.str("type") : v.str("type");
        e.message = err.isObject() ? err.str("message") : std::string{};
    }
    if (e.message.size() > 300) e.message = e.message.substr(0, 300) + "...";
    if (status == 0) {   // une erreur du flux : son code d'apres son type
        static const std::pair<const char*, int> k[] = {{"invalid_request_error", 400}, {"authentication_error", 401}, {"permission_error", 403},
                                                        {"not_found_error", 404},       {"request_too_large", 413},     {"rate_limit_error", 429},
                                                        {"api_error", 500},             {"overloaded_error", 529}};
        status = 500;
        for (const auto& [t, s] : k)
            if (e.type == t) status = s;
    }
    e.status = status;
    const std::string said = e.message.empty() ? std::string{} : " : " + e.message;
    switch (status) {
        case 400: e.text = "Demande refus\xC3\xA9" "e par le service (400)" + said; break;
        case 401: e.text = "Cl\xC3\xA9 API refus\xC3\xA9" "e (401) : v\xC3\xA9rifie-la dans les Param\xC3\xA8tres de l'Assistant IA."; break;
        case 403: e.text = "Acc\xC3\xA8s refus\xC3\xA9 (403) : cette cl\xC3\xA9 n'a pas le droit de faire cette demande" + said; break;
        case 404:
            if (!isJson) e.text = "Adresse introuvable (404) : v\xC3\xA9rifie l'adresse (le relais) dans les Param\xC3\xA8tres de l'Assistant IA.";
            else if (lower(e.message).find("model") != std::string::npos || e.message.empty())
                e.text = "Mod\xC3\xA8le introuvable : " + guillemets(model) + " (404). Choisis-en un avec \xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB dans les Param\xC3\xA8tres.";
            else e.text = "Introuvable (404)" + said;
            break;
        case 413: e.text = "La demande est trop lourde (413) : retire des pi\xC3\xA8" "ces jointes, ou commence une nouvelle conversation."; break;
        case 429:
            e.text = "Trop de demandes (429) : le service limite le d\xC3\xA9" "bit de ta cl\xC3\xA9.";
            e.retryable = true;
            break;
        case 500:
            e.text = "Erreur interne du service (500).";
            e.retryable = true;
            break;
        case 529:
            e.text = "Le service est surcharg\xC3\xA9 (529).";
            e.retryable = true;
            break;
        default:
            if (status >= 500) {
                e.text = "Le service ne r\xC3\xA9pond pas correctement (" + std::to_string(status) + ").";
                e.retryable = true;
            } else {
                e.text = "Le service a r\xC3\xA9pondu " + std::to_string(status) + said;
            }
    }
    return e;
}

// ---- le flux ------------------------------------------------------------------------------------
void AnthropicStream::status(const char* s) {
    if (phase_ == s) return;
    phase_ = s;
    out_.push_back(statusEvent(s));
}

void AnthropicStream::feed(std::string_view chunk) {
    if (finished_ || failed_) return;
    std::vector<SseEvent> events;
    sse_.feed(chunk, events);
    for (const auto& e : events) {
        handle(e);
        if (finished_ || failed_) break;
    }
}

void AnthropicStream::end() {
    if (finished_ || failed_) return;
    std::vector<SseEvent> events;
    sse_.finish(events);
    for (const auto& e : events) handle(e);
    if (finished_ || failed_) return;
    failed_ = true;
    error_ = {};
    error_.text = "La connexion a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e avant la fin de la r\xC3\xA9ponse.";
    error_.retryable = true;
}

std::vector<ProviderEvent> AnthropicStream::take() { return std::exchange(out_, {}); }

void AnthropicStream::handle(const SseEvent& e) {
    if (e.event == "ping") return;
    json::Value v;
    if (!json::parse(e.data, v) || !v.isObject()) return;   // une ligne illisible : ignoree
    const std::string type = e.event == "message" || e.event.empty() ? v.str("type") : e.event;
    // 1.16.0 : les jetons de la reponse (message_start : l'entree et le cache ; message_delta : la
    // sortie, cumulee) - un evenement Usage a chaque nouvelle valeur.
    const auto readUsage = [this](const json::Value& u) {
        if (!u.isObject()) return;
        const ProviderUsage before = usage_;
        if (u.has("input_tokens")) usage_.input = u.integer("input_tokens");
        if (u.has("cache_creation_input_tokens")) usage_.cacheWrite = u.integer("cache_creation_input_tokens");
        if (u.has("cache_read_input_tokens")) usage_.cacheRead = u.integer("cache_read_input_tokens");
        if (u.has("output_tokens")) usage_.output = u.integer("output_tokens");
        if (usage_.input == before.input && usage_.output == before.output && usage_.cacheRead == before.cacheRead && usage_.cacheWrite == before.cacheWrite)
            return;
        ProviderEvent ue;
        ue.type = ProviderEvent::Type::Usage;
        ue.usage = usage_;
        out_.push_back(std::move(ue));
    };
    if (type == "message_start") {
        status("reflexion");
        readUsage(v.at("message").at("usage"));
    } else if (type == "content_block_start") {
        const int index = static_cast<int>(v.integer("index"));
        const auto& cb = v.at("content_block");
        Block b;
        b.type = cb.str("type");
        if (b.type == "text") {
            status("ecriture");
            if (const std::string t = cb.str("text"); !t.empty()) {
                ProviderEvent te;
                te.type = ProviderEvent::Type::Text;
                te.text = t;
                out_.push_back(std::move(te));
                emitted_ = true;
            }
        } else if (b.type == "tool_use") {
            status("operations");
            b.id = cb.str("id");
            b.name = cb.str("name");
            if (const auto* in = cb.find("input"); in && in->isObject() && !in->members().empty()) b.start = json::write(*in, false);
        } else if (b.type == "thinking" || b.type == "redacted_thinking") {
            status("reflexion");
        }
        blocks_[index] = std::move(b);
    } else if (type == "content_block_delta") {
        const int index = static_cast<int>(v.integer("index"));
        const auto& d = v.at("delta");
        const std::string dt = d.str("type");
        if (dt == "text_delta") {
            status("ecriture");
            ProviderEvent te;
            te.type = ProviderEvent::Type::Text;
            te.text = d.str("text");
            if (!te.text.empty()) {
                out_.push_back(std::move(te));
                emitted_ = true;
            }
        } else if (dt == "input_json_delta") {
            if (auto it = blocks_.find(index); it != blocks_.end()) it->second.json += d.str("partial_json");
        }
        // thinking_delta, signature_delta, citations_delta : ignores (l'etat "reflexion" suffit).
    } else if (type == "content_block_stop") {
        const int index = static_cast<int>(v.integer("index"));
        auto it = blocks_.find(index);
        if (it == blocks_.end()) return;
        it->second.stopped = true;
        if (it->second.type == "tool_use") {
            ProviderEvent te;
            te.type = ProviderEvent::Type::ToolUse;
            te.toolUseId = it->second.id;
            te.toolName = it->second.name;
            // Les morceaux concatenes ; aucun : l'entree donnee au debut du bloc (ou {}).
            te.toolInputJson = !it->second.json.empty() ? it->second.json : !it->second.start.empty() ? it->second.start : std::string("{}");
            tools_.push_back(std::move(te));   // rendu a la fin du message seulement
            emitted_ = true;
        }
    } else if (type == "message_delta") {
        const auto& d = v.at("delta");
        if (const auto* r = d.find("stop_reason"); r && r->isString()) stopReason_ = r->asString();
        readUsage(v.at("usage"));
    } else if (type == "message_stop") {
        stopped_ = true;
        const std::string reason = stopReason_.empty() ? std::string("end_turn") : stopReason_;
        // Un appel d'outil n'est propose que si le message s'est fini normalement (coupe par
        // max_tokens, son entree peut etre tronquee ; un refus : rien n'est execute).
        bool complete = reason == "end_turn" || reason == "tool_use" || reason == "stop_sequence";
        for (const auto& [i, b] : blocks_)
            if (b.type == "tool_use" && !b.stopped) complete = false;
        if (complete)
            for (auto& t : tools_) out_.push_back(std::move(t));
        tools_.clear();
        out_.push_back(doneEvent(reason));
        finished_ = true;
    } else if (type == "error") {
        error_ = apiError(0, e.data, model_);
        failed_ = true;
    }
}

// ---- le fournisseur --------------------------------------------------------------------------------
AnthropicProvider::AnthropicProvider() : AnthropicProvider(Options{}) {}
AnthropicProvider::AnthropicProvider(Options o) : opt_(o) {}

AnthropicProvider::~AnthropicProvider() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

bool AnthropicProvider::start(ProviderRequest request, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (busy_) return fail("Une r\xC3\xA9ponse est d\xC3\xA9j\xC3\xA0 en cours.");
    if (blank(request.model))
        return fail("Choisis un mod\xC3\xA8le dans les Param\xC3\xA8tres de l'Assistant IA (\xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB).");
    if (request.apiKey.empty() && blank(request.endpoint)) return fail("Entre ta cl\xC3\xA9 API dans les Param\xC3\xA8tres de l'Assistant IA.");
    std::string messages, models, w;
    if (!anthropicUrls(request.endpoint, messages, models, &w)) return fail(w);
    if (!httpAvailable(&w)) return fail("Pas de connexion possible sur ce poste : " + w + ".");
    if (thread_.joinable()) thread_.join();   // le precedent est fini (busy_ faux)
    {
        std::lock_guard<std::mutex> lk(m_);
        events_.clear();
        http_ = std::shared_ptr<HttpClient>(makeHttpClient());
    }
    cancelled_ = false;
    busy_ = true;
    thread_ = std::thread([this, r = std::move(request), u = std::move(messages)]() mutable { run(std::move(r), std::move(u)); });
    return true;
}

std::vector<ProviderEvent> AnthropicProvider::poll() {
    std::lock_guard<std::mutex> lk(m_);
    return std::exchange(events_, {});
}

void AnthropicProvider::cancel() {
    cancelled_ = true;
    std::shared_ptr<HttpClient> http;
    {
        std::lock_guard<std::mutex> lk(m_);
        http = http_;
    }
    if (http) http->cancel();
    cv_.notify_all();
}

void AnthropicProvider::push(ProviderEvent e) {
    std::lock_guard<std::mutex> lk(m_);
    events_.push_back(std::move(e));
}

bool AnthropicProvider::waitFor(double seconds) {
    std::unique_lock<std::mutex> lk(m_);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
    cv_.wait_until(lk, until, [this] { return cancelled_.load(); });
    return !cancelled_;
}

void AnthropicProvider::run(ProviderRequest request, std::string url) {
    std::shared_ptr<HttpClient> http;
    {
        std::lock_guard<std::mutex> lk(m_);
        http = http_;
    }
    const auto finish = [this](ProviderEvent last) {
        push(std::move(last));
        busy_ = false;
    };
    // Reessaye seul : 429, 529, 5xx (une coupure ou un delai : Reessayer, a la main).
    const auto autoRetry = [](const ApiError& e) { return e.retryable && (e.status == 429 || e.status >= 500); };
    push(statusEvent("connexion"));
    HttpRequest hr;
    hr.method = "POST";
    hr.url = url;
    hr.headers = anthropicHeaders(request.apiKey);
    hr.body = anthropicBody(request);   // lit les pieces jointes : ici, sur le fil
    hr.connectTimeoutSeconds = opt_.connectTimeout;
    hr.idleTimeoutSeconds = std::clamp(request.timeoutSeconds, 5, 600);
    request.apiKey.clear();
    Url u;
    (void)parseUrl(url, u);
    double delay = opt_.firstDelay;
    for (int attempt = 0;; ++attempt) {
        if (cancelled_) return finish(doneEvent("arrete"));
        if (attempt > 0) push(statusEvent("connexion"));
        AnthropicStream stream;
        stream.setModel(request.model);
        int status = 0;
        std::string errBody;
        HttpSink sink;
        sink.onHeaders = [&](int s, const HttpHeaders& h) {
            status = s;
            // 1.16.0 : la limite du service, dans les en-tetes de chaque reponse (une 429 : l'attente).
            ProviderEvent le;
            le.type = ProviderEvent::Type::Usage;
            le.limits = limitsFromHeaders(h, now());
            if (le.limits.known() || le.limits.retryAfterSeconds >= 0) push(std::move(le));
        };
        sink.onData = [&](std::string_view chunk) {
            if (status == 200) {
                stream.feed(chunk);
                for (auto& e : stream.take()) push(std::move(e));
            } else if (errBody.size() < 64 * 1024) {
                errBody.append(chunk.data(), std::min<std::size_t>(chunk.size(), 64 * 1024 - errBody.size()));
            }
        };
        HttpResponse r = http->perform(hr, sink);
        if (cancelled_ || r.failure == HttpResponse::Failure::Cancelled) return finish(doneEvent("arrete"));
        if (status == 0) status = r.status;
        std::optional<ApiError> retry;
        if (status == 200) {
            // Le flux, lu jusqu'au bout (ou coupe).
            stream.end();
            for (auto& e : stream.take()) push(std::move(e));
            if (stream.finished()) {   // le Done est deja pousse
                busy_ = false;
                return;
            }
            // La connexion a lache pendant le flux : pas de nouvelle tentative automatique (du
            // texte est peut-etre deja montre) ; le panneau propose Reessayer.
            if (!r.ok()) return finish(errorEvent(r.error, 200, true));
            const ApiError& e = stream.error();   // event: error, ou la fin sans message_stop
            if (!autoRetry(e) || stream.emitted() || attempt >= opt_.maxRetries) return finish(errorEvent(e.text, e.status, e.retryable));
            retry = e;
        } else if (status == 0) {
            // Pas de reponse : une panne du reseau, dite telle quelle (Reessayer, sauf sans transport).
            const bool canRetry = r.failure != HttpResponse::Failure::Unavailable && r.failure != HttpResponse::Failure::BadUrl;
            return finish(errorEvent(r.error, 0, canRetry));
        } else {
            const ApiError e = apiError(status, errBody, request.model);
            if (!autoRetry(e) || attempt >= opt_.maxRetries) return finish(errorEvent(e.text, e.status, e.retryable));
            retry = e;
        }
        // Une nouvelle tentative : l'attente que le service demande, sinon croissante.
        double wait = delay;
        if (const std::string ra = r.header("retry-after"); !ra.empty() && std::all_of(ra.begin(), ra.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const double asked = std::atof(ra.c_str());
            if (asked > opt_.maxRetryAfter)
                return finish(errorEvent(retry->text + " Le service demande d'attendre " + secondsText(asked) + " : r\xC3\xA9" "essaie plus tard.", retry->status, true));
            wait = std::max(wait, asked);
        }
        push(statusEvent("reprise", retry->text + " Nouvelle tentative (" + std::to_string(attempt + 1) + "/" + std::to_string(opt_.maxRetries) + ") dans "
                                        + secondsText(wait) + "\xE2\x80\xA6"));
        if (!waitFor(wait)) return finish(doneEvent("arrete"));
        delay *= opt_.factor;
    }
}

// ---- la liste des modeles ------------------------------------------------------------------------------
ModelsResult parseModels(int status, std::string_view body) {
    ModelsResult r;
    r.httpStatus = status;
    if (status != 200) {
        r.error = apiError(status, body, "").text;
        if (status == 404) r.error = "Adresse introuvable (404) : v\xC3\xA9rifie l'adresse (le relais) dans les Param\xC3\xA8tres.";
        return r;
    }
    json::Value v;
    if (!json::parse(body, v) || !v.isObject() || !v.at("data").isArray()) {
        r.error = "R\xC3\xA9ponse illisible : ce n'est pas la liste des mod\xC3\xA8les (l'adresse est-elle la bonne ?).";
        return r;
    }
    for (const auto& m : v.at("data").items()) {
        ModelInfo info;
        info.id = m.str("id");
        info.name = m.str("display_name", info.id);
        info.contextTokens = std::max<long long>(0, m.integer("max_input_tokens"));   // 1.16.0
        if (!info.id.empty()) r.models.push_back(std::move(info));
    }
    r.ok = true;
    return r;
}

ModelQuery::~ModelQuery() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

bool ModelQuery::start(std::string endpoint, std::string apiKey, int timeoutSeconds, std::string* why) {
    if (busy_) {
        if (why) *why = "Une demande est d\xC3\xA9j\xC3\xA0 en cours.";
        return false;
    }
    if (apiKey.empty() && blank(endpoint)) {
        if (why) *why = "Entre d'abord ta cl\xC3\xA9 API.";
        return false;
    }
    std::string messages, models;
    if (!anthropicUrls(endpoint, messages, models, why)) return false;
    return startUrl(models + "?limit=1000", anthropicHeaders(apiKey), timeoutSeconds, false, false, why);
}

bool ModelQuery::startUrl(std::string url, HttpHeaders headers, int timeoutSeconds, bool openai, bool allowPrivateHttp, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (busy_) return fail("Une demande est d\xC3\xA9j\xC3\xA0 en cours.");
    std::string w;
    if (!httpAvailable(&w)) return fail("Pas de connexion possible sur ce poste : " + w + ".");
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lk(m_);
        result_.reset();
        http_ = std::shared_ptr<HttpClient>(makeHttpClient());
    }
    busy_ = true;
    thread_ = std::thread([this, url = std::move(url), hs = std::move(headers), timeoutSeconds, openai, allowPrivateHttp] {
        std::shared_ptr<HttpClient> http;
        {
            std::lock_guard<std::mutex> lk(m_);
            http = http_;
        }
        HttpRequest hr;
        hr.method = "GET";
        hr.url = url;
        hr.headers = hs;
        hr.connectTimeoutSeconds = 20;
        hr.idleTimeoutSeconds = std::clamp(timeoutSeconds, 5, 600);
        hr.allowPrivateHttp = allowPrivateHttp;
        std::string body;
        int status = 0;
        HttpSink sink;
        sink.onHeaders = [&](int s, const HttpHeaders&) { status = s; };
        sink.onData = [&](std::string_view c) {
            if (body.size() < 4 * 1024 * 1024) body.append(c.data(), c.size());
        };
        const HttpResponse r = http->perform(hr, sink);
        ModelsResult res;
        if (r.failure == HttpResponse::Failure::Cancelled) {
            res.error = "Arr\xC3\xAAt\xC3\xA9" "e.";
        } else if (!r.ok()) {
            res.error = r.error;
            res.httpStatus = status ? status : r.status;
        } else {
            res = openai ? parseOpenAIModels(status ? status : r.status, body) : parseModels(status ? status : r.status, body);
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            result_ = std::move(res);
        }
        busy_ = false;
    });
    return true;
}

std::optional<ModelsResult> ModelQuery::poll() {
    std::lock_guard<std::mutex> lk(m_);
    return std::exchange(result_, std::nullopt);
}

void ModelQuery::cancel() {
    std::shared_ptr<HttpClient> http;
    {
        std::lock_guard<std::mutex> lk(m_);
        http = http_;
    }
    if (http) http->cancel();
}

} // namespace app::ai
