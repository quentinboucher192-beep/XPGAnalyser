// =============================================================================
//  ui/widgets/Charts.cpp - 1.47.0 : les graphiques de l'Admin (1/2) - la
//  palette, les cles de couleur, les formats francais, les echelles, le dessin
//  fin (traits anticrenneles), la legende, la bulle, la base commune (survol,
//  clic, clavier) et TimeChart. La suite : ChartsMore.cpp.
// =============================================================================
#include "Charts.hpp"

#include "../Icons.hpp"
#include "../Shapes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace ui::charts {

using gfx::FontId;
using gfx::Point;
using gfx::Rect;

namespace detail {
FontId fontAxis() { return FontId{11}; }
float tw(std::string_view s, FontId f) { return s.empty() ? 0.f : ui::measureWidth(s, f); }
float lh(FontId f) { return ui::lineHeight(f); }
// Le texte centre verticalement sur `cy`, aligne : -1 a gauche de x (x est le debut), 0 centre, 1 x est la fin.
void text(gfx::IRenderer& r, float x, float cy, std::string_view s, FontId f, Color c, int align) {
    if (s.empty()) return;
    const float w = r.measure(s, f).width;
    const float h = r.lineHeight(f);
    const float x0 = align < 0 ? x : align == 0 ? x - w * 0.5f : x - w;
    r.drawText({std::round(x0), std::round(cy - h * 0.5f)}, s, f, c);
}
std::string fit(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t.empty() ? std::string() : t + "\xE2\x80\xA6";
}
} // namespace detail

using detail::fit;
using detail::lh;
using detail::text;
using detail::tw;

// =============================================================================
//  1. LES COULEURS
// =============================================================================
namespace colors {
namespace {
// La suite au-dela des 4 admins et des cles inconnues : des teintes moyennes, distinctes, aucune d'etat.
constexpr Color kMore[] = {Color::rgb(0x2A78D6), Color::rgb(0x1BAF7A), Color::rgb(0x6250D6), Color::rgb(0xEDA100),
                           Color::rgb(0x0EA5B7), Color::rgb(0xA855F7), Color::rgb(0x64748B), Color::rgb(0xE87BA4)};

std::string fold(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {   // e accent aigu, grave, circonflexe : e (Decouverte)
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0xA8 && d <= 0xAB) { out += 'E'; ++i; continue; }
            if (d >= 0x88 && d <= 0x8B) { out += 'E'; ++i; continue; }
        }
        if (std::isalnum(c)) out += static_cast<char>(std::toupper(c));
    }
    return out;
}
float lin(std::uint8_t v) {
    const float c = static_cast<float>(v) / 255.f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float luminance(Color c) { return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b); }
} // namespace

Color admin(int order) noexcept {
    if (order >= 0 && order < 4) return kAdmins[order];
    const int n = static_cast<int>(sizeof(kMore) / sizeof(kMore[0]));
    return kMore[((order < 0 ? -order : order) - 4 + n * 4) % n];
}

Color offer(std::string_view code) noexcept {
    const std::string f = fold(code);
    if (f == "PERSONAL") return kPersonal;
    if (f == "PROFESSIONAL") return kProfessional;
    if (f == "ENTERPRISE") return kEnterprise;
    if (f == "OPTIONS" || f == "OPTION") return kOptions;
    if (f == "DECOUVERTE" || f == "DISCOVERY" || f == "ESSAI") return kDiscovery;
    return kCompare;
}

Color criticality(int level) noexcept { return kCriticality[std::clamp(level, 0, 3)]; }

int sequentialStep(double value, double max) noexcept {
    if (!(value > 0.0) || !(max > 0.0)) return 0;
    const double t = std::min(1.0, value / max);
    return 1 + std::clamp(static_cast<int>(std::floor(t * 6.0 - 1e-9)), 0, 5);
}

Color sequential(double value, double max) noexcept { return kSequential[sequentialStep(value, max)]; }

Color ordinal(int index, int count) noexcept {
    if (count <= 0 || index < 0) return kCompare;
    // Les plus recentes : les plus foncees ; au-dela de 4, les plus anciennes en gris.
    const int fromNewest = count - 1 - index;
    if (fromNewest >= 4) return kCompare;
    const int shown = std::min(count, 4);
    return kOrdinal[4 - shown + (shown - 1 - fromNewest)];
}

Color mix(Color a, Color b, float t) noexcept {
    t = std::clamp(t, 0.f, 1.f);
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

Ink ink(const Theme& theme) noexcept {
    Ink k;
    if (!theme.isDark()) {
        // La maquette, a l'octet pres (ds.css, extra.css, stats.js).
        k.ink = Color::rgb(0x0F172A);
        k.ink2 = Color::rgb(0x334155);
        k.muted = Color::rgb(0x64748B);
        k.faint = Color::rgb(0x94A3B8);
        k.grid = Color::rgb(0xEEF1F5);
        k.base = Color::rgb(0xD5DCE6);
        k.hover = Color::rgb(0xF1F4F8);
        k.rule = Color::rgb(0x94A3B8);
        k.surface = theme.brand.card.a ? theme.brand.card : Color::rgb(0xFFFFFF);
        k.line = Color::rgb(0xE3E8EF);
        k.line2 = Color::rgb(0xD5DCE6);
        k.accent = theme.color.accent;
        k.accentInk = theme.color.accentPressed;
        k.track = Color::rgb(0xE6EBF2);
        k.dark = false;
        return k;
    }
    const auto& c = theme.color;
    k.ink = c.text;
    k.ink2 = mix(c.text, c.textMuted, 0.3f);
    k.muted = c.textMuted;
    k.faint = c.textDisabled;
    k.surface = theme.brand.card.a ? theme.brand.card : c.panelBg;
    k.grid = mix(k.surface, c.text, 0.08f);
    k.base = mix(k.surface, c.text, 0.2f);
    k.hover = mix(k.surface, c.text, 0.06f);
    k.rule = c.textDisabled;
    k.line = c.border;
    k.line2 = c.borderStrong;
    k.accent = c.accent;
    k.accentInk = mix(c.accent, c.text, 0.3f);
    k.track = mix(k.surface, c.text, 0.12f);
    k.dark = true;
    return k;
}

Color onTheme(Color c, const Ink& k) noexcept {
    if (!k.dark) return c;
    const float l = luminance(c);
    if (l < 0.22f) return mix(c, Color::rgb(0xFFFFFF), std::clamp(0.45f - l, 0.15f, 0.4f));
    return c;
}
} // namespace colors

// =============================================================================
//  2. ColorKeys
// =============================================================================
ColorKeys::ColorKeys(std::vector<std::string> domain, std::vector<Color> palette) { setDomain(std::move(domain), std::move(palette)); }

void ColorKeys::setDomain(std::vector<std::string> domain, std::vector<Color> palette) {
    domain_ = std::move(domain);
    palette_ = std::move(palette);
}

void ColorKeys::pin(std::string key, Color c) {
    for (auto& [k, v] : pinned_)
        if (k == key) {
            v = c;
            return;
        }
    pinned_.emplace_back(std::move(key), c);
}

int ColorKeys::indexOf(std::string_view key) const {
    for (std::size_t i = 0; i < domain_.size(); ++i)
        if (domain_[i] == key) return static_cast<int>(i);
    return -1;
}

bool ColorKeys::knows(std::string_view key) const {
    for (const auto& [k, v] : pinned_)
        if (k == key) return true;
    return indexOf(key) >= 0;
}

Color ColorKeys::byHash(std::string_view key) {
    // FNV-1a : la meme cle, la meme couleur, d'un lancement a l'autre.
    std::uint32_t h = 2166136261u;
    for (const char ch : key) {
        h ^= static_cast<unsigned char>(ch);
        h *= 16777619u;
    }
    return colors::admin(4 + static_cast<int>(h % 8u));
}

Color ColorKeys::colorOf(std::string_view key) const {
    for (const auto& [k, v] : pinned_)
        if (k == key) return v;
    const int i = indexOf(key);
    if (i >= 0) {
        if (!palette_.empty()) return palette_[static_cast<std::size_t>(i) % palette_.size()];
        return colors::admin(i);
    }
    if (this != &standard() && standard().knows(key)) return standard().colorOf(key);
    return byHash(key);
}

const ColorKeys& ColorKeys::standard() {
    static const ColorKeys keys = [] {
        ColorKeys k;
        const char* names[4] = {"camille", "lea", "hugo", "nina"};
        for (int i = 0; i < 4; ++i) {
            k.pin("admin:" + std::to_string(i), colors::kAdmins[i]);
            k.pin(std::string("admin:") + names[i], colors::kAdmins[i]);
        }
        k.pin("offre:PERSONAL", colors::kPersonal);
        k.pin("offre:PROFESSIONAL", colors::kProfessional);
        k.pin("offre:ENTERPRISE", colors::kEnterprise);
        k.pin("offre:OPTIONS", colors::kOptions);
        k.pin("offre:DECOUVERTE", colors::kDiscovery);
        for (int i = 0; i < 4; ++i) k.pin("crit:" + std::to_string(i), colors::kCriticality[i]);
        k.pin("cmp", colors::kCompare);
        k.pin("autres", colors::kOthers);
        k.pin("un", colors::kOne);
        k.pin("deux", colors::kTwo);
        k.pin("up:0", colors::kUp[0]);
        k.pin("up:1", colors::kUp[1]);
        k.pin("down:0", colors::kDown[0]);
        k.pin("down:1", colors::kDown[1]);
        return k;
    }();
    return keys;
}

// =============================================================================
//  3. LES FORMATS FRANCAIS
// =============================================================================
namespace fr {
namespace {
std::string groupDigits(unsigned long long n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += kThin;
        out += d[i];
    }
    return out;
}
std::string signOf(double v) { return v < 0 ? std::string(kMinus) : std::string(); }
// v >= 0, `digits` chiffres apres la virgule au plus, sans zeros de fin.
std::string decimalAbs(double v, int digits) {
    digits = std::clamp(digits, 0, 6);
    const double p = std::pow(10.0, digits);
    const double r = std::round(v * p) / p;
    const double ip = std::floor(r + 1e-9);
    unsigned long long frac = static_cast<unsigned long long>(std::llround((r - ip) * p));
    if (static_cast<double>(frac) >= p) frac = 0;
    std::string out = groupDigits(static_cast<unsigned long long>(ip));
    if (digits > 0 && frac > 0) {
        std::string f = std::to_string(frac);
        while (static_cast<int>(f.size()) < digits) f = "0" + f;
        while (!f.empty() && f.back() == '0') f.pop_back();
        if (!f.empty()) out += "," + f;
    }
    return out;
}
const char* const kMonthsShort[12] = {"janv.", "f\xC3\xA9vr.", "mars", "avr.", "mai", "juin", "juil.", "ao\xC3\xBBt", "sept.", "oct.", "nov.", "d\xC3\xA9" "c."};
const char* const kMonthsLong[12] = {"janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet", "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre"};
const char* const kDays[7] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}
} // namespace

