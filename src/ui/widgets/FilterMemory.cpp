// =============================================================================
//  ui/widgets/FilterMemory.cpp - lot API 8 : les filtres retenus d'une seance a
//  l'autre (voir FilterMemory.hpp).
// =============================================================================
#include "FilterMemory.hpp"

#include "Controls.hpp"
#include "TableFilters.hpp"

#include <algorithm>
#include <cstdint>

namespace ui {

namespace {

struct Entry {
    std::uint64_t        id{0};
    FilterMemory::Client client;
};

struct MemoryState {
    FilterMemory::Store store;
    std::vector<Entry>  clients;
    std::uint64_t       next{1};
};

MemoryState& memory() {
    static MemoryState s;
    return s;
}

bool isHex(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return c - 'a' + 10;
}

// Les morceaux entre deux `sep`, tels quels (un champ echappe ne contient pas `sep`).
std::vector<std::string> splitRaw(std::string_view text, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const auto at = text.find(sep, start);
        if (at == std::string_view::npos) {
            out.emplace_back(text.substr(start));
            return out;
        }
        out.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
}

// Les conditions, par un mot lisible (le fichier des reglages se lit a la main).
struct OpName { ColumnFilter::Op op; const char* name; };
constexpr OpName kOps[] = {
    {ColumnFilter::Op::Contains, "contient"}, {ColumnFilter::Op::StartsWith, "commence"},
    {ColumnFilter::Op::Equals, "egal"},       {ColumnFilter::Op::NotEquals, "different"},
    {ColumnFilter::Op::Empty, "vide"},        {ColumnFilter::Op::NotEmpty, "non-vide"},
    {ColumnFilter::Op::Between, "entre"}};

std::string opName(ColumnFilter::Op op) {
    for (const auto& o : kOps)
        if (o.op == op) return o.name;
    return {};
}

ColumnFilter::Op opFromName(std::string_view name) {
    for (const auto& o : kOps)
        if (name == o.name) return o.op;
    return ColumnFilter::Op::None;
}

std::string listName(ColumnFilter::List list) {
    switch (list) {
        case ColumnFilter::List::Only: return "seulement";
        case ColumnFilter::List::Except: return "sauf";
        default: return {};
    }
}

ColumnFilter::List listFromName(std::string_view name) {
    if (name == "seulement") return ColumnFilter::List::Only;
    if (name == "sauf") return ColumnFilter::List::Except;
    return ColumnFilter::List::None;
}

bool parseIndex(std::string_view s, std::size_t& out) {
    if (s.empty() || s.size() > 6) return false;
    std::size_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::size_t>(c - '0');
    }
    out = v;
    return true;
}

} // namespace

// ================================================================ le crochet ===
void FilterMemory::install(Store store) { memory().store = std::move(store); }

bool FilterMemory::installed() noexcept {
    const auto& s = memory().store;
    return static_cast<bool>(s.read) && static_cast<bool>(s.write);
}

std::string FilterMemory::read(const std::string& key) {
    if (key.empty() || !installed()) return {};
    return memory().store.read(key);
}

void FilterMemory::write(const std::string& key, const std::string& value) {
    if (key.empty() || !installed()) return;
    memory().store.write(key, value);
}

void FilterMemory::forget(bool allProjects) {
    if (memory().store.forget) memory().store.forget(allProjects);
}

// ======================================================== les widgets inscrits ===
std::shared_ptr<void> FilterMemory::enroll(Client client) {
    auto& m = memory();
    const std::uint64_t id = m.next++;
    m.clients.push_back({id, std::move(client)});
    // Le jeton : detruit, il desinscrit (le widget meurt avec lui).
    return std::shared_ptr<void>(static_cast<void*>(new char(0)), [id](void* p) {
        delete static_cast<char*>(p);
        std::erase_if(memory().clients, [id](const Entry& e) { return e.id == id; });
    });
}

void FilterMemory::contextChanged(bool reload) {
    // Par numero, relu a chaque appel : relire une table peut en faire naitre ou
    // mourir d'autres (un volet qui se refait) - une copie des fonctions
    // appellerait un widget detruit.
    std::vector<std::uint64_t> ids;
    for (const auto& e : memory().clients) ids.push_back(e.id);
    for (const auto id : ids) {
        std::function<void()> call;
        for (const auto& e : memory().clients)
            if (e.id == id) call = reload ? e.client.reload : e.client.resave;
        if (call) call();
    }
}

std::size_t FilterMemory::enrolled() noexcept { return memory().clients.size(); }

