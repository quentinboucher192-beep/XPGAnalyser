// =============================================================================
//  license_admin/GitHubSync.hpp - 1.31.0 : LA SYNCHRO DES LICENCES AVEC UN DEPOT
//  GITHUB PRIVE (l'outil de l'EDITEUR seulement ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Le depot prive est la SOURCE CENTRALE de ce que l'editeur a signe : le
//  registre, la liste de revocation, chaque licence, et le registre des
//  activations. Un poste Admin y PUBLIE ce qu'il a, et en RECOIT ce qu'un autre
//  poste (ou lui-meme, avant une panne) y a mis.
//
//  CE QUI N'Y VA JAMAIS : la cle privee de l'editeur, le jeton GitHub, la cle de
//  synchro. Ils restent sur le poste, proteges par DPAPI (le compte Windows).
//
//  CE QUI Y EST CHIFFRE : tout ce qui nomme un client ou un PC - les licences
//  (.xpglic : le client, ses PC), la liste de revocation (sa raison est un texte
//  libre) et le registre des activations - chiffre avec la CLE DE SYNCHRO
//  (XChaCha20-Poly1305, libsodium) ; le chemin du fichier est lie au chiffre (un
//  fichier deplace ne se dechiffre plus). En clair : le registre (signe, sans
//  donnee de client), l'index et un LISEZ-MOI.
//
//  LA CLE DE SYNCHRO : 32 octets tires sur ce poste (github-config), gardes par
//  DPAPI ; un CODE DE SECOURS (XPGS1-..., 55 caracteres et un controle) la
//  redonne sur un autre poste. Sans elle, les fichiers chiffres du depot sont
//  illisibles - y compris par GitHub, et par qui volerait le jeton.
//
//  L'INDEX (index.xpgsync) : la liste des fichiers publies, l'empreinte SHA-256
//  de chacun EN CLAIR, et un code d'authentification (BLAKE2b avec une cle tiree
//  de la cle de synchro) : un fichier modifie hors de l'outil est refuse, meme en
//  clair. Les signatures (registre, revocations, licences) sont verifiees en
//  plus quand une cle d'editeur de confiance est la (la sienne, ou celles
//  compilees dans l'outil).
//
//  UN ENVOI = UN COMMIT (l'API Git Data de GitHub : blobs, arbre, commit, puis la
//  branche avancee SANS forcer) : si un autre poste a publie entre-temps, la
//  branche refuse, l'outil relit, fusionne et recommence une fois.
//
//  LES REGLES DE FUSION : registre et revocations - le plus grand numero gagne ;
//  une licence - la plus recente (sa date de signature) gagne ; le registre des
//  activations - les lignes des deux cotes (aucune ligne perdue, aucune en
//  double). Deux versions differentes d'une meme date : celle du poste reste, et
//  c'est dit (rien n'est ecrase en silence).
//
//  RESEAU : HTTPS par le transport du systeme (WinHTTP sous Windows, libcurl
//  sous Linux - app/ai/AiHttp) ; l'outil est en console, il attend la reponse.
//  Un depot PUBLIC est refuse.
// =============================================================================
#pragma once

#include "../licensing/KeyFile.hpp"
#include "../licensing/Time.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::github {

using licensing::Seconds;
using SyncKey = std::array<std::uint8_t, 32>;

inline constexpr const char* kConfigFile = "github.json";
inline constexpr const char* kTokenFile = "jeton-github.cle";
inline constexpr const char* kSyncKeyFile = "cle-synchro.cle";
inline constexpr const char* kArchiveDir = "archive";
inline constexpr const char* kIndexName = "index.xpgsync";
inline constexpr const char* kReadmeName = "LISEZ-MOI.md";
inline constexpr const char* kDefaultApi = "https://api.github.com";

// ---- la configuration (github.json, dans le dossier de la cle) ----
struct Config {
    std::string repo;                    // "proprietaire/nom"
    std::string branch{"main"};
    std::string root;                    // un sous-dossier du depot ("" : la racine)
    std::string api{kDefaultApi};        // les essais : http://127.0.0.1:PORT
};
[[nodiscard]] bool validRepo(std::string_view repo) noexcept;
[[nodiscard]] bool validBranch(std::string_view branch) noexcept;
[[nodiscard]] bool validRoot(std::string_view root) noexcept;
[[nodiscard]] std::string configPath(const std::string& folder);
[[nodiscard]] bool configured(const std::string& folder);
bool readConfig(const std::string& folder, Config& out, std::string* why);
bool writeConfig(const std::string& folder, const Config& config, std::string* why);

