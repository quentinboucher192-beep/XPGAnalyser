// app/VariablesPane.cpp - les variables et les sous-routines de l'API (lot API 5).
#include "VariablesPane.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : XPGAnalyser API - pas d'IHM

#include "ApiPanes.hpp"
#include "hmi/HmiPanels.hpp"

#include "../project/EditCommands.hpp"
#include "../project/RenameCommands.hpp"
#include "../project/MacroSpec.hpp"
#include "../project/ApiCommands.hpp"
#include "RenameDialog.hpp"               // lot 7 : requestRename (le dialogue qui montre tout)
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>

namespace app {

using ui::RowIndex;
using PG = ui::PropertyGrid;
using namespace apikit;
namespace usage = project::usage;
namespace mt = project::members;

namespace {

constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
constexpr int kNewTable = 9999;
constexpr int kTableBase = 10000;

// Lot API 7 : le retrait d'une ligne sous un titre de genre, puis par niveau
// de membre (la fleche d'un enfant tombe sous l'icone de son parent).
constexpr float kVariableIndent = 18.f;
constexpr float kMemberIndent = 16.f;

// L'allure d'un membre : son icone dit sa nature (un tableau, une structure,
// un bloc, une variable), sa couleur son sens (une entree, une sortie...) ; un
// paquet, une ligne d'un tableau rangent des elements, en retrait.
void memberLook(const domain::Project& p, const mt::Node& node, ui::Icon& icon, ui::Tone& tone) {
    if (!node.real) {
        icon = node.what == "ligne" ? ui::Icon::AnimationTable : ui::Icon::Folder;
        tone = ui::Tone::Muted;
        return;
    }
    switch (mt::natureOf(p, node.type)) {
        case mt::Nature::Array:     icon = ui::Icon::AnimationTable; break;
        case mt::Nature::Structure: icon = ui::Icon::DerivedType; break;
        case mt::Nature::Block:     icon = ui::Icon::FunctionBlock; break;
        default:                    icon = ui::Icon::Variable; break;
    }
    tone = ui::Tone::None;
    if (node.what == "entr\xC3\xA9" "e")             tone = ui::Tone::Input;
    else if (node.what == "sortie")                    tone = ui::Tone::Output;
    else if (node.what == "entr\xC3\xA9" "e-sortie")  tone = ui::Tone::InOut;
    else if (node.what == "publique")                  tone = ui::Tone::Info;
    else if (node.what == "priv\xC3\xA9" "e")         tone = ui::Tone::Muted;
}

// Le chemin montre d'un noeud : celui d'un membre ; un paquet, une ligne :
// celui du tableau suivi de leur libelle ("tempon[100 ... 149]").
std::string shownPath(const mt::Node& n) { return n.real ? n.path : n.path + n.label; }

// Ce chemin passe-t-il par `n` pour arriver a `want` (en minuscules) ? Un
// membre : `want` est lui, ou commence par lui suivi de "." ou "[". Un paquet
// ou une ligne : l'indice de `want` dans leur dimension est dans leur plage,
// les indices deja fixes sont les leurs.
bool leadsTo(const mt::Node& n, const std::string& want) {
    const auto base = lower(n.path);
    if (want.size() < base.size() || want.compare(0, base.size(), base) != 0) return false;
    if (n.real) return want.size() == base.size() || want[base.size()] == '.' || want[base.size()] == '[';
    if (want.size() <= base.size() || want[base.size()] != '[') return false;
    const auto close = want.find(']', base.size());
    if (close == std::string::npos) return false;
    // "[2,5]" (ou "[2, 5]") : les indices, un par dimension.
    std::vector<std::int64_t> indices;
    std::size_t from = base.size() + 1;
    while (from <= close) {
        const auto comma = std::min(want.find(',', from), close);
        std::size_t k = from;
        while (k < comma && want[k] == ' ') ++k;
        const bool negative = k < comma && want[k] == '-';
        if (k < comma && (want[k] == '-' || want[k] == '+')) ++k;
        const auto digits = k;
        std::int64_t value = 0;
        while (k < comma && want[k] >= '0' && want[k] <= '9' && value < 100000000000000LL)
            value = value * 10 + (want[k++] - '0');
        const bool any = k > digits;
        while (k < comma && want[k] == ' ') ++k;
        if (!any || k != comma) return false;
        indices.push_back(negative ? -value : value);
        from = comma + 1;
    }
    if (n.dim >= indices.size() || n.fixed.size() > n.dim) return false;
    for (std::size_t d = 0; d < n.fixed.size(); ++d)
        if (indices[d] != n.fixed[d]) return false;
    return indices[n.dim] >= n.first && indices[n.dim] <= n.last;
}

// Un chemin tape (un script, une recherche) sous la forme d'une cle : sans la
// casse ni les espaces, et les points de suspension d'un libelle montre remis
// comme dans la cle - "tempon[100 ... 149]" est le paquet tempon[100..149],
// "Grille[2, ...]" la ligne grille[2,*]. Les cles n'ont jamais d'espace.
std::string keyForm(std::string_view path) {
    std::string s = lower(path);
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    static const std::string kDots = "\xE2\x80\xA6";
    for (auto at = s.find(kDots); at != std::string::npos; at = s.find(kDots, at)) {
        const auto after = at + kDots.size();
        const bool row = at > 0 && s[at - 1] == ',' && after < s.size() && s[after] == ']';
        s.replace(at, kDots.size(), row ? "*" : "..");
    }
    return s;
}

// Ce qui mene a une cle, pour leadsTo : un paquet ("t[100..149]", "b[0,100..199]")
// par son premier indice, une ligne ("g[2,*]") par ses indices fixes. Un
// membre ou un element : sa cle elle-meme.
std::string probeOf(const std::string& key) {
    if (key.empty() || key.back() != ']') return key;
    const auto open = key.rfind('[');
    if (open == std::string::npos) return key;
    std::string inside = key.substr(open + 1, key.size() - open - 2);
    if (const auto dots = inside.find(".."); dots != std::string::npos) inside.erase(dots);
    else if (inside.size() > 2 && inside.compare(inside.size() - 2, 2, ",*") == 0) inside.erase(inside.size() - 2);
    else return key;
    return key.substr(0, open + 1) + inside + "]";
}

// La cle du parent d'un membre, sans le projet : "armoires[0].sorties" ->
// "armoires[0]" -> "armoires" -> "" (une variable). Les ".." d'un paquet
// sont entre crochets : ils ne coupent pas. Un element range dans un paquet
// remonte au tableau (le paquet n'est pas dans sa cle) : assez pour choisir
// la ligne la plus proche qui reste montree.
std::string parentKey(const std::string& key) {
    int depth = 0;
    for (std::size_t i = key.size(); i-- > 0;) {
        const char c = key[i];
        if (c == ']') ++depth;
        else if (c == '[' && --depth == 0) return key.substr(0, i);
        else if (c == '.' && depth == 0) return key.substr(0, i);
    }
    return {};
}

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

PG::Property ro(std::string name, std::string value, std::string description = {}) {
    return {std::move(name), std::move(value), PG::ValueType::ReadOnly, std::move(description), {}, nullptr};
}

ui::Icon genreIcon(int g) {
    switch (static_cast<usage::Genre>(g)) {
        case usage::Genre::DfbInstance:   return ui::Icon::FunctionBlock;
        case usage::Genre::StandardBlock: return ui::Icon::Task;
        case usage::Genre::DdtInstance:   return ui::Icon::DerivedType;
        case usage::Genre::Located:       return ui::Icon::LocatedVariable;
        default:                          return ui::Icon::Variable;
    }
}

std::string csvCell(const std::string& s) {
    if (s.find_first_of(";\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

} // namespace

// ======================================================================
//                               VARIABLES
// ======================================================================
class VariablesPane::Model final : public ui::ITableModel {
public:
    explicit Model(VariablesPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, CType, CAddress, CUsed, CHmi, CWhere, CComment, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Nom", "Type", "Adresse", "Utilis\xC3\xA9" "e", "IHM", "O\xC3\xB9", "Commentaire"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.group)
            return c == CName ? std::string(usage::genreLabel(static_cast<usage::Genre>(row.genre))) + "   " + std::to_string(row.count) : std::string{};
        // Lot API 7 : un membre - son libelle sous sa variable (".PT1", "[3]"),
        // son type, ce qu'il est ; rien d'autre (il n'a ni adresse ni usages a lui).
        if (row.member) {
            switch (c) {
                case CName:    return row.node.label;
                case CType:    return row.pack ? mt::groupType(row.node) : row.node.type;
                case CComment: return row.note;
                default:       return {};
            }
        }
        const auto& i = pane_.infos_[row.info];
        switch (c) {
            case CName: return i.name;
            case CType: return i.type;
            case CAddress: return i.address.empty() ? std::string("\xE2\x80\x94") : i.address;
            case CUsed: return std::to_string(i.sections);
            case CHmi: return i.hmi ? "lue " + std::to_string(i.hmi->uses) + "\xC3\x97" : std::string{};
            case CWhere: return i.where.empty() ? std::string("\xE2\x80\x94") : i.where;
            default: return i.comment;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        if (row.group) {
            if (c == CName) {
                s.spanRow = true;
                s.bold = true;
                s.expander = pane_.collapsed_.count(row.genre) ? 0 : 1;
                s.icon = genreIcon(row.genre);
                s.iconTone = ui::Tone::Family3;
            }
            return s;
        }
        if (row.member) {
            switch (c) {
                case CName:
                    s.indent = kVariableIndent + kMemberIndent * static_cast<float>(row.depth);
                    s.expander = row.structure ? (row.open ? 1 : 0) : -1;
                    s.icon = row.icon;
                    s.iconTone = row.tone;
                    s.monospace = true;
                    if (row.pack) s.fgTone = ui::Tone::Muted;
                    break;
                case CType:
                    s.monospace = true;
                    s.fgTone = ui::Tone::Muted;
                    break;
                case CComment: s.fgTone = ui::Tone::Muted; break;
                default: break;
            }
            return s;
        }
        const auto& i = pane_.infos_[row.info];
        switch (c) {
            case CName:
                s.indent = kVariableIndent;
                // Lot API 7 : la fleche d'une variable qui a des membres.
                s.expander = row.structure ? (row.open ? 1 : 0) : -1;
                s.icon = genreIcon(static_cast<int>(i.genre));
                s.iconTone = i.sections ? ui::Tone::Info : ui::Tone::Muted;
                s.monospace = true;
                break;
            case CType: s.monospace = true; s.fgTone = ui::Tone::Muted; break;
            case CAddress: s.monospace = true; s.fgTone = i.address.empty() ? ui::Tone::Muted : ui::Tone::None; break;
            case CUsed: if (!i.sections) s.fgTone = ui::Tone::Warning, s.bold = true; break;
            case CHmi: if (i.hmi) s.fgTone = ui::Tone::Family1, s.bold = true; break;
            case CWhere: case CComment: s.fgTone = ui::Tone::Muted; break;
            default: break;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size() || pane_.rows_[r].group) return {};
        const auto& row = pane_.rows_[r];
        const auto& i = pane_.infos_[row.info];
        if (row.member) {
            std::string tip = shownPath(row.node) + " \xC2\xB7 " + (row.pack ? mt::groupType(row.node) : row.node.type);
            if (row.structure) tip += " \xC2\xB7 la fl\xC3\xA8" "che d\xC3\xA9plie ce qu'il contient";
            return tip + " \xC2\xB7 double-clic : o\xC3\xB9 " + i.name + " est \xC3\xA9" "crite et lue.";
        }
        const std::string unfold = row.structure ? std::string(" La fl\xC3\xA8" "che d\xC3\xA9plie ses membres.") : std::string{};
        if (!i.sections && i.hmi) return "Le programme ne la nomme pas, mais l'IHM la lit : ne pas la supprimer sans regarder." + unfold;
        if (!i.sections) return "Aucune section ne la nomme." + unfold;
        return "Double-clic : o\xC3\xB9 elle est \xC3\xA9" "crite et lue, ligne par ligne." + unfold;
    }
private:
    VariablesPane& pane_;
};

namespace {
// Lot recherche : le texte d'une variable dans une colonne, tel que la table le
// montre (les filtres des colonnes le lisent sans passer par une ligne).
template <class Info>
std::string variableCell(const Info& i, std::size_t c) {
    switch (c) {
        case 0: return i.name;
        case 1: return i.type;
        case 2: return i.address.empty() ? std::string("\xE2\x80\x94") : i.address;
        case 3: return std::to_string(i.sections);
        case 4: return i.hmi ? "lue " + std::to_string(i.hmi->uses) + "\xC3\x97" : std::string{};
        case 5: return i.where.empty() ? std::string("\xE2\x80\x94") : i.where;
        default: return i.comment;
    }
}
} // namespace

VariablesPane::VariablesPane(std::string id) : ui::Widget(std::move(id)) {
    filters_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(this->id() + ".filtres", "Rechercher : nom, type, adresse, commentaire\xE2\x80\xA6")));
    links_ += filters_->changed->connect([this] {
        // Lot recherche : la recherche lue une fois, et surlignee dans les cases.
        query_ = ui::SearchQuery(filters_->search());
        if (table_) table_->setHighlight(filters_->search());
        rebuildRows();
        updateHint();
    });
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".variables")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    {
        std::vector<ui::TableView::Column> cols(Model::CCount);
        const float widths[] = {270.f, 210.f, 100.f, 80.f, 70.f, 300.f, 220.f};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = model_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        cols[Model::CUsed].align = ui::Align::End;
        cols[Model::CHmi].visible = core::hasIhm();      // 1.12.0 : XPGAnalyser API n'a pas d'IHM
        table_->setColumns(std::move(cols));
    }
    // Lot recherche : LES FILTRES DES COLONNES. Le volet les applique lui-meme
    // (Host) : ils choisissent des variables - les titres de genre gardent leur
    // compte, un genre replie reste replie, les membres suivent leur variable.
    table_->setColumnFiltersEnabled(true);
    table_->setColumnFilterMode(ui::TableView::ColumnFilterMode::Host);
    table_->setColumnValuesProvider([this](std::size_t col, const std::function<void(const std::string&)>& emit) {
        // Les valeurs des variables que gardent la pastille, la recherche et les
        // AUTRES filtres : cocher une valeur ne montre jamais une liste vide.
        for (const auto& i : infos_)
            if (passes(i, static_cast<int>(col))) emit(variableCell(i, col));
    });
    links_ += table_->columnFiltersChanged->connect([this] {
        rebuildRows();
        updateHint();
    });
    table_->setSelectionMode(ui::SelectionMode::Extended);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (syncing_) return;
        refreshProperties();
    });
    links_ += table_->expanderClicked->connect([this](RowIndex r) {
        if (r >= rows_.size()) return;
        if (rows_[r].group) {
            if (!collapsed_.erase(rows_[r].genre)) collapsed_.insert(rows_[r].genre);
            rebuildRows();
            return;
        }
        // Lot API 7 : une variable ou un membre se deplie sur ses membres ; la
        // ligne reste choisie (la table l'a choisie au clic sur la fleche).
        if (!rows_[r].structure) return;
        const auto key = lower(keyOf(rows_[r]));
        if (!expanded_.erase(key)) expanded_.insert(key);
        keep_ = keyOf(rows_[r]);
        rebuildRows();
    });
    links_ += table_->activated->connect([this](RowIndex r) {
        // Un membre : les usages de sa variable (on ne cherche pas un membre seul).
        if (r < rows_.size() && !rows_[r].group) runAction(AUsages);
    });
    // Lot API 6 : coller depuis Excel - les colonnes reconnues par leur titre
    // (Nom, Type, Adresse, Commentaire, Valeur initiale), un nom inconnu cree la
    // variable, un nom connu la met a jour ; un seul Ctrl+Z pour tout le collage.
    paste_.table = table_;
    paste_.keyColumn = static_cast<int>(Model::CName);
    paste_.target = [this](const ui::TableView::PasteRequest& rq) { return pasteTarget(rq); };
    paste_.refresh = [this] {
        if (hosts_.request) hosts_.request("rafraichir");
        refresh();
    };
    paste_.done = [this](const paste::Report& rep, const paste::Target& t) {
        pasteFresh_ = true;
        if (hosts_.status) hosts_.status(rep.status(t));
        if (!rep.createdKeys.empty()) (void)selectVariable(rep.createdKeys.front());
        else if (!rep.updatedKeys.empty()) (void)selectVariable(rep.updatedKeys.front());
    };
    paste::bind(paste_);
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(this->id() + ".tables")));
    links_ += menu_->itemChosen->connect([this](int choice) {
        // Lot API 7 : les membres choisis aussi (armoires[0].ana.PT1), sous
        // leur chemin - celui que la simulation connait.
        auto names = selectedPaths();
        if (names.empty() || !hosts_.addToTable) return;
        if (choice == kNewTable) hosts_.addToTable(std::move(names), kNpos);
        else if (choice >= kTableBase) hosts_.addToTable(std::move(names), static_cast<std::size_t>(choice - kTableBase));
    });
    // Lot API 8 : la recherche et la pastille retenues, des maintenant - une
    // variable demandee aussitot (Aller a...) les voit, et les efface si elles la cachent.
    filters_->recall();
}

