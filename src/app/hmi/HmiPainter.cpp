#include "../../hmi/HmiWidgets.hpp"
#include "../../hmi/HmiForms.hpp"
#include "HmiPainter.hpp"
#include "HmiParamPanes.hpp"   // 1.9 : le repere des copies modifiees
#include "HmiPaintKit.hpp"
#include "HmiIcons.hpp"

#include "HmiImages.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiAlarmGroups.hpp"   // 1.10.2 (AL) : les couleurs des groupes d'alarmes
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiAlarmViews.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiControls.hpp"    // 1.12.2 : le tableau dynamique (tableLayout)
#include "../../hmi/HmiTypeForms.hpp"   // 1.12.2 : ses colonnes, dans l'editeur
#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

namespace app {

using hmi::Kind;
using hmi::Object;
using hmi::edit::Pt;
namespace shapes = ui::shapes;

double HmiPropertySource::number(const Object& o, std::string_view key, double fallback) const {
    double v = fallback;
    const auto t = text(o, key);
    if (!t.empty() && hmi::parseNumber(t, v)) return v;
    return fallback;
}
bool HmiPropertySource::flag(const Object& o, std::string_view key, bool fallback) const {
    const auto t = text(o, key);
    return t.empty() ? fallback : hmi::parseBool(t, fallback);
}

bool tryParseColor(std::string_view t, gfx::Color& out) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
    if (t.empty() || t == "transparent" || t == "aucun") return false;
    if (t.front() == '#') t.remove_prefix(1);
    else if (t.size() > 3 && (t.substr(0, 3) == "16#")) t.remove_prefix(3);
    if (t.size() != 6 && t.size() != 8) return false;
    unsigned v = 0;
    for (char c : t) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return false;
    }
    if (t.size() == 6) out = {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v), 255};
    else out = {static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
    return true;
}

gfx::Color parseColor(std::string_view t, gfx::Color fallback) {
    gfx::Color c;
    return tryParseColor(t, c) ? c : fallback;
}

gfx::Color hmiSeenColor(gfx::Color c, const hmi::DisplayOptions* d, bool onColored, bool isText) {
    if (!d || c.a == 0 || !hmi::changesColors(*d)) return c;
    const std::uint32_t rgb = (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
    const std::uint32_t v = hmi::displayRgb(rgb, *d, onColored, isText);
    return {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v), c.a};
}

namespace paint {

gfx::Color fade(gfx::Color c, float f) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(c.a * f, 0.f, 255.f))); }


std::vector<gfx::Point> rectLocal(double w, double h, double radius) {
    return shapes::roundedRect(0, 0, static_cast<float>(w), static_cast<float>(h), static_cast<float>(radius));
}

void fillAndStroke(const Ctx& c, const std::vector<gfx::Point>& local, bool closed) {
    const auto pts = c.map(local);
    const gfx::Color fill = c.color("fill");
    if (closed && fill.a) shapes::fillPolygon(c.r, pts, fill);
    const gfx::Color stroke = c.color("stroke");
    if (stroke.a && c.stroke() > 0) shapes::strokePolyline(c.r, pts, closed, stroke, std::max(1.f, c.stroke()));
}

// Un texte dans une boite locale de l'objet : aligne, centre verticalement,
// tourne avec l'objet, jamais retourne.
// La police ressource citee par l'objet ("font"), si c'en est une.
const hmi::Resource* fontOf(const Ctx& c) {
    if (!c.opt.assets) return nullptr;
    const auto name = c.src.text(c.o, "font");
    if (name.empty() || name == "Sans") return nullptr;
    for (const auto& r : c.opt.assets->resources)
        if (r.name == name) return r.kind() == hmi::MediaKind::Font && r.data ? &r : nullptr;
    return nullptr;
}

// Les lignes d'un texte coupees a la largeur `maxPx` (en pixels ecran), mot
// par mot ; un mot plus large que la boite reste entier sur sa ligne.
std::string wrapText(const Ctx& c, std::string_view s, gfx::FontId f, float maxPx) {
    std::string out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto nl = s.find('\n', from);
        const std::string_view para = s.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
        std::string line;
        std::size_t w = 0;
        while (w < para.size()) {
            while (w < para.size() && para[w] == ' ') ++w;
            if (w >= para.size()) break;
            std::size_t e = para.find(' ', w);
            if (e == std::string_view::npos) e = para.size();
            const std::string word(para.substr(w, e - w));
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.r.measure(candidate, f).width > maxPx) {
                out += line + "\n";
                line = word;
            } else {
                line = candidate;
            }
            w = e;
        }
        out += line;
        if (nl == std::string_view::npos) break;
        out += "\n";
        from = nl + 1;
    }
    return out;
}

void text(const Ctx& c, std::string_view raw, double bx, double by, double bw, double bh, gfx::Color col,
          double sizePx, std::string_view align, bool wrap) {
    if (raw.empty() || col.a == 0) return;
    const auto px = static_cast<std::uint16_t>(std::clamp(sizePx * c.vp.zoom, 6.0, 200.0));
    // Multiligne : "\n" tape dans le texte ; et le retour a la ligne automatique.
    std::string owned = hmi::unescapeText(raw);
    if (wrap) owned = wrapText(c, owned, gfx::FontId{px}, static_cast<float>(std::max(1.0, (bw - 8) * c.vp.zoom)));
    const std::string_view s = owned;
    const gfx::FontId f{px};
    // UNE POLICE IMPORTEE : chaque ligne est ecrite avec elle (une image
    // blanche, teintee a la couleur du texte), alignee comme le texte normal.
    if (const auto* font = fontOf(c)) {
        std::vector<std::string_view> rows;
        for (std::size_t from = 0; from <= s.size();) {
            const auto nl = s.find('\n', from);
            rows.push_back(s.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from));
            if (nl == std::string_view::npos) break;
            from = nl + 1;
        }
        const float rowH = static_cast<float>(px) * 1.2f;
        const float total = rowH * static_cast<float>(rows.size());
        bool drawn = true;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].empty()) continue;
            const auto img = HmiImageCache::instance().fontText(c.r, *font, rows[i], static_cast<float>(px));
            if (!img.valid()) { drawn = false; break; }
            const double lw = img.width / c.vp.zoom, lhh = rowH / c.vp.zoom;
            double lx = bx + (bw - lw) / 2;
            if (align == "gauche") lx = bx + 4;
            else if (align == "droite") lx = bx + bw - lw - 4;
            const double ly = by + (bh - total / c.vp.zoom) / 2 + static_cast<double>(i) * lhh;
            const gfx::Point centre = c.map(lx + lw / 2, ly + lhh / 2);
            const auto iw = static_cast<float>(img.width), ih = static_cast<float>(img.height);
            c.r.drawImage(img.tex, {centre.x - iw / 2, centre.y - ih / 2, iw, ih}, static_cast<float>(c.o.rotation()) + c.vp.angle,
                          false, false, col);
        }
        if (drawn) return;
    }
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto nl = s.find('\n', start);
        lines.push_back(s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    const float lh = c.r.lineHeight(f);
    const float total = lh * static_cast<float>(lines.size());
    const double angle = c.o.rotation() + c.vp.angle;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float tw = c.r.measure(lines[i], f).width;
        // La ligne, dans le repere local, en pixels ecran divises par le zoom.
        const double lw = tw / c.vp.zoom, lhh = lh / c.vp.zoom;
        double lx = bx + (bw - lw) / 2;
        if (align == "gauche") lx = bx + 4;
        else if (align == "droite") lx = bx + bw - lw - 4;
        const double ly = by + (bh - total / c.vp.zoom) / 2 + static_cast<double>(i) * lhh;
        const gfx::Point centre = c.map(lx + lw / 2, ly + lhh / 2);
        const gfx::Point origin{centre.x - tw / 2, centre.y - lh / 2};
        c.r.drawTextRotated(origin, lines[i], f, col, static_cast<float>(angle), centre);
    }
}

std::vector<gfx::Point> parsePoints(const Ctx& c) {
    std::vector<gfx::Point> out;
    std::istringstream in(c.src.text(c.o, "points"));
    std::string pair;
    while (in >> pair) {
        const auto comma = pair.find(',');
        if (comma == std::string::npos) continue;
        double x = 0, y = 0;
        if (hmi::parseNumber(pair.substr(0, comma), x) && hmi::parseNumber(pair.substr(comma + 1), y))
            out.push_back({static_cast<float>(x), static_cast<float>(y)});
    }
    return out;
}

const hmi::Resource* resourceNamed(const Ctx& c, std::string_view name) {
    if (!c.opt.assets || name.empty()) return nullptr;
    for (const auto& r : c.opt.assets->resources)
        if (r.name == name) return &r;
    return nullptr;
}

// Lot 10 : la couleur de remplacement de l'objet (Recolorier) : un SVG la
// prend a la place des siennes, une image PNG ou JPEG en est teintee.
hmi::SvgRecolor recolorOf(const Ctx& c) {
    hmi::SvgRecolor rc;
    const gfx::Color col = parseColor(c.src.text(c.o, "recolor"), gfx::Color{0, 0, 0, 0});
    if (col.a == 0) return rc;
    rc.active = true;
    rc.rgb = (static_cast<std::uint32_t>(col.r) << 16) | (static_cast<std::uint32_t>(col.g) << 8) | col.b;
    rc.mode = c.src.text(c.o, "recolorMode", "tout");
    const gfx::Color from = parseColor(c.src.text(c.o, "recolorFrom", "#000000"), gfx::Color{0, 0, 0, 255});
    rc.from = (static_cast<std::uint32_t>(from.r) << 16) | (static_cast<std::uint32_t>(from.g) << 8) | from.b;
    return rc;
}

// Une image de ressource dans la boite locale (lx, ly, lw, lh) de l'objet :
// "ajuster" garde les proportions, "etirer" remplit la boite. Lot 10 : un SVG
// (`res`) se rasterise a la taille ou il s'affiche - net a tous les zooms - et
// prend la couleur de remplacement de l'objet.
void drawResourceImage(const Ctx& c, const HmiImageCache::Image& img, std::string_view mode, double lx, double ly,
                       double lw, double lh, const hmi::Resource* res = nullptr) {
    double dw = lw, dh = lh;
    if (mode == "aucun") {
        // Sa taille d'origine (un pixel d'image, un pixel de vue), centree.
        dw = img.width;
        dh = img.height;
    } else if (mode != "\xC3\xA9tirer" && mode != "etirer") {
        const double scale = std::min(lw / std::max(1, img.width), lh / std::max(1, img.height));
        dw = img.width * scale;
        dh = img.height * scale;
    }
    const gfx::Point centre = c.map(lx + lw / 2, ly + lh / 2);
    const float sw = static_cast<float>(dw * c.vp.zoom), sh = static_cast<float>(dh * c.vp.zoom);
    const hmi::SvgRecolor rc = recolorOf(c);
    gfx::TextureId tex = img.tex;
    gfx::Color tint{255, 255, 255, static_cast<std::uint8_t>(255 * c.alpha)};
    if (res && res->data && hmi::isSvgImage(*res->data, res->format)) {
        // La taille a l'ecran, arrondie a 4 pixels : un zoom qui glisse ne
        // redessine pas le SVG a chaque image.
        const auto quant = [](float v) { return std::max(4, static_cast<int>(std::ceil(v / 4.f)) * 4); };
        const auto crisp = HmiImageCache::instance().svg(c.r, *res, quant(sw), quant(sh), rc);
        if (crisp.valid()) tex = crisp.tex;
    } else if (rc.active) {
        tint = gfx::Color{static_cast<std::uint8_t>((rc.rgb >> 16) & 0xFF), static_cast<std::uint8_t>((rc.rgb >> 8) & 0xFF),
                          static_cast<std::uint8_t>(rc.rgb & 0xFF), tint.a};
    }
    c.r.drawImage(tex, {centre.x - sw / 2, centre.y - sh / 2, sw, sh}, static_cast<float>(c.o.rotation()) + c.vp.angle,
                  c.o.flipH(), c.o.flipV(), tint);
}

bool drawNamedImage(const Ctx& c, std::string_view name, std::string_view mode, double lx, double ly, double lw, double lh) {
    const auto* res = resourceNamed(c, name);
    if (!res) return false;
    const auto img = HmiImageCache::instance().resource(c.r, *res);
    if (!img.valid()) return false;
    drawResourceImage(c, img, mode, lx, ly, lw, lh, res);
    return true;
}

void placeholder(const Ctx& c, std::string_view label) {
    const auto pts = c.map(rectLocal(c.w(), c.h()));
    shapes::fillPolygon(c.r, pts, c.color("fill", gfx::Color::rgb(0x2A313C)));
    const gfx::Color line = c.fixed(0x5A6678);
    shapes::strokePolyline(c.r, pts, true, line, 1);
    c.r.line(pts[0], pts[2], line, 1);
    c.r.line(pts[1], pts[3], line, 1);
    text(c, label, 0, 0, c.w(), c.h(), c.fixedText(0xC8D0DC), 13, "centre");
}

void drawTable(const Ctx& c, const std::vector<std::string>& headers, int rows) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    const double hh = 26;
    const gfx::Color head = c.fixed(0x323A47);
    shapes::fillPolygon(c.r, c.map(rectLocal(c.w(), std::min(hh, c.h()))), head);
    const gfx::Color grid = c.fixed(0x4A5566);
    const gfx::Color txt = c.fixed(0xDDE3EA);
    const std::size_t n = std::max<std::size_t>(1, headers.size());
    const double cw = c.w() / static_cast<double>(n);
    for (std::size_t k = 0; k < headers.size(); ++k) {
        text(c, headers[k], cw * static_cast<double>(k), 0, cw, hh, txt, 13, "gauche");
        if (k > 0) c.r.line(c.map(cw * static_cast<double>(k), 0), c.map(cw * static_cast<double>(k), c.h()), grid, 1);
    }
    const double rh = rows > 0 ? (c.h() - hh) / rows : 0;
    for (int k = 0; k <= rows && rh > 0; ++k) {
        const double y = hh + rh * k;
        c.r.line(c.map(0, y), c.map(c.w(), y), grid, 1);
    }
}

