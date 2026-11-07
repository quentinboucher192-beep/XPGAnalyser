// =============================================================================
//  license_admin/ServiceClient.cpp - 1.35.0 : le service des licences, cote
//  editeur (voir ServiceClient.hpp, docs/SERVICE-LICENCES.md). JAMAIS LIVRE A UN
//  CLIENT.
// =============================================================================
#include "ServiceClient.hpp"

#include "Admin.hpp"
#include "ArchiveSync.hpp"

#include "../app/ai/AiHttp.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Documents.hpp"
#include "../licensing/Service.hpp"
#include "../licensing/SignedFile.hpp"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace license_admin::online {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace gw = licensing::gateway;
namespace svc = licensing::service;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

namespace {

constexpr const char* kConfigFormat = "xpg-service-editeur/1";
constexpr const char* kEntryFormat = "xpg-demande-service/1";
constexpr const char* kFetchFormat = "xpg-releve-service/1";
constexpr const char* kSnapshotFormat = "xpg-releve-licences/1";
constexpr const char* kNumbersFormat = "xpg-postes-publies/1";
constexpr std::size_t kMaxReply = 32u * 1024u * 1024u;
constexpr Seconds kDay = 86400;
constexpr int kMaxDetailLines = 50;

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
std::string isoOr(Seconds t) { return t ? licensing::isoTime(t) : std::string(); }
Seconds timeOf(const Value& v, std::string_view key) {
    Seconds t = 0;
    const std::string s = v.str(key);
    if (!s.empty() && licensing::parseIsoTime(s, t)) return t;
    return 0;
}
bool keyFromHex(const std::string& hex, crypto::PublicKey& out) {
    crypto::Bytes raw;
    if (!licensing::isKeyHex(hex) || !crypto::unhex(hex, raw, 32) || raw.size() != 32) return false;
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}
licensing::Signer signerOf(const licensing::SigningKey& key) {
    return [&key](std::string_view m) { return key.sign(m); };
}
Result refused(std::string why) {
    Result r;
    r.ok = false;
    r.message = "Refus\xC3\xA9 : " + std::move(why);
    return r;
}
// Un texte venu du service, montre a l'editeur : sans caractere de controle, 300 caracteres au plus.
std::string clean(std::string_view s, std::size_t max = 300) {
    std::string out;
    for (char c : s)
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    if (out.size() > max) out.resize(max);
    return out;
}

// Le texte d'un fichier de l'archive, signe par CETTE cle ; `kind` : le genre attendu (un seul bloc).
bool archiveSignedText(const std::string& folder, const char* relative, DocKind kind, const licensing::SigningKey& key, std::string& text,
                       SignedBlock& block, std::string* why) {
    text = archive::archivedText(folder, relative);
    if (text.empty()) return fail(why, "");
    std::vector<SignedBlock> blocks;
    std::string w;
    if (!licensing::readBlocks(text, blocks, &w) || blocks.size() != 1 || blocks[0].kind != kind)
        return fail(why, std::string(relative) + " de l'archive est illisible" + (w.empty() ? std::string() : " (" + w + ")"));
    if (!licensing::verifyBlock(blocks[0], key.publicKey()))
        return fail(why, std::string(relative) + " de l'archive n'est pas sign\xC3\xA9 par la cl\xC3\xA9 de ce poste (" + key.keyId() + ")");
    block = std::move(blocks[0]);
    return true;
}

// La licence d'un fichier .xpglic, signee par CETTE cle ; `block` : son bloc tel quel.
bool licenceOf(const std::string& licenceText, const licensing::SigningKey& key, licensing::License& out, SignedBlock* block, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!licensing::readBlocks(licenceText, blocks, why)) return false;
    for (auto& b : blocks) {
        if (b.kind != DocKind::License) continue;
        if (!licensing::verifyBlock(b, key.publicKey())) return fail(why, "la licence n'est pas sign\xC3\xA9" "e par la cl\xC3\xA9 de ce poste");
        if (!licensing::parseLicense(b.document, out, why)) return false;
        if (block) *block = std::move(b);
        return true;
    }
    return fail(why, "ce fichier ne contient pas de licence");
}

} // namespace

// ============================================================================ la configuration
std::string configPath(const std::string& folder) { return deploy::joinPath(folder, kConfigFile); }
bool configured(const std::string& folder) { return isFile(configPath(folder)); }

