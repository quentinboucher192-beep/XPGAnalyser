// =============================================================================
//  app/MacroFormView.cpp - le formulaire d'une macro (lot macros 1)
// =============================================================================
#include "MacroFormView.hpp"

#include "ClipboardFiles.hpp"

#include "../ui/Icons.hpp"
#include "../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace app {

namespace mm = project::macro;
using mm::FieldKind;

namespace {

const gfx::FontId kLabel{16};
const gfx::FontId kHelp{13};
const gfx::FontId kHint{13};
const gfx::FontId kGroup{17};

std::string lower(std::string_view s) {
    std::string out = mm::foldAccents(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> wrap(std::string_view text, gfx::FontId font, float width) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string_view::npos) nl = text.size();
        const auto para = text.substr(from, nl - from);
        from = nl + 1;
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            auto sp = para.find(' ', i);
            if (sp == std::string_view::npos) sp = para.size();
            const auto word = para.substr(i, sp - i);
            const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && ui::measureWidth(candidate, font) > width) {
                out.push_back(line);
                line = std::string(word);
            } else {
                line = candidate;
            }
            i = sp + 1;
        }
        if (!line.empty() || para.empty()) out.push_back(line);
        if (nl == text.size()) break;
    }
    return out;
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto at = s.find(',', from);
        auto part = std::string(s.substr(from, (at == std::string_view::npos ? s.size() : at) - from));
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
        if (!part.empty()) out.push_back(std::move(part));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    return out;
}

// "1" -> 1, "1.0" -> 1 : les valeurs d'une colonne de classeur se lisent en REAL.
std::string numberText(const std::string& v) {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end && *end == '\0' && !v.empty() && std::fabs(d - std::round(d)) < 1e-9) return std::to_string(static_cast<long long>(std::llround(d)));
    return v;
}

gfx::Color withAlpha(gfx::Color c, float a) {
    c.a = static_cast<std::uint8_t>(std::clamp(a, 0.f, 1.f) * static_cast<float>(c.a));
    return c;
}

} // namespace

// ============================================================ SwitchToggle ====
namespace macroui {

SwitchToggle::SwitchToggle(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

void SwitchToggle::setOn(bool on) {
    if (on == on_) return;
    on_ = on;
    animating_ = true;
    animStart_ = -1.0;
    invalidate();
}

ui::SizeHint SwitchToggle::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {90.f, 26.f};
    h.minimum = {90.f, 26.f};
    return h;
}

void SwitchToggle::clickForTest() {
    setOn(!on_);
    toggled->emit(on_);
}

void SwitchToggle::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    if (animating_ && animStart_ < 0.0) animStart_ = ctx.time;
    float t = 1.f;
    if (animating_) {
        t = static_cast<float>((ctx.time - animStart_) * 1000.0 / std::max(1.f, ctx.theme.motion.hoverMs * 1.5f));
        if (t >= 1.f) {
            t = 1.f;
            animating_ = false;
        } else {
            invalidate();
        }
    }
    const float k = on_ ? t : 1.f - t;   // 0 : a gauche (non), 1 : a droite (oui)
    const gfx::Rect pill{b.x, b.y + (b.h - 22.f) * 0.5f, 42.f, 22.f};
    const gfx::Color off = c.borderStrong;
    const gfx::Color onC = c.accent;
    const gfx::Color fill{static_cast<std::uint8_t>(off.r + (onC.r - off.r) * k), static_cast<std::uint8_t>(off.g + (onC.g - off.g) * k),
                          static_cast<std::uint8_t>(off.b + (onC.b - off.b) * k), 255};
    ctx.r.fillRoundedRect(pill, fill, 11.f);
    if (focused()) ctx.r.strokeRect({pill.x - 2.f, pill.y - 2.f, pill.w + 4.f, pill.h + 4.f}, ctx.theme.brand.focusRing, 1.f);
    const float knobX = pill.x + 3.f + k * 20.f;
    ctx.r.fillRoundedRect({knobX, pill.y + 3.f, 16.f, 16.f}, gfx::Color{255, 255, 255, 255}, 8.f);
    ctx.r.drawText({pill.x + pill.w + 10.f, b.y + (b.h - 16.f) * 0.5f}, on_ ? "oui" : "non", kLabel, on_ ? c.text : c.textMuted);
}

ui::EventResult SwitchToggle::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        grabFocus();
        setOn(!on_);
        toggled->emit(on_);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && (k->key == ui::Key::Space || k->key == ui::Key::Return)) {
        setOn(!on_);
        toggled->emit(on_);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// =============================================================== Segmented ====
Segmented::Segmented(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

void Segmented::setOptions(std::vector<std::string> labels) {
    labels_ = std::move(labels);
    if (selected_ >= static_cast<int>(labels_.size())) selected_ = -1;
    invalidateLayout();
    invalidate();
}

void Segmented::setSelected(int index) {
    if (index == selected_) return;
    selected_ = index;
    invalidate();
}

float Segmented::preferredWidth() const {
    float w = 0.f;
    for (const auto& l : labels_) w += ui::measureWidth(l, kLabel) + 24.f;
    return w + 2.f;
}

ui::SizeHint Segmented::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {preferredWidth(), 32.f};
    h.minimum = h.preferred;
    return h;
}

gfx::Rect Segmented::optionRect(std::size_t index) const {
    const auto b = bounds();
    float x = b.x + 1.f;
    const float natural = preferredWidth();
    const float scale = natural > b.w && natural > 0.f ? b.w / natural : 1.f;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const float w = (ui::measureWidth(labels_[i], kLabel) + 24.f) * scale;
        if (i == index) return {x, b.y + 1.f, w, b.h - 2.f};
        x += w;
    }
    return {};
}

int Segmented::optionAt(gfx::Point p) const {
    for (std::size_t i = 0; i < labels_.size(); ++i)
        if (optionRect(i).contains(p)) return static_cast<int>(i);
    return -1;
}

