// app/TaskPanes.cpp - les taches et l'ordre d'execution (lot API 4).
#include "TaskPanes.hpp"

#include "../project/CodeIconKeys.hpp"

#include "ApiPanes.hpp"
#include "ApiListKit.hpp"                  // lot API 8 : la recherche (ApiFilterBar)
#include "ConditionsNotice.hpp"            // 1.11 (R111) : les conditions d'activation manquantes
#include "hmi/HmiPanels.hpp"

#include "../domain/ExecutionOrder.hpp"
#include "../project/ApiCommands.hpp"
#include "../project/EditCommands.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/FilterMemory.hpp"  // lot API 8 : les termes surlignes dans la liste

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace app {

using ui::RowIndex;
using PG = ui::PropertyGrid;

namespace {

const gfx::FontId kSmall{13};
constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

std::string thousands(std::size_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string priorityOf(std::string_view name) {
    if (name == "MAST") return "t\xC3\xA2" "che ma\xC3\xAEtre \xC2\xB7 passe apr\xC3\xA8s FAST et les \xC3\xA9v\xC3\xA8nements";
    if (name == "FAST") return "plus haute que MAST";
    if (name.rfind("AUX", 0) == 0) return "plus basse que MAST (le temps qui reste)";
    return {};
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

// =================================================================== Taches ====
class TasksPane::Model final : public ui::ITableModel {
public:
    explicit Model(TasksPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, CType, CPeriod, CWatchdog, CSections, CUnits, CLines, CPriority, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"T\xC3\xA2" "che", "Type", "P\xC3\xA9riode", "Chien de garde", "Sections", "Unit\xC3\xA9s", "Lignes", "Priorit\xC3\xA9"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] const domain::Task* taskOf(RowIndex r) const {
        const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
        if (!p || r >= pane_.rows_.size() || pane_.rows_[r].task >= p->tasks.size()) return nullptr;
        return &p->tasks[pane_.rows_[r].task];
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        const auto* t = taskOf(r);
        if (!t) {
            switch (c) {
                case CName: return row.name;
                case CType: return "p\xC3\xA9riodique";
                case CPriority: return priorityOf(row.name) + " \xC2\xB7 + " + row.name + " pour la cr\xC3\xA9" "er";
                default: return "\xE2\x80\x94";
            }
        }
        switch (c) {
            case CName: return row.name;
            case CType: return t->type == "periodic" ? "p\xC3\xA9riodique" : "cyclique";
            case CPeriod: return t->type == "periodic" ? std::to_string(t->period) + " ms" : std::string("au plus vite");
            case CWatchdog: return std::to_string(t->watchdog) + " ms";
            case CSections: return std::to_string(row.sections);
            case CUnits: return std::to_string(row.units);
            case CLines: return thousands(row.lines);
            default: return priorityOf(row.name);
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (!taskOf(r)) {
            s.fgTone = ui::Tone::Muted;
            if (c == CName) s.icon = ui::Icon::Task, s.iconTone = ui::Tone::Muted;
            return s;
        }
        if (c == CName) {
            s.bold = true;
            s.icon = ui::Icon::Task;
            s.iconTone = ui::Tone::Info;
        } else if (c == CPeriod && cellText(r, c) == "au plus vite") {
            s.fgTone = ui::Tone::Muted;
        } else if (c == CPriority) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(RowIndex r, std::size_t c) const override {
        const auto* t = taskOf(r);
        if (!t || !pane_.hosts_.project || !pane_.hosts_.project()) return false;
        return c == CType || c == CWatchdog || (c == CPeriod && t->type == "periodic");
    }
    [[nodiscard]] std::vector<std::string> cellChoices(RowIndex r, std::size_t c) const override {
        if (c != CType || !taskOf(r)) return {};
        return {"cyclique", "p\xC3\xA9riodique"};
    }
    bool setCellText(RowIndex r, std::size_t c, std::string_view text) override {
        const auto* t = taskOf(r);
        if (!t) return false;
        std::string type = t->type == "periodic" ? "periodic" : "cyclic";
        std::uint32_t period = t->period, watchdog = t->watchdog;
        const auto number = static_cast<std::uint32_t>(std::max(0L, std::strtol(std::string(text).c_str(), nullptr, 10)));
        if (c == CType) {
            type = text.rfind("p", 0) == 0 ? "periodic" : "cyclic";
            if (type == "periodic" && period == 0) period = 20;
        } else if (c == CPeriod) {
            period = number;
        } else if (c == CWatchdog) {
            watchdog = number;
        } else {
            return false;
        }
        pane_.setTask(pane_.rows_[r].task, type, period, watchdog);
        return true;
    }
private:
    TasksPane& pane_;
};

// Ce que la tache execute, dans l'ordre, en poids de lignes (une barre par entree).
class TasksPane::Weights final : public ui::Widget {
public:
    explicit Weights(std::string id) : ui::Widget(std::move(id)) {}
    // Lot API 8 : une recherche n'en montre qu'une partie - `ranks` : le rang de
    // chacune dans la tache, `query` : surlignee, `all` : combien en tout.
    void set(std::string task, std::vector<project::api::Entry> entries, std::vector<std::size_t> ranks = {},
             ui::SearchQuery query = {}, std::size_t all = 0) {
        task_ = std::move(task);
        entries_ = std::move(entries);
        ranks_ = std::move(ranks);
        query_ = std::move(query);
        all_ = std::max(all, entries_.size());
        invalidate();
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        r.fillRect(b, c.windowBg);
        std::size_t total = 0, most = 1;
        for (const auto& e : entries_) {
            total += e.lines;
            most = std::max(most, e.lines);
        }
        // Lot API 8 : une recherche - "3 SUR 21 ENTREES".
        const std::string count = all_ > entries_.size() ? std::to_string(entries_.size()) + " SUR " + std::to_string(all_) : std::to_string(entries_.size());
        const std::string title = "CE QUE " + task_ + " EX\xC3\x89" "CUTE, DANS L'ORDRE  \xC2\xB7  POIDS EN LIGNES DE CODE  \xC2\xB7  "
                                + count + " ENTR\xC3\x89" "ES, " + thousands(total) + " LIGNES";
        r.drawText({b.x + 16.f, b.y + 10.f}, title, kSmall, c.textMuted);
        if (entries_.empty()) {
            r.drawText({b.x + 16.f, b.y + 40.f}, all_ > 0 ? std::string("Rien de ce que la t\xC3\xA2" "che ex\xC3\xA9" "cute ne correspond \xC3\xA0 la recherche.")
                                                            : std::string("Cette t\xC3\xA2" "che n'ex\xC3\xA9" "cute rien encore."),
                       gfx::FontId{15}, c.textMuted);
            return;
        }
        r.pushClip(b);
        const float rowH = 26.f;
        const float nameX = b.x + 72.f, barX = b.x + 330.f, barW = std::max(40.f, b.w - 330.f - 90.f);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            const float y = b.y + 34.f + static_cast<float>(i) * rowH;
            if (y > b.bottom()) break;
            if (i % 2) r.fillRect({b.x + 8.f, y, b.w - 16.f, rowH}, c.rowAltBg);
            // Lot API 8 : son rang dans la tache, meme quand la recherche en cache.
            const std::string rank = std::to_string((i < ranks_.size() ? ranks_[i] : i) + 1);
            r.drawText({b.x + 38.f - r.measure(rank, kSmall).width, y + 5.f}, rank, kSmall, c.textMuted);
            const auto tone = ctx.theme.tone(e.unit ? ui::Tone::Family1 : ui::Tone::Info, c.accent);
            ui::drawIcon(r, e.unit ? ui::Icon::Program : ui::Icon::Section, {b.x + 46.f, y + 5.f, 16.f, 16.f}, tone);
            ui::drawSearchMarks(r, query_, e.name, nameX, y + 4.f, r.lineHeight(gfx::FontId{15}), barX - nameX - 8.f, gfx::FontId{15});
            r.drawText({nameX, y + 4.f}, e.name, gfx::FontId{15}, c.text);
            r.fillRoundedRect({barX, y + 9.f, barW, 8.f}, c.gridLine, 4.f);
            const float w = std::max(3.f, barW * static_cast<float>(e.lines) / static_cast<float>(most));
            r.fillRoundedRect({barX, y + 9.f, w, 8.f}, tone, 4.f);
            const std::string n = thousands(e.lines);
            r.drawText({b.right() - 20.f - r.measure(n, kSmall).width, y + 5.f}, n, kSmall, c.textMuted);
        }
        r.popClip();
    }
private:
    std::string                       task_;
    std::vector<project::api::Entry>  entries_;
    std::vector<std::size_t>          ranks_;    // lot API 8 : le rang de chacune dans la tache
    ui::SearchQuery                   query_;    // lot API 8 : surlignee
    std::size_t                       all_{0};   // lot API 8 : combien en tout
};

TasksPane::TasksPane(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".taches")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    {
        std::vector<ui::TableView::Column> cols(Model::CCount);
        const float widths[] = {150.f, 120.f, 120.f, 130.f, 80.f, 70.f, 80.f, 420.f};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = model_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
            if (i >= Model::CPeriod && i <= Model::CLines) cols[i].align = ui::Align::End;
        }
        table_->setColumns(std::move(cols));
    }
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
        if (syncing_ || rows.empty()) return;
        current_ = static_cast<std::size_t>(rows.front());
        refreshProperties();
    });
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
    weights_ = &static_cast<Weights&>(addChild(std::make_unique<Weights>(this->id() + ".poids")));
    // ---- Lot API 8 : chercher une tache et ce qu'elle execute ----
    //  "Gestion" : les taches qui executent une entree Gestion, et dans la liste
    //  ces entrees-la ; "MAST" : MAST et tout ce qu'elle execute. Surlignee,
    //  "3 sur 21" (les entrees de la tache choisie), retenue d'une seance a l'autre.
    filters_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(
        this->id() + ".filtres", "Rechercher : t\xC3\xA2" "che, section, unit\xC3\xA9\xE2\x80\xA6")));
    links_ += filters_->changed->connect([this] { applySearch(); });
    filters_->recall();
    // ---- fin Lot API 8 ----
}