VariablesPane::~VariablesPane() = default;

void VariablesPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void VariablesPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAdd, HmiGlyph::Plus, "Cr\xC3\xA9" "er une variable globale", "Variable");
    t.add(ARename, HmiGlyph::Text, "Renommer la variable choisie : le code et les tables d'animation suivent", "Renommer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer la variable choisie (Ctrl+Z la rend)", "Supprimer");
    t.separator();
    t.add(AAddToTable, HmiGlyph::Table, "Ajouter les variables (ou les membres) choisis \xC3\xA0 une table d'animation (une existante ou une nouvelle)",
          "Ajouter \xC3\xA0 une table d'animation \xE2\x96\xBE");
    t.separator();
    t.add(AUsages, HmiGlyph::Search, "Voir les usages : o\xC3\xB9 elle est \xC3\xA9" "crite et lue, ligne par ligne", "Voir les usages");
    t.add(AExportCsv, HmiGlyph::Export, "Exporter les variables montr\xC3\xA9" "es en CSV (s\xC3\xA9parateur ;), dans le dossier du projet", "Exporter CSV");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    // Une seule ligne, et une variable : un membre ne se renomme ni ne se supprime seul.
    const auto one = [this] {
        const auto rows = selectedRows();
        return rows.size() == 1 && !rows_[rows.front()].member;
    };
    t.setEnabledWhen(AAdd, editable);
    t.setEnabledWhen(ARename, [editable, one] { return editable() && one(); });
    t.setEnabledWhen(ADelete, [editable, one] { return editable() && one(); });
    t.setEnabledWhen(AAddToTable, [this, editable] { return editable() && !selectedPaths().empty(); });
    t.setEnabledWhen(AUsages, [this] { return usageInfo() != nullptr; });
    t.setEnabledWhen(AExportCsv, [this] { return !infos_.empty(); });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    updateHint();
}

