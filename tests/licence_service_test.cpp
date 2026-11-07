// =============================================================================
//  tests/licence_service_test.cpp - 1.35.0 : LE CLIENT DU SERVICE DES LICENCES
//  DES STUDIOS (app/LicenceService), contre le VRAI Worker lance sur ce poste
// -----------------------------------------------------------------------------
//  Le service : tests/service_local.hpp (workerd, `wrangler dev --local`, un
//  dossier d'etat neuf). L'essai joue aussi l'EDITEUR : une cle d'essai creee
//  ici certifie la cle du service, depose le registre et des licences en ligne
//  (avec l'empreinte d'un code), retire un PC, repond a une demande - par des
//  appels /admin signes (service::signRequest). Plusieurs << PC >> : un dossier
//  de licences chacun (une cle de PC chacun), une horloge du magasin decalable.
//  On verifie :
//    - la disponibilite ; le service pas encore certifie ; hors ligne : une phrase ;
//    - la Decouverte enregistree par le service, et une autre installation de la
//      meme machine qui ne la recommence pas (le debut le plus ancien garde) ;
//    - un code faux (refuse, rien d'installe), mal ecrit (rien n'est envoye),
//      le bon code (Valide, en ligne, le bail), tous les postes pris ;
//    - un 401 du a l'heure : /v1/etat relu, un second essai ;
//    - deux Studios sur le meme dossier : le releve fusionne ;
//    - la licence flottante : le jeton, aucun poste libre (libre dans N min),
//      rendre, le fil de fond qui le prend et le rend a la fermeture ;
//    - la demande sans code : envoyee scellee, une seule a la fois, annulee,
//      puis acceptee par l'editeur (la licence s'installe) ;
//    - le code laisse par l'installateur : active puis efface ; garde sans
//      reseau ; efface s'il est refuse ; le fil de fond du lancement ;
//    - le renouvellement et l'heure signee recalee (le service en avance) ;
//    - retire par l'editeur : efface au renouvellement suivant ; liberer ;
//    - la cle d'un PC recopiee sur une autre installation : rien n'est
//      renouvele pour la copie, son activation est effacee ; l'original garde
//      la sienne ;
//    - liberer au-dela des changements de PC permis : la place gardee jusqu'a la
//      fin du bail (la phrase le dit) ; le refus d'un autre PC dit quand elle
//      revient ;
//    - une licence finie : plus renouvelee (gardee sur le PC, un avis) ;
//    - une activation que le service a oubliee (effacee 90 jours apres la fin
//      de son bail) : effacee du PC au renouvellement ;
//    - le bail : 7 jours avant, expire hors ligne (l'horloge du magasin), puis
//      la connexion revenue (renouvele, un avis) ;
//    - LE CODE JAMAIS dans un message, l'historique, le releve.
//  Node.js ou wrangler absents (le service ne se lance pas) : SAUTE, code 0.
// =============================================================================
#include "service_local.hpp"

#include "../src/app/BackgroundTasks.hpp"
#include "../src/app/Licence.hpp"
#include "../src/app/LicenceService.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
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
namespace json = core::json;
namespace ls = app::licence::service;
namespace ai = app::ai;
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

// ---- le monde de l'essai ----
fs::path                         g_base;
std::string                      g_url;          // le service local
std::unique_ptr<lic::SigningKey> g_editor;       // la cle (d'essai) de l'editeur
std::string                      g_registry;     // le registre signe
fs::path                         g_bundled;
lic::Registry                    g_reg;
std::atomic<long long>           g_shift{0};     // l'horloge du magasin = celle du systeme + g_shift
std::vector<std::string>         g_said;         // tout ce qui a ete dit (le code n'y est jamais)
std::vector<std::string>         g_codes;        // les codes employes (normalises)

ls::Result keep(ls::Result r) {
    g_said.push_back(r.message);
    for (const auto& n : r.notices) g_said.push_back(n.title);
    return r;
}
bool noticeKey(const ls::Result& r, const std::string& key) {
    for (const auto& n : r.notices)
        if (n.key == key) return true;
    return false;
}
std::string noticeTitle(const ls::Result& r, const std::string& key) {
    for (const auto& n : r.notices)
        if (n.key == key) return n.title;
    return {};
}
bool bell(const std::string& key, std::string* title = nullptr) {
    for (const auto& n : app::bgtasks::notices())
        if (n.key == key) {
            if (title) *title = n.title;
            return true;
        }
    return false;
}

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

std::string block(lic::DocKind kind, const std::string& doc) {
    return lic::writeBlock(lic::signBlock(kind, doc, g_editor->publicKey(), [](std::string_view m) { return g_editor->sign(m); }));
}

// ---- l'editeur : les appels au service (le fil de l'essai) ----
struct Http {
    bool        reached{false};
    int         status{0};
    std::string body;
    json::Value v;
};
Http http(const std::string& method, const std::string& path, const std::string& body, bool sign);

Seconds serviceTime() {
    const Http h = http("GET", "/v1/etat", "", false);
    return h.v.isObject() ? static_cast<Seconds>(h.v.integer("heure")) : lic::systemNow();
}

Http http(const std::string& method, const std::string& path, const std::string& body, bool sign) {
    ai::HttpRequest req;
    req.method = method;
    req.url = g_url + path;
    req.headers = {{"Content-Type", "application/json"}};
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (sign)
        for (auto& h : lsv::signRequest(method, path, body, g_editor->publicKey(), [](std::string_view m) { return g_editor->sign(m); }, serviceTime()))
            req.headers.push_back(h);
    auto client = ai::makeHttpClient();
    Http out;
    ai::HttpSink sink;
    sink.onData = [&out](std::string_view c) { out.body.append(c); };
    const auto r = client->perform(req, sink);
    out.reached = r.ok();
    out.status = r.status;
    (void)json::parse(out.body, out.v);
    return out;
}

std::string jsonBody(std::initializer_list<std::pair<const char*, json::Value>> fields) {
    json::Value v = json::Value::object();
    for (const auto& [k, x] : fields) v.set(k, x);
    return json::write(v, false);
}

// La cle du service certifiee par l'editeur, le registre depose.
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

