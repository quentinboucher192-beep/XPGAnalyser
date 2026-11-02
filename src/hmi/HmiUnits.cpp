// =============================================================================
//  hmi/HmiUnits.cpp - 1.13.0 (lot J6) : la bibliotheque des unites (voir l'en-tete)
// =============================================================================
#include "HmiUnits.hpp"
#include "HmiTypeRegistry.hpp"   // typereg::isNumber
#include "HmiTypes.hpp"          // types::membersOf (les membres herites)
#include "../project/MemberTree.hpp"   // project::members::parseArray (l'element d'un ARRAY)
#include "HmiDisplay.hpp"        // 1.14.1 (lot N5) : hmi::unitLabel (degC se lit avec le signe degre dans une formule)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>   // 1.22.8 : exactNumber (le plus court qui se relit)
#include <cstdio>
#include <cstdlib>

namespace hmi::units {

namespace {

// Minuscules, sans accents (UTF-8), sans espaces du bord : pour comparer un symbole tape a un symbole
// fourni (la recherche du selecteur est « sans casse/accents »). degC, DEGC, « deg c » se rejoignent.
std::string fold(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC2 && i + 1 < s.size()) {           // \xC2\xB0 (degre), \xC2\xB3 (exposant 3)...
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0xB0 || d == 0xBA) { i += 2; continue; }   // le degre (et l'ordinal tape a sa place) : ignore (degc == °c)
            if (d == 0xA0) { i += 2; continue; }                // l'espace insecable : un espace
            if (d == 0xB2) { out += '2'; i += 2; continue; }
            if (d == 0xB3) { out += '3'; i += 2; continue; }
            // Le signe micro (la touche du clavier) : un u, comme on l'ecrit sans lui (um, us, uA). Avant, il
            // etait jete : um devenait m, le metre, et une conversion se trompait d'un facteur un million.
            if (d == 0xB5) { out += 'u'; i += 2; continue; }
            // Tout autre caractere : garde tel quel - un symbole inconnu reste inconnu (Generer le dit) au lieu
            // de se confondre avec une unite fournie.
            out += s[i];
            out += s[i + 1];
            i += 2;
            continue;
        }
        if (c == 0xCE && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBC) {   // le mu grec : un u aussi
            out += 'u';
            i += 2;
            continue;
        }
        if (c == 0xC3 && i + 1 < s.size()) {            // les lettres accentuees Latin-1
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            // e accentue, a accentue... : la lettre de base, en minuscule.
            static const char* const base = "aaaaaaaceeeeiiiidnooooo.ouuuuy.y";   // 0x80..0x9F (approx.)
            if (d >= 0xA0 && d <= 0xBF) out += base[(d - 0xA0) & 0x1F];
            else if (d >= 0x80 && d <= 0x9F) out += base[(d - 0x80) & 0x1F];
            i += 2;
            continue;
        }
        if (!std::isspace(c)) out += static_cast<char>(std::tolower(c));
        ++i;
    }
    return out;
}

// Les alias : plusieurs ecritures menent au meme symbole fourni (replie).
struct Alias { const char* written; const char* symbol; };
const Alias kAliases[] = {
    {"degc", "degC"}, {"celsius", "degC"}, {"c", "degC"},
    {"degf", "degF"}, {"fahrenheit", "degF"},
    {"kelvin", "K"},
    {"m3/h", "m3/h"}, {"m3h", "m3/h"},
    {"m3/s", "m3/s"},
    {"nm3/h", "Nm3/h"}, {"nm3h", "Nm3/h"},
    {"l/s", "L/s"}, {"l/min", "L/min"}, {"l/h", "L/h"},
    {"pourcent", "%"}, {"pct", "%"},
};

} // namespace

const std::vector<UnitFamily>& builtinFamilies() {
    // name (ASCII), reference, label (visible, accents en \xHH)
    static const std::vector<UnitFamily> k = {
        {"pression", "Pa", "Pression"},
        {"temperature", "K", "Temp\xC3\xA9rature"},
        {"debit", "m3/h", "D\xC3\xA9" "bit"},
        {"debit_normalise", "Nm3/h", "D\xC3\xA9" "bit normalis\xC3\xA9"},
        {"temps", "s", "Temps"},
        {"longueur", "m", "Longueur"},
        {"masse", "kg", "Masse"},
        {"tension", "V", "Tension"},
        {"courant", "A", "Courant"},
        {"frequence", "Hz", "Fr\xC3\xA9quence"},
        {"pourcentage", "%", "Pourcentage"},
    };
    return k;
}

