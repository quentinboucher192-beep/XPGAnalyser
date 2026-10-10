#include "HmiControls.hpp"

#include <algorithm>
#include <mutex>
#include <thread>
#include <utility>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Sans ses apostrophes ('Azote' -> Azote), sans les blancs autour.
std::string unquoted(std::string_view s) {
    std::string t = trim(s);
    if (t.size() >= 2 && ((t.front() == '\'' && t.back() == '\'') || (t.front() == '"' && t.back() == '"'))) t = t.substr(1, t.size() - 2);
    return t;
}
std::vector<std::string> splitKeep(std::string_view text, char sep) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto at = text.find(sep, from);
        out.push_back(trim(text.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from)));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    return out;
}
// Un nombre, ou TRUE / FALSE (1 / 0).
bool numeric(std::string_view s, double& out) {
    const std::string t = unquoted(s);
    if (parseNumber(t, out)) return true;
    const std::string l = lower(t);
    if (l == "true" || l == "vrai") { out = 1; return true; }
    if (l == "false" || l == "faux") { out = 0; return true; }
    return false;
}
double fontSizeOf(const Object& o, double fallback) { return std::clamp(o.number("fontSize", fallback), 6.0, 96.0); }

} // namespace

// ================================================================== listes ===
std::vector<std::string> listItems(std::string_view text) {
    std::vector<std::string> out;
    for (auto& s : splitKeep(text, ';')) if (!s.empty()) out.push_back(std::move(s));
    return out;
}

namespace {
// Un moteur repond sur SON fil (celui qui l'a pose) : un controle lance ailleurs (la generation
// en arriere-plan) voit la source, comme l'editeur, sans toucher au moteur qui tourne.
struct ResolverEntry {
    const void*     owner{nullptr};
    std::thread::id thread{};
    ChoiceResolver  resolve;
};
std::mutex& resolversLock() {
    static std::mutex m;
    return m;
}
std::vector<ResolverEntry>& choiceResolvers() {
    static std::vector<ResolverEntry> r;
    return r;
}
ChoiceResolver choiceResolver() {
    std::lock_guard<std::mutex> lock(resolversLock());
    const auto& r = choiceResolvers();
    const auto here = std::this_thread::get_id();
    for (auto it = r.rbegin(); it != r.rend(); ++it)
        if (it->thread == here) return it->resolve;
    return {};
}
} // namespace

void registerChoiceResolver(const void* owner, ChoiceResolver resolver) {
    unregisterChoiceResolver(owner);
    if (!resolver) return;
    std::lock_guard<std::mutex> lock(resolversLock());
    choiceResolvers().push_back({owner, std::this_thread::get_id(), std::move(resolver)});
}

void unregisterChoiceResolver(const void* owner) noexcept {
    std::lock_guard<std::mutex> lock(resolversLock());
    std::erase_if(choiceResolvers(), [owner](const ResolverEntry& e) { return e.owner == owner; });
}

bool hasChoiceResolver() noexcept { return static_cast<bool>(choiceResolver()); }

std::string itemsSourceOf(const Object& o) {
    std::string t = trim(o.text("itemsFrom"));
    std::erase(t, '$');                        // un repere : $Liste$ -> Liste (resolu avant par la vue)
    return trim(t);
}

std::vector<Choice> choicesOf(const Object& o) {
    // 1.12.2 : les elements venus d'ailleurs (une enumeration, une liste, un tableau, une MAP).
    if (const std::string from = itemsSourceOf(o); !from.empty()) {
        std::vector<Choice> out;
        if (const auto resolve = choiceResolver()) {
            if (!resolve(from, out)) out.clear();
            return out;
        }
        Choice c;
        c.label = "\xE2\x86\x90 " + from;
        c.value = "0";
        c.index = true;
        return {c};
    }
    const auto labels = listItems(o.text(o.kind == Kind::Selector ? "positions" : "items"));
    const std::string rawValues = o.text("values");
    const auto values = rawValues.empty() ? std::vector<std::string>{} : splitKeep(rawValues, ';');
    std::vector<Choice> out;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        Choice c;
        c.label = labels[i];
        if (i < values.size() && !values[i].empty()) c.value = values[i];
        else {
            c.value = std::to_string(i);
            c.index = true;
        }
        out.push_back(std::move(c));
    }
    return out;
}

int choiceIndexOf(const std::vector<Choice>& choices, std::string_view shown) {
    double sv = 0;
    const bool sNum = numeric(shown, sv);
    const std::string s = lower(unquoted(shown));
    for (std::size_t i = 0; i < choices.size(); ++i) {
        double vv = 0;
        if (sNum && numeric(choices[i].value, vv) && std::fabs(vv - sv) < 1e-9) return static_cast<int>(i);
        if (!choices[i].index && lower(unquoted(choices[i].value)) == s) return static_cast<int>(i);
    }
    // Une STRING qui contient le libelle (pas de valeurs donnees).
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (lower(choices[i].label) == s) return static_cast<int>(i);
    return -1;
}