void Segmented::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(b, c.border, 6.f);
    ctx.r.fillRoundedRect({b.x + 1.f, b.y + 1.f, b.w - 2.f, b.h - 2.f}, c.inputBg, 5.f);
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const auto r = optionRect(i);
        const bool on = static_cast<int>(i) == selected_;
        if (on) ctx.r.fillRoundedRect({r.x + 2.f, r.y + 2.f, r.w - 4.f, r.h - 4.f}, c.accent, 4.f);
        else if (static_cast<int>(i) == hover_) ctx.r.fillRoundedRect({r.x + 2.f, r.y + 2.f, r.w - 4.f, r.h - 4.f}, ctx.theme.brand.hover, 4.f);
        if (i > 0 && !on && static_cast<int>(i) - 1 != selected_)
            ctx.r.line({r.x, r.y + 6.f}, {r.x, r.y + r.h - 6.f}, c.border, 1.f);
        const float tw = ui::measureWidth(labels_[i], kLabel);
        ctx.r.drawText({r.x + (r.w - tw) * 0.5f, r.y + (r.h - 17.f) * 0.5f}, labels_[i], kLabel,
                       on ? c.textInverted : c.text);
    }
    if (focused()) ctx.r.strokeRect({b.x - 2.f, b.y - 2.f, b.w + 4.f, b.h + 4.f}, ctx.theme.brand.focusRing, 1.f);
}

ui::EventResult Segmented::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = bounds().contains(m->pos) ? optionAt(m->pos) : -1;
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
        grabFocus();
        const int i = optionAt(d->pos);
        if (i >= 0 && i != selected_) {
            selected_ = i;
            invalidate();
            selectionChanged->emit(i);
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !labels_.empty()) {
        int i = selected_;
        if (k->key == ui::Key::Left) i = std::max(0, i - 1);
        else if (k->key == ui::Key::Right) i = std::min(static_cast<int>(labels_.size()) - 1, i + 1);
        else return ui::EventResult::Ignored;
        if (i != selected_) {
            selected_ = i;
            invalidate();
            selectionChanged->emit(i);
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// =============================================================== FileInput ====
FileInput::FileInput(std::string id) : ui::InputText(std::move(id)) {}

ui::EventResult FileInput::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && k->key == ui::Key::V && k->mods.ctrl) {
        // UN FICHIER COPIE DANS L'EXPLORATEUR n'est pas du texte : le presse-
        // papiers en porte la liste (CF_HDROP). Le premier gagne.
        const auto files = ui::clipboardFiles();
        if (!files.empty()) {
            setText(files.front());
            fileChosen->emit(files.front());
            return ui::EventResult::Consumed;
        }
        // Du texte : un chemin ("Copier en tant que chemin d'acces" le met entre
        // guillemets), ou une adresse file://.
        const auto pasted = cleanPastedPath(ui::clipboardText());
        if (!pasted.empty()) {
            setText(pasted);
            fileChosen->emit(pasted);
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev); drop && bounds().contains(drop->pos)) {
        setText(drop->path);
        fileChosen->emit(drop->path);
        return ui::EventResult::Consumed;
    }
    return ui::InputText::onEvent(ev);
}

} // namespace macroui

// ================================================================= la ligne ====
struct MacroFormView::Row {
    FormField field;
    std::string hint;
    ui::Tone hintTone{ui::Tone::Muted};
    std::string example;
    std::vector<std::string> helpLines, labelLines;
    // Les widgets (enfants du formulaire).
    ui::InputText* input{nullptr};
    macroui::FileInput* file{nullptr};
    ui::Button* browse{nullptr};
    std::vector<ui::Button*> chips;             // fichiers recents, valeurs rapides
    std::vector<std::string> chipValues;
    macroui::SwitchToggle* toggle{nullptr};
    macroui::Segmented* segments{nullptr};
    std::vector<std::string> segmentValues;
    ui::DropDown* drop{nullptr};
    ui::MultiLineText* lines{nullptr};
    std::vector<ui::Checkbox*> checks;
    std::vector<std::string> checkValues;
    std::string builtKind;                       // de quoi les widgets ont ete faits
    // La mise en page.
    float y{0.f}, h{0.f};
    float controlH{0.f};
    double appeared{-1.0};
    core::ConnectionScope links;

    [[nodiscard]] std::vector<ui::Widget*> widgets() const {
        std::vector<ui::Widget*> out;
        for (ui::Widget* w : {static_cast<ui::Widget*>(input), static_cast<ui::Widget*>(file), static_cast<ui::Widget*>(browse),
                              static_cast<ui::Widget*>(toggle), static_cast<ui::Widget*>(segments), static_cast<ui::Widget*>(drop),
                              static_cast<ui::Widget*>(lines)})
            if (w) out.push_back(w);
        for (auto* c : chips) out.push_back(c);
        for (auto* c : checks) out.push_back(c);
        return out;
    }
};

MacroFormView::MacroFormView(std::string id) : ui::Widget(std::move(id)) {}
MacroFormView::~MacroFormView() = default;

MacroFormView::Row* MacroFormView::rowOf(const std::string& key) const {
    for (const auto& r : rows_)
        if (r->field.key == key) return r.get();
    return nullptr;
}

std::vector<std::string> MacroFormView::shownKeys() const {
    std::vector<std::string> out;
    for (const auto* r : ordered()) out.push_back(r->field.key);
    return out;
}

ui::Widget* MacroFormView::controlOf(const std::string& key) const {
    const auto* r = rowOf(key);
    if (!r) return nullptr;
    if (r->file) return r->file;
    if (r->input) return r->input;
    if (r->toggle) return r->toggle;
    if (r->segments) return r->segments;
    if (r->drop) return r->drop;
    if (r->lines) return r->lines;
    return r->checks.empty() ? nullptr : r->checks.front();
}

std::string MacroFormView::valueOf(const std::string& key) const {
    const auto* r = rowOf(key);
    return r ? r->field.value : std::string{};
}

std::string MacroFormView::hintOf(const std::string& key) const {
    const auto* r = rowOf(key);
    return r ? r->hint : std::string{};
}

