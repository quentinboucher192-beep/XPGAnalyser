#include "HmiWidgets.hpp"
#include "HmiRoles.hpp"   // 1.12.6 (lot C) : le decoupeur unique des listes

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> splitOn(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

} // namespace

// ---------------------------------------------------------------- image animee
std::vector<ImageState> parseImageStates(std::string_view text) {
    std::vector<ImageState> out;
    for (const auto& raw : splitOn(text, '\n')) {
        const auto line = trim(raw);
        if (line.empty()) continue;
        ImageState st;
        std::string rest = line;
        if (const auto arrow = line.find("=>"); arrow != std::string::npos) {
            st.condition = trim(std::string_view(line).substr(0, arrow));
            rest = trim(std::string_view(line).substr(arrow + 2));
        }
        if (const auto at = rest.rfind('@'); at != std::string::npos) {
            st.periodMs = std::max(0, std::atoi(trim(std::string_view(rest).substr(at + 1)).c_str()));
            rest = trim(std::string_view(rest).substr(0, at));
        }
        for (const auto& img : splitOn(rest, ','))
            if (auto name = trim(img); !name.empty()) st.images.push_back(std::move(name));
        out.push_back(std::move(st));
    }
    return out;
}

std::string formatImageStates(const std::vector<ImageState>& states) {
    std::string out;
    for (const auto& st : states) {
        if (!out.empty()) out += '\n';
        out += (st.condition.empty() ? std::string("TRUE") : st.condition) + " => ";
        for (std::size_t i = 0; i < st.images.size(); ++i) out += (i ? ", " : "") + st.images[i];
        if (st.periodMs > 0) out += " @ " + std::to_string(st.periodMs);
    }
    return out;
}

std::string imageFrame(const ImageState& s, int objectPeriodMs, double seconds) {
    if (s.images.empty()) return {};
    if (s.images.size() == 1) return s.images.front();
    const int period = std::max(20, s.periodMs > 0 ? s.periodMs : objectPeriodMs > 0 ? objectPeriodMs : 500);
    const auto step = static_cast<long long>(std::floor(std::max(0.0, seconds) * 1000.0 / period));
    return s.images[static_cast<std::size_t>(step % static_cast<long long>(s.images.size()))];
}

// ------------------------------------------------------------------- tableau
std::vector<std::vector<std::string>> parseCells(std::string_view text) {
    std::vector<std::vector<std::string>> out;
    if (text.empty()) return out;
    for (const auto& line : splitOn(text, '\n')) out.push_back(splitOn(line, '\t'));
    return out;
}

std::string formatCells(const std::vector<std::vector<std::string>>& rows) {
    std::string out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r) out += '\n';
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            if (c) out += '\t';
            std::string cell = rows[r][c];
            std::replace(cell.begin(), cell.end(), '\t', ' ');
            std::replace(cell.begin(), cell.end(), '\n', ' ');
            out += cell;
        }
    }
    return out;
}

bool cellIsExpression(std::string_view cell) noexcept { return !cell.empty() && cell.front() == '='; }
bool cellIsTemplate(std::string_view cell) noexcept {
    const auto open = cell.find('{');
    return open != std::string_view::npos && cell.find('}', open) != std::string_view::npos;
}

std::vector<double> columnFractions(const Object& o, std::size_t n) {
    std::vector<double> w(n, 1.0);
    if (const auto* p = o.find("widths"); p && !p->value.empty()) {
        const auto parts = roles::splitList(p->value);   // 1.12.6 (lot C) : le decoupeur unique
        for (std::size_t i = 0; i < n && i < parts.size(); ++i) {
            double v = 0;
            if (parseNumber(trim(parts[i]), v) && v > 0) w[i] = v;
        }
    }
    double total = 0;
    for (const double x : w) total += x;
    for (auto& x : w) x = total > 0 ? x / total : 1.0 / static_cast<double>(std::max<std::size_t>(1, n));
    return w;
}

std::vector<std::string> splitSemicolons(std::string_view s) {
    // 1.12.6 (lot C) : le decoupeur unique (HmiRoles) - une chaine 'a;b', une cle M['a;b'], un repere
    // $A;B$ restent entiers (avant : coupes en deux, les noms et couleurs decales d'un cran).
    return roles::splitList(s, ';');
}

std::string joinSemicolons(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ";" : "") + v[i];
    return out;
}

