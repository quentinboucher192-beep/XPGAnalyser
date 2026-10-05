// app/hmi/HmiTwinValues.cpp - les valeurs simulees des jumeaux (lot 18).
#include "HmiTwinValues.hpp"

#include "HmiEquipmentHost.hpp"
#include "HmiPaneKit.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../ui/TextSearch.hpp"      // lot recherche
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <utility>

namespace app {

namespace zn = hmi::zones;
namespace tw = hmi::twin;
using hmi::MemTable;
using PG = ui::PropertyGrid;
using hmikit::prop;

namespace {

// 1.9 : "14:02:31", une heure murale (s depuis 1970), en heure locale.
std::string wallClockText(double wall) {
    if (wall <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

const gfx::FontId kTiny{11};
const gfx::FontId kSmall{12};
const gfx::FontId kBody{14};

constexpr float kBarH = 36.f;        // les filtres
constexpr float kHeadH = 24.f;       // les titres des colonnes
constexpr float kRowH = 40.f;
constexpr float kGroupH = 26.f;
constexpr float kAddH = 24.f;
constexpr float kLaneH = 34.f;
constexpr float kCurvesHeadH = 26.f;
constexpr float kAxisH = 16.f;
constexpr float kStatusH = 22.f;

gfx::Color alpha(gfx::Color c, int a) {
    c.a = static_cast<std::uint8_t>(std::clamp(a, 0, 255));
    return c;
}
gfx::Color forcedColor() { return gfx::Color{236, 132, 38, 255}; }

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

bool inside(const gfx::Rect& r, gfx::Point p) { return r.w > 0 && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

// La vague : "anime".
void wave(gfx::IRenderer& r, float x, float cy, float w, gfx::Color c) {
    gfx::Point prev{x, cy};
    for (int i = 1; i <= 12; ++i) {
        const float t = static_cast<float>(i) / 12.f;
        const gfx::Point p{x + t * w, cy - std::sin(t * 6.2831853f) * 3.f};
        r.line(prev, p, c, 1.5f);
        prev = p;
    }
}

// Un petit triangle d'attention.
void warnGlyph(gfx::IRenderer& r, float x, float y, gfx::Color c) {
    r.line({x, y + 11}, {x + 6, y}, c, 1.6f);
    r.line({x + 6, y}, {x + 12, y + 11}, c, 1.6f);
    r.line({x, y + 11}, {x + 12, y + 11}, c, 1.6f);
    r.line({x + 6, y + 4}, {x + 6, y + 7}, c, 1.6f);
    r.line({x + 6, y + 9}, {x + 6, y + 9.6f}, c, 1.6f);
}

void checkBox(gfx::IRenderer& r, gfx::Rect b, bool on, gfx::Color accent, gfx::Color border, gfx::Color bg) {
    if (on) {
        r.fillRoundedRect(b, accent, 3);
        r.line({b.x + 3, b.y + b.h * 0.52f}, {b.x + b.w * 0.42f, b.y + b.h - 4}, gfx::Color{255, 255, 255, 255}, 2.f);
        r.line({b.x + b.w * 0.42f, b.y + b.h - 4}, {b.x + b.w - 3, b.y + 3.5f}, gfx::Color{255, 255, 255, 255}, 2.f);
    } else {
        r.fillRoundedRect(b, bg, 3);
        r.strokeRect(b, border, 1.2f);
    }
}

// Une valeur lisible : "12,4", "231,6", "5100".
std::string valueText(double v, bool integer) {
    char b[48];
    if (integer || (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e12)) std::snprintf(b, sizeof b, "%.0f", v);
    else if (std::fabs(v) >= 1000) std::snprintf(b, sizeof b, "%.1f", v);
    else std::snprintf(b, sizeof b, "%.2f", v);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    for (auto& c : s)
        if (c == '.') c = ',';
    if (s == "-0") s = "0";
    return s;
}

std::string lowered(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmedOf(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool integerType(const std::string& t) {
    const std::string u = lowered(t);
    return u != "real" && u != "lreal";
}

bool bandKind(hmi::BehaviorKind k) {
    return k == hmi::BehaviorKind::Sine || k == hmi::BehaviorKind::Ramp || k == hmi::BehaviorKind::Random;
}

bool periodKind(hmi::BehaviorKind k) {
    return k != hmi::BehaviorKind::Constant && k != hmi::BehaviorKind::Copy && k != hmi::BehaviorKind::FollowPlc;
}

std::string ageOf(double seconds) {
    if (seconds < 1) return "\xC3\xA0 l'instant";
    if (seconds < 90) return "il y a " + std::to_string(static_cast<int>(seconds)) + " s";
    if (seconds < 5400) return "il y a " + std::to_string(static_cast<int>(seconds / 60)) + " min";
    return "il y a " + std::to_string(static_cast<int>(seconds / 3600)) + " h";
}

std::string tableKind(const hmi::twin::ValueRow& r) {
    if (r.table == MemTable::Coils) return "bobine";
    if (r.table == MemTable::DiscreteInputs) return "entr\xC3\xA9" "e TOR";
    if (r.bit >= 0) return "bit " + std::to_string(r.bit) + " d'un mot";
    return "BOOL";
}

} // namespace

// ===================================================================== le widget ===
HmiTwinValues::HmiTwinValues(std::string id, bool compact) : ui::Widget(std::move(id)), compact_(compact) {
    const std::string base = this->id();
    if (!compact_) {
        twinBox_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".twins")));
        showBox_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>(base + ".show")));
        showBox_->setItems({{"mes variables + registres anim\xC3\xA9s ou forc\xC3\xA9s", "tout", {}, true},
                            {"les lignes anim\xC3\xA9" "es ou forc\xC3\xA9" "es", "actives", {}, true}});
        showBox_->setSelectedIndex(0);
        searchBox_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".search")));
        searchBox_->setPlaceholder("Chercher (variable, 43003)");
    } else {
        curvesOpen_ = false;
    }
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".kinds")));
    links_ += menu_->itemChosen->connect([this](int item) {
        const std::string e = menuEquip_, k = menuKey_;
        std::string kind = "aucun";
        if (item >= 1 && item <= static_cast<int>(std::size(hmi::kBehaviorKinds)))
            kind = std::string(hmi::behaviorKindKey(hmi::kBehaviorKinds[static_cast<std::size_t>(item - 1)]));
        kindChosen->emit(e, k, kind);
    });
}

HmiTwinValues::~HmiTwinValues() = default;

void HmiTwinValues::setLines(std::vector<Line> lines) {
    lines_ = std::move(lines);
    if (drag_.on) {
        drag_.on = false;
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (lines_[i].kind == Line::Kind::Row && lines_[i].equipment == drag_.equipment && lines_[i].key == drag_.key) {
                drag_.line = i;
                drag_.on = true;
            }
    }
    hover_.reset();
    invalidate();
}

void HmiTwinValues::setLive(const std::string& equipment, const std::string& key, std::string value, std::string sub, std::optional<double> v, bool forced) {
    auto& l = live_[equipment + "|" + key];
    if (l.text == value && l.sub == sub && l.v == v && l.forced == forced) return;
    l.text = std::move(value);
    l.sub = std::move(sub);
    l.v = v;
    l.forced = forced;
    invalidate();
}

void HmiTwinValues::sample(const std::string& equipment, const std::string& key, double t, double v, bool forced) {
    auto& h = hist_[equipment + "|" + key];
    if (!h.empty() && t <= h.back().t) return;
    h.push_back({t, static_cast<float>(v), forced});
    while (!h.empty() && h.front().t < t - 310.0) h.pop_front();
}

std::size_t HmiTwinValues::samples(const std::string& equipment, const std::string& key) const {
    const auto it = hist_.find(equipment + "|" + key);
    return it == hist_.end() ? 0 : it->second.size();
}

void HmiTwinValues::setNow(double t) {
    now_ = t;
    invalidate();
}

std::size_t HmiTwinValues::selIndex() const {
    std::size_t first = std::string::npos;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const auto& l = lines_[i];
        if (l.kind != Line::Kind::Row || l.equipment != selEquip_ || l.key != selKey_) continue;
        if (!selTitle_.empty() && l.title == selTitle_) return i;
        if (first == std::string::npos) first = i;
    }
    return first;
}

void HmiTwinValues::select(const std::string& equipment, const std::string& key) {
    if (equipment != selEquip_ || key != selKey_) selTitle_.clear();
    selEquip_ = equipment;
    selKey_ = key;
    // La montrer.
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].kind == Line::Kind::Row && lines_[i].equipment == equipment && lines_[i].key == key) {
            folded_.erase(equipment);
            gfx::Rect r{};
            if (lineRect(i, r)) {
                const auto area = listArea();
                if (r.y < area.y) scroll_ = std::max(0.f, scroll_ - (area.y - r.y));
                else if (r.y + r.h > area.y + area.h) scroll_ += r.y + r.h - (area.y + area.h);
            }
        }
    invalidate();
}

void HmiTwinValues::setCurvesOpen(bool open) {
    curvesOpen_ = open;
    invalidate();
}
void HmiTwinValues::setWindow(double seconds) {
    window_ = std::clamp(seconds, 10.0, 300.0);
    invalidate();
}
void HmiTwinValues::setFrozen(bool frozen) {
    frozen_ = frozen;
    frozenAt_ = now_;
    invalidate();
}

gfx::Rect HmiTwinValues::eventBounds() const {
    if (drag_.on) return {-100000.f, -100000.f, 200000.f, 200000.f};   // la poignee suit la souris partout
    return bounds();
}

void HmiTwinValues::onLayout() {
    const auto b = bounds();
    // Le menu du mouvement : des bornes non vides, sinon il n'est jamais dessine (la passe du dessus).
    if (menu_) menu_->setBounds(b);
    if (compact_ || !twinBox_) return;
    float x = b.x + 70.f;
    twinBox_->setBounds({x, b.y + 5.f, 260.f, 26.f});
    x += 260.f + 76.f;
    showBox_->setBounds({x, b.y + 5.f, 300.f, 26.f});
    x += 310.f;
    const float w = std::max(80.f, std::min(220.f, b.x + b.w - x - 300.f));
    searchBox_->setBounds({x, b.y + 5.f, w, 26.f});
}

HmiTwinValues::Cols HmiTwinValues::cols() const {
    const auto b = bounds();
    Cols c;
    const float w = b.w - 10.f;
    if (compact_) {
        c.check = b.x + 8;
        c.name = b.x + 32;
        c.nameW = std::min(150.f, w * 0.26f);
        c.bar = c.name + c.nameW + 6;
        c.forceW = 104;
        c.valueW = 70;
        c.barW = std::max(80.f, b.x + w - c.bar - c.forceW - c.valueW - 14);
        c.force = c.bar + c.barW + 6;
        c.value = c.force + c.forceW + 6;
        return c;
    }
    c.check = b.x + 8;
    c.name = b.x + 34;
    c.nameW = 168;
    c.addr = c.name + c.nameW + 4;
    c.type = c.addr + 62;
    c.kind = c.type + 50;
    c.kindW = 106;
    c.bar = c.kind + c.kindW + 8;
    const float right = 8 + 52 + 6 + 124 + 6 + 100 + 6 + 96;
    c.barW = std::max(120.f, b.x + w - c.bar - right);
    c.period = c.bar + c.barW + 8;
    c.force = c.period + 56;
    c.forceW = 124;
    c.value = c.force + c.forceW + 6;
    c.valueW = 100;
    c.spark = c.value + c.valueW + 6;
    c.sparkW = std::max(40.f, b.x + w - c.spark);
    return c;
}

std::vector<std::size_t> HmiTwinValues::curveLines() const {
    std::vector<std::size_t> out;
    // 1.11.2 (BLK) : la ligne choisie, cherchee UNE fois. selIndex() parcourt toutes les
    // lignes ; l'appeler pour chaque ligne faisait n x n comparaisons, et lineRect (donc
    // listArea, curvesArea, curveLines) est appele pour chaque ligne a chaque image :
    // n x n x n. Sur Tunnel_Paris_CDG (IHM > Configuration > Equipements), une seule
    // image ne finissait plus (plus de 15 min). Meme resultat, meme ordre.
    const std::size_t sel = selIndex();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const auto& l = lines_[i];
        if (l.kind != Line::Kind::Row) continue;
        const bool chosen = i == sel;
        if (l.animated || l.forced || chosen) out.push_back(i);
    }
    return out;
}

gfx::Rect HmiTwinValues::curvesArea() const {
    const auto b = bounds();
    if (compact_) return {b.x, b.y + b.h, b.w, 0};
    float h = kCurvesHeadH;
    if (curvesOpen_) {
        const auto n = curveLines().size();
        h += std::min(b.h * 0.42f, static_cast<float>(std::max<std::size_t>(n, 1)) * kLaneH + kAxisH + 6.f);
    }
    return {b.x, b.y + b.h - h, b.w, h};
}

gfx::Rect HmiTwinValues::listArea() const {
    const auto b = bounds();
    const float top = b.y + (compact_ ? 0.f : kBarH) + kHeadH;
    const float bottom = compact_ ? b.y + b.h - kStatusH : curvesArea().y;
    return {b.x, top, b.w, std::max(0.f, bottom - top)};
}

