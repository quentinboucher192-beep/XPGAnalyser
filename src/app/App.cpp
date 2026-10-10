#include "App.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : XPGAnalyser API, XPGAnalyser IHM
#include "Dossiers.hpp"
#include "EditionMigration.hpp"   // 1.12.0 : les projets de la 1.11, recopies dans projets\api et projets\ihm
#include "Epinglage.hpp"
#include "BackgroundTasks.hpp"          // Lot API 8 : bandeau haut (la cloche : l'export termine)
#include "screens/StationScreen.hpp"
#include "hmi/HmiSimulation.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"   // 1.12.2 : l'editeur de code qui a le focus (F12)
#include "Brand.hpp"
#include "HistoryPanel.hpp"
#include "ThemeGallery.hpp"
#include "VersionClose.hpp"
#include "hmi/HmiAskDialog.hpp"
#include "hmi/HmiImportDialog.hpp"   // 1.11.2 (decision 188) : un paquet lache ouvre la fenetre d import
#include "Capture.hpp"
#include "ClipboardFiles.hpp"
#include "ScriptRunner.hpp"
#include "tutorial/TutorialApp.hpp"         // 1.11 (T1) : les tutoriels, branches
#include "ExportTarget.hpp"                 // Lot API 8 : les clics d'un navigateur n'exportent pas en demandant ou
#include "DropFilesDialog.hpp"              // ---- Lot API 8 : glisser de fichiers, 2e partie (le dialogue ouvert prend ce qu'on lache) ----
#include "../ui/widgets/FilterMemory.hpp"   // Lot API 8 : les filtres retenus d'une seance a l'autre
#include "../ui/widgets/ColorPalette.hpp"   // 1.10 (chantier Q) : la pipette (le curseur en croix)
#include "../ui/ThemeFile.hpp"              // Lot API 8 : themes (ceux de l'utilisateur, relus au demarrage)
#include "FileExplorerHost.hpp"             // ---- Lot API 8 : l'explorateur de fichiers de l'appli ----
#include "screens/LibraryHelpScreen.hpp"
#include "../ui/widgets/PropertyGridHooks.hpp"   // integration I111, lien 5
#include "screens/HelpCenterScreen.hpp"   // 1.11 (T2) : le centre d'aide unique
#include "screens/HmiExprScreen.hpp"     // 1.11 (chantier T3, D5) : la page des expressions, seule
#include "screens/Screens.hpp"
#include "../project/BlockLibrary.hpp"
#include "AboutDialog.hpp"                    // 1.10 (R2) : A propos d'XPGAnalyser
#include "../project/GrafcetEdit.hpp"
#include "../project/GrafcetRewrite.hpp"
#include "../project/GrafcetRewrite2.hpp"   // 1.10 (R2) : Ctrl+Z des commandes du grafcet
#include "../export/XpgWriter.hpp"          // Vers Control Expert : un autre dossier que src/
#include "../project/ProjectIcon.hpp"
#include "../hmi/HmiVersions.hpp"
#include "../hmi/HmiNetInfo.hpp"
#include "SimDebugPane.hpp"            // Lot API 8 : F11 dans Simulation > Debogage
#include "../hmi/HmiStore.hpp"
#include "NoveltyCenter.hpp"              // 1.10 (chantier P) : les nouveautes se voient
#include "../core/CrashGuard.hpp"           // 1.10.2 (CR) : le chien de garde bat a chaque image
#include "../core/CallTrail.hpp"            // 1.10.2 (CR) : le journal interne (XPG_TRACE, XPG_PORTEE)
#include "CrashDialogs.hpp"                 // 1.10.2 (CR) : Aide > Journal interne
#include "../core/SingleInstance.hpp"       // 1.11.2 (UNI) : une seule instance

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>                  // ---- Lot API 8 : les filtres retenus d'une session rejouee ----
#include <system_error>
#include <tuple>

#include "../ui/Widget.hpp"
#include "../help/Shortcuts.hpp"     // 1.11 (T2) : les touches viennent de la table unique

#include <algorithm>
#include <chrono>
#include <vector>
#include <cstdio>
#include <ctime>

// ---------------------------------------------------------------------------
//  SDL3 is included only here and in platform/. Nothing above this line in the
//  dependency graph knows the windowing system exists.
//
//  When XPG_WITH_SDL is defined by the build (the .vcxproj and the CMake target
//  both define it), the include below is UNCONDITIONAL. A wrong include path is
//  then a compile error naming the missing file, at the moment it happens.
//
//  An earlier revision probed with __has_include() alone. That was a mistake:
//  a wrong include path produced a program that compiled, linked and only
//  complained at startup. Silent fallbacks turn a build problem into a runtime
//  mystery; when SDL cannot be found the build should say so.
// ---------------------------------------------------------------------------
#if !defined(XPG_WITH_SDL)
#  if __has_include(<SDL3/SDL.h>)
#    define XPG_WITH_SDL 1
#  else
#    define XPG_WITH_SDL 0
#    pragma message("XPG_WITH_SDL is not defined and <SDL3/SDL.h> is not on the " \
                    "include path: building a console-only binary. The graphical " \
                    "front end will refuse to start. Define XPG_WITH_SDL=1 and add " \
                    "the SDL3 include directory to fix this.")
#  endif
#endif

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#  define XPG_HAVE_SDL3 1
#else
#  define XPG_HAVE_SDL3 0
#endif

#if XPG_HAVE_SDL3
#include <mutex>
namespace app::filepick {
// =============================================================== lot macros 1 ==
//  L'EXPLORATEUR DE FICHIERS. SDL rappelle, peut-etre sur un autre fil ; la
//  reponse est rangee sous un verrou et rendue par App::frame, sur le fil de
//  l'interface - le seul ou un widget peut etre touche. Les filtres vivent dans
//  la demande jusqu'a ce que SDL ait rappele : il les lit jusque-la.
struct Request {
    std::function<void(std::string)> done;
    std::vector<std::string>          names, patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string                       start;
};
namespace {
std::mutex                                  g_mutex;
std::vector<std::pair<Request*, std::string>> g_done;
} // namespace

void SDLCALL onPicked(void* userdata, const char* const* files, int /*filter*/) {
    std::string chosen;
    if (files && files[0]) chosen = files[0];
    std::lock_guard<std::mutex> lock(g_mutex);
    g_done.emplace_back(static_cast<Request*>(userdata), std::move(chosen));
}

void deliver() {
    std::vector<std::pair<Request*, std::string>> ready;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ready.swap(g_done);
    }
    for (auto& [request, path] : ready) {
        std::unique_ptr<Request> owned(request);
        if (owned->done) owned->done(std::move(path));
    }
}
} // namespace app::filepick
#endif

#if XPG_HAVE_SDL3
namespace app {
// Defined in platform/SdlEventPump.cpp. Declared here, after <SDL3/SDL.h>, so
// SDL_Event is the real union and not a namespace-local incomplete type.
std::optional<ui::InputEvent> translate(const SDL_Event& e);
}
#endif

namespace app {

namespace {
// LOT 19 : LES LIBELLES DE L'AUTOMATE, EN FRANCAIS DANS L'HISTORIQUE. Les
// commandes du programme se nomment en anglais ("add variable 'X'") : les
// tests les lisent ainsi, on ne les change pas ; l'historique les traduit.
std::string frenchLabel(const std::string& en, const std::string& place) {
    const auto quoted = [&](std::size_t from) {
        std::string name = en.substr(from);
        if (!name.empty() && name.front() == '\'') name.erase(0, 1);
        if (!name.empty() && name.back() == '\'') name.pop_back();
        return name;
    };
    const auto starts = [&](const char* p) { return en.rfind(p, 0) == 0; };
    const auto lastSegment = [&] {
        const std::string sep = "\xE2\x80\xBA ";
        const auto at = place.rfind(sep);
        return at == std::string::npos ? std::string() : place.substr(at + sep.size());
    };
    if (starts("add subroutine ")) return "Ajouter la sous-routine " + quoted(15);
    if (starts("add section ")) return "Ajouter la section " + quoted(12);
    if (starts("add program unit ")) return "Ajouter l'unit\xC3\xA9 de programme " + quoted(17);
    if (starts("add DFB type ")) return "Ajouter le type DFB " + quoted(13);
    if (starts("add derived type ")) return "Ajouter le type d\xC3\xA9riv\xC3\xA9 " + quoted(17);
    if (starts("add variable ")) return "Ajouter la variable " + quoted(13);
    if (en == "edit section") {
        const auto name = lastSegment();
        return place.find("Sections") != std::string::npos && !name.empty() ? "Modifier la section " + name : "Modifier une section";
    }
    if (en == "clear section") return "Vider la section";
    if (starts("add rack ")) return "Ajouter le rack " + en.substr(9);
    if (starts("add ") && en.find(" in rack ") != std::string::npos) {
        const auto at = en.find(" in rack ");
        std::string where = en.substr(at + 9);
        if (const auto slot = where.find(" slot "); slot != std::string::npos)
            where = where.substr(0, slot) + ", emplacement " + where.substr(slot + 6);
        return "Ajouter " + en.substr(4, at - 4) + " (rack " + where + ")";
    }
    if (starts("deplacer ") || starts("monter ") || starts("descendre ")) {
        std::string t = en;
        t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
        if (const auto at = t.find("d'execution"); at != std::string::npos) t.replace(at, 11, "d'ex\xC3\xA9" "cution");
        if (t.rfind("Deplacer", 0) == 0) t.replace(0, 8, "D\xC3\xA9placer");
        return t;
    }
    if (starts("delete step X")) return "Supprimer l'\xC3\xA9tape X" + en.substr(13);
    if (starts("delete ")) return "Supprimer " + en.substr(7);
    if (starts("edit condition of T")) return "Modifier la condition de T" + en.substr(19);
    if (starts("edit body of A")) return "Modifier l'action A" + en.substr(14);
    if (starts("insert transition T")) return "Ins\xC3\xA9rer la transition T" + en.substr(19);
    if (starts("insert action A")) return "Ins\xC3\xA9rer l'action A" + en.substr(15);
    if (starts("insert step X")) return "Ins\xC3\xA9rer l'\xC3\xA9tape X" + en.substr(13);
    if (starts("re-point transition T")) return "Relier la transition T" + en.substr(21);
    if (starts("rename step X")) return "Renommer l'\xC3\xA9tape X" + en.substr(13);
    if (starts("append ")) return "Ajouter la ligne " + en.substr(7);
    if (const auto at = en.find(" etape(s))"); at != std::string::npos)
        return "Macro " + en.substr(0, at) + " \xC3\xA9tape(s))";
    return en;
}
} // namespace

core::Result<std::unique_ptr<App>> App::create(AppOptions options) {
    std::unique_ptr<App> a(new App());
    a->options_ = std::move(options);
    // 1.12.0 : la fenetre porte le nom de son application (XPGAnalyser API, XPGAnalyser IHM).
    if (!core::editionLabel().empty() && a->options_.title == AppOptions{}.title) a->options_.title = core::productName();

    // Settings are loaded before anything is built, so the first frame already
    // shows the workspace the user left behind rather than the default one.
    // 1.10 (chantier P) : des reglages existaient deja (un profil d'une version d'avant).
    const bool hadSettings = a->settings_.load(Settings::defaultPath());
    a->recent_ = a->settings_.getList("recent.projects");
    // 1.12.2 : le profil des raccourcis des editeurs de code (Visual Studio par defaut).
    if (const auto profile = ui::keymap::profileFromKey(a->settings_.getString("editeur.raccourcis", "vs")))
        ui::keymap::setCurrent(*profile);
    // 1.12.0 : au premier lancement de chaque application, ses projets de la 1.11
    // (projets\<Nom>, l'automate et l'IHM ensemble) recopies dans son rangement -
    // sa moitie ; les originaux restent ; les projets recents suivent les copies.
    if (core::edition() != core::Edition::Both) {
        const auto root = a->projectsRoot();
        const auto report = edition::migrateLegacyProjects(root.parent_path(), root, core::edition());
        if (report.ran && !report.copied.empty()) {
            edition::remapRecent(a->recent_, report);
            a->settings_.setList("recent.projects", a->recent_);
            const auto n = report.copied.size();
            bgtasks::post({"edition:migration", "Projets",
                           std::to_string(n) + (n > 1 ? " projets de la 1.11 recopi\xC3\xA9s" : " projet de la 1.11 recopi\xC3\xA9") + " dans "
                               + dossiers::utf8De(root),
                           core::hasApi() ? std::string("Leur programme, sans leur IHM (elle est dans XPGAnalyser IHM). Les originaux restent dans le dossier parent.")
                                          : std::string("Leur IHM, sans le programme de l'automate : les noms de l'automate qu'elle lisait sont devenus "
                                                        "ses variables IHM (dossier Automate), l'automate reli\xC3\xA9 par Modbus TCP l'\xC3\xA9quipement \xC2\xAB Automate \xC2\xBB. "
                                                        "Les originaux restent dans le dossier parent."),
                           "Ouvrir le dossier", "open.folder:" + dossiers::utf8De(root), "info"});
        }
        for (const auto& problem : report.problems) std::fprintf(stderr, "projets de la 1.11 : %s\n", problem.c_str());
    }
    a->masterKey_.loadStored(a->settings_.getString("security.masterKey"));
    // ---- Lot API 8 : themes ----
    // Les themes de l'utilisateur (un .xpgtheme chacun, dans "themes/" a cote
    // de settings.txt) : relus AVANT d'appliquer le theme retenu, qui peut en
    // etre un. Un fichier abime n'empeche rien : il est dit, les autres chargent.
    {
        const auto themesDir = std::filesystem::path(Settings::sharedFolder()) / "themes";   // 1.12.0 : communs aux deux applications
        const auto u8 = themesDir.u8string();
        ui::UserThemes::setFolder(std::string(u8.begin(), u8.end()));
        for (const auto& problem : ui::UserThemes::load()) std::fprintf(stderr, "theme : %s\n", problem.c_str());
    }
    // ---- fin Lot API 8 ----
    a->options_.themeName = a->settings_.getString("view.theme", a->options_.themeName);
    a->setTheme(a->options_.themeName);
    if (auto r = a->initPlatform(a->options_); !r) return core::Err<core::Error>(r.error());
#if XPG_HAVE_SDL3
    // 1.11.2 (UNI) : une demande d'une autre instance reveille la boucle (le fil
    // d'ecoute pose un evenement ; App::takeInstanceRequests la prend a l'image).
    if (auto* instance = core::instance::current()) {
        const Uint32 wakeType = SDL_RegisterEvents(1);
        instance->setWake([wakeType] {
            if (wakeType == 0) return;
            SDL_Event e{};
            e.type = wakeType;
            (void)SDL_PushEvent(&e);
        });
    }
#endif

    a->importer_ = std::make_unique<importer::ProjectImporter>(a->bus_);
    // The factory is fully populated before the manager is constructed:
    // MenuManager is neither copyable nor assignable by design, so it must be
    // built once with its final factory rather than reassigned afterwards.
    a->menus_      = std::make_unique<menu::MenuManager>(a->buildMenuFactory());
    a->menuGlobal_ = std::make_unique<menu::MenuManager::ScopedInstance>(*a->menus_);

    a->registerActions();
    a->installFilterMemory();      // Lot API 8 : les filtres retenus d'une seance a l'autre (avant tout ecran)

    // The block library, from resources/. Searched next to the executable and
    // next to the working directory; a miss leaves the built-in seed in place
    // rather than leaving the editor with no suggestions at all.
    if (auto library = project::BlockLibrary::loadFromFile("resources/schneider_library.txt"))
        project::BlockLibrary::installShared(std::move(*library));

    // A finished import becomes the current project on the UI thread, at a frame
    // boundary, so no view ever observes a half-built model.
    App* self = a.get();
    // LOT 19 : L'HISTORIQUE. Une rafale de frappe (ou de fleches) ne fait
    // qu'un pas tant qu'on ne s'arrete pas plus de 2,5 s ; et chaque pas note
    // ou il a ete fait (l'ecran fournit l'endroit, voir setPlaceProvider).
    a->commands_.setMergeWindow(std::chrono::milliseconds(2500));
    // Lot 20 : les volets ouvrent leurs groupes (coller, importer) sur cette pile.
    core::CommandGroupScope::registerStack(&a->commands_);
    a->commands_.setDescriber([self](const core::ICommand& whole, core::CommandInfo& info) {
        // Lot 20 : un groupe (un collage, un import) se decrit par sa premiere
        // commande - son cote, son endroit - et garde son libelle a lui.
        const auto* group = dynamic_cast<const core::GroupCommand*>(&whole);
        const core::ICommand& c = group && !group->empty() ? *group->parts().front() : whole;
        const bool ihm = dynamic_cast<const hmi::ViewCommand*>(&c) != nullptr
                      || dynamic_cast<const hmi::ProjectCommand*>(&c) != nullptr;
        info.area = ihm ? 2 : 1;
        if (self->placeProvider_) self->placeProvider_(info);
        // Une commande de vue dit sa vue, meme lancee d'ailleurs (un dialogue
        // ouvert depuis l'arbre pendant qu'un autre onglet est devant).
        if (const auto* vc = dynamic_cast<const hmi::ViewCommand*>(&c); vc && self->hmi_) {
            // "Deplacer" dit QUOI : l'objet (ou "3 objets"), si le libelle ne le dit pas deja.
            if (const auto what = vc->objectsSummary(); !group && !what.empty() && info.label.find(what) == std::string::npos
                && info.label.find("objet") == std::string::npos)
                info.label += " " + what;
            const std::string key = "hmi:vue:" + std::to_string(vc->viewId());
            if (info.placeKey != key)
                if (const auto* v = self->hmi_->project.view(vc->viewId())) {
                    info.placeKey = key;
                    info.place = "IHM \xE2\x80\xBA Vues \xE2\x80\xBA " + v->name;
                }
        }
        if (info.place.empty()) info.place = ihm ? "IHM" : "API";
        if (!ihm) info.label = frenchLabel(info.label, info.place);
    });
    a->subscriptions_ += a->bus_.subscribe<importer::ImportFinished>(
        [self](const importer::ImportFinished& e) {
            self->document_ = std::const_pointer_cast<domain::Project>(e.project);
            self->project_  = e.project;
            self->report_   = e.report;
            self->commands_.clear();
            self->openedAtMs_ = core::CommandStack::wallMs();
            self->savedAtMs_  = 0;

            // The previous simulation belongs to the previous project. Now that
            // App holds a reference of its own, the runtime no longer dies with
            // the simulation screen - which is the point, but it also means a
            // stale one would happily answer hovers with values measured on a
            // program that is no longer open. Wrong values are worse than none.
            self->simulation_.detach();
            self->setSimulationStatus({});

            self->rememberPath(e.path);
            if (self->sources_.empty()) self->sources_.push_back(e.path);
            // L'IHM avant ProjectOpened : les ecrans qui se relient au projet
            // trouvent la sienne deja la.
            self->bindHmi();
            self->bus_.publish(ProjectOpened{e.project, e.report});
            self->menus().SwitchMenu(self->stationPending_ ? "station" : "analysis");
            if (self->stationPending_) {
                self->stationPending_ = false;
                self->stationActive_ = true;
            }
            // APRES SwitchMenu : les deux sont mis en file et appliques dans
            // l'ordre, et SwitchMenu vide la pile - le dialogue demande avant
            // disparaissait avec elle, sans avoir jamais ete vu.
            self->offerProjectFolder(e.path);
        });
    // Lot API 7 : chaque projet republie (une macro, un collage, une mise a jour
    // de bibliotheque, Ctrl+Z) change le programme que la simulation a prepare.
    a->subscriptions_ += a->bus_.subscribe<ProjectOpened>([self = a.get()](const ProjectOpened&) { ++self->programChanges_; });
    a->subscriptions_ += a->bus_.subscribe<importer::ImportFailed>(
        [self](const importer::ImportFailed& e) {
            self->menus().ShowDialog(std::make_unique<MessageDialog>(
                                         "Import failed", e.message, MessageDialog::Icon::Error),
                                     [](const menu::DialogResult&) {});
        });

    a->menus_->SwitchMenu("startup");
    a->promptMasterKeyIfNeeded();
    // 1.8.0 : l'icone sur la barre des taches, cochee a l'installation (installation.ini) :
    // demandee une fois par compte, quand l'accueil est a l'ecran sans dialogue.
    {
        const auto& d = dossiers::actuel();
        a->taskbarPinPending_ = d.installe && d.epinglerBarre && a->settings_.getInt("taskbar.pinAsked", 0) == 0;
    }
    // Lot 15 : la reprise - le verrou de cette session, et une session d'avant
    // arretee brutalement (le PC eteint, le logiciel plante).
    a->recovery_.start((std::filesystem::path(Settings::defaultPath()).parent_path() / "reprise").string());
    // La periode de la sauvegarde de reprise (2 min ; un reglage pour les essais).
    if (const int period = a->settings_.getInt("recovery.autosaveSeconds", 120); period >= 5)
        a->recovery_.setAutosavePeriod(period);
    if (const auto& crashed = a->recovery_.crashed(); crashed && crashed->hasData()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const bool sameStation = !a->options_.station.empty() && !crashed->stationState.empty()
                                 && fs::weakly_canonical(a->options_.station, ec) == fs::weakly_canonical(crashed->project, ec);
        if (sameStation) {
            // Le poste relance sans personne (demarrage automatique) : la reprise
            // se propose au demarrage du poste, avec son compte a rebours.
            if (auto found = a->recovery_.take()) {
                a->stationRestore_ = Recovery::readFile(found->stationState);
                a->stationRestoreAsk_ = true;
            }
        } else {
            a->offerRecovery();
        }
    }
    // Lot 14 : --ihm <dossier> - le poste d'exploitation, des que le projet est ouvert.
    if (!a->options_.station.empty()) {
        // Lot 15 : --ihm (le lanceur, le demarrage avec le PC) - un projet FINISH.
        a->openStation(a->options_.station, a->stationRestore_.has_value());
    } else if (!a->options_.openOnStart.empty()) {
        a->openPath(a->options_.openOnStart);
    }
    // 1.11 (T1) : les tutoriels inscrits, le lanceur de help::startTutorial branche.
    tutorials::install(*a);
    if (!a->options_.script.empty()) {
        auto script = ScriptRunner::fromFile(*a, a->options_.script, a->options_.capturesDir);
        if (!script) return core::Err<core::Error>(script.error());
        a->script_ = std::move(*script);
    }
    // 1.10 (chantier P) : la version lancee, ce que l'utilisateur a deja vu ;
    // la fenetre des nouveautes attend l'accueil (NoveltyCenter::tick).
    noveltyCenter().start(*a, hadSettings);
    return a;
}

App::~App() {
    if (auto* instance = core::instance::current()) instance->setWake({});   // 1.11.2 (UNI) : avant SDL_Quit
    tutorials::shutdown();   // 1.11 (T1) : la session de tutoriel tient l'appli (UiDriver)
    // Lot API 7 : les onglets detaches rentrent D'ABORD - leur ecran (qui les
    // remet en onglets) est encore la, et leurs fenetres partent avant SDL_Quit.
    detached_.giveBackAll();
    // Lot 20 : la pile qui s'en va n'accueille plus de groupe.
    if (core::CommandGroupScope::stack() == &commands_) core::CommandGroupScope::registerStack(nullptr);
    // Lot 14 : les fenetres des ecrans secondaires, avant la fenetre principale ;
    // la liaison Modbus et le serveur de demonstration (leurs fils).
    closeScreenWindows();
    comm_.shutdown();
    equip_.shutdown();      // lot 15
    recovery_.stop();       // lot 15 : une sortie normale - le verrou s'efface
    notify_.shutdown();
    web_.shutdown();
    // Persist on the way out: the workspace a user arranged should survive
    // closing the program, including when they close it with the window button.
    settings_.setList("recent.projects", recent_);
    settings_.set("view.theme", theme_.name);
#if XPG_HAVE_SDL3
    // Lot 8 : l'etat de la fenetre, repris au prochain lancement (pas pour une
    // session rejouee, qui impose sa taille).
    if (window_ && !options_.sizeGiven) {
        const SDL_WindowFlags f = SDL_GetWindowFlags(window_);
        settings_.set("window.maximized", (f & SDL_WINDOW_MAXIMIZED) != 0);
        if ((f & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) == 0) {
            int w = 0, h = 0;
            if (SDL_GetWindowSize(window_, &w, &h) && w > 0 && h > 0) {
                settings_.set("window.width", w);
                settings_.set("window.height", h);
            }
        }
    }
#endif
    settings_.save();

#if XPG_HAVE_SDL3
    menus_.reset();          // menus release widgets, widgets release textures...
    renderer_.reset();       // ...before the renderer that owns the GPU objects...
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();              // ...and only then the subsystem itself.
#endif
}

core::Status App::initPlatform(const AppOptions& o) {
#if XPG_HAVE_SDL3
    if (!SDL_Init(SDL_INIT_VIDEO))
        return core::fail(core::ErrorCode::SdlInit, SDL_GetError());

    // LOT 8 : AGRANDIE AU LANCEMENT. Une fenetre de 1536 x 1024 au milieu d'un
    // ecran de 1920 x 1080, qu'il faut agrandir a la main a chaque ouverture,
    // c'est ce qu'on remarque en premier. --size (les sessions rejouees) garde
    // la taille exacte ; sinon la derniere taille "normale" est reprise, pour
    // le jour ou l'on reduit la fenetre.
    int width = o.width, height = o.height;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!o.sizeGiven) {
        width = std::max(640, static_cast<int>(settings_.getInt("window.width", width)));
        height = std::max(480, static_cast<int>(settings_.getInt("window.height", height)));
        if (settings_.getBool("window.maximized", true)) flags |= SDL_WINDOW_MAXIMIZED;
    }
#if defined(_WIN32)
    // Lot 12 : sous Windows, la classe de fenetre prend l'icone de l'executable
    // (xpg_analyzer.rc, ressource 1) - la barre des taches l'a des l'ouverture.
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    window_ = SDL_CreateWindow(o.title.c_str(), width, height, flags);
    if (!window_) return core::fail(core::ErrorCode::SdlInit, SDL_GetError());
    // Lot 12 : LE LOGO DANS LA BARRE DE TITRE ET LA BARRE DES TACHES, partout
    // (Windows, Linux, macOS) : rasterise depuis le SVG embarque, a plusieurs
    // tailles - le systeme prend la plus proche de ce qu'il affiche. Les pixels
    // doivent vivre jusqu'a SDL_SetWindowIcon, qui les copie.
    {
        std::vector<hmi::Rgba> sizes;
        for (const int side : {256, 16, 24, 32, 48, 64, 128}) {
            hmi::Rgba img;
            if (brand::logoPixels(side, img)) sizes.push_back(std::move(img));
        }
        if (!sizes.empty()) {
            auto& big = sizes.front();
            if (SDL_Surface* icon = SDL_CreateSurfaceFrom(big.width, big.height, SDL_PIXELFORMAT_RGBA32, big.pixels.data(), big.width * 4)) {
                for (std::size_t i = 1; i < sizes.size(); ++i) {
                    auto& img = sizes[i];
                    if (SDL_Surface* alt = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.pixels.data(), img.width * 4)) {
                        (void)SDL_AddSurfaceAlternateImage(icon, alt);
                        SDL_DestroySurface(alt);         // l'icone en garde une reference
                    }
                }
                (void)SDL_SetWindowIcon(window_, icon);
                SDL_DestroySurface(icon);
            }
        }
    }
    // Le drapeau a la creation ne suffit pas partout : on le redemande.
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0) SDL_MaximizeWindow(window_);

    auto r = gfx::SdlRenderer::create(window_);
    if (!r) return core::Err<core::Error>(r.error());
    renderer_ = std::move(*r);

    // Without this, SDL never sends SDL_EVENT_TEXT_INPUT and every text field
    // in the application silently swallows keystrokes. It is not implied by
    // SDL_Init: SDL3 requires text input to be started per window.
    SDL_StartTextInput(window_);

    // Hand the widget layer the two things it needs from the platform but must
    // not link against: text measurement, so sizeHint() stops guessing, and the
    // clipboard, so Ctrl+C/X/V work in every InputText.
    ui::PlatformServices services;
    services.measureWidth = [r = renderer_.get()](std::string_view text, gfx::FontId font) {
        return r->measure(text, font).width;
    };
    services.lineHeight = [r = renderer_.get()](gfx::FontId font) { return r->lineHeight(font); };
    services.clipboardGet = [] {
        char* text = SDL_GetClipboardText();      // SDL owns this
        std::string out = text ? text : "";
        SDL_free(text);
        return out;
    };
    services.clipboardSet = [](std::string_view text) { SDL_SetClipboardText(std::string(text).c_str()); };
    services.surfaceSize = [r = renderer_.get()] { return r->surfaceSize(); };
    // LOT MACROS 1 : L'EXPLORATEUR DE FICHIERS DU SYSTEME (le bouton ... des
    // champs de fichier), par SDL3. La reponse peut arriver sur un autre fil :
    // elle est rangee, et App::frame la rend sur celui de l'interface.
    services.pickFile = [this](const ui::FilePick& pick, std::function<void(std::string)> done) {
        // ---- Lot API 8 : l'explorateur de fichiers de l'appli (le reglage "explorateur.systeme",
        // faux par defaut ; une session rejouee garde celui du systeme, sauf apres explorateur-montrer) ----
        if (menus_ && explorer::wantsAppExplorer(*this)) return explorer::open(*this, pick, std::move(done));
        // ---- fin Lot API 8 : l'explorateur de fichiers ----
        auto request = std::make_unique<filepick::Request>();
        request->done = std::move(done);
        request->start = pick.start;
        for (const auto& [name, pattern] : pick.filters) {
            request->names.push_back(name);
            request->patterns.push_back(pattern);
        }
        for (std::size_t i = 0; i < request->names.size(); ++i)
            request->filters.push_back(SDL_DialogFileFilter{request->names[i].c_str(), request->patterns[i].c_str()});
        auto* raw = request.release();
        // Le bouton ... des champs de chemin (ui/widgets/PathBrowse.hpp) : un
        // fichier a ouvrir, a enregistrer, ou un DOSSIER ; et le titre de la
        // fenetre (ce qu'on choisit). Par les proprietes : SDL les lit pendant
        // l'appel (SDL_ShowOpenFileDialog fait de meme) ; les filtres, eux,
        // vivent dans la demande jusqu'a la reponse.
        const SDL_FileDialogType type = pick.folder ? SDL_FILEDIALOG_OPENFOLDER
                                      : pick.save   ? SDL_FILEDIALOG_SAVEFILE
                                                    : SDL_FILEDIALOG_OPENFILE;
        const SDL_PropertiesID props = SDL_CreateProperties();
        // ---- Lot API 8 : au-dessus de la fenetre ou l'on travaille (le bouton ...
        // d'un dialogue ou d'une page dans une fenetre detachee), sinon la principale.
        SDL_Window* parent = menus_ ? detached_.systemWindow(menus_->currentWindow()) : nullptr;
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent ? parent : window_);
        if (!pick.folder && !raw->filters.empty()) {
            SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, raw->filters.data());
            SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(raw->filters.size()));
        }
        if (!raw->start.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, raw->start.c_str());
        if (!pick.title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, pick.title.c_str());
        SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
        SDL_ShowFileDialogWithProperties(type, &filepick::onPicked, raw, props);
        SDL_DestroyProperties(props);
        return true;
    };
    // Ctrl+C sur un fichier dans l'Explorateur : une liste de fichiers, pas du texte.
    services.clipboardFiles = [] {
#if defined(_WIN32)
        return win32ClipboardFiles();
#else
        std::vector<std::string> out;
        if (SDL_HasClipboardData("text/uri-list")) {
            std::size_t size = 0;
            if (void* data = SDL_GetClipboardData("text/uri-list", &size)) {
                out = parseUriList(std::string_view(static_cast<const char*>(data), size));
                SDL_free(data);
            }
        }
        return out;
#endif
    };
    ui::installPlatformServices(std::move(services));
    return core::ok();
