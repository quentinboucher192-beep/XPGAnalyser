// =============================================================================
//  license_admin/ServiceClient.hpp - 1.35.0 : LE SERVICE DES LICENCES, COTE
//  EDITEUR (l'outil de l'EDITEUR seulement ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Le contrat : docs/SERVICE-LICENCES.md ; les formats partages :
//  licensing/Service.hpp. Le service est le Worker Cloudflare que l'editeur met
//  en ligne lui-meme (outils/service/LISEZ-MOI-SERVICE.txt). Il remplace GitHub
//  pour tout (la passerelle des demandes de la 1.34, la synchro de la 1.31).
//
//  CE QUE FAIT L'OUTIL DE L'EDITEUR AVEC LUI :
//    - le regler : son adresse (service.json, dans le dossier de la cle) -
//      https://..., ou http://127.0.0.1 pour les essais ;
//    - le verifier : GET /v1/etat, la cle du service, son certificat ;
//    - CERTIFIER LA CLE DU SERVICE : lire sa cle publique (son empreinte est
//      montree), signer avec la cle de l'editeur un certificat << service >>
//      (un an ; les droits activation, flottante, heure, decouverte ; l'adresse
//      reglee) et le deposer - puis le renouveler avant sa fin ;
//    - publier le registre, et la liste de revocation (PRIVEE : le service ne
//      la donne qu'aux PC concernes) ;
//    - LES LICENCES EN LIGNE : une licence signee SANS PC (licensing : le champ
//      service = l'adresse, aucun PC lie), deposee avec l'empreinte d'un CODE
//      D'ACTIVATION tire ici (le code lui-meme n'est jamais envoye ni ecrit : il
//      est montre une fois a l'editeur) et la duree du bail ; la lister (les
//      activations par PC, les jetons flottants), retirer un PC, la desactiver,
//      la prolonger (une emission plus recente, le meme code ou un neuf) ;
//    - LES DEMANDES MANUELLES : les relever (GET /admin/demandes), les ouvrir
//      avec la cle de l'editeur (scellees pour elle), verifier la signature du
//      PC, les ranger sur ce poste (demandes-en-ligne/) ; refuser (l'etat
//      << poste >> signe, la raison scellee pour le PC) ; repondre par une
//      licence liee au PC (son etat << poste >>, la licence scellee pour lui) ;
//    - les comptes (GET /admin/stats) ; les fichiers PUBLICS des Studios (kit).
//
//  CHAQUE APPEL /admin EST SIGNE par la cle de l'editeur (service::signRequest :
//  elle ne quitte jamais ce poste) ; l'heure est celle du service (GET /v1/etat :
//  l'ecart est corrige). Aucun secret dans une adresse, un message, un journal.
//
//  LES DEMANDES (gardes de la 1.34, adaptees) : une demande (poste, nonce) deja
//  vue, ou dont la date SIGNEE par le PC n'est pas plus recente que la derniere
//  de ce PC, est REJOUEE : ignoree, rien n'est publie pour elle ; le sceau d'une
//  demande est garde a sa premiere lecture (son empreinte) - un sceau change
//  ensuite est ignore ; une demande d'AJOUT a une licence existante n'est pas
//  une preuve (joinWarning : l'application demande une confirmation) ; au plus
//  50 lignes de detail par releve (une avalanche ne noie rien).
//
//  RESEAU : tout ce qui suit << le reseau >> est BLOQUANT (runSlow dans
//  l'application, la console attend). Hors ligne, cle refusee, limite atteinte :
//  un refus en francais, jamais une reussite inventee.
// =============================================================================
#pragma once

#include "../licensing/Documents.hpp"   // 1.37.0 : licensing::License (le mail a l'acceptation)
#include "../licensing/Admins.hpp"      // 1.44.0 : plusieurs admins (la liste, qui croire)
#include "../core/Json.hpp"
#include "../licensing/Gateway.hpp"
#include "../licensing/KeyFile.hpp"
#include "../licensing/Time.hpp"

