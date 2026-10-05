#include "GrafcetPanels.hpp"

#include "../project/GrafcetCheck.hpp"
#include "../project/GrafcetDiagram.hpp"

#include <algorithm>

namespace app {

    using namespace grafcet;

    namespace {

        bool truthy(const std::string& text) {
            return text == "TRUE" || text == "1" || text == "true";
        }

        // The array index is the position Builder wrote to, not the step's id. They
        // coincide in every chart of the reference project, and relying on that would
        // break on the first one where they do not.
        int indexOfStep(const Chart& chart, int id) {
            const auto at = std::find_if(chart.steps.begin(), chart.steps.end(),
                [&](const Step& s) { return s.id == id; });
            return at == chart.steps.end() ? -1 : static_cast<int>(std::distance(chart.steps.begin(), at));
        }

        std::string joinIds(const std::vector<int>& ids) {
            std::string out;
            for (std::size_t i = 0; i < ids.size(); ++i) {
                if (i) out += ", ";
                out += "X" + std::to_string(ids[i]);
            }
            return out.empty() ? "-" : out;
        }

        constexpr const char* kNoRun = "-";

    } // namespace

    // ---------------------------------------------------------------------------
    std::string grafcetField(const sim::Runtime* runtime, const Chart& chart,
        std::string_view array, int index, std::string_view field) {
        if (!runtime || chart.arrayPrefix.empty() || index < 0) return {};
        const std::string name = std::string(array) + "_" + chart.arrayPrefix + "["
            + std::to_string(index) + "]." + std::string(field);
        sim::Value v;
        // 1.10 : les tableaux d'une unite de programme sont nommes avec elle
        if (!chart.scope.empty() && runtime->get(chart.scope + name, v)) return v.display();
        if (!runtime->get(name, v)) return {};
        return v.display();
    }

    bool grafcetFlag(const sim::Runtime* runtime, const Chart& chart,
        std::string_view array, int index, std::string_view field) {
        return truthy(grafcetField(runtime, chart, array, index, field));
    }

    std::string engineField(const sim::Runtime* runtime, const Chart& chart,
        std::string_view path) {
        if (!runtime || chart.instance.empty()) return {};
        sim::Value v;
        if (!chart.scope.empty() && runtime->get(chart.scope + chart.instance + "." + std::string(path), v))
            return v.display();
        if (!runtime->get(chart.instance + "." + std::string(path), v)) return {};
        return v.display();
    }

    // ---------------------------------------------------------------------------
    GrafcetStepModel::GrafcetStepModel(Chart chart, sim::Runtime** runtime)
        : chart_(std::move(chart)), runtime_(runtime) {}

    std::string GrafcetStepModel::headerText(std::size_t column) const {
        switch (column) {
        case Id:         return "N\xC2\xB0";
        case Name:       return "Nom";
        case State:      return "\xC3\x89tat";
        case ActiveTime: return "Active for";
        default:         return {};
        }
    }

    std::size_t GrafcetStepModel::activeCount() const {
        std::size_t n = 0;
        for (const auto& s : chart_.steps)
            if (grafcetFlag(*runtime_, chart_, "Steps", indexOfStep(chart_, s.id), "Active")) ++n;
        return n;
    }

    std::string GrafcetStepModel::cellText(ui::RowIndex row, std::size_t column) const {
        if (row >= chart_.steps.size()) return {};
        const auto& s = chart_.steps[row];
        const int index = indexOfStep(chart_, s.id);

        switch (column) {
        case Id:   return "X" + std::to_string(s.id);
        case Name: {
            std::string label = s.name.empty() ? std::string("-") : s.name;
            // What kind of step it is belongs beside the name: the drawing shows
            // it with a double frame, and a table has no frames.
            if (s.initial) label += "  (initiale)";
            if (s.isFinal) label += "  (finale)";
            return label;
        }
        case State: {
            if (!*runtime_) return kNoRun;   // no run: not "inactive", unknown
            return grafcetFlag(*runtime_, chart_, "Steps", index, "Active") ? "active"
                : "inactive";
        }
        case ActiveTime: {
            if (!*runtime_) return kNoRun;
            if (!grafcetFlag(*runtime_, chart_, "Steps", index, "Active")) return "";
            const auto text = grafcetField(*runtime_, chart_, "Steps", index, "ActiveTime");
            const auto readable = durationText(text);
            return text.empty() ? kNoRun : readable.empty() ? text : readable;
        }
        default: return {};
        }
    }

