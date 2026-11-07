// =============================================================================
//  license_admin/Passerelle.cpp - 1.34.0 : la passerelle GitHub des licences,
//  cote editeur (voir Passerelle.hpp). JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "Passerelle.hpp"

#include "Admin.hpp"
#include "GitHubSync.hpp"

#include "../app/ai/AiHttp.hpp"
#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Documents.hpp"
#include "../licensing/SignedFile.hpp"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

namespace license_admin::passerelle {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace gw = licensing::gateway;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

namespace {

constexpr const char* kConfigFormat = "xpg-passerelle-editeur/1";
constexpr const char* kTokenFormat = "xpg-jeton-demandes/1";
constexpr const char* kEntryFormat = "xpg-demande-en-ligne/1";
constexpr const char* kFetchFormat = "xpg-releve-demandes/1";
constexpr std::size_t kMaxReply = 32u * 1024u * 1024u;
constexpr int kMaxPages = 50;   // 5000 issues : au-dela, un depot de passerelle n'est plus raisonnable

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}
bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 16u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}
bool writePrivate(const std::string& path, std::string_view text, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, why);
}
std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}
void wipe(std::string& s) {
    if (!s.empty()) sodium_memzero(s.data(), s.size());
    s.clear();
}
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
}

bool keyFromHex(const std::string& hex, crypto::PublicKey& out) {
    crypto::Bytes raw;
    if (!licensing::isKeyHex(hex) || !crypto::unhex(hex, raw, 32) || raw.size() != 32) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}

// ---- un secret garde sur ce poste (le meme format que le jeton GitHub, un autre nom) ----
bool saveSecret(const std::string& path, const crypto::Bytes& plain, std::string* why) {
    crypto::Bytes prot;
    if (!crypto::protect(plain, prot, false, why)) return false;
    std::string text = "# Le jeton des demandes de la passerelle (XPG-License-Admin), protege par DPAPI pour ce compte.\n"
                       "# Ne se copie pas sur un autre poste ; il n'est publie que masque, dans passerelle.xpgpas.\n";
    text += std::string("format=") + kTokenFormat + "\n";
    text += std::string("protection=") + (crypto::protectedHere() ? "dpapi" : "fichier") + "\n";
    text += "secret=" + crypto::hex(prot.data(), prot.size()) + "\n";
    crypto::wipe(prot.data(), prot.size());
    const bool ok = writePrivate(path, text, why);
    wipe(text);
    return ok;
}

bool loadSecret(const std::string& path, crypto::Bytes& plain, std::string* why) {
    std::string text;
    if (!readText(path, text, why)) return false;
    std::map<std::string, std::string> kv;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (kv["format"] != kTokenFormat) return fail(why, "le fichier " + path + " n'est pas du bon format (" + kTokenFormat + ")");
    crypto::Bytes prot;
    if (!crypto::unhex(kv["secret"], prot)) return fail(why, "le fichier " + path + " est ab\xC3\xAEm\xC3\xA9");
    std::string w;
    if (!crypto::unprotect(prot, plain, &w))
        return fail(why, "le fichier " + path + " ne se relit pas ici (" + w + ") : il a \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9 sous un autre compte Windows ou sur un autre poste");
    return true;
}

std::string tokenPath(const std::string& folder) { return deploy::joinPath(folder, kRequestTokenFile); }

// L'adresse qui recoit un jeton : GitHub lui-meme, ou ce poste (le faux GitHub des essais).
bool allowedUrl(const std::string& url, const char* official, const char* what, std::string* why) {
    app::ai::Url u;
    std::string w;
    if (!app::ai::parseUrl(url, u, &w)) return fail(why, std::string(what) + " : " + w);
    if (u.loopback()) return true;
    std::string t = url;
    while (!t.empty() && t.back() == '/') t.pop_back();
    if (t == official) return true;
    return fail(why, std::string(what) + " " + cite(url) + " refus\xC3\xA9" "e : seulement " + official + " (ou ce poste, 127.0.0.1 ou localhost, pour les essais)");
}

} // namespace

// ============================================================================ la configuration
std::string configPath(const std::string& folder) { return deploy::joinPath(folder, kConfigFile); }
bool configured(const std::string& folder) { return isFile(configPath(folder)); }

bool readConfig(const std::string& folder, Config& out, std::string* why) {
    const std::string path = configPath(folder);
    if (!isFile(path)) return fail(why, "la passerelle n'est pas r\xC3\xA9gl\xC3\xA9" "e sur ce poste (XPG-License-Admin passerelle-config --depot PROPRIETAIRE/NOM)");
    std::string text, err;
    Value v;
    if (!readText(path, text, why)) return false;
    if (!core::json::parse(text, v, &err) || !v.isObject() || v.str("format") != kConfigFormat)
        return fail(why, path + " est illisible (" + (err.empty() ? std::string("pas le bon format") : err) + ")");
    Config c;
    c.repo = v.str("depot");
    c.branch = v.str("branche", "main");
    c.api = v.str("api", gw::kDefaultApi);
    c.raw = v.str("raw", gw::kDefaultRaw);
    while (!c.api.empty() && c.api.back() == '/') c.api.pop_back();
    while (!c.raw.empty() && c.raw.back() == '/') c.raw.pop_back();
    if (!gw::validRepo(c.repo) || !gw::validBranch(c.branch)) return fail(why, path + " : d\xC3\xA9p\xC3\xB4t ou branche refus\xC3\xA9s");
    std::string w;
    if (!allowedUrl(c.api, gw::kDefaultApi, "l'adresse de l'API", &w) || !allowedUrl(c.raw, gw::kDefaultRaw, "l'adresse raw", &w))
        return fail(why, path + " : " + w);
    out = std::move(c);
    return true;
}

bool writeConfig(const std::string& folder, const Config& in, std::string* why) {
    Config c = in;
    while (!c.api.empty() && c.api.back() == '/') c.api.pop_back();
    while (!c.raw.empty() && c.raw.back() == '/') c.raw.pop_back();
    if (!gw::validRepo(c.repo))
        return fail(why, "le d\xC3\xA9p\xC3\xB4t de la passerelle s'\xC3\xA9" "crit PROPRIETAIRE/NOM (par exemple ton-compte/passerelle-licences) : " + cite(c.repo) + " est refus\xC3\xA9");
    if (!gw::validBranch(c.branch)) return fail(why, "nom de branche refus\xC3\xA9 : " + cite(c.branch));
    if (!allowedUrl(c.api, gw::kDefaultApi, "l'adresse de l'API", why) || !allowedUrl(c.raw, gw::kDefaultRaw, "l'adresse raw", why)) return false;
    Value v = Value::object();
    v.set("format", std::string(kConfigFormat));
    v.set("depot", c.repo);
    v.set("branche", c.branch);
    v.set("api", c.api);
    v.set("raw", c.raw);
    return writePrivate(configPath(folder), core::json::write(v, true) + "\n", why);
}

gw::Address address(const Config& c) {
    gw::Address a;
    a.repo = c.repo;
    a.branch = c.branch;
    a.api = c.api;
    a.raw = c.raw;
    return a;
}

std::string issueLink(const std::string& repo, int issue) {
    std::string url = "https://github.com/" + repo;
    if (issue > 0) url += "/issues/" + std::to_string(issue);
    return url;
}

// ============================================================================ le jeton des demandes
bool hasRequestToken(const std::string& folder) { return isFile(tokenPath(folder)); }

bool saveRequestToken(const std::string& folder, const std::string& tokenIn, std::string* why) {
    std::string token = trimmed(tokenIn);
    if (!github::plausibleToken(token)) {
        wipe(token);
        return fail(why, "ce n'est pas un jeton GitHub (20 \xC3\xA0 255 caract\xC3\xA8res, sans espace)");
    }
    std::string mine, w;
    const bool same = github::loadToken(folder, mine, &w) && mine == token;
    wipe(mine);
    if (same) {
        wipe(token);
        return fail(why, "c'est TON jeton GitHub (celui de l'\xC3\xA9" "diteur) : il ne doit jamais \xC3\xAAtre publi\xC3\xA9. Cr\xC3\xA9" "e un jeton \xC3\xA0 part, limit\xC3\xA9 aux issues du d\xC3\xA9p\xC3\xB4t de la passerelle");
    }
    crypto::Bytes plain(token.begin(), token.end());
    wipe(token);
    const bool ok = saveSecret(tokenPath(folder), plain, why);
    crypto::wipe(plain.data(), plain.size());
    return ok;
}

bool loadRequestToken(const std::string& folder, std::string& token, std::string* why) {
    if (!hasRequestToken(folder)) return fail(why, "pas de jeton des demandes sur ce poste (XPG-License-Admin passerelle-jeton-demandes)");
    crypto::Bytes plain;
    if (!loadSecret(tokenPath(folder), plain, why)) return false;
    token.assign(plain.begin(), plain.end());
    crypto::wipe(plain.data(), plain.size());
    if (!github::plausibleToken(token)) {
        wipe(token);
        return fail(why, "le jeton des demandes enregistr\xC3\xA9 est ab\xC3\xAEm\xC3\xA9 : enregistre-le de nouveau");
    }
    return true;
}