// =================================================================== etats ===
std::vector<StateEntry> parseStateList(std::string_view text, std::string* error) {
    std::vector<StateEntry> out;
    for (const auto& entry : listItems(text)) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) {
            if (error && error->empty()) *error = "\xC3\xA9tat sans \xC2\xAB = \xC2\xBB : " + entry;
            continue;
        }
        StateEntry e;
        e.match = trim(std::string_view(entry).substr(0, eq));
        if (e.match.empty()) {
            if (error && error->empty()) *error = "\xC3\xA9tat sans valeur : " + entry;
            continue;
        }
        const auto parts = splitKeep(std::string_view(entry).substr(eq + 1), '|');
        e.text = parts.empty() ? std::string{} : parts[0];
        for (std::size_t k = 1; k < parts.size(); ++k) {
            const std::string p = lower(parts[k]);
            if (p.empty()) continue;
            if (p == "clignote" || p == "clignotant" || p == "blink") e.blink = true;
            else if (e.color.empty()) e.color = parts[k];
            else if (error && error->empty()) *error = "\xC3\xA9tat " + e.match + " : \xC2\xAB " + parts[k] + " \xC2\xBB inconnu (une couleur, ou clignote)";
        }
        out.push_back(std::move(e));
    }
    return out;
}

bool stateFieldFits(std::string_view text, std::string* why) {
    if (text.find(';') == std::string_view::npos && text.find('|') == std::string_view::npos) return true;
    if (why) *why = "\xC2\xAB ; \xC2\xBB et \xC2\xAB | \xC2\xBB s\xC3\xA9parent les \xC3\xA9tats : ils ne peuvent pas \xC3\xAAtre dans un texte";
    return false;
}

std::string formatStateList(const std::vector<StateEntry>& states) {
    std::string out;
    for (const auto& e : states) {
        if (!out.empty()) out += "; ";
        out += trim(e.match) + " = " + trim(e.text);
        if (!trim(e.color).empty()) out += " | " + trim(e.color);
        if (e.blink) out += " | clignote";
    }
    return out;
}

namespace {
// Le texte sans ses parentheses exterieures, quand elles l'enveloppent entier : "(A AND B)" -> "A AND B".
std::string unwrapped(std::string t) {
    t = trim(t);
    while (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        int depth = 0;
        bool whole = true;
        char quote = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (quote) { if (c == quote) quote = 0; continue; }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '(') ++depth;
            else if (c == ')' && --depth == 0 && i + 1 < t.size()) { whole = false; break; }
        }
        if (!whole) break;
        t = trim(std::string_view(t).substr(1, t.size() - 2));
    }
    return t;
}
// La premiere position, hors parentheses et hors textes, de `c` seul (pas "??" ni ":=") ; npos : aucune.
std::size_t topLevel(std::string_view s, char c, std::size_t from = 0) {
    int depth = 0;
    char quote = 0;
    for (std::size_t i = from; i < s.size(); ++i) {
        const char ch = s[i];
        if (quote) { if (ch == quote) quote = 0; continue; }
        if (ch == '\'' || ch == '"') { quote = ch; continue; }
        if (ch == '(' || ch == '[') ++depth;
        else if ((ch == ')' || ch == ']') && depth > 0) --depth;
        else if (depth == 0 && ch == c) {
            if (c == '?' && ((i + 1 < s.size() && s[i + 1] == '?') || (i > 0 && s[i - 1] == '?'))) continue;
            if (c == ':' && i + 1 < s.size() && s[i + 1] == '=') continue;
            return i;
        }
    }
    return std::string_view::npos;
}
} // namespace

std::string conditionChain(const std::vector<std::string>& conditions) {
    std::string out;
    for (std::size_t k = 0; k < conditions.size(); ++k)
        out += "(" + unwrapped(conditions[k].empty() ? std::string("FALSE") : conditions[k]) + ") ? " + std::to_string(k + 1) + " : ";
    return out + "0";
}

bool parseConditionChain(std::string_view expr, std::vector<std::string>& conditions) {
    conditions.clear();
    std::string rest = trim(expr);
    if (!rest.empty() && rest.front() == '=') rest = trim(std::string_view(rest).substr(1));
    for (int k = 1;; ++k) {
        if (unwrapped(rest) == "0") return !conditions.empty();
        const auto q = topLevel(rest, '?');
        if (q == std::string::npos) return false;
        const auto c = topLevel(rest, ':', q + 1);
        if (c == std::string::npos) return false;
        const std::string cond = unwrapped(rest.substr(0, q));
        const std::string num = trim(std::string_view(rest).substr(q + 1, c - q - 1));
        if (cond.empty() || num != std::to_string(k)) return false;
        conditions.push_back(cond);
        rest = trim(std::string_view(rest).substr(c + 1));
    }
}

int stateIndexOf(const std::vector<StateEntry>& states, std::string_view shown) {
    double sv = 0;
    const bool sNum = numeric(shown, sv);
    const std::string s = lower(unquoted(shown));
    int fallback = -1;
    for (std::size_t i = 0; i < states.size(); ++i) {
        const std::string& m = states[i].match;
        if (m == "*") {
            if (fallback < 0) fallback = static_cast<int>(i);
            continue;
        }
        if (const auto dots = m.find(".."); dots != std::string::npos) {
            double a = 0, b = 0;
            if (sNum && parseNumber(m.substr(0, dots), a) && parseNumber(m.substr(dots + 2), b)
                && sv >= std::min(a, b) - 1e-9 && sv <= std::max(a, b) + 1e-9)
                return static_cast<int>(i);
            continue;
        }
        double mv = 0;
        if (numeric(m, mv)) {
            if (sNum && std::fabs(mv - sv) < 1e-9) return static_cast<int>(i);
            continue;
        }
        if (lower(unquoted(m)) == s) return static_cast<int>(i);
    }
    return fallback;
}

