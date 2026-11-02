// =============================================================================
//  app/ai/AiViewImages.cpp - voir AiViewTemplates.hpp : LES IMAGES D'UN MODELE DE VUES (des fonctions pures)
// -----------------------------------------------------------------------------
//  Le cadre de l'ecran dans une page (findScreen), le recadrage et la reduction,
//  les couleurs dominantes (paletteOf), les bandes communes a plusieurs vues
//  (commonBands), les mots des couleurs et des tailles. Sans ecran, sans disque,
//  sans PDFium : des pixels RGBA (hmi::Rgba) en entree, des nombres en sortie.
// =============================================================================
#include "AiViewTemplates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <unordered_map>

namespace app::ai::viewtpl {

namespace {

bool valid(const hmi::Rgba& im) {
    return im.width > 0 && im.height > 0
           && im.pixels.size() >= static_cast<std::size_t>(im.width) * static_cast<std::size_t>(im.height) * 4u;
}

// La couleur d'un pixel posee sur du blanc (une image transparente se lit comme sur la page).
struct Rgb {
    int r{0}, g{0}, b{0};
};
inline Rgb rgbAt(const hmi::Rgba& im, int x, int y) {
    const std::uint8_t* p = im.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(im.width) + static_cast<std::size_t>(x)) * 4u;
    const int a = p[3];
    if (a == 255) return {p[0], p[1], p[2]};
    const auto over = [a](int c) { return (c * a + 255 * (255 - a) + 127) / 255; };
    return {over(p[0]), over(p[1]), over(p[2])};
}
inline bool nonWhite(const Rgb& c) { return std::min({c.r, c.g, c.b}) < 235; }
inline bool darkPx(const Rgb& c) { return c.r + c.g + c.b < 330; }
inline std::uint32_t pack(const Rgb& c) {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | static_cast<std::uint32_t>(c.b);
}
inline Rgb unpack(std::uint32_t v) { return {static_cast<int>((v >> 16) & 0xFF), static_cast<int>((v >> 8) & 0xFF), static_cast<int>(v & 0xFF)}; }
inline int dist2(std::uint32_t a, std::uint32_t b) {
    const Rgb x = unpack(a), y = unpack(b);
    return (x.r - y.r) * (x.r - y.r) + (x.g - y.g) * (x.g - y.g) + (x.b - y.b) * (x.b - y.b);
}

// La part de pixels non blancs et sombres d'une ligne (y, de xa a xb) ou d'une colonne (x, de ya a yb).
struct LineStats {
    double ink{0}, dark{0}, line{0};   // line : les pixels d'un trait (un trait fin anticrenele compris : gris moyen et plus fonce)
};
inline bool linePx(const Rgb& c) { return c.r + c.g + c.b < 450; }
LineStats rowStats(const hmi::Rgba& im, int y, int xa, int xb) {
    LineStats s;
    if (xb <= xa) return s;
    int n = 0, d = 0, l = 0;
    for (int x = xa; x < xb; ++x) {
        const Rgb c = rgbAt(im, x, y);
        n += nonWhite(c) ? 1 : 0;
        d += darkPx(c) ? 1 : 0;
        l += linePx(c) ? 1 : 0;
    }
    s.ink = static_cast<double>(n) / (xb - xa);
    s.dark = static_cast<double>(d) / (xb - xa);
    s.line = static_cast<double>(l) / (xb - xa);
    return s;
}
LineStats colStats(const hmi::Rgba& im, int x, int ya, int yb) {
    LineStats s;
    if (yb <= ya) return s;
    int n = 0, d = 0, l = 0;
    for (int y = ya; y < yb; ++y) {
        const Rgb c = rgbAt(im, x, y);
        n += nonWhite(c) ? 1 : 0;
        d += darkPx(c) ? 1 : 0;
        l += linePx(c) ? 1 : 0;
    }
    s.ink = static_cast<double>(n) / (yb - ya);
    s.dark = static_cast<double>(d) / (yb - ya);
    s.line = static_cast<double>(l) / (yb - ya);
    return s;
}

// Elargir un bord, ligne par ligne, depuis `edge` (dedans) dans le sens `dir` jusqu'a `limit` (compris) : on garde ce qui a de
// l'encre ; on s'arrete sur une bande blanche de `gap` lignes, ou sur une ligne sombre pleine suivie de blanc (le bord du cadre).
template <class Stats>
int grow(int edge, int dir, int limit, int gap, const Stats& stats) {
    int last = edge, white = 0;
    for (int k = edge + dir; dir < 0 ? k >= limit : k <= limit; k += dir) {
        const LineStats s = stats(k);
        if (s.dark >= 0.9) {   // une ligne : le bord si ce qui suit est blanc
            last = k;
            const int beyond = k + dir;
            if (dir < 0 ? beyond < limit : beyond > limit) break;
            if (stats(beyond).ink < 0.02) break;
            white = 0;
            continue;
        }
        if (s.ink < 0.02) {
            if (++white >= gap) break;
        } else {
            white = 0;
            last = k;
        }
    }
    return last;
}

// UN BORD de la capture, depuis `edge` (le bord du coeur, dedans) dans le sens `dir` jusqu'a `limit` (`far` : l'autre bout) : LE CADRE - la ligne pleine
// la plus proche (97 % de pixels d'un trait), meme au-dela d'un fond blanc de la capture (une vue d'architecture) ; une ligne
// separee du contenu par une bande blanche n'est pas le cadre (le tableau de la page) : le bord est alors le contenu. Sans
// ligne : elargi jusqu'a une bande blanche (grow).
template <class Stats, class Continues>
int sideOf(int edge, int dir, int limit, int far, int gap, const Stats& stats, const Continues& continues) {
    const auto inside = [&](int k) { return dir < 0 ? k >= limit : k <= limit; };
    const auto inner = [&](int k) { return dir < 0 ? k <= edge : k >= edge; };   // pas plus loin que le coeur, vers l'interieur
    const auto inImage = [&](int k) { return dir < 0 ? k <= far : k >= far; };    // `far` : l'autre bout de l'image
    for (int k = edge; inside(k); k += dir) {
        if (stats(k).line < 0.97) continue;
        // un trait FIN (au plus 3 lignes, comptees des deux cotes) : une bande sombre pleine de la capture (un en-tete fonce) n'est
        // pas son cadre
        int in = k, out = k;
        for (int n = 0; n < 8 && inImage(in - dir) && stats(in - dir).line >= 0.97; ++n) in -= dir;
        while (inside(out + dir) && stats(out + dir).line >= 0.97) out += dir;
        k = out;   // le trait le plus au bord
        if (std::abs(out - in) + 1 > 3) continue;
        // separe du contenu par une bande blanche : une ligne du tableau de la page, pas le cadre - le bord est le contenu
        int white = 0, j = in - dir;
        while (inner(j) && stats(j).ink < 0.02) {
            ++white;
            j -= dir;
        }
        if (white >= gap) return j;
        // la capture continue juste au-dela (de l'encre aux deux bouts des lignes suivantes : ses cotes, son en-tete) : un trait
        // DANS la capture (le cadre d'un schema colle dans la vue), pas son cadre
        if (continues(out, dir)) continue;
        return out;
    }
    return grow(edge, dir, limit, gap, stats);
}

std::string pctText(double share) {
    char b[16];
    if (share >= 0.095) std::snprintf(b, sizeof b, "%d", static_cast<int>(std::lround(share * 100.0)));
    else std::snprintf(b, sizeof b, "%.1f", share * 100.0);
    std::string s = b;
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

} // namespace

// ---------------------------------------------------------------- le cadre de l'ecran ---
Rect findScreen(const hmi::Rgba& page) {
    if (!valid(page)) return {};
    const int W = page.width, H = page.height;
    // 0. Une capture sans marge (son pourtour n'est pas blanc) : l'image entiere.
    {
        long long white = 0, all = 0;
        const auto look = [&](int x, int y) {
            ++all;
            white += nonWhite(rgbAt(page, x, y)) ? 0 : 1;
        };
        for (int x = 0; x < W; ++x) {
            look(x, 0);
            look(x, H - 1);
        }
        for (int y = 1; y + 1 < H; ++y) {
            look(0, y);
            look(W - 1, y);
        }
        if (white * 10 < all * 6) return {0, 0, W, H};
    }
    // 1. Des blocs : remplis quand la moitie de leurs pixels n'est pas blanche.
    const int B = std::max(2, std::max(W, H) / 250);
    const int gw = (W + B - 1) / B, gh = (H + B - 1) / B;
    std::vector<unsigned char> fill(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0);
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            int n = 0, t = 0;
            for (int y = gy * B; y < std::min(H, (gy + 1) * B); ++y)
                for (int x = gx * B; x < std::min(W, (gx + 1) * B); ++x) {
                    ++t;
                    n += nonWhite(rgbAt(page, x, y)) ? 1 : 0;
                }
            fill[static_cast<std::size_t>(gy) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(gx)] = n * 2 >= t ? 1 : 0;
        }
    // 2. Le plus grand rectangle de blocs remplis (les hauteurs par colonne, une pile).
    std::vector<int> h(static_cast<std::size_t>(gw), 0);
    long long best = 0;
    int bx = 0, by = 0, bw = 0, bh = 0;
    std::vector<std::pair<int, int>> st;   // (debut, hauteur)
    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            auto& hh = h[static_cast<std::size_t>(gx)];
            hh = fill[static_cast<std::size_t>(gy) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(gx)] ? hh + 1 : 0;
        }
        st.clear();
        for (int gx = 0; gx <= gw; ++gx) {
            const int cur = gx < gw ? h[static_cast<std::size_t>(gx)] : 0;
            int start = gx;
            while (!st.empty() && st.back().second >= cur) {
                const auto [s, sh] = st.back();
                st.pop_back();
                const long long area = static_cast<long long>(sh) * (gx - s);
                if (area > best) {
                    best = area;
                    bx = s;
                    by = gy - sh + 1;
                    bw = gx - s;
                    bh = sh;
                }
                start = s;
            }
            st.emplace_back(start, cur);
        }
    }
    if (best <= 0) return {};
    int x0 = bx * B, y0 = by * B, x1 = std::min(W, (bx + bw) * B) - 1, y1 = std::min(H, (by + bh) * B) - 1;
    // 3. Jusqu'au cadre de la capture, sinon jusqu'a une bande blanche (deux passes : les lignes, puis les colonnes, deux fois).
    const int gap = std::max(3, std::min(W, H) / 100);
    // Au-dela d'un trait, la capture continue-t-elle ? Dans les 4 lignes (ou colonnes) suivantes qui ne sont pas des traits, de
    // l'encre pres des DEUX bouts (a 3 % pres).
    const auto ends = [&](bool rows, int k, int a, int b) {
        const int m = std::max(3, (rows ? W : H) * 3 / 100), n = rows ? W : H;
        const auto inkAt = [&](int t) { return rows ? nonWhite(rgbAt(page, t, k)) : nonWhite(rgbAt(page, k, t)); };
        bool lo = false, hi = false;
        for (int t = std::max(0, a - m); t <= std::min(n - 1, a + m) && !lo; ++t) lo = inkAt(t);
        for (int t = std::max(0, b - m); t <= std::min(n - 1, b + m) && !hi; ++t) hi = inkAt(t);
        return lo && hi;
    };
    for (int pass = 0; pass < 2; ++pass) {
        const auto row = [&](int y) { return rowStats(page, y, x0, x1 + 1); };
        const auto rowGoesOn = [&](int out, int dir) {
            for (int y = out + dir, seen = 0; y >= 0 && y < H && seen < 4; y += dir) {
                if (row(y).line >= 0.97) continue;
                ++seen;
                if (ends(true, y, x0, x1)) return true;
            }
            return false;
        };
        const int top = sideOf(y0, -1, 0, H - 1, gap, row, rowGoesOn), bottom = sideOf(y1, +1, H - 1, 0, gap, row, rowGoesOn);
        y0 = top;
        y1 = bottom;
        const auto col = [&](int x) { return colStats(page, x, y0, y1 + 1); };
        const auto colGoesOn = [&](int out, int dir) {
            for (int x = out + dir, seen = 0; x >= 0 && x < W && seen < 4; x += dir) {
                if (col(x).line >= 0.97) continue;
                ++seen;
                if (ends(false, x, y0, y1)) return true;
            }
            return false;
        };
        const int left = sideOf(x0, -1, 0, W - 1, gap, col, colGoesOn), right = sideOf(x1, +1, W - 1, 0, gap, col, colGoesOn);
        x0 = left;
        x1 = right;
    }
    Rect r{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    if (static_cast<double>(r.w) * r.h < 0.04 * W * H) return {};
    return r;
}

