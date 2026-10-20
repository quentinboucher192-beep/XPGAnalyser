// =============================================================================
//  app/hmi/HmiDeclGrid.cpp - 1.11.18 (refonte des scripts, lot 5) : les onglets de
//  declarations (voir HmiDeclGrid.hpp)
// =============================================================================
#include "HmiDeclGrid.hpp"

#include "HmiAssist.hpp"       // 1.12.7 : l'aide a la saisie du code d'un calcul
#include "HmiIcons.hpp"
#include "HmiTypePicker.hpp"
#include "../../hmi/HmiCalc.hpp"       // 1.12.7 : les valeurs calculees au demarrage
#include "../../hmi/HmiMigrate.hpp"
#include "../../hmi/HmiRuntime.hpp"    // 1.12.7 : leur apercu et leur deballage (Runtime::computedObjects)
#include "../../hmi/HmiTypes.hpp"      // 1.12.7 : une structure se deballe aussi
#include "../../hmi/HmiOperators.hpp"
#include "../../hmi/HmiSymbols.hpp"    // 1.12.16 : les constantes d'une vue (viewmembers)
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;
namespace de = hmi::decledit;

namespace {

enum GridAction : int { GAdd = 1, GRemove, GDuplicate, GUp, GDown, GUses, GItems /* 1.12.2 */, GCalc /* 1.12.7 */ };
// 1.12.18 : les entrees du menu du clic droit (les ids rendus par TableView::contextAction).
constexpr int kActionBase = 1000;   // + une GridAction
constexpr int kUseBase = 1100;      // + le rang d'une utilisation (Voir les usages)
constexpr int kUses = -1;                                   // la colonne calculee Utilisations

// 1.12.7 : le script d'un endroit (et sa vue) ; nul : parti.
const hmi::Script* scriptAt(const hmi::Project& p, const de::Place& at, const hmi::View** view) {
    if (at.kind == de::Place::Kind::Script) {
        for (const auto& g : p.programs.scripts)
            if (g.id == at.id) return &g;
        return nullptr;
    }
    if (const hmi::View* v = p.view(at.view))
        for (const auto& g : v->scripts)
            if (g.id == at.id) {
                if (view) *view = v;
                return &g;
            }
    return nullptr;
}
const std::string& kPickType = typepicker::kChoose;            // 1.11.19 (lot 6) : le selecteur de types
const std::string kOtherType = "Autre type\xE2\x80\xA6";    // ouvre un champ libre (ARRAY, REF_TO...)

bool sameText(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool samePlace(const de::Place& a, const de::Place& b) noexcept {
    return a.kind == b.kind && a.view == b.view && a.object == b.object && a.type == b.type && a.id == b.id
        && sameText(a.function, b.function);
}

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}

} // namespace

// ------------------------------------------------------------------ le modele ----
//  UN SEUL MODELE pour toute la vie de la grille : refresh() change ses lignes et dit
//  modelReset - la table garde sa selection et son defilement, et une case en cours
//  d'ecriture ne perd pas le modele qui l'ecrit.
class HmiDeclGrid::Rows final : public ui::ITableModel {
public:
    struct Row {
        hmi::Declaration d;
        std::string      fault;
        std::size_t      uses{0};
        std::string      preview;       // 1.12.7 : sa valeur calculee ([1, 2]...), ou pourquoi elle ne l'est pas
        bool             previewError{false};
        sim::ObjRef      object;        // 1.12.7 : sa valeur calculee (le deballage)
    };
    std::vector<Row>         rows;
    std::vector<int>         columns;           // par colonne de la table : la de::Column, ou kUses
    de::Tab                  tab{de::Tab::Constants};
    hmi::decl::Role          role{hmi::decl::Role::Script};
    bool                     editableAll{false};
    std::vector<std::string> types;
    mutable int              freeTypeRow{-1};   // "Autre type..." : la prochaine case Type de cette ligne est un champ
    std::function<bool(std::size_t, de::Column, const std::string&)> commit;
    std::function<void(std::size_t)>                                askFreeType;
    std::function<void(std::size_t)>                                askPickType;

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return columns.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        if (c >= columns.size()) return {};
        return columns[c] == kUses ? std::string("Utilisations") : de::columnTitle(static_cast<de::Column>(columns[c]), tab);
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size() || c >= columns.size()) return {};
        if (columns[c] == kUses) return std::to_string(rows[r].uses);
        // 1.12.7 : une valeur calculee au demarrage - ce qu'elle vaut (l'apercu), sinon qu'elle l'est.
        if (static_cast<de::Column>(columns[c]) == de::Column::Value && rows[r].d.computed && !rows[r].preview.empty())
            return "\xC6\x92x " + (rows[r].previewError ? "erreur : " : std::string("= ")) + rows[r].preview;
        return de::cellText(rows[r].d, static_cast<de::Column>(columns[c]));
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows.size() || c >= columns.size()) return s;
        const auto& row = rows[r];
        if (columns[c] == kUses) {
            if (row.uses == 0) s.fgTone = ui::Tone::Muted;     // jamais employee
            return s;
        }
        switch (static_cast<de::Column>(columns[c])) {
            case de::Column::Name:
                s.icon = row.d.kind == hmi::DeclKind::Constant ? ui::Icon::Constant : ui::Icon::Variable;
                s.monospace = true;
                if (!row.fault.empty()) {
                    s.fgTone = ui::Tone::Error;
                    s.iconTone = ui::Tone::Error;
                }
                break;
            case de::Column::Type:
                s.monospace = true;
                break;
            case de::Column::Value:
                s.monospace = true;
                if (row.d.computed) s.fgTone = row.previewError ? ui::Tone::Error : ui::Tone::Accent;   // 1.12.7
                break;
            case de::Column::Storage:
                if (row.d.storage == hmi::Storage::Persistent) s.fgTone = ui::Tone::Accent;
                break;
            default: break;
        }
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (c < columns.size() && columns[c] == kUses) return rows[a].uses < rows[b].uses;
        return cellText(a, c) < cellText(b, c);
    }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override {
        // 1.12.7 : la valeur calculee se change par « \xC6\x92x Calcul\xE2\x80\xA6 » (pas dans la case).
        if (r < rows.size() && c < columns.size() && columns[c] != kUses && rows[r].d.computed
            && static_cast<de::Column>(columns[c]) == de::Column::Value)
            return false;
        return editableAll && r < rows.size() && c < columns.size() && columns[c] != kUses;
    }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size() || c >= columns.size() || columns[c] == kUses) return {};
        const auto col = static_cast<de::Column>(columns[c]);
        switch (col) {
            case de::Column::Type: {
                if (freeTypeRow == static_cast<int>(r)) {
                    freeTypeRow = -1;                         // cette fois, un champ libre
                    return {};
                }
                std::vector<std::string> out = types;
                const std::string& cur = rows[r].d.type;
                if (!cur.empty() && std::none_of(out.begin(), out.end(), [&](const std::string& t) { return sameText(t, cur); }))
                    out.insert(out.begin(), cur);
                if (typepicker::available()) out.push_back(kPickType);   // sans hote (un essai) : pas d'entree
                out.push_back(kOtherType);
                return out;
            }
            case de::Column::Storage: {
                std::vector<std::string> out;
                for (const auto s : de::storagesFor(role)) out.emplace_back(de::storageLabel(s));
                return out;
            }
            case de::Column::Mode:
                return {std::string(de::modeLabel(hmi::PassMode::In)), std::string(de::modeLabel(hmi::PassMode::InOut)),
                        std::string(de::modeLabel(hmi::PassMode::Out))};
            case de::Column::Visibility:
                return {std::string(de::visibilityLabel(hmi::Visibility::Public)), std::string(de::visibilityLabel(hmi::Visibility::Private))};
            default: return {};
        }
    }
    // 1.12.14 : la case Type ouvre le selecteur de types (il compose aussi les types imbriques) ; sans
    // hote (un essai), la liste et « Autre type... ».
    bool editInDialog(ui::RowIndex r, std::size_t c) override {
        if (r >= rows.size() || c >= columns.size() || columns[c] == kUses || !askPickType || !typepicker::available()) return false;
        if (static_cast<de::Column>(columns[c]) != de::Column::Type) return false;
        askPickType(r);
        return true;
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= rows.size() || c >= columns.size() || columns[c] == kUses || !commit) return false;
        const auto col = static_cast<de::Column>(columns[c]);
        if (col == de::Column::Type && text == kOtherType) {
            if (askFreeType) askFreeType(r);
            return false;
        }
        if (col == de::Column::Type && text == kPickType) {
            if (askPickType) askPickType(r);
            return false;
        }
        return commit(r, col, std::string(text));
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= rows.size()) return {};
        const auto& row = rows[r];
        std::string tip = row.d.name + " : " + row.d.type;
        if (row.d.computed) {
            // 1.12.7 : calculee au demarrage - son code, ses locales, ce qu'elle vaut.
            tip += "  (\xC6\x92x calcul\xC3\xA9" "e au d\xC3\xA9marrage)\n" + row.d.calc;
            if (!row.d.locals.empty()) tip += "Locales :\n" + de::localsText(row.d.locals);
            if (!row.preview.empty()) tip += (row.previewError ? "Erreur : " : "= ") + row.preview + "\n";
            tip += "\xC2\xAB Valeur initiale\xE2\x80\xA6 \xC2\xBB pour le changer.";
        } else if (!row.d.value.empty()) {
            tip += " := " + row.d.value;
        }
        if (row.d.kind == hmi::DeclKind::Variable && role == hmi::decl::Role::Script)
            tip += "\n" + std::string(de::storageLabel(row.d.storage)) + " : " + std::string(de::storageHelp(row.d.storage));
        if (!row.d.description.empty()) tip += "\n" + row.d.description;
        tip += "\n" + (row.uses ? plural(row.uses, "utilisation", "utilisations") : std::string("jamais employ\xC3\xA9" "e"));
        if (!row.fault.empty()) tip = "Faute : " + row.fault + "\n" + tip;
        return tip;
    }
};

