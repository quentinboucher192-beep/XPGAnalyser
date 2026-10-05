// =============================================================================
//  app/App.hpp - composition root
// -----------------------------------------------------------------------------
//  This is the only place in the program where concrete types are wired to
//  interfaces. Everything below receives what it needs through its constructor,
//  which is what makes the layers below independently testable (see tests/).
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/EventBus.hpp"
#include "../core/Result.hpp"
#include "../core/ServiceRegistry.hpp"
#include "../import/ProjectImporter.hpp"
#include "../project/EditCommands.hpp"
#include "../sim/Runtime.hpp"
#include "../project/ProjectStore.hpp"
#include "SimulationHost.hpp"
#include "SimJournal.hpp"              // Lot API 8 : le journal de la simulation
#include "hmi/HmiCommHost.hpp"
#include "hmi/HmiEquipmentHost.hpp"
#include "Recovery.hpp"
#include "hmi/HmiNotifyHost.hpp"
#include "hmi/HmiWebHost.hpp"
#include "Settings.hpp"
#include "DetachedWindows.hpp"
#include "../menu/MenuManager.hpp"
#include "../platform/Renderer.hpp"
#include "../ui/Theme.hpp"
#include "../hmi/HmiCommands.hpp"

#include <functional>
#include <optional>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

struct SDL_Window;

namespace app {

class ScriptRunner;

struct AppOptions {
    std::string title{"PLC Project Analyzer"};
    int         width{1536};
    int         height{1024};
    // Lot 8 : --size impose la taille (les sessions rejouees). Sinon la fenetre
    // s'ouvre AGRANDIE, et l'appli se souvient si on l'a reduite.
    bool        sizeGiven{false};
    bool        vsync{true};
    // LE CLAIR PAR DEFAUT. Sur un poste de bureau - et c'est la que ce
    // programme s'utilise, pas devant une armoire - il se lit mieux et
    // s'imprime tel quel. Le sombre reste a un clic, et le reglage est retenu
    // dans les preferences, donc ce choix ne s'impose qu'une fois.
    std::string themeName{"Light"};
    std::string openOnStart;     // path passed on the command line
    // Une session a rejouer (voir ScriptRunner.hpp), et ou ranger ses captures.
    std::string script;
    std::string capturesDir;
    // Lot 14 : --ihm <dossier du projet> - le poste d'exploitation (l'IHM seule,
    // en plein ecran, sans l'editeur).
    std::string station;
};

// Published when a project becomes current; every view repopulates from this.
struct ProjectOpened {
    std::shared_ptr<const domain::Project> project;
    std::shared_ptr<importer::AnalysisReport> report;
};
struct ProjectClosed {};
struct ThemeChanged { std::string name; };

// Lot 19 : annuler, retablir, revenir a un etat de l'historique. Publie apres
// le mouvement : la barre d'etat le dit, et l'ecran montre l'endroit.
struct HistoryMoved {
    std::string label;          // la derniere action defaite ou refaite
    std::string place, placeKey;
    bool        undo{true};
    std::size_t count{0};       // 0 : rien n'a bouge (rien a annuler...)
    std::string error;          // une commande a refuse de se defaire
    // Revenir a un etat (double clic dans l'historique) : l'action d'arrivee
    // (label/place en sont alors les siens) ; toOpening : l'etat de l'ouverture.
    bool        toState{false};
    bool        toOpening{false};
};
// Une annulation a change le texte d'une section de l'automate : les onglets
// ouverts se relisent (sans rien empiler).
struct SectionTextsRestored {};
// Lot 19 : ouvrir / fermer l'historique (action edit.history, Ctrl+H).
struct HistoryToggle {};
// Lot API 7 : un onglet de l'API a ouvrir, par sa cle ("simulation",
// "statistiques", "variables") - les actions sim.open (F9), view.statistics
// (Ctrl+5) et view.variables (Ctrl+1), qui ouvraient des ecrans pleins. L'ecran
// d'analyse l'ouvre ; App ne connait pas ses onglets.
struct OpenApiTab { std::string key; };
// Lot 19 : un message pour la barre d'etat (Ctrl+S : "Enregistre", sans dialogue).
struct StatusNotice { std::string text; double seconds{6.0}; };
// Lot 21 : le projet vient d'etre enregistre ; `autoVersion` : la version
// automatique creee dans la foulee ("V7"), vide sinon.
struct ProjectSaved { std::string autoVersion; };

class App {
public:
    static core::Result<std::unique_ptr<App>> create(AppOptions);
    ~App();