#else
    (void)o;
    // Reaching this means XPG_WITH_SDL was 0 at compile time for THIS file.
    // Check, in order: (1) src/platform/SdlRenderer.cpp and SdlEventPump.cpp are
    // in the project, (2) XPG_WITH_SDL=1 is in the preprocessor definitions,
    // (3) the SDL3 include directory is the folder that CONTAINS the SDL3
    // sub-folder, not the sub-folder itself. --cli still works in this build.
    return core::fail(core::ErrorCode::SdlInit,
                      "this binary was compiled without SDL3 support: App.cpp saw "
                      "XPG_WITH_SDL=0. Add XPG_WITH_SDL=1 to the preprocessor "
                      "definitions and put the SDL3 include directory on the "
                      "include path, then rebuild. '--cli <file>' works without SDL.");
#endif
}

menu::MenuFactory App::buildMenuFactory() {
    menu::MenuFactory f;
    f.add("startup",                 [this] { return menu::MenuPtr(std::make_unique<StartupScreen>(*this)); });
    f.add("analysis",                [this] { return menu::MenuPtr(std::make_unique<MainAnalysisScreen>(*this)); });
    // Lot API 7 : plus d'ecrans pleins pour la simulation (F9), l'explorateur de
    // variables (Ctrl+1) ni les statistiques (Ctrl+5) - des onglets de l'API
    // (OpenApiTab, ouvert par l'ecran d'analyse).
    f.add("analysis.libraries",      [this] { return menu::MenuPtr(std::make_unique<LibraryExplorerScreen>(*this)); });
    f.add("analysis.programs",       [this] { return menu::MenuPtr(std::make_unique<ProgramUnitsScreen>(*this)); });
    // 1.11 (chantier T2) : LE CENTRE D'AIDE UNIQUE. Les quatre entrees d'avant
    // (l'aide generale, les macros, les blocs, l'IHM) ouvrent le meme ecran, sur
    // leur chapitre ; F1 l'ouvre sur le sujet de l'endroit. Les anciens ecrans
    // restent compiles jusqu'au retrait (leurs pieces servent au centre).
    f.add("help",                    [this] { return menu::MenuPtr(std::make_unique<HelpCenterScreen>(
                                          *this, "help", help::center::Chapter::Start)); });
    // Lot 14 : le poste d'exploitation (l'IHM seule, en plein ecran).
    f.add("station",                 [this] { return menu::MenuPtr(std::make_unique<StationScreen>(*this)); });
    // L'AIDE DE LA BIBLIOTHEQUE N'ETAIT ENREGISTREE NULLE PART.
    //
    // L'ecran existait - la page, ses trois onglets, son editeur, ses modeles,
    // ses tests - et aucun chemin n'y menait : personne ne pouvait l'ouvrir.
    // C'est exactement ce que dit le commentaire de "help.open" quelques lignes
    // plus bas, et ca s'etait reproduit un etage en dessous.
    //
    // Lot macros 1 : deux onglets de l'aide au lieu d'un ecran pour tout -
    // les macros (rangees dans les dossiers de l'onglet Macros) et les blocs
    // DFB / DDT (les dossiers de libs/).
    f.add("help.macros",             [this] { return menu::MenuPtr(std::make_unique<HelpCenterScreen>(
                                          *this, "help.macros", help::center::Chapter::Macros)); });
    f.add("help.blocs",              [this] { return menu::MenuPtr(std::make_unique<HelpCenterScreen>(
                                          *this, "help.blocs", help::center::Chapter::Blocks)); });
    // Lot 8 : l'aide de l'IHM (objets et exemples animes), depuis l'aide generale.
    f.add("help.hmi",                [this] { return menu::MenuPtr(std::make_unique<HelpCenterScreen>(
                                          *this, "help.hmi", help::center::Chapter::Hmi)); });
    // 1.11 (chantier T3, D5) : la page des expressions (les 11 types, le champ d'essai), seule
    // tant que le centre d'aide de T2 ne l'accroche pas (alors : le centre sur expr-bool).
    f.add("help.expressions",        [this] { return menu::MenuPtr(std::make_unique<HmiExprScreen>(*this)); });
    // Integration I111, lien 5 : "La page des expressions..." (clic droit sur une case a expression,
    // ui::PropertyGrid) ouvre la page de T3 sur le type de la case : le tutoriel de l'expression
    // (T1, etape 7) y mene sur le type couleur, ou il encadre le bouton du type ("#   Couleur").
    ui::propertygrid::setExpressionPageHook([this](const ui::PropertyGrid::Property& p) {
        using VT = ui::PropertyGrid::ValueType;
        const char* key = p.type == VT::Color     ? "couleur"
                        : p.type == VT::Boolean   ? "bool"
                        : p.type == VT::Integer   ? "entier"
                        : p.type == VT::Real      ? "reel"
                        : p.type == VT::Enum      ? "enumeration"
                                                  : "texte";
        menus_->PushMenu(std::make_unique<HmiExprScreen>(*this, key));
    });
    f.add("settings",                [this] { return menu::MenuPtr(std::make_unique<SettingsScreen>(*this)); });
    f.add("settings.graphics",       [this] { return menu::MenuPtr(std::make_unique<GraphicsSettingsScreen>(*this)); });
    f.add("settings.theme",          [this] { return menu::MenuPtr(std::make_unique<ThemeSettingsScreen>(*this)); });
    return f;
}

