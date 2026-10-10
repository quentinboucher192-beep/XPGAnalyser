// =============================================================================
//  app/screens/TopBarWorkspace.cpp - lot API 8 : le bandeau haut, cote ecran
// -----------------------------------------------------------------------------
//  Ce que le bandeau haut (TopBar, TopBarLot8.cpp) montre de plus que la barre
//  du lot 7, et ses actions nouvelles :
//    - les projets recents (le menu de la puce projet) ;
//    - les 10 dernieres actions (le chevron d'Annuler) : "edit.undoTo:n"
//      annule les n dernieres ;
//    - la mini-courbe du temps de cycle (4 echantillons par seconde) ;
//    - la cloche : ce que SimStatus juge digne d'attention dans la simulation,
//      plus les avis poses par les autres (bgtasks::post : Compiler, la
//      bibliotheque, un export) ; "sim.go:<cle>" regle une ligne de la
//      simulation comme le bouton de la Vue d'ensemble ;
//    - les taches de fond (bgtasks::begin / progress / end) ;
//    - Deposer un fichier... : l'explorateur, puis la question du depot.
// =============================================================================
#include "Screens.hpp"

#include "../ApiListKit.hpp"
#include "../App.hpp"
#include "../BackgroundTasks.hpp"
#include "../ApiPanes.hpp"            // 1.11 (R111) : l'onglet Ordre d'execution (ApiFrame)
#include "../ConditionsNotice.hpp"   // 1.11 (R111) : les conditions d'activation manquantes
#include "../FileExplorerHost.hpp"
#include "../GoToSearch.hpp"
#include "../SimJournal.hpp"
#include "../SimStatus.hpp"
#include "../SimulationHost.hpp"
#include "../TaskPanes.hpp"            // 1.11 (R111) : ExecutionOrderPane
#include "../TopBar.hpp"
#include "../hmi/HmiSimulation.hpp"   // 1.10 : la pastille IHM
#include "../../project/SharedLibrary.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace app {

namespace {

std::string ago(std::chrono::steady_clock::time_point t) {
    if (t == std::chrono::steady_clock::time_point{}) return {};
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    if (s < 60.0) return "il y a " + std::to_string(std::max(1, static_cast<int>(s))) + " s";
    if (s < 3600.0) return "il y a " + std::to_string(static_cast<int>(s / 60.0)) + " min";
    return "il y a " + std::to_string(static_cast<int>(s / 3600.0)) + " h";
}

// 1.11.2 (UNI, decision 204) : un projet recent qui est celui deja ouvert (le meme
// dossier, quelle que soit son ecriture : / ou \, majuscules sous Windows). Comme
// samePath de l'accueil : la meme chaine d'abord (la liste retient projectFolder()
// tel quel ; un dossier accentue que equivalent() ne trouverait pas sous Windows).
bool recentSameFolder(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}

std::string recentLeaf(const std::string& path) {
    const std::filesystem::path p(path);
    std::string leaf = p.filename().string();
    if (leaf.empty()) leaf = p.parent_path().filename().string();   // "C:\Projets\Tunnel\"
    return leaf.empty() ? path : leaf;
}

std::string toneWord(simstatus::Tone t) {
    switch (t) {
        case simstatus::Tone::Error:   return "error";
        case simstatus::Tone::Warning: return "warning";
        case simstatus::Tone::Ok:      return "ok";
        default:                       return "info";
    }
}

} // namespace

