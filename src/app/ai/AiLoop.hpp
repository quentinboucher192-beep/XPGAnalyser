// =============================================================================
//  app/ai/AiLoop.hpp - 1.22.4 : NE PAS TOURNER EN ROND (<< Proposer la suite >>)
// -----------------------------------------------------------------------------
//  Le client (01/11) : << quand on fait proposer la suite, ne pas tourner en
//  rond, verifier si on revient au meme point plusieurs fois >>. Sur son projet,
//  l'IA reproposait les memes choses (les memes variables introuvables
//  << Vanne.Cmd >>, les memes corrections) sans avancer.
//
//  Ce fichier dit, sans disque ni interface ni reseau, QU'ON REVIENT AU MEME
//  POINT, au moment ou une proposition arrive (une fois par proposition) :
//
//  LES EMPREINTES
//    - celle d'une proposition (opsPrint) : ses operations NORMALISEES (l'action,
//      le genre, le nom, la vue visee, les objets tries, les proprietes triees par
//      cle, la variable, le bord, les noms des copies, le code, les variables, les
//      membres, les parametres, les generiques, le compte, les reperes, la
//      detection, les exclusions), chaque valeur sans les blancs en trop et en
//      minuscules, les lignes TRIEES : la meme proposition reordonnee a la meme
//      empreinte, une operation differente en donne une autre. LA POSITION NE
//      COMPTE PAS (les proprietes x et y, le decalage d'une duplication) : deux
//      propositions qui ne different que par la place des objets sont la meme
//      proposition. L'etape de mission (Operation::step) ne compte pas non plus. Les
//      choix de l'utilisateur (les corrections au clic droit) ne sont pas de l'IA :
//      ils n'y sont pas. Une proposition sans operation n'a pas d'empreinte ;
//    - celle de ses erreurs (errorsPrint) : chaque message de validation
//      normalise (errorKey : minuscules, sans accents, sans les nombres isoles -
//      << operation 12 >> -, sans la liste << les plus proches >> qui change avec le
//      projet ; les noms gardes : Pompe_1, %MW100, Vanne.Cmd), tries, sans
//      doublon ; vide : aucune erreur ;
//    - la suite annoncee par l'IA (nextKey) : Proposal::next normalisee (les
//      nombres gardes : << les vues 3 a 5 >> n'est pas << les vues 6 a 8 >>).
//
//  LES SIGNES D'UN CERCLE (missionCircle : une proposition de mission ;
//  conversationCircle : une proposition d'une conversation) :
//    (a) MEMES OPERATIONS : la meme empreinte qu'une proposition precedente de la
//        meme mission (ou de la meme conversation), quel que soit son sort -
//        refusee, en echec, appliquee puis annulee, appliquee ; obsolete aussi,
//        mais seulement si elle avait ete demandee sur le MEME etat du projet (une
//        proposition perimee parce que le projet a change pendant sa preparation,
//        refaite a l'identique sur le projet du moment, n'est pas un cercle) ;
//    (b) MEMES ERREURS : les memes erreurs (non vides) que la tentative precedente
//        pour la meme etape - deux fois de suite (dans une conversation : que la
//        proposition precedente) ;
//    (c) TROP DE TENTATIVES : une etape proposee ou corrigee kMaxAttempts fois (3)
//        sans devenir terminee (Step::attempts, remis a zero quand elle l'est, et
//        quand l'utilisateur donne une consigne - Modifier la demande) ;
//    (d) LE MEME POINT : le projet a la meme empreinte (AiMissionStore : printOf,
//        prise a chaque appel) qu'a kSamePoint (2) tentatives precedentes pour la
//        meme etape - la 3e fois au meme endroit avec la meme etape en attente
//        (depuis la derniere consigne de l'utilisateur pour elle) ;
//    (e) LA MEME SUITE (une conversation) : la suite annoncee (non vide) est celle
//        d'une proposition precedente de la conversation : la demande n'avance pas.
//
//  QUAND UN CERCLE EST VU, la proposition n'est pas proposee comme si de rien
//  n'etait : elle est RETENUE (flag) - sa carte dit le cercle et propose Modifier
//  la demande, Corriger a la main, Ignorer cette etape, Forcer la fin de la
//  mission, Proposer quand meme ; elle ne s'applique pas (mission::mayApply), la
//  suite de la mission ne repart pas (nextBatch) tant que l'utilisateur n'a pas
//  choisi (choose). En amont, la consigne suivante dit a l'IA ce qui a deja ete
//  propose pour ces etapes et pourquoi cela n'a pas abouti (triedText, borne).
// =============================================================================
#pragma once

