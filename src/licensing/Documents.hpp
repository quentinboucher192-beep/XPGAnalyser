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
//
//  1.42.0 - LES OFFRES TEMPORAIRES, LES OPTIONS, LES ACHATS (facultatifs, ecrits
//  seulement s'ils servent : tant que l'editeur ne s'en sert pas, le registre et
//  les licences restent ceux de la 1.41, lisibles par elle) :
//    - registre, une edition : "sale_from" / "sale_until" - en vente pendant une
//      periode (la boutique des Studios ne la montre qu'alors) ;
//    - registre : "options" - le catalogue des options (des fonctionnalites
//      vendues a part, pour un temps : leurs durees et leurs prix) ;
//    - licence : "options" - les options de CETTE licence (leurs fonctionnalites,
//      du ... au ...) : ouvertes en plus de celles de la licence pendant leur
//      periode ; "purchases" - l'historique des achats du client (Mes achats).
//  Un programme d'avant la 1.42.0 refuse un document qui les porte : l'Admin le
//  dit avant le premier usage (mettre les Studios des clients en 1.42).
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
    // 1.37.0 (facultatifs, ecrits seulement s'ils sont remplis) : la << boutique >> des Studios - le prix tel que
    // l'editeur l'ecrit ("490 \xE2\x82\xAC HT / an", 60 caracteres au plus) et une phrase (200 au plus).
    std::string               price;
    std::string               description;
    // 1.38.0 (facultatifs) : une reduction - le nouveau prix ("390 \xE2\x82\xAC HT / an", 60 au plus) et sa fin (0 : aucune ;
    // passee, la carte de la boutique reprend le prix normal). Le prix reduit sans date : refuse.
    std::string               promoPrice;
    Seconds                   promoUntil{0};
    // 1.42.0 (facultatifs) : une offre TEMPORAIRE - en vente du saleFrom (compris) au saleUntil (exclu) ; 0 : sans debut,
    // sans fin. Hors de cette periode, la boutique des Studios ne la montre pas ; les licences deja vendues vont jusqu'a
    // leur fin (la periode de vente ne touche jamais une licence).
    Seconds                   saleFrom{0};
    Seconds                   saleUntil{0};
    // En vente a cette heure (pas retiree, dans sa periode).
    [[nodiscard]] bool onSale(Seconds now) const noexcept {
        return !retired && (saleFrom == 0 || now >= saleFrom) && (saleUntil == 0 || now < saleUntil);
    }
    [[nodiscard]] bool temporary() const noexcept { return saleFrom != 0 || saleUntil != 0; }
};
// 1.42.0 : une OPTION du catalogue - des fonctionnalites vendues a part, pour un temps, a ajouter a une licence dont l'offre
// ne les a pas. Ses durees : 7 jours, 30 jours... ou 0 (jusqu'a la fin de la licence), chacune avec son prix (un texte,
// comme celui d'une offre : "19 \xE2\x82\xAC HT").
struct OptionDuration {
    int         days{0};      // 1..3660 ; 0 : jusqu'a la fin de la licence
    std::string price;        // 60 au plus ; vide : sur demande
    bool operator==(const OptionDuration&) const = default;
};
struct OptionDef {
    std::string                 id;            // "exports_avances" : [a-z0-9_], 64 au plus, commence par une lettre
    std::string                 label;         // "Exports avanc\xC3\xA9s" (200 au plus)
    std::string                 description;   // une phrase (200 au plus, facultative)
    std::vector<FeatureGrant>   features;      // ce qu'elle ouvre (au moins une, declarees)
    std::vector<std::string>    editions;      // les offres a qui elle est proposee ; vide : toutes
    std::vector<OptionDuration> durations;     // 1 a 8
    int                         trialDays{0};  // 0 : pas d'essai ; sinon : ces jours offerts, une fois par client
    bool                        shop{true};    // montree dans la boutique des Studios (Licence > Options)
    bool                        retired{false};
    bool operator==(const OptionDef&) const = default;
    // Proposee a cette offre ?
    [[nodiscard]] bool offeredTo(std::string_view edition) const noexcept;
};
// 1.38.0 : une annonce de l'editeur (<< Prevenir mes clients >>) - une notification dans les Studios, jamais un mail. Elle
// voyage dans le registre signe : un client hors connexion la recoit a sa prochaine connexion, une seule fois (son id).
struct Announcement {
    std::string id;        // "A-20261108-1" : [A-Za-z0-9_-], 40 au plus, unique dans le registre
    std::string title;     // une ligne (120 au plus)
    std::string text;      // le detail (600 au plus)
    std::string edition;   // l'offre concernee (facultatif) : la notification ouvre la boutique
    Seconds     at{0};     // quand l'editeur l'a ecrite
    Seconds     until{0};  // apres : plus montree (0 : sans fin)
    bool operator==(const Announcement&) const = default;
};
struct Registry {
    int                     version{kFormatVersion};
    std::uint64_t           number{0};               // croissant : 318, 319...
    Seconds                 issuedAt{0};
    std::string             unlicensedEdition;       // l'edition sans licence : "FREE"
    std::vector<FeatureDef> features;
    std::vector<EditionDef> editions;
    std::vector<Announcement> announcements;         // 1.38.0 (facultatif, 20 au plus ; ecrit seulement s'il y en a)
    std::vector<OptionDef>  options;                 // 1.42.0 (facultatif, 64 au plus ; ecrit seulement s'il y en a)

