// =============================================================================
//  hmi/HmiSymbolArgCheck.hpp - 1.12.11 (lot G de l'audit des objets) : LES
//                              ARGUMENTS D'UNE INSTANCE DE SYMBOLE, VERIFIES
//                              COMME CEUX D'UNE POPUP
// -----------------------------------------------------------------------------
//  Une action Ouvrir une popup voit ses arguments compares aux parametres de la
//  popup (params::checkArguments, 1.9). Une instance de symbole ne l'etait pas :
//  Generer ne disait ni un argument du mauvais type, ni un calcul donne a un
//  parametre que le symbole ecrit, ni les arguments sans nom en trop, ni une
//  constante qui ne se convertit pas (collee telle quelle). check() le dit :
//    - le type de l'argument contre celui du parametre (STRING pour un INT, une
//      T_Vanne pour une T_Pompe ; un elargissement INT -> REAL passe) ;
//    - un calcul ou une constante pour un parametre que le symbole ecrit (une
//      action, un champ de saisie, un script) : l'ecriture n'ira nulle part ;
//    - plus d'arguments sans nom que de parametres, ou un argument sans nom au
//      rang d'un parametre deja nomme : ignore ;
//    - une constante qui ne se convertit pas dans le type du parametre.
//  DES AVERTISSEMENTS : un projet qui demarrait demarre toujours. Un argument
//  nomme inconnu reste une erreur de checkSymbols (1.11.22), dite une fois.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <vector>

namespace hmi {
struct Issue;
namespace exprcheck { struct PlcPaths; }
} // namespace hmi

namespace hmi::symargs {

void check(const Project&, const exprcheck::PlcPaths&, std::vector<Issue>& out);

} // namespace hmi::symargs