void App::registerActions() {
    using core::Action;

    // LE CHEMIN DE SORTIE. Il n'y en avait aucun : une fois l'affaire ouverte,
    // revenir a l'accueil demandait de fermer la fenetre et de relancer.
    //
    // L'ecran d'analyse s'en charge lui-meme parce que c'est lui qui sait poser
    // la question des modifications ; ici on ne fait que declarer le raccourci
    // et le libelle, pour qu'ils apparaissent dans la palette de commandes
    // comme tout le reste.
    actions_.add(Action{"app.home", "Revenir au menu principal", help::keys::bindingOf("app.home"), {}, {}, nullptr,
                        [this] { goHome(); }});
    // 1.8.0 : les dossiers de l'application (app/Dossiers.hpp) - l'accueil en a le
    // lien, le menu Projet l'entree.
    actions_.add(Action{"app.folders", "Dossiers de l'application...", {}, {}, {}, nullptr,
                        [this] { showFoldersDialog(*this); }});

    // F1, LA TOUCHE QUE TOUT LE MONDE ESSAIE. L'aide existait - le document,
    // son widget, ses formats - sans qu'aucun chemin n'y mene. Un ensemble dont
    // chaque morceau est complet peut tres bien ne servir a rien.
    actions_.add(Action{"help.open", "Aide", help::keys::bindingOf("help.open"), {}, {}, nullptr,
                        [this] { menus_->PushMenu("help"); }});
    // 1.11 (chantier T3, D5) : la page des expressions (Aide > Les expressions, Aller a...).
    actions_.add(Action{"help.expressions", "Page des expressions", {}, {}, {}, nullptr,
                        [this] { menus_->PushMenu("help.expressions"); }});
    // 1.10 (chantier P) : les nouveautes - la fenetre, et les reperes orange.
    actions_.add(Action{"help.news", "Nouveaut\xC3\xA9s\xE2\x80\xA6", {}, {}, {}, nullptr,
                        [] { noveltyCenter().openBoard(); }});
    actions_.add(Action{"help.newsMarks", "Rep\xC3\xA8res des nouveaut\xC3\xA9s", {}, {}, {}, nullptr,
                        [this] {
                            auto& c = noveltyCenter();
                            c.setMarksHidden(!c.marksHidden());
                            bus_.publish(StatusNotice{c.marksHidden()
                                ? "Rep\xC3\xA8res des nouveaut\xC3\xA9s masqu\xC3\xA9s (Aide \xE2\x80\xBA Rep\xC3\xA8res des nouveaut\xC3\xA9s : les remontrer)."
                                : "Rep\xC3\xA8res des nouveaut\xC3\xA9s remontr\xC3\xA9s : les \xC3\xA9l\xC3\xA9ments nouveaux sont encadr\xC3\xA9s en orange.", 6.0});
                        }});

    // 1.10 (R2, menu Aide) : la fenetre A propos (version, construction, dossiers,
    // composants et licences, informations pour le support).
    actions_.add(Action{"help.about", "\xC3\x80 propos d'XPGAnalyser\xE2\x80\xA6", {}, {}, {}, nullptr,
                        [this] { showAboutDialog(*this); }});
    // 1.10.2 (CR) : l'historique interne des appels (app/CrashDialogs.hpp).
    actions_.add(Action{"help.callTrail", "Journal interne (historique des appels)\xE2\x80\xA6", {}, {}, {}, nullptr,
                        [this] { crashdialogs::showCallTrail(*this); }});

    // 1.11.2 (UNI, decision 218) : "Ouvrir..." (le menu de la puce projet, Ctrl+O, la
    // palette) remplacait le projet modifie sans un mot : ses modifications non
    // enregistrees etaient perdues. La question de l'accueil d'abord (App::confirmReplace),
    // avant la fenetre Ouvrir un projet, comme l'accueil. L'accueil, lui, la pose deja
    // avant de declencher file.open (StartupScreen::openFileDialog) : pas deux fois.
    actions_.add(Action{"file.open", "Ouvrir un projet\xE2\x80\xA6", help::keys::bindingOf("file.open"), {}, {}, nullptr,
                        [this] {
                            const auto show = [this] {
                                menus_->ShowDialog(std::make_unique<OpenProjectDialog>(*this),
                                                   [this](const menu::DialogResult& r) {
                                                       if (!r.accepted()) return;
                                                       // Dossier... (l'explorateur) : un dossier de
                                                       // projet s'ouvre, il ne s'importe pas.
                                                       if (project::ProjectStore::isProjectFolder(r.payload)) openProjectFolder(r.payload);
                                                       else openPath(r.payload);
                                                   });
                            };
                            if (dynamic_cast<StartupScreen*>(menus_->top())) show();
                            else confirmReplace(show);
                        }});
    // Importing the hardware export needed its own entry point: the Open button
    // starts a new project, which is the opposite of what a .XHW is for.
    actions_.add(Action{"file.importHardware", "Importer la configuration mat\xC3\xA9rielle (.XHW)\xE2\x80\xA6", help::keys::bindingOf("file.importHardware"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] {
                            menus_->ShowDialog(
                                std::make_unique<OpenProjectDialog>(*this, /*hardwareOnly*/ true),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    // 1.8.0 : dans un projet ouvert (un dossier), lu a cote et suivi
                                    // par sa fenetre, puis pose en une commande (Ctrl+Z).
                                    if (auto* screen = dynamic_cast<MainAnalysisScreen*>(menus_->top()); screen && document_) {
                                        screen->startImportJob(false, {r.payload});
                                        return;
                                    }
                                    openPath(r.payload);
                                });
                        }});
    // 1.8.0 : LE PROGRAMME LISIBLE - toutes les sections dans l'ordre d'execution,
    // en Excel, PDF et texte, pour qui n'a pas Control Expert (LisibleWorkspace.cpp).
    actions_.add(Action{"program.export", "Exporter le programme lisible\xE2\x80\xA6", help::keys::bindingOf("program.export"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] {
                            if (auto* screen = dynamic_cast<MainAnalysisScreen*>(menus_->top())) screen->askProgramExport(0);
                            else bus_.publish(StatusNotice{"Exporter le programme lisible : depuis l'espace de travail du projet (Projet \xE2\x80\xBA Exporter le programme lisible).", 8.0});
                        }});
    // Lot 7 : UN .XPG DANS LE PROJET OUVERT - un nouveau MAST. L'ecran d'analyse
    // demande le fichier, montre ce qui est garde, change, supprime (et les
    // liens de l'IHM perdus), puis importe en une commande ; le projet garde son
    // IHM, ses versions, son materiel. (Ouvrir un .XPG, lui, remplace tout.)
    actions_.add(Action{"file.importMast", "Importer un .XPG (nouveau MAST)\xE2\x80\xA6", "",
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] {
                            if (auto* screen = dynamic_cast<MainAnalysisScreen*>(menus_->top())) screen->askImportMast();
                            else bus_.publish(StatusNotice{"Importer un .XPG : depuis l'espace de travail du projet (Projet \xE2\x80\xBA Importer un .XPG).", 8.0});
                        }});
    actions_.add(Action{"project.new", "Nouveau projet\xE2\x80\xA6", help::keys::bindingOf("project.new"), {}, {}, nullptr,
                        [this] {
                            namespace fs = std::filesystem;
                            // 1.12.0 : XPGAnalyser IHM - un dossier d'IHM, sans programme : son nom, son dossier.
                            if (!core::hasApi()) {
                                newHmiProject();
                                return;
                            }
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Name", "New machine", "", false, {}});
                            // SOUS ./projets/, ET PAS A COTE DE L'EXECUTABLE.
                            //
                            // Le defaut etait le dossier courant, c'est-a-dire
                            // celui du programme : sur une installation dans
                            // Program Files, chaque affaire y deposait un
                            // dossier. Au bout de vingt, on ne distingue plus
                            // les projets du logiciel.
                            f.push_back({"Folder", (projectsRoot() / "New machine").string(),
                                         "sous ./projets/ par defaut", false, {}});
                            f.push_back({"CPU", "BMXP342020", "", false,
                                         {"BMXP342020", "BMXP342030", "BMXP342010", "BMXP342000"}});
                            menus_->ShowDialog(
                                // Le dossier : le bouton ... choisit OU le creer (le nom reste).
                                FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.newProject", "New project",
                                    "Creates a project folder with one MAST task. Sections, "
                                    "types and blocks are added afterwards.",
                                    std::move(f), "Create"), 1, ui::newFolder(projectsRoot().string(), "Dossier du nouveau projet")),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 3) return;
                                    auto p = project::ProjectStore::createEmpty(v[0], v[2]);
                                    project::Manifest m;
                                    m.name         = v[0];
                                    m.state        = project::State::New;
                                    m.cpuReference = v[2];
                                    if (const auto key = core::editionKey(); !key.empty()) m.edition = std::string(key);   // 1.12.0
                                    if (auto ok = project::ProjectStore::save(*p, m, v[1]); !ok) {
                                        menus_->ShowDialog(std::make_unique<MessageDialog>(
                                            "Impossible de cr\xC3\xA9" "er", ok.error().message(),
                                            MessageDialog::Icon::Error),
                                            [](const menu::DialogResult&) {});
                                        return;
                                    }
                                    adoptProject(p, m, v[1]);
                                });
                        }});

    actions_.add(Action{"project.save", "Enregistrer le projet", help::keys::bindingOf("project.save"),
                        [this] { return project_ != nullptr && !projectFolder_.empty(); }, {},
                        nullptr,
                        [this] {
                            auto r = saveProject();
                            menus_->ShowDialog(std::make_unique<MessageDialog>(
                                r ? "Enregistr\xC3\xA9" : "\xC3\x89" "chec de l'enregistrement",
                                r ? "Projet \xC3\xA9" "crit dans " + projectFolder_
                                  : r.error().message(),
                                r ? MessageDialog::Icon::Info : MessageDialog::Icon::Error),
                                [](const menu::DialogResult&) {});
                        }});

    actions_.add(Action{"project.saveAs", "Enregistrer sous (dossier de projet)\xE2\x80\xA6", "",
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] {
                            namespace fs = std::filesystem;
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Name", project_ ? project_->header.projectName : "",
                                         "", false, {}});
                            f.push_back({"Folder",
                                         (projectsRoot() / "project").string(), "", false, {}});
                            menus_->ShowDialog(
                                FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.saveAs",
                                    "Save as project folder",
                                    "Writes the imported project as an editable folder: one .st "
                                    "file per section, declarations as readable tables.",
                                    std::move(f), "Save"), 1, ui::newFolder(projectsRoot().string(), "Enregistrer sous : le dossier du projet")),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !project_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    project::Manifest m = manifest_;
                                    m.name  = v[0];
                                    m.state = project::State::Dev;
                                    if (const auto key = core::editionKey(); !key.empty()) m.edition = std::string(key);   // 1.12.0
                                    m.company    = project_->header.company;
                                    m.product    = project_->header.product;
                                    m.dtdVersion = project_->header.dtdVersion;
                                    if (auto ok = project::ProjectStore::save(*project_, m, v[1]); ok) {
                                        manifest_ = m;
                                        projectFolder_ = v[1];
                                        rememberPath(v[1]);
                                        if (saveHmi(v[1])) hmiKey_ = v[1];
                                    }
                                });
                        }});

    actions_.add(Action{"project.duplicate", "Dupliquer le projet\xE2\x80\xA6", "",
                        [this] { return !projectFolder_.empty(); }, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"New name", manifest_.name + " - variant", "", false, {}});
                            f.push_back({"Destination folder", projectFolder_ + " - copy", "",
                                         false, {}});
                            menus_->ShowDialog(
                                FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.duplicate",
                                    "Duplicate project",
                                    "The copy is always editable: it comes back as DEV, without "
                                    "the lock and without the generated src/ folder.",
                                    std::move(f), "Duplicate"), 1, ui::newFolder(projectFolder_, "Dossier de la copie")),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    auto ok = project::ProjectStore::duplicate(projectFolder_,
                                                                               v[1], v[0]);
                                    menus_->ShowDialog(std::make_unique<MessageDialog>(
                                        ok ? "Dupliqu\xC3\xA9" : "Impossible de dupliquer",
                                        ok ? "Copi\xC3\xA9 dans " + v[1] : ok.error().message(),
                                        ok ? MessageDialog::Icon::Info : MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                                });
                        }});

    actions_.add(Action{"project.rename", "Renommer le projet\xE2\x80\xA6", "",
                        [this] { return !projectFolder_.empty(); }, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Name", manifest_.name, "", false, {}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.rename", "Rename project",
                                    "Renames the project, not the folder on disk.",
                                    std::move(f), "Rename"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted()) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.empty() || v[0].empty()) return;
                                    manifest_.name = v[0];
                                    (void)saveProject();
                                });
                        }});

    actions_.add(Action{"project.state", "Changer l'\xC3\xA9tat du projet\xE2\x80\xA6", "",
                        [this] { return !projectFolder_.empty(); }, {}, nullptr,
                        [this] {
                            // Lot API 6 : L'ETAT SUIT LES VERSIONS. FINISH termine la version en
                            // cours (une version validee), LOCK la livre et la verrouille (une
                            // version livree) ; DEV la rouvre (la suivante commence). Les deux
                            // premiers champs sont ceux d'avant : les sessions rejouees s'y fient.
                            std::vector<FormDialog::Field> f;
                            f.push_back({"State", std::string(project::toString(manifest_.state)),
                                         "", false, {"NEW", "DEV", "FINISH", "LOCK"}});
                            f.push_back({"Password (LOCK only)", "", "", true, {}});
                            f.push_back({"Nom de la version (FINISH, LOCK)", "", "vide : \xC2\xAB Termin\xC3\xA9" "e le \xE2\x80\xA6 \xC2\xBB, \xC2\xAB Livr\xC3\xA9" "e le \xE2\x80\xA6 \xC2\xBB", false, {}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.state", "\xC3\x89tat du projet",
                                    "NEW : rien de fait. DEV : la version suivante en cours. FINISH : la version en cours "
                                    "est termin\xC3\xA9" "e - une version valid\xC3\xA9" "e la fige. LOCK : livr\xC3\xA9" "e et verrouill\xC3\xA9" "e - une "
                                    "version livr\xC3\xA9" "e, et le mot de passe (les fichiers restent lisibles : c'est une "
                                    "barri\xC3\xA8re, pas un chiffrement). Le menu de la version, en haut, fait la m\xC3\xAAme chose.",
                                    std::move(f), "Apply"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !project_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.empty()) return;
                                    const auto state = project::stateFromString(v[0]);
                                    const auto password = v.size() > 1 ? v[1] : std::string{};
                                    const auto name = v.size() > 2 ? v[2] : std::string{};
                                    const auto refuse = [this](const std::string& title, const std::string& text) {
                                        menus_->ShowDialog(std::make_unique<MessageDialog>(title, text, MessageDialog::Icon::Warning),
                                                           [](const menu::DialogResult&) {});
                                    };
                                    if (state == manifest_.state) return;
                                    if (state == project::State::Lock && password.empty()) {
                                        refuse("Password required", "Locking a project without a password would lock it "
                                                                    "for everyone including you.");
                                        return;
                                    }
                                    if (manifest_.state == project::State::Lock) {
                                        refuse("Projet verrouill\xC3\xA9", "Le projet est LOCK : Projet > D\xC3\xA9verrouiller d'abord (le mot de passe, ou la cl\xC3\xA9 du PC ma\xC3\xAEtre).");
                                        return;
                                    }
                                    if (state == project::State::Finish || state == project::State::Lock) {
                                        auto made = closeVersion(*this, state, name, {}, password);
                                        if (!made) {
                                            refuse("\xC3\x89tat non chang\xC3\xA9", made.error().context.empty() ? made.error().message() : made.error().context);
                                            return;
                                        }
                                        bus_.publish(StatusNotice{"V" + std::to_string(*made) + (state == project::State::Lock ? " livr\xC3\xA9" "e et verrouill\xC3\xA9" "e : le projet est LOCK."
                                                                                                                          : " valid\xC3\xA9" "e : le projet est FINISH."), 10.0});
                                        return;
                                    }
                                    auto store = hmi::ver::open(projectFolder_);
                                    const int last = store && store->last() ? store->last()->number : 0;
                                    if (state == project::State::New && last > 0) {
                                        refuse("\xC3\x89tat non chang\xC3\xA9", "Le projet a d\xC3\xA9j\xC3\xA0 des versions (V" + std::to_string(last) + ") : il ne redevient pas NEW.");
                                        return;
                                    }
                                    if (auto st = setProjectState(state); !st) {
                                        refuse("\xC3\x89tat non chang\xC3\xA9", st.error().message());
                                        return;
                                    }
                                    bus_.publish(StatusNotice{state == project::State::Dev ? "DEV : la V" + std::to_string(last + 1) + " commence."
                                                                                           : std::string("NEW : rien de fait encore."), 8.0});
                                });
                        }});

    // Lot 15 : deverrouiller - le mot de passe, ou la cle du PC maitre.
    actions_.add(Action{"project.unlock", "D\xC3\xA9verrouiller le projet...", "",
                        [this] { return !projectFolder_.empty() && manifest_.state == project::State::Lock; }, {}, nullptr,
                        [this] { askUnlock(); }});

    // ---- creating things --------------------------------------------------
    // Every one of these goes through apply(), so it lands on the undo stack and
    // Ctrl+Z takes it back. The dialogs only gather the values; the commands do
    // the validating, in project/EditCommands.cpp, where the model rules live.
    auto haveDocument = [this] { return document_ != nullptr; };

    actions_.add(Action{"create.section", "Nouvelle section\xE2\x80\xA6", "",
                        haveDocument, {}, nullptr,
                        [this] {
                            if (!document_) return;
                            // Where the section goes is a first-class choice: a
                            // body inside a program unit or a DFB is not the
                            // same thing as a task section.
                            const auto containers = project::sectionContainers(*document_);
                            std::vector<std::string> labels;
                            for (const auto& c : containers) labels.push_back(c.label);

                            std::vector<FormDialog::Field> f;
                            f.push_back({"Nom", "NewSection", "", false, {}});
                            f.push_back({"Conteneur", labels.front(), "", false, labels});
                            f.push_back({"T\xC3\xA2" "che", "MAST", "sections de t\xC3\xA2" "che seulement", false,
                                         {"MAST", "FAST", "AUX0"}});
                            f.push_back({"Langage", "ST", "", false,
                                         {"ST", "IL", "LD", "FBD", "SFC"}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newSection", "Nouvelle section",
                                    "Une section de t\xC3\xA2" "che tourne \xC3\xA0 son rang dans la t\xC3\xA2" "che. Une section dans une unit\xC3\xA9 de programme ou un DFB en est le code, et son nom n'a besoin d'\xC3\xAAtre unique que l\xC3\xA0 : deux unit\xC3\xA9s peuvent avoir chacune un Init.",
                                    std::move(f), "Cr\xC3\xA9" "er"),
                                [this, containers](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 4) return;
                                    auto owner = domain::kNoIndex;
                                    for (const auto& c : containers)
                                        if (c.label == v[1]) owner = c.index;
                                    const auto lang = v[3] == "IL"  ? domain::PouLanguage::IL
                                                    : v[3] == "LD"  ? domain::PouLanguage::LD
                                                    : v[3] == "FBD" ? domain::PouLanguage::FBD
                                                    : v[3] == "SFC" ? domain::PouLanguage::SFC
                                                                    : domain::PouLanguage::ST;
                                    apply(std::make_unique<project::AddSectionCommand>(
                                        document_, v[0],
                                        owner == domain::kNoIndex ? v[2] : std::string{},
                                        lang, owner));
                                });
                        }});

    actions_.add(Action{"create.unit", "Nouvelle unit\xC3\xA9 de programme\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Nom", "NewUnit", "", false, {}});
                            f.push_back({"T\xC3\xA2" "che", "MAST", "", false, {"MAST", "FAST", "AUX0"}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newUnit", "Nouvelle unit\xC3\xA9 de programme",
                                    "Une unit\xC3\xA9 de programme regroupe des sections et porte ses propres variables ; elle tourne en bloc, \xC3\xA0 son rang dans la t\xC3\xA2" "che.",
                                    std::move(f), "Cr\xC3\xA9" "er"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    apply(std::make_unique<project::AddProgramUnitCommand>(
                                        document_, v[0], v[1]));
                                });
                        }});

    actions_.add(Action{"create.dfb", "Nouveau type de bloc DFB\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Nom", "DFB_New", "", false, {}});
                            f.push_back({"Version", "0.01", "", false, {}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newDfb", "Nouveau bloc DFB",
                                    "Cr\xC3\xA9" "e un bloc fonction d\xC3\xA9riv\xC3\xA9. Ses entr\xC3\xA9" "es, ses sorties et son code s'ajoutent ensuite.",
                                    std::move(f), "Cr\xC3\xA9" "er"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    apply(std::make_unique<project::AddFunctionBlockCommand>(
                                        document_, v[0], v[1]));
                                });
                        }});

    actions_.add(Action{"create.ddt", "Nouveau type d\xC3\xA9riv\xC3\xA9 (DDT)\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Nom", "ST_New", "", false, {}});
                            f.push_back({"Version", "0.01", "", false, {}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newDdt", "Nouveau type d\xC3\xA9riv\xC3\xA9",
                                    "Cr\xC3\xA9" "e une structure. Ses champs s'ajoutent ensuite : + Champ dans l'onglet Types d\xC3\xA9riv\xC3\xA9s, ou une variable de port\xC3\xA9" "e Member.",
                                    std::move(f), "Cr\xC3\xA9" "er"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    apply(std::make_unique<project::AddDerivedTypeCommand>(
                                        document_, v[0], v[1]));
                                });
                        }});

    actions_.add(Action{"create.variable", "Nouvelle variable\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            if (!document_) return;
                            const auto containers = project::variableContainers(*document_);
                            const auto types      = project::availableTypeChoices(*document_);

                            // Field order matters: the rules below address them
                            // by position, and so does the payload.
                            enum F { Name, In, Scope, Type, Array, Dims, Address, Init, Comment };

                            std::vector<std::string> typeNames;
                            for (const auto& t : types) typeNames.push_back(t.name);

                            std::vector<FormDialog::Field> f(9);
                            f[Name]    = {"Nom", "", "gNewVariable", false, {}};
                            f[In]      = {"D\xC3\xA9" "clar\xC3\xA9" "e dans", containers.front().label, "", false,
                                          {containers.front().label}};
                            f[Scope]   = {"Port\xC3\xA9" "e", "Global", "", false,
                                          {"Global", "Input", "Output", "InOut",
                                           "Public", "Private", "Member"}};
                            f[Type]    = {"Type", typeNames.front(), "", false, typeNames};
                            f[Array]   = {"Tableau", "No", "", false, {"No", "Yes"}};
                            f[Dims]    = {"Dimensions", "0..9",
                                          "0..9  ou  0..9, 0..3 pour deux dimensions", false, {}};
                            f[Address] = {"Adresse", "",
                                          "%MW10, %I0.3, %Q0.1 - vide : non situ\xC3\xA9" "e",
                                          false, {}};
                            f[Init]    = {"Valeur initiale", "", "", false, {}};
                            f[Comment] = {"Commentaire", "", "", false, {}};

                            auto dialog = std::make_unique<FormDialog>(
                                "dialog.newVariable", "Nouvelle variable",
                                "Input, Output et InOut : l'interface d'un DFB ou d'une unit\xC3\xA9 de programme. Public et Private : ses propres donn\xC3\xA9" "es. Member : un champ d'un type d\xC3\xA9riv\xC3\xA9 (c'est ainsi qu'on remplit un DDT). Global : n'appartient \xC3\xA0 aucun conteneur.",
                                std::move(f), "Cr\xC3\xA9" "er");

                            // Only what is legal, and the reason when something
                            // is not. The command refuses an array of function
                            // blocks anyway; offering it and then refusing it is
                            // how a dialog earns distrust.
                            // The rules themselves live in project::, next to
                            // the command that enforces them, so they can be
                            // tested and so there is only one statement of them.
                            // This lambda only marshals fields.
                            dialog->setRules(
                                [this](const std::vector<std::string>& v,
                                       std::vector<FormDialog::FieldState>& fs) {
                                    if (v.size() < 9 || !document_) return;

                                    project::VariableFormState st;
                                    st.scope       = v[Scope];
                                    st.container   = v[In];
                                    st.type        = v[Type];
                                    st.arrayWanted = v[Array] == "Yes";
                                    project::applyVariableFormRules(*document_, st);

                                    fs[In].choices  = st.containerChoices;
                                    fs[In].value    = st.container;
                                    fs[In].enabled  = st.containerEnabled;
                                    fs[In].hint     = st.containerHint;

                                    fs[Type].choices = st.typeChoices;
                                    fs[Type].value   = st.type;

                                    fs[Array].enabled = st.arrayAllowed;
                                    fs[Array].hint    = st.arrayHint;
                                    fs[Array].value   = st.arrayWanted ? "Yes" : "No";

                                    fs[Dims].enabled = st.arrayWanted;
                                    fs[Dims].hint    = st.arrayWanted ? std::string{}
                                                                     : "mettre Tableau \xC3\xA0 Yes";
                                });

                            menus_->ShowDialog(
                                std::move(dialog),
                                [this, containers](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 9) return;

                                    project::AddVariableCommand::Spec spec;
                                    spec.name      = v[Name];
                                    spec.address   = v[Address];
                                    spec.initValue = v[Init];
                                    spec.comment   = v[Comment];

                                    spec.type = project::composeTypeName(
                                        v[Type], v[Array] == "Yes", v[Dims]);

                                    spec.scope =
                                        v[Scope] == "Input"   ? domain::VariableScope::Input
                                      : v[Scope] == "Output"  ? domain::VariableScope::Output
                                      : v[Scope] == "InOut"   ? domain::VariableScope::InOut
                                      : v[Scope] == "Public"  ? domain::VariableScope::Public
                                      : v[Scope] == "Private" ? domain::VariableScope::Local
                                      : v[Scope] == "Member"  ? domain::VariableScope::DerivedMember
                                                              : domain::VariableScope::Global;
                                    for (const auto& c : containers)
                                        if (c.label == v[In]) spec.owner = c.index;

                                    // The rules above should make this
                                    // unreachable. It stays because a dialog that
                                    // silently creates something surprising is
                                    // worse than one that says no.
                                    const bool needsOwner =
                                        spec.scope != domain::VariableScope::Global;
                                    if (needsOwner && spec.owner == domain::kNoIndex) {
                                        menus_->ShowDialog(std::make_unique<MessageDialog>(
                                            "Choisir un conteneur",
                                            "Une variable " + v[Scope] + " appartient \xC3\xA0 une unit\xC3\xA9 de programme, \xC3\xA0 un DFB ou \xC3\xA0 un type d\xC3\xA9riv\xC3\xA9 : en choisir un dans \xC2\xAB D\xC3\xA9" "clar\xC3\xA9" "e dans \xC2\xBB, ou prendre la port\xC3\xA9" "e Global.",
                                            MessageDialog::Icon::Warning),
                                            [](const menu::DialogResult&) {});
                                        return;
                                    }
                                    apply(std::make_unique<project::AddVariableCommand>(
                                        document_, std::move(spec)));
                                });
                        }});

    actions_.add(Action{"create.rack", "Ajouter un rack\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            // Both lists come from the catalogue file, so adding a
                            // reference there is enough to make it selectable here.
                            const auto catalog = importer::HardwareCatalog::builtinFallback();
                            std::vector<std::string> racks, supplies;
                            for (const auto* b : catalog.backplanes())
                                racks.push_back(b->reference + "  -  " + b->description);
                            for (const auto* s : catalog.powerSupplies())
                                supplies.push_back(s->reference + "  -  " + s->description);
                            if (racks.empty()) racks.push_back("BMXXBP0800");
                            if (supplies.empty()) supplies.push_back("BMXCPS2000");

                            std::vector<FormDialog::Field> f;
                            f.push_back({"Embase", racks.front(), "", false, racks});
                            f.push_back({"Alimentation", supplies.front(), "", false, supplies});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newRack", "Ajouter un rack",
                                    "Chaque rack porte son alimentation, \xC3\xA0 sa place double. Le rack 0 re\xC3\xA7oit aussi le processeur \xC3\xA0 l'emplacement 0 : c'est la seule place permise \xC3\xA0 un BMX P34.",
                                    std::move(f), "Ajouter"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 2) return;
                                    // The labels carry a description after the
                                    // reference; only the reference is the value.
                                    auto reference = [](std::string label) {
                                        const auto at = label.find("  -  ");
                                        return at == std::string::npos ? label : label.substr(0, at);
                                    };
                                    apply(std::make_unique<project::AddRackCommand>(
                                        document_, reference(v[0]), reference(v[1])));
                                });
                        }});

    actions_.add(Action{"create.module", "Ajouter un module\xE2\x80\xA6", "", haveDocument, {}, nullptr,
                        [this] {
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Rack", "0", "", false, {}});
                            f.push_back({"Emplacement", "1", "", false, {}});
                            f.push_back({"R\xC3\xA9" "f\xC3\xA9rence", "BMXDDI1602",
                                         "une r\xC3\xA9" "f\xC3\xA9rence de resources/plc_catalog.txt", false, {}});
                            menus_->ShowDialog(
                                std::make_unique<FormDialog>("dialog.newModule", "Ajouter un module",
                                    "L'emplacement doit \xC3\xAAtre libre et dans l'embase. Sur le rack 0, l'emplacement 0 est celui du processeur (un BMX P34 est toujours \xC3\xA0 l'emplacement 00) ; l'alimentation a sa place double, pos\xC3\xA9" "e avec le rack.",
                                    std::move(f), "Ajouter"),
                                [this](const menu::DialogResult& r) {
                                    if (!r.accepted() || !document_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    if (v.size() < 3) return;
                                    apply(std::make_unique<project::AddModuleCommand>(
                                        document_,
                                        static_cast<std::uint16_t>(std::atoi(v[0].c_str())),
                                        static_cast<std::int16_t>(std::atoi(v[1].c_str())),
                                        v[2]));
                                });
                        }});

    // ---- simulation --------------------------------------------------------
    // Lot API 7 : F9 n'ouvre plus un ecran plein mais l'onglet API > Simulation
    // (la table de forcage, la courbe, ce que dit le simulateur) : la barre du
    // haut, l'arbre et les autres onglets restent la pendant que le programme
    // tourne. L'ecran d'analyse ouvre l'onglet (OpenApiTab) ; la simulation
    // elle-meme reste dans SimulationHost, a App.
    // ---- Lot API 8 : Centre de simulation ----
    // F9 ouvre Simulation > Vue d'ensemble (la maquette validee) ; l'onglet de
    // l'automate est a un clic (sa carte, l'arbre Simulation > Automate).
    actions_.add(Action{"sim.open", "Simulation : vue d'ensemble", help::keys::bindingOf("sim.open"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] { bus_.publish(OpenApiTab{"sim:ensemble"}); }});
    // ---- fin Lot API 8 : Centre de simulation ----
    // Le bouton "Workspace" de l'ancien ecran. Il n'y a plus d'ecran a quitter :
    // depuis l'espace de travail, rien ne se fait (SwitchMenu le reconstruisait
    // et fermait tous ses onglets).
    actions_.add(Action{"sim.back", "Revenir \xC3\xA0 l'espace de travail", "",
                        [] { return true; }, {}, nullptr,
                        [this] { if (!menus_->contains("analysis")) menus_->SwitchMenu("analysis"); }});
    // 1.11 (T2) : les touches (F5, Maj+F5) viennent de help::keys ; Lot API 8 : F10 est la section suivante.
    for (const auto& [id, label] : std::initializer_list<std::pair<const char*, const char*>>{
             {"sim.run", "Run"}, {"sim.pause", "Pause"},
             {"sim.stop", "Stop"}, {"sim.step", "Step one scan"},
             {"sim.force", "Force..."}, {"sim.unforce", "Release"},
             {"sim.watch", "Watch on the trend"}}) {
        actions_.add(Action{id, label, help::keys::bindingOf(id), [] { return true; }, {}, nullptr, [] {}});
    }

    actions_.add(Action{"project.exportSources", "Exporter vers Control Expert", "",
                        [this] { return project_ != nullptr && !projectFolder_.empty(); }, {},
                        nullptr,
                        [this] {
                            // OU ECRIRE MAST.XPG (et CONFIG.XHW) : src/ du projet par defaut
                            // (Exporter, ou Entree dans le champ), ou un autre dossier - celui
                            // qu'importe Control Expert - choisi avec le bouton ... (l'explorateur).
                            const std::string src = (std::filesystem::path(projectFolder_) / "src").string();
                            std::vector<FormDialog::Field> f;
                            f.push_back({"Dossier", src, "src/ du projet par d\xC3\xA9" "faut", false, {}});
                            menus_->ShowDialog(
                                FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.exportSources", "Vers Control Expert",
                                    "\xC3\x89" "crit MAST.XPG, et CONFIG.XHW si le projet a des racks, pr\xC3\xAA" "ts \xC3\xA0 r\xC3\xA9importer "
                                    "dans Control Expert. Par d\xC3\xA9" "faut dans src/ du projet ; le bouton \xE2\x80\xA6 choisit un autre dossier.",
                                    std::move(f), "Exporter"), 0, ui::chooseFolder(src, "Vers Control Expert : le dossier")),
                                [this, src](const menu::DialogResult& r) {
                                    if (!r.accepted() || !project_) return;
                                    const auto v = FormDialog::split(r.payload);
                                    std::string folder = v.empty() ? std::string{} : v[0];
                                    while (!folder.empty() && std::isspace(static_cast<unsigned char>(folder.back()))) folder.pop_back();
                                    while (!folder.empty() && std::isspace(static_cast<unsigned char>(folder.front()))) folder.erase(folder.begin());
                                    if (folder.size() >= 2 && folder.front() == '"' && folder.back() == '"') folder = folder.substr(1, folder.size() - 2);
                                    if (folder.empty()) folder = src;
                                    std::size_t count = 0;
                                    std::string error;
                                    if (folder == src) {
                                        // src/ du projet : l'export de toujours.
                                        auto written = project::ProjectStore::exportSources(*project_, projectFolder_);
                                        if (written) count = written->size();
                                        else error = written.error().message();
                                    } else {
                                        const std::filesystem::path dir(folder);
                                        if (auto st = exporter::writeXpgFile(*project_, (dir / "MAST.XPG").string()); !st) error = st.error().message();
                                        else ++count;
                                        if (error.empty() && !project_->hardware.racks.empty()) {
                                            if (auto st = exporter::writeXhwFile(*project_, (dir / "CONFIG.XHW").string()); !st) error = st.error().message();
                                            else ++count;
                                        }
                                    }
                                    // ---- Lot API 8 : bandeau haut - la cloche : l'export termine, et son dossier ----
                                    if (error.empty())
                                        bgtasks::post({"export:control-expert", "Projet",
                                                       std::string("Export termin\xC3\xA9 : MAST.XPG") + (count > 1 ? ", CONFIG.XHW" : ""), folder,
                                                       "Ouvrir le dossier", "open.folder:" + folder, "ok"});
                                    // ---- fin Lot API 8 : bandeau haut ----
                                    menus_->ShowDialog(std::make_unique<MessageDialog>(
                                        error.empty() ? "Export\xC3\xA9" : "Export impossible",
                                        error.empty() ? std::to_string(count) + (count > 1 ? " fichiers \xC3\xA9" "crits dans " : " fichier \xC3\xA9" "crit dans ") + folder
                                                      : error,
                                        error.empty() ? MessageDialog::Icon::Info : MessageDialog::Icon::Error),
                                        [](const menu::DialogResult&) {});
                                });
                        }});

    actions_.add(Action{"file.close", "Fermer le projet", "",       // lot 7 : Ctrl+W ferme un onglet
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] {
                            project_.reset();
                            document_.reset();
                            report_.reset();
                            projectFolder_.clear();
                            sources_.clear();
                            manifest_ = project::Manifest{};
                            commands_.clear();
                            openedAtMs_ = 0;
                            savedAtMs_ = 0;
                            hmi_.reset();
                            hmiKey_.clear();
                            hmiWarnings_.clear();
                            hmiUnreadable_.clear();
                            // Lot API 7 : la simulation du projet ferme ne tourne
                            // plus derriere l'accueil (App l'avancerait a chaque image).
                            simulation_.detach();
                            setSimulationStatus({});
                            bus_.publish(ProjectClosed{});
                            menus_->SwitchMenu("startup");
                        }});
    actions_.add(Action{"edit.undo", "Annuler", help::keys::bindingOf("edit.undo"),
                        [this] { return commands_.canUndo(); }, {}, nullptr,
                        [this] { undo(); }});
    actions_.add(Action{"edit.redo", "R\xC3\xA9tablir", help::keys::bindingOf("edit.redo"),
                        [this] { return commands_.canRedo(); }, {}, nullptr,
                        [this] { redo(); }});
    // Lot 19 : l'historique (l'ecran l'ouvre ; ici pour les sessions : action edit.history).
    actions_.add(Action{"edit.history", "Historique", help::keys::bindingOf("edit.history"), {}, {}, nullptr,
                        [this] { bus_.publish(HistoryToggle{}); }});
    actions_.add(Action{"project.saveKeyboard", "Enregistrer (Ctrl+S)", help::keys::bindingOf("project.saveKeyboard"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] { saveFromKeyboard(); }});
    // Lot API 7 : l'explorateur de variables et le tableau des statistiques
    // etaient des ecrans pleins ; ce sont des onglets de l'API (Variables - celui
    // qui existait deja - et Statistiques). Les actions gardent leur identifiant.
    actions_.add(Action{"view.variables", "Variables (onglet de l'API)", help::keys::bindingOf("view.variables"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] { bus_.publish(OpenApiTab{"variables"}); }});
    actions_.add(Action{"view.statistics", "Statistiques (onglet de l'API)", help::keys::bindingOf("view.statistics"),
                        [this] { return project_ != nullptr; }, {}, nullptr,
                        [this] { bus_.publish(OpenApiTab{"statistiques"}); }});
    // Lot API 6 : la galerie des themes, a droite, sans assombrir l'ecran.
    actions_.add(Action{"view.theme", "Th\xC3\xA8me\xE2\x80\xA6", "", {}, {}, nullptr,
                        [this] { showThemeGallery(); }});
    actions_.add(Action{"analyze.run", "R\xC3\xA9importer et r\xC3\xA9" "analyser", "",       // Lot API 8 : F5 est Simuler (Centre de simulation)
                        [this] { return project_ != nullptr && !importer_->busy(); }, {}, nullptr,
                        [this] { if (project_) importer_->importAsync(project_->header.sourceFile); }});
    actions_.add(Action{"nav.back", "Retour", help::keys::bindingOf("nav.back"),
                        [this] { return menus_->depth() > 1; }, {}, nullptr,
                        [this] { menus_->PreviousMenu(); }});
}