// L'ordre de lecture : sans groupe, puis les groupes, puis les reglages avances.
std::vector<MacroFormView::Row*> MacroFormView::ordered() const {
    std::vector<Row*> out;
    for (const auto& r : rows_)
        if (!r->field.advanced && r->field.group.empty()) out.push_back(r.get());
    for (const auto& g : groupOrder_) {
        // Lot API 6 : dans le groupe, l'ordre de sa ligne "#! groupe" d'abord.
        const auto spec = std::find_if(groupKeys_.begin(), groupKeys_.end(), [&](const project::macro::GroupSpec& x) { return x.name == g; });
        std::vector<Row*> inGroup;
        for (const auto& r : rows_)
            if (!r->field.advanced && r->field.group == g) inGroup.push_back(r.get());
        if (spec != groupKeys_.end()) {
            const auto rank = [&](const Row* r) {
                for (std::size_t i = 0; i < spec->keys.size(); ++i)
                    if (spec->keys[i] == r->field.key) return i;
                return spec->keys.size();
            };
            std::stable_sort(inGroup.begin(), inGroup.end(), [&](const Row* a, const Row* b) { return rank(a) < rank(b); });
        }
        out.insert(out.end(), inGroup.begin(), inGroup.end());
    }
    // Un groupe que la liste ne connait pas (ne devrait pas arriver).
    for (const auto& r : rows_)
        if (!r->field.advanced && !r->field.group.empty()
            && std::find(groupOrder_.begin(), groupOrder_.end(), r->field.group) == groupOrder_.end())
            out.push_back(r.get());
    for (const auto& r : rows_)
        if (r->field.advanced) out.push_back(r.get());
    return out;
}

// ------------------------------------------------------------------ show ----
void MacroFormView::show(const std::vector<FormField>& fields, double now) {
    now_ = now;
    filling_ = true;
    const bool first = rows_.empty();
    // 1. Ce qui disparait.
    for (auto it = rows_.begin(); it != rows_.end();) {
        const bool kept = std::any_of(fields.begin(), fields.end(), [&](const FormField& f) { return f.key == (*it)->field.key; });
        if (kept) {
            ++it;
            continue;
        }
        for (auto* w : (*it)->widgets()) (void)removeChild(*w);
        it = rows_.erase(it);
    }
    // 2. Ce qui reste, et ce qui arrive - dans l'ordre de la macro.
    std::vector<std::unique_ptr<Row>> next;
    for (const auto& f : fields) {
        std::unique_ptr<Row> row;
        for (auto& r : rows_)
            if (r && r->field.key == f.key) row = std::move(r);
        const std::string kind = std::string(mm::kindKey(f.spec.kind)) + (f.table ? ":tableau" : "");
        if (!row) {
            row = std::make_unique<Row>();
            row->appeared = first ? -1.0 : now;
        }
        const bool rebuild = row->builtKind != kind;
        const bool focusedControl = [&] {
            for (auto* w : row->widgets())
                if (w->focused()) return true;
            return false;
        }();
        const std::string previous = row->field.value;
        row->field = f;
        if (rebuild) {
            for (auto* w : row->widgets()) (void)removeChild(*w);
            row->input = nullptr;
            row->file = nullptr;
            row->browse = nullptr;
            row->chips.clear();
            row->chipValues.clear();
            row->toggle = nullptr;
            row->segments = nullptr;
            row->segmentValues.clear();
            row->drop = nullptr;
            row->lines = nullptr;
            row->checks.clear();
            row->checkValues.clear();
            row->links.clear();
            row->builtKind = kind;
            buildControls(*row);
            updateControls(*row, true);
        } else {
            // Un champ en cours de frappe garde ce qu'on y tape.
            if (focusedControl) row->field.value = previous;
            updateControls(*row, !focusedControl);
        }
        computeHint(*row);
        next.push_back(std::move(row));
    }
    rows_ = std::move(next);
    filling_ = false;
    invalidateLayout();
    invalidate();
}

void MacroFormView::emitChange(Row& row, std::string value, bool immediate) {
    if (filling_) return;
    row.field.value = value;
    row.field.origin = FormField::Origin::Typed;
    computeHint(row);
    invalidate();
    const std::string key = row.field.key;    // la ligne peut disparaitre pendant le signal
    changed->emit(key, value, immediate);
}

