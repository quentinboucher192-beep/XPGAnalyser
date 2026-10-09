// =============================================================================
//  hmi/HmiDeclEdit.hpp - 1.11.18 (refonte des scripts, lot 5) : EDITER LES
//  DECLARATIONS D'UN CODE
// -----------------------------------------------------------------------------
//  Les gestes des onglets Constantes, Variables, Parametres (et Locales d'une
//  fonction) - ajouter, supprimer, dupliquer, deplacer, modifier une case - sur les
//  declarations du modele (hmi::Declaration, lot 3) d'un code : un script general
//  ou de vue, une fonction IHM ou de symbole, une redefinition, un operateur. Sans
//  ecran : les grilles (app/hmi/HmiDeclGrid) et le collage depuis Excel les
//  appellent dans une commande (Ctrl+Z) ; les essais aussi.
//
//  CHAQUE GESTE VALIDE AVANT DE TOUCHER AU PROJET : refuse (faux et `why`), il n'a
//  rien change. Un nom est un identifiant ST, ni reserve, ni pris dans ce code (ses
//  autres declarations, un bloc VAR encore ecrit dans son texte, le nom de la
//  fonction, a, b et Resultat d'un operateur, les parametres d'une fonction
//  redefinie). Un type est un type de base, un tableau, un type IHM du projet...
//
//  RENOMMER une declaration renomme ses utilisations dans son code (hmi::replacePath :
//  ni les chaines, ni les commentaires, ni les membres x.Nom, ni les arguments nommes
//  f(Nom := 1)) et dans les valeurs des autres declarations ; un parametre d'une
//  fonction de symbole, aussi dans le corps de ses redefinitions (elles le lisent).
//  Une declaration n'est vue que de son code jusqu'au lot 7 (les noms qualifies) :
//  il n'y a rien d'autre a renommer - sauf l'argument nomme d'un appel d'une fonction
//  (F(Ancien := 1)), que le renommage symbolique du lot 10 suivra.
//
//  LES LIBELLES de l'ecran (Execution, Conservee, Persistante ; Entree, Entree/sortie,
//  Sortie ; Public, Prive) se relisent avec tolerance - le collage d'Excel : sans
//  casse ni accents, en anglais, et les mots des anciens blocs (VAR_TEMP, VAR,
//  RETAIN ; IN, OUT, IN_OUT).
// =============================================================================
#pragma once

#include "HmiDecl.hpp"
#include "HmiMigrate.hpp"   // migrate::Place : l'adresse d'un code
#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::decledit {

using Place = migrate::Place;

// Ou vit un code : son corps et ses declarations (nuls : introuvable).
struct Code {
    std::string*                    body{nullptr};
    std::vector<Declaration>*       decls{nullptr};
    decl::Role                      role{decl::Role::Script};
    const std::vector<Declaration>* inherited{nullptr};   // une redefinition : les declarations de sa fonction
    std::vector<std::string>        reserved;             // les noms du code que nul ne prend (la fonction, a, b...)
    bool                            declares{true};       // faux : un script C ou C++ (le langage veut ses declarations dans le code)
    [[nodiscard]] bool valid() const noexcept { return body && decls; }
};
[[nodiscard]] Code locate(Project&, const Place&);
// La meme, en lecture (les pointeurs ne servent qu'a lire).
[[nodiscard]] Code locate(const Project&, const Place&);

// ---- les onglets -------------------------------------------------------------
enum class Tab : std::uint8_t { Constants, Variables, Parameters };
[[nodiscard]] DeclKind kindOf(Tab) noexcept;
// Ceux d'un code : un script - Constantes, Variables ; une fonction - Parametres,
// Variables (ses locales), Constantes ; un operateur, une redefinition - Variables
// (locales), Constantes. Un script C ou C++ : aucun.
[[nodiscard]] std::vector<Tab> tabsOf(const Code&);
// "Constantes", "Variables", "Locales" (une fonction, un operateur), "Parametres" (accentues).
[[nodiscard]] std::string tabLabel(Tab, decl::Role);
// Les lignes d'un onglet : les indices dans `decls` des declarations de son genre, dans l'ordre.
[[nodiscard]] std::vector<std::size_t> rowsOf(const std::vector<Declaration>&, Tab);

enum class Column : std::uint8_t { Name, Type, Value, Storage, Mode, Visibility, Description };
// Les colonnes d'un onglet, dans l'ordre de la grille (sans les colonnes calculees).
[[nodiscard]] std::vector<Column> columnsOf(Tab, decl::Role);
// "Nom", "Type", "Valeur" (une constante) / "Initiale" (une variable) / "Defaut" (un
// parametre), "Stockage", "Mode", "Visibilite", "Documentation" (accentues).
[[nodiscard]] std::string columnTitle(Column, Tab);
// Le texte d'une case (ce que la grille montre et que Ctrl+C copie).
[[nodiscard]] std::string cellText(const Declaration&, Column);