// ------------------------------------------------------ gestionnaire de recettes
RecipeManagerLayout recipeManagerLayout(const Object& o, double w, double h) {
    RecipeManagerLayout l;
    const double bar = 30, pad = 4;
    double x = pad;
    std::vector<std::string> labels;
    if (const auto* p = o.find("buttons")) labels = splitSemicolons(p->value);
    else labels.assign(std::begin(kRecipeButtons), std::end(kRecipeButtons));
    const double fontSize = std::clamp(o.number("fontSize", 13), 8.0, 32.0);
    for (const auto& label : labels) {
        if (label.empty()) continue;
        // Une largeur d'apres le texte (sans police : 0,58 em par caractere, en
        // comptant les caracteres et non les octets).
        std::size_t chars = 0;
        for (const char c : label) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
        const double bw = std::max(56.0, static_cast<double>(chars) * fontSize * 0.58 + 18);
        if (x + bw > w - pad) break;
        l.buttons.push_back({label, Box{x, pad, bw, bar - 2 * pad}});
        x += bw + pad;
    }
    l.title = Box{x + pad, 0, std::max(0.0, w - x - 2 * pad), bar};
    l.table = Box{0, bar, w, std::max(0.0, h - bar)};
    l.rowH = std::max(18.0, fontSize + 9);
    l.headerH = l.rowH + 2;
    l.visibleRows = l.table.h > l.headerH ? static_cast<std::size_t>((l.table.h - l.headerH) / l.rowH) : 0;
    return l;
}

std::string recipeManagerHit(const Object& o, double w, double h, double x, double y, std::size_t records) {
    const auto l = recipeManagerLayout(o, w, h);
    for (const auto& b : l.buttons)
        if (b.box.contains(x, y)) return "bouton:" + b.label;
    if (y >= l.table.y + l.headerH && y < l.table.bottom() && x >= 0 && x <= w) {
        const auto row = static_cast<std::size_t>((y - l.table.y - l.headerH) / l.rowH);
        if (row < records && row < l.visibleRows) return "ligne:" + std::to_string(row);
    }
    return {};
}

std::string progressLabel(double value, double min, double max, bool percentSign) {
    const double f = max > min ? std::clamp((value - min) / (max - min), 0.0, 1.0) : 0.0;
    return formatNumber(std::round(f * 100.0)) + (percentSign ? " %" : "");
}

