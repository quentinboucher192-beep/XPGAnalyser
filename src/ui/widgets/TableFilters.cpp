// =============================================================================
//  ui/widgets/TableFilters.cpp - lot recherche : les filtres des colonnes d'une
//  TableView (le filtre, sa fenetre, la bande des pastilles) et la recherche
//  surlignee dans les cases. Voir TableFilters.hpp et DataViews.hpp.
// =============================================================================
#include "TableFilters.hpp"

#include "Controls.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ui {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kText{15};
constexpr float kWidth = 380.f;
constexpr float kPad = 12.f;
constexpr float kTitleH = 36.f;
constexpr float kChipH = 24.f;
constexpr float kChipGap = 6.f;
constexpr float kFieldH = 28.f;
constexpr float kRowH = 24.f;
constexpr float kButtonH = 30.f;
constexpr float kSearchW = 190.f;
constexpr float kStripH = 30.f;
constexpr std::size_t kMaxValues = 5000;     // les valeurs distinctes lues au plus
constexpr std::size_t kMaxShown = 200;       // les valeurs montrees au plus

bool isBlank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

std::string_view trimView(std::string_view s) noexcept {
    while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
    while (!s.empty() && isBlank(s.back())) s.remove_suffix(1);
    return s;
}

// Une case sans valeur : vide, ou le tiret qu'y mettent les volets.
bool blankCell(std::string_view s) noexcept {
    s = trimView(s);
    return s.empty() || s == "-" || s == "\xE2\x80\x94" || s == "\xE2\x80\x93";
}

// Un texte court pour une pastille : coupe a `max` octets, sur un debut de caractere.
std::string shortText(std::string_view s, std::size_t max) {
    if (s.size() <= max) return std::string(s);
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return std::string(s.substr(0, cut)) + "\xE2\x80\xA6";
}

std::string thousands(std::size_t n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string shownValue(const std::string& v) { return blankCell(v) ? std::string("(vide)") : v; }

// Un texte qui tient dans `maxW`, coupe avec "..." sinon (sur un debut de caractere).
void drawFit(gfx::IRenderer& r, gfx::Point at, std::string_view text, gfx::FontId f, gfx::Color c, float maxW) {
    if (maxW <= 0.f || text.empty()) return;
    const auto fits = r.fitCharacters(text, f, maxW);
    if (fits >= text.size()) {
        r.drawText(at, text, f, c);
        return;
    }
    std::size_t keep = fits > 3 ? fits - 3 : 0;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    r.drawText(at, std::string(text.substr(0, keep)) + "...", f, c);
}

void cross(gfx::IRenderer& r, gfx::Point c, float s, gfx::Color col, float w = 1.5f) {
    r.line({c.x - s, c.y - s}, {c.x + s, c.y + s}, col, w);
    r.line({c.x - s, c.y + s}, {c.x + s, c.y - s}, col, w);
}

// Une case a cocher : cochee, a moitie (une partie des valeurs), vide.
void checkbox(gfx::IRenderer& r, const Theme& th, const gfx::Rect& box, bool on, bool partial) {
    const auto& c = th.color;
    if (on || partial) {
        r.fillRoundedRect(box, c.accent, 3.f);
        if (on) {
            r.line({box.x + 3.f, box.y + box.h * 0.52f}, {box.x + box.w * 0.42f, box.bottom() - 3.5f}, c.selectionText, 1.8f);
            r.line({box.x + box.w * 0.42f, box.bottom() - 3.5f}, {box.right() - 3.f, box.y + 3.5f}, c.selectionText, 1.8f);
        } else {
            r.fillRect({box.x + 3.5f, box.y + box.h * 0.5f - 1.f, box.w - 7.f, 2.f}, c.selectionText);
        }
    } else {
        r.fillRoundedRect(box, c.inputBg, 3.f);
        r.strokeRect(box, c.borderStrong, 1.f);
    }
}

bool needsValueOp(ColumnFilter::Op op) noexcept {
    using Op = ColumnFilter::Op;
    return op == Op::Contains || op == Op::StartsWith || op == Op::Equals || op == Op::NotEquals || op == Op::Between;
}

} // namespace

// ================================================================ ColumnFilter ===
bool ColumnFilter::parseNumber(std::string_view text, double& out) {
    const std::string_view s = trimView(text);
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }
    double value = 0.0;
    std::size_t intDigits = 0, fracDigits = 0;
    while (i < s.size()) {
        if (isDigit(s[i])) {
            value = value * 10.0 + static_cast<double>(s[i] - '0');
            ++intDigits;
            ++i;
            continue;
        }
        // Un separateur de milliers (espace, insecable, fine insecable) suivi
        // d'exactement trois chiffres : "1 234".
        std::size_t sep = 0;
        if (s[i] == ' ') sep = 1;
        else if (s.compare(i, 2, "\xC2\xA0") == 0) sep = 2;
        else if (s.compare(i, 3, "\xE2\x80\xAF") == 0) sep = 3;
        const std::size_t at = i + sep;
        if (sep && intDigits > 0 && at + 3 <= s.size() && isDigit(s[at]) && isDigit(s[at + 1]) && isDigit(s[at + 2])
            && (at + 3 == s.size() || !isDigit(s[at + 3]))) {
            i = at;
            continue;
        }
        break;
    }
    if (i + 1 < s.size() && (s[i] == '.' || s[i] == ',') && isDigit(s[i + 1])) {
        ++i;
        double scale = 0.1;
        while (i < s.size() && isDigit(s[i])) {
            value += static_cast<double>(s[i] - '0') * scale;
            scale *= 0.1;
            ++fracDigits;
            ++i;
        }
    }
    if (intDigits + fracDigits == 0) return false;
    // Ce qui suit : rien, ou une petite unite sans chiffre ("ms", "%", "x").
    const auto rest = trimView(s.substr(i));
    if (rest.size() > 4) return false;
    for (const char c : rest)
        if (isDigit(c) || c == '.' || c == ',') return false;
    out = negative ? -value : value;
    return true;
}

bool ColumnFilter::active() const noexcept {
    if (list != List::None) return true;
    switch (op) {
        case Op::None: return false;
        case Op::Empty: case Op::NotEmpty: return true;
        case Op::Between: return !trimView(value).empty() || !trimView(value2).empty();
        default: return !trimView(value).empty();
    }
}