hmi::Rgba crop(const hmi::Rgba& img, const Rect& r0) {
    hmi::Rgba out;
    if (!valid(img)) return out;
    Rect r = r0;
    r.x = std::clamp(r.x, 0, img.width);
    r.y = std::clamp(r.y, 0, img.height);
    r.w = std::clamp(r.w, 0, img.width - r.x);
    r.h = std::clamp(r.h, 0, img.height - r.y);
    if (r.empty()) return out;
    out.width = r.w;
    out.height = r.h;
    out.pixels.resize(static_cast<std::size_t>(r.w) * static_cast<std::size_t>(r.h) * 4u);
    for (int y = 0; y < r.h; ++y) {
        const auto* src = img.pixels.data() + (static_cast<std::size_t>(r.y + y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(r.x)) * 4u;
        std::copy(src, src + static_cast<std::size_t>(r.w) * 4u, out.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(r.w) * 4u);
    }
    return out;
}

hmi::Rgba fit(const hmi::Rgba& img, int maxSide) {
    if (!valid(img) || maxSide <= 0 || std::max(img.width, img.height) <= maxSide) return img;
    const double f = static_cast<double>(maxSide) / std::max(img.width, img.height);
    const int nw = std::max(1, static_cast<int>(std::lround(img.width * f)));
    const int nh = std::max(1, static_cast<int>(std::lround(img.height * f)));
    hmi::Rgba out;
    out.width = nw;
    out.height = nh;
    out.pixels.resize(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4u);
    for (int y = 0; y < nh; ++y) {
        const int ya = static_cast<int>(static_cast<long long>(y) * img.height / nh);
        const int yb = std::max(ya + 1, static_cast<int>(static_cast<long long>(y + 1) * img.height / nh));
        for (int x = 0; x < nw; ++x) {
            const int xa = static_cast<int>(static_cast<long long>(x) * img.width / nw);
            const int xb = std::max(xa + 1, static_cast<int>(static_cast<long long>(x + 1) * img.width / nw));
            std::array<long long, 4> sum{0, 0, 0, 0};
            for (int yy = ya; yy < yb; ++yy)
                for (int xx = xa; xx < xb; ++xx) {
                    const auto* p = img.pixels.data() + (static_cast<std::size_t>(yy) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(xx)) * 4u;
                    for (int k = 0; k < 4; ++k) sum[static_cast<std::size_t>(k)] += p[k];
                }
            const long long n = static_cast<long long>(yb - ya) * (xb - xa);
            auto* q = out.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4u;
            for (int k = 0; k < 4; ++k) q[k] = static_cast<std::uint8_t>((sum[static_cast<std::size_t>(k)] + n / 2) / n);
        }
    }
    return out;
}

// ------------------------------------------------------------------ les couleurs ---
std::string hex(std::uint32_t rgb) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02X%02X%02X", static_cast<unsigned>((rgb >> 16) & 0xFF), static_cast<unsigned>((rgb >> 8) & 0xFF),
                  static_cast<unsigned>(rgb & 0xFF));
    return b;
}