#include "AiMission.hpp"
#include "AiOperations.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::loop {

inline constexpr int kMaxAttempts = 3;   // (c) : une etape proposee 3 fois sans aboutir
inline constexpr int kSamePoint = 2;     // (d) : deja 2 tentatives depuis la meme empreinte du projet (la 3e)

// ---- les empreintes ------------------------------------------------------------------------------------------------
// La ligne normalisee d'une operation (sans la position, sans l'etape) - ce qui entre dans l'empreinte.
[[nodiscard]] std::string opLine(const Operation&);
// L'empreinte des operations (16 chiffres hexadecimaux ; vide : aucune operation).
[[nodiscard]] std::string opsPrint(const Proposal&);
[[nodiscard]] std::string opsPrint(const std::vector<Operation>&);
// Un message d'erreur normalise (voir l'en-tete) ; l'empreinte d'une liste d'erreurs (vide : aucune).
[[nodiscard]] std::string errorKey(std::string_view message);
[[nodiscard]] std::string errorsPrint(const std::vector<std::string>& errors);
// Les noms en cause dans les erreurs (le premier << ... >> de chacune, sans doublon), `max` au plus, puis "..." :
// "Vanne.Cmd, Vanne.Etat\xE2\x80\xA6". Aucune erreur ne cite de nom : le debut de la premiere.
[[nodiscard]] std::string errorNames(const std::vector<std::string>& errors, std::size_t max = 3);
// La suite normalisee (vide : aucune).
[[nodiscard]] std::string nextKey(std::string_view next);

// ---- les signes -----------------------------------------------------------------------------------------------------
enum class Sign : std::uint8_t { SameOps, SameErrors, Attempts, SamePoint, SameNext };
[[nodiscard]] std::string_view signKey(Sign) noexcept;     // "memes_operations", "memes_erreurs", "tentatives", "meme_point", "meme_suite"
[[nodiscard]] std::string_view signLabel(Sign) noexcept;   // "M\xC3\xAAmes op\xC3\xA9rations"...

struct Circle {
    std::vector<Sign>        signs;
    std::vector<std::string> lines;   // une phrase par constat : "P5 refait P3 (refus\xC3\xA9" "e ; m\xC3\xAAmes 18 op\xC3\xA9rations...)"
    std::string              text;    // les phrases jointes par " ; "
    std::string              again;   // la proposition refaite (Proposal::id ; vide : aucune)
    std::vector<std::string> steps;   // les etapes en cause (une mission), dans l'ordre de la proposition
    [[nodiscard]] bool on() const noexcept { return !signs.empty(); }
    [[nodiscard]] bool has(Sign) const noexcept;
    [[nodiscard]] std::vector<std::string> keys() const;
};

// UNE PROPOSITION DE MISSION qui vient d'arriver (mission::onGenerated : ses empreintes, les tentatives de ses etapes ;
// mission::onChecked : celle de ses erreurs, quand la verification est faite), comparee aux propositions precedentes de
// la mission : les signes (a) a (d). Rien n'est change.
[[nodiscard]] Circle missionCircle(const mission::Mission&, std::string_view proposalId);