bool eraseRequestToken(const std::string& folder, std::string* why) {
    if (!hasRequestToken(folder)) return true;
    std::error_code ec;
    if (!fs::remove(P(tokenPath(folder)), ec) || ec) return fail(why, "impossible de supprimer " + tokenPath(folder));
    return true;
}

std::string Result::text() const {
    std::string t = message;
    for (const auto& d : details) t += "\n" + d;
    return t;
}

// ============================================================================ les demandes relevees (local)
const char* requestStateKey(RequestState s) noexcept {
    switch (s) {
        case RequestState::Pending: return "en_attente";
        case RequestState::Received: return "recue";
        case RequestState::Accepted: return "acceptee";
        case RequestState::Refused: return "refusee";
        case RequestState::Cancelled: return "annulee";
        case RequestState::Replaced: return "remplacee";
        case RequestState::Unreadable: return "illisible";
    }
    return "?";
}

bool parseRequestStateKey(std::string_view key, RequestState& out) noexcept {
    for (RequestState s : {RequestState::Pending, RequestState::Received, RequestState::Accepted, RequestState::Refused, RequestState::Cancelled,
                           RequestState::Replaced, RequestState::Unreadable})
        if (key == requestStateKey(s)) {
            out = s;
            return true;
        }
    return false;
}

std::string requestStateLabel(RequestState s) {
    switch (s) {
        case RequestState::Pending: return "Nouvelle";
        case RequestState::Received: return "En attente";
        case RequestState::Accepted: return "Accept\xC3\xA9" "e";
        case RequestState::Refused: return "Refus\xC3\xA9" "e";
        case RequestState::Cancelled: return "Annul\xC3\xA9" "e par le client";
        case RequestState::Replaced: return "Remplac\xC3\xA9" "e";
        case RequestState::Unreadable: return "Illisible";
    }
    return "?";
}

std::string requestsFolder(const std::string& folder) { return deploy::joinPath(folder, kRequestsDir); }

namespace {

std::string entryPath(const std::string& folder, int issue) { return deploy::joinPath(requestsFolder(folder), "issue-" + std::to_string(issue) + ".json"); }

std::string isoOr(Seconds t) { return t ? licensing::isoTime(t) : std::string(); }
Seconds timeOf(const Value& v, std::string_view key) {
    Seconds t = 0;
    const std::string s = v.str(key);
    if (!s.empty() && licensing::parseIsoTime(s, t)) return t;
    return 0;
}

bool writeEntry(const std::string& folder, Entry& e, std::string* why) {
    Value v = Value::object();
    v.set("format", std::string(kEntryFormat));
    v.set("issue", e.issue);
    v.set("ouverte", e.issueOpen);
    v.set("etat", std::string(requestStateKey(e.state)));
    v.set("poste", e.device);
    if (!e.deviceKey.empty()) v.set("cle_pc", e.deviceKey);
    if (!e.nonce.empty()) v.set("demande", e.nonce);
    if (!e.customer.empty()) v.set("client", e.customer);
    if (!e.contact.empty()) v.set("contact", e.contact);
    if (!e.message.empty()) v.set("message", e.message);
    if (!e.deviceName.empty()) v.set("nom_pc", e.deviceName);
    if (!e.fingerprint.empty()) v.set("empreinte", e.fingerprint);
    if (!e.software.empty()) v.set("logiciel", e.software);
    if (!e.licenseId.empty()) v.set("licence_demandee", e.licenseId);
    if (e.requestedAt) v.set("demandee_le", isoOr(e.requestedAt));
    if (e.openedAt) v.set("issue_le", isoOr(e.openedAt));
    if (e.seenAt) v.set("vue_le", isoOr(e.seenAt));
    if (e.answeredAt) v.set("repondue_le", isoOr(e.answeredAt));
    if (!e.answer.empty()) v.set("reponse", e.answer);
    if (!e.requestPath.empty()) v.set("fichier", U(P(e.requestPath).filename()));
    v.set("recue_publiee", e.receivedPublished);
    Value probs = Value::array();
    for (const auto& p : e.problems) probs.push(Value(p));
    v.set("problemes", std::move(probs));
    e.sidecarPath = entryPath(folder, e.issue);
    return writePrivate(e.sidecarPath, core::json::write(v, true) + "\n", why);
}

bool readEntry(const std::string& folder, const std::string& path, Entry& e) {
    std::string text;
    Value v;
    if (!readText(path, text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != kEntryFormat) return false;
    e = Entry{};
    e.issue = static_cast<int>(v.integer("issue"));
    if (e.issue <= 0) return false;
    e.issueOpen = v.flag("ouverte", true);
    if (!parseRequestStateKey(v.str("etat"), e.state)) e.state = RequestState::Unreadable;
    e.device = v.str("poste");
    e.deviceKey = v.str("cle_pc");
    e.nonce = v.str("demande");
    e.customer = v.str("client");
    e.contact = v.str("contact");
    e.message = v.str("message");
    e.deviceName = v.str("nom_pc");
    e.fingerprint = v.str("empreinte");
    e.software = v.str("logiciel");
    e.licenseId = v.str("licence_demandee");
    e.requestedAt = timeOf(v, "demandee_le");
    e.openedAt = timeOf(v, "issue_le");
    e.seenAt = timeOf(v, "vue_le");
    e.answeredAt = timeOf(v, "repondue_le");
    e.answer = v.str("reponse");
    const std::string file = v.str("fichier");
    // Seulement un nom de fichier du dossier des demandes (jamais un chemin ailleurs).
    if (!file.empty() && file.find('/') == std::string::npos && file.find('\\') == std::string::npos && file != "." && file != "..")
        e.requestPath = deploy::joinPath(requestsFolder(folder), file);
    e.receivedPublished = v.flag("recue_publiee");
    if (const Value* p = v.find("problemes"); p && p->isArray())
        for (const auto& x : p->items())
            if (x.isString()) e.problems.push_back(x.asString());
    e.sidecarPath = path;
    return true;
}

} // namespace

std::vector<Entry> listRequests(const std::string& folder) {
    std::vector<Entry> out;
    const fs::path dir = P(requestsFolder(folder));
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (!f.is_regular_file(ec)) continue;
        const std::string name = U(f.path().filename());
        if (name.rfind("issue-", 0) != 0 || f.path().extension() != ".json") continue;
        Entry e;
        if (readEntry(folder, U(f.path()), e)) out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.issue > b.issue; });
    return out;
}

bool findWaiting(const std::string& folder, const std::string& deviceIn, Entry& out, std::string* why) {
    const std::string device = trimmed(deviceIn);
    if (device.empty()) return fail(why, "quel poste ? (son identifiant, tel que passerelle-demandes le montre)");
    const auto all = listRequests(folder);
    std::vector<const Entry*> hits;
    if (device[0] == '#') {
        const int n = std::atoi(device.c_str() + 1);
        for (const auto& e : all)
            if (e.issue == n) hits.push_back(&e);
        if (hits.empty()) return fail(why, "aucune demande relev\xC3\xA9" "e pour l'issue " + device);
        if (!hits[0]->waiting())
            return fail(why, "la demande de l'issue " + device + " n'attend pas de d\xC3\xA9" "cision (" + requestStateLabel(hits[0]->state) + ")");
        out = *hits[0];
        return true;
    }
    if (device.size() < 6) return fail(why, "l'identifiant du poste est trop court (6 caract\xC3\xA8res au moins)");
    std::set<std::string> devices;
    for (const auto& e : all)
        if (e.waiting() && e.device.rfind(device, 0) == 0) {
            hits.push_back(&e);
            devices.insert(e.device);
        }
    if (hits.empty()) return fail(why, "aucune demande en attente pour le poste " + device + " (rel\xC3\xA8ve d'abord les demandes)");
    if (devices.size() > 1) return fail(why, "plusieurs postes commencent par " + device + " : donne l'identifiant entier");
    out = *hits[0];   // la plus recente (listRequests : les plus recentes d'abord)
    return true;
}

bool isOnlineRequestPath(const std::string& folder, const std::string& path) {
    std::error_code ec;
    const fs::path dir = fs::weakly_canonical(P(requestsFolder(folder)), ec);
    if (ec) return false;
    const fs::path p = fs::weakly_canonical(P(path), ec);
    if (ec) return false;
    return p.parent_path() == dir && p.extension() == ".xpgreq";
}

