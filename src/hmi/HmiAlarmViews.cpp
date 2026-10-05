#include "HmiAlarmViews.hpp"
#include "HmiAlarmGroups.hpp"   // 1.10.2 (AL) : la zone d'un groupe d'alarmes
#include "HmiObjectAlarms.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace hmi {

namespace {

bool has(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

double stampToSeconds(std::string_view s) {
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != ' ' && s[10] != 'T') || s[13] != ':' || s[16] != ':') return -1;
    const auto num = [&](std::size_t at, std::size_t n) {
        int v = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const char ch = s[at + i];
            if (ch < '0' || ch > '9') return -1;
            v = v * 10 + (ch - '0');
        }
        return v;
    };
    int y = num(0, 4);
    const int m = num(5, 2), d = num(8, 2), hh = num(11, 2), mm = num(14, 2), ss = num(17, 2);
    if (y < 0 || m < 1 || m > 12 || d < 1 || hh < 0 || mm < 0 || ss < 0) return -1;
    // Les jours depuis 1970 (H. Hinnant).
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const double days = static_cast<double>(era) * 146097.0 + static_cast<double>(doe) - 719468.0;
    double frac = 0;
    if (s.size() > 20 && s[19] == '.') {
        double scale = 0.1;
        for (std::size_t i = 20; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, scale /= 10) frac += (s[i] - '0') * scale;
    }
    return days * 86400.0 + hh * 3600.0 + mm * 60.0 + ss + frac;
}

bool inGroup(std::string_view alarmGroup, std::string_view filter) noexcept {
    return alarmGroupMatches(alarmGroup, {}, {}, filter);
}
// 1.9 : le groupe declare, le groupe interne (une alarme d'objet), les symboles.
bool inGroup(const LiveAlarm& a, std::string_view filter) noexcept {
    return alarmGroupMatches(a.group, a.objectGroup, a.symbols, filter);
}
bool inGroup(const ShelvedAlarm& s, std::string_view filter) noexcept {
    return alarmGroupMatches(s.group, s.objectGroup, s.symbols, filter);
}
bool inGroup(const AlarmOccurrence& o, std::string_view filter) noexcept {
    // Une alarme d'objet terminee : son groupe interne se lit dans son nom
    // ("Vue.Pompe_3.Defaut" -> "Vue.Pompe_3") ; les noms du projet n'ont pas de point.
    const std::size_t dot = o.name.rfind('.');
    const std::string_view objectGroup = dot == std::string::npos ? std::string_view{} : std::string_view(o.name).substr(0, dot);
    return alarmGroupMatches(o.group, objectGroup, {}, filter);
}

// ================================================================ bandeau ====
int bannerPick(const std::vector<LiveAlarm>& alarms, std::string_view group, std::string_view mode, double seconds, int periodMs,
               int step) {
    std::vector<int> shown;
    for (std::size_t i = 0; i < alarms.size(); ++i)
        if (inGroup(alarms[i], group)) shown.push_back(static_cast<int>(i));
    if (shown.empty()) return -1;
    const int n = static_cast<int>(shown.size());
    const auto wrap = [&](long long k) { return shown[static_cast<std::size_t>(((k % n) + n) % n)]; };
    if (has(mode, "filement")) {
        const double period = std::max(500, periodMs) / 1000.0;
        return wrap(static_cast<long long>(std::floor(std::max(0.0, seconds) / period)) + step);
    }
    if (has(mode, "cente")) {
        // La derniere apparue : l'horodatage le plus grand.
        int best = 0;
        for (int k = 1; k < n; ++k)
            if (alarms[static_cast<std::size_t>(shown[static_cast<std::size_t>(k)])].appeared
                > alarms[static_cast<std::size_t>(shown[static_cast<std::size_t>(best)])].appeared)
                best = k;
        return wrap(best + step);
    }
    // La plus grave : la premiere a acquitter (la liste est classee), sinon la premiere.
    int first = 0;
    for (int k = 0; k < n; ++k)
        if (!alarms[static_cast<std::size_t>(shown[static_cast<std::size_t>(k)])].acked) { first = k; break; }
    return wrap(first + step);
}

std::size_t bannerCount(const std::vector<LiveAlarm>& alarms, std::string_view group) {
    std::size_t n = 0;
    for (const auto& a : alarms) n += inGroup(a, group);
    return n;
}

BannerLayout bannerLayout(const Object& o, double w, double h) {
    BannerLayout l;
    l.fontSize = std::clamp(o.number("fontSize", 15), 8.0, std::max(8.0, h * 0.6));
    const double fs = l.fontSize;
    l.stripe = {0, 0, std::min(8.0, w * 0.05), h};
    double right = w - 4;
    if (o.flag("ackButton", true)) {
        const double bw = std::clamp(fs * 6.4, 60.0, std::max(60.0, w * 0.22));
        l.ack = {right - bw, std::max(3.0, h * 0.14), bw, std::max(10.0, h - 2 * std::max(3.0, h * 0.14))};
        right = l.ack.x - 6;
    }
    if (o.flag("showCount", true)) {
        const double cw = fs * 3.2;
        l.count = {right - cw, std::max(3.0, h * 0.14), cw, std::max(10.0, h - 2 * std::max(3.0, h * 0.14))};
        right = l.count.x - 6;
    }
    const double tx = l.stripe.w + 8;
    l.time = {tx, 0, fs * 5.0, h};
    l.message = {l.time.right() + 6, 0, std::max(0.0, right - (l.time.right() + 6)), h};
    return l;
}

