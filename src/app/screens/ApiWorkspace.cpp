// =============================================================================
//  app/screens/ApiWorkspace.cpp - lot API 2 : la barre du haut, les onglets de l'API
// -----------------------------------------------------------------------------
//  La barre du haut (TopBar) : ses actions gardent les identifiants d'avant ;
//  ce fichier route les nouvelles (le tableau de bord, les panneaux, les pages
//  de l'aide) et tient la barre a jour (le projet, son etat, le point
//  « modifie », la simulation).
//
//  Les onglets de l'API, comme ceux de l'IHM (openHmiPane) : un par entree de
//  l'arbre, retrouve s'il est deja ouvert. Ce lot : le tableau de bord (un clic
//  sur « API »), Configuration (une page GARDEE : le rack et ses proprietes,
//  relies au projet depuis le debut), Taches, Sous-routines. Les types, les
//  blocs, l'ordre, les variables et les tables d'animation suivent (lots 3 a 5).
// =============================================================================
#include "Screens.hpp"
#include "../SectionComparePane.hpp"   // 1.8.0 : le comparateur de sections
#include "../../project/ProjectIcon.hpp"

#include "../AnimationTablesPane.hpp"
#include "../ApiListKit.hpp"            // lot API 7 : apikit::libraryState
#include "../ApiPanes.hpp"
#include "../App.hpp"
#include "../ConditionsNotice.hpp"       // 1.11 (R111) : « Ne plus le dire pour ce projet »
#include "../ConfigurationPane.hpp"
#include "../MacrosPane.hpp"
#include "../SimulationPane.hpp"         // lot API 7 : API > Simulation
#include "../StatisticsPane.hpp"         // lot API 7 : API > Statistiques
#include "../TaskPanes.hpp"
#include "../TopBar.hpp"
#include "../TutorialsLot8.hpp"          // Lot API 8 : didacticiels et aide (Aide > Nouveautes, Raccourcis)
#include "../TypePanes.hpp"
#include "../VariablePicker.hpp"
#include "../VariablesPane.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiPanels.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../hmi/HmiTreeData.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../import/ProjectImporter.hpp"
#include "../../project/ApiChecks.hpp"
#include "../../project/ApiCommands.hpp"
#include "../../project/ProjectStore.hpp"
#include "../../project/Security.hpp"
#include "../../project/SharedLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace app {

using namespace ui;
using NK = ProjectTreeModel::NodeKind;

namespace {

AnimationTablesPane* tablesPaneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<AnimationTablesPane*>(&frame->content()) : nullptr;
}

// Le volet d'un onglet de l'API (nul : pas ouvert, ou pas de ce genre).
template <typename Pane>
Pane* paneOf(ui::Widget* page) {
    auto* frame = dynamic_cast<ApiFrame*>(page);
    return frame ? dynamic_cast<Pane*>(&frame->content()) : nullptr;
}

// Lot API 7 : un widget a l'ecran - lui et tous ses parents visibles. Un onglet
// cache ne l'est pas ; l'onglet montre d'un autre groupe, ou une page detachee
// dans sa fenetre, si. "L'onglet courant" ne suffit plus a le dire.
bool onScreen(const ui::Widget* w) {
    if (!w) return false;
    for (; w; w = w->parent())
        if (!w->visible()) return false;
    return true;
}

std::string lowerText(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Lot API 7 : `a` est-elle une version plus recente que `b` ? Par morceaux
// separes de points : deux nombres se comparent en nombres ("0.24" apres
// "0.3" : 24 > 3 ; "1.10" apres "1.9"), le reste en texte ; un morceau absent
// vaut zero ("1.2.1" apres "1.2").
bool versionNewer(std::string_view a, std::string_view b) {
    const auto digits = [](std::string_view s) {
        if (s.empty()) return false;
        for (const char c : s) if (c < '0' || c > '9') return false;
        return true;
    };
    const auto next = [](std::string_view s, std::size_t& at) {
        if (at >= s.size()) return std::string_view{};
        const auto dot = s.find('.', at);
        const auto part = s.substr(at, dot == std::string_view::npos ? std::string_view::npos : dot - at);
        at = dot == std::string_view::npos ? s.size() : dot + 1;
        return part;
    };
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        auto pa = next(a, i), pb = next(b, j);
        if (pa.empty() && digits(pb)) pa = "0";
        if (pb.empty() && digits(pa)) pb = "0";
        if (digits(pa) && digits(pb)) {
            // Sans les zeros de tete, le plus long est le plus grand : pas de
            // conversion, donc pas de depassement sur un numero absurde.
            while (pa.size() > 1 && pa.front() == '0') pa.remove_prefix(1);
            while (pb.size() > 1 && pb.front() == '0') pb.remove_prefix(1);
            if (pa.size() != pb.size()) return pa.size() > pb.size();
        }
        if (pa != pb) return pa > pb;
    }
    return false;
}

} // namespace

// ------------------------------------------------------------ la barre ----
void MainAnalysisScreen::onBarAction(std::string_view id) {
    if (lisibleBarAction(id)) return;   // 1.8.0 : l'export lisible, l'import suivi
    if (id == "sim.run" || id == "sim.pause" || id == "sim.stop" || id == "sim.step") {
        runSimulationTransport(id);
        return;
    }
    // ---- Lot API 8 : Centre de simulation (un clic sur l'etat de la barre du haut) ----
    if (id == "sim.center") { (void)openSimCenter("ensemble"); return; }
    // ---- fin Lot API 8 : Centre de simulation ----
    // ---- Lot API 8 : bandeau haut (Revenir avant, un projet recent, la cloche, Deposer) ----
    if (topBarLot8Action(id)) return;
    // ---- fin Lot API 8 : bandeau haut ----
    if (id == "edit.history") { toggleHistory(); return; }
    if (id.rfind("version.", 0) == 0) { onVersionAction(std::string(id)); return; }   // lot API 6
    if (id == "project.icon") { showIconEditor(); return; }                            // lot API 6
    if (id == "macros.open") { openMacros(); return; }   // lot macros 1
    if (id == "macros.new") {
        openMacros();
        if (auto* m = macrosPane()) m->runAction(MacrosPane::ANewMacro);
        return;
    }
    if (id == "api.dashboard") { openApiPane("api"); return; }
    // 1.12.2 : Projet > Nouveau dans XPGAnalyser IHM - les creations de l'IHM.
    if (id == "hmi.new.view") { askNewHmiView("vue"); return; }
    if (id == "hmi.new.popup") { askNewHmiView("popup"); return; }
    if (id == "hmi.new.symbol") { askNewHmiView("symbole"); return; }
    if (id == "hmi.new.script") { askHmiNewScript(hmi::ScriptLang::ST); return; }
    if (id == "hmi.new.function") { askHmiNewFunction(); return; }
    if (id == "hmi.new.variable") { askHmiVariable(0); return; }
    // 1.11 (R111, decision 15) : le bouton de l'avis des conditions d'activation
    // manquantes - le dossier du projet dans les reglages de l'utilisateur.
    if (id == conditions::kSilenceAction) {
        if (conditions::silence(app_.settings(), app_.projectFolder())) {
            if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("ordre")))   // sa ligne d'aide n'en parle plus
                if (auto* pane = dynamic_cast<ExecutionOrderPane*>(&frame->content())) pane->refresh();
            app_.events().publish(StatusNotice{"Conditions d'activation : plus rien n'en sera dit pour ce projet (r\xC3\xA9glages de l'utilisateur).", 8.0});
        }
        return;
    }
    // Lot 7 : l'affichage multi-fenetre (Affichage > Disposition).
    if (id == "layout.tabs")   { (void)setLayoutMode("onglets"); return; }
    if (id == "layout.groups") { (void)setLayoutMode("groupes"); return; }
    if (id == "layout.mosaic") { (void)setLayoutMode("mosaique"); return; }
    if (id == "layout.detach") {
        if (centre_ && centre_->tabCount() > 0) (void)detachTab(centre_->currentIndex());
        return;
    }
    if (id == "view.layout") {
        // Le centre doit etre a l'ecran, sinon l'onglet s'ouvre dans un panneau cache.
        if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
        openKeptPage("panneaux");
        return;
    }
    if (id == "help.apiTutorial") { openApiTutorial(); return; }      // lot API 7 : le didacticiel de l'API
    if (id == "help.hmiTutorial") { openHmiHelp("didacticiel"); return; }   // 1.12.0 : celui de l'IHM (ses parcours)
    // ---- Lot API 8 : didacticiels et aide ----
    if (id == lot8::kNewsAction) { openApiTutorial("nouveautes-lot8"); return; }   // l'onglet des nouveautes
    if (id == lot8::kShortcutsAction) {                                           // la page des raccourcis
        lot8::setPendingHelpAnchor(lot8::kShortcutsAnchor);
        app_.menus().PushMenu("help");
        return;
    }
    // ---- fin Lot API 8 : didacticiels et aide ----
    if (id == "help.macros" || id == "help.blocs" || id == "help.hmi") {
        app_.menus().PushMenu(std::string(id));
        return;
    }
    (void)app_.actions().trigger(id, app_.commands());
}