// =================================================================== zones ===
std::vector<Zone> parseZones(std::string_view text, std::string* error) {
    std::vector<Zone> out;
    for (const auto& entry : listItems(text)) {
        const auto eq = entry.find('=');
        const std::string range = trim(std::string_view(entry).substr(0, eq == std::string::npos ? entry.size() : eq));
        Zone z;
        z.color = eq == std::string::npos ? std::string{} : trim(std::string_view(entry).substr(eq + 1));
        std::string a, b;
        if (const auto dots = range.find(".."); dots != std::string::npos) {
            a = range.substr(0, dots);
            b = range.substr(dots + 2);
        } else {
            // Le tiret qui separe (pas le signe d'un nombre negatif).
            for (std::size_t k = 1; k < range.size(); ++k) {
                if (range[k] != '-') continue;
                std::size_t p = k;
                while (p > 0 && range[p - 1] == ' ') --p;
                if (p > 0 && (std::isdigit(static_cast<unsigned char>(range[p - 1])) || range[p - 1] == '.')) {
                    a = range.substr(0, k);
                    b = range.substr(k + 1);
                    break;
                }
            }
        }
        if (a.empty() || b.empty() || !parseNumber(a, z.from) || !parseNumber(b, z.to) || z.color.empty()) {
            if (error && error->empty()) *error = "zone illisible : \xC2\xAB " + entry + " \xC2\xBB (de-\xC3\xA0 = couleur, par ex. 0-60 = #2ECC71)";
            continue;
        }
        if (z.from > z.to) std::swap(z.from, z.to);
        out.push_back(std::move(z));
    }
    return out;
}

const Zone* zoneOf(const std::vector<Zone>& zones, double value) {
    for (const auto& z : zones)
        if (value >= z.from - 1e-9 && value <= z.to + 1e-9) return &z;
    return nullptr;
}

// ========================================================= valeurs bornees ===
double snapToStep(double value, double min, double max, double step) {
    const double lo = std::min(min, max), hi = std::max(min, max);
    double v = std::clamp(value, lo, hi);
    if (step > 0) {
        v = lo + std::round((v - lo) / step) * step;
        v = std::clamp(v, lo, hi);
        v = std::round(v * 1e9) / 1e9;     // pas de 0.30000000000000004
    }
    return v;
}

double fractionOf(double value, double min, double max) {
    if (!(max > min)) return 0;
    return std::clamp((value - min) / (max - min), 0.0, 1.0);
}

SliderLayout sliderLayout(const Object& o, double w, double h) {
    SliderLayout l;
    l.vertical = o.text("orientation") == "verticale";
    const double fs = fontSizeOf(o, 13);
    const bool showValue = o.flag("showValue", true);
    if (!l.vertical) {
        l.knob = std::clamp(h * 0.2, 7.0, 14.0);
        const double valueW = showValue ? std::min(w * 0.34, fs * 5.2) : 0;
        const double cy = std::max(l.knob + 2, h * 0.4);
        l.track = {l.knob + 2, cy - 3, std::max(4.0, w - valueW - 2 * l.knob - 6), 6};
        if (showValue) l.value = {w - valueW, 0, valueW, h * 0.8};
    } else {
        l.knob = std::clamp(w * 0.2, 7.0, 14.0);
        const double valueH = showValue ? fs * 2.0 : 0;
        const double cx = std::max(l.knob + 2, w * 0.4);
        l.track = {cx - 3, l.knob + 2, 6, std::max(4.0, h - valueH - 2 * l.knob - 6)};
        if (showValue) l.value = {0, h - valueH, w, valueH};
    }
    return l;
}

double sliderFractionAt(const Object& o, double w, double h, double x, double y) {
    const auto l = sliderLayout(o, w, h);
    if (!l.vertical) return l.track.w > 0 ? std::clamp((x - l.track.x) / l.track.w, 0.0, 1.0) : 0;
    return l.track.h > 0 ? std::clamp(1.0 - (y - l.track.y) / l.track.h, 0.0, 1.0) : 0;
}

double knobFractionAt(double w, double h, double x, double y) {
    const double cx = w / 2, cy = h / 2;
    const double dx = x - cx, dy = y - cy;
    if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) return 0.5;
    double a = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;   // 0 a droite, sens horaire (y vers le bas)
    double rel = std::fmod(a - kKnobStartDeg + 720.0, 360.0);
    if (rel <= kKnobSweepDeg) return rel / kKnobSweepDeg;
    return rel > kKnobSweepDeg + (360.0 - kKnobSweepDeg) / 2 ? 0.0 : 1.0;   // le creux du bas : le bout le plus proche
}

