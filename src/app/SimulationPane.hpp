// =============================================================================
//  app/SimulationPane.hpp - API > Simulation (lot API 7)
// -----------------------------------------------------------------------------
//  L'ancien ecran plein de la table de forcage (F9) devient un ONGLET DE
//  L'API : la barre du haut, l'arbre et les autres onglets restent la pendant
//  que le programme tourne.
//
//   * L'ETAT EN GRAND : en marche, en pause, arretee, ou HALTE - en rouge, avec
//     ce qui l'a arretee, la section et la ligne (« Aller a la ligne »), et, si
//     le bloc en cause a une version plus recente en bibliotheque, le dire. Le
//     cycle, le temps simule, le temps de calcul d'un cycle, la vitesse (x1,
//     x10, au plus vite).
//   * LES VARIABLES DU PROJET, rangees comme dans l'onglet Variables (instances
//     de DFB, blocs standard, types derives, situees, les autres ; puis les
//     locales de chaque unite), qui se deplient a toute profondeur
//     (project/MemberTree, le moteur des tables d'animation) - au lieu de 2 000
//     cases %MW sur 63 983. L'ecran ne fabrique que les lignes depliees et ne
//     lit que leurs valeurs. Colonnes : valeur, type, forcage, « ecrite par »
//     (l'entree de la tache qui l'ecrit).
//   * LES FORCAGES au meme endroit ; la COURBE des variables suivies (six au
//     plus) ; CE QUE DIT LE SIMULATEUR - dont les fonctions qu'il ne connait
//     pas : s'arreter dessus, ou continuer en leur faisant rendre 0 (le defaut).
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/MemberTree.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/PathBrowse.hpp"   // exporter / importer les forcages : le bouton ...

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; class PopupMenu; class SearchQuery; }
namespace sim { class Runtime; class Value; struct Diagnostic; }

namespace app {

class ApiFrame;
class ApiFilterBar;
class SimulationHost;
namespace macroui { class Segmented; }

class SimulationPane final : public ui::Widget {
public:
    enum Action : int { ARun = 1, AStep, AStop, AForce, ARelease, AReleaseAll, AWatch, AAddToTable, AExport, AImport, AHelp };

    struct Hosts {
        std::function<std::shared_ptr<const domain::Project>()> project;
        std::function<SimulationHost*()>                         host;
        // Lance la simulation sur le projet ouvert (la prepare au besoin) ; faux :
        // `why` dit pourquoi.
        std::function<bool(std::string* why)>                     attach;
        // Ouvrir une section a une ligne ; `section` peut etre "BLOC.Section"
        // (le corps d'un DFB).
        std::function<void(const std::string& section, std::uint32_t line)> goToLine;
        std::function<void(std::vector<std::string> names)>       addToTable;
        std::function<void(const std::string& key)>               request;   // "bibliotheque", "variables:<nom>"
        std::function<void(const std::string& text)>              status;
        std::function<void(const std::string& title, const std::string& text, const std::string& initial,
                           std::function<void(const std::string&)> done)> ask;
        // UN CHEMIN (exporter, importer les forcages) : la meme question, le
        // champ avec le bouton ... de l'explorateur de fichiers. Absent : `ask`.
        std::function<void(const std::string& title, const std::string& text, const std::string& initial,
                           ui::PathBrowse browse, std::function<void(const std::string&)> done)> askPath;
        std::function<std::string()>                              folder;    // le dossier du projet
        // La version d'un bloc en bibliotheque, si elle est plus recente que
        // celle du projet ("0.24") ; vide sinon.
        std::function<std::string(const std::string& type)>       newerInLibrary;
    };

    explicit SimulationPane(std::string id);
    ~SimulationPane() override;
    void setHosts(Hosts h);
    void attach(ApiFrame& frame);
    // Le projet a change : les racines et « ecrite par » se refont (une lecture
    // du code de MAST ; rien d'autre ne relit le projet).
    void refresh();
    // A chaque image, l'onglet a l'ecran ou non (`now` : des secondes). Cache,
    // il ne fait que noter l'heure. A l'ecran (ici ou dans une fenetre
    // detachee) : l'etat a chaque image ; les valeurs des lignes, toutes les
    // cases (« Qui changent ») et la courbe quatre fois par seconde au plus, et
    // seulement quand un cycle a tourne ; les forcages et le simulateur une fois
    // par seconde.
    void tick(double now);
    void runAction(int action);