std::string integer(double v) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const double r = std::round(v);
    if (r == 0.0) return "0";
    return signOf(r) + groupDigits(static_cast<unsigned long long>(std::fabs(r)));
}

std::string decimal(double v, int digits) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const std::string a = decimalAbs(std::fabs(v), digits);
    return (a == "0" ? std::string() : signOf(v)) + a;
}

std::string euros(double v) { return integer(v) + kNbsp + kEuro; }

std::string eurosCents(double v) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const double r = std::round(std::fabs(v) * 100.0) / 100.0;
    const double ip = std::floor(r + 1e-9);
    const int c = static_cast<int>(std::llround((r - ip) * 100.0)) % 100;
    return (r == 0.0 ? std::string() : signOf(v)) + groupDigits(static_cast<unsigned long long>(ip)) + "," + two(c) + kNbsp + kEuro;
}

std::string compact(double v) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    const double a = std::fabs(v);
    struct U { double f; const char* s; };
    const U units[] = {{1e9, "Md"}, {1e6, "M"}, {1e3, "k"}};
    for (const auto& u : units) {
        if (a >= u.f) {
            const double q = a / u.f;
            const int digits = q < 10.0 ? 1 : 0;
            return signOf(v) + decimalAbs(q, digits) + kThin + u.s;
        }
    }
    return decimal(v, a < 10.0 ? 1 : 0);
}

std::string eurosCompact(double v) {
    const double a = std::fabs(v);
    if (a < 1000.0) return integer(v) + kNbsp + kEuro;
    std::string c = compact(v);   // "1,5 k" -> "1,5 kEUR"
    return c + kEuro;
}

std::string percent(double v, int digits) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    return decimal(v, digits) + kThin + "%";
}

std::string signedPercent(double v) {
    const double r = std::round(v);
    if (r == 0.0) return "=";
    return std::string(r > 0 ? "+" : kMinus) + groupDigits(static_cast<unsigned long long>(std::fabs(r))) + kThin + "%";
}

std::string signedInteger(double v) {
    const double r = std::round(v);
    if (r == 0.0) return "=";
    return std::string(r > 0 ? "+" : kMinus) + groupDigits(static_cast<unsigned long long>(std::fabs(r)));
}

std::string signedPoints(double v) {
    const double r = std::round(v);
    if (r == 0.0) return "=";
    const double a = std::fabs(r);
    return std::string(r > 0 ? "+" : kMinus) + groupDigits(static_cast<unsigned long long>(a)) + kNbsp + (a > 1 ? "pts" : "pt");
}

std::string deltaPercent(double cur, double prev) {
    if (!(std::fabs(prev) > 1e-12) || !std::isfinite(cur) || !std::isfinite(prev)) return {};
    return signedPercent((cur - prev) * 100.0 / std::fabs(prev));
}

std::string hours(double h) {
    if (!std::isfinite(h)) return "\xE2\x80\x94";
    const std::string sg = h < 0 ? std::string(kMinus) : std::string();
    long long minutes = std::llround(std::fabs(h) * 60.0);
    if (minutes < 60) return sg + std::to_string(minutes) + kNbsp + "min";
    if (minutes < 24 * 60) {
        const long long hh = minutes / 60, mm = minutes % 60;
        return sg + std::to_string(hh) + kNbsp + "h" + (mm ? std::string(kNbsp) + two(static_cast<int>(mm)) : std::string());
    }
    const long long totalH = std::llround(std::fabs(h));
    const long long dd = totalH / 24, hh = totalH % 24;
    return sg + std::to_string(dd) + kNbsp + "j" + (hh ? std::string(kNbsp) + std::to_string(hh) + kNbsp + "h" : std::string());
}

std::string clock(double h) {
    if (!std::isfinite(h)) return "\xE2\x80\x94";
    long long minutes = std::llround(h * 60.0);
    minutes = ((minutes % 1440) + 1440) % 1440;
    const long long hh = minutes / 60, mm = minutes % 60;
    return std::to_string(hh) + kNbsp + "h" + (mm ? std::string(kNbsp) + two(static_cast<int>(mm)) : std::string());
}

// Le calendrier civil (Howard Hinnant, << chrono-Compatible Low-Level Date Algorithms >>).
Day day(int y, int m, int d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

void civil(Day z, int& y, int& m, int& d) noexcept {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yy + (m <= 2 ? 1 : 0));
}

int weekday(Day d) noexcept {
    // Le 01/01/1970 etait un jeudi (3).
    const long long w = (d + 3) % 7;
    return static_cast<int>(w < 0 ? w + 7 : w);
}

Day today() noexcept { return static_cast<Day>(std::time(nullptr) / 86400); }

std::string ddmm(Day d) {
    int y, m, dd;
    civil(d, y, m, dd);
    return two(dd) + "/" + two(m);
}

std::string ddmmyyyy(Day d) {
    int y, m, dd;
    civil(d, y, m, dd);
    return two(dd) + "/" + two(m) + "/" + std::to_string(y);
}

std::string dayShort(Day d) {
    int y, m, dd;
    civil(d, y, m, dd);
    return std::to_string(dd) + kNbsp + kMonthsShort[(m + 11) % 12];
}

std::string dayLong(Day d) { return weekdayShort(weekday(d)) + " " + dayShort(d); }

std::string weekdayShort(int wd) { return kDays[((wd % 7) + 7) % 7]; }

std::string monthShort(int month) { return kMonthsShort[((month - 1) % 12 + 12) % 12]; }

std::string monthLong(int month) { return kMonthsLong[((month - 1) % 12 + 12) % 12]; }

std::string monthYear(Day d) {
    int y, m, dd;
    civil(d, y, m, dd);
    return monthShort(m) + " " + std::to_string(y);
}

std::string weekRange(Day monday) { return "Semaine du " + ddmm(monday) + " au " + ddmm(monday + 6); }
} // namespace fr

// =============================================================================
//  4. LES ECHELLES
// =============================================================================
double niceStep(double raw) noexcept {
    if (!(raw > 0.0) || !std::isfinite(raw)) return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double m = raw / p;
    const double k = m <= 1.0 + 1e-9 ? 1.0 : m <= 2.0 + 1e-9 ? 2.0 : m <= 2.5 + 1e-9 ? 2.5 : m <= 5.0 + 1e-9 ? 5.0 : 10.0;
    return k * p;
}

std::vector<double> niceTicks(double lo, double hi, int target) {
    if (!std::isfinite(lo)) lo = 0.0;
    if (!std::isfinite(hi)) hi = lo;
    if (hi < lo) std::swap(lo, hi);
    // Un seul niveau (tout a zero, un seul point) : deux graduations, la valeur et un pas rond au-dessus.
    if (!(hi > lo)) return {lo, lo + (lo == 0.0 ? 1.0 : niceStep(std::fabs(lo)))};
    target = std::max(1, target);
    const double st = niceStep((hi - lo) / target);
    const double a = std::floor(lo / st + 1e-9);
    const double b = std::ceil(hi / st - 1e-9);
    std::vector<double> t;
    for (double k = a; k <= b + 1e-9 && t.size() < 64; k += 1.0) {
        double v = k * st;
        if (std::fabs(v) < st * 1e-9) v = 0.0;
        t.push_back(v);
    }
    if (t.size() < 2) t.push_back(t.empty() ? hi : t.back() + st);
    return t;
}

std::vector<double> hourTicks(double hi, int target) {
    if (!(hi > 0.0) || !std::isfinite(hi)) hi = 1.0;
    static const double steps[] = {0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 24, 48, 72, 96, 168, 336, 720};
    target = std::max(1, target);
    double st = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (const double s : steps)
        if (hi / s <= target + 1e-9) {
            st = s;
            break;
        }
    std::vector<double> t;
    for (double v = 0.0; v <= hi + st - 1e-9 && t.size() < 64; v += st) t.push_back(v);
    if (t.size() < 2) t.push_back(st);
    return t;
}

float Linear::map(double v) const noexcept {
    const double span = d1 - d0;
    if (std::fabs(span) < 1e-300) return r0;
    return static_cast<float>(static_cast<double>(r0) + (v - d0) / span * static_cast<double>(r1 - r0));
}

double Linear::invert(float x) const noexcept {
    const float span = r1 - r0;
    if (std::fabs(span) < 1e-6f) return d0;
    return d0 + static_cast<double>((x - r0) / span) * (d1 - d0);
}

int Band::indexAt(float x) const noexcept {
    if (n <= 0 || x < r0 || x >= r1) return -1;
    return std::clamp(static_cast<int>((x - r0) / slot()), 0, n - 1);
}

int nearestIndex(float x, float r0, float r1, int n) noexcept {
    if (n <= 0) return -1;
    if (n == 1 || r1 <= r0) return 0;
    const float t = (x - r0) / (r1 - r0) * static_cast<float>(n - 1);
    return std::clamp(static_cast<int>(std::lround(t)), 0, n - 1);
}

double median(std::vector<double> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return kNone;
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) * 0.5;
}

double mean(const std::vector<double>& v) {
    double s = 0;
    std::size_t n = 0;
    for (const double x : v)
        if (std::isfinite(x)) {
            s += x;
            ++n;
        }
    return n ? s / static_cast<double>(n) : kNone;
}

// =============================================================================
//  5. LES AXES TOUT FAITS, LA BULLE A PLAT
// =============================================================================
std::vector<Category> dailyAxis(fr::Day first, int count, bool shadeWeekends) {
    std::vector<Category> out;
    for (int i = 0; i < count; ++i) {
        const fr::Day d = first + i;
        out.push_back({fr::ddmm(d), fr::dayLong(d), shadeWeekends && fr::weekday(d) >= 5});
    }
    return out;
}

