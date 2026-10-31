// =============================================================================
//  app/ai/AiOperations.hpp - l'Assistant IA : les operations qu'il propose
// -----------------------------------------------------------------------------
//  CE QUE L'IA PEUT DEMANDER, ET RIEN D'AUTRE. Une proposition est une liste
//  d'operations sur la vue active, ecrites avec les VRAIES structures du modele
//  (l'audit, section 2.2) :
//
//    creer      un genre de kKindInfos ("Tank"), un nom voulu, des proprietes -
//               seulement des cles que hmi::makeObject donne a ce genre ;
//    modifier   un objet existant (par son nom), des proprietes ;
//    associer   un objet, une variable - qui DOIT exister (variables IHM, noms
//               de l'automate relie) : sinon une erreur, et c'est l'utilisateur
//               qui choisit une variable existante ou de la creer ;
//    aligner    des objets, un bord (hmi::edit::align) ;
//    distribuer des objets, un sens (hmi::edit::distribute) ;
//    supprimer  des objets (hmi::edit::remove) - plusieurs : une confirmation ;
//    dupliquer  (1.15.0) des objets (avec leurs enfants), un decalage, les noms
//               voulus des copies - comme Ctrl+D (hmi::edit::duplicate) ;
//    code       (1.15.0) le CODE BRUT d'objets (le format du fichier de vue :
//               hmi::parseObjectCode, puis hmi::edit::pasteCode, comme Ctrl+V d'un
//               code) - pour un objet riche (ses actions, son contenu). Chaque
//               propriete y est verifiee comme pour creer ; une action qui porte
//               du code (executer un script, appeler, un calcul Maths) ou une
//               fonction redefinie est refusee.
//
//    1.16.0 (le contrat ; le chantier OPTIONS les remplit) :
//    creer_vue       une vue neuve (son nom, sa taille, son fond) ; les operations
//                    qui suivent dans la meme proposition visent cette vue ;
//    creer_variables des variables de l'IHM (nom, type, description, valeur
//                    initiale) - montrees dans l'apercu, jamais en silence ;
//    creer_type      un type IHM (une structure : ses membres, nom et type) ;
//    dupliquer       + `count` copies et `markers` : les reperes $...$ avancent comme
//                    Ctrl+Maj+D (hmi : la duplication avec reperes) ;
//    creer, modifier + une valeur qui commence par "=" est une FORMULE (fx), lue et
//                    verifiee comme une formule de l'inspecteur.
//
//    1.18.0 (LES OUTILS DE L'IA, a la fin) :
//    creer_vue + role   symbole, popup, modele (ecran modele), entete, pied - avec
//                       ses parametres (nom, type, defaut) et ses generiques <T, K> ;
//                       creer_symbole, creer_popup, creer_modele_vue : les memes ;
//    creer_type + generiques  une structure generique (T_Pile<T>, T_Paire<T, K>) ;
//    creer_equipement   Modbus TCP/IP ou Ethernet TCP/IP ; `detecter` PROPOSE la
//                       detection de ce qui repond (l'utilisateur la lance, rien
//                       ne part sur le reseau a la proposition) ;
//    adresser           une variable IHM liee a un equipement, son adresse (%MW100),
//                       une formule de lien pour un tableau de structures
//                       ([*].Retour = %MW(1000 + 10 * i), HmiAddressPlan) ;
//    creer_script, creer_fonction  du texte de projet (ST, C, C++) : compile par le
//                       compilateur du logiciel pour les diagnostics, JAMAIS execute ;
//    creer_alarme       condition, message, priorite... (la condition compilee) ;
//    creer_ressource    depuis une piece jointe de la conversation ;
//    creer_dossier, ranger  les dossiers des listes (vues, scripts, types...).
//
//  DEPLACER, REDIMENSIONNER : c'est modifier x, y, w, h (le geste natif
//  hmi::edit::setBox : un groupe garde ses enfants a leur place relative).
//
//  Jamais de code execute : une operation inconnue est refusee a la lecture.
//  Jamais une variable inventee : la propriete `variable` d'un creer ou d'un
//  modifier suit la meme regle qu'associer ; une formule (un defaut, un retour
//  d'etat) ne lit que des noms qui existent.
//
//  VALIDER (validate) ne touche a rien : les comptes, les objets, les
//  proprietes avant -> apres, les variables, les avertissements et les erreurs.
//  APPLIQUER (makeCommand) fait tout dans UNE commande (hmi::changeView, ou
//  hmi::changeProject quand une variable est creee) : un seul Ctrl+Z.
//  L'APERCU (preview) fait la meme chose sur une COPIE de la vue : rien ne
//  bouge dans le projet tant que l'utilisateur n'a pas dit Appliquer.
//
//  L'EMPREINTE d'une vue (fingerprint) est gardee avec la proposition : une
//  proposition d'une ancienne conversation ne s'applique jamais sur une vue qui
//  a change depuis.
// =============================================================================
#pragma once