FetchState readFetchState(const std::string& folder) {
    FetchState s;
    std::string text;
    Value v;
    const std::string path = deploy::joinPath(requestsFolder(folder), kFetchFile);
    if (!isFile(path) || !readText(path, text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != kFetchFormat)
        return s;
    s.fetchedAt = timeOf(v, "releve_le");
    s.total = static_cast<int>(v.integer("demandes"));
    s.pending = static_cast<int>(v.integer("en_attente"));
    return s;
}

// ============================================================================ GitHub
namespace {

struct Reply {
    int         status{0};
    std::string body;
    std::string error;      // une panne du reseau (en francais) ; vide : une reponse est arrivee
    std::string rateLeft;   // x-ratelimit-remaining
    Value       json;
    [[nodiscard]] std::string message() const {
        std::string m = json.isObject() ? json.str("message") : std::string();
        std::string clean;
        for (char c : m)
            if (static_cast<unsigned char>(c) >= 0x20) clean += c;
        return clean.substr(0, 300);
    }
};

class Api {
public:
    Api(Config config, std::string token, std::string tool) : config_(std::move(config)), token_(std::move(token)), tool_(std::move(tool)) {
        if (tool_.empty()) tool_ = "XPG-License-Admin";
    }
    ~Api() { wipe(token_); }
    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    // `tail` : apres /repos/PROPRIETAIRE/NOM ("" : le depot lui-meme).
    Reply call(const std::string& method, const std::string& tail, const std::string& body = {}) {
        Reply r;
        app::ai::HttpRequest req;
        req.method = method;
        req.url = gw::apiUrl(address(config_), tail);
        req.headers = {{"Authorization", "Bearer " + token_},
                       {"Accept", "application/vnd.github+json"},
                       {"X-GitHub-Api-Version", "2022-11-28"},
                       {"User-Agent", tool_}};
        if (!body.empty() || method != "GET") req.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
        req.body = body;
        req.connectTimeoutSeconds = 20;
        req.idleTimeoutSeconds = 60;
        app::ai::HttpSink sink;
        bool tooBig = false;
        sink.onData = [&](std::string_view chunk) {
            if (r.body.size() + chunk.size() > kMaxReply) {
                tooBig = true;
                return;
            }
            r.body.append(chunk);
        };
        auto client = app::ai::makeHttpClient();
        const auto resp = client->perform(req, sink);
        // Le jeton ne reste pas dans la requete (elle va etre detruite) : efface.
        for (auto& [k, v] : req.headers) sodium_memzero(v.data(), v.size());
        if (!resp.ok()) {
            app::ai::Url u;
            (void)app::ai::parseUrl(config_.api, u, nullptr);
            r.error = resp.error.empty() ? app::ai::failureText(resp.failure, u.host) : resp.error;
            if (r.error.empty()) r.error = "pas de connexion";
            return r;
        }
        if (tooBig) {
            r.error = "la r\xC3\xA9ponse de GitHub est trop grande";
            return r;
        }
        r.status = resp.status;
        r.rateLeft = resp.header("x-ratelimit-remaining");
        (void)core::json::parse(r.body, r.json, nullptr);
        return r;
    }

    // Une reponse refusee, en francais : le jeton n'y est jamais.
    [[nodiscard]] std::string explain(const Reply& r, const std::string& what, const char* whose = "ton jeton GitHub") const {
        if (!r.error.empty()) return "pas de r\xC3\xA9ponse de GitHub pour " + what + " : " + r.error + " (hors ligne ? rien n'a \xC3\xA9t\xC3\xA9 publi\xC3\xA9)";
        const std::string m = r.message().empty() ? std::string() : " (GitHub : " + r.message() + ")";
        switch (r.status) {
            case 401:
                return "GitHub refuse " + std::string(whose) + " (expir\xC3\xA9, r\xC3\xA9voqu\xC3\xA9 ou mal copi\xC3\xA9) pour " + what + " : enregistre-le de nouveau" + m;
            case 403:
                if (r.rateLeft == "0") return "GitHub limite les requ\xC3\xAAtes de ce jeton pour l'instant : r\xC3\xA9" "essaie dans une heure" + m;
                return std::string(whose) + " n'a pas le droit de " + what + " sur " + config_.repo
                       + " (il lui faut, sur ce d\xC3\xA9p\xC3\xB4t : Contents et Issues en lecture et \xC3\xA9" "criture)" + m;
            case 404:
                return "introuvable sur GitHub pour " + what + " : le d\xC3\xA9p\xC3\xB4t " + config_.repo
                       + " n'existe pas, ou " + whose + " ne le voit pas (un jeton \xC3\xA0 grain fin doit nommer ce d\xC3\xA9p\xC3\xB4t)" + m;
            case 410:
                return "les issues du d\xC3\xA9p\xC3\xB4t " + config_.repo + " sont d\xC3\xA9sactiv\xC3\xA9" "es : Settings \xE2\x80\xBA General \xE2\x80\xBA Features \xE2\x80\xBA Issues" + m;
            case 429:
                return "GitHub limite les requ\xC3\xAAtes pour l'instant : r\xC3\xA9" "essaie dans quelques minutes" + m;
            case 301:
            case 302:
            case 307:
            case 308:
                return "GitHub renvoie ailleurs pour " + what + " : le d\xC3\xA9p\xC3\xB4t a \xC3\xA9t\xC3\xA9 renomm\xC3\xA9 ou transf\xC3\xA9r\xC3\xA9 ; r\xC3\xA8gle son nouveau nom" + m;
            default:
                return "GitHub a r\xC3\xA9pondu " + std::to_string(r.status) + " pour " + what + m;
        }
    }

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    Config      config_;
    std::string token_;
    std::string tool_;
};

// ---- les fichiers du depot (API Contents) ----
struct RemoteFile {
    bool        exists{false};
    std::string content, sha;
};

bool getFile(Api& api, const std::string& path, RemoteFile& out, std::string* why) {
    out = RemoteFile{};
    const Reply r = api.call("GET", "/contents/" + path + "?ref=" + api.config().branch);
    if (r.status == 404 && r.error.empty()) return true;   // absent
    if (r.status != 200) return fail(why, api.explain(r, "lire " + path));
    crypto::Bytes raw;
    if (!r.json.isObject() || r.json.str("encoding", "base64") != "base64" || !crypto::unbase64(r.json.str("content"), raw))
        return fail(why, "GitHub a rendu " + path + " illisible");
    out.exists = true;
    out.content.assign(raw.begin(), raw.end());
    out.sha = r.json.str("sha");
    return true;
}

// Ecrire un fichier (un commit). `known` : ce que getFile vient de lire (le sha pour remplacer). Un conflit (409 :
// ecrit entre-temps) : relu, une seconde fois.
bool putFile(Api& api, const std::string& path, const std::string& content, const std::string& message, RemoteFile known, bool& unchanged,
             std::string* why) {
    unchanged = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0 && !getFile(api, path, known, why)) return false;
        if (known.exists && known.content == content) {
            unchanged = true;
            return true;
        }
        Value body = Value::object();
        body.set("message", message);
        body.set("content", crypto::base64(content));
        body.set("branch", api.config().branch);
        if (known.exists) body.set("sha", known.sha);
        const Reply r = api.call("PUT", "/contents/" + path, core::json::write(body, false));
        if (r.status == 200 || r.status == 201) return true;
        if ((r.status == 409 || r.status == 422) && r.error.empty() && attempt == 0) continue;
        return fail(why, api.explain(r, "\xC3\xA9" "crire " + path));
    }
    return fail(why, path + " a chang\xC3\xA9 sur GitHub pendant l'\xC3\xA9" "criture (deux fois) : relance plus tard");
}

// ---- les issues ----
struct Issue {
    int         number{0};
    std::string title, body, state;
    int         comments{0};
    Seconds     createdAt{0};
};

bool listIssues(Api& api, std::vector<Issue>& out, std::string* why) {
    out.clear();
    for (int page = 1; page <= kMaxPages; ++page) {
        const Reply r = api.call("GET", "/issues?state=all&per_page=100&page=" + std::to_string(page));
        if (r.status != 200) return fail(why, api.explain(r, "lire les issues"));
        if (!r.json.isArray()) return fail(why, "GitHub a rendu une liste d'issues illisible");
        for (const auto& it : r.json.items()) {
            if (!it.isObject() || it.has("pull_request")) continue;
            Issue i;
            i.number = static_cast<int>(it.integer("number"));
            i.title = it.str("title");
            i.body = it.str("body");
            i.state = it.str("state");
            i.comments = static_cast<int>(it.integer("comments"));
            i.createdAt = timeOf(it, "created_at");
            if (i.number > 0) out.push_back(std::move(i));
        }
        if (r.json.items().size() < 100) return true;
    }
    return true;
}

bool listComments(Api& api, int issue, std::vector<std::string>& bodies, std::string* why) {
    bodies.clear();
    for (int page = 1; page <= 10; ++page) {
        const Reply r = api.call("GET", "/issues/" + std::to_string(issue) + "/comments?per_page=100&page=" + std::to_string(page));
        if (r.status != 200) return fail(why, api.explain(r, "lire les commentaires de l'issue #" + std::to_string(issue)));
        if (!r.json.isArray()) return fail(why, "GitHub a rendu des commentaires illisibles");
        for (const auto& c : r.json.items())
            if (c.isObject()) bodies.push_back(c.str("body"));
        if (r.json.items().size() < 100) return true;
    }
    return true;
}