// ---- le jeton GitHub (jeton-github.cle : DPAPI, ce compte) ----
//  Jamais ecrit dans un message, un journal, un fichier en clair.
[[nodiscard]] bool hasToken(const std::string& folder);
bool saveToken(const std::string& folder, const std::string& token, std::string* why);
bool loadToken(const std::string& folder, std::string& token, std::string* why);
bool eraseToken(const std::string& folder, std::string* why);
// "jeton a grain fin" (github_pat_), "jeton classique" (ghp_) ; vide : forme inconnue.
[[nodiscard]] std::string tokenKind(std::string_view token);
// Un jeton plausible : 20 a 255 caracteres, sans espace ni caractere de controle.
[[nodiscard]] bool plausibleToken(std::string_view token) noexcept;

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

// ---- chiffrer un fichier pour le depot ----
//  "XPG-CHIFFRE 1 <id de la cle>\n" puis le base64 (nonce 24 octets + chiffre) en lignes de 76.
[[nodiscard]] std::string seal(const SyncKey& key, std::string_view path, std::string_view plain);
bool openSealed(const SyncKey& key, std::string_view path, std::string_view sealed, std::string& plain, std::string* why);

// ---- l'archive locale (dossier de la cle / archive) ----
//  registre/registre.xpgreg, revocations/revocations.xpgrev, licences/<ID>.xpglic : le dernier de chaque.
//  Le registre des activations reste ou il est (activations.jsonl, dans le dossier de la cle).
[[nodiscard]] std::string archiveFolder(const std::string& folder);
// Ranger un fichier signe (registre, revocations ou licence ; une demande : refuse). La signature doit etre
// celle d'une cle de confiance (`trusted`). `what` : ce qui a ete fait ("ajoute", "remplace", "deja la",
// "plus ancien que celui de l'archive : garde l'actuel"). Faux : `why`.
bool archiveSigned(const std::string& folder, std::string_view bytes, const std::vector<licensing::crypto::PublicKey>& trusted,
                   std::string* what, std::string* why);
// Les cles de confiance de l'outil : celle de `key` (si elle est la) et celles compilees (PublisherKey.hpp).
[[nodiscard]] std::vector<licensing::crypto::PublicKey> trustedKeys(const licensing::SigningKey* key);
// Le nom de fichier d'un identifiant de licence (lettres, chiffres, . _ - ; le reste devient _).
[[nodiscard]] std::string safeName(std::string_view licenseId);

// ---- la synchro ----
struct SyncOptions {
    bool        receiveOnly{false};    // recevoir sans rien envoyer (un poste neuf, une restauration)
    bool        dryRun{false};         // dire ce qui serait recu et envoye, sans rien changer (github-etat)
    Seconds     now{0};
    std::string tool;                  // "XPG-License-Admin 1.1.0" (le message du commit)
};
struct SyncReport {
    bool                     ok{false};
    std::string              why;            // le refus (en francais), si !ok
    std::vector<std::string> received;       // une ligne par fichier recu (ou a recevoir, en dryRun)
    std::vector<std::string> sent;           // une ligne par fichier envoye (ou a envoyer)
    std::vector<std::string> warnings;       // les conflits, les fichiers refuses
    std::string              commit;         // le commit cree ("" : aucun)
    std::string              remoteInfo;     // "proprietaire/nom (prive), branche main, dernier envoi le ..."
    bool                     upToDate{false};
    bool                     preview{false};       // dryRun : rien n'a change, les listes disent ce qui serait fait
    bool                     initialized{false};   // le depot etait vide : son premier commit (LISEZ-MOI)
    int                      attempts{0};
    [[nodiscard]] std::string text() const;
};
// Lit le depot, fusionne dans le poste ce qui est plus recent, puis publie (un commit) ce que le depot n'a pas.
// `key` : la cle de l'editeur si elle est la (signatures verifiees, auteur de l'envoi) ; nul : poste sans cle.
[[nodiscard]] SyncReport synchronize(const std::string& folder, const licensing::SigningKey* key, const SyncOptions& options);

// Pour les essais : les lignes du registre des activations qui manquent a `local` (dans l'ordre de `remote`).
[[nodiscard]] std::vector<std::string> missingLines(std::string_view local, std::string_view remote);

} // namespace license_admin::github
