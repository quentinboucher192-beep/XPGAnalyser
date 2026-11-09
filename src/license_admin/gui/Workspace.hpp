// =============================================================================
//  license_admin/gui/Workspace.hpp - 1.33.0 : LE POSTE ADMIN, VU PAR L'APPLICATION
//  XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Ce que les ecrans de l'application lisent et font, sans aucun ecran : la cle
//  de l'editeur, l'archive (le registre, la liste de revocation, chaque licence),
//  le registre des activations, le service des licences. Chaque operation rend
//  un OpResult (fait ou refuse, en francais, et ce qui a ete ecrit) ; les ecrans
//  le montrent tel quel. Les memes fonctions que la console (Admin.hpp,
//  ArchiveSync.hpp, ServiceClient.hpp, KeyBackup.hpp) : un fichier signe par
//  l'application est identique a celui de la console.
//
//  Le dossier : celui de la cle (defaultKeyFolder : %LOCALAPPDATA%\XPG\LicenseAdmin),
//  le meme que XPG-License-Admin.exe - les deux outils voient la meme cle, la
//  meme archive, le meme registre des activations.
//
//  LES SECRETS (phrase de passe, code de secours, code d'activation) passent par
//  les arguments des fonctions et ne sont jamais gardes ni ecrits par le
//  Workspace. Le code d'activation d'une licence en ligne : tire par
//  prepareOnlineLicence, rendu a l'appelant (montre une fois), seule son
//  empreinte part au service.
//
//  LENT : backupKey / checkBackup / restoreKey (Argon2id, quelques secondes) et
//  tout ce qui parle au service (service*, archiveSync) - a lancer par
//  Background / runSlow, jamais sur le fil de l'interface. Ces fonctions-la sont
//  const : elles relisent la cle et les fichiers, jamais l'etat du Workspace ; ce
//  qu'elles rapportent (l'etat du service, le releve des licences en ligne) est
//  adopte ensuite sur le fil de l'interface (adoptServiceState, adoptSnapshot).
//  Le reste lit ou ecrit de petits fichiers.
//
//  1.35.0 : LE SERVICE DES LICENCES (ServiceClient.hpp ; plus aucun GitHub) :
//  chaque operation qui signe, le service regle, laisse un ServiceJob
//  (takeServiceJob : une licence liee a ses PC, une revocation, un PC retire, un
//  registre, une licence en ligne prolongee) que l'application lance par runSlow
//  (publishToService, Page.hpp).
//
//  1.44.0 : PLUSIEURS ADMINS (docs/ADMINS.md). Le poste a un ROLE : l'admin+ (la
//  cle de l'editeur), un admin (sa cle, cle-admin.cle, nommee dans la liste des
//  admins signee par l'editeur, avec ses DROITS), en attente (sa cle n'est pas
//  encore dans la liste), retire, ou rien. Tout ce qui signe passe par signer() :
//  la cle de l'editeur pour l'admin+, celle de l'admin sinon ; une operation sans
//  le droit qu'il faut est refusee (whyNot : la phrase du bouton grise). Chaque
//  operation ecrit sa ligne au JOURNAL DES ADMINS (journal : signee, gardee dans
//  journal-a-envoyer.jsonl, envoyee par le prochain appel au service). L'admin+
//  gere les admins (accepter, retirer, les droits, les profils), releve et
//  verifie le journal (la chaine), regle sa criticite. Le code de ces fonctions :
//  WorkspaceAdmins.cpp.
// =============================================================================
#pragma once

#include "../Admin.hpp"
#include "../ArchiveSync.hpp"
#include "../KeyBackup.hpp"
#include "../ServiceClient.hpp"
#include "../../core/Version.hpp"
#include "../../licensing/Admins.hpp"
#include "../../licensing/Documents.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace license_admin::gui {

using licensing::Seconds;

inline constexpr const char* kAppName = "XPG License Manager Admin";
// 1.33.0 : la premiere application (1.0.0) ; 1.35.0 : le service des licences (1.1.0) ; 1.40.0 : la version des Studios (son
// installateur la porte : une seule version a dire)
inline constexpr const char* kAppVersion = XPG_ANALYZER_VERSION;

// ---- le resultat d'une operation : montre tel quel ----
struct OpResult {
    bool                     ok{false};
    std::string              message;     // la phrase : "Licence XPG-2026-00042 sign\xC3\xA9" "e." ou "Refus\xC3\xA9 : ..."
    std::vector<std::string> details;     // le detail (ce qui a ete signe, range, note, les avertissements)
    std::string              output;      // le fichier ecrit (s'il y en a un)
    [[nodiscard]] static OpResult fail(std::string why);
    [[nodiscard]] static OpResult done(std::string message);
    [[nodiscard]] std::string text() const;   // message puis details, une ligne chacun
};

// ---- la cle de l'editeur ----
struct KeyStatus {
    bool        present{false};
    bool        test{false};        // une cle d'ESSAI (jamais pour des clients)
    bool        protectedHere{false};   // DPAPI (Windows) ; sinon un fichier 0600
    std::string id, name, fingerprint, path;
    std::string problem;            // absente ou illisible : pourquoi
};

// ---- une licence de l'archive : sa derniere emission signee ----
struct LicenceEntry {
    std::string        path;        // archive/licences/<ID>.xpglic
    licensing::License licence;
    bool               revoked{false};      // toute la licence (cette emission)
    bool               expired{false};
    int                revokedDevices{0};   // des PC revoques un par un
    [[nodiscard]] std::string stateLabel() const;   // "active", "expir\xC3\xA9" "e", "r\xC3\xA9voqu\xC3\xA9" "e", "active (1 PC r\xC3\xA9voqu\xC3\xA9)"
};