// ------------------------------------------------------------ 1.12.7 : le deballage ----
//  La valeur a taille de la ligne choisie : la racine (son nom), puis ses elements - un indice
//  ([0], [1, 2] pour un tableau a deux dimensions), une cle ('bas', 3), un membre (Temp, Item1) -
//  chacun avec son type et sa valeur ; un element a taille se deplie a son tour (la fleche).

// ------------------------------------------------------------------- la grille ----
HmiDeclGrid::HmiDeclGrid(std::string id, hmi::DocumentPtr doc, Apply apply, de::Tab tab)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), tab_(tab) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(GAdd, HmiGlyph::Plus, "Ajouter une d\xC3\xA9" "claration (Inser) : un nom libre, \xC3\xA0 renommer dans la case", "Ajouter");
    tools->add(GRemove, HmiGlyph::Delete, "Supprimer les lignes choisies (Suppr) - Ctrl+Z les rend", "Supprimer");
    tools->add(GDuplicate, HmiGlyph::Duplicate, "Dupliquer les lignes choisies (Ctrl+D) : Nom_copie, juste apr\xC3\xA8s", "Dupliquer");
    tools->separator();
    tools->add(GUp, HmiGlyph::Up, "Monter la ligne choisie (Alt+Haut)", {});
    tools->add(GDown, HmiGlyph::Down, "Descendre la ligne choisie (Alt+Bas)", {});
    tools->separator();
    tools->add(GUses, HmiGlyph::Search, "Voir les usages dans le code (Maj+F12 ; l'onglet Code, le suivant \xC3\xA0 chaque clic)",
               "Voir les usages");
    // 1.12.16 (T4) : la valeur initiale - en brut (un editeur fait pour le type) ou en code (une fonction qui
    // rend le type), une seule fenetre (a la place d'« Elements... » 1.12.2 et de « fx Calcul... » 1.12.7).
    tools->add(GCalc, HmiGlyph::Code, "Valeur initiale : en brut (un \xC3\xA9" "diteur fait pour le type : une ligne par entr\xC3\xA9" "e, par \xC3\xA9l\xC3\xA9ment, "
               "par membre) ou en code (FUNCTION Init_Nom : son type - le code rend la valeur, au d\xC3\xA9marrage) ; le type expliqu\xC3\xA9",
               "Valeur initiale\xE2\x80\xA6");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto one = [this] { return usable() && selectedRows().size() == 1; };
    tools_->setEnabledWhen(GCalc, [this, one] {
        if (!one() || !calceditor::available() || tableColumnOf(de::Column::Value) < 0) return false;
        const auto r = selectedRows().front();
        return r < rows_->rows.size();
    });
    tools_->setEnabledWhen(GAdd, [this] { return usable(); });
    tools_->setEnabledWhen(GRemove, [this] { return usable() && !selectedRows().empty(); });
    tools_->setEnabledWhen(GDuplicate, [this] { return usable() && !selectedRows().empty(); });
    tools_->setEnabledWhen(GUp, [this, one] { return one() && selectedRows().front() > 0; });
    tools_->setEnabledWhen(GDown, [this, one] { return one() && selectedRows().front() + 1 < count(); });
    tools_->setEnabledWhen(GUses, one);
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher\xE2\x80\xA6",
        "Filtrer les lignes : chaque mot dans l'une des colonnes (tous les mots), sans casse ni accents ; \"une phrase\" ; -mot : l'exclure.")));
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setSelectionMode(ui::SelectionMode::Extended);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    hint_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".hint")));
    // 1.12.7 : le deballage de la valeur choisie (cache tant qu'aucune valeur a taille n'est choisie).
    unfoldModel_ = std::make_shared<HmiUnfoldModel>();
    unfold_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(base + ".unfold")));
    unfold_->setModel(unfoldModel_);
    unfold_->setColumns(HmiUnfoldModel::columns());   // une table sans colonnes ne montre rien
    unfold_->setVisibility(ui::Visibility::Collapsed);

    rows_ = std::make_shared<Rows>();
    rows_->tab = tab_;
    rows_->commit = [this](std::size_t row, de::Column col, const std::string& text) { return setCell(row, col, text); };
    rows_->askFreeType = [this](std::size_t row) {
        const int c = tableColumnOf(de::Column::Type);
        if (c < 0) return;
        rows_->freeTypeRow = static_cast<int>(row);
        (void)table_->beginCellEdit(static_cast<ui::RowIndex>(row), static_cast<std::size_t>(c));
    };
    rows_->askPickType = [this](std::size_t row) { pickType(row); };
    table_->setModel(rows_);

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case GAdd: (void)add(); break;
            case GRemove: (void)removeSelected(); break;
            case GDuplicate: (void)duplicateSelected(); break;
            case GUp: (void)moveSelected(-1); break;
            case GDown: (void)moveSelected(+1); break;
            case GUses:
                if (const auto sel = selectedRows(); sel.size() == 1 && sel.front() < rows_->rows.size())
                    usesRequested->emit(rows_->rows[sel.front()].d.name);
                break;
            case GCalc:
                if (const auto sel = selectedRows(); sel.size() == 1) editInitial(sel.front());
                break;
            default: break;
        }
    });
    // 1.12.18 (planche 15) : le menu du clic droit - la ligne visee, ses gestes (ceux de la barre),
    // Voir les usages (une entree par utilisation dans le code), Copier, Coller, Supprimer.
    table_->setExtraContextOnly(true);
    table_->setExtraContextItems([this] { return contextItems(); });
    links_ += table_->contextAction->connect([this](int choice) {
        if (choice >= kUseBase) {
            const auto sel = selectedRows();
            if (sel.size() == 1 && sel.front() < rows_->rows.size()) useRequested->emit(rows_->rows[sel.front()].d.name, choice - kUseBase);
            return;
        }
        tools_->triggered->emit(choice - kActionBase);
    });
    links_ += search_->changed->connect([this] { applySearch(); });
    // 1.12.7 : le deballage suit la ligne choisie ; la fleche deplie un element.
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { refreshUnfold(); });
    links_ += unfold_->expanderClicked->connect([this](ui::RowIndex r) { unfoldModel_->toggle(r); });

    // EXCEL : le collage (Ctrl+V, Ctrl+Maj+V) - le nom dit quelle ligne.
    paste_.table = table_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) { return pasteTarget(rq); };
    paste::bind(paste_);
    rebuildColumns();
    refresh();
}

HmiDeclGrid::~HmiDeclGrid() = default;

