// =============================================================================
//  ui/widgets/Globe.cpp - 1.47.0 : le globe pointille qui tourne (voir Globe.hpp)
// -----------------------------------------------------------------------------
//  Trois parties : les calculs (le masque des terres, la projection, la taille
//  des marques) ; le petit rasteriseur (le disque et ses points dans une image,
//  les marques, les anneaux et les glyphes en petites textures sur-echantillonnees) ;
//  le widget (le temps, la souris, les groupes de villes, les noms, l'infobulle).
// =============================================================================
#include "Globe.hpp"

#include "../Zoom.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <utility>

namespace ui {

namespace globe {

namespace {

#include "GlobeLand.inc"

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
constexpr double kCell = 360.0 / kLandCols;   // 0,25 degre

struct LandMask {
    std::vector<std::uint64_t> bits;
    int                        cells{0};
};

// La table decodee une fois : un bit par case, du nord au sud, de -180 a +180.
const LandMask& landMask() {
    static const LandMask mask = [] {
        LandMask out;
        out.bits.assign((static_cast<std::size_t>(kLandCols) * static_cast<std::size_t>(kLandRows) + 63u) / 64u, 0u);
        int value[256];
        std::fill(std::begin(value), std::end(value), -1);
        for (int i = 0; kLandAlphabet[i] != '\0'; ++i) value[static_cast<unsigned char>(kLandAlphabet[i])] = i;
        const char* p = kLandRuns;
        const auto next = [&]() -> int {
            if (*p == '\0') return -1;
            const int v = value[static_cast<unsigned char>(*p++)];
            if (v < kLandOneChar) return v;   // -1 : un signe hors de l'alphabet
            if (*p == '\0') return -1;
            const int lo = value[static_cast<unsigned char>(*p++)];
            return lo < 0 ? -1 : ((v - kLandOneChar) << 6) | lo;
        };
        for (int row = 0; row < kLandRows; ++row) {
            int  col = 0;
            bool land = false;
            while (col < kLandCols) {
                const int v = next();
                if (v < 0) return out;   // une table abimee : ce qui est lu (l'essai compte les cases)
                if (land) {
                    for (int c = col; c < std::min(kLandCols, col + v); ++c) {
                        const std::size_t bit = static_cast<std::size_t>(row) * kLandCols + static_cast<std::size_t>(c);
                        out.bits[bit >> 6] |= std::uint64_t{1} << (bit & 63u);
                        ++out.cells;
                    }
                }
                col += v;
                land = !land;
            }
        }
        return out;
    }();
    return mask;
}

} // namespace

const std::array<LayerStyle, kLayerCount>& styles() noexcept {
    // Les couleurs et les formes de globe.js (validees par validate_palette.js) ; l'ordre de la legende.
    static const std::array<LayerStyle, kLayerCount> list = {{
        {Layer::Clients, "clients", "Clients", "Clients", "la ville de leurs PC", gfx::Color::rgb(0x2a78d6), Shape::Disc},
        {Layer::ActivePcs, "actifs", "PC actifs maintenant", "PC actifs", "vus il y a moins de 15 min", gfx::Color::rgb(0x1baf7a), Shape::Pulse},
        {Layer::Outdated, "pas_a_jour", "Clients pas \xC3\xA0 jour", "Pas \xC3\xA0 jour", "un PC pas \xC3\xA0 jour", gfx::Color::rgb(0xeb6834), Shape::Triangle},
        {Layer::Admins, "admins", "Admins", "Admins", "leur derni\xC3\xA8re connexion", gfx::Color::rgb(0x6d4aff), Shape::Star},
        {Layer::Discoveries, "decouvertes", "D\xC3\xA9" "couvertes en cours", "D\xC3\xA9" "couvertes", "l'essai, pas encore achet\xC3\xA9",
         gfx::Color::rgb(0x008300), Shape::Ring},
        {Layer::Orders, "commandes", "Commandes", "Commandes", "pay\xC3\xA9" "es dans la p\xC3\xA9riode", gfx::Color::rgb(0xeda100), Shape::Square},
        {Layer::Requests, "demandes", "Demandes en attente", "Demandes", "re\xC3\xA7ues, pas encore trait\xC3\xA9" "es", gfx::Color::rgb(0xe87ba4),
         Shape::Diamond},
    }};
    return list;
}

const LayerStyle& style(Layer l) noexcept { return styles()[index(l)]; }

bool layerFromKey(std::string_view key, Layer& out) noexcept {
    for (const auto& s : styles())
        if (key == s.key) {
            out = s.layer;
            return true;
        }
    return false;
}

Projected project(const View& v, double lat, double lon) noexcept {
    // Monde : x vers (0 N, 90 E), y le nord, z vers (0 N, 0 E). Puis le lacet (la longitude de la vue), le tangage
    // (sa latitude) : le point (v.lat, v.lon) vient au centre, (0, 0, 1).
    const double a = lat * kRad, b = lon * kRad;
    const double x = std::cos(a) * std::sin(b), y = std::sin(a), z = std::cos(a) * std::cos(b);
    const double cl = std::cos(v.lon * kRad), sl = std::sin(v.lon * kRad), cp = std::cos(v.lat * kRad), sp = std::sin(v.lat * kRad);
    const double x1 = x * cl - z * sl, z1 = x * sl + z * cl;
    const double y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
    return {x1, y2, z2};
}

bool unproject(const View& v, double x, double y, double& lat, double& lon) noexcept {
    const double r2 = x * x + y * y;
    if (r2 > 1.0) return false;
    const double z2 = std::sqrt(1.0 - r2);
    const double cl = std::cos(v.lon * kRad), sl = std::sin(v.lon * kRad), cp = std::cos(v.lat * kRad), sp = std::sin(v.lat * kRad);
    const double wy = y * cp + z2 * sp, z1 = -y * sp + z2 * cp;
    const double wx = x * cl + z1 * sl, wz = -x * sl + z1 * cl;
    lat = std::asin(std::clamp(wy, -1.0, 1.0)) / kRad;
    lon = std::atan2(wx, wz) / kRad;
    return true;
}

double normalizeLon(double lon) noexcept {
    double l = std::fmod(lon + 180.0, 360.0);
    if (l < 0.0) l += 360.0;
    return l - 180.0;
}

double shortestTurn(double fromLon, double toLon) noexcept { return normalizeLon(toLon - fromLon); }

bool isLand(double lat, double lon) noexcept {
    const int row = std::clamp(static_cast<int>(std::floor((90.0 - lat) / kCell)), 0, kLandRows - 1);
    const int col = std::clamp(static_cast<int>(std::floor((normalizeLon(lon) + 180.0) / kCell)), 0, kLandCols - 1);
    const std::size_t bit = static_cast<std::size_t>(row) * kLandCols + static_cast<std::size_t>(col);
    return ((landMask().bits[bit >> 6] >> (bit & 63u)) & 1u) != 0;
}

int landCellCount() noexcept { return landMask().cells; }
int landCellCountExpected() noexcept { return kLandCells; }

const std::vector<float>& landDots(float stepDeg) {
    static std::map<int, std::vector<float>> cache;
    const float step = std::clamp(stepDeg, 0.3f, 20.f);
    const int   key = static_cast<int>(std::lround(step * 1000.f));
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::vector<float> out;
    // Comme globe.js (grille) : des rangees de latitude, autant de points par degre a chaque latitude ; une rangee sur
    // deux decalee d'un demi-pas.
    for (int k = 0;; ++k) {
        const double lat = -90.0 + step * 0.5 + step * k;
        if (lat >= 90.0) break;
        const int n = std::max(1, static_cast<int>(std::lround(360.0 * std::cos(lat * kRad) / step)));
        const double dec = static_cast<double>(std::lround((lat + 90.0) / step) % 2) * 0.5;
        for (int i = 0; i < n; ++i) {
            const double lon = -180.0 + (i + dec) * 360.0 / n;
            if (!isLand(lat, lon)) continue;
            const double a = lat * kRad, b = lon * kRad;
            out.push_back(static_cast<float>(std::cos(a) * std::sin(b)));
            out.push_back(static_cast<float>(std::sin(a)));
            out.push_back(static_cast<float>(std::cos(a) * std::cos(b)));
        }
    }
    return cache.emplace(key, std::move(out)).first->second;
}

float markerRadius(int count) noexcept {
    if (count <= 0) return 0.f;
    // La maquette (globe.js, taille) : 2,4 + 1,55 x racine(n), 9 au plus - la legende : 1 -> 8 px de large, 4 -> 11,
    // 9 -> 14, 18 et plus -> 18.
    return std::min(9.f, 2.4f + 1.55f * std::sqrt(static_cast<float>(count)));
}

std::string formatCount(int n) {
    if (n < 1000) return std::to_string(n);
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(n) / 1000.0);
    std::string s = b;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s + " k";
}

std::vector<Place> samplePlaces() {
    struct Row {
        const char* city;
        const char* country;
        double      lon, lat;
        const char* region;
        int         clients, pcs, active, outdated, admins, discoveries, orders, requests;
    };
    // globe.js, V (la maquette) : ville, pays, longitude, latitude, region, puis clients, PC, actifs, pas a jour,
    // admins, Decouvertes, commandes (30 j), demandes.
    static const Row rows[] = {
        {"Lyon", "FR", 4.85, 45.76, "Auvergne-Rh\xC3\xB4ne-Alpes", 9, 64, 9, 2, 1, 1, 3, 1},
        {"Paris", "FR", 2.35, 48.86, "\xC3\x8Ele-de-France", 8, 55, 7, 2, 1, 1, 3, 1},
        {"Toulouse", "FR", 1.44, 43.6, "Occitanie", 4, 29, 3, 1, 0, 0, 1, 0},
        {"Bruxelles", "BE", 4.35, 50.85, "Bruxelles-Capitale", 4, 24, 4, 1, 0, 1, 2, 0},
        {"Montr\xC3\xA9" "al", "CA", -73.57, 45.5, "Qu\xC3\xA9" "bec", 3, 23, 1, 1, 0, 2, 1, 0},
        {"Gen\xC3\xA8ve", "CH", 6.14, 46.2, "Gen\xC3\xA8ve", 3, 19, 2, 0, 0, 1, 1, 0},
        {"Nantes", "FR", -1.55, 47.22, "Pays de la Loire", 3, 17, 2, 0, 0, 0, 1, 0},
        {"Casablanca", "MA", -7.59, 33.57, "Casablanca-Settat", 2, 14, 2, 1, 0, 1, 0, 1},
        {"Lille", "FR", 3.06, 50.63, "Hauts-de-France", 2, 13, 2, 0, 0, 0, 0, 0},
        {"Bordeaux", "FR", -0.58, 44.84, "Nouvelle-Aquitaine", 2, 12, 1, 0, 0, 0, 1, 0},
        {"Marseille", "FR", 5.37, 43.3, "Provence-Alpes-C\xC3\xB4te d'Azur", 2, 11, 2, 1, 0, 0, 0, 0},
        {"Grenoble", "FR", 5.72, 45.19, "Auvergne-Rh\xC3\xB4ne-Alpes", 2, 10, 1, 0, 1, 0, 0, 0},
        {"Strasbourg", "FR", 7.75, 48.58, "Grand Est", 2, 9, 1, 0, 0, 0, 0, 0},
        {"Luxembourg", "LU", 6.13, 49.61, "Luxembourg", 1, 7, 1, 0, 0, 0, 1, 0},
        {"Lausanne", "CH", 6.63, 46.52, "Vaud", 1, 6, 1, 0, 0, 0, 0, 0},
        {"Tunis", "TN", 10.18, 36.81, "Tunis", 1, 6, 1, 1, 0, 0, 0, 0},
        {"Li\xC3\xA8ge", "BE", 5.57, 50.63, "Wallonie", 1, 5, 1, 0, 0, 0, 0, 1},
        {"Rennes", "FR", -1.68, 48.11, "Bretagne", 1, 5, 0, 0, 0, 0, 0, 0},
        {"Dakar", "SN", -17.47, 14.72, "Dakar", 1, 5, 1, 1, 0, 0, 0, 1},
        {"Qu\xC3\xA9" "bec", "CA", -71.21, 46.81, "Qu\xC3\xA9" "bec", 1, 4, 0, 0, 0, 1, 0, 0},
        {"Abidjan", "CI", -4.02, 5.36, "Abidjan", 1, 4, 0, 0, 0, 1, 0, 0},
    };
    std::vector<Place> out;
    for (const auto& r : rows) {
        Place p;
        p.city = r.city;
        p.country = r.country;
        p.id = p.country + "|" + p.city;
        p.region = r.region;
        p.lat = r.lat;
        p.lon = r.lon;
        p.pcs = r.pcs;
        p.setCount(Layer::Clients, r.clients);
        p.setCount(Layer::ActivePcs, r.active);
        p.setCount(Layer::Outdated, r.outdated);
        p.setCount(Layer::Admins, r.admins);
        p.setCount(Layer::Discoveries, r.discoveries);
        p.setCount(Layer::Orders, r.orders);
        p.setCount(Layer::Requests, r.requests);
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace globe

// =================================================================== le dessin ==
namespace {

using gfx::Color;
using gfx::Point;
using gfx::Rect;
using globe::Layer;
using globe::Shape;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
constexpr double kFrame = 1.0 / 30.0;     // la vue avance a 30 images par seconde au plus
constexpr double kMaxStep = 0.1;          // un pas de temps (une page revenue, une image lente) : 0,1 s au plus
constexpr double kPuffSeconds = 1.6;      // une bouffee (En direct)
constexpr Color  kInk{15, 23, 42, 255};       // #0f172a
constexpr Color  kLabel{51, 65, 85, 255};     // #334155
constexpr Color  kTipText{203, 213, 225, 255};
constexpr Color  kTipFaint{148, 163, 184, 255};
constexpr Color  kGlyphOff{184, 194, 208, 255};

// ---- un peu de geometrie pour les formes ----
struct Vec {
    float x, y;
};
bool insidePolygon(const std::vector<Vec>& p, float x, float y) {
    bool in = false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    }
    return in;
}
float segmentDistance(Vec a, Vec b, float x, float y) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0.f ? ((x - a.x) * dx + (y - a.y) * dy) / l2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float ex = a.x + t * dx - x, ey = a.y + t * dy - y;
    return std::sqrt(ex * ex + ey * ey);
}
// Dans le polygone grossi de `grow` (les joints arrondis d'un trait de 2 x grow).
bool insideGrown(const std::vector<Vec>& p, float grow, float x, float y) {
    if (insidePolygon(p, x, y)) return true;
    if (grow <= 0.f) return false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if (segmentDistance(p[j], p[i], x, y) <= grow) return true;
    return false;
}
bool insideRoundRect(float x0, float y0, float x1, float y1, float rad, float x, float y) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    const float cx = std::clamp(x, x0 + rad, x1 - rad), cy = std::clamp(y, y0 + rad, y1 - rad);
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

// Le contour d'une forme de calque, centree en 0, de << rayon >> r (globe.js, forme).
std::vector<Vec> shapePolygon(Shape s, float r) {
    std::vector<Vec> p;
    if (s == Shape::Triangle) {
        const float h = r * 1.25f;
        p = {{0.f, -h}, {h * 0.95f, h * 0.62f}, {-h * 0.95f, h * 0.62f}};
    } else if (s == Shape::Diamond) {
        const float h = r * 1.2f;
        p = {{0.f, -h}, {h, 0.f}, {0.f, h}, {-h, 0.f}};
    } else if (s == Shape::Star) {
        const float o = r * 1.3f, in = r * 0.52f;
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(-kPi / 2 + k * kPi / 4), d = (k % 2) ? in : o;
            p.push_back({d * std::cos(a), d * std::sin(a)});
        }
    }
    return p;
}

// Une couleur premultipliee (un echantillon).
struct Pm {
    float r{0}, g{0}, b{0}, a{0};
};
void over(Pm& dst, Color c, float alpha) {
    const float a = alpha * (static_cast<float>(c.a) / 255.f);
    if (a <= 0.f) return;
    dst.r = static_cast<float>(c.r) / 255.f * a + dst.r * (1.f - a);
    dst.g = static_cast<float>(c.g) / 255.f * a + dst.g * (1.f - a);
    dst.b = static_cast<float>(c.b) / 255.f * a + dst.b * (1.f - a);
    dst.a = a + dst.a * (1.f - a);
}

// Une image w x h : 4 x 4 echantillons par pixel, `paint` compose l'echantillon (x, y) (le repere de l'image).
std::vector<std::uint8_t> supersample(int w, int h, const std::function<void(float, float, Pm&)>& paint) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0u);
    constexpr int kS = 4;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            Pm acc;
            for (int sj = 0; sj < kS; ++sj)
                for (int si = 0; si < kS; ++si) {
                    Pm s;
                    paint(static_cast<float>(i) + (static_cast<float>(si) + 0.5f) / kS, static_cast<float>(j) + (static_cast<float>(sj) + 0.5f) / kS, s);
                    acc.r += s.r; acc.g += s.g; acc.b += s.b; acc.a += s.a;
                }
            const float n = 1.f / (kS * kS);
            acc.r *= n; acc.g *= n; acc.b *= n; acc.a *= n;
            std::uint8_t* o = &px[(static_cast<std::size_t>(j) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)) * 4u];
            if (acc.a > 0.f) {
                o[0] = static_cast<std::uint8_t>(std::lround(std::clamp(acc.r / acc.a, 0.f, 1.f) * 255.f));
                o[1] = static_cast<std::uint8_t>(std::lround(std::clamp(acc.g / acc.a, 0.f, 1.f) * 255.f));
                o[2] = static_cast<std::uint8_t>(std::lround(std::clamp(acc.b / acc.a, 0.f, 1.f) * 255.f));
                o[3] = static_cast<std::uint8_t>(std::lround(std::clamp(acc.a, 0.f, 1.f) * 255.f));
            }
        }
    return px;
}

