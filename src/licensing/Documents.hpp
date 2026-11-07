// =============================================================================
//  licensing/Documents.hpp - 1.30.0 (prepare en 1.29.0) : les documents signes
// -----------------------------------------------------------------------------
//  Quatre documents JSON, chacun dans un bloc signe (SignedFile.hpp) :
//
//    le REGISTRE (xpg-registry)       les fonctionnalites et les editions, numerote
//                                     (le << manifeste n. 318 >> de la maquette) ;
//                                     signe par l'editeur ; un client refuse un
//                                     registre plus ancien que celui qu'il a vu
//    la LICENCE (xpg-license)         un client, une edition, SES fonctionnalites
//                                     (copiees a l'emission : changer une edition
//                                     ne touche pas les licences signees), le
//                                     nombre de PC, le type, les dates, les PC lies
//                                     (leurs cles publiques) ; signee par l'editeur
//    les REVOCATIONS (xpg-revocations) les licences retirees (ou un seul PC d'une
//                                     licence : un transfert), numerotee, signee
//    la DEMANDE D'ACTIVATION          la cle publique du PC (et un nom choisi par
//      (xpg-activation-request)       l'utilisateur, facultatif) ; signee PAR LE PC
//
//  LA LECTURE EST STRICTE : JSON bien forme, un objet, aucune cle en double (a
//  aucun niveau), aucune cle inconnue (un champ qui restreindrait une licence ne
//  peut pas etre ignore en silence : un champ nouveau = une version nouvelle du
//  format), chaque champ du bon genre et dans ses bornes. Une version de format
//  plus recente est refusee avec << mets a jour >>.
//
//  L'ECRITURE est celle de l'Admin : toujours le meme texte pour les memes
//  valeurs (l'ordre des cles fixe, deux espaces, des \n, UTF-8). Le verifieur ne
//  reecrit jamais : il verifie les octets recus (voir SignedFile.hpp).
//
//  1.35.0 - LA LICENCE EN LIGNE (le service des licences, Service.hpp,
//  docs/SERVICE-LICENCES.md) : le champ << service >> = l'adresse du service
//  (service::validUrl) et AUCUN PC lie (devices vide) ; max_devices est alors
//  le nombre de postes (d'activations) que le service accorde. Le type
//  << floating >> (licence flottante) n'existe qu'en ligne. Les regles :
//  service vide -> au moins un PC lie (comme avant) ; service donne -> aucun PC
//  (un melange est refuse) ; floating sans service -> refuse. Sur un PC, une
//  licence en ligne ne vaut qu'avec l'activation de ce PC signee par le service
//  (activation.xpgact, Store.hpp) : elle ne lie jamais un PC par elle-meme.
//
//  1.32.0 - TROIS CHAMPS FACULTATIFS (ecrits seulement s'ils servent ; un
//  document sans eux reste lisible par la 1.30 et la 1.31) :
//    - "station" (demande, PC lie d'une licence) : l'empreinte salee de
//      l'installation de Windows (stationHash, KeyFile.hpp) - le fil d'alarme
//      d'une cle de PC recopiee ailleurs ;
//    - "issued_before" (revocation) : seules les emissions de la licence
//      signees AVANT cette date sont revoquees (une emission precise).
//  Un programme d'avant la 1.32.0 refuse un document qui les porte (champ
//  inconnu) : c'est voulu (un champ qui restreint ne s'ignore pas).
// =============================================================================
#pragma once

#include "Time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace licensing {

inline constexpr int kFormatVersion = 1;
inline constexpr std::string_view kLicenseFormat = "xpg-license";
inline constexpr std::string_view kRegistryFormat = "xpg-registry";
inline constexpr std::string_view kRevocationsFormat = "xpg-revocations";
inline constexpr std::string_view kRequestFormat = "xpg-activation-request";

