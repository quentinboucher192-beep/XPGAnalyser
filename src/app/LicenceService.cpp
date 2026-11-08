// =============================================================================
//  app/LicenceService.cpp - 1.35.0 : le client du service des licences (voir
//  LicenceService.hpp ; le contrat : docs/SERVICE-LICENCES.md)
// =============================================================================
#include "LicenceService.hpp"

#include "BackgroundTasks.hpp"
#include "Licence.hpp"
#include "ai/AiHttp.hpp"

#include "../core/Edition.hpp"
#include "../core/Json.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Gateway.hpp"
#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace app::licence::service {

namespace {

namespace lic = licensing;
namespace lsv = licensing::service;
namespace lgw = licensing::gateway;
namespace json = core::json;
namespace crypto = deploy::crypto;

constexpr std::size_t kMaxBody = 4u * 1024u * 1024u;   // une reponse porte des licences et des blocs signes (moins de 1 Mo)

// ============================================================ l'adresse ==
std::mutex& addressMutex() {
    static std::mutex m;
    return m;
}
std::optional<lsv::Address>& addressOverride() {
    static std::optional<lsv::Address> a;
    return a;
}

// ============================================================ l'heure du service ==
//  L'ecart appris de GET /v1/etat (l'heure du service - celle de ce PC) : les appels signes portent l'heure corrigee.
std::atomic<long long> g_offset{0};
std::atomic<bool>      g_offsetKnown{false};

lic::Seconds serviceNow() { return lic::systemNow() + static_cast<lic::Seconds>(g_offset.load()); }

// Le certificat du service, garde en memoire (relu si la cle du service change).
std::mutex& certMutex() {
    static std::mutex m;
    return m;
}
std::string& certCache() {
    static std::string c;
    return c;
}
std::string& certKeyCache() {
    static std::string k;
    return k;
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
    [[nodiscard]] bool reached() const noexcept { return failure == ai::HttpResponse::Failure::None && !tooBig; }
};

std::string hostOf(const std::string& url) {
    ai::Url u;
    if (ai::parseUrl(url, u)) return u.host;
    return "le service";
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
    if (!r.ok()) g.error = r.error.empty() ? ai::failureText(r.failure, hostOf(req.url)) : r.error;
    if (g.tooBig) g.error = "la r\xC3\xA9ponse du service est trop grosse";
    return g;
}

// Une phrase finit par un point.
std::string sentence(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;
}

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
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

// "dans 12 min", "dans 2 h" (au moins une minute).
std::string inText(long long seconds) {
    if (seconds < 60) seconds = 60;
    const long long min = (seconds + 59) / 60;
    if (min < 120) return "dans " + std::to_string(min) + " min";
    return "dans " + std::to_string((min + 59) / 60) + " h";
}

// Ce que dit une erreur du service ({"erreur":"<code>","message":"...","reessayer_dans":N,"libre_dans":N,
// "libre_le":"<date>"}).
struct ServiceError {
    std::string code, message;
    long long   retryIn{0};
    long long   freeIn{0};
    lic::Seconds freeOn{0};   // 409 complet (licence fixe) : quand une place gardee par un PC libere revient
};
ServiceError errorOf(const Got& g) {
    ServiceError e;
    json::Value v;
    if (!json::parse(g.body, v) || !v.isObject()) return e;
    e.code = v.str("erreur");
    // la phrase du service : courte, sans caractere de controle (elle s'affiche telle quelle)
    std::string m;
    for (const char c : v.str("message"))
        if (static_cast<unsigned char>(c) >= 0x20) m += c;
    m = trim(m);
    if (m.size() > 220) {
        m.resize(220);
        while (!m.empty() && (static_cast<unsigned char>(m.back()) & 0xC0) == 0x80) m.pop_back();
        if (!m.empty() && (static_cast<unsigned char>(m.back()) & 0xC0) == 0xC0) m.pop_back();
        m += "\xE2\x80\xA6";
    }
    e.message = m;
    e.retryIn = std::max<long long>(0, v.integer("reessayer_dans"));
    e.freeIn = std::max<long long>(0, v.integer("libre_dans"));
    lic::Seconds on = 0;
    if (lic::parseIsoTime(v.str("libre_le"), on)) e.freeOn = on;
    return e;
}

// Le service ne repond pas : une phrase claire (sans le point final).
std::string offlineText(const Got& g, const std::string& host) {
    using F = ai::HttpResponse::Failure;
    switch (g.failure) {
        case F::Cancelled: return "Arr\xC3\xAAt\xC3\xA9" "e : le programme se ferme";
        case F::Unavailable: {
            std::string e = sentence(g.error);
            if (!e.empty()) e.pop_back();
            return e;
        }
        case F::BadUrl: return "L'adresse du service des licences est refus\xC3\xA9" "e (" + host + ")";
        case F::Tls: return "Le service des licences (" + host + ") refuse la connexion s\xC3\xA9" "curis\xC3\xA9" "e (certificat non reconnu)";
        case F::None:
            if (g.tooBig) return "La r\xC3\xA9ponse du service des licences est trop grosse : elle est ignor\xC3\xA9" "e";
            return {};
        default: return "Hors ligne : le service des licences (" + host + ") ne r\xC3\xA9pond pas";
    }
}

// Ce qui n'a pas marche, en une phrase (`what` : ce qu'on faisait, sans point).
std::string problem(const Got& g, const std::string& host, const std::string& what) {
    if (!g.reached()) return offlineText(g, host) + ".";
    const ServiceError e = errorOf(g);
    if (g.status == 429)
        return what + " : le service limite les appels de ce PC pour l'instant. R\xC3\xA9" "essaie " + inText(e.retryIn > 0 ? e.retryIn : 3600) + ".";
    if (g.status >= 500)
        return what + " : le service des licences est en panne pour l'instant (erreur " + std::to_string(g.status) + "). R\xC3\xA9" "essaie plus tard.";
    if (!e.message.empty()) return what + " : " + sentence(e.message);
    return what + " : le service r\xC3\xA9pond " + std::to_string(g.status) + ".";
}

// ============================================================ le releve ==
bool storeRecord(const Record& mine, std::string* why) {
    Record disk;
    if (!loadRecord(disk, nullptr)) return saveRecord(mine, why);
    return saveRecord(mergeRecords(disk, mine), why);
}

void note(bool ok, const std::string& text) {
    lic::HistoryEvent e;
    e.at = lic::systemNow();
    e.kind = "service";
    e.ok = ok;
    e.text = text;
    std::string why;
    (void)lic::addHistoryEvent(e, &why);   // un dossier ou ce compte n'ecrit pas : le message l'a deja dit
}

std::string software() { return core::productName() + " " + XPG_ANALYZER_VERSION; }

// "07/11/2027" (UTC, comme les dates des licences).
std::string isoDay(lic::Seconds at) {
    const std::string d = at ? lic::isoDate(at) : std::string();
    return d.size() == 10 ? d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) : d;
}

// ============================================================ la connexion ==
struct Conn {
    lsv::Address             a;
    std::string              host;
    std::string              cert;          // le certificat du service (texte du bloc)
    std::string              serviceKey;    // sa cle (64 hexadecimaux)
    std::vector<std::string> lines;         // ce qui s'est passe en se connectant (un registre adopte...)
    bool                     timeNoted{false};
    int                      timeLine{-1};         // la phrase de l'heure pas notee (retiree si un second essai la note)
    bool                     offline{false};       // /v1/etat n'a pas repondu (why : la phrase, sans point)
};

// Des octets signes recus (un registre, une liste de revocation) : importes s'ils sont plus recents que ceux du
// PC ; sinon rien. Le magasin verifie la signature. `line` : ce qui s'est passe ("" : rien de nouveau).
bool adoptBytes(std::string_view bytes, lic::DocKind kind, std::uint64_t current, const std::string& label, std::string& line, bool& changed) {
    line.clear();
    std::vector<lic::SignedBlock> blocks;
    std::string w;
    std::uint64_t number = 0;
    if (lic::readBlocks(bytes, blocks, &w) && blocks.size() == 1 && blocks[0].kind == kind) {
        if (kind == lic::DocKind::Registry) {
            lic::Registry r;
            if (lic::parseRegistry(blocks[0].document, r, &w)) number = r.number;
        } else {
            lic::RevocationList r;
            if (lic::parseRevocations(blocks[0].document, r, &w)) number = r.number;
        }
    }
    const bool feminine = kind != lic::DocKind::Registry;
    if (number == 0) {
        line = label + " est illisible" + (w.empty() ? std::string(".") : " (" + w + ").");
        return false;
    }
    if (number <= current) return true;
    const auto rep = lic::importLicenseBytes(bytes);
    if (rep.ok) {
        changed = true;
        line = label + " n\xC2\xB0 " + std::to_string(number) + " est install\xC3\xA9" + (feminine ? "e." : ".");
        return true;
    }
    line = label + " n\xC2\xB0 " + std::to_string(number) + " est refus\xC3\xA9" + (feminine ? "e" : "") + " : " + sentence(rep.why);
    return false;
}

Got getPublic(const Conn& c, const std::string& pathAndQuery) {
    ai::HttpRequest req;
    req.method = "GET";
    req.url = c.a.url + pathAndQuery;
    req.headers = {{"User-Agent", userAgent()}, {"Cache-Control", "no-cache"}};
    req.connectTimeoutSeconds = 15;
    req.idleTimeoutSeconds = 30;
    return perform(req);
}

// L'ecart d'heure appris de GET /v1/etat. `etat` : la reponse lue. Faux : `why` (une phrase).
bool readState(Conn& c, json::Value& etat, std::string& why) {
    const Got g = getPublic(c, "/v1/etat");
    if (!g.reached()) {
        c.offline = true;
        why = offlineText(g, c.host);
        return false;
    }
    if (g.status != 200) {
        why = problem(g, c.host, "Le service des licences n'a pas pu \xC3\xAAtre lu");
        return false;
    }
    if (!json::parse(g.body, etat) || !etat.isObject() || etat.str("service") != "xpg-licences") {
        why = "Le service des licences r\xC3\xA9pond, mais pas comme pr\xC3\xA9vu (ce n'est pas le service des licences XPG ?).";
        return false;
    }
    const long long heure = etat.integer("heure");
    if (heure > 0) {
        g_offset = static_cast<long long>(heure - lic::systemNow());
        g_offsetKnown = true;
    }
    return true;
}

// La cle publique de ce PC (creee si besoin).
bool devicePublicKey(crypto::PublicKey& pk, std::string& why) {
    std::string hex, w;
    if (!lic::deviceKeyHex(hex, &w)) {
        why = "La cl\xC3\xA9 de ce PC n'est pas lisible : " + sentence(w);
        return false;
    }
    crypto::Bytes b;
    if (!crypto::unhex(hex, b, pk.size())) {
        why = "La cl\xC3\xA9 de ce PC est mal \xC3\xA9" "crite.";
        return false;
    }
    std::copy(b.begin(), b.end(), pk.begin());
    return true;
}

// L'identifiant de ce poste ; faux et `why` vide : ce PC n'a pas encore de cle (il n'a rien demande).
bool existingDevice(std::string& id, std::string& why) {
    why.clear();
    if (lic::thisDeviceFingerprint().empty()) return false;
    crypto::PublicKey pk{};
    if (!devicePublicKey(pk, why)) return false;
    id = lsv::deviceId(pk);
    return true;
}

// L'empreinte de l'installation de Windows de ce PC (licensing::thisStationHash, celle que porte sa demande) : le
// service la demande pour renouveler, prendre ou rendre un jeton, liberer - une cle de PC recopiee ailleurs n'y
// sert a rien. Vide : `why` (une phrase).
std::string stationOf(std::string& why) {
    std::string s = lic::thisStationHash();
    if (s.empty())
        why = "L'empreinte de l'installation de Windows de ce PC n'est pas lisible : le service des licences ne peut pas reconna\xC3\xAEtre ce poste.";
    return s;
}

// Qui signe les appels : la cle de ce PC, par le module (licensing::deviceSignServiceMessage) ; au lancement, avant
// le module, un magasin temporaire (startupLease).
struct DeviceSigner {
    std::function<bool(crypto::PublicKey&, std::string&)>                                          publicKey;
    std::function<bool(std::string_view, crypto::Signature&, crypto::PublicKey&, std::string&)> sign;
};
const DeviceSigner& moduleSigner() {
    static const DeviceSigner s{[](crypto::PublicKey& pk, std::string& why) { return devicePublicKey(pk, why); },
                                [](std::string_view m, crypto::Signature& sig, crypto::PublicKey& pub, std::string& why) {
                                    return lic::deviceSignServiceMessage(m, sig, pub, &why);
                                }};
    return s;
}

// Un appel signe par la cle de ce PC. Un refus 401 du a l'heure : /v1/etat relu, l'appel refait une fois.
Got callSigned(Conn& c, const std::string& method, const std::string& path, const std::string& body, std::string& why, int connectTimeout = 15,
               int idleTimeout = 30, const DeviceSigner& signer = moduleSigner()) {
    crypto::PublicKey pk{};
    if (!signer.publicKey(pk, why)) {
        Got g;
        g.failure = ai::HttpResponse::Failure::Other;
        g.error = why;
        return g;
    }
    const auto once = [&](std::string& signWhy) {
        bool signedOk = true;
        const auto headers = lsv::signRequest(method, path, body, pk, [&](std::string_view m) {
            crypto::Signature sig{};
            crypto::PublicKey pub{};
            std::string w;
            if (!signer.sign(m, sig, pub, w) || pub != pk) {
                signedOk = false;
                signWhy = w.empty() ? std::string("la cl\xC3\xA9 de ce PC a chang\xC3\xA9") : w;
            }
            return sig;
        }, serviceNow());
        Got g;
        if (!signedOk) {
            g.failure = ai::HttpResponse::Failure::Other;
            g.error = "L'appel n'a pas pu \xC3\xAAtre sign\xC3\xA9 par ce PC : " + sentence(signWhy);
            return g;
        }
        ai::HttpRequest req;
        req.method = method;
        req.url = c.a.url + path;
        req.headers = {{"User-Agent", userAgent()}, {"Content-Type", "application/json"}};
        for (const auto& h : headers) req.headers.push_back(h);
        req.body = body;
        req.connectTimeoutSeconds = connectTimeout;
        req.idleTimeoutSeconds = idleTimeout;
        return perform(req);
    };
    std::string signWhy;
    Got g = once(signWhy);
    if (g.failure == ai::HttpResponse::Failure::Other && !signWhy.empty()) {
        why = g.error;
        return g;
    }
    if (g.reached() && g.status == 401 && !g_stopping) {
        // L'heure ? L'ecart relu : s'il a change (l'horloge du PC a bouge), ou si le service le dit, un second essai.
        const long long before = g_offset.load();
        const ServiceError e = errorOf(g);
        json::Value etat;
        std::string w;
        if (readState(c, etat, w)) {
            const long long drift = std::llabs(g_offset.load() - before);
            if (drift > 30 || e.code == "date" || e.code == "heure" || e.code == "horloge") g = once(signWhy);
        }
    }
    return g;
}