// ========================================================== le texte retenu ===
std::string FilterMemory::escape(std::string_view text) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool edgeBlank = (c == ' ' || c == '\t') && (i == 0 || i + 1 == text.size());
        if (c < 0x20 || c == 0x7F || c == '%' || c == ';' || c == ',' || c == '|' || c == '=' || edgeBlank) {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string FilterMemory::unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && isHex(text[i + 1]) && isHex(text[i + 2])) {
            out += static_cast<char>((hexValue(text[i + 1]) << 4) | hexValue(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string FilterMemory::join(const std::vector<std::string>& fields, char sep) {
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out += sep;
        out += escape(fields[i]);
    }
    return out;
}

std::vector<std::string> FilterMemory::split(std::string_view text, char sep) {
    auto parts = splitRaw(text, sep);
    for (auto& p : parts) p = unescape(p);
    return parts;
}

std::string FilterMemory::encodeColumns(const std::vector<SavedFilter>& filters) {
    std::string out;
    for (const auto& s : filters) {
        const auto& f = s.filter;
        if (!f.active()) continue;
        std::vector<std::string> fields{s.title, std::to_string(f.column), opName(f.op), f.value, f.value2, listName(f.list)};
        for (const auto& v : f.values) fields.push_back(v);
        out += ";" + join(fields, ',');
    }
    return out.empty() ? std::string{} : "c1" + out;
}

std::vector<FilterMemory::SavedFilter> FilterMemory::decodeColumns(std::string_view stored) {
    std::vector<SavedFilter> out;
    if (stored.rfind("c1;", 0) != 0) return out;
    const auto records = splitRaw(stored.substr(3), ';');
    for (const auto& record : records) {
        const auto fields = split(record, ',');
        if (fields.size() < 6) continue;           // abime : ignore
        SavedFilter s;
        s.title = fields[0];
        if (!parseIndex(fields[1], s.filter.column)) continue;
        s.filter.op = opFromName(fields[2]);
        s.filter.value = fields[3];
        s.filter.value2 = fields[4];
        s.filter.list = listFromName(fields[5]);
        for (std::size_t i = 6; i < fields.size(); ++i) s.filter.values.push_back(fields[i]);
        if (!s.filter.active()) continue;
        out.push_back(std::move(s));
    }
    return out;
}

int FilterMemory::columnFor(const std::vector<std::string>& titles, std::string_view title, std::size_t rank) {
    const std::string want = foldForSearch(title);
    if (want.empty()) return -1;
    int first = -1;
    for (std::size_t i = 0; i < titles.size(); ++i) {
        if (foldForSearch(titles[i]) != want) continue;
        if (i == rank) return static_cast<int>(i);
        if (first < 0) first = static_cast<int>(i);
    }
    return first;
}

std::string FilterMemory::encodeSearch(const std::string& search, const std::string& chip) {
    if (search.empty() && chip.empty()) return {};
    return "s1;" + escape(search) + ";" + escape(chip);
}

bool FilterMemory::decodeSearch(std::string_view stored, std::string& search, std::string& chip) {
    search.clear();
    chip.clear();
    if (stored.rfind("s1;", 0) != 0) return false;
    const auto parts = splitRaw(stored.substr(3), ';');
    if (!parts.empty()) search = unescape(parts[0]);
    if (parts.size() > 1) chip = unescape(parts[1]);
    return true;
}

std::string FilterMemory::describe(std::string_view stored) {
    std::string out;
    if (stored.rfind("c1;", 0) == 0) {
        for (const auto& s : decodeColumns(stored)) out += (out.empty() ? "" : " ; ") + s.filter.label(s.title);
        return out;
    }
    std::string search, chip;
    if (decodeSearch(stored, search, chip)) {
        if (!search.empty()) out = "recherche \xC2\xAB " + search + " \xC2\xBB";
        if (!chip.empty()) out += (out.empty() ? "" : " ; ") + std::string("pastille \xC2\xAB ") + chip + " \xC2\xBB";
        return out;
    }
    return std::string(stored);
}

// ============================================================ SearchMemory ===
void SearchMemory::bind(std::string key, std::function<State()> get, std::function<void(const State&)> set, bool recallNow) {
    key_ = std::move(key);
    get_ = std::move(get);
    set_ = std::move(set);
    token_ = FilterMemory::enroll({[this] { recall(); }, [this] { save(); }});
    if (recallNow) recall();
}

void SearchMemory::bindField(InputText& field, std::string key) {
    InputText* f = &field;
    if (key.empty()) key = field.id();
    links_ += field.textChanged->connect([this](const std::string&) { save(); });
    bind(std::move(key), [f] { return State{f->text(), {}}; }, [f](const State& s) { f->setText(s.search); }, true);
}

void SearchMemory::recall() {
    // Sans memoire (les tests, un outil) : rien ne change.
    if (!set_ || key_.empty() || !FilterMemory::installed()) return;
    State s;
    (void)FilterMemory::decodeSearch(FilterMemory::read(key_), s.search, s.chip);
    recalling_ = true;
    set_(s);
    recalling_ = false;
}

void SearchMemory::save() {
    if (recalling_ || !get_ || key_.empty() || !FilterMemory::installed()) return;
    const auto s = get_();
    FilterMemory::write(key_, FilterMemory::encodeSearch(s.search, s.chip));
}

// ====================================================== TableView : retenus ===
namespace {

bool sameFilters(const std::vector<ColumnFilter>& a, const std::vector<ColumnFilter>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.column != y.column || x.op != y.op || x.value != y.value || x.value2 != y.value2 || x.list != y.list || x.values != y.values)
            return false;
    }
    return true;
}

} // namespace