// ---- un client : ce que disent ses licences et le registre des activations ----
struct ClientEntry {
    std::string              name;
    std::vector<std::string> licenceIds;
    int                      activeLicences{0};
    int                      devices{0};          // PC lies, toutes licences
    int                      pendingRequests{0};  // demandes recues, pas encore signees
    Seconds                  lastActivity{0};
};

// ---- les chiffres du tableau de bord ----
struct Dashboard {
    int clients{0};
    int licences{0}, activeLicences{0}, expiredLicences{0}, revokedLicences{0};
    int expiringSoon{0};                // actives qui finissent dans les 30 jours
    int devices{0};                     // PC actives (dernier etat "active")
    int pendingRequests{0};             // demandes recues pas encore signees
    // 1.35.0 : le service des licences, au dernier releve (0 : jamais releve sur ce poste).
    bool    serviceConfigured{false};
    int     onlineLicences{0};          // les licences en ligne (le service)
    int     onlineDevices{0};           // les PC actives en ligne
    int     floatingLeases{0};          // les jetons flottants en cours
    int     floatingSeats{0};           // les postes des licences flottantes (au total)
    int     onlinePending{0};           // les demandes manuelles qui attendent ta decision
    Seconds snapshotAt{0};              // le releve des licences en ligne
    Seconds onlineFetchedAt{0};         // le releve des demandes
    std::vector<std::pair<std::string, int>> byEdition;   // edition -> licences actives
    std::vector<ActivationRecord>            recent;      // les dernieres operations (les plus recentes d'abord, 12 au plus)
};

// ---- 1.35.0 : le service des licences et l'archive en ligne ----
struct ServiceStatus {
    bool                       configured{false};
    online::Config             config;
    std::string                problem;           // la configuration ne se lit pas
    bool                       syncKey{false};    // la cle de synchro de l'archive est sur ce poste
    std::string                syncKeyId;
    archive::LastSync          lastSync;          // la derniere synchro reussie de l'archive
    online::FetchState         fetch;             // le dernier releve des demandes
    std::vector<online::Entry> requests;          // les demandes relevees (les plus recentes d'abord)
    int                        pending{0};        // celles qui attendent une decision
    online::Snapshot           snapshot;          // le dernier releve des licences en ligne (service-releve.json)
    bool                       hasState{false};   // l'etat du service lu dans cette seance (Verifier)
    online::ServiceState       state;
    Seconds                    stateAt{0};
    [[nodiscard]] bool ready() const { return configured && problem.empty(); }
};

// Ce qu'une operation qui signe laisse a publier sur le service (lance par l'application, lentement).
struct ServiceJob {
    enum class Kind : std::uint8_t {
        Licence,         // une licence liee a des PC : a chacun (son etat << poste >>, la licence scellee) - licenceText
        Removal,         // un PC retire d'une licence liee : la nouvelle emission aux autres, << retiree >> pour lui (deviceKey)
        Revocation,      // la liste deposee, puis << revoquee >> pour deviceKeys (les PC lies)
        Unrevoke,        // la liste deposee, puis la licence liee (licenceText, si elle est la) a ses PC
        Registry,        // le registre de l'archive
        OnlineLicence,   // une licence EN LIGNE deposee (licenceText) ; codeHash vide : le code reste le meme
    };
    Kind                     kind{Kind::Licence};
    std::string              label;        // "Service : publication de XPG-2026-00042"
    std::string              licenseId;
    std::string              licenceText;
    std::string              deviceKey;
    std::vector<std::string> deviceKeys;
    std::string              reason;
    std::string              codeHash;          // OnlineLicence : l'empreinte d'un code neuf (jamais le code) ; vide : le code reste
    int                      leaseDays{0};      // OnlineLicence : le bail (jours) ; 0 : le service garde le sien
    int                      requestNumber{0};  // OnlineLicence : la demande manuelle a laquelle elle repond (0 : aucune)
};

// ---- une demande d'activation lue (l'apercu de l'assistant) ----
struct RequestPreview {
    bool             ok{false};
    std::string      why;          // refusee : pourquoi
    std::string      path;
    ActivationRecord record;       // kind "demande" : nom du PC, empreinte, logiciel, date, licence demandee
};

// ---- ce que l'assistant de licence remplit ----
struct LicenceDraft {
    std::vector<std::string> requestPaths;     // les demandes (.xpgreq) ; une licence etendue peut n'en avoir aucune
    std::string              licenseId;
    std::string              customer;
    std::string              edition;
    std::optional<licensing::LicenseType> type;   // vide : celui de l'edition
    int                      maxDevices{0};        // 0 : celui de l'edition
    int                      durationDays{-1};     // -1 : celui de l'edition ; 0 : sans fin (perpetuelle)
    int                      offlineGraceDays{-1}; // -1 : celui de l'edition
    std::vector<std::string> features;             // vide : celles de l'edition
    bool                     extend{false};        // etendre la licence licenseId de l'archive (ses PC sont gardes)
    Seconds                  expiresAt{0};         // 1.33.0 : une fin exacte (une extension garde la sienne) ; 0 : durationDays
    std::vector<std::string> dropDevices;          // 1.33.0 : les PC (cles) a retirer de la licence etendue
    bool                     online{false};        // 1.35.0 : une licence EN LIGNE (aucun PC : un code d'activation)
    int                      leaseDays{online::kDefaultLeaseDays};   // son bail (jours)
    // 1.42.0 : les options de la licence (nullopt : celles de la licence etendue) ; l'achat que cette signature ajoute a
    // l'historique (vide : celui qu'elle est - une licence neuve, une prolongation ; rien pour un PC retire).
    std::optional<std::vector<licensing::LicenseOption>> options;
    std::optional<licensing::Purchase>                    purchase;
};

