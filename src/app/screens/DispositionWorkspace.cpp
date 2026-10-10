// =============================================================================
//  app/screens/DispositionWorkspace.cpp - 1.12.3 : PROJET > DISPOSITION...
// -----------------------------------------------------------------------------
//  La fenetre de l'application suit la disposition en vigueur (Disposition.hpp) :
//  ses panneaux (les cases d'Affichage > Panneaux a afficher font toujours le
//  travail), leur place (l'explorateur a droite, le panneau du bas a droite de
//  l'editeur), la bande d'etat, les lignes alternees ; les sous-onglets des pages
//  ouvertes ; les pages du centre qui s'ouvrent seules (a l'ouverture du projet,
//  au demarrage de la simulation) et ou elles vont (un onglet, cote a cote, une
//  fenetre detachee). Gardee dans les reglages de l'edition (disposition.*).
// =============================================================================
#include "Screens.hpp"
#include "../App.hpp"
#include "../Disposition.hpp"
#include "../DispositionDialog.hpp"
#include "../hmi/HmiBuildPanes.hpp"
#include "../hmi/HmiCommPanes.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../../core/Edition.hpp"
#include "../../ui/widgets/TabArea.hpp"

#include <string_view>

namespace app {

namespace dp = disposition;
using ui::Checkbox;

namespace {

// Un panneau de la fenetre -> la cle de sa case (Affichage > Panneaux a afficher).
struct PanelRow {
    const char* row;
    const char* key;
};
constexpr PanelRow kPanelRows[] = {
    {"explorateur", "view.projectExplorer"}, {"documents", "view.openDocuments"},  {"bas", "view.outputPanel"},
    {"configuration", "view.plcConfiguration"}, {"rangee", "view.bottomRow"},    {"diagnostics", "view.diagnostics"},
    {"resume", "view.analysisSummary"},      {"etatProjet", "view.projectStatus"},
};
const char* rowOfPanel(std::string_view key) {
    for (const auto& p : kPanelRows)
        if (key == p.key) return p.row;
    return nullptr;
}

} // namespace

void MainAnalysisScreen::loadDisposition() {
    auto& s = app_.settings();
    const bool kept = !s.keysWithPrefix(std::string(dp::kPrefix)).empty();
    dp::Layout l = dp::fromSettings([&s](const std::string& k) { return s.getString(k); });
    if (!kept) {
        // La premiere fois : ce que montraient les cases d'Affichage > Panneaux a afficher (leurs
        // reglages ; une installation neuve : sans la rangee du bas ni les documents ouverts).
        for (const auto& p : kPanelRows)
            if (l.items.count(p.row)) {
                const std::string_view key = p.key;
                l.items[p.row].shown = s.getBool(p.key, key != "view.bottomRow" && key != "view.openDocuments");
            }
        if (l.items.count("alternees")) l.items["alternees"].shown = s.getBool("view.alternatingRows", true);
    }
    dp::setCurrent(std::move(l));
}

void MainAnalysisScreen::saveDisposition() {
    auto& s = app_.settings();
    dp::toSettings(dp::current(), [&s](const std::string& k, const std::string& v) { s.set(k, v); },
                   [&s](const std::string& k) { s.remove(k); });
    s.save();
}

void MainAnalysisScreen::applyDisposition(DispositionMode mode) {
    const auto& l = dp::current();
    const auto base = dp::defaults();
    const bool sim = dp::simulating();
    // Ce qu'on touche : tout (la fenetre Disposition) ; au lancement, ce que l'utilisateur
    // a regle (le reste garde ce que la fenetre avait a la fermeture) ; au demarrage et a
    // l'arret de la simulation, ce qui ne se montre qu'en edition ou en simulation.
    const auto touched = [&](const char* row) {
        const auto& it = l.item(row);
        switch (mode) {
            case DispositionMode::All: return true;
            case DispositionMode::Startup: return !(it == base.item(row));
            case DispositionMode::Simulation: return it.shown && it.when != "toujours";
        }
        return false;
    };
    for (const auto& p : panels_) {
        const char* row = rowOfPanel(p.key);
        if (!row || !dp::row(row) || !touched(row)) continue;
        bool want = dp::shown(l, row, sim);
        if (mode == DispositionMode::Startup && std::string_view(row) == "bas" && l.item("bas").start == "replie") want = false;
        if (p.box->isChecked() != want) p.box->setState(want ? Checkbox::State::Checked : Checkbox::State::Unchecked);
    }
    if (altRowsBox_ && dp::row("alternees") && touched("alternees")) {
        const bool want = l.item("alternees").shown;
        if (altRowsBox_->isChecked() != want) altRowsBox_->setState(want ? Checkbox::State::Checked : Checkbox::State::Unchecked);
    }
    // Les places : l'explorateur (la colonne de gauche) apres le centre ; le panneau du bas a
    // droite de l'editeur.
    if (upper_ && upper_->paneCount() > 1) {
        std::vector<std::size_t> order;
        if (l.item("explorateur").where == "droite") {
            for (std::size_t k = 1; k < upper_->paneCount(); ++k) order.push_back(k);
            order.push_back(0);
        }
        upper_->setOrder(std::move(order));
    }
    if (centreColumn_)
        centreColumn_->setOrientation(l.item("bas").where == "droite" ? ui::Orientation::Horizontal : ui::Orientation::Vertical);
    if (status_ && dp::row("etat"))
        status_->setVisibility(dp::shown(l, "etat", sim) ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    // Les sous-onglets : le panneau du bas, puis les pages ouvertes.
    if (bottomPanel_ && bottomPanel_->tabs()) dp::applyTabs(*bottomPanel_->tabs(), "bas", mode == DispositionMode::Startup);
    for (const auto& [key, page] : hmiTabs_) {
        if (auto* editor = dynamic_cast<HmiEditor*>(page)) editor->applyDisposition();
        else if (auto* simPane = dynamic_cast<HmiSimulationPane*>(page)) dp::applyTabs(simPane->tabs(), "sim");
        else if (auto* scripts = dynamic_cast<HmiScriptsPane*>(page)) { if (scripts->tabs()) dp::applyTabs(*scripts->tabs(), "prog"); }
        else if (auto* comm = dynamic_cast<HmiCommPane*>(page)) dp::applyTabs(comm->tabs(), "eq");
    }
    applyPanelVisibility();        // les separateurs se remettent en page d'eux-memes
}

void MainAnalysisScreen::placeDispositionPage(const char* row, std::size_t tab) {
    if (!centre_ || !dp::row(row) || tab >= centre_->tabCount()) return;
    const std::string& where = dp::current().item(row).where;
    if (where == "cote") {
        if (centre_->tabCount() > 1) (void)centre_->splitTab(tab, ui::TabArea::Side::Right);
    } else if (where == "detache") {
        (void)detachTab(tab);
    }
}

void MainAnalysisScreen::openDispositionPages(const std::string& when, bool startView) {
    if (!core::hasIhm() || !app_.hmi()) return;
    const auto& l = dp::current();
    const auto opens = [&](const char* row) { return dp::row(row) && l.item(row).when == when; };
    if (startView && opens("page.vue"))
        if (const auto doc = app_.hmi(); doc && doc->project.view(doc->project.config.startView)) openHmiView(doc->project.config.startView);
    if (opens("page.programmation")) openHmiPane("scripts");
    if (opens("page.poste")) openHmiPane("poste");
    // La simulation en dernier : elle reste devant.
    if (opens("page.simulation") && !(when == "simulation" && hmiTab("simulation"))) openHmiPane("simulation");
}

void MainAnalysisScreen::openDisposition() {
    DispositionDialog::Spec spec;
    spec.applied = dp::current();
    const std::string mine = app_.settings().getString(std::string(dp::kMineKey));
    spec.mine = mine.empty() ? dp::defaults() : dp::parse(mine);
    spec.edition = !core::hasApi() ? "XPGAnalyser IHM" : !core::hasIhm() ? "XPGAnalyser API" : "XPGAnalyser";
    spec.apply = [this](const dp::Layout& l) {
        dp::setCurrent(l);
        saveDisposition();
        applyDisposition(DispositionMode::All);
        if (status_) status_->setTransientMessage("Disposition appliqu\xC3\xA9" "e (gard\xC3\xA9" "e dans les r\xC3\xA9glages)", 4.0);
    };
    spec.keepMine = [this](const dp::Layout& l) {
        app_.settings().set(std::string(dp::kMineKey), dp::serialize(l));
        app_.settings().save();
    };
    spec.reset = [this] {
        app_.settings().removePrefix(std::string(dp::kPrefix));
        app_.settings().save();
    };
    app_.menus().ShowDialog(std::make_unique<DispositionDialog>(std::move(spec)), [](const menu::DialogResult&) {});
}

} // namespace app
