// =============================================================================
//  app/hmi/HmiItemsDialog.cpp - 1.12.7 : le tableau des elements d'une valeur
//  (voir l'en-tete)
// =============================================================================
#include "HmiItemsDialog.hpp"

#include "../../hmi/HmiTypeForms.hpp"
#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace app {

namespace tf = hmi::typeform;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// La valeur nulle d'un element (une case vide avant une case ecrite).
std::string zeroOf(const std::string& type) {
    const std::string u = upper(trim(type));
    if (u == "STRING" || u == "WSTRING" || u.rfind("STRING[", 0) == 0 || u.rfind("STRING(", 0) == 0) return "''";
    if (u == "CHAR" || u == "WCHAR") return "' '";
    if (u == "BOOL") return "FALSE";
    if (u == "TIME") return "T#0s";
    if (u == "REAL" || u == "LREAL") return "0.0";
    if (u.rfind("LIST", 0) == 0 || u.rfind("VECTOR", 0) == 0 || u.rfind("MAP", 0) == 0 || u.rfind("ARRAY", 0) == 0 || u.rfind("SET ", 0) == 0
        || u.rfind("RING", 0) == 0)      // 1.12.7 : SET, RING
        return "[]";
    return "0";
}

// "1..4" -> {1, 4} ; faux : illisible.
bool bounds(const std::string& text, long long& lo, long long& hi) {
    const auto dots = text.find("..");
    if (dots == std::string::npos) return false;
    char* end = nullptr;
    const std::string a = trim(text.substr(0, dots)), b = trim(text.substr(dots + 2));
    lo = std::strtoll(a.c_str(), &end, 10);
    if (a.empty() || !end || *end) return false;
    hi = std::strtoll(b.c_str(), &end, 10);
    if (b.empty() || !end || *end) return false;
    return hi >= lo && hi - lo < 10000;
}