std::string bannerHit(const Object& o, double w, double h, double x, double y) {
    const auto l = bannerLayout(o, w, h);
    if (l.ack.w > 0 && l.ack.contains(x, y)) return "acquitter";
    if (l.count.w > 0 && l.count.contains(x, y)) return "suivante";
    return {};
}

// ================================================================ compteur ===
std::size_t alarmCount(const std::vector<LiveAlarm>& alarms, const std::vector<ShelvedAlarm>& shelved, std::string_view group,
                       std::string_view count) {
    std::size_t n = 0;
    if (has(count, "mise")) {
        for (const auto& s : shelved) n += inGroup(s, group);
        return n;
    }
    const bool active = has(count, "activ"), all = has(count, "cours");
    for (const auto& a : alarms) {
        if (!inGroup(a, group)) continue;
        if (all) ++n;
        else if (active) n += a.active;
        else n += !a.acked;
    }
    return n;
}

int strongestPriority(const std::vector<LiveAlarm>& alarms, std::string_view group, bool unackedOnly) {
    int best = 0;
    for (const auto& a : alarms)
        if (inGroup(a, group) && (!unackedOnly || !a.acked) && (best == 0 || a.priority < best)) best = a.priority;
    return best;
}

// 1.11 (R111) : la plus forte : la priorite la plus forte, puis une a acquitter, puis la premiere.
namespace {
bool stronger(const LiveAlarm& a, const LiveAlarm* best) noexcept {
    return !best || a.priority < best->priority || (a.priority == best->priority && !a.acked && best->acked);
}
} // namespace

const LiveAlarm* strongestAlarm(const std::vector<LiveAlarm>& alarms, std::string_view group, bool unackedOnly) {
    const LiveAlarm* best = nullptr;
    for (const auto& a : alarms)
        if (inGroup(a, group) && (!unackedOnly || !a.acked) && stronger(a, best)) best = &a;
    return best;
}

const LiveAlarm* strongestZoneAlarm(const Project& p, const std::vector<LiveAlarm>& alarms, std::string_view zone) {
    const LiveAlarm* best = nullptr;
    for (const auto& a : alarms)
        if ((inGroup(a, zone) || (!a.group.empty() && alarmZoneOf(p, a.group) == zone)) && stronger(a, best)) best = &a;
    return best;
}

// ================================================================ resume =====
std::vector<std::string> zonesOf(const Project& p, std::string_view groups) {
    std::vector<std::string> out;
    for (const auto& g : splitSemicolons(groups)) {
        const std::string t = trimmed(g);
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    if (!out.empty()) return out;
    // 1.10.2 (AL) : la zone de chaque groupe d'alarmes (sa zone, sinon son nom) - un
    // projet sans groupe declare garde ses zones d'avant, les groupes de ses alarmes.
    for (const auto& g : alarmGroupsOf(p)) {
        const std::string z = alarmZoneOf(p, g.name);
        if (!z.empty() && std::find(out.begin(), out.end(), z) == out.end()) out.push_back(z);
    }
    return out;
}

std::vector<ZoneSummary> zoneSummaries(const Project& p, const std::vector<LiveAlarm>& alarms,
                                       const std::vector<ShelvedAlarm>& shelved, std::string_view groups) {
    std::vector<ZoneSummary> out;
    for (const auto& z : zonesOf(p, groups)) {
        ZoneSummary s;
        s.name = z;
        // 1.10.2 (AL) : une alarme compte aussi dans la zone de son groupe d'alarmes.
        const auto inZone = [&](const auto& a) { return inGroup(a, z) || (!a.group.empty() && alarmZoneOf(p, a.group) == z); };
        for (const auto& a : alarms) {
            if (!inZone(a)) continue;              // 1.9 : un groupe d'objet, un motif aussi
            ++s.current;
            s.active += a.active;
            s.unacked += !a.acked;
            if (s.highest == 0 || a.priority < s.highest) s.highest = a.priority;
        }
        for (const auto& sh : shelved) s.shelved += inZone(sh);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Box> summaryTiles(const Object& o, double w, double h, std::size_t zones) {
    std::vector<Box> out;
    if (zones == 0) return out;
    const auto perRow = static_cast<std::size_t>(std::clamp(o.number("perRow", 3), 1.0, 12.0));
    const std::size_t cols = std::min(perRow, zones);
    const std::size_t rows = (zones + cols - 1) / cols;
    const double gap = 8, pad = 8;
    const double tw = (w - 2 * pad - gap * static_cast<double>(cols - 1)) / static_cast<double>(cols);
    const double th = (h - 2 * pad - gap * static_cast<double>(rows - 1)) / static_cast<double>(rows);
    for (std::size_t i = 0; i < zones; ++i) {
        const std::size_t r = i / cols, c = i % cols;
        out.push_back({pad + static_cast<double>(c) * (tw + gap), pad + static_cast<double>(r) * (th + gap), std::max(0.0, tw),
                       std::max(0.0, th)});
    }
    return out;
}

std::string summaryHit(const Object& o, double w, double h, double x, double y, std::size_t zones) {
    const auto tiles = summaryTiles(o, w, h, zones);
    for (std::size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].contains(x, y)) return "zone:" + std::to_string(i);
    return {};
}

// ================================================================ listes ======
std::vector<std::size_t> historyAlarmRows(const std::vector<LiveAlarm>& alarms, std::string_view source, std::string_view group) {
    std::vector<std::size_t> out;
    const bool ackedOnly = source != "alarmes";
    for (std::size_t i = 0; i < alarms.size(); ++i)
        if (inGroup(alarms[i], group) && (!ackedOnly || alarms[i].acked)) out.push_back(i);
    return out;
}

std::vector<std::size_t> historyShelvedRows(const std::vector<ShelvedAlarm>& shelved, std::string_view group) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < shelved.size(); ++i)
        if (inGroup(shelved[i], group)) out.push_back(i);
    return out;
}