// Une chaine qui tient dans `maxLocal` (unites de la vue), sinon coupee "...".
// 1.9 finale : un texte vide reste vide (une case a cocher etroite sans texte
// n'ecrit pas "...") ; sans place, rien ; trop peu de place pour un caractere
// et "..." : les caracteres qui tiennent, coupes net - jamais "..." seul.
std::string fitted(const Ctx& c, const std::string& s, double maxLocal, double sizePx) {
    if (s.empty()) return s;
    const auto px = static_cast<std::uint16_t>(std::clamp(sizePx * c.vp.zoom, 6.0, 200.0));
    const float maxPx = static_cast<float>(maxLocal * c.vp.zoom);
    if (c.r.measure(s, gfx::FontId{px}).width <= maxPx) return s;
    if (maxPx <= 0.f) return {};
    const auto cutAt = [&s](std::size_t n) {
        std::size_t cut = std::min(n, s.size());
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        return cut;
    };
    const float dots = c.r.measure("...", gfx::FontId{px}).width;
    if (const std::size_t cut = cutAt(c.r.fitCharacters(s, gfx::FontId{px}, std::max(0.f, maxPx - dots))); cut > 0)
        return s.substr(0, cut) + "...";
    return s.substr(0, cutAt(c.r.fitCharacters(s, gfx::FontId{px}, maxPx)));
}

// Un tableau alimente par un fichier externe : son en-tete, puis les lignes
// qui tiennent dans l'objet.
void drawTableData(const Ctx& c, const hmi::ExternalData& d) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    const double hh = 26, rh = 22;
    const gfx::Color head = c.fixed(0x323A47);
    shapes::fillPolygon(c.r, c.map(rectLocal(c.w(), std::min(hh, c.h()))), head);
    const gfx::Color grid = c.fixed(0x4A5566);
    const gfx::Color txt = c.fixed(0xDDE3EA);
    const gfx::Color headTxt = c.fixed(0xFFFFFF);
    const std::size_t n = std::max<std::size_t>(1, d.headers.size());
    const double cw = c.w() / static_cast<double>(n);
    for (std::size_t k = 0; k < d.headers.size(); ++k) {
        text(c, fitted(c, d.headers[k], cw - 8, 13), cw * static_cast<double>(k), 0, cw, hh, headTxt, 13, "gauche");
        if (k > 0) c.r.line(c.map(cw * static_cast<double>(k), 0), c.map(cw * static_cast<double>(k), c.h()), grid, 1);
    }
    const std::size_t fit = static_cast<std::size_t>(std::max(0.0, (c.h() - hh) / rh));
    for (std::size_t i = 0; i < d.rows.size() && i < fit; ++i) {
        const double y = hh + rh * static_cast<double>(i);
        if (i % 2 == 1) shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(y)}, {static_cast<float>(c.w()), static_cast<float>(y)},
                                                          {static_cast<float>(c.w()), static_cast<float>(y + rh)}, {0.f, static_cast<float>(y + rh)}}),
                                            fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        for (std::size_t k = 0; k < d.rows[i].size() && k < n; ++k)
            text(c, fitted(c, d.rows[i][k], cw - 8, 12), cw * static_cast<double>(k), y, cw, rh, txt, 12, "gauche");
    }
    if (!d.ok()) text(c, d.error, 0, hh, c.w(), c.h() - hh, c.fixedText(0xE5534B), 12, "centre");
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) { out.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}


std::string trimmedPen(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<std::string> pensOf(const std::string& list) {
    std::vector<std::string> out;
    for (auto& p : split(list, ';')) {
        auto t = trimmedPen(p);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// "2026-09-22 07:40:12.350" -> secondes (jours civils, sans fuseau : seules
// les differences comptent). -1 si ce n'est pas une date.
double stampSeconds(std::string_view s) {
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != ' ' && s[10] != 'T') || s[13] != ':') return -1;
    auto num = [&](std::size_t at, std::size_t n) {
        int v = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const char ch = s[at + i];
            if (ch < '0' || ch > '9') return -1;
            v = v * 10 + (ch - '0');
        }
        return v;
    };
    int y = num(0, 4), m = num(5, 2), d = num(8, 2), hh = num(11, 2), mm = num(14, 2), ss = num(17, 2);
    if (y < 0 || m < 1 || d < 1 || hh < 0 || mm < 0 || ss < 0) return -1;
    // Jours depuis 1970 (algorithme de H. Hinnant).
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const double days = static_cast<double>(era) * 146097.0 + static_cast<double>(doe) - 719468.0;
    double frac = 0;
    if (s.size() > 20 && s[19] == '.') {
        double scale = 0.1;
        for (std::size_t i = 20; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, scale /= 10) frac += (s[i] - '0') * scale;
    }
    return days * 86400.0 + hh * 3600.0 + mm * 60.0 + ss + frac;
}

// Un tableau de lignes vivantes : en-tete, colonnes proportionnelles, une
// bande de couleur a gauche de chaque ligne (la priorite d'une alarme).
struct LiveRow { std::vector<std::string> cells; gfx::Color stripe{0, 0, 0, 0}; bool strong{false}; };

void drawRows(const Ctx& c, const std::vector<std::string>& headers, const std::vector<double>& widths,
              const std::vector<LiveRow>& rows, const std::string& empty, int selected = -1) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    // 1.11 (R111) : la taille du texte, une propriete de l'objet (fontSize, 12 par defaut,
    // de 7 a 40) ; l'en-tete et la hauteur des lignes suivent (26 et 22 a 12).
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 12), 7.0, 40.0), k = fs / 12.0;
    const double hh = 26 * k, rh = 22 * k;
    shapes::fillPolygon(c.r, c.map(rectLocal(c.w(), std::min(hh, c.h()))), c.fixed(0x323A47));
    const gfx::Color grid = c.fixed(0x4A5566);
    const gfx::Color txt = c.fixed(0xDDE3EA);
    const gfx::Color headTxt = c.fixed(0xFFFFFF);
    double total = 0;
    for (double w : widths) total += w;
    std::vector<double> xs{0};
    for (double w : widths) xs.push_back(xs.back() + c.w() * w / std::max(1e-9, total));
    for (std::size_t j = 0; j < headers.size() && j + 1 < xs.size(); ++j) {
        const double cw = xs[j + 1] - xs[j];
        text(c, fitted(c, headers[j], cw - 10, 13 * k), xs[j] + 4, 0, cw, hh, headTxt, 13 * k, "gauche");
        if (j > 0) c.r.line(c.map(xs[j], 0), c.map(xs[j], c.h()), grid, 1);
    }
    const std::size_t fit = static_cast<std::size_t>(std::max(0.0, (c.h() - hh) / rh));
    for (std::size_t i = 0; i < rows.size() && i < fit; ++i) {
        const double y = hh + rh * static_cast<double>(i);
        const auto band = [&](double x0, double x1, gfx::Color col) {
            shapes::fillPolygon(c.r, c.map({{static_cast<float>(x0), static_cast<float>(y)}, {static_cast<float>(x1), static_cast<float>(y)},
                                            {static_cast<float>(x1), static_cast<float>(y + rh)}, {static_cast<float>(x0), static_cast<float>(y + rh)}}),
                                col);
        };
        if (i % 2 == 1) band(0, c.w(), fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        // Lot 11 : la ligne choisie (l'alarme dont la consigne se montre).
        if (static_cast<int>(i) == selected) band(0, c.w(), fade(gfx::Color{47, 111, 214, 90}, c.alpha));
        if (rows[i].stripe.a) band(0, 4, fade(rows[i].stripe, c.alpha));
        for (std::size_t j = 0; j < rows[i].cells.size() && j + 1 < xs.size(); ++j) {
            const double cw = xs[j + 1] - xs[j];
            const gfx::Color col = rows[i].strong && rows[i].stripe.a && j + 1 == rows[i].cells.size() ? fade(rows[i].stripe, c.alpha) : txt;
            text(c, fitted(c, rows[i].cells[j], cw - 10, fs), xs[j] + 4, y, cw, rh, col, fs, "gauche");
        }
    }
    if (rows.empty() && !empty.empty() && c.h() > hh + rh)
        text(c, empty, 0, hh, c.w(), rh * 1.5, c.fixedText(0x8A96A8), fs, "centre");
    else if (rows.size() > fit && fit > 0)
        text(c, "+" + std::to_string(rows.size() - fit), c.w() - 60 * k, c.h() - rh, 56 * k, rh, c.fixedText(0x8A96A8), 11 * k, "droite");
}

gfx::Color priorityColor(int p) {
    switch (p) {
        case 1: return gfx::Color::rgb(0xE5534B);
        case 2: return gfx::Color::rgb(0xF2994A);
        case 3: return gfx::Color::rgb(0xF1C40F);
        default: return gfx::Color::rgb(0x4FA3FF);
    }
}

gfx::Color alarmColor(const hmi::Runtime* rt, const hmi::LiveAlarm& a) {
    const gfx::Color byPriority = priorityColor(a.priority);
    const hmi::Project* p = rt ? rt->project() : nullptr;
    const hmi::AlarmGroupDef* g = p && !a.group.empty() ? hmi::alarmGroupByName(*p, a.group) : nullptr;
    if (!g) return byPriority;
    const std::string& want = !a.active ? g->colorCleared : a.acked ? g->colorAcked : g->colorActive;
    return want.empty() ? byPriority : parseColor(want, byPriority);
}

std::string clock(const std::string& stamp) { return stamp.size() >= 19 ? stamp.substr(11, 8) : stamp; }

void drawHistoryObject(const Ctx& c, const hmi::View&) {
    const std::string source = c.src.text(c.o, "source", "alarmes");
    const std::string group = c.src.text(c.o, "group");
    const auto* rt = c.opt.runtime;
    const bool live = !c.opt.editor && rt;
    std::vector<LiveRow> rows;
    const auto keep = [&](const std::string& g) { return group.empty() || g == group; };
    if (source == "alarmes" || source == "acquitt\xC3\xA9" "es") {
        const bool ackedOnly = source != "alarmes";
        int selected = -1;
        if (live)
            for (const auto k : hmi::historyAlarmRows(rt->alarms(), source, group)) {
                const auto& a = rt->alarms()[k];
                if (a.name == rt->selectedAlarm()) selected = static_cast<int>(rows.size());
                rows.push_back({{clock(a.appeared), std::to_string(a.priority), a.name, a.message, a.state()},
                                c.seen(alarmColor(rt, a)), !a.acked});   // 1.10.2 : la couleur du groupe
            }
        drawRows(c, {"Heure", "Prio.", "Alarme", "Message", "\xC3\x89tat"}, {1.1, 0.6, 1.6, 3.2, 1.6}, rows,
                 live ? std::string(ackedOnly ? "aucune alarme acquitt\xC3\xA9" "e" : "aucune alarme active")
                      : std::string(ackedOnly ? "Alarmes acquitt\xC3\xA9" "es (en marche)" : "Alarmes actives (en marche)"),
                 selected);
        return;
    }
    // Lot 11 : les alarmes mises de cote (qui, pourquoi, combien de temps encore).
    if (source.rfind("mises", 0) == 0) {
        int selected = -1;
        if (live)
            for (const auto k : hmi::historyShelvedRows(rt->shelvedAlarms(), group)) {
                const auto& sh = rt->shelvedAlarms()[k];
                if (sh.name == rt->selectedAlarm()) selected = static_cast<int>(rows.size());
                const double left = sh.until < 0 ? -1 : std::max(0.0, sh.until - rt->now());
                std::string rest = "sans limite";
                if (left >= 0) {
                    const auto t = static_cast<long long>(std::ceil(left));
                    rest = t >= 3600 ? std::to_string(t / 3600) + " h " + std::to_string((t / 60) % 60) + " min"
                         : t >= 60   ? std::to_string(t / 60) + " min " + std::to_string(t % 60) + " s"
                                     : std::to_string(t) + " s";
                }
                rows.push_back({{sh.name, sh.group, sh.by.empty() ? std::string("-") : sh.by, sh.reason.empty() ? std::string("-") : sh.reason, rest},
                                c.seen(gfx::Color::rgb(0x8E7CC3)), false});
            }
        drawRows(c, {"Alarme", "Zone", "Par", "Raison", "Reste"}, {1.6, 1, 0.9, 2.6, 1.1}, rows,
                 live ? std::string("aucune alarme mise de c\xC3\xB4t\xC3\xA9") : std::string("Alarmes mises de c\xC3\xB4t\xC3\xA9 (en marche)"),
                 selected);
        return;
    }
    if (source == "historique") {
        if (live) {
            const auto add = [&](const hmi::AlarmOccurrence& a) {
                if (keep(a.group))
                    rows.push_back({{clock(a.appeared), a.acked.empty() ? std::string("-") : clock(a.acked),
                                     a.cleared.empty() ? std::string("-") : clock(a.cleared), a.name, a.message},
                                    c.seen(priorityColor(a.priority)), false});
            };
            if (c.opt.history) for (auto it = c.opt.history->alarms.rbegin(); it != c.opt.history->alarms.rend(); ++it) add(*it);
            else for (auto it = rt->closedAlarms().rbegin(); it != rt->closedAlarms().rend(); ++it) add(*it);
        }
        drawRows(c, {"Apparue", "Acquitt\xC3\xA9" "e", "Disparue", "Alarme", "Message"}, {1, 1, 1, 1.5, 3}, rows,
                 live ? std::string("aucune alarme termin\xC3\xA9" "e") : std::string("Historique des alarmes (en marche)"));
        return;
    }
    // Lot 13 : le journal d'audit - qui, quoi, avant -> apres, le motif, la signature.
    if (source == "audit") {
        if (live) {
            const auto& list = rt->auditTrail();
            for (auto it = list.rbegin(); it != list.rend() && rows.size() < 400; ++it) {
                std::string change = it->before.empty() && it->after.empty() ? std::string("-")
                                   : (it->before.empty() ? std::string("-") : it->before) + " \xE2\x86\x92 " + (it->after.empty() ? std::string("-") : it->after);
                std::string why = it->reason;
                if (!it->signature.empty()) why = "sign\xC3\xA9 " + it->signature + (why.empty() ? std::string{} : " : " + why);
                const bool refused = it->kind.find("refus") != std::string::npos || it->kind == "Verrouillage";
                const bool signedLine = !it->signature.empty();
                rows.push_back({{clock(it->stamp), it->user.empty() ? std::string("-") : it->user, it->kind, it->target, change,
                                 why.empty() ? std::string("-") : why},
                                refused ? c.seen(gfx::Color::rgb(0xE5534B)) : signedLine ? c.seen(gfx::Color::rgb(0x8E7CC3)) : gfx::Color{0, 0, 0, 0}, false});
            }
        }
        drawRows(c, {"Heure", "Qui", "Type", "Cible", "Avant \xE2\x86\x92 apr\xC3\xA8s", "Motif"}, {0.9, 0.8, 1, 1.5, 2, 1.8}, rows,
                 live ? (rt->auditOn() ? std::string("journal d'audit vide") : std::string("journal d'audit \xC3\xA9teint (Configuration > Historiques)"))
                      : std::string("Journal d'audit (en marche)"));
        return;
    }
    if (source == "syst\xC3\xA8me") {
        if (live)
            for (auto it = rt->journal().rbegin(); it != rt->journal().rend(); ++it)
                rows.push_back({{it->stamp.substr(0, 8), it->kind, it->source, it->message},
                                it->kind == "Erreur" ? c.seen(gfx::Color::rgb(0xE5534B)) : gfx::Color{0, 0, 0, 0}, false});
        drawRows(c, {"Heure", "Type", "Source", "Message"}, {1, 1, 1.6, 3.4}, rows,
                 live ? std::string("journal vide") : std::string("Historique syst\xC3\xA8me (en marche)"));
        return;
    }
    // Evenements (par defaut).
    if (live)
        for (auto it = rt->events().rbegin(); it != rt->events().rend(); ++it) {
            const bool alarm = it->kind.find("Alarme") != std::string::npos || it->kind.find("Appar") != std::string::npos;
            rows.push_back({{clock(it->stamp), it->kind, it->source, it->message},
                            alarm ? c.seen(gfx::Color::rgb(0xE5534B)) : gfx::Color{0, 0, 0, 0}, false});
        }
    drawRows(c, {"Heure", "Type", "Source", "Message"}, {1, 1.2, 1.5, 3.3}, rows,
             live ? std::string("aucun \xC3\xA9v\xC3\xA9nement") : std::string("\xC3\x89v\xC3\xA9nements (en marche)"));
}

