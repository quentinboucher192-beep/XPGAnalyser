// hmi/HmiReportLayout.cpp - 1.25.0 : la mise en page des rapports.
#include "HmiReportLayout.hpp"

#include "HmiExport.hpp"
#include "HmiNatives.hpp"
#include "HmiSemanticColors.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace hmi::report {

namespace {

constexpr double kMm = 72.0 / 25.4;
constexpr std::uint32_t kStandardAccent = 0x3373D9;

bool sameText(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto cut = std::min(s.find(';', from), s.size());
        if (auto t = trim(s.substr(from, cut - from)); !t.empty()) out.push_back(std::move(t));
        from = cut + 1;
    }
    return out;
}

double r8(std::uint32_t c) { return static_cast<double>((c >> 16) & 0xFF) / 255.0; }
double g8(std::uint32_t c) { return static_cast<double>((c >> 8) & 0xFF) / 255.0; }
double b8(std::uint32_t c) { return static_cast<double>(c & 0xFF) / 255.0; }

// Vers le blanc : t = 0 la couleur, t = 1 le blanc.
std::uint32_t towardWhite(std::uint32_t c, double t) {
    const auto mix = [t](std::uint32_t v) { return static_cast<std::uint32_t>(std::lround(static_cast<double>(v) + (255.0 - static_cast<double>(v)) * t)); };
    return (mix((c >> 16) & 0xFF) << 16) | (mix((c >> 8) & 0xFF) << 8) | mix(c & 0xFF);
}

std::string number(double v, int decimals) {
    if (!std::isfinite(v)) return "-";
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string s = b;
    if (s == "-0" || s == "-0.0" || s == "-0.00") s.erase(0, 1);
    return s;
}

// Le point de code d'un octet WinAnsi (0 : aucun).
std::uint32_t unicodeOfWinAnsi(unsigned char c) {
    static const std::uint16_t k80[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                          0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                          0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    if (c >= 0x80 && c < 0xA0) return k80[c - 0x80];
    return c;
}

// 1.25.1 : une police TrueType des ressources, lisible (ses chasses pour les octets WinAnsi 0..255).
bool trueTypeOf(const Project& p, std::string_view name, const Resource*& res, FontMetrics& m) {
    res = p.resourceByName(name);
    if (!res || !res->data || res->kind() != MediaKind::Font) return false;
    std::vector<std::uint32_t> codes(256, 0);
    for (int c = 32; c < 256; ++c) codes[static_cast<std::size_t>(c)] = unicodeOfWinAnsi(static_cast<unsigned char>(c));
    return fontMetrics(*res->data, codes, m);
}

// Un nom PostScript : lettres, chiffres, tirets.
std::string postScriptName(std::string_view family, std::string_view fallback) {
    std::string out;
    for (const char ch : family.empty() ? fallback : family) {
        if (std::isalnum(static_cast<unsigned char>(ch))) out += ch;
        else if (!out.empty() && out.back() != '-') out += '-';
        if (out.size() >= 60) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("Police") : out;
}

std::string fontKey(std::string_view f) {
    if (sameText(f, "Times")) return "Times";
    if (sameText(f, "Courier")) return "Courier";
    return "Helvetica";
}

ReportLayout makeStandard() {
    ReportLayout l;
    l.name = std::string(kStandard);
    l.accentTitles = false;   // 1.25.1 : les titres comme avant la 1.25.0 (en noir)
    return l;
}

ReportLayout makeCompact() {
    ReportLayout l;
    l.name = "Compacte";
    l.landscape = true;
    l.marginTop = l.marginBottom = l.marginLeft = l.marginRight = 10;
    l.titleSize = 13;
    l.bodySize = 8;
    l.tableSize = 7;
    for (const auto& p : partInfos()) l.parts.push_back({std::string(p.key), true});
    return l;
}

ReportLayout makeSummaryOnly() {
    ReportLayout l;
    l.name = "Synth\xC3\xA8se seule";
    for (const auto& p : partInfos()) l.parts.push_back({std::string(p.key), p.key == "synthese" || p.key == "graphique"});
    l.visa = true;
    return l;
}

} // namespace

// =================================================================== parties ===
const std::vector<PartInfo>& partInfos() {
    static const std::vector<PartInfo> kParts = {
        {"synthese", "Synth\xC3\xA8se", "Les chiffres de la p\xC3\xA9riode : alarmes, mesures, production.", false},
        {"graphique", "Graphique : alarmes par priorit\xC3\xA9", "Des barres, une par priorit\xC3\xA9 (si le rapport recueille les alarmes).", false},
        {"alarmes", "Alarmes de la p\xC3\xA9riode", "Un tableau : chaque apparition, acquitt\xC3\xA9" "e par qui, sa dur\xC3\xA9" "e.", true},
        {"frequentes", "Alarmes les plus fr\xC3\xA9quentes", "Un tableau : les 20 plus fr\xC3\xA9quentes et leur dur\xC3\xA9" "e cumul\xC3\xA9" "e.", true},
        {"mesures", "Mesures (min, moyenne, max)", "Un tableau : chaque mesure du rapport.", true},
        {"courbes", "Courbes des mesures", "Une courbe par mesure, sur la p\xC3\xA9riode (en Excel : la feuille de leurs valeurs).", false},
        {"production", "Production", "Un tableau : bons, rebuts, cadence, TRS (si le rapport recueille la production).", true},
        {"evenements", "\xC3\x89v\xC3\xA9nements", "Un tableau : connexions, recettes, \xC3\xA9" "critures (si le rapport les recueille).", true},
    };
    return kParts;
}

const PartInfo* partInfo(std::string_view key) noexcept {
    for (const auto& p : partInfos())
        if (p.key == key) return &p;
    return nullptr;
}

std::vector<ReportLayoutPart> partsOf(const ReportLayout& l) {
    std::vector<ReportLayoutPart> out;
    if (l.parts.empty()) {
        for (const auto& p : partInfos()) out.push_back({std::string(p.key), p.key != "courbes"});
        return out;
    }
    for (const auto& part : l.parts) {
        if (!partInfo(part.key)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const ReportLayoutPart& x) { return x.key == part.key; })) continue;
        out.push_back(part);
    }
    for (const auto& p : partInfos())
        if (std::none_of(out.begin(), out.end(), [&](const ReportLayoutPart& x) { return x.key == p.key; })) out.push_back({std::string(p.key), false});
    return out;
}