    // ---- pour les scripts et les tests ------------------------------------
    // Un chemin de la simulation, sans casse ni blancs : "Armoires[0].ana.PT1.mes",
    // "Grille[2, 5]", "Unite.compteur" (une locale). Cachee, elle est montree :
    // toutes les variables, son dossier et ses parents ouverts.
    bool selectVariable(std::string_view path);
    // Un dossier par son libelle (« Instances de DFB » ; celui des locales d'une
    // unite par le nom de l'unite), ou une variable, un membre, un paquet (sa
    // cle, son libelle « [100 ... 199] », son chemin) - a toute profondeur.
    bool setExpanded(std::string_view path, bool open);
    [[nodiscard]] bool isExpanded(std::string_view path) const;
    // Force chaque variable a valeur choisie, la valeur lue dans son type.
    bool forceSelected(const std::string& text);
    bool chooseChip(std::string_view label);
    void setSearch(const std::string& text);
    void setSpeed(int factor);                    // 1, 10, 0 (au plus vite)
    void setUnknownPolicy(bool continueWithZero);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string valueOf(std::string_view path) const;
    [[nodiscard]] std::string stateLine() const;  // "EN MARCHE . cycle 1284" ; "HALTE : ..."
    // Lot API 8 (le moteur) : la couleur du bandeau, en mots (le script
    // automate-bandeau) : "vert" en marche, "bleu" en pause, "orange" sur un
    // point d'arret (et pas prete), "rouge" en halte, "gris" arretee.
    [[nodiscard]] std::string bandColor() const;
    [[nodiscard]] std::vector<std::string> diagnosticLines() const;
    [[nodiscard]] std::vector<std::string> watched() const { return watched_; }

    // Un message du moteur, dit en francais ("'X' is not declared" ->
    // "X n'est pas declaree") ; et son endroit ("Gestion_reports, ligne 42").
    [[nodiscard]] static std::string frenchMessage(const std::string& message);
    [[nodiscard]] static std::string frenchPlace(const std::string& section, std::uint32_t line);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class Model;
    class ForcedModel;
    class DiagModel;
    class Band;
    class Trend;
    friend class Model;
    friend class ForcedModel;
    friend class DiagModel;
    friend class Band;
    friend class Trend;

    struct Root {                     // une variable du projet, une racine
        std::string name;             // le chemin de la simulation (Unite.nom pour une locale)
        std::string label;            // ce que la ligne montre
        std::string type, address, writer;
        int         folder{0};
        bool        agg{false};       // une structure, un tableau, une instance : pas de valeur a elle
        std::string section;          // ou `writer` l'ecrit (la section, la ligne)
        std::uint32_t line{0};
        std::string comment;          // lot recherche : son commentaire (la recherche le lit)
    };
    struct Row {
        bool        folder{false};
        int         folderId{0};
        std::size_t count{0};         // un dossier : ses variables
        int         depth{0};
        bool        agg{false};       // se deplie
        project::members::Node node;  // une variable, un membre, un paquet
        std::string label, writer, address;
        int         root{-1};         // la racine (roots_) ; -1 : inconnue
        std::string section;          // ou `writer` l'ecrit
        std::uint32_t line{0};
        bool        array{false};     // un tableau, ou un paquet de ses elements (son icone)
        bool        open{false};      // depliee (ou le dossier ouvert) : la fleche, sans chercher a chaque image
    };
    struct Live {
        std::string text;
        double      changedAt{-10.0};
        bool        isBool{false}, truthy{false}, ok{false}, forced{false};
        double      readAt{-100.0};   // la derniere lecture (un eclairage n'a de sens que si on regardait)
    };
    // Ou le programme ecrit : le chemin tel que le code l'ecrit (en minuscules ;
    // un indice peut etre une expression, armoires[i]), l'entree de MAST qui
    // l'ecrit, sa section et sa ligne.
    struct WriteSite {
        std::string   pattern, label, section;
        std::uint32_t line{0};
    };
    // Une ligne de « Le simulateur » (deux lignes de la table : le titre, le detail).
    struct DiagItem {
        ui::Tone      tone{ui::Tone::Warning};
        std::string   title, detail, section;
        std::uint32_t line{0};
        bool          link{false};
    };
    // Un diagnostic d'un cycle, garde : le moteur ne dit un avertissement qu'une
    // fois (une adresse hors memoire, une instance sans corps).
    struct SeenDiag {
        int           severity{0};
        std::string   message, section;
        std::uint32_t line{0};
    };
    // Ce que dit le bandeau (et stateLine()).
    struct BandInfo {
        int           kind{0};        // 0 arretee, 1 en marche, 2 en pause, 3 halte, 4 pas prete
        std::string   word, reason, place, section, block, newer;
        std::uint32_t line{0};
        bool          unknownFunction{false}, attached{false};
        std::uint64_t cycle{0};
        std::int64_t  clockMs{0}, intervalMs{20}, scanMicros{0};
        // Lot API 8 (le moteur) : en pause SUR UN POINT D'ARRET (kind 2) - le
        // bandeau passe a l'orange et dit ou (section, line, place).
        bool          breakpoint{false};
    };