void App::openPath(std::string path) {
    if (path.empty()) return;
    XPG_PORTEE_TEXTE("App::openPath", std::filesystem::path(path).filename().string());   // 1.10.2 (CR)
    // 1.12.0 : XPGAnalyser IHM ouvre ses dossiers de projet, pas les exports de Control Expert.
    if (!core::hasApi()) {
        if (project::ProjectStore::isProjectFolder(path)) {
            openProjectFolder(std::move(path));
            return;
        }
        menus_->ShowDialog(std::make_unique<MessageDialog>(
                               "Un export de l'automate",
                               std::filesystem::path(path).filename().string()
                                   + " : les exports de Control Expert (.XPG, .XHW, .XDB) s'ouvrent avec XPGAnalyser API.\n\n"
                                     "XPGAnalyser IHM ouvre les dossiers de ses projets (Projets\\ihm).",
                               MessageDialog::Icon::Info),
                           [](const menu::DialogResult&) {});
        return;
    }

    // A .XPG starts a new project. A .XHW or .XDB completes the one in hand,
    // because those exports carry no program of their own.
    const auto format = importer::ProjectImporter::sniff({}, path);
    const bool completes = format == importer::SourceFormat::Xhw
                        || format == importer::SourceFormat::Xdb;

    std::vector<std::string> batch;
    if (completes && !sources_.empty()) {
        batch = sources_;
        std::erase(batch, path);              // re-opening the same file replaces it
    }
    batch.push_back(path);
    sources_ = batch;

    importer_->importAsync(std::move(batch));
}

// The master key is asked for once, on the first run, and cannot be dismissed:
// without it a project locked on this machine could never be recovered.
void App::setSimulationStatus(std::string text) {
    if (simStatus_ == text) return;
    simStatus_ = std::move(text);
    simulationStatusChanged->emit(simStatus_);
}

void App::proposeTaskbarPin() {
    if (!taskbarPinPending_) return;
    taskbarPinPending_ = false;
    settings_.set("taskbar.pinAsked", 1);
    settings_.save();
    // Windows affiche sa propre confirmation ; sinon, le geste a faire (une fois).
    if (epinglage::demanderBarreDesTaches() == epinglage::Resultat::Demande) return;
    menus_->ShowDialog(
        std::make_unique<MessageDialog>(
            "\xC3\x89pingler XPGAnalyser \xC3\xA0 la barre des t\xC3\xA2" "ches",
            "Tu as demand\xC3\xA9, \xC3\xA0 l'installation, une ic\xC3\xB4ne sur la barre des t\xC3\xA2" "ches. "
            "Sur ce PC, Windows ne laisse pas une application s'\xC3\xA9pingler elle-m\xC3\xAAme.\n\n"
            "Pour l'\xC3\xA9pingler : clic droit sur l'ic\xC3\xB4ne de XPGAnalyser dans la barre des t\xC3\xA2" "ches "
            "(en bas de l'\xC3\xA9" "cran), puis \xC2\xAB \xC3\x89pingler \xC3\xA0 la barre des t\xC3\xA2" "ches \xC2\xBB.",
            MessageDialog::Icon::Info),
        [](const menu::DialogResult&) {});
}

void App::promptMasterKeyIfNeeded() {
    if (masterKey_.defined()) return;

    // ---- Lot API 8 : le premier lancement, en francais, l'adresse deja trouvee ----
    //  (livre a quelqu'un d'autre, l'appli s'ouvrait sur un dialogue en anglais qui
    //  demandait une adresse MAC a recopier a la main.)
    std::string foundMac;
    for (const auto& a : hmi::netinfo::adapters()) {
        if (a.mac.empty() || a.mac == "00-00-00-00-00-00") continue;
        if (foundMac.empty() || a.up) foundMac = a.mac;
        if (a.up) break;
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back(FormDialog::Field{"Adresse MAC", foundMac, "A4-BB-6D-12-9F-03", false, {}});

    menus_->ShowDialog(
        std::make_unique<FormDialog>(
            "dialog.masterKey", "D\xC3\xA9" "finir la cl\xC3\xA9 administrateur",
            "Ce poste n'a pas encore de cl\xC3\xA9 administrateur. Indique l'adresse MAC de ce PC "
            "(celle trouv\xC3\xA9" "e est d\xC3\xA9j\xC3\xA0 \xC3\xA9" "crite) : elle devient la cl\xC3\xA9 ma\xC3\xAEtresse qui "
            "d\xC3\xA9verrouille tout projet verrouill\xC3\xA9 ici quand le mot de passe est oubli\xC3\xA9. "
            "Elle est gard\xC3\xA9" "e sous forme d'empreinte, jamais en clair. Une adresse MAC se lit "
            "et peut s'imiter : c'est un moyen de r\xC3\xA9" "cup\xC3\xA9ration, pas une barri\xC3\xA8re de "
            "s\xC3\xA9" "curit\xC3\xA9.",
            std::move(fields), "D\xC3\xA9" "finir la cl\xC3\xA9", /*cancellable*/ false),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) { promptMasterKeyIfNeeded(); return; }   // it is not optional
            const auto values = FormDialog::split(r.payload);
            const auto mac = values.empty() ? std::string{} : values.front();
            if (!project::MasterKey::looksLikeMac(mac)) {
                menus_->ShowDialog(std::make_unique<MessageDialog>(
                                       "Adresse invalide",
                                       "'" + mac + "' n'est pas une adresse MAC : six paires de chiffres "
                                       "hexad\xC3\xA9" "cimaux sont attendues, par exemple A4-BB-6D-12-9F-03.",
                                       MessageDialog::Icon::Warning),
                                   [this](const menu::DialogResult&) { promptMasterKeyIfNeeded(); });
                return;
            }
            masterKey_.setFromMac(mac);
            settings_.set("security.masterKey", masterKey_.stored());
            settings_.save();
        });
}