// -------------------------------------------------------------- les widgets ----
void MacroFormView::buildControls(Row& row) {
    const auto& f = row.field;
    const auto& spec = f.spec;
    const std::string base = id() + "." + f.key;
    Row* r = &row;
    const auto yesNoValues = [&]() -> std::pair<std::string, std::string> {
        std::string yes = "O", no = "N";
        for (const auto& o : spec.options) {
            const auto v = lower(o.value);
            if (v == "o" || v == "oui" || v == "y" || v == "1" || v == "true") yes = o.value;
            if (v == "n" || v == "non" || v == "0" || v == "false") no = o.value;
        }
        return {yes, no};
    };

    switch (spec.kind) {
        case FieldKind::YesNo: {
            auto t = std::make_unique<macroui::SwitchToggle>(base + ".oui");
            row.toggle = &static_cast<macroui::SwitchToggle&>(addChild(std::move(t)));
            const auto [yes, no] = yesNoValues();
            row.links += row.toggle->toggled->connect([this, r, yes = yes, no = no](bool on) { emitChange(*r, on ? yes : no, true); });
            break;
        }
        case FieldKind::Choice:
        case FieldKind::Number: {
            // Des boutons quand les choix sont peu nombreux et tiennent ; une liste sinon.
            std::vector<std::string> labels, values;
            if (spec.kind == FieldKind::Choice) {
                for (const auto& o : spec.options) {
                    values.push_back(o.value);
                    labels.push_back(o.label.empty() ? o.value : o.label);
                }
            } else if (spec.hasRange && !spec.options.empty()
                       && static_cast<double>(spec.options.size()) >= spec.maximum - spec.minimum + 1.0 - 1e-9) {
                for (const auto& o : spec.options) {
                    values.push_back(o.value);
                    labels.push_back(o.value + " " + o.label);
                }
            }
            if (!values.empty()) {
                float width = 0.f;
                for (const auto& l : labels) width += ui::measureWidth(l, kLabel) + 24.f;
                if (values.size() <= 5 && width <= 560.f) {
                    auto s = std::make_unique<macroui::Segmented>(base + ".choix");
                    s->setOptions(labels);
                    row.segments = &static_cast<macroui::Segmented&>(addChild(std::move(s)));
                    row.segmentValues = values;
                    row.links += row.segments->selectionChanged->connect([this, r](int i) {
                        if (i >= 0 && static_cast<std::size_t>(i) < r->segmentValues.size())
                            emitChange(*r, r->segmentValues[static_cast<std::size_t>(i)], true);
                    });
                } else {
                    auto d = std::make_unique<ui::DropDown>(base + ".liste");
                    std::vector<ui::DropDown::Item> items;
                    for (std::size_t i = 0; i < values.size(); ++i)
                        items.push_back({labels[i] == values[i] ? labels[i] : labels[i] + "  (" + values[i] + ")", values[i], {}, true});
                    d->setItems(std::move(items));
                    row.drop = &static_cast<ui::DropDown&>(addChild(std::move(d)));
                    row.links += row.drop->selectionChanged->connect([this, r](int) {
                        if (const auto* it = r->drop->selectedItem()) emitChange(*r, it->value, true);
                    });
                }
                break;
            }
            // Un nombre libre : un champ, et ses valeurs a libelle en raccourcis.
            auto in = std::make_unique<ui::InputText>(base);
            in->setPlaceholder(spec.hasRange ? "un nombre" : "");
            row.input = &static_cast<ui::InputText&>(addChild(std::move(in)));
            row.links += row.input->textChanged->connect([this, r](const std::string& t) { emitChange(*r, t, false); });
            row.links += row.input->editingDone->connect([this, r](const std::string& t) { emitChange(*r, t, true); });
            for (const auto& o : spec.options) {
                auto b = std::make_unique<ui::Button>(o.value + " " + o.label, base + ".raccourci." + o.value);
                b->setStyle(ui::Button::Style::Flat);
                auto* raw = &static_cast<ui::Button&>(addChild(std::move(b)));
                row.chips.push_back(raw);
                row.chipValues.push_back(o.value);
                const std::string v = o.value;
                row.links += raw->clicked->connect([this, r, v] {
                    if (r->input) r->input->setText(v);
                    emitChange(*r, v, true);
                });
            }
            break;
        }
        case FieldKind::SheetChoice: {
            std::vector<std::pair<std::string, std::string>> values;
            for (const auto& o : spec.options) values.emplace_back(o.value, o.label);
            if (hosts_.sheetValues)
                for (auto& [v, l] : hosts_.sheetValues(spec.sheet, spec.valueColumn, spec.labelColumn)) values.emplace_back(numberText(v), l);
            auto d = std::make_unique<ui::DropDown>(base + ".liste");
            std::vector<ui::DropDown::Item> items;
            for (const auto& [v, l] : values) items.push_back({l.empty() || l == v ? v : v + " \xC2\xB7 " + l, v, {}, true});
            d->setItems(std::move(items));
            row.drop = &static_cast<ui::DropDown&>(addChild(std::move(d)));
            row.links += row.drop->selectionChanged->connect([this, r](int) {
                if (const auto* it = r->drop->selectedItem()) emitChange(*r, it->value, true);
            });
            break;
        }
        case FieldKind::File:
        case FieldKind::OutputFile: {
            auto in = std::make_unique<macroui::FileInput>(base);
            row.file = &static_cast<macroui::FileInput&>(addChild(std::move(in)));
            row.input = row.file;
            const bool output = spec.kind == FieldKind::OutputFile;
            row.links += row.file->fileChosen->connect([this, r](const std::string& path) { emitChange(*r, path, true); });
            row.links += row.file->textChanged->connect([this, r](const std::string& t) { emitChange(*r, t, false); });
            row.links += row.file->editingDone->connect([this, r](const std::string& t) { emitChange(*r, cleanPastedPath(t), true); });
            if (!output) {
                auto b = std::make_unique<ui::Button>("\xE2\x80\xA6", base + ".parcourir");
                b->setTooltip("Choisir le fichier dans l'explorateur de fichiers");
                row.browse = &static_cast<ui::Button&>(addChild(std::move(b)));
                const std::string key = f.key;
                row.links += row.browse->clicked->connect([this, key] { (void)browse(key); });
                // Les fichiers recents qui ont la bonne extension.
                if (hosts_.recentFiles) {
                    std::size_t n = 0;
                    for (const auto& path : hosts_.recentFiles()) {
                        const auto ext = lower(std::filesystem::path(path).extension().string());
                        const bool fits = spec.extensions.empty()
                            || std::find(spec.extensions.begin(), spec.extensions.end(), ext) != spec.extensions.end();
                        if (!fits || n == 3) continue;
                        ++n;
                        auto chip = std::make_unique<ui::Button>(std::filesystem::path(path).filename().string(),
                                                                 base + ".recent." + std::to_string(n));
                        chip->setStyle(ui::Button::Style::Flat);
                        chip->setTooltip(path);
                        auto* raw = &static_cast<ui::Button&>(addChild(std::move(chip)));
                        row.chips.push_back(raw);
                        row.chipValues.push_back(path);
                        row.links += raw->clicked->connect([this, r, path] {
                            if (r->file) r->file->setText(path);
                            emitChange(*r, path, true);
                        });
                    }
                }
            }
            break;
        }
        case FieldKind::List: {
            auto m = std::make_unique<ui::MultiLineText>(base + ".lignes");
            m->setReadOnly(false);
            m->setShowLineNumbers(false);
            m->setLanguage(ui::Language::PlainText);
            row.lines = &static_cast<ui::MultiLineText&>(addChild(std::move(m)));
            const std::string separator = spec.membersOf.empty() ? "," : ", ";
            row.links += row.lines->textChanged->connect([this, r, separator](const std::string& t) {
                std::string joined;
                std::size_t from = 0;
                while (from <= t.size()) {
                    auto nl = t.find('\n', from);
                    if (nl == std::string::npos) nl = t.size();
                    std::string line = t.substr(from, nl - from);
                    while (!line.empty() && (std::isspace(static_cast<unsigned char>(line.back())) || line.back() == ',')) line.pop_back();
                    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
                    if (!line.empty()) joined += (joined.empty() ? "" : separator) + line;
                    from = nl + 1;
                }
                emitChange(*r, joined, false);
            });
            // Ce qui se propose pendant la frappe : les sous-routines, les
            // entrees du bloc choisi...
            row.lines->setCompletionProvider([this, r](std::string_view prefix, std::vector<ui::MultiLineText::Completion>& out) {
                const auto& s = r->field.spec;
                const auto want = lower(prefix);
                if (!s.membersOf.empty() && hosts_.members) {
                    std::string type;
                    if (const auto* other = rowOf(s.membersOf)) type = other->field.value;
                    for (const auto& [name, t] : hosts_.members(type)) {
                        if (!want.empty() && lower(name).rfind(want, 0) != 0) continue;
                        ui::MultiLineText::Completion c;
                        c.text = name;
                        c.detail = t;
                        c.insert = name + " := ";
                        c.rank = 10;
                        out.push_back(std::move(c));
                    }
                    return;
                }
                if (!s.source.empty() && hosts_.names) {
                    if (const auto kind = mm::kindFromKey(s.source)) {
                        for (const auto& [name, detail] : hosts_.names(*kind)) {
                            if (!want.empty() && lower(name).find(want) == std::string::npos) continue;
                            ui::MultiLineText::Completion c;
                            c.text = name;
                            c.detail = detail;
                            c.rank = lower(name).rfind(want, 0) == 0 ? 10 : 50;
                            out.push_back(std::move(c));
                        }
                    }
                }
            });
            break;
        }
        case FieldKind::Checks: {
            std::vector<std::pair<std::string, std::string>> items;
            if (spec.source == "retard" && hosts_.outdated) items = hosts_.outdated();
            else if (spec.source == "onglets" && hosts_.sheets)
                for (const auto& s : hosts_.sheets()) items.emplace_back(s, std::string{});
            else
                for (const auto& o : spec.options) items.emplace_back(o.value, o.label == o.value ? std::string{} : o.label);
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& [value, detail] = items[i];
                auto c = std::make_unique<ui::Checkbox>(detail.empty() ? value : value + "   " + detail,
                                                        base + ".case." + std::to_string(i));
                auto* raw = &static_cast<ui::Checkbox&>(addChild(std::move(c)));
                row.checks.push_back(raw);
                row.checkValues.push_back(value);
                row.links += raw->stateChanged->connect([this, r](ui::Checkbox::State) {
                    std::vector<std::string> on;
                    for (std::size_t k = 0; k < r->checks.size(); ++k)
                        if (r->checks[k]->isChecked()) on.push_back(r->checkValues[k]);
                    std::string joined;
                    if (!(r->field.spec.emptyMeansAll && on.size() == r->checks.size()))
                        for (const auto& v : on) joined += (joined.empty() ? "" : ",") + v;
                    emitChange(*r, joined, true);
                });
            }
            break;
        }
        default: {
            // Un texte, un nom, ou un objet du projet a choisir pendant la frappe.
            auto in = std::make_unique<ui::InputText>(base);
            row.input = &static_cast<ui::InputText&>(addChild(std::move(in)));
            row.links += row.input->textChanged->connect([this, r](const std::string& t) { emitChange(*r, t, false); });
            row.links += row.input->editingDone->connect([this, r](const std::string& t) { emitChange(*r, t, true); });
            if (mm::isPicker(spec.kind)) {
                const auto kind = spec.kind;
                row.input->setAssist([this, kind](std::string_view before, std::size_t& from,
                                                  std::vector<ui::InputText::Suggestion>& out) {
                    from = 0;
                    const auto want = lower(before);
                    std::vector<std::pair<std::string, std::string>> names;
                    if (kind == FieldKind::Sheet && hosts_.sheets)
                        for (const auto& s : hosts_.sheets()) names.emplace_back(s, "onglet");
                    else if (hosts_.names)
                        names = hosts_.names(kind);
                    for (const auto& [name, detail] : names) {
                        if (!want.empty() && lower(name).find(want) == std::string::npos) continue;
                        ui::InputText::Suggestion s;
                        s.text = name;
                        s.detail = detail;
                        out.push_back(std::move(s));
                        if (out.size() >= 60) break;
                    }
                });
            }
            break;
        }
    }
    // L'aide entiere au survol du champ.
    if (!f.help.empty())
        for (auto* w : row.widgets())
            if (w->tooltip().empty()) w->setTooltip(f.help);
}

