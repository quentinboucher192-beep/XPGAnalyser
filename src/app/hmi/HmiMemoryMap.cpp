// app/hmi/HmiMemoryMap.cpp - la carte memoire d'un equipement (lot 17).
#include "HmiMemoryMap.hpp"

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace zn = hmi::zones;
using hmi::MemTable;

namespace {

const gfx::FontId kTiny{11};
const gfx::FontId kSmall{12};
const gfx::FontId kBody{14};

constexpr float kBannerH = 28.f;
constexpr float kHeaderH = 30.f;
constexpr float kColsH = 20.f;
constexpr float kLineH = 42.f;
constexpr float kFoldH = 24.f;
constexpr float kGapH = 24.f;
constexpr float kAddrW = 104.f;
constexpr float kScrollW = 8.f;

const MemTable kOrder[] = {MemTable::Holding, MemTable::InputRegisters, MemTable::Coils, MemTable::DiscreteInputs};

std::uint64_t keyOf(MemTable t, std::uint32_t o) { return (static_cast<std::uint64_t>(t) << 32) | o; }

gfx::Color alpha(gfx::Color c, int a) {
    c.a = static_cast<std::uint8_t>(std::clamp(a, 0, 255));
    return c;
}

gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
    return gfx::Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

float textW(gfx::IRenderer& r, std::string_view s, gfx::FontId f) { return r.measure(s, f).width; }

void bold(gfx::IRenderer& r, gfx::Point p, std::string_view s, gfx::FontId f, gfx::Color c) {
    r.drawText(p, s, f, c);
    r.drawText({p.x + 0.6f, p.y}, s, f, c);
}

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (s.empty() || w <= 4 || textW(r, s, f) <= w) return w <= 4 ? std::string{} : s;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(s, f, std::max(0.f, w - textW(r, dots, f)));
    std::size_t cut = std::min(n, s.size());
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + dots;
}

// Des hachures a 45 degres dans un rectangle (hors zone, pas dans les zones).
void hatch(gfx::IRenderer& r, gfx::Rect b, gfx::Color c, float step = 7.f) {
    r.pushClip(b);
    for (float x = b.x - b.h; x < b.x + b.w; x += step) r.line({x, b.y + b.h}, {x + b.h, b.y}, c, 1.2f);
    r.popClip();
}

// Des rayures (partage en lecture).
void stripes(gfx::IRenderer& r, gfx::Rect b, gfx::Color c) {
    r.pushClip(b);
    for (float x = b.x - b.h; x < b.x + b.w; x += 9.f) r.line({x, b.y + b.h}, {x + b.h, b.y}, c, 3.f);
    r.popClip();
}

void dashedRect(gfx::IRenderer& r, gfx::Rect b, gfx::Color c, float width = 1.4f) {
    const float dash = 5, gap = 4;
    for (float x = b.x; x < b.x + b.w; x += dash + gap) {
        const float e = std::min(x + dash, b.x + b.w);
        r.line({x, b.y}, {e, b.y}, c, width);
        r.line({x, b.y + b.h}, {e, b.y + b.h}, c, width);
    }
    for (float y = b.y; y < b.y + b.h; y += dash + gap) {
        const float e = std::min(y + dash, b.y + b.h);
        r.line({b.x, y}, {b.x, e}, c, width);
        r.line({b.x + b.w, y}, {b.x + b.w, e}, c, width);
    }
}

// La vague : "elle change".
void wave(gfx::IRenderer& r, float x, float cy, gfx::Color c) {
    gfx::Point prev{x, cy};
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.f;
        const gfx::Point p{x + t * 10.f, cy + std::sin(t * 6.2831853f) * 2.6f};
        r.line(prev, p, c, 1.4f);
        prev = p;
    }
}

bool inside(const gfx::Rect& r, gfx::Point p) { return r.w > 0 && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

std::string lowerOf(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    // Les lettres accentuees (UTF-8, deux octets) : e aigu, e grave, e circonflexe.
    std::string res;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c == 0xC3 && i + 1 < out.size()) {
            const unsigned char d = static_cast<unsigned char>(out[i + 1]);
            res.push_back(static_cast<char>(c));
            res.push_back(static_cast<char>(d >= 0xA0 && d <= 0xBE ? d - 0x20 : d));
            ++i;
            continue;
        }
        res.push_back(static_cast<char>(std::toupper(c)));
    }
    return res;
}

std::string lastPart(const std::string& s) {
    const auto dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(dot + 1);
}

// Le nombre de cases : "40 mots", "1 bit".
std::string cellsText(MemTable t, std::uint32_t n) {
    const bool bits = zn::isBits(t);
    return std::to_string(n) + (bits ? (n > 1 ? " bits" : " bit") : (n > 1 ? " mots" : " mot"));
}

} // namespace

HmiMemoryMap::HmiMemoryMap(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
}

ui::SizeHint HmiMemoryMap::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {1000.f, 600.f};
    h.minimum = {300.f, 200.f};
    h.stretchX = h.stretchY = 1.f;
    return h;
}

void HmiMemoryMap::setMap(zn::MemoryMap map, hmi::MemZones shown) {
    map_ = std::move(map);
    shown_ = std::move(shown);
    at_.clear();
    worst_.clear();
    for (std::size_t i = 0; i < map_.vars.size(); ++i) {
        const auto& v = map_.vars[i];
        for (std::uint32_t o = v.first; o <= v.last && o - v.first < 70000; ++o) at_[keyOf(v.table, o)].push_back(static_cast<std::uint32_t>(i));
    }
    for (const auto& c : map_.conflicts)
        for (std::uint32_t o = c.first; o <= c.last && o - c.first < 70000; ++o) {
            int& w = worst_[keyOf(c.table, o)];
            w = std::max(w, c.severity);
        }
    for (std::size_t i = 0; i < map_.vars.size(); ++i)
        if (map_.vars[i].outOfZone)
            for (std::uint32_t o = map_.vars[i].first; o <= map_.vars[i].last && o - map_.vars[i].first < 70000; ++o) worst_[keyOf(map_.vars[i].table, o)] = 4;
    rebuild();
}

