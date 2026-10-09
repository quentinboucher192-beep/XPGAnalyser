#include "HmiNatives.hpp"
#include "HmiTypeRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <mutex>
#include <random>

namespace hmi::natives {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
std::string_view trimmed(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
// Un parametre qui en vaut plusieurs : "IN1...INn" (les points de suspension).
bool variadic(const Param& p) noexcept { return p.name.find("\xE2\x80\xA6") != std::string_view::npos; }

// ------------------------------------------------------------------- les couleurs ---
struct Rgba { double r{0}, g{0}, b{0}, a{255}; };
std::uint8_t channel(double v) noexcept { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }
std::string colorText(const Rgba& c, bool withAlpha) {
    char buf[16];
    if (withAlpha) std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", channel(c.r), channel(c.g), channel(c.b), channel(c.a));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X", channel(c.r), channel(c.g), channel(c.b));
    return buf;
}
bool readColor(const sim::Value& v, Rgba& out) {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    if (!parseColor(v.type() == sim::Type::String ? v.asString() : v.display(), r, g, b, a)) return false;
    out = {double(r), double(g), double(b), double(a)};
    return true;
}
Rgba mix(const Rgba& x, const Rgba& y, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}
double hueToRgb(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

// ------------------------------------------------------------------- l'aleatoire ---
std::mutex& randomMutex() {
    static std::mutex m;
    return m;
}
std::mt19937& randomEngine() {
    static std::mt19937 e(static_cast<std::uint32_t>(std::time(nullptr)));
    return e;
}
std::uint32_t nextRandom() {
    std::lock_guard<std::mutex> lock(randomMutex());
    return randomEngine()();
}

// ---------------------------------------------------------------- les conversions ---
const std::vector<std::string_view> kConversionTypes = {"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT",
                                                         "BYTE", "WORD", "DWORD", "LWORD", "REAL", "LREAL", "STRING", "TIME"};
bool isIntegerType(std::string_view t) noexcept {
    return t == "SINT" || t == "INT" || t == "DINT" || t == "LINT" || t == "USINT" || t == "UINT" || t == "UDINT" || t == "ULINT"
        || t == "BYTE" || t == "WORD" || t == "DWORD" || t == "LWORD";
}
std::string cTypeOf(std::string_view t) {
    if (const auto* x = typeExtra(t)) {
        const std::string c(x->cType);
        const auto sp = c.find(' ');
        return sp == std::string::npos ? c : c.substr(0, sp);
    }
    return std::string(t);
}
std::string behaviourOf(std::string_view a, std::string_view b) {
    const bool real = a == "REAL" || a == "LREAL";
    if (a == "STRING" && (isIntegerType(b) || b == "REAL" || b == "LREAL"))
        return "Le texte se lit en d\xC3\xA9" "cimal (espaces de t\xC3\xAAte ignor\xC3\xA9s, signe permis) ; un texte illisible donne 0.";
    if (a == "STRING" && b == "BOOL") return "Le texte se lit comme un entier : TRUE s'il ne vaut pas 0.";
    if (a == "STRING" && b == "TIME") return "Le texte se lit comme un nombre de millisecondes.";
    if (b == "STRING") return "Le texte affich\xC3\xA9 de la valeur (TRUE, 42, 2.6, T#1500ms).";
    if (real && isIntegerType(b))
        return "Arrondi au plus proche (2.5 donne 3), comme la norme ; hors de la plage de " + std::string(b)
             + ", les bits de poids fort sont perdus.";
    if (real && b == "TIME") return "Le r\xC3\xA9" "el compte des millisecondes, arrondi au plus proche.";
    if (b == "BOOL") return "TRUE si la valeur ne vaut pas 0.";
    if (a == "BOOL") return "TRUE donne 1, FALSE donne 0.";
    if (b == "TIME") return "L'entier compte des millisecondes.";
    if (a == "TIME") return "La dur\xC3\xA9" "e en millisecondes.";
    if (b == "REAL" || b == "LREAL") return "La m\xC3\xAAme valeur, en r\xC3\xA9" "el.";
    return "La m\xC3\xAAme valeur ; hors de la plage de " + std::string(b)
         + ", les bits de poids fort sont perdus (DINT_TO_INT(100000) = -31072).";
}
std::string cConversion(std::string_view a, std::string_view b) {
    const std::string cb = cTypeOf(b);
    if (b == "STRING")
        return std::string("snprintf(texte, sizeof texte, \"%") + ((a == "REAL" || a == "LREAL") ? "f" : "d") + "\", valeur);";
    if (a == "STRING") {
        if (b == "REAL") return "valeur = strtof(texte, NULL);";
        if (b == "LREAL") return "valeur = strtod(texte, NULL);";
        return "valeur = (" + cb + ")strtol(texte, NULL, 10);";
    }
    if ((a == "REAL" || a == "LREAL") && isIntegerType(b)) return "valeur = (" + cb + ")lround(x);";
    if (b == "BOOL") return "valeur = (x != 0);";
    return "valeur = (" + cb + ")x;";
}
std::string cppConversion(std::string_view a, std::string_view b) {
    const std::string cb = cTypeOf(b);
    if (b == "STRING") return "texte = std::to_string(valeur);";
    if (a == "STRING") {
        if (b == "REAL") return "valeur = std::stof(texte);";
        if (b == "LREAL") return "valeur = std::stod(texte);";
        if (b == "BOOL") return "valeur = std::stoi(texte) != 0;";
        return "valeur = static_cast<" + cb + ">(std::stol(texte));";
    }
    if ((a == "REAL" || a == "LREAL") && isIntegerType(b)) return "valeur = static_cast<" + cb + ">(std::lround(x));";
    if (b == "TIME") return "valeur = std::chrono::milliseconds(x);";
    if (a == "TIME") return "valeur = duree.count();   // des ms";
    return "valeur = static_cast<" + cb + ">(x);";
}

// "32767" -> "32 767" (l'espace fine insecable).
std::string grouped(long double v) {
    const bool negative = v < 0;
    std::string digits;
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0Lf", negative ? -v : v);
        digits = buf;
    }
    std::string out;
    int n = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (n > 0 && n % 3 == 0) out.insert(0, "\xE2\x80\xAF");
        out.insert(out.begin(), *it);
        ++n;
    }
    return negative ? "-" + out : out;
}

} // namespace