void MacroFormView::updateControls(Row& row, bool force) {
    const auto& f = row.field;
    const auto& spec = f.spec;
    if (row.toggle) {
        const auto v = lower(f.value);
        row.toggle->setOn(v == "o" || v == "oui" || v == "1" || v == "y" || v == "true");
    }
    if (row.segments) {
        int sel = -1;
        for (std::size_t i = 0; i < row.segmentValues.size(); ++i)
            if (row.segmentValues[i] == f.value || numberText(row.segmentValues[i]) == numberText(f.value)) sel = static_cast<int>(i);
        row.segments->setSelected(sel);
    }
    if (row.drop) {
        int sel = -1;
        const auto& items = row.drop->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == f.value || numberText(items[i].value) == numberText(f.value)) sel = static_cast<int>(i);
        if (sel < 0 && !f.value.empty()) {
            // Une valeur hors liste (tapee avant, ou d'un autre classeur) : on la garde visible.
            auto copy = items;
            copy.push_back({f.value + "  (hors liste)", f.value, {}, true});
            row.drop->setItems(std::move(copy));
            sel = static_cast<int>(items.size());
        }
        row.drop->setSelectedIndex(sel);
    }
    if (row.input && force && row.input->text() != f.value) row.input->setText(f.value);
    if (row.input) {
        std::string placeholder;
        if (spec.kind == FieldKind::File && spec.optional) {
            const auto last = hosts_.lastWorkbook ? hosts_.lastWorkbook() : std::string{};
            placeholder = last.empty() ? "glisse le fichier ici, ou \xE2\x80\xA6, ou Ctrl+V"
                                       : "vide : " + std::filesystem::path(last).filename().string() + " (le dernier ouvert)";
        } else if (spec.kind == FieldKind::File) {
            placeholder = "glisse le fichier ici, ou \xE2\x80\xA6, ou Ctrl+V";
        } else if (spec.optional) {
            placeholder = "vide";
        } else if (mm::isPicker(spec.kind)) {
            placeholder = "tape, ou Ctrl+Espace pour la liste";
        }
        row.input->setPlaceholder(placeholder);
    }
    if (row.lines && force) {
        // "a,b,c" -> une ligne par element.
        std::string text;
        for (const auto& part : splitList(f.value)) text += (text.empty() ? "" : "\n") + part;
        if (row.lines->text() != text) row.lines->setText(text);
    }
    if (!row.checks.empty()) {
        const auto on = splitList(f.value);
        for (std::size_t i = 0; i < row.checks.size(); ++i) {
            const bool checked = (f.value.empty() && spec.emptyMeansAll)
                || std::find(on.begin(), on.end(), row.checkValues[i]) != on.end();
            row.checks[i]->setState(checked ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        }
    }
}

