// app/AnimationTablesPane.cpp - les tables d'animation, avec l'IHM (lot API 3).
#include "AnimationTablesPane.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : XPGAnalyser API - pas de variable IHM

#include "ApiPanes.hpp"
#include "RenameDialog.hpp"               // lot 7 : requestRename (le dialogue qui montre tout)
#include "hmi/HmiModbusToolPane.hpp"
#include "hmi/HmiPanels.hpp"

#include "../hmi/HmiCommands.hpp"
#include "../hmi/HmiRuntime.hpp"
#include "../project/BlockLibrary.hpp"
#include "../project/CrossReference.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace app {

using ui::RowIndex;

namespace {

const gfx::FontId kSmall{13};
constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}


// Le type d'une adresse directe (%MW10 : un INT ; %M5 : un EBOOL...).
std::string typeOfAddress(std::string_view a) {
    const auto u = upper(a);
    if (u.rfind("%MW", 0) == 0 || u.rfind("%IW", 0) == 0 || u.rfind("%QW", 0) == 0 || u.rfind("%KW", 0) == 0 || u.rfind("%SW", 0) == 0) return "INT";
    if (u.rfind("%MD", 0) == 0 || u.rfind("%ID", 0) == 0 || u.rfind("%QD", 0) == 0 || u.rfind("%KD", 0) == 0) return "DINT";
    if (u.rfind("%MF", 0) == 0 || u.rfind("%KF", 0) == 0) return "REAL";
    return "EBOOL";
}

// L'element d'un tableau : "ARRAY[0..5] OF ST_Armoire" -> "ST_Armoire".
std::string elementOf(const std::string& type) {
    const auto u = upper(type);
    const auto of = u.find(" OF ");
    return u.rfind("ARRAY", 0) == 0 && of != std::string::npos ? trim(type.substr(of + 4)) : type;
}

struct ApiInfo {
    std::string type, declared, address;
    bool        known{false};
    domain::Index variable{domain::kNoIndex};
};