void App::apply(core::CommandPtr command, bool refreshViews) {
    if (!command) return;
    // Lot 15 : un projet LOCK ne se modifie pas.
    if (manifest_.state == project::State::Lock && !projectFolder_.empty()) {
        // Lot 20 : un collage de cent lignes ne montre qu'UNE fois le refus.
        if (commands_.grouping()) {
            if (lockSaidInGroup_ == commands_.groupSerial()) return;
            lockSaidInGroup_ = commands_.groupSerial();
        }
        menus_->ShowDialog(std::make_unique<MessageDialog>(
                               "Projet verrouill\xC3\xA9",
                               "Ce projet est LOCK : personne ne le modifie (\xC2\xAB " + command->label() + " \xC2\xBB est refus\xC3\xA9).\n\n"
                               "Projet > D\xC3\xA9verrouiller : le mot de passe, ou la cl\xC3\xA9 du PC ma\xC3\xAEtre.",
                               MessageDialog::Icon::Warning),
                           [](const menu::DialogResult&) {});
        return;
    }
    const auto label = command->label();
    // Lot API 6 : LA PREMIERE MODIFICATION d'un projet NEW ou FINISH commence la
    // version suivante - le projet passe en DEV, sans rien demander (une question
    // a chaque premiere modification serait vite cliquee sans etre lue). Le
    // message dit laquelle ; tout annuler jusqu'a l'etat enregistre rend l'etat
    // d'avant (settleReopened), enregistrer fixe le DEV.
    std::optional<project::State> reopenedNow;
    if (!projectFolder_.empty() && !reopenedFrom_
        && (manifest_.state == project::State::New || manifest_.state == project::State::Finish)) {
        reopenedNow = manifest_.state;
        reopenedFrom_ = manifest_.state;
        manifest_.state = project::State::Dev;
    }
    auto result = commands_.push(std::move(command));
    if (!result && reopenedNow) {           // rien n'a change : l'etat non plus
        manifest_.state = *reopenedNow;
        reopenedFrom_.reset();
    } else if (reopenedNow) {
        std::string said;
        int last = 0;
        if (auto store = hmi::ver::open(projectFolder_); store && store->last()) last = store->last()->number;
        if (*reopenedNow == project::State::Finish && last > 0)
            said = "La V" + std::to_string(last) + " \xC3\xA9tait termin\xC3\xA9" "e : tu modifies maintenant la V" + std::to_string(last + 1)
                 + " (DEV). Ctrl+Z jusqu'au bout la rend termin\xC3\xA9" "e.";
        else if (*reopenedNow == project::State::Finish)
            said = "Le projet \xC3\xA9tait FINISH : tu le modifies, il passe en DEV (la V1 en cours). Ctrl+Z jusqu'au bout le rend FINISH.";
        else
            said = "Premi\xC3\xA8re modification : la V" + std::to_string(last + 1) + " commence, le projet passe en DEV.";
        bus_.publish(StatusNotice{said, 12.0});
    }
    if (!result) {
        // Lot API 5 : la phrase de la commande, sans le prefixe technique
        // (« invalid argument: », « project data is partial: »).
        const auto& e = result.error();
        menus_->ShowDialog(std::make_unique<MessageDialog>("Impossible : " + label,
                                                           e.context.empty() ? e.message() : e.context,
                                                           MessageDialog::Icon::Warning),
                           [](const menu::DialogResult&) {});
        return;
    }
    // Every view rebinds from the model; the change is visible immediately and
    // Ctrl+Z takes it back.
    if (refreshViews) bus_.publish(ProjectOpened{project_, report_});
}

void App::adoptProject(std::shared_ptr<domain::Project> p, project::Manifest m,
                       std::string folder) {
    document_      = p;
    commands_.clear();          // undo history belongs to the project that is gone
    reopenedFrom_.reset();
    openedAtMs_    = core::CommandStack::wallMs();
    savedAtMs_     = 0;
    project_       = std::move(p);
    manifest_      = std::move(m);
    projectFolder_ = std::move(folder);
    if (!projectFolder_.empty()) rememberPath(projectFolder_);
    // Lot API 7 : la simulation decrivait le programme d'avant, comme a la fin
    // d'un import (plus haut) - et depuis qu'elle est un onglet de l'API, cet
    // onglet, la pastille de l'arbre et Simuler la montreraient et la
    // relanceraient sur le nouveau projet. Un autre projet : une autre simulation.
    simulation_.detach();
    setSimulationStatus({});
    bindHmi();
    bus_.publish(ProjectOpened{project_, report_});
    // Lot 15 : le poste d'exploitation ne s'ouvre que sur un projet FINISH (sauf
    // la reprise d'un poste qui tournait).
    std::string notFinished;
    if (stationPending_ && !stationForce_ && !projectFinished(&notFinished)) {
        stationPending_ = false;
        stationRestore_.reset();
        menus_->SwitchMenu("analysis");
        menus_->ShowDialog(std::make_unique<MessageDialog>(
                               "Poste d'exploitation",
                               "Le poste d'exploitation ne s'ouvre que sur un projet FINISH (d\xC3\xA9" "clar\xC3\xA9 fini).\n\n" + manifest_.name + " est " + notFinished
                                   + " : il s'ouvre en conception. Projet > \xC3\x89tat pour le passer en FINISH.",
                               MessageDialog::Icon::Warning),
                           [](const menu::DialogResult&) {});
    } else {
        menus_->SwitchMenu(stationPending_ ? "station" : "analysis");   // lot 14 : --ihm
    }
    stationForce_ = false;
    recovery_.setProject(projectFolder_, stationPending_ ? "poste" : "conception");   // lot 15
    if (stationPending_) {
        stationPending_ = false;
        stationActive_ = true;
    }
}

// Lot 15 : les modifications gardees par la reprise, sur le projet d'origine -
// a enregistrer (le projet sur le disque n'a pas change).
void App::openRecovered(const Recovery::Found& found) {
    auto opened = project::ProjectStore::open(found.autosave);
    if (!opened || opened->needsPassword) {
        openProjectFolder(found.project);
        return;
    }
    // Le projet est celui d'origine : son dossier, pas celui de la reprise.
    opened->project->header.sourceFile = found.project;
    document_ = opened->project;
    commands_.clear();
    openedAtMs_ = core::CommandStack::wallMs();
    savedAtMs_ = 0;
    project_ = opened->project;
    manifest_ = opened->manifest;
    projectFolder_ = found.autosave;
    simulation_.detach();                        // lot API 7 : celle du projet d'avant (voir adoptProject)
    setSimulationStatus({});
    hmi_.reset();
    bindHmi();                                   // l'IHM de la sauvegarde de reprise
    projectFolder_ = found.project;              // ... enregistree dans le projet d'origine
    hmiKey_ = found.project;
    rememberPath(projectFolder_);
    bus_.publish(ProjectOpened{project_, report_});
    menus_->SwitchMenu("analysis");
    commands_.markUnsaved();
    recovery_.setProject(projectFolder_, "conception");
    menus_->ShowDialog(std::make_unique<MessageDialog>(
                           "Donn\xC3\xA9" "es recharg\xC3\xA9" "es",
                           "Les modifications gard\xC3\xA9" "es le " + found.autosavedAt + " sont recharg\xC3\xA9" "es.\n\n"
                           "Le projet sur le disque n'a pas chang\xC3\xA9 : enregistre (Ctrl+S) pour les y garder.",
                           MessageDialog::Icon::Info),
                       [](const menu::DialogResult&) {});
}

void App::openStation(const std::string& folder, bool force) {
    stationPending_ = true;
    stationForce_ = force;
    openProjectFolder(folder);
}

bool App::projectFinished(std::string* why) const {
    if (manifest_.state == project::State::Finish) return true;
    if (why) *why = std::string(project::toString(manifest_.state));
    return false;
}

std::vector<std::string> App::masterCandidates() const {
    std::vector<std::string> out;
    if (masterKey_.defined()) out.push_back(masterKey_.stored());
    for (const auto& a : hmi::netinfo::adapters()) {
        if (a.mac.empty() || !project::MasterKey::looksLikeMac(a.mac)) continue;
        project::MasterKey k;
        k.setFromMac(a.mac);
        if (std::find(out.begin(), out.end(), k.stored()) == out.end()) out.push_back(k.stored());
    }
    return out;
}

