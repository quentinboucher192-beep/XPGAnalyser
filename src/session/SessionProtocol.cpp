// =============================================================================
//  session/SessionProtocol.cpp - 1.43.0 : voir SessionProtocol.hpp
// =============================================================================
#include "SessionProtocol.hpp"

#include "../deploy/Crypto.hpp"
#include "../deploy/Net.hpp"
#include "../hmi/HmiStore.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>

namespace session {

namespace {

bool header(const core::json::Value& v, std::string_view op, std::string& nonce) {
    if (!v.isObject() || v.integer("xpgs") != 1 || v.str("op") != op) return false;
    const auto& n = v.at("nonce");
    if (!n.isString() || n.asString().empty() || n.asString().size() > 64) return false;
    nonce = n.asString();
    return true;
}

// Un texte recu, borne et sans caractere de commande (il s'affiche tel quel).
std::string clean(const std::string& s, std::size_t max) {
    std::string out;
    out.reserve(std::min(s.size(), max));
    for (const char c : s) {
        if (out.size() >= max) break;
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) continue;
        out.push_back(c);
    }
    // Ne pas couper un caractere UTF-8 en deux.
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
        std::size_t i = out.size();
        while (i > 0 && (static_cast<unsigned char>(out[i - 1]) & 0xC0) == 0x80) --i;
        if (i == 0) { out.clear(); break; }
        const auto lead = static_cast<unsigned char>(out[i - 1]);
        const std::size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (out.size() - (i - 1) >= need) break;
        out.resize(i - 1);
    }
    return out;
}

bool isHex(std::string_view s, std::size_t size) {
    if (s.size() != size) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

} // namespace

const char* roleKey(Role r) noexcept { return r == Role::Watch ? "regarder" : "modifier"; }
Role roleOf(std::string_view key) noexcept { return key == "regarder" ? Role::Watch : Role::Edit; }
std::string roleText(Role r) { return r == Role::Watch ? "regarde" : "modifie"; }

// ---- la decouverte ----
std::string searchMessage(const std::string& nonce) {
    core::json::Value v = core::json::Value::object();
    v.set("xpgs", 1);
    v.set("op", "CHERCHE");
    v.set("nonce", nonce);
    return core::json::write(v, false);
}

bool parseSearch(std::string_view text, std::string& nonce) {
    if (text.size() > 512) return false;
    core::json::Value v;
    return core::json::parse(text, v, nullptr) && header(v, "CHERCHE", nonce);
}

std::string hereMessage(const std::string& nonce, const Announce& a) {
    core::json::Value v = core::json::Value::object();
    v.set("xpgs", 1);
    v.set("op", "ICI");
    v.set("nonce", nonce);
    v.set("session", a.session);
    v.set("hote", a.host);
    v.set("pc", a.machine);
    v.set("id_pc", a.machineId);
    v.set("logiciel", a.app);
    v.set("version", a.version);
    v.set("projet", a.project);
    v.set("fp", a.fingerprint);
    v.set("sid", a.sessionId);
    v.set("port", a.port);
    v.set("membres", a.members);
    v.set("max", a.maxMembers);
    v.set("code", a.needsCode);
    v.set("proto", a.protocol);
    return core::json::write(v, false);
}

bool parseHere(std::string_view text, std::string& nonce, Announce& a) {
    if (text.size() > 2048) return false;
    core::json::Value v;
    if (!core::json::parse(text, v, nullptr) || !header(v, "ICI", nonce)) return false;
    a.session = clean(v.str("session"), 80);
    a.host = clean(v.str("hote"), 64);
    a.machine = clean(v.str("pc"), 64);
    a.machineId = clean(v.str("id_pc"), 16);
    a.app = clean(v.str("logiciel"), 32);
    a.version = clean(v.str("version"), 16);
    a.project = clean(v.str("projet"), 80);
    a.fingerprint = v.str("fp");
    if (!isHex(a.fingerprint, 64)) return false;   // sans empreinte, rien a epingler
    a.sessionId = v.str("sid");
    if (!isHex(a.sessionId, 16)) return false;
    a.port = static_cast<int>(v.integer("port", kPort));
    if (a.port <= 0 || a.port > 65535) return false;
    a.members = static_cast<int>(std::clamp<std::int64_t>(v.integer("membres", 1), 0, 64));
    a.maxMembers = static_cast<int>(std::clamp<std::int64_t>(v.integer("max", kMaxMembers), 1, 64));
    a.needsCode = v.flag("code");
    a.protocol = static_cast<int>(v.integer("proto", 0));
    return a.protocol >= 1;
}

