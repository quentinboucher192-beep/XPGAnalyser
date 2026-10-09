// =============================================================================
//  hmi/HmiOverload.hpp - 1.11.20 : LES SIGNATURES ET LES SURCHARGES
// -----------------------------------------------------------------------------
//  Une fonction (IHM du projet, d'un symbole, interne a un script) a une
//  signature : ses parametres dans l'ordre - VAR_INPUT (par valeur), VAR_IN_OUT
//  (par reference : une variable), VAR_OUTPUT (une sortie : une variable, ou
//  rien) - et son type de retour. Plusieurs fonctions peuvent porter le meme nom
//  si leurs parametres different (la spec, § 10) : un appel choisit la sienne.
//
//  UNE SEULE REGLE, ici, pour le controle (l'editeur, Compiler) et pour le moteur
//  (la simulation) : ils choisissent toujours la meme surcharge.
//    1. L'ARITE : les arguments par position remplissent les parametres dans leur
//       ordre, ceux par nom (a := x, q => y) leur parametre. Une entree sans valeur
//       par defaut et une E/S sont obligatoires, une sortie non. Trop d'arguments,
//       un nom inconnu, un parametre donne deux fois : non.
//    2. LA REFERENCE : une E/S, une sortie et un REF_TO attendent une variable (un
//       nom, un membre, une case, p^), pas une valeur calculee.
//    3. S'IL RESTE PLUSIEURS SURCHARGES, LES TYPES : chaque argument coute sa
//       conversion (hmi::typereg::conversion) - exacte 0, elargie 1 ou 2, avec
//       perte 4, vers un entier plus petit 8, d'un reel vers un entier 16 (le moteur
//       arrondit a l'entier le plus proche, comme il l'a toujours fait : 2.5 -> 3) ;
//       interdite (un texte pour un nombre,
//       deux structures) : non. Un litteral entier (5) coute 0 vers son type
//       naturel (INT s'il y tient, sinon DINT, LINT), 1 vers un autre entier ou il
//       tient, 2 vers un reel. Par reference : le meme type 0, un nombre pour un
//       nombre 4 (comme le moteur l'accepte). La moins chere gagne ; a egalite,
//       l'appel est AMBIGU - sauf si un type inconnu en est la cause : alors
//       `uncertain`, et le moteur tranche avec les types qu'il voit.
//  Une seule surcharge qui remplit l'arite est choisie sans regarder les types :
//  une fonction non surchargee se choisit comme avant (ajouter une surcharge ne
//  rend pas fautif un appel qui ne la concerne pas). 1.11.21 : ses arguments sont
//  ensuite controles (typeMisfits) - une E/S d'un autre type est une faute (le
//  moteur la refuse), une autre conversion interdite un avertissement (le moteur
//  la fait sans rien dire : 'a' pour un REAL donne 0).
//
//  DEUX SURCHARGES NE DOIVENT PAS AVOIR LA MEME FORME : meme nombre de
//  parametres, memes modes, des types que le moteur calcule de la meme facon
//  (REAL et LREAL, INT et SINT, DINT et LINT, UINT et USINT, UDINT et ULINT). Le
//  type de retour seul ne les distingue pas.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {
struct HmiFunction;
}
namespace sim {
class Function;
struct ArgShape;
} // namespace sim

