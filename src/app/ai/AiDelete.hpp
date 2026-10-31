// =============================================================================
//  app/ai/AiDelete.hpp - 1.21.0 : LA SUPPRESSION INTELLIGENTE de l'Assistant IA
//                        (le planificateur des suppressions)
// -----------------------------------------------------------------------------
//  << Supprime le type T_Pompe. >> Avant de toucher a quoi que ce soit, le
//  GRAPHE DES RELATIONS du projet (hmi::rel : les donnees reelles, jamais une
//  dependance inventee) dit :
//    ce qui PART          l'element demande et ce qu'il contient (les objets
//                         d'une vue, les popups d'un symbole, les methodes d'un
//                         type) ;
//    les DEPENDANCES      directes (qui l'utilise : une variable de ce type, un
//                         bouton qui ouvre cette popup), indirectes (de proche en
//                         proche : l'alarme qui surveille cette variable, la vue
//                         qui montre l'objet qui la lit), potentielles (un texte
//                         qui le NOMME sans que le logiciel le lise : a verifier) ;
//    ce qui est PARTAGE   ce que l'element emploie et qui sert aussi ailleurs :
//                         garde ;
//    les ORPHELINS        ce qui ne servirait plus a rien apres (rel::orphans,
//                         avant / apres la suppression, sur une copie).
//  Le resume compte par genre, en francais : "4 variables directement
//  concernees", "8 alarmes dependantes", "2 vues contenant des objets associes".
//
//  LES STRATEGIES - jamais toutes les dependances supprimees d'office :
//    seul        l'element seul, s'il ne casse rien ; sinon REFUSE, et la raison
//                dit ce qui en depend et quoi choisir ;
//    remplacer   ce qui le cite cite un AUTRE element du meme genre, qui EXISTE
//                (jamais un nom invente) : le passage de Renommer
//                (rewriteHmiReferences, renameTypeEverywhere...) ;
//    references  il part, avec SEULEMENT les references qui le citent (une
//                propriete d'objet videe, une action retiree, les instances d'un
//                symbole, une variable deliee de l'equipement) ; une reference
//                qui ne se retire pas sans casser (du code, une condition
//                d'alarme, une variable de ce type) : refuse, et dit laquelle ;
//    + inutiles  en plus, ce qui devient inutile : une popup que plus rien
//                n'ouvre, un script sur appel que plus rien n'appelle, une
//                fonction, une ressource - JAMAIS une definition (type, symbole),
//                une variable ni un equipement (signales, gardes).
//  LA DEFINITION N'EST PAS L'INSTANCE : supprimer une instance de symbole ne
//  touche ni le symbole ni ses autres instances ; supprimer une vue ne supprime
//  pas un script general qu'elle appelle ; supprimer un objet garde sa variable ;
//  supprimer un type employe : refuse, ou remplace. Les EXCLUSIONS (exclure) :
//  jamais supprimees ni modifiees.
//
//  Tout se fait sur une COPIE, puis se VERIFIE (l'element absent, AUCUNE
//  REFERENCE RESTANTE - le graphe d'apres, sur des noeuds temoins du nom
//  supprime -, la verification globale hmi::audit : aucune erreur bloquante
//  nouvelle qui le cite) ; rien a moitie. AiOperations en fait UNE commande
//  (hmi::changeProject) : un seul Ctrl+Z reprend tout.
//
//  API Studio : les suppressions de l'automate (project/DeleteCommands.hpp :
//  RemoveEntityCommand, collateralDamage, refusalReason) ne sont pas a la portee
//  de l'Assistant dans cette version.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "../../hmi/HmiCheck.hpp"       // NameExists
#include "../../hmi/HmiRelations.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai::del {

// ---- les genres -----------------------------------------------------------------
//  Object : des objets d'une vue (comme avant la 1.21.0) ; Instance : des instances de symbole (des objets aussi -
//  jamais la definition) ; Template : un ecran modele, un en-tete, un pied de page.
enum class Genre : std::uint8_t { Object, Instance, View, Popup, Symbol, Template, Variable, Type, Alarm, Script, Function, Equipment,
                                  Resource, Folder };
