// =============================================================================
//  app/ai/AiAssistant.hpp - l'Assistant IA : la seance (le CAIManager de la demande)
// -----------------------------------------------------------------------------
//  CE QUI VIT ENTRE LE PANNEAU ET LE MODELE, SANS UN WIDGET :
//    - les conversations (chargees de IA/conversations/, ecrites a chaque pas) et
//      celle qui est ouverte ; creer, choisir, renommer, supprimer ;
//    - l'envoi d'une demande et la GENERATION de sa reponse, par etats : en
//      attente, reflexion, ecriture mot a mot, finie - et Arreter ;
//    - les pieces jointes du prochain message (copiees dans IA/pieces-jointes/,
//      leur traitement simule, visible) ;
//    - le mode (Previsualisation, Direct), les reglages, la cle API (EN MEMOIRE
//      SEULEMENT : elle n'est jamais ecrite) ;
//    - appliquer ou refuser une proposition : la commande passe par l'hote (la
//      pile de l'application : un seul Ctrl+Z) ; une proposition perimee (la vue
//      a change) ne s'applique jamais.
//
//  LE FOURNISSEUR. La demonstration (AiDemo.hpp, Settings::provider "demo"),
//  sans fil ni reseau : la reponse est calculee a l'envoi et "s'ecrit" au rythme
//  de tick() - son chemin n'a pas change. 1.15.0 : UN VRAI FOURNISSEUR
//  (AiProvider.hpp : "anthropic") part sur son fil de travail ; tick() lit ses
//  evenements a chaque image (poll) : les etats, le texte au fil de l'eau, l'appel
//  d'outil -> proposalFromTool (AiPrompt.hpp) -> validate -> la carte, comme la
//  demonstration. La requete porte les consignes et le contexte (systemPrompt),
//  l'outil des operations, et les tours de la conversation : les textes, les
//  pieces jointes, les appels d'outil precedents et leur RESULTAT REEL
//  (appliquee, refusee, en attente, annulee par Ctrl+Z, ou l'erreur de
//  validation). Une erreur (cle refusee, modele introuvable, hors ligne, delai)
//  devient un message "erreur" de la conversation, et Reessayer le renvoie.
//  Arreter ferme la connexion ; le fournisseur arrete finit sur son fil, sans
//  bloquer l'interface (retiring_).
//
//  1.16.0 : L'IA INTEGREE (AiLocal.hpp, Settings::provider "integree", le choix par
//  defaut d'une installation neuve) suit le chemin de la demonstration : sa reponse est
//  calculee a l'envoi (des regles, sur ce PC : rapide, sans fil), puis "s'ecrit" au
//  rythme de tick() apres une courte reflexion ; sa proposition entre dans la
//  conversation comme celle d'un vrai fournisseur (la carte, l'apercu, le mode Direct,
//  un Ctrl+Z). Les tours precedents de la conversation lui donnent le sens d'une suite
//  ("et une autre", "la meme en bleu").
//
//  L'HORLOGE est celle qu'on passe a tick() (les secondes de l'application) :
//  les essais et les seances de script sont reproductibles.
// =============================================================================
#pragma once

#include "AiActivity.hpp"   // 1.22.6 : ce que fait l'IA en direct, le bilan par action
#include "AiAnthropic.hpp"
#include "AiDemo.hpp"
#include "AiLearning.hpp"
#include "AiPlan.hpp"   // 1.19.0 : le plan d'action et le compte rendu
#include "AiPrompt.hpp"   // 1.20.0 : IncidentSource
#include "AiAgentTools.hpp"
#include "AiAttachments.hpp"   // 1.21.0 : le contexte de la discussion
#include "AiBuild.hpp"         // 1.22.5 : l'avis du logiciel (Generer et Compiler sur une copie)
#include "AiFileModels.hpp"    // 1.21.0 : les classeurs joints, les modeles de fichiers
#include "AiMissionStore.hpp"   // 1.22.0 : les missions IA persistantes (et AiMission.hpp)
#include "AiProvider.hpp"
#include "AiStore.hpp"
#include "AiUsage.hpp"
#include "AiViewTemplates.hpp"   // 1.22.8 : les modeles de vues (un PV d'approbation, des captures)

#include <atomic>
#include <thread>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hmi::audit { struct Report; }   // 1.22.0 : l'audit final d'une mission (HmiAudit.hpp)

namespace app::ai {

struct LocalReply;   // AiLocal.hpp

class Assistant {
public:
    // Ce que l'application donne (l'ecran du projet ; un essai, les siens).
    struct Host {
        std::function<hmi::DocumentPtr()>     document;     // le projet IHM ouvert (nul : aucun)
        std::function<hmi::Id()>              activeView;   // la vue de l'onglet ouvert (kNoId : aucune)
        std::function<std::vector<hmi::Id>()> selection;    // la selection de cet editeur
        std::function<void(core::CommandPtr)> apply;        // empiler (Ctrl+Z)
        std::function<std::string()>          projectName, projectPath;
        Env                                   env;          // les noms de l'automate relie
        // 1.16.0 (a la fin) : ouvrir une vue dans l'editeur (son onglet) - apres Appliquer une proposition qui
        // cree une vue (creer_vue), l'editeur l'ouvre. Nul : rien ne s'ouvre (un essai).
        std::function<void(hmi::Id)>          openView;
        // 1.16.0 (captures, a la fin) : lire un export de Control Expert joint (.XPG) comme l'automate de la seance
        // (IHM Studio n'en a pas d'autre) ; vrai et `said` : ce qui a ete lu ("891 variables globales et 28 types
        // (DDT)") ; faux et `said` : pourquoi. Nul : un .XPG joint reste une piece jointe ordinaire.
        std::function<bool(const std::string& path, std::string* said)> readPlc;
        // 1.19.0 (a la fin) : MONTRER un element du projet nomme dans le plan ou le compte rendu (une vue, un objet d'une
        // vue, une variable, un type, une fonction, un script, une alarme, un equipement, une ressource) : son onglet ou
        // son volet. `kind` : celui de plan::Ref. Faux : introuvable (supprime depuis). Nul : rien ne s'ouvre (un essai).
        std::function<bool(const std::string& kind, const std::string& name, const std::string& view)> reveal;
        // 1.20.0 (a la fin) : LES DOSSIERS D'INCIDENT du projet (en attente et valides) - l'IA integree repond aux
        // questions de diagnostic (<< Pourquoi cette alarme est-elle apparue ? >>...) depuis eux, l'IA distante en recoit
        // un resume borne. Nul : aucun (un essai) - rien ne change.
        IncidentSource incidents;
        // 1.21.0 (a la fin) : LA DETECTION RESEAU proposee par l'IA (l'outil detecter_equipements) - ouvrir la fenetre de
        // confirmation du scanner, preremplie (`encoded` : hmi::discovery::encodeRequest ; `reason` : pourquoi) ; c'est
        // l'utilisateur qui lance. Faux et `why` : impossible. Nul : l'outil n'est pas offert.
        std::function<bool(const std::string& encoded, const std::string& reason, std::string* why)> askScan;
        // ... et le dernier compte rendu de scan : son numero (0 : aucun ; change a chaque nouveau) et son texte.
        std::function<std::uint64_t()> scanSerial;
        std::function<std::string()>   scanReportText;
    };

    // Les etats de la generation, dans l'ordre. 1.15.0 : Proposing, l'appel d'outil s'ecrit (un
    // vrai fournisseur : les operations arrivent apres le texte).
    enum class Phase : std::uint8_t { Idle, Waiting, Thinking, Writing, Proposing };
    // Les durees de la demonstration (secondes) ; l'ecriture : des mots par seconde.
    static constexpr double kWaiting = 0.6, kThinking = 1.2, kWordsPerSecond = 36.0, kAttachSeconds = 1.2;

    explicit Assistant(Host host);
    ~Assistant();

    // ---- le cycle de vie -----------------------------------------------------------
    // Relit les reglages et les conversations (les demonstrations a la premiere ouverture) ;
    // applique la retention. `problems` : les fichiers illisibles.
    void load(std::vector<std::string>* problems = nullptr);
    // A chaque image : la generation, le traitement des pieces jointes. Vrai : quelque chose a change.
    bool tick(double now);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // ---- les conversations -----------------------------------------------------------
    [[nodiscard]] const std::vector<Conversation>& conversations() const noexcept { return conversations_; }
    [[nodiscard]] const Conversation* current() const noexcept;
    [[nodiscard]] int  currentIndex() const noexcept { return current_; }
    void               newConversation();
    bool               select(std::string_view id);
    bool               rename(std::string_view id, std::string title);
    bool               remove(std::string_view id);
    [[nodiscard]] int  indexOf(std::string_view id) const noexcept;

