// =============================================================================
//  app/ai/AiDemo.hpp - l'Assistant IA : les reponses de demonstration
// -----------------------------------------------------------------------------
//  AUCUNE IA ICI, ET L'ECRAN LE DIT. La maquette reconnait quelques demandes
//  (des mots-cles, sans accents ni casse) et repond comme le ferait l'Assistant
//  branche : un texte (markdown simple) et, quand il y a lieu, une PROPOSITION
//  faite de vraies operations sur de vrais genres (AiOperations.hpp) :
//
//    - le synoptique de station de pompage (3 cuves, 3 bargraphes de niveau,
//      3 vannes, 3 tuyauteries, 6 pompes, 12 boutons a impulsion, un titre) ;
//    - une cuve avec un indicateur de niveau ;
//    - "celle de droite" de la selection en rouge ;
//    - aligner les quatre boutons ;
//    - associer la pompe choisie a une variable (PUMP_01_RUN n'existe pas : la
//      reponse ne l'invente pas, elle propose d'en choisir une ou de la creer) ;
//    - supprimer les vannes (plusieurs : une confirmation).
//
//  Toute autre demande : une reponse honnete, et les suggestions. Une demande
//  qui a besoin de la selection et n'en a pas la demande, sans rien proposer.
// =============================================================================
#pragma once

#include "AiStore.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

// Ce que la demande voit du projet au moment de l'envoi.
struct DemoContext {
    const hmi::Project*     project{nullptr};
    const hmi::View*        view{nullptr};
    std::vector<hmi::Id>    selection;
    std::vector<Attachment> attachments;   // celles du message
    Env                     env;
};

struct DemoReply {
    std::string             text;                 // markdown simple
    std::optional<Proposal> proposal;             // la vue, l'empreinte, la demande sont remplies
    bool                    suggestions{false};   // montrer les suggestions sous la reponse
};

[[nodiscard]] DemoReply demoReply(std::string_view request, const DemoContext&);
// Les demandes proposees (les puces cliquables du panneau), dans l'ordre.
[[nodiscard]] const std::vector<std::string>& demoSuggestions();
// Un titre de conversation tire de la premiere demande ("Station de pompage").
[[nodiscard]] std::string titleFor(std::string_view request);
// Les conversations de demonstration (premiere ouverture : marquees "demo").
[[nodiscard]] std::vector<Conversation> demoConversations();
// La demande sans accents, en minuscules, les blancs reduits (ce que la reconnaissance lit).
[[nodiscard]] std::string fold(std::string_view text);

} // namespace app::ai