bool ColumnFilter::accepts(std::string_view cell) const {
    const auto t = trimView(cell);
    const auto v = trimView(value);
    switch (op) {
        case Op::None: break;
        case Op::Contains:
            if (!v.empty() && !containsFolded(t, foldForSearch(v))) return false;
            break;
        case Op::StartsWith:
            if (!v.empty() && foldForSearch(t).rfind(foldForSearch(v), 0) != 0) return false;
            break;
        case Op::Equals:
            if (!v.empty() && foldForSearch(t) != foldForSearch(v)) return false;
            break;
        case Op::NotEquals:
            if (!v.empty() && foldForSearch(t) == foldForSearch(v)) return false;
            break;
        case Op::Empty:
            if (!blankCell(t)) return false;
            break;
        case Op::NotEmpty:
            if (blankCell(t)) return false;
            break;
        case Op::Between: {
            if (trimView(value).empty() && trimView(value2).empty()) break;
            double x = 0.0, a = 0.0, b = 0.0;
            if (!parseNumber(t, x)) return false;
            if (!trimView(value).empty() && parseNumber(value, a) && x < a) return false;
            if (!trimView(value2).empty() && parseNumber(value2, b) && x > b) return false;
            break;
        }
    }
    if (list == List::None) return true;
    const bool listed = std::find(values.begin(), values.end(), cell) != values.end();
    return list == List::Only ? listed : !listed;
}

std::string ColumnFilter::opLabel(Op op) {
    switch (op) {
        case Op::Contains:   return "contient";
        case Op::StartsWith: return "commence par";
        case Op::Equals:     return "=";
        case Op::NotEquals:  return "diff\xC3\xA9rent de";
        case Op::Empty:      return "vide";
        case Op::NotEmpty:   return "non vide";
        case Op::Between:    return "entre";
        default:             return {};
    }
}

std::string ColumnFilter::label(std::string_view columnTitle) const {
    const std::string title(columnTitle);
    const std::string v = shortText(trimView(value), 28), v2 = shortText(trimView(value2), 16);
    std::string cond;
    switch (op) {
        case Op::Contains:   if (!v.empty()) cond = title + " contient " + v; break;
        case Op::StartsWith: if (!v.empty()) cond = title + " commence par " + v; break;
        case Op::Equals:     if (!v.empty()) cond = title + " = " + v; break;
        case Op::NotEquals:  if (!v.empty()) cond = title + " diff\xC3\xA9rent de " + v; break;
        case Op::Empty:      cond = title + " vide"; break;
        case Op::NotEmpty:   cond = title + " non vide"; break;
        case Op::Between:
            if (!v.empty() && !v2.empty()) cond = title + " entre " + v + " et " + v2;
            else if (!v.empty()) cond = title + " >= " + v;
            else if (!v2.empty()) cond = title + " <= " + v2;
            break;
        default: break;
    }
    if (list == List::None) return cond;
    std::string part;
    const auto joined = [&] {
        std::string s;
        for (std::size_t i = 0; i < values.size(); ++i) s += (i ? ", " : "") + shortText(shownValue(values[i]), 16);
        return s;
    };
    const std::string many = std::to_string(values.size()) + " valeurs";
    if (list == List::Only) {
        if (values.empty()) part = ": aucune valeur";
        else if (values.size() == 1) part = "= " + shortText(shownValue(values.front()), 28);
        else part = ": " + (values.size() <= 3 ? joined() : many);
    } else {
        part = "sauf " + (values.size() == 1 ? shortText(shownValue(values.front()), 28) : values.size() <= 3 ? joined() : many);
    }
    return cond.empty() ? title + " " + part : cond + ", " + part;
}

bool ColumnFilter::acceptsAll(const std::vector<ColumnFilter>& filters, const std::function<std::string(std::size_t)>& cell,
                              int skipColumn) {
    for (const auto& f : filters) {
        if (skipColumn >= 0 && f.column == static_cast<std::size_t>(skipColumn)) continue;
        if (!f.active()) continue;
        if (!f.accepts(cell(f.column))) return false;
    }
    return true;
}

// =========================================================== ColumnFilterPopup ===
ColumnFilterPopup::ColumnFilterPopup(std::string id) : Widget(std::move(id)) {}

void ColumnFilterPopup::open(Spec spec) {
    spec_ = std::move(spec);
    const auto& cur = spec_.current;
    op_ = cur.op == ColumnFilter::Op::None ? ColumnFilter::Op::Contains : cur.op;
    value_ = cur.value;
    value2_ = cur.value2;
    listSearch_.clear();
    items_.clear();
    items_.reserve(spec_.values.size());
    for (const auto& [v, n] : spec_.values) {
        bool checked = true;
        const bool listed = std::find(cur.values.begin(), cur.values.end(), v) != cur.values.end();
        if (cur.list == ColumnFilter::List::Only) checked = listed;
        else if (cur.list == ColumnFilter::List::Except) checked = !listed;
        items_.push_back({v, n, checked});
    }
    field_ = needsValue() ? 0 : 2;
    replaceOnType_ = !value_.empty();
    hoverRow_ = hoverChip_ = -1;
    open_ = true;
    refilter();
    setFocusPolicy(true);
    grabFocus();
    invalidate();
}

void ColumnFilterPopup::close() {
    if (!open_) return;
    open_ = false;
    if (focused()) releaseFocus();
    setFocusPolicy(false);            // fermee, elle n'est plus sur le chemin de Tab
    invalidate();
}

void ColumnFilterPopup::onFocusChanged(bool gained) {
    if (!gained) close();             // un clic ailleurs : fermee, rien de change
}

bool ColumnFilterPopup::needsValue() const noexcept { return needsValueOp(op_); }

std::vector<ColumnFilter::Op> ColumnFilterPopup::ops() const {
    using Op = ColumnFilter::Op;
    std::vector<Op> out{Op::Contains, Op::StartsWith, Op::Equals, Op::NotEquals, Op::Empty, Op::NotEmpty};
    if (spec_.numeric || op_ == Op::Between) out.push_back(Op::Between);
    return out;
}