std::vector<ui::PopupMenu::Item> HmiDeclGrid::contextItems() const {
    using Item = ui::PopupMenu::Item;
    std::vector<Item> items;
    const auto sel = selectedRows();
    const bool one = sel.size() == 1 && sel.front() < rows_->rows.size();
    const auto tool = [this](int action, std::string label, std::string key, std::string why) {
        Item it;
        it.label = std::move(label);
        it.shortcut = std::move(key);
        it.id = kActionBase + action;
        it.enabled = tools_->isEnabled(action);
        if (!it.enabled) it.disabledReason = std::move(why);
        return it;
    };
    const auto rule = [] {
        Item it;
        it.separator = true;
        return it;
    };
    if (one) {
        const auto& d = rows_->rows[sel.front()].d;
        Item head;
        head.heading = true;
        head.label = d.name;
        head.shortcut = d.type + " \xC2\xB7 " + de::tabLabel(tab_, role_);
        items.push_back(std::move(head));
    } else if (sel.size() > 1) {
        Item head;
        head.heading = true;
        head.label = std::to_string(sel.size()) + " d\xC3\xA9" "clarations";
        items.push_back(std::move(head));
    }
    const std::string noRow = "choisis une ligne";
    auto addItem = tool(GAdd, "Ajouter", "Inser", "pas de code ici");
    addItem.icon = ui::Icon::Plus;
    items.push_back(std::move(addItem));
    items.push_back(tool(GDuplicate, "Dupliquer", "Ctrl+D", noRow));
    items.push_back(tool(GUp, "Monter", "Alt+Haut", one ? std::string("d\xC3\xA9j\xC3\xA0 en t\xC3\xAAte") : noRow));
    items.push_back(tool(GDown, "Descendre", "Alt+Bas", one ? std::string("d\xC3\xA9j\xC3\xA0 en bas") : noRow));
    // Voir les usages : une entree par utilisation dans le code (la ligne et son texte).
    {
        std::vector<de::Use> uses;
        std::string body;
        if (one && place_ && doc_) {
            const auto code = de::locate(doc_->project, *place_);
            if (code.valid()) {
                body = *code.body;
                uses = de::usesIn(body, rows_->rows[sel.front()].d.name);
            }
        }
        Item u;
        u.label = "Voir les usages (" + std::to_string(uses.size()) + ")";
        u.icon = ui::Icon::Search;
        u.id = -1;
        for (std::size_t k = 0; k < uses.size() && k < 20; ++k) {
            // Le texte de la ligne, sans ses blancs de tete.
            std::size_t from = 0;
            for (int l = 1; l < uses[k].line && from != std::string::npos; ++l) {
                from = body.find('\n', from);
                if (from != std::string::npos) ++from;
            }
            std::string text = from == std::string::npos ? std::string{} : body.substr(from, body.find('\n', from) - from);
            text.erase(0, std::min(text.find_first_not_of(" \t"), text.size()));
            if (text.size() > 48) text = text.substr(0, 46) + "\xE2\x80\xA6";
            Item c;
            c.label = "ligne " + std::to_string(uses[k].line) + " : " + text;
            c.id = kUseBase + static_cast<int>(k);
            u.children.push_back(std::move(c));
        }
        if (u.children.empty()) {
            u.enabled = false;
            u.disabledReason = one ? std::string("aucune utilisation dans le code") : noRow;
        }
        items.push_back(std::move(u));
    }
    items.push_back(tool(GCalc, "Valeur initiale\xE2\x80\xA6", {}, one ? std::string("pas pour ce type") : noRow));
    items.push_back(rule());
    // Copier, Coller : ceux du tableau (ids 1 et 3, l'Excel des grilles).
    Item copy;
    copy.label = "Copier";
    copy.shortcut = "Ctrl+C";
    copy.id = 1;
    copy.enabled = !sel.empty();
    if (!copy.enabled) copy.disabledReason = noRow;
    items.push_back(std::move(copy));
    Item paste;
    paste.label = "Coller";
    paste.shortcut = "Ctrl+V";
    paste.id = 3;
    paste.enabled = usable() && !ui::clipboardText().empty();
    if (!paste.enabled) paste.disabledReason = usable() ? std::string("le presse-papiers est vide") : std::string("pas de code ici");
    items.push_back(std::move(paste));
    items.push_back(rule());
    items.push_back(tool(GRemove, "Supprimer", "Suppr", noRow));
    return items;
}

std::string HmiDeclGrid::title() const { return de::tabLabel(tab_, role_); }
std::size_t HmiDeclGrid::count() const noexcept { return rows_ ? rows_->rows.size() : 0; }
std::size_t HmiDeclGrid::faultCount() const noexcept {
    std::size_t n = 0;
    if (rows_)
        for (const auto& r : rows_->rows) n += r.fault.empty() ? 0 : 1;
    return n;
}

bool HmiDeclGrid::usable() const {
    if (!place_ || !doc_) return false;
    const auto code = de::locate(doc_->project, *place_);
    const auto tabs = de::tabsOf(code);
    return std::find(tabs.begin(), tabs.end(), tab_) != tabs.end();
}

void HmiDeclGrid::setPlace(std::optional<de::Place> place, std::string why) {
    const bool same = place.has_value() == place_.has_value() && (!place || samePlace(*place, *place_));
    place_ = std::move(place);
    placeWhy_ = std::move(why);
    if (!same) {
        table_->selectModelRows({}, false);
        table_->setScrollOffset(0.f);
        paste::forget(paste_);
    }
    refresh();
}

void HmiDeclGrid::rebuildColumns() {
    const auto cols = de::columnsOf(tab_, role_);
    columns_.clear();
    usesColumn_ = -1;
    std::vector<ui::TableView::Column> view;
    const bool order = tab_ == de::Tab::Parameters;     // l'ordre des parametres est la signature : pas de tri
    for (const auto c : cols) {
        if (c == de::Column::Description) {               // Utilisations avant la documentation (la maquette)
            usesColumn_ = static_cast<int>(columns_.size());
            columns_.push_back(kUses);
            view.push_back({"Utilisations", 92.f, 60.f, true, !order, true, ui::Align::End});
        }
        columns_.push_back(static_cast<int>(c));
        float w = 120.f;
        switch (c) {
            case de::Column::Name:        w = 170.f; break;
            case de::Column::Type:        w = 150.f; break;
            case de::Column::Value:       w = 130.f; break;
            case de::Column::Storage:     w = 110.f; break;
            case de::Column::Mode:        w = 120.f; break;
            case de::Column::Visibility:  w = 92.f; break;
            case de::Column::Description: w = 320.f; break;
        }
        view.push_back({de::columnTitle(c, tab_), w, 50.f, true, !order});
    }
    table_->setColumns(std::move(view));
    rows_->columns = columns_;
    rows_->role = role_;
    haveColumns_ = true;
}

void HmiDeclGrid::refresh() {
    // Les lignes choisies (par identifiant) et le defilement restent.
    std::vector<Id> keep;
    for (const auto r : selectedRows())
        if (r < rows_->rows.size()) keep.push_back(rows_->rows[r].d.id);
    const float scroll = table_->scrollOffset();
    const auto code = place_ && doc_ ? de::locate(doc_->project, *place_) : de::Code{};
    const hmi::decl::Role role = code.valid() ? code.role : hmi::decl::Role::Script;
    if (role != role_ || !haveColumns_) {
        role_ = role;
        rebuildColumns();
    }
    rows_->rows.clear();
    const bool ok = code.valid() && usable();
    rows_->editableAll = ok;
    if (code.valid()) {
        const auto faults = de::faults(doc_->project, code);
        for (const auto i : de::rowsOf(*code.decls, tab_)) {
            Rows::Row row;
            row.d = (*code.decls)[i];
            row.fault = i < faults.size() ? faults[i] : std::string{};
            row.uses = de::usageCount(code, i);
            rows_->rows.push_back(std::move(row));
        }
        rows_->types = de::typeChoices(doc_->project);
        fillPreviews(code);
    }
    rows_->modelReset->emit();
    table_->setScrollOffset(scroll);
    std::vector<ui::RowIndex> sel;
    for (std::size_t i = 0; i < rows_->rows.size(); ++i)
        if (std::find(keep.begin(), keep.end(), rows_->rows[i].d.id) != keep.end()) sel.push_back(static_cast<ui::RowIndex>(i));
    table_->selectModelRows(sel, false);
    applySearch();
    refreshUnfold();
    // La barre du bas : pourquoi la grille est vide, ou ce que veulent dire ses colonnes.
    std::string hint;
    if (!place_) hint = placeWhy_.empty() ? std::string("Aucun code choisi.") : placeWhy_;
    else if (!code.valid()) hint = "Ce code n'existe plus.";
    else if (!code.declares) hint = "Un script C ou C++ garde ses d\xC3\xA9" "clarations dans son code : le langage les y veut.";
    else if (!ok) hint = "Cet onglet ne s'applique pas \xC3\xA0 ce code.";
    else {
        const std::size_t bad = faultCount();
        if (bad) hint = plural(bad, "d\xC3\xA9" "claration fautive", "d\xC3\xA9" "clarations fautives") + " (en rouge, la raison en infobulle)  \xC2\xB7  ";
        switch (tab_) {
            case de::Tab::Constants:
                hint += "Une valeur : 100.0, 3, T#5s, 'texte', E_Mode#Auto  \xC2\xB7  Ctrl+C / Ctrl+V : Excel";
                break;
            case de::Tab::Variables:
                hint += role_ == hmi::decl::Role::Script
                            ? "Stockage : Ex\xC3\xA9" "cution (remise \xC3\xA0 chaque ex\xC3\xA9" "cution) \xC2\xB7 Conserv\xC3\xA9" "e (entre deux ex\xC3\xA9" "cutions) "
                              "\xC2\xB7 Persistante (aussi apr\xC3\xA8s un red\xC3\xA9marrage)"
                            : "Une locale reste locale : elle repart de sa valeur initiale \xC3\xA0 chaque appel (une fonction n'a pas de m\xC3\xA9moire)";
                break;
            case de::Tab::Parameters:
                hint += "L'ordre est la signature (Alt+Haut, Alt+Bas) \xC2\xB7 un d\xC3\xA9" "faut rend le param\xC3\xA8tre facultatif \xC2\xB7 "
                        "Entr\xC3\xA9" "e/sortie : la variable de l'appelant est \xC3\xA9" "crite";
                break;
        }
    }
    hint_->setMessage(hint, faultCount() ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiDeclGrid::applySearch() {
    ui::FilterChain chain;
    if (search_ && !search_->text().empty()) chain.setGlobalTerm(search_->text());
    table_->setFilter(std::move(chain));
    if (search_) search_->setCount(table_->visibleRowCount(), count());
}

std::vector<std::size_t> HmiDeclGrid::selectedRows() const {
    std::vector<std::size_t> out;
    for (const auto r : table_->selectedModelRows())
        if (r < rows_->rows.size()) out.push_back(r);
    std::sort(out.begin(), out.end());
    return out;
}

void HmiDeclGrid::selectRows(const std::vector<std::size_t>& rows) {
    std::vector<ui::RowIndex> sel;
    for (const auto r : rows)
        if (r < rows_->rows.size()) sel.push_back(static_cast<ui::RowIndex>(r));
    table_->selectModelRows(sel, true);
}

std::string HmiDeclGrid::rowName(std::size_t row) const { return row < rows_->rows.size() ? rows_->rows[row].d.name : std::string{}; }

bool HmiDeclGrid::selectName(const std::string& name) {
    for (std::size_t i = 0; i < rows_->rows.size(); ++i)
        if (sameText(rows_->rows[i].d.name, name)) {
            if (search_ && !search_->text().empty()) search_->setText({});   // la ligne doit se voir
            selectRows({i});
            return true;
        }
    return false;
}

int HmiDeclGrid::tableColumnOf(de::Column column) const {
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i] == static_cast<int>(column)) return static_cast<int>(i);
    return -1;
}

