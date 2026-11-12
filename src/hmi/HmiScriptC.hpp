// =============================================================================
//  hmi/HmiScriptC.hpp - 1.50.0 : LES SCRIPTS C ET C++ COMPILES ET EXECUTES
// -----------------------------------------------------------------------------
//  Un script C ou C++ de l'IHM (un script general, OnOpen, OnCycle, OnClose...)
//  est TRADUIT, ligne pour ligne, dans le ST de l'IHM (le dialecte), puis lu et
//  execute par le meme moteur que les scripts ST : memes variables IHM, memes
//  fonctions, memes erreurs - a la ligne du texte C (chaque ligne C donne sa
//  ligne ST, le meme numero). Rien n'est compile en code machine : pas de
//  compilateur externe, le meme comportement sous Linux et sous Windows.
//
//  LE SOUS-ENSEMBLE PRIS EN CHARGE (supportedSubset le dit a l'ecran) :
//    - les types : int, short, long, long long, unsigned..., char (un entier),
//      float, double, bool, les intN_t / uintN_t, size_t ; un texte (const
//      char*, char s[N], std::string) ; les types du ST (REAL, DINT, BOOL...)
//      et les types IHM (T_Four, une enumeration) ; les tableaux T t[N][M]
//      (un tableau de tableaux ; leurs indices partent de 0) ;
//    - if / else, switch / case (un break a la fin de chaque case), while,
//      do ... while, for (et, en C++, for (T x : tableau)), break, continue,
//      return ; les blocs { } et leurs variables locales ; static : une
//      variable gardee d'une execution a l'autre (au niveau du script) ;
//    - les operateurs du C (+ - * / % == != < > <= >= && || ! ~ & | ^ << >>
//      ?: et les affectations = += -= *= /= %= &= |= ^= <<= >>= ++ --, ces
//      dernieres sur leur propre instruction) ; les conversions (int)x,
//      (float)x, static_cast<int>(x) ;
//    - les fonctions du script (parametres par valeur, par pointeur T* p appele
//      avec &x, par reference T& r en C++) ; les fonctions et les variables IHM
//      par leur nom ; sqrt, fabs, pow, sin, cos, floor, ceil, round, min, max,
//      strlen, printf (au journal)... et leurs std:: en C++ ;
//    - #include est ignore (l'en-tete du projet est deja connu) ; #define N 10
//      (une constante) est pris.
//  Ce qui n'est pas pris (dit a sa ligne) : les pointeurs locaux et leur
//  arithmetique, malloc, les structures, classes, templates, lambdas,
//  exceptions, std::vector / std::cout, goto, sizeof.
//
//  LE POINT D'ENTREE : les instructions au niveau du script s'executent dans
//  l'ordre ; s'il n'y en a pas, la fonction main() (ou celle qui porte le nom
//  du script, ou de son evenement : OnCycle()...) est appelee.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiScript.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::cscript {

struct Translation {
    std::string                   st;          // le ST du dialecte IHM : autant de lignes que le texte C
    std::vector<ScriptDiagnostic> errors;      // la premiere faute du texte C (vide : traduit)
    std::string                   entry;       // la fonction appelee faute d'instructions au niveau du script ("" : aucune)
    // Les declarations rangees en tete (du script : ligne 1 ; d'une fonction : la ligne de son en-tete) : le nom ST de
    // chacune (majuscules) et la ligne de sa declaration C - une faute du moteur sur l'une d'elles est dite a cette ligne.
    std::vector<std::pair<std::string, int>> declared;
    std::vector<int>                         declarationLines;    // les lignes ou elles sont rangees
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Traduit un script C (lang C) ou C++ (lang Cpp). `entryNames` : les noms de fonction pris pour point d'entree apres
// main (le nom du script, son evenement), sans casse.
[[nodiscard]] Translation translate(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames = {});
// La meme, gardee (un script de cycle se traduit une fois, pas a chaque cycle).
[[nodiscard]] Translation translated(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames = {});
// Les noms d'entree d'un script : son nom et son evenement (OnCycle, Demarrage...).
[[nodiscard]] std::vector<std::string> entryNamesOf(const Script&);

// Une faute du moteur (sur le ST traduit) a sa ligne C : celle de la declaration qu'elle nomme, si elle tombe sur une
// ligne de declarations rangees.
void placeFault(const Translation&, ScriptDiagnostic&);

// Ce que le moteur prend du C ou du C++, en une phrase (l'ecran : le dialogue Nouveau script, Compiler).
[[nodiscard]] std::string supportedSubset(ScriptLang lang);
// Une phrase courte : "C : compile pour le moteur des scripts (un sous-ensemble du C)".
[[nodiscard]] std::string compiledNote(ScriptLang lang);

} // namespace hmi::cscript