void MainAnalysisScreen::refreshTopBar() {
    if (!topBar_) return;
    auto project = app_.project();
    std::string name, state;
    if (!app_.projectFolder().empty()) {
        name = std::filesystem::path(app_.projectFolder()).filename().string();
        state = std::string(project::toString(app_.manifest().state));
    }
    if (name.empty() && project) name = project->header.projectName;
    topBar_->setProject(std::move(name), std::move(state), project != nullptr && app_.commands().isModified());
    // Lot API 6 : la version que l'on modifie, et l'icone du projet.
    refreshVersionChip();
    const domain::ProjectIcon none;
    const auto& icon = project ? project->icon : none;
    if (!(icon == logoShown_)) {
        logoShown_ = icon;
        topBar_->setLogo(icon.empty() ? std::vector<std::uint8_t>{} : project::icon::rgba(icon));
    }
    refreshTopBarLot8();    // ---- Lot API 8 : bandeau haut (recents, 10 dernieres actions, cloche, taches) ----
}

// ------------------------------------------------------- les pages gardees ----
std::size_t MainAnalysisScreen::openKeptPage(const std::string& key) {
    const auto it = keptPages_.find(key);
    if (it == keptPages_.end() || !centre_) return static_cast<std::size_t>(-1);
    auto& k = it->second;
    if (k.parked) {
        const auto at = centre_->addTab(k.meta, std::move(k.parked));
        centre_->setCurrentIndex(at);
        return at;
    }
    const int at = centre_->indexOf(k.page);
    if (at >= 0) centre_->setCurrentIndex(static_cast<std::size_t>(at));
    else (void)showPage(k.page);        // lot 7 : detachee, sa fenetre devant
    return at >= 0 ? static_cast<std::size_t>(at) : static_cast<std::size_t>(-1);
}

bool MainAnalysisScreen::parkTab(std::size_t index) {
    const auto* page = centre_ ? centre_->page(index) : nullptr;
    if (!page) return false;
    for (auto& [key, k] : keptPages_)
        if (k.page == page && !k.parked) {
            k.parked = centre_->takeTab(index);
            return true;
        }
    return false;
}

bool MainAnalysisScreen::isApiTab(const ui::Widget* page) const {
    if (!page) return false;
    for (const auto& [key, w] : apiTabs_) if (w == page) return true;
    for (const auto& [key, k] : keptPages_) if (k.page == page) return true;
    return false;
}

ui::Widget* MainAnalysisScreen::apiTab(const std::string& key) const {
    const auto it = apiTabs_.find(key);
    return it == apiTabs_.end() ? nullptr : it->second;
}

// Lot API 7 : pour les scripts (simulation-forcer, statistiques-vue...) - le
// volet par son onglet, quel que soit l'onglet courant.
ui::Widget* MainAnalysisScreen::apiPaneContent(const std::string& key, bool open) {
    if (open && !apiTab(key)) openApiTabFromAction(key);
    auto* frame = dynamic_cast<ApiFrame*>(apiTab(key));
    return frame ? &frame->content() : nullptr;
}

