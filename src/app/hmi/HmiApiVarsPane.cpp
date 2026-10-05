// IHM > Configuration > VARIABLES DU PROGRAMME : les variables de l'automate en
// arbre (1.11.1, chantier API-V, decision 104). Voir HmiApiVarsPane.hpp.
#include "HmiApiVarsPane.hpp"
#include "HmiScriptPanes.hpp"            // glisser / inserer : l'editeur de script de l'IHM

#include "../../hmi/HmiApiVars.hpp"     // le modele d'API-M
#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <variant>

namespace app {

namespace {

using K = ApiVarNode::Kind;
using A = ApiVarNode::Access;

constexpr float kIndent = 16.f;
const std::string kDot = "  \xC2\xB7  ";

std::string bytesText(double b) {
    char buf[64];
    if (b < 0) return "?";
    if (b > 0 && b < 1) std::snprintf(buf, sizeof buf, "%.0f bit%s", b * 8, b * 8 > 1 ? "s" : "");
    else if (b < 1024) std::snprintf(buf, sizeof buf, "%.0f o", b);
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f Ko", b / 1024);
    else std::snprintf(buf, sizeof buf, "%.1f Mo", b / 1024 / 1024);
    return buf;
}

bool folderKind(K k) noexcept { return k == K::Group || k == K::Unit || k == K::Scope; }

// 1.11.2 (API-V) : pourquoi une constante de l'automate ne s'ecrit pas (la raison de Compiler).
constexpr const char* kConstantWhy = "une constante de l'automate : l'IHM la lit, un script ne peut pas l'\xC3\xA9" "crire";

// La phrase de l'acces, quand le modele n'en donne pas.
std::string accessSentence(const ApiVarNode& n) {
    if (!n.accessWhy.empty()) return n.accessWhy;
    switch (n.access) {
        case A::ReadWrite:
            return "l'IHM la lit et l'\xC3\xA9" "crit" + (n.reference.empty() ? std::string{} : " (adresse " + n.reference + ")") + ".";
        case A::ReadOnly:
            return "elle se lit mais ne s'\xC3\xA9" "crit pas" + (n.reference.empty() ? std::string{} : " (adresse " + n.reference + ")") + ".";
        case A::NoAddress:
            return "sans adresse : l'IHM la lit en simulation, pas sur l'automate r\xC3\xA9" "el. Pour la rendre accessible : "
                   "donne-lui une adresse dans Control Expert, puis Fichier \xE2\x80\xBA Importer.";   // 1.11.2 : le « tu » du plan (ef5dd88)
        case A::None: break;
    }
    return {};
}


// Les vues vivantes : l'ecran leur passe la souris pendant un glisser (routeDrag).
std::vector<HmiApiVarsView*>& liveViews() {
    static std::vector<HmiApiVarsView*> views;
    return views;
}

// Les identifiants de nos entrees dans le menu du clic droit de la table : la
// table prend les siens (1 a 5, 99, 100 et plus) et ignore les negatifs.
constexpr int kMenuCopy = -100;
constexpr int kMenuInsert = -101;
constexpr int kMenuUse = -200;          // -200 - k : le k-ieme emploi
constexpr std::size_t kMaxUsesListed = 40;

// Le premier editeur de script de l'IHM montre sous `w` (et sous ce point, si
// `at` n'est pas nul) ; nullptr : aucun.
HmiScriptsPane* shownScriptPane(ui::Widget& w, const gfx::Point* at) {
    if (!w.visible()) return nullptr;
    if (auto* pane = dynamic_cast<HmiScriptsPane*>(&w)) {
        // L'editeur et ce qui le tient jusqu'au volet (un onglet du volet) : montres.
        auto& ed = pane->editor();
        bool shown = true;
        for (ui::Widget* x = &ed; x && x != pane && shown; x = x->parent()) shown = x->visible();
        if (shown && ed.bounds().w > 0.f && (!at || ed.bounds().contains(*at))) return pane;
    }
    for (const auto& c : w.children())
        if (auto* found = shownScriptPane(*c, at)) return found;
    return nullptr;
}

ui::Widget& rootOf(ui::Widget& w) {
    ui::Widget* r = &w;
    while (r->parent()) r = r->parent();
    return *r;
}

} // namespace

const char* apiAccessLabel(ApiVarNode::Access a) noexcept {
    switch (a) {
        case A::ReadWrite: return "Lecture / \xC3\xA9" "criture";
        case A::ReadOnly:  return "Lecture seule";
        case A::NoAddress: return "Sans adresse \xE2\x80\x94 simulation seulement";
        case A::None:      break;
    }
    return "";
}

// ------------------------------------------------------------------ le modele -
class ApiVarsRows final : public ui::ITableModel {
public:
    explicit ApiVarsRows(const HmiApiVarsView& v) : v_(v) {}
    [[nodiscard]] std::size_t rowCount() const override { return v_.lines_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return HmiApiVarsView::ColumnCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kHeaders[] = {"Nom", "Type", "Port\xC3\xA9" "e", "Accessible", "Utilis\xC3\xA9" "e par l'IHM",
                                               "R\xC3\xA9" "f\xC3\xA9rence", "Taille"};
        c = v_.logicalColumn(c);
        return c < HmiApiVarsView::ColumnCount ? kHeaders[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override { return v_.lineText(r, v_.logicalColumn(c)); }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        c = v_.logicalColumn(c);
        ui::CellStyle s;
        if (r >= v_.lines_.size()) return s;
        const auto& l = v_.lines_[r];
        const auto* n = l.node;
        if (c == HmiApiVarsView::Name) {
            s.indent = kIndent * static_cast<float>(l.depth);
            s.expander = l.expandable ? (l.open ? 1 : 0) : -1;
            if (!n || n->kind == K::More) {             // « ... et N autres »
                s.fgTone = ui::Tone::Muted;
                return s;
            }
            if (folderKind(n->kind)) {
                s.bold = true;
                s.icon = n->kind == K::Unit ? ui::Icon::Program : (l.open ? ui::Icon::FolderOpen : ui::Icon::Folder);
                s.iconTone = ui::Tone::Accent;
                if (const auto it = v_.leavesUnder_.find(n); it != v_.leavesUnder_.end()) s.badge = std::to_string(it->second);
                return s;
            }
            // Le cadenas : un nom de l'automate ne se renomme pas ici (une
            // variable IHM, R1111-6 : pas de cadenas).
            s.monospace = true;
            s.icon = n->hmi ? ui::Icon::Variable : ui::Icon::Lock;
            s.iconTone = ui::Tone::Muted;
            return s;
        }
        if (!n || folderKind(n->kind) || n->kind == K::More) {
            if (c == HmiApiVarsView::Type) s.fgTone = ui::Tone::Muted;
            return s;
        }
        switch (c) {
            case HmiApiVarsView::Type: s.fgTone = ui::Tone::Accent; break;
            case HmiApiVarsView::Access:
                s.fgTone = n->access == A::ReadWrite ? ui::Tone::Ok : n->access == A::NoAddress ? ui::Tone::Warning : ui::Tone::Muted;
                s.bold = n->access == A::ReadWrite;
                break;
            case HmiApiVarsView::Usage: s.bold = l.uses > 0; break;
            case HmiApiVarsView::Reference: s.monospace = true; break;
            default: break;
        }
        return s;
    }
    // L'ordre est celui de l'arbre : les titres ne trient pas.
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= v_.lines_.size()) return {};
        const auto& l = v_.lines_[r];
        const auto* n = l.node;
        if (!n || n->kind == K::More)
            return "Les \xC3\xA9l\xC3\xA9ments suivants du tableau : la fl\xC3\xA8" "che (ou un double-clic) les montre.";
        if (folderKind(n->kind)) {
            const auto it = v_.leavesUnder_.find(n);
            std::string tip = n->name + " : " + std::to_string(it == v_.leavesUnder_.end() ? 0 : it->second) + " variable(s)";
            if (n->kind == K::Unit) tip += "\nUne unit\xC3\xA9 de programme : ses variables se lisent par " + n->path + ".<variable>.";
            return tip + "\nLa fl\xC3\xA8" "che (ou un double-clic) " + (l.open ? "le replie." : "le d\xC3\xA9plie.");
        }
        std::string tip = n->path + " : " + n->type;
        if (n->hmi)
            return tip + "\nUne variable du projet IHM (Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables) : elle se renomme l\xC3\xA0-bas.";
        // 1.11.2 (API-V) : en lecture seule, le cadenas dit pourquoi (une constante, une entree %I,
        // une ligne « lecture seule » de la table, la liaison en lecture seule : la raison du modele),
        // en une ligne avec ce qu'il disait deja (un nom de l'automate ne se renomme pas ici).
        if (n->access == A::ReadOnly) {
            std::string why = accessSentence(*n);
            if (!why.empty() && why.back() == '.') why.pop_back();
            // 1.11.2 : le remede tutoie, comme les raisons du plan (API-M, ef5dd88 : « donne-lui... »).
            tip += "\nLe cadenas : en lecture seule (" + why + ") ; et un nom de l'automate ne se renomme pas ici : "
                   "renomme-le dans Control Expert, puis Fichier \xE2\x80\xBA Importer.";
        } else {
            if (n->access != A::None) tip += "\n" + std::string(apiAccessLabel(n->access)) + " : " + accessSentence(*n);
            tip += "\nUn nom de l'automate (le cadenas) : il ne se renomme pas ici ; renomme-le dans Control Expert, "
                   "puis Fichier \xE2\x80\xBA Importer.";
        }
        if (n->uses.empty() && l.uses > 0) tip += "\nEmploy\xC3\xA9" "e " + std::to_string(l.uses) + " fois (elle et ce qu'elle contient).";
        if (!n->uses.empty()) {
            tip += "\nEmploy\xC3\xA9" "e " + std::to_string(n->uses.size()) + " fois :";
            for (std::size_t i = 0; i < n->uses.size() && i < 8; ++i) tip += "\n  " + n->uses[i];
            if (n->uses.size() > 8) tip += "\n  ... et " + std::to_string(n->uses.size() - 8) + " autres";
            // R1111-10 : le geste dit (decision 125).
            tip += "\nUn clic sur \xC2\xAB Utilis\xC3\xA9" "e par l'IHM \xC2\xBB : la liste des endroits ; en choisir un y m\xC3\xA8ne "
                   "(aussi : clic droit \xE2\x80\xBA Emplois).";
        }
        return tip + "\nDouble-clic : ins\xC3\xA9rer " + n->path + ".";
    }
private:
    const HmiApiVarsView& v_;
};

