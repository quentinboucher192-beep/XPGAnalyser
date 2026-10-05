// =============================================================================
//  hmi/HmiDisplay.cpp - la vue preparee (lot 13) : langue, unites et formats,
//                       taille des textes, couleurs, symboles
// -----------------------------------------------------------------------------
//  LES COULEURS se transforment en teinte, saturation, luminosite (TSL) :
//    daltonien   une couleur franche change de teinte - vert -> bleu (200 deg),
//                rouge -> vermillon (18), orange -> ambre (42) ; les bleus se
//                decalent un peu (225) pour ne pas se confondre avec l'ancien
//                vert ; les gris ne bougent pas ;
//    jour        un gris (saturation < 0,25) voit sa luminosite retournee
//                (1,08 - L) : les fonds sombres deviennent clairs, les textes
//                clairs sombres ; une couleur franche trop lumineuse fonce
//                jusqu'a une luminance lisible sur un fond clair (0,36 pour un
//                fond, 0,14 pour un texte : un jaune devient ocre), une couleur
//                franche tres sombre (un fond bleu nuit) s'eclaircit. Un texte
//                pose sur une couleur franche (un bouton bleu) garde la sienne.
//  Les couleurs FIXES du dessin des objets (le fond d'un champ, une fleche) :
//  displayRgb, la meme regle, appelee par le dessin en marche.
// =============================================================================
#include "HmiDisplay.hpp"

#include "HmiExpr.hpp"
#include "HmiLanguages.hpp"
#include "HmiLive.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace hmi {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "Armoires[2].ana.PT1.mes" -> "armoires[].ana.pt1.mes" : les cases oubliees.
std::string genericPath(std::string_view path) {
    std::string out;
    bool inside = false;
    for (const char c : path) {
        if (c == '[') { inside = true; out += '['; continue; }
        if (c == ']') { inside = false; out += ']'; continue; }
        if (inside || std::isspace(static_cast<unsigned char>(c))) continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool plainPath(std::string_view s) {
    const std::string t = trim(s);
    if (t.empty() || !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_')) return false;
    for (const char c : t)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']')) return false;
    return true;
}

bool isHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

// "Armoire.ana.PT1.mes" dans une vue qui declare Armoire := Armoires[0] :
// "Armoires[0].ana.PT1.mes" ; "" : pas un parametre qui designe une variable.
std::string throughParam(std::string_view path, const View* in) {
    if (!in || in->params.empty()) return {};
    const std::string t = trim(path);
    std::size_t end = 0;
    while (end < t.size() && t[end] != '.' && t[end] != '[') ++end;
    const auto* prm = in->param(std::string_view(t).substr(0, end));
    if (!prm) return {};
    const std::string def = trim(prm->defaultValue);
    if (def.empty() || !plainPath(genericPath(def))) return {};
    return def + t.substr(end);
}

// ---- les couleurs en TSL ----------------------------------------------------------------
struct Rgb {
    int r{0}, g{0}, b{0}, a{-1};   // a : -1 sans canal alpha
};

bool parseColor(std::string_view s, Rgb& out) {
    const std::string t = trim(s);
    if ((t.size() != 7 && t.size() != 9) || t[0] != '#') return false;
    for (std::size_t i = 1; i < t.size(); ++i)
        if (!isHex(t[i])) return false;
    const auto byte = [&](std::size_t at) { return static_cast<int>(std::strtol(t.substr(at, 2).c_str(), nullptr, 16)); };
    out.r = byte(1);
    out.g = byte(3);
    out.b = byte(5);
    out.a = t.size() == 9 ? byte(7) : -1;
    return true;
}

std::string colorText(const Rgb& c) {
    char buf[16];
    if (c.a >= 0) std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

struct Hsl {
    double h{0}, s{0}, l{0};   // h en degres, s et l de 0 a 1
};

Hsl toHsl(const Rgb& c) {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    Hsl out;
    out.l = (mx + mn) / 2;
    const double d = mx - mn;
    if (d < 1e-9) return out;
    out.s = out.l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) out.h = std::fmod((g - b) / d + (g < b ? 6 : 0), 6.0);
    else if (mx == g) out.h = (b - r) / d + 2;
    else out.h = (r - g) / d + 4;
    out.h *= 60;
    return out;
}

Rgb fromHsl(const Hsl& in, int alpha) {
    const double s = std::clamp(in.s, 0.0, 1.0), l = std::clamp(in.l, 0.0, 1.0);
    double h = std::fmod(in.h, 360.0);
    if (h < 0) h += 360;
    const double c = (1 - std::fabs(2 * l - 1)) * s;
    const double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1));
    const double m = l - c / 2;
    double r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    const auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return {byte(r + m), byte(g + m), byte(b + m), alpha};
}

