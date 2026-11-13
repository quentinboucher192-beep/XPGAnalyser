// =============================================================================
//  app/ScriptChat.hpp - 1.50.1 : LES COMMANDES DE SCRIPT DES DISCUSSIONS
//                       (repondre, reduire / deballer, copier - de bout en bout)
// -----------------------------------------------------------------------------
//  La 1.50.0 n'essayait repondre / reduire / copier qu'au niveau du protocole et
//  des widgets ; le client : << pas par une vraie session entre deux PC >>. Ces
//  commandes passent par l'INTERFACE (l'onglet << Session >> du panneau du bas ;
//  la discussion de l'Assistant IA), comme un clic : l'essai de bout en bout
//  (sessione2e : deux Studio sur ce PC) et les captures s'en servent.
//
//  L'onglet Session (il s'ouvre ; N : le N-ieme MESSAGE de la discussion, sans les
//  lignes de la session ni le journal ; 1 : le plus ancien) :
//    session-ecrire "texte"               ecrire a l'equipe par le champ (\n : a la ligne)
//    session-repondre N "texte"           << Repondre >> sur le message N (la barre << En reponse a ... >>), puis
//                                         envoyer le texte : il part en reponse
//    session-cite N "debut"               le message N cite un message dont l'extrait contient << debut >> ; l'extrait
//                                         est montre au-dessus de son texte ; un clic dessus choisit le message cite
//    session-reduit N oui|non             le message N est-il reduit (<< ... voir tout >>) ?
//    session-reduire N|tout / session-deballer N
//    session-copier N [auteur|ctrl+c]     Copier (le clic droit) ; auteur : avec l'auteur et l'heure ; ctrl+c : le
//                                         message choisi, puis Ctrl+C sur la liste
//    session-presse-papiers ["texte"]     ce que contient le presse-papiers (le debut, sa longueur) ; avec un texte :
//                                         il doit le contenir
//    session-discussion                   les messages : << N. 14:32 Lea : debut... [reduit 4/9] [cite : ...] >>
//  La discussion de l'Assistant IA (N : le N-ieme message de la conversation ouverte) :
//    ia repondre N | ia citation N ["debut"] | ia reduit N oui|non | ia reduire N|tout | ia deballer N
//    ia copier N [auteur|ctrl+c|code] | ia presse-papiers ["texte"] | ia discussion | ia survoler N | ia menu N
//  Aucune ne dit un code de session ni une cle.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace ui { class Widget; }

namespace app {

class App;
namespace ai { class AiPanel; }

namespace scriptchat {

struct Result {
    enum class Kind { Done, Retry, Failed };
    Kind                     kind{Kind::Done};
    std::vector<std::string> lines;   // au journal du script ("[script] " devant)
    std::string              why;     // Failed : pourquoi
    bool                     move{false};   // ia survoler : la souris a poser la (le ScriptRunner la deplace)
    float                    x{0.f}, y{0.f};
};

[[nodiscard]] bool isSessionCommand(const std::string& cmd);
// `root` : le widget de l'ecran du dessus (l'onglet Session y est cherche) ; `retries` : les essais deja faits de la ligne.
[[nodiscard]] Result session(App& app, ui::Widget* root, const std::vector<std::string>& words, int retries);

[[nodiscard]] bool isAiCommand(const std::string& sub);
[[nodiscard]] Result ai(ai::AiPanel& panel, const std::vector<std::string>& words);

} // namespace scriptchat
} // namespace app