// LA COURBE : des plumes (une expression chacune), une fenetre de temps, une
// echelle fixe ou automatique, une interpolation (lineaire, escalier, points).
// Temps reel : les mesures du moteur IHM ; historique : les mesures archivees
// (ou un fichier externe : 1re colonne le temps, puis une colonne par plume).
void drawTrendObject(const Ctx& c, const hmi::View& view) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    const double w = c.w(), h = c.h();
    const auto pens = pensOf(c.src.text(c.o, "variables"));
    const auto colorList = split(c.src.text(c.o, "colors"), ';');
    const double duration = std::max(1.0, c.src.number(c.o, "duration", 60));
    const bool autoScale = c.src.text(c.o, "scale", "fixe") == "auto";
    const std::string interp = c.src.text(c.o, "interpolation", "lin\xC3\xA9" "aire");
    const std::string mode = c.src.text(c.o, "mode", "temps r\xC3\xA9" "el");
    const bool historyMode = mode.rfind("historique", 0) == 0;
    const bool legend = c.src.flag(c.o, "legend", true);
    const auto penColor = [&](std::size_t i) {
        const auto& pal = hmiTrendPalette();
        const bool own = i < colorList.size() && !trimmedPen(colorList[i]).empty();
        const gfx::Color col = parseColor(own ? trimmedPen(colorList[i]) : pal[i % pal.size()], gfx::Color::rgb(0x4FA3FF));
        return fade(own ? col : c.seen(col), c.alpha);   // lot 13 : la palette par defaut, telle qu'on la voit
    };

    // Les donnees : une serie (temps, valeur) par plume, et la fin de la fenetre.
    std::vector<std::vector<std::pair<double, double>>> series(pens.size());
    double end = 0;
    bool preview = false;
    std::string endLabel;
    const auto* rt = c.opt.runtime;
    if (!c.opt.editor && rt && !historyMode) {
        end = rt->now();
        if (const auto* list = rt->trend(view.id, c.o.id))
            for (std::size_t i = 0; i < pens.size() && i < list->size(); ++i)
                for (const auto& pt : (*list)[i].points) series[i].push_back(pt);
        endLabel = "0 s";
    } else if (!c.opt.editor && historyMode) {
        const std::string source = c.src.text(c.o, "source");
        if (!source.empty() && c.opt.assets) {
            for (const auto& f : c.opt.assets->files) {
                if (f.name != source) continue;
                const auto data = hmiExternalData(f, 5000);
                for (const auto& row : data->rows) {
                    if (row.empty()) continue;
                    double t = stampSeconds(row[0]);
                    if (t < 0 && !hmi::parseNumber(row[0], t)) continue;
                    for (std::size_t i = 0; i < pens.size(); ++i) {
                        std::size_t col = i + 1;
                        for (std::size_t k = 1; k < data->headers.size(); ++k)
                            if (data->headers[k] == pens[i]) col = k;
                        double v = 0;
                        if (col < row.size() && hmi::parseNumber(row[col], v)) series[i].emplace_back(t, v);
                    }
                    end = std::max(end, t);
                    if (stampSeconds(row[0]) >= 0) endLabel = clock(row[0]);
                }
            }
        } else if (c.opt.history) {
            for (const auto& smp : c.opt.history->samples)
                for (std::size_t i = 0; i < pens.size(); ++i)
                    if (smp.variable == pens[i]) {
                        series[i].emplace_back(smp.epoch, smp.value);
                        if (smp.epoch >= end) { end = smp.epoch; endLabel = clock(smp.stamp); }
                    }
        }
    } else {
        // L'editeur : un apercu, pour juger des couleurs et de l'interpolation.
        preview = true;
        end = duration;
        for (std::size_t i = 0; i < pens.size(); ++i)
            for (int k = 0; k <= 48; ++k) {
                const double t = duration * k / 48.0;
                const double lo = c.src.number(c.o, "ymin", 0), hi = c.src.number(c.o, "ymax", 100);
                const double v = lo + (hi - lo) * (0.5 + 0.32 * std::sin(t / duration * 6.283 * (1 + 0.5 * static_cast<double>(i)) + static_cast<double>(i)));
                series[i].emplace_back(t, v);
            }
        endLabel = "aper\xC3\xA7u";
    }
    const double start = end - duration;

    // L'echelle.
    double lo = c.src.number(c.o, "ymin", 0), hi = c.src.number(c.o, "ymax", 100);
    if (autoScale) {
        bool any = false;
        double mn = 0, mx = 0;
        for (const auto& s : series)
            for (const auto& [t, v] : s) {
                if (t < start) continue;
                if (!any) { mn = mx = v; any = true; }
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
        if (any) {
            const double pad = std::max(1e-6, (mx - mn) * 0.08);
            lo = mn - pad;
            hi = mx + pad;
            if (mx - mn < 1e-9) { lo = mn - 1; hi = mx + 1; }
        }
    }
    if (hi <= lo) hi = lo + 1;

    // La zone de trace : la legende en haut, les graduations a gauche et en bas.
    const double top = legend && !pens.empty() ? 24 : 8, left = 46, bottom = 20, right = 10;
    const double pw = std::max(10.0, w - left - right), ph = std::max(10.0, h - top - bottom);
    const gfx::Color grid = c.fixed(0x323A47);
    const gfx::Color axis = c.fixed(0x5A6678);
    const gfx::Color label = c.fixed(0x9AA6B8);
    for (int k = 0; k <= 4; ++k) {
        const double y = top + ph * k / 4.0;
        c.r.line(c.map(left, y), c.map(left + pw, y), k == 4 ? axis : grid, 1);
        text(c, hmi::formatNumber(std::round((hi - (hi - lo) * k / 4.0) * 100) / 100), 0, y - 9, left - 4, 18, label, 11, "droite");
    }
    for (int k = 0; k <= 4; ++k) {
        const double x = left + pw * k / 4.0;
        c.r.line(c.map(x, top), c.map(x, top + ph), k == 0 ? axis : grid, 1);
        std::string t = k == 4 ? endLabel : "-" + hmi::formatNumber(std::round(duration * (4 - k) / 4.0)) + " s";
        text(c, t, x - 40, top + ph + 1, 80, bottom - 2, label, 11, k == 0 ? "gauche" : k == 4 ? "droite" : "centre");
    }

    // Les plumes.
    const auto X = [&](double t) { return left + (t - start) / duration * pw; };
    const auto Y = [&](double v) { return top + (1 - (std::clamp(v, lo, hi) - lo) / (hi - lo)) * ph; };
    // Lot 18 : les marques - une zone de mouvement tiree, un forcage (sur une plume de la courbe).
    if (!c.opt.editor && rt && !historyMode) {
        // 1.9 : les traits d'abord, puis les textes, du plus recent au plus ancien (une
        // bascule de l'instant, pres du bord droit, garde le sien) : a droite du trait
        // s'il y a la place, sinon a gauche, sinon sur une seconde ligne ; jamais l'un
        // sur l'autre ; un fond sous chaque texte (un autre trait peut le traverser).
        struct Label {
            double           x;
            double           tw;
            std::string_view what;
            gfx::Color       col;
        };
        std::vector<Label> labels;
        const auto px = static_cast<std::uint16_t>(std::clamp(11.0 * c.vp.zoom, 6.0, 200.0));
        for (const auto& m : rt->trendMarkers()) {
            if (m.at < start || m.at > end) continue;
            bool mine = false;
            for (const auto& pen : pens) mine = mine || trimmedPen(pen) == m.variable;
            if (!mine) continue;
            const double x = X(m.at);
            // 1.9 : une bascule vers l'esclave simule (ou le retour au vrai) - le violet du simule.
            const gfx::Color col = fade(m.slave ? c.fixed(0x8B8FE8) : m.forced ? c.fixed(0xEC8426) : c.fixed(0x4FA3FF), c.alpha);
            for (double y = top; y < top + ph; y += 7) c.r.line(c.map(x, y), c.map(x, std::min(top + ph, y + 4)), col, 1);
            const double tw = std::min(260.0, c.r.measure(m.text, gfx::FontId{px}).width / std::max(0.01f, c.vp.zoom) + 8.0);
            labels.push_back({x, tw, m.text, m.slave ? fade(c.fixed(0xB3B6F5), c.alpha) : col});
        }
        const gfx::Color plate = c.color("fill");
        double limit[2] = {left + pw, left + pw};   // par ligne : le debut du texte pose le plus a gauche
        for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
            for (int row = 0; row < 2; ++row) {
                double from = 0;
                if (it->x + 4 + it->tw <= limit[row]) from = it->x + 4;
                else if (it->x - 4 <= limit[row] && it->x - 4 - it->tw >= left) from = it->x - 4 - it->tw;
                else continue;
                const double y = top + 2 + 16.0 * row;
                if (plate.a) {
                    const auto x0 = static_cast<float>(from + 2), x1 = static_cast<float>(from + it->tw - 2);
                    const auto y0 = static_cast<float>(y + 1), y1 = static_cast<float>(y + 15);
                    shapes::fillPolygon(c.r, c.map({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}), plate);
                }
                text(c, it->what, from, y, it->tw, 16, it->col, 11, "gauche");
                limit[row] = from - 2;
                break;
            }
        }
    }
    for (std::size_t i = 0; i < series.size(); ++i) {
        const auto col = penColor(i);
        std::vector<gfx::Point> pts;
        for (const auto& [t, v] : series[i]) {
            if (t < start - duration * 0.02 || t > end + 1e-9) continue;
            const double x = std::clamp(X(t), left, left + pw);
            if (interp == "escalier" && !pts.empty())
                pts.push_back({pts.back().x, static_cast<float>(Y(v))});
            pts.push_back({static_cast<float>(x), static_cast<float>(Y(v))});
        }
        if (pts.empty()) continue;
        if (interp == "points") {
            for (const auto& p : pts) {
                const auto m = c.map(p.x, p.y);
                const float r = std::max(1.5f, 2.2f * c.vp.zoom);
                c.r.fillRect({m.x - r, m.y - r, 2 * r, 2 * r}, col);
            }
        } else {
            if (interp == "escalier" && !preview && !c.opt.editor) pts.push_back({static_cast<float>(X(end)), pts.back().y});
            shapes::strokePolyline(c.r, c.map(pts), false, col, std::max(1.f, 1.8f * c.vp.zoom));
        }
    }
    // La legende : une pastille, le nom, et la derniere valeur.
    if (legend && !pens.empty()) {
        double x = left;
        // Lot 6 : le nom de la plume ("names"), sinon son expression.
        const auto names = hmi::splitSemicolons(c.src.text(c.o, "names"));
        for (std::size_t i = 0; i < pens.size(); ++i) {
            std::string name = i < names.size() && !names[i].empty() ? names[i] : pens[i];
            if (!series[i].empty() && !preview) name += " = " + hmi::formatNumber(std::round(series[i].back().second * 100) / 100);
            // La largeur du libelle, mesuree a la taille ou il sera ecrit.
            const auto px = static_cast<std::uint16_t>(std::clamp(11.0 * c.vp.zoom, 6.0, 200.0));
            const double textW = c.r.measure(name, gfx::FontId{px}).width / std::max(0.01f, c.vp.zoom);
            const double tw = std::min(w - x - 8, 26.0 + textW);
            if (tw < 40) break;
            const auto sw = c.map({{static_cast<float>(x), 8.f}, {static_cast<float>(x + 10), 8.f},
                                   {static_cast<float>(x + 10), 16.f}, {static_cast<float>(x), 16.f}});
            shapes::fillPolygon(c.r, sw, penColor(i));
            text(c, fitted(c, name, tw - 16, 11), x + 12, 3, tw - 14, 18, label, 11, "gauche");
            x += tw + 8;
        }
    }
    if (pens.empty()) text(c, "Courbe : aucune plume (Variables trac\xC3\xA9" "es)", 0, 0, w, h, label, 12, "centre");
    else if (historyMode && !c.opt.editor && std::all_of(series.begin(), series.end(), [](const auto& s) { return s.empty(); }))
        text(c, "aucune mesure archiv\xC3\xA9" "e", left, top, pw, ph, label, 12, "centre");
}