// ================================================================ selecteur ===
SelectorLayout selectorLayout(const Object& o, double w, double h, std::size_t count) {
    SelectorLayout l;
    l.rotary = o.text("style") != "boutons";
    const double fs = fontSizeOf(o, 14);
    const std::size_t n = std::max<std::size_t>(1, count);
    if (!l.rotary) {
        const double gap = 4;
        const double sw = (w - gap * static_cast<double>(n - 1)) / static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) l.labels.push_back({static_cast<double>(i) * (sw + gap), 0, std::max(1.0, sw), h});
        return l;
    }
    // Le bouton en bas, les libelles au-dessus : la place du repere (fs * 0.45),
    // d'un jour (4) et d'une ligne de texte (fs * 1.7) au-dessus du bouton.
    const double bh = fs * 1.7, tick = fs * 0.45, gap = 4;
    l.radius = std::max(8.0, std::min(w * 0.2, (h - 8 - tick - gap - bh) * 0.5));
    l.cx = w / 2;
    l.cy = h - l.radius - 4;
    const double span = n <= 1 ? 0.0 : std::min(170.0, 55.0 * static_cast<double>(n - 1));
    const double maxW = std::clamp(w / static_cast<double>(std::max<std::size_t>(2, n)), 44.0, 120.0);
    const auto choices = choicesOf(o);
    for (std::size_t i = 0; i < n; ++i) {
        const double a = n <= 1 ? 270.0 : 270.0 - span / 2 + span * static_cast<double>(i) / static_cast<double>(n - 1);
        l.angles.push_back(a);
        const double r = a * 3.14159265358979323846 / 180.0;
        const double ca = std::cos(r), sa = std::sin(r);
        // Le libelle au bout de son repere, sans le toucher : sa boite est
        // poussee vers l'exterieur de sa demi-largeur (a gauche, a droite) ou de
        // sa demi-hauteur (en haut). Sa largeur : celle du texte, estimee.
        std::size_t chars = 0;
        if (i < choices.size())
            for (const unsigned char ch : choices[i].label) chars += (ch & 0xC0) != 0x80;
        const double tw = std::min(maxW, 0.56 * fs * static_cast<double>(std::max<std::size_t>(chars, 2)) + 8);
        const double tx = l.cx + ca * (l.radius + tick), ty = l.cy + sa * (l.radius + tick);
        const double d = std::fabs(ca) * tw / 2 + std::fabs(sa) * bh / 2 + gap;
        const double px = tx + ca * d, py = ty + sa * d;
        Box b{px - tw / 2, py - bh / 2, tw, bh};
        b.x = std::clamp(b.x, 0.0, std::max(0.0, w - tw));
        b.y = std::clamp(b.y, 0.0, std::max(0.0, h - bh));
        l.labels.push_back(b);
    }
    return l;
}

std::string selectorHit(const Object& o, double w, double h, double x, double y, std::size_t count) {
    const auto l = selectorLayout(o, w, h, count);
    for (std::size_t i = 0; i < l.labels.size() && i < count; ++i)
        if (l.labels[i].contains(x, y)) return "position:" + std::to_string(i);
    if (l.rotary && std::hypot(x - l.cx, y - l.cy) <= l.radius * 1.05) return "suivant";
    return {};
}

// ============================================================ boutons radio ===
std::vector<Box> radioBoxes(const Object& o, double w, double h, std::size_t count) {
    std::vector<Box> out;
    const std::size_t n = std::max<std::size_t>(1, count);
    const bool horizontal = o.text("orientation") == "horizontale";
    for (std::size_t i = 0; i < n; ++i) {
        if (horizontal) out.push_back({w * static_cast<double>(i) / static_cast<double>(n), 0, w / static_cast<double>(n), h});
        else out.push_back({0, h * static_cast<double>(i) / static_cast<double>(n), w, h / static_cast<double>(n)});
    }
    return out;
}

std::string radioHit(const Object& o, double w, double h, double x, double y, std::size_t count) {
    const auto boxes = radioBoxes(o, w, h, count);
    for (std::size_t i = 0; i < boxes.size() && i < count; ++i)
        if (boxes[i].contains(x, y)) return "option:" + std::to_string(i);
    return {};
}

// ========================================================= liste deroulante ===
ComboLayout comboLayout(const Object& o, double w, double h, std::size_t count, std::size_t first) {
    ComboLayout l;
    l.rowH = std::max(22.0, fontSizeOf(o, 16) * 1.75);
    const auto maxVisible = static_cast<std::size_t>(std::clamp(o.number("maxVisible", 6), 1.0, 40.0));
    const std::size_t shown = std::min(count, maxVisible);
    const bool scroll = count > maxVisible;
    const double band = scroll ? 16.0 : 0.0;
    l.first = count > shown ? std::min(first, count - shown) : 0;
    l.list = {0, h + 2, w, band * 2 + l.rowH * static_cast<double>(std::max<std::size_t>(1, shown))};
    if (scroll) {
        l.up = {0, l.list.y, w, band};
        l.down = {0, l.list.bottom() - band, w, band};
    }
    for (std::size_t i = 0; i < shown; ++i) l.rows.push_back({0, l.list.y + band + l.rowH * static_cast<double>(i), w, l.rowH});
    return l;
}

