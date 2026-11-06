// =============================================================================
//  licensing/Time.cpp - 1.30.0 : les dates des licences (voir Time.hpp)
// =============================================================================
#include "Time.hpp"

#include <chrono>
#include <cstdio>

namespace licensing {

namespace {

// Jours depuis 1970-01-01 du jour civil (a, m, j).
constexpr std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

struct Civil {
    std::int64_t y;
    unsigned     m, d;
};

constexpr Civil civilFromDays(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {y + (m <= 2 ? 1 : 0), m, d};
}

static_assert(daysFromCivil(1970, 1, 1) == 0);
static_assert(daysFromCivil(2000, 3, 1) == 11017);

std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

bool digits(std::string_view s, std::size_t pos, std::size_t n, unsigned& out) noexcept {
    out = 0;
    for (std::size_t i = pos; i < pos + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + static_cast<unsigned>(s[i] - '0');
    }
    return true;
}

} // namespace

Seconds systemNow() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string isoTime(Seconds t) {
    const std::int64_t days = floorDiv(t, kDay);
    const std::int64_t rest = t - days * kDay;
    const Civil c = civilFromDays(days);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02d:%02d:%02dZ", static_cast<long long>(c.y), c.m, c.d,
                  static_cast<int>(rest / 3600), static_cast<int>((rest % 3600) / 60), static_cast<int>(rest % 60));
    return buf;
}

std::string isoDate(Seconds t) { return isoTime(t).substr(0, 10); }

bool parseIsoTime(std::string_view s, Seconds& out) noexcept {
    // AAAA-MM-JJTHH:MM:SSZ : 20 caracteres exactement.
    if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!digits(s, 0, 4, y) || !digits(s, 5, 2, mo) || !digits(s, 8, 2, d) || !digits(s, 11, 2, h) || !digits(s, 14, 2, mi)
        || !digits(s, 17, 2, se))
        return false;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || h > 23 || mi > 59 || se > 59) return false;
    static constexpr unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const unsigned maxDay = kDays[mo - 1] + (mo == 2 && leap ? 1 : 0);
    if (d > maxDay) return false;
    out = daysFromCivil(y, mo, d) * kDay + h * 3600 + mi * 60 + se;
    return true;
}

} // namespace licensing
