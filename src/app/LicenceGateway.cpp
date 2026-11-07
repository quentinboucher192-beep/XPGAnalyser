// =============================================================================
//  app/LicenceGateway.cpp - 1.34.0 : la demande en ligne, le cote des Studios
//  de la passerelle GitHub (voir LicenceGateway.hpp)
// =============================================================================
#include "LicenceGateway.hpp"

#include "Licence.hpp"
#include "ai/AiHttp.hpp"

#include "../core/Edition.hpp"
#include "../core/Json.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace app::licence::gateway {

namespace {

namespace lic = licensing;
namespace lgw = licensing::gateway;
namespace json = core::json;
namespace crypto = deploy::crypto;

constexpr std::size_t kMaxBody = 4u * 1024u * 1024u;   // un etat de PC porte une licence scellee (moins de 1 Mo)

// ============================================================ l'adresse ==
std::mutex& addressMutex() {
    static std::mutex m;
    return m;
}
std::optional<lgw::Address>& addressOverride() {
    static std::optional<lgw::Address> a;
    return a;
}

// ============================================================ le transport ==
//  La requete en cours (une a la fois : le fil de fond) ; shutdown() l'arrete.
std::mutex& httpMutex() {
    static std::mutex m;
    return m;
}
ai::HttpClient*   g_active = nullptr;
std::atomic<bool> g_stopping{false};

struct Got {
    ai::HttpResponse::Failure failure{ai::HttpResponse::Failure::None};
    std::string               error;     // la panne, en francais
    int                       status{0};
    std::string               body;
    bool                      tooBig{false};
    std::string               remaining;   // x-ratelimit-remaining
    [[nodiscard]] bool reached() const noexcept { return failure == ai::HttpResponse::Failure::None && !tooBig; }
};

std::string hostOf(const std::string& url) {
    ai::Url u;
    if (ai::parseUrl(url, u)) return u.host;
    return "GitHub";
}

std::string userAgent() { return std::string("XPG-Studio/") + XPG_ANALYZER_VERSION; }

Got perform(const ai::HttpRequest& req) {
    Got g;
    auto client = ai::makeHttpClient();
    {
        std::lock_guard<std::mutex> lock(httpMutex());
        g_active = client.get();
    }
    // Apres l'inscription : un shutdown() commence avant elle est vu ici, un shutdown() apres l'arrete.
    if (g_stopping) {
        std::lock_guard<std::mutex> lock(httpMutex());
        g_active = nullptr;
        g.failure = ai::HttpResponse::Failure::Cancelled;
        g.error = "Arr\xC3\xAAt\xC3\xA9" "e : le programme se ferme.";
        return g;
    }
    ai::HttpSink sink;
    sink.onData = [&g](std::string_view chunk) {
        if (g.body.size() + chunk.size() > kMaxBody) g.tooBig = true;
        else g.body.append(chunk);
    };
    const ai::HttpResponse r = client->perform(req, sink);
    {
        std::lock_guard<std::mutex> lock(httpMutex());
        g_active = nullptr;
    }
    g.failure = r.failure;
    g.status = r.status;
    g.remaining = r.header("x-ratelimit-remaining");
    if (!r.ok()) g.error = r.error.empty() ? ai::failureText(r.failure, hostOf(req.url)) : r.error;
    if (g.tooBig) g.error = "la r\xC3\xA9ponse de " + hostOf(req.url) + " est trop grosse";
    return g;
}

// Un fichier du depot, SANS jeton (raw).
Got getRaw(const lgw::Address& a, std::string_view path) {
    ai::HttpRequest req;
    req.method = "GET";
    req.url = lgw::rawUrl(a, path);
    req.headers = {{"User-Agent", userAgent()}, {"Cache-Control", "no-cache"}};
    req.connectTimeoutSeconds = 15;
    req.idleTimeoutSeconds = 30;
    return perform(req);
}

// L'API des issues, AVEC le jeton des demandes : il ne va que dans l'en-tete Authorization.
Got callApi(const lgw::Address& a, const std::string& method, std::string_view tail, const json::Value& body, const std::string& token) {
    ai::HttpRequest req;
    req.method = method;
    req.url = lgw::apiUrl(a, tail);
    req.headers = {{"Authorization", "Bearer " + token},
                   {"Accept", "application/vnd.github+json"},
                   {"X-GitHub-Api-Version", "2022-11-28"},
                   {"User-Agent", userAgent()},
                   {"Content-Type", "application/json"}};
    req.body = json::write(body, false);
    req.connectTimeoutSeconds = 15;
    req.idleTimeoutSeconds = 30;
    Got g = perform(req);
    for (auto& h : req.headers) crypto::wipe(h.second.data(), h.second.size());   // le jeton ne reste pas en memoire
    return g;
}

bool rateLimited(const Got& g) { return g.status == 429 || (g.status == 403 && g.remaining == "0"); }

// Le message d'une reponse GitHub ({"message": "..."}), court ; jamais une adresse ni un en-tete.
std::string githubMessage(const Got& g) {
    json::Value v;
    if (!json::parse(g.body, v) || !v.isObject()) return {};
    std::string m = v.str("message");
    if (m.size() > 160) m = m.substr(0, 160) + "\xE2\x80\xA6";
    return m;
}

// Une phrase finit par un point.
std::string sentence(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;
}

// Ce qui n'a pas marche, en une phrase (`what` : ce qu'on faisait).
std::string problem(const Got& g, const std::string& what) {
    if (g.failure != ai::HttpResponse::Failure::None) return sentence(what + " : " + g.error);
    if (g.tooBig) return what + " : " + g.error + ".";
    if (rateLimited(g))
        return what + " : GitHub limite les requ\xC3\xAAtes pour l'instant (trop de demandes depuis ce r\xC3\xA9seau). R\xC3\xA9" "essaie dans une heure.";
    const std::string m = githubMessage(g);
    return what + " : GitHub r\xC3\xA9pond " + std::to_string(g.status) + (m.empty() ? std::string() : " (" + m + ")") + ".";
}

// ============================================================ verifier ==
const lic::TrustedKey* findKey(const std::vector<lic::TrustedKey>& keys, std::string_view id) {
    for (const auto& k : keys)
        if (k.id == id) return &k;
    return nullptr;
}

// Un seul bloc de ce genre, signe par une cle de l'editeur que ce programme croit.
bool verifySigned(std::string_view bytes, lic::DocKind kind, const std::vector<lic::TrustedKey>& keys, lic::SignedBlock& out,
                  const lic::TrustedKey*& signer, std::string& why) {
    std::vector<lic::SignedBlock> blocks;
    std::string w;
    if (!lic::readBlocks(bytes, blocks, &w)) {
        why = "le fichier est illisible (" + w + ")";
        return false;
    }
    if (blocks.size() != 1 || blocks[0].kind != kind) {
        why = "le fichier n'est pas un seul bloc \xC2\xAB " + std::string(lic::docKindKey(kind)) + " \xC2\xBB";
        return false;
    }
    signer = findKey(keys, blocks[0].keyId);
    if (!signer) {
        why = "le fichier est sign\xC3\xA9 par une cl\xC3\xA9 que ce programme ne conna\xC3\xAEt pas (" + blocks[0].keyId + ")";
        return false;
    }
    if (!lic::verifyBlock(blocks[0], signer->key)) {
        why = "la signature du fichier est fausse";
        return false;
    }
    out = std::move(blocks[0]);
    return true;
}

struct VerifiedConfig {
    lgw::Config     config;
    lic::TrustedKey signer;
};

// La configuration publiee : signee par l'editeur, pour CE depot, pas plus ancienne que la derniere vue.
bool fetchConfig(const lgw::Address& a, const Record& rec, VerifiedConfig& out, std::string& why) {
    const Got g = getRaw(a, lgw::kConfigPath);
    if (!g.reached() || (g.status != 200 && g.status != 404)) {
        why = problem(g, "La passerelle n'a pas pu \xC3\xAAtre lue");
        return false;
    }
    if (g.status == 404) {
        why = "La passerelle n'a pas encore de configuration publi\xC3\xA9" "e par l'\xC3\xA9" "diteur.";
        return false;
    }
    const auto keys = lic::trustedPublisherKeys();
    lic::SignedBlock block;
    const lic::TrustedKey* signer = nullptr;
    std::string w;
    if (!verifySigned(g.body, lic::DocKind::Gateway, keys, block, signer, w)) {
        why = "La configuration de la passerelle est refus\xC3\xA9" "e : " + w + ".";
        return false;
    }
    lgw::Config c;
    if (!lgw::parseConfig(block.document, c, &w)) {
        why = "La configuration de la passerelle est refus\xC3\xA9" "e : " + w + ".";
        return false;
    }
    if (c.repo != a.repo) {
        crypto::wipe(c.requestToken.data(), c.requestToken.size());
        why = "La configuration de la passerelle est refus\xC3\xA9" "e : elle vaut pour un autre d\xC3\xA9p\xC3\xB4t (" + c.repo + ").";
        return false;
    }
    if (c.number < rec.configNumber) {
        crypto::wipe(c.requestToken.data(), c.requestToken.size());
        why = "La configuration de la passerelle est refus\xC3\xA9" "e : elle est plus ancienne (n\xC2\xB0 " + std::to_string(c.number) +
              ") que la derni\xC3\xA8re vue (n\xC2\xB0 " + std::to_string(rec.configNumber) + ").";
        return false;
    }
    out.config = std::move(c);
    out.signer = *signer;
    return true;
}

void forget(VerifiedConfig& v) {
    crypto::wipe(v.config.requestToken.data(), v.config.requestToken.size());
    v.config.requestToken.clear();
}

// L'identifiant de ce poste. `create` : la cle du PC est creee si elle manque (une demande) ; sinon, sans cle,
// faux et `why` vide (ce PC n'a encore rien demande).
bool myDevice(std::string& id, bool create, std::string& why) {
    why.clear();
    if (!create && lic::thisDeviceFingerprint().empty()) return false;
    std::string hex;
    if (!lic::deviceKeyHex(hex, &why)) {
        why = "La cl\xC3\xA9 de ce PC n'est pas lisible : " + why;
        return false;
    }
    crypto::Bytes b;
    crypto::PublicKey pk{};
    if (!crypto::unhex(hex, b, pk.size())) {
        why = "La cl\xC3\xA9 de ce PC est mal \xC3\xA9" "crite.";
        return false;
    }
    std::copy(b.begin(), b.end(), pk.begin());
    id = lgw::deviceId(pk);
    return true;
}

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void note(bool ok, const std::string& text) {
    lic::HistoryEvent e;
    e.at = lic::systemNow();
    e.kind = "passerelle";
    e.ok = ok;
    e.text = text;
    std::string why;
    (void)lic::addHistoryEvent(e, &why);   // un dossier ou ce compte n'ecrit pas : le message l'a deja dit
}

std::string software() { return core::productName() + " " + XPG_ANALYZER_VERSION; }

// Un document plus recent que celui du PC (registre, revocations) : importe ; sinon rien (pas de bruit dans
// l'historique). `line` : ce qui s'est passe ("" : rien de nouveau). Faux : la passerelle n'a pas repondu.
bool adoptIfNewer(const lgw::Address& a, std::string_view path, lic::DocKind kind, std::uint64_t current, std::string& line, bool& changed,
                  std::string& why) {
    line.clear();
    const Got g = getRaw(a, path);
    if (g.reached() && g.status == 404) return true;
    if (!g.reached() || g.status != 200) {
        why = problem(g, kind == lic::DocKind::Registry ? "Le registre n'a pas pu \xC3\xAAtre lu" : "La liste de r\xC3\xA9vocation n'a pas pu \xC3\xAAtre lue");
        return false;
    }
    std::vector<lic::SignedBlock> blocks;
    std::string w;
    std::uint64_t number = 0;
    if (lic::readBlocks(g.body, blocks, &w) && blocks.size() == 1 && blocks[0].kind == kind) {
        if (kind == lic::DocKind::Registry) {
            lic::Registry r;
            if (lic::parseRegistry(blocks[0].document, r, &w)) number = r.number;
        } else {
            lic::RevocationList r;
            if (lic::parseRevocations(blocks[0].document, r, &w)) number = r.number;
        }
    }
    const std::string label = kind == lic::DocKind::Registry ? "Le registre publi\xC3\xA9" : "La liste de r\xC3\xA9vocation publi\xC3\xA9" "e";
    if (number == 0) {
        line = label + " est illisible" + (w.empty() ? std::string(".") : " (" + w + ").");
        return true;
    }
    if (number <= current) return true;
    const auto rep = lic::importLicenseBytes(g.body);
    if (rep.ok) {
        changed = true;
        line = label + " n\xC2\xB0 " + std::to_string(number) + " est install\xC3\xA9" + (kind == lic::DocKind::Registry ? "." : "e.");
    } else {
        line = label + " n\xC2\xB0 " + std::to_string(number) + " est refus\xC3\xA9" + (kind == lic::DocKind::Registry ? "" : "e") + " : " + rep.why;
    }
    return true;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += ' ';
        out += p;
    }
    return out;
}