ColumnFilter ColumnFilterPopup::draft() const {
    using Op = ColumnFilter::Op;
    ColumnFilter f;
    f.column = spec_.column;
    f.op = op_;
    f.value = std::string(trimView(value_));
    f.value2 = std::string(trimView(value2_));
    if (f.op == Op::Between ? (f.value.empty() && f.value2.empty()) : (needsValueOp(f.op) && f.value.empty())) f.op = Op::None;
    if (!needsValueOp(f.op)) f.value.clear();
    if (f.op != Op::Between) f.value2.clear();
    std::vector<std::string> on, off;
    for (const auto& it : items_) (it.checked ? on : off).push_back(it.value);
    if (!off.empty()) {
        if (on.size() <= off.size()) {
            f.list = ColumnFilter::List::Only;
            f.values = std::move(on);
        } else {
            f.list = ColumnFilter::List::Except;
            f.values = std::move(off);
        }
    }
    return f;
}

void ColumnFilterPopup::setOp(ColumnFilter::Op op) {
    using Op = ColumnFilter::Op;
    // Un second clic sur "vide", "non vide" ou "entre" les retire : la condition
    // redevient "contient" (sans valeur, elle ne filtre rien).
    if (op == op_ && op != Op::Contains) op = Op::Contains;
    op_ = op;
    if (!needsValue() && field_ < 2) field_ = 2;
    else if (needsValue() && field_ == 2 && listSearch_.empty()) field_ = 0;
    if (op_ != Op::Between && field_ == 1) field_ = 0;
    invalidate();
}

void ColumnFilterPopup::setValue(const std::string& text, bool second) {
    if (!needsValue()) op_ = ColumnFilter::Op::Contains;
    (second && op_ == ColumnFilter::Op::Between ? value2_ : value_) = text;
    field_ = second && op_ == ColumnFilter::Op::Between ? 1 : 0;
    replaceOnType_ = false;
    invalidate();
}

void ColumnFilterPopup::setListSearch(const std::string& text) {
    listSearch_ = text;
    field_ = 2;
    replaceOnType_ = false;
    refilter();
}

bool ColumnFilterPopup::toggleValue(std::string_view value) {
    for (auto& it : items_)
        if (it.value == value || (blankCell(value) && blankCell(it.value) && value == "(vide)")) {
            it.checked = !it.checked;
            invalidate();
            return true;
        }
    return false;
}

void ColumnFilterPopup::setAllShown(bool checked) {
    for (const auto i : shown_) items_[i].checked = checked;
    invalidate();
}

void ColumnFilterPopup::apply() {
    const ColumnFilter f = draft();
    close();
    applied->emit(f);
}

void ColumnFilterPopup::clearFilter() {
    const std::size_t column = spec_.column;
    close();
    cleared->emit(column);
}

void ColumnFilterPopup::refilter() {
    shown_.clear();
    const std::string needle = foldForSearch(trimView(listSearch_));
    for (std::size_t i = 0; i < items_.size() && shown_.size() < kMaxShown; ++i)
        if (needle.empty() || containsFolded(shownValue(items_[i].value), needle)) shown_.push_back(i);
    scroll_ = 0.f;
    invalidate();
}

std::string& ColumnFilterPopup::fieldText(int field) {
    return field == 2 ? listSearch_ : field == 1 ? value2_ : value_;
}

void ColumnFilterPopup::typeInto(const std::string& utf8) {
    if (field_ < 2 && !needsValue()) op_ = ColumnFilter::Op::Contains;
    auto& s = fieldText(field_);
    if (replaceOnType_) {
        s.clear();
        replaceOnType_ = false;
    }
    if (s.size() < 200) s += utf8;
    if (field_ == 2) refilter();
    invalidate();
}

void ColumnFilterPopup::backspace() {
    auto& s = fieldText(field_);
    if (replaceOnType_) {
        s.clear();
        replaceOnType_ = false;
    } else if (!s.empty()) {
        std::size_t cut = s.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.erase(cut);
    }
    if (field_ == 2) refilter();
    invalidate();
}

void ColumnFilterPopup::nextField(bool backwards) {
    std::vector<int> fields;
    if (needsValue()) fields.push_back(0);
    if (op_ == ColumnFilter::Op::Between) fields.push_back(1);
    fields.push_back(2);
    auto it = std::find(fields.begin(), fields.end(), field_);
    std::size_t at = it == fields.end() ? 0 : static_cast<std::size_t>(it - fields.begin());
    at = backwards ? (at + fields.size() - 1) % fields.size() : (at + 1) % fields.size();
    field_ = fields[at];
    replaceOnType_ = !fieldText(field_).empty();
    invalidate();
}

// ------------------------------------------------------------------ geometrie ---
namespace {
// Les conditions en rangees, dans la largeur de la fenetre (origine : son coin).
std::vector<std::pair<ColumnFilter::Op, gfx::Rect>> chipLayout(const std::vector<ColumnFilter::Op>& ops) {
    std::vector<std::pair<ColumnFilter::Op, gfx::Rect>> out;
    float x = kPad, y = kTitleH;
    for (const auto op : ops) {
        const float w = measureWidth(ColumnFilter::opLabel(op), kSmall) + 22.f;
        if (x + w > kWidth - kPad && x > kPad) {
            x = kPad;
            y += kChipH + kChipGap;
        }
        out.emplace_back(op, gfx::Rect{x, y, w, kChipH});
        x += w + kChipGap;
    }
    return out;
}
} // namespace

float ColumnFilterPopup::chipsBottom() const {
    const auto layout = chipLayout(ops());
    return layout.empty() ? kTitleH : layout.back().second.bottom();
}

gfx::Rect ColumnFilterPopup::popupRect() const {
    // Du haut vers le bas : le titre, les conditions, la valeur, la liste
    // (sa recherche, huit lignes, une note), les boutons.
    const float fieldY = chipsBottom() + 10.f;
    const float listY = fieldY + kFieldH + 16.f;
    const float rowsY = listY + kFieldH + 6.f;
    const float noteY = rowsY + static_cast<float>(visibleRows()) * kRowH + 4.f;
    const float h = noteY + 20.f + 8.f + kButtonH + kPad;
    const auto a = spec_.anchor;
    float x = a.x, y = a.bottom() + 2.f;
    const auto surface = surfaceSize();
    if (surface.w > 0.f && x + kWidth > surface.w - 4.f) x = std::max(4.f, surface.w - kWidth - 4.f);
    if (surface.h > 0.f && y + h > surface.h - 4.f) y = std::max(4.f, std::min(a.y - h - 2.f, surface.h - h - 4.f));
    return {x, y, kWidth, h};
}