// ==================================================================== pages ===
const std::vector<PaperInfo>& papers() {
    static const std::vector<PaperInfo> kPapers = {
        {"A4", "A4 (210 \xC3\x97 297 mm)", 210, 297},
        {"A3", "A3 (297 \xC3\x97 420 mm)", 297, 420},
        {"A5", "A5 (148 \xC3\x97 210 mm)", 148, 210},
        {"Lettre", "Lettre US (216 \xC3\x97 279 mm)", 215.9, 279.4},
    };
    return kPapers;
}

const PaperInfo* paperInfo(std::string_view key) noexcept {
    for (const auto& p : papers())
        if (sameText(p.key, key)) return &p;
    return nullptr;
}

std::pair<double, double> pageSize(const ReportLayout& l) {
    const auto* p = paperInfo(l.paper);
    if (!p) p = &papers().front();
    double w = p->width * kMm, h = p->height * kMm;
    if (l.landscape) std::swap(w, h);
    return {w, h};
}

std::string pageText(const ReportLayout& l) {
    const auto* p = paperInfo(l.paper);
    return std::string(p ? p->key : std::string_view("A4")) + (l.landscape ? " paysage" : " portrait");
}

// ========================================================= fournies, projet ===
const std::vector<ReportLayout>& builtInLayouts() {
    static const std::vector<ReportLayout> kBuiltIn = {makeStandard(), makeCompact(), makeSummaryOnly()};
    return kBuiltIn;
}

const ReportLayout* builtInLayout(std::string_view name) noexcept {
    for (const auto& l : builtInLayouts())
        if (sameText(l.name, name)) return &l;
    return nullptr;
}

bool isBuiltIn(std::string_view name) noexcept { return builtInLayout(name) != nullptr; }

const ReportLayout* findLayout(const Project& p, std::string_view name) noexcept {
    if (name.empty()) return nullptr;
    if (const auto* own = p.reportLayoutByName(name)) return own;
    return builtInLayout(name);
}

std::string defaultLayoutName(const Project& p) {
    if (const auto* l = findLayout(p, p.reportLayoutDefault)) return l->name;
    return std::string(kStandard);
}

const ReportLayout& layoutOf(const Project& p, const Report& r) {
    if (const auto* own = findLayout(p, r.layout)) return *own;
    if (const auto* def = findLayout(p, p.reportLayoutDefault)) return *def;
    return *builtInLayout(kStandard);
}

std::vector<std::string> usersOf(const Project& p, std::string_view name) {
    std::vector<std::string> out;
    for (const auto& r : p.reports)
        if (sameText(layoutOf(p, r).name, name)) out.push_back(r.name);
    return out;
}

bool nameFree(const Project& p, std::string_view name, Id except) {
    if (isBuiltIn(name)) return false;
    for (const auto& l : p.reportLayouts)
        if (l.id != except && sameText(l.name, name)) return false;
    return true;
}

std::string uniqueName(const Project& p, std::string wanted) {
    wanted = trim(wanted);
    if (wanted.empty()) wanted = "Mise_en_page";
    if (nameFree(p, wanted)) return wanted;
    for (int k = 2;; ++k) {
        const std::string n = wanted + " (" + std::to_string(k) + ")";
        if (nameFree(p, n)) return n;
    }
}

ReportLayout duplicate(Project& p, const ReportLayout& from, const std::string& name) {
    ReportLayout l = from;
    l.id = p.allocate();
    l.name = uniqueName(p, name.empty() ? from.name + " (copie)" : name);
    if (l.parts.empty()) l.parts = partsOf(from);
    return l;
}

std::vector<std::string> problems(const Project& p, const ReportLayout& l) {
    std::vector<std::string> out;
    const std::string name = trim(l.name);
    if (name.empty()) out.emplace_back("il lui faut un nom");
    else if (isBuiltIn(name)) out.push_back("\xC2\xAB " + name + " \xC2\xBB est une mise en page fournie : choisis un autre nom");
    else if (!nameFree(p, name, l.id)) out.push_back("\xC2\xAB " + name + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0");
    if (!paperInfo(l.paper)) out.emplace_back("format : A4, A3, A5 ou Lettre");
    for (const double m : {l.marginTop, l.marginBottom, l.marginLeft, l.marginRight})
        if (!(m >= 3 && m <= 60)) {
            out.emplace_back("les marges vont de 3 \xC3\xA0 60 mm");
            break;
        }
    if (!(l.titleSize >= 8 && l.titleSize <= 32)) out.emplace_back("le titre : de 8 \xC3\xA0 32 pt");
    if (!(l.bodySize >= 6 && l.bodySize <= 16)) out.emplace_back("le corps : de 6 \xC3\xA0 16 pt");
    if (!(l.tableSize >= 5 && l.tableSize <= 14)) out.emplace_back("les tableaux : de 5 \xC3\xA0 14 pt");
    if (!isStandardFont(l.font)) {
        const Resource* res = nullptr;
        FontMetrics m;
        if (!trueTypeOf(p, l.font, res, m))
            out.push_back("la police \xC2\xAB " + l.font + " \xC2\xBB : Helvetica, Times, Courier, ou une police TrueType (TTF) des ressources");
    }
    {
        const std::string resolved = semantic::resolve(p, l.accent);
        std::uint8_t r = 0, g = 0, b = 0, a = 0;
        if (!natives::parseColor(resolved, r, g, b, a))
            out.push_back("la couleur d'accent \xC2\xAB " + l.accent + " \xC2\xBB : #RRGGBB ou @Nom (une couleur s\xC3\xA9mantique)");
    }
    if (!l.logo.empty()) {
        const auto* res = p.resourceByName(l.logo);
        if (!res || res->kind() != MediaKind::Image) out.push_back("le logo \xC2\xAB " + l.logo + " \xC2\xBB n'est pas une image des ressources");
    }
    const auto parts = partsOf(l);
    if (std::none_of(parts.begin(), parts.end(), [](const ReportLayoutPart& x) { return x.on; })) out.emplace_back("aucune partie coch\xC3\xA9" "e");
    return out;
}

std::size_t renameReferences(Project& p, std::string_view from, std::string_view to) {
    std::size_t n = 0;
    for (auto& r : p.reports)
        if (!r.layout.empty() && sameText(r.layout, from)) {
            r.layout = std::string(to);
            ++n;
        }
    if (!p.reportLayoutDefault.empty() && sameText(p.reportLayoutDefault, from)) p.reportLayoutDefault = std::string(to);
    return n;
}

std::size_t forget(Project& p, std::string_view name) {
    std::size_t n = 0;
    for (auto& r : p.reports)
        if (!r.layout.empty() && sameText(r.layout, name)) {
            r.layout.clear();
            ++n;
        }
    if (!p.reportLayoutDefault.empty() && sameText(p.reportLayoutDefault, name)) p.reportLayoutDefault.clear();
    return n;
}

std::uint32_t accentOf(const Project& p, const ReportLayout& l) {
    const std::string resolved = semantic::resolve(p, l.accent);
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    if (!natives::parseColor(resolved, r, g, b, a)) return kStandardAccent;
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
}

// ===================================================================== trous ===
const std::vector<std::string_view>& fieldNames() {
    static const std::vector<std::string_view> kNames = {"Titre", "SousTitre", "Rapport", "Nom",  "Periode", "Site", "Projet",
                                                          "Debut", "Fin",       "Date",    "Heure", "Ecrit",  "Page", "Pages"};
    return kNames;
}

std::string expand(std::string_view pattern, const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    std::size_t i = 0;
    while (i < pattern.size()) {
        const auto open = pattern.find('{', i);
        if (open == std::string_view::npos) {
            out.append(pattern.substr(i));
            break;
        }
        out.append(pattern.substr(i, open - i));
        const auto close = pattern.find('}', open + 1);
        if (close == std::string_view::npos) {
            out.append(pattern.substr(open));
            break;
        }
        const auto key = pattern.substr(open + 1, close - open - 1);
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return sameText(f.first, key); });
        if (it != fields.end()) out += it->second;
        else out.append(pattern.substr(open, close - open + 1));
        i = close + 1;
    }
    return out;
}