// ------------------------------------------------------- les onglets ----
void MainAnalysisScreen::openApiPane(const std::string& key) {
    if (!centre_) return;
    if (auto* page = apiTab(key)) {
        // Lot 7 : en onglet (devant), ou dans sa fenetre detachee (devant) -
        // jamais un deuxieme volet pour le meme onglet.
        if (showPage(page)) return;
        apiTabs_.erase(key);
    }
    auto project = app_.project();
    ui::WidgetPtr pane;
    std::string title;
    Icon icon = Icon::Cpu;
    if (key == "api") {
        auto dash = std::make_unique<ApiDashboard>("analysis.api.tableau");
        paneLinks_ += dash->openRequested->connect([this](const std::string& k) { onApiRequest(k); });
        auto frame = std::make_unique<ApiFrame>("analysis.api", std::move(dash));
        auto& tools = frame->tools();
        tools.add(1, HmiGlyph::Refresh, "R\xC3\xA9importer et r\xC3\xA9" "analyser les fichiers du projet (F5)", "R\xC3\xA9" "analyser");
        tools.add(2, HmiGlyph::Import, "Importer le .XHW : les racks, les modules, leurs voies", "Importer le .XHW\xE2\x80\xA6");
        tools.separator();
        tools.add(3, HmiGlyph::Export, "\xC3\x89" "crit src/MAST.XPG et src/CONFIG.XHW, pr\xC3\xAAts \xC3\xA0 r\xC3\xA9importer", "Vers Control Expert\xE2\x80\xA6");
        tools.separator();
        tools.add(4, HmiGlyph::Code, "L'onglet Macros : lancer, ranger, cr\xC3\xA9" "er", "Macros");
        tools.add(5, HmiGlyph::Gear, "Le processeur, les racks et leurs modules", "Configuration");
        tools.add(6, HmiGlyph::Help, "L'aide (F1)", "Aide");
        paneLinks_ += tools.triggered->connect([this](int a) {
            switch (a) {
                case 1: (void)app_.actions().trigger("analyze.run", app_.commands()); break;
                case 2: (void)app_.actions().trigger("file.importHardware", app_.commands()); break;
                case 3: (void)app_.actions().trigger("project.exportSources", app_.commands()); break;
                case 4: openMacros(); break;
                case 5: openApiPane("configuration"); break;
                default: (void)app_.actions().trigger("help.open", app_.commands()); break;
            }
        });
        frame->setHint("Chaque carte ouvre son onglet ; chaque ligne de \xC2\xAB \xC3\x80 regarder \xC2\xBB a le bouton qui la r\xC3\xA8gle.");
        pane = std::move(frame);
        title = "API";
        icon = Icon::Cpu;
    } else if (key == "taches") {
        // Lot API 4 : les taches se modifient (cyclique / periodique, periode,
        // chien de garde, FAST, AUX) - chaque changement est une commande.
        auto panel = std::make_unique<TasksPane>("analysis.api.taches.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.taches", std::move(panel));
        raw->attach(*frame);
        raw->setHosts(apiHosts());
        pane = std::move(frame);
        title = "API \xC2\xB7 T\xC3\xA2" "ches";
        icon = Icon::Task;
    } else if (key == "ordre") {
        auto panel = std::make_unique<ExecutionOrderPane>("analysis.api.ordre.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.ordre", std::move(panel));
        raw->attach(*frame);
        raw->setHosts(apiHosts());
        pane = std::move(frame);
        title = "API \xC2\xB7 Ordre d'ex\xC3\xA9" "cution";
        icon = Icon::Section;
    } else if (key == "configuration") {
        auto panel = std::make_unique<ConfigurationPane>("analysis.api.configuration.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.configuration", std::move(panel));
        raw->attach(*frame);
        raw->setHosts(apiHosts());
        pane = std::move(frame);
        title = "API \xC2\xB7 Configuration";
        icon = Icon::Settings;
    } else if (key == "tables") {
        auto panel = std::make_unique<AnimationTablesPane>("analysis.api.tables.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.tables", std::move(panel));
        raw->attach(*frame);
        AnimationTablesPane::Hosts h;
        h.project = [this] { return app_.document(); };
        // Lot API 6 : pendant un collage (un groupe), pas de rafraichissement a
        // chaque ligne ; refreshViews le fait une fois, a la fin.
        h.apply = [this](core::CommandPtr c) { app_.apply(std::move(c), !app_.commands().grouping()); };
        h.refreshViews = [this] { onApiRequest("rafraichir"); };
        h.plc = [this] { return app_.simulationRuntime(); };
        h.running = [this] { return app_.simulation().state() == SimulationHost::State::Running; };
        h.hmi = [this] { return app_.hmi(); };
        h.hmiRuntime = [this]() -> hmi::Runtime* {
            auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
            return sim && sim->runtime().running() ? &sim->runtime() : nullptr;
        };
        h.pick = [this](bool hmi) { pickAnimationVariables(hmi); };
        h.status = [this](const std::string& m) { if (status_) status_->setMessage(m); };
        raw->setHosts(std::move(h));
        pane = std::move(frame);
        title = "API \xC2\xB7 Tables d'animation";
        icon = Icon::AnimationTable;
    } else if (key == "types" || key == "dfb" || key == "unites" || key == "variables") {
        // Lot API 5 : les types derives, les blocs DFB, les unites, les variables.
        std::unique_ptr<ApiFrame> frame;
        if (key == "types") {
            auto panel = std::make_unique<DerivedTypesPane>("analysis.api.types.volet");
            auto* raw = panel.get();
            frame = std::make_unique<ApiFrame>("analysis.api.types", std::move(panel));
            raw->attach(*frame);
            raw->setHosts(apiHosts());
            title = "API \xC2\xB7 Types d\xC3\xA9riv\xC3\xA9s";
            icon = Icon::DerivedType;
        } else if (key == "dfb") {
            auto panel = std::make_unique<DfbPane>("analysis.api.dfb.volet");
            auto* raw = panel.get();
            frame = std::make_unique<ApiFrame>("analysis.api.dfb", std::move(panel));
            raw->attach(*frame);
            raw->setHosts(apiHosts());
            title = "API \xC2\xB7 Blocs DFB";
            icon = Icon::FunctionBlock;
        } else if (key == "unites") {
            auto panel = std::make_unique<UnitsPane>("analysis.api.unites.volet");
            auto* raw = panel.get();
            frame = std::make_unique<ApiFrame>("analysis.api.unites", std::move(panel));
            raw->attach(*frame);
            raw->setHosts(apiHosts());
            title = "API \xC2\xB7 Unit\xC3\xA9s de programme";
            icon = Icon::Program;
        } else {
            auto panel = std::make_unique<VariablesPane>("analysis.api.variables.volet");
            auto* raw = panel.get();
            frame = std::make_unique<ApiFrame>("analysis.api.variables", std::move(panel));
            raw->attach(*frame);
            raw->setHosts(apiHosts());
            title = "API \xC2\xB7 Variables";
            icon = Icon::Variable;
        }
        pane = std::move(frame);
    } else if (key == "sous-routines") {
        // Lot API 5 : un onglet vide ou rempli, le meme (il suit le projet).
        // Le nom que montre la barre : celui du dossier du projet (l'export
        // garde souvent un nom generique, « Projet »).
        std::string projectName = app_.projectFolder().empty() ? std::string{}
                                : std::filesystem::path(app_.projectFolder()).filename().string();
        if (projectName.empty() && project) projectName = project->header.projectName;
        auto panel = std::make_unique<SubroutinesPane>("analysis.api.sr.volet", projectName);
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.sr", std::move(panel));
        raw->setHosts(apiHosts());
        raw->attach(*frame);
        pane = std::move(frame);
        title = "API \xC2\xB7 Sous-routines";
        icon = Icon::Section;
    } else if (key == "simulation") {
        // Lot API 7 : l'ancien ecran plein de la table de forcage (F9). La
        // simulation est a App (SimulationHost) : l'onglet la montre et la
        // pilote, la fermer ne l'arrete pas.
        auto panel = std::make_unique<SimulationPane>("analysis.api.simulation.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.simulation", std::move(panel));
        raw->attach(*frame);
        SimulationPane::Hosts h;
        // La vue du projet : c'est elle que la simulation prepare (App::project()).
        h.project = [this] { return app_.project(); };
        h.host = [this] { return &app_.simulation(); };
        h.attach = [this](std::string* why) { return attachSimulation(why); };
        h.goToLine = [this](const std::string& section, std::uint32_t line) { goToSectionLine(section, line); };
        // « Ajouter a une table d'animation » : la table choisie dans l'onglet des
        // tables ; le projet n'en a pas : une nouvelle.
        h.addToTable = [this](std::vector<std::string> names) {
            if (names.empty()) return;
            openApiPane("tables");
            const auto p = app_.project();
            auto* tables = tablesPaneOf(apiTab("tables"));
            const auto table = tables && p && tables->currentTable() < p->animationTables.size() ? tables->currentTable()
                                                                                                 : static_cast<std::size_t>(-1);
            addVariablesToTable(std::move(names), table);
        };
        h.request = [this](const std::string& k) { onApiRequest(k); };
        h.status = [this](const std::string& m) {
            if (!status_) return;
            status_->dismissTransient();
            status_->setMessage(m);
        };
        h.ask = [this](const std::string& askTitle, const std::string& text, const std::string& initial,
                       std::function<void(const std::string&)> done) { askApiText(askTitle, text, initial, std::move(done)); };
        // Exporter / importer les forcages : un chemin, avec le bouton ... (l'explorateur).
        h.askPath = [this](const std::string& askTitle, const std::string& text, const std::string& initial, ui::PathBrowse browse,
                           std::function<void(const std::string&)> done) {
            askApiText(askTitle, text, initial, std::move(done), std::move(browse));
        };
        h.folder = [this] { return app_.projectFolder(); };
        h.newerInLibrary = [this](const std::string& type) { return newerInLibrary(type); };
        raw->setHosts(std::move(h));
        pane = std::move(frame);
        title = "Simulation \xC2\xB7 Automate";   // Lot API 8 : Centre de simulation (etait "API . Simulation" ; les scripts gardent l'ancien titre)
        icon = Icon::Play;
    } else if (key == "comparer") {
        // 1.8.0 : le comparateur de sections (openSectionCompare y pose les sections).
        auto cmp = std::make_unique<SectionComparePane>("analysis.api.comparer");
        auto* raw = cmp.get();
        SectionComparePane::Hosts h;
        h.project = [this]() -> std::shared_ptr<const domain::Project> { return app_.project(); };
        h.openSection = [this](domain::Index s, int line) {
            const auto p = app_.project();
            if (!p || s >= p->sections.size()) return;
            if (line > 0) goToSectionLine(std::string(p->strings.text(p->sections[s].name)), static_cast<std::uint32_t>(line));
            else openDocument(s);
        };
        h.retitle = [this, raw](const std::string& t) {
            if (!centre_) return;
            if (const int at = centre_->indexOf(raw); at >= 0) centre_->setTabTitle(static_cast<std::size_t>(at), t);
        };
        h.status = [this](const std::string& m) {
            if (status_) status_->setTransientMessage(m, 6.0);
        };
        raw->setHosts(std::move(h));
        pane = std::move(cmp);
        title = "Comparer des sections";
        icon = Icon::Layers;
    } else if (key == "statistiques") {
        // Lot API 7 : l'ancien tableau des statistiques (Ctrl+5), un volet a hotes
        // comme les autres.
        auto panel = std::make_unique<StatisticsPane>("analysis.api.statistiques.volet");
        auto* raw = panel.get();
        auto frame = std::make_unique<ApiFrame>("analysis.api.statistiques", std::move(panel));
        raw->attach(*frame);
        raw->setHosts(apiHosts());
        pane = std::move(frame);
        title = "API \xC2\xB7 Statistiques";
        icon = Icon::Chart;
    } else {
        return;
    }
    auto* raw = pane.get();
    const auto at = centre_->addTab(TabControl::Tab{title, icon, true, false}, std::move(pane));
    apiTabs_[key] = raw;
    centre_->setCurrentIndex(at);
    refreshApiPanes();
}

