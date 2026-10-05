// =============================================================================
//  domain/ExecutionOrder.hpp — l'ordre dans lequel une tache execute ses sections
// -----------------------------------------------------------------------------
//  POURQUOI CE FICHIER EXISTE
//
//  `Section::order` etait lu a l'import, ecrit a l'export, range dans le
//  projet - et personne ne s'en servait. L'arbre ne le montrait pas, aucune
//  commande ne le changeait, et le simulateur, lui, ne le respectait qu'a
//  moitie : il prenait les sections de la tache dans l'ordre, puis AJOUTAIT A
//  LA FIN celles des unites de programme. Une unite placee en tete par
//  l'utilisateur tournait donc en queue.
//
//  Ca ne se voit pas sur une temperature. Ca se voit sur un comptage de
//  pieces, sur une commande d'IHM qui repond avec un cycle de retard, et sur
//  une mesure publiee avant que le programme ne l'ait calculee. C'est
//  exactement la classe de defaut que ce projet existe pour attraper.
//
//  CE QUE CE FICHIER POSE
//
//  Une seule verite : `Section::order`. `Task::sections` n'est qu'un cache
//  trie, et toute fonction qui touche a l'ordre le reconstruit. Le jour ou les
//  deux divergent, c'est le cache qui a tort.
//
//  Les fonctions ne sont pas des commandes : elles n'annulent rien. C'est
//  `project::ReorderSectionCommand` qui les enveloppe pour que Ctrl+Z les
//  atteigne, comme tout le reste.
// =============================================================================
#pragma once

#include "ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace domain {

// Une etape de l'ordre d'execution. `unit` est l'unite de programme ou la
// section vit ; pour une section de tache nue, c'est le POU de genre Section
// que l'application cree pour qu'elle apparaisse dans l'explorateur.
struct ExecutionStep {
    Index         section{kNoIndex};
    Index         unit{kNoIndex};
    std::uint32_t order{0};
    bool          fromProgramUnit{false};   // vit dans une unite, pas dans la tache
};

// Toutes les sections que cette tache execute, dans l'ordre : celles de la
// tache ET celles des unites de programme qu'elle ordonnance, melees et triees
// par `order`. Les separer etait le defaut du simulateur.
//
// Le tri est STABLE : plusieurs sections partagent legitimement un `order`
// apres un import - Control Expert ne garantit pas l'unicite - et deux
// executions de suite doivent donner le meme ordre, faute de quoi une
// simulation n'est pas reproductible.
[[nodiscard]] std::vector<ExecutionStep> executionOrder(const Project&, SymbolId task);
[[nodiscard]] std::vector<ExecutionStep> executionOrder(const Project&, std::string_view task);

// Lot API 4 : LES ENTREES de la tache - ce que Control Expert range dans
// <taskDesc> : une section de la tache, ou une unite de programme EN BLOC (toutes
// ses sections, a son rang). Une unite sans rang (order 0) donne une entree par
// section, comme avant.
struct ExecutionEntry {
    bool               unit{false};        // une unite de programme, en bloc
    Index              pou{kNoIndex};      // l'unite, ou le POU de la section
    Index              section{kNoIndex};  // la premiere section de l'entree
    std::vector<Index> sections;           // toutes, dans l'ordre
};
[[nodiscard]] std::vector<ExecutionEntry> executionEntries(const Project&, SymbolId task);
[[nodiscard]] std::vector<ExecutionEntry> executionEntries(const Project&, std::string_view task);

// Renumerote 1, 2, 3... dans l'ordre courant, et retrie `Task::sections`.
// Les entrees prennent un rang chacune (une unite en bloc : `Pou::order`) ; les
// sections d'une unite, 1, 2, 3... chez elle.
// A appeler apres tout deplacement : sans ca les ex aequo d'un import rendent
// "monter d'un cran" imprevisible.
void renumber(Project&, SymbolId task);

// Deplace une section de `delta` ENTREES dans sa tache (-1 monter, +1 descendre).
// Une section d'une unite en bloc deplace l'unite entiere.
// Rend false quand il n'y a nulle part ou aller - premiere ligne vers le haut,
// derniere vers le bas - sans rien changer.
[[nodiscard]] bool moveSection(Project&, Index section, int delta);

// Place une section AVANT `before` (ou a la fin si `before` vaut kNoIndex).
// C'est ce dont le glisser-deposer a besoin : il connait une cible, pas un
// nombre de crans.
[[nodiscard]] bool moveSectionBefore(Project&, Index section, Index before);

// La tache qui execute cette section, en remontant l'unite de programme quand
// la section n'en porte pas elle-meme. Rend 0 quand personne ne l'execute -
// le corps d'un DFB, par exemple, qui tourne quand on l'appelle et pas au
// rythme d'une tache.
[[nodiscard]] SymbolId taskOf(const Project&, Index section);

} // namespace domain
