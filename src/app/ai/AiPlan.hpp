// =============================================================================
//  app/ai/AiPlan.hpp - 1.19.0 : le PLAN D'OPERATIONS de l'Assistant IA
// -----------------------------------------------------------------------------
//  Une demande complexe (<< une station de pompage avec deux pompes, leurs
//  equipements, les variables, les alarmes, un symbole, une popup et la vue >>)
//  devient une proposition de plusieurs dizaines d'operations (AiOperations.hpp).
//  Le PLAN les rend lisibles et sures, sans rien toucher au projet :
//
//    les etapes      une par operation : ce qu'elle cree, modifie ou reutilise,
//                    ce qu'elle reference, sa categorie (types et variables,
//                    equipements, adresses, scripts et fonctions, alarmes, vues
//                    et symboles, popups, ressources, dossiers), son titre ;
//    les dependances une etape qui lit un nom qu'une autre cree en depend (une
//                    alarme et sa variable, un symbole et son type, une vue et
//                    son symbole, un script et sa fonction) - calculees sur les
//                    noms reellement ecrits, jamais devinees ;
//    l'ordre         UNE UNITE est une operation du projet, ou une vue neuve et
//                    ses objets, ou les objets d'une vue existante ; les unites
//                    sont triees par leurs dependances (tri topologique) - JAMAIS
//                    l'ordre dans lequel le modele d'IA les a ecrites. Un cycle
//                    est une erreur bloquante, sauf entre du code (deux fonctions
//                    qui s'appellent : le code est compile une fois tout cree) ;
//    les etats       a faire, validee, appliquee, echouee, ignoree (une etape
//                    retiree, ou qui depend d'une etape echouee ou retiree),
//                    annulee (Ctrl+Z) - mis a jour par l'execution reelle ;
//    le compte rendu apres Appliquer : ce qui a ete cree, modifie, reutilise,
//                    ce qui reste a faire - LU DANS LE PROJET (avant / apres),
//                    jamais recopie de ce que l'IA a annonce.
//
//  Le plan affiche dans la conversation, l'apercu et le compte rendu viennent
//  tous de ces donnees : aucune divergence possible avec ce qui s'applique.
// =============================================================================
#pragma once

#include "AiOperations.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::plan {

enum class State : std::uint8_t { ToDo, Validated, Applied, Failed, Skipped, Cancelled };
// "a faire", "validee", "appliquee", "echouee", "ignoree", "annulee" (en francais, accents compris).
[[nodiscard]] std::string_view stateLabel(State) noexcept;
[[nodiscard]] std::string_view stateKey(State) noexcept;              // "a_faire"... (le fichier)
[[nodiscard]] State            stateFromKey(std::string_view) noexcept;

// Les categories de l'apercu, dans leur ordre d'affichage.
enum class Category : std::uint8_t { Types, Equipments, Addresses, Code, Alarms, Views, Popups, Resources, Folders, Objects };
[[nodiscard]] std::string_view categoryLabel(Category) noexcept;     // "Types et variables"...
[[nodiscard]] Category         categoryOf(const Operation&) noexcept;

enum class Verb : std::uint8_t { Create, Modify, Reuse, Remove, Arrange };
[[nodiscard]] std::string_view verbLabel(Verb) noexcept;              // "creation", "modification", "reutilisation"...

// Un element nomme du projet : ce qu'une etape cree, modifie ou lit.
struct Ref {
    std::string kind;      // "type", "variable", "equipement", "vue", "symbole", "popup", "modele", "entete", "pied",
                           // "script", "fonction", "alarme", "ressource", "dossier", "objet"
    std::string name;
    std::string view;      // un objet : sa vue (vide : la vue ouverte)
    bool        exists{false};   // deja dans le projet (une reference : reutilisee)
    bool operator==(const Ref&) const = default;
};

struct Step {
    std::string              id;          // "E1", "E2"... dans l'ordre d'execution
    std::size_t              op{0};       // l'indice dans Proposal::ops
    Operation::Action        action{Operation::Action::Create};
    Category                 category{Category::Objects};
    Verb                     verb{Verb::Create};
    std::string              title;       // "Creer le type T_Pompe" (accents compris)
    std::string              name;
    std::string              target;      // les objets : la vue visee (vide : la vue ouverte)
    std::vector<std::string> deps;        // les identifiants des etapes dont elle depend
    std::vector<Ref>         creates, modifies, uses;
    std::vector<std::string> checks;      // ce qui sera verifie ("les membres et leurs types"...)
    std::vector<std::string> errors, warnings;
    State                    state{State::ToDo};
    bool                     optional{false};   // rien n'en depend : la retirer ne casse rien
    bool                     excluded{false};   // retiree par l'utilisateur
    std::string              note;              // pourquoi cet etat ("depend de E3, echouee")
};

