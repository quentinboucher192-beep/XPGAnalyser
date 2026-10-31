#include "HmiFolderTable.hpp"

#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <iterator>
#include <cctype>

namespace app {

namespace fold = hmi::fold;
using Where = ui::TreeView::DropWhere;

// ------------------------------------------------------------ le modele ------
class FolderRowsModel final : public ui::ITableModel {
public:
    struct Line {
        HmiFolderTable::Row        row;
        std::vector<std::string>   cells;
        ui::CellStyle              style;     // la premiere colonne (un dossier : ses autres cases sont grises)
        std::vector<ui::CellStyle> styles;    // un element : chaque colonne
        int                        expander{-1};   // 1.16.1 : un element ou une ligne depliee (setUnfold) : sa fleche
        std::size_t                owner{0};       // 1.16.1 : la ligne de son element (un dossier, un element : la sienne)
    };
    FolderRowsModel(std::vector<std::string> headers, std::vector<Line> lines, bool folders)
        : headers_(std::move(headers)), lines_(std::move(lines)), folders_(folders) {}
    std::function<bool(const std::string& path, const std::string& name)> rename;
    std::function<bool(hmi::Id, std::size_t)>                     itemEditable;
    std::function<bool(hmi::Id, std::size_t, const std::string&)> itemCommit;

    [[nodiscard]] std::size_t rowCount() const override { return lines_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < lines_.size() && c < lines_[r].cells.size() ? lines_[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines_.size()) return {};
        const auto& line = lines_[r];
        if (line.row.folder) {
            ui::CellStyle s;
            if (c == 0) {
                s = line.style;
                s.indent = static_cast<float>(line.row.depth) * 18.f;
            } else {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        ui::CellStyle s = c == 0 ? line.style : c < line.styles.size() ? line.styles[c] : ui::CellStyle{};
        if (c == 0 && folders_) {
            s.indent = static_cast<float>(line.row.depth) * 18.f + 16.f;   // sous la fleche de son dossier
            s.expander = line.expander;                                    // 1.16.1 : un element qui se deplie
        } else if (c == 0 && (line.expander >= 0 || !line.row.detail.empty())) {
            s.indent = static_cast<float>(line.row.depth) * 18.f;          // une liste melee : le retrait des deballages
            s.expander = line.expander;
        }
        return s;
    }
    // L'ordre est celui du projet : trier par une colonne garderait les dossiers en tete. 1.16.1 : une ligne depliee
    // se trie avec son element (sa case) - dans les deux sens, elle reste dessous, dans son ordre (le tri est stable).
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        const bool fa = a < lines_.size() && lines_[a].row.folder, fb = b < lines_.size() && lines_[b].row.folder;
        if (fa != fb) return fa;
        const auto owner = [this](ui::RowIndex r) { return r < lines_.size() ? static_cast<ui::RowIndex>(lines_[r].owner) : r; };
        return cellText(owner(a), c) < cellText(owner(b), c);
    }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines_.size()) return false;
        if (!lines_[r].row.detail.empty()) return false;   // 1.16.1 : une ligne depliee se lit
        if (lines_[r].row.folder) return c == 0 && static_cast<bool>(rename);
        return itemEditable && itemEditable(lines_[r].row.id, c);
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (!editable(r, c)) return false;
        if (lines_[r].row.folder) return rename(lines_[r].row.path, std::string(text));
        return itemCommit && itemCommit(lines_[r].row.id, c, std::string(text));
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= lines_.size() || !lines_[r].row.folder) return {};
        return lines_[r].row.path + "\nGlisse des lignes dessus pour les y ranger ; double-clic sur son nom : le renommer.";
    }
    [[nodiscard]] bool canDropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex to, Where where) const override {
        if (!folders_ || from.empty()) return false;
        bool anyItem = false, anyFolder = false;
        for (const auto f : from) {
            if (f >= lines_.size() || !lines_[f].row.detail.empty()) return false;   // 1.16.1 : une ligne depliee ne se glisse pas
            (lines_[f].row.folder ? anyFolder : anyItem) = true;
        }
        // Sous les lignes : a la racine (s'il y a quelque chose a y remonter).
        if (to == ui::TableView::kNoRow) {
            if (where != Where::Into) return false;
            for (const auto f : from) {
                const auto& r = lines_[f].row;
                if (r.folder ? !hmi::types::folderParent(r.path).empty() : !r.path.empty()) return true;
            }
            return false;
        }
        if (to >= lines_.size() || !lines_[to].row.detail.empty()) return false;
        const auto& target = lines_[to].row;
        if (target.folder) {
            if (where != Where::Into) return false;
            // Pas un dossier dans lui-meme (ni dans un des siens), pas deja la.
            bool useful = false;
            for (const auto f : from) {
                const auto& r = lines_[f].row;
                if (r.folder) {
                    if (fold::inside(target.path, r.path)) return false;
                    if (!fold::sameFolder(hmi::types::folderParent(r.path), target.path)) useful = true;
                } else if (!fold::sameFolder(r.path, target.path)) {
                    useful = true;
                }
            }
            return useful;
        }
        // Entre deux elements : reordonner (des elements seulement).
        return anyItem && !anyFolder && where != Where::Into;
    }
    [[nodiscard]] const Line& line(ui::RowIndex r) const { return lines_[r]; }