#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::online {

using licensing::Seconds;

inline constexpr const char* kConfigFile = "service.json";
inline constexpr const char* kSnapshotFile = "service-releve.json";   // le dernier releve des licences en ligne
inline constexpr const char* kStatusNumbersFile = "postes-publies.json";   // le dernier numero d'etat publie par PC
inline constexpr const char* kRequestsDir = "demandes-en-ligne";
inline constexpr const char* kFetchFile = "releve.json";               // dans kRequestsDir : le dernier releve
inline constexpr const char* kLicencesDir = "licences";                // dans kRequestsDir : les licences signees pour elles
inline constexpr int kDefaultLeaseDays = 30;                           // le bail d'une activation (jours)
inline constexpr int kCertificateDays = 365;                           // un certificat de service vaut un an
inline constexpr int kRenewBeforeDays = 30;                            // le renouveler dans ses 30 derniers jours

// ---- la configuration (service.json, dans le dossier de la cle) ----
struct Config {
    std::string url;   // "https://xpg-licences.compte.workers.dev" (sans / final)
};
[[nodiscard]] std::string configPath(const std::string& folder);
[[nodiscard]] bool configured(const std::string& folder);
bool readConfig(const std::string& folder, Config& out, std::string* why);
// L'adresse : https://hote[:port] sans chemin, ou http://127.0.0.1 / localhost (les essais). Une adresse copiee avec
// un / final est acceptee (il est retire).
bool writeConfig(const std::string& folder, const Config& config, std::string* why);
[[nodiscard]] std::string normalizeUrl(std::string_view in);

// ---- le resultat d'une operation (en francais ; jamais un secret) ----
struct Result {
    bool                     ok{false};
    std::string              message;
    std::vector<std::string> details;
    [[nodiscard]] std::string text() const;   // message puis details
};

struct Options {
    Seconds     now{0};       // l'heure de ce poste (les essais la fixent) ; l'ecart avec le service est corrige
    std::string tool;         // "XPG-License-Admin/1.3.0" (l'User-Agent)
    // 1.44.0 : PLUSIEURS ADMINS (docs/ADMINS.md). La cle qui signe les appels est celle de l'admin+ (la cle de l'editeur)
    // ou celle d'un admin de la liste. Ce que l'outil croit d'un document de l'archive (le relire, le sceller pour un
    // PC, le mail) : la cle qui signe, les cles de l'editeur (`editorKeys`), un admin de la liste (`admins`, avec son
    // droit ; retire : un document garde). Deposer un document (registre, revocations, licence) : le service ne prend
    // que celui signe par la cle qui appelle (l'editeur pour l'admin+). Vides : la cle qui signe seulement (comme avant).
    std::vector<licensing::crypto::PublicKey>   editorKeys;
    std::optional<licensing::admins::AdminList> admins;
    // X-XPG-Logiciel de chaque appel signe ("XPG-License-Admin/1.44.0") ; vide : celui de cette version.
    std::string software;
};

// ============================================================================ le client (BLOQUANT)
struct Reply {
    int               status{0};
    std::string       body;
    std::string       error;   // une panne du reseau (en francais) ; vide : une reponse est arrivee
    core::json::Value json;
    [[nodiscard]] std::string code() const;      // "erreur" d'une reponse refusee ("code", "complet"...)
    [[nodiscard]] std::string message() const;   // son "message" (nettoye, 300 caracteres au plus)
};