// ---- les genres de licence ----------------------------------------------------
enum class LicenseType : std::uint8_t {
    Perpetual,    // "perpetual"   sans fin
    Temporary,    // "temporary"   jusqu'a expires_at, puis la grace
    Evaluation,   // "evaluation"  jusqu'a expires_at, sans grace
    Floating,     // "floating"    1.35.0 : en ligne seulement - un jeton pris aupres du service (jeton.xpgjet)
};
[[nodiscard]] const char* licenseTypeKey(LicenseType) noexcept;
// "perp\xC3\xA9tuelle", "temporaire", "\xC3\xA9valuation", "flottante" (pour l'affichage).
[[nodiscard]] std::string licenseTypeLabel(LicenseType);
[[nodiscard]] bool parseLicenseType(std::string_view key, LicenseType& out) noexcept;

// ---- une fonctionnalite accordee, et ses limites --------------------------------
// Une limite est un entier nomme ("max_views": 50). Son nom est declare dans le
// registre, avec la fonctionnalite.
using Limits = std::vector<std::pair<std::string, std::int64_t>>;   // triees par nom
struct FeatureGrant {
    std::string id;       // "ai_generation"
    Limits      limits;
    bool operator==(const FeatureGrant&) const = default;
};

// ---- le registre ----------------------------------------------------------------
struct LimitDef {
    std::string id;       // "max_views"
    std::string label;    // "Vues au plus"
};
struct FeatureDef {
    std::string           id;        // "ai_generation" : [a-z0-9_], 64 au plus
    std::string           product;   // "API Studio", "IHM Studio", "IA", "Autres"
    std::string           label;     // "G\xC3\xA9n\xC3\xA9ration"
    std::vector<LimitDef> limits;
    bool                  retired{false};   // retiree : connue des clients, plus accordee par l'Admin
};
struct EditionDef {
    std::string               id;              // "PROFESSIONAL" : [A-Z0-9_], 32 au plus
    std::string               label;
    LicenseType               type{LicenseType::Temporary};   // les valeurs par defaut d'une licence neuve (l'Admin)
    int                       maxDevices{1};
    int                       durationDays{0};                // 0 : sans fin
    int                       offlineGraceDays{0};
    std::vector<FeatureGrant> features;
    bool                      retired{false};
};
struct Registry {
    int                     version{kFormatVersion};
    std::uint64_t           number{0};               // croissant : 318, 319...
    Seconds                 issuedAt{0};
    std::string             unlicensedEdition;       // l'edition sans licence : "FREE"
    std::vector<FeatureDef> features;
    std::vector<EditionDef> editions;

    [[nodiscard]] const FeatureDef* feature(std::string_view id) const noexcept;
    [[nodiscard]] const EditionDef* edition(std::string_view id) const noexcept;
};

// ---- la licence -------------------------------------------------------------------
struct BoundDevice {
    std::string key;          // la cle publique Ed25519 du PC, 64 hexadecimaux minuscules
    std::string name;         // le nom que le client a donne au PC (facultatif) ; jamais son nom reseau
    Seconds     activatedAt{0};
    std::string station;      // 1.32.0 : facultatif, 32 hexadecimaux (stationHash) ; vide : pas controle
    bool operator==(const BoundDevice&) const = default;
};
struct License {
    int                       version{kFormatVersion};
    std::string               licenseId;        // "XPG-2026-00042"
    std::string               customer;         // le nom du client
    std::string               edition;          // "PROFESSIONAL"
    LicenseType               type{LicenseType::Temporary};
    std::string               service;          // 1.35.0 : l'adresse du service (une licence EN LIGNE, sans PC lie) ; vide : liee par l'editeur
    int                       maxDevices{1};
    std::vector<FeatureGrant> features;         // triees par identifiant
    Seconds                   issuedAt{0};
    Seconds                   expiresAt{0};     // 0 : jamais (perpetuelle)
    int                       offlineGraceDays{0};
    std::vector<BoundDevice>  devices;
    std::uint64_t             manifest{0};      // le numero du registre en vigueur a l'emission
    std::string               nonce;            // 32 hexadecimaux : deux emissions ne se ressemblent jamais