// ================================================================= WinAnsi ===
std::string fromWinAnsi(std::string_view w) {
    std::string out;
    for (const char ch : w) {
        const auto c = static_cast<unsigned char>(ch);
        std::uint32_t u = unicodeOfWinAnsi(c);
        if (u == 0) u = 0x3F;
        if (u < 0x80) {
            out += static_cast<char>(u);
        } else if (u < 0x800) {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (u >> 12));
            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
    return out;
}

double textWidth(std::string_view font, std::string_view winAnsi, double size, bool bold) {
    if (fontKey(font) == "Courier") return 0.6 * size * static_cast<double>(winAnsi.size());
    return pdfTextWidth(winAnsi, size, bold);
}

double textWidth(const PageDoc& d, std::string_view winAnsi, double size, bool bold) {
    if (d.widths.size() < 256) return textWidth(d.font, winAnsi, size, bold);
    double w = 0;
    for (const char ch : winAnsi) w += d.widths[static_cast<unsigned char>(ch)];
    // Le gras d'une TrueType est simule (le contour epaissi) : un peu plus large.
    return w / 1000.0 * size * (bold ? 1.03 : 1.0) + (bold ? size * 0.03 : 0.0);
}

bool isStandardFont(std::string_view name) noexcept { return name == "Helvetica" || name == "Times" || name == "Courier"; }

std::vector<std::string> fontChoices(const Project& p) {
    std::vector<std::string> out{"Helvetica", "Times", "Courier"};
    for (const auto& r : p.assets.resources)
        if (r.kind() == MediaKind::Font && r.data) {
            const Resource* res = nullptr;
            FontMetrics m;
            if (trueTypeOf(p, r.name, res, m)) out.push_back(r.name);
        }
    return out;
}

// ============================================================ la pagination ===
PageDoc paginate(const Project& p, const Content& c, const ReportLayout& L) {
    PageDoc d;
    const auto [W, H] = pageSize(L);
    d.width = W;
    d.height = H;
    d.font = fontKey(L.font);
    // 1.25.1 : une police TrueType des ressources - ses chasses, ses mesures, son fichier (integre au PDF).
    if (!isStandardFont(L.font)) {
        const Resource* res = nullptr;
        FontMetrics m;
        if (trueTypeOf(p, L.font, res, m) && m.widths.size() == 256) {
            d.font = "TrueType";
            d.fontName = postScriptName(m.family, res->name);
            d.fontData = res->data;
            d.widths = m.widths;
            for (int c = 32; c < 256; ++c)   // un caractere absent de la police : la largeur d'une espace (ou d'un demi-cadratin)
                if (d.widths[static_cast<std::size_t>(c)] <= 0) d.widths[static_cast<std::size_t>(c)] = d.widths[32] > 0 ? d.widths[32] : 500;
            d.ascent = m.ascent;
            d.descent = m.descent;
            d.capHeight = m.capHeight;
            for (int k = 0; k < 4; ++k) d.bbox[k] = m.bbox[k];
        }
    }
    const double ML = std::clamp(L.marginLeft, 3.0, 60.0) * kMm, MR = std::clamp(L.marginRight, 3.0, 60.0) * kMm;
    const double MT = std::clamp(L.marginTop, 3.0, 60.0) * kMm, MB = std::clamp(L.marginBottom, 3.0, 60.0) * kMm;
    const double avail = std::max(60.0, W - ML - MR);
    const double body = std::clamp(L.bodySize, 6.0, 16.0), titleSz = std::clamp(L.titleSize, 8.0, 32.0);
    const double ts = std::clamp(L.tableSize, 5.0, 14.0), head2 = body + 3;
    const std::uint32_t accent = accentOf(p, L), ink = 0x262626, gray = 0x666666, tint = towardWhite(accent, 0.82);
    // 1.25.1 : le titre et les intertitres - dans la couleur d'accent, ou en noir (Standard : comme avant la 1.25.0).
    const std::uint32_t titleInk = L.accentTitles ? accent : 0x1A1A1A, headInk = L.accentTitles ? accent : 0x262626;

    // ---- le logo : une image des ressources, decodee, posee sur du blanc
    int logo = -1;
    if (!L.logo.empty())
        if (const auto* res = p.resourceByName(L.logo); res && res->data && res->kind() == MediaKind::Image) {
            Rgba img;
            if (decodeImage(*res->data, res->format, img, 512) && img.width > 0 && img.height > 0) {
                for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
                    const unsigned a = img.pixels[i + 3];
                    for (int k = 0; k < 3; ++k)
                        img.pixels[i + static_cast<std::size_t>(k)] =
                            static_cast<std::uint8_t>((img.pixels[i + static_cast<std::size_t>(k)] * a + 255u * (255u - a)) / 255u);
                    img.pixels[i + 3] = 255;
                }
                d.images.push_back(std::move(img));
                logo = 0;
            }
        }

    std::vector<DrawOp>* page = nullptr;
    double y = 0;
    const auto text = [&](double x, double yy, std::string winAnsi, double size, bool bold, std::uint32_t color) {
        DrawOp o;
        o.kind = DrawOp::Kind::Text;
        o.x = x;
        o.y = yy;
        o.size = size;
        o.bold = bold;
        o.color = color;
        o.text = std::move(winAnsi);
        page->push_back(std::move(o));
    };
    const auto fill = [&](double x, double yy, double w, double h, std::uint32_t color) {
        DrawOp o;
        o.kind = DrawOp::Kind::Fill;
        o.x = x;
        o.y = yy;
        o.w = w;
        o.h = h;
        o.color = color;
        page->push_back(std::move(o));
    };
    const auto stroke = [&](double x, double yy, double w, double h, std::uint32_t color, double width) {
        DrawOp o;
        o.kind = DrawOp::Kind::Stroke;
        o.x = x;
        o.y = yy;
        o.w = w;
        o.h = h;
        o.color = color;
        o.size = width;
        page->push_back(std::move(o));
    };
    const auto line = [&](double x1, double y1, double x2, double y2, std::uint32_t color, double width) {
        DrawOp o;
        o.kind = DrawOp::Kind::Line;
        o.points = {{x1, y1}, {x2, y2}};
        o.color = color;
        o.size = width;
        page->push_back(std::move(o));
    };
    const auto image = [&](int index, double x, double yy, double w, double h) {
        DrawOp o;
        o.kind = DrawOp::Kind::Image;
        o.image = index;
        o.x = x;
        o.y = yy;
        o.w = w;
        o.h = h;
        page->push_back(std::move(o));
    };
    const auto fit = [&](const std::string& winAnsi, double size, bool bold, double maxW) {
        // Un peu de marge : une colonne a la largeur exacte de son texte ne le coupe pas pour un arrondi.
        maxW += 0.01;
        if (textWidth(d, winAnsi, size, bold) <= maxW) return winAnsi;
        std::string t = winAnsi;
        while (!t.empty() && textWidth(d, t + "...", size, bold) > maxW) t.pop_back();
        return t + "...";
    };
    const auto logoBox = [&](double maxH) {
        if (logo < 0) return std::pair<double, double>{0, 0};
        const auto& img = d.images[static_cast<std::size_t>(logo)];
        double h = maxH, w = h * img.width / img.height;
        if (w > avail * 0.3) {
            w = avail * 0.3;
            h = w * img.height / img.width;
        }
        return std::pair<double, double>{w, h};
    };
    const std::string titleText = toWinAnsi(expand(L.title, c.fields));
    const std::string subtitleText = toWinAnsi(expand(L.subtitle, c.fields));

    // ---- une page : l'en-tete (la premiere, en grand ; les suivantes, en petit)
    const auto startPage = [&](bool first) {
        d.pages.emplace_back();
        page = &d.pages.back();
        const double top = H - MT;
        if (first) {
            const auto [lw, lh] = logoBox(std::max(28.0, titleSz * 2.6));
            double tx = ML, tw = avail;
            if (lw > 0) {
                if (L.logoRight) image(logo, W - MR - lw, top - lh, lw, lh);
                else {
                    image(logo, ML, top - lh, lw, lh);
                    tx = ML + lw + 10;
                }
                tw = avail - lw - 10;
            }
            text(tx, top - titleSz, fit(titleText, titleSz, true, tw), titleSz, true, titleInk);
            if (!subtitleText.empty()) text(tx, top - titleSz - body - 6, fit(subtitleText, body, false, tw), body, false, gray);
            const double hh = std::max(lh, titleSz + (subtitleText.empty() ? 0.0 : body + 6) + 2) + 8;
            line(ML, top - hh, W - MR, top - hh, accent, 1.5);
            y = top - hh - 22;
        } else {
            const auto [lw, lh] = logoBox(body * 2.2);
            double tx = ML, tw = avail;
            if (lw > 0) {
                if (L.logoRight) image(logo, W - MR - lw, top - lh, lw, lh);
                else {
                    image(logo, ML, top - lh, lw, lh);
                    tx = ML + lw + 8;
                }
                tw = avail - lw - 8;
            }
            text(tx, top - body - 1, fit(titleText, body, true, tw), body, true, titleInk);
            const double hh = std::max(lh, body + 4) + 5;
            line(ML, top - hh, W - MR, top - hh, accent, 0.8);
            y = top - hh - 16;
        }
    };
    const double bottom = MB + 6;
    const auto room = [&](double h) { return y - h >= bottom; };
    const auto heading = [&](const std::string& utf8) {
        text(ML, y - head2, fit(toWinAnsi(utf8), head2, true, avail), head2, true, headInk);
        y -= head2 + 8;
    };

    startPage(true);
    const auto parts = partsOf(L);
    const auto sectionOf = [&](std::string_view key) -> const ExportTable* {
        for (std::size_t i = 0; i < c.keys.size() && i < c.sections.size(); ++i)
            if (c.keys[i] == key) return &c.sections[i];
        return nullptr;
    };
    for (const auto& part : parts) {
        if (!part.on) continue;
        if (part.key == "synthese") {
            if (c.summary.empty()) continue;
            const double rowH = body * 1.6;
            if (!room(head2 + 8 + rowH * std::min<double>(3, static_cast<double>(c.summary.size())))) startPage(false);
            heading("Synth\xC3\xA8se");
            const double labelW = std::min(170.0, avail * 0.36);
            for (std::size_t i = 0; i < c.summary.size(); ++i) {
                if (!room(rowH)) {
                    startPage(false);
                    heading("Synth\xC3\xA8se (suite)");
                }
                if (L.zebra && i % 2 == 0) fill(ML, y - rowH, avail, rowH, 0xF2F4F8);
                text(ML + 6, y - rowH + rowH * 0.3, fit(toWinAnsi(c.summary[i].first), body, true, labelW - 6), body, true, 0x333333);
                text(ML + labelW + 14, y - rowH + rowH * 0.3, fit(toWinAnsi(c.summary[i].second), body, false, avail - labelW - 20), body,
                     false, ink);
                y -= rowH;
            }
            y -= 18;
        } else if (part.key == "graphique") {
            int most = 0;
            for (const int n : c.byPriority) most = std::max(most, n);
            if (!c.alarms || c.byPriority.empty() || most == 0) continue;
            const double barH = std::max(10.0, body * 1.35), step = barH + 6;
            if (!room(head2 + 12 + step * 4)) startPage(false);
            heading("Alarmes par priorit\xC3\xA9");
            static const std::uint32_t bars[4] = {0xD9403A, 0xF28C33, 0xEDC233, 0x4D8CE6};
            const double barMax = std::max(40.0, avail - 170);
            for (std::size_t k = 0; k < c.byPriority.size() && k < 4; ++k) {
                const double w = std::max(1.0, barMax * static_cast<double>(c.byPriority[k]) / static_cast<double>(most));
                text(ML + 6, y - barH + 2, fit(toWinAnsi(std::string(alarmPriorityLabel(static_cast<int>(k) + 1))), body, false, 80), body, false, 0x333333);
                fill(ML + 90, y - barH, w, barH, bars[k]);
                text(ML + 96 + w, y - barH + 2, std::to_string(c.byPriority[k]), body, true, 0x333333);
                y -= step;
            }
            y -= 14;
        } else if (part.key == "courbes") {
            if (c.curves.empty()) continue;
            const double chartH = L.landscape ? 100 : 120;
            bool first = true;
            for (const auto& cv : c.curves) {
                if (!room((first ? head2 + 8 : 0.0) + chartH + body + 30)) {
                    startPage(false);
                    if (!first) heading("Courbes des mesures (suite)");
                }
                if (first) heading("Courbes des mesures");
                first = false;
                const std::string label = cv.name + (cv.unit.empty() ? std::string{} : " (" + cv.unit + ")");
                text(ML, y - body, fit(toWinAnsi(label), body, true, avail), body, true, 0x333333);
                y -= body + 6;
                const double labelW = 46;
                const double bx = ML + labelW, bw = avail - labelW, by = y - chartH;
                double lo = cv.lo, hi = cv.hi;
                if (!(hi > lo)) {
                    lo -= 1;
                    hi += 1;
                }
                const double pad = (hi - lo) * 0.08;
                lo -= pad;
                hi += pad;
                for (int g = 0; g <= 4; ++g) {
                    const double gy = by + chartH * g / 4.0;
                    line(bx, gy, bx + bw, gy, g == 0 ? 0x999999 : 0xE3E6EA, g == 0 ? 0.6 : 0.4);
                }
                stroke(bx, by, bw, chartH, 0xBBBBBB, 0.5);
                const int dec = (hi - lo) >= 100 ? 0 : (hi - lo) >= 10 ? 1 : 2;
                const std::string top = number(hi, dec), low = number(lo, dec);
                text(bx - 4 - textWidth(d, top, ts, false), by + chartH - ts, top, ts, false, gray);
                text(bx - 4 - textWidth(d, low, ts, false), by + 1, low, ts, false, gray);
                double t0 = c.from, t1 = c.to;
                if (!(t1 > t0) && !cv.points.empty()) {
                    t0 = cv.points.front().first;
                    t1 = cv.points.back().first;
                }
                if (!(t1 > t0)) t1 = t0 + 1;
                const std::string a = stampOf(t0), b = stampOf(t1);
                text(bx, by - ts - 3, a, ts, false, gray);
                text(bx + bw - textWidth(d, b, ts, false), by - ts - 3, b, ts, false, gray);
                DrawOp poly;
                poly.kind = DrawOp::Kind::Polyline;
                poly.color = accent;
                poly.size = 1.2;
                for (const auto& [t, v] : cv.points) {
                    const double px = bx + bw * std::clamp((t - t0) / (t1 - t0), 0.0, 1.0);
                    const double py = by + chartH * std::clamp((v - lo) / (hi - lo), 0.0, 1.0);
                    poly.points.emplace_back(px, py);
                }
                if (poly.points.size() == 1) poly.points.push_back({poly.points.front().first + 1, poly.points.front().second});
                if (!poly.points.empty()) page->push_back(std::move(poly));
                y = by - ts - 18;
            }
            y -= 6;
        } else if (const auto* info = partInfo(part.key); info && info->table) {
            const ExportTable* t = sectionOf(part.key);
            if (!t) continue;
            const double rowH = ts * 1.65, headH = ts * 2;
            const std::size_t ncol = std::max<std::size_t>(1, t->headers.size());
            std::vector<std::string> heads(ncol);
            for (std::size_t k = 0; k < t->headers.size(); ++k) heads[k] = toWinAnsi(t->headers[k]);
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : t->rows) {
                std::vector<std::string> row(ncol);
                for (std::size_t k = 0; k < r.size() && k < ncol; ++k) row[k] = toWinAnsi(r[k]);
                rows.push_back(std::move(row));
            }
            std::vector<double> widths(ncol, ts * 3.3);
            for (std::size_t k = 0; k < ncol; ++k) {
                widths[k] = std::max(widths[k], textWidth(d, heads[k], ts, true) + 8);
                for (const auto& r : rows) widths[k] = std::max(widths[k], std::min(avail * 0.42, textWidth(d, r[k], ts, false) + 8));
            }
            // Trop large : les colonnes longues (un message) retrecissent d'abord, les courtes gardent leur largeur.
            double total = 0;
            for (const double w : widths) total += w;
            if (total > avail) {
                double narrow = 0, wide = 0;
                for (const double w : widths) (w > 90 ? wide : narrow) += w;
                if (wide > 0 && narrow < avail) {
                    const double k = (avail - narrow) / wide;
                    for (auto& w : widths)
                        if (w > 90) w *= k;
                } else {
                    for (auto& w : widths) w *= avail / total;
                }
            }
            double tableW = 0;
            for (const double w : widths) tableW += w;
            const auto header = [&](bool again) {
                heading(t->title + (again ? " (suite)" : ""));
                if (again && !L.repeatHeader) return;
                fill(ML, y - headH, tableW, headH, tint);
                double x = ML;
                for (std::size_t k = 0; k < ncol; ++k) {
                    text(x + 4, y - headH + (headH - ts) * 0.5 + 1, fit(heads[k], ts, true, widths[k] - 8), ts, true, 0x1A1A1A);
                    x += widths[k];
                }
                y -= headH;
            };
            if (!room(head2 + 8 + headH + rowH)) startPage(false);
            header(false);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (!room(rowH)) {
                    startPage(false);
                    header(true);
                }
                if (L.zebra && i % 2 == 1) fill(ML, y - rowH, tableW, rowH, 0xF5F7FA);
                double x = ML;
                for (std::size_t k = 0; k < ncol; ++k) {
                    text(x + 4, y - rowH + (rowH - ts) * 0.5 + 1, fit(rows[i][k], ts, false, widths[k] - 8), ts, false, ink);
                    x += widths[k];
                }
                y -= rowH;
            }
            if (rows.empty()) {
                text(ML + 4, y - rowH + 4, toWinAnsi("(rien sur la p\xC3\xA9riode)"), ts, false, 0x737373);
                y -= rowH;
            }
            line(ML, y, ML + tableW, y, 0x999999, 0.6);
            y -= 24;
        }
    }

    // ---- les cases de visa, en fin de rapport
    if (L.visa) {
        const auto labels = splitList(L.visaLabels);
        if (!labels.empty()) {
            const double boxH = 52;
            if (!room(boxH + 12)) startPage(false);
            y -= 4;
            const double gap = 10, bw = (avail - gap * static_cast<double>(labels.size() - 1)) / static_cast<double>(labels.size());
            for (std::size_t i = 0; i < labels.size(); ++i) {
                const double x = ML + static_cast<double>(i) * (bw + gap);
                stroke(x, y - boxH, bw, boxH, 0x8C8C8C, 0.6);
                text(x + 5, y - 11, fit(toWinAnsi(labels[i] + " :"), 8, false, bw - 10), 8, false, 0x595959);
            }
            y -= boxH + 12;
        }
    }

    // ---- le pied de chaque page
    const std::size_t count = d.pages.size();
    const double footY = std::max(8.0, MB * 0.5 - 3);
    for (std::size_t i = 0; i < count; ++i) {
        page = &d.pages[i];
        auto fields = c.fields;
        fields.emplace_back("Page", std::to_string(i + 1));
        fields.emplace_back("Pages", std::to_string(count));
        const std::string left = toWinAnsi(expand(L.footLeft, fields));
        const std::string center = toWinAnsi(expand(L.footCenter, fields));
        const std::string right = toWinAnsi(expand(L.footRight, fields));
        if (left.empty() && center.empty() && right.empty()) continue;
        line(ML, footY + 10, W - MR, footY + 10, 0xD0D0D0, 0.4);
        const double third = avail / 3.0;
        const double rw = textWidth(d, right, 8, false);
        if (!right.empty()) text(W - MR - std::min(rw, avail), footY, fit(right, 8, false, avail), 8, false, 0x737373);
        if (!center.empty()) {
            const std::string cfit = fit(center, 8, false, third);
            text(ML + (avail - textWidth(d, cfit, 8, false)) * 0.5, footY, cfit, 8, false, 0x737373);
        }
        if (!left.empty()) {
            const double leftRoom = (center.empty() ? avail - std::min(rw, avail) - 12 : third - 8);
            text(ML, footY, fit(left, 8, false, std::max(20.0, leftRoom)), 8, false, 0x737373);
        }
    }
    return d;
}

