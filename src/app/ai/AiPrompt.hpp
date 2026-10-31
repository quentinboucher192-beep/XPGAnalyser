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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

namespace memory { class Registry; }
namespace incidents { struct Item; }   // AiIncident.hpp

// 1.20.0 : LES DOSSIERS D'INCIDENT du projet (en attente et valides, dans n'importe quel ordre) - ceux que la
// surveillance a relus (IncidentHost::listing) ; nul, ou une liste nulle : aucun.
using IncidentSource = std::function<std::shared_ptr<const std::vector<incidents::Item>>()>;

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
    // 1.20.0 (a la fin) : les dossiers d'incident (nul : aucun - un essai). L'IA integree en diagnostique les questions
    // (AiIncidentAnswers) ; le contexte du modele en recoit un resume borne quand la demande parle d'incident.
    IncidentSource          incidents;
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

// ---- 1.16.0 (FOURNISSEURS, a la fin) : les modeles gratuits, petits ou sans appel d'outil ----------
// Sous cette fenetre de contexte (jetons), les consignes sont resserrees (compactPromptParts).
inline constexpr int kSmallContext = 16000;
// Les consignes d'un modele SANS appel d'outil : la proposition dans UN bloc ```json {"resume": ...,
// "operations": [...]} a la fin du texte (le format de l'entree de l'outil : la meme lecture stricte,
// proposalFromTool, et la meme validation).
[[nodiscard]] std::string textToolInstructions();
// Le bloc ```json de la proposition dans une reponse en texte : le dernier qui contient "operations"
// (sinon le dernier bloc). Faux : aucun. `json` : son contenu brut ; `rest` : le texte sans le bloc.
bool extractProposalBlock(std::string_view text, std::string* json, std::string* rest = nullptr);
// Un appel d'outil precedent ecrit en bloc (le tour de l'assistant, relu par un modele sans outil).
[[nodiscard]] std::string proposalBlock(std::string_view inputJson);
// Les consignes resserrees d'une petite fenetre (`contextTokens` jetons) : les regles en bref, le
// registre reduit aux genres de la vue et de la demande, la vue resumee (des budgets a sa taille).
[[nodiscard]] PromptParts compactPromptParts(const PromptInput&, int contextTokens);

} // namespace app::ai
