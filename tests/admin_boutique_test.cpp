// =============================================================================
//  tests/admin_boutique_test.cpp - 1.46.0 : LA BOUTIQUE ET LES COMMANDES DANS
//  L'OUTIL DE L'EDITEUR (license_admin/Shop, gui/WorkspaceShop.cpp ; docs/
//  BOUTIQUE.md), contre le VRAI Worker lance sur ce poste (tests/service_local.hpp)
//  et un FAUX Stripe (tests/outils/faux_stripe.mjs) - jamais le vrai Stripe
// -----------------------------------------------------------------------------
//  Sans reseau : les montants, les modes, le catalogue (ecrit, relu, verifie), le
//  masque des secrets. Puis toute la boucle : l'admin+ (une cle d'ESSAI) regle la
//  boutique ; un PC commande (la demande scellee, comme un Studio) ; Stripe dit
//  << paye >> (un webhook signe par le faux Stripe) ; l'Admin livre seul : la
//  licence en ligne de l'offre signee, deposee, son code scelle dans l'etat
//  << poste >> du PC (qui s'active avec lui), le journal, la commande << livree >> ;
//  une seconde livraison refusee ; une option ajoutee a sa licence ; une commande
//  a revoir (une licence inconnue) ; << Livrer seul >> decoche ; le remboursement ;
//  BRANCHER STRIPE avec un faux kit (un faux wrangler qui garde l'empreinte de ce
//  qu'il recoit sur son entree standard) : le webhook cree chez le faux Stripe, les
//  deux secrets poses, rien d'ecrit ni d'affiche ; la console (argv[2]).
//  1.46.1 : le montant suit le registre (le prix affiche HT + 20 %, la reduction
//  et sa fin, un montant libre, un reglage d'avant) ; la page de paiement de
//  l'API de Stripe d'aujourd'hui (le faux Stripe refuse payment_method_types) ;
//  Stripe-Version et api_version ; les produits et les prix chez Stripe (crees
//  par le service) ; la boutique qui suit un registre depose.
//  Les valeurs des secrets : essai_jeton_... ; des noms d'essai.
//
//    admin_boutique_test SOURCE [XPG-License-Admin]
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/ServiceDeploy.hpp"
#include "../src/license_admin/Shop.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;
namespace gw = licensing::gateway;
namespace json = core::json;
namespace on = license_admin::online;
namespace shop = license_admin::shop;
namespace gui = license_admin::gui;
namespace dep = license_admin::deployer;

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
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
std::string hex32() {
    std::mt19937_64 rng(std::random_device{}());
    char b[40];
    std::snprintf(b, sizeof b, "%016llx%016llx", static_cast<unsigned long long>(rng()), static_cast<unsigned long long>(rng()));
    return b;
}

struct Reply {
    int         status{0};
    std::string body;
    json::Value value;
};

// Un appel HTTP (signe par `key` si donnee, a l'heure du service `base`).
Reply call(const std::string& base, const std::string& method, const std::string& path, const std::string& body, const lic::SigningKey* key) {
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

// ---- un PC d'essai (comme un Studio) ----
struct Pc {
    std::unique_ptr<lic::SigningKey> key;
    std::string                      name;
    Pc(const fs::path& dir, const std::string& n) : name(n) {
        std::string why;
        key = lic::SigningKey::create(U(dir / "identite-pc.cle"), lic::deviceKeySpec(), n, false, &why);
    }
    [[nodiscard]] std::string station() const { return lic::stationHash("machine d'essai " + name, key->publicKey()); }
    [[nodiscard]] std::string request(std::string* nonce = nullptr) const {
        lic::ActivationRequest r;
        r.deviceKey = key->publicKeyHex();
        r.deviceName = name;
        r.software = "IHM Studio 1.46.0 (essai)";
        r.createdAt = lic::systemNow();
        r.nonce = hex32();
        r.station = station();
        if (nonce) *nonce = r.nonce;
        return lic::writeBlock(lic::signBlock(lic::DocKind::Request, lic::writeRequest(r), key->publicKey(), [this](std::string_view m) { return key->sign(m); }));
    }
    // Le corps de POST /v1/boutique/commande : la demande scellee pour l'editeur (comme POST /v1/demandes).
    [[nodiscard]] std::string order(const lic::SigningKey& editor, const std::string& article, const std::string& company, const std::string& existing,
                                    std::string* nonce = nullptr) const {
        gw::OnlineRequest o;
        o.request = request(nonce);
        o.customer = company;
        o.email = "camille@exemple.invalid";
        o.contact = "Camille Essai";
        json::Value b = json::Value::object();
        b.set("article", article);
        b.set("societe", company);
        b.set("nom", "Camille Essai");
        b.set("courriel", "camille@exemple.invalid");
        if (!existing.empty()) b.set("licence_existante", existing);
        b.set("cle", editor.keyId());
        b.set("scelle", gw::sealFor(editor.publicKey(), gw::writeOnlineRequest(o)));
        return json::write(b, false);
    }
};

bool readPosteState(const std::string& base, const Pc& pc, const lic::SigningKey& editor, gw::DeviceStatus& st, gw::StatusContent& content) {
    const Reply r = call(base, "GET", "/v1/poste", {}, pc.key.get());
    if (r.status != 200 || !r.value.isObject() || !r.value.find("poste") || !r.value.at("poste").isString()) return false;
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(r.value.at("poste").asString(), blocks, nullptr) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::DeviceStatus) return false;
    if (!lic::verifyBlock(blocks[0], editor.publicKey())) return false;
    if (!gw::parseDeviceStatus(blocks[0].document, st, nullptr)) return false;
    content = gw::StatusContent{};
    if (st.sealed.empty()) return true;
    std::string plain;
    return gw::openWith(*pc.key, st.sealed, plain, nullptr) && gw::parseStatusContent(plain, content, nullptr);
}

