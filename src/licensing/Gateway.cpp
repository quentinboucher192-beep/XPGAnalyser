// =============================================================================
//  licensing/Gateway.cpp - 1.34.0 : la passerelle GitHub des licences, les
//  formats (voir Gateway.hpp)
// =============================================================================
#include "Gateway.hpp"

#include "../core/Json.hpp"

#include <algorithm>
#include <cstdlib>

#ifndef XPG_PASSERELLE_DEPOT
#  define XPG_PASSERELLE_DEPOT ""
#endif
#ifndef XPG_PASSERELLE_BRANCHE
#  define XPG_PASSERELLE_BRANCHE "main"
#endif

namespace licensing::gateway {

namespace json = core::json;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

bool nameChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

bool isHex(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Le base64 en lignes de 76 (un corps d'issue lisible).
std::string wrap76(const std::string& b64) {
    std::string out;
    for (std::size_t i = 0; i < b64.size(); i += 76) {
        out.append(b64, i, 76);
        out += '\n';
    }
    return out;
}

// Un objet JSON strict : pas d'autre cle que `known`, "version" = 1.
bool readObject(std::string_view text, json::Value& v, std::initializer_list<std::string_view> known, std::string_view what, std::string* why) {
    std::string err;
    if (!json::parse(text, v, &err) || !v.isObject()) return fail(why, std::string(what) + " n'est pas un JSON lisible" + (err.empty() ? "" : " (" + err + ")"));
    const json::Value extra = json::unknownMembers(v, known);
    if (!extra.members().empty()) return fail(why, std::string(what) + " porte un champ inconnu (" + extra.members().front().first + ")");
    const json::Value* ver = v.find("version");
    if (!ver || !ver->isNumber() || ver->asInt() != 1) return fail(why, std::string(what) + " n'est pas de la version 1 (un logiciel plus r\xC3\xA9" "cent l'a \xC3\xA9" "crit ?)");
    return true;
}

bool readString(const json::Value& v, std::string_view key, std::string& out, bool required, std::size_t maxLen, std::string_view what,
                std::string* why) {
    const json::Value* f = v.find(key);
    if (!f) {
        if (required) return fail(why, std::string(what) + " : le champ " + std::string(key) + " manque");
        out.clear();
        return true;
    }
    if (!f->isString()) return fail(why, std::string(what) + " : le champ " + std::string(key) + " n'est pas un texte");
    if (f->asString().size() > maxLen) return fail(why, std::string(what) + " : le champ " + std::string(key) + " est trop long");
    out = f->asString();
    return true;
}

bool readCount(const json::Value& v, std::string_view key, std::uint64_t& out, std::string_view what, std::string* why) {
    const json::Value* f = v.find(key);
    if (!f || !f->isNumber() || f->asNumber() < 1 || f->asNumber() > 9007199254740991.0 || f->asNumber() != static_cast<double>(f->asInt()))
        return fail(why, std::string(what) + " : le champ " + std::string(key) + " doit \xC3\xAAtre un entier positif");
    out = static_cast<std::uint64_t>(f->asInt());
    return true;
}

bool readTime(const json::Value& v, std::string_view key, Seconds& out, std::string_view what, std::string* why) {
    std::string t;
    if (!readString(v, key, t, true, 40, what, why)) return false;
    if (!parseIsoTime(t, out)) return fail(why, std::string(what) + " : la date " + std::string(key) + " est illisible");
    return true;
}

std::string maskStream(std::string_view repo, std::size_t n) {
    std::string stream;
    std::uint32_t counter = 0;
    while (stream.size() < n) {
        const auto d = crypto::sha256("xpg-passerelle-masque/1|" + std::string(repo) + "|" + std::to_string(counter++));
        stream.append(reinterpret_cast<const char*>(d.data()), d.size());
    }
    stream.resize(n);
    return stream;
}

} // namespace

// ============================================================ l'adresse ==
bool validRepo(std::string_view repo) noexcept {
    const auto slash = repo.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= repo.size() || repo.size() > 140) return false;
    const auto owner = repo.substr(0, slash), name = repo.substr(slash + 1);
    if (name.find('/') != std::string_view::npos || name == "." || name == "..") return false;
    return std::all_of(owner.begin(), owner.end(), nameChar) && std::all_of(name.begin(), name.end(), nameChar);
}

bool validBranch(std::string_view branch) noexcept {
    if (branch.empty() || branch.size() > 100 || branch.front() == '/' || branch.back() == '/' || branch.find("..") != std::string_view::npos)
        return false;
    return std::all_of(branch.begin(), branch.end(), [](char c) { return nameChar(c) || c == '/'; });
}

Address compiledAddress() {
    Address a;
    a.repo = XPG_PASSERELLE_DEPOT;
    a.branch = XPG_PASSERELLE_BRANCHE;
#ifdef XPG_LICENCES_CLE_ESSAI
    // Seulement dans une construction d'essai : le faux GitHub des essais et des captures.
    if (const char* env = std::getenv("XPG_PASSERELLE"); env && *env) {
        const std::string v = env;
        const auto at = v.find('@');
        a.repo = v.substr(0, at);
        if (at != std::string::npos) {
            const std::string base = v.substr(at + 1);
            a.api = base + "/api";
            a.raw = base + "/raw";
        }
    }
#endif
    if (!validRepo(a.repo) || !validBranch(a.branch)) return {};
    return a;
}

// ============================================================ les chemins ==
std::string statusPath(std::string_view deviceId) { return std::string(kStatusDir) + "/" + std::string(deviceId) + ".xpgpos"; }

std::string rawUrl(const Address& a, std::string_view path) { return a.raw + "/" + a.repo + "/" + a.branch + "/" + std::string(path); }

std::string apiUrl(const Address& a, std::string_view tail) { return a.api + "/repos/" + a.repo + std::string(tail); }

std::string deviceId(const crypto::PublicKey& deviceKey) {
    std::string m = "xpg-poste-passerelle/1";
    m.append(reinterpret_cast<const char*>(deviceKey.data()), deviceKey.size());
    const auto d = crypto::sha256(m);
    return crypto::hex(d.data(), 16);
}

bool isDeviceId(std::string_view s) noexcept { return s.size() == 32 && isHex(s); }

// ============================================================ sceller ==
std::string sealFor(const crypto::PublicKey& key, std::string_view plain) {
    crypto::Bytes box;
    if (!crypto::seal(key, plain, box)) return {};
    return wrap76(crypto::base64(box.data(), box.size()));
}

bool openWith(const SigningKey& key, std::string_view sealedBase64, std::string& plain, std::string* why) {
    crypto::Bytes box;
    if (!crypto::unbase64(sealedBase64, box)) return fail(why, "le contenu scell\xC3\xA9 n'est pas du base64");
    if (!key.openSeal(box, plain)) return fail(why, "le contenu scell\xC3\xA9 ne s'ouvre pas avec " + std::string(key.test() ? "cette cl\xC3\xA9 d'essai" : "cette cl\xC3\xA9") + " (scell\xC3\xA9 pour une autre, ou modifi\xC3\xA9)");
    return true;
}

// ============================================================ la configuration ==
std::string maskToken(std::string_view repo, std::string_view token) {
    std::string x(token);
    const std::string s = maskStream(repo, x.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<char>(x[i] ^ s[i]);
    return crypto::base64(x);
}

bool unmaskToken(std::string_view repo, std::string_view masked, std::string& token) {
    crypto::Bytes b;
    if (!crypto::unbase64(masked, b) || b.empty() || b.size() > 255) return false;
    const std::string s = maskStream(repo, b.size());
    token.assign(b.begin(), b.end());
    for (std::size_t i = 0; i < token.size(); ++i) token[i] = static_cast<char>(token[i] ^ s[i]);
    return std::all_of(token.begin(), token.end(), [](char c) { return c > 32 && c < 127; });
}

std::string writeConfig(const Config& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("numero", c.number);
    v.set("emise", isoTime(c.issuedAt));
    v.set("depot", c.repo);
    v.set("jeton_demandes", maskToken(c.repo, c.requestToken));
    return json::write(v) + "\n";
}

bool parseConfig(std::string_view text, Config& out, std::string* why) {
    constexpr std::string_view what = "la configuration de la passerelle";
    json::Value v;
    if (!readObject(text, v, {"version", "numero", "emise", "depot", "jeton_demandes"}, what, why)) return false;
    Config c;
    std::string masked;
    if (!readCount(v, "numero", c.number, what, why) || !readTime(v, "emise", c.issuedAt, what, why) ||
        !readString(v, "depot", c.repo, true, 140, what, why) || !readString(v, "jeton_demandes", masked, true, 400, what, why))
        return false;
    if (!validRepo(c.repo)) return fail(why, std::string(what) + " : le d\xC3\xA9p\xC3\xB4t est mal \xC3\xA9" "crit");
    if (!unmaskToken(c.repo, masked, c.requestToken)) return fail(why, std::string(what) + " : le jeton des demandes est illisible");
    out = std::move(c);
    return true;
}

// ============================================================ l'etat d'un PC ==
const char* stateKey(State s) noexcept {
    switch (s) {
        case State::Received: return "recue";
        case State::Accepted: return "acceptee";
        case State::Refused: return "refusee";
        case State::Cancelled: return "annulee";
        case State::Updated: return "mise_a_jour";
        case State::Revoked: return "revoquee";
        case State::Removed: return "retiree";
    }
    return "?";
}

bool parseStateKey(std::string_view key, State& out) noexcept {
    for (State s : {State::Received, State::Accepted, State::Refused, State::Cancelled, State::Updated, State::Revoked, State::Removed})
        if (key == stateKey(s)) {
            out = s;
            return true;
        }
    return false;
}

std::string stateLabel(State s) {
    switch (s) {
        case State::Received: return "Re\xC3\xA7ue par l'\xC3\xA9" "diteur";
        case State::Accepted: return "Accept\xC3\xA9" "e";
        case State::Refused: return "Refus\xC3\xA9" "e";
        case State::Cancelled: return "Annul\xC3\xA9" "e";
        case State::Updated: return "Licence mise \xC3\xA0 jour";
        case State::Revoked: return "Licence r\xC3\xA9voqu\xC3\xA9" "e";
        case State::Removed: return "PC retir\xC3\xA9 de la licence";
    }
    return "?";
}

std::string writeDeviceStatus(const DeviceStatus& s) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("poste", s.device);
    v.set("numero", s.number);
    v.set("emis", isoTime(s.issuedAt));
    v.set("etat", stateKey(s.state));
    if (!s.requestNonce.empty()) v.set("demande", s.requestNonce);
    if (!s.licenseId.empty()) v.set("licence", s.licenseId);
    if (!s.sealed.empty()) v.set("scelle", s.sealed);
    return json::write(v) + "\n";
}

bool parseDeviceStatus(std::string_view text, DeviceStatus& out, std::string* why) {
    constexpr std::string_view what = "l'\xC3\xA9tat du PC";
    json::Value v;
    if (!readObject(text, v, {"version", "poste", "numero", "emis", "etat", "demande", "licence", "scelle"}, what, why)) return false;
    DeviceStatus s;
    std::string state;
    if (!readString(v, "poste", s.device, true, 32, what, why) || !readCount(v, "numero", s.number, what, why) ||
        !readTime(v, "emis", s.issuedAt, what, why) || !readString(v, "etat", state, true, 20, what, why) ||
        !readString(v, "demande", s.requestNonce, false, 128, what, why) || !readString(v, "licence", s.licenseId, false, 64, what, why) ||
        !readString(v, "scelle", s.sealed, false, 700u * 1024u, what, why))
        return false;
    if (!isDeviceId(s.device)) return fail(why, std::string(what) + " : l'identifiant du poste est mal \xC3\xA9" "crit");
    if (!parseStateKey(state, s.state)) return fail(why, std::string(what) + " : l'\xC3\xA9tat \xC2\xAB " + state + " \xC2\xBB est inconnu");
    out = std::move(s);
    return true;
}

std::string writeStatusContent(const StatusContent& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    if (!c.message.empty()) v.set("message", c.message);
    if (!c.license.empty()) v.set("licence", c.license);
    return json::write(v, false);
}

bool parseStatusContent(std::string_view text, StatusContent& out, std::string* why) {
    constexpr std::string_view what = "le contenu scell\xC3\xA9";
    json::Value v;
    if (!readObject(text, v, {"version", "message", "licence"}, what, why)) return false;
    StatusContent c;
    if (!readString(v, "message", c.message, false, 2000, what, why) || !readString(v, "licence", c.license, false, 300u * 1024u, what, why))
        return false;
    out = std::move(c);
    return true;
}

// ============================================================ la demande en ligne ==
std::string writeOnlineRequest(const OnlineRequest& r) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("demande", r.request);
    v.set("client", r.customer);
    if (!r.contact.empty()) v.set("contact", r.contact);
    if (!r.message.empty()) v.set("message", r.message);
    return json::write(v, false);
}

