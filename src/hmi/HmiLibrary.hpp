// =============================================================================
//  hmi/HmiLibrary.hpp - 1.13.0 (lot J8) : les bibliotheques reliees (hmi::biblio)
// -----------------------------------------------------------------------------
//  UNE BIBLIOTHEQUE EST DU CODE PARTAGE, RELIE ET NON COPIE. Elle a un NOM (son
//  espace de noms de tete : Gaz publie Gaz.T_Vanne, Gaz.EnDefaut), une VERSION
//  (1.4.0 : majeur.mineur.correctif) et un contenu (types, fonctions, scripts,
//  avec leurs sous-espaces). Elle vit dans un FICHIER .xpgbib, un zip lisible :
//
//      bibliotheque.txt   nom, version, description, dependances, outil qui l'a ecrite
//      ihm/ihm.txt ...    un petit projet IHM ecrit par serializeProject (le modele
//                         final de la 1.13 : methodes, unites, generiques)
//
//  LE LIEN, PAS LE CONTENU. Le projet enregistre une ligne
//      bibliotheque nom="Gaz" version="1.4.0" compat="1.2.0" chemin="libs/Gaz-1.4.0.xpgbib"
//  et un cache compact de l'interface (bib_type, bib_fonction, bib_script). Le code
//  n'est jamais recopie dans ihm/ (c'est la difference avec un paquet .xpgtypes, que
//  l'import COPIE) : un diff de projet reste court ; deux projets partagent vraiment
//  le meme code.
//
//  OU ELLE VIT. Le chemin se cherche relatif au projet d'abord (libs/Gaz-1.4.0.xpgbib :
//  portable en git), puis dans les dossiers de bibliotheques partagees de la machine
//  (setSharedFolders), puis tel quel (absolu). Deplacee : Localiser...
//
//  VERSION ET COMPATIBILITE (semver : meme majeur, mineur >= celui demande). Plus
//  recente compatible : reliee, une note ; Mettre a jour fige la version trouvee. Plus
//  ancienne que le plancher : un avertissement (un symbole recent manquera). Majeur
//  different : refusee, le cache d'interface garde le projet verifiable. Absente : une
//  erreur et Localiser... Un fichier d'une version plus recente de l'outil : refuse
//  comme un paquet (core::ErrorCode::XmlUnsupportedDtd, pkg::tooNew).
//
//  LECTURE SEULE. Son contenu (Project::libraryCache) s'emploie partout - types,
//  appels, IHM_APPELER - mais ne s'edite pas : il n'est pas dans Project::programs.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::biblio {

inline constexpr std::string_view kLibraryExtension = ".xpgbib";
inline constexpr std::string_view kManifestFile = "bibliotheque.txt";
inline constexpr int              kLibraryFormat = 1;

// ---- les versions (semver) ------------------------------------------------------------
struct Version {
    int  major{0}, minor{0}, patch{0};
    bool ok{false};
    [[nodiscard]] std::string text() const;
};
[[nodiscard]] Version parseVersion(std::string_view);     // "1.4.0", "1.4" (correctif 0), "v1.4.0"
[[nodiscard]] int     compare(const Version&, const Version&) noexcept;   // <0, 0, >0
// L'etat d'un fichier trouve en `found`, pour un lien a `recorded` de plancher `floor`.
[[nodiscard]] LibraryState stateOf(std::string_view found, std::string_view recorded, std::string_view floor);
[[nodiscard]] std::string_view stateLabel(LibraryState) noexcept;    // "reli\xC3\xA9" "e", "plus r\xC3\xA9" "cente dispo"...
// La version suivante proposee : majeur (rupture), mineur (ajout), correctif (sans changement d'interface).
enum class Bump : std::uint8_t { Major, Minor, Patch };
[[nodiscard]] std::string bumped(std::string_view version, Bump);

// ---- le fichier .xpgbib -----------------------------------------------------------------
struct Manifest {
    int         format{kLibraryFormat};
    std::string writer;                 // la version de l'outil qui l'a ecrite
    std::string name;                   // l'espace de noms de tete (Gaz)
    std::string version;                // 1.4.0
    std::string description;
    std::string fromProject;            // le projet d'ou elle a ete publiee
    std::string created;
    std::vector<std::pair<std::string, std::string>> dependencies;   // (nom, plancher) : Util >= 1.0.0
};
struct Package {
    Manifest manifest;
    Project  content;                   // ses types, fonctions, scripts (ns : Gaz, Gaz.Ligne...)
};
[[nodiscard]] Bytes                 toZip(const Package&);
[[nodiscard]] core::Result<Package> fromZip(const Bytes&);
[[nodiscard]] core::Status          writeFile(const Package&, const std::string& path);
[[nodiscard]] core::Result<Package> readFile(const std::string& path);
// "2 types, 3 fonctions, 1 script" ; vide : "vide".
[[nodiscard]] std::string contentsText(const Project& content);

// ---- publier --------------------------------------------------------------------------------
struct PublishSpec {
    std::string              name;           // l'espace de tete (Gaz) ; ses items, et ceux de ses sous-espaces, partent
    std::string              version{"1.0.0"};
    std::string              description{};
    std::vector<std::string> spaces{};       // des espaces en plus (vide : name et ses sous-espaces)
    std::vector<Id>          items{};        // des items en plus, par identifiant (types, fonctions, scripts)
    std::vector<std::pair<std::string, std::string>> dependencies{};
};
struct PublishPlan {
    Package                  package;
    std::vector<std::string> missing;        // ce qui manquerait : "la variable IHM Pression (elle ne voyage pas)"...
    std::string              refusal;        // vide : publiable ; sinon pourquoi non
};
[[nodiscard]] PublishPlan collect(const Project&, const PublishSpec&);