float HmiTwinValues::lineHeight(const Line& l) const {
    if (l.kind == Line::Kind::Group) return kGroupH;
    if (folded(l.equipment)) return 0;
    if (l.kind == Line::Kind::Add) return compact_ ? 0.f : kAddH;
    return compact_ ? 36.f : kRowH;
}

float HmiTwinValues::contentHeight() const {
    float h = 0;
    for (const auto& l : lines_) h += lineHeight(l);
    return h;
}

bool HmiTwinValues::lineRect(std::size_t index, gfx::Rect& out) const {
    const auto area = listArea();
    float y = area.y - scroll_;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const float h = lineHeight(lines_[i]);
        if (i == index) {
            if (h <= 0) return false;
            out = {area.x, y, area.w - 8.f, h};
            return true;
        }
        y += h;
    }
    return false;
}

gfx::Rect HmiTwinValues::partOf(std::size_t i, const gfx::Rect& r, Part part) const {
    const auto c = cols();
    const auto& l = lines_[i];
    switch (part) {
        case Part::Check: return {c.check - 3, r.y + r.h * 0.5f - 10, 22, 20};
        case Part::Kind: return compact_ ? gfx::Rect{} : gfx::Rect{c.kind, r.y + 8, c.kindW, 24};
        case Part::Bar: return {c.bar, r.y, c.barW, r.h};
        case Part::ForceBox: return l.boolean ? gfx::Rect{} : gfx::Rect{c.force, r.y + r.h * 0.5f - 9, 18, 18};
        case Part::ForceValue: return l.boolean ? gfx::Rect{} : gfx::Rect{c.force + 22, r.y + r.h * 0.5f - 11, c.forceW - (compact_ ? 24 : 56), 22};
        case Part::Free: return l.boolean ? gfx::Rect{c.force, r.y + r.h * 0.5f - 10, compact_ ? 44.f : 48.f, 20} : gfx::Rect{};
        case Part::Zero: return l.boolean ? gfx::Rect{c.force + (compact_ ? 44.f : 48.f), r.y + r.h * 0.5f - 10, 26, 20} : gfx::Rect{};
        case Part::One: return l.boolean ? gfx::Rect{c.force + (compact_ ? 70.f : 74.f), r.y + r.h * 0.5f - 10, 26, 20} : gfx::Rect{};
        case Part::Low:
        case Part::High:
        case Part::Band: {
            const gfx::Rect bar{c.bar, r.y, c.barW, r.h};
            if (l.barMode == 0 || l.warn || !l.note.empty() || (l.bandFixed && part != Part::Band)) return {};
            const float ty = r.y + r.h - (compact_ ? 12.f : 14.f);
            const float xa = xOf(l, bar, l.barMode == 3 ? 0.0 : l.lo), xb = xOf(l, bar, l.barMode == 2 ? l.lo : l.hi);
            if (part == Part::Low) return l.barMode == 1 ? gfx::Rect{xa - 7, ty - 10, 14, 20} : gfx::Rect{};
            if (part == Part::High) return gfx::Rect{xb - 7, ty - 10, 14, 20};
            return l.barMode == 1 && xb - xa > 16 ? gfx::Rect{xa + 7, ty - 7, xb - xa - 14, 14} : gfx::Rect{};
        }
        case Part::Row: return r;
        default: return {};
    }
}

float HmiTwinValues::xOf(const Line& l, const gfx::Rect& bar, double v) const {
    const float x0 = bar.x + 30.f, x1 = bar.x + bar.w - 34.f;
    const double span = l.barHi - l.barLo;
    const double t = span > 1e-12 ? (v - l.barLo) / span : 0.0;
    return x0 + static_cast<float>(std::clamp(t, 0.0, 1.0)) * std::max(1.f, x1 - x0);
}

double HmiTwinValues::valueAtX(const Line& l, const gfx::Rect& bar, float x) const {
    const float x0 = bar.x + 30.f, x1 = bar.x + bar.w - 34.f;
    const double t = std::clamp(static_cast<double>((x - x0) / std::max(1.f, x1 - x0)), 0.0, 1.0);
    double v = l.barLo + t * (l.barHi - l.barLo);
    // Des pas ronds : un centieme de la barre.
    const double span = std::fabs(l.barHi - l.barLo);
    double step = span / 100.0;
    if (step > 0) {
        const double mag = std::pow(10.0, std::floor(std::log10(step)));
        step = step / mag < 2 ? mag : step / mag < 5 ? 2 * mag : 5 * mag;
        v = std::round(v / step) * step;
    }
    if (!l.boolean && integerType(l.type) && l.rawOf == nullptr) v = std::round(v);
    return v;
}

std::pair<std::size_t, HmiTwinValues::Part> HmiTwinValues::hit(gfx::Point p) const {
    const auto area = listArea();
    if (!inside(area, p)) return {std::string::npos, Part::None};
    // 1.11.2 (BLK) : un seul passage, comme onPaint (lineRect pour chaque ligne : n x n).
    float rowY = area.y - scroll_;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const float rowH = lineHeight(lines_[i]);
        const gfx::Rect r{area.x, rowY, area.w - 8.f, rowH};
        rowY += rowH;
        if (rowH <= 0 || !inside(r, p)) continue;
        const auto& l = lines_[i];
        if (l.kind == Line::Kind::Group) return {i, Part::Group};
        if (l.kind == Line::Kind::Add) return {i, Part::Add};
        for (const Part part : {Part::Check, Part::Kind, Part::High, Part::Low, Part::Band, Part::ForceBox, Part::ForceValue, Part::Free, Part::Zero, Part::One})
            if (inside(partOf(i, r, part), p)) return {i, part};
        return {i, Part::Row};
    }
    return {std::string::npos, Part::None};
}

const HmiTwinValues::Line* HmiTwinValues::line(const std::string& equipment, const std::string& key) const {
    for (const auto& l : lines_)
        if (l.kind == Line::Kind::Row && l.equipment == equipment && (l.key == key || tw::sameCell(l.key, key))) return &l;
    return nullptr;
}

bool HmiTwinValues::partRect(const std::string& equipment, const std::string& key, Part part, gfx::Rect& out) {
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const auto& l = lines_[i];
        const bool match = part == Part::Add ? l.kind == Line::Kind::Add && l.equipment == equipment
                                             : l.kind == Line::Kind::Row && l.equipment == equipment && (l.key == key || tw::sameCell(l.key, key));
        if (!match) continue;
        folded_.erase(equipment);
        gfx::Rect r{};
        if (!lineRect(i, r)) return false;
        const auto area = listArea();
        if (r.y < area.y || r.y + r.h > area.y + area.h) {
            // La faire defiler jusqu'a elle.
            scroll_ = std::clamp(scroll_ + (r.y - area.y) - area.h * 0.3f, 0.f, std::max(0.f, contentHeight() - area.h));
            invalidate();
            if (!lineRect(i, r)) return false;
        }
        out = part == Part::Add || part == Part::Row ? r : partOf(i, r, part);
        return out.w > 0;
    }
    return false;
}

bool HmiTwinValues::valueX(const std::string& equipment, const std::string& key, double v, float& x) {
    gfx::Rect bar{};
    if (!partRect(equipment, key, Part::Bar, bar)) return false;
    const auto* l = line(equipment, key);
    if (!l) return false;
    x = xOf(*l, bar, v);
    return true;
}

void HmiTwinValues::openKindMenu(std::size_t i, gfx::Point at) {
    const auto& l = lines_[i];
    menuEquip_ = l.equipment;
    menuKey_ = l.key;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item none;
    none.label = "aucun (ne bouge pas)";
    none.id = 0;
    items.push_back(none);
    int id = 1;
    for (const auto k : hmi::kBehaviorKinds) {
        ui::PopupMenu::Item it;
        it.label = std::string(hmi::behaviorKindLabel(k));
        it.id = id++;
        if (l.boolean && k != hmi::BehaviorKind::Blink && k != hmi::BehaviorKind::Constant && k != hmi::BehaviorKind::Copy
            && k != hmi::BehaviorKind::FollowPlc && k != hmi::BehaviorKind::Steps) {
            it.enabled = false;
            it.disabledReason = "pas pour un bit";
        }
        items.push_back(it);
    }
    menu_->setItems(std::move(items));
    auto* root = rootWidget();
    const auto sb = root ? root->bounds() : bounds();
    menu_->openAt(at, {sb.w, sb.h});
}

// ------------------------------------------------------------------ peinture ---
void HmiTwinValues::paintSpark(const ui::PaintContext& ctx, const Line& l, const gfx::Rect& r) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    const auto it = hist_.find(lineKey(l));
    if (it == hist_.end() || it->second.size() < 2) return;
    const double t1 = frozen_ ? frozenAt_ : now_, t0 = t1 - 60.0;
    double lo = l.barLo, hi = l.barHi;
    if (l.boolean) {
        lo = -0.1;
        hi = 1.1;
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    const auto X = [&](double t) { return r.x + static_cast<float>((t - t0) / (t1 - t0)) * r.w; };
    const auto Y = [&](double v) { return r.y + r.h - 3 - static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0)) * (r.h - 6); };
    gfx::Point prev{};
    bool have = false;
    for (const auto& s : it->second) {
        if (s.t < t0 || s.t > t1) continue;
        const gfx::Point p{X(s.t), Y(s.v)};
        if (have) {
            if (l.boolean) {
                g.line(prev, {p.x, prev.y}, s.forced ? forcedColor() : th.color.ok, 1.4f);
                g.line({p.x, prev.y}, p, s.forced ? forcedColor() : th.color.ok, 1.4f);
            } else {
                g.line(prev, p, s.forced ? forcedColor() : th.color.accent, 1.4f);
            }
        }
        prev = p;
        have = true;
    }
}

void HmiTwinValues::paintBar(const ui::PaintContext& ctx, const Line& l, const Live* live, const gfx::Rect& bar, bool dragging) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    const float ty = bar.y + bar.h - (compact_ ? 12.f : 14.f);
    const float x0 = bar.x + 30.f, x1 = bar.x + bar.w - 34.f;
    const bool isInt = l.barMode == 3 || (!l.boolean && integerType(l.type) && !l.rawOf);
    const auto label = [&](double v) { return valueText(v, false) + (l.barMode == 3 ? " s" : std::string{}); };
    (void)isInt;
    // Les bornes de la barre.
    const std::string sLo = valueText(l.barLo, false), sHi = valueText(l.barHi, false) + (l.barMode == 3 ? " s" : std::string{});
    g.drawText({x0 - 6 - textW(g, sLo, kTiny), ty - 7}, sLo, kTiny, th.color.textMuted);
    g.drawText({x1 + 6, ty - 7}, sHi, kTiny, th.color.textMuted);
    g.fillRoundedRect({x0, ty - 2, x1 - x0, 4}, alpha(th.color.textMuted, 60), 2);
    const bool active = l.animated;
    const gfx::Color band = active ? th.color.accent : alpha(th.color.textMuted, 140);
    double lo = l.lo, hi = l.hi;
    if (dragging) {
        lo = drag_.curLo;
        hi = drag_.curHi;
    }
    const auto handle = [&](float x, bool hot) {
        g.fillRoundedRect({x - 4.5f, ty - 8, 9, 16}, hot ? th.color.accent : gfx::Color{235, 240, 248, 255}, 3);
        g.strokeRect({x - 4.5f, ty - 8, 9, 16}, active ? th.color.accent : th.color.textMuted, 1.2f);
    };
    const bool hotLo = dragging && drag_.part == Part::Low, hotHi = dragging && (drag_.part == Part::High), hotBand = dragging && drag_.part == Part::Band;
    if (l.barMode == 1) {
        const float xa = xOf(l, bar, lo), xb = xOf(l, bar, hi);
        g.fillRoundedRect({xa, ty - 3, std::max(2.f, xb - xa), 6}, alpha(band, hotBand ? 255 : 200), 3);
        if (!l.bandFixed) {
            handle(xa, hotLo || hotBand);
            handle(xb, hotHi || hotBand);
        }
        const std::string a = label(lo), b = label(hi);
        float la = xa - textW(g, a, kTiny) * 0.5f, lb = xb - textW(g, b, kTiny) * 0.5f;
        if (lb < la + textW(g, a, kTiny) + 4) lb = la + textW(g, a, kTiny) + 4;
        g.drawText({la, ty - 22}, a, kTiny, active ? th.color.text : th.color.textMuted);
        g.drawText({lb, ty - 22}, b, kTiny, active ? th.color.text : th.color.textMuted);
    } else if (l.barMode == 2) {
        const float xa = xOf(l, bar, lo);
        handle(xa, hotHi);
        const std::string a = label(lo);
        g.drawText({xa - textW(g, a, kTiny) * 0.5f, ty - 22}, a, kTiny, active ? th.color.text : th.color.textMuted);
    } else if (l.barMode == 3) {
        const float xb = xOf(l, bar, hi);
        g.fillRoundedRect({x0, ty - 3, std::max(2.f, xb - x0), 6}, active ? th.color.ok : alpha(th.color.textMuted, 140), 3);
        handle(xb, hotHi);
        const std::string a = valueText(hi, false) + " s \xC3\xA0 1";
        g.drawText({x0 + std::max(0.f, (xb - x0 - textW(g, a, kTiny)) * 0.5f), ty - 22}, a, kTiny, active ? th.color.text : th.color.textMuted);
    }
    // Le point : la valeur.
    if (live && live->v && l.barMode != 3 && !l.boolean) {
        const float xv = xOf(l, bar, *live->v);
        g.fillRoundedRect({xv - 5, ty - 5, 10, 10}, gfx::Color{250, 252, 255, 255}, 5);
        g.fillRoundedRect({xv - 3, ty - 3, 6, 6}, live->forced ? forcedColor() : th.color.accent, 3);
    }
    // Le forcage : une marque orange.
    if (l.forced && !l.boolean) {
        const float xf = xOf(l, bar, l.forcedEng);
        g.line({xf, ty - 9}, {xf, ty + 9}, forcedColor(), 2.f);
        const std::string t = "F " + valueText(l.forcedEng, false);
        const float tw1 = textW(g, t, kTiny) + 8;
        const float bx = std::clamp(xf - tw1 * 0.5f, bar.x, bar.x + bar.w - tw1);
        g.fillRoundedRect({bx, ty - 24, tw1, 13}, forcedColor(), 3);
        g.drawText({bx + 4, ty - 24}, t, kTiny, gfx::Color{20, 20, 20, 255});
    }
}

