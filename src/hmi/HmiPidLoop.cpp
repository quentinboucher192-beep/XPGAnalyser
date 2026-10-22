// hmi/HmiPidLoop.cpp - 1.12.24 : ce que la carte et la fenetre du Reglage PID disent d'une boucle
// (sa pastille, ses nombres, ses durees, son export), sans le moteur.
#include "HmiPidLoop.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hmi {

namespace {

// Sans accents ni casse : "Equilibre" pour "Équilibré".
std::string plain(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            out += (d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB) ? 'e' : (d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5) ? 'a' : 'x';
            ++i;
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

std::string stampOf(double epoch, const char* format) {
    const auto secs = static_cast<std::time_t>(std::floor(epoch));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[40];
    std::strftime(b, sizeof b, format, &tm);
    return b;
}

} // namespace

double pidRangeSeconds(int range) noexcept {
    switch (range) {
        case 1: return 8 * 3600.0;
        case 2: return 24 * 3600.0;
        case 3: return 7 * 86400.0;
        default: return 1800.0;
    }
}

std::string_view pidRangeName(int range) noexcept {
    switch (range) {
        case 1: return "8 h";
        case 2: return "24 h";
        case 3: return "7 jours";
        default: return "30 min";
    }
}

pid::Goal pidGoalOf(std::string_view text) noexcept {
    const std::string p = plain(text);
    if (p.rfind("rob", 0) == 0) return pid::Goal::Robust;
    if (p.rfind("rap", 0) == 0) return pid::Goal::Fast;
    return pid::Goal::Balanced;
}

pid::Derivative pidDerivativeOf(std::string_view text) noexcept {
    const std::string p = plain(text);
    if (p.rfind("jam", 0) == 0) return pid::Derivative::Never;
    if (p.rfind("tou", 0) == 0) return pid::Derivative::Always;
    return pid::Derivative::IfHelps;
}

PidBadge pidBadge(const PidLoop& L) {
    if (!L.read) return {"Non lue", 3};
    switch (L.learner.state()) {
        case pid::State::NoData:      return {"Pas de donn\xC3\xA9" "es", 0};
        case pid::State::Learning:    return {"Apprend", 0};
        case pid::State::Healthy:     return {"Saine", 1};
        case pid::State::Oscillating: return {"Oscille", 3};
        case pid::State::Sluggish:    return {"Lente", 2};
        case pid::State::Saturated:   return {"Satur\xC3\xA9" "e", 3};
        case pid::State::Manual:      return {"En MANU", 0};
        case pid::State::Frozen:      return {"Mesure fig\xC3\xA9" "e", 3};
    }
    return {"Apprend", 0};
}

pid::BlockValues pidBlockOf(const PidLoop& L, const pid::Gains& g) {
    return pid::toBlock(g, L.bind.form, L.bind.units(L.learner.config().outSpan()));
}

std::string pidNumber(double v, int decimals) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", std::clamp(decimals, 0, 6), v);
    std::string s = b;
    // La virgule francaise ; pas de zeros inutiles apres elle (1,50 -> 1,5 ; 2,00 -> 2).
    if (const auto dot = s.find('.'); dot != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (const auto d2 = s.find('.'); d2 != std::string::npos) s[d2] = ',';
    }
    if (s == "-0") s = "0";
    return s;
}

std::string pidTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "\xE2\x80\x94";
    if (seconds < 10) return pidNumber(seconds, 1) + " s";
    const long long s = std::llround(seconds);
    char b[48];
    if (s < 60) std::snprintf(b, sizeof b, "%lld s", s);
    else if (s < 3600) std::snprintf(b, sizeof b, "%lld min %02lld s", s / 60, s % 60);
    else std::snprintf(b, sizeof b, "%lld h %02lld min", s / 3600, (s / 60) % 60);
    return b;
}

std::string pidTimeUnitName(double timeSeconds) {
    if (std::fabs(timeSeconds - 60) < 1e-9) return "min";
    if (std::fabs(timeSeconds - 0.001) < 1e-12) return "ms";
    if (std::fabs(timeSeconds - 0.1) < 1e-12) return "0,1 s";
    return "s";
}

std::string pidCsv(const PidLoop& L, double window) {
    std::string out = "heure;consigne;mesure;mesure min;mesure max;sortie\r\n";
    for (const auto& m : L.learner.minutes(window)) {
        out += stampOf(m.t, "%Y-%m-%d %H:%M") + ";" + pidNumber(m.sp, 3) + ";" + pidNumber(m.pv, 3) + ";" + pidNumber(m.pvMin, 3) + ";"
             + pidNumber(m.pvMax, 3) + ";" + pidNumber(m.out, 3) + "\r\n";
    }
    return out;
}

} // namespace hmi
