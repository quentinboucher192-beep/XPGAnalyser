// =============================================================================
//  license_admin/Admin.hpp - 1.30.0 (prepare en 1.29.0) : l'outil de l'EDITEUR
// -----------------------------------------------------------------------------
//  CE CODE N'EST JAMAIS LIVRE A UN CLIENT : il n'entre que dans XPG-License-Admin
//  (cible xpg_license_admin) et dans l'essai licensing_test. Le controle du
//  paquet (outils/mingw/package_mingw.py) arrete la construction si le nom de
//  l'outil, sa marque (kAdminMarker) ou une cle privee se trouve dans le dossier
//  a livrer.
//
//  LA CLE DE L'EDITEUR : une paire Ed25519 creee sur le poste Admin
//  (createPublisherKey), la partie secrete protegee par DPAPI pour le compte
//  Windows de l'administrateur (sous Linux : fichier 0600). Elle ne sort jamais
//  du poste ; seule sa cle PUBLIQUE est exportee, en C++
//  (src/licensing/PublisherKey.hpp), et compilee dans les logiciels clients.
//
//  SIGNER : le registre (les editions et fonctionnalites, numerote), la liste de
//  revocation (numerotee), et une licence a partir d'une ou plusieurs demandes
//  d'activation (les PC) et d'un fichier de parametres. Le fichier de licence
//  rendu au client contient la licence, le registre (et la liste de revocation
//  si elle est donnee) : un seul fichier a importer.
// =============================================================================
#pragma once

#include "../licensing/Documents.hpp"
#include "../licensing/KeyFile.hpp"
#include "../licensing/SignedFile.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin {

namespace crypto = deploy::crypto;
using licensing::Seconds;
using licensing::SigningKey;

// La marque de l'outil Admin : dans l'exe, dans le fichier de la cle de l'editeur.
// Le controle du paquet la cherche (outils/mingw/package_mingw.py).
inline constexpr const char* kAdminMarker = "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER";
// La marque d'une cle d'ESSAI (dans son nom, donc dans PublisherKey.hpp et dans
// un programme qui l'embarque) : le controle du paquet la refuse aussi.
inline constexpr const char* kTestKeyMarker = "XPG-CLE-ESSAI-LICENCES";
inline constexpr const char* kPublisherKeyFile = "cle-editeur-licences.cle";

[[nodiscard]] const licensing::KeyFileSpec& publisherKeySpec();
// Windows : %LOCALAPPDATA%\XPG\LicenseAdmin ; ailleurs : $XDG_CONFIG_HOME/xpg-license-admin (ou ~/.config/...).
[[nodiscard]] std::string defaultKeyFolder();
[[nodiscard]] std::string publisherKeyPath(const std::string& folder);

// Creer la cle (refuse s'il y en a deja une). `test` : une cle d'ESSAI (son nom le dit, partout).
[[nodiscard]] std::unique_ptr<SigningKey> createPublisherKey(const std::string& folder, const std::string& name, bool test, std::string* why);
[[nodiscard]] std::unique_ptr<SigningKey> loadPublisherKey(const std::string& folder, std::string* why);

// ---- 1.44.0 : LA CLE D'UN ADMIN (docs/ADMINS.md, par. 1) ----------------------------------------------
//  Un admin qui n'est pas l'admin+ a SA cle (Ed25519), nee sur son PC, protegee comme celle de l'editeur (DPAPI
//  pour ce compte ; ailleurs un fichier 0600) ; elle n'en sort jamais. La liste des admins (signee par la cle de
//  l'editeur) la nomme, avec ses droits.
inline constexpr const char* kAdminKeyFile = "cle-admin.cle";
[[nodiscard]] const licensing::KeyFileSpec& adminKeySpec();
[[nodiscard]] std::string adminKeyPath(const std::string& folder);
// Creer la cle d'admin (refuse s'il y en a deja une) ; `name` : le nom de l'admin (<< Lea >>).
[[nodiscard]] std::unique_ptr<SigningKey> createAdminKey(const std::string& folder, const std::string& name, std::string* why);
[[nodiscard]] std::unique_ptr<SigningKey> loadAdminKey(const std::string& folder, std::string* why);

// ---- les cles publiques en C++ (exporter-cle) ----------------------------------------
//  src/licensing/PublisherKey.hpp      les VRAIES cles (kKeys) ; vide : aucune cle, les
//                                      licences ne sont pas actives (NotConfigured) ;
//  src/licensing/TestPublisherKey.hpp  les cles d'ESSAI (kTestKeys), compilees seulement
//                                      avec l'option CMake XPG_LICENCES_CLE_ESSAI.
//  Une cle d'essai ne va jamais dans PublisherKey.hpp, une vraie jamais dans
//  TestPublisherKey.hpp. Plusieurs cles (une rotation) : `keep` garde celles du fichier.
inline constexpr const char* kPublisherHeaderName = "PublisherKey.hpp";
inline constexpr const char* kTestPublisherHeaderName = "TestPublisherKey.hpp";