// ---- lot 6 ------------------------------------------------------------------
// Un tableau a cases (cells) : l'en-tete des colonnes, puis ses lignes, chaque
// case a sa largeur relative (widths). En marche, les cases pilotees arrivent
// deja evaluees ; dans l'editeur, leur source se lit telle quelle.
void drawTableCells(const Ctx& c, const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    // La taille du texte (fontSize, 14 par defaut) donne la hauteur des lignes.
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 14), 8.0, 40.0);
    const double rh = std::max(22.0, fs + 12), hh = rh + 4;
    std::size_t n = headers.size();
    for (const auto& r : rows) n = std::max(n, r.size());
    n = std::max<std::size_t>(1, n);
    const auto frac = hmi::columnFractions(c.o, n);
    std::vector<double> xs(n + 1, 0);
    for (std::size_t k = 0; k < n; ++k) xs[k + 1] = xs[k] + frac[k] * c.w();
    const gfx::Color head = c.fixed(0x323A47);
    shapes::fillPolygon(c.r, c.map(rectLocal(c.w(), std::min(hh, c.h()))), head);
    const gfx::Color grid = c.fixed(0x4A5566);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color headTxt = c.fixed(0xFFFFFF);
    const gfx::Color dyn = c.fixedText(0x7FB8FF);
    for (std::size_t k = 0; k < n; ++k) {
        const double cw = xs[k + 1] - xs[k];
        if (k < headers.size()) text(c, fitted(c, headers[k], cw - 8, fs), xs[k], 0, cw, hh, headTxt, fs, "gauche");
        if (k > 0) c.r.line(c.map(xs[k], 0), c.map(xs[k], c.h()), grid, 1);
    }
    const std::size_t fit = static_cast<std::size_t>(std::max(0.0, (c.h() - hh) / rh));
    for (std::size_t i = 0; i < rows.size() && i < fit; ++i) {
        const double y = hh + rh * static_cast<double>(i);
        if (i % 2 == 1)
            shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(y)}, {static_cast<float>(c.w()), static_cast<float>(y)},
                                            {static_cast<float>(c.w()), static_cast<float>(y + rh)}, {0.f, static_cast<float>(y + rh)}}),
                                fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, y + rh), c.map(c.w(), y + rh), grid, 1);
        for (std::size_t k = 0; k < rows[i].size() && k < n; ++k) {
            const auto& cell = rows[i][k];
            const double cw = xs[k + 1] - xs[k];
            // Dans l'editeur, une case pilotee se distingue (bleutee).
            const bool driven = c.opt.editor && (hmi::cellIsExpression(cell) || hmi::cellIsTemplate(cell));
            text(c, fitted(c, cell, cw - 8, fs), xs[k], y, cw, rh, driven ? dyn : txt, fs, "gauche");
        }
    }
}

// 1.12.2 : UN TABLEAU DYNAMIQUE (rowsFrom) - les lignes de sa variable, a partir de la premiere
// montree (il defile), sa barre de defilement quand toutes ne tiennent pas (hmi::tableLayout, le
// meme que tableHit) ; vide : (aucune ligne).
void drawTableRows(const Ctx& c, const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows,
                   std::size_t first) {
    fillAndStroke(c, rectLocal(c.w(), c.h()));
    const auto l = hmi::tableLayout(c.o, c.w(), c.h(), rows.size(), first);
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 14), 8.0, 40.0);
    const double width = std::max(1.0, c.w() - l.bar.w);
    std::size_t n = headers.size();
    for (const auto& r : rows) n = std::max(n, r.size());
    n = std::max<std::size_t>(1, n);
    const auto frac = hmi::columnFractions(c.o, n);
    std::vector<double> xs(n + 1, 0);
    for (std::size_t k = 0; k < n; ++k) xs[k + 1] = xs[k] + frac[k] * width;
    shapes::fillPolygon(c.r, c.map(rectLocal(c.w(), std::min(l.headerH, c.h()))), c.fixed(0x323A47));
    const gfx::Color grid = c.fixed(0x4A5566);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color headTxt = c.fixed(0xFFFFFF);
    for (std::size_t k = 0; k < n; ++k) {
        const double cw = xs[k + 1] - xs[k];
        if (k < headers.size()) text(c, fitted(c, headers[k], cw - 8, fs), xs[k], 0, cw, l.headerH, headTxt, fs, "gauche");
        if (k > 0) c.r.line(c.map(xs[k], 0), c.map(xs[k], c.h()), grid, 1);
    }
    for (std::size_t i = 0; i < l.fit && l.first + i < rows.size(); ++i) {
        const auto& row = rows[l.first + i];
        const double y = l.headerH + l.rowH * static_cast<double>(i);
        if ((l.first + i) % 2 == 1)
            shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(y)}, {static_cast<float>(width), static_cast<float>(y)},
                                            {static_cast<float>(width), static_cast<float>(y + l.rowH)}, {0.f, static_cast<float>(y + l.rowH)}}),
                                fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, y + l.rowH), c.map(width, y + l.rowH), grid, 1);
        for (std::size_t k = 0; k < row.size() && k < n; ++k) {
            const double cw = xs[k + 1] - xs[k];
            text(c, fitted(c, row[k], cw - 8, fs), xs[k], y, cw, l.rowH, txt, fs, "gauche");
        }
    }
    if (rows.empty())
        text(c, "(aucune ligne)", 0, l.headerH, c.w(), std::min(l.rowH, std::max(0.0, c.h() - l.headerH)), c.fixedText(0x8A96A8), fs, "centre");
    if (l.bar.w > 0) {
        const auto quad = [](const hmi::Box& b) {
            return std::vector<gfx::Point>{{static_cast<float>(b.x), static_cast<float>(b.y)}, {static_cast<float>(b.right()), static_cast<float>(b.y)},
                                           {static_cast<float>(b.right()), static_cast<float>(b.bottom())},
                                           {static_cast<float>(b.x), static_cast<float>(b.bottom())}};
        };
        shapes::fillPolygon(c.r, c.map(quad(l.bar)), c.fixed(0x1B2028));
        shapes::fillPolygon(c.r, c.map(quad(l.thumb)), c.fixed(0x6B7686));
    }
}

// Le gestionnaire de recettes : ses boutons, le nom de la recette, puis ses jeux
// en tableau (une colonne par element). En marche, le jeu choisi est surligne.
void drawRecipeManager(const Ctx& c) {
    fillAndStroke(c, rectLocal(c.w(), c.h(), 3));
    const auto l = hmi::recipeManagerLayout(c.o, c.w(), c.h());
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 8.0, 32.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color muted = c.fixed(0x9AA6B8);
    const std::string recipeName = c.src.text(c.o, "recipe");
    const hmi::Recipe* recipe = c.opt.project ? c.opt.project->recipeByName(recipeName) : nullptr;
    const hmi::Id selected = c.opt.runtime ? c.opt.runtime->recipeSelection(c.o.id) : hmi::kNoId;
    for (const auto& b : l.buttons) {
        const bool needsRow = b.label != "Ajouter";
        const bool enabled = c.opt.editor || !needsRow || selected != hmi::kNoId;
        const auto shape = shapes::roundedRect(static_cast<float>(b.box.x), static_cast<float>(b.box.y),
                                               static_cast<float>(b.box.w), static_cast<float>(b.box.h), 3);
        shapes::fillPolygon(c.r, c.map(shape), (enabled ? c.fixed(0x2F6FD6) : c.fixed(0x3A4556)));
        text(c, b.label, b.box.x, b.box.y, b.box.w, b.box.h, (enabled ? c.fixedOn(0xFFFFFF) : c.fixedText(0x8A94A3)),
             fs, "centre");
    }
    text(c, fitted(c, recipe ? recipe->name : recipeName.empty() ? std::string("(aucune recette)") : recipeName + " (introuvable)",
                   l.title.w - 4, fs),
         l.title.x, l.title.y, l.title.w, l.title.h, recipe ? muted : c.fixed(0xE5534B), fs, "droite");

    // Le tableau : "Jeu", puis un element par colonne (et son unite).
    const std::size_t fields = recipe ? recipe->fields.size() : 0;
    const std::size_t n = 1 + fields;
    const double firstW = fields ? std::min(c.w() * 0.28, 150.0) : c.w();
    const double cw = fields ? (c.w() - firstW) / static_cast<double>(fields) : 0;
    auto colX = [&](std::size_t k) { return k == 0 ? 0.0 : firstW + cw * static_cast<double>(k - 1); };
    auto colW = [&](std::size_t k) { return k == 0 ? firstW : cw; };
    const double top = l.table.y;
    shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(top)}, {static_cast<float>(c.w()), static_cast<float>(top)},
                                    {static_cast<float>(c.w()), static_cast<float>(top + l.headerH)},
                                    {0.f, static_cast<float>(top + l.headerH)}}),
                        c.fixed(0x323A47));
    const gfx::Color grid = c.fixed(0x4A5566);
    for (std::size_t k = 0; k < n; ++k) {
        std::string head = k == 0 ? std::string("Jeu") : recipe->fields[k - 1].name;
        if (k > 0 && !recipe->fields[k - 1].unit.empty()) head += " (" + recipe->fields[k - 1].unit + ")";
        text(c, fitted(c, head, colW(k) - 8, fs), colX(k), top, colW(k), l.headerH, c.fixedText(0xFFFFFF), fs, "gauche");
        if (k > 0) c.r.line(c.map(colX(k), top), c.map(colX(k), c.h()), grid, 1);
    }
    if (!recipe) return;
    for (std::size_t i = 0; i < recipe->records.size() && i < l.visibleRows; ++i) {
        const auto& rec = recipe->records[i];
        const double y = top + l.headerH + l.rowH * static_cast<double>(i);
        const bool isSel = rec.id == selected;
        if (isSel || i % 2 == 1)
            shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(y)}, {static_cast<float>(c.w()), static_cast<float>(y)},
                                            {static_cast<float>(c.w()), static_cast<float>(y + l.rowH)}, {0.f, static_cast<float>(y + l.rowH)}}),
                                isSel ? fade(gfx::Color{47, 111, 214, 150}, c.alpha) : fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, y + l.rowH), c.map(c.w(), y + l.rowH), grid, 1);
        text(c, fitted(c, rec.name, colW(0) - 8, fs), 0, y, colW(0), l.rowH, txt, fs, "gauche");
        for (std::size_t k = 0; k < fields; ++k) {
            std::string v = k < rec.values.size() ? hmi::unescapeText(rec.values[k]) : std::string{};
            // Un texte 'Azote N2' se lit sans ses apostrophes (l'operateur voit une valeur, pas une expression).
            if (!c.opt.editor && v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
            text(c, fitted(c, v, colW(k + 1) - 8, fs), colX(k + 1), y, colW(k + 1), l.rowH, txt, fs, "gauche");
        }
    }
    if (recipe->records.empty())
        text(c, "aucun jeu : Ajouter", 0, top + l.headerH, c.w(), l.rowH * 2, muted, fs, "centre");
}


// ---- lot 8 : le champ de saisie et les objets des utilisateurs -------------------
gfx::Color accentOf(const Ctx& c) { return c.color("accent", gfx::Color::rgb(0x2F6FD6)); }

// Ce qui tient dans la largeur, la FIN du texte (on tape au bout) : les
// premiers caracteres tombent.
std::string tailFitting(const Ctx& c, const std::string& s, double maxLocal, double sizePx, std::size_t& dropped) {
    dropped = 0;
    const auto px = static_cast<std::uint16_t>(std::clamp(sizePx * c.vp.zoom, 6.0, 200.0));
    const float maxPx = static_cast<float>(maxLocal * c.vp.zoom);
    std::size_t from = 0;
    while (from < s.size() && c.r.measure(std::string_view(s).substr(from), gfx::FontId{px}).width > maxPx) {
        ++from;
        while (from < s.size() && (static_cast<unsigned char>(s[from]) & 0xC0) == 0x80) ++from;
    }
    dropped = from;
    return s.substr(from);
}