std::string colorWord(std::uint32_t rgb) {
    const Rgb c = unpack(rgb);
    const int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    if (mx - mn < 24) {
        if (mx < 40) return "noir";
        if (mx > 240) return "blanc";
        if (mx < 100) return "gris fonc\xC3\xA9";
        if (mx < 180) return "gris";
        return "gris clair";
    }
    // la teinte, en degres
    double hue = 0;
    const double d = mx - mn;
    if (mx == c.r) hue = 60.0 * std::fmod((c.g - c.b) / d + 6.0, 6.0);
    else if (mx == c.g) hue = 60.0 * ((c.b - c.r) / d + 2.0);
    else hue = 60.0 * ((c.r - c.g) / d + 4.0);
    const bool pale = mn >= 110 && mx > 200;
    std::string w;
    if (hue < 15 || hue >= 345) w = mx < 120 ? "bordeaux" : "rouge";
    else if (hue < 40) w = mx < 140 ? "marron" : "orange";
    else if (hue < 70) w = "jaune";
    else if (hue < 160) w = "vert";
    else if (hue < 200) w = "cyan";
    else if (hue < 255) w = "bleu";
    else if (hue < 290) w = "violet";
    else w = "magenta";
    return pale && w != "jaune" ? w + " clair" : w;
}

