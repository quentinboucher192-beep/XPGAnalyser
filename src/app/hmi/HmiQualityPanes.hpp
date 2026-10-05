// =============================================================================
//  app/hmi/HmiQualityPanes.hpp - la qualite (lot 13) : IHM > Essais
// -----------------------------------------------------------------------------
//  IHM > ESSAIS. Les essais de reception du projet (hmi::TestScenario) : a
//  gauche la liste (et le bilan du dernier passage de chacun), au milieu les
//  pas de l'essai choisi avec leur verdict, a droite la fiche du pas (l'action,
//  la cible, la valeur, l'attendu, une note) ou de l'essai.
//
//  LANCER : la simulation rejoue l'essai, pas a pas, a l'ecran (l'hote ouvre
//  l'onglet Simulation) ; chaque verdict revient ici au fil de l'eau (le
//  Document garde le dernier rapport de chaque essai). TOUT LANCER : chaque
//  essai d'un coup, sans ecran, au temps simule (l'IHM relie a l'automate
//  simule s'il tourne). LE RAPPORT : l'essai choisi (ou toute la campagne) en
//  PDF, Excel ou CSV, dans le dossier exports/ du projet.
//
//  Chaque changement du projet (un essai, un pas) est une commande : Ctrl+Z.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiScenarios.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }
namespace sim { class Environment; }

namespace app {

class HmiScenariosPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiScenariosPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes du volet, pour les boutons, les scripts et les tests.
    hmi::Id addScenario(const std::string& name = "Essai");
    hmi::Id duplicateScenario(hmi::Id);
    bool    removeScenario(hmi::Id);
    // "nom", "description".
    bool    setScenarioField(hmi::Id, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Un pas apres `after` (-1 : a la fin) ; rend son rang (-1 : refuse).
    int     addStep(hmi::Id scenario, hmi::TestStep step = {}, int after = -1);
    bool    removeStep(hmi::Id scenario, int index);
    bool    moveStep(hmi::Id scenario, int index, int delta);
    // "action", "cible", "valeur", "attendu", "note".
    bool    setStepField(hmi::Id scenario, int index, const std::string& key, const std::string& value, std::string* why = nullptr);

    [[nodiscard]] hmi::Id selectedScenario() const;
    [[nodiscard]] int     selectedStep() const;          // -1 : aucun
    void selectScenario(hmi::Id);
    void selectStep(int index);

    // Tout lancer, sans ecran : un rapport par essai (dans le Document).
    std::size_t runAll();
    // Le rapport de l'essai choisi (sinon de toute la campagne) dans exports/.
    bool exportReport(hmi::ExportFormat, std::string* where = nullptr);

    struct Hosts {
        std::function<void(hmi::Id)>                                   play;       // la simulation rejoue l'essai
        std::function<void()>                                          stop;       // et l'arrete
        std::function<::sim::Environment*()>                           plc;        // Tout lancer : l'automate simule (ou nul)
        std::function<bool(const hmi::ExportRequest&, std::string*)>   exportFile;
        std::function<void(hmi::Id)>                                   remove;     // confirmer la suppression d'un essai
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     scenarios() noexcept { return *list_; }
    [[nodiscard]] ui::TableView&     steps() noexcept { return *steps_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refreshSteps();
    void rebuildProperties();
    void say(std::string text, bool error = false);
    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    Hosts                 hosts_;
    HmiToolStrip*         tools_{nullptr};
    ui::TableView*        list_{nullptr};
    ui::TableView*        steps_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    std::shared_ptr<ui::ITableModel> listModel_, stepsModel_;
    std::vector<hmi::Id>  order_;
    hmi::Id               shown_{hmi::kNoId};      // l'essai dont les pas sont montres
    bool                  refreshing_{false};
    std::string           message_;
    core::ConnectionScope links_;
};

} // namespace app
