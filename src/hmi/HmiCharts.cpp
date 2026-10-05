#include "HmiCharts.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hmi {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

const std::vector<std::string>& chartPalette() {
    static const std::vector<std::string> p = {"#4FA3FF", "#F2994A", "#2ECC71", "#E5534B", "#B98CFF", "#F1C40F",
                                               "#1ABC9C", "#FF6FB5"};
    return p;
}

std::vector<ChartItem> chartItems(const Object& o, std::string_view listKey) {
    std::vector<ChartItem> out;
    const auto exprs = splitSemicolons(o.text(listKey));
    const auto names = splitSemicolons(o.text("names"));
    const auto colors = splitSemicolons(o.text("colors"));
    const auto& pal = chartPalette();
    std::size_t k = 0;
    for (const auto& e : exprs) {
        const std::string expr = trimmed(e);
        if (expr.empty()) { ++k; continue; }
        ChartItem it;
        it.expression = expr;
        it.name = k < names.size() ? trimmed(names[k]) : std::string{};
        const std::string c = k < colors.size() ? trimmed(colors[k]) : std::string{};
        it.color = !c.empty() ? c : pal[out.size() % pal.size()];
        out.push_back(std::move(it));
        ++k;
    }
    return out;
}

std::string chartItemLabel(const ChartItem& it) { return it.name.empty() ? it.expression : it.name; }

std::string joinLiveValues(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += kLiveSeparator;
        out += v[i];
    }
    return out;
}

std::vector<std::string> splitLiveValues(std::string_view s) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t from = 0;
    while (true) {
        const auto at = s.find(kLiveSeparator, from);
        out.emplace_back(s.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    return out;
}

std::vector<std::optional<double>> liveNumbers(std::string_view s) {
    std::vector<std::optional<double>> out;
    for (const auto& v : splitLiveValues(s)) {
        double x = 0;
        if (v == "TRUE") out.emplace_back(1.0);
        else if (v == "FALSE") out.emplace_back(0.0);
        else if (parseNumber(v, x)) out.emplace_back(x);
        else out.emplace_back(std::nullopt);
    }
    return out;
}

std::vector<PieSlice> pieSlices(const std::vector<double>& values) {
    std::vector<PieSlice> out;
    double sum = 0;
    for (double v : values) sum += std::max(0.0, v);
    if (sum <= 0) return out;
    double at = 0;
    for (double v : values) {
        PieSlice s;
        s.value = v;
        s.fraction = std::max(0.0, v) / sum;
        s.from = at;
        s.to = at + s.fraction * 360.0;
        at = s.to;
        out.push_back(s);
    }
    if (!out.empty()) out.back().to = 360.0;
    return out;
}

std::size_t HistogramStats::tallest() const noexcept {
    std::size_t m = 0;
    for (auto c : counts) m = std::max(m, c);
    return m;
}

HistogramStats histogram(const std::vector<double>& samples, double lo, double hi, int bins, std::optional<double> low,
                         std::optional<double> high) {
    HistogramStats h;
    bins = std::clamp(bins, 1, 200);
    h.counts.assign(static_cast<std::size_t>(bins), 0);
    if (hi <= lo) hi = lo + 1;
    double sum = 0, sq = 0;
    bool first = true;
    for (double x : samples) {
        if (!std::isfinite(x)) continue;
        ++h.n;
        sum += x;
        sq += x * x;
        if (first) { h.min = h.max = x; first = false; }
        h.min = std::min(h.min, x);
        h.max = std::max(h.max, x);
        if (x < lo) { ++h.below; }
        else if (x > hi) { ++h.above; }
        else {
            auto k = static_cast<std::size_t>((x - lo) / (hi - lo) * bins);
            if (k >= h.counts.size()) k = h.counts.size() - 1;      // x == hi : la derniere classe
            ++h.counts[k];
        }
        if ((low && x < *low) || (high && x > *high)) ++h.outOfTolerance;
    }
    if (h.n > 0) {
        h.mean = sum / static_cast<double>(h.n);
        // L'ecart type de l'echantillon (n - 1) ; une seule mesure : 0.
        const double var = h.n > 1 ? std::max(0.0, (sq - static_cast<double>(h.n) * h.mean * h.mean) / static_cast<double>(h.n - 1)) : 0.0;
        h.sigma = std::sqrt(var);
        if (h.sigma > 1e-12) {
            if (low && high && *high > *low) h.cp = (*high - *low) / (6 * h.sigma);
            if (low && high) h.cpk = std::min(*high - h.mean, h.mean - *low) / (3 * h.sigma);
            else if (high) h.cpk = (*high - h.mean) / (3 * h.sigma);
            else if (low) h.cpk = (h.mean - *low) / (3 * h.sigma);
        }
    }
    return h;
}

bool parseXYPoints(std::string_view text, std::vector<std::pair<double, double>>& out, std::string* why) {
    out.clear();
    std::string cur;
    std::vector<std::string> tokens;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == ';') {
            if (!cur.empty()) tokens.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    for (const auto& t : tokens) {
        const auto comma = t.find(',');
        double x = 0, y = 0;
        if (comma == std::string::npos || !parseNumber(t.substr(0, comma), x) || !parseNumber(t.substr(comma + 1), y)) {
            if (why) *why = "point illisible : \xC2\xAB " + t + " \xC2\xBB (x,y attendu)";
            out.clear();
            return false;
        }
        out.emplace_back(x, y);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return true;
}

std::optional<double> interpolateAt(const std::vector<std::pair<double, double>>& curve, double x) {
    if (curve.empty()) return std::nullopt;
    if (curve.size() == 1) return std::fabs(curve[0].first - x) < 1e-12 ? std::optional<double>(curve[0].second) : std::nullopt;
    if (x < curve.front().first - 1e-12 || x > curve.back().first + 1e-12) return std::nullopt;
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const auto& a = curve[i - 1];
        const auto& b = curve[i];
        if (x > b.first + 1e-12) continue;
        if (b.first - a.first < 1e-12) return b.second;
        const double t = (x - a.first) / (b.first - a.first);
        return a.second + (b.second - a.second) * t;
    }
    return curve.back().second;
}

std::vector<StateSegment> stateSegments(const std::deque<std::pair<double, double>>& changes, double start, double end) {
    std::vector<StateSegment> out;
    if (changes.empty() || end <= start) return out;
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const double from = changes[i].first;
        const double to = i + 1 < changes.size() ? changes[i + 1].first : end;
        const double a = std::max(from, start), b = std::min(to, end);
        if (b <= a) continue;
        if (!out.empty() && out.back().value == changes[i].second && std::fabs(out.back().to - a) < 1e-9) {
            out.back().to = b;       // le meme etat, redit : un seul segment
            continue;
        }
        out.push_back({a, b, changes[i].second});
    }
    return out;
}