Hsl colorBlind(Hsl c) {
    if (c.s < 0.2) return c;
    const double h = c.h;
    if (h >= 75 && h < 170) c.h = 200;                       // vert -> bleu
    else if (h >= 340 || h < 15) c.h = 18;                   // rouge -> vermillon
    else if (h >= 15 && h < 45) c.h = 42;                    // orange -> ambre
    else if (h >= 170 && h < 250) c.h = 225;                 // bleu : un peu plus loin du nouveau "vert"
    return c;
}

// La luminance relative (0 : noir, 1 : blanc), les canaux lineaires.
double luminance(const Rgb& c) {
    const auto lin = [](int v) {
        const double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

Hsl dayTheme(Hsl c, bool onColored, bool text) {
    if (onColored) return c;
    if (c.s < 0.25) {
        c.l = std::clamp(1.08 - c.l, 0.08, 0.97);
        return c;
    }
    if (c.l < 0.25) {                                        // un fond sombre et franc : clair ; un texte sombre reste
        if (!text) c.l = std::clamp(0.92 - c.l, 0.6, 0.9);
        return c;
    }
    // Lisible sur un fond clair : fonce jusqu'a la luminance voulue.
    const double cap = text ? 0.14 : 0.36;
    for (int i = 0; i < 45 && c.l > 0.1 && luminance(fromHsl(c, -1)) > cap; ++i) c.l -= 0.02;
    return c;
}

// Un texte et la cle de son fond, dans le meme objet.
std::string_view backgroundKeyOf(std::string_view textKey) {
    if (textKey == "activeTextColor") return "activeColor";
    if (textKey == "titleColor") return "titleFill";
    if (textKey == "headerTextColor") return "headerColor";
    return {};
}

bool isColoredBackground(const Object& o, std::string_view key) {
    const auto check = [&](std::string_view k) {
        Rgb c;
        const auto* p = o.find(k);
        if (!p || !p->expr.empty() || !parseColor(p->value, c)) return false;
        if (c.a >= 0 && c.a < 128) return false;
        return toHsl(c).s >= 0.25;
    };
    if (!key.empty()) return check(key);
    const auto* fill = o.find("fill");
    if (fill && !trim(fill->value).empty()) return check("fill");
    return check("buttonColor");
}

// Les cles qui portent des textes ou des noms, pas des couleurs.
bool colorless(std::string_view key) {
    return isTranslatableKey(key) || isTranslatableList(key) || key == "image" || key == "variable" || key == "variables"
        || key == "views" || key == "symbol" || key == "params" || key == "fileName" || key == "cells" || key == "mapZones";
}

// Remplace les couleurs d'une expression : seulement dans ses chaines ('...').
std::string colorsInExpression(std::string_view expr, const DisplayOptions& o) {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (expr[i] != '\'') { out += expr[i++]; continue; }
        const auto close = expr.find('\'', i + 1);
        if (close == std::string_view::npos) { out += expr.substr(i); break; }
        out += '\'';
        out += displayColors(expr.substr(i + 1, close - i - 1), o);
        out += '\'';
        i = close + 1;
    }
    return out;
}

bool multiVariable(Kind k) {
    return k == Kind::BarChart || k == Kind::XYChart || k == Kind::StateChart || k == Kind::PieChart || k == Kind::RadarChart
        || k == Kind::Histogram || k == Kind::VariableTable || k == Kind::Trend;
}

} // namespace

// ================================================================ reglages ===
DisplayOptions projectDisplay(const Project& p) {
    DisplayOptions o;
    const auto& l = p.languages;
    const Language* start = l.startLanguage.empty() ? nullptr : l.find(l.startLanguage);
    o.language = start ? start->code : l.source();
    o.textScale = p.config.textScale;
    o.colorMode = p.config.colorMode;
    o.symbols = p.config.statusSymbols;
    o.theme = p.config.theme;
    return o;
}

bool plainDisplay(const Project& p, const DisplayOptions& o) {
    return !isTranslated(p.languages, o.language) && o.textScale == 100 && o.colorMode != "daltonien" && !o.symbols && o.theme != "jour"
        && p.displays.empty();
}