bool commentIssue(Api& api, int issue, const std::string& text, std::string* why) {
    Value body = Value::object();
    body.set("body", text);
    const Reply r = api.call("POST", "/issues/" + std::to_string(issue) + "/comments", core::json::write(body, false));
    if (r.status == 201 || r.status == 200) return true;
    return fail(why, api.explain(r, "commenter l'issue #" + std::to_string(issue)));
}

bool closeIssue(Api& api, int issue, bool completed, std::string* why) {
    Value body = Value::object();
    body.set("state", std::string("closed"));
    body.set("state_reason", std::string(completed ? "completed" : "not_planned"));
    const Reply r = api.call("PATCH", "/issues/" + std::to_string(issue), core::json::write(body, false));
    if (r.status == 200) return true;
    return fail(why, api.explain(r, "fermer l'issue #" + std::to_string(issue)));
}

// ---- l'etat d'un PC dans le depot ----
struct RemoteStatus {
    RemoteFile            file;
    bool                  readable{false};
    gw::DeviceStatus      status;
};

bool readRemoteStatus(Api& api, const std::string& device, RemoteStatus& out, std::string* why) {
    out = RemoteStatus{};
    if (!getFile(api, gw::statusPath(device), out.file, why)) return false;
    if (!out.file.exists) return true;
    std::vector<SignedBlock> blocks;
    if (licensing::readBlocks(out.file.content, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == DocKind::DeviceStatus &&
        gw::parseDeviceStatus(blocks[0].document, out.status, nullptr) && out.status.device == device)
        out.readable = true;
    return true;
}

licensing::Signer signerOf(const licensing::SigningKey& key) {
    return [&key](std::string_view m) { return key.sign(m); };
}

// Publier l'etat d'un PC (numero : celui du depot + 1). `line` : ce qui a ete publie.
bool publishStatus(Api& api, const licensing::SigningKey& key, const std::string& deviceKeyHex, gw::State state, const std::string& nonce,
                   const std::string& licenseId, const gw::StatusContent& content, Seconds now, std::string& line, std::string* why) {
    crypto::PublicKey pc{};
    if (!keyFromHex(deviceKeyHex, pc)) return fail(why, "la cl\xC3\xA9 du PC est illisible");
    const std::string device = gw::deviceId(pc);
    RemoteStatus remote;
    if (!readRemoteStatus(api, device, remote, why)) return false;
    gw::DeviceStatus s;
    s.device = device;
    // Un etat illisible dans le depot (abime, ecrit a la main) : un numero que le PC n'a pas pu voir passer.
    s.number = remote.readable ? remote.status.number + 1 : remote.file.exists ? static_cast<std::uint64_t>(std::max<Seconds>(now, 1)) : 1;
    s.issuedAt = now;
    s.state = state;
    s.requestNonce = nonce;
    s.licenseId = licenseId;
    if (!content.message.empty() || !content.license.empty()) {
        s.sealed = gw::sealFor(pc, gw::writeStatusContent(content));
        if (s.sealed.empty()) return fail(why, "impossible de sceller le contenu pour ce PC (sa cl\xC3\xA9 est refus\xC3\xA9" "e)");
    }
    const SignedBlock block = licensing::signBlock(DocKind::DeviceStatus, gw::writeDeviceStatus(s), key.publicKey(), signerOf(key));
    const std::string path = gw::statusPath(device);
    bool unchanged = false;
    if (!putFile(api, path, licensing::writeBlock(block), "Etat du poste " + device.substr(0, 12) + " : " + gw::stateKey(state), remote.file, unchanged, why))
        return false;
    line = path + " : " + gw::stateKey(state) + " (n\xC2\xB0 " + std::to_string(s.number) + ")";
    return true;
}

bool openApi(const std::string& folder, const Options& o, Config& c, std::unique_ptr<Api>& api, std::string* why) {
    if (!readConfig(folder, c, why)) return false;
    std::string token;
    if (!github::loadToken(folder, token, why)) {
        if (why) *why += " - le jeton GitHub de l'\xC3\xA9" "diteur sert aussi \xC3\xA0 la passerelle (Synchronisation GitHub \xE2\x80\xBA 2. Le jeton)";
        return false;
    }
    api = std::make_unique<Api>(c, std::move(token), o.tool);
    return true;
}

Result refused(std::string why) {
    Result r;
    r.ok = false;
    r.message = "Refus\xC3\xA9 : " + std::move(why);
    return r;
}

// Le numero d'un fichier signe d'un genre (registre, revocations, configuration) ; 0 : illisible.
std::uint64_t numberOf(std::string_view bytes, DocKind kind) {
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(bytes, blocks, nullptr)) return 0;
    for (const auto& b : blocks) {
        if (b.kind != kind) continue;
        if (kind == DocKind::Registry) {
            licensing::Registry reg;
            if (licensing::parseRegistry(b.document, reg, nullptr)) return reg.number;
        } else if (kind == DocKind::Revocations) {
            licensing::RevocationList list;
            if (licensing::parseRevocations(b.document, list, nullptr)) return list.number;
        } else if (kind == DocKind::Gateway) {
            gw::Config c;
            if (gw::parseConfig(b.document, c, nullptr)) return c.number;
        }
        return 0;
    }
    return 0;
}

// Le registre et la liste de revocation de l'archive -> le depot (s'ils y manquent ou y sont plus anciens).
bool syncArchiveFiles(Api& api, const std::string& folder, const licensing::SigningKey& key, bool registry, bool revocations, Result& res) {
    bool ok = true;
    const struct {
        bool        want;
        const char* local;
        const char* remote;
        DocKind     kind;
        const char* label;
    } files[] = {{registry, "registre/registre.xpgreg", gw::kRegistryPath, DocKind::Registry, "registre"},
                 {revocations, "revocations/revocations.xpgrev", gw::kRevocationsPath, DocKind::Revocations, "liste de r\xC3\xA9vocation"}};
    for (const auto& f : files) {
        if (!f.want) continue;
        const std::string path = U(P(github::archiveFolder(folder)) / P(f.local));
        if (!isFile(path)) {
            res.details.push_back(std::string("Pas de ") + f.label + " dans l'archive : rien \xC3\xA0 publier.");
            continue;
        }
        std::string local, why;
        if (!readText(path, local, &why)) {
            res.details.push_back(std::string("ATTENTION : ") + f.label + " de l'archive illisible (" + why + ").");
            ok = false;
            continue;
        }
        // Seulement ce que la cle de ce poste a signe (un fichier d'une autre cle ne serait pas cru par les Studios).
        std::vector<SignedBlock> blocks;
        if (!licensing::readBlocks(local, blocks, &why) || blocks.size() != 1 || blocks[0].kind != f.kind || !licensing::verifyBlock(blocks[0], key.publicKey())) {
            res.details.push_back(std::string("ATTENTION : ") + f.label + " de l'archive n'est pas sign\xC3\xA9 par la cl\xC3\xA9 de ce poste : pas publi\xC3\xA9.");
            ok = false;
            continue;
        }
        const std::uint64_t mine = numberOf(local, f.kind);
        RemoteFile remote;
        if (!getFile(api, f.remote, remote, &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
            continue;
        }
        const std::uint64_t theirs = remote.exists ? numberOf(remote.content, f.kind) : 0;
        if (remote.exists && theirs >= mine) {
            res.details.push_back(std::string(f.remote) + " : d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour sur la passerelle (n\xC2\xB0 " + std::to_string(theirs) + ").");
            continue;
        }
        bool unchanged = false;
        if (!putFile(api, f.remote, local, std::string("Passerelle : ") + f.label + " n\xC2\xB0 " + std::to_string(mine), remote, unchanged, &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
            continue;
        }
        res.published.push_back(std::string(f.remote) + " (n\xC2\xB0 " + std::to_string(mine) + ")");
        res.details.push_back(std::string("Publi\xC3\xA9 : ") + f.remote + " (n\xC2\xB0 " + std::to_string(mine) + ").");
    }
    return ok;
}

// Une demande en ligne ouverte et verifiee.
struct Opened {
    bool                         ok{false};
    std::string                  problem;
    gw::OnlineRequest            online;
    licensing::ActivationRequest request;
    ActivationRecord             record;
    crypto::PublicKey            pc{};
};

Opened openRequest(const licensing::SigningKey& key, const gw::IssueBody& ib, Seconds now) {
    Opened o;
    if (ib.keyId != key.keyId()) {
        o.problem = "scell\xC3\xA9" "e pour une autre cl\xC3\xA9 (" + ib.keyId + ") : ce poste ne peut pas la lire";
        return o;
    }
    std::string plain, why;
    if (!gw::openWith(key, ib.sealed, plain, &why)) {
        o.problem = why;
        return o;
    }
    const bool parsed = gw::parseOnlineRequest(plain, o.online, &why);
    wipe(plain);
    if (!parsed) {
        o.problem = why;
        return o;
    }
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(o.online.request, blocks, &why) || blocks.size() != 1 || blocks[0].kind != DocKind::Request) {
        o.problem = "la demande d'activation jointe est illisible" + (why.empty() ? std::string() : " (" + why + ")");
        return o;
    }
    if (!licensing::parseRequest(blocks[0].document, o.request, &why) || !keyFromHex(o.request.deviceKey, o.pc)) {
        o.problem = "la demande d'activation jointe est illisible (" + why + ")";
        return o;
    }
    if (!licensing::verifyBlock(blocks[0], o.pc)) {
        o.problem = "la demande n'est pas sign\xC3\xA9" "e par la cl\xC3\xA9 du PC qu'elle porte (modifi\xC3\xA9" "e ?)";
        return o;
    }
    if (gw::deviceId(o.pc) != ib.device) {
        o.problem = "l'identifiant du poste de l'issue n'est pas celui de la cl\xC3\xA9 du PC";
        return o;
    }
    if (!requestRecord(o.online.request, now, o.record, &why)) {
        o.problem = why;
        return o;
    }
    o.ok = true;
    return o;
}

// Une annulation valide de cette demande parmi les commentaires ? `forged` : des annulations refusees.
bool cancelledBy(const std::vector<std::string>& comments, const crypto::PublicKey& pc, const std::string& device, const std::string& nonce, int& forged) {
    bool found = false;
    for (const auto& body : comments) {
        SignedBlock b;
        if (!gw::parseCancelComment(body, b, nullptr)) continue;
        gw::Cancel c;
        if (!licensing::verifyBlock(b, pc) || !gw::parseCancel(b.document, c, nullptr) || c.device != device || c.requestNonce != nonce) {
            ++forged;
            continue;
        }
        found = true;
    }
    return found;
}

std::string requestFileName(const std::string& device, const std::string& nonce) {
    std::string n;
    for (char c : nonce)
        if (std::isalnum(static_cast<unsigned char>(c))) n += c;
    return device + "-" + n.substr(0, 8) + ".xpgreq";
}

void addProblem(Entry& e, const std::string& p) {
    if (std::find(e.problems.begin(), e.problems.end(), p) == e.problems.end()) e.problems.push_back(p);
}

// Fermer les autres issues ouvertes de ce poste (des demandes remplacees) apres une reponse.
void closeReplaced(Api& api, const std::string& folder, const std::string& device, int answered, Result& res) {
    for (auto& e : listRequests(folder)) {
        if (e.device != device || e.issue == answered || e.state != RequestState::Replaced || !e.issueOpen) continue;
        std::string why;
        if (commentIssue(api, e.issue, "Remplac\xC3\xA9" "e par une demande plus r\xC3\xA9" "cente de ce poste.", &why) && closeIssue(api, e.issue, false, &why)) {
            e.issueOpen = false;
            (void)writeEntry(folder, e, nullptr);
        } else {
            res.details.push_back("ATTENTION : l'issue #" + std::to_string(e.issue) + " (remplac\xC3\xA9" "e) reste ouverte : " + why);
        }
    }
}

// La licence d'un fichier .xpglic, signee par CETTE cle.
bool licenceOf(const std::string& licenceText, const licensing::SigningKey& key, licensing::License& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(licenceText, blocks, why)) return false;
    for (const auto& b : blocks) {
        if (b.kind != DocKind::License) continue;
        if (!licensing::verifyBlock(b, key.publicKey())) return fail(why, "la licence n'est pas sign\xC3\xA9" "e par la cl\xC3\xA9 de ce poste");
        return licensing::parseLicense(b.document, out, why);
    }
    return fail(why, "ce fichier ne contient pas de licence");
}

