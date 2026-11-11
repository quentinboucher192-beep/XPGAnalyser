// =============================================================================
//  tests/licence_boutique_test.cpp - 1.46.0 : LA BOUTIQUE COTE STUDIO (app/LicenceService,
//  app/LicenceShop ; le contrat : docs/BOUTIQUE.md) contre le VRAI Worker lance sur ce
//  poste (tests/service_local.hpp) et un FAUX Stripe (tests/outils/faux_stripe.mjs)
// -----------------------------------------------------------------------------
//  - la boutique fermee (pas de reglage) : le Studio la voit fermee, rien ne s'achete ;
//  - ouverte par l'editeur (PUT /admin/boutique) : le catalogue (le montant TTC, le
//    mode test), relu par le Studio ;
//  - Acheter : la demande de ce PC scellee comme une demande en ligne (l'editeur l'ouvre :
//    l'offre, la societe, le courriel ; le nonce de la demande d'activation), la page de
//    paiement du faux Stripe ; 409 en_cours (la meme page) ; Annuler ; 502 de Stripe
//    (rien de garde) ;
//  - le paiement (un evenement signe du faux Stripe vers le webhook) : << payee >>, le recu ;
//  - LA LIVRAISON, comme l'Admin la fait (docs/BOUTIQUE.md, par. 5 et 8.11) : une licence
//    en ligne deposee, son code scelle pour ce PC dans son etat << poste >> (acceptee,
//    requestNonce = le nonce de la demande scellee avec la commande), la commande << livree >>
//    -> le Studio relit ses commandes, voit << livree >>, lit l'etat << poste >> et s'active
//    seul (la fenetre de la reponse : << Ta commande est livree >>) ;
//  - une OPTION achetee : la licence emise de nouveau avec l'option sur le service, l'etat
//    << poste >> acceptee sans code -> le Studio renouvelle et recoit l'option.
//  SAUTE sans Node.js ni wrangler (ou sous Windows).
// =============================================================================
#include "service_local.hpp"

#include "../src/app/Licence.hpp"
#include "../src/app/LicenceService.hpp"
#include "../src/app/LicenceShop.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/Service.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
namespace shop = app::licence::shop;
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

constexpr Seconds D = lic::kDay;

fs::path                         g_base;
std::string                      g_url;      // le service local
std::unique_ptr<lic::SigningKey> g_editor;   // la cle (d'essai) de l'editeur
std::string                      g_registry;
fs::path                         g_bundled;
lic::Registry                    g_reg;

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

struct Http {
    bool        reached{false};
    int         status{0};
    std::string body;
    json::Value v;
};
Http http(const std::string& base, const std::string& method, const std::string& path, const std::string& body, bool sign);

Seconds serviceTime() {
    const Http h = http(g_url, "GET", "/v1/etat", "", false);
    return h.v.isObject() ? static_cast<Seconds>(h.v.integer("heure")) : lic::systemNow();
}

