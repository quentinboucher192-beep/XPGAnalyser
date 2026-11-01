// =============================================================================
//  app/ai/AiVarFolders.hpp - l'Assistant IA : le dossier des variables qu'il cree
// -----------------------------------------------------------------------------
//  1.22.1 (<< l'IA doit bien comprendre le contexte et separer les variables dans des
//  dossiers, sous-dossiers, pour ne jamais les melanger >>). Une variable IHM a un
//  dossier (hmi::Variable::folder : "Station de pompage/Pompes/P1", des noms separes
//  par /, vide : la racine) ; les dossiers du projet : hmi::types::allFolders (ceux de
//  Programs::folders, ceux des variables et leurs parents).
//
//  creer_variables porte un dossier par variable (Operation::VariableSpec::folder),
//  sinon celui de l'operation ("dossier"). Ici, des regles SIMPLES et DETERMINISTES -
//  jamais un nom de variable invente, seulement le rangement :
//
//    LE CHEMIN     normalize : des noms separes par /, les blancs rognes (et resserres),
//                  les noms vides retires ; kMaxDepth niveaux, kMaxName caracteres par nom,
//                  kMaxPath en tout ; ni \ : * ? " < > | ni caractere de controle (une
//                  erreur claire). Vide : aucun dossier donne.
//    L'EXISTANT    resolve : un dossier qui existe deja est repris tel qu'il s'ecrit, sans
//                  tenir compte de la casse ni des blancs en trop (<< pompes >> -> << Pompes >>),
//                  niveau par niveau : jamais un double.
//    LE PREFIXE    prefixOf : l'equipement d'un nom - le premier morceau (entre deux _) qui
//                  porte un chiffre (P1_Marche, Marche_P1 -> P1 ; un morceau tout en chiffres
//                  garde le precedent : Pompe_1_Marche -> Pompe_1) ; sans chiffre, le premier
//                  morceau (Cuve_Niveau -> Cuve : un prefixe simple, pas un equipement). Un nom
//                  d'un seul morceau n'a pas de prefixe.
//    SANS DOSSIER  place, pour une variable creee sans dossier (ni le sien ni celui de son
//                  operation, et qu'aucun << ranger >> de la proposition ne range) :
//                    1. le dossier ou vivent deja les variables du meme prefixe (celui qui en
//                       a le plus ; a egalite, le premier rencontre) ;
//                    2. sinon, si au moins deux variables creees ensemble (la meme operation)
//                       ont ce prefixe : un dossier a son nom, a la racine ;
//                    3. sinon la racine, comme avant.
//                  Chaque dossier deduit est DIT (un avertissement de l'apercu).
//    LE MELANGE    un meme dossier final (pas la racine) qui recevrait les variables d'au
//                  moins deux equipements (des prefixes a chiffre), chacun avec au moins
//                  deux variables (celles qui y sont deja comprises), dont une creee ici : un
//                  avertissement, et la separation proposee (Pompes/P1, Pompes/P2). Une liste
//                  de mesures (une variable par capteur) n'est pas un melange.
//
//  Le contexte envoye au modele montre l'arborescence reelle (treeText : chaque dossier,
//  ses variables, bornee en taille) ; l'IA integree range avec la meme regle (suggest).
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::varfold {

inline constexpr std::size_t kMaxDepth = 6;     // les niveaux d'un chemin, au plus
inline constexpr std::size_t kMaxName = 60;     // un nom de dossier (comme hmi::types::validFolder)
inline constexpr std::size_t kMaxPath = 128;    // le chemin entier (comme creer_dossier)

// Le chemin remis en forme ("Station de pompage / pompes/" -> "Station de pompage/pompes") ; faux et `why` : un caractere
// interdit, un nom trop long, trop de niveaux. Vide (ou fait de / et de blancs) : vrai, `out` vide (aucun dossier).
[[nodiscard]] bool normalize(std::string_view raw, std::string& out, std::string* why = nullptr);
// La cle d'un chemin : sans la casse (lettres accentuees comprises) ni les blancs en trop - deux chemins de meme cle sont
// le meme dossier.
[[nodiscard]] std::string key(std::string_view path);
// Un chemin normalise, ecrit comme les dossiers `known` qui existent deja (niveau par niveau ; un niveau neuf garde son
// ecriture).
[[nodiscard]] std::string resolve(std::string_view normalized, const std::vector<std::string>& known);

// Le prefixe d'un nom de variable (voir plus haut).
struct Prefix {
    std::string text;              // "P1", "Pompe_1", "Cuve" ; vide : aucun
    bool        equipment{false};  // un prefixe a chiffre : un equipement
};
[[nodiscard]] Prefix prefixOf(std::string_view name);

// Une variable creee : son nom, le dossier demande (normalise ; vide : aucun), rangee par un << ranger >> (alors `folder`
// est le dossier de ce ranger : montre, ni cree ni deduit ici).
struct Request {
    std::string name;
    std::string folder;
    bool        stored{false};
};
struct Placed {
    enum class How : std::uint8_t { Given, Stored, Existing, Common, Root };
    std::string folder;            // le dossier final, tel qu'il s'ecrira ("" : la racine)
    How         how{How::Root};    // donne, range par ranger, deduit (regle 1, regle 2), la racine
    std::string prefix;            // deduit : le prefixe
};
struct Placement {
    std::vector<Placed>      placed;    // une par demande, dans l'ordre
    std::vector<std::string> created;   // les dossiers a creer (chemins complets, chacun une fois, dans l'ordre)
    std::vector<std::string> warnings;  // les dossiers deduits, les melanges
};
// Le rangement des variables creees ENSEMBLE (une operation creer_variables), sur le projet tel qu'il est avant elles.
[[nodiscard]] Placement place(const hmi::Project&, const std::vector<Request>&);
// L'IA integree : le dossier que place donnerait a ces variables creees ensemble sans dossier ("" : la racine).
[[nodiscard]] std::vector<std::string> suggest(const hmi::Project&, const std::vector<std::string>& names);

// Le contexte : l'arborescence des dossiers de variables (un dossier par ligne, en retrait ; ses variables, et avec ses
// sous-dossiers), au plus `maxBytes` - les premiers niveaux d'abord ; le reste est compte. `folders`, `listed` : combien
// de dossiers, combien montres.
[[nodiscard]] std::string treeText(const hmi::Project&, std::size_t maxBytes, std::size_t* folders = nullptr, std::size_t* listed = nullptr);

} // namespace app::ai::varfold