std::vector<Found> search(const SearchOptions& o, std::string* why) {
    std::vector<Found> out;
    deploy::net::startup();
    deploy::net::UdpSocket sock;
    std::string err;
    if (!sock.bind("0.0.0.0", 0, &err)) {
        if (why) *why = "recherche impossible : " + err;
        return out;
    }
    (void)sock.enableBroadcast();
    unsigned char raw[8];
    deploy::crypto::randomBytes(raw, sizeof raw);
    const std::string nonce = deploy::crypto::hex(raw, sizeof raw);
    const std::string msg = searchMessage(nonce);
    std::vector<std::string> targets = o.direct;
    if (o.broadcast) {
        targets.emplace_back("255.255.255.255");
        for (const auto& b : deploy::net::broadcastAddresses()) targets.push_back(b);
    }
    const auto sendAll = [&]() {
        int sent = 0;
        for (const auto& t : targets)
            if (sock.sendTo(t, o.port, msg, &err)) ++sent;
        return sent;
    };
    if (sendAll() == 0) {
        if (why) *why = targets.empty() ? "aucune adresse \xC3\xA0 interroger" : "recherche impossible : " + err;
        return out;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(std::max(50, o.waitMs));
    bool resent = false;
    std::set<std::string> seen;
    for (;;) {
        const auto now = Clock::now();
        if (now >= deadline) break;
        if (!resent && now - start >= (deadline - start) / 3) {
            resent = true;
            (void)sendAll();
        }
        const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        deploy::net::Datagram d;
        const int rc = sock.receive(d, std::min(left, 100));
        if (rc < 0) break;
        if (rc == 0) continue;
        std::string n;
        Announce a;
        if (!parseHere(d.data, n, a) || n != nonce) continue;
        if (!seen.insert(a.sessionId).second) continue;
        out.push_back({d.from, std::move(a)});
    }
    return out;
}

// ---- le code ----
std::string newCode() { return deploy::crypto::randomDigits(kCodeDigits); }

std::string codeShown(std::string_view code) {
    if (code.size() != 6) return std::string(code);
    return std::string(code.substr(0, 3)) + " " + std::string(code.substr(3));
}

std::string codeTyped(std::string_view text) {
    std::string out;
    for (const char c : text)
        if (c >= '0' && c <= '9') out.push_back(c);
    return out;
}

// ---- les membres ----
core::json::Value toJson(const Member& m) {
    core::json::Value v = core::json::Value::object();
    v.set("n", m.number);
    v.set("nom", m.name);
    v.set("pc", m.machine);
    v.set("id_pc", m.machineId);
    v.set("logiciel", m.app);
    v.set("version", m.version);
    v.set("couleur", static_cast<std::int64_t>(m.color));
    v.set("role", roleKey(m.role));
    v.set("vue", static_cast<std::int64_t>(m.view));
    core::json::Value sel = core::json::Value::array();
    for (const auto id : m.selection) sel.push(static_cast<std::int64_t>(id));
    v.set("choisis", std::move(sel));
    return v;
}

bool fromJson(const core::json::Value& v, Member& m) {
    if (!v.isObject()) return false;
    const auto n = v.integer("n", -1);
    if (n < 0 || n >= 16) return false;
    m.number = static_cast<int>(n);
    m.name = clean(v.str("nom"), 64);
    m.machine = clean(v.str("pc"), 64);
    m.machineId = clean(v.str("id_pc"), 16);
    m.app = clean(v.str("logiciel"), 32);
    m.version = clean(v.str("version"), 16);
    m.color = static_cast<std::uint32_t>(std::clamp<std::int64_t>(v.integer("couleur"), 0, 0xFFFFFF));
    m.role = roleOf(v.str("role"));
    const auto view = v.integer("vue");
    m.view = view > 0 && view <= 0xFFFFFFFFll ? static_cast<hmi::Id>(view) : hmi::kNoId;
    m.selection.clear();
    for (const auto& s : v.at("choisis").items()) {
        if (m.selection.size() >= 4096) break;
        const auto id = s.asInt();
        if (id > 0 && id <= 0xFFFFFFFFll) m.selection.push_back(static_cast<hmi::Id>(id));
    }
    return true;
}

// ---- 1.50.0 : les messages de la discussion ----
std::string chatExcerpt(std::string_view text, std::size_t maxChars) {
    // Les blancs du debut sautes ; la premiere ligne qui a du texte.
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) ++start;
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string out;
    std::size_t chars = 0, i = start;
    for (; i < end && chars < maxChars;) {
        const auto u = static_cast<unsigned char>(text[i]);
        std::size_t len = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : u >= 0xC0 ? 2 : 1;
        if (i + len > end) len = end - i;
        if (u < 0x20 || u == 0x7F) out.push_back(' ');
        else out.append(text.substr(i, len));
        i += len;
        ++chars;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    bool more = i < end;
    for (std::size_t j = end; !more && j < text.size(); ++j) more = text[j] != ' ' && text[j] != '\n' && text[j] != '\r' && text[j] != '\t';
    if (more && !out.empty()) out += "\xE2\x80\xA6";
    return out;
}

std::string chatIdOf(std::string_view from, std::string_view time, std::string_view text) {
    // FNV-1a sur 64 bits : un nom stable, pas un secret.
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::string_view s) {
        for (const char c : s) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        h ^= 0x1F;
        h *= 1099511628211ull;
    };
    mix(from);
    mix(time);
    mix(text);
    static const char* hex = "0123456789abcdef";
    std::string out = "h";
    for (int k = 15; k >= 0; --k) out.push_back(hex[(h >> (k * 4)) & 0xF]);
    return out;
}

