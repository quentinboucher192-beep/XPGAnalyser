// =============================================================================
//  app/DropFilesPlan.hpp - lot API 8 : ce qu'on peut faire des fichiers deposes
// -----------------------------------------------------------------------------
//  GLISSER N'IMPORTE QUEL FICHIER DANS LA FENETRE. Au lot 7, un .XPG ou un .XHW
//  lache sur la fenetre posait une question ; les autres fichiers ne faisaient
//  rien, sauf sur un volet qui les prend (Ressources, Fichiers externes, le
//  champ d'une macro). Ici, ce qu'on PEUT faire de chaque fichier, lu dans son
//  contenu :
//
//    UN CLASSEUR (.xlsx, .xlsm ; .csv, .tsv ; un .xls ne se lit pas : le dire) :
//    ses onglets et leurs titres de colonnes (xls::Workbook ; le vbaProject.bin
//    d'un .xlsm est ignore). Une ligne par possibilite :
//      - importer maintenant (un Ctrl+Z chacun) : les variables de l'IHM (une
//        liste Nom / Type / Adresse...), les traductions (la colonne
//        "Texte (fr)"), les jeux d'une recette, des lignes dans une table
//        d'animation ;
//      - ajouter au projet : aux Fichiers externes de l'IHM, aux Ressources
//        (un classeur n'en est pas une : grisee, avec sa raison) ;
//      - les macros : en faire le classeur des macros, et chaque macro de la
//        bibliotheque qui lit un classeur et dont les onglets sont la (ses
//        OpenSheet('...'), ses lignes "#! lit", son champ de fichier) - son
//        formulaire, le classeur deja choisi, jusqu'a l'apercu.
//    Chaque ligne dit ce qu'elle fera et ce qu'elle a reconnu ; une ligne
//    impossible est grisee avec sa raison ; les plus sures sont cochees.
//
//    LES AUTRES FICHIERS : Ressources si l'IHM accepte le genre (images, sons,
//    videos, polices), Fichiers externes si elle sait le lier (classeurs, CSV,
//    textes, JSON, XML, bases SQLite). Deja la : remplacer, garder les deux,
//    ignorer. Par defaut, la case qui convient au genre.
//    Lot API 8, 2e partie : TOUT fichier (PDF, DOCX, ZIP, image, texte...) va
//    dans les Ressources (une copie ; un document hors media, garde tel quel)
//    et / ou les Fichiers externes (un lien ; un Document s'il n'est pas des
//    donnees lisibles). Par defaut : un media en Ressources, le reste en lien.
//
//  CE FICHIER NE DESSINE RIEN ET NE MODIFIE RIEN : il lit les fichiers, libs/
//  et le projet, et rend un plan. DropFilesDialog le montre (les cases sont
//  celles du plan) ; l'ecran le fait, par les chemins qui existent deja
//  (screens/DropFilesWorkspace.cpp). Il se verifie sans ecran (dropfiles_test).
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"      // hmi::Resource : l'apercu d'une image

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }

namespace app::dropfiles {

// ---- le genre d'un fichier ----------------------------------------------------------
//  Le contenu d'abord pour un programme (.XPG) ou une configuration (.XHW) - un
//  export renomme reste ce qu'il est -, l'extension pour le reste.
enum class Genre : std::uint8_t {
    Program, Hardware,              // .XPG, .XHW : la question du lot 7
    Workbook, Table,                // .xlsx .xlsm .xls ; .csv .tsv
    Image, Sound, Video, Font,      // ce que les Ressources de l'IHM acceptent
    Pdf, Text, Json, Xml, Database, // documents et donnees
    Folder, Other, Missing,
};
[[nodiscard]] Genre genreOf(const std::string& path);
[[nodiscard]] std::string genreLabel(Genre);                 // "Classeur Excel", "Image"...
[[nodiscard]] inline bool isWorkbook(Genre g) noexcept { return g == Genre::Workbook || g == Genre::Table; }

// Ce que l'analyse lit du projet ouvert. Tout est facultatif : sans IHM, les
// lignes de l'IHM sont grisees ; sans libs/, aucune macro.
struct Context {
    std::string            libsRoot;          // libs/ (libs/Macros : les macros)
    const domain::Project* plc{nullptr};      // les variables, les tables d'animation
    const hmi::Project*    hmi{nullptr};
    std::string            projectFolder;     // un fichier externe dedans : chemin relatif
    std::string            macroWorkbook;     // le classeur des macros aujourd'hui ("" : aucun)
};

// ---- un classeur ----------------------------------------------------------------------
enum class ActionKind : std::uint8_t {
    HmiVariables, Translations, Recipe, AnimationTable,     // importer maintenant
    ExternalFile, Resource,                                  // ajouter au projet
    MacroWorkbook, Macro,                                    // les macros
};
// Les trois temps de Faire, dans cet ordre : les imports (un Ctrl+Z chacun),
// les ajouts, puis les macros (leur formulaire reste ouvert, devant).
enum class Stage : std::uint8_t { Import, Add, Macros };
[[nodiscard]] Stage stageOf(ActionKind k) noexcept;
[[nodiscard]] std::string stageTitle(Stage);

// Un choix d'une liste : la recette qui recoit les jeux, la table d'animation.
struct Target {
    std::string label, value;
};

struct Action {
    ActionKind  kind{ActionKind::Macro};
    std::string label;             // "Lancer ImporterCartes - onglet Cartes API reconnu"
    std::string detail;            // ce qu'elle fera, ce qu'elle a reconnu ; \n : a la ligne
    bool        possible{true};
    std::string why;               // grisee : pourquoi
    bool        checked{false};    // cochee (au depart : les plus sures)
    std::string macro;             // Macro : son nom
    std::string field;             // Macro : le champ qui recoit le fichier ("classeur", "tableau:es")
    std::string sheet;             // l'onglet lu ; vide : le fichier entier (un CSV)
    std::size_t rows{0};
    std::vector<Target> targets;   // Recipe, AnimationTable : ou (une liste deroulante)
    int         target{0};
    int         score{0};          // Macro : les onglets qu'elle lit dans ce classeur
    // AnimationTable : les variables reconnues, et vrai pour une variable IHM.
    std::vector<std::string> items;
    std::vector<bool>        itemHmi;
};

struct SheetInfo {
    std::string              name;
    std::size_t              rows{0};
    std::vector<std::string> columns;
};

struct WorkbookPlan {
    std::string            path, name;
    Genre                  genre{Genre::Workbook};
    std::uint64_t          bytes{0};
    bool                   readable{false};
    std::string            error;          // illisible : pourquoi
    bool                   vba{false};     // un vbaProject.bin (ignore)
    std::vector<SheetInfo> sheets;
    std::vector<Action>    actions;        // dans l'ordre de Faire (Stage, puis la liste)
    // "13 onglets : Config, Cartes API, ES... - 84,5 Ko - macros VBA ignorees".
    [[nodiscard]] std::string summary() const;
};
[[nodiscard]] WorkbookPlan analyseWorkbook(const std::string& path, const Context& ctx);

// ---- un autre fichier -------------------------------------------------------------------
enum class Conflict : std::uint8_t { Replace, KeepBoth, Ignore };
[[nodiscard]] std::string conflictLabel(Conflict);          // "Remplacer", "Garder les deux", "Ignorer"

struct FileRow {
    std::string   path, name;
    Genre         genre{Genre::Other};
    std::string   kind;                     // "Image PNG, 64 x 64", "Document PDF"...
    std::uint64_t bytes{0};
    bool          resourceOk{false}, fileOk{false};
    std::string   resourceWhy, fileWhy;     // grisee : pourquoi
    bool          resource{false}, file{false};     // cochees
    // Deja la (meme nom, ou meme fichier lie) : lequel, et que faire.
    std::string   resourceExisting, fileExisting;
    hmi::Id       resourceId{hmi::kNoId}, fileId{hmi::kNoId};
    bool          sameFile{false};          // le lien existant vise deja ce fichier
    bool          identical{false};         // la ressource existante a le meme contenu
    Conflict      conflict{Conflict::Replace};
    // Une image : de quoi dessiner sa vignette (le contenu lu une fois).
    hmi::Resource preview;
    bool          hasPreview{false};
    [[nodiscard]] bool present() const noexcept { return !resourceExisting.empty() || !fileExisting.empty(); }
    [[nodiscard]] bool wanted() const noexcept { return (resource && resourceOk) || (file && fileOk); }
};
[[nodiscard]] FileRow analyseFile(const std::string& path, const Context& ctx);

// ---- le depot entier ---------------------------------------------------------------------
//  Les options du .XPG / .XHW sont celles du lot 7 : l'ecran les remplit
//  (leur texte dit le nom du projet) ; `code` est le sien.
struct ProgramOption {
    int         code{0};
    std::string label, detail;
};

struct Plan {
    std::string                projectName;
    std::vector<std::string>   programs, hardware;   // .XPG, .XHW (le premier de chaque sert)
    std::vector<ProgramOption> programOptions;
    int                        programChoice{0};     // le rang dans programOptions
    std::vector<WorkbookPlan>  workbooks;
    std::vector<FileRow>       files;
    std::vector<std::string>   ignored;              // un dossier, un fichier absent, un .XPG en trop
    // Ce qui sera fait (cases cochees, possibles ; le choix du .XPG compte pour un).
    [[nodiscard]] std::size_t count() const;
};
[[nodiscard]] Plan analyse(const std::vector<std::string>& paths, const Context& ctx);

// ---- relire un onglet au moment de faire ----------------------------------------------
//  Les titres et les lignes, comme l'analyse les a vus (la ligne d'en-tete
//  cherchee, pas supposee). `sheet` vide : le premier onglet, ou le CSV.
struct SheetData {
    std::vector<std::string>              columns;
    std::vector<std::vector<std::string>> rows;
};
[[nodiscard]] bool readSheet(const std::string& path, const std::string& sheet, SheetData& out, std::string* why = nullptr);
// Ce qu'Excel met dans le presse-papiers : les titres, puis une ligne par ligne,
// separes par des tabulations (une case a tabulation, retour ou guillemet :
// entre guillemets) - ce que relit le collage des tableaux (TablePaste).
[[nodiscard]] std::string tabText(const SheetData&);
// Un CSV a ';' (le format des recettes).
[[nodiscard]] std::string csvText(const SheetData&);

// ---- les morceaux, pour les essais ------------------------------------------------------
// Les colonnes que lit le code d'une macro : Cell(t, i, 'Nom'), HasColumn(t, 'Nom').
[[nodiscard]] std::vector<std::string> cellColumns(std::string_view macroSource);
// Sans casse ni accents (les scripts designent une ligne par son debut).
[[nodiscard]] std::string fold(std::string_view text);
[[nodiscard]] bool startsFolded(std::string_view text, std::string_view prefix);
// Un nom de table d'animation tire d'un nom d'onglet : "Seuils (bar)" -> "Seuils_bar".
[[nodiscard]] std::string tableName(std::string_view base);

} // namespace app::dropfiles
