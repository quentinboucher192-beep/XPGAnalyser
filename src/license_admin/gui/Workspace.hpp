// =============================================================================
//  license_admin/gui/Workspace.hpp - 1.33.0 : LE POSTE ADMIN, VU PAR L'APPLICATION
//  XPG-License-Manager-Admin (l'outil de l'EDITEUR ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Ce que les ecrans de l'application lisent et font, sans aucun ecran : la cle
//  de l'editeur, l'archive (le registre, la liste de revocation, chaque licence),
//  le registre des activations, la synchro GitHub. Chaque operation rend un
//  OpResult (fait ou refuse, en francais, et ce qui a ete ecrit) ; les ecrans le
//  montrent tel quel. Les memes fonctions que la console (Admin.hpp,
//  GitHubSync.hpp, KeyBackup.hpp) : un fichier signe par l'application est
//  identique a celui de la console.
//
//  Le dossier : celui de la cle (defaultKeyFolder : %LOCALAPPDATA%\XPG\LicenseAdmin),
//  le meme que XPG-License-Admin.exe - les deux outils voient la meme cle, la
//  meme archive, le meme registre des activations.
//
//  LES SECRETS (phrase de passe, jeton GitHub, code de secours) passent par les
//  arguments des fonctions et ne sont jamais gardes ni ecrits par le Workspace
//  (le jeton : protege par DPAPI, par GitHubSync).
//
//  LENT : backupKey / checkBackup / restoreKey (Argon2id, quelques secondes) et
//  githubSync (le reseau) - a lancer par Background, jamais sur le fil de
//  l'interface. Le reste lit ou ecrit de petits fichiers.
//
//  1.34.0 : LA PASSERELLE DES CLIENTS (un depot GitHub PUBLIC, Passerelle.hpp) :
//  la regler, relever les demandes en ligne (passerelleFetch), refuser, et
//  PUBLIER ce qu'une operation vient de signer (une licence a tous ses PC, une
//  revocation, un PC retire, un registre) : chaque operation qui signe, la
//  passerelle reglee, laisse un GatewayJob (takeGatewayJob) que l'application
//  lance par runSlow (publishToGateway, Page.hpp). Les passerelle* qui touchent
//  au reseau sont LENTES (const : elles relisent la cle et les fichiers ; jamais
//  l'etat du Workspace).
// =============================================================================
#pragma once

#include "../Admin.hpp"
#include "../GitHubSync.hpp"
#include "../KeyBackup.hpp"
#include "../Passerelle.hpp"
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
inline constexpr const char* kAppVersion = "1.0.0";   // 1.33.0 : la premiere application

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
    // 1.34.0 : les demandes en ligne (la passerelle) qui attendent ta decision, au dernier releve.
    bool    onlineConfigured{false};
    int     onlinePending{0};
    Seconds onlineFetchedAt{0};         // 0 : jamais relevees sur ce poste
    std::vector<std::pair<std::string, int>> byEdition;   // edition -> licences actives
    std::vector<ActivationRecord>            recent;      // les dernieres operations (les plus recentes d'abord, 12 au plus)
};

// ---- la synchro GitHub ----
struct GithubStatus {
    bool           configured{false};
    github::Config config;
    bool           token{false};
    bool           syncKey{false};
    std::string    syncKeyId;
    std::string    problem;      // la configuration ne se lit pas
};

// ---- 1.34.0 : la passerelle des clients (Passerelle.hpp) ----
struct PasserelleStatus {
    bool                           configured{false};
    passerelle::Config             config;
    bool                           requestToken{false};    // le jeton des demandes (celui des clients) est sur ce poste
    bool                           publisherToken{false};  // le jeton GitHub de l'editeur (celui de la synchro)
    std::string                    problem;                // la configuration ne se lit pas
    passerelle::FetchState         fetch;                  // le dernier releve
    std::vector<passerelle::Entry> requests;               // les demandes relevees (les plus recentes d'abord)
    int                            pending{0};             // celles qui attendent une decision
};

