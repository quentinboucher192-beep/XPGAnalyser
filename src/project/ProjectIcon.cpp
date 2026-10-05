// project/ProjectIcon.cpp - l'icone du projet (lot API 6).
#include "ProjectIcon.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace project::icon {

namespace {

constexpr int N = kSize;
constexpr std::string_view kLetters = ".123456789ABCDEFG";

using Grid = std::vector<std::uint8_t>;

void ensure(domain::ProjectIcon& ic) {
    if (ic.pixels.size() != static_cast<std::size_t>(N * N)) ic.pixels.assign(static_cast<std::size_t>(N * N), 0);
    if (std::all_of(ic.palette.begin(), ic.palette.end(), [](std::uint32_t c) { return c == 0; })) ic.palette = defaultPalette();
}

// ---- les primitives des dessins (les memes que la maquette) --------------------
void P(Grid& g, int x, int y, std::uint8_t c) {
    if (x >= 0 && x < N && y >= 0 && y < N) g[static_cast<std::size_t>(y * N + x)] = c;
}
void R(Grid& g, int x, int y, int w, int h, std::uint8_t c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) P(g, i, j, c);
}
void Fr(Grid& g, int x, int y, int w, int h, std::uint8_t c) {
    for (int i = x; i < x + w; ++i) { P(g, i, y, c); P(g, i, y + h - 1, c); }
    for (int j = y; j < y + h; ++j) { P(g, x, j, c); P(g, x + w - 1, j, c); }
}
// Un disque : les pixels dont le CENTRE (i + 0,5 ; j + 0,5) est a moins de r.
void D(Grid& g, double cx, double cy, double r, std::uint8_t c) {
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const double dx = i + 0.5 - cx, dy = j + 0.5 - cy;
            if (dx * dx + dy * dy <= r * r) P(g, i, j, c);
        }
}
// Un polygone : les pixels dont le centre est dedans (pair-impair).
void Pg(Grid& g, const std::vector<std::pair<double, double>>& pts, std::uint8_t c) {
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const double x = i + 0.5, y = j + 0.5;
            bool inside = false;
            for (std::size_t a = 0, b = pts.size() - 1; a < pts.size(); b = a++) {
                const auto [xa, ya] = pts[a];
                const auto [xb, yb] = pts[b];
                if ((ya > y) != (yb > y) && x < (xb - xa) * (y - ya) / (yb - ya) + xa) inside = !inside;
            }
            if (inside) P(g, i, j, c);
        }
}
// Le contour : chaque pixel vide qui touche un pixel plein par un cote prend c.
void Out(Grid& g, std::uint8_t c) {
    const Grid src = g;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            if (src[static_cast<std::size_t>(j * N + i)]) continue;
            static constexpr int kD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& d : kD) {
                const int x = i + d[0], y = j + d[1];
                if (x < 0 || y < 0 || x >= N || y >= N) continue;
                const auto v = src[static_cast<std::size_t>(y * N + x)];
                if (v && v != c) {
                    g[static_cast<std::size_t>(j * N + i)] = c;
                    break;
                }
            }
        }
}
void Ln(Grid& g, int x0, int y0, int x1, int y1, std::uint8_t c) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        P(g, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

// Une police de 5 x 7, pour les initiales (lignes de haut en bas).
const char* glyphOf(char ch) {
    switch (ch) {
        case 'A': return "01110100011000111111100011000110001"; case 'B': return "11110100011000111110100011000111110";
        case 'C': return "01110100011000010000100001000101110"; case 'D': return "11100100101000110001100011001011100";
        case 'E': return "11111100001000011110100001000011111"; case 'F': return "11111100001000011110100001000010000";
        case 'G': return "01110100011000010111100011000101111"; case 'H': return "10001100011000111111100011000110001";
        case 'I': return "01110001000010000100001000010001110"; case 'J': return "00111000100001000010000101001001100";
        case 'K': return "10001100101010011000101001001010001"; case 'L': return "10000100001000010000100001000011111";
        case 'M': return "10001110111010110101100011000110001"; case 'N': return "10001100011100110101100111000110001";
        case 'O': return "01110100011000110001100011000101110"; case 'P': return "11110100011000111110100001000010000";
        case 'Q': return "01110100011000110001101011001001101"; case 'R': return "11110100011000111110101001001010001";
        case 'S': return "01111100001000001110000010000111110"; case 'T': return "11111001000010000100001000010000100";
        case 'U': return "10001100011000110001100011000101110"; case 'V': return "10001100011000110001100010101000100";
        case 'W': return "10001100011000110101101011010101010"; case 'X': return "10001100010101000100010101000110001";
        case 'Y': return "10001100010101000100001000010000100"; case 'Z': return "11111000010001000100010001000011111";
        case '0': return "01110100011001110101110011000101110"; case '1': return "00100011000010000100001000010001110";
        case '2': return "01110100010000100010001000100011111"; case '3': return "11111000100010000010000011000101110";
        case '4': return "00010001100101010010111110001000010"; case '5': return "11111100001111000001000011000101110";
        case '6': return "00110010001000011110100011000101110"; case '7': return "11111000010001000100010000100001000";
        case '8': return "01110100011000101110100011000101110"; case '9': return "01110100011000101111000010001001100";
        default:  return nullptr;
    }
}
void glyph(Grid& g, char ch, int x0, int y0, std::uint8_t c) {
    const char* bits = glyphOf(ch);
    if (!bits) return;
    for (int r = 0; r < 7; ++r)
        for (int q = 0; q < 5; ++q)
            if (bits[r * 5 + q] == '1') R(g, x0 + q * 2, y0 + r * 2, 2, 2, c);
}

// Les accents retires, en majuscules : "Elevateur" -> "ELEVATEUR".
std::string plainUpper(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[++i]);
            char base = 0;
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) base = 'A';
            else if (d == 0x87 || d == 0xA7) base = 'C';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'E';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'I';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'O';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'U';
            out += base ? base : '?';
            continue;
        }
        if (c >= 0x80) { out += '?'; continue; }
        out += static_cast<char>(std::toupper(c));
    }
    return out;
}