void HmiTwinValues::paintRow(const ui::PaintContext& ctx, std::size_t i, const gfx::Rect& r, const Cols& c) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    const auto& l = lines_[i];
    const bool chosen = i == selIndex();
    if (chosen) g.fillRect(r, alpha(th.color.selectionBg, 150));
    else if (hover_ && hover_->first == i) g.fillRect(r, th.brand.hover);
    g.line({r.x, r.y + r.h - 0.5f}, {r.x + r.w, r.y + r.h - 0.5f}, alpha(th.color.border, 120), 1.f);
    const Live* live = nullptr;
    if (const auto it = live_.find(lineKey(l)); it != live_.end()) live = &it->second;
    const float mid = r.y + r.h * 0.5f;
    const gfx::Color muted = l.running ? th.color.textMuted : th.color.textDisabled;
    // La case Animer.
    checkBox(g, {c.check, mid - 7, 14, 14}, l.animated, th.color.accent, th.color.borderStrong, th.color.inputBg);
    // Le nom.
    {
        const float w = c.nameW;
        bold(g, {c.name, r.y + (compact_ ? 3.f : 4.f)}, fit(g, l.title, kBody, w), kBody, l.sub == "sans variable" ? th.color.textMuted : th.color.text);
        std::string sub = compact_ ? l.address + (l.sub.empty() ? std::string{} : " \xC2\xB7 " + l.sub) : l.sub;
        g.drawText({c.name, r.y + (compact_ ? 19.f : 22.f)}, fit(g, sub, kTiny, w), kTiny, muted);
    }
    if (!compact_) {
        g.drawText({c.addr, mid - 8}, l.address, kSmall, th.color.text);
        g.drawText({c.type, mid - 8}, l.type, kSmall, muted);
        // Le mouvement.
        const gfx::Rect kb{c.kind, r.y + 8, c.kindW, 24};
        g.fillRoundedRect(kb, th.color.inputBg, 3);
        g.strokeRect(kb, hover_ && hover_->first == i && hover_->second == Part::Kind ? th.color.accent : th.color.border, 1.f);
        float tx = kb.x + 7;
        if (l.hasBehavior) {
            wave(g, tx, kb.y + 12, 12, l.animated ? th.color.accent : th.color.textMuted);
            tx += 17;
        }
        g.drawText({tx, kb.y + 4}, fit(g, l.kindLabel, kSmall, kb.x + kb.w - tx - 16), kSmall, l.hasBehavior ? th.color.text : th.color.textMuted);
        g.line({kb.x + kb.w - 13, kb.y + 10}, {kb.x + kb.w - 9, kb.y + 14}, th.color.textMuted, 1.3f);
        g.line({kb.x + kb.w - 9, kb.y + 14}, {kb.x + kb.w - 5, kb.y + 10}, th.color.textMuted, 1.3f);
    }
    // La barre (ou ce qu'elle dit a la place).
    const gfx::Rect bar{c.bar, r.y, c.barW, r.h};
    if (l.warn && !l.animated) {
        warnGlyph(g, bar.x + 4, r.y + 6, th.color.warning);
        g.drawText({bar.x + 22, r.y + 3}, fit(g, l.warnText, kSmall, bar.w - 24), kSmall, th.color.warning);
        g.drawText({bar.x + 22, r.y + 20}, fit(g, "l'animer \xC3\xA9" "craserait ce que l'IHM y \xC3\xA9" "crit (d\xC3\xA9" "coch\xC3\xA9" "e d'office)", kTiny, bar.w - 24), kTiny, muted);
    } else if (!l.note.empty()) {
        g.drawText({bar.x + 6, mid - 8}, fit(g, l.note, kSmall, bar.w - 8), kSmall, muted);
    } else if (l.barMode != 0 || (live && live->v && !l.boolean)) {
        paintBar(ctx, l, live, bar, drag_.on && drag_.line == i);
    } else if (l.boolean) {
        g.drawText({bar.x + 6, mid - 8}, l.forced ? "forc\xC3\xA9 \xC3\xA0 " + l.forcedText : std::string("\xE2\x80\x94"), kSmall, l.forced ? forcedColor() : muted);
    }
    // La periode.
    if (!compact_) g.drawText({c.period, mid - 8}, l.period, kSmall, l.animated ? th.color.text : muted);
    // Forcer.
    if (l.boolean) {
        const char* seg[] = {"libre", "0", "1"};
        const Part parts[] = {Part::Free, Part::Zero, Part::One};
        for (int k = 0; k < 3; ++k) {
            const auto sr = partOf(i, r, parts[k]);
            const bool on = (k == 0 && l.boolForce < 0) || (k == 1 && l.boolForce == 0) || (k == 2 && l.boolForce == 1);
            const gfx::Color fill = on ? (k == 0 ? th.color.accent : forcedColor()) : th.color.inputBg;
            g.fillRect(sr, fill);
            g.strokeRect(sr, th.color.border, 1.f);
            const float tw1 = textW(g, seg[k], kSmall);
            g.drawText({sr.x + (sr.w - tw1) * 0.5f, sr.y + 2}, seg[k], kSmall, on ? gfx::Color{255, 255, 255, 255} : th.color.text);
        }
    } else {
        const auto fb = partOf(i, r, Part::ForceBox);
        checkBox(g, fb, l.forced, forcedColor(), th.color.borderStrong, th.color.inputBg);
        const auto fv = partOf(i, r, Part::ForceValue);
        if (l.forced) {
            g.fillRoundedRect(fv, alpha(forcedColor(), 40), 3);
            g.strokeRect(fv, forcedColor(), 1.2f);
            g.drawText({fv.x + 5, fv.y + 3}, fit(g, l.forcedText, kSmall, fv.w - 8), kSmall, th.color.text);
        } else {
            g.drawText({fv.x + 2, fv.y + 3}, "libre", kSmall, muted);
        }
        if (!compact_) g.drawText({fv.x + fv.w + 4, fv.y + 3}, "brut", kTiny, muted);
    }
    // La valeur.
    {
        float vx = c.value;
        const bool forcedNow = l.forced || (live && live->forced);
        if (forcedNow) {
            g.fillRoundedRect({vx, mid - (compact_ ? 8.f : 13.f), 13, 14}, forcedColor(), 2);
            g.drawText({vx + 3, mid - (compact_ ? 8.f : 13.f)}, "F", kTiny, gfx::Color{20, 20, 20, 255});
            vx += 17;
        }
        const std::string text = live ? live->text : std::string("\xE2\x80\x94");
        bold(g, {vx, compact_ ? mid - 9 : r.y + 4}, fit(g, text, kBody, c.valueW - (vx - c.value)), kBody, forcedNow ? forcedColor() : th.color.text);
        if (!compact_ && live && !live->sub.empty()) g.drawText({c.value, r.y + 22}, fit(g, live->sub, kTiny, c.valueW), kTiny, muted);
    }
    // La courbe de la ligne.
    if (!compact_) paintSpark(ctx, l, {c.spark, r.y + 5, c.sparkW, r.h - 10});
}

void HmiTwinValues::paintCurves(const ui::PaintContext& ctx, const gfx::Rect& area) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    g.fillRect(area, th.color.panelBg);
    g.line({area.x, area.y}, {area.x + area.w, area.y}, th.color.border, 1.f);
    // L'en-tete : replier, la fenetre, figer, la bande.
    const float hy = area.y + (kCurvesHeadH - 16) * 0.5f;
    {
        const float ax = area.x + 10;
        if (curvesOpen_) {
            g.line({ax, hy + 4}, {ax + 5, hy + 10}, th.color.text, 1.4f);
            g.line({ax + 5, hy + 10}, {ax + 10, hy + 4}, th.color.text, 1.4f);
        } else {
            g.line({ax + 2, hy + 2}, {ax + 8, hy + 7}, th.color.text, 1.4f);
            g.line({ax + 8, hy + 7}, {ax + 2, hy + 12}, th.color.text, 1.4f);
        }
        bold(g, {ax + 16, hy}, "COURBES", kSmall, th.color.text);
        const std::string w = window_ >= 299 ? "5 derni\xC3\xA8res minutes" : window_ >= 59 ? "60 derni\xC3\xA8res secondes" : "30 derni\xC3\xA8res secondes";
        g.drawText({ax + 86, hy}, "\xE2\x80\x94 les lignes anim\xC3\xA9" "es ou forc\xC3\xA9" "es, " + w + (frozen_ ? " (fig\xC3\xA9" "es)" : std::string{}), kSmall, th.color.textMuted);
    }
    // Les boutons a droite.
    {
        float x = area.x + area.w - 470;
        g.drawText({x, hy}, "Fen\xC3\xAAtre", kSmall, th.color.textMuted);
        x += 62;
        const struct { const char* t; double s; } ws[] = {{"30 s", 30}, {"1 min", 60}, {"5 min", 300}};
        for (const auto& wb : ws) {
            const float w = textW(g, wb.t, kSmall) + 14;
            const bool on = std::fabs(window_ - wb.s) < 1;
            g.fillRect({x, area.y + 4, w, kCurvesHeadH - 8}, on ? th.color.accent : th.color.inputBg);
            g.strokeRect({x, area.y + 4, w, kCurvesHeadH - 8}, th.color.border, 1.f);
            g.drawText({x + 7, hy}, wb.t, kSmall, on ? gfx::Color{255, 255, 255, 255} : th.color.text);
            x += w;
        }
        x += 18;
        g.fillRect({x, area.y + 7, 3, 12}, frozen_ ? th.color.accent : th.color.text);
        g.fillRect({x + 6, area.y + 7, 3, 12}, frozen_ ? th.color.accent : th.color.text);
        g.drawText({x + 14, hy}, "Figer", kSmall, frozen_ ? th.color.accent : th.color.text);
        x += 70;
        checkBox(g, {x, area.y + 6, 14, 14}, bandOn_, th.color.accent, th.color.borderStrong, th.color.inputBg);
        g.drawText({x + 20, hy}, "la bande de la ligne choisie", kSmall, th.color.text);
    }
    if (!curvesOpen_) return;
    const auto list = curveLines();
    const float labelW = 150.f;
    const gfx::Rect plot{area.x + labelW, area.y + kCurvesHeadH, area.w - labelW - 16, area.h - kCurvesHeadH - kAxisH - 4};
    const double t1 = frozen_ ? frozenAt_ : now_, t0 = t1 - window_;
    const auto X = [&](double t) { return plot.x + static_cast<float>((t - t0) / (t1 - t0)) * plot.w; };
    if (list.empty()) {
        g.drawText({area.x + 16, plot.y + 8}, "Aucune ligne anim\xC3\xA9" "e ou forc\xC3\xA9" "e : cochez \xC2\xAB Animer \xC2\xBB, ou forcez une case.", kSmall, th.color.textMuted);
        return;
    }
    const float laneH = std::min(kLaneH, plot.h / static_cast<float>(list.size()));
    g.pushClip({area.x, plot.y, area.w, plot.h + 2});
    float y = plot.y;
    const std::size_t sel = selIndex();
    for (const std::size_t i : list) {
        const auto& l = lines_[i];
        const bool chosen = i == sel;
        const gfx::Rect lane{plot.x, y + 3, plot.w, laneH - 6};
        if (chosen) g.fillRect({area.x, y, area.w, laneH}, alpha(th.color.selectionBg, 110));
        g.line({area.x, y + laneH - 0.5f}, {area.x + area.w, y + laneH - 0.5f}, alpha(th.color.border, 90), 1.f);
        bold(g, {area.x + 10, y + 2}, fit(g, l.title, kSmall, labelW - 14), kSmall, th.color.text);
        std::string sub;
        if (const auto it = live_.find(lineKey(l)); it != live_.end()) sub = (it->second.forced ? "F " : "") + it->second.text;
        g.drawText({area.x + 10, y + 17}, fit(g, sub, kTiny, labelW - 14), kTiny, th.color.textMuted);
        double lo = l.barLo, hi = l.barHi;
        if (l.boolean) {
            lo = -0.15;
            hi = 1.15;
        }
        if (hi - lo < 1e-9) hi = lo + 1;
        const auto Y = [&](double v) { return lane.y + lane.h - static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0)) * lane.h; };
        // La bande de la ligne choisie.
        if (chosen && bandOn_ && l.barMode == 1) {
            const float ya = Y(l.hi), yb = Y(l.lo);
            g.fillRect({lane.x, ya, lane.w, std::max(1.f, yb - ya)}, alpha(th.color.accent, 45));
        }
        const auto it = hist_.find(lineKey(l));
        if (it != hist_.end()) {
            gfx::Point prev{};
            bool have = false, wasForced = false;
            float lastLabelEnd = -1e9f;
            float lastForcedV = 0;
            for (const auto& s : it->second) {
                if (s.t < t0 || s.t > t1) continue;
                const gfx::Point p{X(s.t), Y(s.v)};
                const gfx::Color col = s.forced ? forcedColor() : l.boolean ? th.color.ok : th.color.accent;
                if (have) {
                    if (l.boolean) {
                        g.line(prev, {p.x, prev.y}, col, 1.5f);
                        g.line({p.x, prev.y}, p, col, 1.5f);
                    } else {
                        g.line(prev, p, col, 1.5f);
                    }
                }
                if (s.forced && (!wasForced || std::fabs(s.v - lastForcedV) > 1e-6f) && p.x > lastLabelEnd + 4) {
                    // Le debut du forcage (ou une autre valeur forcee) : une marque et une etiquette.
                    g.line({p.x, lane.y}, {p.x, lane.y + lane.h}, forcedColor(), 1.f);
                    const std::string t = "F " + valueText(s.v, false);
                    const float w = textW(g, t, kTiny) + 8;
                    g.fillRoundedRect({p.x + 2, lane.y, w, 13}, forcedColor(), 3);
                    g.drawText({p.x + 6, lane.y}, t, kTiny, gfx::Color{20, 20, 20, 255});
                    lastLabelEnd = p.x + 2 + w;
                }
                if (s.forced) lastForcedV = s.v;
                wasForced = s.forced;
                prev = p;
                have = true;
            }
        }
        y += laneH;
    }
    g.popClip();
    // L'axe du temps.
    const float ay = area.y + area.h - kAxisH;
    for (int k = 0; k <= 4; ++k) {
        const double t = t0 + (t1 - t0) * k / 4.0;
        const float x = X(t);
        std::string s;
        if (k == 4) s = "maintenant";
        else {
            const double ago = t1 - t;
            s = ago >= 60 ? "-" + valueText(ago / 60, false) + " min" : "-" + valueText(ago, true) + " s";
        }
        const float w = textW(g, s, kTiny);
        g.drawText({std::clamp(x - w * 0.5f, plot.x, plot.x + plot.w - w), ay}, s, kTiny, th.color.textMuted);
    }
}

