// =============================================================================
//  app/SimulationPane.cpp - API > Simulation (lot API 7)
// -----------------------------------------------------------------------------
//  Ce que l'onglet montre : voir SimulationPane.hpp. Ici, comment il reste
//  rapide sur un projet de 63 983 cases :
//
//   * LES LIGNES SONT CELLES QU'ON VOIT, ET RIEN D'AUTRE. Les racines (les
//     variables globales, les locales des unites) se refont a chaque refresh() ;
//     une racine ne fabrique ses membres que si on la deplie (project/MemberTree,
//     un grand tableau par paquets de 100).
//   * LES VALEURS SE LISENT QUATRE FOIS PAR SECONDE AU PLUS, et seulement quand
//     un cycle a tourne (ou qu'on vient de forcer) : un onglet en pause ne coute
//     rien. Le modele de la table ne lit que rows_ et live_, jamais le moteur.
//   * « ECRITE PAR » se calcule une fois par refresh() : le code de MAST lu une
//     seule fois, dans l'ordre d'execution, qui garde pour chaque racine les
//     chemins ecrits (armoires[i].sorties.V3.cmd) ; une ligne prend le premier
//     qui la couvre - l'entree de MAST qui l'ecrit en premier dans le cycle.
// =============================================================================
#include "SimulationPane.hpp"

#include "ApiListKit.hpp"
#include "ApiPanes.hpp"
#include "MacroFormView.hpp"
#include "SimulationHost.hpp"
#include "hmi/HmiPanels.hpp"

#include "../domain/ExecutionOrder.hpp"
#include "../project/ApiChecks.hpp"
#include "../project/TypeUsage.hpp"
#include "../sim/Runtime.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace app {

namespace mt = project::members;
using ui::RowIndex;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kBody{14};
const gfx::FontId kState{20};

constexpr std::size_t  kNpos = static_cast<std::size_t>(-1);
constexpr double       kReadEvery = 0.25;       // s : les valeurs, quatre fois par seconde au plus
constexpr double       kFlash = 1.0;            // s : une valeur qui vient de changer s'eclaire
constexpr double       kChanging = 10.0;        // s : « Qui changent » - changee depuis moins que cela
constexpr double       kNever = -1.0e9;
constexpr std::size_t  kMaxWatched = 6;         // six courbes, c'est deja beaucoup a lire
constexpr std::int64_t kTrendMs = 30000;        // la courbe : les 30 dernieres secondes simulees
constexpr int          kSearchBudget = 6000;    // les noeuds qu'une recherche de membre visite
constexpr std::size_t  kSearchHits = 300;
constexpr int          kMenuCopy = 100;
constexpr int          kMenuWriter = 101;

// Les dossiers fixes (les genres de project::usage), puis un par unite.
enum FolderId : int { FDfb = 0, FStd, FDdt, FLocated, FOther };
// Les pastilles, dans l'ordre de la ligne des filtres.
enum ChipId : std::size_t { ChAll = 0, ChForced, ChChanging, ChLocated, ChWatched };
enum Column : std::size_t { CVar = 0, CValue, CType, CForce, CWriter, CCount };

const char* const kDot = " \xC2\xB7 ";               // " . "
const char* const kEllipsis = "\xE2\x80\xA6";        // ...
const char* const kDash = "\xE2\x80\x94";            // le tiret : pas de valeur

unsigned char uc(char c) noexcept { return static_cast<unsigned char>(c); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(uc(c)));
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Un chemin tape (un script, un collage) : sans la casse ni les blancs -
// "Grille[2, 5]" est la case Grille[2,5], le nom que la simulation connait.
std::string pathKey(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        if (!std::isspace(uc(c))) out += static_cast<char>(std::tolower(uc(c)));
    return out;
}

bool startsWith(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool endsWith(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

// Les milliers, separes d'une espace fine insecable - comme l'onglet Variables.
std::string grouped(std::string_view digits) {
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += digits[i];
    }
    return out;
}

std::string thousands(long long n) {
    const bool negative = n < 0;
    // -LLONG_MIN deborde : on passe par le non signe.
    const unsigned long long a = negative ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
    return (negative ? std::string("-") : std::string{}) + grouped(std::to_string(a));
}

std::string count(std::size_t n) { return thousands(static_cast<long long>(n)); }

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return count(n) + " " + std::string(n == 1 ? one : many);
}

// Quelques noms d'une liste, pour une phrase : "a, b, c..." (cinq au plus).
std::string someNames(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < 5; ++i) out += (i ? ", " : "") + names[i];
    if (names.size() > 5) out += "\xE2\x80\xA6";
    return out;
}

// Un nombre decimal "1234.5" -> "1 234,5".
std::string frenchDecimal(const std::string& c) {
    std::string head = c, tail;
    if (const auto dot = c.find('.'); dot != std::string::npos) {
        head = c.substr(0, dot);
        tail = c.substr(dot + 1);
    }
    std::string sign;
    if (!head.empty() && (head.front() == '-' || head.front() == '+')) {
        if (head.front() == '-') sign = "-";
        head.erase(0, 1);
    }
    return sign + grouped(head) + (tail.empty() ? std::string{} : "," + tail);
}

// Un REAL comme on le lit : 12,50 ; 18 094 ; 0,125 ; 1,500E+07.
std::string formatReal(double x) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x > 0.0 ? "+inf" : "-inf";
    const double a = std::fabs(x);
    char buf[64];
    if (a >= 1e15 || (a > 0.0 && a < 1e-4)) {
        std::snprintf(buf, sizeof buf, "%.3E", x);
        std::string s = buf;
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    }
    if (x == std::floor(x)) return thousands(static_cast<long long>(x));
    const int decimals = a >= 1.0 ? 2 : a >= 0.01 ? 3 : 5;
    std::snprintf(buf, sizeof buf, "%.*f", decimals, x);
    return frenchDecimal(buf);
}

// Une duree comme on l'ecrit en ST : T#1s500ms, T#2m, T#0ms.
std::string formatTime(std::int64_t ms) {
    if (ms == 0) return "T#0ms";
    std::string out = ms < 0 ? "-T#" : "T#";
    std::uint64_t v = ms < 0 ? 0ULL - static_cast<std::uint64_t>(ms) : static_cast<std::uint64_t>(ms);
    const std::pair<std::uint64_t, const char*> units[] = {
        {86400000ULL, "d"}, {3600000ULL, "h"}, {60000ULL, "m"}, {1000ULL, "s"}, {1ULL, "ms"}};
    for (const auto& [size, name] : units)
        if (v >= size) {
            out += std::to_string(v / size) + name;
            v %= size;
        }
    return out;
}

// Une valeur, dite comme on la lit : TRUE, 12,50, 18 094, T#1s500ms, 'texte'.
std::string formatValue(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool:    return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real:    return formatReal(v.asReal());
        case sim::Type::Time:    return formatTime(v.asInteger());
        case sim::Type::String:
        case sim::Type::Unknown: return v.display();
        default:                 return thousands(static_cast<long long>(v.asInteger()));
    }
}

// Un REAL au plus court qui se relit a l'identique (au plus `most` chiffres) :
// 0.1, 18094.123, 1e-05 - "%g" seul (six chiffres) perdait 18094.123 en 18094.1.
std::string exactReal(double x, int most) {
    char buf[64];
    for (int digits = 6; digits <= most; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, x);
        if (std::strtod(buf, nullptr) == x) break;
    }
    return buf;
}

// La valeur telle qu'on la retaperait : le champ du dialogue « Forcer » (neuf
// chiffres : la precision d'un REAL de l'automate, pas le bruit du double).
std::string literalOf(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool:   return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real:   return exactReal(v.asReal(), 9);
        case sim::Type::Time:   return formatTime(v.asInteger());
        case sim::Type::String: return "'" + v.asString() + "'";
        default:                return std::to_string(v.asInteger());
    }
}

// Ce qui dit qu'une case a change, sans garder sa valeur : ses bits.
std::uint64_t fingerprint(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Real: {
            const double d = v.asReal();
            std::uint64_t bits = 0;
            std::memcpy(&bits, &d, sizeof bits);
            return bits;
        }
        case sim::Type::String: return static_cast<std::uint64_t>(std::hash<std::string>{}(v.asString()));
        default:                return static_cast<std::uint64_t>(v.asInteger());
    }
}

// Le numero de ligne qu'une erreur de lecture porte dans son texte
// ("invalid argument: line 12: expected THEN") : le diagnostic, lui, dit 0.
std::uint32_t lineInMessage(std::string_view m) {
    for (auto at = m.find("line "); at != std::string_view::npos; at = m.find("line ", at + 5)) {
        if (at != 0 && m[at - 1] != ' ') continue;
        std::size_t i = at + 5, n = 0;
        std::uint32_t line = 0;
        while (i < m.size() && std::isdigit(uc(m[i])) && n < 9) {
            line = line * 10u + static_cast<std::uint32_t>(m[i] - '0');
            ++i;
            ++n;
        }
        if (n > 0 && i < m.size() && m[i] == ':') return line;
    }
    return 0;
}

// ---------------------------------------------------------------- forcer ----
// Les bornes d'un entier : on dit « hors des bornes » plutot que de laisser la
// valeur se replier sans rien dire (40000 dans un INT deviendrait -25536).
bool integerBounds(sim::Type t, long long& lo, long long& hi) {
    switch (t) {
        case sim::Type::Byte:  lo = 0; hi = 255; return true;
        case sim::Type::Word:
        case sim::Type::UInt:  lo = 0; hi = 65535; return true;
        case sim::Type::Int:   lo = -32768; hi = 32767; return true;
        case sim::Type::DInt:  lo = -2147483648LL; hi = 2147483647LL; return true;
        case sim::Type::DWord:
        case sim::Type::UDInt: lo = 0; hi = 4294967295LL; return true;
        default: return false;
    }
}

// Sans les separateurs qu'on tape ou qu'on copie : _, espaces, espaces fines.
std::string compact(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = uc(s[i]);
        if (c == '_' || std::isspace(c)) continue;
        if (c == 0xE2 && i + 2 < s.size() && uc(s[i + 1]) == 0x80 && uc(s[i + 2]) == 0xAF) { i += 2; continue; }
        if (c == 0xC2 && i + 1 < s.size() && uc(s[i + 1]) == 0xA0) { ++i; continue; }
        out += s[i];
    }
    return out;
}

// "T#1H2M3S4MS", "TIME#1.5S", "500" (des millisecondes) - en majuscules.
bool parseDuration(const std::string& up, std::int64_t& ms) {
    std::string t = up;
    if (const auto hash = t.find('#'); hash != std::string::npos) t = t.substr(hash + 1);
    bool negative = false;
    if (!t.empty() && t.front() == '-') {
        negative = true;
        t.erase(0, 1);
    }
    if (t.empty()) return false;
    double total = 0.0;
    bool any = false;
    std::size_t i = 0;
    while (i < t.size()) {
        std::size_t j = i;
        while (j < t.size() && (std::isdigit(uc(t[j])) || t[j] == '.' || t[j] == ',')) ++j;
        if (j == i) return false;
        std::string number = t.substr(i, j - i);
        std::replace(number.begin(), number.end(), ',', '.');
        char* end = nullptr;
        const double n = std::strtod(number.c_str(), &end);
        if (!end || *end != '\0') return false;
        std::size_t k = j;
        while (k < t.size() && std::isalpha(uc(t[k]))) ++k;
        const auto unit = t.substr(j, k - j);
        double factor = 0.0;
        if (unit.empty()) {
            if (any || k != t.size()) return false;
            factor = 1.0;                              // un nombre seul : des millisecondes
        } else if (unit == "D") factor = 86400000.0;
        else if (unit == "H") factor = 3600000.0;
        else if (unit == "M") factor = 60000.0;
        else if (unit == "S") factor = 1000.0;
        else if (unit == "MS") factor = 1.0;
        else return false;
        total += n * factor;
        any = true;
        i = k;
    }
    ms = static_cast<std::int64_t>(std::llround(negative ? -total : total));
    return any;
}

// Le texte tape pour forcer, lu DANS LE TYPE DE LA CASE : TRUE / FALSE pour un
// BOOL, 12,5 ou 12.5 pour un REAL, 16#FF, T#1s500ms... Faux : `why` dit pourquoi.
bool parseForce(const std::string& typed, sim::Type type, sim::Value& out, std::string& why) {
    const std::string raw = trim(typed);
    if (raw.empty()) {
        why = "rien n'est tap\xC3\xA9";
        return false;
    }
    if (type == sim::Type::String || (type == sim::Type::Unknown && raw.front() == '\'')) {
        std::string s = raw;
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
        out = sim::Value::text(s);
        return true;
    }
    std::string t = compact(raw);
    std::string up = upper(t);
    // Un prefixe de type (INT#5, REAL#1.5) n'apprend rien ; une base (16#FF) ou
    // une duree (T#5S), si.
    if (const auto hash = up.find('#'); hash != std::string::npos) {
        const auto head = up.substr(0, hash);
        if (head != "16" && head != "8" && head != "2" && head != "T" && head != "TIME") {
            t = t.substr(hash + 1);
            up = up.substr(hash + 1);
        }
    }
    const bool yes = up == "TRUE" || up == "VRAI" || up == "ON";
    const bool no = up == "FALSE" || up == "FAUX" || up == "OFF";
    if (type == sim::Type::Bool) {
        if (yes || up == "1") { out = sim::Value::boolean(true); return true; }
        if (no || up == "0") { out = sim::Value::boolean(false); return true; }
        why = "un BOOL se force \xC3\xA0 TRUE ou FALSE";
        return false;
    }
    if (type == sim::Type::Unknown) {
        if (yes) { out = sim::Value::boolean(true); return true; }
        if (no) { out = sim::Value::boolean(false); return true; }
    }
    if (type == sim::Type::Time || startsWith(up, "T#") || startsWith(up, "TIME#")) {
        std::int64_t ms = 0;
        if (!parseDuration(up, ms)) {
            why = "une dur\xC3\xA9" "e s'\xC3\xA9" "crit T#500ms, T#1s500ms, T#2m (ou un nombre de millisecondes)";
            return false;
        }
        if (ms < 0 || ms > 4294967295LL) {
            why = "hors des bornes d'un TIME";
            return false;
        }
        if (type == sim::Type::Time || type == sim::Type::Unknown) out = sim::Value::time(ms);
        else if (type == sim::Type::Real) out = sim::Value::real(static_cast<double>(ms));
        else out = sim::Value::integer(type, ms);
        return true;
    }
    // Une base : 16#FF, 2#1010, 8#17 - le motif de bits, replie a la largeur du type.
    {
        std::string digits = up;
        bool negative = false;
        if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) {
            negative = digits.front() == '-';
            digits.erase(0, 1);
        }
        if (const auto hash = digits.find('#'); hash != std::string::npos) {
            const int base = std::atoi(digits.substr(0, hash).c_str());
            digits = digits.substr(hash + 1);
            char* end = nullptr;
            const unsigned long long n = std::strtoull(digits.c_str(), &end, base);
            if ((base != 2 && base != 8 && base != 16) || digits.empty() || !end || *end != '\0') {
                why = "\xC2\xAB " + raw + " \xC2\xBB ne se lit pas (16#FF, 2#1010, 8#17)";
                return false;
            }
            if (type == sim::Type::Real) {
                out = sim::Value::real(negative ? -static_cast<double>(n) : static_cast<double>(n));
                return true;
            }
            const auto target = type == sim::Type::Unknown ? sim::Type::DInt : type;
            const int bits = sim::bitWidth(target);
            const unsigned long long max = bits >= 64 ? ~0ULL : (1ULL << bits) - 1ULL;
            if (!sim::isInteger(target) || n > max) {
                why = "trop grand pour un " + std::string(sim::toString(target));
                return false;
            }
            const auto value = static_cast<long long>(n);
            out = sim::Value::integer(target, negative ? -value : value);
            return true;
        }
    }
    // Un nombre : la virgule francaise comme le point.
    std::string number = t;
    std::replace(number.begin(), number.end(), ',', '.');
    char* end = nullptr;
    const double d = std::strtod(number.c_str(), &end);
    if (number.empty() || !end || *end != '\0' || !std::isfinite(d)) {
        why = "\xC2\xAB " + raw + " \xC2\xBB ne se lit pas comme un nombre";
        return false;
    }
    if (type == sim::Type::Real || (type == sim::Type::Unknown && number.find_first_of(".eE") != std::string::npos)) {
        out = sim::Value::real(d);
        return true;
    }
    const auto target = type == sim::Type::Unknown ? sim::Type::DInt : type;
    if (!sim::isInteger(target)) {
        why = "ce type ne se force pas ici";
        return false;
    }
    const double r = std::round(d);
    long long lo = 0, hi = 0;
    if (integerBounds(target, lo, hi) && (r < static_cast<double>(lo) || r > static_cast<double>(hi))) {
        why = "hors des bornes d'un " + std::string(sim::toString(target)) + " (" + thousands(lo) + " \xC3\xA0 " + thousands(hi) + ")";
        return false;
    }
    out = sim::Value::integer(target, static_cast<std::int64_t>(r));
    return true;
}

// ------------------------------------------------------------- « ecrite par » ----
bool identStart(char c) noexcept { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(uc(c)) || c == '_'; }

bool sameWord(std::string_view w, const char* keyword) noexcept {
    std::size_t i = 0;
    for (; keyword[i] != '\0'; ++i)
        if (i >= w.size() || std::tolower(uc(w[i])) != keyword[i]) return false;
    return i == w.size();
}

bool isKeyword(std::string_view w) noexcept {
    static const char* const kWords[] = {"if", "then", "else", "elsif", "end_if", "case", "of", "end_case", "for", "to",
                                         "by", "do", "end_for", "while", "end_while", "repeat", "until", "end_repeat",
                                         "return", "exit", "and", "or", "xor", "not", "mod", "true", "false"};
    for (const auto* k : kWords)
        if (sameWord(w, k)) return true;
    return false;
}

// Le code sans commentaires ni chaines : des blancs a leur place, les fins de
// ligne gardees (les numeros de ligne aussi).
std::string codeOnly(std::string_view code) {
    std::string out(code);
    std::size_t i = 0;
    const auto blank = [&](std::size_t from, std::size_t to) {
        for (auto k = from; k < to && k < out.size(); ++k)
            if (out[k] != '\n') out[k] = ' ';
    };
    while (i < out.size()) {
        if (out[i] == '(' && i + 1 < out.size() && out[i + 1] == '*') {
            const auto end = out.find("*)", i + 2);
            const auto stop = end == std::string::npos ? out.size() : end + 2;
            blank(i, stop);
            i = stop;
            continue;
        }
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            const auto end = out.find('\n', i);
            const auto stop = end == std::string::npos ? out.size() : end;
            blank(i, stop);
            i = stop;
            continue;
        }
        if (out[i] == '\'' || out[i] == '"') {
            const char q = out[i];
            auto k = i + 1;
            while (k < out.size() && out[k] != q && out[k] != '\n') ++k;
            const auto stop = std::min(out.size(), k + 1);
            blank(i, stop);
            i = stop;
            continue;
        }
        ++i;
    }
    return out;
}

// Un chemin a partir de `at` : un identifiant, puis .membre et [indices]. Rend
// sa fin ; `out` : le chemin sans blancs (vide : pas de chemin ici). Le bit
// d'un mot (Mot.3) ecrit le mot : il est retire.
std::size_t readPath(std::string_view s, std::size_t at, std::string& out) {
    out.clear();
    if (at >= s.size() || !identStart(s[at])) return at;
    std::size_t i = at;
    while (i < s.size() && identChar(s[i])) out += s[i++];
    for (;;) {
        if (i + 1 < s.size() && s[i] == '.' && identChar(s[i + 1])) {
            std::string member;
            ++i;
            while (i < s.size() && identChar(s[i])) member += s[i++];
            const bool bit = std::all_of(member.begin(), member.end(), [](char c) { return std::isdigit(uc(c)) != 0; });
            if (!bit) out += "." + member;
            continue;
        }
        if (i < s.size() && s[i] == '[') {
            int depth = 0;
            std::size_t j = i;
            for (; j < s.size(); ++j) {
                if (s[j] == '[') ++depth;
                else if (s[j] == ']' && --depth == 0) break;
            }
            if (j >= s.size()) break;
            for (std::size_t q = i; q <= j; ++q)
                if (!std::isspace(uc(s[q]))) out += s[q];
            i = j + 1;
            continue;
        }
        break;
    }
    return i;
}

// Chaque ecriture d'un corps de section : le chemin a gauche d'un ':=' en tete
// d'instruction, l'instance d'un appel (Tempo(...) ecrit ses broches), et les
// sorties liees (Q => y). La meme lecture simple que project/ApiChecks : les
// instructions se coupent aux ';' et apres THEN, DO, ELSE, REPEAT, OF.
template <typename Fn>
void forEachWrite(std::string_view body, Fn&& fn) {
    const std::string code = codeOnly(body);
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i] == '\n') starts.push_back(i + 1);
    const auto lineOf = [&](std::size_t pos) {
        return static_cast<std::uint32_t>(std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin());
    };
    const auto statement = [&](std::size_t from, std::size_t to) {
        const std::string_view st(code.data() + from, to - from);
        std::size_t p = 0;
        const auto skip = [&](std::size_t& k) {
            while (k < st.size() && std::isspace(uc(st[k]))) ++k;
        };
        skip(p);
        // Un libelle de CASE (« 1, 2: », « 10..20: », « IDLE: ») avant l'instruction.
        {
            std::size_t k = p;
            while (k < st.size() && (identChar(st[k]) || st[k] == ',' || st[k] == '.' || st[k] == '-' || st[k] == '#'
                                     || std::isspace(uc(st[k]))))
                ++k;
            if (k > p && k < st.size() && st[k] == ':' && (k + 1 >= st.size() || st[k + 1] != '=')) {
                p = k + 1;
                skip(p);
            }
        }
        std::string path;
        const auto end = readPath(st, p, path);
        if (!path.empty() && !isKeyword(std::string_view(path).substr(0, path.find_first_of(".[")))) {
            std::size_t k = end;
            skip(k);
            if (k + 1 < st.size() && st[k] == ':' && st[k + 1] == '=') fn(path, lineOf(from + p));
            else if (k < st.size() && st[k] == '(') fn(path, lineOf(from + p));   // un appel : toute l'instance
        }
        for (auto k = st.find("=>"); k != std::string_view::npos; k = st.find("=>", k + 2)) {
            std::size_t q = k + 2;
            skip(q);
            std::string target;
            (void)readPath(st, q, target);
            if (!target.empty() && !isKeyword(std::string_view(target).substr(0, target.find_first_of(".["))))
                fn(target, lineOf(from + q));
        }
    };
    std::size_t start = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char ch = code[i];
        if (ch == ';') {
            statement(start, i);
            start = i + 1;
            continue;
        }
        if (identStart(ch) && (i == 0 || !identChar(code[i - 1]))) {
            std::size_t e = i;
            while (e < code.size() && identChar(code[e])) ++e;
            const std::string_view w(code.data() + i, e - i);
            if (sameWord(w, "then") || sameWord(w, "do") || sameWord(w, "else") || sameWord(w, "repeat") || sameWord(w, "of")) {
                statement(start, e);
                start = e;
            }
            i = e - 1;
        }
    }
    if (start < code.size()) statement(start, code.size());
}

