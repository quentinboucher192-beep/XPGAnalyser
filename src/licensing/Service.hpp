// =============================================================================
//  licensing/Service.hpp - 1.35.0 : LE SERVICE DES LICENCES (un Worker
//  Cloudflare, service/) - les formats partages ; aucun reseau ici
// -----------------------------------------------------------------------------
//  Le contrat complet : docs/SERVICE-LICENCES.md. Ici, ce que les Studios et
//  l'outil de l'editeur ont en commun :
//    - l'adresse du service (compilee : ServiceAddress.hpp) ;
//    - LES APPELS SIGNES : quatre en-tetes X-XPG-*, la signature Ed25519 d'un
//      message qui lie la methode, le chemin, l'heure, un nonce et l'empreinte
//      du corps (la cle du PC, ou celle de l'editeur) ;
//    - le code d'activation (XPG-XXXX-XXXX-XXXX-XXXX) et son empreinte ;
//    - l'empreinte de la machine (la Decouverte : un essai par machine) ;
//    - les documents signes par le service (activation, jeton, heure,
//      decouverte) et le certificat de service signe par l'editeur, avec leur
//      verification : un document du service ne vaut que signe par la cle que
//      nomme un certificat valide, signe par une cle de l'editeur que ce
//      programme croit.
// =============================================================================
#pragma once

#include "Documents.hpp"
#include "SignedFile.hpp"
#include "Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace licensing::service {

// ---- l'adresse ----
struct Address {
    std::string url;   // "https://xpg-licences.compte.workers.dev" (sans / final) ; vide : pas de service
    [[nodiscard]] bool empty() const noexcept { return url.empty(); }
};
// https://hote[:port] sans chemin, ou http:// vers ce poste seulement (127.0.0.1, localhost : les essais).
[[nodiscard]] bool validUrl(std::string_view url) noexcept;
// L'adresse compilee (ServiceAddress.hpp ; l'option CMake XPG_SERVICE_URL la remplace) ; vide : ce programme n'a pas
// de service (licences par fichier seulement). SEULEMENT dans une construction d'essai (XPG_LICENCES_CLE_ESSAI) : la
// variable d'environnement XPG_SERVICE la remplace (le service des essais, sur ce poste).
[[nodiscard]] Address compiledAddress();

// ---- les appels signes ----
inline constexpr const char* kHeaderKey = "X-XPG-Cle";
inline constexpr const char* kHeaderDate = "X-XPG-Date";
inline constexpr const char* kHeaderNonce = "X-XPG-Nonce";
inline constexpr const char* kHeaderSignature = "X-XPG-Signature";
// Le message signe : "XPG-SERVICE-1\n<METHODE>\n<chemin>\n<date>\n<nonce>\n<sha256 hexa du corps>".
[[nodiscard]] std::string requestMessage(std::string_view method, std::string_view pathAndQuery, std::string_view date,
                                         std::string_view nonce, std::string_view body);
// Les quatre en-tetes d'un appel signe (`now` : l'heure retenue - celle du service corrigee).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> signRequest(std::string_view method, std::string_view pathAndQuery,
                                                                           std::string_view body, const crypto::PublicKey& pub,
                                                                           const Signer& signer, Seconds now);
// La verification (les essais, un faux service) : la cle, la signature ; l'heure a +-600 s de `now`.
bool verifyRequest(std::string_view method, std::string_view pathAndQuery, std::string_view body, std::string_view keyHex,
                   std::string_view date, std::string_view nonce, std::string_view signatureHex, Seconds now, std::string* why);

// ---- l'identifiant d'un poste (le meme qu'en 1.34) ----
[[nodiscard]] std::string deviceId(const crypto::PublicKey& deviceKey);   // 32 hexadecimaux
[[nodiscard]] bool isDeviceId(std::string_view s) noexcept;
[[nodiscard]] bool isKeyHex(std::string_view s) noexcept;                 // 64 hexadecimaux minuscules

