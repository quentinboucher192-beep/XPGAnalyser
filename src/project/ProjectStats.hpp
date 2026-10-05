// =============================================================================
//  project/ProjectStats.hpp - lot API 7 : les statistiques du projet ouvert
// -----------------------------------------------------------------------------
//  L'ancien ecran « Statistiques » (Affichage, Ctrl+5) lisait le RAPPORT
//  D'IMPORT : un projet ouvert depuis son dossier n'en a pas, l'ecran restait
//  vide (« No project loaded »), et rien ne suivait une modification. Les
//  chiffres sont maintenant calcules ICI, sur le projet tel qu'il est, sans
//  ecran (tests/projectstats_test.cpp) ; l'onglet API > Statistiques
//  (app/StatisticsPane) les montre et les recalcule apres chaque commande.
//
//  CE QUI EST COMPTE, ET COMMENT
//
//   * LES LIGNES : celles des sections que la tache principale (MAST) execute,
//     entree par entree (project::api::entriesOf : une section, ou une unite de
//     programme en bloc), par section et par langage. Une ligne est une ligne
//     du texte de la section tel que l'export l'ecrit (Section::lineCount, comme
//     partout ailleurs). Le corps des DFB, les sous-routines et les autres
//     taches sont comptes A PART : ils ne sont pas « le programme, en lignes »,
//     et les trois repartitions (entrees, sections, langages) font toutes le
//     meme total.
//
//   * LES VARIABLES : les globales (constantes comprises), les locales
//     (privees et publiques, des unites ET des DFB) et les parametres (entrees,
//     sorties, entrees-sorties). Les champs des types derives n'en sont pas :
//     ils comptent dans leur type. Le genre est celui de l'onglet Variables
//     (project::usage::genreOf).
//     Lues / ecrites : une lecture simple du code, comme ApiChecks (voir
//     ProjectStats.cpp) - une ecriture est un nom en tete d'instruction avant
//     ':=', une sortie liee par '=>', l'argument de SET / RESET / INC / DEC, ou
//     ce qu'on passe a une entree-sortie d'un bloc ; le reste est une lecture.
//     Dans une unite, un parametre lie a une globale (EffectiveParameter) EST
//     cette globale ; une variable propre au POU cache la globale du meme nom.
//     Un code qui n'est pas du ST (LD, FBD, SFC, IL) ne dit pas le sens : ce
//     qu'il nomme compte comme lu ET ecrit - jamais une fausse alerte.
//     « Pas utilisee » : ni lue, ni ecrite, ni liee a une unite.
//     « Ecrite, jamais lue » et « lue, jamais ecrite » laissent de cote ce qui
//     vit aussi HORS du code : une variable situee (le materiel, l'IHM, un
//     superviseur la lisent et l'ecrivent par son adresse), une variable que
//     l'IHM lit (elle peut aussi l'ecrire), une valeur initiale (une constante).
//
//   * LA MEMOIRE DECLAREE : un MODELE des declarations, pas le plan memoire de
//     l'automate (SizeModel, plus bas, dit ses regles).
//
//   * « A REGARDER » : ce que les chiffres suggerent, chacun avec la cle de
//     l'onglet qui le regle (MainAnalysisScreen::onApiRequest).
//
//  UN PASSAGE sur les variables, UN sur le code (plus celui des lectures avant
//  l'ecriture, celui de l'onglet Ordre d'execution) : le projet d'essai (891
//  variables, 75 sections) se calcule en une quinzaine de millisecondes, a
//  chaque modification.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "ApiChecks.hpp"
#include "SharedLibrary.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace project::stats {

// ------------------------------------------------------------- les langages ----
enum class Language : std::uint8_t { ST, SFC, LD, FBD, IL, Other };
constexpr std::size_t kLanguageCount = 6;
using LanguageLines = std::array<std::size_t, kLanguageCount>;

[[nodiscard]] Language languageOf(domain::PouLanguage) noexcept;
[[nodiscard]] std::string_view languageName(Language) noexcept;    // "ST"
[[nodiscard]] std::string_view languageLabel(Language) noexcept;   // "ST (texte structure)", en UTF-8