void MainAnalysisScreen::refreshApiPanes() {
    auto project = app_.project();
    if (!project) return;
    if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("api")))
        if (auto* dash = dynamic_cast<ApiDashboard*>(&frame->content())) {
            project::SharedLibrary library(project::SharedLibrary::defaultRoot());
            const bool scanned = library.scan().has_value();
            std::size_t macros = 0;
            for (const auto& item : library.items())
                if (item.kind == project::LibraryItemKind::Macro) ++macros;
            dash->setSummary(project::api::summarize(*project, scanned ? &library : nullptr), macros);
        }
    // Lots API 3 et 4 : les volets a hotes relisent le projet (une commande,
    // un autre projet, une macro).
    if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("taches")))
        if (auto* pane = dynamic_cast<TasksPane*>(&frame->content())) pane->refresh();
    if (auto* cmp = sectionComparePane()) cmp->recompute();          // 1.8.0 : le comparateur suit le projet
    if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("ordre")))
        if (auto* pane = dynamic_cast<ExecutionOrderPane*>(&frame->content())) pane->refresh();
    if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("configuration")))
        if (auto* pane = dynamic_cast<ConfigurationPane*>(&frame->content())) pane->refresh();
    if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("tables")))
        if (auto* pane = dynamic_cast<AnimationTablesPane*>(&frame->content())) pane->refresh();
    // Lot API 5 : la bibliotheque se relit (une macro vient peut-etre de la
    // changer), puis les volets.
    apiLibraryRead_ = false;
    if (auto* pane = paneOf<DerivedTypesPane>(apiTab("types"))) pane->refresh();
    if (auto* pane = paneOf<DfbPane>(apiTab("dfb"))) pane->refresh();
    if (auto* pane = paneOf<UnitsPane>(apiTab("unites"))) pane->refresh();
    if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) pane->refresh();
    if (auto* pane = paneOf<SubroutinesPane>(apiTab("sous-routines"))) pane->refresh();
    // Lot API 7 : la simulation (ses racines, « ecrite par ») et les statistiques
    // relisent le projet.
    if (auto* pane = paneOf<SimulationPane>(apiTab("simulation"))) pane->refresh();
    if (auto* pane = paneOf<StatisticsPane>(apiTab("statistiques"))) pane->refresh();
}

// ---------------------------------------- ce que demandent les volets ----
void MainAnalysisScreen::onApiRequest(const std::string& key) {
    if (lisibleRequest(key)) return;    // 1.8.0 : comparer, exporter, icone
    // Lot API 6 : apres un collage depuis Excel, toutes les vues une fois.
    if (key == "rafraichir") {
        app_.events().publish(ProjectOpened{app_.project(), std::const_pointer_cast<importer::AnalysisReport>(app_.report())});
        return;
    }
    if (key == "configuration") openApiPane("configuration");
    else if (key == "ordre") {
        openApiPane("ordre");          // lot API 4 : un onglet (il depliait l'arbre)
    } else if (key == "tables") {
        openApiPane("tables");         // lot API 3
    } else if (key == "simulation" || key == "statistiques") {
        openApiPane(key);              // lot API 7 : des onglets (des ecrans pleins avant)
    } else if (key.rfind("section:", 0) == 0) {
        const auto index = static_cast<domain::Index>(std::strtoul(key.c_str() + 8, nullptr, 10));
        if (auto p = app_.project(); p && index < p->sections.size()) openDocument(index);
    } else if (key == "create.rack" || key == "create.module") {
        (void)app_.actions().trigger(key, app_.commands());
    } else if (key == "types") {
        openApiPane("types");          // lot API 5 : un onglet (il depliait l'arbre)
    } else if (key == "dfb" || key == "unites" || key == "sous-routines") {
        openApiPane(key);
    } else if (key == "variables") {
        openApiPane("variables");
    } else if (key.rfind("variables:", 0) == 0) {
        // Lot API 5 : Types derives > Voir les variables - l'onglet, cherche sur le type.
        openApiPane("variables");
        if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) pane->search(key.substr(10));
    } else if (key.rfind("variable:", 0) == 0) {
        const auto index = static_cast<domain::Index>(std::strtoul(key.c_str() + 9, nullptr, 10));
        if (auto p = app_.project(); p && index < p->variables.size()) openVariable(index);
    } else if (key.rfind("sous-routine:", 0) == 0) {
        const auto index = static_cast<domain::Index>(std::strtoul(key.c_str() + 13, nullptr, 10));
        if (auto p = app_.project(); p && index < p->sections.size()) openSubroutine(index);
    } else if (key.rfind("ordre:", 0) == 0) {
        openApiPane("ordre");
        if (auto* pane = paneOf<ExecutionOrderPane>(apiTab("ordre"))) (void)pane->selectEntry(key.substr(6));
    } else if (key == "create.ddt" || key == "create.dfb" || key == "create.unit" || key == "create.variable" || key == "create.section") {
        (void)app_.actions().trigger(key, app_.commands());
    } else if (key.rfind("csv:", 0) == 0) {
        const auto nl = key.find('\n');
        if (nl != std::string::npos) saveApiCsv(key.substr(4, nl - 4), key.substr(nl + 1));
    } else if (key == "variables-inutilisees") {
        // Lot API 5 : l'onglet Variables, sur « Pas utilisees » (et, comme avant,
        // la table des variables du bas).
        openApiPane("variables");
        if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) {
            pane->search("");
            (void)pane->filters().chooseChip("Pas utilis");
        }
        // La recherche d'abord : vider son texte repose SON filtre (textChanged),
        // qui effacerait celui-ci s'il etait pose avant.
        if (variableSearch_) variableSearch_->setText("");
        if (variables_ && variableModel_) {
            FilterChain chain;
            auto m = variableModel_;
            // Les globales seulement : ce que le tableau de bord compte.
            chain.addPredicate("unused", [m](RowIndex r) {
                return m->usage(r) == 0 && m->variable(r).scope == domain::VariableScope::Global;
            });
            variables_->setFilter(std::move(chain));
        }
        status_->setMessage("La table des variables ne montre que celles que le programme n'utilise pas. "
                            "Pas utilis\xC3\xA9" "e ne veut pas dire \xC3\xA0 supprimer : l'IHM ou le syst\xC3\xA8me peut la lire.");
    } else if (key == "didacticiel" || key.rfind("didacticiel:", 0) == 0) {
        openApiTutorial(key.size() > 12 ? key.substr(12) : std::string{});      // lot API 7
    } else if (key == "bibliotheque") {
        openMacros("MettreAJourBibliotheque", /*launch=*/true);
    } else if (key == "import-xhw") {
        importHardwareIntoProject();   // lot API 4 : dans le projet ouvert, en une commande
    } else if (key == "macros") {
        openMacros();
    } else if (key.rfind("macro:", 0) == 0) {
        openMacros(key.substr(6), /*launch=*/true);
    } else if (key == "aide") {
        (void)app_.actions().trigger("help.open", app_.commands());
    }
}

// ================================================= lots API 3 et 4 : les hotes ----
ApiPaneHosts MainAnalysisScreen::apiHosts() {
    ApiPaneHosts h;
    h.view = [this] { return app_.project(); };
    h.project = [this] { return app_.document(); };
    // Lot API 6 : pendant un collage (un groupe ouvert), les vues ne se refont pas
    // a chaque ligne - une fois a la fin ("rafraichir").
    h.apply = [this](core::CommandPtr c) { app_.apply(std::move(c), !app_.commands().grouping()); };
    h.request = [this](const std::string& k) { onApiRequest(k); };
    // Un message plus recent remplace aussi le message passager (« Annule : ... »)
    // qui, sinon, le cacherait pendant ses huit secondes.
    h.status = [this](const std::string& m) {
        if (!status_) return;
        status_->dismissTransient();
        status_->setMessage(m);
    };
    h.ask = [this](const std::string& title, const std::string& text, const std::string& initial,
                   std::function<void(const std::string&)> done) { askApiText(title, text, initial, std::move(done)); };
    // Lot API 5
    h.remove = [this](domain::EntityKind kind, domain::Index index) { confirmAndDelete(kind, index); };
    h.hmiReads = [this] { return hmiReadsOfPlc(); };
    h.addToTable = [this](std::vector<std::string> names, std::size_t table) { addVariablesToTable(std::move(names), table); };
    h.library = [this] { return apiLibrary(); };
    return h;
}