// ---- les petites textures : marques, anneaux, glyphes (une fois par renderer) ----
enum class SpriteKind : std::uint8_t { Marker, Ring, Glyph };
struct Sprite {
    gfx::TextureId tex{};
    int            size{0};   // carre size x size ; le centre de la forme au milieu
};
using SpriteKey = std::tuple<const gfx::IRenderer*, int, int, std::uint32_t, int, int>;
std::map<SpriteKey, Sprite>& sprites() {
    static std::map<SpriteKey, Sprite> cache;
    return cache;
}
std::uint32_t packColor(Color c) {
    return (static_cast<std::uint32_t>(c.r) << 24) | (static_cast<std::uint32_t>(c.g) << 16) | (static_cast<std::uint32_t>(c.b) << 8) | c.a;
}

// Les textures du globe d'un widget detruit, rendues au prochain dessin d'un globe sur le meme renderer.
std::vector<std::pair<const gfx::IRenderer*, gfx::TextureId>>& orphans() {
    static std::vector<std::pair<const gfx::IRenderer*, gfx::TextureId>> list;
    return list;
}

const Sprite& makeSprite(gfx::IRenderer& drawing, SpriteKind kind, Shape shape, Color color, int a, int b) {
    gfx::IRenderer& r = ui::unzoomed(drawing);   // les textures : celles du renderer de la surface
    auto& cache = sprites();
    const SpriteKey key{&r, static_cast<int>(kind), static_cast<int>(shape), packColor(color), a, b};
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    if (cache.size() > 800) {   // borne : on repart de zero (les tailles changent avec le zoom)
        for (auto it = cache.begin(); it != cache.end();) {
            if (std::get<0>(it->first) == &r) {
                if (it->second.tex.v) r.releaseImage(it->second.tex);
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    Sprite s;
    const Color white{255, 255, 255, 255};
    if (kind == SpriteKind::Marker) {
        // La marque d'un calque (globe.js, marque) : un tour blanc de 2 px (un trait de 4 px), puis la forme pleine ;
        // l'anneau : un disque blanc, puis l'anneau ; le pouls : un point blanc au milieu.
        const float rad = static_cast<float>(a) / 4.f;
        const int   half = static_cast<int>(std::ceil(rad * 1.35f + 3.f));
        s.size = 2 * half;
        const float c = static_cast<float>(half);
        const auto poly = shapePolygon(shape, rad);
        const float q = std::min(2.5f, rad * 0.3f), hq = rad * 0.9f;
        const float lw = std::max(2.2f, rad * 0.42f);
        const auto px = supersample(s.size, s.size, [&](float x, float y, Pm& o) {
            const float dx = x - c, dy = y - c, d = std::sqrt(dx * dx + dy * dy);
            switch (shape) {
                case Shape::Ring:
                    if (d <= rad + 2.f) over(o, white, 1.f);
                    if (std::fabs(d - (rad - lw / 2.f + 0.5f)) <= lw / 2.f) over(o, color, 1.f);
                    break;
                case Shape::Disc:
                case Shape::Pulse:
                    if (d <= rad + 2.f) over(o, white, 1.f);
                    if (d <= rad) over(o, color, 1.f);
                    if (shape == Shape::Pulse && d <= std::max(1.4f, rad * 0.32f)) over(o, white, 1.f);
                    break;
                case Shape::Square:
                    if (insideRoundRect(-hq - 2.f, -hq - 2.f, hq + 2.f, hq + 2.f, q + 2.f, dx, dy)) over(o, white, 1.f);
                    if (insideRoundRect(-hq, -hq, hq, hq, q, dx, dy)) over(o, color, 1.f);
                    break;
                default:
                    if (insideGrown(poly, 2.f, dx, dy)) over(o, white, 1.f);
                    if (insidePolygon(poly, dx, dy)) over(o, color, 1.f);
                    break;
            }
        });
        s.tex = r.createImage(px.data(), s.size, s.size);
    } else if (kind == SpriteKind::Ring) {
        // Un anneau de rayon a / 2 et de largeur b / 10 (le halo des PC actifs, une bouffee, la ville choisie).
        const float rad = static_cast<float>(a) / 2.f, w = static_cast<float>(b) / 10.f;
        const int   half = static_cast<int>(std::ceil(rad + w / 2.f + 2.f));
        s.size = 2 * half;
        const float c = static_cast<float>(half);
        const auto px = supersample(s.size, s.size, [&](float x, float y, Pm& o) {
            const float dx = x - c, dy = y - c, d = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(d - rad) <= w / 2.f) over(o, color, 1.f);
        });
        s.tex = r.createImage(px.data(), s.size, s.size);
    } else {
        // Le glyphe de la legende (globe.js, SVGF : une boite de 14), a `a` pixels de cote.
        s.size = std::max(4, a);
        const float k = static_cast<float>(s.size) / 14.f;
        std::vector<Vec> poly;
        if (shape == Shape::Triangle) poly = {{7.f, 1.6f}, {12.6f, 11.4f}, {1.4f, 11.4f}};
        else if (shape == Shape::Diamond) poly = {{7.f, 0.8f}, {13.2f, 7.f}, {7.f, 13.2f}, {0.8f, 7.f}};
        else if (shape == Shape::Star) poly = {{7.f, 0.6f}, {8.6f, 5.4f}, {13.4f, 7.f}, {8.6f, 8.6f}, {7.f, 13.4f}, {5.4f, 8.6f}, {0.6f, 7.f}, {5.4f, 5.4f}};
        const auto px = supersample(s.size, s.size, [&](float x, float y, Pm& o) {
            const float u = x / k, v = y / k, du = u - 7.f, dv = v - 7.f, d = std::sqrt(du * du + dv * dv);
            switch (shape) {
                case Shape::Disc: if (d <= 5.f) over(o, color, 1.f); break;
                case Shape::Pulse:
                    if (d <= 5.f) over(o, color, 1.f);
                    if (d <= 1.7f) over(o, white, 1.f);
                    break;
                case Shape::Ring: if (std::fabs(d - 4.3f) <= 1.3f) over(o, color, 1.f); break;
                case Shape::Square: if (insideRoundRect(2.f, 2.f, 12.f, 12.f, 2.f, u, v)) over(o, color, 1.f); break;
                default: if (insidePolygon(poly, u, v)) over(o, color, 1.f); break;
            }
        });
        s.tex = r.createImage(px.data(), s.size, s.size);
    }
    return cache.emplace(key, s).first->second;
}

void drawSprite(gfx::IRenderer& r, const Sprite& s, float x, float y, float alpha) {
    if (!s.tex.v || alpha <= 0.003f) return;
    const float h = static_cast<float>(s.size) / 2.f;
    const Rect dst{std::round(x - h), std::round(y - h), static_cast<float>(s.size), static_cast<float>(s.size)};
    r.drawImage(s.tex, dst, 0.f, false, false, Color{255, 255, 255, static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.f, 1.f) * 255.f))});
}

