// =============================================================================
//  ui/widgets/FilterBar.cpp - 1.47.0 (planche 12) : << Filtrer et chercher >>, la
//  meme barre au-dessus de chaque longue liste (voir FilterBar.hpp)
// =============================================================================
#include "FilterBar.hpp"

#include "Controls.hpp"
#include "FilterMemory.hpp"
#include "../Icons.hpp"
#include "../Shapes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>

namespace ui {

namespace {

constexpr float kRowH = 32.f, kRowGap = 8.f, kItemGap = 6.f;
const gfx::FontId kFont{13}, kSmall{12}, kTiny{11};
constexpr std::int64_t kDay = 86400;
constexpr int kPeriods[6] = {1, 7, 30, 90, 365, 0};

std::string slugOf(std::string_view title) {
    const std::string f = foldForSearch(title);
    std::string out;
    for (const char c : f) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum) out += c;
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("colonne") : out;
}

std::tm localOf(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}

std::int64_t startOfDay(std::int64_t t) {
    std::tm tm = localOf(t);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

std::string dayLabel(std::int64_t day) {
    const std::tm tm = localOf(day);
    char b[16];
    std::snprintf(b, sizeof b, "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    return b;
}

std::string elide(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

std::string shownValue(const std::string& v) { return v.empty() ? std::string("(vide)") : v; }

void chevronDown(gfx::IRenderer& r, float x, float y, gfx::Color c) {
    r.line({x, y}, {x + 3.5f, y + 3.5f}, c, 1.4f);
    r.line({x + 3.5f, y + 3.5f}, {x + 7.f, y}, c, 1.4f);
}

void cross(gfx::IRenderer& r, float cx, float cy, float s, gfx::Color c) {
    r.line({cx - s, cy - s}, {cx + s, cy + s}, c, 1.4f);
    r.line({cx - s, cy + s}, {cx + s, cy - s}, c, 1.4f);
}

// Un trait en tirets (le bord d'une puce sans filtre : planche 12) : les deux cotes droits, puis les bouts arrondis.
void dashedRound(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float rad = b.h * 0.5f, cy = b.y + rad;
    for (float x = b.x + rad; x < b.right() - rad; x += 6.f) {
        const float x1 = std::min(x + 3.f, b.right() - rad);
        r.line({x, b.y + 0.5f}, {x1, b.y + 0.5f}, c, 1.f);
        r.line({x, b.bottom() - 0.5f}, {x1, b.bottom() - 0.5f}, c, 1.f);
    }
    constexpr float kPi = 3.14159265f;
    for (int side = 0; side < 2; ++side) {
        const float cx = side == 0 ? b.x + rad : b.right() - rad;
        const float a0 = side == 0 ? kPi * 0.5f : -kPi * 0.5f;   // la moitie gauche (ou droite) du cercle
        for (int k = 0; k < 8; k += 2) {
            const float t0 = a0 + kPi * static_cast<float>(k) / 8.f, t1 = a0 + kPi * static_cast<float>(k + 1) / 8.f;
            r.line({cx + rad * std::cos(t0), cy + rad * std::sin(t0)}, {cx + rad * std::cos(t1), cy + rad * std::sin(t1)}, c, 1.f);
        }
    }
}

} // namespace

// ================================================================================== la puce (un bouton)
class FilterBar::Chip final : public Button {
public:
    enum class Role : std::uint8_t { Field, Add, Clear, Saved, ClearSearch };
    Chip(FilterBar& bar, Role role, std::size_t field, std::string text, std::string id)
        : Button(std::move(text), std::move(id)), bar_(bar), role_(role), field_(field) {}

    [[nodiscard]] SizeHint sizeHint() const override {
        SizeHint h;
        h.preferred = {width(), role_ == Role::Saved ? 30.f : 28.f};
        h.minimum = h.preferred;
        return h;
    }
    [[nodiscard]] float width() const {
        switch (role_) {
            case Role::Field: {
                const bool on = bar_.fieldActive(field_);
                const auto* p = bar_.pickOf(field_);
                // Comme onPaint : << Etat : >> en clair, les valeurs en gras ; la croix (ou le chevron) a droite.
                const std::string full = text();
                const auto colon = full.find(" : ");
                float w = 10.f + 26.f + 2.f;
                if (on && colon != std::string::npos)
                    w += measureWidth(full.substr(0, colon + 3), kSmall) + measureWidth(full.substr(colon + 3), gfx::semibold(kSmall));
                else
                    w += measureWidth(full, gfx::semibold(kSmall));
                if (p && p->viaMenu) w += measureWidth("clic droit", gfx::semibold(gfx::FontId{10})) + 12.f;
                return w;
            }
            case Role::Add: return 10.f + 14.f + measureWidth(text(), gfx::semibold(kSmall)) + 10.f;
            case Role::Clear: return measureWidth(text(), gfx::semibold(kSmall)) + 8.f;
            case Role::Saved: return 10.f + 16.f + measureWidth(text(), gfx::semibold(kSmall)) + 8.f + 10.f + 8.f;
            case Role::ClearSearch: return 18.f;
        }
        return 60.f;
    }
    [[nodiscard]] bool takeCross() {
        const bool x = downOnCross_;
        downOnCross_ = false;
        return x;
    }
    [[nodiscard]] std::size_t field() const noexcept { return field_; }
    [[nodiscard]] Role role() const noexcept { return role_; }

protected:
    [[nodiscard]] gfx::Rect crossRect() const {
        const gfx::Rect b = bounds();
        return {b.right() - 22.f, b.y, 22.f, b.h};
    }
    EventResult onEvent(const InputEvent& ev) override {
        if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left && bounds().contains(d->pos))
            downOnCross_ = role_ == Role::Field && bar_.fieldActive(field_) && bar_.fields_[field_].kind != Kind::Period && crossRect().contains(d->pos);
        return Button::onEvent(ev);
    }
    void onPaint(const PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        const gfx::FontId f = gfx::semibold(kSmall);
        const float ty = b.y + (b.h - r.lineHeight(f)) * 0.5f;
        switch (role_) {
            case Role::Field: {
                const bool on = bar_.fieldActive(field_);
                const bool open = bar_.openMenu() == Menu::Chip && bar_.menuField() == field_;
                const auto* p = bar_.pickOf(field_);
                if (on) {
                    r.fillRoundedRect(b, c.accent.withAlpha(24), b.h * 0.5f);
                    shapes::fillFramedRect(r, b, c.accent.withAlpha(24), c.accent.withAlpha(60), b.h * 0.5f);
                } else {
                    r.fillRoundedRect(b, c.panelBg, b.h * 0.5f);
                    dashedRound(r, b, c.borderStrong);
                }
                if (hovered() || open) r.fillRoundedRect(b.inset(-2.f, -2.f), c.accent.withAlpha(22), b.h * 0.5f + 2.f);
                const gfx::Color ink = on ? c.accentPressed : c.text;
                float x = b.x + 10.f;
                // << Etat : >> en clair, les valeurs en gras.
                const std::string full = text();
                const auto colon = full.find(" : ");
                if (on && colon != std::string::npos) {
                    const std::string head = full.substr(0, colon + 3), tail = full.substr(colon + 3);
                    r.drawText({x, ty}, head, kSmall, ink);
                    x += r.measure(head, kSmall).width;
                    const float room = b.right() - 26.f - x - (p && p->viaMenu ? measureWidth("clic droit", gfx::semibold(gfx::FontId{10})) + 12.f : 0.f);
                    const std::string t = elide(r, tail, f, room);
                    r.drawText({x, ty}, t, f, ink);
                    x += r.measure(t, f).width;
                } else {
                    const std::string t = elide(r, full, f, b.w - 32.f);
                    r.drawText({x, ty}, t, f, ink);
                    x += r.measure(t, f).width;
                }
                if (p && p->viaMenu) {   // la puce posee par le clic droit (planche 12)
                    const gfx::FontId fs = gfx::semibold(gfx::FontId{10});
                    const float sw = r.measure("clic droit", fs).width + 10.f;
                    const gfx::Rect tag{x + 6.f, b.y + (b.h - 16.f) * 0.5f, sw, 16.f};
                    r.fillRoundedRect(tag, c.panelBg, 5.f);
                    r.drawText({tag.x + 5.f, tag.y + (tag.h - r.lineHeight(fs)) * 0.5f}, "clic droit", fs, c.accentPressed);
                }
                const float ax = b.right() - 17.f, ay = b.y + b.h * 0.5f;
                if (on && bar_.fields_[field_].kind != Kind::Period) cross(r, ax + 3.5f, ay, 3.5f, c.accent);
                else chevronDown(r, ax, ay - 2.f, on ? c.accent : c.textMuted);
                break;
            }
            case Role::Add: {
                if (hovered()) r.fillRoundedRect(b, c.accent.withAlpha(18), b.h * 0.5f);
                const float cx = b.x + 16.f, cy = b.y + b.h * 0.5f;
                r.line({cx - 5.f, cy}, {cx + 5.f, cy}, c.accentPressed, 1.6f);
                r.line({cx, cy - 5.f}, {cx, cy + 5.f}, c.accentPressed, 1.6f);
                r.drawText({b.x + 26.f, ty}, text(), f, c.accentPressed);
                break;
            }
            case Role::Clear:
                r.drawText({b.x + 4.f, ty}, text(), f, hovered() ? c.accent : c.accentPressed);
                break;
            case Role::Saved: {
                shapes::fillFramedRect(r, b, c.panelBg, hovered() || bar_.openMenu() == Menu::Saved ? c.accent : c.borderStrong, 8.f);
                drawIcon(r, bar_.currentSavedName().empty() ? Icon::Star : Icon::StarFilled, {b.x + 10.f, b.y + (b.h - 14.f) * 0.5f, 14.f, 14.f},
                         bar_.currentSavedName().empty() ? c.textMuted : c.accent);
                const std::string t = elide(r, text(), f, b.w - 50.f);
                r.drawText({b.x + 30.f, ty}, t, f, c.text);
                chevronDown(r, b.right() - 18.f, b.y + b.h * 0.5f - 2.f, c.textMuted);
                break;
            }
            case Role::ClearSearch:
                if (hovered()) r.fillRoundedRect(b.inset(1.f, 4.f), c.accent.withAlpha(20), 4.f);
                cross(r, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 3.5f, c.textMuted);
                break;
        }
    }

private:
    FilterBar&  bar_;
    Role        role_;
    std::size_t field_;
    bool        downOnCross_{false};
};

// ================================================================================== l'etat
bool FilterBar::State::active() const noexcept {
    if (!SearchQuery(search).empty()) return true;
    for (const auto& p : picks)
        if (p.active()) return true;
    return false;
}

FilterBar::FilterBar(std::string id, std::vector<Field> fields, std::string searchId) : Widget(std::move(id)), fields_(std::move(fields)) {
    auto field = std::make_unique<InputText>(searchId.empty() ? this->id() + ".recherche" : std::move(searchId));
    field->setPlaceholder("Chercher\xE2\x80\xA6");
    field->setEscapeClears(true);
    field->setPadding({3.f, 74.f, 3.f, 30.f});
    field->setTooltip("Dans toutes les colonnes montr\xC3\xA9" "es (Ctrl+F) : des mots, \"une phrase\", -mot pour exclure");
    search_ = static_cast<InputText*>(&addChild(std::move(field)));
    links_ += search_->textChanged->connect([this](const std::string& t) {
        if (state_.search == t) return;
        state_.search = t;
        edited();
    });
    auto clearSearch = std::make_unique<Chip>(*this, Chip::Role::ClearSearch, 0, std::string(), this->id() + ".recherche.effacer");
    clearSearch->setTooltip("Effacer la recherche (\xC3\x89" "chap)");
    Chip* cs = static_cast<Chip*>(&addChild(std::move(clearSearch)));
    links_ += cs->clicked->connect([this] { setSearch({}); });
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        auto chip = std::make_unique<Chip>(*this, Chip::Role::Field, i, fields_[i].chipLabel.empty() ? fields_[i].title : fields_[i].chipLabel,
                                           this->id() + ".puce." + slugOf(fields_[i].chipLabel.empty() ? fields_[i].title : fields_[i].chipLabel));
        Chip* c = static_cast<Chip*>(&addChild(std::move(chip)));
        links_ += c->clicked->connect([this, c] {
            const std::size_t f = c->field();
            if (c->takeCross()) clearField(f);
            else if (menu_ == Menu::Chip && menuField_ == f) closeMenu();
            else openChipMenu(f);
        });
        chips_.push_back(c);
    }
    add_ = static_cast<Button*>(&addChild(std::make_unique<Chip>(*this, Chip::Role::Add, 0, "Filtre", this->id() + ".plus-filtre")));
    add_->setTooltip("Un filtre sur une autre colonne");
    clear_ = static_cast<Button*>(&addChild(std::make_unique<Chip>(*this, Chip::Role::Clear, 0, "Effacer les filtres", this->id() + ".effacer")));
    savedBtn_ = static_cast<Button*>(&addChild(std::make_unique<Chip>(*this, Chip::Role::Saved, 0, "Mes filtres", this->id() + ".mes-filtres")));
    savedBtn_->setTooltip("Tes filtres de cette liste, enregistr\xC3\xA9s sur ce PC");
    links_ += add_->clicked->connect([this] { menu_ == Menu::Add ? closeMenu() : openAddMenu(); });
    links_ += clear_->clicked->connect([this] { clearAll(); });
    links_ += savedBtn_->clicked->connect([this] { menu_ == Menu::Saved ? closeMenu() : openSavedMenu(); });
    popup_ = static_cast<FilterBarPopup*>(&addChild(std::make_unique<FilterBarPopup>(this->id() + ".menu")));
    popup_->onRow = [this](std::size_t i) { popupRow(i); };
    popup_->onLink = [this](std::size_t i) { popupFooter(i); };
    popup_->onRemove = [this](std::size_t i) { popupDelete(i); };
    popup_->onEnter = [this](const std::string& t) { popupEnter(t); };
    popup_->onClose = [this] {
        menu_ = Menu::None;
        naming_ = managing_ = false;
        invalidate();
    };
    syncChips();
}

FilterBar::~FilterBar() {
    *alive_ = false;
    links_.clear();
}

// ------------------------------------------------------------------------------------------ brancher
void FilterBar::attach(TableView& table, TimeOf time, std::function<std::string(RowIndex)> extra) {
    table_ = &table;
    TableView* t = &table;
    Source s;
    s.rows = [t] { return t->model() ? t->model()->rowCount() : std::size_t{0}; };
    s.text = [t](std::size_t r, std::size_t f) {
        const auto& m = t->model();
        return m && f < m->columnCount() && r < m->rowCount() ? m->cellText(static_cast<RowIndex>(r), f) : std::string();
    };
    if (time) s.time = [time](std::size_t r, std::size_t f) { return time(static_cast<RowIndex>(r), f); };
    if (extra) s.extra = [extra](std::size_t r) { return extra(static_cast<RowIndex>(r)); };
    source_ = std::move(s);
    linked_ = nullptr;
    hookTable(table);
    edited(false);
}

void FilterBar::hookTable(TableView& table) {
    std::weak_ptr<bool> alive = alive_;
    TableView::FilterBarHooks hooks;
    hooks.active = [this, alive](std::size_t c) { return !alive.expired() && fieldActive(c); };
    hooks.open = [this, alive](std::size_t c, gfx::Rect r) {
        if (!alive.expired() && c < fields_.size() && fields_[c].kind != Kind::Text) openChipMenu(c, r);
    };
    hooks.any = [this, alive] { return !alive.expired() && state_.active(); };
    hooks.clear = [this, alive] {
        if (!alive.expired()) clearAll();
    };
    table.setFilterBarHooks(std::move(hooks));
}

void FilterBar::setSource(Source source) {
    source_ = std::move(source);
    table_ = nullptr;
    countDirty_ = true;
    invalidate();
}

void FilterBar::linkTable(TableView& table) {
    table_ = nullptr;
    linked_ = &table;
    hookTable(table);
    table.setHighlight(state_.search);
}

void FilterBar::refreshCounts() {
    countDirty_ = true;
    if (menu_ != Menu::None) refreshMenu();
    invalidate();
}

void FilterBar::setMemoryKey(std::string key) {
    memoryKey_ = std::move(key);
    loadMemory();
}

std::int64_t FilterBar::now() const {
    if (now_) return now_();
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::size_t FilterBar::rowCount() const { return source_.rows ? source_.rows() : 0; }

std::string FilterBar::cell(std::size_t row, std::size_t field) const { return source_.text ? source_.text(row, field) : std::string(); }

// ------------------------------------------------------------------------------------------ filtrer
FilterBar::Pick* FilterBar::pickOf(std::size_t field) {
    for (auto& p : state_.picks)
        if (p.field == field) return &p;
    return nullptr;
}
const FilterBar::Pick* FilterBar::pickOf(std::size_t field) const {
    for (const auto& p : state_.picks)
        if (p.field == field) return &p;
    return nullptr;
}

bool FilterBar::fieldActive(std::size_t field) const {
    const Pick* p = pickOf(field);
    return p && p->active();
}

bool FilterBar::fieldAccepts(const Pick& p, std::size_t row) const {
    if (!p.active() || p.field >= fields_.size()) return true;
    if (fields_[p.field].kind == Kind::Period) {
        const std::int64_t t = source_.time ? source_.time(row, p.field) : 0;
        if (t <= 0) return false;
        if (p.day > 0) return t >= p.day && t < p.day + kDay;
        if (p.days == 1) return t >= startOfDay(now());
        return t >= now() - static_cast<std::int64_t>(p.days) * kDay;
    }
    const std::string v = cell(row, p.field);
    return std::find(p.values.begin(), p.values.end(), v) != p.values.end();
}

bool FilterBar::matchesSearch(std::size_t row, const SearchQuery& q) const {
    if (q.empty()) return true;
    SearchQuery::Progress found;
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        if (!fields_[f].searchable) continue;
        q.feed(found, cell(row, f));
        if (q.hopeless(found) || (q.excluded().empty() && q.satisfied(found))) break;
    }
    if (source_.extra && !q.hopeless(found) && !(q.excluded().empty() && q.satisfied(found))) q.feed(found, source_.extra(row));
    return q.satisfied(found);
}

bool FilterBar::acceptsWith(const State& s, std::size_t row, int skipField) const {
    for (const auto& p : s.picks)
        if (static_cast<int>(p.field) != skipField && !fieldAccepts(p, row)) return false;
    if (&s == &state_) return matchesSearch(row, query_);
    return matchesSearch(row, SearchQuery(s.search));
}

bool FilterBar::accepts(std::size_t row) const { return acceptsWith(state_, row, -1); }
bool FilterBar::acceptsExcept(std::size_t row, int field) const { return acceptsWith(state_, row, field); }

std::size_t FilterBar::shownCount() const {
    if (table_) return table_->visibleRowCount();
    if (countDirty_) {
        countDirty_ = false;
        std::size_t n = 0;
        const std::size_t rows = rowCount();
        if (!state_.active()) n = rows;
        else
            for (std::size_t i = 0; i < rows; ++i) n += accepts(i) ? 1 : 0;
        shownCache_ = n;
    }
    return shownCache_;
}

std::size_t FilterBar::totalCount() const { return rowCount(); }

std::string FilterBar::countText() const { return std::to_string(shownCount()) + " sur " + std::to_string(totalCount()); }

std::size_t FilterBar::countFor(const State& s) const {
    std::size_t n = 0;
    const std::size_t rows = rowCount();
    const SearchQuery q(s.search);
    for (std::size_t i = 0; i < rows; ++i) {
        bool ok = true;
        for (const auto& p : s.picks)
            if (!fieldAccepts(p, i)) {
                ok = false;
                break;
            }
        if (ok && matchesSearch(i, q)) ++n;
    }
    return n;
}

int FilterBar::fieldByTitle(std::string_view title) const {
    const std::string want = foldForSearch(title);
    for (std::size_t i = 0; i < fields_.size(); ++i)
        if (foldForSearch(fields_[i].title) == want || (!fields_[i].chipLabel.empty() && foldForSearch(fields_[i].chipLabel) == want)) return static_cast<int>(i);
    return -1;
}

std::vector<std::pair<std::string, std::size_t>> FilterBar::valueCounts(std::size_t field) const {
    std::vector<std::pair<std::string, std::size_t>> out;
    if (field >= fields_.size()) return out;
    std::map<std::string, std::size_t> counts;
    const std::size_t rows = rowCount();
    for (std::size_t i = 0; i < rows; ++i)
        if (acceptsExcept(i, static_cast<int>(field))) ++counts[cell(i, field)];
    if (const Pick* p = pickOf(field))
        for (const auto& v : p->values) (void)counts[v];   // une valeur cochee reste (meme a 0)
    for (const auto& o : fields_[field].order)
        if (const auto it = counts.find(o); it != counts.end()) {
            out.emplace_back(it->first, it->second);
            counts.erase(it);
        }
    std::vector<std::pair<std::string, std::size_t>> rest(counts.begin(), counts.end());
    std::stable_sort(rest.begin(), rest.end(), [](const auto& a, const auto& b) {
        if (a.first.empty() != b.first.empty()) return b.first.empty();   // (vide) a la fin
        const auto fa = foldForSearch(a.first), fb = foldForSearch(b.first);
        return fa != fb ? fa < fb : a.first < b.first;
    });
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::vector<std::pair<int, std::size_t>> FilterBar::periodCounts(std::size_t field) const {
    std::vector<std::pair<int, std::size_t>> out;
    if (field >= fields_.size()) return out;
    std::size_t n[6] = {0, 0, 0, 0, 0, 0};
    const std::int64_t t0 = now(), today = startOfDay(t0);
    const std::size_t rows = rowCount();
    for (std::size_t i = 0; i < rows; ++i) {
        if (!acceptsExcept(i, static_cast<int>(field))) continue;
        ++n[5];
        const std::int64_t t = source_.time ? source_.time(i, field) : 0;
        if (t <= 0) continue;
        if (t >= today) ++n[0];
        for (int k = 1; k < 5; ++k)
            if (t >= t0 - static_cast<std::int64_t>(kPeriods[k]) * kDay) ++n[k];
    }
    for (int k = 0; k < 6; ++k) out.emplace_back(kPeriods[k], n[k]);
    return out;
}

std::string FilterBar::periodLabel(int days) {
    switch (days) {
        case 0: return "tout";
        case 1: return "aujourd'hui";
        case 365: return "12 mois";
        default: return std::to_string(days) + " j";
    }
}

std::string FilterBar::chipText(std::size_t field) const {
    if (field >= fields_.size()) return {};
    const Field& f = fields_[field];
    const std::string name = f.chipLabel.empty() ? f.title : f.chipLabel;
    const Pick* p = pickOf(field);
    if (!p || !p->active()) return name;
    if (f.kind == Kind::Period) return name + " : " + (p->day > 0 ? "le " + dayLabel(p->day) : periodLabel(p->days));
    std::string v;
    for (std::size_t i = 0; i < p->values.size() && i < 2; ++i) v += (i ? ", " : "") + shownValue(p->values[i]);
    if (p->values.size() > 2) v += " +" + std::to_string(p->values.size() - 2);
    return name + " : " + v;
}

std::vector<std::size_t> FilterBar::chipFields() const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < fields_.size(); ++i)
        if (fields_[i].chip) out.push_back(i);
    for (const auto& p : state_.picks)
        if (std::find(out.begin(), out.end(), p.field) == out.end() && p.field < fields_.size()) out.push_back(p.field);
    return out;
}

