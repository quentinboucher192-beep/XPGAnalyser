// =============================================================================
//  license_admin/ArchiveSync.hpp - 1.35.0 : L'ARCHIVE PRIVEE DE L'EDITEUR ET SA
//  SYNCHRO PAR LE SERVICE DES LICENCES (l'outil de l'EDITEUR seulement ; jamais
//  livre a un client)
// -----------------------------------------------------------------------------
//  1.31.0 a 1.34.0 : l'archive se synchronisait avec un depot GitHub prive
//  (GitHubSync). 1.35.0 : plus aucun GitHub - elle passe par le service des
//  licences de l'editeur (son Worker Cloudflare, docs/SERVICE-LICENCES.md,
//  routes /admin/archive), avec les MEMES fichiers chiffres et les MEMES regles
//  de fusion.
//
//  L'ARCHIVE DU POSTE (dossier de la cle / archive) : le registre, la liste de
//  revocation, chaque licence (la derniere emission), et le registre des
//  activations (activations.jsonl, a cote). C'est la SOURCE CENTRALE de ce que
//  l'editeur a signe ; le service en garde une copie pour ses autres postes.
//
//  CE QUI N'Y VA JAMAIS : la cle privee de l'editeur, la cle de synchro, son
//  code de secours, un code d'activation. Ils restent sur le poste (DPAPI).
//
//  CE QUI EST CHIFFRE avant de partir : tout ce qui nomme un client ou un PC -
//  les licences, la liste de revocation (sa raison est un texte libre), le
//  registre des activations - avec la CLE DE SYNCHRO (XChaCha20-Poly1305,
//  libsodium) ; le chemin du fichier est lie au chiffre (un fichier deplace ne
//  se dechiffre plus). En clair : le registre (signe, sans donnee de client) et
//  l'index. Le service, ou qui le lirait, ne voit rien d'un client.
//
//  LA CLE DE SYNCHRO : 32 octets tires sur le premier poste, gardes par DPAPI ;
//  un CODE DE SECOURS (XPGS1-..., 55 caracteres et un controle) la redonne sur
//  un autre poste. Sans elle, les fichiers chiffres de l'archive en ligne sont
//  illisibles.
//
//  L'INDEX (index.xpgsync) : la liste des fichiers, l'empreinte SHA-256 de
//  chacun EN CLAIR et du texte depose, un compteur croissant, et un code
//  d'authentification (BLAKE2b, une cle tiree de la cle de synchro) : un fichier
//  modifie hors de l'outil est refuse ; une archive remise a un etat plus ancien
//  (le compteur recule) aussi. Les signatures (registre, revocations, licences)
//  sont verifiees en plus (la cle de ce poste, ou celles compilees dans l'outil).
//
//  UN ENVOI : POST /admin/archive avec la version lue (l'envoi optimiste) ; un
//  autre poste a envoye entre-temps (409) : l'outil relit, fusionne et
//  recommence (trois fois au plus).
//
//  LES REGLES DE FUSION : registre et revocations - le plus grand numero gagne ;
//  une licence - la plus recente (sa date de signature) gagne ; le registre des
//  activations - les lignes des deux cotes (aucune perdue, aucune en double).
//  Deux versions differentes d'une meme date : celle du poste reste, et c'est
//  dit (rien n'est ecrase en silence).
//
//  RESEAU : synchronize est BLOQUANT (l'application le lance par runSlow, la
//  console attend). Hors ligne, cle refusee : un refus en francais, jamais une
//  reussite inventee.
// =============================================================================
#pragma once

#include "../licensing/Admins.hpp"   // 1.44.0 : un document signe par un admin de la liste
#include "../licensing/KeyFile.hpp"
#include "../licensing/Time.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::archive {

using licensing::Seconds;
using SyncKey = std::array<std::uint8_t, 32>;

inline constexpr const char* kSyncKeyFile = "cle-synchro.cle";
inline constexpr const char* kArchiveDir = "archive";
inline constexpr const char* kIndexName = "index.xpgsync";
inline constexpr const char* kSeenFile = "archive-vu.json";        // la derniere synchro, le plus grand compteur vu
inline constexpr std::size_t kMaxRemoteFile = 1536u * 1024u;       // 1,5 Mio par fichier (la limite du service)
inline constexpr std::size_t kMaxPostBytes = 1800u * 1024u;        // un envoi (le service : 2 Mio au plus)

