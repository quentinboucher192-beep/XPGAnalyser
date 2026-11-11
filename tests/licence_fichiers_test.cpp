// =============================================================================
//  tests/licence_fichiers_test.cpp - 1.44.0 : LES FICHIERS, LES MESSAGES A UN
//  PC, LES VERSIONS ET LES ADRESSES DE SECOURS COTE STUDIO (app/FileDownload,
//  app/LicenceService, app/Updates ; docs/ADMINS.md 6, 7 et 10), contre le
//  VRAI Worker lance sur ce poste (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  L'essai joue l'EDITEUR (une cle d'essai : il certifie le service, depose le
//  registre, des fichiers en morceaux, des messages, une version) et des PC (un
//  dossier de licences chacun). On verifie :
//    - le registre et ses adresses de secours (fallback_services) : ecrites
//      seulement s'il y en a, lues et controlees ;
//    - un fichier public telecharge et verifie (manifeste signe par l'editeur,
//      taille, SHA-256), deja la ensuite ; REPRIS apres une coupure (seuls les
//      morceaux manquants) ; REFUSE quand un morceau est modifie en route, quand
//      le manifeste n'est pas celui annonce, ou signe par une autre cle (rien
//      n'est garde) ;
//    - un fichier << jeton >> (sans : refuse par le service ; avec : verifie) ;
//      un fichier << admins >> (sans : refuse ; avec le jeton d'admin en
//      X-XPG-Jeton : verifie) ; le jeton d'admin lu par le Studio (cru, expire,
//      d'une cle inconnue) ;
//    - un message a un PC : releve et ouvert par CE PC seulement, une fois ; sa
//      notification << avec fichiers >>, le fichier telecharge et verifie depuis
//      elle ; un fichier annonce qui ne correspond pas : << Fichier refuse >> ; un
//      message d'une autre cle, pour un autre poste, scelle pour un autre : refuse ;
//    - une version publiee plus recente : vue, annoncee une fois, la notification,
//      le toast, << Plus tard >> et la pastille ; le PC d'un admin (2 fichiers,
//      << Ton Admin ... n'est plus a jour >>) ; les lignes de l'installateur ;
//    - l'adresse principale qui ne repond pas : l'adresse de secours du registre,
//      son certificat qui nomme SON adresse (une licence s'active par elle) ; sans
//      adresse de secours : hors ligne ; un secours dont le certificat nomme une
//      autre adresse : refuse.
//  Node.js ou wrangler absents (le service ne se lance pas) : SAUTE, code 0.
// =============================================================================
#include "service_local.hpp"

