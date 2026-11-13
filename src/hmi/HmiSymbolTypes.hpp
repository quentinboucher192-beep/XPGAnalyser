// =============================================================================
//  hmi/HmiSymbolTypes.hpp - 1.51.0 : les types IHM d'un symbole
// -----------------------------------------------------------------------------
//  UN SYMBOLE PORTE SES PROPRES TYPES IHM (<< faire en sorte que les symboles
//  puissent porter des types IHM >>) : des structures, des enumerations, des
//  interfaces, des rappels - les memes que ceux du projet (membres, methodes,
//  visibilite, heritage, interfaces, generiques), avec le meme editeur.
//
//  OU ILS VIVENT. Dans la liste des types du projet (Programs::types), marques
//  par HmiType::symbol (le symbole qui les porte), leur espace de noms (ns) etant
//  le nom du symbole : leur nom complet est MonSymbole.T_Etat. Tout ce qui lit les
//  types (le moteur, Compiler, Generer, le depliage, l'export C) les voit ainsi.
//  Comme les popups d'un symbole (View::ownerSymbol), ils partent avec lui : le
//  dupliquer, l'exporter, l'importer, le copier, le supprimer (la corbeille),
//  l'enregistrer et le relire.
//
//  LA PORTEE (la regle de qualification) :
//   - DANS le symbole (ses parametres, ses fonctions, ses scripts, les formules de
//     ses objets, ses alarmes, ses types) : T_Etat, par son nom court. Le code du
//     symbole est un item de l'espace MonSymbole (hmi::names) : un nom court y vise
//     d'abord les types du symbole, puis ceux du projet - la portee la plus proche
//     gagne (un T_Etat du projet est cache par celui du symbole) ;
//   - HORS du symbole (une vue, une instance posee, un script, une fonction, un
//     autre symbole) : MonSymbole.T_Etat, toujours - comme une fonction d'un
//     symbole se nomme par son instance (Vue.Vanne_3.Ouvrir), un type, lui, n'a pas
//     d'instance : il se nomme par son symbole. Compiler propose de qualifier un
//     nom court.
//  Un type du symbole peut employer les types du projet (membres, parent,
//  interfaces). Les textes de type ecrits dans le symbole (le type d'un parametre,
//  d'une declaration, d'un membre) restent tels qu'ecrits ; quand un nom court
//  designerait un autre type (un homonyme du projet ou d'un autre symbole),
//  normalize() l'ecrit en entier (MonSymbole.T_Etat) dans la meme commande.
//
//  LE FICHIER : la ligne type_ihm d'un type d'un symbole porte symbole=<id de la
//  vue> (et espace=<nom du symbole>). Une IHM Studio d'avant ignore symbole= : elle
//  lit un type de l'espace MonSymbole (rien ne se perd ; voir docs/FORMATS.md).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::symtypes {

// Le type est-il porte par un symbole ?
[[nodiscard]] inline bool owned(const HmiType& t) noexcept { return t.symbol != kNoId; }
// Les types du symbole `symbol`, dans l'ordre du projet ; leurs identifiants.
[[nodiscard]] std::vector<const HmiType*> typesOf(const Project&, Id symbol);
[[nodiscard]] std::vector<Id> typeIdsOf(const Project&, Id symbol);
// Le symbole porte-t-il au moins un type ?
[[nodiscard]] bool hasTypes(const Project&, Id symbol);
// Le symbole qui porte le type ; nul : un type du projet (ou son symbole introuvable).
[[nodiscard]] const View* ownerOf(const Project&, const HmiType&);
// Le type `name` du symbole (nom court, sans casse) ; nul : aucun.
[[nodiscard]] const HmiType* find(const Project&, Id symbol, std::string_view name);
// LA PORTEE DES NOMS du code d'une vue : le nom du symbole s'il porte des types (son espace : ses types s'y lisent
// sans prefixe) ; vide sinon (une vue, une popup, un symbole sans type).
[[nodiscard]] std::string scopeOf(const Project&, const View&);
// L'espace `ns` n'est-il que celui d'un symbole (chaque item de cet espace est un type de ce symbole) ?
[[nodiscard]] bool symbolScope(const Project&, std::string_view ns);
// Le projet emploie-t-il des espaces de noms HORS des symboles (un item, une bibliotheque, un espace vide garde) ?
// Le dossier Bibliotheques et la page Espaces ne montrent pas ceux des symboles.
[[nodiscard]] bool projectSpacesUsed(const Project&);