private:
    std::vector<std::string> headers_;
    std::vector<Line>        lines_;
    bool                     folders_{true};
};

// ------------------------------------------------------------ la table -------
HmiFolderTable::HmiFolderTable(ui::TableView& table, hmi::DocumentPtr doc, Apply apply)
    : table_(table), doc_(std::move(doc)), apply_(std::move(apply)) {
    table_.setSelectionMode(ui::SelectionMode::Extended);
    table_.setRowDragEnabled(true);
    links_ += table_.expanderClicked->connect([this](ui::RowIndex r) {
        const auto* row = this->row(r);
        if (row && row->folder) setCollapsed(row->path, !collapsed(row->path));
        else if (row && unfold_) unfoldToggled->emit(row->id, row->detail);   // 1.16.1 : le volet deplie, puis refait la liste
    });
    links_ += table_.rowsDropped->connect([this](const std::vector<ui::RowIndex>& rows, ui::RowIndex to, Where where) {
        (void)drop(rows, to, where);
    });
}

HmiFolderTable::~HmiFolderTable() = default;

std::string HmiFolderTable::upper(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool HmiFolderTable::collapsed(const std::string& path) const { return collapsed_.count(upper(path)) > 0; }

void HmiFolderTable::setCollapsed(const std::string& path, bool on) {
    if (on) collapsed_.insert(upper(path));
    else collapsed_.erase(upper(path));
    relayout->emit();                         // le volet se reconstruit (et garde sa selection)
}

void HmiFolderTable::rebuild(std::optional<fold::List> list, const std::vector<hmi::Id>& shown, std::vector<std::string> headers,
                             const Cells& cells, const Style& style) {
    // La selection d'avant, a retrouver. 1.16.1 : une ligne depliee, par son element et son chemin.
    std::vector<hmi::Id> keepItems;
    std::vector<std::string> keepFolders;
    std::vector<std::pair<hmi::Id, std::string>> keepDetails;
    for (const auto r : table_.selectedModelRows())
        if (const auto* row = this->row(r)) {
            if (row->folder) keepFolders.push_back(row->path);
            else if (!row->detail.empty()) keepDetails.emplace_back(row->id, row->detail);
            else if (std::find(keepItems.begin(), keepItems.end(), row->id) == keepItems.end()) keepItems.push_back(row->id);
        }

    list_ = list;
    rows_.clear();
    items_ = 0;
    details_ = 0;
    std::vector<FolderRowsModel::Line> lines;
    const auto& p = doc_->project;
    const auto itemLine = [&](hmi::Id id, const std::string& folder, int depth) {
        FolderRowsModel::Line line;
        line.row = {false, id, folder, depth, {}};
        line.cells = cells(id);
        line.cells.resize(headers.size());
        line.style = style(id, 0);
        for (std::size_t c = 0; c < headers.size(); ++c) line.styles.push_back(c == 0 ? line.style : style(id, c));
        line.owner = lines.size();
        // 1.16.1 : ses lignes depliees, a la suite de la sienne.
        std::vector<Detail> parts;
        if (unfold_) line.expander = unfold_(id, parts);
        rows_.push_back(line.row);
        lines.push_back(std::move(line));
        ++items_;
        const std::size_t owner = lines.size() - 1;
        for (auto& d : parts) {
            FolderRowsModel::Line sub;
            sub.row = {false, id, folder, depth + std::max(1, d.depth), std::move(d.key)};
            sub.cells = std::move(d.cells);
            sub.cells.resize(headers.size());
            sub.styles = std::move(d.styles);
            sub.styles.resize(headers.size());
            sub.style = sub.styles.front();
            sub.expander = d.expander;
            sub.owner = owner;
            rows_.push_back(sub.row);
            lines.push_back(std::move(sub));
            ++details_;
        }
    };
    if (!list) {
        for (const auto id : shown) itemLine(id, {}, 0);
    } else {
        const auto all = fold::items(p, *list);
        const auto folderOf = [&](hmi::Id id) {
            for (const auto& i : all)
                if (i.id == id) return i.folder;
            return std::string{};
        };
        const auto folders = fold::allFolders(p, *list);
        // Un dossier, puis ce qu'il contient : ses sous-dossiers, puis ses elements.
        std::function<void(const std::string&, int)> emit = [&](const std::string& parent, int depth) {
            for (const auto& f : folders) {
                if (!fold::sameFolder(hmi::types::folderParent(f), parent)) continue;
                std::size_t count = 0;
                for (const auto id : shown)
                    if (fold::inside(folderOf(id), f)) ++count;
                FolderRowsModel::Line line;
                line.row = {true, hmi::kNoId, f, depth, {}};
                line.owner = lines.size();
                line.cells.assign(headers.size(), std::string{});
                line.cells[0] = hmi::types::folderLeaf(f);
                if (line.cells.size() > 1) line.cells[1] = std::to_string(count) + " " + fold::noun(*list, count);
                line.style.bold = true;
                line.style.icon = collapsed(f) ? ui::Icon::Folder : ui::Icon::FolderOpen;
                line.style.iconTone = ui::Tone::Warning;
                line.style.expander = collapsed(f) ? 0 : 1;
                rows_.push_back(line.row);
                lines.push_back(std::move(line));
                if (!collapsed(f)) emit(f, depth + 1);
            }
            for (const auto id : shown)
                if (fold::sameFolder(folderOf(id), parent)) itemLine(id, folderOf(id), depth);
        };
        emit({}, 0);
    }
    model_ = std::make_shared<FolderRowsModel>(std::move(headers), std::move(lines), list.has_value());
    model_->rename = [this](const std::string& path, const std::string& name) { return renameFolder(path, name); };
    model_->itemEditable = itemEditable_;
    model_->itemCommit = itemCommit_;
    table_.setRowDragEnabled(list.has_value());
    table_.setModel(model_);
    // La selection retrouvee. 1.16.1 : une ligne depliee qui n'est plus montree (repliee, son membre renomme ou
    // retire) laisse la place a son element.
    for (const auto& [id, key] : keepDetails)
        if (rowOfDetail(id, key) < 0 && std::find(keepItems.begin(), keepItems.end(), id) == keepItems.end()) keepItems.push_back(id);
    std::vector<ui::RowIndex> again;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& r = rows_[i];
        if (!r.folder && r.detail.empty() && std::find(keepItems.begin(), keepItems.end(), r.id) != keepItems.end())
            again.push_back(static_cast<ui::RowIndex>(i));
        if (!r.detail.empty())
            for (const auto& [id, key] : keepDetails)
                if (id == r.id && upper(key) == upper(r.detail)) again.push_back(static_cast<ui::RowIndex>(i));
        if (r.folder)
            for (const auto& f : keepFolders)
                if (fold::sameFolder(f, r.path)) again.push_back(static_cast<ui::RowIndex>(i));
    }
    if (!again.empty()) table_.selectModelRows(again, false);
    // Un dossier qu'on vient de creer : son nom se tape tout de suite.
    if (!editAfter_.empty()) {
        const std::string path = editAfter_;
        editAfter_.clear();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].folder && fold::sameFolder(rows_[i].path, path)) {
                table_.selectModelRows({static_cast<ui::RowIndex>(i)}, false);
                (void)table_.beginCellEdit(static_cast<ui::RowIndex>(i), 0);
                break;
            }
    }
}