Http http(const std::string& base, const std::string& method, const std::string& path, const std::string& body, bool sign) {
    ai::HttpRequest req;
    req.method = method;
    req.url = base + path;
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
Http editor(const std::string& method, const std::string& path, const std::string& body = {}) { return http(g_url, method, path, body, true); }

std::string jsonBody(std::initializer_list<std::pair<const char*, json::Value>> fields) {
    json::Value v = json::Value::object();
    for (const auto& [k, x] : fields) v.set(k, x);
    return json::write(v, false);
}

// La cle du service certifiee par l'editeur, le registre depose.
bool publisherSetup(std::string& why) {
    const Http k = editor("GET", "/admin/cle-service");
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
    const Http p = editor("PUT", "/admin/certificat", jsonBody({{"certificat", block(lic::DocKind::ServiceCert, lsv::writeCert(c))}}));
    if (p.status != 200) {
        why = "PUT /admin/certificat : " + std::to_string(p.status) + " " + p.body;
        return false;
    }
    const Http r = editor("PUT", "/admin/registre", jsonBody({{"registre", g_registry}}));
    if (r.status != 200) {
        why = "PUT /admin/registre : " + std::to_string(r.status) + " " + r.body;
        return false;
    }
    return true;
}

// Une licence en ligne de l'offre PROFESSIONAL (comme l'Admin la signe pour une commande), deposee ; `code` : son code
// (vide : celui deja depose reste - une nouvelle emission).
bool putLicence(const lic::License& l, const std::string& normalizedCode) {
    std::initializer_list<std::pair<const char*, json::Value>> withCode = {
        {"licence", block(lic::DocKind::License, lic::writeLicense(l))}, {"code_hash", lsv::codeHash(normalizedCode)}, {"bail_jours", 30}, {"active", true}};
    std::initializer_list<std::pair<const char*, json::Value>> noCode = {{"licence", block(lic::DocKind::License, lic::writeLicense(l))}};
    const Http h = editor("PUT", "/admin/licences", jsonBody(normalizedCode.empty() ? noCode : withCode));
    check(h.status == 200, "l'\xC3\xA9" "diteur d\xC3\xA9pose la licence " + l.licenseId + " (" + std::to_string(h.status) + " " + h.body.substr(0, 160) + ")");
    return h.status == 200;
}
lic::License onlineLicence(const std::string& id, const std::string& customer) {
    lic::License l;
    l.licenseId = id;
    l.customer = customer;
    l.edition = "PROFESSIONAL";
    l.type = lic::LicenseType::Temporary;
    l.service = g_url;
    l.maxDevices = 5;
    if (const lic::EditionDef* e = g_reg.edition("PROFESSIONAL")) l.features = e->features;
    lic::normalizeGrants(l.features);
    const Seconds now = serviceTime();
    l.issuedAt = now - 3600;
    l.expiresAt = now + 365 * D;
    l.offlineGraceDays = 14;
    l.manifest = g_reg.number;
    l.nonce = lic::newNonce();
    return l;
}

// La commande n chez l'editeur (GET /admin/commandes) ; nul : absente.
json::Value orderAtService(long long n) {
    const Http h = editor("GET", "/admin/commandes");
    if (const json::Value* list = h.v.find("commandes"); list && list->isArray())
        for (const auto& c : list->items())
            if (c.integer("numero") == n) return c;
    return {};
}

// La demande scellee d'une commande, ouverte par l'editeur ; son nonce (celui de la demande d'activation).
bool openOrder(const json::Value& c, lgw::OnlineRequest& q, std::string& nonce, std::string& why) {
    std::string plain;
    if (!lgw::openWith(*g_editor, c.str("scelle"), plain, &why) || !lgw::parseOnlineRequest(plain, q, &why)) return false;
    std::vector<lic::SignedBlock> blocks;
    lic::ActivationRequest req;
    if (!lic::readBlocks(q.request, blocks, &why) || blocks.empty() || !lic::parseRequest(blocks[0].document, req, &why)) return false;
    nonce = req.nonce;
    return true;
}

// L'etat << poste >> d'une commande livree, publie par l'editeur (comme l'Admin : online::answerOrder).
bool publishDelivery(const json::Value& c, const std::string& requestNonce, int number, const lgw::StatusContent& content, std::string& why) {
    lic::crypto::Bytes kb;
    lic::crypto::PublicKey pk{};
    if (!lic::crypto::unhex(c.str("cle_pc"), kb, pk.size())) {
        why = "cle_pc illisible";
        return false;
    }
    std::copy(kb.begin(), kb.end(), pk.begin());
    lgw::DeviceStatus s;
    s.device = lsv::deviceId(pk);
    s.number = static_cast<std::uint64_t>(number);
    s.issuedAt = lic::systemNow();
    s.state = lgw::State::Accepted;
    s.requestNonce = requestNonce;
    s.sealed = lgw::sealFor(pk, lgw::writeStatusContent(content));
    const Http put = editor("PUT", "/admin/postes", jsonBody({{"poste", s.device}, {"etat", block(lic::DocKind::DeviceStatus, lgw::writeDeviceStatus(s))}}));
    if (put.status != 200) {
        why = "PUT /admin/postes : " + std::to_string(put.status) + " " + put.body.substr(0, 160);
        return false;
    }
    return true;
}

// Le faux Stripe : un evenement signe envoye au webhook du service ; la session d'une commande.
Http stripeEvent(const std::string& stripe, const std::string& secret, const std::string& type, const std::string& session) {
    json::Value b = json::Value::object();
    b.set("url", g_url + "/v1/boutique/stripe");
    b.set("secret", secret);
    b.set("type", type);
    b.set("session", session);
    return http(stripe, "POST", "/__essai/evenement", json::write(b, false), false);
}
std::string sessionOf(const std::string& stripe, long long number) {
    const Http r = http(stripe, "GET", "/__essai/sessions", {}, false);
    if (const json::Value* a = r.v.find("sessions"); a && a->isArray())
        for (const auto& s : a->items())
            if (s.integer("numero") == number) return s.str("id");
    return {};
}

lic::StoreOptions pcOptions(const std::string& name, const std::string& machine) {
    lic::StoreOptions o;
    o.folder = U(g_base / "pcs" / name);
    o.trustedKeys = {trustedOf(*g_editor)};
    o.compiledKeys = false;
    o.bundledRegistry = U(g_bundled);
    o.machineId = machine;
    o.evaluationPeriod = true;
    o.serviceUrl = g_url;
    return o;
}

ls::Record record() {
    ls::Record r;
    std::string why;
    (void)ls::loadRecord(r, &why);
    return r;
}
const shop::Order* orderIn(const ls::Record& r, std::uint64_t n) { return shop::findOrder(r.orders, n); }

app::licence::Answer answerOf(const ls::Result& r) {
    app::licence::Answer a;
    a.kind = r.answer;
    a.licenseId = r.licenseId;
    a.note = r.answerNote;
    a.online = r.answerOnline;
    a.code = r.answerCode;
    a.mail = r.answerMail;
    a.mailTo = r.answerMailTo;
    a.option = r.answerOption;
    a.order = r.answerOrder;
    return a;
}
bool noticeHas(const ls::Result& r, const std::string& part) {
    for (const auto& n : r.notices)
        if (has(n.title, part)) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
#if defined(_WIN32)
    (void)repo;
    std::printf("SAUTE : le service local et le faux Stripe ne sont lances que sous Linux\n");
    return 0;
#else
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::uint8_t rnd[10];
    deploy::crypto::randomBytes(rnd, sizeof rnd);
    const std::string tag = deploy::crypto::hex(rnd, sizeof rnd);
    g_base = fs::temp_directory_path() / ("xpg-licence-boutique-" + tag.substr(0, 12));
    std::error_code ec;
    fs::remove_all(g_base, ec);
    fs::create_directories(g_base, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{g_base};

    std::string why;
    g_editor = adm::createPublisherKey(U(g_base / "editeur"), "essai boutique Studios", true, &why);
    check(g_editor != nullptr, "la cl\xC3\xA9 d'essai de l'\xC3\xA9" "diteur (" + why + ")");
    if (!g_editor) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    check(adm::signRegistry(*g_editor, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), lic::systemNow() - 60, g_registry, &why),
          "le registre sign\xC3\xA9 (" + why + ")");
    g_bundled = g_base / "livre" / "registre.xpgreg";
    writeFile(g_bundled, g_registry);
    {
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(g_registry, blocks, &why) && blocks.size() == 1 && lic::parseRegistry(blocks[0].document, g_reg, &why),
              "le registre se relit (" + why + ")");
    }

    // ---- le faux Stripe, puis le service (ses secrets de Stripe : des valeurs d'essai) ----
    const std::string kKey = "essai_jeton_sk_test_" + tag.substr(0, 16);
    const std::string kHook = "essai_jeton_whsec_" + tag.substr(4, 16);
    xpg_test::FakeStripeProcess stripe(U(repo), kKey);
    if (!stripe.ready()) {
        std::printf("SAUTE : le faux Stripe ne d\xC3\xA9marre pas (Node.js manque ?)\n");
        return 0;
    }
    xpg_test::LocalService::Options so;
    so.env = {{"XPG_STRIPE_SECRET_KEY", kKey}, {"XPG_STRIPE_WEBHOOK_SECRET", kHook}, {"XPG_STRIPE_API_BASE", stripe.base()}};
    xpg_test::LocalService svc({g_editor->publicKeyHex()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return 0;
    }
    check(svc.ready(), "le service local d\xC3\xA9marre (" + svc.error() + ")");
    if (!svc.ready()) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    g_url = svc.url();
    std::printf("   (service local : %s, %.1f s ; faux Stripe : %s)\n", g_url.c_str(), svc.startSeconds(), stripe.base().c_str());
    lsv::Address address;
    address.url = g_url;
    app::licence::start(pcOptions("A", "machine-boutique-1"));
    app::licence::startNotices(false);   // les avis, sans le fil de fond
    ls::setAddressForTests(address);
    check(publisherSetup(why), "l'\xC3\xA9" "diteur certifie la cl\xC3\xA9 du service, d\xC3\xA9pose le registre (" + why + ")");

    ls::OrderForm form;
    form.customer = "Client d'essai";
    form.name = "Camille Essai";
    form.email = "camille.essai@exemple.fr";
    form.pcName = "Poste essai boutique";

    std::printf("-- la boutique fermee (aucun reglage) : rien ne s'achete\n");
    {
        auto r = ls::sync();
        const shop::Catalogue cat = shop::catalogue();
        // 1.47.0 : le service de ce depot est plus recent (les statistiques) ; la boutique demande 1.46.0 ou plus
        check(r.reached && cat.known && !cat.selling() && shop::serviceKnowsShop(cat.serviceVersion) && !cat.edition("PROFESSIONAL"),
              "un service 1.46.0 ou plus, boutique ferm\xC3\xA9" "e : connue, aucun article (" + cat.serviceVersion + ")");
        ls::OrderForm f = form;
        f.article = "ed:PROFESSIONAL";
        r = ls::placeOrder(f);
        check(!r.ok && !r.reached && has(r.message, "pas (ou plus) en vente en ligne"), "Acheter : pas en vente, rien ne part (" + r.message + ")");
    }

    std::printf("-- l'editeur ouvre la boutique : le catalogue relu par le Studio\n");
    {
        const std::string settings =
            "{\"active\":true,\"livraison_auto\":true,\"devise\":\"eur\",\"articles\":["
            "{\"id\":\"ed:PROFESSIONAL\",\"genre\":\"edition\",\"edition\":\"PROFESSIONAL\",\"montant\":49000,\"libelle\":\"Licence PROFESSIONAL - 1 an\"},"
            "{\"id\":\"opt:ia_avancee:30\",\"genre\":\"option\",\"option\":\"ia_avancee\",\"jours\":30,\"montant\":2900,\"libelle\":\"Option Assistant IA - 1 mois\"}]}";
        const Http put = editor("PUT", "/admin/boutique", settings);
        check(put.status == 200, "PUT /admin/boutique (" + std::to_string(put.status) + " " + put.body.substr(0, 160) + ")");
        const auto r = ls::refreshShop();
        const shop::Catalogue cat = shop::catalogue();
        check(r.ok && r.shopLooked && cat.selling() && cat.test() && cat.edition("PROFESSIONAL") && cat.edition("PROFESSIONAL")->amount == 49000 &&
                  cat.option("ia_avancee", 30) && cat.articles.size() == 2 && has(r.message, "ouverte"),
              "le catalogue : PROFESSIONAL 490,00 TTC, l'option IA 1 mois, paiement d'essai (" + r.message + ")");
        check(shop::buyLabel(*cat.edition("PROFESSIONAL"), cat.currency) == "Acheter \xE2\x80\x94 490,00\xC2\xA0\xE2\x82\xAC TTC", "le bouton : Acheter -- 490,00 EUR TTC");
        check(fs::exists(fs::path(shop::cachePath())), "le catalogue gard\xC3\xA9 dans le dossier des licences (boutique.json)");
    }

    std::printf("-- Acheter : la demande scellee, la page de paiement ; 409 en_cours ; Annuler ; 502 de Stripe\n");
    std::uint64_t n1 = 0, n2 = 0;
    std::string nonce2;
    {
        ls::OrderForm f = form;
        f.article = "ed:PROFESSIONAL";
        ls::OrderForm noMail = f;
        noMail.email.clear();
        auto r = ls::placeOrder(noMail);
        check(!r.ok && !r.reached && has(r.message, "courriel"), "sans courriel : rien ne part (" + r.message + ")");
        r = ls::placeOrder(f);
        n1 = r.orderNumber;
        auto rec = record();
        const shop::Order* o = orderIn(rec, n1);
        check(r.ok && n1 > 0 && has(r.orderUrl, "https://checkout.stripe.invalid/pay/") && !r.orderReopened && o && o->state == shop::kToPay &&
                  !o->nonce.empty() && o->mode == "test" && o->amount == 49000 && o->payUrl == r.orderUrl && o->expires > lic::systemNow(),
              "la commande n\xC2\xB0 " + std::to_string(n1) + " : la page de paiement (" + r.orderUrl + "), gard\xC3\xA9" "e dans le relev\xC3\xA9 avec son nonce");
        const json::Value c = orderAtService(static_cast<long long>(n1));
        lgw::OnlineRequest q;
        std::string nonce;
        check(c.isObject() && c.str("etat") == "a_payer" && c.str("article") == "ed:PROFESSIONAL" && c.str("societe") == form.customer &&
                  c.str("courriel") == form.email && c.str("nom") == form.name && openOrder(c, q, nonce, why) && q.edition == "PROFESSIONAL" &&
                  q.customer == form.customer && q.email == form.email && q.option.empty() && o && nonce == o->nonce,
              "l'\xC3\xA9" "diteur ouvre la commande : la demande scell\xC3\xA9" "e comme une demande en ligne (l'offre, la soci\xC3\xA9t\xC3\xA9, le courriel, le nonce) (" + why + ")");
        // une seconde commande pendant que la premiere attend : la meme page (409 en_cours)
        r = ls::placeOrder(f);
        check(!r.ok && r.orderReopened && r.orderNumber == n1 && o && r.orderUrl == o->payUrl && has(r.message, "attend d\xC3\xA9j\xC3\xA0 son paiement"),
              "409 en_cours : la page de la commande qui attend, rouverte (" + r.message + ")");
        // Annuler
        r = ls::cancelOrder(n1);
        rec = record();
        check(r.ok && orderIn(rec, n1) && orderIn(rec, n1)->state == shop::kCancelled && orderAtService(static_cast<long long>(n1)).str("etat") == "annulee" &&
                  has(r.message, "rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9"),
              "Annuler la commande : annul\xC3\xA9" "e chez le service (" + r.message + ")");
        // 502 : Stripe refuse la page de paiement - rien de garde
        const Http down = http(stripe.base(), "POST", "/__essai/regler", "{\"panne\":\"checkout\"}", false);
        r = ls::placeOrder(f);
        const auto after = ls::refreshOrders();
        check(down.status == 200 && !r.ok && !r.orderNumber && has(r.message, "La page de paiement ne s'ouvre pas") && has(r.message, "Stripe refuse") &&
                  has(r.message, "rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9") && after.ok && after.orders.size() == 1,
              "502 de Stripe : une phrase claire, rien de gard\xC3\xA9 (" + r.message + ")");
        (void)http(stripe.base(), "POST", "/__essai/regler", "{\"panne\":\"\"}", false);
        r = ls::placeOrder(f);
        n2 = r.orderNumber;
        rec = record();
        check(r.ok && n2 > n1 && orderIn(rec, n2) && orderIn(rec, n2)->state == shop::kToPay, "une nouvelle commande, n\xC2\xB0 " + std::to_string(n2));
        if (orderIn(rec, n2)) nonce2 = orderIn(rec, n2)->nonce;
        (void)ls::refreshOrders();   // la premiere relecture : les dates du service
        const auto again = ls::refreshOrders();
        check(again.ok && again.ordersLooked && again.orders.size() == 2 && !again.changed && again.notices.empty(), "relues sans changement : rien \xC3\xA0 dire (" + again.message + ")");
    }

    std::printf("-- le paiement (le webhook du faux Stripe) : payee, le recu\n");
    {
        const std::string session = sessionOf(stripe.base(), static_cast<long long>(n2));
        const Http ev = stripeEvent(stripe.base(), kHook, "checkout.session.completed", session);
        check(!session.empty() && ev.status == 200 && ev.v.integer("statut") == 200, "l'\xC3\xA9v\xC3\xA9nement sign\xC3\xA9 accept\xC3\xA9 par le service (" + ev.body.substr(0, 120) + ")");
        const auto r = ls::refreshOrders();
        const auto rec = record();
        const shop::Order* o = orderIn(rec, n2);
        check(r.ok && r.changed && o && o->state == shop::kPaid && has(o->receiptUrl, "https://invoice.stripe.invalid/") &&
                  noticeHas(r, "Paiement re\xC3\xA7u pour la commande n\xC2\xB0 " + std::to_string(n2)) && lic::getLicenseState().licenseId.empty(),
              "pay\xC3\xA9" "e : << Paiement re\xC3\xA7u... ta licence arrive d\xC3\xA8s que l'\xC3\xA9" "diteur la d\xC3\xA9livre >>, le re\xC3\xA7u (" + r.message + ")");
        const auto v = app::licence::orderView(*o, lic::systemNow());
        check(has(v.text, "Paiement re\xC3\xA7u : ta licence arrive") && v.receipt && !v.pay, "la fen\xC3\xAAtre : pay\xC3\xA9" "e, Ouvrir le re\xC3\xA7u");
    }

    std::printf("-- la livraison (comme l'Admin) : le Studio s'active seul\n");
    const std::string delivered = "XPG-2026-00901";
    {
        const json::Value c = orderAtService(static_cast<long long>(n2));
        lgw::OnlineRequest q;
        std::string nonce;
        check(c.isObject() && c.str("etat") == "payee" && openOrder(c, q, nonce, why) && nonce == nonce2, "l'\xC3\xA9" "diteur ouvre la commande pay\xC3\xA9" "e (" + why + ")");
        const std::string code = lsv::newActivationCode();
        std::string norm;
        (void)lsv::normalizeCode(code, norm);
        lic::License l = onlineLicence(delivered, c.str("societe"));
        const bool put = putLicence(l, norm);
        lgw::StatusContent content;
        content.onlineLicense = delivered;
        content.code = code;
        content.message = "Ta licence " + delivered + " (PROFESSIONAL, commande n\xC2\xB0 " + std::to_string(n2) + ") est pr\xC3\xAAte : ton Studio l'active tout seul.";
        const bool published = publishDelivery(c, nonce, 1, content, why);
        const Http take = editor("POST", "/admin/commandes/prendre", jsonBody({{"numero", n2}}));
        const Http done = editor("POST", "/admin/commandes/livree", jsonBody({{"numero", n2}, {"licence", delivered}}));
        check(put && published && take.status == 200 && done.status == 200, "livr\xC3\xA9" "e chez le service (" + why + std::to_string(done.status) + " " + done.body.substr(0, 120) + ")");
        const auto r = ls::refreshOrders();
        const auto rec = record();
        const auto st = lic::getLicenseState();
        const shop::Order* o = orderIn(rec, n2);
        check(r.ok && o && o->state == shop::kDelivered && o->applied && o->licence == delivered && st.licenseId == delivered && st.online &&
                  st.status == lic::LicenseStatus::Valid,
              "livr\xC3\xA9" "e : le Studio lit son \xC3\xA9tat << poste >> et s'active seul (" + r.message + ")");
        check(r.answer == ls::kAnswerAccepted && r.answerOnline && r.answerOrder == n2 && r.licenseInstalled && !has(r.message, code),
              "la r\xC3\xA9ponse : la commande livr\xC3\xA9" "e (la fen\xC3\xAAtre qui le confirme), jamais le code dans la phrase");
        const auto v = app::licence::answerView(answerOf(r), st);
        check(v.accepted && v.title == "Ta commande est livr\xC3\xA9" "e" && has(v.text, "Ta commande n\xC2\xB0 " + std::to_string(n2) + " est livr\xC3\xA9" "e") &&
                  has(v.text, delivered),
              "la fen\xC3\xAAtre : << Ta commande est livr\xC3\xA9" "e >>, la licence");
        const auto again = ls::refreshOrders();
        check(again.ok && again.answer.empty() && !again.licenseInstalled, "relues de nouveau : pas de seconde activation (" + again.message + ")");
        bool inHistory = false;
        for (const auto& h : lic::getHistory(200)) inHistory = inHistory || has(h.text, code) || has(h.text, norm);
        check(!inHistory, "le code n'est pas dans l'historique");
    }

    std::printf("-- une option achetee : la licence emise de nouveau, le Studio renouvelle\n");
    {
        ls::OrderForm f = form;
        f.article = "opt:ia_avancee:30";
        f.licenseId = delivered;
        f.optionText = "Assistant IA avanc\xC3\xA9, 1 mois";
        auto r = ls::placeOrder(f);
        const std::uint64_t n3 = r.orderNumber;
        auto rec = record();
        const shop::Order* o = orderIn(rec, n3);
        check(r.ok && n3 > n2 && o && o->option == "ia_avancee" && o->optionText == f.optionText && !o->nonce.empty(),
              "la commande d'option n\xC2\xB0 " + std::to_string(n3) + " (le relev\xC3\xA9 garde l'option et son nonce)");
        json::Value c = orderAtService(static_cast<long long>(n3));
        lgw::OnlineRequest q;
        std::string nonce;
        check(c.isObject() && c.str("licence_existante") == delivered && openOrder(c, q, nonce, why) && q.option == "ia_avancee" && q.optionDays == 30 &&
                  q.edition.empty() && o && nonce == o->nonce,
              "l'\xC3\xA9" "diteur lit l'option et sa dur\xC3\xA9" "e, la licence existante (" + why + ")");
        const Http ev = stripeEvent(stripe.base(), kHook, "checkout.session.completed", sessionOf(stripe.base(), static_cast<long long>(n3)));
        r = ls::refreshOrders();
        check(ev.status == 200 && orderIn(record(), n3) && orderIn(record(), n3)->state == shop::kPaid && noticeHas(r, "ton option arrive"),
              "pay\xC3\xA9" "e : << ton option arrive >> (" + r.message + ")");
        // la licence emise de nouveau avec l'option (la meme, une emission plus recente), puis l'etat << poste >> sans code
        lic::License l = onlineLicence(delivered, "Client d'essai");
        const Seconds now = serviceTime();
        l.issuedAt = now - 60;
        lic::LicenseOption opt;
        opt.id = "ia_avancee";
        opt.label = "Assistant IA avanc\xC3\xA9";
        lic::FeatureGrant g;
        g.id = "ai_advanced_models";
        opt.features = {g};
        opt.from = now - 60;
        opt.until = now + 30 * D;
        l.options = {opt};
        const bool put = putLicence(l, {});
        lgw::StatusContent content;
        content.onlineLicense = delivered;
        content.message = "Ton option (commande n\xC2\xB0 " + std::to_string(n3) + ") est pr\xC3\xAAte.";
        c = orderAtService(static_cast<long long>(n3));
        const bool published = publishDelivery(c, nonce, 2, content, why);
        (void)editor("POST", "/admin/commandes/prendre", jsonBody({{"numero", n3}}));
        const Http done = editor("POST", "/admin/commandes/livree", jsonBody({{"numero", n3}, {"licence", delivered}}));
        check(put && published && done.status == 200, "l'option livr\xC3\xA9" "e chez le service (" + why + ")");
        r = ls::refreshOrders();
        rec = record();
        const auto st = lic::getLicenseState();
        bool hasOption = false;
        for (const auto& x : st.options) hasOption = hasOption || (x.id == "ia_avancee" && x.activeAt(st.trustedNow ? st.trustedNow : lic::systemNow(), st.expiresAt));
        check(r.ok && orderIn(rec, n3) && orderIn(rec, n3)->state == shop::kDelivered && orderIn(rec, n3)->applied && hasOption && st.licenseId == delivered,
              "livr\xC3\xA9" "e : le Studio renouvelle et re\xC3\xA7oit l'option (" + r.message + ")");
        check(r.answer == ls::kAnswerAccepted && r.answerOrder == n3 && r.answerOption == f.optionText,
              "la r\xC3\xA9ponse : << Ton option est arriv\xC3\xA9" "e >> (la commande n\xC2\xB0 " + std::to_string(n3) + ")");
        const auto v = app::licence::answerView(answerOf(r), st);
        check(v.title == "Ton option est arriv\xC3\xA9" "e" && has(v.text, "Ta commande n\xC2\xB0 " + std::to_string(n3) + " est livr\xC3\xA9" "e") && has(v.text, f.optionText),
              "la fen\xC3\xAAtre : l'option, la commande");
    }

    std::printf("-- Mes achats : les commandes de ce PC\n");
    {
        const auto r = ls::refreshOrders();
        check(r.ok && r.orders.size() == 3 && r.orders[0].number > r.orders[1].number && r.orders[2].state == shop::kCancelled &&
                  !r.orders[0].receiptUrl.empty() && !r.orders[1].receiptUrl.empty() && r.orders[2].receiptUrl.empty(),
              "trois commandes (la plus r\xC3\xA9" "cente d'abord), les re\xC3\xA7us des pay\xC3\xA9" "es");
    }

    std::printf("-- 1.48.1 : Actualiser (les notifications) - le releve tout de suite aupres du service, sa phrase\n");
    {
        const std::string why = ls::startRefreshNotices();
        check(why.empty(), "Actualiser : parti (" + why + ")");
        for (int i = 0; i < 3000 && ls::refreshingNotices(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const std::string note = ls::refreshNoticesNote();
        check(!ls::refreshingNotices() && has(note, "Notifications relev\xC3\xA9" "es aupr\xC3\xA8s du service"), "Actualiser : fini, sa phrase (" + note + ")");
        for (int i = 0; i < 3000; ++i) {   // le travail releve ; une synchronisation qu'il aurait lancee, finie aussi
            ls::poll();
            if (!ls::busy()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(!ls::busy(), "Actualiser : plus rien ne tourne ensuite");
    }

    std::printf("-- 1.48.0 : le panier - une commande de plusieurs lignes (une page de paiement), une demande sans paiement (chaque ligne se valide)\n");
    {
        shop::CartLine pro;
        pro.article = "ed:PROFESSIONAL";
        pro.kind = "edition";
        pro.edition = "PROFESSIONAL";
        pro.label = "PROFESSIONAL";
        shop::CartLine opt;
        opt.article = "opt:ia_avancee:30";
        opt.kind = "option";
        opt.option = "ia_avancee";
        opt.days = 30;
        opt.label = "Assistant IA avanc\xC3\xA9, 1 mois";   // avec l'offre du panier (avec_ligne)
        ls::OrderForm f = form;
        f.lines = {pro, opt};
        f.payment = true;
        auto r = ls::placeOrder(f);
        const json::Value c = orderAtService(static_cast<long long>(r.orderNumber));
        const json::Value* cl = c.find("lignes");
        check(r.ok && r.orderNumber > 0 && has(r.orderUrl, "https://checkout.stripe.invalid/pay/") && cl && cl->isArray() && cl->items().size() == 2 &&
                  cl->items()[1].integer("avec_ligne") == 1 && c.integer("montant") == 49000 + 2900,
              "un panier pay\xC3\xA9 : une commande de deux lignes, une page de paiement, l'option avec la ligne de l'offre (" + r.message + ")");
        auto rec = record();
        const shop::Order* o = orderIn(rec, r.orderNumber);
        check(o && o->lines.size() == 2 && o->state == shop::kToPay && o->payment, "le relev\xC3\xA9 garde ses lignes");
        const auto cancel = ls::cancelOrder(r.orderNumber);
        check(cancel.ok, "annul\xC3\xA9" "e (" + cancel.message + ")");
        // une demande sans paiement : une offre << sur demande >> (pas dans le catalogue) ; l'editeur refuse la ligne
        shop::CartLine ent;
        ent.article = "ed:ENTERPRISE";
        ent.kind = "edition";
        ent.edition = "ENTERPRISE";
        ent.label = "ENTERPRISE (sur demande)";
        ent.request = true;
        f.lines = {ent};
        f.payment = false;
        r = ls::placeOrder(f);
        const std::uint64_t nd = r.orderNumber;
        rec = record();
        o = orderIn(rec, nd);
        check(r.ok && nd > 0 && r.orderUrl.empty() && o && o->state == shop::kToValidate && !o->payment && o->lines.size() == 1,
              "une demande sans paiement : << \xC3\xA0 valider >>, aucune page de paiement (" + r.message + ")");
        const Http refuse = editor("POST", "/admin/commandes/ligne", jsonBody({{"numero", nd}, {"ligne", 1}, {"action", "refusee"}, {"raison", "Essai : pas pour ce poste"}}));
        r = ls::refreshOrders();
        rec = record();
        o = orderIn(rec, nd);
        check(refuse.status == 200 && r.ok && o && o->lines.size() == 1 && o->lines[0].state == shop::kRefused && has(o->lines[0].error, "Essai") &&
                  o->state == shop::kRefused,
              "l'\xC3\xA9" "diteur refuse la ligne : le Studio la voit refus\xC3\xA9" "e, avec sa raison (" + std::to_string(refuse.status) + " " + refuse.body.substr(0, 120) + ")");
    }

    std::printf("-- le service ne repond plus : une phrase, rien ne change\n");
    {
        lsv::Address dead;
        dead.url = "http://127.0.0.1:1";
        ls::setAddressForTests(dead);
        ls::OrderForm f = form;
        f.article = "ed:PROFESSIONAL";
        const auto r = ls::placeOrder(f);
        const auto o = ls::refreshOrders();
        check(!r.ok && !r.reached && has(r.message, "Hors ligne") && has(r.message, "rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "bit\xC3\xA9") && !o.ok && !o.reached &&
                  record().orders.size() == 5,
              "hors ligne : << " + r.message + " >>");
        ls::setAddressForTests(address);
    }

    ls::setAddressForTests(std::nullopt);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
#endif
}