// ---- 1.42.0 : une option a ajouter a une licence (Licences > Ajouter une option...) ----
struct OptionDraft {
    std::string optionId;     // une option du catalogue (registre)
    int         days{7};      // sa duree (0 : jusqu'a la fin de la licence) ; ignoree si `until` est donne
    Seconds     from{0};      // 0 : maintenant
    Seconds     until{0};     // 0 : from + days (days 0 : la fin de la licence)
    std::string price;        // vide : celui du catalogue pour cette duree ; "offerte" si gift
    bool        gift{false};
};

// ---- 1.42.0 : une demande d'option d'un client (Demandes en ligne) : la fenetre << Ajouter une option >> reglee dessus ----
struct OptionPreset {
    bool        set{false};
    std::string optionId;
    int         days{0};          // 0 : jusqu'a la fin de la licence ; -1 : des dates (from .. lastDay)
    Seconds     from{0};          // 0 : des qu'elle est acceptee
    Seconds     lastDay{0};       // des dates : le dernier jour, compris
    int         requestNumber{0}; // la demande a laquelle elle repond (0 : aucune)
};

// ---- 1.42.0 : un client supprime (Clients > Supprimer le client...) : ses licences, toutes finies, dans la corbeille ----
struct DeletedClient {
    std::string              name;
    Seconds                  deletedAt{0};
    std::vector<std::string> licenceIds;
    std::vector<std::string> trashFiles;    // le fichier de chaque licence dans la corbeille (corbeille-licences/)
};

// ---- 1.44.0 : plusieurs admins (docs/ADMINS.md) ----
// Le role de ce poste : l'admin+ (la cle de l'editeur) ; un admin de la liste (sa cle, active) ; en attente (sa cle
// n'est pas encore dans la liste) ; retire (sa cle est dans la liste, retiree) ; rien (aucune cle).
enum class Role : std::uint8_t { None, Plus, Admin, Pending, Retired };
[[nodiscard]] const char* roleKey(Role r) noexcept;   // "aucun", "admin+", "admin", "attente", "retire"
[[nodiscard]] std::string roleLabel(Role r);          // << Admin+ >>, << Admin >>, << En attente d'acceptation >>...

struct AdminKeyStatus {
    bool        present{false};
    std::string id, keyHex, fingerprint, name, path;   // fingerprint : admins::fingerprint (lue au telephone)
    std::string problem;
};

// Un admin de la liste, et ce que le service en dit (GET /admin/admins).
struct AdminInfo {
    licensing::admins::Admin admin;
    std::string              fingerprint;
    Seconds                  lastAction{0};   // sa derniere ligne au journal
    std::string              software, version;
    Seconds                  seen{0};         // son dernier appel signe
};
struct AdminsView {
    int                    number{0};
    std::vector<AdminInfo> admins;
    std::vector<AdminInfo> editors;   // les cles de l'editeur (l'admin+) : admin.id, admin.key, et ce que le service en sait
};
// Une demande pour devenir admin (GET /admin/admins/demandes).
struct AdminRequest {
    std::string key, id, fingerprint, name, email, pc, machine, software;
    Seconds     received{0};
    bool        fingerprintMismatch{false};   // l'empreinte rendue par le service n'est pas celle calculee ici (jamais accepter)
};
// Ce qu'a fait refreshAdminState / refreshAdminList.
struct AdminRefresh {
    bool        listAdopted{false};     // une liste plus recente est gardee (admins.xpgadm)
    int         listNumber{0};
    int         journalNew{0};          // admin+ : les lignes du journal relevees
    bool        accepted{false};        // en attente : la demande est acceptee (ce poste est admin)
    bool        syncKeyReceived{false}; // la cle de synchro recue par le trousseau
    std::string requestState;           // en attente : "attente", "acceptee", "refusee", "retire"
};

// Une ligne du journal des admins, verifiee (admin+).
struct JournalRow {
    licensing::admins::JournalLine line;
    bool                           verified{false};   // la signature est celle de la cle, et cette cle est connue
    std::string                    who;               // "admin+", le nom de l'admin, "le service"
    std::string                    problem;           // vide : rien a dire
    licensing::admins::Level       level{licensing::admins::Level::Info};   // la criticite en vigueur (la ligne, sinon son type)
    bool                           notify{false};     // << Me prevenir tout de suite >>
    bool                           overridden{false}; // la criticite changee a la main pour cette ligne
    std::string                    overrideBy;
    Seconds                        overrideAt{0};
};
struct JournalView {
    std::vector<JournalRow>  rows;       // les plus anciennes d'abord
    std::vector<std::string> problems;   // << chaine rompue >>, une signature fausse, la copie de ce poste modifiee...
    std::uint64_t            last{0};
    std::string              lastHash;
};
struct ActionSetting {
    licensing::admins::Level level{licensing::admins::Level::Info};
    bool                     notify{false};
};
struct Criticality {
    std::map<std::string, ActionSetting> actions;   // chaque type d'action (admins::actionTypes)
    bool                                 emailCritical{false};   // << Aussi par courriel >> (les critiques)
};

class Workspace {
public:
    explicit Workspace(std::string folder);

