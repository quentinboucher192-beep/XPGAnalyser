// =============================================================================
//  hmi/HmiScriptCheck.hpp - 1.10 : les erreurs d'un script ST, a leur place
// -----------------------------------------------------------------------------
//  checkScript (HmiScript) dit si un script SE LIT (la syntaxe). Ce module dit
//  s'il peut MARCHER, sans l'executer, et OU est chaque faute (ligne, colonne,
//  longueur) - pour Compiler et Generer (un clic y mene) et pour l'editeur
//  (le soulignement pendant la frappe) :
//   - un nom inconnu : ni variable IHM, ni variable de l'automate, ni SYS, ni
//     Vue.Objet.Propriete, ni parametre de la vue (popup, symbole), ni variable
//     locale (VAR ... END_VAR), ni compteur de boucle - avec le nom connu le
//     plus proche (Temp_Moteur n'existe pas : veux-tu dire Temp_Moteur1 ?) ;
//   - un membre inconnu (Moteur.Vitesse quand le type n'a pas Vitesse), un
//     indice sur ce qui n'est pas un tableau ;
//   - une fonction inconnue, le mauvais nombre d'arguments (fonctions standard,
//     IHM_..., fonctions IHM du projet), une procedure appelee dans un calcul ;
//   - une ecriture interdite : SYS (sauf les ecrivables), une propriete en
//     lecture seule, une variable IHM en lecture seule ;
//   - les types incompatibles evidents : un texte affecte a un nombre ou a un
//     booleen, un nombre a un texte, un texte comme condition d'un IF.
//  Une lecture tolerante : elle ne s'arrete jamais sur ce qu'elle ne comprend
//  pas (la syntaxe est l'affaire de sim::parse).
// =============================================================================
#pragma once

#include "HmiCheck.hpp"
#include "HmiDecl.hpp"        // 1.11.18 (refonte, lot 3) : decl::Role
#include "HmiExprCheck.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::scriptcheck {

struct Finding {
    enum class Severity : std::uint8_t { Warning, Error } severity{Severity::Error};
    int         line{0};       // 1 = la premiere
    int         column{0};     // 1 = le premier octet de la ligne
    int         length{0};     // en octets, sur la ligne
    std::string message;       // en francais, tutoie, dit quoi faire
    std::string name;          // un nom inconnu (vide : un autre probleme)
    std::string suggestion;    // ... et le nom connu le plus proche
    // 1.10 (decision 15 ; integration I2) : la correction proposee par S1
    // (lang110::Finding) - "Ajouter les valeurs manquantes" d'un CASE sur une
    // enumeration : inserer `fixText` au debut de la ligne `fixLine` (1 = la
    // premiere ligne du code entier ; 0 : pas de correction).
    std::string fixLabel{};
    int         fixLine{0};
    std::string fixText{};
};

// Ce que le script voit.
struct Scope {
    const Project*     project{nullptr};
    const View*        view{nullptr};       // un script de vue : ses parametres (popup, symbole)
    const HmiFunction* function{nullptr};   // le corps d'une fonction IHM : VAR_INPUT, son nom
    // Les noms de l'automate (la racine d'un chemin) ; vide : l'automate n'est
    // pas connu, un nom inconnu de l'IHM peut etre a lui (rien n'est dit).
    std::function<bool(std::string_view root)> plcKnown{};
    std::vector<std::string> candidates{};  // des noms en plus pour "veux-tu dire"
    exprcheck::PlcPaths      plc{};         // les membres des chemins de l'automate
};

// Les fautes d'un code ST, dans l'ordre du texte (avec celles de dialectFindings).
[[nodiscard]] std::vector<Finding> check(const Scope&, std::string_view code);
// 1.11.18 (refonte, lot 3) : un corps et ses declarations du modele, reconstruites sur sa
// ligne 1 (decl::composeCode) : les fautes rendues au corps (les colonnes de sa ligne 1) ;
// celles qui tombent dans les declarations (une valeur initiale qui cite un nom inconnu...)
// nomment la declaration, ligne 0. `inherited` : les parametres d'une fonction redefinie.
[[nodiscard]] std::vector<Finding> check(const Scope&, std::string_view body, const std::vector<Declaration>& decls,
                                         decl::Role role, const std::vector<Declaration>* inherited = nullptr);

// 1.10 (decisions 13 et 13 bis) : LE POINT D'APPEL DU CHANTIER S1. Les
// constructions que S1 ajoute aux scripts de l'IHM (fonctions internes,
// REF_TO / POINTER TO, ^, ADR, REF, NULL, tableaux a N dimensions, MAP,
// FOR EACH, MAP_...) sont controlees par S1 dans SES fichiers
// (src/hmi/HmiScriptCheck110.hpp/.cpp : hmi::lang110::analyze). Des que
// l'en-tete de S1 est la (__has_include), check() l'appelle pour chaque
// script : ses fonctions internes et ses noms (FOR EACH...) sont connus sur
// leurs lignes, ses constats s'ajoutent aux miens (une fois chacun). Sans
// lui, check() reconnait seul ces constructions : une fonction interne (avant
// ou apres son emploi), ses parametres et ses variables (dans la fonction),
// les variables d'un FOR EACH (dans la boucle), un nom declare d'un type du
// dialecte, les fonctions du dialecte ne sont jamais des noms inconnus ;
// leurs types, leurs arguments : S1. Voir avancement/S-interface.md.
// dialectFindings : les constats de S1 seuls (vide sans S1).
[[nodiscard]] std::vector<Finding> dialectFindings(const Scope&, std::string_view code);
// 1.10 : les noms que le dialecte IHM declare ou emploie dans ce code, en
// majuscules (fonctions internes, leurs parametres et variables, variables de
// FOR EACH, declarations d'un type riche, mots et fonctions du dialecte) :
// Generer (scriptNames) ne les dit pas "variable inexistante".
[[nodiscard]] std::set<std::string> dialectDeclared(std::string_view code);
// 1.10 (decision 15 ; integration I2) : appliquer la correction d'un constat a
// son code - `fixText` insere au debut de la ligne `fixLine` (a la fin si le code
// a moins de lignes ; un code en \r\n le reste). Faux, code inchange : pas de
// correction.
[[nodiscard]] bool applyFix(const Finding&, std::string& code);

// Tous les scripts du projet : generaux, de vue (popups, symboles : avec leurs
// parametres), les actions Executer un script, les fonctions IHM. Chaque
// constat a sa ligne et sa colonne (Issue::column, Issue::length).
[[nodiscard]] std::vector<Issue> projectIssues(const Project&, const NameExists& plcHasName,
                                               const exprcheck::PlcPaths& plcPaths = {},
                                               const CompileFocus* focus = nullptr);   // 1.11.13 : une partie (nul : tout)

} // namespace hmi::scriptcheck