// ------------------------------------------------------------------------------------------ les gestes
void FilterBar::edited(bool remember) {
    query_ = SearchQuery(state_.search);
    countDirty_ = true;
    if (table_) {
        FilterChain chain;
        if (state_.active()) {
            std::weak_ptr<bool> alive = alive_;
            chain.addPredicate("barre", [this, alive](RowIndex r) { return alive.expired() || accepts(r); });
        }
        table_->setFilter(std::move(chain));
        table_->setHighlight(state_.search);
    }
    if (linked_) linked_->setHighlight(state_.search);
    syncChips();
    if (remember && !loading_) saveMemory();
    invalidateLayout();
    invalidate();
    if (menu_ != Menu::None) refreshMenu();
    changed->emit();
}

void FilterBar::setState(State s) {
    for (auto it = s.picks.begin(); it != s.picks.end();)
        it = it->field < fields_.size() ? it + 1 : s.picks.erase(it);
    state_ = std::move(s);
    if (search_->text() != state_.search) search_->setText(state_.search);   // textChanged : deja dans l'etat, rien
    edited();
}

void FilterBar::setSearch(std::string text) {
    if (search_->text() == text && state_.search == text) return;
    search_->setText(text);   // textChanged : edited
    if (state_.search != text) {
        state_.search = std::move(text);
        edited();
    }
}

