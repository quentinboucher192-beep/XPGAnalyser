// =============================================================================
//  app/ExecutionOrderWiring.hpp — brancher l'ordre d'execution sur l'arbre
// -----------------------------------------------------------------------------
//  Tout ce qui relie le dossier "Ordre d'execution" au projet tient ici : les
//  deux predicats du glisser-deposer, le sens a donner a un depot, et les deux
//  actions du menu contextuel.
//
//  CE FICHIER NE CONNAIT PAS TON MENU. Ouvrir un menu contextuel est une
//  affaire d'ecran, et il en existe deja un dans l'application ; en poser un
//  second serait pire que rien. `moveUp` et `moveDown` rendent donc une
//  commande prete a pousser sur la pile, et `canMoveUp` / `canMoveDown` disent
//  s'il faut griser l'entree. Trois lignes chez toi, et rien a reecrire ici.
//
//  POURQUOI UNE COMMANDE ET PAS UNE MUTATION DIRECTE : deplacer une section
//  renumerote ses voisines. Sans commande, Ctrl+Z ne rattrape pas le
//  deplacement - et dans un ordre d'execution, un deplacement qu'on ne peut pas
//  defaire est un deplacement qu'on hesite a faire.
// =============================================================================
#pragma once

#include "ViewModels.hpp"

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../domain/ExecutionOrder.hpp"
#include "../project/EditCommands.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>

namespace app {

class ExecutionOrderWiring {
public:
    using ProjectPtr = std::shared_ptr<domain::Project>;
    using Apply      = std::function<void(core::CommandPtr)>;

    // ---- le menu contextuel --------------------------------------------------
    // `node` est ce sur quoi on a clique droit : une etape de l'ordre
    // d'execution, ou une section, ou une unite d'une seule section. Les trois
    // designent la meme chose, et l'utilisateur ne fait pas la difference.
    [[nodiscard]] static bool canMove(const domain::Project& p, const ProjectTreeModel& model,
                                      ui::NodeId node, int delta) {
        const auto section = model.sectionOf(node);
        if (section == domain::kNoIndex) return false;
        const auto task = domain::taskOf(p, section);
        if (task == 0) return false;                 // un corps de DFB ne s'ordonne pas

        const auto steps = domain::executionOrder(p, task);
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (steps[i].section != section) continue;
            const auto to = static_cast<long long>(i) + delta;
            return to >= 0 && to < static_cast<long long>(steps.size());
        }
        return false;
    }

    [[nodiscard]] static bool canMoveUp(const domain::Project& p, const ProjectTreeModel& m,
                                        ui::NodeId n) { return canMove(p, m, n, -1); }
    [[nodiscard]] static bool canMoveDown(const domain::Project& p, const ProjectTreeModel& m,
                                          ui::NodeId n) { return canMove(p, m, n, +1); }

    [[nodiscard]] static core::CommandPtr move(const ProjectPtr& p, const ProjectTreeModel& model,
                                               ui::NodeId node, int delta) {
        const auto section = model.sectionOf(node);
        if (section == domain::kNoIndex) return nullptr;
        return std::make_unique<project::ReorderSectionCommand>(p, section, delta);
    }

    // ---- le glisser-deposer ---------------------------------------------------
    //
    //  `apply` est ce que fait l'ecran d'une commande : normalement
    //  `App::apply`, qui la pousse sur la pile et rafraichit les vues.
    //
    //  `links` EST OBLIGATOIRE, et ce n'est pas une precaution de style :
    //  `Signal::connect` rend une Connection qui se DEBRANCHE en mourant. La
    //  laisser tomber par terre branche puis debranche aussitot, et le depot ne
    //  fait plus rien - sans message, sans plantage, sans rien. C'est ce qui
    //  est arrive a la premiere version de ce fichier, et c'est le test de
    //  bout en bout qui l'a montre.
    static void install(ui::TreeView& tree, std::shared_ptr<ProjectTreeModel> model,
                        ProjectPtr project, Apply apply, core::ConnectionScope& links) {
        // On ne traine QUE ce qui a un sens a trainer. Un arbre ou tout se
        // deplace par defaut est un arbre ou l'on change l'ordre d'execution
        // par accident, et l'accident ne se voit qu'a la mise en service.
        tree.setDragPredicate([model, project](ui::NodeId n) {
            const auto section = model->sectionOf(n);
            return section != domain::kNoIndex && domain::taskOf(*project, section) != 0;
        });

        // Une section ne se depose qu'a cote d'une autre section DE LA MEME
        // TACHE. Deposer "dedans" n'a pas de sens ici : l'ordre est une liste,
        // pas une hierarchie.
        tree.setDropPredicate([model, project](ui::NodeId dragged, ui::NodeId target,
                                               ui::TreeView::DropWhere where) {
            if (where == ui::TreeView::DropWhere::Into) return false;
            const auto a = model->sectionOf(dragged);
            const auto b = model->sectionOf(target);
            if (a == domain::kNoIndex || b == domain::kNoIndex || a == b) return false;
            const auto task = domain::taskOf(*project, a);
            return task != 0 && domain::taskOf(*project, b) == task;
        });

        links += tree.dropped->connect([model, project, apply](ui::NodeId dragged,
                                                               ui::NodeId target,
                                                               ui::TreeView::DropWhere where) {
            const auto section = model->sectionOf(dragged);
            const auto onto    = model->sectionOf(target);
            if (section == domain::kNoIndex || onto == domain::kNoIndex) return;

            // Le modele ne connait qu'une seule notion d'insertion : AVANT.
            // "Apres la cible" se traduit donc en "avant la suivante", et
            // "apres la derniere" en "a la fin". Une deuxieme notion
            // d'insertion serait une deuxieme occasion de se tromper d'un cran.
            auto before = onto;
            if (where == ui::TreeView::DropWhere::After) {
                const auto task  = domain::taskOf(*project, section);
                const auto steps = domain::executionOrder(*project, task);
                before = domain::kNoIndex;
                for (std::size_t i = 0; i < steps.size(); ++i)
                    if (steps[i].section == onto && i + 1 < steps.size()) {
                        before = steps[i + 1].section;
                        break;
                    }
            }
            apply(std::make_unique<project::ReorderSectionCommand>(
                project, section, project::ReorderSectionCommand::Before{before}));
        });
    }
};

} // namespace app