std::string normalizeUrl(std::string_view in) {
    std::string s = trimmed(std::string(in));
    while (!s.empty() && s.back() == '/') s.pop_back();
    // L'adresse n'a pas de chemin : tout en minuscules (un nom d'hote copie avec des majuscules).
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool readConfig(const std::string& folder, Config& out, std::string* why) {
    const std::string path = configPath(folder);
    if (!isFile(path))
        return fail(why, "le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 sur ce poste (XPG-License-Admin service-config --url https://... ; dans l'application : Service en ligne)");
    std::string text, err;
    Value v;
    if (!readText(path, text, why)) return false;
    if (!core::json::parse(text, v, &err) || !v.isObject() || v.str("format") != kConfigFormat)
        return fail(why, path + " est illisible (" + (err.empty() ? std::string("pas le bon format") : err) + ")");
    Config c;
    c.url = normalizeUrl(v.str("url"));
    if (!svc::validUrl(c.url)) return fail(why, path + " : l'adresse " + cite(c.url) + " est refus\xC3\xA9" "e (https://..., ou http://127.0.0.1 pour les essais)");
    out = std::move(c);
    return true;
}

bool writeConfig(const std::string& folder, const Config& in, std::string* why) {
    Config c;
    c.url = normalizeUrl(in.url);
    if (!svc::validUrl(c.url))
        return fail(why, "l'adresse du service s'\xC3\xA9" "crit https://NOM.COMPTE.workers.dev (sans chemin ; http:// seulement vers ce poste, pour les essais) : " + cite(c.url) +
                             " est refus\xC3\xA9" "e");
    Value v = Value::object();
    v.set("format", std::string(kConfigFormat));
    v.set("url", c.url);
    return writePrivate(configPath(folder), core::json::write(v, true) + "\n", why);
}

std::string Result::text() const {
    std::string t = message;
    for (const auto& d : details) t += "\n" + d;
    return t;
}

// ============================================================================ le client
std::string Reply::code() const { return json.isObject() ? clean(json.str("erreur"), 40) : std::string(); }
std::string Reply::message() const { return json.isObject() ? clean(json.str("message")) : std::string(); }

Client::Client(std::string url, const licensing::SigningKey& key, Options options) : url_(normalizeUrl(url)), key_(key), options_(std::move(options)) {
    if (options_.tool.empty()) options_.tool = "XPG-License-Admin";
    if (options_.now <= 0) options_.now = licensing::systemNow();
}

Reply Client::call(const std::string& method, const std::string& pathAndQuery, const std::string& body, bool signed_) {
    Reply r;
    app::ai::HttpRequest req;
    req.method = method;
    req.url = url_ + pathAndQuery;
    req.headers = {{"Accept", "application/json"}, {"User-Agent", options_.tool}};
    if (!body.empty() || method != "GET") req.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
    if (signed_)
        for (auto& h : svc::signRequest(method, pathAndQuery, body, key_.publicKey(), signerOf(key_), serviceNow())) req.headers.push_back(std::move(h));
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
    if (!resp.ok()) {
        app::ai::Url u;
        (void)app::ai::parseUrl(url_, u, nullptr);
        r.error = resp.error.empty() ? app::ai::failureText(resp.failure, u.host) : resp.error;
        if (r.error.empty()) r.error = "pas de connexion";
        return r;
    }
    if (tooBig) {
        r.error = "la r\xC3\xA9ponse du service est trop grande";
        return r;
    }
    r.status = resp.status;
    (void)core::json::parse(r.body, r.json, nullptr);
    return r;
}

bool Client::connect(std::string* why) {
    const Reply r = call("GET", "/v1/etat", {}, false);
    if (r.status != 200 || !r.error.empty()) return fail(why, explain(r, "lire l'\xC3\xA9tat du service"));
    if (!r.json.isObject() || r.json.str("service") != "xpg-licences" || !r.json.has("heure"))
        return fail(why, "l'adresse " + url_ + " r\xC3\xA9pond, mais ce n'est pas le service des licences XPG (v\xC3\xA9rifie l'adresse)");
    const Seconds t = static_cast<Seconds>(r.json.integer("heure"));
    offset_ = t > 0 ? t - options_.now : 0;
    state_ = r.json;
    connected_ = true;
    return true;
}

std::string Client::explain(const Reply& r, const std::string& what) const {
    if (!r.error.empty())
        return "pas de r\xC3\xA9ponse du service (" + url_ + ") pour " + what + " : " + r.error + " (hors ligne ? rien n'a \xC3\xA9t\xC3\xA9 chang\xC3\xA9 sur le service)";
    const std::string m = r.message().empty() ? std::string() : " (le service : " + r.message() + ")";
    switch (r.status) {
        case 401:
            return "le service refuse la signature de l'appel pour " + what + " (l'heure de ce poste ? un appel rejou\xC3\xA9 ?)" + m;
        case 403:
            if (r.code() == "code") return "le service refuse pour " + what + m;
            return "le service ne reconna\xC3\xAEt pas la cl\xC3\xA9 de ce poste (" + key_.keyId() + ") pour " + what +
                   " : sa cl\xC3\xA9 publique doit \xC3\xAAtre dans PUBLISHER_KEYS du Worker (cle-publique.txt du kit, puis npx wrangler deploy)" + m;
        case 404: return "introuvable sur le service pour " + what + m;
        case 409: return "conflit sur le service pour " + what + m;
        case 413: return "trop gros pour le service (2 Mio par envoi, 1,5 Mio par fichier) pour " + what + m;
        case 429: {
            const auto wait = r.json.isObject() ? r.json.integer("reessayer_dans") : 0;
            return "le service limite les appels pour l'instant (" + what + ")" + (wait > 0 ? " : r\xC3\xA9" "essaie dans " + std::to_string(wait) + " s" : std::string()) + m;
        }
        default:
            if (r.status >= 500) return "le service a eu une erreur (" + std::to_string(r.status) + ") pour " + what + m;
            return "le service a r\xC3\xA9pondu " + std::to_string(r.status) + " pour " + what + m;
    }
}

bool openClient(const std::string& folder, const licensing::SigningKey& key, const Options& o, std::unique_ptr<Client>& out, std::string* why) {
    Config c;
    if (!readConfig(folder, c, why)) return false;
    auto client = std::make_unique<Client>(c.url, key, o);
    if (!client->connect(why)) return false;
    out = std::move(client);
    return true;
}

// ============================================================================ l'etat, le certificat
bool ServiceState::needsRenewal(Seconds now) const { return !certOk || certExpires - now <= static_cast<Seconds>(kRenewBeforeDays) * kDay; }

std::string keyFingerprint(const std::string& keyHex) {
    crypto::PublicKey pk{};
    if (!keyFromHex(keyHex, pk)) return {};
    return licensing::publicKeyFingerprint(pk, 8);
}

namespace {

// Lire la cle du service et son certificat (GET /admin/cle-service) dans `st`.
bool readKeyAndCert(Client& c, const licensing::SigningKey& key, ServiceState& st, std::string* why) {
    const Reply r = c.call("GET", "/admin/cle-service");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return fail(why, c.explain(r, "lire la cl\xC3\xA9 du service"));
    st.adminOk = true;
    st.serviceKey = r.json.str("cle");
    if (!licensing::isKeyHex(st.serviceKey)) return fail(why, "le service a rendu une cl\xC3\xA9 illisible");
    const Value* cert = r.json.find("certificat");
    if (!cert || !cert->isString() || cert->asString().empty()) {
        st.certProblem = "aucun certificat d\xC3\xA9pos\xC3\xA9 : la cl\xC3\xA9 du service n'est pas encore certifi\xC3\xA9" "e";
        return true;
    }
    svc::ServiceCert sc;
    std::string w;
    // La date n'est pas regardee ici : l'expiration se dit a part (renouveler).
    if (!svc::verifyCert(cert->asString(), {key.publicKey()}, 0, sc, &w)) {
        st.certProblem = "le certificat d\xC3\xA9pos\xC3\xA9 ne va pas : " + w;
        return true;
    }
    st.certIssued = sc.issuedAt;
    st.certExpires = sc.expiresAt;
    st.certUrl = sc.url;
    if (sc.key != st.serviceKey) st.certProblem = "le certificat nomme une AUTRE cl\xC3\xA9 que celle du service (le Worker a \xC3\xA9t\xC3\xA9 r\xC3\xA9install\xC3\xA9 ?) : certifie de nouveau";
    else if (sc.url != c.url()) st.certProblem = "le certificat est pour l'adresse " + sc.url + ", pas " + c.url() + " : certifie de nouveau";
    else if (c.serviceNow() > sc.expiresAt) st.certProblem = "le certificat a expir\xC3\xA9 le " + licensing::isoDate(sc.expiresAt) + " : renouvelle-le";
    else {
        for (const char* right : {svc::kRightActivation, svc::kRightFloating, svc::kRightTime, svc::kRightTrial})
            if (!sc.grants(right)) st.certProblem = std::string("le certificat ne donne pas le droit ") + right + " : renouvelle-le";
    }
    st.certOk = st.certProblem.empty();
    return true;
}

} // namespace

Result verify(const std::string& folder, const licensing::SigningKey& key, const Options& o, ServiceState* out) {
    ServiceState st;
    Config cfg;
    std::string why;
    if (!readConfig(folder, cfg, &why)) return refused(why);
    Client c(cfg.url, key, o);
    if (!c.connect(&why)) {
        if (out) *out = st;
        return refused(why);
    }
    st.reached = true;
    const Value& e = c.state();
    st.version = clean(e.str("version"), 40);
    st.time = static_cast<Seconds>(e.integer("heure"));
    st.certified = e.flag("certifie");
    st.registry = static_cast<std::uint64_t>(e.integer("registre"));
    if (const Value* k = e.find("cle_service"); k && k->isString()) st.serviceKey = k->asString();
    Result res;
    res.details.push_back("Service : " + cfg.url + " (version " + (st.version.empty() ? std::string("?") : st.version) + "), heure " + isoOr(st.time) + ".");
    const Seconds drift = st.time - o.now;
    if (o.now > 0 && (drift > 300 || drift < -300))
        res.details.push_back("L'horloge de ce poste a " + std::to_string((drift < 0 ? -drift : drift) / 60) + " min d'\xC3\xA9" "cart avec le service : l'outil signe avec l'heure du service.");
    if (!readKeyAndCert(c, key, st, &why)) {
        if (out) *out = st;
        return refused(why);
    }
    res.details.push_back("Ta cl\xC3\xA9 (" + key.keyId() + ") est accept\xC3\xA9" "e par le service.");
    res.details.push_back("Cl\xC3\xA9 du service : " + keyFingerprint(st.serviceKey) + ".");
    if (st.certOk)
        res.details.push_back("Certificat : valable jusqu'au " + licensing::isoDate(st.certExpires) +
                              (st.needsRenewal(c.serviceNow()) ? " - \xC3\xA0 renouveler bient\xC3\xB4t." : "."));
    else
        res.details.push_back("Certificat : " + st.certProblem + ".");
    res.details.push_back(st.registry ? "Registre d\xC3\xA9pos\xC3\xA9 : n\xC2\xB0 " + std::to_string(st.registry) + "." : std::string("Aucun registre d\xC3\xA9pos\xC3\xA9 sur le service."));
    if (out) *out = st;
    res.ok = true;
    res.message = st.certOk ? "Service v\xC3\xA9rifi\xC3\xA9 : " + cfg.url + ", cl\xC3\xA9 certifi\xC3\xA9" "e jusqu'au " + licensing::isoDate(st.certExpires) + "."
                            : "Service joint (" + cfg.url + "), mais sa cl\xC3\xA9 n'est pas certifi\xC3\xA9" "e : Certifier la cl\xC3\xA9 du service.";
    return res;
}

Result readServiceKey(const std::string& folder, const licensing::SigningKey& key, const Options& o, std::string& keyHex) {
    keyHex.clear();
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    ServiceState st;
    if (!readKeyAndCert(*c, key, st, &why)) return refused(why);
    keyHex = st.serviceKey;
    Result r;
    r.ok = true;
    r.message = "Cl\xC3\xA9 du service lue : " + keyFingerprint(keyHex) + ".";
    r.details.push_back(st.certOk ? "Elle est certifi\xC3\xA9" "e jusqu'au " + licensing::isoDate(st.certExpires) + "." : "Certificat : " + st.certProblem + ".");
    return r;
}

Result certify(const std::string& folder, const licensing::SigningKey& key, const Options& o, const std::string& expectedKey) {
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    ServiceState st;
    if (!readKeyAndCert(*c, key, st, &why)) return refused(why);
    if (!expectedKey.empty() && expectedKey != st.serviceKey)
        return refused("la cl\xC3\xA9 du service a chang\xC3\xA9 depuis que tu l'as vue (" + keyFingerprint(expectedKey) + " -> " + keyFingerprint(st.serviceKey) +
                       ") : rien n'est sign\xC3\xA9 ; relis-la et compare son empreinte");
    svc::ServiceCert cert;
    cert.key = st.serviceKey;
    cert.url = c->url();
    cert.issuedAt = c->serviceNow();
    cert.expiresAt = cert.issuedAt + static_cast<Seconds>(kCertificateDays) * kDay;
    cert.rights = {svc::kRightActivation, svc::kRightFloating, svc::kRightTime, svc::kRightTrial};
    const SignedBlock block = licensing::signBlock(DocKind::ServiceCert, svc::writeCert(cert), key.publicKey(), signerOf(key));
    Value body = Value::object();
    body.set("certificat", licensing::writeBlock(block));
    const Reply r = c->call("PUT", "/admin/certificat", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "d\xC3\xA9poser le certificat"));
    Result res;
    res.ok = true;
    res.message = std::string(st.certOk ? "Certificat renouvel\xC3\xA9" : "Cl\xC3\xA9 du service certifi\xC3\xA9" "e") + " jusqu'au " + licensing::isoDate(cert.expiresAt) + ".";
    res.details.push_back("Cl\xC3\xA9 du service : " + keyFingerprint(cert.key) + ".");
    res.details.push_back("Adresse : " + cert.url + " ; droits : activation, flottante, heure, d\xC3\xA9" "couverte.");
    res.details.push_back("Sign\xC3\xA9 par ta cl\xC3\xA9 " + key.keyId() + (key.test() ? " (CL\xC3\x89 D'ESSAI : les Studios livr\xC3\xA9s ne le croiront pas)" : "") + ".");
    res.details.push_back("Renouvelle-le avant le " + licensing::isoDate(cert.expiresAt - static_cast<Seconds>(kRenewBeforeDays) * kDay) +
                          " (Service en ligne \xE2\x80\xBA Renouveler, ou service-certifier).");
    return res;
}

