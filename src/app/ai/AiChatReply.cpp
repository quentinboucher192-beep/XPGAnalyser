// =============================================================================
//  app/ai/AiChatReply.cpp - 1.50.1 : voir AiChatReply.hpp
// =============================================================================
#include "AiChatReply.hpp"

#include "../hmi/SessionText.hpp"
#include "../../session/SessionProtocol.hpp"

#include <string_view>

namespace app::ai::chatreply {

namespace {

constexpr const char* kKey = "reponseA";
constexpr std::size_t kModelLines = 12;    // le modele recoit au plus 12 lignes du message cite...
constexpr std::size_t kModelBytes = 800;   // ... et 800 octets

bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && blank(s.front())) s.remove_prefix(1);
    while (!s.empty() && blank(s.back())) s.remove_suffix(1);
    return s;
}

// Une ligne sans sa mise en forme : le titre (#), la puce, la citation (>), **, `, *.
std::string plainLine(std::string_view l) {
    l = trimmed(l);
    while (!l.empty() && l.front() == '#') l.remove_prefix(1);
    l = trimmed(l);
    if (l.size() >= 2 && (l[0] == '-' || l[0] == '*' || l[0] == '>') && l[1] == ' ') l.remove_prefix(2);
    std::string out;
    for (const char c : l)
        if (c != '*' && c != '`') out += c;
    return std::string(trimmed(out));
}

// Couper sur une frontiere de caractere UTF-8.
std::size_t utf8Cut(std::string_view s, std::size_t n) {
    if (n >= s.size()) return s.size();
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return n;
}

} // namespace

std::string authorOf(const Message& m) {
    if (m.role == "utilisateur" && m.extra.isObject() && m.extra.has("logiciel")) return "Logiciel";
    return m.role == "utilisateur" ? "Moi" : "Assistant";
}

std::string excerptOf(const std::string& md) {
    // La premiere ligne qui a du texte (les balises ``` sautees), sans sa mise en forme ; la suite reste (<< ... >>).
    std::size_t from = 0;
    while (from <= md.size()) {
        std::size_t nl = md.find('\n', from);
        if (nl == std::string::npos) nl = md.size();
        const std::string_view raw = std::string_view(md).substr(from, nl - from);
        const std::string_view t = trimmed(raw);
        if (!t.empty() && t.rfind("```", 0) != 0) {
            std::string line = plainLine(t);
            if (!line.empty()) {
                std::string rest = nl < md.size() ? md.substr(nl) : std::string{};
                // la suite (sans ses balises de code : une balise fermante seule n'est pas << de la suite >>)
                std::string tail;
                for (const char c : rest)
                    if (c != '`') tail += c;
                return session::chatExcerpt(line + tail);
            }
        }
        if (nl >= md.size()) break;
        from = nl + 1;
    }
    return {};
}

Quote quoteOf(const Message& m) { return {m.id, authorOf(m), excerptOf(m.text)}; }

std::optional<Quote> readQuote(const Message& m) {
    if (!m.extra.isObject()) return std::nullopt;
    const auto* q = m.extra.find(kKey);
    if (!q || !q->isObject()) return std::nullopt;
    Quote out{q->str("id"), q->str("auteur"), q->str("extrait")};
    if (out.empty()) return std::nullopt;
    return out;
}

void writeQuote(Message& m, const Quote& q) {
    if (!m.extra.isObject()) m.extra = json::Value::object();
    auto o = json::Value::object();
    o.set("id", q.id);
    o.set("auteur", q.who);
    o.set("extrait", q.excerpt);
    m.extra.set(kKey, std::move(o));
}

std::string timeOf(const Message& m) {
    std::string when = shortDate(m.date, false);
    if (when.find(':') == std::string::npos) {   // un autre jour : << 05/11 >> - et l'heure
        const std::string hm = shortDate(m.date, true);
        if (hm != when) when += " " + hm;
    }
    return when;
}

std::string forModel(const Conversation& c, const Message& m) {
    const auto q = readQuote(m);
    if (!q) return m.text;
    const Message* target = nullptr;
    for (const auto& x : c.messages)
        if (x.id == q->id && &x != &m) target = &x;
    std::string head;
    if (!target) head = "[Citation \xE2\x80\x94 l'utilisateur r\xC3\xA9pond \xC3\xA0 ce message pr\xC3\xA9" "c\xC3\xA9" "dent (" + (q->who.empty() ? std::string("?") : q->who) + ") :]";
    else if (target->role != "utilisateur") head = "[Citation \xE2\x80\x94 l'utilisateur r\xC3\xA9pond \xC3\xA0 ta r\xC3\xA9ponse de " + timeOf(*target) + " :]";
    else if (authorOf(*target) == "Logiciel") head = "[Citation \xE2\x80\x94 l'utilisateur r\xC3\xA9pond \xC3\xA0 ce message du logiciel (" + timeOf(*target) + ") :]";
    else head = "[Citation \xE2\x80\x94 l'utilisateur reprend sa demande de " + timeOf(*target) + " :]";
    std::string body;
    if (target) {
        // Les premieres lignes du message cite, telles qu'il les a ecrites (kModelLines, kModelBytes au plus).
        const std::string& t = target->text;
        std::size_t from = 0, lines = 0;
        bool cut = false;
        while (from < t.size()) {
            if (lines == kModelLines || body.size() >= kModelBytes) {
                cut = true;
                break;
            }
            std::size_t nl = t.find('\n', from);
            if (nl == std::string::npos) nl = t.size();
            std::string_view line = std::string_view(t).substr(from, nl - from);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (body.size() + line.size() > kModelBytes) {
                line = line.substr(0, utf8Cut(line, kModelBytes - body.size()));
                cut = true;
            }
            body += "> " + std::string(line) + "\n";
            ++lines;
            from = nl + 1;
            if (cut) break;
        }
        if (cut) body += "> \xE2\x80\xA6\n";
    }
    if (body.empty()) body = "> " + q->excerpt + "\n";
    return head + "\n" + body + "[Fin de la citation]\n\n" + m.text;
}

std::string copyText(const Message& m, bool withAuthor) {
    sessionui::Row r;
    r.time = timeOf(m);
    r.who = authorOf(m);
    r.text = m.text;
    return sessionui::copyText(r, withAuthor);
}

} // namespace app::ai::chatreply
