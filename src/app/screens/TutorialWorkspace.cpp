// =============================================================================
//  app/screens/TutorialWorkspace.cpp - lot 21 : le didacticiel en parcours
// -----------------------------------------------------------------------------
//  LES PARCOURS AU CHOIX (leurs cartes sont sur la page " Le didacticiel " de
//  l'aide de l'IHM). Chacun est une suite d'etapes composees ici, avec ce que
//  l'ecran sait : l'onglet courant, l'arbre, la selection de l'editeur, le
//  projet IHM, la pile d'annulation, les versions sur le disque.
//   - decouvrir     la visite de l'IHM, a jour des lots 10 a 21 ;
//   - premiere-vue  une vue, un afficheur relie, un bouton et son action, la simulation ;
//   - symbole       " Creer un symbole de projet " : une carte d'armoire devient
//                   un symbole, pose deux fois, modifie une fois, simule ;
//   - modbus        la visite des equipements Modbus ;
//   - excel         des lignes d'un tableur collees dans les variables IHM, un seul Ctrl+Z ;
//   - versions      annuler, retablir, l'historique, une version, la comparer.
//
//  UNE ETAPE INTERACTIVE regarde l'ecran quelques fois par seconde (son
//  `done`) : le geste fait, l'etape suivante vient. " Montre-moi " le fait
//  avec les memes commandes que l'utilisateur (Ctrl+Z les defait).
//
//  LA PROGRESSION, dans les reglages : "didacticiel.<cle>.etape" (l'etape
//  atteinte, 1..n ; 0 : fini, ou jamais commence) et "didacticiel.<cle>.fait".
//  " Reprendre " repart de l'etape atteinte.
//
//  TOUT DEFAIRE : le parcours note le dernier pas de l'historique en
//  commencant ; a la fin, App::goToHistory y revient. Les versions creees
//  restent : elles sont sur le disque, pas dans l'historique.
//
//  LOT API 7 : LES PARCOURS DE L'API PASSENT PAR ICI AUSSI (leurs etapes sont
//  composees dans app/ApiTrails.cpp) : une cle "api-..." ; ils n'ont pas
//  besoin de l'IHM, leur progression est rangee projet par projet
//  (ApiTrails::prefix), et Tout defaire peut faire un peu plus que revenir
//  dans l'historique (deforcer une variable, rendre un bloc que la macro a mis
//  a jour hors de l'historique) - et il est propose des qu'il y a de quoi.
//  Repris dans la meme session du projet, un parcours de l'API garde son
//  depart : Tout defaire retire aussi ce que ses premieres etapes ont cree.
//  Leur bulle montre toujours Precedent et Suivant (setFullNavigation).
// =============================================================================
#include "Screens.hpp"
#include "../TopBar.hpp"   // lot API 2

#include "../ApiTrails.hpp"   // lot API 7
#include "../App.hpp"
#include "../HistoryPanel.hpp"
#include "../TablePaste.hpp"
#include "../VersionComparePane.hpp"
#include "../VersionsPane.hpp"
#include "../hmi/HmiCanvas.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiHelpPane.hpp"
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiPanes.hpp"
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../hmi/HmiTutorial.hpp"
#include "../hmi/HmiVariablePanes.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiSymbols.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace app {

using namespace ui;
using NK = ProjectTreeModel::NodeKind;
namespace ver = hmi::ver;
using Step = HmiTutorial::Step;
using Rect = gfx::Rect;

namespace {

constexpr const char* kTrailView = "Vue_Didacticiel";
constexpr const char* kTrailSymbol = "Carte_Didacticiel";

// Un objet cite-t-il ce texte (une valeur ou une expression) ?
bool mentions(const hmi::Object& o, std::string_view needle) {
    for (const auto& p : o.props)
        if (p.value.find(needle) != std::string::npos || p.expr.find(needle) != std::string::npos) return true;
    return false;
}

// Le premier widget de ce type montre dans une page (en largeur d'abord).
template <class T>
T* shownIn(Widget* root) {
    if (!root || !root->visible()) return nullptr;
    std::vector<Widget*> queue{root};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        Widget* w = queue[i];
        if (auto* t = dynamic_cast<T*>(w)) return t;
        for (const auto& c : w->children())
            if (c->visible()) queue.push_back(c.get());
    }
    return nullptr;
}

Rect unite(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.right(), b.right()) - x, std::max(a.bottom(), b.bottom()) - y};
}

// Le cadre d'un objet de la vue, a l'ecran (l'editeur montre), rogne a la zone de dessin.
Rect screenBox(HmiEditor& ed, const hmi::Object& o) {
    const auto vp = ed.canvas().viewport();
    const auto b = o.box();
    const auto a = vp.toScreen(b.x, b.y), c = vp.toScreen(b.right(), b.bottom());
    return ed.canvas().bounds().intersect({a.x, a.y, c.x - a.x, c.y - a.y});
}

// "Rectangle, Texte, 2 Afficheur numerique, Voyant" : les genres d'une selection.
std::string kindsText(const hmi::View& v, const std::vector<hmi::Id>& ids) {
    std::vector<std::pair<std::string, int>> counts;
    for (const auto id : ids) {
        const auto* o = v.object(id);
        if (!o) continue;
        const std::string label(hmi::kindLabel(o->kind));
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == label; });
        if (it == counts.end()) counts.emplace_back(label, 1);
        else ++it->second;
    }
    std::string out;
    for (const auto& [label, n] : counts) out += (out.empty() ? "" : ", ") + (n > 1 ? std::to_string(n) + " " : std::string{}) + label;
    return out;
}

// Les objets de la carte : tout ce qui n'est pas une instance de symbole.
std::vector<hmi::Id> cardObjects(const hmi::View& v) {
    std::vector<hmi::Id> ids;
    for (const auto& o : v.objects)
        if (o.kind != hmi::Kind::SymbolInstance && o.parent == hmi::kNoId) ids.push_back(o.id);
    return ids;
}

std::vector<const hmi::Object*> instancesIn(const hmi::View& v, const std::string& symbol) {
    std::vector<const hmi::Object*> out;
    for (const auto& o : v.objects)
        if (o.kind == hmi::Kind::SymbolInstance && (symbol.empty() || o.text("symbol") == symbol)) out.push_back(&o);
    return out;
}

// Un nouvel objet pose dans la vue (dans son calque actif) ; rend son identifiant.
hmi::Id addObject(hmi::Project& p, hmi::View& v, hmi::Kind kind, const std::string& base, double x, double y, double w, double h,
                  const std::vector<std::pair<std::string, std::string>>& props) {
    hmi::Object o = hmi::makeObject(kind, p.allocate(), hmi::uniqueObjectName(v, base), x, y, v.activeLayer);
    o.setNumber("w", w);
    o.setNumber("h", h);
    // "=expression" : une propriete animee (Valeur (expression) de l'inspecteur).
    for (const auto& [k, value] : props) {
        if (!value.empty() && value.front() == '=') o.setExpr(k, value.substr(1));
        else o.set(k, value);
    }
    return hmi::edit::addObject(v, std::move(o));
}

std::uint64_t topSerial(const core::CommandStack& st) { return st.done().empty() ? 0 : st.done().back().info.serial; }

// Ou revenir dans l'historique pour retrouver l'etat du debut d'un parcours :
// la plus recente des entrees faites qui n'est pas plus recente que son depart.
// Le depart lui-meme a pu etre annule entre-temps (un Ctrl+Z de trop) :
// goToHistory le chercherait alors dans les annulees et le RETABLIRAIT.
std::uint64_t undoTarget(const core::CommandStack& st, std::uint64_t serial0) {
    std::uint64_t target = 0;
    for (const auto& e : st.done())
        if (e.info.serial <= serial0) target = e.info.serial;
    return target;
}

// Lot API 7 : le depart (le dernier pas de l'historique) de chaque parcours de
// l'API lance, par sa cle de progression, et l'ouverture du projet pour
// laquelle il vaut. Reprendre dans la meme session garde ce depart : Tout
// defaire retire aussi ce que les premieres etapes ont cree. Les pas de
// l'historique ne survivent pas a la fermeture du projet : d'une session a
// l'autre, le depart est la reprise.
std::map<std::string, std::pair<std::int64_t, std::uint64_t>>& apiTrailOrigins() {
    static std::map<std::string, std::pair<std::int64_t, std::uint64_t>> origins;
    return origins;
}

} // namespace

// ----------------------------------------------------------- l'etat d'un parcours
struct MainAnalysisScreen::TrailRun {
    std::string   key, title;
    std::uint64_t serial0{0};            // le dernier pas de l'historique en commencant
    // "symbole" et "premiere-vue" : la vue de l'exercice ; le symbole cree.
    std::string   viewName, symbolName;
    hmi::Id       viewId{hmi::kNoId};
    std::vector<hmi::Object> symbolBefore;   // le symbole en arrivant a l'etape 8
    std::set<hmi::Id>        viewsBefore;    // "premiere-vue" : les vues d'avant
    bool          revealed{false};       // une tuile ou une propriete amenee en vue (une fois)
    // "versions"
    std::uint64_t stepSerial{0}, stepRevision{0};
    std::size_t   versionsBefore{0};
    // "excel" : les variables creees par le collage
    std::vector<std::string> pasted;
    // Lot API 7 : un parcours de l'API, et ce qu'il fait en plus de l'historique.
    bool                  api{false};
    std::function<void()> beforeUndo, afterUndo, closed;
    std::function<bool()> pending;       // il reste a defaire hors de l'historique

