// =============================================================================
//  hmi/HmiRoles.cpp - 1.12.6 (lots B et C) : le lecteur par role, le decoupeur
//                     unique des listes, la conversion a l'ecriture
// -----------------------------------------------------------------------------
//  Voir HmiRoles.hpp. Les regles sont celles des § 2.1 (lire), 2.2 (ecrire),
//  2.4 et 2.8 (les listes, les reperes) de docs/AUDIT-BIBLIOTHEQUE-OBJETS.md.
// =============================================================================
#include "HmiRoles.hpp"

#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"
#include "HmiTypeRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace hmi::roles {

namespace {

constexpr std::int64_t kDayMs = 86'400'000;

std::string_view trimView(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string upperCopy(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

// Un mot sans ses apostrophes ni ses blancs du bord ('TRUE' -> TRUE).
std::string_view bare(std::string_view s) noexcept {
    s = trimView(s);
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front()) s = trimView(s.substr(1, s.size() - 2));
    return s;
}

bool falseWord(std::string_view t) noexcept {
    t = bare(t);
    return iequal(t, "FALSE") || iequal(t, "FAUX") || t == "0" || iequal(t, "NON") || iequal(t, "OFF");
}

// Un nombre au plus court, qui se relit a l'identique (2 ; 1.5 ; 1234567.8).
std::string numberText(double x) { return exactValue(sim::Value::real(x)); }

std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

std::string two(long v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02ld", v);
    return b;
}

// L'heure d'un jour (des millisecondes depuis minuit) : 14:30:00 (,250 si des millisecondes).
std::string clockFrench(std::int64_t ms) {
    const std::int64_t s = ms / 1000;
    std::string out = two(static_cast<long>(s / 3600)) + ":" + two(static_cast<long>((s / 60) % 60)) + ":" + two(static_cast<long>(s % 60));
    if (const auto rest = ms % 1000; rest != 0) {
        char b[8];
        std::snprintf(b, sizeof b, ",%03lld", static_cast<long long>(rest));
        out += b;
    }
    return out;
}

// Un jour du calendrier (des jours depuis le 1970-01-01) : 09/10/2026.
std::string dayFrench(std::int64_t days) {
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    sim::civilFromDays(days, y, m, d);
    char b[24];
    std::snprintf(b, sizeof b, "%02u/%02u/%04lld", d, m, static_cast<long long>(y));
    return b;
}

bool isDate(sim::Type t) noexcept { return t == sim::Type::Date || t == sim::Type::Tod || t == sim::Type::Dt; }

} // namespace

// ================================================================ lire ======
bool trueWord(std::string_view t) noexcept {
    t = bare(t);
    return iequal(t, "TRUE") || iequal(t, "VRAI") || t == "1" || iequal(t, "OUI") || iequal(t, "ON");
}

bool readBool(std::string_view text, bool fallback) noexcept {
    const std::string_view t = bare(text);
    if (t.empty()) return fallback;
    if (trueWord(t)) return true;
    if (falseWord(t)) return false;
    double x = 0;
    if (parseNumber(t, x)) return x != 0.0;
    return fallback;
}

bool readNumber(std::string_view text, double& out) noexcept {
    const std::string_view t = bare(text);
    if (parseNumber(t, out)) return true;
    if (iequal(t, "TRUE") || iequal(t, "VRAI")) { out = 1; return true; }
    if (iequal(t, "FALSE") || iequal(t, "FAUX")) { out = 0; return true; }
    return false;
}

std::string displayText(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool: return v.isTruthy() ? "Vrai" : "Faux";
        case sim::Type::Time: return formatValue(v, "t");
        case sim::Type::Date: return dayFrench(floorDiv(v.asInteger(), kDayMs));
        case sim::Type::Tod: return clockFrench(((v.asInteger() % kDayMs) + kDayMs) % kDayMs);
        case sim::Type::Dt: {
            const std::int64_t days = floorDiv(v.asInteger(), kDayMs);
            return dayFrench(days) + " " + clockFrench(v.asInteger() - days * kDayMs);
        }
        case sim::Type::String: return v.asString();
        default: return formatValue(v);
    }
}