// ------------------------------------------------------------------ la table -
// La table en arbre, plus deux gestes : un clic gauche sur une case est dit a
// la vue (« Utilisee par l'IHM » : ou) ; le clic droit met les entrees de la
// vue en tete du menu de la table (Copier, Coller... restent dessous).
class ApiVarsTable final : public ui::TableView {
public:
    ApiVarsTable(std::string id, HmiApiVarsView& v) : ui::TableView(std::move(id)), v_(v) {
        if (auto* m = contextMenu())
            menuLinks_ += m->itemChosen->connect([this](int action) {
                if (action <= kMenuCopy) v_.menuChosen(action);
            });
    }

protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const auto* d = std::get_if<ui::MouseDown>(&ev);
        const bool inside = d && bounds().contains(d->pos);
        const auto r = ui::TableView::onEvent(ev);
        if (!inside) return r;
        // La ligne de VUE sous la souris (-1 : les titres, ou sous les lignes).
        int at = -1;
        for (std::size_t i = 0; i < visibleRowCount() && at < 0; ++i) {
            gfx::Rect rr{};
            if (rowRect(i, rr) && d->pos.y >= rr.y && d->pos.y < rr.bottom()) at = static_cast<int>(i);
        }
        if (at < 0) return r;
        const auto vi = static_cast<std::size_t>(at);
        if (d->button == ui::MouseButton::Right) {
            auto* m = contextMenu();
            if (m && m->isOpen()) {
                auto mine = v_.menuItems(viewRow(vi));
                if (!mine.empty()) {
                    mine.push_back({{}, {}, {}, ui::Icon::None, true, true, -1});
                    for (const auto& it : m->items()) mine.push_back(it);
                    m->setItems(std::move(mine));
                }
            }
        } else if (d->button == ui::MouseButton::Left && d->clickCount == 1 && lastClickedColumn() >= 0) {
            gfx::Rect er{};
            if (!(expanderRect(vi, er) && er.contains(d->pos)))           // la fleche : deplier, rien d'autre
                v_.cellClicked(viewRow(vi), v_.logicalColumn(static_cast<std::size_t>(lastClickedColumn())));
        }
        return r;
    }

private:
    HmiApiVarsView& v_;
    core::ConnectionScope menuLinks_;
};

// -------------------------------------------------------------------- la vue -
HmiApiVarsView::~HmiApiVarsView() {
    auto& views = liveViews();
    views.erase(std::remove(views.begin(), views.end(), this), views.end());
}