// ---- lot API 6 : coller depuis Excel ---------------------------------------------
paste::Target VariablesPane::pasteTarget(const ui::TableView::PasteRequest& rq) {
    using Field = project::SetVariableFieldCommand::Field;
    paste::Target t;
    t.noun = "variable";
    t.nouns = "variables";
    t.feminine = true;
    const auto indexOf = [this](const std::string& key) -> domain::Index {
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        if (!p) return domain::kNoIndex;
        for (domain::Index i = 0; i < p->variables.size(); ++i)
            if (p->variables[i].scope == domain::VariableScope::Global && lower(text(*p, p->variables[i].name)) == lower(key)) return i;
        return domain::kNoIndex;
    };
    const auto setter = [this, indexOf](Field f) {
        return [this, indexOf, f](const std::string& key, const std::string& value, std::string* why) -> bool {
            const auto doc = hosts_.project ? hosts_.project() : nullptr;
            const auto i = indexOf(key);
            if (!doc || i == domain::kNoIndex) {
                if (why) *why = doc ? "variable introuvable" : "le projet ne se modifie pas";
                return false;
            }
            const auto& v = doc->variables[i];
            std::string now;
            switch (f) {
                case Field::Type: now = text(*doc, v.type.name); break;
                case Field::Address: now = v.address.raw; break;
                case Field::InitValue: now = text(*doc, v.initValue); break;
                case Field::Comment: now = text(*doc, v.comment); break;
                case Field::Effective: break;
            }
            std::string val = value;
            if (f == Field::Address && (val == "-" || val == "\xE2\x80\x94")) val.clear();
            if (val == now) return true;
            if (f == Field::Type && !apikit::validPlcType(*doc, val, why)) return false;
            if (f == Field::Address && !apikit::validAddress(val, why)) return false;
            hosts_.apply(std::make_unique<project::SetVariableFieldCommand>(doc, i, f, val));
            return true;
        };
    };
    const auto computed = [](std::string title, std::vector<std::string> aliases, int col) {
        return paste::column(std::move(title), std::move(aliases), col, nullptr);
    };
    t.columns.push_back(paste::column("Nom", {"Name", "Variable", "Mnemonique", "Symbole", "Identifiant"}, static_cast<int>(Model::CName), nullptr, true));
    t.columns.push_back(paste::column("Type", {"Type de donnee", "DataType", "Data type"}, static_cast<int>(Model::CType), setter(Field::Type)));
    t.columns.push_back(paste::column("Adresse", {"Address", "Topologie", "Adresse topologique"}, static_cast<int>(Model::CAddress), setter(Field::Address)));
    t.columns.push_back(computed("Utilis\xC3\xA9" "e", {"Utilisee", "Used"}, static_cast<int>(Model::CUsed)));
    t.columns.push_back(computed("IHM", {"HMI"}, static_cast<int>(Model::CHmi)));
    t.columns.push_back(computed("O\xC3\xB9", {"Ou", "Sections", "Where"}, static_cast<int>(Model::CWhere)));
    t.columns.push_back(paste::column("Commentaire", {"Comment", "Description", "Libelle", "Designation"}, static_cast<int>(Model::CComment), setter(Field::Comment)));
    t.columns.push_back(paste::column("Valeur initiale", {"Initiale", "Initial", "Init", "Initial value", "Valeur par defaut"}, -1, setter(Field::InitValue)));
    t.exists = [indexOf](const std::string& key) { return indexOf(key) != domain::kNoIndex; };
    t.unknown = "introuvable";
    t.freeKey = [this](const std::string& key) {
        std::vector<std::string> taken;
        for (const auto& i : infos_) taken.push_back(i.name);
        return apikit::freeName(key, taken);
    };
    t.create = [this, indexOf](const std::string& key, const std::map<std::string, std::string>& cells, paste::Notes& notes,
                               std::vector<std::string>& used, std::string* why) -> std::string {
        const auto doc = hosts_.project ? hosts_.project() : nullptr;
        if (!doc) {
            if (why) *why = "le projet ne se modifie pas";
            return {};
        }
        if (!project::macro::isIdentifier(key)) {
            if (why) *why = "\xC2\xAB " + key + " \xC2\xBB n'est pas un nom IEC (une lettre, puis lettres, chiffres, _)";
            return {};
        }
        project::AddVariableCommand::Spec spec;
        spec.name = key;
        spec.type = "BOOL";
        spec.scope = domain::VariableScope::Global;
        if (const auto it = cells.find("type"); it != cells.end() && !it->second.empty()) {
            std::string w;
            if (apikit::validPlcType(*doc, it->second, &w)) spec.type = it->second;
            else notes.push_back({"Type", w + " \xE2\x80\x94 la variable est cr\xC3\xA9\xC3\xA9" "e en BOOL"});
        }
        used.push_back("type");
        if (const auto it = cells.find("adresse"); it != cells.end() && !it->second.empty()) {
            std::string w;
            if (apikit::validAddress(it->second, &w)) spec.address = it->second;
            else notes.push_back({"Adresse", w});
            used.push_back("adresse");
        }
        if (const auto it = cells.find("commentaire"); it != cells.end()) {
            spec.comment = it->second;
            used.push_back("commentaire");
        }
        if (const auto it = cells.find("valeurinitiale"); it != cells.end()) {
            spec.initValue = it->second;
            used.push_back("valeurinitiale");
        }
        hosts_.apply(std::make_unique<project::AddVariableCommand>(doc, std::move(spec)));
        if (indexOf(key) == domain::kNoIndex) {
            if (why) *why = "refus\xC3\xA9" "e par le projet";
            return {};
        }
        return key;
    };
    // Sans la colonne Nom : les lignes collees vont dans les variables montrees,
    // a partir de la ligne choisie (les titres de groupe et, lot API 7, les
    // membres deplies sautes : ce ne sont pas des variables).
    for (std::size_t i = rq.anchorViewRow; i < table_->visibleRowCount(); ++i) {
        const auto r = table_->viewRow(i);
        if (r < rows_.size() && !rows_[r].group && !rows_[r].member && rows_[r].info < infos_.size())
            t.keysFromAnchor.push_back(infos_[rows_[r].info].name);
    }
    return t;
}