void drawMarker(gfx::IRenderer& r, Layer l, float x, float y, float rad, float alpha) {
    const auto& st = globe::style(l);
    const Sprite& s = makeSprite(r, SpriteKind::Marker, st.shape, st.color, static_cast<int>(std::lround(rad * 4.f)), 0);
    if (s.tex.v) drawSprite(r, s, x, y, alpha);
    else r.fillRoundedRect({x - rad, y - rad, 2 * rad, 2 * rad}, st.color.withAlpha(static_cast<std::uint8_t>(alpha * 255.f)), rad);
}

void drawRing(gfx::IRenderer& r, Color c, float x, float y, float rad, float width, float alpha) {
    if (rad <= 0.5f) return;
    const Sprite& s = makeSprite(r, SpriteKind::Ring, Shape::Disc, c, static_cast<int>(std::lround(rad * 2.f)), static_cast<int>(std::lround(width * 10.f)));
    drawSprite(r, s, x, y, alpha);
}

// Une couleur droite (non premultipliee) posee sur un pixel RGBA droit.
inline void blendPixel(std::uint8_t* p, float cr, float cg, float cb, float a) {
    if (a <= 0.f) return;
    const float da = static_cast<float>(p[3]) / 255.f;
    const float oa = a + da * (1.f - a);
    if (oa <= 0.f) return;
    const float k = da * (1.f - a);
    p[0] = static_cast<std::uint8_t>(std::lround((cr * a + static_cast<float>(p[0]) * k) / oa));
    p[1] = static_cast<std::uint8_t>(std::lround((cg * a + static_cast<float>(p[1]) * k) / oa));
    p[2] = static_cast<std::uint8_t>(std::lround((cb * a + static_cast<float>(p[2]) * k) / oa));
    p[3] = static_cast<std::uint8_t>(std::lround(oa * 255.f));
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

// ================================================================== le widget ==
struct GlobeView::Impl {
    std::vector<globe::Place> places;
    globe::LayerSet           layers = globe::LayerSet::of({Layer::Clients, Layer::ActivePcs, Layer::Outdated});
    globe::View               view{38.0, 4.85, 1.f};
    double                    homeLat{38.0}, homeLon{4.85};
    bool                      autoRotate{true}, frozen{false}, compact{false};
    float                     speed{4.f};

    struct Anim {
        double lon0, lat0, dLon, lat1;
        float  z0, z1;
        double start, duration;
    };
    std::optional<Anim> anim;
    double              vLon{0.0}, vLat{0.0};   // l'elan (degres par seconde)
    double              clock{0.0};             // le temps du globe (advance)
    float               shift{0.f}, shiftTarget{0.f};   // le decalage du centre (setCentreShift)
    double              lastStep{-1.0};         // l'heure de la peinture du dernier pas

    // la souris
    using Clock = std::chrono::steady_clock;
    bool              pressing{false}, dragging{false};
    Point             pressAt{}, lastAt{};
    Clock::time_point lastMove{};

    // choisir, survoler, en direct
    int                      selected{-1};
    std::optional<Point>     leader;
    std::vector<Rect>        reserved;
    int                      hover{-1};
    Point                    hoverPos{};
    bool                     forcedHover{false};
    core::Connection         hoverLink;   // la souris sortie du widget : le survol (meme celui d'une capture) s'oublie
    struct Puff {
        int    place;
        double start;
    };
    std::vector<Puff> puffs;

    // les groupes de villes et leurs marques, pour la vue du moment
    struct Group {
        int                                   lead{-1};
        std::vector<int>                      members;
        float                                 x{}, y{}, z{}, r{};
        std::array<int, globe::kLayerCount>   n{};
    };
    struct Item {
        int   group;
        Layer layer;
        float x, y, r, alpha;
        int   order;
    };
    std::vector<Group> groups;
    std::vector<Item>  items;
    bool               groupsDirty{true};

    // les images : le disque (halo et degrade : refait quand la taille change, ou le zoom de plus de 15 %), et les
    // points des terres (refaits quand la vue change)
    Rect                      lastBounds{};
    std::vector<std::uint8_t> bg, dots;
    gfx::TextureId            bgTex{}, dotsTex{};
    const gfx::IRenderer*     owner{nullptr};
    Rect                      bgRect{}, dotsRect{};
    Point                     bgCentre{};   // le centre du disque quand son image a ete faite
    float                     bgZoom{0.f};
    bool                      dotsDirty{true}, bgDirty{true};

    [[nodiscard]] float margin() const { return compact ? 10.f : 26.f; }

    // (plus bas)
    void checkBounds(const GlobeView& self);
    void ensureGroups(const GlobeView& self);
    [[nodiscard]] int  hitGroup(Point p) const;   // l'index du groupe sous `p` ; -1
    [[nodiscard]] int  groupWith(int place) const;
    void setHover(GlobeView& self, int place);
    void buildBackground(gfx::IRenderer& r, const GlobeView& self);
    void buildDots(gfx::IRenderer& r, const GlobeView& self);
    void drawLabels(gfx::IRenderer& r, const GlobeView& self);
    void drawTooltip(gfx::IRenderer& r, const GlobeView& self);
    [[nodiscard]] std::vector<std::pair<Layer, int>> tipLines(const Group& g) const;
};

GlobeView::GlobeView(std::string id) : Widget(std::move(id)), d_(std::make_unique<Impl>()) {
    d_->hoverLink = hoverChanged->connect([this](bool inside) {
        if (inside) return;
        d_->forcedHover = false;
        if (!d_->pressing) d_->setHover(*this, -1);
    });
}

GlobeView::~GlobeView() {
    if (d_->owner) {
        if (d_->bgTex.v) orphans().emplace_back(d_->owner, d_->bgTex);
        if (d_->dotsTex.v) orphans().emplace_back(d_->owner, d_->dotsTex);
    }
}

// ---- les donnees ----
void GlobeView::setPlaces(std::vector<globe::Place> places) {
    d_->places = std::move(places);
    if (d_->selected >= static_cast<int>(d_->places.size())) d_->selected = -1;
    d_->hover = -1;
    d_->puffs.clear();
    d_->groupsDirty = true;
    invalidate();
}
const std::vector<globe::Place>& GlobeView::places() const noexcept { return d_->places; }
int GlobeView::findPlace(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < d_->places.size(); ++i)
        if (d_->places[i].id == id) return static_cast<int>(i);
    return -1;
}
void GlobeView::setLayers(globe::LayerSet layers) {
    if (d_->layers == layers) return;
    d_->layers = layers;
    d_->groupsDirty = true;
    invalidate();
}
globe::LayerSet GlobeView::layers() const noexcept { return d_->layers; }
void GlobeView::setLayerVisible(Layer l, bool on) {
    globe::LayerSet s = d_->layers;
    s.set(l, on);
    setLayers(s);
}