using Draw = void (*)(Grid&, std::string_view);

void drawArmoire(Grid& g, std::string_view) {
    R(g, 5, 2, 22, 3, 3); R(g, 6, 5, 20, 24, 4); Fr(g, 8, 7, 16, 20, 3); R(g, 21, 15, 2, 4, 2);
    Pg(g, {{15.5, 9.5}, {10, 20.5}, {21, 20.5}}, 9);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{16, 12}, {16, 13}, {15, 14}, {15, 15}, {16, 15}, {17, 15}, {16, 16}, {16, 17}, {15, 18}}) P(g, x, y, 1);
    R(g, 10, 23, 12, 1, 3); R(g, 10, 25, 12, 1, 3); R(g, 7, 29, 3, 2, 2); R(g, 22, 29, 3, 2, 2); Out(g, 1);
}
void drawBouteille(Grid& g, std::string_view) {
    R(g, 12, 1, 8, 2, 2); R(g, 15, 3, 2, 2, 3); R(g, 13, 5, 6, 2, 4);
    D(g, 16, 13, 6.2, 7); R(g, 10, 13, 12, 16, 7); Pg(g, {{19, 8.5}, {22, 12}, {22, 29}, {19, 29}}, 6);
    R(g, 12, 11, 1, 16, 8); R(g, 10, 18, 12, 4, 4); R(g, 12, 19, 5, 1, 3); R(g, 12, 20, 7, 1, 3); R(g, 10, 29, 12, 1, 2); Out(g, 1);
}
void drawVanne(Grid& g, std::string_view) {
    R(g, 1, 17, 7, 6, 3); R(g, 24, 17, 7, 6, 3); R(g, 1, 17, 7, 1, 4); R(g, 24, 17, 7, 1, 4);
    R(g, 7, 15, 2, 10, 2); R(g, 23, 15, 2, 10, 2);
    Pg(g, {{9, 14}, {9, 26}, {16, 20}}, 13); Pg(g, {{23, 14}, {23, 26}, {16, 20}}, 13);
    R(g, 15, 8, 2, 12, 2); R(g, 10, 5, 12, 3, 7); R(g, 11, 5, 10, 1, 8); Out(g, 1);
}
void drawFlamme(Grid& g, std::string_view) {
    Pg(g, {{16, 1.5}, {21, 8}, {24.5, 14}, {25, 20}, {22.5, 26}, {18, 29}, {14, 29}, {9.5, 26}, {7, 20}, {7.5, 14}, {11, 8}}, 8);
    Pg(g, {{16, 9}, {19.5, 15}, {21, 20}, {19.5, 25}, {16, 27}, {12.5, 25}, {11, 20}, {12.5, 15}}, 9);
    D(g, 16, 23.5, 2.8, 5); Out(g, 6);
}
void drawMoteur(Grid& g, std::string_view) {
    R(g, 10, 5, 8, 4, 13); R(g, 11, 6, 6, 1, 12); R(g, 5, 9, 19, 15, 13);
    for (int x = 7; x <= 21; x += 2) R(g, x, 10, 1, 13, 14);
    R(g, 24, 11, 3, 11, 3); R(g, 27, 15, 4, 3, 4); R(g, 6, 24, 17, 2, 2); R(g, 7, 26, 4, 3, 2); R(g, 18, 26, 4, 3, 2); Out(g, 1);
}
void drawEclair(Grid& g, std::string_view) {
    D(g, 16, 16, 15, 14); Pg(g, {{19, 3}, {9, 18}, {15, 18}, {12.5, 29}, {23.5, 13}, {17.5, 13}, {21, 3}}, 9); Out(g, 1);
}
void drawEngrenage(Grid& g, std::string_view) {
    D(g, 16, 16, 10, 3);
    for (int k = 0; k < 8; ++k) {
        const double a = k * 3.14159265358979323846 / 4.0;
        const double cx = 16 + 11.5 * std::cos(a), cy = 16 + 11.5 * std::sin(a);
        R(g, static_cast<int>(std::floor(cx - 2 + 0.5)), static_cast<int>(std::floor(cy - 2 + 0.5)), 4, 4, 3);
    }
    D(g, 16, 16, 6, 4); D(g, 16, 16, 3.2, 0); Out(g, 2);
}
void drawGoutte(Grid& g, std::string_view) {
    D(g, 16, 20.5, 9, 13); Pg(g, {{16, 2}, {23.6, 16.5}, {8.4, 16.5}}, 13); D(g, 16, 21.5, 6.5, 12); R(g, 11, 17, 2, 5, 5); P(g, 12, 16, 5); Out(g, 14);
}
void drawUsine(Grid& g, std::string_view) {
    R(g, 3, 16, 26, 12, 3); Pg(g, {{3, 16}, {3, 10}, {11, 16}}, 2); Pg(g, {{11, 16}, {11, 10}, {19, 16}}, 2); Pg(g, {{19, 16}, {19, 10}, {27, 16}}, 2);
    R(g, 23, 4, 4, 12, 7); R(g, 23, 7, 4, 1, 5); D(g, 27, 2.5, 2.2, 4); D(g, 22, 2, 1.6, 4);
    for (const int x : {5, 10, 15}) R(g, x, 20, 3, 3, 9);
    R(g, 22, 21, 4, 7, 16); R(g, 0, 28, 32, 2, 2); Out(g, 1);
}
void drawPompe(Grid& g, std::string_view) {
    R(g, 1, 15, 6, 6, 3); R(g, 20, 5, 10, 5, 3); D(g, 15, 17, 10, 12); Pg(g, {{10, 11}, {10, 23}, {22, 17}}, 13); Out(g, 1);
}
void drawAutomate(Grid& g, std::string_view) {
    R(g, 3, 5, 26, 22, 2);
    for (const auto& [x, c] : std::vector<std::pair<int, std::uint8_t>>{{5, 13}, {11, 4}, {17, 4}, {23, 4}}) R(g, x, 7, 5, 18, c);
    R(g, 6, 9, 3, 2, 12); P(g, 6, 13, 10); P(g, 8, 13, 9); P(g, 6, 15, 7);
    for (const int x : {12, 18, 24}) { P(g, x, 9, 10); P(g, x + 2, 9, 10); P(g, x, 11, 10); P(g, x + 2, 11, 7); R(g, x, 20, 3, 3, 3); }
    R(g, 1, 27, 30, 2, 3); Out(g, 1);
}
void drawCuve(Grid& g, std::string_view) {
    D(g, 16, 9, 8, 4); D(g, 16, 22, 8, 4); R(g, 8, 9, 16, 13, 4); R(g, 19, 6, 5, 19, 3);
    R(g, 13, 8, 5, 15, 2); R(g, 14, 15, 3, 7, 12); R(g, 14, 14, 3, 1, 5);
    R(g, 9, 27, 2, 4, 2); R(g, 21, 27, 2, 4, 2); R(g, 24, 21, 7, 3, 3); R(g, 14, 1, 4, 2, 3); Out(g, 1);
}
void drawThermometre(Grid& g, std::string_view) {
    R(g, 13, 3, 6, 20, 5); D(g, 16, 25, 5, 7); R(g, 15, 10, 2, 14, 7); P(g, 14, 25, 8); P(g, 14, 24, 8);
    for (const int y : {6, 10, 14, 18}) R(g, 20, y, 3, 1, 2);
    for (const int y : {8, 12, 16}) R(g, 20, y, 2, 1, 3);
    Out(g, 1);
}
void drawVentilateur(Grid& g, std::string_view) {
    D(g, 16, 16, 14.5, 3); D(g, 16, 16, 12.5, 0);
    for (int k = 0; k < 3; ++k) {
        const double a = k * 2.0 * 3.14159265358979323846 / 3.0 - 3.14159265358979323846 / 2.0;
        std::vector<std::pair<double, double>> pts;
        for (const auto& [x, y] : std::vector<std::pair<double, double>>{{1, -1}, {5, -4.5}, {10.5, -4}, {11, 0}, {6, 2.5}, {1, 1.5}})
            pts.emplace_back(16 + x * std::cos(a) - y * std::sin(a), 16 + x * std::sin(a) + y * std::cos(a));
        Pg(g, pts, k == 0 ? 12 : 13);
    }
    D(g, 16, 16, 3, 2); D(g, 16, 16, 1.2, 4); Out(g, 1);
}
void drawInitiales(Grid& g, std::string_view name) {
    R(g, 2, 2, 28, 28, 13);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{2, 2}, {3, 2}, {2, 3}, {29, 2}, {28, 2}, {29, 3}, {2, 29}, {3, 29}, {2, 28}, {29, 29}, {28, 29}, {29, 28}})
        P(g, x, y, 0);
    const auto t = initialsOf(name.empty() ? std::string_view("Projet") : name);
    const int w = t.size() > 1 ? 22 : 10, x0 = 16 - w / 2;
    for (std::size_t i = 0; i < t.size() && i < 2; ++i) {
        glyph(g, t[i], x0 + static_cast<int>(i) * 12 + 1, 10, 14);
        glyph(g, t[i], x0 + static_cast<int>(i) * 12, 9, 5);
    }
}