namespace {

// Le role PROBABLE d'une couleur dans une vue de supervision (le calcul ne le sait pas : l'IA le confirme sur l'image).
std::string roleOf(std::uint32_t rgb) {
    const Rgb c = unpack(rgb);
    const int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    if (mx - mn < 24) {
        if (mx < 50) return "texte, bandeau d'alarmes";
        if (mx > 240) return "en-t\xC3\xAAte, fonds clairs";
        if (mx < 200) return "cadres des valeurs, boutons";
        return "boutons, cadres";
    }
    if (c.r > 170 && c.g < 90 && c.b < 90) return "d\xC3\xA9" "fauts, alarmes";
    if (c.r > 180 && c.g > 170 && c.b < 120) return "valeurs (texte)";
    if (c.g >= c.r + 40 && c.g >= c.b + 40) return "conduites, marche";
    if (c.g > 170 && c.b > 170 && c.r < 170) return "consignes";
    if (c.r > 200 && c.g > 90 && c.g < 190 && c.b < 90) return "symboles, rep\xC3\xA8res";
    if (c.b > c.r + 40 && c.b > c.g + 20) return "titres, libell\xC3\xA9s";
    return {};
}

struct Cluster {
    std::uint32_t rep{0};
    std::uint64_t count{0};
};

} // namespace

