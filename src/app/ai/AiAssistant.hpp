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

#include "AiAnthropic.hpp"
#include "AiDemo.hpp"
#include "AiLearning.hpp"
#include "AiPlan.hpp"   // 1.19.0 : le plan d'action et le compte rendu
#include "AiPrompt.hpp"   // 1.20.0 : IncidentSource
#include "AiAgentTools.hpp"
#include "AiAttachments.hpp"   // 1.21.0 : le contexte de la discussion
#include "AiFileModels.hpp"    // 1.21.0 : les classeurs joints, les modeles de fichiers
#include "AiMissionStore.hpp"   // 1.22.0 : les missions IA persistantes (et AiMission.hpp)
#include "AiProvider.hpp"
#include "AiStore.hpp"
#include "AiUsage.hpp"

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

private:
    Conversation* cur() noexcept;
    bool          applyWith(int message, std::string* why, bool ignoringErrors);   // 1.16.1
    bool          afterApply(Conversation& c, int message, plan::Plan pl, const std::optional<hmi::Project>& before,
                             const std::vector<std::string>& skipped);             // 1.20.1 : la suite (sur le fil de l'interface)
    void          pollApply();                                                       // 1.20.1
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
    bool                                   launchMission(mission::Mission& m, std::string text, MissionCall call, std::string* why);
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
};

[[nodiscard]] std::string_view phaseLabel(Assistant::Phase) noexcept;   // "En attente", "R\xC3\xA9" "flexion"...

} // namespace app::ai