void FilterBar::focusSearch() { search_->takeFocus(); }

void FilterBar::filterOn(std::size_t field, const std::string& value) {
    if (field >= fields_.size()) return;
    Pick* p = pickOf(field);
    if (!p) {
        state_.picks.push_back({field});
        p = &state_.picks.back();
    }
    p->values = {value};
    p->days = 0;
    p->day = 0;
    p->viaMenu = true;
    edited();
}

void FilterBar::filterOnDay(std::size_t field, std::int64_t when) {
    if (field >= fields_.size() || when <= 0) return;
    Pick* p = pickOf(field);
    if (!p) {
        state_.picks.push_back({field});
        p = &state_.picks.back();
    }
    p->values.clear();
    p->days = 0;
    p->day = startOfDay(when);
    p->viaMenu = true;
    edited();
}

void FilterBar::setValues(std::size_t field, std::vector<std::string> values) {
    if (field >= fields_.size()) return;
    Pick* p = pickOf(field);
    if (!p) {
        state_.picks.push_back({field});
        p = &state_.picks.back();
    }
    p->values = std::move(values);
    p->viaMenu = false;
    edited();
}

void FilterBar::toggleValue(std::size_t field, const std::string& value) {
    std::vector<std::string> v;
    if (const Pick* p = pickOf(field)) v = p->values;
    const auto it = std::find(v.begin(), v.end(), value);
    if (it == v.end()) v.push_back(value);
    else v.erase(it);
    setValues(field, std::move(v));
}