// Le certificat du service (garde en memoire tant que la cle du service ne change pas), verifie : signe par une cle
// de l'editeur que ce programme croit, pour la cle que le service annonce, pas expire.
bool fetchCert(Conn& c, const std::string& serviceKey, std::string& why, const std::vector<lic::TrustedKey>* trusted = nullptr) {
    {
        std::lock_guard<std::mutex> lock(certMutex());
        if (!certCache().empty() && certKeyCache() == serviceKey) {
            c.cert = certCache();
            c.serviceKey = serviceKey;
            return true;
        }
    }
    const Got g = getPublic(c, "/v1/certificat");
    if (!g.reached() || g.status != 200) {
        why = g.reached() && g.status == 404 ? std::string("Le service des licences n'est pas encore pr\xC3\xAAt : l'\xC3\xA9" "diteur ne l'a pas encore certifi\xC3\xA9.")
                                             : problem(g, c.host, "Le certificat du service n'a pas pu \xC3\xAAtre lu");
        return false;
    }
    std::vector<crypto::PublicKey> keys;
    for (const auto& k : trusted ? *trusted : lic::trustedPublisherKeys()) keys.push_back(k.key);
    lsv::ServiceCert cert;
    std::string w;
    if (!lsv::verifyCert(g.body, keys, serviceNow(), cert, &w)) {
        why = "Le certificat du service des licences est refus\xC3\xA9 : " + sentence(w) + " Rien n'a chang\xC3\xA9.";
        return false;
    }
    if (!serviceKey.empty() && cert.key != serviceKey) {
        why = "Le certificat du service des licences est refus\xC3\xA9 : il est pour une autre cl\xC3\xA9 que celle du service. Rien n'a chang\xC3\xA9.";
        return false;
    }
    c.cert = g.body;
    c.serviceKey = cert.key;
    std::lock_guard<std::mutex> lock(certMutex());
    certCache() = g.body;
    certKeyCache() = cert.key;
    return true;
}

// Un certificat recu dans une reponse (activer, renouveler) : garde en memoire s'il est valable.
void adoptCert(Conn& c, const std::string& text) {
    if (text.empty() || text == c.cert) return;
    std::vector<crypto::PublicKey> keys;
    for (const auto& k : lic::trustedPublisherKeys()) keys.push_back(k.key);
    lsv::ServiceCert cert;
    if (!lsv::verifyCert(text, keys, serviceNow(), cert, nullptr)) return;
    c.cert = text;
    c.serviceKey = cert.key;
    std::lock_guard<std::mutex> lock(certMutex());
    certCache() = text;
    certKeyCache() = cert.key;
}

// L'heure signee (un nonce frais) : le compteur du bail est recale. `say` : une panne est dite (sinon, en silence : un
// second essai). Vrai : notee.
bool noteTime(Conn& c, Record& rec, bool say) {
    std::uint8_t n[16];
    crypto::randomBytes(n, sizeof n);
    const std::string nonce = crypto::hex(n, sizeof n);
    const Got g = getPublic(c, "/v1/heure?n=" + nonce);
    const auto said = [&c, say](std::string line) {
        if (!say) return;
        c.timeLine = static_cast<int>(c.lines.size());
        c.lines.push_back(std::move(line));
    };
    if (!g.reached() || g.status != 200) {
        said(problem(g, c.host, "L'heure du service n'a pas pu \xC3\xAAtre lue"));
        return false;
    }
    std::string w;
    if (!lic::noteServiceTime(g.body, nonce, c.cert, &w)) {
        said("L'heure sign\xC3\xA9" "e du service est refus\xC3\xA9" "e : " + sentence(w));
        return false;
    }
    c.timeNoted = true;
    rec.lastTime = lic::systemNow();
    if (c.timeLine >= 0 && c.timeLine < static_cast<int>(c.lines.size())) c.lines.erase(c.lines.begin() + c.timeLine);
    c.timeLine = -1;
    return true;
}

// A CHAQUE CONNEXION : l'etat du service (l'ecart d'heure, certifie, le registre), le certificat, l'heure signee.
// Faux : `why` (une phrase ; hors ligne, pas certifie...). `rec` : les dates des connexions.
bool connect(Conn& c, Record& rec, Result& res, std::string& why) {
    c.a = address();
    c.host = hostOf(c.a.url);
    json::Value etat;
    if (!readState(c, etat, why)) return false;
    res.reached = true;
    res.serviceChanges = etat.integer("changements", -1);   // 1.39.0 : -1 : un service d'avant
    res.serviceRegistry = etat.integer("registre", -1);
    rec.lastSync = lic::systemNow();
    if (!etat.flag("certifie")) {
        why = "Le service des licences n'est pas encore pr\xC3\xAAt : l'\xC3\xA9" "diteur ne l'a pas encore certifi\xC3\xA9.";
        return false;
    }
    // Le registre publie : adopte s'il est plus recent que celui du PC.
    const double regNumber = etat.num("registre");
    if (regNumber > 0 && static_cast<std::uint64_t>(regNumber) > lic::getLicenseState().registryNumber) {
        const Got g = getPublic(c, "/v1/registre");
        if (g.reached() && g.status == 200) {
            std::string line;
            (void)adoptBytes(g.body, lic::DocKind::Registry, lic::getLicenseState().registryNumber, "Le registre publi\xC3\xA9", line, res.changed);
            c.lines.push_back(line);
        }
    }
    if (!fetchCert(c, etat.str("cle_service"), why)) return false;
    (void)noteTime(c, rec, true);   // le reste peut se faire sans elle ; le compteur attend un nouvel essai
    return true;
}

// Une seconde chance pour l'heure signee : apres une activation ou un renouvellement reussi, si elle n'a pas pu etre
// notee en se connectant (sans elle, une licence en ligne reste << bail expire >> jusqu'a la prochaine connexion).
void noteTimeAgain(Conn& c, Record& rec) {
    if (!c.timeNoted && !c.cert.empty()) (void)noteTime(c, rec, false);
}

// ============================================================ l'etat << poste >> (la demande manuelle) ==
const lic::TrustedKey* findKey(const std::vector<lic::TrustedKey>& keys, std::string_view id) {
    for (const auto& k : keys)
        if (k.id == id) return &k;
    return nullptr;
}

// 1.36.0 : la cle de l'editeur qui a signe le certificat de CE service (celle qu'il emploie) ; nul : inconnue.
const lic::TrustedKey* certSigner(const Conn& c, const std::vector<lic::TrustedKey>& keys) {
    std::vector<lic::SignedBlock> blocks;
    if (c.cert.empty() || !lic::readBlocks(c.cert, blocks, nullptr) || blocks.empty()) return nullptr;
    return findKey(keys, blocks[0].keyId);
}

// Un seul bloc de ce genre, signe par une cle de l'editeur que ce programme croit.
bool verifyPublisherBlock(std::string_view bytes, lic::DocKind kind, lic::SignedBlock& out, std::string& why) {
    const auto keys = lic::trustedPublisherKeys();
    std::vector<lic::SignedBlock> blocks;
    std::string w;
    if (!lic::readBlocks(bytes, blocks, &w)) {
        why = "le bloc est illisible (" + w + ")";
        return false;
    }
    if (blocks.size() != 1 || blocks[0].kind != kind) {
        why = "ce n'est pas un seul bloc \xC2\xAB " + std::string(lic::docKindKey(kind)) + " \xC2\xBB";
        return false;
    }
    const lic::TrustedKey* signer = findKey(keys, blocks[0].keyId);
    if (!signer) {
        why = "il est sign\xC3\xA9 par une cl\xC3\xA9 que ce programme ne conna\xC3\xAEt pas (" + blocks[0].keyId + ")";
        return false;
    }
    if (!lic::verifyBlock(blocks[0], signer->key)) {
        why = "sa signature est fausse";
        return false;
    }
    out = std::move(blocks[0]);
    return true;
}

// L'identifiant de la licence d'un fichier .xpglic ; vide : aucune.
std::string licenseIdOf(std::string_view bytes) {
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(bytes, blocks, nullptr)) return {};
    for (const auto& b : blocks) {
        if (b.kind != lic::DocKind::License) continue;
        lic::License l;
        if (lic::parseLicense(b.document, l, nullptr)) return l.licenseId;
    }
    return {};
}

std::string installedLicenseId(const Record& rec) {
    const auto st = lic::getLicenseState();
    return !st.licenseId.empty() ? st.licenseId : rec.receivedLicense;
}

bool forThisRequest(const Record& rec, const lgw::DeviceStatus& s) { return !rec.nonce.empty() && s.requestNonce == rec.nonce; }

bool stepActivate(Conn& c, const std::string& normalized, const std::string& pcName0, Record& rec, Result& res, std::vector<std::string>& lines);
void scrub(std::string& text, const std::string& normalized, const std::string& typed);

