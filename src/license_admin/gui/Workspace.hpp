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
//  WorkspaceAdmins.cpp. Les fichiers (deposes en morceaux, repris), les messages a
//  un PC (scelles pour sa cle), les versions (lues dans les installateurs, publiees),
//  les adresses de secours et la sauvegarde complete (le service, le dossier, la
//  cle ; restauree ailleurs) : WorkspaceVersions.cpp.
// =============================================================================
#pragma once

#include "../Admin.hpp"
#include "../ArchiveSync.hpp"
#include "../KeyBackup.hpp"
#include "../ServiceClient.hpp"
#include "../Shop.hpp"   // 1.46.0 : la boutique et les commandes
#include "../Stats.hpp"  // 1.47.0 : les statistiques et le globe
#include "../Changes.hpp"   // 1.48.1 : << quelque chose a change >> (toutes les pages se relisent)
#include "../ServiceConnection.hpp"   // 1.47.0 : la connexion au service qui voyage avec l'installateur
#include "Derogation.hpp"             // 1.47.0 : la derogation << Gerer le service >>
#include "../Edition.hpp"             // 1.49.0 : l'Admin+ ou l'Admin classique (XPG Admin)
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
// 1.49.0 : le nom du programme qui tourne - << XPG Admin >> (l'Admin classique, license_admin::classique()) ou celui de
// l'Admin+ (kAppName). La fenetre, le menu, l'aide.
[[nodiscard]] inline const char* appName() noexcept { return license_admin::classique() ? "XPG Admin" : kAppName; }
// La phrase de XPG Admin quand on lui demande ce qui est a l'admin+ (un lien, un essai) : il refuse et le dit.
inline constexpr const char* kClassicRefusal =
    "XPG Admin ne fait pas cela : c'est \xC3\xA0 l'admin+ (XPG License Manager Admin, sur le poste qui a la cl\xC3\xA9 de l'\xC3\xA9" "diteur)";
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
    // 1.47.0 : la connexion signee adoptee (connexion-service.json : d'ou vient l'adresse, la cle du service attendue) et
    // les fichiers refuses a la derniere recherche (une adresse modifiee, une cle inconnue...).
    bool                                hasConnection{false};
    connexion::Connection               connection;
    std::vector<connexion::Refused>     connectionRefused;
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
        DeleteLicences,  // 1.48.1 : des licences en ligne supprimees de l'archive, effacees du service (licenseIds)
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
    std::vector<std::string> licenseIds;        // 1.48.1 : DeleteLicences
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
// Le role de ce poste : l'admin+ (la cle de l'editeur) ; un admin de la liste (sa cle, active) ; en cours d'ajout (sa
// cle n'est pas encore dans la liste : 1.51.0, inscrite par son code d'invitation, l'Admin+ la signe d'office) ; retire
// (sa cle est dans la liste, retiree) ; rien (aucune cle).
enum class Role : std::uint8_t { None, Plus, Admin, Pending, Retired };
[[nodiscard]] const char* roleKey(Role r) noexcept;   // "aucun", "admin+", "admin", "attente", "retire"
[[nodiscard]] std::string roleLabel(Role r);          // << Admin+ >>, << Admin >>, << Admin (ajout en cours) >>...

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
// 1.47.0 : une invitation de l'admin+ (<< Ajouter un admin... >> ; invitations-admins.json, sur son poste) - le nom, le
// courriel, le profil prevu, comment l'Admin est parti (<< studio >>, << lien >>, << code >>), quand.
// 1.51.0 : PLUS DE DEMANDE D'ADMIN - c'est elle qui fait l'admin. Ses droits (une copie du profil a la creation) sont
// gardes ICI, jamais sur le service : l'Admin+ signe d'office la cle inscrite par son code avec eux (admitInvitedAdmins),
// une seule fois (`admittedKey`) ; une inscription dont l'invitation n'a pas ete creee sur ce poste n'est jamais signee.
struct AdminInvitation {
    std::string name, email, profile, via;
    Seconds     at{0};
    std::string id;          // 1.49.0 : l'invitation du service (inv-...) ; vide : une d'avant
    Seconds     expires{0};  // 1.49.0 : la fin du code (48 h)
    std::vector<std::string> rights;      // 1.51.0 : les droits choisis (`rightsSet` faux : une d'avant, le profil par son nom)
    bool                     rightsSet{false};
    std::string              admittedKey; // 1.51.0 : la cle signee d'office pour elle (vide : pas encore)
    Seconds                  admittedAt{0};
};
// 1.47.0 : une liste des admins vue sur ce poste (admins-vus.jsonl) et ce qu'elle disait de cette cle : l'histoire de
// ses droits (une liste sur l'autre).
struct AccountStep {
    int                      number{0};
    Seconds                  issued{0};
    bool                     listed{false}, active{false};   // la cle y est ; active (pas retiree)
    std::string              profile;
    std::vector<std::string> rights;
};
// 1.47.0 : << Mon compte >> (PageAccount.cpp) - qui est ce poste, sans reseau.
struct AccountInfo {
    Role                     role{Role::None};
    std::string              name, email;            // le nom (la liste, sinon la cle) ; le courriel (son invitation)
    std::string              keyId, keyHex, fingerprint;   // la cle qui signe (l'editeur, ou l'admin) ; vides : aucune
    bool                     testKey{false};          // une cle d'ESSAI de l'editeur
    int                      listNumber{0};           // la liste des admins en vigueur sur ce poste (0 : aucune)
    int                      addedInList{0};          // la premiere liste vue qui porte sa cle (0 : inconnue)
    Seconds                  since{0}, retired{0};    // admin depuis ; retire le (0 : non)
    std::string              profile;                 // son profil d'origine (la liste)
    std::vector<std::string> rights;                  // ses droits (l'admin+ : les 13)
    std::string              listPc;                  // le PC que la liste lui connait
    Seconds                  joinedAt{0};             // 1.51.0 : inscrit par son code d'invitation le (inscription-admin.json)
    std::string              invitationId;            // 1.51.0 : l'invitation qui l'a inscrit (inv-...) ; vide : inconnue
    std::string              pc, machine;             // CE PC : son nom, l'identifiant de la machine
    std::vector<AccountStep> steps;                   // un admin : les listes vues ici, de la plus ancienne a la plus recente
};
// Ce qu'a fait refreshAdminState / refreshAdminList.
struct AdminRefresh {
    bool        listAdopted{false};     // une liste plus recente est gardee (admins.xpgadm)
    int         listNumber{0};
    int         journalNew{0};          // admin+ : les lignes du journal relevees
    bool        admitted{false};        // en cours d'ajout : la cle est dans la liste signee (ce poste est admin)
    bool        syncKeyReceived{false}; // la cle de synchro recue par le trousseau
    std::string joinState;              // 1.51.0, en cours d'ajout : "inscrit", "admis", "retire", "aucune"
    std::vector<std::string> invitedAdmitted;   // 1.51.0, l'admin+ : les admins inscrits par leur code, signes d'office (leurs noms)
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

// ---- 1.44.0 : les fichiers, les messages a un PC, les versions, la sauvegarde complete (WorkspaceVersions.cpp) ----
// L'avancement d'un envoi (les octets deja sur le service, sur le total) ; faux : arreter (l'envoi reprend ensuite la ou il
// s'est arrete : le meme manifeste, les morceaux qui manquent).
using Progress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

// Un fichier depose sur le service par ce poste (fichiers-envoyes.json : reprendre un envoi, renvoyer apres une restauration).
struct SentFile {
    licensing::admins::FileManifest manifest;
    std::string                     block;      // le manifeste signe (le meme texte reprend l'envoi)
    std::string                     path;       // le fichier sur ce poste
    bool                            complete{false};
    Seconds                         sentAt{0};
};

// Un PC a qui ecrire (les activations, les demandes, les licences en ligne que connait ce poste).
struct MessagePc {
    std::string deviceId;      // l'identifiant du poste sur le service (32 hexadecimaux : service::deviceId)
    std::string deviceKey;     // sa cle (64 hexadecimaux) : le message est scelle pour elle
    std::string name, customer, licenseId;
    std::string studio;        // << IHM Studio 1.43.0 >> (sa derniere demande) ; vide : inconnu
    std::string studioVersion; // << 1.43.0 >> ; vide : inconnue
    Seconds     lastSeen{0};
};

// Une version publiee (GET /admin/versions) et ses fichiers (encore sur le service ? connus sur ce poste ?).
struct VersionFileState {
    licensing::admins::VersionFile file;
    bool                           present{false};   // entier sur le service
    std::string                    localPath;        // le fichier envoye par ce poste (fichiers-envoyes.json) ; vide : inconnu
};
struct PublishedVersion {
    std::string                   version;
    Seconds                       published{0};
    bool                          important{false};
    std::vector<std::string>      notes;
    std::vector<VersionFileState> files;
};
struct SoftwareSeen {   // un admin (ou l'admin+) et l'Admin qu'il a
    std::string key, name, software, version;
    Seconds     seen{0};
    bool        upToDate{false};   // la derniere version publiee (ou plus recente)
};
struct VersionsView {
    std::vector<PublishedVersion>                             versions;     // la plus recente d'abord
    std::map<std::string, int>                                byVersion;    // "1.44.0" -> nombre de cles (PC et admins)
    std::map<std::string, std::map<std::string, int>>         bySoftware;   // "IHM-Studio" -> "1.44.0" -> nombre
    std::vector<SoftwareSeen>                                 admins;
    int                                                       total{0};
    int                                                       upToDate{0};  // ceux de la derniere version (ou plus recente)
    [[nodiscard]] std::string latest() const { return versions.empty() ? std::string() : versions.front().version; }
};
// Un fichier d'une version publiee dont le service n'a plus les octets (une restauration ailleurs) : a renvoyer.
struct FileToResend {
    licensing::admins::VersionFile file;
    std::string                    version;
    std::string                    localPath;    // le fichier envoye par ce poste, s'il est encore la ; vide : a choisir
};

// La sauvegarde complete (docs/ADMINS.md, par. 9).
struct BackupSettings {
    bool        configured{false};   // un dossier et la cle derivee de la phrase (sauvegarde.cle, protegee pour ce compte)
    std::string folder;
    Seconds     last{0};
    std::string lastPath;
    std::string problem;             // pourquoi elle n'est pas prete (vide : prete)
};
struct BackupInfo {   // l'en-tete d'un .xpgsauve (sans la phrase)
    std::string keyId, serviceUrl, params;
    Seconds     created{0};
};
struct RestoreOptions {
    // Faux : seulement le service (le poste de l'admin+ est intact, son dossier plus recent que la sauvegarde ne recule pas) ;
    // la cle de l'editeur de ce poste doit etre celle de la sauvegarde.
    bool        restoreFolder{true};
    bool        replaceKey{false};       // une AUTRE cle de l'editeur est deja dans le dossier : mise de cote (jamais effacee)
    bool        restoreService{false};   // << Remettre aussi le service >>
    std::string serviceUrl;              // son adresse (un nouveau deploiement) ; vide : celle de la sauvegarde
    bool        replaceService{false};   // le service n'est pas vide (409 pas_vide) : le remplacer
};
// 1.49.0 : repartir de zero (WorkspaceReset.cpp ; docs/ADMINS.md, par. 13).
inline constexpr const char* kStartOverWord = "REPARTIR";
struct StartOverReport {
    std::string                                       backupPath;      // la sauvegarde complete faite d'abord
    std::string                                       backupSha256;
    std::vector<std::pair<std::string, std::int64_t>> erased;          // le service : les lignes effacees, table par table
    int                                               adminsNumber{0}, revocationsNumber{0}, registryNumber{0};
    int                                               localFiles{0};   // les fichiers effaces sur ce poste
    bool                                              archiveSent{false};
};
struct RestoreReport {
    int                                                files{0};          // les fichiers du dossier remis
    bool                                               editorKey{false};  // la cle de l'editeur remise (ou deja la, la meme)
    bool                                               syncKey{false};
    int                                                codes{0};          // les codes d'activation gardes de nouveau
    bool                                               serviceRestored{false};
    bool                                               serviceNotEmpty{false};   // 409 pas_vide : recommencer avec replaceService
    std::vector<std::pair<std::string, std::uint64_t>> tables;            // les lignes remises, table par table (le service)
    int                                                filesToResend{0};  // fichiers_a_renvoyer
    std::string                                        serviceKey;
};

// ---- 1.46.0 : la boutique et les commandes (WorkspaceShop.cpp ; docs/BOUTIQUE.md) ----
// Une commande livree (ou pas) par deliverOrders.
struct OrderDelivery {
    int         number{0};
    int         line{0};            // 1.48.0 : la ligne de la commande
    bool        delivered{false};
    bool        review{false};      // mise << a revoir >> (la raison : problem)
    std::string licence, customer, article, problem;
};
struct DeliveryReport {
    std::vector<OrderDelivery> lines;
    int                        delivered{0}, review{0}, skipped{0};
    bool                       autoOff{false};   // automatique : << Livrer seul >> est decoche (rien n'a ete livre)
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
    // 1.48.1 : SUPPRIMER des licences qui ne servent plus (Licences > Supprimer, une ou plusieurs ; WorkspaceDelete.cpp) :
    // chacune revoquee OU finie - une seule encore en vigueur et rien n'est fait. Leurs fichiers vont dans la corbeille (la
    // synchro ne les ramene pas), leurs PC quittent Activations, le client garde son histoire ; une ligne au journal des
    // admins (licence_supprimer) par licence ; leurs revocations restent dans la liste signee. Celles EN LIGNE : effacees
    // aussi du service (takeServiceJob : DeleteLicences, la coquille la lance), et cachees des releves d'ici la.
    OpResult deleteLicences(const std::vector<std::string>& licenseIds);
    // Pourquoi cette licence ne se supprime pas (le bouton grise, son infobulle) ; vide : elle se supprime.
    [[nodiscard]] std::string whyNotDeleteLicence(const LicenceEntry& e) const;
    // LENT : effacer ces licences du service (POST /admin/licences/supprimer ; le droit << licence_revoquer >>).
    [[nodiscard]] OpResult serviceDeleteLicences(const std::vector<std::string>& licenseIds) const;
    // 1.48.1 : SUPPRIMER des PC de la liste (Activations > Supprimer ; WorkspaceDelete.cpp) : un PC qui ne sert plus
    // (revoque, sa licence finie ou supprimee) ou une demande notee restee sans suite. Un PC actif : refuse (retire-le de
    // sa licence, ou revoque-le). Retenus dans pc-supprimes.json : un PC revient s'il a une activite plus recente (une
    // nouvelle demande, une licence). Le registre des activations (l'Historique) n'est pas touche ; une ligne au journal
    // des admins (pc_supprimer).
    OpResult deleteDevices(const std::vector<std::string>& deviceKeys);
    [[nodiscard]] std::string whyNotDeleteDevice(const KnownDevice& d) const;
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
    // 1.51.0 : une cle d'admin INSCRITE par son code d'invitation (inscription-admin.json, ou une demande d'avant la
    // 1.51) ; faux : une cle sans code accepte (un code refuse) - Pending ne dit alors pas << ajout en cours >>.
    [[nodiscard]] bool joined() const noexcept { return joined_; }
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

