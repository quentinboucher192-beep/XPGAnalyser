// =============================================================================
//  help/Trails.hpp - 1.12.28 : les parcours du didacticiel de l'IHM, dans l'aide F1
// -----------------------------------------------------------------------------
//  Le client (23/10) : « j'aimerais combiner le didacticiel de l'ihm avec l'aide
//  F1 ; met ca a jour en combinant ». Les six parcours (lot 21 : une visite, des
//  exercices qui font faire) vivaient a part, sur une page du volet Aide IHM. Ils
//  sont maintenant des pages de l'aide : L'IHM > Demarrer > Le didacticiel, une
//  page d'ensemble (« didacticiel ») et une page par parcours (« parcours-<cle> »),
//  ecrites dans le guide (tools/guide-ihm/guide-ihm.txt) ; la page montre la
//  carte du parcours (Commencer, Reprendre, Refaire) ; une page qui parle du
//  meme sujet le propose (« Parcours guide : ... »).
//
//  CE MODULE : le catalogue (le nom, la sorte, les etapes, la duree), et les
//  liens entre les parcours et les pages. Pur, sans ecran : l'ecran du projet
//  (TutorialWorkspace) compose les etapes ; le centre d'aide montre les cartes.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace help::trails {

struct Trail {
    std::string_view key;        // « symbole » : la cle des reglages (didacticiel.<cle>.etape)
    std::string_view title;      // « Creer un symbole de projet »
    std::string_view kind;       // « visite » ou « interactif »
    std::string_view summary;    // ce qu'on y fait, en une ligne
    int              steps{0};
    int              minutes{0};
    bool             isNew{false};
    // Les pages de l'aide qui proposent ce parcours (leurs cles dans le guide).
    std::vector<std::string_view> topics;
};

// Les parcours de l'IHM, dans l'ordre des cartes.
[[nodiscard]] const std::vector<Trail>& ihm();
[[nodiscard]] const Trail* find(std::string_view key);

// La page de l'aide d'un parcours : « parcours-<cle> ». Et dans l'autre sens : le
// parcours d'une page (nul si ce n'en est pas une).
inline constexpr std::string_view kPagePrefix = "parcours-";
inline constexpr std::string_view kOverview = "didacticiel";
[[nodiscard]] std::string pageOf(const Trail& t);
[[nodiscard]] const Trail* ofPage(std::string_view topicKey);
// Les parcours que propose une page (« symboles » : Creer un symbole de projet).
[[nodiscard]] std::vector<const Trail*> proposedOn(std::string_view topicKey);

// La progression, dans les reglages : « didacticiel.<cle>.etape » (l'etape atteinte,
// 1..n ; 0 : fini ou jamais commence) et « didacticiel.<cle>.fait ».
[[nodiscard]] std::string stepKey(const Trail& t);
[[nodiscard]] std::string doneKey(const Trail& t);

} // namespace help::trails
