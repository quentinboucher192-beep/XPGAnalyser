// =============================================================================
//  project/MemberTree.hpp - ce qu'une ligne depliee montre dessous (lot API 7)
// -----------------------------------------------------------------------------
//  Une variable se deplie sur ses membres, ET CHAQUE MEMBRE A SON TOUR, sans
//  limite de profondeur :
//
//    * un DDT         sur ses champs              Armoires[0].ana
//    * une instance   sur ses broches, ses publiques et ses privees
//      de bloc        (DFB du projet), ou ses broches (bloc de la bibliotheque)
//    * un tableau     sur ses elements            tempon[104]
//
//  UN GRAND TABLEAU SE RANGE PAR PAQUETS. Au-dela de 100 elements, ses enfants
//  sont des paquets de 100 ([0 ... 99], [100 ... 199]...) ; au-dela de 10 000,
//  des paquets de 10 000, qui se deplient en paquets de 100 ; et ainsi de suite.
//  Rien n'est cache, et l'ecran ne fabrique que les lignes qu'on deplie : un
//  tableau de 50 000 elements s'ouvre aussi vite qu'un de 5.
//
//  UN TABLEAU A PLUSIEURS DIMENSIONS se deplie une dimension apres l'autre :
//  ARRAY[0..3, 0..7] OF INT montre ses lignes [2, ...], puis ses cases [2, 5].
//  Le nom d'une case est celui que la simulation connait : Grille[2,5].
//
//  Un noeud "reel" est une variable ou un membre (il a une valeur) ; un noeud
//  "groupe" est un paquet ou une ligne d'un tableau (il n'en a pas, il range).
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project::members {

// Un tableau lu dans le texte de son type : "ARRAY[0..3, 0..7] OF INT".
struct ArrayShape {
    std::vector<std::pair<std::int64_t, std::int64_t>> dims;   // [bas, haut] de chaque dimension
    std::string element;                                        // "INT"
    [[nodiscard]] bool valid() const noexcept { return !dims.empty(); }
    // Tous les elements (le produit des dimensions), plafonne a 10^15.
    [[nodiscard]] std::int64_t count() const noexcept;
};
// Invalide si ce n'est pas un tableau, ou si une borne n'est pas un nombre.
[[nodiscard]] ArrayShape parseArray(std::string_view type);

// "[2,5]" : le suffixe du nom d'un element, tel que la simulation l'ecrit.
[[nodiscard]] std::string indexSuffix(const std::vector<std::int64_t>& indices);

// La taille d'un paquet : au-dela, un niveau de rangement de plus.
constexpr std::int64_t kChunk = 100;

struct Node {
    std::string path;    // reel : le chemin de la variable ; groupe : celui du tableau
    std::string type;    // reel : son type ; groupe : le type du tableau, entier
    std::string key;     // unique : l'etat deplie se retient par cette cle
    std::string label;   // sous son parent : ".PT1", "[3]", "[2, 5]", "[100 ... 149]", "[2, ...]"
    std::string what;    // "champ", "element", "entree", "sortie", "entree-sortie",
                         // "publique", "privee", "paquet", "ligne" (en francais, accentue)
    bool        real{true};
    // Un groupe : les indices deja fixes, la dimension qu'il parcourt, sa plage.
    std::vector<std::int64_t> fixed;
    std::size_t  dim{0};
    std::int64_t first{0}, last{-1};
    [[nodiscard]] std::int64_t size() const noexcept { return last - first + 1; }
};

// La racine d'une variable (sa cle est son chemin).
[[nodiscard]] Node root(std::string path, std::string type);
// Les enfants d'un noeud, dans l'ordre de la declaration.
[[nodiscard]] std::vector<Node> children(const domain::Project& p, const Node& n);
// Sans les fabriquer : un tableau non vide, un DDT, une instance de bloc, un groupe.
[[nodiscard]] bool hasChildren(const domain::Project& p, const Node& n);
// BOOL, INT, REAL, STRING[16]... : rien dessous.
[[nodiscard]] bool isElementary(std::string_view type);
// Ce qu'un groupe contient, pour sa colonne Type : "paquet de 100", "ARRAY[0..7] OF INT".
[[nodiscard]] std::string groupType(const Node& n);

// ---- lot API 7 (l'arbre, l'onglet Variables) : ce qu'une ligne montre --------
//
// LA NATURE D'UN TYPE, pour l'icone d'une ligne : elementaire (BOOL,
// STRING[16]...), un tableau, un DDT du projet, un bloc (un DFB du projet, un
// bloc de la bibliotheque) ; Unknown : ni le projet ni la bibliotheque ne le
// connaissent.
enum class Nature : std::uint8_t { Elementary, Array, Structure, Block, Unknown };
[[nodiscard]] Nature natureOf(const domain::Project& p, std::string_view type);

// LA DECLARATION D'UN MEMBRE : la variable du projet qui declare `child`,
// enfant de `parent` - le champ du DDT, la broche ou la variable du DFB. Un
// element, un paquet, une ligne gardent celle qui declare le tableau
// (`parentDecl`, celle du parent). Une broche d'un bloc de la bibliotheque
// n'en a pas : kNoIndex.
[[nodiscard]] domain::Index declarationOf(const domain::Project& p, const Node& parent, domain::Index parentDecl,
                                          const Node& child);
// SON COMMENTAIRE : celui de sa declaration (`decl`, rendue par declarationOf) ;
// pour un element, celui que la declaration du tableau donne a cet element
// (<instanceElementDesc name="[2]">). Vide : aucun - un paquet, une ligne n'en
// ont pas.
[[nodiscard]] std::string commentOf(const domain::Project& p, domain::Index decl, const Node& parent, const Node& child);

} // namespace project::members
