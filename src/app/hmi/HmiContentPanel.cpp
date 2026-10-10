#include "HmiContentPanel.hpp"
#include "../../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate

#include "HmiAssetPanes.hpp"
#include "HmiAssist.hpp"
#include "HmiIcons.hpp"
#include "HmiPainter.hpp"
#include "../../hmi/HmiMarkers.hpp"   // 1.11 (REP-1) : la recette sans les $ de ses reperes
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiControls.hpp"
#include "../../hmi/HmiWidgets.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : les champs a expression, partout pareils

#include <algorithm>
#include <cstdlib>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;

namespace {

enum Act : int { AAdd = 1, ARemove, AUp, ADown, AAddColumn, ARemoveColumn };

class Rows final : public ui::ITableModel {
public:
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, ui::Icon icon)
        : headers_(std::move(headers)), rows_(std::move(rows)), icon_(icon) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        if (c == 0) return std::to_string(r + 1);
        return c - 1 < rows_[r].size() ? rows_[r][c - 1] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t c) const override {
        ui::CellStyle s;
        if (c == 1) s.icon = icon_;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    ui::Icon                              icon_;
};

// ------------------------------------------------------------- un tableau ---
struct TableData {
    std::vector<std::string>              headers, widths;
    std::vector<std::vector<std::string>> rows;
    [[nodiscard]] std::size_t columns() const { return headers.size(); }
};

TableData readTable(const hmi::Object& o) {
    TableData t;
    t.headers = hmi::splitSemicolons(o.text("columns"));
    t.widths = hmi::splitSemicolons(o.text("widths"));
    t.rows = hmi::parseCells(o.text("cells"));
    // Pas encore de cases : les lignes vides que dessine le tableau.
    if (t.rows.empty() && o.text("cells").empty()) {
        const int n = std::clamp(static_cast<int>(o.number("rows", 0)), 0, 200);
        t.rows.assign(static_cast<std::size_t>(n), {});
    }
    std::size_t n = t.headers.size();
    for (const auto& r : t.rows) n = std::max(n, r.size());
    n = std::max<std::size_t>(1, n);
    while (t.headers.size() < n) t.headers.push_back("Colonne " + std::to_string(t.headers.size() + 1));
    for (auto& r : t.rows) r.resize(n);
    if (!t.widths.empty()) t.widths.resize(n, "1");
    return t;
}

void writeTable(hmi::Object& o, const TableData& t) {
    o.set("columns", hmi::joinSemicolons(t.headers));
    o.set("cells", hmi::formatCells(t.rows));
    o.set("widths", hmi::joinSemicolons(t.widths));
    o.setNumber("rows", static_cast<double>(t.rows.size()));
}

std::string rowSummary(const std::vector<std::string>& cells) {
    std::string s;
    for (std::size_t k = 0; k < cells.size(); ++k) s += (k ? "  |  " : "") + (cells[k].empty() ? std::string("-") : cells[k]);
    return s;
}

// --------------------------------------------------------------- des plumes -
struct Pens {
    std::vector<std::string> exprs, names, colors;
};

Pens readPens(const hmi::Object& o) {
    Pens p;
    p.exprs = hmi::splitSemicolons(o.text("variables"));
    p.names = hmi::splitSemicolons(o.text("names"));
    p.colors = hmi::splitSemicolons(o.text("colors"));
    p.names.resize(p.exprs.size());
    const auto& palette = hmiTrendPalette();
    while (p.colors.size() < p.exprs.size()) p.colors.push_back(palette[p.colors.size() % palette.size()]);
    return p;
}

void writePens(hmi::Object& o, Pens p) {
    o.set("variables", hmi::joinSemicolons(p.exprs));
    const bool named = std::any_of(p.names.begin(), p.names.end(), [](const std::string& n) { return !n.empty(); });
    o.set("names", named ? hmi::joinSemicolons(p.names) : std::string{});
    // Les couleurs des plumes, puis celles qui attendent les plumes suivantes.
    const auto& palette = hmiTrendPalette();
    if (p.colors.size() < p.exprs.size() + 1) p.colors.push_back(palette[p.exprs.size() % palette.size()]);
    o.set("colors", hmi::joinSemicolons(p.colors));
}

std::string imagesText(const std::vector<std::string>& images) {
    std::string s;
    for (std::size_t i = 0; i < images.size(); ++i) s += (i ? ", " : "") + images[i];
    return s;
}

// La derniere suite de chiffres d'un nom : "En-tete 3" -> 3 ; "2 - Valeur" -> 2.
int numberIn(std::string_view s, bool last) {
    int value = -1;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] >= '0' && s[i] <= '9') {
            int v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
            value = v;
            if (!last) return value;
        } else {
            ++i;
        }
    }
    return value;
}

// Lot 11 : le nom d'un element de la liste, selon l'objet (une plume, une barre...).
std::string penWord(const hmi::Object* o) {
    if (!o) return "Plume";
    switch (o->kind) {
        case hmi::Kind::BarChart: return "Barre";
        case hmi::Kind::PieChart: return "Part";
        case hmi::Kind::RadarChart: return "Axe";
        case hmi::Kind::XYChart: return "Courbe";
        case hmi::Kind::StateChart: return "Ligne";
        case hmi::Kind::VariableTable: return "Variable";
        default: return "Plume";
    }
}

// --------------------------------------------------------- lot 12 : la barre -
struct NavList {
    std::vector<std::string> views, labels;
    bool                     automatic{false};   // "views" vide : les vues ordinaires du projet
};

NavList readNav(const hmi::Project& p, const hmi::Object& o) {
    NavList n;
    n.automatic = hmi::listItems(o.text("views")).empty();
    for (const auto& it : hmi::navItems(&p, o)) {
        n.views.push_back(it.view);
        n.labels.push_back(it.label == hmi::viewCaption(it.view) ? std::string{} : it.label);
    }
    return n;
}

void writeNav(hmi::Object& o, const NavList& n) {
    o.set("views", hmi::joinSemicolons(n.views));
    const bool named = std::any_of(n.labels.begin(), n.labels.end(), [](const std::string& l) { return !l.empty(); });
    o.set("labels", named ? hmi::joinSemicolons(n.labels) : std::string{});
}

} // namespace