void HmiTwinValues::onPaint(const ui::PaintContext& ctx) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    const auto b = bounds();
    g.fillRect(b, th.color.windowBg);
    if (!compact_) {
        g.fillRect({b.x, b.y, b.w, kBarH}, th.color.panelBg);
        const float y = b.y + (kBarH - g.lineHeight(th.font.ui)) * 0.5f;
        g.drawText({b.x + 10, y}, "Esclaves", th.font.ui, th.color.textMuted);
        if (showBox_) g.drawText({showBox_->bounds().x - 70, y}, "Montrer", th.font.ui, th.color.textMuted);
        if (searchBox_) {
            const float sx = searchBox_->bounds().x + searchBox_->bounds().w + 12;
            const std::string st = fit(g, status_, kSmall, b.x + b.w - sx - 8);
            g.fillRoundedRect({sx, b.y + kBarH * 0.5f - 4, 8, 8}, th.color.ok, 4);
            g.drawText({sx + 14, b.y + (kBarH - 16) * 0.5f}, st, kSmall, th.color.ok);
        }
    }
    const auto c = cols();
    // Les titres des colonnes.
    {
        const float hy = b.y + (compact_ ? 0.f : kBarH);
        g.fillRect({b.x, hy, b.w, kHeadH}, th.color.headerBg);
        const float ty = hy + (kHeadH - 15) * 0.5f;
        wave(g, c.check + 1, hy + kHeadH * 0.5f, 12, th.color.textMuted);
        g.drawText({c.name, ty}, compact_ ? "Variable" : "Variable / registre", kSmall, th.color.textMuted);
        if (!compact_) {
            g.drawText({c.addr, ty}, "Adresse", kSmall, th.color.textMuted);
            g.drawText({c.type, ty}, "Type", kSmall, th.color.textMuted);
            g.drawText({c.kind, ty}, "Mouvement", kSmall, th.color.textMuted);
            g.drawText({c.period, ty}, "P\xC3\xA9riode", kSmall, th.color.textMuted);
            g.drawText({c.spark, ty}, "Courbe 60 s", kSmall, th.color.textMuted);
        }
        g.drawText({c.bar, ty}, compact_ ? "Zone de mouvement (tirer)" : "Plage de la valeur (tirer les poign\xC3\xA9" "es)", kSmall, th.color.textMuted);
        g.drawText({c.force, ty}, "Forcer", kSmall, th.color.textMuted);
        g.drawText({c.value, ty}, "Valeur", kSmall, th.color.textMuted);
    }
    // Les lignes.
    const auto area = listArea();
    const float maxScroll = std::max(0.f, contentHeight() - area.h);
    scroll_ = std::clamp(scroll_, 0.f, maxScroll);
    g.pushClip(area);
    if (lines_.empty()) {
        g.drawText({area.x + 16, area.y + 12},
                   "Aucun esclave simul\xC3\xA9 : clone un vrai appareil (sa fiche : Cloner en esclave simul\xC3\xA9), ou ajoute un esclave virtuel (onglet \xC3\x89quipements).", kBody, th.color.textMuted);
    }
    // 1.11.2 (BLK) : la place de chaque ligne, cumulee en un seul passage (comme lineRect,
    // qui reprend tout depuis la premiere ligne et refait listArea : n x n par image).
    float rowY = area.y - scroll_;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const float rowH = lineHeight(lines_[i]);
        const gfx::Rect r{area.x, rowY, area.w - 8.f, rowH};
        rowY += rowH;
        if (rowH <= 0) continue;
        if (r.y + r.h < area.y || r.y > area.y + area.h) continue;
        const auto& l = lines_[i];
        if (l.kind == Line::Kind::Group) {
            g.fillRect(r, alpha(th.color.headerBg, 200));
            const float ax = r.x + 8, ay = r.y + (r.h - 12) * 0.5f;
            if (!folded(l.equipment)) {
                g.line({ax, ay + 3}, {ax + 5, ay + 9}, th.color.text, 1.4f);
                g.line({ax + 5, ay + 9}, {ax + 10, ay + 3}, th.color.text, 1.4f);
            } else {
                g.line({ax + 2, ay + 1}, {ax + 8, ay + 6}, th.color.text, 1.4f);
                g.line({ax + 8, ay + 6}, {ax + 2, ay + 11}, th.color.text, 1.4f);
            }
            bold(g, {r.x + 24, r.y + 5}, l.title, kSmall, th.color.text);
            const float tx = r.x + 30 + textW(g, l.title, kSmall);
            g.drawText({tx, r.y + 5}, fit(g, l.sub, kSmall, r.x + r.w - tx - 8), kSmall, l.running ? th.color.info : th.color.textMuted);
            continue;
        }
        if (l.kind == Line::Kind::Add) {
            const bool hot = hover_ && hover_->first == i;
            g.drawText({r.x + 44, r.y + 4}, fit(g, l.title, kSmall, r.w - 50), kSmall, hot ? th.color.accentHover : th.color.accent);
            continue;
        }
        paintRow(ctx, i, r, c);
    }
    // L'infobulle de la poignee tiree (par-dessus les lignes).
    if (drag_.on && drag_.line < lines_.size()) {
        gfx::Rect r{};
        if (lineRect(drag_.line, r)) {
            const auto& l = lines_[drag_.line];
            const gfx::Rect bar{c.bar, r.y, c.barW, r.h};
            const double lo = drag_.curLo, hi = drag_.curHi;
            const bool high = drag_.part != Part::Low;
            const double v = high ? hi : lo;
            std::string t = l.barMode == 3 ? std::string("\xC3\xA0 1 pendant ") : drag_.part == Part::Band ? std::string("zone ") : high ? std::string("max ") : std::string("min ");
            if (drag_.part == Part::Band) t += valueText(lo, false) + " \xE2\x86\x92 " + valueText(hi, false);
            else t += valueText(v, false) + (l.barMode == 3 ? " s" : std::string{});
            if (l.rawOf && l.barMode != 3) t += drag_.part == Part::Band ? " (" + l.rawOf(lo) + " \xE2\x86\x92 " + l.rawOf(hi).substr(5) + ")" : " (" + l.rawOf(v) + ")";
            const float xv = xOf(l, bar, drag_.part == Part::Band ? (lo + hi) / 2 : v);
            const float w = textW(g, t, kSmall) + 12;
            const float bx = std::clamp(xv - w * 0.5f, bar.x - 20, bar.x + bar.w - w + 20);
            g.fillRoundedRect({bx, r.y + r.h + 1, w, 18}, th.brand.tooltipBg, 4);
            g.strokeRect({bx, r.y + r.h + 1, w, 18}, th.color.accent, 1.f);
            g.drawText({bx + 6, r.y + r.h + 2}, t, kSmall, th.brand.tooltipText);
        }
    }
    g.popClip();
    if (contentHeight() > area.h && area.h > 0) {
        const float thumb = std::max(24.f, area.h * area.h / contentHeight());
        const float ty = area.y + (area.h - thumb) * (maxScroll > 0 ? scroll_ / maxScroll : 0.f);
        g.fillRoundedRect({area.x + area.w - 7, ty, 5, thumb}, alpha(th.color.textMuted, 110), 3);
    }
    if (!compact_) paintCurves(ctx, curvesArea());
    else {
        const gfx::Rect st{b.x, b.y + b.h - kStatusH, b.w, kStatusH};
        g.fillRect(st, th.color.panelBg);
        g.drawText({st.x + 8, st.y + 3}, fit(g, status_, kSmall, st.w - 16), kSmall, th.color.textMuted);
    }
}