void HmiMemoryMap::setLive(MemCellFn cells, std::vector<MemVarLive> vars, int passes) {
    cells_ = std::move(cells);
    varLive_ = std::move(vars);
    passes_ = passes;
    rebuild();
}

void HmiMemoryMap::setOptions(Options o) {
    if (o.perRow != 16) o.perRow = 10;
    options_ = std::move(o);
    rebuild();
}

void HmiMemoryMap::setBanner(std::string text, int tone) {
    if (text == banner_ && tone == bannerTone_) return;
    banner_ = std::move(text);
    bannerTone_ = tone;
    invalidate();
}

void HmiMemoryMap::setBehaviorCells(std::set<std::pair<int, std::uint32_t>> cells) {
    behaviorCells_ = std::move(cells);
    invalidate();
}

void HmiMemoryMap::setForcedCells(std::set<std::pair<int, std::uint32_t>> cells) {
    forcedCells_ = std::move(cells);
    invalidate();
}

void HmiMemoryMap::setTableOpen(MemTable t, bool open) {
    if (open) closed_.erase(static_cast<int>(t));
    else closed_.insert(static_cast<int>(t));
    rebuild();
}

void HmiMemoryMap::unfold(MemTable t, std::uint32_t offset) {
    for (const auto& r : rows_)
        if (r.kind == RowKind::Fold && r.table == t && offset >= r.first && offset <= r.last) {
            unfolded_.insert({static_cast<int>(t), r.first});
            rebuild();
            return;
        }
}

void HmiMemoryMap::clearSelection() {
    selected_.reset();
    invalidate();
}

void HmiMemoryMap::select(MemTable t, std::uint32_t offset, bool doReveal) {
    selected_ = Cell{t, offset};
    if (doReveal) reveal(t, offset);
    invalidate();
}

// ---------------------------------------------------------------- les lignes ---
bool HmiMemoryMap::inZone(MemTable t, std::uint32_t offset) const {
    if (!map_.zones.declared) return true;
    return zn::contains(map_.zones.of(t), offset, 1);
}

bool HmiMemoryMap::cellActive(MemTable t, std::uint32_t o) const {
    if (!cells_) return false;
    const auto st = cells_(t, o);
    return st && st->active();
}

bool HmiMemoryMap::lineHasVars(MemTable t, std::uint32_t first, std::uint32_t count) const {
    for (std::uint32_t o = first; o < first + count; ++o)
        if (at_.count(keyOf(t, o))) return true;
    return false;
}

bool HmiMemoryMap::lineHasProblem(MemTable t, std::uint32_t first, std::uint32_t count) const {
    for (std::uint32_t o = first; o < first + count; ++o)
        if (const auto it = worst_.find(keyOf(t, o)); it != worst_.end() && it->second >= 2) return true;
    return false;
}

bool HmiMemoryMap::lineActive(MemTable t, std::uint32_t first, std::uint32_t count) const {
    if (!cells_) return false;
    for (std::uint32_t o = first; o < first + count; ++o)
        if (cellActive(t, o)) return true;
    return false;
}

bool HmiMemoryMap::lineMatches(MemTable t, std::uint32_t first, std::uint32_t count) const {
    const std::string s = lowerOf(options_.search);
    if (s.empty()) return true;
    for (std::uint32_t o = first; o < first + count; ++o) {
        if (lowerOf(zn::modicon(t, o)) == s || lowerOf(zn::schneider(t, o)) == s || std::to_string(o) == s) return true;
        if (const auto it = at_.find(keyOf(t, o)); it != at_.end())
            for (const auto i : it->second)
                if (lowerOf(map_.vars[i].name).find(s) != std::string::npos) return true;
    }
    return false;
}

