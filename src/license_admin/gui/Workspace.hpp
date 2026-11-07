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
// =============================================================================
#pragma once

#include "../Admin.hpp"
#include "../ArchiveSync.hpp"
#include "../KeyBackup.hpp"
#include "../ServiceClient.hpp"
#include "../../licensing/Documents.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace license_admin::gui {

using licensing::Seconds;

inline constexpr const char* kAppName = "XPG License Manager Admin";
inline constexpr const char* kAppVersion = "1.1.0";   // 1.33.0 : la premiere application ; 1.35.0 : le service des licences

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
    std::string              codeHash;          // OnlineLicence : l'empreinte d'un code neuf (jamais le code)
    int                      leaseDays{online::kDefaultLeaseDays};
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
    [[nodiscard]] OpResult serviceStats() const;
    [[nodiscard]] OpResult serviceRun(const ServiceJob& job) const;
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

private:
    void loadKey();
    void loadArchive();
    void loadActivations();
    void loadService();
    OpResult archiveAndNote(const std::string& bytes, const std::vector<ActivationRecord>& records, OpResult r);
    [[nodiscard]] std::string archivedText(const std::string& relative) const;
    OpResult signRevocationList(licensing::RevocationList list, const std::string& what);

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

// "07/11/2026", "07/11/2026 14:05" (heure locale du PC), "" pour 0.
[[nodiscard]] std::string dateFr(Seconds t);
[[nodiscard]] std::string dateTimeFr(Seconds t);

} // namespace license_admin::gui