void VariablesPane::refresh() {
    // Une modification apres le collage : son bandeau ne dit plus vrai.
    if (!paste_.pasting) {
        if (pasteFresh_) pasteFresh_ = false;
        else paste::forget(paste_);
    }
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    // La ligne choisie (une variable ou un membre), avant que les variables
    // ne soient relues : elle le reste.
    if (keep_.empty()) keep_ = selectionKey();
    infos_.clear();
    memberRows_.clear();          // lot API 7 : un type a pu changer
    reads_ = hosts_.hmiReads ? hosts_.hmiReads() : std::map<std::string, ApiHmiRead>{};
    unused_ = readByHmi_ = 0;
    if (p) {
        const usage::Where where(*p);
        for (domain::Index i = 0; i < p->variables.size(); ++i) {
            const auto& v = p->variables[i];
            if (v.scope != domain::VariableScope::Global) continue;
            Info info;
            info.index = i;
            info.name = text(*p, v.name);
            info.type = text(*p, v.type.name);
            info.address = v.address.raw;
            info.comment = text(*p, v.comment);
            info.sections = where.sectionsOf(info.name).size();
            info.where = where.sectionNames(info.name, 2);
            info.genre = usage::genreOf(*p, v);
            // Lot API 7 : une fois par rafraichissement, pas a chaque frappe du filtre.
            info.structure = mt::hasChildren(*p, mt::root(info.name, info.type));
            infos_.push_back(std::move(info));
        }
        std::sort(infos_.begin(), infos_.end(), [](const Info& a, const Info& b) { return lower(a.name) < lower(b.name); });
        for (auto& info : infos_) {
            const auto it = reads_.find(lower(info.name));
            info.hmi = it == reads_.end() ? nullptr : &it->second;
            unused_ += info.sections ? 0u : 1u;
            readByHmi_ += info.hmi ? 1u : 0u;
        }
    }
    std::size_t located = 0, dfb = 0;
    for (const auto& i : infos_) {
        located += i.address.empty() ? 0u : 1u;
        dfb += i.genre == usage::Genre::DfbInstance ? 1u : 0u;
    }
    if (core::hasIhm())
        filters_->setChips({{"Toutes", infos_.size()}, {"Situ\xC3\xA9" "es", located}, {"Instances de DFB", dfb},
                            {"Pas utilis\xC3\xA9" "es", unused_}, {"Lues par l'IHM", readByHmi_}});
    else      // 1.12.0 : XPGAnalyser API - pas d'IHM (la puce « Lues par l'IHM », la derniere, tombe)
        filters_->setChips({{"Toutes", infos_.size()}, {"Situ\xC3\xA9" "es", located}, {"Instances de DFB", dfb},
                            {"Pas utilis\xC3\xA9" "es", unused_}});
    rebuildRows();
    updateHint();
}

bool VariablesPane::passes(const Info& i, int skipColumn) const {
    switch (filters_->current()) {
        case 1: if (i.address.empty()) return false; break;
        case 2: if (i.genre != usage::Genre::DfbInstance) return false; break;
        case 3: if (i.sections) return false; break;
        case 4: if (!i.hmi) return false; break;
        default: break;
    }
    // Lot recherche : chaque mot (ou "phrase") dans le nom, le type, l'adresse,
    // le COMMENTAIRE ou les sections ; aucun -mot exclu. Puis les filtres des colonnes.
    if (!query_.matches({i.name, i.type, i.address, i.comment, i.where})) return false;
    return !table_ || ui::ColumnFilter::acceptsAll(table_->columnFilters(), [&](std::size_t c) { return variableCell(i, c); }, skipColumn);
}

void VariablesPane::showVariable(const Info& i) {
    // Seulement ce qui la cache (chaque changement de filtre refait les lignes) :
    // la pastille d'abord, puis la recherche si elle la cache encore, puis (lot
    // recherche) les filtres des colonnes.
    if (!passes(i)) {
        filters_->setCurrent(0);
        if (!passes(i)) filters_->setSearch("");
        if (!passes(i)) table_->clearColumnFilters();
    }
    collapsed_.erase(static_cast<int>(i.genre));
}

void VariablesPane::rebuildRows() {
    if (keep_.empty()) keep_ = selectionKey();
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    rows_.clear();
    std::size_t kept = 0;                 // lot recherche : "37 sur 251"
    for (int g = 0; g < 5; ++g) {
        std::vector<std::size_t> members;
        for (std::size_t k = 0; k < infos_.size(); ++k)
            if (static_cast<int>(infos_[k].genre) == g && passes(infos_[k])) members.push_back(k);
        kept += members.size();
        if (members.empty()) continue;
        Row head;
        head.group = true;
        head.genre = g;
        head.count = members.size();
        rows_.push_back(head);
        if (collapsed_.count(g)) continue;
        for (const auto k : members) {
            Row r;
            r.genre = g;
            r.info = k;
            // Lot API 7 : une variable qui a des membres porte sa fleche ; depliee,
            // ses membres suivent (et ceux des membres deplies, sans limite).
            r.structure = p && infos_[k].structure;
            if (r.structure) {
                r.node = mt::root(infos_[k].name, infos_[k].type);
                r.decl = infos_[k].index;
                r.open = expanded_.count(lower(r.node.key)) != 0;
            }
            rows_.push_back(std::move(r));
            if (rows_.back().open) addMembers(*p, Row(rows_.back()), 1);     // une copie : rows_ grandit dessous
        }
    }
    table_->setColumnFilterCounts(kept, infos_.size());
    model_->modelReset->emit();
    syncing_ = true;
    // La ligne gardee (une variable ou un membre) ; sans ligne gardee, la
    // premiere variable, comme avant. Repliee sous un parent, c'est lui (le plus
    // proche qui reste montre).
    std::size_t pick = kNpos;
    if (keep_.empty()) {
        for (std::size_t i = 0; i < rows_.size() && pick == kNpos; ++i)
            if (!rows_[i].group && !rows_[i].member) pick = i;
    } else {
        for (auto want = keyForm(keep_); !want.empty() && pick == kNpos; want = parentKey(want))
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (!rows_[i].group && lower(keyOf(rows_[i])) == want) {
                    pick = i;
                    break;
                }
    }
    // Rien a garder (la variable cachee par un filtre, supprimee) : aucune ligne.
    // La table garde sinon les rangs d'avant, qui designent d'autres lignes - et
    // Renommer, Supprimer agiraient sur elles.
    table_->selectModelRows(pick != kNpos ? std::vector<RowIndex>{static_cast<RowIndex>(pick)} : std::vector<RowIndex>{}, false);
    syncing_ = false;
    keep_.clear();
    refreshProperties();
}

void VariablesPane::addMembers(const domain::Project& p, const Row& parent, int depth) {
    // Sans limite de profondeur ni de nombre : un grand tableau arrive par
    // paquets de 100, que l'on deplie a leur tour. Seules les lignes depliees
    // sont faites ; les membres d'un parent se calculent une fois (memberRows_).
    auto known = memberRows_.find(lower(keyOf(parent)));
    if (known == memberRows_.end()) {
        std::vector<Row> kids;
        for (auto& n : mt::children(p, parent.node)) {
            Row c;
            c.member = true;
            c.pack = !n.real;
            c.structure = mt::hasChildren(p, n);
            c.decl = mt::declarationOf(p, parent.node, parent.decl, n);
            memberLook(p, n, c.icon, c.tone);
            // Ce qu'il est (un champ, une entree, un element...), et ce que le
            // projet en dit : le commentaire du champ, de la broche, de l'element.
            c.comment = mt::commentOf(p, c.decl, parent.node, n);
            c.note = c.comment.empty() ? n.what : n.what + " \xC2\xB7 " + c.comment;
            c.node = std::move(n);
            kids.push_back(std::move(c));
        }
        known = memberRows_.emplace(lower(keyOf(parent)), std::move(kids)).first;
    }
    // Un std::map ne deplace pas ses elements : la liste reste la quand les
    // niveaux dessous ajoutent les leurs.
    for (const auto& proto : known->second) {
        Row c = proto;
        c.genre = parent.genre;
        c.info = parent.info;
        c.depth = depth;
        c.open = c.structure && expanded_.count(lower(c.node.key)) != 0;
        rows_.push_back(c);
        if (c.open) addMembers(p, c, depth + 1);
    }
}