std::string HmiDeclGrid::noun(bool plural) const {
    switch (tab_) {
        case de::Tab::Constants:  return plural ? "les constantes" : "la constante";
        case de::Tab::Variables:
            if (role_ == hmi::decl::Role::Script) return plural ? "les variables" : "la variable";
            return plural ? "les locales" : "la locale";
        case de::Tab::Parameters: return plural ? "les param\xC3\xA8tres" : "le param\xC3\xA8tre";
    }
    return {};
}

std::string HmiDeclGrid::ownerLabel() const {
    if (!place_ || !doc_) return {};
    const auto& p = doc_->project;
    using K = de::Place::Kind;
    switch (place_->kind) {
        case K::Script:
        case K::ViewScript:
            if (const auto* s = p.script(place_->id)) return s->name;
            break;
        case K::Function:
            for (const auto& f : p.programs.functions)
                if (f.id == place_->id) return f.name;
            break;
        case K::SymbolFunction:
            if (const auto* v = p.view(place_->view))
                for (const auto& f : v->functions)
                    if (f.id == place_->id) return v->name + "." + f.name;
            break;
        case K::Override:
            if (const auto* v = p.view(place_->view))
                if (const auto* o = v->object(place_->object)) return o->name + "." + place_->function;
            break;
        case K::TypeOperator:
            if (const auto* t = p.hmiType(place_->type))
                for (const auto& o : t->operators)
                    if (o.id == place_->id) return t->name + " " + hmi::operatorSignature(o);
            break;
        case K::SymbolOperator:
            if (const auto* v = p.view(place_->view))
                for (const auto& o : v->operators)
                    if (o.id == place_->id) return v->name + " " + hmi::operatorSignature(o);
            break;
        case K::ViewConstants:   // 1.12.16
            if (const auto* v = p.view(place_->view)) return v->name;
            break;
    }
    return {};
}

void HmiDeclGrid::say(std::string text, bool warning) {
    message_ = std::move(text);
    message->emit(message_, warning);
}

bool HmiDeclGrid::change(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& edit) {
    if (!place_ || !doc_) {
        say(placeWhy_.empty() ? std::string("Aucun code choisi.") : placeWhy_, true);
        return false;
    }
    std::string why;
    bool ok = false;
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { ok = edit(p, &why); });
    if (!ok) {
        say("Refus\xC3\xA9 : " + why, true);
        return false;
    }
    paste::forget(paste_);
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiDeclGrid::add() {
    const auto sel = selectedRows();
    const int after = sel.empty() ? -1 : static_cast<int>(sel.back());
    Id made = kNoId;
    const de::Place at = place_ ? *place_ : de::Place{};
    const de::Tab tab = tab_;
    if (!change("Ajouter " + noun(false) + " (" + ownerLabel() + ")",
                [&](hmi::Project& p, std::string* why) { return de::add(p, at, tab, after, {}, &made, why); }))
        return false;
    refresh();
    for (std::size_t i = 0; i < rows_->rows.size(); ++i)
        if (rows_->rows[i].d.id == made) {
            selectRows({i});
            say(rows_->rows[i].d.name + (feminine() ? " ajout\xC3\xA9" "e : son nom est \xC3\xA0 \xC3\xA9" "crire (F2 plus tard) - Ctrl+Z la retire"
                                                    : " ajout\xC3\xA9 : son nom est \xC3\xA0 \xC3\xA9" "crire (F2 plus tard) - Ctrl+Z le retire"),
                false);
            // Le nom tout de suite a ecrire, comme dans la maquette.
            if (const int c = tableColumnOf(de::Column::Name); c >= 0) (void)table_->beginCellEdit(static_cast<ui::RowIndex>(i), static_cast<std::size_t>(c));
        }
    return true;
}

bool HmiDeclGrid::removeSelected() {
    const auto sel = selectedRows();
    if (sel.empty()) {
        say("Supprimer : aucune ligne choisie.", true);
        return false;
    }
    std::size_t used = 0;
    std::string names;
    for (const auto r : sel) {
        used += rows_->rows[r].uses;
        names += (names.empty() ? "" : ", ") + rows_->rows[r].d.name;
    }
    const de::Place at = *place_;
    const de::Tab tab = tab_;
    if (!change("Supprimer " + noun(sel.size() > 1) + " " + names + " (" + ownerLabel() + ")",
                [&](hmi::Project& p, std::string* why) { return de::remove(p, at, tab, sel, why); }))
        return false;
    refresh();
    say(names + " supprim\xC3\xA9" + (feminine() ? "e" : "") + (sel.size() > 1 ? "s" : "")
            + (used ? " - le code les citait " + plural(used, "fois", "fois") + " : Compiler le dira" : std::string{}) + " (Ctrl+Z les rend)",
        used > 0);
    return true;
}

bool HmiDeclGrid::duplicateSelected() {
    const auto sel = selectedRows();
    if (sel.empty()) {
        say("Dupliquer : aucune ligne choisie.", true);
        return false;
    }
    std::vector<Id> made;
    const de::Place at = place_ ? *place_ : de::Place{};
    const de::Tab tab = tab_;
    if (!change("Dupliquer " + noun(sel.size() > 1) + " (" + ownerLabel() + ")",
                [&](hmi::Project& p, std::string* why) { return de::duplicate(p, at, tab, sel, &made, why); }))
        return false;
    refresh();
    std::vector<std::size_t> rows;
    std::string names;
    for (std::size_t i = 0; i < rows_->rows.size(); ++i)
        if (std::find(made.begin(), made.end(), rows_->rows[i].d.id) != made.end()) {
            rows.push_back(i);
            names += (names.empty() ? "" : ", ") + rows_->rows[i].d.name;
        }
    selectRows(rows);
    say("Copie : " + names + " (Ctrl+Z la retire)", false);
    return true;
}

bool HmiDeclGrid::moveSelected(int delta) {
    const auto sel = selectedRows();
    if (sel.size() != 1) {
        say("D\xC3\xA9placer : choisis une ligne.", true);
        return false;
    }
    const std::size_t row = sel.front();
    const de::Place at = place_ ? *place_ : de::Place{};
    const de::Tab tab = tab_;
    if (!change((delta < 0 ? "Monter " : "Descendre ") + rows_->rows[row].d.name + " (" + ownerLabel() + ")",
                [&](hmi::Project& p, std::string* why) { return de::move(p, at, tab, row, delta, why); }))
        return false;
    refresh();
    selectRows({static_cast<std::size_t>(static_cast<long>(row) + delta)});
    return true;
}