    // ---- envoyer, arreter ---------------------------------------------------------------
    // Faux et `why` : un texte vide, une generation en cours, des pieces jointes en traitement,
    // l'Assistant desactive.
    bool send(std::string text, std::string* why = nullptr);
    void stop();
    [[nodiscard]] Phase phase() const noexcept { return gen_.phase; }
    [[nodiscard]] bool  generating() const noexcept { return gen_.phase != Phase::Idle; }
    // Le message de l'assistant en cours d'ecriture (son indice ; -1 : aucun) et ce qui en est ecrit.
    [[nodiscard]] int         generatingMessage() const noexcept;
    // 1.16.1 : la conversation de la reponse en cours (vide : aucune) - l'avertissement de fin de reponse.
    [[nodiscard]] std::string generatingConversationId() const { return generating() ? gen_.conversation : std::string{}; }
    [[nodiscard]] std::string shownText() const;
    // Les suggestions a montrer sous la derniere reponse (une demande non reconnue).
    [[nodiscard]] bool suggestionsAfterLast() const noexcept { return suggestionsAfter_ && !generating(); }
    // 1.16.0 : LES SUGGESTIONS (les puces de la conversation vide, et sous une reponse qui en a) : celles de
    // l'IA integree (apres une demande qu'elle n'a pas comprise : les formulations qu'elle propose), sinon
    // celles de la demonstration.
    [[nodiscard]] const std::vector<std::string>& suggestions() const;

    // ---- les pieces jointes du prochain message ------------------------------------------
    struct Pending {
        Attachment attachment;
        double     started{-1};   // la seconde du debut du traitement (-1 : pas encore vu par tick)
        float      progress{0.f}; // 0..1
    };
    [[nodiscard]] const std::vector<Pending>& pending() const noexcept { return pending_; }
    bool attach(const std::string& path, std::string* why = nullptr);
    void detach(std::size_t index);
    [[nodiscard]] bool processing() const noexcept;
    // 1.21.0 : LE CONTEXTE DE LA DISCUSSION (AiAttachments.hpp) - les fichiers joints de la conversation ouverte et ceux
    // du prochain message, leur disponibilite verifiee sur le disque ; une piece retiree de la discussion ne part plus
    // avec les demandes (sa copie reste), elle se remet. Une piece en attente d'envoi retiree : detach.
    [[nodiscard]] attach::DiscussionContext discussionContext() const;
    bool removeFromDiscussion(const std::string& attachmentId, std::string* why = nullptr);
    bool restoreToDiscussion(const std::string& attachmentId, std::string* why = nullptr);
    // Une piece en cours d'analyse (le fil d'analyse : texte des PDF, DOCX, XLSX...).
    [[nodiscard]] bool analysing() const;

    // ---- 1.21.0 : LES CLASSEURS JOINTS (AiWorkbook.hpp, AiFileModels.hpp) ----------------------------------------------
    // Les classeurs (.xlsx, .xlsm) de la conversation ouverte et du prochain message, pour l'IA : lus (nul : pas encore -
    // leur lecture part alors sur un fil, tick la recoit) et la mise en page retenue (le choix de l'utilisateur, sinon le
    // modele reconnu, sinon la detection).
    [[nodiscard]] std::vector<sheets::Attached> workbooks() const;
    // Le classeur lu d'une piece ; nul : pas encore (sa lecture part sur un fil).
    [[nodiscard]] std::shared_ptr<const sheets::Workbook> workbookOf(const std::string& attachmentId) const;
    // La piece (de la conversation ouverte, ou en attente d'envoi) ; nul : introuvable.
    [[nodiscard]] const Attachment* attachmentById(const std::string& attachmentId) const;
    // 1.21.2 : un modele de fichier enregistre (cree ou mis a jour) : un message de l'Assistant dans la conversation ouverte
    // (une nouvelle s'il n'y en a pas), le modele en bloc de code JSON. Faux : rien n'a ete ajoute.
    bool postFileModel(const sheets::FileModel& m, bool created);
    // 1.21.2 : la piece dans TOUTES les conversations (et les pieces en attente) - une notification peut viser une piece
    // d'une conversation fermee ; `conversation` : la sienne (en attente : la conversation ouverte). Nul : introuvable.
    [[nodiscard]] const Attachment* findAttachment(const std::string& attachmentId, std::string* conversation = nullptr) const;
    // 1.21.2 : un scan prepare par l'IA vient d'etre propose (la confirmation ouverte) : le prochain compte rendu qui
    // arrive (dans les kScanAwaitSeconds) va dans l'onglet Notifications.
    void expectScan();
    static constexpr double kScanAwaitSeconds = 1800.0;
    // LE CHOIX de la fenetre << Modele... >> (une correspondance, un modele existant) : garde avec la piece et enregistre ;
    // un modele choisi compte une utilisation. clear : revenir a la reconnaissance de l'analyse.
    bool setWorkbookChoice(const std::string& attachmentId, const sheets::Choice& choice, std::string* why = nullptr);
    bool clearWorkbookChoice(const std::string& attachmentId, std::string* why = nullptr);
    // Les lectures de classeurs finies (les essais : attendre, puis tick).
    bool waitWorkbooks(int ms);

    // ---- 1.22.8 : LES MODELES DE VUES (AiViewTemplates.hpp) ----------------------------------------------------------
    // Les modeles de vues de la conversation ouverte et du prochain message : un PV d'approbation reconnu, une capture choisie
    // (le plus recent d'abord) - lus par l'analyse (les vues, leurs images recadrees, la charte).
    [[nodiscard]] std::vector<viewtpl::Attached> viewTemplates() const;
    // LE CHOIX << Modele de vues >> d'une piece (une image, un PDF) : on - la lire comme un modele de vues (son analyse est
    // refaite : ses vues, sa charte), off - ne plus la lire ainsi. Garde avec la piece et enregistre.
    bool setViewTemplateChoice(const std::string& attachmentId, bool on, std::string* why = nullptr);

    // ---- 1.22.0 : LES MISSIONS IA PERSISTANTES (AiMission.hpp, AiMissionStore.hpp) ----------------------------------
    // Une demande complexe devient une MISSION : un plan d'etapes ordonnees par leurs dependances, des propositions
    // successives liees a ces etapes, leur resultat REEL (le compte rendu lu dans le projet), gardee dans IA/missions
    // et reprise apres une fermeture. Les missions sont relues par load() ; chaque evenement les enregistre aussitot.
    [[nodiscard]] const std::vector<mission::Mission>& missions() const noexcept { return missions_; }
    [[nodiscard]] std::uint64_t                        missionsRevision() const noexcept { return missionsRev_; }
    [[nodiscard]] const mission::Mission*              findMission(std::string_view id) const noexcept;
    // La mission de la conversation ouverte (lancee ou reprise dans cette conversation ; la plus recente) ; nul : aucune.
    [[nodiscard]] const mission::Mission* activeMission() const noexcept;
    // La mission d'une proposition (Proposal::id) ; nul : une proposition hors mission.
    [[nodiscard]] const mission::Mission* missionOfProposal(std::string_view proposalId) const noexcept;
    // Proposer << Creer une mission IA ? >> pour cette demande : une IA connectee, une demande complexe
    // (mission::looksComplex), aucune mission en cours dans la conversation ouverte.
    [[nodiscard]] bool suggestsMission(std::string_view request) const;
    // CREER : la mission (enregistree), dans la conversation ouverte (une neuve s'il n'y en a pas) ; la demande part a
    // l'IA pour l'ANALYSE du projet et le PLAN (outil planifier_mission) - aucune operation encore. Faux et `why` : pas
    // d'IA connectee, une reponse en cours, l'Assistant desactive...
    bool createMission(std::string request, std::string* why = nullptr);
    // LA SUITE : la prochaine proposition (les etapes pretes regroupees, mission::nextBatch) demandee a l'IA dans la
    // conversation de la mission (rouverte au besoin), avec l'etat REEL du projet. Faux et `why` : rien de realisable
    // (dit : tout est fait, bloque, une proposition attend d'etre appliquee), en pause, annulee, une reponse en cours,
    // le projet de la mission n'est pas celui qui est ouvert.
    bool continueMission(std::string_view id, std::string* why = nullptr);
    // Une CORRECTION pour une etape en erreur (mission::correctionPrompt) : une nouvelle proposition, a valider - jamais
    // une modification silencieuse d'une proposition deja appliquee.
    bool correctStep(std::string_view id, std::string_view step, std::string* why = nullptr);
    // ACTUALISER LE CONTEXTE (le projet a change depuis la derniere sauvegarde) : l'IA relit le projet et revoit le
    // plan (planifier_mission) ; les etapes terminees et les operations appliquees sont gardees.
    bool refreshMission(std::string_view id, std::string* why = nullptr);
    // L'AUDIT FINAL, sur un fil de fond et une copie du projet (hmi::audit::run) : le rapport (mission::finalReport),
    // l'etat Terminee seulement si toutes les etapes obligatoires sont terminees sans erreur bloquante.
    bool auditMission(std::string_view id, std::string* why = nullptr);
    [[nodiscard]] bool auditing(std::string_view id) const;
    // Pause (la reponse en cours pour cette mission est arretee et marquee interrompue ; plus aucune application),
    // reprise (le constat de reprise d'abord - mission::recover), annulation, archivage, renommer, modifier le plan.
    bool pauseMission(std::string_view id, std::string* why = nullptr);
    bool resumeMission(std::string_view id, std::string* why = nullptr);
    bool cancelMission(std::string_view id, std::string* why = nullptr);
    // 1.22.2 : FORCER LA FIN (mission::forceFinish) - l'appel a l'IA en cours pour elle est arrete (comme Pause et
    // Annuler) ; ses propositions en attente sont refusees (leur carte dans la conversation aussi : aucune verification ni
    // application ne les pose plus) ; ses etapes non terminees sont ignorees ; elle est terminee, enregistree et notifiee ;
    // puis l'audit final, comme pour une fin normale, si son projet est ouvert (sinon le tableau de bord le propose). Rien
    // n'est defait dans le projet. canForceFinish : faux et `why` - introuvable, en lecture seule (un format plus recent),
    // deja terminee ou annulee, une de ses propositions s'applique sur un fil de fond (attendre sa fin : son compte rendu
    // reel doit d'abord arriver a la mission).
    [[nodiscard]] bool canForceFinish(std::string_view id, std::string* why = nullptr) const;
    bool forceFinishMission(std::string_view id, std::string* why = nullptr);
    bool archiveMission(std::string_view id, bool on, std::string* why = nullptr);
    bool renameMission(std::string_view id, std::string name, std::string* why = nullptr);
    bool editMissionPlan(std::string_view id, const mission::PlanEdit&, std::string* why = nullptr);
    // Reprendre une mission dans la conversation OUVERTE (une autre que la sienne : elle la retient aussi), ou ouvrir la
    // conversation la plus recente de la mission.
    bool attachMission(std::string_view id, std::string* why = nullptr);
    bool openMissionConversation(std::string_view id);
    // Le constat de reprise, sans rien changer (l'ecran de reprise) : le projet ouvert compare a la mission.
    [[nodiscard]] mission::Recovery inspectMission(std::string_view id) const;
    // Le message qui porte une proposition dans la conversation OUVERTE (-1 : pas dans celle-ci).
    [[nodiscard]] int messageOfProposal(std::string_view proposalId) const;
    // 1.22.1 : la proposition appliquee sur un fil de fond (Proposal::id ; vide : aucune) - l'activite d'une etape de mission.
    [[nodiscard]] std::string applyingProposal() const { return applyJob_ ? applyJob_->proposalId : std::string{}; }