const std::vector<UnitDef>& builtinUnits() {
    // symbol, family, factor, offset (valeur_reference = valeur * factor + offset), label
    static const std::vector<UnitDef> k = {
        // pression (reference Pa)
        {"Pa", "pression", 1.0, 0.0, "pascal"},
        {"hPa", "pression", 100.0, 0.0, "hectopascal"},
        {"kPa", "pression", 1000.0, 0.0, "kilopascal"},
        {"mbar", "pression", 100.0, 0.0, "millibar"},
        {"bar", "pression", 100000.0, 0.0, "bar"},
        {"psi", "pression", 6894.757293, 0.0, "livre par pouce carr\xC3\xA9"},
        // temperature (reference K) - affine (facteur ET decalage)
        {"K", "temperature", 1.0, 0.0, "kelvin"},
        {"degC", "temperature", 1.0, 273.15, "degr\xC3\xA9 Celsius"},
        {"degF", "temperature", 5.0 / 9.0, 459.67 * 5.0 / 9.0, "degr\xC3\xA9 Fahrenheit"},
        // debit (reference m3/h)
        {"m3/h", "debit", 1.0, 0.0, "m\xC3\xA8tre cube par heure"},
        {"m3/s", "debit", 3600.0, 0.0, "m\xC3\xA8tre cube par seconde"},
        {"L/s", "debit", 3.6, 0.0, "litre par seconde"},
        {"L/min", "debit", 0.06, 0.0, "litre par minute"},
        {"L/h", "debit", 0.001, 0.0, "litre par heure"},
        // debit normalise (reference Nm3/h) - famille a part, non convertible vers m3/h
        {"Nm3/h", "debit_normalise", 1.0, 0.0, "m\xC3\xA8tre cube normal par heure"},
        // temps (reference s)
        {"s", "temps", 1.0, 0.0, "seconde"},
        {"ms", "temps", 0.001, 0.0, "milliseconde"},
        {"min", "temps", 60.0, 0.0, "minute"},
        {"h", "temps", 3600.0, 0.0, "heure"},
        {"j", "temps", 86400.0, 0.0, "jour"},
        // longueur (reference m)
        {"m", "longueur", 1.0, 0.0, "m\xC3\xA8tre"},
        {"mm", "longueur", 0.001, 0.0, "millim\xC3\xA8tre"},
        {"cm", "longueur", 0.01, 0.0, "centim\xC3\xA8tre"},
        {"km", "longueur", 1000.0, 0.0, "kilom\xC3\xA8tre"},
        // masse (reference kg)
        {"kg", "masse", 1.0, 0.0, "kilogramme"},
        {"g", "masse", 0.001, 0.0, "gramme"},
        {"t", "masse", 1000.0, 0.0, "tonne"},
        // tension (reference V)
        {"V", "tension", 1.0, 0.0, "volt"},
        {"mV", "tension", 0.001, 0.0, "millivolt"},
        {"kV", "tension", 1000.0, 0.0, "kilovolt"},
        // courant (reference A)
        {"A", "courant", 1.0, 0.0, "amp\xC3\xA8re"},
        {"mA", "courant", 0.001, 0.0, "milliamp\xC3\xA8re"},
        // frequence (reference Hz)
        {"Hz", "frequence", 1.0, 0.0, "hertz"},
        {"kHz", "frequence", 1000.0, 0.0, "kilohertz"},
        // pourcentage (reference %) - sans dimension
        {"%", "pourcentage", 1.0, 0.0, "pour cent"},
        {"ratio", "pourcentage", 100.0, 0.0, "rapport (0..1)"},
    };
    return k;
}

void Catalog::add(const UnitFamily& f) {
    for (auto& x : families_)
        if (fold(x.name) == fold(f.name)) { x = f; return; }   // le projet remplace une fournie de meme nom
    families_.push_back(f);
}
void Catalog::add(const UnitDef& u) {
    for (auto& x : units_)
        if (fold(x.symbol) == fold(u.symbol)) { x = u; return; }
    units_.push_back(u);
}

Catalog::Catalog() {
    for (const auto& f : builtinFamilies()) families_.push_back(f);
    for (const auto& u : builtinUnits()) units_.push_back(u);
}

Catalog::Catalog(const Config& cfg) : Catalog() {
    for (const auto& f : cfg.unitFamilies) add(f);   // le projet ajoute, ou remplace par meme nom
    for (const auto& u : cfg.units) add(u);
}

const UnitDef* Catalog::unit(std::string_view symbol) const {
    const std::string s = fold(symbol);
    if (s.empty()) return nullptr;
    for (const auto& u : units_)
        if (fold(u.symbol) == s) return &u;
    for (const auto& a : kAliases)
        if (s == a.written) {
            const std::string want = fold(a.symbol);
            for (const auto& u : units_)
                if (fold(u.symbol) == want) return &u;
        }
    return nullptr;
}

const UnitFamily* Catalog::family(std::string_view name) const {
    const std::string s = fold(name);
    for (const auto& f : families_)
        if (fold(f.name) == s) return &f;
    return nullptr;
}

std::string Catalog::familyOf(std::string_view symbol) const {
    const UnitDef* u = unit(symbol);
    return u ? u->family : std::string{};
}

bool Catalog::known(std::string_view symbol) const noexcept {
    std::string_view s = symbol;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty()) return true;   // vide : pas d'unite, ce n'est pas une erreur
    return unit(s) != nullptr;
}

std::string Catalog::canonical(std::string_view symbol) const {
    const UnitDef* u = unit(symbol);
    return u ? u->symbol : std::string{};
}

std::string Catalog::familyLabel(std::string_view name) const {
    const UnitFamily* f = family(name);
    if (f) return f->label.empty() ? f->name : f->label;
    return std::string(name);
}

std::string Catalog::familyPhrase(std::string_view symbol) const {
    const UnitDef* u = unit(symbol);
    if (!u) return "une grandeur";
    // Les familles fournies au masculin ; les autres fournies sont au feminin. Une famille du projet : on
    // ne connait pas son genre - « une grandeur Angle ».
    static const char* const kMasculine[] = {"debit", "debit_normalise", "temps", "courant", "pourcentage"};
    static const char* const kFeminine[] = {"pression", "temperature", "longueur", "masse", "tension", "frequence"};
    std::string label = familyLabel(u->family);
    std::string low = label;
    if (!low.empty()) low[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(low[0])));
    const std::string f = fold(u->family);
    for (const char* m : kMasculine)
        if (f == m) return "un " + low;
    for (const char* m : kFeminine)
        if (f == m) return "une " + low;
    return label.empty() ? std::string("une grandeur") : "une grandeur " + label;
}

std::string Catalog::unitLabel(std::string_view symbol) const {
    const UnitDef* u = unit(symbol);
    if (u) return u->label.empty() ? u->symbol : u->label;
    return std::string(symbol);
}

