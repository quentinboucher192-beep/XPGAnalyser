// =============================================================================
//  app/ai/AiChatReply.hpp - 1.50.1 : REPONDRE A UN MESSAGE, REDUIRE, COPIER
//                            (la discussion de l'Assistant IA)
// -----------------------------------------------------------------------------
//  La 1.50.0 a donne a la discussion des SESSIONS (app/hmi/SessionText) : repondre
//  a un message precis, reduire / deballer un long message, le copier. Le client :
//  << corriger les limites restantes >> - la discussion de l'Assistant IA ne les
//  avait pas. Les memes gestes, les memes mots, les memes regles
//  (sessionui::kCollapseLines, replyLabel, copyText, la barre ReplyBar) :
//
//   - REPONDRE : << Repondre >> (le survol d'un message, son clic droit) montre
//     << En reponse a Assistant : << debut du message... >> >> au-dessus de la
//     saisie (sa croix, Echap : l'annuler). La demande envoyee PORTE LA CITATION
//     (Message::extra "reponseA" : {"id", "auteur", "extrait"}) ; sa bulle montre
//     l'extrait au-dessus du texte (un clic : le message cite, choisi). LE MODELE
//     LA RECOIT, balisee, devant le texte de la demande (forModel) : les premieres
//     lignes du message cite, entre << [Citation ...] >> et << [Fin de la
//     citation] >> - il sait a quoi la demande repond. L'IA integree et la
//     demonstration ne lisent que le texte (elles ne gardent pas l'historique).
//   - REDUIRE / DEBALLER : la mise en page dans AiChatView (collapseCut) - les
//     blocs de code restent entiers.
//   - COPIER : copyText - le texte du message tel qu'il est ecrit (le markdown, les
//     blocs de code avec leurs balises) ; avec l'auteur et l'heure :
//     << 14:32 Assistant : ... >> (<< hier 14:32 >>, << 05/11 14:32 >> un autre jour).
//
//  UN STUDIO D'AVANT (1.50.0 et avant) relit la conversation : "reponseA" est un
//  champ qu'il ne connait pas, garde tel quel (Message::extra) ; il montre le
//  texte de la demande sans la citation (docs/FORMATS.md).
// =============================================================================
#pragma once

#include "AiStore.hpp"

#include <optional>
#include <string>

namespace app::ai::chatreply {

// Le message cite par une demande (vide : aucun).
struct Quote {
    std::string id;        // Message::id ("m3")
    std::string who;       // << Assistant >>, << Moi >>, << Logiciel >>
    std::string excerpt;   // le debut de son texte (une ligne, 80 caracteres au plus, sans la mise en forme)
    [[nodiscard]] bool empty() const noexcept { return id.empty() && excerpt.empty(); }
};

// Qui a ecrit ce message, tel que la discussion le dit (<< Moi >>, << Assistant >>, << Logiciel >>).
[[nodiscard]] std::string authorOf(const Message&);
// Le debut d'un texte markdown : sa premiere ligne qui a du texte (une balise de bloc de code sautee), sans **, `, #,
// les puces ; 80 caracteres au plus, << ... >> s'il y a la suite.
[[nodiscard]] std::string excerptOf(const std::string& markdown);
// La citation pour repondre a ce message.
[[nodiscard]] Quote quoteOf(const Message&);
// La citation d'une demande (Message::extra "reponseA") ; rien : pas une reponse.
[[nodiscard]] std::optional<Quote> readQuote(const Message&);
void writeQuote(Message&, const Quote&);
// Le texte que le modele recoit pour ce message : la citation balisee puis le texte (sans citation : le texte).
[[nodiscard]] std::string forModel(const Conversation&, const Message&);
// L'heure d'un message telle que la copie la dit : << 14:32 >>, << hier 14:32 >>, << 05/11 14:32 >>.
[[nodiscard]] std::string timeOf(const Message&);
// Ce que << Copier >> met dans le presse-papiers (le texte tel qu'il est ecrit) ; avec l'auteur et l'heure.
[[nodiscard]] std::string copyText(const Message&, bool withAuthor);

} // namespace app::ai::chatreply