// La reponse de l'editeur concerne-t-elle la demande en cours ?
bool answersCurrent(const Record& rec, const lgw::DeviceStatus& s) {
    return rec.local == kLocalSent && !rec.nonce.empty() && (s.requestNonce == rec.nonce || s.requestNonce.empty());
}

} // namespace

// ============================================================ l'adresse ==
void setAddressForTests(std::optional<lgw::Address> a) {
    std::lock_guard<std::mutex> lock(addressMutex());
    addressOverride() = std::move(a);
}

lgw::Address address() {
    {
        std::lock_guard<std::mutex> lock(addressMutex());
        if (addressOverride()) return *addressOverride();
    }
    return lgw::compiledAddress();
}

bool available(std::string* why) {
    if (address().empty()) {
        if (why) *why = "Ce programme n'a pas de passerelle en ligne : la demande et la licence passent par fichier.";
        return false;
    }
    if (!lic::isConfigured()) {
        if (why) *why = "Les licences ne sont pas activ\xC3\xA9" "es dans ce programme : il n'y a rien \xC3\xA0 demander.";
        return false;
    }
    if (why) why->clear();
    return true;
}

// ============================================================ le releve ==
std::string recordPath() { return deploy::joinPath(lic::getLicenseFolder(), kRecordFile); }

