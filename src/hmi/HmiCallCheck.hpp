// =============================================================================
//  hmi/HmiCallCheck.hpp - 1.11.20 : LES APPELS DES FONCTIONS DE L'UTILISATEUR,
//  CONTROLES COMME LE MOTEUR LES FAIT
// -----------------------------------------------------------------------------
//  Le controle d'un script (HmiScriptCheck) lit le code par ses mots ; il ne voit
//  ni le type d'un argument ni ce qu'est un argument (une variable, un calcul).
//  Ici, le code est lu par le simulateur (le dialecte IHM, sim::parse) et chaque
//  appel d'une fonction de l'utilisateur - du projet, d'un symbole (Ouvrir,
//  Vanne_3.Ouvrir, Vue.Vanne_3.Ouvrir, SUPER.Ouvrir), interne au script - est
//  juge par la regle des surcharges (hmi::overload), celle que le moteur
//  applique a l'execution :
//    - l'arite : E/S (VAR_IN_OUT) et sorties (VAR_OUTPUT) comptent comme des
//      parametres ; une entree sans valeur par defaut et une E/S sont dues ;
//    - la reference : une E/S, une sortie, un REF_TO veulent une variable ;
//    - les surcharges : aucune ne convient (leurs formes dites), l'appel est
//      ambigu, ou le choix depend d'un type inconnu ici (un avertissement).
//  Les types des noms : les declarations du code (VAR, parametres d'une fonction
//  interne), les parametres et locales de la fonction controlee, les variables
//  IHM (et leurs membres), les parametres de la vue, les variables de l'automate.
//  Un code qui ne se lit pas : rien n'est dit ici (`ok` faux) - la syntaxe l'est
//  ailleurs, et le controle par mots compte alors les arguments seul.
// =============================================================================
#pragma once

#include "HmiExprCheck.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {
struct Project;
struct View;
struct HmiFunction;
} // namespace hmi

namespace hmi::callcheck {

struct Context {
    const Project*      project{nullptr};
    const View*         view{nullptr};       // une vue, une popup, un symbole : ses parametres, ses instances, ses fonctions
    const HmiFunction*  function{nullptr};   // le corps d'une fonction (du projet ou d'un symbole) : ses parametres
    exprcheck::PlcPaths plc{};               // les variables de l'automate (vide : inconnues)
};

struct Problem {
    int         line{0};                     // 1 = la premiere du code
    int         column{0};                   // 1 = le premier octet de la ligne (0 : inconnue)
    int         length{0};
    bool        error{true};                 // faux : un avertissement (le choix depend d'un type inconnu)
    std::string message;                     // en francais
};

// Les appels d'un code ST (un script, le corps d'une fonction : `ctx.function`).
[[nodiscard]] std::vector<Problem> checkCode(const Context&, std::string_view code, bool* ok = nullptr);
// Les appels d'une expression (une propriete de vue, un texte a trous : son contenu).
[[nodiscard]] std::vector<Problem> checkExpression(const Context&, std::string_view expression, bool* ok = nullptr);

// Les fonctions de l'utilisateur qu'un nom d'appel designe vu d'ici (toutes ses surcharges) :
// celles du symbole d'abord (dans un symbole, ses popups), puis celles du projet ; un chemin
// d'instance (Vanne_3.Ouvrir, Vue.Vanne_3.Ouvrir, SUPER.Ouvrir) : celles de son symbole.
[[nodiscard]] std::vector<const HmiFunction*> candidates(const Context&, std::string_view callee);

} // namespace hmi::callcheck