    [[nodiscard]] const std::string& folder() const noexcept { return folder_; }
    // Tout relire : la cle, l'archive, le registre des activations, la synchro. Apres chaque operation.
    void reload();
    // L'heure (les essais la fixent).
    [[nodiscard]] Seconds now() const;
    void setClock(std::function<Seconds()> clock) { clock_ = std::move(clock); }

    // ---- la cle ----
    [[nodiscard]] const KeyStatus& key() const noexcept { return key_; }
    [[nodiscard]] const licensing::SigningKey* signingKey() const noexcept { return signing_.get(); }
    OpResult createKey(const std::string& name, bool test);
    // PublisherKey.hpp (la cle PUBLIQUE en C++ ; une cle d'essai : TestPublisherKey.hpp). `keep` : garder celles du fichier.
    OpResult exportPublicKey(const std::string& outPath, bool keep);
    // LENT (Argon2id) : par Background. Les phrases ne sont jamais gardees.
    OpResult backupKey(const std::string& phrase, const std::string& outPath) const;
    OpResult checkBackup(const std::string& path, const std::string& phrase) const;
    OpResult restoreKey(const std::string& path, const std::string& phrase);
    [[nodiscard]] static bool acceptablePhrase(const std::string& phrase, std::string* why);

    // ---- l'archive ----
    [[nodiscard]] bool hasRegistry() const noexcept { return hasRegistry_; }
    [[nodiscard]] const licensing::Registry& registry() const noexcept { return registry_; }
    [[nodiscard]] bool hasRevocations() const noexcept { return hasRevocations_; }
    [[nodiscard]] const licensing::RevocationList& revocations() const noexcept { return revocations_; }
    [[nodiscard]] const std::vector<LicenceEntry>& licences() const noexcept { return licences_; }
    [[nodiscard]] const LicenceEntry* licence(const std::string& licenseId) const;
    [[nodiscard]] std::vector<ClientEntry> clients() const;
    [[nodiscard]] Dashboard dashboard() const;
    [[nodiscard]] const std::vector<std::string>& archiveProblems() const noexcept { return archiveProblems_; }
    // Ranger des fichiers signes (registre, revocations, licences) ; une demande (.xpgreq) est notee
    // dans le registre des activations. Ce que la console fait par archiver et enregistrer-demande.
    OpResult importFiles(const std::vector<std::string>& paths);
    // Le fichier a envoyer au client (.xpglic : la licence, le registre, la liste de revocation).
    OpResult exportLicence(const std::string& licenseId, const std::string& outPath) const;

    // ---- l'assistant de licence ----
    [[nodiscard]] RequestPreview readRequest(const std::string& path) const;
    // L'identifiant suivant : XPG-<annee>-<00001>, apres le plus grand de l'archive pour cette annee.
    [[nodiscard]] std::string suggestLicenseId() const;
    // Ce que serait la licence (sans rien signer) : les lignes du recapitulatif, ou le refus.
    OpResult preview(const LicenceDraft& draft) const;
    // Signer : le fichier `outPath` (.xpglic a envoyer), l'archive, le registre des activations.
    OpResult signLicence(const LicenceDraft& draft, const std::string& outPath);
    // Noter des demandes recues (pas encore signees).
    OpResult recordRequests(const std::vector<std::string>& paths);
    // 1.33.0 : retirer un PC d'une licence (un PC remplace ou rendu) : une nouvelle emission, sans lui, avec la meme
    // fin, les memes fonctionnalites et le meme nombre de PC ; ecrite dans `outPath` (a envoyer au client).
    OpResult removeDevice(const std::string& licenseId, const std::string& deviceKey, const std::string& outPath);

    // ---- revoquer (une nouvelle liste : numero + 1, signee, rangee) ----
    // `deviceKey` vide : toute la licence ; `onlyThisIssue` : seulement les emissions jusqu'a celle de l'archive
    // (une licence remplacee par une plus recente ; issued_before = la date de la suivante, soit maintenant).
    OpResult revoke(const std::string& licenseId, const std::string& deviceKey, const std::string& reason, bool onlyThisIssue);
    // Retirer une revocation (une nouvelle liste sans elle).
    OpResult unrevoke(std::size_t index);
    // 1.38.0 : SUPPRIMER une licence REVOQUEE de l'archive (Licences > Supprimer) : son fichier va dans la corbeille de
    // l'outil (corbeille-licences/), la synchro ne la ramene pas (licences-supprimees.json) ; sa revocation reste dans la
    // liste signee (les Studios la refusent toujours) et le registre des activations garde son histoire. Une licence
    // pas revoquee : refuse (revoque-la d'abord).
    OpResult deleteRevokedLicence(const std::string& licenseId);
    // 1.42.0 : une licence est-elle EN VIGUEUR (ni finie, ni revoquee) ? Un client ou une offre qui en ont une ne se
    // suppriment pas (un Studio s'arreterait chez le client).
    [[nodiscard]] static bool inForce(const LicenceEntry& e) noexcept { return !e.revoked && !e.expired; }
    // Les licences en vigueur de cette offre (la suppression d'une offre : refusee s'il y en a).
    [[nodiscard]] std::vector<std::string> licencesInForce(const std::string& edition) const;
    // La fin de la derniere licence en vigueur de cette offre (0 : aucune ; une perpetuelle : jamais, -1).
    [[nodiscard]] Seconds lastLicenceEnd(const std::string& edition) const;
    // 1.42.0 : SUPPRIMER un client (aucune licence en vigueur) : ses licences vont dans la corbeille (corbeille-licences/, la
    // synchro ne les ramene pas), le client est retenu dans clients-supprimes.json (il ne revient pas par le registre des
    // activations) ; RESTAURER le remet (ses fichiers reviennent dans l'archive).
    OpResult deleteClient(const std::string& name);
    OpResult restoreClient(const std::string& name);
    [[nodiscard]] std::vector<DeletedClient> deletedClients() const;
    // 1.42.0 : l'HISTORIQUE DES ACHATS d'un client (Mes achats, la fiche) : chaque licence signee (une offre), chaque
    // prolongation, chaque option, des licences de l'archive et du registre des activations ; les plus anciens d'abord.
    [[nodiscard]] std::vector<licensing::Purchase> purchaseHistory(const std::string& customer) const;
    // 1.42.0 : AJOUTER UNE OPTION a une licence : une nouvelle emission (memes PC, meme fin), l'option en plus. Une licence en
    // ligne : sa nouvelle emission part sur le service (le code reste) ; une licence liee : le fichier `outPath` (a envoyer)
    // et le service s'il est regle.
    OpResult addOption(const std::string& licenseId, const OptionDraft& draft, const std::string& outPath);
    // 1.42.0 : les offres a supprimer d'elles-memes quand plus aucune licence ne les utilise (offres-a-supprimer.json).
    [[nodiscard]] std::vector<std::string> offersToDelete() const;
    OpResult setDeleteWhenUnused(const std::string& edition, bool on);
    // La liste signee a envoyer aux clients (.xpgrev ; ils l'importent comme une licence).
    OpResult exportRevocations(const std::string& outPath) const;

