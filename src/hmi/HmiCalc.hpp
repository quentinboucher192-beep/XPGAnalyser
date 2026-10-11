// =============================================================================
//  hmi/HmiCalc.hpp - 1.12.7 : LES VALEURS CALCULEES AU DEMARRAGE
// -----------------------------------------------------------------------------
//  Une constante d'un script - ou la valeur initiale d'une de ses variables - peut
//  venir d'un code ST, comme un final de Java : le code ecrit la declaration sous
//  son nom, une fois, au demarrage, avant les scripts de Demarrage et le premier
//  cycle. Il lit les constantes de son script, les constantes publiques des autres
//  scripts generaux (toujours par leur nom qualifie : Reglages.Seuils), les valeurs
//  de depart des variables IHM et les natives ; ses locales sont a part (l'onglet
//  Locales : nom, type, valeur initiale, documentation) - jamais de VAR ... END_VAR
//  dans le code. Ici, sans le moteur :
//    - les declarations avec lesquelles le calcul tourne (declarationsFor) et son
//      code complet (runnableCode) ;
//    - ses fautes propres (analyse) : un bloc VAR dans le code, une constante ecrite,
//      une variable du script lue par une constante, une constante d'un autre script
//      inconnue, privee ou ecrite, un parametre de vue, une valeur jamais ecrite ;
//    - l'ordre des calculs (plan) : les constantes d'abord, chacune apres celles
//      qu'elle lit, puis les variables ; une boucle est dite (et refusee).
//  Le moteur (Runtime::computeDeclarations) suit ce plan ; une erreur de calcul
//  arrete le demarrage.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::calc {

struct Issue {
    bool        error{true};      // faux : un avertissement
    int         line{0};          // dans le code du calcul (1 = sa premiere ligne) ; 0 : la declaration
    int         column{0};
    std::string message;
};

struct Analysis {
    std::vector<Issue>       issues;
    std::vector<std::string> reads;            // les valeurs calculees qu'il lit : "Nom" (son script), "Script.Nom"
    std::vector<std::string> uses;             // tout ce qu'il lit (calcule ou ecrit) : ses constantes, Reglages.X, les variables IHM
    bool                     assigns{false};   // il ecrit sa declaration (:=, ou en premier argument d'un appel)
    [[nodiscard]] bool hasError() const noexcept;
};

// Les declarations avec lesquelles tourne le calcul de `d` (une declaration de `owner`) : elle-meme
// (une variable, qu'il ecrit), ses locales, les variables des boucles FOR qu'il ne declare pas
// (DINT), les constantes de son script - et ses variables, pour le calcul d'une variable.
[[nodiscard]] std::vector<Declaration> declarationsFor(const Script& owner, const Declaration& d);
// Son code complet : ces declarations reconstruites sur sa ligne 1 (les lignes du calcul ne bougent pas).
[[nodiscard]] std::string runnableCode(const Script& owner, const Declaration& d);

// Les fautes propres a un calcul (les noms inexistants et la syntaxe : les controles du projet et
// l'editeur les disent comme pour le reste du code, avec runnableCode). `view` : la vue du script
// (nulle : un script general).
[[nodiscard]] Analysis analyse(const Project&, const Script& owner, const Declaration& d, const View* view = nullptr);

// La constante `Script.Nom` d'un script general, lue d'ailleurs : sa declaration ; nulle : `why` dit
// pourquoi (script inconnu, constante inconnue, une variable, privee). `from` : le script qui lit (le
// sien lit aussi ses constantes privees). Un chemin plus long (Reglages.Recette.Temp) : sa racine.
[[nodiscard]] const Declaration* qualified(const Project&, std::string_view path, const Script** script = nullptr,
                                           std::string* why = nullptr, Id from = kNoId);

struct Step {
    Id          script{kNoId};
    Id          view{kNoId};               // kNoId : un script general
    Id          declaration{kNoId};
    std::string name;                      // le nom de la declaration
    std::string path;                      // "Reglages.Seuils", "Vue_A.Ouverture.Max"
};
struct Plan {
    std::vector<Step> order;               // les constantes d'abord, chacune apres celles qu'elle lit
    std::string       cycle;               // "Reglages.A -> Reglages.B -> Reglages.A" ; vide : aucune boucle
};
[[nodiscard]] Plan plan(const Project&);
// Le projet a-t-il une valeur calculee ?
[[nodiscard]] bool hasComputed(const Project&) noexcept;
// Le nom affiche d'une declaration calculee : "Reglages.Seuils" (general), "Vue_A.Ouverture.Max".
[[nodiscard]] std::string pathOf(const Script&, const Declaration&, const View* view = nullptr);

// Qui lit la declaration `d` de `owner` (la fenetre du calcul, « Lu par ») : les calculs (le sien
// excepte), le code de son script et, pour une constante publique d'un script general, les scripts et
// les fonctions qui la lisent par son nom qualifie. Des libelles : « calcul de Reglages.Total »,
// « le code de Reglages », « le script Vue_Calcul.Init », « la fonction Calcul_Debit ».
[[nodiscard]] std::vector<std::string> readers(const Project&, const Script& owner, const Declaration& d, const View* view = nullptr);

// Le nom le plus proche parmi `names` (sans casse, a deux fautes de frappe au plus) ; "" : aucun.
[[nodiscard]] std::string closest(std::string_view name, const std::vector<std::string>& names);

} // namespace hmi::calc