    // ---- devenir admin : LE CODE D'INVITATION (1.51.0, docs/ADMINS.md par. 4 ; un poste sans cle de l'editeur ; LENT) ----
    // XPG Admin : cree la cle d'admin s'il le faut et l'INSCRIT avec le code (POST /v1/admins/inscription, signee par
    // elle) : le service sert le code (48 h, une seule fois) et note la cle d'un coup ; l'Admin+ la signe d'office dans
    // la liste avec le profil choisi a l'invitation - plus de demande, ni d'empreinte, ni de 6 chiffres. Le nom et le
    // courriel sont ceux de l'invitation. `refused` vrai : le code est refuse (mal ecrit ; le service : << Ce code
    // d'invitation est inconnu. >>, << ... a expire (48 h). >>, << ... a deja servi. >>) : a retaper. invitation.txt est
    // efface des que le service a repondu. Deja admin : reussi, rien a faire.
    [[nodiscard]] OpResult joinWithInvitation(const std::string& invitation, bool* refused = nullptr) const;
    // Le code d'invitation pose par l'installateur (%LOCALAPPDATA%\XPG\LicenseAdmin\invitation.txt : le dossier de
    // l'Admin), remis en forme ; vide : pas de fichier (ou pas un code).
    [[nodiscard]] std::string pendingInvitationCode() const;
    // Ou en est la cle de ce poste (GET /v1/admins/inscription) : "inscrit", "admis", "retire", "aucune".
    [[nodiscard]] OpResult joinState(std::string& state) const;
    // Relever la liste des admins (GET /v1/admins) et la garder si elle est plus recente ; un admin accepte : les cles de
    // l'editeur retenues, la cle de synchro recue par le trousseau.
    [[nodiscard]] OpResult refreshAdminList(AdminRefresh* out = nullptr) const;
    // Le regard du service (Shell, toutes les 15 s) : `etat` (GET /admin/etat, serviceWatch) ou nul (en cours d'ajout :
    // l'inscription est regardee). Une liste plus recente est gardee, le journal releve (admin+), le journal a envoyer
    // part ; 1.51.0, l'admin+ : les admins inscrits par leur code sont signes d'office (admitInvitedAdmins).
    [[nodiscard]] OpResult refreshAdminState(const online::AdminWatch* etat, AdminRefresh* out = nullptr) const;
    // Prendre la cle de synchro dans le trousseau (GET /admin/trousseau, ouverte par la cle d'admin).
    [[nodiscard]] OpResult receiveSyncKey() const;