// ---- la cle de synchro (cle-synchro.cle : DPAPI, ce compte) ----
[[nodiscard]] bool hasSyncKey(const std::string& folder);
bool createSyncKey(const std::string& folder, SyncKey& out, std::string* why);   // refuse s'il y en a deja une
bool loadSyncKey(const std::string& folder, SyncKey& out, std::string* why);
// L'identifiant public de la cle ("xpgs-" + 16 hexadecimaux) : il ne la revele pas.
[[nodiscard]] std::string syncKeyId(const SyncKey& key);
// Le code de secours : "XPGS1-" puis 11 groupes de 5 (base32 de Crockford, la cle et 2 octets de controle).
[[nodiscard]] std::string recoveryCode(const SyncKey& key);
// Relire un code (tirets, espaces, minuscules, O/0 et I/L/1 toleres). Faux : `why`.
bool parseRecoveryCode(std::string_view code, SyncKey& out, std::string* why);
// Le code redonne la cle sur ce poste. Une AUTRE cle deja la : refuse (rien n'est remplace), sauf `replace` :
// l'ancienne est alors gardee a cote (cle-synchro.cle.ancienne-<date>), jamais effacee ; `kept` dit ou.
bool restoreSyncKey(const std::string& folder, std::string_view code, SyncKey& out, std::string* why, bool replace = false,
                    std::string* kept = nullptr);

// ---- chiffrer un fichier de l'archive ----
//  "XPG-CHIFFRE 1 <id de la cle>\n" puis le base64 (nonce 24 octets + chiffre) en lignes de 76.
[[nodiscard]] std::string seal(const SyncKey& key, std::string_view path, std::string_view plain);
bool openSealed(const SyncKey& key, std::string_view path, std::string_view sealed, std::string& plain, std::string* why);

// ---- l'archive locale (dossier de la cle / archive) ----
//  registre/registre.xpgreg, revocations/revocations.xpgrev, licences/<ID>.xpglic : le dernier de chaque.
[[nodiscard]] std::string archiveFolder(const std::string& folder);
// Ranger un fichier signe (registre, revocations ou licence ; une demande : refuse). La signature doit etre
// celle d'une cle de confiance (`trusted`). `what` : ce qui a ete fait ("ajoute", "remplace", "deja la",
// "plus ancien que celui de l'archive : garde l'actuel"). Faux : `why`.
bool archiveSigned(const std::string& folder, std::string_view bytes, const std::vector<licensing::crypto::PublicKey>& trusted,
                   std::string* what, std::string* why);
// 1.44.0 : la meme, et un document signe par un admin de la liste `admins` (sa signature, son droit pour ce genre ;
// retire : un document garde) est aussi range (docs/ADMINS.md, par. 5 : << l'archive accepte les documents signes par la
// cle de l'editeur ou un admin de la liste >>).
bool archiveSigned(const std::string& folder, std::string_view bytes, const std::vector<licensing::crypto::PublicKey>& trusted,
                   const licensing::admins::AdminList* admins, std::string* what, std::string* why);
// Les cles de confiance de l'outil : celle de `key` (si elle est la) et celles compilees (PublisherKey.hpp).
[[nodiscard]] std::vector<licensing::crypto::PublicKey> trustedKeys(const licensing::SigningKey* key);
// Le nom de fichier d'un identifiant de licence (lettres, chiffres, . _ - ; le reste devient _).
[[nodiscard]] std::string safeName(std::string_view licenseId);
// Le texte d'un fichier de l'archive (vide : absent ou illisible). `relative` : "registre/registre.xpgreg".
[[nodiscard]] std::string archivedText(const std::string& folder, const std::string& relative);

// ---- 1.38.0 : les licences SUPPRIMEES de l'archive (une licence revoquee, retiree de la liste) ----
//  Le fichier va dans la corbeille de l'outil (corbeille-licences/, jamais efface) et son identifiant est retenu
//  (licences-supprimees.json, a cote de l'archive) : la synchro ne le ramene pas de l'archive en ligne (qui en garde
//  une copie chiffree). Une emission PLUS RECENTE de la meme licence (re-signee apres) revient, elle. La revocation
//  reste dans la liste signee : les Studios la refusent toujours.
inline constexpr const char* kDeletedFile = "licences-supprimees.json";
inline constexpr const char* kTrashDir = "corbeille-licences";
struct DeletedLicence {
    std::string id;
    Seconds     issuedAt{0};    // l'emission supprimee (une plus recente revient)
    Seconds     deletedAt{0};
};
[[nodiscard]] std::vector<DeletedLicence> deletedLicences(const std::string& folder);
bool noteDeleted(const std::string& folder, const DeletedLicence& d, std::string* why);
// 1.42.0 : ne plus retenir une licence supprimee (un client restaure : ses licences reviennent dans l'archive).
bool forgetDeleted(const std::string& folder, const std::string& licenseId, std::string* why);
// Cette emission d'une licence a-t-elle ete supprimee sur ce poste ?
[[nodiscard]] bool isDeleted(const std::vector<DeletedLicence>& list, const std::string& licenseId, Seconds issuedAt);