// ---- la vue ----
void GlobeView::setView(double lat, double lon, float zoom) {
    d_->anim.reset();
    d_->vLon = d_->vLat = 0.0;
    d_->view.lat = std::clamp(lat, globe::kMinLat, globe::kMaxLat);
    d_->view.lon = globe::normalizeLon(lon);
    d_->view.zoom = std::clamp(zoom, globe::kMinZoom, globe::kMaxZoom);
    d_->dotsDirty = d_->groupsDirty = true;
    invalidate();
}
globe::View GlobeView::view() const noexcept { return d_->view; }

void GlobeView::turnTo(double lat, double lon, float zoom, double seconds) {
    const float z1 = zoom > 0.f ? std::clamp(zoom, globe::kMinZoom, globe::kMaxZoom) : d_->view.zoom;
    const double lat1 = std::clamp(lat, -60.0, 70.0);
    if (d_->frozen || seconds <= 0.0) {
        setView(lat1, lon, z1);
        return;
    }
    d_->vLon = d_->vLat = 0.0;
    d_->anim = Impl::Anim{d_->view.lon, d_->view.lat, globe::shortestTurn(d_->view.lon, lon), lat1, d_->view.zoom, z1, d_->clock, seconds};
    invalidate();
}
void GlobeView::setZoom(float zoom) {
    const float z = std::clamp(zoom, globe::kMinZoom, globe::kMaxZoom);
    if (z == d_->view.zoom) return;
    if (d_->anim) d_->anim->z1 = d_->anim->z0 = z;
    d_->view.zoom = z;
    d_->dotsDirty = d_->groupsDirty = true;
    invalidate();
}
void GlobeView::zoomBy(float factor) { setZoom(d_->view.zoom * factor); }
void GlobeView::setHome(double lat, double lon) {
    d_->homeLat = lat;
    d_->homeLon = lon;
}
void GlobeView::recenter() { turnTo(d_->homeLat, d_->homeLon, 1.f, 1.0); }
void GlobeView::setAutoRotate(bool on) { d_->autoRotate = on; }
bool GlobeView::autoRotate() const noexcept { return d_->autoRotate; }
void GlobeView::setRotationSpeed(float degreesPerSecond) { d_->speed = std::max(0.f, degreesPerSecond); }
bool GlobeView::frozen() const noexcept { return d_->frozen; }
void GlobeView::setFrozen(bool on) {
    d_->frozen = on;
    if (on) {
        if (d_->anim) {   // la fin du mouvement, tout de suite
            const auto a = *d_->anim;
            setView(a.lat1, a.lon0 + a.dLon, a.z1);
        }
        d_->vLon = d_->vLat = 0.0;
    }
    invalidate();
}
bool GlobeView::compact() const noexcept { return d_->compact; }
void GlobeView::setCompact(bool on) {
    if (d_->compact == on) return;
    d_->compact = on;
    d_->speed = on ? 9.f : 4.f;
    d_->bgDirty = d_->dotsDirty = d_->groupsDirty = true;
    invalidate();
}

bool GlobeView::moving() const noexcept {
    if (d_->anim || std::fabs(d_->vLon) > 0.5 || std::fabs(d_->vLat) > 0.5) return true;
    return d_->autoRotate && !d_->frozen && !d_->dragging && !hovered() && !d_->forcedHover && d_->selected < 0 && d_->speed > 0.f;
}

// ---- choisir, survoler, en direct ----
void GlobeView::setSelected(int placeIndex) {
    const int s = placeIndex >= 0 && placeIndex < static_cast<int>(d_->places.size()) ? placeIndex : -1;
    if (s == d_->selected) return;
    d_->selected = s;
    d_->groupsDirty = true;
    invalidate();
}
int GlobeView::selected() const noexcept { return d_->selected; }
void GlobeView::setLeaderTarget(std::optional<Point> target) { d_->leader = target; }
void GlobeView::setReservedAreas(std::vector<Rect> areas) { d_->reserved = std::move(areas); }
void GlobeView::pulse(int placeIndex) {
    if (placeIndex < 0 || placeIndex >= static_cast<int>(d_->places.size())) return;
    d_->puffs.push_back({placeIndex, d_->clock});
    if (d_->puffs.size() > 64) d_->puffs.erase(d_->puffs.begin());
    invalidate();
}