Result publishRegistry(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    std::string text, why;
    SignedBlock block;
    if (!archiveSignedText(folder, "registre/registre.xpgreg", DocKind::Registry, key, text, block, &why))
        return refused(why.empty() ? std::string("pas de registre sign\xC3\xA9 dans l'archive (\xC3\x89" "ditions \xE2\x80\xBA Signer le registre)") : why);
    licensing::Registry reg;
    if (!licensing::parseRegistry(block.document, reg, &why)) return refused(why);
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    const auto theirs = static_cast<std::uint64_t>(c->state().integer("registre"));
    Result res;
    res.ok = true;
    if (theirs >= reg.number) {
        res.message = "Registre d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour sur le service (n\xC2\xB0 " + std::to_string(theirs) + ").";
        return res;
    }
    Value body = Value::object();
    body.set("registre", text);
    const Reply r = c->call("PUT", "/admin/registre", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "d\xC3\xA9poser le registre n\xC2\xB0 " + std::to_string(reg.number)));
    res.message = "Registre n\xC2\xB0 " + std::to_string(reg.number) + " d\xC3\xA9pos\xC3\xA9 sur le service" + (theirs ? " (il avait le n\xC2\xB0 " + std::to_string(theirs) + ")." : ".");
    return res;
}

Result publishRevocations(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    std::string text, why;
    SignedBlock block;
    if (!archiveSignedText(folder, "revocations/revocations.xpgrev", DocKind::Revocations, key, text, block, &why))
        return refused(why.empty() ? std::string("pas de liste de r\xC3\xA9vocation dans l'archive") : why);
    licensing::RevocationList list;
    if (!licensing::parseRevocations(block.document, list, &why)) return refused(why);
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    Value body = Value::object();
    body.set("revocations", text);
    const Reply r = c->call("PUT", "/admin/revocations", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "d\xC3\xA9poser la liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(list.number)));
    Result res;
    res.ok = true;
    res.message = "Liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(list.number) + " d\xC3\xA9pos\xC3\xA9" "e sur le service (" +
                  plural(list.revoked.size(), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ").";
    res.details.push_back("Elle reste priv\xC3\xA9" "e : le service ne la donne qu'aux PC concern\xC3\xA9s, et ne renouvelle plus leurs activations.");
    return res;
}

// ============================================================================ les licences en ligne
std::string OnlineActivation::stateLabel() const {
    if (state == "active") return "active";
    if (state == "liberee") return "lib\xC3\xA9r\xC3\xA9" "e par le PC";
    if (state == "retiree") return "retir\xC3\xA9" "e";
    if (state == "revoquee") return "r\xC3\xA9voqu\xC3\xA9" "e";
    return state.empty() ? std::string("?") : state;
}

int OnlineLicence::activeCount() const {
    return static_cast<int>(std::count_if(activations.begin(), activations.end(), [](const OnlineActivation& a) { return a.state == "active"; }));
}

const OnlineActivation* OnlineLicence::activation(const std::string& deviceKey) const {
    for (const auto& a : activations)
        if (a.deviceKey == deviceKey) return &a;
    return nullptr;
}

const OnlineLicence* Snapshot::licence(const std::string& id) const {
    for (const auto& l : licences)
        if (l.licenseId == id) return &l;
    return nullptr;
}

int Snapshot::devices() const {
    int n = 0;
    for (const auto& l : licences) n += l.activeCount();
    return n;
}

int Snapshot::leases() const {
    int n = 0;
    for (const auto& l : licences) n += static_cast<int>(l.leases.size());
    return n;
}

bool parseLicences(const Value& reply, Snapshot& out, std::string* why) {
    const Value* list = reply.isObject() ? reply.find("licences") : nullptr;
    if (!list || !list->isArray()) return fail(why, "le service a rendu une liste de licences illisible");
    std::vector<OnlineLicence> all;
    for (const auto& x : list->items()) {
        if (!x.isObject()) continue;
        OnlineLicence l;
        l.licenseId = clean(x.str("licence"), 64);
        if (!licensing::isLicenseId(l.licenseId)) continue;
        l.issuedAt = timeOf(x, "emission");
        l.type = clean(x.str("type"), 16);
        l.seats = static_cast<int>(x.integer("postes_max"));
        l.leaseDays = static_cast<int>(x.integer("bail_jours", kDefaultLeaseDays));
        l.active = x.flag("active", true);
        l.hasCode = x.flag("code");
        if (const Value* acts = x.find("activations"); acts && acts->isArray())
            for (const auto& a : acts->items()) {
                if (!a.isObject() || !licensing::isKeyHex(a.str("poste"))) continue;
                OnlineActivation act;
                act.deviceKey = a.str("poste");
                act.name = clean(a.str("nom"), 100);
                act.station = clean(a.str("station"), 32);
                act.state = clean(a.str("etat"), 16);
                act.activatedAt = timeOf(a, "activee");
                act.seenAt = timeOf(a, "vue");
                act.until = timeOf(a, "jusqu_a");
                l.activations.push_back(std::move(act));
            }
        if (const Value* toks = x.find("jetons"); toks && toks->isArray())
            for (const auto& t : toks->items()) {
                if (!t.isObject() || !licensing::isKeyHex(t.str("poste"))) continue;
                l.leases.push_back({t.str("poste"), timeOf(t, "jusqu_a")});
            }
        all.push_back(std::move(l));
    }
    std::sort(all.begin(), all.end(), [](const OnlineLicence& a, const OnlineLicence& b) { return a.licenseId < b.licenseId; });
    out.licences = std::move(all);
    return true;
}

Snapshot readSnapshot(const std::string& folder) {
    Snapshot s;
    std::string text;
    Value v;
    const std::string path = deploy::joinPath(folder, kSnapshotFile);
    if (!isFile(path) || !readText(path, text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != kSnapshotFormat) return s;
    if (!parseLicences(v, s, nullptr)) return Snapshot{};
    s.at = timeOf(v, "releve_le");
    s.url = v.str("url");
    return s;
}

bool writeSnapshot(const std::string& folder, const Snapshot& s, std::string* why) {
    Value v = Value::object();
    v.set("format", std::string(kSnapshotFormat));
    v.set("releve_le", isoOr(s.at));
    v.set("url", s.url);
    Value list = Value::array();
    for (const auto& l : s.licences) {
        Value x = Value::object();
        x.set("licence", l.licenseId);
        x.set("emission", isoOr(l.issuedAt));
        x.set("type", l.type);
        x.set("postes_max", l.seats);
        x.set("bail_jours", l.leaseDays);
        x.set("active", l.active);
        x.set("code", l.hasCode);
        Value acts = Value::array();
        for (const auto& a : l.activations) {
            Value y = Value::object();
            y.set("poste", a.deviceKey);
            y.set("nom", a.name);
            y.set("station", a.station);
            y.set("activee", isoOr(a.activatedAt));
            y.set("vue", isoOr(a.seenAt));
            y.set("jusqu_a", isoOr(a.until));
            y.set("etat", a.state);
            acts.push(std::move(y));
        }
        x.set("activations", std::move(acts));
        Value toks = Value::array();
        for (const auto& t : l.leases) {
            Value y = Value::object();
            y.set("poste", t.deviceKey);
            y.set("jusqu_a", isoOr(t.until));
            toks.push(std::move(y));
        }
        x.set("jetons", std::move(toks));
        list.push(std::move(x));
    }
    v.set("licences", std::move(list));
    return writePrivate(deploy::joinPath(folder, kSnapshotFile), core::json::write(v, true) + "\n", why);
}

Result putLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const std::string& codeHash, int leaseDays,
                  bool active, const Options& o) {
    licensing::License lic;
    SignedBlock block;
    std::string why;
    if (!licenceOf(licenceText, key, lic, &block, &why)) return refused(why);
    if (lic.service.empty() || !lic.devices.empty())
        return refused("la licence " + lic.licenseId + " n'est pas une licence en ligne (elle est li\xC3\xA9" "e \xC3\xA0 des PC : elle part \xC3\xA0 ses PC, pas avec un code)");
    if (!codeHash.empty() && (codeHash.size() != 64 || !licensing::isKeyHex(codeHash))) return refused("l'empreinte du code est illisible");
    if (leaseDays < 1 || leaseDays > 366) return refused("le bail d'une activation va de 1 \xC3\xA0 366 jours");
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    if (lic.service != c->url())
        return refused("la licence " + lic.licenseId + " nomme le service " + lic.service + ", pas " + c->url() + " : signe une nouvelle \xC3\xA9mission pour ce service");
    Value body = Value::object();
    body.set("licence", licensing::writeBlock(block));
    if (!codeHash.empty()) body.set("code_hash", codeHash);
    body.set("bail_jours", leaseDays);
    body.set("active", active);
    const Reply r = c->call("PUT", "/admin/licences", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "d\xC3\xA9poser la licence " + lic.licenseId));
    Result res;
    res.ok = true;
    res.message = "Licence en ligne " + lic.licenseId + (active ? " d\xC3\xA9pos\xC3\xA9" "e sur le service" : " d\xC3\xA9sactiv\xC3\xA9" "e sur le service") + ".";
    res.details.push_back(std::to_string(lic.maxDevices) + (lic.type == licensing::LicenseType::Floating ? " postes en m\xC3\xAAme temps (flottante)" : " PC au plus") +
                          ", bail de " + std::to_string(leaseDays) + " j" + (lic.expiresAt ? ", fin le " + licensing::isoDate(lic.expiresAt) : std::string(", sans fin")) + ".");
    if (!codeHash.empty()) res.details.push_back("Le service ne garde que l'empreinte du code d'activation, jamais le code.");
    else if (active) res.details.push_back("Le code d'activation reste le m\xC3\xAAme.");
    if (!active) res.details.push_back("Ses PC ne sont plus renouvel\xC3\xA9s : chacun s'arr\xC3\xAAte \xC3\xA0 la fin de son bail, ou d\xC3\xA8s qu'il se connecte ; le code ne marche plus.");
    return res;
}

Result listLicences(const std::string& folder, const licensing::SigningKey& key, const Options& o, Snapshot& out) {
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    const Reply r = c->call("GET", "/admin/licences");
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "lire les licences en ligne"));
    Snapshot s;
    if (!parseLicences(r.json, s, &why)) return refused(why);
    s.at = o.now > 0 ? o.now : licensing::systemNow();
    s.url = c->url();
    Result res;
    if (!writeSnapshot(folder, s, &why)) res.details.push_back("ATTENTION : le relev\xC3\xA9 n'a pas pu \xC3\xAAtre gard\xC3\xA9 sur ce poste (" + why + ").");
    int floating = 0;
    for (const auto& l : s.licences) floating += l.floating() ? 1 : 0;
    res.ok = true;
    res.message = plural(s.licences.size(), "licence en ligne", "licences en ligne") + " : " + plural(static_cast<std::size_t>(s.devices()), "PC activ\xC3\xA9", "PC activ\xC3\xA9s") +
                  (floating ? ", " + plural(static_cast<std::size_t>(s.leases()), "jeton flottant en cours", "jetons flottants en cours") : std::string()) + ".";
    out = std::move(s);
    return res;
}

