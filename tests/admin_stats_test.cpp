// =============================================================================
//  tests/admin_stats_test.cpp - 1.47.0 : LES STATISTIQUES ET LE GLOBE DANS
//  L'OUTIL DE L'EDITEUR (license_admin/Stats, gui/WorkspaceStats.cpp ; docs/
//  STATISTIQUES.md), contre le VRAI Worker lance sur ce poste
//  (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  Sans reseau : les jours et les periodes, les adresses (un nom de client
//  accentue), la lecture des reponses, les textes (euros, durees, variations),
//  la version du service qu'il faut. Puis : l'admin+ (une cle d'ESSAI) certifie
//  le service, signe deux licences en ligne (Atelier Exemple, Societe d'essai) ;
//  des PC s'activent depuis leur ville (X-XPG-Lieu-Essai : le lieu des essais,
//  que le service n'ecoute que pres de 127.0.0.1), un se libere, un autre
//  commence une Decouverte et depose une demande ; le releve du jour ; les
//  courbes (divisees, filtrees par un client, comparees) ; les mises a jour ;
//  les heures des admins ; le globe et le direct ; la console (argv[2]).
//  Des noms d'essai seulement.
//
//    admin_stats_test SOURCE [XPG-License-Admin]
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/Stats.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;
namespace json = core::json;
namespace st = license_admin::stats;
namespace gui = license_admin::gui;

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
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
std::string hex32() {
    static std::mt19937_64 rng(std::random_device{}());
    char b[40];
    std::snprintf(b, sizeof b, "%016llx%016llx", static_cast<unsigned long long>(rng()), static_cast<unsigned long long>(rng()));
    return b;
}

struct Reply {
    int         status{0};
    std::string body;
    json::Value value;
};
using Headers = std::vector<std::pair<std::string, std::string>>;

// Un appel HTTP (signe par `key` si donnee, a l'heure du service), avec des en-tetes en plus (le lieu, le logiciel).
Reply call(const std::string& base, const std::string& method, const std::string& path, const std::string& body, const lic::SigningKey* key,
           const Headers& extra = {}) {
    lic::Seconds now = lic::systemNow();
    if (key) {
        auto c = app::ai::makeHttpClient();
        app::ai::HttpRequest q;
        q.method = "GET";
        q.url = base + "/v1/etat";
        std::string b;
        app::ai::HttpSink s;
        s.onData = [&b](std::string_view x) { b.append(x); };
        (void)c->perform(q, s);
        json::Value v;
        if (json::parse(b, v, nullptr) && v.isObject()) now = static_cast<lic::Seconds>(v.integer("heure", now));
    }
    auto client = app::ai::makeHttpClient();
    app::ai::HttpRequest req;
    req.method = method;
    req.url = base + path;
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    for (const auto& h : extra) req.headers.push_back(h);
    if (key)
        for (auto& h : sv::signRequest(method, path, body, key->publicKey(), [key](std::string_view m) { return key->sign(m); }, now)) req.headers.push_back(h);
    Reply out;
    app::ai::HttpSink sink;
    sink.onData = [&out](std::string_view chunk) { out.body.append(chunk.data(), chunk.size()); };
    const auto r = client->perform(req, sink);
    out.status = r.status;
    (void)json::parse(out.body, out.value, nullptr);
    return out;
}