    int run();                       // owns the frame loop; returns the exit code

    // 1.11.1 (T1, R111-18) : la voie d'un evenement du client, une fois passes les
    // filtres de la fenetre (F11, F12, dialogues detaches, fichiers deposes) : la
    // bulle des nouveautes, puis le calque du tutoriel (sa barre), puis les ecrans.
    // App::pumpEvents y passe ; la commande de session clic-souris aussi, pour
    // cliquer comme une souris (UiDriver::send, lui, va droit aux ecrans).
    void deliverClientEvent(const ui::InputEvent& e);

    // Services handed to screens.
    [[nodiscard]] core::EventBus&        events()   noexcept { return bus_; }
    [[nodiscard]] menu::MenuManager&     menus()    noexcept { return *menus_; }
    [[nodiscard]] core::ActionRegistry&  actions()  noexcept { return actions_; }
    [[nodiscard]] core::CommandStack&    commands() noexcept { return commands_; }
    [[nodiscard]] importer::ProjectImporter& importer() noexcept { return *importer_; }
    [[nodiscard]] const ui::Theme&       theme() const noexcept { return theme_; }
    [[nodiscard]] Settings&              settings() noexcept { return settings_; }
    void setTheme(std::string_view name);
    // Lot API 6 : Affichage > Theme... - la galerie des neuf themes.
    void showThemeGallery();
    // Une session rejouee (--script) : ce qui s'ouvre de soi-meme pour un
    // humain (le didacticiel de l'IHM) ne doit pas couvrir l'ecran du script.
    [[nodiscard]] bool scripted() const noexcept { return script_ != nullptr; }

    // Retourne a l'ecran d'accueil, en demandant d'abord s'il reste des
    // modifications non enregistrees. Le bouton et le raccourci y passent tous
    // les deux : deux chemins finiraient par diverger sur la question.
    void goHome();

    // ./projets/ : le dossier ou les affaires sont creees par defaut, a cote du
    // programme et non melangees a lui.
    [[nodiscard]] static std::filesystem::path projectsRoot();

    [[nodiscard]] std::shared_ptr<const domain::Project> project() const noexcept { return project_; }
    [[nodiscard]] std::shared_ptr<const importer::AnalysisReport> report() const noexcept { return report_; }
    [[nodiscard]] const std::vector<std::string>& recentPaths() const noexcept { return recent_; }
    void forgetPath(const std::string& path);
    void rememberPath(std::string path);

    // Opening a hardware or dictionary export on its own would throw away the
    // program that is already loaded. openPath() merges instead: it re-imports
    // the current sources together with the new file, in one pass.
    void openPath(std::string path);
    [[nodiscard]] const std::vector<std::string>& sourcePaths() const noexcept { return sources_; }

    // ---- projects ---------------------------------------------------------
    [[nodiscard]] const std::string&        projectFolder() const noexcept { return projectFolder_; }
    [[nodiscard]] const project::Manifest&  manifest() const noexcept { return manifest_; }
    [[nodiscard]] project::MasterKey&       masterKey() noexcept { return masterKey_; }

    void adoptProject(std::shared_ptr<domain::Project>, project::Manifest, std::string folder);
    // The mutable model the edit commands act on. project() is the same object
    // seen as const, so views cannot modify what they display.
    [[nodiscard]] std::shared_ptr<domain::Project> document() const noexcept { return document_; }
    // Runs a command through the undo stack and refreshes every view.
    // refreshViews rebinds every model, which is right after a structural change
    // and wrong after a keystroke: it would rebuild the tree and the tables sixty
    // times a second while someone types.
    void apply(core::CommandPtr command, bool refreshViews = true);
    [[nodiscard]] core::Status saveProject();