// ---- 1.44.0 : LES REGLAGES PARTAGES DE L'ADMIN+ (docs/ADMINS.md, par. 2, 5, 6) ----
//  Des fichiers JSON du dossier de la cle, synchronises avec l'archive (scelles par la cle de synchro, comme les
//  licences) : les profils de droits, la criticite du journal des admins, les criticites changees a la main par ligne.
//  Chacun porte "maj" (l'heure de sa derniere ecriture, secondes Unix) : le plus recent gagne ; deux versions
//  differentes de la meme heure : celle du poste reste, et c'est dit. Une version d'avant la 1.44.0 les laisse tels
//  quels dans l'archive en ligne (un genre qu'elle ne connait pas).
inline constexpr const char* kProfilesFile = "profils-admins.json";
inline constexpr const char* kCriticalityFile = "criticite.json";
inline constexpr const char* kJournalNotesFile = "journal-notes.json";
[[nodiscard]] const std::vector<std::string>& sharedSettingsFiles();

// ---- la synchro (le service : /admin/archive) ----
struct SyncOptions {
    bool        receiveOnly{false};    // recevoir sans rien envoyer (un poste neuf, une restauration)
    bool        dryRun{false};         // dire ce qui serait recu et envoye, sans rien changer
    Seconds     now{0};
    std::string tool;                  // "XPG-License-Admin 1.3.0" (l'index le note : qui a envoye)
    // Les essais seulement : appele juste avant le premier envoi de chaque tentative (un autre poste envoie
    // entre-temps : le 409, puis la relecture et la fusion).
    std::function<void()> beforeSend;
    // 1.44.0 : plusieurs admins - les cles de l'editeur que ce poste croit (un admin n'a pas la cle de l'editeur : il
    // croit celles compilees et celles retenues) et la liste des admins en vigueur : un document signe par un admin de
    // la liste (avec son droit) est recu comme un document de l'editeur.
    std::vector<licensing::crypto::PublicKey>   editorKeys;
    std::optional<licensing::admins::AdminList> admins;
    std::string                                 software;   // X-XPG-Logiciel des appels (vide : celui de cette version)
};
struct SyncReport {
    bool                     ok{false};
    std::string              why;            // le refus (en francais), si !ok
    std::vector<std::string> received;       // une ligne par fichier recu (ou a recevoir, en dryRun)
    std::vector<std::string> sent;           // une ligne par fichier envoye (ou a envoyer)
    std::vector<std::string> warnings;       // les conflits, les fichiers refuses
    std::string              remoteInfo;     // "https://... (version 12), dernier envoi le ... par ..."
    std::uint64_t            version{0};     // la version de l'archive en ligne apres la synchro
    bool                     upToDate{false};
    bool                     preview{false};       // dryRun : rien n'a change, les listes disent ce qui serait fait
    int                      attempts{0};
    [[nodiscard]] std::string text() const;
};
// Lit l'archive en ligne, fusionne dans le poste ce qui est plus recent, puis envoie ce qu'elle n'a pas.
// `key` : la cle de l'editeur (elle signe les appels au service ; ses signatures sont aussi verifiees). 1.44.0 : ou la
// cle d'un admin de la liste (options.editorKeys, options.admins : ce qu'il croit en plus de sa cle).
[[nodiscard]] SyncReport synchronize(const std::string& folder, const licensing::SigningKey& key, const SyncOptions& options);

// La derniere synchro reussie de ce poste (archive-vu.json) : 0 si jamais.
struct LastSync {
    Seconds       at{0};
    std::uint64_t version{0};
    std::string   url;
};
[[nodiscard]] LastSync lastSync(const std::string& folder);

// Pour les essais : les lignes du registre des activations qui manquent a `local` (dans l'ordre de `remote`).
[[nodiscard]] std::vector<std::string> missingLines(std::string_view local, std::string_view remote);

} // namespace license_admin::archive