    // ---- 1.22.6 : DE LA CONVERSATION A LA MISSION IA ------------------------------------------------------------------
    // Une grosse demande faite en conversation (la capture du 01/11 : << Propose la suite : S3b..., S5..., S9 >>, 991 k jetons)
    // se fait mieux en mission : l'utilisateur la demande (un message envoye, ou << Envoyer en mission >>), ou le logiciel et
    // l'IA la conseillent (une carte : Creer la mission, Continuer ici). JAMAIS une mission creee sans le clic de l'utilisateur.
    struct MissionAdvice {
        std::string reason;          // en clair, pour la carte ("elle \xC3\xA9num\xC3\xA8re 6 \xC3\xA9tapes (S3b, S5, S6, S7, S8, S9)")
        std::string source;          // "logiciel" (l'heuristique) ou "ia" (mission_conseillee, [MISSION_CONSEILLEE: ...])
        int         message{-1};     // le message de l'utilisateur vise (-1 : la demande pas encore envoyee)
    };
    // L'HEURISTIQUE, avant l'envoi : la demande enumere des etapes ou des lots (S3b, S5... ; une liste ; << puis >>), elle
    // demande la suite d'une conversation qui en a deja annonce plusieurs, ou - avec une demande complexe - la derniere reponse
    // a ete coupee par la longueur, la conversation a deja consomme kAdviceTokens jetons. Faux : rien a conseiller (pas d'IA
    // connectee, une question, une mission deja en cours ici, << Continuer ici >> choisi pour cette conversation).
    [[nodiscard]] bool adviseMission(std::string_view request, MissionAdvice* out = nullptr) const;
    static constexpr long long kAdviceTokens = 500000;
    // Le conseil porte par la derniere reponse de la conversation ouverte (l'IA, ou le logiciel a l'arrivee : la suite annoncee
    // une fois de trop) ; nul : aucun, deja traite, une mission deja en cours ici, ou << Continuer ici >> choisi.
    [[nodiscard]] std::optional<MissionAdvice> pendingAdvice() const;
    // << Continuer ici >> : retenu pour la conversation ouverte (plus de carte ni de conseil montre) ; le conseil en cours est
    // traite. missionAdviceDeclined : ce choix est fait pour la conversation ouverte.
    void               declineMissionAdvice();
    [[nodiscard]] bool missionAdviceDeclined() const;
    // CREER UNE MISSION AVEC CE MESSAGE (un message de l'utilisateur de la conversation ouverte) : la demande, ses pieces
    // jointes et celles de la conversation (le .XPG lu, les classeurs), le contexte utile de la conversation (conversationDigest :
    // les propositions appliquees, la suite annoncee). La mission part dans une conversation NEUVE (l'historique ne la suit pas,
    // ses jetons non plus) ; celle-ci dit << Mission << X >> creee - Ouvrir >> (un message du logiciel, lie a la mission). Puis le
    // plan (planifier_mission), comme createMission. Faux et `why` : pas un message de l'utilisateur, pas d'IA connectee...
    bool createMissionFromMessage(int message, std::string* why = nullptr);
    // ENVOYER EN MISSION : la demande tapee (et les pieces en attente) devient une mission au lieu d'un message ; une
    // conversation vide la garde (createMission), une conversation commencee : comme createMissionFromMessage.
    bool sendAsMission(std::string text, std::string* why = nullptr);
    // Le contexte d'une conversation, dit a la mission qui en sort (ses `upTo` premiers messages) ; vide : rien d'utile.
    [[nodiscard]] std::string conversationDigest(const Conversation& c, std::size_t upTo) const;

    // ---- 1.22.4 : NE PAS TOURNER EN ROND (AiLoop.hpp) ----------------------------------------------------------------
    // A son arrivee (une fois), chaque proposition est comparee aux precedentes : celles de sa mission (memes operations,
    // memes erreurs deux fois de suite, une etape tentee 3 fois sans aboutir, le projet revenu au meme point), celles de sa
    // conversation (memes operations, memes erreurs, la meme suite). UN CERCLE VU : la proposition est RETENUE - elle ne
    // s'applique pas (ni en mode Direct), la suite de la mission ne repart pas ; sa carte dit le cercle et propose les
    // choix ; la mission le note (historique, notification << La mission tourne en rond >>).
    struct LoopInfo {
        std::string              text;      // le cercle, en clair (vide : aucun)
        std::vector<std::string> signs;     // loop::signKey
        std::string              choice;    // le choix fait (vide : aucun)
        std::string              mission;   // la mission de la proposition (vide : une proposition de conversation)
        std::vector<std::string> steps;     // les etapes en cause (mission)
        std::string              tag;       // "P5" (mission) ; vide sinon
        bool                     held{false};   // retenue : un cercle, pas encore de choix, toujours << proposee >>
    };
    [[nodiscard]] LoopInfo loopOf(int message) const;
    // LE CHOIX sur la carte d'une proposition retenue (la conversation ouverte) :
    //  - ProposeAnyway (Proposer quand meme) : elle redevient une proposition comme les autres ;
    //  - ByHand (Corriger a la main) : idem, la previsualisation s'ouvre (le panneau) pour la corriger avant d'appliquer ;
    //  - EditRequest (Modifier la demande) : une conversation - la demande revient dans la saisie (`request`), la
    //    proposition reste a decider ; une mission - la proposition est refusee (ses etapes redeviennent pretes) et
    //    `request` rend le debut de la consigne a ecrire (<< Pour S4 : >>) : le prochain envoi de cette conversation qui
    //    commence ainsi devient une CONSIGNE de la mission (gardee, dite a l'IA) et la suite repart avec elle ;
    //  - SkipStep (Ignorer cette etape, une mission) : la proposition est refusee, les etapes en cause ignorees ;
    //  - ForceFinish (Forcer la fin de la mission) : forceFinishMission (le panneau demande d'abord).
    // Faux et `why` : pas de proposition retenue a ce message, un choix qui ne vaut que pour une mission, une mission en
    // lecture seule...
    enum class LoopChoice : std::uint8_t { EditRequest, ByHand, SkipStep, ForceFinish, ProposeAnyway };
    bool chooseLoop(int message, LoopChoice choice, std::string* why = nullptr, std::string* request = nullptr);
    // La consigne attendue apres Modifier la demande (une mission) : son debut (<< Pour S4 : >>) ; vide : aucune.
    [[nodiscard]] std::string steerPrefix() const { return steer_ ? steer_->prefix : std::string{}; }

