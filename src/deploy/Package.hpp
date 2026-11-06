// =============================================================================
//  deploy/Package.hpp - 1.28.0 : le paquet de deploiement .xpgdeploy
// -----------------------------------------------------------------------------
//  Conception : docs/DEPLOIEMENT.md, section 3. Un zip (miniz) :
//
//      app/...          le poste : les fichiers du projet utiles au poste, et le lanceur
//      manifest.json    le manifeste (UTF-8), signe tel quel, octet pour octet
//      manifest.sig     la signature Ed25519 du manifeste (128 caracteres hexadecimaux)
//
//  Le dossier app/ EST le dossier du projet sur le PC cible : le lanceur
//  (app/lancer-poste.cmd) lance le programme sur le dossier ou il se trouve
//  (IHM-Studio.exe --ihm "<app>"). Les entrees du zip : les fichiers de app/
//  d'abord, le manifeste et sa signature a la fin (une seule lecture de chaque
//  fichier : son SHA-256 se calcule pendant qu'il est compresse).
//
//  CE QUI PART (collectProject) - ce que le poste d'exploitation lit vraiment.
//  IHM-Studio.exe --ihm "<dossier>" (App::openStation) ouvre le projet par
//  project::ProjectStore::open, qui ne lit que project.xpgproj dans IHM Studio
//  (core::hasApi() faux : ni config/, ni vars/, ni sections/...), refuse s'il
//  n'est pas FINISH (App::projectFinished), puis l'IHM par hmi::load : ihm/ihm.txt
//  et ce que l'index designe (vues/, scripts/, fonctions/, ressources/,
//  modeles/...). En marche, les scripts lisent donnees/ (FILE_READ, DIR_DATA :
//  hmi/HmiRuntimeFiles). D'ou, relatif au dossier du projet :
//
//    PREND   project.xpgproj
//            ihm/       tout, SAUF ihm/corbeille/ (la corbeille de l'IHM),
//                       ihm/supprimes/ (la corbeille du projet), ihm/historique/
//                       (l'historique de marche de CE PC : alarmes, mesures,
//                       audit, valeurs retenues), ihm/historique-edition/
//                       (l'historique des modifications, la conception),
//                       ihm/reseau/ (les machines decouvertes depuis ce PC),
//                       ihm/ressources/enregistrements/ (enregistrements et
//                       dossiers d'incident)
//            donnees/   tout, SAUF donnees/extraits/ (ce qu'Extraire a produit)
//    LAISSE  tout le reste a la racine : versions/, exports/, .xpg/ (le cache
//            de construction, refait sur le poste), le programme de l'automate
//            (config/, vars/, ddt/, dfb/, units/, sections/, tables/, src/ :
//            le poste IHM ne le lit pas), project.lock, le lanceur de ce PC
//            (lancer-poste.cmd / .sh : le paquet porte le sien), tout autre
//            fichier pose a cote ;
//            et partout : les fichiers temporaires et copies de secours (.tmp,
//            ~, ~$..., .part, .bak), les paquets et archives (.xpgdeploy,
//            .xihm, .xapi), Thumbs.db / desktop.ini / .DS_Store, les liens
//            symboliques (jamais suivis).
//  Ce qui est laisse est dit (ProjectCollection::skipped), pour l'apercu.
//
//  SEUL UN PROJET FINISH PART (comme le poste) : project.xpgproj, "state = FINISH"
//  (project/ProjectStore.cpp, readManifest ; les etats : project/Security.hpp).
//  Un projet d'API Studio (edition = api) ne part pas : le poste est celui
//  d'IHM Studio.
//
//  VERIFIER (verifyPackage), dans l'ordre ; la premiere faute arrete :
//    1. le zip se lit ; manifest.json et manifest.sig y sont ; nombre de
//       fichiers et tailles dans les limites (PackageLimits) ;
//    2. la signature est bonne et la cle est approuvee. La signature est
//       verifiee sur les OCTETS LUS, avant toute lecture du JSON quand une cle
//       approuvee la reconnait ; sinon le manifeste est lu pour connaitre
//       publisher.key et dire "signature valide, editeur non approuve" ou
//       "signature fausse" ;
//    3. chaque chemin est sur (safePackagePath), aucune entree n'est un lien
//       symbolique ni un dossier, deux chemins ne different pas que par la casse ;
//    4. les fichiers du zip et ceux du manifeste sont exactement les memes ;
//       chaque taille et chaque SHA-256 concorde.
//  Rien n'est extrait sur le disque : le zip est lu par flux, chaque fichier
//  decompresse en memoire par blocs de 64 Kio et hache au passage.
//
//  Exemple (IHM Studio > Creer un package de deploiement) :
//      auto key = deploy::DevKey::loadOrCreate(dossierDesReglages, &why);
//      auto apercu = deploy::collectProject(dossierDuProjet);   // la liste, ce qui est laisse, le refus
//      deploy::PackageBuildOptions o;
//      o.projectFolder = dossierDuProjet;
//      o.outputPath = ".../Armoire_Gaz-V3.xpgdeploy";
//      o.launcherName = app::station::launcherName(true);
//      o.launcherText = app::station::launcherText(programme, nom, true);
//      o.progress = [&](const deploy::PackageProgress& p) { return !annule; };
//      auto r = deploy::buildPackage(o, *key);   // r.ok, r.error, r.sha256, r.packageBytes
// =============================================================================
#pragma once