// ====================================================================== PDF ===
Bytes toPdf(const PageDoc& d) {
    const bool trueType = d.widths.size() == 256 && d.fontData && !d.fontData->empty();
    const auto rgb = [](std::uint32_t c, bool strokeColor) {
        return pdfNumber(r8(c)) + " " + pdfNumber(g8(c)) + " " + pdfNumber(b8(c)) + (strokeColor ? " RG " : " rg ");
    };
    std::vector<std::string> contents;
    for (const auto& ops : d.pages) {
        std::string s;
        for (const auto& o : ops) {
            switch (o.kind) {
                case DrawOp::Kind::Text:
                    if (trueType && o.bold)   // 1.25.1 : le gras d'une TrueType - le contour epaissi (le mode 2 : remplir et tracer)
                        s += "q BT /F1 " + pdfNumber(o.size) + " Tf " + rgb(o.color, false) + rgb(o.color, true) + "2 Tr " + pdfNumber(o.size * 0.03)
                           + " w " + pdfNumber(o.x) + " " + pdfNumber(o.y) + " Td " + pdfLiteral(o.text) + " Tj ET Q\n";
                    else
                        s += "BT /" + std::string(o.bold ? "F2 " : "F1 ") + pdfNumber(o.size) + " Tf " + rgb(o.color, false) + pdfNumber(o.x) + " "
                           + pdfNumber(o.y) + " Td " + pdfLiteral(o.text) + " Tj ET\n";
                    break;
                case DrawOp::Kind::Fill:
                    s += rgb(o.color, false) + pdfNumber(o.x) + " " + pdfNumber(o.y) + " " + pdfNumber(o.w) + " " + pdfNumber(o.h) + " re f\n";
                    break;
                case DrawOp::Kind::Stroke:
                    s += rgb(o.color, true) + pdfNumber(o.size) + " w " + pdfNumber(o.x) + " " + pdfNumber(o.y) + " " + pdfNumber(o.w) + " "
                       + pdfNumber(o.h) + " re S\n";
                    break;
                case DrawOp::Kind::Line:
                case DrawOp::Kind::Polyline: {
                    if (o.points.size() < 2) break;
                    s += rgb(o.color, true) + pdfNumber(o.size) + " w 1 J 1 j ";
                    for (std::size_t i = 0; i < o.points.size(); ++i)
                        s += pdfNumber(o.points[i].first) + " " + pdfNumber(o.points[i].second) + (i == 0 ? " m " : " l ");
                    s += "S\n";
                    break;
                }
                case DrawOp::Kind::Image:
                    if (o.image < 0 || static_cast<std::size_t>(o.image) >= d.images.size()) break;
                    s += "q " + pdfNumber(o.w) + " 0 0 " + pdfNumber(o.h) + " " + pdfNumber(o.x) + " " + pdfNumber(o.y) + " cm /Im"
                       + std::to_string(o.image) + " Do Q\n";
                    break;
            }
        }
        contents.push_back(std::move(s));
    }
    if (contents.empty()) contents.emplace_back();
    const std::string regular = d.font == "Times" ? "Times-Roman" : d.font == "Courier" ? "Courier" : "Helvetica";
    const std::string bold = d.font == "Times" ? "Times-Bold" : d.font == "Courier" ? "Courier-Bold" : "Helvetica-Bold";
    std::vector<std::string> objects;
    const std::size_t count = contents.size();
    // 1, 2 : le catalogue, les pages ; 3, 4 : les polices (F1, F2) ; une TrueType : 5 sa description, 6 son fichier ;
    // puis les images, puis chaque page et son contenu.
    const std::size_t firstImage = trueType ? 7 : 5, firstPage = firstImage + d.images.size();
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t p = 0; p < count; ++p) kids += std::to_string(firstPage + 2 * p) + " 0 R ";
    objects.push_back("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(count) + " >>");
    if (trueType) {
        std::string widths;
        for (int c = 32; c < 256; ++c) widths += std::to_string(static_cast<long>(std::lround(d.widths[static_cast<std::size_t>(c)]))) + " ";
        const std::string font = "<< /Type /Font /Subtype /TrueType /BaseFont /" + d.fontName + " /FirstChar 32 /LastChar 255 /Widths [ " + widths
                               + "] /FontDescriptor 5 0 R /Encoding /WinAnsiEncoding >>";
        objects.push_back(font);
        objects.push_back(font);
        objects.push_back("<< /Type /FontDescriptor /FontName /" + d.fontName + " /Flags 32 /FontBBox [" + std::to_string(d.bbox[0]) + " "
                          + std::to_string(d.bbox[1]) + " " + std::to_string(d.bbox[2]) + " " + std::to_string(d.bbox[3]) + "] /ItalicAngle 0 /Ascent "
                          + std::to_string(static_cast<long>(std::lround(d.ascent))) + " /Descent " + std::to_string(static_cast<long>(std::lround(d.descent)))
                          + " /CapHeight " + std::to_string(static_cast<long>(std::lround(d.capHeight))) + " /StemV 80 /FontFile2 6 0 R >>");
        const auto& data = *d.fontData;
        objects.push_back("<< /Length " + std::to_string(data.size()) + " /Length1 " + std::to_string(data.size()) + " >>\nstream\n"
                          + std::string(data.begin(), data.end()) + "\nendstream");
    } else {
        objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /" + regular + " /Encoding /WinAnsiEncoding >>");
        objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /" + bold + " /Encoding /WinAnsiEncoding >>");
    }
    std::string xobjects;
    for (std::size_t i = 0; i < d.images.size(); ++i) {
        const auto& img = d.images[i];
        std::string raw;
        raw.reserve(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3);
        for (std::size_t k = 0; k + 3 < img.pixels.size(); k += 4) {
            raw += static_cast<char>(img.pixels[k]);
            raw += static_cast<char>(img.pixels[k + 1]);
            raw += static_cast<char>(img.pixels[k + 2]);
        }
        objects.push_back("<< /Type /XObject /Subtype /Image /Width " + std::to_string(img.width) + " /Height " + std::to_string(img.height)
                          + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length " + std::to_string(raw.size()) + " >>\nstream\n" + raw + "\nendstream");
        xobjects += "/Im" + std::to_string(i) + " " + std::to_string(firstImage + i) + " 0 R ";
    }
    for (std::size_t p = 0; p < count; ++p) {
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pdfNumber(d.width) + " " + pdfNumber(d.height)
                          + "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >>"
                          + (xobjects.empty() ? std::string{} : " /XObject << " + xobjects + ">>") + " >> /Contents "
                          + std::to_string(firstPage + 2 * p + 1) + " 0 R >>");
        objects.push_back("<< /Length " + std::to_string(contents[p].size()) + " >>\nstream\n" + contents[p] + "endstream");
    }
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(pdf.begin(), pdf.end());
}

