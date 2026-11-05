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

class HmiReportLayoutPane;   // 1.25.0 : l'onglet Mise en page (HmiReportLayoutPane.hpp)
class HmiReportPreview;
class HmiReportsPane;

// ---- 1.25.0 : GENERER LE RAPPORT MAINTENANT (la maquette validee) ----------------------------------------------------
//  Une fenetre au-dessus du volet (le reste grise) : la periode (la derniere complete, celle en cours, ou une choisie :
//  du ... au ...), le format (PDF, Excel), la mise en page (celle du rapport proposee d'abord ; celles du projet, puis
//  les fournies ; << Modifier les mises en page... >> ouvre l'onglet), apres l'ecriture (ouvrir le fichier ; l'envoyer
//  aux destinataires du rapport), le nom du fichier dans exports/ ; a droite l'apercu de la page 1 (les fleches : les
//  autres pages), qui suit la mise en page choisie. Generer : exports/ ; Ailleurs... : ou l'ecrire se demande (le
//  dialogue de l'export, ExportTarget.hpp). Echap, Annuler : rien.
class HmiReportGenerateSheet final : public ui::Widget {
public:
    enum Period : int { Last = 0, Current = 1, Chosen = 2 };
    explicit HmiReportGenerateSheet(std::string id, HmiReportsPane& pane);
    void open(const std::string& report, bool current);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] const std::string& report() const noexcept { return report_; }
    // ---- les choix (les scripts, les essais) ----
    void setPeriod(int period);
    [[nodiscard]] int period() const noexcept { return period_; }
    bool setRange(const std::string& from, const std::string& to, std::string* why = nullptr);   // "2026-11-01 06:00"
    void setFormat(const std::string& format);                           // "PDF", "Excel"
    [[nodiscard]] const std::string& format() const noexcept { return format_; }
    bool setLayout(const std::string& name);                             // une mise en page connue
    [[nodiscard]] const std::string& layout() const noexcept { return layout_; }
    void setOpenAfter(bool on);
    void setSend(bool on);
    [[nodiscard]] bool openAfter() const noexcept { return openAfter_; }
    [[nodiscard]] bool send() const noexcept { return send_; }
    [[nodiscard]] std::vector<std::string> layoutChoices() const;        // dans l'ordre de la liste
    [[nodiscard]] hmi::ReportOptions options() const;
    [[nodiscard]] std::string fileHint() const;                          // "Ecrit dans exports/ : rapport_X_....pdf"
    [[nodiscard]] std::string periodText(int period) const;              // "La derniere (04/11 06:00 -> 05/11 06:00)"
    bool generate(std::string* where = nullptr);                         // Generer (exports/)
    bool generateElsewhere();                                            // Ailleurs... (la question, puis Generer)
    [[nodiscard]] HmiReportPreview& preview() noexcept { return *preview_; }
    [[nodiscard]] ui::DropDown& layoutBox() noexcept { return *layoutBox_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void sync();
    void updatePreview();
    [[nodiscard]] std::pair<double, double> range() const;   // la periode choisie, en secondes
    HmiReportsPane&                 pane_;
    bool                            open_{false};
    std::string                     report_, format_{"PDF"}, layout_, from_, to_;
    int                             period_{Last};
    bool                            openAfter_{true}, send_{true};
    std::vector<ui::ToggleButton*>  periods_, formats_;
    ui::InputText*                  fromBox_{nullptr};
    ui::InputText*                  toBox_{nullptr};
    ui::DropDown*                   layoutBox_{nullptr};
    ui::Checkbox*                   openBox_{nullptr};
    ui::Checkbox*                   sendBox_{nullptr};
    ui::Button*                     prev_{nullptr};
    ui::Button*                     next_{nullptr};
    ui::Button*                     elsewhere_{nullptr};
    ui::Button*                     cancel_{nullptr};
    ui::Button*                     ok_{nullptr};
    HmiReportPreview*               preview_{nullptr};
    std::vector<std::string>        layoutNames_;
    bool                            syncing_{false};
    gfx::Rect                       panel_{};
    float                           labelsX_{0}, rowsY_[5]{};
    core::ConnectionScope           links_;
};

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
    // 1.25.0 : avec ce qu'on a choisi dans la fenetre << Generer le rapport maintenant >> ; `openAfter` : ouvrir le fichier.
    bool generateWith(const std::string& name, const hmi::ReportOptions& options, std::string* where = nullptr, bool openAfter = false);
    // 1.25.0 : la fenetre (Generer maintenant..., Derniere periode...), l'onglet Mise en page.
    void openGenerate(const std::string& name, bool current);
    [[nodiscard]] HmiReportGenerateSheet& sheet() noexcept { return *sheet_; }
    [[nodiscard]] HmiReportLayoutPane&    layouts() noexcept { return *layouts_; }
    [[nodiscard]] hmi::DocumentPtr        document() const { return doc_; }
    void showLayouts(const std::string& select = {});

    [[nodiscard]] std::string selectedReport() const;
    void selectReport(const std::string& name);

    struct Hosts {
        // Ecrire le rapport (la simulation s'il y en a une, sinon l'historique).
        std::function<bool(hmi::Id report, bool current, std::string* where)> write;
        // 1.25.0 : ecrire avec ce qu'on a choisi (la periode, le format, la mise en page, l'envoi) ; vide : write.
        std::function<bool(hmi::Id report, const hmi::ReportOptions& options, std::string* where)> writeWith;
        // 1.25.0 : ouvrir un fichier ecrit (son programme) ; "" : fait, sinon pourquoi.
        std::function<std::string(const std::string& path)> openFile;
        std::function<std::string(hmi::Id report)>                          next;       // le prochain depart ("2026-09-26 06:00")
        std::function<std::string()>                                        projectFolder;
    };
    void setHosts(Hosts h);

    // Sans simulation : le rapport depuis l'historique seul, ecrit par `write`.
    [[nodiscard]] static bool writeOffline(const hmi::Project&, const hmi::History&, hmi::Id report, bool current,
                                           const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where,
                                           hmi::ReportOutput* output = nullptr);
    // 1.25.0 : avec ce qu'on a choisi (la periode, le format, la mise en page ; sans envoi : output->recipients vide).
    [[nodiscard]] static bool writeOffline(const hmi::Project&, const hmi::History&, hmi::Id report, const hmi::ReportOptions& options,
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
    friend class HmiReportGenerateSheet;
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
    HmiReportLayoutPane*   layouts_{nullptr};      // 1.25.0 : l'onglet Mise en page
    HmiReportGenerateSheet* sheet_{nullptr};       // 1.25.0 : Generer le rapport maintenant
    std::shared_ptr<ui::ITableModel> tableModel_, filesModel_;
    std::vector<std::string> order_;
    double                 lastLive_{-10};
    bool                   refreshing_{false};
    std::string            message_;
    core::ConnectionScope  links_;
};

} // namespace app