void writeReply(core::json::Value& out, const ChatReply& r) {
    if (r.empty()) return;
    core::json::Value v = core::json::Value::object();
    v.set("id", r.id);
    v.set("de", r.from);
    v.set("extrait", r.excerpt);
    out.set("reponse", std::move(v));
}

ChatReply readReply(const core::json::Value& message) {
    ChatReply r;
    const auto& v = message.at("reponse");
    if (!v.isObject()) return r;
    r.id = clean(v.str("id"), 40);
    r.from = clean(v.str("de"), 64);
    r.excerpt = chatExcerpt(clean(v.str("extrait"), 4 * kExcerptChars + 8));
    return r;
}

void writeChat(core::json::Value& out, const ChatMessage& m) {
    out.set("de", m.from);
    out.set("couleur", static_cast<std::int64_t>(m.color));
    out.set("texte", m.text);
    out.set("heure", m.time);
    if (!m.id.empty()) out.set("id", m.id);
    writeReply(out, m.reply);
}

ChatMessage readChat(const core::json::Value& v) {
    ChatMessage m;
    m.from = v.str("de");
    m.text = v.str("texte").substr(0, kMessageMax);
    m.time = v.str("heure");
    m.color = static_cast<std::uint32_t>(std::clamp<std::int64_t>(v.integer("couleur"), 0, 0xFFFFFF));
    m.id = clean(v.str("id"), 40);
    if (m.id.empty()) m.id = chatIdOf(m.from, m.time, m.text);
    m.reply = readReply(v);
    return m;
}

std::string quotedText(const ChatMessage& m) {
    if (m.reply.empty()) return m.text;
    return "En r\xC3\xA9ponse \xC3\xA0 " + (m.reply.from.empty() ? std::string("un message") : m.reply.from) + " : \xC2\xAB " + m.reply.excerpt + " \xC2\xBB\n" + m.text;
}

