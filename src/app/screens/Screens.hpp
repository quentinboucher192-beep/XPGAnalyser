// =============================================================================
//  app/screens/Screens.hpp - the six screens from the brief
// -----------------------------------------------------------------------------
//  Each screen is a WidgetMenu: it owns a widget tree, subscribes to the bus in
//  OnEnter and drops those subscriptions in OnExit. A screen never keeps a
//  pointer to the Project; it keeps a shared_ptr, taken at the moment it enters.
// =============================================================================
#pragma once

#include "../SimulationHost.hpp"
#include "../../project/GrafcetLayoutFile.hpp"
#include "../../project/Macro.hpp"
#include "../../project/MacroMemory.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../domain/Reindex.hpp"   // domain::EntityKind, named in the
                                        // right-click handlers below
#include "../../help/HelpIndex.hpp"     // help::Target, la destination d'un F1
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/TabArea.hpp"   // lot 7 : le centre en plusieurs groupes
#include "../../ui/widgets/HelpView.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/PathBrowse.hpp"   // le bouton ... des champs de chemin
#include "../../project/ProjectStore.hpp"
#include "../../sim/Runtime.hpp"
#include "../GoToPanel.hpp"     // lot 20 : Aller a... (Ctrl+K)
#include "../../hmi/HmiVersions.hpp"   // lot 21 : les versions
#include "../hmi/HmiTrails.hpp"         // lot 21 : les parcours du didacticiel
#include "../hmi/HmiValueKind.hpp"      // 1.11.3 : la demande du carre de legende
#include "../hmi/HmiTypePicker.hpp"     // 1.11.19 (lot 6) : le selecteur de types
#include "../RackView.hpp"
#include "../ViewModels.hpp"
#include "../../core/CodeIcons.hpp"          // 1.8.0 : les icones au choix
#include "../../core/Command.hpp"            // 1.11.15 : applyFromSimulation
#include "../../project/MastImport.hpp"      // 1.8.0 : l'import suivi (le recapitulatif)

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hmi { struct Issue; enum class ScriptLang : std::uint8_t; struct RecipeRequest; struct ResourceRequest; struct UserRequest; struct ExportRequest; }
namespace hmi::pipeline { struct Report; struct Diagnostic; struct Request; enum class Mode : std::uint8_t; }   // 1.11.13

namespace app {

class MacrosPane;   // lot macros 1 : l'onglet Macros
class HmiBuildManager;      // 1.11.13 : la generation incrementale de l'IHM (hmi/HmiBuild.hpp)
class HmiBuildOutputPane;   // 1.11.13 : ses sorties (hmi/HmiBuildPanes.hpp) ; 1.11.14 : le panneau du bas
class HmiLiveSource;        // 1.11.21 : un volet de code montre (hmi/HmiLive.hpp)
struct ConsoleEntry;        // 1.11.14 : une ligne de la Console (hmi/HmiConsole.hpp)
class MacroEditorView;         // lot API 6 : le mode Modifier d'une macro
class TopBar;       // lot API 2 : la barre du haut
class ApiDashboard; // lot API 2 : le tableau de bord de l'API
// ---- Lot API 8 : Centre de simulation ----
class SimCenterModel;                                   // SimCenter.hpp
namespace simstatus { struct Report; struct Snapshot; } // SimStatus.hpp
// ---- fin Lot API 8 : Centre de simulation ----
// ---- Lot API 8 : glisser n'importe quel fichier ----
namespace dropfiles { struct Plan; }                    // DropFilesPlan.hpp
// ---- fin Lot API 8 : glisser n'importe quel fichier ----


class App;
class HmiTutorial;
class HistoryPanel;
struct HistoryMoved;

// --------------------------------------------------------------- 1. Startup ---
//  L'accueil (lot API 7) : a gauche la marque et les actions, au centre les
//  projets recents en cartes, a droite le projet choisi. Les widgets sont dans
//  app/StartPage.hpp ; l'ecran lit le disque et fait ce qu'ils demandent
//  (screens/StartScreen.cpp). Ses signaux se branchent une fois (buildUi) :
//  onEnter, rappele a chaque dialogue ferme, ne double plus rien.
namespace start { class StartPage; struct ProjectEntry; }

class StartupScreen final : public menu::WidgetMenu {
public:
    explicit StartupScreen(App& app);
    ~StartupScreen() override;
    [[nodiscard]] std::string title() const override { return "Start"; }

    // Le clavier de l'accueil (Entree, F2, Suppr, Ctrl+F, les fleches, Echap,
    // Ctrl+N, Ctrl+O, F1 ; taper hors d'un champ cherche) et les fichiers
    // glisses n'importe ou dans la fenetre.
    ui::EventResult HandleEvent(const ui::InputEvent&) override;
    void            Update(const menu::FrameContext&) override;

    // ---- pour les scripts et les tests ----------------------------------------
    // Choisir un projet par son chemin ou son nom (sans souci de la casse ni
    // des accents) ; s'il etait cache, la recherche et le filtre s'effacent.
    bool selectProject(const std::string& pathOrName);
    [[nodiscard]] std::string selectedPath() const;
    // Les noms des cartes, dans l'ordre de l'ecran.
    [[nodiscard]] std::vector<std::string> visibleProjects() const;
    void setSearch(const std::string& text);
    // Un filtre par le debut de son libelle : "Tous", "Proj", "Exp", "FINISH", "LOCK".
    bool chooseFilter(const std::string& labelPrefix);
    // Un tri par le debut de son libelle : "Les plus", "Par nom", "Par etat".
    bool chooseSort(const std::string& labelPrefix);
    // Les gestes des boutons et du clavier, tels quels.
    void openSelected();                                   // Entree, double clic
    void renameSelected();                                 // F2
    void duplicateSelected();
    void deleteSelected() { supprimerProjet(); }           // Suppr
    [[nodiscard]] start::StartPage* page() const noexcept { return page_; }

protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;

private:
    void reload(const std::string& keep = {});             // relire les recents, garder le choix
    [[nodiscard]] std::vector<start::ProjectEntry> readRecent(std::string& autostartName);
    void loadDetails(start::ProjectEntry& e);              // les nombres du panneau de droite
    [[nodiscard]] bool isOpenProject(const start::ProjectEntry& e) const;
    // Ce qui remplace le projet en memoire demande d'abord ce que deviennent
    // ses modifications non enregistrees.
    void confirmReplace(std::function<void()> then);
    // Un seul dialogue demande par image : deux clics dans la meme image n'en
    // ouvrent pas deux.
    [[nodiscard]] bool claimDialog();
    bool handleKey(const ui::KeyDown& k, bool beforeWidgets);
    void triggerAction(const char* id);
    void newProject();
    void openFileDialog();
    void openPath(const std::string& path);               // un dossier, un export, ou plusieurs (';')
    void openTyped();                                      // le champ du chemin, Entree
    void openDropped(std::vector<std::string> paths);
    void showTutorial();                                   // la visite "Decouvrir l'API", sur un projet ouvert
    void showSettings();
    void chooseTheme(const std::string& key);
    void supprimerProjet();
    void openStation();                                    // lot 15
    void askAutostart();                                   // lot 15

    App&                     app_;
    start::StartPage*        page_{nullptr};
    std::vector<std::string> pendingDrops_;                // glisses dans la fenetre, ouverts dans Update
    std::uint64_t            frame_{0};
    std::uint64_t            dialogFrame_{~std::uint64_t{0}};
    core::ConnectionScope    uiLinks_;                     // les widgets : branches une fois (buildUi)
    core::ConnectionScope    busLinks_;                    // le bus : renouvele a chaque onEnter
};

// --------------------------------------------------- 2. Main analysis screen ---
//  Dock layout matching the reference mockup:
//
//   ????????????????????????????????? ToolBar ??????????????????????????????
//   ? rail ?  Project explorer  ?  PLC configuration   ? Variables browser ?
//   ?      ?  (TreeView)        ?  (TabControl +       ? (TableView)       ?
//   ?      ?                    ?   PropertyGrid)      ?                   ?
//   ?      ?????????????????????????????????????????????????????????????????
//   ?      ?  DFB library ? DB library ? Programming sections              ?
//   ?      ?????????????????????????????????????????????????????????????????
//   ?      ?  Diagnostics ? Analysis summary ? Project status              ?
//   ????????????????????????????????? StatusBar ????????????????????????????
struct ApiPaneHosts;       // TaskPanes.hpp (lots API 3 et 4)
struct ApiHmiRead;         // TaskPanes.hpp (lot API 5)

class MainAnalysisScreen final : public menu::WidgetMenu {
public:
    explicit MainAnalysisScreen(App& app);
    [[nodiscard]] std::string title() const override { return "Analysis"; }
    [[nodiscard]] menu::MenuTraits traits() const override;

    // F1 est interceptee ici, avant l'arbre de widgets : elle doit marcher quel
    // que soit ce qui a le focus, et aucun widget de cet ecran ne s'en sert.
    ui::EventResult HandleEvent(const ui::InputEvent&) override;
    // Lot 19 : les boutons Annuler / Retablir (leurs infobulles disent quoi),
    // le point "modifie" d'Enregistrer, l'endroit courant pour l'historique.
    void Update(const menu::FrameContext&) override;

    // Le catalogue de libs/ a relire : apres un enregistrement depuis l'aide,
    // ce qui est en memoire ici ne decrit plus les fichiers.
    void refreshHelpLibrary();

    // Lot API 7 : l'accueil (sans projet ouvert) demande le didacticiel de
    // l'API ; l'ecran l'ouvre des qu'un projet est la (takePendingApiTutorial).
    // `trail` : un parcours a lancer aussi ("api-decouvrir"...) ; vide : les cartes.
    static void requestApiTutorial(std::string trail = {});

    // ---- lot API 7 : les onglets de l'API, pour les scripts et les tests -----
    //  Le volet d'un onglet de l'API ("simulation", "statistiques",
    //  "variables"...) ou qu'il soit - derriere un autre onglet, dans un autre
    //  groupe d'onglets, dans une fenetre detachee : retrouve par son onglet
    //  (apiTab), jamais par l'onglet courant. Nul : pas ouvert. `open` : s'il ne
    //  l'est pas, il s'ouvre d'abord, comme par F9, Ctrl+1 ou Ctrl+5 (sans
    //  projet : le message habituel, et nul).
    [[nodiscard]] ui::Widget* apiPaneContent(const std::string& key, bool open = false);

    // ---- lot 7 : fermer des onglets du centre (MainAnalysisScreen.cpp) ------
    //  Le clic droit sur un onglet (Fermer, Fermer tout, Fermer tout sauf
    //  celui-ci), sa croix et le clic du milieu, Ctrl+W. Les onglets qui ne se
    //  ferment pas restent (fixes, sans croix) ; les pages gardees sont mises
    //  de cote. Ce qui n'est pas enregistre (une macro modifiee dans l'editeur
    //  de l'onglet Macros) fait poser UNE question pour tous les onglets vises ;
    //  `askUnsaved` faux : fermer sans demander (les tests). Rendent le nombre
    //  d'onglets fermes TOUT DE SUITE : 0 quand la question attend sa reponse
    //  (ils se ferment sur "Fermer sans les garder", aucun sur Annuler).
    std::size_t closeTab(std::size_t index, bool askUnsaved = true);
    std::size_t closeAllTabs(bool askUnsaved = true);
    std::size_t closeOtherTabs(std::size_t keep, bool askUnsaved = true);
    std::size_t closeCurrentTab();              // Ctrl+W : l'onglet ouvert, question comprise

    // ---- lot 7 : RENOMMER, EN VOYANT TOUT CE QUI SUIT (RenameWorkspace.cpp) ----
    //  Le modal (RenameDialog) liste ce qui change des deux cotes - le code, les
    //  tables, les vues, les scripts, la variable liee - puis le fait en UNE
    //  commande. `kind` : "variable" (une globale ; "Unite.var" : une variable
    //  d'unite), "ddt", "dfb", "unite", "section", "table", "ihm-variable",
    //  "ihm-vue". Un genre inconnu, un nom introuvable : la barre d'etat le dit.
    void askRename(const std::string& kind, const std::string& name);
    // La meme, le nouveau nom deja tape (les scripts : renommer "k" "a" "b").
    // Faux (et why) : pas de dialogue ouvert.
    bool askRenameTo(const std::string& kind, const std::string& name, const std::string& newName, std::string* why = nullptr);

