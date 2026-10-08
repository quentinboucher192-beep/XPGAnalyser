// =============================================================================
//  app/screens/HistoryWorkspace.cpp - lot 19 : l'historique et les raccourcis
// -----------------------------------------------------------------------------
//  CE QUE L'ECRAN APPORTE A LA PILE D'ANNULATION :
//   - l'endroit de chaque commande (l'onglet du centre, son sous-onglet) et le
//     moyen d'y retourner (une cle : "hmi:equipements#7", "hmi:vue:12",
//     "doc:3"...) ;
//   - les raccourcis qui marchent partout : Ctrl+Z, Ctrl+Y, Ctrl+Maj+Z,
//     Ctrl+S, Ctrl+H, Ctrl+Tab, Ctrl+PgSuiv / PgPrec ;
//   - le tiroir Historique : un double clic demande a revenir a un etat, la
//     question est posee ici, App::goToHistory le fait ;
//   - apres chaque mouvement, la barre d'etat dit ce qui a ete defait (ou
//     refait) et l'ecran montre l'endroit.
// =============================================================================
#include "Screens.hpp"
#include "../MacrosPane.hpp"
#include "../../help/HelpSession.hpp"
#include "../TablePaste.hpp"

#include "../App.hpp"
#include "../HistoryPanel.hpp"
#include "../TopBar.hpp"
#include "../hmi/HmiAskDialog.hpp"
#include "../hmi/HmiTutorial.hpp"
#include "../SearchFocus.hpp"          // lot API 8 : finitions (Ctrl+F)

#include <algorithm>
#include <string>

namespace app {

using namespace ui;

namespace {

const std::string kSep = " \xE2\x80\xBA ";          // " > " typographique
const std::string kIhmDot = "IHM \xC2\xB7 ";         // "IHM . " : le titre des onglets de l'IHM

// Le premier TabControl visible d'une page (ses sous-onglets), en largeur
// d'abord : la bande du haut d'un volet avant celles de ses fiches.
TabControl* firstTabs(Widget* root) {
    if (!root) return nullptr;
    std::vector<Widget*> queue{root};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        Widget* w = queue[i];
        if (w != root && !w->visible()) continue;
        if (w != root)
            if (auto* t = dynamic_cast<TabControl*>(w)) return t;
        for (const auto& c : w->children()) queue.push_back(c.get());
    }
    return nullptr;
}

} // namespace

TabControl* MainAnalysisScreen::subTabsOf(Widget* page) const { return firstTabs(page); }

// ------------------------------------------------------------ l'endroit ---
std::string MainAnalysisScreen::currentPlaceKey(std::string* name) const {
    core::CommandInfo info;
    describePlace(info);
    if (name) *name = info.place;
    return info.placeKey;
}

void MainAnalysisScreen::describePlace(core::CommandInfo& info) const {
    if (!centre_ || centre_->tabCount() == 0) return;
    const auto index = centre_->currentIndex();
    Widget* page = centre_->page(index);
    const auto* tab = centre_->tab(index);
    const std::string title = tab ? tab->title : std::string();

    std::string hmiKey;
    for (const auto& [key, widget] : hmiTabs_)
        if (widget == page) { hmiKey = key; break; }

    if (!hmiKey.empty()) {
        info.placeKey = "hmi:" + hmiKey;
        if (hmiKey.rfind("vue:", 0) == 0) {
            info.place = "IHM" + kSep + "Vues" + kSep + title;
            return;                                  // l'inspecteur n'est pas un endroit
        } else {
            std::string rest = title.rfind(kIhmDot, 0) == 0 ? title.substr(kIhmDot.size()) : title;
            info.place = "IHM" + kSep + rest;
        }
        if (auto* tabs = firstTabs(page); tabs && tabs->tabCount() > 1) {
            const auto sub = tabs->currentIndex();
            if (const auto* t = tabs->tab(sub)) {
                info.place += kSep + t->title;
                info.placeKey += "#" + std::to_string(sub);
            }
        }
        return;
    }
    for (const auto& d : documents_)
        if (d.tab == index) {
            info.placeKey = "doc:" + std::to_string(d.section);
            info.place = "API" + kSep + "Sections" + kSep + title;
            return;
        }
    info.placeKey = "tab:" + title;
    info.place = title;
}