HmiContentPanel::HmiContentPanel(std::string id, hmi::DocumentPtr doc, Id view, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), view_(view), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AAdd, HmiGlyph::Plus, "Ajouter (une ligne, une plume, un \xC3\xA9tat)", "Ajouter");
    tools->add(ARemove, HmiGlyph::Delete, "Retirer l'\xC3\xA9l\xC3\xA9ment choisi");
    tools->add(AUp, HmiGlyph::Up, "Monter l'\xC3\xA9l\xC3\xA9ment (l'ordre compte : le premier \xC3\xA9tat vrai l'emporte)");
    tools->add(ADown, HmiGlyph::Down, "Descendre l'\xC3\xA9l\xC3\xA9ment");
    tools->separator();
    tools->add(AAddColumn, HmiGlyph::Table, "Ajouter une colonne au tableau", "Colonne");
    tools->add(ARemoveColumn, HmiGlyph::Delete, "Retirer la colonne (celle de la derni\xC3\xA8re case \xC3\xA9" "dit\xC3\xA9" "e, sinon la derni\xC3\xA8re)");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto editable = [this] {
        const auto m = mode();
        return m == Mode::Table || m == Mode::Trend || m == Mode::AnimatedImage || m == Mode::Zones || m == Mode::Tabs || m == Mode::NavItems;
    };
    tools_->setEnabledWhen(AAdd, editable);
    tools_->setEnabledWhen(ARemove, [this, editable] { return editable() && selectedIndex() >= 0; });
    tools_->setEnabledWhen(AUp, [this, editable] { return editable() && selectedIndex() > 0; });
    tools_->setEnabledWhen(ADown, [this, editable] {
        return editable() && selectedIndex() >= 0 && selectedIndex() + 1 < static_cast<int>(count());
    });
    tools_->setEnabledWhen(AAddColumn, [this] { return mode() == Mode::Table; });
    tools_->setEnabledWhen(ARemoveColumn, [this] {
        const auto* o = current();
        return mode() == Mode::Table && o && readTable(*o).columns() > 1;
    });
    auto table = std::make_unique<ui::TableView>(base + ".list");
    table->setColumns({{"#", 40.f, 32.f, false, false, true, ui::Align::End}, {"\xC3\x89l\xC3\xA9ment", 300.f}, {"D\xC3\xA9tail", 260.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(true);
    grid->setNameColumnRatio(0.42f);
    const auto src = assist::sourcesFor(doc_);
    grid->setFieldAssist([this, basic = assist::gridAssist(src), cell = assist::templateOrExpression(src)](
                             std::string_view cat, const PG::Property& p) -> ui::InputText::Assist {
        // Une case d'une ligne ("2 - Valeur") : sa colonne est retenue (Retirer la
        // colonne), et l'aide est celle d'un texte a trous ou d'une expression.
        if (mode() == Mode::Table && cat.rfind("Ligne", 0) == 0) {
            lastColumn_ = numberIn(p.name, false) - 1;
            return cell;
        }
        if (mode() == Mode::Table && cat == "Colonnes") lastColumn_ = numberIn(p.name, true) - 1;
        return basic(cat, p);
    });
    grid->setPaletteColors([doc = doc_] { return doc ? assist::projectColors(doc->project) : std::vector<std::string>{}; });
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));

    links_ += tools_->triggered->connect([this](int a) {
        const int sel = selectedIndex();
        switch (a) {
            case AAdd: if (addItem()) selectIndex(static_cast<int>(count()) - 1); break;
            case ARemove: (void)removeItem(sel); break;
            case AUp: if (moveItem(sel, -1)) selectIndex(sel - 1); break;
            case ADown: if (moveItem(sel, +1)) selectIndex(sel + 1); break;
            case AAddColumn: (void)addColumn(); break;
            case ARemoveColumn: (void)removeColumn(lastColumn_); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selected_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        rebuildGrid();
    });
    refresh();
}

const hmi::Object* HmiContentPanel::current() const {
    const auto* v = doc_->project.view(view_);
    return v && object_ != kNoId ? v->object(object_) : nullptr;
}

HmiContentPanel::Mode HmiContentPanel::mode() const {
    const auto* o = current();
    if (!o) return Mode::None;
    switch (o->kind) {
        case hmi::Kind::Table:         return Mode::Table;
        case hmi::Kind::Trend:         return Mode::Trend;
        case hmi::Kind::AnimatedImage: return Mode::AnimatedImage;
        case hmi::Kind::RecipeManager: return Mode::RecipeManager;
        // Lot 11 : les listes d'expressions des graphiques et du tableau de
        // variables se reglent comme les plumes d'une courbe.
        case hmi::Kind::BarChart: case hmi::Kind::PieChart: case hmi::Kind::RadarChart: case hmi::Kind::XYChart:
        case hmi::Kind::StateChart: case hmi::Kind::VariableTable:
            return Mode::Trend;
        // Lot 12
        case hmi::Kind::ZoneMap:       return Mode::Zones;
        case hmi::Kind::TabContainer:  return Mode::Tabs;
        case hmi::Kind::NavBar:        return Mode::NavItems;
        // 1.10.4 (K3) : ce qui tient d'autres objets - ses objets.
        case hmi::Kind::Group: case hmi::Kind::Container: case hmi::Kind::Frame: case hmi::Kind::ScrollPanel:
        case hmi::Kind::CollapsiblePanel:
            return Mode::Members;
        default:                       return Mode::None;
    }
}

std::size_t HmiContentPanel::count() const {
    const auto* o = current();
    if (!o) return 0;
    switch (mode()) {
        case Mode::Table:         return readTable(*o).rows.size();
        case Mode::Trend:         return readPens(*o).exprs.size();
        case Mode::AnimatedImage: return hmi::parseImageStates(o->text("states")).size();
        case Mode::RecipeManager: {
            const auto* r = doc_->project.recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text));
            return r ? r->records.size() : 0;
        }
        case Mode::Zones:    return hmi::parseMapZones(o->text("mapZones")).size();
        case Mode::Tabs:     return hmi::tabLabels(*o).size();
        case Mode::NavItems: return hmi::navItems(&doc_->project, *o).size();
        case Mode::Members: {
            std::size_t n = 0;
            if (const auto* v = doc_->project.view(view_))
                for (const auto& c : v->objects) n += c.parent == o->id ? 1u : 0u;
            return n;
        }
        case Mode::None: break;
    }
    return 0;
}