void GlobeView::advance(double seconds) {
    const double dt = std::clamp(seconds, 0.0, kMaxStep);
    d_->clock += dt;
    Impl& d = *d_;
    const globe::View before = d.view;
    if (d.anim) {
        const auto& a = *d.anim;
        const double u = a.duration > 0.0 ? std::min(1.0, (d.clock - a.start) / a.duration) : 1.0;
        const double e = u < 0.5 ? 4.0 * u * u * u : 1.0 - std::pow(-2.0 * u + 2.0, 3.0) / 2.0;
        d.view.lon = globe::normalizeLon(a.lon0 + a.dLon * e);
        d.view.lat = a.lat0 + (a.lat1 - a.lat0) * e;
        d.view.zoom = static_cast<float>(a.z0 + (a.z1 - a.z0) * e);
        if (u >= 1.0) d.anim.reset();
    } else if (!d.dragging && !d.frozen) {
        if (std::fabs(d.vLon) > 0.5 || std::fabs(d.vLat) > 0.5) {   // l'elan apres un lancer
            d.view.lon = globe::normalizeLon(d.view.lon + d.vLon * dt);
            d.view.lat = std::clamp(d.view.lat + d.vLat * dt, globe::kMinLat, globe::kMaxLat);
            const double f = std::pow(0.04, dt);
            d.vLon *= f;
            d.vLat *= f;
        } else {
            d.vLon = d.vLat = 0.0;
            if (moving()) d.view.lon = globe::normalizeLon(d.view.lon - static_cast<double>(d.speed) * dt);
        }
    }
    if (d.shift != d.shiftTarget) {   // le centre glisse vers sa place (une demi-seconde environ)
        const float k = d.frozen ? 1.f : static_cast<float>(std::min(1.0, dt * 7.0));
        d.shift += (d.shiftTarget - d.shift) * k;
        if (std::fabs(d.shiftTarget - d.shift) < 0.5f) d.shift = d.shiftTarget;
        d.dotsDirty = d.groupsDirty = true;
        invalidate();
    }
    if (!d.frozen) {
        d.puffs.erase(std::remove_if(d.puffs.begin(), d.puffs.end(), [&](const Impl::Puff& p) { return d.clock - p.start >= kPuffSeconds; }),
                      d.puffs.end());
    }
    if (before.lat != d.view.lat || before.lon != d.view.lon || before.zoom != d.view.zoom) {
        d.dotsDirty = d.groupsDirty = true;
        invalidate();
    }
}

// ---- la geometrie ----
Point GlobeView::centre() const noexcept {
    const Rect b = bounds();
    return {b.x + b.w / 2.f + d_->shift, b.y + b.h / 2.f};
}

void GlobeView::setCentreShift(float dx) {
    if (dx == d_->shiftTarget) return;
    d_->shiftTarget = dx;
    d_->bgDirty = true;   // l'image du disque couvre l'ancien centre et le nouveau (buildBackground)
    if (d_->frozen) {
        d_->shift = dx;
        d_->dotsDirty = d_->groupsDirty = true;
    }
    invalidate();
}
float GlobeView::centreShift() const noexcept { return d_->shiftTarget; }
float GlobeView::radius() const noexcept {
    const Rect b = bounds();
    const float r0 = std::max(8.f, std::min(b.w, b.h) / 2.f - d_->margin());
    return r0 * d_->view.zoom;
}


namespace {
// La place fixe des calques autour du premier (globe.js, SLOT), en degres (0 a droite, 90 en bas).
float slotAngle(Layer l) {
    switch (l) {
        case Layer::ActivePcs: return -45.f;
        case Layer::Outdated: return 45.f;
        case Layer::Admins: return -135.f;
        case Layer::Discoveries: return 135.f;
        case Layer::Orders: return 0.f;
        case Layer::Requests: return 180.f;
        case Layer::Clients: return -90.f;
    }
    return 0.f;
}
} // namespace

// Un autre cadre : tout se refait (l'image du disque, les points, les groupes).
void GlobeView::Impl::checkBounds(const GlobeView& self) {
    const Rect b = self.bounds();
    if (b.x == lastBounds.x && b.y == lastBounds.y && b.w == lastBounds.w && b.h == lastBounds.h) return;
    lastBounds = b;
    bgDirty = dotsDirty = groupsDirty = true;
}

void GlobeView::onLayout() { d_->checkBounds(*this); }

SizeHint GlobeView::sizeHint() const { return SizeHint{{480.f, 480.f}, {96.f, 96.f}, 1.f, 1.f}; }

// ---- les groupes de villes et leurs marques (globe.js, dessine) ----
void GlobeView::Impl::ensureGroups(const GlobeView& self) {
    if (!groupsDirty) return;
    groupsDirty = false;
    groups.clear();
    items.clear();
    const Point c = self.centre();
    const float R = self.radius();
    std::vector<Layer> ks;
    for (const auto& st : globe::styles())
        if (layers.has(st.layer)) ks.push_back(st.layer);
    struct Seen {
        int   i;
        float x, y, z;
        int   weight;
    };
    std::vector<Seen> seen;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const auto& pl = places[i];
        const auto p = globe::project(view, pl.lat, pl.lon);
        if (p.z <= -0.02) continue;
        int w = 0;
        for (Layer k : ks) w += std::max(0, pl.count(k));
        if (w <= 0) continue;
        seen.push_back({static_cast<int>(i), c.x + R * static_cast<float>(p.x), c.y - R * static_cast<float>(p.y), static_cast<float>(p.z), w});
    }
    // La plus grande d'abord (elle porte le groupe) ; a egalite, le plus de PC.
    std::stable_sort(seen.begin(), seen.end(), [&](const Seen& a, const Seen& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        return places[static_cast<std::size_t>(a.i)].pcs > places[static_cast<std::size_t>(b.i)].pcs;
    });
    const float threshold = compact ? 9.f : 20.f;
    for (const auto& v : seen) {
        Group* into = nullptr;
        if (v.i != selected)
            for (auto& q : groups)
                if (q.lead != selected && std::hypot(q.x - v.x, q.y - v.y) < threshold) {
                    into = &q;
                    break;
                }
        const auto& pl = places[static_cast<std::size_t>(v.i)];
        if (into) {
            into->members.push_back(v.i);
            for (Layer k : ks) into->n[globe::index(k)] += std::max(0, pl.count(k));
        } else {
            Group q;
            q.lead = v.i;
            q.members = {v.i};
            q.x = v.x;
            q.y = v.y;
            q.z = v.z;
            for (Layer k : ks) q.n[globe::index(k)] = std::max(0, pl.count(k));
            groups.push_back(std::move(q));
        }
    }
    // Les marques : le premier calque coche au centre, les autres a leur place fixe autour (toujours la meme).
    const float sc = (compact ? 0.55f : 1.f) * std::pow(view.zoom, 0.2f);
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        auto& q = groups[gi];
        const float al = std::clamp((q.z + 0.02f) / 0.22f, 0.f, 1.f);
        bool first = true;
        float r0 = 0.f;
        for (Layer k : ks) {
            const int n = q.n[globe::index(k)];
            if (n <= 0) continue;
            const float r = globe::markerRadius(n) * sc;
            if (first) {
                r0 = q.r = r;
                items.push_back({static_cast<int>(gi), k, q.x, q.y, r, al, 0});
                first = false;
            } else {
                const float a = static_cast<float>(slotAngle(k) * kRad), dist = (r0 + r) * 0.66f;
                items.push_back({static_cast<int>(gi), k, q.x + dist * std::cos(a), q.y + dist * std::sin(a), r, al, 1});
            }
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.r > b.r;
    });
}

int GlobeView::Impl::hitGroup(Point p) const {
    int   best = -1;
    float bd = 1e9f;
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& q = groups[gi];
        if (q.z < 0.05f) continue;
        const float d = std::hypot(q.x - p.x, q.y - p.y);
        if (d < std::max(10.f, q.r + 5.f) && d < bd) {
            bd = d;
            best = static_cast<int>(gi);
        }
    }
    return best;
}

int GlobeView::Impl::groupWith(int place) const {
    for (std::size_t gi = 0; gi < groups.size(); ++gi)
        for (int m : groups[gi].members)
            if (m == place) return static_cast<int>(gi);
    return -1;
}

void GlobeView::Impl::setHover(GlobeView& self, int place) {
    if (hover == place) return;
    hover = place;
    self.hoverPlaceChanged->emit(place);
    self.invalidate();
}

