// =============================================================================
//  hmi/HmiPackage.hpp - des vues qui voyagent : exporter, importer, modeles (lot 20)
// -----------------------------------------------------------------------------
//  UN PAQUET DE VUES (.xpgvues ; .xpgmodele pour un modele) est un zip, lisible
//  par n'importe quel outil :
//
//      paquet.txt        ce qu'il contient, d'ou il vient, pour quoi faire
//      ihm/ihm.txt ...   un petit projet IHM : les vues choisies ET ce dont
//                        elles ont besoin, ecrit par le meme code que le
//                        dossier ihm/ d'un projet (serializeProject)
//
//  CE QU'UNE VUE EMPORTE (collect) :
//    - son ecran modele (et le sien), son en-tete, son pied de page ;
//    - les symboles de ses instances (et ceux qu'ils contiennent) ;
//    - les popups que ses actions ouvrent (et ce qu'elles emportent) ;
//    - les ressources qu'elle cite (images, sons, polices) ;
//    - les styles nommes de ses objets ;
//    - les variables IHM qu'elle lit ou ecrit (leur definition : l'import sait
//      les creer), leurs types IHM, et les fonctions IHM qu'elle appelle.
//  Les variables de l'automate ne voyagent pas : Generer dit s'il en manque.
//
//  IMPORTER, C'EST COMPARER AVANT D'AGIR (plan) : chaque element du paquet est
//  absent du projet (il sera ajoute), identique (le projet garde le sien), ou
//  different - et alors on choisit : l'importer sous un autre nom (ses
//  citations dans les vues importees suivent), remplacer celui du projet, ou
//  garder celui du projet. Les variables absentes sont creees (ou non). Tout se
//  fait dans UNE modification du projet (importInto) : l'ecran en fait une
//  commande, et Ctrl+Z annule l'import entier.
//
//  UN MODELE est un paquet d'une vue (kind "modele"), avec un nom, une
//  categorie et une description. Creer une vue depuis un modele (instantiate)
//  importe sa vue sous le nom choisi et ajoute ce qui manque (symboles,
//  images, styles, variables) ; ce qui existe deja dans le projet est garde.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::pkg {

inline constexpr std::string_view kViewsExtension = ".xpgvues";
inline constexpr std::string_view kTemplateExtension = ".xpgmodele";
// 1.11.2 (decision 162) : EXPORTER LES SYMBOLES. Un paquet de genre "symboles" :
// les symboles choisis (vues de role symbole) et ce dont ils ont besoin - les
// symboles qu'ils contiennent, leurs images, leurs styles, les variables IHM
// qu'ils lisent, leurs types IHM, les fonctions IHM qu'ils appellent. Il
// s'importe comme des vues (plan, importInto : renommer ou remplacer chaque
// nom en conflit, une seule commande, Ctrl+Z). Le format du PROJET ne change
// pas : le petit projet du paquet s'ecrit comme le dossier ihm/ (22).
//  Les methodes d'objet (decision 156, API-M) : declarees par objet, elles
//  voyageront avec les objets de leurs symboles ; un symbole qui en a ecrit son
//  petit projet au format 23, et une version qui ne lit que le 22 le refuse.
inline constexpr std::string_view kSymbolsExtension = ".xpgsymboles";
// 1.11.2 (decision 174) : EXPORTER LES TYPES IHM, LES FONCTIONS IHM, LES SCRIPTS
// GENERAUX (Programmation generale), sur le meme paquet. Genres "types",
// "fonctions", "scripts" ; ce qu'ils emportent (collectPrograms) : un type, les
// types IHM de ses membres ; une fonction, les types IHM et les fonctions IHM
// qu'elle appelle (de proche en proche) ; un script, les fonctions et les types
// qu'il emploie ; tous, les variables IHM qu'ils lisent ou ecrivent (comme
// declarations, avec leurs types). "Importer..." ouvre tout paquet (kAllExtensions).
inline constexpr std::string_view kTypesExtension = ".xpgtypes";
inline constexpr std::string_view kFunctionsExtension = ".xpgfonctions";
inline constexpr std::string_view kScriptsExtension = ".xpgscripts";
inline constexpr std::string_view kAllExtensions[] = {".xpgvues", ".xpgsymboles", ".xpgtypes", ".xpgfonctions", ".xpgscripts"};
// 1.11.2 : LE FORMAT DU PAQUET (paquet.txt, "format=") et la version qui l'a
// ecrit ("version="). 1 : lot 20 (sans ces champs) ; 2 : 1.11.2 (les symboles).
// Un paquet d'un format plus recent, ou dont le petit projet a un format IHM
// plus recent que celui de cet outil, est REFUSE, et la raison le dit (la
// version qui l'a ecrit) : rien n'est importe a moitie.
inline constexpr int              kPackageFormat = 2;
inline constexpr std::string_view kWriterVersion = "1.12.1";

