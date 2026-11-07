// =============================================================================
//  licensing/Gateway.hpp - 1.34.0 : LA PASSERELLE GITHUB DES LICENCES (les
//  formats ; aucun reseau ici)
// -----------------------------------------------------------------------------
//  UN DEPOT GITHUB PUBLIC, << la passerelle >>, entre les logiciels des clients
//  (API Studio, IHM Studio) et l'outil de l'editeur :
//
//    les CONTENUS du depot - ecrits par l'editeur seul (son jeton), signes par
//    sa cle, lus par les clients SANS jeton (raw.githubusercontent.com) :
//      passerelle.xpgpas       la configuration (signee) : le jeton des demandes
//      registre.xpgreg         le registre en vigueur (adopte s'il est plus recent)
//      revocations.xpgrev      la liste de revocation en vigueur (idem)
//      postes/<poste>.xpgpos   l'etat d'UN PC (signe) : sa demande recue,
//                              acceptee (sa licence, scellee pour lui), refusee
//                              (la raison, scellee), annulee, mise a jour,
//                              revoquee, retiree d'une licence
//
//    les ISSUES du depot - ouvertes par les clients avec le JETON DES DEMANDES
//    (un jeton a grain fin limite aux issues de ce depot : il ne peut ni ecrire
//    un fichier, ni toucher a une licence) :
//      une demande d'activation = une issue ; son corps porte la demande signee
//      par le PC, scellee pour l'editeur (personne d'autre ne lit le nom du
//      client, du PC, le message) ; annuler = un commentaire signe par le PC.
//
//  CE QUI EST PUBLIC, HONNETEMENT : le depot est public (les clients le lisent
//  sans jeton) ; on y voit le nombre de PC et de demandes, leurs dates, et des
//  identifiants de poste (une empreinte de la cle du PC, qui ne dit rien de son
//  materiel). Les noms, les licences et les messages sont scelles (libsodium :
//  une boite scellee vers la cle Ed25519 convertie en X25519). Le JETON DES
//  DEMANDES est dans la configuration publiee, masque (pas en clair : les robots
//  de GitHub ne le revoquent pas) mais lisible par qui cherche - il ne permet
//  QUE d'ouvrir, de commenter et de fermer des issues de ce depot. Tout ce que
//  lit un client est signe par l'editeur : un faux etat, une fausse licence, une
//  fausse configuration sont refuses.
//
//  L'IDENTIFIANT D'UN POSTE : les 32 premiers hexadecimaux de
//  SHA-256("xpg-poste-passerelle/1" || cle publique du PC) - stable pour ce PC,
//  sans rien dire de sa cle ni de son materiel.
// =============================================================================
#pragma once

#include "KeyFile.hpp"
#include "SignedFile.hpp"
#include "Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace licensing::gateway {

inline constexpr const char* kDefaultApi = "https://api.github.com";
inline constexpr const char* kDefaultRaw = "https://raw.githubusercontent.com";

// ---- l'adresse ----
struct Address {
    std::string repo;                 // "proprietaire/nom" ; vide : pas de passerelle
    std::string branch{"main"};
    std::string api{kDefaultApi};     // les essais : http://127.0.0.1:PORT/api
    std::string raw{kDefaultRaw};     // les essais : http://127.0.0.1:PORT/raw
    [[nodiscard]] bool empty() const noexcept { return repo.empty(); }
};
[[nodiscard]] bool validRepo(std::string_view repo) noexcept;     // "proprietaire/nom" (lettres, chiffres, . _ -)
[[nodiscard]] bool validBranch(std::string_view branch) noexcept;
// L'adresse compilee dans ce programme (GatewayAddress.hpp ; l'option CMake XPG_PASSERELLE_DEPOT la remplace) ; vide :
// ce programme n'a pas de passerelle (les demandes et les licences passent par fichier, comme avant).
// SEULEMENT dans une construction d'essai (XPG_LICENCES_CLE_ESSAI) : la variable d'environnement
// XPG_PASSERELLE la remplace - "proprietaire/nom" ou "proprietaire/nom@http://127.0.0.1:PORT" (api = base/api,
// raw = base/raw : le faux GitHub des essais). Un programme livre ne la lit pas.
[[nodiscard]] Address compiledAddress();

// ---- les chemins et les adresses ----
inline constexpr const char* kConfigPath = "passerelle.xpgpas";
inline constexpr const char* kRegistryPath = "registre.xpgreg";
inline constexpr const char* kRevocationsPath = "revocations.xpgrev";
inline constexpr const char* kStatusDir = "postes";
[[nodiscard]] std::string statusPath(std::string_view deviceId);                   // "postes/<id>.xpgpos"
[[nodiscard]] std::string rawUrl(const Address& a, std::string_view path);         // <raw>/<repo>/<branche>/<path>
[[nodiscard]] std::string apiUrl(const Address& a, std::string_view tail);         // <api>/repos/<repo><tail> ("/issues")

// ---- l'identifiant d'un poste ----
[[nodiscard]] std::string deviceId(const crypto::PublicKey& deviceKey);
[[nodiscard]] bool isDeviceId(std::string_view s) noexcept;   // 32 hexadecimaux minuscules

// ---- sceller, ouvrir (le base64 d'un sceau, en lignes de 76) ----
[[nodiscard]] std::string sealFor(const crypto::PublicKey& key, std::string_view plain);   // vide : la cle est fausse
bool openWith(const SigningKey& key, std::string_view sealedBase64, std::string& plain, std::string* why);