void FilterBar::setPeriod(std::size_t field, int days) {
    if (field >= fields_.size()) return;
    Pick* p = pickOf(field);
    if (!p) {
        state_.picks.push_back({field});
        p = &state_.picks.back();
    }
    p->days = std::max(0, days);
    p->day = 0;
    p->values.clear();
    p->viaMenu = false;
    edited();
}

void FilterBar::addChip(std::size_t field) {
    if (field >= fields_.size() || pickOf(field)) return;
    state_.picks.push_back({field});
    edited();
}

void FilterBar::clearField(std::size_t field) {
    // La puce s'en va (une puce d'office reste, sans filtre : chipFields la montre toujours).
    std::erase_if(state_.picks, [&](const Pick& p) { return p.field == field; });
    if (menu_ == Menu::Chip && menuField_ == field) closeMenu();
    edited();
}

void FilterBar::clearAll() {
    state_.picks.clear();
    if (!search_->text().empty()) {
        state_.search.clear();
        search_->setText({});   // textChanged : rien (deja vide dans l'etat)
    }
    state_.search.clear();
    closeMenu();
    edited();
}

// ------------------------------------------------------------------------------------------ Mes filtres
bool FilterBar::saveAs(const std::string& name) {
    std::string n = name;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    if (n.empty()) return false;
    State s = state_;
    for (auto& p : s.picks) p.viaMenu = false;
    for (auto& sv : saved_)
        if (sv.name == n) {
            sv.state = std::move(s);
            saveMemory();
            syncChips();
            invalidateLayout();
            invalidate();
            return true;
        }
    saved_.push_back({n, std::move(s)});
    saveMemory();
    syncChips();   // << Mes filtres >> dit le filtre choisi
    invalidateLayout();
    invalidate();
    return true;
}

bool FilterBar::applySaved(const std::string& name) {
    for (const auto& sv : saved_)
        if (sv.name == name) {
            setState(sv.state);
            return true;
        }
    return false;
}

bool FilterBar::removeSaved(const std::string& name) {
    const auto before = saved_.size();
    std::erase_if(saved_, [&](const Saved& s) { return s.name == name; });
    if (saved_.size() == before) return false;
    saveMemory();
    syncChips();
    invalidateLayout();
    invalidate();
    return true;
}

std::string FilterBar::currentSavedName() const {
    if (!state_.active()) return {};
    const std::string now = encodeState(state_);
    for (const auto& sv : saved_)
        if (encodeState(sv.state) == now) return sv.name;
    return {};
}

std::string FilterBar::proposedName() const {
    std::string n;
    for (const auto& p : state_.picks)
        if (p.active()) n += (n.empty() ? "" : " \xC2\xB7 ") + chipText(p.field);
    if (!SearchQuery(state_.search).empty()) n += (n.empty() ? "" : " \xC2\xB7 ") + std::string("\xC2\xAB ") + state_.search + " \xC2\xBB";
    return n;
}

// Le texte retenu : "b1;<recherche>;<puce>;<puce>..." - une puce : "<titre>,v,<valeur>,<valeur>..." ou
// "<titre>,p,<jours>,<jour>" (chaque champ echappe : FilterMemory).
std::string FilterBar::encodeState(const State& s) const {
    std::vector<std::string> parts{"b1", s.search};
    for (const auto& p : s.picks) {
        if (p.field >= fields_.size()) continue;
        std::vector<std::string> f{fields_[p.field].title};
        if (fields_[p.field].kind == Kind::Period) {
            f.push_back("p");
            f.push_back(std::to_string(p.days));
            f.push_back(std::to_string(p.day));
        } else {
            f.push_back("v");
            for (const auto& v : p.values) f.push_back(v);
        }
        parts.push_back(FilterMemory::join(f, ','));
    }
    return FilterMemory::join(parts, ';');
}

