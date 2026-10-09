// =============================================================================
//  hmi/HmiMigrate.hpp - 1.11.18 (refonte des scripts, lot 4) : LA MIGRATION DES BLOCS VAR
// -----------------------------------------------------------------------------
//  Les blocs VAR... END_VAR ecrits dans le code d'un script, d'une fonction, d'une
//  redefinition ou d'un operateur passent dans le modele (hmi::Declaration, lot 3) :
//  lus sans perte par hmi::decl::extract (lot 2), convertis, retires du code. Le code
//  ne garde que sa logique ; le moteur lit les memes declarations, reconstruites
//  (decl::codeOf) : l'execution ne change pas.
//
//                      un script                 une fonction, une redefinition, un operateur
//    VAR               Variable Conservee        Variable Execution (une fonction n'a pas de memoire)
//    VAR_TEMP          Variable Execution        Variable Execution
//    VAR CONSTANT      Constante                 Constante
//    VAR RETAIN        Variable Conservee (D3)   Variable Execution (RETAIN sans effet)
//    VAR_INPUT         - (le script n'est pas    Parametre Entree
//    VAR_IN_OUT          migre : il ne tourne    Parametre Entree/sortie
//    VAR_OUTPUT          pas aujourd'hui)        Parametre Sortie
//  Le commentaire d'une declaration devient sa documentation ; ceux qui ne sont a
//  aucune restent dans le code, a la place du bloc. Une declaration migree est
//  Privee (decision D11 : elle n'etait vue que de son code).
//
//  NE MIGRE PAS (et le rapport dit pourquoi) : un code dont un bloc est illisible
//  (bloc ouvert, declaration illisible), un script qui declare des parametres, une
//  redefinition dont les parametres different de ceux de sa fonction (ou dont la
//  fonction ne migre pas), un operateur qui declare des parametres. Les fonctions
//  internes (FUNCTION... END_FUNCTION) restent dans le code avec leurs blocs : elles
//  deviendront des fonctions du script au lot 8 (decision D2).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hmi::migrate {

// Ou est un code a migrer.
struct Place {
    enum class Kind : std::uint8_t { Script, ViewScript, Function, SymbolFunction, Override, TypeOperator, SymbolOperator };
    Kind        kind{Kind::Script};
    Id          view{kNoId};       // un script de vue, une fonction ou un operateur de symbole, une redefinition
    Id          object{kNoId};     // une redefinition : l'instance
    Id          type{kNoId};       // un operateur de type IHM : le type
    Id          id{kNoId};         // le script, la fonction, l'operateur
    std::string function;          // une redefinition : la fonction redefinie
    std::string label;             // "script Statistiques_Pression", "Vue_Armoire_A.OnOpen", "fonction Vanne.Ouvrir"...
};

struct Note {
    bool        attention{false};  // vrai : un point d'attention (le rapport le montre a part)
    std::string text;
};

struct Item {
    Place                    place;
    bool                     migrated{false};   // faux : laisse tel quel (`why`)
    std::string              why;
    std::vector<Declaration> decls;             // les declarations converties (sans identifiant : apply en donne)
    std::string              body;              // le code sans ses blocs
    std::size_t              comments{0};       // commentaires devenus documentation
    std::size_t              defaults{0};       // valeurs initiales, constantes ou par defaut
    std::vector<Note>        notes;
};

struct Plan {
    std::vector<Item> items;                    // chaque code qui a un bloc VAR, migre ou non
    [[nodiscard]] std::size_t migrated() const noexcept;
    [[nodiscard]] std::size_t skipped() const noexcept;
    [[nodiscard]] std::size_t declarations() const noexcept;
    [[nodiscard]] std::size_t attentions() const noexcept;
};

// Un code de ce projet a-t-il encore un bloc de declaration dans son texte ?
[[nodiscard]] bool needed(const Project&);
// Ce que la migration ferait. `only` : les seuls codes a considerer (vide : tous).
[[nodiscard]] Plan plan(const Project&, const std::function<bool(const Place&)>& only = {});
// Applique le plan (une commande : Ctrl+Z rend les corps a l'octet pres) : chaque code
// migre recoit ses declarations, des identifiants neufs, et son corps sans blocs.
// Rend le nombre de codes changes.
std::size_t apply(Project&, const Plan&);
// Le rapport, en texte (le volet le montre ; le guide le cite) : un resume, puis code par
// code - ses declarations, ses valeurs, ses commentaires, ses points d'attention.
[[nodiscard]] std::string report(const Plan&);
// "12 codes migres, 41 declarations ; 2 laisses tels quels ; 3 points d'attention" ;
// `before` (la question posee avant d'agir) : "12 codes a migrer, 41 declarations...".
[[nodiscard]] std::string summary(const Plan&, bool before = false);

} // namespace hmi::migrate