int HmiFolderTable::rowOfItem(hmi::Id id) const noexcept {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].folder && rows_[i].detail.empty() && rows_[i].id == id) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------- 1.16.1 : les deballages ------
void HmiFolderTable::setUnfold(Unfold unfold) {
    const bool first = !unfold_;
    unfold_ = std::move(unfold);
    if (!first) return;
    // Le clavier : avant les touches de la liste (Suppr, Ctrl+D...) posees par le volet.
    auto before = table_.keyHandler();
    table_.setKeyHandler([this, before = std::move(before)](const ui::KeyDown& k) { return unfoldKey(k) || (before && before(k)); });
}

std::string HmiFolderTable::detailAt(ui::RowIndex modelRow) const {
    const auto* r = row(modelRow);
    return r ? r->detail : std::string{};
}

int HmiFolderTable::rowOfDetail(hmi::Id item, const std::string& key) const noexcept {
    if (key.empty()) return -1;
    const std::string want = upper(key);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].folder && rows_[i].id == item && !rows_[i].detail.empty() && upper(rows_[i].detail) == want) return static_cast<int>(i);
    return -1;
}

std::string HmiFolderTable::selectedDetail() const {
    for (const auto r : table_.selectedModelRows())
        if (const auto* row = this->row(r); row && !row->detail.empty()) return row->detail;
    return {};
}