// Un champ : son fond, son cadre (accentue quand il a le focus), son texte et,
// en saisie, le curseur qui clignote. `beforeCaret` : ce qui precede le
// curseur (nul : pas de focus). `hint` : le texte pale quand il est vide.
void drawField(const Ctx& c, const hmi::Box& b, const std::string& shown, const std::string* beforeCaret, double fs,
               std::string_view align, gfx::Color textCol, const std::string& hint) {
    const auto shape = shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w),
                                           static_cast<float>(b.h), 3);
    shapes::fillPolygon(c.r, c.map(shape), c.fixed(0x141820));
    const bool focused = beforeCaret != nullptr;
    shapes::strokePolyline(c.r, c.map(shape), true, focused ? accentOf(c) : c.fixed(0x4A5568),
                           focused ? std::max(1.5f, 2.f * c.vp.zoom) : 1.f);
    const double inner = std::max(0.0, b.w - 12);
    std::size_t dropped = 0;
    const std::string visible = focused ? tailFitting(c, shown, inner, fs, dropped) : fitted(c, shown, inner, fs);
    if (visible.empty() && !hint.empty() && !focused)
        text(c, fitted(c, hint, inner, fs), b.x + 2, b.y, b.w - 4, b.h, c.fixedText(0x6B7686), fs, align);
    else
        text(c, visible, b.x + 2, b.y, b.w - 4, b.h, textCol, fs, align);
    if (!focused || std::fmod(c.opt.time, 1.0) > 0.6) return;
    // Le curseur : au bout de ce qui le precede, aligne comme le texte.
    const auto px = static_cast<std::uint16_t>(std::clamp(fs * c.vp.zoom, 6.0, 200.0));
    const gfx::FontId f{px};
    const double tw = c.r.measure(visible, f).width / c.vp.zoom;
    const std::string before = beforeCaret->size() >= dropped ? beforeCaret->substr(dropped) : std::string{};
    const double bw = c.r.measure(before, f).width / c.vp.zoom;
    const double bx = b.x + 2, bwBox = b.w - 4;
    double start = bx + (bwBox - tw) / 2;
    if (align == "gauche") start = bx + 4;
    else if (align == "droite") start = bx + bwBox - tw - 4;
    const double x = std::clamp(start + bw, b.x + 3, b.right() - 3);
    c.r.line(c.map(x, b.y + b.h * 0.2), c.map(x, b.bottom() - b.h * 0.2), c.fixed(0xE6EAF0),
             std::max(1.f, 1.3f * c.vp.zoom));
}

// La ligne de message d'un objet (erreur en rouge, reponse en vert) ; elle
// s'efface au bout de 8 s.
void drawFormMessage(const Ctx& c, const hmi::Box& b, const hmi::FormState* f, double fs) {
    if (!f || f->message.empty() || !c.opt.runtime) return;
    if (c.opt.runtime->now() - f->messageAt > 8.0) return;
    const gfx::Color col = (f->error ? c.fixedText(0xFF6B6B) : c.fixedText(0x4CD08A));
    text(c, fitted(c, f->message, b.w, fs * 0.85), b.x, b.y, b.w, b.h, col, fs * 0.85, "centre");
}

void drawButtonBox(const Ctx& c, const hmi::Box& b, const std::string& label, gfx::Color fill, gfx::Color txt, double fs) {
    const auto shape = shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w),
                                           static_cast<float>(b.h), 4);
    shapes::fillPolygon(c.r, c.map(shape), fill);
    text(c, fitted(c, label, b.w - 8, fs), b.x, b.y, b.w, b.h, txt, fs, "centre");
}

// 1.9 : en marche, le champ d'un membre d'une copie de parametre modifie depuis
// la capture (pas encore applique) porte le repere jaune (HmiParamPanes).
void markCopy19(const Ctx& c) {
    if (c.opt.editor || !c.opt.runtime) return;
    if (!hmiparams::copyModified(*c.opt.runtime, c.view.id, c.src.text(c.o, "variable"))) return;
    const gfx::Point a = c.map(0, 0), b = c.map(c.w(), c.h());
    hmiparams::paintCopyMark(c.r, {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)},
                             static_cast<float>(c.vp.zoom), c.alpha);
}

void drawInputField(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 3)));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 16), 6.0, 120.0);
    const std::string mode = c.src.text(c.o, "mode", "num\xC3\xA9rique");
    const bool secret = mode.rfind("mot", 0) == 0;
    const bool numeric = !secret && mode.rfind("texte", 0) != 0;
    const std::string unit = c.src.text(c.o, "unit");
    const std::string align = c.src.text(c.o, "align", numeric ? "droite" : "gauche");
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const gfx::Color muted = c.fixed(0x8A94A3);
    const double unitW = unit.empty() ? 0.0 : std::min(w * 0.4, fs * 0.62 * static_cast<double>(unit.size()) + 10);
    const hmi::Box box{0, 0, w - unitW, h};
    const hmi::FormState* f = c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr;
    const bool focused = c.opt.runtime && c.opt.runtime->focusedObject() == c.o.id && f && f->focus == "valeur";
    if (focused) {
        const auto it = f->text.find("valeur");
        const std::string typed = it == f->text.end() ? std::string{} : it->second;
        const auto ct = f->caret.find("valeur");
        const std::size_t caret = std::min(ct == f->caret.end() ? typed.size() : ct->second, typed.size());
        const std::string shown = secret ? hmi::maskedText(typed) : typed;
        const std::string before = secret ? hmi::maskedText(typed.substr(0, caret)) : typed.substr(0, caret);
        // La valeur montree au moment du clic : surlignee tant qu'on n'a rien tape.
        if (f->fresh && !shown.empty()) {
            const auto px = static_cast<std::uint16_t>(std::clamp(fs * c.vp.zoom, 6.0, 200.0));
            const double tw = std::min(box.w - 8, static_cast<double>(c.r.measure(shown, gfx::FontId{px}).width / c.vp.zoom));
            const double x0 = align == "droite" ? box.w - tw - 6 : align == "gauche" ? 6 : (box.w - tw) / 2;
            shapes::fillPolygon(c.r, c.map(shapes::roundedRect(static_cast<float>(x0), static_cast<float>(h * 0.18), static_cast<float>(tw),
                                                              static_cast<float>(h * 0.64), 2)),
                                fade(gfx::Color{47, 111, 214, 150}, c.alpha));
        }
        drawField(c, box, shown, &before, fs, align, txt);
    } else {
        std::string shown;
        if (c.opt.editor) {
            // Dans l'editeur : la variable reliee, entre accolades, pour s'y retrouver.
            const std::string var = c.src.text(c.o, "variable");
            shown = var.empty() ? std::string("(aucune variable)") : "{" + var + "}";
            drawField(c, box, {}, nullptr, fs, align, txt, shown);
        } else if (secret) {
            drawField(c, box, {}, nullptr, fs, align, txt, c.src.text(c.o, "placeholder"));
        } else {
            std::string value = c.src.text(c.o, "value");
            if (numeric && !value.empty()) {
                double x = 0;
                if (hmi::parseNumber(value, x)) value = hmi::formatValue(sim::Value::real(x), c.src.text(c.o, "format"));
            }
            drawField(c, box, value, nullptr, fs, align, txt, c.src.text(c.o, "placeholder"));
        }
    }
    if (!unit.empty()) text(c, unit, w - unitW, 0, unitW, h, muted, fs * 0.9, "gauche");
    // Le clavier virtuel demande : un petit clavier dans le coin (editeur).
    if (c.opt.editor && c.src.text(c.o, "keyboard", "aucun") != "aucun") {
        const double s = std::min(14.0, h * 0.4);
        const auto a = c.map(box.w - s - 3, 2), e = c.map(box.w - 3, 2 + s);
        drawHmiGlyph(c.r, HmiGlyph::Keyboard, {std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)}, muted);
    }
    // La reponse a une saisie refusee, sous le champ.
    if (f && f->error && !f->message.empty() && c.opt.runtime && c.opt.runtime->now() - f->messageAt <= 8.0)
        text(c, fitted(c, f->message, std::max(w, 220.0), fs * 0.75), 0, h + 2, std::max(w, 220.0), fs * 1.3,
             c.fixed(0xFF6B6B), fs * 0.75, "gauche");
}

// Les utilisateurs actifs, dans l'ordre du projet (la connexion en liste).
std::vector<const hmi::User*> enabledUsers(const Ctx& c) {
    std::vector<const hmi::User*> out;
    if (c.opt.project) for (const auto& u : c.opt.project->security.users) if (u.enabled) out.push_back(&u);
    return out;
}

void drawPanelFrame(const Ctx& c) { fillAndStroke(c, rectLocal(c.w(), c.h(), c.src.number(c.o, "radius", 6))); }

void drawLoginPanel(const Ctx& c) {
    drawPanelFrame(c);
    const hmi::FormState* f = c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr;
    const bool list = c.src.flag(c.o, "userList", true);
    const auto users = enabledUsers(c);
    const hmi::User* chosen = !list || users.empty() ? nullptr
                            : c.opt.runtime ? c.opt.runtime->loginChoice(c.o.id) : users.front();
    const auto l = hmi::loginLayout(c.o, c.w(), c.h(), chosen && chosen->protection == "dynamique");
    const double fs = 13 * l.scale;
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const gfx::Color muted = c.fixed(0x9AA6B8);
    // Le titre, avec l'icone.
    const double ic = l.title.h * 0.9;
    {
        const auto a = c.map(l.title.x, l.title.y + (l.title.h - ic) / 2), e = c.map(l.title.x + ic, l.title.y + (l.title.h + ic) / 2);
        drawHmiGlyph(c.r, HmiGlyph::Login, {std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)}, accentOf(c));
    }
    text(c, fitted(c, c.src.text(c.o, "text", "Connexion"), l.title.w - ic - 8, 17 * l.scale), l.title.x + ic + 6, l.title.y,
         l.title.w - ic - 6, l.title.h, txt, 17 * l.scale, "gauche");
    const bool focusedObj = c.opt.runtime && c.opt.runtime->focusedObject() == c.o.id && f;
    const auto fieldText = [&](const std::string& name) {
        if (!f) return std::string{};
        const auto it = f->text.find(name);
        return it == f->text.end() ? std::string{} : it->second;
    };
    const auto caretText = [&](const std::string& name, bool secret) {
        const std::string t = fieldText(name);
        const auto ct = f->caret.find(name);
        const std::size_t at = std::min(ct == f->caret.end() ? t.size() : ct->second, t.size());
        return secret ? hmi::maskedText(t.substr(0, at)) : t.substr(0, at);
    };
    for (const auto& field : l.fields) {
        text(c, field.label, field.labelBox.x, field.labelBox.y, field.labelBox.w, field.labelBox.h, muted, 11.5 * l.scale, "gauche");
        if (field.name == "utilisateur" && list) {
            // < nom >
            const auto arrow = [&](const hmi::Box& b, bool left) {
                shapes::fillPolygon(c.r, c.map(shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w),
                                                                  static_cast<float>(b.h), 3)),
                                    c.fixed(0x323A47));
                const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, d = b.h * 0.18;
                const std::vector<gfx::Point> tri = left
                    ? std::vector<gfx::Point>{{static_cast<float>(cx - d), static_cast<float>(cy)}, {static_cast<float>(cx + d), static_cast<float>(cy - d * 1.4)},
                                              {static_cast<float>(cx + d), static_cast<float>(cy + d * 1.4)}}
                    : std::vector<gfx::Point>{{static_cast<float>(cx + d), static_cast<float>(cy)}, {static_cast<float>(cx - d), static_cast<float>(cy - d * 1.4)},
                                              {static_cast<float>(cx - d), static_cast<float>(cy + d * 1.4)}};
                shapes::fillPolygon(c.r, c.map(tri), txt);
            };
            arrow(l.prev, true);
            arrow(l.next, false);
            drawField(c, l.userBox, {}, nullptr, fs, "centre", txt);
            std::string name = chosen ? chosen->login : std::string("(aucun utilisateur)");
            if (chosen && !chosen->fullName.empty()) name += "  \xC2\xB7  " + chosen->fullName;
            text(c, fitted(c, name, l.userBox.w - 8, fs), l.userBox.x, l.userBox.y, l.userBox.w, l.userBox.h, txt, fs, "centre");
            continue;
        }
        const bool secret = field.secret;
        const bool hasFocus = focusedObj && f->focus == field.name;
        const std::string t = fieldText(field.name);
        const std::string shown = secret ? hmi::maskedText(t) : t;
        if (hasFocus) {
            const std::string before = caretText(field.name, secret);
            drawField(c, field.box, shown, &before, fs, "gauche", txt);
        } else {
            const std::string hint = field.name == "utilisateur" ? std::string("identifiant") : std::string{};
            drawField(c, field.box, shown, nullptr, fs, "gauche", txt, hint);
        }
    }
    drawButtonBox(c, l.button, c.src.text(c.o, "buttonText", "Se connecter"), accentOf(c), c.fixedOn(0xFFFFFF),
                  14 * l.scale);
    drawFormMessage(c, l.message, f, 14 * l.scale);
    // Dans l'editeur, sans utilisateurs : le dire.
    if (c.opt.editor && list && users.empty())
        text(c, "aucun utilisateur (Configuration > Utilisateurs)", l.message.x, l.message.y, l.message.w, l.message.h, muted, 11 * l.scale, "centre");
}