// Publier une licence a ses PC (sauf `skipKey`) ; << acceptee >> pour une demande en attente.
bool publishToDevices(Api& api, const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText,
                      const licensing::License& lic, const Options& o, Result& res, int& accepted, int& updated) {
    bool ok = true;
    accepted = updated = 0;
    const auto entries = listRequests(folder);
    std::vector<Entry> answered;
    for (const auto& d : lic.devices) {
        const Entry* waiting = nullptr;
        for (const auto& e : entries)   // la plus recente d'abord
            if (e.waiting() && e.deviceKey == d.key) {
                waiting = &e;
                break;
            }
        gw::StatusContent content;
        content.license = licenceText;
        content.message = waiting ? "Licence " + lic.licenseId + " accept\xC3\xA9" "e." : "Nouvelle \xC3\xA9mission de la licence " + lic.licenseId + ".";
        std::string line, why;
        const gw::State st = waiting ? gw::State::Accepted : gw::State::Updated;
        const std::string who = "PC " + (d.name.empty() ? std::string("(sans nom)") : cite(d.name));
        if (!publishStatus(api, key, d.key, st, waiting ? waiting->nonce : std::string(), lic.licenseId, content, o.now, line, &why)) {
            res.details.push_back("\xC3\x89" "CHEC pour le " + who + " : " + why);
            ok = false;
            continue;
        }
        res.published.push_back(line);
        res.details.push_back(who + " : " + (waiting ? "accept\xC3\xA9" "e" : "mise \xC3\xA0 jour") + " (" + line + ").");
        if (waiting) {
            ++accepted;
            answered.push_back(*waiting);
        } else {
            ++updated;
        }
    }
    for (auto& e : answered) {
        std::string why;
        e.state = RequestState::Accepted;
        e.answer = lic.licenseId;
        e.answeredAt = o.now;
        if (commentIssue(api, e.issue, "Accept\xC3\xA9" "e : la licence est publi\xC3\xA9" "e pour ce poste.", &why) && closeIssue(api, e.issue, true, &why))
            e.issueOpen = false;
        else
            res.details.push_back("ATTENTION : l'issue #" + std::to_string(e.issue) + " n'a pas pu \xC3\xAAtre ferm\xC3\xA9" "e (" + why + ") ; la licence est bien publi\xC3\xA9" "e pour ce PC.");
        if (!writeEntry(folder, e, &why)) res.details.push_back("ATTENTION : " + why);
        closeReplaced(api, folder, e.device, e.issue, res);
    }
    return ok;
}

} // namespace

// ============================================================================ verifier
Result verify(const std::string& folder, const Options& o) {
    Config c;
    std::unique_ptr<Api> api;
    std::string why;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    const Reply r = api->call("GET", "");
    if (r.status != 200) return refused(api->explain(r, "voir le d\xC3\xA9p\xC3\xB4t"));
    if (!r.json.isObject()) return refused("GitHub a rendu une description du d\xC3\xA9p\xC3\xB4t illisible");
    const Value* priv = r.json.find("private");
    if (!priv || !priv->isBool() || priv->asBool())
        return refused("le d\xC3\xA9p\xC3\xB4t " + c.repo + " est PRIV\xC3\x89 (ou sa visibilit\xC3\xA9 est inconnue) : les logiciels des clients lisent la passerelle SANS jeton. "
                       "Rends-le public (Settings \xE2\x80\xBA General \xE2\x80\xBA Danger Zone \xE2\x80\xBA Change visibility), ou cr\xC3\xA9" "e un d\xC3\xA9p\xC3\xB4t public \xC3\xA0 part : "
                       "jamais celui de l'archive des licences");
    const Value* perms = r.json.find("permissions");
    const bool push = perms && perms->isObject() && perms->flag("push");
    if (!push)
        return refused("ton jeton GitHub ne peut pas \xC3\xA9" "crire dans " + c.repo + " : sur github.com, donne-lui ce d\xC3\xA9p\xC3\xB4t (Repository access) avec "
                       "Repository permissions \xE2\x80\xBA Contents : Read and write et Issues : Read and write");
    res.details.push_back("D\xC3\xA9p\xC3\xB4t " + c.repo + " : public, ton jeton peut y \xC3\xA9" "crire.");
    const std::string def = r.json.str("default_branch");
    if (!def.empty() && def != c.branch)
        res.details.push_back("Attention : la branche par d\xC3\xA9" "faut du d\xC3\xA9p\xC3\xB4t est " + def + ", la passerelle \xC3\xA9" "crit dans " + c.branch + ".");
    const Reply issues = api->call("GET", "/issues?state=all&per_page=1");
    if (issues.status != 200) return refused(api->explain(issues, "lire les issues"));
    res.details.push_back("Issues : lisibles.");
    if (hasRequestToken(folder)) {
        std::string req;
        if (!loadRequestToken(folder, req, &why)) return refused(why);
        Api other(c, std::move(req), o.tool);
        const Reply q = other.call("GET", "");
        if (q.status != 200) return refused(other.explain(q, "voir le d\xC3\xA9p\xC3\xB4t", "le jeton des demandes"));
        res.details.push_back("Jeton des demandes : accept\xC3\xA9 par GitHub (il voit le d\xC3\xA9p\xC3\xB4t).");
    } else {
        res.details.push_back("Jeton des demandes : pas encore enregistr\xC3\xA9 (il faut le publier avant que les clients puissent demander).");
    }
    res.details.push_back("Les droits d'\xC3\xA9" "criture se confirment au premier envoi (Publier la configuration).");
    res.ok = true;
    res.message = "Passerelle v\xC3\xA9rifi\xC3\xA9" "e : " + c.repo + " (branche " + c.branch + ").";
    return res;
}

