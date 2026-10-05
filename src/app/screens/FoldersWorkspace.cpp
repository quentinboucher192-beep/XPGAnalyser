// =============================================================================
//  app/screens/FoldersWorkspace.cpp - lot 21 : les dossiers dans l'arbre
// -----------------------------------------------------------------------------
//  L'ARBRE DU PROJET MONTRE LES DOSSIERS DES LISTES DE L'IHM (vues, popups,
//  ecrans modeles, en-tetes, pieds de page, symboles, scripts generaux, types
//  IHM) comme ceux des variables, et on y glisse et depose :
//   - un ou plusieurs elements (Ctrl+clic) sur un dossier, ou sur le noeud de
//     la liste (sa racine) : ils y sont ranges ;
//   - entre deux elements : ils s'y placent (l'ordre du projet change) et
//     prennent le dossier de leur voisin ;
//   - un dossier sur un autre : il y va, avec ce qu'il contient.
//  Rien ne passe d'une liste a l'autre (une popup ne devient pas une vue).
//  Les predicats de l'ordre d'execution (les sections de l'API) restent : ceux
//  de l'IHM s'y ajoutent. Une commande par geste - Ctrl+Z le defait.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiFolders.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>

namespace app {

using namespace ui;
using NK = ProjectTreeModel::NodeKind;
using Where = TreeView::DropWhere;
namespace fold = hmi::fold;

namespace {

// Ce qu'un noeud de l'arbre est pour les dossiers : un element d'une liste, un
// dossier (ou le noeud de la liste : le dossier ""), ou rien.
struct Place {
    int         list{-1};
    bool        folder{false};
    std::string path;            // le dossier (folder), ou celui de l'element
    hmi::Id     id{hmi::kNoId};  // l'element
};

} // namespace

void MainAnalysisScreen::wireHmiTreeDrag() {
    if (!explorer_ || !treeModel_) return;
    const auto execDrag = explorer_->dragPredicate();
    const auto execDrop = explorer_->dropPredicate();
    const auto place = [this](NodeId n) {
        Place p;
        if (!treeModel_) return p;
        if (treeModel_->hmiListFolderOf(n, p.list, p.path)) {
            p.folder = true;
            return p;
        }
        p.list = treeModel_->hmiListOf(n);
        if (p.list < 0) return p;
        const auto kind = ProjectTreeModel::kindOf(n);
        p.id = static_cast<hmi::Id>(kind == NK::HmiView ? treeModel_->hmiViewOf(n) : treeModel_->hmiIdOf(n));
        if (auto doc = app_.hmi()) p.path = fold::folderOf(doc->project, static_cast<fold::List>(p.list), p.id);
        return p;
    };
    explorer_->setDragPredicate([place, execDrag](NodeId n) {
        const Place p = place(n);
        // Un element d'une liste, ou un de ses dossiers (pas le noeud de la liste lui-meme).
        if (p.list >= 0 && (!p.folder || !p.path.empty())) return true;
        return execDrag && execDrag(n);
    });
    explorer_->setDropPredicate([this, place, execDrop](NodeId dragged, NodeId target, Where where) {
        const Place from = place(dragged);
        if (from.list < 0) return execDrop && execDrop(dragged, target, where);
        const Place to = place(target);
        if (to.list != from.list) return false;
        // Tout ce qu'on traine est-il de la meme liste ? Un dossier est-il du lot ?
        bool anyFolder = false;
        for (const auto n : explorer_->draggedNodes()) {
            const Place p = place(n);
            if (p.list != from.list) return false;
            if (p.folder) {
                anyFolder = true;
                if (to.folder && fold::inside(to.path, p.path)) return false;   // pas dans lui-meme
            }
        }
        if (to.folder) return where == Where::Into;
        return !anyFolder && where != Where::Into;                               // entre deux elements
    });
    execOrderLinks_ += explorer_->dropped->connect([this, place](NodeId dragged, NodeId target, Where where) {
        const Place from = place(dragged);
        if (from.list < 0) return;                     // une section : l'ordre d'execution s'en charge
        const Place to = place(target);
        auto doc = app_.hmi();
        if (!doc || to.list != from.list) return;
        const auto list = static_cast<fold::List>(from.list);
        std::vector<hmi::Id> ids;
        std::vector<std::string> folders;
        auto nodes = explorer_->draggedNodes();
        if (nodes.empty()) nodes.push_back(dragged);
        for (const auto n : nodes) {
            const Place p = place(n);
            if (p.list != from.list) continue;
            if (p.folder) folders.push_back(p.path);
            else ids.push_back(p.id);
        }
        std::string said;
        core::CommandPtr cmd;
        if (to.folder) {
            std::size_t moved = 0;
            std::string why;
            const std::string label = "Ranger " + (folders.empty() ? std::to_string(ids.size()) + " " + fold::noun(list, ids.size()) + " " : std::string{})
                                    + "dans " + (to.path.empty() ? std::string(fold::label(list)) : to.path);
            cmd = hmi::changeProject(doc, label, [&](hmi::Project& p) {
                for (const auto& f : folders)
                    if (!fold::sameFolder(hmi::types::folderParent(f), to.path) && fold::moveFolder(p, list, f, to.path, &why)) ++moved;
                moved += fold::moveToFolder(p, list, ids, to.path);
            });
            if (moved == 0) {
                if (!why.empty() && status_) status_->setTransientMessage("Rien n'est d\xC3\xA9plac\xC3\xA9 : " + why, 6.0);
                return;
            }
            const std::string what = folders.empty() ? fold::noun(list, moved) + " " + fold::agreed(list, moved, "rang\xC3\xA9")
                                                     : std::string(moved > 1 ? "\xC3\xA9l\xC3\xA9ments rang\xC3\xA9s" : "\xC3\xA9l\xC3\xA9ment rang\xC3\xA9");
            said = std::to_string(moved) + " " + what + " "
                 + (to.path.empty() ? "\xC3\xA0 la racine de " + std::string(fold::label(list)) : "dans " + to.path);
        } else {
            bool ok = false;
            const bool after = where == Where::After;
            cmd = hmi::changeProject(doc, "D\xC3\xA9placer " + std::to_string(ids.size()) + " " + fold::noun(list, ids.size()),
                                     [&](hmi::Project& p) { ok = fold::moveNear(p, list, ids, to.id, after); });
            if (!ok) return;
            // Le nom de la cible (pas son libelle de l'arbre, avec sa taille et ses objets).
            std::string anchorName = treeModel_->text(target);
            for (const auto& item : fold::items(doc->project, list))
                if (item.id == to.id) {
                    anchorName = item.name;
                    break;
                }
            said = std::to_string(ids.size()) + " " + fold::noun(list, ids.size()) + " " + fold::agreed(list, ids.size(), "plac\xC3\xA9")
                 + (after ? " apr\xC3\xA8s " : " avant ") + anchorName;
        }
        if (cmd) app_.apply(std::move(cmd), false);
        if (status_) status_->setTransientMessage(said + " (Ctrl+Z pour annuler)", 8.0, StatusBar::Severity::Success);
    });
}

} // namespace app