FilterBar::State FilterBar::decodeState(std::string_view text) const {
    State s;
    const auto parts = FilterMemory::split(text, ';');
    if (parts.empty() || parts[0] != "b1") return s;
    if (parts.size() > 1) s.search = parts[1];
    for (std::size_t i = 2; i < parts.size(); ++i) {
        const auto f = FilterMemory::split(parts[i], ',');
        if (f.size() < 2) continue;
        const int field = fieldByTitle(f[0]);
        if (field < 0) continue;   // une colonne qui n'existe plus : ignoree
        Pick p;
        p.field = static_cast<std::size_t>(field);
        if (f[1] == "p" && f.size() >= 4) {
            p.days = std::atoi(f[2].c_str());
            p.day = std::atoll(f[3].c_str());
        } else if (f[1] == "v") {
            p.values.assign(f.begin() + 2, f.end());
        }
        s.picks.push_back(std::move(p));
    }
    return s;
}

void FilterBar::loadMemory() {
    if (memoryKey_.empty() || !FilterMemory::installed()) return;
    loading_ = true;
    saved_.clear();
    const auto list = FilterMemory::split(FilterMemory::read(memoryKey_ + ".mes"), '|');
    for (std::size_t i = 0; i + 1 < list.size(); i += 2)
        if (!list[i].empty()) saved_.push_back({list[i], decodeState(list[i + 1])});
    const std::string kept = FilterMemory::read(memoryKey_);
    if (!kept.empty()) setState(decodeState(kept));
    loading_ = false;
    invalidate();
}

void FilterBar::saveMemory() const {
    if (memoryKey_.empty() || !FilterMemory::installed()) return;
    FilterMemory::write(memoryKey_, state_.active() ? encodeState(state_) : std::string());
    std::vector<std::string> list;
    for (const auto& sv : saved_) {
        list.push_back(sv.name);
        list.push_back(encodeState(sv.state));
    }
    FilterMemory::write(memoryKey_ + ".mes", list.empty() ? std::string() : FilterMemory::join(list, '|'));
}

// ------------------------------------------------------------------------------------------ les menus
gfx::Rect FilterBar::anchorOf(std::size_t field) const {
    if (field < chips_.size() && chips_[field]->visible()) return chips_[field]->bounds();
    return add_->visible() ? add_->bounds() : bounds();
}

void FilterBar::openChipMenu(std::size_t field, gfx::Rect anchor) {
    if (field >= fields_.size()) return;
    // Une colonne sans puce (l'entonnoir d'un titre, + Filtre) : sa puce apparait.
    if (!fields_[field].chip && !pickOf(field)) {
        state_.picks.push_back({field});
        syncChips();
        invalidateLayout();
        layout();
    }
    menu_ = Menu::Chip;
    menuField_ = field;
    naming_ = managing_ = false;
    FilterBarPopup::Spec spec;
    spec.anchor = anchor.empty() ? anchorOf(field) : anchor;
    spec.keepOpen = fields_[field].kind != Kind::Period;
    spec.width = 248.f;
    if (fields_[field].kind != Kind::Period) {
        spec.searchHint = "Chercher " + (fields_[field].noun.empty() ? std::string("une valeur") : fields_[field].noun) + "\xE2\x80\xA6";
        spec.links = {"Tout cocher", "Aucune"};
    }
    popup_->open(std::move(spec));
    refreshMenu();
    invalidate();
}

void FilterBar::openAddMenu() {
    menu_ = Menu::Add;
    naming_ = managing_ = false;
    FilterBarPopup::Spec spec;
    spec.anchor = add_->bounds();
    spec.searchHint = "Filtrer sur la colonne\xE2\x80\xA6";
    spec.width = 240.f;
    popup_->open(std::move(spec));
    refreshMenu();
    invalidate();
}

void FilterBar::openSavedMenu() {
    menu_ = Menu::Saved;
    naming_ = managing_ = false;
    FilterBarPopup::Spec spec;
    spec.anchor = savedBtn_->bounds();
    std::string noun = noun_.empty() ? id() : noun_;
    for (auto& ch : noun)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    spec.title = "MES FILTRES \xC2\xB7 " + noun;
    spec.width = 290.f;
    popup_->open(std::move(spec));
    refreshMenu();
    invalidate();
}

void FilterBar::closeMenu() {
    if (popup_->isOpen()) popup_->close();
    menu_ = Menu::None;
    naming_ = managing_ = false;
    invalidate();
}

void FilterBar::refreshMenu() {
    if (!popup_->isOpen()) {
        menu_ = Menu::None;
        return;
    }
    std::vector<FilterBarPopup::Row> rows;
    menuValues_.clear();
    std::string note;
    if (menu_ == Menu::Chip && menuField_ < fields_.size()) {
        const Pick* p = pickOf(menuField_);
        if (fields_[menuField_].kind == Kind::Period) {
            static const char* const kLabels[6] = {"Aujourd'hui", "7 derniers jours", "30 derniers jours", "90 derniers jours", "12 derniers mois", "Tout"};
            const auto counts = periodCounts(menuField_);
            for (int k = 0; k < 6; ++k) {
                FilterBarPopup::Row r;
                r.label = kLabels[k];
                r.count = std::to_string(counts[static_cast<std::size_t>(k)].second);
                r.mark = 3;
                r.on = p ? (p->day == 0 && p->days == kPeriods[k]) : kPeriods[k] == 0;
                rows.push_back(std::move(r));
                menuValues_.push_back(std::to_string(kPeriods[k]));
            }
            if (p && p->day > 0) {
                FilterBarPopup::Row r;
                r.label = "Le " + dayLabel(p->day);
                r.mark = 3;
                r.on = true;
                rows.insert(rows.begin(), std::move(r));
                menuValues_.insert(menuValues_.begin(), "jour");
            }
        } else {
            for (const auto& [v, n] : valueCounts(menuField_)) {
                FilterBarPopup::Row r;
                r.label = shownValue(v);
                r.count = std::to_string(n);
                r.mark = 1;
                r.on = p && std::find(p->values.begin(), p->values.end(), v) != p->values.end();
                rows.push_back(std::move(r));
                menuValues_.push_back(v);
            }
            note = "sur les " + std::to_string(rowCount());
        }
    } else if (menu_ == Menu::Add) {
        const auto shown = chipFields();
        for (std::size_t f = 0; f < fields_.size(); ++f) {
            if (fields_[f].kind == Kind::Text || std::find(shown.begin(), shown.end(), f) != shown.end()) continue;
            FilterBarPopup::Row r;
            r.label = fields_[f].title;
            r.icon = fields_[f].kind == Kind::Period ? Icon::History : Icon::Filter;
            if (fields_[f].kind == Kind::Values) r.count = std::to_string(valueCounts(f).size());
            rows.push_back(std::move(r));
            menuValues_.push_back(std::to_string(f));
        }
        if (rows.empty()) note = "toutes les colonnes ont leur puce";
    } else if (menu_ == Menu::Saved) {
        const std::string current = currentSavedName();
        for (const auto& sv : saved_) {
            FilterBarPopup::Row r;
            r.label = sv.name;
            r.count = std::to_string(countFor(sv.state));
            r.mark = 2;
            r.on = sv.name == current;
            r.tick = sv.name == current;
            r.removable = managing_;
            rows.push_back(std::move(r));
            menuValues_.push_back(sv.name);
        }
        if (saved_.empty() && !naming_) note = "aucun pour l'instant";
        // Le menu refait en entier : le champ du nom (Enregistrer ce filtre...), les liens du pied.
        FilterBarPopup::Spec spec = popup_->spec();
        const bool wasNaming = spec.nameField;
        spec.nameField = naming_;
        spec.searchHint = naming_ ? std::string("Le nom de ce filtre (Entr\xC3\xA9" "e)") : std::string();
        spec.text = naming_ ? (wasNaming ? popup_->fieldText() : proposedName()) : std::string();
        spec.links = {naming_ ? std::string("Enregistrer") : std::string("Enregistrer ce filtre\xE2\x80\xA6"),
                      managing_ ? std::string("Termin\xC3\xA9") : std::string("G\xC3\xA9rer\xE2\x80\xA6")};
        spec.rows = std::move(rows);
        spec.note = std::move(note);
        popup_->open(std::move(spec));
        return;
    }
    popup_->setRows(std::move(rows), std::move(note));
}