std::size_t closingBracket(std::string_view s, std::size_t open) noexcept {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '[') ++depth;
        else if (s[i] == ']' && --depth == 0) return i;
    }
    return std::string_view::npos;
}

bool literalIndex(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (const char c : s)
        if (!(std::isdigit(uc(c)) || c == ',' || c == '-' || c == '+')) return false;
    return true;
}

// Le chemin ecrit couvre-t-il ce chemin-ci ? Tous deux en minuscules. Un indice
// qui est une expression (armoires[i]) couvre tous les indices ; un chemin plus
// court couvre ses membres (Tempon_OUT := Tempon_IN ecrit tout le tableau).
bool covers(std::string_view pattern, std::string_view path) {
    std::size_t i = 0, j = 0;
    while (i < pattern.size()) {
        if (j >= path.size()) return false;
        if (pattern[i] == '[' && path[j] == '[') {
            const auto pe = closingBracket(pattern, i), qe = closingBracket(path, j);
            if (pe == std::string_view::npos || qe == std::string_view::npos) return false;
            const auto index = pattern.substr(i + 1, pe - i - 1);
            if (literalIndex(index) && index != path.substr(j + 1, qe - j - 1)) return false;
            i = pe + 1;
            j = qe + 1;
            continue;
        }
        if (pattern[i] != path[j]) return false;
        ++i;
        ++j;
    }
    return j == path.size() || path[j] == '.' || path[j] == '[';
}

// ------------------------------------------------------------- les membres ----
// Ce que la colonne Valeur dit d'une structure : {...} ; d'un tableau : « 16 el. ».
std::string aggregateText(const mt::Node& n) {
    if (!n.real) return {};
    const auto shape = mt::parseArray(n.type);
    if (shape.valid()) return thousands(static_cast<long long>(shape.count())) + " \xC3\xA9l.";
    return std::string("{") + kEllipsis + "}";
}

// Les membres d'une variable dont le chemin contient `needle` (au-dela du nom de
// la racine). On ne descend pas sous un membre trouve, ni dans un tableau
// d'elements simples (ses noms ne sont que des indices, sauf si l'on cherche un
// indice) ; `budget` borne les noeuds visites.
void searchMembers(const domain::Project& p, const mt::Node& n, const std::string& needle, std::size_t rootLength,
                   int& budget, std::vector<mt::Node>& out, std::size_t limit) {
    if (budget <= 0 || out.size() >= limit) return;
    for (auto& c : mt::children(p, n)) {
        if (--budget <= 0 || out.size() >= limit) return;
        if (!c.real) {
            searchMembers(p, c, needle, rootLength, budget, out, limit);
            continue;
        }
        const auto low = lower(c.path);
        if (const auto at = low.find(needle); at != std::string::npos && at + needle.size() > rootLength) {
            out.push_back(std::move(c));
            continue;
        }
        if (!mt::hasChildren(p, c)) continue;
        if (needle.find('[') == std::string::npos) {
            const auto shape = mt::parseArray(c.type);
            if (shape.valid() && mt::isElementary(shape.element)) continue;
        }
        searchMembers(p, c, needle, rootLength, budget, out, limit);
    }
}

// Le nom court d'une courbe : le dernier segment, avec celui d'avant quand il
// est court (PT1.mes, V3.cmd, tempon[3]).
std::string shortName(const std::string& path) {
    const auto cut = path.find_last_of(".[");
    if (cut == std::string::npos || cut == 0) return path;
    const auto last = path.substr(path[cut] == '.' ? cut + 1 : cut);
    if (last.size() > 6) return last;
    const auto before = path.find_last_of(".[", cut - 1);
    const auto from = before == std::string::npos ? std::size_t{0} : (path[before] == '.' ? before + 1 : before);
    return path.substr(from);
}

// Le temps simule : 26,2 s ; 12 min 05 s ; 1 h 02 min.
std::string simulatedTime(std::int64_t ms) {
    char buf[64];
    if (ms < 600000) {
        std::snprintf(buf, sizeof buf, "%.1f s", static_cast<double>(ms) / 1000.0);
        return frenchDecimal(buf);
    }
    const auto s = ms / 1000;
    if (s < 3600) std::snprintf(buf, sizeof buf, "%lld min %02lld s", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    else std::snprintf(buf, sizeof buf, "%lld h %02lld min", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    return buf;
}

// Le temps de calcul d'un cycle : 3,1 ms ; 0,35 ms.
std::string scanTime(std::int64_t micros) {
    char buf[64];
    const double ms = static_cast<double>(micros) / 1000.0;
    std::snprintf(buf, sizeof buf, ms < 1.0 ? "%.2f ms" : "%.1f ms", ms);
    return frenchDecimal(buf);
}

// Un pas « rond » pour l'axe des valeurs : 1, 2 ou 5 fois une puissance de dix.
double niceStep(double range, int target) {
    if (!(range > 0.0)) return 1.0;
    const double raw = range / static_cast<double>(target);
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / magnitude;
    return (f <= 1.0 ? 1.0 : f <= 2.0 ? 2.0 : f <= 5.0 ? 5.0 : 10.0) * magnitude;
}

std::string tickLabel(double v, double step) {
    char buf[48];
    const int decimals = step >= 1.0 ? 0 : std::min(6, static_cast<int>(std::ceil(-std::log10(step))));
    std::snprintf(buf, sizeof buf, "%.*f", decimals, std::fabs(v) < step * 1e-6 ? 0.0 : v);
    return frenchDecimal(buf);
}

void drawBold(gfx::IRenderer& r, gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color c) {
    r.drawText(at, s, f, c);
    r.drawText({at.x + 0.7f, at.y}, s, f, c);
}

// Un calque transparent pose sur une table : il prend les clics qu'il sait
// traiter (le clic droit de la table des variables, la croix d'une ligne
// forcee, « Voir ») et laisse passer tout le reste a la table dessous. La table
// garde ainsi son clic droit a elle (Copier...) sur ses titres.
class ClickCatcher final : public ui::Widget {
public:
    ClickCatcher(std::string id, std::function<bool(const ui::MouseDown&)> onDown)
        : ui::Widget(std::move(id)), onDown_(std::move(onDown)) {}
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos) && onDown_ && onDown_(*d))
            return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }
private:
    std::function<bool(const ui::MouseDown&)> onDown_;
};

const gfx::Color kTraces[] = {gfx::Color::rgb(0x5B9BF0), gfx::Color::rgb(0x4CC38A), gfx::Color::rgb(0xE8B04A),
                              gfx::Color::rgb(0x45C4D8), gfx::Color::rgb(0xD07FC6), gfx::Color::rgb(0xE8835A)};

} // namespace

// ============================================================== les modeles ====
class SimulationPane::Model final : public ui::ITableModel {
public:
    explicit Model(SimulationPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case CVar:   return "Variable";
            case CValue: return "Valeur";
            case CType:  return "Type";
            case CForce: return "For\xC3\xA7" "age";
            default:     return "\xC3\x89" "crite par";
        }
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.folder) return c == CVar ? row.label : std::string{};
        const auto* l = r < pane_.live_.size() ? &pane_.live_[r] : nullptr;
        switch (c) {
            case CVar:   return row.label;
            case CValue: return l ? l->text : std::string{};
            case CType:  return row.node.real ? row.node.type : mt::groupType(row.node);
            case CForce: return l && l->forced ? l->text : std::string{};
            default:     return row.writer;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        const auto* l = r < pane_.live_.size() ? &pane_.live_[r] : nullptr;
        if (row.folder) {
            if (c == CVar) {
                s.bold = true;
                s.expander = row.open ? 1 : 0;
                s.icon = row.open ? ui::Icon::FolderOpen : ui::Icon::Folder;
                s.iconTone = ui::Tone::Muted;
                s.badge = count(row.count);
            }
            return s;
        }
        switch (c) {
            case CVar:
                s.indent = 16.f * static_cast<float>(row.depth);
                s.expander = row.agg ? (row.open ? 1 : 0) : -1;
                if (row.array) s.icon = ui::Icon::Layers;
                else if (row.agg) s.icon = ui::Icon::DerivedType;
                else s.icon = row.address.empty() ? ui::Icon::Variable : ui::Icon::LocatedVariable;
                s.iconTone = ui::Tone::Muted;
                if (!row.node.real) s.fgTone = ui::Tone::Muted;
                break;
            case CValue:
                s.monospace = true;
                if (!l) break;
                if (row.agg || !row.node.real || !l->ok) s.fgTone = ui::Tone::Muted;
                else if (l->forced) {
                    s.fgTone = ui::Tone::Warning;
                    s.bold = true;
                } else if (l->isBool) s.fgTone = l->truthy ? ui::Tone::Ok : ui::Tone::Muted;
                // La valeur vient de changer : elle s'eclaire (le bleu de la
                // maquette, lisible sur un theme clair comme sur un sombre), puis
                // s'eteint en une seconde.
                if (l->ok && pane_.now_ - l->changedAt < kFlash) {
                    const double fade = 1.0 - (pane_.now_ - l->changedAt) / kFlash;
                    s.bg = gfx::Color{70, 140, 230, static_cast<std::uint8_t>(std::clamp(24.0 + 76.0 * fade, 0.0, 255.0))};
                }
                break;
            case CType:
                s.fgTone = ui::Tone::Muted;
                break;
            case CForce:
                if (l && l->forced) {
                    s.icon = ui::Icon::Lock;
                    s.iconTone = ui::Tone::Warning;
                    s.fgTone = ui::Tone::Warning;
                    s.monospace = true;
                    s.bold = true;
                }
                break;
            default:
                s.fgTone = ui::Tone::Muted;
                break;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.folder)
            return row.label + kDot + plural(row.count, "variable", "variables") + kDot + "la fl\xC3\xA8" "che l'ouvre";
        if (!row.node.real)
            return row.node.path + row.node.label + kDot + mt::groupType(row.node) + kDot + "la fl\xC3\xA8" "che l'ouvre";
        std::string tip = row.node.path + kDot + (row.node.type.empty() ? std::string("type inconnu") : row.node.type);
        // Lot recherche : son commentaire (la recherche le lit aussi).
        if (row.root >= 0 && static_cast<std::size_t>(row.root) < pane_.roots_.size() && row.node.path == pane_.roots_[static_cast<std::size_t>(row.root)].name
            && !pane_.roots_[static_cast<std::size_t>(row.root)].comment.empty())
            tip += kDot + pane_.roots_[static_cast<std::size_t>(row.root)].comment;
        if (const auto* l = r < pane_.live_.size() ? &pane_.live_[r] : nullptr; l && !l->ok && !row.agg && pane_.runtime())
            tip += kDot + std::string("la simulation ne la conna\xC3\xAEt pas (un tableau refus\xC3\xA9, un bloc sans corps en ST)");
        if (!row.address.empty()) tip += kDot + std::string("situ\xC3\xA9" "e en ") + row.address;
        if (!row.writer.empty()) tip += kDot + std::string("\xC3\xA9" "crite par ") + row.writer + " (" + frenchPlace(row.section, row.line) + ")";
        else if (!row.agg) tip += kDot + std::string("aucune entr\xC3\xA9" "e de MAST ne l'\xC3\xA9" "crit");
        tip += row.agg ? kDot + std::string("la fl\xC3\xA8" "che d\xC3\xA9plie ses membres")
                       : kDot + std::string("double-clic : la forcer");
        return tip;
    }
private:
    SimulationPane& pane_;
};

class SimulationPane::ForcedModel final : public ui::ITableModel {
public:
    explicit ForcedModel(SimulationPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return std::max<std::size_t>(1, pane_.forcedNames_.size()); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? std::string("Variable forc\xC3\xA9" "e") : c == 1 ? std::string("Valeur") : std::string{};
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (pane_.forcedNames_.empty())
            return c == 0 ? std::string("Rien n'est forc\xC3\xA9 : le programme d\xC3\xA9" "cide de tout.") : std::string{};
        if (r >= pane_.forcedNames_.size()) return {};
        if (c == 0) return pane_.forcedNames_[r];
        if (c == 1) return r < pane_.forcedValues_.size() ? pane_.forcedValues_[r] : std::string{};
        return "\xC3\x97";   // la croix : relacher
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (pane_.forcedNames_.empty()) {
            s.fgTone = ui::Tone::Muted;
            s.spanRow = c == 0;
            return s;
        }
        if (r >= pane_.forcedNames_.size()) return s;
        switch (c) {
            case 0:
                s.icon = ui::Icon::Lock;
                s.iconTone = ui::Tone::Warning;
                s.monospace = true;
                break;
            case 1:
                s.monospace = true;
                s.bold = true;
                s.fgTone = ui::Tone::Warning;
                break;
            default:
                s.fgTone = ui::Tone::Muted;
                s.bold = true;
                break;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.forcedNames_.size()) return "Double-clic sur une valeur de la table : la forcer.";
        return pane_.forcedNames_[r] + std::string(" est forc\xC3\xA9" "e") + kDot + "clic : la montrer dans la table"
             + kDot + "double-clic : une autre valeur" + kDot + "\xC3\x97 : la rel\xC3\xA2" "cher";
    }
private:
    SimulationPane& pane_;
};

// Deux lignes par element : le titre (son icone, « Voir ») et le detail, en retrait.
class SimulationPane::DiagModel final : public ui::ITableModel {
public:
    explicit DiagModel(SimulationPane& pane) : pane_(pane) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.diagItems_.size() * 2; }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? std::string("Ce que dit le simulateur") : std::string{};
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        const std::size_t i = r / 2;
        if (i >= pane_.diagItems_.size()) return {};
        const auto& it = pane_.diagItems_[i];
        if (r % 2 == 0) return c == 0 ? it.title : (it.link ? std::string("Voir") : std::string{});
        return c == 0 ? it.detail : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        const std::size_t i = r / 2;
        if (i >= pane_.diagItems_.size()) return s;
        const auto& it = pane_.diagItems_[i];
        if (r % 2 == 0) {
            if (c == 0) {
                s.icon = it.tone == ui::Tone::Error ? ui::Icon::Error
                       : it.tone == ui::Tone::Ok    ? ui::Icon::Ok
                       : it.tone == ui::Tone::Info  ? ui::Icon::Info
                                                    : ui::Icon::Warning;
                s.iconTone = it.tone;
                if (it.tone == ui::Tone::Ok || it.tone == ui::Tone::Error) s.fgTone = it.tone;
            } else {
                s.fgTone = ui::Tone::Accent;
            }
        } else if (c == 0) {
            // Sous le titre, apres son icone (le retrait et la place de la fleche).
            s.indent = 5.f;
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        const std::size_t i = r / 2;
        if (i >= pane_.diagItems_.size()) return {};
        const auto& it = pane_.diagItems_[i];
        return it.title + (it.detail.empty() ? std::string{} : kDot + it.detail)
             + (it.link ? kDot + std::string("Voir : ouvrir la section \xC3\xA0 la ligne") : std::string{});
    }
private:
    SimulationPane& pane_;
};

// ============================================================== le bandeau ====
//  L'etat en grand, sur toute la largeur : un voyant, le mot, ce qui compte (le
//  cycle, le temps simule, le temps de calcul, les entrees de MAST), la vitesse.
//  Une halte le rend rouge : pourquoi, ou (« Aller a la ligne »), et, si le
//  bloc en cause a une version plus recente en bibliotheque, le dire.
class SimulationPane::Band final : public ui::Widget {
public:
    explicit Band(SimulationPane& pane) : ui::Widget(pane.id() + ".etat"), pane_(pane) {
        speed_ = &static_cast<macroui::Segmented&>(addChild(std::make_unique<macroui::Segmented>(pane.id() + ".vitesse")));
        speed_->setOptions({"\xC3\x97" "1", "\xC3\x97" "10", "au plus vite"});
        speed_->setSelected(0);
        speed_->setTooltip("La vitesse : le temps r\xC3\xA9" "el, dix fois plus vite, ou autant de cycles que l'image en laisse");
        links_ += speed_->selectionChanged->connect([this](int i) { pane_.setSpeed(i == 0 ? 1 : i == 1 ? 10 : 0); });
    }
    [[nodiscard]] macroui::Segmented& speed() noexcept { return *speed_; }
    // Pour les scripts : ou est un lien du bandeau ("ligne", "continuer",
    // "bibliotheque", "arreter") apres un premier dessin ; vide : pas montre.
    [[nodiscard]] gfx::Rect linkRect(std::string_view key) const {
        for (const auto& [k, r] : hits_)
            if (k == key) return r;
        return {};
    }
protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(speed_->preferredWidth(), 260.f);
        speed_->setBounds({b.right() - 14.f - w, b.y + (b.h - 28.f) * 0.5f, w, 28.f});
    }
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            std::string h;
            for (const auto& [k, r] : hits_)
                if (r.contains(m->pos)) h = k;
            if (h != hover_) {
                hover_ = h;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left)
            for (const auto& [k, r] : hits_)
                if (r.contains(d->pos)) {
                    const std::string key = k;     // bandLink peut redessiner (hits_ change)
                    pane_.bandLink(key);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }
private:
    SimulationPane&     pane_;
    macroui::Segmented* speed_{nullptr};
    mutable std::vector<std::pair<std::string, gfx::Rect>> hits_;
    std::string         hover_;
    core::ConnectionScope links_;
};

