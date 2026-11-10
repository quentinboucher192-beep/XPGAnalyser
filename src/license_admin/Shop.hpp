// =============================================================================
//  license_admin/Shop.hpp - 1.46.0 : LA BOUTIQUE ET LES COMMANDES, COTE EDITEUR
//  (l'outil de l'EDITEUR seulement ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Le contrat : docs/BOUTIQUE.md. Le service des licences (1.46.0) garde le
//  catalogue de la boutique (les montants encaisses, en centimes TTC) et les
//  commandes ; Stripe encaisse (sa page de paiement hebergee : la carte n'est
//  jamais vue ici, jamais gardee) ; l'Admin de l'editeur LIVRE les commandes
//  payees (gui/WorkspaceShop.cpp : la licence signee ici, comme une demande
//  acceptee).
//
//  CE QUI EST ICI (sans ecran) :
//    - le catalogue (Settings, Article) et les commandes (Order) : lire le JSON
//      du service, ecrire celui de PUT /admin/boutique, les verifier ;
//    - les appels au service (BLOQUANTS, signes par la cle qui signe : l'admin+
//      ou un admin) : GET/PUT /admin/boutique, GET /admin/commandes, prendre,
//      livree, a-revoir, rembourser ;
//    - BRANCHER STRIPE (connectStripe) : la cle secrete collee par l'editeur
//      (jamais ecrite sur le disque, jamais dans un journal, effacee apres) ; le
//      point d'arrivee du webhook cree chez Stripe (POST /v1/webhook_endpoints,
//      l'adresse du service + /v1/boutique/stripe, les 5 evenements), son secret
//      lu ; les deux poses comme SECRETS DU WORKER par le kit du service (wrangler
//      secret put, la valeur sur l'entree standard : deployer::putSecret).
//    - les textes : le montant (<< 490,00 EUR >>), l'etat d'une commande, le mode.
//
//  LES ESSAIS n'appellent jamais le vrai Stripe : l'adresse de son API se
//  remplace (setStripeApiBaseForTests, ou la variable XPG_STRIPE_API_ESSAI)
//  SEULEMENT par une adresse de ce poste (http://127.0.0.1:PORT ou localhost) ;
//  les valeurs d'essai commencent par essai_jeton_.
//
//  1.46.1 : LE MONTANT SUIT LE REGISTRE. Par defaut, le montant d'un article est
//  le prix affiche du registre (HT + 20 % = TTC) et, tant qu'une reduction court
//  (promo_price jusqu'a promo_until), le montant reduit : l'Admin envoie les deux
//  et la fin (followRegistry), le service choisit a chaque commande. Un montant
//  LIBRE (tape par l'editeur) ne suit rien. Les produits et les prix chez Stripe :
//  crees par le SERVICE (il a la cle) a l'enregistrement et apres Brancher
//  Stripe ; l'Admin dit ce qu'il en est (CatalogState). L'API de Stripe :
//  kStripeVersion sur chaque appel et sur le point d'arrivee du webhook.
// =============================================================================
#pragma once

#include "ServiceClient.hpp"
#include "ServiceDeploy.hpp"

#include "../core/Json.hpp"
#include "../licensing/Documents.hpp"
#include "../licensing/Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::shop {

using licensing::Seconds;

// Le service qu'il faut pour la boutique (ses routes) : un service plus ancien marche pour tout le reste.
inline constexpr const char* kServiceRequired = "1.46.0";
inline constexpr std::int64_t kMinAmount = 50;           // 0,50 EUR
inline constexpr std::int64_t kMaxAmount = 100000000;    // 1 000 000 EUR
inline constexpr std::size_t  kMaxArticles = 200;
inline constexpr const char*  kDeliveriesFile = "boutique-livraisons.json";   // les livraisons commencees (reprises)
// 1.46.1 : la version de l'API de Stripe (l'en-tete Stripe-Version ; api_version du point d'arrivee du webhook) - la meme
// que celle du service (VERSION_STRIPE, service/src/boutique.js). Depuis celle-ci, Stripe refuse payment_method_types.
inline constexpr const char*  kStripeVersion = "2026-09-30.endive";
// Les evenements de Stripe que le webhook recoit (docs/BOUTIQUE.md, par. 2).
[[nodiscard]] const std::vector<std::string>& stripeEvents();

