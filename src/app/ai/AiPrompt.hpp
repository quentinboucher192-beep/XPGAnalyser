// =============================================================================
//  app/ai/AiPrompt.hpp - l'Assistant IA : ce qu'on envoie au modele, ce qu'on lit
// -----------------------------------------------------------------------------
//  LE CONTRAT ENTRE LE REGISTRE ET LE RESEAU (1.15.0). Trois fonctions, sans
//  reseau ni fil :
//
//    - systemPrompt : les consignes (ce que l'Assistant fait, ce qu'il ne fait
//      jamais : inventer une variable, une propriete, un genre), le registre des
//      genres TIRE DU CODE, le contexte du projet et de la vue selon les reglages
//      de confidentialite, et les connaissances actives (le centre de memoire) ;
//    - operationsTool : l'outil kOperationsTool (AiProvider.hpp), son schema JSON ;
//    - proposalFromTool : l'entree recue de cet outil -> une proposition sur la
//      vue active (a valider ensuite par AiOperations.hpp : validate).
//
//  Le chantier du registre ecrit ces fonctions pour de bon (AiRegistry,
//  AiContext) ; la premiere version (le contrat) en donne une forme simple.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "AiStore.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

// Ce que la demande voit du projet au moment de l'envoi.
struct PromptInput {
    const hmi::Project*  project{nullptr};
    const hmi::View*     view{nullptr};       // la vue active (nul : aucune)
    std::vector<hmi::Id> selection;
    Settings             settings;            // la confidentialite : sendView, sendSelection, sendVariables
    Env                  env;                 // les noms de l'automate relie
    std::string          projectName;         // pour les connaissances par projet
};

// Les consignes et le contexte (le champ "system" de la requete).
[[nodiscard]] std::string systemPrompt(const PromptInput&);
// Le tableau "tools" (JSON) : l'outil kOperationsTool et son schema.
[[nodiscard]] std::string operationsTool();
// L'entree de l'outil -> une proposition sur la vue active (sa vue, son empreinte, la
// demande, le resume, les operations). nullopt et `why` (en francais) : illisible.
[[nodiscard]] std::optional<Proposal> proposalFromTool(std::string_view inputJson, const PromptInput&,
                                                       std::string_view request, std::string* why = nullptr);

} // namespace app::ai
