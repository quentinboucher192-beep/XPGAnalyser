// =============================================================================
//  app/GrafcetPanels.hpp - the tables beside the drawing
// -----------------------------------------------------------------------------
//  WHAT IS NOT HERE, AND WHY.
//
//  The layout these panels follow asks for a few columns the engine does not
//  keep, and they are absent rather than blank:
//
//    "Nb franchissements" per transition - ST_GC_Transition has no counter. The
//        engine keeps Debug.TotalTransitionsFired for the whole chart and
//        Debug.LastTransitionFiredId for the most recent one, and that is all.
//    "Dernier franchissement" / "Derniere activation" as a wall-clock time -
//        the engine has no per-object timestamp. Runtime.TsNow is a TIME
//        accumulated since the PLC started, not a date.
//    CPU and memory - those belong to the PLC, and nothing in the project
//        reports them.
//
//  A column of dashes reads as "never happened", which is a different and wrong
//  statement. So the columns are the ones there is an answer for, and the
//  missing ones are written down here instead of drawn empty.
//
//  Every value is read from the runtime on demand, never cached: a cached value
//  that stops updating looks exactly like a machine that stopped moving.
// =============================================================================
#pragma once

#include "../project/Grafcet.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <string>

namespace app {

    // One live field of one array element, e.g. Steps_ManuA[2].ActiveTime. Empty
    // when there is no run, which the callers render as "-" rather than as "off".
    [[nodiscard]] std::string grafcetField(const sim::Runtime*, const grafcet::Chart&,
        std::string_view array, int index,
        std::string_view field);
    [[nodiscard]] bool grafcetFlag(const sim::Runtime*, const grafcet::Chart&,
        std::string_view array, int index, std::string_view field);

    // A chart's engine instance field, e.g. Gc_ManuA.Debug.ActiveStepCount.
    [[nodiscard]] std::string engineField(const sim::Runtime*, const grafcet::Chart&,
        std::string_view path);

    class GrafcetStepModel final : public ui::ITableModel {
    public:
        enum Column { Id, Name, State, ActiveTime, ColumnCount };
        GrafcetStepModel(grafcet::Chart chart, sim::Runtime** runtime);

        [[nodiscard]] std::size_t rowCount() const override { return chart_.steps.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        // Sorting is by the declared order, which is the chart's own order: an id
        // is the reader's landmark here, not an arbitrary key, and re-sorting a
        // GRAFCET by name would scramble the thing they came to read.
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override {
            return a < b;
        }


        [[nodiscard]] std::size_t activeCount() const;

    private:
        grafcet::Chart chart_;
        sim::Runtime** runtime_;   // the pane's pointer, so a run starting is seen
    };

    class GrafcetTransitionModel final : public ui::ITableModel {
    public:
        enum Column { Id, Condition, State, Source, Destination, ColumnCount };
        GrafcetTransitionModel(grafcet::Chart chart, sim::Runtime** runtime);

        [[nodiscard]] std::size_t rowCount() const override { return chart_.transitions.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        // Sorting is by the declared order, which is the chart's own order: an id
        // is the reader's landmark here, not an arbitrary key, and re-sorting a
        // GRAFCET by name would scramble the thing they came to read.
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override {
            return a < b;
        }


    private:
        grafcet::Chart chart_;
        sim::Runtime** runtime_;
    };

    class GrafcetActionModel final : public ui::ITableModel {
    public:
        enum Column { Id, Name, Trigger, Step, State, Elapsed, ColumnCount };
        GrafcetActionModel(grafcet::Chart chart, sim::Runtime** runtime);

        [[nodiscard]] std::size_t rowCount() const override { return chart_.actions.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        // Sorting is by the declared order, which is the chart's own order: an id
        // is the reader's landmark here, not an arbitrary key, and re-sorting a
        // GRAFCET by name would scramble the thing they came to read.
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override {
            return a < b;
        }


        [[nodiscard]] std::size_t firingCount() const;

    private:
        grafcet::Chart chart_;
        sim::Runtime** runtime_;
    };

    // The line under the drawing: cycle, how many steps are active, how many
    // transitions have fired, and any fault the engine is reporting. Composed here
    // rather than in the widget so the wording can be asserted.
    // ---- 1.10, chantier R ----------------------------------------------------

    // L'etat d'un grafcet en simulation, en quelques mots : "X3, X5" (ses etapes
    // actives), "fini", "d\xC3\xA9" "faut X9" ; vide hors simulation. `tone` :
    // 0 rien, 1 en marche, 2 fini, 3 en defaut.
    [[nodiscard]] std::string chartRunState(const sim::Runtime*, const grafcet::Chart&, int* tone = nullptr);

    // Le navigateur des instances de DFB_GRAFCETENGINE du programme : une ligne par
    // grafcet (nom, comptes, avertissements ; en simulation son etat).
    class GrafcetInstanceModel final : public ui::ITableModel {
    public:
        enum Column { Name, Counts, State, ColumnCount };
        struct Entry {
            grafcet::Chart chart;
            std::string    section;          // la section qui le construit (SFC_DetoxalA)
            std::size_t    severe{ 0 };      // controles en defaut
            std::size_t    remarks{ 0 };     // remarques (le grafcet marche)
        };
        GrafcetInstanceModel(std::vector<Entry> entries, sim::Runtime** runtime);

        [[nodiscard]] std::size_t rowCount() const override { return entries_.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override {
            return a < b;
        }
        [[nodiscard]] const Entry* entry(ui::RowIndex row) const {
            return row < entries_.size() ? &entries_[row] : nullptr;
        }
        [[nodiscard]] int rowOf(const std::string& instance) const;
        [[nodiscard]] std::size_t totalSteps() const;
        [[nodiscard]] std::size_t totalTransitions() const;
        [[nodiscard]] std::size_t totalActions() const;

    private:
        std::vector<Entry> entries_;
        sim::Runtime**     runtime_;
    };

    // Une table de textes, ses colonnes nommees (Controles, Historique, Proprietes).
    class GrafcetRowsModel final : public ui::ITableModel {
    public:
        explicit GrafcetRowsModel(std::vector<std::string> headers) : headers_(std::move(headers)) {}
        void setRows(std::vector<std::vector<std::string>> rows, std::vector<gfx::Color> colors = {}) {
            rows_ = std::move(rows);
            colors_ = std::move(colors);
            modelReset->emit();
        }
        [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
        [[nodiscard]] std::string headerText(std::size_t c) const override {
            return c < headers_.size() ? headers_[c] : std::string{};
        }
        [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t c) const override {
            return row < rows_.size() && c < rows_[row].size() ? rows_[row][c] : std::string{};
        }
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t) const override {
            ui::CellStyle style;
            if (row < colors_.size() && colors_[row].a) style.fg = colors_[row];
            return style;
        }
        [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }

    private:
        std::vector<std::string>              headers_;
        std::vector<std::vector<std::string>> rows_;
        std::vector<gfx::Color>               colors_;
    };

    [[nodiscard]] std::string executionSummary(const sim::Runtime*, const grafcet::Chart&);

    // The engine's four fault flags, in words. Empty when none is set.
    [[nodiscard]] std::vector<std::string> engineFaults(const sim::Runtime*, const grafcet::Chart&);

} // namespace app