// ---- le code d'activation ----
// Un code neuf, tire au hasard (80 bits) : "XPG-7K3M-9QPT-2WXD-H8NB".
[[nodiscard]] std::string newActivationCode();
// Le relire avec souplesse (minuscules, espaces, tirets, le prefixe XPG facultatif, O pour 0, I et L pour 1) : les
// 16 caracteres normalises. Faux : pas un code.
bool normalizeCode(std::string_view text, std::string& normalized);
[[nodiscard]] std::string formatCode(std::string_view normalized);         // "XPG-XXXX-XXXX-XXXX-XXXX"
[[nodiscard]] std::string codeHash(std::string_view normalized);           // 64 hexadecimaux (ce que garde le service)

// ---- l'empreinte de la machine (la Decouverte) ----
// Les 32 premiers hexadecimaux de SHA-256("xpg-decouverte/1|" + identifiant de l'installation) ; vide si l'identifiant
// est vide.
[[nodiscard]] std::string machineHash(std::string_view machineId);

// ---- les documents ----
inline constexpr const char* kRightActivation = "activation";
inline constexpr const char* kRightFloating = "flottante";
inline constexpr const char* kRightTime = "heure";
inline constexpr const char* kRightTrial = "decouverte";

// Le certificat de service (genre "service", signe par l'EDITEUR).
struct ServiceCert {
    int                      version{1};
    std::string              key;        // la cle publique du service, 64 hexadecimaux
    std::string              url;        // l'adresse du service
    Seconds                  issuedAt{0};
    Seconds                  expiresAt{0};
    std::vector<std::string> rights;     // kRight*
    [[nodiscard]] bool grants(std::string_view right) const;
};
[[nodiscard]] std::string writeCert(const ServiceCert& c);
bool parseCert(std::string_view json, ServiceCert& out, std::string* why);

// L'activation d'un PC (genre "activation", signee par le SERVICE).
struct Activation {
    int           version{1};
    std::string   licenseId;
    Seconds       licenseIssuedAt{0};   // l'emission de la licence activee ("emission")
    std::string   deviceKey;            // 64 hexadecimaux
    std::string   station;              // 32 hexadecimaux (stationHash) ; vide : aucune
    bool          floating{false};      // "type" : "flottante" / "fixe"
    std::uint64_t number{0};
    Seconds       issuedAt{0};
    Seconds       until{0};             // "jusqu_a" : la fin du bail
};
[[nodiscard]] std::string writeActivation(const Activation& a);
bool parseActivation(std::string_view json, Activation& out, std::string* why);

// Le jeton d'une licence flottante (genre "jeton", signe par le SERVICE).
struct Lease {
    int         version{1};
    std::string licenseId;
    std::string deviceKey;
    Seconds     issuedAt{0};
    Seconds     until{0};
};
[[nodiscard]] std::string writeLease(const Lease& l);
bool parseLease(std::string_view json, Lease& out, std::string* why);

// L'heure signee (genre "heure", signee par le SERVICE) ; `nonce` : celui du client (la fraicheur).
struct SignedTime {
    int         version{1};
    Seconds     time{0};
    std::string nonce;
};
[[nodiscard]] std::string writeTime(const SignedTime& t);
bool parseTime(std::string_view json, SignedTime& out, std::string* why);

// La Decouverte d'une machine (genre "decouverte", signee par le SERVICE).
struct Trial {
    int         version{1};
    std::string deviceKey;
    std::string machine;     // machineHash
    Seconds     start{0};    // "debut" : la premiere fois que le service a vu cette machine
    int         days{30};
};
[[nodiscard]] std::string writeTrial(const Trial& t);
bool parseTrial(std::string_view json, Trial& out, std::string* why);

// ---- verifier ----
// Le certificat : un seul bloc "service", signe par une de `publisherKeys`, pas expire a `now`. `now` = 0 : la date
// n'est pas regardee.
bool verifyCert(std::string_view certText, const std::vector<crypto::PublicKey>& publisherKeys, Seconds now, ServiceCert& out,
                std::string* why);
// Un bloc du service : signe par la cle du certificat, du genre attendu, et le certificat donne le droit `right`.
bool verifyServiceBlock(const SignedBlock& block, DocKind kind, const ServiceCert& cert, std::string_view right, std::string* why);
// Le texte d'un fichier de un ou plusieurs blocs : le premier du genre `kind` (faux s'il n'y en a pas).
bool findBlock(std::string_view fileText, DocKind kind, SignedBlock& out, std::string* why);

} // namespace licensing::service