bool loadRecord(Record& out, std::string* why) {
    out = Record{};
    const std::string path = recordPath();
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec)) return true;
    crypto::Bytes bytes;
    std::string w;
    if (!crypto::readFile(path, bytes, &w, 1024u * 1024u)) {
        if (why) *why = "Le relev\xC3\xA9 de la demande en ligne ne se lit pas : " + w;
        return false;
    }
    json::Value v;
    if (!json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), v, &w) || !v.isObject()) {
        if (why) *why = "Le relev\xC3\xA9 de la demande en ligne est illisible (" + recordPath() + ").";
        return false;
    }
    Record r;
    r.issue = static_cast<int>(v.integer("issue"));
    r.device = v.str("poste");
    r.nonce = v.str("demande");
    lic::Seconds t = 0;
    if (lic::parseIsoTime(v.str("envoyee_le"), t)) r.sentAt = t;
    r.customer = v.str("client");
    r.contact = v.str("contact");
    r.message = v.str("message");
    r.pcName = v.str("nom_pc");
    r.licenseId = v.str("licence_demandee");
    r.local = v.str("etat_local");
    if (lic::parseIsoTime(v.str("annulee_le"), t)) r.cancelledAt = t;
    r.outcome = v.str("reponse");
    const double n = v.num("dernier_numero");
    r.lastNumber = n > 0 ? static_cast<std::uint64_t>(n) : 0;
    r.lastState = v.str("dernier_etat");
    r.lastNonce = v.str("derniere_demande");
    if (lic::parseIsoTime(v.str("derniere_verification"), t)) r.lastCheck = t;
    r.lastMessage = v.str("dernier_message");
    r.receivedLicense = v.str("licence_recue");
    const double c = v.num("configuration");
    r.configNumber = c > 0 ? static_cast<std::uint64_t>(c) : 0;
    if (r.local != kLocalSent && r.local != kLocalCancelled) r.local.clear();
    out = std::move(r);
    return true;
}