TasksPane::~TasksPane() = default;

// ---- Lot API 8 : chercher une tache et ce qu'elle execute ----
std::vector<std::string> TasksPane::taskTexts(std::size_t row) const {
    std::vector<std::string> texts;
    if (row >= rows_.size()) return texts;
    for (const auto c : {Model::CName, Model::CType, Model::CPriority}) texts.push_back(model_->cellText(static_cast<RowIndex>(row), c));
    return texts;
}

void TasksPane::applySearch() {
    const ui::SearchQuery query(filters_ ? filters_->search() : std::string{});
    ui::FilterChain chain;
    if (!query.empty()) {
        std::vector<bool> keep(rows_.size(), false);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            std::vector<std::vector<std::string>> entries;
            for (const auto& e : rows_[i].entries) entries.push_back({e.name, e.unit ? "unit\xC3\xA9" : "section"});
            keep[i] = apikit::searchTwoLevels(query, taskTexts(i), entries).parent;
        }
        chain.addPredicate("recherche", [keep](RowIndex r) { return r < keep.size() && keep[r]; });
    }
    table_->setFilter(std::move(chain));
    table_->setHighlight(query.text());
    // La tache choisie cachee : la premiere montree.
    bool shown = false;
    for (std::size_t i = 0; i < table_->visibleRowCount() && !shown; ++i) shown = table_->viewRow(i) == current_;
    if (!shown && table_->visibleRowCount() > 0) {
        current_ = static_cast<std::size_t>(table_->viewRow(0));
        syncing_ = true;
        table_->selectModelRows({static_cast<RowIndex>(current_)}, false);
        syncing_ = false;
    }
    refreshProperties();       // la liste de ce qu'elle execute, et le compte
}
// ---- fin Lot API 8 ----

void TasksPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void TasksPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAddFast, HmiGlyph::Plus, "Ajouter la t\xC3\xA2" "che FAST (p\xC3\xA9riodique, 5 ms)", "FAST");
    t.add(AAddAux, HmiGlyph::Plus, "Ajouter une t\xC3\xA2" "che AUX (p\xC3\xA9riodique, 100 ms)", "AUX");
    t.add(ARemove, HmiGlyph::Delete, "Supprimer la t\xC3\xA2" "che choisie (vide seulement ; pas MAST)", "Supprimer");
    t.separator();
    t.add(AOrder, HmiGlyph::List, "L'ordre d'ex\xC3\xA9" "cution de la t\xC3\xA2" "che", "Ordre d'ex\xC3\xA9" "cution");
    t.add(ASimulation, HmiGlyph::Play, "L'\xC3\xA9" "cran de simulation : mesurer le cycle (F9)", "Mesurer en simulation");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    t.setEnabledWhen(AAddFast, [this, editable] {
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        if (!editable() || !p) return false;
        for (const auto& task : p->tasks) if (p->strings.text(task.name) == "FAST") return false;
        return true;
    });
    t.setEnabledWhen(AAddAux, editable);
    t.setEnabledWhen(ARemove, [this, editable] {
        if (!editable() || current_ >= rows_.size() || rows_[current_].task == kNpos) return false;
        return rows_[current_].name != "MAST" && rows_[current_].sections == 0 && rows_[current_].units == 0;
    });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    frame.setHint("Les t\xC3\xA2" "ches viennent de config/tasks.txt ; une modification passe par l'historique (Ctrl+Z).");
}

std::string TasksPane::currentTaskName() const {
    return current_ < rows_.size() ? rows_[current_].name : std::string{};
}

void TasksPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto keep = currentTaskName();
    rows_.clear();
    if (p) {
        for (std::size_t i = 0; i < p->tasks.size(); ++i) {
            Row r;
            r.name = std::string(p->strings.text(p->tasks[i].name));
            r.task = i;
            r.entries = project::api::entriesOf(*p, r.name);      // lot API 8 : gardees (la recherche)
            for (const auto& e : r.entries) {
                (e.unit ? r.units : r.sections) += 1u;
                r.lines += e.lines;
            }
            rows_.push_back(std::move(r));
        }
        for (const char* ghost : {"FAST", "AUX0"}) {
            bool present = false;
            for (const auto& r : rows_) present = present || r.name == ghost;
            if (!present) {
                Row r;
                r.name = ghost;
                rows_.push_back(std::move(r));
            }
        }
    }
    current_ = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].name == keep) current_ = i;
    model_->modelReset->emit();
    syncing_ = true;
    if (!rows_.empty()) table_->selectModelRows({static_cast<RowIndex>(current_)}, false);
    syncing_ = false;
    applySearch();       // lot API 8 : la recherche sur les taches relues (puis refreshProperties)
}

bool TasksPane::selectTask(std::string_view name) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].name == name) {
            // Lot API 8 : une recherche qui la cache s'efface (elle est demandee).
            bool shown = false;
            for (std::size_t k = 0; k < table_->visibleRowCount() && !shown; ++k) shown = table_->viewRow(k) == i;
            if (!shown && filters_ && !filters_->search().empty()) filters_->setSearch("");
            current_ = i;
            syncing_ = true;
            table_->selectModelRows({static_cast<RowIndex>(i)}, false);
            syncing_ = false;
            refreshProperties();
            return true;
        }
    return false;
}

void TasksPane::setTask(std::size_t task, std::string type, std::uint32_t period, std::uint32_t watchdog) {
    auto doc = hosts_.project ? hosts_.project() : nullptr;
    if (!doc || task >= doc->tasks.size() || !hosts_.apply) return;
    const auto& t = doc->tasks[task];
    const std::string was = t.type == "periodic" ? "periodic" : "cyclic";
    if (was == type && (type != "periodic" || t.period == period) && t.watchdog == watchdog) return;   // rien ne change
    hosts_.apply(std::make_unique<project::SetTaskCommand>(doc, task, std::move(type), period, watchdog));
}