    // ---- 1.22.5 : L'AVIS DU LOGICIEL (AiBuild.hpp) ET LE PROCESSUS DE DEVELOPPEMENT --------------------------------------
    // A son arrivee, CHAQUE proposition de l'IA connectee (une conversation, une mission ; lourde ou non) passe par GENERER
    // et COMPILER sur une copie du projet ou elle est appliquee (build::verdict), sur un fil de fond (startBuild, pollBuilds :
    // une copie du projet prise sur le fil de l'interface, l'environnement fige ; rien ne bloque l'interface). Son verdict
    // (les constats NOUVEAUX) est garde avec elle (Proposal::extra << logiciel >>), verse dans ses verifications
    // (build::merge : check(), la carte, l'apercu, la mission) et dit a l'IA (toolResult). Des ERREURS nouvelles et
    // << Corriger seule >> : l'Assistant demande AUSSITOT a l'IA de corriger (un message du logiciel dans la conversation ;
    // une mission : sa correction, mission::correctionPrompt) ; la proposition passe << remplacee >> (elle ne s'applique plus)
    // et la corrigee repasse par la validation et l'avis du logiciel - 2 corrections au plus par demande (kMaxFixes), puis
    // c'est dit. Les avertissements seuls ne relancent rien. Une correction qui tourne en rond (AiLoop.hpp) est retenue. Le
    // mode Direct n'applique qu'une proposition dont le verdict est propre (attendu).
    // LE PROCESSUS DE DEVELOPPEMENT (Proposal::extra << processus >>) : ce qui s'est passe pour la demande, dans l'ordre -
    // recue, verifiee, Generer et Compiler, correction demandee, corrigee... ; une proposition corrigee reprend celui de la
    // precedente (une demande, un processus). La carte le montre (repliable, depliee pendant le travail), la bande du haut
    // sa derniere etape, une mission dans l'activite de l'etape et son journal.
    static constexpr int kMaxFixes = 2;
    struct ProcessLine {
        std::string date;          // ISO
        std::string time;          // "14:02"
        std::string what;          // recue, verification, logiciel, correction, corrigee, attente, arret, cercle, direct
        std::string text;          // la phrase, sans l'heure
        int         errors{0}, warnings{0};
        std::string proposal;      // la proposition que l'entree concerne (Proposal::id)
        struct Finding {
            bool        error{true};
            std::string text;      // "Compiler : ... (Vue_Station . Btn_P2 . value)"
            std::string object, view;
        };
        std::vector<Finding> findings;   // les constats de Generer et Compiler (une entree << logiciel >>)
    };
    [[nodiscard]] static std::vector<ProcessLine> processOf(const Proposal&);
    [[nodiscard]] std::vector<ProcessLine> processOf(int message) const;
    // Le verdict garde avec la proposition (nul : pas encore, ou pas d'avis du logiciel pour elle).
    [[nodiscard]] static std::optional<build::Verdict> verdictOf(const Proposal&);
    [[nodiscard]] std::optional<build::Verdict> verdictOf(int message) const;
    // Generer et Compiler tournent pour cette proposition (son identifiant).
    [[nodiscard]] bool building(std::string_view proposalId) const;
    [[nodiscard]] bool buildingAny() const noexcept { return !buildJobs_.empty(); }
    // Le processus travaille encore pour la proposition de ce message : Generer et Compiler tournent, une correction
    // automatique attend ou s'ecrit (la carte deplie le processus).
    [[nodiscard]] bool processWorking(int message) const;
    // La derniere etape du processus d'une proposition de la conversation ouverte, en une ligne (la bande du haut) :
    // << Generer et Compiler sur une copie du projet... >>, << Correction 1/2 : l'IA corrige 2 erreurs de Compiler... >> ; vide :
    // pas de processus.
    [[nodiscard]] std::string processHeadline(int message) const;
    // La correction automatique en cours pour cette mission : ce qu'elle corrige ("Compiler : 2 erreurs") ; vide : aucune.
    [[nodiscard]] std::string autoCorrecting(std::string_view missionId) const;
    // Change a chaque verdict arrive (le panneau le passe a la previsualisation ouverte sur sa proposition).
    [[nodiscard]] std::uint64_t verdictRevision() const noexcept { return verdictRev_; }
    // LA CORRECTION DU LOGICIEL d'un constat (build::Finding : fixFrom -> fixTo, le rang du constat dans le verdict de la
    // proposition du message) : posee dans l'operation en cause de la PROPOSITION (sa propriete, sinon son code), jamais dans
    // le projet ; l'avis du logiciel est redemande. Faux et `why` : pas de correction pour ce constat, l'operation inconnue,
    // le texte plus la, la proposition n'est plus << proposee >>.
    bool applyBuildFix(int message, std::size_t finding, std::string* why = nullptr);
    // 1.22.7 : plusieurs constats d'un coup (Tout corriger rapidement : un seul avis redemande) - le nombre de corrections
    // posees ; 0 et `why` : aucune. Chacune est une RETOUCHE (Revenir la reprend : undoRetouch).
    std::size_t applyBuildFixes(int message, const std::vector<std::size_t>& findings, std::string* why = nullptr);