void SimulationPane::Band::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto b = bounds();
    hits_.clear();
    const auto info = pane_.bandInfo();
    // Lot API 8 (le moteur) : les couleurs de la maquette - vert en marche, bleu
    // en pause, ORANGE sur un point d'arret, rouge en halte (pas prete : orange).
    const ui::Tone tone = info.kind == 1 ? ui::Tone::Ok
                        : info.kind == 2 ? (info.breakpoint ? ui::Tone::Warning : ui::Tone::Info)
                        : info.kind == 4 ? ui::Tone::Warning
                        : info.kind == 3 ? ui::Tone::Error
                                         : ui::Tone::Muted;
    const auto col = th.onSurface(th.tone(tone, c.textMuted));
    const bool halted = info.kind == 3;
    r.fillRect(b, halted ? c.error.withAlpha(th.isDark() ? 46 : 30) : c.panelBg);
    if (halted) r.fillRect({b.x, b.y, 4.f, b.h}, c.error);
    r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, halted ? c.error : c.border);

    // Le voyant : un halo quand ca tourne.
    const float cy = b.y + b.h * 0.5f;
    float x = b.x + 18.f;
    if (info.kind == 1) r.fillRoundedRect({x - 4.f, cy - 11.f, 22.f, 22.f}, col.withAlpha(60), 11.f);
    r.fillRoundedRect({x, cy - 7.f, 14.f, 14.f}, col, 7.f);
    x += 28.f;
    const float wordH = r.lineHeight(kState);
    drawBold(r, {x, cy - wordH * 0.5f}, info.word, kState, col);
    x += r.measure(info.word, kState).width + 26.f;

    // A droite : la vitesse, ou (halte) le bouton Arreter.
    float right = b.right() - 14.f;
    const std::string speedLabel = "Vitesse";
    if (halted) {
        const std::string label = "Arr\xC3\xAAter";
        const float w = r.measure(label, kSmall).width + 26.f;
        const gfx::Rect button{right - w, cy - 14.f, w, 28.f};
        const bool hot = hover_ == "arreter";
        r.fillRoundedRect(button, hot ? c.borderStrong : c.border, 5.f);
        r.fillRoundedRect({button.x + 1.f, button.y + 1.f, button.w - 2.f, button.h - 2.f}, hot ? c.rowAltBg : c.panelBg, 4.f);
        r.drawText({button.x + 13.f, cy - r.lineHeight(kSmall) * 0.5f}, label, kSmall, c.text);
        hits_.emplace_back("arreter", button);
        right = button.x - 16.f;
    } else {
        const float lw = r.measure(speedLabel, kSmall).width;
        const float sx = speed_->bounds().x;
        r.drawText({sx - 10.f - lw, cy - r.lineHeight(kSmall) * 0.5f}, speedLabel, kSmall, c.textMuted);
        right = sx - 10.f - lw - 18.f;
    }

    r.pushClip({b.x, b.y, std::max(0.f, right - b.x), b.h});
    const auto link = [&](float& at, float y, const std::string& label, const std::string& key) {
        const auto lc = th.onSurface(c.accent);
        const float w = r.measure(label, kSmall).width;
        r.drawText({at, y}, label, kSmall, lc);
        if (hover_ == key) r.fillRect({at, y + r.lineHeight(kSmall) - 1.f, w, 1.f}, lc);
        hits_.emplace_back(key, gfx::Rect{at - 2.f, y - 2.f, w + 4.f, r.lineHeight(kSmall) + 4.f});
        at += w + 14.f;
    };
    if (halted || info.kind == 4) {
        // Deux lignes : pourquoi, puis ou et quoi faire.
        const float y1 = b.y + 7.f, y2 = b.y + 27.f;
        std::string first = info.kind == 4 ? "La simulation ne se pr\xC3\xA9pare pas : " + info.reason
                                           : "au cycle " + count(static_cast<std::size_t>(info.cycle)) + " : " + info.reason;
        r.drawText({x, y1}, first, kBody, c.text);
        float at = x;
        if (halted) {
            if (!info.place.empty()) {
                r.drawText({at, y2}, info.place, kSmall, c.textMuted);
                at += r.measure(info.place, kSmall).width + 12.f;
                link(at, y2, "Aller \xC3\xA0 la ligne", "ligne");
            }
            if (info.unknownFunction) link(at, y2, "Relancer en l'ignorant (elle rend 0)", "continuer");
            if (!info.newer.empty()) {
                const std::string lib = info.block + " " + info.newer + " en biblioth\xC3\xA8que corrige peut-\xC3\xAAtre ce probl\xC3\xA8me";
                r.drawText({at, y2}, lib, kSmall, th.onSurface(c.warning));
                at += r.measure(lib, kSmall).width + 12.f;
                link(at, y2, "Mettre \xC3\xA0 jour", "bibliotheque");
            }
        } else {
            r.drawText({at, y2}, "Simuler r\xC3\xA9" "essaie ; le projet doit avoir des sections en ST dans MAST.", kSmall, c.textMuted);
        }
    } else {
        // Une ligne : les faits, la valeur en clair, le reste en gris.
        const float ty = cy - r.lineHeight(kSmall) * 0.5f;
        const auto part = [&](const std::string& s, bool strong) {
            if (strong) drawBold(r, {x, ty}, s, kSmall, c.text);
            else r.drawText({x, ty}, s, kSmall, c.textMuted);
            x += r.measure(s, kSmall).width;
        };
        if (!info.attached) {
            part("cycle ", false);
            part("0", true);
            x += 20.f;
            part("\xC2\xAB Simuler \xC2\xBB la pr\xC3\xA9pare et la lance : le programme de MAST tourne ici, cycle apr\xC3\xA8s cycle.", false);
        } else {
            // Lot API 8 (le moteur) : sur un point d'arret, OU d'abord (la maquette).
            if (info.breakpoint && !info.place.empty()) {
                part(info.place, true);
                x += 20.f;
            }
            part("cycle ", false);
            part(count(static_cast<std::size_t>(info.cycle)), true);
            x += 20.f;
            part("temps simul\xC3\xA9 ", false);
            part(simulatedTime(info.clockMs), true);
            x += 20.f;
            part("MAST ", false);
            part(std::to_string(info.intervalMs) + " ms", true);
            if (info.cycle > 0) {
                part(" \xC2\xB7 un cycle calcul\xC3\xA9 en ", false);
                part(scanTime(info.scanMicros), true);
            }
            x += 20.f;
            // Les entrees de MAST : ses sections, et ses unites (l'infobulle les compte).
            part(count(pane_.entries_), true);
            part(pane_.entries_ == 1 ? " entr\xC3\xA9" "e \xC2\xB7 " : " entr\xC3\xA9" "es \xC2\xB7 ", false);
            part(count(pane_.taskSections_), true);
            part(pane_.taskSections_ == 1 ? " section" : " sections", false);
            // ---- Lot API 8 : le moteur ---- en pause sur un point d'arret, ou le
            // programme vient de changer en ligne (en pause, ou depuis moins de
            // 250 cycles ; un echec : tant qu'il dure) : la phrase du moteur.
            if (const auto* h = pane_.host()) {
                const auto& change = h->lastOnlineChange();
                if (h->state() == SimulationHost::State::Paused && h->lastBreakHit()) {
                    x += 20.f;
                    part("point d'arr\xC3\xAAt ", false);
                    part(h->lastBreakHit()->section + ", ligne " + std::to_string(h->lastBreakHit()->line), true);
                } else if (change && (change->failed || h->state() == SimulationHost::State::Paused
                                      || change->atScan + 250 >= static_cast<std::uint64_t>(info.cycle))) {
                    x += 20.f;
                    part(change->summary, change->failed);
                }
            }
        }
    }
    r.popClip();

    // L'infobulle : ce que les nombres veulent dire (le bandeau n'a la place que
    // pour les nombres).
    std::string tip;
    if (halted) {
        tip = "La simulation s'est arr\xC3\xAAt\xC3\xA9" "e sur une erreur : Aller \xC3\xA0 la ligne l'ouvre ; Arr\xC3\xAAter repart des valeurs initiales.";
    } else if (info.kind == 4) {
        tip = "La simulation ne se pr\xC3\xA9pare pas : " + info.reason;
    } else {
        tip = "Chaque cycle avance le temps simul\xC3\xA9 de " + std::to_string(info.intervalMs) + " ms (la p\xC3\xA9riode de MAST)";
        if (info.cycle > 0) {
            tip += " ; le dernier s'est calcul\xC3\xA9 en " + scanTime(info.scanMicros);
            if (const auto* h = pane_.host(); h && h->lastScanStatements() > 0)
                tip += " (" + plural(h->lastScanStatements(), "instruction", "instructions") + ")";
        }
        tip += ". MAST : " + plural(pane_.entries_, "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", " + plural(pane_.taskSections_, "section", "sections")
             + " et " + plural(pane_.units_, "unit\xC3\xA9 de programme", "unit\xC3\xA9s de programme") + ".";
    }
    if (tip != tooltip()) setTooltip(std::move(tip));
}

// ============================================================== la courbe ====
//  Les variables suivies (six au plus), sur les 30 dernieres secondes simulees,
//  lues dans l'historique du moteur (Runtime::history) : la courbe et la table
//  ne peuvent pas se contredire. Les BOOL ont leur bande en bas ; une variable
//  forcee est en pointilles. « Figer » garde l'image (une copie des points).
class SimulationPane::Trend final : public ui::Widget {
public:
    explicit Trend(SimulationPane& pane) : ui::Widget(pane.id() + ".courbe"), pane_(pane) {}
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    void setFrozen(bool on) {
        if (on == frozen_) return;
        snapshot_.clear();
        if (on) {
            snapshot_ = collect();
            auto* rt = pane_.runtime();
            frozenEnd_ = rt ? rt->clockMs() : 0;
        }
        frozen_ = on;
        invalidate();
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left)
            for (const auto& [rect, path] : legend_)
                if (rect.contains(d->pos)) {
                    const std::string keep = path;
                    (void)pane_.selectVariable(keep);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }
private:
    struct Series {
        std::string path, name;
        bool        forced{false}, isBool{false};
        std::vector<sim::Sample> points;
    };
    [[nodiscard]] std::vector<Series> collect() const {
        std::vector<Series> out;
        auto* rt = pane_.runtime();
        const std::int64_t end = rt ? rt->clockMs() : 0;
        for (const auto& path : pane_.watched_) {
            Series s;
            s.path = path;
            s.name = shortName(path);
            if (rt) {
                s.forced = rt->isForced(path);
                sim::Value v;
                s.isBool = rt->get(path, v) && v.type() == sim::Type::Bool;
                if (const auto* h = rt->history(path)) {
                    // Les 30 dernieres secondes seulement, prises par la fin.
                    auto it = h->end();
                    while (it != h->begin() && std::prev(it)->clockMs >= end - kTrendMs - 1000) --it;
                    s.points.assign(it, h->end());
                }
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    SimulationPane&     pane_;
    bool                frozen_{false};
    std::int64_t        frozenEnd_{0};
    std::vector<Series> snapshot_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> legend_;
};

void SimulationPane::Trend::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto b = bounds();
    legend_.clear();
    r.fillRect(b, c.panelBg);
    const auto series = frozen_ ? snapshot_ : collect();
    auto* rt = pane_.runtime();
    const std::int64_t end = frozen_ ? frozenEnd_ : (rt ? rt->clockMs() : 0);
    const std::int64_t start = end - kTrendMs;
    if (series.empty()) {
        const std::string msg = "Choisis une variable, puis \xC2\xAB Suivre sur la courbe \xC2\xBB : elle se trace ici (six au plus).";
        const auto n = r.fitCharacters(msg, kSmall, b.w - 16.f);
        const std::string shown = n >= msg.size() ? msg : msg.substr(0, n);
        r.drawText({b.x + std::max(8.f, (b.w - r.measure(shown, kSmall).width) * 0.5f), b.y + b.h * 0.5f - 8.f}, shown, kSmall, c.textMuted);
        return;
    }
    const float legendH = 22.f;
    const gfx::Rect plot{b.x + 36.f, b.y + 8.f, std::max(10.f, b.w - 44.f), std::max(10.f, b.h - 8.f - 18.f - legendH)};
    r.pushClip(b);

    // L'echelle commune des valeurs (les BOOL ont leur bande, a part).
    bool anyNumber = false, anyBool = false, first = true;
    double lo = 0.0, hi = 1.0;
    for (const auto& s : series) {
        if (s.isBool) {
            anyBool = true;
            continue;
        }
        anyNumber = true;
        for (const auto& p : s.points) {
            if (p.clockMs < start) continue;
            if (first) {
                lo = hi = p.value;
                first = false;
            }
            lo = std::min(lo, p.value);
            hi = std::max(hi, p.value);
        }
    }
    if (first) { lo = 0.0; hi = 1.0; }
    if (hi - lo < 1e-9) {
        const double pad = std::max(1.0, std::fabs(lo) * 0.1);
        lo -= pad;
        hi += pad;
    }
    const double step = niceStep(hi - lo, 4);
    lo = std::floor(lo / step) * step;
    hi = std::ceil(hi / step) * step;
    const gfx::Rect numArea = anyBool && anyNumber ? gfx::Rect{plot.x, plot.y, plot.w, plot.h * 0.72f} : plot;
    const gfx::Rect boolArea = anyBool ? (anyNumber ? gfx::Rect{plot.x, plot.y + plot.h * 0.80f, plot.w, plot.h * 0.20f} : plot) : gfx::Rect{};

    // La grille et ses valeurs.
    if (anyNumber) {
        int guard = 0;
        for (double v = lo; v <= hi + step * 0.5 && guard < 24; v += step, ++guard) {
            const float y = numArea.bottom() - static_cast<float>((v - lo) / (hi - lo)) * numArea.h;
            r.line({plot.x, y}, {plot.right(), y}, c.gridLine, 1.f);
            const auto label = tickLabel(v, step);
            r.drawText({plot.x - 6.f - r.measure(label, kSmall).width, y - r.lineHeight(kSmall) * 0.5f}, label, kSmall, c.textMuted);
        }
    }
    if (anyBool) {
        r.fillRect(boolArea, c.rowAltBg);
        r.drawText({plot.x - 6.f - r.measure("0/1", kSmall).width, boolArea.y + boolArea.h * 0.5f - r.lineHeight(kSmall) * 0.5f},
                   "0/1", kSmall, c.textMuted);
    }
    r.line({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, c.border, 1.f);
    {
        const float ly = plot.bottom() + 3.f;
        const std::string a = "\xE2\x88\x92" "30 s", m = "\xE2\x88\x92" "15 s", z = "maintenant";
        r.drawText({plot.x, ly}, a, kSmall, c.textMuted);
        r.drawText({plot.x + (plot.w - r.measure(m, kSmall).width) * 0.5f, ly}, m, kSmall, c.textMuted);
        r.drawText({plot.right() - r.measure(z, kSmall).width, ly}, z, kSmall, c.textMuted);
    }

    // Les traces : un point par colonne de pixels au plus (le min et le max de
    // la colonne), sinon 1 500 points par variable a chaque image.
    std::size_t index = 0;
    for (const auto& s : series) {
        const auto colour = th.onSurface(kTraces[index % (sizeof kTraces / sizeof kTraces[0])]);
        ++index;
        const gfx::Rect area = s.isBool ? boolArea : numArea;
        const auto yOf = [&](double v) {
            if (s.isBool) return v != 0.0 ? area.y + 2.f : area.bottom() - 2.f;
            return area.bottom() - static_cast<float>((v - lo) / (hi - lo)) * area.h;
        };
        const float width = s.forced ? 2.f : 1.6f;
        // Une forcee : en pointilles (6 pixels dessines, 4 sautes).
        const auto seg = [&](gfx::Point a, gfx::Point z) {
            if (s.forced && std::fmod(std::max(0.f, a.x - plot.x), 10.f) >= 6.f) return;
            r.line(a, z, colour, width);
        };
        gfx::Point prev{};
        bool have = false;
        int bucket = INT_MIN;
        float bMin = 0.f, bMax = 0.f;
        for (const auto& p : s.points) {
            if (p.clockMs < start || p.clockMs > end) continue;
            const float px = plot.x + static_cast<float>(static_cast<double>(p.clockMs - start) / static_cast<double>(kTrendMs)) * plot.w;
            const float py = yOf(p.value);
            const int col = static_cast<int>(std::floor(px));
            if (have && col == bucket) {
                bMin = std::min(bMin, py);
                bMax = std::max(bMax, py);
                if (s.isBool && std::fabs(py - prev.y) > 0.5f) seg({px, prev.y}, {px, py});
                prev = {px, py};
                continue;
            }
            if (have && bMax - bMin > 0.5f && !s.isBool)
                seg({static_cast<float>(bucket) + 0.5f, bMin}, {static_cast<float>(bucket) + 0.5f, bMax});
            if (have) {
                if (s.isBool) {
                    seg(prev, {px, prev.y});
                    if (std::fabs(py - prev.y) > 0.5f) seg({px, prev.y}, {px, py});
                } else {
                    seg(prev, {px, py});
                }
            }
            bucket = col;
            bMin = bMax = py;
            prev = {px, py};
            have = true;
        }
        if (have && bMax - bMin > 0.5f && !s.isBool)
            seg({static_cast<float>(bucket) + 0.5f, bMin}, {static_cast<float>(bucket) + 0.5f, bMax});
    }

    // La legende : cliquer un nom montre la variable dans la table.
    float lx = plot.x;
    const float ly = b.bottom() - legendH + 3.f;
    index = 0;
    for (const auto& s : series) {
        const auto colour = th.onSurface(kTraces[index % (sizeof kTraces / sizeof kTraces[0])]);
        ++index;
        const std::string name = s.forced ? s.name + " (forc\xC3\xA9" "e)" : s.name;
        const float w = 18.f + r.measure(name, kSmall).width;
        if (lx + w > b.right() - 4.f && lx > plot.x) break;
        const float my = ly + r.lineHeight(kSmall) * 0.5f;
        if (s.forced) {
            r.line({lx, my}, {lx + 5.f, my}, colour, 2.f);
            r.line({lx + 8.f, my}, {lx + 13.f, my}, colour, 2.f);
        } else {
            r.line({lx, my}, {lx + 13.f, my}, colour, 2.f);
        }
        r.drawText({lx + 18.f, ly}, name, kSmall, c.text);
        legend_.emplace_back(gfx::Rect{lx, ly - 2.f, w, r.lineHeight(kSmall) + 4.f}, s.path);
        lx += w + 16.f;
    }
    r.popClip();
}

// ================================================================ le volet ====
SimulationPane::SimulationPane(std::string id) : ui::Widget(std::move(id)) {
    band_ = &static_cast<Band&>(addChild(std::make_unique<Band>(*this)));

    filter_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(
        this->id() + ".filtres", std::string("Chercher une variable, un membre (PT1.mes)") + kEllipsis)));
    filter_->setChips({{"Toutes", 0}, {"Forc\xC3\xA9" "es", 0}, {"Qui changent", 0}, {"Situ\xC3\xA9" "es", 0}, {"Suivies", 0}});
    links_ += filter_->changed->connect([this] {
        if (table_) table_->setHighlight(filter_->search());     // lot recherche : surlignee
        if (syncing_) return;
        rebuildRows();
        updateTools();
    });

    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".table")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    table_->setSelectionMode(ui::SelectionMode::Extended);
    table_->setAlternatingRowColors(true);
    // Lot recherche : les filtres des colonnes (Variable, Type, Ecrite par) choisissent
    // des racines ; le volet les applique (la table ne trie ni ne filtre : sa vue
    // reste 0..n-1, ce que la lecture des valeurs suppose).
    table_->setColumnFiltersEnabled(true);
    table_->setColumnFilterMode(ui::TableView::ColumnFilterMode::Host);
    table_->setColumnValuesProvider([this](std::size_t col, const std::function<void(const std::string&)>& emit) {
        const ui::SearchQuery query(filter_ ? filter_->search() : std::string{});
        const auto chip = filter_ ? filter_->current() : std::size_t{0};
        for (std::size_t i = 0; i < roots_.size(); ++i)
            if (rootKept(i, chip, query, static_cast<int>(col))) emit(rootCell(i, col));
    });
    links_ += table_->columnFiltersChanged->connect([this] {
        if (syncing_) return;
        rebuildRows();
        updateTools();
    });
    links_ += table_->expanderClicked->connect([this](RowIndex r) { toggleRow(static_cast<std::size_t>(r)); });
    links_ += table_->activated->connect([this](RowIndex r) { activateRow(static_cast<std::size_t>(r)); });
    // Le clic droit sur une ligne : notre menu (sur les titres : celui de la table).
    catcher_ = &addChild(std::make_unique<ClickCatcher>(this->id() + ".clic", [this](const ui::MouseDown& d) {
        if (d.button != ui::MouseButton::Right) return false;
        std::size_t hit = kNpos;
        for (std::size_t i = 0; i < table_->visibleRowCount(); ++i) {
            gfx::Rect rr;
            if (table_->rowRect(i, rr) && rr.contains(d.pos)) {
                hit = table_->viewRow(i);
                break;
            }
        }
        if (hit == kNpos || hit >= rows_.size()) return false;
        const auto sel = table_->selectedModelRows();
        if (std::find(sel.begin(), sel.end(), static_cast<RowIndex>(hit)) == sel.end())
            table_->selectModelRows({static_cast<RowIndex>(hit)}, true);
        openMenu(d.pos);
        return true;
    }));

    // Les forcages : une liste sous son titre peint (le cadre cache les titres de la table).
    forcedBox_ = &addChild(std::make_unique<ui::Widget>(this->id() + ".forcages.cadre"));
    forced_ = &static_cast<ui::TableView&>(forcedBox_->addChild(std::make_unique<ui::TableView>(this->id() + ".forcages")));
    forcedModel_ = std::make_shared<ForcedModel>(*this);
    forced_->setModel(forcedModel_);
    forced_->setSelectionMode(ui::SelectionMode::Single);
    forced_->setAlternatingRowColors(false);
    links_ += forced_->activated->connect([this](RowIndex r) {
        if (r >= forcedNames_.size()) return;
        askForce({forcedNames_[r]});
    });
    forcedBox_->addChild(std::make_unique<ClickCatcher>(this->id() + ".forcages.clic", [this](const ui::MouseDown& d) {
        if (d.button != ui::MouseButton::Left || forcedNames_.empty()) return false;
        for (std::size_t i = 0; i < forced_->visibleRowCount(); ++i) {
            gfx::Rect rr;
            if (!forced_->rowRect(i, rr) || !rr.contains(d.pos)) continue;
            const auto r = static_cast<std::size_t>(forced_->viewRow(i));
            if (r >= forcedNames_.size()) return false;
            const std::string path = forcedNames_[r];
            gfx::Rect cross;
            if (forced_->cellRect(i, 2, cross) && cross.contains(d.pos)) {
                releasePaths({path});
                return true;
            }
            // Ailleurs sur la ligne : la montrer dans la table (le clic suit son cours).
            (void)selectVariable(path);
            return false;
        }
        return false;
    }));

    trend_ = &static_cast<Trend&>(addChild(std::make_unique<Trend>(*this)));

    policy_ = &static_cast<macroui::Segmented&>(addChild(std::make_unique<macroui::Segmented>(this->id() + ".politique")));
    policy_->setOptions({"s'arr\xC3\xAAter", "continuer (0)"});
    policy_->setSelected(1);
    policy_->setTooltip("Une fonction que le simulateur ne conna\xC3\xAEt pas : arr\xC3\xAAter le cycle dessus, "
                        "ou lui faire rendre 0 et continuer (elle est list\xC3\xA9" "e ici)");
    links_ += policy_->selectionChanged->connect([this](int i) { setUnknownPolicy(i == 1); });

    diagBox_ = &addChild(std::make_unique<ui::Widget>(this->id() + ".simulateur.cadre"));
    diag_ = &static_cast<ui::TableView&>(diagBox_->addChild(std::make_unique<ui::TableView>(this->id() + ".simulateur")));
    diagModel_ = std::make_shared<DiagModel>(*this);
    diag_->setModel(diagModel_);
    diag_->setSelectionMode(ui::SelectionMode::Single);
    diag_->setAlternatingRowColors(false);
    links_ += diag_->activated->connect([this](RowIndex r) { goToItem(static_cast<std::size_t>(r) / 2); });
    diagBox_->addChild(std::make_unique<ClickCatcher>(this->id() + ".simulateur.clic", [this](const ui::MouseDown& d) {
        if (d.button != ui::MouseButton::Left) return false;
        for (std::size_t i = 0; i < diag_->visibleRowCount(); ++i) {
            gfx::Rect cell;
            if (!diag_->cellRect(i, 1, cell) || !cell.contains(d.pos)) continue;
            const auto r = static_cast<std::size_t>(diag_->viewRow(i));
            if (r % 2 == 0 && r / 2 < diagItems_.size() && diagItems_[r / 2].link) {
                goToItem(r / 2);
                return true;
            }
            return false;
        }
        return false;
    }));

    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(this->id() + ".menu")));
    links_ += menu_->itemChosen->connect([this](int a) { menuChosen(a); });

    rebuildDiag();
}

SimulationPane::~SimulationPane() = default;

void SimulationPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void SimulationPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(ARun, HmiGlyph::Play, "Lancer la simulation : le programme de MAST tourne, cycle apr\xC3\xA8s cycle", "Simuler");
    t.add(AStep, HmiGlyph::Refresh, "Un seul cycle, puis la pause", "Un cycle");
    t.add(AStop, HmiGlyph::Stop, "Arr\xC3\xAAter : tout repart des valeurs initiales ; les for\xC3\xA7" "ages restent", "Arr\xC3\xAAter");
    t.separator();
    t.add(AForce, HmiGlyph::Lock,
          "Forcer les variables choisies (une valeur pour toutes) : le programme ne peut plus les changer ; double-clic sur une valeur, aussi",
          std::string("Forcer") + kEllipsis);
    t.add(ARelease, HmiGlyph::Unlock, "Rel\xC3\xA2" "cher les variables choisies : le programme les reprend", "Rel\xC3\xA2" "cher");
    t.add(AReleaseAll, HmiGlyph::Unlock, "Rendre toutes les variables forc\xC3\xA9" "es au programme", "Rel\xC3\xA2" "cher tout (0)");
    t.separator();
    t.add(AWatch, HmiGlyph::Trend, "Suivre les variables choisies sur la courbe (six au plus) ; d\xC3\xA9j\xC3\xA0 suivies : ne plus les suivre",
          "Suivre sur la courbe");
    t.add(AAddToTable, HmiGlyph::VariableTable, "Ajouter les variables choisies \xC3\xA0 une table d'animation", "Ajouter \xC3\xA0 une table d'animation");
    t.separator();
    t.add(AExport, HmiGlyph::Export, "\xC3\x89" "crire les for\xC3\xA7" "ages dans un fichier texte (le dossier simulation du projet)",
          std::string("Exporter les for\xC3\xA7" "ages") + kEllipsis);
    t.add(AImport, HmiGlyph::Import, "Appliquer des for\xC3\xA7" "ages gard\xC3\xA9s dans un fichier", std::string("Importer") + kEllipsis);
    t.separator();
    t.add(AHelp, HmiGlyph::Help, "L'aide de la simulation (F1)", "Aide (F1)");

    using S = SimulationHost::State;
    // Ces predicats se relisent a chaque dessin de la barre : ils parcourent la
    // selection sans la trier ni copier de chemins (Ctrl+A en choisit des milliers).
    const auto selectionHas = [this](auto&& test) {
        if (!table_) return false;
        for (const auto r : table_->selectedModelRows())
            if (r < rows_.size() && test(static_cast<std::size_t>(r))) return true;
        return false;
    };
    const auto isValue = [this](std::size_t r) { return !rows_[r].folder && rows_[r].node.real && !rows_[r].agg; };
    t.setEnabledWhen(ARun, [this] { return project() != nullptr; });
    t.setEnabledWhen(AStep, [this] {
        auto* h = host();
        return project() != nullptr && (!h || h->state() != S::Halted);
    });
    t.setEnabledWhen(AStop, [this] {
        auto* h = host();
        return h && h->attached() && h->state() != S::Stopped;
    });
    t.setEnabledWhen(AForce, [selectionHas, isValue] { return selectionHas(isValue); });
    t.setEnabledWhen(ARelease, [this, selectionHas] {
        if (selectionHas([this](std::size_t r) { return r < live_.size() && live_[r].forced; })) return true;
        return forced_ && !forced_->selectedModelRows().empty() && !forcedNames_.empty();
    });
    t.setEnabledWhen(AReleaseAll, [this] { return !forcedNames_.empty(); });
    t.setEnabledWhen(AWatch, [selectionHas, isValue] { return selectionHas(isValue); });
    t.setEnabledWhen(AAddToTable, [this, selectionHas] {
        return static_cast<bool>(hosts_.addToTable) && selectionHas([this](std::size_t r) { return !rows_[r].folder && rows_[r].node.real; });
    });
    t.setEnabledWhen(AExport, [this] { return runtime() != nullptr && !forcedNames_.empty(); });
    t.setEnabledWhen(AImport, [this] { return project() != nullptr; });
    links_ += t.triggered->connect([this](int a) { runAction(a); });

    frame.setHint("F9 ouvre cet onglet \xC2\xB7 la barre du haut garde Simuler, Arr\xC3\xAAter, Un cycle \xC2\xB7 double-clic sur une valeur : "
                  "la forcer \xC2\xB7 clic droit : Forcer, Rel\xC3\xA2" "cher, Suivre, Ajouter \xC3\xA0 une table");
    runLabel_.clear();
    releaseAllShown_ = kNpos;
    updateTools();
}

// ------------------------------------------------------------- les acces ----
SimulationHost* SimulationPane::host() const { return hosts_.host ? hosts_.host() : nullptr; }

sim::Runtime* SimulationPane::runtime() const {
    auto* h = host();
    return h ? h->runtime() : nullptr;
}

std::shared_ptr<const domain::Project> SimulationPane::project() const {
    return hosts_.project ? hosts_.project() : nullptr;
}

bool SimulationPane::shown() const {
    for (const ui::Widget* w = this; w; w = w->parent())
        if (!w->visible()) return false;
    return true;
}

void SimulationPane::status(const std::string& message) const {
    if (hosts_.status) hosts_.status(message);
}

std::string SimulationPane::keyOf(const Row& r) const {
    return r.folder ? "#dossier:" + std::to_string(r.folderId) : lower(r.node.key);
}

std::vector<std::size_t> SimulationPane::selectedRows() const {
    std::vector<std::size_t> out;
    if (!table_) return out;
    for (const auto r : table_->selectedModelRows())
        if (r < rows_.size()) out.push_back(static_cast<std::size_t>(r));
    std::sort(out.begin(), out.end());
    return out;
}

std::string SimulationPane::tablePath(const Row& r) const {
    if (r.root >= 0 && static_cast<std::size_t>(r.root) < roots_.size()) {
        const auto& root = roots_[static_cast<std::size_t>(r.root)];
        // Une locale : la table d'animation la connait par son nom, sans l'unite
        // (elle retrouve l'unite en lisant la declaration).
        if (root.name != root.label && root.name.size() > root.label.size() && startsWith(r.node.path, root.name))
            return r.node.path.substr(root.name.size() - root.label.size());
    }
    return r.node.path;
}

std::string SimulationPane::liveText(std::string_view path) const {
    auto* rt = runtime();
    if (!rt) return kDash;
    sim::Value v;
    return rt->get(path, v) ? formatValue(v) : std::string{};
}

// -------------------------------------------------------------- les racines ----
void SimulationPane::refresh() {
    rebuildRoots();
    checkRuntime();
    rebuildRows();
    refreshSide();
    updateTools();
    if (band_) band_->invalidate();
    invalidateLayout();
}

void SimulationPane::rebuildRoots() {
    // Ce qui changeait reste « qui change » : un refresh() suit chaque commande.
    std::map<std::string, Live> before;
    for (std::size_t i = 0; i < roots_.size() && i < rootLive_.size(); ++i) before.emplace(roots_[i].name, rootLive_[i]);
    roots_.clear();
    writers_.clear();
    entries_ = taskSections_ = units_ = 0;
    folders_ = {"Instances de DFB", "Temporisations et blocs standard", "Instances de types d\xC3\xA9riv\xC3\xA9s",
                "Situ\xC3\xA9" "es", "Autres"};
    const auto p = project();
    if (p) {
        buildWriters(*p);
        for (const auto& e : project::api::entriesOf(*p, "MAST")) {
            ++entries_;
            if (e.unit) ++units_;
            else ++taskSections_;
        }
        const auto add = [&](const domain::Variable& v, std::string name, std::string label, int folder) {
            Root r;
            r.name = std::move(name);
            r.label = std::move(label);
            r.type = text(*p, v.type.name);
            if (v.located || v.address.valid()) r.address = v.address.raw;
            r.folder = folder;
            r.comment = text(*p, v.comment);          // lot recherche : la recherche le lit
            r.agg = mt::hasChildren(*p, mt::root(r.name, r.type));
            // Qui l'ecrit, elle-meme (une structure : ses membres le disent, un par un).
            if (const auto it = writers_.find(lower(r.name)); it != writers_.end())
                for (const auto& s : it->second)
                    if (covers(s.pattern, lower(r.name))) {
                        r.writer = s.label;
                        r.section = s.section;
                        r.line = s.line;
                        break;
                    }
            roots_.push_back(std::move(r));
        };
        for (const auto& v : p->variables) {
            if (v.scope != domain::VariableScope::Global && v.scope != domain::VariableScope::Constant) continue;
            int folder = FOther;
            switch (project::usage::genreOf(*p, v)) {
                case project::usage::Genre::DfbInstance:   folder = FDfb; break;
                case project::usage::Genre::StandardBlock: folder = FStd; break;
                case project::usage::Genre::DdtInstance:   folder = FDdt; break;
                case project::usage::Genre::Located:       folder = FLocated; break;
                default:                                   folder = FOther; break;
            }
            const auto name = text(*p, v.name);
            add(v, name, name, folder);
        }
        // Les locales de chaque unite, simulees sous « Unite.nom ».
        for (const auto& pou : p->pous) {
            if (pou.kind != domain::PouKind::ProgramUnit) continue;
            const auto unit = text(*p, pou.name);
            const int folder = static_cast<int>(folders_.size());
            bool any = false;
            const auto take = [&](domain::Index vi) {
                if (vi >= p->variables.size()) return;
                const auto& v = p->variables[vi];
                const auto name = text(*p, v.name);
                add(v, unit + "." + name, name, folder);
                any = true;
            };
            for (const auto vi : pou.parameters) take(vi);
            for (const auto vi : pou.locals) take(vi);
            if (any) folders_.push_back("Locales" + std::string(kDot) + unit);
        }
    }
    rootLive_.assign(roots_.size(), Live{});
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        const auto it = before.find(roots_[i].name);
        if (it != before.end()) rootLive_[i] = it->second;
        else rootLive_[i].changedAt = kNever;
    }
    changingShown_.clear();
    // Les cases sont rangees par indice de racine : l'index se refait a la
    // prochaine lecture.
    probes_.clear();
    rootSlots_.clear();
    probedRuntime_ = nullptr;
    probedGeneration_ = ~std::uint64_t{0};
}

void SimulationPane::buildWriters(const domain::Project& p) {
    writers_.clear();
    std::unordered_set<std::string> seenSites;     // chemin + entree : deja note
    std::unordered_set<std::string> globals;
    for (const auto& v : p.variables)
        if (v.scope == domain::VariableScope::Global || v.scope == domain::VariableScope::Constant)
            globals.insert(lower(p.strings.text(v.name)));
    // Les noms que chaque unite declare (ses parametres, ses locales) : dans ses
    // sections, ils la designent elle, pas une globale.
    std::map<domain::Index, std::unordered_set<std::string>> declared;
    const auto declaredBy = [&](domain::Index unit) -> const std::unordered_set<std::string>& {
        if (const auto it = declared.find(unit); it != declared.end()) return it->second;
        auto& set = declared[unit];
        if (unit < p.pous.size()) {
            for (const auto v : p.pous[unit].parameters)
                if (v < p.variables.size()) set.insert(lower(p.strings.text(p.variables[v].name)));
            for (const auto v : p.pous[unit].locals)
                if (v < p.variables.size()) set.insert(lower(p.strings.text(p.variables[v].name)));
        }
        return set;
    };
    const auto unitOf = [&](const domain::Section& sec) -> domain::Index {
        return sec.owner < p.pous.size() && p.pous[sec.owner].kind == domain::PouKind::ProgramUnit ? sec.owner : domain::kNoIndex;
    };
    const auto scan = [&](domain::Index si, const std::string& label) {
        if (si >= p.sections.size()) return;
        const auto& sec = p.sections[si];
        const std::string sectionName = text(p, sec.name);
        const domain::Index unit = unitOf(sec);
        const std::string unitName = unit != domain::kNoIndex ? lower(text(p, p.pous[unit].name)) : std::string{};
        forEachWrite(sec.body, [&](const std::string& path, std::uint32_t line) {
            const auto lp = lower(path);
            const auto root = lp.substr(0, lp.find_first_of(".["));
            std::string key, pattern;
            if (unit != domain::kNoIndex && declaredBy(unit).count(root)) {
                key = unitName + "." + root;
                pattern = unitName + "." + lp;
            } else if (globals.count(root)) {
                key = root;
                pattern = lp;
            } else {
                return;
            }
            // La premiere ligne de chaque chemin, par entree, suffit. Pas de
            // plafond bas : Armoires s'ecrit en des centaines d'endroits, et ses
            // membres ecrits tard (Acquisitions_ANA, la quatrieme entree)
            // perdaient leur « ecrite par » au-dela des 64 premiers.
            if (!seenSites.insert(pattern + '\x1f' + label).second) return;
            auto& sites = writers_[key];
            if (sites.size() >= 4096) return;
            sites.push_back(WriteSite{pattern, label, sectionName, line});
        });
    };
    // Dans l'ordre d'execution de MAST : la premiere entree qui ecrit est celle qu'on dit.
    for (const auto& step : domain::executionOrder(p, std::string_view("MAST"))) {
        if (step.section >= p.sections.size()) continue;
        const auto& sec = p.sections[step.section];
        const auto unit = unitOf(sec);
        scan(step.section, unit != domain::kNoIndex ? text(p, p.pous[unit].name) + kDot + text(p, sec.name) : text(p, sec.name));
    }
    // Les sous-routines ensuite : elles ecrivent quand on les appelle.
    for (domain::Index si = 0; si < p.sections.size(); ++si)
        if (p.sections[si].isSubroutine) scan(si, text(p, p.sections[si].name) + " (SR)");
}

void SimulationPane::fillWriter(Row& r) const {
    r.writer.clear();
    r.section.clear();
    r.line = 0;
    if (r.folder || !r.node.real || r.root < 0 || static_cast<std::size_t>(r.root) >= roots_.size()) return;
    const auto it = writers_.find(lower(roots_[static_cast<std::size_t>(r.root)].name));
    if (it == writers_.end()) return;
    const auto path = lower(r.node.path);
    for (const auto& s : it->second)
        if (covers(s.pattern, path)) {
            r.writer = s.label;
            r.section = s.section;
            r.line = s.line;
            return;
        }
}

bool SimulationPane::locate(const domain::Project& p, std::string_view path, mt::Node& out, std::vector<std::string>* chain,
                            int* root) const {
    const auto want = pathKey(path);
    if (want.empty()) return false;
    int best = -1;
    std::size_t bestLength = 0;
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        const auto name = lower(roots_[i].name);
        if (best >= 0 && name.size() <= bestLength) continue;
        if (want == name || (want.size() > name.size() && startsWith(want, name) && (want[name.size()] == '.' || want[name.size()] == '['))) {
            best = static_cast<int>(i);
            bestLength = name.size();
        }
    }
    if (best < 0) return false;
    if (root) *root = best;
    auto node = mt::root(roots_[static_cast<std::size_t>(best)].name, roots_[static_cast<std::size_t>(best)].type);
    for (int guard = 0; guard < 64; ++guard) {
        if (node.real && lower(node.path) == want) {
            out = std::move(node);
            return true;
        }
        if (!mt::hasChildren(p, node)) return false;
        if (chain) chain->push_back(lower(node.key));
        auto kids = mt::children(p, node);
        bool found = false;
        for (auto& k : kids) {
            const auto kp = lower(k.path);
            if (k.real) {
                if (kp == want || (want.size() > kp.size() && startsWith(want, kp) && (want[kp.size()] == '.' || want[kp.size()] == '['))) {
                    node = std::move(k);
                    found = true;
                    break;
                }
                continue;
            }
            // Un paquet, une ligne d'un tableau : les indices du chemin cherche.
            if (want.size() <= kp.size() || !startsWith(want, kp) || want[kp.size()] != '[') continue;
            const auto close = want.find(']', kp.size());
            if (close == std::string::npos) continue;
            std::vector<std::int64_t> idx;
            {
                const auto inside = want.substr(kp.size() + 1, close - kp.size() - 1);
                std::size_t from = 0;
                while (from <= inside.size()) {
                    const auto comma = inside.find(',', from);
                    const auto part = inside.substr(from, (comma == std::string::npos ? inside.size() : comma) - from);
                    idx.push_back(std::strtoll(part.c_str(), nullptr, 10));
                    if (comma == std::string::npos) break;
                    from = comma + 1;
                }
            }
            if (idx.size() <= k.dim) continue;
            bool same = true;
            for (std::size_t f = 0; f < k.fixed.size(); ++f)
                if (f >= idx.size() || idx[f] != k.fixed[f]) same = false;
            if (!same || idx[k.dim] < k.first || idx[k.dim] > k.last) continue;
            node = std::move(k);
            found = true;
            break;
        }
        if (!found) return false;
    }
    return false;
}

// ---------------------------------------------------------------- les lignes ----
bool SimulationPane::rootPasses(std::size_t root, std::size_t chip) const {
    if (root >= roots_.size()) return false;
    switch (chip) {
        case ChChanging: return root < rootLive_.size() && now_ - rootLive_[root].changedAt < kChanging;
        case ChLocated:  return !roots_[root].address.empty();
        default:         return true;
    }
}

std::string SimulationPane::rootCell(std::size_t root, std::size_t column) const {
    if (root >= roots_.size()) return {};
    const auto& rt = roots_[root];
    switch (column) {
        case CVar:    return rt.label;
        case CType:   return rt.type;
        case CWriter: return rt.writer;
        default:      return {};             // la valeur, le forcage : ils changent a chaque cycle
    }
}

bool SimulationPane::rootKept(std::size_t root, std::size_t chip, const ui::SearchQuery& query, int skipColumn) const {
    if (!rootPasses(root, chip)) return false;
    const auto& rt = roots_[root];
    if (!query.matches({rt.label, rt.name, rt.comment, rt.type, rt.address, rt.writer})) return false;
    return ui::ColumnFilter::acceptsAll(table_->columnFilters(), [&](std::size_t c) { return rootCell(root, c); }, skipColumn);
}

void SimulationPane::addRootRow(const domain::Project& p, std::size_t root, int depth) {
    const auto& rt = roots_[root];
    Row r;
    r.root = static_cast<int>(root);
    r.depth = depth;
    r.node = mt::root(rt.name, rt.type);
    r.label = rt.label;
    r.address = rt.address;
    r.agg = rt.agg;
    r.array = mt::parseArray(rt.type).valid();
    r.writer = rt.writer;
    r.section = rt.section;
    r.line = rt.line;
    r.open = r.agg && expanded_.count(lower(r.node.key)) != 0;
    rows_.push_back(r);
    if (r.open) addChildren(p, r, depth + 1);
}

void SimulationPane::addChildren(const domain::Project& p, const Row& parent, int depth) {
    // Sans limite de profondeur : un grand tableau arrive par paquets, qu'on
    // deplie a leur tour. Une copie de chaque ligne : rows_ grandit dessous.
    for (auto& n : mt::children(p, parent.node)) {
        Row c;
        c.root = parent.root;
        c.depth = depth;
        c.agg = mt::hasChildren(p, n);
        c.array = !n.real || mt::parseArray(n.type).valid();
        c.label = n.label;
        c.node = std::move(n);
        fillWriter(c);
        c.open = c.agg && expanded_.count(lower(c.node.key)) != 0;
        rows_.push_back(c);
        if (c.open) addChildren(p, c, depth + 1);
    }
}

void SimulationPane::addPathRow(const domain::Project& p, const std::string& path, int depth) {
    Row r;
    r.depth = depth;
    int root = -1;
    mt::Node n;
    if (locate(p, path, n, nullptr, &root)) {
        r.node = std::move(n);
    } else {
        // Une adresse directe (%MW12), un nom que l'arbre ne connait pas : son
        // type selon la simulation.
        std::string type;
        if (auto* rt = runtime()) {
            sim::Value v;
            if (rt->get(path, v)) type = std::string(sim::toString(v.type()));
        }
        r.node = mt::root(path, type);
    }
    r.root = root;
    r.label = path;
    r.agg = mt::hasChildren(p, r.node);
    r.array = mt::parseArray(r.node.type).valid();
    if (root >= 0) r.address = r.node.path == roots_[static_cast<std::size_t>(root)].name ? roots_[static_cast<std::size_t>(root)].address : std::string{};
    fillWriter(r);
    r.open = r.agg && expanded_.count(lower(r.node.key)) != 0;
    rows_.push_back(r);
    if (r.open) addChildren(p, r, depth + 1);
}