    // ---- lot API 6 : l'etat du projet suit les versions ----------------------
    //  La premiere modification d'un projet NEW ou FINISH le passe en DEV : la
    //  version suivante commence, et le message le dit. Annuler jusqu'a l'etat
    //  enregistre le rend a son etat d'avant ; enregistrer fixe le DEV.
    [[nodiscard]] std::optional<project::State> reopenedFrom() const noexcept { return reopenedFrom_; }
    //  Pose l'etat et l'ecrit dans le manifeste (LOCK : avec le verrou du mot de
    //  passe) - le reste du projet n'est pas reecrit.
    core::Status setProjectState(project::State state, const std::string& password = {});

    // ---- lot 19 : annuler, retablir, l'historique --------------------------
    //  Ctrl+Z et Ctrl+Y partout (l'ecran les prend avant les widgets qui ne
    //  s'en servent pas) ; une seule pile pour le programme et l'IHM. Chaque
    //  mouvement dit ce qu'il a fait (HistoryMoved) et rafraichit ce qui doit
    //  l'etre : l'IHM se redessine d'elle-meme, une section de l'automate se
    //  relit, une creation ou une suppression dans l'automate rebinde tout.
    void undo();
    void redo();
    // Revenir a l'etat juste apres l'entree `serial` de l'historique (dans les
    // faites : on annule jusqu'a elle ; dans les annulees : on retablit jusqu'a
    // elle) ; 0 : l'etat a l'ouverture.
    void goToHistory(std::uint64_t serial);
    [[nodiscard]] std::int64_t openedAtMs() const noexcept { return openedAtMs_; }
    [[nodiscard]] std::int64_t savedAtMs() const noexcept { return savedAtMs_; }
    // L'endroit ou l'on est (l'onglet, son sous-onglet) : l'ecran le connait,
    // la pile le note avec chaque commande (CommandInfo::place, placeKey).
    void setPlaceProvider(std::function<void(core::CommandInfo&)> provider) { placeProvider_ = std::move(provider); }
    // Ctrl+S : enregistrer, et le dire dans la barre d'etat (le bouton garde
    // son dialogue ; une erreur en ouvre un).
    void saveFromKeyboard();
    // Fermer la fenetre : s'il reste des modifications, la question d'abord
    // (Enregistrer et quitter / Quitter sans enregistrer / Annuler).
    void requestClose();
    // Le nombre de pas entre l'etat enregistre et l'etat courant.
    [[nodiscard]] std::size_t pendingChanges() const noexcept;
    void openProjectFolder(std::string folder);      // prompts for a password if locked
    // 1.11.2 (UNI, decision 204) : OUVRIR UN AUTRE PROJET REMPLACE CELUI QUI EST EN MEMOIRE.
    // S'il a des modifications non enregistrees : la question de l'accueil d'abord (la meme
    // que StartupScreen::confirmReplace : "Les enregistrer d'abord" / "Les abandonner" ;
    // Annuler : rien ne s'ouvre), puis then(). Sans modification : then() tout de suite.
    // Le menu de la puce projet (les projets recents) passe par elle.
    // 1.11.2 (UNI, decision 218) : aussi "Ouvrir..." (l'action file.open : le menu de la
    // puce, Ctrl+O, la palette) et "Fermer le projet" de la puce (closing : la meme
    // question, sa derniere phrase dit que le projet se ferme).
    void confirmReplace(std::function<void()> then, bool closing = false);
    // 1.11.2 (UNI, decision 195) : UNE SEULE INSTANCE. Les demandes d'une autre instance
    // lancee (core/SingleInstance.hpp), prises a chaque image : la fenetre revient au
    // premier plan ; un projet (ou un poste, --ihm) demande s'ouvre - apres la question
    // d'enregistrer celui qui est modifie ; plus tard si une question attend deja, si un
    // tutoriel tourne ; jamais par-dessus le poste d'exploitation en marche.
    void takeInstanceRequests();
    void promptMasterKeyIfNeeded();                  // the first-run modal
    // 1.8.0 : l'epinglage a la barre des taches demande a l'installation (une fois par
    // compte) ; l'accueil l'appelle quand la fenetre est au premier plan, sans dialogue.
    [[nodiscard]] bool taskbarPinPending() const noexcept { return taskbarPinPending_; }
    void               proposeTaskbarPin();