    ui::CellStyle GrafcetStepModel::cellStyle(ui::RowIndex row, std::size_t column) const {
        ui::CellStyle style;
        if (row >= chart_.steps.size() || !*runtime_) return style;
        if (grafcetFlag(*runtime_, chart_, "Steps", indexOfStep(chart_, chart_.steps[row].id),
            "Active")) {
            style.fg = gfx::Color::rgb(0x7FD08A);
            if (column == State) style.bold = true;
        }
        return style;
    }

    // ---------------------------------------------------------------------------
    GrafcetTransitionModel::GrafcetTransitionModel(Chart chart, sim::Runtime** runtime)
        : chart_(std::move(chart)), runtime_(runtime) {}

    std::string GrafcetTransitionModel::headerText(std::size_t column) const {
        switch (column) {
        case Id:          return "N\xC2\xB0";
        case Condition:   return "R\xC3\xA9" "ceptivit\xC3\xA9";
        case State:       return "\xC3\x89tat";
        case Source:      return "De";
        case Destination: return "Vers";
        default:          return {};
        }
    }

    std::string GrafcetTransitionModel::cellText(ui::RowIndex row, std::size_t column) const {
        if (row >= chart_.transitions.size()) return {};
        const auto& t = chart_.transitions[row];
        switch (column) {
        case Id: return "T" + std::to_string(t.id);
        case Condition:
            // The engine's own label when it has one; the ST expression is a
            // paragraph and belongs in the panel below, not in a column.
            return drawnCondition(chart_, t);
        case State: {
            if (!*runtime_) return kNoRun;
            const int index = static_cast<int>(row);
            if (grafcetFlag(*runtime_, chart_, "Trans", index, "Validated")) return "valid\xC3\xA9" "e";
            if (grafcetFlag(*runtime_, chart_, "Trans", index, "Condition")) return "vraie";
            return "fausse";
        }
        case Source:      return joinIds(t.sources);
        case Destination: return joinIds(t.destinations);
        default:          return {};
        }
    }

    ui::CellStyle GrafcetTransitionModel::cellStyle(ui::RowIndex row, std::size_t column) const {
        ui::CellStyle style;
        if (row >= chart_.transitions.size() || !*runtime_) return style;
        const int index = static_cast<int>(row);
        if (grafcetFlag(*runtime_, chart_, "Trans", index, "Validated")) {
            style.fg = gfx::Color::rgb(0x7FD08A);
            style.bold = column == State;
        }
        else if (grafcetFlag(*runtime_, chart_, "Trans", index, "Condition")) {
            style.fg = gfx::Color::rgb(0x6FA8DC);
        }
        return style;
    }

    // ---------------------------------------------------------------------------
    GrafcetActionModel::GrafcetActionModel(Chart chart, sim::Runtime** runtime)
        : chart_(std::move(chart)), runtime_(runtime) {}

    std::string GrafcetActionModel::headerText(std::size_t column) const {
        switch (column) {
        case Id:      return "N\xC2\xB0";
        case Name:    return "Nom";
        case Trigger: return "Genre";
        case Step:    return "\xC3\x89tape";
        case State:   return "\xC3\x89tat";
        case Elapsed: return "\xC3\x89" "coul\xC3\xA9";
        default:      return {};
        }
    }