// ---- les noms ---------------------------------------------------------------------------------
// Un nom permis pour un type du symbole : un identifiant, pas un type elementaire, libre parmi les types du symbole
// (`self` : le type renomme). Un homonyme du projet est permis (la portee la plus proche).
[[nodiscard]] bool validName(const Project&, Id symbol, std::string_view name, Id self = kNoId, std::string* why = nullptr);
// "T_Etat", "T_Etat_2"... : un nom libre parmi les types du symbole.
[[nodiscard]] std::string uniqueName(const Project&, Id symbol, std::string_view wanted);
// MonSymbole.T_Etat.
[[nodiscard]] std::string qualifiedOf(const Project&, const HmiType&);

// ---- l'edition (chaque fonction sur un projet de commande : Ctrl+Z reprend le tout) -------------
// Creer un type dans le symbole : une structure (deux membres pour commencer : Valeur, Marche), une enumeration (deux
// valeurs), une interface (une signature d'exemple), un rappel (CALLBACK (x : REAL) : BOOL). Son identifiant ; kNoId
// et `why` : un symbole introuvable, un nom refuse.
Id addType(Project&, Id symbol, std::string_view name, HmiTypeKind kind = HmiTypeKind::Structure, std::string* why = nullptr);
// Rattacher un type cree ailleurs (le volet des types, l'enumeration) au symbole : son espace devient le symbole.
bool adopt(Project&, Id type, Id symbol);
// Dupliquer un type du symbole dans le meme symbole (T_Etat_copie) : son identifiant ; kNoId : introuvable.
Id duplicateType(Project&, Id type);
// Copier les types du symbole `from` dans le symbole `to` (dupliquer, coller un symbole) : identifiants neufs, l'espace
// du nouveau symbole ; les textes de `to` (et des types copies) qui nommaient from.T nomment to.T. Rend le nombre copie.
std::size_t copyTypes(Project&, Id from, Id to);
// Retirer les types du symbole (le symbole part). Rend le nombre retire.
std::size_t removeTypes(Project&, Id symbol);
// RENOMMER UN TYPE DU SYMBOLE en `newName` (renameTypeEverywhere y mene) : dans le symbole et ses types, chaque nom
// court qui le vise et son nom complet ; ailleurs, son nom complet seul (MonSymbole.T_Etat -> MonSymbole.T_Mode, et
// MonSymbole.T_Etat#x). Un homonyme du projet ou d'un autre symbole ne bouge pas. Rend le nombre de changements.
std::size_t renameType(Project&, Id type, std::string_view newName);
// Renommer un symbole de `from` en `to` : l'espace de ses types et chaque texte du projet qui les nomme en entier
// (from.T_Etat -> to.T_Etat). A appeler APRES le renommage de la vue. Rend le nombre de changements.
std::size_t renameScope(Project&, std::string_view from, std::string_view to);
// Le meme dans un seul symbole et ses types (une copie, un element rendu par la corbeille sous un autre nom).
std::size_t renameScopeIn(Project&, Id symbol, std::string_view from, std::string_view to);

// REMETTRE EN ORDRE (apres chaque commande, a la lecture d'un projet, apres un import) :
//  - l'espace de chaque type d'un symbole est le nom de son symbole ;
//  - un type dont le symbole n'existe plus : `dropOrphans` (une commande qui supprime le symbole) le retire ; sinon (un
//    fichier dont la vue manque) il devient un type du projet, dans le meme espace - rien ne se perd ;
//  - dans un symbole, un nom court d'un de ses types que le projet lirait autrement (un homonyme) est ecrit en entier
//    dans les textes de type (parametres, retours, declarations, membres, parents, interfaces, methodes).
// Rend le nombre de changements.
std::size_t normalize(Project&, bool dropOrphans = true);
// Le meme pour une vue modifiee (une commande de vue) : ses textes de type, vus du projet `p`.
std::size_t normalizeView(const Project& p, View& v);

// ---- le moteur et les controles ----------------------------------------------------------------
// Le code d'un symbole, ses types nommes en entier (T_Etat -> MonSymbole.T_Etat) ; sans type : tel quel.
[[nodiscard]] std::string qualify(const Project&, const View& symbol, std::string_view code, bool function = false,
                                  const std::vector<std::string>& hidden = {});
// Un texte de type vu du symbole : T_Etat -> MonSymbole.T_Etat, ARRAY[1..3] OF T_Etat -> ARRAY[1..3] OF MonSymbole.T_Etat.
[[nodiscard]] std::string typeIn(const Project&, const View& symbol, std::string_view type);
// Le texte montre dans le symbole : MonSymbole.T_Etat -> T_Etat (ses types sans prefixe) ; le reste tel quel.
[[nodiscard]] std::string shortIn(const Project&, const View& symbol, std::string_view type);
// Les noms courts des types du symbole (l'autocompletion du code du symbole), tries.
[[nodiscard]] std::vector<std::string> shortNames(const Project&, Id symbol);

} // namespace hmi::symtypes
