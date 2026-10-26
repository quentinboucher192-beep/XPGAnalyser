// =============================================================================
//  hmi/HmiFunctionTests.hpp - 1.13.0 (lot J4) : les tests unitaires des
//                             fonctions IHM - des cas rejoues sur un banc, un
//                             verdict par cas
// -----------------------------------------------------------------------------
//  UN CAS DE TEST (HmiModel.hpp : FunctionTest) est epingle a UNE fonction
//  precise (par son id - chaque surcharge garde ses propres cas). Il porte une
//  liste d'arguments (un par parametre), un resultat attendu, une tolerance
//  (sur un retour REAL/LREAL), des poses (valeurs mises avant l'appel) et des
//  verifs (post-conditions, dont le JOURNAL). Rejoue sur un Runtime neuf (comme
//  runScenario) : bind au projet, prime() remet les variables a l'initiale, les
//  poses ecrivent leurs variables, puis runFunction(id, arguments) comme l'Essai
//  existant. Rien du projet ni de la simulation n'est touche.
//
//  C'EST AUTRE CHOSE que les essais de reception (HmiScenarios.hpp), qui
//  rejouent l'OPERATEUR sur l'IHM en marche (ouvrir une vue, cliquer, signer).
//  Ici on teste une FONCTION seule, sans vue, sans operateur. Le verdict reutilise
//  hmi::Verdict / verdictLabel, le rapport reutilise ExportTable.
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiModel.hpp"
#include "HmiScenarios.hpp"   // hmi::Verdict, verdictLabel (le meme vocabulaire que les essais)

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim { class Environment; }

namespace hmi {

// Le resultat d'un cas : son verdict, le detail (attendu vs obtenu), l'appel joue
// et la valeur obtenue (que l'onglet Tests montre sous "derniere execution").
struct FunctionTestResult {
    Id          test{kNoId};
    std::string name;
    Verdict     verdict{Verdict::Pending};
    std::string detail;        // "attendu 18.0 - obtenu 18.0 (ecart 0.0)", "E_Niveau : niveau hors plage: 130"
    std::string call;          // "Moyenne_Pression(12.0, 20.0, 0.25)"
    std::string got;           // la valeur obtenue (vide : procedure ou erreur)
    double      seconds{0};
};

// Le rapport d'une fonction : un resultat par cas, et le bilan.
struct FunctionTestReport {
    Id          function{kNoId};
    std::string functionName;
    std::vector<FunctionTestResult> results;
    [[nodiscard]] std::size_t count(Verdict) const;
    [[nodiscard]] bool        ok() const;       // au moins un cas, aucun echec ni erreur
    [[nodiscard]] bool        failed() const;   // au moins un echec ou une erreur (la pastille rouge)
    // "7 cas : 6 reussis, 1 echec" ; "aucun cas" (accentue).
    [[nodiscard]] std::string summary() const;
};

// Joue tous les cas ACTIFS d'une fonction sur un banc neuf (relie a `plc` s'il y en
// a un, en lecture seule). Un cas inactif recoit le verdict "non joue".
[[nodiscard]] FunctionTestReport runFunctionTests(const Project&, const HmiFunction&, sim::Environment* plc = nullptr);
// Un seul cas, sur son propre banc.
[[nodiscard]] FunctionTestResult runFunctionTest(const Project&, const HmiFunction&, const FunctionTest&, sim::Environment* plc = nullptr);

// Le rapport d'une fonction en tableau : Cas, Arguments, Attendu, Verdict, Detail,
// Duree ; le titre et le sous-titre disent la fonction et le bilan.
[[nodiscard]] ExportTable functionTestTable(const HmiFunction&, const FunctionTestReport&);
// Toute la campagne : Fonction, Cas, Verdict, Detail, avec le bilan en sous-titre.
[[nodiscard]] ExportTable campaignTestTable(const std::vector<std::pair<const HmiFunction*, const FunctionTestReport*>>&);

// Les controles de Generer, SANS jouer : mauvais nombre d'arguments, argument /
// attendu / pose / verif illisible, pose ou verif sur une variable inconnue,
// tolerance non numerique, cas sans nom, deux cas du meme nom, attendu sur une
// procedure. Chaque Issue designe la fonction et le cas fautif.
struct Issue;
void checkFunctionTests(const Project&, std::vector<Issue>& out);

// 1.13.0 (seconde passe) : LE TYPE DANS LEQUEL UN CAS LIT UNE CASE (un argument, l'attendu) - le type declare ;
// pour un generique de la fonction (T), sa contrainte (ANY_NUM ; ANY sans contrainte) : 2,5 -> 2.5, VRAI -> TRUE,
// Azote -> 'Azote' s'y lisent comme dans toute case (la regle des cases), au lieu d'un refus "par une variable".
[[nodiscard]] std::string testCellType(const HmiFunction&, std::string_view declared);
// 1.13.0 (seconde passe) : la tolerance sert-elle au retour de cette fonction - REAL, LREAL, ou un generique qui peut
// etre un reel (T : ANY_REAL, ANY_NUM, ANY...). Le banc la joue aussi des que la valeur obtenue est un REAL.
[[nodiscard]] bool testToleranceApplies(const HmiFunction&);
// 1.13.0 (seconde passe) : REMET LES ARGUMENTS DES CAS sur les parametres de la fonction apres un changement de sa
// signature (un parametre retire, deplace, ajoute) : `before` et `after` sont les noms des parametres, dans l'ordre,
// avant et apres. Chaque argument suit son parametre par son nom (celui d'un parametre retire disparait, un parametre
// ajoute part vide) ; ce qui depassait deja la signature reste au bout ; les vides de fin sont otes. Vrai : un cas a
// change. Les noms d'un parametre renomme ne bougent pas de place : rien a remettre.
bool remapTestArguments(HmiFunction&, const std::vector<std::string>& before, const std::vector<std::string>& after);
// Les noms des parametres d'une fonction, dans l'ordre (ceux du modele et d'un ancien bloc VAR_INPUT du code).
[[nodiscard]] std::vector<std::string> testParameterNames(const HmiFunction&);

} // namespace hmi