// Une ligne de l'automate : sa racine (une variable declaree), puis ses membres
// dans les DDT - "armoires[0].ana.PT1.mes" -> REAL.
ApiInfo apiInfoOf(const domain::Project& p, std::string_view path) {
    ApiInfo out;
    if (!path.empty() && path.front() == '%') {
        out.known = true;
        out.address = std::string(path);
        out.type = typeOfAddress(path);
        out.declared = "adresse directe (sans nom)";
        return out;
    }
    std::vector<std::string> parts;
    {
        std::string cur;
        for (const char ch : path) {
            if (ch == '.') { parts.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        parts.push_back(cur);
    }
    const auto bare = [](const std::string& s) { return s.substr(0, s.find('[')); };
    const auto rootName = lower(bare(parts.front()));
    domain::Index best = domain::kNoIndex;
    for (domain::Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        if (lower(p.strings.text(v.name)) != rootName) continue;
        if (v.scope == domain::VariableScope::Global) { best = i; break; }
        // Un champ de DDT porte aussi un nom : il ne compte pas comme racine.
        bool field = false;
        for (const auto& dt : p.derivedTypes) field = field || std::find(dt.fields.begin(), dt.fields.end(), i) != dt.fields.end();
        if (!field && best == domain::kNoIndex) best = i;
    }
    if (best == domain::kNoIndex) return out;
    const auto& root = p.variables[best];
    out.variable = best;
    out.known = true;
    out.address = root.address.raw;
    if (root.scope == domain::VariableScope::Global) out.declared = "variable globale";
    else if (root.owner < p.pous.size()) out.declared = "locale \xC3\xA0 " + std::string(p.strings.text(p.pous[root.owner].name));
    else out.declared = "locale";
    std::string type(p.strings.text(root.type.name));
    if (parts.front().find('[') != std::string::npos) type = elementOf(type);
    for (std::size_t k = 1; k < parts.size(); ++k) {
        const auto member = lower(bare(parts[k]));
        const auto typeName = lower(type);
        std::string next;
        for (const auto& dt : p.derivedTypes) {
            if (lower(p.strings.text(dt.name)) != typeName) continue;
            for (const auto f : dt.fields)
                if (f < p.variables.size() && lower(p.strings.text(p.variables[f].name)) == member)
                    next = std::string(p.strings.text(p.variables[f].type.name));
        }
        if (next.empty()) { type.clear(); break; }
        type = parts[k].find('[') != std::string::npos ? elementOf(next) : next;
    }
    out.type = type;
    return out;
}

// Un texte tape en valeur, selon le type de la variable quand on le connait.
std::optional<sim::Value> parseValue(std::string text, const sim::Value* current) {
    text = trim(std::move(text));
    if (text.empty()) return std::nullopt;
    const auto u = upper(text);
    const auto type = current ? current->type() : sim::Type::Unknown;
    const bool yes = u == "TRUE" || u == "VRAI" || u == "ON";
    const bool no = u == "FALSE" || u == "FAUX" || u == "OFF";
    if (type == sim::Type::Bool) {
        if (yes || u == "1") return sim::Value::boolean(true);
        if (no || u == "0") return sim::Value::boolean(false);
        return std::nullopt;
    }
    if (yes) return sim::Value::boolean(true);
    if (no) return sim::Value::boolean(false);
    if (u.rfind("T#", 0) == 0) return sim::Value::time(std::atoll(text.c_str() + 2));
    if (type == sim::Type::String || (!text.empty() && text.front() == '\'')) {
        std::string s = text;
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
        return sim::Value::text(s);
    }
    std::string num = text;
    std::replace(num.begin(), num.end(), ',', '.');
    char* end = nullptr;
    const double d = std::strtod(num.c_str(), &end);
    if (!end || *end != '\0') return std::nullopt;
    if (type == sim::Type::Real || (type == sim::Type::Unknown && num.find('.') != std::string::npos)) return sim::Value::real(d);
    if (sim::isInteger(type)) return sim::Value::integer(type, static_cast<std::int64_t>(std::llround(d)));
    if (type == sim::Type::Time) return sim::Value::time(static_cast<std::int64_t>(std::llround(d)));
    return sim::Value::integer(sim::Type::DInt, static_cast<std::int64_t>(std::llround(d)));
}

const hmi::Variable* hmiVariable(const hmi::Document* doc, std::string_view name) {
    if (!doc) return nullptr;
    for (const auto& v : doc->project.programs.variables)
        if (v.name == name) return &v;
    return nullptr;
}

// ---- lot API 6 puis 7 : les membres d'une structure ---------------------------
//
// Ce qu'une ligne depliee montre dessous vient de project/MemberTree : les
// champs d'un DDT, les broches (et les publiques, les privees) d'une instance
// de bloc, les elements d'un tableau - par paquets de 100 au-dela de 100, une
// dimension apres l'autre -, a toute profondeur. Le chemin de chacun est celui
// que la simulation connait (Armoires[2].ana.PT1.mes, Grille[2,5], Tempo.Q).
namespace mt = project::members;

} // namespace

// ============================================================ les modeles ====
class AnimationTablesPane::TablesModel final : public ui::ITableModel {
public:
    explicit TablesModel(AnimationTablesPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t tables() const {
        const auto p = pane_.project();
        return p ? p->animationTables.size() : 0u;
    }
    [[nodiscard]] std::size_t rowCount() const override { return tables() + 1; }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? "Tables \xC2\xB7 " + std::to_string(tables()) : std::string("Lignes");
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        const auto p = pane_.project();
        if (!p || r >= p->animationTables.size()) return c == 0 ? std::string("+ Nouvelle table") : std::string{};
        const auto& t = p->animationTables[r];
        return c == 0 ? std::string(p->strings.text(t.name)) : std::to_string(t.entries.size());
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= tables()) {
            s.fgTone = ui::Tone::Muted;
            return s;
        }
        if (c == 0) {
            s.icon = ui::Icon::AnimationTable;
            s.iconTone = ui::Tone::Info;
        } else {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(RowIndex r, std::size_t c) const override { return c == 0 && r < tables(); }
    bool setCellText(RowIndex r, std::size_t c, std::string_view text) override {
        const auto p = pane_.project();
        if (c != 0 || !p || r >= p->animationTables.size()) return false;
        const std::string name = trim(std::string(text));
        if (name == p->strings.text(p->animationTables[r].name)) return true;
        if (const auto why = project::animationTableNameProblem(*p, name, r); !why.empty()) {
            if (pane_.hosts_.status) pane_.hosts_.status("Nom refus\xC3\xA9 : " + why);
            return false;
        }
        pane_.pendingSelect_ = name;
        pane_.hosts_.apply(std::make_unique<project::RenameAnimationTableCommand>(p, r, name));
        return true;
    }
private:
    AnimationTablesPane& pane_;
};

class AnimationTablesPane::LinesModel final : public ui::ITableModel {
public:
    explicit LinesModel(AnimationTablesPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case CSource: return "Source";
            case CName:   return "Variable";
            case CType:   return "Type";
            case CValue:  return "Valeur";
            case CForced: return "For\xC3\xA7" "age";
            case CNew:    return "Nouvelle valeur";
            default:      return "Lien / remarque";
        }
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        switch (c) {
            case CSource: return row.child ? std::string{} : row.hmi ? std::string("IHM") : std::string("API");
            case CName:   return row.name;
            case CType:   return row.group ? mt::groupType(row.node) : row.type.empty() ? std::string("?") : row.type;
            case CValue:  return r < pane_.live_.size() ? pane_.live_[r].text : std::string{};
            case CForced: {
                if (row.hmi) return {};
                auto* rt = pane_.hosts_.plc ? pane_.hosts_.plc() : nullptr;
                const std::string target = pane_.simName(row);
                if (!rt || !rt->isForced(target)) return {};
                sim::Value v;
                return rt->get(target, v) ? "forc\xC3\xA9" "e \xC3\xA0 " + v.display() : std::string("forc\xC3\xA9" "e");
            }
            case CNew: {
                const auto it = pane_.newValues_.find(pane_.keyOf(row));
                return it == pane_.newValues_.end() ? std::string{} : it->second;
            }
            default: return row.note;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        switch (c) {
            case CSource:
                s.bold = true;
                s.fgTone = row.hmi ? ui::Tone::Family1 : ui::Tone::Info;
                // Lot API 6 : la fleche d'une structure ; une fille, en retrait.
                s.indent = 12.f * static_cast<float>(row.depth);
                if (row.structure && !row.hmi) s.expander = pane_.expanded_.count(lower(row.node.key)) ? 1 : 0;
                break;
            case CName:
                s.monospace = true;
                if (!row.known) s.fgTone = ui::Tone::Warning;
                else if (row.child) s.fgTone = ui::Tone::Muted;
                break;
            case CType:
                s.monospace = true;
                s.fgTone = ui::Tone::Muted;
                break;
            case CValue:
                s.monospace = true;
                if (r < pane_.live_.size()) {
                    const auto& l = pane_.live_[r];
                    if (!l.ok) s.fgTone = ui::Tone::Muted;
                    else if (l.isBool) s.fgTone = l.truthy ? ui::Tone::Ok : ui::Tone::Muted;
                    // La valeur vient de changer : elle s'eclaire un instant.
                    if (l.flash > 0) {
                        s.bold = true;
                        s.bg = gfx::Color{236, 132, 38, static_cast<std::uint8_t>(20 + l.flash * 2)};
                    }
                }
                break;
            case CForced:
                s.bold = true;
                s.fgTone = ui::Tone::Warning;
                break;
            case CNew:
                s.monospace = true;
                s.fgTone = ui::Tone::Accent;
                break;
            default:
                s.fgTone = row.known ? ui::Tone::Muted : ui::Tone::Warning;
                break;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(RowIndex r, std::size_t c) const override { return c == CNew && r < pane_.rows_.size() && !pane_.rows_[r].group; }
    bool setCellText(RowIndex r, std::size_t c, std::string_view text) override {
        if (c != CNew || r >= pane_.rows_.size()) return false;
        const auto key = pane_.keyOf(pane_.rows_[r]);
        const auto t = trim(std::string(text));
        if (t.empty()) pane_.newValues_.erase(key);
        else pane_.newValues_[key] = t;
        return true;
    }
    [[nodiscard]] bool canDropRows(const std::vector<RowIndex>& from, RowIndex to, ui::TreeView::DropWhere where) const override {
        // Les lignes de la table se deplacent ; les filles suivent leur structure.
        if (from.size() != 1 || where == ui::TreeView::DropWhere::Into || to == from.front()) return false;
        if (from.front() >= pane_.rows_.size() || pane_.rows_[from.front()].child) return false;
        return to == ui::TableView::kNoRow || (to < pane_.rows_.size() && !pane_.rows_[to].child);
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        std::string tip = row.name + " \xC2\xB7 " + (row.type.empty() ? std::string("type inconnu") : row.type) + " \xC2\xB7 "
                        + (row.hmi ? "variable de l'IHM" : row.declared);
        if (row.structure && !row.hmi) tip += " \xC2\xB7 la fl\xC3\xA8" "che d\xC3\xA9plie ses membres";
        return tip;
    }
private:
    AnimationTablesPane& pane_;
};

// Le cadre de depot, par-dessus les lignes, pendant un glisser depuis l'arbre.
class AnimationTablesPane::DropOverlay final : public ui::Widget {
public:
    explicit DropOverlay(AnimationTablesPane& pane) : ui::Widget(pane.id() + ".depot"), pane_(pane) {}
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        if (!pane_.dropHint_) return;
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.strokeRect({b.x + 2.f, b.y + 2.f, b.w - 4.f, b.h - 4.f}, c.accent, 2.f);
        const std::string text = "L\xC3\xA2" "cher ici : ajouter \xC3\xA0 " + pane_.currentTableName();
        const float tw = ctx.r.measure(text, kSmall).width + 24.f;
        const gfx::Rect tag{b.x + (b.w - tw) * 0.5f, b.y + 8.f, tw, 24.f};
        ctx.r.fillRoundedRect(tag, c.accent, 4.f);
        ctx.r.drawText({tag.x + 12.f, tag.y + (tag.h - ctx.r.lineHeight(kSmall)) * 0.5f}, text, kSmall, c.textInverted);
        // Ce qu'on traine, sous la souris.
        if (!pane_.dropLabel_.empty()) {
            const float lw = ctx.r.measure(pane_.dropLabel_, kSmall).width + 34.f;
            const gfx::Rect chip{pane_.dropAt_.x + 14.f, pane_.dropAt_.y + 10.f, lw, 24.f};
            ctx.r.fillRoundedRect(chip, c.panelBg, 4.f);
            ctx.r.strokeRect(chip, c.accent, 1.f);
            ui::drawIcon(ctx.r, ui::Icon::Variable, {chip.x + 7.f, chip.y + 5.f, 14.f, 14.f}, c.accent);
            ctx.r.drawText({chip.x + 26.f, chip.y + (chip.h - ctx.r.lineHeight(kSmall)) * 0.5f}, pane_.dropLabel_, kSmall, c.text);
        }
    }
private:
    AnimationTablesPane& pane_;
};

// ================================================================ le volet ====
AnimationTablesPane::AnimationTablesPane(std::string id) : ui::Widget(std::move(id)) {
    tables_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".tables")));
    tablesModel_ = std::make_shared<TablesModel>(*this);
    tables_->setModel(tablesModel_);
    {
        ui::TableView::Column name{"Tables", 172.f, 60.f, true, false};
        ui::TableView::Column count{"Lignes", 80.f, 40.f, true, false};
        count.align = ui::Align::End;
        tables_->setColumns({name, count});
    }
    tables_->setSelectionMode(ui::SelectionMode::Single);
    links_ += tables_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
        if (syncing_ || rows.empty()) return;
        const auto p = project();
        const auto n = p ? p->animationTables.size() : 0u;
        if (rows.front() >= n) {
            runAction(ATable);
            return;
        }
        selectTable(static_cast<std::size_t>(rows.front()));
    });

    lines_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".lignes")));
    linesModel_ = std::make_shared<LinesModel>(*this);
    lines_->setModel(linesModel_);
    {
        std::vector<ui::TableView::Column> cols(CCount);
        const char* titles[] = {"Source", "Variable", "Type", "Valeur", "For\xC3\xA7" "age", "Nouvelle valeur", "Lien / remarque"};
        const float widths[] = {78.f, 270.f, 110.f, 110.f, 110.f, 130.f, 300.f};
        for (std::size_t i = 0; i < CCount; ++i) {
            cols[i].title = titles[i];
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        lines_->setColumns(std::move(cols));
    }
    lines_->setSelectionMode(ui::SelectionMode::Extended);
    lines_->setRowDragEnabled(true);
    lines_->setAlternatingRowColors(true);
    // Lot recherche : les filtres des colonnes (la table les applique) - ils
    // choisissent des lignes de la table ; les membres deplies suivent la leur.
    lines_->setColumnFiltersEnabled(true);
    links_ += lines_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (!syncing_) onLinesSelected();
    });
    links_ += lines_->rowsDropped->connect([this](const std::vector<RowIndex>& from, RowIndex to, ui::TableView::DropWhere where) {
        auto doc = project();
        if (!doc || from.size() != 1 || !current() || !hosts_.apply || from.front() >= rows_.size() || rows_[from.front()].child) return;
        // Des lignes de la VUE aux lignes de la TABLE : les filles depliees n'y sont pas.
        const auto count = current()->entries.size();
        const auto src = rows_[from.front()].entry;
        std::size_t dest = count;
        if (to != ui::TableView::kNoRow && to < rows_.size())
            dest = rows_[to].entry + (where == ui::TableView::DropWhere::After ? 1u : 0u);
        if (src >= count) return;
        if (dest > src) --dest;
        dest = std::min(dest, count - 1);
        if (dest == src) return;
        const std::string moved = rows_[from.front()].name;
        hosts_.apply(std::make_unique<project::MoveAnimationLineCommand>(doc, table_, src, dest));
        (void)selectLine(moved);
    });
    // Lot API 6 : une structure se deplie sur ses membres.
    links_ += lines_->expanderClicked->connect([this](RowIndex r) {
        if (r >= rows_.size() || !rows_[r].structure || rows_[r].hmi) return;
        const auto key = lower(rows_[r].node.key);
        if (!expanded_.erase(key)) expanded_.insert(key);
        const std::string keep = rows_[r].name;
        rebuildRows();
        (void)selectLine(keep);
    });
    // Lot API 6 : coller depuis Excel - des variables (une ligne chacune), et
    // leur nouvelle valeur ; un seul Ctrl+Z pour les lignes ajoutees.
    paste_.table = lines_;
    paste_.keyColumn = static_cast<int>(CName);
    paste_.target = [this](const ui::TableView::PasteRequest& rq) { return pasteTarget(rq); };
    paste_.refresh = [this] {
        if (hosts_.refreshViews) hosts_.refreshViews();
        refresh();
    };
    paste_.done = [this](const paste::Report& rep, const paste::Target& t) {
        pasteFresh_ = true;
        if (hosts_.status) hosts_.status(rep.status(t));
        if (!rep.createdKeys.empty()) (void)selectLine(rep.createdKeys.front());
        else if (!rep.updatedKeys.empty()) (void)selectLine(rep.updatedKeys.front());
    };
    paste::bind(paste_);
    links_ += lines_->cellEdited->connect([this](RowIndex, std::size_t, const std::string&, bool accepted) {
        if (accepted) lines_->invalidate();
    });

    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
    chart_ = &static_cast<HmiSeriesChart&>(addChild(std::make_unique<HmiSeriesChart>(this->id() + ".courbe")));
    overlay_ = &static_cast<DropOverlay&>(addChild(std::make_unique<DropOverlay>(*this)));
    overlay_->setVisibility(ui::Visibility::Collapsed);
}

