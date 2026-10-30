// =============================================================================
//  app/FileFinder.hpp - 1.14.0 (lot M5) : chercher les fichiers de l'appli sur le PC
// -----------------------------------------------------------------------------
//  LA DEMANDE : "dans le menu principal ajoute un chercheur de fichiers
//  compatibles sur TOUT le pc des extensions specifiques de l'application avec
//  un filtre pour les extensions".
//
//  CE FICHIER NE DESSINE RIEN. Il dit quelles extensions sont celles de l'appli
//  (le catalogue, par groupes), il parcourt les dossiers sur un fil a part et
//  rend ce qu'il trouve au fil de l'eau, il lit la version dans l'en-tete d'un
//  fichier quand c'est rapide et sur, et il dit ce que chaque geste ferait d'un
//  fichier trouve (ouvrir ici, dans l'autre appli, importer). Le volet
//  (FileFinderPane) le montre ; l'ecran fait les gestes (FinderWorkspace.cpp).
//
//  LE PARCOURS (Search) :
//    - un fil a part, de priorite basse (Windows : le mode "arriere-plan", qui
//      baisse aussi les entrees-sorties ; ailleurs : nice) ; un lecteur reseau
//      a son rythme (une petite pause par dossier) ;
//    - ni les liens symboliques ni les jonctions ne sont suivis (pas de boucle) ;
//      un dossier refuse est passe sans erreur et compte ("12 dossiers refuses") ;
//    - Arreter l'arrete tout de suite : le fil voit le drapeau a chaque entree ;
//    - les resultats restent dans la recherche partagee (shared()) tant que
//      l'appli est ouverte, jusqu'a la recherche suivante.
//
//  Il se verifie sans ecran : hmi_editor_test (finder1140).
// =============================================================================
#pragma once

#include "../core/Edition.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace app::finder {

// ---- le catalogue ------------------------------------------------------------------
// L'application d'un fichier : celle qui l'ouvre (ou l'importe).
enum class Studio : std::uint8_t { Api, Ihm, Both };

struct ExtInfo {
    std::string_view key;      // l'extension, en minuscules, sans le point : "xpgproj"
    std::string_view label;    // ce qu'elle est : "Projet (dossier)"
    std::string_view group;    // son groupe : "Projets"
    Studio           studio;   // l'application qui s'en sert
};
// Dans l'ordre de l'ecran : Projets, Exports Control Expert, Paquets IHM,
// Bibliotheques, Autres.
[[nodiscard]] const std::vector<ExtInfo>& catalog();
[[nodiscard]] const ExtInfo* info(std::string_view key);
[[nodiscard]] std::vector<std::string_view> groups();
// L'extension d'un nom de fichier si elle est au catalogue ("MAST.XPG" -> "xpg") ;
// vide sinon. La casse ne compte pas.
[[nodiscard]] std::string keyOf(std::string_view fileName);
// "Celles d'API Studio" / "Celles d'IHM Studio" : les extensions de l'application
// lancee (Both : les siennes et les communes ; l'edition Both : toutes).
[[nodiscard]] bool mine(const ExtInfo&, core::Edition);
[[nodiscard]] std::string_view studioName(Studio);          // "API Studio", "IHM Studio"

// ---- ce que l'on cherche, ou -----------------------------------------------------------
enum class Where : std::uint8_t { WholePc, SystemDrive, Documents, Folder };

struct Request {
    Where       where{Where::WholePc};
    std::string folder;              // Where::Folder : le dossier (UTF-8)
    bool        network{true};       // les lecteurs reseau mappes (tout le PC)
    bool        removable{true};     // les cles USB et les disques externes (tout le PC)
    bool        skipSystem{true};    // Windows, Program Files, ProgramData, la Corbeille, les dossiers systeme et caches
    bool        zips{false};         // dans les .zip aussi (leur table des matieres)
    // Les essais : ces dossiers au lieu de ceux de `where` (vide : ceux de `where`) ;
    // ces dossiers-la refuses comme un dossier sans droits (l'essai tourne en
    // administrateur : un chmod 000 n'y refuse rien).
    std::vector<std::string> roots;
    std::vector<std::string> refuseForTests;
};