    // ---- gerer les admins (l'admin+ seulement ; LENT : par runSlow, puis reload) ----
    [[nodiscard]] OpResult serviceAdmins(AdminsView& out) const;
    // 1.49.0 (docs/ADMINS.md, par. 12) : creer une invitation par codes (POST /admin/admins/invitation ; l'admin+) - le code
    // (INV-XXXX-XXXX, 48 h, une seule fois) rendu dans `code` (montre une fois, jamais garde), l'invitation notee
    // (invitations-admins.json, sans le code), une ligne << admin_inviter >> au journal. 1.51.0 : ses droits sont ceux du
    // profil (`rights` s'ils sont donnes), notes avec elle : la cle qui servira le code sera admin avec eux, d'office.
    [[nodiscard]] OpResult inviteAdmin(const std::string& name, const std::string& email, const std::string& profile, std::string& code,
                                       AdminInvitation* noted = nullptr, const std::vector<std::string>* rights = nullptr) const;
    // 1.51.0 : SIGNER D'OFFICE LES ADMINS INSCRITS PAR LEUR CODE (GET /admin/admins/inscrits ; l'admin+, jamais XPG Admin).
    // Chaque inscrit dont l'invitation a ete creee SUR CE POSTE (invitations-admins.json), pas deja servie par une autre
    // cle : la cle de l'editeur signe UNE liste (numero + 1) qui les porte, avec les droits notes a l'invitation ; le
    // trousseau de chacun ; une ligne << admin_ajouter >> par admin (code d'invitation, invite par). Les autres : jamais
    // signes (dit dans les details). `names` : ceux qui sont admins. Aucun inscrit : reussi, rien de signe.
    [[nodiscard]] OpResult admitInvitedAdmins(std::vector<std::string>* names = nullptr) const;
    // Les invitations de l'admin+ (invitations-admins.json, les plus recentes d'abord) ; en noter une (<< Ajouter un
    // admin... >>, a l'envoi de l'Admin ; 100 gardees).
    [[nodiscard]] std::vector<AdminInvitation> adminInvitations() const;
    OpResult noteAdminInvitation(const AdminInvitation& invitation) const;
    // 1.47.0 : << Mon compte >> : qui est ce poste (sans reseau).
    [[nodiscard]] AccountInfo account() const;
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