    // ---- ce que les etapes regardent ------------------------------------------
    static HmiEditor* editorOf(MainAnalysisScreen& s, hmi::Id view) {
        return view == hmi::kNoId ? nullptr : dynamic_cast<HmiEditor*>(s.hmiTab("vue:" + std::to_string(view)));
    }
    static HmiEditor* currentEditor(MainAnalysisScreen& s) {
        if (!s.centre_ || s.centre_->tabCount() == 0) return nullptr;
        return dynamic_cast<HmiEditor*>(s.centre_->page(s.centre_->currentIndex()));
    }
    static Widget* currentPage(MainAnalysisScreen& s) {
        if (!s.centre_ || s.centre_->tabCount() == 0) return nullptr;
        return s.centre_->page(s.centre_->currentIndex());
    }
    static const hmi::View* viewOf(MainAnalysisScreen& s, hmi::Id id) {
        auto doc = s.app_.hmi();
        return doc ? doc->project.view(id) : nullptr;
    }
    static ui::NodeId childOfKind(MainAnalysisScreen& s, ui::NodeId parent, NK kind) {
        if (!s.treeModel_) return ui::kInvalidNode;
        for (std::size_t k = 0; k < s.treeModel_->childCount(parent); ++k)
            if (ProjectTreeModel::kindOf(s.treeModel_->childAt(parent, k)) == kind) return s.treeModel_->childAt(parent, k);
        return ui::kInvalidNode;
    }
    // Une entree du dossier IHM par son genre (Configuration > Equipements : sous Configuration ;
    // Variables IHM : sous la Programmation generale).
    static ui::NodeId nodeOf(MainAnalysisScreen& s, NK kind) {
        const auto root = ProjectTreeModel::hmiFolderNode();
        if (kind == NK::HmiFolder) return root;
        if (kind == NK::VersionsFolder) return ProjectTreeModel::versionsFolderNode();
        // Lot API 8 : Centre de simulation - IHM > Simulation est devenu Simulation > IHM.
        if (kind == NK::HmiSimulation) return childOfKind(s, ProjectTreeModel::simFolderNode(), NK::HmiSimulation);
        if (kind == NK::HmiComm) return childOfKind(s, childOfKind(s, root, NK::HmiConfig), NK::HmiComm);
        if (kind == NK::HmiVariablesFolder) return childOfKind(s, childOfKind(s, root, NK::HmiScripts), NK::HmiVariablesFolder);
        if (kind == NK::HmiViewFolder) {
            const auto views = childOfKind(s, root, NK::HmiViews);
            return s.treeModel_ && views != ui::kInvalidNode && s.treeModel_->childCount(views) > 1 ? s.treeModel_->childAt(views, 1) : views;
        }
        const auto n = childOfKind(s, root, kind);
        return n == ui::kInvalidNode ? root : n;
    }
    // Lot API 8 : l'arbre du projet - un outil sorti de l'arbre (Outil Modbus,
    // Generer, Compiler...) : son bouton dans la rangee sous le titre IHM.
    static std::function<bool(Rect&)> row(MainAnalysisScreen& s, NK kind) {
        return [&s, kind](Rect& r) {
            return s.treeToolRect(ProjectTreeModel::pack(kind, 0), r) || (s.explorer_ && s.explorer_->rowRect(nodeOf(s, kind), r));
        };
    }
    static std::function<void()> reveal(MainAnalysisScreen& s, NK kind) {
        return [&s, kind] { if (!s.revealTreeTool(ProjectTreeModel::pack(kind, 0))) (void)s.revealTreeNode(nodeOf(s, kind)); };
    }
    static bool viewRow(MainAnalysisScreen& s, hmi::Id id, Rect& r) {
        return s.explorer_ && s.treeModel_ && id != hmi::kNoId && s.explorer_->rowRect(s.treeModel_->hmiViewNode(id), r);
    }
    static bool button(ui::Button* b, Rect& r) {
        if (!b || !b->visible()) return false;
        r = b->bounds();
        return !r.empty();
    }
    // Lot API 2 : une partie de la barre du haut (Annuler, Retablir, Historique).
    static bool barPart(MainAnalysisScreen& s, std::string_view part, Rect& r) {
        if (!s.topBar_) return false;
        r = s.topBar_->partRect(part);
        return !r.empty();
    }
    // La simulation IHM sur cette vue, en marche.
    static bool simulating(MainAnalysisScreen& s, hmi::Id view) {
        auto* sim = dynamic_cast<HmiSimulationPane*>(s.hmiTab("simulation"));
        return sim && currentPage(s) == sim && sim->runtime().running() && sim->currentView() == view;
    }
    static void simulate(MainAnalysisScreen& s, hmi::Id view) {
        s.openHmiPane("simulation");
        auto* sim = dynamic_cast<HmiSimulationPane*>(s.hmiTab("simulation"));
        if (!sim) return;
        if (!s.app_.simulation().attached() || s.app_.simulation().state() != SimulationHost::State::Running)
            s.runSimulationTransport("sim.run");
        sim->restart();
        sim->goToView(view);
    }

    // ---- les parcours -------------------------------------------------------------
    static std::vector<Step> discover(MainAnalysisScreen& s);
    static std::vector<Step> firstView(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run);
    static std::vector<Step> symbol(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run);
    static std::vector<Step> modbus(MainAnalysisScreen& s);
    static std::vector<Step> excel(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run);
    static std::vector<Step> versions(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run);
    // La vue de l'exercice (creee si elle manque ; `fresh` : une vue neuve si l'ancienne est pleine).
    static hmi::Id ensureView(MainAnalysisScreen& s, TrailRun& run, bool fresh);
    // Le symbole pose dans la vue de l'exercice (son nom, note) ; faux : pas encore.
    static bool findSymbol(MainAnalysisScreen& s, TrailRun& run);
};

// ============================================================ les cartes =====
namespace {
struct TrailDef {
    const char* key;
    const char* title;
    const char* kind;
    const char* summary;
    int         steps, minutes;
    bool        isNew;
};
const TrailDef kTrails[] = {
    {"decouvrir", "D\xC3\xA9" "couvrir l'IHM", "visite", "L'arbre, les vues, les symboles, la simulation, l'historique, les versions.", 14, 3, false},
    {"premiere-vue", "Ma premi\xC3\xA8re vue", "interactif", "Une vue, un afficheur reli\xC3\xA9, un bouton et son action, puis la simulation.", 7, 6, false},
    {"symbole", "Cr\xC3\xA9" "er un symbole de projet", "interactif", "Une carte d'armoire devient un symbole, pos\xC3\xA9 deux fois, modifi\xC3\xA9 une fois.", 9, 8, false},
    {"modbus", "Relier un \xC3\xA9quipement Modbus", "visite", "Les \xC3\xA9quipements, les adresses, l'outil Modbus, l'esclave simul\xC3\xA9.", 7, 4, false},
    {"excel", "Coller depuis Excel", "interactif", "Des lignes d'un tableur deviennent des variables IHM, en un seul Ctrl+Z.", 4, 3, true},
    {"versions", "Historique et versions", "interactif", "Annuler, r\xC3\xA9tablir, l'historique, une version, la comparer.", 6, 4, true},
};
} // namespace

std::vector<TrailCard> MainAnalysisScreen::tutorialTrails() const {
    std::vector<TrailCard> out;
    const auto& settings = app_.settings();
    for (const auto& d : kTrails) {
        TrailCard c;
        c.key = d.key;
        c.title = d.title;
        c.kind = d.kind;
        c.summary = d.summary;
        c.steps = d.steps;
        c.minutes = d.minutes;
        c.isNew = d.isNew;
        c.reached = std::clamp(settings.getInt(std::string("didacticiel.") + d.key + ".etape", 0), 0, d.steps);
        c.done = settings.getBool(std::string("didacticiel.") + d.key + ".fait", false);
        c.running = trailRun_ && trailRun_->key == d.key && hmiTutorial_ && hmiTutorial_->active();
        out.push_back(std::move(c));
    }
    return out;
}

void MainAnalysisScreen::refreshTrailCards() {
    if (auto* help = dynamic_cast<HmiHelpPane*>(hmiTab("aide"))) help->refreshTrails();
    ApiTrails::refreshPage(*this);   // lot API 7 : les cartes de l'API
}

std::string MainAnalysisScreen::runningTrail() const {
    return trailRun_ && hmiTutorial_ && hmiTutorial_->active() ? trailRun_->key : std::string{};
}

std::string MainAnalysisScreen::currentHmiKey() const {
    if (!centre_ || centre_->tabCount() == 0) return {};
    Widget* page = centre_->page(centre_->currentIndex());
    for (const auto& [key, widget] : hmiTabs_)
        if (widget == page) return key;
    return {};
}

bool MainAnalysisScreen::revealTreeNode(ui::NodeId node, gfx::Rect* rowOut) {
    if (!explorer_ || !treeModel_ || node == ui::kInvalidNode) return false;
    // Lot API 8 : l'arbre du projet - montrer un noeud, c'est tout l'arbre (le rail revient a "Tout").
    if (treeModel_->scope() != 0) (void)setTreeScope("tout");
    // Le chemin depuis la racine ; on ne descend que dans les dossiers (pas dans
    // les objets d'une vue, ni dans les variables d'une structure).
    std::vector<ui::NodeId> path;
    std::function<bool(ui::NodeId, int)> find = [&](ui::NodeId at, int depth) -> bool {
        if (at == node) return true;
        if (depth > 8) return false;
        const auto kind = ProjectTreeModel::kindOf(at);
        // Lot API 7 : et le dossier API, ses tables d'animation, ses macros, ses
        // taches (les parcours de l'API y montrent une table, une macro, la
        // section qu'ils font ecrire).
        const bool folder = at == treeModel_->root() || kind == NK::HmiFolder || kind == NK::HmiViews || kind == NK::HmiViewFolder
                         || kind == NK::HmiTemplateFolder || kind == NK::HmiSymbolsFolder || kind == NK::HmiScripts
                         || kind == NK::HmiScriptsFolder || kind == NK::HmiVariablesFolder || kind == NK::HmiVarFolder
                         || kind == NK::HmiTypesFolder || kind == NK::HmiConfig || kind == NK::VersionsFolder
                         || kind == NK::HmiListFolder || kind == NK::ApiFolder || kind == NK::TablesFolder
                         || kind == NK::MacroFolder || kind == NK::MacroSubFolder || kind == NK::TaskFolder || kind == NK::Task
                         || kind == NK::SimFolder;   // Lot API 8 : Centre de simulation (Automate, IHM y sont)
        if (!folder) return false;
        path.push_back(at);
        for (std::size_t i = 0; i < treeModel_->childCount(at); ++i)
            if (find(treeModel_->childAt(at, i), depth + 1)) return true;
        path.pop_back();
        return false;
    };
    if (!find(treeModel_->root(), 0)) return false;
    for (const auto n : path) explorer_->expand(n);
    explorer_->ensureVisible(node);
    return rowOut ? explorer_->rowRect(node, *rowOut) : true;
}