std::vector<ColumnFilterPopup::Chip> ColumnFilterPopup::chips() const {
    std::vector<Chip> out;
    const auto p = popupRect();
    for (const auto& [op, r] : chipLayout(ops())) out.push_back({op, {p.x + r.x, p.y + r.y, r.w, r.h}});
    return out;
}

gfx::Rect ColumnFilterPopup::opRect(ColumnFilter::Op op) const {
    for (const auto& c : chips())
        if (c.op == op) return c.rect;
    return {};
}

gfx::Rect ColumnFilterPopup::fieldRect(int field) const {
    const auto p = popupRect();
    const float fieldY = p.y + chipsBottom() + 10.f;
    if (field == 2) return {p.right() - kPad - kSearchW, fieldY + kFieldH + 16.f, kSearchW, kFieldH};
    if (!needsValue()) return {};
    const float w = kWidth - 2.f * kPad;
    if (op_ == ColumnFilter::Op::Between) {
        const float half = (w - 34.f) * 0.5f;
        return field == 0 ? gfx::Rect{p.x + kPad, fieldY, half, kFieldH} : gfx::Rect{p.x + kPad + half + 34.f, fieldY, half, kFieldH};
    }
    return field == 0 ? gfx::Rect{p.x + kPad, fieldY, w, kFieldH} : gfx::Rect{};
}

gfx::Rect ColumnFilterPopup::listRect() const {
    const auto s = fieldRect(2);
    const auto p = popupRect();
    return {p.x + kPad, s.bottom() + 6.f, kWidth - 2.f * kPad, static_cast<float>(visibleRows()) * kRowH};
}

gfx::Rect ColumnFilterPopup::listRowRect(std::size_t shownIndex) const {
    const auto l = listRect();
    const float y = l.y + (static_cast<float>(shownIndex) - std::floor(scroll_)) * kRowH;
    if (y < l.y - 0.5f || y + kRowH > l.bottom() + 0.5f) return {};
    return {l.x, y, l.w, kRowH};
}

gfx::Rect ColumnFilterPopup::applyRect() const {
    const auto p = popupRect();
    return {p.right() - kPad - 112.f, p.bottom() - kPad - kButtonH, 112.f, kButtonH};
}

gfx::Rect ColumnFilterPopup::clearRect() const {
    const auto a = applyRect();
    return {a.x - 8.f - 96.f, a.y, 96.f, kButtonH};
}

gfx::Rect ColumnFilterPopup::closeRect() const {
    const auto p = popupRect();
    return {p.right() - 32.f, p.y + 8.f, 22.f, 22.f};
}

gfx::Rect ColumnFilterPopup::eventBounds() const { return open_ ? popupRect() : gfx::Rect{}; }

// ------------------------------------------------------------------- gestes ---
EventResult ColumnFilterPopup::onEvent(const InputEvent& ev) {
    if (!open_) return EventResult::Ignored;
    const auto p = popupRect();
    const auto rowAt = [&](gfx::Point at) -> int {
        const auto l = listRect();
        if (!l.contains(at)) return -1;
        const auto k = static_cast<std::size_t>((at.y - l.y) / kRowH + std::floor(scroll_));
        return k <= shown_.size() ? static_cast<int>(k) : -1;
    };
    const float maxScroll = std::max(0.f, static_cast<float>(shown_.size() + 1) - static_cast<float>(visibleRows()));
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        int row = -1, chip = -1;
        if (p.contains(m->pos)) {
            row = rowAt(m->pos);
            const auto cs = chips();
            for (std::size_t i = 0; i < cs.size(); ++i)
                if (cs[i].rect.contains(m->pos)) chip = static_cast<int>(i);
        }
        if (row != hoverRow_ || chip != hoverChip_) {
            hoverRow_ = row;
            hoverChip_ = chip;
            invalidate();
        }
        return p.contains(m->pos) ? EventResult::Consumed : EventResult::Ignored;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!p.contains(w->pos)) return EventResult::Ignored;
        scroll_ = std::clamp(std::floor(scroll_) - w->dy * 3.f, 0.f, maxScroll);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!p.contains(d->pos)) return EventResult::Ignored;
        if (!focused()) grabFocus();
        if (closeRect().contains(d->pos)) {
            close();
            dismissed->emit();
            return EventResult::Consumed;
        }
        for (const auto& c : chips())
            if (c.rect.contains(d->pos)) {
                setOp(c.op);
                return EventResult::Consumed;
            }
        for (int f = 0; f < 3; ++f)
            if (fieldRect(f).contains(d->pos)) {
                field_ = f;
                replaceOnType_ = false;
                invalidate();
                return EventResult::Consumed;
            }
        if (const int row = rowAt(d->pos); row >= 0) {
            if (row == 0) {
                const bool all = std::all_of(shown_.begin(), shown_.end(), [&](std::size_t i) { return items_[i].checked; });
                setAllShown(!all);
            } else {
                auto& it = items_[shown_[static_cast<std::size_t>(row - 1)]];
                it.checked = !it.checked;
                invalidate();
            }
            return EventResult::Consumed;
        }
        if (applyRect().contains(d->pos)) {
            apply();
            return EventResult::Consumed;
        }
        if (clearRect().contains(d->pos)) {
            clearFilter();
            return EventResult::Consumed;
        }
        return EventResult::Consumed;
    }
    if (const auto* u = std::get_if<MouseUp>(&ev)) return p.contains(u->pos) ? EventResult::Consumed : EventResult::Ignored;
    if (!focused()) return EventResult::Ignored;
    if (const auto* t = std::get_if<TextInput>(&ev)) {
        typeInto(t->utf8);
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        switch (k->key) {
            case Key::Escape:
                close();
                dismissed->emit();
                return EventResult::Consumed;
            case Key::Return:
                apply();
                return EventResult::Consumed;
            case Key::Tab:
                nextField(k->mods.shift);
                return EventResult::Consumed;
            case Key::Backspace:
                backspace();
                return EventResult::Consumed;
            case Key::Delete:
                fieldText(field_).clear();
                replaceOnType_ = false;
                if (field_ == 2) refilter();
                invalidate();
                return EventResult::Consumed;
            case Key::Down:
            case Key::Up:
                scroll_ = std::clamp(std::floor(scroll_) + (k->key == Key::Down ? 1.f : -1.f), 0.f, maxScroll);
                invalidate();
                return EventResult::Consumed;
            case Key::A:
                if (k->mods.ctrl) {
                    replaceOnType_ = !fieldText(field_).empty();
                    invalidate();
                    return EventResult::Consumed;
                }
                break;
            default:
                break;
        }
        // Les raccourcis de l'ecran (Ctrl+K...) passent ; le reste ne va pas a la table.
        return k->mods.ctrl ? EventResult::Ignored : EventResult::Consumed;
    }
    return EventResult::Ignored;
}