    // ======================================= 1.44.0 : fichiers, messages, versions, sauvegarde (WorkspaceVersions.cpp) ==
    // Tout ce qui parle au service est LENT (runSlow, puis reload) ; reserve a l'admin+ (docs/ADMINS.md, par. 1), sauf la
    // restauration (un Admin neuf) et les adresses de secours (le droit << offres >>).
    // ---- les fichiers ----
    // Deposer un fichier (SHA-256 lu en flux, le manifeste << fichier >> signe par la cle de l'editeur, puis les morceaux de
    // 1 Mio qui manquent). `kind` : "admin", "studios", "autre" ; `access` : "public", "admins", "jeton". Repris : le meme
    // fichier (meme contenu, meme sorte, meme acces) reprend son manifeste et n'envoie que ce qui manque. `id` : l'identifiant.
    [[nodiscard]] OpResult uploadFile(const std::string& path, const std::string& kind, const std::string& version, const std::string& access,
                                      std::string& id, const Progress& progress = {}) const;
    // Un lien vers un fichier entier (POST /admin/fichiers/jeton) : `seconds` de 60 a 172800 (48 h, le plus que permet le
    // service) ; `once` : un seul telechargement. `token` : 32 hexadecimaux (le service n'en garde que l'empreinte).
    [[nodiscard]] OpResult fileToken(const std::string& id, int seconds, bool once, std::string& token, Seconds* expires = nullptr) const;
    // L'adresse a donner (un navigateur) : <service>/v1/fichiers/lien?jeton=...
    [[nodiscard]] std::string fileLink(const std::string& token) const;
    [[nodiscard]] std::vector<SentFile> sentFiles() const;