    // What the simulator is doing, so the workspace can show it whichever tab is
    // in front (lot API 7 : API > Simulation is a tab now). Empty means "not
    // simulating".
    void setSimulationStatus(std::string text);
    // The simulation itself, which outlives every screen and every tab. See
    // SimulationHost.hpp for why it does not belong to a view.
    [[nodiscard]] SimulationHost&       simulation()       noexcept { return simulation_; }
    // Lot API 7 : le programme a change sans commande (une mise a jour de la
    // bibliotheque par une macro) : la simulation preparee avant se refera.
    void noteProgramChanged() noexcept { ++programChanges_; }
    [[nodiscard]] const SimulationHost& simulation() const noexcept { return simulation_; }
    [[nodiscard]] sim::Runtime* simulationRuntime() const noexcept {
        return const_cast<sim::Runtime*>(simulation_.runtime());
    }
    // ---- Lot API 8 : le journal de la simulation (SimJournal.hpp) ----
    //  Tout ce qui arrive pendant une simulation (l'automate, l'IHM, les
    //  equipements, le debogage), dit en francais, avec ou aller. Le Centre de
    //  simulation l'ecrit (SimulationWorkspace.cpp) et le montre (Simulation >
    //  Journal, la frise de la Vue d'ensemble). Y ecrire, de n'importe ou :
    //    simJournal().add(SimSource::Debogage, SimSeverity::Info, "texte", SimJournal::goLine("Section", 12));
    [[nodiscard]] SimJournal&       simJournal()       noexcept { return simJournal_; }
    [[nodiscard]] const SimJournal& simJournal() const noexcept { return simJournal_; }
    // ---- fin Lot API 8 ----
    // Lot 14 : la liaison avec l'automate reel (Modbus TCP) et le serveur de
    // demonstration - comme la simulation, ils ne dependent d'aucun ecran.
    [[nodiscard]] CommHost& comm() noexcept { return comm_; }
    // Lot 15 : les equipements du reseau (leurs liaisons, les simules, les
    // pings, les ports du PC).
    [[nodiscard]] EquipmentHost& equipments() noexcept { return equip_; }
    // Lot 15 : la reprise apres un arret brutal (le verrou de session, la
    // sauvegarde de reprise, l'etat du poste).
    [[nodiscard]] Recovery& recovery() noexcept { return recovery_; }
    // L'etat du poste a recharger au demarrage du poste (vide : aucun) ; `ask` :
    // le demander d'abord (30 s, puis d'office) - un demarrage sans personne.
    [[nodiscard]] std::optional<std::string> takeStationRestore(bool* ask = nullptr);
    // La question "Voulez-vous recharger les donnees precedentes ?".
    void offerRecovery();
    // Lot 15 : le sujet sur lequel s'ouvre l'aide de l'IHM en plein ecran (F1
    // du poste : le poste d'exploitation) ; pris une fois.
    void setHelpTopic(std::string topic) { helpTopic_ = std::move(topic); }
    [[nodiscard]] std::string takeHelpTopic(std::string fallback) {
        std::string t = helpTopic_.empty() ? std::move(fallback) : std::move(helpTopic_);
        helpTopic_.clear();
        return t;
    }
    // Lot 14 : les notifications des alarmes (courriel, SMS) et la boite d'essai.
    [[nodiscard]] NotifyHost& notify() noexcept { return notify_; }
    // Lot 14 : l'acces par navigateur (le serveur web integre).
    [[nodiscard]] WebHost& web() noexcept { return web_; }
    // La vue IHM en marche a l'ecran : le poste d'exploitation, ou la simulation
    // IHM si c'est l'onglet montre (nulle sinon) - l'image des navigateurs.
    [[nodiscard]] class HmiSimulationPane* liveHmiPane() const;

