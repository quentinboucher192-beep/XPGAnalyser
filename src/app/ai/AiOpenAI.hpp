// =============================================================================
//  app/ai/AiOpenAI.hpp - l'Assistant IA : un service COMPATIBLE OPENAI
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier FOURNISSEURS le remplit). L'API "chat
//  completions" que parlent Ollama et LM Studio (sur ce PC, gratuits, sans cle)
//  et beaucoup de services en ligne (OpenRouter, Groq, Google Gemini, Mistral...,
//  certains avec une offre gratuite) : POST <adresse>/chat/completions (stream),
//  GET <adresse>/models. Le modele reste une case libre (rien en dur) ; le
//  service propose seulement son adresse.
// =============================================================================
#pragma once

#include "AiProvider.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace app::ai {

// Un service connu : sa cle (Settings::preset), son libelle, son adresse de base (avec /v1...), sur ce PC
// (pas de limite, pas de cle) ou en ligne.
struct ServicePreset {
    std::string_view key, label, endpoint;
    bool             local{false};
    bool             needsKey{true};
};
[[nodiscard]] const std::vector<ServicePreset>& servicePresets();
[[nodiscard]] const ServicePreset* findServicePreset(std::string_view key) noexcept;

// Le fournisseur (makeProvider("openai")) ; nul dans le contrat, le chantier le remplit.
[[nodiscard]] std::unique_ptr<Provider> makeOpenAIProvider();

} // namespace app::ai
