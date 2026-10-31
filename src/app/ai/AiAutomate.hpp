// =============================================================================
//  app/ai/AiAutomate.hpp - l'Assistant IA : l'automate relie -> l'IHM
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier OPTIONS le remplit). Les variables
//  globales et les types (DDT) de l'automate relie (Env::plcVariables,
//  Env::plcTypes) -> des operations creer_variables et creer_type proposees :
//  rien n'est cree avant l'apercu et Appliquer (un seul Ctrl+Z). Ce qui existe
//  deja dans l'IHM n'est pas repropose.
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
[[nodiscard]] std::vector<Operation> typesFromPlc(const hmi::Project&, const Env&, std::string_view filter = {});

} // namespace app::ai