void HmiFolderTable::selectDetail(hmi::Id item, const std::string& key) {
    if (const int r = rowOfDetail(item, key); r >= 0) table_.selectModelRows({static_cast<ui::RowIndex>(r)});
}

// Gauche sur une ligne repliee (ou sans fleche) : sa ligne parente - l'element d'une ligne depliee, la ligne au-dessus
// d'elle, le dossier d'un element. Suppr et Ctrl+D sur une ligne depliee : rien (elle se lit), et pourquoi.
bool HmiFolderTable::unfoldKey(const ui::KeyDown& k) {
    const auto selected = table_.selectedModelRows();
    if (selected.empty() || !model_) return false;
    const ui::RowIndex at = selected.back();
    if (at >= rows_.size()) return false;
    const auto& r = rows_[at];
    if (!r.detail.empty() && ((k.key == ui::Key::Delete && k.mods.none()) || (k.key == ui::Key::D && k.mods.ctrl && !k.mods.alt))) {
        message->emit("Une ligne d\xC3\xA9pli\xC3\xA9" "e se lit : elle se change dans son propre type (double-clic pour l'ouvrir).");
        return true;
    }
    if (k.key != ui::Key::Left || !k.mods.none()) return false;
    const int open = r.folder ? (collapsed(r.path) ? 0 : 1) : model_->line(at).expander;
    if (open == 1 || r.depth == 0) return false;   // deplie : la table le replie
    for (std::size_t i = at; i-- > 0;)
        if (rows_[i].depth == r.depth - 1) {
            table_.selectModelRows({static_cast<ui::RowIndex>(i)});
            return true;
        }
    return false;
}

hmi::Id HmiFolderTable::itemAt(ui::RowIndex modelRow) const noexcept {
    const auto* r = row(modelRow);
    return r && !r->folder ? r->id : hmi::kNoId;   // 1.16.1 : une ligne depliee - son element
}

std::vector<hmi::Id> HmiFolderTable::selectedItems() const {
    std::vector<hmi::Id> out;
    for (const auto r : table_.selectedModelRows())
        if (const auto id = itemAt(r); id != hmi::kNoId && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    return out;
}

hmi::Id HmiFolderTable::selectedItem() const {
    const auto all = selectedItems();
    return all.empty() ? hmi::kNoId : all.front();
}

std::string HmiFolderTable::selectedFolder() const {
    for (const auto r : table_.selectedModelRows())
        if (const auto* row = this->row(r); row && row->folder) return row->path;
    return {};
}

std::string HmiFolderTable::targetFolder() const {
    for (const auto r : table_.selectedModelRows())
        if (const auto* row = this->row(r)) return row->path;
    return {};
}

void HmiFolderTable::selectItem(hmi::Id id) {
    if (!list_ || id == hmi::kNoId) {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (!rows_[i].folder && rows_[i].detail.empty() && rows_[i].id == id) table_.selectModelRows({static_cast<ui::RowIndex>(i)});
        return;
    }
    // Ses dossiers deplies d'abord (la table se reconstruit).
    const std::string folder = fold::folderOf(doc_->project, *list_, id);
    bool opened = false;
    for (const auto& f : hmi::types::folderChain(folder))
        if (collapsed(f)) {
            collapsed_.erase(upper(f));
            opened = true;
        }
    if (opened) relayout->emit();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].folder && rows_[i].detail.empty() && rows_[i].id == id) {
            table_.selectModelRows({static_cast<ui::RowIndex>(i)});
            return;
        }
}

