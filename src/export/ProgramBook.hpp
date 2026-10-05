// =============================================================================
//  export/ProgramBook.hpp - 1.8.0 : le programme, lisible par quelqu'un d'autre
// -----------------------------------------------------------------------------
//  Menu Projet > Exporter le programme lisible... (et le clic droit sur API, une
//  unite, une section ; l'accueil). Le but : envoyer un fichier a quelqu'un qui
//  n'a pas Control Expert, pour qu'il comprenne le programme.
//
//  CE QUI EST ECRIT, DANS L'ORDRE OU L'AUTOMATE L'EXECUTE (l'ordre d'execution
//  de chaque tache, celui de l'onglet Ordre d'execution) :
//    - un guide de lecture (le cycle, les conditions, les unites, le ST) ;
//    - le sommaire : chaque section, son unite ou sa tache, sa condition, ses
//      lignes, son commentaire, son role (l'icone choisie) ;
//    - chaque section : son bandeau, ce qu'elle ECRIT, ce qu'elle LIT (les
//      variables du projet), les blocs qu'elle APPELLE, puis son code numerote ;
//    - une unite de programme commence par ses parametres (ce que chacun recoit) ;
//    - en annexe : le code des blocs DFB, les variables globales.
//
//  TROIS FORMATS, SANS BIBLIOTHEQUE : texte (UTF-8 avec BOM, 100 colonnes,
//  fins de ligne Windows), Excel (.xlsx : Lisez-moi, Sommaire aux liens
//  cliquables, un onglet par groupe, code en Consolas colore, impression
//  reglee), PDF (A4 : couverture, sommaire pagine, signets, code colore).
//
//  La PORTEE : tout le programme, une unite de programme, ou des sections
//  choisies (elles gardent leur ordre d'execution et leur rang dans le cycle).
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace exporter::book {

struct Options {
    enum class Scope : std::uint8_t { All, Unit, Sections };
    Scope                       scope{Scope::All};
    std::string                 unit;                 // Scope::Unit
    std::vector<domain::Index>  sections;             // Scope::Sections
    bool guide{true};             // guide de lecture et sommaire
    bool access{true};            // ce que chaque section lit, ecrit, appelle
    bool lineNumbers{true};
    bool roles{true};             // les icones choisies (le role)
    bool unitParams{true};
    bool dfbAppendix{true};
    bool variablesAppendix{true};
    // Remplis par l'appelant : ce qui ne se lit pas dans le projet.
    std::string exportedAt;       // "30/09/2026 a 10:42"
    std::string appName{"XPGAnalyser"};
    std::string appVersion;
    std::string projectName;      // le nom du projet (sinon celui du fichier)
};

struct Param { std::string direction, name, type, receives, comment; };

struct Section {
    std::size_t              rank{0};        // 1 = la premiere du cycle (toutes taches confondues)
    domain::Index            index{domain::kNoIndex};
    std::string              name, owner, task;   // owner : l'unite, ou la tache
    bool                     inUnit{false};
    std::string              condition;      // vide : a chaque cycle
    std::string              comment;        // le premier commentaire parlant du code
    std::vector<std::string> lines;          // les tabulations en 4 espaces
    std::size_t              lineCount{0};
    std::vector<std::string> writes, reads, calls;
    int                      icon{-1};       // core::codeicons ; -1 : aucune
};

struct Unit {
    std::string        name, task;
    std::size_t        rank{0};              // son rang dans la tache (Pou::order)
    std::vector<Param> params;
    std::size_t        locals{0}, sections{0}, lines{0};
    int                icon{-1};
};

struct Dfb {
    std::string        name, version;
    std::vector<Param> params;
    std::size_t        inputs{0}, outputs{0}, inOuts{0};
    struct Body { std::string name; std::vector<std::string> lines; int icon{-1}; };
    std::vector<Body>  bodies;
    std::size_t        lines{0};
    int                icon{-1};
};

struct Variable { std::string name, type, address, initial, comment; };

// L'ordre du document : une tache (son titre), une unite (son en-tete), une section.
struct Item {
    enum Kind : std::uint8_t { Task, Unit, Section } kind{Section};
    std::size_t index{0};                    // dans tasks, units, sections
};

struct TaskInfo { std::string name, kind; std::uint32_t period{0}, watchdog{0}; std::size_t sections{0}, lines{0}; };

struct Book {
    std::string project, version, cpu, product, source, exportedAt, app;
    std::string scopeLabel;                   // "Tout le programme", "L'unite de programme X", "2 sections choisies"
    std::vector<TaskInfo> tasks;
    std::vector<Section>  sections;
    std::vector<Unit>     units;
    std::vector<Item>     order;
    std::vector<Dfb>      dfbs;
    std::vector<Variable> variables;
    std::size_t totalSections{0}, totalLines{0};       // tout le programme (le rang "17 / 65")
    std::size_t scopeLines{0};                          // ce qui est exporte
    std::size_t derivedTypes{0}, programUnits{0};
};

[[nodiscard]] Book build(const domain::Project&, const Options&);

// Le nom des fichiers, sans extension : "Programme_Projet_2026-09-30",
// "Programme_Projet_Logigrammes_A", "Programme_Projet_SFC_ManuA_SFC_ManuB".
[[nodiscard]] std::string fileStem(const Book&, const Options&, std::string_view isoDate);

[[nodiscard]] std::string                toText(const Book&, const Options&);   // UTF-8 avec BOM, CRLF
[[nodiscard]] std::vector<std::uint8_t> toXlsx(const Book&, const Options&);
[[nodiscard]] std::vector<std::uint8_t> toPdf(const Book&, const Options&);

// Pour les ecrivains : la ligne de code decoupee en morceaux colores.
struct Run { std::string text; enum Kind : std::uint8_t { Plain, Keyword, Type, Comment, String, Number, Address, Function } kind{Plain}; };
[[nodiscard]] std::vector<Run> colorize(std::string_view line, bool& inComment);

// Le texte d'une icone pour le sommaire ("Grafcet / sequence") ; vide : aucune.
[[nodiscard]] std::string roleName(int icon);

} // namespace exporter::book