std::string VariablesPane::keyOf(const Row& r) const {
    if (r.group) return {};
    if (r.member) return r.node.key;
    return r.info < infos_.size() ? infos_[r.info].name : std::string{};   // la cle d'une racine : son nom
}

std::vector<std::size_t> VariablesPane::selectedRows() const {
    std::vector<std::size_t> out;
    for (const auto r : table_->selectedModelRows())
        if (r < rows_.size() && !rows_[r].group && rows_[r].info < infos_.size()) out.push_back(static_cast<std::size_t>(r));
    std::sort(out.begin(), out.end());
    return out;
}

std::string VariablesPane::selectionKey() const {
    const auto rows = selectedRows();
    return rows.empty() ? std::string{} : keyOf(rows_[rows.front()]);
}

std::vector<const VariablesPane::Info*> VariablesPane::selectedInfos() const {
    // Les variables seules : un membre ne se renomme ni ne se supprime.
    std::vector<const Info*> out;
    for (const auto r : selectedRows())
        if (!rows_[r].member) out.push_back(&infos_[rows_[r].info]);
    return out;
}

std::vector<std::string> VariablesPane::selectedPaths() const {
    std::vector<std::string> out;
    for (const auto r : selectedRows()) {
        const auto& row = rows_[r];
        if (row.pack) continue;                 // un paquet n'est pas une variable
        std::string path = row.member ? row.node.path : infos_[row.info].name;
        if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
    }
    return out;
}

const VariablesPane::Info* VariablesPane::usageInfo() const {
    const auto rows = selectedRows();
    return rows.size() == 1 ? &infos_[rows_[rows.front()].info] : nullptr;
}

std::string VariablesPane::selectedPath() const {
    const auto rows = selectedRows();
    if (rows.size() != 1) return {};
    const auto& row = rows_[rows.front()];
    return row.member ? shownPath(row.node) : infos_[row.info].name;
}

bool VariablesPane::locate(const domain::Project& p, std::string_view path, const Info*& info, mt::Node& node,
                           std::vector<std::string>& chain) const {
    info = nullptr;
    chain.clear();
    const auto want = keyForm(path);
    const auto rootName = want.substr(0, want.find_first_of(".["));
    if (rootName.empty()) return false;
    for (const auto& i : infos_)
        if (lower(i.name) == rootName) {
            info = &i;
            break;
        }
    if (!info) return false;
    // De la variable a la ligne, un niveau apres l'autre : l'enfant de cette cle,
    // sinon celui qui y mene (un membre, un paquet ou une ligne dont la plage
    // tient l'indice). Seuls les niveaux traverses sont calcules : un tableau de
    // 50 000 elements coute trois niveaux de paquets, pas 50 000 lignes.
    const auto probe = probeOf(want);
    node = mt::root(info->name, info->type);
    for (int level = 0; level < 256; ++level) {
        if (lower(node.key) == want) return true;
        const auto kids = mt::children(p, node);
        auto next = std::find_if(kids.begin(), kids.end(), [&](const mt::Node& k) { return lower(k.key) == want; });
        if (next == kids.end()) next = std::find_if(kids.begin(), kids.end(), [&](const mt::Node& k) { return leadsTo(k, probe); });
        if (next == kids.end()) return false;
        chain.push_back(lower(node.key));
        node = *next;
    }
    return false;                      // 256 niveaux : un type qui se contient
}

bool VariablesPane::setExpanded(std::string_view path, bool open) {
    const auto want = keyForm(path);
    if (want.empty()) return false;
    // Montree : par sa cle, ou par son chemin montre (un paquet, une ligne).
    for (const auto& r : rows_) {
        if (r.group || !r.structure) continue;
        const auto key = lower(keyOf(r));
        if (key != want && !(r.member && keyForm(shownPath(r.node)) == want)) continue;
        // La ligne choisie reste choisie ; repliee sous elle, c'est elle qui l'est.
        if (open ? expanded_.insert(key).second : expanded_.erase(key) != 0) rebuildRows();
        return true;
    }
    // Pas montree : la retrouver sous sa variable. La replier ne demande rien
    // d'autre ; la deplier montre sa variable et ouvre ses parents.
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const Info* info = nullptr;
    mt::Node node;
    std::vector<std::string> chain;
    if (!p || !locate(*p, path, info, node, chain) || !mt::hasChildren(*p, node)) return false;
    const auto key = lower(node.key);
    if (!open) {
        if (expanded_.erase(key) != 0) rebuildRows();
        return true;
    }
    showVariable(*info);
    for (auto& k : chain) expanded_.insert(std::move(k));
    expanded_.insert(key);
    keep_ = node.key;                  // choisie : la table la montre
    rebuildRows();
    return true;
}

bool VariablesPane::isExpanded(std::string_view path) const {
    // keyForm : "tempon[100 ... 149]" est la cle tempon[100..149].
    return expanded_.count(keyForm(path)) != 0;
}

bool VariablesPane::revealMember(std::string_view path) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const Info* info = nullptr;
    mt::Node node;
    std::vector<std::string> chain;
    if (!p || !locate(*p, path, info, node, chain)) return false;
    // Sa variable montree (les filtres qui la cachent leves), chaque niveau
    // au-dessus de lui deplie, lui choisi.
    showVariable(*info);
    for (auto& k : chain) expanded_.insert(std::move(k));
    keep_ = node.key;
    rebuildRows();
    return true;
}

bool VariablesPane::selectVariable(std::string_view name) {
    for (const auto& i : infos_)
        if (lower(i.name) == lower(name)) {
            if (filters_->current() != 0 || (!filters_->search().empty() && !containsNoCase(i.name, filters_->search()))) {
                filters_->setSearch("");
                filters_->setCurrent(0);
            }
            // Lot recherche : un filtre de colonne qui la cache s'efface aussi.
            if (!passes(i)) table_->clearColumnFilters();
            collapsed_.erase(static_cast<int>(i.genre));
            keep_ = i.name;
            rebuildRows();
            return true;
        }
    // Lot API 7 : un membre ("armoires[0].ana") se montre aussi.
    return name.find_first_of(".[") != std::string_view::npos && revealMember(name);
}

void VariablesPane::search(const std::string& text) {
    if (filters_->current() != 0) filters_->setCurrent(0);
    filters_->setSearch(text);
}

std::vector<std::string> VariablesPane::tablesOf(const std::string& name) const {
    std::vector<std::string> out;
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return out;
    for (const auto& t : p->animationTables)
        for (const auto& e : t.entries) {
            if (e.hmi) continue;
            std::string path = text(*p, e.name);
            path = path.substr(0, std::min(path.find('.'), path.find('[')));
            if (lower(path) == lower(name)) {
                out.push_back(text(*p, t.name));
                break;
            }
        }
    return out;
}

