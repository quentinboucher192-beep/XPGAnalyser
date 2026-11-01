// =============================================================================
//  app/ai/AiMissionBoard.hpp - 1.22.0 : LE TABLEAU DE BORD D'UNE MISSION IA (au centre)
// -----------------------------------------------------------------------------
//  Une page de la zone centrale, comme << Chercher des fichiers >> : un onglet
//  << Mission IA . Station de pompage >> par mission, ouvert par le panneau de
//  l'Assistant (l'onglet Missions IA, la bande << Mission IA >> au-dessus de la
//  conversation, une notification << mission:<id> >>). De haut en bas :
//
//    l'en-tete          le nom, le projet, l'etat (sa couleur), la progression
//                       globale (sa regle en infobulle : mission::progressRule),
//                       la derniere activite ; Pause / Reprendre, Annuler...,
//                       1.22.3 : Forcer la fin de la mission... (une question),
//                       la conversation de la mission, l'ecran de reprise ;
//    la progression     planification, generation, validation, application,
//                       globale - celles de mission::progressOf, jamais un
//                       pourcentage invente ;
//    l'objectif         la demande initiale resumee (un clic : en entier) et
//                       l'analyse du projet faite par l'IA ;
//    la prochaine action mission::nextAction et le bouton qui la fait (Proposer
//                       la suite, Corriger, Actualiser le contexte, l'audit
//                       final, voir la proposition qui attend) ; les erreurs et
//                       les blocages ;
//    le plan d'action   un tableau : numero, etape, etat colore, details,
//                       dependances, progression (les operations reellement
//                       appliquees de l'etape) ; 1.22.1 : son pourcentage
//                       (mission::stepProgressOf, sa regle en infobulle), ce
//                       qu'elle fait maintenant (mission::activityOf : << L'IA
//                       prepare la proposition P2... (depuis 0:42) >>...) ; un
//                       clic deplie son JOURNAL (mission::stepJournal : ses
//                       propositions et leur sort, les operations appliquees,
//                       les erreurs, les decisions, avec l'heure) ;
//    les propositions   P1, P2... (1.22.3 : chacune depliee ou reduite a une
//                       ligne - un clic sur son en-tete, le clic droit, Tout
//                       deplier / Tout replier) : leurs nombres (operations, creations,
//                       modifications, reutilisations), leurs etapes, leur etat,
//                       Previsualiser / Verifier / Appliquer (par la conversation
//                       de la mission : les gestes du panneau) ; une proposition
//                       appliquee reste consultable (son compte rendu reel) ; la
//                       suite prevue ;
//    le rapport final   s'il existe (l'audit global, ce qui a ete fait, ce qui
//                       reste a verifier) ;
//    l'historique       la chronologie des evenements (le plus recent d'abord).
//
//  1.22.1 : l'en-tete dit aussi << En ce moment >> (l'etape du moment, son
//  pourcentage, ce qu'elle fait) ; la page se refait chaque seconde tant que
//  l'IA repond pour la mission (le temps ecoule), ou a chaque pour-cent d'une
//  application de fond - jamais a chaque image.
//
//  L'ECRAN DE REPRISE (Mode::Resume) : le dernier etat sauvegarde, les etapes
//  terminees / en cours / restantes (1.22.1 : ou en est chacune, son
//  pourcentage et ce qu'elle fait), les dernieres operations appliquees, la
//  coherence du projet (Assistant::inspectMission : le meme, modifie - la liste
//  des changements -, introuvable), la prochaine action. << Reprendre la
//  mission >> (resumeMission) ; le projet a change : << Actualiser le contexte >>
//  d'abord (refreshMission) ; introuvable : on l'explique, on ne propose pas de
//  reprendre, l'historique reste consultable.
//
//  RIEN DE LONG AU DESSIN NI A LA MISE EN PAGE : la page est refaite (une liste
//  d'ordres de dessin) quand la revision des missions change (poll, au tick),
//  quand sa largeur change, ou sur un geste ; le constat de reprise n'est refait
//  que sur un geste ou quand le projet change (au plus toutes les 2 s).
// =============================================================================
#pragma once

