#pragma once
// 1.11 (chantier T3, CONCEPTION-T3 § 5) : les tutoriels DEDUITS. Chaque sujet du
// centre d'aide qui n'a pas de fichier .tuto ecrit a la main recoit un tutoriel
// ecrit ici, dans le format .tuto de T1 (CONCEPTION-T1 § 1, src/help/Tutorial.hpp) :
// le meme lecteur le lit, le joue, le verifie et le compte.
//
// Sans ecran : TopicInfo (celui de T1, help/TutorialLaunch.hpp) est une donnee que
// l'appli remplit depuis l'index du centre (T2) et ses sources (le guide, la
// table des expressions, la bibliotheque). A LA FUSION : help::tutorialForTopic
// (T1) appelle deduceTutorial quand aucun fichier ecrit ne porte le @sujet.
//
// Les chemins que lisent les "A toi" (TutorialApp.cpp de T1, read) : selection,
// objets[<genre>], simulation.ihm existent ; essai.convient ("oui" quand le champ
// d'essai de la page des expressions dit "convient a une case <type>") est lu depuis
// l'integration (I111). Tranche 13 (decision du chef, 03/10) : essai.texte, le texte
// tape dans ce champ (HmiExprPageView::trialText). Les deux sont lus par la scene de
// T1 (app/tutorial/TutorialExprTrial.hpp, qui appelle page->trialText()) depuis
// l'integration I111, tranche 8 : kPathsToAdd est vide.

#include "help/TutorialLaunch.hpp"   // help::TopicInfo, help::TopicKind, help::topicKindName (T1, la reference)

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::guide { struct Topic; }
namespace hmi::exprguide { struct TypeEntry; }

namespace help {

// A l'integration (I111, 02/10) : TopicInfo et TopicKind sont ceux de T1 (help/TutorialLaunch.hpp).
// Ce que lit le deducteur : Object, name (le nom francais de la tuile, "Vanne"), variants,
// properties ; Guide, place (la cible de l'arbre), headings et headingTexts ; Macro, Block,
// DataType, name et fields ; Expression, button, syntax, operators, example, exampleCaption,
// wrong, right, reason, wanted ; tous, key, title, summary. Special et Other : pas de deduit.

// Le texte .tuto ; vide si le sujet n'en permet pas (whyNoTutorial dit pourquoi).
// 5 etapes au plus, dont au moins un "A toi" avec son @verifier.
[[nodiscard]] std::string deduceTutorial(const TopicInfo& topic);
[[nodiscard]] std::string whyNoTutorial(const TopicInfo& topic);

// L'identifiant du tutoriel deduit : "deduit-<cle>".
[[nodiscard]] std::string deducedId(std::string_view key);

// Les chemins des "A toi" que la scene de T1 ne lit pas encore. Vide depuis l'integration I111
// (tranche 8) : essai.convient et essai.texte (le texte du champ d'essai, HmiExprPageView::
// trialText, une seule regle) sont lus par la scene (app/tutorial/TutorialExprTrial.hpp).
inline constexpr std::array<std::string_view, 0> kPathsToAdd{};

// Le champ d'essai et les boutons des types de la page des expressions (HmiExprScreen).
inline constexpr std::string_view kExprTrialField = "champ:help.expressions.page.essai";

} // namespace help

namespace hmi::tutotopics {

// Les TopicInfo des sources que xpg_hmi connait : un sujet du guide (objet ou sujet
// ordinaire) et un type d'expression.
[[nodiscard]] help::TopicInfo fromGuide(const hmi::guide::Topic& topic);
[[nodiscard]] help::TopicInfo fromExprType(const hmi::exprguide::TypeEntry& type);
// Tous : les sujets du guide puis les 11 types d'expression.
[[nodiscard]] std::vector<help::TopicInfo> all();
// La ligne de l'arbre d'un lieu du guide (@lieux) ; vide : aucune connue.
[[nodiscard]] std::string treePathForPlace(std::string_view place);
// Tranche 10 : la ligne de l'arbre d'un sujet du guide sans lieu connu (vide : aucune).
[[nodiscard]] std::string treePathForTopic(std::string_view key);
// Tranche 22 (R111-12, decision 53) : l'endroit d'un sujet de Demarrer qui parle de l'aide, le « ? » de la barre du haut
// ("barre:?", la cible de T1) ; il passe avant les @lieux. Vide : le sujet n'en a pas.
[[nodiscard]] std::string barPlaceForTopic(std::string_view key);
// Tranche 23 (decision 62 : au verificateur de T1, centre.ouvert lit « non » apres F1) : une condition d'« A toi »
// (le texte d'un @verifier, « simulation.ihm = marche ») que T1 a declaree sure, vue juste au verificateur sur
// l'appli : objets[...], simulation.ihm, essai.texte, essai.convient, et centre.ouvert depuis la tranche 24
// (decision 67). Un deduit n'en verifie pas d'autre.
[[nodiscard]] bool conditionDeclaredSafe(std::string_view check);
// 1.11.2 (T1, decision 141) : un des sujets « les plus vus » (ceux ou mene F1, et le chapitre Demarrer) : sans autre
// « A toi », son etape 1 fait choisir sa ligne de l'arbre (arbre.choisi(<chemin>), une fois cette condition sure).
[[nodiscard]] bool mostViewedTopic(std::string_view key);
// Le dossier a deplier d'abord (double clic) pour voir cette ligne ; vide : elle se voit.
[[nodiscard]] std::string foldedParent(std::string_view path);
// Les lieux du guide (@lieux) qui ont une ligne de l'arbre (tranche 8).
[[nodiscard]] std::vector<std::string> knownPlaces();
// Tranche 12 : le libelle de la propriete qu'une expression lie (le @param "value" du guide :
// « Valeur », ou « Position » pour la vanne 3 voies de la 1.10.4) ; vide sans valeur a lier.
[[nodiscard]] std::string valueLabel(const help::TopicInfo& topic);
// Tranche 17 (decision 13 du chef, 03/10 : six « A toi » F8 deja vrais) : une ligne de l'arbre dont le
// clic demarre la simulation de l'IHM (Simulation > IHM ouvre HmiSimulationPane, qui la demarre).
// L'etape 1 d'un sujet qui y vit l'encadre sans cliquer.
[[nodiscard]] bool startsSimulation(std::string_view place);
// Tranche 17 : l'etat que vise l'« A toi » d'une etape est-il deja atteint par les etapes d'avant
// (et @avant), autant que le deducteur peut le savoir ? Les regles : simulation.ihm = marche apres
// F8 ou un clic sur une ligne qui demarre la simulation ; essai.convient = oui apres le choix d'un
// type (la page met son premier exemple dans le champ) sans essai.texte ; essai.texte = "x" apres
// un geste qui a deja tape x ; objets[N] >= 1 apres un objet N pose ou glisse ; et tout « A toi »
// dont les gestes ont tous deja ete faits. Vide : non ; sinon « etape N : la raison » (la 1re).
[[nodiscard]] std::string atoiAlreadyReached(std::string_view tutoText);

} // namespace hmi::tutotopics