void HmiFolderTable::selectFolder(const std::string& path) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].folder && fold::sameFolder(rows_[i].path, path)) {
            table_.selectModelRows({static_cast<ui::RowIndex>(i)});
            return;
        }
}

std::string HmiFolderTable::newFolder() {
    if (!list_) {
        message->emit("Les dossiers se rangent dans une liste : choisis Vues, Popups, un mod\xC3\xA8le... dans l'arbre.");
        return {};
    }
    const std::string parent = targetFolder();
    const auto all = fold::allFolders(doc_->project, *list_);
    const auto taken = [&](const std::string& f) {
        return std::any_of(all.begin(), all.end(), [&](const std::string& x) { return fold::sameFolder(x, f); });
    };
    std::string path;
    for (int i = 1; i < 1000; ++i) {
        const std::string leaf = i == 1 ? std::string("Nouveau dossier") : "Nouveau dossier " + std::to_string(i);
        path = parent.empty() ? leaf : parent + "/" + leaf;
        if (!taken(path)) break;
    }
    std::string why;
    bool ok = false;
    const auto list = *list_;
    auto cmd = hmi::changeProject(doc_, "Nouveau dossier " + path, [&](hmi::Project& p) { ok = fold::addFolder(p, list, path, &why); });
    if (!ok) {
        message->emit("Dossier non cr\xC3\xA9\xC3\xA9 : " + why);
        return {};
    }
    editAfter_ = path;
    if (cmd) apply_(std::move(cmd));
    message->emit("Dossier \xC2\xAB " + path + " \xC2\xBB cr\xC3\xA9\xC3\xA9 : tape son nom (Entr\xC3\xA9" "e), puis glisse des lignes dessus.");
    return path;
}

bool HmiFolderTable::renameFolder(const std::string& path, const std::string& name) {
    if (!list_) return false;
    std::string leaf = name;
    while (!leaf.empty() && std::isspace(static_cast<unsigned char>(leaf.back()))) leaf.pop_back();
    while (!leaf.empty() && std::isspace(static_cast<unsigned char>(leaf.front()))) leaf.erase(leaf.begin());
    if (leaf.find('/') != std::string::npos) {
        message->emit("Un nom de dossier ne contient pas de / : glisse le dossier pour le ranger ailleurs.");
        return false;
    }
    const std::string parent = hmi::types::folderParent(path);
    const std::string target = parent.empty() ? leaf : parent + "/" + leaf;
    if (target == path) return true;
    std::string why;
    bool ok = false;
    const auto list = *list_;
    auto cmd = hmi::changeProject(doc_, "Renommer le dossier " + path, [&](hmi::Project& p) { ok = fold::renameFolder(p, list, path, target, &why); });
    if (!ok) {
        message->emit("Dossier non renomm\xC3\xA9 : " + why);
        return false;
    }
    if (collapsed(path)) {
        collapsed_.erase(upper(path));
        collapsed_.insert(upper(target));
    }
    if (cmd) apply_(std::move(cmd));
    message->emit("Dossier renomm\xC3\xA9 : " + target + " (ses \xC3\xA9l\xC3\xA9ments suivent)");
    return true;
}

bool HmiFolderTable::deleteFolder(const std::string& path) {
    if (!list_ || path.empty()) return false;
    const auto list = *list_;
    const std::size_t inside = fold::countIn(doc_->project, list, path, true);
    bool ok = false;
    auto cmd = hmi::changeProject(doc_, "Supprimer le dossier " + path, [&](hmi::Project& p) { ok = fold::removeFolder(p, list, path); });
    if (!ok) return false;
    if (cmd) apply_(std::move(cmd));
    message->emit("Dossier " + path + " supprim\xC3\xA9 : " + (inside == 0 ? std::string("il \xC3\xA9tait vide")
                                                                       : std::to_string(inside) + " " + fold::noun(list, inside) + " remont\xC3\xA9(s) d'un cran")
                  + " (Ctrl+Z le rend)");
    return true;
}