    // ---- les propositions -----------------------------------------------------------------
    // La proposition du message `message` de la conversation ouverte (nul : aucune).
    [[nodiscard]] const Proposal* proposal(int message) const noexcept;
    [[nodiscard]] Check check(int message) const;
    // 1.20.0 : l'environnement FIGE (les noms de l'automate, les pieces jointes de la conversation) pour verifier une
    // proposition sur un fil de fond - la carte d'une grosse proposition ; faux : impossible (verifier ici).
    bool frozenEnvironment(Env& out) const;
    // L'apercu (une copie de la vue, rien ne change dans le projet).
    [[nodiscard]] Preview previewOf(int message) const;
    // Appliquer : UN pas d'annulation. Faux et `why` : perimee, vue absente, erreurs, plus a
    // l'etat "proposee". La confirmation d'une suppression multiple est a l'appelant (needsConfirm).
    bool apply(int message, std::string* why = nullptr);
    // 1.16.1 : appliquer en laissant de cote ce qui est en erreur (l'option << Appliquer malgre les erreurs >> ; le
    // panneau a demande confirmation). Les erreurs sont notees sous la reponse et dans le journal d'activite.
    bool applyIgnoringErrors(int message, std::string* why = nullptr);
    // 1.20.1 : APPLIQUER SUR UN FIL DE FOND (une grosse proposition, au moins kBackgroundOps operations) : la proposition
    // est appliquee a une copie du projet, l'interface reste libre (la carte montre l'avancement) ; au tick, si le
    // projet n'a pas change depuis, UNE commande pose le resultat (un Ctrl+Z le reprend) - sinon rien n'est applique,
    // c'est dit. Faux : pas de fil de fond (petite proposition, environnement non figeable, une autre en cours) - appliquer
    // directement (apply).
    bool applyInBackground(int message, bool ignoringErrors, std::string* why = nullptr);
    [[nodiscard]] bool  applying(int message) const;
    [[nodiscard]] bool  applyingAny() const noexcept { return applyJob_ != nullptr; }
    // 1.22.1 : une verification de fond en cours pour cette proposition (son identifiant).
    [[nodiscard]] bool  checkingProposal(std::string_view proposalId) const;
    [[nodiscard]] float applyProgress() const;
    static constexpr std::size_t kBackgroundOps = 40;
    bool refuse(int message);
    // 1.16.1 : REVENIR SUR UN REFUS (un refus par megarde) - la proposition redevient << proposee >> ; la carte la
    // revalide (une vue changee depuis : elle le dit, et propose de redemander).
    bool unrefuse(int message);
    [[nodiscard]] bool needsConfirm(int message) const;
    // Le choix de l'utilisateur pour une variable introuvable (une variable existante, ou la creer).
    bool chooseVariable(int message, VariableChoice choice);
    // 1.22.1 : LES CORRECTIONS (le clic droit sur une erreur de l'apercu ou de la carte, AiFixes.hpp) - la proposition change,
    // JAMAIS le projet ; elle est gardee avec la conversation et se revalide par son chemin habituel (la carte, l'apercu ;
    // sur un fil de fond pour une grosse). Faux : plus << proposee >>, ou rien de valable a poser.
    //  - chooseVariable : la variable introuvable `from` (toutes les operations qui la citent) - une variable existante a la
    //    place (choice.use), ou la creer (choice.create, choice.type ; choice.use : le nom a creer, vide : creatableName) ;
    //  - clearChoice : revenir sur ce choix (la variable redevient introuvable, son erreur revient) ;
    //  - excludeOps : retirer (ou remettre) plusieurs operations d'un coup, comme setExcluded (une seule ecriture).
    bool chooseVariable(int message, const std::string& from, VariableChoice choice);
    bool clearChoice(int message, const std::string& from);
    bool excludeOps(int message, const std::vector<std::size_t>& ops, bool excluded);
    // 1.22.7 : CORRIGER RAPIDEMENT (AiFixes.hpp) - des RETOUCHES d'une operation de la PROPOSITION (jamais du projet), comme
    // chooseVariable pour une variable : chacune est gardee avec la forme d'avant de l'operation (Proposal::extra
    // << retouches >> : la categorie CORRECTIONS la liste) ; la proposition se revalide, l'avis du logiciel est redemande.
    //  - setDeleteStrategy : la strategie d'une operation supprimer (Operation::props << strategie >> : << seul >>,
    //    << remplacer >> - avec son remplacant deja donne -, << references >>) ; << inutiles >> est garde. Des objets d'une vue :
    //    << seul >> seulement ;
    //  - setOperationProperty : une propriete d'une operation (<< decaler >> d'un modifier_vue) ; jamais un secret ;
    //  - undoRetouch : revenir sur une retouche (son id) - l'operation reprend sa forme d'avant, et les retouches faites depuis
    //    sur la meme operation partent avec.
    // Faux et `why` : plus << proposee >>, l'operation n'est pas du bon genre, une valeur inconnue, la retouche n'existe plus.
    bool setDeleteStrategy(int message, std::size_t op, std::string strategy, std::string* why = nullptr);
    bool setOperationProperty(int message, std::size_t op, const std::string& key, const std::string& value, std::string* why = nullptr);
    bool undoRetouch(int message, const std::string& id, std::string* why = nullptr);
    // 1.22.8 : UNE UNITE AJOUTEE A LA PROPOSITION (<< Creer l'unite... >>, Corriger rapidement une unite inconnue) - l'operation
    // creer_unite `op` va au bout de la proposition (le plan la pose avant les variables qui l'emploient), gardee comme une
    // RETOUCHE << ajout >> (CORRECTIONS la liste ; Revenir - undoRetouch - la retire) ; jamais au projet. Le meme symbole deja
    // ajoute : rien de plus (vrai). Faux et `why` : plus << proposee >>, pas une creer_unite, une unite que planUnit refuse.
    bool addUnit(int message, const Operation& op, std::string* why = nullptr);
    // 1.14.2 : CE QU'EST DEVENUE UNE PROPOSITION APPLIQUEE, d'apres la vue telle qu'elle est (les
    // empreintes d'avant et d'apres Appliquer) : toujours appliquee ; annulee (Ctrl+Z, ou le projet
    // rouvert sans avoir ete enregistre : la vue est revenue a son etat d'avant) ; ou la vue a change
    // depuis (d'autres gestes, ou elle n'existe plus). NotApplied : pas une proposition appliquee.
    enum class Applied : std::uint8_t { NotApplied, Applied, Undone, Changed };
    [[nodiscard]] Applied appliedState(int message) const;
    // Reappliquer une proposition annulee : la vue est exactement celle de la proposition, elle
    // s'applique de nouveau (UN pas d'annulation, une nouvelle commande). Faux et `why` sinon.
    bool reapply(int message, std::string* why = nullptr);
    // 1.19.0 : LE PLAN D'ACTION de la proposition (ses etapes dans l'ordre d'execution, leurs etats) : celui garde a
    // l'application (les etats confirmes dans le projet ; << annulee >> apres un Ctrl+Z), sinon celui de la validation
    // du moment. Le meme plan que l'apercu : aucune divergence possible.
    [[nodiscard]] plan::Plan planOf(int message) const;
    // Le compte rendu reel (le projet d'avant et d'apres Appliquer) ; vide : pas encore appliquee.
    [[nodiscard]] std::optional<plan::Report> reportOf(int message) const;
    // Retirer (ou remettre) une etape du plan avant d'appliquer : les etapes qui en dependent sont ignorees a leur tour
    // (le plan est recalcule). Faux : la proposition n'est plus << proposee >>.
    bool setExcluded(int message, std::size_t op, bool excluded);

    // ---- le mode, les reglages, la cle -----------------------------------------------------
    [[nodiscard]] bool directMode() const noexcept { return settings_.mode == "direct"; }
    void               setDirectMode(bool direct);
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    void               setSettings(Settings s);   // enregistre (parametres.json)
    // La cle API : en memoire pour la seance, jamais ecrite en clair (l'ecran le dit). 1.15.0 :
    // "Memoriser la cle" la protege par Windows (DPAPI, IA/cle.bin, AiSecret.hpp) ; load() la relit.
    void setApiKey(std::string key) { apiKey_ = std::move(key); ++revision_; }
    [[nodiscard]] const std::string& apiKey() const noexcept { return apiKey_; }
    // Memoriser (protegee) ou oublier la cle de la seance. Faux et `why` : Linux (jamais ecrite),
    // pas de cle, Windows n'a pas pu la proteger.
    bool rememberKey(bool on, std::string* why = nullptr);
    [[nodiscard]] bool keyRemembered() const;
    // 1.18.0 (le gestionnaire des cles, AiKeys) : relire la cle ACTIVE du fournisseur choisi (la fenetre Cles API vient
    // de la changer) ; vide : aucune. Faux et un avis : elle ne se relit pas sur ce poste.
    bool reloadKey();
    // L'etat de connexion, tel que l'en-tete le dit : "Demonstration - aucune IA connectee",
    // "Anthropic - <modele>", "A configurer : choisis un modele et une cle"...
    [[nodiscard]] std::string connectionText() const;

    // ---- le fournisseur (1.15.0) ---------------------------------------------------------------
    using ProviderFactory = std::function<std::unique_ptr<Provider>(std::string_view key)>;
    // Un essai y met son faux fournisseur ; nul : makeProvider (AiProvider.hpp).
    void setProviderFactory(ProviderFactory f) { providerFactory_ = std::move(f); }
    // 1.21.0 : le bac a sable (l'outil executer_code) - actif dans IHM Studio, avec l'appel d'outil. `program` : le
    // programme relance en --bac-a-sable (vide : celui-ci ; les essais donnent le leur).
    void setSandbox(bool on, std::string program = {}) {
        sandbox_ = on;
        sandboxProgram_ = std::move(program);
    }
    [[nodiscard]] bool sandbox() const noexcept { return sandbox_; }
    // Le fournisseur des reglages n'est pas la demonstration.
    // Un fournisseur par le reseau (1.16.0 : providerUsesNetwork - "anthropic", "openai") ; "demo" et
    // "integree" restent sur le PC.
    [[nodiscard]] bool live() const noexcept { return providerUsesNetwork(settings_.provider); }
    // 1.16.0 : l'IA integree (AiLocal.hpp) - sur ce PC, sans connexion ni configuration.
    [[nodiscard]] bool local() const noexcept { return settings_.provider == "integree"; }
    // Il manque le modele ou la cle (l'en-tete dit "A configurer", un clic ouvre les parametres).
    [[nodiscard]] bool needsSetup() const;
    // Pret a envoyer ? Faux et `why` : ce qui manque, en francais.
    [[nodiscard]] bool ready(std::string* why = nullptr) const;
    // Ce que l'indicateur dit en plus de l'etat (une nouvelle tentative et son attente).
    [[nodiscard]] const std::string& statusDetail() const noexcept { return gen_.detail; }
    // Le dernier message de la conversation ouverte est une erreur (ou un arret) d'un vrai
    // fournisseur : Reessayer le retire et renvoie la demande.
    [[nodiscard]] bool canRetry() const;
    bool retry(std::string* why = nullptr);
    // "Ce qui part", avant le premier envoi d'une conversation a un vrai fournisseur (vide : rien
    // a dire) : la demande, la vue et ses objets, la selection, les variables, les pieces jointes.
    [[nodiscard]] std::string outgoingSummary() const;
    // La requete d'une conversation, ses messages [0, upTo) (essais ; l'envoi s'en sert) : les
    // tours, les resultats reels des appels d'outil. Ni consignes ni outil (ceux de l'envoi).
    [[nodiscard]] ProviderRequest requestFor(const Conversation&, std::size_t upTo) const;
    // Le resultat d'un appel d'outil tel que le tour suivant le dit au modele (texte, erreur ?).
    [[nodiscard]] std::pair<std::string, bool> toolResult(const Message&) const;
    // 1.22.6 : L'HISTORIQUE RESUME - les derniers appels d'outil (kFullCalls au moins ; les resumes avancent par paquets de
    // kCompactEvery, pour que l'historique se relise en cache) et toute proposition a decider partent entiers ; un appel plus
    // ancien et regle part resume dans le texte de son tour (compactCall : vide, il part entier).
    static constexpr std::size_t kFullCalls = 2, kCompactEvery = 3;
    [[nodiscard]] std::string compactCall(const Message&) const;