AnimationTablesPane::~AnimationTablesPane() = default;

void AnimationTablesPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void AnimationTablesPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(ATable, HmiGlyph::Plus, "Cr\xC3\xA9" "er une table d'animation", "Table");
    t.add(ARename, HmiGlyph::Text, "Renommer la table choisie (sur place)", "Renommer");
    t.add(ADuplicate, HmiGlyph::Duplicate, "Dupliquer la table, avec ses lignes", "Dupliquer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer la table (Ctrl+Z la rend)", "Supprimer");
    t.separator();
    t.add(AAddApi, HmiGlyph::SystemVars, "Ajouter des variables de l'automate (API)", "Variable API");
    t.add(AAddHmi, HmiGlyph::View, "Ajouter des variables de l'IHM", "Variable IHM");
    t.add(ARemove, HmiGlyph::LayerRemove, "Retirer les lignes choisies de la table", "Retirer");
    t.separator();
    t.add(AWrite, HmiGlyph::InputField, "\xC3\x89" "crire la nouvelle valeur dans la simulation", "\xC3\x89" "crire\xE2\x80\xA6");
    t.add(AForce, HmiGlyph::Lock, "Forcer : la variable garde la nouvelle valeur (ou celle du moment)", "Forcer");
    t.add(AUnforce, HmiGlyph::Unlock, "D\xC3\xA9" "forcer les lignes choisies", "D\xC3\xA9" "forcer");
    t.add(AUnforceAll, HmiGlyph::Refresh, "Tout d\xC3\xA9" "forcer, dans toute la simulation", "Tout d\xC3\xA9" "forcer");
    const auto haveTable = [this] { return project() != nullptr && current() != nullptr; };
    t.setEnabledWhen(ARename, haveTable);
    t.setEnabledWhen(ADuplicate, haveTable);
    t.setEnabledWhen(ADelete, haveTable);
    t.setEnabledWhen(AAddApi, haveTable);
    t.setEnabledWhen(AAddHmi, [this, haveTable] { return haveTable() && hosts_.hmi && hosts_.hmi() != nullptr; });
    t.setVisibleWhen(AAddHmi, [] { return core::hasIhm(); });   // 1.12.0 : XPGAnalyser API n'a pas d'IHM
    t.setEnabledWhen(ATable, [this] { return project() != nullptr; });
    t.setEnabledWhen(ARemove, [this] { return !selectedLines().empty(); });
    t.setEnabledWhen(AWrite, [this] { return !selectedLines().empty(); });
    const auto apiSelected = [this] {
        if (!hosts_.plc || !hosts_.plc()) return false;
        for (const auto i : selectedLines())
            if (i < rows_.size() && !rows_[i].hmi) return true;
        return false;
    };
    t.setEnabledWhen(AForce, apiSelected);
    t.setEnabledWhen(AUnforce, apiSelected);
    t.setEnabledWhen(AUnforceAll, [this] {
        auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
        return rt && !rt->forcedNames().empty();
    });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    updateHint();
}

std::shared_ptr<domain::Project> AnimationTablesPane::project() const {
    return hosts_.project ? hosts_.project() : nullptr;
}

const domain::AnimationTable* AnimationTablesPane::current() const {
    const auto p = project();
    return p && table_ < p->animationTables.size() ? &p->animationTables[table_] : nullptr;
}

std::string AnimationTablesPane::currentTableName() const {
    const auto p = project();
    const auto* t = current();
    return p && t ? std::string(p->strings.text(t->name)) : std::string{};
}

std::size_t AnimationTablesPane::lineCount() const { return rows_.size(); }

std::string AnimationTablesPane::valueText(std::size_t row) const {
    return row < live_.size() ? live_[row].text : std::string{};
}