    // ---- lot 7 : OU EXPORTER, pour les exports en un clic de l'IHM (ExportTarget.hpp) ----
    //  Le dialogue : exports/ du projet deja ecrit (Entree : comme avant), le
    //  bouton ... pour un autre dossier ou un autre nom ; accepte, `then` exporte
    //  et writeHmiExport ecrit a l'endroit choisi. Faux : pas de dossier de projet.
    bool askHmiExportTarget(const std::string& what, const std::string& filter, std::function<void()> then);

    // ---- Lot API 8 : les exports qui demandent ou (HmiRuntimeDialogs.cpp) ----
    //  L'IHM en marche dans l'editeur (le bouton d'export, l'action Exporter,
    //  IHM_EXPORTER, lances par un geste) : le meme dialogue, le fichier propose
    //  en entier (exports/<nom habituel>) ; a la reponse, writeHmiExport ecrit
    //  puis `done(ecrit, ou)` - Annuler : done(false, ""). Faux : pas de dossier
    //  de projet (le moteur ecrit alors dans exports/ du dossier temporaire).
    bool askHmiRuntimeExport(const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done);
    // ---- fin Lot API 8 : les exports qui demandent ou ----

    // ---- Lot API 8 : Centre de simulation > Debogage (SimDebugWorkspace.cpp) ----
    //  Ouvre (ou montre) l'onglet Simulation > Debogage.
    void openSimDebug();
    //  LES POINTS D'ARRET DANS L'ONGLET D'UNE SECTION (openDocument l'appelle) :
    //  la colonne de la marge - un clic, F9 sur la ligne du curseur (Ctrl+F9 :
    //  activer / desactiver) -, les ronds rouges, la ligne d'arret en pause, et
    //  la valeur au survol (les locales d'une unite, l'instance d'un bloc).
    void wireSectionBreakpoints(ui::MultiLineText& editor, domain::Index section);
    //  A chaque image (tickApiPanes) : l'onglet Debogage, les marges des
    //  sections ouvertes, les passages dans le journal (onglet ouvert ou non).
    void tickSimDebug(double now);
    //  Pour les scripts (point-arret-marge) : l'onglet de la section ouvert, la
    //  ligne en vue, ou cliquer dans sa marge. Faux : pas de section de ce nom,
    //  ou pas encore dessinee (a redemander a l'image suivante).
    bool sectionMarginPoint(const std::string& section, int line, gfx::Point& where);
    //  LE JOURNAL DU DEBOGAGE - le point de branchement vers le journal du Centre
    //  de simulation (SimJournal) : le genre ("point-arret", "arret", "pas",
    //  "continuer", "pause", "arreter", "condition", "espion"), la phrase, la
    //  section et la ligne (0 : aucune). Les derniers evenements restent ici
    //  (debugEvents) pour qui les demande.
    void journalDebugEvent(const std::string& kind, const std::string& text, const std::string& section, int line);
    [[nodiscard]] std::vector<std::string> debugEvents() const;
private:
    struct SimDebugState;
    std::shared_ptr<SimDebugState> simDebug_;
    // ---- fin Lot API 8 : Centre de simulation > Debogage ----

    // ---- Lot API 8 : le bandeau bas (StatusStripWorkspace.cpp) ----
    //  La barre d'etat : le dernier message et son journal (les 50 derniers),
    //  la selection de l'onglet, les compteurs (Compiler de l'IHM, A regarder
    //  de l'API), la simulation, la cible, le zoom de l'onglet, le theme,
    //  l'enregistrement et les raccourcis du moment ; chaque morceau se clique.
public:
    //  Ce que l'onglet `page` dit de sa selection, au milieu du bandeau
    //  ("Vitesse_Moteur · REAL · %MW120 · lue par 3 vues") ; "" : rien.
    void setStatusSelection(const ui::Widget* page, std::function<std::string()> describe);
    //  Pour les scripts (messages-journal, barre-compteurs, barre-zoom, barre-theme).
    void openMessageJournal(bool open);
    [[nodiscard]] bool messageJournalOpen() const;
    [[nodiscard]] std::vector<std::string> messageJournalLines() const;
    [[nodiscard]] std::string statusStripText() const;     // les pastilles visibles, dans l'ordre
    void openStatusCounts();                               // le clic sur les compteurs
    [[nodiscard]] int tabZoomPercent() const;              // 0 : l'onglet ouvert n'a pas de zoom
    bool setTabZoomPercent(int percent);                   // faux : pas de zoom dans cet onglet
    bool setStatusTheme(const std::string& name);          // faux : theme inconnu
    //  Le clic d'une pastille par son nom : journal, selection, erreurs,
    //  avertissements, simulation, cible, zoom, theme, enregistrement (faux :
    //  inconnue, ou cachee en ce moment).
    bool clickStatusChip(const std::string& which);
private:
    struct StatusStripState;
    std::shared_ptr<StatusStripState> strip_;
    void buildStatusStrip(ui::OverlayHost& host);
    void tickStatusStrip(double now);
    void runStatusMenu(int id);
    void openStatusMenu(int which);                        // 1 : le zoom, 2 : le theme
    [[nodiscard]] std::string describeStatusSelection(const std::string& key) const;
    // ---- fin Lot API 8 : le bandeau bas ----

    // ---- Lot API 8 : Centre de simulation (SimulationWorkspace.cpp) ----
    //  Le dossier Simulation de l'arbre (entre IHM et Versions) : Vue d'ensemble,
    //  Automate (l'onglet de l'API d'avant), IHM (la simulation de l'IHM),
    //  Equipements, Debogage (openSimDebug), Forcages, Courbes, Journal.
public:
    //  Un onglet du Centre par sa cle : "ensemble", "automate", "ihm",
    //  "equipements", "debogage", "forcages", "courbes", "journal". Faux : cle
    //  inconnue (ou pas de projet, pas d'IHM : la barre d'etat le dit).
    bool openSimCenter(const std::string& key);
    //  Suivre une cle "aller a" (SimJournal.hpp) : un onglet du Centre, une
    //  ligne ("ligne:<section>:<n>"), une variable, une vue, les alarmes,
    //  Simuler / Pause / Un cycle / Arreter / Relancer, la bibliotheque.
    void simCenterGo(const std::string& key);
    //  Le volet d'un onglet du Centre ("ensemble", "forcages", "courbes",
    //  "journal"), ou qu'il soit ; `open` : ouvert s'il ne l'est pas. Nul : pas ouvert.
    [[nodiscard]] ui::Widget* simCenterPane(const std::string& key, bool open = false);
    //  Le calcul du moment (le bandeau, les cartes, l'attention), refait maintenant.
    [[nodiscard]] simstatus::Report simCenterReport();
    //  Le modele des volets : les courbes, les forcages, le dernier rapport.
    [[nodiscard]] SimCenterModel& simCenterModel();
    //  Une courbe de plus : une variable de l'automate ou de l'IHM ("ihm:" devant :
    //  de l'IHM). Faux : `why` dit pourquoi (inconnue, deja la, deja huit).
    bool addSimTrend(const std::string& path, std::string* why = nullptr);
    //  Relacher des forcages par leur cle ("plc:<chemin>", "twin:<equipement>:<adresse>|<type>").
    void releaseSimForcings(const std::vector<std::string>& keys);
private:
    struct SimCenterState;
    std::shared_ptr<SimCenterState> simCenter_;
    bool routeSimNode(ui::NodeId node);          // un clic dans l'arbre sur un noeud du Centre
    void ensureSimCenter();                      // l'etat, les signaux de SimulationHost (une fois)
    void tickSimCenter(double now);              // a chaque image (Update)
    void pollSimCenter(double t);                // le collecteur : ce qui a change
    [[nodiscard]] simstatus::Snapshot simSnapshot(double t);
    void refreshSimBadges();                     // les pastilles du dossier Simulation
    // Les raccourcis de la maquette (handleShortcut, apres les widgets) : F5
    // Simuler / Continuer, Maj+F5 Arreter, F10 la section suivante - pas quand
    // on tape (un champ, un editeur de code), ni quand l'onglet Debogage les prend.
    bool simCenterShortcut(const ui::KeyDown& k);
    // ---- fin Lot API 8 : Centre de simulation ----

protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;
private:
    void bindProject(ProjectRef project, std::shared_ptr<const importer::AnalysisReport> report);
    void onTreeSelection(ui::NodeId node);      // drives the centre and right panes

    // Source preview: sections open as closable tabs next to the configuration
    // tabs and stay open together, so two pieces of logic can be compared.
    void openDocument(domain::Index sectionIndex);
    void closeDocument(std::size_t tabIndex);
    void refreshDocumentList();