class Client {
public:
    // `key` : la cle de l'editeur (elle signe chaque appel /admin) ; elle doit vivre plus longtemps que le client.
    Client(std::string url, const licensing::SigningKey& key, Options options);
    // GET /v1/etat : l'heure du service (l'ecart est corrige pour signer). Faux : `why`.
    bool connect(std::string* why);
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] const core::json::Value& state() const noexcept { return state_; }   // la reponse de /v1/etat
    [[nodiscard]] Seconds serviceNow() const noexcept { return options_.now + offset_; }
    [[nodiscard]] const std::string& url() const noexcept { return url_; }
    [[nodiscard]] const Options& options() const noexcept { return options_; }   // 1.44.0 : qui croire (editorKeys, admins)
    [[nodiscard]] const licensing::SigningKey& key() const noexcept { return key_; }
    // Un appel ; `signed_` : avec les quatre en-tetes X-XPG-* (toutes les routes /admin et celles d'un PC).
    Reply call(const std::string& method, const std::string& pathAndQuery, const std::string& body = {}, bool signed_ = true);
    // Une reponse refusee, en francais : ce qui a ete refuse et pourquoi.
    [[nodiscard]] std::string explain(const Reply& r, const std::string& what) const;

private:
    std::string                    url_;
    const licensing::SigningKey&   key_;
    Options                        options_;
    Seconds                        offset_{0};
    bool                           connected_{false};
    core::json::Value              state_;
};

// Ouvrir le client du service de ce poste (la configuration, puis GET /v1/etat). Faux : `why`.
bool openClient(const std::string& folder, const licensing::SigningKey& key, const Options& o, std::unique_ptr<Client>& out, std::string* why);

// ============================================================================ l'etat, le certificat
struct ServiceState {
    bool          reached{false};      // /v1/etat a repondu
    std::string   version;             // celle du Worker
    Seconds       time{0};             // son heure
    std::string   serviceKey;          // sa cle publique (64 hexadecimaux) ; vide : pas encore creee
    bool          certified{false};    // /v1/etat dit qu'un certificat est depose
    bool          adminOk{false};      // la cle de ce poste est acceptee (/admin/cle-service : pas 403)
    bool          certOk{false};       // le certificat lu : signe par CETTE cle de l'editeur, pour cette cle de service et cette adresse
    Seconds       certIssued{0}, certExpires{0};
    std::string   certUrl;
    std::string   certProblem;         // pourquoi le certificat ne va pas (vide : il va)
    std::uint64_t registry{0};         // le numero du registre depose (0 : aucun)
    std::string   certBlock;           // 1.47.0 : le certificat tel que depose (le bloc << service >>, signe par l'editeur)
    [[nodiscard]] bool needsRenewal(Seconds now) const;   // pas de certificat valable, ou il finit dans kRenewBeforeDays
};
// L'empreinte lisible d'une cle de service (64 hexadecimaux) : "1A2B 3C4D ..." ; vide si illisible.
[[nodiscard]] std::string keyFingerprint(const std::string& keyHex);

