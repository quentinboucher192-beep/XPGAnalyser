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
// =============================================================================
#pragma once

#include "ServiceClient.hpp"
#include "ServiceDeploy.hpp"

#include "../core/Json.hpp"
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
// Les evenements de Stripe que le webhook recoit (docs/BOUTIQUE.md, par. 2).
[[nodiscard]] const std::vector<std::string>& stripeEvents();

// ---- le catalogue ----
struct Article {
    std::string  id;          // "ed:PROFESSIONAL", "opt:ai_advanced:365"
    std::string  kind;        // "edition", "option"
    std::string  edition;     // une offre du registre
    std::string  option;      // une option du registre
    int          days{0};     // la duree de l'option (0 : jusqu'a la fin de la licence)
    std::int64_t amount{0};   // en centimes, TTC
    std::string  label;       // le nom sur la page de paiement et la facture
};
[[nodiscard]] std::string articleId(const std::string& kind, const std::string& editionOrOption, int days = 0);

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
    [[nodiscard]] const Article* find(const std::string& id) const;
};
bool parseSettings(const core::json::Value& v, Settings& out, std::string* why);
// Le corps de PUT /admin/boutique.
[[nodiscard]] std::string writeSettings(const Settings& s);
// Les regles du service (montants, libelles, identifiants, doublons) ; faux : `why`.
bool validSettings(const Settings& s, std::string* why);

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
