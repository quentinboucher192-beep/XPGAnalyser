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
// 1.12.17 : Value - une valeur seule, de n'importe quel type (une variable archivee, la ligne d'un
// chronogramme, la valeur affectee a une variable simple) : ni un tableau, ni une liste, ni une MAP, ni une
// structure entiers. Bool, Number et Color en exigent une aussi ; Text et Any non (un texte montre un
// tableau entier : [1, 2, 3]).
enum class Want : std::uint8_t { Any, Bool, Number, Text, Color, Value };

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
    // 1.12.17 : une expression d'un script (le simulateur ST) : ASCII_TO_STRING et STRING_TO_ASCII y
    // prennent un tableau entier (les cases d'une chaine) ; dans une fx, non.
    bool script{false};
    // 1.13.0 : un nom que le code declare lui-meme (une locale, un parametre, un compteur, le nom de la
    // fonction) : il cache la variable IHM du meme nom - ni son type ni son unite ne valent pour lui (le
    // moteur lit la locale). Vide : aucun.
    std::function<bool(std::string_view root)> hidden{};
    // 1.13.0 (seconde passe) : L'UNITE DECLAREE d'un chemin qui part d'un de ces noms (`path` : Debit, Mesures[0],
    // Ligne.Pression) - la colonne Unite d'une locale, d'un parametre (Declaration::unit), l'unite du retour pour le nom
    // de la fonction ; vide : aucune. Le moteur la lit de meme (EN, UNITE, l'affectation convertie).
    std::function<std::string(std::string_view path)> localUnit{};
};

struct Problem {
    std::string message;        // en francais, tutoie, dit quoi faire
    std::string unknownName;    // un nom inconnu : lequel (vide : un autre probleme)
    std::string suggestion;     // ... et le nom connu le plus proche (vide : aucun)
    // 1.11.21 : un avertissement - l'expression marche (un appel dont la surcharge depend d'un
    // type inconnu, un argument que la simulation convertit sans rien dire). Compiler le dit en
    // avertissement ; l'inspecteur ne la marque pas fautive.
    bool        warning{false};
    // 1.12.17 : un tableau, une liste, une MAP, une structure ENTIERS la ou une valeur seule est
    // attendue - une erreur qui bloque toujours (Generer, Compiler, la simulation), quelle que soit
    // la case (une plume, une action, une alarme comme une fx).
    bool        whole{false};
    // 1.12.20 : la correction d'un clic (hmi::Issue::fixFrom) - 'Valider' -> Valider ; vide : aucune.
    std::string fixFrom{}, fixTo{};
    // 1.13.0 : un verdict d'unite (bar + mbar, EN mal employe, une affectation de deux familles) - une erreur
    // partout, un script compris (sauf `warning` : une info, l'unite perdue ou convertie d'office).
    bool        unit{false};
    // 1.13.0 (seconde passe) : ou est fixFrom dans le texte controle (son premier octet) ; npos : pas dit. La correction
    // d'un clic remplace cette occurrence-la quand le meme nom revient (PressionBasse < 900.0 AND Pression > PressionBasse).
    std::size_t fixAt{static_cast<std::size_t>(-1)};
};