    // ---- les messages a un PC ----
    // Les PC connus de ce poste (le choix << Dans les notifications de son Studio >>), par client puis par nom.
    [[nodiscard]] std::vector<MessagePc> pcsForMessages() const;
    // Un message (genre << message >>, signe par la cle de l'editeur) a un PC connu (`device` : son identifiant ou sa cle) :
    // le texte, les fichiers joints et l'action (<< admin >> : installer l'Admin pour devenir admin) scelles pour SA cle ;
    // PUT /admin/messages. `number` : son numero sur le service.
    [[nodiscard]] OpResult sendMessageToPc(const std::string& device, const std::string& title, const std::string& text,
                                           const std::vector<licensing::admins::MessageFile>& attachments, const std::string& action,
                                           int* number = nullptr) const;
    // Un fichier a joindre a un message : depose (acces << jeton >>), un lien de 48 h (plusieurs telechargements).
    [[nodiscard]] OpResult attachFile(const std::string& path, licensing::admins::MessageFile& out, const Progress& progress = {}) const;
    // << Ajouter un admin >> : l'installateur de l'Admin de la derniere version publiee (sinon `fallbackPath`, depose pour
    // l'occasion), envoye dans les notifications du Studio de ce PC (l'action << admin >>) ; ou par un LIEN (48 h, une fois).
    // 1.49.0 : `invitationCode` (s'il y en a un) va dans le message : << Ton code d'invitation : INV-... (a saisir dans
    // l'installateur, valable 48 h) >>.
    [[nodiscard]] OpResult sendAdminInstaller(const std::string& device, const std::string& adminName, const std::string& fallbackPath,
                                              const Progress& progress = {}, const std::string& invitationCode = {}) const;
    [[nodiscard]] OpResult adminInstallerLink(const std::string& fallbackPath, std::string& url, const Progress& progress = {},
                                              const std::string& invitationCode = {}) const;

    // ---- les versions ----
    // La version d'un installateur Windows (<< 1.44.1 >>) lue dans sa ressource VS_VERSIONINFO (ProductVersion, sinon
    // FileVersion : VersionInfoProductVersion / VersionInfoVersion des .iss), sans jamais le lancer. Vide : `why`.
    [[nodiscard]] static std::string readInstallerVersion(const std::string& path, std::string* why = nullptr);
    // 1.46.1 : un .zip qui porte un installateur (API-IHM-Studio-Setup-*.exe, XPG-License-Admin-Setup-*.exe) : CE fichier
    // seul extrait dans un dossier temporaire (`out` : son chemin), puis tout se passe comme avec le .exe. Refuse (faux,
    // `why`) : pas un zip, aucun installateur attendu, plusieurs, un chemin qui sort du dossier (.., absolu), une entree
    // chiffree, une taille absurde (plus de 1 Gio, ou plus de 1000 fois sa taille compressee). `admin` : l'Admin, sinon les
    // Studios. installerKindsInZip : quels installateurs il porte (le glisser decide de la zone).
    [[nodiscard]] static bool installerFromZip(const std::string& zipPath, bool admin, std::string& out, std::string* why = nullptr);
    [[nodiscard]] static bool installerKindsInZip(const std::string& zipPath, bool& studios, bool& admin, std::string* why = nullptr);
    // 1.49.0 : trois installateurs - `kind` 1 : les Studios (API-IHM-Studio-Setup), 2 : l'Admin+ (XPG-License-Admin-Setup ; c'est
    // le << admin >> des deux fonctions d'avant), 3 : XPG Admin (XPG-Admin-Setup, l'Admin classique).
    [[nodiscard]] static bool installerFromZip(const std::string& zipPath, int kind, std::string& out, std::string* why = nullptr);
    [[nodiscard]] static bool installerKindsInZip(const std::string& zipPath, bool& studios, bool& plus, bool& classic, std::string* why = nullptr);
    [[nodiscard]] static bool isZipPath(const std::string& path);
    // Les versions publiees et qui est a jour (GET /admin/versions).
    [[nodiscard]] OpResult versions(VersionsView& out) const;
    // Publier : les deux installateurs, de la meme version, plus recente que la derniere publiee ; deposes (les Studios :
    // public ; l'Admin : les admins), puis le bloc << version >> signe (PUT /admin/versions). `notes` : 40 au plus.
    // 1.49.0 : `adminExe` est XPG Admin (sorte << admin >>, pour les admins) ; `adminPlusExe` (facultatif) l'Admin+ (sorte
    // << admin_plus >> : la cle de l'editeur seule le telecharge, jamais propose a un admin ni a un client).
    [[nodiscard]] OpResult publishVersion(const std::string& studiosExe, const std::string& adminExe, const std::vector<std::string>& notes,
                                          bool important, const Progress& progress = {}, const std::string& adminPlusExe = {}) const;
    // Les notes de version a proposer pour cette version (vide : l'Admin n'embarque pas les notes des Studios ; a taper).
    [[nodiscard]] std::vector<std::string> suggestedNotes(const std::string& version) const;
    // Apres une restauration ailleurs : les fichiers des versions publiees dont le service n'a plus les octets, et les renvoyer.
    [[nodiscard]] OpResult filesToResend(std::vector<FileToResend>& out) const;
    [[nodiscard]] OpResult resendFile(const std::string& id, const std::string& path, const Progress& progress = {}) const;

    // ---- les adresses de secours (le registre : fallback_services ; l'admin+ ou le droit << offres >>) ----
    [[nodiscard]] std::vector<std::string> fallbackServices() const;
    // Un registre numero + 1 avec ces adresses (3 au plus, https, sans doublon ni l'adresse principale), signe et range ; le
    // depot sur le service part par takeServiceJob comme toute signature du registre.
    OpResult setFallbackServices(const std::vector<std::string>& urls);