// ------------------------------------------------------------ rafraichir ----
void AnimationTablesPane::refresh() {
    // Une modification apres le collage : son bandeau ne dit plus vrai.
    if (!paste_.pasting) {
        if (pasteFresh_) pasteFresh_ = false;
        else paste::forget(paste_);
    }
    const auto p = project();
    const auto n = p ? p->animationTables.size() : 0u;
    if (!pendingSelect_.empty() && p) {
        for (std::size_t i = 0; i < n; ++i)
            if (p->strings.text(p->animationTables[i].name) == pendingSelect_) table_ = i;
        pendingSelect_.clear();
    }
    if (table_ >= n) table_ = n == 0 ? kNpos : (table_ == kNpos ? std::size_t{0} : n - 1);
    tablesModel_->modelReset->emit();
    {
        syncing_ = true;
        if (table_ != kNpos) tables_->selectModelRows({static_cast<RowIndex>(table_)}, false);
        syncing_ = false;
    }
    rebuildRows();
    refreshProperties();
    refreshChart();
    updateHint();
}

bool AnimationTablesPane::selectTable(std::size_t index) {
    const auto p = project();
    if (!p || index >= p->animationTables.size()) return false;
    const bool changed = index != table_;
    table_ = index;
    syncing_ = true;
    tables_->selectModelRows({static_cast<RowIndex>(index)}, false);
    syncing_ = false;
    if (changed) {
        rebuildRows();
        syncing_ = true;
        lines_->selectModelRows({}, false);
        syncing_ = false;
    }
    refreshProperties();
    refreshChart();
    updateHint();
    return true;
}

bool AnimationTablesPane::selectTable(std::string_view name) {
    const auto p = project();
    if (!p) return false;
    for (std::size_t i = 0; i < p->animationTables.size(); ++i)
        if (lower(p->strings.text(p->animationTables[i].name)) == lower(name)) return selectTable(i);
    return false;
}

bool AnimationTablesPane::selectLine(std::string_view name) {
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (pass == 0 ? rows_[i].name == name : lower(rows_[i].name) == lower(name)) {
                lines_->selectModelRows({static_cast<RowIndex>(i)}, true);
                return true;
            }
    return false;
}

bool AnimationTablesPane::setExpanded(std::string_view path, bool open) {
    // Le nom montre ("tempon[100 ... 149]") ou la cle ("tempon[100..149]", "Grille[2,*]").
    for (const auto& r : rows_)
        if ((lower(r.name) == lower(path) || lower(r.node.key) == lower(path)) && r.structure && !r.hmi) {
            const auto key = lower(r.node.key);
            if (open ? !expanded_.insert(key).second : expanded_.erase(key) == 0) return true;
            rebuildRows();
            return true;
        }
    return false;
}

bool AnimationTablesPane::isExpanded(std::string_view path) const {
    return expanded_.count(lower(path)) != 0;
}