// ------------------------------------------------------------------ souris ---
ui::EventResult HmiTwinValues::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        if (drag_.on && drag_.line < lines_.size()) {
            const auto& l = lines_[drag_.line];
            gfx::Rect r{};
            if (lineRect(drag_.line, r)) {
                const auto c = cols();
                const gfx::Rect bar{c.bar, r.y, c.barW, r.h};
                const double v = valueAtX(l, bar, m->pos.x);
                if (drag_.part == Part::Low) {
                    drag_.curLo = std::min(v, drag_.curHi);
                } else if (drag_.part == Part::High) {
                    if (l.barMode == 1) drag_.curHi = std::max(v, drag_.curLo);
                    else {
                        drag_.curHi = v;
                        drag_.curLo = l.barMode == 2 ? v : drag_.curLo;
                    }
                } else if (drag_.part == Part::Band) {
                    const double v0 = valueAtX(l, bar, drag_.startX);
                    double d = v - v0;
                    d = std::clamp(d, l.barLo - drag_.lo, l.barHi - drag_.hi);
                    drag_.curLo = drag_.lo + d;
                    drag_.curHi = drag_.hi + d;
                }
                invalidate();
            }
            return ui::EventResult::Consumed;
        }
        const auto h = hit(m->pos);
        std::optional<std::pair<std::size_t, Part>> now;
        if (h.first != std::string::npos) now = h;
        if (now != hover_) {
            hover_ = now;
            std::string tip;
            if (now && now->first < lines_.size()) {
                const auto& l = lines_[now->first];
                switch (now->second) {
                    case Part::Check: tip = "Animer : la valeur bouge toute seule (d\xC3\xA9" "coch\xC3\xA9" "e : ses r\xC3\xA9glages restent)"; break;
                    case Part::Kind: tip = "Le mouvement : sinus, rampe, al\xC3\xA9" "atoire, clignote, \xC3\xA9tapes..."; break;
                    case Part::Low: case Part::High: tip = "Tirer la poign\xC3\xA9" "e : la borne suit la souris (le brut suit l'\xC3\xA9" "chelle)"; break;
                    case Part::Band: tip = "Tirer la bande : la zone de mouvement se d\xC3\xA9place enti\xC3\xA8re"; break;
                    case Part::ForceBox: tip = "Forcer : tenir la case \xC3\xA0 sa valeur brute ; une \xC3\xA9" "criture y est refus\xC3\xA9" "e (exception 04)"; break;
                    case Part::ForceValue: tip = "La valeur brute forc\xC3\xA9" "e : la fiche la change (\xC3\x80 la valeur brute)"; break;
                    case Part::Free: tip = "Libre : pas forc\xC3\xA9" "e"; break;
                    case Part::Zero: tip = "Forcer \xC3\xA0 0"; break;
                    case Part::One: tip = "Forcer \xC3\xA0 1"; break;
                    case Part::Add: tip = "Un registre de plus, sans variable : la premi\xC3\xA8re case libre des zones, anim\xC3\xA9" "e"; break;
                    default: tip = l.tooltip; break;
                }
            }
            setTooltip(tip);
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        const auto area = listArea();
        if (!inside(area, w->pos)) return ui::EventResult::Ignored;
        const float before = scroll_;
        scroll_ = std::clamp(scroll_ - w->dy * 60.f, 0.f, std::max(0.f, contentHeight() - area.h));
        if (scroll_ != before) {
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        (void)u;
        if (!drag_.on) return ui::EventResult::Ignored;
        drag_.on = false;
        invalidate();
        const std::string e = drag_.equipment, k = drag_.key;
        const double lo = drag_.curLo, hi = drag_.curHi;
        if (std::fabs(lo - drag_.lo) > 1e-12 || std::fabs(hi - drag_.hi) > 1e-12) bandChanged->emit(e, k, lo, hi);
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        // Les courbes : replier, la fenetre, figer, la bande.
        if (!compact_) {
            const auto ca = curvesArea();
            if (inside({ca.x, ca.y, ca.w, kCurvesHeadH}, d->pos)) {
                const float right = ca.x + ca.w - 470;
                if (d->pos.x < right - 10) {
                    setCurvesOpen(!curvesOpen_);
                    return ui::EventResult::Consumed;
                }
                float x = right + 62;
                const double ws[] = {30, 60, 300};
                const float widths[] = {40, 50, 50};
                for (int k = 0; k < 3; ++k) {
                    if (d->pos.x >= x && d->pos.x < x + widths[k]) {
                        setWindow(ws[k]);
                        return ui::EventResult::Consumed;
                    }
                    x += widths[k];
                }
                x += 18;
                if (d->pos.x >= x && d->pos.x < x + 60) {
                    setFrozen(!frozen_);
                    return ui::EventResult::Consumed;
                }
                x += 70;
                if (d->pos.x >= x) {
                    bandOn_ = !bandOn_;
                    invalidate();
                }
                return ui::EventResult::Consumed;
            }
            if (inside(ca, d->pos)) return ui::EventResult::Consumed;
        }
        const auto [i, part] = hit(d->pos);
        if (i == std::string::npos || i >= lines_.size()) return ui::EventResult::Ignored;
        const Line l = lines_[i];
        if (part == Part::Group) {
            if (folded(l.equipment)) folded_.erase(l.equipment);
            else folded_.insert(l.equipment);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (part == Part::Add) {
            addRequested->emit(l.equipment);
            return ui::EventResult::Consumed;
        }
        const bool newRow = l.equipment != selEquip_ || l.key != selKey_ || l.title != selTitle_;
        selEquip_ = l.equipment;
        selKey_ = l.key;
        selTitle_ = l.title;
        invalidate();
        switch (part) {
            case Part::Check: animateToggled->emit(l.equipment, l.key, !l.animated); return ui::EventResult::Consumed;
            case Part::Kind:
                if (newRow) rowChosen->emit(l.equipment, l.key);
                openKindMenu(i, {partOf(i, [&] { gfx::Rect r{}; (void)lineRect(i, r); return r; }(), Part::Kind).x, d->pos.y + 14});
                return ui::EventResult::Consumed;
            case Part::Low:
            case Part::High:
            case Part::Band:
                drag_.on = true;
                drag_.line = i;
                drag_.part = part;
                drag_.startX = d->pos.x;
                drag_.lo = l.barMode == 3 ? 0.0 : l.lo;
                drag_.hi = l.barMode == 2 ? l.lo : l.hi;
                drag_.curLo = drag_.lo;
                drag_.curHi = drag_.hi;
                drag_.equipment = l.equipment;
                drag_.key = l.key;
                setTooltip({});
                if (newRow) rowChosen->emit(l.equipment, l.key);
                return ui::EventResult::Consumed;
            case Part::ForceBox: forceToggled->emit(l.equipment, l.key, !l.forced); return ui::EventResult::Consumed;
            case Part::Free: boolForced->emit(l.equipment, l.key, -1); return ui::EventResult::Consumed;
            case Part::Zero: boolForced->emit(l.equipment, l.key, 0); return ui::EventResult::Consumed;
            case Part::One: boolForced->emit(l.equipment, l.key, 1); return ui::EventResult::Consumed;
            default:
                if (d->clickCount >= 2) rowActivated->emit(l.equipment, l.key);
                else rowChosen->emit(l.equipment, l.key);
                return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ================================================================= le controleur ===
TwinValuesController::TwinValuesController(hmi::DocumentPtr doc, Apply apply) : doc_(std::move(doc)), apply_(std::move(apply)) {}

void TwinValuesController::attach(HmiTwinValues& w) {
    widget_ = &w;
    links_ += w.animateToggled->connect([this](const std::string& e, const std::string& k, bool on) { (void)setAnimated(e, k, on); });
    links_ += w.kindChosen->connect([this](const std::string& e, const std::string& k, const std::string& kind) { (void)setKind(e, k, kind); });
    links_ += w.bandChanged->connect([this](const std::string& e, const std::string& k, double lo, double hi) { (void)setBand(e, k, lo, hi); });
    links_ += w.forceToggled->connect([this](const std::string& e, const std::string& k, bool on) {
        if (!on) {
            (void)setForced(e, k, {});
            return;
        }
        // Forcer : tenir la case a sa valeur de maintenant.
        const auto* r = ref(e, k);
        std::string raw = "0";
        if (r && host())
            if (const auto bank = host()->twinBank(e))
                if (const auto v = tw::rowValue(*bank, r->row, r->low)) raw = tw::numberText(*v);
        for (auto& ch : raw)
            if (ch == ',') ch = '.';
        (void)setForced(e, k, raw);
    });
    links_ += w.boolForced->connect([this](const std::string& e, const std::string& k, int v) { (void)setForced(e, k, v < 0 ? std::string{} : std::to_string(v)); });
    links_ += w.rowChosen->connect([this](const std::string& e, const std::string& k) {
        selEquip_ = e;
        selKey_ = k;
        if (changed) changed();
    });
    links_ += w.rowActivated->connect([this](const std::string& e, const std::string& k) {
        selEquip_ = e;
        selKey_ = k;
        if (changed) changed();
    });
    links_ += w.addRequested->connect([this](const std::string& e) { (void)addRegister(e); });
    if (auto* t = w.twinFilter())
        links_ += t->selectionChanged->connect([this, t](int) {
            const auto* item = t->selectedItem();
            setTwinFilter(item && item->value != "tous" ? item->value : std::string{});
        });
    if (auto* s = w.showFilter()) links_ += s->selectionChanged->connect([this](int i) { setShow(i); });
    if (auto* s = w.searchBox()) links_ += s->textChanged->connect([this](const std::string& t) { setSearch(t); });
}

const hmi::Equipment* TwinValuesController::equipment(const std::string& name) const {
    return doc_ ? doc_->project.equipmentByName(name) : nullptr;
}

const TwinValuesController::RowRef* TwinValuesController::ref(const std::string& e, const std::string& address) const {
    for (const auto& r : refs_)
        if (r.equipment == e && r.row.address == address) return &r;
    for (const auto& r : refs_)
        if (r.equipment == e && tw::sameCell(r.row.address, address)) return &r;
    return nullptr;
}

std::optional<hmi::twin::ValueRow> TwinValuesController::row(const std::string& e, const std::string& address) const {
    const auto* q = equipment(e);
    if (!q) return std::nullopt;
    for (const auto& r : tw::valueRows(doc_->project, *q))
        if (r.address == address || tw::sameCell(r.address, address)) return r;
    // Une case libre (la carte) : sans variable, sans mouvement.
    return address.empty() ? std::nullopt : tw::freeRow(address);
}

bool TwinValuesController::fail(std::string m, std::string* why) {
    if (say) say(m, true);
    if (why) *why = std::move(m);
    return false;
}

void TwinValuesController::setTwinFilter(const std::string& e) {
    filter_ = e;
    refresh();
}
void TwinValuesController::setShow(int mode) {
    show_ = mode;
    refresh();
}
void TwinValuesController::setSearch(const std::string& text) {
    search_ = lowered(trimmedOf(text));
    refresh();
}

void TwinValuesController::select(const std::string& e, const std::string& address) {
    selEquip_ = e;
    selKey_ = address;
    if (const auto* r = ref(e, address)) selKey_ = r->row.address;
    if (widget_) widget_->select(selEquip_, selKey_);
    if (changed) changed();
}

void TwinValuesController::refresh() {
    buildLines();
    tick();
}

void TwinValuesController::buildLines() {
    refs_.clear();
    if (!widget_ || !doc_) return;
    const auto& p = doc_->project;
    std::vector<HmiTwinValues::Line> lines;
    std::vector<ui::DropDown::Item> twins{{"tous les esclaves simul\xC3\xA9s", "tous", {}, true}};
    std::size_t nTwins = 0;
    for (const auto& e : p.equipments) {
        if (!e.hasTwin()) continue;
        ++nTwins;
        twins.push_back({e.twinLabel(), e.name, {}, true});
        if (!filter_.empty() && filter_ != e.name) continue;
        const bool low = e.wordOrder != "fort";
        auto* h = host();
        const int port = h ? h->simulatedPort(e.name) : 0;
        const auto bank = h ? h->twinBank(e.name) : nullptr;
        HmiTwinValues::Line g;
        g.kind = HmiTwinValues::Line::Kind::Group;
        g.equipment = e.name;
        g.title = e.name;
        g.running = port > 0;
        // 1.9 : esclave lie (pret, l'IHM lit le vrai ; ou lu par l'IHM, depuis la
        // bascule) ou seulement simule ; son port, ses requetes.
        const auto status = h ? h->status(e.name) : std::nullopt;
        const bool read = status && status->enabled && status->viaTwin;
        g.sub = std::string(" \xC2\xB7 ") + (e.simulated ? "seulement simul\xC3\xA9" : "esclave li\xC3\xA9");
        if (port <= 0) g.sub += " \xC2\xB7 arr\xC3\xAAt\xC3\xA9";
        else if (read && status->fallback && status->readSince > 0) g.sub += " \xC2\xB7 lu par l'IHM (bascule " + wallClockText(status->readSince) + ")";
        else if (read) g.sub += " \xC2\xB7 lu par l'IHM";
        else g.sub += " \xC2\xB7 pr\xC3\xAAt \xC2\xB7 l'IHM lit le vrai";
        // (Son port et ses requetes : la ligne de l'esclave dans Configuration > Equipements.)
        lines.push_back(g);
        for (const auto& r : tw::valueRows(p, e)) {
            const hmi::Behavior* b = r.behavior >= 0 ? &e.behaviors[static_cast<std::size_t>(r.behavior)] : nullptr;
            const hmi::Forcing* f = r.forcing >= 0 ? &e.forcings[static_cast<std::size_t>(r.forcing)] : nullptr;
            const bool animated = b && b->enabled;
            if (show_ == 1 && !animated && !f) continue;
            // Lot recherche : la recherche de toutes les listes (mots ET, "phrase", -exclu).
            if (!search_.empty() && !ui::SearchQuery(search_).matches({r.variable, r.address, r.type})) continue;
            HmiTwinValues::Line l;
            l.kind = HmiTwinValues::Line::Kind::Row;
            l.equipment = e.name;
            l.key = r.address;
            l.address = r.address;
            l.type = r.type;
            l.boolean = r.boolean;
            l.running = port > 0;
            l.title = r.variable.empty() ? "registre " + r.address : r.variable;
            if (r.variable.empty()) l.sub = "sans variable";
            else if (r.scaled) l.sub = "brut " + tw::numberText(r.rawMin) + ".." + tw::numberText(r.rawMax) + " = " + tw::numberText(r.engMin) + ".." + tw::numberText(r.engMax);
            else if (r.boolean) l.sub = tableKind(r) + (r.written ? " \xC2\xB7 \xC3\xA9" "crite par l'IHM" : std::string{});
            else l.sub = r.written ? "\xC3\xA9" "crite par l'IHM" : "lue par l'IHM";
            l.animated = animated;
            l.hasBehavior = b != nullptr;
            l.kindLabel = b ? std::string(hmi::behaviorKindLabel(b->kind)) : std::string("\xE2\x80\x94");
            const std::optional<double> nowRaw = bank ? tw::rowValue(*bank, r, low) : std::nullopt;
            // La barre : reglee a la main, sinon faite une fois (elle ne saute pas avec la valeur).
            const std::string bk = e.name + "|" + r.address;
            std::pair<double, double> range;
            if (const auto it = bars_.find(bk); it != bars_.end()) range = it->second;
            else if (b && (bandKind(b->kind) || b->kind == hmi::BehaviorKind::Constant || b->kind == hmi::BehaviorKind::Steps || b->kind == hmi::BehaviorKind::Blink)) {
                // Autour de la zone (elle ne bouge pas avec la valeur) ; en valeur de la variable.
                hmi::Behavior be = *b;
                be.a = r.eng(b->a);
                be.b = r.eng(b->b);
                range = tw::barRange(r, &be, std::nullopt);
                autoBars_.erase(bk);
            } else {
                // Sans zone : autour de la valeur, gardee (la barre ne saute pas avec elle).
                const auto auto1 = tw::barRange(r, nullptr, nowRaw ? std::optional<double>(r.eng(*nowRaw)) : std::nullopt);
                auto& kept = autoBars_[bk];
                if (kept.first == kept.second || auto1.first < kept.first || auto1.second > kept.second) kept = auto1;
                range = kept;
            }
            l.barLo = range.first;
            l.barHi = range.second;
            if (b) {
                if (bandKind(b->kind)) {
                    l.barMode = 1;
                    l.lo = r.eng(b->a);
                    l.hi = r.eng(b->b);
                    if (l.lo > l.hi) std::swap(l.lo, l.hi);
                } else if (b->kind == hmi::BehaviorKind::Constant && !r.boolean) {
                    l.barMode = 2;
                    l.lo = l.hi = r.eng(b->a);
                } else if (b->kind == hmi::BehaviorKind::Blink) {
                    l.barMode = 3;
                    l.barLo = 0;
                    l.barHi = b->period > 0 ? b->period : 2;
                    l.lo = 0;
                    l.hi = b->delay > 0 ? b->delay : l.barHi / 2;
                } else if (b->kind == hmi::BehaviorKind::Steps && !r.boolean) {
                    double lo = 0, hi = 0;
                    bool any = false;
                    std::string cur;
                    for (char ch : b->source + ";") {
                        if (ch == ';' || ch == ' ' || ch == '|') {
                            if (!cur.empty()) {
                                std::replace(cur.begin(), cur.end(), ',', '.');
                                const double v = std::strtod(cur.c_str(), nullptr);
                                lo = any ? std::min(lo, v) : v;
                                hi = any ? std::max(hi, v) : v;
                                any = true;
                            }
                            cur.clear();
                        } else {
                            cur.push_back(ch);
                        }
                    }
                    l.barMode = any ? 1 : 0;
                    l.bandFixed = true;
                    l.lo = r.eng(lo);
                    l.hi = r.eng(hi);
                } else {
                    l.note = tw::behaviorText(*b);
                }
                if (periodKind(b->kind)) l.period = tw::numberText(b->period) + " s";
                else l.period = "\xE2\x80\x94";
            } else {
                l.period = "\xE2\x80\x94";
            }
            if (f) {
                l.forced = true;
                l.forcedText = tw::numberText(f->value);
                l.forcedEng = r.eng(f->value);
                if (r.boolean) l.boolForce = f->value != 0 ? 1 : 0;
                if (r.boolean && b && b->enabled) l.note = "forc\xC3\xA9 \xC3\xA0 " + l.forcedText + " \xC2\xB7 " + std::string(hmi::behaviorKindLabel(b->kind)) + " au d\xC3\xA9" "for\xC3\xA7" "age";
            }
            if (r.written && !animated) {
                l.warn = true;
                l.warnText = "\xC3\xA9" "crite par " + r.writers;
            }
            if (r.scaled) {
                const auto row1 = r;
                l.rawOf = [row1](double eng) { return "brut " + tw::numberText(std::round(row1.raw(eng))); };
            }
            l.tooltip = l.title + " \xC2\xB7 " + r.address + " (" + std::string(zn::tableLabel(r.table)) + ")" + (r.writers.empty() ? std::string{} : " \xC2\xB7 \xC3\xA9" "crite par " + r.writers);
            lines.push_back(std::move(l));
            refs_.push_back({e.name, r, low});
        }
        HmiTwinValues::Line add;
        add.kind = HmiTwinValues::Line::Kind::Add;
        add.equipment = e.name;
        add.title = "+ Ajouter un registre de " + e.name + " (" + (e.zones.declared ? zn::summary(e.zones) : std::string("zones non d\xC3\xA9" "clar\xC3\xA9" "es")) + ")\xE2\x80\xA6";
        lines.push_back(add);
    }
    if (auto* t = widget_->twinFilter()) {
        twins.front().label = "tous les esclaves simul\xC3\xA9s (" + std::to_string(nTwins) + ")";
        int sel = 0;
        for (std::size_t i = 1; i < twins.size(); ++i)
            if (twins[i].value == filter_) sel = static_cast<int>(i);
        bool same = t->items().size() == twins.size();
        for (std::size_t i = 0; same && i < twins.size(); ++i) same = t->items()[i].label == twins[i].label && t->items()[i].value == twins[i].value;
        if (!same) {
            t->setItems(twins);
            t->setSelectedIndex(sel);
        }
    }
    widget_->setLines(std::move(lines));
    widget_->select(selEquip_, selKey_);
    const auto n = counts();
    // 1.9 : "3 esclaves en marche - 5 animees - 2 forcees - 2 equipements lus en simule".
    widget_->setStatus(std::to_string(n.running) + (n.running > 1 ? " esclaves" : " esclave") + " en marche \xC2\xB7 " + std::to_string(n.animated) + " anim\xC3\xA9" "e"
                       + (n.animated > 1 ? "s" : "") + " \xC2\xB7 " + std::to_string(n.forced) + " forc\xC3\xA9" "e" + (n.forced > 1 ? "s" : "")
                       + (n.refused ? " \xC2\xB7 " + std::to_string(n.refused) + " \xC3\xA9" "criture" + (n.refused > 1 ? "s" : "") + " refus\xC3\xA9" "e" + (n.refused > 1 ? "s" : "") : std::string{})
                       + simulatedReadsTail());
}

// 1.9 : " - 2 equipements lus en simule" (vide : aucun).
std::string TwinValuesController::simulatedReadsTail() const {
    auto* h = host();
    if (!h) return {};
    std::size_t n = 0;
    for (const auto& st : h->statuses()) n += st.enabled && st.viaTwin ? 1 : 0;
    if (!n) return {};
    return " \xC2\xB7 " + std::to_string(n) + (n > 1 ? " \xC3\xA9quipements lus en simul\xC3\xA9" : " \xC3\xA9quipement lu en simul\xC3\xA9");
}

void TwinValuesController::tick() {
    if (!widget_) return;
    auto* h = host();
    const double t = tw::now();
    widget_->setNow(t);
    const bool takeSample = lastSample_ < 0 || t - lastSample_ >= 0.1;
    if (takeSample) lastSample_ = t;
    std::string lastEquip;
    std::shared_ptr<tw::TwinBank> bank;
    for (const auto& r : refs_) {
        if (r.equipment != lastEquip) {
            lastEquip = r.equipment;
            bank = h ? h->twinBank(r.equipment) : nullptr;
        }
        if (!bank) {
            widget_->setLive(r.equipment, r.row.address, "\xE2\x80\x94", "esclave arr\xC3\xAAt\xC3\xA9", std::nullopt, false);
            continue;
        }
        const auto raw = tw::rowValue(*bank, r.row, r.low);
        const bool forced = r.row.bit >= 0 ? ((bank->forcedMask(r.row.table, r.row.offset) >> r.row.bit) & 1) != 0
                                           : bank->forced(r.row.table, r.row.offset, r.row.boolean ? 1u : (r.row.type == "REAL" || r.row.type == "DINT" || r.row.type == "UDINT" || r.row.type == "DWORD" ? 2u : 1u));
        if (!raw) continue;
        const double eng = r.row.eng(*raw);
        std::string text = r.row.boolean ? (*raw != 0 ? "1" : "0") : valueText(eng, !r.row.scaled && integerType(r.row.type));
        std::string sub = r.row.scaled ? "brut " + valueText(*raw, true) : std::string{};
        widget_->setLive(r.equipment, r.row.address, text, sub, eng, forced);
        if (takeSample) widget_->sample(r.equipment, r.row.address, t, eng, forced);
    }
}

TwinValuesController::Counts TwinValuesController::counts() const {
    Counts c;
    if (!doc_) return c;
    auto* h = host();
    for (const auto& e : doc_->project.equipments) {
        if (!e.hasTwin()) continue;
        ++c.twins;
        if (h && h->simulatedPort(e.name)) ++c.running;
        for (const auto& b : e.behaviors) c.animated += b.enabled ? 1 : 0;
        c.forced += e.forcings.size();
        if (h)
            if (const auto bank = h->twinBank(e.name)) c.refused += bank->counters().forcedRefused;
    }
    for (const auto& r : refs_) (void)r;
    c.rows = refs_.size();
    return c;
}

bool TwinValuesController::changeBehaviors(const std::string& name, const std::string& label, const std::function<bool(std::vector<hmi::Behavior>&, std::string&)>& fn,
                                           std::string* why, const std::string& mergeKey) {
    const auto* e = equipment(name);
    if (!e || !e->hasTwin()) return fail(name + " n'a pas d'esclave simul\xC3\xA9", why);
    auto list = e->behaviors;
    std::string reason;
    if (!fn(list, reason)) return fail(reason, why);
    if (list == e->behaviors) return true;
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& x) {
        for (auto& q : x.equipments)
            if (q.name == name) q.behaviors = list;
    }, mergeKey);
    if (!cmd) return true;
    apply_(std::move(cmd));
    refresh();
    if (changed) changed();
    return true;
}

bool TwinValuesController::changeForcings(const std::string& name, const std::string& label, const std::function<bool(std::vector<hmi::Forcing>&, std::string&)>& fn,
                                          std::string* why) {
    const auto* e = equipment(name);
    if (!e || !e->hasTwin()) return fail(name + " n'a pas d'esclave simul\xC3\xA9", why);
    auto list = e->forcings;
    std::string reason;
    if (!fn(list, reason)) return fail(reason, why);
    if (list == e->forcings) return true;
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& x) {
        for (auto& q : x.equipments)
            if (q.name == name) q.forcings = list;
    });
    if (!cmd) return true;
    apply_(std::move(cmd));
    refresh();
    if (changed) changed();
    return true;
}