// Une proposition d'une conversation, telle que la detection la voit (Proposal::extra << cercle >>, sinon calculee).
struct Seen {
    std::string id;
    std::string tag;           // "la 3e proposition" (la phrase du cercle)
    std::string state;         // "proposee", "appliquee", "annulee" (Ctrl+Z), "refusee"
    std::string ops;           // opsPrint
    int         opCount{0};
    std::string errors;        // errorsPrint (vide : aucune erreur, ou pas encore verifiee)
    int         errorCount{0};
    std::string names;         // errorNames
    std::string next;          // nextKey
    std::string nextText;      // Proposal::next tel quel (la phrase)
};
// Ce que la detection retient d'une proposition : les empreintes gardees dans son extra (<< cercle >>), sinon celles de
// ses operations et de sa suite (ses erreurs : inconnues).
[[nodiscard]] Seen seenOf(const Proposal&);
// "la 1re proposition", "la 3e proposition".
[[nodiscard]] std::string ordinalTag(int order);
// Les signes (a), (b) et (e) d'une proposition d'une conversation, comparee aux precedentes (dans l'ordre).
[[nodiscard]] Circle conversationCircle(const std::vector<Seen>& earlier, const Seen& now);
// Les empreintes d'une proposition, pour son extra (<< cercle >>) : operations, nb_operations, erreurs, nb_erreurs, noms,
// suite. `errors` nul : pas encore verifiee (les erreurs ne sont pas ecrites). L'Assistant y ajoute ce qu'il a vu :
// comparee (elle l'a ete a son arrivee), texte (le cercle), signes, choix (vide : retenue).
void stamp(Proposal&, const std::vector<std::string>* errors);

// ---- la mission : retenir, choisir ----------------------------------------------------------------------------------
// LE CERCLE VU : la proposition est retenue (ProposalLink : loop, loopSigns, loopSteps ; loopChoice vide), l'historique
// le dit (<< cercle >> : << La mission tourne en rond : ... >>).
void flag(mission::Mission&, std::string_view proposalId, const Circle&, std::string now);
// La proposition retenue de la mission, qui attend le choix de l'utilisateur (nul : aucune).
[[nodiscard]] const mission::ProposalLink* held(const mission::Mission&);
// LE CHOIX de l'utilisateur : "quand_meme" (Proposer quand meme), "a_la_main" (Corriger a la main), "demande_modifiee",
// "etape_ignoree", "fin_forcee". La proposition n'est plus retenue ; l'historique et les decisions le gardent. Faux :
// proposition inconnue, pas retenue, choix inconnu.
bool choose(mission::Mission&, std::string_view proposalId, std::string_view choice, std::string now, std::string* why = nullptr);
[[nodiscard]] std::string_view choiceLabel(std::string_view choice);   // "Propos\xC3\xA9" "e quand m\xC3\xAAme"...
// Une CONSIGNE de l'utilisateur pour ces etapes (Modifier la demande, dans une mission) : une decision de la mission
// (<< Consigne pour S4 >>), reprise par triedText dans les demandes suivantes. Faux : texte vide.
bool addConsigne(mission::Mission&, const std::vector<std::string>& steps, std::string text, std::string now);

// ---- en amont : ce que l'IA doit savoir -----------------------------------------------------------------------------
// LES TENTATIVES DEJA FAITES pour ces etapes, sans aboutir (refusees, obsoletes, en echec, annulees, interrompues ; les
// plus recentes d'abord, 4 au plus) : la proposition, son sort, ses operations (3), ses erreurs exactes (les 2 premieres),
// le cercle s'il a ete vu ; puis la consigne de ne pas reproposer la meme chose ; puis les consignes de l'utilisateur pour
// ces etapes (3 au plus). Borne a `maxChars` (environ ; une ligne : 480). Vide : aucune tentative ni consigne.
[[nodiscard]] std::string triedText(const mission::Mission&, const std::vector<std::string>& steps, std::size_t maxChars = 3000);

} // namespace app::ai::loop