#include "AiAssistant.hpp"
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui { struct Theme; }

namespace app::ai {

class AiPanel;

// ---- ce que les ecrans des missions partagent (la liste, la bande du panneau, le tableau de bord) -------------------
namespace missionui {

// Une couleur dite par son sens : le theme du moment la donne au dessin.
enum class Tone : std::uint8_t { Text, Muted, Info, Accent, Ok, Warning, Error, Off, Card, Header, RowAlt, Border };
[[nodiscard]] gfx::Color color(const ui::Theme&, Tone);
[[nodiscard]] Tone stateTone(mission::State) noexcept;
[[nodiscard]] Tone stepTone(mission::StepState) noexcept;
[[nodiscard]] Tone proposalTone(std::string_view state) noexcept;
[[nodiscard]] Tone coherenceTone(mission::Coherence) noexcept;
[[nodiscard]] Tone eventTone(std::string_view kind) noexcept;
// "\xC3\xA0 valider", "appliqu\xC3\xA9" "e", "en \xC3\xA9" "chec"... (l'etat d'une proposition de la mission)
[[nodiscard]] std::string proposalStateLabel(std::string_view state);
// "Cr\xC3\xA9" "ation", "Plan modifi\xC3\xA9", "Application"... (le genre d'un evenement de l'historique)
[[nodiscard]] std::string eventLabel(std::string_view kind);
// Le nom montre : le nom de la mission, sinon le debut de sa demande.
[[nodiscard]] std::string title(const mission::Mission&);
// "12 op\xC3\xA9rations \xC2\xB7 8 cr\xC3\xA9" "ations \xC2\xB7 3 modifications \xC2\xB7 1 r\xC3\xA9utilisation"
[[nodiscard]] std::string proposalCounts(const mission::ProposalLink&);
// Les etapes, avec leur titre : "S2 Cr\xC3\xA9" "er les variables, S3 ..." (au plus `max` ; le reste : "...").
[[nodiscard]] std::string stepList(const mission::Mission&, const std::vector<std::string>& ids, std::size_t max = 6);
// 1.22.3 : LA CARTE D'UNE PROPOSITION, DEPLIEE OU REDUITE. Par defaut : depliee si elle attend une decision (a valider,
// verifiee, en echec), reduite sinon (appliquee, refusee, annulee, obsolete, interrompue). Reduite, elle dit en une ligne
// ses nombres : "302 op\xC3\xA9rations \xC2\xB7 293 appliqu\xC3\xA9" "es, 9 en \xC3\xA9" "chec" (proposalLine).
[[nodiscard]] bool        cardOpenByDefault(std::string_view proposalState) noexcept;
[[nodiscard]] std::string proposalLine(const mission::ProposalLink&);
// 1.22.3 : la question de << Forcer la fin de la mission... >> : ce qui va se passer (les etapes non terminees ignorees,
// rien de defait, une proposition en attente non appliquee, la reponse de l'IA en cours arretee si `calling`).
[[nodiscard]] std::string forceFinishQuestion(const mission::Mission&, bool calling);

// LE GESTE DU MOMENT d'une mission (le bouton de << Prochaine action >>, celui de la bande du panneau) - ce que le
// modele permet maintenant, dans l'ordre de mission::nextAction (le bouton dit ce que le texte dit) : rien si le projet
// est introuvable, reprendre (en pause), relancer la planification (pas de plan), actualiser le contexte (le projet a
// change), corriger une etape en erreur, voir la proposition qui attend une decision, proposer la suite (des etapes
// pretes), l'audit final (tout est fait). Aucun : `why` dit pourquoi.
struct Next {
    enum class Kind : std::uint8_t { None, Resume, Refresh, Review, Correct, Audit, Continue };
    Kind                     kind{Kind::None};
    std::string              label, tooltip, why;
    std::string              step;       // Correct : l'etape
    std::string              proposal;   // Review : la proposition
    std::vector<std::string> steps;      // Continue : les etapes du prochain lot
};
// `busy` : l'IA travaille deja pour cette mission (une reponse dans sa conversation, l'audit final).
[[nodiscard]] Next nextOf(const mission::Mission&, bool busy);
// L'ecran de reprise d'abord ? (en pause, un appel interrompu, le projet modifie ou introuvable)
[[nodiscard]] bool wantsResume(const mission::Mission&, mission::Coherence now, bool busy);
// La progression d'une etape, dite par ses operations REELLES (les lignes des propositions qui la servent) : faites,
// en tout ; {0, 0} : aucune operation encore.
[[nodiscard]] std::pair<int, int> stepOps(const mission::Mission&, const mission::Step&);

// 1.22.1 : CE QUE FAIT UNE ETAPE, EN DIRECT. Le moment d'une mission lu dans l'Assistant (sans rien changer) : l'IA repond-
// elle a son appel (Mission::pending, la conversation qui ecrit), depuis combien de temps (l'horloge de AiStore : now) ;
// une de ses propositions s'applique-t-elle sur un fil de fond (son avancement) ; l'audit final tourne-t-il.
[[nodiscard]] mission::Live liveOf(const Assistant&, const mission::Mission&);
// La cle du direct : change chaque seconde tant que l'IA repond pour la mission, a chaque pour-cent de son application de
// fond ; 0 si rien ne travaille. Les ecrans se refont quand elle change - jamais a chaque image.
[[nodiscard]] std::int64_t liveKey(const mission::Live&) noexcept;
[[nodiscard]] Tone activityTone(mission::Activity) noexcept;
[[nodiscard]] Tone journalTone(std::string_view kind) noexcept;
// LE MOMENT d'une mission en une ligne (la bande, la liste, l'en-tete du tableau de bord) : l'etape du moment
// (mission::currentStep), sa progression et son activite ; ou l'activite de la mission elle-meme quand aucune etape ne la
// porte (le plan ou l'actualisation que l'IA prepare, l'audit final, un appel interrompu). Vide : rien en cours.
struct Moment {
    std::string            step, title;   // "S4", son titre (vides : la mission elle-meme)
    mission::StepProgress  progress;
    mission::StepActivity  activity;
    [[nodiscard]] bool        empty() const noexcept { return activity.text.empty(); }
    [[nodiscard]] std::string line() const;   // "S4 \xC2\xB7 40 % \xE2\x80\x94 P2 : 12 op\xC3\xA9rations \xC3\xA0 valider..." (l'activite seule sans etape)
};
[[nodiscard]] Moment momentOf(const mission::Mission&, const mission::Live&);

// Le texte en lignes, au mot, a cette largeur (`maxLines` : 0, toutes ; sinon la derniere finit par ...).
[[nodiscard]] std::vector<std::string> wrap(const std::string& text, gfx::FontId, float width, std::size_t maxLines = 0);
// Coupe pour tenir dans `width` (finit par ...), jamais au milieu d'un caractere.
[[nodiscard]] std::string elide(const std::string& text, gfx::FontId, float width);
// "3 cr\xC3\xA9" "ations" / "1 cr\xC3\xA9" "ation"
[[nodiscard]] std::string count(int n, std::string_view one, std::string_view many);

// UNE LISTE D'ORDRES DE DESSIN (refaite quand ce qu'elle montre change, jamais au dessin) : la liste des missions,
// la bande, le tableau de bord se dessinent en la parcourant.
struct Op {
    enum class Kind : std::uint8_t { Text, Bold, Fill, Round, Stroke, Chip, Bar, Icon };
    Kind        kind{Kind::Text};
    gfx::Rect   r{};
    std::string text;
    gfx::FontId font{14};
    Tone        tone{Tone::Text};
    int         alpha{255};
    float       frac{0.f};       // Bar : 0..1
    ui::Icon    icon{ui::Icon::None};
};
void paintOps(const ui::PaintContext&, const std::vector<Op>&, gfx::Point origin);

} // namespace missionui

class AiMissionBoard final : public ui::Widget {
public:
    enum class Mode : std::uint8_t { Board, Resume };

