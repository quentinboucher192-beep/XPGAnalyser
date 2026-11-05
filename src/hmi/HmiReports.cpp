// hmi/HmiReports.cpp - les rapports periodiques (lot 14).
#include "HmiReports.hpp"
#include "HmiReportLayout.hpp"   // 1.25.0 : la mise en page Standard
#include "HmiDisplay.hpp"   // 1.13.0 (seconde passe) : l'unite physique d'une mesure

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hmi::report {

namespace {

std::tm localOf(double epoch) {
    const auto t = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// L'heure legale de ce jour (mois 1..12 ; un jour hors du mois se normalise :
// le 0 est le dernier jour du mois d'avant).
double localEpoch(int year, int month, int day, int minutes) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = minutes / 60;
    tm.tm_min = minutes % 60;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return static_cast<double>(std::mktime(&tm));
}

int weekdayOf(const std::tm& tm) { return tm.tm_wday == 0 ? 7 : tm.tm_wday; }   // 1 lundi ... 7 dimanche

int minutesOf(const Report& r) {
    int m = 360;
    if (!parseTime(r.time, m)) m = 360;
    return m;
}

std::string two(int v) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitNames(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        std::size_t a = 0, b = cur.size();
        while (a < b && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
        if (b > a) out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (const char c : s) {
        if (c == ';' || c == ',' || c == '\n') flush();
        else cur += c;
    }
    flush();
    return out;
}

std::string number(double v, int decimals) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string s = b;
    if (s == "-0" || s == "-0.0" || s == "-0.00") s.erase(0, 1);
    return s;
}

// Assez de decimales pour distinguer les valeurs d'une mesure.
int decimalsFor(double lo, double hi) {
    const double span = std::fabs(hi - lo), mag = std::max(std::fabs(lo), std::fabs(hi));
    if (mag >= 1000 || span >= 100) return 0;
    if (mag >= 100 || span >= 10) return 1;
    return 2;
}

std::string duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "\xE2\x80\x94";
    const long long s = static_cast<long long>(std::llround(seconds));
    const long long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0) return std::to_string(h) + " h " + two(static_cast<int>(m)) + " min";
    if (m > 0) return std::to_string(m) + " min " + two(static_cast<int>(sec)) + " s";
    return std::to_string(sec) + " s";
}

// "07:40:12" le meme jour que la fin de la periode, sinon "24/09 07:40".
std::string shortStamp(std::string_view stamp, double toEpoch) {
    if (stamp.size() < 19) return std::string(stamp);
    const std::string day = stampOf(toEpoch).substr(0, 10);
    if (stamp.substr(0, 10) == day) return std::string(stamp.substr(11, 8));
    return std::string(stamp.substr(8, 2)) + "/" + std::string(stamp.substr(5, 2)) + " " + std::string(stamp.substr(11, 5));
}

std::string priorityName(int p) { return std::string(alarmPriorityLabel(std::clamp(p, 1, kAlarmPriorities))); }

} // namespace

bool parseTime(std::string_view text, int& minutes) {
    std::string t;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.empty()) return false;
    std::size_t i = 0;
    int h = 0, m = 0, digits = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])) && digits < 2) { h = h * 10 + (t[i++] - '0'); ++digits; }
    if (digits == 0) return false;
    if (i < t.size()) {
        if (t[i] != ':' && t[i] != 'h') return false;
        ++i;
        digits = 0;
        while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])) && digits < 2) { m = m * 10 + (t[i++] - '0'); ++digits; }
        if (i != t.size()) return false;
    }
    if (h > 24 || m > 59 || (h == 24 && m != 0)) return false;
    minutes = h * 60 + m;
    return true;
}