void App::askUnlock() {
    if (!project_ || projectFolder_.empty() || manifest_.state != project::State::Lock) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back(FormDialog::Field{"Mot de passe", "", "vide : la cl\xC3\xA9 du PC ma\xC3\xAEtre (son adresse MAC)", true, {}});
    menus_->ShowDialog(
        std::make_unique<FormDialog>("dialog.unlockProject", "D\xC3\xA9verrouiller le projet",
            "LOCK : personne ne modifie le projet. D\xC3\xA9verrouiller le repasse en DEV. Le mot de passe du verrou ; "
            "sans lui, laisse vide : si ce PC est le PC ma\xC3\xAEtre (l'adresse MAC d\xC3\xA9" "clar\xC3\xA9" "e au premier lancement, "
            "le \xC2\xAB patron \xC2\xBB), sa cl\xC3\xA9 suffit.",
            std::move(fields), "D\xC3\xA9verrouiller"),
        [this](const menu::DialogResult& r) {
            if (!r.accepted() || !project_) return;
            const auto values = FormDialog::split(r.payload);
            const std::string password = values.empty() ? std::string{} : values.front();
            const auto lock = project::ProjectStore::open(projectFolder_);
            bool ok = false;
            std::string how;
            if (lock && lock->lock.valid()) {
                if (!password.empty() && project::checkPassword(lock->lock, password)) {
                    ok = true;
                    how = "le mot de passe";
                }
                if (!ok)
                    for (const auto& key : masterCandidates())
                        if (project::checkMaster(lock->lock, key)) {
                            ok = true;
                            how = "la cl\xC3\xA9 du PC ma\xC3\xAEtre (adresse MAC)";
                            break;
                        }
            } else {
                ok = true;        // un LOCK sans verrou lisible : rien a verifier
                how = "aucun verrou";
            }
            if (!ok) {
                menus_->ShowDialog(std::make_unique<MessageDialog>(
                                       "D\xC3\xA9verrouillage refus\xC3\xA9",
                                       "Le mot de passe ne correspond pas, et ce PC n'est pas le PC ma\xC3\xAEtre : aucune de ses cartes r\xC3\xA9seau n'a "
                                       "l'adresse MAC du patron.",
                                       MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
            manifest_.state = project::State::Dev;
            (void)project::ProjectStore::save(*project_, manifest_, projectFolder_);
            menus_->ShowDialog(std::make_unique<MessageDialog>(
                                   "Projet d\xC3\xA9verrouill\xC3\xA9",
                                   manifest_.name + " est d\xC3\xA9verrouill\xC3\xA9 (" + how + ") : il repasse en DEV, les modifications sont permises.",
                                   MessageDialog::Icon::Info),
                               [](const menu::DialogResult&) {});
        });
}

std::optional<std::string> App::takeStationRestore(bool* ask) {
    auto out = std::move(stationRestore_);
    stationRestore_.reset();
    if (ask) *ask = stationRestoreAsk_;
    stationRestoreAsk_ = false;
    return out;
}

void App::offerRecovery() {
    const auto& c = recovery_.crashed();
    if (!c || !c->hasData()) return;
    namespace fs = std::filesystem;
    const std::string name = c->project.empty() ? std::string("(sans projet)") : fs::path(c->project).filename().string();
    // Court : il tient dans le paragraphe du dialogue, sans barre de defilement.
    std::string message = "XpgAnalyzer s'est arr\xC3\xAAt\xC3\xA9 brutalement" + (c->lastBeat.empty() ? std::string{} : " vers " + c->lastBeat)
                          + " (PC \xC3\xA9teint, ou logiciel ferm\xC3\xA9 sans passer par Quitter).\n\n"
                          + "Projet : " + name + "\n";
    if (!c->autosave.empty())
        message += "- des modifications non enregistr\xC3\xA9" "es, gard\xC3\xA9" "es le " + c->autosavedAt + " (le projet sur le disque est intact) ;\n";
    if (!c->stationState.empty())
        message += "- l'\xC3\xA9tat du poste d'exploitation (vue, variables, alarmes), gard\xC3\xA9 le " + c->stationAt + ".\n";
    message += "\nVoulez-vous recharger les donn\xC3\xA9" "es pr\xC3\xA9" "c\xC3\xA9" "dentes ?";
    auto dialog = std::make_unique<MessageDialog>("Arr\xC3\xAAt inattendu", message, MessageDialog::Icon::Question, "Recharger");
    dialog->setCancelLabel("Non, repartir \xC3\xA0 neuf");
    menus_->ShowDialog(std::move(dialog), [this](const menu::DialogResult& r) {
        if (!r.accepted()) {
            recovery_.forget();
            return;
        }
        auto found = recovery_.take();
        if (!found) return;
        if (found->mode == "poste" && !found->stationState.empty()) {
            // Le poste repart avec son etat d'avant.
            stationRestore_ = Recovery::readFile(found->stationState);
            stationRestoreAsk_ = false;
            stationPending_ = true;
            openProjectFolder(found->project);
        } else if (!found->autosave.empty()) {
            openRecovered(*found);
        } else {
            openProjectFolder(found->project);
        }
    });
}

// ------------------------------------------------------------------- IHM ----
//
//  Charger celle du dossier, ou en creer une : une vue de demarrage vide, au
//  nom du projet. Rien n'est ecrit tant qu'on n'enregistre pas - ouvrir un
//  projet ne doit pas modifier son dossier.
void App::bindHmi() {
    // 1.12.0 : XPGAnalyser API n'a pas d'IHM (meme dans un dossier de la 1.11 qui en a une).
    if (!core::hasIhm()) {
        hmi_.reset();
        hmiKey_.clear();
        hmiWarnings_.clear();
        hmiUnreadable_.clear();
        return;
    }
    const std::string key = !projectFolder_.empty() ? projectFolder_
                          : !sources_.empty()       ? sources_.front()
                                                    : std::string{};
    if (hmi_ && key == hmiKey_) return;          // la meme : on la garde telle quelle
    hmiKey_ = key;
    hmiWarnings_.clear();
    hmiUnreadable_.clear();

    auto doc = std::make_shared<hmi::Document>();
    bool loaded = false;
    if (!projectFolder_.empty() && hmi::exists(projectFolder_)) {
        hmi::LoadReport report;
        if (auto p = hmi::load(projectFolder_, &report)) {
            doc->project = std::move(*p);
            hmiWarnings_ = std::move(report.warnings);
            doc->history = hmi::loadHistory(projectFolder_, &hmiWarnings_);
            loaded = true;
        } else {
            hmiUnreadable_ = p.error().message();
            hmiWarnings_.push_back("ihm/ illisible : " + hmiUnreadable_
                                   + " - une IHM vide la remplace en m\xC3\xA9moire, et le dossier "
                                     "n'est pas r\xC3\xA9\xC3\xA9" "crit");
        }
    }
    if (!loaded) {
        auto& cfg = doc->project.config;
        cfg.name = !manifest_.name.empty() ? manifest_.name
                 : project_ && !project_->header.projectName.empty() ? project_->header.projectName
                                                                     : std::string("IHM");
        cfg.created = hmi::nowStamp();
        auto start = hmi::makeView(doc->project, "Vue_Accueil");
        cfg.startView = start.id;
        doc->project.views.push_back(std::move(start));
    }
    hmi_ = std::move(doc);
}

core::Status App::saveHmi(const std::string& folder) {
    if (!hmi_ || folder.empty()) return core::ok();
    if (!hmiUnreadable_.empty() && folder == hmiKey_)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "l'IHM de ce dossier n'a pas pu \xC3\xAAtre relue (" + hmiUnreadable_
                          + ") : elle n'est pas r\xC3\xA9\xC3\xA9" "crite, pour ne rien perdre. Corrige ou "
                            "d\xC3\xA9place ihm/, puis rouvre le projet.");
    auto& cfg = hmi_->project.config;
    if (cfg.created.empty()) cfg.created = hmi::nowStamp();
    cfg.modified = hmi::nowStamp();
    if (auto st = hmi::save(hmi_->project, folder); !st) return st;
    // L'historique de la marche (lot 4), borne par les reglages du projet.
    hmi_->history.trim(hmi_->project.history, hmi::wallStamp());
    return hmi::saveHistory(hmi_->history, folder);
}

core::Status App::saveProject() {
    if (!project_)
        return core::fail(core::ErrorCode::InvalidArgument, "no project to save");
    if (projectFolder_.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "the project has no folder yet");
    // Lot 15 : LOCK - personne ne modifie (Projet > Deverrouiller d'abord).
    if (manifest_.state == project::State::Lock)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "projet verrouill\xC3\xA9 (LOCK) : personne ne le modifie - Projet > D\xC3\xA9verrouiller d'abord");

    manifest_.modified = {};
    if (manifest_.name.empty()) manifest_.name = project_->header.projectName;
    manifest_.company    = project_->header.company;
    manifest_.product    = project_->header.product;
    manifest_.dtdVersion = project_->header.dtdVersion;
    if (auto ok = project::ProjectStore::save(*project_, manifest_, projectFolder_); !ok) return ok;
    // L'IHM avec le projet : un projet enregistre sans ses vues serait un
    // projet a moitie enregistre.
    if (auto ok = saveHmi(projectFolder_); !ok)
        return core::fail(ok.error().code,
                          "le programme est enregistr\xC3\xA9, mais pas l'IHM : " + ok.error().message());
    hmiKey_ = projectFolder_;
    commands_.markSaved();
    reopenedFrom_.reset();          // lot API 6 : le DEV est sur le disque
    savedAtMs_ = core::CommandStack::wallMs();
    recovery_.clearAutosave();      // lot 15 : la sauvegarde de reprise ne sert plus
    // Lot 21 : la version automatique, si le projet la demande (Versions >
    // Version automatique). Une version qui echoue n'empeche pas l'enregistrement.
    ProjectSaved saved;
    if (auto store = hmi::ver::open(projectFolder_); store && store->autoMode != hmi::ver::AutoMode::Never) {
        if (auto made = hmi::ver::afterSave(*store, hmi::ver::defaultAuthor()); made && made->has_value())
            saved.autoVersion = "V" + std::to_string((*made)->number);
    }
    bus_.publish(saved);
    return core::ok();
}

// 1.12.0 : le projet d'une autre application - "" : celui-ci s'ouvre ici. Un projet
// de la 1.11 (sans edition) s'ouvre dans les deux : chacune n'y voit que sa moitie.
static std::string editionMismatch(const project::Manifest& m) {
    const std::string name = m.name.empty() ? std::string("Ce projet") : "\xC2\xAB " + m.name + " \xC2\xBB";
    if (m.edition == "ihm" && !core::hasIhm())
        return name + " est un projet IHM : il s'ouvre avec XPGAnalyser IHM.\n\nXPGAnalyser API ouvre les projets de l'automate (Projets\\api).";
    if (m.edition == "api" && !core::hasApi())
        return name + " est un projet de l'automate : il s'ouvre avec XPGAnalyser API.\n\nXPGAnalyser IHM ouvre les projets IHM (Projets\\ihm).";
    return {};
}

void App::openProjectFolder(std::string folder) {
    XPG_PORTEE_TEXTE("App::openProjectFolder", std::filesystem::path(folder).filename().string());   // 1.10.2 (CR)
    core::crash::setOpenDocuments(std::filesystem::path(folder).filename().string());   // le nom seul, pour le rapport
    auto opened = project::ProjectStore::open(folder);
    if (!opened) {
        menus_->ShowDialog(std::make_unique<MessageDialog>("Impossible d'ouvrir",
                                                           opened.error().message(),
                                                           MessageDialog::Icon::Error),
                           [](const menu::DialogResult&) {});
        return;
    }
    // 1.12.0 : un projet de l'autre application s'ouvre avec elle.
    if (const std::string why = editionMismatch(opened->manifest); !why.empty()) {
        menus_->ShowDialog(std::make_unique<MessageDialog>("Un projet de l'autre application", why, MessageDialog::Icon::Warning),
                           [](const menu::DialogResult&) {});
        return;
    }
    if (!opened->needsPassword) {
        adoptProject(opened->project, opened->manifest, std::move(folder));
        return;
    }

    // Locked: ask, and accept either the password or this machine's master key.
    std::vector<FormDialog::Field> fields;
    fields.push_back(FormDialog::Field{"Password", "", "", true, {}});
    menus_->ShowDialog(
        std::make_unique<FormDialog>("dialog.unlock", "Project is locked",
                                     "'" + opened->manifest.name + "' is locked. Enter its "
                                     "password, or leave it empty to try this machine's "
                                     "administrator key.",
                                     std::move(fields), "Unlock"),
        [this, folder](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto values = FormDialog::split(r.payload);
            const auto password = values.empty() ? std::string{} : values.front();

            // La cle maitre : celle des reglages (le patron declare au premier
            // lancement), puis - lot 15 - celle de chaque carte reseau de ce PC.
            auto opened2 = project::ProjectStore::open(folder, password, masterKey_.stored());
            if (!opened2)
                for (const auto& key : masterCandidates()) {
                    opened2 = project::ProjectStore::open(folder, password, key);
                    if (opened2) break;
                }
            if (!opened2) {
                menus_->ShowDialog(std::make_unique<MessageDialog>(
                                       "Wrong password",
                                       "The password does not match, and the administrator key "
                                       "for this machine does not either.",
                                       MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
            adoptProject(opened2->project, opened2->manifest, folder);
        });
}

// =============================================================================
//  1.11.2 (UNI, decision 195) : UNE SEULE INSTANCE - les demandes d'une autre
// -----------------------------------------------------------------------------
//  La deuxieme instance a passe sa ligne de commande (main.cpp) et s'est arretee.
//  Ici : la fenetre revient au premier plan ; un projet demande s'ouvre comme depuis
//  l'accueil (un dossier de projet, un export, plusieurs fichiers ensemble ; --ihm :
//  le poste), apres la question d'enregistrer le projet en cours s'il est modifie -
//  la meme que l'accueil (StartupScreen::confirmReplace). Une question deja a
//  l'ecran, un tutoriel en cours : la demande attend, et la barre d'etat le dit.
// =============================================================================
namespace {
std::string instanceLeaf(const std::string& path) {
    const std::filesystem::path p(path);
    std::string leaf = p.filename().string();
    if (leaf.empty()) leaf = p.parent_path().filename().string();   // "C:\Projets\Tunnel\"
    return leaf.empty() ? path : leaf;
}
bool instanceSamePlace(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}
} // namespace

void App::bringToFront() {
#if XPG_HAVE_SDL3
    if (!window_) return;
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) SDL_RestoreWindow(window_);
    // Sous Windows : SetForegroundWindow, que la deuxieme instance a permis
    // (AllowSetForegroundWindow) ; refuse, le bouton de la barre des taches clignote.
    core::instance::bringToFront(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    SDL_RaiseWindow(window_);
#endif
}

void App::takeInstanceRequests() {
    auto* instance = core::instance::current();
    if (!instance) return;
    for (auto& request : instance->take()) {
        std::vector<std::string> files;
        std::string station;
        for (std::size_t i = 0; i < request.args.size(); ++i) {
            const std::string& a = request.args[i];
            if ((a == "--poste" || a == "--ihm") && i + 1 < request.args.size()) station = request.args[++i];
            else if (a.rfind("--", 0) == 0) continue;   // les autres options ne changent rien ici
            else if (!a.empty()) files.push_back(a);
        }
        std::fprintf(stderr, "[instance] une autre instance a \xC3\xA9t\xC3\xA9 lanc\xC3\xA9" "e (%zu argument(s))\n", request.args.size());
        bringToFront();
        if (!station.empty() || !files.empty()) {
            instancePaths_ = station.empty() ? std::move(files) : std::vector<std::string>{station};
            instanceStation_ = !station.empty();
            instanceWaitSaid_ = false;          // la derniere demande l'emporte
        } else {
            bus_.publish(StatusNotice{"XPGAnalyser est d\xC3\xA9j\xC3\xA0 ouvert : une seule fen\xC3\xAAtre \xC3\xA0 la fois.", 6.0});
        }
    }
    if (!menus_) return;
    // La question posee n'est plus a l'ecran sans avoir repondu (la pile videe par un
    // changement d'ecran) : apres 60 tours, on ne l'attend plus. (ShowDialog est mis en
    // file : le premier tour apres, elle n'y est pas encore.)
    if (instanceAsked_ && !menus_->questionWaiting()) {
        if (++instanceAskIdle_ > 60) instanceAsked_ = false;
    } else {
        instanceAskIdle_ = 0;
    }
    if (instancePaths_.empty() || instanceAsked_) return;
    const std::string what = instanceLeaf(instancePaths_.front());
    if (stationActive_) {
        // Le poste d'exploitation en marche (souvent en kiosque) : rien ne s'ouvre par-dessus.
        bus_.publish(StatusNotice{"Le poste d'exploitation est en marche : " + what
                                  + " ne s'ouvre pas ici (Passer en conception d'abord).", 10.0});
        instancePaths_.clear();
        return;
    }
    // Une question attend deja, la fermeture se demande, un tutoriel tourne : apres.
    const bool tutorial = tutorials::player() != nullptr;
    if (menus_->questionWaiting() || closeAsked_ || tutorial) {
        if (!instanceWaitSaid_) {
            instanceWaitSaid_ = true;
            bus_.publish(StatusNotice{what + " s'ouvrira ici d\xC3\xA8s que "
                                      + std::string(tutorial ? "le tutoriel sera fini." : "la question ouverte aura sa r\xC3\xA9ponse."), 10.0});
        }
        return;
    }
    const std::vector<std::string> paths = std::move(instancePaths_);
    instancePaths_.clear();
    const bool station = instanceStation_;
    instanceWaitSaid_ = false;
    // Le projet deja ouvert ici : on le garde tel quel (le relire perdrait ce qui n'est pas enregistre).
    if (!station && paths.size() == 1 && project_ && instanceSamePlace(paths.front(), projectFolder_)) {
        bus_.publish(StatusNotice{what + " est d\xC3\xA9j\xC3\xA0 ouvert ici.", 6.0});
        return;
    }
    const bool unsaved = project_ && !projectFolder_.empty() && commands_.isModified();
    if (!unsaved) {
        openRequested(paths, station);
        return;
    }
    const std::string folderName = std::filesystem::path(projectFolder_).filename().string();
    const std::string name = !manifest_.name.empty() ? manifest_.name : folderName;
    const std::string keep = "Les enregistrer d'abord";
    const std::string drop = "Les abandonner";
    std::string why = "XPGAnalyser est d\xC3\xA9j\xC3\xA0 ouvert : " + what
                    + " a \xC3\xA9t\xC3\xA9 demand\xC3\xA9 dans cette fen\xC3\xAAtre (une seule \xC3\xA0 la fois).\n\n"
                    + name + " a des modifications qui ne sont pas encore enregistr\xC3\xA9" "es";
    if (const auto last = commands_.undoLabel(); !last.empty()) why += " (la derni\xC3\xA8re : " + last + ")";
    why += ". Ce que tu ouvres le remplace en m\xC3\xA9moire : choisis ce qu'elles deviennent.";
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Les modifications de " + name, keep, "", false, {keep, drop}});
    instanceAsked_ = true;
    menus_->ShowDialog(
        std::make_unique<FormDialog>("dialog.instanceOuvrir", "Ouvrir " + what, why, std::move(fields), "Continuer"),
        [this, keep, paths, station](const menu::DialogResult& r) {
            instanceAsked_ = false;
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v.front() == keep) {
                if (auto st = saveProject(); !st) {
                    menus_->ShowDialog(std::make_unique<MessageDialog>(
                                           "Enregistrement impossible",
                                           st.error().message() + "\n\nRien n'a \xC3\xA9t\xC3\xA9 ouvert : les modifications sont toujours en m\xC3\xA9moire.",
                                           MessageDialog::Icon::Error),
                                       [](const menu::DialogResult&) {});
                    return;
                }
            }
            openRequested(paths, station);
        });
}

void App::openRequested(const std::vector<std::string>& paths, bool station) {
    if (paths.empty()) return;
    if (station) {
        openStation(paths.front());
        return;
    }
    // Comme l'accueil (StartupScreen::openPath) : un dossier de projet parmi eux, lui ;
    // sinon l'export (ou les exports ensemble).
    for (const auto& p : paths)
        if (project::ProjectStore::isProjectFolder(p)) {
            openProjectFolder(p);
            return;
        }
    if (paths.size() == 1) openPath(paths.front());
    else importer_->importAsync(paths);
}

// =============================================================================
//  1.11.2 (UNI, decision 204) : LA QUESTION DE L'ACCUEIL, HORS DE L'ACCUEIL
// -----------------------------------------------------------------------------
//  Ouvrir un projet recent par le menu de la puce projet remplacait le projet en
//  memoire sans un mot : ses modifications non enregistrees etaient perdues.
//  L'accueil le demandait deja (StartupScreen::confirmReplace) ; c'est la meme
//  question, mot pour mot (et le meme identifiant, dialog.startLeave).
//  1.11.2 (UNI, decision 218) : "Ouvrir..." (file.open) et "Fermer le projet" de
//  la puce passent aussi par elle. closing : la meme question, sa derniere phrase
//  dit que le projet se ferme ; l'enregistrement impossible : il reste ouvert.
// =============================================================================
void App::confirmReplace(std::function<void()> then, bool closing) {
    const bool unsaved = project_ && !projectFolder_.empty() && commands_.isModified();
    if (!unsaved) {
        if (then) then();
        return;
    }
    const std::string name = !manifest_.name.empty() ? manifest_.name : instanceLeaf(projectFolder_);
    const std::string keep = "Les enregistrer d'abord";
    const std::string drop = "Les abandonner";
    std::string why = name + " a des modifications qui ne sont pas encore enregistr\xC3\xA9" "es";
    if (const auto last = commands_.undoLabel(); !last.empty()) why += " (la derni\xC3\xA8re : " + last + ")";
    why += closing ? ". Fermer le projet le retire de la m\xC3\xA9moire : choisis ce qu'elles deviennent."
                   : ". Ce que tu ouvres le remplace en m\xC3\xA9moire : choisis ce qu'elles deviennent.";
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Les modifications de " + name, keep, "", false, {keep, drop}});
    menus_->ShowDialog(
        std::make_unique<FormDialog>("dialog.startLeave", "Modifications non enregistr\xC3\xA9" "es", why, std::move(fields), "Continuer"),
        [this, keep, closing, next = std::move(then)](const menu::DialogResult& r) {
            if (!r.accepted()) return;          // Annuler : rien ne s'ouvre (ne se ferme), rien ne se perd
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v.front() == keep) {
                if (auto st = saveProject(); !st) {
                    menus_->ShowDialog(std::make_unique<MessageDialog>(
                                           "Enregistrement impossible",
                                           st.error().message() + (closing
                                               ? "\n\nLe projet n'a pas \xC3\xA9t\xC3\xA9 ferm\xC3\xA9 : les modifications sont toujours en m\xC3\xA9moire."
                                               : "\n\nRien n'a \xC3\xA9t\xC3\xA9 ouvert : les modifications sont toujours en m\xC3\xA9moire."),
                                           MessageDialog::Icon::Error),
                                       [](const menu::DialogResult&) {});
                    return;
                }
            }
            if (next) next();
        });
}

// An import on its own is read-only: nothing can be created or saved until the
// project has a folder. Rather than let the user discover that by finding every
// command greyed out, offer the folder straight away.
void App::offerProjectFolder(const std::string& importedFrom) {
    namespace fs = std::filesystem;
    if (!projectFolder_.empty() || !document_) return;

    const auto source = fs::path(importedFrom);
    const auto suggested = (source.parent_path() / source.stem()).string();

    std::vector<FormDialog::Field> f;
    // Lot API 8 : en francais, comme le reste de l'appli.
    f.push_back({"Nom", document_->header.projectName.empty() ? source.stem().string()
                                                              : document_->header.projectName,
                 "", false, {}});
    f.push_back({"Dossier", suggested, "", false, {}});

    menus_->ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.adopt", "Cr\xC3\xA9" "er un projet \xC3\xA0 partir de cet export",
            "Le fichier est lu. Un dossier de projet le rend modifiable et enregistrable : un "
            "fichier .st par section, les d\xC3\xA9" "clarations en tableaux lisibles, et src/ "
            "r\xC3\xA9g\xC3\xA9n\xC3\xA9r\xC3\xA9 pour Control Expert. Annuler : le parcourir en lecture seule.",
            std::move(f), "Cr\xC3\xA9" "er le projet"), 1, ui::newFolder(suggested, "Dossier du projet")),
        [this](const menu::DialogResult& r) {
            if (!r.accepted() || !document_) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2 || v[1].empty()) return;

            project::Manifest m;
            m.name         = v[0];
            m.state        = project::State::Dev;
            m.cpuReference = document_->hardware.cpuReference;
            m.cpuFirmware  = document_->hardware.cpuFirmware;
            m.company      = document_->header.company;
            m.product      = document_->header.product;
            m.dtdVersion   = document_->header.dtdVersion;

            if (auto ok = project::ProjectStore::save(*document_, m, v[1]); !ok) {
                menus_->ShowDialog(std::make_unique<MessageDialog>("Impossible de cr\xC3\xA9" "er",
                                                                   ok.error().message(),
                                                                   MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
            manifest_      = m;
            projectFolder_ = v[1];
            rememberPath(v[1]);
            // L'IHM suit le projet dans son dossier : elle y est ecrite tout de
            // suite, et lui appartient desormais.
            if (auto ok = saveHmi(v[1]); !ok)
                menus_->ShowDialog(std::make_unique<MessageDialog>("IHM non enregistr\xC3\xA9" "e",
                                                                   ok.error().message(),
                                                                   MessageDialog::Icon::Warning),
                                   [](const menu::DialogResult&) {});
            else
                hmiKey_ = v[1];
        });
}

// Le pendant de rememberPath. Un projet supprime du disque doit sortir de la
// liste : une ligne qui mene a un dossier qui n'existe plus est une invitation a
// cliquer pour rien.
void App::forgetPath(const std::string& path) {
    recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
    settings_.setList("recent.projects", recent_);
}

void App::rememberPath(std::string path) {
    if (path.empty()) return;
    recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
    recent_.insert(recent_.begin(), std::move(path));
    if (recent_.size() > 12) recent_.resize(12);
}

// UN SEUL ENDROIT DECIDE. Le bouton de la barre d'outils et le raccourci
// clavier passent tous les deux par ici : s'ils avaient chacun leur chemin, l'un
// des deux finirait par oublier de poser la question.
//
// ET LA QUESTION N'EST PAS "avez-vous fait quelque chose". canUndo() y repond,
// et ce n'est pas la meme chose : annuler jusqu'au point de depart le laisse a
// vrai alors que le fichier est intact, et enregistrer ne le remet pas a faux
// alors que plus rien n'est en attente.
// Le dossier ou vont les affaires. Cree au premier besoin : un chemin propose
// dans un dialogue doit exister, sinon la premiere creation echoue sur une
// erreur de dossier absent que personne ne comprend.
std::filesystem::path App::projectsRoot() {
    namespace fs = std::filesystem;
    // 1.8.0 : la version installee les range ou dit XPGAnalyser.ini (app/Dossiers.hpp) ;
    // sinon (developpement, portable, sessions rejouees) projets\ du dossier de travail.
    const std::string regle = dossiers::actif(dossiers::Cle::Projets);
    auto racine = regle.empty() ? fs::current_path() / "projets" : dossiers::cheminDe(regle);
    // 1.12.0 : deux applications, deux rangements - projets\api, projets\ihm.
    if (const auto key = core::editionKey(); !key.empty()) racine /= std::string(key);
    std::error_code ec;
    fs::create_directories(racine, ec);
    return ec ? fs::current_path() : racine;
}

// 1.12.0 : XPGAnalyser IHM - un nouveau projet IHM : un dossier (Projets\ihm\<Nom>), son
// manifeste (edition = ihm), une vue de demarrage. Pas de programme : les variables de
// l'IHM sont les siennes, liees aux adresses de ses equipements.
void App::newHmiProject() {
    namespace fs = std::filesystem;
    std::vector<FormDialog::Field> f;
    f.push_back({"Nom", "Nouvelle IHM", "", false, {}});
    f.push_back({"Dossier", dossiers::utf8De(projectsRoot() / "Nouvelle IHM"), "sous Projets\\ihm par d\xC3\xA9" "faut", false, {}});
    menus_->ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.newProject", "Nouveau projet IHM",
                                   "Cr\xC3\xA9" "e le dossier d'une IHM : une vue de d\xC3\xA9marrage, ses variables, ses \xC3\xA9quipements. "
                                   "Pas de programme d'automate : les variables de l'IHM se lient aux adresses de ses \xC3\xA9quipements (Modbus).",
                                   std::move(f), "Cr\xC3\xA9" "er"),
                               1, ui::newFolder(dossiers::utf8De(projectsRoot()), "Dossier du nouveau projet IHM")),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2 || v[0].empty() || v[1].empty()) return;
            std::error_code ec;
            if (fs::exists(dossiers::cheminDe(v[1]) / "project.xpgproj", ec)) {
                menus_->ShowDialog(std::make_unique<MessageDialog>("Impossible de cr\xC3\xA9" "er", v[1] + " est d\xC3\xA9j\xC3\xA0 un projet.",
                                                                   MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
            auto p = std::make_shared<domain::Project>();
            p->header.projectName = v[0];
            p->header.sourceFile = v[1];
            project::Manifest m;
            m.name = v[0];
            m.state = project::State::New;
            m.edition = "ihm";
            m.company.clear();
            m.dtdVersion.clear();
            if (auto ok = project::ProjectStore::save(*p, m, v[1]); !ok) {
                menus_->ShowDialog(std::make_unique<MessageDialog>("Impossible de cr\xC3\xA9" "er", ok.error().message(), MessageDialog::Icon::Error),
                                   [](const menu::DialogResult&) {});
                return;
            }
            adoptProject(p, m, v[1]);
            // Le dossier est un projet IHM complet des sa creation (ihm/ et sa vue de demarrage).
            if (auto ok = saveHmi(v[1]); ok) hmiKey_ = v[1];
        });
}

void App::goHome() {
    if (!commands_.isModified()) {
        menus_->SwitchMenu("startup");
        return;
    }

    std::string message = "Des modifications n'ont pas ete enregistrees.\n\n";
    if (const auto label = commands_.undoLabel(); !label.empty())
        message += "La derniere est : " + label + "\n\n";
    message += "Revenir au menu principal les laisse en memoire - elles ne sont "
               "PAS perdues - mais elles ne seront ecrites que lorsque vous "
               "enregistrerez.";

    menus_->ShowDialog(
        std::make_unique<MessageDialog>("Revenir au menu principal ?", message,
                                        MessageDialog::Icon::Question, "Revenir"),
        [this](const menu::DialogResult& r) {
            if (r.accepted()) menus_->SwitchMenu("startup");
        });
}

void App::setTheme(std::string_view name) {
    // Le repli est le CLAIR, comme le defaut. Un nom inconnu - un fichier de
    // preferences d'une version precedente, une faute de frappe - doit ramener
    // le meme theme que la premiere ouverture, pas un autre.
    //
    // Lot API 6 : neuf themes, par leur cle ("Dark", "Night") ou leur libelle
    // ("Sombre", "Nuit") - Theme::byName. La preference retient la CLE.
    theme_ = ui::Theme::byName(name);
    settings_.set("view.theme", theme_.name);
    bus_.publish(ThemeChanged{theme_.name});
}

void App::settleReopened() {
    if (!reopenedFrom_ || commands_.isModified()) return;
    manifest_.state = *reopenedFrom_;
    reopenedFrom_.reset();
    bus_.publish(StatusNotice{"Plus rien de modifi\xC3\xA9 : le projet est de nouveau " + std::string(project::toString(manifest_.state)) + ".", 8.0});
}

core::Status App::setProjectState(project::State state, const std::string& password) {
    if (!project_ || projectFolder_.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "le projet n'a pas encore de dossier");
    if (state == project::State::Lock && password.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "verrouiller demande un mot de passe");
    manifest_.state = state;
    reopenedFrom_.reset();
    project::LockRecord lock;
    if (state == project::State::Lock) lock = project::makeLock(password, masterKey_.stored());
    if (auto ok = project::ProjectStore::save(*project_, manifest_, projectFolder_, lock); !ok) return ok;
    if (auto ok = saveHmi(projectFolder_); !ok) return ok;
    hmiKey_ = projectFolder_;
    commands_.markSaved();
    savedAtMs_ = core::CommandStack::wallMs();
    return core::ok();
}

void App::refreshWindowIcon() {
#if XPG_HAVE_SDL3
    if (!window_) return;
    const domain::ProjectIcon none;
    const auto& want = document_ ? document_->icon : none;
    if (want == windowIcon_) return;
    windowIcon_ = want;
    if (want.empty()) return;       // le systeme garde celle du programme
    auto px = project::icon::rgba(want);
    if (SDL_Surface* surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32, px.data(), 32 * 4)) {
        SDL_SetWindowIcon(window_, surface);
        SDL_DestroySurface(surface);
    }
#endif
}

void App::showThemeGallery() {
    ThemeGalleryDialog::Hosts h;
    h.current = [this] { return theme_.name; };
    h.apply = [this](const std::string& key) { setTheme(key); };
    // ---- Lot API 8 : themes ----
    // L'apercu de l'editeur : un theme pas encore enregistre, sur tout l'ecran,
    // sans toucher la preference (Annuler rend le theme d'avant par apply).
    h.preview = [this](const ui::Theme& t) {
        theme_ = t;
        bus_.publish(ThemeChanged{theme_.name});
    };
    h.folder = [] { return ui::UserThemes::folder(); };
    // ---- fin Lot API 8 ----
    menus_->ShowDialog(std::make_unique<ThemeGalleryDialog>(std::move(h)), [this](const menu::DialogResult& r) {
        // Garder : la preference est deja posee par setTheme ; on l'ecrit tout de suite.
        if (r.accepted()) settings_.save();
    });
}