// ------------------------------------------------------------------- dessin ---
void ColumnFilterPopup::onPaintOverlay(const PaintContext& ctx) {
    if (!open_) return;
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto p = popupRect();
    r.fillRoundedRect({p.x + 3.f, p.y + 5.f, p.w, p.h}, gfx::Color{0, 0, 0, 90}, 6.f);
    r.fillRoundedRect(p, c.panelBg, 6.f);
    r.strokeRect(p, c.accent, 1.f);

    // Le titre, la croix.
    const float lhText = r.lineHeight(kText), lhSmall = r.lineHeight(kSmall);
    r.drawText({p.x + kPad, p.y + (kTitleH - lhText) * 0.5f}, "Filtrer :", kText, c.textMuted);
    const float tx = p.x + kPad + r.measure("Filtrer :", kText).width + 6.f;
    drawFit(r, {tx, p.y + (kTitleH - lhText) * 0.5f}, spec_.title, kText, c.text, p.right() - 40.f - tx);
    const auto cr = closeRect();
    cross(r, {cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f}, 4.5f, c.textMuted);

    // Les conditions.
    const auto cs = chips();
    for (std::size_t i = 0; i < cs.size(); ++i) {
        const bool on = cs[i].op == op_;
        const bool hot = static_cast<int>(i) == hoverChip_;
        r.fillRoundedRect(cs[i].rect, on ? c.accent.withAlpha(70) : hot ? c.rowAltBg : c.headerBg, kChipH * 0.5f);
        if (on) r.strokeRect(cs[i].rect, c.accent, 1.f);
        r.drawText({cs[i].rect.x + 11.f, cs[i].rect.y + (kChipH - lhSmall) * 0.5f}, ColumnFilter::opLabel(cs[i].op), kSmall,
                   on ? c.text : c.textMuted);
    }

    // Un champ : son texte, le curseur (clignotant) ou tout choisi, l'indication.
    const auto field = [&](int f, std::string_view hint) {
        const auto fr = fieldRect(f);
        if (fr.w <= 0.f) return;
        const auto& text = f == 2 ? listSearch_ : f == 1 ? value2_ : value_;
        const bool has = focused() && field_ == f;
        r.fillRect(fr, c.inputBg);
        r.strokeRect(fr, has ? c.accent : c.border, 1.f);
        const float ty = fr.y + (fr.h - lhText) * 0.5f;
        if (text.empty()) {
            drawFit(r, {fr.x + 7.f, fr.y + (fr.h - lhSmall) * 0.5f}, hint, kSmall, c.textDisabled, fr.w - 12.f);
        } else {
            if (has && replaceOnType_)
                r.fillRect({fr.x + 5.f, fr.y + 4.f, std::min(fr.w - 10.f, r.measure(text, kText).width + 3.f), fr.h - 8.f}, c.selectionBg);
            drawFit(r, {fr.x + 7.f, ty}, text, kText, c.text, fr.w - 12.f);
        }
        if (has && !replaceOnType_ && std::fmod(ctx.time, 1.0) < 0.55) {
            const float cx = std::min(fr.right() - 5.f, fr.x + 7.f + r.measure(text, kText).width + 1.f);
            r.fillRect({cx, fr.y + 5.f, 1.f, fr.h - 10.f}, c.text);
        }
    };
    if (needsValue()) {
        if (op_ == ColumnFilter::Op::Between) {
            field(0, "de");
            field(1, "\xC3\xA0");
            const auto a = fieldRect(0);
            r.drawText({a.right() + 11.f, a.y + (a.h - lhSmall) * 0.5f}, "et", kSmall, c.textMuted);
        } else {
            field(0, "la valeur (sans casse ni accents)");
        }
    } else {
        const float fy = p.y + chipsBottom() + 10.f;
        r.drawText({p.x + kPad, fy + (kFieldH - lhSmall) * 0.5f},
                   op_ == ColumnFilter::Op::Empty ? "Les cases sans valeur (ou un tiret)." : "Les cases qui ont une valeur.", kSmall, c.textMuted);
    }

    // La liste des valeurs.
    const auto sr = fieldRect(2);
    r.line({p.x + 1.f, sr.y - 8.f}, {p.right() - 1.f, sr.y - 8.f}, c.border, 1.f);
    std::size_t checked = 0;
    for (const auto& it : items_) checked += it.checked ? 1u : 0u;
    r.drawText({p.x + kPad, sr.y + (sr.h - lhSmall) * 0.5f},
               "Valeurs  " + thousands(checked) + "/" + thousands(items_.size()), kSmall, c.textMuted);
    field(2, "chercher dans la liste");
    const auto l = listRect();
    r.fillRect(l, c.inputBg);
    r.strokeRect(l, c.border, 1.f);
    r.pushClip(l);
    const auto first = static_cast<std::size_t>(std::floor(scroll_));
    for (std::size_t k = first; k <= shown_.size() && k < first + static_cast<std::size_t>(visibleRows()); ++k) {
        const auto rr = listRowRect(k);
        if (rr.w <= 0.f) continue;
        if (static_cast<int>(k) == hoverRow_) r.fillRect(rr, ctx.theme.brand.hover);
        const gfx::Rect box{rr.x + 6.f, rr.y + (kRowH - 14.f) * 0.5f, 14.f, 14.f};
        const float ty = rr.y + (kRowH - lhSmall) * 0.5f;
        if (k == 0) {
            std::size_t on = 0;
            std::size_t total = 0;
            for (const auto i : shown_) {
                on += items_[i].checked ? 1u : 0u;
                total += items_[i].count;
            }
            checkbox(r, ctx.theme, box, on == shown_.size() && !shown_.empty(), on > 0 && on < shown_.size());
            r.drawText({box.right() + 8.f, ty}, listSearch_.empty() ? "(Tout)" : "(Toutes celles trouv\xC3\xA9" "es)", kSmall, c.text);
            const std::string n = thousands(total);
            r.drawText({rr.right() - 8.f - r.measure(n, kSmall).width, ty}, n, kSmall, c.textMuted);
            continue;
        }
        const auto& it = items_[shown_[k - 1]];
        checkbox(r, ctx.theme, box, it.checked, false);
        const std::string n = thousands(it.count);
        const float nw = r.measure(n, kSmall).width;
        drawFit(r, {box.right() + 8.f, ty}, shownValue(it.value), kSmall, blankCell(it.value) ? c.textMuted : c.text,
                rr.right() - 16.f - nw - box.right() - 8.f);
        r.drawText({rr.right() - 8.f - nw, ty}, n, kSmall, c.textMuted);
    }
    r.popClip();
    std::string note;
    if (shown_.empty() && !listSearch_.empty()) note = "Aucune valeur ne contient \xC2\xAB " + listSearch_ + " \xC2\xBB.";
    else if (spec_.more || shown_.size() >= kMaxShown)
        note = "Les " + std::to_string(shown_.size()) + " premi\xC3\xA8res valeurs : cherche pour affiner.";
    else if (spec_.values.empty()) note = "Aucune valeur dans les lignes montr\xC3\xA9" "es.";
    if (!note.empty()) drawFit(r, {l.x, l.bottom() + 4.f}, note, kSmall, c.textMuted, l.w);

    // Les boutons.
    const auto clr = clearRect(), app = applyRect();
    r.fillRoundedRect(clr, c.headerBg, 4.f);
    r.strokeRect(clr, c.border, 1.f);
    const auto clw = r.measure("Effacer", kText).width;
    r.drawText({clr.x + (clr.w - clw) * 0.5f, clr.y + (clr.h - lhText) * 0.5f}, "Effacer", kText, c.text);
    r.fillRoundedRect(app, c.accent, 4.f);
    const auto apw = r.measure("Appliquer", kText).width;
    r.drawText({app.x + (app.w - apw) * 0.5f, app.y + (app.h - lhText) * 0.5f}, "Appliquer", kText, c.selectionText);
}