#include "AiJson.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai {

struct Operation {
    // 1.16.0 (le contrat, a la fin) : View (creer_vue), Variables (creer_variables), Type (creer_type).
    // 1.18.0 (a la fin) : Equipment (creer_equipement), Address (adresser), Script, Function, Alarm, Resource,
    // Folder (creer_dossier), Store (ranger).
    enum class Action : std::uint8_t { Create, Modify, Bind, Align, Distribute, Remove, Duplicate, Code, View, Variables, Type,
                                       Equipment, Address, Script, Function, Alarm, Resource, Folder, Store };
    Action                                           action{Action::Create};
    std::string                                      kind;      // creer : la cle du genre ("Tank")
    std::string                                      name;      // creer : le nom voulu ; modifier, associer : l'objet
    std::vector<std::string>                         objects;   // aligner, distribuer, supprimer
    std::vector<std::pair<std::string, std::string>> props;     // creer, modifier : cle -> valeur
    std::string                                      variable;  // associer
    std::string                                      edge;      // aligner : haut, bas, gauche, droite, centre-h, centre-v ;
                                                                // distribuer : horizontal, vertical
    // 1.15.0 (a la fin) : dupliquer - les noms voulus des copies (dans l'ordre de `objects` ; vide
    // ou pris : un nom libre), le decalage ; code - le code brut des objets a poser.
    std::vector<std::string>                         names;
    double                                           dx{20}, dy{20};
    std::string                                      code;
    // 1.16.0 (le contrat, a la fin) :
    //  - creer_vue : `name` (le nom de la vue), `props` (largeur, hauteur, fond, description) ;
    //  - creer_variables : `variables` ; creer_type : `name` (le nom du type) et `members` ;
    //  - dupliquer : `count` copies (1 : une), `markers` (les reperes $...$ avancent d'une copie a l'autre).
    struct VariableSpec {
        std::string name, type, description, initial;
        bool operator==(const VariableSpec&) const = default;
    };
    struct MemberSpec {
        std::string name, type, description;
        bool operator==(const MemberSpec&) const = default;
    };
    std::vector<VariableSpec>                        variables;
    std::vector<MemberSpec>                          members;
    int                                              count{1};
    bool                                             markers{false};
    // 1.18.0 (a la fin) : LES OUTILS DE L'IA -
    //  - creer_type, creer_vue (un symbole, une popup), creer_fonction : `generics` ("T", "K : ANY_NUM") ;
    //  - creer_vue (un symbole, une popup) : `params` (nom, type, description, valeur : la valeur par defaut) ;
    //  - creer_equipement : `detect` - PROPOSER la detection de ce qui repond (l'utilisateur la lance) ;
    //  - les autres champs (equipement, adresse, langage, corps, condition, liste, dossier...) : dans `props`.
    std::vector<std::string>                         generics;
    std::vector<VariableSpec>                        params;
    bool                                             detect{false};
    // 1.19.0 (a la fin) : les objets (creer, modifier, associer...) - la vue visee, par son nom : une vue creee dans
    // la meme proposition (creer_vue) ou une vue du projet. Vide : la derniere creer_vue ecrite avant l'operation,
    // sinon la vue ouverte (comme en 1.18.0). Le JSON : "vue".
    std::string                                      target;
    bool operator==(const Operation&) const = default;
};

// 1.18.0 : l'operation ne touche que le projet (pas la vue ouverte) : creer_variables, creer_type, creer_vue et les outils.
[[nodiscard]] bool projectLevel(Operation::Action) noexcept;
[[nodiscard]] std::string_view actionKey(Operation::Action) noexcept;          // "creer"...
[[nodiscard]] std::optional<Operation::Action> actionFromKey(std::string_view) noexcept;

