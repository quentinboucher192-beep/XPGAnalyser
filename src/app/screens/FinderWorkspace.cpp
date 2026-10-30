// =============================================================================
//  app/screens/FinderWorkspace.cpp - 1.14.0 (lot M5) : chercher les fichiers sur le PC
// -----------------------------------------------------------------------------
//  Le volet (FileFinderPane) ne fait aucun geste lui-meme : ses Hosts sont ici.
//    - dans l'ecran du projet : un onglet de document ("Chercher des fichiers"),
//      un seul, que Ctrl+Maj+P, Projet > Chercher des fichiers sur ce PC... et
//      Aller a / Faire (Ctrl+K) ouvrent ou montrent ; il reste quand le projet
//      change (il n'est d'aucun projet) ;
//    - depuis l'accueil (le menu principal), un ecran plein (FileFinderScreen),
//      Retour (Echap) en haut : on n'a pas de projet ou importer, le reste marche.
//  Les gestes : Ouvrir (ici ; un fichier de l'autre application la lance avec
//  lui), Importer selon le genre (un paquet IHM par la fenetre Importer, un
//  modele par la galerie, une bibliotheque IHM par Relier, un .XPG / .XHW par la
//  question du depot, un enregistrement .xrec dans Ressources > Enregistrements,
//  un bloc ou une macro dans la bibliotheque, un theme dans les themes), Ajouter
//  aux recents (un projet).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../FileFinder.hpp"
#include "../FileFinderPane.hpp"
#include "../hmi/HmiAssetPanes.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/ThemeFile.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace app {

using ui::Checkbox;
using ui::Icon;
using ui::TabControl;

namespace {

// Ce qui se fait sans l'ecran du projet : ouvrir, les recents, la bibliotheque, les themes.
// `workspace` : l'ecran du projet (il importe le reste lui-meme).
FileFinderPane::Hosts commonHosts(App& app, FileFinderPane* pane, bool workspace) {
    FileFinderPane::Hosts h;
    h.context = [&app, workspace] {
        finder::Context c;
        c.edition = core::edition();
        c.workspace = workspace;
        c.hmiProject = core::hasIhm() && app.hmi() != nullptr;
        c.plcProject = core::hasApi() && app.project() != nullptr;
        c.recordings = core::hasIhm();   // 1.14.0 (lot M4) : un .xrec va dans Ressources > Enregistrements
        return c;
    };
    h.open = [&app, pane](const finder::Hit& hit) {
        const auto here = core::edition();
        const std::string target = hit.key == "xpgproj" ? finder::projectFolderOf(hit) : hit.path;
        if (!finder::openedHere(hit, here)) {
            // L'autre application, lancee avec le fichier (deja ouverte : elle le recoit).
            const auto studio = finder::studioOf(hit, here);
            const std::string name(finder::studioName(studio));
            const std::string why = finder::launchStudio(studio, target);
            pane->tell(why.empty() ? name + " s'ouvre avec " + hit.name + "." : "Ouvrir avec " + name + " : " + why + ".");
            return;
        }
        // Un .XHW ou un .XDB complete le projet ouvert (comme Ouvrir...) : pas de question.
        if (hit.key == "xhw" || hit.key == "xdb") {
            app.openPath(target);
            return;
        }
        if (hit.key == "xpgproj") app.confirmReplace([&app, target] { app.openProjectFolder(target); });
        else app.confirmReplace([&app, target] { app.openPath(target); });
    };
    h.addRecent = [&app, pane](const finder::Hit& hit) {
        const std::string folder = finder::projectFolderOf(hit);
        app.rememberPath(folder);
        app.settings().setList("recent.projects", app.recentPaths());
        (void)app.settings().save();
        pane->tell("Ajout\xC3\xA9 aux r\xC3\xA9" "cents : " + folder + " (l'accueil le montre en t\xC3\xAA" "te).");
    };
    h.import = [&app, pane](const finder::Hit& hit) {
        switch (finder::importKindOf(hit.key)) {
            case finder::ImportKind::Library: {
                project::SharedLibrary library(project::SharedLibrary::defaultRoot());
                (void)library.scan();
                auto added = library.importFile(hit.path);
                if (!added) {
                    pane->tell("Ajouter \xC3\xA0 la biblioth\xC3\xA8que : " + added.error().message());
                    return;
                }
                pane->tell("Ajout\xC3\xA9 \xC3\xA0 la biblioth\xC3\xA8que : " + added->name + " " + added->version + " (" + added->category + ")"
                           + (hit.key == "mac" ? std::string(" - l'onglet Macros le montre.") : std::string(" - Aide \xE2\x80\xBA Blocs DFB / DDT.")));
                return;
            }
            case finder::ImportKind::Theme: {
                const auto r = ui::UserThemes::importFile(hit.path);
                if (!r.ok) {
                    pane->tell("Ajouter aux th\xC3\xA8mes : ligne " + std::to_string(r.line) + " : " + r.error);
                    return;
                }
                app.setTheme(r.name);
                pane->tell("Th\xC3\xA8me \xC2\xAB " + r.name + " \xC2\xBB ajout\xC3\xA9 et appliqu\xC3\xA9"
                           + (r.renamedFrom.empty() ? std::string{} : " (\xC2\xAB " + r.renamedFrom + " \xC2\xBB existait d\xC3\xA9j\xC3\xA0)")
                           + " - Affichage \xE2\x80\xBA Th\xC3\xA8me\xE2\x80\xA6");
                return;
            }
            default:
                pane->tell("Importer : depuis l'onglet Chercher des fichiers d'un projet ouvert.");
                return;
        }
    };
    h.saveChoices = [&app](const std::string& text) { app.settings().set(std::string(finder::kSettingsKey), text); };
    h.help = [&app] {
        app.setHelpTopic("chercher-fichiers");
        app.menus().PushMenu("help.hmi");
    };
    return h;
}

} // namespace