Result retireDevice(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId, const std::string& deviceKey, const Options& o) {
    if (!licensing::isLicenseId(licenseId)) return refused("identifiant de licence non valide : " + licenseId);
    if (!licensing::isKeyHex(deviceKey)) return refused("la cl\xC3\xA9 du PC n'est pas valide (64 hexad\xC3\xA9" "cimaux)");
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    Value body = Value::object();
    body.set("licence", licenseId);
    body.set("poste", deviceKey);
    const Reply r = c->call("POST", "/admin/activations/retirer", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "retirer ce PC de la licence " + licenseId));
    Result res;
    res.ok = true;
    res.message = "PC " + keyFingerprint(deviceKey).substr(0, 19) + " retir\xC3\xA9 de la licence " + licenseId + ".";
    res.details.push_back("Son activation n'est plus renouvel\xC3\xA9" "e : il s'arr\xC3\xAAte \xC3\xA0 la fin de son bail, ou d\xC3\xA8s qu'il se connecte. Sa place est libre.");
    return res;
}

Result setActive(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId, bool active, int leaseDays, const Options& o) {
    if (!licensing::isLicenseId(licenseId)) return refused("identifiant de licence non valide : " + licenseId);
    const std::string text = archive::archivedText(folder, "licences/" + archive::safeName(licenseId) + ".xpglic");
    if (text.empty()) return refused("la licence " + licenseId + " n'est pas dans l'archive de ce poste (synchronise l'archive d'abord)");
    return putLicence(folder, key, text, {}, leaseDays > 0 ? leaseDays : kDefaultLeaseDays, active, o);
}

Result stats(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    const Reply r = c->call("GET", "/admin/stats");
    if (r.status != 200 || !r.error.empty() || !r.json.isObject()) return refused(c->explain(r, "lire les compteurs"));
    Result res;
    res.ok = true;
    res.message = "Les compteurs du service (" + c->url() + ") :";
    const std::function<void(const Value&, const std::string&)> walk = [&](const Value& v, const std::string& prefix) {
        for (const auto& [k, x] : v.members()) {
            const std::string name = prefix.empty() ? clean(k, 60) : prefix + " \xE2\x80\xBA " + clean(k, 60);
            if (x.isObject()) walk(x, name);
            else if (x.isNumber()) res.details.push_back(name + " : " + std::to_string(x.asInt()));
            else if (x.isString()) res.details.push_back(name + " : " + clean(x.asString(), 80));
            else if (x.isBool()) res.details.push_back(name + " : " + (x.asBool() ? "oui" : "non"));
        }
    };
    walk(r.json, {});
    return res;
}

// ============================================================================ les demandes (sur ce poste)
const char* requestStateKey(RequestState s) noexcept {
    switch (s) {
        case RequestState::Pending: return "attente";
        case RequestState::Accepted: return "acceptee";
        case RequestState::Refused: return "refusee";
        case RequestState::Cancelled: return "annulee";
        case RequestState::Replaced: return "remplacee";
        case RequestState::Handled: return "traitee";
        case RequestState::Unreadable: return "illisible";
        case RequestState::Ignored: return "ignoree";
    }
    return "?";
}

bool parseRequestStateKey(std::string_view key, RequestState& out) noexcept {
    for (RequestState s : {RequestState::Pending, RequestState::Accepted, RequestState::Refused, RequestState::Cancelled, RequestState::Replaced,
                           RequestState::Handled, RequestState::Unreadable, RequestState::Ignored})
        if (key == requestStateKey(s)) {
            out = s;
            return true;
        }
    return false;
}

std::string requestStateLabel(RequestState s) {
    switch (s) {
        case RequestState::Pending: return "En attente";
        case RequestState::Accepted: return "Accept\xC3\xA9" "e";
        case RequestState::Refused: return "Refus\xC3\xA9" "e";
        case RequestState::Cancelled: return "Annul\xC3\xA9" "e par le client";
        case RequestState::Replaced: return "Remplac\xC3\xA9" "e";
        case RequestState::Handled: return "Trait\xC3\xA9" "e (un autre poste)";
        case RequestState::Unreadable: return "Illisible";
        case RequestState::Ignored: return "Ignor\xC3\xA9" "e (rejou\xC3\xA9" "e)";
    }
    return "?";
}

std::string joinWarning(const std::string& licenseId, const std::string& customer) {
    if (licenseId.empty()) return {};
    return "\xC3\x80 v\xC3\xA9rifier : ajout \xC3\xA0 la licence " + licenseId + " demand\xC3\xA9 par quelqu'un qui dit \xC3\xAAtre \xC2\xAB " +
           (customer.empty() ? std::string("?") : customer) + " \xC2\xBB \xE2\x80\x94 confirme avec ton client avant d'accepter.";
}

std::string joinWarning(const Entry& e) { return e.joinRequest() ? joinWarning(e.licenseId, e.customer) : std::string(); }

std::string requestsFolder(const std::string& folder) { return deploy::joinPath(folder, kRequestsDir); }