    // ---- le registre (editions et fonctionnalites) : numero + 1, signe, range ----
    OpResult signRegistry(const licensing::Registry& edited);
    // Le premier registre, depuis un JSON ecrit a la main (registre.json de la console).
    OpResult signRegistryJson(const std::string& path);

    // ---- le registre des activations ----
    [[nodiscard]] const std::vector<ActivationRecord>& records() const noexcept { return records_; }
    [[nodiscard]] std::vector<KnownDevice> devices() const;
    [[nodiscard]] int skippedLines() const noexcept { return skipped_; }
    OpResult exportCsv(const std::vector<KnownDevice>& devices, const std::string& path) const;

    // ---- 1.35.0 : le service des licences ----
    [[nodiscard]] const ServiceStatus& service() const noexcept { return service_; }
    // L'adresse du service (https://..., ou http://127.0.0.1 pour les essais).
    OpResult serviceConfigure(const std::string& url);
    // La cle de synchro de l'archive : la creer (`recoveryCode` recoit son code de secours, a noter hors du PC), ou la
    // redonner par un code (un autre poste ; `replace` : une autre cle deja la est mise de cote).
    OpResult syncKeyCreate(std::string* recoveryCode);
    OpResult syncKeyRestore(const std::string& code, bool replace);
    [[nodiscard]] std::string syncRecoveryCode(std::string* why) const;
    // LENT (le reseau) : par runSlow. `state` (facultatif) : ce qui a ete lu, a adopter ensuite (adoptServiceState).
    [[nodiscard]] OpResult serviceVerify(online::ServiceState* state = nullptr) const;
    [[nodiscard]] OpResult serviceReadKey(std::string& keyHex) const;
    [[nodiscard]] OpResult serviceCertify(const std::string& expectedKey) const;
    [[nodiscard]] archive::SyncReport archiveSync(bool receiveOnly, bool dryRun) const;
    [[nodiscard]] OpResult serviceListLicences(online::Snapshot& out) const;
    [[nodiscard]] OpResult serviceRetire(const std::string& licenseId, const std::string& deviceKey) const;
    [[nodiscard]] OpResult serviceSetActive(const std::string& licenseId, bool active) const;
    [[nodiscard]] OpResult serviceFetch() const;
    [[nodiscard]] OpResult serviceRefuse(const std::string& which, const std::string& reason) const;
    // 1.42.0 : une demande d'option acceptee pour une licence en ligne (online::answerOption).
    [[nodiscard]] OpResult serviceAnswerOption(const std::string& which, const std::string& licenseId, const std::string& message) const;
    [[nodiscard]] OpResult serviceStats() const;
    // 1.40.0 : les compteurs de ce qui change (GET /admin/etat) - le regard de l'outil ouvert (Shell::autoWatch).
    [[nodiscard]] OpResult serviceWatch(online::AdminWatch& out) const;
    [[nodiscard]] OpResult serviceRun(const ServiceJob& job) const;
    // 1.37.0 : une licence EN LIGNE deposee avec son code : le code garde chiffre sur ce poste (Voir le code...), et la
    // reponse a la demande (requestNumber) porte le code scelle pour le PC (son Studio s'active seul) et l'etat du mail.
    [[nodiscard]] OpResult serviceRunOnline(const ServiceJob& job, const std::string& code) const;
    // 1.37.0 : RENVOYER la reponse d'une demande << acceptee ici >> (Entry::acceptedHere) dont la reponse n'est pas partie :
    // une licence en ligne (son code garde sur ce poste, scelle pour le PC), ou la licence liee (a chacun de ses PC).
    // Lent (reseau) : runSlow.
    [[nodiscard]] OpResult serviceResendAnswer(int number) const;
    // 1.37.0 : le mail au client a l'acceptation (vide : pas de mail regle sur ce poste).
    [[nodiscard]] online::Mailer mailer() const;
    // Sur le fil de l'interface : ce qu'un travail lent a lu.
    void adoptServiceState(const online::ServiceState& state);
    void adoptSnapshot(online::Snapshot snapshot);
    // Ce que la derniere operation a laisse a publier (rien si le service n'est pas regle).
    [[nodiscard]] bool hasServiceJob() const noexcept { return job_.has_value(); }
    std::optional<ServiceJob> takeServiceJob();
    void queueServiceJob(ServiceJob job);
    // Une LICENCE EN LIGNE : la signer (aucun PC), la ranger, tirer un CODE D'ACTIVATION neuf. Rien n'est envoye : `job`
    // la depose (par runSlow, serviceRun) ; `code` (le code lisible) n'est a montrer qu'une fois ce depot reussi, puis a
    // oublier. `outPath` (facultatif) : une copie du .xpglic. `requestNumber` : la demande manuelle a laquelle elle repond.
    // Une licence en ligne PROLONGEE (draft.extend) : une emission plus recente, le meme code (`code` vide, l'empreinte
    // absente : le service garde la sienne).
    OpResult prepareOnlineLicence(const LicenceDraft& draft, const std::string& outPath, ServiceJob& job, std::string& code, int requestNumber = 0);
    // Un code neuf pour une licence en ligne de l'archive (l'ancien ne marche plus une fois deposee).
    OpResult prepareNewCode(const std::string& licenseId, ServiceJob& job, std::string& code);
    // Une demande relevee (dans demandes-en-ligne/) ? Ou ecrire la licence qui lui repond (sans rien demander).
    [[nodiscard]] bool isOnlineRequest(const std::string& path) const;
    [[nodiscard]] std::string onlineLicencePath(const std::string& licenseId) const;
    [[nodiscard]] const online::Entry* onlineRequest(int number) const;
    // La demande relevee dont `path` est le .xpgreq (nullptr : ce n'en est pas une).
    [[nodiscard]] const online::Entry* onlineRequestAt(const std::string& path) const;
    // Une licence de l'archive est-elle EN LIGNE (son champ service) ?
    [[nodiscard]] static bool isOnlineLicence(const licensing::License& l) { return !l.service.empty(); }
    // Les fichiers PUBLICS a compiler dans les Studios (la cle publique, le registre, l'adresse du service).
    OpResult studioKit(const std::string& outDir) const;
    // cle-publique.txt (la cle PUBLIQUE de l'editeur en hexadecimal : PUBLISHER_KEYS du service) dans ce dossier.
    OpResult writePublicKeyFile(const std::string& outDir) const;

