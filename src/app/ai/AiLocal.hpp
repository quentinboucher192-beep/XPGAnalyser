// =============================================================================
//  app/ai/AiLocal.hpp - l'Assistant IA : l'IA INTEGREE (sans connexion)
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier INTEGREE le remplit). Une IA ecrite dans le
//  logiciel : elle comprend des demandes en francais par des regles (les verbes,
//  les genres du registre et leurs synonymes, les nombres, les couleurs, les
//  positions, les noms d'objets et de variables du projet), sans reseau, sans
//  compte, sans modele a telecharger. Elle rend la meme chose qu'un vrai
//  fournisseur : un texte, et peut-etre une proposition - qui passe par la meme
//  validation, le meme apercu et le meme Ctrl+Z.
//
//  Elle ne devine pas : une demande qu'elle ne comprend pas recoit une reponse
//  honnete et des formulations qu'elle comprend.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "AiPrompt.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

struct LocalReply {
    std::string              text;          // la reponse (markdown simple, au tutoiement)
    std::optional<Proposal>  proposal;      // nullopt : rien a proposer (une question, pas compris)
    std::vector<std::string> suggestions;   // des formulations comprises (quand la demande ne l'est pas)
};

// La demande `request` sur le projet et la vue de `in` (le contexte, comme pour un vrai fournisseur).
[[nodiscard]] LocalReply understand(const PromptInput& in, std::string_view request);

} // namespace app::ai