// L'etat de CE PC publie par l'editeur (le bloc << poste >> de /v1/renouveler ou /v1/poste) : les reponses aux
// demandes manuelles, avec les memes regles de relecture qu'en 1.34. 1.37.0 : `conn` (la connexion ouverte) active une
// licence EN LIGNE acceptee avec son code (scelle pour ce PC) ; nul : a la prochaine verification.
void applyDeviceStatus(std::string_view blockText, const std::string& id, Record& rec, Result& res, std::vector<std::string>& lines, Conn* conn) {
    rec.lastCheck = lic::systemNow();
    lgw::DeviceStatus s;
    {
        lic::SignedBlock block;
        std::string w;
        if (!verifyPublisherBlock(blockText, lic::DocKind::DeviceStatus, block, w) || !lgw::parseDeviceStatus(block.document, s, &w)) {
            lines.push_back("L'\xC3\xA9tat publi\xC3\xA9 pour ce PC est refus\xC3\xA9 : " + sentence(w));
            return;
        }
        if (s.device != id) {
            lines.push_back("L'\xC3\xA9tat publi\xC3\xA9 est refus\xC3\xA9 : il est pour un autre poste (" + s.device.substr(0, 12) + "\xE2\x80\xA6).");
            return;
        }
    }
    if (s.number <= rec.lastNumber) return;   // rien de nouveau (ou un etat plus ancien)

    lgw::StatusContent content;
    std::string sealProblem;
    if (!s.sealed.empty()) {
        std::string plain, w;
        if (!lic::deviceOpenSeal(s.sealed, plain, &w) || !lgw::parseStatusContent(plain, content, &w)) sealProblem = w.empty() ? "illisible" : w;
    }
    bool applied = true;
    std::string revLine;
    if (sealProblem.empty() && !content.revocations.empty()) {
        if (!adoptBytes(content.revocations, lic::DocKind::Revocations, lic::getLicenseState().revocationNumber,
                        "La liste de r\xC3\xA9vocation re\xC3\xA7ue de l'\xC3\xA9" "diteur", revLine, res.changed))
            applied = false;
    }
    const bool mine = forThisRequest(rec, s);
    const bool open = pending(rec);
    bool stale = false;
    std::string text, notice, tone;
    bool ok = true;
    switch (s.state) {
        case lgw::State::Received:
            if (mine && open) text = "Ta demande est re\xC3\xA7ue par l'\xC3\xA9" "diteur : elle attend sa d\xC3\xA9" "cision.";
            else stale = true;
            break;
        case lgw::State::Accepted:
        case lgw::State::Updated: {
            const bool updated = s.state == lgw::State::Updated;
            std::string lid = licenseIdOf(content.license);
            bool installedOrPresent = false;
            bool settled = false;   // 1.37.0 : une reponse finale sans licence installee ici (le code a demander, illisible...)
            if (!sealProblem.empty()) {
                applied = false;
                text = "L'\xC3\xA9" "diteur a publi\xC3\xA9 une licence pour ce PC, mais elle ne s'ouvre pas : " + sentence(sealProblem);
            } else if (content.license.empty() && !content.code.empty() && !updated) {
                // 1.37.0 : une licence EN LIGNE, son code scelle pour CE PC : activee ici, sans rien taper.
                const std::string onlineId = content.onlineLicense;
                const auto now = lic::getLicenseState();
                std::string normalized;
                if (now.online && !onlineId.empty() && now.licenseId == onlineId &&
                    (now.status == lic::LicenseStatus::Valid || now.status == lic::LicenseStatus::Grace)) {
                    installedOrPresent = true;
                    lid = onlineId;
                    if (rec.receivedLicense.empty()) rec.receivedLicense = lid;
                    text = "Ta licence en ligne " + lid + " est d\xC3\xA9j\xC3\xA0 activ\xC3\xA9" "e sur ce PC.";
                } else if (!lsv::normalizeCode(content.code, normalized)) {
                    settled = true;
                    text = "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande (licence en ligne " + onlineId + "), mais son code d'activation est illisible : demande-le-lui.";
                    notice = text;
                    tone = "warning";
                } else if (!conn) {
                    applied = false;
                    text = "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande (licence en ligne " + onlineId + ") : elle s'active \xC3\xA0 la prochaine v\xC3\xA9rification.";
                } else {
                    Result one;
                    std::vector<std::string> act;
                    const bool done = stepActivate(*conn, normalized, rec.pcName, rec, one, act);
                    for (auto& l : act) scrub(l, normalized, content.code);
                    res.retryAfter = std::max(res.retryAfter, one.retryAfter);
                    if (done) {
                        installedOrPresent = true;
                        lid = !one.licenseId.empty() ? one.licenseId : onlineId;
                        res.licenseId = lid;
                        res.licenseInstalled = true;
                        res.changed = true;
                        rec.receivedLicense = lid;
                        res.answer = kAnswerAccepted;   // la fenetre qui le confirme
                        res.answerNote = trim(content.message);
                        res.answerOnline = true;
                        // Le mail ne l'a pas apporte (pas parti, ou aucun mail) : le code est montre une fois, dans la
                        // fenetre (jamais ecrit) - pour un autre PC ou une reinstallation.
                        if (content.mail != lgw::kMailSent) res.answerCode = lsv::formatCode(normalized);
                        text = "Ta licence en ligne " + lid + " est activ\xC3\xA9" "e sur ce PC (re\xC3\xA7ue de l'\xC3\xA9" "diteur).";
                        notice = text;
                        tone = "ok";
                    } else if (one.definitive) {
                        settled = true;
                        text = "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande (licence en ligne " + onlineId + "), mais le service refuse son code : " +
                               (act.empty() ? std::string("demande-le-lui.") : act.back());
                        notice = text;
                        tone = "warning";
                    } else {
                        applied = false;
                        text = "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande (licence en ligne " + onlineId + "), mais son activation n'a pas pu se faire" +
                               (act.empty() ? std::string(".") : " : " + act.back());
                    }
                }
                crypto::wipe(normalized.data(), normalized.size());
            } else if (content.license.empty() && !updated) {
                // Acceptee par une licence en ligne SANS son code (un outil de l'editeur d'avant la 1.37.0) : c'est fini
                // ici, le code vient de l'editeur (avant : << en attente >> sans fin).
                settled = true;
                text = "L'\xC3\xA9" "diteur a accept\xC3\xA9 ta demande par une licence en ligne" +
                       (content.onlineLicense.empty() ? std::string() : " (" + content.onlineLicense + ")") +
                       " : il te donne son code d'activation ; entre-le dans Licence\xE2\x80\xA6 \xE2\x80\xBA Activation \xE2\x80\xBA Activer avec un code.";
                notice = text;
                tone = "info";
            } else if (content.license.empty()) {
                applied = false;
                text = "L'\xC3\xA9" "diteur a mis \xC3\xA0 jour la demande, mais l'\xC3\xA9tat ne contient pas de licence.";
            } else {
                const auto rep = lic::importLicenseBytes(content.license);
                if (lid.empty() && rep.licenseInstalled) lid = rep.state.licenseId;
                res.licenseId = lid;
                if (rep.licenseInstalled) {
                    installedOrPresent = true;
                    res.licenseInstalled = true;
                    res.changed = true;
                    rec.receivedLicense = lid;
                    res.answer = updated ? kAnswerUpdated : kAnswerAccepted;   // 1.36.0 : la fenetre qui le confirme
                    res.answerNote = trim(content.message);
                    text = updated ? "Ta licence " + lid + " est mise \xC3\xA0 jour (re\xC3\xA7ue de l'\xC3\xA9" "diteur)."
                                   : "Ta licence " + lid + " est install\xC3\xA9" "e (re\xC3\xA7ue de l'\xC3\xA9" "diteur).";
                    notice = text;
                    tone = "ok";
                } else if (rep.ok) {
                    installedOrPresent = true;
                    if (rec.receivedLicense.empty()) rec.receivedLicense = lid;
                    text = "Ta licence " + lid + " est d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e.";
                } else {
                    applied = false;
                    text = "La licence re\xC3\xA7ue de l'\xC3\xA9" "diteur n'est pas install\xC3\xA9" "e : " + sentence(rep.why);
                }
            }
            if (!installedOrPresent && !settled) {
                ok = false;
                text += " Nouvel essai \xC3\xA0 la prochaine v\xC3\xA9rification.";
                notice = text;
                tone = "error";
            }
            if (!content.message.empty()) text += " Mot de l'\xC3\xA9" "diteur : " + content.message;
            if ((installedOrPresent || settled) && rec.local == kLocalSent && (mine || (s.requestNonce.empty() && open && res.licenseInstalled)))
                rec.outcome = "acceptee";
            // 1.37.0 : le mail de l'editeur n'est pas parti - verifier l'adresse (le profil) ; le code est dans la fenetre.
            if (applied && content.mail == lgw::kMailFailed && !updated) {
                res.answerMail = content.mail;
                res.answerMailTo = content.mailTo;
                std::string m = "Le mail de l'\xC3\xA9" "diteur n'est pas parti" + (content.mailTo.empty() ? std::string() : " (vers " + content.mailTo + ")") +
                                " : v\xC3\xA9rifie ton adresse (Licence\xE2\x80\xA6 \xE2\x80\xBA Mon profil).";
                if (!res.answerCode.empty()) m += " Ton code d'activation est dans la fen\xC3\xAAtre de confirmation.";
                res.notices.push_back({"licence:mail", m, "warning"});
                text += " " + m;
            } else if (applied && content.mail == lgw::kMailSent && !updated) {
                res.answerMail = content.mail;
                res.answerMailTo = content.mailTo;
            }
            break;
        }
        case lgw::State::Refused: {
            if (!(mine && open)) {
                stale = true;
                break;
            }
            std::string reason = trim(content.message);
            if (!sealProblem.empty()) reason = "(la raison ne s'ouvre pas : " + sealProblem + ")";
            text = reason.empty() ? std::string("Ta demande d'activation a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e (sans raison donn\xC3\xA9" "e).")
                                  : "Ta demande d'activation a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e : " + sentence(reason);
            ok = false;
            res.refused = true;
            res.answer = kAnswerRefused;   // 1.36.0 : la fenetre qui le dit
            res.answerNote = sealProblem.empty() ? trim(content.message) : reason;
            notice = text;
            tone = "warning";
            rec.outcome = "refusee";
            break;
        }
        case lgw::State::Cancelled:
            if (mine && (open || rec.local == kLocalCancelled)) {
                text = "L'\xC3\xA9" "diteur a pris note de l'annulation de ta demande.";
                if (open) rec.outcome = "annulee";
            } else {
                stale = true;
            }
            break;
        case lgw::State::Revoked: {
            if (!sealProblem.empty()) applied = false;
            const std::string lid = installedLicenseId(rec);
            text = "L'\xC3\xA9" "diteur a r\xC3\xA9voqu\xC3\xA9 la licence " + (lid.empty() ? std::string("de ce PC") : lid) + ".";
            if (!content.message.empty()) text += " " + sentence(content.message);
            if (!sealProblem.empty()) text += " Le d\xC3\xA9tail ne s'ouvre pas : " + sentence(sealProblem);
            ok = false;
            notice = text;
            tone = "error";
            res.changed = true;
            break;
        }
        case lgw::State::Removed: {
            const std::string lid = installedLicenseId(rec);
            text = "Ce PC a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 de la licence " + (lid.empty() ? std::string() : lid + " ") + "par l'\xC3\xA9" "diteur.";
            if (!content.message.empty()) text += " " + sentence(content.message);
            ok = false;
            notice = text;
            tone = "warning";
            res.changed = true;
            break;
        }
    }
    if (!revLine.empty()) {
        text = join({text, revLine});
        if (!notice.empty()) notice = join({notice, revLine});
    }
    const std::string stamp = " (\xC3\xA9tat n\xC2\xB0 " + std::to_string(s.number) + " publi\xC3\xA9 par l'\xC3\xA9" "diteur)";
    if (stale) {
        text = "L'\xC3\xA9" "diteur a publi\xC3\xA9 l'\xC3\xA9tat \xC2\xAB " + lgw::stateLabel(s.state) + " \xC2\xBB pour " +
               (open ? std::string("une autre demande de ce PC : ta demande en cours ne change pas.")
                     : std::string("une demande de ce PC qui n'est plus en cours : rien ne change."));
        if (applied) rec.lastNumber = s.number;
        else res.retryLater = true;
        note(true, text + stamp);
        res.changed = true;
        lines.push_back(text);
        return;
    }
    const bool fresh = applied || text != rec.lastMessage;
    if (applied) {
        rec.lastNumber = s.number;
        rec.lastState = lgw::stateKey(s.state);
        rec.lastNonce = s.requestNonce;
    } else {
        res.retryLater = true;
        if (!fresh) notice.clear();
    }
    rec.lastMessage = text;
    if (fresh) note(ok, text + stamp);
    if (!notice.empty()) res.notices.push_back({"licence:demande", notice, tone});
    res.changed = true;
    lines.push_back(text);
}

// ============================================================ les etapes ==
//  Chacune suppose la connexion faite (connect) ; elle ajoute sa phrase a `lines` et ses avis a `res`.

// Le bloc d'un champ d'une reponse (une chaine JSON, sinon vide).
std::string field(const json::Value& v, std::string_view key) {
    const json::Value* f = v.find(key);
    return f && f->isString() ? f->asString() : std::string();
}

// Le code : retire de toute phrase qui pourrait le contenir (le service ne le renvoie jamais ; prudence).
void scrub(std::string& text, const std::string& normalized, const std::string& typed) {
    const auto cut = [&text](const std::string& s) {
        if (s.size() < 8) return;
        for (auto at = text.find(s); at != std::string::npos; at = text.find(s)) text.replace(at, s.size(), "XPG-\xE2\x80\xA6");
    };
    cut(typed);
    cut(lsv::formatCode(normalized));
    cut(normalized);
}

bool stepActivate(Conn& c, const std::string& normalized, const std::string& pcName0, Record& rec, Result& res, std::vector<std::string>& lines) {
    const std::string pcName = trim(pcName0);
    std::string block, why;
    lic::ActivationRequest parsed;
    if (!lic::activationRequestText(pcName, {}, software(), block, &parsed, &why)) {
        lines.push_back("La demande de ce PC n'a pas pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e : " + sentence(why) + " Le code n'a pas \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.");
        return false;
    }
    json::Value body = json::Value::object();
    body.set("code", lsv::formatCode(normalized));
    body.set("demande", block);
    body.set("nom", pcName);
    std::string bodyText = json::write(body, false);
    const Got g = callSigned(c, "POST", "/v1/activer", bodyText, why);
    crypto::wipe(bodyText.data(), bodyText.size());   // le code ne reste pas en memoire
    if (!g.reached()) {
        lines.push_back(why.empty() ? offlineText(g, c.host) + " ; l'activation ne s'est pas faite, r\xC3\xA9" "essaie une fois connect\xC3\xA9." : why);
        return false;
    }
    if (g.status != 200) {
        const ServiceError e = errorOf(g);
        res.refused = g.status != 429 && g.status < 500;
        if (g.status == 403 && (e.code == "code" || e.code.empty())) {
            res.definitive = true;
            lines.push_back("Ce code n'est pas reconnu par le service des licences (inconnu ou d\xC3\xA9sactiv\xC3\xA9) : rien n'est install\xC3\xA9. "
                            "V\xC3\xA9rifie-le, ou demande-le \xC3\xA0 l'\xC3\xA9" "diteur.");
        } else if (g.status == 409 && e.code == "limite") {
            // Le maximum d'activations enregistrees de CETTE licence (le service le dit)
            lines.push_back((e.message.empty() ? std::string("Cette licence a trop d'activations enregistr\xC3\xA9" "es : demande \xC3\xA0 l'\xC3\xA9" "diteur.")
                                               : sentence(e.message)) +
                            " Rien n'est install\xC3\xA9.");
        } else if (g.status == 409) {
            std::string text = "Tous les postes de cette licence sont d\xC3\xA9j\xC3\xA0 pris : rien n'est install\xC3\xA9. Lib\xC3\xA8re un autre PC (fen\xC3\xAAtre Licence de ce "
                               "PC \xE2\x80\xBA Lib\xC3\xA9rer ce poste), ou demande un poste de plus \xC3\xA0 l'\xC3\xA9" "diteur.";
            // Une place gardee par un PC libere au-dela des changements de PC permis : la date ou elle revient.
            if (e.freeOn > 0)
                text += " Une place revient le " + isoDay(e.freeOn) + " : celle d'un PC lib\xC3\xA9r\xC3\xA9 alors que la licence avait d\xC3\xA9j\xC3\xA0 chang\xC3\xA9 "
                        "de PC plusieurs fois ce mois-ci (il la garde jusqu'\xC3\xA0 la fin de son bail).";
            lines.push_back(text);
        } else if (g.status == 403 && e.code == "activation") {
            // La cle de ce PC est deja activee pour cette licence sur une autre installation de Windows.
            lines.push_back((e.message.empty() ? std::string("Cette activation est pour un autre poste.") : sentence(e.message)) + " Rien n'est install\xC3\xA9.");
        } else if (g.status == 410) {
            res.definitive = true;
            lines.push_back(std::string("Cette licence est ") + (e.code == "expiree" ? "expir\xC3\xA9" "e" : "r\xC3\xA9voqu\xC3\xA9" "e") +
                            " : le code ne peut plus l'activer. Rien n'est install\xC3\xA9.");
        } else if (g.status == 400) {
            lines.push_back("Le service refuse la demande de ce PC" + (e.message.empty() ? std::string(".") : " : " + sentence(e.message)) +
                            " Rien n'est install\xC3\xA9.");
        } else {
            res.retryAfter = static_cast<int>(e.retryIn);
            lines.push_back(problem(g, c.host, "L'activation n'a pas pu se faire") + " Rien n'est install\xC3\xA9.");
        }
        return false;
    }
    json::Value v;
    if (!json::parse(g.body, v) || !v.isObject() || field(v, "licence").empty() || field(v, "activation").empty()) {
        lines.push_back("Le service a r\xC3\xA9pondu, mais sa r\xC3\xA9ponse est illisible : rien n'est install\xC3\xA9. R\xC3\xA9" "essaie plus tard.");
        return false;
    }
    adoptCert(c, field(v, "certificat"));
    const auto rep = lic::installActivation(field(v, "licence"), field(v, "activation"), c.cert);
    if (!rep.ok) {
        lines.push_back("Le service a accept\xC3\xA9 le code, mais la licence re\xC3\xA7ue n'est pas install\xC3\xA9" "e : " + sentence(rep.why));
        return false;
    }
    noteTimeAgain(c, rec);
    const auto st = lic::getLicenseState();
    const std::string lid = !st.licenseId.empty() ? st.licenseId : licenseIdOf(field(v, "licence"));
    res.licenseId = lid;
    res.licenseInstalled = rep.licenseInstalled || st.licenseId == lid;
    res.changed = true;
    rec.activatedLicense = lid;
    rec.activatedPcName = pcName;
    rec.activatedAt = lic::systemNow();
    rec.lastRenew = lic::systemNow();
    const std::string edition = st.licensedEdition.empty() ? st.edition : st.licensedEdition;
    std::string text = "Licence " + lid + (edition.empty() ? std::string() : " (" + edition + ")") + " activ\xC3\xA9" "e sur ce PC";
    if (st.activationUntil) text += " : bail jusqu'au " + isoDay(st.activationUntil);
    text += ".";
    if (st.floating) text += " Licence flottante : un poste est pris tant que le programme tourne.";
    note(true, "Licence " + lid + " activ\xC3\xA9" "e en ligne par code" + (pcName.empty() ? std::string() : " (PC \xC2\xAB " + pcName + " \xC2\xBB)") + ".");
    res.notices.push_back({"licence:activation", text, "ok"});
    lines.push_back(text);
    return true;
}