bool parseOnlineRequest(std::string_view text, OnlineRequest& out, std::string* why) {
    constexpr std::string_view what = "la demande en ligne";
    json::Value v;
    if (!readObject(text, v, {"version", "demande", "client", "contact", "message"}, what, why)) return false;
    OnlineRequest r;
    if (!readString(v, "demande", r.request, true, 32u * 1024u, what, why) || !readString(v, "client", r.customer, true, 200, what, why) ||
        !readString(v, "contact", r.contact, false, 200, what, why) || !readString(v, "message", r.message, false, 2000, what, why))
        return false;
    out = std::move(r);
    return true;
}

std::string issueTitle(std::string_view deviceId) {
    return "Demande d'activation XPG (poste " + std::string(deviceId.substr(0, 12)) + ")";
}

std::string issueBody(std::string_view deviceId, std::string_view publisherKeyId, std::string_view sealedBase64) {
    std::string b = kIssueMagic;
    b += "\nposte " + std::string(deviceId) + "\ncle " + std::string(publisherKeyId) + "\n";
    b.append(sealedBase64.data(), sealedBase64.size());
    if (b.back() != '\n') b += '\n';
    return b;
}

bool parseIssueBody(std::string_view body, IssueBody& out, std::string* why) {
    // GitHub peut rendre des fins de ligne CRLF : elles ne comptent pas ici (le sceau est authentifie).
    std::string t;
    t.reserve(body.size());
    for (char c : body)
        if (c != '\r') t.push_back(c);
    std::size_t pos = 0;
    const auto line = [&](std::string& l) {
        const auto eol = t.find('\n', pos);
        if (eol == std::string::npos) return false;
        l = t.substr(pos, eol - pos);
        pos = eol + 1;
        return true;
    };
    std::string magic, poste, cle;
    if (!line(magic) || magic != kIssueMagic) return fail(why, "ce n'est pas une demande en ligne XPG");
    if (!line(poste) || poste.rfind("poste ", 0) != 0 || !isDeviceId(poste.substr(6))) return fail(why, "la demande en ligne : la ligne du poste est illisible");
    if (!line(cle) || cle.rfind("cle ", 0) != 0 || cle.size() != 24) return fail(why, "la demande en ligne : la ligne de la cl\xC3\xA9 est illisible");
    IssueBody b;
    b.device = poste.substr(6);
    b.keyId = cle.substr(4);
    b.sealed = t.substr(pos);
    crypto::Bytes probe;
    if (b.sealed.empty() || b.sealed.size() > 96u * 1024u || !crypto::unbase64(b.sealed, probe)) return fail(why, "la demande en ligne : le contenu scell\xC3\xA9 est illisible");
    out = std::move(b);
    return true;
}