// Un fichier trouve.
struct Hit {
    std::string   path;       // le chemin entier (UTF-8) ; dans un zip : le zip, puis le chemin dedans
    std::string   name;       // le nom seul (un projet : le nom de son dossier)
    std::string   dir;        // le dossier (dans un zip : "D:\x\a.zip > sous/dossier")
    std::string   key;        // l'extension au catalogue
    std::uint64_t size{0};    // octets
    std::int64_t  mtime{0};   // secondes depuis 1970 (UTC) ; 0 : inconnue
    std::string   version;    // lue dans l'en-tete ; vide : "-" (pas lue, ou pas de version)
    std::string   edition;    // un projet, une archive : "ihm", "api" ; vide : inconnue (un projet 1.11 : les deux)
    std::string   archive;    // le .zip qui le contient ; vide : un fichier du disque
    std::string   folded;     // le nom plie (minuscules, sans accents) : le filtre, le tri
};

struct Progress {
    std::uint64_t folders{0};        // dossiers lus
    std::uint64_t files{0};          // fichiers trouves
    std::uint64_t refused{0};        // dossiers refuses (droits, lecteur absent)
    std::uint64_t links{0};          // liens et jonctions passes
    std::uint64_t skipped{0};        // dossiers systeme et caches passes
    std::uint64_t zips{0};           // archives .zip lues
    std::string   current;           // le dossier en cours
    std::string   where;             // les lecteurs ou dossiers parcourus : "C:, D:, \\SERVEUR\Projets"
    bool          running{false};
    bool          stopped{false};    // Arreter
    bool          finished{false};   // tout parcouru
    bool          truncated{false};  // trop de fichiers : la recherche s'est arretee a kMaxHits
    double        seconds{0.0};
    std::uint64_t generation{0};     // chaque recherche a son numero (0 : aucune)
};
inline constexpr std::size_t kMaxHits = 50000;

// ---- la recherche -------------------------------------------------------------------
class Search {
public:
    Search();
    ~Search();                                   // arrete et attend le fil
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    // Une nouvelle recherche : les resultats d'avant partent. Une recherche en
    // cours est arretee d'abord (sans l'attendre).
    void start(Request r);
    // Arreter : le fil s'arrete a l'entree suivante ; rend tout de suite.
    void stop();
    // Attendre la fin (les essais, la sortie).
    void wait();
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] std::size_t found() const;
    // Les fichiers trouves a partir du rang `from` (le volet prend les nouveaux).
    [[nodiscard]] std::vector<Hit> since(std::size_t from) const;
    [[nodiscard]] const Request& request() const noexcept { return request_; }

    struct Run;                                  // l'etat partage avec le fil

private:
    void reap(bool all);                         // attendre les fils deja arretes
    std::shared_ptr<Run>     run_;
    std::thread              worker_;
    std::vector<std::pair<std::shared_ptr<Run>, std::thread>> finishing_;
    std::uint64_t            generation_{0};
    Request                  request_;
};

// La recherche de l'application : ses resultats restent tant qu'elle est ouverte
// (le volet se ferme, se rouvre : ils sont la).
[[nodiscard]] Search& shared();

// ---- les morceaux, publics pour les essais ---------------------------------------------
// Un dossier a passer (skipSystem) : `driveRoot` - il est a la racine d'un lecteur
// (Windows, Program Files...) ; `hidden` - cache ou systeme (Windows), .nom ailleurs.
[[nodiscard]] bool skipFolder(std::string_view name, bool driveRoot, bool hidden);
// La version lue dans l'en-tete d'un fichier, quand c'est rapide et sur (quelques Ko,
// ou la table des matieres d'un zip) ; vide : pas de version. `edition` recoit "ihm",
// "api" pour un projet ou une archive qui le dit.
[[nodiscard]] std::string readVersion(const std::string& path, std::string_view key, std::string* edition = nullptr);
// Les racines d'une demande (les lecteurs du PC, C:, les Documents, le dossier) ;
// `network` recoit, pour chacune, vrai si c'est un lecteur reseau. Lent (les lecteurs) :
// sur le fil de la recherche.
[[nodiscard]] std::vector<std::string> rootsFor(const Request& r, std::vector<bool>* network = nullptr);
// Les lecteurs pour le libelle "Tout le PC (C:, D:, E:)" : vite (sans lire les volumes).
[[nodiscard]] std::string drivesLabel(bool network, bool removable);
[[nodiscard]] std::string systemDrive();                     // "C:" ; "/" ailleurs
[[nodiscard]] std::string documentsFolder();

