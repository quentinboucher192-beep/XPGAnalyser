// =============================================================================
//  project/ProjectArchive.hpp - 1.12.30 : un projet en un seul fichier
// -----------------------------------------------------------------------------
//  L'ARCHIVE D'UN PROJET : .xihm (IHM Studio), .xapi (API Studio). Un zip, que
//  n'importe quel outil zip ouvre, qui porte TOUT le dossier du projet tel qu'il
//  est enregistre - le manifeste (la fiche), l'IHM (vues, ressources de tous les
//  genres, scripts, alarmes, equipements, simulation et ses reglages, historique
//  des objets, modeles, styles...), le programme de l'automate (sections, LD et
//  GRAFCET gardes tels quels, unites, DFB, DDT, variables, configuration, tables,
//  icone), les donnees, les versions - et ce que le projet cite hors de son
//  dossier : les fichiers externes lies ailleurs (_externes/), la disposition des
//  grafcets posee a cote du dossier (_a-cote/), les macros de la bibliotheque
//  (_macros/, au choix).
//
//  UN MANIFESTE EN TETE (manifeste.txt) : le genre, l'outil et sa version, le
//  projet, ses comptes section par section, et chaque fichier - sa taille, son
//  empreinte SHA-256, sa date. Relire l'archive, c'est verifier chacun : present,
//  intact (le CRC du zip, puis la taille et l'empreinte). Le format est celui de
//  l'archive .zip de l'IHM (1.x) : une archive d'avant se relit, et une version
//  d'avant relit l'IHM d'une archive .xihm renommee en .zip.
//
//  1.12.31 : LE CACHE DE CONSTRUCTION VIENT AVEC (.xpg/build/build-cache.txt :
//  les empreintes, les etats et les diagnostics du dernier build) - un projet
//  rouvert reprend son build ou il en etait, sans rien regenerer. Ses artefacts
//  (la forme generee de chaque element) se deduisent du projet : ils ne voyagent
//  pas, le deballage les refait d'apres le cache (hmi::pipeline::restoreArtifacts),
//  chacun a l'empreinte que le cache a retenue, sinon rien (il sera regenere).
//
//  CE QU'ELLE NE PREND PAS : les artefacts du build, son verrou et la copie de
//  secours de son cache, ce qui sort du projet (exports/), la corbeille de l'IHM
//  (ihm/corbeille/), les fichiers temporaires (.tmp, ~) - et les archives et les
//  pages web d'archive posees dans le dossier (elles se contiendraient l'une
//  l'autre). Au choix : les versions (versions/) et l'historique de marche
//  (ihm/historique/ : alarmes, mesures, audit, memoire des reglages PID).
//
//  DEBALLER (ouvrir une archive) : dans un dossier neuf seulement ; un chemin qui
//  sortirait du dossier (absolu, ..) est refuse ; chaque fichier reprend sa date.
//
//  UNE PAGE WEB PEUT PORTER L'ARCHIVE : en base64, dans un bloc
//  <script type="application/x-xpg-archive">, annonce dans l'en-tete par
//  <meta name="xpg-archive">. Ouvrir la page reconstruit le projet.
//
//  Ce fichier ne sait rien de l'IHM ni de l'automate : hmi/HmiArchive (les
//  comptes de l'IHM, les fichiers externes relies) et project/ApiArchive (les
//  comptes de l'automate) s'en servent.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace project::archive {

using Bytes = std::vector<std::uint8_t>;

// ---- le genre ---------------------------------------------------------------
enum class Kind : std::uint8_t { Ihm, Api };
[[nodiscard]] std::string_view extension(Kind) noexcept;     // ".xihm", ".xapi"
[[nodiscard]] std::string_view key(Kind) noexcept;           // "ihm", "api"
[[nodiscard]] std::string_view application(Kind) noexcept;   // "IHM Studio", "API Studio"
[[nodiscard]] std::optional<Kind> kindFromKey(std::string_view) noexcept;

// 1 : l'archive .zip de l'IHM (1.x : ihm/ et ses historiques) ; 2 : 1.12.30, le
// projet entier (genre=, chaque fichier avec sa date et son origine). Une archive
// d'un format plus recent est refusee : elle porte peut-etre ce que cette version
// ne saurait pas remettre en place.
inline constexpr int kFormat = 2;

