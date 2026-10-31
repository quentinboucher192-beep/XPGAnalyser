// =============================================================================
//  app/ai/AiProvider.cpp - les fournisseurs d'une cle des parametres
// =============================================================================
#include "AiProvider.hpp"

#include "AiAnthropic.hpp"
#include "AiOpenAI.hpp"

namespace app::ai {

std::unique_ptr<Provider> makeProvider(std::string_view key) {
    if (key == "anthropic") return std::make_unique<AnthropicProvider>();
    if (key == "openai") return makeOpenAIProvider();   // 1.16.0 (le contrat : nul tant que le chantier ne l'a pas rempli)
    return nullptr;   // "demo" (son propre chemin, sans reseau), ou un fournisseur a venir
}

} // namespace app::ai