std::vector<std::size_t> AnimationTablesPane::selectedEntries() const {
    std::vector<std::size_t> out;
    for (const auto i : selectedLines())
        if (i < rows_.size() && !rows_[i].child && rows_[i].entry != kNpos) out.push_back(rows_[i].entry);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::size_t> AnimationTablesPane::selectedLines() const {
    std::vector<std::size_t> out;
    if (!lines_ || !lines_->visible()) return out;
    for (const auto r : lines_->selectedModelRows())
        if (r < rows_.size()) out.push_back(static_cast<std::size_t>(r));
    std::sort(out.begin(), out.end());
    return out;
}

void AnimationTablesPane::rebuildRows() {
    rows_.clear();
    const auto p = project();
    const auto* t = current();
    const auto doc = hosts_.hmi ? hosts_.hmi() : nullptr;
    if (p && t) {
        for (std::size_t ei = 0; ei < t->entries.size(); ++ei) {
            const auto& e = t->entries[ei];
            Row r;
            r.entry = ei;
            r.name = std::string(p->strings.text(e.name));
            r.hmi = e.hmi;
            if (e.hmi) {
                r.declared = "Variables IHM";
                if (const auto* v = hmiVariable(doc.get(), r.name)) {
                    r.type = v->type;
                    r.linked = v->bound();
                    r.address = v->address;
                    if (!v->folder.empty()) r.declared += " / " + v->folder;
                    r.note = r.linked ? "lue sur l'automate (" + v->equipment + (v->address.empty() ? std::string{} : ", " + v->address) + ")"
                                      : std::string("locale \xC3\xA0 l'IHM");
                } else {
                    r.known = false;
                    r.note = doc ? "inconnue de l'IHM" : "le projet n'a pas d'IHM";
                }
            } else {
                const auto info = apiInfoOf(*p, r.name);
                r.known = info.known;
                r.type = info.type;
                r.declared = info.declared;
                r.address = info.address;
                if (info.variable < p->variables.size()) {
                    const auto& root = p->variables[info.variable];
                    if (root.scope != domain::VariableScope::Global && root.owner < p->pous.size())
                        r.scope = std::string(p->strings.text(p->pous[root.owner].name)) + ".";
                }
                r.node = mt::root(r.name, info.type);
                r.structure = info.known && !info.type.empty() && mt::hasChildren(*p, r.node);
                if (!info.known) r.note = "inconnue du programme";
                else if (doc) {
                    // L'IHM la lit-elle ? (une variable IHM liee a la meme adresse)
                    for (const auto& v : doc->project.programs.variables)
                        if (v.bound() && !v.address.empty() && !r.address.empty() && upper(v.address) == upper(r.address)) {
                            r.note = "l'IHM la lit (" + v.name + ", " + v.address + ")";
                            break;
                        }
                }
                if (r.note.empty() && r.structure)
                    r.note = upper(r.type).rfind("ARRAY", 0) == 0 ? std::string("tableau : la fl\xC3\xA8" "che d\xC3\xA9plie ses \xC3\xA9l\xC3\xA9ments")
                                                                 : std::string("structure : la fl\xC3\xA8" "che d\xC3\xA9plie ses membres");
                if (r.note.empty() && info.declared.rfind("adresse", 0) == 0)
                    r.note = "adresse directe : la simulation ne lit que les variables nomm\xC3\xA9" "es";
                if (r.note.empty() && !r.address.empty() && r.address.front() == '%' && r.declared.rfind("adresse", 0) != 0)
                    r.note = "situ\xC3\xA9" "e en " + r.address;
            }
            if (r.node.key.empty()) r.node = mt::root(r.name, r.type);
            const bool open = r.structure && !r.hmi && expanded_.count(lower(r.node.key));
            rows_.push_back(std::move(r));
            if (open) addChildren(*p, Row(rows_.back()), 1);
        }
    }
    live_.assign(rows_.size(), Live{});
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& l = live_[i];
        l.text = liveOf(rows_[i], l.truthy, l.isBool, l.ok);
    }
    linesModel_->modelReset->emit();
    lines_->setVisibility(t != nullptr && !rows_.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    invalidate();
}

void AnimationTablesPane::addChildren(const domain::Project& p, const Row& parent, int depth) {
    // Lot API 7 : sans limite de profondeur ni de nombre - un grand tableau
    // arrive par paquets, que l'on deplie a leur tour.
    for (auto& n : mt::children(p, parent.node)) {
        Row c;
        c.group = !n.real;
        c.name = n.real ? n.path : n.path + n.label;
        c.type = n.real ? n.type : std::string{};
        c.declared = n.what + " de " + parent.name;
        c.entry = parent.entry;
        c.depth = depth;
        c.child = true;
        c.scope = parent.scope;
        c.structure = mt::hasChildren(p, n);
        c.note = n.what;
        if (c.group) c.note += " \xC2\xB7 " + std::to_string(n.size()) + (n.size() > 1 ? " \xC3\xA9l\xC3\xA9ments" : " \xC3\xA9l\xC3\xA9ment");
        else if (c.structure) {
            const auto shape = mt::parseArray(n.type);
            c.note += shape.valid() ? " \xC2\xB7 tableau de " + std::to_string(shape.count()) + (shape.count() > mt::kChunk ? ", en paquets" : "")
                                    : std::string(" \xC2\xB7 structure");
        }
        c.node = std::move(n);
        const bool open = c.structure && expanded_.count(lower(c.node.key));
        rows_.push_back(c);
        if (open) addChildren(p, c, depth + 1);      // une copie : rows_ grandit dessous
    }
}

std::string AnimationTablesPane::simName(const Row& r) const {
    auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
    if (!rt || r.scope.empty()) return r.name;
    sim::Value v;
    return rt->get(r.name, v) || !rt->get(r.scope + r.name, v) ? r.name : r.scope + r.name;
}

std::string AnimationTablesPane::liveOf(const Row& r, bool& truthy, bool& isBool, bool& ok) const {
    truthy = isBool = ok = false;
    if (r.group) return {};                        // un paquet, une ligne : pas de valeur a lui
    if (!r.hmi) {
        auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
        if (!rt) return "\xE2\x80\x94";
        sim::Value v;
        if (rt->get(r.name, v) || (!r.scope.empty() && rt->get(r.scope + r.name, v))) {
            ok = true;
            isBool = v.type() == sim::Type::Bool;
            truthy = isBool && v.isTruthy();
            return v.display();
        }
        return r.structure ? std::string("{\xE2\x80\xA6}") : std::string("?");
    }
    if (auto* hr = hosts_.hmiRuntime ? hosts_.hmiRuntime() : nullptr)
        if (const auto* v = hr->variable(r.name)) {
            ok = true;
            isBool = v->type() == sim::Type::Bool;
            truthy = isBool && v->isTruthy();
            return v->display();
        }
    const auto doc = hosts_.hmi ? hosts_.hmi() : nullptr;
    if (const auto* v = hmiVariable(doc.get(), r.name)) return v->initial;
    return "\xE2\x80\x94";
}

void AnimationTablesPane::onLinesSelected() {
    refreshProperties();
    refreshChart();
}

void AnimationTablesPane::refreshProperties() {
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> cats;
    const auto p = project();
    const auto* t = current();
    const auto sel = selectedLines();
    const auto doc = hosts_.hmi ? hosts_.hmi() : nullptr;
    if (!p || !t) {
        PG::Category c;
        c.name = "Tables d'animation";
        c.properties.push_back({"Aucune table", "+ Table en cr\xC3\xA9" "e une", PG::ValueType::ReadOnly,
                                "Une table d'animation montre des variables pendant la simulation : leur valeur, leur for\xC3\xA7" "age.", {}, nullptr});
        cats.push_back(std::move(c));
    } else if (sel.size() == 1 && sel.front() < rows_.size()) {
        const auto& r = rows_[sel.front()];
        PG::Category c;
        c.name = "Variable " + r.name;
        c.properties.push_back({"Source", r.hmi ? "IHM" : "API (l'automate)", PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"Type", r.type.empty() ? std::string("?") : r.type, PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"D\xC3\xA9" "clar\xC3\xA9" "e dans", r.declared.empty() ? std::string("?") : r.declared, PG::ValueType::ReadOnly, {}, {}, nullptr});
        if (!r.address.empty()) c.properties.push_back({"Adresse", r.address, PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"Valeur", sel.front() < live_.size() ? live_[sel.front()].text : std::string{}, PG::ValueType::ReadOnly,
                                "La valeur au dernier cycle de la simulation (\xE2\x80\x94 : la simulation n'est pas pr\xC3\xAAte).", {}, nullptr});
        cats.push_back(std::move(c));
        if (!r.hmi && r.known && r.declared.rfind("adresse", 0) != 0) {
            // Qui l'ecrit : les references de la racine, gardees si elles touchent cette ligne.
            const auto root = r.name.substr(0, std::min(r.name.find('.'), r.name.find('[')));
            const auto xr = project::crossReference(*p, root);
            const auto me = lower(r.name);
            PG::Category w;
            w.name = "Qui l'\xC3\xA9" "crit";
            for (const auto& ref : xr.writes) {
                const auto path = lower(ref.path);
                if (!(path == me || path.rfind(me + ".", 0) == 0 || me.rfind(path + ".", 0) == 0 || path == lower(root))) continue;
                w.properties.push_back({ref.sectionName + " : " + std::to_string(ref.line), ref.text, PG::ValueType::ReadOnly, ref.condition, {}, nullptr});
                if (w.properties.size() >= 8) break;
            }
            if (w.properties.empty())
                w.properties.push_back({"Personne", "aucune section ne l'\xC3\xA9" "crit (une entr\xC3\xA9" "e, une constante, ou l'IHM)",
                                        PG::ValueType::ReadOnly, {}, {}, nullptr});
            cats.push_back(std::move(w));
        }
        PG::Category h;
        h.name = "IHM";
        if (r.hmi) {
            if (const auto* v = hmiVariable(doc.get(), r.name)) {
                h.properties.push_back({"Valeur initiale", v->initial, PG::ValueType::ReadOnly, {}, {}, nullptr});
                if (!v->description.empty()) h.properties.push_back({"Description", v->description, PG::ValueType::ReadOnly, {}, {}, nullptr});
                h.properties.push_back({"Liaison", v->bound() ? v->equipment + " \xC2\xB7 " + v->address : std::string("locale : elle vit dans l'IHM"),
                                        PG::ValueType::ReadOnly,
                                        v->bound() ? "Pendant la simulation, l'IHM la lit sur l'automate : les deux valeurs doivent dire la m\xC3\xAAme chose."
                                                   : "\xC3\x89" "crire change sa valeur dans l'IHM en marche ; la forcer la tient.",
                                        {}, nullptr});
            }
        } else {
            std::string readers;
            if (doc && !r.address.empty())
                for (const auto& v : doc->project.programs.variables)
                    if (v.bound() && upper(v.address) == upper(r.address)) readers += (readers.empty() ? "" : ", ") + v.name;
            h.properties.push_back({"Lue par l'IHM", readers.empty() ? std::string("non") : readers, PG::ValueType::ReadOnly,
                                    readers.empty() ? std::string{} : "L'IHM lit la m\xC3\xAAme valeur \xC3\xA0 " + r.address + " : pendant la simulation, les deux colonnes doivent dire la m\xC3\xAAme chose.",
                                    {}, nullptr});
        }
        cats.push_back(std::move(h));
    } else {
        PG::Category c;
        const std::string name(p->strings.text(t->name));
        c.name = "Table " + name;
        auto doc2 = p;
        const auto index = table_;
        c.properties.push_back({"Nom", name, PG::ValueType::Text, "Un identifiant Control Expert : une lettre, puis lettres, chiffres et _.", {},
                                [this, doc2, index](std::string_view v) {
                                    const std::string n = trim(std::string(v));
                                    if (!project::animationTableNameProblem(*doc2, n, index).empty()) return false;
                                    pendingSelect_ = n;
                                    hosts_.apply(std::make_unique<project::RenameAnimationTableCommand>(doc2, index, n));
                                    return true;
                                }});
        std::vector<std::string> units;
        for (const auto& pou : p->pous)
            if (pou.kind == domain::PouKind::ProgramUnit) units.push_back(std::string(p->strings.text(pou.name)));
        const std::string owner(p->strings.text(t->owner));
        if (!units.empty())
            c.properties.push_back({"Unit\xC3\xA9", owner.empty() ? units.front() : owner, PG::ValueType::Enum,
                                    "L'unit\xC3\xA9 de programme qui porte la table dans l'export (Control Expert range les tables par unit\xC3\xA9).",
                                    units, [this, doc2, index](std::string_view v) {
                                        hosts_.apply(std::make_unique<project::SetAnimationTableOwnerCommand>(doc2, index, std::string(v)));
                                        return true;
                                    }});
        std::size_t api = 0, ihm = 0;
        for (const auto& e : t->entries) (e.hmi ? ihm : api) += 1;
        c.properties.push_back({"Lignes", std::to_string(t->entries.size()), PG::ValueType::ReadOnly, {}, {}, nullptr});
        c.properties.push_back({"De l'automate", std::to_string(api), PG::ValueType::ReadOnly, "Elles partent vers Control Expert avec l'export.", {}, nullptr});
        c.properties.push_back({"De l'IHM", std::to_string(ihm), PG::ValueType::ReadOnly,
                                "Elles restent dans le dossier du projet (tables/animation.txt) : Control Expert ne conna\xC3\xAEt pas l'IHM.", {}, nullptr});
        cats.push_back(std::move(c));
    }
    props_->setCategories(std::move(cats));
}

void AnimationTablesPane::refreshChart() {
    if (!chart_) return;
    const auto sel = selectedLines();
    auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
    if (sel.size() != 1 || sel.front() >= rows_.size()) {
        chartKey_.clear();
        samples_.clear();
        chart_->setData({}, 0.0, 1.0, "Choisis une ligne : sa courbe se trace ici pendant la simulation.");
        return;
    }
    const auto& r = rows_[sel.front()];
    const auto key = keyOf(r);
    if (key != chartKey_) {
        chartKey_ = key;
        samples_.clear();
        if (!r.hmi && rt) rt->watch(simName(r));
    }
    HmiSeriesChart::Series s;
    s.name = r.name;
    if (!r.hmi && rt) {
        if (const auto* h = rt->history(simName(r)))
            for (const auto& smp : *h) s.points.push_back({static_cast<double>(smp.clockMs) / 1000.0, smp.value});
        s.forced = rt->isForced(simName(r));
    } else if (r.hmi) {
        if (auto* hr = hosts_.hmiRuntime ? hosts_.hmiRuntime() : nullptr)
            if (const auto* v = hr->variable(r.name)) {
                const double at = hr->now();
                lastHmiSample_ = at;
                if (samples_.empty() || samples_.back().first < at) samples_.push_back({at, v->asReal()});
                while (samples_.size() > 120) samples_.pop_front();
            }
        s.points.assign(samples_.begin(), samples_.end());
    }
    const bool running = hosts_.running && hosts_.running();
    const double from = s.points.empty() ? 0.0 : s.points.front().first;
    double to = s.points.empty() ? 1.0 : s.points.back().first;
    if (to <= from) to = from + 1.0;
    std::vector<HmiSeriesChart::Series> series;
    if (!s.points.empty()) series.push_back(std::move(s));
    chart_->setData(std::move(series), from, to,
                    running ? std::string("La courbe commence au prochain cycle.")
                            : std::string("Lance la simulation (F9) : la ligne choisie se trace ici, cycle apr\xC3\xA8s cycle."));
}

void AnimationTablesPane::updateHint() {
    if (!frame_) return;
    const bool running = hosts_.running && hosts_.running();
    std::string text;
    ui::Tone tone = ui::Tone::None;
    if (!project()) {
        text = "Le projet est un export lu tel quel : enregistre-le en dossier pour cr\xC3\xA9" "er des tables.";
    } else if (!current()) {
        text = "Aucune table : \xC2\xAB Table \xC2\xBB en cr\xC3\xA9" "e une ; une variable gliss\xC3\xA9" "e depuis l'arbre s'y ajoute.";
    } else {
        const auto n = rows_.size();
        auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
        const auto scans = rt ? rt->scanCount() : 0u;
        const bool hmiLive = hosts_.hmiRuntime && hosts_.hmiRuntime();
        std::string state = running ? std::string("Simulation en marche")
                          : scans > 0 ? "Automate arr\xC3\xAAt\xC3\xA9 au cycle " + std::to_string(scans) + " (la barre dit pourquoi)"
                                      : std::string("Simulation arr\xC3\xAAt\xC3\xA9" "e");
        if (hmiLive && !running) state += " \xC2\xB7 l'IHM tourne";
        text = state + " \xC2\xB7 table " + currentTableName() + " \xC2\xB7 " + std::to_string(n) + (n == 1 ? " ligne" : " lignes")
             + (running || hmiLive ? " \xC2\xB7 les valeurs se lisent \xC3\xA0 chaque cycle"
                : scans > 0 ? " \xC2\xB7 \xC3\x89" "crire et Forcer restent possibles" : " \xC2\xB7 F9 la lance : les valeurs vivront ici");
        tone = running || hmiLive ? ui::Tone::Ok : scans > 0 ? ui::Tone::Warning : ui::Tone::None;
    }
    if (text == hintShown_) return;
    hintShown_ = text;
    frame_->setHint(text, tone);
}

void AnimationTablesPane::tick() {
    if (!visible() || !lines_) return;
    if (pendingRename_ && current()) {
        gfx::Rect r{};
        if (tables_->rowRect(table_, r)) {
            pendingRename_ = false;
            (void)tables_->beginCellEdit(static_cast<RowIndex>(table_), 0);
        }
    }
    bool changed = false;
    for (std::size_t i = 0; i < rows_.size() && i < live_.size(); ++i) {
        auto& l = live_[i];
        bool truthy = false, isBool = false, ok = false;
        const auto text = liveOf(rows_[i], truthy, isBool, ok);
        if (text != l.text) {
            if (l.ok && ok) l.flash = 30;
            l.text = text;
            changed = true;
        } else if (l.flash > 0) {
            --l.flash;
            changed = true;
        }
        l.truthy = truthy;
        l.isBool = isBool;
        l.ok = ok;
    }
    if (changed) lines_->invalidate();
    auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
    const std::uint64_t scan = rt ? rt->scanCount() : 0u;
    // La courbe : a chaque cycle de l'automate ; une ligne de l'IHM, au temps
    // de l'IHM (elle vit meme quand l'automate est arrete).
    auto* hr = hosts_.hmiRuntime ? hosts_.hmiRuntime() : nullptr;
    const bool hmiLine = !chartKey_.empty() && chartKey_.rfind("ihm:", 0) == 0;
    if (scan != lastScan_ || (hmiLine && hr && hr->now() - lastHmiSample_ >= 0.25)) {
        lastScan_ = scan;
        refreshChart();
    }
    updateHint();
}

// ---------------------------------------------------------------- agir ----
bool AnimationTablesPane::addLines(std::vector<project::AnimationLine> lines, std::size_t table) {
    auto doc = project();
    if (table == kNpos) table = table_;
    if (!doc || table >= doc->animationTables.size() || lines.empty() || !hosts_.apply) return false;
    // Rien de neuf : le dire, sans commande vide dans l'historique.
    const auto& t = doc->animationTables[table];
    std::size_t fresh = 0;
    for (const auto& l : lines) {
        bool present = false;
        for (const auto& e : t.entries) present = present || (e.hmi == l.hmi && doc->strings.text(e.name) == l.name);
        fresh += present ? 0u : 1u;
    }
    if (fresh == 0) {
        if (hosts_.status) hosts_.status(lines.size() == 1 ? lines.front().name + " est d\xC3\xA9j\xC3\xA0 dans la table." : "Ces variables sont d\xC3\xA9j\xC3\xA0 dans la table.");
        return false;
    }
    if (table != table_) pendingSelect_ = std::string(doc->strings.text(t.name));
    hosts_.apply(std::make_unique<project::AddAnimationLinesCommand>(doc, table, std::move(lines)));
    return true;
}

void AnimationTablesPane::writeSelected(bool force) {
    auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
    auto* hr = hosts_.hmiRuntime ? hosts_.hmiRuntime() : nullptr;
    std::size_t done = 0;
    std::string refused;
    for (const auto i : selectedLines()) {
        const auto& r = rows_[i];
        if (r.group) continue;                     // un paquet ne s'ecrit pas
        const auto it = newValues_.find(keyOf(r));
        const std::string text = it == newValues_.end() ? std::string{} : it->second;
        if (!r.hmi) {
            if (!rt) { refused = "la simulation n'est pas pr\xC3\xAAte : F9 la lance"; continue; }
            sim::Value cur;
            const std::string target = simName(r);
            const bool has = rt->get(target, cur);
            if (text.empty()) {
                if (force && has && rt->force(target, cur)) ++done;
                else if (!force) refused = "tape d'abord la nouvelle valeur (colonne Nouvelle valeur)";
                continue;
            }
            const auto v = parseValue(text, has ? &cur : nullptr);
            if (!v) { refused = "\xC2\xAB " + text + " \xC2\xBB ne se lit pas comme une valeur de " + r.name; continue; }
            if (force ? rt->force(target, *v) : rt->set(target, *v)) ++done;
            else refused = r.name + " : inconnue de la simulation";
        } else {
            if (!hr) { refused = "l'IHM ne tourne pas (IHM > Simulation la lance)"; continue; }
            // 1.11.5 : une variable IHM se force aussi (le moteur de l'IHM la tient, il ignore les ecritures).
            if (force && text.empty()) {
                if (const auto* cur = hr->variable(r.name); cur && hr->forceVariable(r.name, *cur)) ++done;
                else refused = r.name + " : pas une variable IHM qui tourne";
                continue;
            }
            if (text.empty()) { refused = "tape d'abord la nouvelle valeur (colonne Nouvelle valeur)"; continue; }
            const auto v = parseValue(text, hr->variable(r.name));
            if (!v) { refused = "\xC2\xAB " + text + " \xC2\xBB ne se lit pas comme une valeur de " + r.name; continue; }
            if (force) {
                std::string why;
                if (hr->forceVariable(r.name, *v, &why)) ++done;
                else refused = why;
            } else if (hr->environment().write(r.name, *v)) ++done;
            else refused = r.name + " : l'IHM refuse l'\xC3\xA9" "criture";
        }
    }
    if (hosts_.status) {
        std::string msg;
        if (done) msg = std::to_string(done) + (force ? (done == 1 ? " variable forc\xC3\xA9" "e" : " variables forc\xC3\xA9" "es")
                                                       : (done == 1 ? " valeur \xC3\xA9" "crite" : " valeurs \xC3\xA9" "crites"))
                      + " dans la simulation.";
        if (!refused.empty()) msg += (msg.empty() ? "" : " ") + std::string("Refus : ") + refused + ".";
        if (!msg.empty()) hosts_.status(msg);
    }
    tick();
}

void AnimationTablesPane::runAction(int action) {
    auto doc = project();
    const auto status = [this](const std::string& s) { if (hosts_.status) hosts_.status(s); };
    switch (action) {
        case ATable: {
            if (!doc || !hosts_.apply) { status("Enregistre d'abord le projet en dossier : un export lu tel quel ne se modifie pas."); return; }
            const auto name = project::freeAnimationTableName(*doc, "Nouvelle_table");
            const auto before = doc->animationTables.size();
            pendingSelect_ = name;
            pendingRename_ = true;
            hosts_.apply(std::make_unique<project::AddAnimationTableCommand>(doc, name, project::defaultAnimationTableOwner(*doc)));
            // Refusee (un projet LOCK) : rien a renommer - surtout pas la table choisie.
            if (doc->animationTables.size() == before) {
                pendingSelect_.clear();
                pendingRename_ = false;
            }
            return;
        }
        case ARename:
            // Lot 7 : le bouton ouvre le dialogue du renommage (un double-clic
            // sur le nom renomme toujours sur place) ; sans lui, sur place.
            if (current() && requestRename("table", currentTableName())) return;
            if (current()) {
                tables_->selectModelRows({static_cast<RowIndex>(table_)}, false);
                (void)tables_->beginCellEdit(static_cast<RowIndex>(table_), 0);
            }
            return;
        case ADuplicate: {
            const auto* t = current();
            if (!doc || !t) return;
            std::vector<project::AnimationLine> lines;
            for (const auto& e : t->entries) lines.push_back({std::string(doc->strings.text(e.name)), e.hmi});
            const auto name = project::freeAnimationTableName(*doc, currentTableName() + "_copie");
            pendingSelect_ = name;
            hosts_.apply(std::make_unique<project::AddAnimationTableCommand>(doc, name, std::string(doc->strings.text(t->owner)),
                                                                             std::move(lines), table_ + 1, true));
            return;
        }
        case ADelete:
            if (doc && current()) hosts_.apply(std::make_unique<project::RemoveAnimationTableCommand>(doc, table_));
            return;
        case AAddApi:
        case AAddHmi:
            if (current() && hosts_.pick) hosts_.pick(action == AAddHmi);
            return;
        case ARemove: {
            auto sel = selectedEntries();
            if (!doc) return;
            if (sel.empty()) {
                if (!selectedLines().empty()) status("Un membre ne se retire pas seul : choisis la ligne de sa structure.");
                return;
            }
            hosts_.apply(std::make_unique<project::RemoveAnimationLinesCommand>(doc, table_, std::move(sel)));
            return;
        }
        case AWrite: writeSelected(false); return;
        case AForce: writeSelected(true); return;
        case AUnforce: {
            auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
            if (!rt) return;
            std::size_t n = 0;
            for (const auto i : selectedLines())
                if (!rows_[i].hmi && rt->unforce(simName(rows_[i]))) ++n;
            status(n ? std::to_string(n) + (n == 1 ? " variable d\xC3\xA9" "forc\xC3\xA9" "e." : " variables d\xC3\xA9" "forc\xC3\xA9" "es.")
                     : std::string("Rien \xC3\xA0 d\xC3\xA9" "forcer dans les lignes choisies."));
            tick();
            return;
        }
        case AUnforceAll: {
            auto* rt = hosts_.plc ? hosts_.plc() : nullptr;
            if (!rt) return;
            const auto n = rt->forcedNames().size();
            rt->unforceAll();
            status(std::to_string(n) + (n == 1 ? " variable d\xC3\xA9" "forc\xC3\xA9" "e" : " variables d\xC3\xA9" "forc\xC3\xA9" "es") + " dans toute la simulation.");
            tick();
            return;
        }
        default: return;
    }
}

// ---------------------------------------------- lot API 6 : coller depuis Excel ----
//
// Une colonne de noms collee cree une ligne par nom (API, ou IHM si la colonne
// Source le dit) ; un nom deja la (une ligne, ou le membre d'une structure
// depliee) prend la Nouvelle valeur collee a cote. Un nom que ni le programme
// ni l'IHM ne connait est refuse, avec sa raison : une faute de frappe ne
// devient pas une ligne "inconnue".
paste::Target AnimationTablesPane::pasteTarget(const ui::TableView::PasteRequest& rq) {
    paste::Target t;
    t.noun = "ligne";
    t.nouns = "lignes";
    t.feminine = true;
    const auto rowOf = [this](const std::string& key) -> std::size_t {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (lower(rows_[i].name) == lower(key)) return i;
        return kNpos;
    };
    // Une ligne de la table, meme pas encore montree (creee plus haut dans ce collage).
    const auto inTable = [this](const std::string& key) {
        const auto doc = project();
        const auto* tb = current();
        if (!doc || !tb) return false;
        for (const auto& e : tb->entries)
            if (lower(doc->strings.text(e.name)) == lower(key)) return true;
        return false;
    };
    const auto computed = [](std::string title, std::vector<std::string> aliases, int col) {
        return paste::column(std::move(title), std::move(aliases), col, nullptr);
    };
    t.columns.push_back(computed("Source", {"Origine", "API/IHM"}, static_cast<int>(CSource)));
    t.columns.push_back(paste::column("Variable", {"Nom", "Name", "Mnemonique", "Symbole", "Chemin", "Adresse"}, static_cast<int>(CName), nullptr, true));
    t.columns.push_back(computed("Type", {"Type de donnee", "DataType"}, static_cast<int>(CType)));
    t.columns.push_back(computed("Valeur", {"Value", "Valeur courante"}, static_cast<int>(CValue)));
    t.columns.push_back(computed("For\xC3\xA7" "age", {"Forcage", "Force"}, static_cast<int>(CForced)));
    t.columns.push_back(paste::column("Nouvelle valeur", {"Nouvelle", "A ecrire", "Consigne", "New value"}, static_cast<int>(CNew),
                                      [this, rowOf](const std::string& key, const std::string& value, std::string* why) {
                                          const auto i = rowOf(key);
                                          const bool hmi = i != kNpos && rows_[i].hmi;
                                          const std::string k = (hmi ? "ihm:" : "api:") + (i != kNpos ? rows_[i].name : key);
                                          const auto v = trim(value);
                                          if (v.empty()) return true;           // une case vide ne change rien
                                          if (v == "-" || v == "\xE2\x80\x94") newValues_.erase(k);
                                          else newValues_[k] = v;
                                          (void)why;
                                          return true;
                                      }));
    t.columns.push_back(computed("Lien / remarque", {"Lien", "Remarque", "Note"}, static_cast<int>(CNote)));
    t.exists = [rowOf, inTable](const std::string& key) { return rowOf(key) != kNpos || inTable(key); };
    t.unknown = "introuvable";
    t.freeKey = [](const std::string& key) { return key; };
    t.create = [this](const std::string& key, const std::map<std::string, std::string>& cells, paste::Notes&, std::vector<std::string>& used,
                      std::string* why) -> std::string {
        const auto doc = project();
        if (!doc || !current()) {
            if (why) *why = doc ? "choisis d'abord une table" : "le projet ne se modifie pas";
            return {};
        }
        bool hmi = false;
        if (const auto it = cells.find("source"); it != cells.end()) {
            const auto src = lower(trim(it->second));
            hmi = src == "ihm" || src == "hmi";
        }
        used.push_back("source");
        const auto name = trim(key);
        if (hmi) {
            const auto h = hosts_.hmi ? hosts_.hmi() : nullptr;
            if (!hmiVariable(h.get(), name)) {
                if (why) *why = h ? "inconnue de l'IHM" : "le projet n'a pas d'IHM";
                return {};
            }
        } else if (!apiInfoOf(*doc, name).known) {
            if (why) *why = "inconnue du programme (ni variable, ni membre, ni adresse %)";
            return {};
        }
        if (!addLines({{name, hmi}})) {
            if (why) *why = "d\xC3\xA9j\xC3\xA0 dans la table";
            return {};
        }
        if (const auto it = cells.find("nouvellevaleur"); it != cells.end() && !trim(it->second).empty())
            newValues_[(hmi ? "ihm:" : "api:") + name] = trim(it->second);
        used.push_back("nouvellevaleur");
        return name;
    };
    for (std::size_t i = rq.anchorViewRow; i < lines_->visibleRowCount(); ++i) {
        const auto r = lines_->viewRow(i);
        if (r < rows_.size()) t.keysFromAnchor.push_back(rows_[r].name);
    }
    return t;
}

ui::EventResult AnimationTablesPane::onEvent(const ui::InputEvent& ev) {
    // La table choisie est vide : ses lignes sont cachees derriere le cadre
    // "Glisse des variables ici" - Ctrl+V y colle quand meme.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::V && k->mods.ctrl && !k->mods.alt && !k->repeat
        && current() != nullptr && !lines_->visible())
        return lines_->pasteFromClipboard(k->mods.shift) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    return ui::EventResult::Ignored;
}