void SimulationPane::rebuildRows() {
    rebuilding_ = true;
    // Ce qui etait choisi, par cle : retrouve apres (Ctrl+A peut en choisir des
    // milliers : un ensemble, pas une liste parcourue a chaque ligne).
    std::unordered_set<std::string> keep;
    for (const auto r : selectedRows()) keep.insert(keyOf(rows_[r]));
    std::vector<std::string> before;
    before.reserve(rows_.size());
    // Les valeurs lues gardees par cle : une ligne qui revient (replier puis
    // deplier, chercher puis effacer) les retrouve sans attendre une lecture.
    if (lastSeen_.size() > 20000) lastSeen_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        before.push_back(keyOf(rows_[i]));
        if (i < live_.size() && !rows_[i].folder && !rows_[i].agg && rows_[i].node.real && live_[i].ok) lastSeen_[before.back()] = live_[i];
    }
    rows_.clear();
    searching_ = false;

    const auto p = project();
    std::size_t keptRoots = 0, totalRoots = roots_.size();      // lot recherche : "37 sur 251"
    if (p && filter_) {
        const auto chip = filter_->current();
        // Lot recherche : la recherche de toutes les listes - chaque mot (ou
        // "phrase") dans le nom, le COMMENTAIRE, le type, l'adresse ou qui
        // l'ecrit, aucun -mot exclu ; puis les membres (leur chemin), par le
        // premier mot. Les filtres des colonnes choisissent les racines.
        const ui::SearchQuery query(trim(filter_->search()));
        const std::string needle = query.terms().empty() ? std::string{} : query.terms().front();
        searching_ = !query.empty();
        if (chip == ChForced || chip == ChWatched) {
            // Une liste a plat : les chemins complets, depliables comme dans l'arbre.
            const auto list = chip == ChForced ? forcedNames_ : watched_;
            totalRoots = list.size();
            for (const auto& path : list)
                if (query.matches({path})) {
                    addPathRow(*p, path, 0);
                    ++keptRoots;
                }
        } else {
            if (chip == ChChanging) {
                changingShown_.clear();
                for (std::size_t i = 0; i < roots_.size(); ++i)
                    if (rootPasses(i, ChChanging)) changingShown_.push_back(i);
                nextChanging_ = now_ + 2.0;
            }
            const ui::SearchQuery none;
            std::vector<std::vector<std::size_t>> byFolder(folders_.size());
            for (std::size_t i = 0; i < roots_.size(); ++i) {
                const auto f = roots_[i].folder;
                if (f >= 0 && static_cast<std::size_t>(f) < byFolder.size() && rootKept(i, chip, none, -1))
                    byFolder[static_cast<std::size_t>(f)].push_back(i);
            }
            int budget = kSearchBudget;
            std::size_t hits = 0;
            for (std::size_t f = 0; f < folders_.size(); ++f) {
                const auto& list = byFolder[f];
                if (list.empty()) continue;
                Row head;
                head.folder = true;
                head.folderId = static_cast<int>(f);
                head.label = folders_[f];
                head.count = list.size();
                head.open = searching_ || openFolders_.count(static_cast<int>(f)) != 0;
                if (query.empty()) {
                    keptRoots += list.size();
                    rows_.push_back(head);
                    if (head.open)
                        for (const auto i : list) addRootRow(*p, i, 1);
                    continue;
                }
                // Chercher : les racines que la recherche trouve (nom, commentaire...),
                // puis les membres (leur chemin entier) ; les dossiers trouves s'ouvrent.
                const auto at = rows_.size();
                rows_.push_back(head);
                std::size_t found = 0;
                for (const auto i : list) {
                    const auto& rt = roots_[i];
                    if (query.matches({rt.label, rt.name, rt.comment, rt.type, rt.address, rt.writer})) {
                        addRootRow(*p, i, 1);
                        ++found;
                        ++keptRoots;
                        continue;
                    }
                    if (needle.empty() || !rt.agg || budget <= 0 || hits >= kSearchHits) continue;
                    std::vector<mt::Node> matches;
                    searchMembers(*p, mt::root(rt.name, rt.type), needle, rt.name.size(), budget, matches, kSearchHits - hits);
                    for (auto& m : matches) {
                        if (!query.matches({m.path})) continue;       // les autres mots, les exclus
                        Row r;
                        r.root = static_cast<int>(i);
                        r.depth = 1;
                        r.agg = mt::hasChildren(*p, m);
                        r.array = mt::parseArray(m.type).valid();
                        // Une locale : sans son unite (le dossier la dit deja).
                        r.label = rt.name != rt.label && m.path.size() > rt.name.size() - rt.label.size()
                                      ? m.path.substr(rt.name.size() - rt.label.size())
                                      : m.path;
                        r.node = std::move(m);
                        fillWriter(r);
                        r.open = r.agg && expanded_.count(lower(r.node.key)) != 0;
                        rows_.push_back(r);
                        if (r.open) addChildren(*p, r, 2);
                        ++found;
                        ++hits;
                    }
                }
                if (found == 0) rows_.resize(at);          // un dossier sans rien : pas montre
                else rows_[at].count = found;
            }
        }
    }

    table_->setColumnFilterCounts(keptRoots, totalRoots);

    // Les valeurs deja lues reviennent tout de suite (pas de tiret le temps d'une lecture).
    live_.assign(rows_.size(), Live{});
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& r = rows_[i];
        if (r.folder) continue;
        if (r.agg) {
            live_[i].text = aggregateText(r.node);
            continue;
        }
        if (const auto it = lastSeen_.find(keyOf(r)); it != lastSeen_.end()) live_[i] = it->second;
    }
    readValues();

    bool same = before.size() == rows_.size();
    for (std::size_t i = 0; same && i < rows_.size(); ++i) same = before[i] == keyOf(rows_[i]);
    if (!same) {
        // La table ne trie ni ne filtre : sa vue est 0..n-1. Autant de lignes
        // qu'avant, elle reste valable et la table garde sa position ; un autre
        // nombre la refait (et la table repart en haut) - la ligne choisie
        // revient alors en vue.
        const bool resized = before.size() != rows_.size();
        if (resized) model_->modelReset->emit();
        std::vector<RowIndex> selection;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (keep.count(keyOf(rows_[i]))) selection.push_back(static_cast<RowIndex>(i));
        if (resized || selection != table_->selectedModelRows()) {
            syncing_ = true;
            table_->selectModelRows(std::move(selection), false);
            syncing_ = false;
        }
    }
    table_->invalidate();
    rebuilding_ = false;
    invalidate();
}

void SimulationPane::toggleRow(std::size_t index) {
    if (index >= rows_.size()) return;
    const auto& row = rows_[index];
    if (row.folder) {
        if (searching_) return;     // la recherche ouvre tout
        const int f = row.folderId;
        if (!openFolders_.erase(f)) openFolders_.insert(f);
    } else if (row.agg) {
        const auto key = lower(row.node.key);
        if (!expanded_.erase(key)) expanded_.insert(key);
    } else {
        return;
    }
    rebuildRows();
}

void SimulationPane::activateRow(std::size_t index) {
    if (index >= rows_.size()) return;
    const auto& row = rows_[index];
    if (row.folder || row.agg) {
        toggleRow(index);
        return;
    }
    if (!row.node.real) return;
    // Sur « Ecrite par » : la ligne qui l'ecrit ; ailleurs : la forcer.
    if (table_->lastClickedColumn() == static_cast<int>(CWriter) && !row.section.empty() && hosts_.goToLine) {
        const std::string section = row.section;
        const auto line = row.line;
        hosts_.goToLine(section, line);
        return;
    }
    askForce({row.node.path});
}

bool SimulationPane::selectVariable(std::string_view path) {
    const auto want = pathKey(path);
    if (want.empty()) return false;
    const auto pick = [&]() -> bool {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (!rows_[i].folder && rows_[i].node.real && lower(rows_[i].node.path) == want) {
                table_->selectModelRows({static_cast<RowIndex>(i)}, true);
                table_->invalidate();
                return true;
            }
        return false;
    };
    if (pick()) return true;
    const auto p = project();
    if (!p) return false;
    mt::Node node;
    std::vector<std::string> chain;
    int root = -1;
    if (!locate(*p, path, node, &chain, &root)) return false;
    // Elle n'est pas montree : toutes les variables, sans recherche, son dossier
    // et ses parents ouverts.
    if (filter_ && (filter_->current() != ChAll || !trim(filter_->search()).empty())) {
        syncing_ = true;
        filter_->setSearch("");
        filter_->setCurrent(ChAll);
        syncing_ = false;
    }
    // Lot recherche : un filtre de colonne qui la cache s'efface aussi.
    if (!table_->columnFilters().empty() && !rootKept(static_cast<std::size_t>(root), ChAll, ui::SearchQuery{}, -1)) {
        syncing_ = true;
        table_->clearColumnFilters();
        syncing_ = false;
    }
    openFolders_.insert(roots_[static_cast<std::size_t>(root)].folder);
    for (const auto& k : chain) expanded_.insert(k);
    rebuildRows();
    return pick();
}

bool SimulationPane::setExpanded(std::string_view path, bool open) {
    const auto want = pathKey(path);          // une cle, un chemin : sans blancs
    const auto label = lower(trim(path));     // un libelle : « Instances de DFB », « [100 ... 199] »
    if (want.empty()) return false;
    // Un dossier, par son libelle (celui des locales d'une unite : par le nom de l'unite aussi).
    for (std::size_t f = 0; f < folders_.size(); ++f)
        if (folderIs(f, label)) {
            const int id = static_cast<int>(f);
            if (open ? openFolders_.insert(id).second : openFolders_.erase(id) != 0) rebuildRows();
            return true;
        }
    const auto apply = [&](const std::string& key) {
        if (open ? expanded_.insert(key).second : expanded_.erase(key) != 0) rebuildRows();
        return true;
    };
    // Montree : par sa cle, son libelle, ou son chemin (un paquet a le chemin de
    // son tableau : seulement par sa cle ou son libelle).
    for (const auto& r : rows_)
        if (!r.folder && r.agg && (lower(r.node.key) == want || lower(r.label) == label || (r.node.real && lower(r.node.path) == want)))
            return apply(lower(r.node.key));
    // Pas montree : la trouver, montrer ses parents, puis elle.
    const auto p = project();
    if (!p) return false;
    mt::Node node;
    std::vector<std::string> chain;
    int root = -1;
    if (!locate(*p, path, node, &chain, &root) || !mt::hasChildren(*p, node)) return false;
    openFolders_.insert(roots_[static_cast<std::size_t>(root)].folder);
    for (const auto& k : chain) expanded_.insert(k);
    const auto key = lower(node.key);
    if (open) expanded_.insert(key);
    else expanded_.erase(key);
    rebuildRows();
    return true;
}

bool SimulationPane::isExpanded(std::string_view path) const {
    const auto want = pathKey(path);
    const auto label = lower(trim(path));
    for (std::size_t f = 0; f < folders_.size(); ++f)
        if (folderIs(f, label)) return openFolders_.count(static_cast<int>(f)) != 0;
    if (expanded_.count(want)) return true;
    for (const auto& r : rows_)
        if (!r.folder && r.agg && (lower(r.label) == label || (r.node.real && lower(r.node.path) == want)) && r.open)
            return true;
    return false;
}

bool SimulationPane::folderIs(std::size_t folder, const std::string& label) const {
    if (folder >= folders_.size()) return false;
    if (lower(folders_[folder]) == label) return true;
    if (folder <= static_cast<std::size_t>(FOther)) return false;
    // « Locales · Unite » : l'unite seule, ou « Locales Unite », « Locales - Unite ».
    const std::string dot = kDot;
    const auto at = folders_[folder].find(dot);
    if (at == std::string::npos) return false;
    const auto unit = lower(std::string_view(folders_[folder]).substr(at + dot.size()));
    return label == unit || label == "locales " + unit || label == "locales - " + unit;
}

bool SimulationPane::chooseChip(std::string_view label) { return filter_ && filter_->chooseChip(label); }

void SimulationPane::setSearch(const std::string& textToFind) {
    if (filter_) filter_->setSearch(textToFind);
}

// ---------------------------------------------------------------- les valeurs ----
void SimulationPane::readValues() {
    // Les lignes seulement, tout de suite : l'appelant decide du rythme (tick
    // lit quatre fois par seconde). Les 64 000 cases (« Qui changent »), tick
    // les parcourt par morceaux (sweepStep).
    auto* rt = runtime();
    const bool quiet = quietRead_;
    quietRead_ = false;
    bool changed = false;
    const auto readOne = [&](const std::string& path, Live& l, int root) {
        Live fresh;
        fresh.readAt = now_;
        fresh.changedAt = l.changedAt;
        if (rt) {
            sim::Value v;
            if (rt->get(path, v)) {
                fresh.ok = true;
                fresh.text = formatValue(v);
                fresh.isBool = v.type() == sim::Type::Bool;
                fresh.truthy = fresh.isBool && v.isTruthy();
                fresh.forced = rt->isForced(path);
            } else {
                fresh.text = "?";
            }
        } else {
            fresh.text = kDash;
        }
        // Un changement ne s'eclaire que si l'on regardait deja (une lecture recente).
        if (!quiet && l.ok && fresh.ok && fresh.text != l.text && now_ - l.readAt <= 1.0) {
            fresh.changedAt = now_;
            flashUntil_ = now_ + kFlash + 0.1;
            if (root >= 0 && static_cast<std::size_t>(root) < rootLive_.size()) rootLive_[static_cast<std::size_t>(root)].changedAt = now_;
        }
        const bool diff = fresh.text != l.text || fresh.forced != l.forced || fresh.ok != l.ok || fresh.changedAt != l.changedAt;
        l = std::move(fresh);
        return diff;
    };
    for (std::size_t i = 0; i < rows_.size() && i < live_.size(); ++i) {
        const auto& r = rows_[i];
        if (r.folder || !r.node.real || r.agg) continue;
        if (readOne(r.node.path, live_[i], r.root)) changed = true;
    }
    if (changed && table_) table_->invalidate();
    updateChips();
}

// L'index des cases : chacune sous sa racine, par l'adresse de sa valeur. Une
// fois par runtime (et quand la table des cases grandit : une adresse %MW
// touchee pour la premiere fois, une variable de boucle).
void SimulationPane::buildProbes() {
    probes_.clear();
    rootSlots_.assign(roots_.size(), 0);
    sweepAt_ = 0;
    passOpen_ = false;
    const auto* rt = runtime();
    probedRuntime_ = rt;
    probedGeneration_ = host() ? host()->generation() : ~std::uint64_t{0};
    probesBuiltAt_ = now_;
    probedSlots_ = rt ? rt->slotCount() : 0;
    if (!rt || roots_.empty()) return;
    // Les racines par leur nom en minuscules ("armoires", "unite.compteur") ; une
    // situee aussi par son adresse (le runtime lui donne une case de ce nom).
    std::unordered_map<std::string, std::uint32_t> byName;
    std::unordered_set<std::string> units;
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        const auto name = lower(roots_[i].name);
        byName.emplace(name, static_cast<std::uint32_t>(i));
        if (roots_[i].name != roots_[i].label) units.insert(name.substr(0, name.find('.')));
        if (!roots_[i].address.empty()) byName.emplace(lower(roots_[i].address), static_cast<std::uint32_t>(i));
    }
    probes_.reserve(probedSlots_);
    // Une seule chaine, reutilisee : 64 000 cases, pas 64 000 allocations.
    std::string key;
    const auto take = [&key](const std::string& name, std::size_t cut) {
        key.assign(name, 0, cut == std::string::npos ? name.size() : cut);
        for (auto& c : key) c = static_cast<char>(std::tolower(uc(c)));
    };
    rt->forEachSlot([&](const std::string& name, const sim::Value& value) {
        // La racine d'une case : son nom jusqu'au premier '.' ou '[' ; une locale,
        // les deux premiers (Unite.compteur) ; une adresse, tout son nom (%I0.1.3).
        std::size_t cut = name.empty() || name.front() == '%' ? std::string::npos : name.find_first_of(".[");
        take(name, cut);
        if (cut != std::string::npos && name[cut] == '.' && units.count(key)) take(name, name.find_first_of(".[", cut + 1));
        const auto it = byName.find(key);
        if (it == byName.end()) return;
        probes_.push_back(Probe{&value, fingerprint(value), it->second});
        ++rootSlots_[it->second];
    });
}

// Un morceau du tour des cases, a chaque image : un seizieme (4 096 au moins),
// pour que 64 000 lectures eparses ne tombent pas toutes sur la meme image. Un
// tour ne commence que si un cycle a tourne depuis le precedent : en pause,
// rien.
void SimulationPane::sweepStep(std::uint64_t scan) {
    const auto* rt = runtime();
    if (!rt) {
        probes_.clear();
        probedRuntime_ = nullptr;
        probedGeneration_ = ~std::uint64_t{0};
        return;
    }
    // Un autre runtime - meme ne a la meme adresse que l'ancien (detach puis
    // attach) : le numero le dit, et les cases de l'ancien ne sont plus lues.
    const std::uint64_t generation = host() ? host()->generation() : 0;
    if (rt != probedRuntime_ || generation != probedGeneration_
        || (rt->slotCount() != probedSlots_ && now_ - probesBuiltAt_ >= 5.0)) {
        buildProbes();          // les empreintes du moment : rien n'a encore change
        passScan_ = scan;
        return;
    }
    if (probes_.empty()) return;
    // Un Arret, un autre runtime : un tour complet sans rien compter.
    if (quietSweep_) {
        quietLeft_ = probes_.size();
        quietSweep_ = false;
    }
    if (!passOpen_) {
        if (scan == passScan_) return;
        passOpen_ = true;
        passScan_ = scan;
        sweepAt_ = 0;
    }
    // Longtemps sans regarder (l'onglet etait cache) : ce qui differe a change
    // quand personne ne regardait - pas « a l'instant ».
    if (now_ - lastChunk_ > 2.0) quietLeft_ = probes_.size();
    lastChunk_ = now_;
    const std::size_t end = std::min(probes_.size(), sweepAt_ + std::max<std::size_t>(4096, probes_.size() / 16));
    for (; sweepAt_ < end; ++sweepAt_) {
        auto& pr = probes_[sweepAt_];
        const bool quiet = quietLeft_ > 0;
        if (quiet) --quietLeft_;
        const auto print = fingerprint(*pr.value);
        if (print == pr.print) continue;
        pr.print = print;
        if (!quiet && pr.root < rootLive_.size()) rootLive_[pr.root].changedAt = now_;
    }
    if (sweepAt_ >= probes_.size()) passOpen_ = false;
}

// Ou sont les cases : les deux plus grosses racines, quand elles en tiennent une
// bonne part (« dont 32 000 dans armoires et 16 384 dans Grille »).
std::string SimulationPane::biggestRoots(std::size_t total) const {
    if (total == 0 || rootSlots_.size() != roots_.size()) return {};
    std::size_t first = kNpos, second = kNpos;
    for (std::size_t i = 0; i < rootSlots_.size(); ++i) {
        if (first == kNpos || rootSlots_[i] > rootSlots_[first]) {
            second = first;
            first = i;
        } else if (second == kNpos || rootSlots_[i] > rootSlots_[second]) {
            second = i;
        }
    }
    if (first == kNpos || rootSlots_[first] < 1000 || rootSlots_[first] * 5 < total) return {};
    std::string out = ", dont " + count(rootSlots_[first]) + " dans " + roots_[first].name;
    if (second != kNpos && rootSlots_[second] >= 1000 && rootSlots_[second] * 10 >= total)
        out += " et " + count(rootSlots_[second]) + " dans " + roots_[second].name;
    return out;
}

void SimulationPane::updateChips() {
    if (!filter_) return;
    std::size_t changing = 0, located = 0;
    std::vector<std::size_t> changingSet;
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        if (!roots_[i].address.empty()) ++located;
        if (rootPasses(i, ChChanging)) {
            ++changing;
            changingSet.push_back(i);
        }
    }
    std::vector<std::pair<std::string, std::size_t>> chips{{"Toutes", roots_.size()},
                                                           {"Forc\xC3\xA9" "es", forcedNames_.size()},
                                                           {"Qui changent", changing},
                                                           {"Situ\xC3\xA9" "es", located},
                                                           {"Suivies", watched_.size()}};
    if (chips != chipsShown_) {
        chipsShown_ = chips;
        filter_->setChips(std::move(chips));
    }
    // « Qui changent » choisie : ses lignes suivent (pas pendant une
    // reconstruction), toutes les deux secondes au plus - une liste qui se
    // refait a chaque lecture ne se lit plus.
    if (!rebuilding_ && filter_->current() == ChChanging && changingSet != changingShown_ && now_ >= nextChanging_) rebuildRows();
}

// ------------------------------------------------------------- chaque image ----
void SimulationPane::tick(double now) {
    now_ = now;
    if (!shown()) return;
    checkRuntime();
    auto* h = host();
    const int state = h ? static_cast<int>(h->state()) : 0;
    if (state != stateSeen_) {
        const int previous = stateSeen_;
        stateSeen_ = state;
        onStateChanged(previous, state);
    }
    const std::uint64_t scan = h ? h->scanCount() : 0;
    // Le compteur a recule : un Arret (puis une marche) entre deux images, sans
    // que l'etat vu ait change - les valeurs initiales ne sont pas des changements.
    if (lastScan_ != ~std::uint64_t{0} && scan < lastScan_) quietRead_ = quietSweep_ = true;
    if ((scan != lastScan_ || valuesDirty_) && now >= nextRead_) {
        const bool advanced = scan != lastScan_;
        lastScan_ = scan;
        valuesDirty_ = false;
        nextRead_ = now + kReadEvery;
        readValues();
        // Arreter vide les historiques du moteur : on redemande les suivies.
        if (auto* rt = runtime())
            for (const auto& w : watched_)
                if (!rt->history(w)) rt->watch(w);
        if (advanced) {
            band_->invalidate();
            if (trend_ && !trend_->frozen()) trend_->invalidate();
        }
    }
    // « Qui changent » : un morceau du tour de toutes les cases, a chaque image.
    sweepStep(scan);
    if (now >= nextPoll_) {
        nextPoll_ = now + 1.0;
        pollForcing();
        rebuildDiag();
        // La vitesse et la regle ont pu changer ailleurs (la barre du haut, un script).
        if (h) {
            const int speed = h->speed();
            band_->speed().setSelected(speed == 1 ? 0 : speed == 10 ? 1 : speed == 0 ? 2 : -1);
            policy_->setSelected(h->continueOnUnknownCalls() ? 1 : 0);
        }
        updateChips();
    }
    // Les eclairages s'eteignent : la table se redessine tant qu'il y en a (20
    // images par seconde, pas plus).
    if (now < flashUntil_ && now >= nextPaint_) {
        nextPaint_ = now + 0.05;
        table_->invalidate();
    }
    updateTools();
}

void SimulationPane::checkRuntime() {
    auto* h = host();
    if (h != hostSeen_) {
        hostLinks_.clear();
        hostSeen_ = h;
        // Chaque cycle qui dit quelque chose, meme onglet cache : le moteur ne dit
        // un avertissement qu'une fois.
        if (h)
            hostLinks_ += h->scanned->connect([this](const sim::ScanReport& report) {
                if (!report.diagnostics.empty()) noteDiagnostics(report.diagnostics);
            });
    }
    auto* rt = runtime();
    const std::uint64_t generation = h ? h->generation() : 0;
    if (rt == runtimeSeen_ && generation == generationSeen_) return;
    runtimeSeen_ = rt;
    generationSeen_ = generation;
    lastSeen_.clear();
    seenDiags_.clear();
    newerBlock_.clear();
    newerVersion_.clear();
    quietRead_ = quietSweep_ = true;
    valuesDirty_ = true;
    nextRead_ = 0.0;
    if (rt) {
        attachError_.clear();
        // L'historique doit couvrir la fenetre de la courbe (30 s, a la periode de MAST).
        const auto interval = std::max<std::int64_t>(1, h ? h->scanIntervalMs() : 20);
        rt->setHistoryDepth(static_cast<std::size_t>(std::clamp<std::int64_t>(kTrendMs / interval + 64, 2000, 40000)));
        for (const auto& w : watched_) rt->watch(w);
        if (h) noteDiagnostics(h->lastDiagnostics());
    }
    pollForcing();
    rebuildDiag();
    if (band_) band_->invalidate();
}