struct Entry { Preset preset; Draw draw; };
const std::vector<Entry>& entries() {
    static const std::vector<Entry> k{
        {{"armoire", "Armoire"}, drawArmoire},
        {{"bouteille", "Bouteille de gaz"}, drawBouteille},
        {{"vanne", "Vanne"}, drawVanne},
        {{"flamme", "Flamme"}, drawFlamme},
        {{"moteur", "Moteur"}, drawMoteur},
        {{"eclair", "\xC3\x89" "clair"}, drawEclair},
        {{"engrenage", "Engrenage"}, drawEngrenage},
        {{"goutte", "Goutte"}, drawGoutte},
        {{"usine", "Usine"}, drawUsine},
        {{"pompe", "Pompe"}, drawPompe},
        {{"automate", "Automate"}, drawAutomate},
        {{"cuve", "Cuve"}, drawCuve},
        {{"thermometre", "Thermom\xC3\xA8tre"}, drawThermometre},
        {{"ventilateur", "Ventilateur"}, drawVentilateur},
        {{"initiales", "Initiales"}, drawInitiales},
    };
    return k;
}

} // namespace

const std::array<std::uint32_t, 16>& defaultPalette() {
    static constexpr std::array<std::uint32_t, 16> k{0x0F1115, 0x3A414D, 0x7A8394, 0xC9CED8, 0xFFFFFF, 0x7A2E2A, 0xE4574F, 0xF08A3C,
                                                     0xF5C542, 0x3FBF7F, 0x1F6B45, 0x4CB8E8, 0x2F6FE0, 0x1B2F5C, 0x9B7BEA, 0xA0703C};
    return k;
}