struct Manifest {
    int         format{kPackageFormat}; // 1.11.2 : le format du paquet (1 : ecrit avant la 1.11.2)
    std::string writer;                // 1.11.2 : la version qui l'a ecrit ("1.11.2" ; vide avant)
    std::string kind{"vues"};          // "vues" (Exporter les vues), "modele" ou "symboles" (1.11.2) ;
                                       // 1.11.2 (decision 174) : "types", "fonctions", "scripts"
    std::string name;                  // le nom du modele (ou du paquet)
    std::string category;              // modele : Synoptiques, Popups...
    std::string description;
    std::string fromProject;           // le projet d'origine
    std::string fromStation;           // le poste d'origine
    std::string created;               // "2026-09-26 21:40"
    std::vector<std::string> views;    // les vues choisies (les autres sont des dependances)
    std::vector<std::string> items;    // 1.11.2 (decision 174) : les types, fonctions ou scripts choisis
    bool        askVariables{false};   // modele : les variables se choisissent a la creation
};

struct Package {
    Manifest manifest;
    Project  content;                  // les vues et ce qu'elles emportent
};

// Ce que les vues `views` emportent, dans un petit projet (voir l'en-tete).
[[nodiscard]] Package collect(const Project&, const std::vector<Id>& views);
// 1.11.2 : les symboles `symbols` (ce qui n'est pas un symbole est ignore) et ce
// dont ils ont besoin ; genre "symboles".
[[nodiscard]] Package collectSymbols(const Project&, const std::vector<Id>& symbols);
[[nodiscard]] bool isSymbolsPackage(const Package&) noexcept;
// 1.11.2 (decision 174) : les types IHM, les fonctions IHM ou les scripts
// generaux choisis (par identifiant) et ce dont ils ont besoin (voir plus haut).
enum class ProgramKind : std::uint8_t { Types, Functions, Scripts };
[[nodiscard]] Package collectPrograms(const Project&, ProgramKind, const std::vector<Id>& chosen);
[[nodiscard]] bool isProgramsPackage(const Package&) noexcept;              // genre types, fonctions ou scripts
[[nodiscard]] std::string_view extensionOf(const Package&) noexcept;        // ".xpgtypes"...
// "Importer des symboles", "Importer des types IHM"... : ce que le fichier apporte.
[[nodiscard]] std::string importTitle(const Package&);

// Le compte de ce qu'un paquet emporte en plus de ses vues choisies :
// "1 symbole, 2 images, 1 style, 12 variables". 1.11.2 : un paquet de symboles
// compte a part les symboles choisis (chosenSymbols) et ceux qu'ils contiennent
// (symbols) : "2 symboles" puis "1 symbole imbrique, 2 images...".
struct Contents {
    std::size_t views{0}, symbols{0}, popups{0}, templates{0}, resources{0}, styles{0}, variables{0}, types{0}, functions{0};
    std::size_t chosenSymbols{0};
    std::size_t scripts{0};            // 1.11.2 (decision 174) : les scripts generaux
    std::size_t chosen{0};             // 1.11.2 : les types, fonctions ou scripts choisis (compris dans types...)
};
[[nodiscard]] Contents contentsOf(const Package&);
[[nodiscard]] std::string contentsText(const Package&, bool withViews = false);