void TasksPane::refreshProperties() {
    std::vector<PG::Category> cats;
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const bool editable = hosts_.project && hosts_.project() != nullptr;
    if (!p || current_ >= rows_.size()) {
        props_->setCategories({});
        weights_->set("", {});
        shownEntries_ = 0;                                   // lot API 8
        if (filters_) filters_->setCount(0, 0);
        return;
    }
    const auto& row = rows_[current_];
    PG::Category c;
    c.name = row.name;
    if (row.task >= p->tasks.size()) {
        c.properties.push_back({"\xC3\x89tat", "pas encore dans le projet", PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"La cr\xC3\xA9" "er", "\xC2\xAB " + std::string(row.name == "FAST" ? "FAST" : "AUX") + " \xC2\xBB dans la barre",
                                PG::ValueType::ReadOnly, {}, {}, nullptr});
        cats.push_back(std::move(c));
        PG::Category k;
        k.name = "Ce qu'il faut savoir";
        k.properties.push_back({"Priorit\xC3\xA9", priorityOf(row.name), PG::ValueType::ReadOnly,
                                row.name == "FAST" ? "FAST est p\xC3\xA9riodique (1 \xC3\xA0 255 ms) : pour ce qui doit r\xC3\xA9pondre vite, un comptage, une s\xC3\xA9" "curit\xC3\xA9."
                                                   : "Une t\xC3\xA2" "che AUX tourne dans le temps que MAST laisse : pour ce qui peut attendre.",
                                {}, nullptr});
        cats.push_back(std::move(k));
        props_->setCategories(std::move(cats));
        weights_->set(row.name, {});
        shownEntries_ = 0;                                   // lot API 8
        if (filters_) filters_->setCount(0, 0);
        return;
    }
    const auto& t = p->tasks[row.task];
    const auto index = row.task;
    const bool periodic = t.type == "periodic";
    c.properties.push_back({"Nom", row.name, PG::ValueType::ReadOnly, {}, {}, nullptr});
    c.properties.push_back({"Type", periodic ? "p\xC3\xA9riodique" : "cyclique", editable ? PG::ValueType::Enum : PG::ValueType::ReadOnly,
                            "Cyclique : un cycle commence d\xC3\xA8s que le pr\xC3\xA9" "c\xC3\xA9" "dent est fini. P\xC3\xA9riodique : toutes les N ms.",
                            {"cyclique", "p\xC3\xA9riodique"},
                            editable ? std::function<bool(std::string_view)>([this, index](std::string_view v) {
                                const auto doc = hosts_.project();
                                if (!doc || index >= doc->tasks.size()) return false;
                                const auto& task = doc->tasks[index];
                                const std::string type = v.rfind("p", 0) == 0 ? "periodic" : "cyclic";
                                setTask(index, type, task.period ? task.period : 20u, task.watchdog);
                                return true;
                            }) : nullptr});
    c.properties.push_back({"P\xC3\xA9riode (ms)", periodic ? std::to_string(t.period) : std::string("\xE2\x80\x94 (au plus vite)"),
                            periodic && editable ? PG::ValueType::Integer : PG::ValueType::ReadOnly, "De 1 \xC3\xA0 255 ms (p\xC3\xA9riodique seulement).", {},
                            periodic && editable ? std::function<bool(std::string_view)>([this, index](std::string_view v) {
                                const auto doc = hosts_.project();
                                if (!doc || index >= doc->tasks.size()) return false;
                                const auto n = static_cast<std::uint32_t>(std::max(0L, std::strtol(std::string(v).c_str(), nullptr, 10)));
                                setTask(index, "periodic", n, doc->tasks[index].watchdog);
                                return true;
                            }) : nullptr});
    c.properties.push_back({"Chien de garde (ms)", std::to_string(t.watchdog), editable ? PG::ValueType::Integer : PG::ValueType::ReadOnly,
                            "L'automate s'arr\xC3\xAAte si un cycle d\xC3\xA9passe cette dur\xC3\xA9" "e (10 \xC3\xA0 1500 ms).", {},
                            editable ? std::function<bool(std::string_view)>([this, index](std::string_view v) {
                                const auto doc = hosts_.project();
                                if (!doc || index >= doc->tasks.size()) return false;
                                const auto& task = doc->tasks[index];
                                const auto n = static_cast<std::uint32_t>(std::max(0L, std::strtol(std::string(v).c_str(), nullptr, 10)));
                                setTask(index, task.type == "periodic" ? "periodic" : "cyclic", task.period, n);
                                return true;
                            }) : nullptr});
    c.properties.push_back({"Sections", std::to_string(row.sections), PG::ValueType::ReadOnly, {}, {}, nullptr});
    c.properties.push_back({"Unit\xC3\xA9s de programme", std::to_string(row.units), PG::ValueType::ReadOnly, {}, {}, nullptr});
    c.properties.push_back({"Lignes de code", thousands(row.lines), PG::ValueType::ReadOnly, {}, {}, nullptr});
    cats.push_back(std::move(c));
    PG::Category k;
    k.name = "Ce qu'il faut savoir";
    k.properties.push_back({periodic ? "P\xC3\xA9riodique" : "Cyclique",
                            periodic ? "un cycle toutes les " + std::to_string(t.period) + " ms ; le chien de garde arr\xC3\xAAte l'automate si un cycle d\xC3\xA9passe "
                                           + std::to_string(t.watchdog) + " ms."
                                     : "un cycle commence d\xC3\xA8s que le pr\xC3\xA9" "c\xC3\xA9" "dent est fini ; le chien de garde arr\xC3\xAAte l'automate si un cycle d\xC3\xA9passe "
                                           + std::to_string(t.watchdog) + " ms.",
                            PG::ValueType::ReadOnly, {}, {}, nullptr});
    cats.push_back(std::move(k));
    props_->setCategories(std::move(cats));
    // Lot API 8 : la recherche - les entrees qu'elle garde, a leur rang, et le compte.
    auto entries = project::api::entriesOf(*p, row.name);
    const std::size_t all = entries.size();
    const ui::SearchQuery query(filters_ ? filters_->search() : std::string{});
    std::vector<std::vector<std::string>> texts;
    for (const auto& e : entries) texts.push_back({e.name, e.unit ? "unit\xC3\xA9" : "section"});
    const auto kept = apikit::searchTwoLevels(query, taskTexts(current_), texts);
    std::vector<project::api::Entry> shown;
    std::vector<std::size_t> ranks;
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (kept.children[i]) {
            ranks.push_back(i);
            shown.push_back(std::move(entries[i]));
        }
    shownEntries_ = shown.size();
    if (filters_) filters_->setCount(shown.size(), all);
    weights_->set(row.name, std::move(shown), std::move(ranks), query, all);
}