void MainAnalysisScreen::goToPlace(const std::string& key) {
    if (key.empty() || !centre_) return;
    if (key == currentPlaceKey()) return;
    if (key.rfind("hmi:", 0) == 0) {
        std::string rest = key.substr(4);
        int sub = -1;
        if (const auto hash = rest.find('#'); hash != std::string::npos) {
            sub = std::atoi(rest.c_str() + hash + 1);
            rest.resize(hash);
        }
        if (rest.rfind("vue:", 0) == 0) {
            const auto id = std::strtoull(rest.c_str() + 4, nullptr, 10);
            auto doc = app_.hmi();
            if (!doc || !doc->project.view(static_cast<hmi::Id>(id))) return;   // la vue n'existe plus
            openHmiView(id);
        } else {
            openHmiPane(rest);
        }
        if (sub >= 0)
            if (auto* tabs = firstTabs(hmiTab(rest)); tabs && static_cast<std::size_t>(sub) < tabs->tabCount())
                tabs->setCurrentIndex(static_cast<std::size_t>(sub));
        return;
    }
    if (key.rfind("doc:", 0) == 0) {
        const auto section = static_cast<domain::Index>(std::strtoul(key.c_str() + 4, nullptr, 10));
        if (auto p = app_.project(); p && section < p->sections.size()) openDocument(section);
        return;
    }
    if (key.rfind("tab:", 0) == 0) {
        const std::string title = key.substr(4);
        for (std::size_t i = 0; i < centre_->tabCount(); ++i)
            if (const auto* t = centre_->tab(i); t && t->title == title) { centre_->setCurrentIndex(i); return; }
    }
}

// ------------------------------------------------------------ le tiroir ---
void MainAnalysisScreen::wireHistory() {
    app_.setPlaceProvider([this](core::CommandInfo& info) { describePlace(info); });
    if (historyPanel_) {
        historyPanel_->setStack(&app_.commands());
        links_ += historyPanel_->goToRequested->connect([this](std::uint64_t serial) { askGoToHistory(serial); });
        links_ += historyPanel_->placeRequested->connect([this](const std::string& key) { goToPlace(key); });
        links_ += historyPanel_->savedStateRequested->connect([this] {
            // L'etat enregistre : sa position dans la frise.
            const auto& stack = app_.commands();
            const auto saved = stack.savedDepth();
            if (saved < 0) return;
            const auto nDone = static_cast<std::ptrdiff_t>(stack.done().size());
            if (saved == 0) { askGoToHistory(0); return; }
            // L'entree juste avant le point enregistre (dans la frise).
            const auto t = saved - 1;
            const auto& e = t < nDone ? stack.done()[static_cast<std::size_t>(t)]
                                      : stack.undone()[stack.undone().size() - 1 - static_cast<std::size_t>(t - nDone)];
            askGoToHistory(e.info.serial);
        });
    }
    links_ += app_.events().subscribe<HistoryToggle>([this](const HistoryToggle&) { toggleHistory(); });
    links_ += app_.events().subscribe<HistoryMoved>([this](const HistoryMoved& e) { onHistoryMoved(e); });
    links_ += app_.events().subscribe<SectionTextsRestored>([this](const SectionTextsRestored&) { refreshOpenDocuments(); });
    links_ += app_.events().subscribe<StatusNotice>([this](const StatusNotice& e) {
        if (status_) status_->setTransientMessage(e.text, e.seconds, StatusBar::Severity::Success);
    });
    // Lot 21 : apres un enregistrement, les versions (et la version automatique).
    links_ += app_.events().subscribe<ProjectSaved>([this](const ProjectSaved& e) {
        refreshVersions();
        if (!e.autoVersion.empty() && status_)
            status_->setTransientMessage("Enregistr\xC3\xA9 \xE2\x80\x94 version automatique " + e.autoVersion + " (Versions)", 6.0,
                                         StatusBar::Severity::Success);
    });
    // Lot 20 : Aller a... - le champ de la barre, le panneau, ce qu'on y choisit.
    if (goToBox_ && goToPanel_) {
        links_ += goToBox_->clicked->connect([this] { openGoTo(); });
        goToPanel_->setSearch([this](const std::string& text) { return goToSearch(text); });
        links_ += goToPanel_->chosen->connect([this](const GoToPanel::Result& r) { goToResult(r); });
    }
    // Lot 20 : ce qu'un collage depuis Excel a fait, dans la barre d'etat de l'ecran.
    App* app = &app_;
    paste::setNotifier([app](const std::string& text) { app->events().publish(StatusNotice{text, 8.0}); });
}

