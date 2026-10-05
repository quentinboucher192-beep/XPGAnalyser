// =============================================================================
//  app/hmi/HmiReportPanes.hpp - Configuration > Rapports (lot 14)
// -----------------------------------------------------------------------------
//  LES RAPPORTS PERIODIQUES. Au milieu, la liste : chacun, sa periode (chaque
//  jour, chaque semaine, chaque mois, a l'heure dite), son format (PDF, Excel),
//  ce qu'il contient, ses destinataires, et quand part le prochain ; l'onglet
//  Ecrits : les rapports deja dans exports/.
//
//  A droite, le rapport choisi. GENERER MAINTENANT : la periode en cours,
//  jusqu'a maintenant ; DERNIERE PERIODE : la derniere complete (celle
//  d'hier, de la semaine passee...). Si la simulation IHM tourne, le rapport
//  prend ses compteurs de production ; sinon l'historique seul. Lot API 8 : les
//  deux boutons demandent OU (ExportTarget.hpp : exports/ propose, le bouton
//  ... ailleurs) ; generate() ecrit sans demander, comme les envois programmes.
//
//  Chaque changement du projet est une commande : Ctrl+Z.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiReportsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiReportsPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes du volet, pour les boutons, les scripts et les tests.
    bool addReport(const std::string& name = "Rapport_journalier", std::string* why = nullptr);
    bool removeReport(const std::string& name, std::string* why = nullptr);
    // "nom", "titre", "periode" (jour, semaine, mois), "heure", "jour_semaine",
    // "jour_mois", "format", "alarmes", "mesures", "production", "evenements",
    // "destinataires", "actif".
    bool setReportField(const std::string& name, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Generer maintenant (`current` : la periode en cours ; sinon la derniere
    // complete). `where` : le fichier, ou pourquoi.
    bool generate(const std::string& name, bool current, std::string* where = nullptr);

    [[nodiscard]] std::string selectedReport() const;
    void selectReport(const std::string& name);

    struct Hosts {
        // Ecrire le rapport (la simulation s'il y en a une, sinon l'historique).
        std::function<bool(hmi::Id report, bool current, std::string* where)> write;
        std::function<std::string(hmi::Id report)>                          next;       // le prochain depart ("2026-09-26 06:00")
        std::function<std::string()>                                        projectFolder;
    };
    void setHosts(Hosts h);

    // Sans simulation : le rapport depuis l'historique seul, ecrit par `write`.
    [[nodiscard]] static bool writeOffline(const hmi::Project&, const hmi::History&, hmi::Id report, bool current,
                                           const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where,
                                           hmi::ReportOutput* output = nullptr);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     reports() noexcept { return *table_; }
    [[nodiscard]] ui::TableView&     written() noexcept { return *files_; }
    [[nodiscard]] ui::TabControl&    tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    void refreshFiles();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool change(const std::string& label, const std::function<void(hmi::Project&)>& fn);

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    Hosts                  hosts_;
    HmiToolStrip*          tools_{nullptr};
    ui::TabControl*        tabs_{nullptr};
    ui::TableView*         table_{nullptr};
    ui::TableView*         files_{nullptr};
    ui::PropertyGrid*      grid_{nullptr};
    ui::StatusBar*         status_{nullptr};
    std::shared_ptr<ui::ITableModel> tableModel_, filesModel_;
    std::vector<std::string> order_;
    double                 lastLive_{-10};
    bool                   refreshing_{false};
    std::string            message_;
    core::ConnectionScope  links_;
};

} // namespace app
