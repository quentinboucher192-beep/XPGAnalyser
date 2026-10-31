// =============================================================================
//  app/ai/AiAutomate.cpp - l'automate relie -> l'IHM (1.16.0) : voir AiAutomate.hpp
// =============================================================================
#include "AiAutomate.hpp"

namespace app::ai {

// Le contrat : rien a proposer ; le chantier OPTIONS le remplit.
std::vector<Operation> variablesFromPlc(const hmi::Project&, const Env&, std::string_view) { return {}; }
std::vector<Operation> typesFromPlc(const hmi::Project&, const Env&, std::string_view) { return {}; }

} // namespace app::ai