void MainAnalysisScreen::refreshTopBarLot8() {
    if (!topBar_) return;
    // L'horloge de l'appli, pas celle du mur : une session rejouee (1/30 s par image) voit
    // la cloche et les taches changer au meme moment a chaque rejeu.
    const double now = frameClock_;
    const auto&  sim = app_.simulation();

    // La mini-courbe : le temps du dernier cycle, 4 fois par seconde, tant que
    // la simulation tourne.
    if (sim.attached() && sim.state() == SimulationHost::State::Running && now - topBarCycleAt_ >= 0.25) {
        topBarCycleAt_ = now;
        topBar_->setCycleTime(static_cast<float>(sim.lastScanMicros()) / 1000.f, static_cast<float>(topBarPeriodMs_));
    }

    // Les 10 dernieres actions et les projets recents : quand la pile bouge,
    // et chaque seconde ("il y a 3 min" avance).
    auto&      stack = app_.commands();
    // Une fois par seconde, ou tout de suite quand une tache ou un avis change.
    const auto registry = bgtasks::revision();
    const bool tick = now - topBarNoticesAt_ >= 1.0 || registry != topBarTasksRevision_;
    topBarTasksRevision_ = registry;
    if (stack.revision() != topBarRevision_ || tick) {
        topBarRevision_ = stack.revision();
        std::vector<TopBar::UndoItem> undo;
        const auto& done = stack.done();
        for (auto it = done.rbegin(); it != done.rend() && undo.size() < 10; ++it)
            undo.push_back({it->info.label.empty() ? std::string("Action sans nom") : it->info.label, ago(it->last)});
        topBar_->setUndoList(std::move(undo));
        topBar_->setRecentProjects(app_.recentPaths());
    }
    if (!tick) return;
    topBarNoticesAt_ = now;

    // 1.11 (R111, decisions 6 et 15) : les conditions d'activation manquantes
    // d'un projet importe avant la 1.8.0 - l'avis de la cloche, pose des
    // l'ouverture, tenu a jour chaque seconde (un reimport du .XPG qui les rend
    // le retire, Ctrl+Z le remet ; « Ne plus le dire pour ce projet » le tait).
    {
        const auto p = app_.project();
        if (conditions::syncNotice(conditions::message(p.get(), app_.manifest(), app_.settings(), app_.projectFolder())))
            if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("ordre")))   // sa ligne d'aide suit l'avis
                if (auto* pane = dynamic_cast<ExecutionOrderPane*>(&frame->content())) pane->refresh();
    }

    // Les mises a jour de bibliotheque (les blocs DFB et les DDT dont la
    // bibliotheque a une autre version) : toutes les 30 secondes.
    if (now - topBarLibraryAt_ >= 30.0) {
        topBarLibraryAt_ = now;
        std::size_t n = 0;
        std::string names;
        if (const auto p = app_.project()) {
            if (const auto library = apiLibrary()) {
                const auto consider = [&](std::string_view name, std::string_view version, project::LibraryItemKind kind) {
                    if (apikit::libraryState(library.get(), name, version, kind).kind != apikit::LibState::Different) return;
                    if (++n <= 4) names += (names.empty() ? "" : ", ") + std::string(name);
                };
                for (const auto& pou : p->pous)
                    if (pou.kind == domain::PouKind::FunctionBlockType)
                        consider(p->strings.text(pou.name), pou.version, project::LibraryItemKind::FunctionBlock);
                for (const auto& d : p->derivedTypes) consider(p->strings.text(d.name), d.version, project::LibraryItemKind::DerivedType);
            }
        }
        if (n == 0) bgtasks::withdraw("bibliotheque:mises-a-jour");
        else
            bgtasks::post({"bibliotheque:mises-a-jour", "Projet",
                           std::to_string(n) + (n > 1 ? " mises \xC3\xA0 jour de biblioth\xC3\xA8que" : " mise \xC3\xA0 jour de biblioth\xC3\xA8que"),
                           names + (n > 4 ? "\xE2\x80\xA6" : ""), "Voir", "api.dashboard", "warning"});
    }

    // La cloche : les avis des autres (Compiler, bibliotheque, export), puis
    // ce que la simulation juge digne d'attention (SimStatus, comme la Vue
    // d'ensemble).
    std::vector<TopBar::Notice> notices;
    for (auto& a : bgtasks::notices()) {
        TopBar::Notice n;
        n.group = a.group.empty() ? std::string("Projet") : a.group;
        n.title = std::move(a.title);
        n.detail = std::move(a.detail);
        n.button = std::move(a.button);
        n.action = std::move(a.action);
        n.tone = std::move(a.tone);
        n.key = std::move(a.key);
        notices.push_back(std::move(n));
    }
    if (sim.attached() && app_.project()) {
        ensureSimCenter();
        const auto snap = simSnapshot(app_.simJournal().now());
        if (snap.plc.periodMs > 0) topBarPeriodMs_ = snap.plc.periodMs;
        const auto report = simstatus::compute(snap);
        for (const auto& a : report.attention) {
            TopBar::Notice n;
            n.group = "Simulation";
            n.title = a.text;
            n.detail = a.detail;
            n.button = a.action.label;
            n.action = a.action.key.empty() ? std::string("sim.center") : "sim.go:" + a.action.key;
            n.tone = toneWord(a.tone);
            n.key = "sim:" + a.id + ":" + a.text;
            notices.push_back(std::move(n));
        }
    }
    topBar_->setNotices(std::move(notices));

    // Les taches de fond : celles qui se sont inscrites (bgtasks), et les
    // macros d'un depot qui passent l'une apres l'autre.
    std::vector<TopBar::Task> tasks;
    for (auto& t : bgtasks::list()) tasks.push_back({std::move(t.label), t.progress, std::move(t.cancelAction)});
    if (!dropMacroRunning_.empty())
        tasks.push_back({"Macros du d\xC3\xA9p\xC3\xB4t : " + dropMacroRunning_
                             + (dropMacros_.empty() ? std::string{} : " (+" + std::to_string(dropMacros_.size()) + ")"),
                         -1.f, {}});
    topBar_->setTasks(std::move(tasks));
}