void TasksPane::runAction(int action) {
    auto doc = hosts_.project ? hosts_.project() : nullptr;
    switch (action) {
        case AAddFast:
        case AAddAux: {
            if (!doc || !hosts_.apply) return;
            std::string name = "FAST";
            if (action == AAddAux) {
                name.clear();
                for (int i = 0; i < 4 && name.empty(); ++i) {
                    const std::string candidate = "AUX" + std::to_string(i);
                    bool used = false;
                    for (const auto& t : doc->tasks) used = used || doc->strings.text(t.name) == candidate;
                    if (!used) name = candidate;
                }
                if (name.empty()) {
                    if (hosts_.status) hosts_.status("Les quatre t\xC3\xA2" "ches AUX existent d\xC3\xA9j\xC3\xA0 (AUX0 \xC3\xA0 AUX3).");
                    return;
                }
            }
            hosts_.apply(std::make_unique<project::AddTaskCommand>(doc, name));
            selectTask(name);
            return;
        }
        case ARemove:
            if (doc && current_ < rows_.size() && rows_[current_].task < doc->tasks.size())
                hosts_.apply(std::make_unique<project::RemoveTaskCommand>(doc, rows_[current_].task));
            return;
        case AOrder: if (hosts_.request) hosts_.request("ordre"); return;
        case ASimulation: if (hosts_.request) hosts_.request("simulation"); return;
        default: return;
    }
}

void TasksPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.24f, 260.f, 400.f);
    // Lot API 8 : la barre de recherche au-dessus de la table.
    const float barH = filters_ ? 44.f : 0.f;
    if (filters_) filters_->setBounds({b.x, b.y, b.w - rightW - 1.f, barH});
    const float top = b.y + barH, height = std::max(0.f, b.h - barH);
    const float tableH = std::min(height * 0.4f, 34.f + 26.f * static_cast<float>(std::max<std::size_t>(rows_.size(), 3u)) + 8.f);
    table_->setBounds({b.x, top, b.w - rightW - 1.f, tableH});
    weights_->setBounds({b.x, top + tableH + 1.f, b.w - rightW - 1.f, std::max(0.f, height - tableH - 1.f)});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
}

void TasksPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

// ======================================================= l'ordre d'execution ====
class ExecutionOrderPane::Model final : public ui::ITableModel {
public:
    explicit Model(ExecutionOrderPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CRank, CName, CLines, CWrites, CReads, CKind, CLate, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.entries_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Rang", "Entr\xC3\xA9" "e", "Lignes", "\xC3\x89" "crit", "Lit", "Genre", "Lecture avant l'\xC3\xA9" "criture"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.entries_.size()) return {};
        const auto& e = pane_.entries_[r];
        switch (c) {
            case CRank: return std::to_string(r + 1);
            case CName: return e.unit ? e.name + "  \xC2\xB7 unit\xC3\xA9, " + apikit::plural(e.sections, "section", "sections") : e.name;
            case CLines: return thousands(e.lines);
            case CWrites: return r < pane_.access_.size() ? std::to_string(pane_.access_[r].writes.size()) : std::string{};
            case CReads: return r < pane_.access_.size() ? std::to_string(pane_.access_[r].reads.size()) : std::string{};
            case CKind: {
                if (e.unit) return "unit\xC3\xA9 de programme";
                const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
                if (p && e.section < p->sections.size()) return "section " + std::string(domain::toString(p->sections[e.section].language));
                return "section";
            }
            default: {
                const auto* late = pane_.lateOf(r);
                if (!late) return {};
                return "lit " + late->variable + " avant l'\xC3\xA9" "criture (rang " + std::to_string(late->writerRank) + ", " + late->writer + ")";
            }
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.entries_.size()) return s;
        const auto& e = pane_.entries_[r];
        if (c == CName) {
            s.icon = e.unit ? ui::Icon::Program : ui::Icon::Section;
            s.iconTone = e.unit ? ui::Tone::Family1 : ui::Tone::Info;
            // 1.8.0 : l'icone au choix de la section, ou de l'unite.
            if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr) {
                const int ic = e.unit ? project::codeicons::pouIcon(*p, e.unitIndex) : project::codeicons::sectionIcon(*p, e.section);
                if (ic >= 0) {
                    s.icon = ui::codeIcon(ic);
                    s.iconColor = ui::codeIconColor(ic);
                    s.iconTone = ui::Tone::None;
                }
            }
            if (pane_.lateOf(r)) s.badge = "!", s.badgeTone = ui::Tone::Warning;
        } else if (c == CRank || c == CKind) {
            s.fgTone = ui::Tone::Muted;
        } else if (c == CLate) {
            s.fgTone = ui::Tone::Warning;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool canDropRows(const std::vector<RowIndex>& from, RowIndex to, ui::TreeView::DropWhere where) const override {
        return from.size() == 1 && where != ui::TreeView::DropWhere::Into && to != from.front() && pane_.hosts_.project && pane_.hosts_.project();
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.entries_.size()) return {};
        return pane_.entries_[r].unit ? "Une unit\xC3\xA9 de programme tourne en bloc, avec ses sections, \xC3\xA0 son rang."
                                      : "Glisser la ligne la d\xC3\xA9place ; Ctrl+Z la remet.";
    }
private:
    ExecutionOrderPane& pane_;
};