std::vector<Category> weeklyAxis(fr::Day firstMonday, int count) {
    std::vector<Category> out;
    for (int i = 0; i < count; ++i) {
        const fr::Day d = firstMonday + 7 * i;
        out.push_back({fr::ddmm(d), fr::weekRange(d), false});
    }
    return out;
}

std::vector<Category> monthlyAxis(int year, int month, int count) {
    std::vector<Category> out;
    for (int i = 0; i < count; ++i) {
        const int m0 = month - 1 + i;
        const int y = year + m0 / 12, m = m0 % 12 + 1;
        out.push_back({fr::monthShort(m), fr::monthLong(m) + " " + std::to_string(y), false});
    }
    return out;
}

std::vector<Category> labelAxis(const std::vector<std::string>& labels) {
    std::vector<Category> out;
    for (const auto& l : labels) out.push_back({l, {}, false});
    return out;
}

std::string Tip::text() const {
    std::string out = title;
    for (const auto& r : rows) {
        if (!out.empty()) out += "\n";
        out += r.value;
        if (!r.name.empty()) out += " " + r.name;
    }
    if (!extra.empty()) out += (out.empty() ? "" : "\n") + extra;
    if (!click.empty()) out += (out.empty() ? "" : "\n") + click;
    return out;
}

// =============================================================================
//  6. LE DESSIN FIN
// =============================================================================
namespace {
struct V2 {
    float x, y;
};
V2 sub(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
V2 norm(V2 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y);
    return l > 1e-6f ? V2{v.x / l, v.y / l} : V2{0.f, 0.f};
}

void quad(std::vector<gfx::Vertex>& out, Point a, Color ca, Point b, Color cb, Point c, Color cc, Point d, Color cd) {
    out.push_back({a, ca});
    out.push_back({b, cb});
    out.push_back({c, cc});
    out.push_back({a, ca});
    out.push_back({c, cc});
    out.push_back({d, cd});
}

void strokeSolid(gfx::IRenderer& r, const std::vector<Point>& pts, Color c, float width) {
    if (pts.size() < 2) return;
    const std::size_t n = pts.size();
    std::vector<V2> nrm(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const V2 d = norm(sub(pts[i + 1], pts[i]));
        nrm[i] = {-d.y, d.x};
    }
    // La normale de chaque sommet (l'onglet), bornee : un pic aigu ne fait pas de pointe infinie.
    std::vector<V2> m(n);
    std::vector<float> scale(n, 1.f);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) m[i] = nrm[0];
        else if (i == n - 1) m[i] = nrm[n - 2];
        else {
            V2 s{nrm[i - 1].x + nrm[i].x, nrm[i - 1].y + nrm[i].y};
            s = norm(s);
            if (s.x == 0.f && s.y == 0.f) s = nrm[i];
            const float dt = s.x * nrm[i].x + s.y * nrm[i].y;
            m[i] = s;
            scale[i] = dt > 0.3f ? 1.f / dt : 1.f / 0.3f;
        }
    }
    const float hw = std::max(0.5f, width * 0.5f);
    const float core = std::max(0.f, hw - 0.5f), outer = hw + 0.5f;
    const Color clear = c.withAlpha(0);
    std::vector<gfx::Vertex> v;
    v.reserve((n - 1) * 18);
    const auto at = [&](std::size_t i, float off) { return Point{pts[i].x + m[i].x * off * scale[i], pts[i].y + m[i].y * off * scale[i]}; };
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const std::size_t j = i + 1;
        const Point li = at(i, core), ri = at(i, -core), lj = at(j, core), rj = at(j, -core);
        const Point lo = at(i, outer), ro = at(i, -outer), loj = at(j, outer), roj = at(j, -outer);
        if (core > 0.f) quad(v, li, c, lj, c, rj, c, ri, c);
        quad(v, lo, clear, loj, clear, lj, c, li, c);
        quad(v, ri, c, rj, c, roj, clear, ro, clear);
    }
    r.fillTriangles(v.data(), v.size());
}
} // namespace

void strokePath(gfx::IRenderer& r, const std::vector<Point>& pts, Color c, float width, bool dashed) {
    if (pts.size() < 2) return;
    if (!dashed) {
        strokeSolid(r, pts, c, width);
        return;
    }
    // Des tirets de 5 px, des vides de 4 px, le long du trace.
    const float dash = 5.f, gap = 4.f;
    std::vector<Point> cur;
    float phase = 0.f;
    bool on = true;
    cur.push_back(pts[0]);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        Point a = pts[i];
        const Point b = pts[i + 1];
        float len = std::hypot(b.x - a.x, b.y - a.y);
        while (len > 1e-3f) {
            const float left = (on ? dash : gap) - phase;
            if (len < left) {
                phase += len;
                if (on) cur.push_back(b);
                len = 0.f;
            } else {
                const float t = left / len;
                const Point p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                if (on) {
                    cur.push_back(p);
                    strokeSolid(r, cur, c, width);
                    cur.clear();
                } else {
                    cur.push_back(p);
                }
                on = !on;
                phase = 0.f;
                a = p;
                len = std::hypot(b.x - a.x, b.y - a.y);
            }
        }
    }
    if (on && cur.size() >= 2) strokeSolid(r, cur, c, width);
}

void fillUnder(gfx::IRenderer& r, const std::vector<Point>& pts, float baseY, Color c) {
    if (pts.size() < 2) return;
    std::vector<gfx::Vertex> v;
    v.reserve((pts.size() - 1) * 6);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Point a = pts[i], b = pts[i + 1];
        quad(v, a, c, b, c, {b.x, baseY}, c, {a.x, baseY}, c);
    }
    r.fillTriangles(v.data(), v.size());
}

void fillDisc(gfx::IRenderer& r, Point o, float radius, Color c) {
    if (radius <= 0.f) return;
    const int seg = std::clamp(static_cast<int>(radius * 5.f), 12, 64);
    const float core = std::max(0.f, radius - 0.5f), outer = radius + 0.5f;
    const Color clear = c.withAlpha(0);
    std::vector<gfx::Vertex> v;
    v.reserve(static_cast<std::size_t>(seg) * 9);
    constexpr float kTau = 6.2831853f;
    for (int i = 0; i < seg; ++i) {
        const float a0 = kTau * static_cast<float>(i) / static_cast<float>(seg), a1 = kTau * static_cast<float>(i + 1) / static_cast<float>(seg);
        const Point c0{o.x + std::cos(a0) * core, o.y + std::sin(a0) * core}, c1{o.x + std::cos(a1) * core, o.y + std::sin(a1) * core};
        const Point e0{o.x + std::cos(a0) * outer, o.y + std::sin(a0) * outer}, e1{o.x + std::cos(a1) * outer, o.y + std::sin(a1) * outer};
        v.push_back({o, c});
        v.push_back({c0, c});
        v.push_back({c1, c});
        quad(v, c0, c, e0, clear, e1, clear, c1, c);
    }
    r.fillTriangles(v.data(), v.size());
}

void paintDot(gfx::IRenderer& r, Point centre, float radius, Color c, Color ring) {
    fillDisc(r, centre, radius + 2.f, ring);
    fillDisc(r, centre, radius, c);
}

void paintColumn(gfx::IRenderer& r, const Rect& box, Color c, float radius, bool up, bool horizontal) {
    if (box.w <= 0.f || box.h <= 0.f) return;
    const float rad = std::min({radius, horizontal ? box.h * 0.5f : box.w * 0.5f, horizontal ? box.w : box.h});
    if (rad <= 1.f) {
        r.fillRect(box, c);
        return;
    }
    if (horizontal) {
        // Le bout a droite arrondi, la base (a gauche) carree.
        r.fillRoundedRect(box, c, rad);
        r.fillRect({box.x, box.y, std::max(0.f, box.w - rad), box.h}, c);
        return;
    }
    r.fillRoundedRect(box, c, rad);
    if (up) r.fillRect({box.x, box.y + rad, box.w, std::max(0.f, box.h - rad)}, c);
    else r.fillRect({box.x, box.y, box.w, std::max(0.f, box.h - rad)}, c);
}

void paintInlineBar(gfx::IRenderer& r, const Theme& theme, const Rect& box, double fraction, Color color) {
    const colors::Ink k = colors::ink(theme);
    const float rad = box.h * 0.5f;
    r.fillRoundedRect(box, k.track, rad);
    const double f = std::clamp(std::isfinite(fraction) ? fraction : 0.0, 0.0, 1.0);
    if (f <= 0.0) return;
    const float w = std::max(box.h, static_cast<float>(box.w * f));
    r.fillRoundedRect({box.x, box.y, w, box.h}, colors::onTheme(color, k), rad);
}

void paintSegments(gfx::IRenderer& r, const Theme& theme, const Rect& box, const std::vector<double>& values, const std::vector<Color>& cols) {
    const colors::Ink k = colors::ink(theme);
    double total = 0;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (std::isfinite(values[i]) && values[i] > 0) {
            total += values[i];
            idx.push_back(i);
        }
    if (total <= 0 || box.w <= 0.f) return;
    const float gap = 2.f;
    const float avail = box.w - gap * static_cast<float>(idx.size() - 1);
    const float rad = std::min(4.f, box.h * 0.5f);
    float x = box.x;
    for (std::size_t j = 0; j < idx.size(); ++j) {
        const std::size_t i = idx[j];
        float w = avail * static_cast<float>(values[i] / total);
        if (j + 1 == idx.size()) w = box.right() - x;
        const Color c = colors::onTheme(i < cols.size() ? cols[i] : colors::kCompare, k);
        const Rect seg{x, box.y, std::max(1.f, w), box.h};
        const bool first = j == 0, last = j + 1 == idx.size();
        if ((first || last) && seg.w > rad * 2.f) {
            r.fillRoundedRect(seg, c, rad);
            if (!first) r.fillRect({seg.x, seg.y, rad, seg.h}, c);
            if (!last) r.fillRect({seg.right() - rad, seg.y, rad, seg.h}, c);
        } else {
            r.fillRect(seg, c);
        }
        x += w + gap;
    }
}

