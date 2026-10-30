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
//
//  1.15.0 (le registre), EN PLUS, sans rien changer a ce qui precede :
//    - PromptInput a deux champs de plus, a la fin : la demande du tour (les
//      genres qu'elle evoque recoivent le detail de leurs proprietes) et le
//      registre des connaissances deja charge (nul : relu du disque) ;
//    - promptParts : les memes consignes en deux morceaux - ce qui change peu
//      (les consignes, le registre, les connaissances : a mettre en cache chez
//      le fournisseur) puis ce qui suit la demande (le detail des genres utiles,
//      le contexte de la vue) ; systemPrompt = les deux, bout a bout ;
//    - feedback : le texte du resultat d'outil quand le logiciel refuse une
//      proposition (les erreurs, et le detail des genres en cause), ecrit pour
//      le modele : il corrige au tour suivant.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "AiStore.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

namespace memory { class Registry; }

// Ce que la demande voit du projet au moment de l'envoi.
struct PromptInput {
    const hmi::Project*  project{nullptr};
    const hmi::View*     view{nullptr};       // la vue active (nul : aucune)
    std::vector<hmi::Id> selection;
    Settings             settings;            // la confidentialite : sendView, sendSelection, sendVariables
    Env                  env;                 // les noms de l'automate relie
    std::string          projectName;         // pour les connaissances par projet
    // 1.15.0 (a la fin) : la demande de ce tour (le dernier message de l'utilisateur ; ceux d'avant
    // peuvent suivre) - les genres qu'elle evoque ont le detail de leurs proprietes.
    std::string          request;
    // 1.15.0 (a la fin) : les connaissances (AiKnowledge.hpp) deja chargees ; nul : relues du disque.
    const memory::Registry* knowledge{nullptr};
};

// Les consignes et le contexte (le champ "system" de la requete).
[[nodiscard]] std::string systemPrompt(const PromptInput&);
// Le tableau "tools" (JSON) : l'outil kOperationsTool et son schema.
[[nodiscard]] std::string operationsTool();
// L'entree de l'outil -> une proposition sur la vue active (sa vue, son empreinte, la
// demande, le resume, les operations). nullopt et `why` (en francais) : illisible.
[[nodiscard]] std::optional<Proposal> proposalFromTool(std::string_view inputJson, const PromptInput&,
                                                       std::string_view request, std::string* why = nullptr);

// 1.15.0 : les consignes en deux morceaux (le premier change peu : la mise en cache du fournisseur).
struct PromptParts {
    std::string stable;    // les consignes, le format des operations, le registre, les connaissances actives
    std::string dynamic;   // le detail des genres de la vue et de la demande, puis le contexte
};
[[nodiscard]] PromptParts promptParts(const PromptInput&);
// 1.15.0 : le texte du resultat d'outil (en erreur) d'une proposition que le logiciel refuse - ses
// erreurs, et le detail des genres qu'elles citent ; vide : `check` n'a pas d'erreur.
[[nodiscard]] std::string feedback(const Check& check, const Proposal& proposal);

} // namespace app::ai