Palette paletteOf(const std::vector<const hmi::Rgba*>& imgs, std::size_t maxColors) {
    Palette out;
    std::unordered_map<std::uint32_t, std::uint64_t> counts;
    std::uint64_t total = 0;
    for (const hmi::Rgba* im : imgs) {
        if (!im || !valid(*im)) continue;
        const int step = static_cast<long long>(im->width) * im->height > 1500000 ? 2 : 1;
        for (int y = 0; y < im->height; y += step)
            for (int x = 0; x < im->width; x += step) {
                ++counts[pack(rgbAt(*im, x, y))];
                ++total;
            }
    }
    if (total == 0) return out;
    // Une photo (des centaines de milliers de couleurs) : ramenee a 5 bits par canal avant de regrouper.
    if (counts.size() > 60000) {
        std::unordered_map<std::uint32_t, std::uint64_t> coarse;
        for (const auto& [k, n] : counts) coarse[(k & 0xF8F8F8u) | 0x040404u] += n;
        counts.swap(coarse);
    }
    std::vector<std::pair<std::uint32_t, std::uint64_t>> list(counts.begin(), counts.end());
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    // Les groupes : la couleur la plus frequente represente ceux qui lui ressemblent (a 40 pres : l'anticrenelage, le JPEG).
    constexpr int kNear = 40 * 40;
    std::vector<Cluster> groups;
    for (const auto& [rgb, n] : list) {
        int bestI = -1, bestD = 1 << 30;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const int d = dist2(groups[i].rep, rgb);
            if (d < bestD) {
                bestD = d;
                bestI = static_cast<int>(i);
            }
        }
        if (bestI >= 0 && (bestD <= kNear || groups.size() >= 64)) groups[static_cast<std::size_t>(bestI)].count += n;
        else groups.push_back({rgb, n});
    }
    std::sort(groups.begin(), groups.end(), [](const Cluster& a, const Cluster& b) { return a.count != b.count ? a.count > b.count : a.rep < b.rep; });
    for (const auto& g : groups) {
        const double share = static_cast<double>(g.count) / static_cast<double>(total);
        if (share < 0.003 || out.colors.size() >= std::max<std::size_t>(1, maxColors)) continue;
        out.colors.push_back({g.rep, share, roleOf(g.rep)});
    }
    // Le fond : la couleur (d'un groupe garde) la plus frequente au centre des images.
    std::vector<std::uint64_t> center(out.colors.size(), 0);
    for (const hmi::Rgba* im : imgs) {
        if (!im || !valid(*im) || out.colors.empty()) continue;
        const int step = std::max(1, std::min(im->width, im->height) / 200);
        for (int y = im->height / 5; y < im->height * 4 / 5; y += step)
            for (int x = im->width / 5; x < im->width * 4 / 5; x += step) {
                const std::uint32_t c = pack(rgbAt(*im, x, y));
                std::size_t bi = 0;
                int bd = 1 << 30;
                for (std::size_t i = 0; i < out.colors.size(); ++i) {
                    const int d = dist2(out.colors[i].rgb, c);
                    if (d < bd) {
                        bd = d;
                        bi = i;
                    }
                }
                ++center[bi];
            }
    }
    if (!out.colors.empty()) {
        const auto it = std::max_element(center.begin(), center.end());
        const std::size_t bi = static_cast<std::size_t>(it - center.begin());
        out.background = out.colors[bi].rgb;
        out.backgroundShare = out.colors[bi].share;
        out.colors[bi].role = "fond";
    }
    return out;
}