// ============================================================ lancer ========
void MainAnalysisScreen::startTrail(const std::string& key, bool resume) {
    if (!hmiTutorial_ || !treeModel_ || !explorer_) return;
    const TrailDef* def = nullptr;
    for (const auto& d : kTrails)
        if (key == d.key) def = &d;
    // Lot API 7 : les parcours de l'API (ApiTrails.cpp) ont besoin du programme,
    // pas de l'IHM ; ceux de l'IHM, de son projet.
    const ApiTrailDef* apiDef = def ? nullptr : ApiTrails::find(key);
    if (!def && !apiDef) return;
    if (def && !app_.hmi()) return;
    ApiTrails::Built built;             // sans projet, build() le refuse et dit pourquoi
    if (apiDef) {
        built = ApiTrails::build(*this, key);
        if (!built.refusal.empty() || built.steps.empty()) {
            if (status_) status_->setTransientMessage(std::string(apiDef->title) + " : " + built.refusal, 10.0);
            return;
        }
    }
    if (hmiTutorial_->active()) hmiTutorial_->stop(false);   // le parcours en cours retient son etape

    auto run = std::make_shared<TrailRun>();
    run->key = def ? def->key : apiDef->key;
    run->title = def ? def->title : apiDef->title;
    run->api = apiDef != nullptr;
    run->serial0 = topSerial(app_.commands());
    const std::string prefix = run->api ? ApiTrails::prefix(*this, run->key) : "didacticiel." + run->key;
    const int reached = app_.settings().getInt(prefix + ".etape", 0);
    const std::size_t from = resume && reached > 1 ? static_cast<std::size_t>(reached - 1) : 0;
    std::vector<Step> steps;
    if (run->api) {
        steps = std::move(built.steps);
        run->beforeUndo = std::move(built.beforeUndo);
        run->afterUndo = std::move(built.afterUndo);
        run->closed = std::move(built.closed);
        run->pending = std::move(built.pending);
        // Repris dans la meme session du projet : le depart du premier lancement
        // (apiTrailOrigins) ; commence ou refait : maintenant.
        auto& origins = apiTrailOrigins();
        const std::int64_t opened = app_.openedAtMs();
        if (const auto it = origins.find(prefix); from > 0 && it != origins.end() && it->second.first == opened)
            run->serial0 = std::min(run->serial0, it->second.second);
        origins[prefix] = {opened, run->serial0};
    } else if (run->key == "decouvrir") steps = TrailRun::discover(*this);
    else if (run->key == "premiere-vue") steps = TrailRun::firstView(*this, run);
    else if (run->key == "symbole") {
        run->viewName = resume ? app_.settings().getString(prefix + ".vue") : std::string{};
        run->symbolName = resume ? app_.settings().getString(prefix + ".symbole") : std::string{};
        (void)TrailRun::ensureView(*this, *run, !resume);
        // "Creer un symbole" propose le nom du parcours tant qu'il tourne (l'etape 5 le cite).
        trailSymbolName_ = !run->symbolName.empty() ? run->symbolName : hmi::uniqueViewName(app_.hmi()->project, kTrailSymbol);
        steps = TrailRun::symbol(*this, run);
    } else if (run->key == "modbus") steps = TrailRun::modbus(*this);
    else if (run->key == "excel") steps = TrailRun::excel(*this, run);
    else if (run->key == "versions") steps = TrailRun::versions(*this, run);
    if (steps.empty()) return;
    trailRun_ = run;
    if (run->key != "symbole") trailSymbolName_.clear();

    tutorialLinks_.clear();
    tutorialLinks_ += hmiTutorial_->stepped->connect([this, run, prefix](std::size_t index) {
        app_.settings().set(prefix + ".etape", static_cast<int>(index) + 1);
        if (status_ && hmiTutorial_)
            status_->setTransientMessage("Didacticiel : " + run->title + " \xE2\x80\x94 \xC3\xA9tape " + std::to_string(index + 1) + " / "
                                             + std::to_string(hmiTutorial_->stepCount()),
                                         8.0);
        refreshTrailCards();
    });
    tutorialLinks_ += hmiTutorial_->closed->connect([this, run, prefix](bool finished) {
        if (finished) {
            app_.settings().set(prefix + ".fait", true);
            app_.settings().set(prefix + ".etape", 0);
            if (run->api) app_.settings().set(prefix + ".date", ApiTrails::today());   // "fait le 28/09"
        }
        if (run->key == "decouvrir") app_.settings().set("hmi.tutorial.seen", true);
        if (run->closed) run->closed();
        if (trailRun_ == run) trailRun_.reset();
        trailSymbolName_.clear();
        // Les autres parcours : dans l'aide de l'IHM (F1), ou dans l'onglet API . Didacticiel.
        const std::string where = run->api ? "Aide \xE2\x80\xBA Didacticiel de l'API" : "F1, puis Didacticiel";
        if (status_)
            status_->setTransientMessage(finished ? "Parcours termin\xC3\xA9 : " + run->title + ". " + where + ", pour les autres parcours."
                                                  : "Parcours interrompu : " + run->title + ". " + where + ", puis Reprendre.",
                                         8.0);
        refreshTrailCards();
    });
    // Lot API 7 : la bulle des parcours de l'API montre toujours Precedent et
    // Suivant (la maquette) ; celle de l'IHM reste celle du lot 21.
    hmiTutorial_->setFullNavigation(run->api);
    hmiTutorial_->start(std::move(steps), run->key == "decouvrir" ? std::string{} : run->title, from);
    // Un parcours interactif peut tout defaire, a la fin : l'historique revient a son debut.
    // Lot API 7 : ce que l'historique ne porte pas se defait autour (ApiTrails::Built),
    // et le propose meme quand la pile n'a rien de neuf (un bloc mis a jour par la macro).
    if (std::string(def ? def->kind : apiDef->kind) == "interactif") {
        const std::uint64_t serial0 = run->serial0;
        hmiTutorial_->setUndo(
            [this, serial0, run] {
                if (run->beforeUndo) run->beforeUndo();
                app_.goToHistory(undoTarget(app_.commands(), serial0));
                if (run->afterUndo) run->afterUndo();
            },
            [this, serial0, run] { return topSerial(app_.commands()) > serial0 || (run->pending && run->pending()); });
    }
    refreshTrailCards();
}

// ============================================================ Decouvrir l'IHM
std::vector<Step> MainAnalysisScreen::TrailRun::discover(MainAnalysisScreen& s) {
    std::vector<Step> st;
    st.push_back({"Bienvenue dans l'IHM",
                  "Cette visite montre en trois minutes o\xC3\xB9 tout se trouve : l'arbre IHM, la configuration, les vues et les "
                  "symboles, la programmation, la simulation, l'historique et les versions.\n"
                  "Suivant (ou Entr\xC3\xA9" "e) pour continuer, Passer (ou \xC3\x89" "chap) pour la fermer. Les autres parcours du "
                  "didacticiel font faire : F1, puis Didacticiel.",
                  {}, {}});
    st.push_back({"Le dossier IHM",
                  "Tout part d'ici, dans l'arbre du projet, \xC3\xA0 c\xC3\xB4t\xC3\xA9 du dossier API. Chaque entr\xC3\xA9" "e ouvre son onglet au "
                  "centre ; le triangle d\xC3\xA9plie les vues, les alarmes, les utilisateurs, la programmation... et un clic "
                  "m\xC3\xA8ne au bon endroit. Les listes se rangent en dossiers, qu'on glisse et d\xC3\xA9pose.",
                  row(s, NK::HmiFolder), reveal(s, NK::HmiFolder)});
    st.push_back({"La configuration",
                  "L'identit\xC3\xA9 de l'IHM, sa r\xC3\xA9solution, sa vue de d\xC3\xA9marrage, son cycle. Dessous : les alarmes, les "
                  "recettes, les utilisateurs, les historiques, les langues, les \xC3\xA9quipements (Modbus), le poste "
                  "d'exploitation.",
                  row(s, NK::HmiConfig), reveal(s, NK::HmiConfig)});
    st.push_back({"Les vues",
                  "Tes \xC3\xA9" "crans, en trois dossiers : Mod\xC3\xA8les (\xC3\xA9" "crans mod\xC3\xA8les, en-t\xC3\xAAtes, pieds de page), Vues et Popups. "
                  "Une vue s'ouvre dans l'\xC3\xA9" "diteur : la biblioth\xC3\xA8que \xC3\xA0 droite, les propri\xC3\xA9t\xC3\xA9s et les actions dans "
                  "l'inspecteur. Nouvelle vue part d'un mod\xC3\xA8le de la galerie.",
                  row(s, NK::HmiViews), reveal(s, NK::HmiViews)});
    st.push_back({"Les symboles",
                  "Une carte dessin\xC3\xA9" "e une fois, pos\xC3\xA9" "e partout : un symbole d\xC3\xA9" "clare des param\xC3\xA8tres (Armoire), chaque "
                  "instance relie le sien (Armoire := Armoires[1]). Modifier le symbole modifie toutes ses instances. "
                  "Le parcours \xC2\xAB Cr\xC3\xA9" "er un symbole de projet \xC2\xBB le fait faire.",
                  row(s, NK::HmiSymbolsFolder), reveal(s, NK::HmiSymbolsFolder)});
    st.push_back({"Les styles",
                  "Des apparences nomm\xC3\xA9" "es (couleurs, police, bordure) que les objets citent : changer le style change "
                  "tous les objets qui le citent.",
                  row(s, NK::HmiStyles), reveal(s, NK::HmiStyles)});
    st.push_back({"Ressources et fichiers",
                  "Les images, sons, vid\xC3\xA9os et polices du projet, et les fichiers externes (Excel, CSV, JSON, "
                  "SQLite...) qu'un tableau peut afficher.",
                  row(s, NK::HmiResources), reveal(s, NK::HmiResources)});
    st.push_back({"La programmation",
                  "Les scripts ST, les fonctions IHM, les variables et les types IHM, et tout ce que l'IHM lit ou "
                  "\xC3\xA9" "crit. L'aide \xC3\xA0 la saisie propose les noms pendant la frappe ; une table se colle depuis Excel.",
                  row(s, NK::HmiScripts), reveal(s, NK::HmiScripts)});
    st.push_back({"La simulation",
                  // ---- Lot API 8 : didacticiels et aide ---- IHM > Simulation est devenu Simulation > IHM.
                  "Simulation \xE2\x80\xBA IHM (le dossier Simulation, au m\xC3\xAAme niveau qu'API et IHM ; F9 ouvre sa Vue d'ensemble) : "
                  "l'IHM en marche, sur le simulateur de l'automate : les clics, les scripts, les alarmes, les "
                  "recettes ; forcer une variable, changer d'utilisateur ; le journal dit tout.",
                  row(s, NK::HmiSimulation), reveal(s, NK::HmiSimulation)});
    st.push_back({"L'outil Modbus et les essais",
                  "L'outil Modbus lit et \xC3\xA9" "crit un \xC3\xA9quipement, montre les trames, espionne le r\xC3\xA9seau : la mise en "
                  "service avant l'IHM. Les essais (IHM \xE2\x80\xBA Essais) rejouent un sc\xC3\xA9nario et disent ce qui passe.",
                  row(s, NK::HmiModbusTool), reveal(s, NK::HmiModbusTool)});
    st.push_back({"G\xC3\xA9n\xC3\xA9rer et Compiler",
                  "G\xC3\xA9n\xC3\xA9rer v\xC3\xA9rifie la coh\xC3\xA9rence du projet, Compiler son code. Un double-clic sur un constat m\xC3\xA8ne \xC3\xA0 "
                  "sa source : l'objet, l'action, la ligne.",
                  row(s, NK::HmiGenerate), reveal(s, NK::HmiGenerate)});
    st.push_back({"L'historique",
                  "Ctrl+Z et Ctrl+Y marchent partout, et annuler montre l'endroit. L'historique (Ctrl+H) liste chaque "
                  "action, son endroit, son heure ; un double clic revient \xC3\xA0 cet \xC3\xA9tat.",
                  [&s](Rect& r) { return barPart(s, "historique", r); }, {}});
    st.push_back({"Les versions",
                  "Une version garde tout le projet tel qu'il est (Ctrl+Alt+S) : nomm\xC3\xA9" "e, dat\xC3\xA9" "e, brouillon, valid\xC3\xA9" "e ou "
                  "livr\xC3\xA9" "e. Comparer deux versions montre ce qui a chang\xC3\xA9 ; on restaure un \xC3\xA9l\xC3\xA9ment seul, ou tout.",
                  row(s, NK::VersionsFolder), reveal(s, NK::VersionsFolder)});
    st.push_back({"F1, partout",
                  "F1 ouvre l'aide de l'endroit o\xC3\xB9 tu es : le volet, l'objet choisi, le nom sous le curseur. Dans "
                  "l'aide, Didacticiel m\xC3\xA8ne aux parcours : cr\xC3\xA9" "er un symbole, coller depuis Excel, l'historique et les "
                  "versions...",
                  {}, {}});
    return st;
}