// ============================================================================ publier la configuration
Result publishArchive(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    Config c;
    std::unique_ptr<Api> api;
    std::string why;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    res.ok = syncArchiveFiles(*api, folder, key, true, true, res);
    res.message = res.ok ? "Registre et r\xC3\xA9vocations de la passerelle \xC3\xA0 jour." : "Refus\xC3\xA9 : le registre ou la liste de r\xC3\xA9vocation n'a pas \xC3\xA9t\xC3\xA9 publi\xC3\xA9.";
    return res;
}

Result publishConfig(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    Config c;
    std::unique_ptr<Api> api;
    std::string why;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    std::string token;
    if (!loadRequestToken(folder, token, &why)) return refused(why + " : sans lui, les logiciels des clients ne peuvent pas envoyer de demande");
    {
        std::string mine;
        const bool same = github::loadToken(folder, mine, nullptr) && mine == token;
        wipe(mine);
        if (same) {
            wipe(token);
            return refused("le jeton des demandes est TON jeton GitHub : il ne sera jamais publi\xC3\xA9 ; cr\xC3\xA9" "e un jeton \xC3\xA0 part (Issues seulement)");
        }
    }
    RemoteFile remote;
    if (!getFile(*api, gw::kConfigPath, remote, &why)) {
        wipe(token);
        return refused(why);
    }
    gw::Config gc;
    gc.number = 1;
    if (remote.exists) {
        const std::uint64_t before = numberOf(remote.content, DocKind::Gateway);
        // Illisible (abimee, ecrite a la main) : un numero plus grand que tout ce qu'un Studio a pu voir.
        gc.number = before > 0 ? before + 1 : static_cast<std::uint64_t>(std::max<Seconds>(o.now, 1));
    }
    gc.issuedAt = o.now;
    gc.repo = c.repo;
    gc.requestToken = token;
    wipe(token);
    std::string doc = gw::writeConfig(gc);
    wipe(gc.requestToken);
    const SignedBlock block = licensing::signBlock(DocKind::Gateway, doc, key.publicKey(), signerOf(key));
    wipe(doc);
    bool unchanged = false;
    Result res;
    if (!putFile(*api, gw::kConfigPath, licensing::writeBlock(block), "Configuration de la passerelle n\xC2\xB0 " + std::to_string(gc.number), remote, unchanged, &why))
        return refused(why);
    res.published.push_back(std::string(gw::kConfigPath) + " (n\xC2\xB0 " + std::to_string(gc.number) + ")");
    res.details.push_back("Publi\xC3\xA9 : " + std::string(gw::kConfigPath) + " (n\xC2\xB0 " + std::to_string(gc.number) + ", le jeton des demandes masqu\xC3\xA9).");
    const bool files = syncArchiveFiles(*api, folder, key, true, true, res);
    res.ok = files;
    res.message = files ? "Configuration de la passerelle publi\xC3\xA9" "e (n\xC2\xB0 " + std::to_string(gc.number) + ") sur " + c.repo + "."
                        : "Configuration publi\xC3\xA9" "e (n\xC2\xB0 " + std::to_string(gc.number) + "), mais le registre ou la liste de r\xC3\xA9vocation n'a pas pu l'\xC3\xAAtre.";
    if (!files) res.message = "Refus\xC3\xA9 : " + res.message;
    return res;
}