bool TwinValuesController::setAnimated(const std::string& name, const std::string& rawAddress, bool on, std::string* why) {
    const auto r = row(name, trimmedOf(rawAddress));
    if (!r) return fail(name + " : pas de ligne \xC2\xAB " + rawAddress + " \xC2\xBB (une variable li\xC3\xA9" "e ou un registre anim\xC3\xA9)", why);
    const auto* e = equipment(name);
    std::optional<double> now;
    if (host())
        if (const auto bank = host()->twinBank(name)) now = tw::rowValue(*bank, *r, e->wordOrder != "fort");
    const std::string what = r->variable.empty() ? r->address : r->variable;
    const bool ok = changeBehaviors(name, (on ? "Animer " : "Arr\xC3\xAAter ") + what, [&](std::vector<hmi::Behavior>& list, std::string&) {
        if (r->behavior >= 0 && static_cast<std::size_t>(r->behavior) < list.size()) {
            list[static_cast<std::size_t>(r->behavior)].enabled = on;
            return true;
        }
        if (!on) return true;
        list.push_back(tw::defaultBehavior(*r, now));
        return true;
    }, why);
    if (!ok) return false;
    select(name, r->address);
    if (say) {
        if (on && r->written) say(what + " anim\xC3\xA9" "e \xE2\x80\x94 attention : l'IHM l'\xC3\xA9" "crit (" + r->writers + "), le mouvement \xC3\xA9" "crase ce qu'elle y \xC3\xA9" "crit.", false);
        else if (const auto* e2 = equipment(name); e2 && on) {
            const auto r2 = row(name, r->address);
            const hmi::Behavior* b = r2 && r2->behavior >= 0 ? &e2->behaviors[static_cast<std::size_t>(r2->behavior)] : nullptr;
            say(e2->twinLabel() + " \xC2\xB7 " + what + " anim\xC3\xA9" "e : " + (b ? std::string(hmi::behaviorKindLabel(b->kind)) + " " + tw::behaviorText(*b) : std::string{}) + ".", false);
        } else {
            say(what + " ne bouge plus (ses r\xC3\xA9glages restent : recocher \xC2\xAB Animer \xC2\xBB les reprend).", false);
        }
    }
    return true;
}

bool TwinValuesController::setKind(const std::string& name, const std::string& rawAddress, const std::string& kindText, std::string* why) {
    const auto r = row(name, trimmedOf(rawAddress));
    if (!r) return fail(name + " : pas de ligne \xC2\xAB " + rawAddress + " \xC2\xBB", why);
    const std::string k = lowered(trimmedOf(kindText));
    const std::string what = r->variable.empty() ? r->address : r->variable;
    if (k.empty() || k.rfind("aucun", 0) == 0) {
        return changeBehaviors(name, what + " : plus de mouvement", [&](std::vector<hmi::Behavior>& list, std::string&) {
            if (r->behavior >= 0 && static_cast<std::size_t>(r->behavior) < list.size()) list.erase(list.begin() + r->behavior);
            return true;
        }, why);
    }
    const auto kind = hmi::behaviorKindFrom(kindText);
    if (!kind) return fail("mouvement \xC2\xAB " + kindText + " \xC2\xBB inconnu", why);
    const auto* e = equipment(name);
    std::optional<double> now;
    if (host())
        if (const auto bank = host()->twinBank(name)) now = tw::rowValue(*bank, *r, e->wordOrder != "fort");
    const bool ok = changeBehaviors(name, what + " : " + std::string(hmi::behaviorKindLabel(*kind)), [&](std::vector<hmi::Behavior>& list, std::string& reason) {
        hmi::Behavior b = r->behavior >= 0 && static_cast<std::size_t>(r->behavior) < list.size() ? list[static_cast<std::size_t>(r->behavior)] : tw::defaultBehavior(*r, now);
        const auto before = b.kind;
        b.kind = *kind;
        b.enabled = true;
        if (before != b.kind) {
            if (b.kind == hmi::BehaviorKind::Counter) {
                b.b = 1;
                b.period = 1;
            } else if (b.kind == hmi::BehaviorKind::Blink) {
                b.period = 2;
                b.delay = 0;
            } else if (b.kind == hmi::BehaviorKind::Steps && b.source.empty()) {
                b.source = tw::numberText(b.a) + "; " + tw::numberText((b.a + b.b) / 2) + "; " + tw::numberText(b.b);
                b.period = 5;
            } else if (b.kind == hmi::BehaviorKind::Constant && now) {
                b.a = *now;
            } else if (bandKind(b.kind) && b.b <= b.a) {
                b.b = b.a + 100;
            }
            if ((b.kind == hmi::BehaviorKind::Copy || b.kind == hmi::BehaviorKind::FollowPlc) && b.source.empty())
                b.source = b.kind == hmi::BehaviorKind::FollowPlc ? std::string("?") : r->address;
            if (b.period <= 0) b.period = 10;
        }
        if (!tw::validBehavior(b, &reason)) {
            reason = what + " : " + reason;
            return false;
        }
        if (r->behavior >= 0 && static_cast<std::size_t>(r->behavior) < list.size()) list[static_cast<std::size_t>(r->behavior)] = b;
        else list.push_back(b);
        return true;
    }, why);
    if (ok) select(name, r->address);
    return ok;
}