#include "Crypto.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace deploy {

class DevKey;

inline constexpr std::string_view kPackageExtension = ".xpgdeploy";
inline constexpr std::string_view kPackageFormat = "xpgdeploy";
inline constexpr int              kPackageVersion = 1;
inline constexpr std::string_view kPackageManifestName = "manifest.json";
inline constexpr std::string_view kPackageSignatureName = "manifest.sig";
inline constexpr std::string_view kPackageAppPrefix = "app/";

// Les limites d'un paquet (DEPLOIEMENT.md) ; les essais les baissent.
struct PackageLimits {
    std::uint64_t maxFiles{20000};                         // fichiers sous app/ (le lanceur compris)
    std::uint64_t maxFileBytes{2ull * 1024 * 1024 * 1024};  // 2 Gio par fichier
    std::uint64_t maxTotalBytes{4ull * 1024 * 1024 * 1024}; // 4 Gio au total
    std::uint64_t maxManifestBytes{16ull * 1024 * 1024};   // manifest.json
    std::size_t   maxPathLength{240};                       // caracteres (unites UTF-16, comme Windows)
};

// ---- les chemins ---------------------------------------------------------------
// Un chemin du paquet est-il sur ? Relatif, sous app/, separe par '/', en UTF-8
// valide ; ni "..", ni "." , ni segment vide ; ni '/' ou '\' au debut, ni '\' du
// tout, ni ':' (lettre de lecteur, flux NTFS), ni caractere de controle ou
// interdit par Windows (< > " | ? *) ; aucun segment n'est un nom reserve de
// Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9, avec ou sans extension :
// "nul.txt"), ni ne finit par un point ou une espace ; `maxLength` caracteres au
// plus. Faux : `why` dit pourquoi, en francais.
[[nodiscard]] bool safePackagePath(std::string_view path, std::string* why = nullptr, std::size_t maxLength = 240);

// ---- le manifeste --------------------------------------------------------------
struct PackageFile {
    std::string   path;     // "app/ihm/ihm.txt"
    std::uint64_t size{0};
    std::string   sha256;   // 64 caracteres hexadecimaux minuscules
};

struct PackageManifest {
    std::string              format{std::string(kPackageFormat)};
    int                      version{kPackageVersion};
    std::string              application;     // "Armoire_Gaz"
    std::string              appVersion;      // "V3"
    std::string              createdAt;       // "2026-11-06T13:40:00Z" (UTC)
    std::string              createdOn;       // le PC qui l'a construit
    std::string              runtimeProgram{"IHM-Studio.exe"};
    std::string              runtimeMinVersion;
    std::string              runtimeMode{"--ihm"};
    std::string              launcher;        // "app/lancer-poste.cmd"
    std::string              publisherName;
    std::string              publisherKey;    // la cle publique Ed25519, 64 caracteres hexadecimaux
    std::vector<PackageFile> files;           // tries par chemin
    std::uint64_t            totalSize{0};    // la somme des tailles
};

// Le texte du manifeste (UTF-8, le format de DEPLOIEMENT.md) : ce sont CES
// octets qui sont signes et ecrits dans le zip.
[[nodiscard]] std::string writeManifest(const PackageManifest&);
// Relire un manifeste : JSON bien forme, chaque champ present et du bon genre,
// aucune cle en double. Un champ inconnu est ignore. Faux : `why`.
// (La signature se verifie AVANT, sur les octets : verifyPackage.)
bool parseManifest(std::string_view json, PackageManifest& out, std::string* why);

// ---- la collecte : ce qui part (la regle : en tete de ce fichier) ---------------
struct ProjectInfo {
    std::string folder;       // le dossier du projet (UTF-8)
    std::string name;         // name = (project.xpgproj) ; vide : le nom du dossier
    std::string version;      // version = (project.xpgproj)
    std::string state;        // "NEW", "DEV", "FINISH", "LOCK"
    std::string edition;      // "ihm", "api", "" (un projet de la 1.11 : les deux)
    std::string lastVersion;  // "V3" : la derniere version validee ou livree (versions/index.txt) ; vide : aucune
    [[nodiscard]] bool finished() const noexcept { return state == "FINISH"; }
};
// Lire project.xpgproj (et versions/index.txt). Faux : ce n'est pas un dossier de projet (`why`).
bool readProjectInfo(const std::string& projectFolder, ProjectInfo& out, std::string* why);