// Une licence en ligne deposee avec l'empreinte d'un code neuf ; rend le code (forme XPG-XXXX-...).
std::string putLicence(const std::string& id, lic::LicenseType type, int seats, int bailDays = 30, Seconds expiresIn = 0) {
    lic::License l;
    l.licenseId = id;
    l.customer = "Client d'essai Atelier";
    l.edition = "PROFESSIONAL";
    l.type = type;
    l.service = g_url;
    l.maxDevices = seats;
    if (const lic::EditionDef* e = g_reg.edition("PROFESSIONAL")) l.features = e->features;
    lic::normalizeGrants(l.features);
    const Seconds now = serviceTime();
    l.issuedAt = now - 3600;
    l.expiresAt = expiresIn > 0 ? now + expiresIn : type == lic::LicenseType::Temporary ? now + 365 * D : 0;
    l.offlineGraceDays = type == lic::LicenseType::Temporary ? 14 : 0;
    l.manifest = g_reg.number;
    l.nonce = lic::newNonce();
    const std::string code = lsv::newActivationCode();
    std::string norm;
    (void)lsv::normalizeCode(code, norm);
    g_codes.push_back(norm);
    const Http h = http("PUT", "/admin/licences",
                        jsonBody({{"licence", block(lic::DocKind::License, lic::writeLicense(l))}, {"code_hash", lsv::codeHash(norm)},
                                  {"bail_jours", bailDays}, {"active", true}}),
                        true);
    check(h.status == 200, "l'editeur depose la licence " + id + " (" + std::to_string(h.status) + " " + h.body.substr(0, 160) + ")");
    return code;
}

// La description d'une licence chez le service (GET /admin/licences).
json::Value licenceAtService(const std::string& id) {
    const Http h = http("GET", "/admin/licences", "", true);
    if (const json::Value* list = h.v.find("licences"); list && list->isArray())
        for (const auto& l : list->items())
            if (l.str("licence") == id) return l;
    return {};
}

// ---- les PC (un dossier de licences chacun) ----
fs::path folderOf(const std::string& name) { return g_base / "pcs" / name; }

lic::StoreOptions pcOptions(const std::string& name, const std::string& machine) {
    lic::StoreOptions o;
    o.folder = U(folderOf(name));
    o.trustedKeys = {trustedOf(*g_editor)};
    o.compiledKeys = false;
    o.bundledRegistry = U(g_bundled);
    o.clock = [] { return lic::systemNow() + static_cast<Seconds>(g_shift.load()); };
    o.machineId = machine;
    o.evaluationPeriod = true;
    o.serviceUrl = g_url;   // les documents du service ne sont crus que pour SON adresse (celle du certificat)
    // code-activation.txt : ecrit par l'installateur (les administrateurs), comme sur un vrai poste
    o.fileOwner = [](const std::string&) { return lic::FileOwner::Admin; };
    return o;
}

void usePc(const std::string& name, const std::string& machine) { app::licence::start(pcOptions(name, machine)); }

int stateLines() {
    int n = 0;
    for (const auto& h : lic::getHistory(0)) n += h.kind == "etat";
    return n;
}

std::string pcKeyHex() {
    std::string hex, why;
    (void)lic::deviceKeyHex(hex, &why);
    return hex;
}

ls::Record record() {
    ls::Record r;
    std::string why;
    (void)ls::loadRecord(r, &why);
    return r;
}

bool near(Seconds a, Seconds b, Seconds tolerance) { return a >= b - tolerance && a <= b + tolerance; }

// ---- un crochet sur le transport : modifier une requete (ou agir pendant elle) ----
std::shared_ptr<ai::HttpClient>                g_system;   // le vrai transport (libcurl)
std::mutex                                     g_hookMutex;
std::function<bool(ai::HttpRequest&)>          g_hook;     // vrai : fait (retire)
std::vector<std::string>                       g_seen;     // les chemins vus pendant le crochet

class HookClient final : public ai::HttpClient {
public:
    ai::HttpResponse perform(const ai::HttpRequest& req0, const ai::HttpSink& sink) override {
        ai::HttpRequest req = req0;
        {
            std::lock_guard<std::mutex> lock(g_hookMutex);
            g_seen.push_back(req.url.substr(g_url.size() < req.url.size() ? g_url.size() : 0));
            if (g_hook && g_hook(req)) g_hook = nullptr;
        }
        return g_system->perform(req, sink);
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    std::atomic<bool> cancelled_{false};
};

void installHook(std::function<bool(ai::HttpRequest&)> hook) {
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        g_hook = std::move(hook);
        g_seen.clear();
    }
    ai::setHttpFactory([] { return std::unique_ptr<ai::HttpClient>(new HookClient()); });
}
void removeHook() {
    ai::setHttpFactory(nullptr);
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_hook = nullptr;
}