// ---- le texte -------------------------------------------------------------------------
[[nodiscard]] std::string typeText(const Hit&);              // "Projet IHM Studio", "Export programme"
[[nodiscard]] std::string sizeText(std::uint64_t bytes);     // "412 Ko", "5,3 Mo"
[[nodiscard]] std::string dateText(std::int64_t mtime);      // "2026-10-12 14:05"
// La ligne de la progression : "Recherche... 62 311 dossiers lus . 108 fichiers . D:\Affaires"
// et la fin : "Termine : 108 fichiers dans 62 311 dossiers (C:, D:), en 14 s . 12 dossiers refuses".
[[nodiscard]] std::string progressText(const Progress&);
[[nodiscard]] std::string thousands(std::uint64_t n);

// ---- les gestes ------------------------------------------------------------------------
//  Ce que l'ecran sait de lui-meme : un projet ouvert (et lequel), l'endroit.
struct Context {
    core::Edition edition{core::Edition::Both};
    bool workspace{false};    // le volet est dans l'ecran du projet (pas l'accueil)
    bool hmiProject{false};   // une IHM ouverte
    bool plcProject{false};   // un programme de l'automate ouvert
    bool recordings{false};   // Ressources > Enregistrements existe (les .xrec s'y importent)
};
// L'application qui ouvre ce fichier (un projet : son edition ; un projet de la 1.11,
// sans edition : celle qui est lancee).
[[nodiscard]] Studio studioOf(const Hit&, core::Edition here);
// Ouvert ici (le meme programme) ?
[[nodiscard]] bool openedHere(const Hit&, core::Edition here);
// Un projet, une archive, un export : il s'ouvre ; le reste s'importe.
[[nodiscard]] bool openable(std::string_view key);

enum class ImportKind : std::uint8_t {
    None,
    HmiPackage,     // .xpgvues, .xpgsymboles, .xpgtypes, .xpgfonctions, .xpgscripts : la fenetre Importer
    HmiTemplate,    // .xpgmodele : la galerie des modeles (la fenetre Importer)
    HmiLibrary,     // .xpgbib : Relier une bibliotheque
    PlcExport,      // .xpg, .xhw, .xdb : dans le projet ouvert (la question du depot)
    Library,        // .dfb, .ddt, .mac : la bibliotheque (libs/)
    Theme,          // .xpgtheme : les themes
    Recording,      // .xrec : Ressources > Enregistrements
    Operators,      // .xpgst : le volet Operateurs ou Scripts (son Importer...)
};
[[nodiscard]] ImportKind importKindOf(std::string_view key);

// Ce que fait un geste sur un fichier : possible ou non (et pourquoi), son libelle.
struct Gesture {
    bool        enabled{false};
    std::string label;       // "Ouvrir", "Ouvrir avec IHM Studio", "Importer dans le projet", "Ajouter a la bibliotheque"
    std::string why;         // grise : pourquoi
};
[[nodiscard]] Gesture openGesture(const Hit&, const Context&);
[[nodiscard]] Gesture importGesture(const Hit&, const Context&);
[[nodiscard]] Gesture recentGesture(const Hit&, const Context&);
// Le dossier a ouvrir (Ajouter aux recents, ouvrir un projet) : celui du project.xpgproj.
[[nodiscard]] std::string projectFolderOf(const Hit&);

// ---- le systeme ------------------------------------------------------------------------
// L'Explorateur, le fichier choisi (ailleurs : son dossier). Vide : fait.
[[nodiscard]] std::string revealInExplorer(const std::string& path);
// L'autre application (API Studio <-> IHM Studio), a cote de celle-ci, avec ce fichier.
// Vide : lancee ; sinon pourquoi.
[[nodiscard]] std::string launchStudio(Studio which, const std::string& path);

// ---- les reglages (le choix des extensions et des options est garde) ----------------------
struct Choices {
    std::vector<std::string> extensions;   // cochees ; toutes au premier lancement
    Where       where{Where::WholePc};
    std::string folder;
    bool        network{true}, removable{true}, skipSystem{true}, zips{false};
};
[[nodiscard]] std::string serialize(const Choices&);
[[nodiscard]] Choices parseChoices(std::string_view text);   // vide : le choix par defaut
inline constexpr std::string_view kSettingsKey = "finder.choices";

} // namespace app::finder
