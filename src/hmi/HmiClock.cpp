// =============================================================================
//  hmi/HmiClock.cpp - 1.12.12 (lot H) : l'horloge liee a une variable
// -----------------------------------------------------------------------------
//  Voir HmiClock.hpp : lire une heure (TOD, DT, DATE, STRING ISO 8601, REAL en
//  secondes, TIME), la montrer dans un fuseau, ecrire la date et l'heure.
// =============================================================================
#include "HmiClock.hpp"

#include "HmiModel.hpp"      // parseNumber
#include "HmiRoles.hpp"      // parseDuration
#include "../sim/Value.hpp"  // daysFromCivil, civilFromDays, parseDateText

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>

namespace hmi::clock {

namespace {

constexpr std::int64_t kDay = 86'400;

std::string_view trimView(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
// Des secondes depuis le 01/01/1970 (une heure du mur) : la date et l'heure.
DateTime fromSeconds(std::int64_t s) {
    const std::int64_t days = floorDiv(s, kDay), inDay = s - days * kDay;
    std::int64_t y = 0;
    unsigned m = 1, d = 1;
    sim::civilFromDays(days, y, m, d);
    DateTime t;
    t.year = static_cast<int>(y);
    t.month = static_cast<int>(m);
    t.day = static_cast<int>(d);
    t.hour = static_cast<int>(inDay / 3600);
    t.minute = static_cast<int>((inDay / 60) % 60);
    t.second = static_cast<int>(inDay % 60);
    return t;
}
std::int64_t secondsOf(const DateTime& t) {
    return sim::daysFromCivil(t.year, static_cast<unsigned>(t.month), static_cast<unsigned>(t.day)) * kDay + t.hour * 3600
         + t.minute * 60 + t.second;
}
void setClock(DateTime& t, std::int64_t secondsOfDay) {
    secondsOfDay = ((secondsOfDay % kDay) + kDay) % kDay;
    t.hour = static_cast<int>(secondsOfDay / 3600);
    t.minute = static_cast<int>((secondsOfDay / 60) % 60);
    t.second = static_cast<int>(secondsOfDay % 60);
}

// Un petit lecteur : des chiffres, un caractere attendu.
struct Scan {
    std::string_view s;
    std::size_t      at{0};
    [[nodiscard]] bool done() const noexcept { return at >= s.size(); }
    [[nodiscard]] char peek() const noexcept { return done() ? '\0' : s[at]; }
    bool eat(char c) noexcept {
        if (peek() != c) return false;
        ++at;
        return true;
    }
    // De `minDigits` a `maxDigits` chiffres ; faux : il n'y en a pas assez.
    bool number(int minDigits, int maxDigits, int& out) noexcept {
        int n = 0, v = 0;
        while (n < maxDigits && !done() && std::isdigit(static_cast<unsigned char>(s[at]))) {
            v = v * 10 + (s[at] - '0');
            ++at;
            ++n;
        }
        out = v;
        return n >= minDigits;
    }
    void spaces() noexcept {
        while (!done() && s[at] == ' ') ++at;
    }
};

// Une date au debut : 2026-10-12 ou 12/10/2026.
bool scanDate(Scan& sc, int& y, int& m, int& d) {
    const std::size_t start = sc.at;
    int a = 0;
    if (sc.number(4, 4, a) && sc.eat('-')) {
        y = a;
        if (sc.number(1, 2, m) && sc.eat('-') && sc.number(1, 2, d)) return true;
    }
    sc.at = start;
    if (sc.number(1, 2, d) && sc.eat('/') && sc.number(1, 2, m) && sc.eat('/') && sc.number(4, 4, y)) return true;
    sc.at = start;
    return false;
}
// Une heure : 14:30, 14:30:05, 14:30:05.250 (les fractions sont lues, pas montrees).
bool scanClock(Scan& sc, int& h, int& mi, int& s) {
    const std::size_t start = sc.at;
    s = 0;
    if (!(sc.number(1, 2, h) && sc.eat(':') && sc.number(2, 2, mi))) {
        sc.at = start;
        return false;
    }
    if (sc.eat(':')) {
        if (!sc.number(2, 2, s)) {
            sc.at = start;
            return false;
        }
        if (sc.peek() == '.' || sc.peek() == ',') {
            ++sc.at;
            int frac = 0;
            if (!sc.number(1, 9, frac)) {
                sc.at = start;
                return false;
            }
        }
    }
    return true;
}
// Un fuseau a la fin : Z, UTC, +02:00, +0200, +2, -05:30.
bool scanZone(Scan& sc, int& minutes) {
    sc.spaces();
    if (sc.done()) return false;
    const std::string rest = lower(sc.s.substr(sc.at));
    if (rest == "z" || rest == "utc" || rest == "gmt") {
        minutes = 0;
        sc.at = sc.s.size();
        return true;
    }
    if (rest.rfind("utc", 0) == 0 || rest.rfind("gmt", 0) == 0) sc.at += 3;
    const char sign = sc.peek();
    if (sign != '+' && sign != '-') return false;
    ++sc.at;
    int h = 0, m = 0;
    const std::size_t digitsAt = sc.at;
    if (!sc.number(1, 2, h)) return false;
    if (sc.eat(':')) {
        if (!sc.number(2, 2, m)) return false;
    } else if (sc.at - digitsAt == 2 && !sc.done()) {
        if (!sc.number(2, 2, m)) return false;   // +0200
    }
    if (h > 14 || m > 59) return false;
    minutes = (sign == '-' ? -1 : 1) * (h * 60 + m);
    return true;
}

// Le texte apres un prefixe IEC (TOD#, DT#...), sans casse ; nullopt : pas ce prefixe.
std::optional<std::string_view> after(std::string_view t, std::initializer_list<std::string_view> prefixes) {
    for (const auto p : prefixes)
        if (t.size() > p.size() && lower(t.substr(0, p.size())) == lower(p)) return t.substr(p.size());
    return std::nullopt;
}

const char* const kMonths[12] = {"janvier", "f\xC3\xA9vrier", "mars",  "avril",     "mai",      "juin",
                                 "juillet", "ao\xC3\xBBt",    "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};
const char* const kShortMonths[12] = {"janv.", "f\xC3\xA9vr.", "mars", "avr.", "mai", "juin",
                                      "juil.", "ao\xC3\xBBt",   "sept.", "oct.", "nov.", "d\xC3\xA9" "c."};
const char* const kDays[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};

} // namespace

bool parseZone(std::string_view text, Zone& out) {
    out = Zone{};
    const std::string t = lower(trimView(text));
    if (t.empty() || t == "local" || t == "locale" || t == "heure locale") return true;
    if (t == "utc" || t == "gmt" || t == "z") {
        out.kind = Zone::Kind::Utc;
        return true;
    }
    std::string_view rest = t;
    if (rest.rfind("utc", 0) == 0 || rest.rfind("gmt", 0) == 0) rest = trimView(rest.substr(3));
    int sign = 1;
    if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
        sign = rest.front() == '-' ? -1 : 1;
        rest = trimView(rest.substr(1));
    }
    if (rest.empty()) return false;
    double hours = 0;
    int minutes = 0;
    if (const auto colon = rest.find(':'); colon != std::string_view::npos) {
        double h = 0, m = 0;
        if (!parseNumber(rest.substr(0, colon), h) || !parseNumber(rest.substr(colon + 1), m) || h != std::floor(h) || m != std::floor(m)
            || m < 0 || m >= 60)
            return false;
        minutes = static_cast<int>(h) * 60 + static_cast<int>(m);
    } else {
        if (!parseNumber(rest, hours)) return false;   // 2, 5,5 ou 5.5
        minutes = static_cast<int>(std::lround(hours * 60.0));
    }
    if (minutes < 0 || minutes > 14 * 60) return false;
    out.kind = minutes == 0 ? Zone::Kind::Utc : Zone::Kind::Offset;
    out.minutes = sign * minutes;
    return true;
}

DateTime civilOf(double epochSeconds, const Zone& zone) {
    if (zone.kind == Zone::Kind::Local) return dateTimeFromEpoch(epochSeconds);
    const auto s = static_cast<std::int64_t>(std::floor(epochSeconds));
    return fromSeconds(s + (zone.kind == Zone::Kind::Offset ? zone.minutes * 60LL : 0LL));
}

Reading read(std::string_view value, const Zone& zone, const DateTime& today) {
    Reading r;
    r.time = today;
    std::string_view t = trimView(value);
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) t = trimView(t.substr(1, t.size() - 2));
    const auto refuse = [&](std::string why) {
        r.ok = false;
        r.why = std::move(why);
        return r;
    };
    if (t.empty()) return refuse("aucune heure");
    const std::string quoted = "'" + std::string(t) + "'";
    // Les litteraux IEC : TOD#14:30:00, DT#2026-10-12-14:30:00, D#2026-10-12, T#1h30m.
    std::int64_t ms = 0;
    if (const auto rest = after(t, {"TIME_OF_DAY#", "TOD#"})) {
        if (!sim::parseDateText(sim::Type::Tod, *rest, ms)) return refuse(quoted + " n'est pas une heure");
        setClock(r.time, floorDiv(ms, 1000));
        r.hasDate = false;
        return r;
    }
    if (const auto rest = after(t, {"DATE_AND_TIME#", "DT#"})) {
        if (!sim::parseDateText(sim::Type::Dt, *rest, ms)) return refuse(quoted + " n'est pas une date et heure");
        r.time = fromSeconds(floorDiv(ms, 1000));
        return r;
    }
    if (const auto rest = after(t, {"DATE#", "D#"})) {
        if (!sim::parseDateText(sim::Type::Date, *rest, ms)) return refuse(quoted + " n'est pas une date");
        r.time = fromSeconds(floorDiv(ms, 1000));
        return r;
    }
    if (const auto rest = after(t, {"TIME#", "T#"})) {
        if (!roles::parseDuration(t, ms)) return refuse(quoted + " n'est pas une dur\xC3\xA9" "e");
        setClock(r.time, floorDiv(ms, 1000));   // la duree depuis minuit
        r.hasDate = false;
        return r;
    }
    // Un nombre de secondes : moins d'un jour, une heure du jour ; au-dela, un horodatage Unix (UTC).
    if (double x = 0; parseNumber(t, x)) {
        if (!std::isfinite(x) || std::fabs(x) > 1e13) return refuse(quoted + " : hors des dates possibles");
        if (x >= 0 && x < static_cast<double>(kDay)) {
            setClock(r.time, static_cast<std::int64_t>(std::floor(x)));
            r.hasDate = false;
            return r;
        }
        r.time = civilOf(x, zone);
        return r;
    }
    // Une date, une heure, les deux (ISO 8601, ou a la francaise), et peut-etre un fuseau.
    Scan sc{t};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    const bool date = scanDate(sc, y, mo, d);
    bool clockRead = false;
    if (date) {
        if (sc.peek() == 'T' || sc.peek() == 't' || sc.peek() == ' ' || sc.peek() == '-') {
            const std::size_t mark = sc.at;
            ++sc.at;
            sc.spaces();
            clockRead = scanClock(sc, h, mi, s);
            if (!clockRead) sc.at = mark;
        }
    } else {
        clockRead = scanClock(sc, h, mi, s);
    }
    if (!date && !clockRead) return refuse(quoted + " n'est pas une heure (14:30:00, 2026-10-12T14:30:00, un TOD, un DT)");
    if (date && (mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo))) return refuse(quoted + " : date impossible");
    if (clockRead && (h > 23 || mi > 59 || s > 59)) return refuse(quoted + " : heure impossible");
    int zoneMinutes = 0;
    const bool zoned = scanZone(sc, zoneMinutes);
    sc.spaces();
    if (!sc.done()) return refuse(quoted + " n'est pas une heure (14:30:00, 2026-10-12T14:30:00, un TOD, un DT)");
    if (!date) {
        r.hasDate = false;
        if (!zoned) {
            setClock(r.time, h * 3600 + mi * 60 + s);
            return r;
        }
        y = today.year;
        mo = today.month;
        d = today.day;
    }
    DateTime wall{y, mo, d, h, mi, s};
    if (zoned) {
        // Une heure qui dit son fuseau : l'instant, montre dans le fuseau de l'horloge.
        const DateTime shown = civilOf(static_cast<double>(secondsOf(wall) - zoneMinutes * 60LL), zone);
        if (date) r.time = shown;
        else {
            r.time.hour = shown.hour;
            r.time.minute = shown.minute;
            r.time.second = shown.second;
        }
        return r;
    }
    r.time = wall;
    return r;
}