bool TwinValuesController::setBand(const std::string& name, const std::string& rawAddress, double lo, double hi, std::string* why) {
    const auto r = row(name, trimmedOf(rawAddress));
    if (!r) return fail(name + " : pas de ligne \xC2\xAB " + rawAddress + " \xC2\xBB", why);
    if (r->behavior < 0) return fail((r->variable.empty() ? r->address : r->variable) + " n'est pas anim\xC3\xA9" "e : cochez \xC2\xAB Animer \xC2\xBB d'abord", why);
    const auto* e = equipment(name);
    const auto& cur = e->behaviors[static_cast<std::size_t>(r->behavior)];
    const std::string what = r->variable.empty() ? r->address : r->variable;
    std::string text;
    const bool ok = changeBehaviors(name, what + " : zone de mouvement", [&](std::vector<hmi::Behavior>& list, std::string& reason) {
        auto& b = list[static_cast<std::size_t>(r->behavior)];
        if (b.kind == hmi::BehaviorKind::Blink) {
            if (hi <= 0 || hi > b.period) {
                reason = what + " : \xC3\xA0 1 pendant 0 \xC3\xA0 " + tw::numberText(b.period) + " s";
                return false;
            }
            b.delay = std::round(hi * 10) / 10;
            text = "\xC3\xA0 1 pendant " + tw::numberText(b.delay) + " s toutes les " + tw::numberText(b.period) + " s";
            return true;
        }
        double a = r->raw(std::min(lo, hi)), c = r->raw(std::max(lo, hi));
        if (a > c) std::swap(a, c);
        if (integerType(b.type)) {
            a = std::round(a);
            c = std::round(c);
        }
        if (b.kind == hmi::BehaviorKind::Constant) {
            b.a = integerType(b.type) ? std::round(r->raw(hi)) : r->raw(hi);
            text = "vaut " + valueText(r->eng(b.a), false);
        } else {
            b.a = a;
            b.b = c;
            text = valueText(r->eng(a), false) + " \xE2\x86\x92 " + valueText(r->eng(c), false) + (r->scaled ? " (brut " + tw::numberText(a) + " \xE2\x86\x92 " + tw::numberText(c) + ")" : std::string{});
        }
        return true;
    }, why);
    (void)cur;
    if (ok && say && !text.empty()) say(e->twinLabel() + " \xC2\xB7 " + what + " : " + text + " (Ctrl+Z revient \xC3\xA0 la zone d'avant).", false);
    return ok;
}

bool TwinValuesController::setField(const std::string& name, const std::string& rawAddress, const std::string& key, const std::string& rawValue, std::string* why) {
    const auto r = row(name, trimmedOf(rawAddress));
    if (!r) return fail(name + " : pas de ligne \xC2\xAB " + rawAddress + " \xC2\xBB", why);
    if (r->behavior < 0) return fail((r->variable.empty() ? r->address : r->variable) + " n'est pas anim\xC3\xA9" "e : cochez \xC2\xAB Animer \xC2\xBB d'abord", why);
    const std::string v = trimmedOf(rawValue);
    const std::string what = r->variable.empty() ? r->address : r->variable;
    return changeBehaviors(name, what + " : " + key, [&](std::vector<hmi::Behavior>& list, std::string& reason) {
        auto& b = list[static_cast<std::size_t>(r->behavior)];
        const auto number = [&](double& out) {
            std::string c = v;
            std::replace(c.begin(), c.end(), ',', '.');
            char* end = nullptr;
            const double d = std::strtod(c.c_str(), &end);
            if (c.empty() || (end && *end) || !std::isfinite(d)) {
                reason = key + " : un nombre";
                return false;
            }
            out = d;
            return true;
        };
        double d = 0;
        if (key == "periode") {
            if (!number(d) || d <= 0) {
                if (reason.empty()) reason = "p\xC3\xA9riode : plus de 0 s";
                return false;
            }
            b.period = d;
            if (b.kind == hmi::BehaviorKind::Blink && b.delay >= d) b.delay = 0;
        } else if (key == "retard") {
            if (!number(d)) return false;
            b.delay = d;
        } else if (key == "min" || key == "max") {
            if (!number(d)) return false;
            const double raw = integerType(b.type) ? std::round(r->raw(d)) : r->raw(d);
            const double span = std::fabs(b.b - b.a);
            if (b.kind == hmi::BehaviorKind::Constant) b.a = raw;
            else if (key == "min") {
                b.a = raw;
                if (bandKind(b.kind) && b.a > b.b) b.b = b.a + span;      // la bande suit : sa largeur reste
            } else {
                b.b = raw;
                if (bandKind(b.kind) && b.b < b.a) b.a = b.b - span;
            }
        } else if (key == "bruit") {
            // En valeur de la variable : le brut suit l'echelle (une largeur, pas une position).
            if (!number(d) || d < 0) {
                if (reason.empty()) reason = "bruit : 0 ou plus";
                return false;
            }
            b.noise = std::fabs(r->raw(d) - r->raw(0));
            if (integerType(b.type)) b.noise = std::round(b.noise);
        } else if (key == "a") {
            if (!number(b.a)) return false;
        } else if (key == "b") {
            if (!number(b.b)) return false;
        } else if (key == "source") {
            b.source = v;
        } else if (key == "type") {
            std::string u = v;
            for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            b.type = u;
        } else {
            reason = "r\xC3\xA9glage inconnu : " + key;
            return false;
        }
        if (!tw::validBehavior(b, &reason)) {
            reason = what + " : " + reason;
            return false;
        }
        return true;
    }, why);
}

bool TwinValuesController::setForced(const std::string& name, const std::string& rawAddress, const std::string& rawValue, std::string* why) {
    const auto r = row(name, trimmedOf(rawAddress));
    if (!r) return fail(name + " : pas de ligne \xC2\xAB " + rawAddress + " \xC2\xBB", why);
    const std::string v = trimmedOf(rawValue);
    const std::string what = r->variable.empty() ? r->address : r->variable;
    const auto* e = equipment(name);
    if (v.empty() || lowered(v) == "libre" || lowered(v) == "non") {
        const bool ok = changeForcings(name, "D\xC3\xA9" "forcer " + what, [&](std::vector<hmi::Forcing>& list, std::string&) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const hmi::Forcing& f) { return tw::sameCell(f.address, r->address); }), list.end());
            return true;
        }, why);
        if (ok && say) say(e->twinLabel() + " \xC2\xB7 " + what + " d\xC3\xA9" "forc\xC3\xA9" "e : elle garde sa valeur jusqu'\xC3\xA0 la prochaine \xC3\xA9" "criture ou au mouvement.", false);
        if (ok) select(name, r->address);
        return ok;
    }
    double raw = 0;
    std::string reason;
    if (!tw::parseRaw(*r, v, raw, &reason)) return fail(what + " : " + reason, why);
    const bool ok = changeForcings(name, "Forcer " + what + " \xC3\xA0 " + tw::numberText(raw), [&](std::vector<hmi::Forcing>& list, std::string& reason2) {
        hmi::Forcing f;
        f.address = r->address;
        f.type = r->boolean ? std::string("BOOL") : r->type;
        f.value = raw;
        f.since = hmi::nowStamp();
        std::vector<tw::TwinBank::ForcedCell> cells;
        if (!tw::forcedCells(f, e->wordOrder != "fort", cells, &reason2)) return false;
        for (auto& x : list)
            if (tw::sameCell(x.address, r->address)) {
                f.since = x.value == raw ? x.since : f.since;
                x = f;
                return true;
            }
        list.push_back(f);
        return true;
    }, why);
    if (ok && say) {
        std::string text = e->twinLabel() + " \xC2\xB7 " + what + " forc\xC3\xA9" "e \xC3\xA0 " + tw::rowForcingText(*r, raw)
                           + " : chaque lecture la rend ; une \xC3\xA9" "criture y est refus\xC3\xA9" "e (exception 04, compt\xC3\xA9" "e).";
        say(text, false);
    }
    if (ok) select(name, r->address);
    return ok;
}

bool TwinValuesController::unforceAll(std::string* why) {
    std::size_t n = 0;
    for (const auto& e : doc_->project.equipments) n += e.forcings.size();
    if (!n) {
        if (say) say("Aucune case forc\xC3\xA9" "e.", false);
        return true;
    }
    auto cmd = hmi::changeProject(doc_, "D\xC3\xA9" "forcer tout", [&](hmi::Project& x) {
        for (auto& q : x.equipments) q.forcings.clear();
    });
    (void)why;
    if (cmd) apply_(std::move(cmd));
    refresh();
    if (changed) changed();
    if (say) say(std::to_string(n) + " case" + (n > 1 ? "s" : "") + " d\xC3\xA9" "forc\xC3\xA9" "e" + (n > 1 ? "s" : "") + " : les \xC3\xA9" "critures y passent de nouveau.", false);
    return true;
}

bool TwinValuesController::animateAll(bool on, std::string* why) {
    std::size_t n = 0;
    for (const auto& e : doc_->project.equipments)
        if (e.hasTwin())
            for (const auto& b : e.behaviors) n += b.enabled != on ? 1 : 0;
    if (!n) {
        if (say) say(on ? "Rien \xC3\xA0 animer : chaque mouvement est d\xC3\xA9j\xC3\xA0 en marche (cochez \xC2\xAB Animer \xC2\xBB sur une ligne pour en cr\xC3\xA9" "er un)."
                        : "Rien ne bouge d\xC3\xA9j\xC3\xA0.", false);
        return true;
    }
    auto cmd = hmi::changeProject(doc_, on ? "Tout animer" : "Tout arr\xC3\xAAter", [&](hmi::Project& x) {
        for (auto& q : x.equipments)
            if (q.hasTwin())
                for (auto& b : q.behaviors) b.enabled = on;
    });
    (void)why;
    if (cmd) apply_(std::move(cmd));
    refresh();
    if (changed) changed();
    if (say) say(std::to_string(n) + " mouvement" + (n > 1 ? "s" : "") + (on ? " repris" : " arr\xC3\xAAt\xC3\xA9" + std::string(n > 1 ? "s" : "") + " (leurs r\xC3\xA9glages restent)") + ".", false);
    return true;
}

std::string TwinValuesController::addRegister(const std::string& name, const std::string& rawAddress, std::string* why) {
    const auto* e = equipment(name);
    if (!e || !e->hasTwin()) {
        (void)fail(name + " n'a pas d'esclave simul\xC3\xA9", why);
        return {};
    }
    std::string address = trimmedOf(rawAddress);
    if (address.empty()) {
        // La premiere case libre (registres de maintien, puis d'entree) : ni variable, ni mouvement, ni forcage.
        const auto rows = tw::valueRows(doc_->project, *e);
        const auto m = zn::buildMap(doc_->project, *e);
        for (const MemTable t : {MemTable::Holding, MemTable::InputRegisters}) {
            std::vector<hmi::MemRange> ranges = e->zones.declared ? e->zones.of(t) : std::vector<hmi::MemRange>{{0, 99}};
            for (const auto& rg : ranges) {
                for (std::uint32_t o = rg.first; o <= rg.last && address.empty(); ++o) {
                    bool used = !m.at(t, o).empty();
                    for (const auto& r : rows) used = used || (r.table == t && r.offset == o);
                    if (!used) address = zn::modicon(t, o);
                }
                if (!address.empty()) break;
            }
            if (!address.empty()) break;
        }
        if (address.empty()) {
            (void)fail(e->name + " : pas de case libre dans ses zones", why);
            return {};
        }
    }
    hmi::Behavior b;
    b.address = address;
    b.type = "INT";
    hmi::comm::Point pt;
    if (hmi::equip::placeEquipmentAddress(address, hmi::equip::typeOfName("INT"), pt) && pt.bits()) {
        b.type = "BOOL";
        b.kind = hmi::BehaviorKind::Blink;
        b.period = 2;
    } else {
        b.kind = hmi::BehaviorKind::Steps;
        b.source = "1; 3; 5; 2";
        b.period = 5;
    }
    std::string reason;
    if (!tw::validBehavior(b, &reason)) {
        (void)fail(address + " : " + reason, why);
        return {};
    }
    for (const auto& x : e->behaviors)
        if (tw::sameCell(x.address, address)) {
            (void)fail(address + " a d\xC3\xA9j\xC3\xA0 un mouvement", why);
            return {};
        }
    if (!changeBehaviors(name, e->name + " : un registre anim\xC3\xA9 (" + address + ")", [&](std::vector<hmi::Behavior>& list, std::string&) {
            list.push_back(b);
            return true;
        }, why))
        return {};
    select(name, address);
    if (say) say(e->twinLabel() + " \xC2\xB7 registre " + address + " anim\xC3\xA9 (" + std::string(hmi::behaviorKindLabel(b.kind)) + " " + tw::behaviorText(b) + ") \xE2\x80\x94 sans variable IHM : l'outil Modbus et la carte le voient.", false);
    return address;
}

