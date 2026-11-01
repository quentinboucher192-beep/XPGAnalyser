// =============================================================================
//  app/ai/AiMission.hpp - 1.22.0 : LES MISSIONS IA PERSISTANTES (le modele)
// -----------------------------------------------------------------------------
//  Une demande complexe (<< construis une station de pompage complete avec deux
//  pompes, une cuve, les equipements Modbus, les variables, les alarmes, les
//  scripts et les vues >>) devient une MISSION : un objet de l'application,
//  garde dans IA/missions/<id>.json, independant de la conversation qui l'a
//  lancee. Elle sait ce qui est PREVU (son plan : des etapes ordonnees par leurs
//  dependances), ce qui est REELLEMENT FAIT (les propositions appliquees, leur
//  compte rendu lu dans le projet - AiPlan.hpp : report), ce qui RESTE, et dans
//  quel ETAT se trouve le projet (son empreinte, AiMissionStore.hpp).
//
//  Ce fichier est le modele et ses regles, sans disque ni interface ni reseau :
//    - les etats d'une etape et leurs transitions controlees (canMove) ;
//    - le plan : lu de l'outil planifier_mission de l'IA (planFromTool), ses
//      dependances completees par les regles de categories (une alarme lit des
//      variables, une vue montre des variables...), sans cycle, ordonne par ses
//      dependances (JAMAIS l'ordre dans lequel le modele l'a ecrit) ;
//    - le prochain lot (nextBatch) : les etapes pretes, regroupees en une
//      proposition a la mesure de la longueur d'une reponse (1.21.3) ;
//    - le suivi : chaque proposition liee a ses etapes, son resultat REEL
//      (onApplied : le compte rendu du projet d'avant / d'apres), la verification
//      des elements crees dans le projet du moment (verifyAgainst) ;
//    - la progression, calculee et explicable (progressOf) ;
//    - 1.22.1 : ce que fait CHAQUE ETAPE - sa progression reelle (stepProgressOf), son
//      activite du moment en une ligne (activityOf), son journal (stepJournal) ;
//    - 1.22.2 : la fin forcee par l'utilisateur (forceFinish : les etapes non terminees ignorees, rien de defait) ;
//    - 1.22.4 : ne pas tourner en rond (AiLoop.hpp : les empreintes des propositions, les tentatives par etape, une
//      proposition retenue tant que l'utilisateur n'a pas choisi, les tentatives deja faites dites a l'IA) ;
//    - le rapport final (finalReport) et les textes donnes a l'IA (contextes).
//
//  Le projet reel est toujours la source de verite : une etape n'est << terminee >>
//  que si ce qu'elle a cree est dans le projet ; une proposition << appliquee >>
//  seulement si le compte rendu reel le dit - jamais parce que l'IA l'a annonce.
// =============================================================================
#pragma once

#include "AiJson.hpp"
#include "AiPlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { struct Project; }
namespace hmi::audit { struct Report; }

