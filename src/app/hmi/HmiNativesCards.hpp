// =============================================================================
//  app/hmi/HmiNativesCards.hpp - 1.12.0 : LES FICHES DES NATIVES
// -----------------------------------------------------------------------------
//  La branche Natives (IHM > Programmation generale) montre tout ce que le
//  langage de l'IHM connait sans rien declarer : chaque fiche est un article
//  (ui::HelpArticle) compose ici depuis le catalogue (hmi/HmiNatives.hpp) - le
//  volet (HmiNativesPane) ne fait que l'afficher. Sans ecran : essayable.
//
//  LES CLES (le volet, l'arbre, les liens d'une fiche a l'autre, les sessions) :
//    natives                          la branche : son sommaire
//    types | types-dossier:<cle> | type:<NOM> | generique:<NOM>       (1.12.17 : 5 dossiers)
//    fonctions | dossier:<cle> | fonction:<NOM>                        (1.12.17 : par theme ;
//        categorie:<id>, la cle d'avant, mene au dossier de la categorie)
//    conversions | conv-source:<TYPE> | conversion:<X_TO_Y>
//    operateurs | operateur:<rang>
//    instructions | instruction:<rang>
//    enumerations | enums-dossier:<cle> | enum:<NOM> | enum-valeur:<NOM>#<Valeur>
//    index | index:<NOM>              De A a Z (1.12.17)
//  Une fiche se lit dans la notation choisie (ST, C, C++ : celle de l'aide,
//  gardee dans les reglages) ; son selecteur rend "notation:<N>".
// =============================================================================
#pragma once

#include "../../ui/widgets/HelpArticleView.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace app::natives {

// 1.12.1 : un operateur, une instruction aussi par son nom ("operateur:? :",
// "instruction:TRY") : la cle de l'arbre, son rang ("operateur:11", "instruction:9").
[[nodiscard]] std::string canonicalKey(std::string_view key);
// La fiche d'une cle ; vide : cle inconnue.
[[nodiscard]] ui::HelpArticle article(std::string_view key, std::string_view notation);
// Le titre de l'onglet pour une cle ("Natives · LIMIT").
[[nodiscard]] std::string title(std::string_view key);
// Ce que « Inserer » pose dans un script : l'appel ("LIMIT()"), le litteral
// (TRANSITION#Fondu), le mot (IF ... THEN). Vide : rien a inserer pour cette cle.
[[nodiscard]] std::string insertText(std::string_view key);
// Toutes les cles, dans l'ordre de l'arbre (l'essai les compose toutes).
[[nodiscard]] std::vector<std::string> allKeys();
// La cle d'une native nommee (une fonction, une conversion X_TO_Y, une valeur
// d'enumeration TRANSITION#Fondu) ; vide : pas une native (F1 dans un script).
[[nodiscard]] std::string keyOfWord(std::string_view word);


} // namespace app::natives