// Pourquoi le service ne renouvelle plus une activation de ce PC (le motif de /v1/renouveler), en une phrase.
std::string goneText(const std::string& lid, const std::string& motif) {
    const std::string pre = "Ce PC n'est plus activ\xC3\xA9 pour la licence " + lid + " : ";
    const std::string post = " Son activation est effac\xC3\xA9" "e de ce PC.";
    if (motif == "liberee") return pre + "ce poste a \xC3\xA9t\xC3\xA9 lib\xC3\xA9r\xC3\xA9." + post;
    if (motif == "retiree")
        return pre + "l'\xC3\xA9" "diteur l'a retir\xC3\xA9, ou un autre PC a pris sa place apr\xC3\xA8s la fin de son bail." + post;
    if (motif == "revoquee") return pre + "l'\xC3\xA9" "diteur a r\xC3\xA9voqu\xC3\xA9 cette licence." + post;
    if (motif == "desactivee") return pre + "l'\xC3\xA9" "diteur a d\xC3\xA9sactiv\xC3\xA9 cette licence." + post;
    if (motif == "autre_poste")
        return pre + "cette activation est celle d'une autre installation de Windows (la cl\xC3\xA9 de ce PC a-t-elle \xC3\xA9t\xC3\xA9 recopi\xC3\xA9" "e ?)." + post +
               " Active ce PC avec son code, ou demande \xC3\xA0 l'\xC3\xA9" "diteur.";
    if (motif == "inconnue_ici")
        return pre + "le service ne conna\xC3\xAEt plus son activation (elle est effac\xC3\xA9" "e quelque temps apr\xC3\xA8s la fin de son bail)." + post +
               " Active-le de nouveau avec son code.";
    return pre + "l'\xC3\xA9" "diteur l'a lib\xC3\xA9r\xC3\xA9, retir\xC3\xA9 ou r\xC3\xA9voqu\xC3\xA9." + post;
}

// Le renouvellement : le bail de chaque activation de ce PC (sa cle ET son installation de Windows : "station"), celles
// que le service ne renouvelle plus (`retirees` et leur motif), l'etat << poste >>. La liste de revocation de l'editeur
// ne vient plus d'ici : une revocation d'une licence en ligne est un motif (revoquee) ; celle d'une licence liee a ce
// PC vient dans son etat << poste >>, scelle pour lui.
bool stepRenew(Conn& c, Record& rec, Result& res, std::vector<std::string>& lines, bool& devicePart) {
    std::string why;
    const auto before = lic::getLicenseState();
    const std::string station = stationOf(why);
    if (station.empty()) {
        lines.push_back(why + " Le bail n'a pas pu \xC3\xAAtre renouvel\xC3\xA9.");
        return false;
    }
    json::Value body = json::Value::object();
    body.set("station", station);
    const Got g = callSigned(c, "POST", "/v1/renouveler", json::write(body, false), why);
    if (!g.reached() || g.status != 200) {
        res.retryAfter = static_cast<int>(errorOf(g).retryIn);
        lines.push_back(!why.empty() && !g.reached() ? why : problem(g, c.host, "Le bail n'a pas pu \xC3\xAAtre renouvel\xC3\xA9") + " Rien n'a chang\xC3\xA9.");
        return false;
    }
    json::Value v;
    if (!json::parse(g.body, v) || !v.isObject()) {
        lines.push_back("Le service a r\xC3\xA9pondu au renouvellement, mais sa r\xC3\xA9ponse est illisible : rien n'a chang\xC3\xA9.");
        return false;
    }
    adoptCert(c, field(v, "certificat"));
    bool allOk = true;
    bool renewed = false;
    bool unclear = false;             // une activation recue sans licence lisible : rien n'est deduit de son absence
    std::vector<std::string> known;   // les licences dont le service a parle (renouvelees ou retirees)
    if (const json::Value* acts = v.find("activations"); acts && acts->isArray()) {
        for (const auto& a : acts->items()) {
            if (!a.isObject()) {
                unclear = true;
                continue;
            }
            const std::string lid = licenseIdOf(field(a, "licence"));
            if (lid.empty()) unclear = true;
            else known.push_back(lid);
            const auto rep = lic::installActivation(field(a, "licence"), field(a, "activation"), c.cert);
            if (!rep.ok) {
                allOk = false;
                lines.push_back("Le bail de la licence " + (lid.empty() ? std::string("re\xC3\xA7ue") : lid) + " n'est pas install\xC3\xA9 : " + sentence(rep.why));
            } else {
                res.changed = true;
                renewed = true;
            }
        }
    }
    // 1.38.0 : les PC de chaque licence renouvelee (Mes appareils : << 3 PC sur 20 >>) - gardes pour la fenetre Licence.
    if (const json::Value* postes = v.find("postes"); postes && postes->isObject()) {
        json::Value keep = json::Value::object();
        keep.set("version", 1);
        keep.set("recu_le", lic::isoTime(lic::systemNow()));
        keep.set("postes", *postes);
        std::string w;
        (void)crypto::replaceFile(devicesPath(), json::write(keep) + "\n", false, &w);
    }
    const json::Value* motifs = v.find("motifs");
    // Une activation que le service ne renouvelle plus : effacee de ce PC (sauf une licence finie : elle reste, le
    // magasin la dit finie, avec sa grace).
    const auto drop = [&](const std::string& lid, const std::string& motif) {
        const bool had = lic::getLicenseState().licenseId == lid || rec.activatedLicense == lid;
        if (motif == "expiree") {
            if (!had) return;
            const std::string text = "La licence " + lid + " a pris fin : le service ne la renouvelle plus. Demande une prolongation \xC3\xA0 l'\xC3\xA9" "diteur.";
            res.notices.push_back({"licence:fin", text, "warning"});
            lines.push_back(text);
            return;
        }
        std::string w;
        if (!lic::dropActivation(lid, &w)) {
            allOk = false;
            lines.push_back("L'activation de la licence " + lid + " n'a pas pu \xC3\xAAtre effac\xC3\xA9" "e : " + sentence(w));
            return;
        }
        res.changed = true;
        if (rec.activatedLicense == lid) rec.activatedLicense.clear();
        if (!had) return;
        const std::string text = goneText(lid, motif);
        note(false, text);
        // 1.39.0 : perdue sans que ce PC l'ait rendue (retiree, revoquee, desactivee...) : une fenetre le dit tout de suite
        res.notices.push_back({"licence:retrait", text, "warning", motif != "liberee", lid});
        lines.push_back(text);
    };
    if (const json::Value* gone = v.find("retirees"); gone && gone->isArray()) {
        for (const auto& item : gone->items()) {
            const std::string lid = item.asString();
            if (lid.empty()) continue;
            known.push_back(lid);
            drop(lid, motifs && motifs->isObject() ? motifs->str(lid) : std::string());
        }
    }
    // La licence en ligne de ce PC dont le service ne dit rien : il ne connait plus cette activation (effacee apres la
    // fin de son bail) - elle ne sera plus jamais renouvelee.
    if (renewed) noteTimeAgain(c, rec);
    if (!unclear) {
        const auto st = lic::getLicenseState();
        if (st.online && !st.licenseId.empty() && std::find(known.begin(), known.end(), st.licenseId) == known.end()) drop(st.licenseId, "inconnue_ici");
    }
    if (const std::string poste = field(v, "poste"); !poste.empty()) {
        std::string id, w;
        if (existingDevice(id, w)) applyDeviceStatus(poste, id, rec, res, lines, &c);
    }
    devicePart = true;
    rec.lastRenew = lic::systemNow();
    const auto after = lic::getLicenseState();
    // Renouvele en silence ; sauf une licence qui revient (le bail etait fini) : un avis.
    if (after.online && after.status == lic::LicenseStatus::Valid &&
        (before.status == lic::LicenseStatus::LeaseExpired || before.status == lic::LicenseStatus::NoFloatingSeat || !before.online)) {
        const std::string text = "Bail renouvel\xC3\xA9 : ta licence " + after.licenseId + " est de nouveau active" +
                                 (after.activationUntil ? " (jusqu'au " + isoDay(after.activationUntil) + ")." : std::string("."));
        res.notices.push_back({"licence:bail", text, "ok"});
        lines.push_back(text);
    } else if (after.online && after.activationUntil && renewed) {
        lines.push_back("Bail renouvel\xC3\xA9 jusqu'au " + isoDay(after.activationUntil) + ".");
    } else if (!after.online && before.online && lines.empty()) {
        lines.push_back("Le service n'a plus d'activation pour ce PC.");
    } else if (lines.empty()) {
        lines.push_back("Rien \xC3\xA0 renouveler : ce PC n'a pas de licence en ligne.");
    }
    return allOk;
}

bool stepTrial(Conn& c, Record& rec, Result& res, std::vector<std::string>& lines) {
    const std::string machine = lic::thisMachineHash();
    if (machine.empty()) {
        lines.push_back("L'empreinte de cette machine n'est pas lisible : la D\xC3\xA9" "couverte reste not\xC3\xA9" "e sur ce PC seulement.");
        return false;
    }
    json::Value body = json::Value::object();
    body.set("machine", machine);
    std::string why;
    const Got g = callSigned(c, "POST", "/v1/decouverte", json::write(body, false), why);
    if (!g.reached() || g.status != 200) {
        res.retryAfter = static_cast<int>(errorOf(g).retryIn);
        lines.push_back(!why.empty() && !g.reached() ? why : problem(g, c.host, "La D\xC3\xA9" "couverte n'a pas pu \xC3\xAAtre d\xC3\xA9" "clar\xC3\xA9" "e au service"));
        return false;
    }
    std::string w;
    if (!lic::installTrialRecord(g.body, c.cert, &w)) {
        lines.push_back("La D\xC3\xA9" "couverte enregistr\xC3\xA9" "e par le service est refus\xC3\xA9" "e : " + sentence(w));
        return false;
    }
    lsv::Trial t;
    lic::SignedBlock b;
    if (lsv::findBlock(g.body, lic::DocKind::Trial, b, nullptr) && lsv::parseTrial(b.document, t, nullptr)) rec.trialStart = t.start;
    const bool first = rec.trialRecordedAt == 0;
    if (first) rec.trialRecordedAt = lic::systemNow();
    rec.trialSync = lic::systemNow();
    res.changed = true;
    const auto st = lic::getLicenseState();
    std::string text = "D\xC3\xA9" "couverte enregistr\xC3\xA9" "e par le service";
    if (rec.trialStart) text += " (commenc\xC3\xA9" "e le " + isoDay(rec.trialStart) + " sur cette machine)";
    text += ".";
    if (st.status == lic::LicenseStatus::Evaluation)
        text += " Il te reste " + std::to_string(std::max(0, st.evaluationDaysLeft)) + (st.evaluationDaysLeft > 1 ? " jours." : " jour.");
    if (first) note(true, text);
    lines.push_back(text);
    return true;
}

// La licence flottante de ce PC (installee, en ligne, flottante) ; vide : aucune.
std::string floatingLicense() {
    const auto st = lic::getLicenseState();
    return st.floating ? st.licenseId : std::string();
}

bool stepTakeLease(Conn& c, const std::string& lid, Result& res, std::vector<std::string>& lines) {
    std::string why;
    const std::string station = stationOf(why);
    if (station.empty()) {
        lines.push_back(why + " Le poste flottant n'a pas pu \xC3\xAAtre pris.");
        return false;
    }
    json::Value body = json::Value::object();
    body.set("licence", lid);
    body.set("station", station);
    const Got g = callSigned(c, "POST", "/v1/flottante/jeton", json::write(body, false), why);
    if (!g.reached()) {
        lines.push_back(!why.empty() ? why : offlineText(g, c.host) + " ; le poste flottant n'a pas pu \xC3\xAAtre pris.");
        return false;
    }
    const ServiceError e = errorOf(g);
    if (g.status == 409) {
        lic::dropLease();   // le service le dit : ce PC n'a pas de poste (un jeton garde de la fermeture ne vaut plus)
        res.noSeat = true;
        res.refused = true;
        res.retryAfter = static_cast<int>(e.freeIn);
        const std::string text = "Licence flottante " + lid + " : aucun poste libre" +
                                 (e.freeIn > 0 ? " \xE2\x80\x94 libre " + inText(e.freeIn) : std::string()) + ". Nouvel essai tout seul.";
        res.notices.push_back({"licence:flottante", text, "warning"});
        lines.push_back(text);
        return false;
    }
    if (g.status == 403 || g.status == 410) {
        lic::dropLease();
        res.refused = true;
        lines.push_back(!e.message.empty() ? "Pas de poste flottant : " + sentence(e.message)
                                           : "Ce PC n'a pas d'activation flottante pour la licence " + lid + " : le service ne lui donne pas de poste.");
        return false;
    }
    if (g.status != 200) {
        res.retryAfter = static_cast<int>(e.retryIn);
        lines.push_back(problem(g, c.host, "Le poste flottant n'a pas pu \xC3\xAAtre pris"));
        return false;
    }
    json::Value v;
    std::string w;
    if (!json::parse(g.body, v) || !v.isObject() || field(v, "jeton").empty()) {
        lines.push_back("Le service a donn\xC3\xA9 un poste flottant, mais sa r\xC3\xA9ponse est illisible.");
        return false;
    }
    if (!lic::installLease(field(v, "jeton"), c.cert, &w)) {
        lines.push_back("Le jeton du poste flottant est refus\xC3\xA9 : " + sentence(w));
        return false;
    }
    res.changed = true;
    const auto st = lic::getLicenseState();
    lines.push_back("Poste flottant pris" + (st.leaseUntil ? " jusqu'\xC3\xA0 " + dateText(st.leaseUntil).substr(11) : std::string()) +
                    " (renouvel\xC3\xA9 tant que le programme tourne).");
    return true;
}