// ============================================================ Relier un equipement Modbus
std::vector<Step> MainAnalysisScreen::TrailRun::modbus(MainAnalysisScreen& s) {
    std::vector<Step> st;
    st.push_back({"Une variable dans un \xC3\xA9quipement",
                  "Une variable IHM peut se lire et s'\xC3\xA9" "crire dans un \xC3\xA9quipement Modbus TCP : un automate, une centrale "
                  "de mesure, un variateur. Cette visite montre o\xC3\xB9 tout se r\xC3\xA8gle, dans l'ordre de la mise en service.",
                  {}, {}});
    st.push_back({"Configuration \xE2\x80\xBA \xC3\x89quipements",
                  "La liste des \xC3\xA9quipements : l'automate, les \xC3\xA9quipements du r\xC3\xA9seau (adresse IP, port 502, num\xC3\xA9ro "
                  "d'unit\xC3\xA9), le r\xC3\xA9seau du PC, et le scanner IP qui les trouve.",
                  row(s, NK::HmiComm), reveal(s, NK::HmiComm)});
    st.push_back({"Les variables li\xC3\xA9" "es",
                  "Dans Variables IHM : la colonne \xC3\x89quipement et l'Adresse (%MW100, 40101...), l'acc\xC3\xA8s (lecture seule "
                  "ou \xC3\xA9" "criture), une mise \xC3\xA0 l'\xC3\xA9" "chelle brute vers physique. Une structure part de son adresse, ses "
                  "membres suivent.",
                  row(s, NK::HmiVariablesFolder), reveal(s, NK::HmiVariablesFolder)});
    st.push_back({"Le plan d'adressage",
                  "Dans l'onglet de l'\xC3\xA9quipement : chaque variable \xC3\xA0 sa place (mots, bits), les trous et les "
                  "chevauchements signal\xC3\xA9s. Un double-clic sur une ligne ouvre la variable.",
                  row(s, NK::HmiComm), {}});
    st.push_back({"L'outil Modbus",
                  "Lire et \xC3\xA9" "crire une adresse, voir les trames, l'espion, le ping : on v\xC3\xA9rifie l'\xC3\xA9quipement avant "
                  "l'IHM.",
                  row(s, NK::HmiModbusTool), reveal(s, NK::HmiModbusTool)});
    st.push_back({"L'esclave simul\xC3\xA9",
                  "Sans l'\xC3\xA9quipement, son esclave simul\xC3\xA9 r\xC3\xA9pond \xC3\xA0 sa place : la simulation de l'IHM le lit et l'\xC3\xA9" "crit comme le "
                  "vrai ; on y importe une m\xC3\xA9moire (CSV).",
                  row(s, NK::HmiSimulation), reveal(s, NK::HmiSimulation)});
    st.push_back({"En marche : la qualit\xC3\xA9",
                  "En marche, chaque variable li\xC3\xA9" "e a sa qualit\xC3\xA9 (bonne, p\xC3\xA9rim\xC3\xA9" "e, en d\xC3\xA9" "faut). G\xC3\xA9n\xC3\xA9rer v\xC3\xA9rifie les "
                  "adresses et les \xC3\xA9quipements. Pour aller plus loin : F1, \xC2\xAB L'outil Modbus \xC2\xBB.",
                  row(s, NK::HmiGenerate), reveal(s, NK::HmiGenerate)});
    return st;
}

// ============================================================ Creer un symbole de projet
hmi::Id MainAnalysisScreen::TrailRun::ensureView(MainAnalysisScreen& s, TrailRun& run, bool fresh) {
    auto doc = s.app_.hmi();
    if (!doc) return hmi::kNoId;
    if (!run.viewName.empty())
        if (const auto* v = doc->project.viewByName(run.viewName)) {
            run.viewId = v->id;
            return v->id;
        }
    std::string name = kTrailView;
    if (const auto* v = doc->project.viewByName(name)) {
        if (!fresh || v->objects.empty()) {
            run.viewId = v->id;
            run.viewName = v->name;
            s.app_.settings().set("didacticiel.symbole.vue", run.viewName);
            return v->id;
        }
        name = hmi::uniqueViewName(doc->project, kTrailView);
    }
    hmi::Id made = hmi::kNoId;
    auto cmd = hmiNewViewCommand(doc, name, 0, 0, "La vue du parcours \xC2\xAB Cr\xC3\xA9" "er un symbole de projet \xC2\xBB (didacticiel)",
                                 &made, "vue");
    if (!cmd) return hmi::kNoId;
    s.app_.apply(std::move(cmd), false);
    if (const auto* v = doc->project.view(made)) {
        run.viewId = v->id;
        run.viewName = v->name;
        s.app_.settings().set("didacticiel.symbole.vue", run.viewName);
    }
    return made;
}

bool MainAnalysisScreen::TrailRun::findSymbol(MainAnalysisScreen& s, TrailRun& run) {
    auto doc = s.app_.hmi();
    const auto* v = doc ? doc->project.view(run.viewId) : nullptr;
    if (!v) return false;
    if (!run.symbolName.empty() && !instancesIn(*v, run.symbolName).empty() && doc->project.viewByName(run.symbolName)) return true;
    for (const auto* o : instancesIn(*v, {})) {
        const std::string name = o->text("symbol");
        const auto* sym = doc->project.viewByName(name);
        if (sym && hmi::isSymbolView(*sym)) {
            run.symbolName = name;
            s.app_.settings().set("didacticiel.symbole.symbole", name);
            return true;
        }
    }
    return false;
}