    // Lot 7 : fermer des onglets (voir closeTab). Un onglet se ferme s'il a sa
    // croix et n'est pas fixe ; ce qu'il perdrait, une ligne par chose ("la
    // macro X, modifiee dans l'editeur") - vide : une vue du projet, rien a
    // perdre. closeTabs demande (une fois) puis ferme, par PAGE : chaque
    // fermeture decale les indices ; `focus` : l'onglet a montrer ensuite.
    [[nodiscard]] bool tabClosable(std::size_t index) const;
    [[nodiscard]] std::vector<std::string> unsavedInTab(std::size_t index) const;
    std::size_t closeTabs(const std::vector<std::size_t>& indices, bool askUnsaved, ui::Widget* focus = nullptr);
    std::size_t closePages(const std::vector<ui::Widget*>& pages, ui::Widget* focus);
    // Le menu d'un onglet (clic droit sur son en-tete) : il vise sa PAGE.
    void showTabMenu(std::size_t index, gfx::Point at);
    void runTabMenuAction(int action);
    // Lot 7 : L'AFFICHAGE MULTI-FENETRE. Le centre se partage (onglets, groupes
    // cote a cote, mosaique automatique) et un onglet part dans une fenetre a
    // lui (App::detachedWindows). Les onglets retenus par leur INDICE (sections,
    // grafcets, macros, variables, appels) sont recales quand le centre en
    // insere ou en deplace un ; showPage montre une page ou qu'elle soit.
public:
    bool setLayoutMode(const std::string& name);          // "onglets", "groupes", "mosaique"
    bool splitTab(std::size_t index, bool below);         // Diviser a droite / en bas
    bool detachTab(std::size_t index);                    // dans une fenetre a lui
    bool showPage(ui::Widget* page);                      // en onglet (devant) ou sa fenetre (devant)
    [[nodiscard]] ui::TabArea* centreTabs() const noexcept { return centre_; }
    // ---- Lot API 8 : l'arbre du projet (screens/TreeWorkspace.cpp) ----
    //  Le filtre au-dessus de l'arbre (Ctrl+Maj+F) : l'arbre ne garde que ce qui
    //  correspond (deplie, surligne), plus ce qu'Aller a... trouve DANS le
    //  contenu (FilterHit, sous le dossier de son domaine). "" : efface, l'arbre
    //  revient deplie comme avant.
    void setTreeFilter(const std::string& text);          // le champ suit
    // 1.11 (chantier T3, C4) : les deux filtres de l'arbre, des mots reserves du champ.
    static constexpr std::string_view kTreeFilterNotCompiling = "@ne-compile-pas";
    static constexpr std::string_view kTreeFilterNotGenerated = "@non-genere";
    [[nodiscard]] const std::string& treeFilterText() const noexcept { return treeFilterText_; }
    [[nodiscard]] std::size_t treeFilterCount() const noexcept { return treeFilterCount_; }
    [[nodiscard]] std::vector<std::string> treeLines() const;   // ce que montre l'arbre (arbre-lignes)
    void focusTreeFilter();                               // Ctrl+Maj+F
    //  Les outils sortis de l'arbre (la rangee de boutons sous API et IHM) :
    //  le bouton k ouvre ce qu'ouvrait son ancien noeud. `name` : le debut du
    //  texte de l'ancien noeud ("Compiler", "Exporter", "Statistiques") ou du
    //  bouton ("Echanges") ; faux : pas un outil de ce domaine.
    bool openTreeTool(bool hmi, std::size_t k);
    bool openTreeTool(bool hmi, const std::string& name);
    //  La pastille rouge : les expressions impossibles du dernier Generer /
    //  Compiler de l'IHM, sur IHM > Vues et sur le titre IHM ("2 erreurs").
    void setTreeExprErrors(std::size_t count, std::size_t inViews);
    //  Le dernier compte (et son projet) : l'arbre se refait a chaque modification, la pastille reste.
    std::size_t treeExprErrors_{0}, treeExprErrorsInViews_{0};
    std::string treeExprFolder_;
    //  Les versions en bref (le travail en cours, les 5 dernieres, "Voir les N
    //  versions...") ; vrai : toutes, comme avant.
    void showAllTreeVersions(bool all);
    //  Epingles (clic droit > Epingler ; retenus par projet dans les reglages de
    //  l'utilisateur, cle "arbre.epingles.<projet>") et Recents (les 4 derniers
    //  noeuds ouverts depuis l'arbre), en haut. pinTreePath : le chemin se lit
    //  comme pour le script "arbre" ("IHM/Vues/Vue_A"). Faux : rien n'a change.
    bool pinTreeNode(ui::NodeId node, bool pin);
    bool pinTreePath(const std::string& path, bool pin);
    //  1.11.12 : le noeud d'un chemin ("IHM/Symboles/S_Vanne", lu comme pour pinTreePath) ;
    //  tout son sous-arbre (lui d'abord, profondeur d'abord, `cap` noeuds au plus, sans
    //  les membres des variables ni Epingles / Recents) ; visiter un noeud : son menu
    //  (clic droit), sa carte (survol), puis son clic - ce que fait la souris. Le script
    //  arbre-parcourir s'en sert pour chercher les plantages d'un genre de noeud (la
    //  1.11.11 plantait sur un clic sur Fonctions ou Popups d'un symbole).
    [[nodiscard]] ui::NodeId treeNodeOfPath(const std::string& path) const;
    [[nodiscard]] std::vector<ui::NodeId> treeSubtree(ui::NodeId from, std::size_t cap) const;
    std::string visitTreeNode(ui::NodeId node);
    [[nodiscard]] bool isTreePinned(ui::NodeId node) const;
    [[nodiscard]] static bool treePinnable(ui::NodeId node);   // pas les titres ni les lignes du lot
    void tickTree(double now);                            // a chaque image (tickApiPanes)
    //  2e partie. Les didacticiels qui visaient un ancien noeud d'outil
    //  (ApiStatistics, HmiCompile...) : ou est son bouton dans la rangee (faux :
    //  pas un outil, ou pas dessine) ; revealTreeTool montre la rangee.
    bool treeToolRect(ui::NodeId oldNode, gfx::Rect& out) const;
    bool revealTreeTool(ui::NodeId oldNode);
    //  La portee (le rail) : "tout", "epingles", "api", "ihm", "simulation",
    //  "versions" ; la densite (serre : 22 px, large : 26 px ; retenue) ; tout
    //  replier ; suivre l'onglet actif (l'arbre montre et choisit ce qu'on regarde).
    bool setTreeScope(const std::string& scope);
    [[nodiscard]] const std::string& treeScope() const noexcept { return treeScope_; }
    bool setTreeDensity(const std::string& density);
    void collapseTree();
    void setTreeFollow(bool on);
    [[nodiscard]] bool treeFollow() const noexcept { return treeFollow_; }
    void followActiveTab();                               // l'onglet actif : son noeud, montre et choisi
    //  Ce qui a change depuis la derniere version (le point orange) ; les mises
    //  a jour de bibliotheque (la pastille orange) ; la sante du projet (le pied).
    void setTreeChanges(const std::vector<std::string>& changedKinds, int sinceVersion);
    void setTreeLibraryUpdates(std::size_t count);
    [[nodiscard]] std::string treeHealthText() const;
    [[nodiscard]] std::string treeCardText(ui::NodeId node) const;   // la carte au survol
    void refreshTreeState();                              // le rail, le pied, les mises a jour : tout de suite
    void openTreeHealthPart(const std::string& part);     // simulation, erreurs, bibliotheque, versions
    [[nodiscard]] std::string treeCurrentCard() const;    // la carte du noeud choisi (arbre-carte)
    bool treeActionOnCurrent(std::size_t action);         // 0 epingler, 1 detacher, 2 le menu (arbre-action)
    // ---- fin Lot API 8 : l'arbre du projet ----
    // ---- 1.11.13 : la generation incrementale, pour les scripts (ScriptRunner) et les essais ----
    //  mode : "generer", "regenerer", "compiler", "generer-compiler", "regenerer-compiler",
    //  "demarrer", "nettoyer" ; portee : une cle ("script:12") ou un chemin ("IHM/Vues"),
    //  vide : tout. `dialog` : la fenetre de progression tout de suite.
    bool scriptHmiBuild(const std::string& mode, const std::string& scope, bool dialog, std::string* why);
    [[nodiscard]] bool hmiBuildBusy() const;
    [[nodiscard]] std::string hmiBuildSummary() const;   // l'etat du projet et le dernier build, en une ligne
    void showHmiBuildOutputs(int tab);                    // 0 : Sorties ; 1 : Diagnostics
    // Modifier le code d'un script de l'IHM (comme au clavier : une commande, Ctrl+Z la rend).
    bool scriptHmiEditScript(const std::string& name, const std::string& line, std::string* why);
private:
    // ---- Lot API 8 : l'arbre du projet ----
    [[nodiscard]] ui::WidgetPtr wrapExplorer(std::unique_ptr<ui::TreeView> tree);   // le champ au-dessus de l'arbre
    void applyTreeFilter(const std::string& text);
    bool openTreeFilterHit(ui::NodeId node);              // un resultat du contenu : comme Aller a...
    ui::InputText*                    treeFilter_{nullptr};
    ui::Widget*                       treeFilterInfo_{nullptr};   // "N resultats pour ..."
    std::string                       treeFilterText_;
    std::size_t                       treeFilterCount_{0};
    std::vector<ui::NodeId>           treeFilterSaved_;           // les noeuds deplies avant le filtre
    std::vector<GoToPanel::Result>    treeFilterResults_;         // rang = index du FilterHit
    void noteTreeRecent(ui::NodeId node);
    void refreshTreeShortcuts();
    void loadTreePins();
    void saveTreePins() const;
    [[nodiscard]] std::string treePinsKey() const;
    std::vector<ui::NodeId>           treePins_, treeRecents_;
    bool                              treeShortcutsDirty_{false};
    double                            treeNow_{0.0}, treeRecentAt_{0.0};
    // 2e partie : le rail, le pied, la densite, suivre l'onglet actif.
    ui::Widget*                       treeRail_{nullptr};
    ui::Widget*                       treeFoot_{nullptr};
    std::string                       treeScope_{"tout"};
    bool                              treeFollow_{true};
    ui::Widget*                       treeFollowedPage_{nullptr};   // la derniere page suivie
    std::uint64_t                     treeLibRevision_{~0ull};      // les mises a jour, recomptees quand le projet change
    double                            treeChromeAt_{0.0};
    void refreshTreeChrome();                                       // le rail et le pied (leurs etats)
    void openTreeHealth(const std::string& part);                   // un morceau du pied
    void treeHoverAction(ui::NodeId node, std::size_t action);      // Epingler, Detacher, ...
    // ---- fin Lot API 8 : l'arbre du projet ----
    void shiftTabIndices(std::size_t removed);            // un onglet est sorti a cet indice
    void onTabInserted(std::size_t at);
    void onTabMoved(std::size_t from, std::size_t to);
    void saveLayoutMode();
    ui::PopupMenu*        tabMenu_{nullptr};
    ui::Widget*           tabMenuPage_{nullptr};

    // The View tab and its persistence.
    ui::WidgetPtr buildViewPanel();
    void          applyPanelVisibility();
    void          loadWorkspace();
    void          saveWorkspace();

    // ---- right-click ------------------------------------------------------
    // What a node offers depends on what it is. The ids are stable and explicit
    // rather than row numbers: adding a separator must not renumber the actions.
    enum ContextAction : int {
        ActionOpen = 1,
        ActionDelete,
        ActionPublishToLibrary,
        ActionImportFromLibrary,
        ActionExpandAll,
        ActionCollapseAll,
        // L'ordre d'execution, et l'aide du noeud. Ajoutes a la FIN : les
        // numeros sont ce que le menu renvoie, et renumeroter les anciens ferait
        // repondre "Supprimer" a un clic sur "Publier".
        ActionMoveUp,
        ActionMoveDown,
        ActionHelp,
        // Lot 20 : une vue de l'IHM - en faire un modele, l'exporter.
        ActionHmiSaveTemplate,
        ActionHmiExportView,
        // Lot macros 1 : une macro, un dossier de macros.
        ActionMacroLaunch,
        ActionMacroEdit,
        ActionMacroShow,
        ActionMacroNew,
        ActionMacroNewFolder,
        ActionMacroRename,
        ActionMacroDelete,
        // Lot API 3 : une variable -> une table d'animation. Un numero par
        // table, a partir de celui-ci (le menu rend le numero de l'entree).
        ActionAddToAnimationTable = 1000,
        // Lot 7 : les entrees du menu par genre de noeud (ExplorerMenus.cpp)
        // portent leur action : l'id = ActionCall + son rang dans contextCalls_.
        ActionCall = 100000,
    };
    // The run indicator and transport on the workspace toolbar.
    void refreshSimulationIndicator();
    void runSimulationTransport(std::string_view id);   // sim.run / pause / stop / step
    void askHmiForce(std::string variable);             // le dialogue "Forcer" du volet Simulation IHM

    // Opens the drawn chart for an engine instance, or says why it cannot.
    // Named by the instance rather than by a chart index: the instance is what a
    // reader double-clicks, and what survives the project being re-analysed.
    void openGrafcet(const std::string& instanceName);
    // The chart editing handlers. They live on the screen and not on the pane
    // because only a screen can put up a modal and only the screen owns the
    // command stack - the pane reports intent and nothing else.
    //
    // They take a ui::Widget* because the pane is a private class in the
    // implementation file, and dragging it into this header only so a parameter
    // could be spelled would put a whole widget into everyone's compile for the
    // sake of one pointer. The cast is in one place and is checked there.
    void editGrafcetPart(ui::Widget* pane, domain::Index section, int part, int id);
    void removeGrafcetPart(ui::Widget* pane, domain::Index section, int part, int id);
    void insertGrafcetPart(ui::Widget* pane, domain::Index section, int tool, int at);
    void linkGrafcet(ui::Widget* pane, domain::Index section, int transitionId, int stepId);
    void refreshGrafcetPane(const std::string& paneId, domain::Index section);
    void refreshOpenDocuments();
    void saveLayoutFile();

    // One tab per variable: what it is, what it contains, who writes it, who
    // reads it. The counts the analyser already had could answer none of that.
    void openVariable(domain::Index variable);
    // Une sous-routine : sa source, et surtout QUI L'APPELLE - la seule question
    // qui se pose devant une SR et que la table des sections ne savait pas poser.
    void openSubroutine(domain::Index section);

    // ---- macros ----------------------------------------------------------
    // LOT MACROS 1 : L'ONGLET MACROS (MacrosPane). La liste en dossiers, la
    // fiche, le formulaire type, l'apercu en direct, Appliquer = une commande.
    // L'onglet du code reste : "Modifier le code".
    void refreshMacroList();
    // Le code d'une macro, dans un onglet : le lire, le corriger, l'enregistrer.
    void openMacro(const std::string& name);
    // Lot API 6 : l'editeur d'une macro (le mode Modifier, dans l'onglet
    // Macros) ; nul : pas ouvert. makeMacroEditor le fabrique, pret a servir.
    MacroEditorView* macroEditor(const std::string& name);
    std::unique_ptr<MacroEditorView> makeMacroEditor(const std::string& name);
    // Le formulaire de la macro (l'onglet Macros), avec le code de libs/ ou
    // celui de l'editeur (pas encore enregistre).
    void runMacro(const std::string& name);
    void runMacroSource(const std::string& name, std::string source);
    void saveMacro(const std::string& name, std::string source, bool askName);
    [[nodiscard]] std::string macroSourceOf(const std::string& name) const;
    // L'onglet Macros : ouvert (ou montre), la macro choisie, lancee si `launch`.
    void openMacros(const std::string& select = {}, bool launch = false);
    [[nodiscard]] MacrosPane* macrosPane() const;
    project::macro::MacroMemory& macroMemory();
    // La bibliotheque des macros a change : l'onglet et l'arbre suivent.
    void macrosChanged(const std::string& select = {});
    // Les dialogues de l'onglet Macros (MacrosWorkspace.cpp).
    void askNewMacro(const std::string& folder);
    void askNewMacroFolder(const std::string& parent);
    void askRenameMacro(const std::string& name);
    void askDuplicateMacro(const std::string& name);
    void askDeleteMacro(const std::string& name);
    void askRenameMacroFolder(const std::string& folder);
    void askDeleteMacroFolder(const std::string& folder);
    void restoreMacro(const std::string& name);
    void askPurgeMacro(const std::string& name);
    void askSaveMacroProfile(const std::string& name, std::map<std::string, std::string> answers);
    void openMacroHelp(const std::string& name);
    void wireMacroTreeDrag();
    void openGrafcetFor(const std::string& symbol);   // a name from anywhere
    [[nodiscard]] std::string grafcetInstanceAt(ui::NodeId) const;

