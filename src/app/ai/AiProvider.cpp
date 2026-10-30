// =============================================================================
//  app/ai/AiProvider.cpp - les fournisseurs d'une cle des parametres
// =============================================================================
#include "AiProvider.hpp"

#include "AiAnthropic.hpp"

namespace app::ai {

std::unique_ptr<Provider> makeProvider(std::string_view key) {
    if (key == "anthropic") return std::make_unique<AnthropicProvider>();
    return nullptr;   // "demo" (son propre chemin, sans reseau), ou un fournisseur a venir
}

} // namespace app::ai
