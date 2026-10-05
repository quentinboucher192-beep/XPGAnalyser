#include "HmiVarMotion.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hmi::motion {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool number(std::string_view text, double& out) {
    std::string t = trimmed(text);
    // Une unite tapee par habitude : "30 s".
    if (!t.empty() && (t.back() == 's' || t.back() == 'S')) t = trimmed(std::string_view(t).substr(0, t.size() - 1));
    if (t.empty()) return false;
    std::replace(t.begin(), t.end(), ',', '.');
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

// "20 ; 80 ; 10", "20 .. 80" -> les nombres ; faux : un morceau illisible.
bool numbers(std::string_view text, std::vector<double>& out) {
    std::string t(text);
    for (std::size_t at = t.find(".."); at != std::string::npos; at = t.find("..", at + 1)) t.replace(at, 2, ";");
    std::size_t start = 0;
    while (start <= t.size()) {
        const auto semi = t.find(';', start);
        const std::string piece = t.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        if (!trimmed(piece).empty()) {
            double d = 0;
            if (!number(piece, d)) return false;
            out.push_back(d);
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return true;
}

std::string num(double v) {
    if (std::fabs(v - std::round(v)) < 1e-9) return std::to_string(static_cast<long long>(std::llround(v)));
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::vector<double> stepsOf(const std::string& text) {
    std::vector<double> out;
    (void)numbers(text, out);
    return out;
}

} // namespace

std::vector<BehaviorKind> kindsFor(bool boolean) {
    if (boolean) return {BehaviorKind::Constant, BehaviorKind::Blink, BehaviorKind::Steps};
    return {BehaviorKind::Constant, BehaviorKind::Sine, BehaviorKind::Ramp, BehaviorKind::Counter,
            BehaviorKind::Blink, BehaviorKind::Random, BehaviorKind::Steps};
}

Motion defaultFor(BehaviorKind kind, bool boolean, double current) {
    Motion m;
    m.kind = kind;
    m.period = 10;
    if (boolean) {
        m.a = current != 0 ? 1 : 0;
        m.b = 1;
        if (kind == BehaviorKind::Blink) m.period = 2;
        if (kind == BehaviorKind::Steps) m.steps = "0; 1";
        return m;
    }
    // Autour de la valeur du moment : +/- 20 % (0 : de 0 a 100).
    const double span = std::fabs(current) > 1e-9 ? std::fabs(current) * 0.2 : 50.0;
    const double mid = std::fabs(current) > 1e-9 ? current : 50.0;
    switch (kind) {
        case BehaviorKind::Constant: m.a = current; m.b = current; break;
        case BehaviorKind::Counter: m.a = current; m.b = 1; m.period = 1; break;
        case BehaviorKind::Blink: m.a = 0; m.b = 1; m.period = 2; break;
        case BehaviorKind::Steps: m.steps = num(mid - span) + "; " + num(mid) + "; " + num(mid + span); break;
        default: m.a = mid - span; m.b = mid + span; break;
    }
    return m;
}

double valueAt(const Motion& m, double t, State& st) {
    const double period = m.period > 1e-3 ? m.period : 1.0;
    switch (m.kind) {
        case BehaviorKind::Constant: return m.a;
        case BehaviorKind::Sine: return (m.a + m.b) / 2 + (m.b - m.a) / 2 * std::sin(2 * kPi * t / period);
        case BehaviorKind::Ramp: return m.a + (m.b - m.a) * (std::fmod(std::max(0.0, t), period) / period);
        case BehaviorKind::Counter: return m.a + m.b * std::floor(std::max(0.0, t) / period);
        case BehaviorKind::Blink:
            return m.delay > 0 ? (std::fmod(std::max(0.0, t), period) < m.delay ? 1 : 0)
                               : static_cast<long long>(std::floor(std::max(0.0, t) / period)) % 2 == 0 ? 1 : 0;
        case BehaviorKind::Random:
            if (t >= st.nextAt || st.nextAt - t > period) {
                st.nextAt = t + period;
                // xorshift64 : une suite propre a la variable, sans etat global.
                st.seed ^= st.seed << 13;
                st.seed ^= st.seed >> 7;
                st.seed ^= st.seed << 17;
                const double u = static_cast<double>(st.seed >> 11) / 9007199254740992.0;
                st.value = std::min(m.a, m.b) + u * std::fabs(m.b - m.a);
            }
            return st.value;
        case BehaviorKind::Steps: {
            const auto list = stepsOf(m.steps);
            if (list.empty()) return m.a;
            return list[static_cast<std::size_t>(std::floor(std::max(0.0, t) / period)) % list.size()];
        }
        default: return m.a;
    }
}

std::string text(const Motion& m) {
    const std::string kind(behaviorKindLabel(m.kind));
    const std::string p = " \xC2\xB7 " + num(m.period) + " s";
    switch (m.kind) {
        case BehaviorKind::Constant: return kind + " " + num(m.a);
        case BehaviorKind::Counter: return kind + " " + num(m.a) + " +" + num(m.b) + p;
        case BehaviorKind::Blink: return kind + (m.delay > 0 ? " " + num(m.delay) + " s / " + num(m.period) + " s" : p);
        case BehaviorKind::Steps: return kind + " " + m.steps + p;
        default: return kind + " " + num(m.a) + " \xE2\x86\x92 " + num(m.b) + p;
    }
}

std::string editText(const Motion& m) {
    switch (m.kind) {
        case BehaviorKind::Constant: return num(m.a);
        case BehaviorKind::Blink: return m.delay > 0 ? num(m.delay) + " ; " + num(m.period) : num(m.period / 2) + " ; " + num(m.period);
        case BehaviorKind::Steps: return m.steps;
        default: return num(m.a) + " ; " + num(m.b) + " ; " + num(m.period);
    }
}

bool parse(std::string_view text, Motion& m, std::string* why) {
    std::vector<double> v;
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!numbers(text, v) || v.empty()) return fail("des nombres s\xC3\xA9par\xC3\xA9s par \xC2\xAB ; \xC2\xBB (20 ; 80 ; 10)");
    Motion n = m;
    switch (m.kind) {
        case BehaviorKind::Constant:
            if (v.size() != 1) return fail("une constante : une seule valeur (50)");
            n.a = n.b = v[0];
            break;
        case BehaviorKind::Steps: {
            std::string s;
            for (const double d : v) s += (s.empty() ? "" : "; ") + num(d);
            n.steps = s;
            break;
        }
        case BehaviorKind::Blink:
            if (v.size() > 2 || v[0] <= 0) return fail("clignote : le temps \xC3\xA0 1, puis la p\xC3\xA9riode (1,5 ; 4)");
            n.delay = v[0];
            if (v.size() == 2) n.period = v[1];
            if (n.delay >= n.period) return fail("clignote : le temps \xC3\xA0 1 doit \xC3\xAAtre plus court que la p\xC3\xA9riode");
            break;
        case BehaviorKind::Counter:
            if (v.size() < 2 || v.size() > 3) return fail("compteur : le d\xC3\xA9part ; le pas ; la p\xC3\xA9riode (0 ; 1 ; 1)");
            n.a = v[0];
            n.b = v[1];
            if (v.size() == 3) n.period = v[2];
            break;
        default:
            if (v.size() < 2 || v.size() > 3) return fail("les bornes : min ; max (20 ; 80), et la p\xC3\xA9riode si besoin (20 ; 80 ; 30)");
            n.a = std::min(v[0], v[1]);
            n.b = std::max(v[0], v[1]);
            if (v.size() == 3) n.period = v[2];
            break;
    }
    if (n.period <= 0) return fail("la p\xC3\xA9riode doit \xC3\xAAtre positive");
    m = n;
    return true;
}

} // namespace hmi::motion