ComboLayout listLayout(const Object& o, double w, double h, std::size_t count, std::size_t first) {
    ComboLayout l;
    l.rowH = std::max(18.0, fontSizeOf(o, 14) * 1.7);
    l.list = {0, 0, w, h};
    const auto fits = static_cast<std::size_t>(std::max(1.0, std::floor(h / l.rowH)));
    const bool scroll = count > fits;
    const double band = scroll ? 14.0 : 0.0;
    const auto shown = scroll ? static_cast<std::size_t>(std::max(1.0, std::floor((h - band * 2) / l.rowH))) : count;
    l.first = count > shown ? std::min(first, count - shown) : 0;
    if (scroll) {
        l.up = {0, 0, w, band};
        l.down = {0, h - band, w, band};
    }
    for (std::size_t i = 0; i < shown && l.first + i < count; ++i) l.rows.push_back({0, band + l.rowH * static_cast<double>(i), w, l.rowH});
    return l;
}

std::string listHit(const Object& o, double w, double h, double x, double y, std::size_t count, std::size_t first) {
    const auto l = listLayout(o, w, h, count, first);
    if (l.up.w > 0 && l.up.contains(x, y)) return "defiler:-1";
    if (l.down.w > 0 && l.down.contains(x, y)) return "defiler:1";
    for (std::size_t i = 0; i < l.rows.size(); ++i)
        if (l.rows[i].contains(x, y)) return "choix:" + std::to_string(l.first + i);
    return {};
}

TableLayout tableLayout(const Object& o, double w, double h, std::size_t count, std::size_t first) {
    TableLayout l;
    // Comme les cases d'un tableau (lot 6) : la taille du texte donne la hauteur des lignes.
    const double fs = std::clamp(o.number("fontSize", 14), 8.0, 40.0);
    l.rowH = std::max(22.0, fs + 12);
    l.headerH = l.rowH + 4;
    l.fit = static_cast<std::size_t>(std::max(0.0, std::floor((h - l.headerH) / l.rowH)));
    l.first = count > l.fit ? std::min(first, count - l.fit) : 0;
    if (count > l.fit && l.fit > 0) {
        const double bw = 12;
        l.bar = {w - bw, l.headerH, bw, std::max(0.0, h - l.headerH)};
        const double part = static_cast<double>(l.fit) / static_cast<double>(count);
        const double th = std::max(16.0, l.bar.h * part);
        const double room = std::max(0.0, l.bar.h - th);
        const double at = count > l.fit ? static_cast<double>(l.first) / static_cast<double>(count - l.fit) : 0.0;
        l.thumb = {l.bar.x + 2, l.bar.y + room * at, bw - 4, th};
    }
    return l;
}

std::string tableHit(const Object& o, double w, double h, double x, double y, std::size_t count, std::size_t first) {
    const auto l = tableLayout(o, w, h, count, first);
    if (l.bar.w <= 0 || !l.bar.contains(x, y)) return {};
    const std::string page = std::to_string(std::max<std::size_t>(1, l.fit));
    if (y < l.thumb.y) return "defiler:-" + page;
    if (y > l.thumb.bottom()) return "defiler:" + page;
    return {};
}

std::string comboHit(const Object& o, double w, double h, double x, double y, std::size_t count, bool open, std::size_t first) {
    if (open) {
        const auto l = comboLayout(o, w, h, count, first);
        if (l.up.w > 0 && l.up.contains(x, y)) return "defiler:-1";
        if (l.down.w > 0 && l.down.contains(x, y)) return "defiler:1";
        for (std::size_t i = 0; i < l.rows.size(); ++i)
            if (l.rows[i].contains(x, y)) return "choix:" + std::to_string(l.first + i);
    }
    if (x >= 0 && y >= 0 && x <= w && y <= h) return "ouvrir";
    return {};
}

