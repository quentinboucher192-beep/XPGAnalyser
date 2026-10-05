#include "HmiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> splitOn(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

} // namespace

// ---------------------------------------------------------------- image animee
std::vector<ImageState> parseImageStates(std::string_view text) {
    std::vector<ImageState> out;
    for (const auto& raw : splitOn(text, '\n')) {
        const auto line = trim(raw);
        if (line.empty()) continue;
        ImageState st;
        std::string rest = line;
        if (const auto arrow = line.find("=>"); arrow != std::string::npos) {
            st.condition = trim(std::string_view(line).substr(0, arrow));
            rest = trim(std::string_view(line).substr(arrow + 2));
        }
        if (const auto at = rest.rfind('@'); at != std::string::npos) {
            st.periodMs = std::max(0, std::atoi(trim(std::string_view(rest).substr(at + 1)).c_str()));
            rest = trim(std::string_view(rest).substr(0, at));
        }
        for (const auto& img : splitOn(rest, ','))
            if (auto name = trim(img); !name.empty()) st.images.push_back(std::move(name));
        out.push_back(std::move(st));
    }
    return out;
}

std::string formatImageStates(const std::vector<ImageState>& states) {
    std::string out;
    for (const auto& st : states) {
        if (!out.empty()) out += '\n';
        out += (st.condition.empty() ? std::string("TRUE") : st.condition) + " => ";
        for (std::size_t i = 0; i < st.images.size(); ++i) out += (i ? ", " : "") + st.images[i];
        if (st.periodMs > 0) out += " @ " + std::to_string(st.periodMs);
    }
    return out;
}

std::string imageFrame(const ImageState& s, int objectPeriodMs, double seconds) {
    if (s.images.empty()) return {};
    if (s.images.size() == 1) return s.images.front();
    const int period = std::max(20, s.periodMs > 0 ? s.periodMs : objectPeriodMs > 0 ? objectPeriodMs : 500);
    const auto step = static_cast<long long>(std::floor(std::max(0.0, seconds) * 1000.0 / period));
    return s.images[static_cast<std::size_t>(step % static_cast<long long>(s.images.size()))];
}

// ------------------------------------------------------------------- tableau
std::vector<std::vector<std::string>> parseCells(std::string_view text) {
    std::vector<std::vector<std::string>> out;
    if (text.empty()) return out;
    for (const auto& line : splitOn(text, '\n')) out.push_back(splitOn(line, '\t'));
    return out;
}

std::string formatCells(const std::vector<std::vector<std::string>>& rows) {
    std::string out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r) out += '\n';
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            if (c) out += '\t';
            std::string cell = rows[r][c];
            std::replace(cell.begin(), cell.end(), '\t', ' ');
            std::replace(cell.begin(), cell.end(), '\n', ' ');
            out += cell;
        }
    }
    return out;
}

bool cellIsExpression(std::string_view cell) noexcept { return !cell.empty() && cell.front() == '='; }
bool cellIsTemplate(std::string_view cell) noexcept {
    const auto open = cell.find('{');
    return open != std::string_view::npos && cell.find('}', open) != std::string_view::npos;
}

std::vector<double> columnFractions(const Object& o, std::size_t n) {
    std::vector<double> w(n, 1.0);
    if (const auto* p = o.find("widths"); p && !p->value.empty()) {
        const auto parts = splitOn(p->value, ';');
        for (std::size_t i = 0; i < n && i < parts.size(); ++i) {
            double v = 0;
            if (parseNumber(trim(parts[i]), v) && v > 0) w[i] = v;
        }
    }
    double total = 0;
    for (const double x : w) total += x;
    for (auto& x : w) x = total > 0 ? x / total : 1.0 / static_cast<double>(std::max<std::size_t>(1, n));
    return w;
}

std::vector<std::string> splitSemicolons(std::string_view s) {
    std::vector<std::string> out;
    if (trim(s).empty()) return out;
    for (const auto& part : splitOn(s, ';')) out.push_back(trim(part));
    return out;
}

std::string joinSemicolons(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ";" : "") + v[i];
    return out;
}

// ------------------------------------------------------ gestionnaire de recettes
RecipeManagerLayout recipeManagerLayout(const Object& o, double w, double h) {
    RecipeManagerLayout l;
    const double bar = 30, pad = 4;
    double x = pad;
    std::vector<std::string> labels;
    if (const auto* p = o.find("buttons")) labels = splitSemicolons(p->value);
    else labels.assign(std::begin(kRecipeButtons), std::end(kRecipeButtons));
    const double fontSize = std::clamp(o.number("fontSize", 13), 8.0, 32.0);
    for (const auto& label : labels) {
        if (label.empty()) continue;
        // Une largeur d'apres le texte (sans police : 0,58 em par caractere, en
        // comptant les caracteres et non les octets).
        std::size_t chars = 0;
        for (const char c : label) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
        const double bw = std::max(56.0, static_cast<double>(chars) * fontSize * 0.58 + 18);
        if (x + bw > w - pad) break;
        l.buttons.push_back({label, Box{x, pad, bw, bar - 2 * pad}});
        x += bw + pad;
    }
    l.title = Box{x + pad, 0, std::max(0.0, w - x - 2 * pad), bar};
    l.table = Box{0, bar, w, std::max(0.0, h - bar)};
    l.rowH = std::max(18.0, fontSize + 9);
    l.headerH = l.rowH + 2;
    l.visibleRows = l.table.h > l.headerH ? static_cast<std::size_t>((l.table.h - l.headerH) / l.rowH) : 0;
    return l;
}

std::string recipeManagerHit(const Object& o, double w, double h, double x, double y, std::size_t records) {
    const auto l = recipeManagerLayout(o, w, h);
    for (const auto& b : l.buttons)
        if (b.box.contains(x, y)) return "bouton:" + b.label;
    if (y >= l.table.y + l.headerH && y < l.table.bottom() && x >= 0 && x <= w) {
        const auto row = static_cast<std::size_t>((y - l.table.y - l.headerH) / l.rowH);
        if (row < records && row < l.visibleRows) return "ligne:" + std::to_string(row);
    }
    return {};
}

} // namespace hmi
