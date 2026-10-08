// =============================================================================
//  app/screens/ExplorerMenus.cpp - lot 7 : le clic droit de l'explorateur
// -----------------------------------------------------------------------------
//  UN MENU PAR GENRE DE NOEUD. Le clic droit proposait les memes quelques
//  entrees partout (Ouvrir, Supprimer, Tout deplier...), en anglais pour la
//  plupart. Il propose maintenant ce qui se fait LA : sur le dossier API, tout
//  ce qui se cree ; sur une variable, la renommer, l'ajouter a une table
//  d'animation, la forcer, la suivre sur la courbe, voir ou elle sert ; sur
//  une unite, une section de plus, son code, sa place dans l'ordre
//  d'execution ; sur une vue de l'IHM, l'ouvrir dans une fenetre, la
//  dupliquer, la ranger ; sur une version, la comparer, la restaurer...
//
//  CHAQUE ENTREE FAIT QUELQUE CHOSE DE REEL, par les chemins qui existent
//  deja : les onglets (openApiPane, openHmiPane) et leurs actions (runAction),
//  les actions de l'application (create.section...), les dialogues de
//  l'ecran (askRename, askDeleteHmiView...). Une entree qui ne peut pas servir
//  est GRISEE AVEC SA RAISON (un export pas encore enregistre en dossier, un
//  type encore utilise, une simulation arretee), et les raccourcis qui existent
//  sont ecrits en face (F2, Suppr, F1, F9, Ctrl+1, Ctrl+5).
//
//  Les entrees portent leur action : un rappel range dans contextCalls_, l'id
//  de l'entree valant ActionCall + son rang. Le menu est refait a chaque clic
//  droit : il juge chaque entree contre le projet tel qu'il est MAINTENANT.
//  Les sous-menus (ui::PopupMenu::Item::children, lot 7) : les tables
//  d'animation, les sections d'une unite ou d'un bloc.
//
//  F2 ET SUPPR sur le noeud courant de l'arbre (TreeView::renameRequested,
//  deleteRequested) font ce que font les entrees Renommer et Supprimer du
//  menu de ce noeud (treeKey) - ou disent pourquoi elles sont grisees.
// =============================================================================
#include "Screens.hpp"

#include "../AnimationTablesPane.hpp"
#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../../hmi/HmiBuildState.hpp"   // 1.11 (chantier T3, C4) : les deux filtres de l'arbre
#include "../ExecutionOrderWiring.hpp"
#include "../SimCenter.hpp"              // ---- Lot API 8 : Centre de simulation (les menus de ses noeuds) ----
#include "../SimulationPane.hpp"
#include "../StatisticsPane.hpp"
#include "../TaskPanes.hpp"
#include "../TypePanes.hpp"
#include "../VariablesPane.hpp"
#include "../hmi/HmiDesignPanes.hpp"
#include "../../help/HelpIndex.hpp"
#include "../../help/HelpSession.hpp"     // help::labelOf
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiFolders.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../project/ApiCommands.hpp"
#include "../../project/DeleteCommands.hpp"
#include "../../project/SharedLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "../hmi/HmiBuild.hpp"          // 1.11.13 : le build de l'IHM
#include "../hmi/HmiBuildPanes.hpp"
#include "../FilePreview.hpp"

namespace app {

using namespace ui;
using NK = ProjectTreeModel::NodeKind;
namespace fold = hmi::fold;

namespace {

using Item = PopupMenu::Item;

const std::string kDots = "\xE2\x80\xA6";
// Un export lu tel quel ne se modifie pas : il faut un dossier de projet.
const std::string kNoDocument = "enregistre d'abord le projet en dossier";

template <typename Pane>
Pane* paneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<Pane*>(&frame->content()) : nullptr;
}

// Un libelle trop long pour un en-tete de menu : coupe, avec des points.
std::string clipped(std::string text, std::size_t max) {
    if (text.size() <= max) return text;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
    return text.substr(0, cut) + kDots;
}

// La fabrique des entrees : une entree porte son action (un rappel range dans
// `calls`, son id = base + rang) ; sans action, ou avec une raison, grisee.
class MenuKit {
public:
    MenuKit(std::vector<Item>& items, std::vector<std::function<void()>>& calls, int base)
        : items_(items), calls_(calls), base_(base) {}

    [[nodiscard]] Item make(std::string label, Icon icon, std::function<void()> fn, std::string shortcut = {},
                            std::string why = {}) {
        Item it;
        it.label = std::move(label);
        it.icon = icon;
        it.shortcut = std::move(shortcut);
        it.enabled = why.empty() && static_cast<bool>(fn);
        it.disabledReason = std::move(why);
        if (it.enabled) {
            calls_.push_back(std::move(fn));
            it.id = base_ + static_cast<int>(calls_.size() - 1);
        }
        return it;
    }
    void add(std::string label, Icon icon, std::function<void()> fn, std::string shortcut = {}, std::string why = {}) {
        items_.push_back(make(std::move(label), icon, std::move(fn), std::move(shortcut), std::move(why)));
    }
    // Un sous-menu : ses entrees a cote de lui ; vide, ou `why` : grise.
    void sub(std::string label, Icon icon, std::vector<Item> children, std::string why = {}) {
        Item it;
        it.label = std::move(label);
        it.icon = icon;
        if (why.empty() && children.empty()) why = "rien \xC3\xA0 proposer";
        it.enabled = why.empty();
        it.disabledReason = std::move(why);
        if (it.enabled) it.children = std::move(children);
        items_.push_back(std::move(it));
    }
    // Une ligne de titre (ce sur quoi on a clique), et a droite un detail.
    void heading(std::string label, std::string right = {}) {
        Item it;
        it.label = std::move(label);
        it.shortcut = std::move(right);
        it.heading = true;
        items_.push_back(std::move(it));
    }
    void separator() {
        if (items_.empty() || items_.back().separator || items_.back().heading) return;
        Item it;
        it.separator = true;
        items_.push_back(std::move(it));
    }
    void trim() {
        while (!items_.empty() && items_.back().separator) items_.pop_back();
    }

private:
    std::vector<Item>&                  items_;
    std::vector<std::function<void()>>& calls_;
    int                                 base_;
};

} // namespace

// ============================================================ le menu ====
void MainAnalysisScreen::showExplorerMenu(NodeId node, gfx::Point at) {
    if (!contextMenu_) return;
    contextNode_ = node;
    std::vector<PopupMenu::Item> items;
    TreeKeys keys;
    buildExplorerMenu(node, items, keys);
    contextMenu_->setItems(std::move(items));
    const auto surface = root().bounds();
    contextMenu_->openAt(at, {surface.w, surface.h});
}

// Lot 7 : l'infobulle d'un noeud de l'arbre, calculee a chaque image tant
// qu'elle est ouverte (TreeView::setNodeTooltip) : ce qu'une variable vaut EN CE
// MOMENT dans la simulation, l'etat et le cycle de API > Simulation. "" : celle
// de l'arbre.
std::string MainAnalysisScreen::explorerTip(NodeId node) const {
    using State = SimulationHost::State;
    const auto kind = ProjectTreeModel::kindOf(node);
    const auto& host = app_.simulation();
    if (kind == NK::ApiSimulation) {
        if (!host.attached()) return "La simulation n'est pas pr\xC3\xA9par\xC3\xA9" "e : Simuler (ou F9) la lance.\nClic droit : Simuler, Un cycle.";
        const auto state = host.state();
        const std::string said = state == State::Running ? "en marche" : state == State::Paused ? "en pause"
                               : state == State::Halted ? "arr\xC3\xAAt\xC3\xA9" "e (d\xC3\xA9" "faut)" : "arr\xC3\xAAt\xC3\xA9" "e";
        return "La simulation : " + said + " \xC2\xB7 cycle " + std::to_string(host.scanCount())
             + "\nClic droit : Simuler, Pause, Un cycle, Arr\xC3\xAAter.";
    }
    // Le nom que la simulation connait : une globale, un membre (d'une globale ou
    // d'une variable d'unite), une variable d'unite ("Unite.var").
    const auto project = app_.project();
    if (!project || !treeModel_ || !host.attached()) return {};
    std::string path;
    if (kind == NK::ListVariable) {
        const auto at = treeModel_->variableOf(node);
        if (at < project->variables.size()) path = std::string(project->strings.text(project->variables[at].name));
    } else if (kind == NK::MemberNode && !treeModel_->memberIsGroup(node)) {
        const auto root = treeModel_->memberRootOf(node);
        if (root < project->variables.size()) {
            const auto& var = project->variables[root];
            if (var.scope == domain::VariableScope::Global) path = treeModel_->memberPathOf(node);
            else if (var.scope != domain::VariableScope::DerivedMember && var.owner < project->pous.size()
                     && project->pous[var.owner].kind == domain::PouKind::ProgramUnit)
                path = std::string(project->strings.text(project->pous[var.owner].name)) + "." + treeModel_->memberPathOf(node);
        }
    } else if (kind == NK::DfbVariable) {
        const auto i = ProjectTreeModel::indexOf(node);
        if (i < project->variables.size()) {
            const auto& var = project->variables[i];
            if (var.owner < project->pous.size() && project->pous[var.owner].kind == domain::PouKind::ProgramUnit)
                path = std::string(project->strings.text(project->pous[var.owner].name)) + "." + std::string(project->strings.text(var.name));
        }
    }
    const auto* rt = host.runtime();
    if (path.empty() || !rt) return {};
    sim::Value value;
    if (!rt->get(path, value)) return {};
    std::string tip = path + " = " + value.display();
    if (rt->isForced(path)) tip += "   (forc\xC3\xA9" "e)";
    tip += "\nEn simulation, cycle " + std::to_string(host.scanCount()) + " : la valeur suit tant que l'infobulle est ouverte.";
    return tip;
}

// F2 et Suppr sur le noeud courant : l'entree Renommer (ou Supprimer) de son menu.
void MainAnalysisScreen::treeKey(NodeId node, bool rename) {
    if (node == kInvalidNode || (contextMenu_ && contextMenu_->isOpen())) return;
    contextNode_ = node;
    std::vector<PopupMenu::Item> items;
    TreeKeys keys;
    buildExplorerMenu(node, items, keys);
    const auto fn = rename ? keys.rename : keys.remove;
    if (fn) {
        fn();
        return;
    }
    const std::string& why = rename ? keys.renameWhy : keys.removeWhy;
    if (status_)
        status_->setTransientMessage(std::string(rename ? "F2" : "Suppr") + " : "
                                         + (why.empty() ? std::string(rename ? "rien \xC3\xA0 renommer ici" : "rien \xC3\xA0 supprimer ici") : why),
                                     5.0);
}

