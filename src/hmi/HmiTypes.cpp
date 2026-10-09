// hmi/HmiTypes.cpp - les types IHM, les tableaux, leur depliage en cases et leur
// place Modbus (lot 16).
#include "HmiTypes.hpp"
#include "HmiEnums.hpp"     // 1.10 (decision 15) : une enumeration est une case DINT

#include "HmiEquipment.hpp"
#include "HmiMarkers.hpp"   // 1.11.1 (REP) : pathProblems lit un script sans les $ de ses reperes
#include "HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types elementaires d'une variable

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace hmi::types {

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

bool identifier(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// Un entier signe : "0", "-5", "12".
bool integerOf(std::string_view s, long long& out) {
    const std::string t = trimmed(s);
    if (t.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (t[0] == '-' || t[0] == '+') {
        neg = t[0] == '-';
        i = 1;
    }
    if (i >= t.size() || t.size() - i > 9) return false;
    long long v = 0;
    for (; i < t.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        v = v * 10 + (t[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

// "0..9" -> 0, 9.
bool rangeOf(std::string_view s, long long& lo, long long& hi, std::string* why) {
    const auto dots = s.find("..");
    if (dots == std::string_view::npos) return fail(why, "bornes \xC2\xAB " + trimmed(s) + " \xC2\xBB : \xC3\xA9" "crire bas..haut (0..9)");
    if (!integerOf(s.substr(0, dots), lo) || !integerOf(s.substr(dots + 2), hi))
        return fail(why, "bornes \xC2\xAB " + trimmed(s) + " \xC2\xBB : deux entiers (0..9, 1..4, -5..5)");
    if (hi < lo) return fail(why, "bornes " + std::to_string(lo) + ".." + std::to_string(hi) + " : la haute avant la basse");
    return true;
}

// Le chemin d'une case : "Consignes[3]", "Matrice[2,7]".
std::string indexText(long long i) { return "[" + std::to_string(i) + "]"; }
std::string indexText(long long i, long long j) { return "[" + std::to_string(i) + "," + std::to_string(j) + "]"; }

// Une liste "1.5, 2, 'a,b', 3" -> ses valeurs (les virgules entre guillemets ou
// entre parentheses ne coupent pas). Une seule valeur : une liste d'un.
std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    for (char c : s) {
        if (c == '\'' ) quoted = !quoted;
        if (!quoted && (c == '(' || c == '[')) ++depth;
        if (!quoted && (c == ')' || c == ']')) --depth;
        if (c == ',' && !quoted && depth == 0) {
            out.push_back(trimmed(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(trimmed(cur));
    return out;
}

// La tete d'ecriture de la place Modbus.
struct Cursor {
    long long word{0};             // le prochain mot libre
    long long packWord{-1};        // le mot ou se rangent les BOOL en cours (-1 : aucun)
    int       packBit{0};
    void close() noexcept { packWord = -1; }
};

struct Walker {
    const Project& p;
    bool           pack;
    Flat&          out;
    std::vector<std::string> stack;   // les types IHM en cours (majuscules) : un cycle
    Cursor         cur;

    bool full() const noexcept { return static_cast<long long>(out.leaves.size()) > kMaxElements; }

    void leaf(const std::string& path, const std::string& rel, const std::string& type, const std::string& initial,
              const std::string& description) {
        Leaf l;
        l.path = path;
        l.rel = rel;
        l.type = type;
        l.initial = initial;
        l.description = description;
        if (type == "BOOL" && pack) {
            if (cur.packWord < 0 || cur.packBit >= 16) {
                cur.packWord = cur.word++;
                cur.packBit = 0;
                l.words = 1;
            }
            l.word = cur.packWord;
            l.bit = cur.packBit++;
        } else {
            cur.close();
            l.word = cur.word;
            l.words = wordsOf(type);
            cur.word += l.words;
        }
        out.leaves.push_back(std::move(l));
    }

    void walk(const std::string& path, const std::string& rel, std::string_view type, std::string_view initial,
              const std::string& description) {
        if (!out.error.empty()) return;
        Spec s;
        std::string why;
        if (!parseSpec(type, s, &why)) {
            out.error = path + " : " + why;
            return;
        }
        if (s.array()) {
            if (s.length() > kMaxElements || static_cast<long long>(out.leaves.size()) + s.length() > kMaxElements) {
                out.error = path + " : plus de " + std::to_string(kMaxElements) + " cases";
                return;
            }
            cur.close();
            const long long start = cur.word;
            Aggregate a;
            a.path = path;
            a.array = true;
            a.spec = s;
            a.typeName = specText(s);
            // Les valeurs initiales : une pour toutes, ou une liste (tableau de cases simples ;
            // 1.10 : une enumeration est une case simple, un DINT).
            const HmiType* elementType = isElementary(s.element) ? nullptr : p.hmiTypeByName(s.element);
            const bool enumElement = elementType && elementType->kind == HmiTypeKind::Enumeration;
            const bool simple = isElementary(s.element) || enumElement;
            const auto list = simple ? splitList(initial) : std::vector<std::string>{};
            const bool perElement = list.size() > 1;
            long long k = 0;
            const auto elementInit = [&]() -> std::string {
                if (!simple) return {};
                if (!perElement) return trimmed(initial);
                return k < static_cast<long long>(list.size()) ? list[static_cast<std::size_t>(k)] : std::string{};
            };
            if (s.dims == 1) {
                for (long long i = s.low[0]; i <= s.high[0] && out.error.empty(); ++i, ++k)
                    walk(path + indexText(i), rel + indexText(i), s.element, elementInit(), description);
            } else {
                for (long long i = s.low[0]; i <= s.high[0] && out.error.empty(); ++i)
                    for (long long j = s.low[1]; j <= s.high[1] && out.error.empty(); ++j, ++k)
                        walk(path + indexText(i, j), rel + indexText(i, j), s.element, elementInit(), description);
            }
            cur.close();
            a.weight.words = cur.word - start;
            a.weight.bytes = a.weight.words * 2;
            if (simple) a.elementBytes = s.element == "BOOL" ? 1 : wordsOf(enumElement ? std::string_view("DINT") : std::string_view(s.element)) * 2;
            else a.elementBytes = s.length() > 0 ? a.weight.bytes / s.length() : 0;
            out.aggregates.push_back(std::move(a));
            return;
        }
        if (isElementary(s.element)) {
            leaf(path, rel, s.element, std::string(trimmed(initial)), description);
            return;
        }
        const HmiType* ty = p.hmiTypeByName(s.element);
        if (!ty) {
            out.error = path + " : type inconnu \xC2\xAB " + s.element + " \xC2\xBB";
            return;
        }
        // 1.10 (decision 15, S1) : une enumeration est une case DINT (le nombre de sa valeur) ;
        // sa valeur initiale s'ecrit Auto, T_MODE#Auto, le texte ou le nombre (vide : la premiere).
        if (ty->kind == HmiTypeKind::Enumeration) {
            std::int64_t n = ty->values.empty() ? 0 : ty->values.front().value;
            if (!trimmed(initial).empty() && !enumNumberOf(*ty, trimmed(initial), n)) {
                out.error = path + " : valeur initiale \xC2\xAB " + trimmed(initial) + " \xC2\xBB : pas une valeur de " + ty->name;
                return;
            }
            leaf(path, rel, "DINT", std::to_string(n), description);
            return;
        }
        const std::string key = upperOf(ty->name);
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
            out.error = path + " : le type " + ty->name + " se contient lui-m\xC3\xAAme";
            return;
        }
        stack.push_back(key);
        cur.close();
        const long long start = cur.word;
        Aggregate a;
        a.path = path;
        a.spec = s;
        a.typeName = ty->name;
        for (const auto& m : ty->members) {
            if (!out.error.empty()) break;
            walk(path + "." + m.name, rel.empty() ? m.name : rel + "." + m.name, m.type, m.initial,
                 m.description.empty() ? description : m.description);
        }
        cur.close();
        a.weight.words = cur.word - start;
        a.weight.bytes = a.weight.words * 2;
        out.aggregates.push_back(std::move(a));
        stack.pop_back();
    }
};

// Le depart d'une variable liee : sa zone, son premier mot (ou bit), sa facon de s'ecrire.
enum class Style { Schneider, Zero, X, Modicon };
enum class Zone { Holding, Input, Coil, Discrete };
struct Start {
    Zone      zone{Zone::Holding};
    long long offset{0};
    Style     style{Style::Schneider};
    int       digits{5};           // Modicon : 5 (40001) ou 6 (400001)
};

bool startOf(std::string_view start, Start& out, std::string* why) {
    const std::string raw = upperOf(trimmed(start));
    const std::string canon = equip::canonicalAddress(raw, why);
    if (canon.empty()) return false;
    // "%MW3000", "%M5", "%IW4", "%I7", "%MF20", "%MD20" ; un bit de mot : refuse.
    std::string letters;
    std::size_t i = 1;
    while (i < canon.size() && std::isalpha(static_cast<unsigned char>(canon[i]))) letters += canon[i++];
    const std::string number = canon.substr(i);
    if (number.find('.') != std::string::npos)
        return fail(why, "une structure ou un tableau part d'un mot, pas d'un bit (" + std::string(trimmed(start)) + ")");
    long long n = 0;
    if (!integerOf(number, n) || n < 0) return fail(why, "adresse illisible \xC2\xAB " + std::string(trimmed(start)) + " \xC2\xBB");
    out.offset = n;
    if (letters == "MW" || letters == "MD" || letters == "MF") out.zone = Zone::Holding;
    else if (letters == "IW" || letters == "ID" || letters == "IF") out.zone = Zone::Input;
    else if (letters == "M") out.zone = Zone::Coil;
    else if (letters == "I") out.zone = Zone::Discrete;
    else return fail(why, "adresse " + std::string(trimmed(start)) + " : pas lisible par Modbus");
    if (raw.front() == '%') out.style = Style::Schneider;
    else if (raw.rfind("HR", 0) == 0 || raw.rfind("IR", 0) == 0 || raw.rfind("CO", 0) == 0 || raw.rfind("DI", 0) == 0) out.style = Style::Zero;
    else if (raw.size() > 2 && raw[1] == 'X') out.style = Style::X;
    else {
        out.style = Style::Modicon;
        const auto dot = raw.find('.');
        out.digits = static_cast<int>(raw.substr(0, dot).size()) == 6 ? 6 : 5;
    }
    return true;
}

std::string modicon(char lead, long long n, int digits) {
    char b[24];
    if (digits == 6 || n > 9999) std::snprintf(b, sizeof b, "%c%05lld", lead, n);
    else std::snprintf(b, sizeof b, "%c%04lld", lead, n);
    return b;
}

std::string pathKey(std::string_view s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

// ------------------------------------------------------------------ types ---
// 1.11.19 (refonte, lot 6) : les types elementaires d'une variable IHM viennent du registre des
// types (typereg::UseVariable : ceux qui ont une place Modbus), sous leur nom exact.
bool isElementary(std::string_view name) noexcept {
    const auto* e = typereg::baseRegistry().byName(name);
    return e && e->usable(typereg::UseVariable) && e->category != typereg::Category::Generic && sameText(e->name, name);
}

bool isComposite(std::string_view type) noexcept {
    const std::string t = trimmed(type);
    return !t.empty() && !isElementary(t);
}

bool parseSpec(std::string_view text, Spec& out, std::string* why) {
    out = Spec{};
    const std::string t = trimmed(text);
    if (t.empty()) return fail(why, "type vide");
    const std::string up = upperOf(t);
    if (up.rfind("ARRAY", 0) != 0 || (up.size() > 5 && (std::isalnum(static_cast<unsigned char>(up[5])) || up[5] == '_'))) {
        if (!identifier(t)) return fail(why, "type illisible \xC2\xAB " + t + " \xC2\xBB");
        out.element = isElementary(t) ? up : t;
        return true;
    }
    const auto open = t.find('[');
    const auto close = t.find(']');
    if (open == std::string::npos || close == std::string::npos || close < open)
        return fail(why, "tableau illisible \xC2\xAB " + t + " \xC2\xBB : ARRAY[0..9] OF REAL");
    const std::string inside = t.substr(open + 1, close - open - 1);
    const std::string rest = trimmed(std::string_view(t).substr(close + 1));
    if (upperOf(rest).rfind("OF", 0) != 0 || rest.size() < 3 || !std::isspace(static_cast<unsigned char>(rest[2])))
        return fail(why, "tableau \xC2\xAB " + t + " \xC2\xBB : il manque OF (ARRAY[0..9] OF REAL)");
    const std::string element = trimmed(std::string_view(rest).substr(2));
    if (upperOf(element).rfind("ARRAY", 0) == 0)
        return fail(why, "un tableau de tableaux : \xC3\xA9" "crire ARRAY[0..3, 0..9] OF ... (deux dimensions)");
    if (!identifier(element)) return fail(why, "tableau \xC2\xAB " + t + " \xC2\xBB : type des cases illisible");
    const auto parts = splitList(inside);
    if (parts.size() > 2) return fail(why, "un tableau a une ou deux dimensions (ARRAY[0..3, 0..9] OF INT)");
    for (std::size_t d = 0; d < parts.size(); ++d)
        if (!rangeOf(parts[d], out.low[d], out.high[d], why)) return false;
    out.dims = static_cast<int>(parts.size());
    out.element = isElementary(element) ? upperOf(element) : element;
    if (out.length() > kMaxElements)
        return fail(why, "tableau de " + std::to_string(out.length()) + " cases : " + std::to_string(kMaxElements) + " au plus");
    return true;
}

std::string specText(const Spec& s) {
    if (!s.array()) return s.element;
    std::string out = "ARRAY[" + std::to_string(s.low[0]) + ".." + std::to_string(s.high[0]);
    if (s.dims == 2) out += ", " + std::to_string(s.low[1]) + ".." + std::to_string(s.high[1]);
    return out + "] OF " + s.element;
}

std::string normalized(std::string_view type) {
    Spec s;
    if (!parseSpec(type, s)) return trimmed(type);
    return specText(s);
}

bool validType(const Project& p, std::string_view type, std::string* why) {
    Spec s;
    if (!parseSpec(type, s, why)) return false;
    if (isElementary(s.element)) return true;
    if (!p.hmiTypeByName(s.element)) return fail(why, "type inconnu \xC2\xAB " + s.element + " \xC2\xBB");
    return true;
}

std::string cycleOf(const Project& p, std::string_view typeName) {
    std::vector<std::string> path;
    std::string found;
    std::function<void(const HmiType&)> visit = [&](const HmiType& ty) {
        if (!found.empty()) return;
        const std::string key = upperOf(ty.name);
        for (std::size_t i = 0; i < path.size(); ++i)
            if (upperOf(path[i]) == key) {
                for (std::size_t k = i; k < path.size(); ++k) found += path[k] + " -> ";
                found += ty.name;
                return;
            }
        if (path.size() > 64) return;
        path.push_back(ty.name);
        for (const auto& m : ty.members) {
            Spec s;
            if (!parseSpec(m.type, s) || isElementary(s.element)) continue;
            if (const auto* sub = p.hmiTypeByName(s.element)) visit(*sub);
        }
        path.pop_back();
    };
    if (const auto* ty = p.hmiTypeByName(typeName)) visit(*ty);
    return found;
}

std::vector<std::string> usersOf(const Project& p, std::string_view typeName) {
    std::vector<std::string> out;
    const auto uses = [&](std::string_view type) {
        Spec s;
        return parseSpec(type, s) && sameText(s.element, typeName);
    };
    for (const auto& ty : p.programs.types)
        for (const auto& m : ty.members)
            if (uses(m.type)) {
                out.push_back(ty.name + "." + m.name);
                break;
            }
    for (const auto& v : p.programs.variables)
        if (uses(v.type)) out.push_back(v.name);
    return out;
}

// ------------------------------------------------------------ la place ------
long long wordsOf(std::string_view t) noexcept {
    if (sameText(t, "DINT") || sameText(t, "UDINT") || sameText(t, "DWORD") || sameText(t, "REAL") || sameText(t, "LREAL")
        || sameText(t, "TIME"))
        return 2;
    if (sameText(t, "STRING")) return 16;
    return 1;
}

Weight weightOf(const Project& p, std::string_view type, bool packBools) {
    Spec s;
    if (!parseSpec(type, s)) return {};
    if (!s.array() && isElementary(s.element)) {
        const long long w = wordsOf(s.element);
        return {w, s.element == "BOOL" && packBools ? 1 : w * 2};
    }
    const Flat f = flatten(p, "x", type, {}, packBools);
    if (!f.ok() || f.aggregates.empty()) return {};
    return f.aggregates.back().weight;
}

Flat flatten(const Project& p, std::string_view root, std::string_view type, std::string_view initial, bool packBools) {
    Flat out;
    Walker w{p, packBools, out, {}, {}};
    w.walk(std::string(root), {}, type, initial, {});
    if (!out.ok()) {
        out.leaves.clear();
        out.aggregates.clear();
    }
    return out;
}

bool bitArea(std::string_view start) noexcept {
    Start st;
    return startOf(start, st, nullptr) && (st.zone == Zone::Coil || st.zone == Zone::Discrete);
}

std::string memberAddress(std::string_view start, long long word, int bit, std::string_view type, std::string* why) {
    Start st;
    if (!startOf(start, st, why)) return {};
    const bool isBool = sameText(type, "BOOL");
    char b[40];
    if (st.zone == Zone::Coil || st.zone == Zone::Discrete) {
        if (!isBool) {
            fail(why, "un " + std::string(type) + " ne tient pas dans un bit : le d\xC3\xA9part " + trimmed(start)
                          + " est une bobine (partir d'un registre : 40001, %MW)");
            return {};
        }
        const long long n = st.offset + word;
        if (n > 65535) {
            fail(why, "au-del\xC3\xA0 de l'adresse 65535");
            return {};
        }
        switch (st.style) {
            case Style::Schneider: std::snprintf(b, sizeof b, "%s%lld", st.zone == Zone::Coil ? "%M" : "%I", n); return b;
            case Style::Zero: std::snprintf(b, sizeof b, "%s%lld", st.zone == Zone::Coil ? "CO" : "DI", n); return b;
            case Style::X: std::snprintf(b, sizeof b, "%cx%04lld", st.zone == Zone::Coil ? '0' : '1', n + 1); return b;
            case Style::Modicon: return modicon(st.zone == Zone::Coil ? '0' : '1', n + 1, st.digits);
        }
        return {};
    }
    const long long n = st.offset + word;
    if (n + wordsOf(type) - 1 > 65535) {
        fail(why, "au-del\xC3\xA0 de l'adresse 65535");
        return {};
    }
    const std::string bitText = isBool && bit >= 0 ? "." + std::to_string(bit) : std::string{};
    const bool holding = st.zone == Zone::Holding;
    switch (st.style) {
        case Style::Schneider: {
            const char* letters = holding ? "%MW" : "%IW";
            if (!isBool && wordsOf(type) == 2)
                letters = sameText(type, "REAL") || sameText(type, "LREAL") ? (holding ? "%MF" : "%IF") : (holding ? "%MD" : "%ID");
            std::snprintf(b, sizeof b, "%s%lld", letters, n);
            return std::string(b) + bitText;
        }
        case Style::Zero:
            std::snprintf(b, sizeof b, "%s%lld", holding ? "HR" : "IR", n);
            return std::string(b) + bitText;
        case Style::X:
            std::snprintf(b, sizeof b, "%cx%04lld", holding ? '4' : '3', n + 1);
            return std::string(b) + bitText;
        case Style::Modicon: return modicon(holding ? '4' : '3', n + 1, st.digits) + bitText;
    }
    return {};
}

bool memberCovers(std::string_view pattern, std::string_view rel) {
    const std::string pt = pathKey(pattern), r = pathKey(rel);
    if (pt.empty()) return false;
    std::size_t i = 0, j = 0;
    while (i < pt.size()) {
        if (pt.compare(i, 3, "[*]") == 0) {
            // [*] : un indice quelconque ([3], [1,2]).
            if (j >= r.size() || r[j] != '[') return false;
            const auto close = r.find(']', j);
            if (close == std::string::npos) return false;
            i += 3;
            j = close + 1;
            continue;
        }
        if (j >= r.size() || pt[i] != r[j]) return false;
        ++i;
        ++j;
    }
    return j == r.size() || r[j] == '.' || r[j] == '[';
}

std::string internalEntryOf(const Variable& v, std::string_view rel) {
    if (!v.bound()) return {};
    for (const auto& e : v.internal)
        if (memberCovers(e, rel)) return e;
    return {};
}

bool isInternalMember(const Variable& v, std::string_view rel) { return !internalEntryOf(v, rel).empty(); }

std::vector<Variable> leafVariables(const Project& p, const Variable& v, std::vector<Aggregate>* aggregates,
                                    std::vector<std::string>* why, std::string* error) {
    std::vector<Variable> out;
    if (!isComposite(v.type)) {
        out.push_back(v);
        if (why) why->emplace_back();
        return out;
    }
    const Flat f = flatten(p, v.name, v.type, v.initial, v.packBools);
    if (!f.ok()) {
        if (error) *error = f.error;
        return out;
    }
    if (aggregates) aggregates->insert(aggregates->end(), f.aggregates.begin(), f.aggregates.end());
    const bool bits = v.bound() && !v.address.empty() && bitArea(v.address);
    const std::size_t n = f.leaves.size();
    // 1.11.8 : les membres internes (gardes dans l'IHM).
    std::vector<char> internal(n, 0);
    if (v.bound() && !v.internal.empty())
        for (std::size_t i = 0; i < n; ++i) internal[i] = isInternalMember(v, f.leaves[i].rel) ? 1 : 0;
    // 1.11.8 (« Recalculer la place memoire ») : les mots que n'occupent que des membres
    // internes sont rendus ; chaque case recule d'autant de mots rendus avant elle. Les BOOL
    // d'une zone de bits se comptent par rang : un BOOL interne n'en prend plus.
    std::vector<long long> word(n), rank(n);
    {
        std::vector<long long> freed;
        if (v.compact && v.bound()) {
            std::map<long long, bool> onlyInternal;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& l = f.leaves[i];
                const long long count = std::max<long long>(1, wordsOf(l.type));
                for (long long w = l.word; w < l.word + count; ++w) {
                    const auto [it, inserted] = onlyInternal.emplace(w, internal[i] != 0);
                    if (!inserted) it->second = it->second && internal[i] != 0;
                }
            }
            for (const auto& [w, only] : onlyInternal)
                if (only) freed.push_back(w);
        }
        long long r = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& l = f.leaves[i];
            word[i] = l.word - static_cast<long long>(std::lower_bound(freed.begin(), freed.end(), l.word) - freed.begin());
            rank[i] = r;
            if (l.type == "BOOL" && !(v.compact && internal[i])) ++r;
        }
    }
    // 1.11.8 : le depart d'un membre compose ("Vannes[2]" -> "%MW3050") - ses cases le suivent,
    // a la meme place l'une par rapport a l'autre. Le plus precis gagne (Vannes[2].Sous avant Vannes[2]).
    struct Start {
        std::string pattern, address;
        long long   base{0};
    };
    std::vector<Start> starts;
    if (v.bound())
        for (const auto& pl : v.places) {
            if (trimmed(pl.address).empty()) continue;
            bool leaf = false, any = false;
            long long base = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (pathKey(pl.path) == pathKey(f.leaves[i].rel)) leaf = true;
                if (!memberCovers(pl.path, f.leaves[i].rel)) continue;
                if (v.compact && internal[i]) continue;
                const long long at = bits ? rank[i] : word[i];
                if (!any || at < base) base = at;
                any = true;
            }
            if (!leaf && any) starts.push_back({pl.path, trimmed(pl.address), base});
        }
    std::sort(starts.begin(), starts.end(), [](const Start& a, const Start& b) { return pathKey(a.pattern).size() > pathKey(b.pattern).size(); });
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& l = f.leaves[i];
        Variable x;
        x.id = v.id;
        x.name = l.path;
        x.type = l.type;
        x.initial = l.initial;
        x.description = l.description.empty() ? v.description : l.description;
        x.folder = v.folder;
        x.readOnly = v.readOnly;
        x.packBools = v.packBools;
        std::string reason;
        if (v.bound() && !internal[i]) {
            x.equipment = v.equipment;
            const MemberAddress* fixed = nullptr;
            for (const auto& pl : v.places)
                if (pathKey(pl.path) == pathKey(l.rel)) fixed = &pl;
            const Start* from = nullptr;
            for (const auto& s : starts)
                if (memberCovers(s.pattern, l.rel)) {
                    from = &s;
                    break;
                }
            if (fixed && !trimmed(fixed->address).empty()) {
                x.address = trimmed(fixed->address);
            } else if (from) {
                // (le depart d'un membre est de la meme sorte que celui de la variable : mots, ou bits)
                x.address = bits ? memberAddress(from->address, rank[i] - from->base, -1, l.type, &reason)
                                 : memberAddress(from->address, word[i] - from->base, l.bit, l.type, &reason);
            } else if (v.address.empty()) {
                reason = "sans adresse de d\xC3\xA9part";
            } else if (bits) {
                x.address = l.type == "BOOL" ? memberAddress(v.address, rank[i], -1, l.type, &reason)
                                             : memberAddress(v.address, 0, -1, l.type, &reason);
            } else {
                x.address = memberAddress(v.address, word[i], l.bit, l.type, &reason);
            }
        }
        if (why) why->push_back(reason);
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<Variable> flatVariables(const Project& p) {
    std::vector<Variable> out;
    out.reserve(p.programs.variables.size());
    for (const auto& v : p.programs.variables) {
        if (!isComposite(v.type)) {
            out.push_back(v);
            continue;
        }
        auto leaves = leafVariables(p, v);
        for (auto& l : leaves) out.push_back(std::move(l));
    }
    return out;
}

namespace {
bool spanOfLeaves(const std::vector<Variable>& leaves, Span& out) {
    out = Span{};
    bool any = false;
    for (const auto& leaf : leaves) {
        comm::Point pt;
        if (leaf.address.empty() || !equip::placeEquipmentAddress(leaf.address, equip::registerType(leaf), pt)) continue;
        const long long first = pt.offset;
        const long long last = static_cast<long long>(pt.offset) + (pt.bits() ? 0 : pt.size - 1);
        if (!any) {
            out.first = first;
            out.last = last;
            out.bits = pt.bits();
            any = true;
        } else {
            out.first = std::min(out.first, first);
            out.last = std::max(out.last, last);
        }
    }
    return any;
}
} // namespace

bool spanOf(const Project& p, const Variable& v, Span& out) {
    if (!v.bound() || v.address.empty()) return false;
    return spanOfLeaves(leafVariables(p, v), out);
}

std::string spanText(const Project& p, const Variable& v) {
    if (!v.bound() || v.address.empty()) return {};
    return spanTextOf(leafVariables(p, v), v.address);
}

std::string spanTextOf(const std::vector<Variable>& leaves, std::string_view startAddress) {
    Span s;
    if (!spanOfLeaves(leaves, s)) return {};
    const std::string raw = upperOf(trimmed(startAddress));
    const bool schneider = !raw.empty() && raw.front() == '%';
    Start st;
    (void)startOf(startAddress, st, nullptr);
    char a[24], b[24];
    if (s.bits) {
        const char lead = st.zone == Zone::Coil ? '0' : '1';
        const std::string what = st.zone == Zone::Coil ? "bobine" : "entr\xC3\xA9" "e TOR";
        if (schneider) {
            if (s.first == s.last) return "bit " + std::to_string(s.first);
            return "bits " + std::to_string(s.first) + " \xC3\xA0 " + std::to_string(s.last);
        }
        if (s.first == s.last) return what + " " + modicon(lead, s.first + 1, 5);
        return what + "s " + modicon(lead, s.first + 1, 5) + " \xC3\xA0 " + modicon(lead, s.last + 1, 5);
    }
    if (schneider) {
        if (s.first == s.last) return "mot " + std::to_string(s.first);
        return "mots " + std::to_string(s.first) + " \xC3\xA0 " + std::to_string(s.last);
    }
    const char lead = st.zone == Zone::Input ? '3' : '4';
    std::snprintf(a, sizeof a, "%s", modicon(lead, s.first + 1, st.digits).c_str());
    std::snprintf(b, sizeof b, "%s", modicon(lead, s.last + 1, st.digits).c_str());
    if (s.first == s.last) return std::string("registre ") + a;
    return std::string("registres ") + a + " \xC3\xA0 " + b;
}

// -------------------------------------------------- les proprietes ----------
const std::vector<PropertyInfo>& properties() {
    static const std::vector<PropertyInfo> all = {
        {"Length", "DINT", "le nombre de cases (toutes, pour deux dimensions : Rows \xC3\x97 Columns)", true, false},
        {"Size", "DINT", "le m\xC3\xAAme que Length, sous son autre nom", true, false},
        {"MaxSize", "DINT", "la capacit\xC3\xA9 d\xC3\xA9" "clar\xC3\xA9" "e ; un tableau a une taille fixe : \xC3\xA9gale \xC3\xA0 Length", true, false},
        {"Low", "DINT", "la borne basse (de la premi\xC3\xA8re dimension)", true, false},
        {"High", "DINT", "la borne haute (de la premi\xC3\xA8re dimension)", true, false},
        {"Low2", "DINT", "la borne basse de la deuxi\xC3\xA8me dimension", true, true},
        {"High2", "DINT", "la borne haute de la deuxi\xC3\xA8me dimension", true, true},
        {"Rows", "DINT", "le nombre de lignes (la premi\xC3\xA8re dimension)", true, false},
        {"Columns", "DINT", "le nombre de colonnes (la deuxi\xC3\xA8me dimension ; 1 pour un tableau \xC3\xA0 une dimension)", true, false},
        {"Dimensions", "INT", "1 ou 2", true, false},
        {"Bytes", "DINT", "le poids en octets (la place Modbus : 2 octets par mot)", false, false},
        {"Words", "DINT", "le poids en mots Modbus de 16 bits", false, false},
        {"ElementBytes", "DINT", "le poids d'une case, en octets (un BOOL : 1)", true, false},
    };
    return all;
}

const PropertyInfo* property(std::string_view name) noexcept {
    for (const auto& pi : properties())
        if (sameText(pi.name, name)) return &pi;
    return nullptr;
}

bool propertyValue(const Aggregate& a, std::string_view name, sim::Value& out) {
    const PropertyInfo* pi = property(name);
    if (!pi) return false;
    if (pi->arrayOnly && !a.array) return false;
    const auto dint = [&](long long v) {
        out = sim::Value::integer(sim::Type::DInt, v);
        return true;
    };
    const std::string n = upperOf(name);
    if (n == "BYTES") return dint(a.weight.bytes);
    if (n == "WORDS") return dint(a.weight.words);
    const Spec& s = a.spec;
    if (n == "LENGTH" || n == "SIZE" || n == "MAXSIZE") return dint(s.length());
    if (n == "LOW") return dint(s.low[0]);
    if (n == "HIGH") return dint(s.high[0]);
    if (n == "LOW2") return dint(s.dims == 2 ? s.low[1] : 0);
    if (n == "HIGH2") return dint(s.dims == 2 ? s.high[1] : 0);
    if (n == "ROWS") return dint(s.count(0));
    if (n == "COLUMNS") return dint(s.dims == 2 ? s.count(1) : 1);
    if (n == "DIMENSIONS") {
        out = sim::Value::integer(sim::Type::Int, s.dims);
        return true;
    }
    if (n == "ELEMENTBYTES") return dint(a.elementBytes);
    return false;
}

// ------------------------------------------------------ les chemins ---------
std::string typeOfPath(const Project& p, std::string_view path) {
    // La racine : une variable IHM.
    std::size_t i = 0;
    while (i < path.size() && (std::isalnum(static_cast<unsigned char>(path[i])) || path[i] == '_')) ++i;
    const auto* v = p.variable(path.substr(0, i));
    if (!v) return {};
    std::string type = normalized(v->type);
    while (i < path.size()) {
        const char c = path[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '[') {
            // Les indices ne sont pas evalues : on passe au crochet fermant.
            int depth = 0;
            std::size_t j = i;
            for (; j < path.size(); ++j) {
                if (path[j] == '[') ++depth;
                else if (path[j] == ']' && --depth == 0) break;
            }
            if (j >= path.size()) return {};
            Spec s;
            if (!parseSpec(type, s) || !s.array()) return {};
            type = s.element;
            i = j + 1;
            continue;
        }
        if (c == '.') {
            std::size_t j = i + 1;
            while (j < path.size() && (std::isalnum(static_cast<unsigned char>(path[j])) || path[j] == '_')) ++j;
            const std::string_view member = path.substr(i + 1, j - i - 1);
            Spec s;
            if (!parseSpec(type, s) || s.array() || isElementary(s.element)) return {};
            const auto* ty = p.hmiTypeByName(s.element);
            if (!ty) return {};
            const TypeMember* found = nullptr;
            for (const auto& m : ty->members)
                if (sameText(m.name, member)) found = &m;
            if (!found) return {};
            type = normalized(found->type);
            i = j;
            continue;
        }
        return {};
    }
    return type;
}

std::vector<TypeMember> membersOf(const Project& p, std::string_view typeName) {
    Spec s;
    if (!parseSpec(typeName, s) || s.array()) return {};
    const auto* ty = p.hmiTypeByName(s.element);
    return ty ? ty->members : std::vector<TypeMember>{};
}

std::string summary(const Project& p, std::string_view type, bool packBools) {
    Spec s;
    if (!parseSpec(type, s)) return {};
    const Weight w = weightOf(p, type, packBools);
    const std::string words = std::to_string(w.words) + " mot" + (w.words > 1 ? "s" : "");
    if (s.array()) {
        std::string dims = std::to_string(s.low[0]) + ".." + std::to_string(s.high[0]);
        if (s.dims == 2) dims += ", " + std::to_string(s.low[1]) + ".." + std::to_string(s.high[1]);
        return "tableau de " + std::to_string(s.length()) + " " + s.element + " (" + dims + "), " + words;
    }
    if (isElementary(s.element)) return s.element + ", " + words;
    const auto* ty = p.hmiTypeByName(s.element);
    if (!ty) return "type inconnu";
    return "structure " + ty->name + " : " + std::to_string(ty->members.size()) + " membre" + (ty->members.size() > 1 ? "s" : "") + ", " + words;
}

// ------------------------------------------------- la verification ---------
std::vector<PathProblem> pathProblems(const Project& p, std::string_view source) {
    std::vector<PathProblem> out;
    if (p.programs.variables.empty()) return out;
    // 1.11.1 (REP) : un script (ou une expression) se lit sans les $ de ses reperes ;
    // les lignes ne bougent pas.
    const std::string stripped = markers::strip(source);
    const std::string_view code = stripped;
    const auto isStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto isPart = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const std::size_t n = code.size();
    int line = 1;
    std::size_t i = 0;
    char prev = ' ';          // le dernier caractere significatif : '.' devant un nom = un membre
    while (i < n) {
        const char c = code[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '(' && i + 1 < n && code[i + 1] == '*') {            // (* commentaire *)
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == ')')) { if (code[i] == '\n') ++line; ++i; }
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {             // // commentaire
            while (i < n && code[i] != '\n') ++i;
            continue;
        }
        if (c == '\'' || c == '"') {                                   // une chaine
            ++i;
            while (i < n && code[i] != c) { if (code[i] == '\n') ++line; ++i; }
            ++i;
            prev = c;
            continue;
        }
        if (!isStart(c)) {
            if (!std::isspace(static_cast<unsigned char>(c))) prev = c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && isPart(code[j])) ++j;
        const std::string_view name = code.substr(i, j - i);
        const bool root = prev != '.' && prev != '#';
        prev = 'a';
        i = j;
        const Variable* v = root ? p.variable(name) : nullptr;
        if (!v || !isComposite(v->type)) continue;
        // La chaine qui suit : [..], .membre, .Propriete
        std::string type = normalized(v->type);
        std::string path(name);
        const int at = line;
        while (true) {
            std::size_t k = i;
            while (k < n && (code[k] == ' ' || code[k] == '\t')) ++k;
            if (k < n && code[k] == '[') {
                int depth = 0;
                std::size_t e = k;
                for (; e < n; ++e) {
                    if (code[e] == '[') ++depth;
                    else if (code[e] == ']' && --depth == 0) break;
                    else if (code[e] == '\n') break;
                }
                if (e >= n || code[e] != ']') break;
                const std::string inside(code.substr(k + 1, e - k - 1));
                Spec s;
                if (!parseSpec(type, s) || !s.array()) {
                    out.push_back({at, path + " n'est pas un tableau : pas d'indice entre crochets"});
                    i = e + 1;
                    break;
                }
                const auto parts = splitList(inside);
                if (static_cast<int>(parts.size()) != s.dims) {
                    out.push_back({at, path + " a " + std::to_string(s.dims) + " dimension" + (s.dims > 1 ? "s" : "") + " : " + std::to_string(s.dims)
                                           + " indice" + (s.dims > 1 ? "s" : "") + " entre crochets (" + specText(s) + ")"});
                } else {
                    for (int d = 0; d < s.dims; ++d) {
                        long long x = 0;
                        if (!integerOf(parts[static_cast<std::size_t>(d)], x)) continue;
                        if (x < s.low[d] || x > s.high[d])
                            out.push_back({at, path + "[" + inside + "] : l'indice " + std::to_string(x) + " est hors des bornes du tableau ("
                                                   + std::to_string(s.low[d]) + ".." + std::to_string(s.high[d]) + ")"});
                    }
                }
                path += "[" + inside + "]";
                type = s.element;
                i = e + 1;
                continue;
            }
            if (k < n && code[k] == '.' && k + 1 < n && (isStart(code[k + 1]) || std::isdigit(static_cast<unsigned char>(code[k + 1])))) {
                std::size_t e = k + 1;
                while (e < n && isPart(code[e])) ++e;
                const std::string member(code.substr(k + 1, e - k - 1));
                i = e;
                Spec s;
                if (!parseSpec(type, s)) break;
                const auto written = [&] {
                    std::size_t q = e;
                    while (q < n && (code[q] == ' ' || code[q] == '\t')) ++q;
                    return q + 1 < n && code[q] == ':' && code[q + 1] == '=';
                };
                if (s.array()) {
                    const PropertyInfo* pi = property(member);
                    if (!pi) out.push_back({at, path + " est un tableau : ." + member + " n'existe pas (Length, Low, High... ou [i] pour une case)"});
                    else if (pi->twoD && s.dims != 2) out.push_back({at, path + "." + member + " : pour un tableau \xC3\xA0 deux dimensions"});
                    else if (written()) out.push_back({at, path + "." + member + " : une propri\xC3\xA9t\xC3\xA9 se lit seulement"});
                    break;
                }
                if (isElementary(s.element)) {
                    if (!std::isdigit(static_cast<unsigned char>(member[0])))
                        out.push_back({at, path + " est un " + s.element + " : pas de membre ." + member});
                    break;
                }
                const auto* ty = p.hmiTypeByName(s.element);
                if (!ty) break;
                const TypeMember* found = nullptr;
                for (const auto& m : ty->members)
                    if (sameText(m.name, member)) found = &m;
                if (found) {
                    path += "." + found->name;
                    type = normalized(found->type);
                    continue;
                }
                const PropertyInfo* pi = property(member);
                if (pi && !pi->arrayOnly) {
                    if (written()) out.push_back({at, path + "." + member + " : une propri\xC3\xA9t\xC3\xA9 se lit seulement"});
                    break;
                }
                std::string list;
                for (const auto& m : ty->members) list += (list.empty() ? "" : ", ") + m.name;
                out.push_back({at, path + " (" + ty->name + ") n'a pas de membre " + member + (list.empty() ? std::string{} : " (membres : " + list + ")")});
                break;
            }
            break;
        }
    }
    return out;
}

// ------------------------------------------------------ les dossiers --------
std::vector<std::string> folderChain(std::string_view folder) {
    std::vector<std::string> out;
    const std::string f = trimmed(folder);
    if (f.empty()) return out;
    std::size_t from = 0;
    while (true) {
        const auto slash = f.find('/', from);
        out.push_back(f.substr(0, slash));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return out;
}

std::string folderLeaf(std::string_view folder) {
    const auto slash = folder.rfind('/');
    return std::string(slash == std::string_view::npos ? folder : folder.substr(slash + 1));
}

std::string folderParent(std::string_view folder) {
    const auto slash = folder.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(folder.substr(0, slash));
}

bool validFolder(std::string_view folder, std::string* why) {
    const std::string f = trimmed(folder);
    if (f.empty()) return fail(why, "nom de dossier vide");
    std::size_t from = 0;
    while (true) {
        const auto slash = f.find('/', from);
        const std::string part = trimmed(std::string_view(f).substr(from, slash == std::string::npos ? std::string::npos : slash - from));
        if (part.empty()) return fail(why, "dossier \xC2\xAB " + f + " \xC2\xBB : un nom vide entre deux /");
        if (part.find('"') != std::string::npos || part.find('\\') != std::string::npos)
            return fail(why, "dossier \xC2\xAB " + part + " \xC2\xBB : ni guillemet ni \\");
        if (part.size() > 60) return fail(why, "dossier \xC2\xAB " + part + " \xC2\xBB : 60 caract\xC3\xA8res au plus");
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return true;
}

std::vector<std::string> allFolders(const Project& p) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    const auto add = [&](std::string_view f) {
        for (const auto& c : folderChain(f))
            if (seen.insert(upperOf(c)).second) out.push_back(c);
    };
    for (const auto& f : p.programs.folders) add(f);
    for (const auto& v : p.programs.variables) add(v.folder);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return upperOf(a) < upperOf(b); });
    return out;
}

} // namespace hmi::types