struct ExportedKey {
    std::string       id;      // keyIdOf(key)
    std::string       name;
    bool              test{false};
    crypto::PublicKey key{};
};
[[nodiscard]] ExportedKey exportedKeyOf(const SigningKey& key);

// Le C++ d'une liste de cles, toutes du genre du fichier (`test`).
[[nodiscard]] std::string publisherKeyHeader(const std::vector<ExportedKey>& keys, bool test, Seconds now);
// Le fichier pour cette seule cle (PublisherKey.hpp ; une cle d'essai : TestPublisherKey.hpp).
[[nodiscard]] std::string publisherKeyHeader(const SigningKey& key, Seconds now);
// Relire un fichier ecrit par publisherKeyHeader : ses cles et son genre (`test`). Chaque
// identifiant est verifie (celui de sa cle). Faux : `why`.
bool readPublisherKeyHeader(std::string_view text, std::vector<ExportedKey>& keys, bool& test, std::string* why);

// exporter-cle : le texte a ecrire dans `outName` (le nom du fichier de sortie, pour le
// controle du genre). `empty` : le fichier SANS cle (`key` ignore) ; sinon `key` est ajoutee,
// aux cles deja dans `existing` (le fichier actuel) si `keep`, seule sinon. `summary` : ce qui
// est ecrit, et les cles qui en sortent. Faux : `why`.
bool exportPublisherKeys(const SigningKey* key, std::string_view existing, const std::string& outName, bool keep, bool empty, Seconds now,
                         std::string& out, std::string* why, std::string* summary);

// Signer un registre ecrit a la main (JSON : number, unlicensed_edition,
// features, editions ; format, version et issued_at sont ajoutes s'ils manquent).
// `out` : le fichier .xpgreg. Faux : `why`.
bool signRegistry(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why);
// Signer une liste de revocation (JSON : number, revoked[] ; revoked_at manquant : maintenant).
// Chaque entree : license_id, reason, [device] (un seul PC), [revoked_at], et 1.32.0 [issued_before] (date ISO :
// seules les EMISSIONS de cette licence signees avant cette date sont revoquees - une emission precise, remplacee
// par une plus recente ; sans lui : toutes). Une liste qui l'emploie n'est lue que par les programmes 1.32.0 ou
// plus recents (les precedents la refusent : champ inconnu).
// `records` (facultatif) : une ligne du registre des activations par licence ou PC revoque.
struct ActivationRecord;
bool signRevocations(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why,
                     std::vector<ActivationRecord>* records = nullptr);

struct LicenseInputs {
    std::string              params;        // le JSON des parametres (license_id, customer, edition...)
    std::vector<std::string> requests;      // le contenu des demandes d'activation (.xpgreq)
    std::string              existing;      // facultatif : la licence a etendre (.xpglic : ses PC sont gardes)
    std::string              registry;      // le registre signe (.xpgreg), obligatoire
    std::string              revocations;   // facultatif : la liste signee (.xpgrev), jointe au fichier
    // 1.33.0 : les PC (leur cle, 64 hexadecimaux) a RETIRER de la licence etendue (`existing`) : un PC remplace ne
    // reste pas dans la nouvelle emission (sinon, revoque, il bloquerait toute nouvelle emission).
    std::vector<std::string> dropDevices;
    // 1.35.0 : une LICENCE EN LIGNE - l'adresse du service des licences (ServiceClient.hpp) ; vide : une licence liee a
    // des PC (comme avant). Une licence en ligne ne lie AUCUN PC : elle s'active par un code aupres de ce service
    // (son champ << service >> le nomme) ; pas de demande ; elle peut etre flottante (type floating). Etendre une licence
    // en ligne (existing) garde son service.
    std::string              service;
    // 1.42.0 : les OPTIONS de la licence et l'historique des ACHATS du client a y ecrire. nullopt : ceux de la licence
    // etendue (`existing`), sinon aucun. Une liste vide n'ecrit rien (le document reste lisible par la 1.41).
    std::optional<std::vector<licensing::LicenseOption>> options;
    std::optional<std::vector<licensing::Purchase>>      purchases;
    // 1.44.0 : le registre, les revocations et la licence a etendre peuvent etre signes par une AUTRE cle que celle qui
    // signe (un admin signe avec sa cle une licence dont le registre vient de l'editeur) : vrai si ce bloc est d'une
    // cle de confiance (l'editeur, un admin de la liste). Vide : la cle qui signe seulement (comme avant).
    std::function<bool(const licensing::SignedBlock&)> alsoTrusted;
};
// Signer une licence. `out` : le fichier .xpglic (licence + registre [+ revocations]) ;
// `summary` : ce qui a ete signe, en une ligne par fait. 1.32.0 (H2) : l'empreinte du poste de chaque demande
// (station) est recopiee dans le PC lie ; un PC deja lie qui redemande depuis une AUTRE installation de Windows
// (la meme cle, une autre empreinte : la cle recopiee) est refuse.
// `records` (facultatif) : une ligne du registre des activations par PC lie.
bool signLicense(const SigningKey& key, const LicenseInputs& in, Seconds now, std::string& out, std::string* why, std::string* summary,
                 std::vector<ActivationRecord>* records = nullptr);