// ---- l'image du disque : le halo, puis le disque blanc a peine ombre (globe.js, dessine) ----
void GlobeView::Impl::buildBackground(gfx::IRenderer& r, const GlobeView& self) {
    bgDirty = false;
    if (bgTex.v) r.releaseImage(bgTex);
    bgTex = {};
    const Rect  b = self.bounds();
    const Point c = self.centre();
    const float R = self.radius(), m = margin();
    // Le cadre du halo, coupe au widget agrandi de 20 % autour du centre : l'image sert telle quelle, agrandie ou
    // reduite, tant que le zoom ne bouge pas de plus de 15 %.
    const float ex = b.w * 0.6f + std::max(std::fabs(shift), std::fabs(shiftTarget)) + 2.f, ey = b.h * 0.6f + 2.f;
    const float x0 = std::floor(std::max(c.x - R - m, c.x - ex)), x1 = std::ceil(std::min(c.x + R + m, c.x + ex));
    const float y0 = std::floor(std::max(c.y - R - m, c.y - ey)), y1 = std::ceil(std::min(c.y + R + m, c.y + ey));
    const int   W = std::min(4096, static_cast<int>(x1 - x0)), H = std::min(4096, static_cast<int>(y1 - y0));
    bgZoom = view.zoom;
    bgCentre = c;
    if (W <= 0 || H <= 0) return;
    bgRect = {x0, y0, static_cast<float>(W), static_cast<float>(H)};
    bg.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 0u);
    const float ox = c.x - x0, oy = c.y - y0, hy = oy + R * 0.02f;
    const float h0 = R * 0.92f, h1 = R + m * 0.95f, hOut = R + m;
    // Le degrade du disque (createRadialGradient de deux cercles : (-0,32 R, -0,38 R) rayon 0,1 R, vers le disque) :
    // pour chaque pixel, le plus grand t dont le cercle intermediaire passe par lui.
    const float fx = ox - 0.32f * R, fy = oy - 0.38f * R, fr = 0.1f * R;
    const float cdx = ox - fx, cdy = oy - fy, dr = R - fr;
    const float qa = cdx * cdx + cdy * cdy - dr * dr;
    for (int j = 0; j < H; ++j) {
        const float py = static_cast<float>(j) + 0.5f;
        for (int i = 0; i < W; ++i) {
            const float px = static_cast<float>(i) + 0.5f;
            std::uint8_t* p = &bg[(static_cast<std::size_t>(j) * static_cast<std::size_t>(W) + static_cast<std::size_t>(i)) * 4u];
            const float dh = std::hypot(px - ox, py - hy);
            if (dh <= hOut) {
                const float t = (dh - h0) / (h1 - h0);
                const float a = t <= 0.f ? 0.20f : t < 0.35f ? lerp(0.20f, 0.08f, t / 0.35f) : t < 1.f ? lerp(0.08f, 0.f, (t - 0.35f) / 0.65f) : 0.f;
                const float edge = std::clamp(hOut + 0.5f - dh, 0.f, 1.f);
                p[0] = 100; p[1] = 116; p[2] = 139;
                p[3] = static_cast<std::uint8_t>(std::lround(a * edge * 255.f));
            }
            const float d = std::hypot(px - ox, py - oy);
            const float cov = std::clamp(R + 0.5f - d, 0.f, 1.f);
            if (cov > 0.f) {
                const float pdx = px - fx, pdy = py - fy;
                const float qb = pdx * cdx + pdy * cdy + fr * dr, qc = pdx * pdx + pdy * pdy - fr * fr;
                const float disc = qb * qb - qa * qc;
                float t = disc >= 0.f && qa != 0.f ? (qb - std::sqrt(disc)) / qa : 0.f;
                t = std::clamp(t, 0.f, 1.f);
                float cr, cg, cb;
                if (t < 0.7f) {
                    const float u = t / 0.7f;
                    cr = lerp(255.f, 251.f, u); cg = lerp(255.f, 252.f, u); cb = lerp(255.f, 254.f, u);
                } else {
                    const float u = (t - 0.7f) / 0.3f;
                    cr = lerp(251.f, 238.f, u); cg = lerp(252.f, 242.f, u); cb = lerp(254.f, 248.f, u);
                }
                blendPixel(p, cr, cg, cb, cov);
            }
            const float sc = std::clamp(1.f - std::fabs(d - R), 0.f, 1.f) * 0.06f;
            if (sc > 0.f) blendPixel(p, 15.f, 23.f, 42.f, sc);
        }
    }
    bgTex = r.createImage(bg.data(), W, H);
    if (!bgTex.v) bg.clear();
}

// ---- l'image des points des terres (3 paquets d'opacite selon la distance au bord) ----
void GlobeView::Impl::buildDots(gfx::IRenderer& r, const GlobeView& self) {
    dotsDirty = false;
    if (dotsTex.v) r.releaseImage(dotsTex);
    dotsTex = {};
    const Rect  b = self.bounds();
    const Point c = self.centre();
    const float R = self.radius();
    const float x0 = std::floor(std::max(b.x, c.x - R - 2.f)), x1 = std::ceil(std::min(b.right(), c.x + R + 2.f));
    const float y0 = std::floor(std::max(b.y, c.y - R - 2.f)), y1 = std::ceil(std::min(b.bottom(), c.y + R + 2.f));
    const int   W = std::min(4096, static_cast<int>(x1 - x0)), H = std::min(4096, static_cast<int>(y1 - y0));
    if (W <= 0 || H <= 0) return;
    dotsRect = {x0, y0, static_cast<float>(W), static_cast<float>(H)};
    dots.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 0u);
    const auto& P = globe::landDots(compact ? 4.f : 1.1f);
    const float R0 = R / view.zoom;
    // Le rayon d'un point (globe.js : 1,3 px, plus pour un grand globe) ; un globe plus petit que celui de la planche
    // (R0 < 250 : une fenetre etroite) a des points un peu plus fins - le meme rapport point / ecart que la planche.
    const float pr0 = (compact ? 1.15f : 1.3f) * (compact ? 1.f : std::max(0.82f, R0 / 250.f)) * std::pow(view.zoom, 0.6f);
    const float colR = compact ? 160.f : 170.f, colG = compact ? 174.f : 183.f, colB = compact ? 196.f : 203.f;
    const float cl = static_cast<float>(std::cos(view.lon * kRad)), sl = static_cast<float>(std::sin(view.lon * kRad));
    const float cp = static_cast<float>(std::cos(view.lat * kRad)), sp = static_cast<float>(std::sin(view.lat * kRad));
    const float ox = c.x - x0, oy = c.y - y0;
    for (std::size_t k = 0; k + 2 < P.size(); k += 3) {
        const float x = P[k], y = P[k + 1], z = P[k + 2];
        const float x1r = x * cl - z * sl, z1 = x * sl + z * cl;
        const float y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
        if (z2 <= 0.02f) continue;
        const float sx = ox + R * x1r, sy = oy - R * y2;
        if (sx < -4.f || sy < -4.f || sx > static_cast<float>(W) + 4.f || sy > static_cast<float>(H) + 4.f) continue;
        const float A = z2 > 0.45f ? 1.f : z2 > 0.2f ? 0.72f : 0.42f;
        const float rr = pr0 * (0.55f + 0.45f * std::sqrt(z2));
        const int   i0 = std::max(0, static_cast<int>(std::floor(sx - rr - 1.f))), i1 = std::min(W - 1, static_cast<int>(std::ceil(sx + rr + 1.f)));
        const int   j0 = std::max(0, static_cast<int>(std::floor(sy - rr - 1.f))), j1 = std::min(H - 1, static_cast<int>(std::ceil(sy + rr + 1.f)));
        for (int j = j0; j <= j1; ++j) {
            const float dy = static_cast<float>(j) + 0.5f - sy;
            for (int i = i0; i <= i1; ++i) {
                const float dx = static_cast<float>(i) + 0.5f - sx;
                const float cov = std::clamp(rr + 0.5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
                if (cov <= 0.f) continue;
                blendPixel(&dots[(static_cast<std::size_t>(j) * static_cast<std::size_t>(W) + static_cast<std::size_t>(i)) * 4u], colR, colG, colB, A * cov);
            }
        }
    }
    dotsTex = r.createImage(dots.data(), W, H);
}

// ---- les noms des villes (<< Paris +6 >>), sans chevauchement ; sinon un badge : combien de villes ----
void GlobeView::Impl::drawLabels(gfx::IRenderer& r, const GlobeView& self) {
    const Rect b = self.bounds();
    const gfx::FontId font = gfx::semibold(gfx::FontId{11});
    std::vector<Rect> taken = reserved;
    for (const auto& it : items) taken.push_back({it.x - it.r - 2.f, it.y - it.r - 2.f, 2.f * it.r + 4.f, 2.f * it.r + 4.f});
    const auto overlaps = [](const Rect& a, const Rect& c) { return a.x < c.right() && a.right() > c.x && a.y < c.bottom() && a.bottom() > c.y; };
    const auto freeBox = [&](const Rect& box) {
        if (box.x < b.x + 2.f || box.right() > b.right() - 2.f || box.y < b.y + 2.f || box.bottom() > b.bottom() - 2.f) return false;
        for (const auto& t : taken)
            if (overlaps(box, t)) return false;
        return true;
    };
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& q = groups[gi];
        if (q.z < 0.25f || q.lead == selected) continue;
        float e = q.r;
        for (const auto& it : items)
            if (it.group == static_cast<int>(gi)) e = std::max(e, std::hypot(it.x - q.x, it.y - q.y) + it.r);
        e += 5.f;
        const std::size_t others = q.members.size() - 1;
        const std::string t = places[static_cast<std::size_t>(q.lead)].city + (others ? " +" + std::to_string(others) : std::string());
        const auto tm = r.measure(t, font);
        const float alpha = std::min(1.f, (q.z - 0.2f) * 5.f);
        std::optional<Rect> spot;
        float tx = 0.f;
        for (int side : {1, -1}) {
            const float x = side > 0 ? q.x + e : q.x - e - tm.width;
            const Rect box{x - 3.f, q.y - 8.f, tm.width + 6.f, 16.f};
            if (freeBox(box)) {
                spot = box;
                tx = x;
                break;
            }
        }
        if (spot) {
            taken.push_back(*spot);
            const float ty = std::round(q.y - tm.height / 2.f);
            const Color halo{255, 255, 255, static_cast<std::uint8_t>(std::lround(235.f * alpha))};
            static const float kHalo[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}, {-2, 0}, {2, 0}, {0, -2}, {0, 2}};
            for (const auto& o : kHalo) r.drawText({std::round(tx) + o[0], ty + o[1]}, t, font, halo);
            r.drawText({std::round(tx), ty}, t, font, kLabel.withAlpha(static_cast<std::uint8_t>(std::lround(255.f * alpha))));
        } else if (others) {
            const std::string bt = std::to_string(q.members.size());
            const gfx::FontId bf = gfx::semibold(gfx::FontId{9});
            const auto bm = r.measure(bt, bf);
            const float bw = std::max(13.f, bm.width + 7.f);
            const float bx = q.x - q.r * 0.8f - 7.f, by = q.y - q.r * 0.8f - 6.f;
            const Rect box{std::round(bx - bw / 2.f), std::round(by - 6.5f), std::round(bw), 13.f};
            r.fillRoundedRect(box, Color{148, 163, 184, static_cast<std::uint8_t>(std::lround(255.f * alpha))}, 6.5f);
            r.fillRoundedRect(box.inset(1.f, 1.f), Color{255, 255, 255, static_cast<std::uint8_t>(std::lround(255.f * alpha))}, 5.5f);
            r.drawText({std::round(box.x + (box.w - bm.width) / 2.f), std::round(box.y + (box.h - bm.height) / 2.f)}, bt, bf,
                       kLabel.withAlpha(static_cast<std::uint8_t>(std::lround(255.f * alpha))));
        }
    }
}