Palette paletteOf(const hmi::Rgba& img, std::size_t maxColors) { return paletteOf(std::vector<const hmi::Rgba*>{&img}, maxColors); }

// ------------------------------------------------------------- les bandes communes ---
namespace {

// Une image ramenee a w x h (le plus proche voisin) : des couleurs empaquetees.
std::vector<std::uint32_t> sample(const hmi::Rgba& im, int w, int h) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(im.height - 1, static_cast<int>((static_cast<long long>(y) * 2 + 1) * im.height / (2LL * h)));
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(im.width - 1, static_cast<int>((static_cast<long long>(x) * 2 + 1) * im.width / (2LL * w)));
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = pack(rgbAt(im, sx, sy));
        }
    }
    return out;
}

// La part des pixels de la ligne y de a qui se retrouvent dans b a un pixel pres (dans le carre 3 x 3 autour de la meme place : deux
// captures collees a des echelles un peu differentes, recadrees a quelques pixels pres).
double rowMatch(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int w, int h, int y) {
    int same = 0;
    for (int x = 0; x < w; ++x) {
        const Rgb p = unpack(a[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
        bool ok = false;
        for (int dy = -1; dy <= 1 && !ok; ++dy) {
            const int yb = y + dy;
            if (yb < 0 || yb >= h) continue;
            for (int dx = -1; dx <= 1 && !ok; ++dx) {
                const int xb = x + dx;
                if (xb < 0 || xb >= w) continue;
                const Rgb q = unpack(b[static_cast<std::size_t>(yb) * static_cast<std::size_t>(w) + static_cast<std::size_t>(xb)]);
                ok = std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b) <= 48;
            }
        }
        same += ok ? 1 : 0;
    }
    return static_cast<double>(same) / w;
}

// La longueur de la suite de lignes pareilles depuis un bord (`fromTop`), des ecarts de `tol` lignes toleres ; la moitie au plus.
int run(const std::vector<unsigned char>& same, int h, bool fromTop, int tol) {
    int last = -1, bad = 0;
    for (int k = 0; k < h / 2; ++k) {
        const int y = fromTop ? k : h - 1 - k;
        if (same[static_cast<std::size_t>(y)]) {
            last = k;
            bad = 0;
        } else if (++bad > tol) {
            break;
        }
    }
    return last + 1;
}

// Les deux couleurs les plus frequentes des lignes y0 a y1 des vues `which` : la premiere, et la suivante si elle couvre 15 % (sinon -1).
std::pair<std::uint32_t, int> modeOf(const std::vector<std::vector<std::uint32_t>>& imgs, const std::vector<int>& which, int w, int y0, int y1) {
    std::unordered_map<std::uint32_t, int> n;
    int all = 0;
    for (const int i : which)
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                ++n[imgs[static_cast<std::size_t>(i)][static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]];
                ++all;
            }
    std::vector<std::pair<std::uint32_t, int>> list(n.begin(), n.end());
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    if (list.empty()) return {0, -1};
    // la suivante : une couleur franchement differente de la premiere (pas son anticrenelage)
    for (std::size_t k = 1; k < list.size(); ++k) {
        if (list[k].second * 100 < all * 15) break;
        if (dist2(list[k].first, list[0].first) > 60 * 60) return {list[0].first, static_cast<int>(list[k].first)};
    }
    return {list[0].first, -1};
}

} // namespace