    // ---- la liste des modeles (Parametres : Lister les modeles, Tester la connexion) ----------
    // Sur un fil ; le resultat arrive par pollModels (tick l'appelle aussi). Faux et `why`.
    bool queryModels(const Settings& s, const std::string& key, std::string* why = nullptr);
    [[nodiscard]] bool modelsBusy() const noexcept { return modelQuery_ && modelQuery_->busy(); }
    bool               pollModels();   // vrai : un resultat vient d'arriver
    [[nodiscard]] const std::optional<ModelsResult>& models() const noexcept { return models_; }
    [[nodiscard]] std::uint64_t modelsRevision() const noexcept { return modelsRev_; }

    // Ce qui vient de se passer, pour la barre d'etat de l'ecran ("Proposition appliquee : ...").
    [[nodiscard]] std::string takeNotice() { auto n = std::move(notice_); notice_.clear(); return n; }
    // En mode Direct, une proposition qui demande confirmation attend l'appelant (son message).
    [[nodiscard]] int takeDirectConfirm() { const int m = directConfirm_; directConfirm_ = -1; return m; }

    [[nodiscard]] const Host& host() const noexcept { return host_; }
    // Le contexte d'une demande (la vue active, la selection), comme a l'envoi.
    [[nodiscard]] DemoContext context() const;
    // 1.17.0 : << Essayer une phrase >> (l'onglet Apprentissage) - l'IA integree la lit sur le projet et la vue
    // ouverts, SANS RIEN APPLIQUER ni rien mesurer : comprise ou non, et ce qu'elle proposerait.
    [[nodiscard]] learning::Trial tryPhrase(const std::string& phrase) const;

    // ---- 1.16.0 (FOURNISSEURS) : la limite et la consommation en direct, le budget ---------------
    // Ce qu'on sait de la limite du service et des jetons (les evenements Usage du fournisseur).
    [[nodiscard]] const LimitState& limitState() const noexcept { return limit_; }
    // La bande du panneau a l'instant `now` (la seconde de l'application : le compte a rebours avance).
    [[nodiscard]] UsageBand usageBand(double now) const;
    // Les cumuls du jour et du mois (IA/usage.json, relus apres chaque reponse).
    [[nodiscard]] const UsageTotals& usageTotals() const;
    // Le budget est atteint : un envoi a un fournisseur relie demande d'abord confirmation (vrai et
    // `question`, en francais). allowOverBudget : l'utilisateur a confirme, le prochain envoi passe.
    [[nodiscard]] bool budgetBlocks(std::string* question = nullptr) const;
    void allowOverBudget() noexcept { overBudgetOk_ = true; }
    // La fenetre de contexte du modele : celle des reglages, sinon celle que le service a dite
    // (Lister les modeles) ; 0 : inconnue.
    [[nodiscard]] int contextWindow() const;
    // 1.21.3 : LA LONGUEUR D'UNE REPONSE demandee (max_tokens) : celle des reglages, sinon Auto (kAutoOutputTokens, la
    // moitie de la fenetre de contexte connue au plus) ; jamais plus que la limite que le service a dite pour ce modele
    // pendant la seance (un refus 400, redemande aussitot).
    [[nodiscard]] int outputTokens() const;
    // 1.21.3 : LA SUITE d'une proposition (l'IA l'a decoupee : son champ << suite >>) - le texte a envoyer pour la
    // demander (<< Propose la suite : ... >>) ; vide : pas de suite, ou la proposition n'est pas encore appliquee.
    [[nodiscard]] std::string nextRequest(int message) const;
    // La proposition part en texte (Outil : JSON dans le texte, ou un modele qui a refuse l'outil).
    [[nodiscard]] bool toolsInText() const;
    // Le service est sur ce PC (Ollama, LM Studio, le reseau de l'usine) : pas de limite.
    [[nodiscard]] bool localService() const;

    // ---- 1.22.6 : CE QUE FAIT L'IA, EN DIRECT, ET CE QUE CA COUTE (AiActivity.hpp) ---------------------------------------
    // LA LIGNE D'ACTIVITE a l'instant `now` (la seconde de l'application) : pendant la reponse d'un fournisseur par le reseau,
    // ce qui se passe (l'envoi, la reflexion, l'ecriture, l'operation qui s'ecrit, un outil repondu sur ce PC, une attente du
    // service), le contexte (l'etape de mission servie, la correction automatique) et les dernieres etapes ; apres elle, sous
    // la derniere reponse de la conversation ouverte, la verification de fond de sa proposition, Generer et Compiler, son
    // application de fond, une correction qui attend. Vide : rien en cours. Le panneau la relit au plus kRefresh fois par seconde.
    [[nodiscard]] activity::Snapshot activity(double now) const;
    // LE BILAN de la reponse en cours (ses appels au service, le reel et les estimations) ; vide : aucune.
    [[nodiscard]] activity::Bill liveBill(double now) const;
    // Le bilan garde avec la reponse `message` de la conversation ouverte (Message::extra << bilan >>) ; nul : aucun.
    [[nodiscard]] std::optional<activity::Bill> billOf(int message) const;
    // La seconde du dernier tick (l'horloge de l'Assistant ; les essais la donnent).
    [[nodiscard]] double lastTick() const noexcept { return lastNow_; }

private:
    Conversation* cur() noexcept;
    bool          applyWith(int message, std::string* why, bool ignoringErrors);   // 1.16.1
    bool          afterApply(Conversation& c, int message, plan::Plan pl, const std::optional<hmi::Project>& before,
                             const std::vector<std::string>& skipped);             // 1.20.1 : la suite (sur le fil de l'interface)
    void          pollApply();                                                       // 1.20.1
    // 1.22.1 : LA VERIFICATION D'UNE PROPOSITION LOURDE SUR UN FIL DE FOND (checkInBackground : une operation << adresser >>,
    // ou 40 operations et plus) - jamais sur le fil de l'interface (le blocage de 8 s du 01/11 sous Windows : la validation
    // a l'arrivee de la reponse, puis celle de la mission). Sur une copie du projet et un environnement fige ; au tick
    // (pollChecks), le resultat : les comptes de la proposition, la verification de la mission, la derniere verification
    // connue (toolResult la relit au lieu de valider). Faux : pas de fil possible (pas de projet, environnement non figeable).
    enum class CheckFor : std::uint8_t { Counts, Mission };
    bool          startCheck(const std::string& conversation, const Proposal& p, CheckFor purpose, std::string missionId = {},
                             std::vector<std::string> steps = {}, int attempt = 0);
    void          pollChecks();
    void          missionChecked(mission::Mission& m, const Proposal& p, const std::vector<std::string>& steps, const Check* ck,
                                 mission::State before, const std::string& when);
    Conversation* generatingConversation() noexcept;
    Conversation* touch(Conversation& c);   // la date ; la liste retriee : rend la conversation (deplacee)
    void save(Conversation& c);
    void finish();
    void autoApply(int message);
    // 1.15.0 : un vrai fournisseur.
    bool startLive(Conversation& c, std::string request, std::string* why);
    // 1.16.0 (captures) : les exports de Control Expert joints (.XPG...) lus comme l'automate de la seance
    // (Host::readPlc) ; le texte a dire (vide : aucun).
    [[nodiscard]] std::string readPlcAttachments(const std::vector<Attachment>& attachments);
    // 1.22.3 : L'AUTOMATE D'UN .XPG JOINT, POUR TOUT APPEL A L'IA - le plus recent .XPG de la conversation (sinon des pieces de
    // la mission) est relu s'il n'est pas celui de la seance. Il n'etait lu qu'a l'envoi d'un message : un appel de mission
    // joignait le fichier sans le lire (l'IA n'en voyait que le nom : << je ne peux pas lire programme.XPG >>, 01/11), et apres
    // une relance du logiciel l'automate lu n'etait plus la. Le texte a dire (vide : rien de lu).
    [[nodiscard]] std::string ensurePlc(const Conversation& c, const mission::Mission* m = nullptr);
    // 1.16.1 : l'IA integree sur la demande `text`, avec les `turns` premiers messages de `c` pour la suite des
    // demandes (<< et une autre >>).
    [[nodiscard]] LocalReply askLocal(const Conversation* c, std::size_t turns, const DemoContext& ctx, const std::string& text) const;
    void pollLive();
    void finishLive(const std::string& stopReason);
    void failLive(const ProviderEvent& e);
    void retire();                                    // le fournisseur en cours part finir sur son fil
    [[nodiscard]] Applied appliedOf(const Proposal&) const;

    // 1.22.0 : UN APPEL A L'IA POUR UNE MISSION - la mission, son but ("plan", "proposition", "correction",
    // "actualisation"), les etapes visees (le lot d'une proposition ; l'etape a corriger), et ce qui a change dans le
    // projet depuis la derniere empreinte de la mission. Pose avant startLive (nextMission_), garde par la generation.
    struct MissionCall {
        std::string              id, purpose, step;
        std::vector<std::string> steps;
        mission::Batch           batch;
        std::string              changes;
        // 1.22.5 : une correction demandee par le logiciel (l'avis de Generer et Compiler) - la proposition corrigee, son
        // numero, ce qu'elle corrige ("Compiler : 2 erreurs"), et le retour du logiciel (build::feedbackText) ajoute a la
        // consigne de correction.
        std::string              fixOf;
        int                      fixN{0};
        std::string              fixWho;
        int                      fixErrors{0};
        std::string              feedback;
    };