// ================================================================ unites =====
const VariableDisplay* displayOf(const Project& p, std::string_view path, const View* in) {
    const std::string wanted = trim(path);
    if (wanted.empty() || p.displays.empty()) return nullptr;
    const std::string exact = lowerAscii(wanted);
    for (const auto& d : p.displays)
        if (lowerAscii(trim(d.path)) == exact) return &d;
    const std::string generic = genericPath(wanted);
    for (const auto& d : p.displays)
        if (genericPath(d.path) == generic) return &d;
    if (const std::string via = throughParam(wanted, in); !via.empty()) return displayOf(p, via);
    return nullptr;
}

std::string displayRowPath(std::string_view path, const View* in) {
    std::string t = throughParam(path, in);
    if (t.empty()) t = trim(path);
    std::string out;
    bool inside = false;
    for (const char c : t) {
        if (c == '[') { inside = true; out += c; continue; }
        if (c == ']') { inside = false; out += c; continue; }
        if (!inside && !std::isspace(static_cast<unsigned char>(c))) out += c;
    }
    return out;
}

std::string displayVariableOf(const Object& o) {
    if (const auto* v = o.find("value"); v && !v->expr.empty()) return plainPath(v->expr) ? trim(v->expr) : std::string{};
    return trim(autoValueSource(o));
}

bool followsVariableFormat(const Object& o) { return o.flag("varFormat", true); }

std::string effectiveFormat(const Project& p, const Object& o, std::string_view fallback) {
    if (followsVariableFormat(o))
        if (const auto* d = displayOf(p, displayVariableOf(o)); d && !trim(d->format).empty()) return trim(d->format);
    const std::string own = o.text("format");
    return own.empty() ? std::string(fallback) : own;
}

std::string effectiveUnit(const Project& p, const Object& o) {
    if (followsVariableFormat(o))
        if (const auto* d = displayOf(p, displayVariableOf(o)); d && !trim(d->unit).empty()) return trim(d->unit);
    return o.text("unit");
}

std::string rowFormat(const Project* p, const Object& o, std::size_t row) {
    const auto formats = splitSemicolons(o.text("formats"));
    if (row < formats.size() && !trim(formats[row]).empty()) return trim(formats[row]);
    if (p && followsVariableFormat(o)) {
        const auto vars = splitSemicolons(o.text("variables"));
        if (row < vars.size())
            if (const auto* d = displayOf(*p, vars[row]); d && !trim(d->format).empty()) return trim(d->format);
    }
    return o.text("format", "0.00");
}

std::string formatTemplate(std::string_view text, const Project& p, const View* in) {
    if (text.find('{') == std::string_view::npos) return std::string(text);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            out += text.substr(i, 2);
            i += 2;
            continue;
        }
        if (c != '{') { out += c; ++i; continue; }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) { out += text.substr(i); break; }
        std::string inside(text.substr(i + 1, close - i - 1));
        std::string expr = inside, format;
        bool withUnit = false;
        const auto colon = inside.rfind(':');
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string f = inside.substr(colon + 1);
            if (f == "u" || f == "U") { withUnit = true; expr = inside.substr(0, colon); }
            else if (looksLikeFormat(f)) { format = f; expr = inside.substr(0, colon); }
        }
        const VariableDisplay* d = plainPath(expr) ? displayOf(p, expr, in) : nullptr;
        if (format.empty() && d && !trim(d->format).empty()) format = trim(d->format);
        out += '{' + expr + (format.empty() ? std::string{} : ":" + format) + '}';
        if (withUnit && d && !trim(d->unit).empty()) out += " " + trim(d->unit);
        i = close + 1;
    }
    return out;
}

std::string displaySample(const VariableDisplay& d) {
    const std::string f = trim(d.format);
    std::string s = formatValue(sim::Value::real(1234.5678), f.empty() ? std::string_view("0.0") : std::string_view(f));
    if (!trim(d.unit).empty()) s += " " + trim(d.unit);
    return s;
}

std::vector<std::string> displayUses(const Project& p, const VariableDisplay& d) {
    std::vector<std::string> out;
    const auto add = [&](std::string s) {
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
    };
    for (const auto& v : p.views) {
        const auto mine = [&](std::string_view path) { return displayOf(p, path, &v) == &d; };
        for (const auto& o : v.objects) {
            const std::string where = v.name + "." + o.name;
            if (o.kind == Kind::VariableTable) {
                for (const auto& var : splitSemicolons(o.text("variables")))
                    if (mine(var)) add(where);
            } else if (!multiVariable(o.kind) && (o.find("unit") || o.find("format")) && mine(displayVariableOf(o))) {
                add(where);
            }
            for (const auto& prop : o.props) {
                if (!prop.expr.empty() || !isTranslatableKey(prop.key) || prop.value.find('{') == std::string::npos) continue;
                for (const auto& hole : templateHoles(prop.value))
                    if (plainPath(hole) && mine(hole)) add(where);
            }
        }
    }
    return out;
}