void FilterBar::popupRow(std::size_t index) {
    if (index >= menuValues_.size()) return;
    const std::string v = menuValues_[index];
    switch (menu_) {
        case Menu::Chip:
            if (fields_[menuField_].kind == Kind::Period) {
                if (v != "jour") setPeriod(menuField_, std::atoi(v.c_str()));
                closeMenu();
            } else {
                toggleValue(menuField_, v);
            }
            break;
        case Menu::Add: {
            const std::size_t f = static_cast<std::size_t>(std::atoi(v.c_str()));
            closeMenu();
            addChip(f);
            layout();
            openChipMenu(f);
            break;
        }
        case Menu::Saved:
            if (!managing_) {
                (void)applySaved(v);
                closeMenu();
            }
            break;
        case Menu::None: break;
    }
}

void FilterBar::popupFooter(std::size_t index) {
    if (menu_ == Menu::Chip) {
        if (index == 0) {   // Tout cocher : celles que la recherche du menu montre
            std::vector<std::string> v;
            if (const Pick* p = pickOf(menuField_)) v = p->values;
            for (const std::size_t i : popup_->shownRows())
                if (i < menuValues_.size() && std::find(v.begin(), v.end(), menuValues_[i]) == v.end()) v.push_back(menuValues_[i]);
            setValues(menuField_, std::move(v));
        } else {
            setValues(menuField_, {});
        }
        return;
    }
    if (menu_ == Menu::Saved) {
        if (index == 0) {
            if (naming_) {
                popupEnter(popup_->fieldText());
                return;
            }
            if (!state_.active()) return;   // rien a enregistrer (le lien le dit : grise)
            naming_ = true;
            managing_ = false;
        } else {
            managing_ = !managing_;
            naming_ = false;
        }
        refreshMenu();
    }
}

void FilterBar::popupDelete(std::size_t index) {
    if (menu_ != Menu::Saved || index >= menuValues_.size()) return;
    (void)removeSaved(menuValues_[index]);
    refreshMenu();
}

void FilterBar::popupEnter(const std::string& text) {
    if (menu_ == Menu::Saved && naming_) {
        if (saveAs(text)) {
            naming_ = false;
            refreshMenu();
        }
    }
}

// ------------------------------------------------------------------------------------------ la mise en page
void FilterBar::syncChips() {
    const auto shown = chipFields();
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const bool on = std::find(shown.begin(), shown.end(), i) != shown.end();
        chips_[i]->setText(chipText(i));
        chips_[i]->setTooltip(fieldActive(i) ? (fields_[i].kind == Kind::Period ? std::string("Changer la p\xC3\xA9riode") : std::string("La croix : enlever ce filtre"))
                                             : "Filtrer sur \xC2\xAB " + fields_[i].title + " \xC2\xBB");
        if (!on) chips_[i]->setVisibility(Visibility::Collapsed);
    }
    const std::string current = currentSavedName();
    savedBtn_->setText(current.empty() ? std::string("Mes filtres") : current);
}

void FilterBar::layoutRow(float width, std::vector<Placed>* out, float* height) const {
    const float x0 = bounds().x, y0 = bounds().y;
    const float searchW = std::clamp(width * 0.2f, 170.f, 250.f);
    // A droite : le compte, Effacer, Mes filtres.
    const float savedW = std::clamp(static_cast<const Chip*>(savedBtn_)->width(), 120.f, 230.f);
    const bool clearShown = state_.active();
    const float clearW = static_cast<const Chip*>(clear_)->width();
    const float countW = measureWidth(countText(), gfx::semibold(kSmall)) + 8.f;
    float right = x0 + width - savedW;
    if (out) out->push_back({savedBtn_, {right, y0 + 1.f, savedW, kRowH - 2.f}});
    if (clearShown) {
        right -= clearW + 8.f;
        if (out) out->push_back({clear_, {right, y0 + 2.f, clearW, kRowH - 4.f}});
    }
    right -= countW + 8.f;
    const float countX = right;
    float x = x0;
    if (out) out->push_back({search_, {x, y0, searchW, kRowH}});
    x += searchW + 10.f;
    float limit = countX - 8.f, y = y0;
    int row = 0;
    std::vector<const Widget*> items;
    for (const std::size_t f : chipFields()) items.push_back(chips_[f]);
    items.push_back(add_);
    for (const Widget* w : items) {
        const float iw = std::min(static_cast<const Chip*>(w)->width(), std::max(80.f, width - 20.f));
        if (x + iw > limit && x > x0 + 1.f) {
            if (row == 1) {   // plus de place : la puce attend (elle reste dans + Filtre)
                if (out) out->push_back({const_cast<Widget*>(w), {}});
                continue;
            }
            ++row;
            y = y0 + kRowH + kRowGap;
            x = x0;
            limit = x0 + width;
        }
        if (out) out->push_back({const_cast<Widget*>(w), {x, y + 2.f, iw, kRowH - 4.f}});
        x += iw + kItemGap;
    }
    if (height) *height = row == 0 ? kRowH : kRowH * 2.f + kRowGap;
    if (out) out->push_back({nullptr, {countX, y0, countW, kRowH}});
}

float FilterBar::heightFor(float width) const {
    float h = kRowH;
    // la mise en page a cette largeur (bounds().x / y sans importance pour la hauteur)
    layoutRow(width, nullptr, &h);
    return h;
}

SizeHint FilterBar::sizeHint() const {
    SizeHint h;
    h.preferred = {bounds().w, heightFor(bounds().w > 0.f ? bounds().w : 900.f)};
    h.minimum = h.preferred;
    return h;
}

void FilterBar::onLayout() {
    std::vector<Placed> placed;
    float h = 0.f;
    layoutRow(bounds().w, &placed, &h);
    for (auto* c : chips_) c->setVisibility(Visibility::Collapsed);
    clear_->setVisibility(Visibility::Collapsed);
    for (const auto& p : placed) {
        if (!p.widget) {
            countRect_ = p.rect;
            continue;
        }
        if (p.rect.empty()) {
            p.widget->setVisibility(Visibility::Collapsed);
            continue;
        }
        p.widget->setVisibility(Visibility::Visible);
        p.widget->setBounds(p.rect);
    }
    // La croix de la recherche, dans le champ.
    for (const auto& ch : children())
        if (auto* c = dynamic_cast<Chip*>(ch.get()); c && c->role() == Chip::Role::ClearSearch) {
            const gfx::Rect s = search_->bounds();
            c->setVisibility(state_.search.empty() ? Visibility::Collapsed : Visibility::Visible);
            c->setBounds({s.right() - 24.f, s.y + 2.f, 20.f, s.h - 4.f});
        }
    popup_->setBounds(bounds());
}

void FilterBar::onPaint(const PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    // << 2 sur 214 >> : le nombre en gras.
    if (countRect_.w > 0.f) {
        const std::string shown = std::to_string(shownCount()), rest = " sur " + std::to_string(totalCount());
        const gfx::FontId fb = gfx::semibold(kSmall);
        const float w = r.measure(shown, fb).width + r.measure(rest, kSmall).width;
        float x = countRect_.right() - w;
        const float y = countRect_.y + (countRect_.h - r.lineHeight(kSmall)) * 0.5f;
        r.drawText({x, y}, shown, fb, c.text);
        x += r.measure(shown, fb).width;
        r.drawText({x, y}, rest, kSmall, c.textMuted);
    }
}

