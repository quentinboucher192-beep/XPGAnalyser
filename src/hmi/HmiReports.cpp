// hmi/HmiReports.cpp - les rapports periodiques (lot 14).
#include "HmiReports.hpp"

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
    for (const auto& s : h.samples) {
        if (!inside(s.epoch)) continue;
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), s.variable) == wanted.end()) continue;
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
        std::string unit;
        int shown = -1;                       // les decimales du format de la variable (lot 13)
        for (const auto& d : p.displays)
            if (d.path == name) {
                unit = d.unit;
                if (!d.format.empty()) {
                    const auto dot = d.format.find('.');
                    shown = dot == std::string::npos ? 0 : static_cast<int>(d.format.size() - dot - 1);
                }
            }
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
        if (!frequent.rows.empty()) c.sections.push_back(std::move(frequent));
    }
    if (!order.empty()) c.sections.push_back(std::move(measures));
    if (r.production && !production.empty()) c.sections.push_back(std::move(prod));
    if (r.events) c.sections.push_back(std::move(events));
    return c;
}

// =================================================================== PDF ===
//  A4 portrait. Page 1 : le titre, la periode, la synthese, les alarmes par
//  priorite (un graphique a barres) ; puis chaque section, son tableau coupe
//  aux pages (l'en-tete repete). Le pied : la page, le rapport.
Bytes renderPdf(const Content& c) {
    constexpr double W = 595, H = 842, M = 40;
    constexpr double fs = 8.5, rowH = 14, headH = 17;
    const double avail = W - 2 * M;
    std::vector<std::string> pages;
    std::string s;
    double y = H - M;
    const auto textAt = [&](double x, double yy, const std::string& winAnsi, double size, bool bold, double gray) {
        s += "BT /" + std::string(bold ? "F2 " : "F1 ") + pdfNumber(size) + " Tf " + pdfNumber(gray) + " g " + pdfNumber(x) + " "
           + pdfNumber(yy) + " Td " + pdfLiteral(winAnsi) + " Tj ET\n";
    };
    const auto fit = [&](const std::string& winAnsi, double size, bool bold, double maxW) {
        // Un peu de marge : une colonne a la largeur exacte de son texte (w + 8 - 8)
        // ne doit pas le couper pour un arrondi.
        maxW += 0.01;
        if (pdfTextWidth(winAnsi, size, bold) <= maxW) return winAnsi;
        std::string t = winAnsi;
        while (!t.empty() && pdfTextWidth(t + "...", size, bold) > maxW) t.pop_back();
        return t + "...";
    };
    const auto newPage = [&] {
        pages.push_back(std::move(s));
        s.clear();
        y = H - M;
    };
    const auto rect = [&](double x, double yy, double w, double h, double r, double g, double b) {
        s += pdfNumber(r) + " " + pdfNumber(g) + " " + pdfNumber(b) + " rg " + pdfNumber(x) + " " + pdfNumber(yy) + " " + pdfNumber(w) + " "
           + pdfNumber(h) + " re f\n";
    };

    // ---- le titre et la periode
    textAt(M, y - 16, fit(toWinAnsi(c.title), 16, true, avail), 16, true, 0.1);
    textAt(M, y - 32, fit(toWinAnsi(c.subtitle), 9, false, avail), 9, false, 0.4);
    s += "0.2 0.45 0.85 RG 1.5 w " + pdfNumber(M) + " " + pdfNumber(y - 40) + " m " + pdfNumber(W - M) + " " + pdfNumber(y - 40) + " l S\n";
    y -= 62;

    // ---- la synthese : libelle, valeur
    if (!c.summary.empty()) {
        textAt(M, y, toWinAnsi("Synth\xC3\xA8se"), 12, true, 0.15);
        y -= 10;
        for (std::size_t i = 0; i < c.summary.size(); ++i) {
            if (i % 2 == 0) rect(M, y - rowH, avail, rowH, 0.95, 0.96, 0.98);
            textAt(M + 6, y - rowH + 4, fit(toWinAnsi(c.summary[i].first), 9, true, 150), 9, true, 0.2);
            textAt(M + 170, y - rowH + 4, fit(toWinAnsi(c.summary[i].second), 9, false, avail - 176), 9, false, 0.15);
            y -= rowH;
        }
        y -= 18;
    }

    // ---- les alarmes par priorite : des barres
    int most = 0;
    for (const int n : c.byPriority) most = std::max(most, n);
    if (!c.byPriority.empty() && most > 0) {
        textAt(M, y, toWinAnsi("Alarmes par priorit\xC3\xA9"), 12, true, 0.15);
        y -= 12;
        static const double colors[4][3] = {{0.85, 0.25, 0.22}, {0.95, 0.55, 0.2}, {0.93, 0.76, 0.2}, {0.3, 0.55, 0.9}};
        const double barMax = avail - 170;
        for (std::size_t k = 0; k < c.byPriority.size() && k < 4; ++k) {
            const double w = barMax * static_cast<double>(c.byPriority[k]) / static_cast<double>(most);
            textAt(M + 6, y - 14, toWinAnsi(priorityName(static_cast<int>(k) + 1)), 9, false, 0.2);
            rect(M + 90, y - 16, std::max(1.0, w), 12, colors[k][0], colors[k][1], colors[k][2]);
            textAt(M + 96 + std::max(1.0, w), y - 14, std::to_string(c.byPriority[k]), 9, true, 0.2);
            y -= 18;
        }
        y -= 14;
    }

    // ---- les sections
    for (const auto& t : c.sections) {
        std::size_t ncol = std::max<std::size_t>(1, t.headers.size());
        std::vector<std::string> heads(ncol);
        for (std::size_t k = 0; k < t.headers.size(); ++k) heads[k] = toWinAnsi(t.headers[k]);
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : t.rows) {
            std::vector<std::string> row(ncol);
            for (std::size_t k = 0; k < r.size() && k < ncol; ++k) row[k] = toWinAnsi(r[k]);
            rows.push_back(std::move(row));
        }
        std::vector<double> widths(ncol, 28);
        for (std::size_t k = 0; k < ncol; ++k) {
            widths[k] = std::max(widths[k], pdfTextWidth(heads[k], fs, true) + 8);
            for (const auto& r : rows) widths[k] = std::max(widths[k], std::min(avail * 0.42, pdfTextWidth(r[k], fs, false) + 8));
        }
        // Trop large : les colonnes longues (un message) retrecissent d'abord,
        // les courtes (une heure, une priorite) gardent leur largeur.
        double total = 0;
        for (const double w : widths) total += w;
        if (total > avail) {
            double narrow = 0, wide = 0;
            for (const double w : widths) (w > 90 ? wide : narrow) += w;
            if (wide > 0 && narrow < avail) {
                const double k = (avail - narrow) / wide;
                for (auto& w : widths)
                    if (w > 90) w *= k;
            } else {
                for (auto& w : widths) w *= avail / total;
            }
        }
        double tableW = 0;
        for (const double w : widths) tableW += w;
        const auto header = [&](bool again) {
            textAt(M, y - 12, fit(toWinAnsi(t.title + (again ? " (suite)" : "")), 12, true, avail), 12, true, 0.15);
            y -= 22;
            rect(M, y - headH, tableW, headH, 0.85, 0.88, 0.93);
            double x = M;
            for (std::size_t k = 0; k < ncol; ++k) {
                textAt(x + 4, y - headH + 5.5, fit(heads[k], fs, true, widths[k] - 8), fs, true, 0.1);
                x += widths[k];
            }
            y -= headH;
        };
        if (y - 22 - headH - rowH < M + 20) newPage();
        header(false);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (y - rowH < M + 20) {
                newPage();
                header(true);
            }
            if (i % 2 == 1) rect(M, y - rowH, tableW, rowH, 0.96, 0.97, 0.98);
            double x = M;
            for (std::size_t k = 0; k < ncol; ++k) {
                textAt(x + 4, y - rowH + 4, fit(rows[i][k], fs, false, widths[k] - 8), fs, false, 0.15);
                x += widths[k];
            }
            y -= rowH;
        }
        if (rows.empty()) {
            textAt(M + 4, y - rowH + 4, toWinAnsi("(rien sur la p\xC3\xA9riode)"), fs, false, 0.45);
            y -= rowH;
        }
        s += "0.6 G 0.6 w " + pdfNumber(M) + " " + pdfNumber(y) + " m " + pdfNumber(M + tableW) + " " + pdfNumber(y) + " l S\n";
        y -= 24;
    }
    pages.push_back(std::move(s));

    // ---- le pied de chaque page, puis les objets
    const std::size_t count = pages.size();
    const std::string foot = toWinAnsi(c.title);
    for (std::size_t p = 0; p < count; ++p) {
        s.clear();
        const std::string page = "Page " + std::to_string(p + 1) + " / " + std::to_string(count);
        textAt(W - M - pdfTextWidth(page, 8, false), M - 18, page, 8, false, 0.45);
        textAt(M, M - 18, fit("XpgAnalyzer - " + foot, 8, false, avail - 80), 8, false, 0.45);
        pages[p] += s;
    }
    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t p = 0; p < count; ++p) kids += std::to_string(5 + 2 * p) + " 0 R ";
    objects.push_back("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(count) + " >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (std::size_t p = 0; p < count; ++p) {
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pdfNumber(W) + " " + pdfNumber(H)
                          + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + std::to_string(6 + 2 * p) + " 0 R >>");
        objects.push_back("<< /Length " + std::to_string(pages[p].size()) + " >>\nstream\n" + pages[p] + "endstream");
    }
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(pdf.begin(), pdf.end());
}

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