// ------------------------------------------------------------------ le code ----
// Une entree de la tache : une section, ou une unite de programme en bloc.
struct EntryLines {
    std::string   name;
    bool          unit{false};
    std::size_t   rank{0};              // 1 : la premiere entree executee
    std::size_t   sections{0};
    std::size_t   lines{0};             // la somme de byLanguage
    LanguageLines byLanguage{};
    domain::Index section{domain::kNoIndex};     // une section : son indice dans project.sections
    domain::Index unitIndex{domain::kNoIndex};   // une unite : son POU
};

// Une section que la tache execute (celle d'une unite comprise).
struct SectionLines {
    std::string   name;
    std::string   unit;                 // l'unite qui la porte ; "" : une section de la tache
    domain::Index section{domain::kNoIndex};
    domain::Index owner{domain::kNoIndex};       // cette unite (son POU) ; kNoIndex : une section de la tache
    Language      language{Language::ST};
    std::size_t   lines{0};
};

struct CodeStats {
    std::string               task;             // la tache principale : MAST
    std::size_t               lines{0};         // ses lignes : la somme des entrees
    std::vector<EntryLines>   entries;          // dans l'ordre d'execution
    std::vector<SectionLines> sections;         // dans l'ordre d'execution
    LanguageLines             byLanguage{};
    std::size_t               taskSections{0};  // les entrees qui sont une section
    std::size_t               units{0};         // les entrees qui sont une unite
    std::size_t               unitSections{0};  // les sections de ces unites
    // Hors de la tache principale : le corps des DFB, les sous-routines, les
    // autres taches (et ce que rien n'execute).
    std::size_t dfbLines{0}, dfbs{0};
    std::size_t subroutineLines{0}, subroutines{0};
    std::size_t otherLines{0};
};

// ------------------------------------------------------------ les variables ----
enum class Scope : std::uint8_t { Global, Local, Parameter };
constexpr std::size_t kScopeCount = 3;
// project::usage::Genre, dans son ordre : instances de DFB, blocs standard,
// instances de types derives, situees, les autres.
constexpr std::size_t kGenreCount = 5;

struct NamedVariable {
    std::string   name;
    domain::Index index{domain::kNoIndex};   // dans project.variables
    std::string   detail;                    // l'adresse, s'il y en a une
};

struct DuplicateAddress {
    std::string                address;     // tel qu'ecrit la premiere fois
    std::vector<NamedVariable> variables;   // deux ou plus
};

struct VariableStats {
    std::array<std::size_t, kScopeCount>                          count{};
    std::array<std::array<std::size_t, kGenreCount>, kScopeCount> byGenre{};
    std::size_t commented{0};                // parmi toutes (globales, locales, parametres)
    std::size_t located{0};
    std::size_t globalsWithoutComment{0};
    std::size_t locatedWithoutComment{0};    // parmi ces globales, les situees
    std::string firstLocatedWithoutComment;  // l'adresse de la premiere
    // Les compteurs : des variables GLOBALES (ce que l'onglet Variables montre).
    std::vector<NamedVariable>    unused;            // ni lues, ni ecrites, ni liees a une unite
    std::vector<NamedVariable>    readByHmi;
    std::vector<NamedVariable>    writtenNeverRead;  // ni par le code, ni par l'IHM ; ni situee
    std::vector<NamedVariable>    readNeverWritten;  // par le code ; ni valeur initiale, ni situee, ni lue par l'IHM
    std::vector<DuplicateAddress> duplicates;
    std::vector<NamedVariable>    ioWithoutComment;  // %I, %IW, %Q, %QW, %CH
    [[nodiscard]] std::size_t total() const noexcept { return count[0] + count[1] + count[2]; }
    [[nodiscard]] double commentedShare() const noexcept {
        return total() ? static_cast<double>(commented) / static_cast<double>(total()) : 0.0;
    }
};