std::string formatDate(const DateTime& d, std::string_view format) {
    char b[64];
    const int m = std::clamp(d.month, 1, 12) - 1;
    const std::string day = d.day == 1 ? std::string("1er") : std::to_string(d.day);
    if (format == "jj/mm/aa") std::snprintf(b, sizeof b, "%02d/%02d/%02d", d.day, d.month, ((d.year % 100) + 100) % 100);
    else if (format == "aaaa-mm-jj") std::snprintf(b, sizeof b, "%04d-%02d-%02d", d.year, d.month, d.day);
    else if (format == "jj mmm aaaa") std::snprintf(b, sizeof b, "%s %s %04d", day.c_str(), kShortMonths[m], d.year);
    else if (format == "jour jj mois aaaa")
        std::snprintf(b, sizeof b, "%s %s %s %04d", kDays[weekdayOf(d)], day.c_str(), kMonths[m], d.year);
    else std::snprintf(b, sizeof b, "%02d/%02d/%04d", d.day, d.month, d.year);
    return b;
}

std::string formatTime(const DateTime& d, bool twelve, bool seconds) {
    char b[32];
    const int hour = twelve ? (d.hour % 12 == 0 ? 12 : d.hour % 12) : d.hour;
    if (seconds) std::snprintf(b, sizeof b, "%02d:%02d:%02d", hour, d.minute, d.second);
    else std::snprintf(b, sizeof b, "%02d:%02d", hour, d.minute);
    std::string out = b;
    if (twelve) out += d.hour < 12 ? " AM" : " PM";
    return out;
}

int weekdayOf(const DateTime& d) {
    const std::int64_t days = sim::daysFromCivil(d.year, static_cast<unsigned>(std::clamp(d.month, 1, 12)),
                                                 static_cast<unsigned>(std::clamp(d.day, 1, 31)));
    return static_cast<int>(((days + 3) % 7 + 7) % 7);   // le 01/01/1970 : un jeudi
}

} // namespace hmi::clock