void FilterBar::onPaintOverlay(const PaintContext& ctx) {
    // La loupe et << Ctrl+F >> (ou << 2 trouvees >>) par-dessus le champ (il a garde la place : son retrait).
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const gfx::Rect s = search_->bounds();
    if (s.empty() || !search_->visible()) return;
    drawIcon(r, Icon::Search, {s.x + 9.f, s.y + (s.h - 14.f) * 0.5f, 14.f, 14.f}, search_->focused() ? c.accent : c.textMuted);
    if (state_.search.empty()) {
        const gfx::FontId fk = gfx::FontId{10};
        const float kw = r.measure("Ctrl+F", fk).width + 10.f;
        const gfx::Rect k{s.right() - kw - 8.f, s.y + (s.h - 18.f) * 0.5f, kw, 18.f};
        shapes::fillFramedRect(r, k, c.panelBg, c.border, 4.f);
        r.drawText({k.x + 5.f, k.y + (k.h - r.lineHeight(fk)) * 0.5f}, "Ctrl+F", fk, c.textMuted);
    } else {
        const std::size_t n = shownCount();
        const std::string t = std::to_string(n) + (n > 1 ? " trouv\xC3\xA9" "es" : " trouv\xC3\xA9" "e");
        const float tw = r.measure(t, kTiny).width;
        r.drawText({s.right() - 28.f - tw, s.y + (s.h - r.lineHeight(kTiny)) * 0.5f}, t, kTiny, c.textMuted);
    }
}

EventResult FilterBar::onEvent(const InputEvent& ev) {
    // Ctrl+F : la recherche de la liste montree (la page cachee ne recoit rien).
    if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::F && k->mods.ctrl && !k->mods.alt && !k->mods.shift) {
        focusSearch();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

Widget* FilterBar::chipWidget(std::size_t field) const { return field < chips_.size() ? chips_[field] : nullptr; }

// ================================================================================== le menu
FilterBarPopup::FilterBarPopup(std::string id) : Widget(std::move(id)) {}

void FilterBarPopup::open(Spec spec) {
    const bool reopen = open_;
    const float oldScroll = static_cast<float>(scroll_);
    const std::string keep = text_;
    const bool sameField = reopen && spec_.searchHint == spec.searchHint && spec_.nameField == spec.nameField;
    spec_ = std::move(spec);
    text_ = spec_.nameField ? spec_.text : (sameField ? keep : std::string());
    scroll_ = sameField ? static_cast<int>(oldScroll) : 0;
    hover_ = -1;
    open_ = true;
    if (const auto s = surfaceSize(); s.w > 0.f) surface_ = s;
    invalidate();
}

void FilterBarPopup::setRows(std::vector<Row> rows, std::string note) {
    spec_.rows = std::move(rows);
    spec_.note = std::move(note);
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(shownRows().size()) - visibleRows()));
    invalidate();
}

void FilterBarPopup::close() {
    if (!open_) return;
    open_ = false;
    hover_ = -1;
    invalidate();
    if (onClose) onClose();
}

void FilterBarPopup::setFieldText(std::string t) {
    text_ = std::move(t);
    scroll_ = 0;
    invalidate();
}

std::vector<std::size_t> FilterBarPopup::shownRows() const {
    std::vector<std::size_t> out;
    const SearchQuery q(spec_.nameField ? std::string() : text_);
    for (std::size_t i = 0; i < spec_.rows.size(); ++i)
        if (q.empty() || q.matches({spec_.rows[i].label})) out.push_back(i);
    return out;
}

int FilterBarPopup::visibleRows() const { return 9; }

float FilterBarPopup::rowsTop() const {
    const gfx::Rect p = popupRect();
    float y = p.y + 6.f;
    if (!spec_.title.empty()) y += 24.f;
    if (!spec_.searchHint.empty() || spec_.nameField) y += 34.f;
    return y;
}

gfx::Rect FilterBarPopup::popupRect() const {
    const int rows = std::min(visibleRows(), static_cast<int>(shownRows().size()));
    float h = 12.f + (spec_.title.empty() ? 0.f : 24.f) + (!spec_.searchHint.empty() || spec_.nameField ? 34.f : 0.f) + 28.f * static_cast<float>(rows);
    if (!spec_.links.empty() || !spec_.note.empty()) h += 36.f;
    float x = spec_.anchor.x, y = spec_.anchor.bottom() + 4.f;
    const gfx::Size s = surface_.w > 0.f ? surface_ : surfaceSize();
    if (s.w > 0.f && x + spec_.width > s.w - 8.f) x = std::max(8.f, s.w - 8.f - spec_.width);
    if (s.h > 0.f && y + h > s.h - 8.f) y = std::max(8.f, spec_.anchor.y - 4.f - h);
    return {x, y, spec_.width, h};
}

gfx::Rect FilterBarPopup::rowRect(std::size_t shownIndex) const {
    const int k = static_cast<int>(shownIndex) - scroll_;
    if (k < 0 || k >= visibleRows()) return {};
    const gfx::Rect p = popupRect();
    return {p.x + 6.f, rowsTop() + 28.f * static_cast<float>(k), p.w - 12.f, 28.f};
}

gfx::Rect FilterBarPopup::linkRect(std::size_t index) const {
    if (index >= spec_.links.size()) return {};
    const gfx::Rect p = popupRect();
    float x = p.x + 12.f;
    for (std::size_t i = 0; i < index; ++i) x += measureWidth(spec_.links[i], gfx::semibold(kSmall)) + 22.f;
    return {x - 4.f, p.bottom() - 34.f, measureWidth(spec_.links[index], gfx::semibold(kSmall)) + 8.f, 28.f};
}

gfx::Rect FilterBarPopup::eventBounds() const {
    if (!open_) return {};
    const gfx::Size s = surface_.w > 0.f ? surface_ : surfaceSize();
    if (s.w > 0.f && s.h > 0.f) return {0.f, 0.f, s.w, s.h};
    return popupRect();
}