int App::run() {
#if XPG_HAVE_SDL3
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    bool running = true;
    // Lot API 7 : ou poser les onglets detaches, et quelle fenetre montrer
    // quand une question y attend.
    detached_.setMainWindow(window_);

    while (running && !quit_) {
        const auto now = Clock::now();
        // Une session rejouee (--script) marche a l'HORLOGE FIXE : 1/30 s par
        // image, quelle que soit la vitesse de la machine. Les fondus, les
        // clignotements, les messages temporaires et les cycles du simulateur
        // tombent alors au meme endroit a chaque rejeu : les captures sont
        // reproductibles.
        const double dt = script_ ? 1.0 / 30.0 : std::chrono::duration<double>(now - last).count();
        last = now;
        time_ += dt;

        core::crash::heartbeat();   // 1.10.2 (CR) : le battement (core/CrashGuard.hpp)
        if (double stalled = 0; core::crash::takeRecovery(stalled))   // et la note de reprise apres un blocage
            bus_.publish(StatusNotice{crashdialogs::recoveryNote(stalled), 12.0});
        pumpEvents(running);
        takeInstanceRequests();     // 1.11.2 (UNI) : une autre instance a ete lancee
        frame(dt);
    }
    // Une session rejouee dont une commande n'a pas trouve sa cible : le dire
    // a qui l'a lancee (un script de CI, un .bat).
    return script_ && script_->failures() > 0 ? 3 : 0;
#else
    return 1;
#endif
}

#if XPG_HAVE_SDL3
namespace {
// Lot API 7 : la fenetre d'ou vient un evenement, par son numero - y compris
// une fenetre deja detruite (SDL_GetWindowFromEvent rend alors nul, comme pour
// un evenement qui n'est d'aucune fenetre). 0 : d'aucune fenetre.
SDL_WindowID eventWindowId(const SDL_Event& e) {
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) return e.window.windowID;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:               return e.key.windowID;
        case SDL_EVENT_TEXT_EDITING:         return e.edit.windowID;
        case SDL_EVENT_TEXT_INPUT:           return e.text.windowID;
        case SDL_EVENT_MOUSE_MOTION:         return e.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:      return e.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:          return e.wheel.windowID;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:        return e.drop.windowID;
        default:                             return 0;
    }
}
} // namespace
#endif

void App::pumpEvents(bool& running) {
#if XPG_HAVE_SDL3
    SDL_Event e;
    const SDL_WindowID mainId = window_ ? SDL_GetWindowID(window_) : 0;
    // Fermer l'application (la croix, Alt+F4).
    const auto closeAsked = [&] {
        // Lot 14 : le kiosque refuse Alt+F4 (le gardien demande le mot de passe).
        if (quitGuard_ && quitGuard_()) return;
        // Lot 19 : des modifications non enregistrees - la question d'abord.
        // (Aujourd'hui fermer la fenetre les perdait sans un mot.)
        if (!stationActive_ && project_ && !projectFolder_.empty() && commands_.isModified()
            && !scripted()) {
            requestClose();
            return;
        }
        running = false;
    };
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            closeAsked();
            continue;
        }
        // Lot API 7 : LES ONGLETS DETACHES. Ce qui vient d'une de leurs fenetres
        // va a elle, et a rien d'autre : sa croix rend la page, la souris qui
        // la quitte n'y laisse rien de survole, le reste va a sa page.
        const SDL_WindowID windowId = eventWindowId(e);
        if (windowId != 0 && windowId != mainId) {
            if (detached_.ownsWindow(windowId)) {
                if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) (void)detached_.closeRequested(windowId);
                else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) detached_.mouseLeft(windowId);
                else if (auto ev = translate(e)) {
                    // Un .XPG / .XHW lache sur une fenetre detachee : la question (et
                    // l'import) se posent dans la fenetre principale, comme partout.
                    const auto* drop = std::get_if<ui::FileDropped>(&*ev);
                    // ---- Lot API 8 : glisser de fichiers, 2e partie ---- "Que faire de ces
                    // fichiers ?" ouvert : ce qu'on lache (meme ici) s'y ajoute (dropFile).
                    if (drop && menus_ && dynamic_cast<DropFilesDialog*>(menus_->top())) {
                        dropFile(*drop);
                        if (window_) SDL_RaiseWindow(window_);
                        continue;
                    }
                    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
                    // Lot API 8 : sauf quand une question attend (un dialogue de cette
                    // fenetre prend ce qu'on y lache, comme dans la principale).
                    if (drop && project_ && !stationActive_ && MainAnalysisScreen::importableFile(drop->path)
                        && !menus_->questionWaiting()) {
                        droppedFiles_.push_back(drop->path);
                        if (window_) SDL_RaiseWindow(window_);
                    } else {
                        (void)detached_.routeEvent(windowId, *ev);
                    }
                }
                continue;
            }
            // Une fenetre qui n'est plus (un onglet qui vient de rentrer) : ses
            // derniers evenements, encore dans la file, ne vont nulle part - pas
            // a l'ecran principal, a des coordonnees qui ne sont pas les siennes.
            if (SDL_GetWindowFromID(windowId) == nullptr) continue;
        }
        // La croix de la fenetre principale quand une autre fenetre est ouverte :
        // SDL n'envoie SDL_EVENT_QUIT qu'a la fermeture de la DERNIERE.
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && windowId == mainId && mainId != 0
            && (detached_.count() > 0 || !screenWindows_.empty())) {
            closeAsked();
            continue;
        }
        // Lot 14 : les fenetres des ecrans secondaires montrent, elles ne
        // commandent rien - leurs evenements ne vont pas aux ecrans.
        if (!screenWindows_.empty() && mainId) {
            SDL_Window* from = SDL_GetWindowFromEvent(&e);
            if (from && from != window_) continue;
        }
        if (stationActive_ && hmi_ && hmi_->project.station.kiosk && e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11) continue;
        // SdlEventPump::translate() converts SDL_Event -> ui::InputEvent; the
        // widget tree never sees an SDL type.
        if (auto ev = translate(e)) {
            // F12 : une capture de la fenetre, dans captures/. Avant les ecrans :
            // aucun ne s'en sert, et elle doit marcher dans un dialogue aussi.
            // 1.12.2 : sauf dans un editeur de code au profil Visual Studio : F12 y va a la
            // definition (Maj+F12 aux references).
            if (const auto* k = std::get_if<ui::KeyDown>(&*ev);
                k != nullptr && k->key == ui::Key::F12 && k->mods.none() && !k->repeat) {
                auto* code = ui::MultiLineText::focusedCodeEditor();
                if (!(code && code->claimsKey(*k))) {
                    requestCapture(defaultCapturePath());
                    continue;
                }
            }
            // Lot 8 : F11, le plein ecran sans bordure (et retour).
            // Lot API 8 : sauf quand Simulation > Debogage est a l'ecran : F11 y fait un cycle.
            if (const auto* k = std::get_if<ui::KeyDown>(&*ev);
                k != nullptr && k->key == ui::Key::F11 && k->mods.none() && !k->repeat && window_
                && !SimDebugPane::claimsKey(ui::Key::F11)
                && !HmiSimulationPane::claimsKey(ui::Key::F11)) {   // 1.10 : le plein ecran de l'IHM
                const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(window_, !full);
                continue;
            }
            // ---- Lot API 8 : les dialogues dans la fenetre detachee ----
            // La question attend dans une fenetre detachee : la principale ne
            // prend rien (sa croix, plus haut, pose toujours la question
            // habituelle) ; un clic ou une touche amene cette fenetre devant.
            if (detached_.holdMainEvent(*ev)) continue;
            // ---- fin lot API 8 ----
            // Lot 7 : un fichier glisse dans la fenetre (voir dropFile).
            if (const auto* drop = std::get_if<ui::FileDropped>(&*ev)) {
                dropFile(*drop);
                continue;
            }
            // 1.11.1 (T1, R111-18) : la bulle des nouveautes, le calque du
            // tutoriel, puis les ecrans (voir deliverClientEvent).
            deliverClientEvent(*ev);
        }
    }
#else
    running = false;
#endif
}

void App::deliverClientEvent(const ui::InputEvent& e) {
    // 1.10 (chantier P) : la bulle de Me montrer prend ses clics ; un clic
    // sur un element marque NOUVEAU l'utilise (et passe dessous).
    if (noveltyCenter().handle(e) == ui::EventResult::Consumed) return;
    // 1.11 (T1) : un tutoriel en cours mange la souris et le clavier
    // pendant la lecture ; en "A toi", il ne garde que sa barre.
    if (tutorials::event(e)) return;
    if (menus_) (void)menus_->HandleEvent(e);
}

// ----------------------------------------------- lot 7 : deposer des fichiers ----
void App::dropFile(const ui::FileDropped& drop) {
    auto* top = menus_ ? menus_->top() : nullptr;
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    // "Que faire de ces fichiers ?" est au-dessus : le fichier lache s'y ajoute
    // (une ligne de plus, ses cases comme au premier depot).
    if (auto* dropDialog = dynamic_cast<DropFilesDialog*>(top); dropDialog && !drop.path.empty()) {
        (void)dropDialog->addFiles({drop.path});
        return;
    }
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    const bool workspace = project_ && !stationActive_ && !drop.path.empty() && top;
    if (auto* screen = dynamic_cast<MainAnalysisScreen*>(top); screen && workspace) {
        // Un volet qui prend des fichiers (l'image d'une ressource, le champ
        // d'une macro) sous la souris : il le garde, comme avant. 1.11.2 (decision
        // 188) : sauf un paquet (.xpgvues, .xpgsymboles...) : il ouvre la fenetre d'import.
        if (!HmiImportDialog::isPackageFile(drop.path) && screen->HandleEvent(drop) == ui::EventResult::Consumed) return;
        droppedFiles_.push_back(drop.path);
        return;
    }
    // Un ecran plein au-dessus de l'espace de travail (l'aide, les reglages) :
    // s'il ne le prend pas, un .XPG ou un .XHW y ramene - l'import se demande la.
    // ---- Lot API 8 : glisser n'importe quel fichier : tout fichier (pas un dossier) aussi ----
    std::error_code dropEc;
    const bool anyFile = !drop.path.empty() && std::filesystem::is_regular_file(std::filesystem::path(drop.path), dropEc);
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----
    if (workspace && top->traits().kind == menu::MenuKind::Screen && !dynamic_cast<StartupScreen*>(top)
        && menus_->contains("analysis") && (MainAnalysisScreen::importableFile(drop.path) || anyFile)) {
        if (top->HandleEvent(drop) == ui::EventResult::Consumed) return;
        droppedFiles_.push_back(drop.path);
        return;
    }
    // L'accueil (il ouvre ce qu'on lui depose), un dialogue, le poste : comme avant.
    if (menus_) (void)menus_->HandleEvent(drop);
}

// ---- Lot API 8 : glisser n'importe quel fichier ----
bool App::queueDroppedFile(const std::string& path) {
    if (!project_ || stationActive_ || path.empty() || !menus_ || !menus_->contains("analysis")) return false;
    droppedFiles_.push_back(path);
#if XPG_HAVE_SDL3
    if (window_) SDL_RaiseWindow(window_);
#endif
    return true;
}
// ---- fin Lot API 8 : glisser n'importe quel fichier ----

void App::deliverDroppedFiles() {
    if (droppedFiles_.empty()) return;
    auto* screen = dynamic_cast<MainAnalysisScreen*>(menus_->top());
    if (!screen && project_ && menus_->contains("analysis") && dropWait_ < 3) {
        // L'ecran du dessus rend la main ; les fichiers attendent l'image suivante.
        if (dropWait_++ == 0) menus_->PopTo("analysis");
        return;
    }
    auto files = std::move(droppedFiles_);
    droppedFiles_.clear();
    dropWait_ = 0;
    if (screen && project_) screen->filesDropped(std::move(files));
}

// ------------------------------------------------------ lot 14 : le poste ----
HmiSimulationPane* App::liveHmiPane() const {
    auto* top = menus_ ? menus_->top() : nullptr;
    if (auto* station = dynamic_cast<StationScreen*>(top)) return station->pane();
    auto* root = top ? top->widgetRoot() : nullptr;
    // Lot 7 : le centre est une TabArea (des groupes d'onglets) ; le volet de
    // simulation IHM peut etre dans n'importe quel groupe - le premier trouve.
    auto* centre = root ? dynamic_cast<ui::TabArea*>(root->findById("analysis.centre")) : nullptr;
    if (!centre || !centre->tabCount()) return nullptr;
    if (auto* current = dynamic_cast<HmiSimulationPane*>(centre->page(centre->currentIndex()))) return current;
    for (std::size_t i = 0; i < centre->tabCount(); ++i)
        if (auto* pane = dynamic_cast<HmiSimulationPane*>(centre->page(i)); pane && pane->visible()) return pane;
    return nullptr;
}

void App::enterStation() {
    if (!hmi_ || stationActive_) return;
    stationActive_ = true;
    recovery_.setProject(projectFolder_, "poste");     // lot 15 : la reprise suit le mode
    // Essayer le poste depuis la conception : par-dessus elle, qu'on retrouve
    // telle qu'on l'a laissee.
    menus_->PushMenu("station");
}

void App::leaveStation() {
    stationActive_ = false;
    recovery_.setProject(projectFolder_, "conception");   // lot 15
    quitGuard_ = nullptr;
    closeScreenWindows();
    setStationWindow(false, false);
    // Lance par --ihm, il n'y a rien dessous : la conception s'ouvre.
    if (menus_->contains("analysis")) menus_->PopTo("analysis");
    else menus_->SwitchMenu("analysis");
}

void App::setStationWindow(bool fullScreen, bool hideCursor) {
#if XPG_HAVE_SDL3
    if (!window_) return;
    // Une session rejouee garde sa taille (--size) : ses captures restent comparables.
    if (!options_.sizeGiven) SDL_SetWindowFullscreen(window_, fullScreen);
    if (hideCursor) SDL_HideCursor();
    else SDL_ShowCursor();
#else
    (void)fullScreen;
    (void)hideCursor;
#endif
}

int App::displayCount() const {
#if XPG_HAVE_SDL3
    int n = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) SDL_free(ids);
    return n;
#else
    return 0;
#endif
}

gfx::IRenderer* App::openScreenWindow(int display, int w, int h, const std::string& title, bool fullScreen) {
#if XPG_HAVE_SDL3
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    SDL_Rect r{0, 0, std::max(320, w), std::max(200, h)};
    const bool exists = ids && display >= 1 && display <= n && SDL_GetDisplayBounds(ids[display - 1], &r);
    if (ids) SDL_free(ids);
    if (!exists) {
        // Pas de moniteur de ce rang : une fenetre ordinaire, a la taille de la vue.
        r = {60 * static_cast<int>(screenWindows_.size() + 1), 60 * static_cast<int>(screenWindows_.size() + 1), std::max(320, w), std::max(200, h)};
    }
    SDL_Window* win = SDL_CreateWindow(title.c_str(), r.w, r.h, exists ? SDL_WINDOW_BORDERLESS : SDL_WindowFlags{0});
    if (!win) return nullptr;
    SDL_SetWindowPosition(win, r.x, r.y);
    if (exists && fullScreen && !options_.sizeGiven) SDL_SetWindowFullscreen(win, true);
    auto created = gfx::SdlRenderer::create(win);
    if (!created) {
        SDL_DestroyWindow(win);
        return nullptr;
    }
    ScreenWindow sw;
    sw.window = win;
    sw.renderer = std::move(*created);
    screenWindows_.push_back(std::move(sw));
    return screenWindows_.back().renderer.get();
#else
    (void)display; (void)w; (void)h; (void)title; (void)fullScreen;
    return nullptr;
#endif
}

void App::closeScreenWindows() {
    ++screenEpoch_;          // le poste sait que ses fenetres ne sont plus
#if XPG_HAVE_SDL3
    for (auto& sw : screenWindows_) {
        sw.renderer.reset();
        if (sw.window) SDL_DestroyWindow(sw.window);
    }
#endif
    screenWindows_.clear();
}