void MainAnalysisScreen::toggleHistory() {
    if (!historyPanel_) return;
    if (!historyPanel_->isOpen()) {
        std::string name;
        const auto key = currentPlaceKey(&name);
        historyPanel_->setCurrentPlace(key, name);
        historyPanel_->setTimes(app_.openedAtMs(), app_.savedAtMs());
    }
    historyPanel_->toggle();
}

void MainAnalysisScreen::askGoToHistory(std::uint64_t serial) {
    const auto& stack = app_.commands();
    // Ce qui bougera : les faites au-dessus d'elle (annulees), ou les annulees
    // jusqu'a elle (retablies).
    std::vector<const core::CommandInfo*> moving;
    bool undo = true;
    const core::CommandInfo* target = nullptr;
    if (serial == 0) {
        for (auto it = stack.done().rbegin(); it != stack.done().rend(); ++it) moving.push_back(&it->info);
    } else {
        bool inDone = false;
        for (const auto& e : stack.done()) if (e.info.serial == serial) { inDone = true; target = &e.info; }
        if (inDone) {
            for (auto it = stack.done().rbegin(); it != stack.done().rend() && it->info.serial != serial; ++it)
                moving.push_back(&it->info);
        } else {
            undo = false;
            for (auto it = stack.undone().rbegin(); it != stack.undone().rend(); ++it) {
                moving.push_back(&it->info);
                if (it->info.serial == serial) { target = &it->info; break; }
            }
            if (!target) return;
        }
    }
    if (moving.empty()) {
        if (status_) status_->setTransientMessage("C'est d\xC3\xA9j\xC3\xA0 l'\xC3\xA9tat actuel.", 5.0);
        if (target) goToPlace(target->placeKey);
        return;
    }

    HmiAskDialog::Spec spec;
    spec.id = "dialog.historique";
    spec.title = undo ? "Revenir \xC3\xA0 un \xC3\xA9tat pr\xC3\xA9" "c\xC3\xA9" "dent" : "R\xC3\xA9tablir jusqu'\xC3\xA0 une action";
    if (serial == 0) spec.text = "Revenir \xC3\xA0 l'\xC3\xA9tat de l'ouverture du projet"
                                 + (app_.openedAtMs() > 0 ? " (" + historyClock(app_.openedAtMs()) + ")" : std::string()) + " ?";
    else if (undo) spec.text = "Revenir \xC3\xA0 l'\xC3\xA9tat juste apr\xC3\xA8s\n\xC2\xAB " + target->label + " \xC2\xBB ("
                               + historyClock(target->lastMs) + ") ?";
    else spec.text = "R\xC3\xA9tablir jusqu'\xC3\xA0\n\xC2\xAB " + target->label + " \xC2\xBB (" + historyClock(target->lastMs) + ") ?";
    const auto n = moving.size();
    spec.text += "\n\n" + std::to_string(n) + (undo ? (n > 1 ? " actions seront annul\xC3\xA9" "es :" : " action sera annul\xC3\xA9" "e :")
                                                   : (n > 1 ? " actions seront r\xC3\xA9tablies :" : " action sera r\xC3\xA9tablie :"));
    const std::size_t shown = std::min<std::size_t>(n, 8);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto* m = moving[i];
        spec.text += "\n   \xE2\x80\xA2 " + m->label + "  (" + historyClock(m->lastMs)
                   + (m->place.empty() ? std::string() : ", " + m->place) + ")";
    }
    if (n > shown) spec.text += "\n   \xE2\x80\xA6 et " + std::to_string(n - shown) + " autre(s)";
    spec.note = undo ? "Elles restent dans l'historique, gris\xC3\xA9" "es : Ctrl+Y ou un double clic les r\xC3\xA9tablit. "
                       "Une nouvelle action les oublierait."
                     : "Ctrl+Z les annule \xC3\xA0 nouveau.";
    spec.confirm = undo ? "Revenir \xC3\xA0 cet \xC3\xA9tat" : "R\xC3\xA9tablir";
    spec.cancel = "Annuler";
    if (target && !target->placeKey.empty()) {
        spec.stopLabel = "Aller \xC3\xA0 l'endroit seulement";
        const std::string key = target->placeKey;
        spec.onStop = [this, key] {
            app_.menus().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
            goToPlace(key);
        };
    }
    spec.width = 680.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, serial](const menu::DialogResult& r) {
        if (r.accepted()) app_.goToHistory(serial);
    });
}