bool HmiDeclGrid::setCell(std::size_t row, de::Column column, const std::string& text) {
    if (row >= rows_->rows.size()) return false;
    const std::string name = rows_->rows[row].d.name;
    const de::Place at = place_ ? *place_ : de::Place{};
    const de::Tab tab = tab_;
    const std::string what = column == de::Column::Name ? "Renommer " + name + " en " + text
                                                        : de::columnTitle(column, tab_) + " de " + name;
    std::string note;                     // 1.11.21 : la valeur retiree par un changement de type
    if (!change(what + " (" + ownerLabel() + ")", [&](hmi::Project& p, std::string* why) {
            if (!de::set(p, at, tab, row, column, text, why, &note)) return false;
            // 1.12.16 : une constante de vue renommee - la vue la lit par son nom, le projet par Vue.Nom.
            if (column == de::Column::Name && at.kind == de::Place::Kind::ViewConstants && name != text)
                if (const auto* v = p.view(at.view)) (void)hmi::viewmembers::renameMember(p, v->name, name, text);
            return true;
        }))
        return false;
    refresh();
    if (!note.empty()) say(name + " : " + note + " (Ctrl+Z la rend)", false);
    if (column == de::Column::Name && name != text) {
        const std::size_t uses = row < rows_->rows.size() ? rows_->rows[row].uses : 0;
        say(name + (feminine() ? " renomm\xC3\xA9" "e en " : " renomm\xC3\xA9 en ") + text
                + (uses ? " - le code suit (" + plural(uses, "utilisation", "utilisations") + ")" : std::string{}),
            false);
    }
    return true;
}

// 1.12.2 : l'editeur des elements de la valeur ; 1.12.16 : la fenetre « Valeur initiale », en Brut.
bool HmiDeclGrid::editItems(std::size_t row) { return editInitial(row, 0); }