// ---- les libelles ------------------------------------------------------------
[[nodiscard]] std::string_view storageLabel(Storage) noexcept;        // "Execution", "Conservee", "Persistante"
[[nodiscard]] std::string_view storageHelp(Storage) noexcept;         // ce que le stockage veut dire (une phrase)
[[nodiscard]] std::string_view modeLabel(PassMode) noexcept;          // "Entree", "Entree/sortie", "Sortie"
[[nodiscard]] std::string_view visibilityLabel(Visibility) noexcept;  // "Public", "Prive"
[[nodiscard]] std::optional<Storage>    storageFromText(std::string_view) noexcept;
[[nodiscard]] std::optional<PassMode>   modeFromText(std::string_view) noexcept;
[[nodiscard]] std::optional<Visibility> visibilityFromText(std::string_view) noexcept;
// Les stockages permis : un script les trois ; une fonction, un operateur : Execution.
[[nodiscard]] std::vector<Storage> storagesFor(decl::Role) noexcept;
// Les types proposes : les types de base, puis les structures et enumerations du projet.
[[nodiscard]] std::vector<std::string> typeChoices(const Project&);
// Le type est-il permis pour une declaration (de base, riche, ou un type IHM du projet) ?
[[nodiscard]] bool typeAllowed(const Project&, std::string_view type);

// ---- ce que la grille montre ---------------------------------------------------
struct Use {
    int line{0};                 // 1 = la premiere ligne du corps
    int column{0};               // 1 = le premier octet de la ligne
    int length{0};
};
// Les utilisations d'un nom dans un code ST (sans casse ; ni commentaire, ni chaine, ni
// membre, ni argument nomme ; un litteral type T#5s ou E_Mode#Auto n'en est pas une).
[[nodiscard]] std::vector<Use> usesIn(std::string_view body, std::string_view name);
// Le nombre d'utilisations de la declaration `index` : son code et les valeurs des autres.
[[nodiscard]] std::size_t usageCount(const Code&, std::size_t index);
// Les fautes de chaque declaration (meme ordre que decls ; "" : juste), jointes par " ; ".
[[nodiscard]] std::vector<std::string> faults(const Project&, const Code&);

// ---- les noms ------------------------------------------------------------------
// Un nom libre dans ce code : `base`, puis base2, base3... (sans casse).
[[nodiscard]] std::string freeName(const Code&, std::string_view base);
// `name` est-il permis pour la declaration `self` (un indice de decls ; npos : une nouvelle) ?
[[nodiscard]] bool nameAllowed(const Code&, std::string_view name, std::size_t self, std::string* why);

// ---- les gestes (dans une commande : hmi::changeProject) ----------------------------
//  `row` : une ligne de l'onglet (rowsOf), pas un indice de decls. Faux et `why` : refuse,
//  le projet n'a pas bouge.
// Une declaration neuve apres la ligne `after` (-1 : a la fin) ; `name` vide : un nom libre
// (Constante, Variable, Parametre). Ses valeurs par defaut : Constante REAL 0.0, Variable
// INT 0 Execution, Parametre REAL Entree ; Publique dans un script, Privee ailleurs (une
// locale reste locale). `made` : son identifiant.
bool add(Project&, const Place&, Tab, int after, std::string_view name, Id* made, std::string* why);
bool remove(Project&, const Place&, Tab, const std::vector<std::size_t>& rows, std::string* why);
// Chaque ligne copiee juste apres elle, nommee Nom_copie (Nom_copie2...).
bool duplicate(Project&, const Place&, Tab, const std::vector<std::size_t>& rows, std::vector<Id>* made, std::string* why);
// Monter (-1) ou descendre (+1) d'une place dans son onglet. L'ordre des parametres est
// la signature : celui des appels sans nom.
bool move(Project&, const Place&, Tab, std::size_t row, int delta, std::string* why);
// Ecrire une case (le texte de la grille ou d'Excel). Le nom renomme les utilisations.
// 1.11.21 : un TYPE change - la valeur qui ne lui convient plus est retiree (une constante
// prend la valeur nulle de son type : 0, 0.0, FALSE, '', T#0s) et `note` le dit ; une VALEUR
// qui ne convient pas au type est refusee (valueMisfit).
bool set(Project&, const Place&, Tab, std::size_t row, Column, std::string_view text, std::string* why,
         std::string* note = nullptr);
// 1.11.21 : une valeur (initiale, d'une constante, par defaut) convient-elle au type ? Vide :
// oui, ou on ne sait pas le dire (un nom, une expression, un type inconnu) ; sinon pourquoi.
//   - une structure, un tableau de structures : non (ils prennent les valeurs de leur type) ;
//   - une enumeration : une de ses valeurs (T_MODE#Auto, Auto, son nombre) ;
//   - un litteral (TRUE, 5, 16#FF, 2.5, 'texte', T#5s, INT#3) : sa conversion vers le type
//     (la regle du lot 6) - interdite, ou un entier hors des bornes : non. Un tableau d'un type
//     de base : son element (une valeur seule remplit toutes les cases).
[[nodiscard]] std::string valueMisfit(const Project&, std::string_view type, std::string_view value);

} // namespace hmi::decledit