void VariablesPane::refreshProperties() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto rows = selectedRows();
    const bool editable = hosts_.project && hosts_.project() != nullptr;
    std::vector<PG::Category> cats;
    if (!p || rows.empty()) {
        PG::Category c;
        c.name = "Variables globales";
        c.properties.push_back(ro("Variables", std::to_string(infos_.size())));
        c.properties.push_back(ro("Pas utilis\xC3\xA9" "es par le programme", std::to_string(unused_)));
        c.properties.push_back(ro("Lues par l'IHM", std::to_string(readByHmi_)));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    if (rows.size() > 1) {
        // Des variables et, lot API 7, des membres : chacun sous son chemin.
        bool members = false;
        for (const auto r : rows) members = members || rows_[r].member;
        PG::Category c;
        c.name = std::to_string(rows.size()) + (members ? " lignes choisies" : " variables choisies");
        for (std::size_t k = 0; k < rows.size() && k < 30; ++k) {
            const auto& row = rows_[rows[k]];
            if (row.member) c.properties.push_back(ro(shownPath(row.node), row.pack ? mt::groupType(row.node) : row.node.type));
            else c.properties.push_back(ro(infos_[row.info].name, infos_[row.info].type));
        }
        c.properties.push_back(ro("", "\xC2\xAB Ajouter \xC3\xA0 une table d'animation \xC2\xBB les y met toutes."));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    if (rows_[rows.front()].member) {
        // Lot API 7 : un membre deplie - ce qu'il est, sa variable, ce qu'on en fait.
        const auto& row = rows_[rows.front()];
        const auto& root = infos_[row.info];
        const bool line = row.pack && row.node.what == "ligne";
        PG::Category c;
        c.name = !row.pack ? std::string("Membre") : line ? std::string("Ligne d'un tableau") : std::string("Paquet d'\xC3\xA9l\xC3\xA9ments");
        c.properties.push_back(ro("Chemin", shownPath(row.node),
                                  row.pack ? std::string("Il range des \xC3\xA9l\xC3\xA9ments du tableau : il n'a pas de valeur \xC3\xA0 lui.")
                                           : std::string("Le nom que la simulation et les tables d'animation connaissent.")));
        c.properties.push_back(ro("Type", row.pack ? mt::groupType(row.node) : row.node.type));
        c.properties.push_back(ro("Ce qu'il est", row.node.what));
        if (!row.comment.empty()) c.properties.push_back(ro("Commentaire", row.comment));
        if (row.pack) {
            c.properties.push_back(ro("\xC3\x89l\xC3\xA9ments", thousands(static_cast<std::size_t>(row.node.size()))));
        } else if (row.structure) {
            const auto shape = mt::parseArray(row.node.type);
            c.properties.push_back(ro("Contenu", shape.valid() ? "un tableau de " + thousands(static_cast<std::size_t>(shape.count())) + " \xC3\xA9l\xC3\xA9ments"
                                                              : std::string("des membres : la fl\xC3\xA8" "che les d\xC3\xA9plie")));
        }
        cats.push_back(std::move(c));
        PG::Category v;
        v.name = "Sa variable";
        v.properties.push_back(ro("Nom", root.name));
        v.properties.push_back(ro("Type", root.type));
        const usage::Where where(*p);
        v.properties.push_back(ro("Programme", root.sections ? plural(root.sections, "section", "sections") + " : " + where.sectionNames(root.name, 8)
                                                             : std::string("aucune section"),
                                  "Les sections qui nomment la variable. Double-clic sur la ligne : chaque endroit."));
        cats.push_back(std::move(v));
        PG::Category k;
        k.name = "Et ensuite";
        if (!row.pack)
            k.properties.push_back(ro("", "\xC2\xAB Ajouter \xC3\xA0 une table d'animation \xC2\xBB y met ce membre : tu y lis sa valeur pendant la simulation, "
                                          "tu peux l'\xC3\xA9" "crire ou la forcer."));
        k.properties.push_back(ro("", "Un membre ne se renomme ni ne se supprime seul : son type (" + root.type + ") le d\xC3\xA9" "crit."));
        cats.push_back(std::move(k));
        props_->setCategories(std::move(cats));
        return;
    }
    const auto& i = infos_[rows_[rows.front()].info];
    if (i.index >= p->variables.size()) {
        props_->setCategories({});
        return;
    }
    const auto& v = p->variables[i.index];
    PG::Category c;
    c.name = "Variable";
    const auto index = i.index;
    PG::Property name;
    name.name = "Nom";
    name.value = i.name;
    name.type = editable ? PG::ValueType::Text : PG::ValueType::ReadOnly;
    name.description = "Renommer : les sections qui la nomment et les tables d'animation suivent (un Ctrl+Z).";
    if (editable)
        name.commit = [this, index, old = i.name](std::string_view value) {
            // Lot 7 : le nom tape ouvre le dialogue qui montre tout ce qui suit
            // (code, tables, IHM) ; la grille garde l'ancien, le dialogue renomme.
            if (value != old && requestRename("variable", old, std::string(value))) return false;
            auto doc = hosts_.project ? hosts_.project() : nullptr;
            if (!doc || !hosts_.apply) return false;
            const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Variable, index, value);
            if (!problem.empty()) {
                if (hosts_.status) hosts_.status(problem);
                return false;
            }
            keep_ = std::string(value);
            hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Variable, index, std::string(value)));
            return true;
        };
    c.properties.push_back(std::move(name));
    c.properties.push_back(ro("Type", i.type));
    c.properties.push_back(ro("Adresse", i.address.empty() ? std::string("\xE2\x80\x94") : i.address));
    const std::string init = text(*p, v.initValue);
    c.properties.push_back(ro("Initiale", init.empty() ? std::string("\xE2\x80\x94") : init));
    c.properties.push_back(ro("Commentaire", i.comment));
    c.properties.push_back(ro("Genre", std::string(usage::genreLabel(i.genre))));
    cats.push_back(std::move(c));
    PG::Category w;
    w.name = "O\xC3\xB9 elle sert";
    const usage::Where where(*p);
    w.properties.push_back(ro("Programme", i.sections ? plural(i.sections, "section", "sections") + " : " + where.sectionNames(i.name, 8) : std::string("aucune section"),
                              "Les sections qui la nomment (commentaires et cha\xC3\xAEnes non compt\xC3\xA9s). Double-clic : chaque ligne."));
    if (core::hasIhm())       // 1.12.0 : XPGAnalyser API n'a pas d'IHM
    w.properties.push_back(ro("IHM", i.hmi ? "lue " + std::to_string(i.hmi->uses) + " fois" + (i.hmi->via.empty() ? std::string{} : " (par " + i.hmi->via + ")")
                                           : std::string("non"),
                              i.hmi ? std::string("Vues, scripts, alarmes : chaque endroit qui la montre ou la teste compte une fois.") : std::string{}));
    const auto tables = tablesOf(i.name);
    std::string list;
    for (const auto& t : tables) list += (list.empty() ? "" : ", ") + t;
    w.properties.push_back(ro("Tables d'animation", list.empty() ? std::string("aucune") : list));
    cats.push_back(std::move(w));
    PG::Category k;
    k.name = "Avant de la supprimer";
    if (!i.sections && i.hmi)
        k.properties.push_back(ro("", "Le programme ne la nomme pas, mais l'IHM la lit : la supprimer casserait " + std::to_string(i.hmi->uses)
                                          + " affichage(s). Elle est sans doute \xC3\xA9" "crite par le syst\xC3\xA8me ou par une autre station."));
    else if (i.sections)
        k.properties.push_back(ro("", "Elle est nomm\xC3\xA9" "e dans " + plural(i.sections, "section", "sections") + " : les supprimer d'abord du code."));
    else
        k.properties.push_back(ro("", "Personne ne s'en sert : Supprimer passe par l'historique, un Ctrl+Z la remet."));
    cats.push_back(std::move(k));
    props_->setCategories(std::move(cats));
}

void VariablesPane::updateHint() {
    if (!frame_) return;
    frame_->setHint(plural(infos_.size(), "variable globale", "variables globales") + " \xC2\xB7 " + std::to_string(unused_) + " pas utilis\xC3\xA9" "es par le programme \xC2\xB7 "
                    + (core::hasIhm() ? std::to_string(readByHmi_) + " lues par l'IHM \xC2\xB7 " : std::string{})
                    + "double-clic : o\xC3\xB9 elle est \xC3\xA9" "crite et lue.");
}

void VariablesPane::openTableMenu() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p || !frame_) return;
    std::vector<ui::PopupMenu::Item> items;
    for (std::size_t t = 0; t < p->animationTables.size(); ++t) {
        const auto& table = p->animationTables[t];
        items.push_back({text(*p, table.name), std::to_string(table.entries.size()), "", ui::Icon::AnimationTable, true, false, kTableBase + static_cast<int>(t)});
    }
    if (!items.empty()) items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    items.push_back({"Nouvelle table\xE2\x80\xA6", "", "", ui::Icon::AnimationTable, true, false, kNewTable});
    menu_->setItems(std::move(items));
    const auto r = frame_->tools().rectOf(AAddToTable);
    const auto surface = ui::surfaceSize();
    menu_->openAt({r.x, r.bottom() + 2.f}, surface.w > 0 ? surface : gfx::Size{bounds().right(), bounds().bottom()});
}