// "objet", "instance", "vue", "popup", "symbole", "modele", "variable", "type", "alarme", "script", "fonction", "equipement",
// "ressource", "dossier" (le JSON de l'outil).
[[nodiscard]] std::string_view genreKey(Genre) noexcept;
// Un genre lu (sans casse, quelques synonymes : "objets", "ecran_modele", "entete"...) ; vide : Object. Nul : inconnu.
[[nodiscard]] std::optional<Genre> genreFromKey(std::string_view) noexcept;
// "la variable", "le type", "l'instance"... (UTF-8).
[[nodiscard]] std::string genreNoun(Genre);
// Le genre vise des objets d'une vue (vide, "objet", "instance") : l'operation reste sur sa vue.
[[nodiscard]] bool objectGenre(std::string_view key) noexcept;
// Une operation supprimer d'elements du projet (pas des objets d'une vue).
[[nodiscard]] bool projectRemoval(const Operation&) noexcept;

// ---- les strategies ---------------------------------------------------------------
enum class Strategy : std::uint8_t { Only, Replace, References };
// "seul", "remplacer", "references".
[[nodiscard]] std::string_view strategyKey(Strategy) noexcept;
// Lue sans casse ; "inutiles" : seul (+ inutiles, lu par requestOf). Nul : inconnue.
[[nodiscard]] std::optional<Strategy> strategyFromKey(std::string_view) noexcept;

// ---- la demande ---------------------------------------------------------------------
struct Request {
    Genre                    genre{Genre::Object};
    std::vector<std::string> names;        // les elements (une vue, un type... ; un dossier : son chemin)
    std::string              view;         // Object, Instance : leur vue (son nom)
    std::string              list;         // Folder : la liste ("vues", "scripts"..., "variables") ; vide : la seule qui l'a
    Strategy                 strategy{Strategy::Only};
    bool                     useless{false};   // supprimer aussi ce qui devient inutile
    std::string              replacement;      // Replace : un element EXISTANT du meme genre
    std::vector<std::string> keep;             // les exclusions (jamais supprimees ni modifiees)
};
// La demande d'une operation supprimer : genre, objets, vue (Operation::target, sinon `openView`), proprietes
// (strategie, remplacer_par, inutiles, liste) et exclusions. Faux et `why` : un genre ou une strategie inconnus,
// remplacer sans remplacant, une strategie qui ne s'applique pas a des objets.
bool requestOf(const Operation&, Request&, std::string* why = nullptr, std::string_view openView = {});

// ---- l'analyse ------------------------------------------------------------------------
struct Item {
    hmi::rel::Kind kind{hmi::rel::Kind::Variable};
    std::string    name;                  // le nom du noeud du graphe ("Pompe_1", "Vue_A.OnOpen", un objet : son nom)
    hmi::Id        id{hmi::kNoId};
    hmi::Id        view{hmi::kNoId};      // un objet, un script ou une fonction de vue : sa vue
    std::string    viewName;              // ... son nom
    std::string    detail;                // ce qu'il est : "variable Pompe_1 (T_Pompe, liee a Station)"
    std::string    why;                   // pourquoi il est la : "demand\xC3\xA9", "contenu par Vue_A", ou il cite l'element
};

// Un objet que la strategie modifie (une propriete videe, une action retiree) ou retire (une instance) : l'apercu le liste.
struct ObjectEdit {
    hmi::Id             view{hmi::kNoId}, object{hmi::kNoId};
    std::string         viewName, name;
    hmi::Kind           kind{hmi::Kind::Rectangle};
    bool                removed{false};
    std::vector<Change> changes;          // cle : avant -> apres ("action 2" : "Ouvrir Pop_Pompe" -> "")
};

struct Analysis {
    std::string       title;              // "Suppression du type T_Pompe"
    Strategy          strategy{Strategy::Only};
    bool              useless{false};
    std::string       replacement;
    std::vector<Item> targets;            // les elements demandes, trouves
    std::vector<Item> removed;            // TOUT ce qui part : les demandes, leur contenu, les devenus inutiles
    std::vector<Item> direct;             // ce qui les utilise directement (hors de ce qui part)
    std::vector<Item> indirect;           // de proche en proche
    std::vector<Item> potential;          // par le nom seulement (a verifier)
    std::vector<Item> shared;             // ce qu'ils emploient et qui sert aussi ailleurs : garde
    std::vector<Item> orphans;            // ce qui deviendrait orphelin et reste (une definition, une variable, exclu)
    std::vector<Item> equipments;         // les equipements des variables concernees
    std::vector<Item> moved;              // un dossier supprime : ce qui remonte d'un cran
    std::vector<std::string> fixes;       // ce que la strategie change ailleurs, une phrase chacun
    std::vector<ObjectEdit>  objects;     // les objets modifies ou retires par la strategie
    std::vector<std::string> errors, warnings;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    // Le resume par categorie et par genre : "4 variables directement concern\xC3\xA9" "es", "8 alarmes d\xC3\xA9pendantes"...
    [[nodiscard]] std::vector<std::string> summary() const;
    // Le tout en texte (le titre, le resume, le detail) : ce que l'Assistant dit avant de supprimer.
    [[nodiscard]] std::string text(std::size_t maxPerList = 8) const;
};

