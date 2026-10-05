// =============================================================================
//  app/TaskPanes.hpp - lot API 4 : les taches, l'ordre d'execution
// -----------------------------------------------------------------------------
//  Deux onglets de l'API, faits comme les autres (ApiFrame : la barre, la ligne
//  d'aide) :
//
//   * TACHES - les taches du projet (MAST ; FAST et AUX0 en gris, a creer),
//     cyclique ou periodique, la periode, le chien de garde : modifiables dans
//     le tableau ou dans les proprietes, chaque changement est une commande.
//     Dessous, ce que la tache executee, dans l'ordre, en poids de lignes.
//   * ORDRE D'EXECUTION - les entrees de MAST (une section, ou une unite de
//     programme qui tourne en bloc) : glisser une ligne, Monter, Descendre,
//     Deplacer vers un rang ; « Verifier l'ordre » marque les lectures faites
//     avant l'ecriture (une variable lue par une entree, ecrite plus loin) et
//     « Placer apres » range l'entree juste apres celle qui ecrit.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../domain/Reindex.hpp"
#include "../project/ApiChecks.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; class PropertyGrid; }

namespace app {

class ApiFrame;
class ApiFilterBar;      // lot API 8 : la recherche (ApiListKit.hpp)

// Lot API 5 : une variable de l'automate que l'IHM lit - par quelle variable
// IHM (liee a la meme adresse) ou directement, et a combien d'endroits.
struct ApiHmiRead {
    std::string via;        // la variable IHM liee ; "" : citee telle quelle
    int         uses{0};    // vues, scripts, alarmes... (un endroit = un emploi)
};

// Ce que les onglets de l'API demandent a l'ecran.
struct ApiPaneHosts {
    std::function<std::shared_ptr<const domain::Project>()> view;      // le projet a lire
    std::function<std::shared_ptr<domain::Project>()>       project;   // modifiable ; nul : export lu tel quel
    std::function<void(core::CommandPtr)>                   apply;
    std::function<void(const std::string&)>                 request;   // "ordre", "simulation", "macro:RangerSections", "section:<nom>"...
    std::function<void(const std::string&)>                 status;
    // Une question a un champ (un rang, une reference) : done(texte) si OK.
    std::function<void(const std::string& title, const std::string& text, const std::string& initial,
                       std::function<void(const std::string&)> done)> ask;
    // ---- lot API 5 ------------------------------------------------------------
    // Supprimer, apres avoir dit ce qui part avec (une commande : Ctrl+Z).
    std::function<void(domain::EntityKind, domain::Index)>  remove;
    // Les variables de l'automate que l'IHM lit, par nom en minuscules.
    std::function<std::map<std::string, ApiHmiRead>()>      hmiReads;
    // Des variables dans une table d'animation (npos : une nouvelle table).
    std::function<void(std::vector<std::string>, std::size_t table)> addToTable;
    // La bibliotheque partagee, lue une fois par rafraichissement (nul : absente).
    std::function<std::shared_ptr<const project::SharedLibrary>()>   library;
};

// ================================================================ Taches ====
class TasksPane final : public ui::Widget {
public:
    enum Action : int { AAddFast = 1, AAddAux, ARemove, AOrder, ASimulation };
    explicit TasksPane(std::string id);
    ~TasksPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectTask(std::string_view name);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] std::string currentTaskName() const;
    // Lot API 8 : chercher une tache et ce qu'elle execute (voir apikit::searchTwoLevels) :
    // les taches qui gardent quelque chose, et les entrees montrees de la tache choisie.
    [[nodiscard]] ApiFilterBar& filters() noexcept { return *filters_; }
    [[nodiscard]] std::size_t shownEntries() const noexcept { return shownEntries_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    class Weights;
    friend class Model;
    struct Row {
        std::string name;
        std::size_t task{static_cast<std::size_t>(-1)};   // npos : une tache a creer (en gris)
        std::size_t sections{0}, units{0}, lines{0};
        std::vector<project::api::Entry> entries;          // lot API 8 : ce qu'elle execute (la recherche)
    };
    void refreshProperties();
    void setTask(std::size_t task, std::string type, std::uint32_t period, std::uint32_t watchdog);
    // Lot API 8 : la recherche, sur les taches et la liste de ce qu'execute la choisie.
    void applySearch();
    [[nodiscard]] std::vector<std::string> taskTexts(std::size_t row) const;

    ApiPaneHosts                  hosts_;
    ApiFrame*                     frame_{nullptr};
    ui::TableView*                table_{nullptr};
    ui::PropertyGrid*             props_{nullptr};
    Weights*                      weights_{nullptr};
    ApiFilterBar*                 filters_{nullptr};    // lot API 8
    std::size_t                   shownEntries_{0};     // lot API 8 : les entrees montrees de la tache choisie
    std::shared_ptr<Model>        model_;
    std::vector<Row>              rows_;
    std::size_t                   current_{0};         // la ligne choisie
    bool                          syncing_{false};
    core::ConnectionScope         links_;
};

// ===================================================== Ordre d'execution ====
class ExecutionOrderPane final : public ui::Widget {
public:
    enum Action : int { AUp = 1, ADown, AMoveTo, ASort, ACheck, APlaceAfter, AOpen };
    explicit ExecutionOrderPane(std::string id);
    ~ExecutionOrderPane() override;
    void setHosts(ApiPaneHosts h);
    void attach(ApiFrame& frame);
    void refresh();
    void runAction(int action);
    bool selectEntry(std::string_view name);
    // Deplacer l'entree `from` (rang 0..n-1) pour qu'elle finisse au rang `to`.
    bool moveEntry(std::size_t from, std::size_t to);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] std::size_t lateCount() const noexcept { return late_.size(); }
    [[nodiscard]] const std::vector<project::api::Entry>& entries() const noexcept { return entries_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    friend class Model;
    [[nodiscard]] std::size_t selected() const;
    [[nodiscard]] domain::Index firstSectionOf(std::size_t entry) const;
    [[nodiscard]] const project::api::LateRead* lateOf(std::size_t entry) const;
    void refreshProperties();
    void updateHint();

    ApiPaneHosts                            hosts_;
    ApiFrame*                               frame_{nullptr};
    ui::TableView*                          table_{nullptr};
    ui::PropertyGrid*                       props_{nullptr};
    std::shared_ptr<Model>                  model_;
    std::string                             task_;
    std::vector<project::api::Entry>        entries_;
    std::vector<project::api::Access>       access_;
    std::vector<project::api::LateRead>     late_;
    std::size_t                             lines_{0};
    bool                                    checked_{false};   // « Verifier l'ordre » a ete demande
    std::string                             pendingSelect_;
    bool                                    syncing_{false};
    core::ConnectionScope                   links_;
};

} // namespace app
