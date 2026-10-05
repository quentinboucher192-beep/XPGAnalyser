// =============================================================================
//  app/hmi/HmiPanes.hpp - les onglets des entrees de l'arbre IHM
// -----------------------------------------------------------------------------
//  HmiConfigPane   Configuration : identite du projet IHM, resolution, vue de
//                  demarrage, statistiques, et les variables du programme que
//                  les vues peuvent lire (type, portee, utilisation, reference,
//                  taille), relues a chaque ouverture : la synchronisation avec
//                  le projet principal est automatique.
//  HmiViewsPane    Vues : creer, dupliquer, renommer, supprimer, ordonner, vue
//                  de demarrage, ouvrir.
//  HmiReportPane   Generer / Compiler : le rapport, et un double-clic pour
//                  aller a l'objet en cause.
//  HmiInfoPane     une entree dont le contenu arrive dans un lot suivant : ce
//                  qu'elle fera, dit en clair.
//
//  Chaque modification est une commande (hmi::changeProject / changeView).
// =============================================================================
#pragma once

#include "HmiApiVarsPane.hpp"          // 1.11.1 (API-V) : les variables de l'automate en arbre
#include "HmiPanels.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les listes rangees en dossiers
#include "../../core/Command.hpp"
#include "../../hmi/HmiApiVars.hpp"     // 1.11.1 (API-M) : le modele
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace domain { class Project; }
namespace importer { struct AnalysisReport; }