// ------------------------------------------------------------- autour du catalogue ---
const Function* function(std::string_view name) noexcept {
    for (const auto& f : functions())
        if (iequals(f.name, name)) return &f;
    return nullptr;
}
const Category* category(std::string_view id) noexcept {
    for (const auto& c : categories())
        if (c.id == id) return &c;
    return nullptr;
}
const TypeExtra* typeExtra(std::string_view name) noexcept {
    for (const auto& t : typeExtras())
        if (iequals(t.name, name)) return &t;
    return nullptr;
}

std::string signature(const Function& f) {
    std::string s(f.name);
    s += '(';
    for (std::size_t i = 0; i < f.params.size(); ++i) {
        const auto& p = f.params[i];
        if (i) s += "; ";
        if (p.optional) s += '[';
        s += std::string(p.name) + " : " + std::string(p.type);
        if (p.optional) s += ']';
    }
    return s + ") : " + std::string(f.returns);
}
std::string shortSignature(const Function& f) {
    std::string s = "(";
    for (std::size_t i = 0; i < f.params.size(); ++i) {
        if (i) s += ", ";
        s += f.params[i].optional ? "[" + std::string(f.params[i].name) + "]" : std::string(f.params[i].name);
    }
    return s + ") : " + std::string(f.returns);
}

bool arity(std::string_view name, int& min, int& max) noexcept {
    if (const auto* f = function(name)) {
        min = 0;
        max = 0;
        bool open = false;
        for (const auto& p : f->params) {
            if (variadic(p)) {
                open = true;
                min += p.name.rfind("IN0", 0) == 0 ? 1 : 2;   // MUX(K, IN0...) : 1 de plus ; MIN, MAX, CONCAT : 2
                continue;
            }
            ++max;
            if (!p.optional) ++min;
        }
        if (open) max = -1;
        return true;
    }
    if (conversion(name)) {
        min = max = 1;
        return true;
    }
    return false;
}