// Les types d'un tuple : "INT, STRING" (les virgules de premier niveau).
std::vector<std::string> splitTop(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (const char c : s) {
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

// Une cle de MAP ecrite : 'bas' -> bas (le tableau la montre sans apostrophes).
std::string keyShown(std::string k) {
    k = trim(std::move(k));
    if (k.size() >= 2 && (k.front() == '\'' || k.front() == '"') && k.back() == k.front()) return k.substr(1, k.size() - 2);
    return k;
}

float buttonWidth(const std::string& text) { return std::max(96.f, ui::measureWidth(text, kButton) + 36.f); }

class ItemsBody final : public ui::Widget {
public:
    ItemsBody(std::string title, std::string info) : title_(std::move(title)), info_(std::move(info)) {}
    ui::TableView*           table{nullptr};
    std::vector<ui::Button*> left;        // Ajouter, Supprimer, Monter, Descendre
    ui::Button*              cancel{nullptr};
    ui::Button*              ok{nullptr};
    std::string              status;

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(720.f, std::max(360.f, r.w - 40.f));
        const float h = std::min(560.f, std::max(320.f, r.h - 40.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f, inner = w - 32.f;
        const float top = panel_.y + 34.f + 10.f + 40.f;
        const float by = panel_.bottom() - 44.f;
        if (table) table->setBounds({x, top, inner, std::max(60.f, by - 30.f - top)});
        float lx = x;
        for (auto* b : left) {
            const float bw = buttonWidth(b->text());
            b->setBounds({lx, by, bw, 30.f});
            lx += bw + 8.f;
        }
        float bx = panel_.right() - 16.f;
        for (auto* b : {ok, cancel}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
        statusAt_ = by - 24.f;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 34.f}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold;
        r.drawText({panel_.x + 12.f, panel_.y + (34.f - r.lineHeight(bold)) * 0.5f}, title_, bold, c.text);
        // L'explication, sur deux lignes au plus.
        float y = panel_.y + 34.f + 10.f;
        std::string line, word;
        int lines = 0;
        const float inner = panel_.w - 32.f;
        const auto flush = [&](bool last) {
            if (!word.empty()) {
                const std::string trial = line.empty() ? word : line + " " + word;
                if (!line.empty() && ui::measureWidth(trial, kSmall) > inner) {
                    if (lines < 2) r.drawText({panel_.x + 16.f, y}, line, kSmall, c.textMuted);
                    y += 18.f;
                    ++lines;
                    line = word;
                } else {
                    line = trial;
                }
                word.clear();
            }
            if (last && !line.empty() && lines < 2) r.drawText({panel_.x + 16.f, y}, line, kSmall, c.textMuted);
        };
        for (const char ch : info_) {
            if (ch == ' ') flush(false);
            else word += ch;
        }
        flush(true);
        if (!status.empty()) r.drawText({panel_.x + 16.f, statusAt_}, status, kSmall, c.text);
    }

private:
    std::string title_, info_;
    gfx::Rect   panel_{};
    float       statusAt_{0.f};
};

} // namespace

// ------------------------------------------------------------------ le modele ----
std::size_t HmiItemsDialog::Model::rowCount() const {
    if (mode == Mode::Matrix) return labels.size();
    return values.size();
}

std::size_t HmiItemsDialog::Model::columnCount() const { return mode == Mode::Matrix ? 1 + columns2 : 2; }

std::string HmiItemsDialog::Model::headerText(std::size_t c) const {
    switch (mode) {
        case Mode::List:
            if (flavor == "RING") return c == 0 ? "#" : "Valeur (" + element + ", " + std::to_string(capacity) + " au plus)";     // 1.12.7
            return c == 0 ? "#" : "Valeur (" + element + ")";
        case Mode::Array:  return c == 0 ? "Indice" : "Valeur (" + element + ")";
        case Mode::Map:    return c == 0 ? std::string(textKeys ? "Cl\xC3\xA9 (texte)" : "Cl\xC3\xA9 (nombre)") : "Valeur (" + element + ")";
        case Mode::Tuple:  return c == 0 ? std::string("\xC3\x89l\xC3\xA9ment") : std::string("Valeur");
        case Mode::Matrix: return c == 0 ? std::string("Ligne") : (c - 1 < columnLabels.size() ? columnLabels[c - 1] : std::string{});
    }
    return {};
}

std::string HmiItemsDialog::Model::cellText(ui::RowIndex r, std::size_t c) const {
    switch (mode) {
        case Mode::List:
            if (r >= values.size()) return {};
            return c == 0 ? std::to_string(r) : values[r];
        case Mode::Array:
        case Mode::Tuple:
            if (r >= values.size()) return {};
            return c == 0 ? (r < labels.size() ? labels[r] : std::string{}) : values[r];
        case Mode::Map:
            if (r >= values.size()) return {};
            return c == 0 ? (r < keys.size() ? keys[r] : std::string{}) : values[r];
        case Mode::Matrix: {
            if (r >= labels.size()) return {};
            if (c == 0) return labels[r];
            const std::size_t at = r * columns2 + (c - 1);
            return at < values.size() ? values[at] : std::string{};
        }
    }
    return {};
}

ui::CellStyle HmiItemsDialog::Model::cellStyle(ui::RowIndex, std::size_t c) const {
    ui::CellStyle s;
    s.monospace = true;
    if (c == 0 && mode != Mode::Map) s.fgTone = ui::Tone::Muted;
    return s;
}

bool HmiItemsDialog::Model::editable(ui::RowIndex r, std::size_t c) const {
    if (r >= rowCount()) return false;
    if (mode == Mode::Map) return c <= 1;
    return c >= 1;
}

bool HmiItemsDialog::Model::setCellText(ui::RowIndex r, std::size_t c, std::string_view text) {
    if (!editable(r, c)) return false;
    const std::string t = trim(std::string(text));
    if (mode == Mode::Matrix) {
        const std::size_t at = r * columns2 + (c - 1);
        if (at >= values.size()) return false;
        values[at] = t;
        return true;
    }
    if (mode == Mode::Map && c == 0) {
        if (r >= keys.size()) return false;
        keys[r] = keyShown(t);
        return true;
    }
    if (r >= values.size()) return false;
    values[r] = t;
    return true;
}

// 1.12.7 : un SET se range et perd ses doublons, un RING garde ses N dernieres valeurs - comme le moteur
// les lira. Les valeurs ecrites se comparent comme des nombres (12 < 100), sinon comme des textes ('P1').
namespace {
bool numberText(const std::string& t, double& out) {
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
}
std::string unquoted(const std::string& t) {
    return t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front() ? t.substr(1, t.size() - 2) : t;
}
void shapeItems(std::vector<std::string>& items, const std::string& flavor, std::size_t capacity) {
    if (flavor == "RING" && capacity > 0 && items.size() > capacity) items.erase(items.begin(), items.end() - static_cast<std::ptrdiff_t>(capacity));
    if (flavor != "SET") return;
    std::stable_sort(items.begin(), items.end(), [](const std::string& a, const std::string& b) {
        double x = 0, y = 0;
        const bool na = numberText(a, x), nb = numberText(b, y);
        if (na && nb) return x < y;
        if (na != nb) return na;
        return unquoted(a) < unquoted(b);
    });
    std::vector<std::string> kept;
    for (auto& i : items) {
        double x = 0, y = 0;
        const bool same = !kept.empty() && (numberText(kept.back(), x) && numberText(i, y) ? x == y : unquoted(kept.back()) == unquoted(i));
        if (!same) kept.push_back(std::move(i));
    }
    items = std::move(kept);
}
} // namespace

std::shared_ptr<HmiItemsDialog::Model> HmiItemsDialog::modelOf(const std::string& type, const std::string& value) {
    auto m = std::make_shared<Model>();
    const auto shape = tf::decompose(type);
    m->element = shape.element;
    std::vector<std::string> items;
    const bool literal = tf::literalItems(value, items);
    const std::string single = literal ? std::string{} : trim(value);     // une valeur seule (un tableau : toutes ses cases)
    switch (shape.form) {
        case tf::Form::Map: {
            m->mode = Model::Mode::Map;
            const std::string key = upper(trim(shape.parameter));
            m->textKeys = key.empty() || key.find("STRING") != std::string::npos;
            for (const auto& it : items) {
                const auto at = it.find(":=");
                if (at == std::string::npos) continue;
                m->keys.push_back(keyShown(it.substr(0, at)));
                m->values.push_back(trim(it.substr(at + 2)));
            }
            break;
        }
        case tf::Form::Tuple: {
            m->mode = Model::Mode::Tuple;
            // Les types : l'element, puis ceux du parametre (TUPLE(INT, STRING) : INT ; STRING).
            m->types.push_back(shape.element);
            for (auto& t : splitTop(shape.parameter)) m->types.push_back(t);
            for (std::size_t k = 0; k < m->types.size(); ++k) {
                m->labels.push_back("Item" + std::to_string(k + 1) + " : " + m->types[k]);
                m->values.push_back(k < items.size() ? items[k] : std::string{});
            }
            break;
        }
        case tf::Form::Array:
        case tf::Form::Array2D: {
            const auto dims = splitTop(shape.parameter);
            long long lo1 = 0, hi1 = -1, lo2 = 0, hi2 = -1;
            const bool two = shape.form == tf::Form::Array2D && dims.size() == 2 && bounds(dims[0], lo1, hi1) && bounds(dims[1], lo2, hi2);
            if (two) {
                m->mode = Model::Mode::Matrix;
                m->columns2 = static_cast<std::size_t>(hi2 - lo2 + 1);
                for (long long j = lo2; j <= hi2; ++j) m->columnLabels.push_back("[.., " + std::to_string(j) + "]");
                for (long long i = lo1; i <= hi1; ++i) m->labels.push_back("[" + std::to_string(i) + ", ..]");
                const std::size_t n = m->labels.size() * m->columns2;
                for (std::size_t k = 0; k < n; ++k) m->values.push_back(k < items.size() ? items[k] : single);
                break;
            }
            if (!dims.empty() && bounds(dims[0], lo1, hi1)) {
                m->mode = Model::Mode::Array;
                for (long long i = lo1; i <= hi1; ++i) {
                    m->labels.push_back("[" + std::to_string(i) + "]");
                    const std::size_t k = static_cast<std::size_t>(i - lo1);
                    m->values.push_back(k < items.size() ? items[k] : single);
                }
                break;
            }
            m->mode = Model::Mode::List;     // des bornes illisibles : comme une liste
            m->values = items;
            break;
        }
        default:
            m->mode = Model::Mode::List;
            m->values = items;
            if (!literal && !single.empty()) m->values.push_back(single);
            // 1.12.7 : un SET (range, sans doublon), un RING (ses N dernieres).
            if (shape.form == tf::Form::Set || shape.form == tf::Form::Ring) {
                m->flavor = shape.form == tf::Form::Set ? "SET" : "RING";
                long long n = 0;
                if (shape.form == tf::Form::Ring) {
                    char* end = nullptr;
                    n = std::strtoll(shape.parameter.c_str(), &end, 10);
                    m->capacity = n > 0 ? static_cast<std::size_t>(n) : 0;
                }
                shapeItems(m->values, m->flavor, m->capacity);
            }
            break;
    }
    return m;
}

std::string HmiItemsDialog::literalOf(const Model& m) {
    std::vector<std::string> items;
    switch (m.mode) {
        case Model::Mode::List:
            for (const auto& v : m.values)
                if (!trim(v).empty()) items.push_back(trim(v));
            shapeItems(items, m.flavor, m.capacity);       // 1.12.7 : SET, RING
            break;
        case Model::Mode::Map:
            for (std::size_t k = 0; k < m.values.size() && k < m.keys.size(); ++k) {
                const std::string key = trim(m.keys[k]);
                if (key.empty()) continue;
                const std::string written = m.textKeys ? "'" + key + "'" : key;
                const std::string v = trim(m.values[k]).empty() ? zeroOf(m.element) : trim(m.values[k]);
                items.push_back(written + " := " + v);
            }
            break;
        case Model::Mode::Tuple:
            for (std::size_t k = 0; k < m.values.size(); ++k)
                items.push_back(trim(m.values[k]).empty() ? zeroOf(k < m.types.size() ? m.types[k] : std::string{}) : trim(m.values[k]));
            return tf::literalOf(items, true);
        case Model::Mode::Array:
        case Model::Mode::Matrix: {
            // Jusqu'a la derniere case ecrite ; une case vide avant elle : la valeur nulle de l'element.
            std::size_t last = 0;
            for (std::size_t k = 0; k < m.values.size(); ++k)
                if (!trim(m.values[k]).empty()) last = k + 1;
            for (std::size_t k = 0; k < last; ++k) items.push_back(trim(m.values[k]).empty() ? zeroOf(m.element) : trim(m.values[k]));
            break;
        }
    }
    return tf::literalOf(items, false);
}

// ------------------------------------------------------------------ le dialogue ----
HmiItemsDialog::HmiItemsDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)), model_(modelOf(spec_.type, spec_.value)) {}