// Lire un fichier signe et dire ce qu'il contient, et si chaque signature est
// bonne (celle de `key` pour licence, registre, revocations ; celle du PC pour une
// demande). Faux : un bloc est refuse.
bool describeFile(std::string_view bytes, const SigningKey* key, std::string& text);

// ---- 1.30.0 : LE REGISTRE DES ACTIVATIONS ------------------------------------------------------------
//  Sans service en ligne, l'editeur ne connait que les PC qui lui envoient une demande d'activation.
//  L'outil tient, dans le dossier de la cle (--dossier), activations.jsonl : une ligne JSON par
//  evenement, AJOUTEE A LA FIN (le fichier n'est jamais reecrit) :
//    "licence"     a chaque signer-licence, une ligne par PC lie (et "extension" quand --licence
//                  etendait une licence existante) ;
//    "demande"     enregistrer-demande : une demande recue, pas encore signee ;
//    "revocation"  a chaque signer-revocations, une ligne par licence (ou PC) revoque.
//  Rien du materiel : la demande n'en porte pas ; l'Admin ne voit que ce que les clients lui envoient.
//  Voir CHAQUE LANCEMENT du logiciel demandera le service de licences en ligne (docs/LICENCES.md, par. 12).
inline constexpr const char* kActivationsFile = "activations.jsonl";
[[nodiscard]] std::string activationsPath(const std::string& folder);

struct ActivationRecord {
    std::string kind;           // "licence", "demande", "revocation"
    Seconds     at{0};          // l'heure de l'enregistrement
    std::string licenseId, customer, edition, type;   // type : "perpetual", "temporary"...
    Seconds     expiresAt{0};   // 0 : sans fin
    std::string deviceKey;      // la cle publique du PC (64 hexadecimaux) ; vide : toute la licence (une revocation)
    std::string deviceName;     // le nom donne au PC par le client
    std::string fingerprint;    // l'empreinte courte et lisible de sa cle
    std::string software;       // le logiciel et sa version, tels que la demande les porte ("IHM Studio 1.30.0")
    Seconds     requestedAt{0}; // la date de la demande (0 : inconnue - un PC garde d'une licence etendue)
    Seconds     activatedAt{0};
    Seconds     signedAt{0};
    bool        extension{false};
    Seconds     revokedAt{0};
    std::string reason;
    Seconds     issuedAt{0};       // 1.32.0 : "licence" - l'emission de la licence signee
    Seconds     issuedBefore{0};   // 1.32.0 : "revocation" - seules les emissions d'avant (0 : toutes)
};

// Une demande d'activation recue (verifiee : signee par la cle du PC qu'elle porte) -> sa ligne "demande".
bool requestRecord(std::string_view requestBytes, Seconds now, ActivationRecord& out, std::string* why);
// Ajouter des lignes a la fin de activations.jsonl (le dossier est cree s'il le faut). Faux : `why`.
bool appendActivations(const std::string& folder, const std::vector<ActivationRecord>& records, std::string* why);
// Relire le registre (les lignes illisibles sont comptees dans `skipped` et ignorees). Absent : vide, vrai.
bool readActivations(const std::string& folder, std::vector<ActivationRecord>& out, int* skipped, std::string* why);

// Un PC connu : le dernier etat de chacun (par cle de PC), a la date `now`.
struct KnownDevice {
    std::string deviceKey, name, fingerprint, customer, licenseId, edition, software;
    Seconds     requestedAt{0}, activatedAt{0}, expiresAt{0}, revokedAt{0};
    std::string state;          // "demande", "active", "revoque", "expire"
};
[[nodiscard]] std::vector<KnownDevice> knownDevices(const std::vector<ActivationRecord>& records, Seconds now);
// Garder ceux d'une licence (`licenseId`, exact) et d'un client (`customer` : contenu, sans tenir compte de la casse).
[[nodiscard]] std::vector<KnownDevice> filterDevices(const std::vector<KnownDevice>& all, const std::string& licenseId, const std::string& customer);
// Le tableau (un PC par ligne, puis les totaux : PC, licences, clients) ; le meme en CSV (UTF-8 avec BOM,
// separateur ; et CRLF : pour Excel).
// 1.32.0 (M4) : dans le CSV, une cellule qui commence par = + - @ (tabulation, retour) prend une apostrophe devant :
// Excel la lit comme du texte, jamais comme une formule.
[[nodiscard]] std::string devicesTable(const std::vector<KnownDevice>& devices);
[[nodiscard]] std::string devicesCsv(const std::vector<KnownDevice>& devices);

} // namespace license_admin
