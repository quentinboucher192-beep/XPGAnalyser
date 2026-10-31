// =============================================================================
//  app/ai/AiUsage.hpp - l'Assistant IA : ce qui a ete consomme (les jetons)
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier FOURNISSEURS le remplit). Les jetons de
//  chaque reponse, cumules par jour et par mois et par fournisseur, dans
//  IA/usage.json (jamais la demande ni la cle) : la jauge du budget que
//  l'utilisateur se fixe, et l'affichage en direct de la limite.
// =============================================================================
#pragma once

#include "AiProvider.hpp"

#include <string>
#include <string_view>

namespace app::ai {

struct UsageTotals {
    long long tokensToday{0}, tokensMonth{0};
    long long requestsToday{0}, requestsMonth{0};
};

// Ajoute une reponse (son usage) au cumul du fournisseur ; ecrit IA/usage.json.
void recordUsage(std::string_view provider, const ProviderUsage& usage);
// Les cumuls du jour et du mois (vide : tous les fournisseurs).
[[nodiscard]] UsageTotals usageTotals(std::string_view provider = {});

} // namespace app::ai