namespace hmi::overload {

enum class Mode : std::uint8_t { In, InOut, Out };

struct Param {
    std::string name, type;
    Mode        mode{Mode::In};
    bool        optional{false};    // une entree qui a une valeur par defaut ; une sortie
};

struct Signature {
    std::string        name;        // tel qu'ecrit
    std::vector<Param> params;
    std::string        result{};    // vide : sans retour
    std::string        key{};       // qui la designe pour l'appelant ("#615" : son identifiant) ; vide : aucune
    // "Random(Min : REAL; Max : REAL; VAR_IN_OUT Graine : REAL; VAR_OUTPUT Tirage : REAL) : REAL"
    [[nodiscard]] std::string text() const;
    // La forme, pour les messages et les doublons : "Random(REAL, REAL, VAR_IN_OUT REAL, VAR_OUTPUT REAL)".
    [[nodiscard]] std::string shape() const;
};

// Un argument d'un appel, tel qu'on le voit sans l'evaluer.
struct Arg {
    std::string name{};             // nomme (a := ..., q => ...) ; vide : par position
    bool        output{false};      // "=>"
    std::string type{};             // son type ; vide : inconnu
    bool        designator{false};  // une variable : un nom, un membre, une case, p^
    bool        literal{false};     // un litteral entier : sa valeur est `value`
    long long   value{0};
    std::string text{};             // tel qu'ecrit (les messages) ; facultatif
};

struct Choice {
    int              chosen{-1};          // l'indice de la surcharge choisie ; -1 : aucune
    std::vector<int> tied{};              // ambigu (ou incertain) : celles a egalite
    bool             uncertain{false};    // l'egalite vient d'un type inconnu : le moteur tranchera
    std::string      why{};               // aucune, ambigu, incertain : pourquoi (en francais, les formes dites)
};

// L'appel `args` parmi `candidates` (meme nom).
[[nodiscard]] Choice choose(const std::vector<Signature>& candidates, const std::vector<Arg>& args);
// Une signature seule : pourquoi l'appel ne la remplit pas (arite, reference) ; vide : il la remplit.
// Les types ne sont pas exiges (une fonction non surchargee se controle comme avant).
[[nodiscard]] std::string misfit(const Signature&, const std::vector<Arg>&);
// 1.11.21 : LES TYPES DES ARGUMENTS d'un appel a la surcharge `sig` (celle que choose() a
// prise) : chaque argument dont la conversion est interdite (cost() < 0). `error` : le moteur
// refuserait l'appel (une E/S d'un autre type) ; sinon un avertissement (il convertit sans
// rien dire). Un type inconnu, ANY : rien. Vide : rien a dire (ou l'arite n'est pas remplie).
struct Misfit {
    std::string message;    // "texte attend un STRING, pas un REAL"
    bool        error{false};
};
[[nodiscard]] std::vector<Misfit> typeMisfits(const Signature& sig, const std::vector<Arg>& args);
// Deux signatures qu'un appel ne saurait distinguer (la meme forme) ; le nom n'est pas compare.
[[nodiscard]] bool sameShape(const Signature& a, const Signature& b);
// Le cout d'un argument pour un parametre (-1 : non) - la regle, pour les essais.
[[nodiscard]] int cost(const Arg&, const Param&);
// Le type naturel d'un litteral entier : INT, DINT ou LINT.
[[nodiscard]] std::string literalType(long long value);
// Un parametre passe par reference (E/S, sortie, REF_TO) ?
[[nodiscard]] bool byReference(const Param&);
// "VAR_IN_OUT " / "VAR_OUTPUT " / "" : le mot d'un mode, tel qu'une signature l'ecrit.
[[nodiscard]] std::string_view modeWord(Mode) noexcept;

// La signature d'une fonction du projet ou d'un symbole : ses parametres (du modele ou de ses
// blocs), dans l'ordre ; `key` = "#" + son identifiant.
[[nodiscard]] Signature signatureOf(const HmiFunction&);

// LES SURCHARGES MAL DISTINGUEES d'une liste de fonctions (celles du projet, celles d'un
// symbole) : deux fonctions du meme nom et de la meme forme (chacune est dite), une
// fonction virtuelle qui a des surcharges. `function` : l'identifiant de la fonction en faute.
struct Clash {
    std::uint32_t function{0};
    std::string   message;
};
[[nodiscard]] std::vector<Clash> clashes(const std::vector<HmiFunction>& functions);
// Celle d'une fonction interne d'un script (lue par le simulateur) : une entree non donnee y
// prend sa valeur initiale (le dialecte) - seules les E/S sont dues.
[[nodiscard]] Signature signatureOf(const sim::Function&);
// Les arguments d'un appel vus par le simulateur (sim::ArgShape), pour choose().
[[nodiscard]] std::vector<Arg> argsOf(const std::vector<sim::ArgShape>&);

} // namespace hmi::overload