// Ce que l'utilisateur a choisi pour une variable introuvable (une operation associer).
struct VariableChoice {
    std::string use;              // une variable existante, a la place
    bool        create{false};    // creer la variable demandee (dans les variables de l'IHM)
    std::string type{"BOOL"};
    bool operator==(const VariableChoice&) const = default;
};

struct Proposal {
    std::string            id;                   // "op1"
    std::string            state{"proposee"};    // proposee | appliquee | refusee
    std::string            view;                 // le nom de la vue visee
    hmi::Id                viewId{hmi::kNoId};
    std::string            fingerprint;          // l'empreinte de la vue a la proposition
    // 1.14.2 : l'empreinte de la vue juste APRES Appliquer (vide : pas encore appliquee, ou un
    // fichier d'avant). La carte compare la vue du moment aux deux : appliquee, annulee (Ctrl+Z,
    // ou le projet pas enregistre), ou la vue a change depuis.
    std::string            fingerprintAfter;
    std::string            request;              // la demande (Modifier la demande la remet dans la saisie)
    std::string            summary;              // le libelle de la commande ("Synoptique de station de pompage")
    std::vector<Operation> ops;
    VariableChoice         variable;
    int                    create{0}, modify{0}, remove{0};   // les comptes (le contrat), refaits par validate
    json::Value            extra;                // les champs inconnus, gardes
    // 1.19.0 (a la fin) : LE PLAN - les operations retirees par l'utilisateur dans l'apercu (leurs indices : elles ne
    // s'appliquent pas, celles qui en dependent non plus) ; le plan tel qu'il a ete applique (ses etapes et leurs etats,
    // AiPlan : toJson) et le COMPTE RENDU reel (AiPlan : reportToJson) - gardes avec la conversation.
    std::vector<std::size_t> excluded;
    json::Value              plan;
    json::Value              report;
};