// ---- le catalogue ----
struct Article {
    std::string  id;          // "ed:PROFESSIONAL", "opt:ai_advanced:365"
    std::string  kind;        // "edition", "option"
    std::string  edition;     // une offre du registre
    std::string  option;      // une option du registre
    int          days{0};     // la duree de l'option (0 : jusqu'a la fin de la licence)
    std::int64_t amount{0};   // en centimes, TTC (1.46.1 : le montant de base)
    std::string  label;       // le nom sur la page de paiement et la facture
    // 1.46.1 : le montant suit le registre (faux) ou il est LIBRE (tape par l'editeur) ; une reduction : le montant reduit
    // jusqu'a sa fin (comprise jusqu'a la seconde d'avant : comme le registre, now < reducedUntil).
    bool         free{false};
    std::int64_t reduced{0};        // 0 : pas de reduction
    Seconds      reducedUntil{0};
    bool         legacy{false};     // lu d'un reglage d'avant la 1.46.1 (ni libre, ni suivi) : followRegistry decide
    std::int64_t current{0};        // le montant du moment selon le service (montant_actuel) ; 0 : pas dit
    std::string  stripeProduct;     // le produit chez Stripe (cree par le service) ; vide : pas (encore)
    [[nodiscard]] bool reducedAt(Seconds now) const noexcept { return reduced > 0 && reducedUntil > 0 && now < reducedUntil; }
    [[nodiscard]] std::int64_t amountAt(Seconds now) const noexcept { return reducedAt(now) ? reduced : amount; }
};
[[nodiscard]] std::string articleId(const std::string& kind, const std::string& editionOrOption, int days = 0);

// 1.46.1 : les produits chez Stripe, d'apres le service (la derniere mise a jour).
struct CatalogState {
    std::string state;      // ok, partiel, refus, panne, pas_branche, en_cours ; vide : jamais faite
    std::string message;    // ce que le service en dit (sans secret)
    Seconds     when{0};
    int         products{0};
    [[nodiscard]] bool ok() const noexcept { return state == "ok"; }
};
bool parseCatalogState(const core::json::Value& v, CatalogState& out);

struct StripeState {
    bool        connected{false};   // la cle secrete est posee sur le Worker
    std::string mode;               // "test", "reel" ; vide : pas branche
    bool        webhook{false};     // le secret du webhook est pose
    Seconds     lastEvent{0};       // le dernier evenement recu (0 : aucun)
};

struct Settings {
    bool                 active{false};
    bool                 autoDeliver{true};
    std::string          currency{"eur"};
    std::vector<Article> articles;
    std::int64_t         number{0};
    StripeState          stripe;      // lu (GET /admin/boutique), jamais ecrit
    bool                 open{false}; // le service dit la boutique ouverte
    CatalogState         catalog;     // 1.46.1 : les produits chez Stripe (lu, jamais ecrit)
    [[nodiscard]] const Article* find(const std::string& id) const;
};
bool parseSettings(const core::json::Value& v, Settings& out, std::string* why);
// Le corps de PUT /admin/boutique.
[[nodiscard]] std::string writeSettings(const Settings& s);
// Les regles du service (montants, libelles, identifiants, doublons) ; faux : `why`.
bool validSettings(const Settings& s, std::string* why);

// ---- 1.46.1 : le montant suit le registre ----
// Le prix affiche (un texte du registre : << 490 EUR HT / an >>) en centimes TTC (HT : + 20 %) ; faux : pas de nombre.
// `base` : le prix normal dont un prix reduit qui ne dit ni << HT >> ni << TTC >> (<< 10 EUR >>) herite (<< 50 EUR HT >> :
// le reduit est HT aussi).
bool shownPriceTtc(const std::string& price, std::int64_t& cents, const std::string& base = {});
// Ce que le registre dit d'un article : le montant de base, et la reduction (le prix reduit et sa fin) d'une offre.
struct RegistryAmounts {
    bool         ok{false};      // le prix affiche se lit (sinon : rien a suivre)
    std::int64_t amount{0};
    std::int64_t reduced{0};     // 0 : pas de reduction
    Seconds      until{0};
    std::string  price, promo;   // les textes du registre
};
[[nodiscard]] RegistryAmounts registryAmounts(const licensing::Registry& reg, const Article& a);
// Les articles qui suivent le registre prennent ses montants (de base, reduit, la fin) ; un article d'avant la 1.46.1
// (legacy) le suit si son montant est celui du registre (de base, ou reduit en cours a `now`), sinon il devient libre
// (resolveLegacy faux : il reste tel quel). Rend le nombre d'articles changes.
int followRegistry(Settings& s, const licensing::Registry& reg, Seconds now, bool resolveLegacy = true);
// << 12,00 EUR TTC (reduit jusqu'au 22/12, puis 60,00 EUR) >>, << 60,00 EUR TTC >>, << 6,00 EUR TTC (montant libre) >>.
[[nodiscard]] std::string articleAmountText(const Article& a, Seconds now, const std::string& currency = "eur");

// ---- les commandes ----
struct Order {
    int          number{0};
    std::string  device;        // l'identifiant du poste (32 hexa)
    std::string  deviceKey;     // la cle publique du PC (64 hexa)
    std::string  article, kind, edition, option;
    int          days{0};
    std::int64_t amount{0};
    std::string  currency, label, company, name, email, existingLicence;
    std::string  keyId;         // la cle pour qui le scelle rendu est fait
    std::string  sealed;        // la demande du PC, scellee pour cette cle ; vide : pas pour ce poste
    bool         forOther{false};
    std::string  state;         // a_payer, payee, livree, expiree, annulee, echec_paiement, remboursee, a_revoir
    std::string  mode;          // test, reel
    Seconds      created{0}, updated{0}, expires{0}, paid{0}, takenAt{0};
    std::string  stripeSession, stripePayment, stripeRefund, receiptUrl, licence, error;
    [[nodiscard]] bool toDeliver() const noexcept { return state == "payee"; }
    [[nodiscard]] bool deliverable() const noexcept { return state == "payee" || state == "a_revoir"; }
    [[nodiscard]] bool refundable() const noexcept { return (state == "payee" || state == "livree" || state == "a_revoir") && !stripePayment.empty(); }
};
struct OrdersView {
    std::vector<Order> orders;      // les plus recentes d'abord
    std::int64_t       counter{-1}; // le compteur des changements (GET /admin/etat : commandes)
    int                toDeliver{0};
    [[nodiscard]] const Order* find(int number) const;
};
bool parseOrders(const core::json::Value& v, OrdersView& out, std::string* why);