// =================================================== TableView : la recherche ===
void TableView::setHighlight(std::string_view query) {
    if (query == highlight_.text()) return;
    highlight_ = SearchQuery(query);
    invalidate();
}

void TableView::paintHighlights(const PaintContext& ctx, std::string_view text, float x, float y, float h, float maxWidth,
                                gfx::FontId font) const {
    if (highlight_.empty() || text.empty() || maxWidth <= 0.f) return;
    const auto ranges = highlight_.ranges(text);
    if (ranges.empty()) return;
    const gfx::Color mark{230, 180, 60, 110};
    for (const auto& [a, b] : ranges) {
        const float x0 = x + ctx.r.measure(text.substr(0, a), font).width;
        if (x0 >= x + maxWidth) break;
        const float x1 = std::min(x + maxWidth, x + ctx.r.measure(text.substr(0, b), font).width);
        if (x1 > x0) ctx.r.fillRoundedRect({x0 - 1.f, y + 3.f, x1 - x0 + 2.f, h - 6.f}, mark, 2.f);
    }
}

// ============================================== TableView : les filtres des colonnes ===
float TableView::stripHeight() const noexcept { return columnFilters_.empty() ? 0.f : kStripH; }

float TableView::headerStripTop() const noexcept { return contentRect().y + bannerHeight(); }

void TableView::setColumnFiltersEnabled(bool on) {
    if (filtersEnabled_ == on) return;
    filtersEnabled_ = on;
    if (!on && filterPopup_) filterPopup_->close();
    // Lot API 8 : allumee, la table relit ce qu'elle retient (ses colonnes deja
    // posees : tout de suite ; sinon a setColumns).
    if (on) recallColumnFilters();
    invalidate();
}

void TableView::setColumnFilterMode(ColumnFilterMode mode) {
    if (filterMode_ == mode) return;
    filterMode_ = mode;
    rebuildView();
}

const ColumnFilter* TableView::columnFilter(std::size_t column) const noexcept {
    for (const auto& f : columnFilters_)
        if (f.column == column) return &f;
    return nullptr;
}

void TableView::setColumnFilter(ColumnFilter filter) {
    if (!filter.active()) {
        removeColumnFilter(filter.column);
        return;
    }
    for (auto& f : columnFilters_)
        if (f.column == filter.column) {
            f = std::move(filter);
            columnFiltersEdited();
            return;
        }
    columnFilters_.push_back(std::move(filter));
    columnFiltersEdited();
}

void TableView::removeColumnFilter(std::size_t column) {
    const auto before = columnFilters_.size();
    std::erase_if(columnFilters_, [&](const ColumnFilter& f) { return f.column == column; });
    if (columnFilters_.size() != before) columnFiltersEdited();
}

void TableView::clearColumnFilters() {
    if (columnFilters_.empty()) return;
    columnFilters_.clear();
    columnFiltersEdited();
}

void TableView::columnFiltersEdited() {
    headerHeight_ = titleHeight_ + bannerHeight() + stripHeight();
    if (filterMode_ == ColumnFilterMode::Table) rebuildView();
    else invalidate();
    if (!memoryRestoring_) rememberColumnFilters();     // lot API 8 : retenus (vides : oublies)
    columnFiltersChanged->emit();
}

bool TableView::columnFiltersAccept(RowIndex modelRow, int skipColumn) const {
    if (!model_ || columnFilters_.empty()) return true;
    return ColumnFilter::acceptsAll(columnFilters_, [&](std::size_t c) { return model_->cellText(modelRow, c); }, skipColumn);
}