// ============================================================================ relever les demandes
Result fetchRequests(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    Config c;
    std::unique_ptr<Api> api;
    std::string why;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    std::vector<Issue> issues;
    if (!listIssues(*api, issues, &why)) return refused(why);

    std::map<int, Entry> known;
    for (auto& e : listRequests(folder)) known[e.issue] = std::move(e);

    Result res;
    bool ok = true;
    std::vector<Entry> entries;
    std::map<int, crypto::PublicKey> keys;
    std::map<int, int> commentCount;
    for (const auto& is : issues) {
        gw::IssueBody ib;
        if (!gw::parseIssueBody(is.body, ib, nullptr)) continue;   // une issue ordinaire : pas une demande
        Entry e;
        const auto old = known.find(is.number);
        const bool isNew = old == known.end();
        if (!isNew) e = old->second;
        e.issue = is.number;
        e.issueOpen = is.state != "closed";
        e.openedAt = is.createdAt;
        if (!e.seenAt) e.seenAt = o.now;
        e.device = ib.device;
        e.problems.clear();
        const Opened op = openRequest(key, ib, o.now);
        if (!op.ok) {
            e.state = RequestState::Unreadable;
            e.problems.push_back(op.problem);
            if (!writeEntry(folder, e, &why)) {
                res.details.push_back("ATTENTION : " + why);
                ok = false;
            }
            if (isNew) res.details.push_back("Issue #" + std::to_string(is.number) + " : illisible - " + op.problem + ".");
            continue;
        }
        // Le .xpgreq tel quel (l'assistant de licence prend des fichiers).
        const std::string file = deploy::joinPath(requestsFolder(folder), requestFileName(ib.device, op.request.nonce));
        if (!isFile(file) && !writePrivate(file, op.online.request, &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
        }
        if (e.nonce != op.request.nonce && !e.nonce.empty()) {   // le corps de l'issue a change : une autre demande
            e.state = RequestState::Pending;
            e.receivedPublished = false;
        }
        e.requestPath = file;
        e.deviceKey = op.request.deviceKey;
        e.nonce = op.request.nonce;
        e.customer = op.online.customer;
        e.contact = op.online.contact;
        e.message = op.online.message;
        e.deviceName = op.record.deviceName;
        e.fingerprint = op.record.fingerprint;
        e.software = op.record.software;
        e.licenseId = op.record.licenseId;
        e.requestedAt = op.request.createdAt;
        if (e.state == RequestState::Unreadable || e.state == RequestState::Replaced) e.state = RequestState::Pending;
        if (isNew) {
            ++res.newRequests;
            res.details.push_back("Nouvelle : #" + std::to_string(is.number) + " " + e.customer + ", PC " + cite(e.deviceName.empty() ? "(sans nom)" : e.deviceName)
                                  + (e.software.empty() ? std::string() : ", " + e.software) + (e.licenseId.empty() ? std::string() : ", licence " + e.licenseId) + ".");
        }
        keys[e.issue] = op.pc;
        commentCount[e.issue] = is.comments;
        entries.push_back(std::move(e));
    }

    // La plus recente de chaque poste l'emporte ; les autres en attente sont remplacees.
    std::map<std::string, int> newest;
    for (const auto& e : entries) newest[e.device] = std::max(newest[e.device], e.issue);

    for (auto& e : entries) {
        const bool final = e.state == RequestState::Accepted || e.state == RequestState::Refused || e.state == RequestState::Cancelled;
        if (final) {
            if (e.issueOpen) {   // repondue, mais l'issue est restee ouverte (une panne) : fermee maintenant
                if (closeIssue(*api, e.issue, e.state == RequestState::Accepted, &why)) e.issueOpen = false;
                else res.details.push_back("ATTENTION : " + why);
            }
        } else if (e.issue != newest[e.device]) {
            e.state = RequestState::Replaced;
        } else {
            // Une annulation signee par le PC ?
            bool cancelled = false;
            if (commentCount[e.issue] > 0) {
                std::vector<std::string> comments;
                if (!listComments(*api, e.issue, comments, &why)) {
                    res.details.push_back("ATTENTION : " + why);
                    ok = false;
                } else {
                    int forged = 0;
                    cancelled = cancelledBy(comments, keys[e.issue], e.device, e.nonce, forged);
                    if (forged > 0) addProblem(e, plural(static_cast<std::size_t>(forged), "annulation refus\xC3\xA9" "e (pas sign\xC3\xA9" "e par ce PC, ou pas pour cette demande)",
                                                         "annulations refus\xC3\xA9" "es (pas sign\xC3\xA9" "es par ce PC, ou pas pour cette demande)"));
                }
            }
            RemoteStatus remote;
            const bool remoteOk = readRemoteStatus(*api, e.device, remote, &why);
            if (!remoteOk) {
                res.details.push_back("ATTENTION : " + why);
                ok = false;
            }
            const bool remoteForThis = remoteOk && remote.readable && remote.status.requestNonce == e.nonce;
            if (cancelled) {
                std::string line;
                if (remoteForThis && remote.status.state == gw::State::Cancelled) {
                    e.state = RequestState::Cancelled;
                } else if (remoteOk && publishStatus(*api, key, e.deviceKey, gw::State::Cancelled, e.nonce, {}, {}, o.now, line, &why)) {
                    res.published.push_back(line);
                    e.state = RequestState::Cancelled;
                } else {
                    res.details.push_back("ATTENTION : l'annulation de #" + std::to_string(e.issue) + " n'a pas \xC3\xA9t\xC3\xA9 publi\xC3\xA9" "e : " + why);
                    ok = false;
                }
                if (e.state == RequestState::Cancelled) {
                    e.answeredAt = o.now;
                    res.details.push_back("Annul\xC3\xA9" "e par le client : #" + std::to_string(e.issue) + " (" + e.customer + ").");
                    if (e.issueOpen) {
                        if (closeIssue(*api, e.issue, false, &why)) e.issueOpen = false;
                        else res.details.push_back("ATTENTION : " + why);
                    }
                }
            } else if (remoteForThis && (remote.status.state == gw::State::Accepted || remote.status.state == gw::State::Refused ||
                                          remote.status.state == gw::State::Cancelled)) {
                // Deja repondue (par un autre poste Admin, ou avant la perte de ce dossier).
                e.state = remote.status.state == gw::State::Accepted ? RequestState::Accepted
                          : remote.status.state == gw::State::Refused ? RequestState::Refused : RequestState::Cancelled;
                if (e.state == RequestState::Accepted) e.answer = remote.status.licenseId;
            } else {
                if (!e.issueOpen) addProblem(e, "l'issue a \xC3\xA9t\xC3\xA9 ferm\xC3\xA9" "e sur GitHub sans annulation sign\xC3\xA9" "e par le PC");
                if (!e.receivedPublished && remoteForThis) e.receivedPublished = true;   // << recue >> deja publiee
                if (!e.receivedPublished && remoteOk) {
                    std::string line;
                    if (publishStatus(*api, key, e.deviceKey, gw::State::Received, e.nonce, {}, {}, o.now, line, &why)) {
                        e.receivedPublished = true;
                        res.published.push_back(line);
                    } else {
                        res.details.push_back("ATTENTION : \xC2\xAB re\xC3\xA7ue \xC2\xBB n'a pas \xC3\xA9t\xC3\xA9 publi\xC3\xA9" "e pour #" + std::to_string(e.issue) + " : " + why);
                        ok = false;
                    }
                }
                e.state = e.receivedPublished ? RequestState::Received : RequestState::Pending;
            }
        }
        if (!writeEntry(folder, e, &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
        }
    }

    // Le releve : sa date, les comptes.
    const auto all = listRequests(folder);
    int pending = 0;
    for (const auto& e : all) pending += e.waiting() ? 1 : 0;
    res.pending = pending;
    Value v = Value::object();
    v.set("format", std::string(kFetchFormat));
    v.set("releve_le", licensing::isoTime(o.now));
    v.set("depot", c.repo);
    v.set("demandes", static_cast<int>(all.size()));
    v.set("en_attente", pending);
    if (!writePrivate(deploy::joinPath(requestsFolder(folder), kFetchFile), core::json::write(v, true) + "\n", &why)) {
        res.details.push_back("ATTENTION : " + why);
        ok = false;
    }
    res.ok = ok;
    std::string head = plural(all.size(), "demande en ligne", "demandes en ligne") + " sur " + c.repo + " : " + std::to_string(pending) + " en attente de ta d\xC3\xA9" "cision";
    if (res.newRequests > 0) head += " (" + plural(static_cast<std::size_t>(res.newRequests), "nouvelle", "nouvelles") + ")";
    res.message = (ok ? "Demandes relev\xC3\xA9" "es : " : "Relev\xC3\xA9 incomplet : ") + head + ".";
    if (!ok) res.message = "Refus\xC3\xA9 : " + res.message;
    return res;
}

// ============================================================================ publier une licence, refuser
Result publishLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const Options& o) {
    licensing::License lic;
    std::string why;
    if (!licenceOf(licenceText, key, lic, &why)) return refused(why);
    if (lic.devices.empty()) return refused("la licence " + lic.licenseId + " n'est li\xC3\xA9" "e \xC3\xA0 aucun PC");
    Config c;
    std::unique_ptr<Api> api;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    int accepted = 0, updated = 0;
    const bool ok = publishToDevices(*api, folder, key, licenceText, lic, o, res, accepted, updated);
    res.ok = ok;
    const std::string what = plural(lic.devices.size(), "PC", "PC");
    if (ok) {
        res.message = "Licence " + lic.licenseId + " publi\xC3\xA9" "e sur la passerelle pour " + what;
        if (accepted) res.message += " (" + plural(static_cast<std::size_t>(accepted), "demande accept\xC3\xA9" "e", "demandes accept\xC3\xA9" "es") + ")";
        res.message += " : chaque PC la r\xC3\xA9" "cup\xC3\xA8re seul.";
    } else {
        res.message = "Refus\xC3\xA9 : la licence " + lic.licenseId + " n'est publi\xC3\xA9" "e que pour " + std::to_string(accepted + updated) + " PC sur " + std::to_string(lic.devices.size())
                      + " (le fichier sign\xC3\xA9 est bien dans l'archive ; republie-la plus tard).";
    }
    return res;
}

Result refuse(const std::string& folder, const licensing::SigningKey& key, const std::string& device, const std::string& reasonIn, const Options& o) {
    const std::string reason = trimmed(reasonIn);
    if (reason.empty()) return refused("donne la raison du refus (le client la lit dans son logiciel ; elle n'est pas publi\xC3\xA9" "e en clair)");
    if (reason.size() > 1500) return refused("la raison est trop longue (1500 caract\xC3\xA8res au plus)");
    Entry e;
    std::string why;
    if (!findWaiting(folder, device, e, &why)) return refused(why);
    Config c;
    std::unique_ptr<Api> api;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    gw::StatusContent content;
    content.message = reason;
    std::string line;
    if (!publishStatus(*api, key, e.deviceKey, gw::State::Refused, e.nonce, {}, content, o.now, line, &why)) return refused(why);
    res.published.push_back(line);
    e.state = RequestState::Refused;
    e.answer = reason;
    e.answeredAt = o.now;
    if (commentIssue(*api, e.issue, "Refus\xC3\xA9" "e.", &why) && closeIssue(*api, e.issue, false, &why)) e.issueOpen = false;
    else res.details.push_back("ATTENTION : l'issue #" + std::to_string(e.issue) + " n'a pas pu \xC3\xAAtre ferm\xC3\xA9" "e (" + why + ") ; le refus est bien publi\xC3\xA9.");
    if (!writeEntry(folder, e, &why)) res.details.push_back("ATTENTION : " + why);
    closeReplaced(*api, folder, e.device, e.issue, res);
    res.ok = true;
    res.message = "Demande #" + std::to_string(e.issue) + " refus\xC3\xA9" "e (" + e.customer + ") : le PC voit \xC2\xAB Refus\xC3\xA9" "e \xC2\xBB et ta raison.";
    res.details.insert(res.details.begin(), "Publi\xC3\xA9 : " + line + ".");
    return res;
}

// ============================================================================ revocation, PC retire
Result publishRevocation(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId, const std::vector<std::string>& deviceKeys,
                         const std::string& reason, const Options& o) {
    Config c;
    std::unique_ptr<Api> api;
    std::string why;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    bool ok = syncArchiveFiles(*api, folder, key, false, true, res);
    int done = 0;
    for (const auto& dk : deviceKeys) {
        gw::StatusContent content;
        content.message = reason.empty() ? std::string("Licence r\xC3\xA9voqu\xC3\xA9" "e.") : reason;
        std::string line;
        if (publishStatus(*api, key, dk, gw::State::Revoked, {}, licenseId, content, o.now, line, &why)) {
            res.published.push_back(line);
            ++done;
        } else {
            res.details.push_back("\xC3\x89" "CHEC pour un PC : " + why);
            ok = false;
        }
    }
    res.ok = ok;
    res.message = ok ? "R\xC3\xA9vocation publi\xC3\xA9" "e sur la passerelle (" + plural(static_cast<std::size_t>(done), "PC pr\xC3\xA9venu", "PC pr\xC3\xA9venus") + ")."
                     : "Refus\xC3\xA9 : la r\xC3\xA9vocation n'est publi\xC3\xA9" "e qu'en partie sur la passerelle (" + std::to_string(done) + " PC sur " + std::to_string(deviceKeys.size()) + ").";
    return res;
}

Result publishRemoval(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const std::string& removedDeviceKey,
                      const Options& o) {
    licensing::License lic;
    std::string why;
    if (!licenceOf(licenceText, key, lic, &why)) return refused(why);
    Config c;
    std::unique_ptr<Api> api;
    if (!openApi(folder, o, c, api, &why)) return refused(why);
    Result res;
    int accepted = 0, updated = 0;
    bool ok = publishToDevices(*api, folder, key, licenceText, lic, o, res, accepted, updated);
    gw::StatusContent content;
    content.message = "Ce PC a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 de la licence " + lic.licenseId + ".";
    std::string line;
    if (publishStatus(*api, key, removedDeviceKey, gw::State::Removed, {}, lic.licenseId, content, o.now, line, &why)) {
        res.published.push_back(line);
        res.details.push_back("PC retir\xC3\xA9 : " + line + ".");
    } else {
        res.details.push_back("\xC3\x89" "CHEC pour le PC retir\xC3\xA9 : " + why);
        ok = false;
    }
    res.ok = ok;
    res.message = ok ? "Nouvelle \xC3\xA9mission de " + lic.licenseId + " publi\xC3\xA9" "e sur la passerelle (" + plural(lic.devices.size(), "PC", "PC") + ", le PC retir\xC3\xA9 pr\xC3\xA9venu)."
                     : "Refus\xC3\xA9 : la nouvelle \xC3\xA9mission de " + lic.licenseId + " n'est publi\xC3\xA9" "e qu'en partie sur la passerelle.";
    return res;
}

// ============================================================================ les fichiers des Studios
std::string gatewayAddressHeader(const std::string& repo, const std::string& branch, Seconds now) {
    (void)now;
    // La meme forme que src/licensing/GatewayAddress.hpp (depose tel quel a sa place).
    return "// =============================================================================\n"
           "//  licensing/GatewayAddress.hpp - 1.34.0 : L'ADRESSE DE LA PASSERELLE GITHUB DES\n"
           "//  LICENCES, compilee dans les logiciels des clients (API Studio, IHM Studio)\n"
           "// -----------------------------------------------------------------------------\n"
           "//  Le depot GitHub PUBLIC ou les logiciels des clients envoient leurs demandes\n"
           "//  d'activation et recuperent leur licence (licensing/Gateway.hpp). Ce n'est pas\n"
           "//  un secret : \"proprietaire/nom\", comme dans l'adresse github.com/proprietaire/nom.\n"
           "//\n"
           "//  VIDE : ce programme n'a pas de passerelle - les demandes et les licences\n"
           "//  passent par fichier, comme avant la 1.34.0.\n"
           "//\n"
           "//  L'option CMake XPG_PASSERELLE_DEPOT (non vide) la remplace a la construction ;\n"
           "//  dans une construction d'essai, la variable d'environnement XPG_PASSERELLE\n"
           "//  aussi (le faux GitHub des essais).\n"
           "// =============================================================================\n"
           "#pragma once\n"
           "\n"
           "namespace licensing::gateway {\n"
           "\n"
           "inline constexpr const char* kCompiledRepo = \"" + repo + "\";\n"
           "inline constexpr const char* kCompiledBranch = \"" + (branch.empty() ? std::string("main") : branch) + "\";\n"
           "\n"
           "} // namespace licensing::gateway\n";
}

Result studioKit(const std::string& folder, const licensing::SigningKey* key, const std::string& outDir, Seconds now) {
    if (!key) return refused("pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste");
    if (key->test())
        return refused("c'est une cl\xC3\xA9 d'ESSAI : elle ne va jamais dans les Studios livr\xC3\xA9s (les licences qu'elle signe ne sont pas pour des clients). "
                       "Cr\xC3\xA9" "e la vraie cl\xC3\xA9 de l'\xC3\xA9" "diteur, ou restaure sa sauvegarde, puis recommence");
    if (trimmed(outDir).empty()) return refused("dans quel dossier \xC3\xA9" "crire les fichiers ?");
    std::string why, header, summary;
    if (!exportPublisherKeys(key, {}, kPublisherHeaderName, false, false, now, header, &why, &summary)) return refused(why);
    // Le registre de l'archive, signe par cette cle.
    const std::string regPath = U(P(github::archiveFolder(folder)) / P("registre/registre.xpgreg"));
    if (!isFile(regPath)) return refused("pas de registre sign\xC3\xA9 dans l'archive : signe-le d'abord (\xC3\x89" "ditions), puis recommence");
    std::string registry;
    if (!readText(regPath, registry, &why)) return refused(why);
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(registry, blocks, &why) || blocks.size() != 1 || blocks[0].kind != DocKind::Registry)
        return refused("le registre de l'archive est illisible" + (why.empty() ? std::string() : " (" + why + ")"));
    if (!licensing::verifyBlock(blocks[0], key->publicKey()))
        return refused("le registre de l'archive n'est pas sign\xC3\xA9 par la cl\xC3\xA9 de ce poste (" + key->keyId() + ") : les Studios ne le croiraient pas. "
                       "Signe un nouveau registre (\xC3\x89" "ditions), puis recommence");
    const std::uint64_t regNumber = numberOf(registry, DocKind::Registry);
    // L'adresse de la passerelle (vide : pas de passerelle).
    Config c;
    std::string repo, branch = "main";
    Result res;
    if (configured(folder)) {
        if (!readConfig(folder, c, &why)) return refused(why);
        repo = c.repo;
        branch = c.branch;
    }
    const std::string address = gatewayAddressHeader(repo, branch, now);
    const std::string date = licensing::isoDate(now);
    std::string readme =
        "Les fichiers pour les Studios (API Studio, IHM Studio) - pr\xC3\xA9par\xC3\xA9s le " + date + " par l'outil de l'\xC3\xA9" "diteur.\n"
        "\n"
        "Rien de secret ici : une cl\xC3\xA9 PUBLIQUE, un registre sign\xC3\xA9, une adresse. Ils se donnent au d\xC3\xA9veloppeur\n"
        "(ou se commitent dans le d\xC3\xA9p\xC3\xB4t du code) \xC3\xA0 ces chemins, puis les Studios se reconstruisent :\n"
        "\n"
        "  1. PublisherKey.hpp     ->  src/licensing/PublisherKey.hpp\n"
        "        la cl\xC3\xA9 publique de l'\xC3\xA9" "diteur (" + key->keyId() + ") : les Studios v\xC3\xA9rifient les licences avec elle.\n"
        "  2. registre.xpgreg      ->  ressources-licences/registre.xpgreg\n"
        "        le registre sign\xC3\xA9 n\xC2\xB0 " + std::to_string(regNumber) + " (les \xC3\xA9" "ditions, dont celle d'\xC3\xA9valuation) : livr\xC3\xA9 avec l'installateur.\n"
        "  3. GatewayAddress.hpp   ->  src/licensing/GatewayAddress.hpp\n" +
        (repo.empty() ? std::string("        AUCUNE passerelle r\xC3\xA9gl\xC3\xA9" "e : les demandes et les licences passeront par fichier.\n")
                      : "        la passerelle " + repo + " (branche " + branch + ") : les demandes et les licences passent par GitHub.\n") +
        "\n"
        "Si tu changes un jour de cl\xC3\xA9, exporte-la plut\xC3\xB4t avec \xC2\xAB Exporter la cl\xC3\xA9 publique\xE2\x80\xA6 \xC2\xBB en cochant\n"
        "\xC2\xAB garder les cl\xC3\xA9s d\xC3\xA9j\xC3\xA0 dans le fichier \xC2\xBB : les licences sign\xC3\xA9" "es avec l'ancienne restent valables.\n"
        "\n"
        "NE METS JAMAIS avec eux : la cl\xC3\xA9 priv\xC3\xA9" "e (cle-editeur-licences.cle), sa sauvegarde (.xpgcle), le jeton\n"
        "GitHub, le jeton des demandes, le code de secours de la synchro.\n";
    std::string crlf;
    for (char ch : readme) {
        if (ch == '\n') crlf += '\r';
        crlf += ch;
    }
    std::error_code ec;
    fs::create_directories(P(outDir), ec);
    const struct {
        const char*        name;
        const std::string& text;
    } files[] = {{kPublisherHeaderName, header}, {"registre.xpgreg", registry}, {"GatewayAddress.hpp", address}, {"LISEZ-MOI.txt", crlf}};
    for (const auto& f : files) {
        const std::string path = U(P(outDir) / P(f.name));
        if (!crypto::replaceFile(path, f.text, false, &why)) return refused(why);
        res.published.push_back(path);
    }
    res.ok = true;
    res.message = "Fichiers pour les Studios \xC3\xA9" "crits dans " + outDir + " : PublisherKey.hpp, registre.xpgreg, GatewayAddress.hpp (et LISEZ-MOI.txt).";
    res.details.push_back("Cl\xC3\xA9 publique : " + key->keyId() + " -> src/licensing/PublisherKey.hpp");
    res.details.push_back("Registre n\xC2\xB0 " + std::to_string(regNumber) + " -> ressources-licences/registre.xpgreg");
    if (repo.empty())
        res.details.push_back("ATTENTION : aucune passerelle r\xC3\xA9gl\xC3\xA9" "e : GatewayAddress.hpp est vide (les demandes passeront par fichier). R\xC3\xA8gle-la dans "
                              "Synchronisation GitHub \xE2\x80\xBA Passerelle des clients si tu la veux.");
    else
        res.details.push_back("Passerelle : " + repo + " (branche " + branch + ") -> src/licensing/GatewayAddress.hpp");
    res.details.push_back("Donne-les au d\xC3\xA9veloppeur (ou commite-les \xC3\xA0 ces chemins), puis reconstruis les Studios. Rien de secret dedans.");
    return res;
}

} // namespace license_admin::passerelle