// `dropLocal` faux (la fermeture) : le jeton de ce PC reste dans son dossier jusqu'a sa fin (15 min au plus) - le poste
// est rendu au service, et l'etat de ce PC ne change pas en partant (pas de << Valide -> aucun poste >> dans son
// historique a chaque fermeture) ; au lancement suivant, le poste est repris (startupLease), ou le service dit
// qu'il n'y en a plus (409 : le jeton est alors efface).
bool stepReturnLease(Conn& c, const std::string& lid, Result& res, std::vector<std::string>& lines, int connectTimeout, int idleTimeout,
                     bool dropLocal = true) {
    std::string why;
    const std::string station = stationOf(why);
    if (station.empty()) {
        lines.push_back(why + " Le poste flottant se lib\xC3\xA9rera de lui-m\xC3\xAAme \xC3\xA0 la fin du jeton (15 min au plus).");
        return false;
    }
    json::Value body = json::Value::object();
    body.set("licence", lid);
    body.set("station", station);
    const Got g = callSigned(c, "POST", "/v1/flottante/rendre", json::write(body, false), why, connectTimeout, idleTimeout);
    if (!g.reached() || g.status != 200) {
        lines.push_back((!why.empty() && !g.reached() ? why : problem(g, c.host, "Le poste flottant n'a pas pu \xC3\xAAtre rendu")) +
                        " Il se lib\xC3\xA9rera de lui-m\xC3\xAAme \xC3\xA0 la fin du jeton (15 min au plus).");
        return false;
    }
    if (dropLocal) lic::dropLease();
    res.changed = dropLocal;
    lines.push_back("Poste flottant rendu : un autre PC peut le prendre.");
    return true;
}

bool stepDevice(Conn& c, Record& rec, Result& res, std::vector<std::string>& lines) {
    std::string id, why;
    if (!existingDevice(id, why)) {
        if (!why.empty()) {
            lines.push_back(why);
            return false;
        }
        rec.lastCheck = lic::systemNow();
        lines.push_back("Rien de nouveau : ce PC n'a encore fait aucune demande.");
        return true;
    }
    const Got g = callSigned(c, "GET", "/v1/poste", "", why);
    if (!g.reached() || g.status != 200) {
        res.retryAfter = static_cast<int>(errorOf(g).retryIn);
        lines.push_back(!why.empty() && !g.reached() ? why : problem(g, c.host, "L'\xC3\xA9tat de ce PC n'a pas pu \xC3\xAAtre lu"));
        return false;
    }
    json::Value v;
    if (!json::parse(g.body, v) || !v.isObject()) {
        lines.push_back("Le service a r\xC3\xA9pondu, mais l'\xC3\xA9tat de ce PC est illisible.");
        return false;
    }
    const std::size_t before = lines.size();
    const std::string poste = field(v, "poste");
    if (!poste.empty()) applyDeviceStatus(poste, id, rec, res, lines, &c);
    else rec.lastCheck = lic::systemNow();
    if (lines.size() == before)
        lines.push_back(pending(rec) ? std::string("Rien de nouveau : ta demande attend que l'\xC3\xA9" "diteur la voie.")
                                     : std::string("Rien de nouveau de l'\xC3\xA9" "diteur pour ce PC."));
    return true;
}

// Le debut commun des travaux : disponible, le releve lu, la connexion. Faux : `res.message` dit pourquoi (hors
// ligne : UNE phrase, finie par `offlineTail`).
bool begin(Result& res, Conn& c, Record& rec, const char* offlineTail = "rien n'a chang\xC3\xA9") {
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return false;
    }
    if (!loadRecord(rec, &why)) {
        res.message = why;
        return false;
    }
    res.pendingAfter = pending(rec);
    res.requestSentAt = res.pendingAfter ? rec.sentAt : 0;
    if (!connect(c, rec, res, why)) {
        if (c.offline) res.message = why + " ; " + offlineTail + ".";
        else res.message = sentence(why) + (res.reached ? std::string() : " Rien n'a chang\xC3\xA9.");
        if (res.reached) (void)storeRecord(rec, nullptr);
        return false;
    }
    return true;
}

// La fin commune : le releve fusionne et ecrit, la phrase.
void finish(Result& res, const Conn& c, const Record& rec, std::vector<std::string> lines) {
    std::vector<std::string> all = c.lines;
    for (auto& l : lines) all.push_back(std::move(l));
    res.message = join(all);
    std::string why;
    if (!storeRecord(rec, &why)) res.message = join({res.message, "Attention : " + why});
    res.pendingAfter = pending(rec);
    res.requestSentAt = res.pendingAfter ? rec.sentAt : 0;
}

} // namespace

// ============================================================ l'adresse ==
void setAddressForTests(std::optional<lsv::Address> a) {
    std::lock_guard<std::mutex> lock(addressMutex());
    addressOverride() = std::move(a);
    g_offset = 0;
    g_offsetKnown = false;
    std::lock_guard<std::mutex> lock2(certMutex());
    certCache().clear();
    certKeyCache().clear();
}

lsv::Address address() {
    {
        std::lock_guard<std::mutex> lock(addressMutex());
        if (addressOverride()) return *addressOverride();
    }
    return lsv::compiledAddress();
}

bool available(std::string* why) {
    if (address().empty()) {
        if (why) *why = "Ce programme n'a pas de service des licences : la demande et la licence passent par fichier.";
        return false;
    }
    if (!lic::isConfigured()) {
        if (why) *why = "Les licences ne sont pas activ\xC3\xA9" "es dans ce programme : il n'y a rien \xC3\xA0 activer.";
        return false;
    }
    if (why) why->clear();
    return true;
}

lic::Seconds clockOffset() noexcept { return g_offsetKnown ? static_cast<lic::Seconds>(g_offset.load()) : 0; }

// ============================================================ le releve ==
std::string recordPath() { return deploy::joinPath(lic::getLicenseFolder(), kRecordFile); }

// ============================================================ 1.38.0 : les PC d'une licence en ligne ==
std::string devicesPath() { return deploy::joinPath(lic::getLicenseFolder(), "postes-en-ligne.json"); }

std::string shortKey(const std::string& k) {
    if (k.size() < 16) return k;
    std::string a = k.substr(0, 6), b = k.substr(k.size() - 6);
    for (auto* s : {&a, &b})
        for (char& ch : *s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return a + "\xE2\x80\xA6" + b;
}

bool parseDevices(std::string_view text, lic::Seconds receivedAt, std::vector<OnlineDevices>& out) {
    out.clear();
    json::Value v;
    if (!json::parse(std::string(text), v) || !v.isObject()) return false;
    const auto clean = [](std::string s, std::size_t max) {
        if (s.size() > max) s.resize(max);
        for (char& ch : s)
            if (static_cast<unsigned char>(ch) < 0x20) ch = ' ';
        return s;
    };
    const auto when = [](const json::Value& o, const char* key) {
        lic::Seconds t = 0;
        (void)lic::parseIsoTime(o.str(key), t);
        return t;
    };
    for (const auto& [lid, l] : v.members()) {
        if (!l.isObject() || lid.empty() || lid.size() > 64) continue;
        OnlineDevices d;
        d.licenseId = lid;
        d.type = clean(l.str("type"), 16);
        d.max = static_cast<int>(std::clamp<std::int64_t>(l.integer("max"), 0, 1000000));
        d.used = static_cast<int>(std::clamp<std::int64_t>(l.integer("utilises"), 0, 1000000));
        d.total = static_cast<int>(std::clamp<std::int64_t>(l.integer("total"), 0, 1000000));
        d.receivedAt = receivedAt;
        if (const json::Value* list = l.find("liste"); list && list->isArray())
            for (const auto& x : list->items()) {
                if (!x.isObject() || d.list.size() >= 500) continue;
                OnlineDevice p;
                p.name = clean(x.str("nom"), 80);
                p.key = clean(x.str("cle"), 64);
                p.activatedAt = when(x, "activee");
                p.seenAt = when(x, "vue");
                p.until = when(x, "jusqu_a");
                p.state = clean(x.str("etat"), 16);
                p.thisPc = x.flag("ce_pc");
                p.token = x.flag("jeton");
                d.list.push_back(std::move(p));
            }
        if (d.total < static_cast<int>(d.list.size())) d.total = static_cast<int>(d.list.size());
        out.push_back(std::move(d));
    }
    return true;
}

std::optional<OnlineDevices> onlineDevices(const std::string& licenseId) {
    if (licenseId.empty()) return std::nullopt;
    crypto::Bytes bytes;
    std::string w;
    if (!crypto::readFile(devicesPath(), bytes, &w, 2u * 1024u * 1024u)) return std::nullopt;
    json::Value v;
    if (!json::parse(std::string(bytes.begin(), bytes.end()), v) || !v.isObject()) return std::nullopt;
    lic::Seconds at = 0;
    (void)lic::parseIsoTime(v.str("recu_le"), at);
    std::vector<OnlineDevices> all;
    const json::Value& p = v.at("postes");
    if (!p.isObject() || !parseDevices(json::write(p, false), at, all)) return std::nullopt;
    for (auto& d : all)
        if (d.licenseId == licenseId) return std::move(d);
    return std::nullopt;
}

bool loadRecord(Record& out, std::string* why) {
    out = Record{};
    const std::string path = recordPath();
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec)) return true;
    crypto::Bytes bytes;
    std::string w;
    if (!crypto::readFile(path, bytes, &w, 1024u * 1024u)) {
        if (why) *why = "Le relev\xC3\xA9 du service des licences ne se lit pas : " + w;
        return false;
    }
    json::Value v;
    if (!json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), v, &w) || !v.isObject()) {
        if (why) *why = "Le relev\xC3\xA9 du service des licences est illisible (" + recordPath() + ").";
        return false;
    }
    const auto time = [&v](std::string_view key) {
        lic::Seconds t = 0;
        return lic::parseIsoTime(v.str(key), t) ? t : lic::Seconds{0};
    };
    const auto count = [&v](std::string_view key) {
        const double n = v.num(key);
        return n > 0 ? static_cast<std::uint64_t>(n) : std::uint64_t{0};
    };
    Record r;
    r.number = count("numero");
    r.device = v.str("poste");
    r.nonce = v.str("demande");
    r.sentAt = time("envoyee_le");
    r.customer = v.str("client");
    r.contact = v.str("contact");
    r.message = v.str("message");
    r.pcName = v.str("nom_pc");
    r.licenseId = v.str("licence_demandee");
    r.edition = v.str("edition");   // 1.37.0
    r.email = v.str("courriel");
    r.local = v.str("etat_local");
    r.cancelledAt = time("annulee_le");
    r.outcome = v.str("reponse");
    r.lastNumber = count("dernier_numero");
    r.lastState = v.str("dernier_etat");
    r.lastNonce = v.str("derniere_demande");
    r.lastCheck = time("derniere_verification");
    r.lastMessage = v.str("dernier_message");
    r.receivedLicense = v.str("licence_recue");
    r.lastSync = time("derniere_connexion");
    r.lastRenew = time("dernier_renouvellement");
    r.lastTime = time("derniere_heure");
    r.trialSync = time("decouverte_declaree");
    r.trialRecordedAt = time("decouverte_enregistree");
    r.trialStart = time("decouverte_debut");
    r.activatedLicense = v.str("licence_activee");
    r.activatedPcName = v.str("nom_pc_active");
    r.activatedAt = time("activee_le");
    if (r.local != kLocalSent && r.local != kLocalCancelled) r.local.clear();
    out = std::move(r);
    return true;
}

bool saveRecord(const Record& r, std::string* why) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    const auto time = [&v](const char* key, lic::Seconds t) {
        if (t) v.set(key, lic::isoTime(t));
    };
    const auto text = [&v](const char* key, const std::string& s) {
        if (!s.empty()) v.set(key, s);
    };
    if (r.number) v.set("numero", r.number);
    text("poste", r.device);
    text("demande", r.nonce);
    time("envoyee_le", r.sentAt);
    text("client", r.customer);
    text("contact", r.contact);
    text("message", r.message);
    text("nom_pc", r.pcName);
    text("licence_demandee", r.licenseId);
    text("edition", r.edition);   // 1.37.0
    text("courriel", r.email);
    text("etat_local", r.local);
    time("annulee_le", r.cancelledAt);
    text("reponse", r.outcome);
    if (r.lastNumber) v.set("dernier_numero", r.lastNumber);
    text("dernier_etat", r.lastState);
    text("derniere_demande", r.lastNonce);
    time("derniere_verification", r.lastCheck);
    text("dernier_message", r.lastMessage);
    text("licence_recue", r.receivedLicense);
    time("derniere_connexion", r.lastSync);
    time("dernier_renouvellement", r.lastRenew);
    time("derniere_heure", r.lastTime);
    time("decouverte_declaree", r.trialSync);
    time("decouverte_enregistree", r.trialRecordedAt);
    time("decouverte_debut", r.trialStart);
    text("licence_activee", r.activatedLicense);
    text("nom_pc_active", r.activatedPcName);
    time("activee_le", r.activatedAt);
    std::string w;
    if (!crypto::replaceFile(recordPath(), json::write(v) + "\n", false, &w)) {
        if (why) *why = "Le relev\xC3\xA9 du service des licences n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + w;
        return false;
    }
    return true;
}