void VariablesPane::runAction(int action) {
    // Renommer, Supprimer : une seule ligne, et une variable (lot API 7 : un
    // membre choisi avec elle ne compte pas pour elle).
    auto sel = selectedInfos();
    if ((action == ARename || action == ADelete) && selectedRows().size() != 1) sel.clear();
    switch (action) {
        case AAdd:
            if (hosts_.request) hosts_.request("create.variable");
            return;
        case ARename: {
            // Lot 7 : le dialogue qui montre tout ce qui suit (code, tables, IHM) ;
            // sans lui, comme avant.
            if (sel.size() == 1 && requestRename("variable", sel.front()->name)) return;
            if (sel.size() != 1 || !hosts_.ask) return;
            const auto index = sel.front()->index;
            const std::string old = sel.front()->name;
            const auto count = sel.front()->sections;
            hosts_.ask("Renommer " + old, "Le nouveau nom : " + (count ? plural(count, "section", "sections") + " la nomment et suivent" : std::string("aucune section ne la nomme"))
                                              + ", comme les tables d'animation.",
                       old, [this, index, old](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply) return;
                           const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Variable, index, value);
                           if (!problem.empty()) {
                               if (hosts_.status) hosts_.status(problem);
                               return;
                           }
                           keep_ = value;
                           hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Variable, index, value));
                           if (hosts_.status) hosts_.status(old + " renomm\xC3\xA9" "e en " + value + " : le code et les tables suivent. Ctrl+Z le d\xC3\xA9" "fait.");
                       });
            return;
        }
        case ADelete:
            if (sel.size() == 1 && hosts_.remove) hosts_.remove(domain::EntityKind::Variable, sel.front()->index);
            return;
        case AAddToTable:
            if (!selectedPaths().empty()) openTableMenu();
            return;
        case AUsages:
            // Un membre : les usages de sa variable.
            if (const auto* i = usageInfo(); i && hosts_.request) hosts_.request("variable:" + std::to_string(i->index));
            return;
        case AExportCsv: {
            if (!hosts_.request) return;
            // Les variables montrees ; les membres deplies n'en sont pas (le
            // fichier se recolle tel quel dans cet onglet).
            std::string csv = "Nom;Type;Adresse;Sections;IHM;O\xC3\xB9;Commentaire\n";
            for (const auto& row : rows_) {
                if (row.group || row.member) continue;
                const auto& i = infos_[row.info];
                csv += csvCell(i.name) + ";" + csvCell(i.type) + ";" + csvCell(i.address) + ";" + std::to_string(i.sections) + ";"
                     + (i.hmi ? std::to_string(i.hmi->uses) : std::string("0")) + ";" + csvCell(i.where) + ";" + csvCell(i.comment) + "\n";
            }
            hosts_.request("csv:variables\n" + csv);
            return;
        }
        default: return;
    }
}

void VariablesPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.27f, 280.f, 440.f);
    const float barH = 44.f;
    filters_->setBounds({b.x, b.y, b.w - rightW - 1.f, barH});
    table_->setBounds({b.x, b.y + barH, b.w - rightW - 1.f, std::max(0.f, b.h - barH)});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
    // Des bornes reelles : un widget vide n'est pas peint, donc pas pousse dans
    // la passe du dessus (le menu ouvert ne se voyait pas). Ferme, il ne prend rien.
    menu_->setBounds(b);
}

void VariablesPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

// ======================================================================
//                             SOUS-ROUTINES
// ======================================================================
class SubroutinesPane::Model final : public ui::ITableModel {
public:
    explicit Model(SubroutinesPane& pane) : pane_(pane) {}
    // Lot API 8 : + le commentaire en tete du code (ce qui la decrit ; la recherche le lit).
    enum Col : std::size_t { CName, CTask, CCallers, CCalls, CLines, CComment, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.list_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Sous-routine", "T\xC3\xA2" "che", "Appel\xC3\xA9" "e par", "Appels", "Lignes", "Commentaire"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.list_.size()) return {};
        const auto& i = pane_.list_[r];
        switch (c) {
            case CName: return i.name;
            case CTask: return i.task;
            case CCallers: {
                std::string out;
                for (const auto& n : i.callers) out += (out.empty() ? "" : ", ") + n;
                return out.empty() ? std::string("personne ne l'appelle") : out;
            }
            case CCalls: return std::to_string(i.callers.size());
            case CComment: return i.comment;       // lot API 8
            default: return thousands(i.lines);
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.list_.size()) return s;
        if (c == CName) {
            s.icon = ui::Icon::Section;
            s.iconTone = ui::Tone::Family2;
            s.bold = true;
        } else if (c == CCallers && pane_.list_[r].callers.empty()) {
            s.fgTone = ui::Tone::Warning;
        } else if (c == CTask || c == CComment) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
private:
    SubroutinesPane& pane_;
};

SubroutinesPane::SubroutinesPane(std::string id, std::string projectName) : ui::Widget(std::move(id)), projectName_(std::move(projectName)) {
    auto empty = std::make_unique<ApiEmptyState>(this->id() + ".vide",
        "Aucune sous-routine dans " + (projectName_.empty() ? std::string("ce projet") : projectName_),
        "Une sous-routine (SR) est une section de la t\xC3\xA2" "che qui ne s'ex\xC3\xA9" "cute que quand une autre l'appelle "
        "(SR_Nom();). Elle \xC3\xA9vite de recopier le m\xC3\xAAme code dans plusieurs sections.",
        std::vector<ApiEmptyState::Way>{
            {"\xC3\x80 la main", "Une sous-routine vide dans la t\xC3\xA2" "che ; son code s'ouvre.", "+ Sous-routine", "sr:nouvelle"},
            {"Avec la macro CreerSR", "Le formulaire demande le nom, la t\xC3\xA2" "che et le code de d\xC3\xA9part.", "Lancer CreerSR\xE2\x80\xA6", "macro:CreerSR"},
            {"L'appeler depuis une section", "AppelerSR pose l'appel dans la section choisie.", "Lancer AppelerSR\xE2\x80\xA6", "macro:AppelerSR"}});
    empty_ = &static_cast<ApiEmptyState&>(addChild(std::move(empty)));
    links_ += empty_->openRequested->connect([this](const std::string& k) {
        if (k == "sr:nouvelle") runAction(AAdd);
        else if (hosts_.request) hosts_.request(k);
    });
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".liste")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    {
        std::vector<ui::TableView::Column> cols(Model::CCount);
        const float widths[] = {260.f, 100.f, 400.f, 80.f, 80.f, 360.f};    // lot API 8 : + Commentaire
        for (std::size_t i = 0; i < cols.size(); ++i) {
            cols[i].title = model_->headerText(i);
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        cols[Model::CCalls].align = ui::Align::End;
        cols[Model::CLines].align = ui::Align::End;
        table_->setColumns(std::move(cols));
    }
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->activated->connect([this](RowIndex) { runAction(AOpen); });
    // ---- Lot API 8 : chercher dans les sous-routines ----
    //  La recherche de la table (ui::SearchQuery sur chaque colonne : le nom, la
    //  tache, qui l'appelle, le commentaire en tete du code) ; surlignee,
    //  "2 sur 7", retenue d'une seance a l'autre.
    filters_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(
        this->id() + ".filtres", "Rechercher : nom, t\xC3\xA2" "che, appel\xC3\xA9" "e par, commentaire\xE2\x80\xA6")));
    links_ += filters_->changed->connect([this] { applySearch(); });
    filters_->recall();
    // ---- fin Lot API 8 ----
}