namespace app {

// Ou le projet IHM lit chaque variable : "Vue_Production/Alarme.value"...
// La cle est le nom de la variable racine EN MAJUSCULES (l'ST ignore la casse).
using HmiUsageIndex = std::map<std::string, std::vector<std::string>, std::less<>>;
[[nodiscard]] HmiUsageIndex hmiUsageIndex(const hmi::Project&);

class HmiVariableTableModel final : public ui::ITableModel {
public:
    enum Column : std::size_t { Name, Type, Scope, Usage, Reference, Size, ColumnCount };
    enum class Filter : std::uint8_t { All, Global, Local, Ddt, Dfb, Io };
    HmiVariableTableModel(std::shared_ptr<const domain::Project> plc, const importer::AnalysisReport* report,
                          HmiUsageIndex usage, Filter filter);
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
    [[nodiscard]] std::string headerText(std::size_t) const override;
    [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
    [[nodiscard]] bool less(ui::RowIndex, ui::RowIndex, std::size_t) const override;
private:
    struct Row { std::string name, type, scope, reference, where; std::size_t usage{0}; double bytes{-1}; bool io{false}; };
    std::vector<Row> rows_;
};

class HmiConfigPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiConfigPane(std::string id, hmi::DocumentPtr doc, Apply apply,
                  std::shared_ptr<const domain::Project> plc, const importer::AnalysisReport* report,
                  std::function<std::uint64_t()> bytesOnDisk = {});
    void refresh();
    void setPlc(std::shared_ptr<const domain::Project> plc, const importer::AnalysisReport* report);
    // Pour les tests et les scripts : la grille et la table des variables.
    // 1.11.1 (API-V) : la table est celle de l'arbre des variables de l'automate.
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&    variables() noexcept { return apiVars_->table(); }
    [[nodiscard]] HmiApiVarsView&   apiVars() noexcept { return *apiVars_; }
    // R1111-5 : l'arbre rempli tout de suite (un onglet tout juste ouvert ne
    // l'est qu'a sa premiere mise en page) - pour y choisir une variable.
    void ensureBuilt();
    // 1.11.1 (API-V) : un emploi choisi dans l'arbre (« Utilisee par l'IHM ») :
    // l'endroit, comme un constat de Compiler - l'ecran y mene (openHmiIssue).
    const core::SignalPtr<const hmi::Issue&> useActivated = core::Signal<const hmi::Issue&>::create();
    // ... l'emploi `index` (dans l'ordre du modele : Model::usesOf) d'un chemin.
    [[nodiscard]] hmi::Issue issueOfUse(const std::string& path, std::size_t index) const;
    // Le dernier message de l'arbre (« API.X copie »...).
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
protected:
    void onLayout() override;
private:
    void rebuild();
    hmi::DocumentPtr doc_;
    Apply apply_;
    std::shared_ptr<const domain::Project> plc_;
    const importer::AnalysisReport* report_{nullptr};
    std::function<std::uint64_t()> bytesOnDisk_;
    ui::Splitter*     split_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    // 1.11.1 (API-V) : l'arbre (son filtre et sa recherche), sur le modele d'API-M ;
    // les noeuds du modele par cle, pour demander leurs enfants.
    HmiApiVarsView*   apiVars_{nullptr};
    std::shared_ptr<const hmi::apivars::Model> apiModel_;
    std::map<std::string, hmi::apivars::Node> apiNodes_;
    ApiVarNode adoptApiNode(const hmi::apivars::Node& n);
    HmiTitledPanel*   varsPanel_{nullptr};
    std::string       message_;
    core::ConnectionScope links_;
    bool dirty_{true};
};

// Les commandes "nouvelle vue" et "supprimer la vue", pour le panneau et pour
// l'hote qui passe par un dialogue. nullptr si rien ne change (nom vide...).
[[nodiscard]] core::CommandPtr hmiNewViewCommand(const hmi::DocumentPtr&, std::string name, int width,
                                                 int height, std::string description, hmi::Id* made = nullptr,
                                                 std::string role = "vue",    // lot 8 : le role (vue, popup, modele...)
                                                 std::string templateKey = {});   // lot 12 : le modele de vue (vide, synoptique...)
[[nodiscard]] core::CommandPtr hmiDeleteViewCommand(const hmi::DocumentPtr&, hmi::Id view);

class HmiViewsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiViewsPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();
    const core::SignalPtr<hmi::Id> openView = core::Signal<hmi::Id>::create();
    [[nodiscard]] hmi::Id selectedView() const;
    void selectView(hmi::Id);
    // L'hote peut prendre la main sur "Nouvelle vue" (un dialogue, avec le role
    // du dossier montre) et sur "Supprimer" (une confirmation). Sans hote,
    // l'action est immediate.
    void setHosts(std::function<void(const std::string& role)> onNew, std::function<void(hmi::Id)> onDelete);
    // Lot 12 : "Dupliquer en remplacant" - l'hote demande le nom, le texte et son
    // remplacant (un dialogue) ; sans hote, une copie simple.
    void setDuplicateReplaceHost(std::function<void(hmi::Id)> ask);
    // Lot 12 : "Depuis un type" - l'hote demande l'instance (DDT, DFB) et le role.
    void setFromTypeHost(std::function<void()> ask);
    // Lot 20 : Enregistrer comme modele, Exporter les vues, Importer des vues
    // (les dialogues sont a l'ecran).
    struct PackageHosts {
        std::function<void(hmi::Id)> saveTemplate;
        std::function<void(hmi::Id)> exportViews;
        std::function<void()>        importViews;
        // 1.11.2 (decision 162) : dans le dossier Symboles, Exporter les
        // symboles (le symbole choisi coche) et Importer des symboles.
        std::function<void(hmi::Id)> exportSymbols;
        std::function<void()>        importSymbols;
    };
    void setPackageHosts(PackageHosts h);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    // Lot 8 : le dossier montre, comme dans l'arbre. -1 : toutes les vues ; 0
    // Modeles, 1 Vues, 2 Popups ; 3, 4, 5 : ecrans modeles, en-tetes, pieds.
    void setFolder(int folder);
    [[nodiscard]] int folder() const noexcept { return folder_; }
    [[nodiscard]] std::string folderTitle() const;       // "Popups", "En-t\xC3\xAAtes"...
    [[nodiscard]] std::string folderRole() const;        // le role d'une vue creee ici
    // Lot 21 : les dossiers de la liste montree (Vues, Popups, un modele,
    // Symboles) ; tous les roles (-1) ou Modeles (0) : une liste melee, sans.
    [[nodiscard]] HmiFolderTable& folders() noexcept { return *folders_; }
    [[nodiscard]] std::vector<hmi::Id> selectedViews() const;
protected:
    void onLayout() override;
private:
    hmi::DocumentPtr doc_;
    Apply apply_;
    HmiToolStrip*     tools_{nullptr};
    ui::TableView*    table_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::unique_ptr<HmiFolderTable> folders_;            // lot 21
    std::function<void(const std::string&)> onNew_;
    std::function<void(hmi::Id)> onDelete_;
    std::function<void(hmi::Id)> onDuplicateReplace_;   // lot 12
    std::function<void()>        onFromType_;           // lot 12
    PackageHosts                 packageHosts_;         // lot 20
    ui::Widget*       thumb_{nullptr};                   // lot 12 : la vignette
    int folder_{-1};
    core::ConnectionScope links_;
};