bool TwinValuesController::setBarRange(const std::string& name, const std::string& address, double lo, double hi) {
    if (!(hi > lo)) return false;
    const auto* r = ref(name, address);
    bars_[name + "|" + (r ? r->row.address : address)] = {lo, hi};
    refresh();
    return true;
}

// ------------------------------------------------------------------- la fiche ---
void TwinValuesController::properties(std::vector<PG::Category>& cats) {
    const auto* e = equipment(selEquip_);
    const auto r = e ? row(selEquip_, selKey_) : std::nullopt;
    if (!e || !r) {
        PG::Category c;
        c.name = "Valeurs simul\xC3\xA9" "es";
        const auto n = counts();
        c.properties.push_back(prop("Esclaves simul\xC3\xA9s", std::to_string(n.twins) + " (" + std::to_string(n.running) + " en marche)", PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Lignes", std::to_string(n.rows), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Anim\xC3\xA9" "es", std::to_string(n.animated), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Forc\xC3\xA9" "es", std::to_string(n.forced), PG::ValueType::ReadOnly));
        c.properties.push_back(prop("\xC3\x89" "critures refus\xC3\xA9" "es", std::to_string(n.refused) + " (sur des cases forc\xC3\xA9" "es : exception 04)", PG::ValueType::ReadOnly));
        c.properties.push_back(prop("Une ligne", "un clic : sa fiche ici ; la case : animer ; les poign\xC3\xA9" "es : la zone", PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
        return;
    }
    const std::string name = e->name, address = r->address;
    const bool low = e->wordOrder != "fort";
    const hmi::Behavior* b = r->behavior >= 0 ? &e->behaviors[static_cast<std::size_t>(r->behavior)] : nullptr;
    const hmi::Forcing* f = r->forcing >= 0 ? &e->forcings[static_cast<std::size_t>(r->forcing)] : nullptr;
    std::shared_ptr<tw::TwinBank> bank = host() ? host()->twinBank(name) : nullptr;
    const std::optional<double> now = bank ? tw::rowValue(*bank, *r, low) : std::nullopt;
    const std::string what = r->variable.empty() ? "registre " + r->address : r->variable;
    PG::Category c;
    c.name = what + " \xC2\xB7 " + e->twinLabel();
    c.properties.push_back(prop("Adresse", r->address + "  (" + zn::schneider(r->table, r->offset) + ")", PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Type", r->type, PG::ValueType::ReadOnly));
    if (r->scaled)
        c.properties.push_back(prop("Mise \xC3\xA0 l'\xC3\xA9" "chelle", "brut " + tw::numberText(r->rawMin) + " \xE2\x86\x92 " + tw::numberText(r->rawMax) + " = " + tw::numberText(r->engMin) + " \xE2\x86\x92 " + tw::numberText(r->engMax),
                                    PG::ValueType::ReadOnly));
    if (bank) {
        const auto a = bank->access(r->table, r->offset);
        const double t = tw::now();
        c.properties.push_back(prop("Lue par", a.readAt < 0 ? std::string("personne") : a.reader + " (" + ageOf(t - a.readAt) + ")", PG::ValueType::ReadOnly));
        c.properties.push_back(prop("\xC3\x89" "crite par", a.writeAt < 0 ? std::string("personne") : a.writer + " (" + ageOf(t - a.writeAt) + ")", PG::ValueType::ReadOnly));
    }
    if (r->written) c.properties.push_back(prop("L'IHM l'\xC3\xA9" "crit", r->writers, PG::ValueType::ReadOnly));
    if (now) c.properties.push_back(prop("Valeur", r->boolean ? std::string(*now != 0 ? "1" : "0") : valueText(r->eng(*now), false) + (r->scaled ? "  brut " + valueText(*now, true) : std::string{}), PG::ValueType::ReadOnly));
    cats.push_back(std::move(c));
    // Animer.
    {
        PG::Category a;
        a.name = "Animer";
        a.properties.push_back(prop("Animer", b && b->enabled ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                    [this, name, address](std::string_view v) { return setAnimated(name, address, v == "TRUE" || v == "true" || v == "1"); },
                                    "La valeur bouge toute seule. D\xC3\xA9" "coch\xC3\xA9" "e : ses r\xC3\xA9glages restent."));
        std::vector<std::string> kinds{"aucun"};
        for (const auto k : hmi::kBehaviorKinds) kinds.emplace_back(hmi::behaviorKindLabel(k));
        a.properties.push_back(prop("Mouvement", b ? std::string(hmi::behaviorKindLabel(b->kind)) : std::string("aucun"), PG::ValueType::Enum,
                                    [this, name, address](std::string_view v) { return setKind(name, address, std::string(v)); },
                                    "sinus, rampe, al\xC3\xA9" "atoire : entre le minimum et le maximum ; clignote : \xC3\xA0 1 une partie de la p\xC3\xA9riode ; "
                                    "\xC3\xA9tapes : une liste de valeurs ; recopie : une autre case ; suit l'automate : une variable du simulateur.",
                                    kinds));
        if (b) {
            const auto field = [this, name, address](const char* key) {
                return [this, name, address, key](std::string_view v) { return setField(name, address, key, std::string(v)); };
            };
            const auto rawNote = [&](double raw) { return r->scaled ? "   brut " + valueText(raw, true) : std::string{}; };
            if (bandKind(b->kind)) {
                a.properties.push_back(prop("Minimum", valueText(r->eng(b->a), false) + rawNote(b->a), PG::ValueType::Text, field("min"),
                                            "La borne basse (en valeur de la variable ; le brut suit l'\xC3\xA9" "chelle). La poign\xC3\xA9" "e de gauche."));
                a.properties.push_back(prop("Maximum", valueText(r->eng(b->b), false) + rawNote(b->b), PG::ValueType::Text, field("max"), "La borne haute. La poign\xC3\xA9" "e de droite."));
            } else if (b->kind == hmi::BehaviorKind::Constant) {
                a.properties.push_back(prop("Vaut", valueText(r->eng(b->a), false) + rawNote(b->a), PG::ValueType::Text, field("min")));
            } else if (b->kind == hmi::BehaviorKind::Counter) {
                a.properties.push_back(prop("D\xC3\xA9part", tw::numberText(b->a), PG::ValueType::Real, field("a")));
                a.properties.push_back(prop("Pas", tw::numberText(b->b), PG::ValueType::Real, field("b")));
            } else if (b->kind == hmi::BehaviorKind::Blink) {
                a.properties.push_back(prop("\xC3\x80 1 pendant (s)", tw::numberText(b->delay > 0 ? b->delay : b->period / 2), PG::ValueType::Real, field("retard"),
                                            "La dur\xC3\xA9" "e \xC3\xA0 1 dans chaque p\xC3\xA9riode (0 : une moiti\xC3\xA9)."));
            } else if (b->kind == hmi::BehaviorKind::Steps) {
                a.properties.push_back(prop("Valeurs", b->source, PG::ValueType::Text, field("source"), "1; 5; 3 : rejou\xC3\xA9" "es une par p\xC3\xA9riode (brut)."));
            } else if (b->kind == hmi::BehaviorKind::Copy) {
                a.properties.push_back(prop("Recopie de", b->source, PG::ValueType::Text, field("source")));
                a.properties.push_back(prop("Retard (s)", tw::numberText(b->delay), PG::ValueType::Real, field("retard")));
            } else if (b->kind == hmi::BehaviorKind::FollowPlc) {
                a.properties.push_back(prop("Variable de l'automate", b->source, PG::ValueType::Text, field("source")));
            }
            if (periodKind(b->kind)) a.properties.push_back(prop("P\xC3\xA9riode (s)", tw::numberText(b->period), PG::ValueType::Real, field("periode")));
            if (!r->boolean && b->kind != hmi::BehaviorKind::Blink)
                a.properties.push_back(prop("Bruit (\xC2\xB1)", valueText(std::fabs(r->eng(b->noise) - r->eng(0)), false) + (r->scaled && b->noise > 0 ? "   brut " + valueText(b->noise, true) : std::string{}),
                                            PG::ValueType::Text, field("bruit"), "Un bruit ajout\xC3\xA9 au mouvement, \xC2\xB1 cette valeur (0 : aucun) - une mesure qui vit."));
            a.properties.push_back(prop("En clair", tw::behaviorText(*b) + (b->enabled ? std::string{} : " (arr\xC3\xAAt\xC3\xA9)"), PG::ValueType::ReadOnly));
            std::string whyb;
            if (!tw::validBehavior(*b, &whyb)) a.properties.push_back(prop("Refus\xC3\xA9", whyb, PG::ValueType::ReadOnly));
        }
        if (r->written && !(b && b->enabled))
            a.properties.push_back(prop("Attention", "l'IHM l'\xC3\xA9" "crit : l'animer \xC3\xA9" "craserait ce qu'elle y \xC3\xA9" "crit (d\xC3\xA9" "coch\xC3\xA9" "e d'office)", PG::ValueType::ReadOnly));
        if (!r->boolean) {
            const auto* ref1 = ref(name, address);
            std::pair<double, double> range{0, 100};
            if (ref1 && widget_)
                if (const auto* l = widget_->line(name, address)) range = {l->barLo, l->barHi};
            a.properties.push_back(prop("La barre va de", valueText(range.first, false) + " \xC3\xA0 " + valueText(range.second, false), PG::ValueType::Text,
                                        [this, name, address](std::string_view v) {
                                            std::string s(v);
                                            for (const char* sep : {" \xC3\xA0 ", " a ", ";", "..", " - "}) {
                                                const auto at = s.find(sep);
                                                if (at == std::string::npos) continue;
                                                std::string x = trimmedOf(s.substr(0, at)), y = trimmedOf(s.substr(at + std::string(sep).size()));
                                                std::replace(x.begin(), x.end(), ',', '.');
                                                std::replace(y.begin(), y.end(), ',', '.');
                                                char* e1 = nullptr;
                                                char* e2 = nullptr;
                                                const double lo = std::strtod(x.c_str(), &e1), hi = std::strtod(y.c_str(), &e2);
                                                if (e1 && !*e1 && e2 && !*e2 && hi > lo) return setBarRange(name, address, lo, hi);
                                            }
                                            if (say) say("La barre : \xC2\xAB 0 \xC3\xA0 25 \xC2\xBB", true);
                                            return false;
                                        },
                                        "L'\xC3\xA9tendue de la barre (l'\xC3\xA9" "cran, pas le projet) : \xC2\xAB 0 \xC3\xA0 25 \xC2\xBB."));
        }
        cats.push_back(std::move(a));
    }
    // Forcer.
    {
        PG::Category fc;
        fc.name = "Forcer";
        fc.properties.push_back(prop("Forcer", f ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                     [this, name, address, now](std::string_view v) {
                                         if (!(v == "TRUE" || v == "true" || v == "1")) return setForced(name, address, {});
                                         std::string raw = now ? tw::numberText(*now) : std::string("0");
                                         for (auto& ch : raw)
                                             if (ch == ',') ch = '.';
                                         return setForced(name, address, raw);
                                     },
                                     "Tenir la case \xC3\xA0 sa valeur brute : chaque lecture la rend ; une \xC3\xA9" "criture (l'IHM, l'outil Modbus, \xC2\xAB \xC3\x89" "crire une valeur \xC2\xBB) "
                                     "est refus\xC3\xA9" "e (exception 04) et compt\xC3\xA9" "e. Le mouvement reprend au d\xC3\xA9" "for\xC3\xA7" "age."));
        fc.properties.push_back(prop("\xC3\x80 la valeur brute", f ? tw::numberText(f->value) : std::string{}, PG::ValueType::Text,
                                     [this, name, address](std::string_view v) { return setForced(name, address, std::string(v)); },
                                     r->boolean ? std::string("0 ou 1") : "Ce qui est dans le registre (" + r->type + ") : 5100, 0x1F, 12.5 ; vide : d\xC3\xA9" "forcer."));
        if (f) {
            if (r->scaled) fc.properties.push_back(prop("Soit", valueText(r->eng(f->value), false) + " (l'\xC3\xA9" "chelle de la variable)", PG::ValueType::ReadOnly));
            if (!f->since.empty()) fc.properties.push_back(prop("Depuis", f->since, PG::ValueType::ReadOnly));
        }
        if (bank) {
            const auto cn = bank->counters();
            fc.properties.push_back(prop("\xC3\x89" "critures refus\xC3\xA9" "es", std::to_string(cn.forcedRefused) + (cn.lastRefused.empty() ? std::string{} : " (la derni\xC3\xA8re : " + cn.lastRefused + ", " + ageOf(tw::now() - cn.lastRefusedAt) + ")"),
                                         PG::ValueType::ReadOnly));
        }
        fc.properties.push_back(prop("Les \xC3\xA9" "critures", "refus\xC3\xA9" "es sur une case forc\xC3\xA9" "e (exception 04), compt\xC3\xA9" "es", PG::ValueType::ReadOnly));
        cats.push_back(std::move(fc));
    }
}

} // namespace app