// -------------------------------------------------------------- la memoire ----
enum class TypeGenre : std::uint8_t { Elementary, Derived, Dfb, Standard, Unknown };
[[nodiscard]] std::string_view typeGenreLabel(TypeGenre) noexcept;   // "type derive (DDT)"... en UTF-8

// Les octets d'un type elementaire (BOOL, INT, STRING[32]...) ; 0 : pas un
// type elementaire (ou une longueur illisible).
[[nodiscard]] std::uint64_t elementaryBytes(std::string_view type) noexcept;

// ---------------------------------------------------------------------------
//  LE MODELE DE TAILLE - des declarations, pas le plan de l'automate.
//
//   * Un type elementaire : BOOL, EBOOL, BYTE, SINT, USINT 1 octet ; INT, UINT,
//     WORD 2 ; DINT, UDINT, DWORD, REAL, TIME, DATE, TOD 4 ; LINT, ULINT, LWORD,
//     LREAL, DT 8 ; STRING[n] n + 1 (le zero final) ; STRING 17 (16 caracteres).
//     Un BOOL compte un octet entier : c'est ce qu'une variable BOOL non situee
//     occupe sur M340 / M580 - le rapport de construction reste l'autorite.
//   * Un type derive (DDT) : la somme de ses champs. SANS BOURRAGE : Control
//     Expert aligne les champs, et le modele l'ignore - c'est pourquoi l'ecran
//     dit « un modele des declarations ».
//   * Un tableau : le nombre d'elements (toutes ses dimensions, et les
//     tableaux de tableaux) fois l'element (members::parseArray le lit). Une
//     borne qui n'est pas un nombre (une constante nommee) : la dimension
//     compte pour un, et la taille est dite incomplete.
//   * Une instance de DFB : la somme de ses parametres et de ses variables,
//     SAUF les entrees-sorties : elles designent la variable qu'on leur passe,
//     elles ne l'emportent pas (la compter deux fois gonflerait les tableaux
//     que les DFB du projet d'essai recoivent ainsi).
//   * Un bloc standard (TON, CTU...) : ses broches, lues dans la bibliotheque
//     des blocs (BlockLibrary), entrees-sorties exceptees comme ci-dessus ;
//     une broche generique (ANY_NUM...) compte 0 - le bloc reste connu.
//   * Un type inconnu : 0 octet, et il est signale (le total est un minimum).
//
//  Les tailles se calculent une fois par type (des DDT dans des DDT) ; un type
//  qui se contiendrait lui-meme s'arrete a 0 au lieu de boucler.
// ---------------------------------------------------------------------------
class SizeModel {
public:
    explicit SizeModel(const domain::Project& p);
    struct Info {
        std::uint64_t bytes{0};
        TypeGenre     genre{TypeGenre::Unknown};
        bool          resolved{false};   // faux : un type inconnu, lui ou dessous
        std::string   name;              // tel que le projet (ou la bibliotheque) l'ecrit
    };
    // Un type ecrit comme dans une declaration : "INT", "armoire",
    // "ARRAY[0..19] OF config_gaz", "string[100]".
    [[nodiscard]] Info of(std::string_view type);

    // Un tableau, meme de tableaux : son type de base et le nombre d'elements ;
    // sinon le type lui-meme et 1. `ok` faux : une borne illisible (une
    // constante nommee) - le nombre est alors inconnu.
    struct Flat {
        std::string   base;
        std::uint64_t count{1};
        bool          ok{true};
    };
    [[nodiscard]] static Flat flatten(std::string_view type);

    // Les types inconnus rencontres jusqu'ici, meme au fond d'un autre (le
    // champ d'un DDT, la variable d'un DFB), dans l'ordre de rencontre.
    [[nodiscard]] const std::vector<std::string>& unknownTypes() const noexcept { return unknown_; }

private:
    [[nodiscard]] Info compute(const std::string& key, std::string_view type);

    const domain::Project&                         p_;
    std::unordered_map<std::string, Info>          cache_;   // par nom en minuscules
    std::unordered_map<std::string, domain::Index> ddt_, dfb_;
    std::vector<std::string>                       busy_;    // les types en cours (une boucle s'arrete)
    std::vector<std::string>                       unknown_;
};