    // ---- lot 14 : le poste d'exploitation ------------------------------------
    //  L'IHM seule (screens/StationScreen) : y entrer depuis la conception
    //  (Essayer le poste), en sortir (Passer en conception). SDL reste ici : la
    //  fenetre en plein ecran, le curseur, les fenetres des ecrans secondaires.
    void enterStation();
    // Lot 15 : ouvrir un projet directement en poste d'exploitation (l'accueil,
    // --ihm, le demarrage avec le PC) - un projet FINISH seulement ; `force` :
    // la reprise apres un arret brutal (le poste tournait deja).
    void openStation(const std::string& folder, bool force = false);
    // Le projet ouvert est-il FINISH ? (sinon why : son etat).
    [[nodiscard]] bool projectFinished(std::string* why = nullptr) const;
    // Lot 15 : deverrouiller (LOCK -> DEV) - le mot de passe, sinon la cle du PC
    // maitre (l'adresse MAC du patron, parmi celles des cartes de ce PC).
    void askUnlock();
    [[nodiscard]] std::vector<std::string> masterCandidates() const;
    void leaveStation();
    [[nodiscard]] bool stationActive() const noexcept { return stationActive_; }
    void setStationWindow(bool fullScreen, bool hideCursor);
    // Les ecrans que voit le systeme ; une fenetre sur l'ecran `display` (1 : le
    // principal) - en plein ecran s'il existe, sinon une fenetre ordinaire de
    // w x h ; son rendu (nul : impossible). Fermees ensemble.
    [[nodiscard]] int displayCount() const;
    gfx::IRenderer* openScreenWindow(int display, int w, int h, const std::string& title, bool fullScreen);
    void closeScreenWindows();
    // Change a chaque fermeture des fenetres des ecrans : le poste qui en garde
    // les renderers sait qu'ils ne valent plus.
    [[nodiscard]] std::uint64_t screenEpoch() const noexcept { return screenEpoch_; }
    // Fermer la fenetre (Alt+F4) : un gardien peut le refuser (le kiosque) -
    // vrai : refuse, il s'en occupe. Et quitter pour de bon.
    void setQuitGuard(std::function<bool()> guard) { quitGuard_ = std::move(guard); }
    void requestQuit() noexcept { quit_ = true; }
    [[nodiscard]] const std::string& simulationStatus() const noexcept { return simStatus_; }
    const core::SignalPtr<const std::string&> simulationStatusChanged =
        core::Signal<const std::string&>::create();
    void offerProjectFolder(const std::string& importedFrom);

    // Une capture de la fenetre, prise apres le prochain dessin (F12 : dans
    // captures/, horodatee).
    void requestCapture(std::string path) { captureRequest_ = std::move(path); }
    // Lot API 7 : les onglets detaches dans des fenetres a eux (DetachedWindows.hpp).
    DetachedWindows& detachedWindows() noexcept { return detached_; }

    // Lot 7 : UN FICHIER GLISSE DANS LA FENETRE PRINCIPALE (SDL, ou `deposer`
    // d'une session rejouee). L'ecran du dessus le recoit d'abord : un volet qui
    // prend des fichiers (une image de l'IHM, le champ d'une macro) le garde.
    // Sinon, un projet ouvert, un .XPG ou un .XHW va a l'ecran d'analyse
    // (MainAnalysisScreen::filesDropped), qui demande quoi en faire ; un autre
    // ecran plein au-dessus de lui (l'aide) rend d'abord la main. L'accueil, un
    // dialogue, le poste d'exploitation : comme avant. Les fichiers d'un meme
    // depot arrivent un a un dans la meme image : ils sont donnes ensemble.
    void dropFile(const ui::FileDropped& drop);
    // ---- Lot API 8 : glisser n'importe quel fichier ----
    //  Un fichier lache sur une fenetre detachee que sa page n'a pas pris
    //  (DetachedWindows) : la question du depot se pose dans la principale,
    //  comme pour un .XPG. Faux : pas de projet ouvert (ou le poste).
    bool queueDroppedFile(const std::string& path);
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----