// ---- l'interface (le cache que garde le projet) -----------------------------------------------
// Les noms sont relatifs a la bibliotheque (T_Vanne, Ligne.T_X) : un alias local les reprend.
void interfaceOf(const Project& content, std::string_view libraryName, std::vector<LibraryTypeSig>& types,
                 std::vector<LibraryFunctionSig>& functions, std::vector<std::string>& scripts);
// La signature d'une fonction telle que le cache la garde : "(v : T_Vanne; seuil : REAL) : BOOL".
[[nodiscard]] std::string signatureOf(const HmiFunction&);

// ---- relier ---------------------------------------------------------------------------------
// Les dossiers des bibliotheques partagees de la machine (l'application les donne ; un essai aussi).
void setSharedFolders(std::vector<std::string> folders);
[[nodiscard]] std::vector<std::string> sharedFolders();
// Le fichier du lien : relatif au projet, puis les dossiers partages, puis tel quel. Vide : introuvable.
[[nodiscard]] std::string findFile(const Library&, const std::string& projectFolder);
// Le chemin a enregistrer pour `file` : relatif au projet s'il est dedans (libs/Gaz.xpgbib), sinon absolu.
[[nodiscard]] std::string pathToRecord(const std::string& file, const std::string& projectFolder);

// Le contenu lu d'un lien : ses items sous l'espace du lien (un alias reprend le nom publie), des
// identifiants a eux (kLibraryIdBase), les types de ses textes ecrits en entier.
[[nodiscard]] LoadedLibrary loadContent(const Library&, const Package&, std::size_t index);
// Le contenu tire du seul cache d'interface (types et signatures, sans corps) : le projet reste verifiable.
[[nodiscard]] LoadedLibrary fromCache(const Library&, std::size_t index);
// Sans disque : chaque lien depuis son cache (parseProject). Avec : chaque lien cherche, lu, compare.
void useCache(Project&);
void resolve(Project&, const std::string& projectFolder);

// Relier un fichier : ce qu'il apporte, ce qui avertit, ce qui empeche. `alias` : relier sous un autre
// nom (une collision d'espace) ; vide : son nom publie.
struct LinkPlan {
    Package                  package;
    Library                  link;           // la ligne qui sera ajoutee (version, compat, chemin, interface)
    std::string              file;           // le fichier lu (absolu)
    std::vector<std::string> warnings;       // une collision d'espace, une dependance absente...
    std::string              refusal;        // vide : on peut relier ; sinon pourquoi non (Relier grise)
    [[nodiscard]] bool ok() const noexcept { return refusal.empty(); }
};
[[nodiscard]] LinkPlan planLink(const Project&, const std::string& file, const std::string& projectFolder,
                                std::string_view alias = {});
// Ajoute le lien et relit les bibliotheques (une seule modification du projet : une commande). Faux : refuse.
bool link(Project&, const LinkPlan&, const std::string& projectFolder);
bool unlink(Project&, std::string_view name);
// Localiser : un autre chemin pour le lien (le fichier deplace).
bool relocate(Project&, std::string_view name, const std::string& file, const std::string& projectFolder, std::string* why = nullptr);

// ---- mettre a jour ----------------------------------------------------------------------------
struct InterfaceChange {
    enum class Kind : std::uint8_t { Added, Removed, Changed } kind{Kind::Added};
    std::string              name;           // le nom complet (Gaz.EnDefaut)
    std::string              what;           // "type", "fonction", "script"
    std::string              before, after;  // la signature (ou les membres) avant et apres
    std::vector<std::string> usedAt;         // ou le projet l'emploie : "script Demarrage", "fonction Calcul"...
    [[nodiscard]] bool breaks() const noexcept { return kind != Kind::Added && !usedAt.empty(); }
};
struct UpdatePlan {
    std::string                  name;       // la bibliotheque
    std::string                  from, to;   // les versions
    std::string                  file;       // le fichier lu
    std::vector<InterfaceChange> changes;
    std::string                  refusal;    // vide : on peut appliquer
    Package                      package;
    [[nodiscard]] std::size_t breakCount() const;
};
// Relit le fichier du lien (ou `file`) et compare son interface a celle du lien.
[[nodiscard]] UpdatePlan planUpdate(const Project&, std::string_view name, const std::string& projectFolder,
                                    const std::string& file = {});
// Fige la version et l'interface lues (une commande : Ctrl+Z la defait). Faux : refuse.
bool applyUpdate(Project&, const UpdatePlan&, const std::string& projectFolder);

// Ou le projet emploie ce nom complet (Gaz.EnDefaut, Gaz.T_Vanne) : "script Demarrage", "variable Vanne_1"...
[[nodiscard]] std::vector<std::string> usesOf(const Project&, std::string_view fullName);
// Les constats de Generer sur les liens : introuvable, incompatible, plus ancienne, collision d'espace,
// chemin absolu (pas portable). (phrase, erreur ?)
struct LinkIssue {
    std::string library;
    std::string message;
    bool        error{true};
    bool        locate{false};   // la correction proposee : Localiser...
};
[[nodiscard]] std::vector<LinkIssue> linkIssues(const Project&);

} // namespace hmi::biblio