namespace app::ai::mission {

inline constexpr int kFormat = 1;   // le format de IA/missions/<id>.json

// ---- les etats -----------------------------------------------------------------------------------------------
enum class StepState : std::uint8_t {
    ToPlan,               // a planifier : le plan n'est pas encore fait
    Waiting,              // en attente : une dependance n'est pas terminee
    Ready,                // prete : ses dependances sont terminees (ou ignorees)
    Running,              // en cours : une proposition se prepare (l'IA ecrit)
    AwaitingValidation,   // en attente de validation : une proposition attend Previsualiser / Appliquer
    Applied,              // appliquee : la proposition est appliquee, la verification suit
    Verifying,            // verification en cours (l'audit de l'etape)
    Done,                 // terminee : ce qu'elle cree est dans le projet
    Error,                // en erreur : l'erreur exacte dans Step::error
    Blocked,              // bloquee : une dependance est en erreur, bloquee ou annulee
    Skipped,              // ignoree (par l'utilisateur)
    Cancelled,            // annulee (la mission, ou l'etape retiree du plan)
};
[[nodiscard]] std::string_view stepLabel(StepState) noexcept;   // "\xC3\x80 planifier", "Pr\xC3\xAA" "te"...
[[nodiscard]] std::string_view stepKey(StepState) noexcept;     // "a_planifier", "prete"... (le fichier)
[[nodiscard]] StepState        stepFromKey(std::string_view) noexcept;
// Les transitions permises (les autres sont refusees, et le refus est dit). Exemple : Waiting -> Ready seulement par
// refreshReadiness (les dependances terminees) ; Done -> Ready seulement si le projet a perdu ce qu'elle avait cree.
[[nodiscard]] bool canMove(StepState from, StepState to) noexcept;
// Une etape finie pour le calcul (terminee, ignoree) ; une etape qui bloque ses dependantes (erreur, bloquee, annulee).
[[nodiscard]] bool isFinished(StepState) noexcept;
[[nodiscard]] bool isBlocking(StepState) noexcept;

enum class State : std::uint8_t {
    Planning,      // le plan se prepare (l'IA analyse le projet)
    Active,        // en cours
    Paused,        // en pause : aucune generation, aucune application
    Blocked,       // plus rien de realisable : il faut une decision (une erreur, un projet introuvable)
    Done,          // terminee : toutes les etapes obligatoires terminees, l'audit final sans erreur bloquante
    Cancelled,     // annulee : aucune proposition de la mission ne s'applique plus
};
[[nodiscard]] std::string_view stateLabel(State) noexcept;   // "Planification", "En cours", "En pause"...
[[nodiscard]] std::string_view stateKey(State) noexcept;     // "planification", "en_cours"...
[[nodiscard]] State            stateFromKey(std::string_view) noexcept;

// La coherence du projet avec la derniere sauvegarde de la mission (AiMissionStore.hpp : la reprise).
enum class Coherence : std::uint8_t { Unknown, Same, Changed, Missing };
[[nodiscard]] std::string_view coherenceLabel(Coherence) noexcept;
[[nodiscard]] std::string_view coherenceKey(Coherence) noexcept;
[[nodiscard]] Coherence        coherenceFromKey(std::string_view) noexcept;

// ---- le contenu ---------------------------------------------------------------------------------------------------
// Les categories d'etape (les regles de dependances implicites en decoulent : implicitDeps).
//   analyse, equipements, types, variables, vues, objets, symboles, popups, alarmes, scripts, fonctions, ressources,
//   verification, audit, rapport, autre.
[[nodiscard]] const std::vector<std::string_view>& categories();
[[nodiscard]] std::string_view categoryLabel(std::string_view key);   // "\xC3\x89quipements"...

struct Step {
    std::string              id;          // "S1", "S2"... stable (une etape ajoutee prend le numero suivant)
    std::string              title;       // "Cr\xC3\xA9" "er les \xC3\xA9quipements Modbus"
    std::string              detail;      // ce qu'elle doit faire, en une ou deux phrases
    std::string              category{"autre"};
    std::vector<std::string> deps;        // les etapes dont elle depend (ecrites par l'IA + implicites)
    std::vector<std::string> implicit;    // celles ajoutees par les regles de categories (dites, pas inventees)
    StepState                state{StepState::Waiting};
    bool                     mandatory{true};
    int                      plannedOps{0};   // l'estimation de l'IA (le poids de l'etape dans la progression)
    // Ce qu'elle doit produire, quand l'IA le sait : "variable:Pompe_1_Marche", "vue:Vue_Station" (genre:nom).
    std::vector<std::string> expected;
    std::vector<std::string> proposals;   // les propositions qui la couvrent (Proposal::id), dans l'ordre
    std::string              note;        // pourquoi cet etat ("d\xC3\xA9pend de S3, en erreur")
    std::string              error;       // la derniere erreur exacte
    std::string              updated;     // ISO
    json::Value              extra;
    // 1.22.4 (a la fin) : NE PAS TOURNER EN ROND (AiLoop.hpp) - ses propositions depuis qu'elle n'est plus terminee (ou
    // depuis toujours) : une de plus a chaque proposition qui la couvre (onGenerated), zero quand elle devient terminee
    // ou quand l'utilisateur donne une consigne pour elle (Modifier la demande : loop::choose).
    // Gardees dans << cercles >> a la fin du fichier (absentes : 0).
    int                      attempts{0};
};

// L'analyse prealable (planifier_mission : ce qui existe deja et ce qui manque) - les textes de l'IA, rapproches du
// projet par verifyAgainst (un element dit << conforme >> qui n'existe pas est signale).
struct Analysis {
    std::vector<std::string> conform;      // "variable:Pompe_1_Marche - d\xC3\xA9j\xC3\xA0 conforme"
    std::vector<std::string> toModify, toComplete, toCreate, impossible;
    std::string              summary;
};

// Une operation d'une proposition, telle qu'elle a ete suivie (le plan de la proposition, AiPlan.hpp).
struct OpLine {
    std::string id;         // l'etape d'operations "E3"
    std::string step;       // l'etape de mission qu'elle sert ("S2" ; vide : toutes celles de la proposition)
    std::string title;      // "Cr\xC3\xA9" "er la variable Pompe_1_Marche"
    std::string verb;       // "creation", "modification", "reutilisation", "suppression", "disposition"
    std::string state;      // plan::stateKey : a_faire, validee, appliquee, echouee, ignoree, annulee
    std::vector<std::string> creates;   // "genre|nom|vue" (plan::Ref) - ce qu'elle cree, verifie dans le projet
    std::string error;
};

struct ProposalLink {
    std::string              id;             // Proposal::id ("op...")
    int                      order{0};       // P1, P2...
    std::string              conversation;   // la conversation et le message qui la portent
    std::string              message;        // "m7"
    std::vector<std::string> steps;          // les etapes de mission qu'elle couvre
    std::string              title;          // Proposal::summary
    // generee, validee, appliquee, refusee, echouee, obsolete (le projet a change), interrompue (l'appel coupe),
    // annulee (la mission annulee, ou Ctrl+Z apres coup).
    std::string              state{"generee"};
    int                      ops{0}, create{0}, modify{0}, reuse{0}, remove{0};
    int                      applied{0}, failed{0}, skipped{0};
    std::string              created, appliedAt;   // ISO
    std::vector<std::string> errors;               // validation, application (exactes)
    std::vector<OpLine>      lines;                // les operations et leur etat
    json::Value              report;               // plan::reportToJson (le compte rendu reel)
    std::string              next;                 // Proposal::next (la suite dite par l'IA)
    // 1.22.4 (a la fin) : NE PAS TOURNER EN ROND (AiLoop.hpp) - gardes dans << cercles >> a la fin du fichier (absents :
    // vides ; une proposition d'avant 1.22.4 n'a pas d'empreinte et ne se compare pas).
    std::string              opsPrint;             // loop::opsPrint (a l'arrivee, onGenerated)
    std::string              errorsPrint;          // loop::errorsPrint de ses erreurs de validation (onChecked ; vide : aucune)
    int                      errorCount{0};
    std::string              errorNames;           // loop::errorNames ("Vanne.Cmd, Vanne.Etat")
    std::string              projectPrint;         // l'empreinte du projet quand elle a ete demandee (ProjectLink::print)
    std::string              loop;                 // LE CERCLE VU, en clair (vide : aucun) - loop::flag
    std::vector<std::string> loopSigns;            // ses signes (loop::signKey)
    std::vector<std::string> loopSteps;            // les etapes en cause
    std::string              loopChoice;           // le choix de l'utilisateur (vide : il n'a pas encore choisi) - loop::choose
    // Retenue : un cercle vu, pas encore de choix, et elle attend toujours une decision (a valider, verifiee).
    [[nodiscard]] bool held() const noexcept { return !loop.empty() && loopChoice.empty() && (state == "generee" || state == "validee"); }
};

struct Event {
    std::string date;     // ISO
    std::string kind;     // creation, analyse, plan, plan_modifie, proposition, validation, application, etape, erreur,
                          // decision, pause, reprise, annulation, interruption, actualisation, audit, terminee, archive
    std::string text;
    std::string step, proposal;
};

struct Decision {
    std::string date, question, answer;
};

// Un appel a l'IA en cours (planification, proposition, correction, actualisation) : ecrit AVANT l'appel, efface
// apres. Retrouve au demarrage : l'appel a ete interrompu (rien n'est rejoue sans verification).
struct Pending {
    bool                     active{false};
    std::string              purpose;        // "plan", "proposition", "correction", "actualisation"
    std::string              conversation;
    std::string              started;        // ISO
    std::vector<std::string> steps;
};

// 1.22.2 : LA FIN FORCEE par l'utilisateur (forceFinish) : quand, pourquoi, les etapes qu'elle a ignorees et les
// propositions qu'elle a refusees (jamais appliquees). Gardee dans le fichier (<< fin_forcee >>) ; vide : pas de fin forcee.
struct Forced {
    std::string              date;        // ISO ; vide : pas de fin forcee
    std::string              reason;      // "fin forc\xC3\xA9" "e par l'utilisateur"
    std::vector<std::string> steps;       // les etapes passees << ignoree (fin forcee) >>
    std::vector<std::string> proposals;   // les propositions en attente refusees par elle
    [[nodiscard]] bool on() const noexcept { return !date.empty(); }
};

// L'empreinte du projet a la derniere sauvegarde (AiMissionStore.hpp : printOf) - la reprise compare.
struct ProjectLink {
    std::string id, name, path;   // memory::identifyProject ; le chemin du dossier du projet
    std::string print;            // l'empreinte de tout le projet (vide : pas encore prise)
    std::vector<std::pair<std::string, std::string>> files;   // fichier du projet -> empreinte (le detail du changement)
    std::string printed;          // ISO
};

struct Mission {
    int                       format{kFormat};
    std::string               id;            // "mission-..." stable
    std::string               name;          // "Station de pompage"
    std::string               description;
    std::string               request;       // la demande initiale EXACTE
    std::string               created, modified;
    ProjectLink               project;
    std::string               conversation;  // celle qui l'a lancee
    std::vector<std::string>  conversations; // toutes celles qui y ont travaille
    std::string               provider, model;
    State                     state{State::Planning};
    bool                      archived{false};
    int                       planRevision{0};
    std::vector<Step>         steps;         // DANS L'ORDRE D'EXECUTION (orderSteps)
    Analysis                  analysis;
    std::vector<ProposalLink> proposals;
    std::vector<Event>        history;
    std::vector<Decision>     decisions;
    Pending                   pending;
    Coherence                 coherence{Coherence::Unknown};
    std::string               coherenceText;
    std::string               resumePoint;   // le dernier point de reprise, en clair
    std::string               summary;       // le resume a jour (summaryOf)
    std::vector<std::string>  attachments;   // les pieces jointes de la conversation (Attachment::id, jamais copiees)
    json::Value               finalReport;   // reportToJson(FinalReport) ; nul : pas d'audit final
    Forced                    forced;        // 1.22.2 : la fin forcee (forceFinish) ; vide : aucune
    json::Value               extra;         // les champs inconnus (un format plus recent), gardes