// ============================================================ lot API 5 ----
std::shared_ptr<const project::SharedLibrary> MainAnalysisScreen::apiLibrary() {
    if (!apiLibraryRead_) {
        apiLibraryRead_ = true;
        auto library = std::make_shared<project::SharedLibrary>(project::SharedLibrary::defaultRoot());
        apiLibrary_ = library->scan() ? std::move(library) : nullptr;
    }
    return apiLibrary_;
}

// Les variables de l'automate que l'IHM lit : par une variable IHM liee a la
// meme adresse (%MW1174), liee par son nom, ou citee telle quelle dans une vue
// ou un script. Le nombre : les endroits (un endroit = un emploi).
std::map<std::string, ApiHmiRead> MainAnalysisScreen::hmiReadsOfPlc() const {
    std::map<std::string, ApiHmiRead> out;
    const auto p = app_.project();
    const auto doc = app_.hmi();
    if (!p || !doc) return out;
    const auto rootOf = [](std::string_view path) {
        std::size_t end = 0;
        while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
        return lowerText(path.substr(0, end));
    };
    std::map<std::string, int> uses;          // racine (en minuscules) -> emplois
    std::map<std::string, int> direct;        // racines qui ne sont pas des variables IHM
    for (const auto& u : hmitree::usedVariables(doc->project)) {
        uses[rootOf(u.path)] += u.uses;
        if (!u.hmi) direct[rootOf(u.path)] += u.uses;
    }
    std::map<std::string, std::string> byAddress, byName;   // -> nom de la variable API
    for (const auto& v : p->variables) {
        if (v.scope != domain::VariableScope::Global) continue;
        const std::string name(p->strings.text(v.name));
        byName[lowerText(name)] = name;
        if (!v.address.raw.empty()) byAddress[lowerText(v.address.raw)] = name;
    }
    for (const auto& hv : doc->project.programs.variables) {
        if (!hv.bound() || hv.address.empty()) continue;
        const auto key = lowerText(hv.address);
        std::string plc;
        if (const auto a = byAddress.find(key); a != byAddress.end()) plc = a->second;
        else if (const auto n = byName.find(key); n != byName.end()) plc = n->second;
        if (plc.empty()) continue;
        auto& r = out[lowerText(plc)];
        if (r.via.empty()) r.via = hv.name;
        const auto u = uses.find(lowerText(hv.name));
        r.uses += u == uses.end() ? 0 : u->second;
    }
    // Citee telle quelle : pas les noms d'une ou deux lettres (i, j, k : les
    // compteurs des boucles des scripts de l'IHM, pas les variables de l'automate).
    for (const auto& [root, count] : direct)
        if (root.size() > 2 && byName.count(root)) out[root].uses += count;
    // Liee mais jamais montree : elle est quand meme lue (l'echange la rafraichit).
    for (auto& [name, r] : out) r.uses = std::max(r.uses, 1);
    return out;
}

// « Ajouter a une table d'animation » : une table existante, ou une nouvelle.
void MainAnalysisScreen::addVariablesToTable(std::vector<std::string> names, std::size_t table) {
    if (names.empty()) return;
    openApiPane("tables");
    auto* pane = tablesPaneOf(apiTab("tables"));
    if (!pane) return;
    std::vector<project::AnimationLine> lines;
    for (auto& n : names) lines.push_back({std::move(n), false});
    if (table == static_cast<std::size_t>(-1)) {
        pane->runAction(AnimationTablesPane::ATable);
        pane->addLines(std::move(lines));
    } else {
        pane->addLines(std::move(lines), table);
    }
}

// Exporter CSV : OU l'ecrire se demande - exports/<nom>.csv du projet par
// defaut (sans projet enregistre : le dossier temporaire), ailleurs avec le
// bouton ... (l'explorateur, "Enregistrer sous"). Entree dans le champ exporte.
void MainAnalysisScreen::saveApiCsv(const std::string& what, const std::string& content) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = app_.projectFolder().empty() ? fs::temp_directory_path(ec) : fs::path(app_.projectFolder()) / "exports";
    const auto utf8 = [](const fs::path& p) {
        const auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    };
    const std::string proposed = utf8(dir / (what + ".csv"));
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier CSV", proposed, "", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.apiCsv", "Exporter en CSV : " + what,
                                   "S\xC3\xA9parateur ;, avec le BOM : Excel l'ouvre tel quel, accents compris. Par d\xC3\xA9" "faut dans "
                                   "exports/ du projet ; le bouton \xE2\x80\xA6 choisit un autre endroit.",
                                   std::move(fields), "Exporter"),
                               0, ui::saveFile("Fichiers CSV|*.csv", proposed, "Exporter en CSV")),
        [this, dir, content, utf8](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            std::string text = v.empty() ? std::string{} : v[0];
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
            if (text.empty()) return;
            fs::path file(std::u8string(text.begin(), text.end()));
            if (file.is_relative()) file = dir / file;          // un nom seul : dans exports/
            if (!file.has_extension()) file += ".csv";
            std::error_code err;
            if (file.has_parent_path()) fs::create_directories(file.parent_path(), err);
            std::ofstream out(file, std::ios::binary);
            out << "\xEF\xBB\xBF" << content;       // le BOM : Excel lit les accents
            if (!status_) return;
            status_->dismissTransient();
            status_->setMessage(out ? "Export\xC3\xA9 : " + utf8(file) + " (s\xC3\xA9parateur ;, s'ouvre dans Excel)."
                                    : "Impossible d'\xC3\xA9" "crire " + utf8(file));
        });
}

void MainAnalysisScreen::tickApiPanes(double now) {
    // Lot API 7 : l'onglet Simulation, OU QU'IL SOIT - derriere un autre onglet,
    // montre dans un autre groupe, detache dans sa fenetre : retrouve par son
    // onglet (apiTab), pas par l'onglet courant, qui peut etre n'importe quel
    // autre alors que la simulation reste a l'ecran. Cache, il ne fait que noter
    // l'heure ; a l'ecran, il lit ses valeurs, son etat et sa courbe (quelques
    // fois par seconde). La pastille de l'arbre suit l'etat, elle, sans onglet.
    if (auto* sim = paneOf<SimulationPane>(apiTab("simulation"))) sim->tick(now);
    // ---- Lot API 8 : Simulation > Debogage (l'onglet, les marges des sections, le journal) ----
    tickSimDebug(now);
    // ---- fin Lot API 8 ----
    // ---- Lot API 8 : Centre de simulation (le collecteur, le bandeau, les pastilles, les courbes) ----
    tickSimCenter(now);
    // ---- fin Lot API 8 : Centre de simulation ----
    // ---- Lot API 8 : l'arbre du projet (les Recents se refont apres le clic) ----
    tickTree(now);
    // ---- fin Lot API 8 : l'arbre du projet ----
    // Lot API 3 : les valeurs en direct de la table d'animation, sa courbe -
    // quand elle se voit, pour la meme raison.
    auto* pane = tablesPaneOf(apiTab("tables"));
    if (!pane || !onScreen(pane)) return;
    // L'IHM avance a chaque dessin de son onglet ; la table la montre en direct
    // meme quand cet onglet est cache : on la fait avancer d'ici.
    if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); sim && sim->runtime().running()) sim->refreshAt(now);
    pane->tick();
}