ExecutionOrderPane::ExecutionOrderPane(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".entrees")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    {
        std::vector<ui::TableView::Column> cols(Model::CCount);
        const float widths[] = {70.f, 340.f, 80.f, 70.f, 64.f, 170.f, 460.f};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = model_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
            if (i == Model::CRank || (i >= Model::CLines && i <= Model::CReads)) cols[i].align = ui::Align::End;
        }
        table_->setColumns(std::move(cols));
    }
    table_->setSelectionMode(ui::SelectionMode::Single);
    table_->setRowDragEnabled(true);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (syncing_) return;
        refreshProperties();
        updateHint();
    });
    links_ += table_->rowsDropped->connect([this](const std::vector<RowIndex>& from, RowIndex to, ui::TableView::DropWhere where) {
        if (from.size() != 1) return;
        const auto src = static_cast<std::size_t>(from.front());
        std::size_t dest = to == ui::TableView::kNoRow ? entries_.size() : static_cast<std::size_t>(to) + (where == ui::TableView::DropWhere::After ? 1u : 0u);
        if (dest > src) --dest;
        (void)moveEntry(src, std::min(dest, entries_.empty() ? 0u : entries_.size() - 1));
    });
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
}

ExecutionOrderPane::~ExecutionOrderPane() = default;

void ExecutionOrderPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void ExecutionOrderPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AUp, HmiGlyph::Up, "Monter l'entr\xC3\xA9" "e choisie d'un rang", "Monter");
    t.add(ADown, HmiGlyph::Down, "Descendre l'entr\xC3\xA9" "e choisie d'un rang", "Descendre");
    t.add(AMoveTo, HmiGlyph::List, "D\xC3\xA9placer l'entr\xC3\xA9" "e choisie vers un rang", "D\xC3\xA9placer vers\xE2\x80\xA6");
    t.separator();
    t.add(ASort, HmiGlyph::Code, "Ranger les sections g\xC3\xA9n\xC3\xA9r\xC3\xA9" "es (la macro RangerSections)", "Ranger (RangerSections)");
    t.add(ACheck, HmiGlyph::Check, "V\xC3\xA9rifier l'ordre : les variables lues avant d'\xC3\xAAtre \xC3\xA9" "crites", "V\xC3\xA9rifier l'ordre");
    t.add(APlaceAfter, HmiGlyph::Down, "Placer l'entr\xC3\xA9" "e choisie juste apr\xC3\xA8s celle qui \xC3\xA9" "crit ce qu'elle lit", "Placer apr\xC3\xA8s\xE2\x80\xA6");
    t.separator();
    t.add(AOpen, HmiGlyph::Code, "Ouvrir la section (ou la premi\xC3\xA8re de l'unit\xC3\xA9)", "Ouvrir");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    t.setEnabledWhen(AUp, [this, editable] { return editable() && selected() != kNpos && selected() > 0; });
    t.setEnabledWhen(ADown, [this, editable] { return editable() && selected() != kNpos && selected() + 1 < entries_.size(); });
    t.setEnabledWhen(AMoveTo, [this, editable] { return editable() && selected() != kNpos; });
    t.setEnabledWhen(APlaceAfter, [this, editable] { return editable() && selected() != kNpos && lateOf(selected()) != nullptr; });
    t.setEnabledWhen(AOpen, [this] { return selected() != kNpos; });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    updateHint();
}

std::size_t ExecutionOrderPane::selected() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= entries_.size() ? kNpos : static_cast<std::size_t>(rows.front());
}

const project::api::LateRead* ExecutionOrderPane::lateOf(std::size_t entry) const {
    for (const auto& l : late_)
        if (l.readerRank == entry + 1) return &l;
    return nullptr;
}

domain::Index ExecutionOrderPane::firstSectionOf(std::size_t entry) const {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p || entry >= entries_.size()) return domain::kNoIndex;
    const auto& e = entries_[entry];
    if (!e.unit) return e.section;
    for (const auto& step : domain::executionOrder(*p, task_))
        if (step.fromProgramUnit && step.unit == e.unitIndex) return step.section;
    return domain::kNoIndex;
}

void ExecutionOrderPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    std::string keep = pendingSelect_;
    pendingSelect_.clear();
    if (keep.empty())
        if (const auto s = selected(); s != kNpos) keep = entries_[s].name;
    entries_.clear();
    access_.clear();
    late_.clear();
    lines_ = 0;
    if (p) {
        task_ = p->tasks.empty() ? std::string("MAST") : std::string(p->strings.text(p->tasks.front().name));
        entries_ = project::api::entriesOf(*p, task_);
        for (const auto& e : entries_) {
            access_.push_back(project::api::accessOf(*p, e));
            lines_ += e.lines;
        }
        late_ = project::api::lateReads(*p, task_);
    }
    model_->modelReset->emit();
    syncing_ = true;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == keep) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
    syncing_ = false;
    refreshProperties();
    updateHint();
    invalidate();
}

bool ExecutionOrderPane::selectEntry(std::string_view name) {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (lower(entries_[i].name) == lower(name)) {
            table_->selectModelRows({static_cast<RowIndex>(i)}, true);
            return true;
        }
    return false;
}

bool ExecutionOrderPane::moveEntry(std::size_t from, std::size_t to) {
    auto doc = hosts_.project ? hosts_.project() : nullptr;
    if (!doc || !hosts_.apply || from >= entries_.size() || to >= entries_.size() || from == to) return false;
    const auto moved = firstSectionOf(from);
    if (moved == domain::kNoIndex) return false;
    // L'entree qui la suivra : celle qui est au rang `to` une fois la deplacee retiree.
    std::vector<std::size_t> rest;
    for (std::size_t i = 0; i < entries_.size(); ++i) if (i != from) rest.push_back(i);
    const domain::Index before = to < rest.size() ? firstSectionOf(rest[to]) : domain::kNoIndex;
    pendingSelect_ = entries_[from].name;
    hosts_.apply(std::make_unique<project::ReorderSectionCommand>(doc, moved, project::ReorderSectionCommand::Before{before}));
    return true;
}

void ExecutionOrderPane::runAction(int action) {
    const auto s = selected();
    const auto status = [this](const std::string& m) { if (hosts_.status) hosts_.status(m); };
    switch (action) {
        case AUp: if (s != kNpos && s > 0) moveEntry(s, s - 1); return;
        case ADown: if (s != kNpos && s + 1 < entries_.size()) moveEntry(s, s + 1); return;
        case AMoveTo: {
            if (s == kNpos || !hosts_.ask) return;
            const auto n = entries_.size();
            hosts_.ask("D\xC3\xA9placer " + entries_[s].name, "Le rang o\xC3\xB9 elle doit finir, de 1 \xC3\xA0 " + std::to_string(n) + " (aujourd'hui : " + std::to_string(s + 1) + ").",
                       std::to_string(s + 1), [this, s, n, status](const std::string& text) {
                           const long rank = std::strtol(text.c_str(), nullptr, 10);
                           if (rank < 1 || rank > static_cast<long>(n)) {
                               status("Rang refus\xC3\xA9 : de 1 \xC3\xA0 " + std::to_string(n) + ".");
                               return;
                           }
                           (void)moveEntry(s, static_cast<std::size_t>(rank - 1));
                       });
            return;
        }
        case ASort: if (hosts_.request) hosts_.request("macro:RangerSections"); return;
        case ACheck: {
            checked_ = true;
            // 1.11 (R111, decisions 6 et 15) : un projet importe avant la 1.8.0 dont
            // les sections de tache ont perdu leurs conditions - le message de
            // l'avis de la cloche, d'abord (il dit le remede).
            const auto missing = conditions::noticeText();
            const std::string first = missing.empty() ? std::string{} : missing + " ";
            if (late_.empty()) {
                status(first + "L'ordre est bon : aucune variable n'est lue avant d'\xC3\xAAtre \xC3\xA9" "crite dans " + task_ + ".");
            } else {
                table_->selectModelRows({static_cast<RowIndex>(late_.front().readerRank - 1)}, true);
                status(first + std::to_string(late_.size()) + (late_.size() == 1 ? " lecture avant l'\xC3\xA9" "criture" : " lectures avant l'\xC3\xA9" "criture")
                       + " : au premier cycle, l'entr\xC3\xA9" "e lit la valeur initiale, ensuite celle du cycle d'avant. \xC2\xAB Placer apr\xC3\xA8s \xC2\xBB la range.");
            }
            updateHint();
            return;
        }
        case APlaceAfter: {
            const auto* late = s == kNpos ? nullptr : lateOf(s);
            if (!late || late->writerRank == 0) return;
            // Juste apres l'ecrivain : son rang (1..n) une fois le lecteur retire.
            const std::size_t writer = late->writerRank - 1;
            const std::size_t to = writer > s ? writer : writer + 1;
            (void)moveEntry(s, std::min(to, entries_.size() - 1));
            return;
        }
        case AOpen:
            if (s != kNpos && hosts_.request) {
                const auto section = firstSectionOf(s);
                if (section != domain::kNoIndex) hosts_.request("section:" + std::to_string(section));
            }
            return;
        default: return;
    }
}