// Attendre la fin d'un travail du fil de fond (licence::tick : le releve).
bool waitRunner(unsigned long long rev, int maxMs = 60000) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(maxMs)) {
        app::licence::tick();
        if (!ls::busy() && ls::revision() != rev) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::uint8_t rnd[6];
    deploy::crypto::randomBytes(rnd, sizeof rnd);
    g_base = fs::temp_directory_path() / ("xpg-licence-service-" + deploy::crypto::hex(rnd, sizeof rnd));
    std::error_code ec;
    fs::remove_all(g_base, ec);
    fs::create_directories(g_base, ec);

    std::string why;
    g_editor = adm::createPublisherKey(U(g_base / "editeur"), "essai service Studios", true, &why);
    check(g_editor != nullptr, "la cle d'essai de l'editeur (" + why + ")");
    if (!g_editor) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    check(adm::signRegistry(*g_editor, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), lic::systemNow() - 60, g_registry,
                            &why),
          "le registre signe (" + why + ")");
    g_bundled = g_base / "livre" / "registre.xpgreg";
    writeFile(g_bundled, g_registry);
    {
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(g_registry, blocks, &why) && blocks.size() == 1 && lic::parseRegistry(blocks[0].document, g_reg, &why),
              "le registre se relit (" + why + ")");
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
    lsv::Address address;
    address.url = g_url;
    lsv::Address dead;
    dead.url = "http://127.0.0.1:1";   // personne n'ecoute : hors ligne
    g_system = std::shared_ptr<ai::HttpClient>(ai::makeHttpClient().release());

    std::printf("-- la disponibilite\n");
    {
        lic::StoreOptions o;
        o.folder = U(g_base / "sans-cle");
        o.compiledKeys = false;
        app::licence::start(std::move(o));
        ls::setAddressForTests(std::nullopt);
        if (lsv::compiledAddress().empty()) check(!ls::available(&why) && has(why, "pas de service"), "pas de service compile : " + why);
        ls::setAddressForTests(address);
        check(!ls::available(&why) && has(why, "pas activ"), "licences pas activees : " + why);
        const auto r = keep(ls::readTime());
        check(!r.ok && !r.reached, "pas activees : aucun appel");
        check(app::licence::indicator().text.empty() && app::licence::startCard().title.empty(), "pas activees : ni indicateur, ni encart");
    }

    std::printf("-- le service pas encore certifie\n");
    usePc("A", "machine-essai-1");
    app::licence::startNotices(false);   // les avis, sans le fil de fond
    {
        check(ls::available(&why), "licences actives et un service : disponible (" + why + ")");
        const auto r = keep(ls::readTime());
        check(!r.ok && r.reached && has(r.message, "pas encore certifi"), "pas de certificat : la phrase (" + r.message + ")");
        check(publisherSetup(why), "l'editeur certifie la cle du service, depose le registre (" + why + ")");
        const auto t = keep(ls::readTime());
        const auto st = lic::getLicenseState();
        check(t.ok && t.reached && near(st.lastServiceSync, lic::systemNow(), 120), "l'heure signee notee (" + t.message + ")");
    }

    std::printf("-- hors ligne : une phrase, rien ne change\n");
    {
        ls::setAddressForTests(dead);
        const auto r = keep(ls::readTime());
        check(!r.ok && !r.reached && r.message == "Hors ligne : le service des licences (127.0.0.1) ne r\xC3\xA9pond pas ; rien n'a chang\xC3\xA9.",
              "hors ligne : " + r.message);
        const auto a = keep(ls::activate("XPG-AAAA-BBBB-CCCC-DDDD", "Poste essai A"));
        check(!a.ok && !a.reached && has(a.message, "ton code n'a pas \xC3\xA9t\xC3\xA9 envoy\xC3\xA9") && !has(a.message, "AAAA"),
              "hors ligne, activer : " + a.message);
        check(lic::getLicenseState().status == lic::LicenseStatus::Evaluation && !present(folderOf("A") / "licence.xpglic"), "rien n'est installe");
        ls::setAddressForTests(address);
    }

    std::printf("-- la Decouverte enregistree par le service\n");
    Seconds trialStart = 0;
    {
        auto st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::Evaluation && st.evaluationDaysLeft == 30, "un PC neuf : Decouverte, 30 jours (" + st.message + ")");
        check(app::licence::indicator().text == "D\xC3\xA9" "couverte \xC2\xB7 30 j" && app::licence::indicator().tone == 4,
              "l'indicateur : " + app::licence::indicator().text);
        const auto card = app::licence::startCard();
        check(has(card.title, "D\xC3\xA9" "couverte : il te reste 30 jours") && card.button == "Activer avec un code\xE2\x80\xA6" && card.tab == app::licence::kTabActivation,
              "l'encart de l'accueil : " + card.title + " / " + card.button);
        const auto r = keep(ls::trial());
        const auto rec = record();
        st = lic::getLicenseState();
        check(r.ok && rec.trialRecordedAt > 0 && near(rec.trialStart, lic::systemNow(), 300) && present(folderOf("A") / lic::LicenseStore::kTrialFile),
              "declaree : enregistree par le service (" + r.message + ")");
        check(st.status == lic::LicenseStatus::Evaluation && st.evaluationDaysLeft == 30, "toujours 30 jours");
        trialStart = rec.trialStart;
    }
    {
        g_shift = 5 * D;   // une autre installation de la meme machine, 5 jours plus tard
        usePc("B", "machine-essai-1");
        auto st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::Evaluation && st.evaluationDaysLeft == 30, "reinstalle (un dossier neuf) : 30 jours sur ce PC seul");
        const auto r = keep(ls::trial());
        st = lic::getLicenseState();
        check(r.ok && st.status == lic::LicenseStatus::Evaluation && st.evaluationDaysLeft == 25 && near(st.evaluationStartedAt, trialStart, 120),
              "le service connait la machine : le debut le plus ancien gagne, 25 jours (" + std::to_string(st.evaluationDaysLeft) + ", " + r.message + ")");
        g_shift = 0;
    }

    std::printf("-- activer avec un code\n");
    usePc("A", "machine-essai-1");
    const std::string code1 = putLicence("XPG-2026-00201", lic::LicenseType::Temporary, 1);
    {
        const auto bad = keep(ls::activate("XPG-12", ""));
        check(!bad.ok && !bad.reached && has(bad.message, "Ce n'est pas un code"), "mal ecrit : rien n'est envoye (" + bad.message + ")");
        const auto wrong = keep(ls::activate("XPG-AAAA-BBBB-CCCC-DDDD", "Poste essai A"));
        check(!wrong.ok && wrong.reached && wrong.refused && wrong.definitive && has(wrong.message, "pas reconnu") && !has(wrong.message, "AAAA"),
              "un code faux : refuse (" + wrong.message + ")");
        check(lic::getLicenseState().status == lic::LicenseStatus::Evaluation && !present(folderOf("A") / "licence.xpglic") &&
                  !present(folderOf("A") / lic::LicenseStore::kActivationFile),
              "un code faux : rien n'est installe");
        // le code lu avec souplesse : minuscules, espaces
        std::string loose = code1;
        for (auto& ch : loose) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (auto& ch : loose)
            if (ch == '-') ch = ' ';
        const auto r = keep(ls::activate(loose, "Poste essai A"));
        const auto st = lic::getLicenseState();
        check(r.ok && r.licenseInstalled && r.licenseId == "XPG-2026-00201" && noticeKey(r, "licence:activation"),
              "le bon code (en minuscules, avec des espaces) : active (" + r.message + ")");
        check(st.status == lic::LicenseStatus::Valid && st.online && st.licenseId == "XPG-2026-00201" && st.activationDaysLeft == 30 &&
                  near(st.activationUntil, lic::systemNow() + 30 * D, 300) && present(folderOf("A") / lic::LicenseStore::kActivationFile),
              "Valide, en ligne, bail de 30 jours (" + st.message + ")");
        const auto rec = record();
        check(rec.activatedLicense == "XPG-2026-00201" && rec.activatedPcName == "Poste essai A", "le releve : la licence, le nom du PC");
        bool hist = false;
        for (const auto& h : lic::getHistory(20)) hist = hist || (h.kind == "service" && has(h.text, "activ\xC3\xA9" "e en ligne"));
        check(hist, "l'historique : activee en ligne");
        const auto b = app::licence::indicator();
        check(has(b.text, "Licence : PROFESSIONAL \xC2\xB7 jusqu'au ") && b.tone == 1 && b.shortText == "PROFESSIONAL", "l'indicateur : " + b.text);
        const auto card = app::licence::startCard();
        check(has(card.title, "Licence : PROFESSIONAL") && has(card.text, "activ\xC3\xA9" "e en ligne, bail jusqu'au") && card.tone == 1,
              "l'encart : " + card.title + " / " + card.text);
    }
    {
        usePc("C", "machine-essai-3");
        const auto r = keep(ls::activate(code1, "Poste essai C"));
        check(!r.ok && r.refused && !r.definitive && has(r.message, "Tous les postes"), "un second PC : tous les postes pris (" + r.message + ")");
        check(lic::getLicenseState().status == lic::LicenseStatus::Evaluation && !present(folderOf("C") / "licence.xpglic"), "rien n'est installe sur C");
    }

    std::printf("-- un 401 du a l'heure : l'heure du service relue, un second essai\n");
    usePc("A", "machine-essai-1");
    {
        int renews = 0;
        installHook([&renews](ai::HttpRequest& req) {
            if (!has(req.url, "/v1/renouveler")) return false;
            ++renews;
            for (auto& h : req.headers)
                if (h.first == lsv::kHeaderDate) h.second = std::to_string(std::stoll(h.second) - 3600);   // une heure de retard
            return true;
        });
        const auto r = keep(ls::renew());
        int seenRenew = 0, seenEtat = 0;
        {
            std::lock_guard<std::mutex> lock(g_hookMutex);
            for (const auto& s : g_seen) {
                seenRenew += has(s, "/v1/renouveler");
                seenEtat += has(s, "/v1/etat");
            }
        }
        removeHook();
        check(r.ok && renews == 1 && seenRenew == 2 && seenEtat >= 2, "refuse (401 heure), /v1/etat relu, refait : renouvele (" + r.message + ")");
    }

    std::printf("-- deux Studios sur le meme dossier\n");
    {
        ls::Record older, newer;
        older.number = 4;
        older.nonce = "n4";
        older.sentAt = 1000;
        older.local = ls::kLocalSent;
        older.lastNumber = 7;
        older.lastMessage = "etat 7";
        older.trialRecordedAt = 500;
        older.trialStart = 400;
        older.activatedLicense = "XPG-2026-00201";
        older.activatedAt = 900;
        newer = older;
        newer.number = 5;
        newer.nonce = "n5";
        newer.sentAt = 2000;
        newer.lastNumber = 6;
        newer.trialRecordedAt = 800;
        newer.trialStart = 300;
        newer.activatedLicense.clear();
        newer.activatedAt = 1500;   // libere apres
        auto m = ls::mergeRecords(newer, older);
        check(m.number == 5 && m.nonce == "n5" && m.lastNumber == 7 && m.lastMessage == "etat 7", "la demande la plus recente, l'etat le plus recent");
        check(m.trialRecordedAt == 500 && m.trialStart == 300 && m.activatedLicense.empty(), "la Decouverte : la premiere fois, le debut le plus ancien ; la liberation gardee");
        ls::Record cancelled = older;
        cancelled.local = ls::kLocalCancelled;
        cancelled.cancelledAt = 1200;
        m = ls::mergeRecords(cancelled, older);
        check(m.local == ls::kLocalCancelled && m.cancelledAt == 1200, "la meme demande : l'annulation de l'autre reste");
        // L'autre Studio envoie une demande pendant le renouvellement de celui-ci : rien ne se perd.
        installHook([](ai::HttpRequest& req) {
            if (!has(req.url, "/v1/renouveler")) return false;
            ls::Record other = record();
            other.number = 77;
            other.nonce = "nonce-autre-studio";
            other.sentAt = lic::systemNow();
            other.local = ls::kLocalSent;
            other.customer = "Client d'essai";
            std::string w;
            (void)ls::saveRecord(other, &w);
            return true;
        });
        const auto r = keep(ls::renew());
        removeHook();
        const auto rec = record();
        check(r.ok && rec.number == 77 && ls::pending(rec) && rec.activatedLicense == "XPG-2026-00201" && near(rec.lastRenew, lic::systemNow(), 60),
              "la demande de l'autre Studio et le renouvellement de celui-ci : les deux restent");
        ls::Record clean = rec;   // la demande d'essai retiree (elle n'existe pas chez le service)
        clean.number = 0;
        clean.local.clear();
        clean.nonce.clear();
        (void)ls::saveRecord(clean, &why);
    }

    std::printf("-- la cle d'un PC recopiee sur une autre installation de Windows\n");
    {
        // A est actif (XPG-2026-00201) ; son dossier recopie tel quel (la cle, la licence, l'activation, le releve) sur une
        // autre machine.
        std::error_code e2;
        fs::remove_all(folderOf("A-copie"), e2);
        fs::create_directories(folderOf("A-copie"), e2);
        for (const std::string f : {std::string(lic::kDeviceKeyFile), std::string("licence.xpglic"), std::string(lic::LicenseStore::kActivationFile),
                                    std::string(ls::kRecordFile)})
            fs::copy_file(folderOf("A") / f, folderOf("A-copie") / f, fs::copy_options::overwrite_existing, e2);
        usePc("A-copie", "machine-essai-copie");
        const auto r = keep(ls::renew());
        check(noticeKey(r, "licence:retrait") && has(noticeTitle(r, "licence:retrait"), "autre installation de Windows") && !lic::getLicenseState().online &&
                  !present(folderOf("A-copie") / lic::LicenseStore::kActivationFile),
              "la copie : rien de renouvele, son activation effacee (" + noticeTitle(r, "licence:retrait") + " / " + r.message + ")");
        const auto a = keep(ls::activate(code1, "Copie de A"));
        check(!a.ok && a.refused && !a.definitive && has(a.message, "autre poste") && !present(folderOf("A-copie") / lic::LicenseStore::kActivationFile),
              "la copie ne prend pas la place de A avec le code (" + a.message + ")");
        usePc("A", "machine-essai-1");
        const auto o = keep(ls::renew());
        const auto st = lic::getLicenseState();
        check(o.ok && st.status == lic::LicenseStatus::Valid && st.online && st.licenseId == "XPG-2026-00201", "A garde la sienne (" + o.message + ")");
    }

    std::printf("-- une licence finie : plus renouvelee\n");
    {
        const std::string code7 = putLicence("XPG-2026-00701", lic::LicenseType::Temporary, 1, 30, 8);
        usePc("E", "machine-essai-e");
        const auto a = keep(ls::activate(code7, "Poste essai E"));
        check(a.ok && lic::getLicenseState().licenseId == "XPG-2026-00701", "activee 8 s avant sa fin (" + a.message + ")");
        std::this_thread::sleep_for(std::chrono::seconds(9));
        const auto r = keep(ls::renew());
        const auto st = lic::getLicenseState();
        check(noticeKey(r, "licence:fin") && has(noticeTitle(r, "licence:fin"), "XPG-2026-00701") && has(noticeTitle(r, "licence:fin"), "a pris fin") &&
                  present(folderOf("E") / lic::LicenseStore::kActivationFile) && st.licenseId == "XPG-2026-00701" && !has(r.message, "Bail renouvel"),
              "finie : pas renouvelee, gardee sur le PC (le magasin la dit finie), un avis (" + noticeTitle(r, "licence:fin") + " / " + st.message + ")");
    }

    std::printf("-- liberer au-dela des changements de PC permis\n");
    {
        const std::string code6 = putLicence("XPG-2026-00601", lic::LicenseType::Perpetual, 1);
        ls::Result last;
        for (int i = 1; i <= 3; ++i) {
            const std::string name = "T" + std::to_string(i);
            usePc(name, "machine-essai-t" + std::to_string(i));
            const auto a = keep(ls::activate(code6, "Poste essai " + name));
            check(a.ok, name + " active (" + a.message + ")");
            last = keep(ls::release());
            check(last.ok && !lic::getLicenseState().online && !present(folderOf(name) / lic::LicenseStore::kActivationFile),
                  name + " libere (" + last.message + ")");
            if (i < 3) check(has(last.message, "est rendu"), name + " : la place rendue tout de suite");
        }
        check(has(last.message, "Sa place reste prise jusqu'au") && has(last.message, "2 fois ces 30 derniers jours") && noticeKey(last, "licence:liberation"),
              "le troisieme : la place gardee jusqu'a la fin de son bail, la phrase le dit (" + last.message + ")");
        usePc("T4", "machine-essai-t4");
        const auto r = keep(ls::activate(code6, "Poste essai T4"));
        check(!r.ok && r.refused && has(r.message, "Tous les postes") && has(r.message, "Une place revient le "),
              "un autre PC : refuse, et quand la place revient (" + r.message + ")");
    }

    std::printf("-- la licence flottante\n");
    const std::string code2 = putLicence("XPG-2026-00301", lic::LicenseType::Floating, 1);
    {
        usePc("F1", "machine-essai-f1");
        auto r = keep(ls::activate(code2, "Poste flottant 1"));
        auto st = lic::getLicenseState();
        check(r.ok && st.floating && st.status == lic::LicenseStatus::NoFloatingSeat, "activee (flottante) : sans jeton, aucun poste (" + st.message + ")");
        check(app::licence::indicator().text == "Flottante : aucun poste", "l'indicateur : " + app::licence::indicator().text);
        r = keep(ls::takeLease());
        st = lic::getLicenseState();
        check(r.ok && st.status == lic::LicenseStatus::Valid && near(st.leaseUntil, lic::systemNow() + 900, 120) &&
                  present(folderOf("F1") / lic::LicenseStore::kLeaseFile),
              "le jeton pris : Valide, 15 min (" + r.message + ")");
        check(app::licence::indicator().text == "Flottante \xC2\xB7 poste pris" && app::licence::indicator().tone == 1, "l'indicateur : " + app::licence::indicator().text);

        usePc("F2", "machine-essai-f2");
        r = keep(ls::activate(code2, "Poste flottant 2"));
        check(r.ok, "un second PC active la flottante (" + r.message + ")");
        r = keep(ls::takeLease());
        st = lic::getLicenseState();
        check(!r.ok && r.noSeat && r.retryAfter > 800 && r.retryAfter <= 900 && st.status == lic::LicenseStatus::NoFloatingSeat,
              "aucun poste libre (" + r.message + ")");
        check(noticeKey(r, "licence:flottante") && has(noticeTitle(r, "licence:flottante"), "aucun poste libre \xE2\x80\x94 libre dans 15 min"),
              "l'avis : " + noticeTitle(r, "licence:flottante"));

        usePc("F1", "machine-essai-f1");
        r = keep(ls::returnLease());
        check(r.ok && !present(folderOf("F1") / lic::LicenseStore::kLeaseFile) && lic::getLicenseState().status == lic::LicenseStatus::NoFloatingSeat,
              "F1 rend son poste (" + r.message + ")");

        usePc("F2", "machine-essai-f2");
        r = keep(ls::takeLease());
        const Seconds first = lic::getLicenseState().leaseUntil;
        check(r.ok && lic::getLicenseState().status == lic::LicenseStatus::Valid, "F2 prend le poste libere (" + r.message + ")");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        r = keep(ls::takeLease());
        check(r.ok && lic::getLicenseState().leaseUntil >= first, "le jeton renouvele (" + r.message + ")");

        // Le fil de fond : la synchro du lancement, puis le jeton ; rendu a la fermeture.
        (void)ls::returnLease();
        ls::startRunner();
        bool leased = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60) && !leased) {
            app::licence::tick();
            leased = !ls::busy() && ls::lastResult().job == ls::Job::LeaseTake && ls::lastResult().ok;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(leased && lic::getLicenseState().status == lic::LicenseStatus::Valid, "le fil de fond prend le jeton tout seul (" + ls::lastResult().message + ")");
        json::Value at = licenceAtService("XPG-2026-00301");
        const json::Value* jetons = at.find("jetons");
        check(jetons && jetons->isArray() && jetons->items().size() == 1, "chez le service : un jeton pris");
        const int linesBefore = stateLines();
        ls::shutdown();
        at = licenceAtService("XPG-2026-00301");
        jetons = at.find("jetons");
        check(jetons && jetons->isArray() && jetons->items().empty(), "la fermeture rend le jeton (chez le service : aucun)");
        check(present(folderOf("F2") / lic::LicenseStore::kLeaseFile) && lic::getLicenseState().status == lic::LicenseStatus::Valid &&
                  stateLines() == linesBefore,
              "... et l'etat de ce PC ne change pas en partant (le fichier du jeton finit seul ; rien dans l'historique)");

        // Le lancement suivant, 20 minutes plus tard (le jeton garde a pris fin) : le poste est repris AVANT la
        // premiere evaluation - pas de << Valide -> aucun poste -> Valide >> dans l'historique.
        check(svc.restart(20 * 60), "20 minutes passent (le service aussi) (" + svc.error() + ")");
        g_url = svc.url();
        address.url = g_url;
        ls::setAddressForTests(address);
        g_shift = 20 * 60;
        const bool pre = ls::startupLease(pcOptions("F2", "machine-essai-f2"));
        usePc("F2", "machine-essai-f2");
        st = lic::getLicenseState();
        check(pre && st.status == lic::LicenseStatus::Valid && st.leaseUntil > lic::systemNow() && stateLines() == linesBefore,
              "relance : le poste repris avant l'evaluation, Valide, aucune ligne d'etat de plus (" + std::string(pre ? "pris" : "PAS PRIS") + ", jeton " +
                  (present(folderOf("F2") / lic::LicenseStore::kLeaseFile) ? "present" : "absent") + ", " + std::to_string(stateLines() - linesBefore) +
                  " ligne(s), " + st.message + ")");
        // ... rendu de nouveau a la fermeture (le fil de fond)
        const auto rev = ls::revision();
        ls::startRunner();
        (void)waitRunner(rev);
        ls::shutdown();
        // Un autre PC prend le poste rendu ; ce PC (son jeton garde, encore bon) demande le sien : le service dit non,
        // le jeton garde est efface.
        usePc("F1", "machine-essai-f1");
        r = keep(ls::takeLease());
        check(r.ok, "F1 prend le poste rendu (" + r.message + ")");
        usePc("F2", "machine-essai-f2");
        check(lic::getLicenseState().status == lic::LicenseStatus::Valid, "F2 : son jeton garde vaut encore (15 min au plus)");
        r = keep(ls::takeLease());
        st = lic::getLicenseState();
        check(!r.ok && r.noSeat && st.status == lic::LicenseStatus::NoFloatingSeat && !present(folderOf("F2") / lic::LicenseStore::kLeaseFile),
              "le service dit : aucun poste - le jeton garde est efface (" + st.message + ")");
        check(!ls::startupLease(pcOptions("F2", "machine-essai-f2")) && lic::getLicenseState().status == lic::LicenseStatus::NoFloatingSeat,
              "au lancement, sans poste libre : rien (l'avis viendra du fil de fond)");
        usePc("F1", "machine-essai-f1");
        r = keep(ls::returnLease());
        check(r.ok, "F1 rend son poste");
        g_shift = 0;
    }

    std::printf("-- la demande sans code\n");
    {
        usePc("R", "machine-essai-r");
        ls::RequestForm f;
        f.customer = "Client d'essai Industrie";
        f.contact = "essai@exemple.invalid";
        f.pcName = "Poste essai R";
        f.message = "Pour l'atelier d'essai";
        auto r = keep(ls::sendRequest(f));
        auto rec = record();
        check(r.ok && r.pendingAfter && rec.number > 0 && ls::pending(rec) && rec.customer == f.customer, "envoyee (" + r.message + ")");
        const std::uint64_t first = rec.number;
        r = keep(ls::sendRequest(f));
        check(!r.ok && has(r.message, "d\xC3\xA9j\xC3\xA0 en cours"), "une seule a la fois (" + r.message + ")");
        // l'editeur la voit, scellee : lui seul la lit
        Http list = http("GET", "/admin/demandes", "", true);
        std::string sealed, state;
        if (const json::Value* d = list.v.find("demandes"); d && d->isArray())
            for (const auto& x : d->items())
                if (static_cast<std::uint64_t>(x.num("numero")) == first) {
                    sealed = x.str("scelle");
                    state = x.str("etat");
                }
        lgw::OnlineRequest q;
        std::string plain;
        check(!sealed.empty() && state == "attente" && !has(sealed, "Client d'essai") && lgw::openWith(*g_editor, sealed, plain, &why) &&
                  lgw::parseOnlineRequest(plain, q, &why) && q.customer == f.customer && q.contact == f.contact,
              "l'editeur l'ouvre avec sa cle (" + why + ")");
        r = keep(ls::cancelRequest());
        rec = record();
        check(r.ok && rec.local == ls::kLocalCancelled && !ls::pending(rec), "annulee (" + r.message + ")");
        list = http("GET", "/admin/demandes", "", true);
        if (const json::Value* d = list.v.find("demandes"); d && d->isArray())
            for (const auto& x : d->items())
                if (static_cast<std::uint64_t>(x.num("numero")) == first) state = x.str("etat");
        check(state == "annulee", "chez le service : annulee");

        // une nouvelle demande, acceptee : la licence de ce PC (liee par l'editeur) s'installe d'elle-meme
        r = keep(ls::sendRequest(f));
        rec = record();
        check(r.ok && rec.number > first && ls::pending(rec), "une nouvelle demande (" + r.message + ")");
        list = http("GET", "/admin/demandes", "", true);
        if (const json::Value* d = list.v.find("demandes"); d && d->isArray())
            for (const auto& x : d->items())
                if (static_cast<std::uint64_t>(x.num("numero")) == rec.number) sealed = x.str("scelle");
        check(lgw::openWith(*g_editor, sealed, plain, &why) && lgw::parseOnlineRequest(plain, q, &why), "l'editeur l'ouvre");
        adm::LicenseInputs in;
        in.params = "{\"license_id\": \"XPG-2026-00501\", \"customer\": \"Client d'essai Industrie\", \"edition\": \"PERSONAL\", \"max_devices\": 1, "
                    "\"duration_days\": 365}";
        in.requests = {q.request};
        in.registry = g_registry;
        std::string licence, summary;
        check(adm::signLicense(*g_editor, in, lic::systemNow(), licence, &why, &summary), "la licence liee a R signee (" + why + ")");
        lic::crypto::Bytes kb;
        lic::crypto::PublicKey pk{};
        const bool keyOk = lic::crypto::unhex(pcKeyHex(), kb, pk.size());
        if (keyOk) std::copy(kb.begin(), kb.end(), pk.begin());
        lgw::StatusContent content;
        content.license = licence;
        content.message = "Bienvenue (essai)";
        lgw::DeviceStatus s;
        s.device = lsv::deviceId(pk);
        s.number = 1;
        s.issuedAt = lic::systemNow();
        s.state = lgw::State::Accepted;
        s.requestNonce = rec.nonce;
        s.sealed = lgw::sealFor(pk, lgw::writeStatusContent(content));
        const Http put = http("PUT", "/admin/postes", jsonBody({{"poste", s.device}, {"etat", block(lic::DocKind::DeviceStatus, lgw::writeDeviceStatus(s))}}), true);
        check(keyOk && put.status == 200, "l'editeur publie l'etat de R (" + std::to_string(put.status) + " " + put.body.substr(0, 120) + ")");
        r = keep(ls::checkDevice());
        rec = record();
        const auto st = lic::getLicenseState();
        check(r.ok && r.licenseInstalled && st.status == lic::LicenseStatus::Valid && st.licenseId == "XPG-2026-00501" && !st.online,
              "acceptee : la licence s'installe (" + r.message + ")");
        check(rec.outcome == "acceptee" && !ls::pending(rec) && noticeKey(r, "licence:demande") && has(r.message, "Bienvenue"),
              "la reponse notee, un avis (" + ls::stateText(rec) + ")");
        r = keep(ls::checkDevice());
        check(r.ok && has(r.message, "Rien de nouveau"), "verifiee de nouveau : rien de nouveau (" + r.message + ")");
    }

    std::printf("-- le code laisse par l'installateur\n");
    const std::string code3 = putLicence("XPG-2026-00401", lic::LicenseType::Perpetual, 1);
    {
        usePc("I", "machine-essai-i");
        const fs::path codeFile = folderOf("I") / lic::LicenseStore::kPendingCodeFile;
        std::string lower = code3;
        for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        writeFile(codeFile, lower + "\r\n");
        ls::setAddressForTests(dead);
        auto r = keep(ls::sync());
        check(!r.ok && !r.reached && present(codeFile) && lic::getLicenseState().status == lic::LicenseStatus::Evaluation,
              "sans reseau : le code est garde (" + r.message + ")");
        ls::setAddressForTests(address);
        r = keep(ls::sync());
        auto st = lic::getLicenseState();
        check(r.licenseInstalled && st.status == lic::LicenseStatus::Valid && st.licenseId == "XPG-2026-00401" && !present(codeFile) &&
                  noticeKey(r, "licence:activation"),
              "en ligne : active, puis efface (" + r.message + ")");
        check(record().trialRecordedAt > 0, "la Decouverte declaree au passage (avant l'activation)");
        writeFile(codeFile, "XPG-ZZZZ-ZZZZ-ZZZZ-ZZZZ\n");
        g_codes.push_back("ZZZZZZZZZZZZZZZZ");
        r = keep(ls::sync());
        check(!present(codeFile) && noticeKey(r, "licence:code-installateur") && has(noticeTitle(r, "licence:code-installateur"), "Il est effac") &&
                  lic::getLicenseState().licenseId == "XPG-2026-00401",
              "un code refuse : efface, un avis, la licence reste (" + noticeTitle(r, "licence:code-installateur") + ")");
        writeFile(codeFile, "pas un code\n");
        r = keep(ls::sync());
        check(!present(codeFile) && noticeKey(r, "licence:code-installateur"), "pas un code : efface, un avis");
    }
    {
        // le fil de fond du lancement : il trouve le code et l'active
        const std::string code4 = putLicence("XPG-2026-00402", lic::LicenseType::Perpetual, 1);
        usePc("I2", "machine-essai-i2");
        const fs::path codeFile = folderOf("I2") / lic::LicenseStore::kPendingCodeFile;
        writeFile(codeFile, code4 + "\n");
        (void)app::licence::takeMessages();
        const auto rev = ls::revision();
        app::licence::startNotices(true);   // le fil de fond (l'application)
        check(waitRunner(rev), "le fil de fond : la synchro du lancement finit");
        const auto last = ls::lastResult();
        g_said.push_back(last.message);
        const auto st = lic::getLicenseState();
        check(last.job == ls::Job::Sync && st.status == lic::LicenseStatus::Valid && st.licenseId == "XPG-2026-00402" && !present(codeFile),
              "le code de l'installateur active au lancement (" + last.message + ")");
        std::string title;
        check(bell("licence:activation", &title) && has(title, "XPG-2026-00402"), "l'avis de la cloche : " + title);
        bool msg = false;
        for (const auto& m : app::licence::takeMessages()) {
            g_said.push_back(m);
            msg = msg || has(m, "XPG-2026-00402");
        }
        check(msg, "le message de la barre d'etat");
        ls::shutdown();
        check(!ls::busy(), "shutdown : rien ne reste en cours");
    }

    std::printf("-- le renouvellement et l'heure signee recalee (le service en avance de 2 jours)\n");
    check(svc.restart(2 * D), "le service relance, son horloge +2 j (" + svc.error() + ")");
    g_url = svc.url();
    address.url = g_url;
    ls::setAddressForTests(address);
    usePc("A", "machine-essai-1");
    {
        const auto r = keep(ls::renew());
        const auto st = lic::getLicenseState();
        check(r.ok && r.notices.empty(), "renouvele en silence (" + r.message + ")");
        check(near(ls::clockOffset(), 2 * D, 120) && near(st.lastServiceSync, lic::systemNow() + 2 * D, 300) && near(st.trustedNow, lic::systemNow() + 2 * D, 300),
              "l'heure du service notee : l'heure retenue suit le service (" + lic::isoTime(st.trustedNow) + ")");
        check(st.status == lic::LicenseStatus::Valid && near(st.activationUntil, lic::systemNow() + 32 * D, 300) && st.activationDaysLeft == 30,
              "le bail : 30 jours a l'heure du service (" + lic::isoTime(st.activationUntil) + ")");
    }

    std::printf("-- retire par l'editeur ; liberer\n");
    {
        const std::string keyA = pcKeyHex();
        const Http h = http("POST", "/admin/activations/retirer", jsonBody({{"licence", "XPG-2026-00201"}, {"poste", keyA}}), true);
        check(h.status == 200, "l'editeur retire A (" + h.body + ")");
        const auto r = keep(ls::renew());
        const auto st = lic::getLicenseState();
        check(noticeKey(r, "licence:retrait") && has(noticeTitle(r, "licence:retrait"), "XPG-2026-00201") && !st.online && st.licenseId.empty() &&
                  !present(folderOf("A") / "licence.xpglic") && !present(folderOf("A") / lic::LicenseStore::kActivationFile),
              "au renouvellement : l'activation effacee, un avis (" + noticeTitle(r, "licence:retrait") + ")");
        check(record().activatedLicense.empty(), "le releve l'oublie");
        usePc("C", "machine-essai-3");
        auto c = keep(ls::activate(code1, "Poste essai C"));
        check(c.ok && lic::getLicenseState().online, "la place libre : C s'active (" + c.message + ")");
        c = keep(ls::release());
        const auto stc = lic::getLicenseState();
        check(c.ok && !stc.online && !present(folderOf("C") / lic::LicenseStore::kActivationFile) && has(c.message, "lib\xC3\xA9r\xC3\xA9"),
              "C se libere (" + c.message + ")");
        std::string etatC;
        if (const json::Value* acts = licenceAtService("XPG-2026-00201").find("activations"); acts && acts->isArray())
            for (const auto& a : acts->items())
                if (a.str("nom") == "Poste essai C") etatC = a.str("etat");
        check(etatC == "liberee", "chez le service : liberee (" + etatC + ")");
    }

    std::printf("-- le bail : 7 jours avant, expire hors ligne, puis la connexion revenue\n");
    usePc("A", "machine-essai-1");
    {
        auto r = keep(ls::activate(code1, "Poste essai A"));
        check(r.ok && lic::getLicenseState().online, "A de nouveau activee (" + r.message + ")");
        // Le PC hors ligne : son horloge avance de 25 jours. Le compteur du bail compte ce saut depuis la derniere heure
        // signee (le service a +2 j) : l'heure retenue = +27 j, le bail (jusqu'a +32 j) : 5 jours.
        g_shift = 25 * D;
        app::licence::refresh();
        auto st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::Valid && st.activationWarning && st.activationDaysLeft == 5, "5 jours de bail : l'avertissement");
        check(app::licence::indicator().text == "Bail : 5 j \xE2\x80\x94 connecte-toi" && app::licence::indicator().tone == 2,
              "l'indicateur : " + app::licence::indicator().text);
        std::string title;
        check(bell("licence:bail-fin", &title) && has(title, "Bail : il reste 5 jours") && has(app::licence::launchNotice(), "connecte ce PC"),
              "l'avis de la cloche : " + title);
        check(has(app::licence::startCard().title, "Bail : il reste 5 jours"), "l'encart : " + app::licence::startCard().title);
        g_shift = 31 * D;   // ... puis 31 jours (l'heure retenue : +33 j) : le bail est fini
        app::licence::refresh();
        st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::LeaseExpired && st.online && !app::licence::allows("ai_generation"),
              "31 jours hors ligne : Bail expire, les fonctions en pause (" + st.message + ")");
        check(app::licence::indicator().text == "Bail expir\xC3\xA9 \xE2\x80\x94 connecte-toi" && app::licence::indicator().tone == 3 &&
                  bell("licence:bail-fin", &title) && has(title, "Bail expir"),
              "l'indicateur et l'avis : " + title);
        ls::setAddressForTests(dead);
        r = keep(ls::renew());
        check(!r.ok && !r.reached && has(r.message, "Hors ligne") && lic::getLicenseState().status == lic::LicenseStatus::LeaseExpired,
              "toujours hors ligne : rien ne change (" + r.message + ")");
        check(svc.restart(2 * D + 31 * D + 3600), "le temps a passe aussi chez le service (" + svc.error() + ")");
        g_url = svc.url();
        address.url = g_url;
        ls::setAddressForTests(address);
        r = keep(ls::renew());
        app::licence::refresh();
        st = lic::getLicenseState();
        check(r.ok && st.status == lic::LicenseStatus::Valid && st.activationDaysLeft == 30 && noticeKey(r, "licence:bail") &&
                  has(noticeTitle(r, "licence:bail"), "de nouveau active"),
              "la connexion revenue : renouvele, de nouveau actif (" + noticeTitle(r, "licence:bail") + ")");
        check(!bell("licence:bail-fin"), "l'avis du bail retire");
        g_shift = 0;
    }

    std::printf("-- une activation que le service a oubliee (effacee 90 jours apres la fin de son bail)\n");
    {
        // Le bail de A court jusqu'au 63e jour (le service a +33 j) ; 160 jours plus tard, le service l'a effacee.
        check(svc.restart(160 * D), "160 jours passent chez le service (" + svc.error() + ")");
        check(svc.url() == g_url, "le meme port (le certificat nomme l'adresse du service)");
        g_url = svc.url();
        address.url = g_url;
        ls::setAddressForTests(address);
        usePc("A", "machine-essai-1");
        const auto r = keep(ls::renew());
        const auto st = lic::getLicenseState();
        check(noticeKey(r, "licence:retrait") && has(noticeTitle(r, "licence:retrait"), "ne conna\xC3\xAEt plus") && !st.online &&
                  !present(folderOf("A") / lic::LicenseStore::kActivationFile),
              "le service ne la connait plus : effacee du PC, un avis (" + noticeTitle(r, "licence:retrait") + " / " + r.message + ")");
    }

    std::printf("-- le code n'est jamais ecrit (messages, historique, releve)\n");
    {
        std::vector<std::string> forms;
        for (const auto& n : g_codes) {
            forms.push_back(n);
            forms.push_back(lsv::formatCode(n));
            forms.push_back(lsv::formatCode(n).substr(4, 9));   // "XXXX-XXXX"
        }
        bool clean = true;
        std::string where;
        const auto scan = [&](const std::string& text, const std::string& what) {
            for (const auto& f : forms)
                if (!f.empty() && has(text, f)) {
                    clean = false;
                    where = what;
                }
        };
        for (const auto& s : g_said) scan(s, "un message");
        for (const auto& e : fs::recursive_directory_iterator(g_base / "pcs", ec)) {
            if (!e.is_regular_file()) continue;
            scan(readFile(e.path()), e.path().filename().string());
        }
        check(clean, "aucun code dans les messages, l'historique, les releves, les fichiers des PC" + (where.empty() ? std::string() : " (trouve : " + where + ")"));
    }

    ls::shutdown();
    ls::setAddressForTests(std::nullopt);
    lic::resetForTests();
    svc.stop();
    fs::remove_all(g_base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