const std::vector<std::string_view>& conversionTypes() { return kConversionTypes; }

std::vector<Conversion> conversions() {
    std::vector<Conversion> out;
    out.reserve(kConversionTypes.size() * (kConversionTypes.size() - 1));
    for (const auto a : kConversionTypes)
        for (const auto b : kConversionTypes) {
            if (a == b) continue;
            Conversion c;
            c.from = std::string(a);
            c.to = std::string(b);
            c.name = c.from + "_TO_" + c.to;
            c.behaviour = behaviourOf(a, b);
            c.st = "Resultat := " + c.name + "(Valeur);";
            c.c = cConversion(a, b);
            c.cpp = cppConversion(a, b);
            out.push_back(std::move(c));
        }
    return out;
}

std::optional<Conversion> conversion(std::string_view name) {
    const std::string u = upper(trimmed(name));
    const auto at = u.find("_TO_");
    if (at == std::string::npos) return std::nullopt;
    const std::string a = u.substr(0, at), b = u.substr(at + 4);
    const auto known = [](const std::string& t) {
        return std::find(kConversionTypes.begin(), kConversionTypes.end(), t) != kConversionTypes.end();
    };
    if (a == b || !known(a) || !known(b)) return std::nullopt;
    Conversion c;
    c.from = a;
    c.to = b;
    c.name = u;
    c.behaviour = behaviourOf(a, b);
    c.st = "Resultat := " + u + "(Valeur);";
    c.c = cConversion(a, b);
    c.cpp = cppConversion(a, b);
    return c;
}

// -------------------------------------------------------------- enumerations natives ---
const NativeEnum* nativeEnum(std::string_view name) noexcept {
    name = trimmed(name);
    for (const auto& e : enums())
        if (iequals(e.name, name)) return &e;
    return nullptr;
}

