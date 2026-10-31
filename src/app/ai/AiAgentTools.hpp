// =============================================================================
//  app/ai/AiAgentTools.hpp - 1.21.0 : LES OUTILS DE L'AGENT (repondus sur ce PC)
// -----------------------------------------------------------------------------
//  L'IA connectee enchaine seule les etapes NON DESTRUCTIVES d'un travail :
//    analyser_projet   le graphe des relations du projet (qui utilise quoi, ce
//                      qu'une suppression toucherait, les elements orphelins),
//                      la verification globale, les conventions - LECTURE SEULE ;
//    executer_code     le bac a sable (AiSandbox.hpp) : analyse, compilation,
//                      execution des cas d'une fonction, dans un processus a part.
//  Chacun travaille sur une COPIE du projet prise au depart de la demande (sur le
//  fil de l'interface) : le fil du fournisseur ne lit jamais le projet vivant.
//  Ce qui MODIFIE le projet reste une proposition (proposer_operations) que
//  l'utilisateur valide : un outil de cette liste ne change jamais rien.
// =============================================================================
#pragma once

#include "AiProvider.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace hmi { struct Project; }

namespace app::ai::agent {

inline constexpr std::string_view kAnalyseTool = "analyser_projet";
inline constexpr int              kMaxAnalyses = 8;   // par demande

// L'outil analyser_projet sur cette copie du projet.
[[nodiscard]] LocalTool analyseTool(std::shared_ptr<const hmi::Project> snapshot);
// L'outil executer_code (le bac a sable) sur cette copie ; `program` : le programme relance (vide : celui-ci).
[[nodiscard]] LocalTool sandboxTool(std::shared_ptr<const hmi::Project> snapshot, std::string program = {});

// La reponse de analyser_projet a une entree JSON (pour les essais ; l'outil l'appelle).
[[nodiscard]] std::string analyse(const hmi::Project&, std::string_view inputJson);

} // namespace app::ai::agent