void HmiDeclGrid::refreshUnfold() {
    if (!unfold_ || !unfoldModel_) return;
    const auto sel = selectedRows();
    sim::ObjRef root;
    std::string name, why;
    if (sel.size() == 1 && sel.front() < rows_->rows.size() && doc_) {
        const auto& row = rows_->rows[sel.front()];
        name = row.d.name;
        if (row.d.computed) {
            root = row.object;
            if (!root && row.previewError) why = row.preview;
        } else if (itemseditor::editable(row.d.type) || !hmi::types::membersOf(doc_->project, row.d.type).empty()) {
            // Une valeur ecrite : lue dans son type (vide : la valeur de son type).
            root = hmi::Runtime::valueObject(doc_->project, row.d.type, row.d.value, &why);
        }
        if (root && !HmiUnfoldModel::sized(root)) root.reset();
    }
    const bool show = root != nullptr || !why.empty();
    if (name != unfoldModel_->rootName) unfoldModel_->open = {""};   // une autre declaration : repliee sous la racine
    unfoldModel_->root = root;
    unfoldModel_->rootName = name;
    unfoldModel_->why = show ? why : std::string{};
    unfoldModel_->rebuild();
    unfoldModel_->modelReset->emit();
    if (show != unfoldShown_) {
        unfoldShown_ = show;
        unfold_->setVisibility(show ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        onLayout();
    }
}

// 1.12.7 : le script (et sa vue) qui porte les declarations montrees ; nul : pas un script.
const hmi::Script* HmiDeclGrid::ownerScript(const de::Code& code, const hmi::View** view) const {
    if (!doc_ || !code.valid()) return nullptr;
    // 1.12.16 : les constantes d'une vue - le script qui les porte pour le moteur (son identifiant : la vue).
    for (const auto& v : doc_->project.views)
        if (&v.constants == code.decls) {
            if (view) *view = &v;
            viewHolder_ = hmi::viewmembers::constantsScript(v);
            return &viewHolder_;
        }
    for (const auto& sc : doc_->project.programs.scripts)
        if (&sc.decls == code.decls) return &sc;
    for (const auto& v : doc_->project.views)
        for (const auto& sc : v.scripts)
            if (&sc.decls == code.decls) {
                if (view) *view = &v;
                return &sc;
            }
    return nullptr;
}

void HmiDeclGrid::fillPreviews(const de::Code& code) {
    if (std::none_of(rows_->rows.begin(), rows_->rows.end(), [](const Rows::Row& r) { return r.d.computed; })) return;
    const hmi::View* owner = nullptr;
    const hmi::Script* sc = ownerScript(code, &owner);
    if (!sc) return;
    // L'apercu se recalcule quand une declaration calculee, une constante ou une valeur de depart change.
    std::string key;
    const auto addScript = [&](const hmi::Script& s) {
        for (const auto& d : s.decls) {
            key += std::to_string(s.id) + "|" + d.name + "|" + d.type + "|" + d.value + "|" + (d.computed ? d.calc : std::string{}) + "|"
                 + de::localsText(d.locals) + "\n";
        }
    };
    for (const auto& s : doc_->project.programs.scripts) addScript(s);
    for (const auto& v : doc_->project.views) {
        for (const auto& s : v.scripts) addScript(s);
        if (!v.constants.empty()) addScript(hmi::viewmembers::constantsScript(v));   // 1.12.16
    }
    for (const auto& v : doc_->project.programs.variables) key += v.name + "=" + v.initial + "\n";
    if (key != previewKey_) {
        previewKey_ = key;
        previewError_.clear();
        preview_ = hmi::Runtime::computedObjects(doc_->project, &previewError_);
    }
    for (auto& row : rows_->rows) {
        if (!row.d.computed) continue;
        std::string u = row.d.name;
        for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        // 1.12.16 : une constante de vue calculee a chaque ouverture - pas de valeur avant que la vue s'ouvre.
        if (owner && place_ && place_->kind == de::Place::Kind::ViewConstants && hmi::viewmembers::computedAtOpen(doc_->project, *owner, row.d)) {
            row.object.reset();
            row.preview = "\xC3\xA0 chaque ouverture";
            row.previewError = false;
            continue;
        }
        if (const auto it = preview_.find(std::to_string(sc->id) + ":" + u); it != preview_.end() && it->second) {
            row.object = it->second;
            row.preview = sim::display(*it->second);
            row.previewError = false;
        } else if (!previewError_.empty()) {
            row.preview = previewError_;
            row.previewError = true;
        }
    }
}

// 1.12.7 : « fx Calcul... » ; 1.12.16 : la fenetre « Valeur initiale », en Code (a la ligne `line`).
bool HmiDeclGrid::editCalculation(std::size_t row, int line) { return editInitial(row, 1, line); }

// 1.12.16 (T4) : « Valeur initiale... » - le Brut ou le Code, une seule fenetre (calceditor, Spec::project).
bool HmiDeclGrid::editInitial(std::size_t row, int mode, int line) {
    if (row >= rows_->rows.size() || !usable() || !calceditor::available()) return false;
    std::string refused;
    if (role_ != hmi::decl::Role::Script)
        refused = "le code ne calcule qu'une constante ou une variable d'un script ou d'une vue ; ici, la valeur s'\xC3\xA9" "crit (en brut)";
    else if (tab_ == de::Tab::Parameters)
        refused = "un param\xC3\xA8tre prend une valeur par d\xC3\xA9" "faut \xC3\xA9" "crite (en brut)";
    if (mode == 1 && !refused.empty()) {
        say("Seule une constante ou une variable d'un script se calcule au d\xC3\xA9marrage.", true);
        return false;
    }
    const auto code = place_ && doc_ ? de::locate(doc_->project, *place_) : de::Code{};
    const hmi::View* view = nullptr;
    const hmi::Script* sc = ownerScript(code, &view);
    const auto& r = rows_->rows[row];
    const auto& d = r.d;
    calceditor::Spec spec;
    spec.title = "Valeur initiale de " + d.name;
    spec.name = d.name;
    spec.type = d.type;
    spec.constant = d.kind == hmi::DeclKind::Constant;
    spec.calc = d.computed || !d.calc.empty() ? d.calc : calceditor::starter(d.name, d.type);
    spec.locals = de::localsText(d.locals);
    spec.line = line;
    // 1.12.16 : la fenetre a deux modes - le Brut (la valeur ecrite) et le Code (la declaration calculee).
    spec.project = &doc_->project;
    spec.value = d.value;
    spec.code = mode == 1 || (mode < 0 && d.computed);
    spec.codeRefused = refused;
    if (sc && place_ && place_->kind == de::Place::Kind::Script) spec.owner = sc->name;
    // 1.12.16 : une constante de vue - lue d'ailleurs par Vue.Nom ; calculee au demarrage ou a chaque ouverture
    // (d'office quand son code lit un parametre de la popup).
    if (view && place_ && place_->kind == de::Place::Kind::ViewConstants) {
        spec.owner = view->name;
        spec.reopen = d.reopen ? 1 : 0;
        spec.reopenForced = !d.reopen && d.computed && hmi::viewmembers::computedAtOpen(doc_->project, *view, d);
    }
    // Ce que le code peut lire : les declarations du script (ses constantes d'abord), les variables IHM.
    if (sc) {
        for (const auto& x : sc->decls)
            if (x.id != d.id && x.kind == hmi::DeclKind::Constant) spec.readable.emplace_back(x.name, x.type);
        for (const auto& x : sc->decls)
            if (x.id != d.id && x.kind == hmi::DeclKind::Variable) spec.readable.emplace_back(x.name, x.type);
    }
    for (const auto& v : doc_->project.programs.variables) {
        if (spec.readable.size() >= 60) break;
        spec.readable.emplace_back(v.name, v.type);
    }
    // Le deballage d'une locale : sa valeur initiale lue dans son type (vide : la valeur de son type).
    spec.valueOf = [doc = doc_](const std::string& type, const std::string& value, std::string* why) -> sim::ObjRef {
        return doc ? hmi::Runtime::valueObject(doc->project, type, value, why) : nullptr;
    };
    // Ce que le calcul donne et ses fautes (le code enregistre ; Appliquer refait le tout).
    if (d.computed && !r.preview.empty()) spec.info += (r.previewError ? "Erreur : " : "Valeur au d\xC3\xA9marrage : ") + r.preview + "\n";
    const hmi::Declaration* inModel = nullptr;
    if (sc)
        for (const auto& x : sc->decls)
            if (x.id == d.id) inModel = &x;
    if (sc && inModel && d.computed)
        for (const auto& is : hmi::calc::analyse(doc_->project, *sc, *inModel, view).issues)
            spec.info += std::string(is.error ? "Faute" : "Attention") + (is.line > 0 ? " ligne " + std::to_string(is.line) : std::string{}) + " : "
                       + is.message + "\n";
    const hmi::Id id = d.id;
    // En direct : le projet copie, ce code et ces locales poses, le calcul fait comme au demarrage.
    if (place_)
        spec.preview = [doc = doc_, at = *place_, t = tab_, id](const std::string& calc, const std::string& locals) -> std::string {
            if (!doc) return {};
            hmi::Project copy = doc->project;
            const auto code = de::locate(copy, at);
            if (!code.valid()) return "Ce code n'existe plus.";
            std::size_t row = static_cast<std::size_t>(-1);
            const auto rows = de::rowsOf(*code.decls, t);
            for (std::size_t r = 0; r < rows.size(); ++r)
                if ((*code.decls)[rows[r]].id == id) row = r;
            if (row == static_cast<std::size_t>(-1)) return "Cette d\xC3\xA9" "claration n'est plus dans l'onglet.";
            std::string why;
            if (!de::setCalculation(copy, at, t, row, calc, locals, &why)) return "Faute : " + why;
            const hmi::Script* sc = nullptr;
            const hmi::View* view = nullptr;
            hmi::Script holder;   // 1.12.16 : les constantes d'une vue (viewmembers::constantsScript)
            if (at.kind == de::Place::Kind::Script) {
                for (const auto& g : copy.programs.scripts)
                    if (g.id == at.id) sc = &g;
            } else if (const hmi::View* v = copy.view(at.view)) {
                view = v;
                if (at.kind == de::Place::Kind::ViewConstants) {
                    holder = hmi::viewmembers::constantsScript(*v);
                    sc = &holder;
                }
                for (const auto& g : v->scripts)
                    if (g.id == at.id) sc = &g;
            }
            const hmi::Declaration* decl = nullptr;
            if (sc)
                for (const auto& x : sc->decls)
                    if (x.id == id) decl = &x;
            if (!sc || !decl) return {};
            if (!decl->computed) return "Pas de code : la valeur redevient \xC3\xA9" "crite (" + (decl->value.empty() ? std::string("vide") : decl->value) + ").";
            std::string out, error;
            const auto values = hmi::Runtime::computedObjects(copy, &error);
            std::string u = decl->name;
            for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (view && at.kind == de::Place::Kind::ViewConstants && hmi::viewmembers::computedAtOpen(copy, *view, *decl))
                out = std::string("Calcul\xC3\xA9" "e \xC3\xA0 chaque ouverture de ") + view->name
                    + " : sa valeur se voit quand la vue s'ouvre (en simulation, avec ses param\xC3\xA8tres).\n";
            else if (const auto it = values.find(std::to_string(sc->id) + ":" + u); it != values.end() && it->second)
                out = "Valeur au d\xC3\xA9marrage : " + sim::display(*it->second) + "\n";
            else if (!error.empty())
                out = "Erreur : " + error + " (le d\xC3\xA9marrage s'arr\xC3\xAAterait)\n";
            const auto a = hmi::calc::analyse(copy, *sc, *decl, view);
            for (const auto& is : a.issues)
                out += std::string(is.error ? "Faute" : "Attention") + (is.line > 0 ? " ligne " + std::to_string(is.line) : std::string{}) + " : "
                     + is.message + "\n";
            // Ce qu'il lit, et qui le lit (la colonne de droite de la fenetre).
            const auto list = [](const std::vector<std::string>& v) {
                std::string s;
                for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
                return s;
            };
            if (!a.uses.empty()) out += "Lit : " + list(a.uses) + "\n";
            if (const auto by = hmi::calc::readers(copy, *sc, *decl, view); !by.empty()) out += "Lu par : " + list(by) + "\n";
            return out;
        };
    // L'aide a la saisie du code : les noms du projet, ceux du script et les locales du moment.
    if (place_)
        spec.assist = [doc = doc_, at = *place_, id](ui::MultiLineText& editor, std::function<std::string()> locals) {
            auto src = assist::sourcesFor(doc);
            src.declarations = [doc, at, id, locals]() -> std::string {
                if (!doc) return {};
                const hmi::View* view = nullptr;
                const hmi::Script* sc = scriptAt(doc->project, at, &view);
                if (!sc) return {};
                for (const auto& x : sc->decls)
                    if (x.id == id) {
                        hmi::Declaration d = x;
                        std::vector<hmi::Declaration> parsed;
                        if (locals && de::parseLocals(doc->project, locals(), parsed, nullptr)) d.locals = std::move(parsed);
                        return assist::declarationsPrefix(hmi::calc::declarationsFor(*sc, d), hmi::decl::Role::Script);
                    }
                return {};
            };
            assist::attach(editor, std::move(src));
        };
    const std::weak_ptr<Rows> alive = rows_;
    // 1.12.16 : ce que la fenetre applique - le Brut (la valeur ecrite ; une declaration calculee le redevient)
    // ou le Code (le calcul, ses locales, le moment), en une commande (un Ctrl+Z).
    spec.outcome = [this, alive, id](const calceditor::Spec::Outcome& o) {
        if (alive.expired()) return;
        for (std::size_t i = 0; i < rows_->rows.size(); ++i) {
            if (rows_->rows[i].d.id != id) continue;
            const std::string name = rows_->rows[i].d.name;
            const bool wasComputed = rows_->rows[i].d.computed;
            const auto t = tab_;
            const auto at = place_;
            if (!at) return;
            if (!o.code) {
                const std::string value = o.value;
                if (change("Valeur initiale de " + name, [=](hmi::Project& p, std::string* why) {
                        if (wasComputed && !de::setCalculation(p, *at, t, i, "", "", why)) return false;
                        return de::set(p, *at, t, i, de::Column::Value, value, why);
                    }))
                    say(name + " := " + (value.empty() ? std::string("la valeur du type") : value)
                            + (wasComputed ? std::string(" - la valeur redevient \xC3\xA9" "crite.") : std::string{}),
                        false);
                return;
            }
            const std::string calc = o.calc, locals = o.locals;
            const int reopen = o.reopen;
            if (change((calc.find_first_not_of(" \t\r\n") == std::string::npos ? "Valeur \xC3\xA9" "crite de " : "Calcul de ") + name,
                       [=](hmi::Project& p, std::string* why) {
                           if (!de::setCalculation(p, *at, t, i, calc, locals, why)) return false;
                           return reopen < 0 || de::setReopen(p, *at, t, i, reopen == 1, why);
                       }))
                say(calc.find_first_not_of(" \t\r\n") == std::string::npos
                        ? name + " : la valeur redevient \xC3\xA9" "crite."
                        : name + (reopen == 1 ? " : calcul\xC3\xA9" "e \xC3\xA0 chaque ouverture" : " : calcul\xC3\xA9" "e au d\xC3\xA9marrage")
                              + " (Compiler dit ses fautes ; la case Valeur, ce qu'elle vaut).",
                    false);
            return;
        }
    };
    calceditor::ask(std::move(spec), [this, alive, id](const std::string& calc, const std::string& locals) {
        if (alive.expired()) return;
        for (std::size_t i = 0; i < rows_->rows.size(); ++i)
            if (rows_->rows[i].d.id == id) {
                const std::string name = rows_->rows[i].d.name;
                const auto t = tab_;
                const auto at = place_;
                if (!at) return;
                if (change((calc.find_first_not_of(" \t\r\n") == std::string::npos ? "Valeur \xC3\xA9" "crite de " : "Calcul de ") + name,
                           [=](hmi::Project& p, std::string* why) { return de::setCalculation(p, *at, t, i, calc, locals, why); }))
                    say(calc.find_first_not_of(" \t\r\n") == std::string::npos
                            ? name + " : la valeur redevient \xC3\xA9" "crite."
                            : name + " : calcul\xC3\xA9" "e au d\xC3\xA9marrage (Compiler dit ses fautes ; la case Valeur, ce qu'elle vaut).",
                        false);
                return;
            }
    });
    return true;
}

void HmiDeclGrid::pickType(std::size_t row) {
    if (row >= rows_->rows.size() || !usable()) return;
    if (!typepicker::available()) {
        say("Le s\xC3\xA9lecteur de types n'est pas ouvert ici : \xC2\xAB Autre type\xE2\x80\xA6 \xC2\xBB pour \xC3\xA9" "crire le type.", true);
        return;
    }
    const auto& d = rows_->rows[row].d;
    HmiTypePicker::Spec spec;
    spec.field = "Type de " + d.name;
    spec.current = d.type;
    spec.use = hmi::typereg::UseDeclaration;
    spec.doc = doc_;
    const std::weak_ptr<int> alive = alive_;
    const hmi::Id id = d.id;
    typepicker::ask(std::move(spec), [this, alive, id](const HmiTypePicker::Answer& a) {
        if (alive.expired() || a.type.empty()) return;   // la grille est partie, ou rien de choisi
        (void)setTypeOf(id, a.type);
    });
}

bool HmiDeclGrid::setTypeOf(hmi::Id declaration, const std::string& type) {
    for (std::size_t i = 0; i < rows_->rows.size(); ++i)
        if (rows_->rows[i].d.id == declaration) return setCell(i, de::Column::Type, type);
    say("Cette d\xC3\xA9" "claration n'est plus dans l'onglet : son type n'a pas chang\xC3\xA9.", true);
    return false;
}

paste::Target HmiDeclGrid::pasteTarget(const ui::TableView::PasteRequest& rq) {
    paste::Target tg;
    switch (tab_) {
        case de::Tab::Constants:  tg.noun = "constante"; tg.nouns = "constantes"; break;
        case de::Tab::Variables:
            tg.noun = role_ == hmi::decl::Role::Script ? "variable" : "locale";
            tg.nouns = tg.noun + "s";
            break;
        case de::Tab::Parameters: tg.noun = "param\xC3\xA8tre"; tg.nouns = "param\xC3\xA8tres"; tg.feminine = false; break;
    }
    const auto rowOf = [this](const std::string& key) -> int {
        for (std::size_t i = 0; i < rows_->rows.size(); ++i)
            if (sameText(rows_->rows[i].d.name, key)) return static_cast<int>(i);
        return -1;
    };
    const auto setter = [this, rowOf](de::Column c) {
        return [this, rowOf, c](const std::string& key, const std::string& value, std::string* why) {
            const int r = rowOf(key);
            if (r < 0) {
                if (why) *why = "introuvable";
                return false;
            }
            const bool ok = setCell(static_cast<std::size_t>(r), c, value);
            if (!ok && why) {
                *why = message_;
                if (why->rfind("Refus\xC3\xA9 : ", 0) == 0) *why = why->substr(std::string("Refus\xC3\xA9 : ").size());
            }
            return ok;
        };
    };
    const auto aliases = [](de::Column c) -> std::vector<std::string> {
        switch (c) {
            case de::Column::Name:
                return {"Name", "Identifiant", "Identifier", "Variable", "Constante", "Constant", "Param\xC3\xA8tre", "Parameter", "Locale",
                        "Symbole", "Nom de la variable"};
            case de::Column::Type:        return {"Type de donn\xC3\xA9" "e", "Data type", "DataType", "Type ST"};
            case de::Column::Value:
                return {"Valeur", "Value", "Initiale", "Valeur initiale", "Initial value", "Init", "D\xC3\xA9" "faut", "Default",
                        "Valeur par d\xC3\xA9" "faut", "Default value"};
            case de::Column::Storage:     return {"Storage", "M\xC3\xA9moire", "Persistance", "Dur\xC3\xA9" "e de vie", "Lifetime"};
            case de::Column::Mode:        return {"Sens", "Direction", "Passage", "Kind"};
            case de::Column::Visibility:  return {"Visibility", "Port\xC3\xA9" "e", "Scope", "Acc\xC3\xA8s", "Access"};
            case de::Column::Description:
                return {"Description", "Commentaire", "Comment", "Doc", "Libell\xC3\xA9", "Label", "Remarque"};
        }
        return {};
    };
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i] == kUses) {
            tg.columns.push_back(paste::column("Utilisations", {"Uses", "Usages", "Utilisation"}, static_cast<int>(i), nullptr));
            continue;
        }
        const auto c = static_cast<de::Column>(columns_[i]);
        std::function<bool(const std::string&, const std::string&, std::string*)> set;
        if (c != de::Column::Name) set = setter(c);            // le nom dit la ligne (la cle) : jamais ecrit par le collage
        auto col = paste::column(de::columnTitle(c, tab_), aliases(c), static_cast<int>(i), std::move(set), c == de::Column::Name);
        tg.columns.push_back(std::move(col));
    }
    tg.exists = [rowOf](const std::string& key) { return rowOf(key) >= 0; };
    tg.create = [this](const std::string& key, const std::map<std::string, std::string>&, paste::Notes&, std::vector<std::string>&,
                       std::string* why) -> std::string {
        Id made = kNoId;
        const de::Place at = place_ ? *place_ : de::Place{};
        const de::Tab tab = tab_;
        std::string refused;
        if (!change("Coller " + noun(false) + " " + key + " (" + ownerLabel() + ")",
                    [&](hmi::Project& p, std::string* w) {
                        const bool ok = de::add(p, at, tab, -1, key, &made, w);
                        if (!ok && w) refused = *w;
                        return ok;
                    })) {
            if (why) *why = refused;
            return {};
        }
        refresh();
        for (const auto& r : rows_->rows)
            if (r.d.id == made) return r.d.name;
        return key;
    };
    tg.freeKey = [this](const std::string& key) {
        if (!place_ || !doc_) return key;
        return de::freeName(de::locate(doc_->project, *place_), key);
    };
    tg.keysFromAnchor = paste::keysFrom(*table_, rq.anchorViewRow, static_cast<std::size_t>(std::max(0, tableColumnOf(de::Column::Name))));
    return tg;
}