std::vector<double> niceTicks(double lo, double hi, int approx) {
    std::vector<double> out;
    if (!(hi > lo) || approx < 1) return out;
    const double raw = (hi - lo) / approx;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (raw <= m * mag + 1e-12) { step = m * mag; break; }
    const double first = std::ceil(lo / step - 1e-9) * step;
    for (double t = first; t <= hi + step * 1e-6 && out.size() < 200; t += step) out.push_back(std::fabs(t) < step * 1e-9 ? 0.0 : t);
    return out;
}

std::pair<double, double> autoRange(const std::vector<double>& values, double fallbackLo, double fallbackHi, bool includeZero) {
    bool any = false;
    double mn = 0, mx = 0;
    for (double v : values) {
        if (!std::isfinite(v)) continue;
        if (!any) { mn = mx = v; any = true; }
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (!any) return {fallbackLo, fallbackHi};
    if (includeZero) { mn = std::min(mn, 0.0); mx = std::max(mx, 0.0); }
    if (mx - mn < 1e-9) { mn -= 1; mx += 1; }
    const double pad = (mx - mn) * 0.08;
    double lo = includeZero && mn >= 0 ? 0.0 : mn - pad, hi = mx + pad;
    const auto ticks = niceTicks(lo, hi, 5);
    if (ticks.size() >= 2) {
        const double step = ticks[1] - ticks[0];
        lo = std::floor(lo / step + 1e-9) * step;
        hi = std::ceil(hi / step - 1e-9) * step;
    }
    return {lo, hi};
}

} // namespace hmi