Bytes renderPdf(const Project& p, const Content& c, const ReportLayout& l) { return toPdf(paginate(p, c, l)); }

Bytes renderXlsx(const Content& c, const ReportLayout& l) {
    std::vector<ExportTable> sheets;
    const auto summarySheet = [&] {
        ExportTable summary;
        summary.title = "Synth\xC3\xA8se";
        summary.subtitle = c.title + " \xE2\x80\x94 " + c.subtitle;
        summary.headers = {"Rubrique", "Valeur"};
        for (const auto& [k, v] : c.summary) summary.rows.push_back({k, v});
        return summary;
    };
    for (const auto& part : partsOf(l)) {
        if (!part.on) continue;
        if (part.key == "synthese") {
            sheets.push_back(summarySheet());
            continue;
        }
        if (part.key == "courbes") {
            // 1.25.1 : les courbes des mesures en Excel - une feuille de leurs valeurs dans le temps : une ligne par instant
            // (tous ceux des courbes, dans l'ordre), une colonne par mesure (son unite) ; une case vide : pas de valeur la.
            if (c.curves.empty()) continue;
            ExportTable t;
            t.title = "Courbes des mesures";
            t.subtitle = c.title + " \xE2\x80\x94 " + c.subtitle;
            t.headers.push_back("Heure");
            std::vector<double> times;
            for (const auto& cv : c.curves) {
                t.headers.push_back(cv.name + (cv.unit.empty() ? std::string{} : " (" + cv.unit + ")"));
                for (const auto& pt : cv.points) times.push_back(pt.first);
            }
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::fabs(a - b) < 0.0005; }), times.end());
            std::vector<std::size_t> at(c.curves.size(), 0);
            for (const double time : times) {
                std::vector<std::string> row{stampOf(time, true)};
                for (std::size_t k = 0; k < c.curves.size(); ++k) {
                    const auto& pts = c.curves[k].points;
                    while (at[k] < pts.size() && pts[at[k]].first < time - 0.0005) ++at[k];
                    const double span = std::fabs(c.curves[k].hi - c.curves[k].lo);
                    const int dec = span >= 100 ? 1 : span >= 10 ? 2 : 3;
                    row.push_back(at[k] < pts.size() && std::fabs(pts[at[k]].first - time) < 0.0005 ? number(pts[at[k]].second, dec) : std::string{});
                }
                t.rows.push_back(std::move(row));
            }
            sheets.push_back(std::move(t));
            continue;
        }
        const auto* info = partInfo(part.key);
        if (!info || !info->table) continue;
        for (std::size_t i = 0; i < c.keys.size() && i < c.sections.size(); ++i)
            if (c.keys[i] == part.key) sheets.push_back(c.sections[i]);
    }
    if (sheets.empty()) sheets.push_back(summarySheet());
    return exportXlsxSheets(sheets);
}

