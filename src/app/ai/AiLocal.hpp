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
//  honnete et des formulations qu'elle comprend. Elle n'invente jamais un nom de
//  variable : elle cherche dans le projet (sans casse, sans accents, une faute
//  pres, "P1" pour Pompe_1) ; si elle hesite, elle demande.
//
//  CE QU'ELLE COMPREND : creer des objets (le genre par son libelle ou un
//  synonyme metier, au pluriel, les nombres en chiffres ou en lettres, une place
//  libre ou a cote d'un objet), un synoptique de station de pompage (ses nombres
//  lus dans la phrase), modifier (couleur, texte, taille, place, visibilite,
//  renommer), associer une variable, creer des variables et une vue, aligner,
//  repartir, dupliquer, supprimer, animer (un defaut qui clignote, une couleur
//  selon une variable), et les questions (que sais-tu faire, combien, quelles
//  variables, quels genres).
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

// 1.16.0 (le chantier INTEGREE, en plus du contrat) : UNE SUITE DE DEMANDES. Les tours precedents de
// la conversation (du plus ancien au plus recent) : la demande et la proposition de la reponse. Ils
// donnent le sens de "et une autre", "la meme en bleu", "pareil pour P2", "mets-la en rouge" : le
// dernier objet cree ou cite. understand(in, request) = understand(in, request, {}).
struct LocalTurn {
    std::string             request;    // la demande de l'utilisateur
    std::optional<Proposal> proposal;   // la proposition de la reponse (nullopt : aucune)
};
[[nodiscard]] LocalReply understand(const PromptInput& in, std::string_view request, const std::vector<LocalTurn>& history);

// Des demandes que l'IA integree comprend (les puces de la conversation vide), dans l'ordre.
[[nodiscard]] const std::vector<std::string>& localSuggestions();

} // namespace app::ai