    [[nodiscard]] SimulationHost* host() const;
    [[nodiscard]] sim::Runtime*   runtime() const;
    [[nodiscard]] std::shared_ptr<const domain::Project> project() const;
    [[nodiscard]] std::string     keyOf(const Row& r) const;
    void rebuildRoots();
    void rebuildRows();
    void addChildren(const domain::Project& p, const Row& parent, int depth);
    void readValues();                                    // les lignes a l'ecran (tick decide du rythme)
    void refreshSide();
    void updateTools();
    [[nodiscard]] std::vector<std::size_t> selectedRows() const;
    void askForce(std::vector<std::string> paths);        // une valeur demandee, pour une ou plusieurs
    void exportForcing();
    void importForcing();
    [[nodiscard]] std::string haltReason(std::string* section, std::uint32_t* line, std::string* block) const;

    // ---- lot API 7 : le reste de la mecanique ----------------------------------
    [[nodiscard]] bool     shown() const;                  // l'onglet est-il a l'ecran
    [[nodiscard]] BandInfo bandInfo() const;
    void checkRuntime();                                  // un autre runtime : tout se relit
    bool ensureAttached();
    void transport(int action);
    void addRootRow(const domain::Project& p, std::size_t root, int depth);
    void addPathRow(const domain::Project& p, const std::string& path, int depth);
    bool locate(const domain::Project& p, std::string_view path, project::members::Node& out,
                std::vector<std::string>* chain, int* root) const;
    void buildWriters(const domain::Project& p);
    void fillWriter(Row& r) const;
    [[nodiscard]] bool rootPasses(std::size_t root, std::size_t chip) const;
    // Lot recherche : la racine passe-t-elle la pastille, la recherche (son nom, son
    // COMMENTAIRE, son type, son adresse, qui l'ecrit) et les filtres des colonnes
    // (sauf `skipColumn`) ? Et son texte dans une colonne, pour ces filtres.
    [[nodiscard]] bool rootKept(std::size_t root, std::size_t chip, const ui::SearchQuery& query, int skipColumn) const;
    [[nodiscard]] std::string rootCell(std::size_t root, std::size_t column) const;
    void updateChips();
    void pollForcing();
    void rebuildDiag();
    void releasePaths(const std::vector<std::string>& paths);
    void toggleWatch(const std::vector<std::string>& paths);
    void openMenu(gfx::Point at);
    void toggleRow(std::size_t row);
    void onStateChanged(int previous, int next);
    void status(const std::string& text) const;
    [[nodiscard]] std::string tablePath(const Row& r) const;   // le nom qu'une table d'animation connait
    [[nodiscard]] std::string liveText(std::string_view path) const;
    std::size_t forcePaths(const std::vector<std::string>& paths, const std::string& text);   // rend combien
    void noteDiagnostics(const std::vector<sim::Diagnostic>& list);
    void activateRow(std::size_t row);
    void menuChosen(int id);
    void goToItem(std::size_t item);
    void bandLink(const std::string& key);
    // « Qui changent » : toutes les cases relues sous leur racine (sweep), par
    // l'adresse de leur valeur (Runtime::forEachSlot, indexee par buildProbes).
    void buildProbes();
    void sweepStep(std::uint64_t scan);                   // un morceau du tour des cases, a chaque image
    void layoutMain(const gfx::Rect& left);               // la table et ses filtres
    void layoutSide(float sideW, float top);               // a droite : forcages, courbe, simulateur
    void layoutStacked();                                  // etroit : les memes, sous la table
    [[nodiscard]] std::string biggestRoots(std::size_t total) const;   // ", dont 32 000 dans armoires"
    [[nodiscard]] std::string simulationFolder() const;                // <projet>/simulation
    // Le dossier `folder` a-t-il ce libelle (en minuscules) ? Celui des locales
    // d'une unite se nomme aussi par l'unite seule (« logigrammes_a »).
    [[nodiscard]] bool folderIs(std::size_t folder, const std::string& label) const;