void drawPasswordChange(const Ctx& c) {
    drawPanelFrame(c);
    const hmi::FormState* f = c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr;
    const auto l = hmi::passwordLayout(c.o, c.w(), c.h());
    const double fs = 13 * l.scale;
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const gfx::Color muted = c.fixed(0x9AA6B8);
    const double ic = l.title.h * 0.9;
    {
        const auto a = c.map(l.title.x, l.title.y + (l.title.h - ic) / 2), e = c.map(l.title.x + ic, l.title.y + (l.title.h + ic) / 2);
        drawHmiGlyph(c.r, HmiGlyph::Password, {std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)}, accentOf(c));
    }
    const std::string titleText = fitted(c, c.src.text(c.o, "text", "Changer le mot de passe"), l.title.w - ic - 8, 16 * l.scale);
    text(c, titleText, l.title.x + ic + 6, l.title.y, l.title.w - ic - 6, l.title.h, txt, 16 * l.scale, "gauche");
    // La largeur prise par le titre (en pixels de la vue) : la mention de droite tient dans le reste.
    const double titleUsed = ic + 6 + [&] {
        const auto px = static_cast<std::uint16_t>(std::clamp(16 * l.scale * c.vp.zoom, 6.0, 200.0));
        return c.vp.zoom > 0 ? static_cast<double>(c.r.measure(titleText, gfx::FontId{px}).width) / c.vp.zoom : 0.0;
    }();
    const bool focusedObj = c.opt.runtime && c.opt.runtime->focusedObject() == c.o.id && f;
    for (const auto& field : l.fields) {
        text(c, field.label, field.labelBox.x, field.labelBox.y, field.labelBox.w, field.labelBox.h, muted, 11.5 * l.scale, "gauche");
        std::string t;
        std::size_t at = 0;
        if (f) {
            if (const auto it = f->text.find(field.name); it != f->text.end()) t = it->second;
            const auto ct = f->caret.find(field.name);
            at = std::min(ct == f->caret.end() ? t.size() : ct->second, t.size());
        }
        const std::string shown = hmi::maskedText(t);
        if (focusedObj && f->focus == field.name) {
            const std::string before = hmi::maskedText(t.substr(0, at));
            drawField(c, field.box, shown, &before, fs, "gauche", txt);
        } else {
            drawField(c, field.box, shown, nullptr, fs, "gauche", txt);
        }
    }
    // Qui change son mot de passe (en marche).
    std::string who;
    if (c.opt.runtime) {
        const auto* u = c.opt.runtime->user();
        who = u ? "pour " + u->login : std::string("personne n'est connect\xC3\xA9");
    }
    if (!who.empty()) {
        const double room = l.title.w - titleUsed - 10 * l.scale;
        if (room > 24 * l.scale) text(c, fitted(c, who, room, 11 * l.scale), l.title.x, l.title.y, l.title.w, l.title.h, muted, 11 * l.scale, "droite");
    }
    drawButtonBox(c, l.button, c.src.text(c.o, "buttonText", "Changer"), accentOf(c), c.fixedOn(0xFFFFFF),
                  14 * l.scale);
    drawFormMessage(c, l.message, f, 14 * l.scale);
}

void drawLogoutButton(const Ctx& c) {
    const double w = c.w(), h = c.h();
    const hmi::FormState* f = c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr;
    const std::string who = c.opt.runtime ? c.opt.runtime->userLogin() : std::string{};
    const bool enabled = c.opt.editor || !who.empty();
    const bool confirming = f && c.opt.runtime && c.opt.runtime->now() < f->confirmUntil;
    const auto shape = rectLocal(w, h, c.src.number(c.o, "radius", 4));
    const gfx::Color fill = enabled ? (confirming ? c.fixed(0xC0392B) : c.color("fill", gfx::Color::rgb(0x8A3A3A)))
                                    : c.fixed(0x3A4150);
    shapes::fillPolygon(c.r, c.map(shape), fill);
    const auto stroke = c.color("stroke");
    if (stroke.a && c.stroke() > 0) shapes::strokePolyline(c.r, c.map(shape), true, stroke, std::max(1.f, c.stroke()));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 16), 6.0, 120.0);
    const gfx::Color txt = enabled ? c.color("textColor", gfx::Color::rgb(0xFFFFFF)) : c.fixed(0x8A94A3);
    std::string label = confirming ? std::string("Confirmer ?") : c.src.text(c.o, "text", "Se d\xC3\xA9" "connecter");
    if (!confirming && c.src.flag(c.o, "showUser", true) && !who.empty()) label += " (" + who + ")";
    const double ic = std::min(h * 0.55, 22.0);
    const auto a = c.map(8, (h - ic) / 2), e = c.map(8 + ic, (h + ic) / 2);
    drawHmiGlyph(c.r, HmiGlyph::Logout, {std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)}, txt);
    text(c, fitted(c, label, w - ic - 20, fs), ic + 12, 0, w - ic - 16, h, txt, fs, "centre");
}

void drawUserInfo(const Ctx& c) {
    const double w = c.w(), h = c.h();
    fillAndStroke(c, rectLocal(w, h, c.src.number(c.o, "radius", 4)));
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 16), 6.0, 120.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xE6EAF0));
    const gfx::Color muted = c.fixed(0x9AA6B8);
    const bool icon = c.src.flag(c.o, "icon", true);
    const double ic = icon ? std::min(h * 0.7, 28.0) : 0.0;
    std::string line;
    bool nobody = false;
    const std::string pattern = c.src.text(c.o, "display", "{nom}  ({groupe}, niveau {niveau})");
    if (c.opt.runtime) {
        const auto* u = c.opt.runtime->user();
        if (!u) {
            nobody = true;
            line = c.src.text(c.o, "empty", "Aucun utilisateur connect\xC3\xA9");
        } else {
            hmi::UserInfoValues v;
            v.login = u->login;
            v.name = u->fullName;
            const auto* g = c.opt.project ? c.opt.project->group(u->group) : nullptr;
            v.group = g ? g->name : std::string{};
            v.level = c.opt.runtime->level();
            const double now = c.opt.runtime->now();
            v.since = c.opt.runtime->loggedInAt() > 0 ? now - c.opt.runtime->loggedInAt() : -1;
            v.remaining = c.opt.runtime->autoLogoutRemaining(now);
            line = hmi::fillUserInfo(pattern, v);
        }
    } else {
        // L'editeur : le gabarit, tel quel.
        line = pattern;
    }
    if (icon) {
        const auto a = c.map(8, (h - ic) / 2), e = c.map(8 + ic, (h + ic) / 2);
        drawHmiGlyph(c.r, HmiGlyph::User, {std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)},
                     nobody ? muted : accentOf(c));
    }
    const double x0 = icon ? ic + 14 : 0;
    text(c, fitted(c, line, w - x0 - 8, fs), x0, 0, w - x0, h, nobody ? muted : txt, fs, c.src.text(c.o, "align", "gauche"));
}

void drawUserManager(const Ctx& c) {
    fillAndStroke(c, rectLocal(c.w(), c.h(), 3));
    const auto l = hmi::recipeManagerLayout(c.o, c.w(), c.h());
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 8.0, 32.0);
    const gfx::Color txt = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color muted = c.fixed(0x9AA6B8);
    const hmi::FormState* f = c.opt.runtime ? c.opt.runtime->formState(c.o.id) : nullptr;
    const hmi::Id selected = f ? f->selected : hmi::kNoId;
    const hmi::Project* p = c.opt.project;
    const hmi::User* sel = p && selected != hmi::kNoId ? p->user(selected) : nullptr;
    // Sous securite, sans la permission Administrer, les boutons sont gris (ils
    // refusent, en le disant).
    const bool mayAdminister = c.opt.editor || !c.opt.runtime || !p || !p->security.enabled
                            || c.opt.runtime->permitted("Administrer");
    for (const auto& b : l.buttons) {
        const bool needsRow = b.label != "Ajouter";
        const bool enabled = c.opt.editor || (mayAdminister && (!needsRow || sel != nullptr));
        std::string label = b.label;
        if (label == "Activer" && sel && sel->enabled) label = "D\xC3\xA9sactiver";
        drawButtonBox(c, b.box, label, (enabled ? c.fixed(0x2F6FD6) : c.fixed(0x3A4556)),
                      (enabled ? c.fixedOn(0xFFFFFF) : c.fixedText(0x8A94A3)), fs);
    }
    const std::size_t n = p ? p->security.users.size() : 0;
    std::string title = std::to_string(n) + (n > 1 ? " utilisateurs" : " utilisateur");
    if (f && !f->message.empty() && c.opt.runtime && c.opt.runtime->now() - f->messageAt <= 8.0) title = f->message;
    text(c, fitted(c, title, l.title.w - 4, fs), l.title.x, l.title.y, l.title.w, l.title.h,
         f && f->error && title == f->message ? c.fixed(0xFF6B6B) : muted, fs, "droite");
    // Le tableau : Identifiant, Nom, Groupe, Protection, Etat.
    const double fr[5] = {0.2, 0.3, 0.2, 0.16, 0.14};
    double xs[6] = {0};
    for (int k = 0; k < 5; ++k) xs[k + 1] = xs[k] + fr[k] * c.w();
    const double top = l.table.y;
    shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(top)}, {static_cast<float>(c.w()), static_cast<float>(top)},
                                    {static_cast<float>(c.w()), static_cast<float>(top + l.headerH)}, {0.f, static_cast<float>(top + l.headerH)}}),
                        c.fixed(0x323A47));
    const gfx::Color grid = c.fixed(0x4A5566);
    for (int k = 0; k < 5; ++k) {
        text(c, fitted(c, std::string(hmi::kUserColumns[k]), xs[k + 1] - xs[k] - 8, fs), xs[k], top, xs[k + 1] - xs[k], l.headerH,
             c.fixed(0xFFFFFF), fs, "gauche");
        if (k > 0) c.r.line(c.map(xs[k], top), c.map(xs[k], c.h()), grid, 1);
    }
    if (!p) return;
    const std::string current = c.opt.runtime ? c.opt.runtime->userLogin() : std::string{};
    for (std::size_t i = 0; i < p->security.users.size() && i < l.visibleRows; ++i) {
        const auto& u = p->security.users[i];
        const double y = top + l.headerH + l.rowH * static_cast<double>(i);
        const bool isSel = u.id == selected;
        if (isSel || i % 2 == 1)
            shapes::fillPolygon(c.r, c.map({{0.f, static_cast<float>(y)}, {static_cast<float>(c.w()), static_cast<float>(y)},
                                            {static_cast<float>(c.w()), static_cast<float>(y + l.rowH)}, {0.f, static_cast<float>(y + l.rowH)}}),
                                isSel ? fade(gfx::Color{47, 111, 214, 150}, c.alpha) : fade(gfx::Color{255, 255, 255, 10}, c.alpha));
        c.r.line(c.map(0, y + l.rowH), c.map(c.w(), y + l.rowH), grid, 1);
        const auto* g = p->group(u.group);
        const bool me = !current.empty() && u.login == current;
        const gfx::Color rowTxt = !u.enabled ? muted : txt;
        const std::string cells[5] = {u.login + (me ? "  \xE2\x97\x8F" : ""), u.fullName, g ? g->name : std::string("-"), u.protection,
                                      u.enabled ? std::string("actif") : std::string("d\xC3\xA9sactiv\xC3\xA9")};
        for (int k = 0; k < 5; ++k)
            text(c, fitted(c, cells[k], xs[k + 1] - xs[k] - 8, fs), xs[k], y, xs[k + 1] - xs[k], l.rowH,
                 k == 4 && u.enabled ? c.fixed(0x4CD08A) : rowTxt, fs, "gauche");
    }
    if (p->security.users.empty())
        text(c, "aucun utilisateur : Ajouter", 0, top + l.headerH, c.w(), l.rowH * 2, muted, fs, "centre");
}

} // namespace paint

using namespace paint;

namespace {

// Les objets d'une vue, du dessous vers le dessus ; un conteneur qui rogne
// (lot 8) garde les siens dans son cadre.
void paintObjectsOf(gfx::IRenderer& r, const hmi::View& view, const HmiViewport& vp, const HmiPropertySource& src,
                    const ui::Theme& theme, const HmiPaintOptions& opt) {
    for (const auto* o : view.paintOrder()) {
        gfx::Rect clip{};
        bool clipped = false;
        for (hmi::Id p = o->parent; p != hmi::kNoId;) {
            const auto* po = view.object(p);
            if (!po) break;
            // Lot 12 : la page d'un conteneur a onglets, le contenu d'un panneau (dans
            // l'editeur, un panneau defilant ou l'on est entre montre tout son contenu).
            hmi::Box b{};
            const bool editingInside = opt.editor && po->kind == Kind::ScrollPanel && opt.insideGroup == po->id;
            if (!editingInside && vp.angle == 0.f && (po->kind != Kind::Container || src.flag(*po, "clip", true))
                && hmi::childClip(view, *po, b)) {
                const auto a = vp.toScreen(b.x, b.y), e = vp.toScreen(b.x + b.w, b.y + b.h);
                const gfx::Rect box{std::min(a.x, e.x), std::min(a.y, e.y), std::fabs(e.x - a.x), std::fabs(e.y - a.y)};
                clip = clipped ? clip.intersect(box) : box;
                clipped = true;
            }
            p = po->parent;
        }
        if (clipped) r.pushClip(clip);
        paintHmiObject(r, view, *o, vp, src, theme, opt);
        if (clipped) r.popClip();
    }
}

// Lot 10 : une instance de symbole, la ou la vue ne porte pas deja ses objets
// (l'editeur, les apercus) : ses objets developpes (relies a ses arguments),
// dans une vue a eux ; dans l'editeur, un cadre pointille violet la signale.
void paintSymbolInstance(const Ctx& c) {
    const Object& o = c.o;
    const hmi::View* sym = c.opt.project ? hmi::symbolOf(*c.opt.project, o) : nullptr;
    if (!sym) {
        const std::string name = o.text("symbol");
        placeholder(c, name.empty() ? std::string("Instance de symbole (aucun symbole)")
                                    : "Symbole introuvable : " + name);
        return;
    }
    hmi::View tv;
    tv.id = c.view.id;
    tv.width = c.view.width;
    tv.height = c.view.height;
    hmi::Layer l;
    l.id = o.layer;
    tv.layers.push_back(l);
    tv.objects = hmi::expandInstance(*c.opt.project, o).objects;
    HmiPaintOptions inner = c.opt;
    inner.insideGroup = hmi::kNoId;
    inner.alpha = c.alpha;
    // Les objets de l'instance ont deja son opacite (hmi::expandInstances) : on ne
    // la compte pas deux fois.
    if (const double op = o.number("opacity", 100); op > 0.0) inner.alpha = static_cast<float>(c.alpha / std::clamp(op / 100.0, 0.01, 1.0));
    paintObjectsOf(c.r, tv, c.vp, HmiPropertySource{}, c.theme, inner);
    if (c.opt.editor) {
        const auto pts = c.map(rectLocal(c.w(), c.h()));
        const gfx::Color col = fade(gfx::Color{185, 140, 255, 150}, c.alpha);
        for (std::size_t k = 0; k < pts.size(); ++k) {
            const gfx::Point a = pts[k], b = pts[(k + 1) % pts.size()];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            for (float t = 0; t < len; t += 9.f) {
                const float t1 = std::min(len, t + 5.f);
                c.r.line({a.x + dx * t / len, a.y + dy * t / len}, {a.x + dx * t1 / len, a.y + dy * t1 / len}, col, 1.f);
            }
        }
    }
}

} // namespace