    void showExplorerMenu(ui::NodeId node, gfx::Point at);
    void runExplorerAction(int action);
    // ---- lot 7 : le clic droit adapte a chaque noeud (ExplorerMenus.cpp) -------
    //  Le menu d'un noeud de l'arbre, selon ce qu'il est (un dossier de l'API,
    //  une variable, une unite, une section, un bloc, un type, une table, la
    //  simulation, une vue de l'IHM, une variable IHM, une version...). Chaque
    //  entree fait quelque chose de reel ; grisee, elle dit pourquoi. Les
    //  entrees Renommer (F2) et Supprimer (Suppr) sont aussi celles des touches
    //  sur le noeud courant de l'arbre (keys).
    struct TreeKeys {
        std::function<void()> rename, remove;
        std::string           renameWhy, removeWhy;   // grisee : pourquoi
    };
    void buildExplorerMenu(ui::NodeId node, std::vector<ui::PopupMenu::Item>& items, TreeKeys& keys);
    void treeKey(ui::NodeId node, bool rename);        // F2 (rename) ou Suppr sur l'arbre
    std::vector<std::function<void()>> contextCalls_;  // les actions des entrees (ActionCall + rang)
    // Lot 7 : l'infobulle d'un noeud, relue tant qu'elle est ouverte - la valeur
    // d'une variable en simulation, l'etat et le cycle de API > Simulation ;
    // "" : celle de l'arbre (TreeView::setNodeTooltip).
    [[nodiscard]] std::string explorerTip(ui::NodeId node) const;