bool parseEnumLiteral(std::string_view text, const NativeEnum** e, const EnumValue** v) noexcept {
    text = trimmed(text);
    const auto hash = text.find('#');
    if (hash == std::string_view::npos || hash == 0) return false;
    const auto* en = nativeEnum(text.substr(0, hash));
    if (!en) return false;
    const auto what = trimmed(text.substr(hash + 1));
    for (const auto& val : en->values) {
        bool hit = iequals(val.name, what);
        if (!hit && !what.empty() && std::all_of(what.begin(), what.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            hit = std::to_string(val.number) == what;
        if (hit) {
            if (e) *e = en;
            if (v) *v = &val;
            return true;
        }
    }
    return false;
}

std::optional<std::string> enumArgument(std::string_view fn, std::size_t index, const sim::Value& v) {
    if (v.type() == sim::Type::String || v.type() == sim::Type::Real || v.type() == sim::Type::Unknown) return std::nullopt;
    for (const auto& e : enums()) {
        if (e.argument < 0 || static_cast<std::size_t>(e.argument) != index || !iequals(e.function, fn)) continue;
        for (const auto& val : e.values)
            if (val.number == v.asInteger()) return std::string(val.argument);
    }
    return std::nullopt;
}

// 1.12.1 : les proprietes des objets.
const NativeEnum* propertyEnum(std::string_view kind, std::string_view key) noexcept {
    const NativeEnum* any = nullptr;
    for (const auto& e : enums())
        for (const auto& u : e.uses) {
            if (u.key != key) continue;
            if (u.kind == kind) return &e;
            if (u.kind == "*" && !any) any = &e;
        }
    return any;
}

std::optional<std::string> propertyWord(std::string_view kind, std::string_view key, const sim::Value& v) {
    if (v.type() == sim::Type::String || v.type() == sim::Type::Real || v.type() == sim::Type::Unknown) return std::nullopt;
    const auto* e = propertyEnum(kind, key);
    if (!e) return std::nullopt;
    for (const auto& val : e->values)
        if (val.number == v.asInteger()) return std::string(val.argument);
    return std::nullopt;
}

const NativeEnum* enumForWords(const std::vector<std::string>& words) noexcept {
    const NativeEnum* best = nullptr;
    for (const auto& e : enums()) {
        if (e.uses.empty() || e.values.empty() || e.values.size() > words.size()) continue;
        bool same = true;
        for (std::size_t i = 0; same && i < e.values.size(); ++i)   // dans un autre ordre aussi (le menu de connexion)
            same = std::find(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(e.values.size()), e.values[i].argument)
                != words.begin() + static_cast<std::ptrdiff_t>(e.values.size());
        for (std::size_t i = e.values.size(); same && i < words.size(); ++i) same = words[i].find(':') != std::string::npos;   // recette:..., objet:...
        if (same && (!best || e.values.size() > best->values.size())) best = &e;
    }
    return best;
}

std::vector<EnumLiteralUse> enumLiteralsIn(std::string_view x) {
    std::vector<EnumLiteralUse> out;
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::size_t i = 0;
    while (i < x.size()) {
        const char c = x[i];
        if (c == '\'' || c == '"') {                                   // une chaine : sautee
            const auto end = x.find(c, i + 1);
            i = end == std::string_view::npos ? x.size() : end + 1;
            continue;
        }
        if (c == '(' && i + 1 < x.size() && x[i + 1] == '*') {          // (* commentaire *)
            const auto end = x.find("*)", i + 2);
            i = end == std::string_view::npos ? x.size() : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < x.size() && x[i + 1] == '/') {          // // commentaire
            const auto end = x.find('\n', i);
            i = end == std::string_view::npos ? x.size() : end + 1;
            continue;
        }
        if (!ident(c) || std::isdigit(static_cast<unsigned char>(c))) {
            if (std::isdigit(static_cast<unsigned char>(c)))              // 16#FF, 2#1010 : des nombres
                while (i < x.size() && (ident(x[i]) || x[i] == '#' || x[i] == '.')) ++i;
            else ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < x.size() && ident(x[i])) ++i;
        if (i >= x.size() || x[i] != '#') continue;
        std::size_t end = i + 1;
        while (end < x.size() && ident(x[end])) ++end;
        const auto text = x.substr(start, end - start);
        const NativeEnum* e = nullptr;
        if (end > i + 1 && parseEnumLiteral(text, &e, nullptr)) out.push_back({e, std::string(text)});
        i = end;
    }
    return out;
}

std::string propertyProblem(std::string_view kind, std::string_view key, std::string_view expression) {
    const auto* want = propertyEnum(kind, key);
    if (!want) return {};
    const auto t = trimmed(expression);
    const auto lits = enumLiteralsIn(t);
    const bool other = lits.size() == 1 && lits.front().enumeration != want && t.size() == lits.front().text.size();
    bool outside = !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    for (const auto& val : want->values) outside = outside && std::to_string(val.number) != t;
    if (!other && !outside) return {};
    std::string choices;
    for (const auto& val : want->values) choices += (choices.empty() ? "" : ", ") + std::string(want->name) + "#" + std::string(val.name);
    return (other ? lits.front().text : std::string(t)) + " n'est pas une valeur de " + std::string(want->name) + " : la propri\xC3\xA9t\xC3\xA9 "
         + std::string(key) + " attend " + choices;
}

// ------------------------------------------------------- les fonctions propres a l'IHM ---
bool isOwnFunction(std::string_view name) noexcept {
    const auto* f = function(name);
    return f && (f->category == "couleur" || f->category == "alea" || f->category == "date");   // date : 1.12.1
}

bool parseColor(std::string_view t, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) noexcept {
    t = trimmed(t);
    if (!t.empty() && t.front() == '#') t.remove_prefix(1);
    else if (t.size() > 3 && t.substr(0, 3) == "16#") t.remove_prefix(3);
    if (t.size() != 6 && t.size() != 8) return false;
    std::uint32_t v = 0;
    for (const char c : t) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
    }
    if (t.size() == 6) {
        r = static_cast<std::uint8_t>(v >> 16);
        g = static_cast<std::uint8_t>(v >> 8);
        b = static_cast<std::uint8_t>(v);
        a = 255;
    } else {
        r = static_cast<std::uint8_t>(v >> 24);
        g = static_cast<std::uint8_t>(v >> 16);
        b = static_cast<std::uint8_t>(v >> 8);
        a = static_cast<std::uint8_t>(v);
    }
    return true;
}

void seedRandom(std::uint32_t seed) noexcept {
    std::lock_guard<std::mutex> lock(randomMutex());
    randomEngine().seed(seed);
}

bool call(std::string_view name, const std::vector<sim::Value>& args, sim::Value& out, std::string* why) {
    const std::string u = upper(name);
    const auto* f = function(u);
    if (!f || (f->category != "couleur" && f->category != "alea" && f->category != "date")) return false;
    int min = 0, max = 0;
    (void)arity(u, min, max);
    const int n = static_cast<int>(args.size());
    if (n < min || (max >= 0 && n > max)) {
        if (why) *why = u + " : " + std::to_string(min) + (max == min ? "" : " \xC3\xA0 " + std::to_string(max)) + " argument(s) attendu(s), "
                      + std::to_string(n) + " donn\xC3\xA9(s)";
        return false;
    }
    const auto real = [&](int i) { return args[static_cast<std::size_t>(i)].asReal(); };
    const auto color = [&](int i, Rgba& c) {
        if (readColor(args[static_cast<std::size_t>(i)], c)) return true;
        if (why) *why = u + " : couleur illisible '" + args[static_cast<std::size_t>(i)].display() + "' (attendu : '#RRGGBB' ou '#RRGGBBAA')";
        return false;
    };
    const auto text = [&](const Rgba& c, bool alpha) {
        out = sim::Value::text(colorText(c, alpha || channel(c.a) != 255));
        return true;
    };
    if (u == "RGB") return text({real(0), real(1), real(2), 255}, false);
    if (u == "RGBA") return text({real(0), real(1), real(2), real(3)}, true);
    if (u == "HSL") {
        double h = std::fmod(real(0), 360.0);
        if (h < 0) h += 360.0;
        h /= 360.0;
        const double s = std::clamp(real(1), 0.0, 100.0) / 100.0, l = std::clamp(real(2), 0.0, 100.0) / 100.0;
        Rgba c;
        if (s <= 0) {
            c.r = c.g = c.b = l * 255.0;
        } else {
            const double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
            c.r = hueToRgb(p, q, h + 1.0 / 3) * 255.0;
            c.g = hueToRgb(p, q, h) * 255.0;
            c.b = hueToRgb(p, q, h - 1.0 / 3) * 255.0;
        }
        return text(c, false);
    }
    if (u == "COULEUR_MELANGER") {
        Rgba x, y;
        if (!color(0, x) || !color(1, y)) return false;
        return text(mix(x, y, real(2)), false);
    }
    if (u == "COULEUR_DEGRADE") {
        Rgba x, y;
        if (!color(3, x) || !color(4, y)) return false;
        const double lo = real(1), hi = real(2);
        const double t = hi == lo ? 0.0 : (real(0) - lo) / (hi - lo);
        return text(mix(x, y, t), false);
    }
    if (u == "COULEUR_ECLAIRCIR" || u == "COULEUR_ASSOMBRIR") {
        Rgba x;
        if (!color(0, x)) return false;
        const double v = u == "COULEUR_ECLAIRCIR" ? 255.0 : 0.0;
        Rgba y = mix(x, {v, v, v, x.a}, real(1) / 100.0);
        y.a = x.a;
        return text(y, false);
    }
    if (u == "COULEUR_OPACITE") {
        Rgba x;
        if (!color(0, x)) return false;
        x.a = std::clamp(real(1), 0.0, 100.0) / 100.0 * 255.0;
        return text(x, true);
    }
    if (u == "COULEUR_CONTRASTE") {
        Rgba x;
        if (!color(0, x)) return false;
        const double lum = 0.299 * x.r + 0.587 * x.g + 0.114 * x.b;
        out = sim::Value::text(lum >= 128.0 ? "#000000" : "#FFFFFF");
        return true;
    }
    if (u == "COULEUR_ROUGE" || u == "COULEUR_VERT" || u == "COULEUR_BLEU" || u == "COULEUR_ALPHA") {
        Rgba x;
        if (!color(0, x)) return false;
        const double v = u == "COULEUR_ROUGE" ? x.r : u == "COULEUR_VERT" ? x.g : u == "COULEUR_BLEU" ? x.b : x.a;
        out = sim::Value::integer(sim::Type::Int, channel(v));
        return true;
    }
    if (u == "RANDOM") {
        out = sim::Value::real(static_cast<double>(nextRandom()) / 4294967296.0);
        return true;
    }
    if (u == "RANDOM_INT") {
        auto lo = args[0].asInteger(), hi = args[1].asInteger();
        if (lo > hi) std::swap(lo, hi);
        const auto span = static_cast<std::uint64_t>(hi - lo) + 1u;
        out = sim::Value::integer(sim::Type::DInt, lo + static_cast<std::int64_t>(nextRandom() % span));
        return true;
    }
    if (u == "RANDOM_REAL") {
        const double lo = real(0), hi = real(1);
        out = sim::Value::real(lo + (hi - lo) * (static_cast<double>(nextRandom()) / 4294967296.0));
        return true;
    }
    if (u == "RANDOM_SEED") {
        seedRandom(static_cast<std::uint32_t>(args[0].asInteger()));
        out = sim::Value::boolean(true);
        return true;
    }
    // 1.12.1 : les dates et heures.
    constexpr std::int64_t kDay = 86400000;
    const auto want = [&](int i, sim::Type t) {
        if (args[static_cast<std::size_t>(i)].type() == t) return true;
        if (why) *why = u + " : " + std::string(sim::toString(t)) + " attendu, pas " + args[static_cast<std::size_t>(i)].display();
        return false;
    };
    if (u == "MAINTENANT") {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        const std::int64_t days = sim::daysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1), static_cast<unsigned>(tm.tm_mday));
        out = sim::Value::integer(sim::Type::Dt, days * kDay + (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec) * 1000LL + ms);
        return true;
    }
    if (u == "DT_TO_DATE" || u == "DT_TO_TOD") {
        if (!want(0, sim::Type::Dt)) return false;
        const auto v = args[0].asInteger();
        const auto day = (v >= 0 ? v / kDay : (v - kDay + 1) / kDay) * kDay;
        out = u == "DT_TO_DATE" ? sim::Value::integer(sim::Type::Date, day) : sim::Value::integer(sim::Type::Tod, v - day);
        return true;
    }
    if (u == "CONCAT_DATE_TOD") {
        if (!want(0, sim::Type::Date) || !want(1, sim::Type::Tod)) return false;
        out = sim::Value::integer(sim::Type::Dt, args[0].asInteger() + args[1].asInteger());
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------- les types ---
std::vector<TypeCard> typeCards() {
    std::vector<TypeCard> out;
    const auto& reg = typereg::baseRegistry();
    for (const auto& e : reg.entries()) {
        if (e.category != typereg::Category::Elementary && e.category != typereg::Category::TextTime) continue;
        TypeCard t;
        t.name = e.name;
        t.category = std::string(typereg::categoryLabel(e.category));
        t.summary = e.doc;
        t.bits = e.numeric.bits;
        const auto& n = e.numeric;
        if (e.name == "BOOL") {
            t.minText = "FALSE";
            t.maxText = "TRUE";
        } else if (n.family == typereg::Family::Integer) {
            if (n.bitString) {
                const int digits = n.bits / 4;
                t.minText = "16#" + std::string(static_cast<std::size_t>(digits), '0') + "  (0)";
                t.maxText = "16#" + std::string(static_cast<std::size_t>(digits), 'F') + "  (" + grouped(n.high) + ")";
            } else {
                t.minText = grouped(n.low);
                t.maxText = grouped(n.high);
            }
        } else if (e.name == "REAL") {
            t.minText = "-3.402823E+38";
            t.maxText = "3.402823E+38";
        } else if (e.name == "LREAL") {
            t.minText = "-1.7976931348623157E+308";
            t.maxText = "1.7976931348623157E+308";
        } else if (e.name == "TIME") {
            t.minText = "T#0ms";
            t.maxText = "T#4294967295ms  (49 j 17 h 2 min 47 s 295 ms)";
        } else if (e.name == "STRING") {
            t.minText = "'' (vide)";
            t.maxText = "STRING[n] : n caract\xC3\xA8res";
        } else if (e.name == "CHAR") {                             // 1.12.1
            t.minText = "'' (aucun)";
            t.maxText = "un caract\xC3\xA8re";
        } else if (e.name == "WSTRING") {
            t.minText = "'' (vide)";
            t.maxText = "WSTRING[n] : n caract\xC3\xA8res";
        } else if (e.name == "DATE") {
            t.minText = "D#1970-01-01";
            t.maxText = "D#9999-12-31";
        } else if (e.name == "TIME_OF_DAY") {
            t.minText = "TOD#00:00:00";
            t.maxText = "TOD#23:59:59.999";
        } else if (e.name == "DATE_AND_TIME") {
            t.minText = "DT#1970-01-01-00:00:00";
            t.maxText = "DT#9999-12-31-23:59:59";
        }
        if (e.numeric.bits > 0) {
            t.size = std::to_string(e.numeric.bits) + " bit" + (e.numeric.bits > 1 ? "s" : "");
            if (e.numeric.bits >= 8) {
                const int bytes = e.numeric.bits / 8;
                t.size += " \xC2\xB7 " + std::to_string(bytes) + " octet" + (bytes > 1 ? "s" : "");
            }
        } else if (e.name == "STRING") {
            t.size = "selon la borne : n + 1 octets";
        }
        if (const auto* x = typeExtra(e.name)) {
            t.modbus = std::string(x->modbus);
            t.cType = std::string(x->cType);
            t.cppType = std::string(x->cppType);
            t.defaultValue = std::string(x->defaultValue);
            for (const auto l : x->literals) t.literals.emplace_back(l);
            for (const auto l : x->notes) t.notes.emplace_back(l);
        }
        static const std::pair<unsigned, const char*> kUses[] = {
            {typereg::UseVariable, "variable IHM"}, {typereg::UseDeclaration, "d\xC3\xA9" "claration (locale, constante)"},
            {typereg::UseParameter, "param\xC3\xA8tre de popup ou de symbole"}, {typereg::UseReturn, "retour de fonction"},
            {typereg::UseOperand, "op\xC3\xA9rande d'op\xC3\xA9rateur"}};
        for (const auto& [bit, label] : kUses)
            if (e.uses & bit) t.uses.emplace_back(label);
        out.push_back(std::move(t));
    }
    return out;
}

std::optional<TypeCard> typeCard(std::string_view name) {
    for (auto& t : typeCards())
        if (iequals(t.name, name)) return t;
    return std::nullopt;
}

const Tree& tree() {
    static const Tree t = [] {
        Tree out;
        for (const auto& c : categories()) {
            std::vector<std::size_t> list;
            for (std::size_t i = 0; i < functions().size(); ++i)
                if (functions()[i].category == c.id) list.push_back(i);
            if (list.empty()) continue;                    // op, instr : les operateurs et les instructions
            out.categories.push_back(&c);
            out.functionsOf.push_back(std::move(list));
        }
        out.types = typeCards();
        out.conversionCount = conversions().size();
        out.functionCount = functions().size() + out.conversionCount;
        return out;
    }();
    return t;
}

std::string conversionName(std::size_t from, std::size_t to) {
    const auto& types = conversionTypes();
    if (from >= types.size() || to >= types.size() || from == to) return {};
    return std::string(types[from]) + "_TO_" + std::string(types[to]);
}

} // namespace hmi::natives
