// =============================================================================
//  app/ai/AiLocal.cpp - l'IA integree (1.16.0) : voir AiLocal.hpp
// =============================================================================
#include "AiLocal.hpp"

namespace app::ai {

// Le contrat : une forme simple, que le chantier INTEGREE remplace.
LocalReply understand(const PromptInput&, std::string_view) {
    LocalReply r;
    r.text = "L'IA int\xC3\xA9gr\xC3\xA9" "e ne comprend pas encore cette demande.";
    return r;
}

} // namespace app::ai