void MainAnalysisScreen::onHistoryMoved(const HistoryMoved& e) {
    if (!status_) return;
    if (!e.error.empty() && e.count == 0) {
        status_->setTransientMessage(std::string(e.undo ? "Annuler" : "R\xC3\xA9tablir") + " : " + e.error, 8.0,
                                     StatusBar::Severity::Warning);
        return;
    }
    if (e.count == 0) {
        status_->setTransientMessage(e.undo ? "Rien \xC3\xA0 annuler." : "Rien \xC3\xA0 r\xC3\xA9tablir.", 4.0);
        return;
    }
    std::string text;
    if (e.toOpening)
        text = "Revenu \xC3\xA0 l'\xC3\xA9tat de l'ouverture (" + std::to_string(e.count)
             + (e.count > 1 ? " actions annul\xC3\xA9" "es)" : " action annul\xC3\xA9" "e)") + " \xE2\x80\x94 Ctrl+Y pour r\xC3\xA9tablir";
    else if (e.toState)
        text = "Revenu \xC3\xA0 l'\xC3\xA9tat juste apr\xC3\xA8s \xC2\xAB " + e.label + " \xC2\xBB (" + std::to_string(e.count)
             + (e.count > 1 ? " actions annul\xC3\xA9" "es)" : " action annul\xC3\xA9" "e)") + " \xE2\x80\x94 Ctrl+Y pour r\xC3\xA9tablir";
    else if (e.count == 1)
        text = std::string(e.undo ? "Annul\xC3\xA9 : " : "R\xC3\xA9tabli : ") + e.label
             + (e.undo ? " \xE2\x80\x94 Ctrl+Y pour r\xC3\xA9tablir" : " \xE2\x80\x94 Ctrl+Z pour annuler");
    else
        text = std::string(e.undo ? "Revenu en arri\xC3\xA8re de " : "R\xC3\xA9tabli : ") + std::to_string(e.count)
             + " actions (jusqu'\xC3\xA0 \xC2\xAB " + e.label + " \xC2\xBB)";
    if (!e.error.empty()) text += " \xE2\x80\x94 attention : " + e.error;
    status_->setTransientMessage(text, 8.0, e.error.empty() ? StatusBar::Severity::Info : StatusBar::Severity::Warning);
    // MONTRER L'ENDROIT : ce qui vient d'etre defait doit se voir.
    goToPlace(e.placeKey);
}

// ------------------------------------------------------------ raccourcis ---
void MainAnalysisScreen::cycleSubTab(int step) {
    if (!centre_ || centre_->tabCount() == 0) return;
    auto* tabs = firstTabs(centre_->page(centre_->currentIndex()));
    if (!tabs || tabs->tabCount() < 2) { cycleCentreTab(step); return; }
    const auto n = static_cast<int>(tabs->tabCount());
    const int next = ((static_cast<int>(tabs->currentIndex()) + step) % n + n) % n;
    tabs->setCurrentIndex(static_cast<std::size_t>(next));
    if (const auto* t = tabs->tab(static_cast<std::size_t>(next)); t && status_)
        status_->setTransientMessage(t->title + (t->badge.empty() ? std::string() : "  \xC2\xB7  " + t->badge), 2.5);
}

void MainAnalysisScreen::cycleCentreTab(int step) {
    if (!centre_ || centre_->tabCount() < 2) return;
    const auto n = static_cast<int>(centre_->tabCount());
    const int next = ((static_cast<int>(centre_->currentIndex()) + step) % n + n) % n;
    centre_->setCurrentIndex(static_cast<std::size_t>(next));
}