// =========================================================== date et heure ===
int daysInMonth(int year, int month) {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

DateTime dateTimeFromEpoch(double seconds) {
    const auto t = static_cast<std::time_t>(std::floor(seconds));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

int weekdayFromEpoch(double seconds) {
    const auto t = static_cast<std::time_t>(std::floor(seconds));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return (tm.tm_wday + 6) % 7;
}

double epochFromDateTime(const DateTime& d) {
    std::tm tm{};
    tm.tm_year = d.year - 1900;
    tm.tm_mon = d.month - 1;
    tm.tm_mday = d.day;
    tm.tm_hour = d.hour;
    tm.tm_min = d.minute;
    tm.tm_sec = d.second;
    tm.tm_isdst = -1;
    return static_cast<double>(std::mktime(&tm));
}

DateTime stepDateTime(DateTime d, std::string_view field, int delta) {
    const auto wrap = [](int v, int lo, int hi) {
        const int n = hi - lo + 1;
        return lo + (((v - lo) % n) + n) % n;
    };
    if (field == "jour") d.day = wrap(d.day + delta, 1, daysInMonth(d.year, d.month));
    else if (field == "mois") d.month = wrap(d.month + delta, 1, 12);
    else if (field == "annee") d.year = std::clamp(d.year + delta, 1970, 2099);
    else if (field == "heure") d.hour = wrap(d.hour + delta, 0, 23);
    else if (field == "minute") d.minute = wrap(d.minute + delta, 0, 59);
    else if (field == "seconde") d.second = wrap(d.second + delta, 0, 59);
    d.day = std::clamp(d.day, 1, daysInMonth(d.year, d.month));
    return d;
}

std::string isoDateTime(const DateTime& d, std::string_view fields, bool seconds) {
    char date[32], time[32];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d", d.year, d.month, d.day);
    if (seconds) std::snprintf(time, sizeof time, "%02d:%02d:%02d", d.hour, d.minute, d.second);
    else std::snprintf(time, sizeof time, "%02d:%02d", d.hour, d.minute);
    if (fields == "date") return date;
    if (fields == "heure") return time;
    return std::string(date) + " " + time;
}

std::string frenchDateTime(const DateTime& d, std::string_view fields, bool seconds) {
    char date[32], time[32];
    std::snprintf(date, sizeof date, "%02d/%02d/%04d", d.day, d.month, d.year);
    if (seconds) std::snprintf(time, sizeof time, "%02d:%02d:%02d", d.hour, d.minute, d.second);
    else std::snprintf(time, sizeof time, "%02d:%02d", d.hour, d.minute);
    if (fields == "date") return date;
    if (fields == "heure") return time;
    return std::string(date) + " " + time;
}

bool parseDateTime(std::string_view text, DateTime& inOut) {
    DateTime d = inOut;
    std::string t = trim(text);
    for (auto& c : t) if (c == 'T') c = ' ';
    bool any = false;
    std::size_t from = 0;
    while (from < t.size()) {
        while (from < t.size() && t[from] == ' ') ++from;
        if (from >= t.size()) break;
        auto end = t.find(' ', from);
        if (end == std::string::npos) end = t.size();
        const std::string tok = t.substr(from, end - from);
        from = end;
        int a = 0, b = 0, c = 0;
        char s1 = 0, s2 = 0;
        const int n = std::sscanf(tok.c_str(), "%d%c%d%c%d", &a, &s1, &b, &s2, &c);
        if (n == 5 && s1 == '-' && s2 == '-') {
            if (b < 1 || b > 12 || c < 1 || c > daysInMonth(a, b) || a < 1970 || a > 2099) return false;
            d.year = a; d.month = b; d.day = c;
        } else if (n == 5 && s1 == '/' && s2 == '/') {
            if (b < 1 || b > 12 || c < 1970 || c > 2099 || a < 1 || a > daysInMonth(c, b)) return false;
            d.day = a; d.month = b; d.year = c;
        } else if (n >= 3 && s1 == ':') {
            if (a < 0 || a > 23 || b < 0 || b > 59) return false;
            d.hour = a; d.minute = b;
            d.second = 0;
            if (n == 5 && s2 == ':') {
                if (c < 0 || c > 59) return false;
                d.second = c;
            } else if (n > 3) {
                return false;
            }
        } else {
            return false;
        }
        any = true;
    }
    if (!any) return false;
    inOut = d;
    return true;
}

PickerLayout pickerLayout(const Object& o, double w, double h) {
    return pickerLayout(o.text("fields", "date et heure"), o.flag("seconds", false), w, h);
}

PickerLayout pickerLayout(std::string_view fieldsWanted, bool seconds, double w, double h) {
    PickerLayout l;
    const std::string fields(fieldsWanted);
    std::vector<std::pair<std::string, double>> spec;    // nom, largeur en chiffres
    if (fields != "heure") spec.insert(spec.end(), {{"jour", 2}, {"mois", 2}, {"annee", 4}});
    if (fields != "date") {
        spec.insert(spec.end(), {{"heure", 2}, {"minute", 2}});
        if (seconds) spec.push_back({"seconde", 2});
    }
    const double buttonsW = std::clamp(w * 0.26, 64.0, 130.0);
    const double area = std::max(20.0, w - buttonsW - 10);
    double units = 0;
    for (const auto& s : spec) units += s.second + 1.0;
    units += static_cast<double>(spec.size() > 0 ? spec.size() - 1 : 0) * 0.6;   // les separateurs
    if (fields == "date et heure") units += 0.8;                                  // l'espace entre la date et l'heure
    const double u = area / std::max(1.0, units);
    const double ah = std::clamp(h * 0.24, 12.0, 30.0);
    const double fh = std::max(12.0, h - 2 * ah - 8);
    double x = 4;
    for (std::size_t i = 0; i < spec.size(); ++i) {
        if (i > 0) x += 0.6 * u + ((spec[i].first == "heure" && fields == "date et heure") ? 0.8 * u : 0.0);
        const double fw = (spec[i].second + 1.0) * u;
        PickerLayout::Field f;
        f.name = spec[i].first;
        f.up = {x, 4, fw, ah};
        f.box = {x, 4 + ah, fw, fh};
        f.down = {x, 4 + ah + fh, fw, ah};
        l.fields.push_back(f);
        x += fw;
    }
    const double bx = w - buttonsW - 4;
    const double bh = (h - 12) / 2;
    l.now = {bx, 4, buttonsW, bh};
    l.ok = {bx, 8 + bh, buttonsW, bh};
    return l;
}

std::string pickerHit(const Object& o, double w, double h, double x, double y) {
    const auto l = pickerLayout(o, w, h);
    for (const auto& f : l.fields) {
        if (f.up.contains(x, y)) return "plus:" + f.name;
        if (f.down.contains(x, y)) return "moins:" + f.name;
    }
    if (l.now.contains(x, y)) return "maintenant";
    if (l.ok.contains(x, y)) return "valider";
    return {};
}

// ==================================================== programmateur horaire ===
int resolutionMinutes(std::string_view resolution) {
    const std::string r = lower(trim(resolution));
    if (r.rfind("1 h", 0) == 0 || r == "60" || r == "60 min" || r == "1h") return 60;
    if (r.rfind("15", 0) == 0) return 15;
    return 30;
}

WeekSchedule emptySchedule(int slotMinutes) {
    WeekSchedule s;
    s.slotMinutes = slotMinutes == 60 || slotMinutes == 15 ? slotMinutes : 30;
    for (auto& d : s.days) d.assign(static_cast<std::size_t>(s.slots()), false);
    return s;
}

namespace {
int dayIndex(std::string_view name) {
    const std::string n = lower(trim(name));
    static constexpr std::string_view kLong[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
    for (int i = 0; i < 7; ++i)
        if (n == lower(std::string(kDayNames[i])) || n == kLong[i]) return i;
    return -1;
}
// "07:00", "7:30", "7h30", "7", "24:00" -> minutes ; -1 : illisible.
int minutesOf(std::string_view text) {
    std::string t = trim(text);
    for (auto& c : t) if (c == 'h' || c == 'H') c = ':';
    int h = 0, m = 0;
    char colon = 0;
    const int n = std::sscanf(t.c_str(), "%d%c%d", &h, &colon, &m);
    if (n == 1) m = 0;
    else if (n == 2 && colon == ':') m = 0;
    else if (n != 3 || colon != ':') return -1;
    if (h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) return -1;
    return h * 60 + m;
}
std::string hhmm(int minutes) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", minutes / 60, minutes % 60);
    return b;
}
std::string dayRanges(const std::vector<bool>& slots, int res) {
    std::string out;
    std::size_t i = 0;
    while (i < slots.size()) {
        if (!slots[i]) { ++i; continue; }
        std::size_t j = i;
        while (j < slots.size() && slots[j]) ++j;
        if (!out.empty()) out += ",";
        out += hhmm(static_cast<int>(i) * res) + "-" + hhmm(static_cast<int>(j) * res);
        i = j;
    }
    return out;
}
} // namespace

bool parseSchedule(std::string_view text, int slotMinutes, WeekSchedule& out, std::string* error) {
    WeekSchedule s = emptySchedule(slotMinutes);
    const int res = s.slotMinutes;
    const auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    for (const auto& entry : listItems(text)) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) return fail("plage sans \xC2\xAB = \xC2\xBB : " + entry + " (Lu-Ve=07:00-12:00)");
        const std::string daysText = trim(std::string_view(entry).substr(0, eq));
        std::vector<int> days;
        const std::string dl = lower(daysText);
        if (dl == "*" || dl == "tous" || dl == "tous les jours") {
            for (int d = 0; d < 7; ++d) days.push_back(d);
        } else {
            for (const auto& part : splitKeep(daysText, ',')) {
                const auto dash = part.find('-');
                if (dash != std::string::npos) {
                    const int a = dayIndex(part.substr(0, dash)), b = dayIndex(part.substr(dash + 1));
                    if (a < 0 || b < 0) return fail("jours inconnus : " + part + " (Lu Ma Me Je Ve Sa Di)");
                    for (int d = a;; d = (d + 1) % 7) {
                        days.push_back(d);
                        if (d == b) break;
                    }
                } else {
                    const int d = dayIndex(part);
                    if (d < 0) return fail("jour inconnu : " + part + " (Lu Ma Me Je Ve Sa Di)");
                    days.push_back(d);
                }
            }
        }
        for (const auto& range : listItems(std::string_view(entry).substr(eq + 1))) {
            for (const auto& r : splitKeep(range, ',')) {
                if (r.empty()) continue;
                const auto dash = r.find('-');
                const int a = dash == std::string::npos ? -1 : minutesOf(r.substr(0, dash));
                const int b = dash == std::string::npos ? -1 : minutesOf(r.substr(dash + 1));
                if (a < 0 || b < 0 || b <= a) return fail("plage illisible : " + r + " (07:00-12:00)");
                const int from = static_cast<int>(std::lround(static_cast<double>(a) / res));
                const int to = static_cast<int>(std::lround(static_cast<double>(b) / res));
                for (const int d : days)
                    for (int k = from; k < to && k < s.slots(); ++k) s.days[static_cast<std::size_t>(d)][static_cast<std::size_t>(k)] = true;
            }
        }
    }
    out = std::move(s);
    return true;
}