std::string historyHit(double w, double h, double x, double y, std::size_t rows) {
    if (x < 0 || x > w || y < kHistoryHeaderH || y > h) return {};
    const auto k = static_cast<std::size_t>((y - kHistoryHeaderH) / kHistoryRowH);
    const auto fit = static_cast<std::size_t>(std::max(0.0, (h - kHistoryHeaderH) / kHistoryRowH));
    if (k >= rows || k >= fit) return {};
    return "ligne:" + std::to_string(k);
}

// ================================================================ consigne ===
const LiveAlarm* instructionAlarm(const std::vector<LiveAlarm>& alarms, std::string_view alarmProp, std::string_view selected) {
    const std::string wanted = trimmed(alarmProp);
    if (!wanted.empty()) {
        for (const auto& a : alarms) if (a.name == wanted) return &a;
        return nullptr;
    }
    if (!selected.empty())
        for (const auto& a : alarms) if (a.name == selected) return &a;
    for (const auto& a : alarms) if (!a.acked) return &a;
    return alarms.empty() ? nullptr : &alarms.front();
}

// ================================================================ statistiques
std::vector<AlarmStatRow> alarmStatistics(const std::deque<AlarmOccurrence>& sinceStart, const std::vector<AlarmOccurrence>* kept,
                                          const std::vector<LiveAlarm>& live, std::string_view range, std::string_view group,
                                          std::string_view sort, std::size_t top, std::string_view nowStamp) {
    const double now = stampToSeconds(nowStamp);
    double from = -1e300;
    if (range == "24 h") from = now - 86400.0;
    else if (has(range, "7 jours")) from = now - 7 * 86400.0;
    std::map<std::string, AlarmStatRow> rows;
    std::vector<std::string> order;
    const auto row = [&](const std::string& name, const std::string& grp, int prio) -> AlarmStatRow& {
        auto it = rows.find(name);
        if (it == rows.end()) {
            AlarmStatRow r;
            r.name = name;
            r.group = grp;
            r.priority = prio;
            it = rows.emplace(name, std::move(r)).first;
            order.push_back(name);
        }
        return it->second;
    };
    const auto addClosed = [&](const AlarmOccurrence& a) {
        if (!inGroup(a, group)) return;
        const double t0 = stampToSeconds(a.appeared);
        if (t0 >= 0 && now >= 0 && t0 < from) return;
        auto& r = row(a.name, a.group, a.priority);
        ++r.count;
        const double t1 = stampToSeconds(a.cleared);
        if (t0 >= 0 && t1 >= t0) r.seconds += t1 - t0;
    };
    const bool whole = !has(range, "lancement");
    if (whole && kept) for (const auto& a : *kept) addClosed(a);
    else for (const auto& a : sinceStart) addClosed(a);
    for (const auto& a : live) {
        if (!inGroup(a, group)) continue;
        auto& r = row(a.name, a.group, a.priority);
        ++r.count;
        r.current = true;
        const double t0 = stampToSeconds(a.appeared);
        const double t1 = a.active ? now : stampToSeconds(a.cleared);
        if (t0 >= 0 && t1 >= t0) r.seconds += t1 - t0;
    }
    std::vector<AlarmStatRow> out;
    for (const auto& n : order) out.push_back(rows[n]);
    const bool byDuration = has(sort, "dur");
    std::stable_sort(out.begin(), out.end(), [&](const AlarmStatRow& a, const AlarmStatRow& b) {
        if (byDuration && std::fabs(a.seconds - b.seconds) > 1e-9) return a.seconds > b.seconds;
        if (a.count != b.count) return a.count > b.count;
        if (std::fabs(a.seconds - b.seconds) > 1e-9) return a.seconds > b.seconds;
        return a.priority < b.priority;
    });
    if (top > 0 && out.size() > top) out.resize(top);
    return out;
}

} // namespace hmi
