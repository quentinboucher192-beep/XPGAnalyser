// =============================================================================
//  help/CenterTutorials.hpp - 1.11 (chantier T2, tranche 8) : ce que le
//  registre des tutoriels de T1 attend du centre d'aide
// -----------------------------------------------------------------------------
//  T1 (CONCEPTION-T1 section 5, src/help/TutorialLaunch.hpp) tient
//  un registre des sujets : le centre y INSCRIT les siens (help::setTopics, un
//  help::TopicInfo par sujet), AFFICHE le compteur (help::countTutorials) et
//  passe par help::tutorialForTopic / help::startTutorial(cle du sujet).
//
//  topicFacts(ix) rend, pour chaque sujet de l'index, les champs de
//  help::TopicInfo (T1), sans dependre de son code ; TopicKindT1 suit l'ordre de
//  help::TopicKind. Le branchement est fait par I111 (integration-111) :
//  l'inscription dans app/screens/HelpCenterScreen.cpp (tutorialTopics, liens 2
//  et 4), le lanceur et la duree des tutoriels dans app/tutorial/TutorialApp.cpp
//  (liens 1 et 3).
// =============================================================================
#pragma once

#include "CenterIndex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace help::center {

// L'ordre de help::TopicKind (T1) : Object, Guide, Macro, Block, DataType,
// Expression, Special, Other.
enum class TopicKindT1 : std::uint8_t { Object, Guide, Macro, Block, DataType, Expression, Special, Other };

// Les champs de help::TopicInfo, sans `body` (le deducteur s'en passe).
struct TopicFacts {
    std::string              key;        // la cle du sujet dans le centre
    std::string              title;
    TopicKindT1              kind{TopicKindT1::Other};
    // Object : le nom francais de sa tuile ("Vanne") ; la macro, le bloc, le type DDT,
    // le type d'expression, la page speciale.
    std::string              name;
    std::vector<std::string> variants;   // Object : ses variantes (vide : la bibliotheque des objets les donnera)
    std::vector<std::string> places;     // Guide : ses @lieux
    std::vector<std::string> headings;   // Guide : ses intertitres ### (le deducteur en garde 5)
    // Tranche 10 : Object, le genre interne du guide ("Valve") ; le champ du meme nom
    // de help::TopicInfo (ajoute par I111). Vide pour les autres sujets.
    std::string              objectType;
};

// Un TopicFacts par sujet de l'index, dans l'ordre de l'arbre.
[[nodiscard]] std::vector<TopicFacts> topicFacts(const Index& ix);

} // namespace help::center