// ---- les textes ----
[[nodiscard]] std::string amountText(std::int64_t cents, const std::string& currency = "eur");   // "490,00 \xE2\x82\xAC"
// "490", "490,5", "490.50", "1 490,00 \xE2\x82\xAC" -> centimes ; faux : illisible.
bool parseAmount(std::string_view text, std::int64_t& cents);
[[nodiscard]] std::string stateLabel(const std::string& state);   // "pay\xC3\xA9" "e", "\xC3\xA0 livrer"...
// Le mode d'une cle secrete de Stripe : "reel" (sk_live_, rk_live_), "test" (sk_test_, rk_test_ ; essai_jeton_ : les
// essais) ; vide : ce n'est pas une cle secrete de Stripe.
[[nodiscard]] std::string modeOfKey(std::string_view key);
[[nodiscard]] std::string modeLabel(const std::string& mode);   // "Stripe en mode test", "Stripe en mode r\xC3\xA9" "el"
// Le tableau de bord de Stripe (le paiement d'une commande : `payment`) ; https://dashboard.stripe.com/...
[[nodiscard]] std::string dashboardUrl(const std::string& mode, const std::string& payment = {});

// ---- le reseau (BLOQUANT) : le service ----
[[nodiscard]] online::Result readSettings(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, Settings& out);
[[nodiscard]] online::Result saveSettings(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, const Settings& in);
[[nodiscard]] online::Result listOrders(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, OrdersView& out);
// Avec un client deja ouvert (la livraison) : prendre, livree, a-revoir. `status` : le code HTTP (409 : prise, etat).
[[nodiscard]] online::Result takeOrder(online::Client& c, int number, int* status = nullptr);
[[nodiscard]] online::Result markDelivered(online::Client& c, int number, const std::string& licenseId, std::string* alreadyLicence = nullptr);
[[nodiscard]] online::Result markReview(online::Client& c, int number, const std::string& reason);
[[nodiscard]] online::Result readSettings(online::Client& c, Settings& out);
[[nodiscard]] online::Result listOrders(online::Client& c, OrdersView& out);
// 1.46.1 : l'admin+ - les produits et les prix chez Stripe mis a jour par le service (POST /admin/boutique/stripe).
[[nodiscard]] online::Result syncStripeCatalog(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, CatalogState& out);
// L'admin+ : le remboursement demande a Stripe par le service (la commande passe remboursee au webhook).
[[nodiscard]] online::Result refund(const std::string& folder, const licensing::SigningKey& key, const online::Options& o, int number);
// La version du service sait-elle la boutique (1.46.0 ou plus) ? `version` : celle de /v1/etat.
[[nodiscard]] bool serviceKnowsShop(const std::string& version);

// ---- Brancher Stripe (BLOQUANT) ----
struct ConnectReport {
    bool                     ok{false};
    std::string              mode;        // test, reel
    std::string              webhookId;   // we_...
    int                      removed{0};  // les anciens points d'arrivee de cette adresse, effaces
    deployer::Outcome        outcome{deployer::Outcome::Failed};   // l'echec du kit (Node.js, Cloudflare...)
    std::string              reason, next, link;
    std::vector<std::string> lines;       // ce qui s'est passe (sans secret)
    [[nodiscard]] std::string text() const;
};
// `secretKey` est effacee (remplie de zeros, videe) quoi qu'il arrive. `serviceUrl` : l'adresse du service (https ;
// http://127.0.0.1 pour les essais). `kit` : ou trouver le kit et Node.js (deployer::Options).
[[nodiscard]] ConnectReport connectStripe(std::string& secretKey, const std::string& serviceUrl, const deployer::Options& kit,
                                          const deployer::LineSink& onLine = {}, const std::atomic<bool>* cancel = nullptr);
// L'adresse de l'API de Stripe : https://api.stripe.com, ou celle des essais (setStripeApiBaseForTests, sinon
// XPG_STRIPE_API_ESSAI) si c'est une adresse de ce poste.
[[nodiscard]] std::string stripeApiBase();
void setStripeApiBaseForTests(std::string base);
// Un texte sans rien qui ressemble a un secret de Stripe (sk_..., rk_..., whsec_..., essai_jeton_...).
[[nodiscard]] std::string maskStripe(std::string text);

} // namespace license_admin::shop