void TableView::setColumnFilterCounts(std::size_t shown, std::size_t total) {
    if (shown == filterShown_ && total == filterTotal_) return;
    filterShown_ = shown;
    filterTotal_ = total;
    invalidate();
}

std::vector<RowIndex> TableView::applyColumnFilters(const std::vector<RowIndex>& rows, std::size_t& shown,
                                                    std::size_t& total) const {
    std::vector<RowIndex> out;
    shown = total = 0;
    if (!model_) return out;
    out.reserve(rows.size());
    const std::size_t first = firstVisibleColumn();
    // Un titre de groupe (spanRow) attend sa premiere ligne gardee ; les lignes
    // du premier niveau de son groupe (le plus petit retrait) sont jugees, les
    // lignes plus en retrait suivent la leur.
    bool pendingHeader = false, haveBase = false, rootKept = false;
    RowIndex header = 0;
    float base = 0.f;
    for (const RowIndex r : rows) {
        const auto st = model_->cellStyle(r, first);
        if (st.spanRow) {
            // Replie, il reste : ses lignes ne sont pas la pour le justifier.
            if (st.expander == 0) {
                out.push_back(r);
                pendingHeader = false;
            } else {
                pendingHeader = true;
                header = r;
            }
            haveBase = false;
            rootKept = false;
            continue;
        }
        if (!haveBase || st.indent <= base + 0.5f) {
            if (!haveBase || st.indent < base) base = st.indent;
            haveBase = true;
            ++total;
            rootKept = columnFiltersAccept(r);
            if (!rootKept) continue;
            ++shown;
            if (pendingHeader) {
                out.push_back(header);
                pendingHeader = false;
            }
            out.push_back(r);
        } else if (rootKept) {
            out.push_back(r);
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::size_t>> TableView::columnValues(std::size_t column, bool* more) const {
    std::map<std::string, std::size_t> counts;
    const auto add = [&](const std::string& s) { ++counts[s]; };
    if (valuesProvider_) {
        valuesProvider_(column, add);
    } else if (model_ && column < model_->columnCount()) {
        const auto rows = filter_.apply(*model_);
        const std::size_t first = firstVisibleColumn();
        bool haveBase = false;
        float base = 0.f;
        for (const RowIndex r : rows) {
            const auto st = model_->cellStyle(r, first);
            if (st.spanRow) {
                haveBase = false;
                continue;
            }
            if (haveBase && st.indent > base + 0.5f) continue;          // un enfant : sa ligne de tete compte
            if (!haveBase || st.indent < base) base = st.indent;
            haveBase = true;
            if (columnFiltersAccept(r, static_cast<int>(column))) add(model_->cellText(r, column));
        }
    }
    std::vector<std::pair<std::string, std::size_t>> out(counts.begin(), counts.end());
    // Des nombres : dans l'ordre des nombres ; sinon sans casse ni accents. Les
    // cases vides d'abord.
    bool numeric = false, allNumeric = true;
    for (const auto& v : out) {
        if (blankCell(v.first)) continue;
        double x = 0.0;
        if (ColumnFilter::parseNumber(v.first, x)) numeric = true;
        else allNumeric = false;
    }
    numeric = numeric && allNumeric;
    std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
        const bool ba = blankCell(a.first), bb = blankCell(b.first);
        if (ba != bb) return ba;
        if (numeric && !ba) {
            double x = 0.0, y = 0.0;
            (void)ColumnFilter::parseNumber(a.first, x);
            (void)ColumnFilter::parseNumber(b.first, y);
            if (x != y) return x < y;
        }
        const auto fa = foldForSearch(a.first), fb = foldForSearch(b.first);
        return fa != fb ? fa < fb : a.first < b.first;
    });
    if (more) *more = out.size() > kMaxValues;
    if (out.size() > kMaxValues) out.resize(kMaxValues);
    return out;
}

gfx::Rect TableView::headerCellRect(std::size_t column) const {
    if (column >= columns_.size() || !columns_[column].visible) return {};
    const auto area = contentRect();
    float x = area.x - scrollX_;
    for (std::size_t i = 0; i < column; ++i)
        if (columns_[i].visible) x += columns_[i].width;
    return {x, area.y + bannerHeight() + stripHeight(), columns_[column].width, titleHeight_};
}

gfx::Rect TableView::filterIconRect(std::size_t column) const {
    if (!filtersEnabled_ || column >= columns_.size() || !columns_[column].filterable || columns_[column].headerIcon != 0) return {};
    const auto cell = headerCellRect(column);
    if (cell.w < 44.f) return {};
    return {cell.right() - 21.f, cell.y + (cell.h - 14.f) * 0.5f, 14.f, 14.f};
}

int TableView::columnByTitle(std::string_view title) const {
    const std::string want = foldForSearch(trimView(title));
    if (want.empty()) return -1;
    const auto titleOf = [&](std::size_t i) {
        return columns_[i].title.empty() && model_ && i < model_->columnCount() ? model_->headerText(i) : columns_[i].title;
    };
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (foldForSearch(titleOf(i)) == want) return static_cast<int>(i);
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (foldForSearch(titleOf(i)).rfind(want, 0) == 0) return static_cast<int>(i);
    return -1;
}

bool TableView::openColumnFilter(std::size_t column) {
    if (!filterPopup_ || !model_ || column >= columns_.size() || column >= model_->columnCount()) return false;
    ColumnFilterPopup::Spec spec;
    spec.column = column;
    spec.title = columns_[column].title.empty() ? model_->headerText(column) : columns_[column].title;
    if (const auto* f = columnFilter(column)) spec.current = *f;
    spec.current.column = column;
    spec.values = columnValues(column, &spec.more);
    bool any = false, all = true;
    for (const auto& v : spec.values) {
        if (blankCell(v.first)) continue;
        double x = 0.0;
        if (ColumnFilter::parseNumber(v.first, x)) any = true;
        else all = false;
    }
    spec.numeric = any && all;
    spec.anchor = headerCellRect(column);
    if (spec.anchor.w <= 0.f) spec.anchor = {contentRect().x, contentRect().y, 240.f, titleHeight_};
    filterPopup_->open(std::move(spec));
    return true;
}

// ---- la bande des filtres ----------------------------------------------------------
namespace {
struct StripItem { gfx::Rect rect; int kind; std::size_t index; std::string text; };

std::vector<StripItem> stripItems(const gfx::Rect& band, const std::vector<std::string>& labels, const std::string& count) {
    std::vector<StripItem> out;
    float x = band.x + 8.f;
    const float y = band.y + 3.f, h = band.h - 6.f;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const float w = measureWidth(labels[i], kSmall) + 40.f;
        const gfx::Rect chip{x, y, w, h};
        out.push_back({chip, 0, i, labels[i]});
        out.push_back({{chip.right() - 22.f, y, 22.f, h}, 1, i, {}});
        x += w + 6.f;
    }
    const std::string add = "+ Filtre";
    out.push_back({{x, y, measureWidth(add, kSmall) + 20.f, h}, 2, 0, add});
    const std::string clear = "Tout effacer";
    const float cw = measureWidth(clear, kSmall) + 16.f;
    out.push_back({{band.right() - 8.f - cw, y, cw, h}, 3, 0, clear});
    const float nw = measureWidth(count, kSmall);
    out.push_back({{band.right() - 8.f - cw - 12.f - nw, y, nw, h}, 4, 0, count});
    return out;
}
} // namespace