    // ======================================================== 1.44.0 : plusieurs admins (WorkspaceAdmins.cpp) ==
    // ---- le role, la cle qui signe, les droits ----
    [[nodiscard]] Role role() const noexcept { return role_; }
    [[nodiscard]] const AdminKeyStatus& adminKey() const noexcept { return adminKey_; }
    // La cle qui signe tout (licences, registre, revocations, etats des postes, journal, appels au service) : celle de
    // l'editeur pour l'admin+, celle de l'admin pour un admin de la liste ; nul sinon.
    [[nodiscard]] const licensing::SigningKey* signer() const noexcept;
    [[nodiscard]] std::vector<std::string> myRights() const;   // les 13 pour l'admin+
    [[nodiscard]] bool can(std::string_view right) const;
    // Pourquoi ce droit manque (la phrase du bouton grise : << Tu n'as pas le droit << ... >> : demande-le a l'admin+. >>) ;
    // vide : permis.
    [[nodiscard]] std::string whyNot(std::string_view right) const;
    // La liste des admins en vigueur sur ce poste (admins.xpgadm, verifiee : signee par une cle de l'editeur).
    [[nodiscard]] const std::optional<licensing::admins::AdminList>& adminList() const noexcept { return adminList_; }
    [[nodiscard]] const licensing::admins::Admin* me() const;   // l'entree de ce poste dans la liste (admin, retire)
    // Les cles de l'editeur que ce poste croit : la sienne (admin+), celles compilees, celles retenues du service.
    [[nodiscard]] const std::vector<licensing::crypto::PublicKey>& editorKeys() const noexcept { return editorKeys_; }

    // ---- devenir admin (un poste sans cle de l'editeur ; LENT : par runSlow, puis reload) ----
    // Cree la cle d'admin s'il le faut, depose la demande (POST /v1/admins/demande, signee par elle) ; `fingerprint` :
    // l'empreinte a lire au telephone a l'admin+ (aussi dans le message).
    [[nodiscard]] OpResult requestAdmin(const std::string& name, const std::string& email, std::string* fingerprint = nullptr) const;
    // Ou en est la demande (GET /v1/admins/demande) : "attente", "acceptee", "refusee", "retire".
    [[nodiscard]] OpResult adminRequestState(std::string& state) const;
    // Relever la liste des admins (GET /v1/admins) et la garder si elle est plus recente ; un admin accepte : les cles de
    // l'editeur retenues, la cle de synchro recue par le trousseau.
    [[nodiscard]] OpResult refreshAdminList(AdminRefresh* out = nullptr) const;
    // Le regard du service (Shell, toutes les 15 s) : `etat` (GET /admin/etat, serviceWatch) ou nul (en attente : la
    // demande est regardee). Une liste plus recente est gardee, le journal releve (admin+), le journal a envoyer part.
    [[nodiscard]] OpResult refreshAdminState(const online::AdminWatch* etat, AdminRefresh* out = nullptr) const;
    // Prendre la cle de synchro dans le trousseau (GET /admin/trousseau, ouverte par la cle d'admin).
    [[nodiscard]] OpResult receiveSyncKey() const;