// ---- Lot API 8 : chercher dans les sous-routines ----
void SubroutinesPane::applySearch() {
    ui::FilterChain chain;
    chain.setGlobalTerm(filters_->search());     // surlignee par la table
    table_->setFilter(std::move(chain));
    filters_->setCount(table_->visibleRowCount(), list_.size());
    // La sous-routine choisie cachee : la premiere montree (Renommer, Supprimer
    // agissent sur ce qu'on voit).
    const auto s = selected();
    bool shown = false;
    for (std::size_t i = 0; i < table_->visibleRowCount() && !shown; ++i) shown = table_->viewRow(i) == s;
    if (!shown && table_->visibleRowCount() > 0) table_->selectModelRows({table_->viewRow(0)}, false);
    invalidate();
}

std::size_t SubroutinesPane::shownCount() const { return table_->visibleRowCount(); }
// ---- fin Lot API 8 ----

SubroutinesPane::~SubroutinesPane() = default;

void SubroutinesPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void SubroutinesPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAdd, HmiGlyph::Plus, "Cr\xC3\xA9" "er une sous-routine (ST) dans la t\xC3\xA2" "che", "Sous-routine");
    t.add(ARename, HmiGlyph::Text, "Renommer la sous-routine choisie : ses appels suivent", "Renommer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer la sous-routine choisie (Ctrl+Z la rend)", "Supprimer");
    t.separator();
    t.add(AOpen, HmiGlyph::Code, "Ouvrir la sous-routine : son code et qui l'appelle", "Ouvrir");
    t.separator();
    t.add(ACreateSr, HmiGlyph::Code, "Cr\xC3\xA9" "er une sous-routine avec la macro CreerSR", "CreerSR\xE2\x80\xA6");
    t.add(ACallSr, HmiGlyph::Code, "Appeler une sous-routine depuis une section (AppelerSR)", "AppelerSR\xE2\x80\xA6");
    t.add(AHelp, HmiGlyph::Help, "Ce qu'en dit l'aide : les sous-routines, les sections, l'ordre d'ex\xC3\xA9" "cution", "Aide");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    const auto chosen = [this] { return selected() < list_.size(); };
    t.setEnabledWhen(AAdd, editable);
    t.setEnabledWhen(ARename, [editable, chosen] { return editable() && chosen(); });
    t.setEnabledWhen(ADelete, [editable, chosen] { return editable() && chosen(); });
    t.setEnabledWhen(AOpen, chosen);
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    refresh();
}

std::size_t SubroutinesPane::selected() const {
    if (list_.empty()) return kNpos;
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= list_.size() ? kNpos : static_cast<std::size_t>(rows.front());
}

void SubroutinesPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const bool asked = !keep_.empty();     // lot API 8 : une sous-routine creee, renommee - a montrer
    if (keep_.empty())
        if (const auto s = selected(); s != kNpos) keep_ = list_[s].name;
    list_.clear();
    if (p) {
        for (domain::Index s = 0; s < p->sections.size(); ++s) {
            const auto& sec = p->sections[s];
            const bool sr = sec.isSubroutine || (sec.owner < p->pous.size() && p->pous[sec.owner].kind == domain::PouKind::SubRoutine);
            if (!sr) continue;
            Info info;
            info.section = s;
            info.name = text(*p, sec.name);
            info.task = sec.task ? text(*p, sec.task) : std::string("MAST");
            info.callers = usage::callersOf(*p, s);
            info.lines = sec.lineCount;
            info.comment = apikit::leadingComment(sec.body);     // lot API 8
            list_.push_back(std::move(info));
        }
    }
    model_->modelReset->emit();
    std::size_t pick = list_.empty() ? kNpos : 0u;
    for (std::size_t i = 0; i < list_.size(); ++i)
        if (lower(list_[i].name) == lower(keep_)) pick = i;
    // ---- Lot API 8 : la recherche ----
    //  Celle qui cache la sous-routine demandee (creee, renommee) s'efface ; sinon
    //  la premiere montree est choisie.
    if (filters_ && pick != kNpos) {
        bool shown = false;
        for (std::size_t i = 0; i < table_->visibleRowCount() && !shown; ++i) shown = table_->viewRow(i) == pick;
        if (!shown && asked && !filters_->search().empty()) filters_->setSearch("");
        else if (!shown) pick = table_->visibleRowCount() > 0 ? static_cast<std::size_t>(table_->viewRow(0)) : kNpos;
    }
    if (filters_) {
        filters_->setVisibility(list_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        filters_->setCount(table_->visibleRowCount(), list_.size());
    }
    // ---- fin Lot API 8 ----
    if (pick != kNpos) table_->selectModelRows({static_cast<RowIndex>(pick)}, false);
    keep_.clear();
    empty_->setVisibility(list_.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    table_->setVisibility(list_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    if (frame_) {
        if (list_.empty()) {
            frame_->setHint("L'arbre montre Sous-routines [0] ; la m\xC3\xAA" "me entr\xC3\xA9" "e ouvre cet onglet.");
        } else {
            std::size_t orphans = 0;
            for (const auto& i : list_) orphans += i.callers.empty() ? 1u : 0u;
            frame_->setHint(plural(list_.size(), "sous-routine", "sous-routines") + (orphans ? " \xC2\xB7 " + std::to_string(orphans) + " que personne n'appelle" : std::string{})
                                + " \xC2\xB7 double-clic : son code et qui l'appelle.",
                            orphans ? ui::Tone::Warning : ui::Tone::None);
        }
    }
    invalidateLayout();
}

void SubroutinesPane::runAction(int action) {
    const auto s = selected();
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    switch (action) {
        case AAdd: {
            if (!hosts_.ask || !p) return;
            std::string proposal;
            for (int n = 1; n < 1000 && proposal.empty(); ++n) {
                const std::string candidate = "SR_" + std::to_string(n);
                bool used = false;
                for (const auto& sec : p->sections) used = used || lower(text(*p, sec.name)) == lower(candidate);
                if (!used) proposal = candidate;
            }
            const std::string task = p->tasks.empty() ? std::string("MAST") : text(*p, p->tasks.front().name);
            hosts_.ask("Cr\xC3\xA9" "er une sous-routine", "Son nom (ST, dans " + task + ") : l'appeler ensuite avec \xC2\xAB Nom(); \xC2\xBB depuis une section.", proposal,
                       [this, task](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply || value.empty()) return;
                           keep_ = value;
                           hosts_.apply(std::make_unique<project::AddSectionCommand>(doc, value, task, domain::PouLanguage::ST, domain::kNoIndex, true));
                           if (hosts_.status) hosts_.status("Sous-routine " + value + " cr\xC3\xA9\xC3\xA9" "e (Ctrl+Z la retire) ; AppelerSR pose son appel dans une section.");
                       });
            return;
        }
        case ARename: {
            // Lot 7 : le dialogue qui montre ses appels avant de les reecrire.
            if (s < list_.size() && requestRename("section", list_[s].name)) return;
            if (s >= list_.size() || !hosts_.ask) return;
            const auto section = list_[s].section;
            const std::string old = list_[s].name;
            hosts_.ask("Renommer " + old, "Le nouveau nom : ses " + plural(list_[s].callers.size(), "appel", "appels") + " suivent.", old,
                       [this, section](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply) return;
                           const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Section, section, value);
                           if (!problem.empty()) {
                               if (hosts_.status) hosts_.status(problem);
                               return;
                           }
                           keep_ = value;
                           hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Section, section, value));
                       });
            return;
        }
        case ADelete:
            if (s < list_.size() && hosts_.remove) hosts_.remove(domain::EntityKind::Section, list_[s].section);
            return;
        case AOpen:
            if (s < list_.size() && hosts_.request) hosts_.request("sous-routine:" + std::to_string(list_[s].section));
            return;
        case ACreateSr: if (hosts_.request) hosts_.request("macro:CreerSR"); return;
        case ACallSr: if (hosts_.request) hosts_.request("macro:AppelerSR"); return;
        case AHelp: if (hosts_.request) hosts_.request("aide"); return;
        default: return;
    }
}

void SubroutinesPane::onLayout() {
    const auto b = bounds();
    empty_->setBounds(b);
    // Lot API 8 : la barre de recherche au-dessus de la table (pas sur l'etat vide).
    const float barH = filters_ && filters_->visible() ? 44.f : 0.f;
    if (filters_) filters_->setBounds({b.x, b.y, b.w, barH});
    table_->setBounds({b.x, b.y + barH, b.w, std::max(0.f, b.h - barH)});
}

void SubroutinesPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
}

} // namespace app
