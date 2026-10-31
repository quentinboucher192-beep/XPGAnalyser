// =============================================================================
//  app/ai/AiOpenAI.cpp - un service compatible OpenAI (1.16.0) : voir AiOpenAI.hpp
// =============================================================================
#include "AiOpenAI.hpp"

namespace app::ai {

const std::vector<ServicePreset>& servicePresets() {
    // Les adresses publiques des services ; aucune cle, aucun modele.
    static const std::vector<ServicePreset> k = {
        {"ollama", "Ollama (sur ce PC, gratuit)", "http://localhost:11434/v1", true, false},
        {"lmstudio", "LM Studio (sur ce PC, gratuit)", "http://localhost:1234/v1", true, false},
        {"openrouter", "OpenRouter (mod\xC3\xA8les gratuits ou payants)", "https://openrouter.ai/api/v1", false, true},
        {"groq", "Groq", "https://api.groq.com/openai/v1", false, true},
        {"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai", false, true},
        {"mistral", "Mistral", "https://api.mistral.ai/v1", false, true},
        {"autre", "Autre service compatible", "", false, false},
    };
    return k;
}

const ServicePreset* findServicePreset(std::string_view key) noexcept {
    for (const auto& p : servicePresets())
        if (p.key == key) return &p;
    return nullptr;
}

// Le contrat : pas encore de fournisseur ; le chantier FOURNISSEURS le remplit.
std::unique_ptr<Provider> makeOpenAIProvider() { return nullptr; }

} // namespace app::ai