std::vector<std::pair<Layer, int>> GlobeView::Impl::tipLines(const Group& g) const {
    std::vector<std::pair<Layer, int>> out;
    for (const auto& st : globe::styles())
        if (layers.has(st.layer) && g.n[globe::index(st.layer)] > 0) out.emplace_back(st.layer, g.n[globe::index(st.layer)]);
    return out;
}

namespace {
std::string nearbyText(std::size_t others) {
    const bool many = others > 1;
    return "+ " + std::to_string(others) + (many ? " villes proches" : " ville proche");
}
// Les autres villes d'un groupe : trois noms au plus, puis << et 6 autres >>.
std::string othersText(const std::vector<globe::Place>& places, const std::vector<int>& members) {
    std::string s;
    const std::size_t shown = std::min<std::size_t>(members.size(), 4);
    for (std::size_t i = 1; i < shown; ++i) {
        if (i > 1) s += ", ";
        s += places[static_cast<std::size_t>(members[i])].city;
    }
    if (members.size() > shown) s += " et " + std::to_string(members.size() - shown) + " autre" + (members.size() - shown > 1 ? "s" : "");
    return s + " \xC2\xB7 un clic pour zoomer";
}
} // namespace

// ---- l'infobulle du survol (globe.css, #gtip) : sombre, la ville, puis le nombre de chaque calque montre ----
void GlobeView::Impl::drawTooltip(gfx::IRenderer& r, const GlobeView& self) {
    if (hover < 0 || compact) return;
    const int gi = groupWith(hover);
    if (gi < 0) return;
    const auto& g = groups[static_cast<std::size_t>(gi)];
    if (g.z < 0.05f) return;
    const auto lines = tipLines(g);
    const std::size_t others = g.members.size() - 1;
    const gfx::FontId fTitle = gfx::semibold(gfx::FontId{12}), fText = gfx::FontId{11}, fNum = gfx::semibold(gfx::FontId{11});
    const std::string title = places[static_cast<std::size_t>(g.lead)].label();
    const std::string small = others ? nearbyText(others) : std::string();
    const std::string group = others ? othersText(places, g.members) : std::string();
    float w = r.measure(title, fTitle).width + (others ? 6.f + r.measure(small, fText).width : 0.f);
    for (const auto& [l, n] : lines)
        w = std::max(w, 14.f + 6.f + r.measure(globe::style(l).name, fText).width + 12.f + r.measure(globe::formatCount(n), fNum).width);
    if (others) w = std::max(w, r.measure(group, fText).width);
    const float lineH = 17.f;
    const float bw = std::ceil(w + 20.f);
    const float bh = 7.f + 17.f + 2.f + lineH * static_cast<float>(lines.size()) + (others ? 17.f : 0.f) + 6.f;
    const Rect  b = self.bounds();
    float left = std::min(hoverPos.x + 14.f, b.right() - bw - 8.f);
    left = std::max(b.x + 8.f, left);
    const float top = std::max(b.y + 8.f, hoverPos.y - bh - 10.f);
    const Rect box{std::round(left), std::round(top), bw, std::round(bh)};
    r.fillRoundedRect({box.x, box.y + 2.f, box.w, box.h}, Color{15, 23, 42, 40}, 9.f);   // une ombre legere
    r.fillRoundedRect(box, kInk, 9.f);
    float y = box.y + 7.f;
    const Color white{255, 255, 255, 255};
    const float th = r.measure(title, fTitle).height;
    r.drawText({box.x + 10.f, y}, title, fTitle, white);
    if (others) {
        const auto sm = r.measure(small, fText);
        r.drawText({box.x + 10.f + r.measure(title, fTitle).width + 6.f, y + th - sm.height}, small, fText, kTipFaint);
    }
    y += 17.f + 2.f;
    for (const auto& [l, n] : lines) {
        const std::string count = globe::formatCount(n);
        const auto tm = r.measure(globe::style(l).name, fText);
        GlobeView::drawGlyph(r, l, {box.x + 10.f, y + (lineH - 14.f) / 2.f - 1.f, 14.f, 14.f});
        r.drawText({box.x + 10.f + 14.f + 6.f, std::round(y + (lineH - tm.height) / 2.f - 1.f)}, globe::style(l).name, fText, kTipText);
        const auto cm = r.measure(count, fNum);
        r.drawText({box.right() - 10.f - cm.width, std::round(y + (lineH - cm.height) / 2.f - 1.f)}, count, fNum, white);
        y += lineH;
    }
    if (others) {
        const auto gm = r.measure(group, fText);
        r.drawText({box.x + 10.f, std::round(y + (lineH - gm.height) / 2.f)}, group, fText, kTipFaint);
    }
}

// ---- les essais, les captures ----
int GlobeView::placeAt(Point p) const {
    d_->ensureGroups(*this);
    const int g = d_->hitGroup(p);
    return g < 0 ? -1 : d_->groups[static_cast<std::size_t>(g)].lead;
}
std::vector<int> GlobeView::groupOf(int placeIndex) const {
    d_->ensureGroups(*this);
    const int g = d_->groupWith(placeIndex);
    std::vector<int> out;
    if (g < 0) return out;
    for (int m : d_->groups[static_cast<std::size_t>(g)].members)
        if (m != placeIndex) out.push_back(m);
    return out;
}
std::optional<Point> GlobeView::screenOf(int placeIndex) const {
    d_->ensureGroups(*this);
    const int g = d_->groupWith(placeIndex);
    if (g < 0) return std::nullopt;
    const auto& q = d_->groups[static_cast<std::size_t>(g)];
    if (q.z <= 0.f) return std::nullopt;
    return Point{q.x, q.y};
}
void GlobeView::hoverAt(Point p) {
    d_->forcedHover = true;
    d_->hoverPos = p;
    d_->ensureGroups(*this);
    const int g = d_->hitGroup(p);
    d_->setHover(*this, g < 0 ? -1 : d_->groups[static_cast<std::size_t>(g)].lead);
}
int GlobeView::hoveredPlace() const noexcept { return d_->hover; }
std::string GlobeView::hoverText() const {
    if (d_->hover < 0) return {};
    d_->ensureGroups(*this);
    const int gi = d_->groupWith(d_->hover);
    if (gi < 0) return {};
    const auto& g = d_->groups[static_cast<std::size_t>(gi)];
    const std::size_t others = g.members.size() - 1;
    std::string s = d_->places[static_cast<std::size_t>(g.lead)].label();
    if (others) s += " " + nearbyText(others);
    for (const auto& [l, n] : d_->tipLines(g)) s += "\n" + std::string(globe::style(l).name) + " " + globe::formatCount(n);
    if (others) s += "\n" + othersText(d_->places, g.members);
    return s;
}

// ---- le glyphe d'un calque ----
void GlobeView::drawGlyph(gfx::IRenderer& r, Layer l, const Rect& box, bool on) {
    const auto& st = globe::style(l);
    const Color c = on ? st.color : kGlyphOff;
    const Sprite& s = makeSprite(r, SpriteKind::Glyph, st.shape, c, static_cast<int>(std::lround(std::min(box.w, box.h))), 0);
    if (s.tex.v) drawSprite(r, s, box.x + box.w / 2.f, box.y + box.h / 2.f, 1.f);
    else r.fillRoundedRect(box.inset(2.f, 2.f), c, box.w / 2.f);
}