std::optional<Affine> Catalog::convert(std::string_view from, std::string_view to) const {
    const UnitDef* a = unit(from);
    const UnitDef* b = unit(to);
    if (!a || !b) return std::nullopt;
    if (a->family != b->family) return std::nullopt;
    // valeur_reference = v * a.factor + a.offset ; v_cible = (reference - b.offset) / b.factor.
    if (b->factor == 0.0) return std::nullopt;
    Affine aff;
    aff.factor = a->factor / b->factor;
    aff.offset = (a->offset - b->offset) / b->factor;
    return aff;
}

Rel Catalog::relation(std::string_view a, std::string_view b) const {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    };
    const std::string_view x = trim(a), y = trim(b);
    if (x.empty() || y.empty()) return Rel::None;
    const UnitDef* ua = unit(x);
    const UnitDef* ub = unit(y);
    if (!ua || !ub) return Rel::Unknown;
    if (fold(ua->symbol) == fold(ub->symbol)) return Rel::Same;
    return ua->family == ub->family ? Rel::SameFamily : Rel::OtherFamily;
}

// --------------------------------------------------------------------------------------------
bool typeTakesUnit(std::string_view type) {
    std::string t(type);
    // ARRAY[...] OF <element> : l'unite porte sur l'element.
    std::string up;
    up.reserve(t.size());
    for (char c : t) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (const auto of = up.rfind(" OF "); up.rfind("ARRAY", 0) == 0 && of != std::string::npos)
        t = t.substr(of + 4);
    // trim
    std::string_view v = t;
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.remove_suffix(1);
    return typereg::isNumber(v);
}

std::string withUnit(std::string_view type, std::string_view unit) {
    std::string out(type);
    std::string_view u = unit;
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.front()))) u.remove_prefix(1);
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back()))) u.remove_suffix(1);
    if (!u.empty()) { out += " ["; out += u; out += "]"; }
    return out;
}

namespace {
std::string stripArray(std::string_view type) {
    const auto shape = project::members::parseArray(std::string(type));
    return shape.valid() ? shape.element : std::string(type);
}
} // namespace

namespace {
// Suit `p` a partir de i (des .membre et des [indice]) depuis le type et l'unite d'un depart.
std::string follow(const Project& project, std::string type, std::string unit, std::string_view p, std::size_t i) {
    const auto word = [&] {
        const std::size_t s = i;
        while (i < p.size() && (std::isalnum(static_cast<unsigned char>(p[i])) || p[i] == '_')) ++i;
        return std::string(p.substr(s, i - s));
    };
    while (i < p.size()) {
        if (p[i] == '[') {                                  // un indice : l'element garde l'unite du membre
            const auto close = p.find(']', i);
            if (close == std::string_view::npos) break;
            type = stripArray(type);
            i = close + 1;
            continue;
        }
        if (p[i] == '.') {
            ++i;
            const std::string member = word();
            if (member.empty()) break;
            const std::string element = stripArray(type);
            const HmiType* ht = project.hmiTypeByName(element);
            if (!ht || ht->kind != HmiTypeKind::Structure) return {};
            // Les membres herites aussi (EXTENDS : la tranche du parent d'abord), comme typeOfPath.
            const auto members = types::membersOf(project, element);
            const TypeMember* found = nullptr;
            std::string b;
            for (char c : member) b += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto& m : members) {
                std::string a;
                a.reserve(m.name.size());
                for (char c : m.name) a += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (a == b) { found = &m; break; }
            }
            if (!found) return {};
            type = found->type;
            unit = found->unit;
            continue;
        }
        break;
    }
    return unit;
}
} // namespace

std::string unitOfPath(const Project& project, std::string_view path) {
    // Decoupe le chemin : un identifiant, puis des .membre et des [indice].
    std::string_view p = path;
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.front()))) p.remove_prefix(1);
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.remove_suffix(1);
    if (p.empty()) return {};
    std::size_t i = 0;
    while (i < p.size() && (std::isalnum(static_cast<unsigned char>(p[i])) || p[i] == '_')) ++i;
    const std::string root(p.substr(0, i));
    if (root.empty()) return {};
    const Variable* var = project.variable(root);
    if (!var) return {};
    return follow(project, var->type, var->unit, p, i);
}

std::string unitAlong(const Project& project, std::string_view type, std::string_view unit, std::string_view rest) {
    std::string_view p = rest;
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.front()))) p.remove_prefix(1);
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.remove_suffix(1);
    std::string u(unit);
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.front()))) u.erase(0, 1);
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back()))) u.pop_back();
    return follow(project, std::string(type), u, p, 0);
}