void TableView::recallColumnFilters() {
    if (!filtersEnabled_ || !remembers_ || id().empty()) {
        memoryPending_ = false;
        return;
    }
    // Inscrite une fois : un autre projet la fait relire, le meme ailleurs reecrire.
    if (!memoryToken_)
        memoryToken_ = FilterMemory::enroll({[this] { recallColumnFilters(); }, [this] { rememberColumnFilters(); }});
    if (!FilterMemory::installed()) {
        memoryPending_ = false;
        return;
    }
    // Pas encore de colonnes (posees a la mise en page) : a setColumns.
    if (columns_.empty()) {
        memoryPending_ = true;
        return;
    }
    memoryPending_ = false;
    std::vector<std::string> titles;
    for (std::size_t i = 0; i < columns_.size(); ++i)
        titles.push_back(columns_[i].title.empty() && model_ && i < model_->columnCount() ? model_->headerText(i) : columns_[i].title);
    std::vector<ColumnFilter> restored;
    for (auto& s : FilterMemory::decodeColumns(FilterMemory::read(id()))) {
        // Une colonne renommee (ou retiree) : le filtre est ignore, sans message.
        const int found = FilterMemory::columnFor(titles, s.title, s.filter.column);
        if (found < 0) continue;
        const auto c = static_cast<std::size_t>(found);
        if (!columns_[c].filterable || columns_[c].headerIcon != 0) continue;
        if (model_ && model_->columnCount() > 0 && c >= model_->columnCount()) continue;
        if (std::any_of(restored.begin(), restored.end(), [c](const ColumnFilter& f) { return f.column == c; })) continue;
        s.filter.column = c;
        restored.push_back(std::move(s.filter));
    }
    if (sameFilters(restored, columnFilters_)) return;      // rien ne change : pas de signal
    // Relus, pas ecrits : un autre projet sans filtres les efface ici sans
    // effacer ce que le projet d'avant retient.
    memoryRestoring_ = true;
    if (filterPopup_ && filterPopup_->isOpen()) filterPopup_->close();
    columnFilters_ = std::move(restored);
    columnFiltersEdited();
    memoryRestoring_ = false;
}

void TableView::rememberColumnFilters() {
    if (!filtersEnabled_ || !remembers_ || id().empty() || memoryPending_ || !FilterMemory::installed()) return;
    std::vector<FilterMemory::SavedFilter> saved;
    for (const auto& f : columnFilters_) {
        if (f.column >= columns_.size()) continue;
        const auto& col = columns_[f.column];
        const std::string title = col.title.empty() && model_ && f.column < model_->columnCount() ? model_->headerText(f.column) : col.title;
        if (title.empty()) continue;
        saved.push_back({title, f});
    }
    FilterMemory::write(id(), FilterMemory::encodeColumns(saved));      // "" : oublie
}

// ============================================================ les marques ===
void drawSearchMarks(gfx::IRenderer& r, const SearchQuery& query, std::string_view text, float x, float y, float h,
                     float maxWidth, gfx::FontId font) {
    if (query.empty() || text.empty() || maxWidth <= 0.f) return;
    const auto ranges = query.ranges(text);
    const gfx::Color mark{230, 180, 60, 110};       // celle des tableaux (TableView::paintHighlights)
    for (const auto& [a, b] : ranges) {
        const float x0 = x + r.measure(text.substr(0, a), font).width;
        if (x0 >= x + maxWidth) break;
        const float x1 = std::min(x + maxWidth, x + r.measure(text.substr(0, b), font).width);
        if (x1 > x0) r.fillRoundedRect({x0 - 1.f, y, x1 - x0 + 2.f, h}, mark, 2.f);
    }
}

} // namespace ui