Read adapt(const KeyRole& r, const sim::Value& v, const Shape& shape) {
    Read out;
    const Role role = r.role == Role::Formula ? r.item : r.role;
    const auto refuse = [&out](std::string why) {
        out.ok = false;
        out.why = std::move(why);
        out.text = "###";
        return out;
    };
    // Une structure, un tableau, une liste entiers : un texte les montre ; un nombre, un booleen,
    // un etat n'en savent rien (le moteur le dit, Generer le dit avant).
    if (!shape.aggregate.empty()) {
        if (role == Role::Text || role == Role::Mini || role == Role::Format || role == Role::Name || role == Role::Choice) {
            out.text = shape.aggregate;
            return out;
        }
        return refuse(shape.aggregateWhy.empty() ? std::string("une valeur seule est attendue (") + shape.aggregate + ")" : shape.aggregateWhy);
    }
    switch (role) {
        case Role::Number:
        case Role::Duration: {
            const Unit unit = role == Role::Duration ? r.unit : Unit::Seconds;
            switch (v.type()) {
                case sim::Type::Bool: out.text = v.isTruthy() ? "1" : "0"; break;
                case sim::Type::Time:
                    out.text = unit == Unit::Milliseconds ? std::to_string(v.asInteger())
                                                          : numberText(static_cast<double>(v.asInteger()) / 1000.0);
                    break;
                // Une date : des secondes depuis 1970 (une courbe XY, un chronogramme) ; une heure : depuis minuit.
                case sim::Type::Date: case sim::Type::Dt: out.text = numberText(static_cast<double>(v.asInteger()) / 1000.0); break;
                case sim::Type::Tod: out.text = numberText(static_cast<double>(((v.asInteger() % kDayMs) + kDayMs) % kDayMs) / 1000.0); break;
                case sim::Type::String: {
                    double x = 0;
                    if (!readNumber(v.asString(), x))
                        return refuse("'" + v.asString() + "' n'est pas un nombre");
                    out.text = numberText(x);
                    break;
                }
                case sim::Type::Unknown: return refuse("aucune valeur");
                default: out.text = exactValue(v); break;
            }
            return out;
        }
        case Role::Boolean:
            if (isDate(v.type())) return refuse("une date n'est ni vraie ni fausse : compare-la (Jour >= D#2026-01-01)");
            if (v.type() == sim::Type::Unknown) return refuse("aucune valeur");
            out.text = (v.type() == sim::Type::String ? trueWord(v.asString()) : v.isTruthy()) ? "TRUE" : "FALSE";
            return out;
        case Role::State:
            if (isDate(v.type())) return refuse("une date n'est pas un \xC3\xA9tat : compare-la");
            if (v.type() == sim::Type::Unknown) return refuse("aucune valeur");
            out.text = v.type() == sim::Type::Bool ? (v.isTruthy() ? "1" : "0") : v.type() == sim::Type::String ? v.asString() : exactValue(v);
            out.name = !shape.enumText.empty() ? shape.enumText : v.type() == sim::Type::String ? v.asString() : std::string{};
            return out;
        case Role::Text:
            out.text = !shape.enumText.empty() ? shape.enumText : displayText(v);
            return out;
        case Role::Color:
            if (v.type() != sim::Type::String)
                return refuse("une couleur est attendue ('#RRGGBB') et la valeur est " + formatValue(v));
            out.text = std::string(trimView(v.asString()));
            return out;
        case Role::Name:
            out.text = v.type() == sim::Type::String ? nameOf(v.asString()) : displayText(v);
            return out;
        case Role::Choice:
        case Role::List:
        case Role::Mini:
        case Role::Format:
        case Role::Date:
        case Role::Formula:
            out.text = v.type() == sim::Type::String ? v.asString() : exactValue(v);
            return out;
    }
    out.text = exactValue(v);
    return out;
}

// ============================================================ decouper =====
std::vector<std::string> splitList(std::string_view text, char sep) {
    std::vector<std::string> out;
    if (trimView(text).empty()) return out;
    for (auto& piece : splitListRaw(text, sep)) out.emplace_back(trimView(piece));
    return out;
}