    Hosts                                   hosts_;
    ApiFrame*                               frame_{nullptr};
    Band*                                   band_{nullptr};
    ApiFilterBar*                           filter_{nullptr};
    ui::TableView*                          table_{nullptr};
    std::shared_ptr<Model>                  model_;
    ui::TableView*                          forced_{nullptr};
    std::shared_ptr<ForcedModel>            forcedModel_;
    Trend*                                  trend_{nullptr};
    ui::TableView*                          diag_{nullptr};
    std::shared_ptr<DiagModel>              diagModel_;
    macroui::Segmented*                     policy_{nullptr};
    gfx::Rect                               forcedTitle_{}, trendTitle_{}, diagTitle_{}, side_{};

    std::vector<std::string>                folders_;      // les libelles des dossiers
    std::vector<Root>                       roots_;
    std::vector<Row>                        rows_;
    std::vector<Live>                       live_;
    std::map<std::string, Live>             lastSeen_;     // la derniere valeur de chaque chemin
    std::set<std::string>                   expanded_;     // cles en minuscules
    std::set<int>                           openFolders_;
    std::vector<std::string>                watched_;
    std::vector<std::string>                forcedNames_;
    std::vector<std::string>                diagLines_;
    double                                  now_{0.0}, nextRead_{0.0};
    std::uint64_t                           lastScan_{~std::uint64_t{0}};
    bool                                    syncing_{false};

    // ---- lot API 7 : le reste de l'etat ------------------------------------------
    ui::Widget*                             forcedBox_{nullptr};   // cache les titres de la liste (le titre est peint)
    ui::Widget*                             diagBox_{nullptr};
    ui::Widget*                             catcher_{nullptr};     // le clic droit sur la table
    ui::PopupMenu*                          menu_{nullptr};
    mutable gfx::Rect                       releaseAllLink_{}, freezeLink_{};
    std::map<std::string, std::vector<WriteSite>> writers_;       // par racine en minuscules ("unite.nom" : une locale)
    std::vector<Live>                       rootLive_;             // les racines, pour « Qui changent »
    std::vector<std::size_t>                changingShown_;
    std::vector<std::string>                forcedValues_;
    std::vector<DiagItem>                   diagItems_;
    std::vector<SeenDiag>                   seenDiags_;
    std::vector<std::pair<std::string, std::size_t>> chipsShown_;
    mutable std::string                     newerBlock_, newerVersion_;   // la bibliotheque, demandee une fois par halte
    std::string                             attachError_;
    std::string                             runLabel_;
    std::size_t                             releaseAllShown_{static_cast<std::size_t>(-1)};
    std::size_t                             entries_{0}, taskSections_{0}, units_{0};
    SimulationHost*                         hostSeen_{nullptr};
    sim::Runtime*                           runtimeSeen_{nullptr};
    std::uint64_t                           generationSeen_{~std::uint64_t{0}};   // SimulationHost::generation
    int                                     stateSeen_{-1};
    double                                  nextPoll_{0.0}, nextPaint_{0.0};
    float                                   headerH_{28.f}, rowH_{24.f};
    float                                   sideWidth_{0.f}, mainWidth_{0.f};
    bool                                    quietRead_{true};
    bool                                    quietSweep_{true};     // le prochain tour des cases ne compte rien (un Arret, un autre runtime)
    bool                                    valuesDirty_{true};
    bool                                    rebuilding_{false};
    bool                                    searching_{false};     // un texte cherche : les dossiers sont ouverts
    gfx::Size                               surface_{};            // la fenetre qui peint ce volet (le menu y reste)
    // Une case sous surveillance : l'adresse de sa valeur, son empreinte, sa racine.
    struct Probe {
        const sim::Value* value{nullptr};
        std::uint64_t     print{0};
        std::uint32_t     root{0};
    };
    std::vector<Probe>                      probes_;
    std::vector<std::size_t>                rootSlots_;            // les cases de chaque racine
    const sim::Runtime*                     probedRuntime_{nullptr};
    std::uint64_t                           probedGeneration_{~std::uint64_t{0}}; // le runtime des cases
    std::size_t                             probedSlots_{0};
    double                                  probesBuiltAt_{-100.0}, lastSweep_{-100.0}, nextChanging_{0.0};
    double                                  flashUntil_{-100.0};   // un eclairage s'eteint encore jusque-la
    std::size_t                             sweepAt_{0};           // ou en est le tour des cases
    std::size_t                             quietLeft_{0};         // des cases encore a relire sans compter
    std::uint64_t                           passScan_{0};          // le cycle du dernier tour commence
    double                                  lastChunk_{-100.0};    // le dernier morceau lu
    bool                                    passOpen_{false};      // un tour en cours
    bool                                    stacked_{false};       // etroit : la colonne de droite passe dessous
    core::ConnectionScope                   hostLinks_;
    core::ConnectionScope                   links_;
};

} // namespace app