bool knowsReplies(std::string_view version) {
    int part[3] = {0, 0, 0};
    std::size_t k = 0;
    bool digit = false;
    for (const char c : version) {
        if (c >= '0' && c <= '9') {
            if (k < 3) part[k] = part[k] * 10 + (c - '0');
            digit = true;
            if (part[k < 3 ? k : 2] > 100000) return false;
        } else if (c == '.') {
            if (!digit) return false;
            ++k;
            digit = false;
        } else {
            break;   // "1.50.0-essai" : la suite ne compte pas
        }
    }
    if (k < 1) return false;
    return part[0] > 1 || (part[0] == 1 && part[1] >= 50);
}

std::string initials(std::string_view name) {
    std::string out;
    bool start = true;
    for (std::size_t i = 0; i < name.size() && out.size() < 8;) {
        const auto u = static_cast<unsigned char>(name[i]);
        const std::size_t len = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : u >= 0xC0 ? 2 : 1;
        if (name[i] == ' ' || name[i] == '-' || name[i] == '.') {
            start = true;
        } else if (start) {
            std::string ch(name.substr(i, len));
            if (ch.size() == 1) ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ch[0])));
            out += ch;
            start = false;
            if (out.size() >= 2 && std::count_if(out.begin(), out.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }) >= 2)
                break;
        }
        i += len;
    }
    return out.empty() ? std::string("?") : out;
}

const std::vector<std::uint32_t>& palette() {
    // Teal (le Studio), violet, ambre, bleu, rose, vert, orange, ardoise : distinctes deux a deux.
    static const std::vector<std::uint32_t> p{0x0D9488, 0x7C3AED, 0xD97706, 0x2563EB, 0xDB2777, 0x16A34A, 0xEA580C, 0x475569};
    return p;
}

std::uint32_t freeColor(const std::vector<Member>& members, std::uint32_t wanted) {
    const auto taken = [&](std::uint32_t c) {
        return std::any_of(members.begin(), members.end(), [c](const Member& m) { return m.color == c; });
    };
    const auto& p = palette();
    if (wanted != 0 && std::find(p.begin(), p.end(), wanted) != p.end() && !taken(wanted)) return wanted;
    for (const auto c : p)
        if (!taken(c)) return c;
    return p[members.size() % p.size()];
}

// ---- les identifiants ----
hmi::Id blockStart(int member) noexcept {
    if (member <= 0) return 1;
    return static_cast<hmi::Id>(member) * kIdBlock;
}

hmi::Id highestId(const hmi::Project& p, hmi::Id lo, hmi::Id hi) {
    hmi::Id best = hmi::kNoId;
    const auto scan = [&](std::string_view text) {
        std::size_t pos = 0;
        while ((pos = text.find(" id=", pos)) != std::string_view::npos) {
            pos += 4;
            std::uint64_t v = 0;
            std::size_t k = pos;
            while (k < text.size() && text[k] >= '0' && text[k] <= '9' && v < 0x100000000ull) {
                v = v * 10 + static_cast<std::uint64_t>(text[k] - '0');
                ++k;
            }
            if (k == pos || v >= 0x100000000ull) continue;
            const auto id = static_cast<hmi::Id>(v);
            if (id >= lo && id < hi && id > best) best = id;
        }
    };
    for (const auto& f : hmi::serializeProject(p)) {
        if (!f.data) continue;
        // Les octets d'une ressource (une image) ne sont pas du texte du projet.
        if (f.path.rfind("ressources/", 0) == 0) continue;
        scan(std::string_view(reinterpret_cast<const char*>(f.data->data()), f.data->size()));
    }
    for (const auto& v : p.views) {
        if (v.id >= lo && v.id < hi && v.id > best) best = v.id;
        for (const auto& o : v.objects)
            if (o.id >= lo && o.id < hi && o.id > best) best = o.id;
    }
    return best;
}

hmi::Id firstFreeInBlock(const hmi::Project& p, int member) {
    if (member <= 0) return p.nextId;
    const hmi::Id lo = blockStart(member);
    const hmi::Id hi = member >= 15 ? 0xFFFFFFFFu : blockStart(member + 1);
    const hmi::Id top = highestId(p, lo, hi);
    return top == hmi::kNoId ? lo : top + 1;
}

} // namespace session