void HmiMemoryMap::rebuild() {
    rows_.clear();
    const std::uint32_t per = static_cast<std::uint32_t>(options_.perRow);
    float y = 0;
    const auto push = [&](Row r, float h) {
        r.y = y;
        r.h = h;
        y += h;
        rows_.push_back(std::move(r));
    };
    for (const MemTable t : kOrder) {
        const bool declared = map_.zones.declared;
        const auto& ranges = declared ? map_.zones.of(t) : shown_.of(t);
        // Les lignes : les zones, et celles des variables hors zone.
        std::set<std::uint32_t> lines;
        for (const auto& rg : ranges)
            for (std::uint64_t b = rg.first / per * per; b <= rg.last; b += per) lines.insert(static_cast<std::uint32_t>(b));
        std::size_t words = 0, errors = 0, warnings = 0, outside = 0, active = 0, activeFree = 0;
        std::set<std::uint32_t> used;
        for (std::size_t i = 0; i < map_.vars.size(); ++i) {
            const auto& v = map_.vars[i];
            if (v.table != t) continue;
            for (std::uint32_t o = v.first; o <= v.last && o - v.first < 70000; ++o) {
                used.insert(o);
                if (v.outOfZone || !declared) lines.insert(o / per * per);
            }
            outside += v.outOfZone ? 1 : 0;
        }
        words = used.size();
        for (const auto& c : map_.conflicts) {
            if (c.table != t) continue;
            errors += c.severity == 3 ? 1 : 0;
            warnings += c.severity == 2 ? 1 : 0;
        }
        if (cells_)
            for (const auto b : lines)
                for (std::uint32_t o = b; o < b + per; ++o)
                    if (cellActive(t, o)) {
                        ++active;
                        activeFree += at_.count(keyOf(t, o)) ? 0 : 1;
                    }
        // Le titre.
        Row head;
        head.kind = RowKind::Header;
        head.table = t;
        std::string text = " (" + std::string(zn::tableModicon(t)) + " \xC2\xB7 " + std::string(zn::tableSchneider(t)) + ") \xE2\x80\x94 ";
        if (declared) text += ranges.empty() ? std::string("aucune zone (la table n'existe pas)") : zn::rangesText(ranges);
        else text += ranges.empty() ? std::string("aucune variable") : "zones non d\xC3\xA9" "clar\xC3\xA9" "es : " + zn::rangesText(ranges);
        if (words) text += " \xC2\xB7 " + cellsText(t, static_cast<std::uint32_t>(words)) + " \xC3\xA0 mes variables";
        if (cells_) text += " \xC2\xB7 " + std::to_string(active) + " case" + (active > 1 ? "s" : "") + " active" + (active > 1 ? "s" : "")
                            + (activeFree ? " (" + std::to_string(activeFree) + " sans variable)" : std::string{});
        head.text = text;
        std::string alert;
        if (errors) alert += std::to_string(errors) + " erreur" + (errors > 1 ? "s" : "");
        if (warnings) alert += (alert.empty() ? "" : " \xC2\xB7 ") + std::to_string(warnings) + " attention";
        if (outside) alert += (alert.empty() ? "" : " \xC2\xB7 ") + std::to_string(outside) + " hors zone";
        head.alert = alert;
        head.tone = errors || outside ? 3 : warnings ? 2 : 0;
        push(std::move(head), kHeaderH);
        if (closed_.count(static_cast<int>(t)) || lines.empty()) {
            y += 4;
            continue;
        }
        Row cols;
        cols.kind = RowKind::Columns;
        cols.table = t;
        push(std::move(cols), kColsH);
        // Ce qui merite d'etre vu (le reste se replie).
        const bool filtering = options_.show != 0 || !options_.search.empty();
        const auto interesting = [&](std::uint32_t b) {
            if (!lineMatches(t, b, per)) return false;
            const bool vars = lineHasVars(t, b, per);
            switch (options_.show) {
                case 1: return vars;
                case 2: return lineHasProblem(t, b, per);
                case 3: return !vars && lineActive(t, b, per);
                default: return vars || lineActive(t, b, per) || (selected_ && selected_->table == t && selected_->offset >= b && selected_->offset < b + per);
            }
        };
        std::vector<std::uint32_t> order(lines.begin(), lines.end());
        std::size_t i = 0;
        std::optional<std::uint32_t> prevEnd;
        while (i < order.size()) {
            const std::uint32_t b = order[i];
            if (prevEnd && b > *prevEnd) {
                Row gap;
                gap.kind = RowKind::Gap;
                gap.table = t;
                gap.first = *prevEnd;
                gap.last = b - 1;
                gap.text = zn::modicon(t, gap.first) + " \xC3\xA0 " + zn::modicon(t, gap.last) + " : "
                           + (declared ? std::string("pas dans les zones de l'\xC3\xA9quipement") : std::string("rien \xC3\xA0 montrer (ni variable, ni zone)"));
                push(std::move(gap), kGapH);
            }
            // Une suite de lignes sans interet, qui se touchent.
            std::size_t j = i;
            while (j < order.size() && (j == i || order[j] == order[j - 1] + per) && !interesting(order[j])) ++j;
            const std::size_t run = j - i;
            const bool open = unfolded_.count({static_cast<int>(t), b}) != 0;
            if (run >= 2 && (options_.fold || filtering) && !open) {
                Row fold;
                fold.kind = RowKind::Fold;
                fold.table = t;
                fold.first = b;
                fold.last = order[j - 1] + per - 1;
                const std::uint32_t n = fold.last - fold.first + 1;
                std::string why = filtering ? std::string("rien \xC3\xA0 montrer ici") : std::string("sans variable")
                                                  + (cells_ ? std::string(", sans activit\xC3\xA9") : std::string{});
                fold.text = zn::modicon(t, fold.first) + " \xC3\xA0 " + zn::modicon(t, fold.last) + " : " + cellsText(t, n) + " \xC2\xB7 " + why + " (repli\xC3\xA9s)";
                push(std::move(fold), kFoldH);
                prevEnd = order[j - 1] + per;
                i = j;
                continue;
            }
            if (run >= 1) {
                // Montrer ces lignes (une seule, ou un repli ouvert, ou pas de repli).
                for (std::size_t k = i; k < j; ++k) {
                    Row line;
                    line.kind = RowKind::Line;
                    line.table = t;
                    line.first = order[k];
                    line.last = order[k] + per - 1;
                    push(std::move(line), kLineH);
                }
                prevEnd = order[j - 1] + per;
                i = j;
                continue;
            }
            Row line;
            line.kind = RowKind::Line;
            line.table = t;
            line.first = b;
            line.last = b + per - 1;
            push(std::move(line), kLineH);
            prevEnd = b + per;
            ++i;
        }
        y += 6;
    }
    invalidate();
}

float HmiMemoryMap::contentHeight() const {
    return rows_.empty() ? 0.f : rows_.back().y + rows_.back().h + 12.f;
}

float HmiMemoryMap::legendHeight() const {
    return behaviorCells_.empty() && forcedCells_.empty() ? 70.f : 90.f;
}

gfx::Rect HmiMemoryMap::mapArea() const {
    const auto b = bounds();
    const float top = banner_.empty() ? 0.f : kBannerH;
    return {b.x, b.y + top, b.w, std::max(0.f, b.h - top - legendHeight())};
}

void HmiMemoryMap::reveal(MemTable t, std::uint32_t offset) {
    // Deplier ce qui la cache.
    if (closed_.erase(static_cast<int>(t))) rebuild();
    for (const auto& r : rows_)
        if (r.kind == RowKind::Fold && r.table == t && offset >= r.first && offset <= r.last) {
            unfolded_.insert({static_cast<int>(t), r.first});
            rebuild();
            break;
        }
    const auto area = mapArea();
    for (const auto& r : rows_)
        if (r.kind == RowKind::Line && r.table == t && offset >= r.first && offset <= r.last) {
            if (r.y < scroll_ || r.y + r.h > scroll_ + area.h) scroll_ = std::max(0.f, r.y - area.h * 0.35f);
            break;
        }
    invalidate();
}

int HmiMemoryMap::rowAt(gfx::Point p) const {
    const auto area = mapArea();
    if (!inside(area, p)) return -1;
    const float y = p.y - area.y + scroll_;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (y >= rows_[i].y && y < rows_[i].y + rows_[i].h) return static_cast<int>(i);
    return -1;
}

std::optional<HmiMemoryMap::Cell> HmiMemoryMap::cellAt(gfx::Point p) const {
    const int i = rowAt(p);
    if (i < 0) return std::nullopt;
    const auto& r = rows_[static_cast<std::size_t>(i)];
    if (r.kind != RowKind::Line) return std::nullopt;
    const auto area = mapArea();
    const float x0 = area.x + kAddrW;
    const float cw = (area.w - kAddrW - kScrollW - 6.f) / static_cast<float>(options_.perRow);
    if (p.x < x0 || cw <= 0) return std::nullopt;
    const int c = static_cast<int>((p.x - x0) / cw);
    if (c < 0 || c >= options_.perRow) return std::nullopt;
    return Cell{r.table, r.first + static_cast<std::uint32_t>(c)};
}