struct CollectedFile {
    std::string   relative;   // relatif au dossier du projet, avec des '/' : "ihm/vues/0003-Synoptique.vue"
    std::string   source;     // le chemin complet (UTF-8)
    std::uint64_t size{0};
};
struct SkippedFiles {
    std::string   what;       // "versions/", "ihm/historique/", "notes.txt"
    std::string   why;        // "les versions du projet (elles restent sur ce PC)"
    std::size_t   files{0};
    std::uint64_t bytes{0};
};
struct ProjectCollection {
    ProjectInfo                project;
    std::vector<CollectedFile> files;        // tries par chemin
    std::vector<SkippedFiles>  skipped;      // tries
    std::uint64_t              totalBytes{0};
    std::string                refusal;      // vide : le projet peut partir ; sinon la phrase a montrer
    [[nodiscard]] bool ok() const noexcept { return refusal.empty(); }
};
// Ce qui partirait, pour l'apercu : la liste est rendue meme quand le projet est
// refuse (pas FINISH, un nom de fichier que Windows refuse, trop gros...) ;
// `refusal` le dit. Rien n'est lu que les tailles.
[[nodiscard]] ProjectCollection collectProject(const std::string& projectFolder, const PackageLimits& limits = {});

// ---- construire ------------------------------------------------------------------
struct PackageProgress {
    std::size_t   filesDone{0}, filesTotal{0};
    std::uint64_t bytesDone{0}, bytesTotal{0};
    std::string   current;    // le fichier en cours ("app/ihm/ihm.txt")
};
// Appelee pendant le travail (a chaque bloc de 64 Kio) ; faux : annuler.
using PackageProgressFn = std::function<bool(const PackageProgress&)>;

struct PackageBuildOptions {
    std::string       projectFolder;
    std::string       outputPath;          // le paquet a ecrire (extension .xpgdeploy ; remplace s'il existe)
    std::string       launcherName;        // "lancer-poste.cmd" (app::station::launcherName(true)) : app/<nom>
    std::string       launcherText;        // son contenu (app::station::launcherText(programme, nom, true))
    std::string       application;         // vide : le nom du projet
    std::string       appVersion;          // vide : la derniere version validee ("V3"), sinon la version du manifeste du projet
    std::string       createdOn;           // vide : DevKey::computerName()
    std::string       runtimeProgram{"IHM-Studio.exe"};
    std::string       runtimeMinVersion;   // vide : la version de ce programme (XPG_ANALYZER_VERSION)
    std::string       runtimeMode{"--ihm"};
    PackageLimits     limits;
    PackageProgressFn progress;            // facultatif
};

struct PackageBuildResult {
    bool            ok{false};
    bool            cancelled{false};      // annule par la progression (rien n'est ecrit)
    std::string     error;                 // la phrase a montrer (vide si ok)
    std::string     path;                  // le paquet ecrit
    std::string     sha256;                // l'empreinte du paquet (hexadecimal)
    std::uint64_t   packageBytes{0};       // sa taille
    PackageManifest manifest;              // ce qu'il annonce
};
// Collecte (le projet doit etre FINISH), ecrit le zip dans un fichier temporaire
// (<sortie>.tmp), le renomme a la fin ; une erreur ou une annulation n'en laisse
// rien. Les fichiers sont hashes pendant la compression : un fichier qui change
// pendant la construction la fait echouer.
[[nodiscard]] PackageBuildResult buildPackage(const PackageBuildOptions&, const DevKey&);

// ---- verifier ---------------------------------------------------------------------
enum class PackageVerdict : std::uint8_t {
    Accepted,              // les quatre etapes passent, l'editeur est approuve
    UnapprovedPublisher,   // la signature est valide pour publisher.key, mais cette cle n'est pas approuvee
    BadSignature,          // la signature ne correspond pas au manifeste (modifie, ou une autre cle)
    Refused,               // une autre faute (zip, limites, manifeste, chemins, contenu)
};

struct PackageVerifyOptions {
    std::vector<crypto::PublicKey> approvedKeys;   // l'agent : les cles recues a l'appairage
    // Le Studio qui ouvre un paquet pour l'inspecter : un editeur non approuve
    // n'arrete pas la verification (les etapes 3 et 4 sont faites) ; le verdict
    // reste UnapprovedPublisher si tout le reste est bon.
    bool              inspect{false};
    PackageLimits     limits;
    PackageProgressFn progress;                    // facultatif (l'etape 4 hache tout)
};

struct PackageReport {
    PackageVerdict  verdict{PackageVerdict::Refused};
    int             failedStep{0};          // 1 a 4 ; 0 : aucune
    std::string     reason;                 // la phrase a montrer, en francais (aussi quand tout va bien)
    bool            signatureValid{false};  // la signature est bonne pour publisher.key
    bool            publisherApproved{false};
    bool            manifestRead{false};    // `manifest` est rempli
    PackageManifest manifest;
    std::string     publisherFingerprint;   // l'empreinte lisible de publisher.key ("4F:9A:...")
    bool            cancelled{false};
    [[nodiscard]] bool ok() const noexcept { return verdict == PackageVerdict::Accepted; }
};
// Verifier le paquet `path` (les quatre etapes, dans l'ordre).
[[nodiscard]] PackageReport verifyPackage(const std::string& path, const PackageVerifyOptions&);

// "4 Gio", "12,5 Mio", "830 octets" (les messages).
[[nodiscard]] std::string packageSizeText(std::uint64_t bytes);

} // namespace deploy