// Les problemes d'une expression qui se lit (sinon : rien, la syntaxe est
// signalee par Expression::compile). `strict` faux (1.12.6) : un type qui ne convient pas a
// `want` est un avertissement, pas une erreur (voir strictType).
[[nodiscard]] std::vector<Problem> check(const Context&, std::string_view source, Want want = Want::Any, bool strict = true);
// La cible d'une ecriture : une variable qu'on peut ecrire. 1.12.17 : `writes` - ce qu'on y ecrit (Bool :
// mettre a 1, basculer ; Number : incrementer ; Value : une valeur seule) ; un tableau, une liste, une
// structure entiers ne le recoivent pas. Any : pas de controle.
[[nodiscard]] std::vector<Problem> checkTarget(const Context&, std::string_view target, Want writes = Want::Any);
// 1.12.17 : CE QUE DONNE UNE EXPRESSION - une valeur seule d'un type connu (`single`), ou un conteneur, une
// structure entiers (`whole`, et leur type) ; rien de connu : ni l'un ni l'autre.
struct Shape {
    bool        single{false};
    std::string type;          // le type ecrit d'un conteneur ou d'une structure entiers
    std::string what, entire, pick;   // types::Whole
    std::string unit;          // 1.13.0 (lot J6) : l'unite physique de la valeur rendue ("bar") ; vide : aucune
    // 1.13.0 : la valeur est lue telle quelle sur un chemin de variable IHM (Pression, Ligne.Vannes[2]) - la seule
    // forme dont le moteur connait l'unite (EN la convertit, l'affectation aussi). Un calcul, un appel : non.
    bool        plain{false};
    std::string unitFrom;      // le chemin, tel qu'ecrit, qui donne son unite a la valeur (PressionBasse dans PressionBasse * 2)
    std::size_t unitFromAt{static_cast<std::size_t>(-1)};   // 1.13.0 (seconde passe) : sa place dans le texte (npos : pas dite)
    [[nodiscard]] bool whole() const noexcept { return !what.empty(); }
};
[[nodiscard]] Shape shapeOf(const Context&, std::string_view source);
// 1.13.0 (lot J6) : LE VERDICT D'UNITE D'UNE AFFECTATION :=. `target` et `source` sont deux expressions (lvalue a
// gauche, valeur a droite). Meme famille, unites differentes : le moteur convertit d'office une source lue telle
// quelle sur un chemin (Pression := PressionBasse) - une info ; un calcul (PressionBasse * 2.0) ne se convertit
// pas : une erreur, convertis dedans avec EN. `strict` (l'option « Unites strictes ») : EN exige dans tous les cas.
// Familles differentes : un refus. Une cible ou une source sans unite : rien a dire (le nu s'adapte). Les problemes
// portent leur correction d'un clic (inserer EN) quand elle existe.
[[nodiscard]] std::vector<Problem> assignmentUnit(const Context&, std::string_view target, std::string_view source, bool strict = false);
// 1.13.0 : les autres passages d'une valeur vers une unite connue (`targetUnit`) - RETURN (ou l'affectation au nom
// de la fonction) vers l'unite du retour, un argument vers celle de son parametre. Le moteur n'y convertit rien :
// meme famille, une erreur (EN, avec sa correction d'un clic) ; familles differentes, un refus. `targetText` nomme
// la cible dans le message (« la fonction Regler », « le parametre Consigne de Regler »).
enum class UnitPass : std::uint8_t { Assign, Return, Argument };
[[nodiscard]] std::vector<Problem> passUnit(const Context&, std::string_view targetUnit, std::string_view source, UnitPass pass,
                                            std::string_view targetText = {}, bool strict = false);
// Les expressions d'un texte a trous ("Vitesse : {Vitesse:0.0} km/h" -> Vitesse).
[[nodiscard]] std::vector<std::string> templateExpressions(std::string_view text);
// Ce que la propriete `key` attend (visible : Bool, fill : Color, x : Number).
[[nodiscard]] Want wantOf(std::string_view key);
// 1.12.6 (lot B) : ce qu'attend la propriete `key` d'un objet de ce genre, lu dans la table des
// roles (HmiRoles.hpp) - la valeur d'une jauge un nombre, celle d'un voyant un booleen, colorOn
// une couleur, le retour d'etat d'un interrupteur un booleen, celui d'un selecteur un etat.
[[nodiscard]] Want wantOf(Kind kind, std::string_view key);
// 1.12.6 : LA GRAVITE D'UN TYPE QUI NE CONVIENT PAS a cette propriete. Ce que Compiler exigeait deja
// en 1.12.5 (x un nombre, visible un booleen, fill une couleur...) reste une erreur, qui bloque le
// demarrage ; ce que la table des roles exige de plus (la Valeur d'une jauge ou d'un voyant, les
// seuils, les couleurs des etats, le retour d'etat...) est un avertissement : un projet qui demarrait
// demarre toujours - l'objet montre ### quand la valeur ne se lit pas, et Generer le dit avant.
[[nodiscard]] bool strictType(Kind kind, std::string_view key);
// Le nom connu le plus proche de `name` (variables IHM, fonctions, parametres,
// candidats) ; vide : aucun d'assez proche.
[[nodiscard]] std::string closest(const Context&, std::string_view name);

} // namespace hmi::exprcheck