bool HmiMemoryMap::cellRect(MemTable t, std::uint32_t offset, gfx::Rect& out) const {
    const auto area = mapArea();
    const float cw = (area.w - kAddrW - kScrollW - 6.f) / static_cast<float>(options_.perRow);
    for (const auto& r : rows_) {
        if (r.kind != RowKind::Line || r.table != t || offset < r.first || offset > r.last) continue;
        const float y = area.y + r.y - scroll_;
        if (y + r.h < area.y || y > area.y + area.h) return false;
        out = {area.x + kAddrW + cw * static_cast<float>(offset - r.first), y, cw, r.h};
        return true;
    }
    return false;
}

bool HmiMemoryMap::changing(MemTable t, std::uint32_t o, const zn::CellStat& st, double now) const {
    auto& e = seen_[keyOf(t, o)];
    if (st.changes > e.first) {
        const bool first = e.first == 0 && e.second == 0;
        e = {st.changes, first && st.changes > 0 && passes_ <= 2 ? -100.0 : now};
        if (first && passes_ > 2) e.second = now;
    }
    return now - e.second < 3.0;
}

std::string HmiMemoryMap::valueText(std::uint16_t v, bool bits) const {
    if (bits) return v ? "1" : "0";
    char b[24];
    if (options_.radix == 2) std::snprintf(b, sizeof b, "%04X", v);
    else if (options_.radix == 1) std::snprintf(b, sizeof b, "%d", static_cast<int>(static_cast<std::int16_t>(v)));
    else std::snprintf(b, sizeof b, "%u", static_cast<unsigned>(v));
    return b;
}

// Lot 7 : la case survolee, avec ses valeurs du moment (relu a chaque image
// tant que l'infobulle est ouverte).
std::string HmiMemoryMap::liveTooltip(gfx::Point mouse) const {
    if (hover_) return describe(hover_->table, hover_->offset);
    return ui::Widget::liveTooltip(mouse);
}

std::string HmiMemoryMap::describe(MemTable t, std::uint32_t o) const {
    std::string s = zn::modicon(t, o) + " \xC2\xB7 " + zn::schneider(t, o) + " \xC2\xB7 " + std::string(zn::tableLabel(t));
    if (map_.zones.declared) s += inZone(t, o) ? std::string("\nDans les zones de l'\xC3\xA9quipement") : std::string("\nHors des zones de l'\xC3\xA9quipement : il la refusera");
    if (cells_)
        if (const auto st = cells_(t, o); st && st->seen) {
            s += "\nValeur " + valueText(st->value, zn::isBits(t));
            if (st->changes) s += " \xC2\xB7 a chang\xC3\xA9 " + std::to_string(st->changes) + " fois (de " + valueText(st->min, zn::isBits(t)) + " \xC3\xA0 " + valueText(st->max, zn::isBits(t)) + ")";
            else if (!st->value) s += " \xC2\xB7 toujours 0";
        }
    if (const auto it = at_.find(keyOf(t, o)); it != at_.end()) {
        for (const auto i : it->second) {
            const auto& v = map_.vars[i];
            s += "\n" + v.name + " (" + v.type + (v.bit >= 0 ? ", bit " + std::to_string(v.bit) : std::string{}) + ") : "
                 + (v.written() ? "\xC3\xA9" "crite par " + v.writers.front() + (v.writers.size() > 1 ? " (+" + std::to_string(v.writers.size() - 1) + ")" : std::string{})
                                : std::string(v.writable ? "lue (personne ne l'\xC3\xA9" "crit)" : "lue"));
            if (i < varLive_.size() && !varLive_[i].text.empty()) s += " = " + varLive_[i].text;
        }
        const auto w = worst_.find(keyOf(t, o));
        if (w != worst_.end() && w->second == 3) s += "\nDeux \xC3\xA9" "critures sur la m\xC3\xAA" "me case : erreur";
        else if (w != worst_.end() && w->second == 2) s += "\nUne \xC3\xA9" "criture et une lecture : attention (\xC3\xA7" "a peut \xC3\xAAtre voulu)";
        else if (w != worst_.end() && w->second == 1) s += "\nPartag\xC3\xA9" "e en lecture : normal";
    } else if (cells_) {
        if (const auto st = cells_(t, o); st && st->active()) s += "\nAucune variable IHM ici : \xC2\xAB Cr\xC3\xA9" "er une variable IHM ici \xC2\xBB (barre du haut)";
    }
    s += "\nDouble-clic : ouvrir la variable \xC2\xB7 tirer une barre : d\xC3\xA9placer la variable";
    return s;
}