void MainAnalysisScreen::askApiText(const std::string& title, const std::string& text, const std::string& initial,
                                    std::function<void(const std::string&)> done, ui::PathBrowse browse) {
    HmiAskDialog::Spec spec;
    spec.id = "dialog.apiAsk";
    spec.title = title;
    spec.text = text;
    HmiAskDialog::Option option;
    // Le libelle du champ suit la question (lot API 5 : les noms ; lot API 7 :
    // l'onglet Simulation - la valeur forcee, le fichier des forcages).
    option.label = title.rfind("Remplacer", 0) == 0 ? "R\xC3\xA9" "f\xC3\xA9rence"
                 : title.rfind("Renommer", 0) == 0  ? "Nouveau nom"
                 : title.rfind("Ajouter un champ", 0) == 0 ? "Champ"
                 : title.rfind("Borner", 0) == 0    ? "D\xC3\xA9" "but-fin"
                 : title.rfind("Forcer", 0) == 0    ? "Valeur"
                 : title.rfind("Exporter", 0) == 0 || title.rfind("Importer", 0) == 0 ? "Fichier"
                 : title.rfind("Cr\xC3\xA9" "er", 0) == 0 || title.rfind("Ajouter", 0) == 0 ? "Nom"
                 : "Rang";
    option.field = initial;
    option.hasField = true;
    // Un chemin (exporter, importer les forcages) : le bouton ... de l'explorateur,
    // et la place de lire un chemin complet.
    const bool path = browse.active();
    option.browse = std::move(browse);
    spec.options.push_back(std::move(option));
    spec.confirm = "OK";
    spec.width = path ? 720.f : 560.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [done = std::move(done)](const menu::DialogResult& r) {
        if (r.accepted() && done) done(HmiAskDialog::parse(r.payload).field);
    });
}

// + Variable API / + Variable IHM : le choix, puis UNE commande.
void MainAnalysisScreen::pickAnimationVariables(bool hmi) {
    auto* pane = tablesPaneOf(apiTab("tables"));
    const auto p = app_.project();
    if (!pane || !p || pane->currentTable() >= p->animationTables.size()) return;
    const auto& table = p->animationTables[pane->currentTable()];
    std::set<std::string> present;
    for (const auto& e : table.entries)
        if (e.hmi == hmi) present.insert(std::string(p->strings.text(e.name)));
    VariablePicker::Spec spec;
    spec.hmi = hmi;
    const auto name = pane->currentTableName();
    if (hmi) {
        const auto doc = app_.hmi();
        if (!doc) return;
        spec.title = "Ajouter des variables IHM \xC3\xA0 " + name;
        for (const auto& v : doc->project.programs.variables) {
            VariablePicker::Candidate c;
            c.name = v.name;
            c.type = v.type;
            c.detail = !v.description.empty() ? v.description : v.bound() ? v.equipment + " " + v.address : std::string("locale \xC3\xA0 l'IHM");
            c.linked = v.bound();
            c.present = present.count(v.name) > 0;
            spec.candidates.push_back(std::move(c));
        }
        spec.note = "Tu peux aussi glisser une variable depuis IHM \xE2\x80\xBA Variables IHM.";
    } else {
        spec.title = "Ajouter des variables de l'automate \xC3\xA0 " + name;
        for (const auto& v : p->variables) {
            if (v.scope != domain::VariableScope::Global) continue;
            VariablePicker::Candidate c;
            c.name = std::string(p->strings.text(v.name));
            c.type = std::string(p->strings.text(v.type.name));
            const std::string comment(p->strings.text(v.comment));
            c.detail = !v.address.raw.empty() ? v.address.raw + (comment.empty() ? std::string{} : "  " + comment) : comment;
            c.linked = v.address.valid();
            c.present = present.count(c.name) > 0;
            spec.candidates.push_back(std::move(c));
        }
        std::sort(spec.candidates.begin(), spec.candidates.end(),
                  [](const VariablePicker::Candidate& a, const VariablePicker::Candidate& b) { return a.name < b.name; });
        spec.note = "Tu peux aussi glisser une variable depuis API \xE2\x80\xBA Variables.";
    }
    app_.menus().ShowDialog(std::make_unique<VariablePicker>(std::move(spec)), [this, hmi](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        auto* target = tablesPaneOf(apiTab("tables"));
        const auto names = VariablePicker::parse(r.payload);
        if (!target || names.empty()) return;
        std::vector<project::AnimationLine> lines;
        for (const auto& n : names) lines.push_back({n, hmi});
        target->addLines(std::move(lines));
    });
}

// Configuration > Importer le .XHW... : les racks d'un .XHW lu a part, poses dans
// le projet ouvert en une commande (le reimporter en entier perdrait le dossier).
void MainAnalysisScreen::importHardwareIntoProject() {
    if (!app_.document()) {
        (void)app_.actions().trigger("file.importHardware", app_.commands());
        return;
    }
    app_.menus().ShowDialog(std::make_unique<OpenProjectDialog>(app_, /*hardwareOnly*/ true), [this](const menu::DialogResult& r) {
        if (!r.accepted() || !app_.document()) return;
        // 1.8.0 : lu a cote, suivi par sa fenetre ; pose ensuite (applyHardwareImport).
        startImportJob(false, {r.payload});
    });
}

// Les variables que portent des noeuds de l'arbre : (nom, de l'IHM ?).
std::vector<std::pair<std::string, bool>> MainAnalysisScreen::treeVariables(const std::vector<ui::NodeId>& nodes) const {
    std::vector<std::pair<std::string, bool>> out;
    const auto p = app_.project();
    const auto doc = app_.hmi();
    for (const auto n : nodes) {
        const auto kind = ProjectTreeModel::kindOf(n);
        if (kind == NK::ListVariable && p && treeModel_) {
            const auto v = treeModel_->variableOf(n);
            if (v < p->variables.size()) out.emplace_back(std::string(p->strings.text(p->variables[v].name)), false);
        } else if (kind == NK::HmiVariable && doc) {
            const auto id = ProjectTreeModel::indexOf(n);
            for (const auto& v : doc->project.programs.variables)
                if ((v.id & ((1u << 28) - 1u)) == id) out.emplace_back(v.name, true);
        }
    }
    return out;
}

// Une variable (API > Variables, IHM > Variables IHM) glissee sur une table
// d'animation DANS l'arbre : elle s'y ajoute.
void MainAnalysisScreen::wireAnimationTreeDrag() {
    if (!explorer_ || !treeModel_) return;
    using Where = ui::TreeView::DropWhere;
    auto beforeDrag = explorer_->dragPredicate();
    auto beforeDrop = explorer_->dropPredicate();
    const auto isVar = [this](ui::NodeId n) {
        const auto k = ProjectTreeModel::kindOf(n);
        return (k == NK::ListVariable && treeModel_ && treeModel_->variableOf(n) != domain::kNoIndex) || k == NK::HmiVariable;
    };
    explorer_->setDragPredicate([beforeDrag, isVar](ui::NodeId n) { return isVar(n) || (beforeDrag && beforeDrag(n)); });
    explorer_->setDropPredicate([this, beforeDrop, isVar](ui::NodeId dragged, ui::NodeId target, Where where) {
        if (isVar(dragged) && ProjectTreeModel::kindOf(target) == NK::AnimationTable)
            return where == Where::Into && app_.document() != nullptr;
        return beforeDrop && beforeDrop(dragged, target, where);
    });
    execOrderLinks_ += explorer_->dropped->connect([this, isVar](ui::NodeId dragged, ui::NodeId target, Where) {
        if (!isVar(dragged) || ProjectTreeModel::kindOf(target) != NK::AnimationTable) return;
        auto nodes = explorer_->draggedNodes();
        if (nodes.empty()) nodes.push_back(dragged);
        const auto vars = treeVariables(nodes);
        const auto table = static_cast<std::size_t>(ProjectTreeModel::indexOf(target));
        openApiPane("tables");
        auto* pane = tablesPaneOf(apiTab("tables"));
        if (!pane || vars.empty()) return;
        std::vector<project::AnimationLine> lines;
        for (const auto& [name, hmi] : vars) lines.push_back({name, hmi});
        pane->addLines(std::move(lines), table);
    });
}

// ... ou par le clic droit : « Ajouter a la table PURGE ».
void MainAnalysisScreen::addTreeVariablesToTable(const std::vector<ui::NodeId>& nodes, std::size_t table) {
    const auto vars = treeVariables(nodes);
    if (vars.empty()) return;
    openApiPane("tables");
    auto* pane = tablesPaneOf(apiTab("tables"));
    if (!pane) return;
    std::vector<project::AnimationLine> lines;
    for (const auto& [name, hmi] : vars) lines.push_back({name, hmi});
    pane->addLines(std::move(lines), table);
}

