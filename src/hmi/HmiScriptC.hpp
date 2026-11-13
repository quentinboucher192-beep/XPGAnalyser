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
//    - 1.50.1 : struct (imbriquees, avec des tableaux ; . et ->), enum et enum
//      class, typedef et using, const et constexpr, sizeof (d'un type, d'une
//      expression : la taille du C, des octets alignes) ; les listes { } et les
//      initialiseurs designes ({ .x = 1 }, { [4] = 29 }) ; #define N 10 et les
//      macros a parametres (#define CARRE(x) ((x) * (x))), #ifdef / #if 0 ;
//    - if / else, switch / case (1.50.1 : un case qui continue dans le suivant,
//      default n'importe ou, break au milieu), while, do ... while, for (et, en
//      C++, for (T x : tableau)), break, continue, return ; les blocs { } et leurs
//      variables locales ; static : une variable gardee d'une execution a l'autre ;
//    - les operateurs du C (+ - * / % == != < > <= >= && || ! ~ & | ^ << >>
//      ?: et les affectations = += -= ... ++ --, 1.50.1 : aussi au milieu d'une
//      expression, t[i++] = x) ; les conversions (int)x, static_cast<int>(x) ;
//    - les pointeurs (POINTER TO du ST) : T* p = &x, *p, p->m, NULL ; 1.50.1 :
//      L'ARITHMETIQUE DANS UN TABLEAU (p + i, p++, p[i], *(p + i), p - q, les
//      comparaisons ; une case hors du tableau est une erreur a sa ligne) ;
//    - les fonctions du script (parametres par valeur, par pointeur, par reference
//      T& en C++) ; les fonctions et les variables IHM par leur nom ; sqrt, fabs,
//      pow, sin, cos, floor, ceil, round, min, max, strlen, printf (au journal)...
//    - 1.50.1, C++ : les classes et les struct a methodes (membres, methodes,
//      constructeurs, this, public / private), l'heritage simple et les methodes
//      virtuelles ; new et delete (un objet, un tableau new T[n] : un tas gere -
//      delete deux fois, un objet employe apres delete : une erreur claire, a sa
//      ligne ; en C, malloc / calloc / free) ; les lambdas (sans capture, ou [=]) ;
//      std::array, std::vector (push_back, size, [i]...), std::string (length,
//      substr, find...), std::function ; std::cout (au journal).
//  Ce qui n'est pas pris (dit a sa ligne, en francais) : goto, les templates, les
//  exceptions (try / catch / throw), l'heritage multiple, la surcharge des
//  operateurs, une lambda qui capture par reference ([&]), union, l'arithmetique
//  d'un pointeur qui ne vise pas un tableau, realloc.
//
//  LE POINT D'ENTREE : les instructions au niveau du script s'executent dans
//  l'ordre ; s'il n'y en a pas, la fonction main() (ou celle qui porte le nom
//  du script, ou de son evenement : OnCycle()...) est appelee.
//
//  1.50.1 : LE DEBOGUEUR. La traduction garde ce qu'il lui faut (DebugInfo) : le nom
//  C de chaque fonction ST (Pompe__demarrer -> Pompe::demarrer), de chaque variable
//  (un nom pris en ST se renomme : end -> end_2) et son type C, les membres caches
//  (une classe a methodes virtuelles porte son type reel), les valeurs des enum.
//  Les lignes sont celles du texte C : un point d'arret, le pas a pas et la ligne
//  en cours n'ont rien a traduire. Un espion, la condition d'un point s'ecrivent en
//  C : expressionAt les traduit dans la portee de la ligne.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiScript.hpp"
#include "../sim/Interpreter.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::cscript {

// 1.50.1 : ce que le debogueur doit savoir pour montrer le C (les noms, les types).
struct DebugVar {
    std::string st;            // le nom ST declare (tel qu'ecrit)
    std::string c;             // le nom C
    std::string type;          // le type C ecrit ("int", "struct Point", "int*", "Pompe")
    std::string record;        // le type C d'une structure (ou de la cible d'un pointeur) : ses membres a montrer
    std::string enumName;      // une variable d'un enum : ses valeurs par leur nom
    bool        hidden{false}; // une variable de la traduction (un temporaire) : jamais montree
};
struct DebugRecord {
    std::string cname;                                   // "Point", "Pompe"
    std::string base;                                    // le parent (l'heritage simple) ; vide : aucun
    int         tag{0};                                  // son type reel (__vt) ; 0 : pas de methode virtuelle
    // Les membres ST de la structure (une hierarchie partage la structure de sa racine) : (nom ST, nom C, classe qui le
    // declare, type C ecrit).
    struct Member {
        std::string st, c, owner, type, record, enumName;
    };
    std::vector<Member> members;
};
struct DebugInfo {
    std::map<std::string, std::string>           functions;   // nom ST (majuscules) -> nom C montre ("Pompe::demarrer")
    std::set<std::string>                        hidden;      // les fonctions de la traduction (majuscules) : pas d'arret
    std::map<std::string, std::vector<DebugVar>> vars;        // nom ST de la fonction (majuscules ; "" : le script) -> ses variables
    std::map<std::string, DebugRecord>           records;     // nom ST du type (majuscules) -> la structure C
    std::map<std::string, std::string>           recordOf;    // nom C -> nom ST du type (majuscules)
    std::map<std::string, std::string>           parents;     // classe C -> sa classe parente
    std::map<int, std::string>                   tags;        // le type reel d'un objet (cvt_) -> sa classe C
    std::map<std::string, std::map<std::int64_t, std::string>> enums;   // enum -> valeur -> nom
};