std::string_view colorName(int index) {
    static constexpr std::string_view k[17] = {"transparent", "noir", "gris fonc\xC3\xA9", "gris", "gris clair", "blanc", "bordeaux", "rouge", "orange",
                                               "jaune", "vert", "vert fonc\xC3\xA9", "cyan", "bleu", "bleu nuit", "violet", "brun"};
    return index >= 0 && index <= 16 ? k[index] : std::string_view("?");
}

char letterOf(int index) { return index >= 0 && index <= 16 ? kLetters[static_cast<std::size_t>(index)] : '.'; }

domain::ProjectIcon blank() {
    domain::ProjectIcon ic;
    ic.palette = defaultPalette();
    ic.pixels.assign(static_cast<std::size_t>(N * N), 0);
    return ic;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> k = [] {
        std::vector<Preset> v;
        for (const auto& e : entries()) v.push_back(e.preset);
        return v;
    }();
    return k;
}

domain::ProjectIcon preset(std::string_view key, std::string_view projectName) {
    for (const auto& e : entries())
        if (key == e.preset.key) {
            auto ic = blank();
            e.draw(ic.pixels, projectName);
            return ic;
        }
    return {};
}

std::string initialsOf(std::string_view projectName) {
    const auto up = plainUpper(projectName);
    const auto alnum = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    std::size_t i = 0;
    while (i < up.size() && !alnum(up[i])) ++i;
    if (i >= up.size()) return "X";
    std::string out(1, up[i]);
    // Apres un separateur ( _ - espace) : la premiere lettre du mot suivant.
    for (std::size_t j = i + 1; j + 1 < up.size(); ++j)
        if ((up[j] == '_' || up[j] == '-' || up[j] == ' ') && alnum(up[j + 1])) return out + up[j + 1];
    // Sinon la majuscule suivante du nom tel qu'il est ecrit ("ArmoireGaz").
    const auto orig = std::string(projectName);
    for (std::size_t j = 1; j < orig.size(); ++j)
        if (orig[j] >= 'A' && orig[j] <= 'Z' && j > i) return out + orig[j];
    for (std::size_t j = i + 1; j < up.size(); ++j)
        if (alnum(up[j])) return out + up[j];
    return out;
}