double lastDue(const Report& r, double now) {
    const std::tm tm = localOf(now);
    const int at = minutesOf(r);
    const int y = tm.tm_year + 1900, mo = tm.tm_mon + 1, d = tm.tm_mday;
    if (r.period == "semaine") {
        const int back = (weekdayOf(tm) - std::clamp(r.weekday, 1, 7) + 7) % 7;
        double due = localEpoch(y, mo, d - back, at);
        if (due > now) due = localEpoch(y, mo, d - back - 7, at);
        return due;
    }
    if (r.period == "mois") {
        const int day = std::clamp(r.monthDay, 1, 28);
        double due = localEpoch(y, mo, day, at);
        if (due > now) due = localEpoch(y, mo - 1, day, at);
        return due;
    }
    double due = localEpoch(y, mo, d, at);
    if (due > now) due = localEpoch(y, mo, d - 1, at);
    return due;
}

double nextDue(const Report& r, double now) {
    const std::tm tm = localOf(now);
    const int at = minutesOf(r);
    const int y = tm.tm_year + 1900, mo = tm.tm_mon + 1, d = tm.tm_mday;
    if (r.period == "semaine") {
        const int ahead = (std::clamp(r.weekday, 1, 7) - weekdayOf(tm) + 7) % 7;
        double due = localEpoch(y, mo, d + ahead, at);
        if (due <= now) due = localEpoch(y, mo, d + ahead + 7, at);
        return due;
    }
    if (r.period == "mois") {
        const int day = std::clamp(r.monthDay, 1, 28);
        double due = localEpoch(y, mo, day, at);
        if (due <= now) due = localEpoch(y, mo + 1, day, at);
        return due;
    }
    double due = localEpoch(y, mo, d, at);
    if (due <= now) due = localEpoch(y, mo, d + 1, at);
    return due;
}

double periodStart(const Report& r, double due) {
    const std::tm tm = localOf(due);
    const int at = tm.tm_hour * 60 + tm.tm_min;
    const int y = tm.tm_year + 1900, mo = tm.tm_mon + 1, d = tm.tm_mday;
    if (r.period == "semaine") return localEpoch(y, mo, d - 7, at);
    if (r.period == "mois") return localEpoch(y, mo - 1, d, at);
    return localEpoch(y, mo, d - 1, at);
}

std::string stampOf(double epoch, bool seconds) {
    const std::tm tm = localOf(epoch);
    std::string s = std::to_string(tm.tm_year + 1900) + "-" + two(tm.tm_mon + 1) + "-" + two(tm.tm_mday) + " " + two(tm.tm_hour) + ":"
                  + two(tm.tm_min);
    if (seconds) s += ":" + two(tm.tm_sec);
    return s;
}

double epochOf(std::string_view stamp) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0;
    const std::string s(stamp);
    const int n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%lf", &y, &mo, &d, &h, &mi, &sec);
    if (n < 3) return std::numeric_limits<double>::quiet_NaN();
    const double base = localEpoch(y, mo, d, (n >= 5 ? h * 60 + mi : 0));
    return base + (n >= 6 ? sec : 0.0);
}

std::string periodLabel(const Report& r) {
    if (r.period == "semaine") return "Hebdomadaire";
    if (r.period == "mois") return "Mensuel";
    return "Journalier";
}