void FilterBarPopup::onPaintOverlay(const PaintContext& ctx) {
    if (!open_) return;
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    surface_ = r.surfaceSize();
    const gfx::Rect p = popupRect();
    r.fillRoundedRect({p.x + 1.f, p.y + 4.f, p.w, p.h}, gfx::Color{15, 23, 42, 40}, 12.f);
    shapes::fillFramedRect(r, p, c.panelBg, c.borderStrong, 11.f);
    float y = p.y + 6.f;
    if (!spec_.title.empty()) {
        r.drawText({p.x + 12.f, y + 6.f}, elide(r, spec_.title, gfx::semibold(kTiny), p.w - 24.f), gfx::semibold(kTiny), c.textMuted);
        y += 24.f;
    }
    if (!spec_.searchHint.empty() || spec_.nameField) {
        const gfx::Rect f{p.x + 6.f, y, p.w - 12.f, 28.f};
        shapes::fillFramedRect(r, f, c.inputBg, spec_.nameField ? c.accent : c.border, 7.f);
        float tx = f.x + 8.f;
        if (!spec_.nameField) {
            drawIcon(r, Icon::Search, {f.x + 8.f, f.y + 7.f, 14.f, 14.f}, c.textMuted);
            tx += 20.f;
        }
        const float ty = f.y + (f.h - r.lineHeight(kSmall)) * 0.5f;
        if (text_.empty()) r.drawText({tx, ty}, elide(r, spec_.searchHint, kSmall, f.right() - tx - 6.f), kSmall, c.textMuted);
        else {
            const std::string t = elide(r, text_, kSmall, f.right() - tx - 10.f);
            r.drawText({tx, ty}, t, kSmall, c.text);
            const float cx = tx + r.measure(t, kSmall).width + 1.f;
            r.line({cx, f.y + 6.f}, {cx, f.bottom() - 6.f}, c.accent, 1.f);
        }
    }
    const auto shown = shownRows();
    for (int k = 0; k < visibleRows(); ++k) {
        const std::size_t si = static_cast<std::size_t>(scroll_ + k);
        if (si >= shown.size()) break;
        const Row& row = spec_.rows[shown[si]];
        const gfx::Rect rr = rowRect(si);
        if (hover_ == static_cast<int>(si) || row.tick) r.fillRoundedRect(rr, c.accent.withAlpha(row.tick ? 22 : 16), 7.f);
        float x = rr.x + 8.f;
        const float cy = rr.y + rr.h * 0.5f;
        if (row.mark == 1) {
            const gfx::Rect box{x, cy - 7.f, 14.f, 14.f};
            if (row.on) {
                r.fillRoundedRect(box, c.accent, 3.f);
                r.line({box.x + 3.f, cy}, {box.x + 6.f, cy + 3.f}, gfx::Color::rgb(0xFFFFFF), 1.6f);
                r.line({box.x + 6.f, cy + 3.f}, {box.x + 11.f, cy - 3.f}, gfx::Color::rgb(0xFFFFFF), 1.6f);
            } else {
                shapes::fillFramedRect(r, box, c.panelBg, c.borderStrong, 3.f);
            }
            x += 22.f;
        } else if (row.mark == 2) {
            drawIcon(r, row.on ? Icon::StarFilled : Icon::Star, {x, cy - 7.f, 14.f, 14.f}, row.on ? c.warning : c.textMuted);
            x += 22.f;
        } else if (row.mark == 3) {
            r.fillRoundedRect({x, cy - 7.f, 14.f, 14.f}, row.on ? c.accent : c.borderStrong, 7.f);
            r.fillRoundedRect({x + (row.on ? 4.f : 1.5f), cy - (row.on ? 3.f : 5.5f), row.on ? 6.f : 11.f, row.on ? 6.f : 11.f}, c.panelBg, row.on ? 3.f : 5.5f);
            x += 22.f;
        } else if (row.icon != Icon::None) {
            drawIcon(r, row.icon, {x, cy - 7.f, 14.f, 14.f}, c.textMuted);
            x += 22.f;
        }
        float right = rr.right() - 8.f;
        if (row.removable) {
            cross(r, right - 5.f, cy, 4.f, c.error);
            right -= 18.f;
        } else if (row.tick) {
            r.line({right - 11.f, cy}, {right - 7.f, cy + 4.f}, c.accent, 1.6f);
            r.line({right - 7.f, cy + 4.f}, {right, cy - 4.f}, c.accent, 1.6f);
            right -= 18.f;
        }
        if (!row.count.empty()) {
            const float cw = r.measure(row.count, kSmall).width;
            r.drawText({right - cw, cy - r.lineHeight(kSmall) * 0.5f}, row.count, kSmall, c.textMuted);
            right -= cw + 10.f;
        }
        const gfx::FontId f = row.on && row.mark == 1 ? gfx::semibold(kFont) : kFont;
        r.drawText({x, cy - r.lineHeight(f) * 0.5f}, elide(r, row.label, f, right - x), f, c.text);
    }
    if (shown.size() > static_cast<std::size_t>(visibleRows())) {   // une barre fine : la place dans la liste
        const float top = rowsTop(), h = 28.f * static_cast<float>(visibleRows());
        const float th = std::max(18.f, h * static_cast<float>(visibleRows()) / static_cast<float>(shown.size()));
        const float ty = top + (h - th) * static_cast<float>(scroll_) / static_cast<float>(shown.size() - static_cast<std::size_t>(visibleRows()));
        r.fillRoundedRect({p.right() - 5.f, ty, 3.f, th}, c.scrollbar, 1.5f);
    }
    if (!spec_.links.empty() || !spec_.note.empty()) {
        r.fillRect({p.x + 6.f, p.bottom() - 38.f, p.w - 12.f, 1.f}, c.border);
        for (std::size_t i = 0; i < spec_.links.size(); ++i) {
            const gfx::Rect lr = linkRect(i);
            if (i > 0) r.drawText({lr.x - 12.f, lr.y + (lr.h - r.lineHeight(kSmall)) * 0.5f}, "\xC2\xB7", kSmall, c.textMuted);
            r.drawText({lr.x + 4.f, lr.y + (lr.h - r.lineHeight(kSmall)) * 0.5f}, spec_.links[i], gfx::semibold(kSmall), c.accentPressed);
        }
        if (!spec_.note.empty()) {
            const float nw = r.measure(spec_.note, kSmall).width;
            r.drawText({p.right() - 12.f - nw, p.bottom() - 34.f + (28.f - r.lineHeight(kSmall)) * 0.5f}, spec_.note, kSmall, c.textMuted);
        }
    }
}

EventResult FilterBarPopup::onEvent(const InputEvent& ev) {
    if (!open_) return EventResult::Ignored;
    const gfx::Rect p = popupRect();
    const auto shown = shownRows();
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < shown.size(); ++i)
            if (rowRect(i).contains(m->pos)) h = static_cast<int>(i);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return p.contains(m->pos) ? EventResult::Consumed : EventResult::Ignored;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!p.contains(w->pos)) return EventResult::Ignored;
        scroll_ = std::clamp(scroll_ + (w->dy > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(shown.size()) - visibleRows()));
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!p.contains(d->pos)) {   // a cote : il se ferme
            close();
            return EventResult::Consumed;
        }
        if (d->button != MouseButton::Left) return EventResult::Consumed;
        for (std::size_t i = 0; i < spec_.links.size(); ++i)
            if (linkRect(i).contains(d->pos)) {
                if (onLink) onLink(i);
                return EventResult::Consumed;
            }
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const gfx::Rect rr = rowRect(i);
            if (!rr.contains(d->pos)) continue;
            const std::size_t row = shown[i];
            if (spec_.rows[row].removable && d->pos.x > rr.right() - 26.f) {
                if (onRemove) onRemove(row);
                return EventResult::Consumed;
            }
            if (onRow) onRow(row);
            return EventResult::Consumed;
        }
        return EventResult::Consumed;
    }
    if (std::get_if<MouseUp>(&ev)) return EventResult::Ignored;
    if (const auto* t = std::get_if<TextInput>(&ev)) {
        if (spec_.searchHint.empty() && !spec_.nameField) return EventResult::Consumed;
        text_ += t->utf8;
        scroll_ = 0;
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        switch (k->key) {
            case Key::Escape: close(); return EventResult::Consumed;
            case Key::Backspace:
                if (!text_.empty()) {
                    while (!text_.empty() && (static_cast<unsigned char>(text_.back()) & 0xC0) == 0x80) text_.pop_back();
                    if (!text_.empty()) text_.pop_back();
                    scroll_ = 0;
                    invalidate();
                }
                return EventResult::Consumed;
            case Key::Down:
            case Key::Up:
                if (!shown.empty()) {
                    hover_ = std::clamp(hover_ + (k->key == Key::Down ? 1 : -1), 0, static_cast<int>(shown.size()) - 1);
                    if (hover_ < scroll_) scroll_ = hover_;
                    if (hover_ >= scroll_ + visibleRows()) scroll_ = hover_ - visibleRows() + 1;
                    invalidate();
                }
                return EventResult::Consumed;
            case Key::Return:
                if (spec_.nameField) {
                    if (onEnter) onEnter(text_);
                } else if (hover_ >= 0 && static_cast<std::size_t>(hover_) < shown.size()) {
                    if (onRow) onRow(shown[static_cast<std::size_t>(hover_)]);
                } else if (shown.size() == 1) {
                    if (onRow) onRow(shown.front());
                }
                return EventResult::Consumed;
            default: return EventResult::Consumed;   // le menu ouvert garde le clavier
        }
    }
    return EventResult::Ignored;
}

} // namespace ui