// ---------------------------------------------------------------- dessiner ----
std::uint8_t at(const domain::ProjectIcon& ic, int x, int y) {
    if (x < 0 || y < 0 || x >= N || y >= N || ic.pixels.size() != static_cast<std::size_t>(N * N)) return 0;
    return ic.pixels[static_cast<std::size_t>(y * N + x)];
}

void put(domain::ProjectIcon& ic, int x, int y, std::uint8_t color, bool mirror) {
    ensure(ic);
    const auto c = static_cast<std::uint8_t>(std::min<int>(color, 16));
    P(ic.pixels, x, y, c);
    if (mirror) P(ic.pixels, N - 1 - x, y, c);
}

void line(domain::ProjectIcon& ic, int x0, int y0, int x1, int y1, std::uint8_t color, bool mirror) {
    ensure(ic);
    Ln(ic.pixels, x0, y0, x1, y1, color);
    if (mirror) Ln(ic.pixels, N - 1 - x0, y0, N - 1 - x1, y1, color);
}

void frame(domain::ProjectIcon& ic, int x0, int y0, int x1, int y1, std::uint8_t color, bool mirror) {
    ensure(ic);
    const auto one = [&](int a0, int b0, int a1, int b1) {
        const int l = std::min(a0, a1), r = std::max(a0, a1), t = std::min(b0, b1), u = std::max(b0, b1);
        Fr(ic.pixels, l, t, r - l + 1, u - t + 1, color);
    };
    one(x0, y0, x1, y1);
    if (mirror) one(N - 1 - x0, y0, N - 1 - x1, y1);
}