// Ce qu'une operation qui signe laisse a publier sur la passerelle (lance par l'application, lentement).
struct GatewayJob {
    enum class Kind : std::uint8_t {
        Licence,      // une licence signee : a chacun de ses PC (licenceText)
        Removal,      // un PC retire : la nouvelle emission aux autres, << retiree >> pour lui (deviceKey)
        Revocation,   // << revoquee >> pour deviceKeys (la raison et la liste de revocation scellees pour chacun)
        Unrevoke,     // le registre, puis la licence (licenceText, si elle est la) a ses PC, avec la liste scellee
        Registry,     // le registre de l'archive (la liste de revocation n'est plus publiee en clair)
    };
    Kind                     kind{Kind::Licence};
    std::string              label;        // "Passerelle : publication de XPG-2026-00042"
    std::string              licenseId;
    std::string              licenceText;
    std::string              deviceKey;
    std::vector<std::string> deviceKeys;
    std::string              reason;
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

    // ---- la synchro GitHub ----
    [[nodiscard]] const GithubStatus& github() const noexcept { return github_; }
    // Le depot (proprietaire/nom), la branche, la racine. La premiere fois : la cle de synchro est creee et
    // `recoveryCode` recoit son code de secours (a noter hors du PC). `code` donne : la cle redonnee par ce code
    // (un autre poste) ; `replace` : une autre cle deja la est mise de cote.
    OpResult githubConfigure(const std::string& repo, const std::string& branch, const std::string& root, const std::string& code,
                             bool replace, std::string* recoveryCode);
    OpResult githubSaveToken(const std::string& token);
    OpResult githubEraseToken();
    [[nodiscard]] std::string githubRecoveryCode(std::string* why) const;
    // LENT (le reseau) : par Background. `dryRun` : ce qui serait recu et envoye (github-etat).
    [[nodiscard]] github::SyncReport githubSync(bool receiveOnly, bool dryRun) const;
    // Pour les essais (le faux GitHub) : l'adresse de l'API.
    OpResult githubSetApi(const std::string& api);

    // ---- 1.34.0 : la passerelle des clients ----
    [[nodiscard]] const PasserelleStatus& passerelle() const noexcept { return passerelle_; }
    // Le depot PUBLIC (proprietaire/nom) et la branche.
    OpResult passerelleConfigure(const std::string& repo, const std::string& branch);
    // Le jeton des demandes (celui des clients : Issues seulement) ; jamais garde par le Workspace.
    OpResult passerelleSaveRequestToken(const std::string& token);
    OpResult passerelleEraseRequestToken();
    // Pour les essais (le faux GitHub local) : les adresses de l'API et de raw.
    OpResult passerelleSetAddress(const std::string& api, const std::string& raw);
    // LENT (le reseau) : par Background / runSlow.
    [[nodiscard]] OpResult passerelleVerify() const;
    [[nodiscard]] OpResult passerellePublishConfig() const;
    [[nodiscard]] OpResult passerelleFetch() const;
    // Refuser la demande en attente de ce poste (son identifiant, ou "#<issue>").
    [[nodiscard]] OpResult passerelleRefuse(const std::string& device, const std::string& reason) const;
    [[nodiscard]] OpResult passerelleRun(const GatewayJob& job) const;
    // Ce que la derniere operation a laisse a publier (rien si la passerelle n'est pas reglee).
    [[nodiscard]] bool hasGatewayJob() const noexcept { return job_.has_value(); }
    std::optional<GatewayJob> takeGatewayJob();
    // Remettre un travail pris (la publication n'a pas pu commencer : un autre travail tournait).
    void queueGatewayJob(GatewayJob job);
    // Une demande relevee (dans demandes-en-ligne/) ? Ou ecrire la licence qui lui repond (sans rien demander).
    [[nodiscard]] bool isOnlineRequest(const std::string& path) const;
    [[nodiscard]] std::string onlineLicencePath(const std::string& licenseId) const;
    [[nodiscard]] const passerelle::Entry* onlineRequest(int issue) const;
    // La demande relevee dont `path` est le .xpgreq (nullptr : ce n'en est pas une).
    [[nodiscard]] const passerelle::Entry* onlineRequestAt(const std::string& path) const;
    // Les fichiers PUBLICS a compiler dans les Studios (la cle publique, le registre, l'adresse de la passerelle).
    OpResult studioKit(const std::string& outDir) const;

private:
    void loadKey();
    void loadArchive();
    void loadActivations();
    void loadGithub();
    void loadPasserelle();
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
    GithubStatus                           github_;
    PasserelleStatus                       passerelle_;
    std::optional<GatewayJob>              job_;
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