// ============================================================ l'annulation ==
std::string writeCancel(const Cancel& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("poste", c.device);
    v.set("demande", c.requestNonce);
    v.set("le", isoTime(c.at));
    return json::write(v, false);
}

bool parseCancel(std::string_view text, Cancel& out, std::string* why) {
    constexpr std::string_view what = "l'annulation";
    json::Value v;
    if (!readObject(text, v, {"version", "poste", "demande", "le"}, what, why)) return false;
    Cancel c;
    if (!readString(v, "poste", c.device, true, 32, what, why) || !readString(v, "demande", c.requestNonce, true, 128, what, why) ||
        !readTime(v, "le", c.at, what, why))
        return false;
    if (!isDeviceId(c.device)) return fail(why, std::string(what) + " : l'identifiant du poste est mal \xC3\xA9" "crit");
    out = std::move(c);
    return true;
}

std::string cancelComment(const SignedBlock& signedCancel) { return std::string(kCancelMagic) + "\n" + writeBlock(signedCancel); }

bool parseCancelComment(std::string_view body, SignedBlock& out, std::string* why) {
    std::string t;
    for (char c : body)
        if (c != '\r') t.push_back(c);
    const std::string head = std::string(kCancelMagic) + "\n";
    if (t.rfind(head, 0) != 0) return fail(why, "ce n'est pas une annulation XPG");
    std::vector<SignedBlock> blocks;
    if (!readBlocks(std::string_view(t).substr(head.size()), blocks, why)) return false;
    if (blocks.size() != 1 || blocks[0].kind != DocKind::Cancel) return fail(why, "l'annulation n'a pas un seul bloc \xC2\xAB annulation \xC2\xBB");
    out = std::move(blocks[0]);
    return true;
}

} // namespace licensing::gateway