void HmiContentPanel::setObject(Id object) {
    if (object == object_) { refresh(); return; }
    object_ = object;
    selected_ = -1;
    lastColumn_ = -1;
    refresh();
    if (count() > 0 && mode() != Mode::RecipeManager) selectIndex(0);
}

int HmiContentPanel::selectedIndex() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(count())) return -1;
    return selected_;
}

void HmiContentPanel::selectIndex(int index) {
    if (index < 0 || index >= static_cast<int>(count())) return;
    hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(index));
    selected_ = index;
    rebuildGrid();
}

void HmiContentPanel::refresh() {
    const auto* o = current();
    std::vector<std::string> headers{"#", "\xC3\x89l\xC3\xA9ment", "D\xC3\xA9tail"};
    std::vector<std::vector<std::string>> rows;
    ui::Icon icon = ui::Icon::Document;
    if (o) {
        switch (mode()) {
            case Mode::Table: {
                const auto t = readTable(*o);
                headers = {"#", "Ligne", "Cases dynamiques"};
                icon = ui::Icon::AnimationTable;
                for (const auto& r : t.rows) {
                    std::size_t driven = 0;
                    for (const auto& c : r) driven += hmi::cellIsExpression(c) || hmi::cellIsTemplate(c);
                    rows.push_back({rowSummary(r), driven ? std::to_string(driven) : std::string{}});
                }
                break;
            }
            case Mode::Trend: {
                const auto p = readPens(*o);
                headers = {"#", penWord(o), "Expression"};
                icon = ui::Icon::Chart;
                for (std::size_t i = 0; i < p.exprs.size(); ++i)
                    rows.push_back({p.names[i].empty() ? p.exprs[i] : p.names[i], p.exprs[i]});
                break;
            }
            case Mode::AnimatedImage: {
                headers = {"#", "Condition", "Images"};
                icon = ui::Icon::Play;
                for (const auto& st : hmi::parseImageStates(o->text("states")))
                    rows.push_back({st.condition.empty() ? std::string("TRUE") : st.condition,
                                    imagesText(st.images) + (st.periodMs > 0 ? "  @ " + std::to_string(st.periodMs) + " ms" : std::string{})});
                break;
            }
            case Mode::RecipeManager: {
                headers = {"#", "Jeu", "Valeurs"};
                icon = ui::Icon::AnimationTable;
                if (const auto* r = doc_->project.recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)))
                    for (const auto& rec : r->records) {
                        std::string values;
                        for (std::size_t k = 0; k < rec.values.size(); ++k)
                            values += (k ? "; " : "") + hmi::unescapeText(rec.values[k]);
                        rows.push_back({rec.name, values});
                    }
                break;
            }
            case Mode::Zones: {
                headers = {"#", "Zone", "Groupe  \xC2\xB7  vue"};
                icon = ui::Icon::Screen;
                for (const auto& z : hmi::parseMapZones(o->text("mapZones")))
                    rows.push_back({z.name, z.alarmGroup() + (z.view.empty() ? std::string{} : "  \xC2\xB7  \xE2\x86\x92 " + z.view)});
                break;
            }
            case Mode::Tabs: {
                headers = {"#", "Page", "Objets"};
                icon = ui::Icon::Folder;
                const auto* v = doc_->project.view(view_);
                const auto labels = hmi::tabLabels(*o);
                for (std::size_t k = 0; k < labels.size(); ++k) {
                    std::size_t n = 0;
                    if (v)
                        for (const auto& c : v->objects)
                            if (c.parent == o->id && hmi::tabPageOf(c) == static_cast<int>(k) + 1) ++n;
                    rows.push_back({labels[k], std::to_string(n)});
                }
                break;
            }
            case Mode::NavItems: {
                headers = {"#", "Vue", "Libell\xC3\xA9"};
                icon = ui::Icon::Screen;
                for (const auto& it : hmi::navItems(&doc_->project, *o)) rows.push_back({it.view, it.label});
                break;
            }
            case Mode::Members: {
                headers = {"#", "Objet", "Type"};
                icon = ui::Icon::Folder;
                if (const auto* v = doc_->project.view(view_))
                    for (const auto& c : v->objects)
                        if (c.parent == o->id) rows.push_back({c.name, std::string(hmi::kindLabel(c.kind))});
                break;
            }
            case Mode::None: break;
        }
    }
    const std::size_t n = rows.size();
    // Les titres des colonnes suivent ce qu'on montre (Ligne, Plume, Condition...).
    table_->setColumns({{headers[0], 40.f, 32.f, false, false, true, ui::Align::End}, {headers[1], 300.f}, {headers[2], 240.f}});
    model_ = std::make_shared<Rows>(std::move(headers), std::move(rows), icon);
    const int keep = selected_;
    table_->setModel(model_);
    if (keep >= 0 && keep < static_cast<int>(n)) {
        hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(keep));
        selected_ = keep;
    } else {
        selected_ = -1;
    }
    rebuildGrid();
    if (n != lastCount_) {
        lastCount_ = n;
        countChanged->emit(n);
    }
    invalidate();
}