    // ---- gerer les admins (l'admin+ seulement ; LENT : par runSlow, puis reload) ----
    [[nodiscard]] OpResult serviceAdmins(AdminsView& out) const;
    [[nodiscard]] OpResult pendingAdminRequests(std::vector<AdminRequest>& out) const;
    // Accepter : la liste numero + 1 (signee par la cle de l'editeur) avec cet admin, ses droits ceux du profil (ou
    // `rights` s'ils sont donnes), le nom du profil note ; publiee ; la cle de synchro deposee dans son trousseau.
    // `fingerprintConfirmed` : << J'ai verifie l'empreinte >> (sinon refuse).
    [[nodiscard]] OpResult acceptAdmin(const std::string& keyHex, const std::string& profile, const std::vector<std::string>* rights = nullptr,
                                       bool fingerprintConfirmed = true) const;
    [[nodiscard]] OpResult refuseAdmin(const std::string& keyHex) const;
    // Retirer : retire = maintenant, droits vides ; ses licences en vigueur gardees (gardes), ou revoquees
    // (`revokeLicencesToo`, par la cle de l'editeur) ; le registre et la liste de revocation en place, s'il les a signes,
    // re-signes par la cle de l'editeur (numero + 1) et publies.
    [[nodiscard]] OpResult retireAdmin(const std::string& keyHex, bool revokeLicencesToo) const;
    // Ses droits (et le profil d'origine note) ; un droit retire : ce qu'il a signe et qui court encore est garde.
    [[nodiscard]] OpResult setAdminRights(const std::string& keyHex, const std::vector<std::string>& rights, const std::string& profile) const;
    // Redeposer la cle de synchro dans le trousseau d'un admin (PUT /admin/trousseau).
    [[nodiscard]] OpResult giveSyncKey(const std::string& keyHex) const;

    // ---- les profils de droits (l'admin+ ; profils-admins.json, partage par l'archive) ----
    [[nodiscard]] std::vector<licensing::admins::Profile> profiles() const;
    OpResult saveProfile(const licensing::admins::Profile& profile);   // cree ou remplace (<< Tous les droits >> : refuse)
    OpResult renameProfile(const std::string& from, const std::string& to);
    OpResult deleteProfile(const std::string& name);
    // Copier les droits d'un profil vers des admins (une liste signee ; LENT).
    [[nodiscard]] OpResult copyProfileTo(const std::string& profile, const std::vector<std::string>& adminKeys) const;

    // ---- le journal des admins ----
    // Une ligne (docs/ADMINS.md, par. 6) : l'entree signee par signer(), gardee dans journal-a-envoyer.jsonl, envoyee
    // par le prochain appel au service (flushJournal). `detailJson`, `beforeJson`, `afterJson` : des objets JSON (vides : {}).
    OpResult journal(const std::string& action, const std::string& object, const std::string& summary, const std::string& detailJson = {},
                     const std::string& beforeJson = {}, const std::string& afterJson = {}) const;
    [[nodiscard]] int journalWaiting() const;   // les lignes pas encore envoyees
    [[nodiscard]] OpResult flushJournal() const;   // LENT
    // L'admin+ : relever le journal (GET /admin/journal), verifier chaque ligne (la signature, la cle, la chaine), le
    // garder (journal-admins.jsonl). LENT. `added` : les lignes relevees.
    [[nodiscard]] OpResult fetchJournal(int* added = nullptr) const;
    [[nodiscard]] JournalView journalLines() const;
    [[nodiscard]] Criticality criticality() const;
    OpResult setCriticality(const std::string& action, licensing::admins::Level level, bool notify);
    OpResult setEmailCritical(bool on);
    // La criticite d'une ligne changee a la main (qui et quand sont notes) ; `reset` : revenir a celle de son type.
    OpResult setLineLevel(std::uint64_t number, licensing::admins::Level level, bool reset = false);
    // Les lignes relevees depuis le dernier appel dont la criticite dit << Me prevenir tout de suite >> (le toast).
    [[nodiscard]] std::vector<JournalRow> newAlerts();

    // ---- ce que l'Admin fait pour un admin (les droits << l'Admin >>) ----
    // Revoir le code d'activation d'une licence en ligne (droit << codes >>) ; une ligne au journal.
    OpResult revealCode(const std::string& licenseId, std::string& code) const;
    // Ecrire a un client (droit << courriels >>) ; LENT (SMTP) ; une ligne au journal.
    [[nodiscard]] OpResult sendClientMail(const std::string& to, const std::string& toName, const std::string& subject, const std::string& body) const;

    // ---- le jeton d'admin (docs/ADMINS.md, par. 7 : le Studio du meme compte telecharge l'installateur de l'Admin) ----
    // jeton-admin.xpgjet, signe par signer() : {cle, poste, expire (30 jours), version, nom}. A chaque lancement et chaque jour
    // (`force` faux : seulement s'il manque ou date de plus d'un jour). Rien pour un poste sans role.
    OpResult writeAdminToken(bool force = true) const;

private:
    void loadKey();
    void loadArchive();
    void loadActivations();
    void loadService();
    OpResult archiveAndNote(const std::string& bytes, const std::vector<ActivationRecord>& records, OpResult r);
    [[nodiscard]] std::string archivedText(const std::string& relative) const;
    OpResult signRevocationList(licensing::RevocationList list, const std::string& what);
    // 1.42.0 : l'historique a ecrire dans une licence signee maintenant (rien tant que le registre n'a pas de champ 1.42).
    [[nodiscard]] std::optional<std::vector<licensing::Purchase>> purchasesToWrite(const LicenceDraft& d) const;