std::string formatSchedule(const WeekSchedule& s) {
    std::string out;
    int d = 0;
    while (d < 7) {
        const std::string ranges = dayRanges(s.days[static_cast<std::size_t>(d)], s.slotMinutes);
        if (ranges.empty()) { ++d; continue; }
        int e = d;
        while (e + 1 < 7 && dayRanges(s.days[static_cast<std::size_t>(e + 1)], s.slotMinutes) == ranges) ++e;
        if (!out.empty()) out += "; ";
        out += std::string(kDayNames[d]);
        if (e > d) out += "-" + std::string(kDayNames[e]);
        out += "=" + ranges;
        d = e + 1;
    }
    return out;
}

bool scheduleOn(const WeekSchedule& s, int weekday, int minuteOfDay) {
    if (weekday < 0 || weekday > 6 || minuteOfDay < 0 || minuteOfDay >= 24 * 60) return false;
    const auto& day = s.days[static_cast<std::size_t>(weekday)];
    const auto slot = static_cast<std::size_t>(minuteOfDay / s.slotMinutes);
    return slot < day.size() && day[slot];
}

ScheduleLayout scheduleLayout(const Object& o, double w, double h, int slotMinutes) {
    ScheduleLayout l;
    const double fs = fontSizeOf(o, 12);
    l.slots = 24 * 60 / (slotMinutes == 60 || slotMinutes == 15 ? slotMinutes : 30);
    l.labelW = std::max(26.0, fs * 2.8);
    l.headerH = std::max(16.0, fs * 1.8);
    l.grid = {l.labelW, l.headerH, std::max(10.0, w - l.labelW - 4), std::max(7.0, h - l.headerH - 4)};
    l.cellW = l.grid.w / l.slots;
    l.cellH = l.grid.h / 7;
    return l;
}