Record mergeRecords(const Record& disk, const Record& mine) {
    Record out = mine;
    // La demande manuelle : la plus recente des deux (envoyee le, puis son numero).
    const auto newer = [](const Record& a, const Record& b) {
        if (a.number == 0) return false;
        if (b.number == 0) return true;
        if (a.sentAt != b.sentAt) return a.sentAt > b.sentAt;
        return a.number > b.number;
    };
    const bool same = disk.number == mine.number && disk.nonce == mine.nonce;
    if (!same && newer(disk, mine)) {
        out.number = disk.number;
        out.device = disk.device;
        out.nonce = disk.nonce;
        out.sentAt = disk.sentAt;
        out.customer = disk.customer;
        out.contact = disk.contact;
        out.message = disk.message;
        out.pcName = disk.pcName;
        out.licenseId = disk.licenseId;
        out.edition = disk.edition;
        out.email = disk.email;
        out.local = disk.local;
        out.cancelledAt = disk.cancelledAt;
        out.outcome = disk.outcome;
    } else if (same && mine.number > 0) {
        if (disk.local == kLocalCancelled && out.local != kLocalCancelled) {
            out.local = kLocalCancelled;
            out.cancelledAt = disk.cancelledAt;
        }
        if (out.outcome.empty()) out.outcome = disk.outcome;
    }
    // L'etat << poste >> : celui qui a traite le plus recent (a egalite : ce travail).
    if (disk.lastNumber > mine.lastNumber) {
        out.lastNumber = disk.lastNumber;
        out.lastState = disk.lastState;
        out.lastNonce = disk.lastNonce;
        out.lastMessage = disk.lastMessage;
        if (!disk.receivedLicense.empty()) out.receivedLicense = disk.receivedLicense;
    }
    if (out.receivedLicense.empty()) out.receivedLicense = disk.receivedLicense;
    out.lastCheck = std::max(disk.lastCheck, mine.lastCheck);
    // Les connexions : les plus recentes ; la Decouverte : la premiere fois enregistree, le debut le plus ancien.
    out.lastSync = std::max(disk.lastSync, mine.lastSync);
    out.lastRenew = std::max(disk.lastRenew, mine.lastRenew);
    out.lastTime = std::max(disk.lastTime, mine.lastTime);
    out.trialSync = std::max(disk.trialSync, mine.trialSync);
    const auto earliest = [](lic::Seconds a, lic::Seconds b) { return a == 0 ? b : b == 0 ? a : std::min(a, b); };
    out.trialRecordedAt = earliest(disk.trialRecordedAt, mine.trialRecordedAt);
    out.trialStart = earliest(disk.trialStart, mine.trialStart);
    // L'activation en ligne : la plus recente des deux (une liberation de l'un efface la sienne, pas une plus recente).
    if (disk.activatedAt > mine.activatedAt) {
        out.activatedLicense = disk.activatedLicense;
        out.activatedPcName = disk.activatedPcName;
        out.activatedAt = disk.activatedAt;
    }
    return out;
}

bool pending(const Record& r) noexcept { return r.number > 0 && r.local == kLocalSent && r.outcome.empty(); }

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

std::string dayText(lic::Seconds at) {
    const std::string d = dateText(at);
    return d.size() >= 10 ? d.substr(0, 10) : d;
}

std::string stateText(const Record& r) {
    if (r.number == 0 || r.local.empty()) return "Aucune demande";
    if (r.local == kLocalCancelled) return "Annul\xC3\xA9" "e" + (r.cancelledAt ? " le " + dateText(r.cancelledAt) : std::string());
    if (r.outcome == "acceptee") return "Accept\xC3\xA9" "e par l'\xC3\xA9" "diteur";
    if (r.outcome == "refusee") return "Refus\xC3\xA9" "e par l'\xC3\xA9" "diteur";
    if (r.outcome == "annulee") return "Annul\xC3\xA9" "e (l'\xC3\xA9" "diteur l'a not\xC3\xA9)";
    if (r.lastState == "recue" && !r.nonce.empty() && r.lastNonce == r.nonce) return "Re\xC3\xA7ue par l'\xC3\xA9" "diteur \xE2\x80\x94 en attente de sa d\xC3\xA9" "cision";
    return "Envoy\xC3\xA9" "e \xE2\x80\x94 en attente de l'\xC3\xA9" "diteur";
}

// ============================================================ les travaux ==
Result readTime() {
    Result res;
    res.job = Job::Time;
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    res.ok = c.timeNoted;
    const auto st = lic::getLicenseState();
    std::vector<std::string> lines;
    if (c.timeNoted)
        lines.push_back("Heure du service not\xC3\xA9" "e" + (st.trustedNow ? " (" + dateText(st.trustedNow) + ")" : std::string()) +
                        " : le compteur du bail est recal\xC3\xA9.");
    finish(res, c, rec, std::move(lines));
    return res;
}