struct Translation {
    std::string                   st;          // le ST du dialecte IHM : autant de lignes que le texte C
    std::vector<ScriptDiagnostic> errors;      // la premiere faute du texte C (vide : traduit)
    std::string                   entry;       // la fonction appelee faute d'instructions au niveau du script ("" : aucune)
    // Les declarations rangees en tete (du script : ligne 1 ; d'une fonction : la ligne de son en-tete) : le nom ST de
    // chacune (majuscules) et la ligne de sa declaration C - une faute du moteur sur l'une d'elles est dite a cette ligne.
    std::vector<std::pair<std::string, int>> declared;
    std::vector<int>                         declarationLines;    // les lignes ou elles sont rangees
    // 1.50.1 : les structures du script (struct, class) : leur nom ST et leurs membres (nom, type ST), pour le moteur
    // (structMembers). Leur nom ST porte une empreinte du script (Point_3FA2C1) : deux scripts ne se melent pas.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> types;
    std::shared_ptr<const DebugInfo> debug;                       // 1.50.1 : le debogueur (jamais nul si traduit)
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Traduit un script C (lang C) ou C++ (lang Cpp). `entryNames` : les noms de fonction pris pour point d'entree apres
// main (le nom du script, son evenement), sans casse.
[[nodiscard]] Translation translate(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames = {});
// La meme, gardee (un script de cycle se traduit une fois, pas a chaque cycle). Ses structures sont connues du moteur
// (structMembers) des qu'elle est faite.
[[nodiscard]] Translation translated(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames = {});
// Les noms d'entree d'un script : son nom et son evenement (OnCycle, Demarrage...).
[[nodiscard]] std::vector<std::string> entryNamesOf(const Script&);

// 1.50.1 : les membres d'une structure d'un script C ou C++ traduit (son nom ST) : (nom, type ST) ; faux : inconnue.
bool structMembers(std::string_view stType, std::vector<std::pair<std::string, std::string>>& out);
// 1.50.1 : le nom est-il celui d'une structure d'un script C ou C++ traduit ?
[[nodiscard]] bool isScriptType(std::string_view stType);

// Une faute du moteur (sur le ST traduit) a sa ligne C : celle de la declaration qu'elle nomme, si elle tombe sur une
// ligne de declarations rangees. 1.50.1 : ses noms ST (Point_3FA2C1, end_2) redeviennent ceux du C.
void placeFault(const Translation&, ScriptDiagnostic&);
// 1.50.1 : un message du moteur (une erreur d'execution) avec les noms du C (les types, les fonctions).
[[nodiscard]] std::string cMessage(const Translation&, std::string_view message);

// ---- 1.50.1 : le debogueur ----
// Une expression C (un espion, la condition d'un point d'arret) traduite en ST dans la portee de la ligne `line` du
// script (0 : le niveau du script, a sa fin) ; vide et `why` : elle ne se traduit pas.
// `condition` : le resultat est un BOOL (if (n) : n <> 0).
[[nodiscard]] std::string expressionAt(std::string_view code, ScriptLang lang, const std::vector<std::string>& entryNames, int line,
                                       std::string_view expression, std::string* why = nullptr, bool condition = false);
// Le nom C d'une fonction ST de la traduction ("" : le script) : "carre", "Pompe::demarrer", "lambda".
[[nodiscard]] std::string shownFunction(const Translation&, std::string_view stFunction);
// Une fonction de la traduction (un aiguillage de methode virtuelle) : le pas a pas la traverse sans s'y arreter.
[[nodiscard]] bool hiddenFunction(const Translation&, std::string_view stFunction);
// Les locales d'un cadre (celles du moteur, en ST) montrees en C : leurs noms, leurs types, les temporaires retires,
// les membres d'une classe selon son type reel, les valeurs d'un enum par leur nom, *p sous un pointeur.
void showLocals(const Translation&, std::string_view stFunction, std::vector<sim::LineProbe::Local>& locals);
// Un nom ecrit dans le ST (une valeur de la ligne : "t[2]", "p^.x", "end_2") ecrit en C ("t[2]", "p->x", "end") ;
// vide : un nom de la traduction, a ne pas montrer.
[[nodiscard]] std::string shownName(const Translation&, std::string_view stFunction, std::string_view stText);

// 1.50.1 : le texte sans ses parties ecartees par le preprocesseur (#if 0, #ifdef d'une macro absente) : leurs lignes en
// blanc (les numeros restent) - la verification de forme ne lit pas ce qui ne compte pas.
[[nodiscard]] std::string withoutInactive(std::string_view code);

// Ce que le moteur prend du C ou du C++, en une phrase (l'ecran : le dialogue Nouveau script, Compiler).
[[nodiscard]] std::string supportedSubset(ScriptLang lang);
// Une phrase courte : "C : compile pour le moteur des scripts (un sous-ensemble du C)".
[[nodiscard]] std::string compiledNote(ScriptLang lang);

} // namespace hmi::cscript