    std::string                            folder_;
    std::function<Seconds()>               clock_;
    KeyStatus                              key_;
    std::unique_ptr<licensing::SigningKey> signing_;
    bool                                   hasRegistry_{false};
    licensing::Registry                    registry_;
    bool                                   hasRevocations_{false};
    licensing::RevocationList              revocations_;
    std::vector<LicenceEntry>              licences_;
    std::vector<std::string>               archiveProblems_;
    std::vector<ActivationRecord>          records_;
    int                                    skipped_{0};
    ServiceStatus                          service_;
    std::optional<ServiceJob>              job_;
    // 1.44.0 : plusieurs admins
    void loadAdmins();
    // Refuse (le droit manque, ou aucune cle) ; nullopt : permis.
    [[nodiscard]] std::optional<OpResult> denied(std::string_view right) const;
    // Ce que signLicence et prepareOnlineLicence demandent pour ce brouillon (licence_creer, _prolonger, _option, poste_retirer).
    [[nodiscard]] std::string licenceRight(const LicenceDraft& d) const;
    // Les entrees de signLicense, et ce que croit un admin (le registre, la licence a etendre d'un autre signataire).
    [[nodiscard]] std::function<bool(const licensing::SignedBlock&)> alsoTrusted() const;
    // Le fichier a donner au client : la liste des admins d'abord quand c'est un admin qui a signe (un Studio hors ligne
    // la lit pour croire la licence), puis le reste.
    [[nodiscard]] std::string withAdminList(const std::string& signedText) const;
    void noteClient(OpResult& r, const std::string& action, const std::string& name, const std::vector<std::string>& licenceIds);
    std::unique_ptr<licensing::SigningKey>      adminSigning_;
    AdminKeyStatus                              adminKey_;
    Role                                        role_{Role::None};
    std::optional<licensing::admins::AdminList> adminList_;
    std::vector<licensing::crypto::PublicKey>   editorKeys_;
    std::uint64_t                               alertedUpTo_{0};   // newAlerts : la derniere ligne deja dite
    bool                                        alertsPrimed_{false};
};

// ---- un travail lent sur un fil a part : l'interface le suit sans attendre (jamais bloquee) ----
class Background {
public:
    Background() = default;
    ~Background();
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;
    // Faux : un travail tourne deja (un a la fois).
    bool start(std::string label, std::function<OpResult()> work);
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] std::string label() const;
    // Le resultat, une fois fini (rendu une seule fois) ; vide tant qu'il tourne.
    [[nodiscard]] std::optional<OpResult> take();
    // Attendre la fin (les essais ; la fermeture de la fenetre).
    void wait();

private:
    mutable std::mutex      mutex_;
    std::thread             thread_;
    std::atomic<bool>       running_{false};
    std::string             label_;
    std::optional<OpResult> result_;
};

// ---- 1.44.0 : ce que partagent Workspace.cpp et WorkspaceAdmins.cpp (pas pour les ecrans) ----
namespace detail {
// La cle qui signe et ce qu'elle croit, RELUES DU DOSSIER : les fonctions LENTES tournent sur un fil a part et ne
// lisent jamais l'etat du Workspace.
struct Caller {
    std::unique_ptr<licensing::SigningKey>      key;   // la cle de l'editeur (admin+) ou celle de l'admin
    Role                                        role{Role::None};
    std::optional<licensing::admins::AdminList> admins;
    std::vector<licensing::crypto::PublicKey>   editorKeys;
    [[nodiscard]] const licensing::admins::Admin* me() const;
    [[nodiscard]] bool can(std::string_view right) const;
};
// Faux : aucune cle sur ce poste, ou elle ne se relit pas (`why`).
bool loadCaller(const std::string& folder, Caller& out, std::string* why);
// Refuse (`right` vide : l'admin+ seulement) ; nullopt : permis.
[[nodiscard]] std::optional<OpResult> callerDenied(const Caller& c, std::string_view right);
[[nodiscard]] online::Options optionsFor(const Caller& c, Seconds now);
// Une ligne du journal des admins signee par `key`, gardee pour l'envoi (journal-a-envoyer.jsonl).
bool queueJournal(const std::string& folder, const licensing::SigningKey& key, Seconds now, const std::string& action, const std::string& object,
                  const std::string& summary, const std::string& detailJson, const std::string& beforeJson, const std::string& afterJson,
                  std::string* why);
// Envoyer les lignes gardees (POST /admin/journal) par ce client ; sans bruit (ce qui reste partira au prochain appel).
int flushJournalWith(const std::string& folder, online::Client& client, std::string* why = nullptr);
// Les memes, en ouvrant le client du service (rien a envoyer : aucun appel).
int flushJournalQuiet(const std::string& folder, const Caller& c, Seconds now);
// Les droits qu'il faut pour remplacer le registre `before` par `after` (la partie qui change : editions -> offres,
// options, announcements -> annonces, une autre partie -> offres) ; vide : rien ne change.
[[nodiscard]] std::vector<std::string> registryRights(const licensing::Registry* before, const licensing::Registry& after);
} // namespace detail

// "07/11/2026", "07/11/2026 14:05" (heure locale du PC), "" pour 0.
[[nodiscard]] std::string dateFr(Seconds t);
[[nodiscard]] std::string dateTimeFr(Seconds t);

} // namespace license_admin::gui