    std::size_t GrafcetActionModel::firingCount() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < chart_.actions.size(); ++i)
            if (grafcetFlag(*runtime_, chart_, "Acts", static_cast<int>(i), "Out")) ++n;
        return n;
    }

    std::string GrafcetActionModel::cellText(ui::RowIndex row, std::size_t column) const {
        if (row >= chart_.actions.size()) return {};
        const auto& a = chart_.actions[row];
        switch (column) {
        case Id:   return "A" + std::to_string(a.id);
        case Name: return a.name.empty() ? std::string("-") : a.name;
        case Trigger: {
            // In words, not "k=11". The whole reason this column is wide.
            return kindLabel(a.kind, a.delay) + "  (" + kindCode(a.kind) + ")";
        }
        case Step:  return a.boundStep < 0 ? std::string("-")
            : "X" + std::to_string(a.boundStep);
        case State: {
            if (!*runtime_) return kNoRun;
            return grafcetFlag(*runtime_, chart_, "Acts", static_cast<int>(row), "Out")
                ? "en cours" : "arr\xC3\xAAt\xC3\xA9" "e";
        }
        case Elapsed: {
            if (!*runtime_ || !usesDelay(a.kind)) return kNoRun;
            const auto text = grafcetField(*runtime_, chart_, "Acts",
                static_cast<int>(row), "TONElapsed");
            const auto readable = durationText(text);
            return text.empty() ? kNoRun : readable.empty() ? text : readable;
        }
        default: return {};
        }
    }

    ui::CellStyle GrafcetActionModel::cellStyle(ui::RowIndex row, std::size_t column) const {
        ui::CellStyle style;
        if (row >= chart_.actions.size() || !*runtime_) return style;
        if (grafcetFlag(*runtime_, chart_, "Acts", static_cast<int>(row), "Out")) {
            style.fg = gfx::Color::rgb(0xE8C46F);
            style.bold = column == State;
        }
        return style;
    }

    // ---------------------------------------------------------------------------
    std::vector<std::string> engineFaults(const sim::Runtime* runtime, const Chart& chart) {
        std::vector<std::string> out;
        if (!runtime) return out;
        // The four the engine actually raises. Spelled out, because
        // "NoActiveStepFault = TRUE" tells an operator nothing they can act on.
        struct Flag { const char* path; const char* meaning; };
        static const Flag flags[] = {
            {"Debug.ConflictDetected",  "deux transitions franchies depuis la m\xC3\xAAme \xC3\xA9tape dans un cycle"},
            {"Debug.WatchdogDetected",  "le grafcet ne s'est pas stabilis\xC3\xA9 dans son cycle"},
            {"Debug.NoActiveStepFault", "aucune \xC3\xA9tape active : le grafcet est arr\xC3\xAAt\xC3\xA9"},
            {"Debug.ConfigFault",       "la d\xC3\xA9" "claration du grafcet est incoh\xC3\xA9rente"},
        };
        for (const auto& f : flags)
            if (truthy(engineField(runtime, chart, f.path))) out.emplace_back(f.meaning);
        return out;
    }

    std::string executionSummary(const sim::Runtime* runtime, const Chart& chart) {
        if (!runtime)
            return chart.name + "  \xC2\xB7  API arr\xC3\xAAt\xC3\xA9" "e : " + std::to_string(chart.steps.size())
            + " \xC3\xA9tapes, " + std::to_string(chart.transitions.size()) + " transitions, "
            + std::to_string(chart.actions.size()) + " actions";

        std::string out = chart.name;
        if (const auto cycle = engineField(runtime, chart, "Runtime.Cycle"); !cycle.empty())
            out += "   cycle " + cycle;
        if (const auto since = engineField(runtime, chart, "Runtime.TsNow"); !since.empty())
            out += "   en marche depuis " + since;

        std::size_t active = 0;
        for (std::size_t i = 0; i < chart.steps.size(); ++i)
            if (grafcetFlag(runtime, chart, "Steps", static_cast<int>(i), "Active")) ++active;
        out += "   " + std::to_string(active) + "/" + std::to_string(chart.steps.size())
            + " \xC3\xA9tapes actives";

        std::size_t firing = 0;
        for (std::size_t i = 0; i < chart.actions.size(); ++i)
            if (grafcetFlag(runtime, chart, "Acts", static_cast<int>(i), "Out")) ++firing;
        out += "   " + std::to_string(firing) + "/" + std::to_string(chart.actions.size())
            + " actions en cours";

        if (const auto fired = engineField(runtime, chart, "Debug.TotalTransitionsFired");
            !fired.empty())
            out += "   " + fired + " franchissements";

        // Faults last, where the eye lands after the numbers.
        for (const auto& fault : engineFaults(runtime, chart)) out += "   D\xC3\x89" "FAUT : " + fault;
        return out;
    }


    // ---------------------------------------------------------------------------
    //  1.10, chantier R : le navigateur des instances
    // ---------------------------------------------------------------------------
    std::string chartRunState(const sim::Runtime* runtime, const Chart& chart, int* tone) {
        if (tone) *tone = 0;
        if (!runtime) return {};
        std::string active;
        int shown = 0, count = 0;
        for (std::size_t i = 0; i < chart.steps.size(); ++i) {
            if (!grafcetFlag(runtime, chart, "Steps", static_cast<int>(i), "Active")) continue;
            ++count;
            if (shown < 3) {
                active += (active.empty() ? "" : ", ") + ("X" + std::to_string(chart.steps[i].id));
                ++shown;
            }
        }
        if (count > shown) active += "\xE2\x80\xA6";
        if (!engineFaults(runtime, chart).empty()) {
            if (tone) *tone = 3;
            return "d\xC3\xA9" "faut" + (active.empty() ? std::string{} : " " + active);
        }
        if (truthy(engineField(runtime, chart, "Finished"))) {
            if (tone) *tone = 2;
            return "fini";
        }
        if (tone) *tone = active.empty() ? 0 : 1;
        return active;
    }

    GrafcetInstanceModel::GrafcetInstanceModel(std::vector<Entry> entries, sim::Runtime** runtime)
        : entries_(std::move(entries)), runtime_(runtime) {}

    std::string GrafcetInstanceModel::headerText(std::size_t column) const {
        switch (column) {
        case Name:   return "Grafcet";
        case Counts: return "\xC3\xA9t.\xC2\xB7tr.\xC2\xB7" "act.";
        case State:  return "\xC3\x89tat";
        default:     return {};
        }
    }

    std::string GrafcetInstanceModel::cellText(ui::RowIndex row, std::size_t column) const {
        if (row >= entries_.size()) return {};
        const auto& e = entries_[row];
        switch (column) {
        case Name: return e.chart.name;
        case Counts:
            return std::to_string(e.chart.steps.size()) + "\xC2\xB7" + std::to_string(e.chart.transitions.size())
                 + "\xC2\xB7" + std::to_string(e.chart.actions.size());
        case State: {
            const auto run = chartRunState(*runtime_, e.chart);
            if (!run.empty()) return run;
            if (e.severe) return std::to_string(e.severe) + " en d\xC3\xA9" "faut";
            if (e.remarks) return std::to_string(e.remarks) + " remarque" + (e.remarks > 1 ? "s" : "");
            return *runtime_ ? "arr\xC3\xAAt\xC3\xA9" : "";
        }
        default: return {};
        }
    }

    ui::CellStyle GrafcetInstanceModel::cellStyle(ui::RowIndex row, std::size_t column) const {
        ui::CellStyle style;
        if (row >= entries_.size() || column != State) return style;
        int tone = 0;
        (void)chartRunState(*runtime_, entries_[row].chart, &tone);
        if (tone == 1) style.fg = gfx::Color::rgb(0x7FD08A);
        else if (tone == 2) style.fg = gfx::Color::rgb(0x6FB7E8);
        else if (tone == 3 || entries_[row].severe) style.fg = gfx::Color::rgb(0xE06C6C);
        else if (entries_[row].remarks) style.fg = gfx::Color::rgb(0xE8C46F);
        return style;
    }

    int GrafcetInstanceModel::rowOf(const std::string& instance) const {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].chart.instance == instance) return static_cast<int>(i);
        return -1;
    }

    std::size_t GrafcetInstanceModel::totalSteps() const {
        std::size_t n = 0;
        for (const auto& e : entries_) n += e.chart.steps.size();
        return n;
    }
    std::size_t GrafcetInstanceModel::totalTransitions() const {
        std::size_t n = 0;
        for (const auto& e : entries_) n += e.chart.transitions.size();
        return n;
    }
    std::size_t GrafcetInstanceModel::totalActions() const {
        std::size_t n = 0;
        for (const auto& e : entries_) n += e.chart.actions.size();
        return n;
    }

} // namespace app