// ============================================================================ le reseau (BLOQUANT)
[[nodiscard]] Result verify(const std::string& folder, const licensing::SigningKey& key, const Options& o, ServiceState* out = nullptr);
// La cle publique du service (GET /admin/cle-service), a montrer avant de la certifier.
[[nodiscard]] Result readServiceKey(const std::string& folder, const licensing::SigningKey& key, const Options& o, std::string& keyHex);
// Signer et deposer le certificat (un an). `expectedKey` (facultatif) : la cle montree a l'editeur ; une autre cle
// trouvee entre-temps est refusee (rien n'est signe).
[[nodiscard]] Result certify(const std::string& folder, const licensing::SigningKey& key, const Options& o, const std::string& expectedKey = {});
// Le registre de l'archive (signe par cette cle) : depose s'il est plus recent que celui du service.
[[nodiscard]] Result publishRegistry(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// La liste de revocation de l'archive (signee par cette cle) : PUT /admin/revocations (PRIVEE).
[[nodiscard]] Result publishRevocations(const std::string& folder, const licensing::SigningKey& key, const Options& o);

// ---- les licences en ligne ----
struct OnlineActivation {
    std::string deviceKey;      // la cle du PC (64 hexadecimaux)
    std::string name;           // le nom du PC (donne par le client)
    std::string station;
    std::string state;          // "active", "liberee", "retiree", "revoquee"
    Seconds     activatedAt{0}, seenAt{0}, until{0};
    [[nodiscard]] std::string stateLabel() const;   // "active", "lib\xC3\xA9r\xC3\xA9" "e"...
};
struct OnlineLease {
    std::string deviceKey;
    Seconds     until{0};
};
struct OnlineLicence {
    std::string                   licenseId;
    Seconds                       issuedAt{0};
    std::string                   type;          // "perpetual", "temporary", "floating"...
    int                           seats{0};      // postes_max
    int                           leaseDays{kDefaultLeaseDays};
    bool                          active{true};
    bool                          hasCode{false};
    std::vector<OnlineActivation> activations;
    std::vector<OnlineLease>      leases;        // les jetons flottants en cours
    [[nodiscard]] bool floating() const { return type == "floating"; }
    [[nodiscard]] int activeCount() const;       // les activations << active >>
    [[nodiscard]] const OnlineActivation* activation(const std::string& deviceKey) const;
};
struct Snapshot {
    Seconds                    at{0};      // 0 : jamais releve
    std::string                url;
    std::vector<OnlineLicence> licences;
    [[nodiscard]] const OnlineLicence* licence(const std::string& id) const;
    [[nodiscard]] int devices() const;     // les PC actives (toutes licences)
    [[nodiscard]] int leases() const;      // les jetons flottants en cours
};
bool parseLicences(const core::json::Value& reply, Snapshot& out, std::string* why);
[[nodiscard]] Snapshot readSnapshot(const std::string& folder);
bool writeSnapshot(const std::string& folder, const Snapshot& s, std::string* why);

// Deposer une licence en ligne (le texte .xpglic : son bloc de licence, signe par cette cle, SANS PC). `codeHash` :
// l'empreinte du code d'activation (service::codeHash) ; vide : le service garde celle qu'il a. `leaseDays` : le bail
// (jours) ; 0 : le service garde celui qu'il a (30 pour une licence neuve).
[[nodiscard]] Result putLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const std::string& codeHash,
                                int leaseDays, bool active, const Options& o);
// GET /admin/licences ; `out` : le releve (aussi ecrit dans service-releve.json).
[[nodiscard]] Result listLicences(const std::string& folder, const licensing::SigningKey& key, const Options& o, Snapshot& out);
// POST /admin/activations/retirer : ce PC n'est plus renouvele (il s'arrete a la fin de son bail, ou des qu'il se connecte).
[[nodiscard]] Result retireDevice(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId,
                                  const std::string& deviceKey, const Options& o);
// Desactiver (ou reactiver) une licence en ligne : la licence de l'archive redeposee avec active = false (true) ;
// `leaseDays` 0 : le bail du service garde.
[[nodiscard]] Result setActive(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId, bool active, int leaseDays,
                               const Options& o);