    // ---- l'IHM du projet ---------------------------------------------------
    //
    //  UN DOCUMENT A PART, RANGE AVEC LE PROJET. hmi::Document vit dans le
    //  dossier du projet (ihm/), se charge quand le projet s'ouvre, s'enregistre
    //  quand il s'enregistre, et ses commandes vont dans LA MEME PILE : Ctrl+Z
    //  reprend un deplacement d'objet comme une creation de section, dans
    //  l'ordre ou on les a faits.
    //
    //  Il appartient au dossier du projet, ou - tant qu'il n'y en a pas - au
    //  fichier importe. Un .XHW qui complete l'import en cours ne le remplace
    //  donc pas ; un autre .XPG, si.
    [[nodiscard]] std::shared_ptr<hmi::Document> hmi() const noexcept { return hmi_; }
    // Ce que le chargement a lu de travers (cles inconnues, references
    // cassees...). Vide quand tout s'est bien passe.
    [[nodiscard]] const std::vector<std::string>& hmiWarnings() const noexcept { return hmiWarnings_; }

private:
    App() = default;
    core::Status initPlatform(const AppOptions&);
    [[nodiscard]] menu::MenuFactory buildMenuFactory();
    void         registerActions();
    void         pumpEvents(bool& running);
    void         frame(double dt);

    AppOptions                                 options_;
    SDL_Window*                                window_{nullptr};   // owned; freed in ~App
    std::unique_ptr<gfx::IRenderer>            renderer_;
    ui::Theme                                  theme_{ui::Theme::light()};

    Settings                                   settings_;
    core::EventBus                             bus_;
    core::ServiceRegistry                      services_;
    core::ActionRegistry                       actions_;
    // Profonde : les commandes de l'IHM ne gardent que ce qu'elles changent
    // (voir hmi/HmiCommands.hpp), et 256 pas se consomment en une minute de
    // dessin. C'est la memoire qui borne, pas un compteur.
    core::CommandStack                         commands_{100000};

    std::unique_ptr<importer::ProjectImporter> importer_;
    std::unique_ptr<menu::MenuManager>         menus_;
    std::unique_ptr<menu::MenuManager::ScopedInstance> menuGlobal_;