    // ---- la sauvegarde complete (docs/ADMINS.md, par. 9 ; l'admin+) ----
    [[nodiscard]] BackupSettings backupSettings() const;
    // Le dossier des sauvegardes et la phrase (LENT : Argon2id) : la cle derivee et son sel gardes proteges pour ce compte
    // (sauvegarde.cle) ; la phrase n'est jamais gardee. `strength` : les essais l'allegent.
    [[nodiscard]] OpResult configureBackup(const std::string& folder, const std::string& passphrase, const KeyBackupOptions& strength = {}) const;
    // Faut-il la sauvegarde du jour (reglee, l'admin+, pas encore faite aujourd'hui) ? L'application l'appelle.
    [[nodiscard]] bool backupDueToday() const;
    // LENT : sauvegarde-licences-AAAA-MM-JJ-HHMM.xpgsauve (le service entier, le dossier du poste, la cle de l'editeur),
    // les 30 dernieres gardees. `path` : le fichier ecrit.
    [[nodiscard]] OpResult backupNow(std::string* path = nullptr) const;
    // L'en-tete (sans la phrase) : la cle, l'heure, le service.
    [[nodiscard]] static OpResult describeBackup(const std::string& path, BackupInfo* info = nullptr);
    // LENT : restaurer (la phrase ; le dossier du poste, la cle de l'editeur protegee pour CE compte, la cle de synchro, les
    // codes ; puis, si demande, le service : debut, lots, fin). Ensuite reload().
    [[nodiscard]] OpResult restoreBackup(const std::string& path, const std::string& passphrase, const RestoreOptions& options,
                                         RestoreReport* report = nullptr) const;

    // ======================================================== 1.49.0 : repartir de zero (WorkspaceReset.cpp ; docs/ADMINS.md, par. 13) ==
    // L'admin+ seul (jamais XPG Admin, l'edition classique), jamais d'elle-meme. LENT : `confirmation` doit etre << REPARTIR >>
    // (kStartOverWord) ; la sauvegarde complete d'abord (backupNow ; pas reglee : refuse), puis le service (POST
    // /admin/reinitialiser : une liste des admins vide et une liste de revocation vide, signees, numero + 1 ; le registre
    // re-signe s'il est signe par un admin), puis le dossier de ce poste ; l'archive en ligne renvoyee. Ensuite reload().
    [[nodiscard]] OpResult startOver(const std::string& confirmation, StartOverReport* report = nullptr) const;
    // Ce que la fenetre dit : ce qui est efface (le service et ce poste), ce qui est garde.
    [[nodiscard]] static const std::vector<std::string>& startOverErased();
    [[nodiscard]] static const std::vector<std::string>& startOverKept();
    // Pourquoi ce poste ne peut pas repartir de zero (le bouton grise) ; vide : il peut.
    [[nodiscard]] std::string whyNotStartOver() const;
    // Le bandeau du premier lancement de l'Admin+ 1.49 (UNE fois) : du ? (l'admin+, une 1.49, jamais propose, quelque chose
    // a effacer) ; puis note (repartir-de-zero.json) des qu'il est montre.
    [[nodiscard]] bool startOverOfferDue() const;
    void noteStartOverOffered() const;

    // ======================================================== 1.46.0 : la boutique et les commandes (WorkspaceShop.cpp) ==
    // LENT (le service, signe par la cle qui signe) : le reglage de la boutique (l'admin+ et un admin le lisent ; seul
    // l'admin+ l'enregistre) ; les commandes (le droit << demandes >>).
    [[nodiscard]] OpResult shopSettings(shop::Settings& out) const;
    [[nodiscard]] OpResult saveShop(const shop::Settings& settings) const;
    [[nodiscard]] OpResult shopOrders(shop::OrdersView& out) const;
    // LENT : LIVRER - `number` 0 : toutes les commandes payees (`automatic` : seulement si << Livrer seul >> est coche, et
    // celles que cet Admin a le droit de livrer) ; sinon cette commande (payee, ou a revoir). Chaque commande : prise sur le
    // service, son scelle ouvert (la demande du PC), la licence en ligne de l'offre signee ici (ou l'option ajoutee a la
    // licence du client), deposee, le code scelle pour le PC, le journal, puis << livree >> ; un echec qui ne passera pas
    // seul : << a revoir >> et la raison. Le travail se fait dans un Workspace neuf relu du dossier (ce Workspace-ci n'est
    // pas touche : reload() ensuite). Les livraisons commencees sont gardees (boutique-livraisons.json) : une reprise ne
    // signe pas une seconde licence.
    [[nodiscard]] OpResult deliverOrders(int number, bool automatic, DeliveryReport* report = nullptr) const;
    // 1.48.0 : le panier - chaque ligne se valide a part : `line` 0, toutes les lignes a valider de la commande (automatique :
    // les payees seulement) ; une ligne, elle seule. Une option << avec_ligne >> attend que sa ligne de licence soit livree.
    [[nodiscard]] OpResult deliverOrders(int number, int line, bool automatic, DeliveryReport* report = nullptr) const;
    // LENT : une ligne refusee (la raison, lue par le client dans Mes achats ; le droit << demandes >>).
    [[nodiscard]] OpResult refuseLine(int number, int line, const std::string& reason) const;
    // LENT : << a revoir >> avec la raison (le droit << demandes >>) ; rembourser (l'admin+ : Stripe, par le service).
    [[nodiscard]] OpResult reviewOrder(int number, const std::string& reason) const;
    [[nodiscard]] OpResult refundOrder(int number) const;
    // 1.48.1 : LENT (l'admin+) - UN article rembourse (la ligne `line` de la commande `number`) ; `what` : l'article et son
    // montant, pour le journal des admins.
    [[nodiscard]] OpResult refundLine(int number, int line, const std::string& what = {}) const;
    // 1.48.1 : LENT (le droit << demandes >>) - SUPPRIMER des commandes sans argent (shop::orderDeletable) et des demandes
    // de licence closes (online::requestDeletable), sur le service puis ici ; une ligne au journal des admins pour
    // chacune. `what` : ce qui part, pour le journal (une ligne par numero). Le compte rendu dit ce qui est parti et ce qui
    // reste (et pourquoi).
    [[nodiscard]] OpResult deleteOrdersAndRequests(const std::vector<int>& orders, const std::vector<int>& requests,
                                                   const std::map<std::string, std::string>& what = {}) const;
    // LENT : BRANCHER STRIPE (l'admin+) - `secretKey` (collee par l'editeur) est effacee quoi qu'il arrive ; le webhook cree
    // chez Stripe, les deux secrets poses sur le Worker par le kit (deployer::putSecret). Jamais ecrite ici.
    [[nodiscard]] OpResult connectStripe(std::string& secretKey, const deployer::Options& kit = {}, const deployer::LineSink& onLine = {},
                                         const std::atomic<bool>* cancel = nullptr) const;
    // Pourquoi cet Admin ne peut pas livrer une commande de ce genre ("edition", "option") ; vide : il peut.
    [[nodiscard]] std::string whyNotDeliver(const std::string& kind) const;
    // Ce que l'application a releve en dernier (sur le fil de l'interface) : le reglage, les commandes, celles a livrer
    // (GET /admin/etat : a_livrer ; -1 : inconnu).
    void adoptShop(const shop::Settings& settings) { shop_ = settings; hasShop_ = true; }
    void adoptOrders(shop::OrdersView view) { orders_ = std::move(view); hasOrders_ = true; ordersToDeliver_ = orders_.toDeliver; }
    void setOrdersToDeliver(int n) { ordersToDeliver_ = n; }
    [[nodiscard]] bool hasShop() const noexcept { return hasShop_; }
    [[nodiscard]] const shop::Settings& shop() const noexcept { return shop_; }
    [[nodiscard]] bool hasOrders() const noexcept { return hasOrders_; }
    [[nodiscard]] const shop::OrdersView& orders() const noexcept { return orders_; }
    [[nodiscard]] int ordersToDeliver() const noexcept { return ordersToDeliver_; }
    // La version du service sait-elle la boutique (l'etat lu) ? Faux aussi quand elle n'est pas encore lue.
    [[nodiscard]] bool serviceKnowsShop() const;

