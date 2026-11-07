// =============================================================================
//  licensing/Gateway.hpp - les formats des demandes en ligne et de l'etat d'un
//  poste, echanges par le service des licences (docs/SERVICE-LICENCES.md) ;
//  aucun reseau ici
// -----------------------------------------------------------------------------
//  Nes en 1.34.0 ; depuis la 1.35.0, le service des licences (un Worker
//  Cloudflare, service/) les transporte tels quels, sans pouvoir les lire :
//
//    la DEMANDE EN LIGNE (POST /v1/demandes, lue par GET /admin/demandes) :
//      OnlineRequest (la demande d'activation signee par le PC, le client, le
//      contact, un message), scellee pour la cle de l'editeur - le service, ni
//      personne d'autre, ne lit le nom du client, du PC, ni le message ;
//
//    L'ETAT D'UN POSTE (PUT /admin/postes, lu par GET /v1/poste ou dans la
//    reponse de /v1/renouveler) : un bloc << poste >> signe par l'editeur
//      (DeviceStatus : recue, acceptee, refusee, annulee, mise a jour,
//      revoquee, retiree) ; son contenu (StatusContent : la licence, la raison
//      d'un refus, la liste de revocation) est scelle pour la cle du PC.
//
//  Le sceau : libsodium, une boite scellee vers la cle Ed25519 convertie en
//  X25519 (base64 en lignes de 76). Tout ce que lit un PC est signe par
//  l'editeur : un faux etat, une fausse licence sont refuses.
//
//  L'identifiant d'un poste est celui du service (licensing::service::deviceId,
//  Service.hpp : 32 hexadecimaux, le meme qu'en 1.34).
// =============================================================================
#pragma once

#include "KeyFile.hpp"
#include "SignedFile.hpp"
#include "Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace licensing::gateway {

// ---- sceller, ouvrir (le base64 d'un sceau, en lignes de 76) ----
[[nodiscard]] std::string sealFor(const crypto::PublicKey& key, std::string_view plain);   // vide : la cle est fausse
bool openWith(const SigningKey& key, std::string_view sealedBase64, std::string& plain, std::string* why);

// ---- l'etat d'un PC (un bloc << poste >> signe par l'editeur) ----
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
    std::string   device;             // l'identifiant du poste (service::deviceId)
    std::uint64_t number{0};          // croissant pour ce poste : un etat plus ancien est refuse
    Seconds       issuedAt{0};
    State         state{State::Received};
    std::string   requestNonce;       // la demande a laquelle il repond ("" : aucune)
    std::string   licenseId;          // la licence concernee ("" : aucune) ; l'editeur ne l'ecrit PLUS en clair depuis la
                                      // 1.34.0 (relecture) - elle est dans la licence scellee
    std::string   sealed;             // le contenu (StatusContent), scelle pour la cle du PC (base64) ; peut etre vide
};
[[nodiscard]] std::string writeDeviceStatus(const DeviceStatus& s);
bool parseDeviceStatus(std::string_view json, DeviceStatus& out, std::string* why);
// Ce que seul le PC lit.
struct StatusContent {
    std::string message;              // la raison d'un refus, un mot de l'editeur (texte libre, court)
    std::string license;              // le fichier de licence (.xpglic : ses blocs signes, tel quel) ; vide : aucun
    // la liste de revocation (.xpgrev, signee, telle quelle) - scellee pour le PC concerne, jamais transmise en clair
    // (ses raisons sont du texte libre) ; vide : aucune
    std::string revocations;
};
[[nodiscard]] std::string writeStatusContent(const StatusContent& c);
bool parseStatusContent(std::string_view json, StatusContent& out, std::string* why);

// ---- la demande en ligne (scellee pour la cle de l'editeur) ----
// Le client, le contact et le message : une ligne chacun, UTF-8 valide, sans caractere de controle (sinon la
// demande est refusee : ils s'affichent dans la console de l'editeur).
struct OnlineRequest {
    int         version{1};
    std::string request;              // la demande d'activation (.xpgreq : son bloc signe par le PC, tel quel)
    std::string customer;             // le nom du client (la societe), donne par l'utilisateur
    std::string contact;              // facultatif : un courriel, un telephone
    std::string message;              // facultatif : un mot pour l'editeur
};
[[nodiscard]] std::string writeOnlineRequest(const OnlineRequest& r);
bool parseOnlineRequest(std::string_view json, OnlineRequest& out, std::string* why);

} // namespace licensing::gateway