bool MainAnalysisScreen::handleShortcut(const KeyDown& k, bool beforeWidgets) {
    // Lot 21 : Ctrl+Alt+S - creer une version.
    if (k.mods.ctrl && k.mods.alt && !k.mods.shift && k.key == Key::S && !beforeWidgets && !k.repeat) {
        askCreateVersion();
        return true;
    }
    // Lot API 7 : F9 ouvre l'onglet API > Simulation (ecrit dans les menus
    // depuis le debut, jamais relie a la touche).
    if (k.key == Key::F9 && k.mods.none() && !beforeWidgets && !k.repeat) {
        openApiTabFromAction("sim:ensemble");   // Lot API 8 : Centre de simulation - F9 ouvre la Vue d'ensemble (la maquette)
        return true;
    }
    // 1.10 (chantier N) : F7 - Compiler l'IHM (le rapport ; ouvert, il repart).
    if (k.key == Key::F7 && k.mods.none() && !beforeWidgets && !k.repeat && app_.hmi()) {
        refreshBuildState(true);            // 1.11 (chantier T3, C4) : les icones de l'arbre, refaites
        openHmiPane("compiler");
        return true;
    }
    // 1.10 (chantier L) : F8 - demarrer l'IHM simulee ; Maj+F8 - l'arreter (l'API ne bouge
    // pas ; la barre du haut et l'onglet Simulation . IHM font la meme chose).
    if (k.key == Key::F8 && !k.mods.ctrl && !k.mods.alt && !beforeWidgets && !k.repeat && app_.hmi()) {
        (void)topBarLot8Action(k.mods.shift ? "hmi.stop" : "hmi.start");
        return true;
    }
    // ---- Lot API 8 : Centre de simulation (F5 Simuler / Continuer, Maj+F5 Arreter, F10 la section suivante) ----
    if (!beforeWidgets && simCenterShortcut(k)) return true;
    // ---- fin Lot API 8 : Centre de simulation ----
    // ---- Lot API 8 : l'arbre du projet (Echap dans l'arbre filtre : le filtre s'efface) ----
    if (!beforeWidgets && k.key == Key::Escape && k.mods.none() && explorer_ && explorer_->focused() && !treeFilterText().empty()) {
        setTreeFilter("");
        return true;
    }
    // ---- fin Lot API 8 : l'arbre du projet ----
    if (!k.mods.ctrl || k.mods.alt) return false;
    if (beforeWidgets) {
        // Lot 20 : Ctrl+K, meme dans un champ en saisie (il n'y fait rien).
        if (k.key == Key::K && !k.mods.shift) { openGoTo(); return true; }
        // ---- Lot API 8 : l'arbre du projet (Ctrl+Maj+F : le curseur dans le filtre de l'arbre) ----
        if (k.key == Key::F && k.mods.shift && !k.repeat) { focusTreeFilter(); return true; }
        // ---- fin Lot API 8 : l'arbre du projet ----
        if (k.key == Key::Tab) { cycleSubTab(k.mods.shift ? -1 : 1); return true; }
        if (k.key == Key::PageDown) { cycleCentreTab(1); return true; }
        if (k.key == Key::PageUp) { cycleCentreTab(-1); return true; }
        return false;
    }
    if (k.repeat && k.key != Key::Z && k.key != Key::Y) return false;
    // ---- Lot API 8 : finitions (Ctrl+F dans les volets) ----
    //  Apres les widgets (un editeur qui aurait son chercher le garde) : le
    //  curseur dans le champ de recherche montre de l'onglet en cours, son
    //  texte choisi (app/SearchFocus.hpp).
    if (k.key == Key::F && !k.mods.shift) {
        Widget* page = centre_ && centre_->tabCount() > 0 ? centre_->page(centre_->currentIndex()) : nullptr;
        if ((!page || !focusSearchField(*page)) && status_)
            status_->setTransientMessage("Pas de champ de recherche dans cet onglet : Ctrl+K cherche partout", 4.0);
        return true;
    }
    // ---- fin Lot API 8 : finitions ----
    switch (k.key) {
        case Key::Z:
            if (k.mods.shift) app_.redo();
            else app_.undo();
            return true;
        case Key::Y:
            app_.redo();
            return true;
        case Key::S:
            app_.saveFromKeyboard();
            return true;
        case Key::H:
            toggleHistory();
            return true;
        // Lot 7 : Ctrl+W ferme l'onglet ouvert (une question s'il a du pas
        // enregistre) ; apres les widgets : un menu ouvert garde la touche.
        case Key::W:
            if (k.mods.shift) return false;
            (void)closeCurrentTab();
            return true;
        // 1.11 (T2) : Ctrl+Maj+O importe la configuration materielle (.XHW) ;
        // Ctrl+H reste a l'Historique (la table help::keys).
        // 1.11.2 (UNI, decision 218) : Ctrl+O, Ouvrir un projet (ecrit a cote de "Ouvrir..."
        // dans la puce projet et dans la table help::keys) ne faisait rien ici. L'action
        // file.open pose d'abord la question de l'accueil si le projet est modifie.
        case Key::O:
            if (k.repeat) return false;
            (void)app_.actions().trigger(k.mods.shift ? "file.importHardware" : "file.open", app_.commands());
            return true;
        // Lot API 7 : Ctrl+1 l'onglet API > Variables, Ctrl+5 API > Statistiques.
        case Key::Num1:
            if (k.mods.shift) return false;
            openApiTabFromAction("variables");
            return true;
        case Key::Num5:
            if (k.mods.shift) return false;
            openApiTabFromAction("statistiques");
            return true;
        default:
            return false;
    }
}