// ------------------------------------------------------------------ dessin ---
void HmiMemoryMap::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto b = bounds();
    now_ = ctx.time;
    r.fillRect(b, th.color.panelBg);
    // ---- la banniere
    if (!banner_.empty()) {
        const gfx::Color c = bannerTone_ == 1 ? th.color.ok : bannerTone_ == 2 ? th.color.warning : bannerTone_ == 3 ? th.color.error : bannerTone_ == 4 ? th.color.info : th.color.textMuted;
        const gfx::Rect br{b.x, b.y, b.w, kBannerH};
        r.fillRect(br, alpha(c, 28));
        r.fillRect({b.x, b.y + kBannerH - 1, b.w, 1}, alpha(c, 90));
        const float pulse = bannerTone_ == 1 ? 0.6f + 0.4f * static_cast<float>(std::sin(ctx.time * 5.0)) : 1.f;
        r.fillRoundedRect({b.x + 12, b.y + kBannerH / 2 - 5, 10, 10}, alpha(c, static_cast<int>(255 * pulse)), 5);
        r.drawText({b.x + 30, b.y + (kBannerH - r.lineHeight(kSmall)) / 2}, fit(r, banner_, kSmall, b.w - 44), kSmall, th.color.text);
        if (bannerTone_ == 1) invalidate();
    }
    const auto area = mapArea();
    // Le defilement borne a ce qui existe.
    const float maxScroll = std::max(0.f, contentHeight() - area.h);
    scroll_ = std::clamp(scroll_, 0.f, maxScroll);
    const float x0 = area.x + kAddrW;
    const float per = static_cast<float>(options_.perRow);
    const float cw = (area.w - kAddrW - kScrollW - 6.f) / per;
    const gfx::Color cellBg = mix(th.color.windowBg, th.color.panelBg, 0.5f);
    const gfx::Color grey = mix(th.color.panelBg, th.color.textMuted, 0.28f);
    const gfx::Color greyHot = mix(th.color.panelBg, th.color.textMuted, 0.48f);
    const gfx::Color readC = gfx::Color::rgb(0x2F5FA8);
    const gfx::Color writeC = gfx::Color::rgb(0x2E7D4F);
    r.pushClip(area);
    if (rows_.empty()) {
        const std::string msg = map_.equipment.empty() ? std::string("Choisissez un \xC3\xA9quipement (Carte de, en haut)") : std::string("Rien \xC3\xA0 montrer");
        r.drawText({area.x + (area.w - textW(r, msg, kBody)) / 2, area.y + 60}, msg, kBody, th.color.textMuted);
    }
    for (const auto& row : rows_) {
        const float y = area.y + row.y - scroll_;
        if (y + row.h < area.y || y > area.y + area.h) continue;
        const gfx::Rect rr{area.x, y, area.w - kScrollW, row.h};
        switch (row.kind) {
            case RowKind::Header: {
                r.fillRect({rr.x, rr.y + 2, rr.w, rr.h - 4}, th.color.headerBg);
                const bool open = !closed_.count(static_cast<int>(row.table));
                const float cx = rr.x + 14, cy = rr.y + rr.h / 2;
                if (open) {
                    r.line({cx - 4, cy - 2}, {cx, cy + 2}, th.color.textMuted, 1.6f);
                    r.line({cx, cy + 2}, {cx + 4, cy - 2}, th.color.textMuted, 1.6f);
                } else {
                    r.line({cx - 2, cy - 4}, {cx + 2, cy}, th.color.textMuted, 1.6f);
                    r.line({cx + 2, cy}, {cx - 2, cy + 4}, th.color.textMuted, 1.6f);
                }
                const std::string label = upperOf(zn::tableLabel(row.table));
                const float ty = rr.y + (rr.h - r.lineHeight(kBody)) / 2;
                bold(r, {rr.x + 28, ty}, label, kBody, th.color.text);
                float x = rr.x + 34 + textW(r, label, kBody);
                const float alertW = row.alert.empty() ? 0.f : textW(r, row.alert, kSmall) + 16;
                const std::string rest = fit(r, row.text, kSmall, rr.x + rr.w - x - alertW - 8);
                r.drawText({x, rr.y + (rr.h - r.lineHeight(kSmall)) / 2}, rest, kSmall, th.color.textMuted);
                x += textW(r, rest, kSmall) + 12;
                if (!row.alert.empty())
                    r.drawText({x, rr.y + (rr.h - r.lineHeight(kSmall)) / 2}, row.alert, kSmall, row.tone == 3 ? th.color.error : th.color.warning);
                break;
            }
            case RowKind::Columns: {
                r.drawText({rr.x + 10, rr.y + 2}, "adresse", kTiny, th.color.textMuted);
                for (int c = 0; c < options_.perRow; ++c) {
                    char lab[8];
                    std::snprintf(lab, sizeof lab, options_.perRow == 16 ? "+%X" : "+%d", c);
                    r.drawText({x0 + cw * static_cast<float>(c) + (cw - textW(r, lab, kTiny)) / 2, rr.y + 2}, lab, kTiny, th.color.textMuted);
                }
                break;
            }
            case RowKind::Fold: {
                r.fillRect({rr.x, rr.y + 1, rr.w, rr.h - 2}, alpha(th.color.headerBg, 120));
                r.line({rr.x + 12, rr.y + rr.h / 2 - 4}, {rr.x + 16, rr.y + rr.h / 2}, th.color.textMuted, 1.4f);
                r.line({rr.x + 16, rr.y + rr.h / 2}, {rr.x + 12, rr.y + rr.h / 2 + 4}, th.color.textMuted, 1.4f);
                r.drawText({rr.x + 26, rr.y + (rr.h - r.lineHeight(kSmall)) / 2}, fit(r, row.text, kSmall, rr.w - 36), kSmall, th.color.textMuted);
                break;
            }
            case RowKind::Gap: {
                r.fillRect({rr.x, rr.y + 1, rr.w, rr.h - 2}, alpha(th.color.windowBg, 160));
                hatch(r, {rr.x, rr.y + 1, rr.w, rr.h - 2}, alpha(th.color.textMuted, 40), 9.f);
                r.drawText({rr.x + 14, rr.y + (rr.h - r.lineHeight(kSmall)) / 2}, fit(r, row.text, kSmall, rr.w - 28), kSmall, th.color.textMuted);
                break;
            }
            case RowKind::Line: {
                const MemTable t = row.table;
                const bool bits = zn::isBits(t);
                // L'adresse : Modicon, et Schneider en petit.
                r.drawText({rr.x + 10, rr.y + 6}, zn::modicon(t, row.first), kSmall, th.color.text);
                r.drawText({rr.x + 10, rr.y + 22}, zn::schneider(t, row.first), kTiny, th.color.textMuted);
                // Les cases.
                std::vector<std::optional<zn::CellStat>> stats(static_cast<std::size_t>(options_.perRow));
                for (int c = 0; c < options_.perRow; ++c) {
                    const std::uint32_t o = row.first + static_cast<std::uint32_t>(c);
                    const gfx::Rect cr{x0 + cw * static_cast<float>(c) + 2, rr.y + 3, cw - 4, rr.h - 6};
                    const bool zone = inZone(t, o);
                    if (cells_) stats[static_cast<std::size_t>(c)] = cells_(t, o);
                    const auto& st = stats[static_cast<std::size_t>(c)];
                    bool underBar = false;          // une barre de variable la couvre : sa valeur est sur la barre
                    if (const auto it = at_.find(keyOf(t, o)); it != at_.end())
                        for (const auto i : it->second) underBar = underBar || map_.vars[i].bit < 0;
                    if (!zone) {
                        r.fillRoundedRect(cr, alpha(th.color.error, 22), 3);
                        hatch(r, cr, alpha(th.color.error, 110));
                    } else if (underBar) {
                        r.fillRoundedRect(cr, cellBg, 3);
                    } else if (st && st->active()) {
                        const bool hot = changing(t, o, *st, ctx.time);
                        r.fillRoundedRect(cr, hot ? greyHot : grey, 3);
                        const std::string v = valueText(st->value, bits);
                        r.drawText({cr.x + cr.w - textW(r, v, kSmall) - 6, cr.y + cr.h - r.lineHeight(kSmall) - 4}, v, kSmall, th.color.text);
                        if (hot) wave(r, cr.x + 6, cr.y + cr.h - 10, th.color.text);
                    } else {
                        r.fillRoundedRect(cr, cellBg, 3);
                        if (st && st->seen)
                            r.drawText({cr.x + cr.w - textW(r, "0", kTiny) - 5, cr.y + cr.h - r.lineHeight(kTiny) - 3}, "0", kTiny, alpha(th.color.textMuted, 120));
                    }
                }
                // Les barres : des cases voisines portant les memes variables (hors bits d'un mot).
                const auto wordVars = [&](std::uint32_t o) {
                    std::vector<std::uint32_t> out;
                    if (const auto it = at_.find(keyOf(t, o)); it != at_.end())
                        for (const auto i : it->second)
                            if (map_.vars[i].bit < 0) out.push_back(i);
                    return out;
                };
                const auto bitVars = [&](std::uint32_t o) {
                    std::vector<std::uint32_t> out;
                    if (const auto it = at_.find(keyOf(t, o)); it != at_.end())
                        for (const auto i : it->second)
                            if (map_.vars[i].bit >= 0) out.push_back(i);
                    return out;
                };
                int c = 0;
                while (c < options_.perRow) {
                    const std::uint32_t o = row.first + static_cast<std::uint32_t>(c);
                    const auto set = wordVars(o);
                    if (set.empty()) {
                        ++c;
                        continue;
                    }
                    int e = c + 1;
                    while (e < options_.perRow && wordVars(row.first + static_cast<std::uint32_t>(e)) == set) ++e;
                    const gfx::Rect bar{x0 + cw * static_cast<float>(c) + 2, rr.y + 3, cw * static_cast<float>(e - c) - 4, rr.h - 6};
                    int sev = 0;
                    bool anyHot = false, allDead = cells_ && passes_ >= 3;
                    for (int k = c; k < e; ++k) {
                        const std::uint32_t ko = row.first + static_cast<std::uint32_t>(k);
                        if (const auto it = worst_.find(keyOf(t, ko)); it != worst_.end()) sev = std::max(sev, it->second);
                        const auto& st = stats[static_cast<std::size_t>(k)];
                        if (st && st->seen && changing(t, ko, *st, ctx.time)) anyHot = true;
                        if (!st || !st->seen || st->value != 0 || st->changes) allDead = false;
                    }
                    const auto& v0 = map_.vars[set.front()];
                    gfx::Color fill = v0.written() ? writeC : readC;
                    gfx::Color edge = mix(fill, th.color.text, 0.35f);
                    if (set.size() > 1) {
                        bool anyWrite = false;
                        for (const auto i : set) anyWrite = anyWrite || map_.vars[i].written();
                        fill = anyWrite ? writeC : readC;
                    }
                    if (sev == 3) {
                        fill = mix(th.color.error, th.color.panelBg, 0.55f);
                        edge = th.color.error;
                    } else if (sev == 2) {
                        edge = th.color.warning;
                    } else if (sev == 4) {
                        fill = mix(th.color.error, th.color.panelBg, 0.7f);
                        edge = th.color.error;
                    }
                    r.fillRoundedRect(bar, alpha(fill, allDead ? 120 : 255), 4);
                    if (sev == 1 || (set.size() > 1 && sev < 2)) stripes(r, bar, alpha(th.color.text, 30));
                    if (sev == 4) hatch(r, bar, alpha(th.color.error, 140));
                    if (allDead) dashedRect(r, bar, th.color.text, 1.4f);
                    else r.strokeRect(bar, edge, sev >= 2 ? 2.f : 1.f);
                    // Les textes : le nom et la valeur ; dessous, les autres noms (ou le type) et L / E.
                    const MemVarLive* lv = set.front() < varLive_.size() ? &varLive_[set.front()] : nullptr;
                    std::string value = lv ? lv->text : std::string{};
                    const float vw = value.empty() ? 0.f : textW(r, value, kSmall) + (anyHot ? 16 : 4);
                    const bool continues = row.first + static_cast<std::uint32_t>(c) > v0.first;
                    const std::string name = (continues ? "\xE2\x86\x90 " : "") + v0.name;
                    r.drawText({bar.x + 6, bar.y + 3}, fit(r, name, kSmall, bar.w - 12 - vw), kSmall, th.color.text);
                    if (!value.empty()) {
                        const float vx = bar.x + bar.w - vw - 4;
                        r.drawText({vx, bar.y + 3}, fit(r, value, kSmall, vw), kSmall, th.color.text);
                        if (anyHot) wave(r, bar.x + bar.w - 14, bar.y + 3 + r.lineHeight(kSmall) / 2, th.color.text);
                    }
                    std::string second;
                    if (set.size() > 1) {
                        for (std::size_t k = 1; k < set.size(); ++k) second += (k > 1 ? " / " : "") + map_.vars[set[k]].name;
                    } else {
                        second = v0.type;
                    }
                    // Les pastilles L et E (l'IHM en marche).
                    float bx = bar.x + bar.w - 6;
                    const float by = bar.y + bar.h - 16;
                    const auto badge = [&](const char* letter, gfx::Color c2) {
                        const float w = 14;
                        bx -= w;
                        r.fillRoundedRect({bx, by, w, 13}, alpha(c2, 230), 3);
                        r.drawText({bx + (w - textW(r, letter, kTiny)) / 2, by + (13 - r.lineHeight(kTiny)) / 2}, letter, kTiny, gfx::Color::rgb(0xFFFFFF));
                        bx -= 3;
                    };
                    bool any = false;
                    for (const auto i : set)
                        if (i < varLive_.size()) {
                            if (varLive_[i].written && !any) { badge("\xC3\x89", th.color.ok); any = true; }
                        }
                    bool anyRead = false;
                    for (const auto i : set) anyRead = anyRead || (i < varLive_.size() && varLive_[i].read);
                    if (anyRead) badge("L", th.color.info);
                    r.drawText({bar.x + 6, bar.y + bar.h - r.lineHeight(kTiny) - 3}, fit(r, second, kTiny, bx - bar.x - 10), kTiny, alpha(th.color.text, 200));
                    c = e;
                }
                // Les mots de BOOL ranges : les seize bits.
                if (!bits)
                    for (int k = 0; k < options_.perRow; ++k) {
                        const std::uint32_t o = row.first + static_cast<std::uint32_t>(k);
                        const auto bv = bitVars(o);
                        if (bv.empty()) continue;
                        const gfx::Rect cr{x0 + cw * static_cast<float>(k) + 2, rr.y + 3, cw - 4, rr.h - 6};
                        if (wordVars(o).empty()) r.fillRoundedRect(cr, cellBg, 3);
                        int sev = 0;
                        if (const auto it = worst_.find(keyOf(t, o)); it != worst_.end()) sev = it->second;
                        r.strokeRect(cr, sev == 3 ? th.color.error : sev == 2 ? th.color.warning : alpha(th.color.textMuted, 150), sev >= 2 ? 2.f : 1.f);
                        const float sw = std::max(3.f, (cr.w - 10) / 8.f);
                        const auto& st = stats[static_cast<std::size_t>(k)];
                        std::string names;
                        for (int bit = 0; bit < 16; ++bit) {
                            const float sx = cr.x + 5 + sw * static_cast<float>(bit % 8);
                            const float sy = cr.y + 4 + (bit < 8 ? 0.f : 7.f);
                            gfx::Color c2 = alpha(th.color.textMuted, 60);
                            for (const auto i : bv)
                                if (map_.vars[i].bit == bit) c2 = map_.vars[i].written() ? writeC : readC;
                            if (st && st->seen && ((st->value >> bit) & 1)) c2 = mix(c2, gfx::Color::rgb(0xFFFFFF), 0.55f);
                            r.fillRect({sx, sy, sw - 1.5f, 5}, c2);
                        }
                        for (const auto i : bv) names += (names.empty() ? "" : ", ") + lastPart(map_.vars[i].name);
                        r.drawText({cr.x + 5, cr.y + cr.h - r.lineHeight(kTiny) - 3}, fit(r, names, kTiny, cr.w - 10), kTiny, th.color.text);
                    }
                // La case choisie, celle sous la souris ; la cible d'un deplacement ; le rond
                // d'un comportement du jumeau (au-dessus des barres).
                for (int k = 0; k < options_.perRow; ++k) {
                    const std::uint32_t o = row.first + static_cast<std::uint32_t>(k);
                    const gfx::Rect cr{x0 + cw * static_cast<float>(k) + 1, rr.y + 2, cw - 2, rr.h - 4};
                    if (behaviorCells_.count({static_cast<int>(t), o})) {
                        // Lot 18 : la vague d'une case animee, en haut a droite.
                        r.fillRoundedRect({cr.x + cr.w - 17, cr.y + 2, 15, 10}, alpha(th.color.panelBg, 220), 3);
                        wave(r, cr.x + cr.w - 15.5f, cr.y + 7, th.color.info);
                    }
                    if (forcedCells_.count({static_cast<int>(t), o})) {
                        const gfx::Color orange{236, 132, 38, 255};
                        r.strokeRect({cr.x + 0.5f, cr.y + 0.5f, cr.w - 1, cr.h - 1}, orange, 2.f);
                        const float fx = cr.x + cr.w - (behaviorCells_.count({static_cast<int>(t), o}) ? 31.f : 14.f);
                        r.fillRoundedRect({fx, cr.y + 2, 12, 12}, orange, 2);
                        r.drawText({fx + 3, cr.y + 1}, "F", kTiny, gfx::Color{20, 20, 20, 255});
                    }
                    if (selected_ && selected_->table == t && selected_->offset == o) {
                        r.strokeRect(cr, th.color.accent, 2.f);
                        r.strokeRect({cr.x - 2, cr.y - 2, cr.w + 4, cr.h + 4}, alpha(th.color.accent, 70), 2.f);
                    } else if (hover_ && hover_->table == t && hover_->offset == o) {
                        r.strokeRect(cr, dragging_ ? th.color.accent : alpha(th.color.text, 140), dragging_ ? 2.f : 1.f);
                    }
                }
                break;
            }
        }
    }
    r.popClip();
    // ---- l'ascenseur
    // 1.11.4 : il se tire.
    if (area.h > 0) sbar_.paint(ctx, area, contentHeight(), area.h, scroll_);
    (void)maxScroll;
    // ---- la legende
    {
        const float top = b.y + b.h - legendHeight();
        r.fillRect({b.x, top, b.w, legendHeight()}, th.color.panelBg);
        r.fillRect({b.x, top, b.w, 1}, th.color.border);
        float x = b.x + 12, y = top + 8;
        const float rowH = 20;
        const auto item = [&](const std::string& label, const std::function<void(gfx::Rect)>& swatch) {
            const float w = 24 + textW(r, label, kSmall) + 18;
            if (x + w > b.x + b.w - 8) {
                x = b.x + 12;
                y += rowH;
            }
            const gfx::Rect s{x, y + 2, 18, 13};
            swatch(s);
            r.drawText({x + 24, y}, label, kSmall, th.color.textMuted);
            x += w;
        };
        item("0 : rien", [&](gfx::Rect s) { r.fillRoundedRect(s, cellBg, 2); r.strokeRect(s, th.color.border, 1.f); });
        item("gris : l'esclave y a une valeur", [&](gfx::Rect s) { r.fillRoundedRect(s, grey, 2); });
        item("elle change", [&](gfx::Rect s) { r.fillRoundedRect(s, greyHot, 2); wave(r, s.x + 4, s.y + 6, th.color.text); });
        item("ma variable, lue", [&](gfx::Rect s) { r.fillRoundedRect(s, readC, 2); });
        item("ma variable, \xC3\xA9" "crite", [&](gfx::Rect s) { r.fillRoundedRect(s, writeC, 2); });
        item("sur une case morte (toujours 0 : la bonne adresse ?)", [&](gfx::Rect s) { r.fillRoundedRect(s, alpha(readC, 120), 2); dashedRect(r, s, th.color.text, 1.f); });
        item("L : l'IHM la lit (en marche)", [&](gfx::Rect s) { r.fillRoundedRect({s.x + 2, s.y, 14, 13}, th.color.info, 3); });
        item("\xC3\x89 : l'IHM vient de l'\xC3\xA9" "crire", [&](gfx::Rect s) { r.fillRoundedRect({s.x + 2, s.y, 14, 13}, th.color.ok, 3); });
        item("partag\xC3\xA9 en lecture", [&](gfx::Rect s) { r.fillRoundedRect(s, readC, 2); stripes(r, s, alpha(th.color.text, 40)); });
        item("\xC3\xA9" "criture + lecture (attention)", [&](gfx::Rect s) { r.fillRoundedRect(s, writeC, 2); r.strokeRect(s, th.color.warning, 2.f); });
        item("deux \xC3\xA9" "critures (erreur)", [&](gfx::Rect s) { r.fillRoundedRect(s, mix(th.color.error, th.color.panelBg, 0.55f), 2); r.strokeRect(s, th.color.error, 2.f); });
        item("hors zone", [&](gfx::Rect s) { r.fillRoundedRect(s, alpha(th.color.error, 22), 2); hatch(r, s, alpha(th.color.error, 140), 5.f); });
        if (!behaviorCells_.empty()) item("anim\xC3\xA9" "e (un mouvement de l'esclave simul\xC3\xA9)", [&](gfx::Rect s) { wave(r, s.x + 4, s.y + 6, th.color.info); });
        if (!forcedCells_.empty())
            item("forc\xC3\xA9" "e (les \xC3\xA9" "critures y sont refus\xC3\xA9" "es)", [&](gfx::Rect s) {
                r.strokeRect(s, gfx::Color{236, 132, 38, 255}, 2.f);
                r.drawText({s.x + 5, s.y - 1}, "F", kTiny, gfx::Color{236, 132, 38, 255});
            });
    }
}