    // Les gestes passent par le panneau (AiPanel::missionAct, missionProposal) : la barre d'etat, les confirmations, la
    // conversation de la mission et ses cartes sont les siennes.
    AiMissionBoard(std::string id, AiPanel& panel, std::string missionId);

    [[nodiscard]] const std::string& missionId() const noexcept { return missionId_; }
    // Le titre de l'onglet : "Mission IA \xC2\xB7 <nom>".
    [[nodiscard]] std::string tabTitle() const;
    [[nodiscard]] bool        found() const noexcept { return found_; }
    // L'IA travaille pour cette mission (une reponse, l'audit) : l'onglet le dit (sa diode).
    [[nodiscard]] bool        busy() const noexcept { return busy_; }

    void               setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    // L'historique en vue (la liste, << Consulter l'historique >> de l'ecran de reprise).
    void showHistory();
    // Relit la mission si la revision des missions (ou l'etat de l'IA) a change ; vrai : la page a change.
    bool poll();
    // Refait tout de suite (et le constat de reprise).
    void refresh();
    // Le constat de reprise (Assistant::inspectMission) - un geste ou l'ecran de reprise, jamais au dessin.
    void inspect();

    // ---- ce qu'on lit (les essais, les scripts) ----
    // Ce que la page dit, dans l'ordre (les textes dessines).
    [[nodiscard]] std::vector<std::string>   lines() const;
    [[nodiscard]] const mission::Progress&   progress() const noexcept { return progress_; }
    [[nodiscard]] const mission::Recovery&   recovery() const noexcept { return recovery_; }
    [[nodiscard]] const missionui::Next&     next() const noexcept { return next_; }
    // Le bouton d'une cle ("pause", "reprendre", "annuler", "forcer" (1.22.3), "conversation", "mode", "action", "reprise.reprendre",
    // "reprise.actualiser", "reprise.historique", "reprise.verifier", "P2.apercu", "P2.verifier", "P2.appliquer",
    // "P1.voir") ; nul : pas montre.
    [[nodiscard]] ui::Button* button(std::string_view key) const;
    // Le cadre d'une section ("entete", "progression", "objectif", "prochaine", "plan", "propositions", "suite",
    // "rapport", "historique", "reprise"), dans la page (le defilement compris) ; vide : absente.
    [[nodiscard]] gfx::Rect sectionRect(std::string_view key) const;
    // 1.22.1 : le JOURNAL d'une etape (un clic sur sa ligne du plan le deplie, un autre le replie) ; la ligne d'une etape
    // dans la page (le defilement compris ; vide : absente).
    bool                    toggleStep(const std::string& stepId);
    [[nodiscard]] bool      stepOpen(std::string_view stepId) const { return stepsOpen_.count(std::string(stepId)) > 0; }
    [[nodiscard]] gfx::Rect stepRect(std::string_view stepId) const;
    [[nodiscard]] const mission::Live& live() const noexcept { return live_; }
    // 1.22.3 : LES CARTES DES PROPOSITIONS, DEPLIEES OU REDUITES (une ligne : le chevron, P1, le titre et ses nombres,
    // l'etat). Par defaut (missionui::cardOpenByDefault) : depliees celles qui attendent une decision, reduites les autres ;
    // le choix de l'utilisateur est garde pour la seance, par mission et par proposition (AiPanel::missionCard - jamais
    // dans le fichier de la mission). Un clic sur l'en-tete d'une carte la bascule ; le clic droit : Deplier, Reduire,
    // Tout deplier, Tout replier ; l'intertitre << PROPOSITIONS (n) >> : Tout deplier / Tout replier.
    [[nodiscard]] bool      cardOpen(std::string_view proposalId) const;
    bool                    toggleCard(const std::string& proposalId);
    void                    setCardOpen(const std::string& proposalId, bool open);
    void                    setAllCards(bool open);
    [[nodiscard]] gfx::Rect cardHeadRect(std::string_view proposalId) const;   // son en-tete, a l'ecran (vide : absente)
    [[nodiscard]] gfx::Rect allCardsRect(bool open) const;                    // << Tout deplier >> (vrai) / << Tout replier >>
    [[nodiscard]] ui::PopupMenu* menu() const noexcept { return menu_; }
    enum MenuId : int { kMenuCardOpen = 1, kMenuCardClose, kMenuAllOpen, kMenuAllClose };
    void openCardMenu(const std::string& proposalId, gfx::Point at);
    void menuChosen(int id);
    [[nodiscard]] float     scrollOffset() const noexcept { return scroll_; }
    [[nodiscard]] float     contentHeight() const noexcept { return content_; }
    void                    scrollTo(float offset);

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Block {
        std::string                                     section;
        float                                           y{0.f}, h{0.f};
        std::vector<missionui::Op>                      ops;       // relatifs au haut du bloc
        std::vector<std::pair<gfx::Rect, std::string>>  tips;
        std::vector<std::pair<gfx::Rect, std::string>>  clicks;    // une zone -> l'action (une cle de doClick)
        std::vector<std::pair<gfx::Rect, std::string>>  buttons;   // une zone -> la cle du bouton
    };
    struct Slot {
        ui::Button*           button{nullptr};
        std::function<void()> run;
        bool                  used{false};
    };
    void rebuild();
    // Les sections (relatives a leur haut ; `w` : la largeur du contenu, `x` : sa marge gauche).
    Block blockMissing(float x, float w);
    Block blockHeader(const mission::Mission&, float x, float w);
    Block blockProgress(const mission::Mission&, float x, float w);
    Block blockGoal(const mission::Mission&, float x, float w);
    Block blockNext(const mission::Mission&, float x, float w);
    Block blockPlan(const mission::Mission&, float x, float w);
    Block blockProposals(const mission::Mission&, float x, float w);
    Block blockFollowing(const mission::Mission&, float x, float w);
    Block blockReport(const mission::Mission&, float x, float w);
    Block blockResume(const mission::Mission&, float x, float w);
    Block blockHistory(const mission::Mission&, float x, float w);
    void doNext();
    void placeButtons();
    void doClick(const std::string& what);
    void say(const std::string& text);
    // Un bouton garde d'une fois sur l'autre (un clic peut refaire la page pendant son signal : il n'est jamais detruit).
    float useButton(Block& blk, const std::string& key, const std::string& label, const std::string& tip, ui::Button::Style style,
                    bool enabled, gfx::Point at, std::function<void()> run);