Result activate(const std::string& code, const std::string& pcName) {
    Result res;
    res.job = Job::Activate;
    std::string normalized;
    if (!lsv::normalizeCode(code, normalized)) {
        res.refused = true;
        res.message = "Ce n'est pas un code d'activation : il a la forme XPG-XXXX-XXXX-XXXX-XXXX (16 lettres et chiffres). Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    if (trim(pcName).size() > kMaxPcName) {
        res.message = "Le nom du PC est trop long (" + std::to_string(kMaxPcName) + " caract\xC3\xA8res au plus). Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    Conn c;
    Record rec;
    if (!begin(res, c, rec, "ton code n'a pas \xC3\xA9t\xC3\xA9 envoy\xC3\xA9, r\xC3\xA9" "essaie une fois connect\xC3\xA9")) {
        scrub(res.message, normalized, code);
        crypto::wipe(normalized.data(), normalized.size());
        return res;
    }
    std::vector<std::string> lines;
    res.ok = stepActivate(c, normalized, pcName, rec, res, lines);
    for (auto& l : lines) scrub(l, normalized, code);
    for (auto& n : res.notices) scrub(n.title, normalized, code);
    crypto::wipe(normalized.data(), normalized.size());
    finish(res, c, rec, std::move(lines));
    return res;
}

Result renew() {
    Result res;
    res.job = Job::Renew;
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::vector<std::string> lines;
    bool device = false;
    res.ok = stepRenew(c, rec, res, lines, device);
    finish(res, c, rec, std::move(lines));
    return res;
}

Result release(const std::string& licenseId) {
    Result res;
    res.job = Job::Release;
    std::string lid = trim(licenseId);
    if (lid.empty()) {
        const auto st = lic::getLicenseState();
        if (st.online) lid = st.licenseId;
    }
    if (lid.empty()) {
        Record rec;
        if (loadRecord(rec, nullptr)) lid = rec.activatedLicense;
    }
    if (lid.empty()) {
        res.message = "Ce PC n'a pas de licence en ligne \xC3\xA0 lib\xC3\xA9rer.";
        return res;
    }
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::string why;
    std::vector<std::string> lines;
    const std::string station = stationOf(why);
    if (station.empty()) {
        lines.push_back(why + " Ce PC garde sa licence.");
        finish(res, c, rec, std::move(lines));
        return res;
    }
    json::Value body = json::Value::object();
    body.set("licence", lid);
    body.set("station", station);
    const Got g = callSigned(c, "POST", "/v1/liberer", json::write(body, false), why);
    const ServiceError e = errorOf(g);
    // 404, ou 403 activation (cette activation est celle d'une autre installation de Windows) : le service n'a pas
    // d'activation de CE poste pour cette licence - elle est effacee ici aussi.
    const bool unknown = g.reached() && (g.status == 404 || (g.status == 403 && e.code == "activation"));
    if (!g.reached() || (g.status != 200 && !unknown)) {
        res.retryAfter = static_cast<int>(e.retryIn);
        lines.push_back((!why.empty() && !g.reached() ? why : problem(g, c.host, "Ce PC n'a pas pu \xC3\xAAtre lib\xC3\xA9r\xC3\xA9")) +
                        " Il garde sa licence.");
        finish(res, c, rec, std::move(lines));
        return res;
    }
    // La place : rendue tout de suite, ou gardee jusqu'a la fin du bail de ce PC (la licence a deja change de PC
    // autant de fois que permis ces 30 derniers jours).
    lic::Seconds keptUntil = 0;
    long long moves = 0, allowed = 0;
    if (!unknown) {
        json::Value v;
        if (json::parse(g.body, v) && v.isObject() && v.str("place") == "gardee") {
            lic::Seconds t = 0;
            if (lic::parseIsoTime(v.str("jusqu_a"), t)) keptUntil = t;
            moves = v.integer("transferts");
            allowed = v.integer("transferts_permis");
        }
    }
    std::string w;
    if (floatingLicense() == lid) lic::dropLease();
    if (!lic::dropActivation(lid, &w)) {
        lines.push_back("Le service a lib\xC3\xA9r\xC3\xA9 le poste, mais l'activation de ce PC n'a pas pu \xC3\xAAtre effac\xC3\xA9" "e : " + sentence(w));
        finish(res, c, rec, std::move(lines));
        return res;
    }
    if (rec.activatedLicense == lid) {
        rec.activatedLicense.clear();
        rec.activatedPcName.clear();
        rec.activatedAt = lic::systemNow();   // la liberation est plus recente que l'activation : la fusion la garde
    }
    res.ok = true;
    res.changed = true;
    res.licenseId = lid;
    std::string text;
    if (unknown && g.status == 403)
        text = "Le service n'a pas d'activation de ce poste pour la licence " + lid +
               " (elle est celle d'une autre installation de Windows) : elle est effac\xC3\xA9" "e ici aussi.";
    else if (unknown)
        text = "Le service n'avait plus d'activation de ce PC pour la licence " + lid + " : elle est effac\xC3\xA9" "e ici aussi.";
    else if (keptUntil > 0)
        text = "Ce PC est lib\xC3\xA9r\xC3\xA9 : les fonctions de la licence " + lid + " sont ferm\xC3\xA9" "es ici. Sa place reste prise jusqu'au " + isoDay(keptUntil) +
               " (la fin de son bail) : la licence a d\xC3\xA9j\xC3\xA0 chang\xC3\xA9 de PC " +
               (moves > 0 ? std::to_string(moves) + " fois" : std::string("plusieurs fois")) + " ces 30 derniers jours" +
               (allowed > 0 ? " (" + std::to_string(allowed) + " au plus)" : std::string()) +
               ". Pour la rendre tout de suite, demande \xC3\xA0 l'\xC3\xA9" "diteur de retirer ce PC.";
    else
        text = "Ce PC est lib\xC3\xA9r\xC3\xA9 : son poste de la licence " + lid +
               " est rendu (un autre PC peut le prendre). Les fonctions de la licence sont ferm\xC3\xA9" "es ici.";
    note(true, text);
    if (keptUntil > 0) res.notices.push_back({"licence:liberation", text, "info"});
    lines.push_back(text);
    finish(res, c, rec, std::move(lines));
    return res;
}

Result trial() {
    Result res;
    res.job = Job::Trial;
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::vector<std::string> lines;
    res.ok = stepTrial(c, rec, res, lines);
    finish(res, c, rec, std::move(lines));
    return res;
}

Result takeLease() {
    Result res;
    res.job = Job::LeaseTake;
    const std::string lid = floatingLicense();
    if (lid.empty()) {
        res.message = "Ce PC n'a pas de licence flottante.";
        return res;
    }
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::vector<std::string> lines;
    res.ok = stepTakeLease(c, lid, res, lines);
    res.licenseId = lid;
    finish(res, c, rec, std::move(lines));
    return res;
}

Result returnLease() {
    Result res;
    res.job = Job::LeaseReturn;
    const std::string lid = floatingLicense();
    if (lid.empty()) {
        res.message = "Ce PC n'a pas de licence flottante.";
        return res;
    }
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    Conn c;
    c.a = address();
    c.host = hostOf(c.a.url);
    std::vector<std::string> lines;
    res.ok = stepReturnLease(c, lid, res, lines, 15, 30);
    res.reached = res.ok;
    res.licenseId = lid;
    res.message = join(lines);
    return res;
}

Result sendRequest(const RequestForm& form0) {
    Result res;
    res.job = Job::Request;
    RequestForm f;
    f.customer = trim(form0.customer);
    f.contact = trim(form0.contact);
    f.pcName = trim(form0.pcName);
    f.licenseId = trim(form0.licenseId);
    f.message = trim(form0.message);
    f.edition = trim(form0.edition);
    f.email = trim(form0.email);
    if (f.customer.empty()) {
        res.message = "Indique le nom de ta soci\xC3\xA9t\xC3\xA9 (ou le tien) : l'\xC3\xA9" "diteur en a besoin pour \xC3\xA9tablir la licence.";
        return res;
    }
    if (f.customer.size() > kMaxCustomer || f.contact.size() > kMaxContact || f.message.size() > kMaxMessage || f.pcName.size() > kMaxPcName ||
        f.licenseId.size() > kMaxLicenseId || f.email.size() > kMaxEmail) {
        res.message = "Un champ est trop long (soci\xC3\xA9t\xC3\xA9, contact et courriel : " + std::to_string(kMaxCustomer) + " caract\xC3\xA8res, nom du PC : " +
                      std::to_string(kMaxPcName) + ", message : " + std::to_string(kMaxMessage) + ").";
        return res;
    }
    if (!f.email.empty() && !lgw::plausibleEmail(f.email)) {
        res.message = "Le courriel \xC2\xAB " + f.email + " \xC2\xBB ne ressemble pas \xC3\xA0 une adresse (nom@domaine.fr) : corrige-le, ou laisse-le vide.";
        return res;
    }
    if (f.edition.size() > kMaxEdition ||
        !std::all_of(f.edition.begin(), f.edition.end(), [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'; })) {
        res.message = "L'\xC3\xA9" "dition choisie est illisible : choisis-la de nouveau dans la liste.";
        return res;
    }
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    {
        Record old;
        if (!loadRecord(old, &why)) {
            res.message = why;
            return res;
        }
        if (pending(old)) {
            res.pendingAfter = true;
            res.message = "Une demande est d\xC3\xA9j\xC3\xA0 en cours pour ce PC (n\xC2\xB0 " + std::to_string(old.number) + ", envoy\xC3\xA9" "e le " + dateText(old.sentAt) +
                          ") : une seule \xC3\xA0 la fois. Annule-la d'abord, ou attends la r\xC3\xA9ponse de l'\xC3\xA9" "diteur.";
            return res;
        }
    }
    // La cle de l'editeur pour laquelle la demande est scellee : celle que ce programme croit (la vraie d'abord).
    const auto keys = lic::trustedPublisherKeys();
    const lic::TrustedKey* key = nullptr;
    for (const auto& k : keys)
        if (!k.test) {
            key = &k;
            break;
        }
    if (!key && !keys.empty()) key = &keys.front();
    if (!key) {
        res.message = "Ce programme n'a pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur : la demande ne peut pas \xC3\xAAtre scell\xC3\xA9" "e.";
        return res;
    }
    std::string block;
    lic::ActivationRequest parsed;
    if (!lic::activationRequestText(f.pcName, f.licenseId, software(), block, &parsed, &why)) {
        res.message = "La demande de ce PC n'a pas pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e : " + sentence(why) + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    lgw::OnlineRequest q;
    q.request = block;
    q.customer = f.customer;
    q.contact = f.contact;
    q.message = f.message;
    q.edition = f.edition;   // 1.37.0
    q.email = f.email;
    std::string sealed = lgw::sealFor(key->key, lgw::writeOnlineRequest(q));
    if (sealed.empty()) {
        res.message = "La demande n'a pas pu \xC3\xAAtre scell\xC3\xA9" "e pour l'\xC3\xA9" "diteur. Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    Conn c;
    Record rec;
    if (!begin(res, c, rec, "ta demande n'est pas partie")) return res;
    // 1.36.0 : scellee pour la cle de l'editeur qui a certifie CE service (celle qu'il emploie et qu'il lit) : une copie
    // d'essai croit aussi la vraie cle ; un programme livre n'en a qu'une (la meme).
    if (const lic::TrustedKey* signer = certSigner(c, keys); signer && signer != key) {
        key = signer;
        sealed = lgw::sealFor(key->key, lgw::writeOnlineRequest(q));
        if (sealed.empty()) {
            res.message = "La demande n'a pas pu \xC3\xAAtre scell\xC3\xA9" "e pour l'\xC3\xA9" "diteur. Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
            return res;
        }
    }
    // Relu juste avant l'envoi : une demande partie entre-temps depuis l'autre Studio (le meme dossier) est vue.
    if (pending(rec)) {
        res.pendingAfter = true;
        res.message = "Une demande vient de partir pour ce PC (n\xC2\xB0 " + std::to_string(rec.number) + ", depuis l'autre Studio ?) : une seule \xC3\xA0 la fois. "
                      "Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        (void)storeRecord(rec, nullptr);
        return res;
    }
    std::string id;
    if (!existingDevice(id, why)) {
        res.message = (why.empty() ? std::string("La cl\xC3\xA9 de ce PC manque.") : why) + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.";
        return res;
    }
    json::Value body = json::Value::object();
    body.set("cle", key->id);
    body.set("scelle", sealed);
    std::vector<std::string> lines;
    const Got g = callSigned(c, "POST", "/v1/demandes", json::write(body, false), why);
    if (!g.reached() || (g.status != 201 && g.status != 200)) {
        res.retryAfter = static_cast<int>(errorOf(g).retryIn);
        lines.push_back((!why.empty() && !g.reached() ? why : problem(g, c.host, "La demande n'a pas pu \xC3\xAAtre envoy\xC3\xA9" "e")) + " Rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9.");
        finish(res, c, rec, std::move(lines));
        return res;
    }
    json::Value answer;
    const double n = json::parse(g.body, answer) && answer.isObject() ? answer.num("numero") : 0;
    if (n <= 0) {
        lines.push_back("Le service a re\xC3\xA7u la demande mais sa r\xC3\xA9ponse est illisible (pas de num\xC3\xA9ro) : v\xC3\xA9rifie plus tard, ou pr\xC3\xA9viens l'\xC3\xA9" "diteur.");
        finish(res, c, rec, std::move(lines));
        return res;
    }
    const auto number = static_cast<std::uint64_t>(n);
    rec.number = number;
    rec.device = id;
    rec.nonce = parsed.nonce;
    rec.sentAt = lic::systemNow();
    rec.customer = f.customer;
    rec.contact = f.contact;
    rec.message = f.message;
    rec.pcName = f.pcName;
    rec.licenseId = f.licenseId;
    rec.edition = f.edition;
    rec.email = f.email;
    rec.local = kLocalSent;
    rec.cancelledAt = 0;
    rec.outcome.clear();
    rec.lastMessage = "Demande envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(number) + ").";
    std::string what = "n\xC2\xB0 " + std::to_string(number);
    if (!f.pcName.empty()) what += ", PC \xC2\xAB " + f.pcName + " \xC2\xBB";
    if (!f.licenseId.empty()) what += ", pour la licence " + f.licenseId;
    if (!f.edition.empty()) what += ", \xC3\xA9" "dition " + f.edition;
    note(true, "Demande d'activation envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur par le service des licences (" + what + ").");
    res.ok = true;
    res.changed = true;
    lines.push_back("Demande envoy\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "diteur (n\xC2\xB0 " + std::to_string(number) + "). Ta licence s'installera d'elle-m\xC3\xAAme d\xC3\xA8s qu'il l'aura accept\xC3\xA9" "e.");
    finish(res, c, rec, std::move(lines));
    res.pendingAfter = true;
    return res;
}

Result cancelRequest() {
    Result res;
    res.job = Job::CancelRequest;
    std::string why;
    if (!available(&why)) {
        res.message = why;
        return res;
    }
    {
        Record old;
        if (!loadRecord(old, &why)) {
            res.message = why;
            return res;
        }
        if (!pending(old)) {
            res.message = "Aucune demande en cours \xC3\xA0 annuler.";
            return res;
        }
    }
    Conn c;
    Record rec;
    if (!begin(res, c, rec, "la demande n'est pas annul\xC3\xA9" "e")) return res;
    if (!pending(rec)) {
        res.message = "Aucune demande en cours \xC3\xA0 annuler.";
        return res;
    }
    json::Value body = json::Value::object();
    body.set("numero", rec.number);
    std::vector<std::string> lines;
    const Got g = callSigned(c, "POST", "/v1/demandes/annuler", json::write(body, false), why);
    if (!g.reached() || g.status != 200) {
        res.retryAfter = static_cast<int>(errorOf(g).retryIn);
        lines.push_back((!why.empty() && !g.reached() ? why : problem(g, c.host, "L'annulation n'a pas pu \xC3\xAAtre envoy\xC3\xA9" "e")) +
                        " La demande n'est pas annul\xC3\xA9" "e.");
        finish(res, c, rec, std::move(lines));
        return res;
    }
    rec.local = kLocalCancelled;
    rec.cancelledAt = lic::systemNow();
    rec.lastMessage = "Demande n\xC2\xB0 " + std::to_string(rec.number) + " annul\xC3\xA9" "e.";
    note(true, "Demande d'activation n\xC2\xB0 " + std::to_string(rec.number) + " annul\xC3\xA9" "e par ce PC.");
    res.ok = true;
    res.changed = true;
    lines.push_back("Demande annul\xC3\xA9" "e. Tu peux en envoyer une nouvelle.");
    finish(res, c, rec, std::move(lines));
    return res;
}

Result checkDevice() {
    Result res;
    res.job = Job::Device;
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::vector<std::string> lines;
    res.ok = stepDevice(c, rec, res, lines);
    finish(res, c, rec, std::move(lines));
    return res;
}

Result sync() {
    Result res;
    res.job = Job::Sync;
    Conn c;
    Record rec;
    if (!begin(res, c, rec)) return res;
    std::vector<std::string> lines;
    bool devicePart = false;
    bool allOk = true;
    // 1. le bail (une licence en ligne)
    {
        const auto st = lic::getLicenseState();
        if (st.online || !rec.activatedLicense.empty()) allOk = stepRenew(c, rec, res, lines, devicePart) && allOk;
    }
    // 2. la Decouverte (pas encore enregistree, puis une fois par jour)
    {
        const auto st = lic::getLicenseState();
        const lic::Seconds now = lic::systemNow();
        if (st.status == lic::LicenseStatus::Evaluation && (rec.trialRecordedAt == 0 || now - rec.trialSync >= lic::kDay))
            allOk = stepTrial(c, rec, res, lines) && allOk;
    }
    // 3. le code laisse par l'installateur : active, puis efface (garde si le service n'a pas pu repondre)
    if (std::string code = lic::pendingActivationCode(); !code.empty()) {
        std::string normalized;
        if (!lsv::normalizeCode(code, normalized)) {
            std::string w;
            (void)lic::clearPendingActivationCode(&w);
            const std::string text = "Le code laiss\xC3\xA9 par l'installateur n'a pas la forme d'un code d'activation : il est effac\xC3\xA9. Active ta licence "
                                     "avec le bon code (fen\xC3\xAAtre Licence).";
            res.notices.push_back({"licence:code-installateur", text, "warning"});
            lines.push_back(text);
        } else {
            Result one;
            std::vector<std::string> codeLines;
            const bool ok = stepActivate(c, normalized, {}, rec, one, codeLines);
            for (auto& l : codeLines) scrub(l, normalized, code);
            for (auto& n : one.notices) scrub(n.title, normalized, code);
            if (ok || one.definitive) {
                std::string w;
                if (!lic::clearPendingActivationCode(&w)) codeLines.push_back("Le code laiss\xC3\xA9 par l'installateur n'a pas pu \xC3\xAAtre effac\xC3\xA9 : " + sentence(w));
            }
            if (!ok) {
                const std::string text = "Le code laiss\xC3\xA9 par l'installateur n'a pas activ\xC3\xA9 de licence : " + join(codeLines) +
                                         (one.definitive ? std::string(" Il est effac\xC3\xA9.") : std::string(" Nouvel essai au prochain lancement."));
                one.notices.push_back({"licence:code-installateur", text, "warning"});
                codeLines = {text};
                allOk = false;
            }
            res.licenseInstalled = res.licenseInstalled || one.licenseInstalled;
            res.changed = res.changed || one.changed;
            if (!one.licenseId.empty()) res.licenseId = one.licenseId;
            for (auto& n : one.notices) res.notices.push_back(std::move(n));
            for (auto& l : codeLines) lines.push_back(std::move(l));
            crypto::wipe(normalized.data(), normalized.size());
        }
        crypto::wipe(code.data(), code.size());
    }
    // 4. l'etat << poste >> (s'il n'est pas deja venu avec le renouvellement)
    if (!devicePart) {
        std::string id, w;
        if (existingDevice(id, w)) {
            std::vector<std::string> dl;
            if (!stepDevice(c, rec, res, dl)) allOk = false;
            // rien de nouveau : pas dit au lancement
            for (auto& l : dl)
                if (l.rfind("Rien de nouveau", 0) != 0) lines.push_back(std::move(l));
        }
    }
    res.ok = allOk;
    if (lines.empty() && c.lines.empty()) lines.push_back("Licence synchronis\xC3\xA9" "e avec le service.");
    finish(res, c, rec, std::move(lines));
    return res;
}

// 1.39.0 : le regard toutes les 2 minutes - GET /v1/etat seulement (public, rien d'ecrit sur ce PC ni au service).
Result watchState() {
    Result res;
    res.job = Job::Watch;
    if (!available()) return res;
    Conn c;
    c.a = address();
    c.host = hostOf(c.a.url);
    json::Value etat;
    std::string why;
    if (!readState(c, etat, why)) return res;   // hors ligne : rien a dire (le prochain regard reessaie)
    res.reached = true;
    res.ok = true;
    res.serviceChanges = etat.integer("changements", -1);
    res.serviceRegistry = etat.integer("registre", -1);
    return res;
}

// ============================================================ le fil de fond ==
namespace {

double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr double kHour = 3600.0;
constexpr double kLeasePeriod = 300.0;   // le jeton (15 min) renouvele toutes les 5 minutes

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
    bool               on{false};          // startRunner
    bool               synced{false};      // la synchro du lancement a joint le service
    double             nextSync{0};
    double             nextRenew{0};
    double             nextLease{0};
    double             nextDevice{0};
    double             nextTrial{0};
    double             nextLook{0};
    double             nextWatch{0};       // 1.39.0 : le prochain regard sur GET /v1/etat
    long long          knownChanges{-1};   // 1.39.0 : le compteur des changements deja vu (-1 : pas encore lu)
    bool               syncNow{false};     // 1.39.0 : le compteur a bouge - la synchronisation complete tout de suite
    bool               waiting{false};     // une demande manuelle attend
    lic::Seconds       waitingSince{0};    // 1.36.0 : envoyee le (la cadence de la verification)
    bool               retry{false};       // un etat << poste >> a reprendre
    std::atomic<bool>  heldBack{false};    // le poste flottant rendu par l'utilisateur
    std::atomic<lic::Seconds> freeAt{0};   // flottante sans poste : quand un poste se libere
    bool               leaseTaken{false};  // ce programme a pris un jeton (a rendre a la fermeture)

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
    // Ce que le fil et ~Runner touchent est construit AVANT le Runner : detruit APRES lui.
    (void)httpMutex();
    (void)addressMutex();
    (void)addressOverride();
    (void)certMutex();
    (void)certCache();
    (void)certKeyCache();
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

double requestCheckDelay(lic::Seconds age) noexcept {
    if (age < 2 * 3600) return 60.0;
    return 300.0;
}

void startRunner() {
    Runner& r = runner();
    r.on = true;
    const double now = steadyNow();
    r.synced = false;
    r.nextSync = now;   // tout de suite (au premier poll)
    r.nextRenew = now + kHour;
    r.nextLease = now;
    r.nextDevice = now + kHour;
    r.nextTrial = now + 24.0 * kHour;
    r.nextLook = 0;
}

bool startSync() {
    if (!available()) return false;
    return launch(Job::Sync, [] { return sync(); });
}
bool startTime() {
    if (!available()) return false;
    return launch(Job::Time, [] { return readTime(); });
}
bool startActivate(const std::string& code, const std::string& pcName) {
    return launch(Job::Activate, [code = std::string(code), pcName]() mutable {
        Result r = activate(code, pcName);
        crypto::wipe(code.data(), code.size());
        return r;
    });
}
bool startRenew() { return launch(Job::Renew, [] { return renew(); }); }
bool startRelease(const std::string& licenseId) {
    return launch(Job::Release, [licenseId] { return release(licenseId); });
}
bool startTrial() { return launch(Job::Trial, [] { return trial(); }); }
bool startTakeLease() {
    runner().heldBack = false;
    return launch(Job::LeaseTake, [] { return takeLease(); });
}
bool startReturnLease() {
    const bool ok = launch(Job::LeaseReturn, [] { return returnLease(); });
    if (ok) runner().heldBack = true;
    return ok;
}
bool startRequest(const RequestForm& form) {
    return launch(Job::Request, [form] { return sendRequest(form); });
}
bool startCancelRequest() { return launch(Job::CancelRequest, [] { return cancelRequest(); }); }
bool startCheckDevice() { return launch(Job::Device, [] { return checkDevice(); }); }
bool startWatch() { return launch(Job::Watch, [] { return watchState(); }); }

bool busy() noexcept { return runner().running; }

Job busyJob() noexcept {
    const Job j = runner().running ? runner().job.load() : Job::None;
    return j == Job::Watch ? Job::None : j;   // 1.39.0 : le regard ne se montre pas (les boutons restent permis)
}

bool leaseHeldBack() noexcept { return runner().heldBack; }

lic::Seconds seatFreeAt() noexcept { return runner().freeAt; }

std::string jobText(Job j) {
    switch (j) {
        case Job::Sync: return "Synchronisation avec le service\xE2\x80\xA6";
        case Job::Time: return "Lecture de l'heure du service\xE2\x80\xA6";
        case Job::Activate: return "Activation en cours\xE2\x80\xA6";
        case Job::Renew: return "Renouvellement en cours\xE2\x80\xA6";
        case Job::Release: return "Lib\xC3\xA9ration en cours\xE2\x80\xA6";
        case Job::Trial: return "D\xC3\xA9" "claration de la D\xC3\xA9" "couverte\xE2\x80\xA6";
        case Job::LeaseTake: return "Prise d'un poste flottant\xE2\x80\xA6";
        case Job::LeaseReturn: return "Retour du poste flottant\xE2\x80\xA6";
        case Job::Request: return "Envoi de la demande\xE2\x80\xA6";
        case Job::CancelRequest: return "Annulation de la demande\xE2\x80\xA6";
        case Job::Device: return "V\xC3\xA9rification aupr\xC3\xA8s du service\xE2\x80\xA6";
        case Job::Watch:
        case Job::None: break;
    }
    return {};
}

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
    const double now = steadyNow();
    // 1.39.0 : le regard - rien a dire, rien a revaluer ; le compteur des changements (ou le registre) a bouge depuis
    // le dernier vu : la synchronisation complete tout de suite (la licence perdue en direct, les offres changees).
    if (res.job == Job::Watch) {
        if (res.reached) {
            const bool moved = (r.knownChanges >= 0 && res.serviceChanges >= 0 && res.serviceChanges != r.knownChanges) ||
                               (res.serviceRegistry > 0 && static_cast<std::uint64_t>(res.serviceRegistry) > lic::getLicenseState().registryNumber);
            if (res.serviceChanges >= 0) r.knownChanges = res.serviceChanges;
            if (moved || !r.synced) r.syncNow = true;   // ou la synchro du lancement n'avait pas joint le service
        }
        return true;
    }
    if (res.reached && res.serviceChanges >= 0) r.knownChanges = res.serviceChanges;
    if (res.job == Job::Sync || res.job == Job::Device || res.job == Job::Request || res.job == Job::CancelRequest) {
        const bool was = r.waiting;
        r.waiting = res.pendingAfter;
        r.waitingSince = res.requestSentAt;
        // 1.36.0 : une demande qui attend : sa reponse regardee souvent (requestCheckDelay) ; une demande qui vient de
        // partir (ou de se voir, au lancement) : la premiere verification dans une minute.
        if (r.waiting && (!was || res.job != Job::Device))
            r.nextDevice = now + requestCheckDelay(lic::systemNow() - r.waitingSince);
    }
    if (res.job == Job::Sync || res.job == Job::Device) r.retry = res.retryLater;
    if (res.job == Job::Sync) {
        r.nextWatch = now + kWatchPeriod;
        r.synced = res.reached;
        r.nextSync = now + (res.reached ? kHour : 15.0 * 60.0);   // hors ligne : un nouvel essai dans un quart d'heure
        if (res.reached && !r.waiting) r.nextDevice = now + kHour;
    }
    if (res.job == Job::Renew && res.reached) r.nextRenew = now + kHour;
    if (res.job == Job::LeaseTake) {
        if (res.ok) {
            r.leaseTaken = true;
            r.freeAt = 0;
            r.nextLease = now + kLeasePeriod;
            bgtasks::withdraw("licence:flottante");
        } else if (res.noSeat) {
            const int wait = std::clamp(res.retryAfter, 60, 900);
            r.freeAt = res.retryAfter > 0 ? lic::systemNow() + res.retryAfter : 0;
            r.nextLease = now + wait;
        } else {
            r.nextLease = now + 120.0;
        }
    }
    if (res.job == Job::LeaseReturn && res.ok) r.leaseTaken = false;
    if (res.job == Job::Activate && res.ok) {
        r.nextLease = now;   // une licence flottante : son poste tout de suite
        r.heldBack = false;
    }
    // Les avis : la cloche et la barre d'etat ; puis l'etat revalue.
    if (res.changed) licence::refresh();
    // 1.36.0 : la reponse de l'editeur a la demande de ce PC : la fenetre qui la confirme (l'application la montre).
    if (!res.answer.empty()) {
        licence::Answer a;
        a.kind = res.answer;
        a.licenseId = res.licenseId;
        a.note = res.answerNote;
        a.online = res.answerOnline;   // 1.37.0
        a.code = res.answerCode;
        a.mail = res.answerMail;
        a.mailTo = res.answerMailTo;
        if (res.answerMail == lgw::kMailSent) licence::noteEmailReached(res.answerMailTo);   // 1.38.0 : Mon profil, verifie
        licence::queueAnswer(std::move(a));
        crypto::wipe(res.answerCode.data(), res.answerCode.size());   // le code : dans la fenetre seulement
        res.answerCode.clear();
    }
    for (const auto& n : res.notices) {
        licence::postNotice(n.key, n.title, res.message == n.title ? std::string() : res.message, n.tone.empty() ? "info" : n.tone);
        licence::queueMessage(n.title);
        if (n.lost) licence::queueLoss(n.licenseId, n.title);   // 1.39.0 : la licence perdue en direct - une fenetre
    }
    r.last = std::move(res);
    ++r.revision;
    return true;
}

} // namespace

void poll() {
    Runner& r = runner();
    if (collect() || r.running) return;
    if (!r.on) return;
    // Ce qu'il y a a faire : regarde une fois par seconde (l'etat de la licence n'est pas relu a chaque image).
    const double now = steadyNow();
    if (now < r.nextLook) return;
    r.nextLook = now + 1.0;
    if (!available()) return;
    if (now >= r.nextSync && !r.synced) {
        r.nextSync = now + 15.0 * 60.0;
        (void)startSync();
        return;
    }
    // 1.39.0 : le service a signale un changement (le regard) : la synchronisation complete, tout de suite.
    if (r.syncNow) {
        r.syncNow = false;
        r.nextWatch = now + kWatchPeriod;
        (void)startSync();
        return;
    }
    const auto st = lic::getLicenseState();
    // 1.39.0 : une licence sur ce PC (en ligne, par fichier, flottante) : un regard sur GET /v1/etat toutes les 2 minutes.
    if (!st.licenseId.empty() && now >= r.nextWatch) {
        r.nextWatch = now + kWatchPeriod;
        (void)startWatch();
        return;
    }
    // La licence flottante : son jeton, renouvele toutes les 5 minutes (sauf rendu par l'utilisateur).
    if (st.floating && !r.heldBack && now >= r.nextLease) {
        r.nextLease = now + kLeasePeriod;
        (void)startTakeLease();
        return;
    }
    // Le bail : renouvele chaque heure s'il finit dans 7 jours (ou s'il est fini : la connexion revenue).
    if (st.online && now >= r.nextRenew) {
        r.nextRenew = now + kHour;
        if (st.activationWarning || st.activationDaysLeft <= 7 || st.status == lic::LicenseStatus::LeaseExpired) {
            (void)startRenew();
            return;
        }
    }
    // Une demande manuelle qui attend : toutes les minutes au debut (1.36.0 : requestCheckDelay) ; un etat a
    // reprendre : une verification par heure.
    if ((r.waiting || r.retry) && now >= r.nextDevice) {
        r.nextDevice = now + (r.waiting ? requestCheckDelay(lic::systemNow() - r.waitingSince) : kHour);
        (void)startCheckDevice();
        return;
    }
    // La Decouverte : une fois par jour tant qu'elle court.
    if (st.status == lic::LicenseStatus::Evaluation && now >= r.nextTrial) {
        r.nextTrial = now + 24.0 * kHour;
        (void)startTrial();
        return;
    }
}

// ============================================================ le poste flottant au lancement ==
bool startupLease(const lic::StoreOptions& options, int waitMs) {
    if (address().empty() || options.folder.empty()) return false;
    // Une licence flottante en ligne sur ce PC ? (son activation ; rien n'est verifie ici : le magasin le fera)
    const auto readText = [](const std::string& path, std::string& out) {
        crypto::Bytes bytes;
        std::string w;
        if (!crypto::readFile(path, bytes, &w, 1024u * 1024u)) return false;
        out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };
    std::string text, lid;
    {
        lic::SignedBlock b;
        lsv::Activation a;
        if (!readText(deploy::joinPath(options.folder, lic::LicenseStore::kActivationFile), text) ||
            !lsv::findBlock(text, lic::DocKind::Activation, b, nullptr) || !lsv::parseActivation(b.document, a, nullptr) || !a.floating)
            return false;
        lid = a.licenseId;
    }
    // Un jeton encore bon (un lancement juste apres une fermeture) : rien a faire maintenant (le fil de fond le renouvelle).
    if (readText(deploy::joinPath(options.folder, lic::LicenseStore::kLeaseFile), text)) {
        lic::SignedBlock b;
        lsv::Lease l;
        if (lsv::findBlock(text, lic::DocKind::Lease, b, nullptr) && lsv::parseLease(b.document, l, nullptr) && l.licenseId == lid &&
            l.until > (options.clock ? options.clock() : lic::systemNow()) + 120)
            return false;
    }
    // Un magasin a part (le module n'est pas encore configure) : la cle de ce PC, l'heure signee, le jeton.
    auto store = std::make_shared<lic::LicenseStore>(options);
    if (!store->configured()) return false;
    auto done = std::make_shared<std::promise<bool>>();
    auto result = done->get_future();
    std::thread worker([store, lid, done] {
        bool ok = false;
        Conn c;
        c.a = address();
        c.host = hostOf(c.a.url);
        json::Value etat;
        std::string why, w;
        if (readState(c, etat, why) && etat.flag("certifie") && fetchCert(c, etat.str("cle_service"), why, &store->trustedKeys())) {
            std::uint8_t n[16];
            crypto::randomBytes(n, sizeof n);
            const std::string nonce = crypto::hex(n, sizeof n);
            const Got t = getPublic(c, "/v1/heure?n=" + nonce);
            if (t.reached() && t.status == 200) (void)store->noteServiceTime(t.body, nonce, c.cert, &w);
            const DeviceSigner signer{[store](crypto::PublicKey& pk, std::string& e) {
                                          std::string hex;
                                          crypto::Bytes kb;
                                          if (!store->deviceKey(hex, &e) || !crypto::unhex(hex, kb, pk.size())) return false;
                                          std::copy(kb.begin(), kb.end(), pk.begin());
                                          return true;
                                      },
                                      [store](std::string_view m, crypto::Signature& sig, crypto::PublicKey& pub, std::string& e) {
                                          return store->deviceSignServiceMessage(m, sig, pub, nullptr, &e);
                                      }};
            json::Value body = json::Value::object();
            body.set("licence", lid);
            body.set("station", store->thisStationHash());
            const Got g = callSigned(c, "POST", "/v1/flottante/jeton", json::write(body, false), why, 3, 4, signer);
            json::Value v;
            if (g.reached() && g.status == 200 && json::parse(g.body, v) && v.isObject()) ok = store->installLease(field(v, "jeton"), c.cert, &w);
            else if (g.reached() && (g.status == 409 || g.status == 403 || g.status == 410)) store->dropLease();
        }
        done->set_value(ok);
    });
    bool ok = false;
    if (result.wait_for(std::chrono::milliseconds(std::max(100, waitMs))) == std::future_status::ready) {
        ok = result.get();
    } else {
        g_stopping = true;   // trop long (un reseau lent) : la requete en cours est arretee ; le fil de fond reprendra
        std::lock_guard<std::mutex> lock(httpMutex());
        if (g_active) g_active->cancel();
    }
    worker.join();
    g_stopping = false;
    if (ok) runner().leaseTaken = true;   // a rendre a la fermeture
    return ok;
}

Result lastResult() { return runner().last; }

unsigned long long revision() noexcept { return runner().revision.load(); }

void shutdown() {
    Runner& r = runner();
    const bool returnLease = r.on && r.leaseTaken;
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
    // Le poste flottant pris par ce programme : rendu, au mieux (un fil, 3 secondes au plus ; sinon il se libere de
    // lui-meme a la fin du jeton).
    const std::string lid = floatingLicense();
    if (returnLease && !lid.empty() && available() && lic::getLicenseState().leaseUntil > 0) {
        std::promise<void> done;
        auto fut = done.get_future();
        std::thread t([lid, &done] {
            Conn c;
            c.a = address();
            c.host = hostOf(c.a.url);
            Result res;
            std::vector<std::string> lines;
            (void)stepReturnLease(c, lid, res, lines, 3, 3, /*dropLocal=*/false);
            done.set_value();
        });
        if (fut.wait_for(std::chrono::seconds(4)) != std::future_status::ready) {
            g_stopping = true;
            std::lock_guard<std::mutex> lock(httpMutex());
            if (g_active) g_active->cancel();
        }
        t.join();
        g_stopping = false;
    }
    r.leaseTaken = false;
}

} // namespace app::licence::service