bool validDisplayPath(std::string_view path) noexcept {
    const auto t = path;
    if (t.empty() || !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_')) return false;
    int depth = 0;
    for (const char c : t) {
        if (c == '[') { if (++depth > 1) return false; continue; }
        if (c == ']') { if (--depth < 0) return false; continue; }
        if (c == '*' && depth == 1) continue;
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return depth == 0;
}

// ================================================================ couleurs ===
std::string displayColor(std::string_view color, const DisplayOptions& o, bool onColored, bool isText) {
    Rgb c;
    if (!parseColor(color, c)) return std::string(color);
    const bool blind = o.colorMode == "daltonien", day = o.theme == "jour";
    if (!blind && !day) return std::string(color);
    Hsl h = toHsl(c);
    if (blind) h = colorBlind(h);
    if (day) h = dayTheme(h, onColored, isText);
    return colorText(fromHsl(h, c.a));
}

bool changesColors(const DisplayOptions& o) noexcept { return o.colorMode == "daltonien" || o.theme == "jour"; }

std::uint32_t displayRgb(std::uint32_t rgb, const DisplayOptions& o, bool onColored, bool isText) {
    const bool blind = o.colorMode == "daltonien", day = o.theme == "jour";
    if (!blind && !day) return rgb;
    // Le dessin redemande sans cesse les memes couleurs : gardees (par reglage).
    thread_local std::unordered_map<std::uint64_t, std::uint32_t> known;
    const std::uint64_t key = (static_cast<std::uint64_t>(rgb) & 0xFFFFFFu) | (blind ? 1ull << 24 : 0) | (day ? 1ull << 25 : 0)
                            | (onColored ? 1ull << 26 : 0) | (isText ? 1ull << 27 : 0);
    if (const auto it = known.find(key); it != known.end()) return it->second;
    const Rgb c{static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), -1};
    Hsl h = toHsl(c);
    if (blind) h = colorBlind(h);
    if (day) h = dayTheme(h, onColored, isText);
    const Rgb out = fromHsl(h, -1);
    const std::uint32_t v = (static_cast<std::uint32_t>(out.r) << 16) | (static_cast<std::uint32_t>(out.g) << 8) | static_cast<std::uint32_t>(out.b);
    if (known.size() > 4096) known.clear();
    known.emplace(key, v);
    return v;
}

std::string displayColors(std::string_view text, const DisplayOptions& o) {
    if (text.find('#') == std::string_view::npos || (o.colorMode != "daltonien" && o.theme != "jour")) return std::string(text);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '#') { out += text[i++]; continue; }
        std::size_t n = 0;
        while (i + 1 + n < text.size() && isHex(text[i + 1 + n])) ++n;
        if (n == 6 || n == 8) {
            out += displayColor(text.substr(i, 1 + n), o);
            i += 1 + n;
        } else {
            out += text[i++];
        }
    }
    return out;
}

int statusSymbolOf(std::string_view color) noexcept {
    Rgb c;
    if (!parseColor(color, c)) return 0;
    const Hsl h = toHsl(c);
    if (h.s < 0.2 || h.l < 0.12) return 4;
    if (h.h >= 75 && h.h < 260) return 1;     // vert, bleu : en marche, bien
    if (h.h >= 340 || h.h < 15) return 2;     // rouge : en defaut
    if (h.h < 75) return 3;                   // orange, jaune : attention
    return 3;                                 // violet, magenta : a regarder
}

// ================================================================ theme =======
std::string themeSelectorHit(const Object& o, double w, double h, double x, double y) {
    const auto l = languageSelectorLayout(o, w, h, 2);
    for (std::size_t i = 0; i < l.buttons.size(); ++i)
        if (l.buttons[i].contains(x, y)) return i == 0 ? std::string("theme:jour") : std::string("theme:nuit");
    return {};
}

std::pair<std::string, std::string> themeLabels(const Object& o) {
    const auto items = splitSemicolons(o.text("themeLabels", "Jour;Nuit"));
    const std::string day = !items.empty() && !trim(items[0]).empty() ? trim(items[0]) : std::string("Jour");
    const std::string night = items.size() > 1 && !trim(items[1]).empty() ? trim(items[1]) : std::string("Nuit");
    return {day, night};
}