bool MainAnalysisScreen::topBarLot8Action(std::string_view id) {
    // ---- 1.10 : la pastille IHM de la barre du haut ----
    //  Demarrer ou arreter l'IHM seule : l'API (le programme) ne bouge pas. Pas
    //  encore d'onglet Simulation . IHM : il s'ouvre, l'IHM y demarre (on le dit).
    if (id == "hmi.toggle" || id == "hmi.start" || id == "hmi.stop") {
        if (!app_.hmi()) {
            if (status_) status_->setTransientMessage("Le projet n'a pas d'IHM.", 5.0);
            return true;
        }
        auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        if (const auto tab = hmiTabs_.find("simulation"); !sim && tab != hmiTabs_.end())
            sim = dynamic_cast<HmiSimulationPane*>(tab->second);   // detache dans sa fenetre
        if (!sim) {
            if (id == "hmi.stop") return true;
            openHmiPane("simulation");
            sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
            if (sim) sim->command("hmi.start");
            refreshSimulationIndicator();
            return true;
        }
        const bool start = id == "hmi.start" || (id == "hmi.toggle" && !sim->hmiRunning());
        // F8 sur une IHM qui tourne deja (Maj+F8 sur une IHM arretee) : rien a faire, on le dit.
        if (start == sim->hmiRunning()) {
            if (status_)
                status_->setTransientMessage(start ? "L'IHM tourne d\xC3\xA9j\xC3\xA0 (Maj+F8 l'arr\xC3\xAAte)" : "L'IHM est d\xC3\xA9j\xC3\xA0 arr\xC3\xAAt\xC3\xA9" "e (F8 la d\xC3\xA9marre)", 4.0);
            return true;
        }
        sim->command(start ? "hmi.start" : "hmi.stop");
        if (status_)
            status_->setTransientMessage(start ? "IHM d\xC3\xA9marr\xC3\xA9" "e (l'API ne bouge pas)" : "IHM arr\xC3\xAAt\xC3\xA9" "e (l'API ne bouge pas)", 4.0);
        refreshSimulationIndicator();
        return true;
    }
    // ---- 1.10 : le menu "Les deux" de la barre du haut (l'API et l'IHM ensemble) ----
    //  Les memes commandes que la barre de l'onglet Simulation . IHM (qui s'ouvre au
    //  besoin pour Demarrer les deux) ; sans IHM, l'API seule.
    if (id == "both.start" || id == "all.stop") {
        auto* sim = app_.hmi() ? dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")) : nullptr;
        if (!sim && app_.hmi()) {
            // l'onglet detache dans sa fenetre : hmiTab ne le voit pas, hmiTabs_ si
            const auto tab = hmiTabs_.find("simulation");
            sim = tab != hmiTabs_.end() ? dynamic_cast<HmiSimulationPane*>(tab->second) : nullptr;
        }
        if (!sim && app_.hmi() && id == "both.start") {
            openHmiPane("simulation");
            sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
        }
        if (sim) sim->command(id);
        else runSimulationTransport(id == "both.start" ? "sim.run" : "sim.stop");
        refreshSimulationIndicator();
        return true;
    }
    // ---- Lot API 8 : l'explorateur de fichiers - Affichage : celui de l'appli ou celui du systeme ----
    //  (le lien "Utiliser l'explorateur du systeme" de l'explorateur mene au systeme ; ici on en revient).
    if (id == "view.fileExplorer") {
        const bool system = !explorer::systemPreferred(app_.settings());
        explorer::setSystemPreferred(app_.settings(), system);
        (void)app_.settings().save();
        if (status_)
            status_->setMessage(system ? "Le bouton \xE2\x80\xA6 ouvrira l'explorateur du syst\xC3\xA8me (Affichage \xE2\x80\xBA Explorateur de "
                                         "fichiers : revenir \xC3\xA0 celui de l'appli)"
                                       : "Le bouton \xE2\x80\xA6 ouvrira l'explorateur de fichiers de l'appli (son aper\xC3\xA7u, ses dossiers du projet)");
        return true;
    }
    // Revenir avant : annuler les n dernieres actions (la liste d'Annuler).
    if (id.rfind("edit.undoTo:", 0) == 0) {
        const int n = std::atoi(std::string(id.substr(12)).c_str());
        for (int i = 0; i < n && app_.commands().canUndo(); ++i)
            if (auto r = app_.actions().trigger("edit.undo", app_.commands()); !r) break;
        refreshTopBar();
        return true;
    }
    // Un projet recent (le menu de la puce projet).
    // 1.11.2 (UNI, decision 204) : il REMPLACE le projet en memoire, dont les modifications
    // non enregistrees partaient sans un mot. Comme l'accueil : la question d'abord
    // (App::confirmReplace) ; le projet deja ouvert ici n'est pas relu du disque.
    if (id.rfind("project.openRecent:", 0) == 0) {
        std::string folder(id.substr(19));
        if (app_.project() && recentSameFolder(folder, app_.projectFolder())) {
            if (status_) status_->setTransientMessage(recentLeaf(folder) + " est d\xC3\xA9j\xC3\xA0 ouvert ici.", 6.0);
            return true;
        }
        app_.confirmReplace([&app = app_, folder] { app.openProjectFolder(folder); });
        return true;
    }
    // "Fermer le projet" (le menu de la puce projet, la palette).
    // 1.11.2 (UNI, decision 218) : il retirait de la memoire le projet modifie sans un mot :
    // ses modifications non enregistrees etaient perdues. La question de l'accueil d'abord
    // (App::confirmReplace, closing ; Annuler : il reste ouvert). L'action file.close, elle,
    // ne demande rien : le tutoriel (le bac a sable jete) et le depot d'un projet a part
    // (qui a deja pose sa question) la declenchent aussi. Ni this ni l'ecran dans le rappel :
    // fermer le projet ramene l'accueil, et l'ecran part.
    if (id == "file.close") {
        app_.confirmReplace([&app = app_] { (void)app.actions().trigger("file.close", app.commands()); }, /*closing*/ true);
        return true;
    }
    // Une ligne de la cloche venue de la simulation : son bouton, comme dans
    // Simulation > Vue d'ensemble.
    if (id.rfind("sim.go:", 0) == 0) {
        simCenterGo(std::string(id.substr(7)));
        return true;
    }
    // Ouvrir le dossier (un export termine, dans la cloche) : l'explorateur de
    // l'appli, a ce dossier.
    if (id.rfind("open.folder:", 0) == 0) {
        ui::FilePick pick;
        pick.title = "Le dossier de l'export";
        pick.start = std::string(id.substr(12));
        (void)ui::pickFile(pick, [](std::string) {});
        return true;
    }
    // Les commandes de la palette (">") qui ne sont pas des actions de la barre.
    // Au clavier (Ctrl+K puis ">notif", Entree) : les panneaux du bandeau.
    if (id == "cmd:cloche" || id == "cmd:annuler-liste" || id == "cmd:taches") {
        if (topBar_)
            topBar_->openMenu(id == "cmd:cloche" ? TopBar::Menu::Notices : id == "cmd:taches" ? TopBar::Menu::Tasks : TopBar::Menu::UndoList);
        return true;
    }
    if (id == "cmd:compiler") { openHmiPane("compiler"); return true; }
    if (id == "cmd:generer") { openHmiPane("generer"); return true; }
    if (id.rfind("cmd:theme:", 0) == 0) { app_.setTheme(id.substr(10)); return true; }
    if (id.rfind("cmd:raccourcis:", 0) == 0) { setEditorKeyProfile(id.substr(15)); return true; }   // 1.12.2
    // Deposer un fichier... : l'explorateur, puis la meme question qu'un depot.
    if (id == "files.drop") {
        ui::FilePick pick;
        pick.title = "D\xC3\xA9poser un fichier dans le projet";
        (void)ui::pickFile(pick, [this](std::string path) {
            if (!path.empty()) askDroppedFiles({std::move(path)});
        });
        return true;
    }
    return false;
}

// ------------------------------------------------ la palette Aller a / Faire... ----
void MainAnalysisScreen::topBarRememberGoTo(const GoToPanel::Result& r) {
    if (r.key.empty()) return;
    std::erase_if(topBarGoToRecents_, [&](const GoToPanel::Result& x) { return x.key == r.key; });
    topBarGoToRecents_.insert(topBarGoToRecents_.begin(), r);
    if (topBarGoToRecents_.size() > 8) topBarGoToRecents_.resize(8);
}

// topBarPalette : dans GoToWorkspace.cpp (il lui faut l'index d'Aller a, complet la seulement).

} // namespace app