    std::shared_ptr<domain::Project>           document_;
    std::shared_ptr<const domain::Project>     project_;
    std::shared_ptr<importer::AnalysisReport>  report_;
    std::vector<std::string>                   recent_;
    std::vector<std::string>                   sources_;   // files making up the project
    std::string                                projectFolder_;
    project::Manifest                          manifest_;
    project::MasterKey                         masterKey_;
    bool                                       taskbarPinPending_ = false;
    std::string                                simStatus_;
    SimulationHost                             simulation_;
    SimJournal                                 simJournal_;      // Lot API 8 : le journal de la simulation
    CommHost                                   comm_;
    EquipmentHost                              equip_;           // lot 15
    Recovery                                   recovery_;        // lot 15 : la reprise
    std::optional<std::string>                 stationRestore_;  // lot 15 : l'etat du poste a recharger
    bool                                       stationRestoreAsk_{false};
    std::string                                helpTopic_;          // lot 15
    void                                       openRecovered(const Recovery::Found& found);
    NotifyHost                                 notify_;          // lot 14
    WebHost                                    web_;             // lot 14
    const domain::Project*                     demoPrepared_{nullptr};   // lot 14 : le simulateur prepare pour le serveur
    // Lot 14 : le poste d'exploitation.
    bool                                       stationPending_{false};   // --ihm : y aller des que le projet est ouvert
    bool                                       stationForce_{false};     // lot 15 : la reprise - sans exiger FINISH
    bool                                       stationActive_{false};
    std::function<bool()>                      quitGuard_;
    // Lot 19 : l'historique et le titre de la fenetre.
    std::function<void(core::CommandInfo&)>    placeProvider_;
    std::int64_t                               openedAtMs_{0};
    std::int64_t                               savedAtMs_{0};
    std::string                                titleShown_;
    bool                                       closeAsked_{false};
    std::uint64_t                              lockSaidInGroup_{0};   // lot 20 : le refus LOCK, une fois par groupe (son numero)
    std::optional<project::State>              reopenedFrom_;         // lot API 6 : NEW / FINISH avant la 1re modification
    void                                       settleReopened();      // apres Ctrl+Z / Ctrl+Y : l'etat d'avant revient-il ?
    domain::ProjectIcon                        windowIcon_;           // lot API 6 : l'icone posee sur la fenetre
    void                                       refreshWindowIcon();
    struct HistoryEffect { bool api{false}; bool text{false}; };
    bool stepHistory(bool undo, HistoryEffect& fx, std::string& error);
    void finishHistory(const HistoryEffect& fx, const core::CommandInfo& last, bool undo,
                       std::size_t count, const std::string& error);
    void refreshWindowTitle();
    struct ScreenWindow {
        SDL_Window*                     window{nullptr};
        std::unique_ptr<gfx::IRenderer> renderer;
    };
    std::vector<ScreenWindow>                  screenWindows_;
    std::uint64_t                              programChanges_{0};   // lot API 7 : chaque ProjectOpened (la revision du programme)
    std::uint64_t                              screenEpoch_{0};
    // L'IHM, et a quoi elle appartient (dossier du projet, ou fichier importe).
    void                                       bindHmi();
    [[nodiscard]] core::Status                 saveHmi(const std::string& folder);
    std::shared_ptr<hmi::Document>             hmi_;
    std::string                                hmiKey_;
    std::vector<std::string>                   hmiWarnings_;
    // Le dossier ihm/ n'a pas pu etre relu : il n'est PAS reecrit par-dessus,
    // pour ne rien perdre. Enregistrer le dit au lieu de l'ecraser.
    std::string                                hmiUnreadable_;
    // Les captures et les sessions rejouees (--script).
    std::string                                captureRequest_;
    std::unique_ptr<ScriptRunner>              script_;
    // Lot 7 : les fichiers deposes de cette image (dropFile), donnes ensemble a
    // l'ecran d'analyse au debut de la suivante ; `dropWait_` : les images deja
    // attendues pendant que l'ecran du dessus rend la main.
    std::vector<std::string>                   droppedFiles_;
    int                                        dropWait_{0};
    void                                       deliverDroppedFiles();
    // 1.11.2 (UNI) : la demande d'une autre instance qui attend (la derniere l'emporte).
    std::vector<std::string>                   instancePaths_;
    bool                                       instanceStation_{false};
    bool                                       instanceAsked_{false};    // la question d'enregistrer est a l'ecran
    int                                        instanceAskIdle_{0};      // ...mais plus dans la pile depuis n tours
    bool                                       instanceWaitSaid_{false}; // "elle s'ouvrira apres..." deja dit
    void                                       bringToFront();
    void                                       openRequested(const std::vector<std::string>& paths, bool station);
    bool                                       quit_{false};
    core::ConnectionScope                      subscriptions_;
    // ---- Lot API 8 : les filtres retenus d'une seance a l'autre (ui::FilterMemory) ----
    //  Dans les reglages, sous "filtres.<empreinte du projet>.<id du widget>" : par
    //  projet et par tableau ; relus a l'ouverture de l'onglet, ecrits a chaque
    //  changement, le fichier enregistre une seconde apres le dernier (et a la sortie).
    void                                       installFilterMemory();
    void                                       filterMemoryContext();      // un projet ouvert, ferme : un autre ?
    void                                       flushFilterMemory();        // chaque image
    [[nodiscard]] std::string                  filterMemoryPrefix() const; // "filtres.<empreinte>." ; "" : pas de projet
    std::string                                filterTag_;
    const void*                                filterDoc_{nullptr};
    bool                                       filterDirty_{false};
    double                                     filterDirtyAt_{0.0};
    std::shared_ptr<int>                       filterAlive_;               // les crochets ne survivent pas a App
    // ---- fin Lot API 8 ----

    double                                     time_{0.0};
    // Lot API 7 : les onglets detaches. Construit apres bus_ (il s'y abonne) ;
    // detruit le premier - ~App lui a deja fait rendre ses pages, avant les
    // ecrans et avant SDL_Quit.
    DetachedWindows                            detached_{*this};
};

} // namespace app