void MacroFormView::computeHint(Row& row) {
    const auto& f = row.field;
    const auto& spec = f.spec;
    row.hint.clear();
    row.hintTone = ui::Tone::Muted;
    row.example.clear();
    const auto set = [&](std::string text, ui::Tone tone) {
        row.hint = std::move(text);
        row.hintTone = tone;
    };
    switch (spec.kind) {
        case FieldKind::Name: {
            const auto c = mm::checkName(f.value, spec.optional);
            if (c.verdict == mm::Verdict::Error) set("\xE2\x9C\x97 " + c.message, ui::Tone::Error);
            else if (c.verdict == mm::Verdict::Warning) set("\xE2\x9A\xA0 " + c.message, ui::Tone::Warning);
            else if (!spec.example.empty()) set("\xE2\x86\x92 " + mm::exampleFor(spec, f.value), ui::Tone::Info);
            break;
        }
        case FieldKind::Number: {
            if (!row.segments && !row.drop) {
                const auto c = mm::checkNumber(f.value, spec.hasRange ? spec.minimum : 0.0, spec.hasRange ? spec.maximum : 0.0);
                if (c.verdict == mm::Verdict::Error) set("\xE2\x9C\x97 " + c.message, ui::Tone::Error);
                else if (const auto label = spec.optionLabel(numberText(f.value)); !label.empty()) set(label, ui::Tone::Info);
                else if (spec.hasRange) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "de %g \xC3\xA0 %g", spec.minimum, spec.maximum);
                    set(buf, ui::Tone::Muted);
                }
            }
            break;
        }
        case FieldKind::File: {
            if (f.value.empty()) {
                if (spec.optional) {
                    const auto last = hosts_.lastWorkbook ? hosts_.lastWorkbook() : std::string{};
                    if (!last.empty()) set("vide : le dernier ouvert, " + last, ui::Tone::Muted);
                    else set("aucun fichier choisi", f.table ? ui::Tone::Warning : ui::Tone::Muted);
                } else {
                    set("\xC3\xA0 choisir", ui::Tone::Warning);
                }
            } else if (hosts_.fileInfo) {
                std::string text;
                const bool ok = hosts_.fileInfo(f.value, text);
                set((ok ? "\xE2\x9C\x93 " : "\xE2\x9C\x97 ") + text, ok ? ui::Tone::Ok : ui::Tone::Error);
            }
            break;
        }
        case FieldKind::OutputFile:
            if (f.value.empty()) set("aucun", ui::Tone::Muted);
            else if (hosts_.outputPath) set(hosts_.outputPath(f.value), ui::Tone::Muted);
            break;
        case FieldKind::Checks:
            if (row.checks.empty()) set(spec.source == "retard" ? "rien n'est en retard : le projet est \xC3\xA0 jour" : "rien \xC3\xA0 cocher",
                                        ui::Tone::Ok);
            break;
        default:
            if (mm::isPicker(spec.kind) && hosts_.names && spec.kind != FieldKind::Sheet) {
                if (f.value.empty()) {
                    if (!spec.optional) set("\xC3\xA0 remplir", ui::Tone::Warning);
                    break;
                }
                bool found = false;
                std::string detail;
                for (const auto& [name, d] : hosts_.names(spec.kind))
                    if (lower(name) == lower(f.value)) {
                        found = true;
                        detail = d;
                    }
                if (found) set("\xE2\x9C\x93 existe" + (detail.empty() ? std::string{} : " \xC2\xB7 " + detail), ui::Tone::Ok);
                else if (spec.mustExist) set("\xE2\x9C\x97 introuvable dans le projet", ui::Tone::Error);
                else if (spec.allowNew || spec.kind == FieldKind::Section || spec.kind == FieldKind::Unit
                         || spec.kind == FieldKind::Subroutine)
                    set("+ sera cr\xC3\xA9\xC3\xA9" "e", ui::Tone::Info);
                else set("absent du projet et de la biblioth\xC3\xA8que", ui::Tone::Warning);
            }
            break;
    }
    if (f.origin == FormField::Origin::Remembered) row.hint += (row.hint.empty() ? "" : "   \xC2\xB7   ") + std::string("comme la derni\xC3\xA8re fois");
}

// ---------------------------------------------------------------- la page ----
float MacroFormView::contentHeight() const { return contentH_; }

void MacroFormView::clampScroll() {
    const float maxScroll = std::max(0.f, contentH_ - bounds().h + 20.f);
    scroll_ = std::clamp(scroll_, 0.f, maxScroll);
}

