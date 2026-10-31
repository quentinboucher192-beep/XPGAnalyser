// =============================================================================
//  app/ai/AiAutomate.hpp - l'Assistant IA : l'automate relie -> l'IHM
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier OPTIONS le remplit). Les variables
//  globales et les types (DDT) de l'automate relie (Env::plcVariables,
//  Env::plcTypes) -> des operations creer_variables et creer_type proposees :
//  rien n'est cree avant l'apercu et Appliquer (un seul Ctrl+Z). Ce qui existe
//  deja dans l'IHM n'est pas repropose.
//
//  LA CONVERSION DES TYPES est celle de la migration 1.12.0 (hmi/HmiStandalone :
//  l'IHM rendue autonome), ici sur le texte des types : EBOOL -> BOOL,
//  STRING[n] -> STRING, TOD -> TIME_OF_DAY, DT -> DATE_AND_TIME, un tableau
//  ARRAY[a..b] OF X -> ARRAY[a..b] OF (X converti), un DDT -> le type IHM du
//  meme nom. Une instance de bloc (TON, un DFB) n'a pas d'equivalent : ecartee.
//  La description d'une variable : le commentaire de l'automate (Env::plcComment),
//  sinon "La variable API.Nom de l'automate relie".
// =============================================================================
#pragma once

#include "AiOperations.hpp"

#include <string_view>
#include <vector>

namespace app::ai {

// Les variables de l'automate (filtrees par `filter` : un morceau de nom ; vide : toutes) qui n'ont pas encore
// leur variable IHM -> une operation creer_variables (vide : rien a creer).
[[nodiscard]] std::vector<Operation> variablesFromPlc(const hmi::Project&, const Env&, std::string_view filter = {});
// Les types DDT de l'automate (filtres de meme) qui n'ont pas encore leur type IHM -> des operations creer_type.
// 1.16.0 : dans l'ordre des dependances (un DDT employe par un autre vient avant lui, meme hors du filtre).
[[nodiscard]] std::vector<Operation> typesFromPlc(const hmi::Project&, const Env&, std::string_view filter = {});

// 1.16.0 (chantier OPTIONS, en plus du contrat) : UNE PROPOSITION ENTIERE prete a valider - les types DDT
// qu'il faut (ceux du filtre et ceux qu'emploient les variables retenues, dans l'ordre des dependances), puis
// les variables (variablesFromPlc). Vide : rien a creer.
[[nodiscard]] std::vector<Operation> fromPlc(const hmi::Project&, const Env&, std::string_view filter = {});
// Le type IHM d'un type de l'automate, tel qu'il s'ecrit ("ARRAY[0..3] OF EBOOL" -> "ARRAY[0..3] OF BOOL") ;
// vide et `why` : pas d'equivalent. `ddts` : les noms des DDT de l'automate (un nom de DDT reste le meme).
[[nodiscard]] std::string hmiTypeOfPlc(std::string_view plcType, const std::vector<std::string>& ddts, const hmi::Project&,
                                       std::string* why = nullptr);

} // namespace app::ai
