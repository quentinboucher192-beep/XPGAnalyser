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
#include "../hmi/HmiIncidentsPane.hpp"            // 1.24.0 : Aller a un dossier d'incident
#include "../Dossiers.hpp"                        // 1.24.0 : ouvrir un dossier (Ouvrir dans l'explorateur)
#include "../FileViewer.hpp"                      // 1.24.0 : Aller a un fichier : l'apercu
#include "../../hmi/HmiAssets.hpp"                // 1.24.0 : le chemin d'un fichier externe
#include "../../hmi/HmiRecording.hpp"             // 1.24.0 : le dossier des enregistrements
#include "../../ui/widgets/PropertyGridHooks.hpp" // 1.24.0 : ui::paths (le fournisseur)
#include "../../menu/MenuManager.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/ThemeFile.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <cctype>
#include <cstdio>
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

// ============================================================ 1.24.0 ====
//  LES CHEMINS (le client, 05/11 : << a chaque fois qu'il y a un chemin vers un fichier / dossier je veux
//  pouvoir faire clic droit -> 'ouvrir dans l'explorateur' ou 'aller a' >>). Les widgets (une case de la
//  grille, une colonne d'un tableau) proposent les entrees ; l'ecran du projet est leur fournisseur
//  (ui::paths::Provider) :
//    - resoudre : un relatif part du dossier du projet ; "ressource:Nom" est le fichier de la ressource
//      (ihm/ressources/0012-Nom), "externe:Nom" celui du fichier externe ;
//    - Ouvrir dans l'explorateur : un fichier y est choisi, un dossier ouvert ;
//    - ALLER A, LA REGLE : un element du projet se montre dans l'application - le projet lui-meme, une vue
//      (son fichier ihm/vues/0003-...) et les dossiers Ressources, Historique, Enregistrements, Incidents :
//      leur noeud de l'arbre, revele et choisi ; une ressource, un fichier externe, un enregistrement (.xrec),
//      un dossier d'incident : leur volet, sur eux. Sinon un fichier s'ouvre dans l'apercu de fichiers de
//      l'application, un dossier dans Chercher des fichiers (sur ce dossier) ; ce qui n'existe pas : grise.
namespace {
namespace fs = std::filesystem;
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string u8text(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}
std::string normalized(const std::string& s) {
    std::string out = u8text(u8path(s).lexically_normal());
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}
// `path` sous `root` : la suite ("ihm/vues/0003-A.vue") ; "" : `root` lui-meme ; faux : ailleurs.
bool under(const std::string& root, const std::string& path, std::string& rest) {
    if (root.empty()) return false;
    const std::string r = normalized(root), p = normalized(path);
    if (p == r) {
        rest.clear();
        return true;
    }
    if (p.size() <= r.size() + 1 || p.compare(0, r.size(), r) != 0 || p[r.size()] != '/') return false;
    rest = p.substr(r.size() + 1);
    return true;
}
// "0012-logo.png" : 12 ; 0 : pas un nom range par identifiant.
std::uint64_t leadingId(const std::string& name) {
    std::uint64_t id = 0;
    std::size_t i = 0;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) id = id * 10 + static_cast<std::uint64_t>(name[i++] - '0');
    return i > 0 && i < name.size() && name[i] == '-' ? id : 0;
}
std::string shown(const std::string& path) {
    const auto slash = normalized(path).find_last_of('/');
    return slash == std::string::npos ? path : normalized(path).substr(slash + 1);
}
} // namespace

std::string MainAnalysisScreen::resolvePath(const std::string& path) const {
    if (path.empty()) return {};
    const std::string folder = app_.projectFolder();
    const auto doc = app_.hmi();
    if (path.rfind("ressource:", 0) == 0) {
        const auto* r = doc ? doc->project.resourceByName(path.substr(10)) : nullptr;
        if (!r || folder.empty()) return {};
        char head[16];
        std::snprintf(head, sizeof head, "%04u-", static_cast<unsigned>(r->id));
        std::error_code ec;
        const std::string dir = folder + "/ihm/ressources";
        for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = u8text(it->path().filename());
            if (name.rfind(head, 0) == 0) return dir + "/" + name;
        }
        return {};
    }
    if (path.rfind("externe:", 0) == 0) {
        const auto* f = doc ? doc->project.externalByName(path.substr(8)) : nullptr;
        return f ? hmi::resolveExternalPath(*f, folder) : std::string{};
    }
    const fs::path p = u8path(path);
    if (p.is_absolute() || folder.empty()) return path;
    return u8text((u8path(folder) / p).lexically_normal());
}

