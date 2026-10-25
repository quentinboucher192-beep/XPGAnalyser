// =============================================================================
//  hmi/HmiUnits.cpp - 1.13.0 (lot J6) : la bibliotheque des unites (voir l'en-tete)
// =============================================================================
#include "HmiUnits.hpp"
#include "HmiTypeRegistry.hpp"   // typereg::isNumber
#include "../project/MemberTree.hpp"   // project::members::parseArray (l'element d'un ARRAY)

#include <algorithm>
#include <cctype>

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
            if (d == 0xB0) { i += 2; continue; }        // le degre : ignore (degc == °c)
            if (d == 0xB2) { out += '2'; i += 2; continue; }
            if (d == 0xB3) { out += '3'; i += 2; continue; }
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

std::string unitOfPath(const Project& project, std::string_view path) {
    // Decoupe le chemin : un identifiant, puis des .membre et des [indice].
    std::string_view p = path;
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.front()))) p.remove_prefix(1);
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.remove_suffix(1);
    if (p.empty()) return {};
    std::size_t i = 0;
    const auto word = [&] {
        const std::size_t s = i;
        while (i < p.size() && (std::isalnum(static_cast<unsigned char>(p[i])) || p[i] == '_')) ++i;
        return std::string(p.substr(s, i - s));
    };
    const std::string root = word();
    if (root.empty()) return {};
    const Variable* var = project.variable(root);
    if (!var) return {};
    std::string type = var->type;
    std::string unit = var->unit;
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
            const HmiType* ht = project.hmiTypeByName(stripArray(type));
            if (!ht || ht->kind != HmiTypeKind::Structure) return {};
            const TypeMember* found = nullptr;
            for (const auto& m : ht->members) {
                std::string a, b;
                a.reserve(m.name.size());
                for (char c : m.name) a += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (char c : member) b += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

} // namespace hmi::units