    // ======================================================== 1.47.0 : les statistiques et le globe (WorkspaceStats.cpp) ==
    // LENT (le service, signe par la cle qui signe : l'admin+ ou un admin ; docs/STATISTIQUES.md) - a lancer par Background.
    // Un service d'avant la 1.47.0 : refuse avec la phrase << deploie la 1.47.0 >> (stats::serviceTooOldText). `raw`
    // (facultatif) : la reponse du service telle quelle.
    [[nodiscard]] OpResult statsSeries(const stats::SeriesQuery& q, stats::SeriesReply& out, std::string* raw = nullptr) const;
    [[nodiscard]] OpResult statsUpdates(const stats::UpdatesQuery& q, stats::UpdatesReply& out, std::string* raw = nullptr) const;
    [[nodiscard]] OpResult statsAdmins(const stats::Period& period, stats::AdminsReply& out, std::string* raw = nullptr) const;
    [[nodiscard]] OpResult statsGlobe(int periodDays, stats::GlobeReply& out, std::string* raw = nullptr) const;
    [[nodiscard]] OpResult statsLive(Seconds since, stats::LiveReply& out, std::string* raw = nullptr) const;
    // L'admin+ : le releve d'aujourd'hui, s'il n'est pas encore pris.
    [[nodiscard]] OpResult statsSnapshot(stats::SnapshotReply& out, std::string* raw = nullptr) const;
    // Sans reseau (l'etat du service deja lu) : la version du service sait-elle les statistiques ? La phrase a montrer
    // sinon (<< Ton service en ligne est en 1.46.0 : deploie la 1.47.0 pour les statistiques et le globe. >>) ; vide : il
    // les sait, ou son etat n'est pas encore lu.
    [[nodiscard]] bool serviceKnowsStats() const;
    [[nodiscard]] std::string statsServiceNote() const;
    // 1.48.1 : LA COHERENCE (docs/STATISTIQUES.md, par. 15). REEVALUER (LENT) : dire au service ce qui est supprime sur ce
    // poste (les clients et les licences de la corbeille, si ce poste a le droit de les supprimer), lui faire remettre ses
    // statistiques d'accord avec ce qui existe ; une ligne au journal des admins (stats_reevaluer). `out` : les corrections.
    [[nodiscard]] OpResult statsReevaluate(stats::ReevaluateReply& out, std::string* raw = nullptr) const;
    // Un client ou une licence supprimes (ou restaures) ici : note pour le service, envoye avant la prochaine lecture des
    // statistiques (sans reseau ; stats-a-envoyer.json). Toutes les vues se relisent (changes::bump).
    void queueStatsExclusion(const stats::Exclusions& ex) const;
    [[nodiscard]] stats::Exclusions pendingStatsExclusions() const;