Content build(const Project& p, const History& h, const Report& r, double from, double to, const std::vector<Production>& production,
              std::string_view writtenAt) {
    Content c;
    const std::string label = lowerAscii(periodLabel(r));
    c.title = !r.title.empty() ? r.title : "Rapport " + label + " \xE2\x80\x94 " + (p.config.name.empty() ? std::string("IHM") : p.config.name);
    c.subtitle = "Du " + stampOf(from) + " au " + stampOf(to) + " \xE2\x80\x94 " + r.name + ", \xC3\xA9" "crit le " + std::string(writtenAt.substr(0, 19));
    c.byPriority.assign(kAlarmPriorities, 0);
    const auto inside = [&](double e) { return std::isfinite(e) && e >= from && e < to; };

    // ---- les alarmes de la periode
    std::vector<const AlarmOccurrence*> occ;
    for (const auto& a : h.alarms)
        if (inside(epochOf(a.appeared))) occ.push_back(&a);
    std::sort(occ.begin(), occ.end(), [](const AlarmOccurrence* x, const AlarmOccurrence* y) { return x->appeared < y->appeared; });
    double totalDuration = 0, longest = -1;
    std::string longestName;
    std::size_t unacked = 0;
    struct Freq {
        int count{0};
        double seconds{0};
        int priority{4};
    };
    std::map<std::string, Freq> freq;
    ExportTable alarms;
    alarms.title = "Alarmes";
    alarms.subtitle = c.subtitle;
    alarms.headers = {"Apparue", "Alarme", "Priorit\xC3\xA9", "Message", "Acquitt\xC3\xA9" "e", "Par", "Disparue", "Dur\xC3\xA9" "e"};
    for (const auto* a : occ) {
        const double start = epochOf(a->appeared);
        const double end = a->cleared.empty() ? to : std::min(to, epochOf(a->cleared));
        const double secs = std::isfinite(end) ? std::max(0.0, end - start) : 0.0;
        totalDuration += secs;
        if (secs > longest) {
            longest = secs;
            longestName = a->name;
        }
        if (a->acked.empty()) ++unacked;
        const int pr = std::clamp(a->priority, 1, kAlarmPriorities);
        ++c.byPriority[static_cast<std::size_t>(pr - 1)];
        auto& f = freq[a->name];
        ++f.count;
        f.seconds += secs;
        f.priority = std::min(f.priority, pr);
        alarms.rows.push_back({shortStamp(a->appeared, to), a->name, priorityName(pr), a->message,
                               a->acked.empty() ? std::string("non") : shortStamp(a->acked, to), a->ackedBy,
                               a->cleared.empty() ? std::string("en cours") : shortStamp(a->cleared, to),
                               duration(secs) + (a->cleared.empty() ? std::string(" (en cours)") : std::string{})});
    }
    ExportTable frequent;
    frequent.title = "Alarmes les plus fr\xC3\xA9quentes";
    frequent.subtitle = c.subtitle;
    frequent.headers = {"Alarme", "Priorit\xC3\xA9", "Apparitions", "Dur\xC3\xA9" "e cumul\xC3\xA9" "e"};
    {
        std::vector<std::pair<std::string, Freq>> list(freq.begin(), freq.end());
        std::sort(list.begin(), list.end(), [](const auto& x, const auto& y) {
            return x.second.count != y.second.count ? x.second.count > y.second.count : x.second.seconds > y.second.seconds;
        });
        for (std::size_t i = 0; i < list.size() && i < 20; ++i)
            frequent.rows.push_back({list[i].first, priorityName(list[i].second.priority), std::to_string(list[i].second.count),
                                     duration(list[i].second.seconds)});
    }

    // ---- les mesures
    std::vector<std::string> wanted = splitNames(r.measures);
    if (wanted.empty()) wanted = p.history.archived;
    struct Stat {
        std::size_t n{0};
        double lo{std::numeric_limits<double>::infinity()}, hi{-std::numeric_limits<double>::infinity()}, sum{0}, last{0}, lastAt{0};
    };
    std::map<std::string, Stat> stats;
    std::map<std::string, std::vector<std::pair<double, double>>> series;   // 1.25.0 : les courbes des mesures
    for (const auto& s : h.samples) {
        if (!inside(s.epoch)) continue;
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), s.variable) == wanted.end()) continue;
        if (std::isfinite(s.value)) series[s.variable].emplace_back(s.epoch, s.value);
        auto& st = stats[s.variable];
        ++st.n;
        st.lo = std::min(st.lo, s.value);
        st.hi = std::max(st.hi, s.value);
        st.sum += s.value;
        if (s.epoch >= st.lastAt) {
            st.last = s.value;
            st.lastAt = s.epoch;
        }
    }
    ExportTable measures;
    measures.title = "Mesures";
    measures.subtitle = c.subtitle;
    measures.headers = {"Variable", "Unit\xC3\xA9", "Minimum", "Moyenne", "Maximum", "Derni\xC3\xA8re", "Mesures"};
    std::size_t sampleCount = 0;
    std::vector<std::string> order = wanted;
    for (const auto& [name, st] : stats)
        if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
    for (const auto& name : order) {
        std::string unit, format;
        int shown = -1;                       // les decimales du format de la variable (lot 13)
        for (const auto& d : p.displays)
            if (d.path == name) {
                unit = d.unit;
                format = d.format;
                if (!d.format.empty()) {
                    const auto dot = d.format.find('.');
                    shown = dot == std::string::npos ? 0 : static_cast<int>(d.format.size() - dot - 1);
                }
            }
        // 1.13.0 (seconde passe) : sans unite sur sa ligne (ou sans ligne), l'unite physique de la variable.
        if (unit.empty()) unit = physicalDisplayUnit(p, name, nullptr, format);
        const auto it = stats.find(name);
        if (it == stats.end() || it->second.n == 0) {
            measures.rows.push_back({name, unit, "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "0"});
            continue;
        }
        const auto& st = it->second;
        sampleCount += st.n;
        const int dec = shown >= 0 ? std::min(shown, 6) : decimalsFor(st.lo, st.hi);
        measures.rows.push_back({name, unit, number(st.lo, dec), number(st.sum / static_cast<double>(st.n), dec), number(st.hi, dec),
                                 number(st.last, dec), std::to_string(st.n)});
        // 1.25.0 : sa courbe - 400 points au plus (la moyenne de chaque tranche de temps), le minimum et le maximum vrais.
        if (auto sit = series.find(name); sit != series.end() && !sit->second.empty()) {
            auto pts = std::move(sit->second);
            std::stable_sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            Curve cv;
            cv.name = name;
            cv.unit = unit;
            cv.lo = st.lo;
            cv.hi = st.hi;
            constexpr std::size_t kMax = 400;
            if (pts.size() <= kMax) {
                cv.points = std::move(pts);
            } else {
                for (std::size_t b = 0; b < kMax; ++b) {
                    const std::size_t a0 = b * pts.size() / kMax, a1 = std::max(a0 + 1, (b + 1) * pts.size() / kMax);
                    double t = 0, v = 0;
                    for (std::size_t k = a0; k < a1; ++k) { t += pts[k].first; v += pts[k].second; }
                    const double n = static_cast<double>(a1 - a0);
                    cv.points.emplace_back(t / n, v / n);
                }
            }
            c.curves.push_back(std::move(cv));
        }
    }

    // ---- la production
    ExportTable prod;
    prod.title = "Production";
    prod.subtitle = c.subtitle;
    prod.headers = {"Compteur", "Poste", "Bons", "Rebuts", "Total", "Cadence (/h)", "TRS"};
    double good = 0, bad = 0;
    for (const auto& pr : production) {
        const auto& f = pr.figures;
        good += f.good;
        bad += f.bad;
        prod.rows.push_back({pr.name, f.shift, number(f.good, 0), number(f.bad, 0), number(f.total, 0), number(f.rate, 1),
                             number(f.oee * 100.0, 1) + " %"});
    }

    // ---- les evenements
    ExportTable events;
    events.title = "\xC3\x89v\xC3\xA9nements";
    events.subtitle = c.subtitle;
    events.headers = {"Heure", "Type", "Source", "Message", "Utilisateur"};
    for (const auto& e : h.events)
        if (inside(epochOf(e.stamp))) events.rows.push_back({shortStamp(e.stamp, to), e.kind, e.source, e.message, e.user});

    // ---- la synthese
    const auto count = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); };
    if (r.alarms) {
        c.summary.emplace_back("Alarmes apparues", std::to_string(occ.size()));
        std::string split;
        for (int k = 1; k <= kAlarmPriorities; ++k)
            split += (k > 1 ? " \xC2\xB7 " : "") + priorityName(k) + " " + std::to_string(c.byPriority[static_cast<std::size_t>(k - 1)]);
        c.summary.emplace_back("Par priorit\xC3\xA9", split);
        c.summary.emplace_back("Dur\xC3\xA9" "e cumul\xC3\xA9" "e", duration(totalDuration));
        c.summary.emplace_back("La plus longue", longestName.empty() ? std::string("\xE2\x80\x94") : longestName + " (" + duration(longest) + ")");
        c.summary.emplace_back("Non acquitt\xC3\xA9" "es", std::to_string(unacked));
    }
    if (!order.empty())
        c.summary.emplace_back("Mesures", count(order.size(), "variable", "variables") + ", " + count(sampleCount, "mesure", "mesures"));
    if (r.production && !production.empty())
        c.summary.emplace_back("Production", "bons " + number(good, 0) + " \xC2\xB7 rebuts " + number(bad, 0) + " \xC2\xB7 "
                                                 + count(production.size(), "compteur", "compteurs"));
    if (r.events) c.summary.emplace_back("\xC3\x89v\xC3\xA9nements", std::to_string(events.rows.size()));

    if (r.alarms) {
        c.sections.push_back(std::move(alarms));
        c.keys.emplace_back("alarmes");
        if (!frequent.rows.empty()) {
            c.sections.push_back(std::move(frequent));
            c.keys.emplace_back("frequentes");
        }
    }
    if (!order.empty()) {
        c.sections.push_back(std::move(measures));
        c.keys.emplace_back("mesures");
    }
    if (r.production && !production.empty()) {
        c.sections.push_back(std::move(prod));
        c.keys.emplace_back("production");
    }
    if (r.events) {
        c.sections.push_back(std::move(events));
        c.keys.emplace_back("evenements");
    }
    // 1.25.0 : la periode et les trous des titres et des pieds d'une mise en page.
    c.from = from;
    c.to = to;
    c.alarms = r.alarms;
    const std::string written(writtenAt.substr(0, std::min<std::size_t>(19, writtenAt.size())));
    c.fields = {{"Titre", c.title},
                {"SousTitre", c.subtitle},
                {"Rapport", !r.title.empty() ? r.title : "Rapport " + label},
                {"Nom", r.name},
                {"Periode", periodLabel(r)},
                {"Site", p.config.name.empty() ? std::string("IHM") : p.config.name},
                {"Projet", p.config.name.empty() ? std::string("IHM") : p.config.name},
                {"Debut", stampOf(from)},
                {"Fin", stampOf(to)},
                {"Date", written.substr(0, std::min<std::size_t>(10, written.size()))},
                {"Heure", written.size() >= 16 ? written.substr(11, 5) : std::string{}},
                {"Ecrit", written}};
    return c;
}