void MainAnalysisScreen::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    takePendingApiTutorial();      // lot API 7 : l'accueil l'a demande avant que le projet soit la
    refreshHistoryButtons();
    // Lot API 2 : le numero de cycle de la simulation, le projet (nom, etat).
    if (topBar_ && app_.simulation().attached()) refreshSimulationIndicator();
    frameClock_ = f.totalSeconds;   // ---- Lot API 8 : bandeau haut (l'horloge de l'appli) ----
    refreshTopBar();
    // Lot macros 1 : l'apercu de la macro, differe apres une frappe.
    if (auto* macros = macrosPane()) macros->tick(f.totalSeconds);
    // Lot macros 1 : " Lancer " depuis la page d'une macro, dans l'aide.
    if (auto name = help::takePendingMacroLaunch(); !name.empty()) openMacros(name, /*launch=*/true);
    // ---- Lot API 8 : glisser n'importe quel fichier ----
    tickDroppedMacros();           // les macros cochees d'un depot : la suivante, quand celle-ci est fermee
    pollLisibleJobs();             // 1.8.0 : l'import suivi, l'export lisible (leurs fils)
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----
    // Lot 21 : l'etape interactive du didacticiel verifie le geste attendu.
    if (hmiTutorial_ && hmiTutorial_->active()) hmiTutorial_->poll(f.totalSeconds);
    // Lot API 3 : les valeurs en direct de la table d'animation, sa courbe.
    // Lot API 7 : et l'onglet Simulation, ou qu'il soit (derriere un autre
    // onglet, dans un autre groupe, detache) - il ne lit rien s'il est cache.
    tickApiPanes(f.totalSeconds);
    // Lot API 7 : la pastille de API > Simulation (en marche, en pause, halte),
    // meme quand l'onglet est ferme ; l'arbre ne se redessine que si elle change.
    refreshSimulationBadge();
    // ---- Lot API 8 : le bandeau bas (StatusStripWorkspace.cpp) ----
    tickStatusStrip(f.totalSeconds);
    // ---- fin Lot API 8 : le bandeau bas ----
    tickHmiBuild();                // 1.11.13 : le build de l'IHM (sa fin, la progression, l'analyse)
}

void MainAnalysisScreen::refreshHistoryButtons() {
    const auto& stack = app_.commands();
    if (historyPanel_ && historyPanel_->isOpen()) {
        historyPanel_->setTimes(app_.openedAtMs(), app_.savedAtMs());
        std::string name;
        const auto key = currentPlaceKey(&name);
        historyPanel_->setCurrentPlace(key, name);
        if (stack.revision() != historyRevision_) historyPanel_->invalidate();
    }
    if (stack.revision() == historyRevision_) return;
    historyRevision_ = stack.revision();
    // Lot API 2 : Annuler / Retablir sont dans la barre du haut (TopBar), et le
    // point « modifie » a cote du nom du projet.
    if (topBar_)
        topBar_->setHistory(stack.canUndo(), stack.canUndo() ? "Annuler : " + stack.undoLabel() + " (Ctrl+Z)" : "Rien \xC3\xA0 annuler (Ctrl+Z)",
                            stack.canRedo(), stack.canRedo() ? "R\xC3\xA9tablir : " + stack.redoLabel() + " (Ctrl+Y)" : "Rien \xC3\xA0 r\xC3\xA9tablir (Ctrl+Y)");
    shownModified_ = stack.isModified() && app_.project() != nullptr;
    refreshTopBar();
}

} // namespace app