// 1.40.0 : GET /admin/etat (signe par l'editeur) - des compteurs qui montent a chaque changement : << activite >> (un PC
// active, libere par son client, retire au renouvellement), << demandes >> (une demande deposee ou annulee par un
// client). L'outil ouvert le regarde toutes les 15 secondes et ne relit les licences, ou les demandes, que s'ils ont
// bouge. Un service d'avant la 1.40.0 ne l'a pas (404) : `supported` faux, le resultat reste reussi.
struct AdminWatch {
    bool          supported{false};
    std::int64_t  activity{-1}, requests{-1}, pending{-1}, changes{-1};
    std::string   version;
    // 1.44.0 (docs/ADMINS.md, par. 6) : le numero de la liste des admins, le dernier numero du journal des admins, la
    // derniere version publiee ; qui appelle : son role ("admin+", "admin") et ses droits. -1 / vide : un service d'avant
    // la 1.44.0. 1.51.0 : `invitedWaiting` - les admins inscrits par leur code d'invitation que l'Admin+ signe d'office
    // (<< admins_inscrits >> ; -1 : un service d'avant la 1.51.0, qui avait des demandes d'admin).
    std::int64_t             admins{-1}, journal{-1}, invitedWaiting{-1};
    std::string              publishedVersion;
    std::string              role;
    std::vector<std::string> rights;
    // 1.46.0 (docs/BOUTIQUE.md) : le compteur des changements des commandes et celles a livrer (payees) ; -1 : un service
    // d'avant la 1.46.0 (sans boutique).
    std::int64_t             orders{-1}, toDeliver{-1};
    // 1.47.0 (docs/ADMINS.md, par. 11) : la derogation << Gerer le service >> - la sienne (l'admin+ : celle en place).
    // derogationSaid : le service a repondu sur ce point (1.47.0) ; derogationId vide : aucune.
    bool                     derogationSaid{false};
    std::string              derogationId, derogationAdmin, derogationName, derogationState, derogationBy;
    std::int64_t             derogationStart{0}, derogationEnd{0}, derogationRevoked{0}, derogationUses{0};
    // 1.52.0 : le format 2 (<< tout >>, active par un code) - la duree, l'heure limite du code, les essais qui restent,
    // annulee (quand, pourquoi : code_faux, code_perime). Jamais le code ni son empreinte (le service ne les rend pas).
    std::int64_t             derogationFormat{1}, derogationDuration{0}, derogationValidUntil{0}, derogationTriesLeft{0}, derogationCancelled{0};
    std::string              derogationRight, derogationReason;
    std::int64_t             serverTime{0};   // l'heure du service (heure)
};
// 1.44.0 : lire la reponse de GET /admin/etat (ses compteurs et, en 1.44.0, le role et les droits de qui appelle).
void readAdminWatch(const core::json::Value& reply, AdminWatch& out);
[[nodiscard]] Result watchState(const std::string& folder, const licensing::SigningKey& key, const Options& o, AdminWatch& out);
// GET /admin/stats : les compteurs, une ligne chacun.
[[nodiscard]] Result stats(const std::string& folder, const licensing::SigningKey& key, const Options& o);

// ---- les demandes manuelles ----
enum class RequestState : std::uint8_t {
    Pending,      // << attente >> : elle attend ta decision
    Accepted,     // << acceptee >> : la licence est partie (liee au PC, ou en ligne avec un code)
    Refused,      // << refusee >>
    Cancelled,    // << annulee >> : le PC l'a annulee
    Replaced,     // << remplacee >> : une demande plus recente du meme PC
    Handled,      // << traitee >> : traitee ailleurs (un autre poste Admin)
    Unreadable,   // << illisible >> : scellee pour une autre cle, signature fausse...
    Ignored,      // << ignoree >> : rejouee (deja vue, ou pas plus recente que la derniere de ce PC)
};
[[nodiscard]] const char* requestStateKey(RequestState s) noexcept;
[[nodiscard]] bool parseRequestStateKey(std::string_view key, RequestState& out) noexcept;
[[nodiscard]] std::string requestStateLabel(RequestState s);