// ============================================================ l'ecran du projet ==
FileFinderPane* MainAnalysisScreen::fileFinder() const {
    const auto it = hmiTabs_.find("chercher");
    return it == hmiTabs_.end() ? nullptr : dynamic_cast<FileFinderPane*>(it->second);
}

void MainAnalysisScreen::openFileFinder() {
    if (!centre_) return;
    // Le centre doit etre a l'ecran (comme Affichage > Panneaux a afficher).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    if (auto* page = fileFinder()) {
        if (showPage(page)) return;      // en onglet, ou sa fenetre detachee devant
    }
    auto made = std::make_unique<FileFinderPane>("chercher");
    auto* pane = made.get();
    pane->setChoices(finder::parseChoices(app_.settings().getString(std::string(finder::kSettingsKey))));
    auto hosts = commonHosts(app_, pane, true);
    // L'ecran du projet importe aussi dans le projet ouvert (et dit ce qu'il a fait dans sa barre d'etat).
    hosts.import = [this, common = hosts.import](const finder::Hit& hit) {
        switch (finder::importKindOf(hit.key)) {
            case finder::ImportKind::Library:
            case finder::ImportKind::Theme:
                common(hit);
                if (finder::importKindOf(hit.key) == finder::ImportKind::Library) {
                    refreshHelpLibrary();
                    if (hit.key == "mac") macrosChanged();
                }
                if (auto* p = fileFinder(); p && status_) status_->setTransientMessage(p->message(), 8.0);
                return;
            default:
                finderImport(hit);
                return;
        }
    };
    hosts.help = [this] { openHmiHelp("chercher-fichiers"); };
    pane->setHosts(std::move(hosts));
    const auto tab = centre_->addTab(TabControl::Tab{"Chercher des fichiers", Icon::Search, /*closable=*/true, false}, std::move(made));
    hmiTabs_["chercher"] = pane;
    centre_->setCurrentIndex(tab);
}