// ---- d'ou vient un fichier ---------------------------------------------------
enum class Origin : std::uint8_t {
    Folder,      // le dossier du projet (le chemin, relatif a lui)
    External,    // un fichier lie, hors du dossier : "_externes/<id>-<nom>" ; link : le chemin du lien
    Beside,      // a cote du dossier : "_a-cote/<nom>" (la disposition des grafcets, <projet>.xpglayout)
    Library,     // une macro de la bibliotheque : "_macros/<chemin sous libs/Macros>"
};
[[nodiscard]] std::string_view originKey(Origin) noexcept;   // "", "externe", "a-cote", "macro"

struct Entry {
    std::string   path;
    std::uint64_t bytes{0};
    std::string   sha256;          // hexadecimal
    std::int64_t  mtime{0};        // secondes depuis 1970 (UTC) ; 0 : inconnue
    Origin        origin{Origin::Folder};
    std::string   link;            // External : le lien tel que le projet l'ecrit
    bool          ok{true};        // a la lecture : present et intact
};

// Une ligne du controle : "Vues : 4 attendu(s), 4 relu(s)".
struct Count {
    std::string key, label;
    long long   expected{0}, found{0};
    [[nodiscard]] bool ok() const noexcept { return expected == found; }
};

struct Manifest {
    int         format{kFormat};
    Kind        kind{Kind::Ihm};
    bool        kindKnown{false};   // genre= lu (une archive .zip de la 1.x n'en a pas : l'IHM)
    std::string tool;               // "IHM Studio 1.12.30"
    std::string created;            // "2026-10-23 19:40"
    std::string project;            // le nom du projet
    std::string folder;             // le nom de son dossier
    int         dataFormat{0};      // le format du projet IHM (format_ihm) ; 0 : sans objet
    std::vector<Count> counts;
    std::vector<Entry> entries;
};
[[nodiscard]] std::string toText(const Manifest&);
// Ce qui se lit est rendu ; une ligne illisible est dite dans `problems`.
void parse(std::string_view text, Manifest& out, std::vector<std::string>& problems);

// ---- prendre le dossier ------------------------------------------------------
struct Item {
    std::string                  path;
    std::shared_ptr<const Bytes> data;
    std::int64_t                 mtime{0};
    Origin                       origin{Origin::Folder};
    std::string                  link;
};
struct CollectOptions {
    bool versions{true};    // versions/
    bool history{true};     // ihm/historique/ : l'historique de marche
};
// Ce que l'archive ne prend pas (chemin relatif au dossier, avec des '/') ;
// `why` : pourquoi, pour le dire ("les artefacts du build, refaits au deballage...").
[[nodiscard]] bool excluded(std::string_view relative, const CollectOptions&, std::string* why = nullptr);
struct Skipped {
    std::string   what, why;     // "exports/", "ce qui sort du projet"
    std::size_t   files{0};
    std::uint64_t bytes{0};
};
// Les fichiers du dossier, tries, avec leur date. `skipped` : ce qui est laisse.
[[nodiscard]] core::Result<std::vector<Item>> collectFolder(const std::string& folder, const CollectOptions&,
                                                            std::vector<Skipped>* skipped = nullptr);
// Un fichier hors du dossier (fichier externe, disposition a cote, macro).
[[nodiscard]] bool readItem(const std::string& file, Item& out);
// Les versions d'un dossier de projet (les lignes "version" de versions/index.txt).
[[nodiscard]] long long versionCount(const std::string& folder);

// ---- ecrire -----------------------------------------------------------------
// Le zip, manifeste.txt en tete. `manifest.entries` est rempli (taille, empreinte,
// date) depuis `items`. Les medias deja compresses (images, sons, videos, zip,
// classeurs, PDF) sont stockes tels quels ; le reste est compresse s'il y gagne.
[[nodiscard]] core::Result<Bytes> pack(Manifest& manifest, const std::vector<Item>& items);
// Ecrire un fichier d'un bloc (.tmp, puis renommage) ; le dossier est cree.
[[nodiscard]] core::Status writeFile(const std::string& path, const Bytes& bytes);