std::vector<std::string> splitListRaw(std::string_view text, char sep) {
    std::vector<std::string> out;
    // Les reperes (mode Texte : un `$` entre apostrophes en ouvre un aussi).
    const auto spans = markers::find(text, markers::Mode::Text);
    std::size_t span = 0;
    int depth = 0;           // [ ( {
    char quote = 0;          // ' ou " : dans une chaine (seulement quand elle se referme : un apostrophe du francais n'en ouvre pas)
    std::string cur;
    const auto glued = [&](std::size_t i) {
        if (i == 0) return false;
        const auto p = static_cast<unsigned char>(text[i - 1]);
        return std::isalnum(p) || p == '_' || p == ')' || p == ']' || p == '\'' || p == '"' || p >= 0x80;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        while (span < spans.size() && spans[span].end() <= i) ++span;
        const bool inMarker = span < spans.size() && i >= spans[span].at && i < spans[span].end();
        const char c = text[i];
        if (!inMarker) {
            if (quote) {
                if (c == quote) {
                    // '' dans une chaine ST : une apostrophe ecrite
                    if (i + 1 < text.size() && text[i + 1] == quote) { cur += c; cur += c; ++i; continue; }
                    quote = 0;
                }
            } else if ((c == '\'' || c == '"') && !glued(i) && text.find(c, i + 1) != std::string_view::npos
                       && text.substr(i + 1, text.find(c, i + 1) - i - 1).find(sep) != std::string_view::npos) {
                // Une chaine qui porte le separateur ('a;b') : entiere. Une apostrophe collee a une lettre,
                // un chiffre ou une parenthese (l'armoire, d'urgence, la fin de 'N2') n'ouvre rien.
                quote = c;
            } else if (c == '[' || c == '(' || c == '{') {
                ++depth;
            } else if ((c == ']' || c == ')' || c == '}') && depth > 0) {
                --depth;
            } else if (c == sep && depth == 0) {
                out.push_back(cur);
                cur.clear();
                continue;
            }
        }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

bool colorOk(std::string_view text) noexcept {
    const std::string_view s = trimView(text);
    if (s.empty() || s == "transparent" || s == "aucun") return true;
    std::string_view h = s;
    if (h.front() == '#') h.remove_prefix(1);
    else if (h.size() > 3 && h.substr(0, 3) == "16#") h.remove_prefix(3);
    else return false;
    if (h.size() != 6 && h.size() != 8) return false;
    return std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

std::string formatTrap(std::string_view format) {
    const std::string_view f = trimView(format);
    if (f.find(',') == std::string_view::npos || f.find('.') != std::string_view::npos || f.find('|') != std::string_view::npos) return {};
    if (!std::all_of(f.begin(), f.end(), [](char c) { return c == '0' || c == '#' || c == ',' || c == '+' || c == '%'; })) return {};
    std::string fixed(f);
    std::replace(fixed.begin(), fixed.end(), ',', '.');
    return "le format " + std::string(f) + " donne un entier (la virgule n'y est pas une d\xC3\xA9" "cimale) : pour des d\xC3\xA9" "cimales, \xC3\xA9" "cris " + fixed;
}

std::string nameOf(std::string_view text) { return std::string(trimView(markers::strip(text, markers::Mode::Text))); }

std::string plainText(std::string_view text) { return markers::strip(text, markers::Mode::Text); }

std::vector<std::string> listItems(const KeyRole& r, std::string_view text) {
    auto items = splitList(text);
    if (r.item == Role::Formula || r.item == Role::Format || r.item == Role::Mini) return items;
    for (auto& s : items) s = nameOf(s);
    return items;
}

// ============================================================== ecrire =====
bool integerBounds(std::string_view typeName, sim::Type t, long double& low, long double& high) {
    if (!typeName.empty()) {
        const auto n = typereg::numericOf(typeName);
        if (n.family == typereg::Family::Integer) {
            low = n.low;
            high = n.high;
            return true;
        }
        if (n.family != typereg::Family::None) return false;
    }
    switch (t) {
        case sim::Type::Byte: low = 0; high = 255; return true;
        case sim::Type::Word: case sim::Type::UInt: low = 0; high = 65535; return true;
        case sim::Type::DWord: case sim::Type::UDInt: low = 0; high = 4294967295.0L; return true;
        case sim::Type::Int: low = -32768; high = 32767; return true;
        case sim::Type::DInt: low = -2147483648.0L; high = 2147483647.0L; return true;
        case sim::Type::LInt: low = -9223372036854775807.0L - 1; high = 9223372036854775807.0L; return true;
        case sim::Type::ULInt: case sim::Type::LWord: low = 0; high = 18446744073709551615.0L; return true;
        default: return false;
    }
}

bool parseDuration(std::string_view text, std::int64_t& ms, Unit bare) {
    std::string t = upperCopy(trimView(text));
    if (t.empty()) return false;
    for (const char* prefix : {"TIME#", "T#"})
        if (t.rfind(prefix, 0) == 0) {
            t.erase(0, std::string_view(prefix).size());
            bare = Unit::Milliseconds;   // T#500 : des millisecondes, comme le ST
            break;
        }
    std::string s;
    for (const char c : t)
        if (c != ' ' && c != '_') s += c == ',' ? '.' : c;
    if (s.empty()) return false;
    const bool negative = s.front() == '-';
    if (negative) s.erase(0, 1);
    // 00:05 (mm:ss), 01:30:00 (hh:mm:ss)
    if (s.find(':') != std::string::npos) {
        std::vector<double> parts;
        std::size_t from = 0;
        while (true) {
            const auto colon = s.find(':', from);
            const std::string piece = s.substr(from, colon == std::string::npos ? std::string::npos : colon - from);
            double x = 0;
            if (piece.empty() || !parseNumber(piece, x) || x < 0) return false;
            parts.push_back(x);
            if (colon == std::string::npos) break;
            from = colon + 1;
        }
        if (parts.size() < 2 || parts.size() > 3) return false;
        const double seconds = parts.size() == 3 ? parts[0] * 3600 + parts[1] * 60 + parts[2] : parts[0] * 60 + parts[1];
        ms = static_cast<std::int64_t>(std::llround(seconds * 1000.0)) * (negative ? -1 : 1);
        return true;
    }
    // Des nombres suivis d'une unite : 1H30, 90MIN, 1.5S, 250MS, 2D4H ; un nombre seul apres H : des minutes.
    double total = 0;
    std::size_t i = 0;
    std::string last;
    bool any = false;
    while (i < s.size()) {
        std::size_t j = i;
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
        if (j == i) return false;
        double x = 0;
        if (!parseNumber(s.substr(i, j - i), x)) return false;
        std::size_t k = j;
        while (k < s.size() && std::isalpha(static_cast<unsigned char>(s[k]))) ++k;
        std::string unit = s.substr(j, k - j);
        if (unit.empty()) unit = last == "H" ? "MIN" : last == "MIN" || last == "M" ? "S" : bare == Unit::Milliseconds ? "MS" : "S";
        double factor = 0;
        if (unit == "MS") factor = 1;
        else if (unit == "S" || unit == "SEC") factor = 1000;
        else if (unit == "M" || unit == "MIN" || unit == "MN") factor = 60'000;
        else if (unit == "H") factor = 3'600'000;
        else if (unit == "D" || unit == "J") factor = kDayMs;
        else return false;
        total += x * factor;
        last = unit;
        any = true;
        i = k;
    }
    if (!any) return false;
    ms = static_cast<std::int64_t>(std::llround(total)) * (negative ? -1 : 1);
    return true;
}

namespace {

std::string typeWord(const Target& t) {
    if (!t.typeName.empty()) return t.typeName;
    return std::string(sim::toString(t.type));
}

std::string valueList(const Target& t) {
    std::string s;
    for (std::size_t k = 0; k < t.enumValues.size() && k < 6; ++k) s += (s.empty() ? "" : ", ") + t.enumValues[k].first;
    if (t.enumValues.size() > 6) s += "...";
    return s;
}

// Un nombre (deja lu) vers une variable numerique : arrondi pour un entier, ramene dans les
// bornes du type et de l'objet (la Console le dit).
Written numberInto(double x, const Target& t) {
    Written w;
    w.ok = true;
    std::string why;
    if (!std::isnan(t.min) && x < t.min) { why = " (le minimum de l'objet)"; x = t.min; }
    if (!std::isnan(t.max) && x > t.max) { why = " (le maximum de l'objet)"; x = t.max; }
    const double asked = x;
    if (t.type == sim::Type::Real) {
        w.value = sim::Value::real(x);
    } else {
        long double low = 0, high = 0;
        long double n = std::llround(x);
        if (std::fabs(x) >= 9.2e18) n = x < 0 ? -9.2e18L : 9.2e18L;
        if (integerBounds(t.typeName, t.type, low, high)) {
            if (n < low) { n = low; why = " (le minimum d'un " + typeWord(t) + ")"; }
            if (n > high) { n = high; why = " (le maximum d'un " + typeWord(t) + ")"; }
        }
        const sim::Type st = t.type == sim::Type::Unknown ? sim::Type::DInt : t.type;
        if (st == sim::Type::ULInt || st == sim::Type::LWord)
            w.value = sim::Value::integer(st, static_cast<std::int64_t>(static_cast<unsigned long long>(n)));
        else
            w.value = sim::Value::integer(st, static_cast<std::int64_t>(n));
        x = static_cast<double>(n);
    }
    if (!why.empty()) w.note = numberText(asked) + " ramen\xC3\xA9 \xC3\xA0 " + numberText(x) + why;
    return w;
}

Written refused(std::string note) {
    Written w;
    w.ok = false;
    w.note = std::move(note);
    return w;
}

} // namespace

Written toVariable(const sim::Value& in, const Target& t) {
    const std::string where = t.path.empty() ? std::string("la variable") : t.path;
    if (!t.aggregate.empty())
        return refused(where + " est " + t.aggregate + " : \xC3\xA9" "cris un de ses membres"
                       + (t.members.empty() ? std::string{} : " (" + t.members + ")") + " - rien n'est \xC3\xA9" "crit");
    // ---- une enumeration du projet : par son nom (Auto, T_MODE#Auto) ou son numero
    if (!t.enumValues.empty()) {
        std::int64_t n = 0;
        bool found = false;
        if (in.type() == sim::Type::String) {
            std::string_view name = bare(in.asString());
            if (const auto hash = name.find('#'); hash != std::string_view::npos) name = name.substr(hash + 1);
            for (const auto& [label, value] : t.enumValues)
                if (iequal(label, name)) { n = value; found = true; break; }
            double x = 0;
            if (!found && parseNumber(name, x) && x == std::floor(x)) {
                n = static_cast<std::int64_t>(x);
                for (const auto& [label, value] : t.enumValues) found = found || value == n;
            }
            if (!found)
                return refused("'" + std::string(bare(in.asString())) + "' n'est pas une valeur de " + typeWord(t) + " (" + valueList(t)
                               + ") - rien n'est \xC3\xA9" "crit");
        } else {
            n = in.type() == sim::Type::Real ? static_cast<std::int64_t>(std::llround(in.asReal())) : in.asInteger();
            for (const auto& [label, value] : t.enumValues) found = found || value == n;
            if (!found)
                return refused(std::to_string(n) + " n'est pas une valeur de " + typeWord(t) + " (" + valueList(t) + ") - rien n'est \xC3\xA9" "crit");
        }
        Written w;
        w.ok = true;
        w.value = sim::Value::integer(t.type == sim::Type::Unknown ? sim::Type::DInt : t.type, n);
        return w;
    }
    Written w;
    w.ok = true;
    switch (t.type) {
        case sim::Type::Unknown:
            w.value = in;
            return w;
        case sim::Type::Bool:
            if (in.type() == sim::Type::String) {
                if (trueWord(in.asString())) w.value = sim::Value::boolean(true);
                else if (falseWord(in.asString())) w.value = sim::Value::boolean(false);
                else return refused("'" + in.asString() + "' : vrai ou faux attendu (TRUE, VRAI, 1, OUI, ON ; FALSE, FAUX, 0, NON, OFF) - rien n'est \xC3\xA9" "crit");
            } else {
                w.value = sim::Value::boolean(in.isTruthy());
            }
            return w;
        case sim::Type::String:
            switch (in.type()) {
                case sim::Type::String: w.value = in; break;
                case sim::Type::Bool: w.value = sim::Value::text(in.isTruthy() ? "TRUE" : "FALSE"); break;
                case sim::Type::Real: w.value = sim::Value::text(t.format.empty() ? exactValue(in) : formatValue(in, t.format)); break;
                case sim::Type::Time: w.value = sim::Value::text(in.display()); break;
                case sim::Type::Date: case sim::Type::Tod: case sim::Type::Dt:
                    w.value = sim::Value::text(sim::dateText(in.type(), in.asInteger()));
                    break;
                default: w.value = sim::Value::text(t.format.empty() ? exactValue(in) : formatValue(in, t.format)); break;
            }
            return w;
        case sim::Type::Time: {
            std::int64_t ms = 0;
            if (in.type() == sim::Type::Time) ms = in.asInteger();
            else if (in.type() == sim::Type::String) {
                if (!parseDuration(in.asString(), ms, t.unit))
                    return refused("'" + in.asString() + "' n'est pas une dur\xC3\xA9" "e (T#5s, 5 s, 00:05, 1 h 30) - rien n'est \xC3\xA9" "crit");
            } else if (in.type() == sim::Type::Bool) {
                return refused(where + " est une dur\xC3\xA9" "e (TIME) : un bool\xC3\xA9" "en ne s'y \xC3\xA9" "crit pas");
            } else {
                const double x = in.type() == sim::Type::Real ? in.asReal() : static_cast<double>(in.asInteger());
                ms = t.unit == Unit::Milliseconds ? static_cast<std::int64_t>(std::llround(x)) : static_cast<std::int64_t>(std::llround(x * 1000.0));
            }
            w.value = sim::Value::time(ms);
            return w;
        }
        case sim::Type::Date: case sim::Type::Tod: case sim::Type::Dt: {
            std::int64_t ms = 0;
            if (isDate(in.type())) ms = in.asInteger();
            else if (in.type() == sim::Type::String) {
                std::string s(bare(in.asString()));
                for (const char* prefix : {"DATE_AND_TIME#", "TIME_OF_DAY#", "DATE#", "DT#", "TOD#", "D#"})
                    if (upperCopy(s).rfind(prefix, 0) == 0) { s.erase(0, std::string_view(prefix).size()); break; }
                // 09/10/2026 (14:30) a la francaise : 2026-10-09(-14:30)
                if (s.size() >= 10 && s[2] == '/' && s[5] == '/') {
                    std::string iso = s.substr(6, 4) + "-" + s.substr(3, 2) + "-" + s.substr(0, 2);
                    const std::string rest(trimView(std::string_view(s).substr(10)));
                    s = rest.empty() ? iso : iso + "-" + rest;
                }
                if (!sim::parseDateText(t.type, s, ms))
                    return refused("'" + in.asString() + "' n'est pas une date ou une heure lisible - rien n'est \xC3\xA9" "crit");
            } else if (in.type() == sim::Type::Bool) {
                return refused(where + " est une date : un bool\xC3\xA9" "en ne s'y \xC3\xA9" "crit pas");
            } else {
                ms = in.type() == sim::Type::Real ? static_cast<std::int64_t>(std::llround(in.asReal())) : in.asInteger();
            }
            w.value = sim::Value::integer(t.type, ms);
            return w;
        }
        default: {
            // un nombre : entier ou reel
            double x = 0;
            switch (in.type()) {
                case sim::Type::String:
                    if (!readNumber(in.asString(), x))
                        return refused("'" + in.asString() + "' n'est pas un nombre - rien n'est \xC3\xA9" "crit dans " + where);
                    break;
                case sim::Type::Bool: x = in.isTruthy() ? 1 : 0; break;
                case sim::Type::Real: x = in.asReal(); break;
                case sim::Type::Time: x = static_cast<double>(in.asInteger()); break;
                default: x = static_cast<double>(in.asInteger()); break;
            }
            return numberInto(x, t);
        }
    }
}

Written textToVariable(std::string_view text, const Target& t) {
    return toVariable(sim::Value::text(std::string(trimView(text))), t);
}

} // namespace hmi::roles