// ================================================================ la vue =====
View displayView(const View& v, const Project& p, const DisplayOptions& o) {
    View out = isTranslated(p.languages, o.language) ? translatedView(v, p.languages, o.language) : v;
    const bool colors = o.colorMode == "daltonien" || o.theme == "jour";
    const double scale = std::clamp(o.textScale, 50, 300) / 100.0;
    for (auto& obj : out.objects) {
        // L'unite et le format de la variable montree.
        if (!p.displays.empty()) {
            if (obj.kind == Kind::VariableTable && followsVariableFormat(obj)) {
                const auto vars = splitSemicolons(obj.text("variables"));
                auto units = splitSemicolons(obj.text("units"));
                auto formats = splitSemicolons(obj.text("formats"));
                units.resize(std::max(units.size(), vars.size()));
                formats.resize(std::max(formats.size(), vars.size()));
                bool changed = false;
                for (std::size_t k = 0; k < vars.size(); ++k) {
                    const auto* d = displayOf(p, vars[k], &v);
                    if (!d) continue;
                    if (trim(units[k]).empty() && !trim(d->unit).empty()) { units[k] = trim(d->unit); changed = true; }
                    if (trim(formats[k]).empty() && !trim(d->format).empty()) { formats[k] = trim(d->format); changed = true; }
                }
                if (changed) {
                    obj.set("units", joinSemicolons(units));
                    obj.set("formats", joinSemicolons(formats));
                }
            } else if (!multiVariable(obj.kind) && followsVariableFormat(obj) && (obj.find("unit") || obj.find("format"))) {
                if (const auto* d = displayOf(p, displayVariableOf(obj), &v)) {
                    if (auto* u = obj.find("unit"); u && u->expr.empty() && !trim(d->unit).empty()) u->value = trim(d->unit);
                    if (auto* f = obj.find("format"); f && f->expr.empty() && !trim(d->format).empty()) f->value = trim(d->format);
                }
            }
        }
        for (auto& prop : obj.props) {
            // Les trous des textes : le format de leur variable ({X:u} : et son unite).
            if (!p.displays.empty() && prop.expr.empty() && isTranslatableKey(prop.key) && prop.value.find('{') != std::string::npos
                && obj.kind != Kind::QrCode)
                prop.value = formatTemplate(prop.value, p, &v);
            // La taille des textes.
            if (scale != 1.0 && (prop.key == "fontSize" || (prop.key.size() > 8 && prop.key.compare(prop.key.size() - 8, 8, "FontSize") == 0))
                && prop.expr.empty()) {
                double n = 0;
                if (parseNumber(prop.value, n)) prop.value = formatNumber(std::round(n * scale * 10) / 10);
            }
        }
        // Les couleurs : daltonien, jour.
        if (colors) {
            // Le fond des textes, lu avant qu'il ne change.
            const bool onFill = isColoredBackground(obj, {});
            std::vector<std::pair<std::string, bool>> textKeys;
            for (const auto& prop : obj.props) {
                const std::string_view k = prop.key;
                const bool textKey = k == "textColor" || k == "activeTextColor" || k == "titleColor" || k == "headerTextColor" || k == "linkColor";
                if (textKey) textKeys.emplace_back(prop.key, backgroundKeyOf(k).empty() ? onFill : isColoredBackground(obj, backgroundKeyOf(k)));
            }
            for (auto& prop : obj.props) {
                if (colorless(prop.key)) continue;
                if (!prop.expr.empty()) {
                    if (prop.expr.find('#') != std::string::npos) prop.expr = colorsInExpression(prop.expr, o);
                    continue;
                }
                if (prop.value.find('#') == std::string::npos) continue;
                bool onColored = false, isText = false;
                for (const auto& [k, colored] : textKeys)
                    if (k == prop.key) { onColored = colored; isText = true; }
                // La couleur d'une chose (le bouton d'un potentiometre) : le theme ne la change pas ;
                // le texte des tuiles du resume des alarmes est pose sur leurs couleurs d'etat, celui
                // d'un plan a zones sur son image.
                if (prop.key == "knobColor"
                    || ((obj.kind == Kind::AlarmSummary || obj.kind == Kind::ZoneMap) && (prop.key == "textColor" || prop.key == "selectedColor")))
                    onColored = true;
                Rgb probe;
                prop.value = parseColor(prop.value, probe) ? displayColor(prop.value, o, onColored, isText) : displayColors(prop.value, o);
            }
        }
        // Les symboles : dans les voyants ; un triangle sur un symbole de synoptique en defaut.
        if (o.symbols && (obj.kind == Kind::Indicator || obj.kind == Kind::MultiStateIndicator || kindIsSynoptic(obj.kind)))
            obj.set("a11ySymbols", "TRUE");
    }
    if (colors) out.background = displayColor(out.background, o);
    return out;
}

} // namespace hmi