// ---- la legende ----
namespace {
FontId legendFont() { return FontId{12}; }
float legendItemWidth(const LegendItem& it) {
    const float sw = it.swatch == Swatch::Line ? 12.f : 9.f;
    float w = sw + 5.f + tw(it.label, legendFont());
    if (!it.value.empty()) w += 4.f + tw(it.value, gfx::semibold(legendFont()));
    return w;
}
void paintSwatch(gfx::IRenderer& r, Swatch s, float x, float cy, Color c, const colors::Ink& k) {
    switch (s) {
        case Swatch::Line: r.fillRoundedRect({x, cy - 1.f, 12.f, 2.f}, c, 1.f); break;
        case Swatch::Ring: shapes::strokeRoundedRect(r, {x - 1.f, cy - 6.f, 12.f, 12.f}, c, 3.f, 2.f); break;
        case Swatch::Dot: paintDot(r, {x + 4.5f, cy}, 3.5f, c, k.surface); break;
        case Swatch::Box: r.fillRoundedRect({x, cy - 4.5f, 9.f, 9.f}, c, 2.5f); break;
    }
}
} // namespace

float legendHeight(const std::vector<LegendItem>& items, float width) {
    if (items.empty()) return 0.f;
    float x = 0.f;
    int rows = 1;
    for (const auto& it : items) {
        const float w = legendItemWidth(it);
        if (x > 0.f && x + w > width) {
            ++rows;
            x = 0.f;
        }
        x += w + 12.f;
    }
    return static_cast<float>(rows) * 17.f;
}

float paintLegend(gfx::IRenderer& r, const Theme& theme, float x0, float y0, float width, const std::vector<LegendItem>& items) {
    if (items.empty()) return 0.f;
    const colors::Ink k = colors::ink(theme);
    float x = x0, y = y0;
    for (const auto& it : items) {
        const float w = legendItemWidth(it);
        if (x > x0 && x + w > x0 + width) {
            x = x0;
            y += 17.f;
        }
        const float cy = y + 8.5f;
        paintSwatch(r, it.swatch, x, cy, colors::onTheme(it.color, k), k);
        float tx = x + (it.swatch == Swatch::Line ? 12.f : 9.f) + 5.f;
        text(r, tx, cy, it.label, legendFont(), k.ink2);
        tx += r.measure(it.label, legendFont()).width;
        if (!it.value.empty()) text(r, tx + 4.f, cy, it.value, gfx::semibold(legendFont()), k.ink);
        x += w + 12.f;
    }
    return y + 17.f - y0;
}

// ---- la bulle ----
namespace {
FontId tipTitleFont() { return gfx::semibold(FontId{11}); }
FontId tipValueFont() { return gfx::semibold(FontId{12}); }
FontId tipTextFont() { return FontId{12}; }
float tipValueColumn(const Tip& t) {
    float w = 0.f;
    for (const auto& r : t.rows) w = std::max(w, tw(r.value, tipValueFont()));
    return std::max(w, 36.f);
}
} // namespace

gfx::Size tipSize(const Tip& t) {
    if (t.empty()) return {0.f, 0.f};
    float w = tw(t.title, tipTitleFont());
    float h = 8.f;
    if (!t.title.empty()) h += 17.f;
    const float vc = tipValueColumn(t);
    for (const auto& r : t.rows) {
        float rw = vc + 7.f;
        if (r.color.a) rw += 12.f + 7.f;
        rw += tw(r.name, tipTextFont());
        w = std::max(w, rw);
        h += 18.f;
    }
    if (!t.extra.empty()) {
        w = std::max(w, tw(t.extra, tipTextFont()));
        h += 22.f;
    }
    if (!t.click.empty()) {
        w = std::max(w, 19.f + tw(t.click, gfx::semibold(tipTextFont())));
        h += 24.f;
    }
    return {std::max(150.f, w + 22.f), h + 6.f};
}

Rect placeTip(const Tip& t, const Rect& anchor, const Rect& limit) {
    const gfx::Size s = tipSize(t);
    float x = anchor.right() + 10.f;
    if (x + s.w > limit.right() - 4.f) x = anchor.x - 10.f - s.w;
    if (x < limit.x + 4.f) x = std::max(limit.x + 4.f, std::min(anchor.right() + 10.f, limit.right() - 4.f - s.w));
    float y = anchor.y;
    if (y + s.h > limit.bottom() - 4.f) y = limit.bottom() - 4.f - s.h;
    if (y < limit.y + 4.f) y = limit.y + 4.f;
    return {std::round(x), std::round(y), s.w, s.h};
}

void paintTip(gfx::IRenderer& r, const Theme& theme, const Tip& t, const Rect& box) {
    if (t.empty() || box.empty()) return;
    const colors::Ink k = colors::ink(theme);
    // L'ombre, a la main (le rendu ne floute pas) : deux voiles tres legers sous la bulle.
    r.fillRoundedRect({box.x - 1.f, box.y + 3.f, box.w + 2.f, box.h + 4.f}, Color{15, 23, 42, static_cast<std::uint8_t>(k.dark ? 60 : 14)}, 12.f);
    r.fillRoundedRect({box.x, box.y + 1.f, box.w, box.h + 1.f}, Color{15, 23, 42, static_cast<std::uint8_t>(k.dark ? 50 : 12)}, 11.f);
    shapes::fillFramedRect(r, box, k.surface, k.line2, 10.f);
    const float x = box.x + 11.f, right = box.right() - 11.f;
    float y = box.y + 8.f;
    if (!t.title.empty()) {
        text(r, x, y + 7.f, fit(r, t.title, tipTitleFont(), right - x), tipTitleFont(), k.muted);
        y += 17.f;
    }
    const float vc = tipValueColumn(t);
    for (const auto& row : t.rows) {
        const float cy = y + 9.f;
        text(r, x, cy, row.value, tipValueFont(), k.ink);
        float nx = x + vc + 7.f;
        if (row.color.a) {
            const Color c = colors::onTheme(row.color, k);
            if (row.line) r.fillRoundedRect({nx, cy - 1.f, 12.f, 2.f}, c, 1.f);
            else r.fillRoundedRect({nx + 2.f, cy - 4.f, 8.f, 8.f}, c, 2.f);
            nx += 19.f;
        }
        text(r, nx, cy, fit(r, row.name, tipTextFont(), right - nx), tipTextFont(), k.muted);
        y += 18.f;
    }
    if (!t.extra.empty()) {
        y += 3.f;
        r.fillRect({x, y, right - x, 1.f}, k.line);
        y += 4.f;
        text(r, x, y + 7.5f, fit(r, t.extra, tipTextFont(), right - x), tipTextFont(), k.ink2);
        y += 15.f;
    }
    if (!t.click.empty()) {
        y += 4.f;
        r.fillRect({x, y, right - x, 1.f}, k.line);
        y += 6.f;
        const float cy = y + 7.5f;
        // La fleche de la souris (un petit triangle) puis << Clic : ... >>.
        const Color ci = k.accentInk;
        const gfx::Vertex arrow[3] = {{{x + 1.f, cy - 6.f}, ci}, {{x + 1.f, cy + 5.f}, ci}, {{x + 9.f, cy + 1.f}, ci}};
        r.fillTriangles(arrow, 3);
        text(r, x + 15.f, cy, fit(r, t.click, gfx::semibold(tipTextFont()), right - x - 15.f), gfx::semibold(tipTextFont()), ci);
    }
}

// ---- l'ecart ----
Delta deltaOf(double cur, double prev, bool upIsGood) {
    Delta d;
    if (!(std::fabs(prev) > 1e-12) || !std::isfinite(cur) || !std::isfinite(prev)) return d;
    const double p = std::round((cur - prev) * 100.0 / std::fabs(prev));
    d.text = fr::signedPercent(p);
    d.direction = p > 0 ? 1 : p < 0 ? -1 : 0;
    d.sense = d.direction == 0 ? 0 : ((d.direction > 0) == upIsGood ? 1 : -1);
    return d;
}

float deltaWidth(const Delta& d, FontId font) {
    if (d.text.empty()) return 0.f;
    return (d.direction ? 14.f : 0.f) + tw(d.text, gfx::semibold(font));
}

float paintDelta(gfx::IRenderer& r, const Theme& theme, float x, float y, const Delta& d, FontId font) {
    if (d.text.empty()) return 0.f;
    const colors::Ink k = colors::ink(theme);
    const Color c = d.sense > 0 ? (k.dark ? theme.color.ok : colors::kGood) : d.sense < 0 ? (k.dark ? theme.color.error : colors::kBad) : k.muted;
    const float h = r.lineHeight(font);
    const float cy = y + h * 0.5f;
    float tx = x;
    if (d.direction) {
        // La fleche de la tendance : une diagonale et sa pointe.
        const float s = d.direction > 0 ? -1.f : 1.f;
        const Point a{x + 1.f, cy - s * 4.f}, b{x + 9.f, cy + s * 4.f};
        strokePath(r, {a, b}, c, 1.6f);
        strokePath(r, {{b.x - 5.f, b.y}, b, {b.x, b.y - s * 5.f}}, c, 1.6f);
        tx += 14.f;
    }
    const FontId f = gfx::semibold(font);
    r.drawText({std::round(tx), std::round(y)}, d.text, f, c);
    return tx - x + r.measure(d.text, f).width;
}

// =============================================================================
//  7. LA BASE : le survol, le clic, le clavier, la bulle
// =============================================================================
ChartBase::ChartBase(std::string id) : Widget(std::move(id)) { setFocusPolicy(true); }

SizeHint ChartBase::sizeHint() const {
    SizeHint h;
    h.preferred = {240.f, 140.f};
    h.minimum = {80.f, 40.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void ChartBase::showTip(int index, int sub) {
    example_ = index;
    exampleSub_ = sub;
    invalidate();
}

bool ChartBase::tipShown() const noexcept {
    if (active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_))) return true;
    return example_ >= 0;
}

bool ChartBase::requestsOverlayPass() const { return visible() && tipShown(); }

void ChartBase::pick(const Pick& p) {
    if (p.index < 0) return;
    if (onPick_) onPick_(p);
}

Pick ChartBase::pickFor(int index, int sub) const {
    Pick p;
    p.index = index;
    p.series = sub;
    return p;
}

void ChartBase::onFocusChanged(bool focused) {
    if (!focused) keyboard_ = false;
    invalidate();
}

void ChartBase::paintFocus(const PaintContext& ctx, const Rect& r) const {
    if (!keyboardShown()) return;
    const colors::Ink k = colors::ink(ctx.theme);
    shapes::strokeRoundedRect(ctx.r, {r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f}, k.accent.withAlpha(160), 8.f, 2.f);
}