// ---- un PC d'essai (comme un Studio), dans sa ville ----
struct Pc {
    std::unique_ptr<lic::SigningKey> key;
    std::string                      name;
    Headers                          headers;   // X-XPG-Lieu-Essai, X-XPG-Logiciel
    Pc(const fs::path& dir, const std::string& n, const std::string& place) : name(n) {
        std::string why;
        key = lic::SigningKey::create(U(dir / "identite-pc.cle"), lic::deviceKeySpec(), n, false, &why);
        headers = {{"X-XPG-Logiciel", "IHM-Studio/1.46.0"}};
        if (!place.empty()) headers.emplace_back("X-XPG-Lieu-Essai", place);
    }
    [[nodiscard]] std::string station() const { return lic::stationHash("machine d'essai " + name, key->publicKey()); }
    [[nodiscard]] std::string request() const {
        lic::ActivationRequest r;
        r.deviceKey = key->publicKeyHex();
        r.deviceName = name;
        r.software = "IHM Studio 1.46.0 (essai)";
        r.createdAt = lic::systemNow();
        r.nonce = hex32();
        r.station = station();
        return lic::writeBlock(lic::signBlock(lic::DocKind::Request, lic::writeRequest(r), key->publicKey(), [this](std::string_view m) { return key->sign(m); }));
    }
    Reply send(const std::string& base, const std::string& method, const std::string& path, const json::Value& body) const {
        return call(base, method, path, json::write(body, false), key.get(), headers);
    }
    Reply activate(const std::string& base, const std::string& code) const {
        json::Value b = json::Value::object();
        b.set("code", code);
        b.set("demande", request());
        b.set("nom", name);
        return send(base, "POST", "/v1/activer", b);
    }
};

const char* kLyon = "ville=Lyon&region=Auvergne-Rh%C3%B4ne-Alpes&pays=FR&lat=45.764&lon=4.8357&colo=CDG";
const char* kParis = "ville=Paris&region=%C3%8Ele-de-France&pays=FR&lat=48.8566&lon=2.3522&colo=CDG";
const char* kBruxelles = "ville=Bruxelles&region=Bruxelles-Capitale&pays=BE&lat=50.8503&lon=4.3517&colo=BRU";

// La console (argv[2]) : sa sortie (et son code).
std::string runConsole(const std::string& exe, const std::string& args, int* code = nullptr) {
    std::string out;
#if !defined(_WIN32)
    FILE* p = ::popen(("\"" + exe + "\" " + args + " 2>&1").c_str(), "r");
    if (!p) return out;
    char buf[1024];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int s = ::pclose(p);
    if (code) *code = WIFEXITED(s) ? WEXITSTATUS(s) : -1;
#else
    (void)exe;
    (void)args;
    (void)code;
#endif
    return out;
}