#include "../src/app/FileDownload.hpp"
#include "../src/app/Licence.hpp"
#include "../src/app/LicenceService.hpp"
#include "../src/app/Notices.hpp"
#include "../src/app/Updates.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/core/Version.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/Service.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace lsv = licensing::service;
namespace lgw = licensing::gateway;
namespace adm = license_admin;
namespace ladm = licensing::admins;
namespace json = core::json;
namespace ls = app::licence::service;
namespace ai = app::ai;
namespace dl = app::filedl;
namespace up = app::updates;
using lic::Seconds;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
bool present(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

constexpr Seconds D = lic::kDay;

fs::path                         g_base;
std::string                      g_url;
std::unique_ptr<lic::SigningKey> g_editor;
std::unique_ptr<lic::SigningKey> g_stranger;   // une cle qui n'est pas celle de l'editeur
lic::Registry                    g_reg;
std::string                      g_registry;

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

std::string block(lic::DocKind kind, const std::string& doc, const lic::SigningKey& key) {
    return lic::writeBlock(lic::signBlock(kind, doc, key.publicKey(), [&key](std::string_view m) { return key.sign(m); }));
}
std::string block(lic::DocKind kind, const std::string& doc) { return block(kind, doc, *g_editor); }

// ---- l'editeur : ses appels au service ----
struct Http {
    bool        reached{false};
    int         status{0};
    std::string body;
    json::Value v;
};
std::shared_ptr<ai::HttpClient> g_system;

Http rawHttp(const std::string& method, const std::string& path, const std::string& body, bool sign, Seconds at = 0) {
    ai::HttpRequest req;
    req.method = method;
    req.url = g_url + path;
    req.headers = {{"Content-Type", "application/json"}};
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (sign)
        for (auto& h : lsv::signRequest(method, path, body, g_editor->publicKey(), [](std::string_view m) { return g_editor->sign(m); }, at))
            req.headers.push_back(h);
    Http out;
    ai::HttpSink sink;
    sink.onData = [&out](std::string_view c) { out.body.append(c); };
    const auto r = g_system->perform(req, sink);
    out.reached = r.ok();
    out.status = r.status;
    (void)json::parse(out.body, out.v);
    return out;
}
Seconds serviceTime() {
    const Http h = rawHttp("GET", "/v1/etat", "", false);
    return h.v.isObject() ? static_cast<Seconds>(h.v.integer("heure")) : lic::systemNow();
}
Http http(const std::string& method, const std::string& path, const std::string& body, bool sign) {
    return rawHttp(method, path, body, sign, sign ? serviceTime() : 0);
}
std::string jsonBody(std::initializer_list<std::pair<const char*, json::Value>> fields) {
    json::Value v = json::Value::object();
    for (const auto& [k, x] : fields) v.set(k, x);
    return json::write(v, false);
}

bool publisherSetup(std::string& why) {
    const Http k = http("GET", "/admin/cle-service", "", true);
    if (!k.reached || k.status != 200 || k.v.str("cle").size() != 64) {
        why = "GET /admin/cle-service : " + std::to_string(k.status) + " " + k.body;
        return false;
    }
    lsv::ServiceCert c;
    c.key = k.v.str("cle");
    c.url = g_url;
    const Seconds now = serviceTime();
    c.issuedAt = now - 30 * D;
    c.expiresAt = now + 3650 * D;
    c.rights = {lsv::kRightActivation, lsv::kRightFloating, lsv::kRightTime, lsv::kRightTrial};
    const Http p = http("PUT", "/admin/certificat", jsonBody({{"certificat", block(lic::DocKind::ServiceCert, lsv::writeCert(c))}}), true);
    if (p.status != 200) {
        why = "PUT /admin/certificat : " + std::to_string(p.status) + " " + p.body;
        return false;
    }
    const Http r = http("PUT", "/admin/registre", jsonBody({{"registre", g_registry}}), true);
    if (r.status != 200) {
        why = "PUT /admin/registre : " + std::to_string(r.status) + " " + r.body;
        return false;
    }
    return true;
}

// Un fichier depose en morceaux (le manifeste signe, puis chaque morceau) ; rend son manifeste.
std::string randomHex(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes);
    deploy::crypto::randomBytes(b.data(), b.size());
    return deploy::crypto::hex(b.data(), b.size());
}
std::string randomBytes(std::size_t n) {
    std::string s(n, '\0');
    deploy::crypto::randomBytes(reinterpret_cast<std::uint8_t*>(s.data()), s.size());
    return s;
}
ladm::FileManifest upload(const std::string& name, const std::string& bytes, const std::string& kind, const std::string& access,
                          const std::string& version = "1.0.0") {
    ladm::FileManifest m;
    m.id = randomHex(16);
    m.name = name;
    m.size = bytes.size();
    m.sha256 = deploy::crypto::sha256Hex(bytes);
    m.kind = kind;
    m.version = version;
    m.access = access;
    m.issued = serviceTime();
    const Http h = http("POST", "/admin/fichiers", jsonBody({{"manifeste", block(lic::DocKind::FileManifest, ladm::writeFileManifest(m))}}), true);
    check(h.status == 201, "l'editeur depose le manifeste de " + name + " (" + std::to_string(h.status) + " " + h.body.substr(0, 120) + ")");
    for (std::uint64_t n = 0; n < m.chunks(); ++n) {
        const std::string part = bytes.substr(static_cast<std::size_t>(n * m.chunk), static_cast<std::size_t>(m.chunk));
        const Http c = http("PUT", "/admin/fichiers/morceau?id=" + m.id + "&n=" + std::to_string(n), part, true);
        if (c.status != 200) check(false, "le morceau " + std::to_string(n) + " de " + name + " : " + std::to_string(c.status) + " " + c.body.substr(0, 120));
    }
    return m;
}

dl::Want wantOf(const ladm::FileManifest& m) {
    dl::Want w;
    w.id = m.id;
    w.name = m.name;
    w.size = m.size;
    w.sha256 = m.sha256;
    w.version = m.version;
    return w;
}

// ---- un transport qui regarde (et modifie) les reponses : un morceau modifie en route ----
std::mutex                                              g_tamperMutex;
std::function<void(const std::string&, std::string&)>   g_tamper;   // l'adresse, le corps recu (a modifier)
std::vector<std::string>                                g_urls;     // les adresses demandees