void paintHmiObject(gfx::IRenderer& r, const hmi::View& view, const Object& o, const HmiViewport& vp,
                    const HmiPropertySource& src, const ui::Theme& theme, const HmiPaintOptions& opt) {
    float alpha = static_cast<float>(std::clamp(src.number(o, "opacity", 100) / 100.0, 0.0, 1.0)) * std::clamp(opt.alpha, 0.f, 1.f);
    if (alpha <= 0.f) return;
    if (opt.editor && view.effectivelyHidden(o)) {
        if (!opt.showEditorHidden) return;
        alpha *= 0.25f;
    }
    if (!opt.editor && !src.flag(o, "visible", true)) return;
    // Le clignotement, en marche : une demi-seconde sur deux, l'objet s'efface -
    // ou, avec une couleur de clignotement, il prend cette couleur.
    if (!opt.editor && src.flag(o, "blink", false) && std::fmod(opt.time, 1.0) >= 0.5) {
        const std::string blinkColor = src.text(o, "blinkColor");
        if (blinkColor.empty() || blinkColor == "transparent") {
            alpha *= 0.15f;
        } else {
            Object lit = o;
            lit.setFlag("blink", false);
            for (const char* key : {"fill", "colorOn", "colorOff", "textColor", "stroke"})
                if (lit.find(key) && !(std::string_view(key) == "stroke" && lit.find("fill"))) lit.set(key, blinkColor);
            paintHmiObject(r, view, lit, vp, HmiPropertySource{}, theme, opt);
            return;
        }
    }
    if (opt.insideGroup != hmi::kNoId) {
        // En edition interne, le reste de la vue s'estompe.
        bool inside = false;
        for (hmi::Id p = o.parent; p != hmi::kNoId;) {
            if (p == opt.insideGroup) { inside = true; break; }
            const auto* po = view.object(p);
            p = po ? po->parent : hmi::kNoId;
        }
        if (!inside) alpha *= 0.35f;
    }
    const Ctx c{r, view, o, vp, src, theme, opt, alpha};
    const double w = c.w(), h = c.h();
    // Lot 10 : une instance de symbole. En marche, la vue qui tourne porte deja
    // ses objets (developpes apres elle) : elle ne dessine rien d'elle-meme.
    if (o.kind == Kind::SymbolInstance) {
        if (!opt.editor && !view.childrenOf(o.id).empty()) return;
        paintSymbolInstance(c);
        return;
    }
    // Lot 9 : les commandes et les afficheurs (HmiControlsPainter.cpp).
    if (drawLot9(c)) return;
    // Lot 10 : les symboles de synoptique (HmiSynopticPainter.cpp).
    if (drawSynoptic(c)) return;
    // Lot 11 : les graphiques ; les objets des alarmes et de la production.
    if (drawCharts(c)) return;
    if (drawLot11(c)) return;
    // Lot 12 : la navigation et la structure.
    if (drawLot12(c)) return;
    // Lot 13 : le selecteur de langue.
    if (drawLot13(c)) return;
    if (drawLot14(c)) return;

    switch (o.kind) {
        case Kind::Rectangle:
            fillAndStroke(c, rectLocal(w, h, src.number(o, "radius")));
            break;
        case Kind::Ellipse:
            fillAndStroke(c, shapes::ellipse({static_cast<float>(w / 2), static_cast<float>(h / 2)},
                                             static_cast<float>(w / 2), static_cast<float>(h / 2)));
            break;
        case Kind::Line: {
            const auto pts = c.map(parsePoints(c));
            shapes::strokePolyline(r, pts, false, c.color("stroke", gfx::Color::rgb(0xD0D8E4)), std::max(1.f, c.stroke()));
            break;
        }
        case Kind::Polygon:
            fillAndStroke(c, parsePoints(c));
            break;
        case Kind::Text: {
            const auto fill = c.color("fill");
            if (fill.a) shapes::fillPolygon(r, c.map(rectLocal(w, h)), fill);
            text(c, src.text(o, "text"), 0, 0, w, h, c.color("textColor", gfx::Color::rgb(0xE6EAF0)),
                 src.number(o, "fontSize", 16), src.text(o, "align", "gauche"), src.flag(o, "wrap"));
            break;
        }
        case Kind::Button: {
            fillAndStroke(c, rectLocal(w, h, src.number(o, "radius", 4)));
            std::string label = src.text(o, "text");
            // Lot 9 : la confirmation - le second clic attendu, l'appui maintenu qui se remplit.
            const std::string confirm = src.text(o, "confirmMode", "aucune");
            if (opt.runtime && !opt.editor && confirm != "aucune") {
                const double now = opt.runtime->now();
                if (opt.runtime->confirmPending(o.id, now)) label = "Confirmer ?";
                const double hold = opt.runtime->holdProgress(o.id, now);
                if (hold > 0) {
                    shapes::fillPolygon(r, c.map(rectLocal(w, h, src.number(o, "radius", 4))), fade(gfx::Color{0, 0, 0, 70}, alpha));
                    shapes::fillPolygon(r, c.map(shapes::roundedRect(2, static_cast<float>(h - 6), static_cast<float>((w - 4) * hold), 4, 2)),
                                        fade(c.seen(gfx::Color::rgb(0xF4F6FA)), alpha));
                    if (hold < 1) label = "Maintenir...";
                }
            }
            text(c, label, 0, 0, w, h, c.color("textColor", gfx::Color::rgb(0xFFFFFF)),
                 src.number(o, "fontSize", 16), "centre", src.flag(o, "wrap"));
            if (opt.editor && confirm != "aucune") {
                // Dans l'editeur : un point d'interrogation dans le coin (commande confirmee).
                const double s = std::min(16.0, h * 0.4);
                shapes::fillPolygon(r, c.map(shapes::ellipse({static_cast<float>(w - s / 2 - 3), static_cast<float>(s / 2 + 3)},
                                                             static_cast<float>(s / 2), static_cast<float>(s / 2), 16)),
                                    fade(c.seen(gfx::Color::rgb(0xF2C94C)), alpha));
                text(c, "?", w - s - 3, 3, s, s, fade(c.seen(gfx::Color::rgb(0x1B1F26)), alpha), s * 0.8, "centre");
            }
            break;
        }
        case Kind::Image: {
            // Le nom vient de la valeur OU de l'expression ("etat graphique" :
            // SEL(Ouvert, 'vanne_fermee.png', 'vanne_ouverte.png')).
            const auto name = src.text(o, "image");
            const auto* res = resourceNamed(c, name);
            // Lot 16 : un GIF anime tourne en boucle en marche, a son rythme ; dans
            // l'editeur, sa premiere image (le GIF anime, lui, se regle).
            if (res && res->format == "GIF" && !opt.editor) {
                if (const auto* gif = HmiImageCache::instance().gif(r, *res); gif && gif->valid()) {
                    const auto fill = c.color("fill");
                    if (fill.a) shapes::fillPolygon(r, c.map(rectLocal(w, h)), fill);
                    drawResourceImage(c, gif->looping(opt.time), src.text(o, "stretch", "ajuster"), 0, 0, w, h);
                    break;
                }
            }
            const auto img = res ? HmiImageCache::instance().resource(r, *res) : HmiImageCache::Image{};
            if (img.valid()) {
                const auto fill = c.color("fill");
                if (fill.a) shapes::fillPolygon(r, c.map(rectLocal(w, h)), fill);
                drawResourceImage(c, img, src.text(o, "stretch", "ajuster"), 0, 0, w, h, res);
            } else {
                placeholder(c, name.empty() ? "Image" : res ? name : name + " (absente)");
            }
            break;
        }
        case Kind::Indicator: {
            const bool on = src.flag(o, "value", false);
            const gfx::Color col = c.color(on ? "colorOn" : "colorOff", on ? gfx::Color::rgb(0x2ECC71)
                                                                           : gfx::Color::rgb(0x4A5261));
            const auto shape = src.text(o, "shape", "rond") == "rond"
                ? shapes::ellipse({static_cast<float>(w / 2), static_cast<float>(h / 2)},
                                  static_cast<float>(w / 2), static_cast<float>(h / 2), 32)
                : rectLocal(w, h, 3);
            const auto pts = c.map(shape);
            shapes::fillPolygon(r, pts, col);
            const auto stroke = c.color("stroke", gfx::Color::rgb(0x1B1F26));
            if (stroke.a) shapes::strokePolyline(r, pts, true, stroke, std::max(1.f, c.stroke()));
            if (on) {   // un reflet : un voyant allume se voit de loin
                const auto glint = shapes::ellipse({static_cast<float>(w * 0.38), static_cast<float>(h * 0.34)},
                                                   static_cast<float>(w * 0.14), static_cast<float>(h * 0.10), 16);
                shapes::fillPolygon(r, c.map(glint), fade(gfx::Color{255, 255, 255, 140}, alpha));
            }
            // Lot 13 : l'accessibilite - l'etat en symbole, pas seulement en couleur.
            if (src.flag(o, "a11ySymbols", false))
                drawStatusSymbol(c, on ? std::max(1, hmi::statusSymbolOf(src.text(o, "colorOn", "#2ECC71"))) : 4, w / 2, h / 2,
                                 std::min(w, h) * 0.55, col);
            break;
        }
        case Kind::ProgressBar: {
            const double mn = src.number(o, "min", 0), mx = src.number(o, "max", 100);
            const double v = src.number(o, "value", 0);
            const double f = mx > mn ? std::clamp((v - mn) / (mx - mn), 0.0, 1.0) : 0.0;
            shapes::fillPolygon(r, c.map(rectLocal(w, h, 3)), c.color("background", gfx::Color::rgb(0x2A313C)));
            const bool vertical = src.text(o, "orientation") == "verticale";
            const auto bar = vertical ? shapes::roundedRect(0, static_cast<float>(h * (1 - f)), static_cast<float>(w),
                                                            static_cast<float>(h * f), 3)
                                      : shapes::roundedRect(0, 0, static_cast<float>(w * f), static_cast<float>(h), 3);
            if (f > 0) shapes::fillPolygon(r, c.map(bar), c.color("fill", gfx::Color::rgb(0x2F6FD6)));
            text(c, hmi::formatNumber(std::round(v)) + (vertical ? "" : " %"), 0, 0, w, h,
                 fade(c.seen(gfx::Color::rgb(0xFFFFFF)), alpha), std::min(14.0, h * 0.6), "centre");
            break;
        }
        case Kind::Gauge: {
            const double mn = src.number(o, "min", 0), mx = src.number(o, "max", 100);
            const double v = src.number(o, "value", 0);
            const double f = mx > mn ? std::clamp((v - mn) / (mx - mn), 0.0, 1.0) : 0.0;
            // Les arcs se font en repere local puis passent par la rotation.
            const float rad = static_cast<float>(std::min(w, h) / 2 * 0.92);
            const gfx::Point centre{static_cast<float>(w / 2), static_cast<float>(h / 2)};
            auto band = [&](float from, float to, gfx::Color col) {
                const auto outer = shapes::arc(centre, rad, from, to, 40);
                auto inner = shapes::arc(centre, rad * 0.78f, from, to, 40);
                std::vector<gfx::Point> poly = outer;
                poly.insert(poly.end(), inner.rbegin(), inner.rend());
                shapes::fillPolygon(r, c.map(poly), col);
            };
            band(135, 405, c.color("background", gfx::Color::rgb(0x2A313C)));
            if (f > 0) band(135, static_cast<float>(135 + 270 * f), c.color("fill", gfx::Color::rgb(0x2F6FD6)));
            text(c, hmi::formatNumber(std::round(v * 10) / 10) + " " + src.text(o, "unit"), 0, h * 0.30, w, h * 0.4,
                 fade(c.seen(gfx::Color::rgb(0xE6EAF0)), alpha), std::max(10.0, h * 0.14), "centre");
            break;
        }
        case Kind::Table: {
            // 1.12.2 : les lignes d'une variable (rowsFrom) - en marche, autant qu'elle en a (il
            // defile) ; dans l'editeur, les colonnes que donnera son type, sur les lignes declarees.
            if (const std::string from = src.text(o, "rowsFrom"); from.find_first_not_of(" \t") != std::string::npos) {
                std::vector<std::string> headers;
                std::vector<std::vector<std::string>> rows;
                if (opt.runtime && !opt.editor && opt.runtime->tableRows(o, headers, rows)) {
                    drawTableRows(c, headers, rows, opt.runtime->listFirst(o.id));
                } else {
                    const auto* var = opt.project ? opt.project->variable(trimmedPen(from)) : nullptr;
                    headers = var ? hmi::typeform::columnsOf(*opt.project, var->type) : std::vector<std::string>{"\xE2\x86\x90 " + trimmedPen(from)};
                    drawTable(c, headers, static_cast<int>(src.number(o, "rows", 4)));
                }
                break;
            }
            // Relie a un fichier externe : ses donnees ; sinon ses cases (lot 6) ;
            // sinon les colonnes declarees.
            // En marche, l'action "Lier un tableau" a pu le relier ailleurs (lot 6).
            const std::string* bound = opt.runtime ? opt.runtime->tableSource(o.id) : nullptr;
            const auto source = bound ? *bound : src.text(o, "source");
            const hmi::ExternalFile* file = nullptr;
            if (opt.assets && !source.empty())
                for (const auto& f : opt.assets->files) if (f.name == source) file = &f;
            const auto cells = src.text(o, "cells");
            if (file) drawTableData(c, *hmiExternalData(*file));
            else if (!cells.empty()) drawTableCells(c, split(src.text(o, "columns"), ';'), hmi::parseCells(cells));
            else drawTable(c, split(src.text(o, "columns"), ';'), static_cast<int>(src.number(o, "rows", 4)));
            break;
        }
        case Kind::History:
            drawHistoryObject(c, view);
            break;
        case Kind::Trend:
            drawTrendObject(c, view);
            break;
        case Kind::Video: {
            // Une video n'est pas decodee (pas de decodeur H.264 / VP9 d'un seul
            // fichier) : son image d'attente, ce que dit son conteneur, et en
            // marche une barre de lecture qui avance a son rythme.
            shapes::fillPolygon(r, c.map(rectLocal(w, h)), c.color("fill", gfx::Color::rgb(0x101318)));
            const auto* video = resourceNamed(c, src.text(o, "video"));
            const auto* poster = resourceNamed(c, src.text(o, "poster"));
            const auto posterImg = poster ? HmiImageCache::instance().resource(r, *poster) : HmiImageCache::Image{};
            if (posterImg.valid()) drawResourceImage(c, posterImg, "ajuster", 0, 0, w, h);
            const double s = std::min(w, h) * 0.22;
            shapes::fillPolygon(r, c.map({{static_cast<float>(w / 2 - s / 2), static_cast<float>(h / 2 - s / 2)},
                                          {static_cast<float>(w / 2 + s / 2), static_cast<float>(h / 2)},
                                          {static_cast<float>(w / 2 - s / 2), static_cast<float>(h / 2 + s / 2)}}),
                                fade(gfx::Color{230, 236, 244, 220}, alpha));
            std::string label = video ? video->name : src.text(o, "video").empty() ? std::string("Vid\xC3\xA9o") : src.text(o, "video") + " (absente)";
            if (video && video->width > 0)
                label += "  \xC2\xB7  " + std::to_string(video->width) + " x " + std::to_string(video->height) + "  \xC2\xB7  "
                       + hmi::formatDuration(video->seconds);
            const double band = std::min(28.0, h / 4);
            shapes::fillPolygon(r, c.map({{0.f, static_cast<float>(h - band)}, {static_cast<float>(w), static_cast<float>(h - band)},
                                          {static_cast<float>(w), static_cast<float>(h)}, {0.f, static_cast<float>(h)}}),
                                fade(gfx::Color{0, 0, 0, 150}, alpha));
            text(c, fitted(c, label, w - 12, 12), 0, h - band, w, band, fade(c.seen(gfx::Color::rgb(0xE6EAF0)), alpha), 12, "gauche");
            if (!opt.editor && video && video->seconds > 0) {
                const double f = std::fmod(opt.time, video->seconds) / video->seconds;
                shapes::fillPolygon(r, c.map({{0.f, static_cast<float>(h - 3)}, {static_cast<float>(w * f), static_cast<float>(h - 3)},
                                              {static_cast<float>(w * f), static_cast<float>(h)}, {0.f, static_cast<float>(h)}}),
                                    fade(c.seen(gfx::Color::rgb(0x2F6FD6)), alpha));
            }
            break;
        }
        case Kind::Container:
            fillAndStroke(c, rectLocal(w, h, 4));
            break;
        case Kind::Group:
            break;
        case Kind::RecipeManager:
            drawRecipeManager(c);
            break;
        // ---- lot 8
        case Kind::InputField:     drawInputField(c); markCopy19(c); break;
        case Kind::LoginPanel:     drawLoginPanel(c); break;
        case Kind::LogoutButton:   drawLogoutButton(c); break;
        case Kind::UserInfo:       drawUserInfo(c); break;
        case Kind::PasswordChange: drawPasswordChange(c); break;
        case Kind::UserManager:    drawUserManager(c); break;
        default:
            break;      // lot 9 : dessines plus haut
        case Kind::AnimatedImage: {
            // En marche, "image" est deja celle de l'etat vrai, a son rang de
            // defilement. Dans l'editeur : l'image par defaut, sinon la premiere
            // du premier etat, et le nombre d'etats.
            std::string name = src.text(o, "image");
            const auto states = hmi::parseImageStates(src.text(o, "states"));
            if (name.empty() && opt.editor)
                for (const auto& st : states) if (!st.images.empty()) { name = st.images.front(); break; }
            const auto* res = resourceNamed(c, name);
            const auto img = res ? HmiImageCache::instance().resource(r, *res) : HmiImageCache::Image{};
            if (img.valid()) drawResourceImage(c, img, src.text(o, "stretch", "ajuster"), 0, 0, w, h, res);
            else placeholder(c, name.empty() ? std::string("Image anim\xC3\xA9" "e") : res ? name : name + " (absente)");
            if (opt.editor) {
                const std::string badge = std::to_string(states.size()) + (states.size() > 1 ? " \xC3\xA9tats" : " \xC3\xA9tat");
                shapes::fillPolygon(r, c.map(shapes::roundedRect(2, 2, 64, 18, 3)), fade(gfx::Color{20, 24, 30, 200}, alpha));
                text(c, badge, 2, 2, 64, 18, fade(c.seen(gfx::Color::rgb(0xE6EAF0)), alpha), 11, "centre");
            }
            break;
        }
        case Kind::AnimatedGif: {
            // Lot 16 : le GIF anime. En marche, l'image que dit le moteur (sa
            // lecture : jouee, en pause, arretee, finie ; -1 : cache a la fin).
            // Dans l'editeur : la premiere image et ce qu'il fera.
            const auto name = src.text(o, "image");
            const auto* res = resourceNamed(c, name);
            const auto* gif = res ? HmiImageCache::instance().gif(r, *res) : nullptr;
            if (!gif || !gif->valid()) {
                placeholder(c, name.empty() ? std::string("GIF anim\xC3\xA9")
                                            : !res ? name + " (absent)" : name + " (pas un GIF anim\xC3\xA9)");
                break;
            }
            int frame = 0;
            if (!opt.editor && opt.runtime) frame = opt.runtime->gifFrameAt(o.id, opt.time);
            const auto fill = c.color("fill");
            if (fill.a) shapes::fillPolygon(r, c.map(rectLocal(w, h)), fill);
            if (frame >= 0) {
                const auto n = static_cast<int>(gif->frames.size());
                drawResourceImage(c, gif->frames[static_cast<std::size_t>(std::clamp(frame, 0, n - 1))], src.text(o, "stretch", "ajuster"),
                                  0, 0, w, h);
            }
            const auto stroke = c.color("stroke");
            if (stroke.a && c.stroke() > 0) shapes::strokePolyline(r, c.map(rectLocal(w, h)), true, stroke, c.stroke());
            if (opt.editor) {
                // Le badge : GIF, ses images, sa lecture.
                const std::string play = src.text(o, "play", "en boucle");
                const std::string badge = "GIF \xC2\xB7 " + std::to_string(gif->frames.size()) + " im. \xC2\xB7 "
                                          + (play == "N fois" ? std::to_string(static_cast<int>(src.number(o, "count", 3))) + " fois" : play);
                const double bw = std::min(w - 4, 150.0);
                if (bw > 30) {
                    shapes::fillPolygon(r, c.map(shapes::roundedRect(2, 2, static_cast<float>(bw), 18, 3)), fade(gfx::Color{20, 24, 30, 200}, alpha));
                    text(c, fitted(c, badge, bw - 6, 11), 2, 2, bw, 18, fade(c.seen(gfx::Color::rgb(0xE6EAF0)), alpha), 11, "centre");
                }
            }
            break;
        }
    }
}