HmiApiVarsView::HmiApiVarsView(std::string id) : ui::Widget(std::move(id)) {
    liveViews().push_back(this);
    const std::string base = this->id();
    auto filter = std::make_unique<ui::DropDown>(base + ".filter");
    filter->setItems({{"Toutes", "all"}, {"Employ\xC3\xA9" "es par l'IHM", "used"}, {"Accessibles en \xC3\xA9" "criture", "write"},
                      {"Sans adresse", "noaddr"}});
    filter->setSelectedIndex(0);
    filterBox_ = &static_cast<ui::DropDown&>(addChild(std::move(filter)));
    auto search = std::make_unique<ui::InputText>(base + ".search");
    search->setPlaceholder("Rechercher (nom, type, r\xC3\xA9" "f\xC3\xA9rence) dans tout l'arbre...");
    searchBox_ = &static_cast<ui::InputText&>(addChild(std::move(search)));
    auto table = std::make_unique<ApiVarsTable>(base + ".tree", *this);
    table->setColumns({{"Nom", 260.f, 60.f, true, false}, {"Type", 170.f, 40.f, true, false},
                       {"Port\xC3\xA9" "e", 170.f, 40.f, true, false}, {"Accessible", 250.f, 40.f, true, false},
                       {"Utilis\xC3\xA9" "e par l'IHM", 230.f, 40.f, true, false},
                       {"R\xC3\xA9" "f\xC3\xA9rence", 100.f, 40.f, true, false},
                       {"Taille", 80.f, 40.f, true, false, true, ui::Align::End}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table->setRowDragEnabled(true);          // une variable se glisse dans un script (routeDrag)
    table->setTooltip("Les variables de l'automate, sous API. : la fl\xC3\xA8" "che (ou un double-clic) d\xC3\xA9plie une ligne ; "
                      "un double-clic sur une variable ins\xC3\xA8re son nom complet ; elle se glisse dans un script.");
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    // La liste des emplois (un clic sur « Utilisee par l'IHM ») : choisir y mene.
    usesMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".uses")));
    links_ += usesMenu_->itemChosen->connect([this](int use) {
        if (use >= 0 && !usesPath_.empty()) useChosen->emit(usesPath_, static_cast<std::size_t>(use));
    });

    links_ += filterBox_->selectionChanged->connect([this](int) {
        const auto* item = filterBox_->selectedItem();
        const std::string v = item ? item->value : std::string("all");
        setFilter(v == "used" ? Filter::Used : v == "write" ? Filter::Writable : v == "noaddr" ? Filter::NoAddress : Filter::All);
    });
    links_ += searchBox_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    links_ += table_->expanderClicked->connect([this](ui::RowIndex row) { toggleLine(row); });
    // R1111-6 : une variable choisie (un clic, les fleches) : `picked`.
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (const auto* n = selectedNode()) picked->emit(*n);
    });
    links_ += table_->activated->connect([this](ui::RowIndex row) {
        if (row >= lines_.size()) return;
        const auto& l = lines_[row];
        if (!l.node || folderKind(l.node->kind) || l.node->kind == K::More) {
            toggleLine(row);
            return;
        }
        insertName->emit(l.node->path);
    });
    rebuild();
}

void HmiApiVarsView::setNodes(std::vector<ApiVarNode> roots) {
    roots_ = std::move(roots);
    fetched_.clear();
    usesUnder_.clear();
    leavesUnder_.clear();
    // La premiere fois : les Globales et les unites deplies (leurs portees aussi).
    if (!seeded_ && !roots_.empty()) {
        seeded_ = true;
        for (const auto& r : roots_) {
            const auto key = keyOf(r, {});
            if (folderKind(r.kind)) open_.insert(key);
            if (r.kind == K::Unit)                       // ses portees (demandees au modele s'il le faut)
                for (const auto& c : kids(r, key))
                    if (c.kind == K::Scope) open_.insert(keyOf(c, key));
        }
    }
    // Les emplois et les variables sous chaque ligne (la pastille d'un dossier) :
    // les dossiers sont lus en entier (leurs enfants demandes au modele : les
    // variables declarees), une instance pas encore depliee compte pour une.
    const std::function<std::size_t(const ApiVarNode&, const std::string&)> tally = [&](const ApiVarNode& n, const std::string& key) {
        const auto& ks = folderKind(n.kind) ? kids(n, key) : n.children;
        std::size_t u = n.uses.size(), l = ks.empty() && !folderKind(n.kind) ? 1 : 0;
        for (const auto& c : ks) {
            u += tally(c, keyOf(c, key));
            l += leavesUnder_[&c];
        }
        usesUnder_[&n] = u;
        leavesUnder_[&n] = l;
        return u;
    };
    for (const auto& r : roots_) (void)tally(r, keyOf(r, {}));
    // Le filtre dit combien il garde de variables DECLAREES (les globales et
    // celles des unites ; une instance compte pour une) ; Accessibles en
    // ecriture compte aussi les membres que la table des adresses nomme.
    std::size_t used = 0, noaddr = 0;
    std::set<std::string> writable;
    const std::function<void(const ApiVarNode&, const std::string&)> count = [&](const ApiVarNode& n, const std::string& key) {
        if (folderKind(n.kind)) {
            for (const auto& c : kids(n, key)) count(c, keyOf(c, key));
            return;
        }
        if (n.kind == K::More) return;
        used += !n.uses.empty() || n.useCount > 0 || usesUnder_[&n] > 0;
        if (n.access == A::ReadWrite) writable.insert(n.path);
        noaddr += n.access == A::NoAddress;
    };
    for (const auto& r : roots_) count(r, keyOf(r, {}));
    if (filterSource_)
        for (auto& p : filterSource_(Filter::Writable)) writable.insert(std::move(p));
    const int sel = filterBox_->selectedIndex();
    filterBox_->setItems({{"Toutes", "all"},
                          {"Employ\xC3\xA9" "es par l'IHM (" + std::to_string(used) + ")", "used"},
                          {"Accessibles en \xC3\xA9" "criture (" + std::to_string(writable.size()) + ")", "write"},
                          {"Sans adresse (" + std::to_string(noaddr) + ")", "noaddr"}});
    filterBox_->setSelectedIndex(sel < 0 ? 0 : sel);
    // R1111-4 : une recherche ou un filtre en cours survit a un changement du
    // projet (setNodes a oublie les enfants deja demandes) : ce qu'ils trouvent
    // sous des noeuds jamais deplies est redemande au modele, comme a la frappe.
    if (searchSource_ && !search_.empty()) prefetchFor(searchSource_(search_));
    if (filterSource_ && filter_ != Filter::All) prefetchFor(filterSource_(filter_));
    rebuild();
}