void SimulationPane::onStateChanged(int previous, int next) {
    using S = SimulationHost::State;
    const auto st = static_cast<S>(next);
    if (st == S::Stopped && previous >= 0 && static_cast<S>(previous) != S::Stopped) {
        // Arreter remet tout aux valeurs initiales : pas d'eclairage pour ca, et
        // la halte d'avant est oubliee (ses avertissements, non : ils restent vrais).
        quietRead_ = quietSweep_ = true;
        seenDiags_.erase(std::remove_if(seenDiags_.begin(), seenDiags_.end(),
                                        [](const SeenDiag& d) { return d.severity == static_cast<int>(sim::Diagnostic::Severity::Error); }),
                         seenDiags_.end());
    }
    if (st != S::Halted) {
        newerBlock_.clear();
        newerVersion_.clear();
    }
    valuesDirty_ = true;
    nextRead_ = 0.0;
    band_->speed().setVisibility(st == S::Halted ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    band_->invalidate();
    rebuildDiag();
    updateTools();
}

void SimulationPane::noteDiagnostics(const std::vector<sim::Diagnostic>& list) {
    for (const auto& d : list) {
        const int severity = static_cast<int>(d.severity);
        const bool known = std::any_of(seenDiags_.begin(), seenDiags_.end(), [&](const SeenDiag& s) {
            return s.severity == severity && s.line == d.line && s.section == d.section && s.message == d.message;
        });
        if (known) continue;
        if (seenDiags_.size() >= 200) break;
        seenDiags_.push_back(SeenDiag{severity, d.message, d.section, d.line});
    }
}

void SimulationPane::updateTools() {
    if (!frame_) return;
    auto& t = frame_->tools();
    auto* h = host();
    const bool running = h && h->state() == SimulationHost::State::Running;
    const std::string label = running ? "Pause" : "Simuler";
    if (label != runLabel_) {
        runLabel_ = label;
        t.setText(ARun,
                  running ? "Mettre en pause : le programme s'arr\xC3\xAAte o\xC3\xB9 il est, les valeurs restent lisibles"
                          : "Lancer la simulation : le programme de MAST tourne, cycle apr\xC3\xA8s cycle",
                  label);
    }
    if (forcedNames_.size() != releaseAllShown_) {
        releaseAllShown_ = forcedNames_.size();
        t.setText(AReleaseAll, "Rendre toutes les variables forc\xC3\xA9" "es au programme",
                  "Rel\xC3\xA2" "cher tout (" + std::to_string(releaseAllShown_) + ")");
    }
}

// ------------------------------------------------------------- la colonne droite ----
void SimulationPane::refreshSide() {
    pollForcing();
    rebuildDiag();
    updateChips();
    if (trend_) trend_->invalidate();
    invalidate();
}

void SimulationPane::pollForcing() {
    auto* rt = runtime();
    auto names = rt ? rt->forcedNames() : std::vector<std::string>{};
    std::vector<std::string> values;
    values.reserve(names.size());
    for (const auto& n : names) {
        sim::Value v;
        values.push_back(rt && rt->get(n, v) ? formatValue(v) : std::string("?"));
    }
    if (names == forcedNames_ && values == forcedValues_) return;
    const bool countChanged = names.size() != forcedNames_.size();
    const bool setChanged = names != forcedNames_;
    forcedNames_ = std::move(names);
    forcedValues_ = std::move(values);
    if (forcedModel_) forcedModel_->modelReset->emit();
    if (countChanged) invalidateLayout();           // la liste grandit (six lignes au plus)
    valuesDirty_ = true;
    nextRead_ = 0.0;
    updateChips();
    updateTools();
    if (setChanged && filter_ && filter_->current() == ChForced && !rebuilding_) rebuildRows();
    invalidate();
}

void SimulationPane::rebuildDiag() {
    std::vector<DiagItem> items;
    auto* h = host();
    auto* rt = runtime();
    const bool carryOn = h ? h->continueOnUnknownCalls() : true;
    // 1. La halte : pourquoi, et ou.
    if (h && h->state() == SimulationHost::State::Halted) {
        std::string section;
        std::uint32_t line = 0;
        DiagItem it;
        it.tone = ui::Tone::Error;
        it.title = haltReason(&section, &line, nullptr);
        it.detail = "Halte au cycle " + count(static_cast<std::size_t>(rt ? rt->scanCount() : 0));
        if (!section.empty()) it.detail += kDot + frenchPlace(section, line);
        it.section = section;
        it.line = line;
        it.link = !section.empty();
        items.push_back(std::move(it));
    }
    // 2. Les fonctions qu'il ne connait pas (notees tant qu'il continue dessus ;
    //    la regle changee pour « s'arreter », le prochain appel arrete le cycle).
    if (rt)
        for (const auto& u : rt->unknownCalls()) {
            DiagItem it;
            it.tone = ui::Tone::Warning;
            it.title = u.name + " n'est pas simul\xC3\xA9" "e";
            it.detail = (u.section.empty() ? std::string{} : u.section + kDot) + plural(static_cast<std::size_t>(u.calls), "appel", "appels")
                      + kDot + (carryOn ? "elle rend 0 et le cycle continue" : "elle n'est plus ignor\xC3\xA9" "e et arr\xC3\xAAte le cycle");
            it.section = u.section;
            it.line = u.line;
            it.link = !u.section.empty();
            items.push_back(std::move(it));
        }
    // 3. Ce que les cycles ont dit (la halte est deja en tete).
    for (const auto& d : seenDiags_) {
        if (d.severity == static_cast<int>(sim::Diagnostic::Severity::Error)) continue;
        DiagItem it;
        it.tone = d.severity == static_cast<int>(sim::Diagnostic::Severity::Warning) ? ui::Tone::Warning : ui::Tone::Info;
        it.title = frenchMessage(d.message);
        it.detail = d.section.empty() ? std::string("pendant un cycle") : frenchPlace(d.section, d.line);
        it.section = d.section;
        it.line = d.line;
        it.link = !d.section.empty();
        items.push_back(std::move(it));
    }
    // 4. Ce que la preparation a dit : une section en LD, un DFB en FBD, du ST illisible.
    std::size_t refused = 0;
    std::string refusedNames;
    if (rt)
        for (const auto& d : rt->preparationDiagnostics()) {
            if (d.message.find("declares more than") != std::string::npos) {
                ++refused;
                const auto a = d.message.find('\''), z = d.message.find('\'', a == std::string::npos ? 0 : a + 1);
                if (a != std::string::npos && z != std::string::npos && refused <= 4)
                    refusedNames += (refusedNames.empty() ? "" : ", ") + d.message.substr(a + 1, z - a - 1);
                continue;
            }
            DiagItem it;
            it.tone = d.severity == sim::Diagnostic::Severity::Error ? ui::Tone::Error : ui::Tone::Warning;
            it.title = frenchMessage(d.message);
            // Une erreur de lecture du ST porte sa ligne dans son texte (le
            // diagnostic dit 0) : « Voir » y va quand meme.
            const auto line = d.line != 0 ? d.line : lineInMessage(d.message);
            it.detail = d.section.empty() ? std::string("\xC3\xA0 la pr\xC3\xA9paration") : frenchPlace(d.section, line);
            it.section = d.section;
            it.line = line;
            it.link = !d.section.empty();
            items.push_back(std::move(it));
        }
    // 5. Le reste, qui marche (tout, si rien n'est dit au-dessus).
    if (rt) {
        DiagItem it;
        it.tone = ui::Tone::Ok;
        it.title = std::string(items.empty() ? "Tout est simul\xC3\xA9 : " : "Le reste est simul\xC3\xA9 : ") + count(rt->slotCount())
                 + " cases" + biggestRoots(rt->slotCount());
        it.detail = refused == 0 ? std::string("aucun tableau refus\xC3\xA9 (plus de 100 000 \xC3\xA9l\xC3\xA9ments)")
                                 : plural(refused, "tableau refus\xC3\xA9", "tableaux refus\xC3\xA9s")
                                       + " (plus de 100 000 \xC3\xA9l\xC3\xA9ments) : " + refusedNames + (refused > 4 ? kEllipsis : "");
        items.push_back(std::move(it));
    } else {
        DiagItem it;
        it.tone = ui::Tone::Info;
        it.title = "La simulation n'est pas pr\xC3\xA9par\xC3\xA9" "e";
        it.detail = "Simuler la pr\xC3\xA9pare : ce que le simulateur ne sait pas faire s'\xC3\xA9" "crira ici.";
        items.push_back(std::move(it));
    }

    const auto same = [](const DiagItem& a, const DiagItem& b) {
        return a.tone == b.tone && a.title == b.title && a.detail == b.detail && a.section == b.section && a.line == b.line && a.link == b.link;
    };
    bool changed = items.size() != diagItems_.size();
    for (std::size_t i = 0; !changed && i < items.size(); ++i) changed = !same(items[i], diagItems_[i]);
    if (!changed) return;
    diagItems_ = std::move(items);
    diagLines_.clear();
    for (const auto& it : diagItems_) diagLines_.push_back(it.detail.empty() ? it.title : it.title + kDot + it.detail);
    if (diagModel_) diagModel_->modelReset->emit();
    invalidate();
}

std::vector<std::string> SimulationPane::diagnosticLines() const { return diagLines_; }

void SimulationPane::goToItem(std::size_t item) {
    if (item >= diagItems_.size() || !diagItems_[item].link || !hosts_.goToLine) return;
    const std::string section = diagItems_[item].section;
    const auto line = diagItems_[item].line;
    hosts_.goToLine(section, line);
}

// ------------------------------------------------------------------ l'etat ----
std::string SimulationPane::haltReason(std::string* section, std::uint32_t* line, std::string* block) const {
    auto* h = host();
    if (!h || h->state() != SimulationHost::State::Halted) return {};
    std::string reason, where;
    std::uint32_t at = 0;
    for (const auto& d : h->lastDiagnostics())
        if (d.severity == sim::Diagnostic::Severity::Error) {
            reason = frenchMessage(d.message);
            where = d.section;
            at = d.line != 0 ? d.line : lineInMessage(d.message);
            break;
        }
    if (reason.empty()) {
        // « Cycle 12 arrete : ... » : le cycle se dit a part.
        reason = frenchMessage(h->haltMessage());
        if (startsWith(reason, "Cycle ")) {
            const auto colon = reason.find(" : ");
            reason = colon == std::string::npos ? std::string("le cycle s'est arr\xC3\xAAt\xC3\xA9") : reason.substr(colon + 3);
        }
    }
    if (section) *section = where;
    if (line) *line = at;
    if (block) {
        // Le corps d'un DFB : la section s'appelle « Bloc.Section ».
        const auto dot = where.find('.');
        *block = dot == std::string::npos || dot == 0 ? std::string{} : where.substr(0, dot);
    }
    return reason;
}

SimulationPane::BandInfo SimulationPane::bandInfo() const {
    BandInfo b;
    auto* h = host();
    auto* rt = runtime();
    b.attached = rt != nullptr;
    b.cycle = rt ? rt->scanCount() : 0;
    b.clockMs = rt ? rt->clockMs() : 0;
    b.intervalMs = h ? h->scanIntervalMs() : 20;
    b.scanMicros = h ? h->lastScanMicros() : 0;
    if (!b.attached && !attachError_.empty()) {
        b.kind = 4;
        b.word = "PAS PR\xC3\x8AT" "E";
        b.reason = attachError_;
        return b;
    }
    switch (h ? h->state() : SimulationHost::State::Stopped) {
        case SimulationHost::State::Running:
            b.kind = 1;
            b.word = "EN MARCHE";
            break;
        case SimulationHost::State::Paused:
            b.kind = 2;
            b.word = "EN PAUSE";
            // Lot API 8 (le moteur) : arretee par un point d'arret - ou, en orange.
            if (h && h->lastBreakHit()) {
                const auto& hit = *h->lastBreakHit();
                b.breakpoint = true;
                b.word = "POINT D'ARR\xC3\x8AT";
                b.section = hit.section;
                b.line = hit.line > 0 ? static_cast<std::uint32_t>(hit.line) : 0u;
                b.place = frenchPlace(b.section, b.line);
            }
            break;
        case SimulationHost::State::Halted: {
            b.kind = 3;
            b.word = "HALTE";
            b.reason = haltReason(&b.section, &b.line, &b.block);
            b.place = frenchPlace(b.section, b.line);
            for (const auto& d : h->lastDiagnostics())
                if (d.severity == sim::Diagnostic::Severity::Error) {
                    b.unknownFunction = d.message.find("is not a function or a block") != std::string::npos;
                    break;
                }
            if (b.unknownFunction && h->continueOnUnknownCalls()) b.unknownFunction = false;
            // La bibliotheque : demandee une fois par halte (elle se lit sur le disque).
            if (!b.block.empty() && hosts_.newerInLibrary) {
                if (b.block != newerBlock_) {
                    newerBlock_ = b.block;
                    newerVersion_ = hosts_.newerInLibrary(b.block);
                }
                b.newer = newerVersion_;
            }
            break;
        }
        default:
            b.kind = 0;
            b.word = "ARR\xC3\x8AT\xC3\x89" "E";
            break;
    }
    return b;
}

std::string SimulationPane::stateLine() const {
    const auto b = bandInfo();
    switch (b.kind) {
        case 1:
        case 2: return b.word + (b.breakpoint && !b.place.empty() ? kDot + b.place : std::string{})   // lot API 8 : ou
                       + kDot + "cycle " + std::to_string(b.cycle);
        case 3: return "HALTE : " + b.reason + (b.place.empty() ? std::string{} : kDot + b.place);
        case 4: return b.word + " : " + b.reason;
        default: return b.cycle > 0 ? b.word + kDot + "cycle " + std::to_string(b.cycle) : b.word;
    }
}

// Lot API 8 (le moteur) : les memes regles que Band::onPaint (le ton du bandeau).
std::string SimulationPane::bandColor() const {
    const auto b = bandInfo();
    switch (b.kind) {
        case 1: return "vert";
        case 2: return b.breakpoint ? "orange" : "bleu";
        case 3: return "rouge";
        case 4: return "orange";
        default: return "gris";
    }
}

void SimulationPane::bandLink(const std::string& key) {
    auto* h = host();
    if (key == "arreter") {
        transport(AStop);
    } else if (key == "ligne") {
        std::string section;
        std::uint32_t line = 0;
        (void)haltReason(&section, &line, nullptr);
        if (!section.empty() && hosts_.goToLine) hosts_.goToLine(section, line);
    } else if (key == "bibliotheque") {
        if (hosts_.request) hosts_.request("bibliotheque");
    } else if (key == "continuer" && h) {
        // Les valeurs d'une halte ne sont plus sures : on repart de zero, la
        // fonction inconnue rendant 0 cette fois.
        setUnknownPolicy(true);
        h->setState(SimulationHost::State::Stopped);
        h->setState(SimulationHost::State::Running);
        status("Relanc\xC3\xA9" "e depuis z\xC3\xA9ro : une fonction inconnue rend 0 et le cycle continue (la liste \xC2\xAB Le simulateur \xC2\xBB les dit).");
        quietRead_ = quietSweep_ = true;          // les valeurs initiales ne sont pas des changements
        valuesDirty_ = true;
        nextRead_ = 0.0;
        updateTools();
    }
    if (band_) band_->invalidate();
}

// -------------------------------------------------------------- la marche ----
bool SimulationPane::ensureAttached() {
    auto* h = host();
    if (!h) {
        status("La simulation n'est pas disponible ici.");
        return false;
    }
    // Lot API 7 : prepare avant un changement du programme (une section, un
    // bloc mis a jour) et pas en marche : le nouveau code, pas l'ancien.
    if (h->attached() && !(h->stale() && h->state() != SimulationHost::State::Running)) return true;
    std::string why;
    if (!hosts_.attach || !hosts_.attach(&why)) {
        attachError_ = why.empty() ? std::string("le projet n'a rien \xC3\xA0 simuler") : frenchMessage(why);
        status("La simulation ne d\xC3\xA9marre pas : " + attachError_ + ".");
        if (band_) band_->invalidate();
        return false;
    }
    attachError_.clear();
    checkRuntime();
    return true;
}

void SimulationPane::transport(int action) {
    auto* h = host();
    if (!h) {
        status("La simulation n'est pas disponible ici.");
        return;
    }
    using S = SimulationHost::State;
    if (action == AStop) {
        if (!h->attached()) return;
        h->setState(S::Stopped);
        status("Simulation arr\xC3\xAAt\xC3\xA9" "e : tout repart des valeurs initiales ; les for\xC3\xA7" "ages restent.");
    } else {
        if (!ensureAttached()) return;
        if (action == AStep) {
            if (h->state() == S::Halted) {
                status("Halte : Arr\xC3\xAAter d'abord, puis Un cycle repart de z\xC3\xA9ro.");
                return;
            }
            h->step();
        } else if (h->state() == S::Running) {
            h->setState(S::Paused);
        } else {
            if (h->state() == S::Halted) {
                // Apres une halte, les valeurs ne sont plus sures : on repart de
                // zero, comme l'automate apres un STOP puis un RUN (et le retour
                // aux valeurs initiales n'eclaire rien).
                h->setState(S::Stopped);
                quietRead_ = quietSweep_ = true;
                status("Relanc\xC3\xA9" "e depuis z\xC3\xA9ro : apr\xC3\xA8s une halte, les valeurs n'\xC3\xA9taient plus s\xC3\xBB" "res.");
            }
            h->setState(S::Running);
        }
    }
    checkRuntime();
    valuesDirty_ = true;
    nextRead_ = 0.0;
    const int state = static_cast<int>(h->state());
    if (state != stateSeen_) {
        const int previous = stateSeen_;
        stateSeen_ = state;
        onStateChanged(previous, state);
    }
    if (action == AStep) {
        readValues();
        lastScan_ = h->scanCount();
        if (trend_) trend_->invalidate();
    }
    band_->invalidate();
    updateTools();
}

void SimulationPane::setSpeed(int factor) {
    auto* h = host();
    if (h) h->setSpeed(factor);
    band_->speed().setSelected(factor == 1 ? 0 : factor == 10 ? 1 : factor == 0 ? 2 : -1);
    band_->invalidate();
    if (factor == 0) status("Au plus vite : autant de cycles que l'image en laisse.");
    else if (factor == 10) status("Dix fois plus vite que le temps r\xC3\xA9" "el.");
    else if (factor == 1)
        status("Le temps r\xC3\xA9" "el : un cycle toutes les " + std::to_string(h ? h->scanIntervalMs() : 20) + " ms.");
}

void SimulationPane::setUnknownPolicy(bool continueWithZero) {
    auto* h = host();
    const bool was = h ? h->continueOnUnknownCalls() : continueWithZero;
    if (h) h->setContinueOnUnknownCalls(continueWithZero);
    if (policy_) policy_->setSelected(continueWithZero ? 1 : 0);
    rebuildDiag();
    if (band_) band_->invalidate();
    if (was != continueWithZero)
        status(continueWithZero ? "Une fonction inconnue rend maintenant 0 et le cycle continue (elle est list\xC3\xA9" "e dans \xC2\xAB Le simulateur \xC2\xBB)."
                                : "Une fonction inconnue arr\xC3\xAAte maintenant le cycle, qui dit o\xC3\xB9.");
}

// -------------------------------------------------------------- les actions ----
void SimulationPane::runAction(int action) {
    switch (action) {
        case ARun:
        case AStep:
        case AStop:
            transport(action);
            return;
        case AForce: {
            // Toutes les variables a valeur choisies : une seule valeur demandee.
            std::vector<std::string> paths;
            for (const auto r : selectedRows())
                if (!rows_[r].folder && rows_[r].node.real && !rows_[r].agg) paths.push_back(rows_[r].node.path);
            if (paths.empty()) {
                status("Choisis une variable \xC3\xA0 valeur (pas une structure) : double-clic sur sa valeur la force aussi.");
                return;
            }
            askForce(std::move(paths));
            return;
        }
        case ARelease: {
            std::vector<std::string> paths;
            for (const auto r : selectedRows())
                if (r < live_.size() && live_[r].forced) paths.push_back(rows_[r].node.path);
            if (paths.empty() && forced_)
                for (const auto r : forced_->selectedModelRows())
                    if (r < forcedNames_.size()) paths.push_back(forcedNames_[r]);
            if (paths.empty()) {
                status("Rien de forc\xC3\xA9 dans ce que tu as choisi.");
                return;
            }
            releasePaths(paths);
            return;
        }
        case AReleaseAll: {
            auto* rt = runtime();
            if (!rt) return;
            const auto n = forcedNames_.size();
            rt->unforceAll();
            pollForcing();
            readValues();
            if (trend_) trend_->invalidate();          // les pointilles des forcees s'en vont
            status(n == 0 ? std::string("Rien n'\xC3\xA9tait forc\xC3\xA9.")
                          : plural(n, "variable rel\xC3\xA2" "ch\xC3\xA9" "e", "variables rel\xC3\xA2" "ch\xC3\xA9" "es") + " : le programme d\xC3\xA9" "cide de tout.");
            return;
        }
        case AWatch: {
            std::vector<std::string> paths;
            for (const auto r : selectedRows())
                if (!rows_[r].folder && rows_[r].node.real && !rows_[r].agg) paths.push_back(rows_[r].node.path);
            toggleWatch(paths);
            return;
        }
        case AAddToTable: {
            std::vector<std::string> names;
            for (const auto r : selectedRows())
                if (!rows_[r].folder && rows_[r].node.real) names.push_back(tablePath(rows_[r]));
            if (names.empty()) {
                status("Choisis des variables : elles s'ajoutent \xC3\xA0 une table d'animation.");
                return;
            }
            if (hosts_.addToTable) hosts_.addToTable(std::move(names));
            return;
        }
        case AExport: exportForcing(); return;
        case AImport: importForcing(); return;
        case AHelp:
            if (hosts_.request) hosts_.request("aide");
            return;
        default: return;
    }
}

void SimulationPane::menuChosen(int id) {
    if (id == kMenuCopy) {
        (void)table_->copySelection(true);
        return;
    }
    if (id == kMenuWriter) {
        const auto rows = selectedRows();
        if (rows.size() == 1 && !rows_[rows.front()].section.empty() && hosts_.goToLine) {
            const std::string section = rows_[rows.front()].section;
            const auto line = rows_[rows.front()].line;
            hosts_.goToLine(section, line);
        }
        return;
    }
    runAction(id);
}

void SimulationPane::openMenu(gfx::Point at) {
    const auto rows = selectedRows();
    bool anyForced = false, anyReal = false, allWatched = true, anyValue = false;
    for (const auto r : rows) {
        const auto& row = rows_[r];
        if (row.folder || !row.node.real) continue;
        anyReal = true;
        if (!row.agg) {
            anyValue = true;
            if (std::find(watched_.begin(), watched_.end(), row.node.path) == watched_.end()) allWatched = false;
        }
        if (r < live_.size() && live_[r].forced) anyForced = true;
    }
    if (!anyValue) allWatched = false;
    const std::string noValue = "choisis une variable \xC3\xA0 valeur (pas une structure)";
    std::vector<ui::PopupMenu::Item> items;
    items.push_back({std::string("Forcer") + kEllipsis, "Double-clic", anyValue ? std::string{} : noValue, ui::Icon::Lock, anyValue, false, AForce});
    items.push_back({"Rel\xC3\xA2" "cher", "Suppr", anyForced ? std::string{} : std::string("rien de forc\xC3\xA9 ici"), ui::Icon::Close, anyForced, false, ARelease});
    items.push_back({allWatched ? "Ne plus suivre sur la courbe" : "Suivre sur la courbe", "", anyValue ? std::string{} : noValue,
                     ui::Icon::Chart, anyValue, false, AWatch});
    items.push_back({"Ajouter \xC3\xA0 une table d'animation", "", anyReal ? std::string{} : std::string("choisis une variable"),
                     ui::Icon::AnimationTable, anyReal && static_cast<bool>(hosts_.addToTable), false, AAddToTable});
    if (rows.size() == 1 && !rows_[rows.front()].section.empty()) {
        const auto& row = rows_[rows.front()];
        items.push_back({"", "", "", ui::Icon::None, true, true, -1});
        items.push_back({"Aller \xC3\xA0 l'\xC3\xA9" "criture", frenchPlace(row.section, row.line), "", ui::Icon::Section,
                         static_cast<bool>(hosts_.goToLine), false, kMenuWriter});
    }
    items.push_back({"", "", "", ui::Icon::None, true, true, -1});
    items.push_back({"Copier", "Ctrl+C", "", ui::Icon::None, !rows.empty(), false, kMenuCopy});
    menu_->setItems(std::move(items));
    // La fenetre qui peint ce volet (retenue au dessin) : ce peut etre une
    // fenetre detachee, plus petite que la principale. Pas encore peint : la
    // surface du moment (ui::SurfaceScope), sinon ses propres bornes.
    gfx::Size surface = surface_;
    if (surface.w <= 0.f || surface.h <= 0.f) surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {bounds().right(), bounds().bottom()};
    menu_->openAt(at, surface);
}

namespace {

// Ce qu'on tape pour forcer une case de ce type : le dialogue le dit.
std::string forceHint(sim::Type t) {
    switch (t) {
        case sim::Type::Bool:   return "TRUE ou FALSE (1 ou 0)";
        case sim::Type::Real:   return "un nombre : 12,5 ou 12.5, 1e-3";
        case sim::Type::Time:   return "une dur\xC3\xA9" "e : T#500ms, T#1s500ms, T#2m";
        case sim::Type::String: return "un texte, entre apostrophes ou non";
        default: {
            long long lo = 0, hi = 0;
            if (integerBounds(t, lo, hi)) return "un entier de " + thousands(lo) + " \xC3\xA0 " + thousands(hi) + " (16#FF, 2#1010 aussi)";
            return "TRUE, FALSE, un nombre, une dur\xC3\xA9" "e (T#500ms)";
        }
    }
}

} // namespace

void SimulationPane::askForce(std::vector<std::string> paths) {
    if (paths.empty() || !ensureAttached()) return;
    auto* rt = runtime();
    if (!rt) return;
    sim::Value current;
    if (!rt->get(paths.front(), current)) {
        status(paths.front() + " : la simulation ne la conna\xC3\xAEt pas.");
        return;
    }
    if (!hosts_.ask) {
        status("Forcer demande une valeur : ce volet n'a pas de dialogue.");
        return;
    }
    // Plusieurs variables choisies : une seule valeur, lue dans le type de chacune.
    const std::string title = paths.size() == 1 ? "Forcer " + paths.front()
                                                : "Forcer " + plural(paths.size(), "variable", "variables");
    std::string text = "Valeur (" + forceHint(current.type()) + ") : le programme ne peut plus ";
    text += paths.size() == 1 ? "la changer, Rel\xC3\xA2" "cher la lui rend." : "les changer, Rel\xC3\xA2" "cher les lui rend.";
    if (paths.size() > 1) text += " " + someNames(paths) + ".";
    hosts_.ask(title, text, literalOf(current),
               [this, targets = std::move(paths)](const std::string& typed) { (void)forcePaths(targets, typed); });
}

std::size_t SimulationPane::forcePaths(const std::vector<std::string>& paths, const std::string& typed) {
    auto* rt = runtime();
    if (!rt) {
        status("La simulation n'est pas pr\xC3\xAAte : Simuler la pr\xC3\xA9pare.");
        return 0;
    }
    std::size_t done = 0;
    std::string lastPath, lastValue, refused;
    for (const auto& path : paths) {
        sim::Value current;
        if (!rt->get(path, current)) {
            if (refused.empty()) refused = path + " : inconnue de la simulation";
            continue;
        }
        sim::Value v;
        std::string why;
        if (!parseForce(typed, current.type(), v, why)) {
            if (refused.empty()) refused = path + " n'est pas forc\xC3\xA9" "e : " + why;
            continue;
        }
        if (!rt->force(path, v)) {
            if (refused.empty()) refused = path + " : la simulation refuse de la forcer";
            continue;
        }
        sim::Value now;
        (void)rt->get(path, now);
        lastPath = path;
        lastValue = formatValue(now);
        ++done;
    }
    // Une seule relecture pour toutes (la liste des forcages parcourt toutes les cases).
    if (done > 0) {
        pollForcing();
        readValues();
        if (trend_) trend_->invalidate();
    }
    if (done == 1 && paths.size() == 1)
        status(lastPath + " forc\xC3\xA9" "e \xC3\xA0 " + lastValue + " : le programme ne peut plus la changer (Rel\xC3\xA2" "cher la lui rend).");
    else if (done > 0)
        status(plural(done, "variable forc\xC3\xA9" "e", "variables forc\xC3\xA9" "es") + " \xC3\xA0 \xC2\xAB " + trim(typed) + " \xC2\xBB"
               + (refused.empty() ? std::string(".") : " ; " + refused + "."));
    else
        status(refused + ".");
    return done;
}

bool SimulationPane::forceSelected(const std::string& typed) {
    std::vector<std::string> paths;
    for (const auto r : selectedRows())
        if (!rows_[r].folder && rows_[r].node.real && !rows_[r].agg) paths.push_back(rows_[r].node.path);
    if (paths.empty() || !ensureAttached()) return false;
    return forcePaths(paths, typed) > 0;
}

void SimulationPane::releasePaths(const std::vector<std::string>& paths) {
    auto* rt = runtime();
    if (!rt) return;
    std::size_t n = 0;
    for (const auto& path : paths)
        if (rt->isForced(path) && rt->unforce(path)) ++n;
    pollForcing();
    readValues();
    if (trend_) trend_->invalidate();
    status(n == 0 ? std::string("Rien de forc\xC3\xA9 dans ce que tu as choisi.")
           : n == 1 ? paths.front() + std::string(" rel\xC3\xA2" "ch\xC3\xA9" "e : le programme la reprend.")
                    : plural(n, "variable rel\xC3\xA2" "ch\xC3\xA9" "e", "variables rel\xC3\xA2" "ch\xC3\xA9" "es") + " : le programme les reprend.");
}

void SimulationPane::toggleWatch(const std::vector<std::string>& paths) {
    if (paths.empty()) {
        status("Choisis une variable \xC3\xA0 valeur (pas une structure) : elle se trace sur la courbe.");
        return;
    }
    auto* rt = runtime();
    const auto isWatched = [&](const std::string& p) { return std::find(watched_.begin(), watched_.end(), p) != watched_.end(); };
    if (std::all_of(paths.begin(), paths.end(), isWatched)) {
        // Rien n'est rendu au moteur (Runtime::unwatch) : une table d'animation
        // peut suivre la meme variable, et son historique partirait avec. Un
        // historique garde coute une lecture par cycle.
        for (const auto& p : paths) watched_.erase(std::remove(watched_.begin(), watched_.end(), p), watched_.end());
        status(paths.size() == 1 ? paths.front() + " n'est plus suivie sur la courbe."
                                 : plural(paths.size(), "variable", "variables") + " ne sont plus suivies sur la courbe.");
    } else {
        std::vector<std::string> added;
        std::size_t refused = 0;
        for (const auto& p : paths) {
            if (isWatched(p)) continue;
            if (watched_.size() >= kMaxWatched) {
                ++refused;
                continue;
            }
            watched_.push_back(p);
            if (rt) rt->watch(p);
            added.push_back(p);
        }
        std::string msg;
        if (added.empty())
            msg = "Six courbes au plus : ne plus en suivre une d'abord (clic droit sur elle, Ne plus suivre sur la courbe).";
        else if (added.size() == 1)
            msg = added.front() + " suivie sur la courbe (elle se trace au prochain cycle).";
        else
            msg = plural(added.size(), "variable suivie", "variables suivies") + " sur la courbe.";
        if (!added.empty() && refused > 0)
            msg += " " + plural(refused, "autre refus\xC3\xA9" "e", "autres refus\xC3\xA9" "es") + " : six courbes au plus.";
        status(msg);
    }
    updateChips();
    if (trend_) trend_->invalidate();
    if (filter_ && filter_->current() == ChWatched) rebuildRows();
    invalidate();
}

// ---------------------------------------------------------- exporter, importer ----
namespace {

// Un chemin tape dans l'interface (UTF-8) : Windows lirait sinon ses accents
// dans la page de code du systeme.
std::filesystem::path pathOfUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// Un chemin dit a l'interface : en UTF-8.
std::string utf8Of(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Le fichier demande : un nom (dans le dossier simulation du projet) ou un
// chemin complet ; sans extension, .txt.
std::filesystem::path resolveFile(const std::filesystem::path& folder, const std::string& answer) {
    std::string name = trim(answer);
    if (name.empty()) name = "forcages.txt";
    auto p = pathOfUtf8(name);
    if (!p.is_absolute()) p = folder / p;
    if (!p.has_extension()) p += ".txt";
    return p;
}

// Une valeur telle que le fichier la garde, et qu'elle se relit a l'identique
// (ici comme par Runtime::importForcing) : TRUE, 42, 18094.123, T#1500ms, 'texte'.
std::string fileLiteral(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool:    return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real:    return exactReal(v.asReal(), 17);
        case sim::Type::Time:    return "T#" + std::to_string(v.asInteger()) + "ms";
        case sim::Type::String:  return "'" + v.asString() + "'";
        case sim::Type::Unknown: return v.display();
        default:                 return std::to_string(v.asInteger());
    }
}

// Les forcages en texte : une ligne « nom = valeur » par variable - un scenario
// qui se garde avec le projet, se relit et se corrige dans un editeur de texte.
std::string forcingText(const sim::Runtime& rt, const std::vector<std::string>& names, std::size_t& written) {
    std::string out = "# Forcages de la simulation (XpgAnalyzer) : une variable par ligne, nom = valeur.\n"
                      "# TRUE / FALSE, 42, 1.5, T#500ms, 'texte'. API > Simulation > Importer... les rappelle ;\n"
                      "# un nom qui n'existe plus est dit, pas ignore.\n";
    written = 0;
    for (const auto& name : names) {
        sim::Value v;
        if (!rt.get(name, v)) continue;
        out += name + " = " + fileLiteral(v) + "\n";
        ++written;
    }
    return out;
}

// Relire des forcages : chaque valeur lue DANS LE TYPE DE SA CASE (parseForce) -
// Runtime::importForcing lisait "1e-05" comme l'entier 1 et "T#1s500ms" comme
// 1 ms. `unknown` : les noms que la simulation ne connait pas ; `unreadable` :
// les lignes dont la valeur ne se lit pas (ou que la case refuse).
std::size_t applyForcing(sim::Runtime& rt, std::string_view text, std::vector<std::string>& unknown,
                         std::vector<std::string>& unreadable) {
    if (startsWith(text, "\xEF\xBB\xBF")) text.remove_prefix(3);      // le BOM d'un fichier UTF-8
    std::size_t applied = 0, from = 0;
    while (from < text.size()) {
        const auto nl = text.find('\n', from);
        const auto end = nl == std::string_view::npos ? text.size() : nl;
        const auto line = trim(text.substr(from, end - from));
        from = end + 1;
        if (line.empty() || line.front() == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            unreadable.push_back(line);
            continue;
        }
        const auto name = trim(std::string_view(line).substr(0, eq));
        const auto typed = trim(std::string_view(line).substr(eq + 1));
        sim::Value current;
        if (name.empty() || !rt.get(name, current)) {
            unknown.push_back(name.empty() ? line : name);
            continue;
        }
        sim::Value v;
        std::string why;
        if (!parseForce(typed, current.type(), v, why) || !rt.force(name, v)) {
            unreadable.push_back(name);
            continue;
        }
        ++applied;
    }
    return applied;
}

} // namespace

std::string SimulationPane::simulationFolder() const {
    const std::string base = hosts_.folder ? hosts_.folder() : std::string{};
    std::error_code ec;
    // Le dossier du projet tel que l'appli le garde (sa page de code), comme ses
    // exports ; sans projet enregistre : le dossier courant.
    const std::filesystem::path root = base.empty() ? std::filesystem::current_path(ec) : std::filesystem::path(base);
    return (root / "simulation").string();
}

void SimulationPane::exportForcing() {
    auto* rt = runtime();
    if (!rt || forcedNames_.empty()) {
        pollForcing();
        if (!rt || forcedNames_.empty()) {
            status("Rien n'est forc\xC3\xA9 : rien \xC3\xA0 exporter.");
            return;
        }
    }
    if (!hosts_.ask && !hosts_.askPath) {
        status("Exporter demande un nom de fichier : ce volet n'a pas de dialogue.");
        return;
    }
    const std::filesystem::path folder(simulationFolder());
    // UN CHEMIN, pas seulement un nom : simulation/forcages.txt du projet par
    // defaut, ailleurs avec le bouton ... (l'explorateur, "Enregistrer sous").
    const std::string title = "Exporter les for\xC3\xA7" "ages";
    const std::string initial = utf8Of(folder / "forcages.txt");
    const std::string question = "Une ligne \xC2\xAB nom = valeur \xC2\xBB par variable forc\xC3\xA9" "e. Le chemin se tape, ou se choisit avec "
                             + std::string(kEllipsis) + " (l'explorateur) ; un nom seul va dans " + utf8Of(folder) + ".";
    auto write = [this, folder](const std::string& answer) {
                   auto* r = runtime();
                   if (!r) return;
                   const auto target = resolveFile(folder, answer);
                   std::error_code err;
                   std::filesystem::create_directories(target.parent_path(), err);
                   std::size_t written = 0;
                   const auto text = forcingText(*r, r->forcedNames(), written);
                   std::ofstream out(target, std::ios::binary | std::ios::trunc);
                   if (out) out << text;
                   if (out) out.close();
                   if (!out) {
                       status("Impossible d'\xC3\xA9" "crire " + utf8Of(target) + ".");
                       return;
                   }
                   status(plural(written, "for\xC3\xA7" "age export\xC3\xA9", "for\xC3\xA7" "ages export\xC3\xA9s") + " dans " + utf8Of(target)
                          + " : Importer" + kEllipsis + " les rappelle.");
               };
    if (hosts_.askPath) hosts_.askPath(title, question, initial, ui::saveFile("For\xC3\xA7" "ages|*.txt", initial, title), std::move(write));
    else hosts_.ask(title, question, initial, std::move(write));
}

void SimulationPane::importForcing() {
    if (!project()) {
        status("Ouvre d'abord un projet : des for\xC3\xA7" "ages s'appliquent \xC3\xA0 sa simulation.");
        return;
    }
    if (!ensureAttached()) return;
    if (!hosts_.ask && !hosts_.askPath) {
        status("Importer demande un nom de fichier : ce volet n'a pas de dialogue.");
        return;
    }
    const std::filesystem::path folder(simulationFolder());
    // Un chemin : simulation/forcages.txt par defaut, un autre fichier avec le
    // bouton ... (l'explorateur).
    const std::string title = "Importer des for\xC3\xA7" "ages";
    const std::string initial = utf8Of(folder / "forcages.txt");
    const std::string question = "Une ligne \xC2\xAB nom = valeur \xC2\xBB par variable. Le chemin se tape, ou se choisit avec "
                             + std::string(kEllipsis) + " (l'explorateur) ; un nom seul se lit dans " + utf8Of(folder) + ".";
    auto read = [this, folder](const std::string& answer) {
                   auto* r = runtime();
                   if (!r) return;
                   const auto source = resolveFile(folder, answer);
                   std::ifstream in(source, std::ios::binary);
                   if (!in) {
                       status("Introuvable : " + utf8Of(source) + ".");
                       return;
                   }
                   std::ostringstream content;
                   content << in.rdbuf();
                   std::vector<std::string> unknown, unreadable;
                   const auto applied = applyForcing(*r, content.str(), unknown, unreadable);
                   pollForcing();
                   readValues();
                   if (trend_) trend_->invalidate();
                   std::string msg = plural(applied, "for\xC3\xA7" "age appliqu\xC3\xA9", "for\xC3\xA7" "ages appliqu\xC3\xA9s") + " ("
                                   + utf8Of(source.filename()) + ")";
                   if (!unknown.empty())
                       msg += " ; " + count(unknown.size()) + (unknown.size() == 1 ? " n'existe plus : " : " n'existent plus : ") + someNames(unknown);
                   if (!unreadable.empty())
                       msg += " ; " + plural(unreadable.size(), "valeur illisible", "valeurs illisibles") + " : " + someNames(unreadable);
                   status(msg + ".");
               };
    if (hosts_.askPath) hosts_.askPath(title, question, initial, ui::openFile("For\xC3\xA7" "ages|*.txt", initial, title), std::move(read));
    else hosts_.ask(title, question, initial, std::move(read));
}

// ---------------------------------------------------------- pour les scripts ----
std::string SimulationPane::valueOf(std::string_view path) const {
    const auto want = pathKey(path);
    for (std::size_t i = 0; i < rows_.size() && i < live_.size(); ++i)
        if (!rows_[i].folder && rows_[i].node.real && lower(rows_[i].node.path) == want) return live_[i].text;
    // Pas montree : lue dans la simulation (sans blancs : "Grille[2, 5]" est Grille[2,5]).
    std::string compactPath;
    for (const char c : path)
        if (!std::isspace(uc(c))) compactPath += c;
    return liveText(compactPath);
}

// -------------------------------------------------------- les messages du moteur ----
std::string SimulationPane::frenchPlace(const std::string& section, std::uint32_t line) {
    const std::string at = line > 0 ? ", ligne " + std::to_string(line) : std::string{};
    if (section.empty()) return line > 0 ? "ligne " + std::to_string(line) : std::string{};
    const auto dot = section.find('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < section.size())
        return "DFB " + section.substr(0, dot) + kDot + "section " + section.substr(dot + 1) + at;
    return section + at;
}

namespace {

// Le nom entre apostrophes en tete : "'X' is ..." -> X (et ce qui suit).
bool quotedHead(std::string_view m, std::string& name, std::string_view& rest) {
    if (m.empty() || m.front() != '\'') return false;
    const auto close = m.find('\'', 1);
    if (close == std::string_view::npos) return false;
    name = std::string(m.substr(1, close - 1));
    rest = m.substr(close + 1);
    return true;
}

// Le nombre en tete de `s` (ses chiffres), et ce qui suit.
bool numberHead(std::string_view s, std::string& digits, std::string_view& rest) {
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(uc(s[i]))) ++i;
    if (i == 0) return false;
    digits = std::string(s.substr(0, i));
    rest = s.substr(i);
    return true;
}

std::string groupedNumber(const std::string& digits) { return grouped(digits); }

// Ce qu'une erreur de lecture du ST attendait : « expected ']' », « expected THEN ».
std::string frenchExpected(std::string_view what) {
    if (what == "expected ':=' or a call") return "\xC2\xAB := \xC2\xBB ou un appel attendu";
    if (what == "expected the loop variable") return "la variable de boucle attendue";
    if (what == "expected ':=' after the loop variable") return "\xC2\xAB := \xC2\xBB attendu apr\xC3\xA8s la variable de boucle";
    if (what == "expected the end of the range") return "la fin de la plage attendue";
    if (what == "expected a value") return "une valeur attendue";
    if (what == "expected a case label") return "un libell\xC3\xA9 de CASE attendu";
    if (what == "expected ':' after the case labels") return "\xC2\xAB : \xC2\xBB attendu apr\xC3\xA8s les libell\xC3\xA9s du CASE";
    if (startsWith(what, "expected '") && endsWith(what, "'")) return "\xC2\xAB " + std::string(what.substr(10, what.size() - 11)) + " \xC2\xBB attendu";
    if (startsWith(what, "expected ")) return std::string(what.substr(9)) + " attendu";
    return {};
}

} // namespace

std::string SimulationPane::frenchMessage(const std::string& message) {
    const std::string m = trim(message);
    if (m.empty()) return m;
    std::string name;
    std::string_view rest;
    std::string digits;
    std::string_view after;

    // SimulationHost : « Scan 12 stopped - ... ».
    if (startsWith(m, "Scan ") && numberHead(std::string_view(m).substr(5), digits, after) && startsWith(after, " stopped")) {
        const auto tail = after.substr(8);
        std::string out = "Cycle " + groupedNumber(digits) + " arr\xC3\xAAt\xC3\xA9";
        if (startsWith(tail, " - ")) out += " : " + frenchMessage(std::string(tail.substr(3)));
        return out;
    }
    // core::Error::message() : « invalid argument: ... [source] » - le code ne dit
    // rien de plus que la suite ; la source est la section, dite a part.
    static const std::pair<const char*, const char*> kCodes[] = {
        {"invalid argument", "argument invalide"},     {"not implemented", "non pris en charge"},
        {"out of range", "hors limites"},             {"project data is partial", "le projet est incomplet"},
        {"file not found", "fichier introuvable"},     {"file could not be read", "fichier illisible"},
        {"cancelled", "annul\xC3\xA9"},
    };
    for (const auto& [en, fr] : kCodes) {
        if (m == en) return fr;
        const std::string prefix = std::string(en) + ": ";
        if (!startsWith(m, prefix)) continue;
        std::string inner = m.substr(prefix.size());
        if (!inner.empty() && inner.back() == ']')
            if (const auto open = inner.rfind(" ["); open != std::string::npos) inner = inner.substr(0, open);
        const auto translated = frenchMessage(inner);
        return translated == trim(inner) && std::string(en) != "invalid argument" ? std::string(fr) + " : " + translated : translated;
    }
    // Une erreur de lecture : « line 12: expected ']' (found 'x') ».
    if (startsWith(m, "line ") && numberHead(std::string_view(m).substr(5), digits, after) && startsWith(after, ": ")) {
        std::string_view what = after.substr(2);
        std::string found;
        if (const auto f = what.rfind(" (found '"); f != std::string_view::npos && endsWith(what, "')")) {
            found = std::string(what.substr(f + 9, what.size() - f - 11));
            what = what.substr(0, f);
        }
        std::string fr;
        if (startsWith(what, "expected")) fr = frenchExpected(what);
        else if (startsWith(what, "bad time literal '") && endsWith(what, "'"))
            fr = "dur\xC3\xA9" "e illisible \xC2\xAB " + std::string(what.substr(18, what.size() - 19)) + " \xC2\xBB";
        else if (what == "bad based literal") fr = "nombre en base illisible";
        else if (what == "bad literal") fr = "nombre illisible";
        else if (startsWith(what, "unexpected character '") && endsWith(what, "'"))
            fr = "caract\xC3\xA8re inattendu \xC2\xAB " + std::string(what.substr(22, what.size() - 23)) + " \xC2\xBB";
        else if (quotedHead(what, name, rest) && rest == " is not supported by the simulator here")
            fr = name + " n'est pas pris en charge ici par le simulateur";
        if (fr.empty()) fr = frenchMessage(std::string(what));
        return "ligne " + digits + " : " + fr + (found.empty() ? std::string{} : " (trouv\xC3\xA9 \xC2\xAB " + found + " \xC2\xBB)");
    }
    // Les messages qui commencent par un nom entre apostrophes.
    if (quotedHead(m, name, rest)) {
        if (rest == " is not declared") return name + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e";
        if (rest == " is not a function or a block this simulator knows")
            return name + " n'est ni une fonction ni un bloc que le simulateur conna\xC3\xAEt";
        if (rest == " cannot be written")
            return name.find('[') != std::string::npos ? name + " ne peut pas \xC3\xAAtre \xC3\xA9" "crite : l'indice est sans doute hors des bornes du tableau"
                                                       : name + " ne peut pas \xC3\xAAtre \xC3\xA9" "crite";
        if (rest == " on a string") return "\xC2\xAB " + name + " \xC2\xBB sur une cha\xC3\xAEne";
        std::string pin;
        std::string_view tail;
        if (startsWith(rest, " has no output called ") && quotedHead(rest.substr(22), pin, tail) && tail.empty())
            return name + " n'a pas de sortie " + pin;
        if (startsWith(rest, " declares more than ") && numberHead(rest.substr(20), digits, after) && after == " elements; it is not simulated")
            return name + " d\xC3\xA9" "clare plus de " + groupedNumber(digits) + " \xC3\xA9l\xC3\xA9ments : il n'est pas simul\xC3\xA9";
        if (startsWith(rest, " nests function-block calls more than ") && numberHead(rest.substr(38), digits, after)
            && after == " deep; a block probably instantiates itself")
            return name + " imbrique des appels de blocs sur plus de " + digits + " niveaux : un bloc s'appelle sans doute lui-m\xC3\xAAme";
        std::string type;
        if (startsWith(rest, " is an instance of ") && quotedHead(rest.substr(19), type, tail)
            && startsWith(tail, ", which has no Structured Text body to execute"))
            return name + " est une instance de " + type + ", qui n'a pas de corps en ST : ses entr\xC3\xA9" "es sont gard\xC3\xA9" "es, "
                   "ses sorties gardent leur derni\xC3\xA8re valeur";
    }
    // Les boucles et le budget du cycle.
    static const char* const kLoopTail = " statements and was stopped; a loop is probably not terminating";
    if (startsWith(m, "the scan exceeded ") && numberHead(std::string_view(m).substr(18), digits, after) && after == kLoopTail)
        return "le cycle a d\xC3\xA9pass\xC3\xA9 " + groupedNumber(digits) + " instructions et s'est arr\xC3\xAAt\xC3\xA9 : une boucle ne se termine sans doute pas";
    if (m == "the scan ran for more than its time limit and was stopped; a loop is probably not terminating")
        return "le cycle a dur\xC3\xA9 plus que sa limite et s'est arr\xC3\xAAt\xC3\xA9 : une boucle ne se termine sans doute pas";
    for (const char* loop : {"FOR", "WHILE", "REPEAT"}) {
        const std::string head = std::string(loop) + " loop ran more than ";
        if (!startsWith(m, head) || !numberHead(std::string_view(m).substr(head.size()), digits, after)) continue;
        if (after == " times") return std::string("la boucle ") + loop + " a tourn\xC3\xA9 plus de " + groupedNumber(digits) + " fois";
        if (after == " times without its condition becoming false")
            return std::string("la boucle ") + loop + " a tourn\xC3\xA9 plus de " + groupedNumber(digits) + " fois sans que sa condition devienne fausse";
    }
    if (m == "FOR loop with a step of zero would never end") return "boucle FOR au pas nul : elle ne finirait jamais";
    if (m == "bad loop bounds") return "bornes de boucle illisibles";
    if (m == "division by zero") return "division par z\xC3\xA9ro";
    if (m == "modulo by zero") return "modulo par z\xC3\xA9ro";
    if (m == "unsupported expression") return "expression non prise en charge";
    if (m == "not something that can be assigned") return "ce n'est pas une variable : on ne peut pas y \xC3\xA9" "crire";
    if (m == "the expression produced nothing") return "l'expression n'a rien donn\xC3\xA9";
    if (startsWith(m, "operator '") && endsWith(m, "'")) return "op\xC3\xA9rateur \xC2\xAB " + m.substr(10, m.size() - 11) + " \xC2\xBB inconnu";
    // La preparation : un langage que le simulateur ne fait pas tourner.
    if (startsWith(m, "section '")) {
        std::string section;
        std::string_view tail;
        if (quotedHead(std::string_view(m).substr(8), section, tail) && startsWith(tail, " is ")) {
            const auto semi = tail.find(';');
            if (semi != std::string_view::npos && tail.substr(semi) == "; this simulator runs Structured Text only, so it will not be executed")
                return "la section " + section + " est en " + std::string(tail.substr(4, semi - 4))
                       + " : le simulateur ne fait tourner que le ST, elle n'est pas ex\xC3\xA9" "cut\xC3\xA9" "e";
        }
    }
    if (startsWith(m, "the body of DFB '")) {
        std::string block;
        std::string_view tail;
        if (quotedHead(std::string_view(m).substr(16), block, tail) && startsWith(tail, " is ")) {
            const auto semi = tail.find(';');
            if (semi != std::string_view::npos && tail.substr(semi) == "; instances of it will store their inputs but do nothing")
                return "le corps du DFB " + block + " est en " + std::string(tail.substr(4, semi - 4))
                       + " : ses instances gardent leurs entr\xC3\xA9" "es mais ne calculent rien";
        }
    }
    if (const auto at = m.find(" is outside the configured data memory ("); at != std::string::npos && at > 0) {
        const auto open = at + 40;
        if (numberHead(std::string_view(m).substr(open), digits, after)) {
            const bool words = startsWith(after, " words)");
            const bool bits = startsWith(after, " bits)");
            if ((words || bits) && endsWith(m, "; the PLC would reject this"))
                return m.substr(0, at) + " est hors de la m\xC3\xA9moire configur\xC3\xA9" "e (" + groupedNumber(digits) + (words ? " mots" : " bits")
                       + ") : l'automate la refuserait";
        }
    }
    if (startsWith(m, "no Structured Text section of task '") && endsWith(m, "' could be prepared"))
        return "aucune section en ST de la t\xC3\xA2" "che " + m.substr(36, m.size() - 36 - 19) + " n'a pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e";
    if (m == "no project to simulate") return "aucun projet \xC3\xA0 simuler";
    if (m == "no project") return "aucun projet";
    return m;
}

// ------------------------------------------------------------- la mise en page ----
void SimulationPane::onLayout() {
    const auto b = bounds();
    const float bandH = 48.f;
    band_->setBounds({b.x, b.y, b.w, bandH});
    // Des bornes reelles pour le menu du clic droit : un widget vide n'est pas
    // peint, donc pas pousse dans la passe du dessus (ouvert, il ne se voyait pas).
    if (menu_) menu_->setBounds(b);
    const float top = b.y + bandH;
    const float h = std::max(0.f, b.bottom() - top);
    // Lot 7 : ETROIT (un groupe d'onglets, une tuile de la mosaique, une fenetre
    // detachee), les forcages, la courbe et le simulateur passent SOUS la table,
    // cote a cote : a droite, ils lui prendraient la moitie de la largeur.
    stacked_ = b.w < 980.f && h >= 300.f;
    if (stacked_) {
        const float bottomH = std::clamp(h * 0.4f, 160.f, 300.f);
        side_ = {b.x, b.bottom() - bottomH, b.w, bottomH};
        const gfx::Rect upper{b.x, top, b.w, std::max(0.f, h - bottomH - 1.f)};
        layoutMain(upper);
        layoutStacked();
        return;
    }
    // La colonne de droite : 420 px environ (la maquette), moins dans une fenetre
    // etroite (un onglet detache) ; la table garde toujours le plus de place.
    const float sideW = b.w >= 1100.f ? std::clamp(b.w * 0.32f, 380.f, 440.f)
                      : b.w >= 760.f  ? std::clamp(b.w * 0.36f, 300.f, 380.f)
                                      : std::clamp(b.w * 0.42f, 200.f, 300.f);
    side_ = {b.right() - sideW, top, sideW, h};
    const gfx::Rect left{b.x, top, std::max(0.f, b.w - sideW - 1.f), h};
    layoutMain(left);
    layoutSide(sideW, top);
}

// La table et sa barre de filtres, dans `left`.
void SimulationPane::layoutMain(const gfx::Rect& left) {
    filter_->setBounds({left.x, left.y, left.w, 44.f});
    const gfx::Rect tableArea{left.x, left.y + 44.f, left.w, std::max(0.f, left.h - 44.f)};
    table_->setBounds(tableArea);
    catcher_->setBounds(tableArea);
    if (std::fabs(left.w - mainWidth_) > 0.5f) {
        mainWidth_ = left.w;
        // Etroite, la table garde la variable et sa valeur : « Ecrite par », puis
        // le type, se cachent (l'infobulle de la ligne les dit encore).
        const bool showWriter = left.w >= 640.f, showType = left.w >= 470.f;
        const float value = 112.f, type = showType ? 170.f : 0.f, force = 104.f;
        const float writer = showWriter ? std::clamp(left.w * 0.2f, 140.f, 240.f) : 0.f;
        const float var = std::max(160.f, left.w - value - type - force - writer - 4.f);
        std::vector<ui::TableView::Column> cols(CCount);
        const char* titles[] = {"Variable", "Valeur", "Type", "For\xC3\xA7" "age", "\xC3\x89" "crite par"};
        const float widths[] = {var, value, showType ? type : 170.f, force, showWriter ? writer : 160.f};
        for (std::size_t i = 0; i < CCount; ++i) {
            cols[i].title = titles[i];
            cols[i].width = widths[i];
            cols[i].sortable = false;
        }
        cols[CValue].align = ui::Align::End;
        cols[CType].visible = showType;
        cols[CWriter].visible = showWriter;
        // Lot recherche : la valeur et le forcage changent a chaque cycle - pas de filtre.
        cols[CValue].filterable = false;
        cols[CForce].filterable = false;
        table_->setColumns(std::move(cols));
    }
}

// La colonne de droite : les forcages, la courbe, le simulateur.
void SimulationPane::layoutSide(float sideW, float top) {
    const float inner = std::max(0.f, sideW - 12.f);
    float y = top;
    forcedTitle_ = {side_.x, y, sideW, 32.f};
    y += 32.f;
    const std::size_t shownRows = std::clamp<std::size_t>(forcedNames_.size(), 1, 6);
    const float forcedH = static_cast<float>(shownRows) * rowH_ + 2.f;
    forcedBox_->setBounds({side_.x + 6.f, y, inner, forcedH});
    // Les titres de la table sont au-dessus du cadre, donc caches : le titre est peint.
    forced_->setBounds({side_.x + 6.f, y - headerH_, inner, forcedH + headerH_});
    if (auto* catcher = forcedBox_->children().size() > 1 ? forcedBox_->children().back().get() : nullptr)
        catcher->setBounds({side_.x + 6.f, y, inner, forcedH});
    y += forcedH + 6.f;
    trendTitle_ = {side_.x, y, sideW, 32.f};
    y += 32.f;
    const float trendH = std::clamp((side_.bottom() - y) * 0.5f, 120.f, 230.f);
    trend_->setBounds({side_.x + 6.f, y, inner, trendH});
    y += trendH + 6.f;
    diagTitle_ = {side_.x, y, sideW, 36.f};
    const float pw = std::min(policy_->preferredWidth(), std::max(120.f, sideW * 0.5f));
    policy_->setBounds({side_.right() - 8.f - pw, y + 5.f, pw, 26.f});
    y += 36.f;
    const float diagH = std::max(0.f, side_.bottom() - y - 4.f);
    diagBox_->setBounds({side_.x + 6.f, y, inner, diagH});
    diag_->setBounds({side_.x + 6.f, y - headerH_, inner, diagH + headerH_});
    if (auto* catcher = diagBox_->children().size() > 1 ? diagBox_->children().back().get() : nullptr)
        catcher->setBounds({side_.x + 6.f, y, inner, diagH});
    if (std::fabs(inner - sideWidth_) > 0.5f) {
        sideWidth_ = inner;
        std::vector<ui::TableView::Column> f(3);
        f[0].title = "Variable forc\xC3\xA9" "e";
        f[0].width = std::max(80.f, inner - 96.f - 30.f - 4.f);
        f[1].title = "Valeur";
        f[1].width = 96.f;
        f[1].align = ui::Align::End;
        f[2].title = "";
        f[2].width = 30.f;
        f[2].align = ui::Align::End;
        for (auto& col : f) col.sortable = false;
        forced_->setColumns(std::move(f));
        std::vector<ui::TableView::Column> d(2);
        d[0].title = "Ce que dit le simulateur";
        d[0].width = std::max(80.f, inner - 56.f - 4.f);
        d[1].title = "";
        d[1].width = 56.f;
        d[1].align = ui::Align::End;
        for (auto& col : d) col.sortable = false;
        diag_->setColumns(std::move(d));
    }
}

// Etroit : sous la table, trois colonnes - les forcages, la courbe, le simulateur.
void SimulationPane::layoutStacked() {
    const float gap = 1.f;
    const float w3 = std::max(0.f, (side_.w - 2.f * gap) / 3.f);
    const float x0 = side_.x, x1 = side_.x + w3 + gap, x2 = side_.x + 2.f * (w3 + gap);
    const float inner = std::max(0.f, w3 - 12.f);
    const float y = side_.y;
    const float body = std::max(0.f, side_.h - 36.f - 4.f);
    forcedTitle_ = {x0, y, w3, 32.f};
    forcedBox_->setBounds({x0 + 6.f, y + 32.f, inner, body});
    forced_->setBounds({x0 + 6.f, y + 32.f - headerH_, inner, body + headerH_});
    if (auto* catcher = forcedBox_->children().size() > 1 ? forcedBox_->children().back().get() : nullptr)
        catcher->setBounds({x0 + 6.f, y + 32.f, inner, body});
    trendTitle_ = {x1, y, w3, 32.f};
    trend_->setBounds({x1 + 6.f, y + 32.f, inner, std::max(0.f, side_.h - 32.f - 6.f)});
    diagTitle_ = {x2, y, w3, 36.f};
    const float pw = std::min(policy_->preferredWidth(), std::max(110.f, w3 * 0.55f));
    policy_->setBounds({x2 + w3 - 8.f - pw, y + 5.f, pw, 26.f});
    diagBox_->setBounds({x2 + 6.f, y + 36.f, inner, body});
    diag_->setBounds({x2 + 6.f, y + 36.f - headerH_, inner, body + headerH_});
    if (auto* catcher = diagBox_->children().size() > 1 ? diagBox_->children().back().get() : nullptr)
        catcher->setBounds({x2 + 6.f, y + 36.f, inner, body});
    if (std::fabs(inner - sideWidth_) > 0.5f) {
        sideWidth_ = inner;
        std::vector<ui::TableView::Column> f(3);
        f[0].title = "Variable forc\xC3\xA9" "e";
        f[0].width = std::max(80.f, inner - 80.f - 30.f - 4.f);
        f[1].title = "Valeur";
        f[1].width = 80.f;
        f[1].align = ui::Align::End;
        f[2].title = "";
        f[2].width = 30.f;
        f[2].align = ui::Align::End;
        for (auto& col : f) col.sortable = false;
        forced_->setColumns(std::move(f));
        std::vector<ui::TableView::Column> d(2);
        d[0].title = "Ce que dit le simulateur";
        d[0].width = std::max(80.f, inner - 48.f - 4.f);
        d[1].title = "";
        d[1].width = 48.f;
        d[1].align = ui::Align::End;
        for (auto& col : d) col.sortable = false;
        diag_->setColumns(std::move(d));
    }
}

void SimulationPane::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& th = ctx.theme;
    const auto& c = th.color;
    // Les mesures du theme (le fort contraste a des titres de 40 px) : les cadres
    // des listes doivent cacher exactement les titres.
    if (th.metric.headerHeight != headerH_ || th.metric.rowHeight != rowH_) {
        headerH_ = th.metric.headerHeight;
        rowH_ = th.metric.rowHeight;
        invalidateLayout();
    }
    // La fenetre qui nous peint (la principale, ou une fenetre detachee) : le
    // menu du clic droit y reste.
    surface_ = r.surfaceSize();
    r.fillRect(bounds(), c.windowBg);
    releaseAllLink_ = freezeLink_ = gfx::Rect{};
    if (side_.w <= 0.f) return;
    r.fillRect(side_, c.panelBg);
    if (stacked_) {
        // Etroit : un trait au-dessus, et entre les trois colonnes.
        r.fillRect({side_.x, side_.y - 1.f, side_.w, 1.f}, c.border);
        r.fillRect({trendTitle_.x - 1.f, side_.y, 1.f, side_.h}, c.border);
        r.fillRect({diagTitle_.x - 1.f, side_.y, 1.f, side_.h}, c.border);
    } else {
        r.fillRect({side_.x - 1.f, side_.y, 1.f, side_.h}, c.border);
    }

    const auto accent = th.onSurface(c.accent);
    // Un titre : son icone, son nom, son nombre ; rend la place a droite.
    const auto title = [&](const gfx::Rect& box, ui::Icon icon, ui::Tone tone, const std::string& name, const std::string& detail) {
        r.fillRect({box.x, box.y, box.w, 1.f}, c.border);
        const float ty = box.y + (box.h - r.lineHeight(kBody)) * 0.5f;
        ui::drawIcon(r, icon, {box.x + 12.f, box.y + (box.h - 15.f) * 0.5f, 15.f, 15.f}, th.onSurface(th.tone(tone, c.textMuted)));
        float x = box.x + 34.f;
        drawBold(r, {x, ty}, name, kBody, c.text);
        x += r.measure(name, kBody).width + 10.f;
        if (!detail.empty()) r.drawText({x, ty + 1.f}, detail, kSmall, c.textMuted);
    };
    const auto rightLink = [&](const gfx::Rect& box, const std::string& label) {
        const float w = r.measure(label, kSmall).width;
        const gfx::Rect hit{box.right() - 12.f - w - 4.f, box.y + 4.f, w + 8.f, box.h - 8.f};
        r.drawText({box.right() - 12.f - w, box.y + (box.h - r.lineHeight(kSmall)) * 0.5f}, label, kSmall, accent);
        return hit;
    };

    title(forcedTitle_, ui::Icon::Force, ui::Tone::Warning, "For\xC3\xA7" "ages", count(forcedNames_.size()));
    if (!forcedNames_.empty()) releaseAllLink_ = rightLink(forcedTitle_, "Rel\xC3\xA2" "cher tout");

    const bool frozen = trend_ && trend_->frozen();
    title(trendTitle_, ui::Icon::Chart, ui::Tone::Info, "La courbe",
          plural(watched_.size(), "suivie", "suivies") + kDot + "30 s" + (frozen ? std::string(kDot) + "fig\xC3\xA9" "e" : std::string{}));
    freezeLink_ = rightLink(trendTitle_, frozen ? "Reprendre" : "Figer");

    // Le titre compte ce que la liste dit (la maquette : « Le simulateur 3 ») ;
    // son icone dit s'il y a quelque chose a regarder.
    bool problems = false, halted = false;
    for (const auto& it : diagItems_) {
        if (it.tone == ui::Tone::Warning || it.tone == ui::Tone::Error) problems = true;
        if (it.tone == ui::Tone::Error) halted = true;
    }
    title(diagTitle_, problems ? ui::Icon::Warning : ui::Icon::Ok, halted ? ui::Tone::Error : problems ? ui::Tone::Warning : ui::Tone::Ok,
          "Le simulateur", count(diagItems_.size()));
    const std::string policyLabel = "fonction inconnue :";
    const float pw = r.measure(policyLabel, kSmall).width;
    const float px = policy_->bounds().x - 8.f - pw;
    if (px > diagTitle_.x + 150.f)
        r.drawText({px, diagTitle_.y + (diagTitle_.h - r.lineHeight(kSmall)) * 0.5f}, policyLabel, kSmall, c.textMuted);
}

ui::EventResult SimulationPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        if (releaseAllLink_.contains(d->pos) && !forcedNames_.empty()) {
            runAction(AReleaseAll);
            return ui::EventResult::Consumed;
        }
        if (freezeLink_.contains(d->pos) && trend_) {
            trend_->setFrozen(!trend_->frozen());
            status(trend_->frozen() ? "La courbe est fig\xC3\xA9" "e : la simulation continue, l'image reste. Reprendre la relance."
                                    : "La courbe suit de nouveau la simulation.");
            invalidate();
            return ui::EventResult::Consumed;
        }
    }
    // Au clavier, dans la table : Entree force (ou deplie), Suppr relache.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !k->repeat && k->mods.none() && table_ && table_->focused()) {
        if (k->key == ui::Key::Delete) {
            runAction(ARelease);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            const auto rows = selectedRows();
            if (rows.size() == 1 && (rows_[rows.front()].folder || rows_[rows.front()].agg)) {
                toggleRow(rows.front());
                return ui::EventResult::Consumed;
            }
            if (!rows.empty()) {
                runAction(AForce);          // les variables a valeur choisies, une valeur pour toutes
                return ui::EventResult::Consumed;
            }
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