void MainAnalysisScreen::buildExplorerMenu(NodeId node, std::vector<PopupMenu::Item>& items, TreeKeys& keys) {
    contextCalls_.clear();
    items.clear();
    keys = TreeKeys{};
    MenuKit m(items, contextCalls_, ActionCall);

    const auto kind = ProjectTreeModel::kindOf(node);
    const auto index = ProjectTreeModel::indexOf(node);
    const auto project = app_.project();
    const auto document = app_.document();
    const auto hmiDoc = app_.hmi();
    const std::string noDoc = document ? std::string{} : kNoDocument;
    const auto text = [&](domain::SymbolId id) { return project ? std::string(project->strings.text(id)) : std::string{}; };

    // ---- les gestes communs ------------------------------------------------------
    const auto trigger = [this](std::string id) {
        return [this, id] { (void)app_.actions().trigger(id, app_.commands()); };
    };
    const auto apiPane = [this](std::string key) { return [this, key] { openApiPane(key); }; };
    const auto hmiPane = [this](std::string key) { return [this, key] { openHmiPane(key); }; };
    const auto legacy = [this](int action) { return [this, action] { runExplorerAction(action); }; };
    const auto copy = [this](std::string what) {
        return [this, what] {
            ui::setClipboardText(what);
            if (status_) status_->setTransientMessage("Copi\xC3\xA9 : " + what, 5.0, StatusBar::Severity::Success);
        };
    };
    // Renommer (F2) et Supprimer (Suppr) : les touches font la meme chose.
    const auto addRename = [&](std::string label, std::function<void()> fn, std::string why = {}) {
        if (why.empty()) keys.rename = fn;
        else keys.renameWhy = why;
        m.add(std::move(label), Icon::Document, std::move(fn), "F2", std::move(why));
    };
    const auto addRemove = [&](std::string label, std::function<void()> fn, std::string why = {}) {
        if (why.empty()) keys.remove = fn;
        else keys.removeWhy = why;
        m.add(std::move(label), Icon::Close, std::move(fn), "Suppr", std::move(why));
    };
    const auto rename = [this](std::string what, std::string name) {
        return [this, what, name] { askRename(what, name); };
    };
    // Supprimer ce que le noeud designe dans le projet (un type, un bloc, une
    // unite, une section, une variable) : juge MAINTENANT - un type qui a
    // gagne une instance depuis le dernier clic droit ne se supprime plus.
    const auto addEntityRemove = [&]() {
        domain::EntityKind ek{};
        domain::Index ei{};
        if (!project || !entityForNode(node, ek, ei)) return;
        std::string reason;
        if (ek == domain::EntityKind::DerivedType) reason = project::firstUserOfDerivedType(*project, ei);
        else if (ek == domain::EntityKind::Pou) reason = project::firstInstanceOfBlock(*project, ei);
        if (!reason.empty()) reason = "utilis\xC3\xA9 par " + reason;
        if (!document) reason = kNoDocument;
        addRemove("Supprimer" + kDots, [this, ek, ei] { confirmAndDelete(ek, ei); }, reason);
    };

    // ---- les tables d'animation, la simulation -------------------------------------
    const auto tablesPane = [this]() -> AnimationTablesPane* {
        openApiPane("tables");
        return paneOf<AnimationTablesPane>(apiTab("tables"));
    };
    const auto newTable = [tablesPane] {
        if (auto* pane = tablesPane()) pane->runAction(AnimationTablesPane::ATable);
    };
    // "Ajouter a une table d'animation >" : une entree par table, et une nouvelle.
    const auto tableMenu = [&](std::vector<project::AnimationLine> lines) {
        if (lines.empty()) return;
        std::vector<Item> kids;
        if (project)
            for (std::size_t t = 0; t < project->animationTables.size() && t < 40; ++t)
                kids.push_back(m.make(text(project->animationTables[t].name), Icon::AnimationTable,
                                      [tablesPane, lines, t] {
                                          if (auto* pane = tablesPane()) (void)pane->addLines(lines, t);
                                      },
                                      {}, noDoc));
        if (!kids.empty()) {
            Item rule;
            rule.separator = true;
            kids.push_back(std::move(rule));
        }
        // La table creee va a la fin de la liste ; l'onglet ne la choisit qu'a
        // son prochain rafraichissement (l'evenement est differe) : les lignes y
        // vont par son rang, pas par "la table choisie".
        kids.push_back(m.make("Nouvelle table", Icon::AnimationTable,
                              [this, tablesPane, lines] {
                                  auto* pane = tablesPane();
                                  auto doc = app_.document();
                                  if (!pane || !doc) return;
                                  const auto before = doc->animationTables.size();
                                  pane->runAction(AnimationTablesPane::ATable);
                                  if (doc->animationTables.size() > before) (void)pane->addLines(lines, doc->animationTables.size() - 1);
                              },
                              {}, noDoc));
        std::string title = "Ajouter \xC3\xA0 une table d'animation";
        if (lines.size() > 1) title += " (" + std::to_string(lines.size()) + ")";
        m.sub(std::move(title), Icon::AnimationTable, std::move(kids), noDoc);
    };
    // Forcer, suivre sur la courbe : l'onglet API > Simulation, la variable
    // choisie (montree, ses dossiers ouverts), puis son action.
    const auto simulationDo = [this](std::string path, int action) {
        return [this, path, action] {
            openApiPane("simulation");
            auto* pane = paneOf<SimulationPane>(apiTab("simulation"));
            if (!pane) return;
            if (!pane->selectVariable(path)) {
                if (status_) status_->setTransientMessage(path + " : la simulation ne la conna\xC3\xAEt pas.", 6.0, StatusBar::Severity::Warning);
                return;
            }
            pane->runAction(action);
        };
    };
    const auto simulationItems = [&](const std::string& path) {
        if (path.empty()) return;
        m.add("Forcer en simulation" + kDots, Icon::Force, simulationDo(path, SimulationPane::AForce));
        bool watched = false;
        if (auto* pane = paneOf<SimulationPane>(apiTab("simulation"))) {
            const auto list = pane->watched();
            watched = std::find(list.begin(), list.end(), path) != list.end();
        }
        m.add(watched ? "Ne plus suivre sur la courbe" : "Suivre sur la courbe", Icon::Chart, simulationDo(path, SimulationPane::AWatch));
    };
    // Les variables de l'arbre que vise le clic droit : ce noeud, et les autres
    // noeuds choisis avec lui (Ctrl+clic) - les globales, les variables IHM, les
    // membres (des globales), les variables d'une unite ("Unite.var").
    const auto linesOf = [&](const std::vector<NodeId>& nodes) {
        std::vector<project::AnimationLine> out;
        const auto push = [&out](std::string name, bool hmi) {
            for (const auto& l : out) if (l.hmi == hmi && l.name == name) return;
            if (!name.empty()) out.push_back({std::move(name), hmi});
        };
        for (const auto n : nodes) {
            const auto k = ProjectTreeModel::kindOf(n);
            if (k == NK::ListVariable || k == NK::HmiVariable) {
                for (auto& [name, hmi] : treeVariables({n})) push(name, hmi);
            } else if (k == NK::MemberNode && treeModel_ && project && !treeModel_->memberIsGroup(n)) {
                const auto root = treeModel_->memberRootOf(n);
                if (root >= project->variables.size()) continue;
                const auto& var = project->variables[root];
                if (var.scope == domain::VariableScope::Global) push(treeModel_->memberPathOf(n), false);
                else if (var.scope != domain::VariableScope::DerivedMember && var.owner < project->pous.size()
                         && project->pous[var.owner].kind == domain::PouKind::ProgramUnit)
                    push(text(project->pous[var.owner].name) + "." + treeModel_->memberPathOf(n), false);
            } else if (k == NK::DfbVariable && project) {
                const auto i = ProjectTreeModel::indexOf(n);
                if (i >= project->variables.size()) continue;
                const auto& var = project->variables[i];
                if (var.owner < project->pous.size() && project->pous[var.owner].kind == domain::PouKind::ProgramUnit)
                    push(text(project->pous[var.owner].name) + "." + text(var.name), false);
            }
        }
        return out;
    };
    // Comme dans l'Explorateur de fichiers : un clic droit sur un noeud CHOISI
    // vise toute la selection ; sur un autre, lui seul.
    const auto selectedLines = [&] {
        std::vector<NodeId> nodes{node};
        if (explorer_) {
            const auto& sel = explorer_->selection();
            if (std::find(sel.begin(), sel.end(), node) != sel.end())
                for (const auto n : sel)
                    if (n != node) nodes.push_back(n);
        }
        return linesOf(nodes);
    };
    // ---- 1.8.0 : l'icone au choix, le comparateur, l'export lisible ----
    //  La meme regle que selectedLines : un clic droit sur un noeud CHOISI vise
    //  toute la selection ; sur un autre, lui seul.
    const auto selectionNodes = [&] {
        std::vector<NodeId> nodes{node};
        if (explorer_) {
            const auto& sel = explorer_->selection();
            if (std::find(sel.begin(), sel.end(), node) != sel.end())
                for (const auto n : sel)
                    if (n != node) nodes.push_back(n);
        }
        return nodes;
    };
    const auto selectedSections = [&] {
        std::vector<domain::Index> out;
        if (!project) return out;
        for (const auto n : selectionNodes()) {
            const auto k = ProjectTreeModel::kindOf(n);
            domain::Index s = domain::kNoIndex;
            if (k == NK::Section || k == NK::DfbSection) s = ProjectTreeModel::indexOf(n);
            else if (k == NK::ExecStep && treeModel_) s = treeModel_->sectionOf(n);
            if (s < project->sections.size() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        }
        return out;
    };
    const auto iconItem = [&] {
        std::string what, from;
        core::codeicons::Kind codeKind = core::codeicons::Kind::Section;
        auto codeKeys = codeIconKeys(selectionNodes(), &what, &from, &codeKind);
        const std::string label = codeKeys.size() > 1 ? "D\xC3\xA9" "finir l'ic\xC3\xB4ne des " + std::to_string(codeKeys.size()) + " \xC3\xA9l\xC3\xA9ments" + kDots
                                                      : "D\xC3\xA9" "finir l'ic\xC3\xB4ne" + kDots;
        const std::string why = codeKeys.empty() ? std::string("rien ici ne porte de code") : noDoc;
        m.add(label, Icon::Image, [this, codeKeys, what, from, codeKind] { askCodeIcon(codeKeys, what, from, codeKind); }, {}, why);
    };
    // "Comparer avec" : les autres sections, celles dont le nom ressemble d'abord (SFC_ManuA -> SFC_ManuB).
    const auto compareWith = [&](domain::Index s) {
        std::vector<Item> kids;
        if (!project || s >= project->sections.size()) return kids;
        const std::string a = text(project->sections[s].name);
        std::vector<std::pair<std::size_t, domain::Index>> ranked;
        for (domain::Index o = 0; o < project->sections.size(); ++o) {
            if (o == s) continue;
            const std::string b = text(project->sections[o].name);
            std::size_t n = 0;
            while (n < a.size() && n < b.size() && std::tolower(static_cast<unsigned char>(a[n])) == std::tolower(static_cast<unsigned char>(b[n]))) ++n;
            ranked.emplace_back(n, o);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (std::size_t k = 0; k < ranked.size() && k < 24; ++k) {
            const auto o = ranked[k].second;
            kids.push_back(m.make(text(project->sections[o].name) + "  (" + std::string(domain::toString(project->sections[o].language)) + ")",
                                  Icon::Section, [this, s, o] {
                                      std::string why;
                                      if (!openSectionCompare({s, o}, &why) && status_) status_->setTransientMessage("Comparer : " + why + ".", 6.0);
                                  }));
        }
        return kids;
    };
    const auto compareAndExport = [&](domain::Index s, bool exportable) {
        const auto secs = selectedSections();
        if (secs.size() >= 2) {
            m.add("Comparer les " + std::to_string(secs.size()) + " sections", Icon::Layers, [this, secs] {
                std::string why;
                if (!openSectionCompare(secs, &why) && status_) status_->setTransientMessage("Comparer : " + why + ".", 6.0);
            });
        } else {
            auto kids = compareWith(s);
            const bool none = kids.empty();
            m.sub("Comparer avec", Icon::Layers, std::move(kids), none ? std::string("aucune autre section") : std::string{});
        }
        if (!exportable) return;          // le corps d'un DFB : dans l'annexe des blocs, pas dans le cycle
        const auto chosen = secs.size() >= 2 ? secs : std::vector<domain::Index>{s};
        m.add(chosen.size() >= 2 ? "Exporter les " + std::to_string(chosen.size()) + " sections (lisible)" + kDots
                                 : std::string("Exporter la section (lisible)") + kDots,
              Icon::Export, [this, chosen] { askProgramExport(2, {}, chosen); });
    };
    // Les sections d'un POU, en sous-menu : chacune ouvre son code.
    const auto codeMenu = [&](domain::Index pou) {
        std::vector<Item> kids;
        if (project && pou < project->pous.size())
            for (const auto s : project->pous[pou].sections) {
                if (s >= project->sections.size()) continue;
                const auto& sec = project->sections[s];
                kids.push_back(m.make(text(sec.name) + "  (" + std::string(domain::toString(sec.language)) + ")", Icon::Document,
                                      [this, s] { openDocument(s); }));
            }
        const bool none = kids.empty();      // avant le deplacement : l'ordre des arguments n'est pas fixe
        m.sub("Ouvrir le code", Icon::Code, std::move(kids), none ? std::string("aucune section") : std::string{});
    };
    // Une section dans l'ordre d'execution : son entree, sinon celle de son unite.
    const auto showInOrder = [this](domain::Index section) {
        return [this, section] {
            const auto p = app_.project();
            openApiPane("ordre");
            auto* pane = paneOf<ExecutionOrderPane>(apiTab("ordre"));
            if (!p || !pane || section >= p->sections.size()) return;
            if (pane->selectEntry(p->strings.text(p->sections[section].name))) return;
            for (const auto& pou : p->pous)
                if (pou.kind == domain::PouKind::ProgramUnit
                    && std::find(pou.sections.begin(), pou.sections.end(), section) != pou.sections.end())
                    (void)pane->selectEntry(p->strings.text(pou.name));
        };
    };
    const auto moveItems = [&] {
        if (!project || !treeModel_ || treeModel_->sectionOf(node) == domain::kNoIndex) return;
        const bool up = ExecutionOrderWiring::canMoveUp(*project, *treeModel_, node);
        const bool down = ExecutionOrderWiring::canMoveDown(*project, *treeModel_, node);
        std::string raison;
        if (!document) raison = kNoDocument;
        else if (domain::taskOf(*project, treeModel_->sectionOf(node)) == 0)
            raison = "le corps d'un DFB ne s'ordonne pas";
        m.add("Monter dans l'ordre d'ex\xC3\xA9" "cution", Icon::Collapse, legacy(ActionMoveUp), {},
              raison.empty() ? (up ? std::string{} : "d\xC3\xA9j\xC3\xA0 en t\xC3\xAAte de t\xC3\xA2" "che") : raison);
        m.add("Descendre dans l'ordre d'ex\xC3\xA9" "cution", Icon::Expand, legacy(ActionMoveDown), {},
              raison.empty() ? (down ? std::string{} : "d\xC3\xA9j\xC3\xA0 en queue de t\xC3\xA2" "che") : raison);
    };
    // Un onglet de l'API choisi sur une ligne, puis son action (runAction).
    const auto typesDo = [this](std::string name, int action) {
        return [this, name, action] {
            openApiPane("types");
            if (auto* pane = paneOf<DerivedTypesPane>(apiTab("types"))) {
                (void)pane->selectType(name);
                pane->runAction(action);
            }
        };
    };
    const auto dfbDo = [this](std::string name, int action) {
        return [this, name, action] {
            openApiPane("dfb");
            if (auto* pane = paneOf<DfbPane>(apiTab("dfb"))) {
                (void)pane->selectBlock(name);
                if (action > 0) pane->runAction(action);
            }
        };
    };
    const auto unitsDo = [this](std::string name, int action) {
        return [this, name, action] {
            openApiPane("unites");
            if (auto* pane = paneOf<UnitsPane>(apiTab("unites"))) {
                (void)pane->selectUnit(name);
                if (action > 0) pane->runAction(action);
            }
        };
    };
    const auto tablesDo = [tablesPane](std::size_t table, int action) {
        return [tablesPane, table, action] {
            if (auto* pane = tablesPane()) {
                (void)pane->selectTable(table);
                pane->runAction(action);
            }
        };
    };
    // Mettre a jour depuis la bibliotheque : seulement s'il y a plus recent.
    const auto updateItem = [&](const std::string& type, std::function<void()> fn) {
        const std::string newer = newerInLibrary(type);
        m.add("Mettre \xC3\xA0 jour depuis la biblioth\xC3\xA8que" + (newer.empty() ? std::string{} : " (v" + newer + ")"), Icon::Refresh,
              std::move(fn), {}, newer.empty() ? std::string("pas de version plus r\xC3\xA9" "cente") : noDoc);
    };
    // Les touches de creation de l'API (+ Unite, + Section...).
    const auto createItems = [&] {
        m.add("+ Unit\xC3\xA9 de programme" + kDots, Icon::Program, trigger("create.unit"), {}, noDoc);
        m.add("+ Section" + kDots, Icon::Section, trigger("create.section"), {}, noDoc);
        m.add("+ Bloc DFB" + kDots, Icon::FunctionBlock, trigger("create.dfb"), {}, noDoc);
        m.add("+ Type d\xC3\xA9riv\xC3\xA9" + kDots, Icon::DerivedType, trigger("create.ddt"), {}, noDoc);
        m.add("+ Variable" + kDots, Icon::Variable, trigger("create.variable"), {}, noDoc);
        m.add("+ Table d'animation", Icon::AnimationTable, newTable, {}, noDoc);
    };

    // ---- l'IHM : ses listes rangees en dossiers (lot 21) -------------------------------
    const auto hmiApply = [this](core::CommandPtr cmd) {
        if (cmd) app_.apply(std::move(cmd), false);
    };
    // Un nouveau dossier dans `parent` ("" : a la racine de la liste) - son nom demande.
    const auto newFolder = [this, hmiApply](int list, std::string parent) {
        return [this, hmiApply, list, parent] {
            askApiText("Cr\xC3\xA9" "er un dossier", parent.empty() ? std::string("Un dossier \xC3\xA0 la racine de la liste.")
                                                                     : "Un dossier dans " + parent + ".",
                       "Nouveau dossier", [this, hmiApply, list, parent](const std::string& name) {
                           auto doc = app_.hmi();
                           if (!doc || name.empty()) return;
                           const std::string path = parent.empty() ? name : parent + "/" + name;
                           bool ok = false;
                           std::string why;
                           hmiApply(hmi::changeProject(doc, "Cr\xC3\xA9" "er le dossier " + path, [&](hmi::Project& p) {
                               ok = fold::addFolder(p, static_cast<fold::List>(list), path, &why);
                           }));
                           if (status_)
                               status_->setTransientMessage(ok ? "Dossier " + path + " cr\xC3\xA9\xC3\xA9 (Ctrl+Z le retire)."
                                                               : "Dossier non cr\xC3\xA9\xC3\xA9 : " + why,
                                                            6.0, ok ? StatusBar::Severity::Success : StatusBar::Severity::Warning);
                       });
        };
    };
    const auto renameFolder = [this, hmiApply](int list, std::string path) {
        return [this, hmiApply, list, path] {
            const std::string leaf = hmi::types::folderLeaf(path);
            askApiText("Renommer le dossier " + leaf, "Son nouveau nom : ce qu'il contient le suit.", leaf,
                       [this, hmiApply, list, path](const std::string& name) {
                           auto doc = app_.hmi();
                           if (!doc || name.empty()) return;
                           const std::string parent = hmi::types::folderParent(path);
                           const std::string to = parent.empty() ? name : parent + "/" + name;
                           bool ok = false;
                           std::string why;
                           hmiApply(hmi::changeProject(doc, "Renommer le dossier " + path, [&](hmi::Project& p) {
                               ok = fold::renameFolder(p, static_cast<fold::List>(list), path, to, &why);
                           }));
                           if (!ok && status_) status_->setTransientMessage("Dossier non renomm\xC3\xA9 : " + why, 6.0, StatusBar::Severity::Warning);
                       });
        };
    };
    const auto removeFolder = [this, hmiApply](int list, std::string path) {
        return [this, hmiApply, list, path] {
            auto doc = app_.hmi();
            if (!doc) return;
            bool ok = false;
            hmiApply(hmi::changeProject(doc, "Supprimer le dossier " + path,
                                        [&](hmi::Project& p) { ok = fold::removeFolder(p, static_cast<fold::List>(list), path); }));
            if (ok && status_)
                status_->setTransientMessage("Dossier " + path + " supprim\xC3\xA9 : ce qu'il contenait remonte d'un cran (Ctrl+Z le remet).", 6.0,
                                             StatusBar::Severity::Success);
        };
    };
    // La liste IHM d'un element (vue, script, type) et son dossier ; -1 : aucune.
    const auto hmiPlace = [&](int& list, std::string& folder) {
        list = treeModel_ ? treeModel_->hmiListOf(node) : -1;
        folder.clear();
        if (list < 0 || !hmiDoc || !treeModel_) return;
        const auto id = static_cast<hmi::Id>(kind == NK::HmiView ? treeModel_->hmiViewOf(node) : treeModel_->hmiIdOf(node));
        folder = fold::folderOf(hmiDoc->project, static_cast<fold::List>(list), id);
    };
    const auto hmiName = [&](const auto& list, std::uint64_t id) -> std::string {
        for (const auto& it : list)
            if (it.id == id) return it.name;
        return {};
    };

    // =====================================================================================
    //  L'EN-TETE : ce sur quoi on a clique (le texte du noeud, coupe).
    // =====================================================================================
    if (treeModel_) {
        const std::string label = treeModel_->text(node);
        if (!label.empty()) m.heading(clipped(label, 64));
    }

    // ---- Lot API 8 : l'arbre du projet (la rangee des outils : un outil par entree, au clavier aussi) ----
    if (kind == NK::ToolRow && treeModel_) {
        const bool hmiRow = index == 1;
        m.heading(hmiRow ? "Outils de l'IHM" : "Outils de l'API");
        const auto chips = treeModel_->style(node).chips;
        for (std::size_t k = 0; k < chips.size(); ++k)
            m.add(chips[k].label, chips[k].icon, [this, hmiRow, k] { (void)openTreeTool(hmiRow, k); });
    }
    // Epingler / Desepingler (en haut de l'arbre) ; un raccourci s'ouvre aussi d'ici.
    {
        const auto target = treeModel_ ? treeModel_->shortcutTarget(node) : ui::kInvalidNode;
        if (target != ui::kInvalidNode) m.add("Ouvrir", Icon::Open, [this, target] { onTreeSelection(target); });
        const auto pinned = target != ui::kInvalidNode ? target : node;
        if (treePinnable(pinned)) {
            const bool on = isTreePinned(pinned);
            m.add(on ? "D\xC3\xA9s\xC3\xA9pingler" : "\xC3\x89pingler en haut", Icon::Star, [this, pinned, on] { (void)pinTreeNode(pinned, !on); });
            m.separator();
        }
    }
    // ---- fin Lot API 8 : l'arbre du projet ----

    const bool isHmi = isHmiNode(node) || kind == NK::HmiListFolder;
    switch (kind) {
    // =====================================================================================
    //  L'API
    // =====================================================================================
    case NK::ApiFolder:
        m.add("Ouvrir le tableau de bord", Icon::Cpu, apiPane("api"));
        m.separator();
        createItems();
        m.separator();
        m.add("R\xC3\xA9" "analyser les fichiers", Icon::Refresh, trigger("analyze.run"));   // Lot API 8 : F5 est Simuler
        m.separator();
        // 1.8.0 : importer (suivi par sa fenetre, en tache de fond), exporter lisible.
        m.add("Importer un .XPG (nouveau MAST)" + kDots, Icon::Program, trigger("file.importMast"), {}, noDoc);
        m.add("Importer la configuration mat\xC3\xA9rielle (.XHW)" + kDots, Icon::Rack, [this] { importHardwareIntoProject(); }, {}, noDoc);
        m.separator();
        m.add("Exporter le programme lisible" + kDots, Icon::Document, [this] { askProgramExport(0); }, "Ctrl+Maj+E",
              project ? std::string{} : "aucun projet");
        m.add("Vers Control Expert" + kDots, Icon::Export, trigger("project.exportSources"), {}, project ? std::string{} : "aucun projet");
        break;

    case NK::ConfigurationFolder:
    case NK::Cpu:
    case NK::RackFolder:
    case NK::Rack:
    case NK::HwModule:
    case NK::ApiChannels:
    case NK::ApiNetwork:
    case NK::ApiMemory:
        m.add(kind == NK::HwModule ? "Ses propri\xC3\xA9t\xC3\xA9s" : "Ouvrir", Icon::Open, [this, node] { onTreeSelection(node); });
        m.separator();
        m.add("+ Rack" + kDots, Icon::Rack, trigger("create.rack"), {}, noDoc);
        m.add("+ Module" + kDots, Icon::Module, trigger("create.module"), {}, noDoc);
        m.add("Importer le .XHW" + kDots, Icon::Open, [this] { importHardwareIntoProject(); });
        break;

    case NK::TaskFolder: {
        bool fast = false;
        if (project)
            for (const auto& t : project->tasks) fast = fast || text(t.name) == "FAST";
        const auto tasksDo = [this](int action) {
            return [this, action] {
                openApiPane("taches");
                if (auto* pane = paneOf<TasksPane>(apiTab("taches"))) pane->runAction(action);
            };
        };
        m.add("Ouvrir l'onglet T\xC3\xA2" "ches", Icon::Task, apiPane("taches"));
        m.separator();
        m.add("+ T\xC3\xA2" "che FAST", Icon::Task, tasksDo(TasksPane::AAddFast), {}, !document ? kNoDocument : fast ? "FAST existe d\xC3\xA9j\xC3\xA0" : "");
        m.add("+ T\xC3\xA2" "che AUX", Icon::Task, tasksDo(TasksPane::AAddAux), {}, noDoc);
        m.add("Voir l'ordre d'ex\xC3\xA9" "cution", Icon::Section, apiPane("ordre"));
        break;
    }
    case NK::Task: {
        const std::string name = project && index < project->tasks.size() ? text(project->tasks[index].name) : std::string{};
        m.add("Ouvrir", Icon::Open, [this, node] { onTreeSelection(node); });
        m.add("Voir l'ordre d'ex\xC3\xA9" "cution", Icon::Section, apiPane("ordre"));
        m.add("Simuler", Icon::Play, [this] { runSimulationTransport("sim.run"); }, {},
              app_.simulation().state() == SimulationHost::State::Running ? "d\xC3\xA9j\xC3\xA0 en marche" : "");
        m.separator();
        addRemove("Supprimer la t\xC3\xA2" "che" + kDots,
                  [this, name] {
                      app_.menus().ShowDialog(
                          std::make_unique<MessageDialog>("Supprimer la t\xC3\xA2" "che " + name + " ?",
                                                          "Ses sections n'ont plus de t\xC3\xA2" "che ou elles tournent. Ctrl+Z la remet.",
                                                          MessageDialog::Icon::Question, "Supprimer"),
                          [this, name](const menu::DialogResult& r) {
                              if (!r.accepted()) return;
                              openApiPane("taches");
                              if (auto* pane = paneOf<TasksPane>(apiTab("taches")); pane && pane->selectTask(name))
                                  pane->runAction(TasksPane::ARemove);
                          });
                  },
                  !document ? kNoDocument : name == "MAST" ? "MAST ne se supprime pas" : "");
        break;
    }

    case NK::ExecOrderFolder:
    case NK::ExecOrderTask: {
        const auto orderDo = [this](int action) {
            return [this, action] {
                openApiPane("ordre");
                if (auto* pane = paneOf<ExecutionOrderPane>(apiTab("ordre"))) pane->runAction(action);
            };
        };
        m.add("Ouvrir l'onglet Ordre d'ex\xC3\xA9" "cution", Icon::Section, apiPane("ordre"));
        m.add("V\xC3\xA9rifier l'ordre", Icon::Ok, orderDo(ExecutionOrderPane::ACheck));
        break;
    }
    case NK::ExecStep: {
        const auto section = treeModel_ ? treeModel_->sectionOf(node) : domain::kNoIndex;
        const bool known = project && section < project->sections.size();
        m.add("Ouvrir la section", Icon::Document, [this, section] { openDocument(section); }, {}, known ? std::string{} : "section introuvable");
        m.add("Voir dans l'ordre d'ex\xC3\xA9" "cution", Icon::Section, [this, node] { (void)routeApiNode(node); });
        m.separator();
        moveItems();
        if (known) {
            m.separator();
            compareAndExport(section, true);
            iconItem();
            m.separator();
            addRename("Renommer la section" + kDots, rename("section", text(project->sections[section].name)), noDoc);
        }
        break;
    }

    case NK::TypesFolder:
        m.add("Ouvrir l'onglet Types d\xC3\xA9riv\xC3\xA9s", Icon::DerivedType, apiPane("types"));
        m.separator();
        m.add("+ Type d\xC3\xA9riv\xC3\xA9" + kDots, Icon::DerivedType, trigger("create.ddt"), {}, noDoc);
        m.add("Importer depuis la biblioth\xC3\xA8que partag\xC3\xA9" "e" + kDots, Icon::Library, legacy(ActionImportFromLibrary), {}, noDoc);
        break;
    case NK::DerivedType: {
        if (!project || index >= project->derivedTypes.size()) break;
        const std::string name = text(project->derivedTypes[index].name);
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.separator();
        m.add("+ Champ" + kDots, Icon::Variable, typesDo(name, DerivedTypesPane::AAddField), {}, noDoc);
        m.add("Voir les variables de ce type", Icon::Variable, [this, name] { onApiRequest("variables:" + name); });
        updateItem(name, typesDo(name, DerivedTypesPane::AUpdate));
        m.add("Publier dans la biblioth\xC3\xA8que partag\xC3\xA9" "e" + kDots, Icon::Export, legacy(ActionPublishToLibrary));
        iconItem();
        m.separator();
        addRename("Renommer" + kDots, rename("ddt", name), noDoc);
        addEntityRemove();
        break;
    }
    case NK::DerivedField: {
        if (!project || index >= project->variables.size()) break;
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.add("Copier le nom", Icon::Document, copy(text(project->variables[index].name)));
        if (treeModel_ && ProjectTreeModel::holdsMembers(node) && explorer_)
            m.add(explorer_->isExpanded(node) ? "Replier ses membres" : "D\xC3\xA9plier ses membres", Icon::Expand,
                  [this, node] { explorer_->toggle(node); });
        m.separator();
        // ---- Lot API 8 : renommer un champ de DDT (le dialogue, genre ddt-champ ; F2 aussi) ----
        {
            const auto& fv = project->variables[index];
            const bool field = fv.scope == domain::VariableScope::DerivedMember && fv.owner < project->derivedTypes.size();
            addRename("Renommer" + kDots,
                      field ? rename("ddt-champ", text(project->derivedTypes[fv.owner].name) + "." + text(fv.name)) : std::function<void()>{},
                      !field ? std::string("pas un champ de type d\xC3\xA9riv\xC3\xA9") : noDoc);
        }
        // ---- fin Lot API 8 ----
        addEntityRemove();
        break;
    }

    case NK::DfbFolder:
        m.add("Ouvrir l'onglet Blocs DFB", Icon::FunctionBlock, apiPane("dfb"));
        m.separator();
        m.add("+ Bloc DFB" + kDots, Icon::FunctionBlock, trigger("create.dfb"), {}, noDoc);
        m.add("Importer depuis la biblioth\xC3\xA8que partag\xC3\xA9" "e" + kDots, Icon::Library, legacy(ActionImportFromLibrary), {}, noDoc);
        break;
    case NK::DfbType: {
        if (!project || index >= project->pous.size()) break;
        const std::string name = text(project->pous[index].name);
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.separator();
        m.add("+ Instance" + kDots, Icon::Variable, dfbDo(name, DfbPane::AInstance), {}, noDoc);
        codeMenu(index);
        m.add("Voir les instances", Icon::Search, [this, name, dfbDo] {
            dfbDo(name, 0)();
            if (status_) status_->setTransientMessage("Les instances de " + name + " : en bas de l'onglet Blocs DFB.", 6.0);
        });
        updateItem(name, dfbDo(name, DfbPane::AUpdate));
        m.add("Publier dans la biblioth\xC3\xA8que partag\xC3\xA9" "e" + kDots, Icon::Export, legacy(ActionPublishToLibrary));
        iconItem();
        m.separator();
        addRename("Renommer" + kDots, rename("dfb", name), noDoc);
        addEntityRemove();
        break;
    }
    case NK::DfbSectionsFolder:
        codeMenu(index);
        m.add("+ Section" + kDots, Icon::Section, trigger("create.section"), {}, noDoc);
        break;
    case NK::DfbInputs:
    case NK::DfbOutputs:
    case NK::DfbInOut:
    case NK::DfbPublicVars:
    case NK::DfbPrivateVars:
    case NK::ProgramUnitVars: {
        if (!project || index >= project->pous.size()) break;
        const auto& pou = project->pous[index];
        const std::string name = text(pou.name);
        const bool unit = pou.kind == domain::PouKind::ProgramUnit;
        m.add("Ouvrir", Icon::Open, unit ? std::function<void()>(unitsDo(name, 0)) : std::function<void()>(dfbDo(name, 0)));
        m.add("+ Variable" + kDots, Icon::Variable, unit ? std::function<void()>(unitsDo(name, UnitsPane::AAddVariable)) : std::function<void()>(trigger("create.variable")), {}, noDoc);
        break;
    }
    case NK::DfbVariable: {
        if (!project || index >= project->variables.size()) break;
        const auto& var = project->variables[index];
        const std::string name = text(var.name);
        const bool unit = var.owner < project->pous.size() && project->pous[var.owner].kind == domain::PouKind::ProgramUnit;
        const std::string owner = var.owner < project->pous.size() ? text(project->pous[var.owner].name) : std::string{};
        if (unit)
            m.add("Ouvrir", Icon::Open, [this, owner, name] {
                openApiPane("unites");
                if (auto* pane = paneOf<UnitsPane>(apiTab("unites"))) (void)pane->selectVariable(owner, name);
            });
        else
            m.add("Ouvrir", Icon::Open, dfbDo(owner, 0));
        m.add("Voir les usages", Icon::Search, [this, index] { openVariable(index); });
        m.separator();
        if (unit) {
            tableMenu(selectedLines());
            simulationItems(owner + "." + name);
        }
        m.add("Copier le nom", Icon::Document, copy(unit ? owner + "." + name : name));
        m.separator();
        if (unit) addRename("Renommer" + kDots, rename("variable", owner + "." + name), noDoc);
        else keys.renameWhy = "une variable d'un bloc DFB ne se renomme pas d'ici";
        addEntityRemove();
        break;
    }

    case NK::UnitsFolder:
        m.add("Ouvrir l'onglet Unit\xC3\xA9s de programme", Icon::Program, apiPane("unites"));
        m.separator();
        m.add("+ Unit\xC3\xA9 de programme" + kDots, Icon::Program, trigger("create.unit"), {}, noDoc);
        m.add("+ Section" + kDots, Icon::Section, trigger("create.section"), {}, noDoc);
        break;
    case NK::ProgramUnit: {
        if (!project || index >= project->pous.size()) break;
        const std::string name = text(project->pous[index].name);
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.separator();
        m.add("+ Section" + kDots, Icon::Section, unitsDo(name, UnitsPane::AAddSection), {}, noDoc);
        m.add("+ Variable" + kDots, Icon::Variable, unitsDo(name, UnitsPane::AAddVariable), {}, noDoc);
        codeMenu(index);
        m.add("Voir dans l'ordre d'ex\xC3\xA9" "cution", Icon::Section, [this, name] { onApiRequest("ordre:" + name); });
        m.separator();
        m.add("Exporter l'unit\xC3\xA9 (lisible)" + kDots, Icon::Export, [this, name] { askProgramExport(1, name); });
        iconItem();
        m.separator();
        addRename("Renommer" + kDots, rename("unite", name), noDoc);
        addEntityRemove();
        break;
    }
    case NK::Section:
    case NK::DfbSection: {
        if (!project || index >= project->sections.size()) break;
        const std::string name = text(project->sections[index].name);
        const bool inTask = domain::taskOf(*project, index) != 0;
        m.add("Ouvrir", Icon::Document, [this, index] { openDocument(index); });
        m.add("Ouvrir dans une fen\xC3\xAAtre", Icon::Screen, [this, index] {
            openDocument(index);
            if (!centre_) return;
            // Son onglet, retrouve par sa page (pas "l'onglet courant") ; deja
            // dans sa fenetre : openDocument l'a montree, rien a detacher.
            const std::string id = "analysis.doc." + std::to_string(index);
            for (std::size_t i = 0; i < centre_->tabCount(); ++i)
                if (const auto* page = centre_->page(i); page && page->id() == id) {
                    (void)detachTab(i);
                    return;
                }
        });
        m.add("Voir dans l'ordre d'ex\xC3\xA9" "cution", Icon::Section, showInOrder(index), {},
              inTask ? std::string{} : "le corps d'un DFB n'est dans aucune t\xC3\xA2" "che");
        moveItems();
        m.separator();
        compareAndExport(index, kind == NK::Section);
        iconItem();
        m.separator();
        addRename("Renommer" + kDots, rename("section", name), noDoc);
        addEntityRemove();
        break;
    }

    case NK::VariablesFolder:
    case NK::ElementaryFolder:
    case NK::DdtInstanceFolder:
    case NK::DfbInstanceFolder:
        m.add("Ouvrir l'onglet Variables", Icon::Variable, apiPane("variables"), "Ctrl+1");
        m.separator();
        m.add("+ Variable" + kDots, Icon::Variable, trigger("create.variable"), {}, noDoc);
        m.add("Voir les variables pas utilis\xC3\xA9" "es", Icon::Filter, [this] { onApiRequest("variables-inutilisees"); });
        m.add("Exporter CSV", Icon::Export, [this] {
            openApiPane("variables");
            if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) pane->runAction(VariablesPane::AExportCsv);
        });
        break;
    case NK::ListVariable: {
        const auto at = treeModel_ ? treeModel_->variableOf(node) : domain::kNoIndex;
        if (!project || at >= project->variables.size()) break;
        const std::string name = text(project->variables[at].name);
        m.add("Ouvrir", Icon::Open, [this, name] {
            openApiPane("variables");
            if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) (void)pane->selectVariable(name);
        });
        m.add("Voir les usages", Icon::Search, [this, at] { openVariable(at); });
        m.separator();
        tableMenu(selectedLines());
        simulationItems(name);
        m.add("Copier le nom", Icon::Document, copy(name));
        m.separator();
        addRename("Renommer" + kDots, rename("variable", name), noDoc);
        addRemove("Supprimer" + kDots, [this, at] { confirmAndDelete(domain::EntityKind::Variable, at); }, noDoc);
        break;
    }
    case NK::MemberNode: {
        if (!treeModel_ || !project) break;
        const auto root = treeModel_->memberRootOf(node);
        const std::string path = treeModel_->memberPathOf(node);
        const bool group = treeModel_->memberIsGroup(node);
        std::string full = path;          // le nom que la simulation et les tables connaissent
        bool usable = false;
        if (root < project->variables.size()) {
            const auto& var = project->variables[root];
            if (var.scope == domain::VariableScope::Global) usable = true;
            else if (var.scope != domain::VariableScope::DerivedMember && var.owner < project->pous.size()
                     && project->pous[var.owner].kind == domain::PouKind::ProgramUnit) {
                full = text(project->pous[var.owner].name) + "." + path;
                usable = true;
            }
        }
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.add("Copier le chemin", Icon::Document, copy(full));
        if (usable && !group) {
            m.separator();
            tableMenu(selectedLines());
            simulationItems(full);
        }
        break;
    }

    case NK::SubroutinesFolder:
        m.add("Ouvrir l'onglet Sous-routines", Icon::Section, apiPane("sous-routines"));
        m.add("+ Sous-routine" + kDots, Icon::Section, [this] {
            openApiPane("sous-routines");
            if (auto* pane = paneOf<SubroutinesPane>(apiTab("sous-routines"))) pane->runAction(SubroutinesPane::AAdd);
        }, {}, noDoc);
        break;
    case NK::Subroutine: {
        const auto sr = treeModel_ ? treeModel_->subroutineOf(node) : domain::kNoIndex;
        if (!project || sr >= project->sections.size()) break;
        const std::string name = text(project->sections[sr].name);
        m.add("Ouvrir (le code et ses appels)", Icon::Code, [this, sr] { openSubroutine(sr); });
        m.add("Ouvrir le code seul", Icon::Document, [this, sr] { openDocument(sr); });
        m.separator();
        addRename("Renommer" + kDots, rename("section", name), noDoc);
        addRemove("Supprimer" + kDots, [this, sr] { confirmAndDelete(domain::EntityKind::Section, sr); }, noDoc);
        break;
    }

    case NK::TablesFolder:
        m.add("Ouvrir l'onglet Tables d'animation", Icon::AnimationTable, apiPane("tables"));
        m.add("+ Table d'animation", Icon::AnimationTable, newTable, {}, noDoc);
        break;
    case NK::AnimationTable: {
        if (!project || index >= project->animationTables.size()) break;
        const std::string name = text(project->animationTables[index].name);
        const std::size_t table = index;
        m.add("Ouvrir", Icon::Open, [this, node] { (void)routeApiNode(node); });
        m.separator();
        m.add("+ Variable de l'automate" + kDots, Icon::Variable, tablesDo(table, AnimationTablesPane::AAddApi), {}, noDoc);
        if (hmiDoc) m.add("+ Variable IHM" + kDots, Icon::Variable, tablesDo(table, AnimationTablesPane::AAddHmi), {}, noDoc);
        m.add("Dupliquer", Icon::Document, tablesDo(table, AnimationTablesPane::ADuplicate), {}, noDoc);
        m.separator();
        addRename("Renommer" + kDots, rename("table", name), noDoc);
        addRemove("Supprimer" + kDots,
                  [this, name, table, tablesDo] {
                      const auto remove = tablesDo(table, AnimationTablesPane::ADelete);
                      app_.menus().ShowDialog(
                          std::make_unique<MessageDialog>("Supprimer la table " + name + " ?",
                                                          "Ses lignes partent avec elle ; les variables restent dans le projet. Ctrl+Z la remet.",
                                                          MessageDialog::Icon::Question, "Supprimer"),
                          [remove](const menu::DialogResult& r) { if (r.accepted()) remove(); });
                  },
                  noDoc);
        break;
    }

    case NK::ApiSimulation: {
        using State = SimulationHost::State;
        const auto& host = app_.simulation();
        const bool attached = host.attached();
        const auto state = host.state();
        const bool running = attached && state == State::Running;
        const bool halted = attached && state == State::Halted;
        const std::string haltedWhy = halted ? "arr\xC3\xAAt\xC3\xA9" "e sur un d\xC3\xA9" "faut : Arr\xC3\xAAter d'abord" : std::string{};
        // Lot API 8 : Centre de simulation - le noeud s'appelle Automate ; F9 ouvre la Vue d'ensemble.
        m.add("Ouvrir l'onglet Automate", Icon::Play, apiPane("simulation"));
        m.add("Ouvrir la vue d'ensemble", Icon::Info, [this] { (void)openSimCenter("ensemble"); }, "F9");
        m.separator();
        m.add(running ? "Pause" : "Simuler", running ? Icon::Pause : Icon::Play,
              [this, running] { runSimulationTransport(running ? "sim.pause" : "sim.run"); }, {}, haltedWhy);
        m.add("Un cycle", Icon::StepOnce, [this] { runSimulationTransport("sim.step"); }, {}, haltedWhy);
        m.add("Arr\xC3\xAAter", Icon::Stop, [this] { runSimulationTransport("sim.stop"); }, {},
              attached && state != State::Stopped ? std::string{} : "d\xC3\xA9j\xC3\xA0 arr\xC3\xAAt\xC3\xA9" "e");
        m.separator();
        m.add("Exporter les for\xC3\xA7" "ages" + kDots, Icon::Export, [this] {
            openApiPane("simulation");
            if (auto* pane = paneOf<SimulationPane>(apiTab("simulation"))) pane->runAction(SimulationPane::AExport);
        });
        break;
    }
    case NK::ApiStatistics: {
        const auto statsDo = [this](int action) {
            return [this, action] {
                openApiPane("statistiques");
                if (auto* pane = paneOf<StatisticsPane>(apiTab("statistiques"))) pane->runAction(action);
            };
        };
        m.add("Ouvrir l'onglet Statistiques", Icon::Chart, apiPane("statistiques"), "Ctrl+5");
        m.add("Recalculer", Icon::Refresh, statsDo(StatisticsPane::ARecompute));
        m.add("Exporter CSV", Icon::Export, statsDo(StatisticsPane::AExport));
        break;
    }

    // ---- Lot API 8 : Centre de simulation ----
    //  Le dossier Simulation et ses onglets (Automate et IHM gardent leurs menus) :
    //  ouvrir, ce que l'onglet sait faire, puis Simuler / Pause, Un cycle, Arreter.
    case NK::SimFolder:
    case NK::SimOverview:
    case NK::SimEquipment:
    case NK::SimDebug:
    case NK::SimForcing:
    case NK::SimTrends:
    case NK::SimJournal: {
        using State = SimulationHost::State;
        const auto& host = app_.simulation();
        const bool attached = host.attached();
        const auto state = host.state();
        const bool running = attached && state == State::Running;
        const bool halted = attached && state == State::Halted;
        const std::string haltedWhy = halted ? "arr\xC3\xAAt\xC3\xA9" "e sur un d\xC3\xA9" "faut : Arr\xC3\xAAter d'abord" : std::string{};
        const auto open = [this](std::string key) { return [this, key] { (void)openSimCenter(key); }; };
        switch (kind) {
            case NK::SimEquipment: m.add("Ouvrir les \xC3\xA9quipements", Icon::Globe, open("equipements")); break;
            case NK::SimDebug:     m.add("Ouvrir le d\xC3\xA9" "bogage", Icon::Analyze, open("debogage")); break;
            case NK::SimForcing:
                m.add("Ouvrir les for\xC3\xA7" "ages", Icon::Force, open("forcages"));
                m.add("Tout rel\xC3\xA2" "cher", Icon::Lock, [this] {
                    (void)openSimCenter("forcages");
                    if (auto* pane = dynamic_cast<SimForcingPane*>(simCenterPane("forcages"))) {
                        pane->refresh();
                        pane->releaseAll();
                    }
                });
                break;
            case NK::SimTrends:
                m.add("Ouvrir les courbes", Icon::Chart, open("courbes"));
                m.add("Retirer toutes les courbes", Icon::Close, [this] { simCenterModel().clearTrends(); });
                break;
            case NK::SimJournal:
                m.add("Ouvrir le journal", Icon::Document, open("journal"));
                m.add("Effacer le journal", Icon::Close, [this] {
                    app_.simJournal().clear();
                    if (auto* pane = dynamic_cast<SimJournalPane*>(simCenterPane("journal"))) pane->refresh();
                });
                break;
            default:               m.add("Ouvrir la vue d'ensemble", Icon::Info, open("ensemble")); break;
        }
        m.separator();
        m.add(running ? "Pause" : "Simuler", running ? Icon::Pause : Icon::Play,
              [this, running] { simCenterGo(running ? "sim.pause" : "sim.run"); }, {}, haltedWhy);
        m.add("Un cycle", Icon::StepOnce, [this] { simCenterGo("sim.step"); }, {}, haltedWhy);
        m.add("Arr\xC3\xAAter", Icon::Stop, [this] { simCenterGo("sim.stop"); }, {},
              attached && state != State::Stopped ? std::string{} : "d\xC3\xA9j\xC3\xA0 arr\xC3\xAAt\xC3\xA9" "e");
        break;
    }
    // ---- fin Lot API 8 : Centre de simulation ----

    case NK::Macro: {
        const bool doc = document != nullptr;
        m.add("Lancer", Icon::Play, legacy(ActionMacroLaunch), {}, doc ? std::string{} : "ouvre un projet d'abord");
        m.add("Modifier le code", Icon::Code, legacy(ActionMacroEdit));
        m.add("Voir dans l'onglet Macros", Icon::Folder, legacy(ActionMacroShow));
        m.separator();
        addRename("Renommer" + kDots, legacy(ActionMacroRename));
        addRemove("Supprimer (dans la corbeille)", legacy(ActionMacroDelete));
        break;
    }
    case NK::MacroFolder:
    case NK::MacroSubFolder: {
        std::string macroFolder;
        const bool known = treeModel_ && treeModel_->macroFolderOf(node, macroFolder);
        m.add("Ouvrir l'onglet Macros", Icon::Folder, legacy(ActionMacroShow));
        m.add("Nouvelle macro ici" + kDots, Icon::Document, legacy(ActionMacroNew));
        m.add("Nouveau dossier ici" + kDots, Icon::Folder, legacy(ActionMacroNewFolder));
        if (known && !macroFolder.empty()) {
            m.separator();
            addRename("Renommer le dossier" + kDots, legacy(ActionMacroRename));
            addRemove("Supprimer le dossier", legacy(ActionMacroDelete));
        }
        break;
    }

    // =====================================================================================
    //  L'IHM
    // =====================================================================================
    case NK::HmiFolder:
        m.add("Ouvrir la configuration", Icon::Settings, hmiPane("config"));
        m.separator();
        m.add("+ Vue" + kDots, Icon::Screen, [this] { askNewHmiView("vue"); });
        m.add("+ Popup" + kDots, Icon::Screen, [this] { askNewHmiView("popup"); });
        m.add("Importer des vues" + kDots, Icon::Open, [this] { askHmiImportViews(); });
        m.separator();
        m.add("G\xC3\xA9n\xC3\xA9rer", Icon::Analyze, hmiPane("generer"));
        m.add("Simuler l'IHM", Icon::Play, hmiPane("simulation"));
        break;

    case NK::HmiViews:
    case NK::HmiViewFolder:
    case NK::HmiTemplateFolder:
    case NK::HmiSymbolsFolder: {
        // Le role d'une vue creee ici, et son nom dans le menu.
        std::string role = "vue", noun = "Vue";
        if (kind == NK::HmiViewFolder && index == 0) { role = "modele"; noun = "Mod\xC3\xA8le"; }
        else if (kind == NK::HmiViewFolder && index == 2) { role = "popup"; noun = "Popup"; }
        else if (kind == NK::HmiTemplateFolder && index == 0) { role = "modele"; noun = "\xC3\x89" "cran mod\xC3\xA8le"; }
        else if (kind == NK::HmiTemplateFolder && index == 1) { role = "entete"; noun = "En-t\xC3\xAAte"; }
        else if (kind == NK::HmiTemplateFolder && index == 2) { role = "pied"; noun = "Pied de page"; }
        else if (kind == NK::HmiSymbolsFolder) { role = "symbole"; noun = "Symbole"; }
        m.add("Ouvrir la liste", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        m.add("+ " + noun + kDots, Icon::Screen, [this, role] { askNewHmiView(role); }, {}, hmiDoc ? std::string{} : "pas d'IHM");
        int list = -1;
        std::string folder;
        if (treeModel_ && treeModel_->hmiListFolderOf(node, list, folder))
            m.add("Nouveau dossier" + kDots, Icon::Folder, newFolder(list, folder));
        m.separator();
        if (kind == NK::HmiSymbolsFolder) {
            // 1.11.2 (decision 162) : les symboles voyagent dans leur fichier a eux.
            // 1.11.2 (decision 174) : "Importer..." ouvre tout paquet (vues, symboles, types, fonctions, scripts).
            m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
            m.add("Exporter les symboles" + kDots, Icon::Export, [this] { askHmiExportSymbols(0); });
            break;
        }
        m.add("Importer des vues" + kDots, Icon::Open, [this] { askHmiImportViews(); });
        m.add("Exporter les vues" + kDots, Icon::Export, [this] { askHmiExportViews(0); });
        break;
    }
    case NK::HmiListFolder: {
        int list = -1;
        std::string folder;
        if (!treeModel_ || !treeModel_->hmiListFolderOf(node, list, folder)) break;
        m.add("Nouveau sous-dossier" + kDots, Icon::Folder, newFolder(list, folder));
        m.separator();
        addRename("Renommer le dossier" + kDots, renameFolder(list, folder));
        addRemove("Supprimer le dossier", removeFolder(list, folder));
        break;
    }
    case NK::HmiView: {
        const auto viewId = treeModel_ ? treeModel_->hmiViewOf(node) : 0;
        const auto* view = hmiDoc ? hmiDoc->project.view(static_cast<hmi::Id>(viewId)) : nullptr;
        if (!view) break;
        const std::string name = view->name;
        m.add("Ouvrir", Icon::Screen, [this, viewId] { openHmiView(viewId); });
        m.add("Ouvrir dans une fen\xC3\xAAtre", Icon::Screen, [this, viewId] {
            openHmiView(viewId);
            if (auto* page = hmiTab("vue:" + std::to_string(viewId)); page && centre_)
                if (const int at = centre_->indexOf(page); at >= 0) (void)detachTab(static_cast<std::size_t>(at));
        });
        m.add("Ses scripts", Icon::Code, [this, viewId] { openHmiScripts(viewId); });
        m.separator();
        m.add("Dupliquer", Icon::Document, [this, viewId] {
            auto doc = app_.hmi();
            if (!doc) return;
            hmi::Id made = hmi::kNoId;
            auto cmd = hmi::changeProject(doc, "Dupliquer la vue", [&](hmi::Project& p) {
                const auto* src = p.view(static_cast<hmi::Id>(viewId));
                if (!src) return;
                const hmi::Id from = src->id;
                const std::string copyName = hmi::uniqueViewName(p, src->name + "_copie");
                made = hmi::design::duplicateViewReplacing(p, from, copyName, {}, {});
            });
            if (cmd) app_.apply(std::move(cmd), false);
            if (made != hmi::kNoId && status_) {
                const auto* copyView = doc->project.view(made);
                status_->setTransientMessage("Vue dupliqu\xC3\xA9" "e : " + (copyView ? copyView->name : std::string{}) + " (Ctrl+Z la retire).", 6.0,
                                             StatusBar::Severity::Success);
            }
        });
        m.add("Dupliquer en rempla\xC3\xA7" "ant" + kDots, Icon::Document, [this, viewId] { askHmiDuplicateReplace(viewId); });
        if (view->role == "vue")
            m.add("Vue de d\xC3\xA9marrage", Icon::Play,
                  [this, viewId] {
                      auto doc = app_.hmi();
                      if (!doc) return;
                      if (auto cmd = hmi::changeProject(doc, "Vue de d\xC3\xA9marrage",
                                                        [&](hmi::Project& p) { p.config.startView = static_cast<hmi::Id>(viewId); }))
                          app_.apply(std::move(cmd), false);
                  },
                  {}, hmiDoc->project.config.startView == static_cast<hmi::Id>(viewId) ? "c'est d\xC3\xA9j\xC3\xA0 elle" : "");
        int list = -1;
        std::string folder;
        hmiPlace(list, folder);
        if (list >= 0) m.add("Nouveau dossier" + kDots, Icon::Folder, newFolder(list, folder));
        m.add("Enregistrer comme mod\xC3\xA8le" + kDots, Icon::Export, legacy(ActionHmiSaveTemplate));
        if (view->role == "symbole") {   // 1.11.2 (decision 162) : un symbole part dans un fichier de symboles
            m.add("Exporter le symbole" + kDots, Icon::Export, [this, viewId] { askHmiExportSymbols(viewId); });
            m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });   // 1.11.2 (scene 5 de MQ5)
        } else
            m.add("Exporter la vue" + kDots, Icon::Export, legacy(ActionHmiExportView));
        m.separator();
        addRename("Renommer" + kDots, rename("ihm-vue", name));
        addRemove("Supprimer" + kDots, [this, viewId] { askDeleteHmiView(viewId); });
        break;
    }
    case NK::HmiViewPart:
    case NK::HmiObjectItem:
    case NK::HmiObjectFamily:          // 1.10.2 (chantier A) : une famille, un parametre d'instance
    case NK::HmiObjectParam:
    case NK::HmiObjectMarker:
    case NK::HmiViewScript:
    case NK::HmiAnimation:
    case NK::HmiLayer:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        if (kind == NK::HmiViewScript) iconItem();   // 1.8.0
        break;
    case NK::HmiObject:
    case NK::HmiGroupEntry: {
        m.add("Ouvrir dans la vue", Icon::Open, [this, node] { openHmiNode(node); });
        const auto viewId = treeModel_ ? treeModel_->hmiViewOf(node) : 0;
        const auto* view = hmiDoc ? hmiDoc->project.view(static_cast<hmi::Id>(viewId)) : nullptr;
        const auto* object = view && treeModel_ ? view->object(static_cast<hmi::Id>(treeModel_->hmiObjectOf(node))) : nullptr;
        if (object) m.add("Copier le nom", Icon::Document, copy(view->name + "." + object->name));
        break;
    }

    case NK::HmiScripts:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        m.add("+ Script ST" + kDots, Icon::Code, [this] { askHmiNewScript(hmi::ScriptLang::ST); });
        m.add("+ Fonction" + kDots, Icon::Code, [this] { askHmiNewFunction(); });
        m.add("+ Variable IHM" + kDots, Icon::Variable, [this] { askHmiVariable(0); });
        break;
    case NK::HmiScriptsFolder: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        m.add("+ Script ST" + kDots, Icon::Code, [this] { askHmiNewScript(hmi::ScriptLang::ST); });
        m.add("+ Script C" + kDots, Icon::Code, [this] { askHmiNewScript(hmi::ScriptLang::C); });
        int list = -1;
        std::string folder;
        if (treeModel_ && treeModel_->hmiListFolderOf(node, list, folder))
            m.add("Nouveau dossier" + kDots, Icon::Folder, newFolder(list, folder));
        m.separator();   // 1.11.2 (decision 174)
        m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
        m.add("Exporter les scripts" + kDots, Icon::Export, [this] { askHmiExportPrograms(2, 0); });
        break;
    }
    case NK::HmiGeneralScript: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Code, [this, node] { openHmiNode(node); });
        int list = -1;
        std::string folder;
        hmiPlace(list, folder);
        if (list >= 0) m.add("Nouveau dossier" + kDots, Icon::Folder, newFolder(list, folder));
        iconItem();   // 1.8.0
        m.add("Exporter le script" + kDots, Icon::Export, [this, id] { askHmiExportPrograms(2, id); });   // 1.11.2 (decision 174)
        m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
        m.separator();
        addRename("Renommer" + kDots, [this, id] { askHmiRenameScript(id); });
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteScript(id); });
        break;
    }
    case NK::HmiFunctionsFolder:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("+ Fonction" + kDots, Icon::Code, [this] { askHmiNewFunction(); });
        m.separator();   // 1.11.2 (decision 174)
        m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
        m.add("Exporter les fonctions" + kDots, Icon::Export, [this] { askHmiExportPrograms(1, 0); });
        break;
    case NK::HmiFunction: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Code, [this, id] { openHmiFunctions(id); });
        m.add("Essayer" + kDots, Icon::Play, [this, id] { askHmiTryFunction(id); });
        iconItem();   // 1.8.0
        m.add("Exporter la fonction" + kDots, Icon::Export, [this, id] { askHmiExportPrograms(1, id); });   // 1.11.2 (decision 174)
        m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
        m.separator();
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteFunction(id); });
        break;
    }
    case NK::HmiVariablesFolder:
    case NK::HmiVarFolder:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("+ Variable IHM" + kDots, Icon::Variable, [this] { askHmiVariable(0); });
        break;
    case NK::HmiVariable: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        const std::string name = hmiDoc ? hmiName(hmiDoc->project.programs.variables, id) : std::string{};
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("Modifier" + kDots, Icon::Settings, [this, id] { askHmiVariable(id); });
        m.add("Voir les usages", Icon::Search, [this, name] {
            openHmiPane("rechercher");
            if (auto* pane = dynamic_cast<HmiFindPane*>(hmiTab("rechercher"))) {
                pane->setScopeView(hmi::kNoId);
                pane->setWholeWord(true);
                pane->setFind(name);
                pane->search();
            }
        }, {}, name.empty() ? "variable introuvable" : "");
        m.separator();
        tableMenu(selectedLines());
        if (!name.empty()) m.add("Copier le nom", Icon::Document, copy(name));
        m.separator();
        addRename("Renommer" + kDots, rename("ihm-variable", name), name.empty() ? "variable introuvable" : "");
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteVariable(id); });
        break;
    }
    case NK::HmiUsedVariable: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        if (treeModel_) m.add("Copier le chemin", Icon::Document, copy(treeModel_->hmiUsedPathOf(node)));
        break;
    }
    case NK::HmiTypesFolder:
    case NK::HmiTypeNode: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        int list = -1;
        std::string folder;
        if (kind == NK::HmiTypesFolder) {
            if (treeModel_) (void)treeModel_->hmiListFolderOf(node, list, folder);
        } else {
            hmiPlace(list, folder);
        }
        if (list >= 0) m.add("Nouveau dossier" + kDots, Icon::Folder, newFolder(list, folder));
        m.separator();   // 1.11.2 (decision 174) : les types voyagent (.xpgtypes)
        if (kind == NK::HmiTypeNode) {
            const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
            m.add("Exporter le type" + kDots, Icon::Export, [this, id] { askHmiExportPrograms(0, id); });
        } else {
            m.add("Exporter les types" + kDots, Icon::Export, [this] { askHmiExportPrograms(0, 0); });
        }
        m.add("Importer" + kDots, Icon::Open, [this] { askHmiImport(); });
        break;
    }
    case NK::HmiSysVar: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        if (index < hmi::pub::kSysVarCount) m.add("Copier le chemin", Icon::Document, copy("SYS." + std::string(hmi::pub::kSysVars[index].name)));
        break;
    }
    case NK::HmiInstViewVar:
    case NK::HmiInstVar: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        // Le chemin : Vue.Variable, ou Vue.Objet.Propriete (comme le volet).
        const hmi::View* v = nullptr;
        if (hmiDoc)
            for (const auto& cand : hmiDoc->project.views)
                if ((cand.id & ((1ull << 28) - 1)) == index) v = &cand;
        const auto sub = ProjectTreeModel::subOf(node);
        std::string path;
        if (v && kind == NK::HmiInstViewVar && sub < std::size(hmi::pub::kViewInfo)) {
            path = v->name + "." + std::string(hmi::pub::kViewInfo[sub].name);
        } else if (v && kind == NK::HmiInstVar) {
            for (const auto& o : v->objects) {
                if ((o.id & ((1ull << 20) - 1)) != (sub >> 8)) continue;
                const auto members = hmi::pub::objectMembers(o);
                if ((sub & 0xFFu) < members.size()) path = hmi::pub::instancePath(*v, o, members[sub & 0xFFu].name);
                break;
            }
        }
        if (!path.empty()) m.add("Copier le chemin", Icon::Document, copy(path));
        break;
    }
    // 1.11.1 (decision 108) : un parametre d'instance, une variable du groupe d'alarmes,
    // une variable d'une alarme (Vue.Objet.Alarmes.Defaut.Acked).
    case NK::HmiInstParam:
    case NK::HmiInstGroupVar:
    case NK::HmiInstAlarmVar: {
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        if (const auto path = treeModel_ ? treeModel_->hmiInstPathOf(node) : std::string{}; !path.empty())
            m.add("Copier le chemin", Icon::Document, copy(path));
        break;
    }

    case NK::HmiAlarm: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteAlarm(id); });
        break;
    }
    case NK::HmiRecipe: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        m.add("Exporter CSV" + kDots, Icon::Export, [this, id] { askHmiRecipeCsv(id, false); });
        m.add("Importer CSV" + kDots, Icon::Open, [this, id] { askHmiRecipeCsv(id, true); });
        m.add("Comparer les jeux" + kDots, Icon::Search, [this, id] { askHmiCompareRecords(id); });
        m.separator();
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteRecipe(id); });
        break;
    }
    case NK::HmiUser: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("Mot de passe" + kDots, Icon::Lock, [this, id] { askHmiPassword(id); });
        m.separator();
        addRemove("Supprimer" + kDots, [this, id] { askHmiDeleteUser(id); });
        break;
    }
    case NK::HmiUserGroup: {
        const auto id = treeModel_ ? treeModel_->hmiIdOf(node) : 0;
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.separator();
        addRemove("Supprimer le groupe" + kDots, [this, id] { askHmiDeleteGroup(id); });
        break;
    }
    case NK::HmiHistory:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("Exporter" + kDots, Icon::Export, [this] { askHmiHistoryExport(0); });
        m.add("Vider l'historique" + kDots, Icon::Close, [this] { askHmiClearHistory(); });
        break;
    case NK::HmiResources:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("Importer une ressource" + kDots, Icon::Image, [this] { askHmiImportResource(); });
        break;
    case NK::HmiExternalFiles:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("Lier un fichier" + kDots, Icon::Document, [this] { askHmiLinkFile(false); });
        m.add("Lier une base de donn\xC3\xA9" "es" + kDots, Icon::Document, [this] { askHmiLinkFile(true); });
        break;
    case NK::HmiLanguages:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("+ Langue" + kDots, Icon::Globe, [this] { askHmiAddLanguage(); });
        m.add("Importer les traductions" + kDots, Icon::Open, [this] { askHmiImportTranslations(); });
        break;
    case NK::HmiUnits:
        m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        m.add("+ Unit\xC3\xA9" + kDots, Icon::Settings, [this] { askHmiAddUnit(); });
        break;

    // =====================================================================================
    //  LES VERSIONS
    // =====================================================================================
    case NK::VersionsFolder:
        m.add("Ouvrir", Icon::Open, [this] { openVersions(); });
        m.add("Cr\xC3\xA9" "er une version" + kDots, Icon::Save, [this] { askCreateVersion(); }, {},
              app_.projectFolder().empty() ? "enregistre d'abord le projet en dossier" : "");
        break;
    case NK::VersionItem: {
        const int number = treeModel_ ? treeModel_->versionOf(node) : -1;
        if (number < 0) break;
        m.add("Ouvrir", Icon::Open, [this, number] { openVersions(number); });
        if (number == 0) {
            // Le travail en cours.
            int last = 0;
            if (treeModel_)
                for (const auto& row : treeModel_->versions())
                    if (row.number > last) last = row.number;
            m.add("Cr\xC3\xA9" "er une version" + kDots, Icon::Save, [this] { askCreateVersion(); }, {},
                  app_.projectFolder().empty() ? "enregistre d'abord le projet en dossier" : "");
            m.add("Comparer \xC3\xA0 la derni\xC3\xA8re version", Icon::Search, [this, last] { openVersionCompare(last, 0); }, {},
                  last > 0 ? std::string{} : "aucune version encore");
            break;
        }
        m.add("Comparer avec le travail en cours", Icon::Search, [this, number] { openVersionCompare(number, 0); });
        m.add("Restaurer" + kDots, Icon::History, [this, number] { askRestoreVersion(number); });
        m.add("Exporter (zip)", Icon::Export, [this, number] { exportVersion(number); });
        m.add("Extraire dans un dossier" + kDots, Icon::Folder, [this, number] { askExtractVersion(number); });
        m.separator();
        addRemove("Supprimer" + kDots, [this, number] { askDeleteVersion(number); });
        break;
    }

    default:
        // Les autres entrees de l'IHM (Configuration, Simulation, Generer,
        // Compiler, Styles, Essais...) : un onglet chacune.
        if (isHmi) m.add("Ouvrir", Icon::Open, [this, node] { openHmiNode(node); });
        break;
    }

    // =====================================================================================
    //  1.11.13 : LE BUILD DE L'IHM (la generation incrementale) - tout ce qui se genere :
    //  Generer, Regenerer, Compiler, Generer et compiler, les diagnostics, l'artefact,
    //  Nettoyer. Une entree grisee dit pourquoi (un build tourne, rien a compiler...).
    // =====================================================================================
    if (treeModel_ && hmiDoc) {
        const auto target = treeModel_->hmiBuildTarget(node);
        if (!target.empty()) {
            ensureHmiBuild();
            namespace pl = hmi::pipeline;
            const bool busy = hmiBuild_ && hmiBuild_->building();
            const std::string busyWhy = busy ? std::string("un build est d\xC3\xA9j\xC3\xA0 en cours") : std::string{};
            const std::vector<std::string> scope = target.key.empty() ? target.paths : std::vector<std::string>{target.key};
            const bool chosen = !target.key.empty();
            const auto info = hmiBuildInfo(target.key, target.paths);
            m.separator();
            m.heading("Build \xC2\xB7 " + clipped(target.label, 40), info.state);
            const auto run = [this, scope, chosen, label = target.label](pl::Mode mode) {
                return [this, scope, chosen, label, mode] { (void)runHmiBuild(mode, scope, chosen, std::string(pl::modeLabel(mode)) + " " + label); };
            };
            m.add("G\xC3\xA9n\xC3\xA9rer", Icon::Analyze, run(pl::Mode::Generate), {}, busyWhy);
            m.add("R\xC3\xA9g\xC3\xA9n\xC3\xA9rer", Icon::Refresh, run(pl::Mode::Regenerate), {}, busyWhy);
            m.add("Compiler", Icon::Code, run(pl::Mode::Compile), {},
                  busy ? busyWhy : !target.code ? std::string("rien \xC3\xA0 compiler ici (pas de code)") : std::string{});
            m.add("G\xC3\xA9n\xC3\xA9rer et compiler", Icon::Play, run(pl::Mode::GenerateCompile), {},
                  busy ? busyWhy : !target.code ? std::string("rien \xC3\xA0 compiler ici : G\xC3\xA9n\xC3\xA9rer suffit") : std::string{});
            std::vector<Item> more;
            more.push_back(m.make("Voir les diagnostics (" + std::to_string(info.diagnostics.size()) + ")", Icon::Warning,
                                  [this] {
                                      if (auto* out = hmiBuildOutput(true)) out->showTab(1);
                                  },
                                  {}, info.diagnostics.empty() ? std::string("aucun diagnostic au dernier build") : std::string{}));
            const pl::Diagnostic* first = nullptr;
            for (const auto& d : info.diagnostics)
                if (d.blocking()) { first = &d; break; }
            more.push_back(m.make("Aller \xC3\xA0 la premi\xC3\xA8re erreur", Icon::Error,
                                  first ? std::function<void()>([this, d = *first] { openHmiDiagnostic(d); }) : std::function<void()>{}, {},
                                  first ? std::string{} : std::string("aucune erreur")));
            std::string errors;
            for (const auto& d : info.diagnostics)
                if (d.blocking())
                    errors += (d.code.empty() ? std::string{} : d.code + " ") + d.path + (d.line ? ", ligne " + std::to_string(d.line) : std::string{}) + " : " + d.message + "\n";
            more.push_back(m.make("Copier les erreurs", Icon::Document, errors.empty() ? std::function<void()>{} : copy(errors), {},
                                  errors.empty() ? std::string("aucune erreur") : std::string{}));
            const std::string artifact = info.artifact;
            more.push_back(m.make("Ouvrir l'artefact g\xC3\xA9n\xC3\xA9r\xC3\xA9", Icon::Document,
                                  artifact.empty() ? std::function<void()>{} : std::function<void()>([this, artifact] {
                                      const auto why = preview::openWithSystem(artifact);
                                      if (status_) status_->setTransientMessage(why.empty() ? "Artefact ouvert : " + artifact : "Artefact : " + why, 6.0,
                                                                                why.empty() ? StatusBar::Severity::Info : StatusBar::Severity::Warning);
                                  }),
                                  {}, artifact.empty() ? (target.key.empty() ? std::string("un dossier : choisis un \xC3\xA9l\xC3\xA9ment") : std::string("pas encore g\xC3\xA9n\xC3\xA9r\xC3\xA9")) : std::string{}));
            more.push_back(m.make("Nettoyer les artefacts", Icon::Close, run(pl::Mode::Clean), {}, busyWhy));
            m.sub("Build : diagnostics, artefacts", Icon::Analyze, std::move(more));
        }
    }

    // =====================================================================================
    //  POUR TOUS : l'aide du noeud (F1), deplier / replier.
    // =====================================================================================
    m.separator();
    {
        const auto cible = helpTargetForNode(node);
        m.add(cible.kind == help::TargetKind::None ? std::string("Aide") : "Aide sur " + help::labelOf(cible), Icon::Info,
              [this, node] { openHelpFor(helpTargetForNode(node)); }, "F1");
    }
    if (explorer_ && treeModel_ && treeModel_->hasChildren(node) && kind != NK::DerivedField) {
        const bool open = explorer_->isExpanded(node);
        m.add(open ? "Replier" : "D\xC3\xA9plier", open ? Icon::Collapse : Icon::Expand, [this, node] { explorer_->toggle(node); });
    }
    m.add("Tout d\xC3\xA9plier", Icon::Expand, legacy(ActionExpandAll));
    m.add("Tout replier", Icon::Collapse, legacy(ActionCollapseAll));
    // 1.11 (chantier T3, C4) : les deux filtres de l'arbre, avec leur nombre.
    if (treeModel_ && treeModel_->buildState()) {
        const auto* built = treeModel_->buildState();
        m.separator();
        // Recette 1.11 (tranche 14, T2-14) : le nombre est celui de tout le projet, pas du noeud
        // clique (« Ce qui ne compile pas (1) » sur MAST comptait un script C++ de l'IHM) ; le
        // libelle le dit. A zero, l'entree est grisee, avec sa raison.
        const auto nc = built->notCompiling(), ng = built->notGenerated();
        m.add("Ce qui ne compile pas, dans tout le projet (" + std::to_string(nc) + ")", Icon::Warning,
              [this] { setTreeFilter(std::string(kTreeFilterNotCompiling)); }, {}, nc == 0 ? std::string("Tout compile.") : std::string{});
        m.add("Ce qui n'est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9, dans tout le projet (" + std::to_string(ng) + ")", Icon::Warning,
              [this] { setTreeFilter(std::string(kTreeFilterNotGenerated)); }, {},
              ng == 0 ? std::string("Tout est g\xC3\xA9n\xC3\xA9r\xC3\xA9.") : std::string{});
    }
    m.trim();
}