    // ---- l'IHM (HmiWorkspace.cpp) -------------------------------------------
    //  Les entrees du dossier IHM de l'arbre ouvrent des onglets ici, a cote des
    //  sections. Ces onglets sont retrouves par leur PAGE et non par leur
    //  indice : fermer un onglet a gauche decale les indices, pas les pages.
    //  La cle dit ce qu'un onglet montre : "config", "vues", "vue:17"...
    [[nodiscard]] static bool isHmiNode(ui::NodeId);
    void openHmiNode(ui::NodeId node);
    void openHmiPane(const std::string& key);
    // Lot 15 : l'outil Modbus vers cette cible ; tab : "lecture", "cyclique",
    // "trames", "espion", "ping".
    void openHmiModbusTool(const std::string& host, int port, int unit, const std::string& tab);
    // Lot 15 : "Lier une variable" a un equipement ; lot 17 : a une adresse et d'un type
    // (Carte memoire : "Creer une variable IHM ici").
    void askHmiBind(const std::string& equipment, const std::string& address = {}, const std::string& type = {});
    void askHmiImportTwin(const std::string& equipment);   // lot 17 : un CSV dans la memoire d'un jumeau
    void openHmiView(std::uint64_t viewId, int part = -1, std::uint64_t objectId = 0);
    void bindHmi();                           // a chaque (re)liaison du projet
    // 1.11 (chantier T3, C4) : l'etat de Compiler et de Generer que l'arbre lit
    // (ses icones). Tout est refait quand le projet de l'automate change ou a
    // Compiler (`full`) ; sinon seuls les scripts dont le texte a change.
    void refreshBuildState(bool full = false);
    std::shared_ptr<hmi::build::Cache> buildState_;
    const domain::Project*             buildStateOf_{nullptr};
    // ---- 1.11.13 : LA GENERATION INCREMENTALE DE L'IHM (HmiBuildWorkspace.cpp) ----
    //  Le gestionnaire de build (un fil a part, une copie du projet), ses etats dans
    //  l'arbre (refaits a chaque analyse), ses commandes (le menu de l'arbre, les
    //  barres des editeurs, Demarrer), sa progression (une fenetre apres 0,3 s) et
    //  ses sorties (l'onglet IHM . Sorties, double-clic : la source).
    std::shared_ptr<HmiBuildManager> hmiBuild_;
    std::uint64_t                    hmiBuildMarksGen_{~0ull};
    bool                             hmiBuildDialogOpen_{false};
    bool                             hmiBuildBackground_{false};
    double                           hmiBuildStartedAt_{-1.0};   // l'heure de l'ecran au lancement
    std::string                      hmiBuildTitle_;
    bool                             hmiBuildStarts_{false};     // un Demarrer (la fenetre se ferme seule)
    std::function<void(const hmi::pipeline::Report&)> hmiBuildThen_;
    struct HmiBuildDone;
    std::vector<std::shared_ptr<HmiBuildDone>> hmiBuildHistory_;   // les derniers builds (l'onglet Sorties les relit)
    core::ConnectionScope            hmiBuildLinks_;
    void ensureHmiBuild();
    // Lancer un build ; faux (et pourquoi dans la barre d'etat) : un build tourne deja.
    // `then` : a la fin (le rapport), sur le fil de l'interface.
    bool runHmiBuild(hmi::pipeline::Mode mode, std::vector<std::string> scope, bool chosen, std::string title,
                     std::function<void(const hmi::pipeline::Report&)> then = {});
    void tickHmiBuild();
    // 1.11.21 : LES DIAGNOSTICS EN DIRECT (hmi/HmiLive.hpp) - le volet de code montre sur la page
    // courante (nul : aucun) ; a chaque image, le panneau du bas reprend ses diagnostics s'ils ont
    // change (un autre document, une frappe).
    [[nodiscard]] HmiLiveSource* shownLiveSource() const;
    void tickHmiLive();
    // Le build d'un demarrage de la simulation IHM (buildGate) ; un build deja en
    // cours (Generer depuis l'arbre) : le demarrage le suit.
    void startHmiBuild(const std::string& source);
    std::string hmiStartPending_;   // non vide : un demarrage attend la fin du build en cours
    // 1.11.15 : la phase G du prochain Demarrer (la remanence de simulation : ce qui sera rendu).
    std::string hmiStartRestore_;
    bool        hmiStartRestoreOff_{false};
    void applyHmiBuildMarks();
    [[nodiscard]] HmiBuildOutputPane* hmiBuildOutput(bool open);
    void replayHmiBuilds(HmiBuildOutputPane& pane);   // un onglet Sorties rouvert : les builds de la seance
    // ---- 1.11.14 : LE PANNEAU DU BAS (Sorties, Console, Diagnostics) ----
    //  Sous les onglets du centre, redimensionnable ; Ctrl+J le montre ou le replie,
    //  Affichage > Panneaux aussi (le choix est retenu). `tab` : l'onglet a montrer.
    void showBottomPanel(bool show, std::size_t tab = static_cast<std::size_t>(-1));
    void toggleBottomPanel();
    [[nodiscard]] bool bottomPanelShown() const;
    void wireBottomPanel();                           // ses signaux (une fois, a la construction)
    void openConsoleSource(const ConsoleEntry& e);    // le double-clic sur une ligne de la Console
    // ---- 1.11.15 : LE CYCLE DE LA SIMULATION (HmiBuildWorkspace.cpp) ----
    //  L'ARRET SUR MODIFICATION : au demarrage de la simulation, l'empreinte de chaque
    //  element (son contenu, sa configuration, son interface) ; a chaque analyse pendant
    //  la marche, ce qui a change. Une modification du developpeur qui change une
    //  empreinte arrete la simulation (proprement : donnees gardees, journaux gardes) ;
    //  celle que la simulation fait elle-meme (une recette, un utilisateur, un forcage
    //  de jumeau : applyFromSimulation) ne l'arrete pas ; une modification qui ne change
    //  aucune empreinte (une description, un dossier, la grille de l'editeur) non plus.
    std::unordered_map<std::string, std::string> hmiRunPrints_, hmiRunPaths_;
    bool hmiRunWatch_{false};
    bool hmiEditSeen_{false}, hmiLiveSeen_{false};
    int  hmiApplyingLive_{0};
    bool hmiRestartNoAsk_{false};                    // « Ne plus demander pour cette session »
    void hmiSimulationLifecycle(bool started);
    void checkRunningModifications();
    void askHmiRestart(std::function<void(bool)> answer);
    void rebuildHmiFor(const std::string& mode);    // "regenerer" (puis redemarrer), "compiler"
public:
    // Une modification venue de la simulation (une recette, un utilisateur, un jumeau) :
    // appliquee comme les autres, elle n'arrete pas la simulation.
    void applyFromSimulation(core::CommandPtr cmd);
    // Pour les sessions rejouees (ihm-console-etat) : les chiffres de la Console et sa
    // derniere ligne ; une ligne (message ou source) contient-elle ce texte ?
    [[nodiscard]] std::string hmiConsoleSummary() const;
    [[nodiscard]] bool hmiConsoleHas(std::string_view text) const;
    // 1.11.21 (les sessions) : ce que montrent les Diagnostics du panneau du bas (le direct, puis le
    // build) ; une ligne des Sorties.
    [[nodiscard]] std::string hmiDiagnosticsSummary() const;
    [[nodiscard]] bool hmiDiagnosticsHas(std::string_view text) const;
    [[nodiscard]] bool hmiOutputsHas(std::string_view text) const;
    // 1.11.15 (ihm-remanence, ihm-sim-etat, ihm-variable) : l'option de la remanence de
    // simulation ; l'etat de la simulation IHM en clair ; la valeur d'une variable IHM.
    void scriptHmiKeepData(bool on);
    [[nodiscard]] std::string hmiSimulationSummary() const;
    [[nodiscard]] std::string hmiVariableText(const std::string& name) const;
private:
    // Les barres des editeurs (scripts, fonctions) : leurs commandes de build et leur etat.
    template <class Hosts> void wireHmiBuildHosts(Hosts& hosts) {
        hosts.build = [this](hmi::pipeline::Mode mode, const std::string& key) { runHmiBuildFor(mode, key); };
        hosts.buildState = [this](const std::string& key) {
            const auto b = hmiBuildBadge(key);
            return std::pair<std::string, std::string>{b.known ? b.glyph + " " + b.label : std::string("\xE2\x80\x94 analyse\xE2\x80\xA6"), b.tip};
        };
        hosts.buildOutputs = [this] { (void)hmiBuildOutput(true); };
        hosts.showDiagnostics = [this] { showHmiBuildOutputs(1); };     // 1.11.21 : la barre d'un volet de code
    }
    void runHmiBuildFor(hmi::pipeline::Mode mode, const std::string& key);
    void openHmiDiagnostic(const hmi::pipeline::Diagnostic& d);
    void openHmiElement(const std::string& key);
    // L'etat d'un element pour une barre d'editeur : glyphe, ton, libelle, raison.
    struct HmiBuildBadge { std::string glyph, label, tip; ui::Tone tone{ui::Tone::None}; bool known{false}; };
    [[nodiscard]] HmiBuildBadge hmiBuildBadge(const std::string& key) const;
    // Ce qu'un noeud (une cle, ou des dossiers) a au dernier build : son etat en mots,
    // ses diagnostics, son artefact (le chemin complet ; vide : aucun).
    struct HmiBuildInfo {
        std::string                            state;         // "A jour", "Modifie", "3 elements : 1 en erreur..."
        std::vector<hmi::pipeline::Diagnostic> diagnostics;   // les erreurs d'abord
        std::string                            artifact;      // le chemin complet de l'artefact (vide : aucun)
    };
    [[nodiscard]] HmiBuildInfo hmiBuildInfo(const std::string& key, const std::vector<std::string>& paths) const;
    void onHmiChanged(std::uint64_t viewId);  // une commande IHM vient de passer
    void forgetHmiTab(std::size_t tabIndex);  // cet onglet va etre ferme
    void askNewHmiView(const std::string& role = "vue");   // le dialogue "Nouvelle vue" (lot 8 : et son role)
    void openHmiViewsFolder(int folder);      // lot 8 : le volet Vues sur un dossier (Modeles, Vues, Popups...)
    void askDeleteHmiView(std::uint64_t viewId);
    void askHmiSymbol(std::uint64_t viewId);  // lot 10 : "Creer un symbole" (le nom, les parametres)
    // 1.11.3 : le carre de legende d'une case - le selecteur de valeur, la creation d'une
    // variable inconnue (le type, la zone API ou IHM), puis la case recoit la valeur.
    void askHmiValue(std::uint64_t viewId, const valuekind::Request& request);
    void askHmiCreateVariable(std::uint64_t viewId, const valuekind::Request& request, const std::string& text, bool fx,
                              const std::string& name);
    void commitHmiValue(std::uint64_t viewId, const valuekind::Request& request, const std::string& text, bool fx);
    void askHmiStyle(std::uint64_t viewId);   // lot 12 : "Creer un style" (son nom)
    void askHmiDuplicate(std::uint64_t viewId);   // 1.10.2 (chantier D) : "Dupliquer..." (reperes, indices, pose)
    void askHmiDuplicateReplace(std::uint64_t viewId);   // lot 12 : "Dupliquer en remplacant"
    // 1.11.18 (refonte des scripts, lot 5) : "Migrer les declarations..." (IHM > Compiler) - le
    // rapport et les codes a cocher, la version "Avant migration des declarations", une commande.
    void askHmiMigrateDeclarations();
    // 1.11.19 (refonte des scripts, lot 6) : le selecteur de types (app::typepicker : une grille,
    // un volet le demandent) - les recents (reglage hmi.types.recents), et "Ouvrir la definition"
    // d'un type IHM (Types IHM) ou d'un DDT (les types derives de l'API).
    void askHmiType(HmiTypePicker::Spec spec, typepicker::Done done);
    void openHmiTypeDefinition(const std::string& key);
    void askHmiViewFromType();                           // lot 12 : "Depuis un type" (DDT, DFB)
    // Les dialogues des volets Ressources et Fichiers externes (HmiWorkspace.cpp).
    void askHmiImportResource();
    // Lot 13 : Configuration > Langues - ajouter une langue, importer un classeur.
    void askHmiAddLanguage();
    void askHmiImportTranslations();
    void askHmiAddUnit();                     // lot 13 : Configuration > Unites et formats
    void askHmiReplaceResource(std::uint64_t resourceId);
    void askHmiRenameResource(std::uint64_t resourceId);
    void askHmiDeleteResource(std::uint64_t resourceId);
    void askHmiLinkFile(bool database);
    void askHmiRemoveFile(std::uint64_t fileId);
    void askHmiChoosePart(std::uint64_t fileId);
    // La programmation (lot 3) : les scripts d'une vue ou generaux (viewId 0),
    // et le script d'un constat a sa ligne ; un constat de Generer / Compiler.
    void openHmiScripts(std::uint64_t viewId, std::uint64_t scriptId = 0, int line = 0);
    void openHmiIssue(const hmi::Issue& issue);
    void askHmiNewScript(hmi::ScriptLang lang);
    void askHmiRenameScript(std::uint64_t scriptId);
    void askHmiDeleteScript(std::uint64_t scriptId);
    void askHmiVariable(std::uint64_t variableId);        // 0 : une nouvelle
    // Lot 16 : Tableau... (les bornes et le type des cases) ; `done` recoit le type.
    void askHmiArrayType(const std::string& current, std::function<void(const std::string&)> done);
    void askHmiDeleteVariable(std::uint64_t variableId);
    // Le lot 7 : les fonctions IHM (Programmation generale > Fonctions).
    void openHmiFunctions(std::uint64_t functionId = 0, int line = 0);
    // 1.11.21 : un noeud du contenu d'un code (l'arbre) : son code, a l'onglet ou a la ligne voulus.
    void openHmiCodeTarget(const ProjectTreeModel::CodeTarget& t);
    void askHmiNewFunction();
    void askHmiDeleteFunction(std::uint64_t functionId);
    void askHmiTryFunction(std::uint64_t functionId);
    // F1 dans l'IHM : le volet d'aide sur le sujet de l'endroit (faux : pas
    // dans l'IHM, l'aide de la bibliotheque prend la main) ; le didacticiel.
    bool openHmiHelpNow();
    void openHmiHelp(const std::string& topic);
    void startHmiTutorial();
    // ---- lot 19 : l'historique et les raccourcis (HistoryWorkspace.cpp) ------
    //  Ctrl+Z / Ctrl+Y / Ctrl+S / Ctrl+H apres les widgets (un champ en saisie
    //  garde son Ctrl+Z) ; Ctrl+Tab et Ctrl+PgSuiv/PgPrec avant (un editeur de
    //  code prendrait la tabulation). L'endroit de chaque commande : l'onglet
    //  du centre et son sous-onglet ; y retourner apres une annulation.
    void wireHistory();
    void toggleHistory();
    void describePlace(core::CommandInfo& info) const;
    [[nodiscard]] std::string currentPlaceKey(std::string* name = nullptr) const;
    void goToPlace(const std::string& key);
    void askGoToHistory(std::uint64_t serial);
    void onHistoryMoved(const HistoryMoved& e);
    bool handleShortcut(const ui::KeyDown& k, bool beforeWidgets);
    void refreshHistoryButtons();
    void cycleSubTab(int step);
    void cycleCentreTab(int step);
    [[nodiscard]] ui::TabControl* subTabsOf(ui::Widget* page) const;
    // ---- lot 20 : les modeles de vues, exporter et importer des vues ---------
    //  (TemplatesWorkspace.cpp). Ma bibliotheque : un dossier a cote des
    //  reglages ; les modeles du projet : dans le projet (ihm/modeles/).
    // ---- lot 20 : Aller a... (Ctrl+K) (GoToWorkspace.cpp) ---------------------
    void openGoTo();
    [[nodiscard]] std::vector<GoToPanel::Result> goToSearch(const std::string& text) const;
    void goToResult(const GoToPanel::Result& r);
    // Lot recherche : ALLER A... CHERCHE PARTOUT. Un index de tout ce qui a un
    // nom (et du code, des scripts, des textes), refait quand le projet change -
    // pas a chaque frappe ; une recherche par categorie, avec leurs totaux.
    void refreshGoToIndex();
    [[nodiscard]] GoToPanel::Outcome goToSearchAll(const std::string& text, int group);
public:
    // (GoToWorkspace.cpp : publique, ses fonctions de recherche la lisent.)
    struct GoToIndex;
    // Pour les scripts (aller-a) : le panneau d'Aller a..., ouvert s'il le faut.
    [[nodiscard]] GoToPanel* goToPanel(bool open);
private:
    std::shared_ptr<GoToIndex> goToIndex_;
    [[nodiscard]] std::string hmiTemplateFolder() const;
    void askHmiSaveTemplate(std::uint64_t viewId);
    void askHmiExportViews(std::uint64_t selectedView);
    void askHmiImportViews(const std::string& path = {});
    // 1.11.2 (decision 162) : exporter des symboles dans un fichier (.xpgsymboles), l'importer dans un autre projet.
    void askHmiExportSymbols(std::uint64_t selectedSymbol);
    void askHmiImportSymbols(const std::string& path = {});
    // 1.11.2 (decision 174) : exporter des types IHM (kind 0, .xpgtypes), des fonctions IHM
    // (1, .xpgfonctions), des scripts generaux (2, .xpgscripts) ; "Importer..." ouvre tout paquet.
    void askHmiExportPrograms(int kind, std::uint64_t selected);
    // 1.11.2 (decision 188) : la fenetre d'import, sur `path` s'il est donne (un paquet lache sur l'appli), sinon
    // vide (Parcourir... dans la fenetre) ; `then` : une fois fermee (Importer ou Annuler) - le reste d'un depot.
    void askHmiImport(const std::string& path = {}, std::function<void()> then = {});
    void askHmiImportTemplate();
    void askHmiManageTemplates();
    void createHmiViewFromTemplate(const std::string& key, const std::string& name, const std::string& role,
                                   int width, int height, const std::string& description);
    // ---- lot 21 : les versions (VersionsWorkspace.cpp) -------------------------
    //  Le volet Versions (IHM > Versions, a la racine de l'arbre) ; creer,
    //  restaurer, supprimer, exporter, extraire ; comparer deux versions et
    //  restaurer un element seul (une commande : Ctrl+Z l'annule).
    void openVersions(int select = -1);
    void refreshVersions();
    void askCreateVersion();
    // Lot API 6 : l'etat du projet suit les versions - le bloc "V5 en cours"
    // de la barre du haut, son menu, Terminer (FINISH), Livrer et verrouiller
    // (LOCK), la version intermediaire (VersionsWorkspace.cpp).
    void refreshVersionChip();
    void onVersionAction(const std::string& id);
    void askFinishVersion();
    void askDeliverVersion();
    // Lot API 6 : l'icone du projet et son editeur en pixel art (IconWorkspace.cpp).
    void showIconEditor();
    void askRestoreVersion(int number);
    void askDeleteVersion(int number);
    void exportVersion(int number);
    void askExtractVersion(int number);
    void openVersionCompare(int a, int b, const std::string& elementKey = {});
    bool restoreVersionElement(const hmi::ver::Element& e, int from, std::string* why);
    HistoryPanel*                      historyPanel_{nullptr};
    GoToPanel*                         goToPanel_{nullptr};    // lot 20 : Aller a...
    GoToBox*                           goToBox_{nullptr};
    std::uint64_t                      historyRevision_{~0ull};
    bool                               shownModified_{false};
    HmiTutorial*                       hmiTutorial_{nullptr};
    core::ConnectionScope              tutorialLinks_;
    // ---- lot 21 : le didacticiel en parcours (TutorialWorkspace.cpp) ---------
    //  Les parcours au choix (leurs cartes, dans l'aide), leur progression dans
    //  les reglages ("didacticiel.<cle>.etape", ".fait") ; lancer ou reprendre
    //  un parcours ; ses etapes interactives regardent l'ecran (l'onglet
    //  courant, la selection, le projet) et Montre-moi le fait a la place.
    [[nodiscard]] std::vector<TrailCard> tutorialTrails() const;
    void startTrail(const std::string& key, bool resume);
    void refreshTrailCards();
    [[nodiscard]] std::string currentHmiKey() const;
    [[nodiscard]] bool revealTreeNode(ui::NodeId node, gfx::Rect* row = nullptr);
    // Lot 21 : les dossiers des listes de l'IHM dans l'arbre - glisser, deposer
    // (FoldersWorkspace.cpp), en plus de l'ordre d'execution.
    void wireHmiTreeDrag();
    struct TrailRun;
    std::shared_ptr<TrailRun>          trailRun_;
    std::string                        trailSymbolName_;   // le nom que "Creer un symbole" propose pendant un parcours
    // Lot API 7 : la cle du parcours ouvert ("" : aucun) - ses cartes le disent.
    [[nodiscard]] std::string runningTrail() const;
    // ---- lot API 7 : le didacticiel de l'API (ApiTrails.cpp) -----------------
    //  La visite << Decouvrir l'API >> et cinq parcours interactifs, sur le
    //  moteur du lot 21 (startTrail) ; leurs cartes dans l'onglet
    //  << API . Didacticiel >> (la cle "didacticiel" des onglets de l'API).
    //  openApiTutorial ouvre (ou montre) l'onglet ; `trail` non vide lance aussi
    //  ce parcours, ou le reprend s'il est en cours : "api-decouvrir",
    //  "api-variable", "api-ihm-table", "api-bloc", "api-ordre", "api-macro".
    void openApiTutorial(const std::string& trail = {});
    // La demande de l'accueil (requestApiTutorial), des qu'un projet est la :
    // a appeler dans onEnter (apres bindProject) et dans Update - rien a faire
    // sans demande.
    void takePendingApiTutorial();
    struct ApiTrails;
    // Le lot 4 (HmiSupervision.cpp) : alarmes, recettes, utilisateurs,
    // historiques, exporter / importer ; la connexion en simulation.
    void askHmiDeleteAlarm(std::uint64_t alarmId);
    void askHmiRecipeCsv(std::uint64_t recipeId, bool import);
    void askHmiCompareRecords(std::uint64_t recipeId);
    void askHmiDeleteRecipe(std::uint64_t recipeId);
    void askHmiPassword(std::uint64_t userId);
    void askHmiStationPassword();                    // lot 14 : le mot de passe de sortie du poste
    void askHmiSmtpPassword();                       // lot 14 : le mot de passe du relais SMTP
    void askHmiDeleteUser(std::uint64_t userId);
    void askHmiDeleteGroup(std::uint64_t groupId);
    void askHmiHistoryExport(int tab);
    void askHmiClearHistory();
    void askHmiArchive(bool import);
    void askHmiLogin(std::string login);
    // Le lot 6 (HmiRuntimeDialogs.cpp) : ce que l'IHM en marche demande a
    // l'ecran - un jeu de recette a ajouter, modifier, supprimer ou relire
    // (gestionnaire de recettes), une ressource a ajouter (filtre d'extensions).
    void askHmiRecipeRecord(const hmi::RecipeRequest& request);
    // Lot 11 : ecrire un fichier exporte par l'IHM en marche (exports/ du projet).
    bool writeHmiExport(const hmi::ExportRequest& request, std::string* where);
    void askHmiRuntimeResource(const hmi::ResourceRequest& request);
    void askHmiUserRecord(const hmi::UserRequest& request);   // lot 8 : la gestion des utilisateurs en marche
    bool openHmiSupervisionIssue(const hmi::Issue& issue);
    [[nodiscard]] ui::Widget* hmiTab(const std::string& key) const;
    std::map<std::string, ui::Widget*> hmiTabs_;
    std::shared_ptr<hmi::Document>     boundHmi_;
    core::ConnectionScope              hmiLinks_;