Bytes render(const Project& p, const Content& c, ExportFormat f, const ReportLayout& l) {
    return f == ExportFormat::Excel ? renderXlsx(c, l) : renderPdf(p, c, l);
}

// ============================================================ les exemples ===
Content sample(const Project& p, const Report* r) {
    Report rr;
    if (r) rr = *r;
    else {
        rr.name = "Rapport_journalier";
        rr.events = true;
    }
    rr.period = "jour";
    rr.time = "06:00";
    Project sp;
    sp.config.name = p.config.name.empty() ? std::string("Site") : p.config.name;
    const std::string debit = "D\xC3\xA9" "bit", temp = "Temp\xC3\xA9rature";
    sp.history.archived = {"Pression", debit, temp};
    sp.displays.push_back(VariableDisplay{"Pression", "bar", "0.00"});
    sp.displays.push_back(VariableDisplay{debit, "m\xC2\xB3/h", "0"});
    sp.displays.push_back(VariableDisplay{temp, "\xC2\xB0" "C", "0.0"});
    if (!rr.measures.empty()) rr.measures.clear();      // les mesures d'exemple, quelles que soient celles du rapport
    const double from = epochOf("2026-11-04 06:00:00"), to = epochOf("2026-11-05 06:00:00");
    History h;
    struct A {
        const char* name;
        int         prio;
        const char* message;
        double      at, ack, clear;   // en minutes depuis 06:00
    };
    static const A kAlarms[] = {
        {"PT1_Haute", 2, "Pression haute ligne 2", 42, 44, 54},          {"Pompe_P1_Defaut", 1, "D\xC3\xA9" "faut pompe P1", 75, 80, 147},
        {"Filtre_Colmate", 3, "Filtre colmat\xC3\xA9", 183, 190, 228},   {"TT3_Basse", 3, "Temp\xC3\xA9rature basse", 327, 330, 335},
        {"Porte_Armoire", 4, "Porte armoire ouverte", 470, 471, 473},    {"PT1_Haute", 2, "Pression haute ligne 2", 512, 515, 530},
        {"Niveau_Bas", 2, "Niveau bas cuve 1", 600, 604, 640},           {"Vanne_V2_Discordance", 3, "Discordance vanne V2", 702, 705, 709},
        {"PT1_Haute", 2, "Pression haute ligne 2", 815, 820, 836},       {"Pompe_P1_Defaut", 1, "D\xC3\xA9" "faut pompe P1", 905, 907, 931},
        {"Porte_Armoire", 4, "Porte armoire ouverte", 1010, 1012, 1013}, {"TT3_Basse", 3, "Temp\xC3\xA9rature basse", 1102, 1105, 1121},
        {"Filtre_Colmate", 3, "Filtre colmat\xC3\xA9", 1260, 1265, 1290}, {"Niveau_Bas", 2, "Niveau bas cuve 1", 1385, -1, -1},
    };
    for (const auto& a : kAlarms) {
        AlarmOccurrence o;
        o.name = a.name;
        o.priority = a.prio;
        o.message = a.message;
        o.appeared = stampOf(from + a.at * 60, true) + ".000";
        if (a.ack >= 0) {
            o.acked = stampOf(from + a.ack * 60, true) + ".000";
            o.ackedBy = "exploitant";
        }
        if (a.clear >= 0) o.cleared = stampOf(from + a.clear * 60, true) + ".000";
        h.alarms.push_back(std::move(o));
    }
    for (int k = 0; k < 144; ++k) {
        const double t = from + k * 600.0, x = static_cast<double>(k) / 144.0 * 6.283185307179586;
        const auto add = [&](const std::string& name, double v) {
            HistorySample s;
            s.variable = name;
            s.epoch = t;
            s.stamp = stampOf(t, true) + ".000";
            s.value = v;
            h.samples.push_back(std::move(s));
        };
        add("Pression", 4.2 + 0.35 * std::sin(x * 2) + 0.12 * std::sin(x * 7));
        add(debit, 182 + 26 * std::sin(x + 0.6) + 6 * std::sin(x * 5));
        add(temp, 14 + 4.5 * std::sin(x - 1.2) + 0.6 * std::sin(x * 9));
    }
    h.events.push_back(HistoryEvent{stampOf(from + 2 * 60, true) + ".000", "Connexion", "Poste 1", "connexion", "exploitant"});
    h.events.push_back(HistoryEvent{stampOf(from + 300 * 60, true) + ".000", "Recette", "Recette_A", "recette charg\xC3\xA9" "e", "exploitant"});
    h.events.push_back(HistoryEvent{stampOf(from + 610 * 60, true) + ".000", "\xC3\x89" "criture", "Consigne_P", "4.20 -> 4.30", "chef"});
    h.events.push_back(HistoryEvent{stampOf(from + 840 * 60, true) + ".000", "D\xC3\xA9" "connexion", "Poste 1", "d\xC3\xA9" "connexion", "exploitant"});
    std::vector<Production> prod{{"Vue_Production/Compteur", ProductionFigures{12480, 312, 12792, 533, 0.94, 0.95, 0.976, 0.874, 86400, 81200, 0, true, "Poste de 06:00"}}};
    return build(sp, h, rr, from, to, prod, "2026-11-05 06:00:00.000");
}

} // namespace hmi::report