bool saveRecord(const Record& r, std::string* why) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    if (r.issue > 0) v.set("issue", r.issue);
    if (!r.device.empty()) v.set("poste", r.device);
    if (!r.nonce.empty()) v.set("demande", r.nonce);
    if (r.sentAt) v.set("envoyee_le", lic::isoTime(r.sentAt));
    if (!r.customer.empty()) v.set("client", r.customer);
    if (!r.contact.empty()) v.set("contact", r.contact);
    if (!r.message.empty()) v.set("message", r.message);
    if (!r.pcName.empty()) v.set("nom_pc", r.pcName);
    if (!r.licenseId.empty()) v.set("licence_demandee", r.licenseId);
    if (!r.local.empty()) v.set("etat_local", r.local);
    if (r.cancelledAt) v.set("annulee_le", lic::isoTime(r.cancelledAt));
    if (!r.outcome.empty()) v.set("reponse", r.outcome);
    if (r.lastNumber) v.set("dernier_numero", r.lastNumber);
    if (!r.lastState.empty()) v.set("dernier_etat", r.lastState);
    if (!r.lastNonce.empty()) v.set("derniere_demande", r.lastNonce);
    if (r.lastCheck) v.set("derniere_verification", lic::isoTime(r.lastCheck));
    if (!r.lastMessage.empty()) v.set("dernier_message", r.lastMessage);
    if (!r.receivedLicense.empty()) v.set("licence_recue", r.receivedLicense);
    if (r.configNumber) v.set("configuration", r.configNumber);
    std::string w;
    if (!crypto::replaceFile(recordPath(), json::write(v) + "\n", false, &w)) {
        if (why) *why = "Le relev\xC3\xA9 de la demande en ligne n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + w;
        return false;
    }
    return true;
}

bool pending(const Record& r) noexcept { return r.issue > 0 && r.local == kLocalSent && r.outcome.empty(); }