// ... ou HORS de l'arbre, sur les lignes de l'onglet des tables. L'arbre ne
// recoit plus la souris une fois dehors : l'ecran suit le glisser, allume le
// cadre de depot, et depose au lacher.
bool MainAnalysisScreen::dropTreeDragOnTables(const ui::InputEvent& ev) {
    auto* pane = tablesPaneOf(apiTab("tables"));
    if (!explorer_ || !explorer_->dragInProgress()) {
        if (pane) pane->setDropHint(false);
        return false;
    }
    const gfx::Point* pos = nullptr;
    bool up = false;
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) pos = &m->pos;
    else if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        pos = &u->pos;
        up = true;
    }
    if (!pos) return false;
    if (explorer_->bounds().contains(*pos)) {
        if (pane) pane->setDropHint(false);
        return false;
    }
    auto nodes = explorer_->draggedNodes();
    if (nodes.empty()) nodes.push_back(explorer_->dragSource());
    const auto vars = treeVariables(nodes);
    const bool over = pane && !vars.empty() && pane->dropZone(*pos);
    if (!up) {
        if (pane) {
            std::string label = vars.empty() ? std::string{} : vars.front().first;
            if (vars.size() > 1) label += " +" + std::to_string(vars.size() - 1);
            pane->setDropHint(over, std::move(label), *pos);
        }
        return false;
    }
    explorer_->cancelDrag();
    if (pane) pane->setDropHint(false);
    if (!over) return false;
    std::vector<project::AnimationLine> lines;
    for (const auto& [name, hmi] : vars) lines.push_back({name, hmi});
    pane->addLines(std::move(lines));
    return true;
}

// Un clic dans l'arbre : le bon onglet de l'API, le bon sous-onglet, la bonne ligne.
bool MainAnalysisScreen::routeApiNode(ui::NodeId node) {
    const auto kind = ProjectTreeModel::kindOf(node);
    const auto index = ProjectTreeModel::indexOf(node);
    const auto p = app_.project();
    if (!p) return false;
    const auto configuration = [this](int tab) -> ConfigurationPane* {
        openApiPane("configuration");
        auto* frame = dynamic_cast<ApiFrame*>(apiTab("configuration"));
        auto* pane = frame ? dynamic_cast<ConfigurationPane*>(&frame->content()) : nullptr;
        if (pane && tab >= 0) pane->showTab(tab);
        return pane;
    };
    switch (kind) {
        case NK::TablesFolder:
            openApiPane("tables");
            return true;
        case NK::AnimationTable:
            openApiPane("tables");
            if (auto* pane = tablesPaneOf(apiTab("tables"))) pane->selectTable(static_cast<std::size_t>(index));
            return true;
        case NK::ExecOrderFolder:
        case NK::ExecOrderTask:
            openApiPane("ordre");
            return true;
        case NK::ExecStep: {
            openApiPane("ordre");
            auto* frame = dynamic_cast<ApiFrame*>(apiTab("ordre"));
            auto* pane = frame ? dynamic_cast<ExecutionOrderPane*>(&frame->content()) : nullptr;
            const auto section = treeModel_ ? treeModel_->sectionOf(node) : domain::kNoIndex;
            if (pane && section < p->sections.size() && !pane->selectEntry(p->strings.text(p->sections[section].name)))
                for (const auto& pou : p->pous)
                    if (pou.kind == domain::PouKind::ProgramUnit && std::find(pou.sections.begin(), pou.sections.end(), section) != pou.sections.end())
                        (void)pane->selectEntry(p->strings.text(pou.name));
            return true;
        }
        case NK::Cpu:               configuration(ConfigurationPane::TProcessor); return true;
        case NK::ConfigurationFolder:
        case NK::RackFolder:
        case NK::Rack:              configuration(ConfigurationPane::TRacks); return true;
        case NK::ApiChannels:       configuration(ConfigurationPane::TChannels); return true;
        case NK::ApiNetwork:        configuration(ConfigurationPane::TNetwork); return true;
        case NK::ApiMemory:         configuration(ConfigurationPane::TMemory); return true;
        case NK::HwModule: {
            const auto sub = static_cast<domain::Index>(node & ((1ull << 28) - 1));
            auto* pane = configuration(ConfigurationPane::TRacks);
            if (pane && index < p->hardware.racks.size() && sub < p->hardware.racks[index].modules.size()) {
                const auto& rack = p->hardware.racks[index];
                (void)pane->selectModule(rack.number, rack.modules[sub].slot);
            }
            return true;
        }
        // Lot API 5 : les types derives, les blocs DFB, les unites, les variables.
        case NK::TypesFolder:
            openApiPane("types");
            return true;
        case NK::DerivedType:
        case NK::DerivedField: {
            openApiPane("types");
            auto* pane = paneOf<DerivedTypesPane>(apiTab("types"));
            std::string type;
            if (kind == NK::DerivedType && index < p->derivedTypes.size()) type = std::string(p->strings.text(p->derivedTypes[index].name));
            else if (kind == NK::DerivedField && index < p->variables.size() && p->variables[index].owner < p->derivedTypes.size())
                type = std::string(p->strings.text(p->derivedTypes[p->variables[index].owner].name));
            if (pane && !type.empty()) (void)pane->selectType(type);
            return false;          // et, comme avant, la table des variables du bas filtree
        }
        case NK::DfbFolder:
            openApiPane("dfb");
            return true;
        case NK::DfbType: {
            openApiPane("dfb");
            if (auto* pane = paneOf<DfbPane>(apiTab("dfb")); pane && index < p->pous.size()) (void)pane->selectBlock(p->strings.text(p->pous[index].name));
            return false;          // idem : la table des variables du bas
        }
        case NK::UnitsFolder:
            openApiPane("unites");
            return true;
        case NK::ProgramUnit: {
            openApiPane("unites");
            if (auto* pane = paneOf<UnitsPane>(apiTab("unites")); pane && index < p->pous.size()) (void)pane->selectUnit(p->strings.text(p->pous[index].name));
            return true;
        }
        case NK::VariablesFolder:
            openApiPane("variables");
            return true;
        // Lot API 7 : l'ancien ecran F9 et l'ancien Ctrl+5, des onglets de l'API.
        case NK::ApiSimulation:
            openApiPane("simulation");
            return true;
        // Lot API 7 : un membre deplie dans l'arbre (armoires[0].sorties.V3) -
        // l'onglet de ce qui le declare, deplie jusqu'a lui.
        case NK::MemberNode: {
            if (!treeModel_) return false;
            const auto root = treeModel_->memberRootOf(node);
            if (root >= p->variables.size()) return false;
            const auto& var = p->variables[root];
            if (var.scope == domain::VariableScope::Global) {
                openApiPane("variables");
                if (auto* pane = paneOf<VariablesPane>(apiTab("variables"))) (void)pane->revealMember(treeModel_->memberPathOf(node));
                return true;
            }
            if (var.scope == domain::VariableScope::DerivedMember && var.owner < p->derivedTypes.size()) {
                openApiPane("types");
                if (auto* pane = paneOf<DerivedTypesPane>(apiTab("types"))) {
                    const std::string owner(p->strings.text(p->derivedTypes[var.owner].name));
                    (void)pane->selectType(owner);
                    (void)pane->setExpanded("m:" + owner + "." + treeModel_->memberKeyOf(node), true);
                }
                return true;
            }
            if (var.owner < p->pous.size()) {
                const auto& pou = p->pous[var.owner];
                const std::string owner(p->strings.text(pou.name));
                if (pou.kind == domain::PouKind::FunctionBlockType) {
                    openApiPane("dfb");
                    if (auto* pane = paneOf<DfbPane>(apiTab("dfb"))) {
                        (void)pane->selectBlock(owner);
                        (void)pane->setExpanded("m:" + owner + "." + treeModel_->memberKeyOf(node), true);
                    }
                } else if (pou.kind == domain::PouKind::ProgramUnit) {
                    openApiPane("unites");
                    if (auto* pane = paneOf<UnitsPane>(apiTab("unites"))) {
                        (void)pane->selectVariable(owner, p->strings.text(var.name));
                        (void)pane->setExpanded("m:" + owner + "." + treeModel_->memberKeyOf(node), true);
                    }
                }
                return true;
            }
            return false;
        }
        case NK::ApiStatistics:
            openApiPane("statistiques");
            return true;
        case NK::Task: {
            openApiPane("taches");
            auto* frame = dynamic_cast<ApiFrame*>(apiTab("taches"));
            if (auto* pane = frame ? dynamic_cast<TasksPane*>(&frame->content()) : nullptr; pane && index < p->tasks.size())
                (void)pane->selectTask(p->strings.text(p->tasks[index].name));
            // Lot API 7 : plus de table des sections en bas de l'ecran a filtrer
            // sur la tache - l'onglet Taches montre ses sections.
            return true;
        }
        default:
            return false;
    }
}