// ============================================================ les actions ====
void MainAnalysisScreen::runExplorerAction(int action) {
    using NodeKind = ProjectTreeModel::NodeKind;
    const NodeId node = contextNode_;
    auto project = app_.project();

    // Lot 7 : une entree qui porte son action.
    if (action >= ActionCall) {
        const auto k = static_cast<std::size_t>(action - ActionCall);
        if (k < contextCalls_.size() && contextCalls_[k]) {
            const auto call = contextCalls_[k];     // une copie : l'action peut refaire le menu
            call();
        }
        return;
    }

    // Lot API 3 : la variable (et les autres variables choisies) dans une table.
    if (action >= ActionAddToAnimationTable && project) {
        const auto table = static_cast<std::size_t>(action - ActionAddToAnimationTable);
        if (table >= project->animationTables.size()) return;
        std::vector<NodeId> nodes{node};
        if (explorer_)
            for (const auto n : explorer_->selection())
                if (n != node) nodes.push_back(n);
        addTreeVariablesToTable(nodes, table);
        return;
    }

    switch (action) {
        case ActionOpen:
            if (project) {
                const auto k = ProjectTreeModel::kindOf(node);
                if (k == NodeKind::Section || k == NodeKind::DfbSection)
                    openDocument(ProjectTreeModel::indexOf(node));
            }
            return;

        // Lot API 7 : pas les membres des variables (ils se deplient a l infini).
        case ActionExpandAll:   explorer_->expandToDepth(99, [](ui::NodeId n) { return ProjectTreeModel::holdsMembers(n); }); return;
        case ActionCollapseAll: explorer_->expandToDepth(0);  return;

        case ActionMoveUp:
        case ActionMoveDown: {
            auto document = app_.document();
            if (!project || !document || !treeModel_) return;
            const int delta = action == ActionMoveUp ? -1 : +1;
            auto command = ExecutionOrderWiring::move(document, *treeModel_, node, delta);
            if (!command) return;
            // Une commande, donc Ctrl+Z. Changer l'ordre d'execution par erreur
            // ne se voit qu'a la mise en service ; il faut pouvoir revenir.
            app_.apply(std::move(command));
            status_->setMessage(std::string(delta < 0 ? "Section remont\xC3\xA9" "e" : "Section descendue")
                                + " dans l'ordre d'ex\xC3\xA9" "cution  -  Ctrl+Z pour annuler");
            return;
        }

        case ActionHelp:
            openHelpFor(helpTargetForNode(node));
            return;

        // Lot macros 1 : une macro, un dossier de macros.
        case ActionMacroLaunch:
        case ActionMacroEdit:
        case ActionMacroShow:
        case ActionMacroNew:
        case ActionMacroNewFolder:
        case ActionMacroRename:
        case ActionMacroDelete: {
            if (!treeModel_) return;
            std::string folder;
            const bool isFolder = treeModel_->macroFolderOf(node, folder);
            const std::string macro = ProjectTreeModel::kindOf(node) == NodeKind::Macro ? treeModel_->text(node) : std::string{};
            if (action == ActionMacroLaunch && !macro.empty()) openMacros(macro, /*launch=*/true);
            else if (action == ActionMacroEdit && !macro.empty()) openMacro(macro);
            else if (action == ActionMacroShow) openMacros(macro.empty() ? folder : macro);
            else if (action == ActionMacroNew) askNewMacro(folder);
            else if (action == ActionMacroNewFolder) askNewMacroFolder(folder);
            else if (action == ActionMacroRename) {
                if (!macro.empty()) askRenameMacro(macro);
                else if (isFolder && !folder.empty()) askRenameMacroFolder(folder);
            } else if (action == ActionMacroDelete) {
                if (!macro.empty()) askDeleteMacro(macro);
                else if (isFolder && !folder.empty()) askDeleteMacroFolder(folder);
            }
            return;
        }

        case ActionHmiSaveTemplate:
            askHmiSaveTemplate(treeModel_ ? treeModel_->hmiViewOf(node) : 0);
            return;
        case ActionHmiExportView:
            askHmiExportViews(treeModel_ ? treeModel_->hmiViewOf(node) : 0);
            return;

        case ActionDelete: {
            domain::EntityKind kind{};
            domain::Index      index{};
            if (entityForNode(node, kind, index)) confirmAndDelete(kind, index);
            return;
        }

        case ActionPublishToLibrary: {
            if (!project) return;
            const auto k = ProjectTreeModel::kindOf(node);
            const auto i = ProjectTreeModel::indexOf(node);
            std::string name;
            if (k == NodeKind::DerivedType && i < project->derivedTypes.size())
                name = project->strings.text(project->derivedTypes[i].name);
            else if (k == NodeKind::DfbType && i < project->pous.size())
                name = project->strings.text(project->pous[i].name);
            if (name.empty()) return;

            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            auto categories = lib.categories();
            if (categories.empty()) categories.emplace_back("General");

            std::vector<FormDialog::Field> fields;
            fields.push_back({"Nom", name, "", false, {}});
            fields.push_back({"Cat\xC3\xA9gorie", categories.front(), "", false, categories});

            app_.menus().ShowDialog(
                std::make_unique<FormDialog>("dialog.publish", "Publier dans la biblioth\xC3\xA8que partag\xC3\xA9" "e",
                    "La biblioth\xC3\xA8que en garde une copie. Les projets qui utilisent d\xC3\xA9j\xC3\xA0 ce bloc\n"
                    "sont pr\xC3\xA9venus de la nouvelle version ; aucun n'est modifi\xC3\xA9.",
                    fields, "Publier"),
                [this, k, name](const menu::DialogResult& r) {
                    if (!r.accepted()) return;
                    auto values = FormDialog::split(r.payload);
                    if (values.size() < 2) return;
                    auto current = app_.project();
                    if (!current) return;

                    project::SharedLibrary target(project::SharedLibrary::defaultRoot());
                    (void)target.scan();
                    const auto status =
                        (k == NodeKind::DerivedType)
                            ? target.publishDerivedType(*current, name, values[1])
                            : target.publishFunctionBlock(*current, name, values[1]);
                    if (status)
                        status_->setMessage(name + " publi\xC3\xA9 dans " + values[1]);
                    else
                        app_.menus().ShowDialog(
                            std::make_unique<MessageDialog>("Publication impossible",
                                                            status.error().message(),
                                                            MessageDialog::Icon::Error),
                            [](const menu::DialogResult&) {});
                });
            return;
        }

        case ActionImportFromLibrary: {
            project::SharedLibrary lib(project::SharedLibrary::defaultRoot());
            (void)lib.scan();
            std::vector<std::string> names;
            for (const auto& item : lib.items()) names.push_back(item.name);
            if (names.empty()) {
                app_.menus().ShowDialog(
                    std::make_unique<MessageDialog>("Biblioth\xC3\xA8que partag\xC3\xA9" "e vide",
                        "Rien n'y est encore publi\xC3\xA9. Clic droit sur un type d\xC3\xA9riv\xC3\xA9 ou un\n"
                        "bloc DFB, puis \xC2\xAB Publier dans la biblioth\xC3\xA8que partag\xC3\xA9" "e \xC2\xBB d'abord.",
                        MessageDialog::Icon::Info),
                    [](const menu::DialogResult&) {});
                return;
            }
            std::vector<FormDialog::Field> fields;
            fields.push_back({"\xC3\x89l\xC3\xA9ment", names.front(), "", false, names});
            fields.push_back({"Si le nom est pris", "Refuser", "", false,
                              {"Refuser", "Remplacer", "Dupliquer"}});

            app_.menus().ShowDialog(
                std::make_unique<FormDialog>("dialog.importLib", "Importer depuis la biblioth\xC3\xA8que partag\xC3\xA9" "e",
                    "La copie est prise maintenant. Un changement ult\xC3\xA9rieur dans la biblioth\xC3\xA8que est\n"
                    "signal\xC3\xA9, jamais appliqu\xC3\xA9.", fields, "Importer"),
                [this](const menu::DialogResult& r) {
                    if (!r.accepted()) return;
                    auto values = FormDialog::split(r.payload);
                    if (values.size() < 2) return;
                    auto document = app_.document();
                    if (!document) return;

                    auto policy = project::SharedLibrary::OnConflict::Refuse;
                    if (values[1] == "Remplacer") policy = project::SharedLibrary::OnConflict::Overwrite;
                    else if (values[1] == "Dupliquer") policy = project::SharedLibrary::OnConflict::Duplicate;

                    project::SharedLibrary source(project::SharedLibrary::defaultRoot());
                    (void)source.scan();
                    auto outcome = source.import(*document, values[0], policy);
                    if (!outcome) {
                        app_.menus().ShowDialog(
                            std::make_unique<MessageDialog>("Import impossible",
                                                            outcome.error().message(),
                                                            MessageDialog::Icon::Error),
                            [](const menu::DialogResult&) {});
                        return;
                    }
                    status_->setMessage("Import\xC3\xA9 : " + outcome->importedAs
                                        + (outcome->replacedExisting ? " (remplac\xC3\xA9)" : ""));
                    if (auto p = app_.project()) bindProject(p, app_.report());
                });
            return;
        }

        default: return;
    }
}

} // namespace app