// =================================================================== PDF ===
//  1.25.0 : la mise en page << Standard >> (hmi/HmiReportLayout.cpp : la page, les parties, le pied).
Bytes renderPdf(const Content& c) { return renderPdf(Project{}, c, *builtInLayout("Standard")); }

Bytes renderXlsx(const Content& c) {
    std::vector<ExportTable> sheets;
    ExportTable summary;
    summary.title = "Synth\xC3\xA8se";
    summary.subtitle = c.title + " \xE2\x80\x94 " + c.subtitle;
    summary.headers = {"Rubrique", "Valeur"};
    for (const auto& [k, v] : c.summary) summary.rows.push_back({k, v});
    sheets.push_back(std::move(summary));
    for (const auto& t : c.sections) sheets.push_back(t);
    return exportXlsxSheets(sheets);
}

Bytes render(const Content& c, ExportFormat f) { return f == ExportFormat::Excel ? renderXlsx(c) : renderPdf(c); }

std::string fileName(const Report& r, double toEpoch, ExportFormat f) {
    const std::string when = stampOf(toEpoch);   // "2026-09-25 06:00"
    return exportFileName("rapport_" + r.name + "_" + when.substr(0, 10) + "_" + when.substr(11, 2) + when.substr(14, 2), f);
}

} // namespace hmi::report