void ChartBase::onPaintOverlay(const PaintContext& ctx) {
    if (!tipShown()) return;
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int i = live ? active_ : example_;
    const int s = live ? activeSub_ : exampleSub_;
    if (i < 0 || i >= itemCount()) return;
    const Tip t = tipFor(i, s);
    if (t.empty()) return;
    Rect limit{0.f, 0.f, 0.f, 0.f};
    const gfx::Size ws = windowSurfaceSize();
    if (ws.w > 0.f && ws.h > 0.f) limit = {0.f, 0.f, ws.w, ws.h};
    else {
        const gfx::Size ss = surfaceSize();
        limit = ss.w > 0.f ? Rect{0.f, 0.f, ss.w, ss.h} : Rect{bounds().x - 400.f, bounds().y - 400.f, bounds().w + 800.f, bounds().h + 800.f};
    }
    paintTip(ctx.r, ctx.theme, t, placeTip(t, tipAnchor(i, s), limit));
}

EventResult ChartBase::onEvent(const InputEvent& ev) {
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const auto [i, s] = itemAt(m->pos);
        if (i != active_ || s != activeSub_ || keyboard_) {
            active_ = i;
            activeSub_ = s;
            keyboard_ = false;
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (d->button != MouseButton::Left || !bounds().contains(d->pos)) return EventResult::Ignored;
        grabFocus();
        keyboard_ = false;
        const Pick p = pickAt(d->pos);
        if (p.index < 0) return EventResult::Ignored;
        active_ = p.index;
        activeSub_ = p.series;
        pick(p);
        invalidate();
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        if (!focused() || k->mods.ctrl || k->mods.alt) return EventResult::Ignored;
        const int n = itemCount(), rows = subCount();
        if (n <= 0) return EventResult::Ignored;
        int i = active_ >= 0 ? active_ : (example_ >= 0 ? example_ : -1);
        int s = activeSub_;
        switch (k->key) {
            case Key::Left: i = i < 0 ? n - 1 : std::max(0, i - 1); break;
            case Key::Right: i = i < 0 ? 0 : std::min(n - 1, i + 1); break;
            case Key::Home: i = 0; break;
            case Key::End: i = n - 1; break;
            case Key::Up:
                if (rows <= 0) i = i < 0 ? 0 : std::max(0, i - 1);
                else s = s < 0 ? 0 : std::max(0, s - 1);
                break;
            case Key::Down:
                if (rows <= 0) i = i < 0 ? 0 : std::min(n - 1, i + 1);
                else s = s < 0 ? 0 : std::min(rows - 1, s + 1);
                break;
            case Key::Return:
            case Key::Space:
                if (i < 0) return EventResult::Ignored;
                pick(pickFor(i, s));
                keyboard_ = true;
                invalidate();
                return EventResult::Consumed;
            case Key::Escape:
                if (active_ < 0 && !keyboard_) return EventResult::Ignored;
                active_ = -1;
                activeSub_ = -1;
                keyboard_ = false;
                invalidate();
                return EventResult::Consumed;
            default: return EventResult::Ignored;
        }
        if (rows > 0 && s < 0) s = 0;
        if (i < 0) i = 0;
        active_ = i;
        activeSub_ = rows > 0 ? s : activeSub_;
        keyboard_ = true;
        invalidate();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

// =============================================================================
//  8. TimeChart
// =============================================================================
namespace {
Format defaultFormat(double hi) {
    if (std::fabs(hi) >= 100000.0) return [](double v) { return fr::compact(v); };
    return [](double v) { return std::fabs(v - std::round(v)) > 1e-9 ? fr::decimal(v, 1) : fr::integer(v); };
}
}

TimeChart::TimeChart(std::string id) : ChartBase(std::move(id)) {}

void TimeChart::setData(TimeChartData data) {
    data_ = std::move(data);
    example_ = data_.exampleTip;
    exampleSub_ = -1;
    if (active_ >= itemCount()) active_ = -1;
    invalidate();
}

void TimeChart::setColorKeys(ColorKeys keys) {
    keys_ = std::move(keys);
    ownKeys_ = true;
    invalidate();
}

void TimeChart::setSharedDomain(double lo, double hi) {
    sharedLo_ = lo;
    sharedHi_ = hi;
    invalidate();
}

bool TimeChart::isBar(const Series& s) const noexcept {
    if (s.role == Role::Compare || s.mark == Mark::Line) return false;
    return data_.kind != Kind::Line;
}

Color TimeChart::seriesColor(std::size_t i) const {
    if (i >= data_.series.size()) return colors::kCompare;
    const Series& s = data_.series[i];
    if (s.role == Role::Compare) return s.color.a ? s.color : colors::kCompare;
    if (s.color.a) return s.color;
    if (!s.key.empty()) return ownKeys_ ? keys_.colorOf(s.key) : ColorKeys::standard().colorOf(s.key);
    // Sans cle : l'ordre des series principales (une mesure : bleu ; deux : bleu puis orange ; la suite).
    int rank = 0;
    for (std::size_t j = 0; j < i; ++j)
        if (data_.series[j].role == Role::Main && data_.series[j].key.empty() && !data_.series[j].color.a) ++rank;
    if (rank == 0) return colors::kOne;
    if (rank == 1) return colors::kTwo;
    return colors::admin(4 + rank);
}

std::pair<double, double> TimeChart::extent() const {
    const auto& d = data_;
    const int n = static_cast<int>(d.x.size());
    double lo = 0.0, hi = 0.0;
    bool any = false;
    double minLine = std::numeric_limits<double>::infinity();
    if (d.kind == Kind::Stacked || d.kind == Kind::Diverging) {
        for (int i = 0; i < n; ++i) {
            double up = 0, dn = 0;
            for (const auto& s : d.series) {
                if (!isBar(s) || static_cast<std::size_t>(i) >= s.values.size()) continue;
                const double v = s.values[static_cast<std::size_t>(i)];
                if (!std::isfinite(v)) continue;
                any = true;
                if (s.below) dn += std::fabs(v);
                else up += v;
            }
            hi = std::max(hi, up);
            if (d.kind == Kind::Diverging) lo = std::min(lo, -dn);
        }
    } else if (d.kind == Kind::Range) {
        lo = std::numeric_limits<double>::infinity();
        hi = -lo;
        for (const auto& s : d.series)
            for (const double v : s.values)
                if (std::isfinite(v)) {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                    any = true;
                }
        if (!any) lo = hi = 0.0;
    } else {
        for (const auto& s : d.series) {
            if (!isBar(s)) continue;
            for (const double v : s.values)
                if (std::isfinite(v)) {
                    hi = std::max(hi, v);
                    lo = std::min(lo, v);
                    any = true;
                }
        }
    }
    for (const auto& s : d.series) {
        if (isBar(s) || d.kind == Kind::Range) continue;
        for (const double v : s.values)
            if (std::isfinite(v)) {
                hi = std::max(hi, v);
                minLine = std::min(minLine, v);
                any = true;
            }
    }
    if (std::isfinite(minLine) && minLine < lo) lo = minLine;
    for (const auto& r : d.refs) hi = std::max(hi, r.value);
    (void)any;
    return {lo, hi};
}

TimeChart::Frame TimeChart::frame() const {
    Frame f;
    const auto& d = data_;
    const Rect b = contentRect();
    f.n = static_cast<int>(d.x.size());
    bool anyBar = false;
    for (const auto& s : d.series) anyBar = anyBar || isBar(s);
    f.band = d.kind != Kind::Line || d.lineInBands || anyBar;

    // La legende en haut.
    const auto items = legendItems();
    const bool showLegend = d.legend == LegendMode::Top || (d.legend == LegendMode::Auto && items.size() >= 2);
    f.legendHeight = showLegend ? legendHeight(items, b.w) + 6.f : 0.f;

    // Le domaine des valeurs.
    auto [lo, hi] = extent();
    if (std::isfinite(sharedLo_)) lo = std::min(lo, sharedLo_);
    if (std::isfinite(sharedHi_)) hi = std::max(hi, sharedHi_);
    if (std::isfinite(d.yMin)) lo = d.yMin;
    if (std::isfinite(d.yMax)) hi = d.yMax;
    const bool flat = !(hi > lo);   // tout a zero, un seul niveau : deux graduations (niceTicks)
    if (flat) hi = lo;
    if (!d.yTicks.empty()) f.ticks = d.yTicks;
    else if (d.kind == Kind::Diverging && lo < 0.0) {
        const double st = niceStep(std::max(hi, -lo) / 2.0);
        for (double k = -std::ceil(-lo / st - 1e-9); k <= std::ceil(hi / st - 1e-9) + 1e-9; k += 1.0) f.ticks.push_back(k * st);
    } else f.ticks = niceTicks(lo, hi, d.yTickCount);
    if (!f.ticks.empty()) {
        lo = std::min(lo, f.ticks.front());
        hi = std::max(hi, f.ticks.back());
    }
    if (!(hi > lo)) hi = lo + 1.0;
    f.lo = lo;
    f.hi = hi;

    // Les marges : les libelles des valeurs a gauche, les etiquettes de fin a droite.
    const Format yf = d.yFormat ? d.yFormat : defaultFormat(hi);
    float maxY = 0.f;
    if (d.yAxis)
        for (const double t : f.ticks) maxY = std::max(maxY, tw(yf(t), detail::fontAxis()));
    const float pl = d.yAxis ? maxY + 9.f : 3.f;
    float endW = 0.f;
    for (const auto& s : d.series)
        if (!s.endLabel.empty()) endW = std::max(endW, tw(s.endLabel, gfx::semibold(detail::fontAxis())) + 12.f);
    const float pr = 5.f + endW;
    const float pb = d.xAxis ? 19.f : 5.f;
    float pt = 7.f;
    f.plot = {b.x + pl, b.y + f.legendHeight + pt, std::max(10.f, b.w - pl - pr), 0.f};
    // Les etiquettes des reperes, en rangees au-dessus du trace.
    const int rows = markerRows(f);
    if (rows > 0) pt += static_cast<float>(rows) * 15.f + 4.f;
    // Trop peu de place : la legende d'abord s'en va (le trace passe avant elle).
    if (f.legendHeight > 0.f && b.h - f.legendHeight - pt - pb < 36.f) f.legendHeight = 0.f;
    f.plot.y = b.y + f.legendHeight + pt;
    f.plot.h = std::max(4.f, b.h - f.legendHeight - pt - pb);
    // Un trace trop bas pour ses graduations (les libelles se toucheraient) : moins de graduations.
    if (d.yTicks.empty() && f.ticks.size() > 2 && f.plot.h / static_cast<float>(f.ticks.size() - 1) < 16.f) {
        const int target = std::max(1, static_cast<int>(f.plot.h / 18.f));
        auto [lo2, hi2] = extent();
        if (std::isfinite(d.yMin)) lo2 = d.yMin;
        if (std::isfinite(d.yMax)) hi2 = d.yMax;
        if (!(hi2 > lo2)) hi2 = lo2 + 1.0;
        std::vector<double> t = niceTicks(lo2, hi2, target);
        if (t.size() > 3) t = {t.front(), t.back()};
        f.ticks = t;
        f.lo = std::min(lo2, t.front());
        f.hi = std::max(hi2, t.back());
    }
    return f;
}

float TimeChart::Frame::slot() const noexcept {
    if (n <= 0) return plot.w;
    return band ? plot.w / static_cast<float>(n) : (n > 1 ? plot.w / static_cast<float>(n - 1) : plot.w);
}

float TimeChart::Frame::xAt(double index) const noexcept {
    if (band) return plot.x + static_cast<float>((index + 0.5) * static_cast<double>(n > 0 ? plot.w / static_cast<float>(n) : plot.w));
    if (n <= 1) return plot.x + plot.w * 0.5f;
    return plot.x + static_cast<float>(index / static_cast<double>(n - 1) * static_cast<double>(plot.w));
}

float TimeChart::Frame::yAt(double v) const noexcept {
    const double span = hi - lo;
    if (!(span > 0.0)) return plot.bottom();
    return plot.bottom() - static_cast<float>((v - lo) / span * static_cast<double>(plot.h));
}

int TimeChart::Frame::indexAt(float x) const noexcept {
    if (n <= 0) return -1;
    if (band) {
        const Band bd{n, plot.x, plot.right()};
        return bd.indexAt(x);
    }
    if (x < plot.x - 8.f || x > plot.right() + 8.f) return -1;
    return nearestIndex(x, plot.x, plot.right(), n);
}

float TimeChart::barWidth(const Frame& f) const {
    const float slot = f.slot();
    return std::max(1.f, std::min(data_.barMax, slot * data_.barFraction));
}

std::vector<LegendItem> TimeChart::legendItems() const {
    std::vector<LegendItem> items;
    for (std::size_t i = 0; i < data_.series.size(); ++i) {
        const Series& s = data_.series[i];
        if (s.noLegend || s.name.empty()) continue;
        if (data_.kind == Kind::Range && i > 0) continue;
        items.push_back({s.name, seriesColor(i), isBar(s) ? Swatch::Box : Swatch::Line, s.legendValue});
    }
    return items;
}

int TimeChart::markerRows(const Frame& f) const {
    // Les etiquettes des reperes : a droite du trait (ou a gauche si elles ne tiennent pas), une deuxieme rangee si
    // elles se chevauchent. Rend le nombre de rangees ; layoutMarkers donne leur place.
    return static_cast<int>(layoutMarkers(f).rows);
}

TimeChart::MarkerLayout TimeChart::layoutMarkers(const Frame& f) const {
    MarkerLayout out;
    const FontId mf = gfx::semibold(detail::fontAxis());
    std::vector<std::vector<std::pair<float, float>>> used;
    for (const auto& m : data_.markers) {
        MarkerLayout::Item it;
        if (m.label.empty()) {
            out.items.push_back(it);
            continue;
        }
        const float x = f.xAt(m.at);
        const float w = tw(m.label, mf);
        // Depuis une valeur (une distribution) : l'etiquette a gauche du trait d'abord ; pleine hauteur : a droite.
        const bool fromValue = std::isfinite(m.fromValue);
        float x0 = fromValue ? x + 4.f - w : x + 4.f;
        if (x0 < f.plot.x - 2.f) x0 = x - 4.f;
        if (x0 + w > f.plot.right() + 4.f) x0 = x - 4.f - w;
        // Trois rangees au plus : une etiquette qui ne tient nulle part n'est pas ecrite (le trait reste).
        int row = 0;
        bool placed = false;
        for (; row < 3; ++row) {
            if (static_cast<std::size_t>(row) >= used.size()) used.emplace_back();
            bool clash = false;
            for (const auto& [a, bb] : used[static_cast<std::size_t>(row)])
                if (x0 < bb + 6.f && x0 + w > a - 6.f) clash = true;
            if (!clash) {
                placed = true;
                break;
            }
        }
        if (!placed) {
            out.items.push_back(it);
            continue;
        }
        used[static_cast<std::size_t>(row)].push_back({x0, x0 + w});
        it.x = x0;
        it.row = row;
        it.shown = true;
        out.rows = std::max(out.rows, row + 1);
        out.items.push_back(it);
    }
    return out;
}

void TimeChart::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const colors::Ink k = colors::ink(ctx.theme);
    const auto& d = data_;
    const Rect b = contentRect();
    if (b.w < 20.f || b.h < 20.f) return;
    const Frame f = frame();
    const int n = f.n;
    const FontId af = detail::fontAxis();
    const Format yf = d.yFormat ? d.yFormat : defaultFormat(f.hi);
    const bool live = active_ >= 0 && ((hovered() && !keyboard_) || (focused() && keyboard_));
    const int shown = live ? active_ : (example_ >= 0 && example_ < n ? example_ : -1);

    // La legende.
    if (f.legendHeight > 0.f) paintLegend(r, ctx.theme, b.x, b.y, b.w, legendItems());
    // Aucune valeur du tout : la ligne de base et une phrase, rien d'autre (pas d'axe invente).
    bool any = false;
    for (const auto& s : d.series)
        for (const double v : s.values) any = any || std::isfinite(v);
    if (!any || n == 0) {
        r.fillRect({f.plot.x, std::round(f.plot.bottom()), f.plot.w, 1.f}, k.base);
        text(r, f.plot.x + f.plot.w * 0.5f, f.plot.y + f.plot.h * 0.5f, fit(r, d.emptyText, FontId{12}, f.plot.w), FontId{12}, k.faint, 0);
        return;
    }

    // Les bandes de fond (les fins de semaine, << pas encore admin >>).
    const float slot = f.slot();
    const Color shadeC = k.dark ? colors::mix(k.surface, k.ink, 0.04f) : Color::rgb(0xF8FAFC);
    for (int i = 0; i < n; ++i)
        if (d.x[static_cast<std::size_t>(i)].shade) r.fillRect({f.xAt(i) - slot * 0.5f, f.plot.y, slot, f.plot.h}, shadeC);
    for (const auto& s : d.shades) {
        const float x0 = f.band ? f.xAt(s.from) - slot * 0.5f : f.xAt(s.from);
        const float x1 = f.band ? f.xAt(s.to) + slot * 0.5f : f.xAt(s.to);
        r.fillRect({std::max(f.plot.x, x0), f.plot.y, std::min(f.plot.right(), x1) - std::max(f.plot.x, x0), f.plot.h}, s.color.a ? s.color : shadeC);
        if (!s.label.empty()) text(r, std::max(f.plot.x, x0) + 5.f, f.plot.y + 8.f, fit(r, s.label, FontId{10}, x1 - x0 - 8.f), FontId{10}, k.faint);
    }

    for (const auto& yb : d.yBands) {
        const float ya = f.yAt(std::max(yb.from, yb.to)), ye = f.yAt(std::min(yb.from, yb.to));
        const float top = std::max(f.plot.y, ya), bot = std::min(f.plot.bottom(), ye);
        if (bot <= top) continue;
        r.fillRect({f.plot.x, top, f.plot.w, bot - top}, yb.color.a ? yb.color : (k.dark ? colors::mix(k.surface, colors::kOne, 0.12f) : Color::rgb(0xF1F6FD)));
        if (!yb.label.empty()) text(r, f.plot.right() - 4.f, top + 8.f, yb.label, FontId{10}, k.faint, 1);
    }

    // La grille et les valeurs.
    for (const double t : f.ticks) {
        const float y = std::round(f.yAt(t));
        r.fillRect({f.plot.x, y, f.plot.w, 1.f}, std::fabs(t) < 1e-12 ? k.base : k.grid);
        if (d.yAxis) text(r, f.plot.x - 7.f, y + 0.5f, yf(t), af, k.faint, 1);
    }
    if (f.lo < 0.0 && f.hi > 0.0 && std::find(f.ticks.begin(), f.ticks.end(), 0.0) == f.ticks.end())
        r.fillRect({f.plot.x, std::round(f.yAt(0.0)), f.plot.w, 1.f}, k.base);

    // L'axe des categories.
    if (d.xAxis && n > 0) {
        std::vector<std::pair<int, int>> ticks = d.xTicks;
        if (ticks.empty()) {
            const int step = std::max(1, static_cast<int>(std::lround(static_cast<double>(n) / 4.0)));
            for (int i = 0; i < n; i += step) ticks.push_back({i, 0});
            if (!f.band && n > 1 && (n - 1) - ticks.back().first >= std::max(1, step * 6 / 10)) ticks.push_back({n - 1, 0});
            if (!f.band) {
                ticks.front().second = -1;
                if (ticks.size() > 1 && ticks.back().first == n - 1) ticks.back().second = 1;
            }
        }
        // Les libelles qui se chevauchent : le dernier gagne contre son voisin, les autres passent.
        struct Placed {
            float x0, x1;
            std::string s;
            float x;
            int a;
        };
        std::vector<Placed> placed;
        for (std::size_t j = 0; j < ticks.size(); ++j) {
            const int i = ticks[j].first;
            if (i < 0 || i >= n) continue;
            const std::string& s = d.x[static_cast<std::size_t>(i)].label;
            const float w = tw(s, af);
            const float x = f.xAt(i);
            int a = ticks[j].second;
            float x0 = a < 0 ? x : a == 0 ? x - w * 0.5f : x - w;
            if (x0 < b.x) {
                x0 = b.x;
            }
            if (x0 + w > b.right()) x0 = b.right() - w;
            const Placed p{x0, x0 + w, s, x, a};
            if (!placed.empty() && p.x0 < placed.back().x1 + 6.f) {
                if (j + 1 == ticks.size()) placed.back() = p;
                continue;
            }
            placed.push_back(p);
        }
        for (const auto& p : placed) r.drawText({std::round(p.x0), std::round(f.plot.bottom() + 5.f)}, p.s, af, k.faint);
    }

    // La colonne survolee (derriere les barres).
    if (shown >= 0 && f.band)
        r.fillRoundedRect({f.xAt(shown) - slot * 0.5f + 1.f, f.plot.y - 2.f, slot - 2.f, f.plot.h + 2.f}, k.hover, 4.f);

    // Les reperes verticaux.
    const MarkerLayout ml = layoutMarkers(f);
    const FontId mf = gfx::semibold(af);
    const float rowsTop = f.plot.y - 4.f - static_cast<float>(ml.rows) * 15.f;
    for (std::size_t j = 0; j < d.markers.size(); ++j) {
        const Marker& m = d.markers[j];
        const float x = std::round(f.xAt(m.at)) + 0.5f;
        const auto& it = j < ml.items.size() ? ml.items[j] : MarkerLayout::Item{};
        const float labelY = rowsTop + static_cast<float>(it.row) * 15.f + 7.5f;
        if (std::isfinite(m.fromValue)) {
            const float y1 = f.yAt(m.fromValue) - 3.f;
            const float y0 = it.shown ? labelY + 7.f : f.plot.y;
            if (y1 > y0) r.fillRect({x - 0.5f, y0, 1.f, y1 - y0}, m.color.a ? m.color : k.ink2);
        } else {
            const float y0 = it.shown ? labelY - 6.f : f.plot.y;
            r.fillRect({x - 0.5f, y0, 1.f, f.plot.bottom() - y0}, m.color.a ? m.color : (k.dark ? k.base : Color::rgb(0xCBD5E1)));
        }
        if (it.shown) text(r, it.x, labelY, m.label, mf, m.strong || std::isfinite(m.fromValue) ? k.ink : k.muted);
    }

    // Les barres.
    const auto barColor = [&](std::size_t si, int i) {
        const Series& s = d.series[si];
        Color c = seriesColor(si);
        if (static_cast<std::size_t>(i) < s.pointColors.size() && s.pointColors[static_cast<std::size_t>(i)].a) c = s.pointColors[static_cast<std::size_t>(i)];
        c = colors::onTheme(c, k);
        if (d.partialFrom >= 0 && i >= d.partialFrom) c = colors::mix(c, k.surface, 0.55f);
        return c;
    };
    std::vector<std::size_t> bars;
    for (std::size_t si = 0; si < d.series.size(); ++si)
        if (isBar(d.series[si])) bars.push_back(si);
    const float y0 = f.yAt(std::max(f.lo, std::min(0.0, f.hi)));
    if (!bars.empty() && n > 0) {
        if (d.kind == Kind::Range) {
            const Series& a = d.series[0];
            const Series* e = d.series.size() > 1 ? &d.series[1] : nullptr;
            const float w = std::max(2.f, std::min(7.f, slot * 0.6f));
            for (int i = 0; i < n && e; ++i) {
                const std::size_t ui = static_cast<std::size_t>(i);
                if (ui >= a.values.size() || ui >= e->values.size()) continue;
                const double lo = a.values[ui], hi = e->values[ui];
                if (!std::isfinite(lo) || !std::isfinite(hi)) continue;
                const float yt = f.yAt(std::max(lo, hi)), yb = f.yAt(std::min(lo, hi));
                const float h = std::max(4.f, yb - yt);
                r.fillRoundedRect({f.xAt(i) - w * 0.5f, yt, w, h}, barColor(0, i), w * 0.5f);
            }
        } else if (d.kind == Kind::Grouped || (d.kind == Kind::Bars && bars.size() > 1)) {
            const float kk = static_cast<float>(bars.size());
            const float bw = std::max(1.f, std::min(std::min(12.f, d.barMax), (slot * 0.74f - (kk - 1.f) * 2.f) / kk));
            for (std::size_t g = 0; g < bars.size(); ++g) {
                const Series& s = d.series[bars[g]];
                for (int i = 0; i < n; ++i) {
                    const std::size_t ui = static_cast<std::size_t>(i);
                    if (ui >= s.values.size() || !std::isfinite(s.values[ui]) || s.values[ui] == 0.0) continue;
                    const float x = f.xAt(i) - (kk * bw + (kk - 1.f) * 2.f) * 0.5f + static_cast<float>(g) * (bw + 2.f);
                    const float yv = f.yAt(s.values[ui]);
                    paintColumn(r, {x, std::min(yv, y0), bw, std::fabs(y0 - yv)}, barColor(bars[g], i), 4.f, yv <= y0);
                }
            }
        } else if (d.kind == Kind::Bars) {
            const Series& s = d.series[bars[0]];
            const float bw = barWidth(f);
            for (int i = 0; i < n; ++i) {
                const std::size_t ui = static_cast<std::size_t>(i);
                if (ui >= s.values.size() || !std::isfinite(s.values[ui]) || s.values[ui] == 0.0) continue;
                const float yv = f.yAt(s.values[ui]);
                paintColumn(r, {f.xAt(i) - bw * 0.5f, std::min(yv, y0), bw, std::fabs(y0 - yv)}, barColor(bars[0], i), 4.f, yv <= y0);
            }
        } else {
            // Empilees (et divergentes) : de la base vers le bout, 2 px de surface entre deux morceaux, le dernier arrondi.
            const float bw = barWidth(f);
            for (int i = 0; i < n; ++i) {
                const std::size_t ui = static_cast<std::size_t>(i);
                for (int dir = 0; dir < 2; ++dir) {
                    const bool down = dir == 1;
                    std::vector<std::size_t> parts;
                    for (const std::size_t si : bars) {
                        const Series& s = d.series[si];
                        if (s.below != down || ui >= s.values.size()) continue;
                        const double v = s.values[ui];
                        if (std::isfinite(v) && std::fabs(v) > 0.0) parts.push_back(si);
                    }
                    double base = 0.0;
                    for (std::size_t pj = 0; pj < parts.size(); ++pj) {
                        const double q = std::fabs(d.series[parts[pj]].values[ui]);
                        const bool last = pj + 1 == parts.size();
                        const float gap0 = pj ? 1.f : 0.f, gap1 = last ? 0.f : 1.f;
                        float ya, yb;
                        if (!down) {
                            yb = f.yAt(base) - gap0;
                            ya = f.yAt(base + q) + gap1;
                        } else {
                            yb = f.yAt(-base) + gap0;
                            ya = f.yAt(-(base + q)) - gap1;
                        }
                        const float top = std::min(ya, yb), h = std::fabs(yb - ya);
                        if (h >= 0.4f) paintColumn(r, {f.xAt(i) - bw * 0.5f, top, bw, h}, barColor(parts[pj], i), last ? 4.f : 0.f, !down);
                        base += q;
                    }
                }
            }
        }
    }

    // Les courbes : la comparaison d'abord (dessous).
    std::vector<std::size_t> lines;
    for (std::size_t si = 0; si < d.series.size(); ++si)
        if (!isBar(d.series[si]) && d.kind != Kind::Range && d.series[si].role == Role::Compare) lines.push_back(si);
    for (std::size_t si = 0; si < d.series.size(); ++si)
        if (!isBar(d.series[si]) && d.kind != Kind::Range && d.series[si].role != Role::Compare) lines.push_back(si);
    const float areaBase = f.yAt(std::max(f.lo, 0.0));
    for (const std::size_t si : lines) {
        const Series& s = d.series[si];
        const Color c = colors::onTheme(seriesColor(si), k);
        const float w = s.width > 0.f ? s.width : (s.role == Role::Compare ? 1.5f : 2.f);
        std::vector<std::vector<Point>> segs(1);
        for (int i = 0; i < n && static_cast<std::size_t>(i) < s.values.size(); ++i) {
            const double v = s.values[static_cast<std::size_t>(i)];
            if (!std::isfinite(v)) {
                if (!segs.back().empty()) segs.emplace_back();
                continue;
            }
            const Point p{f.xAt(i), f.yAt(v)};
            if (s.step && !segs.back().empty()) segs.back().push_back({p.x, segs.back().back().y});
            segs.back().push_back(p);
        }
        for (const auto& pts : segs) {
            if (pts.empty()) continue;
            if (s.area && pts.size() > 1) fillUnder(r, pts, areaBase, c.withAlpha(26));
            if (pts.size() > 1) strokePath(r, pts, c, w, s.dashed);
            else fillDisc(r, pts[0], w, c);
        }
        Point last{0.f, 0.f};
        bool haveLast = false;
        for (auto it = segs.rbegin(); it != segs.rend() && !haveLast; ++it)
            if (!it->empty()) {
                last = it->back();
                haveLast = true;
            }
        if (haveLast && s.endDot) paintDot(r, last, 4.f, c, k.surface);
        if (haveLast && !s.endLabel.empty()) text(r, last.x + 8.f, last.y, s.endLabel, gfx::semibold(af), k.ink2);
    }

    // Les references (un plafond).
    for (const auto& rl : d.refs) {
        const float y = std::round(f.yAt(rl.value));
        r.fillRect({f.plot.x, y, f.plot.w, 1.f}, rl.color.a ? rl.color : k.muted);
        if (!rl.label.empty())
            text(r, rl.labelLeft ? f.plot.x + 4.f : f.plot.right() - 2.f, y - 8.f, rl.label, mf, k.muted, rl.labelLeft ? -1 : 1);
    }
    // Les etiquettes directes.
    for (const auto& nt : d.notes) {
        if (nt.index < 0 || nt.index >= n) continue;
        const int a = nt.anchor == Note::Anchor::Start ? -1 : nt.anchor == Note::Anchor::End ? 1 : 0;
        float x = f.xAt(nt.index);
        const float w = tw(nt.text, mf);
        if (a == 0) x = std::clamp(x, b.x + w * 0.5f, b.right() - w * 0.5f);
        text(r, x, f.yAt(nt.value) + nt.dy, nt.text, mf, k.ink2, a);
    }

    // Le survol des courbes : la regle et les points.
    if (shown >= 0) {
        const float x = std::round(f.xAt(shown)) + 0.5f;
        if (!f.band) r.fillRect({x - 0.5f, f.plot.y - 2.f, 1.f, f.plot.h + 2.f}, k.rule);
        for (const std::size_t si : lines) {
            const Series& s = d.series[si];
            if (static_cast<std::size_t>(shown) >= s.values.size()) continue;
            const double v = s.values[static_cast<std::size_t>(shown)];
            if (!std::isfinite(v)) continue;
            paintDot(r, {f.xAt(shown), f.yAt(v)}, 4.f, colors::onTheme(seriesColor(si), k), k.surface);
        }
    }
    paintFocus(ctx, f.plot);
}

std::pair<int, int> TimeChart::itemAt(Point p) const {
    const Frame f = frame();
    const float slot = f.band ? f.slot() * 0.5f : 8.f;
    if (p.y < f.plot.y - 8.f || p.y > f.plot.bottom() + 16.f || p.x < f.plot.x - slot || p.x > f.plot.right() + slot) return {-1, -1};
    const int i = f.indexAt(std::clamp(p.x, f.plot.x, f.plot.right() - 0.01f));
    if (i < 0) return {-1, -1};
    return {i, seriesAt(i, p, f)};
}

int TimeChart::seriesAt(int i, Point p, const Frame& f) const {
    const auto& d = data_;
    const std::size_t ui = static_cast<std::size_t>(i);
    std::vector<std::size_t> bars;
    for (std::size_t si = 0; si < d.series.size(); ++si)
        if (isBar(d.series[si])) bars.push_back(si);
    // Une courbe proche (12 px) passe avant les barres.
    int best = -1;
    float bestD = 12.f;
    for (std::size_t si = 0; si < d.series.size(); ++si) {
        const Series& s = d.series[si];
        if (isBar(s) || d.kind == Kind::Range || ui >= s.values.size() || !std::isfinite(s.values[ui])) continue;
        const float dist = std::fabs(f.yAt(s.values[ui]) - p.y);
        if (dist < bestD) {
            bestD = dist;
            best = static_cast<int>(si);
        }
    }
    if (best >= 0) return best;
    if (d.kind == Kind::Range) return 0;
    if (d.kind == Kind::Stacked || d.kind == Kind::Diverging) {
        for (int dir = 0; dir < 2; ++dir) {
            double base = 0.0;
            for (const std::size_t si : bars) {
                const Series& s = d.series[si];
                if (s.below != (dir == 1) || ui >= s.values.size() || !std::isfinite(s.values[ui])) continue;
                const double q = std::fabs(s.values[ui]);
                const float ya = dir == 0 ? f.yAt(base + q) : f.yAt(-base);
                const float yb = dir == 0 ? f.yAt(base) : f.yAt(-(base + q));
                if (p.y >= ya - 1.f && p.y <= yb + 1.f && q > 0.0) return static_cast<int>(si);
                base += q;
            }
        }
        return -1;
    }
    if ((d.kind == Kind::Grouped || d.kind == Kind::Bars) && bars.size() > 1) {
        const float kk = static_cast<float>(bars.size());
        const float slot = f.slot();
        const float bw = std::max(1.f, std::min(std::min(12.f, d.barMax), (slot * 0.74f - (kk - 1.f) * 2.f) / kk));
        const float x0 = f.xAt(i) - (kk * bw + (kk - 1.f) * 2.f) * 0.5f;
        const int g = static_cast<int>(std::floor((p.x - x0) / (bw + 2.f)));
        if (g >= 0 && g < static_cast<int>(bars.size())) return static_cast<int>(bars[static_cast<std::size_t>(g)]);
        return -1;
    }
    if (!bars.empty()) return static_cast<int>(bars[0]);
    for (std::size_t si = 0; si < d.series.size(); ++si)
        if (d.series[si].role == Role::Main) return static_cast<int>(si);
    return -1;
}

Pick TimeChart::pickFor(int index, int sub) const {
    Pick p;
    if (index < 0 || index >= itemCount()) return p;
    p.index = index;
    int s = sub;
    if (s < 0)
        for (std::size_t si = 0; si < data_.series.size(); ++si)
            if (data_.series[si].role == Role::Main) {
                s = static_cast<int>(si);
                break;
            }
    p.series = s;
    if (s >= 0 && static_cast<std::size_t>(s) < data_.series.size()) {
        const Series& ser = data_.series[static_cast<std::size_t>(s)];
        p.key = ser.key;
        if (static_cast<std::size_t>(index) < ser.values.size()) p.value = ser.values[static_cast<std::size_t>(index)];
    }
    return p;
}

Pick TimeChart::pickAt(Point p) const {
    const auto [i, s] = itemAt(p);
    if (i < 0) return {};
    Pick pk = pickFor(i, s);
    pk.series = s;
    if (s < 0) {
        pk.key.clear();
        pk.value = kNone;
    }
    return pk;
}

Tip TimeChart::tipFor(int index, int) const {
    Tip t;
    const auto& d = data_;
    if (index < 0 || index >= itemCount()) return t;
    const Category& c = d.x[static_cast<std::size_t>(index)];
    t.title = c.tip.empty() ? c.label : c.tip;
    if (d.partialFrom >= 0 && index >= d.partialFrom) t.title += std::string(" ") + fr::kDot + " en cours";
    const Format yf = d.yFormat ? d.yFormat : defaultFormat(extent().second);
    const Format vf = d.valueFormat ? d.valueFormat : yf;
    const std::size_t ui = static_cast<std::size_t>(index);
    if (d.kind == Kind::Range) {
        if (d.series.size() >= 2 && ui < d.series[0].values.size() && ui < d.series[1].values.size()) {
            const double a = d.series[0].values[ui], e = d.series[1].values[ui];
            if (std::isfinite(a) && std::isfinite(e)) {
                const Format f0 = d.series[0].format ? d.series[0].format : vf;
                Color col = seriesColor(0);
                if (ui < d.series[0].pointColors.size() && d.series[0].pointColors[ui].a) col = d.series[0].pointColors[ui];
                t.rows.push_back({f0(a) + " " + fr::kArrow + " " + f0(e), d.series[0].name, col, false});
            }
        }
    } else {
        std::vector<std::size_t> order;
        for (std::size_t si = 0; si < d.series.size(); ++si) order.push_back(si);
        if (!d.tipForward) std::reverse(order.begin(), order.end());
        // Divergentes : de haut en bas comme le dessin - les entrees (la plus haute d'abord), puis les sorties (la plus
        // proche de 0 d'abord).
        if (d.kind == Kind::Diverging && !d.tipForward) {
            std::vector<std::size_t> up, down;
            for (std::size_t si = d.series.size(); si-- > 0;)
                if (!d.series[si].below) up.push_back(si);
            for (std::size_t si = 0; si < d.series.size(); ++si)
                if (d.series[si].below) down.push_back(si);
            order = up;
            order.insert(order.end(), down.begin(), down.end());
        }
        // La comparaison : apres les series de la periode.
        std::stable_partition(order.begin(), order.end(), [&](std::size_t si) { return d.series[si].role != Role::Compare; });
        for (const std::size_t si : order) {
            const Series& s = d.series[si];
            if (s.noTip || ui >= s.values.size()) continue;
            const double v = s.values[ui];
            if (!std::isfinite(v)) continue;
            if (isBar(s) && v == 0.0 && !s.zeroInTip) continue;
            const Format f = s.format ? s.format : vf;
            Color col = seriesColor(si);
            if (ui < s.pointColors.size() && s.pointColors[ui].a) col = s.pointColors[ui];
            t.rows.push_back({f(s.below ? -std::fabs(v) : v), s.name, col, !isBar(s)});
        }
    }
    if (d.tipExtra) t.extra = d.tipExtra(index);
    t.click = d.clickHint;
    return t;
}

Rect TimeChart::tipAnchor(int index, int) const {
    const Frame f = frame();
    const float x = f.xAt(index);
    if (f.band) return {x - f.slot() * 0.5f, f.plot.y, f.slot(), f.plot.h};
    return {x, f.plot.y, 0.f, f.plot.h};
}

// ---- la distribution d'un delai ----
double binPosition(double value, const std::vector<double>& edges) {
    if (edges.size() < 2 || !std::isfinite(value)) return kNone;
    const std::size_t nb = edges.size() - 1;
    if (value <= edges.front()) return 0.0;
    for (std::size_t i = 0; i < nb; ++i) {
        const double a = edges[i], e = edges[i + 1];
        if (value < e || i + 1 == nb) {
            if (!std::isfinite(e) || e <= a) return static_cast<double>(i);
            // Au milieu de la tranche, decale selon la place de la valeur dans la tranche (dans la colonne).
            const double t = std::clamp((value - a) / (e - a), 0.0, 1.0);
            return static_cast<double>(i) + (t - 0.5) * 0.6;
        }
    }
    return static_cast<double>(nb - 1);
}

TimeChartData makeDistribution(const std::vector<double>& samples, const std::vector<double>& edges, const std::vector<std::string>& labels,
                               Color color, std::string seriesName, Format valueFormat) {
    TimeChartData d;
    d.kind = Kind::Bars;
    d.x = labelAxis(labels);
    const std::size_t nb = edges.size() >= 2 ? edges.size() - 1 : 0;
    std::vector<double> counts(std::max(nb, labels.size()), 0.0);
    for (const double v : samples) {
        if (!std::isfinite(v) || nb == 0) continue;
        std::size_t i = 0;
        while (i + 1 < nb && v >= edges[i + 1]) ++i;
        counts[i] += 1.0;
    }
    Series s;
    s.key = "distribution";
    s.name = std::move(seriesName);
    s.values = counts;
    s.color = color;
    s.format = valueFormat;
    d.series.push_back(std::move(s));
    d.legend = LegendMode::None;
    const double md = median(samples), mn = mean(samples);
    double mx = kNone;
    for (const double v : samples)
        if (std::isfinite(v) && (!std::isfinite(mx) || v > mx)) mx = v;
    const auto heightAt = [&](double pos) {
        const int i = std::clamp(static_cast<int>(std::lround(pos)), 0, static_cast<int>(counts.size()) - 1);
        return counts.empty() ? 0.0 : counts[static_cast<std::size_t>(i)];
    };
    if (std::isfinite(md)) {
        const double p = binPosition(md, edges);
        d.markers.push_back({p, "m\xC3\xA9" "diane " + fr::hours(md), heightAt(p), {}, true});
    }
    if (std::isfinite(mn)) {
        const double p = binPosition(mn, edges);
        d.markers.push_back({p, "moyenne " + fr::hours(mn), heightAt(p), {}, true});
    }
    if (std::isfinite(mx)) {
        const double p = binPosition(mx, edges);
        d.markers.push_back({p, "max " + fr::hours(mx), heightAt(p), {}, true});
    }
    return d;
}

} // namespace ui::charts