void paintHmiView(gfx::IRenderer& r, const hmi::View& view, const HmiViewport& vp, const HmiPropertySource& src,
                  const ui::Theme& theme, const HmiPaintOptions& opt) {
    gfx::Color own;
    const gfx::Color bg = fade(tryParseColor(view.background, own) ? own : hmiSeenColor(gfx::Color::rgb(0x20242B), opt.display),
                               std::clamp(opt.alpha, 0.f, 1.f));
    if (vp.angle == 0.f) {
        const gfx::Point a = vp.toScreen(0, 0);
        r.fillRect({a.x, a.y, view.width * vp.zoom, view.height * vp.zoom}, bg);
    } else {
        // Tournee (transition) : le fond est un quadrilatere.
        shapes::fillPolygon(r, {vp.toScreen(0, 0), vp.toScreen(view.width, 0), vp.toScreen(view.width, view.height),
                                vp.toScreen(0, view.height)},
                            bg);
    }
    // Lot 8 : un conteneur qui rogne (Rogner le contenu) : ce qui lui
    // appartient ne deborde pas de son cadre (pas pendant une rotation).
    paintObjectsOf(r, view, vp, src, theme, opt);
    // Lot 9 : une liste deroulante ouverte se deroule par-dessus toute la vue.
    if (!opt.editor && opt.runtime)
        for (const auto* o : view.paintOrder())
            if (o->kind == Kind::ComboBox && src.flag(*o, "visible", true) && opt.runtime->comboOpen(o->id)) {
                const float alpha = static_cast<float>(std::clamp(src.number(*o, "opacity", 100) / 100.0, 0.0, 1.0)) * opt.alpha;
                const Ctx c{r, view, *o, vp, src, theme, opt, alpha};
                drawComboList(c);
            }
}

void paintHmiSymbolPreview(gfx::IRenderer& r, const hmi::Project& project, const hmi::View& symbol, const gfx::Rect& area,
                           const ui::Theme& theme) {
    if (symbol.width <= 0 || symbol.height <= 0 || area.w <= 2.f || area.h <= 2.f) return;
    const float z = std::min({area.w / static_cast<float>(symbol.width), area.h / static_cast<float>(symbol.height), 2.f});
    HmiViewport vp{area.x + (area.w - static_cast<float>(symbol.width) * z) / 2.f,
                   area.y + (area.h - static_cast<float>(symbol.height) * z) / 2.f, z};
    HmiPaintOptions opt;
    opt.editor = true;
    opt.project = &project;
    opt.assets = &project.assets;
    r.pushClip(area);
    paintObjectsOf(r, symbol, vp, HmiPropertySource{}, theme, opt);
    r.popClip();
}

void paintHmiViewPreview(gfx::IRenderer& r, const hmi::Project& project, const hmi::View& view, const gfx::Rect& area,
                         const ui::Theme& theme) {
    if (view.width <= 0 || view.height <= 0 || area.w <= 2.f || area.h <= 2.f) return;
    // Ce qu'elle emprunte (ecran modele, en-tete, pied) se voit aussi.
    const hmi::View shown = hmi::inherits(project, view) ? hmi::compose(project, view) : view;
    const float z = std::min(area.w / static_cast<float>(shown.width), area.h / static_cast<float>(shown.height));
    HmiViewport vp{area.x + (area.w - static_cast<float>(shown.width) * z) / 2.f,
                   area.y + (area.h - static_cast<float>(shown.height) * z) / 2.f, z};
    const gfx::Rect page{vp.originX, vp.originY, static_cast<float>(shown.width) * z, static_cast<float>(shown.height) * z};
    r.fillRect(page, parseColor(shown.background, gfx::Color::rgb(0x20242B)));
    HmiPaintOptions opt;
    opt.editor = true;
    opt.project = &project;
    opt.assets = &project.assets;
    r.pushClip(page);
    paintObjectsOf(r, shown, vp, HmiPropertySource{}, theme, opt);
    r.popClip();
    r.strokeRect(page, theme.color.border, 1.f);
}

} // namespace app

namespace app {

const std::vector<std::string>& hmiTrendPalette() {
    static const std::vector<std::string> p = {"#4FA3FF", "#F2994A", "#2ECC71", "#E5534B", "#B98CFF", "#F1C40F"};
    return p;
}

const std::vector<std::string>& hmiHistorySources() {
    static const std::vector<std::string> s = {"alarmes", "acquitt\xC3\xA9" "es", "historique", "\xC3\xA9v\xC3\xA9nements",
                                               "syst\xC3\xA8me", "mises de c\xC3\xB4t\xC3\xA9", "audit"};     // lot 13 : audit
    return s;
}

} // namespace app