std::string scheduleHit(const Object& o, double w, double h, double x, double y, int slotMinutes) {
    const auto l = scheduleLayout(o, w, h, slotMinutes);
    const bool inX = x >= l.grid.x && x < l.grid.right();
    const bool inY = y >= l.grid.y && y < l.grid.bottom();
    const int slot = inX ? std::clamp(static_cast<int>((x - l.grid.x) / l.cellW), 0, l.slots - 1) : -1;
    const int day = inY ? std::clamp(static_cast<int>((y - l.grid.y) / l.cellH), 0, 6) : -1;
    if (inX && inY) return "case:" + std::to_string(day) + "," + std::to_string(slot);
    if (inY && x >= 0 && x < l.grid.x) return "jour:" + std::to_string(day);
    if (inX && y >= 0 && y < l.grid.y) return "heure:" + std::to_string(slot);
    return {};
}

// ============================================================== 7 segments ===
std::uint8_t segmentsOf(char c) {
    switch (c) {
        case '0': return 0x3F; case '1': return 0x06; case '2': return 0x5B; case '3': return 0x4F;
        case '4': return 0x66; case '5': return 0x6D; case '6': return 0x7D; case '7': return 0x07;
        case '8': return 0x7F; case '9': return 0x6F; case '-': return 0x40; case '_': return 0x08;
        case 'A': return 0x77; case 'b': return 0x7C; case 'C': return 0x39; case 'c': return 0x58;
        case 'd': return 0x5E; case 'E': return 0x79; case 'F': return 0x71; case 'H': return 0x76;
        case 'h': return 0x74; case 'L': return 0x38; case 'n': return 0x54; case 'o': return 0x5C;
        case 'P': return 0x73; case 'r': return 0x50; case 't': return 0x78; case 'U': return 0x3E;
        case 'u': return 0x1C; case 'y': return 0x6E;
        default: return 0;
    }
}

std::vector<SegmentDigit> sevenSegmentDigits(double value, int digits, int decimals, bool leadingZeros, bool valid) {
    digits = std::clamp(digits, 1, 16);
    decimals = std::clamp(decimals, 0, digits - 1);
    std::vector<SegmentDigit> out(static_cast<std::size_t>(digits));
    const auto dashes = [&] {
        for (auto& d : out) d = {'-', false};
        return out;
    };
    if (!valid || !std::isfinite(value)) return dashes();
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, std::fabs(value));
    std::string num = buf;                                  // "12.5"
    const bool negative = value < 0 && std::fabs(value) >= 0.5 * std::pow(10.0, -decimals);
    std::string plain;
    for (const char c : num) if (c != '.') plain += c;      // "125"
    const std::size_t needed = plain.size() + (negative ? 1 : 0);
    if (needed > out.size()) return dashes();
    // Les chiffres, a droite ; le point sur le chiffre des unites.
    const std::size_t start = out.size() - plain.size();
    for (std::size_t k = 0; k < plain.size(); ++k) out[start + k].c = plain[k];
    if (decimals > 0) out[out.size() - 1 - static_cast<std::size_t>(decimals)].dot = true;
    if (leadingZeros) {
        for (std::size_t k = negative ? 1 : 0; k < start; ++k) out[k].c = '0';
        if (negative) out[0].c = '-';
    } else if (negative) {
        out[start - 1].c = '-';
    }
    return out;
}

// ========================================================== compteur horaire ===
std::string formatRunTime(double seconds, std::string_view format) {
    const double s = std::max(0.0, seconds);
    char b[64];
    if (format == "heures") {
        std::snprintf(b, sizeof b, "%.2f h", s / 3600.0);
        return b;
    }
    const auto total = static_cast<long long>(std::floor(s));
    if (format == "jours") {
        std::snprintf(b, sizeof b, "%lld j %02lld h %02lld min", total / 86400, (total / 3600) % 24, (total / 60) % 60);
        return b;
    }
    std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    return b;
}

} // namespace hmi