namespace {

std::string entryPath(const std::string& folder, int number) {
    return deploy::joinPath(requestsFolder(folder), "demande-" + std::to_string(number) + ".json");
}

bool writeEntry(const std::string& folder, Entry& e, std::string* why) {
    Value v = Value::object();
    v.set("format", std::string(kEntryFormat));
    v.set("numero", e.number);
    v.set("etat", std::string(requestStateKey(e.state)));
    if (!e.serviceState.empty()) v.set("etat_service", e.serviceState);
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
    if (e.createdAt) v.set("deposee_le", isoOr(e.createdAt));
    if (e.seenAt) v.set("vue_le", isoOr(e.seenAt));
    if (e.answeredAt) v.set("repondue_le", isoOr(e.answeredAt));
    if (!e.answer.empty()) v.set("reponse", e.answer);
    if (!e.requestPath.empty()) v.set("fichier", U(P(e.requestPath).filename()));
    if (!e.sealedHash.empty()) v.set("empreinte_sceau", e.sealedHash);
    Value probs = Value::array();
    for (const auto& p : e.problems) probs.push(Value(p));
    v.set("problemes", std::move(probs));
    e.sidecarPath = entryPath(folder, e.number);
    return writePrivate(e.sidecarPath, core::json::write(v, true) + "\n", why);
}

bool readEntry(const std::string& folder, const std::string& path, Entry& e) {
    std::string text;
    Value v;
    if (!readText(path, text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() || v.str("format") != kEntryFormat) return false;
    e = Entry{};
    e.number = static_cast<int>(v.integer("numero"));
    if (e.number <= 0) return false;
    if (!parseRequestStateKey(v.str("etat"), e.state)) e.state = RequestState::Unreadable;
    e.serviceState = v.str("etat_service");
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
    e.createdAt = timeOf(v, "deposee_le");
    e.seenAt = timeOf(v, "vue_le");
    e.answeredAt = timeOf(v, "repondue_le");
    e.answer = v.str("reponse");
    const std::string file = v.str("fichier");
    // Seulement un nom de fichier du dossier des demandes (jamais un chemin ailleurs).
    if (!file.empty() && file.find('/') == std::string::npos && file.find('\\') == std::string::npos && file != "." && file != "..")
        e.requestPath = deploy::joinPath(requestsFolder(folder), file);
    e.sealedHash = v.str("empreinte_sceau");
    if (const Value* p = v.find("problemes"); p && p->isArray())
        for (const auto& x : p->items())
            if (x.isString()) e.problems.push_back(x.asString());
    e.sidecarPath = path;
    return true;
}

void addProblem(Entry& e, const std::string& p) {
    if (std::find(e.problems.begin(), e.problems.end(), p) == e.problems.end()) e.problems.push_back(p);
}

constexpr const char* kEditedProblem = "le sceau de cette demande a chang\xC3\xA9 sur le service apr\xC3\xA8s sa premi\xC3\xA8re lecture : ignor\xC3\xA9 (la demande garde son contenu d'origine)";

// `full` : tout le nonce (deux nonces qui commencent pareil).
std::string requestFileName(const std::string& device, const std::string& nonce, bool full = false) {
    std::string n;
    for (char c : nonce)
        if (std::isalnum(static_cast<unsigned char>(c))) n += c;
    return device + "-" + (full ? n.substr(0, 64) : n.substr(0, 8)) + ".xpgreq";
}

// Une demande ouverte et verifiee.
struct Opened {
    bool                         ok{false};
    std::string                  problem;
    gw::OnlineRequest            online;
    licensing::ActivationRequest request;
    ActivationRecord             record;
    crypto::PublicKey            pc{};
};

Opened openRequest(const licensing::SigningKey& key, const std::string& device, const std::string& keyId, const std::string& sealed, Seconds now) {
    Opened o;
    if (keyId != key.keyId()) {
        o.problem = "scell\xC3\xA9" "e pour une autre cl\xC3\xA9 (" + clean(keyId, 40) + ") : ce poste ne peut pas la lire";
        return o;
    }
    std::string plain, why;
    if (!gw::openWith(key, sealed, plain, &why)) {
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
    if (svc::deviceId(o.pc) != device) {
        o.problem = "l'identifiant du poste de la demande n'est pas celui de la cl\xC3\xA9 du PC";
        return o;
    }
    if (!requestRecord(o.online.request, now, o.record, &why)) {
        o.problem = why;
        return o;
    }
    o.ok = true;
    return o;
}

RequestState fromServiceState(const std::string& s) {
    if (s == "annulee") return RequestState::Cancelled;
    if (s == "remplacee") return RequestState::Replaced;
    if (s == "traitee") return RequestState::Handled;
    return RequestState::Pending;
}

// ---- le dernier numero d'etat publie par PC (postes-publies.json) : un etat plus recent a toujours un numero plus
//      grand - max(le dernier + 1, l'heure Unix) : il monte aussi d'un poste Admin a l'autre. ----
std::string numbersPath(const std::string& folder) { return deploy::joinPath(folder, kStatusNumbersFile); }

std::uint64_t nextStatusNumber(const std::string& folder, const std::string& device, Seconds now) {
    std::string text;
    Value v;
    std::uint64_t last = 0;
    if (isFile(numbersPath(folder)) && readText(numbersPath(folder), text, nullptr) && core::json::parse(text, v, nullptr) && v.isObject() &&
        v.str("format") == kNumbersFormat)
        if (const Value* p = v.find("postes"); p && p->isObject()) last = static_cast<std::uint64_t>(p->integer(device));
    return std::max<std::uint64_t>(last + 1, static_cast<std::uint64_t>(std::max<Seconds>(now, 1)));
}

void rememberStatusNumber(const std::string& folder, const std::string& device, std::uint64_t number) {
    std::string text;
    Value v;
    if (!(isFile(numbersPath(folder)) && readText(numbersPath(folder), text, nullptr) && core::json::parse(text, v, nullptr) && v.isObject() &&
          v.str("format") == kNumbersFormat))
        v = Value::object();
    v.set("format", std::string(kNumbersFormat));
    Value postes = v.has("postes") && v.at("postes").isObject() ? v.at("postes") : Value::object();
    postes.set(device, static_cast<std::uint64_t>(number));
    v.set("postes", std::move(postes));
    (void)writePrivate(numbersPath(folder), core::json::write(v, true) + "\n", nullptr);
}

// Publier l'etat d'un PC (PUT /admin/postes : lu par GET /v1/poste). `line` : ce qui a ete publie.
bool publishStatus(Client& c, const std::string& folder, const licensing::SigningKey& key, const std::string& deviceKeyHex, gw::State state,
                   const std::string& nonce, const gw::StatusContent& content, std::string& line, std::string* why) {
    crypto::PublicKey pc{};
    if (!keyFromHex(deviceKeyHex, pc)) return fail(why, "la cl\xC3\xA9 du PC est illisible");
    const std::string device = svc::deviceId(pc);
    gw::DeviceStatus s;
    s.device = device;
    s.number = nextStatusNumber(folder, device, c.serviceNow());
    s.issuedAt = c.serviceNow();
    s.state = state;
    s.requestNonce = nonce;
    if (!content.message.empty() || !content.license.empty() || !content.revocations.empty()) {
        s.sealed = gw::sealFor(pc, gw::writeStatusContent(content));
        if (s.sealed.empty()) return fail(why, "impossible de sceller le contenu pour ce PC (sa cl\xC3\xA9 est refus\xC3\xA9" "e)");
    }
    const SignedBlock block = licensing::signBlock(DocKind::DeviceStatus, gw::writeDeviceStatus(s), key.publicKey(), signerOf(key));
    Value body = Value::object();
    body.set("poste", device);
    body.set("etat", licensing::writeBlock(block));
    const Reply r = c.call("PUT", "/admin/postes", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return fail(why, c.explain(r, "publier l'\xC3\xA9tat du poste " + device.substr(0, 12)));
    rememberStatusNumber(folder, device, s.number);
    line = "poste " + device.substr(0, 12) + " : " + gw::stateKey(state) + " (n\xC2\xB0 " + std::to_string(s.number) + ")";
    return true;
}

bool markHandled(Client& c, int number, std::string* why) {
    Value body = Value::object();
    body.set("numero", number);
    const Reply r = c.call("POST", "/admin/demandes/traiter", core::json::write(body, false));
    if (r.status != 200 || !r.error.empty()) return fail(why, c.explain(r, "marquer la demande n\xC2\xB0 " + std::to_string(number) + " trait\xC3\xA9" "e"));
    return true;
}

// La liste de revocation de l'archive, telle quelle (signee par CETTE cle) ; faux : aucune (`why` vide), ou illisible.
bool archiveRevocations(const std::string& folder, const licensing::SigningKey& key, std::string& text, licensing::RevocationList& list, std::string* why) {
    list = licensing::RevocationList{};
    SignedBlock b;
    if (!archiveSignedText(folder, "revocations/revocations.xpgrev", DocKind::Revocations, key, text, b, why)) {
        text.clear();
        return false;
    }
    if (!licensing::parseRevocations(b.document, list, why)) {
        text.clear();
        return false;
    }
    return true;
}

bool namesLicence(const licensing::RevocationList& list, const std::string& licenseId) {
    return std::any_of(list.revoked.begin(), list.revoked.end(), [&](const licensing::Revocation& x) { return x.licenseId == licenseId; });
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
        if (name.rfind("demande-", 0) != 0 || f.path().extension() != ".json") continue;
        Entry e;
        if (readEntry(folder, U(f.path()), e)) out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.number > b.number; });
    return out;
}

bool findWaiting(const std::string& folder, const std::string& whichIn, Entry& out, std::string* why) {
    std::string which = trimmed(whichIn);
    if (which.empty()) return fail(why, "quelle demande ? (son num\xC3\xA9ro, tel que service-demandes le montre)");
    const auto all = listRequests(folder);
    if (which[0] == '#') which.erase(0, 1);
    if (!which.empty() && which.size() < 10 && std::all_of(which.begin(), which.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int n = std::atoi(which.c_str());
        for (const auto& e : all)
            if (e.number == n) {
                if (!e.waiting()) return fail(why, "la demande n\xC2\xB0 " + which + " n'attend pas de d\xC3\xA9" "cision (" + requestStateLabel(e.state) + ")");
                out = e;
                return true;
            }
        return fail(why, "aucune demande relev\xC3\xA9" "e sous le n\xC2\xB0 " + which + " (rel\xC3\xA8ve d'abord les demandes)");
    }
    if (which.size() < 6) return fail(why, "l'identifiant du poste est trop court (6 caract\xC3\xA8res au moins)");
    std::vector<const Entry*> hits;
    std::set<std::string> devices;
    for (const auto& e : all)
        if (e.waiting() && e.device.rfind(which, 0) == 0) {
            hits.push_back(&e);
            devices.insert(e.device);
        }
    if (hits.empty()) return fail(why, "aucune demande en attente pour le poste " + which + " (rel\xC3\xA8ve d'abord les demandes)");
    if (devices.size() > 1) return fail(why, "plusieurs postes commencent par " + which + " : donne l'identifiant entier, ou le num\xC3\xA9ro");
    const Entry* best = hits[0];   // la plus recente par sa date SIGNEE
    for (const Entry* e : hits)
        if (e->requestedAt > best->requestedAt) best = e;
    out = *best;
    return true;
}

bool isOnlineRequestPath(const std::string& folder, const std::string& path) {
    const fs::path dir = P(requestsFolder(folder)).lexically_normal();
    const fs::path p = P(path).lexically_normal();
    return !path.empty() && p.parent_path() == dir && p.extension() == ".xpgreq";
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
    s.url = v.str("url");
    return s;
}

Result processRequests(const std::string& folder, const licensing::SigningKey& key, const Value& reply, const std::string& url, const Options& o) {
    const Value* list = reply.isObject() ? reply.find("demandes") : nullptr;
    if (!list || !list->isArray()) return refused("le service a rendu une liste de demandes illisible");
    std::map<int, Entry> known;
    for (auto& e : listRequests(folder)) known[e.number] = std::move(e);
    Result res;
    bool ok = true;
    std::string why;
    std::set<int> touched;
    int lines = 0, unlisted = 0, fresh = 0, ignored = 0;
    const auto note = [&](std::string line) {
        if (lines < kMaxDetailLines) {
            res.details.push_back(std::move(line));
            ++lines;
        } else {
            ++unlisted;
        }
    };
    // Ce qui est deja connu : les demandes vues (poste, nonce) et la date SIGNEE de la plus recente de chaque poste.
    std::map<std::pair<std::string, std::string>, int> seen;
    std::map<std::string, Seconds> latest;
    for (const auto& [n, e] : known) {
        if (e.nonce.empty()) continue;
        seen.emplace(std::make_pair(e.device, e.nonce), n);
        if (e.state != RequestState::Ignored) latest[e.device] = std::max(latest[e.device], e.requestedAt);
    }
    struct Fresh {
        int         number;
        std::string request;   // le .xpgreq tel quel
    };
    std::vector<Fresh> news;
    for (const auto& d : list->items()) {
        if (!d.isObject()) continue;
        const int n = static_cast<int>(d.integer("numero"));
        const std::string device = d.str("poste");
        if (n <= 0 || !svc::isDeviceId(device)) continue;
        const std::string sealed = d.str("scelle");
        const std::string state = d.str("etat");
        const std::string hash = crypto::sha256Hex(sealed);
        if (const auto old = known.find(n); old != known.end()) {
            Entry& e = old->second;
            e.serviceState = state;
            if (e.sealedHash.empty()) e.sealedHash = hash;
            else if (e.sealedHash != hash) addProblem(e, kEditedProblem);
            if (e.waiting() && state != "attente") {   // annulee par le PC, remplacee, ou traitee par un autre poste Admin
                e.state = fromServiceState(state);
                e.answeredAt = timeOf(d, "maj");
                if (e.state == RequestState::Cancelled) note("Annul\xC3\xA9" "e par le client : n\xC2\xB0 " + std::to_string(n) + " (" + e.customer + ").");
            }
            touched.insert(n);
            continue;
        }
        Entry e;
        e.number = n;
        e.device = device;
        e.serviceState = state;
        e.createdAt = timeOf(d, "cree");
        e.seenAt = o.now;
        e.sealedHash = hash;
        touched.insert(n);
        const Opened op = openRequest(key, device, d.str("cle"), sealed, o.now);
        if (!op.ok) {
            e.state = RequestState::Unreadable;
            e.problems.push_back(op.problem);
            note("Demande n\xC2\xB0 " + std::to_string(n) + " : illisible - " + op.problem + ".");
            known.emplace(n, std::move(e));
            continue;
        }
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
        e.state = fromServiceState(state);
        news.push_back({n, op.online.request});
        known.emplace(n, std::move(e));
    }
    // Les nouvelles, dans l'ordre de leur date signee : une demande deja vue, ou pas plus recente que la derniere de ce
    // PC, est REJOUEE : ignoree, rien n'est publie pour elle.
    std::sort(news.begin(), news.end(), [&known](const Fresh& a, const Fresh& b) {
        const Seconds ta = known.at(a.number).requestedAt, tb = known.at(b.number).requestedAt;
        return ta != tb ? ta < tb : a.number < b.number;
    });
    for (const auto& f : news) {
        Entry& e = known.at(f.number);
        std::string replay;
        if (const auto dup = seen.find(std::make_pair(e.device, e.nonce)); dup != seen.end())
            replay = "la m\xC3\xAAme demande que la n\xC2\xB0 " + std::to_string(dup->second) + " (d\xC3\xA9j\xC3\xA0 vue) : rien n'est fait pour elle";
        else if (const auto last = latest.find(e.device); last != latest.end() && e.requestedAt <= last->second)
            replay = "sa date sign\xC3\xA9" "e (" + licensing::isoTime(e.requestedAt) + ") n'est pas plus r\xC3\xA9" "cente que la derni\xC3\xA8re demande de ce PC (" +
                     licensing::isoTime(last->second) + ") - une ancienne demande renvoy\xC3\xA9" "e, ou l'horloge du PC recule : rien n'est fait pour elle";
        if (!replay.empty()) {
            e.state = RequestState::Ignored;
            addProblem(e, replay);
            ++ignored;
            note("ATTENTION : demande n\xC2\xB0 " + std::to_string(f.number) + " ignor\xC3\xA9" "e (rejou\xC3\xA9" "e) - " + replay + ".");
            continue;
        }
        seen.emplace(std::make_pair(e.device, e.nonce), f.number);
        latest[e.device] = e.requestedAt;
        // Le .xpgreq tel quel (l'assistant de licence prend des fichiers).
        std::string file = deploy::joinPath(requestsFolder(folder), requestFileName(e.device, e.nonce));
        std::string there;
        if (isFile(file) && (!readText(file, there, nullptr) || there != f.request))
            file = deploy::joinPath(requestsFolder(folder), requestFileName(e.device, e.nonce, true));
        if (!isFile(file) && !writePrivate(file, f.request, &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
        }
        e.requestPath = file;
        if (!e.waiting()) continue;
        ++fresh;
        std::string line = "Nouvelle : n\xC2\xB0 " + std::to_string(f.number) + " " + e.customer + ", PC " + cite(e.deviceName.empty() ? "(sans nom)" : e.deviceName) +
                           (e.software.empty() ? std::string() : ", " + e.software) + (e.licenseId.empty() ? std::string() : ", licence " + e.licenseId) + ".";
        if (e.joinRequest()) line += " " + joinWarning(e);
        note(std::move(line));
    }
    if (unlisted > 0)
        res.details.push_back("\xE2\x80\xA6 et " + plural(static_cast<std::size_t>(unlisted), "autre ligne", "autres lignes") +
                              " (la liste compl\xC3\xA8te : Demandes en ligne, ou service-demandes --toutes).");
    // La plus recente demande en attente de chaque poste, par sa date SIGNEE ; les autres en attente sont remplacees.
    std::map<std::string, std::pair<Seconds, int>> newest;
    for (const auto& [n, e] : known) {
        if (!e.waiting()) continue;
        const auto cand = std::make_pair(e.requestedAt, n);
        const auto it = newest.find(e.device);
        if (it == newest.end() || it->second < cand) newest[e.device] = cand;
    }
    for (auto& [n, e] : known)
        if (e.waiting() && newest[e.device].second != n) {
            e.state = RequestState::Replaced;
            touched.insert(n);
        }
    for (const int n : touched) {
        if (!writeEntry(folder, known.at(n), &why)) {
            res.details.push_back("ATTENTION : " + why);
            ok = false;
        }
    }
    int pending = 0;
    for (const auto& [n, e] : known) pending += e.waiting() ? 1 : 0;
    Value v = Value::object();
    v.set("format", std::string(kFetchFormat));
    v.set("releve_le", isoOr(o.now));
    v.set("url", url);
    v.set("demandes", static_cast<int>(known.size()));
    v.set("en_attente", pending);
    if (!writePrivate(deploy::joinPath(requestsFolder(folder), kFetchFile), core::json::write(v, true) + "\n", &why)) {
        res.details.push_back("ATTENTION : " + why);
        ok = false;
    }
    res.ok = ok;
    std::string head = plural(known.size(), "demande", "demandes") + " : " + std::to_string(pending) + " en attente de ta d\xC3\xA9" "cision";
    if (fresh > 0) head += " (" + plural(static_cast<std::size_t>(fresh), "nouvelle", "nouvelles") + ")";
    if (ignored > 0) head += " ; " + plural(static_cast<std::size_t>(ignored), "rejou\xC3\xA9" "e, ignor\xC3\xA9" "e", "rejou\xC3\xA9" "es, ignor\xC3\xA9" "es");
    res.message = (ok ? "Demandes relev\xC3\xA9" "es : " : "Refus\xC3\xA9 : relev\xC3\xA9 incomplet : ") + head + ".";
    return res;
}

Result fetchRequests(const std::string& folder, const licensing::SigningKey& key, const Options& o) {
    std::unique_ptr<Client> c;
    std::string why;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    // Depuis la plus ancienne demande encore en attente sur ce poste (son etat peut avoir change), sinon la derniere vue.
    int from = 0, maxSeen = 0, minWaiting = 0;
    const FetchState before = readFetchState(folder);
    if (before.url == c->url()) {
        for (const auto& e : listRequests(folder)) {
            maxSeen = std::max(maxSeen, e.number);
            if (e.waiting() && (minWaiting == 0 || e.number < minWaiting)) minWaiting = e.number;
        }
        from = minWaiting > 0 ? minWaiting - 1 : maxSeen;
    }
    const Reply r = c->call("GET", "/admin/demandes?depuis=" + std::to_string(from));
    if (r.status != 200 || !r.error.empty()) return refused(c->explain(r, "lire les demandes"));
    return processRequests(folder, key, r.json, c->url(), o);
}

Result refuse(const std::string& folder, const licensing::SigningKey& key, const std::string& which, const std::string& reasonIn, const Options& o) {
    const std::string reason = trimmed(reasonIn);
    if (reason.empty()) return refused("donne la raison du refus (le client la lit dans son logiciel ; elle est scell\xC3\xA9" "e pour son PC)");
    if (reason.size() > 1500) return refused("la raison est trop longue (1500 caract\xC3\xA8res au plus)");
    Entry e;
    std::string why;
    if (!findWaiting(folder, which, e, &why)) return refused(why);
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    gw::StatusContent content;
    content.message = reason;
    std::string line;
    if (!publishStatus(*c, folder, key, e.deviceKey, gw::State::Refused, e.nonce, content, line, &why)) return refused(why);
    Result res;
    res.details.push_back("Publi\xC3\xA9 : " + line + ".");
    if (!markHandled(*c, e.number, &why)) res.details.push_back("ATTENTION : " + why + " ; le refus est bien publi\xC3\xA9 pour le PC.");
    e.state = RequestState::Refused;
    e.answer = reason;
    e.answeredAt = o.now;
    if (!writeEntry(folder, e, &why)) res.details.push_back("ATTENTION : " + why);
    res.ok = true;
    res.message = "Demande n\xC2\xB0 " + std::to_string(e.number) + " refus\xC3\xA9" "e (" + e.customer + ") : le PC voit \xC2\xAB Refus\xC3\xA9" "e \xC2\xBB et ta raison.";
    return res;
}

Result answerOnline(const std::string& folder, const licensing::SigningKey& key, int number, const std::string& licenseId, const Options& o) {
    Entry e;
    std::string why;
    if (!findWaiting(folder, std::to_string(number), e, &why)) return refused(why);
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    gw::StatusContent content;
    content.message = "Ta licence en ligne " + licenseId + " est pr\xC3\xAAte : ton \xC3\xA9" "diteur te donne son code d'activation (Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA "
                      "Activer avec un code).";
    std::string line;
    if (!publishStatus(*c, folder, key, e.deviceKey, gw::State::Accepted, e.nonce, content, line, &why)) return refused(why);
    Result res;
    res.details.push_back("Publi\xC3\xA9 : " + line + " (sans le code : tu le donnes toi-m\xC3\xAAme au client).");
    if (!markHandled(*c, e.number, &why)) res.details.push_back("ATTENTION : " + why);
    e.state = RequestState::Accepted;
    e.answer = licenseId;
    e.answeredAt = o.now;
    if (!writeEntry(folder, e, &why)) res.details.push_back("ATTENTION : " + why);
    res.ok = true;
    res.message = "Demande n\xC2\xB0 " + std::to_string(e.number) + " accept\xC3\xA9" "e : licence en ligne " + licenseId + " (le PC l'active avec le code).";
    return res;
}

// ============================================================================ les licences liees a des PC
namespace {

bool publishToDevices(Client& c, const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const licensing::License& lic,
                      bool withRevocations, const std::string& skipKey, Result& res, int& accepted, int& updated) {
    bool ok = true;
    accepted = updated = 0;
    const auto entries = listRequests(folder);
    std::string revText, revWhy;
    licensing::RevocationList revList;
    const bool hasRev = archiveRevocations(folder, key, revText, revList, &revWhy);
    if (!hasRev && !revWhy.empty()) res.details.push_back("ATTENTION : " + revWhy + " : elle ne part pas avec la licence.");
    const bool withRev = hasRev && (withRevocations || namesLicence(revList, lic.licenseId));
    for (const auto& d : lic.devices) {
        if (d.key == skipKey) continue;
        const Entry* waiting = nullptr;
        for (const auto& e : entries)   // la plus recente par sa date signee
            if (e.waiting() && e.deviceKey == d.key && (!waiting || e.requestedAt > waiting->requestedAt)) waiting = &e;
        gw::StatusContent content;
        content.license = licenceText;
        if (withRev) content.revocations = revText;
        content.message = waiting ? "Licence " + lic.licenseId + " accept\xC3\xA9" "e." : "Nouvelle \xC3\xA9mission de la licence " + lic.licenseId + ".";
        std::string line, why;
        const gw::State st = waiting ? gw::State::Accepted : gw::State::Updated;
        const std::string who = "PC " + (d.name.empty() ? std::string("(sans nom)") : cite(d.name));
        if (!publishStatus(c, folder, key, d.key, st, waiting ? waiting->nonce : std::string(), content, line, &why)) {
            res.details.push_back("\xC3\x89" "CHEC pour le " + who + " : " + why);
            ok = false;
            continue;
        }
        res.details.push_back(who + " : " + (waiting ? "accept\xC3\xA9" "e" : "mise \xC3\xA0 jour") + " (" + line + ").");
        if (!waiting) {
            ++updated;
            continue;
        }
        ++accepted;
        Entry e = *waiting;
        if (!markHandled(c, e.number, &why)) res.details.push_back("ATTENTION : " + why + " ; la licence est bien publi\xC3\xA9" "e pour ce PC.");
        e.state = RequestState::Accepted;
        e.answer = lic.licenseId;
        e.answeredAt = c.serviceNow();
        if (!writeEntry(folder, e, &why)) res.details.push_back("ATTENTION : " + why);
    }
    if (withRev)
        res.details.push_back("La liste de r\xC3\xA9vocation n\xC2\xB0 " + std::to_string(revList.number) + " part scell\xC3\xA9" "e avec la licence (jamais en clair).");
    return ok;
}

} // namespace

Result publishBoundLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, bool withRevocations, const Options& o) {
    licensing::License lic;
    std::string why;
    if (!licenceOf(licenceText, key, lic, nullptr, &why)) return refused(why);
    if (lic.devices.empty()) return refused("la licence " + lic.licenseId + " n'est li\xC3\xA9" "e \xC3\xA0 aucun PC (une licence en ligne : elle s'active avec son code)");
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    Result res;
    int accepted = 0, updated = 0;
    const bool ok = publishToDevices(*c, folder, key, licenceText, lic, withRevocations, {}, res, accepted, updated);
    res.ok = ok;
    if (ok) {
        res.message = "Licence " + lic.licenseId + " publi\xC3\xA9" "e pour " + plural(lic.devices.size(), "PC", "PC");
        if (accepted) res.message += " (" + plural(static_cast<std::size_t>(accepted), "demande accept\xC3\xA9" "e", "demandes accept\xC3\xA9" "es") + ")";
        res.message += " : chaque PC la r\xC3\xA9" "cup\xC3\xA8re seul \xC3\xA0 sa prochaine connexion.";
    } else {
        res.message = "Refus\xC3\xA9 : la licence " + lic.licenseId + " n'est publi\xC3\xA9" "e que pour " + std::to_string(accepted + updated) + " PC sur " +
                      std::to_string(lic.devices.size()) + " (le fichier sign\xC3\xA9 est bien dans l'archive ; republie-la plus tard).";
    }
    return res;
}

Result publishRevocation(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId, const std::vector<std::string>& deviceKeys,
                         const std::string& reason, const Options& o) {
    std::string revText, why;
    licensing::RevocationList revList;
    if (!archiveRevocations(folder, key, revText, revList, &why))
        return refused(why.empty() ? std::string("pas de liste de r\xC3\xA9vocation dans l'archive : signe d'abord la r\xC3\xA9vocation") : why);
    Result res = publishRevocations(folder, key, o);
    if (!res.ok) return res;
    if (deviceKeys.empty()) {
        res.details.push_back("Aucun PC li\xC3\xA9 \xC3\xA0 pr\xC3\xA9venir : les PC activ\xC3\xA9s en ligne ne sont plus renouvel\xC3\xA9s.");
        return res;
    }
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    int done = 0;
    for (const auto& dk : deviceKeys) {
        gw::StatusContent content;
        content.message = reason.empty() ? std::string("Licence " + licenseId + " r\xC3\xA9voqu\xC3\xA9" "e.") : reason;
        content.revocations = revText;
        std::string line;
        if (publishStatus(*c, folder, key, dk, gw::State::Revoked, {}, content, line, &why)) {
            res.details.push_back("Publi\xC3\xA9 : " + line + ".");
            ++done;
        } else {
            res.details.push_back("\xC3\x89" "CHEC pour un PC : " + why);
            res.ok = false;
        }
    }
    res.message = res.ok ? "R\xC3\xA9vocation de " + licenseId + " publi\xC3\xA9" "e (" + plural(static_cast<std::size_t>(done), "PC li\xC3\xA9 pr\xC3\xA9venu", "PC li\xC3\xA9s pr\xC3\xA9venus") + ")."
                         : "Refus\xC3\xA9 : la r\xC3\xA9vocation n'est publi\xC3\xA9" "e qu'en partie (" + std::to_string(done) + " PC sur " + std::to_string(deviceKeys.size()) + ").";
    return res;
}

Result publishRemoval(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const std::string& removedDeviceKey,
                      const Options& o) {
    licensing::License lic;
    std::string why;
    if (!licenceOf(licenceText, key, lic, nullptr, &why)) return refused(why);
    std::unique_ptr<Client> c;
    if (!openClient(folder, key, o, c, &why)) return refused(why);
    Result res;
    int accepted = 0, updated = 0;
    bool ok = publishToDevices(*c, folder, key, licenceText, lic, false, removedDeviceKey, res, accepted, updated);
    gw::StatusContent content;
    content.message = "Ce PC a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 de la licence " + lic.licenseId + ".";
    std::string revText, revWhy;
    licensing::RevocationList revList;
    if (archiveRevocations(folder, key, revText, revList, &revWhy) && namesLicence(revList, lic.licenseId)) content.revocations = revText;
    std::string line;
    if (publishStatus(*c, folder, key, removedDeviceKey, gw::State::Removed, {}, content, line, &why)) {
        res.details.push_back("PC retir\xC3\xA9 : " + line + ".");
    } else {
        res.details.push_back("\xC3\x89" "CHEC pour le PC retir\xC3\xA9 : " + why);
        ok = false;
    }
    res.ok = ok;
    res.message = ok ? "Nouvelle \xC3\xA9mission de " + lic.licenseId + " publi\xC3\xA9" "e (" + plural(lic.devices.size(), "PC", "PC") + ", le PC retir\xC3\xA9 pr\xC3\xA9venu)."
                     : "Refus\xC3\xA9 : la nouvelle \xC3\xA9mission de " + lic.licenseId + " n'est publi\xC3\xA9" "e qu'en partie.";
    return res;
}

// ============================================================================ les fichiers des Studios
std::string serviceAddressHeader(const std::string& url) {
    // La meme forme que src/licensing/ServiceAddress.hpp (depose tel quel a sa place).
    return "// =============================================================================\n"
           "//  licensing/ServiceAddress.hpp - 1.35.0 : L'ADRESSE DU SERVICE DES LICENCES,\n"
           "//  compilee dans les logiciels des clients (API Studio, IHM Studio)\n"
           "// -----------------------------------------------------------------------------\n"
           "//  Le Worker Cloudflare de l'editeur (service/, docs/SERVICE-LICENCES.md) :\n"
           "//  \"https://xpg-licences.<son-compte>.workers.dev\". Ce n'est pas un secret.\n"
           "//\n"
           "//  VIDE : ce programme n'a pas de service - les licences passent par fichier\n"
           "//  seulement (la Decouverte marche quand meme, hors ligne).\n"
           "//\n"
           "//  Ecrit par l'outil de l'editeur (Parametres de securite > Preparer les\n"
           "//  fichiers pour les Studios), avec PublisherKey.hpp. L'option CMake\n"
           "//  XPG_SERVICE_URL (non vide) la remplace a la construction ; dans une\n"
           "//  construction d'essai, la variable d'environnement XPG_SERVICE aussi.\n"
           "// =============================================================================\n"
           "#pragma once\n"
           "\n"
           "namespace licensing::service {\n"
           "\n"
           "inline constexpr const char* kCompiledServiceUrl = \"" + url + "\";\n"
           "\n"
           "} // namespace licensing::service\n";
}

Result studioKit(const std::string& folder, const licensing::SigningKey* key, const std::string& outDir, Seconds now) {
    if (!key) return refused("pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste");
    if (key->test())
        return refused("c'est une cl\xC3\xA9 d'ESSAI : elle ne va jamais dans les Studios livr\xC3\xA9s (les licences qu'elle signe ne sont pas pour des clients). "
                       "Cr\xC3\xA9" "e la vraie cl\xC3\xA9 de l'\xC3\xA9" "diteur, ou restaure sa sauvegarde, puis recommence");
    if (trimmed(outDir).empty()) return refused("dans quel dossier \xC3\xA9" "crire les fichiers ?");
    std::string why, header, summary;
    if (!exportPublisherKeys(key, {}, kPublisherHeaderName, false, false, now, header, &why, &summary)) return refused(why);
    std::string registry;
    SignedBlock regBlock;
    if (!archiveSignedText(folder, "registre/registre.xpgreg", DocKind::Registry, *key, registry, regBlock, &why))
        return refused(why.empty() ? std::string("pas de registre sign\xC3\xA9 dans l'archive : signe-le d'abord (\xC3\x89" "ditions), puis recommence")
                                   : why + " : les Studios ne le croiraient pas. Signe un nouveau registre (\xC3\x89" "ditions), puis recommence");
    licensing::Registry reg;
    if (!licensing::parseRegistry(regBlock.document, reg, &why)) return refused(why);
    // L'adresse du service (vide : pas de service).
    Config c;
    std::string url;
    if (configured(folder)) {
        if (!readConfig(folder, c, &why)) return refused(why);
        url = c.url;
    }
    if (url.rfind("http://", 0) == 0)
        return refused("l'adresse r\xC3\xA9gl\xC3\xA9" "e (" + url + ") est celle d'un service d'essai sur ce poste : les Studios livr\xC3\xA9s demandent https://. "
                       "R\xC3\xA8gle l'adresse de ton vrai service (Service en ligne), puis recommence");
    const std::string address = serviceAddressHeader(url);
    const std::string publicKey = key->publicKeyHex() + "\n";
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
        "        le registre sign\xC3\xA9 n\xC2\xB0 " + std::to_string(reg.number) + " (les \xC3\xA9" "ditions, dont celle de la D\xC3\xA9" "couverte) : livr\xC3\xA9 avec l'installateur.\n"
        "  3. ServiceAddress.hpp   ->  src/licensing/ServiceAddress.hpp\n" +
        (url.empty() ? std::string("        AUCUN service r\xC3\xA9gl\xC3\xA9 : les licences passeront par fichier seulement.\n")
                     : "        le service des licences " + url + " : activation par code, licences flottantes, heure sign\xC3\xA9" "e.\n") +
        "  4. cle-publique.txt     ->  pour la mise en ligne du service (outils/service : PUBLISHER_KEYS du Worker)\n"
        "        la m\xC3\xAAme cl\xC3\xA9 publique, en hexad\xC3\xA9" "cimal : le service croit les appels sign\xC3\xA9s par elle.\n"
        "\n"
        "Si tu changes un jour de cl\xC3\xA9, exporte-la plut\xC3\xB4t avec \xC2\xAB Exporter la cl\xC3\xA9 publique\xE2\x80\xA6 \xC2\xBB en cochant\n"
        "\xC2\xAB garder les cl\xC3\xA9s d\xC3\xA9j\xC3\xA0 dans le fichier \xC2\xBB : les licences sign\xC3\xA9" "es avec l'ancienne restent valables.\n"
        "\n"
        "NE METS JAMAIS avec eux : la cl\xC3\xA9 priv\xC3\xA9" "e (cle-editeur-licences.cle), sa sauvegarde (.xpgcle), la cl\xC3\xA9 de\n"
        "synchro et son code de secours, un code d'activation.\n";
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
    } files[] = {{kPublisherHeaderName, header}, {"registre.xpgreg", registry}, {"ServiceAddress.hpp", address}, {"cle-publique.txt", publicKey},
                 {"LISEZ-MOI.txt", crlf}};
    Result res;
    for (const auto& f : files) {
        const std::string path = U(P(outDir) / P(f.name));
        if (!crypto::replaceFile(path, f.text, false, &why)) return refused(why);
    }
    res.ok = true;
    res.message = "Fichiers pour les Studios \xC3\xA9" "crits dans " + outDir + " : PublisherKey.hpp, registre.xpgreg, ServiceAddress.hpp, cle-publique.txt (et LISEZ-MOI.txt).";
    res.details.push_back("Cl\xC3\xA9 publique : " + key->keyId() + " -> src/licensing/PublisherKey.hpp (et cle-publique.txt pour le service)");
    res.details.push_back("Registre n\xC2\xB0 " + std::to_string(reg.number) + " -> ressources-licences/registre.xpgreg");
    if (url.empty())
        res.details.push_back("ATTENTION : aucun service r\xC3\xA9gl\xC3\xA9 : ServiceAddress.hpp est vide (les licences passeront par fichier). R\xC3\xA8gle-le dans "
                              "Service en ligne si tu le veux.");
    else
        res.details.push_back("Service : " + url + " -> src/licensing/ServiceAddress.hpp");
    res.details.push_back("Donne-les au d\xC3\xA9veloppeur (ou commite-les \xC3\xA0 ces chemins), puis reconstruis les Studios. Rien de secret dedans.");
    return res;
}

} // namespace license_admin::online