struct TypeRow {
    std::string   name;
    TypeGenre     genre{TypeGenre::Unknown};
    std::size_t   declared{0};       // les variables qui le declarent, seul ou en tableau
    std::uint64_t instances{0};      // les elements des tableaux compris
    std::uint64_t unitBytes{0};      // un exemplaire
    std::uint64_t totalBytes{0};     // instances x unitBytes
    double        share{0.0};        // de la memoire declaree, 0..1
    bool          resolved{true};
    // Les variables qui le portent, la plus grosse d'abord (quatre au plus) :
    // (nom, instances).
    std::vector<std::pair<std::string, std::uint64_t>> holders;
};

// LA MEMOIRE EST COMPTEE LA OU ELLE EST DECLAREE, une fois : les globales, et
// les locales et parametres des unites de programme. Une variable d'un DFB
// l'est dans chaque instance du DFB (sa taille), un champ dans son type, une
// entree-sortie nulle part (elle designe une autre variable). Chaque octet est
// donc dans UNE ligne de la table des types, et les parts font 100 %.
struct MemoryStats {
    std::uint64_t                          total{0};
    std::array<std::uint64_t, kScopeCount> byScope{};
    std::size_t                            unresolved{0};     // declarations de taille incomplete (un type inconnu, ou dedans)
    std::vector<std::string>               unresolvedTypes;   // les types inconnus eux-memes (0 octet)
    std::vector<TypeRow>                   types;             // le plus lourd d'abord
};

// ------------------------------------------------------------ a regarder ----
struct Finding {
    enum class Tone : std::uint8_t { Ok, Info, Warning, Error };
    Tone        tone{Tone::Info};
    std::string id;         // stable, pour les scripts : "doublons", "unites-longues"...
    std::string title, detail;
    std::string button;     // "" : pas de bouton
    std::string request;    // la cle de MainAnalysisScreen::onApiRequest
};

// Une unite de programme de plus de tant de lignes est a regarder.
constexpr std::size_t kLongUnitLines = 1000;

// ------------------------------------------------------------------ le tout ----
struct ProjectStats {
    bool                       valid{false};     // faux : rien de calcule (pas de projet)
    std::string                project;          // header.projectName
    CodeStats                  code;
    VariableStats              variables;
    MemoryStats                memory;
    std::vector<api::LateRead> late;             // lectures avant l'ecriture, dans l'ordre de la tache
    std::vector<VersionNotice> outdated;         // plus recents dans la bibliotheque
    std::vector<Finding>       findings;         // « A regarder », le plus grave d'abord
    double                     milliseconds{0.0};
};

// Ce que l'ecran sait de l'IHM : une variable de l'automate qu'elle lit
// (app::ApiHmiRead, recopie ici pour que project/ ne depende pas de app/).
struct ReadInfo {
    std::string via;        // la variable IHM liee ; "" : citee telle quelle
    int         uses{0};
};

// `library` et `hmiReads` (cle : le nom en minuscules) peuvent etre nuls.
[[nodiscard]] ProjectStats compute(const domain::Project& p, const SharedLibrary* library,
                                   const std::map<std::string, ReadInfo>* hmiReads);

// Le CSV de l'export (separateur ;, virgule decimale) : les chiffres, les
// entrees, les sections, les types, « A regarder ».
[[nodiscard]] std::string toCsv(const ProjectStats& s);

// ---------------------------------------------------- les nombres, en francais ----
[[nodiscard]] std::string thousands(std::uint64_t n);            // "2 912" (espace fine insecable)
[[nodiscard]] std::string decimal(double value, int decimals);   // "46,7"
[[nodiscard]] std::string bytesText(std::uint64_t bytes);        // "412 o", "16,1 Ko", "1,2 Mo"
[[nodiscard]] std::string percentText(double fraction);          // "34 %", "< 1 %"

} // namespace project::stats