std::string dateText(lic::Seconds at) {
    if (at <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(at);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
}

std::string stateText(const Record& r) {
    if (r.issue <= 0 || r.local.empty()) return "Aucune demande en ligne";
    if (r.local == kLocalCancelled) return "Annul\xC3\xA9" "e" + (r.cancelledAt ? " le " + dateText(r.cancelledAt) : std::string());
    if (r.outcome == "acceptee") return "Accept\xC3\xA9" "e par l'\xC3\xA9" "diteur";
    if (r.outcome == "refusee") return "Refus\xC3\xA9" "e par l'\xC3\xA9" "diteur";
    if (r.outcome == "annulee") return "Annul\xC3\xA9" "e (l'\xC3\xA9" "diteur l'a not\xC3\xA9)";
    if (r.lastState == "recue" && (r.lastNonce == r.nonce || r.lastNonce.empty()))
        return "Re\xC3\xA7ue par l'\xC3\xA9" "diteur \xE2\x80\x94 en attente de sa d\xC3\xA9" "cision";
    return "Envoy\xC3\xA9" "e \xE2\x80\x94 en attente de l'\xC3\xA9" "diteur";
}

// ============================================================ verifier ==
Result check() {
    Result res;
    res.job = Job::Check;
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    const lgw::Address a = address();
    Record rec;
    if (!loadRecord(rec, &why)) {
        res.message = why;
        return res;
    }
    res.pendingAfter = pending(rec);
    VerifiedConfig cfg;
    if (!fetchConfig(a, rec, cfg, why)) {
        res.message = why + " Rien n'a chang\xC3\xA9.";
        return res;
    }
    forget(cfg);   // la verification ne demande rien : le jeton ne sert pas
    bool recChanged = cfg.config.number > rec.configNumber;
    rec.configNumber = std::max(rec.configNumber, cfg.config.number);

    // Le registre et la liste de revocation en vigueur.
    std::vector<std::string> lines;
    {
        const auto st = lic::getLicenseState();
        std::string line;
        if (!adoptIfNewer(a, lgw::kRegistryPath, lic::DocKind::Registry, st.registryNumber, line, res.changed, why)) {
            res.message = why + " Rien n'a chang\xC3\xA9.";
            return res;
        }
        lines.push_back(line);
        const auto st2 = lic::getLicenseState();
        if (!adoptIfNewer(a, lgw::kRevocationsPath, lic::DocKind::Revocations, st2.revocationNumber, line, res.changed, why)) {
            res.message = join({join(lines), why});
            return res;
        }
        lines.push_back(line);
    }

    // L'etat de CE PC.
    std::string id;
    if (!myDevice(id, false, why)) {
        if (!why.empty()) {
            res.message = join({join(lines), why});
            return res;
        }
        rec.lastCheck = lic::systemNow();
        (void)saveRecord(rec, nullptr);
        res.ok = true;
        res.message = join({join(lines), "Rien de nouveau : ce PC n'a encore fait aucune demande."});
        return res;
    }
    const Got g = getRaw(a, lgw::statusPath(id));
    if (!g.reached() || (g.status != 200 && g.status != 404)) {
        res.message = join({join(lines), problem(g, "L'\xC3\xA9tat de ce PC n'a pas pu \xC3\xAAtre lu")});
        return res;
    }
    rec.lastCheck = lic::systemNow();
    if (g.status == 404) {
        res.ok = true;
        res.message = join({join(lines), pending(rec) ? "Rien de nouveau : ta demande attend que l'\xC3\xA9" "diteur la voie."
                                                      : "Rien de nouveau de l'\xC3\xA9" "diteur pour ce PC."});
        if (!saveRecord(rec, &why)) res.message += " " + why;
        return res;
    }
    lgw::DeviceStatus s;
    {
        const auto keys = lic::trustedPublisherKeys();
        lic::SignedBlock block;
        const lic::TrustedKey* signer = nullptr;
        std::string w;
        if (!verifySigned(g.body, lic::DocKind::DeviceStatus, keys, block, signer, w) || !lgw::parseDeviceStatus(block.document, s, &w)) {
            res.message = join({join(lines), "L'\xC3\xA9tat publi\xC3\xA9 pour ce PC est refus\xC3\xA9 : " + w + ". Rien n'a chang\xC3\xA9."});
            return res;
        }
        if (s.device != id) {
            res.message = join({join(lines), "L'\xC3\xA9tat publi\xC3\xA9 est refus\xC3\xA9 : il est pour un autre poste (" + s.device.substr(0, 12) +
                                                 "\xE2\x80\xA6). Rien n'a chang\xC3\xA9."});
            return res;
        }
    }
    if (s.number <= rec.lastNumber) {
        res.ok = true;
        res.message = join({join(lines), s.number == rec.lastNumber
                                             ? std::string("Rien de nouveau de l'\xC3\xA9" "diteur pour ce PC.")
                                             : "Un \xC3\xA9tat plus ancien (n\xC2\xB0 " + std::to_string(s.number) + ", d\xC3\xA9j\xC3\xA0 vu le n\xC2\xB0 " +
                                                   std::to_string(rec.lastNumber) + ") est ignor\xC3\xA9."});
        if (!saveRecord(rec, &why)) res.message += " " + why;
        return res;
    }

    // Un etat nouveau : ce que seul ce PC lit (scelle pour sa cle).
    lgw::StatusContent content;
    std::string sealProblem;
    if (!s.sealed.empty()) {
        std::string plain, w;
        if (!lic::deviceOpenSeal(s.sealed, plain, &w) || !lgw::parseStatusContent(plain, content, &w)) sealProblem = w;
    }
    const bool current = answersCurrent(rec, s);
    std::string text;
    bool ok = true;
    switch (s.state) {
        case lgw::State::Received:
            text = current ? "Ta demande est re\xC3\xA7ue par l'\xC3\xA9" "diteur : elle attend sa d\xC3\xA9" "cision."
                           : "L'\xC3\xA9" "diteur a re\xC3\xA7u une demande de ce PC.";
            break;
        case lgw::State::Accepted:
        case lgw::State::Updated: {
            const bool updated = s.state == lgw::State::Updated;
            if (!sealProblem.empty()) {
                ok = false;
                text = "L'\xC3\xA9" "diteur a publi\xC3\xA9 une licence pour ce PC, mais elle ne s'ouvre pas : " + sealProblem + ".";
                res.notice = text;
                res.noticeTone = "error";
            } else if (content.license.empty()) {
                ok = false;
                text = "L'\xC3\xA9" "diteur a " + std::string(updated ? "mis \xC3\xA0 jour" : "accept\xC3\xA9") + " la demande, mais l'\xC3\xA9tat ne contient pas de licence.";
            } else {
                const auto rep = lic::importLicenseBytes(content.license);
                const std::string lid = !rep.state.licenseId.empty() ? rep.state.licenseId : s.licenseId;
                res.licenseId = lid;
                if (rep.licenseInstalled) {
                    res.licenseInstalled = true;
                    res.changed = true;
                    rec.receivedLicense = lid;
                    text = updated ? "Ta licence " + lid + " est mise \xC3\xA0 jour (re\xC3\xA7ue de l'\xC3\xA9" "diteur)."
                                   : "Ta licence " + lid + " est install\xC3\xA9" "e (re\xC3\xA7ue de l'\xC3\xA9" "diteur).";
                    res.notice = text;
                    res.noticeTone = "ok";
                } else if (rep.ok) {
                    text = "Ta licence " + lid + " est d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e.";
                } else {
                    ok = false;
                    text = "La licence re\xC3\xA7ue de l'\xC3\xA9" "diteur n'est pas install\xC3\xA9" "e : " + rep.why;
                    res.notice = text;
                    res.noticeTone = "error";
                }
            }
            if (!content.message.empty()) text += " Mot de l'\xC3\xA9" "diteur : " + content.message;
            if (current && s.state == lgw::State::Accepted) rec.outcome = "acceptee";
            if (current && updated && res.licenseInstalled) rec.outcome = "acceptee";
            break;
        }
        case lgw::State::Refused: {
            std::string reason = trim(content.message);
            if (!sealProblem.empty()) reason = "(la raison ne s'ouvre pas : " + sealProblem + ")";
            text = reason.empty() ? std::string("Ta demande d'activation a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e (sans raison donn\xC3\xA9" "e).")
                                  : "Ta demande d'activation a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e : " + reason + (reason.back() == '.' ? "" : ".");
            ok = false;
            res.refused = true;
            res.notice = text;
            res.noticeTone = "warning";
            if (current) rec.outcome = "refusee";
            break;
        }
        case lgw::State::Cancelled:
            text = "L'\xC3\xA9" "diteur a pris note de l'annulation de ta demande.";
            if (current) rec.outcome = "annulee";
            break;
        case lgw::State::Revoked:
            text = "L'\xC3\xA9" "diteur a r\xC3\xA9voqu\xC3\xA9 la licence " + (s.licenseId.empty() ? std::string("de ce PC") : s.licenseId) + ".";
            if (!content.message.empty()) text += " " + content.message;
            ok = false;
            res.notice = text;
            res.noticeTone = "error";
            res.changed = true;
            break;
        case lgw::State::Removed:
            text = "Ce PC a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 de la licence " + (s.licenseId.empty() ? std::string() : s.licenseId + " ") + "par l'\xC3\xA9" "diteur.";
            if (!content.message.empty()) text += " " + content.message;
            ok = false;
            res.notice = text;
            res.noticeTone = "warning";
            res.changed = true;
            break;
    }
    rec.lastNumber = s.number;
    rec.lastState = lgw::stateKey(s.state);
    rec.lastNonce = s.requestNonce;
    rec.lastMessage = text;
    recChanged = true;
    note(ok, text + " (\xC3\xA9tat n\xC2\xB0 " + std::to_string(s.number) + " de la passerelle)");
    res.ok = true;
    res.changed = res.changed || recChanged;
    res.message = join({join(lines), text});
    if (!saveRecord(rec, &why)) res.message += " " + why;
    res.pendingAfter = pending(rec);
    return res;
}

// ============================================================ envoyer ==
Result send(const SendForm& form0) {
    Result res;
    res.job = Job::Send;
    SendForm f;
    f.customer = trim(form0.customer);
    f.contact = trim(form0.contact);
    f.pcName = trim(form0.pcName);
    f.licenseId = trim(form0.licenseId);
    f.message = trim(form0.message);
    if (f.customer.empty()) {
        res.message = "Indique le nom de ta soci\xC3\xA9t\xC3\xA9 (ou le tien) : l'\xC3\xA9" "diteur en a besoin pour \xC3\xA9tablir la licence.";
        return res;
    }
    if (f.customer.size() > kMaxCustomer || f.contact.size() > kMaxContact || f.message.size() > kMaxMessage || f.pcName.size() > kMaxPcName ||
        f.licenseId.size() > kMaxLicenseId) {
        res.message = "Un champ est trop long (soci\xC3\xA9t\xC3\xA9 et contact : " + std::to_string(kMaxCustomer) + " caract\xC3\xA8res, nom du PC : " +
                      std::to_string(kMaxPcName) + ", message : " + std::to_string(kMaxMessage) + ").";
        return res;
    }
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    Record old;
    if (!loadRecord(old, &why)) {
        res.message = why;
        return res;
    }
    if (pending(old)) {
        res.pendingAfter = true;
        res.message = "Une demande est d\xC3\xA9j\xC3\xA0 en cours pour ce PC (n\xC2\xB0 " + std::to_string(old.issue) + ", envoy\xC3\xA9" "e le " + dateText(old.sentAt) +
                      ") : une seule \xC3\xA0 la fois. Annule-la d'abord, ou attends la r\xC3\xA9ponse de l'\xC3\xA9" "diteur.";
        return res;
    }
    const lgw::Address a = address();
    VerifiedConfig cfg;
    if (!fetchConfig(a, old, cfg, why)) {
        res.message = why + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    std::string block;
    lic::ActivationRequest parsed;
    if (!lic::activationRequestText(f.pcName, f.licenseId, software(), block, &parsed, &why)) {
        forget(cfg);
        res.message = "La demande de ce PC n'a pas pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e : " + why + ". Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    std::string id;
    if (!myDevice(id, true, why)) {
        forget(cfg);
        res.message = why + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    lgw::OnlineRequest q;
    q.request = block;
    q.customer = f.customer;
    q.contact = f.contact;
    q.message = f.message;
    // Scellee pour la cle de l'editeur qui a signe la configuration : lui seul la lit.
    const std::string sealed = lgw::sealFor(cfg.signer.key, lgw::writeOnlineRequest(q));
    if (sealed.empty()) {
        forget(cfg);
        res.message = "La demande n'a pas pu \xC3\xAAtre scell\xC3\xA9" "e pour l'\xC3\xA9" "diteur. Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    json::Value body = json::Value::object();
    body.set("title", lgw::issueTitle(id));
    body.set("body", lgw::issueBody(id, cfg.signer.id, sealed));
    const Got g = callApi(a, "POST", "/issues", body, cfg.config.requestToken);
    forget(cfg);
    if (!g.reached() || g.status != 201) {
        if (g.reached() && g.status == 401)
            res.message = "GitHub refuse le jeton des demandes de la passerelle (p\xC3\xA9rim\xC3\xA9 ?) : l'\xC3\xA9" "diteur doit publier une nouvelle configuration. "
                          "Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9 ; en attendant, la demande par fichier marche.";
        else res.message = problem(g, "La demande n'a pas pu \xC3\xAAtre envoy\xC3\xA9" "e") + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    json::Value answer;
    const int number = json::parse(g.body, answer) && answer.isObject() ? static_cast<int>(answer.integer("number")) : 0;
    if (number <= 0) {
        res.message = "GitHub a re\xC3\xA7u la demande mais sa r\xC3\xA9ponse est illisible (pas de num\xC3\xA9ro) : v\xC3\xA9rifie plus tard, ou pr\xC3\xA9viens l'\xC3\xA9" "diteur.";
        return res;
    }
    Record rec;
    rec.issue = number;
    rec.device = id;
    rec.nonce = parsed.nonce;
    rec.sentAt = lic::systemNow();
    rec.customer = f.customer;
    rec.contact = f.contact;
    rec.message = f.message;
    rec.pcName = f.pcName;
    rec.licenseId = f.licenseId;
    rec.local = kLocalSent;
    // Ce que la passerelle a deja dit de ce PC reste (un etat deja traite ne l'est pas deux fois).
    rec.lastNumber = old.lastNumber;
    rec.lastState = old.lastState;
    rec.lastNonce = old.lastNonce;
    rec.lastCheck = old.lastCheck;
    rec.receivedLicense = old.receivedLicense;
    rec.configNumber = std::max(old.configNumber, cfg.config.number);
    rec.lastMessage = "Demande envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(number) + ").";
    std::string what = "n\xC2\xB0 " + std::to_string(number);
    if (!f.pcName.empty()) what += ", PC \xC2\xAB " + f.pcName + " \xC2\xBB";
    if (!f.licenseId.empty()) what += ", pour la licence " + f.licenseId;
    note(true, "Demande d'activation envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur par la passerelle (" + what + ").");
    res.ok = true;
    res.changed = true;
    res.pendingAfter = true;
    res.message = "Demande envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(number) +
                  "). Ta licence s'installera d'elle-m\xC3\xAAme d\xC3\xA8s qu'il l'aura accept\xC3\xA9" "e.";
    if (!saveRecord(rec, &why)) res.message += " Attention : " + why;
    return res;
}

// ============================================================ annuler ==
Result cancel() {
    Result res;
    res.job = Job::Cancel;
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    Record rec;
    if (!loadRecord(rec, &why)) {
        res.message = why;
        return res;
    }
    if (!pending(rec)) {
        res.message = "Aucune demande en cours \xC3\xA0 annuler.";
        return res;
    }
    res.pendingAfter = true;
    std::string id;
    if (!myDevice(id, false, why)) {
        res.message = (why.empty() ? std::string("La cl\xC3\xA9 de ce PC manque.") : why) + " La demande n'est pas annul\xC3\xA9" "e.";
        return res;
    }
    lgw::Cancel c;
    c.device = id;
    c.requestNonce = rec.nonce;
    c.at = lic::systemNow();
    std::string blockText;
    std::vector<lic::SignedBlock> blocks;
    if (!lic::deviceSign(lic::DocKind::Cancel, lgw::writeCancel(c), blockText, &why) || !lic::readBlocks(blockText, blocks, &why) || blocks.size() != 1) {
        res.message = "L'annulation n'a pas pu \xC3\xAAtre sign\xC3\xA9" "e par ce PC : " + why + ". La demande n'est pas annul\xC3\xA9" "e.";
        return res;
    }
    const lgw::Address a = address();
    VerifiedConfig cfg;
    if (!fetchConfig(a, rec, cfg, why)) {
        res.message = why + " La demande n'est pas annul\xC3\xA9" "e.";
        return res;
    }
    json::Value comment = json::Value::object();
    comment.set("body", lgw::cancelComment(blocks[0]));
    const std::string tail = "/issues/" + std::to_string(rec.issue);
    Got g = callApi(a, "POST", tail + "/comments", comment, cfg.config.requestToken);
    if (!g.reached() || g.status != 201) {
        forget(cfg);
        res.message = problem(g, "L'annulation n'a pas pu \xC3\xAAtre envoy\xC3\xA9" "e") + " La demande n'est pas annul\xC3\xA9" "e.";
        return res;
    }
    json::Value close = json::Value::object();
    close.set("state", "closed");
    g = callApi(a, "PATCH", tail, close, cfg.config.requestToken);
    forget(cfg);
    if (!g.reached() || g.status != 200) {
        res.message = problem(g, "L'annulation est envoy\xC3\xA9" "e mais la demande n'a pas pu \xC3\xAAtre ferm\xC3\xA9" "e") + " R\xC3\xA9" "essaie.";
        return res;
    }
    rec.local = kLocalCancelled;
    rec.cancelledAt = c.at;
    rec.lastMessage = "Demande n\xC2\xB0 " + std::to_string(rec.issue) + " annul\xC3\xA9" "e.";
    note(true, "Demande d'activation n\xC2\xB0 " + std::to_string(rec.issue) + " annul\xC3\xA9" "e par ce PC (passerelle).");
    res.ok = true;
    res.changed = true;
    res.pendingAfter = false;
    res.message = "Demande annul\xC3\xA9" "e. Tu peux en envoyer une nouvelle.";
    if (!saveRecord(rec, &why)) res.message += " Attention : " + why;
    return res;
}

// ============================================================ le fil de fond ==
namespace {

double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct Runner {
    std::mutex         mutex;        // finished, result
    std::thread        worker;
    std::atomic<bool>  running{false};
    std::atomic<Job>   job{Job::None};
    bool               finished{false};
    Result             result;
    // le fil de l'interface seulement
    Result             last;
    std::atomic<unsigned long long> revision{0};
    bool               on{false};     // startRunner : la verification de l'heure
    bool               waiting{false};   // une demande attend (le dernier resultat le dit)
    double             nextCheck{0};

    ~Runner() {
        if (worker.joinable()) {
            g_stopping = true;
            {
                std::lock_guard<std::mutex> lock(httpMutex());
                if (g_active) g_active->cancel();
            }
            worker.join();
        }
    }
};

Runner& runner() {
    static Runner r;
    return r;
}

bool collect();

bool launch(Job kind, std::function<Result()> fn) {
    Runner& r = runner();
    (void)collect();
    if (r.running || g_stopping) return false;
    if (r.worker.joinable()) r.worker.join();
    r.running = true;
    r.job = kind;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.finished = false;
    }
    r.worker = std::thread([&r, fn = std::move(fn)] {
        Result out = fn();
        std::lock_guard<std::mutex> lock(r.mutex);
        r.result = std::move(out);
        r.finished = true;
    });
    return true;
}

} // namespace

void startRunner() {
    Runner& r = runner();
    r.on = true;
    r.nextCheck = steadyNow() + 3600.0;
    if (available()) (void)startCheck();
}

bool startCheck() {
    if (!available()) return false;
    return launch(Job::Check, [] { return check(); });
}

bool startSend(const SendForm& form) {
    return launch(Job::Send, [form] { return send(form); });
}

bool startCancel() {
    return launch(Job::Cancel, [] { return cancel(); });
}

bool busy() noexcept { return runner().running; }

Job busyJob() noexcept { return runner().running ? runner().job.load() : Job::None; }

namespace {

// Un travail fini : son resultat releve (le fil de l'interface). Faux : rien de fini.
bool collect() {
    Runner& r = runner();
    if (!r.running) return false;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (!r.finished) return false;
    }
    if (r.worker.joinable()) r.worker.join();
    Result res;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        res = std::move(r.result);
        r.finished = false;
    }
    r.running = false;
    r.job = Job::None;
    r.waiting = res.pendingAfter;
    r.nextCheck = steadyNow() + 3600.0;
    // Les avis : la cloche (la licence installee, un refus...) et la barre d'etat ; puis l'etat revalue.
    if (res.changed) licence::refresh();
    if (!res.notice.empty()) {
        licence::postNotice("licence:passerelle", res.notice, res.message, res.noticeTone.empty() ? "info" : res.noticeTone);
        licence::queueMessage(res.notice);
    }
    r.last = std::move(res);
    ++r.revision;
    return true;
}

} // namespace

void poll() {
    Runner& r = runner();
    if (collect() || r.running) return;
    if (r.on && r.waiting && steadyNow() >= r.nextCheck) {
        r.nextCheck = steadyNow() + 3600.0;
        (void)startCheck();
    }
}

Result lastResult() { return runner().last; }

unsigned long long revision() noexcept { return runner().revision.load(); }

void shutdown() {
    Runner& r = runner();
    r.on = false;
    g_stopping = true;
    {
        std::lock_guard<std::mutex> lock(httpMutex());
        if (g_active) g_active->cancel();
    }
    if (r.worker.joinable()) r.worker.join();
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.finished = false;
    }
    r.running = false;
    r.job = Job::None;
    g_stopping = false;
}

} // namespace app::licence::gateway