struct Plan {
    std::vector<Step>        steps;            // DANS L'ORDRE D'EXECUTION
    std::vector<std::string> errors;           // du plan lui-meme (une dependance circulaire)
    [[nodiscard]] const Step* byOp(std::size_t op) const noexcept;
    [[nodiscard]] Step*       byOp(std::size_t op) noexcept;
    [[nodiscard]] const Step* byId(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t count(State) const noexcept;
    // Une demande qui merite un plan affiche : au moins 3 etapes, ou plusieurs categories.
    [[nodiscard]] bool        worthShowing() const noexcept;
};

// ---- l'ordonnancement (sans le projet : seulement les noms ecrits) --------------------------------
// Une UNITE d'execution : une operation du projet ; une vue neuve (creer_vue) et les objets qui la visent ;
// les objets d'une vue existante (la vue ouverte : target vide). `ops` : les indices, dans l'ordre d'execution.
struct Unit {
    enum class Kind : std::uint8_t { Project, NewView, ExistingView };
    Kind                     kind{Kind::Project};
    std::string              target;      // NewView, ExistingView : la vue
    std::vector<std::size_t> ops;
};
struct Schedule {
    std::vector<Unit>                     units;      // dans l'ordre
    std::vector<std::vector<std::size_t>> deps;       // par operation : les operations dont elle depend
    std::vector<std::string>              errors;     // une dependance circulaire (bloquante)
    std::vector<std::size_t>              blocked;    // les operations prises dans ce cycle
    [[nodiscard]] std::vector<std::size_t> order() const;   // les operations, dans l'ordre d'execution
};
[[nodiscard]] Schedule schedule(const Proposal&);

// Ce qu'une operation cree, modifie et lit (les noms ecrits ; `project` dit ce qui existe deja).
void refsOf(const hmi::Project&, const Proposal&, std::size_t op, std::vector<Ref>& creates, std::vector<Ref>& modifies,
            std::vector<Ref>& uses);

// ---- le plan ---------------------------------------------------------------------------------------
// Le plan d'une proposition sur le projet du moment, ses etats tires de la validation (Check::outcomes,
// AiOperations : validate) : validee, echouee, ignoree. Ne touche a rien.
[[nodiscard]] Plan build(const hmi::Project&, const Proposal&, const Check&);
// La forme gardee avec la proposition (la conversation se rouvre avec son plan et ses etats).
[[nodiscard]] json::Value toJson(const Plan&);
[[nodiscard]] Plan        fromJson(const json::Value&);

// ---- le compte rendu ---------------------------------------------------------------------------------
struct Line {
    std::string category;                 // "Types", "Variables", "Equipements Modbus"...
    std::vector<std::string> names;
};
struct Report {
    std::vector<Line>        created, modified, reused;
    std::vector<std::string> todo;        // ce qui reste a faire (l'adresse IP definitive, la detection a lancer...)
    std::vector<std::string> failures;    // "E4 Creer l'alarme X : la raison" ; "E5 ... : non executee (depend de E4)"
    std::vector<std::string> external;    // les actions hors du projet (non annulables par Ctrl+Z) - aucune en general
    std::size_t planned{0}, applied{0}, failed{0}, skipped{0};
    bool        restored{false};          // une transaction refusee : le projet n'a pas change (confirme)
    std::string verdict;                  // "24 operations appliquees, aucune erreur bloquante."
    [[nodiscard]] std::size_t createdCount() const noexcept;
    [[nodiscard]] bool        empty() const noexcept { return created.empty() && modified.empty() && reused.empty(); }
};
// LE COMPTE RENDU REEL : la difference entre le projet d'avant et celui d'apres Appliquer ; `plan` (ses etats mis
// a jour ici : appliquee si ce qu'elle cree est bien dans `after`, echouee sinon) et la proposition (les detections
// proposees, les etapes ignorees).
[[nodiscard]] Report report(const hmi::Project& before, const hmi::Project& after, Plan& plan, const Proposal&);
[[nodiscard]] json::Value reportToJson(const Report&);
[[nodiscard]] Report      reportFromJson(const json::Value&);
// Le texte du compte rendu (markdown leger du panneau) : les sections Cree, Modifie, Reutilise, A faire, puis le verdict.
[[nodiscard]] std::string reportText(const Report&, std::string_view title);
// Le plan d'action en texte (une ligne par etape, numerotee), pour la reponse de l'IA integree et le journal.
[[nodiscard]] std::string planText(const Plan&, std::size_t maxLines = 40);

} // namespace app::ai::plan