HmiItemsDialog::~HmiItemsDialog() = default;

menu::MenuTraits HmiItemsDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiItemsDialog::literal() const { return literalOf(*model_); }

std::size_t HmiItemsDialog::selected() const {
    if (!table_) return static_cast<std::size_t>(-1);
    const auto rows = table_->selectedModelRows();
    return rows.empty() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(rows.front());
}

bool HmiItemsDialog::addRow() {
    if (!model_->resizable()) return false;
    const std::size_t at = model_->values.size();
    model_->values.emplace_back();
    if (model_->mode == Model::Mode::Map) model_->keys.emplace_back();
    model_->modelReset->emit();
    if (table_) {
        table_->selectModelRows({static_cast<ui::RowIndex>(at)}, false);
        (void)table_->beginCellEdit(static_cast<ui::RowIndex>(at), model_->mode == Model::Mode::Map ? 0 : 1);
    }
    return true;
}

bool HmiItemsDialog::removeRow(std::size_t row) {
    if (!model_->resizable() || row >= model_->values.size()) return false;
    model_->values.erase(model_->values.begin() + static_cast<std::ptrdiff_t>(row));
    if (model_->mode == Model::Mode::Map && row < model_->keys.size()) model_->keys.erase(model_->keys.begin() + static_cast<std::ptrdiff_t>(row));
    model_->modelReset->emit();
    if (table_ && !model_->values.empty())
        table_->selectModelRows({static_cast<ui::RowIndex>(std::min(row, model_->values.size() - 1))}, false);
    return true;
}