// Les elements qu'une operation supprimer nomme (le genre et le nom ; un objet : sa vue), tels qu'ecrits - ce que les
// autres suppressions de la meme proposition doivent savoir parti (Context::also). Vide : rien de lisible.
[[nodiscard]] std::vector<Item> namedItems(const Operation&, std::string_view openView = {});
// Un element nomme (namedItems) existe-t-il dans le projet (par son genre et son nom) ?
[[nodiscard]] bool exists(const hmi::Project&, const Item& named);

struct Context {
    hmi::NameExists   plcName;            // les noms de l'automate (la verification globale) ; nul : aucun
    std::vector<Item> also;               // ce que les autres suppressions de la proposition emportent (ne bloque pas)
    bool              global{true};       // la verification globale (hmi::audit) a la fin
};

// LA SUPPRESSION sur `project` (une copie de travail, ou le projet d'une commande) : l'analyse, la strategie, le
// retrait, les devenus inutiles, la verification. Vrai : `project` est le projet d'apres ; faux : `project` est
// INCHANGE et `out.errors` dit pourquoi. `out` : l'analyse (dans les deux cas).
bool run(hmi::Project& project, const Request&, Analysis& out, const Context& = {});
// L'analyse seule (sur une copie : le projet ne change pas) - << si je supprime T_Pompe, que se passe-t-il ? >>.
[[nodiscard]] Analysis analyze(const hmi::Project&, const Request&, const Context& = {});

// ---- la verification --------------------------------------------------------------------
// Les references qui restent dans `after` vers ce qui est parti (`ignore` : celles qui viennent de ces elements, qui
// partent aussi) : une phrase chacune ("Vue_A . Btn_P2 . action 1 (Ouvrir une popup) : objet Btn_P2 ouvre popup Pop_Pompe").
[[nodiscard]] std::vector<std::string> remaining(const hmi::Project& after, const std::vector<Item>& removed,
                                                 const std::vector<Item>& ignore = {});
// LA VERIFICATION D'APRES : chaque element parti est absent, aucune reference ne reste (remaining), et la verification
// globale (hmi::audit::run) ne trouve aucune erreur bloquante nouvelle qui cite un element parti ou `touched` (les
// elements dont la strategie a change une reference). Vide : tout va bien. `global` faux : sans la verification globale
// (deja faite a la validation du meme projet - l'application ne la refait pas deux fois).
[[nodiscard]] std::vector<std::string> verify(const hmi::Project& before, const hmi::Project& after, const std::vector<Item>& removed,
                                              const std::vector<std::string>& touched = {}, const hmi::NameExists& plcName = {},
                                              bool global = true);
// Apres Appliquer (le compte rendu, AiPlan : report) : l'operation supprimer `op` a-t-elle reellement eu lieu dans
// `after` - ses elements absents, aucune reference restante ? Faux et `note` : ce qui reste.
[[nodiscard]] bool confirmRemoved(const hmi::Project& after, const Operation& op, std::string_view openView, std::string* note = nullptr);

// ---- l'apercu, la confirmation -------------------------------------------------------------
// L'analyse sous la forme de l'apercu (Check::Removal).
[[nodiscard]] Check::Removal removalOf(std::size_t op, const Analysis&, const std::vector<std::string>& keep = {});
// LA CONFIRMATION D'UNE SUPPRESSION (le panneau la demande des que Check::confirmRemoval) : le titre, le texte, la liste
// (nom, genre et pourquoi), le bouton. Les objets seuls : comme avant ("Supprimer 3 objets").
struct Confirmation {
    std::string title, text, confirm;
    std::vector<std::pair<std::string, std::string>> items;
};
[[nodiscard]] Confirmation confirmation(const Check&, const Proposal&);

} // namespace app::ai::del