// ------------------------------------------------- 1.14.1 : les fins de course
namespace limits {

namespace {
const std::string kFco = "FC ouvert", kFcf = "FC ferm\xC3\xA9";
const std::string kManoeuvre = "le temps de man\xC5\x93uvre";
std::string portName(int way) { return way == 0 ? std::string("FC ferm\xC3\xA9" "e") : "FC voie " + std::string(wayLabel(way)); }
} // namespace

bool kindHasLimits(Kind k) noexcept {
    return k == Kind::Valve || k == Kind::ThreeWayValve || k == Kind::Damper || k == Kind::MultiBladeDamper || k == Kind::FireDamper;
}

bool limitModeText(Kind k, std::string_view modeText) noexcept {
    if (!kindHasLimits(k)) return false;
    std::size_t a = 0;
    while (a < modeText.size() && (modeText[a] == ' ' || modeText[a] == '\t')) ++a;
    return modeText.substr(a).rfind("fins", 0) == 0;
}

bool limitMode(const Object& o) noexcept {
    const Prop* p = o.find(o.kind == Kind::ThreeWayValve ? std::string_view("positionMode") : kModeKey);
    return p && limitModeText(o.kind, p->value);
}

std::string stateLabel(State s, bool feminine) {
    switch (s) {
        case State::Open: return feminine ? "ouverte" : "ouvert";
        case State::Closed: return feminine ? "ferm\xC3\xA9" "e" : "ferm\xC3\xA9";
        case State::Moving: return "en mouvement";
        case State::Discord: return "discordance";
        case State::None: break;
    }
    return "sans fin de course reli\xC3\xA9" "e";
}

Reading decide(Input fo, Input ff, Input order, bool overdue, bool regulating) {
    Reading r;
    if (!fo.given && !ff.given) {
        r.why = "aucune fin de course reli\xC3\xA9" "e (FC ouvert, FC ferm\xC3\xA9)";
        return r;
    }
    // Une seule reliee : l'autre position s'en deduit (FC ouvert fausse : ferme).
    const bool o = fo.given ? fo.on : !ff.on;
    const bool f = ff.given ? ff.on : !fo.on;
    const std::string alone = fo.given && ff.given ? std::string(" seule vraie") : std::string(" (seule reli\xC3\xA9" "e)");
    if (order.given) {
        r.order = order.on ? 1 : 0;
        r.reached = order.on ? o && !f : f && !o;
    }
    if (o && f) {
        r.state = State::Discord;
        r.both = true;
        r.why = kFco + " et " + kFcf + " vraies \xC3\xA0 la fois";
        return r;
    }
    if (order.given) {
        const std::string what = order.on ? "ordre d'ouverture" : "ordre de fermeture";
        const std::string& wanted = order.on ? kFco : kFcf;
        if (r.reached) {
            r.state = order.on ? State::Open : State::Closed;
            r.why = what + " donn\xC3\xA9 et " + wanted + " atteinte";
        } else if (overdue) {
            r.state = State::Discord;
            r.overdue = true;
            r.why = what + " donn\xC3\xA9, " + wanted + " pas atteinte apr\xC3\xA8s " + kManoeuvre;
        } else {
            r.state = State::Moving;
            r.toward = order.on ? 1 : -1;
            r.why = what + " donn\xC3\xA9, " + wanted + " pas encore atteinte : " + (order.on ? "en ouverture" : "en fermeture");
        }
        return r;
    }
    if (o) {
        r.state = State::Open;
        r.why = fo.given ? kFco + alone : kFcf + " fausse (seule reli\xC3\xA9" "e)";
    } else if (f) {
        r.state = State::Closed;
        r.why = ff.given ? kFcf + alone : kFco + " fausse (seule reli\xC3\xA9" "e)";
    } else if (regulating) {
        r.state = State::Open;
        r.between = true;
        r.why = "ni " + kFco + " ni " + kFcf + " : entre les deux, \xC3\xA0 son ouverture";
    } else {
        r.state = State::Moving;
        r.why = "ni " + kFco + " ni " + kFcf + " : en mouvement";
    }
    return r;
}

Reading3 decide3(const Input (&ports)[4], bool lBore, int order, bool overdue) {
    Reading3 r;
    bool any = false;
    for (int w = 0; w < 4; ++w) {
        if ((lBore && w == 1) || !ports[w].given) continue;
        any = true;
        if (ports[w].on) r.on.push_back(w);
    }
    if (!any) {
        r.why = "aucune fin de course reli\xC3\xA9" "e";
        return r;
    }
    if (order > 3 || (lBore && order == 1)) order = -1;   // une voie qui n'existe pas : pas d'ordre
    if (order >= 0) {
        r.order = order;
        r.reached = order == 0 ? (ports[0].given ? r.on.size() == 1 && r.on[0] == 0 : r.on.empty())
                               : r.on.size() == 1 && r.on[0] == order;
    }
    if (r.on.size() >= 2) {
        r.state = State::Discord;
        r.way = order;
        for (std::size_t i = 0; i < r.on.size(); ++i) r.why += (i == 0 ? "" : i + 1 == r.on.size() ? " et " : ", ") + portName(r.on[i]);
        r.why += " vraies \xC3\xA0 la fois";
        return r;
    }
    if (order >= 0) {
        const std::string what = "ordre " + std::string(order == 0 ? "de fermeture" : "de position " + std::string(wayLabel(order)));
        const std::string wanted = order == 0 && !ports[0].given ? std::string("aucune voie") : portName(order);
        r.way = order;
        if (r.reached) {
            r.state = order == 0 ? State::Closed : State::Open;
            r.why = what + " donn\xC3\xA9 et " + wanted + (order == 0 && !ports[0].given ? " vraie" : " atteinte");
        } else if (overdue) {
            r.state = State::Discord;
            r.overdue = true;
            r.why = what + " donn\xC3\xA9, " + wanted + " pas atteinte apr\xC3\xA8s " + kManoeuvre;
        } else {
            r.state = State::Moving;
            r.why = what + " donn\xC3\xA9, " + wanted + " pas encore atteinte : le boisseau tourne";
        }
        return r;
    }
    if (r.on.size() == 1) {
        r.way = r.on[0];
        r.state = r.way == 0 ? State::Closed : State::Open;
        r.why = portName(r.way) + " seule vraie";
    } else {
        r.state = State::Moving;
        r.why = "aucune fin de course vraie : le boisseau tourne";
    }
    return r;
}

int wayOf(std::string_view raw) {
    const std::string t = trim(raw);
    if (t.empty()) return -1;
    double n = 0;
    if (parseNumber(t, n)) {
        const long v = std::lround(n);
        if (v == 0) return 0;
        if (v == 1 || v == 12 || v == 21) return 1;
        if (v == 2 || v == 13 || v == 31) return 2;
        if (v == 3 || v == 23 || v == 32) return 3;
        return -1;
    }
    if (t.find("1-2") != std::string::npos || t.find("2-1") != std::string::npos) return 1;
    if (t.find("1-3") != std::string::npos || t.find("3-1") != std::string::npos) return 2;
    if (t.find("2-3") != std::string::npos || t.find("3-2") != std::string::npos) return 3;
    if (t.rfind("ferm", 0) == 0 || t.rfind("Ferm", 0) == 0) return 0;
    if (parseBool(t, false) != parseBool(t, true)) return -1;
    return parseBool(t, false) ? 1 : 0;
}

std::string_view wayLabel(int way) noexcept {
    switch (way) {
        case 0: return "ferm\xC3\xA9" "e";
        case 1: return "1-2";
        case 2: return "1-3";
        case 3: return "2-3";
        default: return "?";
    }
}

Input inputOf(const Object& o, std::string_view key) {
    Input in;
    const Prop* p = o.find(key);
    if (!p) return in;
    const std::string t = trim(p->value);
    in.given = !t.empty() || !trim(p->expr).empty();
    if (t.empty()) return in;
    double n = 0;
    in.on = parseNumber(t, n) ? n != 0 : parseBool(t, false);
    return in;
}

Reading read(const Object& o) {
    double opening = 0;
    const bool regulating = o.kind != Kind::FireDamper && parseNumber(trim(o.text("opening")), opening);
    return decide(inputOf(o, "limitOpen"), inputOf(o, "limitClosed"), inputOf(o, "openOrder"), inputOf(o, kOverdueKey).on, regulating);
}

Reading3 read3(const Object& o) {
    Input ports[4];
    for (int w = 0; w < 4; ++w) ports[w] = inputOf(o, kWayKeys[w]);
    const bool lBore = trim(o.text("valve3Bore", "T")) == "L";
    return decide3(ports, lBore, wayOf(o.text("positionOrder")), inputOf(o, kOverdueKey).on);
}

double travelSeconds(std::string_view text) {
    const std::string t = trim(text);
    std::int64_t ms = 0;
    if (t.empty() || !roles::parseDuration(t, ms, roles::Unit::Seconds) || ms <= 0) return 0.0;
    return static_cast<double>(ms) / 1000.0;
}

bool travelTimeOk(std::string_view text) {
    const std::string t = trim(text);
    std::int64_t ms = 0;
    return t.empty() || (roles::parseDuration(t, ms, roles::Unit::Seconds) && ms >= 0);
}

bool TravelClock::step(Track& t, int order, bool reached, double now, double travel) noexcept {
    if (order < 0 || travel <= 0) {
        t = Track{};
        return false;
    }
    // Le chrono repart quand l'Ordre change, quand la fin de course attendue se perd, ou quand l'heure
    // recule (un exemple qui recommence son tour).
    if (order != t.order || (t.reached && !reached) || now < t.since) t.since = now;
    t.order = order;
    t.reached = reached;
    t.overdue = !reached && now - t.since >= travel - 1e-9;
    return t.overdue;
}

void TravelClock::observe(View& shown, double now) {
    for (auto& o : shown.objects) {
        if (!kindHasLimits(o.kind) || !limitMode(o)) continue;
        int order = -1;
        bool reached = false;
        if (o.kind == Kind::ThreeWayValve) {
            const auto r = read3(o);
            order = r.order;
            reached = r.reached;
        } else {
            const auto r = read(o);
            order = r.order;
            reached = r.reached;
        }
        if (step(tracks_[o.id], order, reached, now, travelSeconds(o.text("travelTime")))) o.set(std::string(kOverdueKey), "TRUE");
    }
}

bool TravelClock::overdue(Id object) const noexcept {
    const auto it = tracks_.find(object);
    return it != tracks_.end() && it->second.overdue;
}

void TravelClock::mark(View& shown) const {
    for (auto& o : shown.objects)
        if (overdue(o.id) && kindHasLimits(o.kind) && limitMode(o)) o.set(std::string(kOverdueKey), "TRUE");
}

} // namespace limits

} // namespace hmi