    // ============================ 1.47.0 : gerer le service (l'admin+ seul ; la derogation) - WorkspaceDerogation.cpp ==
    // (docs/ADMINS.md, par. 11) Gerer le service : le mettre en ligne ou a jour, changer son adresse, ses adresses de
    // secours, le releve du jour ; certifier, brancher Stripe (sa cle Stripe), la sauvegarde et la restauration restent a
    // l'admin+ (sa cle).
    // L'admin+, ou un admin dont la derogation court (a l'heure du service estimee).
    [[nodiscard]] bool mayManageService() const;
    // Vide : permis ; sinon la phrase (<< L'admin+ seul gere le service (derogation possible). >>).
    [[nodiscard]] std::string whyNotService() const;
    [[nodiscard]] const Derogation& derogation() const noexcept { return derogation_; }
    // Ce que l'application a relu (GET /admin/etat, GET /admin/derogation) ; la coquille l'adopte sur le fil de l'interface.
    void adoptDerogation(const Derogation& d);
    void adoptDerogation(const online::AdminWatch& watch);
    // LENT. L'admin+ : accorder a un admin de la liste (une nouvelle remplace l'ancienne), revoquer celle en place (en
    // attente du code ou en cours). Une ligne au journal (derogation_accorder, derogation_revoquer).
    // 1.52.0 : le format 2 - << tout >>, `duration` de 10 min a 72 h (secondes), elle commence quand l'admin tape le code
    // (dans l'heure) ; le code n'est ni dans le resultat ni au journal : derogationCode(id) le retire de la cle.
    [[nodiscard]] OpResult grantDerogation(const std::string& adminKeyHex, Seconds duration, Derogation* out = nullptr) const;
    [[nodiscard]] OpResult revokeDerogation(Derogation* out = nullptr) const;
    // 1.52.0 : l'admin+ - le code de la derogation `id` (8 chiffres), retire de la cle de l'editeur de ce poste (rien
    // n'est garde) ; vide : pas l'admin+. A montrer a l'ecran seulement, jamais dans un journal.
    [[nodiscard]] std::string derogationCode(const std::string& id) const;
    // LENT. 1.52.0 : un admin tape le code que l'admin+ voit sur son PC (POST /admin/derogation/activer) ; `id` vide : sa
    // derogation relue d'abord. Refuse : la phrase du service (<< Code faux : il te reste 4 essais. >>, trop tard, annulee).
    [[nodiscard]] OpResult activateDerogation(const std::string& id, const std::string& typedCode, Derogation* out = nullptr) const;
    // LENT. Relire (l'admin+ : celle en place ; un admin : la sienne) ; son document verifie (la cle de l'editeur).
    [[nodiscard]] OpResult readDerogation(Derogation& out) const;
    // LENT. Un admin, AVANT un geste qui gere le service (`action` : deployer, connexion_cloudflare, adresse, secours,
    // releve) : le service dit si sa derogation court (il l'ecrit au journal ; l'admin+ est prevenu). L'admin+ :
    // toujours permis, sans appel. Refuse : la phrase du service (finie, revoquee, aucune).
    [[nodiscard]] OpResult useDerogation(const std::string& action, const std::string& detail = {}) const;
    // Les cles PUBLIQUES de l'editeur que le service croit (PUBLISHER_KEYS, GET /admin/admins) : ce que deployer.mjs passe
    // au Worker ; un admin sous derogation les ecrit dans cle-publique.txt (jamais la sienne). LENT.
    [[nodiscard]] OpResult serviceEditorKeys(std::vector<std::string>& out) const;
    [[nodiscard]] OpResult writeEditorKeysFile(const std::string& kitFolder) const;   // LENT : cle-publique.txt (un admin sous derogation)
    // ---- la connexion au service (ServiceConnection.hpp) ----
    // << Utiliser une connexion... >> : ce fichier (verifie : signe par l'editeur) devient l'adresse du service de ce poste.
    OpResult useConnectionFile(const std::string& path);
    // << Exporter la connexion... >> : le dernier certificat verifie (Verifier) dans ce fichier ; rien de secret.
    [[nodiscard]] OpResult exportConnection(const std::string& path) const;

private:
    // 1.47.0 : au chargement, aucune adresse reglee et pas l'admin+ : la connexion signee cherchee (connexion::places()).
    void discoverConnection();
    Derogation derogation_;
    // 1.46.0 : une commande livree par CE Workspace (deliverOrders le cree neuf).
    OpResult deliverHere(int number, int line, bool automatic, DeliveryReport* report);
    bool     deliverOne(online::Client& c, const shop::Order& order, const shop::Settings& settings, bool automatic, int line, DeliveryReport& rep);
    // 1.48.0 : une ligne (`ord` : la commande, ses champs d'article pris de la ligne ; `req` : son scelle ouvert ;
    // `cartLicence` : une option << avec_ligne >> - la licence livree par cette ligne). Faux : pas livree (dit dans rep).
    bool     deliverLine(online::Client& c, const shop::Order& ord, int line, const online::Entry& req, const std::string& cartLicence, DeliveryReport& rep,
                         std::string& licenseOut);
    void loadKey();
    // 1.48.1 : ce qui a ete supprime ne revient pas par un autre chemin (WorkspaceDelete.cpp) : les PC d'une licence ou
    // d'un client supprimes, les PC supprimes ; les licences supprimees dans le releve du service.
    [[nodiscard]] std::vector<KnownDevice> withoutDeleted(std::vector<KnownDevice> all) const;
    void dropDeletedFromSnapshot();
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
    bool                                        joined_{false};   // 1.51.0 : joined()
    std::optional<licensing::admins::AdminList> adminList_;
    std::vector<licensing::crypto::PublicKey>   editorKeys_;
    std::uint64_t                               alertedUpTo_{0};   // newAlerts : la derniere ligne deja dite
    bool                                        alertsPrimed_{false};
    // 1.46.0 : la boutique (adoptee par l'application ; reload() n'y touche pas)
    shop::Settings                              shop_;
    bool                                        hasShop_{false};
    shop::OrdersView                            orders_;
    bool                                        hasOrders_{false};
    int                                         ordersToDeliver_{-1};
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