std::vector<Step> MainAnalysisScreen::TrailRun::symbol(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run) {
    std::vector<Step> st;
    const auto view = [&s, run]() { return viewOf(s, run->viewId); };
    const auto symbolView = [&s, run]() -> const hmi::View* {
        auto doc = s.app_.hmi();
        return doc && !run->symbolName.empty() ? doc->project.viewByName(run->symbolName) : nullptr;
    };
    const auto selectCard = [&s, run] {
        s.openHmiView(run->viewId);
        auto* ed = editorOf(s, run->viewId);
        const auto* v = viewOf(s, run->viewId);
        if (ed && v) ed->canvas().setSelection(cardObjects(*v));
    };

    // 1. La vue de l'exercice.
    {
        Step x{"Ouvre la vue de l'exercice",
               "Le parcours a cr\xC3\xA9\xC3\xA9 la vue \xC2\xAB " + run->viewName + " \xC2\xBB (IHM \xE2\x80\xBA Vues \xE2\x80\xBA Vues). Ouvre-la : un clic sur son nom "
               "dans l'arbre. Tout ce que le parcours fait se d\xC3\xA9" "fait \xC3\xA0 la fin, si tu veux.",
               [&s, run](Rect& r) { return viewRow(s, run->viewId, r); },
               [&s, run] { if (s.treeModel_) (void)s.revealTreeNode(s.treeModel_->hmiViewNode(run->viewId)); }};
        x.done = [&s, run](std::string& what) {
            auto* ed = currentEditor(s);
            if (!ed || ed->viewId() != run->viewId) return false;
            what = run->viewName + " est ouverte dans l'\xC3\xA9" "diteur";
            return true;
        };
        x.showMe = [&s, run] { s.openHmiView(run->viewId); };
        x.waiting = "J'attends l'ouverture de " + run->viewName + "\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. La carte de l'armoire 1.
    {
        Step x{"Pose une carte pour l'armoire 1",
               "Un cadre (Rectangle), un titre (Texte), deux afficheurs num\xC3\xA9riques et un voyant, reli\xC3\xA9s \xC3\xA0 Armoires[0] : "
               "Valeur (expression) Armoires[0].ana.PT1.mes, Armoires[0].compteur_cycle_pompage, Armoires[0].active. "
               "La biblioth\xC3\xA8que est \xC3\xA0 droite ; Montre-moi pose la carte pour toi.",
               [&s, run](Rect& r) {
                   auto* ed = currentEditor(s);
                   if (!ed || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   r = ed->canvas().bounds();
                   return !r.empty();
               },
               {}};
        x.done = [&s, run, view](std::string& what) {
            const auto* v = view();
            if (!v) return false;
            if (findSymbol(s, *run)) {
                what = "la carte est d\xC3\xA9j\xC3\xA0 devenue le symbole " + run->symbolName;
                return true;
            }
            int frames = 0, linked = 0, total = 0;
            for (const auto& o : v->objects) {
                if (o.kind == hmi::Kind::SymbolInstance) continue;
                ++total;
                if (o.kind == hmi::Kind::Rectangle) ++frames;
                if (mentions(o, "Armoires[0]")) ++linked;
            }
            if (frames < 1 || linked < 2) return false;
            what = "la carte est pos\xC3\xA9" "e : " + std::to_string(total) + " objets, dont " + std::to_string(linked) + " reli\xC3\xA9s \xC3\xA0 Armoires[0]";
            return true;
        };
        x.showMe = [&s, run] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            s.openHmiView(run->viewId);
            auto cmd = hmi::changeProject(doc, "Didacticiel : poser la carte de l'armoire 1", [&](hmi::Project& p) {
                hmi::View* v = p.view(run->viewId);
                if (!v) return;
                (void)addObject(p, *v, hmi::Kind::Rectangle, "Cadre", 60, 60, 330, 236,
                                {{"fill", "#232A35"}, {"stroke", "#4A5568"}, {"radius", "10"}});
                (void)addObject(p, *v, hmi::Kind::Text, "Titre", 72, 70, 306, 36, {{"text", "Armoire"}, {"fontSize", "22"}});
                (void)addObject(p, *v, hmi::Kind::NumericDisplay, "Pression", 80, 118, 210, 56,
                                {{"value", "=Armoires[0].ana.PT1.mes"}, {"unit", "bar"}, {"label", "Pression"}});
                (void)addObject(p, *v, hmi::Kind::NumericDisplay, "Cycles", 80, 190, 210, 50,
                                {{"value", "=Armoires[0].compteur_cycle_pompage"}, {"format", "0"}, {"label", "Cycles"}});
                (void)addObject(p, *v, hmi::Kind::Indicator, "Voyant", 318, 132, 34, 34, {{"value", "=Armoires[0].active"}});
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
        };
        x.waiting = "J'attends la carte : un cadre et au moins deux objets reli\xC3\xA9s \xC3\xA0 Armoires[0]\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. La selection.
    {
        Step x{"Choisis les objets de la carte",
               "Un cadre de s\xC3\xA9lection autour de la carte (glisse dans le vide, d'un coin \xC3\xA0 l'autre), ou Maj+clic sur "
               "chacun des objets.",
               [&s, run, view](Rect& r) {
                   auto* ed = currentEditor(s);
                   const auto* v = view();
                   if (!ed || !v || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   Rect all{};
                   for (const auto id : cardObjects(*v))
                       if (const auto* o = v->object(id)) all = unite(all, screenBox(*ed, *o));
                   r = all;
                   return !r.empty();
               },
               {}};
        x.done = [&s, run, view](std::string& what) {
            const auto* v = view();
            if (!v) return false;
            if (findSymbol(s, *run)) {
                what = "la carte est d\xC3\xA9j\xC3\xA0 un symbole";
                return true;
            }
            auto* ed = editorOf(s, run->viewId);
            if (!ed) return false;
            const auto& sel = ed->canvas().selection();
            int n = 0;
            for (const auto id : sel)
                if (const auto* o = v->object(id); o && o->kind != hmi::Kind::SymbolInstance) ++n;
            if (n < 3) return false;
            what = std::to_string(n) + " objets choisis (" + kindsText(*v, sel) + ")";
            return true;
        };
        x.showMe = selectCard;
        x.waiting = "J'attends la s\xC3\xA9lection (au moins trois objets de la carte)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 4. Creer un symbole.
    {
        Step x{"Transforme la carte en symbole",
               "Clique sur Cr\xC3\xA9" "er un symbole, dans la barre de l'\xC3\xA9" "diteur : les objets choisis partent dans un symbole "
               "r\xC3\xA9utilisable, une instance prend leur place.",
               [&s, run](Rect& r) {
                   auto* ed = currentEditor(s);
                   if (!ed || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   r = ed->tools().rectOf(HmiEditor::ActMakeSymbol);
                   return !r.empty();
               },
               {}};
        x.done = [&s, run](std::string& what) {
            if (auto* top = s.app_.menus().top(); top && top->id() == "dialog.hmiSymbol") {
                what = "le dialogue Cr\xC3\xA9" "er un symbole est ouvert";
                return true;
            }
            if (!findSymbol(s, *run)) return false;
            what = "le symbole " + run->symbolName + " est cr\xC3\xA9\xC3\xA9";
            return true;
        };
        x.showMe = [&s, run, selectCard] {
            auto* ed = editorOf(s, run->viewId);
            if (!ed || ed->canvas().selection().empty()) selectCard();
            s.askHmiSymbol(run->viewId);
        };
        x.waiting = "J'attends le clic sur \xC2\xAB Cr\xC3\xA9" "er un symbole \xC2\xBB\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 5. Le nom et le parametre (le dialogue est au centre : la bulle se met de cote).
    {
        Step x{"Nomme-le, garde son param\xC3\xA8tre",
               "Dans le dialogue : le nom " + s.trailSymbolName_ + " (d\xC3\xA9j\xC3\xA0 propos\xC3\xA9), et le param\xC3\xA8tre lu dans la carte : "
               "Armoire := Armoires[0]. Dans le symbole, Armoires[0] deviendra Armoire. Clique sur Cr\xC3\xA9" "er.",
               {}, {}};
        x.aside = true;
        x.done = [&s, run](std::string& what) {
            if (!findSymbol(s, *run)) return false;
            what = "symbole \xC2\xAB " + run->symbolName + " \xC2\xBB cr\xC3\xA9\xC3\xA9 : une instance remplace la carte";
            return true;
        };
        x.showMe = [&s, run, selectCard, view] {
            if (auto* top = s.app_.menus().top(); top && top->id() == "dialog.hmiSymbol") return;   // le dialogue attend Creer
            auto* ed = editorOf(s, run->viewId);
            if (!ed || ed->canvas().selection().empty()) selectCard();
            ed = editorOf(s, run->viewId);
            const auto* v = view();
            if (!ed || !v) return;
            const auto sel = ed->canvas().selection();
            std::string why;
            const std::string name = s.trailSymbolName_.empty() ? std::string(kTrailSymbol) : s.trailSymbolName_;
            if (!ed->makeSymbol(name, hmi::suggestSymbolParams(*v, sel), &why) && s.status_)
                s.status_->setTransientMessage("Symbole non cr\xC3\xA9\xC3\xA9 : " + why, 8.0);
        };
        x.waiting = "J'attends le symbole (Cr\xC3\xA9" "er, dans le dialogue)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 6. Le symbole ouvert.
    {
        Step x{"Ouvre le symbole",
               "Double-clique l'instance : le symbole s'ouvre dans son \xC3\xA9" "diteur. Ses chemins sont devenus "
               "Armoire.ana.PT1.mes, Armoire.active... : le param\xC3\xA8tre Armoire, que chaque instance relie.",
               [&s, run](Rect& r) {
                   auto* ed = currentEditor(s);
                   const auto* v = viewOf(s, run->viewId);
                   if (!ed || !v || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   const auto inst = instancesIn(*v, run->symbolName);
                   if (inst.empty()) return false;
                   r = screenBox(*ed, *inst.front());
                   return !r.empty();
               },
               {}};
        x.done = [&s, run, symbolView](std::string& what) {
            auto* ed = currentEditor(s);
            const auto* sym = symbolView();
            if (!ed || !sym || ed->viewId() != sym->id) return false;
            what = sym->name + " est ouvert";
            if (!sym->params.empty()) what += " : param\xC3\xA8tre " + sym->params.front().name + " := " + sym->params.front().defaultValue;
            return true;
        };
        x.showMe = [&s, symbolView] {
            if (const auto* sym = symbolView()) s.openHmiView(sym->id);
        };
        x.waiting = "J'attends l'ouverture du symbole (double-clic sur l'instance)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 7. Une deuxieme carte : Armoires[1].
    {
        Step x{"Pose une deuxi\xC3\xA8me carte",
               "Reviens \xC3\xA0 " + run->viewName + " et pose le symbole depuis la biblioth\xC3\xA8que (Symboles du projet), \xC3\xA0 c\xC3\xB4t\xC3\xA9 "
               "de la premi\xC3\xA8re. Dans ses propri\xC3\xA9t\xC3\xA9s, section \xC2\xAB Param\xC3\xA8tres du symbole \xC2\xBB : "
               "\xC3\xA0 la ligne du param\xC3\xA8tre Armoire, la valeur Armoires[1].",
               [&s, run](Rect& r) {
                   auto* ed = currentEditor(s);
                   const auto* v = viewOf(s, run->viewId);
                   if (!ed || !v || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   const auto inst = instancesIn(*v, run->symbolName);
                   if (inst.size() < 2) {
                       if (ed->palette().symbolTileRect(run->symbolName, r)) return true;
                       if (!run->revealed) {
                           run->revealed = true;
                           ed->palette().revealSymbol(run->symbolName);
                       }
                       return false;
                   }
                   // 1.11.2 (SYM) : la ligne du parametre Armoire (« Armoire · <type> ») de la section
                   // « Parametres du symbole » ; la ligne Arguments si l'inspecteur la montre encore.
                   for (const auto& cat : ed->properties().categories()) {
                       if (cat.name != "Param\xC3\xA8tres du symbole") continue;
                       for (const auto& pr : cat.properties)
                           if (pr.name.rfind("Armoire \xC2\xB7 ", 0) == 0 && ed->properties().valueRect(pr.name, r)) return true;
                   }
                   if (ed->properties().valueRect("Arguments", r)) return true;
                   r = screenBox(*ed, *inst.back());
                   return !r.empty();
               },
               [run] { run->revealed = false; }};
        x.done = [&s, run](std::string& what) {
            const auto* v = viewOf(s, run->viewId);
            if (!v) return false;
            const auto inst = instancesIn(*v, run->symbolName);
            if (inst.size() < 2) return false;
            for (const auto* o : inst)
                if (o->text("params").find("Armoires[1]") != std::string::npos) {
                    what = std::to_string(inst.size()) + " cartes : Armoires[0] et Armoires[1]";
                    return true;
                }
            return false;
        };
        x.showMe = [&s, run] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            s.openHmiView(run->viewId);
            hmi::Id made = hmi::kNoId;
            auto cmd = hmi::changeProject(doc, "Didacticiel : poser la carte de l'armoire 2", [&](hmi::Project& p) {
                hmi::View* v = p.view(run->viewId);
                if (!v) return;
                const auto inst = instancesIn(*v, run->symbolName);
                // Une instance sans Armoires[1] : on la relie ; sinon on en pose une.
                for (auto& o : v->objects)
                    if (o.kind == hmi::Kind::SymbolInstance && o.text("symbol") == run->symbolName && inst.size() >= 2
                        && o.id != inst.front()->id && o.text("params").find("Armoires[1]") == std::string::npos) {
                        o.set("params", "Armoire := Armoires[1]");
                        made = o.id;
                        return;
                    }
                double x0 = 60, y0 = 60, w = 330;
                if (!inst.empty()) {
                    const auto b = inst.front()->box();
                    x0 = b.x;
                    y0 = b.y;
                    w = b.w;
                }
                made = hmi::placeSymbol(p, *v, run->symbolName, x0 + w + 40, y0);
                if (auto* o = v->object(made)) o->set("params", "Armoire := Armoires[1]");
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
            if (auto* ed = editorOf(s, run->viewId); ed && made != hmi::kNoId) ed->canvas().setSelection({made});
        };
        x.waiting = "J'attends une deuxi\xC3\xA8me instance, reli\xC3\xA9" "e \xC3\xA0 Armoires[1]\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 8. Modifier le symbole : toutes les cartes suivent.
    {
        Step x{"Change le cadre, dans le symbole",
               "Ouvre le symbole (double-clic sur une carte), choisis le cadre et change son Remplissage : les deux "
               "cartes suivent, sans rien recopier.",
               [&s, run, symbolView](Rect& r) {
                   auto* ed = currentEditor(s);
                   const auto* sym = symbolView();
                   if (!ed || !sym) return false;
                   if (ed->viewId() == sym->id) {
                       if (ed->properties().valueRect("Remplissage", r)) return true;
                       for (const auto& o : sym->objects)
                           if (o.kind == hmi::Kind::Rectangle) {
                               r = screenBox(*ed, o);
                               return !r.empty();
                           }
                       return false;
                   }
                   const auto* v = viewOf(s, run->viewId);
                   if (!v || ed->viewId() != run->viewId) return false;
                   const auto inst = instancesIn(*v, run->symbolName);
                   if (inst.empty()) return false;
                   // Les cartes ensemble : la bulle se place a cote des deux, pas sur la seconde.
                   r = screenBox(*ed, *inst.front());
                   for (const auto* o : inst) {
                       const Rect b = screenBox(*ed, *o);
                       if (b.empty()) continue;
                       const float x0 = std::min(r.x, b.x), y0 = std::min(r.y, b.y);
                       const float x1 = std::max(r.right(), b.right()), y1 = std::max(r.bottom(), b.bottom());
                       r = {x0, y0, x1 - x0, y1 - y0};
                   }
                   return !r.empty();
               },
               [run, symbolView] {
                   if (const auto* sym = symbolView()) run->symbolBefore = sym->objects;
               }};
        x.done = [&s, run, symbolView](std::string& what) {
            const auto* sym = symbolView();
            if (!sym || run->symbolBefore.empty() || sym->objects == run->symbolBefore) return false;
            std::string fill;
            for (const auto& o : sym->objects)
                if (o.kind == hmi::Kind::Rectangle) {
                    fill = o.text("fill");
                    break;
                }
            const auto* v = viewOf(s, run->viewId);
            const std::size_t n = v ? instancesIn(*v, run->symbolName).size() : 0;
            what = (fill.empty() ? std::string("le symbole a chang\xC3\xA9") : "le cadre est " + fill) + " : les " + std::to_string(n) + " cartes suivent";
            return true;
        };
        x.showMe = [&s, run, symbolView] {
            auto doc = s.app_.hmi();
            const auto* sym = symbolView();
            if (!doc || !sym) return;
            const hmi::Id id = sym->id;
            auto cmd = hmi::changeView(doc, id, "Didacticiel : changer le cadre du symbole", [](hmi::Project&, hmi::View& v) {
                for (auto& o : v.objects)
                    if (o.kind == hmi::Kind::Rectangle) {
                        o.set("fill", o.text("fill") == "#1F3A5F" ? "#2B4C7E" : "#1F3A5F");
                        break;
                    }
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
            s.openHmiView(run->viewId);          // les deux cartes, changees ensemble
        };
        x.waiting = "J'attends un changement dans le symbole (le Remplissage du cadre)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 9. En marche.
    {
        Step x{"Lance la simulation",
               "Simulation \xE2\x80\xBA IHM, Marche, puis la vue " + run->viewName + " : chaque carte montre son armoire (la "
               "pression, les cycles, l'\xC3\xA9tat). Ensuite : garder ce que le parcours a cr\xC3\xA9\xC3\xA9, ou tout d\xC3\xA9" "faire.",
               row(s, NK::HmiSimulation), {}};
        x.aside = true;
        x.done = [&s, run](std::string& what) {
            if (!simulating(s, run->viewId)) return false;
            const auto* v = viewOf(s, run->viewId);
            const std::size_t n = v ? instancesIn(*v, run->symbolName).size() : 0;
            what = run->viewName + " en marche : " + std::to_string(n) + " cartes, chacune son armoire";
            return true;
        };
        x.showMe = [&s, run] { simulate(s, run->viewId); };
        x.waiting = "J'attends la simulation de " + run->viewName + "\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Ma premiere vue
std::vector<Step> MainAnalysisScreen::TrailRun::firstView(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run) {
    std::vector<Step> st;
    if (auto doc = s.app_.hmi())
        for (const auto& v : doc->project.views) run->viewsBefore.insert(v.id);
    const auto firstOf = [&s, run](hmi::Kind kind) -> const hmi::Object* {
        const auto* v = viewOf(s, run->viewId);
        if (!v) return nullptr;
        for (const auto& o : v->objects)
            if (o.kind == kind) return &o;
        return nullptr;
    };
    const auto tile = [&s, run](hmi::Kind kind) {
        return std::function<bool(Rect&)>([&s, run, kind](Rect& r) {
            auto* ed = currentEditor(s);
            if (!ed || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
            if (ed->palette().tileRect(kind, r)) return true;
            if (!run->revealed) {
                run->revealed = true;
                ed->palette().revealKind(kind);
            }
            return false;
        });
    };
    const auto add = [&s, run](hmi::Kind kind, const std::string& base, double x, double y, double w, double h,
                               std::vector<std::pair<std::string, std::string>> props) {
        auto doc = s.app_.hmi();
        if (!doc) return hmi::kNoId;
        s.openHmiView(run->viewId);
        hmi::Id made = hmi::kNoId;
        auto cmd = hmi::changeProject(doc, "Didacticiel : poser " + base, [&](hmi::Project& p) {
            if (hmi::View* v = p.view(run->viewId)) made = addObject(p, *v, kind, base, x, y, w, h, props);
        });
        if (cmd) s.app_.apply(std::move(cmd), false);
        if (auto* ed = editorOf(s, run->viewId); ed && made != hmi::kNoId) ed->canvas().setSelection({made});
        return made;
    };

    {
        Step x{"Cr\xC3\xA9" "e une vue",
               "IHM \xE2\x80\xBA Vues \xE2\x80\xBA Vues, Nouvelle vue : un mod\xC3\xA8le de la galerie (Vide suffit), un nom (Ma_Vue), Cr\xC3\xA9" "er.",
               [&s](Rect& r) {
                   if (auto* pane = dynamic_cast<HmiViewsPane*>(currentPage(s))) {
                       const int a = pane->tools().actionByTip("Nouvelle vue");
                       if (a >= 0) {
                           r = pane->tools().rectOf(a);
                           if (!r.empty()) return true;
                       }
                   }
                   return s.explorer_ && s.explorer_->rowRect(nodeOf(s, NK::HmiViewFolder), r);
               },
               reveal(s, NK::HmiViewFolder)};
        x.done = [&s, run](std::string& what) {
            auto doc = s.app_.hmi();
            if (!doc) return false;
            if (run->viewId != hmi::kNoId && doc->project.view(run->viewId)) {
                what = "la vue \xC2\xAB " + run->viewName + " \xC2\xBB est cr\xC3\xA9\xC3\xA9" "e";
                return true;
            }
            for (const auto& v : doc->project.views)
                if (!run->viewsBefore.count(v.id) && v.role == "vue") {
                    run->viewId = v.id;
                    run->viewName = v.name;
                    what = "la vue \xC2\xAB " + v.name + " \xC2\xBB est cr\xC3\xA9\xC3\xA9" "e";
                    return true;
                }
            return false;
        };
        x.showMe = [&s, run] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            hmi::Id made = hmi::kNoId;
            auto cmd = hmiNewViewCommand(doc, "Ma_Vue", 0, 0, "Ma premi\xC3\xA8re vue (didacticiel)", &made, "vue");
            if (!cmd) return;
            s.app_.apply(std::move(cmd), false);
            if (made != hmi::kNoId) s.openHmiView(made);
        };
        x.waiting = "J'attends une nouvelle vue\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Ouvre-la",
               "Un clic sur son nom dans l'arbre (IHM \xE2\x80\xBA Vues \xE2\x80\xBA Vues), ou un double-clic dans la liste des vues : elle "
               "s'ouvre dans l'\xC3\xA9" "diteur.",
               [&s, run](Rect& r) { return viewRow(s, run->viewId, r); },
               [&s, run] { if (s.treeModel_ && run->viewId != hmi::kNoId) (void)s.revealTreeNode(s.treeModel_->hmiViewNode(run->viewId)); }};
        x.done = [&s, run](std::string& what) {
            auto* ed = currentEditor(s);
            if (!ed || run->viewId == hmi::kNoId || ed->viewId() != run->viewId) return false;
            what = run->viewName + " est ouverte dans l'\xC3\xA9" "diteur";
            return true;
        };
        x.showMe = [&s, run] { if (run->viewId != hmi::kNoId) s.openHmiView(run->viewId); };
        x.waiting = "J'attends l'ouverture de la vue\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Pose un afficheur num\xC3\xA9rique",
               "Dans la biblioth\xC3\xA8que, \xC3\xA0 droite : Afficheur num\xC3\xA9rique. Clique la tuile puis clique dans la vue (ou "
               "glisse-la dans la vue).",
               tile(hmi::Kind::NumericDisplay), [run] { run->revealed = false; }};
        x.done = [firstOf](std::string& what) {
            if (!firstOf(hmi::Kind::NumericDisplay)) return false;
            what = "un afficheur num\xC3\xA9rique est pos\xC3\xA9";
            return true;
        };
        x.showMe = [add] { (void)add(hmi::Kind::NumericDisplay, "Afficheur", 80, 80, 240, 56, {}); };
        x.waiting = "J'attends un afficheur num\xC3\xA9rique dans la vue\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Relie-le \xC3\xA0 une variable",
               "Choisis l'afficheur ; dans ses propri\xC3\xA9t\xC3\xA9s, Valeur (expression) : Armoires[0].ana.PT1.mes. L'aide \xC3\xA0 la "
               "saisie propose les noms pendant la frappe (Ctrl+Espace).",
               [&s, run, firstOf](Rect& r) {
                   auto* ed = currentEditor(s);
                   if (!ed || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   if (ed->properties().valueRect("Valeur (expression)", r)) return true;
                   if (const auto* o = firstOf(hmi::Kind::NumericDisplay)) {
                       r = screenBox(*ed, *o);
                       return !r.empty();
                   }
                   return false;
               },
               {}};
        x.done = [&s, run](std::string& what) {
            const auto* v = viewOf(s, run->viewId);
            if (!v) return false;
            for (const auto& o : v->objects) {
                if (o.kind != hmi::Kind::NumericDisplay) continue;
                const auto* p = o.find("value");
                const std::string value = p ? (p->expr.empty() ? p->value : p->expr) : std::string{};
                if (std::any_of(value.begin(), value.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; })) {
                    what = "l'afficheur lit " + value;
                    return true;
                }
            }
            return false;
        };
        x.showMe = [&s, run] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            hmi::Id id = hmi::kNoId;
            auto cmd = hmi::changeView(doc, run->viewId, "Didacticiel : relier l'afficheur", [&](hmi::Project&, hmi::View& v) {
                for (auto& o : v.objects)
                    if (o.kind == hmi::Kind::NumericDisplay) {
                        o.setExpr("value", "Armoires[0].ana.PT1.mes");
                        o.set("unit", "bar");
                        o.set("label", "Pression");
                        id = o.id;
                        break;
                    }
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
            if (auto* ed = editorOf(s, run->viewId); ed && id != hmi::kNoId) ed->canvas().setSelection({id});
        };
        x.waiting = "J'attends une variable dans la Valeur de l'afficheur\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Pose un bouton",
               "Dans la biblioth\xC3\xA8que : Bouton. Pose-le sous l'afficheur.",
               tile(hmi::Kind::Button), [run] { run->revealed = false; }};
        x.done = [firstOf](std::string& what) {
            if (!firstOf(hmi::Kind::Button)) return false;
            what = "un bouton est pos\xC3\xA9";
            return true;
        };
        x.showMe = [add] { (void)add(hmi::Kind::Button, "Bouton", 80, 170, 160, 48, {{"text", "Accueil"}}); };
        x.waiting = "J'attends un bouton dans la vue\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Donne-lui une action",
               "Choisis le bouton ; onglet Actions de l'inspecteur : Ajouter une action, Op\xC3\xA9ration : Naviguer (vers la "
               "vue d'accueil), ou \xC3\x89" "crire une valeur dans une variable.",
               [&s, run, firstOf](Rect& r) {
                   auto* ed = currentEditor(s);
                   if (!ed || ed->viewId() != run->viewId) return viewRow(s, run->viewId, r);
                   if (const auto* o = firstOf(hmi::Kind::Button)) {
                       r = screenBox(*ed, *o);
                       return !r.empty();
                   }
                   return false;
               },
               {}};
        x.done = [&s, run](std::string& what) {
            const auto* v = viewOf(s, run->viewId);
            if (!v) return false;
            for (const auto& o : v->objects)
                if (o.kind == hmi::Kind::Button && !o.actions.empty()) {
                    what = hmi::describeAction(o.actions.front());
                    return true;
                }
            return false;
        };
        x.showMe = [&s, run] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            std::string target;
            if (const auto* start = doc->project.view(doc->project.config.startView); start && start->id != run->viewId) target = start->name;
            for (const auto& v : doc->project.views)
                if (target.empty() && v.id != run->viewId && v.role == "vue") target = v.name;
            auto cmd = hmi::changeView(doc, run->viewId, "Didacticiel : l'action du bouton", [&](hmi::Project&, hmi::View& v) {
                for (auto& o : v.objects)
                    if (o.kind == hmi::Kind::Button) {
                        hmi::Action a;
                        a.trigger = hmi::Trigger::Click;
                        a.operation = hmi::Operation::Navigate;
                        a.target = target;
                        o.actions.push_back(a);
                        break;
                    }
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
        };
        x.waiting = "J'attends une action sur le bouton\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Simule-la",
               "Simulation \xE2\x80\xBA IHM, Marche, puis ta vue : l'afficheur montre la pression de l'armoire, le bouton fait "
               "son action. Ensuite : garder ta vue, ou tout d\xC3\xA9" "faire.",
               row(s, NK::HmiSimulation), {}};
        x.aside = true;
        x.done = [&s, run](std::string& what) {
            if (run->viewId == hmi::kNoId || !simulating(s, run->viewId)) return false;
            what = run->viewName + " en marche";
            return true;
        };
        x.showMe = [&s, run] { if (run->viewId != hmi::kNoId) simulate(s, run->viewId); };
        x.waiting = "J'attends la simulation de ta vue\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Coller depuis Excel
std::vector<Step> MainAnalysisScreen::TrailRun::excel(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run) {
    std::vector<Step> st;
    const auto varsPane = [&s]() -> HmiVariablesPane* {
        auto* pane = dynamic_cast<HmiScriptsPane*>(s.hmiTab("scripts"));
        return pane && currentPage(s) == pane && pane->currentTab() == HmiScriptsPane::TabVariables ? pane->variablesPane() : nullptr;
    };
    const auto tableRect = [varsPane](Rect& r) {
        auto* vars = varsPane();
        if (!vars) return false;
        r = vars->table().bounds();
        return !r.empty();
    };
    {
        Step x{"Ouvre les variables IHM",
               "IHM \xE2\x80\xBA Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM : la table o\xC3\xB9 les lignes vont se coller.",
               row(s, NK::HmiVariablesFolder), reveal(s, NK::HmiVariablesFolder)};
        x.done = [varsPane](std::string& what) {
            if (!varsPane()) return false;
            what = "la table des variables IHM est ouverte";
            return true;
        };
        x.showMe = [&s] {
            s.openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(s.hmiTab("scripts"))) pane->showTab(HmiScriptsPane::TabVariables);
        };
        x.waiting = "J'attends l'onglet Variables IHM\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Copie des lignes dans un tableur",
               "Dans Excel (ou LibreOffice), choisis des lignes avec leurs titres - Nom, Type, Initiale, Description, "
               "Dossier - et Ctrl+C. Pas de tableur sous la main ? Montre-moi en met trois dans le presse-papiers.",
               tableRect, {}};
        x.done = [](std::string& what) {
            const auto grid = paste::parseGrid(ui::clipboardText());
            std::size_t cols = 0;
            for (const auto& line : grid) cols = std::max(cols, line.size());
            if (grid.size() < 2 || cols < 2) return false;
            what = std::to_string(grid.size()) + " lignes de " + std::to_string(cols) + " colonnes dans le presse-papiers";
            return true;
        };
        x.showMe = [] {
            ui::setClipboardText("Nom\tType\tInitiale\tDescription\tDossier\n"
                                 "Didacticiel_Consigne\tREAL\t12.5\tConsigne de l'exercice\tDidacticiel\n"
                                 "Didacticiel_Marche\tBOOL\tFALSE\tMarche demand\xC3\xA9" "e\tDidacticiel\n"
                                 "Didacticiel_Compteur\tINT\t0\tCompteur de cycles\tDidacticiel\n");
        };
        x.waiting = "J'attends un tableau dans le presse-papiers (Ctrl+C dans le tableur)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Colle dans la table",
               "Clique dans la table des variables, puis Ctrl+V. Les colonnes sont reconnues par leur titre ; un nom "
               "nouveau cr\xC3\xA9" "e la variable, un nom connu la met \xC3\xA0 jour ; une case refus\xC3\xA9" "e est marqu\xC3\xA9" "e, avec sa raison.",
               tableRect,
               [varsPane, run] {
                   run->pasted.clear();
                   if (auto* vars = varsPane()) vars->table().clearPasteResult();
               }};
        x.done = [varsPane, run](std::string& what) {
            auto* vars = varsPane();
            if (!vars || vars->table().pasteBanner().empty()) return false;
            run->pasted.clear();
            for (const auto& m : vars->table().pasteMarks())
                if (m.kind == ui::TableView::MarkKind::Created) run->pasted.push_back(m.key);
            what = vars->table().pasteBanner();
            if (const auto nl = what.find('\n'); nl != std::string::npos) what.resize(nl);
            return true;
        };
        x.showMe = [&s, varsPane] {
            if (!varsPane()) {
                s.openHmiPane("scripts");
                if (auto* pane = dynamic_cast<HmiScriptsPane*>(s.hmiTab("scripts"))) pane->showTab(HmiScriptsPane::TabVariables);
            }
            if (auto* vars = varsPane()) (void)vars->table().pasteFromClipboard(false);
        };
        x.waiting = "J'attends le collage (Ctrl+V dans la table)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Un seul Ctrl+Z",
               "Tout le collage est UN pas de l'historique : Ctrl+Z le retire d'un coup, Ctrl+Y le remet. Essaie.",
               [&s](Rect& r) { return barPart(s, "annuler", r); }, {}};
        x.done = [&s, run](std::string& what) {
            auto doc = s.app_.hmi();
            if (!doc) return false;
            if (run->pasted.empty()) {
                const auto& stack = s.app_.commands();
                if (!stack.canRedo() || stack.redoLabel().rfind("Coller", 0) != 0) return false;
            } else {
                for (const auto& name : run->pasted)
                    for (const auto& v : doc->project.programs.variables)
                        if (v.name == name) return false;
            }
            what = "le collage est d\xC3\xA9" "fait d'un coup (Ctrl+Y le remet)";
            return true;
        };
        x.showMe = [&s] { (void)s.app_.actions().trigger("edit.undo", s.app_.commands()); };
        x.waiting = "J'attends Ctrl+Z\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Historique et versions
std::vector<Step> MainAnalysisScreen::TrailRun::versions(MainAnalysisScreen& s, const std::shared_ptr<TrailRun>& run) {
    std::vector<Step> st;
    const auto versionCount = [&s]() -> std::size_t {
        const std::string folder = s.app_.projectFolder();
        if (folder.empty()) return 0;
        auto store = ver::open(folder);
        return store ? store->versions.size() : 0;
    };
    {
        Step x{"Fais un changement",
               "Change quelque chose : renomme une variable IHM, d\xC3\xA9place un objet, change une couleur... Montre-moi "
               "ajoute la variable IHM Didacticiel_Essai.",
               {}, [&s, run] { run->stepSerial = topSerial(s.app_.commands()); }};
        x.done = [&s, run](std::string& what) {
            const auto& stack = s.app_.commands();
            if (topSerial(stack) == run->stepSerial || !stack.canUndo()) return false;
            what = "\xC2\xAB " + stack.undoLabel() + " \xC2\xBB est dans l'historique";
            return true;
        };
        x.showMe = [&s] {
            auto doc = s.app_.hmi();
            if (!doc) return;
            auto cmd = hmi::changeProject(doc, "Didacticiel : nouvelle variable IHM", [](hmi::Project& p) {
                hmi::Variable v;
                v.id = p.allocate();
                v.name = hmi::uniqueVariableName(p, "Didacticiel_Essai");
                v.type = "INT";
                v.initial = "0";
                v.description = "Cr\xC3\xA9\xC3\xA9" "e par le didacticiel";
                p.programs.variables.push_back(std::move(v));
            });
            if (cmd) s.app_.apply(std::move(cmd), false);
        };
        x.waiting = "J'attends un changement\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Annule, puis r\xC3\xA9tablis",
               "Ctrl+Z : le changement part, la barre d'\xC3\xA9tat dit lequel et l'\xC3\xA9" "cran montre l'endroit. Ctrl+Y le remet.",
               [&s](Rect& r) {
                   Rect a{}, b{};
                   const bool ok = barPart(s, "annuler", a);
                   const bool ok2 = barPart(s, "retablir", b);
                   r = unite(ok ? a : Rect{}, ok2 ? b : Rect{});
                   return !r.empty();
               },
               [&s, run] {
                   run->stepSerial = topSerial(s.app_.commands());
                   run->stepRevision = s.app_.commands().revision();
               }};
        x.done = [&s, run](std::string& what) {
            const auto& stack = s.app_.commands();
            if (stack.revision() < run->stepRevision + 2 || topSerial(stack) != run->stepSerial || stack.canRedo()) return false;
            what = "annul\xC3\xA9 puis r\xC3\xA9tabli : \xC2\xAB " + stack.undoLabel() + " \xC2\xBB";
            return true;
        };
        x.showMe = [&s] {
            (void)s.app_.actions().trigger("edit.undo", s.app_.commands());
            (void)s.app_.actions().trigger("edit.redo", s.app_.commands());
        };
        x.waiting = "J'attends Ctrl+Z puis Ctrl+Y\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Ouvre l'historique",
               // Lot API 8 : bandeau haut - Historique est la derniere ligne de la liste d'Annuler (le chevron).
               "Ctrl+H, ou ce chevron (sa derni\xC3\xA8re ligne) : chaque action, son endroit, son heure. Un double clic sur une ligne revient \xC3\xA0 "
               "cet \xC3\xA9tat ; la ligne \xC2\xAB \xC3\x89tat actuel \xC2\xBB y ram\xC3\xA8ne.",
               [&s](Rect& r) { return barPart(s, "historique", r); }, {}};
        x.done = [&s](std::string& what) {
            if (!s.historyPanel_ || !s.historyPanel_->isOpen()) return false;
            what = "l'historique est ouvert (" + std::to_string(s.app_.commands().done().size()) + " pas)";
            return true;
        };
        x.showMe = [&s] {
            if (!s.historyPanel_ || !s.historyPanel_->isOpen()) s.toggleHistory();
        };
        x.waiting = "J'attends l'historique (Ctrl+H)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Cr\xC3\xA9" "e une version",
               "Ctrl+Alt+S : une version garde tout le projet tel qu'il est. Nomme-la, dis son \xC3\xA9tat (brouillon, "
               "valid\xC3\xA9" "e, livr\xC3\xA9" "e) ; le projet s'enregistre d'abord. Elle appara\xC3\xAEt dans l'arbre, sous Versions.",
               row(s, NK::VersionsFolder),
               [&s, run, versionCount] {
                   run->versionsBefore = versionCount();
                   (void)s.revealTreeNode(ProjectTreeModel::versionsFolderNode());
               }};
        x.done = [&s, run, versionCount](std::string& what) {
            if (versionCount() <= run->versionsBefore) return false;
            what = "une version de plus";
            if (auto store = ver::open(s.app_.projectFolder()); store && store->last()) what = store->last()->label() + " est cr\xC3\xA9\xC3\xA9" "e";
            return true;
        };
        x.showMe = [&s] {
            const std::string folder = s.app_.projectFolder();
            if (folder.empty()) {
                if (s.status_) s.status_->setTransientMessage("Enregistre d'abord le projet (Enregistrer sous\xE2\x80\xA6) : une version prend son dossier.", 8.0);
                return;
            }
            if (s.app_.pendingChanges() > 0) (void)s.app_.saveProject();
            auto store = ver::open(folder);
            if (!store) return;
            auto made = ver::create(*store, "Didacticiel", ver::State::Draft, "Cr\xC3\xA9\xC3\xA9" "e par le parcours Historique et versions",
                                    ver::defaultAuthor());
            if (!made) return;
            s.refreshVersions();
            s.openVersions(made->number);
        };
        x.waiting = "J'attends une nouvelle version (Ctrl+Alt+S)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Compare-la",
               "IHM \xE2\x80\xBA Versions : choisis la nouvelle version, puis Comparer. Ce qui a chang\xC3\xA9, \xC3\xA9l\xC3\xA9ment par \xC3\xA9l\xC3\xA9ment : "
               "les sections, les vues, les variables ; un texte c\xC3\xB4te \xC3\xA0 c\xC3\xB4te, une vue en vignettes.",
               [&s](Rect& r) {
                   if (auto* pane = dynamic_cast<VersionsPane*>(currentPage(s))) {
                       r = pane->tools().rectOf(VersionsPane::ACompare);
                       return !r.empty();
                   }
                   return s.explorer_ && s.explorer_->rowRect(ProjectTreeModel::versionsFolderNode(), r);
               },
               {}};
        x.done = [&s](std::string& what) {
            auto* cmp = dynamic_cast<VersionComparePane*>(s.hmiTab("comparer"));
            if (!cmp || currentPage(s) != cmp) return false;
            what = cmp->title() + " : " + std::to_string(cmp->comparison().elements.size()) + " diff\xC3\xA9rence(s)";
            return true;
        };
        x.showMe = [&s] {
            auto store = ver::open(s.app_.projectFolder());
            if (!store || store->versions.empty()) return;
            int newest = 0, previous = 0;
            for (const auto& v : store->versions) newest = std::max(newest, v.number);
            for (const auto& v : store->versions)
                if (v.number < newest) previous = std::max(previous, v.number);
            if (previous > 0) s.openVersionCompare(previous, newest);
            else s.openVersionCompare(newest, 0);
        };
        x.waiting = "J'attends la comparaison (Comparer, dans Versions)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    {
        Step x{"Restaurer, au besoin",
               "Dans la comparaison, \xC2\xAB Restaurer cet \xC3\xA9l\xC3\xA9ment \xC2\xBB remet une section, une vue, un script, en une commande "
               "(Ctrl+Z l'annule). Restaurer une version enti\xC3\xA8re garde d'abord l'\xC3\xA9tat d'avant, en version \xC2\xAB avant "
               "restauration \xC2\xBB. Les versions restent sur le disque : Tout d\xC3\xA9" "faire ne les retire pas.",
               {}, {}};
        x.done = [](std::string& what) {
            what = "tu sais revenir en arri\xC3\xA8re : Ctrl+Z, l'historique, les versions";
            return true;
        };
        st.push_back(std::move(x));
    }
    return st;
}

} // namespace app