void HmiApiVarsView::setFilter(Filter f) {
    if (f == filter_) return;
    filter_ = f;
    closedNarrow_.clear();
    const int index = static_cast<int>(f);
    if (filterBox_->selectedIndex() != index) filterBox_->setSelectedIndex(index);
    // Ce qui passe le filtre sous des noeuds pas encore deplies : leurs ancetres
    // sont demandes au modele (comme pour la recherche).
    if (filterSource_ && f != Filter::All) prefetchFor(filterSource_(f));
    rebuild();
    table_->setScrollOffset(0.f);
}

void HmiApiVarsView::setSearch(std::string text) {
    if (text == search_) return;
    search_ = std::move(text);
    closedNarrow_.clear();
    if (searchBox_->text() != search_) searchBox_->setText(search_);
    if (searchSource_ && !search_.empty()) prefetchFor(searchSource_(search_));
    rebuild();
    table_->setScrollOffset(0.f);
}

// Les ancetres des chemins trouves sont demandes au modele : la recherche les
// voit alors comme le reste. Un dossier se parcourt toujours ; une variable,
// si un chemin trouve est en dessous d'elle (son chemin, puis "." ou "[").
void HmiApiVarsView::prefetchFor(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    const auto below = [&](const std::string& p) {
        return std::any_of(paths.begin(), paths.end(), [&](const std::string& m) {
            return m.size() > p.size() && m.compare(0, p.size(), p) == 0 && (m[p.size()] == '.' || m[p.size()] == '[');
        });
    };
    const std::function<void(const ApiVarNode&, const std::string&)> visit = [&](const ApiVarNode& n, const std::string& parentKey) {
        if (n.kind == K::More) return;                 // le modele ne cherche que dans les premiers elements
        if (!folderKind(n.kind) && (n.path.empty() || !below(n.path))) return;
        const std::string key = keyOf(n, parentKey);
        for (const auto& c : kids(n, key)) visit(c, key);
    };
    for (const auto& r : roots_) visit(r, {});
}

std::string HmiApiVarsView::keyOf(const ApiVarNode& n, const std::string& parentKey) {
    if (!n.key.empty()) return n.key;
    if (!n.path.empty()) return n.path;
    return parentKey.empty() ? "#" + n.name : parentKey + "/" + n.name;
}

const std::vector<ApiVarNode>& HmiApiVarsView::kids(const ApiVarNode& n, const std::string& key) const {
    if (!n.lazy) return n.children;
    if (const auto it = fetched_.find(key); it != fetched_.end()) return it->second;
    if (!fetch_) return n.children;
    return fetched_.emplace(key, fetch_(n)).first->second;
}

// Les enfants deja connus (sans rien demander au modele) : le filtre et la
// recherche ne fabriquent pas tout l'arbre.
namespace {
const std::vector<ApiVarNode>& knownKids(const ApiVarNode& n, const std::map<std::string, std::vector<ApiVarNode>>& fetched,
                                         const std::string& key) {
    if (!n.lazy) return n.children;
    const auto it = fetched.find(key);
    return it != fetched.end() ? it->second : n.children;
}
} // namespace

bool HmiApiVarsView::selfMatches(const ApiVarNode& n) const {
    if (search_.empty()) return true;
    const ui::SearchQuery q(search_);
    return q.matches({std::string_view(n.name), std::string_view(n.path), std::string_view(n.type), std::string_view(n.reference)});
}

bool HmiApiVarsView::keepFilter(const ApiVarNode& n) const {
    switch (filter_) {
        case Filter::All: return true;
        case Filter::Used: if (!n.uses.empty() || n.useCount > 0) return true; break;
        case Filter::Writable: if (n.access == A::ReadWrite) return true; break;
        case Filter::NoAddress: if (n.access == A::NoAddress) return true; break;
    }
    const std::string key = keyOf(n, {});
    const auto& ks = knownKids(n, fetched_, key);
    return std::any_of(ks.begin(), ks.end(), [this](const ApiVarNode& c) { return keepFilter(c); });
}

bool HmiApiVarsView::keepSearch(const ApiVarNode& n) const {
    if (search_.empty() || selfMatches(n)) return true;
    const std::string key = keyOf(n, {});
    const auto& ks = knownKids(n, fetched_, key);
    return std::any_of(ks.begin(), ks.end(), [this](const ApiVarNode& c) { return keepSearch(c); });
}

void HmiApiVarsView::walk(const ApiVarNode& n, const std::string& parentKey, int depth, bool underMatch) {
    if (!keepFilter(n)) return;
    const bool self = !search_.empty() && selfMatches(n);
    if (!underMatch && !keepSearch(n)) return;
    Line l;
    l.node = &n;
    l.key = keyOf(n, parentKey);
    l.depth = depth;
    l.expandable = !n.children.empty() || n.lazy;
    if (n.useCount > 0) l.uses = n.useCount;
    else if (const auto it = usesUnder_.find(&n); it != usesUnder_.end()) l.uses = it->second;
    // Deplie : a la main ; pendant un filtre ou une recherche, ce qui mene a un
    // resultat l'est d'office (sauf ce qu'on y replie). Une ligne trouvee par la
    // recherche reste repliee : son contenu entier y est.
    const bool autoOpen = narrowing() && (filter_ != Filter::All || (!self && !underMatch));
    l.open = l.expandable && (autoOpen ? closedNarrow_.count(l.key) == 0 : open_.count(l.key) > 0);
    const std::string key = l.key;
    const bool open = l.open;
    lines_.push_back(std::move(l));
    if (!open) return;
    const bool under = underMatch || self;
    const auto& ks = kids(n, key);
    // Un tableau tout fabrique (les donnees factices) se coupe ici ; celui du
    // modele d'API-M arrive deja coupe (son noeud More).
    const bool cut = n.kind == K::Array && !narrowing() && ks.size() > kFirstElements && allShown_.count(key) == 0;
    const std::size_t shown = cut ? kFirstElements : ks.size();
    for (std::size_t i = 0; i < shown; ++i) walk(ks[i], key, depth + 1, under);
    if (cut) {
        Line more;
        more.key = key + "#more";
        more.depth = depth + 1;
        more.expandable = true;
        more.more = ks.size() - kFirstElements;
        lines_.push_back(std::move(more));
    }
}