// Une licence en ligne signee par l'admin+ et deposee (le Workspace, comme Licences > Nouvelle licence en ligne).
std::string onlineLicence(gui::Workspace& ws, const std::string& id, const std::string& customer, const std::string& edition, int seats) {
    gui::LicenceDraft d;
    d.online = true;
    d.licenseId = id;
    d.customer = customer;
    d.edition = edition;
    d.maxDevices = seats;
    d.durationDays = 365;
    gui::ServiceJob job;
    std::string code;
    gui::OpResult r = ws.prepareOnlineLicence(d, {}, job, code);
    check(r.ok && !code.empty(), "la licence en ligne " + id + " preparee (" + r.text() + ")");
    if (!r.ok) return {};
    r = ws.serviceRun(job);
    check(r.ok, "la licence en ligne " + id + " deposee (" + r.text() + ")");
    return code;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : std::string(".");
    const std::string console = argc > 2 ? argv[2] : std::string();
    if (!lic::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-admin-stats-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};

    // ======================================================================== sans reseau
    std::printf("-- les jours, les periodes, les adresses (sans reseau)\n");
    {
        const lic::Seconds t = st::dayStart("2026-11-10") + 12 * 3600;
        check(st::dayOf(0) == "1970-01-01" && st::dayOf(t) == "2026-11-10" && st::dayStart("2026-11-10") == 1794268800, "le jour UTC (" + st::dayOf(t) + ")");
        check(st::dayStart("2026-02-30") == -1 && st::dayStart("2026-13-01") == -1 && st::dayStart("10/11/2026") == -1 && st::dayStart("2024-02-29") > 0,
              "un jour illisible : -1 (le 30 fevrier, le mois 13) ; le 29 fevrier 2024 : lu");
        const st::Period w = st::lastDays(t, 7);
        check(w.from == "2026-11-04" && w.to == "2026-11-10", "les 7 derniers jours (" + w.from + " -> " + w.to + ")");
        const st::Period y = st::lastMonths(t, 12);
        check(y.from == "2025-11-11" && y.to == "2026-11-10", "les 12 derniers mois (" + y.from + ")");
        const st::Period m = st::lastMonths(st::dayStart("2026-03-31"), 1);
        check(m.from == "2026-03-01", "un mois avant le 31 mars : du 1er mars (" + m.from + ")");
        const st::Period n = st::nextDays(t, 90);
        check(n.from == "2026-11-10" && n.to == "2027-02-08", "les 90 jours a venir (" + n.to + ")");
        st::SeriesQuery q;
        q.measures = {"ventes_montant", "pc_actifs"};
        q.period = w;
        q.step = st::Step::Week;
        q.split = "offre";
        q.compare = st::Compare::LastYear;
        q.client = "Soci\xC3\xA9t\xC3\xA9 d'essai";
        check(q.path() == "/admin/stats/serie?mesures=ventes_montant,pc_actifs&du=2026-11-04&au=2026-11-10&pas=semaine&par=offre&comparer=an&client=Soci%C3%A9t%C3%A9%20d%27essai",
              "l'adresse des courbes (le nom du client encode) : " + q.path());
        st::UpdatesQuery u;
        check(u.path() == "/admin/stats/mises-a-jour?pas=semaine&jours=42&logiciel=tous", "l'adresse des mises a jour : " + u.path());
        st::Step s{};
        st::Compare c{};
        check(st::parseStep("mois", s) && s == st::Step::Month && !st::parseStep("heure", s) && st::parseCompare("rien", c) && c == st::Compare::None &&
                  std::string(st::stepKey(st::Step::Week)) == "semaine" && std::string(st::compareKey(st::Compare::Previous)) == "avant",
              "les pas et les comparaisons");
    }
    std::printf("-- lire les reponses, les textes, la version (sans reseau)\n");
    {
        json::Value v;
        check(json::parse(R"({"version":"1.47.0","heure":1794326400,"releves_depuis":"2026-11-10","du":"2026-11-03","au":"2026-11-10","pas":"semaine","par":"offre","comparer":"an",
          "periodes":[{"du":"2026-11-03","au":"2026-11-08"},{"du":"2026-11-09","au":"2026-11-10"}],
          "avant":{"du":"2025-11-03","au":"2025-11-10","periodes":[{"du":"2025-11-03","au":"2025-11-08"},{"du":"2025-11-09","au":"2025-11-10"}]},
          "mesures":{"ventes_montant":{"unite":"centimes","genre":"flux","source":"S","points":[184000,252000],"total":436000,
             "parts":[{"cle":"PROFESSIONAL","points":[98000,147000],"total":245000},{"cle":"","points":[0,null],"total":0}],
             "avant":{"points":[150000,209000],"total":359000},"variation":21.4},
           "demandes_delai_median":{"unite":"secondes","genre":"ratio","source":"N","points":[null,13200],"total":13200},
           "demandes_traitees":{"refuse":"droit","droits":["demandes"]}}})",
                          v, nullptr),
              "le JSON d'exemple");
        st::SeriesReply r;
        std::string why;
        check(st::parseSeries(v, r, &why), "lu (" + why + ")");
        const st::Series* m = r.find("ventes_montant");
        check(r.buckets.size() == 2 && r.beforeBuckets.size() == 2 && r.step == st::Step::Week && r.compare == st::Compare::LastYear && r.recordedSince == "2026-11-10" &&
                  r.beforePeriod.from == "2025-11-03",
              "les periodes, la comparaison, le debut des releves");
        check(m && m->points.size() == 2 && m->points[1] == 252000 && m->total == 436000 && m->before.size() == 2 && m->beforeTotal == 359000 &&
                  std::fabs(m->change - 21.4) < 1e-9 && m->part("PROFESSIONAL") && m->part("PROFESSIONAL")->total == 245000 && m->part("") &&
                  !st::isNumber(m->part("")->points[1]) && m->hasComparison(),
              "une mesure : ses points, ses parts, la comparaison, la variation");
        const st::Series* d = r.find("demandes_delai_median");
        check(d && !st::isNumber(d->points[0]) && d->points[1] == 13200 && !d->hasComparison() && !st::isNumber(d->change), "un point inconnu : NaN");
        const st::Series* x = r.find("demandes_traitees");
        check(x && x->refused && x->rights.size() == 1 && x->rights[0] == "demandes", "une mesure refusee (le droit)");
        json::Value bad;
        check(json::parse(R"({"mesures":{"a":{"points":[1]}},"periodes":[{"du":"x","au":"x"},{"du":"y","au":"y"}]})", bad, nullptr) && !st::parseSeries(bad, r, &why),
              "un point par periode, sinon refuse (" + why + ")");
        check(json::parse(R"({"heure":5,"periode":30,"version_derniere":"1.47.0","villes":[{"id":"FR|Lyon","ville":"Lyon","region":"Auvergne-Rh\u00f4ne-Alpes","pays":"FR","lat":45.8,"lon":4.8,"colo":"CDG",
            "clients":9,"pc":64,"actifs":9,"pas_a_jour":2,"admins":1,"decouvertes":1,"commandes":3,"demandes":1,"noms_clients":["Atelier Exemple"],"postes":["ab"],"ids_admins":["xpg-0011223344556677"]}],
            "totaux":{"villes":21,"pays":9,"clients":54,"pc":342,"actifs":42,"pas_a_jour":11,"admins":3,"decouvertes":9,"commandes":14,"demandes":5},"sans_lieu":{"pc":3,"admins":0}})",
                          v, nullptr),
              "le JSON du globe");
        st::GlobeReply g;
        check(st::parseGlobe(v, g, &why) && g.cities.size() == 1 && g.find("FR|Lyon") && g.find("FR|Lyon")->region == "Auvergne-Rh\xC3\xB4ne-Alpes" &&
                  g.find("FR|Lyon")->count("pc") == 64 && g.find("FR|Lyon")->count("pas_a_jour") == 2 && g.find("FR|Lyon")->customers.size() == 1 && g.totals.pcs == 342 &&
                  g.cityCount == 21 && g.countries == 9 && g.unplacedPcs == 3 && g.latestVersion == "1.47.0",
              "le globe lu : une ville, ses calques, les totaux");
        check(json::parse(R"({"heure":10,"actifs":42,"connexions":[{"t":9,"ville":"FR|Lyon","genre":"studio"}]})", v, nullptr), "le JSON du direct");
        st::LiveReply l;
        check(st::parseLive(v, l, &why) && l.active == 42 && l.pings.size() == 1 && l.pings[0].city == "FR|Lyon" && l.pings[0].kind == "studio", "le direct lu");
        check(st::euros(2602000) == "26\xC2\xA0" "020\xC2\xA0\xE2\x82\xAC" && st::euros(70900) == "709\xC2\xA0\xE2\x82\xAC" && st::euros(std::nan("")) == "\xE2\x80\x94",
              "les euros (" + st::euros(2602000) + ")");
        check(st::duration(13200) == "3 h 40" && st::duration(720) == "12 min" && st::duration(187200) == "2 j 4 h" && st::duration(3600) == "1 h" && st::duration(5) == "5 s",
              "les durees (" + st::duration(13200) + ", " + st::duration(187200) + ")");
        check(st::changeText(13.1) == "+13\xC2\xA0%" && st::changeText(-29) == "\xE2\x88\x92" "29\xC2\xA0%" && st::changeText(std::nan("")).empty(), "les variations");
        check(st::measureLabel("ventes_montant") == "Montant encaiss\xC3\xA9" && st::measureLabel("inconnue") == "inconnue" && st::measureNames().size() >= 39,
              "le nom des mesures (" + std::to_string(st::measureNames().size()) + ")");
        check(st::serviceKnowsStats("1.47.0") && st::serviceKnowsStats("1.48.2") && !st::serviceKnowsStats("1.46.0") && !st::serviceKnowsStats(""), "la version qu'il faut : 1.47.0");
        check(st::serviceTooOldText("1.46.0") == "Ton service en ligne est en 1.46.0 : d\xC3\xA9ploie la 1.47.0 pour les statistiques et le globe." &&
                  st::serviceTooOldText("1.47.0").empty(),
              "la phrase d'un service trop ancien : " + st::serviceTooOldText("1.46.0"));
    }

    gui::Workspace plus(U(root / "plus"));
    {
        const auto c = plus.createKey("essai statistiques", true);
        check(c.ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+");
        const auto r = plus.signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json");
        check(r.ok && plus.hasRegistry(), "le registre d'exemple signe");
        check(plus.statsServiceNote().empty() && !plus.serviceKnowsStats(), "sans service : rien a dire, et pas de statistiques");
        st::SeriesReply rep;
        st::SeriesQuery q;
        q.measures = {"pc_actifs"};
        check(!plus.statsSeries(q, rep).ok, "sans service : les courbes refusees");
    }

    // ======================================================================== le service local
    xpg_test::LocalService svc({plus.signingKey()->publicKeyHex()});
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    if (!svc.ready()) {
        std::printf("ECHEC : le service local : %s\n", svc.error().c_str());
        return 1;
    }
    const std::string url = svc.url();
    std::printf("   service local : %s\n", url.c_str());

    std::printf("-- l'admin+ : le service, deux licences en ligne\n");
    std::string codeA, codeB;
    {
        check(plus.serviceConfigure(url).ok, "l'adresse du service");
        check(plus.serviceCertify({}).ok, "la cle du service certifiee");
        auto r = plus.signRegistry(plus.registry());
        auto job = plus.takeServiceJob();
        check(r.ok && job && plus.serviceRun(*job).ok, "le registre depose");
        license_admin::online::ServiceState state;
        check(plus.serviceVerify(&state).ok && state.version == "1.52.0", "le service est en 1.52.0 (" + state.version + ")");
        plus.adoptServiceState(state);
        check(plus.serviceKnowsStats() && plus.statsServiceNote().empty(), "il sait les statistiques");
        codeA = onlineLicence(plus, "XPG-2026-S0001", "Atelier Exemple", "PROFESSIONAL", 3);
        codeB = onlineLicence(plus, "XPG-2026-S0002", "Soci\xC3\xA9t\xC3\xA9 d'essai", "PERSONAL", 1);
    }

    std::printf("-- des PC dans leurs villes : actives, une liberation, une Decouverte et une demande\n");
    Pc pc1(root / "pc1", "ATELIER-PC1", kLyon), pc2(root / "pc2", "ATELIER-PC2", kLyon), pc3(root / "pc3", "ESSAI-PC3", kParis), pc4(root / "pc4", "BRUXELLES-PC4", kBruxelles);
    {
        check(pc1.activate(url, codeA).status == 200 && pc2.activate(url, codeA).status == 200 && pc3.activate(url, codeB).status == 200, "trois PC actives");
        json::Value b = json::Value::object();
        b.set("licence", "XPG-2026-S0001");
        b.set("station", pc2.station());
        check(pc2.send(url, "POST", "/v1/liberer", b).status == 200, "un PC se libere");
        json::Value d = json::Value::object();
        d.set("machine", hex32());
        check(pc4.send(url, "POST", "/v1/decouverte", d).status == 200, "une Decouverte");
        json::Value q = json::Value::object();
        q.set("cle", plus.signingKey()->keyId());
        q.set("scelle", "QUFBQQ==");
        check(pc4.send(url, "POST", "/v1/demandes", q).status == 201, "une demande");
    }

    std::printf("-- le releve du jour, les courbes\n");
    const lic::Seconds now = lic::systemNow();
    {
        st::SnapshotReply snap;
        auto r = plus.statsSnapshot(snap);
        check(r.ok && snap.day == st::dayOf(now) && !snap.taken && snap.snapshot.isObject() && snap.snapshot.find("pc_actifs"),
              "le releve d'aujourd'hui : deja pris au demarrage du service (" + r.message + ")");
        st::SeriesQuery q;
        q.measures = {"licences_nouvelles", "licences_actives", "pc_actifs", "pc_actives", "pc_liberes", "demandes_recues", "decouvertes_commencees", "decouvertes_en_cours"};
        q.period = st::lastDays(now, 7);
        q.split = "offre";
        st::SeriesReply rep;
        std::string raw;
        r = plus.statsSeries(q, rep, &raw);
        check(r.ok && rep.buckets.size() == 7 && rep.beforeBuckets.size() == 7 && rep.series.size() == 8 && rep.serviceVersion == "1.52.0" && has(raw, "\"mesures\""),
              "les courbes lues (" + r.message + ")");
        const auto total = [&](const char* n) {
            const st::Series* s = rep.find(n);
            return s ? s->total : -1;
        };
        check(total("licences_nouvelles") == 2 && total("licences_actives") == 2 && total("pc_actifs") == 2 && total("pc_actives") == 3 && total("pc_liberes") == 1 &&
                  total("demandes_recues") == 1 && total("decouvertes_commencees") == 1 && total("decouvertes_en_cours") == 1,
              "les totaux : 2 licences, 2 PC actifs (3 actives, 1 libere), 1 demande, 1 Decouverte");
        const st::Series* p = rep.find("pc_actifs");
        check(p && p->part("PROFESSIONAL") && p->part("PROFESSIONAL")->total == 1 && p->part("PERSONAL") && p->part("PERSONAL")->total == 1 && p->beforeTotal == 0 &&
                  !st::isNumber(p->change) && p->points.back() == 2,
              "par offre : PROFESSIONAL 1, PERSONAL 1 ; la semaine d'avant : 0");
        // un client au nom accentue (l'adresse signee, telle quelle)
        st::SeriesQuery f;
        f.measures = {"licences_nouvelles", "pc_actifs"};
        f.period = q.period;
        f.compare = st::Compare::None;
        f.client = "soci\xC3\xA9t\xC3\xA9 D'ESSAI";
        r = plus.statsSeries(f, rep);
        check(r.ok && rep.find("licences_nouvelles") && rep.find("licences_nouvelles")->total == 1 && rep.find("pc_actifs") && rep.find("pc_actifs")->total == 1 &&
                  rep.beforeBuckets.empty() && !rep.find("pc_actifs")->hasComparison(),
              "filtre sur un client (Societe d'essai) : 1 licence, 1 PC (" + r.message + ")");
        st::SeriesQuery e;
        e.measures = {"inconnue"};
        r = plus.statsSeries(e, rep);
        check(!r.ok && has(r.message, "inconnue"), "une mesure inconnue : refusee (" + r.message + ")");
    }

    std::printf("-- les mises a jour, les admins, le globe, le direct\n");
    {
        st::UpdatesReply u;
        auto r = plus.statsUpdates({}, u);
        const st::VersionRow* v46 = nullptr;
        for (const auto& v : u.versions)
            if (v.version == "1.46.0") v46 = &v;
        check(r.ok && u.latest.empty() && v46 && v46->studios == 4 && v46->published == 0 && u.seenStudios == 4 && u.seenAdmins == 1 && u.behindTotal == 0,
              "les mises a jour : 4 Studios en 1.46.0, l'Admin de ce poste, aucune version publiee (" + r.message + ")");
        st::AdminsReply a;
        r = plus.statsAdmins(st::lastDays(now, 30), a);
        const st::AdminUsage* me = a.find(plus.signingKey()->keyId());
        check(r.ok && me && me->role == "admin+" && me->calls > 0 && !me->days.empty() && me->days.back() == st::dayOf(now) && me->usualStart >= 0,
              "les heures de l'admin+ : ses appels d'aujourd'hui (" + r.message + ")");
        st::GlobeReply g;
        r = plus.statsGlobe(30, g);
        const st::GlobeCity* lyon = g.find("FR|Lyon");
        const st::GlobeCity* bxl = g.find("BE|Bruxelles");
        check(r.ok && lyon && lyon->pcs == 1 && lyon->active == 2 && lyon->clients == 1 && lyon->customers.size() == 1 && lyon->customers[0] == "Atelier Exemple" &&
                  std::fabs(lyon->lat - 45.8) < 1e-9 && std::fabs(lyon->lon - 4.8) < 1e-9 && lyon->region == "Auvergne-Rh\xC3\xB4ne-Alpes",
              "le globe : Lyon (1 PC active, 2 actifs, Atelier Exemple) (" + r.message + ")");
        check(g.find("FR|Paris") && g.find("FR|Paris")->pcs == 1 && bxl && bxl->discoveries == 1 && bxl->requests == 1 && bxl->pcs == 0 && g.totals.pcs == 2 &&
                  g.totals.clients == 2 && g.countries == 2 && g.unplacedAdmins == 1,
              "Paris, Bruxelles (une Decouverte, une demande), les totaux ; l'admin+ sans lieu (aucun appel place)");
        st::LiveReply l;
        r = plus.statsLive(now - 120, l);
        bool lyonPing = false;
        for (const auto& p : l.pings) lyonPing = lyonPing || (p.city == "FR|Lyon" && p.kind == "studio");
        check(r.ok && l.active == 4 && lyonPing, "le direct : 4 PC actifs, une connexion a Lyon (" + r.message + ")");
    }

    // 1.48.1 : LA COHERENCE (docs/STATISTIQUES.md, par. 15) - le numero des changements, ce qui est supprime ici et part au
    // service avant la lecture suivante, Reevaluer (les corrections, puis rien ; une ligne au journal)
    std::printf("-- 1.48.1 : la coherence (changes, les exclusions, Reevaluer)\n");
    {
        st::Exclusions ex;
        ex.deleteClients = {"Soci\xC3\xA9t\xC3\xA9 d'essai"};
        ex.restoreLicences = {"XPG-2026-00009"};
        check(ex.body() == "{\"supprimer\":{\"clients\":[\"Soci\xC3\xA9t\xC3\xA9 d'essai\"]},\"remettre\":{\"licences\":[\"XPG-2026-00009\"]}}" &&
                  st::Exclusions{}.body() == "{}",
              "le corps de Reevaluer (" + ex.body() + ")");
        json::Value v;
        st::ReevaluateReply rr;
        check(json::parse("{\"jour\":\"2026-11-11\",\"exclus\":{\"clients\":1,\"licences\":2,\"pc\":3},\"nombre\":2,\"corrections\":[{\"groupe\":\"releves\","
                          "\"texte\":\"Licences actives, relev\xC3\xA9 du 11/11 : 17 \xE2\x86\x92 15\",\"mesure\":\"licences_actives\",\"jour\":\"2026-11-11\",\"avant\":17,"
                          "\"apres\":15},{\"groupe\":\"globe\",\"texte\":\"Globe : Lyon retir\xC3\xA9" "e\"}]}",
                          v, nullptr) &&
                  st::parseReevaluate(v, rr, nullptr) && rr.corrections.size() == 2 && rr.excludedLicences == 2 && rr.corrections[0].before == 17 &&
                  rr.corrections[0].after == 15 && rr.corrections[0].day == "2026-11-11" && !st::isNumber(rr.corrections[1].before) &&
                  st::reevaluateSummary(rr) == "2 corrections",
              "la reponse de Reevaluer lue");
        check(st::reevaluateSummary({}) == "Rien \xC3\xA0 corriger : les statistiques \xC3\xA9taient coh\xC3\xA9rentes." &&
                  st::correctionGroupTitle("releves") == "Les relev\xC3\xA9s des jours pass\xC3\xA9s" && st::correctionGroups().size() == 6,
              "les textes de Reevaluer");

        // un client supprime ici : note (sans reseau), toutes les vues averties ; parti au service avant la lecture suivante
        const auto before = license_admin::changes::current();
        st::Exclusions del;
        del.deleteClients = {"Soci\xC3\xA9t\xC3\xA9 d'essai"};
        plus.queueStatsExclusion(del);
        check(license_admin::changes::current() > before && plus.pendingStatsExclusions().deleteClients.size() == 1, "note pour le service ; le numero des changements avance");
        st::GlobeReply g;
        auto r = plus.statsGlobe(30, g);
        check(r.ok && plus.pendingStatsExclusions().empty(), "parti au service avant la lecture du globe (" + r.message + ")");
        check(g.totals.clients == 1 && g.totals.pcs == 1 && (!g.find("FR|Paris") || g.find("FR|Paris")->pcs == 0) && g.find("FR|Lyon") && g.find("FR|Lyon")->pcs == 1,
              "le globe : le client supprime n'y est plus (son PC de Paris), Atelier Exemple reste");
        st::SeriesQuery q;
        q.measures = {"licences_nouvelles", "pc_actifs"};
        q.period = st::lastDays(now, 7);
        q.compare = st::Compare::None;
        q.split = "client";
        st::SeriesReply sr;
        r = plus.statsSeries(q, sr);
        const st::Series* nv = sr.find("licences_nouvelles");
        check(r.ok && nv && nv->total == 1 && !nv->part("Soci\xC3\xA9t\xC3\xA9 d'essai") && sr.find("pc_actifs") && sr.find("pc_actifs")->points.back() == 1,
              "les courbes : plus de Societe d'essai (1 licence, 1 PC)");
        // Reevaluer : tout s'accorde deja - rien a corriger ; une ligne au journal des admins
        const auto c1 = license_admin::changes::current();
        st::ReevaluateReply re;
        std::string raw;
        r = plus.statsReevaluate(re, &raw);
        check(r.ok && re.corrections.empty() && re.excludedClients == 1 && has(r.message, "Rien \xC3\xA0 corriger") && has(raw, "\"corrections\""),
              "Reevaluer : rien a corriger (" + r.message + ")");
        check(license_admin::changes::current() > c1 && plus.journalWaiting() == 0, "Reevaluer : les vues averties, la ligne du journal envoyee");
        // restaure : de nouveau compte
        st::Exclusions back;
        back.restoreClients = {"Soci\xC3\xA9t\xC3\xA9 d'essai"};
        plus.queueStatsExclusion(back);
        r = plus.statsGlobe(30, g);
        check(r.ok && g.totals.clients == 2 && g.totals.pcs == 2, "restaure : de nouveau sur le globe (" + r.message + ")");
        r = plus.statsReevaluate(re);
        check(r.ok && re.corrections.empty() && re.excludedClients == 0, "Reevaluer encore : rien (" + r.message + ")");
    }

    if (!console.empty()) {
        std::printf("-- la console\n");
        int code = -1;
        const std::string k = " --dossier-cle \"" + U(root / "plus") + "\"";
        std::string out = runConsole(console, "stats-serie --mesures pc_actifs,licences_nouvelles --par offre --pas semaine" + k, &code);
        check(code == 0 && has(out, "pc_actifs (PC actifs, nombre, niveau, S+N) : total 2") && has(out, "PROFESSIONAL : 1") && has(out, "licences_nouvelles"),
              "stats-serie (" + out.substr(0, 300) + ")");
        out = runConsole(console, "stats-serie --mesures pc_actifs --json" + k, &code);
        json::Value v;
        check(code == 0 && json::parse(out, v, nullptr) && v.isObject() && v.find("mesures"), "stats-serie --json : la reponse telle quelle");
        out = runConsole(console, "globe" + k, &code);
        check(code == 0 && has(out, "Lyon, FR (Auvergne-Rh\xC3\xB4ne-Alpes) [45.80 4.80]") && has(out, "Bruxelles, BE"), "globe (" + out.substr(0, 300) + ")");
        out = runConsole(console, "stats-mises-a-jour" + k, &code);
        check(code == 0 && has(out, "1.46.0 (pas publi\xC3\xA9" "e) : Studios 4"), "stats-mises-a-jour (" + out.substr(0, 300) + ")");
        out = runConsole(console, "stats-admins" + k, &code);
        check(code == 0 && has(out, "admin+"), "stats-admins (" + out.substr(0, 200) + ")");
        out = runConsole(console, "stats-releve" + k, &code);
        check(code == 0 && has(out, "\xC3\xA9tait d\xC3\xA9j\xC3\xA0 pris"), "stats-releve (" + out + ")");
        out = runConsole(console, "globe-direct" + k, &code);
        check(code == 0 && has(out, "PC actif(s) maintenant"), "globe-direct (" + out.substr(0, 200) + ")");
        out = runConsole(console, "stats-serie" + k, &code);
        check(code == 2 && has(out, "--mesures"), "stats-serie sans --mesures : mauvais usage");
    }
    return finish();
}