    // ---- l'aide ------------------------------------------------------------
    // Ce que F1 doit ouvrir, selon d'ou on appuie. Trois sources, du plus
    // precis au plus vague : le nom sous le curseur, la ligne de diagnostic
    // selectionnee, le noeud de l'explorateur.
    [[nodiscard]] help::Target helpTargetNow() const;
    [[nodiscard]] help::Target helpTargetForNode(ui::NodeId) const;
    [[nodiscard]] help::Target helpTargetForDiagnostic() const;
    void                       openHelpFor(const help::Target&);
    [[nodiscard]] const std::vector<project::CatalogEntry>& helpLibrary() const;
    // Maps a tree node onto the entity the edit commands understand. Returns
    // false for the nodes that are folders or hardware and have no entity
    // behind them.
    [[nodiscard]] bool entityForNode(ui::NodeId, domain::EntityKind& kind,
                                     domain::Index& index) const;
    void confirmAndDelete(domain::EntityKind, domain::Index);

    struct OpenDocument { domain::Index section; std::size_t tab; };
    struct PanelToggle {
        ui::Checkbox* box;
        const char*   key;
        std::size_t   owner;      // index into splitterFor(): the splitters are
        std::size_t   pane;       // built after the toggle list
    };
    [[nodiscard]] ui::Splitter* splitterFor(std::size_t which) const;

    // Set while restoring the workspace, so the widget callbacks that normally
    // persist a change do not fight the load that is causing them.
    struct RestoreGuard {
        bool& flag;
        explicit RestoreGuard(bool& f) : flag(f) { flag = true; }
        ~RestoreGuard() { flag = false; }
    };
    bool restoring_{false};
    bool entered_{false};       // onEnter sans onExit : voir onEnter()

    // ---- lot 7 : un .XPG dans le projet ouvert, des fichiers deposes (ImportWorkspace.cpp) ---
    //  UN NOUVEAU MAST : le fichier (un champ et son bouton ..., prerempli), le
    //  recapitulatif (ImportMastDialog : garde, change, supprime, nouveau, liens
    //  IHM perdus), la version "Avant import du MAST", puis UNE commande
    //  (project::mast::ImportMastCommand : Ctrl+Z rend le projet d'avant).
    //  `hardwarePath` : le .XHW venu avec (deposes ensemble) - dans la meme commande.
    //  DEPOSER : App rassemble les fichiers d'un depot (un .XPG, un .XHW, les deux)
    //  et les donne ici ; une question dit quoi en faire (le MAST, la
    //  configuration, les deux, un projet a part).
public:
    void askImportMast();                                  // l'action file.importMast
    void importMastIntoProject(const std::string& path, const std::string& hardwarePath = {});
    void importHardwareFromPath(const std::string& path);  // le .XHW d'un depot (Importer le .XHW, sans le redemander)
    void filesDropped(std::vector<std::string> paths);
    // Un .XPG ou un .XHW, lu par son contenu : ce qu'un depot peut importer ici.
    [[nodiscard]] static bool importableFile(const std::string& path);
private:
    void applyMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source, bool keep);
    void runMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source, bool keep,
                       const std::string& versionSaid);
    void openDroppedAsProject(std::vector<std::string> files);

    // ---- Lot API 8 : glisser n'importe quel fichier (DropFilesWorkspace.cpp) ----
    //  Un depot qui n'est pas fait que de .XPG / .XHW : le dialogue "Que faire
    //  de ces fichiers ?" (DropFilesDialog, le plan de DropFilesPlan) - d'un
    //  classeur, tout ce qu'on peut en faire ; des autres fichiers, Ressources
    //  et / ou Fichiers externes ; le .XPG / .XHW dans sa section (les choix du
    //  lot 7). Faire passe par les chemins qui existent deja : un import, sa
    //  commande (un Ctrl+Z chacun) ; un ajout, comme son volet ; une macro, son
    //  formulaire dans l'onglet Macros, le fichier deja choisi, jusqu'a
    //  l'apercu ; plusieurs macros, l'une apres l'autre (Fermer ou Annuler dans
    //  l'onglet Macros ouvre la suivante).
public:
    void askDroppedFiles(std::vector<std::string> paths);
    // Ce que le dernier Faire a fait, une ligne par chose (le script depot-bilan).
    [[nodiscard]] const std::vector<std::string>& dropReport() const noexcept { return dropReport_; }
    // Les macros cochees qui attendent leur tour ; ouvrir la suivante tout de suite.
    [[nodiscard]] std::size_t droppedMacrosWaiting() const noexcept { return dropMacros_.size(); }
    bool nextDroppedMacro();