bool HmiFolderTable::moveItems(const std::vector<hmi::Id>& ids, const std::string& folder) {
    if (!list_ || ids.empty()) return false;
    const auto list = *list_;
    std::size_t n = 0;
    auto cmd = hmi::changeProject(doc_, "Ranger " + std::to_string(ids.size()) + " " + fold::noun(list, ids.size())
                                            + (folder.empty() ? std::string(" \xC3\xA0 la racine") : " dans " + folder),
                                  [&](hmi::Project& p) { n = fold::moveToFolder(p, list, ids, folder); });
    if (n == 0) return false;
    if (cmd) apply_(std::move(cmd));
    message->emit(std::to_string(n) + " " + fold::noun(list, n) + " " + fold::agreed(list, n, "rang\xC3\xA9") + " "
                  + (folder.empty() ? std::string("\xC3\xA0 la racine") : "dans " + folder) + " (Ctrl+Z pour annuler)");
    return true;
}

bool HmiFolderTable::drop(const std::vector<ui::RowIndex>& from, ui::RowIndex to, Where where) {
    if (!list_ || from.empty()) return false;
    const auto list = *list_;
    std::vector<hmi::Id> ids;
    std::vector<std::string> folders;
    for (const auto r : from)
        if (const auto* row = this->row(r)) {
            if (row->folder) folders.push_back(row->path);
            else if (row->detail.empty()) ids.push_back(row->id);   // 1.16.1 : une ligne depliee ne se glisse pas
        }
    // La cible : un dossier (dans), la racine, ou un element (avant / apres).
    const Row* target = to == ui::TableView::kNoRow ? nullptr : this->row(to);
    if (target && !target->detail.empty()) return false;
    if (!target || target->folder) {
        const std::string into = target ? target->path : std::string{};
        std::string why;
        std::size_t moved = 0;
        bool refused = false;
        // Le nom de la commande (l'historique) : "Ranger 3 vues dans Ligne 1".
        const std::string label = "Ranger " + (folders.empty() ? std::to_string(ids.size()) + " " + fold::noun(list, ids.size()) + " " : std::string{})
                                + (into.empty() ? std::string("\xC3\xA0 la racine") : "dans " + into);
        auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) {
            // Les dossiers d'abord (leurs elements les suivent), puis les elements.
            for (const auto& f : folders) {
                if (fold::sameFolder(hmi::types::folderParent(f), into)) continue;
                if (fold::moveFolder(p, list, f, into, &why)) ++moved;
                else refused = true;
            }
            moved += fold::moveToFolder(p, list, ids, into);
        });
        if (moved == 0) {
            if (refused) message->emit("Rien n'est d\xC3\xA9plac\xC3\xA9 : " + why);
            return false;
        }
        if (cmd) apply_(std::move(cmd));
        // "4 vues rangees dans Armoires" ; avec un dossier : "3 elements".
        const std::string what = folders.empty() ? fold::noun(list, moved) + " " + fold::agreed(list, moved, "rang\xC3\xA9")
                                                 : std::string(moved > 1 ? "\xC3\xA9l\xC3\xA9ments rang\xC3\xA9s" : "\xC3\xA9l\xC3\xA9ment rang\xC3\xA9");
        message->emit(std::to_string(moved) + " " + what + " " + (into.empty() ? std::string("\xC3\xA0 la racine") : "dans " + into)
                      + (refused ? " ; un dossier refus\xC3\xA9 : " + why : std::string{}) + " (Ctrl+Z pour annuler)");
        return true;
    }
    // Entre deux elements : reordonner.
    if (ids.empty() || !folders.empty()) return false;
    bool ok = false;
    const hmi::Id anchor = target->id;
    const bool after = where == Where::After;
    auto cmd = hmi::changeProject(doc_, "D\xC3\xA9placer " + std::to_string(ids.size()) + " " + fold::noun(list, ids.size()),
                                  [&](hmi::Project& p) { ok = fold::moveNear(p, list, ids, anchor, after); });
    if (!ok) return false;
    if (cmd) apply_(std::move(cmd));
    // Le nom de la cible (sans ce que sa case ajoute : " * demarrage"...).
    std::string anchorName = model_->cellText(to, 0);
    for (const auto& item : fold::items(doc_->project, list))
        if (item.id == anchor) {
            anchorName = item.name;
            break;
        }
    message->emit(std::to_string(ids.size()) + " " + fold::noun(list, ids.size()) + " " + fold::agreed(list, ids.size(), "d\xC3\xA9plac\xC3\xA9")
                  + (after ? " apr\xC3\xA8s " : " avant ") + anchorName + " (Ctrl+Z pour annuler)");
    return true;
}

} // namespace app