Bands commonBands(const std::vector<const hmi::Rgba*>& views) {
    Bands out;
    std::vector<const hmi::Rgba*> ok;
    for (const auto* v : views)
        if (v && valid(*v)) ok.push_back(v);
    out.views = static_cast<int>(ok.size());
    if (ok.size() < 2) return out;
    // Toutes a la meme taille : 320 de large, la hauteur du rapport median.
    std::vector<double> ratios;
    for (const auto* v : ok) ratios.push_back(static_cast<double>(v->height) / v->width);
    std::sort(ratios.begin(), ratios.end());
    const int w = 320;
    const int h = std::max(16, static_cast<int>(std::lround(w * ratios[ratios.size() / 2])));
    std::vector<std::vector<std::uint32_t>> img;
    for (const auto* v : ok) img.push_back(sample(*v, w, h));
    const std::size_t n = img.size();
    // same[i][j][y] : la ligne y de i se retrouve dans j (70 % de ses pixels, a un pixel pres)
    std::vector<std::vector<std::vector<unsigned char>>> same(n, std::vector<std::vector<unsigned char>>(n));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j) {
            std::vector<unsigned char> s(static_cast<std::size_t>(h), 0);
            for (int y = 0; y < h; ++y) s[static_cast<std::size_t>(y)] = rowMatch(img[i], img[j], w, h, y) >= 0.7 ? 1 : 0;
            same[i][j] = s;
            same[j][i] = std::move(s);
        }
    // Chaque bande, avec la vue de reference qui la partage avec le plus d'autres vues (une vue a part - un accueil, une vue
    // d'architecture - ne la donne pas aux autres) ; sa hauteur : celle qu'atteint au moins la moitie des autres vues.
    const int tol = std::max(1, h / 50);
    const int minBand = std::max(2, h / 50);
    struct Best {
        int              height{0};
        std::vector<int> views;
    };
    const auto bandFor = [&](bool fromTop) {
        Best best;
        for (std::size_t r = 0; r < n; ++r) {
            std::vector<std::pair<int, int>> runs;   // (vue, longueur)
            for (std::size_t j = 0; j < n; ++j)
                if (j != r) runs.emplace_back(static_cast<int>(j), run(same[r][j], h, fromTop, tol));
            std::vector<int> lengths;
            for (const auto& x : runs) lengths.push_back(x.second);
            std::sort(lengths.begin(), lengths.end(), std::greater<int>());
            const int band = lengths[(lengths.size() - 1) / 2];
            if (band < minBand) continue;
            std::vector<int> sharing{static_cast<int>(r)};
            for (const auto& [j, len] : runs)
                if (len * 10 >= band * 9) sharing.push_back(j);
            if (sharing.size() > best.views.size() || (sharing.size() == best.views.size() && band > best.height)) best = {band, std::move(sharing)};
        }
        return best;
    };
    if (const Best top = bandFor(true); top.height > 0) {
        out.header = static_cast<double>(top.height) / h;
        out.headerViews = static_cast<int>(top.views.size());
        std::tie(out.headerColor, out.headerAccent) = modeOf(img, top.views, w, 0, top.height);
    }
    if (const Best bottom = bandFor(false); bottom.height > 0) {
        out.footer = static_cast<double>(bottom.height) / h;
        out.footerViews = static_cast<int>(bottom.views.size());
        std::tie(out.footerColor, out.footerAccent) = modeOf(img, bottom.views, w, h - bottom.height, h);
    }
    return out;
}