struct Entry {
    int                      number{0};      // le numero de la demande sur le service
    RequestState             state{RequestState::Pending};
    std::string              serviceState;   // "attente", "annulee", "remplacee", "traitee"
    std::string              device;         // l'identifiant du poste (32 hexadecimaux)
    std::string              deviceKey;      // la cle du PC ; vide : illisible
    std::string              nonce;
    std::string              customer, contact, message;
    std::string              edition, email;   // 1.37.0 : l'edition voulue (la boutique du Studio), le courriel du client (facultatifs)
    // 1.42.0 : une OPTION demandee pour la licence licenseId (vide : une demande de licence) - sa duree en jours (0 : jusqu'a la fin
    // de la licence ; -1 : des dates), son debut voulu (0 : des qu'elle est acceptee), son dernier jour (des dates, compris).
    std::string              option;
    int                      optionDays{0};
    Seconds                  optionFrom{0}, optionLastDay{0};
    [[nodiscard]] bool optionRequest() const noexcept { return !option.empty() && state != RequestState::Unreadable; }
    std::string              deviceName, fingerprint, software, licenseId;   // licenseId : celle demandee
    Seconds                  requestedAt{0};   // la date SIGNEE par le PC
    Seconds                  createdAt{0};     // le depot sur le service
    Seconds                  seenAt{0};        // le premier releve qui l'a vue
    Seconds                  answeredAt{0};
    std::string              answer;           // acceptee : la licence ; refusee : la raison (sur ce poste seulement)
    std::string              requestPath;      // le .xpgreq ("" : illisible)
    std::string              sidecarPath;      // le .json
    std::string              sealedHash;       // SHA-256 du sceau a sa premiere lecture
    std::vector<std::string> problems;
    // 1.44.0 : relevee par un admin, la demande est scellee pour un AUTRE admin (ou l'editeur) : listee sans contenu,
    // elle ne s'ouvre pas sur ce poste (Unreadable).
    bool                     forOther{false};
    [[nodiscard]] bool waiting() const noexcept { return state == RequestState::Pending; }
    [[nodiscard]] bool joinRequest() const noexcept { return !licenseId.empty() && state != RequestState::Unreadable; }
    // 1.37.0 : << acceptee ici >> - une licence est signee pour elle sur ce poste (answer : laquelle), sa reponse n'est pas
    // encore partie (le service injoignable...) : on ne l'accepte ni ne la refuse plus ; on RENVOIE la reponse.
    [[nodiscard]] bool acceptedHere() const noexcept { return state == RequestState::Pending && !answer.empty(); }
};
// << A verifier : ajout a la licence X demande par quelqu'un qui dit etre << Y >> - confirme avec ton client ... >>
[[nodiscard]] std::string joinWarning(const Entry& e);
[[nodiscard]] std::string joinWarning(const std::string& licenseId, const std::string& customer);
[[nodiscard]] std::vector<Entry> listRequests(const std::string& folder);   // les plus recentes d'abord
[[nodiscard]] std::string requestsFolder(const std::string& folder);
// La demande en attente : "N" ou "#N" (son numero), ou l'identifiant d'un poste (6 caracteres au moins).
bool findWaiting(const std::string& folder, const std::string& which, Entry& out, std::string* why);
[[nodiscard]] bool isOnlineRequestPath(const std::string& folder, const std::string& path);

struct FetchState {
    Seconds     fetchedAt{0};
    int         total{0}, pending{0};
    std::string url;
};
[[nodiscard]] FetchState readFetchState(const std::string& folder);