// ================================= lot API 7 : Simulation, Statistiques ----
// F9, Ctrl+1, Ctrl+5 (OpenApiTab). Sans projet, l'onglet n'aurait rien a
// montrer : le dire, comme l'icone du projet.
void MainAnalysisScreen::openApiTabFromAction(const std::string& key) {
    // ---- Lot API 8 : Centre de simulation (F9 : "sim:ensemble") ----
    if (key.rfind("sim:", 0) == 0) {
        (void)openSimCenter(key.substr(4));
        return;
    }
    // ---- fin Lot API 8 : Centre de simulation ----
    if (!app_.project()) {
        const std::string what = key == "simulation" ? "Simulation" : key == "statistiques" ? "Statistiques" : "Variables";
        if (status_) status_->setTransientMessage(what + " : ouvre d'abord un projet.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    // Le centre doit etre a l'ecran, sinon l'onglet s'ouvre dans un panneau cache
    // (comme Affichage > Panneaux a afficher).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    openApiPane(key);
}

// Comme Simuler dans la barre (runSimulationTransport) : la simulation du projet
// ouvert, preparee une fois ; deja prete pour ce projet, elle est gardee telle
// quelle (son cycle, ses forcages). Rien n'est lance ici : l'onglet decide
// (Lancer, Un cycle). `why` : la raison seule - l'onglet en fait sa phrase
// (« La simulation ne demarre pas : ... ») et traduit celle du moteur.
bool MainAnalysisScreen::attachSimulation(std::string* why) {
    const auto project = app_.project();
    if (!project) {
        if (why) *why = "aucun projet ouvert";
        return false;
    }
    if (auto ready = app_.simulation().attach(project); !ready) {
        if (why) *why = ready.error().message();
        return false;
    }
    refreshSimulationIndicator();
    return true;
}

// Les noms que donne le simulateur : une section de tache ("Gestion_reports"),
// ou le corps d'un DFB ("DFB_Vanne.Corps" : le type, puis sa section).
void MainAnalysisScreen::goToSectionLine(const std::string& section, std::uint32_t line) {
    const auto p = app_.project();
    if (!p || section.empty() || !centre_) return;
    const auto want = lowerText(section);
    domain::Index found = domain::kNoIndex;
    // Le nom entier d'abord : une section du programme.
    for (domain::Index i = 0; i < p->sections.size() && found == domain::kNoIndex; ++i)
        if (lowerText(p->strings.text(p->sections[i].name)) == want) found = i;
    // "Bloc.Section" : la section de ce nom dont le proprietaire est ce bloc ;
    // a defaut, la seule section de ce nom.
    if (const auto dot = want.find('.'); found == domain::kNoIndex && dot != std::string::npos) {
        const auto owner = want.substr(0, dot), name = want.substr(dot + 1);
        domain::Index any = domain::kNoIndex;
        std::size_t count = 0;
        for (domain::Index i = 0; i < p->sections.size() && found == domain::kNoIndex; ++i) {
            const auto& s = p->sections[i];
            if (lowerText(p->strings.text(s.name)) != name) continue;
            if (s.owner < p->pous.size() && lowerText(p->strings.text(p->pous[s.owner].name)) == owner) found = i;
            any = i;
            ++count;
        }
        if (found == domain::kNoIndex && count == 1) found = any;
    }
    if (found == domain::kNoIndex) {
        if (status_) status_->setTransientMessage("Section introuvable dans le projet : " + section, 6.0, StatusBar::Severity::Warning);
        return;
    }
    // Le centre a l'ecran, la section ouverte (ou montree), le curseur sur la
    // ligne (les lignes du simulateur commencent a 1, celles de l'editeur a 0).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    openDocument(found);
    // L'editeur par son identifiant (unique : une section, un onglet), dans tout
    // l'ecran : l'onglet de la section peut etre montre dans un autre groupe que
    // celui de l'onglet courant.
    const std::string editorId = "analysis.doc." + std::to_string(found) + ".code";
    auto* editor = widgetRoot() ? dynamic_cast<MultiLineText*>(widgetRoot()->findById(editorId)) : nullptr;
    if (!editor)
        if (auto* page = centre_->page(centre_->currentIndex())) editor = dynamic_cast<MultiLineText*>(page->findById(editorId));
    if (editor && line > 0) editor->goToLine(static_cast<std::size_t>(line - 1));
}

// La halte vient d'un bloc DFB (ou d'un DDT) : la bibliotheque en a-t-elle une
// version plus recente ? Lue une fois par rafraichissement des volets (apiLibrary).
std::string MainAnalysisScreen::newerInLibrary(const std::string& type) {
    const auto p = app_.project();
    if (!p || type.empty()) return {};
    const auto library = apiLibrary();
    if (!library) return {};
    const auto want = lowerText(type);
    std::string name, version;
    auto kind = project::LibraryItemKind::FunctionBlock;
    for (const auto& pou : p->pous)
        if (pou.kind == domain::PouKind::FunctionBlockType && lowerText(p->strings.text(pou.name)) == want) {
            name = std::string(p->strings.text(pou.name));
            version = pou.version;
            break;
        }
    if (name.empty())
        for (const auto& d : p->derivedTypes)
            if (lowerText(p->strings.text(d.name)) == want) {
                name = std::string(p->strings.text(d.name));
                version = d.version;
                kind = project::LibraryItemKind::DerivedType;
                break;
            }
    if (name.empty()) return {};
    const auto state = apikit::libraryState(library.get(), name, version, kind);
    // Differente ne veut pas dire plus recente : une copie plus ancienne en
    // bibliotheque ne se propose pas.
    if (state.kind != apikit::LibState::Different || !versionNewer(state.version, version)) return {};
    return state.version;
}

// La pastille de API > Simulation. A chaque image (Update) et a chaque signal de
// la simulation : une chaine comparee, et l'arbre redessine seulement si elle a
// change - ou si l'arbre a un nouveau modele (chaque commande qui touche la
// structure en refait un, sans pastille).
void MainAnalysisScreen::refreshSimulationBadge() {
    if (!treeModel_ || !explorer_) return;
    using State = SimulationHost::State;
    const auto& sim = app_.simulation();
    std::string text;
    Tone tone = Tone::None;
    if (sim.attached()) {
        switch (sim.state()) {
            case State::Running: text = "en marche"; tone = Tone::Ok; break;
            // ---- Lot API 8 : corrections des captures ---- comme le dossier Simulation
            // et le bandeau : pause simple en bleu, point d'arret en orange.
            case State::Paused:
                text = sim.lastBreakHit() ? "point d'arr\xC3\xAAt" : "en pause";
                tone = sim.lastBreakHit() ? Tone::Warning : Tone::Info;
                break;
            case State::Halted:  text = "halte"; tone = Tone::Error; break;
            default:             break;
        }
    }
    if (text == simBadgeShown_ && simBadgeModel_.lock() == treeModel_) return;
    treeModel_->setSimulationBadge(text, tone);
    simBadgeShown_ = std::move(text);
    simBadgeModel_ = treeModel_;
    explorer_->invalidate();
}

} // namespace app