void HmiApiVarsView::rebuild() {
    // Ce qui est choisi et le defilement restent.
    std::string keep;
    if (const auto sel = table_->selectedModelRows(); !sel.empty() && sel.front() < lines_.size()) keep = lines_[sel.front()].key;
    const float scroll = table_->scrollOffset();
    lines_.clear();
    for (const auto& r : roots_) walk(r, {}, 0, false);
    // Les variables gardees, depliees ou non.
    matches_ = 0;
    const std::function<void(const ApiVarNode&, bool)> count = [&](const ApiVarNode& n, bool under) {
        if (!keepFilter(n)) return;
        const bool self = !search_.empty() && selfMatches(n);
        if (!under && !keepSearch(n)) return;
        if (n.kind == K::More) return;
        const std::string key = keyOf(n, {});
    const auto& ks = knownKids(n, fetched_, key);
        if (ks.empty() && !folderKind(n.kind)) {
            ++matches_;
            return;
        }
        for (const auto& c : ks) count(c, under || self);
    };
    for (const auto& r : roots_) count(r, false);
    model_ = std::make_shared<ApiVarsRows>(*this);
    table_->setModel(model_);
    table_->setHighlight(search_);
    table_->setScrollOffset(scroll);
    if (!keep.empty())
        if (const int at = findLine(keep); at >= 0) table_->selectModelRows({static_cast<ui::RowIndex>(at)}, false);
    invalidate();
}

void HmiApiVarsView::setOpen(const std::string& key, bool open) {
    if (key.size() > 5 && key.compare(key.size() - 5, 5, "#more") == 0) {
        // « ... et N autres » : le tableau entier.
        const auto array = key.substr(0, key.size() - 5);
        if (open) allShown_.insert(array);
        else allShown_.erase(array);
        rebuild();
        return;
    }
    if (open) {
        open_.insert(key);
        closedNarrow_.erase(key);
    } else {
        open_.erase(key);
        if (narrowing()) closedNarrow_.insert(key);
    }
    rebuild();
}

bool HmiApiVarsView::isOpen(const std::string& key) const {
    const int at = findLine(key);
    return at >= 0 && lines_[static_cast<std::size_t>(at)].open;
}

void HmiApiVarsView::toggleLine(std::size_t line) {
    if (line >= lines_.size() || !lines_[line].expandable) return;
    setOpen(lines_[line].key, !lines_[line].open);
}

std::string HmiApiVarsView::lineText(std::size_t line, std::size_t c) const {
    if (line >= lines_.size()) return {};
    const auto& l = lines_[line];
    const auto* n = l.node;
    if (!n) return c == Name ? "\xE2\x80\xA6 et " + std::to_string(l.more) + " autres" : std::string{};
    if (n->kind == K::More) return c == Name ? n->name : std::string{};
    if (folderKind(n->kind)) {
        switch (c) {
            case Name: return n->name;
            case Type: return n->type;
            case Usage: return l.uses ? std::to_string(l.uses) : std::string{};
            default: return {};
        }
    }
    switch (c) {
        case Name: return n->name;
        case Type: return n->type;
        case Scope: return n->scope;
        case Access: return apiAccessLabel(n->access);
        case Usage:
            if (!l.uses) return "-";
            if (n->uses.empty()) return std::to_string(l.uses);             // ceux de ses membres
            return std::to_string(l.uses) + kDot + n->uses.front()
                 + (n->uses.size() > 1 ? "  (+" + std::to_string(n->uses.size() - 1) + ")" : std::string{});
        case Reference: return n->reference.empty() ? std::string("-") : n->reference;
        case Size: return bytesText(n->bytes);
        default: return {};
    }
}

std::string HmiApiVarsView::lineKey(std::size_t line) const { return line < lines_.size() ? lines_[line].key : std::string{}; }
int HmiApiVarsView::lineDepth(std::size_t line) const { return line < lines_.size() ? lines_[line].depth : -1; }

int HmiApiVarsView::findLine(const std::string& key) const {
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].key == key) return static_cast<int>(i);
    return -1;
}

std::string HmiApiVarsView::selectedName() const {
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= lines_.size()) return {};
    const auto* n = lines_[sel.front()].node;
    return n && !folderKind(n->kind) ? n->path : std::string{};
}

bool HmiApiVarsView::selectName(const std::string& path) {
    // Les dossiers au-dessus se deplient : on cherche le chemin des cles. Les
    // enfants du modele sont demandes en chemin (R1111-5 : un nom sous des
    // noeuds jamais deplies), seulement la ou le nom peut etre : un dossier, ou
    // une variable dont il prolonge le chemin (par « . » ou « [ »).
    if (path.empty()) return false;
    std::vector<std::string> chain;
    const auto above = [&](const std::string& p) {
        return path.size() > p.size() && path.compare(0, p.size(), p) == 0 && (path[p.size()] == '.' || path[p.size()] == '[');
    };
    const std::function<bool(const ApiVarNode&, const std::string&)> find = [&](const ApiVarNode& n, const std::string& parent) {
        const auto key = keyOf(n, parent);
        if (n.path == path) return true;
        if (n.kind == K::More || (!folderKind(n.kind) && (n.path.empty() || !above(n.path)))) return false;
        for (const auto& c : kids(n, key))
            if (find(c, key)) {
                chain.push_back(key);
                return true;
            }
        return false;
    };
    bool found = false;
    for (const auto& r : roots_)
        if (find(r, {})) {
            found = true;
            break;
        }
    if (!found) return false;
    for (const auto& k : chain) {
        open_.insert(k);
        closedNarrow_.erase(k);
        allShown_.insert(k);         // un element loin dans un tableau
    }
    rebuild();
    const int at = findLine(path);
    if (at < 0) return false;
    table_->selectModelRows({static_cast<ui::RowIndex>(at)});
    return true;
}

std::string HmiApiVarsView::copyName() {
    const auto name = selectedName();
    if (!name.empty()) ui::setClipboardText(name);
    return name;
}

// ------------------------------------------------- les emplois, le menu -----
void HmiApiVarsView::cellClicked(std::size_t line, std::size_t column) {
    if (column == Usage) (void)showUses(line);
}

bool HmiApiVarsView::showUses(std::size_t line) {
    if (line >= lines_.size()) return false;
    const auto* n = lines_[line].node;
    if (!n || folderKind(n->kind) || n->kind == K::More || n->uses.empty()) return false;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.label = n->path + " : employ\xC3\xA9" "e " + std::to_string(n->uses.size()) + " fois";
    head.heading = true;
    items.push_back(std::move(head));
    for (std::size_t i = 0; i < n->uses.size() && i < kMaxUsesListed; ++i)
        items.push_back({n->uses[i], {}, {}, ui::Icon::None, true, false, static_cast<int>(i)});
    if (n->uses.size() > kMaxUsesListed) {
        ui::PopupMenu::Item more;
        more.label = "\xE2\x80\xA6 et " + std::to_string(n->uses.size() - kMaxUsesListed) + " autres (Compiler les montre tous)";
        more.heading = true;
        items.push_back(std::move(more));
    }
    usesPath_ = n->path;
    usesMenu_->setItems(std::move(items));
    // Sous la case « Utilisee par l'IHM » de la ligne (sinon sous la souris).
    gfx::Point at{bounds().x + 40.f, bounds().y + 40.f};
    for (std::size_t i = 0; i < table_->visibleRowCount(); ++i) {
        gfx::Rect cell{};
        if (table_->viewRow(i) == line && table_->cellRect(i, Usage, cell)) at = {cell.x, cell.bottom()};
    }
    const auto& root = rootOf(*this);
    usesMenu_->openAt(at, {root.bounds().right(), root.bounds().bottom()});
    return true;
}