    AiPanel&                            panel_;
    std::string                         missionId_;
    Mode                                mode_{Mode::Board};
    bool                                found_{false}, busy_{false}, generating_{false};
    std::string                         name_;
    mission::Progress                   progress_;
    mission::Recovery                   recovery_;
    bool                                inspected_{false};
    double                              inspectedAt_{-1e9};
    std::uint64_t                       docRev_{~0ull};
    missionui::Next                     next_;
    std::uint64_t                       seenRev_{0};
    std::vector<Block>                  blocks_;
    std::map<std::string, Slot>         slots_;
    float                               builtW_{-1.f};
    float                               scroll_{0.f}, content_{0.f};
    bool                                requestOpen_{false};    // la demande initiale en entier
    bool                                historyAll_{false};     // tout l'historique
    std::set<std::string>               opsOpen_;               // les propositions dont les operations sont depliees
    std::set<std::string>               stepsOpen_;             // 1.22.1 : les etapes dont le journal est deplie
    std::map<std::string, gfx::Rect>    stepRows_;              // 1.22.1 : la ligne de chaque etape (dans la page)
    mission::Live                       live_;                  // 1.22.1 : le moment (l'appel a l'IA, l'application de fond)
    std::int64_t                        liveKey_{0};
    std::string                         pendingSection_;        // a mettre en vue apres la prochaine mise en page
    std::map<std::string, float>        anchors_;               // une proposition -> le haut de sa carte (dans la page)
    std::map<std::string, gfx::Rect>    cardHeads_;             // 1.22.3 : l'en-tete de chaque carte (dans la page)
    gfx::Rect                           allOpen_{}, allClose_{};   // 1.22.3 : Tout deplier / Tout replier (dans la page)
    ui::PopupMenu*                      menu_{nullptr};         // 1.22.3 : le clic droit sur l'en-tete d'une carte
    std::string                         menuCard_;              // la proposition du menu ouvert
    ui::EdgeScrollBar                   bar_;
    core::ConnectionScope               links_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

} // namespace app::ai