void MacroFormView::onLayout() {
    const auto b = bounds();
    const float pad = 20.f;
    const bool narrow = b.w < 700.f;
    const float labelW = narrow ? b.w - 2 * pad : std::clamp(b.w * 0.36f, 210.f, 340.f);
    const float ctrlX = narrow ? pad : pad + labelW + 22.f;
    const float ctrlW = narrow ? b.w - 2 * pad : std::max(160.f, std::min(520.f, b.w - ctrlX - pad));
    float y = 12.f;
    if (!message_.empty()) y += 16.f * static_cast<float>(wrap(message_, kHint, b.w - 2 * pad - 16.f).size()) + 20.f;
    std::string group = "\x01";
    bool advancedStarted = false;
    advancedHeader_ = {};
    for (Row* row : ordered()) {
        const auto& f = row->field;
        if (f.advanced && !advancedStarted) {
            advancedStarted = true;
            y += 10.f;
            advancedHeader_ = {b.x + pad, b.y + y - scroll_, b.w - 2 * pad, 32.f};
            y += 38.f;
        } else if (!f.advanced && f.group != group) {
            group = f.group;
            if (!group.empty()) y += 44.f;
        }
        const bool hidden = f.advanced && !advancedOpen_;
        for (auto* w : row->widgets()) w->setVisibility(hidden ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        if (hidden) {
            row->y = y;
            row->h = 0.f;
            continue;
        }
        row->labelLines = wrap(f.label, kLabel, labelW);
        row->helpLines = f.help.empty() ? std::vector<std::string>{} : wrap(f.help.substr(0, f.help.find('\n')), kHelp, labelW);
        if (row->helpLines.size() > 3) {
            row->helpLines.resize(3);
            row->helpLines.back() += " \xE2\x80\xA6";
        }
        const float labelH = 20.f * static_cast<float>(row->labelLines.size()) + 16.f * static_cast<float>(row->helpLines.size()) + 4.f;
        float cy = narrow ? y + labelH + 4.f : y;
        const float x0 = b.x + ctrlX;
        float ch = 0.f;
        const auto place = [&](ui::Widget* w, float x, float yy, float wW, float hH) { w->setBounds({x, b.y + yy - scroll_, wW, hH}); };
        if (row->file) {
            const float bw = row->browse ? 40.f : 0.f;
            place(row->file, x0, cy, ctrlW - bw - (row->browse ? 6.f : 0.f), 30.f);
            if (row->browse) place(row->browse, x0 + ctrlW - bw, cy, bw, 30.f);
            ch = 30.f;
            if (!row->chips.empty()) {
                float cx = x0;
                for (auto* chip : row->chips) {
                    const float w = std::min(ctrlW, ui::measureWidth(chip->text(), kHint) + 26.f);
                    if (cx + w > x0 + ctrlW) break;
                    place(chip, cx, cy + 34.f, w, 24.f);
                    cx += w + 6.f;
                }
                ch += 28.f;
            }
        } else if (row->input) {
            place(row->input, x0, cy, std::min(ctrlW, row->chips.empty() ? ctrlW : 200.f), 30.f);
            ch = 30.f;
            float cx = x0 + std::min(ctrlW, 200.f) + 8.f;
            for (auto* chip : row->chips) {
                const float w = ui::measureWidth(chip->text(), kHint) + 26.f;
                place(chip, cx, cy + 3.f, w, 24.f);
                cx += w + 6.f;
            }
        } else if (row->toggle) {
            place(row->toggle, x0, cy + 2.f, 110.f, 26.f);
            ch = 30.f;
        } else if (row->segments) {
            place(row->segments, x0, cy, std::min(ctrlW + 60.f, row->segments->preferredWidth()), 32.f);
            ch = 32.f;
        } else if (row->drop) {
            place(row->drop, x0, cy, std::min(ctrlW, 380.f), 30.f);
            ch = 30.f;
        } else if (row->lines) {
            place(row->lines, x0, cy, ctrlW, 92.f);
            ch = 92.f;
        } else if (!row->checks.empty()) {
            float yy = cy;
            for (auto* c : row->checks) {
                place(c, x0, yy, ctrlW, 24.f);
                yy += 26.f;
            }
            ch = yy - cy;
        }
        row->controlH = ch;
        const float hintH = row->hint.empty() ? 0.f : 20.f;
        const float h = narrow ? labelH + 4.f + ch + hintH : std::max(labelH, ch + hintH);
        row->y = y;
        row->h = h;
        y += h + 16.f;
    }
    contentH_ = y + 20.f;
    clampScroll();
}

void MacroFormView::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    const float pad = 20.f;
    const bool narrow = b.w < 700.f;
    const float labelW = narrow ? b.w - 2 * pad : std::clamp(b.w * 0.36f, 210.f, 340.f);
    const float ctrlX = narrow ? pad : pad + labelW + 22.f;
    float y = 12.f;
    if (!message_.empty()) {
        const auto lines = wrap(message_, kHint, b.w - 2 * pad - 16.f);
        const float h = 16.f * static_cast<float>(lines.size()) + 12.f;
        const gfx::Color tone = ctx.theme.tone(messageTone_, c.textMuted);
        ctx.r.fillRoundedRect({b.x + pad, b.y + y - scroll_, b.w - 2 * pad, h}, withAlpha(tone, 0.14f), 6.f);
        ctx.r.fillRect({b.x + pad, b.y + y - scroll_, 3.f, h}, tone);
        float ly = b.y + y + 6.f - scroll_;
        for (const auto& l : lines) {
            ctx.r.drawText({b.x + pad + 12.f, ly}, l, kHint, c.text);
            ly += 16.f;
        }
        y += h + 8.f;
    }
    std::string group = "\x01";
    bool advancedStarted = false;
    for (Row* row : ordered()) {
        const auto& f = row->field;
        if (f.advanced && !advancedStarted) {
            advancedStarted = true;
            std::size_t n = 0;
            for (const auto& r : rows_)
                if (r->field.advanced) ++n;
            const auto hb = advancedHeader_;
            const std::string title = std::string(advancedOpen_ ? "\xE2\x96\xBE" : "\xE2\x96\xB8") + "  R\xC3\xA9glages avanc\xC3\xA9s  ("
                                    + std::to_string(n) + ")";
            ctx.r.drawText({hb.x, hb.y + 7.f}, title, kGroup, c.textMuted);
            ctx.r.line({hb.x, hb.y + hb.h}, {hb.x + hb.w, hb.y + hb.h}, c.border, 1.f);
        } else if (!f.advanced && f.group != group) {
            group = f.group;
            if (!group.empty()) {
                const float gy = b.y + row->y - 44.f - scroll_;
                ctx.r.drawText({b.x + pad, gy + 8.f}, group, kGroup, c.accent);
                ctx.r.line({b.x + pad, gy + 33.f}, {b.x + b.w - pad, gy + 33.f}, c.border, 1.f);
            }
        }
        if (row->h <= 0.f) continue;
        const float ry = b.y + row->y - scroll_;
        if (ry > b.y + b.h || ry + row->h < b.y) continue;
        // Un champ qui vient d'apparaitre : un lisere qui s'efface.
        if (row->appeared >= 0.0) {
            const float t = static_cast<float>((ctx.time - row->appeared) * 1000.0 / (ctx.theme.motion.flashMs * 2.f));
            if (t < 1.f) {
                ctx.r.fillRoundedRect({b.x + 6.f, ry, 4.f, row->h}, withAlpha(c.accent, 1.f - t), 2.f);
                invalidate();
            } else {
                row->appeared = -1.0;
            }
        }
        float ly = ry + 2.f;
        for (const auto& l : row->labelLines) {
            ctx.r.drawText({b.x + pad, ly}, l, kLabel, c.text);
            ctx.r.drawText({b.x + pad + 0.6f, ly}, l, kLabel, c.text);   // gras simule
            ly += 20.f;
        }
        for (const auto& l : row->helpLines) {
            ctx.r.drawText({b.x + pad, ly}, l, kHelp, c.textMuted);
            ly += 16.f;
        }
        if (!row->hint.empty()) {
            const float hy = narrow ? ly + 4.f + row->controlH + 2.f : ry + row->controlH + 3.f;
            const gfx::Color tone = ctx.theme.tone(row->hintTone, c.textMuted);
            std::string hint = row->hint;
            const float maxW = b.w - (b.x + ctrlX) + b.x - pad;
            while (hint.size() > 4 && ui::measureWidth(hint, kHint) > maxW) hint = hint.substr(0, hint.size() - 4) + "\xE2\x80\xA6";
            ctx.r.drawText({b.x + ctrlX, hy}, hint, kHint, tone);
        }
    }
    // La barre de defilement.
    if (contentH_ > b.h) {
        const float ratio = b.h / contentH_;
        const float barH = std::max(30.f, b.h * ratio);
        const float maxScroll = std::max(1.f, contentH_ - b.h + 20.f);
        const float barY = b.y + (b.h - barH) * std::clamp(scroll_ / maxScroll, 0.f, 1.f);
        ctx.r.fillRoundedRect({b.x + b.w - 7.f, barY, 4.f, barH}, c.scrollbar, 2.f);
    }
}