void HmiDeclGrid::onLayout() {
    const auto b = bounds();
    constexpr float kTools = 36.f, kHint = 22.f, kSearch = 230.f;
    const float searchW = std::min(kSearch, std::max(0.f, b.w * 0.4f));
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 4.f), kTools});
    search_->setBounds({b.right() - searchW, b.y + 4.f, searchW, kTools - 8.f});
    // 1.12.7 : le deballage sous la table (un tiers de la hauteur, 110 a 220 px), quand il montre une valeur.
    const float unfoldH = unfoldShown_ ? std::clamp((b.h - kTools - kHint) * 0.38f, 110.f, 220.f) : 0.f;
    table_->setBounds({b.x, b.y + kTools, b.w, std::max(0.f, b.h - kTools - kHint - unfoldH)});
    if (unfold_) unfold_->setBounds({b.x, b.bottom() - kHint - unfoldH, b.w, unfoldH});
    hint_->setBounds({b.x, b.bottom() - kHint, b.w, kHint});
}

void HmiDeclGrid::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

ui::EventResult HmiDeclGrid::onEvent(const ui::InputEvent& ev) {
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k || k->repeat || table_->cellEditing() || !table_->focused()) return ui::EventResult::Ignored;
    if (k->key == ui::Key::Insert && k->mods.none()) {
        (void)add();
        return ui::EventResult::Consumed;
    }
    if (k->key == ui::Key::Delete && k->mods.none()) {
        (void)removeSelected();
        return ui::EventResult::Consumed;
    }
    if (k->key == ui::Key::D && k->mods.ctrl && !k->mods.shift && !k->mods.alt) {
        (void)duplicateSelected();
        return ui::EventResult::Consumed;
    }
    if ((k->key == ui::Key::Up || k->key == ui::Key::Down) && k->mods.alt && !k->mods.ctrl && !k->mods.shift) {
        (void)moveSelected(k->key == ui::Key::Up ? -1 : +1);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// --------------------------------------------------------------- le bandeau ----
HmiDeclBanner::HmiDeclBanner(std::string id) : ui::Widget(std::move(id)) {
    auto b = std::make_unique<ui::Button>("Migrer ce code", this->id() + ".migrate");
    b->setStyle(ui::Button::Style::Primary);
    b->setTooltip("Passer les blocs VAR de ce code dans ses onglets (leurs commentaires en documentation) : une commande, Ctrl+Z la reprend");
    button_ = &static_cast<ui::Button&>(addChild(std::move(b)));
    links_ += button_->clicked->connect([this] { migrate->emit(); });
    setVisibility(ui::Visibility::Collapsed);
}

void HmiDeclBanner::setText(std::string text) {
    if (text == text_) return;
    text_ = std::move(text);
    setTooltip(text_);                         // le texte entier, s'il est coupe
    setVisibility(text_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    invalidateLayout();
    invalidate();
}

void HmiDeclBanner::onLayout() {
    const auto b = bounds();
    const float w = 150.f;
    button_->setBounds({b.right() - w - 6.f, b.y + 3.f, w, b.h - 6.f});
}

void HmiDeclBanner::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.headerBg);
    ctx.r.fillRect({b.x, b.y, 4.f, b.h}, ctx.theme.color.warning);
    ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, ctx.theme.color.border);
    const float lh = ctx.r.lineHeight(ctx.theme.font.smallUi);
    // Le texte s'arrete avant le bouton (des points de suite ; l'infobulle le dit en entier).
    const float room = std::max(0.f, button_->bounds().x - (b.x + 12.f) - 10.f);
    std::string shown = text_;
    if (ctx.r.measure(shown, ctx.theme.font.smallUi).width > room) {
        const std::string dots = "\xE2\x80\xA6";
        std::size_t cut = std::min(shown.size(), ctx.r.fitCharacters(shown, ctx.theme.font.smallUi,
                                                                     std::max(0.f, room - ctx.r.measure(dots, ctx.theme.font.smallUi).width)));
        while (cut > 0 && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) --cut;
        shown = shown.substr(0, cut) + dots;
    }
    ctx.r.drawText({b.x + 12.f, b.y + (b.h - lh) / 2.f}, shown, ctx.theme.font.smallUi, ctx.theme.color.text);
}