    [[nodiscard]] const Step*         step(std::string_view id) const noexcept;
    [[nodiscard]] Step*               step(std::string_view id) noexcept;
    [[nodiscard]] const ProposalLink* proposal(std::string_view id) const noexcept;
    [[nodiscard]] ProposalLink*       proposal(std::string_view id) noexcept;
};

// ---- creer, journaliser ---------------------------------------------------------------------------------------------
// Une mission neuve (etat Planning, aucune etape), son identifiant neuf, la demande exacte, l'evenement << creation >>.
[[nodiscard]] Mission make(std::string request, std::string conversation, std::string now);
// Un evenement de plus (et Mission::modified) ; borne a kMaxHistory (les plus anciens partent, le premier reste).
void log(Mission&, std::string kind, std::string text, std::string now, std::string step = {}, std::string proposal = {});
inline constexpr std::size_t kMaxHistory = 400;
// Une demande simple ou complexe ? Une heuristique sans reseau (plusieurs categories, << complet >>, plusieurs elements,
// une longue demande) : vrai -> le panneau propose << Creer une mission IA ? >>. Jamais pour une question.
[[nodiscard]] bool looksComplex(std::string_view request);

// ---- le plan --------------------------------------------------------------------------------------------------------
// L'outil planifier_mission (le JSON de l'IA) -> le nom, la description, l'analyse et les etapes de la mission, les
// dependances implicites ajoutees, ordonnees. Faux et `why` : illisible, une etape sans titre, une dependance vers
// une etape inconnue, un cycle (dit : "S3 -> S5 -> S3"). La mission n'est pas touchee si c'est faux.
bool planFromTool(Mission&, std::string_view inputJson, std::string now, std::string* why = nullptr);
[[nodiscard]] std::string planToolJson();   // la definition de l'outil (format Anthropic)
inline constexpr std::string_view kPlanTool = "planifier_mission";
// Les dependances implicites d'une etape (les regles de categories) parmi les etapes donnees.
[[nodiscard]] std::vector<std::string> implicitDeps(const Step&, const std::vector<Step>&);
// L'ordre d'execution (tri topologique stable : a egalite, l'ordre ecrit). Faux et `cycle` : une dependance circulaire.
bool orderSteps(std::vector<Step>&, std::string* cycle = nullptr);
// Les etats d'attente recalcules : Waiting -> Ready (dependances finies), Ready / Waiting -> Blocked (une dependance
// bloquante, la note le dit), Blocked -> Waiting / Ready (la cause levee). Ne touche ni aux etapes en cours ni aux finies.
void refreshReadiness(Mission&);

// MODIFIER LE PLAN (l'utilisateur, ou l'IA a sa demande) - jamais tout reinitialiser : les etapes terminees et les
// operations appliquees sont gardees ; l'historique dit ce qui a change (planRevision + 1).
struct PlanEdit {
    enum class Kind : std::uint8_t { Add, Remove, Skip, Unskip, Rename, SetDeps, SetMandatory };
    Kind                     kind{Kind::Add};
    std::string              step;        // l'etape visee (Add : vide, un identifiant est donne)
    std::string              title, detail, category;
    std::vector<std::string> deps;
    bool                     mandatory{true};
};
// Faux et `why` : une etape terminee ne se retire pas (son travail est dans le projet : << ignorer >> la suite seulement),
// une dependance inconnue, un cycle. `added` : l'identifiant d'une etape ajoutee.
bool editPlan(Mission&, const PlanEdit&, std::string now, std::string* why = nullptr, std::string* added = nullptr);

// ---- le prochain lot --------------------------------------------------------------------------------------------------
// Les etapes a couvrir par la prochaine proposition : les pretes, puis celles qui ne dependent que d'etapes finies ou
// deja dans le lot, tant que le poids estime (plannedOps, au moins 1) tient dans `maxOps`. Vide : rien de realisable
// (`why` le dit : tout est fini, bloque, en attente de validation, la mission en pause...). 1.22.4 : vide aussi tant
// qu'une proposition de la mission est retenue (elle tourne en rond, AiLoop.hpp) - l'utilisateur choisit d'abord.
struct Batch {
    std::vector<std::string> steps;
    int                      ops{0};
    std::vector<std::string> blocked;   // les etapes bloquees et pourquoi ("S5 : d\xC3\xA9pend de S3 (en erreur)")
    std::string              why;
};
[[nodiscard]] Batch nextBatch(const Mission&, int maxOps);
// Les etapes independantes (aucun chemin de dependance entre elles) parmi celles donnees.
[[nodiscard]] bool independent(const Mission&, std::string_view a, std::string_view b);

// ---- le suivi -----------------------------------------------------------------------------------------------------------
// Changer l'etat d'une etape (transition controlee). Faux et `why` : transition refusee (l'etat ne change pas).
bool setStepState(Mission&, std::string_view step, StepState to, std::string now, std::string note = {}, std::string* why = nullptr);
// Une proposition de la mission vient d'etre generee (son message), pour ces etapes : P<n>, les etapes en attente de
// validation. L'etape de chaque operation : Operation::step ("etape" dans l'outil ; vide : toutes celles du lot).
void onGenerated(Mission&, const Proposal&, const std::string& conversation, const std::string& message,
                 const std::vector<std::string>& steps, std::string now);
// Sa verification (validate) : validee, ou ses erreurs exactes (les etapes restent en attente de validation).
void onChecked(Mission&, std::string_view proposal, const Check&, std::string now);
// APPLIQUEE (le plan et le compte rendu REELS, AiPlan.hpp : report) : chaque operation et son etat ; chaque etape
// couverte -> terminee si toutes ses operations sont appliquees et ce qu'elles creent est dans le projet d'apres,
// en erreur (l'erreur exacte) si l'une a echoue, prete si rien ne l'a servie. Puis refreshReadiness.
void onApplied(Mission&, std::string_view proposal, const plan::Plan&, const plan::Report&, const hmi::Project& after, std::string now);
// Refusee, ou devenue obsolete (le projet a change), ou interrompue (l'appel coupe) : ses etapes redeviennent pretes.
void onDropped(Mission&, std::string_view proposal, std::string_view state, std::string reason, std::string now);
// LE PROJET DU MOMENT : ce que les propositions appliquees ont cree y est-il toujours ? Une etape terminee dont un
// element a disparu (Ctrl+Z, une suppression a la main) redevient prete, et c'est dit ; une proposition annulee par
// Ctrl+Z passe << annulee >>. Rend vrai si quelque chose a change.
bool verifyAgainst(Mission&, const hmi::Project&, std::string now);
// Un element "genre|nom|vue" (ou "genre:nom") est-il dans le projet ?
[[nodiscard]] bool exists(const hmi::Project&, std::string_view ref);

// L'etat global recalcule (apres chaque evenement) : Done si toutes les etapes obligatoires sont terminees et l'audit
// final fait sans erreur bloquante ; Blocked si rien n'est realisable et qu'il reste du travail ; sinon Active (sauf
// Paused, Cancelled, Planning, gardes).
void refreshState(Mission&, std::string now);
// Pause / reprise / annulation / archivage (l'etat, l'historique ; l'annulation passe les etapes non finies a annulee).
bool pause(Mission&, std::string now, std::string* why = nullptr);
bool resume(Mission&, std::string now, std::string* why = nullptr);
bool cancel(Mission&, std::string now, std::string* why = nullptr);
void archive(Mission&, bool on, std::string now);
// Une proposition de cette mission peut-elle encore s'appliquer ? Faux et `why` : la mission en pause, annulee, ou la
// proposition n'est plus la bonne (obsolete, annulee) ; 1.22.4 : retenue (elle tourne en rond, pas encore de choix).
[[nodiscard]] bool mayApply(const Mission&, std::string_view proposal, std::string* why = nullptr);

// ---- 1.22.2 : FORCER LA FIN D'UNE MISSION -----------------------------------------------------------------------------
//  L'utilisateur arrete la mission la ou elle en est, SANS RIEN DEFAIRE dans le projet :
//    - un appel a l'IA en cours est efface (l'Assistant l'arrete d'abord, comme Pause et Annuler) ;
//    - chaque proposition qui attend une decision (a valider, verifiee) passe << refusee >> : elle ne s'appliquera pas ;
//    - chaque etape non terminee (ni terminee, ni ignoree, ni annulee) passe << ignoree >>, la note << Fin forcee par
//      l'utilisateur. >> - par les transitions de canMove, AUCUNE N'EST AJOUTEE : une etape en cours, a valider,
//      appliquee ou en verification passe d'abord par << prete >> (comme quand sa proposition est refusee ou son appel
//      arrete), puis << ignoree >> ; les autres (a planifier, en attente, prete, en erreur, bloquee) directement ;
//    - la mission passe << terminee >> (l'etat terminal le plus juste : << annulee >> dirait que plus rien ne compte, alors
//      que ce qui est fait reste fait et verifie) ; Mission::forced le retient : refreshState ne la rouvre plus,
//      mayApply refuse ses propositions, editPlan son plan ; l'historique le dit (<< Fin forcee par l'utilisateur :
//      N etapes ignorees >>) ; un ancien rapport final est retire (l'audit final le refait : finalReport dit << fin forcee >>
//      et les etapes ignorees). La progression ne compte plus les etapes ignorees (progressOf, comme toujours).
// Faux et `why` : la mission est deja terminee ou annulee (rien n'est change).
inline constexpr std::string_view kForcedReason = "fin forc\xC3\xA9" "e par l'utilisateur";
[[nodiscard]] bool mayForceFinish(const Mission&, std::string* why = nullptr);
// Les etapes que la fin forcee ignorerait (ni terminees, ni ignorees, ni annulees), et les propositions qu'elle refuserait
// (a valider, verifiees) : leurs identifiants, dans l'ordre de la mission.
[[nodiscard]] std::vector<std::string> unfinishedSteps(const Mission&);
[[nodiscard]] std::vector<std::string> awaitingProposals(const Mission&);
// `reason` : vide -> kForcedReason.
bool forceFinish(Mission&, std::string now, std::string reason = {}, std::string* why = nullptr);
// L'etat en clair : "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)" apres une fin forcee, sinon stateLabel ; celui d'une etape :
// "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)" pour une etape ignoree par elle, sinon stepLabel.
[[nodiscard]] std::string stateText(const Mission&);
[[nodiscard]] std::string stepText(const Mission&, const Step&);

// ---- la progression ---------------------------------------------------------------------------------------------------
//  LA REGLE (deterministe, dite telle quelle a l'ecran) : chaque etape pese son estimation d'operations (au moins 1) ;
//  les etapes ignorees ou annulees sortent du calcul. Planification : 100 % des que le plan existe. Generation : le
//  poids des etapes qui ont une proposition (ou plus loin). Validation : celles dont la proposition est validee ou
//  appliquee. Application : celles appliquees ou terminees. Globale : celles TERMINEES (verifiees dans le projet).
struct Progress {
    int    steps{0}, done{0}, running{0}, awaiting{0}, waiting{0}, ready{0}, errors{0}, blocked{0}, skipped{0};
    double planning{0}, generation{0}, validation{0}, application{0}, global{0};   // 0..1
    [[nodiscard]] int percent() const noexcept;   // global, arrondi
    [[nodiscard]] std::string text() const;       // "3 / 9 \xC3\xA9tapes termin\xC3\xA9" "es \xC2\xB7 1 en cours \xC2\xB7 5 en attente \xC2\xB7 38 %"
};
[[nodiscard]] Progress progressOf(const Mission&);
[[nodiscard]] std::string progressRule();   // la regle, en clair (l'infobulle) - 1.22.1 : avec la regle par etape

// ---- 1.22.1 : CE QUE FAIT UNE ETAPE (sa progression, son activite du moment, son journal) ---------------------------
//  LA REGLE PAR ETAPE (deterministe, dite telle quelle a l'ecran : stepProgressRule) : ses operations APPLIQUEES (le
//  compte rendu reel, dans une proposition appliquee) sur ses operations PREVUES - celles de ses propositions a valider,
//  appliquees ou en echec (pas les refusees, obsoletes, interrompues ni annulees ; sans les operations retirees ou
//  reprises par Ctrl+Z ; une operation en echec ne compte plus quand une proposition plus recente sert l'etape : la
//  correction la reprend). Aucune operation proposee encore : l'estimation du plan (plannedOps, au moins 1). Terminee
//  (verifiee dans le projet) : 100 % ; sinon jamais plus de 99 %, arrondi a l'inferieur. Ignoree, annulee : pas de
//  pourcentage (l'etat le dit).
struct StepProgress {
    int  applied{0};       // ses operations appliquees
    int  planned{1};       // ses operations prevues (au moins 1)
    int  failed{0};        // en echec, pas reprises par une proposition plus recente
    int  toValidate{0};    // dans une proposition qui attend Previsualiser / Appliquer
    bool estimated{true};  // `planned` est l'estimation du plan (aucune operation proposee encore)
    int  percent{0};       // 0..100 ; -1 : pas de pourcentage (ignoree, annulee)
    [[nodiscard]] bool        counted() const noexcept { return percent >= 0; }
    [[nodiscard]] double      frac() const noexcept;   // 0..1 (0 : pas de pourcentage)
    [[nodiscard]] std::string text() const;            // "40 %" ; "\xE2\x80\x94" sans pourcentage
    [[nodiscard]] std::string detail() const;          // "2 / 5 op\xC3\xA9rations appliqu\xC3\xA9" "es \xC2\xB7 1 en \xC3\xA9" "chec"
};
[[nodiscard]] StepProgress stepProgressOf(const Mission&, const Step&);
[[nodiscard]] std::string  stepProgressRule();

// CE QUE LE LOGICIEL SAIT DU MOMENT, en plus de la mission (l'Assistant le donne ; rien de cela n'est enregistre) : l'IA
// repond-elle maintenant a l'appel de Mission::pending (sinon, un appel en attente est un appel interrompu), depuis
// combien de secondes ; une proposition de la mission s'applique-t-elle sur un fil de fond ; l'audit final tourne-t-il.
struct Live {
    bool        calling{false};
    long long   elapsed{-1};       // secondes depuis Pending::started (-1 : inconnu)
    std::string applying;          // Proposal::id appliquee sur un fil de fond (vide : aucune)
    int         applyPercent{-1};  // son avancement 0..100 (-1 : inconnu)
    bool        auditing{false};
    // 1.22.5 (a la fin) : L'AVIS DU LOGICIEL (AiBuild.hpp) - la proposition de la mission que Generer et Compiler verifient sur
    // une copie du projet (Proposal::id ; vide : aucune), et ce que la correction automatique en cours corrige
    // ("Compiler : 2 erreurs" ; vide : aucune).
    std::string building;
    std::string correcting;
};
// L'ACTIVITE D'UNE ETAPE EN UNE LIGNE (ce qu'elle fait maintenant), deduite de son etat, de ses dependances, de ses
// propositions et de leurs verifications, et du moment : << L'IA prepare la proposition P2... (depuis 0:42) >>,
// << P2 : 12 operations a valider - Previsualiser puis Appliquer >>, << Application de P2... 45 % >>, << Attend S1
// (dependance) >>, << Bloquee : depend de S3 (en erreur) >>, << En erreur : ... >>, << Terminee - 8 operations
// appliquees le 01/11 a 12:04 >>...
enum class Activity : std::uint8_t { Idle, Working, ToValidate, Waiting, Blocked, Error, Done, Off };
struct StepActivity {
    Activity    kind{Activity::Idle};
    std::string text;
    std::string proposal;   // la proposition en jeu (Proposal::id ; vide : aucune)
    bool        live{false};   // l'IA ou une application travaille pour elle maintenant
};
[[nodiscard]] StepActivity activityOf(const Mission&, const Step&, const Live& = {});
// L'etape << du moment >> : celle pour qui l'IA ou une application travaille, sinon celle qui attend une validation, puis
// appliquee, en cours, en erreur, prete, bloquee, en attente (a egalite : l'ordre du plan). Nul : tout est fini.
[[nodiscard]] const Step* currentStep(const Mission&, const Live& = {});
// L'activite de la mission quand aucune etape ne la porte : le plan ou l'actualisation que l'IA prepare, l'audit final, un
// appel interrompu. Vide : rien de tel.
[[nodiscard]] std::string missionActivity(const Mission&, const Live& = {});
[[nodiscard]] std::string elapsedText(long long seconds);   // "0:42", "12:05", "1:02:05"

// LE JOURNAL D'UNE ETAPE, le plus ancien d'abord : les evenements de l'historique qui la concernent (son identifiant,
// une proposition qui la sert, un texte qui la cite), et apres chaque application ses operations - appliquees (leur
// libelle lisible, << \xE2\x9C\x93 Cr\xC3\xA9" "er la variable Pompe_1_Marche >>) ou en echec (l'erreur exacte) ; les decisions qui la citent.
// Une proposition dont l'historique (borne) a perdu la trace est reprise de ce que la mission en garde. Rien d'invente.
struct JournalLine {
    std::string date;       // ISO (vide : inconnue)
    std::string kind;       // un genre d'Event ; "operation" (appliquee), "echec" (une operation en echec), "decision"
    std::string text;
    std::string proposal;   // Proposal::id (vide : aucune)
};
[[nodiscard]] std::vector<JournalLine> stepJournal(const Mission&, const Step&);

// ---- les textes ------------------------------------------------------------------------------------------------------
// Le resume a jour (Mission::summary) : l'etat, la progression, ce qui est fait, la prochaine action.
[[nodiscard]] std::string summaryOf(const Mission&);
// La prochaine action, en clair ("Proposer la suite : S4 Cr\xC3\xA9" "er les alarmes", "Corriger S3 : ...").
[[nodiscard]] std::string nextAction(const Mission&);
// LES CONSIGNES DONNEES A L'IA :
//  - planningPrompt : analyser le projet reel (ce qui existe, a modifier, a completer, a creer, impossible), puis
//    appeler planifier_mission ; ne rien proposer encore ;
//  - batchPrompt : la mission, son plan et ses etats, CE QUI EST REELLEMENT DANS LE PROJET (les elements crees par
//    les propositions precedentes, verifies), ce qui a change depuis, les etapes a couvrir maintenant ; reutiliser les
//    noms exacts, ne rien recreer, une operation porte "etape", "suite" dit les etapes suivantes ;
//  - correctionPrompt : l'etape en erreur, l'erreur exacte, les operations touchees, les dependances ; une proposition
//    de correction (jamais une modification silencieuse d'une proposition appliquee) ;
//  - 1.22.4 : batchPrompt et correctionPrompt disent aussi ce qui a DEJA ETE PROPOSE pour ces etapes sans aboutir, et
//    pourquoi (loop::triedText, borne) : ne pas reproposer la meme chose, changer d'approche ou dire ce qui bloque ;
//  - overviewPrompt : les missions du projet (nom, etat, progression, prochaine action) en quelques lignes, pour toute
//    conversation (<< ou en est la station de pompage ? >>).
[[nodiscard]] std::string planningPrompt(const Mission&);
[[nodiscard]] std::string batchPrompt(const Mission&, const hmi::Project&, const Batch&, std::string_view changes);
[[nodiscard]] std::string correctionPrompt(const Mission&, const hmi::Project&, std::string_view step);
[[nodiscard]] std::string overviewPrompt(const std::vector<Mission>&, std::string_view projectId);
// La fiche complete d'une mission, en texte (l'outil etat_mission, le rapport) ; la liste des missions trouvees
// (chercher_missions) : par le nom, la demande, les etapes, les elements crees ; les plus pertinentes d'abord.
[[nodiscard]] std::string detailText(const Mission&);
[[nodiscard]] std::vector<std::size_t> search(const std::vector<Mission>&, std::string_view query, bool withArchived = false);

// ---- le rapport final -------------------------------------------------------------------------------------------------
struct FinalReport {
    std::vector<std::string> created, modified, reused;   // "genre nom" verifies dans le projet du moment
    std::vector<std::string> finished;                    // "S2 Cr\xC3\xA9" "er les variables"
    std::vector<std::string> notDone;                     // les etapes non terminees, l'analyse << impossible >>
    std::vector<std::string> errors;                      // les erreurs rencontrees et leur etat
    std::vector<std::string> toCheck;                     // a verifier par un humain, et ce qui n'a pas pu etre verifie
    int                      auditErrors{0}, auditWarnings{0};
    bool                     auditRun{false};             // l'audit global du projet a-t-il pu etre fait ?
    bool                     complete{false};             // toutes les etapes obligatoires terminees, sans erreur bloquante
    std::string              date, verdict;
    // 1.22.2 : une FIN FORCEE par l'utilisateur (Mission::forced) - le verdict le dit, et les etapes qu'elle a ignorees
    // ("S3 Cr\xC3\xA9" "er les alarmes") ; jamais << complete >> si elle en a ignore une.
    bool                     forced{false};
    std::vector<std::string> skipped;
};
// `audit` : le resultat de hmi::audit::run sur le projet du moment (nul : pas fait - << non verifiable >>, jamais
// << verifie >>).
[[nodiscard]] FinalReport finalReport(const Mission&, const hmi::Project&, const hmi::audit::Report* audit, std::string now);
[[nodiscard]] json::Value reportToJson(const FinalReport&);
[[nodiscard]] FinalReport reportFromJson(const json::Value&);
[[nodiscard]] std::string reportText(const FinalReport&);

// ---- les outils de recherche (pour n'importe quelle conversation, repondus sur le fil du fournisseur) -----------------
inline constexpr std::string_view kSearchTool = "chercher_missions";
inline constexpr std::string_view kDetailTool = "etat_mission";
[[nodiscard]] std::string searchToolJson();
[[nodiscard]] std::string detailToolJson();
// Les reponses (les missions relues du disque par l'appelant) : jamais une information inventee - << aucune mission ne
// correspond >> quand rien n'est trouve.
[[nodiscard]] std::string runSearchTool(const std::vector<Mission>&, std::string_view inputJson, std::string_view projectId);
[[nodiscard]] std::string runDetailTool(const std::vector<Mission>&, std::string_view inputJson);

// ---- le fichier -----------------------------------------------------------------------------------------------------
[[nodiscard]] json::Value toJson(const Mission&);
// `fromFormat` : le format lu (0 : un fichier sans format, migre). `readOnly` : un format plus recent que kFormat -
// lu (les champs inconnus gardes) mais jamais reecrit par cette version.
[[nodiscard]] Mission fromJson(const json::Value&, int* fromFormat = nullptr, bool* readOnly = nullptr);

} // namespace app::ai::mission