std::vector<ui::PopupMenu::Item> HmiApiVarsView::menuItems(std::size_t line) {
    std::vector<ui::PopupMenu::Item> items;
    if (line >= lines_.size()) return items;
    const auto* n = lines_[line].node;
    if (!n || folderKind(n->kind) || n->kind == K::More || n->path.empty()) return items;
    menuLine_ = line;
    items.push_back({"Copier le nom " + n->path, {}, {}, ui::Icon::None, true, false, kMenuCopy});
    const bool script = shownScriptPane(rootOf(*this), nullptr) != nullptr;
    items.push_back({"Ins\xC3\xA9rer dans le script montr\xC3\xA9", {},
                     script ? std::string{} : std::string("aucun script \xC3\xA0 l'\xC3\xA9" "cran : glisse-la dans un script, ou copie son nom"),
                     ui::Icon::None, script, false, kMenuInsert});
    if (!n->uses.empty()) {
        ui::PopupMenu::Item uses;
        uses.label = "Emplois (" + std::to_string(n->uses.size()) + ")";
        uses.id = kMenuUse + 1;      // une entree a sous-menu ne se choisit pas elle-meme
        for (std::size_t i = 0; i < n->uses.size() && i < kMaxUsesListed; ++i)
            uses.children.push_back({n->uses[i], {}, {}, ui::Icon::None, true, false, kMenuUse - static_cast<int>(i)});
        items.push_back(std::move(uses));
    } else {
        items.push_back({"Emplois", {}, "l'IHM ne l'emploie pas", ui::Icon::None, false, false, kMenuUse + 1});
    }
    return items;
}

void HmiApiVarsView::menuChosen(int id) {
    if (menuLine_ >= lines_.size()) return;
    const auto* n = lines_[menuLine_].node;
    if (!n || n->path.empty()) return;
    const std::string path = n->path;
    if (id == kMenuCopy) {
        ui::setClipboardText(path);
    } else if (id == kMenuInsert) {
        (void)insertInShownScript(rootOf(*this), path);
    } else if (id <= kMenuUse) {
        const auto k = static_cast<std::size_t>(kMenuUse - id);
        if (k < n->uses.size()) useChosen->emit(path, k);
    }
}

// ------------------------------------------------ glisser dans un script ----
std::string HmiApiVarsView::draggedName() const {
    if (!table_->rowDragging()) return {};
    const auto& rows = table_->draggedRows();
    if (rows.empty() || rows.front() >= lines_.size()) return {};
    const auto* n = lines_[rows.front()].node;
    return n && !folderKind(n->kind) && n->kind != K::More ? n->path : std::string{};
}

bool HmiApiVarsView::insertInShownScript(ui::Widget& root, const std::string& name) {
    auto* pane = shownScriptPane(root, nullptr);
    if (!pane || name.empty()) return false;
    auto& ed = pane->editor();
    ed.insertText(name);
    ed.dismissCompletion();
    ed.takeFocus();
    return true;
}

bool HmiApiVarsView::routeDrag(const ui::InputEvent& ev) {
    const auto* up = std::get_if<ui::MouseUp>(&ev);
    if (!up) return false;
    for (auto* v : liveViews()) {
        if (!v->table_->rowDragging()) continue;
        const auto tb = v->table_->bounds();
        if (tb.contains(up->pos)) return false;            // lachee dans la table : elle s'en occupe
        const auto name = v->draggedName();
        // La table au repos : un lacher chez elle (elle n'accepte aucun depot).
        v->table_->dispatch(ui::MouseUp{{tb.x + 1.f, tb.y + 1.f}, up->button, up->mods});
        if (name.empty()) return false;
        auto* pane = shownScriptPane(rootOf(*v), &up->pos);
        if (!pane) return false;
        // Le curseur ou la variable est lachee, puis le nom.
        auto& ed = pane->editor();
        ed.dispatch(ui::MouseDown{up->pos, ui::MouseButton::Left, 1, {}});
        ed.dispatch(ui::MouseUp{up->pos, ui::MouseButton::Left, {}});
        ed.insertText(name);
        ed.dismissCompletion();
        ed.takeFocus();
        return true;
    }
    return false;
}

void HmiApiVarsView::onLayout() {
    const auto b = bounds();
    const float row = 30.f;
    // R1111-10 : la liste des emplois a les bornes de la vue, comme le menu de la
    // table (TableView::onLayout) : un widget sans bornes n'est ni peint ni vise -
    // elle s'ouvrait sans se voir, et un clic sur « Utilisee par l'IHM » ne
    // montrait rien. Ouverte, elle peint par-dessus tout (la passe du dessus).
    usesMenu_->setBounds(b);
    if (compact_) {                 // R1111-6 : la bibliotheque, etroite : le filtre, puis la recherche
        filterBox_->setBounds({b.x, b.y, b.w, row});
        searchBox_->setBounds({b.x, b.y + row + 4.f, b.w, row});
        table_->setBounds({b.x, b.y + 2.f * (row + 4.f), b.w, std::max(0.f, b.h - 2.f * (row + 4.f))});
        return;
    }
    filterBox_->setBounds({b.x, b.y, std::min(230.f, b.w * 0.4f), row});
    searchBox_->setBounds({b.x + std::min(230.f, b.w * 0.4f) + 6.f, b.y, std::max(0.f, b.w - std::min(230.f, b.w * 0.4f) - 6.f), row});
    table_->setBounds({b.x, b.y + row + 4.f, b.w, std::max(0.f, b.h - row - 4.f)});
}