// =============================================================================
//  1.14.1 (lot N5) : CONVERTIR UNE VALEUR (l'onglet Convertir, l'objet Convertisseur d'unites)
// =============================================================================
namespace {

// Un nombre a lire dans une formule : 6 chiffres significatifs au plus, sans exposant ni zeros inutiles (comme la
// colonne Conversion de Configuration > Unites de mesure).
std::string shortNumber(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v);
    const double r = std::strtod(b, nullptr);
    const double a = std::fabs(r);
    if (a == 0.0) return "0";
    if (a < 1e-9 || a >= 1e12) return b;
    const int decimals = std::max(0, 5 - static_cast<int>(std::floor(std::log10(a))));
    std::snprintf(b, sizeof b, "%.*f", decimals, r);
    std::string s(b);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

// 1.14.1 (lot N5, retouche) : un nombre MONTRE a la francaise, la regle de la Calculatrice et de l'outil Modbus
// (mbtool::frenchNumber) : la virgule decimale ("21,755656", "x \xC3\x97 1,8 + 32") ; l'exposant d'un tres petit ou tres
// grand nombre en E ("1,15741E-8", comme la Calculatrice). Pas de separateur de milliers : "1500 mbar" se relit et se
// retape tel quel dans la case Valeur. `s` : le meme nombre ecrit avec un point (printf).
std::string frenchShown(const std::string& s) {
    const auto e = s.find_first_of("eE");
    std::string mantissa = s.substr(0, e);
    for (auto& ch : mantissa)
        if (ch == '.') ch = ',';
    if (e == std::string::npos) return mantissa;
    const int exponent = std::atoi(s.c_str() + e + 1);
    return mantissa + "E" + (exponent < 0 ? "-" : "+") + std::to_string(std::abs(exponent));
}

std::string trimmedText(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

} // namespace

Conversion convertValue(const Catalog& cat, double value, std::string_view from, std::string_view to) {
    Conversion c;
    c.value = value;
    const std::string a = trimmedText(from), b = trimmedText(to);
    const UnitDef* ua = cat.unit(a);
    const UnitDef* ub = cat.unit(b);
    if (ua) c.from = ua->symbol;
    if (ub) c.to = ub->symbol;
    if (a.empty() || b.empty()) {
        c.error = a.empty() ? "choisis l'unit\xC3\xA9 de d\xC3\xA9part" : "choisis l'unit\xC3\xA9 d'arriv\xC3\xA9" "e";
        return c;
    }
    if (!ua || !ub) {
        const std::string& bad = !ua ? a : b;
        c.error = "unit\xC3\xA9 inconnue \xC2\xAB " + bad + " \xC2\xBB (ni fournie, ni du projet)";
        return c;
    }
    if (ua->family != ub->family) {
        // Les unites citees comme les listes les montrent (degC : avec le signe degre).
        const std::string la = unitLabel(ua->symbol), lb = unitLabel(ub->symbol);
        c.error = "dimensions diff\xC3\xA9rentes : " + la + " est " + cat.familyPhrase(ua->symbol) + ", " + lb + " "
                + cat.familyPhrase(ub->symbol) + " \xE2\x80\x94 on ne convertit pas des " + la + " en " + lb;
        return c;
    }
    const auto aff = cat.convert(ua->symbol, ub->symbol);
    if (!aff) {
        c.error = "conversion impossible de " + unitLabel(ua->symbol) + " en " + unitLabel(ub->symbol) + " (facteur nul)";
        return c;
    }
    if (!std::isfinite(value)) {
        c.error = "valeur illisible";
        return c;
    }
    c.result = aff->apply(value);
    c.ok = std::isfinite(c.result);
    if (!c.ok) c.error = "r\xC3\xA9sultat hors des nombres (trop grand)";
    c.formula = formulaText(cat, ua->symbol, ub->symbol);
    return c;
}

std::string formulaText(const Catalog& cat, std::string_view from, std::string_view to) {
    const UnitDef* ua = cat.unit(from);
    const UnitDef* ub = cat.unit(to);
    if (!ua || !ub || ua->family != ub->family) return {};
    const auto aff = cat.convert(ua->symbol, ub->symbol);
    if (!aff) return {};
    const std::string a = unitLabel(ua->symbol), b = unitLabel(ub->symbol);
    if (aff->offset == 0.0) return "1 " + a + " = " + frenchShown(shortNumber(aff->factor)) + " " + b;
    std::string f = "x";
    if (shortNumber(aff->factor) != "1") f += " \xC3\x97 " + frenchShown(shortNumber(aff->factor));
    f += (aff->offset >= 0 ? " + " : " - ") + frenchShown(shortNumber(std::fabs(aff->offset)));
    return a + " -> " + b + " : " + f;
}

std::string enExpression(std::string_view value, std::string_view to) {
    std::string v = trimmedText(value);
    if (v.empty()) v = "Valeur";
    return "EN(" + v + ", '" + trimmedText(to) + "')";
}

std::string formatResult(double v, int decimals) {
    return frenchShown(resultNumber(v, decimals));
}

std::string resultNumber(double v, int decimals) {
    if (!std::isfinite(v)) return "?";
    decimals = std::clamp(decimals, 0, 12);
    if (v == 0.0) return "0";
    const double a = std::fabs(v);
    char b[96];
    // Tres petit (sous la derniere decimale montree) ou tres grand : les chiffres significatifs, pas un 0 trompeur.
    if (a >= 1e15 || a < std::pow(10.0, -decimals) * 0.5) {
        std::snprintf(b, sizeof b, "%.*g", std::max(decimals, 4), v);
        return b;
    }
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string s(b);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

bool parseValue(std::string_view text, double& out) {
    std::string t = trimmedText(text);
    for (auto& c : t)
        if (c == ',') c = '.';
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

std::string familyFrom(const Catalog& cat, std::string_view text) {
    const std::string t = fold(text);
    if (t.empty() || t == "toutes" || t == "tous" || t == "*") return {};
    for (const auto& f : cat.families())
        if (fold(f.name) == t || fold(f.label) == t) return f.name;
    return {};
}

std::vector<const UnitDef*> unitsOf(const Catalog& cat, std::string_view family) {
    const std::string fam = familyFrom(cat, family);
    std::vector<const UnitDef*> out;
    for (const auto& u : cat.units())
        if (fam.empty() || u.family == fam) out.push_back(&u);
    return out;
}

ConverterLayout converterLayout(double w, double h, double fontSize, bool titled, bool writes, int open, std::size_t count,
                                std::size_t first) {
    ConverterLayout l;
    const double fs = std::clamp(fontSize, 7.0, 40.0);
    const double pad = std::max(6.0, fs * 0.5), gap = std::max(4.0, fs * 0.35);
    const double rh = std::max(32.0, fs * 2.2);       // des cibles de 32 pixels au moins (le doigt, Generer)
    double y = pad;
    if (titled) {
        l.title = {pad, 2, std::max(0.0, w - 2 * pad), fs * 1.6};
        y = l.title.y + l.title.h + gap * 0.5;
    }
    // La ligne de saisie : la valeur (40 %), De (30 %), le bouton inverser, Vers (30 %).
    const double avail = std::max(0.0, w - 2 * pad - 3 * gap - rh);
    l.value = {pad, y, avail * 0.4, rh};
    l.from = {l.value.x + l.value.w + gap, y, avail * 0.3, rh};
    l.swap = {l.from.x + l.from.w + gap, y, rh, rh};
    l.to = {l.swap.x + l.swap.w + gap, y, std::max(0.0, w - pad - (l.swap.x + l.swap.w + gap)), rh};
    y += rh + gap;
    const double resultH = std::max(rh, fs * 2.2);
    l.result = {pad, y, std::max(0.0, w - 2 * pad), resultH};
    y += resultH + gap * 0.5;
    const double writeW = writes ? std::max(70.0, fs * 6.0) : 0.0;
    const double lineH = std::max(fs * 1.5, writes ? rh : 0.0);
    l.formula = {pad, y, std::max(0.0, w - 2 * pad - (writes ? writeW + gap : 0.0)), lineH};
    if (writes) l.write = {w - pad - writeW, y, writeW, rh};
    // La liste ouverte : sous sa case, dans l'objet ; des fleches quand tout ne tient pas.
    if ((open == 1 || open == 2) && count > 0) {
        const ConverterBox& box = open == 1 ? l.from : l.to;
        l.rowHeight = std::max(26.0, fs * 1.9);
        const double top = box.y + box.h;
        const double room = std::max(0.0, h - top - 2.0);
        auto slots = static_cast<std::size_t>(std::max(1.0, std::floor(room / l.rowHeight)));
        const double listW = std::min(std::max(box.w, fs * 11.0), w - 4.0);
        const double listX = std::clamp(box.x, 2.0, std::max(2.0, w - 2.0 - listW));
        bool up = false, down = false;
        std::size_t shown = count;
        if (count <= slots) {
            first = 0;
        } else {
            // La derniere page : une fleche en haut, plus rien en bas ; avant elle, une fleche en bas aussi.
            const std::size_t lastFirst = count - std::max<std::size_t>(1, slots - 1);
            first = std::min(first, lastFirst);
            up = first > 0;
            const std::size_t room2 = slots - (up ? 1 : 0);
            if (first + room2 >= count) {
                shown = count - first;
            } else {
                down = true;
                shown = std::max<std::size_t>(1, room2 - 1);
            }
        }
        l.first = first;
        const double listH = static_cast<double>(shown + (up ? 1 : 0) + (down ? 1 : 0)) * l.rowHeight;
        l.list = {listX, top, listW, listH};
        double ry = top;
        if (up) {
            l.up = {listX, ry, listW, l.rowHeight};
            ry += l.rowHeight;
        }
        for (std::size_t i = 0; i < shown && first + i < count; ++i) {
            l.rows.push_back({listX, ry, listW, l.rowHeight});
            l.rowIndex.push_back(first + i);
            ry += l.rowHeight;
        }
        if (down) l.down = {listX, ry, listW, l.rowHeight};
    }
    return l;
}

std::string converterHit(const ConverterLayout& l, double x, double y) {
    // La liste ouverte d'abord : elle est par-dessus le reste.
    if (l.list.contains(x, y)) {
        if (l.up.contains(x, y)) return "liste:haut";
        if (l.down.contains(x, y)) return "liste:bas";
        for (std::size_t i = 0; i < l.rows.size(); ++i)
            if (l.rows[i].contains(x, y)) return "choix:" + std::to_string(l.rowIndex[i]);
        return {};
    }
    if (l.value.contains(x, y)) return "valeur";
    if (l.from.contains(x, y)) return "de";
    if (l.to.contains(x, y)) return "vers";
    if (l.swap.contains(x, y)) return "inverser";
    if (l.write.contains(x, y)) return "ecrire";
    return {};
}

// =============================================================================
//  1.22.8 : CREER UNE UNITE - les controles partages (l'editeur, l'Assistant IA) et la table des unites courantes
// =============================================================================
bool symbolOk(std::string_view s) {
    if (s.empty() || s.size() > 16) return false;
    for (const char c : s)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '[' || c == ']' || c == '\'' || c == '"' || c == ';') return false;
    return true;
}

bool familyNameOk(std::string_view n) {
    if (n.empty() || std::isdigit(static_cast<unsigned char>(n.front()))) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool builtinUnit(std::string_view symbol) {
    const std::string s = fold(symbol);
    return std::any_of(builtinUnits().begin(), builtinUnits().end(), [&](const UnitDef& u) { return fold(u.symbol) == s; });
}

bool builtinFamily(std::string_view name) {
    const std::string s = fold(name);
    return std::any_of(builtinFamilies().begin(), builtinFamilies().end(), [&](const UnitFamily& f) { return fold(f.name) == s; });
}

std::string newUnitProblem(const Catalog& cat, const UnitDef& u, std::string* family) {
    const std::string sym = trimmedText(u.symbol);
    if (!symbolOk(sym)) return "son symbole : court, sans espace ni crochet (bar, m3/h, %)";
    if (const UnitDef* other = cat.unit(sym))
        return "\xC2\xAB " + sym + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 (" + cat.familyLabel(other->family)
               + (builtinUnit(other->symbol) ? ", unit\xC3\xA9 fournie" : ", unit\xC3\xA9 du projet")
               + (fold(other->symbol) != fold(sym) ? " " + other->symbol : std::string{}) + ")";
    const UnitFamily* f = cat.family(trimmedText(u.family));
    if (!f)
        if (const std::string n = familyFrom(cat, u.family); !n.empty()) f = cat.family(n);
    if (!f) return "famille inconnue \xC2\xAB " + trimmedText(u.family) + " \xC2\xBB";
    if (family) *family = f->name;
    if (!std::isfinite(u.factor) || u.factor == 0.0) return "le facteur doit \xC3\xAAtre un nombre non nul";
    if (!std::isfinite(u.offset)) return "le d\xC3\xA9" "calage doit \xC3\xAAtre un nombre";
    return {};
}

std::string newFamilyProblem(const Catalog& cat, std::string_view rawName, std::string_view rawRef) {
    const std::string name = trimmedText(rawName), ref = trimmedText(rawRef);
    if (!familyNameOk(name)) return "son nom : des minuscules, des chiffres et _ (pas de chiffre en t\xC3\xAAte)";
    if (cat.family(name)) return "la famille \xC2\xAB " + name + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0";
    if (!symbolOk(ref)) return "sa r\xC3\xA9" "f\xC3\xA9rence : un symbole court, sans espace (bar, m3/h)";
    if (const UnitDef* u = cat.unit(ref))
        return "\xC2\xAB " + ref + " \xC2\xBB est d\xC3\xA9j\xC3\xA0 une unit\xC3\xA9 (" + cat.familyLabel(u->family)
               + ") : la r\xC3\xA9" "f\xC3\xA9rence d'une famille nouvelle est une unit\xC3\xA9 nouvelle";
    return {};
}

std::string unitChangeProblem(const Config& cfg, std::string_view symbol, double factor, double offset) {
    if (builtinUnit(symbol)) return "une unit\xC3\xA9 fournie est verrouill\xC3\xA9" "e (ajoute plut\xC3\xB4t une unit\xC3\xA9 du projet)";
    const std::string s = fold(symbol);
    const auto it = std::find_if(cfg.units.begin(), cfg.units.end(), [&](const UnitDef& u) { return fold(u.symbol) == s; });
    if (it == cfg.units.end()) return "unit\xC3\xA9 introuvable";
    if (!std::isfinite(factor) || factor == 0.0) return "le facteur doit \xC3\xAAtre un nombre non nul";
    if (!std::isfinite(offset)) return "le d\xC3\xA9" "calage doit \xC3\xAAtre un nombre";
    const Catalog cat(cfg);
    if (const UnitFamily* f = cat.family(it->family); f && fold(f->reference) == s && (factor != 1.0 || offset != 0.0))
        return it->symbol + " est la r\xC3\xA9" "f\xC3\xA9rence de sa famille : facteur 1, d\xC3\xA9" "calage 0";
    return {};
}

std::string exactNumber(double v) {
    if (!std::isfinite(v)) return "0";
    char b[64];
    const auto r = std::to_chars(b, b + sizeof b, v);
    return r.ec == std::errc{} ? std::string(b, r.ptr) : std::string("0");
}

// La table : les unites courantes (fournies comprises : elles relient une famille du projet a la table), par famille.
//  symbole, famille, facteur, decalage, libelle, reference de la famille, libelle de la famille (une famille neuve).
const std::vector<KnownUnit>& knownUnits() {
    static const std::vector<KnownUnit> k = {
        // pression (Pa)
        {"Pa", "pression", 1.0, 0.0, "pascal", "Pa", "Pression"},
        {"hPa", "pression", 100.0, 0.0, "hectopascal", "Pa", "Pression"},
        {"kPa", "pression", 1000.0, 0.0, "kilopascal", "Pa", "Pression"},
        {"MPa", "pression", 1.0e6, 0.0, "m\xC3\xA9gapascal", "Pa", "Pression"},
        {"mbar", "pression", 100.0, 0.0, "millibar", "Pa", "Pression"},
        {"bar", "pression", 100000.0, 0.0, "bar", "Pa", "Pression"},
        {"psi", "pression", 6894.757293, 0.0, "livre par pouce carr\xC3\xA9", "Pa", "Pression"},
        {"mmCE", "pression", 9.80665, 0.0, "millim\xC3\xA8tre de colonne d'eau", "Pa", "Pression"},
        {"cmCE", "pression", 98.0665, 0.0, "centim\xC3\xA8tre de colonne d'eau", "Pa", "Pression"},
        {"mCE", "pression", 9806.65, 0.0, "m\xC3\xA8tre de colonne d'eau", "Pa", "Pression"},
        {"mmH2O", "pression", 9.80665, 0.0, "millim\xC3\xA8tre d'eau", "Pa", "Pression"},
        {"inH2O", "pression", 249.08891, 0.0, "pouce d'eau", "Pa", "Pression"},
        {"mmHg", "pression", 133.322387415, 0.0, "millim\xC3\xA8tre de mercure", "Pa", "Pression"},
        {"Torr", "pression", 101325.0 / 760.0, 0.0, "torr", "Pa", "Pression"},
        {"atm", "pression", 101325.0, 0.0, "atmosph\xC3\xA8re", "Pa", "Pression"},
        // temperature (K)
        {"K", "temperature", 1.0, 0.0, "kelvin", "K", "Temp\xC3\xA9rature"},
        {"degC", "temperature", 1.0, 273.15, "degr\xC3\xA9 Celsius", "K", "Temp\xC3\xA9rature"},
        {"degF", "temperature", 5.0 / 9.0, 459.67 * 5.0 / 9.0, "degr\xC3\xA9 Fahrenheit", "K", "Temp\xC3\xA9rature"},
        // debit (m3/h)
        {"m3/h", "debit", 1.0, 0.0, "m\xC3\xA8tre cube par heure", "m3/h", "D\xC3\xA9" "bit"},
        {"m3/min", "debit", 60.0, 0.0, "m\xC3\xA8tre cube par minute", "m3/h", "D\xC3\xA9" "bit"},
        {"m3/s", "debit", 3600.0, 0.0, "m\xC3\xA8tre cube par seconde", "m3/h", "D\xC3\xA9" "bit"},
        {"m3/j", "debit", 1.0 / 24.0, 0.0, "m\xC3\xA8tre cube par jour", "m3/h", "D\xC3\xA9" "bit"},
        {"L/s", "debit", 3.6, 0.0, "litre par seconde", "m3/h", "D\xC3\xA9" "bit"},
        {"L/min", "debit", 0.06, 0.0, "litre par minute", "m3/h", "D\xC3\xA9" "bit"},
        {"L/h", "debit", 0.001, 0.0, "litre par heure", "m3/h", "D\xC3\xA9" "bit"},
        // debit normalise (Nm3/h) - jamais converti vers m3/h
        {"Nm3/h", "debit_normalise", 1.0, 0.0, "m\xC3\xA8tre cube normal par heure", "Nm3/h", "D\xC3\xA9" "bit normalis\xC3\xA9"},
        {"Nm3/min", "debit_normalise", 60.0, 0.0, "m\xC3\xA8tre cube normal par minute", "Nm3/h", "D\xC3\xA9" "bit normalis\xC3\xA9"},
        {"Nm3/j", "debit_normalise", 1.0 / 24.0, 0.0, "m\xC3\xA8tre cube normal par jour", "Nm3/h", "D\xC3\xA9" "bit normalis\xC3\xA9"},
        {"NL/min", "debit_normalise", 0.06, 0.0, "litre normal par minute", "Nm3/h", "D\xC3\xA9" "bit normalis\xC3\xA9"},
        // debit massique (kg/h) - une famille neuve
        {"kg/h", "debit_massique", 1.0, 0.0, "kilogramme par heure", "kg/h", "D\xC3\xA9" "bit massique"},
        {"kg/min", "debit_massique", 60.0, 0.0, "kilogramme par minute", "kg/h", "D\xC3\xA9" "bit massique"},
        {"kg/s", "debit_massique", 3600.0, 0.0, "kilogramme par seconde", "kg/h", "D\xC3\xA9" "bit massique"},
        {"g/s", "debit_massique", 3.6, 0.0, "gramme par seconde", "kg/h", "D\xC3\xA9" "bit massique"},
        {"t/h", "debit_massique", 1000.0, 0.0, "tonne par heure", "kg/h", "D\xC3\xA9" "bit massique"},
        {"t/j", "debit_massique", 1000.0 / 24.0, 0.0, "tonne par jour", "kg/h", "D\xC3\xA9" "bit massique"},
        // humidite relative (%HR) - une famille neuve, sans autre unite
        {"%HR", "humidite", 1.0, 0.0, "pour cent d'humidit\xC3\xA9 relative", "%HR", "Humidit\xC3\xA9 relative"},
        // puissance (W) - une famille neuve
        {"W", "puissance", 1.0, 0.0, "watt", "W", "Puissance"},
        {"kW", "puissance", 1000.0, 0.0, "kilowatt", "W", "Puissance"},
        {"MW", "puissance", 1.0e6, 0.0, "m\xC3\xA9gawatt", "W", "Puissance"},
        // energie (J) - une famille neuve
        {"J", "energie", 1.0, 0.0, "joule", "J", "\xC3\x89nergie"},
        {"kJ", "energie", 1000.0, 0.0, "kilojoule", "J", "\xC3\x89nergie"},
        {"MJ", "energie", 1.0e6, 0.0, "m\xC3\xA9gajoule", "J", "\xC3\x89nergie"},
        {"Wh", "energie", 3600.0, 0.0, "wattheure", "J", "\xC3\x89nergie"},
        {"kWh", "energie", 3.6e6, 0.0, "kilowattheure", "J", "\xC3\x89nergie"},
        {"MWh", "energie", 3.6e9, 0.0, "m\xC3\xA9gawattheure", "J", "\xC3\x89nergie"},
        // vitesse (m/s), vitesse de rotation (tr/min), volume (m3) - des familles neuves
        {"m/s", "vitesse", 1.0, 0.0, "m\xC3\xA8tre par seconde", "m/s", "Vitesse"},
        {"m/min", "vitesse", 1.0 / 60.0, 0.0, "m\xC3\xA8tre par minute", "m/s", "Vitesse"},
        {"km/h", "vitesse", 1.0 / 3.6, 0.0, "kilom\xC3\xA8tre par heure", "m/s", "Vitesse"},
        {"tr/min", "vitesse_rotation", 1.0, 0.0, "tour par minute", "tr/min", "Vitesse de rotation"},
        {"rpm", "vitesse_rotation", 1.0, 0.0, "tour par minute", "tr/min", "Vitesse de rotation"},
        {"tr/s", "vitesse_rotation", 60.0, 0.0, "tour par seconde", "tr/min", "Vitesse de rotation"},
        {"m3", "volume", 1.0, 0.0, "m\xC3\xA8tre cube", "m3", "Volume"},
        {"L", "volume", 0.001, 0.0, "litre", "m3", "Volume"},
        {"hL", "volume", 0.1, 0.0, "hectolitre", "m3", "Volume"},
        // les autres familles fournies
        {"s", "temps", 1.0, 0.0, "seconde", "s", "Temps"},
        {"m", "longueur", 1.0, 0.0, "m\xC3\xA8tre", "m", "Longueur"},
        {"um", "longueur", 1.0e-6, 0.0, "microm\xC3\xA8tre", "m", "Longueur"},
        {"mm", "longueur", 0.001, 0.0, "millim\xC3\xA8tre", "m", "Longueur"},
        {"in", "longueur", 0.0254, 0.0, "pouce", "m", "Longueur"},
        {"ft", "longueur", 0.3048, 0.0, "pied", "m", "Longueur"},
        {"kg", "masse", 1.0, 0.0, "kilogramme", "kg", "Masse"},
        {"t", "masse", 1000.0, 0.0, "tonne", "kg", "Masse"},
        {"lb", "masse", 0.45359237, 0.0, "livre", "kg", "Masse"},
        {"V", "tension", 1.0, 0.0, "volt", "V", "Tension"},
        {"kV", "tension", 1000.0, 0.0, "kilovolt", "V", "Tension"},
        {"A", "courant", 1.0, 0.0, "amp\xC3\xA8re", "A", "Courant"},
        {"mA", "courant", 0.001, 0.0, "milliamp\xC3\xA8re", "A", "Courant"},
        {"kA", "courant", 1000.0, 0.0, "kiloamp\xC3\xA8re", "A", "Courant"},
        {"Hz", "frequence", 1.0, 0.0, "hertz", "Hz", "Fr\xC3\xA9quence"},
        {"%", "pourcentage", 1.0, 0.0, "pour cent", "%", "Pourcentage"},
        {"ppm", "pourcentage", 1.0e-4, 0.0, "partie par million", "%", "Pourcentage"},
    };
    return k;
}

const KnownUnit* knownUnit(std::string_view symbol) {
    const std::string s = fold(symbol);
    if (s.empty()) return nullptr;
    for (const auto& k : knownUnits())
        if (fold(k.symbol) == s) return &k;
    return nullptr;
}

std::optional<Suggestion> expected(const Catalog& cat, std::string_view symbol, std::string_view reference) {
    const KnownUnit* k = knownUnit(symbol);
    if (!k) return std::nullopt;
    Suggestion s;
    s.unit.symbol = std::string(k->symbol);
    s.unit.label = std::string(k->label);
    // De S vers X (deux unites de la table, la meme famille) : v_X = v_S * a + b.
    const auto toward = [&](const KnownUnit& x, double& a, double& b) {
        a = k->factor / x.factor;
        b = (k->offset - x.offset) / x.factor;
    };
    if (const UnitFamily* f = cat.family(k->family)) {
        // La famille existe : reliee par une de ses unites que la table connait (sa reference d'abord : le compte exact).
        s.unit.family = f->name;
        std::vector<const UnitDef*> members;
        if (const UnitDef* r = cat.unit(f->reference)) members.push_back(r);
        for (const auto& u : cat.units())
            if (fold(u.family) == fold(f->name)) members.push_back(&u);
        for (const UnitDef* u : members) {
            const KnownUnit* x = knownUnit(u->symbol);
            if (!x || x->family != k->family || x->factor == 0.0) continue;
            double a = 0, b = 0;
            toward(*x, a, b);
            s.unit.factor = a * u->factor;                // v_ref = v_X * u.factor + u.offset
            s.unit.offset = b * u->factor + u->offset;
            return s;
        }
        return std::nullopt;
    }
    // Une famille neuve : sa reference (celle de la table, ou celle deja choisie si la table la connait).
    const std::string ref = trimmedText(reference).empty() ? std::string(k->reference) : trimmedText(reference);
    s.unit.family = std::string(k->family);
    s.family = {std::string(k->family), ref, std::string(k->familyLabel)};
    if (fold(ref) == fold(k->reference)) {
        s.family.reference = std::string(k->reference);
        s.unit.factor = k->factor;
        s.unit.offset = k->offset;
        return s;
    }
    const KnownUnit* x = knownUnit(ref);
    if (!x || x->family != k->family || x->factor == 0.0) return std::nullopt;
    double a = 0, b = 0;
    toward(*x, a, b);
    s.family.reference = std::string(x->symbol);
    s.unit.factor = a;
    s.unit.offset = b;
    return s;
}

std::optional<Suggestion> suggestUnit(const Catalog& cat, std::string_view symbol) {
    if (cat.unit(symbol)) return std::nullopt;
    return expected(cat, symbol);
}

namespace {
// << 1 mmCE = 9,80665 Pa >> ; une affine : << degF -> K : x x 0,555556 + 255,372 >> (comme formulaText, sans catalogue).
std::string affineShown(std::string_view from, std::string_view to, double factor, double offset) {
    const std::string a = unitLabel(from), b = unitLabel(to);
    if (offset == 0.0) return "1 " + a + " = " + frenchShown(shortNumber(factor)) + " " + b;
    std::string f = "x";
    if (shortNumber(factor) != "1") f += " \xC3\x97 " + frenchShown(shortNumber(factor));
    f += (offset >= 0 ? " + " : " - ") + frenchShown(shortNumber(std::fabs(offset)));
    return a + " -> " + b + " : " + f;
}
} // namespace

std::string coherenceWarning(const Catalog& cat, const UnitDef& u, std::string_view reference) {
    const auto e = expected(cat, u.symbol, reference);
    if (!e) return {};
    const std::string ref = !e->family.name.empty() ? e->family.reference : [&] {
        const UnitFamily* f = cat.family(e->unit.family);
        return f ? f->reference : std::string{};
    }();
    const std::string usual = affineShown(e->unit.symbol, ref, e->unit.factor, e->unit.offset);
    const std::string mine = trimmedText(u.family);
    if (fold(mine) != fold(e->unit.family) && fold(familyFrom(cat, mine)) != fold(e->unit.family))
        return "\xC2\xAB " + trimmedText(u.symbol) + " \xC2\xBB est d'ordinaire de la famille " + e->unit.family + " (" + usual + "), pas " + mine
               + " : v\xC3\xA9rifie la famille";
    const double tol = 0.01;
    const bool farFactor = std::fabs(u.factor - e->unit.factor) > tol * std::fabs(e->unit.factor);
    const bool farOffset = std::fabs(u.offset - e->unit.offset) > tol * std::max(1.0, std::fabs(e->unit.offset));
    if (!farFactor && !farOffset) return {};
    return "d'ordinaire " + usual + ", pas " + affineShown(trimmedText(u.symbol), ref, u.factor, u.offset) + " : v\xC3\xA9rifie le facteur";
}

} // namespace hmi::units