std::string MainAnalysisScreen::goToPath(const std::string& path, const std::string& resolved, bool dry) {
    using NK = ProjectTreeModel::NodeKind;
    const auto doc = app_.hmi();
    const std::string folder = app_.projectFolder();
    // Un noeud de l'arbre : revele (ses parents deplies) et choisi ; `open` : son volet aussi.
    const auto node = [&](ui::NodeId n, bool open) -> std::string {
        if (!explorer_ || !treeModel_ || n == ui::kInvalidNode) return "pas dans l'arbre du projet";
        if (dry) return {};
        if (revealTreeNode(n)) explorer_->setCurrentNode(n);
        if (open) openHmiNode(n);
        return {};
    };
    const auto resource = [&](const hmi::Resource* r) -> std::string {
        if (!r) return "cette ressource n'est plus dans le projet";
        if (dry) return {};
        const auto id = r->id;
        openHmiPane("ressources");
        if (auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"))) pane->selectResource(id);
        return {};
    };
    const auto external = [&](const hmi::ExternalFile* f) -> std::string {
        if (!f) return "ce fichier externe n'est plus dans le projet";
        if (dry) return {};
        const auto id = f->id;
        openHmiPane("fichiers");
        if (auto* pane = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"))) pane->selectFile(id);
        return {};
    };
    // ---- un element du projet, nomme ----
    if (path.rfind("ressource:", 0) == 0) return resource(doc ? doc->project.resourceByName(path.substr(10)) : nullptr);
    if (path.rfind("externe:", 0) == 0) return external(doc ? doc->project.externalByName(path.substr(8)) : nullptr);
    if (resolved.empty()) return "pas de fichier sur le disque (le projet n'est pas encore enregistr\xC3\xA9)";
    // ---- un element du projet, par son chemin ----
    std::string rest;
    if (under(folder, resolved, rest)) {
        if (rest.empty()) return node(treeModel_ ? treeModel_->root() : ui::kInvalidNode, false);   // le projet lui-meme
        if (doc) {
            std::string sub;
            // Les enregistrements et les incidents (sous ihm/ressources/enregistrements, ou leur dossier choisi).
            const std::string records = hmi::rec::folderOf(doc->project.recording, folder);
            if (under(records, resolved, sub)) {
                if (sub.empty()) return node(ProjectTreeModel::pack(NK::HmiRecordings, 0), true);
                if (sub == "en attente" || sub == "valides") return node(ProjectTreeModel::pack(NK::HmiIncidents, 0), true);
                if (sub.rfind("en attente/", 0) == 0 || sub.rfind("valides/", 0) == 0) {
                    if (dry) return {};
                    openHmiPane("incidents");
                    const std::string incident = sub.substr(sub.find('/') + 1);
                    if (auto* pane = dynamic_cast<HmiIncidentsPane*>(hmiTab("incidents"))) (void)pane->select(incident.substr(0, incident.find('/')));
                    return {};
                }
                if (sub.find('/') == std::string::npos && sub.size() > 5 && sub.ends_with(".xrec")) {
                    if (dry) return {};
                    openHmiPane("enregistrements");
                    if (auto* pane = dynamic_cast<HmiRecordingsPane*>(hmiTab("enregistrements")))
                        for (const auto& it : pane->items())
                            if (normalized(it.path) == normalized(resolved)) (void)pane->select(it.name);
                    return {};
                }
            }
            if (rest == "ihm/ressources") return node(ProjectTreeModel::pack(NK::HmiResources, 0), true);
            if (rest == "ihm/historique") return node(ProjectTreeModel::pack(NK::HmiHistory, 0), true);
            if (rest.rfind("ihm/ressources/", 0) == 0 && rest.find('/', 15) == std::string::npos)
                if (const auto id = leadingId(rest.substr(15)))
                    return resource(doc->project.resource(static_cast<hmi::Id>(id)));
            if (rest.rfind("ihm/vues/", 0) == 0 && rest.find('/', 9) == std::string::npos)
                if (const auto id = leadingId(rest.substr(9)); id && treeModel_)
                    return node(treeModel_->hmiViewNode(id), false);
        }
    }
    // ---- un fichier externe du projet (ou qu'il soit) ----
    if (doc)
        for (const auto& f : doc->project.assets.files)
            if (f.kind != hmi::ExternalKind::Database && normalized(hmi::resolveExternalPath(f, folder)) == normalized(resolved))
                return external(&f);
    // ---- ailleurs : l'apercu d'un fichier, la recherche dans un dossier ----
    std::error_code ec;
    const fs::path p = u8path(resolved);
    if (!fs::exists(p, ec)) return "\xC2\xAB " + shown(resolved) + " \xC2\xBB n'existe pas";
    if (fs::is_directory(p, ec)) {
        if (dry) return {};
        openFileFinder();
        if (auto* pane = fileFinder()) {
            pane->setWhere(finder::Where::Folder, resolved);
            pane->start();
        }
        return {};
    }
    if (dry) return {};
    return showPreviewWindow(app_.detachedWindows(), fileview::fromPath(resolved), true) ? std::string{}
                                                                                       : std::string("la fen\xC3\xAAtre d'aper\xC3\xA7u ne s'ouvre pas ici");
}

void MainAnalysisScreen::installPathLinks() {
    ui::paths::Provider p;
    p.resolve = [this](const std::string& path) { return resolvePath(path); };
    p.reveal = [this](const std::string& resolved) {
        std::error_code ec;
        std::string why;
        if (resolved.empty() || !fs::exists(u8path(resolved), ec)) why = "\xC2\xAB " + shown(resolved) + " \xC2\xBB n'existe pas";
        else if (fs::is_directory(u8path(resolved), ec)) why = dossiers::ouvrirDansLeSysteme(resolved);   // un dossier : ouvert
        else why = finder::revealInExplorer(resolved);                                                  // un fichier : choisi
        if (!why.empty() && status_) status_->setTransientMessage("Ouvrir dans l'explorateur : " + why, 6.0, ui::StatusBar::Severity::Warning);
        return why;
    };
    p.goTo = [this](const std::string& path, const std::string& resolved, bool dry) {
        const std::string why = goToPath(path, resolved, dry);
        if (!dry && !why.empty() && status_) status_->setTransientMessage("Aller \xC3\xA0 : " + why, 6.0, ui::StatusBar::Severity::Warning);
        return why;
    };
    ui::paths::setProvider(std::move(p));
}

} // namespace app
