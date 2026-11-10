// =============================================================================
//  app/LicenceShop.hpp - 1.46.0 : LA BOUTIQUE VUE DU STUDIO (le contrat :
//  docs/BOUTIQUE.md, par. 1, 4, 7 et 8) - sans reseau, sans fenetre
// -----------------------------------------------------------------------------
//  LE CATALOGUE : GET /v1/boutique (public) - ce qui s'achete en ligne, le montant
//  TTC (centimes) de chaque offre (<< ed:PROFESSIONAL >>) et de chaque duree d'une
//  option (<< opt:ai_advanced:365 >>), le mode de Stripe (<< test >> : un paiement
//  d'essai, aucune carte reelle debitee). Lu par le client du service
//  (LicenceService) seulement d'un service 1.46.0 ou plus dont GET /v1/etat dit
//  << boutique >> ; garde en memoire et dans le dossier des licences
//  (boutique.json, commun aux deux Studios), comme le registre : la fenetre
//  montre les boutons << Acheter >> sans attendre le reseau. Un service plus
//  ancien, une boutique fermee : aucun bouton, sans un mot (<< Demander >> reste).
//
//  LES COMMANDES DE CE PC (GET /v1/boutique/commandes) : leur etat (a_payer,
//  payee, livree, expiree, annulee, echec_paiement, remboursee, a_revoir), le
//  recu de Stripe ; gardees dans le releve du service (service-licences.json :
//  Record::orders) avec ce que seul ce PC sait - le nonce de la demande scellee
//  avec la commande (la livraison d'une option se reconnait a lui), l'option et
//  ce qu'en dit l'ecran.
//
//  LA CARTE N'EST JAMAIS ICI : le client paie sur la page de Stripe, dans son
//  navigateur ; ni le Studio, ni le service, ni l'Admin ne la voient, rien n'est
//  garde pour un achat suivant (cardSentence).
// =============================================================================
#pragma once

#include "../licensing/Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::licence::shop {

// La version du service qui a la boutique.
inline constexpr const char* kServiceRequired = "1.46.0";
// Le service est-il assez recent pour la boutique (sa version, GET /v1/etat) ?
[[nodiscard]] bool serviceKnowsShop(std::string_view version);

// ---- le catalogue ----
struct Article {
    std::string  id;         // "ed:PROFESSIONAL", "opt:ai_advanced:365"
    std::string  kind;       // "edition", "option"
    std::string  edition;    // une offre : son identifiant (le registre)
    std::string  option;     // une option : son identifiant
    int          days{0};    // une option : sa duree (0 : jusqu'a la fin de la licence)
    std::int64_t amount{0};  // centimes, TTC
    std::string  label;      // le libelle de la facture
};
struct Catalogue {
    bool                 known{false};      // un service 1.46+ a repondu (faux : rien a montrer)
    bool                 open{false};       // la boutique est ouverte (des articles)
    std::string          currency{"eur"};
    std::string          mode;              // "test", "reel" ; vide : inconnu (fermee)
    std::vector<Article> articles;
    licensing::Seconds   receivedAt{0};
    std::string          serviceVersion;
    [[nodiscard]] const Article* find(std::string_view id) const noexcept;
    // L'article en vente d'une offre ; nul : pas en vente en ligne.
    [[nodiscard]] const Article* edition(std::string_view editionId) const noexcept;
    // L'article en vente d'une option pour cette duree ; nul : pas en vente en ligne.
    [[nodiscard]] const Article* option(std::string_view optionId, int days) const noexcept;
    // Les durees en vente d'une option.
    [[nodiscard]] std::vector<const Article*> options(std::string_view optionId) const;
    [[nodiscard]] bool test() const noexcept { return mode == "test"; }
    // Ouverte, avec au moins un article : les boutons << Acheter >>.
    [[nodiscard]] bool selling() const noexcept { return known && open && !articles.empty(); }
};
// La reponse de GET /v1/boutique ; faux : `why` (illisible). Un article mal ecrit est ignore.
bool parseCatalogue(std::string_view json, Catalogue& out, std::string* why = nullptr);
// Le cache (boutique.json) : ecrit, relu.
[[nodiscard]] std::string writeCatalogue(const Catalogue& c);
bool readCatalogue(std::string_view json, Catalogue& out);
[[nodiscard]] std::string cachePath();
// Le dernier catalogue connu (la memoire ; sinon le fichier, relu une fois) ; inconnu : known faux.
[[nodiscard]] Catalogue catalogue();
// Le nouveau catalogue (le client du service) : en memoire et dans le fichier. Un service sans boutique (trop ancien,
// fermee) : known vrai, open faux - les boutons disparaissent.
void setCatalogue(const Catalogue& c);
// Les essais : oublier la memoire (le fichier est relu au prochain catalogue()).
void resetCatalogueForTests();
// Un numero qui monte a chaque nouveau catalogue (la fenetre se refait).
[[nodiscard]] unsigned long long catalogueRevision() noexcept;

// ---- les textes ----
// "490,00 EUR" (le signe de l'euro, une espace insecable avant ; les milliers : "1 490,00") ; une autre devise : "490,00 USD".
[[nodiscard]] std::string amountText(std::int64_t cents, const std::string& currency = "eur");
// "490,00 EUR TTC" (le signe de l'euro)
[[nodiscard]] std::string amountTtc(std::int64_t cents, const std::string& currency = "eur");
// "Acheter -- 490,00 EUR TTC" (un tiret long, le signe de l'euro)
[[nodiscard]] std::string buyLabel(const Article& a, const std::string& currency = "eur");
// << Paiement d'essai : aucune carte reelle n'est debitee >>
[[nodiscard]] std::string testModeText();
// << Tu paies sur la page securisee de Stripe : ta carte n'est ni vue ni gardee par XPG ; elle est redemandee a chaque achat. >>
[[nodiscard]] std::string cardSentence();