    struct Generation {
        Phase       phase{Phase::Idle};
        double      start{-1};      // la seconde de l'envoi (-1 : prise au prochain tick)
        double      writingFrom{0};
        int         message{-1};
        std::string conversation;   // l'identifiant de sa conversation (on peut en ouvrir une autre)
        std::string full;           // la reponse entiere
        std::size_t words{0}, shownWords{0};
        std::optional<Proposal> proposal;
        bool        suggestions{false};
        bool        local{false};       // 1.16.0 : la reponse de l'IA integree (une reflexion plus courte)
        // 1.15.0 : un vrai fournisseur.
        bool                    live{false};
        std::string             text;          // recu jusqu'ici
        std::string             detail;        // une nouvelle tentative
        std::optional<ToolCall> tool;          // l'appel d'outil recu (complet)
        std::string             request;       // la demande de l'utilisateur
        std::string             provider, model;
        hmi::Id                 viewId{hmi::kNoId};
        std::string             fingerprint;   // l'empreinte de la vue a l'envoi
        // 1.16.0 (FOURNISSEURS) : les jetons recus (le dernier Usage fait foi) ; la proposition en texte.
        ProviderUsage           usage;
        bool                    textTool{false};
        // 1.20.0 : les lectures prealables (les parties d'un gros texte joint lues a part) - ajoutees a la consommation.
        ProviderUsage           preread;
        // 1.21.3 : la longueur demandee (max_tokens) ; une proposition s'ecrivait (l'appel d'outil, ou le bloc JSON en
        // texte) ; cette reponse est la reprise en plusieurs parties d'une reponse coupee ; ce que la reponse coupee a
        // consomme (deja compte au cumul : montre avec celle-ci, jamais recompte).
        int                     maxTokens{0};
        bool                    proposing{false};
        bool                    split{false};
        ProviderUsage           carried;
        // 1.22.0 : l'appel de mission de cette reponse (vide : une demande ordinaire).
        MissionCall             mission;
        // 1.22.5 : la correction automatique d'une proposition de conversation (l'avis du logiciel) - la proposition qu'elle
        // corrige (vide : une demande ordinaire), son numero (1, 2), ce qu'elle corrige ("Compiler : 2 erreurs").
        std::string             fixOf;
        int                     fixN{0};
        std::string             fixWho;       // "Compiler", "G\xC3\xA9n\xC3\xA9rer", "G\xC3\xA9n\xC3\xA9rer et Compiler"
        int                     fixErrors{0};
        // 1.22.6 : ce que fait l'IA (les evenements du fournisseur, a l'instant) et ce que coutent ses appels.
        activity::Tracker       act;
    };