double inkRatio(const hmi::Rgba& page) {
    if (!valid(page)) return 0;
    const int xa = page.width * 6 / 100, xb = page.width * 94 / 100, ya = page.height * 12 / 100, yb = page.height * 82 / 100;
    if (xb <= xa || yb <= ya) return 0;
    std::vector<unsigned char> lineRow(static_cast<std::size_t>(page.height), 0), lineCol(static_cast<std::size_t>(page.width), 0);
    for (int y = ya; y < yb; ++y) lineRow[static_cast<std::size_t>(y)] = rowStats(page, y, xa, xb).dark >= 0.5 ? 1 : 0;
    for (int x = xa; x < xb; ++x) lineCol[static_cast<std::size_t>(x)] = colStats(page, x, ya, yb).dark >= 0.5 ? 1 : 0;
    long long ink = 0, all = 0;
    for (int y = ya; y < yb; ++y) {
        if (lineRow[static_cast<std::size_t>(y)]) continue;
        for (int x = xa; x < xb; ++x) {
            if (lineCol[static_cast<std::size_t>(x)]) continue;
            ++all;
            ink += darkPx(rgbAt(page, x, y)) ? 1 : 0;
        }
    }
    return all > 0 ? static_cast<double>(ink) / static_cast<double>(all) : 0.0;
}

// ------------------------------------------------------------- les mots des tailles ---
std::string ratioText(int w, int h) {
    if (w <= 0 || h <= 0) return {};
    const double r = static_cast<double>(w) / h;
    struct Known {
        double      v;
        const char* text;
    };
    static constexpr Known kKnown[] = {{4.0 / 3.0, "4:3"}, {16.0 / 9.0, "16:9"}, {16.0 / 10.0, "16:10"}, {5.0 / 4.0, "5:4"},
                                       {3.0 / 2.0, "3:2"}, {3.0 / 4.0, "3:4"},   {9.0 / 16.0, "9:16"}};
    const Known* best = nullptr;
    for (const auto& k : kKnown)
        if (std::abs(k.v - r) <= 0.06 && (!best || std::abs(k.v - r) < std::abs(best->v - r))) best = &k;
    if (best) return best->text;
    char b[24];
    std::snprintf(b, sizeof b, "%.2f:1", r);
    std::string s = b;
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

std::pair<int, int> likelySize(double w96, double h96) {
    if (w96 <= 0 || h96 <= 0) return {0, 0};
    static constexpr std::pair<int, int> kSizes[] = {{640, 480},   {800, 480},   {800, 600},   {1024, 600},  {1024, 768}, {1280, 720},
                                                     {1280, 800},  {1280, 1024}, {1366, 768},  {1440, 900},  {1600, 900}, {1600, 1200},
                                                     {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}};
    const double r = w96 / h96;
    const std::pair<int, int>* best = nullptr;
    for (const auto& s : kSizes) {
        const double sr = static_cast<double>(s.first) / s.second;
        if (std::abs(sr - r) > 0.06 || std::abs(s.first - w96) > 0.15 * s.first) continue;
        if (!best || std::abs(s.first - w96) < std::abs(best->first - w96)) best = &s;
    }
    if (best) return *best;
    return {static_cast<int>(std::lround(w96 / 10.0)) * 10, static_cast<int>(std::lround(h96 / 10.0)) * 10};
}

std::string percentText(double share) { return pctText(share); }

} // namespace app::ai::viewtpl
