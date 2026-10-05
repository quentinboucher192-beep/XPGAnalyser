#include "HmiProduction.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hmi {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

double fontOf(const Object& o, double fallback) { return std::clamp(o.number("fontSize", fallback), 8.0, 40.0); }

} // namespace

// ======================================================= compteurs de production ===
ProductionFigures productionFigures(double good, double bad, double runSeconds, double plannedSeconds, double idealRatePerHour,
                                    double rate, double target) {
    ProductionFigures f;
    f.good = std::max(0.0, good);
    f.bad = std::max(0.0, bad);
    f.total = f.good + f.bad;
    f.runSeconds = std::max(0.0, runSeconds);
    f.plannedSeconds = std::max(0.0, plannedSeconds);
    f.rate = std::max(0.0, rate);
    f.target = std::max(0.0, target);
    f.availability = f.plannedSeconds > 0 ? std::clamp(f.runSeconds / f.plannedSeconds, 0.0, 1.0) : 0.0;
    // Les pieces possibles pendant la marche, a la cadence nominale.
    const double possible = idealRatePerHour > 0 ? f.runSeconds * idealRatePerHour / 3600.0 : 0.0;
    f.performance = possible > 0 ? std::clamp(f.total / possible, 0.0, 1.0) : 0.0;
    f.quality = f.total > 0 ? f.good / f.total : 1.0;
    f.oee = f.availability * f.performance * f.quality;
    return f;
}

bool parseShifts(std::string_view text, std::vector<int>& minutes, std::string* why) {
    minutes.clear();
    for (const auto& piece : splitSemicolons(text)) {
        const std::string t = trimmed(piece);
        if (t.empty()) continue;
        int hh = -1, mm = -1;
        char tail = 0;
        const auto colon = t.find_first_of(":hH");
        bool ok = colon != std::string::npos;
        if (ok) {
            ok = std::sscanf(t.substr(0, colon).c_str(), "%d%c", &hh, &tail) == 1;
            const std::string rest = t.substr(colon + 1);
            if (ok) ok = rest.empty() ? (mm = 0, true) : std::sscanf(rest.c_str(), "%d%c", &mm, &tail) == 1;
        }
        if (!ok || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
            if (why) *why = "heure illisible : \xC2\xAB " + t + " \xC2\xBB (06:00 attendu)";
            minutes.clear();
            return false;
        }
        const int m = hh * 60 + mm;
        if (std::find(minutes.begin(), minutes.end(), m) == minutes.end()) minutes.push_back(m);
    }
    std::sort(minutes.begin(), minutes.end());
    return true;
}

int currentShift(const std::vector<int>& shifts, int minuteOfDay) {
    if (shifts.empty()) return -1;
    int best = shifts.back();     // avant le premier debut : le dernier poste de la veille
    for (int s : shifts)
        if (s <= minuteOfDay) best = s;
    return best;
}

std::string minutesText(int m) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", (m / 60) % 24, m % 60);
    return buf;
}

ProductionLayout productionLayout(const Object& o, double w, double h) {
    ProductionLayout l;
    const double fs = fontOf(o, 14), pad = 8, gap = 8;
    const double th = fs * 1.9;
    l.title = {pad, 2, w - 2 * pad, th};
    if (o.flag("showReset", true)) {
        const double bw = std::min(90.0, std::max(56.0, fs * 5.0));
        l.reset = {w - pad - bw, 4, bw, th - 6};
        l.title.w = std::max(0.0, l.reset.x - pad - 6);
    }
    const bool target = o.number("target", 0) > 0 || (o.find("target") && !o.find("target")->expr.empty());
    const double top = th + 6;
    const double remaining = std::max(0.0, h - top - pad);
    const double tilesH = remaining * (target ? 0.5 : 0.58);
    const double tw = (w - 2 * pad - 3 * gap) / 4.0;
    for (int k = 0; k < 4; ++k) l.tiles[k] = {pad + k * (tw + gap), top, std::max(0.0, tw), tilesH};
    const double y0 = top + tilesH + 8;
    const int rows = 3 + (target ? 1 : 0);
    const double rowH = std::max(0.0, (h - pad - y0) / rows);
    const double labelW = fs * 7.2, valueW = fs * 4.2;
    const double bw = std::max(0.0, w - 2 * pad - labelW - valueW);
    for (int k = 0; k < 3; ++k) l.bars[k] = {pad + labelW, y0 + k * rowH + rowH * 0.22, bw, rowH * 0.56};
    if (target) l.progress = {pad + labelW, y0 + 3 * rowH + rowH * 0.22, bw, rowH * 0.56};
    return l;
}

std::string productionHit(const Object& o, double w, double h, double x, double y) {
    const auto l = productionLayout(o, w, h);
    if (l.reset.w > 0 && l.reset.contains(x, y)) return "raz";
    return {};
}

// ======================================================= tableau de variables ======
std::vector<VariableRow> variableRows(const Object& o) {
    std::vector<VariableRow> out;
    const auto exprs = splitSemicolons(o.text("variables"));
    const auto names = splitSemicolons(o.text("names"));
    const auto units = splitSemicolons(o.text("units"));
    for (std::size_t k = 0; k < exprs.size(); ++k) {
        VariableRow r;
        r.expression = trimmed(exprs[k]);
        if (r.expression.empty()) continue;
        r.name = k < names.size() ? trimmed(names[k]) : std::string{};
        r.unit = k < units.size() ? trimmed(units[k]) : std::string{};
        out.push_back(std::move(r));
    }
    return out;
}