// ------------------------------------------------------- donnees factices ----
std::vector<ApiVarNode> apiVarsSample() {
    const auto leaf = [](std::string name, std::string path, std::string type, std::string scope, A access,
                         std::string ref, double bytes, std::vector<std::string> uses = {}) {
        ApiVarNode n;
        n.kind = K::Variable;
        n.name = std::move(name);
        n.path = std::move(path);
        n.type = std::move(type);
        n.scope = std::move(scope);
        n.access = access;
        n.reference = std::move(ref);
        n.bytes = bytes;
        n.uses = std::move(uses);
        return n;
    };
    std::vector<ApiVarNode> out;
    ApiVarNode globals;
    globals.kind = K::Group;
    globals.name = "Globales";
    {
        // Armoires : ARRAY[0..1] OF T_Armoire - ana.PT1.mes, etat.
        ApiVarNode arr;
        arr.kind = K::Array;
        arr.name = "Armoires";
        arr.path = "API.Armoires";
        arr.type = "ARRAY[0..1] OF T_Armoire";
        arr.scope = "Globale";
        arr.bytes = 632;
        for (int i = 0; i < 2; ++i) {
            const std::string p = "API.Armoires[" + std::to_string(i) + "]";
            ApiVarNode el;
            el.kind = K::Structure;
            el.name = "[" + std::to_string(i) + "]";
            el.path = p;
            el.type = "T_Armoire";
            el.scope = "Globale";
            el.bytes = 316;
            ApiVarNode ana;
            ana.kind = K::Structure;
            ana.name = "ana";
            ana.path = p + ".ana";
            ana.type = "T_Ana";
            ana.scope = "Globale";
            ApiVarNode pt1;
            pt1.kind = K::Structure;
            pt1.name = "PT1";
            pt1.path = p + ".ana.PT1";
            pt1.type = "T_Mesure";
            pt1.scope = "Globale";
            pt1.children.push_back(leaf("mes", p + ".ana.PT1.mes", "REAL", "Globale", A::ReadOnly, "%MF" + std::to_string(100 + i * 10), 4,
                                        i == 0 ? std::vector<std::string>{"Vue_Accueil/Afficheur_PT1.value", "alarme PT1_Haut"}
                                               : std::vector<std::string>{}));
            pt1.children.push_back(leaf("seuil_haut", p + ".ana.PT1.seuil_haut", "REAL", "Globale", A::ReadWrite,
                                        "%MF" + std::to_string(102 + i * 10), 4));
            ana.children.push_back(std::move(pt1));
            el.children.push_back(std::move(ana));
            el.children.push_back(leaf("etat", p + ".etat", "INT", "Globale", A::ReadOnly, "%MW" + std::to_string(40 + i), 2));
            arr.children.push_back(std::move(el));
        }
        globals.children.push_back(std::move(arr));
    }
    {
        // V : ARRAY[0..63] OF T_Vanne - les premiers elements, puis « ... et 56 autres ».
        ApiVarNode arr;
        arr.kind = K::Array;
        arr.name = "V";
        arr.path = "API.V";
        arr.type = "ARRAY[0..63] OF T_Vanne";
        arr.scope = "Globale";
        arr.reference = "%MW1000";
        arr.bytes = 64 * 4;
        for (int i = 0; i < 64; ++i) {
            const std::string p = "API.V[" + std::to_string(i) + "]";
            ApiVarNode el;
            el.kind = K::Structure;
            el.name = "[" + std::to_string(i) + "]";
            el.path = p;
            el.type = "T_Vanne";
            el.scope = "Globale";
            el.bytes = 4;
            el.children.push_back(leaf("Ouv", p + ".Ouv", "BOOL", "Globale", A::ReadWrite, "%MW" + std::to_string(1000 + i * 2) + ".0", 0.125,
                                       i == 0 ? std::vector<std::string>{"Vue_Accueil/Valve_1.open"} : std::vector<std::string>{}));
            el.children.push_back(leaf("Pos", p + ".Pos", "INT", "Globale", A::ReadOnly, "%MW" + std::to_string(1001 + i * 2), 2));
            arr.children.push_back(std::move(el));
        }
        globals.children.push_back(std::move(arr));
    }
    globals.children.push_back(leaf("Marche_Ligne", "API.Marche_Ligne", "BOOL", "Globale", A::ReadWrite, "%M10", 0.125,
                                    {"Vue_Synoptique/V_101.moving", "Vue_Synoptique/Bouton_Marche.action", "script Initialisation"}));
    globals.children.push_back(leaf("Input_0", "API.Input_0", "BOOL", "Globale", A::ReadOnly, "%I0.1.0", 0.125));
    globals.children.push_back(leaf("gN", "API.gN", "BOOL", "Globale", A::NoAddress, "", 0.125));
    out.push_back(std::move(globals));

    // Une unite de programme : ses portees.
    ApiVarNode unit;
    unit.kind = K::Unit;
    unit.name = "Gestion_armoires";
    unit.path = "API.Gestion_armoires";
    unit.type = "unit\xC3\xA9 de programme";
    const auto scope = [](std::string name) {
        ApiVarNode s;
        s.kind = K::Scope;
        s.name = std::move(name);
        return s;
    };
    auto pub = scope("Publiques");
    pub.children.push_back(leaf("Etat_Armoire", "API.Gestion_armoires.Etat_Armoire", "INT", "Publique \xC2\xB7 Gestion_armoires",
                                A::ReadOnly, "%MW60", 2, {"Vue_Accueil/Voyant_Etat.value"}));
    pub.children.push_back(leaf("reset", "API.Gestion_armoires.reset", "BOOL", "Publique \xC2\xB7 Gestion_armoires", A::ReadWrite, "%M60", 0.125));
    auto priv = scope("Priv\xC3\xA9" "es");
    priv.children.push_back(leaf("tempo_defaut", "API.Gestion_armoires.tempo_defaut", "TIME", "Priv\xC3\xA9" "e \xC2\xB7 Gestion_armoires",
                                 A::NoAddress, "", 4));
    {
        ApiVarNode pid;
        pid.kind = K::Structure;
        pid.dfb = true;
        pid.name = "Regul";
        pid.path = "API.Gestion_armoires.Regul";
        pid.type = "DFB_Regul";
        pid.scope = "Priv\xC3\xA9" "e \xC2\xB7 Gestion_armoires";
        ApiVarNode pid1;
        pid1.kind = K::Structure;
        pid1.dfb = true;
        pid1.name = "PID1";
        pid1.path = "API.Gestion_armoires.Regul.PID1";
        pid1.type = "PID";
        pid1.scope = pid.scope;
        pid1.children.push_back(leaf("SP", "API.Gestion_armoires.Regul.PID1.SP", "REAL", pid.scope, A::NoAddress, "", 4));
        pid.children.push_back(std::move(pid1));
        priv.children.push_back(std::move(pid));
    }
    auto io = scope("E/S");
    io.children.push_back(leaf("Entree_Marche", "API.Gestion_armoires.Entree_Marche", "BOOL", "Entr\xC3\xA9" "e \xC2\xB7 Gestion_armoires",
                               A::ReadOnly, "%I0.2.0", 0.125));
    unit.children.push_back(std::move(pub));
    unit.children.push_back(std::move(priv));
    unit.children.push_back(std::move(io));
    out.push_back(std::move(unit));
    return out;
}

