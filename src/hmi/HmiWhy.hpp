// =============================================================================
//  hmi/HmiWhy.hpp - 1.12.27 : « POURQUOI ? » EN SIMULATION
// -----------------------------------------------------------------------------
//  Un objet de la vue en marche, et ce qu'il montre maintenant : chacune de ses
//  proprietes calculees (sa formule, sa valeur), les termes de la formule et la
//  valeur de chacun (les operandes d'un AND / OR, les deux cotes d'une
//  comparaison, les conditions d'un etat multiple, les noms lus), et l'etat
//  retenu : « allume (#2ECC71) », « etat 3 sur 3 : 2 = Defaut | #E5534B »,
//  « zone 60..80 : #F2C94C », « cache ».
//
//  Le moteur ne connait ni l'ecran ni les forcages : le volet de simulation
//  montre le rapport (onglet Pourquoi ?), y ajoute les libelles de l'inspecteur
//  et dit quelle variable est forcee.
// =============================================================================
#pragma once

#include "HmiExpr.hpp"
#include "HmiLive.hpp"
#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::why {

// Un terme d'une formule et sa valeur maintenant.
struct Term {
    std::string text;            // "Niveau > 80", "NOT Defaut", "Pompes[2].Marche"
    std::string value;           // "TRUE", "85.2" ; le message si `error`
    bool        error{false};
    bool        name{false};     // un nom lu (une variable, un membre) : il se force
};

// Une propriete calculee de l'objet.
struct Item {
    std::string       key;       // "value", "visible", "fill", "text"...
    std::string       formula;   // la source (sans « = ») ; un texte a trous tel quel
    bool              text{false};
    std::string       value;     // le resultat, en texte ; le message si `error`
    bool              error{false};
    std::vector<Term> terms;
    std::string       state;     // l'etat retenu ; vide : rien a dire de plus que la valeur
};

struct Report {
    Id                object{kNoId};
    std::string       name, kind;
    std::vector<Item> items;     // vide : rien ne se calcule (ses proprietes sont fixes)
};

// Les termes d'une formule, dans l'ordre ou ils comptent : ses operandes de tete
// (AND, OR, XOR, &&, ||), sinon les deux cotes d'une comparaison, puis les noms
// lus qui ne sont pas deja un terme. Douze au plus, sans doublon.
[[nodiscard]] std::vector<std::string> termsOf(std::string_view formula);
// Les trous d'un texte ("Pression : {PT1:0.0} bar" -> PT1), sans leur format.
[[nodiscard]] std::vector<std::string> holesOf(std::string_view text);
// Les noms lus d'une formule (variables, membres, cases de tableau), sans les
// fonctions appelees, les mots cles ni les litteraux types (T#2s, Mode#Auto).
[[nodiscard]] std::vector<std::string> namesOf(std::string_view formula);

// Le rapport d'un objet : `shown` tel que la vue en marche le montre, `values`
// les valeurs calculees de la vue (celles de l'objet y sont prises), `scope` les
// noms propres a la vue (une popup, ses parametres).
[[nodiscard]] Report explain(const Object& shown, const std::vector<LiveValue>& values, sim::Environment& env,
                             const Scope* scope = nullptr);

// Le rapport en texte (Copier) : une ligne par propriete, une par terme (en retrait).
[[nodiscard]] std::string toText(const Report&);

} // namespace hmi::why