// Relever les demandes (GET /admin/demandes?depuis=N) et les ranger (processRequests).
[[nodiscard]] Result fetchRequests(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Ranger une liste de demandes ({"demandes":[...]}, telle que le service la rend) : les ouvrir, les verifier,
// appliquer les gardes. Sans reseau (le releve l'appelle ; les essais aussi).
[[nodiscard]] Result processRequests(const std::string& folder, const licensing::SigningKey& key, const core::json::Value& reply, const std::string& url,
                                     const Options& o);
// 1.48.1 : une demande CLOSE (refusee, annulee, remplacee, ignoree) peut s'effacer ; une demande en attente ou acceptee
// non (elle dit d'ou vient une licence) - `why` : pourquoi.
[[nodiscard]] bool requestDeletable(const Entry& e, std::string* why = nullptr);
// 1.48.1 : effacer une demande close - sur le service (POST /admin/demandes/supprimer ; deja effacee par son menage : rien
// a faire) puis sur ce poste (sa fiche et son fichier, s'il n'est celui d'aucune autre demande).
[[nodiscard]] Result deleteRequest(const std::string& folder, const licensing::SigningKey& key, int number, const Options& o);
// Refuser la demande en attente (findWaiting) : l'etat << refusee >> du poste, la raison scellee pour le PC, la
// demande marquee traitee.
[[nodiscard]] Result refuse(const std::string& folder, const licensing::SigningKey& key, const std::string& which, const std::string& reason,
                            const Options& o);
// 1.42.0 : une demande d'OPTION acceptee pour une licence EN LIGNE (sa nouvelle emission deja deposee) : << acceptee >> pour le PC
// qui l'a demandee (sans licence ni code : son Studio le dit et renouvelle), la demande traitee. Une licence LIEE : la
// publication de sa nouvelle emission (publishBoundLicence) repond deja au PC qui l'a demandee.
[[nodiscard]] Result answerOption(const std::string& folder, const licensing::SigningKey& key, const std::string& which, const std::string& licenseId,
                                  const std::string& message, const Options& o);
// 1.37.0 : LE CODE D'ACTIVATION GARDE SUR CE POSTE, pour le revoir (Licences \xE2\x80\xBA Voir le code...) : chiffre pour ce compte
// de ce PC (DPAPI sous Windows ; ailleurs un fichier 0600), dans codes-activation/<licence>.code du dossier de la cle. Jamais
// synchronise (l'archive ne le porte pas), jamais en clair sur le disque sous Windows. Un nouveau code remplace l'ancien.
bool storeCode(const std::string& folder, const std::string& licenseId, const std::string& code, std::string* why);
bool readCode(const std::string& folder, const std::string& licenseId, std::string& code, std::string* why);
[[nodiscard]] bool hasCode(const std::string& folder, const std::string& licenseId);

// 1.37.0 : LE MAIL au client a l'acceptation (l'appelant l'envoie avant de publier ; ce module n'envoie rien) : son
// etat part, scelle, dans la reponse au PC ("envoye", "echec" : le Studio le dit et demande de verifier l'adresse ; vide :
// pas de mail). Un `Mailer` rend cet etat pour une demande (et la licence acceptee, et le code d'une licence en ligne).
struct MailOutcome {
    std::string state;   // gateway::kMailSent, kMailFailed ; vide : aucun mail
    std::string to;
    std::string note;    // la phrase pour l'editeur (le mail parti, pourquoi il n'est pas parti) ; vide : rien a dire
};
using Mailer = std::function<MailOutcome(const Entry& request, const licensing::License& licence, const std::string& code)>;
// Le mail d'acceptation (sans reseau) : l'objet et le texte - la licence, l'edition, le client, jusqu'a quand, les PC, ce
// qu'elle ouvre (les noms du registre `reg`, s'il est la), le code d'une licence en ligne et comment l'activer, la
// signature `signer` (le nom de l'editeur ; vide : << Ton editeur >>).
struct MailText {
    std::string subject, body;
};
[[nodiscard]] MailText acceptanceMail(const licensing::License& licence, const licensing::Registry* reg, const Entry& request, const std::string& code,
                                      const std::string& signer);
// Le Mailer de l'editeur : le mail d'acceptation par SA boite (Mail.hpp : <dossier>/mail.json et son mot de passe), s'il
// est regle et que la demande donne un courriel ; nul : rien de regle (aucun mail, le Studio montre le code). `reg` :
// les noms du registre (copie).
[[nodiscard]] Mailer smtpMailer(const std::string& folder, const licensing::Registry* reg);
// Repondre a une demande par une licence EN LIGNE : << acceptee >>, la demande marquee traitee. 1.37.0 : LE CODE part
// scelle pour CE PC (son Studio s'active tout seul ; jamais en clair sur le service), avec l'identifiant de la licence
// et l'etat du mail (`mailer` : nul, pas de mail). `licenceText` : la licence deposee (pour le mail).
[[nodiscard]] Result answerOnline(const std::string& folder, const licensing::SigningKey& key, int number, const std::string& licenseId,
                                  const std::string& code, const std::string& licenceText, const Mailer& mailer, const Options& o);
// 1.46.0 : LA BOUTIQUE (docs/BOUTIQUE.md, par. 5) - la demande scellee d'une COMMANDE (comme une demande en ligne :
// scellee pour la cle de ce poste, la signature du PC verifiee, son poste) ouverte en une Entry (sans numero de demande :
// deviceKey, nonce, customer, email, edition, option, deviceName...). Faux : `why`.
bool openSealedRequest(const licensing::SigningKey& key, const std::string& device, const std::string& keyId, const std::string& sealed, Seconds now,
                       Entry& out, std::string* why);
// Repondre au PC d'une commande livree : << acceptee >> - comme answerOnline (le code scelle pour CE PC, la licence en
// ligne, le mail), sans demande a marquer traitee. `code` vide : une option ajoutee a sa licence en ligne (le PC
// renouvelle et la recoit) ; `message` : le mot que lit le Studio.
[[nodiscard]] Result answerOrder(const std::string& folder, const licensing::SigningKey& key, const Entry& request, const std::string& licenseId,
                                 const std::string& code, const std::string& licenceText, const std::string& message, const Mailer& mailer, const Options& o);
// 1.37.0 : UNE SEULE ACCEPTATION - la demande n (en attente) est marquee << acceptee ici >> par la licence `licenseId`,
// des sa signature, avant que la reponse parte (Entry::acceptedHere). Faux : elle n'attend plus, ou l'ecriture echoue.
bool markAcceptedHere(const std::string& folder, int number, const std::string& licenseId, std::string* why);

// ---- les licences liees a des PC (l'etat << poste >> de chacun, signe par l'editeur) ----
// Une licence signee (.xpglic) a CHACUN de ses PC : << acceptee >> pour celui dont une demande attend (elle est marquee
// traitee), << mise_a_jour >> pour les autres ; la licence scellee pour chaque PC (et la liste de revocation de
// l'archive si elle nomme cette licence, ou si `withRevocations`).
[[nodiscard]] Result publishBoundLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText,
                                         bool withRevocations, const Options& o, const Mailer& mailer = {});