// ------------------------------------------------------------------ souris ---
ui::EventResult HmiMemoryMap::onEvent(const ui::InputEvent& ev) {
    {
        float off = scroll_;   // 1.11.4 : l'ascenseur se tire
        if (sbar_.handle(*this, ev, off)) {
            scroll_ = std::max(0.f, off);
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const auto c = cellAt(m->pos);
        if (pressCell_ && !dragVar_.empty() && !dragging_ && std::hypot(m->pos.x - pressAt_.x, m->pos.y - pressAt_.y) > 8.f) dragging_ = true;
        const bool changed = (c.has_value() != hover_.has_value()) || (c && hover_ && (c->table != hover_->table || c->offset != hover_->offset));
        if (changed) {
            hover_ = c;
            setTooltip(c ? describe(c->table, c->offset) : std::string{});
            invalidate();
        }
        return dragging_ ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        const float before = scroll_;
        const float maxScroll = std::max(0.f, contentHeight() - mapArea().h);
        scroll_ = std::clamp(scroll_ - w->dy * 60.f, 0.f, maxScroll);
        if (scroll_ != before) {
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        grabFocus();
        const int i = rowAt(m->pos);
        if (i < 0) return ui::EventResult::Ignored;
        const auto& row = rows_[static_cast<std::size_t>(i)];
        if (row.kind == RowKind::Header) {
            setTableOpen(row.table, closed_.count(static_cast<int>(row.table)) != 0);
            return ui::EventResult::Consumed;
        }
        if (row.kind == RowKind::Fold) {
            unfolded_.insert({static_cast<int>(row.table), row.first});
            rebuild();
            return ui::EventResult::Consumed;
        }
        const auto c = cellAt(m->pos);
        if (!c) return ui::EventResult::Consumed;
        selected_ = c;
        pressCell_ = c;
        pressAt_ = m->pos;
        dragVar_.clear();
        dragging_ = false;
        if (const auto it = at_.find(keyOf(c->table, c->offset)); it != at_.end() && !it->second.empty()) dragVar_ = map_.vars[it->second.front()].root;
        invalidate();
        cellChosen->emit(static_cast<int>(c->table), c->offset);
        if (m->clickCount >= 2 && !dragVar_.empty()) {
            const std::string name = map_.vars[at_[keyOf(c->table, c->offset)].front()].name;
            pressCell_.reset();
            dragVar_.clear();
            variableOpened->emit(name);
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev)) {
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        const auto from = pressCell_;
        const std::string var = dragVar_;
        const bool dragged = dragging_;
        pressCell_.reset();
        dragVar_.clear();
        dragging_ = false;
        if (dragged && from && !var.empty()) {
            const auto to = cellAt(m->pos);
            invalidate();
            if (to && to->table == from->table && to->offset != from->offset) {
                selected_ = to;
                variableMoved->emit(var, static_cast<int>(to->table), static_cast<int>(to->offset) - static_cast<int>(from->offset));
            }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (!selected_ || !focused()) return ui::EventResult::Ignored;
        std::int64_t o = selected_->offset;
        const int per = options_.perRow;
        switch (k->key) {
            case ui::Key::Left: o -= 1; break;
            case ui::Key::Right: o += 1; break;
            case ui::Key::Up: o -= per; break;
            case ui::Key::Down: o += per; break;
            case ui::Key::Return:
                if (const auto it = at_.find(keyOf(selected_->table, selected_->offset)); it != at_.end() && !it->second.empty())
                    variableOpened->emit(map_.vars[it->second.front()].name);
                return ui::EventResult::Consumed;
            default: return ui::EventResult::Ignored;
        }
        if (o < 0 || o > 65535) return ui::EventResult::Consumed;
        select(selected_->table, static_cast<std::uint32_t>(o));
        cellChosen->emit(static_cast<int>(selected_->table), selected_->offset);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