void fill(domain::ProjectIcon& ic, int x, int y, std::uint8_t color, bool mirror) {
    ensure(ic);
    const auto flood = [&](int sx, int sy) {
        if (sx < 0 || sy < 0 || sx >= N || sy >= N) return;
        const auto from = ic.pixels[static_cast<std::size_t>(sy * N + sx)];
        if (from == color) return;
        std::vector<std::pair<int, int>> todo{{sx, sy}};
        while (!todo.empty()) {
            const auto [i, j] = todo.back();
            todo.pop_back();
            if (i < 0 || j < 0 || i >= N || j >= N || ic.pixels[static_cast<std::size_t>(j * N + i)] != from) continue;
            ic.pixels[static_cast<std::size_t>(j * N + i)] = color;
            todo.push_back({i + 1, j});
            todo.push_back({i - 1, j});
            todo.push_back({i, j + 1});
            todo.push_back({i, j - 1});
        }
    };
    flood(x, y);
    if (mirror) flood(N - 1 - x, y);
}

bool isBlank(const domain::ProjectIcon& ic) {
    return std::all_of(ic.pixels.begin(), ic.pixels.end(), [](std::uint8_t v) { return v == 0; });
}

// ---------------------------------------------------------------- couleurs ----
std::uint32_t colorOf(const domain::ProjectIcon& ic, int index) {
    if (index <= 0 || index > 16) return 0;
    const auto c = ic.palette[static_cast<std::size_t>(index - 1)];
    const bool unset = std::all_of(ic.palette.begin(), ic.palette.end(), [](std::uint32_t v) { return v == 0; });
    return unset ? defaultPalette()[static_cast<std::size_t>(index - 1)] : c;
}

std::vector<std::uint8_t> rgba(const domain::ProjectIcon& ic) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(N * N * 4), 0);
    if (ic.pixels.size() != static_cast<std::size_t>(N * N)) return out;
    for (std::size_t i = 0; i < ic.pixels.size(); ++i) {
        const int v = ic.pixels[i];
        if (!v) continue;
        const auto c = colorOf(ic, v);
        out[i * 4 + 0] = static_cast<std::uint8_t>((c >> 16) & 0xFF);
        out[i * 4 + 1] = static_cast<std::uint8_t>((c >> 8) & 0xFF);
        out[i * 4 + 2] = static_cast<std::uint8_t>(c & 0xFF);
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> rgba16(const domain::ProjectIcon& ic) {
    std::vector<std::uint8_t> out(16u * 16u * 4u, 0);
    if (ic.pixels.size() != static_cast<std::size_t>(N * N)) return out;
    for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 16; ++i) {
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int v = ic.pixels[static_cast<std::size_t>((j * 2 + dy) * N + i * 2 + dx)];
                    if (!v) continue;
                    const auto c = colorOf(ic, v);
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    ++n;
                }
            const auto o = static_cast<std::size_t>((j * 16 + i) * 4);
            if (!n) continue;
            out[o + 0] = static_cast<std::uint8_t>(r / n);
            out[o + 1] = static_cast<std::uint8_t>(g / n);
            out[o + 2] = static_cast<std::uint8_t>(b / n);
            out[o + 3] = static_cast<std::uint8_t>(255u * n / 4u);
        }
    return out;
}

