// =============================================================================
//  app/ai/AiUsage.cpp - les jetons consommes (1.16.0) : voir AiUsage.hpp
// =============================================================================
#include "AiUsage.hpp"

namespace app::ai {

// Le contrat : rien n'est encore garde ; le chantier FOURNISSEURS le remplit.
void recordUsage(std::string_view, const ProviderUsage&) {}
UsageTotals usageTotals(std::string_view) { return {}; }

} // namespace app::ai
