// =============================================================================
//  hmi/HmiExprCheck.hpp - Lot API 8 : les expressions impossibles
// -----------------------------------------------------------------------------
//  Expression::compile dit si une expression se LIT (la syntaxe du ST). Ce
//  module dit si elle peut MARCHER, sans l'executer :
//   - chaque nom existe (sinon : le nom connu le plus proche est propose -
//     une variable renommee sur place laisse l'ancien nom dans l'expression) ;
//   - chaque fonction est connue et recoit le bon nombre d'arguments ;
//   - les types se tiennent : un texte n'est ni compare a un nombre ni calcule,
//     une visibilite recoit un booleen, une couleur un texte '#RRGGBB' ;
//   - aucune division par une constante nulle ;
//   - une ecriture (action, champ de saisie) vise une variable qu'on peut
//     ecrire, pas une constante ni une variable en lecture seule.
//  Les membres et les indices constants des variables IHM composees passent
//  par types::pathProblems (le meme controle que les scripts).
//
//  Un type n'est connu que s'il est sur : un litteral, une variable IHM, le
//  retour d'une fonction standard. Une variable de l'automate a un type
//  inconnu ici : elle ne declenche jamais d'erreur de type.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::apivars { class Model; }   // 1.11.1 (API-M) : HmiApiVars.hpp

namespace hmi::exprcheck {

// Ce que la propriete attend de son expression.
enum class Want : std::uint8_t { Any, Bool, Number, Text, Color };

// Les chemins de l'AUTOMATE (tranche 2) : un membre qui n'existe pas, un indice
// constant hors des bornes d'un tableau. Fournis par qui a le projet automate ;
// vides : pas verifies.
struct PlcPaths {
    std::function<std::string(std::string_view root)> rootType{};                             // "" : pas une variable de l'automate
    std::function<std::string(std::string_view type, std::string_view member)> memberType{};  // "" : pas de tel membre
    std::function<bool(std::string_view type)> isStruct{};                                    // une structure (DDT) : ses membres se verifient
    // 1.11.1 (API-M) : les variables de l'automate sous API. (HmiApiVars.hpp) ;
    // nul : un chemin API.... n'est pas verifie (rien n'est dit).
    std::shared_ptr<const apivars::Model> api{};
};

struct Context {
    const Project* project{nullptr};
    const View*    view{nullptr};
    // Ce nom (la racine d'un chemin) est-il connu ? Fourni par l'appelant (les
    // variables IHM et systeme, les vues, les parametres, l'automate). Vide :
    // tout nom passe (l'automate n'est pas connu).
    std::function<bool(std::string_view root)> known{};
    // Des noms en plus pour proposer le plus proche (ceux de l'automate).
    std::vector<std::string> candidates{};
    PlcPaths plc{};
    // 1.11.20 : juger ici les appels des fonctions de l'utilisateur (le controle des appels,
    // hmi::callcheck). Faux : l'appelant les juge lui-meme (le controle d'un script, qui
    // verifie aussi les types de ses expressions) - un seul message par faute.
    bool calls{true};
};

struct Problem {
    std::string message;        // en francais, tutoie, dit quoi faire
    std::string unknownName;    // un nom inconnu : lequel (vide : un autre probleme)
    std::string suggestion;     // ... et le nom connu le plus proche (vide : aucun)
    // 1.11.21 : un avertissement - l'expression marche (un appel dont la surcharge depend d'un
    // type inconnu, un argument que la simulation convertit sans rien dire). Compiler le dit en
    // avertissement ; l'inspecteur ne la marque pas fautive.
    bool        warning{false};
};

// Les problemes d'une expression qui se lit (sinon : rien, la syntaxe est
// signalee par Expression::compile).
[[nodiscard]] std::vector<Problem> check(const Context&, std::string_view source, Want want = Want::Any);
// La cible d'une ecriture : une variable qu'on peut ecrire.
[[nodiscard]] std::vector<Problem> checkTarget(const Context&, std::string_view target);
// Les expressions d'un texte a trous ("Vitesse : {Vitesse:0.0} km/h" -> Vitesse).
[[nodiscard]] std::vector<std::string> templateExpressions(std::string_view text);
// Ce que la propriete `key` attend (visible : Bool, fill : Color, x : Number).
[[nodiscard]] Want wantOf(std::string_view key);
// Le nom connu le plus proche de `name` (variables IHM, fonctions, parametres,
// candidats) ; vide : aucun d'assez proche.
[[nodiscard]] std::string closest(const Context&, std::string_view name);

} // namespace hmi::exprcheck