// Apres une revocation : la liste deposee (PUT /admin/revocations), puis << revoquee >> pour chaque PC lie donne.
[[nodiscard]] Result publishRevocation(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId,
                                       const std::vector<std::string>& deviceKeys, const std::string& reason, const Options& o);
// Apres le retrait d'un PC d'une licence liee : la nouvelle emission aux autres, << retiree >> pour lui.
[[nodiscard]] Result publishRemoval(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText,
                                    const std::string& removedDeviceKey, const Options& o);

// ============================================================================ les fichiers des Studios
//  Ce que l'integrateur compile dans les Studios livres (rien de secret) :
//    PublisherKey.hpp     la cle PUBLIQUE de l'editeur (une cle d'ESSAI est refusee)   -> src/licensing/
//    registre.xpgreg      le registre signe de l'archive (signe par CETTE cle)         -> ressources-licences/
//    ServiceAddress.hpp   l'adresse du service (vide : pas de service)                 -> src/licensing/
//    cle-publique.txt     la cle publique en hexadecimal (PUBLISHER_KEYS du Worker)     -> outils/service/
//    LISEZ-MOI.txt        quoi en faire
[[nodiscard]] std::string serviceAddressHeader(const std::string& url);
// cle-publique.txt : une ligne # (ce que c'est), puis la cle PUBLIQUE en hexadecimal (64) - ce que deployer.bat lit.
[[nodiscard]] std::string publicKeyText(const licensing::SigningKey& key);
[[nodiscard]] Result studioKit(const std::string& folder, const licensing::SigningKey* key, const std::string& outDir, Seconds now);

} // namespace license_admin::online