// ---- valider -------------------------------------------------------------------
struct Change {
    std::string key, before, after;
};
struct Item {
    enum class What : std::uint8_t { Create, Modify, Remove };
    What                what{What::Create};
    hmi::Id             id{hmi::kNoId};   // un objet existant (modifier, supprimer)
    std::string         name;
    hmi::Kind           kind{hmi::Kind::Rectangle};
    std::vector<Change> changes;          // creer : les proprietes donnees ; modifier : avant -> apres
    std::string         variable;         // la variable associee (vide : aucune)
    std::string         view;             // 1.19.0 : la vue de l'objet (une proposition peut en toucher plusieurs)
};
struct Check {
    std::vector<Item>        items;
    std::vector<std::string> warnings, errors;
    std::vector<std::string> bindings;          // "Pompe_1 \xE2\x86\x92 PUMP_01_RUN"
    int                      create{0}, modify{0}, remove{0};
    std::string              missingVariable;   // une variable citee qui n'existe pas
    std::vector<std::string> nearVariables;     // les noms existants les plus proches
    bool                     stale{false};      // la vue a change depuis la proposition
    bool                     viewMissing{false};
    bool                     confirmRemoval{false};   // plusieurs suppressions : demander avant
    // 1.16.0 (a la fin) : ce que la proposition cree en plus des objets, tel que le logiciel le fera - la
    // vue neuve (son nom ; vide : aucune), les variables IHM (le type remis en forme, la valeur initiale
    // lue, la description posee) et les types IHM (nom, membres). L'apercu les liste, la carte les compte.
    std::string                                                             newView;
    std::vector<Operation::VariableSpec>                                    newVariables;
    std::vector<std::pair<std::string, std::vector<Operation::MemberSpec>>> newTypes;
    // 1.18.0 (a la fin) : les autres elements crees ou touches par les outils - une ligne de l'apercu chacun ("Equipement",
    // "Centrale_1", "Modbus TCP/IP 192.168.1.40:502, esclave 1") ; les detections PROPOSEES (l'equipement, la plage).
    struct Made {
        std::string what, name, detail;
    };
    std::vector<Made>                                   made;
    std::vector<std::pair<std::string, std::string>>    detections;
    // 1.19.0 (a la fin) : LE PLAN - ce qui existe deja a l'identique et sert tel quel (un type de memes membres, une
    // variable de meme type, un equipement de memes reglages) ; par operation (l'indice de Proposal::ops) : son etat a la
    // validation, ses erreurs et avertissements, pourquoi elle est ignoree ; l'ordre d'execution (AiPlan : schedule) et
    // les dependances ; les erreurs du plan (une dependance circulaire).
    std::vector<Made>                                   reused;
    struct Outcome {
        enum class State : std::uint8_t { ToDo, Validated, Failed, Skipped, Excluded };
        State                    state{State::ToDo};
        std::vector<std::string> errors, warnings;
        std::string              note;
        std::vector<Made>        made, reused;
    };
    std::vector<Outcome>                                outcomes;
    std::vector<std::size_t>                            order;
    std::vector<std::vector<std::size_t>>               deps;
    std::vector<std::string>                            planErrors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Ce que l'appelant sait en plus de l'IHM : les noms de l'automate relie (vide : aucun).
struct Env {
    std::function<bool(std::string_view)> plcName;
    // 1.15.0 (a la fin) : les variables de l'automate relie, pour le contexte envoye au modele (nom tel
    // qu'une propriete l'ecrit, type) ; nul : aucune liste (plcName seul dit si un nom existe).
    std::function<std::vector<std::pair<std::string, std::string>>()> plcVariables;
    // 1.16.0 (le contrat, a la fin) : les types (DDT) de l'automate relie - leur nom, leurs membres (nom,
    // type, commentaire) ; nul : aucun. Le chantier OPTIONS le branche (AiWorkspace) et s'en sert
    // (AiAutomate.hpp : des types IHM proposes).
    struct PlcType {
        std::string                           name, comment;
        std::vector<Operation::MemberSpec>    members;
    };
    std::function<std::vector<PlcType>()> plcTypes;
    // 1.16.0 (a la fin, chantier OPTIONS) : le commentaire d'une globale de l'automate (son nom, sans "API.") ;
    // vide : aucun. AiAutomate en fait la description d'une variable IHM proposee. Nul : aucun commentaire.
    std::function<std::string(std::string_view)> plcComment;
    // 1.16.0 (captures, a la fin) : l'automate vient d'un FICHIER lu (le .XPG joint a la conversation, IHM Studio) -
    // son nom ("MAST.XPG") ; vide ou nul : l'automate relie (ou aucun). Ses variables servent a creer des variables
    // IHM, elles ne s'associent pas directement.
    std::function<std::string()> plcSource;
    // 1.18.0 (a la fin) : une piece jointe de la conversation, par son nom de fichier ("logo.png") - son chemin sur le disque ;
    // vide (ou nul) : aucune. creer_ressource n'importe que celles-la.
    std::function<std::string(std::string_view)> attachmentPath;
};

[[nodiscard]] std::string fingerprint(const hmi::View&);
// 1.16.0 : LA PORTEE D'UNE PROPOSITION.
//  - createsView : elle commence par creer_vue (une seule, en premier) : ses operations visent la vue neuve,
//    jamais la vue ouverte ;
//  - usesOpenView : sans creer_vue, elle touche des objets (creer, modifier...) : la vue ouverte a la demande ;
//  - ni l'un ni l'autre : elle ne cree que des variables ou des types IHM (le projet seul, aucune vue).
[[nodiscard]] bool createsView(const Proposal&) noexcept;
[[nodiscard]] bool usesOpenView(const Proposal&) noexcept;
// Le nom de la vue neuve (celui de creer_vue), vide sans creer_vue. Proposal::view peut dire la vue ouverte a la
// demande (l'IA integree) : la vue neuve se lit ici.
[[nodiscard]] std::string newViewName(const Proposal&);
// 1.19.0 : la vue visee par une operation sur des objets (son nom ; vide : la vue ouverte) - Operation::target, sinon la
// derniere creer_vue ecrite avant elle. Vide aussi pour une operation du projet.
[[nodiscard]] std::string opTarget(const Proposal&, std::size_t index);
// 1.16.0 : L'EMPREINTE DE CE QUE LA PROPOSITION TOUCHE, telle qu'elle est dans le projet : la vue visee
// (fingerprint, comme en 1.15.0 : une proposition qui ne cree que des objets garde la meme empreinte) ;
// la vue neuve d'un creer_vue (absente, ou son empreinte) ; + l'etat des variables et des types qu'elle cree
// (absents, ou leur definition). Gardee a la proposition (Proposal::fingerprint) et juste apres Appliquer
// (fingerprintAfter) : la carte dit perimee, appliquee, annulee (Ctrl+Z) ou changee depuis.
// `viewPrint` : l'empreinte de la vue ouverte a prendre (celle de l'envoi) a la place de celle du moment.
[[nodiscard]] std::string proposalPrint(const hmi::Project&, const Proposal&);
[[nodiscard]] std::string proposalPrint(const hmi::Project&, const Proposal&, std::string_view viewPrint);
// Ce que la proposition cree en plus des objets, en une ligne : "la vue Vue_Pompes, 12 variables IHM, 2 types
// IHM" ; vide : rien de plus.
[[nodiscard]] std::string creationSummary(const Proposal&);
// La vue neuve d'un creer_vue (son nom, sa taille, son fond, sa description ; un premier calque), comme
// "Nouvelle vue" de l'arbre : les identifiants viennent de `nextId`.
[[nodiscard]] hmi::View newViewFor(const hmi::Project&, const Operation&, hmi::Id& nextId);
[[nodiscard]] bool        variableExists(const hmi::Project&, std::string_view name, const Env& = {});
// Les variables de l'IHM les plus proches d'un nom (les memes lettres, les memes mots).
[[nodiscard]] std::vector<std::string> nearVariables(const hmi::Project&, std::string_view name, std::size_t max = 4);
// La vue visee : par son identifiant, sinon par son nom (nul : plus la).
[[nodiscard]] const hmi::View* targetView(const hmi::Project&, const Proposal&);
[[nodiscard]] Check validate(const hmi::Project&, const Proposal&, const Env& = {});
// 1.18.0 : les detections PROPOSEES par une proposition (un equipement cree avec `detecter`) :
// le nom de l'equipement et la plage a parcourir ("192.168.1.0/24" ; vide : son adresse n'est
// pas une IPv4). La carte appliquee offre de les lancer - c'est l'utilisateur qui clique.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> proposedDetections(const Proposal&);

// ---- appliquer -------------------------------------------------------------------
// Les operations sur `view` (une vue de `project`, ou sa copie) ; les identifiants neufs
// viennent de `nextId`. `created`, `modified`, `removed` : les objets touches. Faux et `why`
// si une operation ne peut pas se faire (rien n'est garanti sur la vue : travailler sur une copie).
// `skipRemovals` : l'apercu garde les objets a supprimer (ils se dessinent barres).
struct Touched {
    std::vector<hmi::Id> created, modified, removed;
};
// 1.15.0 : `env` (a la fin) - les noms de l'automate relie (sans lui, une variable de l'automate est introuvable).
bool applyOps(const hmi::Project& project, hmi::View& view, hmi::Id& nextId, const Proposal&, Touched* touched = nullptr,
              std::string* why = nullptr, bool skipRemovals = false, const Env* env = nullptr, bool lenient = false);
// La commande d'une proposition validee : UN pas d'annulation. nullptr et `why` : rien a faire.
// 1.15.0 : `env` (a la fin), celui de validate - sans lui, une association a une variable de l'automate est refusee.
[[nodiscard]] core::CommandPtr makeCommand(const hmi::DocumentPtr&, const Proposal&, std::string* why = nullptr, const Env& env = {});
// 1.16.1 : APPLIQUER MALGRE LES ERREURS (l'option des parametres, apres une confirmation) - ce qui est valide
// s'applique, ce qui est en erreur est laisse de cote, comme l'apercu le montre ; `skipped` recoit les erreurs
// (l'appelant les note au journal). Une vue qui a change depuis la proposition, ou qui n'existe plus, bloque
// toujours.
[[nodiscard]] core::CommandPtr makeCommandLenient(const hmi::DocumentPtr&, const Proposal&, std::vector<std::string>& skipped,
                                                  std::string* why = nullptr, const Env& env = {});

// L'apercu : la copie de la vue, modifiee (les objets a supprimer y sont encore).
struct Preview {
    bool        ok{false};
    std::string why;
    hmi::View   view;
    Touched     touched;
};
[[nodiscard]] Preview preview(const hmi::Project&, const Proposal&, const Env& env = {});

// ---- le JSON (le bloc "operations", et une proposition entiere) -----------------
[[nodiscard]] json::Value operationsToJson(const std::vector<Operation>&);
// Faux et `why` : une operation inconnue (refusee), ou un champ illisible.
bool operationsFromJson(const json::Value& array, std::vector<Operation>& out, std::string* why = nullptr);
[[nodiscard]] json::Value toJson(const Proposal&);
[[nodiscard]] Proposal    proposalFromJson(const json::Value&);

} // namespace app::ai