// En zip et retour. Un fichier qui n'est pas un paquet est refuse (et dit pourquoi).
// 1.11.2 : un fichier d'une version plus recente aussi (kPackageFormat, le
// format IHM) : "ce fichier vient d'une version plus recente de XpgAnalyzer
// (1.12.0) : cette version (1.11.2) ne sait pas le lire...". Son code :
// core::ErrorCode::XmlUnsupportedDtd (tooNew le reconnait).
[[nodiscard]] Bytes                 toZip(const Package&);
[[nodiscard]] bool                  tooNew(const core::Error&) noexcept;
[[nodiscard]] core::Result<Package> fromZip(const Bytes&);
[[nodiscard]] core::Status          writeFile(const Package&, const std::string& path);
[[nodiscard]] core::Result<Package> readFile(const std::string& path);

// ---- importer ---------------------------------------------------------------------
enum class ItemKind : std::uint8_t { View, Symbol, Template, Popup, Resource, Style, Type, Function, Variable, Script };
[[nodiscard]] std::string_view itemKindLabel(ItemKind) noexcept;   // "vue", "symbole"...
enum class State : std::uint8_t { Missing, Same, Different };
enum class Choice : std::uint8_t {
    Add,        // absent : ajoute
    KeepOurs,   // identique, ou different et on garde celui du projet
    Rename,     // different : importe sous `newName` (ses citations suivent)
    Replace,    // different : remplace celui du projet (meme nom)
    Skip,       // variable absente qu'on ne cree pas
};
[[nodiscard]] std::string_view choiceLabel(Choice) noexcept;       // "ajout\xC3\xA9", "renommer"...

struct Item {
    ItemKind    kind{ItemKind::View};
    std::string name;                  // dans le paquet
    State       state{State::Missing};
    Choice      choice{Choice::Add};
    std::string newName;               // Rename : le nom retenu (libre dans le projet)
    std::string detail;                // "identique", "diff\xC3\xA9rente (12 objets, 10 dans le projet)"...
    [[nodiscard]] std::vector<Choice> choices() const;   // ce qu'on peut choisir pour lui
};

struct Plan {
    std::vector<Item> items;
    [[nodiscard]] std::size_t count(State) const;
    [[nodiscard]] Item*       find(ItemKind, std::string_view name);
    [[nodiscard]] const Item* find(ItemKind, std::string_view name) const;
};

// Compare le paquet au projet ; chaque element recoit le choix par defaut :
// ajoute (absent), garde celui du projet (identique ; un style, un type, une
// fonction ou une variable different), importe sous un autre nom (une vue, un
// symbole, une ressource differents). 1.11.2 (decision 174) : un type, une
// fonction ou un script CHOISI (le paquet de son genre) et different s'importe
// par defaut sous un autre nom (T_Mode_2 : les citations du paquet suivent) ;
// renommer ou remplacer, au choix ; un style garde celui du projet (decision 177).
[[nodiscard]] Plan plan(const Project& target, const Package&);

struct ImportResult {
    std::vector<Id>          views;        // les vues choisies, telles qu'importees (leur identifiant)
    std::vector<std::string> added, renamed, replaced, kept, skipped;   // "vue Vue_A", "image logo.png -> logo_2.png"...
    [[nodiscard]] std::string summary() const;
};
// Applique le plan au projet (a faire dans UNE modification : changeProject).
ImportResult importInto(Project& target, const Package&, const Plan&);

// ---- les modeles ----------------------------------------------------------------------
// La vue d'un modele (la premiere des vues choisies) ; nul : paquet vide.
[[nodiscard]] const View* mainView(const Package&);
// Les variables IHM (et les noms) que la vue du modele lit ou ecrit : ceux qu'on
// peut remplacer a la creation ("a choisir a la creation").
[[nodiscard]] std::vector<std::string> templateVariables(const Package&);
// Une vue neuve depuis le modele, sous `name` et `role` ; ce qui manque au
// projet est ajoute, ce qui existe est garde ; `replace` : les noms a
// remplacer dans la vue (Pression_Sud -> Pression_Nord). Rend la vue creee.
Id instantiate(Project& target, const Package&, const std::string& name, const std::string& role,
               const std::vector<std::pair<std::string, std::string>>& replace = {}, ImportResult* result = nullptr);

} // namespace hmi::pkg