Box GridLayout::cell(std::size_t row, std::size_t col) const {
    if (col + 1 >= xs.size()) return {};
    return {xs[col], table.y + headerH + rowH * static_cast<double>(row), xs[col + 1] - xs[col], rowH};
}

namespace {
GridLayout grid(double x, double y, double w, double h, double fs, const std::vector<double>& fractions) {
    GridLayout g;
    g.rowH = std::max(22.0, fs + 10);
    g.headerH = g.rowH + 2;
    g.table = {x, y, w, std::max(0.0, h)};
    double at = x;
    g.xs.push_back(at);
    double sum = 0;
    for (double f : fractions) sum += f;
    for (double f : fractions) {
        at += w * f / std::max(1e-9, sum);
        g.xs.push_back(at);
    }
    g.visible = static_cast<std::size_t>(std::max(0.0, (h - g.headerH) / g.rowH));
    return g;
}
} // namespace

GridLayout variableTableLayout(const Object& o, double w, double h) {
    return grid(0, 0, w, h, fontOf(o, 13), {0.46, 0.34, 0.20});
}

std::string variableTableHit(const Object& o, double w, double h, double x, double y, std::size_t rows) {
    const auto g = variableTableLayout(o, w, h);
    if (x < 0 || x > w || y < g.headerH) return {};
    const auto k = static_cast<std::size_t>((y - g.headerH) / g.rowH);
    if (k >= rows || k >= g.visible) return {};
    return "ligne:" + std::to_string(k);
}

// ======================================================= editeur de recette =========
RecipeEditorLayout recipeEditorLayout(const Object& o, double w, double h) {
    RecipeEditorLayout l;
    const double fs = fontOf(o, 13), pad = 6;
    const double bar = std::max(30.0, fs * 2.3);
    const double bh = bar - 8;
    l.prev = {pad, 4, bh, bh};
    // Les boutons, a droite de la barre (ceux de "buttons", dans l'ordre) : les
    // premiers d'abord - s'il manque de la place, ce sont les derniers qui
    // tombent. Le nom du jeu prend ce qui reste entre les fleches (90 au moins).
    std::vector<std::string> labels;
    for (const auto& b : splitSemicolons(o.text("buttons", "Enregistrer;Appliquer;Lire;Annuler;Nouveau"))) {
        const std::string t = trimmed(b);
        if (!t.empty()) labels.push_back(t);
    }
    const double minLeft = l.prev.right() + 4 + 90 + 4 + bh + 8;
    const double avail = w - pad - minLeft;
    std::vector<std::pair<std::string, double>> sized;
    double used = 0;
    for (const auto& t : labels) {
        const double bw = std::max(64.0, fs * 0.62 * static_cast<double>(t.size()) + 22);
        const double need = used + (sized.empty() ? 0.0 : 6.0) + bw;
        if (need > avail) break;
        used = need;
        sized.emplace_back(t, bw);
    }
    double x = w - pad - used;
    for (const auto& [t, bw] : sized) {
        l.buttons.push_back({t, {x, 4, bw, bh}});
        x += bw + 6;
    }
    const double right = l.buttons.empty() ? w - pad : l.buttons.front().box.x - 8;
    l.name = {l.prev.right() + 4, 4, std::clamp(right - bh - 4 - (l.prev.right() + 4), 90.0, 240.0), bh};
    l.next = {l.name.right() + 4, 4, bh, bh};
    l.compare = o.flag("compare", true);
    const double top = bar + 2;
    l.grid = l.compare ? grid(0, top, w, h - top, fs, {0.32, 0.20, 0.20, 0.12, 0.16}) : grid(0, top, w, h - top, fs, {0.5, 0.3, 0.2});
    return l;
}

std::string recipeValueShown(std::string_view value) {
    if (value.size() < 2 || value.front() != '\'' || value.back() != '\'') return std::string(value);
    std::string out;
    const std::string_view inner = value.substr(1, value.size() - 2);
    for (std::size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '$' && i + 1 < inner.size() && (inner[i + 1] == '\'' || inner[i + 1] == '$')) { out += inner[++i]; continue; }
        out += inner[i];
    }
    return out;
}
std::string recipeEditorHit(const Object& o, double w, double h, double x, double y, std::size_t fields) {
    const auto l = recipeEditorLayout(o, w, h);
    if (l.prev.contains(x, y)) return "precedent";
    if (l.next.contains(x, y)) return "suivant";
    for (const auto& b : l.buttons)
        if (b.box.contains(x, y)) return "bouton:" + b.label;
    const auto& g = l.grid;
    if (y < g.table.y + g.headerH || x < 0 || x > w) return {};
    const auto k = static_cast<std::size_t>((y - g.table.y - g.headerH) / g.rowH);
    if (k >= fields || k >= g.visible) return {};
    return "ligne:" + std::to_string(k);
}

} // namespace hmi