class TamperClient final : public ai::HttpClient {
public:
    ai::HttpResponse perform(const ai::HttpRequest& req, const ai::HttpSink& sink) override {
        std::function<void(const std::string&, std::string&)> tamper;
        {
            std::lock_guard<std::mutex> lock(g_tamperMutex);
            g_urls.push_back(req.url);
            tamper = g_tamper;
        }
        if (!tamper) return g_system->perform(req, sink);
        std::string body;
        ai::HttpSink mine;
        mine.onHeaders = sink.onHeaders;
        mine.onData = [&body](std::string_view c) { body.append(c); };
        ai::HttpResponse r = g_system->perform(req, mine);
        tamper(req.url, body);
        if (sink.onData && !body.empty()) sink.onData(body);
        return r;
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    std::atomic<bool> cancelled_{false};
};
void watchHttp(std::function<void(const std::string&, std::string&)> tamper) {
    {
        std::lock_guard<std::mutex> lock(g_tamperMutex);
        g_tamper = std::move(tamper);
        g_urls.clear();
    }
    ai::setHttpFactory([] { return std::unique_ptr<ai::HttpClient>(new TamperClient()); });
}
void unwatchHttp() {
    ai::setHttpFactory(nullptr);
    std::lock_guard<std::mutex> lock(g_tamperMutex);
    g_tamper = nullptr;
}
std::size_t urlsWith(const std::string& part) {
    std::lock_guard<std::mutex> lock(g_tamperMutex);
    std::size_t n = 0;
    for (const auto& u : g_urls) n += has(u, part) ? 1 : 0;
    return n;
}

// ---- les PC ----
fs::path folderOf(const std::string& name) { return g_base / "pcs" / name; }
void usePc(const std::string& name, const std::string& serviceUrl, const std::string& bundled) {
    lic::StoreOptions o;
    o.folder = U(folderOf(name));
    o.trustedKeys = {trustedOf(*g_editor)};
    o.compiledKeys = false;
    o.bundledRegistry = bundled;
    o.machineId = "machine-" + name;
    o.evaluationPeriod = true;
    o.serviceUrl = serviceUrl;
    app::licence::start(o);
}
std::string pcDevice() {
    std::string hex, why;
    (void)lic::deviceKeyHex(hex, &why);
    app::licence::refresh();   // l'empreinte de la cle (le releve des messages la demande)
    deploy::crypto::Bytes raw;
    lic::crypto::PublicKey pk{};
    if (!deploy::crypto::unhex(hex, raw, pk.size())) return {};
    std::copy(raw.begin(), raw.end(), pk.begin());
    return lsv::deviceId(pk);
}
lic::crypto::PublicKey pcKey() {
    std::string hex, why;
    (void)lic::deviceKeyHex(hex, &why);
    deploy::crypto::Bytes raw;
    lic::crypto::PublicKey pk{};
    if (deploy::crypto::unhex(hex, raw, pk.size())) std::copy(raw.begin(), raw.end(), pk.begin());
    return pk;
}

// Un registre signe (plus recent que celui du service), avec ses adresses de secours.
std::string registryWith(std::uint64_t number, std::vector<std::string> fallbacks) {
    lic::Registry r = g_reg;
    r.number = number;
    r.issuedAt = lic::systemNow() - 60;
    r.fallbackServices = std::move(fallbacks);
    return block(lic::DocKind::Registry, lic::writeRegistry(r));
}

// Un message a un poste (son contenu scelle pour `to`).
std::string messageBlock(const std::string& device, const lic::crypto::PublicKey& to, const std::string& title, const ladm::MessageContent& c,
                         const lic::SigningKey& signer) {
    ladm::Message m;
    m.device = device;
    m.title = title;
    m.from = "Camille";
    m.issued = lic::systemNow();
    m.sealed = lgw::sealFor(to, ladm::writeMessageContent(c));
    return block(lic::DocKind::Message, ladm::writeMessage(m), signer);
}

ladm::MessageFile messageFile(const ladm::FileManifest& m, const std::string& token = {}) {
    ladm::MessageFile f;
    f.id = m.id;
    f.name = m.name;
    f.size = m.size;
    f.sha256 = m.sha256;
    f.version = m.version;
    f.token = token;
    return f;
}

std::string nextVersion() {
    int a = 0, b = 0, c = 0;
    (void)ladm::parseSemver(XPG_ANALYZER_VERSION, a, b, c);
    return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c + 1);
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    g_base = fs::temp_directory_path() / ("xpg-licence-fichiers-" + randomHex(6));
    std::error_code ec;
    fs::remove_all(g_base, ec);
    fs::create_directories(g_base, ec);
    std::string why;
    g_editor = adm::createPublisherKey(U(g_base / "editeur"), "essai fichiers Studios", true, &why);
    g_stranger = adm::createPublisherKey(U(g_base / "autre"), "une autre cle", true, &why);
    check(g_editor && g_stranger, "les cles d'essai (" + why + ")");
    if (!g_editor || !g_stranger) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    check(adm::signRegistry(*g_editor, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), lic::systemNow() - 60, g_registry, &why),
          "le registre signe (" + why + ")");
    {
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(g_registry, blocks, &why) && blocks.size() == 1 && lic::parseRegistry(blocks[0].document, g_reg, &why),
              "le registre se relit (" + why + ")");
    }

    std::printf("-- le registre : les adresses de secours (fallback_services)\n");
    {
        const std::string plain = lic::writeRegistry(g_reg);
        check(!has(plain, "fallback_services"), "sans adresse de secours : le champ n'est pas ecrit (le registre d'avant, octet pour octet)");
        lic::Registry r = g_reg;
        r.fallbackServices = {"https://secours-1.exemple.dev", "https://secours-2.exemple.dev:8443"};
        lic::Registry back;
        check(lic::parseRegistry(lic::writeRegistry(r), back, &why) && back.fallbackServices == r.fallbackServices, "ecrites et relues (" + why + ")");
        const auto refuse = [&](std::vector<std::string> urls, const std::string& what) {
            lic::Registry x = g_reg;
            x.fallbackServices = std::move(urls);
            lic::Registry y;
            std::string w;
            check(!lic::parseRegistry(lic::writeRegistry(x), y, &w), what + " : refuse (" + w + ")");
        };
        refuse({"http://secours.exemple.dev"}, "http hors de ce poste");
        refuse({"https://secours.exemple.dev/chemin"}, "une adresse avec un chemin");
        refuse({"https://a.dev", "https://b.dev", "https://c.dev", "https://d.dev"}, "4 adresses");
        refuse({"https://a.dev", "https://a.dev/"}, "deux fois la meme");
    }

    // ---- le service, sur ce poste ----
    xpg_test::LocalService svc({g_editor->publicKeyHex()});
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        fs::remove_all(g_base, ec);
        return 0;
    }
    check(svc.ready(), "le service local demarre (" + svc.error() + ")");
    if (!svc.ready()) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    g_url = svc.url();
    std::printf("   (service local : %s, %.1f s)\n", g_url.c_str(), svc.startSeconds());
    g_system = std::shared_ptr<ai::HttpClient>(ai::makeHttpClient().release());
    check(publisherSetup(why), "l'editeur certifie le service, depose le registre (" + why + ")");
    lsv::Address address;
    address.url = g_url;
    const std::string dead = "http://127.0.0.1:1";
    lsv::Address deadAddress;
    deadAddress.url = dead;

    const fs::path bundled = g_base / "livre" / "registre.xpgreg";
    writeFile(bundled, g_registry);
    up::setAdminFolderForTests(U(g_base / "pas-admin"));   // ce compte n'est pas admin (pas le dossier de l'Admin de ce poste)
    usePc("A", g_url, U(bundled));
    ls::setAddressForTests(address);
    app::licence::startNotices(false);   // les avis (sans le fil de fond) : les notifications des messages et de la mise a jour
    app::notices::resetForTests();
    dl::setRootForTests(U(g_base / "telecharges"));

    // ======================================================== les fichiers ==
    std::printf("-- un fichier public : telecharge, verifie ; deja la ; repris\n");
    const std::string toolBytes = randomBytes(2 * 1048576 + 300000);   // 3 morceaux (le dernier : le reste)
    const auto tool = upload("Outil-Essai-1.0.0.exe", toolBytes, "autre", "public");
    {
        const auto o = dl::download(wantOf(tool));
        check(o.ok && o.manifestVerified && o.hashVerified && !o.refused && readFile(fs::path(o.path)) == toolBytes,
              "telecharge et verifie (" + o.message + ")");
        check(has(o.message, "sign\xC3\xA9 par l'\xC3\xA9" "diteur") && present(fs::path(o.path)) && !present(fs::path(o.path + ".part")),
              "la phrase ; le fichier range, pas de .part");
        check(dl::verifiedCopy(wantOf(tool)).has_value() && dl::presentCopy(wantOf(tool)), "la copie verifiee est la");
        watchHttp(nullptr);
        const auto again = dl::download(wantOf(tool));
        check(again.ok && has(again.message, "d\xC3\xA9j\xC3\xA0") && urlsWith("/v1/fichiers/") == 0, "deja la : rien n'est redemande");
        unwatchHttp();
        // une coupure apres le premier morceau (et quelques octets du second) : la reprise ne redemande que ce qui manque
        fs::remove(fs::path(o.path), ec);
        writeFile(fs::path(o.path + ".part"), toolBytes.substr(0, 1048576 + 1000));
        watchHttp([](const std::string&, std::string&) {});
        const auto resumed = dl::download(wantOf(tool));
        check(resumed.ok && readFile(fs::path(resumed.path)) == toolBytes, "repris et verifie (" + resumed.message + ")");
        check(urlsWith("&n=0") == 0 && urlsWith("&n=1") == 1 && urlsWith("&n=2") == 1, "repris : seulement les morceaux 1 et 2 (" +
                                                                                            std::to_string(urlsWith("morceau")) + " demandes)");
        unwatchHttp();
        check(dl::sizeText(tool.size) == "2,3 Mo" && dl::shortHash(tool.sha256).size() > 20, "la taille et l'empreinte lisibles : " + dl::sizeText(tool.size));
    }

    std::printf("-- un fichier modifie en route, un manifeste qui ne correspond pas : refuses, rien n'est garde\n");
    {
        fs::remove_all(g_base / "telecharges", ec);
        watchHttp([](const std::string& url, std::string& body) {
            if (has(url, "morceau") && has(url, "&n=1") && !body.empty()) body[body.size() / 2] = static_cast<char>(body[body.size() / 2] ^ 0x5A);
        });
        const auto o = dl::download(wantOf(tool));
        unwatchHttp();
        const std::string target = dl::finalPath(wantOf(tool));
        check(!o.ok && o.refused && has(o.message, "la signature ne correspond pas : il a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 en route"),
              "un octet change dans un morceau : refuse (" + o.message + ")");
        check(!present(fs::path(target)) && !present(fs::path(target + ".part")), "ni le fichier, ni le .part");
        bool hist = false;
        for (const auto& h : lic::getHistory(10)) hist = hist || (h.kind == "service" && !h.ok && has(h.text, "Fichier refus"));
        check(hist, "une ligne de l'historique le dit");
        // ce que dit le document signe n'est pas ce que dit le manifeste
        dl::Want lie = wantOf(tool);
        lie.sha256 = std::string(64, 'a');
        watchHttp(nullptr);
        const auto o2 = dl::download(lie);
        check(!o2.ok && o2.refused && urlsWith("morceau") == 0, "une autre empreinte annoncee : refuse, aucun morceau demande");
        unwatchHttp();
        // un manifeste signe par une autre cle (remplace en route)
        ladm::FileManifest forged = tool;
        const std::string forgedText = block(lic::DocKind::FileManifest, ladm::writeFileManifest(forged), *g_stranger);
        watchHttp([&forgedText](const std::string& url, std::string& body) {
            if (has(url, "manifeste")) body = forgedText;
        });
        const auto o3 = dl::download(wantOf(tool));
        unwatchHttp();
        check(!o3.ok && o3.refused && !o3.manifestVerified, "un manifeste d'une autre cle : refuse (" + o3.message + ")");
        ladm::FileManifest m;
        std::string w;
        check(!dl::checkManifest(forgedText, wantOf(tool), m, w) && has(w, "cl\xC3\xA9 de l'\xC3\xA9" "diteur"), "checkManifest : " + w);
    }

    std::printf("-- un fichier << jeton >>, un fichier << admins >> (le jeton d'admin)\n");
    const auto linked = upload("Lien-Essai.bin", randomBytes(5000), "autre", "jeton");
    const std::string adminBytes = randomBytes(1048576 + 77);
    const auto adminSetup = upload("XPG-License-Admin-Setup-" + nextVersion() + ".exe", adminBytes, "admin", "admins", nextVersion());
    std::string adminTokenText;
    {
        const auto no = dl::download(wantOf(linked));
        check(!no.ok && !no.refused && has(no.message, "refuse"), "sans le jeton : le service refuse (" + no.message + ")");
        const Http j = http("POST", "/admin/fichiers/jeton", jsonBody({{"id", linked.id}, {"duree", 3600}, {"une_fois", false}}), true);
        check(j.status == 201 && j.v.str("jeton").size() == 32, "l'editeur tire un jeton (" + std::to_string(j.status) + ")");
        dl::Want w = wantOf(linked);
        w.token = j.v.str("jeton");
        const auto yes = dl::download(w);
        check(yes.ok && yes.hashVerified, "avec le jeton : verifie (" + yes.message + ")");

        const auto noAdmin = dl::download(wantOf(adminSetup));
        check(!noAdmin.ok && !noAdmin.refused && has(noAdmin.message, "admins"), "reserve aux admins, sans jeton : refuse (" + noAdmin.message + ")");
        ladm::AdminToken t;
        t.key = g_editor->publicKeyHex();
        t.expires = serviceTime() + 10 * D;
        t.version = "1.0.0";
        t.name = "L\xC3\xA9" "a";
        adminTokenText = block(lic::DocKind::AdminToken, ladm::writeAdminToken(t));
        const auto tok = up::checkAdminToken(adminTokenText, lic::systemNow());
        check(tok.ok && tok.token.version == "1.0.0" && tok.token.name == "L\xC3\xA9" "a" && !tok.base64.empty(), "le jeton d'admin est cru (" + tok.why + ")");
        dl::Want wa = wantOf(adminSetup);
        wa.kind = "admin";
        wa.adminToken = tok.base64;
        const auto withAdmin = dl::download(wa);
        check(withAdmin.ok && readFile(fs::path(withAdmin.path)) == adminBytes, "avec le jeton d'admin (X-XPG-Jeton) : verifie (" + withAdmin.message + ")");
        check(!up::checkAdminToken(adminTokenText, t.expires + 10).ok, "un jeton d'admin expire : pas cru");
        ladm::AdminToken s = t;
        s.key = g_stranger->publicKeyHex();
        const auto st = up::checkAdminToken(block(lic::DocKind::AdminToken, ladm::writeAdminToken(s), *g_stranger), lic::systemNow());
        check(!st.ok && has(st.why, "admin de la liste"), "un jeton d'une cle inconnue : pas cru (" + st.why + ")");
        const auto lie = up::checkAdminToken(block(lic::DocKind::AdminToken, ladm::writeAdminToken(t), *g_stranger), lic::systemNow());
        check(!lie.ok && has(lie.why, "sa cl\xC3\xA9"), "un jeton qui n'est pas signe par sa cle : pas cru (" + lie.why + ")");
    }

    // ======================================================== les messages ==
    std::printf("-- un message a CE PC : releve, ouvert, une fois ; sa notification, son fichier\n");
    const std::string devA = pcDevice();
    const auto keyA = pcKey();
    check(devA.size() == 32 && !lic::thisDeviceFingerprint().empty(), "le PC A a sa cle");
    std::string noticeKey;
    {
        ladm::MessageContent c;
        c.text = "Tu vas pouvoir g\xC3\xA9rer les licences avec l'Admin. Installe-le sur ce PC : il cr\xC3\xA9" "e ta cl\xC3\xA9.";
        c.files.push_back(messageFile(tool));
        c.action = "admin";
        const Http p = http("PUT", "/admin/messages", jsonBody({{"poste", devA}, {"bloc", messageBlock(devA, keyA, "Camille t'a ajout\xC3\xA9" "e comme admin", c, *g_editor)}}),
                            true);
        check(p.status == 201, "l'editeur depose le message (" + std::to_string(p.status) + " " + p.body.substr(0, 120) + ")");
        const auto r = ls::pollMessages();
        check(r.ok && r.messages.size() == 1, "releve : 1 message (" + r.message + ")");
        if (r.messages.size() == 1) {
            const auto& m = r.messages[0];
            check(m.title == "Camille t'a ajout\xC3\xA9" "e comme admin" && m.from == "Camille" && m.action == "admin" && has(m.text, "Installe-le"),
                  "ouvert : le titre, qui, le texte, l'action");
            check(m.files.size() == 1 && m.files[0].id == tool.id && m.files[0].manifestOk && m.files[0].refused.empty(),
                  "son fichier : le manifeste signe par l'editeur dit la meme chose");
        }
        const auto again = ls::pollMessages();
        check(again.ok && again.messages.empty() && has(again.message, "Aucun nouveau"), "releve de nouveau : rien de neuf (" + again.message + ")");
        ls::Record rec;
        check(ls::loadRecord(rec, &why) && !rec.messagesSeen.empty() && rec.messagesSeen[0].second > 0, "le releve garde le dernier numero vu");
        // la notification (le fil de l'interface)
        up::received(r.messages, std::nullopt, false);
        for (const auto& n : app::notices::list())
            if (n.key.rfind("message:", 0) == 0) noticeKey = n.key;
        const auto n = up::notice(noticeKey);
        check(n && n->title == "Camille t'a ajout\xC3\xA9" "e comme admin" && n->category == "Admin" && up::hasFiles(*n) &&
                  up::filesLine(*n) == "1 fichier \xC2\xB7 2,3 Mo" && n->action == up::toMessage(noticeKey),
              "la notification : avec fichiers, le bouton de sa fenetre (" + (n ? up::filesLine(*n) : std::string("absente")) + ")");
        // le fichier telecharge et verifie depuis la notification
        fs::remove_all(g_base / "telecharges", ec);
        check(up::fetchNoticeFile(noticeKey, tool.id, &why), "le telechargement part (" + why + ")");
        const auto t0 = std::chrono::steady_clock::now();
        while (!up::fetching().empty() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!dl::busy()) {
                // App::frame fait up::tick(app) ; ici, sans App : le resultat est pris comme lui
                std::string tag;
                if (auto outs = dl::take(&tag)) {
                    const auto& o = outs->front();
                    up::markFile(noticeKey, tool.id, o.ok, o.refused ? o.message : std::string());
                    check(o.ok, "le fichier du message : telecharge et verifie (" + o.message + ")");
                    break;
                }
            }
        }
        const auto d = up::dataOf(up::notice(noticeKey)->data);
        check(d && d->files.size() == 1 && d->files[0].hashOk && d->files[0].manifestOk, "la notification le dit : << Empreinte verifiee >>");
    }
    {
        // un autre PC ne le voit pas
        usePc("B", g_url, U(bundled));
        (void)pcDevice();
        const auto r = ls::pollMessages();
        check(r.ok && r.messages.empty(), "un autre PC : aucun message (le service ne donne que ceux de CE poste)");
        const std::string devB = pcDevice();
        const auto keyB = pcKey();
        // un fichier annonce qui ne correspond pas a son manifeste : << Fichier refuse >>
        ladm::MessageContent c;
        c.text = "Un outil de diagnostic.";
        ladm::MessageFile bad = messageFile(linked);
        bad.size += 1;   // l'editeur annonce une autre taille que celle du manifeste
        c.files.push_back(bad);
        const Http p = http("PUT", "/admin/messages", jsonBody({{"poste", devB}, {"bloc", messageBlock(devB, keyB, "Outil de diagnostic", c, *g_editor)}}), true);
        check(p.status == 201, "un message au PC B");
        const auto rb = ls::pollMessages();
        check(rb.messages.size() == 1 && rb.messages[0].files.size() == 1 && !rb.messages[0].files[0].refused.empty() && !rb.messages[0].files[0].manifestOk,
              "son fichier ne correspond pas : refuse des la reception");
        app::notices::resetForTests();
        up::received(rb.messages, std::nullopt, false);
        bool refusedNotice = false;
        for (const auto& n : app::notices::list())
            if (n.key == "fichier-refuse:" + linked.id && n.title == "Fichier refus\xC3\xA9" && n.tone == "error") refusedNotice = true;
        check(refusedNotice, "la notification << Fichier refuse >>");
        // ce que le Studio refuse de lui-meme (le service ne laisserait pas passer ces messages) : openMessage
        ls::InboxMessage out;
        std::string w;
        check(!ls::openMessage(messageBlock(devB, keyB, "Faux", c, *g_stranger), devB, out, w) && has(w, "cl\xC3\xA9 de l'\xC3\xA9" "diteur"),
              "un message d'une autre cle : refuse (" + w + ")");
        check(!ls::openMessage(messageBlock(devA, keyB, "Pour A", c, *g_editor), devB, out, w) && has(w, "autre poste"), "pour un autre poste : refuse (" + w + ")");
        check(!ls::openMessage(messageBlock(devB, keyA, "Scelle pour A", c, *g_editor), devB, out, w) && has(w, "ne s'ouvre pas"),
              "scelle pour un autre PC : refuse (" + w + ")");
        check(ls::openMessage(messageBlock(devB, keyB, "Pour B", c, *g_editor), devB, out, w) && out.title == "Pour B", "le bon : ouvert (" + w + ")");
    }

    // ======================================================== les versions ==
    std::printf("-- une version publiee plus recente : vue, annoncee une fois ; la pastille ; le PC d'un admin\n");
    usePc("A", g_url, U(bundled));
    app::notices::resetForTests();
    const std::string v = nextVersion();
    const std::string studiosBytes = randomBytes(700000);
    const auto studios = upload("API-IHM-Studio-Setup-" + v + ".exe", studiosBytes, "studios", "public", v);
    // 1.49.0 : la troisieme sorte - l'installateur de l'Admin+ (admin_plus) : la cle de l'editeur seule le telecharge
    const std::string plusBytes = randomBytes(3000);
    const auto plusSetup = upload("XPG-License-Admin-Setup-" + v + "-EDITEUR.exe", plusBytes, "admin_plus", "admins", v);
    {
        const auto noPlus = dl::download(wantOf(plusSetup));
        check(!noPlus.ok, "l'Admin+ sans jeton : refuse (" + noPlus.message + ")");
        dl::Want wp = wantOf(plusSetup);
        wp.kind = "admin_plus";
        wp.adminToken = up::checkAdminToken(adminTokenText, lic::systemNow()).base64;
        const auto withPlus = dl::download(wp);
        check(withPlus.ok && readFile(fs::path(withPlus.path)) == plusBytes, "l'Admin+ avec le jeton signe par la cle de l'editeur : verifie (" + withPlus.message + ")");
    }
    {
        const auto before = ls::checkUpdate();
        check(before.reached && !before.update && has(before.message, "\xC3\xA0 jour"), "rien de publie : a jour (" + before.message + ")");
        ladm::VersionManifest vm;
        vm.version = v;
        vm.published = serviceTime();
        vm.notes = {"Exports PDF : les grandes vues ne sont plus coup\xC3\xA9" "es.", "Simulation : elle d\xC3\xA9marre deux fois plus vite."};
        vm.files = {{studios.id, studios.name, studios.sha256, "studios", "tous", studios.size},
                    {adminSetup.id, adminSetup.name, adminSetup.sha256, "admin", "admins", adminSetup.size},
                    {plusSetup.id, plusSetup.name, plusSetup.sha256, "admin_plus", "admin_plus", plusSetup.size}};   // 1.49.0
        const Http p = http("PUT", "/admin/versions", jsonBody({{"bloc", block(lic::DocKind::Version, ladm::writeVersion(vm))}}), true);
        check(p.status == 201, "l'editeur publie la " + v + " (" + std::to_string(p.status) + " " + p.body.substr(0, 160) + ")");
        const auto r = ls::checkUpdate();
        check(r.update && r.update->version == v && r.update->announce && r.update->notes.size() == 2 && r.update->files.size() == 3,
              "vue : la mise a jour " + v + ", a annoncer (" + r.message + ")");
        if (r.update) {
            bool signedAll = true;
            for (const auto& f : r.update->files) signedAll = signedAll && f.manifestOk;
            check(signedAll, "ses fichiers : les manifestes signes par l'editeur disent la meme chose");
        }
        const auto w = ls::watchState();
        check(w.update && !w.update->announce, "le regard suivant : la meme version, pas annoncee deux fois");
        // le fil de l'interface : la notification, le toast ; Plus tard : la pastille
        up::received({}, r.update, true);
        const auto n = up::notice("maj:" + v);
        check(n && n->title == "Mise \xC3\xA0 jour " + v + " disponible" && up::hasFiles(*n) && n->action == up::toUpdate(v), "la notification << Mise a jour X disponible >>");
        check(up::toast() && up::toast()->title == "Mise \xC3\xA0 jour " + v + " disponible" && up::chipText().empty(), "le toast ; pas encore de pastille");
        const auto o = up::available();
        check(o && !o->adminPc && o->files().size() == 1 && o->files()[0].kind == "studios", "un client : les Studios seulement (1 fichier)");
        up::later();
        check(up::chipText() == v + " disponible" && !up::toast(), "Plus tard : la pastille << " + up::chipText() + " >>");
        // le PC d'un admin : son jeton dans le dossier de l'Admin
        up::setAdminFolderForTests(U(g_base / "admin-ici"));
        writeFile(g_base / "admin-ici" / up::kAdminTokenFile, adminTokenText);
        up::received({}, w.update, true);
        const auto oa = up::available();
        // 1.49.0 : ce jeton est signe par la cle de l'editeur - le PC de l'admin+ : les Studios et l'Admin+ (admin_plus), jamais
        // XPG Admin ; un admin (sa cle d'admin) recoit << admin >> (Offer::adminKind).
        check(oa && oa->adminPc && oa->adminOutdated && oa->adminKind == "admin_plus" && oa->files().size() == 2 && oa->admin() && oa->admin()->id == plusSetup.id,
              "le PC de l'admin+ : les 2 fichiers (les Studios, l'Admin+)");
        if (oa) check(has(up::adminLine(*oa), "Ton Admin 1.0.0 n'est plus \xC3\xA0 jour (L\xC3\xA9" "a"), "la phrase : " + up::adminLine(*oa));
        // les lignes de commande
        const std::string args = up::silentArguments("C:\\Programmes\\XPG\\IHM-Studio.exe", "D:\\Affaires\\Armoire Gaz");
        check(args == "/SILENT /SUPPRESSMSGBOXES /NORESTART /RELANCER=\"C:\\Programmes\\XPG\\IHM-Studio.exe\" /RELANCERPROJET=\"D:\\Affaires\\Armoire Gaz\"",
              "l'installateur des Studios : " + args);
        check(up::silentArguments("C:\\x.exe", "") == "/SILENT /SUPPRESSMSGBOXES /NORESTART /RELANCER=\"C:\\x.exe\"" &&
                  up::adminArguments() == "/SILENT /SUPPRESSMSGBOXES /NORESTART",
              "sans projet ; l'installateur de l'Admin");
        up::setAdminFolderForTests(U(g_base / "pas-admin"));
    }

    // ======================================================== les adresses de secours ==
    std::printf("-- l'adresse principale ne repond pas : l'adresse de secours du registre\n");
    {
        // une licence en ligne deposee chez le service ; le PC ne connait que l'adresse principale (morte) et, par son
        // registre signe, l'adresse de secours (le service local)
        lic::License l;
        l.licenseId = "XPG-2026-00444";
        l.customer = "Client d'essai Secours";
        l.edition = "PROFESSIONAL";
        l.type = lic::LicenseType::Perpetual;
        l.service = g_url;
        l.maxDevices = 2;
        if (const lic::EditionDef* e = g_reg.edition("PROFESSIONAL")) l.features = e->features;
        lic::normalizeGrants(l.features);
        l.issuedAt = serviceTime() - 3600;
        l.manifest = g_reg.number;
        l.nonce = lic::newNonce();
        const std::string code = lsv::newActivationCode();
        std::string norm;
        (void)lsv::normalizeCode(code, norm);
        const Http h = http("PUT", "/admin/licences",
                            jsonBody({{"licence", block(lic::DocKind::License, lic::writeLicense(l))}, {"code_hash", lsv::codeHash(norm)}, {"bail_jours", 30},
                                      {"active", true}}),
                            true);
        check(h.status == 200, "l'editeur depose la licence en ligne (" + std::to_string(h.status) + ")");

        const fs::path noFallback = g_base / "livre" / "sans-secours.xpgreg";
        writeFile(noFallback, registryWith(g_reg.number + 5, {}));
        usePc("S0", dead, U(noFallback));
        ls::setAddressForTests(deadAddress);
        const auto off = ls::activate(code, "PC-SECOURS");
        check(!off.ok && !off.reached && off.fallbackUsed.empty(), "sans adresse de secours : hors ligne (" + off.message + ")");

        const fs::path withFallback = g_base / "livre" / "secours.xpgreg";
        writeFile(withFallback, registryWith(g_reg.number + 6, {g_url}));
        usePc("S1", dead, U(withFallback));
        ls::setAddressForTests(deadAddress);
        check(ls::fallbackAddresses().size() == 1 && ls::fallbackAddresses()[0] == g_url, "le registre en vigueur porte l'adresse de secours");
        const auto r = ls::activate(code, "PC-SECOURS");
        const auto st = lic::getLicenseState();
        check(r.ok && r.reached && r.fallbackUsed == g_url && has(r.message, "service de secours"), "le service de secours a repondu (" + r.message + ")");
        check(st.status == lic::LicenseStatus::Valid && st.online && st.licenseId == "XPG-2026-00444",
              "la licence s'active par lui : son certificat nomme SON adresse, que le registre signe donne (" + st.message + ")");
        const auto t = ls::readTime();
        check(t.ok && t.fallbackUsed == g_url, "l'heure signee aussi (" + t.message + ")");
        // le secours repond avec le certificat d'une autre adresse (localhost : le meme service, une autre adresse)
        std::string other = g_url;
        other.replace(other.find("127.0.0.1"), 9, "localhost");
        const fs::path wrongFallback = g_base / "livre" / "secours-faux.xpgreg";
        writeFile(wrongFallback, registryWith(g_reg.number + 7, {other}));
        usePc("S2", dead, U(wrongFallback));
        ls::setAddressForTests(deadAddress);
        const auto bad = ls::activate(code, "PC-SECOURS-2");
        check(!bad.ok && has(bad.message, "service de secours") && has(bad.message, "autre adresse") &&
                  lic::getLicenseState().status != lic::LicenseStatus::Valid,
              "un secours dont le certificat nomme une autre adresse : refuse (" + bad.message + ")");
        ls::setAddressForTests(address);
    }

    ls::setAddressForTests(std::nullopt);
    dl::shutdown();
    fs::remove_all(g_base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