void MainAnalysisScreen::finderImport(const finder::Hit& hit) {
    switch (finder::importKindOf(hit.key)) {
        case finder::ImportKind::HmiPackage:
            askHmiImport(hit.path);         // la fenetre Importer : ce qu'il apporte, les noms en conflit
            return;
        case finder::ImportKind::HmiTemplate:
            askHmiImportTemplate(hit.path);
            return;
        case finder::ImportKind::HmiLibrary:
            askHmiLinkLibrary(hit.path);
            return;
        case finder::ImportKind::PlcExport:
            filesDropped({hit.path});       // la question du depot : le MAST, la configuration
            return;
        case finder::ImportKind::Recording: {
            // Copie dans Ressources > Enregistrements (un nom pris devient "Nom (2)"), le volet montre le dossier.
            openHmiPane("enregistrements");
            auto* pane = dynamic_cast<HmiRecordingsPane*>(hmiTab("enregistrements"));
            std::string why;
            if (!pane) why = "le dossier des enregistrements n'est pas ouvert";
            else if (pane->importPath(hit.path, &why)) why.clear();
            else if (why.empty()) why = "copie impossible";
            const std::string said = why.empty() ? hit.name + " import\xC3\xA9 dans Ressources \xE2\x80\xBA Enregistrements."
                                                 : "Importer " + hit.name + " : " + why + ".";
            if (auto* finderPane = fileFinder()) finderPane->tell(said);
            if (status_) status_->setTransientMessage(said, 8.0);
            return;
        }
        default:
            if (status_) status_->setTransientMessage("Importer " + hit.name + " : rien \xC3\xA0 importer pour ce genre ici.", 6.0);
            return;
    }
}

void MainAnalysisScreen::tickFileFinder() {
    if (auto* pane = fileFinder()) pane->poll();
}

// ================================================================ depuis l'accueil ==
FileFinderScreen::FileFinderScreen(App& app) : menu::WidgetMenu("chercher"), app_(app) {}

core::Status FileFinderScreen::buildUi() {
    auto root = std::make_unique<ui::DockLayout>("chercher.root");
    auto bar = std::make_unique<ui::ToolBar>("chercher.barre");
    auto& back = bar->addButton("Retour", "chercher.retour", ui::Icon::Collapse);
    back.setTooltip("Revenir o\xC3\xB9 tu \xC3\xA9tais (\xC3\x89" "chap) ; les r\xC3\xA9sultats restent");
    links_ += back.clicked->connect([this] { app_.menus().PopMenu(); });
    bar->addSeparator();
    auto& help = bar->addButton("Aide", "chercher.aide", ui::Icon::Help);
    help.setTooltip("Chercher des fichiers sur ce PC : l'aide (F1)");
    links_ += help.clicked->connect([this] {
        app_.setHelpTopic("chercher-fichiers");
        app_.menus().PushMenu("help.hmi");
    });
    root->dock(std::move(bar), ui::DockLayout::Side::Top, 42.f);
    auto pane = std::make_unique<FileFinderPane>("chercher.volet");
    pane_ = pane.get();
    pane_->setChoices(finder::parseChoices(app_.settings().getString(std::string(finder::kSettingsKey))));
    pane_->setHosts(commonHosts(app_, pane_, false));
    root->dock(std::move(pane), ui::DockLayout::Side::Center, 0.f);
    setRoot(std::move(root));
    return core::ok();
}

ui::EventResult FileFinderScreen::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !k->repeat && k->mods.none()) {
        if (k->key == ui::Key::F1) {
            app_.setHelpTopic("chercher-fichiers");
            app_.menus().PushMenu("help.hmi");
            return ui::EventResult::Consumed;
        }
        // Echap : d'abord ce qui est ouvert (une liste, un menu, le champ), puis Retour.
        if (k->key == ui::Key::Escape) {
            if (menu::WidgetMenu::HandleEvent(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
            app_.menus().PopMenu();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void FileFinderScreen::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    if (pane_) pane_->poll();
}

} // namespace app