    [[nodiscard]] const FeatureDef* feature(std::string_view id) const noexcept;
    [[nodiscard]] const EditionDef* edition(std::string_view id) const noexcept;
    [[nodiscard]] const OptionDef* option(std::string_view id) const noexcept;   // 1.42.0
    // 1.42.0 : porte-t-il un champ de la 1.42 (une offre temporaire, des options) ? Un Studio d'avant ne le lit plus.
    [[nodiscard]] bool usesFormat142() const noexcept;
};

// ---- la licence -------------------------------------------------------------------
struct BoundDevice {
    std::string key;          // la cle publique Ed25519 du PC, 64 hexadecimaux minuscules
    std::string name;         // le nom que le client a donne au PC (facultatif) ; jamais son nom reseau
    Seconds     activatedAt{0};
    std::string station;      // 1.32.0 : facultatif, 32 hexadecimaux (stationHash) ; vide : pas controle
    bool operator==(const BoundDevice&) const = default;
};
// 1.42.0 : une option ajoutee a CETTE licence : ses fonctionnalites s'ouvrent en plus de celles de la licence, du `from`
// (compris) au `until` (exclu ; 0 : jusqu'a la fin de la licence). Copiees a l'ajout (changer le catalogue ne touche pas
// une option deja vendue). Une option finie reste dans la licence (son historique) jusqu'a ce que l'Admin l'en retire.
struct LicenseOption {
    std::string               id;          // l'option du catalogue ([a-z0-9_])
    std::string               label;       // son nom a l'ajout
    std::vector<FeatureGrant> features;    // triees
    Seconds                   from{0};
    Seconds                   until{0};    // 0 : jusqu'a la fin de la licence
    std::string               price;       // "19 \xE2\x82\xAC HT" (60 au plus) ; vide : rien a dire
    bool                      gift{false}; // offerte (un geste, un essai)
    bool operator==(const LicenseOption&) const = default;
    // Ouverte a cette heure (`licenseEnd` : la fin de la licence, 0 : jamais) ?
    [[nodiscard]] bool activeAt(Seconds now, Seconds licenseEnd) const noexcept {
        const Seconds end = until ? until : licenseEnd;
        return now >= from && (end == 0 || now < end);
    }
};
// 1.42.0 : un achat de l'historique du client (Mes achats) : une offre, une prolongation, une option.
struct Purchase {
    Seconds     at{0};          // quand l'editeur l'a signe
    std::string kind;           // "offer", "extension", "option"
    std::string label;          // "PROFESSIONAL \xC2\xB7 1 an", "Exports avanc\xC3\xA9s \xC2\xB7 1 semaine" (120 au plus)
    std::string licenseId;      // la licence (un client a pu en avoir plusieurs)
    Seconds     from{0};
    Seconds     until{0};       // 0 : sans fin (ou jusqu'a la fin de la licence pour une option)
    std::string price;          // 60 au plus ; vide : rien a dire ; "offerte"...
    bool operator==(const Purchase&) const = default;
};
[[nodiscard]] bool isPurchaseKind(std::string_view k) noexcept;

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
    std::vector<LicenseOption> options;         // 1.42.0 (facultatif, 64 au plus)
    std::vector<Purchase>     purchases;        // 1.42.0 (facultatif, 400 au plus) : l'historique des achats du client

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

// 1.42.0 : les fonctionnalites OUVERTES a cette heure : celles de la licence, plus celles de ses options en cours
// (une fonctionnalite deja dans la licence garde ses limites). Triees.
[[nodiscard]] std::vector<FeatureGrant> effectiveGrants(const License& license, Seconds now);

// La licence tient-elle avec ce registre ? Son edition et chacune de ses
// fonctionnalites y sont declarees, et chaque limite aussi. Faux : `why` (avec
// << importe un registre plus recent >> quand la licence vient d'un registre
// plus recent que celui-ci).
bool checkAgainstRegistry(const License& license, const Registry& registry, std::string* why);

// Des aides partagees avec l'Admin.
[[nodiscard]] bool isFeatureId(std::string_view s) noexcept;      // [a-z0-9_]{1,64}, commence par une lettre
[[nodiscard]] bool isEditionId(std::string_view s) noexcept;      // [A-Z0-9_]{1,32}, commence par une lettre
[[nodiscard]] bool isAnnouncementId(std::string_view s) noexcept; // 1.38.0 : [A-Za-z0-9_-]{1,40}
[[nodiscard]] bool isLicenseId(std::string_view s) noexcept;      // [A-Za-z0-9._-]{1,64}
[[nodiscard]] bool isKeyHex(std::string_view s) noexcept;         // 64 hexadecimaux minuscules
[[nodiscard]] bool isStationHex(std::string_view s) noexcept;     // 1.32.0 : 32 hexadecimaux minuscules
// 1.32.0 : un texte qu'un tableur prendrait pour une formule (= + - @ tabulation ou retour en tete).
[[nodiscard]] bool looksLikeFormula(std::string_view s) noexcept;
[[nodiscard]] std::string newNonce();                             // 32 hexadecimaux tires au hasard
// Trier et dedoublonner des fonctionnalites (par identifiant) et leurs limites (par nom).
void normalizeGrants(std::vector<FeatureGrant>& grants);

} // namespace licensing