// ------------------------------------------------------------- les onglets ----
HmiCodeTabs::HmiCodeTabs(std::string id, hmi::DocumentPtr doc, HmiDeclGrid::Apply apply, ui::WidgetPtr codePage,
                         const std::vector<de::Tab>& tabs, HmiDeclBanner* banner)
    : ui::TabControl(std::move(id)), doc_(std::move(doc)), banner_(banner) {
    const std::string base = this->id();
    addTab({"Code", ui::Icon::Code}, std::move(codePage));
    for (const auto t : tabs) {
        const char* key = t == de::Tab::Constants ? ".constants" : t == de::Tab::Variables ? ".variables" : ".parameters";
        auto g = std::make_unique<HmiDeclGrid>(base + key, doc_, apply, t);
        HmiDeclGrid* raw = g.get();
        links_ += raw->message->connect([this](const std::string& text, bool warning) { message->emit(text, warning); });
        links_ += raw->usesRequested->connect([this](const std::string& name) { usesRequested->emit(name); });
        links_ += raw->useRequested->connect([this](const std::string& name, int k) { useRequested->emit(name, k); });
        addTab({raw->title(), t == de::Tab::Constants ? ui::Icon::Constant : ui::Icon::Variable}, std::move(g));
        grids_.emplace_back(t, raw);
    }
    if (banner_) links_ += banner_->migrate->connect([this] { migrateRequested->emit(); });
}

void HmiCodeTabs::setPlace(std::optional<de::Place> place, std::string why) {
    for (std::size_t i = 0; i < grids_.size(); ++i) {
        HmiDeclGrid& g = *grids_[i].second;
        g.setPlace(place, why);
        setTabTitle(i + 1, g.title());
        setTabBadge(i + 1, g.count() ? std::to_string(g.count()) : std::string{}, g.faultCount() ? ui::Tone::Error : ui::Tone::None);
    }
    if (banner_) {
        banner_->setText(place && doc_ ? declBannerText(doc_->project, *place) : std::string{});
        if (auto* page = banner_->parent()) page->invalidateLayout();
    }
}

HmiDeclGrid* HmiCodeTabs::grid(de::Tab tab) const noexcept {
    for (const auto& [t, g] : grids_)
        if (t == tab) return g;
    return nullptr;
}

std::size_t HmiCodeTabs::indexOf(de::Tab tab) const noexcept {
    for (std::size_t i = 0; i < grids_.size(); ++i)
        if (grids_[i].first == tab) return i + 1;
    return 0;
}

bool HmiCodeTabs::showDeclaration(const std::string& name) {
    for (std::size_t i = 0; i < grids_.size(); ++i)
        if (grids_[i].second->selectName(name)) {
            setCurrentIndex(i + 1);
            return true;
        }
    return false;
}

bool selectNextUse(ui::MultiLineText& editor, const std::string& name, std::string* said, int which) {
    const std::string text = editor.text();
    const auto uses = de::usesIn(text, name);
    if (uses.empty()) {
        if (said) *said = name + " : aucune utilisation dans le code.";
        return false;
    }
    const std::size_t at = std::min(editor.caretOffset(), text.size());
    std::size_t lineStart = 0;
    if (at > 0)
        if (const auto nl = text.rfind('\n', at - 1); nl != std::string::npos) lineStart = nl + 1;
    const int line = static_cast<int>(editor.caretLine()) + 1;
    const int column = static_cast<int>(at - lineStart) + 1;
    std::size_t k = 0;
    while (k < uses.size() && (uses[k].line < line || (uses[k].line == line && uses[k].column < column))) ++k;
    if (k == uses.size()) k = 0;                                    // apres la derniere : la premiere
    if (which >= 0) k = std::min(uses.size() - 1, static_cast<std::size_t>(which));   // 1.12.18 : celle du menu
    editor.selectRange(static_cast<std::size_t>(uses[k].line - 1), static_cast<std::uint32_t>(uses[k].column - 1),
                       static_cast<std::uint32_t>(uses[k].length));
    if (said)
        *said = "Utilisation " + std::to_string(k + 1) + " sur " + std::to_string(uses.size()) + " de " + name + " (ligne "
              + std::to_string(uses[k].line) + ")";
    return true;
}

std::string declarationNamed(std::string_view message) {
    const std::string_view head = "d\xC3\xA9" "claration \xC2\xAB ";
    if (message.substr(0, head.size()) != head) return {};
    const auto end = message.find(" \xC2\xBB", head.size());
    return end == std::string_view::npos ? std::string{} : std::string(message.substr(head.size(), end - head.size()));
}

std::string declBannerText(const hmi::Project& p, const de::Place& place) {
    const auto code = de::locate(p, place);
    if (!code.valid() || !code.declares) return {};
    const auto x = hmi::decl::extract(*code.body);
    if (x.blocks.empty()) return {};
    const auto plan = hmi::migrate::plan(p, [&](const de::Place& at) { return samePlace(at, place); });
    const hmi::migrate::Item* mine = nullptr;
    for (const auto& it : plan.items)
        if (samePlace(it.place, place)) mine = &it;
    if (mine && mine->migrated)
        return "Ancien format : ce code d\xC3\xA9" "clare encore " + plural(mine->decls.size(), "nom", "noms")
             + " dans son texte (VAR \xE2\x80\xA6 END_VAR). \xC2\xAB Migrer ce code \xC2\xBB les passe dans ses onglets.";
    return "Ancien format : ce code garde ses blocs VAR (" + (mine ? mine->why : std::string("rien \xC3\xA0 migrer")) + ").";
}

bool migrateOne(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply, const de::Place& place,
                std::string* report) {
    if (!doc) return false;
    const auto plan = hmi::migrate::plan(doc->project, [&](const de::Place& at) { return samePlace(at, place); });
    const hmi::migrate::Item* mine = nullptr;
    for (const auto& it : plan.items)
        if (samePlace(it.place, place)) mine = &it;
    if (!mine) {
        if (report) *report = "Rien \xC3\xA0 migrer : ce code n'a pas de bloc VAR.";
        return false;
    }
    if (!mine->migrated) {
        if (report) *report = "Non migr\xC3\xA9 : " + mine->why;
        return false;
    }
    auto cmd = hmi::changeProject(doc, "Migrer les d\xC3\xA9" "clarations de " + mine->place.label,
                                  [&](hmi::Project& q) { (void)hmi::migrate::apply(q, plan); });
    if (cmd) apply(std::move(cmd));
    if (report) *report = hmi::migrate::summary(plan) + " (Ctrl+Z reprend la migration)";
    return true;
}

} // namespace app