bool HmiContentPanel::change(const std::string& label, const std::function<bool(hmi::Object&)>& fn) {
    const Id object = object_;
    bool done = false;
    auto cmd = hmi::changeView(doc_, view_, label, [&](hmi::Project&, hmi::View& v) {
        if (auto* o = v.object(object)) done = fn(*o);
    });
    if (!cmd || !done) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiContentPanel::changeInView(const std::string& label, const std::function<bool(hmi::View&, hmi::Object&)>& fn) {
    const Id object = object_;
    bool done = false;
    auto cmd = hmi::changeView(doc_, view_, label, [&](hmi::Project&, hmi::View& v) {
        if (auto* o = v.object(object)) done = fn(v, *o);
    });
    if (!cmd || !done) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiContentPanel::addItem() {
    switch (mode()) {
        case Mode::Table:
            return change("Ajouter une ligne", [](hmi::Object& o) {
                auto t = readTable(o);
                t.rows.emplace_back(t.columns());
                writeTable(o, t);
                return true;
            });
        case Mode::Trend:
            return change("Ajouter : " + penWord(current()), [](hmi::Object& o) {
                auto p = readPens(o);
                p.exprs.emplace_back("0");
                p.names.push_back(penWord(&o) + " " + std::to_string(p.exprs.size()));
                const auto& palette = hmiTrendPalette();
                if (p.colors.size() < p.exprs.size()) p.colors.push_back(palette[(p.exprs.size() - 1) % palette.size()]);
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage:
            return change("Ajouter un \xC3\xA9tat", [](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                hmi::ImageState st;
                st.condition = "TRUE";
                states.push_back(st);
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        // Lot 12
        case Mode::Zones:
            return change("Ajouter une zone", [](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                hmi::MapZone z;
                z.name = "Zone " + std::to_string(zones.size() + 1);
                const double x = 8 + static_cast<double>(zones.size() % 4) * 22, y = 10 + static_cast<double>(zones.size() / 4 % 3) * 28;
                z.points = {{x, y}, {x + 18, y}, {x + 18, y + 22}, {x, y + 22}};
                zones.push_back(std::move(z));
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            return change("Ajouter une page", [](hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                labels.push_back("Page " + std::to_string(labels.size() + 1));
                o.set("tabs", hmi::joinSemicolons(labels));
                return true;
            });
        case Mode::NavItems:
            return change("Ajouter une vue \xC3\xA0 la barre", [this](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                std::string next;
                for (const auto& v : doc_->project.views)
                    if (v.role == "vue" && std::find(n.views.begin(), n.views.end(), v.name) == n.views.end()) { next = v.name; break; }
                if (next.empty() && !doc_->project.views.empty()) next = doc_->project.views.front().name;
                if (next.empty()) return false;
                n.views.push_back(next);
                n.labels.emplace_back();
                writeNav(o, n);
                return true;
            });
        default: return false;
    }
}

bool HmiContentPanel::removeItem(int index) {
    if (index < 0 || index >= static_cast<int>(count())) return false;
    const auto i = static_cast<std::size_t>(index);
    bool ok = false;
    switch (mode()) {
        case Mode::Table:
            ok = change("Retirer une ligne", [i](hmi::Object& o) {
                auto t = readTable(o);
                t.rows.erase(t.rows.begin() + static_cast<std::ptrdiff_t>(i));
                writeTable(o, t);
                return true;
            });
            break;
        case Mode::Trend:
            ok = change("Retirer une plume", [i](hmi::Object& o) {
                auto p = readPens(o);
                p.exprs.erase(p.exprs.begin() + static_cast<std::ptrdiff_t>(i));
                p.names.erase(p.names.begin() + static_cast<std::ptrdiff_t>(i));
                if (i < p.colors.size()) p.colors.erase(p.colors.begin() + static_cast<std::ptrdiff_t>(i));
                writePens(o, std::move(p));
                return true;
            });
            break;
        case Mode::AnimatedImage:
            ok = change("Retirer un \xC3\xA9tat", [i](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                states.erase(states.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
            break;
        // Lot 12
        case Mode::Zones:
            ok = change("Retirer une zone", [i](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                zones.erase(zones.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
            break;
        case Mode::Tabs:
            // Les objets de la page retiree passent sur la page d'avant ; les suivants
            // gardent leur page (renumerotee).
            ok = changeInView("Retirer une page", [i](hmi::View& v, hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                if (labels.size() <= 1) return false;
                const int gone = static_cast<int>(i) + 1;
                labels.erase(labels.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("tabs", hmi::joinSemicolons(labels));
                for (auto& c : v.objects) {
                    if (c.parent != o.id) continue;
                    const int pg = hmi::tabPageOf(c);
                    if (pg == gone) c.setNumber("tabPage", std::max(1, gone - 1));
                    else if (pg > gone) c.setNumber("tabPage", pg - 1);
                }
                o.setNumber("page", std::clamp(static_cast<int>(o.number("page", 1)), 1, static_cast<int>(labels.size())));
                return true;
            });
            break;
        case Mode::NavItems:
            ok = change("Retirer une vue de la barre", [this, i](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                if (i >= n.views.size()) return false;
                n.views.erase(n.views.begin() + static_cast<std::ptrdiff_t>(i));
                n.labels.erase(n.labels.begin() + static_cast<std::ptrdiff_t>(i));
                writeNav(o, n);
                return true;
            });
            break;
        default: break;
    }
    if (ok) {
        selected_ = -1;
        if (count() > 0) selectIndex(std::min(index, static_cast<int>(count()) - 1));
        else rebuildGrid();
    }
    return ok;
}

bool HmiContentPanel::moveItem(int index, int delta) {
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= static_cast<int>(count()) || to >= static_cast<int>(count())) return false;
    const auto a = static_cast<std::size_t>(index), b = static_cast<std::size_t>(to);
    switch (mode()) {
        case Mode::Table:
            return change("Ordonner les lignes", [a, b](hmi::Object& o) {
                auto t = readTable(o);
                std::swap(t.rows[a], t.rows[b]);
                writeTable(o, t);
                return true;
            });
        case Mode::Trend:
            return change("Ordonner les plumes", [a, b](hmi::Object& o) {
                auto p = readPens(o);
                std::swap(p.exprs[a], p.exprs[b]);
                std::swap(p.names[a], p.names[b]);
                if (b < p.colors.size() && a < p.colors.size()) std::swap(p.colors[a], p.colors[b]);
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage:
            return change("Ordonner les \xC3\xA9tats", [a, b](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                std::swap(states[a], states[b]);
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        // Lot 12
        case Mode::Zones:
            return change("Ordonner les zones", [a, b](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                std::swap(zones[a], zones[b]);
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            // Une page deplacee emmene ses objets.
            return changeInView("Ordonner les pages", [a, b](hmi::View& v, hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                std::swap(labels[a], labels[b]);
                o.set("tabs", hmi::joinSemicolons(labels));
                const int pa = static_cast<int>(a) + 1, pb = static_cast<int>(b) + 1;
                for (auto& c : v.objects) {
                    if (c.parent != o.id) continue;
                    const int pg = hmi::tabPageOf(c);
                    if (pg == pa) c.setNumber("tabPage", pb);
                    else if (pg == pb) c.setNumber("tabPage", pa);
                }
                const int shown = hmi::shownTabPage(o);
                if (shown == pa) o.setNumber("page", pb);
                else if (shown == pb) o.setNumber("page", pa);
                return true;
            });
        case Mode::NavItems:
            return change("Ordonner les vues de la barre", [this, a, b](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                std::swap(n.views[a], n.views[b]);
                std::swap(n.labels[a], n.labels[b]);
                writeNav(o, n);
                return true;
            });
        default: return false;
    }
}

bool HmiContentPanel::addColumn() {
    if (mode() != Mode::Table) return false;
    return change("Ajouter une colonne", [](hmi::Object& o) {
        auto t = readTable(o);
        t.headers.push_back("Colonne " + std::to_string(t.headers.size() + 1));
        for (auto& r : t.rows) r.emplace_back();
        if (!t.widths.empty()) t.widths.emplace_back("1");
        writeTable(o, t);
        return true;
    });
}

bool HmiContentPanel::removeColumn(int column) {
    if (mode() != Mode::Table) return false;
    const auto* o = current();
    if (!o) return false;
    const auto n = static_cast<int>(readTable(*o).columns());
    if (n <= 1) return false;
    const auto k = static_cast<std::size_t>(column >= 0 && column < n ? column : n - 1);
    const bool ok = change("Retirer une colonne", [k](hmi::Object& obj) {
        auto t = readTable(obj);
        t.headers.erase(t.headers.begin() + static_cast<std::ptrdiff_t>(k));
        for (auto& r : t.rows) if (k < r.size()) r.erase(r.begin() + static_cast<std::ptrdiff_t>(k));
        if (k < t.widths.size()) t.widths.erase(t.widths.begin() + static_cast<std::ptrdiff_t>(k));
        writeTable(obj, t);
        return true;
    });
    if (ok) lastColumn_ = -1;
    return ok;
}

bool HmiContentPanel::setField(int index, const std::string& field, const std::string& value) {
    const auto i = static_cast<std::size_t>(std::max(0, index));
    const auto colOf = [&](std::string_view prefix) { return static_cast<std::size_t>(std::max(0, std::atoi(field.c_str() + prefix.size()))); };
    switch (mode()) {
        case Mode::Table: {
            if (field.rfind("cell:", 0) == 0) {
                const auto k = colOf("cell:");
                return change("Modifier une case", [i, k, value](hmi::Object& o) {
                    auto t = readTable(o);
                    if (i >= t.rows.size() || k >= t.columns() || t.rows[i][k] == value) return false;
                    t.rows[i][k] = value;
                    writeTable(o, t);
                    return true;
                });
            }
            if (field.rfind("header:", 0) == 0 || field.rfind("width:", 0) == 0) {
                const bool header = field[0] == 'h';
                const auto k = colOf(header ? "header:" : "width:");
                if (!header) {
                    double w = 0;
                    if (!hmi::parseNumber(value, w) || w <= 0) return false;
                }
                return change(header ? "Nommer une colonne" : "Largeur de colonne", [k, header, value](hmi::Object& o) {
                    auto t = readTable(o);
                    if (k >= t.columns()) return false;
                    if (header) {
                        if (t.headers[k] == value) return false;
                        t.headers[k] = value;
                    } else {
                        if (t.widths.empty()) t.widths.assign(t.columns(), "1");
                        if (t.widths[k] == value) return false;
                        t.widths[k] = value;
                    }
                    writeTable(o, t);
                    return true;
                });
            }
            return false;
        }
        case Mode::Trend:
            return change("R\xC3\xA9gler une plume", [i, field, value](hmi::Object& o) {
                auto p = readPens(o);
                if (i >= p.exprs.size()) return false;
                if (field == "name") p.names[i] = value;
                else if (field == "expression") {
                    if (value.empty()) return false;
                    p.exprs[i] = value;
                } else if (field == "color") {
                    while (p.colors.size() <= i) p.colors.push_back(hmiTrendPalette()[p.colors.size() % hmiTrendPalette().size()]);
                    p.colors[i] = value;
                } else return false;
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage: {
            if (field == "default") return change("Image par d\xC3\xA9" "faut", [value](hmi::Object& o) { o.set("image", value); return true; });
            if (field == "objectPeriod") {
                double ms = 0;
                if (!hmi::parseNumber(value, ms) || ms < 20) return false;
                return change("P\xC3\xA9riode de d\xC3\xA9" "filement", [ms](hmi::Object& o) { o.setNumber("period", ms); return true; });
            }
            return change("R\xC3\xA9gler un \xC3\xA9tat", [i, field, value](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                if (i >= states.size()) return false;
                auto& st = states[i];
                if (field == "condition") st.condition = value.empty() ? std::string("TRUE") : value;
                else if (field == "images") {
                    st.images.clear();
                    for (const auto& part : hmi::splitSemicolons([&] {
                             std::string v = value;
                             std::replace(v.begin(), v.end(), ',', ';');
                             return v;
                         }()))
                        if (!part.empty()) st.images.push_back(part);
                } else if (field == "addImage") {
                    if (value.empty()) return false;
                    st.images.push_back(value);
                } else if (field == "period") {
                    double ms = 0;
                    if (!hmi::parseNumber(value, ms) || ms < 0) return false;
                    st.periodMs = static_cast<int>(ms);
                } else return false;
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        }
        case Mode::RecipeManager: {
            if (field == "recipe") return change("Recette du gestionnaire", [value](hmi::Object& o) { o.set("recipe", value); return true; });
            if (field.rfind("button:", 0) == 0) {
                const std::string name = field.substr(7);
                const bool on = hmi::parseBool(value, true);
                return change("Boutons du gestionnaire", [name, on](hmi::Object& o) {
                    const auto* p = o.find("buttons");
                    auto shown = p ? hmi::splitSemicolons(p->value)
                                   : std::vector<std::string>(std::begin(hmi::kRecipeButtons), std::end(hmi::kRecipeButtons));
                    const bool has = std::find(shown.begin(), shown.end(), name) != shown.end();
                    if (has == on) return false;
                    // L'ordre reste celui de la barre : Ajouter, Modifier, Supprimer, Appliquer, Lire.
                    std::vector<std::string> next;
                    for (const auto b : hmi::kRecipeButtons) {
                        const std::string label(b);
                        const bool keep = label == name ? on : std::find(shown.begin(), shown.end(), label) != shown.end();
                        if (keep) next.push_back(label);
                    }
                    o.set("buttons", hmi::joinSemicolons(next));
                    return true;
                });
            }
            return false;
        }
        // Lot 12
        case Mode::Zones:
            return change("R\xC3\xA9gler une zone", [i, field, value](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                if (i >= zones.size()) return false;
                auto& z = zones[i];
                if (field == "name") {
                    if (value.empty() || value.find('|') != std::string::npos) return false;
                    z.name = value;
                } else if (field == "points") {
                    auto pts = hmi::parseZonePoints(value);
                    if (pts.size() < 3) return false;
                    z.points = std::move(pts);
                } else if (field == "group") z.group = value;
                else if (field == "view") z.view = value;
                else if (field == "color") z.color = value;
                else return false;
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            return change("Nommer une page", [i, value](hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                if (i >= labels.size() || value.empty() || value.find(';') != std::string::npos || labels[i] == value) return false;
                labels[i] = value;
                o.set("tabs", hmi::joinSemicolons(labels));
                return true;
            });
        case Mode::NavItems:
            return change("R\xC3\xA9gler la barre de navigation", [this, i, field, value](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                if (i >= n.views.size() || value.find(';') != std::string::npos) return false;
                if (field == "view") {
                    if (value.empty() || !doc_->project.viewByName(value)) return false;
                    n.views[i] = value;
                } else if (field == "label") {
                    n.labels[i] = value == hmi::viewCaption(n.views[i]) ? std::string{} : value;
                } else return false;
                writeNav(o, n);
                return true;
            });
        case Mode::Members:          // 1.10.4 (K3) : en lecture
        case Mode::None: break;
    }
    return false;
}

void HmiContentPanel::rebuildGrid() {
    const auto* o = current();
    const auto readOnly = [](std::string name, std::string value, std::string help = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = PG::ValueType::ReadOnly;
        p.description = std::move(help);
        return p;
    };
    const auto editable = [this](std::string name, std::string value, PG::ValueType t, int index, std::string field,
                                 std::string help = {}, std::vector<std::string> choices = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = t;
        p.description = std::move(help);
        p.enumValues = std::move(choices);
        p.commit = [this, index, field](std::string_view v) { return setField(index, field, std::string(v)); };
        return p;
    };
    std::vector<PG::Category> cats;
    const Mode m = mode();
    if (!o || m == Mode::None) {
        PG::Category info;
        info.name = "Contenu";
        info.properties.push_back(readOnly(o ? o->name : std::string("Rien de choisi"),
                                           o ? std::string(hmi::kindLabel(o->kind)) + " : rien \xC3\xA0 r\xC3\xA9gler ici" : std::string{},
                                           "L'onglet Contenu r\xC3\xA8gle les lignes et colonnes d'un Tableau, les plumes d'une "
                                           "Courbe, les \xC3\xA9tats d'une Image anim\xC3\xA9" "e et la recette d'un Gestionnaire de recettes ; "
                                           "il montre les objets d'un Groupe ou d'un conteneur. Un objet sans contenu n'a pas l'onglet."));
        cats.push_back(std::move(info));
        grid_->setCategories(std::move(cats));
        return;
    }
    const int index = selectedIndex();
    const auto i = static_cast<std::size_t>(std::max(0, index));
    switch (m) {
        case Mode::Table: {
            const auto t = readTable(*o);
            if (index >= 0) {
                PG::Category row;
                row.name = "Ligne " + std::to_string(index + 1);
                for (std::size_t k = 0; k < t.columns(); ++k) {
                    auto cell = editable(std::to_string(k + 1) + " - " + t.headers[k], t.rows[i][k], PG::ValueType::Text,
                                         index, "cell:" + std::to_string(k),
                                         "Un texte ; un texte \xC3\xA0 trous : Niveau {Cuve.niveau:0.0} % ; "
                                         "ou =expression : =Debit * 3.6. \\n : une nouvelle ligne n'existe pas "
                                         "dans une case.");
                    // 1.10 (chantier K) : la case de partout - "=expression" montre son "="
                    // et sa pastille ; la retirer (X, clic droit, "=" seul) vide la case.
                    ui::exprfield::mark(cell, ui::exprfield::Expect::Template);
                    if (!cell.value.empty() && cell.value.front() == '=') cell.expression = cell.value.substr(1);
                    cell.commit = [inner = std::move(cell.commit)](std::string_view v) {
                        return inner(v == "=" ? std::string_view{} : v);
                    };
                    row.properties.push_back(std::move(cell));
                }
                cats.push_back(std::move(row));
            } else {
                PG::Category info;
                info.name = "Lignes";
                info.properties.push_back(readOnly("Lignes", std::to_string(t.rows.size()),
                                                   "Choisis une ligne pour r\xC3\xA9gler ses cases ; \xC2\xAB Ajouter \xC2\xBB en cr\xC3\xA9" "e une."));
                cats.push_back(std::move(info));
            }
            PG::Category columns;
            columns.name = "Colonnes";
            for (std::size_t k = 0; k < t.columns(); ++k) {
                columns.properties.push_back(editable("En-t\xC3\xAAte " + std::to_string(k + 1), t.headers[k], PG::ValueType::Text, -1,
                                                      "header:" + std::to_string(k)));
                columns.properties.push_back(editable("Largeur " + std::to_string(k + 1), k < t.widths.size() ? t.widths[k] : std::string("1"),
                                                      PG::ValueType::Real, -1, "width:" + std::to_string(k),
                                                      "Relative : 2 = deux fois plus large qu'une colonne \xC3\xA0 1."));
            }
            cats.push_back(std::move(columns));
            break;
        }
        case Mode::Trend: {
            const auto p = readPens(*o);
            PG::Category pen;
            if (index >= 0) {
                pen.name = penWord(o) + " " + std::to_string(index + 1);
                pen.properties.push_back(editable("Nom", p.names[i], PG::ValueType::Text, index, "name",
                                                  "Ce que dit la l\xC3\xA9gende ; vide : l'expression."));
                auto expr = editable("Expression", p.exprs[i], PG::ValueType::Text, index, "expression",
                                     core::hasApi() ? "Une variable IHM ou de l'automate, ou une expression : Armoires[0].ana.PT1.mes * 10"
                                                    : "Une variable IHM, ou une expression : Armoires[0].ana.PT1.mes * 10");   // 1.12.2
                // 1.10 (chantier K) : la pastille, l'invite et l'aide des nombres ; "=" tape
                // retire. Une plume ne se vide pas (\xC2\xAB Retirer \xC2\xBB l'enleve) : pas de X.
                ui::exprfield::markWhole(expr, ui::exprfield::Expect::Number, false);
                pen.properties.push_back(std::move(expr));
                // 1.12.2 : le chronogramme colore ses lignes par leurs etats (sa liste d'etats) : la
                // case Couleur d'une de ses lignes ne faisait rien.
                if (o->kind != hmi::Kind::StateChart)
                    pen.properties.push_back(editable("Couleur", i < p.colors.size() ? p.colors[i] : std::string{}, PG::ValueType::Color, index,
                                                      "color"));
            } else {
                pen.name = penWord(o) + "s";
                pen.properties.push_back(readOnly(penWord(o) + "s", std::to_string(p.exprs.size()),
                                                  "Plusieurs plumes dans la m\xC3\xAAme courbe : \xC2\xAB Ajouter \xC2\xBB, puis leur expression."));
            }
            cats.push_back(std::move(pen));
            break;
        }
        case Mode::AnimatedImage: {
            const auto states = hmi::parseImageStates(o->text("states"));
            std::vector<std::string> images{""};
            for (const auto& r : doc_->project.assets.resources)
                if (r.kind() == hmi::MediaKind::Image) images.push_back(r.name);
            if (index >= 0 && i < states.size()) {
                const auto& st = states[i];
                PG::Category state;
                state.name = "\xC3\x89tat " + std::to_string(index + 1);
                auto cond = editable("Condition", st.condition, PG::ValueType::Text, index, "condition",
                                     "Une expression vraie ou fausse : Pompe_Marche AND NOT Defaut. Le premier "
                                     "\xC3\xA9tat vrai l'emporte.");
                // 1.10 (chantier K) : la case de partout ; vide (X) : TRUE, toujours vraie.
                ui::exprfield::markWhole(cond, ui::exprfield::Expect::Bool, st.condition != "TRUE");
                state.properties.push_back(std::move(cond));
                state.properties.push_back(editable("Images (a, b)", imagesText(st.images), PG::ValueType::Text, index, "images",
                                                    "Plusieurs images d\xC3\xA9" "filent \xC3\xA0 la p\xC3\xA9riode de l'\xC3\xA9tat."));
                state.properties.push_back(editable("Ajouter une image", "", PG::ValueType::Enum, index, "addImage",
                                                    "Une image du gestionnaire de ressources, ajout\xC3\xA9" "e \xC3\xA0 la fin.", images));
                state.properties.push_back(editable("P\xC3\xA9riode (ms)", std::to_string(st.periodMs), PG::ValueType::Integer, index,
                                                    "period", "0 : celle de l'objet."));
                cats.push_back(std::move(state));
            }
            PG::Category def;
            def.name = "Sans \xC3\xA9tat vrai";
            std::string current = o->text("image");
            if (std::find(images.begin(), images.end(), current) == images.end()) images.push_back(current);
            def.properties.push_back(editable("Image par d\xC3\xA9" "faut", current, PG::ValueType::Enum, -1, "default",
                                              "Montr\xC3\xA9" "e quand aucune condition n'est vraie.", images));
            def.properties.push_back(editable("P\xC3\xA9riode de l'objet (ms)", hmi::formatNumber(o->number("period", 500)),
                                              PG::ValueType::Integer, -1, "objectPeriod"));
            def.properties.push_back(readOnly("\xC3\x89tats", std::to_string(states.size())));
            cats.push_back(std::move(def));
            break;
        }
        case Mode::RecipeManager: {
            PG::Category rc;
            rc.name = "Recette";
            std::vector<std::string> recipes{""};
            for (const auto& r : doc_->project.recipes) recipes.push_back(r.name);
            const std::string chosen = o->text("recipe");
            if (std::find(recipes.begin(), recipes.end(), chosen) == recipes.end()) recipes.push_back(chosen);
            rc.properties.push_back(editable("Recette", chosen, PG::ValueType::Enum, -1, "recipe",
                                             "Les jeux de cette recette, un par ligne, une colonne par \xC3\xA9l\xC3\xA9ment.", recipes));
            const auto* bp = o->find("buttons");
            const auto shown = bp ? hmi::splitSemicolons(bp->value)
                                  : std::vector<std::string>(std::begin(hmi::kRecipeButtons), std::end(hmi::kRecipeButtons));
            static const char* kHelp[] = {
                "Un nouveau jeu : son nom demand\xC3\xA9, les valeurs de l'installation propos\xC3\xA9" "es.",
                "Le jeu choisi : son nom et ses valeurs, dans un dialogue.",
                "Le jeu choisi, apr\xC3\xA8s confirmation.",
                "\xC3\x89" "crire le jeu choisi dans l'installation (tout ou rien, bornes v\xC3\xA9rifi\xC3\xA9" "es).",
                "Ranger dans le jeu choisi les valeurs lues dans l'installation."};
            std::size_t k = 0;
            for (const auto b : hmi::kRecipeButtons) {
                const std::string label(b);
                const bool on = std::find(shown.begin(), shown.end(), label) != shown.end();
                rc.properties.push_back(editable("Bouton " + label, on ? "TRUE" : "FALSE", PG::ValueType::Boolean, -1, "button:" + label,
                                                 std::string(kHelp[k++]) + " Sous s\xC3\xA9" "curit\xC3\xA9 : permission Recettes."));
            }
            rc.properties.push_back(readOnly("Jeux", std::to_string(count()),
                                             "Ils se modifient dans Configuration > Recettes, ou en marche avec les boutons de l'objet "
                                             "(le projet change, Ctrl+Z le reprend)."));
            cats.push_back(std::move(rc));
            break;
        }
        // Lot 12 : les zones d'un plan.
        case Mode::Zones: {
            const auto zones = hmi::parseMapZones(o->text("mapZones"));
            std::vector<std::string> groups{""}, views{""};
            for (const auto& a : doc_->project.alarms)
                if (!a.group.empty() && std::find(groups.begin(), groups.end(), a.group) == groups.end()) groups.push_back(a.group);
            for (const auto& v : doc_->project.views)
                if (v.role == "vue" || v.role == "popup") views.push_back(v.name);
            if (index >= 0 && i < zones.size()) {
                const auto& z = zones[i];
                PG::Category zc;
                zc.name = "Zone " + std::to_string(index + 1);
                zc.properties.push_back(editable("Nom", z.name, PG::ValueType::Text, index, "name", "\xC3\x89" "crit au centre de la zone."));
                zc.properties.push_back(editable("Points (x,y en %)", hmi::formatZonePoints(z.points), PG::ValueType::Text, index, "points",
                                                 "Les sommets, en % de la largeur et de la hauteur du plan : 10,20 40,20 40,60 10,60. "
                                                 "Un rectangle : x,y,l,h (10,20,30,40)."));
                auto g = groups;
                if (std::find(g.begin(), g.end(), z.group) == g.end()) g.push_back(z.group);
                zc.properties.push_back(editable("Groupe d'alarmes", z.group, PG::ValueType::Enum, index, "group",
                                                 "Ses alarmes colorent la zone ; vide : le groupe du m\xC3\xAAme nom que la zone.", g));
                auto vv = views;
                if (std::find(vv.begin(), vv.end(), z.view) == vv.end()) vv.push_back(z.view);
                zc.properties.push_back(editable("Vue ouverte d'un clic", z.view, PG::ValueType::Enum, index, "view",
                                                 "Vide : le clic choisit la zone sans changer de vue.", vv));
                zc.properties.push_back(editable("Couleur", z.color, PG::ValueType::Color, index, "color",
                                                 "Sa couleur sans alarme ; vide : Couleur d'une zone calme."));
                cats.push_back(std::move(zc));
            } else {
                PG::Category info;
                info.name = "Zones";
                info.properties.push_back(readOnly("Zones", std::to_string(zones.size()),
                                                   "\xC2\xAB Ajouter \xC2\xBB pose une zone rectangulaire ; ses points se r\xC3\xA8glent ici."));
                cats.push_back(std::move(info));
            }
            break;
        }
        // Lot 12 : les pages d'un conteneur a onglets.
        case Mode::Tabs: {
            const auto labels = hmi::tabLabels(*o);
            PG::Category pc;
            pc.name = index >= 0 ? "Page " + std::to_string(index + 1) : std::string("Pages");
            if (index >= 0 && i < labels.size())
                pc.properties.push_back(editable("Libell\xC3\xA9", labels[i], PG::ValueType::Text, index, "label",
                                                 "Le texte de l'onglet. Un clic sur l'onglet, dans l'\xC3\xA9" "diteur, montre la page."));
            pc.properties.push_back(readOnly("Pages", std::to_string(labels.size()),
                                             "Un objet pos\xC3\xA9 sur le conteneur va dans la page montr\xC3\xA9" "e ; retirer ou "
                                             "d\xC3\xA9placer une page emm\xC3\xA8ne ses objets."));
            cats.push_back(std::move(pc));
            break;
        }
        // Lot 12 : les vues d'une barre de navigation.
        case Mode::NavItems: {
            const auto n = readNav(doc_->project, *o);
            PG::Category nc;
            nc.name = index >= 0 ? "Bouton " + std::to_string(index + 1) : std::string("Vues");
            if (index >= 0 && i < n.views.size()) {
                std::vector<std::string> views;
                for (const auto& v : doc_->project.views)
                    if (v.role == "vue") views.push_back(v.name);
                if (std::find(views.begin(), views.end(), n.views[i]) == views.end()) views.push_back(n.views[i]);
                nc.properties.push_back(editable("Vue", n.views[i], PG::ValueType::Enum, index, "view", "La vue ouverte d'un clic.", views));
                nc.properties.push_back(editable("Libell\xC3\xA9", n.labels[i].empty() ? hmi::viewCaption(n.views[i]) : n.labels[i],
                                                 PG::ValueType::Text, index, "label", "Vide : le nom de la vue (sans Vue_, _ en espaces)."));
            }
            nc.properties.push_back(readOnly("Vues", std::to_string(n.views.size()) + (n.automatic ? "  (automatique)" : ""),
                                             "Automatique : toutes les vues ordinaires du projet, dans leur ordre. \xC2\xAB Ajouter \xC2\xBB "
                                             "fixe la liste."));
            cats.push_back(std::move(nc));
            break;
        }
        case Mode::Members: {
            // 1.10.4 (K3) : ce que l'objet tient - en lecture ; on regle un objet en le choisissant.
            std::vector<const hmi::Object*> members;
            if (const auto* v = doc_->project.view(view_))
                for (const auto& c : v->objects)
                    if (c.parent == o->id) members.push_back(&c);
            const std::string how = o->kind == hmi::Kind::Group
                ? std::string("Ses objets se choisissent dans l'explorateur d'objets, ou d'un double-clic dans le groupe. "
                              "Dégrouper les rend libres.")
                : std::string("Ce qu'on pose dedans devient son enfant et le suit ; un double-clic y entre.");
            if (index >= 0 && i < members.size()) {
                const auto& member = *members[i];
                PG::Category mc;
                mc.name = member.name;
                mc.properties.push_back(readOnly("Type", std::string(hmi::kindLabel(member.kind))));
                mc.properties.push_back(readOnly("Position", hmi::formatNumber(member.number("x", 0)) + " ; " + hmi::formatNumber(member.number("y", 0))));
                mc.properties.push_back(readOnly("Taille", hmi::formatNumber(member.number("w", 0)) + " x " + hmi::formatNumber(member.number("h", 0))));
                mc.properties.push_back(readOnly("Réglages", "choisis-le", how));
                cats.push_back(std::move(mc));
            }
            PG::Category info;
            info.name = "Contenu";
            info.properties.push_back(readOnly("Objets", std::to_string(members.size()), how));
            cats.push_back(std::move(info));
            break;
        }
        case Mode::None: break;
    }
    grid_->setCategories(std::move(cats));
}

void HmiContentPanel::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 34});
    const float listH = std::clamp(b.h * 0.34f, 70.f, 190.f);
    table_->setBounds({b.x, b.y + 34, b.w, listH});
    grid_->setBounds({b.x, b.y + 38 + listH, b.w, std::max(0.f, b.h - 38 - listH)});
}

void HmiContentPanel::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