ui::EventResult MacroFormView::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
        scroll_ -= w->dy * 48.f;
        clampScroll();
        invalidateLayout();
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && advancedHeader_.contains(d->pos)) {
        setAdvancedOpen(!advancedOpen_);
        return ui::EventResult::Consumed;
    }
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev); drop && bounds().contains(drop->pos)) {
        if (dropFile(drop->path, drop->pos)) return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void MacroFormView::setAdvancedOpen(bool open) {
    if (open == advancedOpen_) return;
    advancedOpen_ = open;
    invalidateLayout();
    invalidate();
}

gfx::Rect MacroFormView::rowRect(const std::string& key) {
    const auto* r = rowOf(key);
    if (!r) return {};
    return {bounds().x, bounds().y + r->y - scroll_, bounds().w, r->h};
}

void MacroFormView::ensureVisible(const std::string& key) {
    auto* r = rowOf(key);
    if (!r) return;
    if (r->field.advanced && !advancedOpen_) setAdvancedOpen(true);
    layout();
    if (r->y - scroll_ < 0.f) scroll_ = std::max(0.f, r->y - 40.f);
    else if (r->y + r->h - scroll_ > bounds().h) scroll_ = r->y + r->h - bounds().h + 40.f;
    clampScroll();
    invalidateLayout();
    layout();
    invalidate();
}

bool MacroFormView::setValueForTest(const std::string& key, const std::string& value) {
    auto* r = rowOf(key);
    if (!r) return false;
    if (r->toggle) {
        const auto v = lower(value);
        const bool on = v == "o" || v == "oui" || v == "1" || v == "y" || v == "true";
        if (on != r->toggle->on()) r->toggle->clickForTest();
        return true;
    }
    if (r->segments) {
        for (std::size_t i = 0; i < r->segmentValues.size(); ++i)
            if (r->segmentValues[i] == value || lower(r->segments->options()[i]) == lower(value)) {
                r->segments->setSelected(static_cast<int>(i));
                emitChange(*r, r->segmentValues[i], true);
                return true;
            }
        return false;
    }
    if (r->drop) {
        const auto& items = r->drop->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == value || lower(items[i].label) == lower(value)) {
                if (r->drop->selectedIndex() == static_cast<int>(i)) emitChange(*r, items[i].value, true);
                else r->drop->setSelectedIndex(static_cast<int>(i));      // le signal fait le reste
                return true;
            }
        return false;
    }
    if (r->lines) {
        std::string text;
        for (const auto& part : splitList(value)) text += (text.empty() ? "" : "\n") + part;
        r->lines->setText(text);     // textChanged -> emitChange
        emitChange(*r, value, true);
        return true;
    }
    if (!r->checks.empty()) {
        const auto on = splitList(value);
        for (std::size_t i = 0; i < r->checks.size(); ++i)
            r->checks[i]->setState(std::find(on.begin(), on.end(), r->checkValues[i]) != on.end() ? ui::Checkbox::State::Checked
                                                                                                    : ui::Checkbox::State::Unchecked);
        emitChange(*r, value, true);
        return true;
    }
    if (r->input) {
        r->input->setText(value);
        emitChange(*r, value, true);
        return true;
    }
    return false;
}

bool MacroFormView::dropFile(const std::string& path, gfx::Point at) {
    Row* target = nullptr;
    for (auto& r : rows_)
        if (r->file && r->h > 0.f && rowRect(r->field.key).contains(at)) target = r.get();
    if (!target) {
        const auto ext = lower(std::filesystem::path(path).extension().string());
        for (auto& r : rows_) {
            if (!r->file || r->field.spec.kind != FieldKind::File) continue;
            const auto& exts = r->field.spec.extensions;
            if (exts.empty() || std::find(exts.begin(), exts.end(), ext) != exts.end()) {
                target = r.get();
                break;
            }
        }
    }
    if (!target) return false;
    target->file->setText(path);
    emitChange(*target, path, true);
    return true;
}

bool MacroFormView::browse(const std::string& key) {
    auto* r = rowOf(key);
    if (!r || !r->file) return false;
    ui::FilePick pick;
    pick.title = r->field.label;
    std::string patterns;
    for (const auto& e : r->field.spec.extensions) patterns += (patterns.empty() ? "" : ";") + e.substr(e.find_first_not_of('.'));
    if (!patterns.empty()) pick.filters.emplace_back(r->field.label, patterns);
    pick.filters.emplace_back("Tous les fichiers", "*");
    if (!r->field.value.empty()) pick.start = r->field.value;
    else if (hosts_.lastWorkbook) pick.start = std::filesystem::path(hosts_.lastWorkbook()).parent_path().string();
    const std::string k = key;
    const std::weak_ptr<char> alive = alive_;
    return ui::pickFile(pick, [this, k, alive](std::string chosen) {
        if (alive.expired()) return;                   // le formulaire a ete ferme entretemps
        if (chosen.empty()) return;                    // annule
        auto* row = rowOf(k);
        if (!row || !row->file) return;
        row->file->setText(chosen);
        emitChange(*row, chosen, true);
    });
}

} // namespace app