private:
    struct DropMacro {
        std::string macro, field, path;
        bool        workbook{true};       // un .xlsx / .xlsm : il devient le classeur des macros
    };
    void runDropPlan(const dropfiles::Plan& plan);
    bool launchDroppedMacro(const DropMacro& m);
    void tickDroppedMacros();                        // Update : la suivante, quand celle-ci est fermee
    std::vector<DropMacro>   dropMacros_;            // celles qui attendent
    std::string              dropMacroRunning_;      // celle que le depot a lancee ("" : aucune)
    std::vector<std::string> dropReport_;
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----

    // ---- lot API 2 : la barre du haut, les onglets de l'API (ApiWorkspace.cpp) ---
    //  La barre : sept groupes nommes (TopBar) ; chaque action garde son
    //  identifiant d'avant. Les quatre onglets fixes (IO Mapping, Task
    //  Configuration, Communication, Layout) sont partis : Configuration et
    //  Panneaux sont des pages GARDEES (leurs widgets restent relies au projet
    //  et aux reglages) qui s'ouvrent et se ferment comme les autres onglets.
    void onBarAction(std::string_view id);
    void refreshTopBar();
    // ---- Lot API 8 : bandeau haut (TopBarWorkspace.cpp) ----
    //  Ce que le bandeau haut montre de plus : les projets recents, les 10
    //  dernieres actions, la mini-courbe du temps de cycle, la cloche (ce qui
    //  merite l'attention), les taches de fond. refreshTopBarLot8 est rappele a
    //  chaque image (limites dedans) ; topBarLot8Action traite les actions
    //  nouvelles du bandeau (faux : pas une des siennes).
    void refreshTopBarLot8();
    bool topBarLot8Action(std::string_view id);
    double        topBarCycleAt_{-100.0}, topBarNoticesAt_{-100.0}, topBarLibraryAt_{-100.0};
    //  L'horloge de l'appli (FrameContext::totalSeconds, posee par Update) : celle d'une session
    //  rejouee avance de 1/30 s par image - la cloche et les taches s'y rafraichissent comme a l'ecran.
    double        frameClock_{0.0};
    unsigned long long topBarTasksRevision_{~0ull};   // bgtasks::revision() vu au dernier rafraichissement
    std::uint64_t topBarRevision_{~0ull};
    std::int64_t  topBarPeriodMs_{20};
    //  La palette Aller a / Faire... : ">texte" ne cherche que les commandes
    //  (compiler, generer, theme <nom>, nouvelle vue, simuler...) ; vide, elle
    //  montre les derniers choix. nullopt : la recherche habituelle.
    [[nodiscard]] std::optional<GoToPanel::Outcome> topBarPalette(const std::string& text, int group);
    void                           topBarRememberGoTo(const GoToPanel::Result& r);
    std::vector<GoToPanel::Result> topBarGoToRecents_;
    // ---- fin Lot API 8 : bandeau haut ----
    void openApiPane(const std::string& key);
    void onApiRequest(const std::string& key);
    [[nodiscard]] ui::Widget* apiTab(const std::string& key) const;
    void refreshApiPanes();
    std::size_t openKeptPage(const std::string& key);
    bool parkTab(std::size_t index);           // une page gardee : mise de cote au lieu d'etre detruite
    [[nodiscard]] bool isApiTab(const ui::Widget* page) const;
    struct KeptPage {
        ui::TabControl::Tab meta;
        ui::WidgetPtr       parked;            // non nul : fermee, mise de cote
        ui::Widget*         page{nullptr};
    };
    std::map<std::string, KeptPage>    keptPages_;
    std::map<std::string, ui::Widget*> apiTabs_;
    // Lots API 3 et 4 (ApiWorkspace.cpp) : les tables d'animation, la
    // configuration, les taches, l'ordre d'execution - des volets a hotes.
    [[nodiscard]] ApiPaneHosts apiHosts();                 // ce que les volets demandent a l'ecran
    void pickAnimationVariables(bool hmi);                 // + Variable API / + Variable IHM
    // `browse` : un champ de chemin (exporter, importer) - le bouton ... a sa droite.
    void askApiText(const std::string& title, const std::string& text, const std::string& initial,
                    std::function<void(const std::string&)> done, ui::PathBrowse browse = {});
    void importHardwareIntoProject();                      // Configuration > Importer le .XHW...
    void wireAnimationTreeDrag();                          // une variable glissee sur une table
    bool dropTreeDragOnTables(const ui::InputEvent& ev);   // ... ou sur l'onglet des tables
    void addTreeVariablesToTable(const std::vector<ui::NodeId>& nodes, std::size_t table);   // ... ou par le clic droit
    [[nodiscard]] std::vector<std::pair<std::string, bool>> treeVariables(const std::vector<ui::NodeId>& nodes) const;
    void tickApiPanes(double now);
    // Lot API 3 : une variable de l'arbre se TIRE vers une table d'animation. Un
    // appui la choisissait et ouvrait aussitot son onglet - qui recouvrait la
    // table. Son ouverture attend le lacher, et n'a pas lieu si c'etait un glisser.
    bool         treePress_{false};
    bool         treeDragged_{false};
    ui::NodeId   deferredTreeNode_{ui::kInvalidNode};
    bool routeApiNode(ui::NodeId node);                    // un clic dans l'arbre : le bon onglet, le bon sous-onglet
    // Lot API 5 : les variables de l'automate que l'IHM lit ; la bibliotheque
    // lue une fois par rafraichissement des volets ; un CSV ecrit dans le dossier.
    [[nodiscard]] std::map<std::string, ApiHmiRead> hmiReadsOfPlc() const;
    [[nodiscard]] std::shared_ptr<const project::SharedLibrary> apiLibrary();
    void addVariablesToTable(std::vector<std::string> names, std::size_t table);
    void saveApiCsv(const std::string& what, const std::string& content);
    std::shared_ptr<project::SharedLibrary> apiLibrary_;
    bool                                    apiLibraryRead_{false};
    // ---- lot API 7 : Simulation et Statistiques, onglets de l'API (ApiWorkspace.cpp) ---
    //  L'ecran de simulation (F9), l'explorateur de variables (Ctrl+1) et le
    //  tableau des statistiques (Ctrl+5) etaient des ecrans pleins ; ce sont des
    //  onglets de l'API ("simulation", "variables", "statistiques"). Leurs
    //  actions publient OpenApiTab ; l'arbre les ouvre d'un clic.
    void openApiTabFromAction(const std::string& key);    // sans projet : le dire
    bool attachSimulation(std::string* why);               // la simulation du projet ouvert, preparee
    // Une section a une ligne (1 : la premiere ; 0 : sans ligne) ; "Bloc.Section"
    // est le corps d'un DFB - les noms des diagnostics du simulateur.
    void goToSectionLine(const std::string& section, std::uint32_t line);
    // La version d'un bloc DFB (ou d'un DDT) en bibliotheque, si elle est plus
    // recente que celle du projet ; vide sinon.
    [[nodiscard]] std::string newerInLibrary(const std::string& type);
    // La pastille de API > Simulation dans l'arbre ("en marche", "en pause",
    // "halte") ; l'arbre ne se redessine que si elle change (ou le modele).
    void refreshSimulationBadge();
    std::string                             simBadgeShown_;
    std::weak_ptr<ProjectTreeModel>         simBadgeModel_;
    TopBar*                            topBar_{nullptr};
    std::string                        barHaltShown_;      // le message d'arret deja montre
    ui::PopupMenu*                     barMenu_{nullptr};

    App&                  app_;
    ui::Splitter*         outer_{nullptr};      // vertical: three rows
    ui::Splitter*         upper_{nullptr};      // explorer / centre / variables
    ui::Splitter*         left_{nullptr};       // explorer over open documents
    ui::Splitter*         middle_{nullptr};     // DFB library / DB library / sections
    ui::Splitter*         bottom_{nullptr};     // diagnostics / summary / status
    ui::Splitter*         centreColumn_{nullptr};  // 1.11.14 : les onglets du centre / le panneau du bas
    HmiBuildOutputPane*   bottomPanel_{nullptr};   // 1.11.14 : Sorties, Console, Diagnostics
    const HmiLiveSource*  liveSource_{nullptr};    // 1.11.21 : le dernier volet lu (compare, jamais suivi)
    std::uint64_t         liveRevision_{0};
    ui::TreeView*         explorer_{nullptr};
    ui::TabArea*          centre_{nullptr};    // lot 7 : des groupes d'onglets, la mosaique
    ui::PropertyGrid*     configuration_{nullptr};
    RackView*             rackView_{nullptr};
    ui::PropertyGrid*     summary_{nullptr};
    ui::PropertyGrid*     projectStatus_{nullptr};
    ui::TableView*        variables_{nullptr};
    ui::InputText*        variableSearch_{nullptr};
    ui::DropDown*         variableScopeFilter_{nullptr};
    ui::TableView*        diagnostics_{nullptr};
    ui::TreeView*         dfbLibrary_{nullptr};
    ui::TableView*        sections_{nullptr};
    ui::StatusBar*        status_{nullptr};
    ui::ListView*         documentList_{nullptr};
    std::shared_ptr<ui::IListModel> documentModel_;
    ui::Checkbox*         altRowsBox_{nullptr};
    ui::PopupMenu*        contextMenu_{nullptr};
    // Chart tabs already open, by instance name, so a second double-click brings
    // the tab forward instead of opening it twice.
    std::map<std::string, int> grafcetTabs_;
    project::LayoutFile        layoutFile_;   // positions, kept beside the project
    ProjectRef                 boundProject_;  // what bindProject last ran for
    std::map<std::string, int>         macroTabs_;
    // Lot API 6 : les versions relues pour la barre du haut (au plus une fois
    // par seconde, et seulement si versions/index.txt a change).
    // 1.11.2 (BLK, decision 203) : la comparaison de la pastille, dans un fil a part ;
    // le fil est attendu a la destruction (jamais detache : rien ne tourne a la sortie).
    struct VersionJob {
        std::atomic<bool> done{false};
        bool              ok{false};
        std::size_t       changes{0};
        std::thread       thread;
        ~VersionJob() { if (thread.joinable()) thread.join(); }
    };
    struct VersionWatch {
        std::string     folder;
        std::string     stamp;
        double          checkedAt{-100.0};
        double          comparedAt{-100.0};
        std::uint64_t   comparedRevision{~0ull};
        std::size_t     changes{0};
        hmi::ver::Store store;
        bool            stale{true};             // 1.11.2 : a recomparer (ouverture, versions/index.txt)
        std::int64_t    comparedSave{-1};        // 1.11.2 : l'enregistrement deja compare (savedAtMs)
        std::size_t     launched{0};             // 1.11.2 : les comparaisons lancees (essais)
        std::unique_ptr<VersionJob>              job;
        std::vector<std::unique_ptr<VersionJob>> retired;   // un autre dossier : attendus sans bloquer
    };
    VersionWatch                       versionWatch_;
    domain::ProjectIcon                logoShown_;
    // Lot macros 1 : ce que l'onglet Macros retient (reponses, profils, recents,
    // favorites) - un fichier a cote des reglages.
    std::unique_ptr<project::macro::MacroMemory> macroMemory_;
    std::map<std::string, int>         variableTabs_;
    std::map<std::string, int>         callTabs_;
    // The tables' models must outlive the tabs that show them. Held as the
    // public interface rather than the concrete class: that one lives in an
    // anonymous namespace in the implementation, and naming it here would drag
    // a whole widget into everyone's compile for the sake of one pointer.
    std::vector<std::shared_ptr<ui::ITableModel>> variableModels_;
    ui::NodeId            contextNode_{ui::kInvalidNode};   // what the menu is about

    std::vector<PanelToggle>  panels_;
    std::vector<OpenDocument> documents_;
    std::size_t               fixedTabCount_{0};   // tabs that cannot be closed
    std::size_t               viewTabIndex_{0};

    std::shared_ptr<VariableTableModel>    variableModel_;
    std::shared_ptr<ProjectTreeModel>      treeModel_;
    std::shared_ptr<LibraryTreeModel>      libraryModel_;
    std::shared_ptr<SectionTableModel>     sectionModel_;
    std::shared_ptr<DiagnosticsTableModel> diagnosticsModel_;

    // Le dernier nom sous le curseur, rapporte par l'editeur. C'est ce que F1
    // ouvre : l'editeur le calcule deja pour la barre des symboles, et deux
    // calculs du meme nom finissent par ne plus designer la meme chose.
    std::string                            lastSymbol_;
    // Le catalogue de libs/, lu a la premiere demande. `mutable` parce que
    // helpLibrary() est const : il ne change rien de ce que l'ecran montre.
    mutable std::vector<project::CatalogEntry> helpLibrary_;

    // Les connexions du glisser-deposer de l'ordre d'execution. LEUR PROPRE
    // PORTEE, videe a chaque projet : les predicats capturent CE projet et CE
    // modele, et ceux du projet precedent deplaceraient des sections dans un
    // arbre qui n'est plus affiche.
    core::ConnectionScope                  execOrderLinks_;
    core::ConnectionScope                  links_;
    // Lot API 2 : les abonnements des onglets ouverts en cours de route (une
    // section, un grafcet, les volets de l'API...). PAS dans links_ : onEnter le
    // vide a chaque dialogue qui se ferme, et une section ouverte avant un
    // dialogue n'enregistrait plus sa frappe (textChanged debranche). Ils vivent
    // autant que l'ecran ; leurs signaux meurent avec leurs onglets.
    core::ConnectionScope                  paneLinks_;

public:
    // ==== 1.8.0 : les icones au choix, l'export lisible, le comparateur, l'import suivi ====
    //  (screens/LisibleWorkspace.cpp)
    // Les cles (core/CodeIcons.hpp) des elements de ces noeuds de l'arbre qui portent
    // du code ; `what` : "la section SFC_ManuB", "3 \xC3\xA9l\xC3\xA9ments" ; `from` : le nom
    // d'ou vient la suggestion. Vide : aucun de ces noeuds n'en porte.
    [[nodiscard]] std::vector<std::string> codeIconKeys(const std::vector<ui::NodeId>& nodes, std::string* what = nullptr,
                                                        std::string* from = nullptr, core::codeicons::Kind* kind = nullptr) const;
    void askCodeIcon(std::vector<std::string> keys, std::string what, std::string from, core::codeicons::Kind kind);
    bool setCodeIcon(const std::vector<std::string>& keys, const std::string& icon, std::string* why = nullptr);
    void refreshCodeIcons();
    // L'export lisible : 0 tout le programme, 1 une unite, 2 des sections.
    void askProgramExport(int scope, std::string unit = {}, std::vector<domain::Index> sections = {});
    // Sans dialogue (scripts, essais) : les fichiers ecrits, ou faux et pourquoi.
    bool exportProgramNow(int scope, const std::string& unit, const std::vector<domain::Index>& sections, const std::string& folder,
                          const std::string& formats, std::vector<std::string>* written = nullptr, std::string* why = nullptr);
    // Le comparateur de sections (un onglet).
    bool openSectionCompare(std::vector<domain::Index> sections, std::string* why = nullptr);
    [[nodiscard]] class SectionComparePane* sectionComparePane() const;
    // L'import suivi : un .XPG (nouveau MAST, un .XHW avec), ou un .XHW seul.
    void startImportJob(bool mast, std::vector<std::string> paths);
    [[nodiscard]] bool importRunning() const noexcept { return static_cast<bool>(importJob_); }
    bool lisibleRequest(const std::string& key);           // onApiRequest : "comparer:", "exporter:", "icone:"
    bool lisibleBarAction(std::string_view id);            // onBarAction : program.export, import.recap, import.cancel

private:
    void pollLisibleJobs();                                 // Update : l'export et l'import de fond
    void finishImportJob();
    void continueMastImport(std::shared_ptr<const domain::Project> imported, const std::string& source,
                            std::optional<project::mast::Plan> keep, std::optional<project::mast::Plan> replace);
    void applyHardwareImport(std::shared_ptr<const domain::Project> imported, const std::string& source);
    void deliverImport(const std::shared_ptr<class ImportJob>& job);   // le recapitulatif (.XPG) ou la pose (.XHW)
    void pollExportJob();
    struct ExportJob;
    std::shared_ptr<ExportJob>             exportJob_;
    std::shared_ptr<class ImportJob>       importJob_;
    std::vector<std::shared_ptr<class ImportJob>> droppedImports_;   // annules, laches une fois finis (sans bloquer)
    std::shared_ptr<class ImportJob>       importReady_;             // fini en arriere-plan : le recapitulatif attend
    int                                    importTask_{0};
    bool                                   importDialogOpen_{false};
    bool                                   importBackground_{false};
    std::uint64_t                          importRevision_{0};       // les plans du fil valent tant que rien n'a change
    const void*                            importDocument_{nullptr}; // le projet de l'import (un autre ouvert : l'import est lache)
    float                                  importShown_{-1.f};       // la jauge du bandeau, deja montree
    std::string                            lastExportFolder_;        // le dossier du dernier export lisible
};

// ------------------------------------------------------ 3. Variable explorer ---
//  Lot API 7 : parti. L'onglet API > Variables (VariablesPane) fait tout ce
//  qu'il faisait - chercher, filtrer, les pas utilisees, exporter - et plus.

// ------------------------------------------------------- 4. Library explorer ---
class LibraryExplorerScreen final : public menu::WidgetMenu {
public:
    explicit LibraryExplorerScreen(App& app);
    [[nodiscard]] std::string title() const override { return "Libraries"; }
protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;
private:
    App&                              app_;
    ui::ToolBar*                      toolbar_{nullptr};
    ui::Splitter*                     split_{nullptr};
    ui::TreeView*                     tree_{nullptr};
    ui::PropertyGrid*                 details_{nullptr};   // parameters of the selected DFB
    ui::MultiLineText*                source_{nullptr};    // its ST body, read-only
    std::shared_ptr<LibraryTreeModel> model_;
    core::ConnectionScope             links_;
};

// --------------------------------------------- 5. Programming units explorer ---
class ProgramUnitsScreen final : public menu::WidgetMenu {
public:
    explicit ProgramUnitsScreen(App& app);
    [[nodiscard]] std::string title() const override { return "Program units"; }
protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;
private:
    void showSection(domain::Index sectionIndex);

    App&                              app_;
    ui::ToolBar*                      toolbar_{nullptr};
    ui::TreeView*                     tree_{nullptr};
    ui::TabControl*                   editors_{nullptr};
    ui::MultiLineText*                source_{nullptr};
    ui::TableView*                    sections_{nullptr};
    ui::StatusBar*                    status_{nullptr};
    std::shared_ptr<ProjectTreeModel> model_;
    std::shared_ptr<SectionTableModel> sectionModel_;
    core::ConnectionScope             links_;
};

// ------------------------------------------------- 6. Statistics dashboard ---
//  Lot API 7 : parti aussi - l'onglet API > Statistiques (StatisticsPane).