// ------------------------------------------------------------- deposer ----
bool AnimationTablesPane::dropZone(gfx::Point p) const {
    return visible() && current() != nullptr && linesArea_.contains(p);
}

void AnimationTablesPane::setDropHint(bool on, std::string label, gfx::Point at) {
    if (on == dropHint_ && label == dropLabel_ && at.x == dropAt_.x && at.y == dropAt_.y) return;
    dropHint_ = on;
    dropLabel_ = std::move(label);
    dropAt_ = at;
    overlay_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    overlay_->invalidate();
    invalidate();
}

// ------------------------------------------------------------- dessiner ----
void AnimationTablesPane::onLayout() {
    const auto b = bounds();
    const float leftW = std::clamp(b.w * 0.18f, 200.f, 272.f);
    const float rightW = std::clamp(b.w * 0.24f, 240.f, 380.f);
    tables_->setBounds({b.x, b.y, leftW, b.h});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
    const gfx::Rect centre{b.x + leftW + 1.f, b.y, std::max(0.f, b.w - leftW - rightW - 2.f), b.h};
    const float chartH = centre.h > 420.f ? std::min(280.f, centre.h * 0.38f) : 0.f;
    linesArea_ = {centre.x, centre.y, centre.w, centre.h - chartH};
    lines_->setBounds(linesArea_);
    overlay_->setBounds(linesArea_);
    chartArea_ = {centre.x, linesArea_.bottom(), centre.w, chartH};
    chart_->setVisibility(chartH > 0.f ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    chart_->setBounds({chartArea_.x + 10.f, chartArea_.y + 34.f, std::max(0.f, chartArea_.w - 20.f), std::max(0.f, chartArea_.h - 44.f)});
}

void AnimationTablesPane::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(bounds(), c.windowBg);
    r.fillRect({linesArea_.x - 1.f, bounds().y, 1.f, bounds().h}, c.border);
    r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, c.border);
    // La table vide : ou glisser.
    if (!lines_->visible()) {
        const gfx::Rect zone{linesArea_.x + linesArea_.w * 0.5f - 280.f, linesArea_.y + linesArea_.h * 0.5f - 90.f, 560.f, 180.f};
        r.fillRoundedRect(zone, c.panelBg, 6.f);
        for (float x = zone.x; x < zone.right(); x += 10.f) {
            r.fillRect({x, zone.y, 5.f, 1.f}, c.accent);
            r.fillRect({x, zone.bottom() - 1.f, 5.f, 1.f}, c.accent);
        }
        for (float y = zone.y; y < zone.bottom(); y += 10.f) {
            r.fillRect({zone.x, y, 1.f, 5.f}, c.accent);
            r.fillRect({zone.right() - 1.f, y, 1.f, 5.f}, c.accent);
        }
        ui::drawIcon(r, ui::Icon::AnimationTable, {zone.x + zone.w * 0.5f - 16.f, zone.y + 30.f, 32.f, 32.f}, c.accent);
        const bool any = current() != nullptr;
        const std::string title = any ? "Glisse des variables ici" : "Aucune table d'animation";
        const std::string l1 = any ? "depuis API \xE2\x80\xBA Variables ou IHM \xE2\x80\xBA Variables IHM,"
                                   : "\xC2\xAB Table \xC2\xBB, dans la barre, en cr\xC3\xA9" "e une ;";
        const std::string l2 = any ? "avec \xC2\xAB Variable API \xC2\xBB / \xC2\xAB Variable IHM \xC2\xBB, ou colle une liste d'Excel (Ctrl+V)."
                                   : "une variable gliss\xC3\xA9" "e depuis l'arbre sur une table s'y ajoute.";
        const gfx::FontId big{17};
        r.drawText({zone.x + (zone.w - r.measure(title, big).width) * 0.5f, zone.y + 76.f}, title, big, c.text);
        r.drawText({zone.x + (zone.w - r.measure(l1, kSmall).width) * 0.5f, zone.y + 108.f}, l1, kSmall, c.textMuted);
        r.drawText({zone.x + (zone.w - r.measure(l2, kSmall).width) * 0.5f, zone.y + 128.f}, l2, kSmall, c.textMuted);
    }
    // Le titre de la courbe.
    if (chart_->visible()) {
        r.fillRect({chartArea_.x, chartArea_.y, chartArea_.w, 1.f}, c.border);
        std::string title = "LA LIGNE CHOISIE, EN COURBE";
        const auto sel = selectedLines();
        if (sel.size() == 1 && sel.front() < rows_.size()) {
            title += "  \xC2\xB7  " + rows_[sel.front()].name;
            if (!chart_->series().empty()) title += "  \xC2\xB7  " + std::to_string(chart_->series().front().points.size()) + " derniers cycles lus";
        }
        ui::drawIcon(r, ui::Icon::Chart, {chartArea_.x + 12.f, chartArea_.y + 10.f, 14.f, 14.f}, c.textMuted);
        r.drawText({chartArea_.x + 32.f, chartArea_.y + 9.f}, title, kSmall, c.textMuted);
    }
}

} // namespace app
