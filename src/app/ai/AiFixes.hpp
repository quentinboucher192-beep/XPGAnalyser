// =============================================================================
//  app/ai/AiFixes.hpp - 1.22.1 : CORRIGER UNE PROPOSITION DEPUIS SES ERREURS
// -----------------------------------------------------------------------------
//  Le clic droit sur une erreur (ou un avertissement) de l'apercu et de la carte
//  de la conversation ouvre le menu du clic droit de l'appli (ui::PopupMenu)
//  avec des actions ADAPTEES A L'ERREUR, lues dans ses defauts structures
//  (Check::faults) - jamais dans son texte :
//    une variable introuvable    << Utiliser << Y >> >> (les plus proches, 5 au plus),
//                                << Choisir une variable... >> (le choix de l'apercu),
//                                << Creer la variable << X >> >> (un type a choisir,
//                                le conseille d'abord ; un membre << Vanne.Cmd >> se
//                                cree << Vanne_Cmd >>), << Retirer les operations qui
//                                l'utilisent >> ;
//    une erreur d'une operation  << Retirer cette operation >> (Proposal::excluded :
//                                ce qui en depend suit, comme la bande du plan) ;
//    toujours                    << Copier le message >>, << Demander a l'IA de
//                                corriger >> (le texte prepare dans la saisie : rien
//                                ne part seul).
//
//  LA PROPOSITION CHANGE, JAMAIS LE PROJET (Assistant::chooseVariable, clearChoice,
//  excludeOps) : elle est gardee avec la conversation et se revalide par son chemin
//  habituel - l'erreur disparait, la categorie << Corrections >> dit ce qui a ete
//  fait (<< 7 variables remplacees >>) et chaque correction se reprend (le clic
//  droit : << Revenir >>). Appliquer applique ensuite la proposition corrigee. Une
//  variable << creee >> l'est parce que l'utilisateur l'a demande : l'Assistant
//  n'invente toujours aucune variable.
//
//  LES DOUBLONS : deux erreurs identiques (deux operations sur la meme variable
//  introuvable) se lisent sur une seule ligne << ... (2 operations) >>.
// =============================================================================
#pragma once

#include "AiOperations.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class PopupMenu; }

namespace app::ai {

class Assistant;

namespace fixes {

// Une ligne d'erreur ou d'avertissement, telle que l'apercu et la carte la montrent.
struct Line {
    std::string              message;      // le message, tel que la validation l'a ecrit
    std::string              text;         // ce qui se lit : le message, et << (2 operations) >> pour un doublon
    std::size_t              count{1};     // combien de fois la validation l'a dit
    std::vector<std::size_t> ops;          // les operations en cause (indices de Proposal::ops, sans doublon)
    std::vector<std::size_t> faults;       // ses defauts structures (indices de Check::faults ; une erreur seulement)
    bool                     error{true};
};
[[nodiscard]] std::vector<Line> errorLines(const Check&);
[[nodiscard]] std::vector<Line> warningLines(const Check&);

// Une entree du menu. `children` : un sous-menu (Creer la variable : les types) - l'entree elle-meme ne se choisit pas.
struct Action {
    enum class Kind : std::uint8_t { Separator, Use, Pick, Create, RemoveUsing, RemoveOps, Copy, AskAi, Revert, Restore,
                                     BuildFix /* 1.22.5 : la correction du logiciel d'un constat (ops[0] : son rang dans le verdict) */ };
    Kind                     kind{Kind::Separator};
    std::string              label;
    std::string              note;         // la petite ligne sous le libelle
    std::string              name;         // la variable introuvable (Use, Pick, Create, RemoveUsing, Revert)
    std::string              value;        // Use : la variable a la place ; Create : le nom a creer ; Copy, AskAi : le texte
    std::string              type;         // Create : le type
    std::vector<std::size_t> ops;          // RemoveUsing, RemoveOps, Restore
    bool                     enabled{true};
    std::string              why;          // grisee : pourquoi
    std::vector<Action>      children;
};
struct Menu {
    std::string         title;             // la ligne de tete du menu
    std::vector<Action> actions;
};
// Le menu d'une ligne d'erreur ou d'avertissement ; `project` (nul : inconnu) : une variable a creer qui existe deja se
// propose a l'usage.
[[nodiscard]] Menu menuFor(const Proposal&, const Check&, const Line&, const hmi::Project* project);

// LES CORRECTIONS FAITES : une ligne chacune (un choix de variable, une operation retiree), et le geste qui la reprend.
struct Correction {
    std::string text;                      // "<< Vanne.Cmd >> -> Vanne_Ouverte", "operation 3 (associer Vanne_1)"
    std::string state;                     // "remplacee", "a creer (BOOL)", "retiree"
    int         what{1};                   // 0 : une variable a creer, 1 : remplacee, 2 : une operation retiree
    Action      undo;                      // Revenir
};
[[nodiscard]] std::vector<Correction> corrections(const Proposal&);
[[nodiscard]] Menu menuFor(const Proposal&, const Correction&);
// Ce qui a ete corrige, en une ligne : "7 variables remplac\xC3\xA9" "es, 1 op\xC3\xA9ration retir\xC3\xA9" "e" ; vide : rien.
[[nodiscard]] std::string summary(const Proposal&);
// Ce qui, change par une correction, refait la verification (la cle de la carte, de l'apercu).
[[nodiscard]] std::string key(const Proposal&);
// L'operation `op`, en clair : "op\xC3\xA9ration 3 (associer Vanne_1)".
[[nodiscard]] std::string opLabel(const Proposal&, std::size_t op);
// Le texte prepare pour << Demander a l'IA de corriger >> (mis dans la saisie, jamais envoye seul).
[[nodiscard]] std::string askText(const Line&);
// Les types proposes pour creer une variable (le conseille vient en tete du sous-menu).
[[nodiscard]] const std::vector<std::string>& types();

// FAIRE une action qui change la proposition (Use, Create, RemoveUsing, RemoveOps, Revert, Restore) - par l'Assistant ;
// `said` : ce que la barre d'etat dit. Faux : rien n'a change. Pick, Copy et AskAi sont a l'ecran qui les offre.
bool apply(Assistant&, int message, const Action&, std::string* said = nullptr);

// LE MENU DU CLIC DROIT : ses entrees posees dans `menu` (une tete, les actions, leurs icones, les sous-menus) ; `byId` recoit
// les actions choisissables (l'id d'une entree est son rang dans `byId`). Les libelles montres (un sous-menu :
// "parent > enfant") et l'id d'un libelle (-1 : aucun) - pour les essais et les scripts.
void fill(ui::PopupMenu& menu, const Menu&, std::vector<Action>& byId);
[[nodiscard]] std::vector<std::string> labels(const ui::PopupMenu& menu);
[[nodiscard]] int idOf(const ui::PopupMenu& menu, std::string_view label);

} // namespace fixes
} // namespace app::ai