gfx::Rect TableView::filterChipRect(std::size_t index) const {
    if (columnFilters_.empty() || index >= columnFilters_.size()) return {};
    const auto area = contentRect();
    float x = area.x + 8.f;
    for (std::size_t i = 0; i < columnFilters_.size(); ++i) {
        const auto& f = columnFilters_[i];
        const std::string title = f.column < columns_.size() ? columns_[f.column].title : std::string{};
        const float w = measureWidth(f.label(title), kSmall) + 40.f;
        if (i == index) return {x, headerStripTop() + 3.f, w, kStripH - 6.f};
        x += w + 6.f;
    }
    return {};
}

gfx::Rect TableView::addFilterRect() const {
    if (columnFilters_.empty()) return {};
    const auto last = filterChipRect(columnFilters_.size() - 1);
    return {last.right() + 6.f, last.y, measureWidth("+ Filtre", kSmall) + 20.f, last.h};
}

void TableView::paintFilterStrip(const PaintContext& ctx, const gfx::Rect& band) const {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(band, c.panelBg);
    r.fillRect({band.x, band.bottom() - 1.f, band.w, 1.f}, c.border);
    std::vector<std::string> labels;
    for (const auto& f : columnFilters_)
        labels.push_back(f.label(f.column < columns_.size() ? columns_[f.column].title : std::string{}));
    std::size_t shown = filterShown_, total = filterTotal_;
    if (filterMode_ == ColumnFilterMode::Table && total == 0) shown = total = view_.size();
    const std::string count = thousands(shown) + " sur " + thousands(total);
    const float lh = r.lineHeight(kSmall);
    r.pushClip(band);
    for (const auto& it : stripItems(band, labels, count)) {
        const float ty = it.rect.y + (it.rect.h - lh) * 0.5f;
        switch (it.kind) {
            case 0:
                r.fillRoundedRect(it.rect, c.accent.withAlpha(50), it.rect.h * 0.5f);
                r.strokeRect(it.rect, c.accent.withAlpha(150), 1.f);
                drawIcon(r, Icon::Filter, {it.rect.x + 8.f, it.rect.y + (it.rect.h - 12.f) * 0.5f, 12.f, 12.f}, c.accent);
                r.drawText({it.rect.x + 24.f, ty}, it.text, kSmall, c.text);
                break;
            case 1:
                cross(r, {it.rect.x + it.rect.w * 0.5f - 2.f, it.rect.y + it.rect.h * 0.5f}, 3.5f, c.textMuted);
                break;
            case 2:
                r.strokeRect(it.rect, c.border, 1.f);
                r.drawText({it.rect.x + 10.f, ty}, it.text, kSmall, c.textMuted);
                break;
            case 3:
                r.drawText({it.rect.x + 8.f, ty}, it.text, kSmall, c.accent);
                break;
            default:
                r.drawText({it.rect.x, ty}, it.text, kSmall, c.textMuted);
                break;
        }
    }
    r.popClip();
}

bool TableView::filterStripClick(gfx::Point p) {
    const float top = headerStripTop();
    if (stripHeight() <= 0.f || p.y < top || p.y >= top + stripHeight()) return false;
    const auto area = contentRect();
    std::vector<std::string> labels;
    for (const auto& f : columnFilters_)
        labels.push_back(f.label(f.column < columns_.size() ? columns_[f.column].title : std::string{}));
    const auto items = stripItems({area.x, top, area.w, kStripH}, labels, std::string{});
    // La croix d'abord : elle est dans sa pastille.
    for (const auto& it : items)
        if (it.kind == 1 && it.rect.contains(p) && it.index < columnFilters_.size()) {
            removeColumnFilter(columnFilters_[it.index].column);
            return true;
        }
    for (const auto& it : items) {
        if (!it.rect.contains(p)) continue;
        if (it.kind == 0 && it.index < columnFilters_.size()) {
            (void)openColumnFilter(columnFilters_[it.index].column);
            return true;
        }
        if (it.kind == 2) {
            openAddFilterMenu({it.rect.x, it.rect.bottom() + 2.f});
            return true;
        }
        if (it.kind == 3) {
            clearColumnFilters();
            return true;
        }
    }
    return true;
}

void TableView::openAddFilterMenu(gfx::Point at) {
    if (!context_) return;
    std::vector<PopupMenu::Item> items;
    items.push_back({"Filtrer la colonne\xE2\x80\xA6", {}, {}, Icon::None, true, false, -1, true});
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible || !columns_[i].filterable || columns_[i].headerIcon != 0) continue;
        if (model_ && i >= model_->columnCount()) continue;
        const std::string title = columns_[i].title.empty() && model_ ? model_->headerText(i) : columns_[i].title;
        items.push_back({title, columnFilter(i) ? std::string("filtr\xC3\xA9" "e") : std::string{}, {}, Icon::Filter, true, false,
                         static_cast<int>(100 + i)});
    }
    context_->setItems(std::move(items));
    gfx::Size surface{bounds().right() + 400.f, bounds().bottom() + 400.f};
    if (surface_.w > 0.f) surface = surface_;
    context_->openAt(at, surface);
}

} // namespace ui