// ---- le faux Stripe (node tests/outils/faux_stripe.mjs) ----
class FakeStripe {
public:
    FakeStripe(const std::string& source, const std::string& key) {
#if !defined(_WIN32)
        int fds[2];
        if (::pipe(fds) != 0) return;
        const std::string script = source + "/tests/outils/faux_stripe.mjs";
        const pid_t parent = ::getpid();
        const pid_t pid = ::fork();
        if (pid == 0) {
            ::setsid();
            ::dup2(fds[1], 1);
            ::close(fds[0]);
            ::close(fds[1]);
            xpg_test::childHygiene(parent);   // 1.46.0 : il meurt avec l'essai, sans heriter du verrou de la construction
            ::execlp("node", "node", script.c_str(), "--cle", key.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ::close(fds[1]);
        pid_ = pid;
        std::string out;
        for (int i = 0; i < 150 && port_ == 0; ++i) {
            pollfd p{fds[0], POLLIN, 0};
            if (::poll(&p, 1, 100) > 0) {
                char buf[256];
                const ssize_t n = ::read(fds[0], buf, sizeof buf);
                if (n <= 0) break;
                out.append(buf, static_cast<std::size_t>(n));
                const auto at = out.find("port ");
                if (at != std::string::npos && out.find('\n', at) != std::string::npos) port_ = std::atoi(out.c_str() + at + 5);
            }
        }
        ::close(fds[0]);
#else
        (void)source;
        (void)key;
#endif
    }
    ~FakeStripe() {
#if !defined(_WIN32)
        if (pid_ > 0) {
            ::kill(-pid_, SIGTERM);
            int st = 0;
            ::waitpid(pid_, &st, 0);
        }
#endif
    }
    [[nodiscard]] bool ready() const { return port_ > 0; }
    [[nodiscard]] std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }
    // Un evenement signe comme Stripe, envoye au webhook du service.
    Reply event(const std::string& service, const std::string& secret, const std::string& type, const std::string& session, long long amount = -1) const {
        json::Value b = json::Value::object();
        b.set("url", service + "/v1/boutique/stripe");
        b.set("secret", secret);
        b.set("type", type);
        b.set("session", session);
        if (amount >= 0) b.set("montant", static_cast<std::int64_t>(amount));
        return call(base(), "POST", "/__essai/evenement", json::write(b, false), nullptr);
    }
    [[nodiscard]] std::string sessionOf(int number) const {
        const Reply r = call(base(), "GET", "/__essai/sessions", {}, nullptr);
        if (const json::Value* a = r.value.find("sessions"); a && a->isArray())
            for (const auto& s : a->items())
                if (s.integer("numero") == number) return s.str("id");
        return {};
    }
    [[nodiscard]] json::Value requests() const { return call(base(), "GET", "/__essai/requetes", {}, nullptr).value; }

private:
    int port_{0};
#if !defined(_WIN32)
    pid_t pid_{-1};
#endif
};

const shop::Order* orderOf(const shop::OrdersView& v, int number) { return v.find(number); }

// La console (argv[2]) : sa sortie (et son code).
std::string runConsole(const std::string& exe, const std::string& args, int* code = nullptr) {
    std::string out;
#if !defined(_WIN32)
    FILE* p = ::popen(("\"" + exe + "\" " + args + " 2>&1").c_str(), "r");
    if (!p) return out;
    char buf[1024];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int st = ::pclose(p);
    if (code) *code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#else
    (void)exe;
    (void)args;
    (void)code;
#endif
    return out;
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
    const fs::path root = fs::temp_directory_path() / ("xpg-admin-boutique-" + std::to_string(rng() % 1000000000ULL));
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
    const std::string kKey = "essai_jeton_sk_test_" + hex32().substr(0, 20);
    const std::string kHook = "essai_jeton_whsec_" + hex32().substr(0, 20);

    // ======================================================================== sans reseau
    std::printf("-- les montants, les modes, le catalogue (sans reseau)\n");
    {
        check(shop::amountText(49000) == "490,00\xC2\xA0\xE2\x82\xAC" && shop::amountText(149050) == "1\xC2\xA0" "490,50\xC2\xA0\xE2\x82\xAC" && shop::amountText(5) == "0,05\xC2\xA0\xE2\x82\xAC",
              "les montants : 490,00 EUR, 1 490,50 EUR, 0,05 EUR (" + shop::amountText(149050) + ")");
        std::int64_t c = 0;
        check(shop::parseAmount("490", c) && c == 49000 && shop::parseAmount("490,5", c) && c == 49050 && shop::parseAmount("1 490.50 \xE2\x82\xAC", c) && c == 149050 &&
                  shop::parseAmount("99 EUR TTC", c) && c == 9900,
              "lire un montant : 490 ; 490,5 ; 1 490.50 EUR ; 99 EUR TTC");
        check(!shop::parseAmount("abc", c) && !shop::parseAmount("4,999", c) && !shop::parseAmount("", c) && !shop::parseAmount("1,2,3", c), "un montant illisible : refuse");
        check(shop::modeOfKey("sk_live_" + std::string(20, 'a')) == "reel" && shop::modeOfKey("rk_live_" + std::string(20, 'a')) == "reel" &&
                  shop::modeOfKey("sk_test_" + std::string(20, 'a')) == "test" && shop::modeOfKey("rk_test_" + std::string(20, 'b')) == "test" &&
                  shop::modeOfKey(kKey) == "test" && shop::modeOfKey("pk_test_" + std::string(20, 'a')).empty() && shop::modeOfKey("sk_live_a b").empty(),
              "le mode d'apres la cle : sk_live_/rk_live_ reel ; sk_test_/rk_test_ test ; pk_ et un texte : non");
        const std::string masked = shop::maskStripe("cle " + kKey + " et whsec_abc123 et sk_live_XyZ fin");
        check(!has(masked, kKey) && !has(masked, "whsec_abc123") && !has(masked, "sk_live_XyZ") && has(masked, "fin"), "le masque des secrets (" + masked + ")");
        shop::Settings s;
        s.active = true;
        s.articles.push_back({"", "edition", "PROFESSIONAL", "", 0, 49000, "Licence PROFESSIONAL - 1 an"});
        s.articles.push_back({"", "option", "", "ia_avancee", 30, 2900, "Option IA avanc\xC3\xA9" "e - 1 mois"});
        std::string why;
        check(shop::validSettings(s, &why), "un catalogue valable (" + why + ")");
        json::Value v;
        shop::Settings back;
        check(json::parse(shop::writeSettings(s), v, nullptr) && shop::parseSettings(v, back, nullptr) && back.articles.size() == 2 && back.find("ed:PROFESSIONAL") &&
                  back.find("opt:ia_avancee:30") && back.find("opt:ia_avancee:30")->amount == 2900 && back.active,
              "ecrit puis relu : les memes articles (ed:PROFESSIONAL, opt:ia_avancee:30)");
        shop::Settings bad = s;
        bad.articles[0].amount = 49;
        check(!shop::validSettings(bad, &why) && has(why, "0,50"), "49 centimes : refuse (" + why + ")");
        bad = s;
        bad.articles.push_back(s.articles[0]);
        check(!shop::validSettings(bad, &why) && has(why, "deux fois"), "un article deux fois : refuse");
        check(shop::stateLabel("payee") == "pay\xC3\xA9" "e, \xC3\xA0 livrer" && shop::stateLabel("a_revoir") == "\xC3\xA0 revoir", "les etats en francais");
        check(shop::dashboardUrl("test", "pi_123") == "https://dashboard.stripe.com/test/payments/pi_123" && shop::dashboardUrl("reel") == "https://dashboard.stripe.com/payments",
              "le tableau de bord de Stripe (test, reel)");
        check(shop::serviceKnowsShop("1.46.0") && shop::serviceKnowsShop("1.47.2") && !shop::serviceKnowsShop("1.44.0") && !shop::serviceKnowsShop(""), "la boutique : un service 1.46.0");
        // L'adresse de l'API de Stripe : jamais une autre que la vraie, sauf une adresse de ce poste.
        shop::setStripeApiBaseForTests("https://faux.exemple.invalid");
        check(shop::stripeApiBase() == "https://api.stripe.com", "une adresse d'essai hors de ce poste : ignoree");
        shop::setStripeApiBaseForTests("http://127.0.0.1:9");
        check(shop::stripeApiBase() == "http://127.0.0.1:9", "une adresse d'essai de ce poste : prise");
        shop::setStripeApiBaseForTests({});
    }

    std::printf("-- 1.46.1 : le montant suit le registre, la reduction, le montant libre (sans reseau)\n");
    {
        std::int64_t c = 0;
        check(shop::shownPriceTtc("50 \xE2\x82\xAC HT /par an", c) && c == 6000, "50 EUR HT : 60,00 EUR TTC");
        check(shop::shownPriceTtc("10 \xE2\x82\xAC", c, "50 \xE2\x82\xAC HT /par an") && c == 1200, "un prix reduit << 10 EUR >> sous un prix HT : HT aussi, 12,00 EUR TTC");
        check(shop::shownPriceTtc("10 \xE2\x82\xAC TTC", c, "50 \xE2\x82\xAC HT") && c == 1000 && shop::shownPriceTtc("10 \xE2\x82\xAC", c) && c == 1000,
              "<< 10 EUR TTC >> reste TTC ; << 10 EUR >> seul : TTC");
        const lic::Seconds now = lic::systemNow(), until = now + 40 * 86400;
        lic::Registry reg;
        lic::EditionDef ent;
        ent.id = "ENTERPRISE";
        ent.price = "50 \xE2\x82\xAC HT /par an";
        ent.promoPrice = "10 \xE2\x82\xAC";
        ent.promoUntil = until;
        lic::EditionDef per;
        per.id = "PERSONAL";
        per.price = "5 \xE2\x82\xAC HT / an";
        lic::EditionDef cus;
        cus.id = "CUSTOM";
        cus.price = "sur devis";
        lic::OptionDef ia;
        ia.id = "ia_avancee";
        ia.durations = {{30, "29 \xE2\x82\xAC HT"}};
        reg.editions = {ent, per, cus};
        reg.options = {ia};
        // un reglage d'avant la 1.46.1 (sans << libre >>), comme le service 1.46.0 le rend
        json::Value v;
        check(json::parse("{\"active\":true,\"devise\":\"eur\",\"articles\":["
                          "{\"id\":\"ed:ENTERPRISE\",\"genre\":\"edition\",\"edition\":\"ENTERPRISE\",\"montant\":6000,\"libelle\":\"Licence ENTERPRISE\"},"
                          "{\"id\":\"ed:PERSONAL\",\"genre\":\"edition\",\"edition\":\"PERSONAL\",\"montant\":600,\"libelle\":\"Licence PERSONAL\"},"
                          "{\"id\":\"opt:ia_avancee:30\",\"genre\":\"option\",\"option\":\"ia_avancee\",\"jours\":30,\"montant\":3000,\"libelle\":\"Option IA\"},"
                          "{\"id\":\"ed:CUSTOM\",\"genre\":\"edition\",\"edition\":\"CUSTOM\",\"montant\":5000,\"libre\":false,\"libelle\":\"Licence CUSTOM\"}]}",
                          v, nullptr),
              "un reglage d'avant (JSON)");
        shop::Settings s;
        check(shop::parseSettings(v, s, nullptr) && s.articles.size() == 4 && s.articles[0].legacy && s.articles[1].legacy && s.articles[2].legacy && !s.articles[3].legacy &&
                  !s.articles[3].free,
              "lu : trois articles d'avant (ni libres, ni suivis), un qui suit");
        check(shop::followRegistry(s, reg, now) > 0, "le registre suivi");
        const shop::Article& E = s.articles[0];
        check(!E.free && !E.legacy && E.amount == 6000 && E.reduced == 1200 && E.reducedUntil == until && E.amountAt(now) == 1200 && E.amountAt(until) == 6000,
              "ENTERPRISE (son montant etait le prix du registre) : suivi - 60,00 EUR, 12,00 EUR jusqu'a la fin de la reduction, puis 60,00");
        const std::string d = lic::isoDate(until);
        check(shop::articleAmountText(E, now) == "12,00\xC2\xA0\xE2\x82\xAC TTC (r\xC3\xA9" "duit jusqu'au " + d.substr(8, 2) + "/" + d.substr(5, 2) + ", puis 60,00\xC2\xA0\xE2\x82\xAC)" &&
                  shop::articleAmountText(E, until) == "60,00\xC2\xA0\xE2\x82\xAC TTC",
              "<< 12,00 EUR TTC (reduit jusqu'au JJ/MM, puis 60,00 EUR) >> (" + shop::articleAmountText(E, now) + ")");
        check(!s.articles[1].free && s.articles[1].amount == 600 && s.articles[1].reduced == 0, "PERSONAL : suivi, 6,00 EUR TTC");
        check(s.articles[2].free && s.articles[2].amount == 3000 && shop::articleAmountText(s.articles[2], now) == "30,00\xC2\xA0\xE2\x82\xAC TTC (montant libre)",
              "l'option (30,00 EUR, pas le prix du registre : 34,80) : un montant libre, garde");
        check(s.articles[3].free && s.articles[3].amount == 5000, "CUSTOM (<< sur devis >> : rien a suivre) : libre, son montant garde");
        check(shop::followRegistry(s, reg, now) == 0, "suivi de nouveau : rien ne change");
        std::string why;
        check(shop::validSettings(s, &why), "valable (" + why + ")");
        const std::string w = shop::writeSettings(s);
        check(has(w, "\"libre\":false") && has(w, "\"montant_reduit\":1200") && has(w, "\"fin_reduction\":" + std::to_string(until)) && has(w, "\"libre\":true"),
              "ecrit : libre ou suivi, la reduction et sa fin (" + w.substr(0, 160) + ")");
        shop::Settings old;
        old.articles.push_back({"", "edition", "PERSONAL", "", 0, 600, "Licence PERSONAL"});
        old.articles.back().legacy = true;
        check(!has(shop::writeSettings(old), "libre"), "un article d'avant (la console) part sans << libre >>");
        // un reglage d'avant au montant REDUIT : suivi tant que la reduction court, libre apres
        shop::Settings r1, r2;
        (void)json::parse("{\"articles\":[{\"id\":\"ed:ENTERPRISE\",\"genre\":\"edition\",\"edition\":\"ENTERPRISE\",\"montant\":1200,\"libelle\":\"x\"}]}", v, nullptr);
        (void)shop::parseSettings(v, r1, nullptr);
        r2 = r1;
        (void)shop::followRegistry(r1, reg, now);
        (void)shop::followRegistry(r2, reg, until + 10);
        check(!r1.articles[0].free && r1.articles[0].amount == 6000 && r2.articles[0].free && r2.articles[0].amount == 1200,
              "un reglage d'avant a 12,00 EUR : suivi pendant la reduction, libre apres (rien ne change pour lui)");
        // ce que le service 1.46.1 rend : le montant du moment, le produit Stripe, les produits
        (void)json::parse("{\"articles\":[{\"id\":\"ed:ENTERPRISE\",\"genre\":\"edition\",\"edition\":\"ENTERPRISE\",\"montant\":6000,\"libre\":false,\"montant_reduit\":1200,"
                          "\"fin_reduction\":" + std::to_string(until) + ",\"montant_actuel\":1200,\"stripe_produit\":\"xpg-ed-ENTERPRISE\",\"libelle\":\"x\"}],"
                          "\"stripe_catalogue\":{\"etat\":\"refus\",\"message\":\"Stripe refuse (cle " + kKey + ") : Products\",\"quand\":5,\"produits\":0}}",
                          v, nullptr);
        shop::Settings sv1;
        check(shop::parseSettings(v, sv1, nullptr) && sv1.articles[0].current == 1200 && sv1.articles[0].stripeProduct == "xpg-ed-ENTERPRISE" && sv1.articles[0].reduced == 1200 &&
                  sv1.catalog.state == "refus" && !sv1.catalog.ok() && !has(sv1.catalog.message, kKey) && has(sv1.catalog.message, "Products"),
              "lu du service : le montant du moment, le produit Stripe, l'etat des produits (sans secret)");
        check(std::string(shop::kStripeVersion) == "2026-09-30.endive", "la version de l'API de Stripe : 2026-09-30.endive");
        // l'Admin 1.46.1 (son kit : le service 1.46.1) devant un service 1.46.0 : << la 1.46.1 est prete dans l'Admin : deploie-la >>
        const auto vc = dep::checkVersion("1.46.0", "1.46.1");
        check(vc.outdated && vc.kitNewer && !vc.belowRequired && vc.target == "1.46.1" && has(dep::outdatedMessage(vc), "la 1.46.1 est pr\xC3\xAAte dans l'Admin"),
              "un service 1.46.0, le kit 1.46.1 : a mettre a jour (" + dep::outdatedMessage(vc) + ")");
    }

    gui::Workspace plus(U(root / "plus"));
    {
        const auto c = plus.createKey("essai boutique", true);
        check(c.ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+");
        const auto r = plus.signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json");
        check(r.ok && plus.hasRegistry(), "le registre d'exemple signe");
        check(!plus.whyNotDeliver("edition").empty(), "sans service : ne livre pas (" + plus.whyNotDeliver("edition") + ")");
    }

    // ======================================================================== le service local et le faux Stripe
    FakeStripe stripe(source, kKey);
    if (!stripe.ready()) {
        std::printf("SAUTE : le faux Stripe ne demarre pas (Node.js manque ?)\n");
        return finish();
    }
    xpg_test::LocalService::Options so;
    so.env = {{"XPG_STRIPE_SECRET_KEY", kKey}, {"XPG_STRIPE_WEBHOOK_SECRET", kHook}, {"XPG_STRIPE_API_BASE", stripe.base()}};
    xpg_test::LocalService svc({plus.signingKey()->publicKeyHex()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    if (!svc.ready()) {
        std::printf("ECHEC : le service local : %s\n", svc.error().c_str());
        return 1;
    }
    std::printf("   service local : %s ; faux Stripe : %s\n", svc.url().c_str(), stripe.base().c_str());

    std::printf("-- l'admin+ : le service, la boutique\n");
    {
        check(plus.serviceConfigure(svc.url()).ok, "l'adresse du service");
        check(plus.serviceCertify({}).ok, "la cle du service certifiee");
        auto r = plus.signRegistry(plus.registry());
        auto job = plus.takeServiceJob();
        check(r.ok && job && plus.serviceRun(*job).ok, "le registre depose");
        on::ServiceState st;
        // 1.47.0 : le service de ce depot (les statistiques) ; la boutique demande 1.46.0 ou plus
        check(plus.serviceVerify(&st).ok && shop::serviceKnowsShop(st.version), "le service sait la boutique (" + st.version + ")");
        plus.adoptServiceState(st);
        check(plus.serviceKnowsShop() && plus.whyNotDeliver("edition").empty() && plus.whyNotDeliver("option").empty(), "l'admin+ livre les offres et les options");
        shop::Settings s;
        auto g = plus.shopSettings(s);
        check(g.ok && s.stripe.connected && s.stripe.webhook && s.stripe.mode == "test" && !s.active && !s.open, "Stripe branche (mode test), la vente pas encore ouverte (" + g.text() + ")");
        s.active = true;
        s.autoDeliver = true;
        s.articles = {{"", "edition", "PROFESSIONAL", "", 0, 49000, "Licence PROFESSIONAL - 1 an"}, {"", "option", "", "ia_avancee", 30, 2900, "Option IA avanc\xC3\xA9" "e - 1 mois"}};
        const auto w = plus.saveShop(s);
        check(w.ok && has(w.message, "n\xC2\xB0 1"), "la boutique enregistree (" + w.text() + ")");
        // 1.46.1 : les produits chez Stripe, crees par le service
        check(has(w.text(), "Stripe : 2 produit(s) \xC3\xA0 jour chez Stripe"), "les 2 produits chez Stripe (" + w.text() + ")");
        const Reply pr = call(stripe.base(), "GET", "/__essai/produits", {}, nullptr);
        check(has(pr.body, "\"xpg-ed-PROFESSIONAL\"") && has(pr.body, "\"xpg-opt-ia_avancee-30\"") && has(pr.body, "\"unit_amount\":49000"),
              "le faux Stripe : les produits xpg-ed-PROFESSIONAL, xpg-opt-ia_avancee-30 et leurs prix");
        shop::Settings back;
        check(plus.shopSettings(back).ok && back.open && back.articles.size() == 2 && back.number == 1, "relue : ouverte, 2 articles");
        const Reply pub = call(svc.url(), "GET", "/v1/boutique", {}, nullptr);
        check(pub.status == 200 && pub.value.flag("active") && !has(pub.body, kKey), "GET /v1/boutique : ouverte, sans secret");
    }

    std::printf("-- une commande payee, livree seule par l'Admin\n");
    Pc pc1(root / "pc-1", "PC d'essai 1");
    std::string licence1, nonce1;
    int n1 = 0;
    {
        const Reply o = call(svc.url(), "POST", "/v1/boutique/commande", pc1.order(*plus.signingKey(), "ed:PROFESSIONAL", "Client d'essai", {}, &nonce1), pc1.key.get());
        n1 = static_cast<int>(o.value.integer("numero"));
        check(o.status == 201 && n1 > 0 && has(o.value.str("url"), "checkout.stripe.invalid"), "le PC commande : la page de Stripe (" + o.body.substr(0, 120) + ")");
        // 1.46.1 : la page de paiement de l'API d'aujourd'hui (le faux Stripe refuserait payment_method_types), le prix du produit
        {
            std::string keys, version, price;
            if (const json::Value* a = stripe.requests().find("requetes"); a && a->isArray())
                for (const auto& x : a->items()) {
                    if (x.str("chemin") != "/v1/checkout/sessions" || x.str("methode") != "POST") continue;
                    keys.clear();
                    version = x.str("version");
                    if (const json::Value* p = x.find("params"); p && p->isArray())
                        for (const auto& kv : p->items()) {
                            if (!kv.isArray() || kv.items().size() != 2) continue;
                            keys += kv.items()[0].asString() + "=" + kv.items()[1].asString() + ";";
                            if (kv.items()[0].asString() == "line_items[0][price]") price = kv.items()[1].asString();
                        }
                }
            check(version == shop::kStripeVersion && has(keys, "allowed_payment_method_types[0]=card;") && has(keys, "wallet_options[link][display]=never;") &&
                      !has(keys, ";payment_method_types") && !has(keys, "customer=") && !has(keys, "customer_creation") && !has(keys, "setup_future_usage") &&
                      !has(keys, "saved_payment_method_options") && !price.empty(),
                  "la page : Stripe-Version, la carte seule (allowed_payment_method_types), Link cache, le prix du produit, jamais de carte gardee (" + keys.substr(0, 200) + ")");
        }
        on::AdminWatch w;
        check(plus.serviceWatch(w).ok && w.orders >= 1 && w.toDeliver == 0, "le regard : le compteur des commandes, rien a livrer");
        gui::DeliveryReport rep;
        auto d = plus.deliverOrders(0, true, &rep);
        check(d.ok && rep.delivered == 0 && rep.lines.empty(), "rien de paye : rien a livrer (" + d.text() + ")");
        const Reply e = stripe.event(svc.url(), kHook, "checkout.session.completed", stripe.sessionOf(n1));
        check(e.status == 200 && e.value.integer("statut") == 200, "Stripe : paye (le webhook signe)");
        check(plus.serviceWatch(w).ok && w.toDeliver == 1, "le regard : 1 commande a livrer");
        d = plus.deliverOrders(0, true, &rep);
        plus.reload();
        check(d.ok && rep.delivered == 1 && rep.lines.size() == 1 && rep.lines[0].delivered, "l'Admin livre seul (" + d.text() + ")");
        licence1 = rep.lines.empty() ? std::string() : rep.lines[0].licence;
        const gui::LicenceEntry* L = plus.licence(licence1);
        check(L && gui::Workspace::isOnlineLicence(L->licence) && L->licence.customer == "Client d'essai" && L->licence.edition == "PROFESSIONAL" &&
                  L->licence.maxDevices == 5 && L->licence.expiresAt > lic::systemNow() + 360 * 86400,
              "la licence en ligne de l'offre : le client = la societe, 5 PC, un an (" + licence1 + ")");
        check(on::hasCode(U(root / "plus"), licence1), "son code garde chiffre sur ce poste");
        gw::DeviceStatus st;
        gw::StatusContent content;
        check(readPosteState(svc.url(), pc1, *plus.signingKey(), st, content) && st.state == gw::State::Accepted && st.requestNonce == nonce1 &&
                  content.onlineLicense == licence1 && !content.code.empty(),
              "le PC lit << acceptee >> : le code scelle pour lui, la licence, sa demande (le nonce)");
        // le PC s'active avec ce code, comme son Studio
        json::Value act = json::Value::object();
        act.set("code", content.code);
        act.set("demande", pc1.request());
        act.set("nom", pc1.name);
        const Reply a = call(svc.url(), "POST", "/v1/activer", json::write(act, false), pc1.key.get());
        check(a.status == 200 && has(a.value.str("licence"), licence1), "le PC s'active avec le code livre (" + std::to_string(a.status) + ")");
        shop::OrdersView v;
        check(plus.shopOrders(v).ok && orderOf(v, n1) && orderOf(v, n1)->state == "livree" && orderOf(v, n1)->licence == licence1 && v.toDeliver == 0,
              "la commande est << livree >>, avec l'identifiant de la licence");
        check(!fs::exists(root / "plus" / shop::kDeliveriesFile) || !has(readFile(root / "plus" / shop::kDeliveriesFile), "\"" + std::to_string(n1) + "\""),
              "la livraison n'est plus << commencee >>");
        // une seconde livraison : refusee
        d = plus.deliverOrders(n1, false, &rep);
        check(!d.ok && has(d.message, "pas \xC3\xA0 livrer"), "la livrer de nouveau : refuse (" + d.message + ")");
        int added = 0;
        (void)plus.fetchJournal(&added);
        const auto view = plus.journalLines();
        bool line = false;
        for (const auto& row : view.rows)
            if (row.line.parsed.action == "commande_livrer" && has(row.line.parsed.summary, "commande n\xC2\xB0 " + std::to_string(n1) + " livr\xC3\xA9" "e")) line = true;
        check(line, "le journal des admins : << commande n° N livree >>");
    }

    std::printf("-- une option pour la licence du client\n");
    {
        const Reply o = call(svc.url(), "POST", "/v1/boutique/commande", pc1.order(*plus.signingKey(), "opt:ia_avancee:30", "Client d'essai", licence1), pc1.key.get());
        const int n = static_cast<int>(o.value.integer("numero"));
        check(o.status == 201, "le PC commande l'option (" + o.body.substr(0, 100) + ")");
        check(stripe.event(svc.url(), kHook, "checkout.session.completed", stripe.sessionOf(n)).value.integer("statut") == 200, "Stripe : paye");
        gui::DeliveryReport rep;
        const auto d = plus.deliverOrders(0, true, &rep);
        plus.reload();
        check(d.ok && rep.delivered == 1, "livree seule (" + d.text() + ")");
        const gui::LicenceEntry* L = plus.licence(licence1);
        bool has30 = false;
        if (L)
            for (const auto& x : L->licence.options)
                if (x.id == "ia_avancee" && x.until > x.from && x.until - x.from <= 31 * 86400 && has(x.price, "29,00")) has30 = true;
        check(has30, "la licence emise de nouveau avec l'option (1 mois, le montant paye)");
        gw::DeviceStatus st;
        gw::StatusContent content;
        check(readPosteState(svc.url(), pc1, *plus.signingKey(), st, content) && st.state == gw::State::Accepted && content.onlineLicense == licence1 && content.code.empty(),
              "le PC lit << acceptee >> (sa licence, sans code : il renouvelle)");
        json::Value body = json::Value::object();
        body.set("station", pc1.station());
        const Reply rn = call(svc.url(), "POST", "/v1/renouveler", json::write(body, false), pc1.key.get());
        check(rn.status == 200 && has(rn.body, "ia_avancee"), "le renouvellement porte la licence avec l'option");
    }

    std::printf("-- a revoir, << Livrer seul >> decoche, rembourser\n");
    Pc pc2(root / "pc-2", "PC d'essai 2");
    {
        Reply o = call(svc.url(), "POST", "/v1/boutique/commande", pc2.order(*plus.signingKey(), "opt:ia_avancee:30", "Atelier d'essai", "XPG-INCONNUE-1"), pc2.key.get());
        int n = static_cast<int>(o.value.integer("numero"));
        (void)stripe.event(svc.url(), kHook, "checkout.session.completed", stripe.sessionOf(n));
        gui::DeliveryReport rep;
        auto d = plus.deliverOrders(0, true, &rep);
        check(!d.ok && rep.review == 1 && rep.lines.size() == 1 && rep.lines[0].review && has(rep.lines[0].problem, "XPG-INCONNUE-1"),
              "une licence inconnue : a revoir, la raison dite (" + d.text() + ")");
        shop::OrdersView v;
        check(plus.shopOrders(v).ok && orderOf(v, n) && orderOf(v, n)->state == "a_revoir" && has(orderOf(v, n)->error, "XPG-INCONNUE-1"), "sur le service : a_revoir");
        {   // 1.46.0 : la raison, une phrase (une majuscule en tete, un point a la fin)
            const std::string& p = rep.lines.empty() ? std::string() : rep.lines[0].problem;
            check(!p.empty() && p[0] >= 'A' && p[0] <= 'Z' && p.back() == '.' && orderOf(v, n) && orderOf(v, n)->error == p,
                  "la raison a revoir : une majuscule, un point, la meme sur le service (" + p + ")");
        }
        check(plus.reviewOrder(n, "vu avec le client : rembourser").ok, "Revoir... : la raison de l'editeur");
        {
            shop::OrdersView v2;
            check(plus.shopOrders(v2).ok && orderOf(v2, n) && orderOf(v2, n)->error == "Vu avec le client : rembourser.",
                  "la raison de l'editeur : une majuscule, un point (" + (orderOf(v2, n) ? orderOf(v2, n)->error : std::string()) + ")");
        }
        // << Livrer seul >> decoche : la commande payee attend
        shop::Settings s;
        (void)plus.shopSettings(s);
        s.autoDeliver = false;
        check(plus.saveShop(s).ok, "<< Livrer seul >> decoche");
        o = call(svc.url(), "POST", "/v1/boutique/commande", pc2.order(*plus.signingKey(), "ed:PROFESSIONAL", "Atelier d'essai", {}), pc2.key.get());
        const int n2 = static_cast<int>(o.value.integer("numero"));
        (void)stripe.event(svc.url(), kHook, "checkout.session.completed", stripe.sessionOf(n2));
        d = plus.deliverOrders(0, true, &rep);
        check(d.ok && rep.autoOff && rep.delivered == 0, "automatique : rien n'est livre, c'est dit (" + d.message + ")");
        check(plus.shopOrders(v).ok && orderOf(v, n2) && orderOf(v, n2)->state == "payee", "elle attend, payee");
        d = plus.deliverOrders(n2, false, &rep);
        check(d.ok && rep.delivered == 1, "Delivrer maintenant : livree (" + d.text() + ")");
        // rembourser (l'admin+)
        const auto rf = plus.refundOrder(n1);
        check(rf.ok && has(rf.message, "Stripe"), "Rembourser : demande a Stripe (" + rf.text() + ")");
        const json::Value q = stripe.requests();
        bool refund = false;
        if (const json::Value* a = q.find("requetes"); a && a->isArray())
            for (const auto& x : a->items())
                if (x.str("chemin") == "/v1/refunds" && x.str("auth") == "bonne") refund = true;
        check(refund, "le faux Stripe a recu le remboursement (la cle du service)");
        (void)stripe.event(svc.url(), kHook, "charge.refunded", stripe.sessionOf(n1));
        check(plus.shopOrders(v).ok && orderOf(v, n1) && orderOf(v, n1)->state == "remboursee" && plus.licence(licence1), "remboursee ; la licence reste (pas revoquee d'office)");
    }

    std::printf("-- 1.46.1 : la boutique suit le registre (une reduction), les produits chez Stripe\n");
    {
        const lic::Seconds until = lic::systemNow() + 30 * 86400;
        lic::Registry reg = plus.registry();
        for (auto& e : reg.editions)
            if (e.id == "ENTERPRISE") {
                e.price = "50 \xE2\x82\xAC HT /par an";
                e.promoPrice = "10 \xE2\x82\xAC";
                e.promoUntil = until;
            }
        auto r = plus.signRegistry(reg);
        auto job = plus.takeServiceJob();
        check(r.ok && job && plus.serviceRun(*job).ok, "le registre : ENTERPRISE 50 EUR HT, reduite a 10 EUR (depose)");
        shop::Settings s;
        check(plus.shopSettings(s).ok, "la boutique relue");
        shop::Article a{"", "edition", "ENTERPRISE", "", 0, 100, "Licence ENTERPRISE - 1 an"};
        a.free = false;   // suit le registre (le catalogue de l'Admin le fait ainsi)
        s.articles.push_back(a);
        const auto w = plus.saveShop(s);
        check(w.ok && has(w.text(), "produit(s) \xC3\xA0 jour chez Stripe"), "enregistree : ENTERPRISE suit le registre (" + w.text() + ")");
        Reply pub = call(svc.url(), "GET", "/v1/boutique", {}, nullptr);
        const auto amountOf = [](const Reply& p, const std::string& id, const char* field) -> long long {
            if (const json::Value* a = p.value.find("articles"); a && a->isArray())
                for (const auto& x : a->items())
                    if (x.str("id") == id) return x.integer(field, -1);
            return -1;
        };
        check(amountOf(pub, "ed:ENTERPRISE", "montant") == 1200 && amountOf(pub, "ed:ENTERPRISE", "montant_base") == 6000 && amountOf(pub, "ed:ENTERPRISE", "fin_reduction") == until,
              "les Studios lisent 12,00 EUR (60,00 de base, la fin) : << Acheter - 12,00 EUR TTC >>");
        const Reply pr = call(stripe.base(), "GET", "/__essai/produits", {}, nullptr);
        check(has(pr.body, "\"xpg-ed-ENTERPRISE\"") && has(pr.body, "\"unit_amount\":6000") && has(pr.body, "\"unit_amount\":1200"),
              "chez Stripe : le produit ENTERPRISE, son prix (60,00) et son prix reduit (12,00)");
        // une autre reduction dans le registre : la boutique la suit au depot (l'admin+), sans Enregistrer
        reg = plus.registry();
        for (auto& e : reg.editions)
            if (e.id == "ENTERPRISE") e.promoPrice = "8 \xE2\x82\xAC";
        r = plus.signRegistry(reg);
        job = plus.takeServiceJob();
        const auto run = job ? plus.serviceRun(*job) : gui::OpResult::fail("pas de travail");
        check(r.ok && run.ok && has(run.text(), "La boutique suit le registre"), "le registre depose : la boutique suit (" + run.text().substr(0, 200) + ")");
        pub = call(svc.url(), "GET", "/v1/boutique", {}, nullptr);
        check(amountOf(pub, "ed:ENTERPRISE", "montant") == 960, "les Studios lisent 9,60 EUR (8 EUR HT) sans rien faire de plus");
    }

    std::printf("-- Brancher Stripe (un faux kit, un faux wrangler)\n");
    {
        const fs::path kit = root / "kit";
        writeFile(kit / "deployer.mjs", "// faux kit (essai)\n");
        writeFile(kit / "cle-publique.txt", "# essai\n" + plus.signingKey()->publicKeyHex() + "\n");
        writeFile(kit / "service" / "wrangler.toml", "name = \"essai\"\n");
        writeFile(kit / "service" / "src" / "licences.js", "const VERSION = \"1.46.0\";\n");
        // Le faux wrangler : << secret put NOM >>, la valeur lue sur l'entree standard ; il garde NOM et son EMPREINTE.
        writeFile(kit / "service" / "node_modules" / "wrangler" / "bin" / "wrangler.js",
                  "const fs = require('fs'); const crypto = require('crypto');\n"
                  "const [cmd, sub, name] = process.argv.slice(2);\n"
                  "if (process.env.ESSAI_WRANGLER_PAS_CONNECTE) { console.error('You are not authenticated. Please run `wrangler login`.'); process.exit(1); }\n"
                  "let v = ''; process.stdin.on('data', d => v += d); process.stdin.on('end', () => {\n"
                  "  fs.appendFileSync(__dirname + '/../../../../secrets-recus.txt', cmd + ' ' + sub + ' ' + name + ' ' + crypto.createHash('sha256').update(v).digest('hex') + '\\n');\n"
                  "  console.log('Success! Uploaded secret ' + name + ' (le faux wrangler)');\n"
                  "});\n");
        dep::TestHooks h;
        h.kit = U(kit);
        dep::setTestHooks(h);
        shop::setStripeApiBaseForTests(stripe.base());
        std::vector<std::string> lines;
        std::string key = kKey;
        auto r = plus.connectStripe(key, {}, [&lines](const std::string& l) { lines.push_back(l); });
        check(r.ok && key.empty(), "branche : la cle effacee apres (" + r.text() + ")");
        const std::string got = readFile(kit / "secrets-recus.txt");
        const auto sha = [](const std::string& s) { return deploy::crypto::sha256Hex(s); };
        check(has(got, "secret put STRIPE_SECRET_KEY " + sha(kKey)) && has(got, "secret put STRIPE_WEBHOOK_SECRET "), "wrangler secret put : les deux secrets, la valeur sur l'entree standard");
        check(!has(got, kKey), "le faux wrangler n'a vu la cle que sur son entree (rien sur la ligne de commande)");
        const json::Value q = stripe.requests();
        bool created = false;
        if (const json::Value* a = q.find("requetes"); a && a->isArray())
            for (const auto& x : a->items()) {
                if (x.str("chemin") != "/v1/webhook_endpoints" || x.str("methode") != "POST") continue;
                std::string url, events;
                if (const json::Value* p = x.find("params"); p && p->isArray())
                    for (const auto& kv : p->items()) {
                        if (!kv.isArray() || kv.items().size() != 2) continue;
                        if (kv.items()[0].asString() == "url") url = kv.items()[1].asString();
                        if (kv.items()[0].asString().rfind("enabled_events", 0) == 0) events += kv.items()[1].asString() + ";";
                    }
                created = url == svc.url() + "/v1/boutique/stripe" && x.str("auth") == "bonne" && has(events, "checkout.session.completed") && has(events, "charge.refunded") &&
                          has(events, "checkout.session.expired") && has(events, "checkout.session.async_payment_failed") && has(events, "checkout.session.async_payment_succeeded");
            }
        check(created, "le point d'arrivee du webhook cree chez Stripe : l'adresse du service, les 5 evenements");
        // 1.46.1 : la version de l'API fixee (l'en-tete), et celle des evenements (api_version)
        bool pinned = true, apiVersion = false;
        if (const json::Value* a = q.find("requetes"); a && a->isArray())
            for (const auto& x : a->items()) {
                if (x.str("chemin").rfind("/v1/webhook_endpoints", 0) != 0) continue;
                if (x.str("version") != shop::kStripeVersion) pinned = false;
                if (const json::Value* p = x.find("params"); p && p->isArray())
                    for (const auto& kv : p->items())
                        if (kv.isArray() && kv.items().size() == 2 && kv.items()[0].asString() == "api_version" && kv.items()[1].asString() == shop::kStripeVersion) apiVersion = true;
            }
        check(pinned && apiVersion, "Stripe-Version sur chaque appel de l'Admin ; le webhook cree en api_version=2026-09-30.endive");
        check(has(r.text(), "produit(s) \xC3\xA0 jour chez Stripe"), "apres Brancher : le service met les produits a jour chez Stripe (" + r.text().substr(0, 300) + ")");
        std::string all = r.text();
        for (const auto& l : lines) all += l + "\n";
        for (const auto& d : r.details) all += d + "\n";
        check(!has(all, kKey) && !has(all, "whsec_") && !has(all, "essai_jeton_"), "rien de secret dans ce qui est dit");
        // de nouveau : l'ancien point d'arrivee efface
        key = kKey;
        r = plus.connectStripe(key);
        check(r.ok && has(r.text(), "Ancien point d'arriv\xC3\xA9" "e effac\xC3\xA9"), "rebrancher : l'ancien point d'arrivee efface (pas de double webhook)");
        // une cle publiable, une cle illisible : refusees, rien n'est fait
        key = "pk_test_" + std::string(24, 'a');
        r = plus.connectStripe(key);
        check(!r.ok && has(r.message, "PUBLIABLE") && key.empty(), "une cle publiable : refusee, effacee");
        key = "pas une cle";
        r = plus.connectStripe(key);
        check(!r.ok && has(r.message, "pas une cl\xC3\xA9 secr\xC3\xA8te"), "une cle illisible : refusee");
        // pas connecte a Cloudflare : la meme phrase que Deployer
        ::setenv("ESSAI_WRANGLER_PAS_CONNECTE", "1", 1);
        key = kKey;
        r = plus.connectStripe(key);
        ::unsetenv("ESSAI_WRANGLER_PAS_CONNECTE");
        check(!r.ok && has(r.text(), "Cloudflare") && has(r.text(), "Se connecter"), "pas connecte a Cloudflare : dit, avec l'etape suivante (" + r.message + ")");
        // un admin (pas l'admin+) : refuse
        // la console : stripe-brancher lit la cle dans une variable d'environnement, jamais en argument
        if (!console.empty()) {
            ::setenv("XPG_STRIPE_API_ESSAI", stripe.base().c_str(), 1);
            ::setenv("XPG_ESSAI_CLE_STRIPE", kKey.c_str(), 1);
            int code = -1;
            const std::string out = runConsole(console, "stripe-brancher --cle-variable XPG_ESSAI_CLE_STRIPE --dossier-kit \"" + U(kit) + "\" --dossier-cle \"" + U(root / "plus") + "\"", &code);
            check(code == 0 && has(out, "Stripe est branch\xC3\xA9") && !has(out, kKey), "la console : stripe-brancher --cle-variable (" + out.substr(0, 300) + ")");
            ::unsetenv("XPG_ESSAI_CLE_STRIPE");
            const std::string e = runConsole(console, "boutique-etat --dossier-cle \"" + U(root / "plus") + "\"", &code);
            check(code == 0 && has(e, "Stripe en mode test") && has(e, "ed:PROFESSIONAL") && has(e, "490,00"), "la console : boutique-etat (" + e.substr(0, 200) + ")");
            const std::string l = runConsole(console, "commandes --etat livree --dossier-cle \"" + U(root / "plus") + "\"", &code);
            check(code == 0 && has(l, "livr\xC3\xA9" "e") && has(l, "Atelier d'essai"), "la console : commandes --etat livree");
            ::unsetenv("XPG_STRIPE_API_ESSAI");
        }
        dep::setTestHooks({});
        shop::setStripeApiBaseForTests({});
    }
    return finish();
}