    [[nodiscard]] const FeatureGrant* grant(std::string_view id) const noexcept;
    [[nodiscard]] bool boundTo(std::string_view deviceKeyHex) const noexcept;
    // 1.32.0 : l'entree de ce PC (nullptr : pas lie).
    [[nodiscard]] const BoundDevice* device(std::string_view deviceKeyHex) const noexcept;
};

// ---- les revocations ----------------------------------------------------------------
struct Revocation {
    std::string licenseId;
    std::string device;       // vide : toute la licence ; sinon la cle d'un seul PC (un transfert)
    Seconds     revokedAt{0};
    std::string reason;       // "Fin de contrat"
    Seconds     issuedBefore{0};   // 1.32.0 : 0 : toutes les emissions ; sinon celles signees avant (issued_at < issuedBefore)
};
struct RevocationList {
    int                     version{kFormatVersion};
    std::uint64_t           number{0};
    Seconds                 issuedAt{0};
    std::vector<Revocation> revoked;

    // La revocation qui touche cette emission de la licence (signee a `licenseIssuedAt`) sur ce PC, ou nullptr.
    [[nodiscard]] const Revocation* find(std::string_view licenseId, std::string_view deviceKeyHex, Seconds licenseIssuedAt) const noexcept;
};

// ---- la demande d'activation -----------------------------------------------------------
struct ActivationRequest {
    int         version{kFormatVersion};
    std::string deviceKey;    // 64 hexadecimaux
    std::string deviceName;   // facultatif, donne par l'utilisateur ; 1.32.0 : jamais = + - @ en tete (un tableur)
    std::string licenseId;    // facultatif : la licence demandee (celle recue par courriel)
    std::string software;     // facultatif : "IHM Studio 1.30.0"
    Seconds     createdAt{0};
    std::string nonce;
    std::string station;      // 1.32.0 : facultatif, 32 hexadecimaux (stationHash) ; l'Admin le recopie dans la licence
};

// ---- ecrire (l'Admin, la demande du PC) et lire (strictement) -----------------------------
[[nodiscard]] std::string writeRegistry(const Registry&);
[[nodiscard]] std::string writeLicense(const License&);
[[nodiscard]] std::string writeRevocations(const RevocationList&);
[[nodiscard]] std::string writeRequest(const ActivationRequest&);

bool parseRegistry(std::string_view json, Registry& out, std::string* why);
bool parseLicense(std::string_view json, License& out, std::string* why);
bool parseRevocations(std::string_view json, RevocationList& out, std::string* why);
bool parseRequest(std::string_view json, ActivationRequest& out, std::string* why);

// La licence tient-elle avec ce registre ? Son edition et chacune de ses
// fonctionnalites y sont declarees, et chaque limite aussi. Faux : `why` (avec
// << importe un registre plus recent >> quand la licence vient d'un registre
// plus recent que celui-ci).
bool checkAgainstRegistry(const License& license, const Registry& registry, std::string* why);

// Des aides partagees avec l'Admin.
[[nodiscard]] bool isFeatureId(std::string_view s) noexcept;      // [a-z0-9_]{1,64}, commence par une lettre
[[nodiscard]] bool isEditionId(std::string_view s) noexcept;      // [A-Z0-9_]{1,32}, commence par une lettre
[[nodiscard]] bool isLicenseId(std::string_view s) noexcept;      // [A-Za-z0-9._-]{1,64}
[[nodiscard]] bool isKeyHex(std::string_view s) noexcept;         // 64 hexadecimaux minuscules
[[nodiscard]] bool isStationHex(std::string_view s) noexcept;     // 1.32.0 : 32 hexadecimaux minuscules
// 1.32.0 : un texte qu'un tableur prendrait pour une formule (= + - @ tabulation ou retour en tete).
[[nodiscard]] bool looksLikeFormula(std::string_view s) noexcept;
[[nodiscard]] std::string newNonce();                             // 32 hexadecimaux tires au hasard
// Trier et dedoublonner des fonctionnalites (par identifiant) et leurs limites (par nom).
void normalizeGrants(std::vector<FeatureGrant>& grants);

} // namespace licensing