// ---- lire ---------------------------------------------------------------------
struct Contents {
    Manifest                           manifest;
    bool                               hasManifest{false};
    std::map<std::string, std::string> files;        // chemin -> contenu
    std::set<std::string>              damaged;      // le CRC du zip echoue
    std::vector<std::string>           problems;     // ce qui ne colle pas avec le manifeste
    std::uint64_t                      archiveBytes{0};
    bool                               fromPage{false};   // l'archive d'une page web
    // Ecrite par une version plus recente (format) : elle ne se deballe pas.
    bool                               newer{false};
    [[nodiscard]] bool intact() const noexcept { return problems.empty() && damaged.empty(); }
    [[nodiscard]] const std::string* file(const std::string& path) const;
};
[[nodiscard]] core::Result<Contents> read(const Bytes& zip);
// Un .xihm, un .xapi, un .zip ; une page .html : l'archive qu'elle porte.
[[nodiscard]] core::Result<Contents> readFile(const std::string& path);

// ---- deballer -----------------------------------------------------------------
// Vrai : un chemin d'archive qu'on peut poser sous un dossier (ni absolu, ni ..).
[[nodiscard]] bool safePath(std::string_view path) noexcept;
struct UnpackOptions {
    bool        skipDamaged{false};    // deballer ce qui est intact (sinon : refus)
    std::string libraryRoot;           // les macros jointes : ajoutees a <libraryRoot>/Macros si absentes
};
struct Unpacked {
    std::vector<std::string> written;           // relatifs au dossier
    std::vector<Entry>       externals;         // les fichiers lies hors du dossier, poses dans externes/
    std::vector<std::string> externalPaths;     // ... a quel chemin (relatif au dossier), dans le meme ordre
    std::string              beside;            // le fichier pose a cote du dossier ("" : aucun)
    std::vector<std::string> macrosAdded, macrosKept;
    std::vector<std::string> notes;             // ce qui merite d'etre dit
};
[[nodiscard]] core::Status unpack(const Contents&, const std::string& folder, const UnpackOptions&, Unpacked* out = nullptr);
// Un dossier neuf sous `parent` : "<nom>", sinon "<nom> (2)", "<nom> (3)"...
[[nodiscard]] std::string freeFolder(const std::string& parent, std::string_view name);

// ---- une page web qui porte l'archive -----------------------------------------
// A mettre dans <head> : ce qui dit, sans lire toute la page, qu'elle porte une archive.
[[nodiscard]] std::string pageHeadMarker(Kind);
// Le bloc de l'archive (base64), a mettre dans <body>.
[[nodiscard]] std::string pageArchiveBlock(const Bytes& zip, Kind, std::string_view fileName);
// L'archive d'une page ; faux : pas d'archive, ou abimee (`why` le dit).
[[nodiscard]] bool archiveFromPage(std::string_view html, Bytes& zip, std::optional<Kind>* kind = nullptr,
                                   std::string* why = nullptr);
[[nodiscard]] std::string base64(const std::uint8_t* data, std::size_t size);
[[nodiscard]] bool        fromBase64(std::string_view text, Bytes& out);

// ---- reconnaitre un fichier ---------------------------------------------------
[[nodiscard]] bool archiveExtension(std::string_view path) noexcept;   // .xihm, .xapi
[[nodiscard]] bool pageExtension(std::string_view path) noexcept;      // .html, .htm
struct Sniff {
    bool                archive{false};   // une archive de projet (ou une page qui en porte une)
    bool                page{false};      // une page web
    std::optional<Kind> kind;
    std::string         why;              // pas une archive : pourquoi (une page sans archive...)
};
// Par l'extension et le debut du contenu ; ne lit pas toute l'archive.
[[nodiscard]] Sniff sniff(const std::string& path);

// ---- le texte -----------------------------------------------------------------
[[nodiscard]] std::string sizeText(std::uint64_t bytes);     // "412 Ko", "5,3 Mo"
[[nodiscard]] std::string nowText();                          // "2026-10-23 19:40"

} // namespace project::archive