bool HmiItemsDialog::moveRow(std::size_t row, int delta) {
    if (!model_->resizable() || row >= model_->values.size()) return false;
    if (model_->flavor == "SET") return false;          // 1.12.7 : un SET se range tout seul
    const auto to = static_cast<long long>(row) + delta;
    if (to < 0 || to >= static_cast<long long>(model_->values.size())) return false;
    std::swap(model_->values[row], model_->values[static_cast<std::size_t>(to)]);
    if (model_->mode == Model::Mode::Map) std::swap(model_->keys[row], model_->keys[static_cast<std::size_t>(to)]);
    model_->modelReset->emit();
    if (table_) table_->selectModelRows({static_cast<ui::RowIndex>(to)}, false);
    return true;
}

bool HmiItemsDialog::setCell(std::size_t row, std::size_t column, const std::string& text) {
    const bool ok = model_->setCellText(static_cast<ui::RowIndex>(row), column, text);
    if (ok) model_->modelReset->emit();
    return ok;
}

core::Status HmiItemsDialog::buildUi() {
    std::string info = "Le type : " + spec_.type + ". ";
    switch (model_->mode) {
        case Model::Mode::List: info += "Une ligne par \xC3\xA9l\xC3\xA9ment, dans l'ordre (une ligne vide ne compte pas)."; break;
        case Model::Mode::Map: info += "Une ligne par entr\xC3\xA9" "e : sa cl\xC3\xA9 et sa valeur (une cl\xC3\xA9 vide ne compte pas)."; break;
        case Model::Mode::Tuple: info += "Une valeur par \xC3\xA9l\xC3\xA9ment du tuple, dans l'ordre de ses types."; break;
        case Model::Mode::Array:
        case Model::Mode::Matrix:
            info += "Case par case ; apr\xC3\xA8s la derni\xC3\xA8re \xC3\xA9" "crite, les cases gardent la valeur de leur type.";
            break;
    }
    info += " Double-clic ou F2 pour \xC3\xA9" "crire une case. Ctrl+Z reprend la valeur appliqu\xC3\xA9" "e.";
    auto body = std::make_unique<ItemsBody>(spec_.title, info);
    const std::string base = spec_.id;
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setModel(model_);
    table_ = &static_cast<ui::TableView&>(body->addChild(std::move(table)));
    body->table = table_;
    if (model_->resizable()) {
        const auto button = [&](const std::string& text, const std::string& id) {
            auto* b = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, base + "." + id)));
            body->left.push_back(b);
            return b;
        };
        links_ += button("+ Ajouter", "ajouter")->clicked->connect([this] { (void)addRow(); });
        links_ += button("Supprimer", "supprimer")->clicked->connect([this] { (void)removeRow(selected()); });
        links_ += button("Monter", "monter")->clicked->connect([this] { (void)moveRow(selected(), -1); });
        links_ += button("Descendre", "descendre")->clicked->connect([this] { (void)moveRow(selected(), +1); });
    }
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    body->ok = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { finish(true); });
    body->status = std::to_string(model_->rowCount()) + (model_->rowCount() > 1 ? " lignes" : " ligne");
    links_ += model_->modelReset->connect([this, b = body.get()] {
        b->status = std::to_string(model_->rowCount()) + (model_->rowCount() > 1 ? " lignes" : " ligne");
        b->invalidate();
    });
    body_ = body.get();
    setRoot(std::move(body));
    focus().clear();
    focus().focus(table_);
    return core::ok();
}

ui::EventResult HmiItemsDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !(table_ && table_->cellEditing())) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Insert && k->mods.none() && addRow()) return ui::EventResult::Consumed;
        if (k->key == ui::Key::Delete && k->mods.none() && removeRow(selected())) return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiItemsDialog::finish(bool ok) {
    if (done_) return;
    if (ok && table_ && table_->cellEditing()) {                             // la case en cours compte
        const auto* field = table_->activeCellField();
        table_->finishCellEdit(true, field ? field->text() : std::string{});
    }
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? literal() : std::string{}});
}

} // namespace app