// ------------------------------------------- le branchement sur API-M -----
ApiVarNode apiVarNodeOf(const hmi::apivars::Node& n) {
    namespace av = hmi::apivars;
    ApiVarNode v;
    v.key = n.key;
    v.name = n.name;
    v.path = n.path;
    v.type = n.type;
    v.scope = n.scopeText;
    v.reference = n.reference;
    v.accessWhy = n.accessWhy;
    v.bytes = n.bits ? static_cast<double>(n.bits) / 8.0 : -1.0;
    v.useCount = n.uses;
    v.lazy = n.children;
    switch (n.kind) {
        case av::NodeKind::Group:
            v.kind = n.group == av::GroupKind::Unit ? K::Unit : n.group == av::GroupKind::Globals ? K::Group : K::Scope;
            if (n.group == av::GroupKind::Unit && v.type.empty()) v.type = "unit\xC3\xA9 de programme";
            break;
        case av::NodeKind::More:
            v.kind = K::More;
            break;
        case av::NodeKind::Variable:
        case av::NodeKind::Member:
        case av::NodeKind::Element:
            v.kind = !n.children ? K::Variable : (n.type.rfind("ARRAY", 0) == 0 ? K::Array : K::Structure);
            // Une constante se lit, ne s'ecrit pas (Node::writable).
            v.access = n.writable() ? A::ReadWrite
                     : n.access == av::Access::NoAddress ? A::NoAddress : A::ReadOnly;
            // 1.11.2 (API-V) : une constante dit pourquoi l'IHM ne l'ecrit pas - la raison de
            // Compiler (« est une constante de l'automate », HmiScriptCheck) ; sans adresse, la
            // raison du plan suit. Avant : la seule raison du plan (« localisee en %MW... »,
            // ou « donne-lui une adresse »), qui ne disait pas pourquoi elle ne s'ecrit pas.
            if (n.constant)
                v.accessWhy = std::string(kConstantWhy)
                            + (n.access == av::Access::NoAddress && !n.accessWhy.empty() ? " ; " + n.accessWhy : std::string{});
            break;
    }
    return v;
}

} // namespace app

namespace app {

// ---- 1.11.1 (R1111-6) : la bibliotheque de l'editeur de vue --------------------------
void HmiApiVarsView::setCompact(bool compact) {
    if (compact == compact_) return;
    compact_ = compact;
    if (compact_) {
        // L'ordre montre (logicalColumn) : Nom, Accessible, Type, Portee... -
        // dans la colonne etroite, Accessible se voit a cote du nom.
        table_->setColumns({{"Nom", 190.f, 60.f, true, false}, {"Accessible", 150.f, 40.f, true, false},
                            {"Type", 110.f, 40.f, true, false}, {"Port\xC3\xA9" "e", 110.f, 40.f, true, false},
                            {"Utilis\xC3\xA9" "e par l'IHM", 150.f, 40.f, true, false},
                            {"R\xC3\xA9" "f\xC3\xA9rence", 90.f, 40.f, true, false},
                            {"Taille", 60.f, 40.f, true, false, true, ui::Align::End}});
        searchBox_->setPlaceholder("Rechercher (nom, type, r\xC3\xA9" "f\xC3\xA9rence)...");
        table_->setRowDragEnabled(false);    // la vue recoit le lacher (comme la liste d'avant)
        table_->setTooltip("Les variables de l'automate (API.\xE2\x80\xA6), puis celles de l'IHM : un clic choisit une variable, "
                           "puis un clic dans la vue la pose (ou glisse-la dans la vue) ; la fl\xC3\xA8" "che d\xC3\xA9plie une ligne.");
    }
    rebuild();
    invalidateLayout();
}

const ApiVarNode* HmiApiVarsView::selectedNode() const {
    const auto sel = table_->selectedModelRows();
    if (sel.empty() || sel.front() >= lines_.size()) return nullptr;
    const auto* n = lines_[sel.front()].node;
    return n && !folderKind(n->kind) && n->kind != K::More ? n : nullptr;
}

bool HmiApiVarsView::nameRect(const std::string& path, gfx::Rect& out) const {
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].node && lines_[i].node->path == path) return table_->rowRect(i, out);
    return false;
}

std::size_t bindApiModel(HmiApiVarsView& view, std::shared_ptr<const hmi::apivars::Model> model, const hmi::Project& project,
                         std::vector<ApiVarNode> extra) {
    if (!model) {
        view.setFetch({});
        view.setSearchSource({});
        view.setFilterSource({});
        view.setNodes(std::move(extra));
        return 0;
    }
    // La table cle -> noeud du modele (les enfants se demandent par elle).
    auto table = std::make_shared<std::map<std::string, hmi::apivars::Node>>();
    const auto adopt = [table, model](const hmi::apivars::Node& n) {
        (*table)[n.key] = n;
        auto v = apiVarNodeOf(n);
        if (n.uses > 0 && n.real())
            for (const auto& u : model->usesOf(n.path)) v.uses.push_back(u.where);
        return v;
    };
    std::vector<ApiVarNode> roots;
    std::size_t total = 0;
    for (const auto& n : model->roots()) {
        if (n.kind == hmi::apivars::NodeKind::Group && n.group == hmi::apivars::GroupKind::Unit) {
            for (const auto& scope : model->children(n)) total += scope.count;
        } else if (n.kind == hmi::apivars::NodeKind::Group) {
            total += n.count;
        }
        roots.push_back(adopt(n));
    }
    for (auto& e : extra) roots.push_back(std::move(e));
    view.setFetch([table, model, adopt](const ApiVarNode& v) {
        std::vector<ApiVarNode> out;
        const auto it = table->find(v.key);
        if (it == table->end()) return out;
        const hmi::apivars::Node parent = it->second;     // copie : adopt remplit la table
        for (const auto& c : model->children(parent)) out.push_back(adopt(c));
        return out;
    });
    view.setSearchSource([model](std::string_view text) { return model->search(text); });
    view.setFilterSource([model, rows = project.comm.addresses](HmiApiVarsView::Filter f) {
        std::vector<std::string> out;
        if (f != HmiApiVarsView::Filter::Writable) return out;
        for (const auto& a : rows) {
            hmi::apivars::Node n;
            if (!a.variable.empty() && model->node(a.variable, n) && n.writable()) out.push_back(n.path);
        }
        return out;
    });
    view.setNodes(std::move(roots));
    return total;
}

} // namespace app