    Host                      host_;
    Settings                  settings_;
    std::string               apiKey_;
    std::vector<Conversation> conversations_;
    int                       current_{-1};
    Generation                gen_;
    std::vector<Pending>      pending_;
    bool                      suggestionsAfter_{false};
    std::vector<std::string>  localSuggestions_;   // 1.16.0 : celles de la derniere reponse de l'IA integree
    std::string               notice_;
    int                       directConfirm_{-1};
    double                    lastNow_{0};
    std::uint64_t             revision_{1};
    ProviderFactory                        providerFactory_;
    bool                                   sandbox_{true};     // 1.21.0 : executer_code
    std::shared_ptr<agent::Bridge>         bridge_;            // 1.21.0 : le pont des outils de l'agent (detection reseau)
    std::string                            sandboxProgram_;
    // 1.21.0 : le fil d'analyse des pieces jointes (cree au premier fichier joint) ; les souvenirs proposes par l'IA
    // pendant la reponse (l'outil proposer_souvenir, sur le fil du fournisseur) ; le projet dont les anciens souvenirs
    // (lies par le nom seul) ont ete rattaches a son identifiant.
    std::unique_ptr<attach::Analyzer>      analyzer_;
    std::shared_ptr<std::atomic<int>>      proposedMemories_ = std::make_shared<std::atomic<int>>(0);
    std::string                            boundProject_;
    double                                 scanAwaited_{-1.0};   // 1.21.2 : la seconde ou l'IA a prepare un scan (-1 : aucun)
    void submitAnalysis(Attachment& a);
    void pollAnalyses();
    // 1.21.0 : les classeurs lus (par piece jointe), et leurs lectures en cours (une ancienne conversation rouverte : ses
    // classeurs relus sur un fil, jamais sur celui de l'interface).
    struct WorkbookLoad {
        std::string                       id;
        std::shared_ptr<sheets::Workbook> result;
        std::atomic<bool>                 finished{false};
        std::thread                       worker;
    };
    mutable std::map<std::string, std::shared_ptr<const sheets::Workbook>> workbooks_;
    mutable std::vector<std::shared_ptr<WorkbookLoad>>                     loads_;
    void requestWorkbook(const Attachment& a) const;
    void pollWorkbooks();
    void keepWorkbook(const std::string& id, std::shared_ptr<const sheets::Workbook> wb) const;
    [[nodiscard]] std::vector<sheets::Attached> attachedWorkbooks(const Conversation* c, bool withPending) const;
    [[nodiscard]] std::vector<viewtpl::Attached> attachedTemplates(const Conversation* c, bool withPending) const;   // 1.22.8
    Attachment* attachmentMut(const std::string& attachmentId, Conversation** owner);
    std::unique_ptr<Provider>              provider_;
    std::vector<std::unique_ptr<Provider>> retiring_;   // arretes : ils finissent sur leur fil
    // 1.20.1 : l'application de fond en cours (une a la fois).
    struct ApplyJob {
        std::string                         conversation, proposalId;
        int                                 message{-1};
        bool                                ignoringErrors{false};
        std::uint64_t                       revision{0};
        std::shared_ptr<const hmi::Project> before;
        std::atomic<std::size_t>            done{0};
        std::size_t                         total{1};
        std::atomic<bool>                   finished{false};
        std::shared_ptr<hmi::Project>       result;
        std::string                         why;
        std::vector<std::string>            skipped;
        plan::Plan                          plan;
        std::thread                         worker;
    };
    std::shared_ptr<ApplyJob> applyJob_;
    // 1.22.1 : les verifications de fond en cours, et la derniere connue de chaque proposition (la revision du projet).
    struct CheckJob {
        std::string                         conversation, proposalId, mission;
        std::vector<std::string>            steps;
        CheckFor                            purpose{CheckFor::Counts};
        int                                 attempt{0};
        std::uint64_t                       revision{0};
        std::atomic<bool>                   finished{false};
        Check                               result;
        std::thread                         worker;
        double                              started{0};   // 1.22.6 : la seconde de son depart (l'activite)
    };
    std::vector<std::shared_ptr<CheckJob>> checkJobs_;
    struct KnownCheck {
        std::uint64_t revision{0};
        Check         check;
    };
    std::map<std::string, KnownCheck>      knownChecks_;
    // 1.22.5 : L'AVIS DU LOGICIEL - un job a part de CheckJob : il ne rend pas un Check mais un build::Verdict, il dure (tout
    // le projet est genere et compile), il n'est jamais refait quand le projet change (l'avis vaut pour le projet de la
    // proposition ; sa validation, elle, suit le projet) et sa fin decide d'une correction automatique.
    struct BuildJob {
        std::string                         conversation, proposalId;
        std::string                         key;      // le contenu de la proposition (change depuis : le verdict ne vaut plus)
        std::atomic<bool>                   finished{false};
        build::Verdict                      result;
        std::thread                         worker;
        double                              started{0};   // 1.22.6 : la seconde de son depart (l'activite)
    };
    std::vector<std::shared_ptr<BuildJob>> buildJobs_;
    std::uint64_t                          verdictRev_{0};
    // Les corrections automatiques decidees, qui attendent la fin d'une reponse en cours (elles partent au tick).
    struct AutoFix {
        std::string conversation, proposalId, mission;
        std::string who;      // les etapes en cause : "Compiler", "G\xC3\xA9n\xC3\xA9rer", "G\xC3\xA9n\xC3\xA9rer et Compiler"
        int         n{1};     // la correction (1, 2)
        int         errors{0};
    };
    std::vector<AutoFix>                   fixQueue_;
    std::set<std::string>                  settled_;          // les verdicts deja tires (la correction decidee une fois)
    std::set<std::string>                  directWaiting_;    // mode Direct : appliquee quand son verdict propre arrive
    AutoFix                                nextFix_;          // la correction du prochain startLive (consommee par lui)
    // Generer et Compiler pour la proposition de ce message (`validated` : son Check de validate, s'il est la - l'operation de
    // chaque constat) ; impossible (l'option decochee, pas de projet...) : un verdict << pas lances >> et pourquoi (la
    // previsualisation ne reste jamais << en cours >>). Vrai : le fil de fond est parti.
    bool          startBuild(Conversation& c, int message, const Check* validated = nullptr);
    void          noVerdict(Proposal& p, const std::string& why);
    // La proposition a change (une correction au clic droit, une operation retiree, la correction du logiciel) : son avis ne
    // vaut plus - efface, et redemande (une proposition de l'IA connectee).
    void          proposalChanged(Conversation& c, int message);
    void          pollBuilds();
    void          settleVerdict(Conversation& c, int message, const Check& validation);
    bool          validationKnown(const Proposal& p, Check* out) const;
    void          runFixes();
    bool          launchFix(const AutoFix& fix, std::string* why);
    void          linkCorrection(Conversation& c, int message, const std::string& oldId, int n, const std::string& said);
    void          fixAborted(const std::string& conversation, const std::string& oldId, const std::string& mission, const std::string& reason);
    void          addProcess(Proposal& p, const std::string& what, const std::string& text, int errors = 0, int warnings = 0,
                             const build::Verdict* v = nullptr);
    std::unique_ptr<ModelQuery>            modelQuery_;
    std::optional<ModelsResult>            models_;
    std::uint64_t                          modelsRev_{0};
    // 1.16.0 (FOURNISSEURS) : la limite, les cumuls (et leur jour), le budget confirme, les fenetres de
    // contexte dites par les services (fournisseur|modele), le modele qui refuse l'outil.
    void recordLive();
    LimitState                             limit_;
    mutable UsageTotals                    totals_;
    mutable std::string                    totalsDay_;
    bool                                   overBudgetOk_{false};
    std::map<std::string, long long>       modelContext_;
    // 1.22.0 : les missions (relues par load, enregistrees a chaque evenement) et leur revision.
    std::vector<mission::Mission>          missions_;
    std::uint64_t                          missionsRev_{1};
    // 1.22.0 (la mise en oeuvre, AiAssistant.cpp << LES MISSIONS >>) :
    //  - les missions lues d'un format plus recent (jamais reecrites) ; celles dont un appel a l'IA etait en cours a la
    //    fermeture (interrompues : constatees au premier tick ou le projet ouvert est connu) ;
    //  - l'appel de mission du prochain startLive (consomme par lui) ;
    //  - le projet vu au dernier tick (le document et sa revision) : la verification des missions (verifyAgainst) ne se
    //    refait que s'il a change, jamais a chaque image ; l'identite du projet ouvert, gardee tant que son nom, son
    //    chemin et sa date de creation ne changent pas ;
    //  - les audits finals en cours (une copie du projet, un fil chacun ; le resultat sonde au tick).
    std::set<std::string, std::less<>>     missionsReadOnly_;
    std::vector<std::string>               missionsToRecover_;
    MissionCall                            nextMission_;
    const void*                            missionDoc_{nullptr};       // le projet verifie (document, revision)
    std::uint64_t                          missionDocRev_{0};
    const void*                            missionSeenDoc_{nullptr};   // le dernier vu, et quand (il se pose avant d'etre verifie)
    std::uint64_t                          missionSeenRev_{0};
    double                                 missionSeenAt_{0};
    static constexpr double                kMissionSettle = 0.25;      // secondes sans changement avant la verification
    mutable std::string                    missionProjectKey_, missionProjectId_;
    struct AuditJob {
        std::string                                    mission;
        std::shared_ptr<const hmi::Project>            copy;
        std::function<bool(std::string_view)>          plc;        // les noms de l'automate, figes (nul : aucun automate)
        std::string                                    folder;     // le dossier du projet (les fichiers externes relatifs)
        bool                                           plcUnknown{false};   // un automate relie, ses noms non figeables
        const void*                                    doc{nullptr};
        std::uint64_t                                  revision{0};
        std::shared_ptr<hmi::audit::Report>            result;     // nul : l'audit n'a pas pu se faire
        std::atomic<bool>                              finished{false};
        std::thread                                    worker;
    };
    std::vector<std::shared_ptr<AuditJob>> audits_;
    [[nodiscard]] mission::Mission*        missionMut(std::string_view id) noexcept;
    [[nodiscard]] mission::Mission*        missionOfProposalMut(std::string_view proposalId) noexcept;
    // La mission modifiable (faux et `why` : introuvable, ou lue d'un format plus recent).
    [[nodiscard]] mission::Mission*        editableMission(std::string_view id, std::string* why);
    [[nodiscard]] std::string              openProjectId() const;   // memory::identifyProject du projet ouvert (vide : aucun)
    void                                   saveMission(mission::Mission& m);
    void                                   notifyMission(const mission::Mission& m, const std::string& kind, std::string title,
                                                         std::string detail, std::string tone);
    void                                   missionTransition(const mission::Mission& m, mission::State before);
    [[nodiscard]] Conversation*            missionConversation(mission::Mission& m);
    // 1.22.6 : `carried` - des pieces jointes d'une autre conversation qui partent avec la demande (createMissionWith).
    bool                                   launchMission(mission::Mission& m, std::string text, MissionCall call, std::string* why,
                                                         const std::vector<Attachment>* carried = nullptr);
    // 1.22.6 : la mission d'une demande venue d'une conversation commencee (`message` : son message, -1 : la saisie).
    bool                                   createMissionWith(std::string request, int message, std::string* why);
    // 1.22.6 : le conseil de la reponse qui vient d'arriver (l'IA : mission_conseillee, [MISSION_CONSEILLEE: ...] ; le logiciel :
    // la suite annoncee une fois de trop, la proposition coupee) - pose dans Message::extra << missionConseillee >> ; une
    // reponse d'une mission (`missionCall`) : le marqueur est seulement retire du texte.
    void                                   adviceArrived(Conversation& c, Message& m, const std::string& stopReason, bool missionCall);
    [[nodiscard]] std::string              missionPrompt(const mission::Mission& m, const MissionCall& call, const hmi::Project* project) const;
    bool                                   acceptPlan(const std::string& id, const std::string& input, std::string* why, int* count);
    void                                   missionAnswered(const MissionCall& call, Conversation& c, int message, bool planned,
                                                           const std::string& stopReason);
    [[nodiscard]] std::string              callBlocker(const mission::Mission& m) const;
    void                                   endMissionCall(const MissionCall& call, bool interrupted, const std::string& reason);
    bool                                   missionApplied(const Proposal& prop, const plan::Plan& pl, const plan::Report* rep);
    void                                   missionDropped(std::string_view proposalId, std::string_view state, const std::string& reason);
    [[nodiscard]] bool                     missionMayApply(std::string_view proposalId, std::string* why) const;
    void                                   settleStale(mission::Mission& m);
    // 1.22.4 : NE PAS TOURNER EN ROND (AiLoop.hpp) - une proposition de conversation arrivee (ses empreintes, le cercle
    // eventuel ; `errors` nul : pas encore verifiee), et retenue ou non ; la consigne attendue apres Modifier la demande.
    void                                   loopArrived(Conversation& c, int message, const std::vector<std::string>* errors);
    [[nodiscard]] bool                     loopMayApply(const Proposal& p, std::string* why) const;
    bool                                   steerMission(const std::string& text, std::string* why);
    bool                                   continueMissionWith(std::string_view id, const std::string& consigne, std::string* why);
    struct Steer {
        std::string              mission, conversation, prefix;
        std::vector<std::string> steps;
    };
    std::optional<Steer>                   steer_;
    void                                   missionsTick();
    void                                   pollAudits();
    bool                                   relaunchMission(const Message& request, std::string* why);
    // 1.21.3 : les longueurs de reponse que les services ont dites (fournisseur|adresse|modele -> jetons), pour la
    // seance ; la reprise en parties a demander au prochain startLive (la longueur qui a coupe ; 0 : aucune).
    std::map<std::string, int>             outputCaps_;
    int                                    splitFrom_{0};
    [[nodiscard]] std::string              capKey() const;
    std::string                            noTools_;
    std::string                            modelsProvider_;
    // 1.22.6 : L'ACTIVITE ET LE BILAN (AiActivity.hpp) - le suivi de la reponse qui commence (son contexte : la mission, la
    // correction ; `contextGuess` : env. les jetons envoyes) ; a sa fin (finie, en erreur, arretee), son bilan garde avec le
    // message et ses etapes gardees pour la ligne qui suit (la verification, Generer et Compiler de sa proposition).
    void                                   startActivity(long long contextGuess);
    void                                   keepBill(Message& m, bool proposed);
    activity::Tracker                      lastAct_;
    std::string                            lastActConv_;
    int                                    lastActMsg_{-1};
};

[[nodiscard]] std::string_view phaseLabel(Assistant::Phase) noexcept;   // "En attente", "R\xC3\xA9" "flexion"...

} // namespace app::ai