void App::frame(double dt) {
    // Fixed order, every frame. See docs/ARCHITECTURE.md ? Frame pipeline.
    bus_.drain();            // 1. deliver deferred notifications
#if XPG_HAVE_SDL3
    // 1a. Lot macros 1 : les reponses de l'explorateur de fichiers, rendues ici
    //     sur le fil de l'interface (SDL peut les donner sur un autre).
    filepick::deliver();
#endif
    // 1a bis. Lot 7 : les fichiers deposes (dropFile), ensemble a l'ecran
    //     d'analyse - avant applyPending : l'ecran du dessus est encore celui
    //     qui les a recus.
    deliverDroppedFiles();
    refreshWindowTitle();    // 1b. lot 19 : le nom du projet, et " *" s'il est modifie
    settleReopened();        // 1c. lot API 6 : tout annule - l'etat d'avant la 1re modification
    refreshWindowIcon();     // 1d. lot API 6 : l'icone du projet sur la fenetre
    menus_->applyPending();  // 2. apply queued navigation
    noveltyCenter().tick();  // 2. bis 1.10 (chantier P) : la fenetre des nouveautes, Me montrer, les reglages
    // 2b. The PLC does not stop because you looked away from it. The scan is
    //     driven here, not by whichever screen happens to be on top.
    // Lot API 7 : la revision du programme (chaque commande, chaque projet
    // republie) - une simulation preparee avant un changement se refait au
    // prochain Simuler au lieu de rejouer l'ancien code.
    simulation_.setProgramRevision(commands_.revision() * 1000003ULL + programChanges_);
    simulation_.tick(dt);
    // 2c. Lot 14 : la liaison Modbus TCP suit la configuration de l'IHM ; le
    //     serveur de demonstration recoit les valeurs du simulateur.
    //     Le serveur expose le simulateur : il le prepare (sans le lancer), une
    //     fois par projet.
    // 1.12.0 : XPGAnalyser IHM n'a pas d'automate - ni son simulateur, ni la liaison vers
    // « l'automate du projet », ni le serveur de demonstration ; ses equipements, oui.
    const bool plc = core::hasApi();
    if (plc && hmi_ && hmi_->project.comm.demoServer && project_ && !simulation_.attached() && demoPrepared_ != project_.get()) {
        demoPrepared_ = project_.get();
        (void)simulation_.attach(project_);
    }
    if (plc) comm_.tick(hmi_ ? &hmi_->project : nullptr, project_, simulationRuntime(), dt);
    // 2c bis. Lot 15 : les equipements du reseau suivent le projet (liaisons,
    //     equipements simules, pings).
    //     Lot 17 : sur le poste, les vrais appareils ; dans l'application, les
    //     jumeaux (au choix de chaque equipement) ; "suit l'automate" lit le simulateur.
    equip_.setStation(stationActive_);
    equip_.setPlcReader([this, plc](const std::string& name) -> std::optional<double> {
        auto* rt = plc ? simulationRuntime() : nullptr;      // 1.12.0 : « suit l'automate » : pas d'automate
        if (!rt) return std::nullopt;
        sim::Value v;
        if (!rt->read(name, v)) return std::nullopt;
        return v.asReal();
    });
    equip_.tick(hmi_ ? &hmi_->project : nullptr, dt);
    // 1.9 (chantier C) : une bascule (le vrai se tait : l'IHM lit son esclave
    // simule ; il revient) - un avis dans la cloche, un par equipement (le dernier
    // remplace l'ancien). Une seule application par processus : le numero de la
    // derniere bascule dite se garde ici.
    {
        static std::uint64_t switchSeen = 0;
        for (const auto& s : equip_.switchesAfter(switchSeen)) {
            switchSeen = s.seq;
            std::tm tm{};
            const std::time_t t = static_cast<std::time_t>(s.at);
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char at[16];
            std::snprintf(at, sizeof at, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
            bgtasks::post({"esclave-simule:" + s.equipment, "\xC3\x89quipements", std::string(at) + " \xC2\xB7 " + s.equipment, s.text, {}, {},
                           s.toSlave ? "warning" : "ok"});
        }
    }
    // 2c ter. Lot 15 : la reprise - le battement ; en conception, les
    //     modifications non enregistrees gardees toutes les 2 minutes (a part).
    recovery_.tick(dt);
    if (!stationActive_ && project_ && !projectFolder_.empty() && recovery_.autosaveDue(commands_.isModified())) {
        std::string why;
        (void)recovery_.autosave([this](const std::string& folder, std::string* reason) {
            if (auto ok = project::ProjectStore::save(*project_, manifest_, folder); !ok) {
                if (reason) *reason = ok.error().message();
                return false;
            }
            if (hmi_) {
                if (auto st = hmi::save(hmi_->project, folder); !st) {
                    if (reason) *reason = st.error().message();
                    return false;
                }
                (void)hmi::saveHistory(hmi_->history, folder);
            }
            return true;
        }, &why);
    }
    // 2d. Lot 14 : les notifications suivent la configuration (la boite d'essai,
    //     les delais ecoules).
    notify_.tick(hmi_ ? &hmi_->project : nullptr);
    // 2e. Lot 14 : l'acces web suit la configuration ; les clics des navigateurs
    //     (commande permise) sont des clics sur la vue en marche.
    web_.tick(hmi_ ? &hmi_->project : nullptr);
    for (const auto& k : web_.takeClicks()) {
        if (!liveHmiPane()) continue;
        // ---- Lot API 8 : les exports qui demandent ou ----
        // Un clic venu d'un navigateur : personne devant l'ecran pour choisir ou
        // enregistrer - un export qu'il lance va dans exports/, sans question.
        const ExportQuestionMute noQuestion;
        // ---- fin Lot API 8 ----
        menus_->HandleEvent(ui::MouseMove{k.at, {}, {}});
        menus_->HandleEvent(ui::MouseDown{k.at, ui::MouseButton::Left, 1, {}});
        menus_->HandleEvent(ui::MouseUp{k.at, ui::MouseButton::Left, {}});
    }
    menu::FrameContext fc{dt, time_, &theme_,
                          renderer_ ? renderer_->surfaceSize() : gfx::Size{},
                          renderer_ ? renderer_->dpiScale() : 1.f};
    // 1.11 (T1) : le tutoriel en cours - la scene joue sa file, l'horloge avance.
    tutorials::frame(dt * 1000.0);
    menus_->Update(fc);      // 3. tick logic and animations
#if XPG_HAVE_SDL3
    // 1.10 (chantier Q) : la pipette d'une palette de couleurs montre une croix.
    {
        static SDL_Cursor* cross = nullptr;
        static bool        crossShown = false;
        const bool         want = ui::ColorPalette::pickingCount() > 0;
        if (want != crossShown) {
            if (want && !cross) cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
            (void)SDL_SetCursor(want && cross ? cross : SDL_GetDefaultCursor());
            crossShown = want;
        }
    }
#endif
    if (renderer_) {
        renderer_->beginFrame(theme_.color.windowBg);
        menus_->Render(*renderer_, fc);   // 4. paint back-to-front
        tutorials::paint(*renderer_, theme_, fc.surface, time_);   // 1.11 (T1) : le calque du tutoriel
        noveltyCenter().paint(*renderer_, theme_, fc.surface);   // 4. bis 1.10 : les reperes orange, la bulle
        // ---- Lot API 8 : la question attend dans une fenetre detachee : le
        //      voile de la principale, et ou aller (avant la capture).
        detached_.paintOverMain(*renderer_, fc.surface);
        // 4b. Ce qui vient d'etre dessine, avant de le presenter : la capture
        //     demandee (F12) et la session rejouee (--script).
        if (!captureRequest_.empty()) {
            const auto path = std::move(captureRequest_);
            captureRequest_.clear();
            if (auto r = captureToPng(*renderer_, path); r) std::fprintf(stderr, "capture : %s\n", path.c_str());
            else std::fprintf(stderr, "capture impossible : %s\n", r.error().message().c_str());
        }
        // 4c. Lot 14 : l'image de la vue en marche, pour les navigateurs qui la
        //     regardent (au rythme de Configuration > Acces web).
        if (web_.due(time_)) {
            auto* pane = liveHmiPane();
            const bool live = pane && pane->runtime().running();
            std::string view;
            if (live && hmi_)
                if (const auto* v = hmi_->project.view(pane->currentView())) view = v->name;
            web_.publish(*renderer_, live ? pane->canvas().screenRect() : gfx::Rect{}, view, live, time_);
        }
        if (script_ && !script_->afterRender(*renderer_)) quit_ = true;
        renderer_->endFrame();            // 5. present
    }
    // 6. Lot API 7 : les onglets detaches, chacun dans sa fenetre avec son
    //    renderer, a la meme horloge (celle, fixe, d'une session rejouee comprise).
    detached_.frame(time_);
    // 7. Lot API 8 : les filtres retenus, sur le disque une seconde apres le dernier changement.
    flushFilterMemory();
}


// =============================================================================
//  LOT 19 : ANNULER, RETABLIR, L'HISTORIQUE
// -----------------------------------------------------------------------------
//  UNE SEULE PILE, ET CHAQUE MOUVEMENT RAFRAICHIT CE QU'IL FAUT. Les commandes
//  de l'IHM se redessinent d'elles-memes (Document::touched) ; le texte d'une
//  section de l'automate est relu par ses onglets (SectionTextsRestored) ; une
//  creation, une suppression, un deplacement dans l'automate change la
//  structure et rebinde l'ecran, comme App::apply le fait deja.
//
//  Avant ce lot, edit.undo appelait la pile et c'est tout : l'IHM suivait,
//  l'automate non (une section annulee restait affichee), et rien ne disait
//  ce qui venait d'etre defait.
// =============================================================================
bool App::stepHistory(bool undo, HistoryEffect& fx, std::string& error) {
    const auto& list = undo ? commands_.done() : commands_.undone();
    if (list.empty()) return false;
    const auto classify = [&fx](const core::ICommand* c) {
        if (dynamic_cast<const project::SetSectionBodyCommand*>(c) != nullptr) fx.text = true;
        // 1.10 (R2, decision 11 bis) : les commandes du grafcet ne changent que le
        // texte de sections ; les defaire relit les onglets ST et les grafcets ouverts
        // (SectionTextsRestored) au lieu de tout reconstruire (l'onglet du grafcet reste).
        else if (dynamic_cast<const project::SetTransitionConditionCommand*>(c) != nullptr
                 || dynamic_cast<const project::SetActionBodyCommand*>(c) != nullptr
                 || dynamic_cast<const project::InsertStepCommand*>(c) != nullptr
                 || dynamic_cast<const project::RemoveStepCommand*>(c) != nullptr
                 || dynamic_cast<const project::InsertTransitionCommand*>(c) != nullptr
                 || dynamic_cast<const project::InsertActionCommand*>(c) != nullptr
                 || dynamic_cast<const project::SetTransitionEndpointsCommand*>(c) != nullptr
                 || dynamic_cast<const project::RenameStepCommand*>(c) != nullptr
                 || dynamic_cast<const project::RemoveTransitionCommand*>(c) != nullptr
                 || dynamic_cast<const project::RemoveActionCommand*>(c) != nullptr
                 || dynamic_cast<const project::ModifyActionCommand*>(c) != nullptr) fx.text = true;
        else if (dynamic_cast<const hmi::ViewCommand*>(c) == nullptr
                 && dynamic_cast<const hmi::ProjectCommand*>(c) == nullptr) fx.api = true;
    };
    // Lot 20 : un groupe rafraichit ce que ses parties touchent.
    if (const auto* g = dynamic_cast<const core::GroupCommand*>(list.back().command.get())) {
        for (const auto& part : g->parts()) classify(part.get());
    } else {
        classify(list.back().command.get());
    }
    auto r = undo ? commands_.undo() : commands_.redo();
    if (!r && error.empty()) error = r.error().message();
    return true;
}

void App::finishHistory(const HistoryEffect& fx, const core::CommandInfo& last, bool undo,
                        std::size_t count, const std::string& error) {
    if (count > 0) {
        if (fx.api) bus_.publish(ProjectOpened{project_, report_});
        else if (fx.text) bus_.publish(SectionTextsRestored{});
    }
    HistoryMoved moved;
    moved.label = last.label;
    moved.place = last.place;
    moved.placeKey = last.placeKey;
    moved.undo = undo;
    moved.count = count;
    moved.error = error;
    bus_.publish(moved);
}

namespace {
bool lockedProject(const project::Manifest& m, const std::string& folder) {
    return m.state == project::State::Lock && !folder.empty();
}
}

void App::undo() {
    if (lockedProject(manifest_, projectFolder_) || !commands_.canUndo()) {
        finishHistory({}, {}, true, 0, lockedProject(manifest_, projectFolder_) ? "projet verrouill\xC3\xA9 (LOCK)" : "");
        return;
    }
    HistoryEffect fx;
    std::string error;
    const auto info = commands_.done().back().info;
    XPG_TRACE(Document, "annuler : %s", info.label.c_str());   // 1.10.2 (CR)
    (void)stepHistory(true, fx, error);
    finishHistory(fx, info, true, 1, error);
}

void App::redo() {
    if (lockedProject(manifest_, projectFolder_) || !commands_.canRedo()) {
        finishHistory({}, {}, false, 0, lockedProject(manifest_, projectFolder_) ? "projet verrouill\xC3\xA9 (LOCK)" : "");
        return;
    }
    HistoryEffect fx;
    std::string error;
    const auto info = commands_.undone().back().info;
    XPG_TRACE(Document, "r\xC3\xA9tablir : %s", info.label.c_str());   // 1.10.2 (CR)
    (void)stepHistory(false, fx, error);
    finishHistory(fx, info, false, 1, error);
}

void App::goToHistory(std::uint64_t serial) {
    if (lockedProject(manifest_, projectFolder_)) {
        finishHistory({}, {}, true, 0, "projet verrouill\xC3\xA9 (LOCK)");
        return;
    }
    // Ou est-elle ? Dans les faites (ou l'ouverture) : on annule jusqu'a elle.
    // Dans les annulees : on retablit jusqu'a elle, elle comprise.
    bool inDone = serial == 0;
    for (const auto& e : commands_.done()) if (e.info.serial == serial) inDone = true;
    HistoryEffect fx;
    std::string error;
    core::CommandInfo last;
    std::size_t count = 0;
    if (inDone) {
        while (commands_.canUndo() && (serial == 0 || commands_.done().back().info.serial != serial)) {
            last = commands_.done().back().info;
            if (!stepHistory(true, fx, error)) break;
            ++count;
        }
        // L'etat d'arrivee : celui juste apres l'action choisie (son endroit est
        // ce qu'il faut montrer) ; ou l'ouverture.
        core::CommandInfo target = last;
        if (serial != 0 && commands_.canUndo() && commands_.done().back().info.serial == serial)
            target = commands_.done().back().info;
        if (count > 0) {
            if (fx.api) bus_.publish(ProjectOpened{project_, report_});
            else if (fx.text) bus_.publish(SectionTextsRestored{});
        }
        HistoryMoved moved;
        moved.label = target.label;
        moved.place = target.place;
        moved.placeKey = target.placeKey;
        moved.undo = true;
        moved.count = count;
        moved.error = error;
        moved.toState = true;
        moved.toOpening = serial == 0;
        bus_.publish(moved);
        return;
    }
    bool found = false;
    for (const auto& e : commands_.undone()) if (e.info.serial == serial) found = true;
    if (!found) return;
    while (commands_.canRedo()) {
        last = commands_.undone().back().info;
        if (!stepHistory(false, fx, error)) break;
        ++count;
        if (last.serial == serial) break;
    }
    finishHistory(fx, last, false, count, error);
}

std::size_t App::pendingChanges() const noexcept {
    const auto done = static_cast<std::ptrdiff_t>(commands_.done().size());
    const auto saved = commands_.savedDepth();
    if (saved < 0) return static_cast<std::size_t>(std::max<std::ptrdiff_t>(done, 1));
    return static_cast<std::size_t>(done > saved ? done - saved : saved - done);
}

void App::saveFromKeyboard() {
    XPG_PORTEE("App::saveFromKeyboard (Ctrl+S)");   // 1.10.2 (CR)
    if (!project_ || projectFolder_.empty()) {
        bus_.publish(StatusNotice{"Ctrl+S : ce projet n'a pas encore de dossier - Enregistrer sous\xE2\x80\xA6 d'abord", 8.0});
        return;
    }
    auto r = saveProject();
    if (!r) {
        menus_->ShowDialog(std::make_unique<MessageDialog>("Enregistrement impossible", r.error().message(),
                                                           MessageDialog::Icon::Error),
                           [](const menu::DialogResult&) {});
        return;
    }
    bus_.publish(StatusNotice{"Enregistr\xC3\xA9 \xC3\xA0 " + historyClock(savedAtMs_) + " \xE2\x80\x94 " + projectFolder_, 6.0});
}

void App::requestClose() {
    if (closeAsked_) return;
    closeAsked_ = true;
    // Le nom du dossier : c'est lui qu'on voit dans l'explorateur (le manifeste
    // dit souvent "Projet").
    const std::string folderName = std::filesystem::path(projectFolder_).filename().string();
    const std::string name = !folderName.empty() ? folderName : manifest_.name;
    const auto n = pendingChanges();
    HmiAskDialog::Spec spec;
    spec.id = "dialog.quitter";
    spec.title = "Quitter";
    spec.text = n == 0 ? "Le projet " + name + " a des modifications non enregistr\xC3\xA9" "es."
              : "Le projet " + name + " a " + std::to_string(n) + (n > 1 ? " modifications non enregistr\xC3\xA9" "es." : " modification non enregistr\xC3\xA9" "e.");
    if (!commands_.done().empty()) {
        const auto& last = commands_.done().back().info;
        spec.text += "\nLa derni\xC3\xA8re : " + last.label + " (" + historyClock(last.lastMs)
                   + (last.place.empty() ? std::string() : ", " + last.place) + ").";
    }
    spec.confirm = "Enregistrer et quitter";
    spec.cancel = "Annuler";
    spec.stopLabel = "Quitter sans enregistrer";
    spec.onStop = [this] { closeAsked_ = false; quit_ = true; };
    spec.width = 620.f;
    menus_->ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this](const menu::DialogResult& r) {
        closeAsked_ = false;
        if (!r.accepted()) return;
        auto ok = saveProject();
        if (!ok) {
            menus_->ShowDialog(std::make_unique<MessageDialog>("Enregistrement impossible",
                                                               ok.error().message() + "\n\nLa fen\xC3\xAAtre reste ouverte.",
                                                               MessageDialog::Icon::Error),
                               [](const menu::DialogResult&) {});
            return;
        }
        quit_ = true;
    });
}

void App::refreshWindowTitle() {
#if XPG_HAVE_SDL3
    if (!window_) return;
    std::string title = options_.title;
    if (project_ && !stationActive_) {
        std::string name = projectFolder_.empty() ? std::string() : std::filesystem::path(projectFolder_).filename().string();
        if (name.empty()) name = manifest_.name;
        if (name.empty()) name = project_->header.projectName;
        if (!name.empty()) title = name + (commands_.isModified() ? " *" : "") + " \xE2\x80\x94 " + options_.title;
    }
    if (title == titleShown_) return;
    titleShown_ = title;
    SDL_SetWindowTitle(window_, title.c_str());
#endif
}

// =============================================================================
//  ---- Lot API 8 : les filtres retenus d'une seance a l'autre ----
// -----------------------------------------------------------------------------
//  OU ILS SONT RANGES : dans les reglages de l'application (settings.txt), pas
//  dans le dossier du projet. Des filtres sont une facon de regarder, a soi, sur
//  ce poste : dans config/, ils partiraient avec le projet (ses versions, leur
//  comparaison, l'archive, le collegue qui l'ouvre) et un projet LOCK, ou sur un
//  partage en lecture seule, ne pourrait pas les garder ; un reglage s'ecrit
//  tout de suite, sans "Enregistrer". Mais ils n'ont de sens que pour UN
//  projet : la cle porte son empreinte, comme la progression des didacticiels
//  (didacticiel.<parcours>.<empreinte>) - son dossier, sinon le fichier
//  importe, sinon son nom :
//
//    filtres.<empreinte>.<id du tableau> = c1;Type,1,egal,BOOL,,
//    filtres.<empreinte>.<id de la barre> = s1;TON_D;Situees
//
//  Rien n'est ecrit sans projet. Un projet ouvert, ferme : chaque widget relit
//  ce que CE projet retient (FilterMemory::contextChanged).
// =============================================================================
namespace {

std::string filterProjectTag(std::string where) {
    // Les separateurs unifies, sans la casse : C:\Projets\A et c:/projets/a/ sont le meme.
    for (auto& c : where) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (where.size() > 1 && where.back() == '/') where.pop_back();
    std::uint32_t h = 2166136261u;          // FNV-1a
    for (const char c : where) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    char b[12];
    std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h));
    return b;
}

// Une cle de reglage se lit "cle = valeur" : ni blanc, ni '=', ni '#'.
std::string filterKeyPart(const std::string& id) {
    std::string out = id;
    for (auto& c : out)
        if (c == '=' || c == '#' || std::isspace(static_cast<unsigned char>(c))) c = '_';
    return out;
}

} // namespace

std::string App::filterMemoryPrefix() const {
    std::string where = projectFolder_;
    if (where.empty() && project_) where = project_->header.sourceFile;
    if (where.empty() && project_) where = project_->header.projectName;
    return where.empty() ? std::string{} : "filtres." + filterProjectTag(where) + ".";
}

void App::installFilterMemory() {
    filterAlive_ = std::make_shared<int>(0);
    const std::weak_ptr<int> alive = filterAlive_;
    ui::FilterMemory::Store store;
    store.read = [this, alive](const std::string& key) -> std::string {
        if (alive.expired()) return {};
        const auto prefix = filterMemoryPrefix();
        return prefix.empty() ? std::string{} : settings_.getString(prefix + filterKeyPart(key));
    };
    store.write = [this, alive](const std::string& key, const std::string& value) {
        if (alive.expired()) return;
        const auto prefix = filterMemoryPrefix();
        if (prefix.empty()) return;
        const auto k = prefix + filterKeyPart(key);
        if (settings_.getString(k) == value) return;       // rien ne change (absente : "")
        if (value.empty()) settings_.remove(k);             // "Tout effacer" : oubliee
        else settings_.set(k, value);
        filterDirty_ = true;
        filterDirtyAt_ = time_;
    };
    store.forget = [this, alive](bool allProjects) {
        if (alive.expired()) return;
        const auto prefix = allProjects ? std::string("filtres.") : filterMemoryPrefix();
        if (prefix.empty() || settings_.removePrefix(prefix) == 0) return;
        filterDirty_ = true;
        filterDirtyAt_ = time_;
    };
    // Une session rejouee (--script) ne lit ni n'ecrit ce que retiennent les
    // reglages de l'utilisateur : ses filtres vivent le temps de la session
    // (un onglet rouvert, un projet rouvert les retrouvent), et une session
    // rejouee deux fois montre deux fois la meme chose.
    if (!options_.script.empty()) {
        auto kept = std::make_shared<std::map<std::string, std::string>>();
        store.read = [this, alive, kept](const std::string& key) -> std::string {
            if (alive.expired()) return {};
            const auto prefix = filterMemoryPrefix();
            const auto it = prefix.empty() ? kept->end() : kept->find(prefix + filterKeyPart(key));
            return it == kept->end() ? std::string{} : it->second;
        };
        store.write = [this, alive, kept](const std::string& key, const std::string& value) {
            if (alive.expired()) return;
            const auto prefix = filterMemoryPrefix();
            if (prefix.empty()) return;
            if (value.empty()) kept->erase(prefix + filterKeyPart(key));
            else (*kept)[prefix + filterKeyPart(key)] = value;
        };
        store.forget = [this, alive, kept](bool allProjects) {
            if (alive.expired()) return;
            const auto prefix = allProjects ? std::string("filtres.") : filterMemoryPrefix();
            if (prefix.empty()) return;
            for (auto it = kept->lower_bound(prefix); it != kept->end() && it->first.compare(0, prefix.size(), prefix) == 0;) it = kept->erase(it);
        };
    }
    ui::FilterMemory::install(std::move(store));
    subscriptions_ += bus_.subscribe<ProjectOpened>([this](const ProjectOpened&) { filterMemoryContext(); });
    subscriptions_ += bus_.subscribe<ProjectClosed>([this](const ProjectClosed&) { filterMemoryContext(); });
}

void App::filterMemoryContext() {
    // ProjectOpened part aussi a chaque modification (le meme projet republie) :
    // seul un changement d'empreinte compte.
    const auto tag = filterMemoryPrefix();
    if (tag == filterTag_) return;
    // Le meme projet enregistre ailleurs (Enregistrer sous) : ce qui est montre
    // le suit, sous la nouvelle cle. Un autre projet : chacun relit le sien.
    const bool sameProject = document_ && document_.get() == filterDoc_;
    filterTag_ = tag;
    filterDoc_ = document_.get();
    ui::FilterMemory::contextChanged(!sameProject);
}

void App::flushFilterMemory() {
    if (!filterDirty_ || time_ - filterDirtyAt_ < 1.0) return;
    filterDirty_ = false;
    (void)settings_.save();
}
// ---- fin Lot API 8 ----

} // namespace app