// ---- le dessin ----
void GlobeView::onPaint(const PaintContext& ctx) {
    Impl& d = *d_;
    gfx::IRenderer& r = ctx.r;
    gfx::IRenderer& base = ui::unzoomed(r);   // les textures : celles du renderer de la surface
    d.checkBounds(*this);
    // Le temps : la vue n'avance qu'a 30 images par seconde au plus ; une page revenue ne rattrape pas son absence.
    if (d.lastStep < 0.0 || ctx.time < d.lastStep) d.lastStep = ctx.time;
    else if (ctx.time - d.lastStep >= kFrame) {
        advance(ctx.time - d.lastStep);
        d.lastStep = ctx.time;
    }
    // La souris sortie : plus de survol.
    if (!hovered() && !d.forcedHover && !d.pressing && d.hover >= 0) d.setHover(*this, -1);

    // Les textures des globes detruits, et celles d'un autre renderer.
    auto& lost = orphans();
    for (auto it = lost.begin(); it != lost.end();) {
        if (it->first == &base) {
            base.releaseImage(it->second);
            it = lost.erase(it);
        } else {
            ++it;
        }
    }
    if (d.owner != &base) {
        if (d.owner) {
            if (d.bgTex.v) lost.emplace_back(d.owner, d.bgTex);
            if (d.dotsTex.v) lost.emplace_back(d.owner, d.dotsTex);
        }
        d.bgTex = d.dotsTex = {};
        d.owner = &base;
        d.bgDirty = d.dotsDirty = true;
    }

    const Point c = centre();
    const float R = radius();
    const float zr = d.bgZoom > 0.f ? d.view.zoom / d.bgZoom : 0.f;
    if (d.bgDirty || zr > 1.15f || zr < 1.f / 1.15f) d.buildBackground(base, *this);
    if (d.dotsDirty) d.buildDots(base, *this);

    if (d.bgTex.v) {
        const float s = d.view.zoom / d.bgZoom;
        r.drawImage(d.bgTex, {c.x + (d.bgRect.x - d.bgCentre.x) * s, c.y + (d.bgRect.y - d.bgCentre.y) * s, d.bgRect.w * s, d.bgRect.h * s});
    } else {
        r.fillRoundedRect({c.x - R, c.y - R, 2.f * R, 2.f * R}, Color{251, 252, 254, 255}, R);   // sans textures (les essais)
    }
    if (d.dotsTex.v) r.drawImage(d.dotsTex, d.dotsRect);

    d.ensureGroups(*this);
    if (d.hover >= 0 && !d.pressing) {   // la vue a bouge sous la souris : ce qui est dessous
        const int g = d.hitGroup(d.hoverPos);
        d.setHover(*this, g < 0 ? -1 : d.groups[static_cast<std::size_t>(g)].lead);
    }
    // Les halos des PC actifs (sous les marques ; fixes : la maquette figee - pas de battement permanent).
    const Color green = globe::style(Layer::ActivePcs).color;
    for (const auto& it : d.items)
        if (it.layer == Layer::ActivePcs) drawRing(r, green, it.x, it.y, it.r + 2.f + 0.3f * (d.compact ? 7.f : 12.f), 1.4f, it.alpha * 0.7f * 0.38f);
    for (const auto& it : d.items) drawMarker(r, it.layer, it.x, it.y, it.r, it.alpha);
    if (!d.compact) d.drawLabels(r, *this);

    // En direct : une bouffee a chaque connexion (deux anneaux qui s'ouvrent et s'effacent).
    for (const auto& p : d.puffs) {
        const auto& pl = d.places[static_cast<std::size_t>(p.place)];
        const auto pr = globe::project(d.view, pl.lat, pl.lon);
        if (pr.z < 0.0) continue;
        const float x = c.x + R * static_cast<float>(pr.x), y = c.y - R * static_cast<float>(pr.y);
        const double u = d.frozen ? 0.35 : (d.clock - p.start) / kPuffSeconds;
        for (double lag : {0.0, 0.25}) {
            const double uu = u - lag;
            if (uu < 0.0) continue;
            const float alpha = static_cast<float>(std::max(0.0, 1.0 - uu) * 0.8);
            drawRing(r, green, x, y, 4.f + static_cast<float>(uu) * (d.compact ? 14.f : 30.f), 2.f, alpha);
        }
    }

    // La ville choisie : un anneau sombre, et le trait vers sa carte.
    if (d.selected >= 0) {
        const int g = d.groupWith(d.selected);
        if (g >= 0 && d.groups[static_cast<std::size_t>(g)].z > 0.f) {
            const auto& q = d.groups[static_cast<std::size_t>(g)];
            const float rr = std::max(8.f, q.r) + 7.f;
            drawRing(r, kInk, q.x, q.y, rr, 1.5f, 1.f);
            if (d.leader) {
                const float a = std::atan2(d.leader->y - q.y, d.leader->x - q.x);
                r.line({q.x + rr * std::cos(a), q.y + rr * std::sin(a)}, *d.leader, Color{15, 23, 42, 140}, 1.f);
                r.fillRoundedRect({d.leader->x - 2.5f, d.leader->y - 2.5f, 5.f, 5.f}, kInk, 2.5f);
            }
        }
    }
}

// L'infobulle passe au-dessus de tout (la carte d'une ville posee sur le globe par la page) : la passe du dessus.
bool GlobeView::requestsOverlayPass() const { return d_->hover >= 0 && !d_->compact && visible(); }

void GlobeView::onPaintOverlay(const PaintContext& ctx) { d_->drawTooltip(ctx.r, *this); }

// ---- la souris ----
EventResult GlobeView::onEvent(const InputEvent& ev) {
    Impl& d = *d_;
    if (d.compact) return EventResult::Ignored;   // la petite carte : la page decide (un clic ouvre le globe)
    const auto now = Impl::Clock::now();
    const auto since = [&](Impl::Clock::time_point t) { return std::chrono::duration<double>(now - t).count(); };
    if (const auto* m = std::get_if<MouseDown>(&ev)) {
        if (m->button != MouseButton::Left || !bounds().contains(m->pos)) return EventResult::Ignored;
        captureMouse();
        d.pressing = true;
        d.dragging = false;
        d.anim.reset();
        d.vLon = d.vLat = 0.0;
        d.pressAt = d.lastAt = m->pos;
        d.lastMove = now;
        return EventResult::Consumed;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        if (d.pressing) {
            const float dx = m->pos.x - d.lastAt.x, dy = m->pos.y - d.lastAt.y;
            if (!d.dragging && std::fabs(m->pos.x - d.pressAt.x) + std::fabs(m->pos.y - d.pressAt.y) > 3.f) {
                d.dragging = true;
                d.setHover(*this, -1);
            }
            if (d.dragging && (dx != 0.f || dy != 0.f)) {
                const double k = 1.0 / kRad / std::max(1.f, radius());
                const double dt = std::max(0.008, since(d.lastMove));
                d.view.lon = globe::normalizeLon(d.view.lon - dx * k);
                d.view.lat = std::clamp(d.view.lat + dy * k, globe::kMinLat, globe::kMaxLat);
                d.vLon = -dx * k / dt;
                d.vLat = dy * k / dt;
                d.dotsDirty = d.groupsDirty = true;
                invalidate();
            }
            d.lastAt = m->pos;
            d.lastMove = now;
            return EventResult::Consumed;
        }
        d.forcedHover = false;
        d.hoverPos = m->pos;
        d.ensureGroups(*this);
        const int g = bounds().contains(m->pos) ? d.hitGroup(m->pos) : -1;
        d.setHover(*this, g < 0 ? -1 : d.groups[static_cast<std::size_t>(g)].lead);
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseUp>(&ev)) {
        if (m->button != MouseButton::Left || !d.pressing) return EventResult::Ignored;
        releaseMouse();
        d.pressing = false;
        if (d.dragging) {
            d.dragging = false;
            if (since(d.lastMove) > 0.08 || d.frozen) d.vLon = d.vLat = 0.0;   // lache sans elan
            return EventResult::Consumed;
        }
        d.ensureGroups(*this);
        const int g = d.hitGroup(m->pos);
        if (g >= 0 && d.groups[static_cast<std::size_t>(g)].members.size() > 1) {
            // Un groupe : on zoome dessus (ses villes se separent).
            const auto& lead = d.places[static_cast<std::size_t>(d.groups[static_cast<std::size_t>(g)].lead)];
            turnTo(lead.lat, lead.lon, std::min(globe::kMaxZoom, d.view.zoom * 1.9f), 0.9);
        } else {
            placeClicked->emit(g < 0 ? -1 : d.groups[static_cast<std::size_t>(g)].lead);
        }
        return EventResult::Consumed;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos) || w->dy == 0.f || w->mods.ctrl) return EventResult::Ignored;
        zoomBy(std::pow(1.15f, w->dy));
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

std::optional<cursor::Shape> GlobeView::cursorAt(Point local) const {
    if (d_->compact) return std::nullopt;
    if (d_->dragging) return cursor::Shape::Move;
    if (placeAt(local) >= 0) return cursor::Shape::Pointer;
    return std::nullopt;
}

} // namespace ui