// ---- la configuration publiee (passerelle.xpgpas, un bloc << passerelle >> signe par l'editeur) ----
struct Config {
    int           version{1};
    std::uint64_t number{0};          // croissant : une configuration plus ancienne est refusee
    Seconds       issuedAt{0};
    std::string   repo;               // le depot pour lequel elle vaut (= Address::repo, sinon refusee)
    std::string   requestToken;       // EN CLAIR en memoire ; ecrit masque ("masque" dans le JSON)
};
[[nodiscard]] std::string writeConfig(const Config& c);
bool parseConfig(std::string_view json, Config& out, std::string* why);
// Le masque du jeton (XOR avec un flot tire du nom du depot, puis base64) : PAS un secret, seulement pas en clair.
[[nodiscard]] std::string maskToken(std::string_view repo, std::string_view token);
bool unmaskToken(std::string_view repo, std::string_view masked, std::string& token);

// ---- l'etat d'un PC (postes/<id>.xpgpos, un bloc << poste >> signe par l'editeur) ----
enum class State : std::uint8_t {
    Received,    // << recue >> : l'editeur a vu la demande, elle attend sa decision
    Accepted,    // << acceptee >> : la licence est dedans (scellee)
    Refused,     // << refusee >> : la raison est dedans (scellee)
    Cancelled,   // << annulee >> : le PC l'a annulee, l'editeur l'a vu
    Updated,     // << mise_a_jour >> : une nouvelle emission de sa licence (un PC ajoute ou retire, une prolongation)
    Revoked,     // << revoquee >> : sa licence est revoquee (la liste de revocation suit)
    Removed,     // << retiree >> : ce PC a ete retire de sa licence
};
[[nodiscard]] const char* stateKey(State s) noexcept;
[[nodiscard]] bool parseStateKey(std::string_view key, State& out) noexcept;
[[nodiscard]] std::string stateLabel(State s);   // "Accept\xC3\xA9" "e"...
struct DeviceStatus {
    int           version{1};
    std::string   device;             // l'identifiant du poste (= le nom du fichier)
    std::uint64_t number{0};          // croissant pour ce poste : un etat plus ancien est refuse
    Seconds       issuedAt{0};
    State         state{State::Received};
    std::string   requestNonce;       // la demande a laquelle il repond ("" : aucune)
    std::string   licenseId;          // la licence concernee ("" : aucune)
    std::string   sealed;             // le contenu (StatusContent), scelle pour la cle du PC (base64) ; peut etre vide
};
[[nodiscard]] std::string writeDeviceStatus(const DeviceStatus& s);
bool parseDeviceStatus(std::string_view json, DeviceStatus& out, std::string* why);
// Ce que seul le PC lit.
struct StatusContent {
    std::string message;              // la raison d'un refus, un mot de l'editeur (texte libre, court)
    std::string license;              // le fichier de licence (.xpglic : ses blocs signes, tel quel) ; vide : aucun
};
[[nodiscard]] std::string writeStatusContent(const StatusContent& c);
bool parseStatusContent(std::string_view json, StatusContent& out, std::string* why);

// ---- la demande en ligne (le corps d'une issue) ----
//  Le corps :  XPG-DEMANDE-EN-LIGNE 1\n
//              poste <identifiant du poste>\n
//              cle <identifiant de la cle de l'editeur pour laquelle c'est scelle>\n
//              <le sceau, base64 en lignes de 76>\n
//  Le sceau contient OnlineRequest (JSON).
struct OnlineRequest {
    int         version{1};
    std::string request;              // la demande d'activation (.xpgreq : son bloc signe par le PC, tel quel)
    std::string customer;             // le nom du client (la societe), donne par l'utilisateur
    std::string contact;              // facultatif : un courriel, un telephone
    std::string message;              // facultatif : un mot pour l'editeur
};
[[nodiscard]] std::string writeOnlineRequest(const OnlineRequest& r);
bool parseOnlineRequest(std::string_view json, OnlineRequest& out, std::string* why);
inline constexpr const char* kIssueMagic = "XPG-DEMANDE-EN-LIGNE 1";
[[nodiscard]] std::string issueTitle(std::string_view deviceId);   // "Demande d'activation XPG (poste 1a2b3c4d5e6f)"
[[nodiscard]] std::string issueBody(std::string_view deviceId, std::string_view publisherKeyId, std::string_view sealedBase64);
struct IssueBody {
    std::string device, keyId, sealed;
};
bool parseIssueBody(std::string_view body, IssueBody& out, std::string* why);

// ---- l'annulation (un commentaire de l'issue : un bloc << annulation >> signe par le PC) ----
inline constexpr const char* kCancelMagic = "XPG-ANNULATION 1";
struct Cancel {
    int         version{1};
    std::string device;               // l'identifiant du poste
    std::string requestNonce;         // la demande annulee
    Seconds     at{0};
};
[[nodiscard]] std::string writeCancel(const Cancel& c);
bool parseCancel(std::string_view json, Cancel& out, std::string* why);
// Le commentaire : kCancelMagic + "\n" + le bloc signe (writeBlock).
[[nodiscard]] std::string cancelComment(const SignedBlock& signedCancel);
bool parseCancelComment(std::string_view body, SignedBlock& out, std::string* why);

} // namespace licensing::gateway