// ---- les commandes ----
inline constexpr const char* kToPay = "a_payer";
inline constexpr const char* kPaid = "payee";
inline constexpr const char* kDelivered = "livree";
inline constexpr const char* kExpired = "expiree";
inline constexpr const char* kCancelled = "annulee";
inline constexpr const char* kFailed = "echec_paiement";
inline constexpr const char* kRefunded = "remboursee";
inline constexpr const char* kReview = "a_revoir";
// La page de paiement : 30 minutes ; la commande regardee toutes les 10 secondes pendant ce temps, puis toutes les
// 5 minutes tant qu'elle est payee (ou encore a payer).
inline constexpr double kFastPeriod = 10.0, kFastFor = 30.0 * 60.0, kSlowPeriod = 300.0;
inline constexpr std::size_t kMaxOrders = 50;

struct Order {
    std::uint64_t      number{0};
    std::string        article, label, currency{"eur"}, state, mode;
    std::int64_t       amount{0};
    licensing::Seconds created{0}, updated{0}, expires{0};
    std::string        licence;      // livree : la licence
    std::string        receiptUrl;   // le recu de Stripe (sa facture) ; vide : aucun
    std::string        payUrl;       // a_payer : la page de paiement
    // Ce que seul ce PC sait (le releve) : le nonce de la demande scellee avec la commande, l'option commandee (son
    // identifiant) et ce qu'en dit l'ecran (<< Assistant IA avance, 1 an >>), la livraison appliquee ici.
    std::string        nonce, option, optionText;
    bool               applied{false};
};
// La reponse de GET /v1/boutique/commandes ; faux : `why`.
bool parseOrders(std::string_view json, std::vector<Order>& out, std::string* why = nullptr);
// Les commandes relues du service, fondues dans celles du releve (ce que ce PC sait reste ; les plus recentes d'abord,
// kMaxOrders au plus). Vrai : quelque chose a change.
bool mergeOrders(std::vector<Order>& mine, const std::vector<Order>& fresh);
// La commande de ce numero ; nul : aucune.
[[nodiscard]] const Order* findOrder(const std::vector<Order>& list, std::uint64_t number) noexcept;
[[nodiscard]] Order*       findOrder(std::vector<Order>& list, std::uint64_t number) noexcept;
// Une commande close (livree, expiree, annulee, refusee, remboursee) : plus rien a regarder.
[[nodiscard]] bool closed(const std::string& state) noexcept;
// Une commande a regarder encore (a payer, payee, a revoir).
[[nodiscard]] bool open(const std::string& state) noexcept;
// << en attente du paiement >>, << payee >>, << livree >>... (avec leurs accents)
[[nodiscard]] std::string stateLabel(const std::string& state);
// La phrase de la fenetre << Commande n. N >> (ce qui se passe, ce qu'il reste a faire).
[[nodiscard]] std::string stateText(const Order& o, licensing::Seconds now);
// 0 : neutre, 1 : bien, 2 : attention, 3 : erreur (ui::Tone).
[[nodiscard]] int stateTone(const std::string& state) noexcept;
// La commande qui attend (a payer, puis payee, puis a revoir ; la plus recente) ; nul : aucune.
[[nodiscard]] const Order* waitingOrder(const std::vector<Order>& list) noexcept;

// ---- 1.48.0 : LE PANIER (une seule boutique : les achats et les demandes) ----
//  Chaque offre et chaque duree d'option : << Acheter >> (une ligne a payer) ; << ou la demander sans paiement >> (une
//  ligne de demande). Le panier se garde dans le dossier des licences (panier.json, commun aux deux Studios) : il
//  survit a la fermeture de la fenetre. Payer : UNE page de paiement pour toutes les lignes a payer ; Envoyer la
//  demande : les lignes sans paiement. Une option demande sa licence : celle de ce PC, ou une offre du panier.
struct CartLine {
    std::string  article;          // l'article du catalogue ("ed:PROFESSIONAL", "opt:travail_a_plusieurs:30")
    std::string  kind;             // "edition", "option"
    std::string  edition, option;
    int          days{0};          // une option : sa duree (0 : jusqu'a la fin de la licence)
    std::string  label;            // << PROFESSIONAL >>, << Travail a plusieurs, 1 mois >>
    std::string  gives;            // ce qu'elle donne (une ligne)
    std::int64_t amount{0};        // centimes TTC (une ligne a payer) ; 0 : sur demande
    bool         request{false};   // sans paiement (une demande)
    std::string  licence;          // une option : la licence qu'elle etend ; vide : l'offre du panier
};
struct Cart {
    std::vector<CartLine> lines;
    std::string           currency{"eur"};
    [[nodiscard]] std::int64_t total() const noexcept;      // les lignes a payer (TTC)
    [[nodiscard]] std::size_t  toPay() const noexcept;      // les lignes a payer
    [[nodiscard]] std::size_t  requests() const noexcept;   // les lignes sans paiement
    [[nodiscard]] bool         hasOffer() const noexcept;   // une offre (sa licence pour une option du panier)
};
[[nodiscard]] std::string cartPath();
[[nodiscard]] Cart loadCart();
bool saveCart(const Cart& c, std::string* why = nullptr);
// Ajouter une ligne : faux, `why` (deja dans le panier ; une option sans licence ni offre dans le panier).
bool addToCart(Cart& c, CartLine line, const std::string& currentLicence, std::string* why);
// Un numero qui monte a chaque panier ecrit (la fenetre se refait).
[[nodiscard]] unsigned long long cartRevision() noexcept;

} // namespace app::licence::shop