void ExecutionOrderPane::refreshProperties() {
    std::vector<PG::Category> cats;
    const auto s = selected();
    if (s == kNpos) {
        PG::Category c;
        c.name = task_ + " \xC2\xB7 " + std::to_string(entries_.size()) + " entr\xC3\xA9" "es";
        c.properties.push_back({"Lignes", thousands(lines_), PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"Lectures avant l'\xC3\xA9" "criture", std::to_string(late_.size()), PG::ValueType::ReadOnly,
                                "Une variable lue par une entr\xC3\xA9" "e et \xC3\xA9" "crite plus loin : au premier cycle, la lecture voit la valeur initiale.", {}, nullptr});
        c.properties.push_back({"Choisir une entr\xC3\xA9" "e", "ce qu'elle \xC3\xA9" "crit, ce qu'elle lit", PG::ValueType::ReadOnly, {}, {}, nullptr});
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const auto& e = entries_[s];
    PG::Category c;
    c.name = e.name;
    c.properties.push_back({"Rang", std::to_string(s + 1) + " sur " + std::to_string(entries_.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
    c.properties.push_back({"Genre", e.unit ? "unit\xC3\xA9 de programme (" + apikit::plural(e.sections, "section", "sections") + ", en bloc)" : std::string("section"),
                            PG::ValueType::ReadOnly, {}, {}, nullptr});
    c.properties.push_back({"Lignes", thousands(e.lines), PG::ValueType::ReadOnly, {}, {}, nullptr});
    cats.push_back(std::move(c));
    if (s < access_.size()) {
        const auto* late = lateOf(s);
        PG::Category w;
        w.name = "\xC3\x89" "crit  " + std::to_string(access_[s].writes.size());
        for (std::size_t i = 0; i < access_[s].writes.size() && i < 40; ++i)
            w.properties.push_back({"", access_[s].writes[i], PG::ValueType::ReadOnly, {}, {}, nullptr});
        cats.push_back(std::move(w));
        PG::Category rd;
        rd.name = "Lit  " + std::to_string(access_[s].reads.size());
        for (std::size_t i = 0; i < access_[s].reads.size() && i < 60; ++i) {
            const auto& v = access_[s].reads[i];
            const bool isLate = late && late->variable == v;
            rd.properties.push_back({isLate ? "avant l'\xC3\xA9" "criture" : "", v + (isLate ? "  (\xC3\xA9" "crite au rang " + std::to_string(late->writerRank) + ")" : std::string{}),
                                     PG::ValueType::ReadOnly, {}, {}, nullptr});
        }
        cats.push_back(std::move(rd));
        if (late) {
            PG::Category k;
            k.name = "Lu avant l'\xC3\xA9" "criture";
            k.properties.push_back({late->variable, "\xC3\xA9" "crite par " + late->writer + " (rang " + std::to_string(late->writerRank) + ")", PG::ValueType::ReadOnly,
                                    "Au premier cycle, " + e.name + " lit sa valeur initiale ; ensuite, celle du cycle pr\xC3\xA9" "c\xC3\xA9" "dent. Voulu ? Sinon : "
                                    "\xC2\xAB Placer apr\xC3\xA8s \xC2\xBB la range juste apr\xC3\xA8s " + late->writer + ".",
                                    {}, nullptr});
            cats.push_back(std::move(k));
        }
    }
    props_->setCategories(std::move(cats));
    if (frame_) {
        const auto* late = lateOf(s);
        frame_->tools().setText(APlaceAfter, late ? "Placer " + e.name + " juste apr\xC3\xA8s " + late->writer : std::string("Placer l'entr\xC3\xA9" "e juste apr\xC3\xA8s celle qui \xC3\xA9" "crit ce qu'elle lit"),
                                late ? "Placer apr\xC3\xA8s " + late->writer : std::string("Placer apr\xC3\xA8s\xE2\x80\xA6"));
    }
}

void ExecutionOrderPane::updateHint() {
    if (!frame_) return;
    std::string text = task_ + " \xC2\xB7 " + apikit::plural(entries_.size(), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + " \xC2\xB7 "
                     + thousands(lines_) + (lines_ == 1 ? " ligne \xC2\xB7 " : " lignes \xC2\xB7 ");
    ui::Tone tone = ui::Tone::None;
    if (!late_.empty()) {
        text += std::to_string(late_.size()) + (late_.size() == 1 ? " lecture avant l'\xC3\xA9" "criture" : " lectures avant l'\xC3\xA9" "criture")
              + " (colonne de droite) \xC2\xB7 glisser une entr\xC3\xA9" "e la d\xC3\xA9place ; Ctrl+Z la remet.";
        tone = checked_ ? ui::Tone::Warning : ui::Tone::None;
    } else {
        text += "aucune lecture avant l'\xC3\xA9" "criture \xC2\xB7 glisser une entr\xC3\xA9" "e la d\xC3\xA9place ; Ctrl+Z la remet.";
        tone = checked_ ? ui::Tone::Ok : ui::Tone::None;
    }
    // 1.11 (R111) : apres « Verifier l'ordre », les conditions d'activation
    // manquantes passent devant (tant que l'avis de la cloche est pose), en
    // court avec le remede : le message entier est dans la barre d'etat et la cloche.
    if (checked_) {
        if (const auto missing = conditions::shortText(); !missing.empty()) {
            text = missing + " \xC2\xB7 " + text;
            tone = ui::Tone::Warning;
        }
    }
    if (text != frame_->hint()) frame_->setHint(text, tone);
}

void ExecutionOrderPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.25f, 260.f, 420.f);
    table_->setBounds({b.x, b.y, b.w - rightW - 1.f, b.h});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
}

void ExecutionOrderPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

} // namespace app