// ------------------------------------------------------------- settings -------
// =============================================================================
//  L'aide. Sommaire a gauche, document a droite, recherche au-dessus.
//
//  ELLE S'OUVRE, ET C'EST LE POINT. Le document et son widget existaient sans
//  qu'aucun chemin n'y mene : une aide qu'on ne peut pas atteindre n'aide
//  personne, et c'est le genre de manque qu'on ne voit pas en relisant du code
//  puisque chaque morceau, pris seul, est complet.
// =============================================================================
class HelpScreen final : public menu::WidgetMenu {
public:
    explicit HelpScreen(App& app);
protected:
    core::Status buildUi() override;
    void         onEnter() override;
private:
    void rebuildToc(const std::string& filter);
    void showIndex();

    App&                  app_;
    core::ConnectionScope links_;
    std::shared_ptr<const ui::HelpDocument> doc_;
    ui::HelpView*  view_{nullptr};
    ui::ListView*  toc_{nullptr};
    ui::InputText* search_{nullptr};
    ui::StatusBar* status_{nullptr};

    // Ce que la liste de gauche montre en ce moment : le sommaire, les
    // resultats d'une recherche, ou l'index. Les trois y vivent, parce que les
    // trois repondent a "ou est-ce que je vais".
    struct Entry { std::string label; std::string anchor; int level{1}; };
    std::vector<Entry> entries_;
};

// Lot 8 : l'aide de l'IHM en plein ecran, depuis l'aide generale (bouton IHM) -
// le meme volet que F1 dans l'IHM : les vues, la programmation, et la page de
// chaque objet de la bibliotheque avec son exemple anime.
class HmiHelpScreen final : public menu::WidgetMenu {
public:
    explicit HmiHelpScreen(App& app);
protected:
    core::Status buildUi() override;
private:
    App&                  app_;
    core::ConnectionScope links_;
};

class SettingsScreen : public menu::WidgetMenu {
public:
    explicit SettingsScreen(App& app);
protected:
    core::Status buildUi() override;
    App&                  app_;
    core::ConnectionScope links_;
};
class GraphicsSettingsScreen final : public SettingsScreen {
public:
    explicit GraphicsSettingsScreen(App& app);
protected:
    core::Status buildUi() override;
};
class ThemeSettingsScreen final : public SettingsScreen {
public:
    explicit ThemeSettingsScreen(App& app);
protected:
    core::Status buildUi() override;
private:
    std::shared_ptr<ui::RadioGroup> group_;
};

// ------------------------------------------------------------- dialogs --------
// ---------------------------------------------------------------------------
//  One dialog serves every "ask the user for a few values" case: new project,
//  rename, duplicate, unlock, master key. Five bespoke dialogs would be five
//  places to get the layout, the Escape handling and the Enter key subtly
//  different from each other.
// ---------------------------------------------------------------------------
class FormDialog final : public menu::WidgetMenu {
public:
    struct Field {
        std::string label;
        std::string value;
        std::string placeholder;
        bool        secret{false};        // shown as dots; used for passwords
        std::vector<std::string> choices; // non-empty: a dropdown instead of a field
    };

    // What the rules may change about a field once the dialog is up.
    //
    // These are deliberately NOT members of Field. Field is the declaration -
    // what this form asks for - and it is written out at twenty-odd call sites;
    // widening it made every one of them incomplete and produced a wall of
    // -Wmissing-field-initializers that said nothing useful. Enabled-ness and
    // the reason for it are computed, not declared, so they live where they are
    // computed.
    struct FieldState {
        std::string              value;
        std::vector<std::string> choices;
        std::string              placeholder;
        bool                     enabled{true};
        std::string              hint;      // why it is greyed, shown beside it
    };

    // Fields that depend on each other.
    //
    // A form where every field is independent is the easy case and it is not the
    // one we have: the scope decides which containers are legal, and the type
    // decides whether an array is. Without this the dialog offers choices that
    // the command then refuses, which teaches people that the dialog lies.
    //
    // Called with the current values whenever anything changes; it rewrites the
    // fields - their choices, whether they are enabled, and what they say - and
    // the dialog rebuilds from the result. Values are never invented here: if a
    // rule narrows a dropdown so the current value is gone, it must set a new
    // one explicitly, because silently keeping an impossible value is the bug
    // this exists to prevent.
    using Rules = std::function<void(const std::vector<std::string>& values,
                                     std::vector<FieldState>& state)>;

    FormDialog(std::string id, std::string title, std::string explanation,
               std::vector<Field> fields, std::string confirmLabel = "OK",
               bool cancellable = true);

    // Applied once before the first paint and after every change.
    void setRules(Rules rules);

    // Fields that hold CODE rather than a value.
    //
    // Not a flag on Field, deliberately: Field is written out at twenty-odd call
    // sites and widening it once already produced a wall of
    // -Wmissing-field-initializers that said nothing useful. This is set by the
    // two or three dialogs that need it.
    //
    // A code field gets the ST editor - several lines, syntax colouring, a
    // scrollbar when it runs long, and the same completion the document editor
    // has. A condition or an action body is code; asking for it in a one-line box
    // is asking someone to write a program through a letterbox.
    void setCodeFields(std::vector<std::size_t> indices);
    // The same completion the ST editor uses, supplied by the screen because only
    // it knows the project and the section.
    void setCompletionProvider(ui::MultiLineText::CompletionProvider provider);
    // L'aide a la saisie d'un champ d'une ligne (une variable, une expression) :
    // la liste sous le champ. Par indice, comme setCodeFields.
    void setFieldAssist(std::size_t index, ui::InputText::Assist assist);
    // Lot API 6 : Entree valide AUSSI dans ce champ d'une ligne (par indice).
    // Par defaut seul le dernier champ de saisie valide ; un mot de passe
    // facultatif mis en dernier (celui de "Creer une version", pour Livree
    // seulement) ne doit pas enlever Entree au commentaire qui le precede.
    void setEnterField(std::size_t index) { enterFields_.push_back(index); }
    // UN CHAMP DE CHEMIN (par indice, comme setFieldAssist) : un bouton ... a sa
    // droite ouvre l'explorateur de fichiers - un fichier a ouvrir, ou
    // enregistrer, un dossier (ui::openFile, ui::saveFile, ui::chooseFolder,
    // ui::newFolder). Le chemin choisi remplit le champ ; taper un chemin puis
    // Entree marche toujours. Scripts : explorateur "chemin", puis
    // parcourir "Libelle du champ".
    void setFieldBrowse(std::size_t index, ui::PathBrowse browse);
    // La meme chose en une expression : ShowDialog(FormDialog::withBrowse(
    // std::make_unique<FormDialog>(...), 0, ui::openFile("*.csv", dossier)), ...).
    [[nodiscard]] static std::unique_ptr<FormDialog> withBrowse(std::unique_ptr<FormDialog> dialog, std::size_t index,
                                                                ui::PathBrowse browse);

    // Lot 21 : l'ecran continue de vivre dessous (un parcours du didacticiel suit
    // le dialogue : sa bulle se met de cote). Par defaut, il s'arrete.
    void setUpdatesBelow(bool on) { updatesBelow_ = on; }

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return title_; }

    // Values are returned joined by '\n', in field order, through
    // DialogResult::payload - the same channel every other dialog uses.
    static std::vector<std::string> split(const std::string& payload);

protected:
    core::Status buildUi() override;

private:
    std::string              title_, explanation_, confirmLabel_;
    std::vector<Field>       fields_;
    std::vector<FieldState>  state_;
    std::vector<ui::Widget*> inputs_;
    bool                     cancellable_{true};
    bool                     updatesBelow_{false};
    Rules                    rules_;
    std::vector<std::size_t> codeFields_;
    ui::MultiLineText::CompletionProvider completion_;
    std::vector<std::pair<std::size_t, ui::InputText::Assist>> assists_;
    std::vector<std::size_t> enterFields_;
    std::vector<std::pair<std::size_t, ui::PathBrowse>> browses_;          // les champs de chemin
    std::vector<std::pair<std::size_t, ui::Widget*>>    browseButtons_;    // leur bouton ... (suit l'etat du champ)
    bool                     applying_{false};   // rules must not re-enter
    core::ConnectionScope    links_;

    [[nodiscard]] std::vector<std::string> currentValues() const;
    void applyRules();
};

// ---------------------------------------------------------------------------
//  Running the program instead of reading it : lot API 7, l'ecran plein de la
//  simulation (F9) est devenu l'onglet API > Simulation (SimulationPane.hpp) -
//  la barre du haut, l'arbre et les autres onglets restent la pendant que le
//  programme tourne. La simulation elle-meme vit dans SimulationHost, a App.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  1.8.0 : LES DOSSIERS DE L'APPLICATION (app/Dossiers.hpp). L'accueil (le lien
//  Dossiers) et Projet > Dossiers de l'application... : les donnees, les
//  projets, la bibliotheque, les captures - leur chemin, d'ou il vient, Ouvrir
//  (l'Explorateur), Dossier... ; puis les reglages, les journaux, le programme
//  (Ouvrir seulement) ; Ouvrir XPGAnalyser.ini. Enregistrer ecrit XPGAnalyser.ini
//  (app::dossiers::changer) ; le bilan revient dans DialogResult::payload.
//  La version de developpement (pas d'installation.ini) montre et ouvre, sans
//  rien changer.
// ---------------------------------------------------------------------------
class FoldersDialog final : public menu::WidgetMenu {
public:
    explicit FoldersDialog(App& app);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    // Pour les scripts et les tests : le champ d'un dossier (0 donnees ... 3 captures).
    [[nodiscard]] ui::InputText* field(std::size_t index) const noexcept { return index < fields_.size() ? fields_[index] : nullptr; }
    void setNote(std::string text, bool warning);
protected:
    core::Status buildUi() override;
private:
    void openPath(const std::string& utf8, bool create);
    void save();
    App&                           app_;
    std::array<ui::InputText*, 4>  fields_{};
    std::array<std::string, 4>     initial_{};
    ui::Checkbox*                  copy_{nullptr};
    ui::Widget*                    note_{nullptr};
    core::ConnectionScope          links_;
};
// Montre le dialogue ; a la fermeture, le bilan d'un changement dans un message.
void showFoldersDialog(App& app);

class MessageDialog final : public menu::WidgetMenu {
public:
    enum class Icon : std::uint8_t { Info, Warning, Error, Question };

    // confirmLabel turns this into a QUESTION with two answers.
    //
    // It had one button, "Close", and that button returned Ok. So every
    // confirmation in the program was accepted whatever the reader pressed -
    // including "delete this step, renumbering the rest". A dialog that asks a
    // question and has no way to answer no is not a question.
    MessageDialog(std::string title, std::string message, Icon icon,
                  std::string confirmLabel = {});
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return title_; }
    // Lot 15 : le libelle du "non" (Annuler par defaut) ; et une reponse
    // d'office au bout de N secondes - le bouton principal compte a rebours.
    void setCancelLabel(std::string label) { cancelLabel_ = std::move(label); }
    // Le compte a rebours suit l'horloge du PC, pas les images : un poste qui
    // rame (ou une image lente) ne le ralentit pas.
    void setCountdown(double seconds) { countdown_ = seconds; deadline_ = -1; }
    void Update(const menu::FrameContext& f) override;
protected:
    core::Status buildUi() override;
private:
    std::string           title_, message_, confirmLabel_;
    std::string           cancelLabel_{"Annuler"};
    double                countdown_{0};
    double                deadline_{-1};        // l'heure (steady_clock, s) de la reponse d'office
    bool                  answered_{false};
    ui::Button*           okButton_{nullptr};
    Icon                  icon_;
    core::ConnectionScope links_;
};

class OpenProjectDialog final : public menu::WidgetMenu {
public:
    // hardwareOnly changes the wording and the default, nothing else: a .XHW
    // completes the loaded project instead of replacing it.
    explicit OpenProjectDialog(App& app, bool hardwareOnly = false);
    [[nodiscard]] menu::MenuTraits traits() const override;
protected:
    core::Status buildUi() override;
private:
    App&                  app_;
    ui::InputText*        path_{nullptr};
    bool                  hardwareOnly_{false};
    core::ConnectionScope links_;
};

// Progress modal shown while a background import runs; it subscribes to
// ImportProgress and closes itself on ImportFinished / ImportFailed.
class ImportProgressDialog final : public menu::WidgetMenu {
public:
    explicit ImportProgressDialog(App& app);
    [[nodiscard]] menu::MenuTraits traits() const override;
protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;
private:
    App&                  app_;
    ui::Widget*           bar_{nullptr};
    core::ConnectionScope links_;
};

} // namespace app