// ------------------------------------------------------------------ fichier ----
std::string hex(std::uint32_t rgb) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%06X", static_cast<unsigned>(rgb & 0xFFFFFFu));
    return buf;
}

bool parseHex(std::string_view text, std::uint32_t& rgb) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return false;
    std::uint32_t v = 0;
    for (const char c : text) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
    }
    rgb = v;
    return true;
}

std::string toText(const domain::ProjectIcon& ic) {
    std::ostringstream o;
    o << "# Icone du projet : 32 x 32, une lettre par pixel (\".\" : transparent).\n"
      << "# Les couleurs se changent ici, les lettres restent.\n"
      << "taille = 32\n";
    for (int k = 1; k <= 16; ++k) o << "couleur " << letterOf(k) << " = " << hex(colorOf(ic, k)) << '\n';
    o << "pixels\n";
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) o << letterOf(at(ic, x, y));
        o << '\n';
    }
    return o.str();
}

core::Result<domain::ProjectIcon> fromText(std::string_view text) {
    auto ic = blank();
    std::vector<std::string> rows;
    bool inPixels = false;
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        const auto end = text.find('\n', pos);
        std::string_view ln = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++lineNo;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty() || ln.front() == '#') continue;
        if (inPixels) {
            rows.emplace_back(ln);
            continue;
        }
        if (ln == "pixels") { inPixels = true; continue; }
        if (ln.rfind("couleur ", 0) == 0) {
            const auto eq = ln.find('=');
            if (eq == std::string_view::npos || eq < 9) continue;
            const char letter = ln[8];
            const auto k = kLetters.find(static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
            std::uint32_t rgb = 0;
            if (k == std::string_view::npos || k == 0 || !parseHex(ln.substr(eq + 1), rgb))
                return core::fail(core::ErrorCode::InvalidArgument, "config/icone.txt : couleur illisible", std::string(ln));
            ic.palette[k - 1] = rgb;
            continue;
        }
        if (ln.rfind("taille", 0) == 0) {
            const auto eq = ln.find('=');
            if (eq == std::string_view::npos || std::atoi(std::string(ln.substr(eq + 1)).c_str()) != N)
                return core::fail(core::ErrorCode::InvalidArgument, "config/icone.txt : seule la taille 32 se lit", std::string(ln));
            continue;
        }
    }
    if (rows.size() != static_cast<std::size_t>(N))
        return core::fail(core::ErrorCode::InvalidArgument, "config/icone.txt : il faut 32 lignes de pixels",
                          std::to_string(rows.size()) + " lignes");
    for (int y = 0; y < N; ++y) {
        const auto& r = rows[static_cast<std::size_t>(y)];
        if (r.size() != static_cast<std::size_t>(N))
            return core::fail(core::ErrorCode::InvalidArgument, "config/icone.txt : une ligne de pixels n'a pas 32 lettres",
                              "ligne " + std::to_string(y + 1));
        for (int x = 0; x < N; ++x) {
            const auto k = kLetters.find(static_cast<char>(std::toupper(static_cast<unsigned char>(r[static_cast<std::size_t>(x)]))));
            if (k == std::string_view::npos)
                return core::fail(core::ErrorCode::InvalidArgument, "config/icone.txt : lettre inconnue", std::string(1, r[static_cast<std::size_t>(x)]));
            ic.pixels[static_cast<std::size_t>(y * N + x)] = static_cast<std::uint8_t>(k);
        }
    }
    return ic;
}

} // namespace project::icon