class HmiReportPane final : public ui::Widget {
public:
    enum class Mode : std::uint8_t { Generate, Compile };
    HmiReportPane(std::string id, hmi::DocumentPtr doc, Mode mode, hmi::NameExists plcHasName);
    void run();
    [[nodiscard]] const std::vector<hmi::Issue>& issues() const noexcept { return issues_; }
    void setNameExists(hmi::NameExists f) { exists_ = std::move(f); }
    // Lot 14 : le plan d'adressage Modbus (les variables sans adresse, la table).
    void setCommPlan(std::function<hmi::comm::Plan()> f) { commPlan_ = std::move(f); }
    void setPlcScalar(std::function<bool(std::string_view)> f) { plcScalar_ = std::move(f); }
    // ---- Lot API 8 : les expressions impossibles ---- les chemins de l'automate
    void setPlcPaths(hmi::exprcheck::PlcPaths p) { plcPaths_ = std::move(p); }
    // Double-clic sur un constat : la vue et l'objet en cause.
    const core::SignalPtr<hmi::Id, hmi::Id> goTo = core::Signal<hmi::Id, hmi::Id>::create();
    // ... et le constat entier : un script et sa ligne, une action et son rang.
    const core::SignalPtr<hmi::Issue> issueActivated = core::Signal<hmi::Issue>::create();
    // 1.10.4 : "Remplacer..." (Compiler) ; 1.11 (REP) : sur le constat d'un repere qui n'est pas une variable :
    // Dupliquer s'ouvre sur l'objet avec 0 copie, pour remplir le repere dans l'original.
    const core::SignalPtr<hmi::Id, hmi::Id> replaceMarkers = core::Signal<hmi::Id, hmi::Id>::create();
    [[nodiscard]] bool canReplace() const;      // la ligne choisie porte la phrase de Dupliquer (dup::kMarkerHint)
    bool replaceSelected();                      // "Remplacer..." (faux : rien a remplacer)
    // ---- Lot API 8 : l'arbre du projet ---- apres chaque run() (issues() a jour)
    const core::SignalPtr<> ran = core::Signal<>::create();
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
protected:
    void onLayout() override;
private:
    hmi::DocumentPtr doc_;
    Mode mode_;
    hmi::NameExists exists_;
    std::function<hmi::comm::Plan()> commPlan_;   // lot 14
    std::function<bool(std::string_view)> plcScalar_;
    hmi::exprcheck::PlcPaths plcPaths_{};   // ---- Lot API 8 : les expressions impossibles ----
    std::vector<hmi::Issue> issues_;
    HmiToolStrip*  tools_{nullptr};
    ui::TableView* table_{nullptr};
    ui::StatusBar* status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    float laidOutWidth_{-1.f};             // lot 13 : la largeur de la derniere mise en page
    core::ConnectionScope links_;
};

class HmiInfoPane final : public ui::Widget {
public:
    HmiInfoPane(std::string id, std::string title, std::vector<std::string> lines);
protected:
    void onPaint(const ui::PaintContext&) override;
private:
    std::string title_;
    std::vector<std::string> lines_;
};

} // namespace app
