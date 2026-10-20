// hmi/HmiAddressPlan.cpp - « Modifier l'adresse dans toutes les cases... » (1.12.18).
#include "HmiAddressPlan.hpp"

#include "HmiEquipment.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace hmi::addr {

namespace ty = hmi::types;

namespace {

std::string upper(std::string_view s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 || n == -1 ? one : many); }

// Le chemin relatif d'une case (EXTRACTEURS[2].Retour -> [2].Retour ; Four.Heures -> Heures).
std::string relOf(const Variable& v, const Variable& leaf) {
    const std::string rest = leaf.name.substr(std::min(leaf.name.size(), v.name.size()));
    return !rest.empty() && rest.front() == '.' ? rest.substr(1) : rest;
}

// Le motif de la case : jusqu'au dernier [*] ("[*].Retour" -> "[*]", "[*].V[*].P" -> "[*].V[*]").
std::string casePattern(std::string_view pattern) {
    const std::string p(pattern);
    const auto at = p.rfind("[*]");
    return at == std::string::npos ? std::string{} : p.substr(0, at + 3);
}

struct Cell {
    std::string              cell, target;
    std::vector<long long>   idx;
    std::vector<std::size_t> leaves;   // les cases simples sous `target` (liees)
};

std::vector<Cell> cellsOf(const std::vector<Variable>& leaves, const Variable& v, std::string_view pattern) {
    std::vector<Cell> out;
    std::map<std::string, std::size_t> at;
    const std::string cp = casePattern(pattern);
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        if (!leaves[i].bound()) continue;   // un membre interne : ni equipement, ni adresse
        const std::string rel = relOf(v, leaves[i]);
        std::vector<long long> idx;
        std::string target;
        if (!ty::patternIndices(pattern, rel, idx, &target)) continue;
        auto it = at.find(upper(target));
        if (it == at.end()) {
            Cell c;
            c.target = target;
            c.idx = std::move(idx);
            std::vector<long long> ignored;
            if (cp.empty() || !ty::patternIndices(cp, rel, ignored, &c.cell)) c.cell = target;
            it = at.emplace(upper(target), out.size()).first;
            out.push_back(std::move(c));
        }
        out[it->second].leaves.push_back(i);
    }
    return out;
}

// La place d'une case simple : sa zone, son mot (ou son bit), son bit dans le mot.
struct Spot {
    comm::Area area{comm::Area::Holding};
    long long  word{0};
    int        bit{-1};     // un bit d'un mot ; -1 : le mot entier
    long long  words{1};
};
bool spotOf(const Variable& leaf, Spot& s, std::string* why = nullptr) {
    comm::Point pt;
    if (trim(leaf.address).empty()) return fail(why, "sans adresse");
    if (!equip::placeEquipmentAddress(leaf.address, equip::registerType(leaf), pt, why)) return false;
    s.area = pt.area;
    s.word = pt.offset;
    s.bit = pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1;
    s.words = std::max<long long>(1, pt.size);
    return true;
}

// L'adresse de ce qui est vise : la case simple elle-meme, ou le depart d'une case entiere (la plus petite place).
std::string startOf(const std::vector<Variable>& leaves, const Cell& c, const Variable& v) {
    if (c.leaves.size() == 1 && upper(relOf(v, leaves[c.leaves.front()])) == upper(c.target)) return leaves[c.leaves.front()].address;
    std::string best;
    long long bestKey = -1;
    for (const auto i : c.leaves) {
        Spot s;
        if (!spotOf(leaves[i], s)) continue;
        const long long key = s.word * 32 + std::max(0, s.bit);
        if (bestKey < 0 || key < bestKey) {
            bestKey = key;
            best = leaves[i].address;
        }
    }
    return best;
}

// Qui occupe une place : les mots entiers, les bits d'un mot (une bobine : son numero, comme un mot).
struct Occupancy {
    std::map<std::pair<int, long long>, std::vector<std::string>>           words;
    std::map<std::tuple<int, long long, int>, std::vector<std::string>>     bits;
    std::map<std::pair<int, long long>, std::vector<std::string>>           someBit;
    void add(const Spot& s, const std::string& name) {
        const int a = static_cast<int>(s.area);
        if (s.bit >= 0) {
            bits[{a, s.word, s.bit}].push_back(name);
            someBit[{a, s.word}].push_back(name);
        } else {
            for (long long w = s.word; w < s.word + s.words; ++w) words[{a, w}].push_back(name);
        }
    }
    [[nodiscard]] std::vector<std::string> who(const Spot& s) const {
        std::vector<std::string> out;
        const auto take = [&out](const std::vector<std::string>& names) {
            for (const auto& n : names)
                if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        };
        const int a = static_cast<int>(s.area);
        if (s.bit >= 0) {
            if (const auto it = words.find({a, s.word}); it != words.end()) take(it->second);
            if (const auto it = bits.find({a, s.word, s.bit}); it != bits.end()) take(it->second);
        } else {
            for (long long w = s.word; w < s.word + s.words; ++w) {
                if (const auto it = words.find({a, w}); it != words.end()) take(it->second);
                if (const auto it = someBit.find({a, w}); it != someBit.end()) take(it->second);
            }
        }
        return out;
    }
};

// Un depart lu : "%MW551.0" -> "%MW", 551, 0 ; "4x0101" -> "4x", 101 ; "40101" -> "", 40101.
bool parseStart(std::string_view text, std::string& prefix, long long& number, int& bit) {
    const std::string t = trim(text);
    std::size_t k = 0;
    if (t.size() > 2 && std::isdigit(static_cast<unsigned char>(t[0])) && (t[1] == 'x' || t[1] == 'X')) k = 2;
    else
        while (k < t.size() && !std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
    prefix = t.substr(0, k);
    std::size_t e = k;
    while (e < t.size() && std::isdigit(static_cast<unsigned char>(t[e]))) ++e;
    if (e == k || e - k > 9) return false;
    number = std::stoll(t.substr(k, e - k));
    bit = -1;
    if (e == t.size()) return true;
    if (t[e] != '.' || e + 1 >= t.size()) return false;
    long long b = 0;
    for (std::size_t q = e + 1; q < t.size(); ++q) {
        if (!std::isdigit(static_cast<unsigned char>(t[q]))) return false;
        b = b * 10 + (t[q] - '0');
        if (b > 15) return false;
    }
    bit = static_cast<int>(b);
    return true;
}

// n + pas * K, ecrit simplement.
std::string term(long long n, long long stride, const std::string& k) {
    if (stride == 0) return std::to_string(n);
    const std::string step = stride == 1 ? k : std::to_string(stride) + " * " + k;
    return n == 0 ? step : std::to_string(n) + " + " + step;
}

// Le rang d'une case dans l'ordre des cases (ligne par ligne) : "i", "(i - 1)", "(i * 10 + j)".
bool ordinalOf(const std::vector<Cell>& cells, std::string& out, std::string* why) {
    const std::size_t dims = cells.front().idx.size();
    if (dims == 0) return fail(why, "pas d'indice [*] : rien ne varie d'une case \xC3\xA0 l'autre");
    if (dims > 2) return fail(why, "\xC3\xA0 la suite : deux indices au plus (i et j)");
    long long lowI = cells.front().idx[0], lowJ = dims > 1 ? cells.front().idx[1] : 0, highJ = lowJ;
    for (const auto& c : cells) {
        if (c.idx.size() != dims) return fail(why, "des cases aux indices diff\xC3\xA9rents");
        lowI = std::min(lowI, c.idx[0]);
        if (dims > 1) {
            lowJ = std::min(lowJ, c.idx[1]);
            highJ = std::max(highJ, c.idx[1]);
        }
    }
    const std::string i = lowI == 0 ? std::string("i") : "(i - " + std::to_string(lowI) + ")";
    if (dims == 1) {
        out = i;
        return true;
    }
    const std::string j = lowJ == 0 ? std::string("j") : "(j - " + std::to_string(lowJ) + ")";
    out = "(" + i + " * " + std::to_string(highJ - lowJ + 1) + " + " + j + ")";
    return true;
}

bool isDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

} // namespace

// ------------------------------------------------------------------ motifs ---
std::string patternOf(std::string_view rel, bool wholeCases) {
    std::string all;
    for (std::size_t i = 0; i < rel.size(); ++i) {
        if (rel[i] != '[') {
            all += rel[i];
            continue;
        }
        const auto close = rel.find(']', i);
        if (close == std::string_view::npos) {
            all += rel.substr(i);
            break;
        }
        all += "[*]";
        i = close;
    }
    if (all.find("[*]") == std::string::npos) return {};
    return wholeCases ? casePattern(all) : all;
}

bool split(std::string_view formula, std::string& prefix, std::string& expression, std::string& bit) {
    const std::string f = trim(formula);
    const auto open = f.find('(');
    if (open == std::string::npos) return false;
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t k = open; k < f.size(); ++k) {
        if (f[k] == '(') ++depth;
        else if (f[k] == ')' && --depth == 0) {
            close = k;
            break;
        }
    }
    if (close == std::string::npos) return false;
    prefix = trim(std::string_view(f).substr(0, open));
    expression = trim(std::string_view(f).substr(open + 1, close - open - 1));
    std::string rest = trim(std::string_view(f).substr(close + 1));
    bit.clear();
    if (rest.empty()) return true;
    if (rest.front() != '.') return false;
    rest = trim(std::string_view(rest).substr(1));
    if (rest.size() >= 2 && rest.front() == '(' && rest.back() == ')') rest = trim(std::string_view(rest).substr(1, rest.size() - 2));
    bit = rest;
    return !bit.empty();
}

std::string join(std::string_view prefix, std::string_view expression, std::string_view bit) {
    std::string out = trim(prefix) + "(" + trim(expression) + ")";
    const std::string b = trim(bit);
    if (!b.empty()) out += isDigits(b) ? "." + b : ".(" + b + ")";
    return out;
}

// ------------------------------------------------------------------- forme ---
Shape shape(const Project& p, const Variable& v, std::string_view pattern) {
    Shape s;
    const auto leaves = ty::leafVariables(p, v);
    const auto cells = cellsOf(leaves, v, pattern);
    s.cases = cells.size();
    s.bits = !v.address.empty() && ty::bitArea(v.address);
    if (cells.empty()) return s;
    const Cell& c = cells.front();
    s.dims = static_cast<int>(c.idx.size());
    // "EXTRACTEURS[i].Retour" : les [*] du motif en i, j.
    {
        std::string path = v.name + (!pattern.empty() && pattern.front() != '[' ? "." : "");
        int next = 0;
        const std::string pt(pattern);
        for (std::size_t k = 0; k < pt.size(); ++k) {
            if (pt.compare(k, 3, "[*]") != 0) {
                path += pt[k];
                continue;
            }
            // Un [*] qui couvre deux dimensions ([1,2]) : [i, j].
            std::vector<long long> mine;
            std::string upTo;
            (void)ty::patternIndices(pt.substr(0, k + 3), relOf(v, leaves[c.leaves.front()]), mine, &upTo);
            const std::size_t here = mine.size() - static_cast<std::size_t>(next);
            path += "[";
            for (std::size_t q = 0; q < here; ++q) path += (q ? ", " : "") + std::string(next == 0 ? "i" : next == 1 ? "j" : "k"), ++next;
            path += "]";
            k += 2;
        }
        s.path = path;
    }
    for (int d = 0; d < s.dims; ++d) {
        long long lo = c.idx[static_cast<std::size_t>(d)], hi = lo;
        for (const auto& x : cells) {
            lo = std::min(lo, x.idx[static_cast<std::size_t>(d)]);
            hi = std::max(hi, x.idx[static_cast<std::size_t>(d)]);
        }
        s.indices += (d ? ", " : "") + std::string(d == 0 ? "i" : "j") + " de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi);
    }
    const Variable& first = leaves[c.leaves.front()];
    s.leaf = c.leaves.size() == 1 && upper(relOf(v, first)) == upper(c.target);
    if (s.leaf) {
        Spot sp;
        const bool read = spotOf(first, sp);
        s.boolInWord = first.type == "BOOL" && !s.bits && read && sp.bit >= 0;
        if (first.type == "BOOL" && (s.bits || s.boolInWord)) s.text = "BOOL, 1 bit";
        else s.text = first.type + ", " + plural(ty::wordsOf(first.type), "mot", "mots");
    } else {
        long long lo = -1, hi = -1;
        for (const auto i : c.leaves) {
            Spot sp;
            if (!spotOf(leaves[i], sp)) continue;
            if (lo < 0 || sp.word < lo) lo = sp.word;
            hi = std::max(hi, sp.word + (sp.bit >= 0 ? 1 : sp.words));
        }
        s.text = plural(static_cast<long long>(c.leaves.size()), "membre", "membres");
        if (lo >= 0) s.text += ", " + plural(hi - lo, s.bits ? "bit" : "mot", s.bits ? "bits" : "mots");
    }
    return s;
}

// ------------------------------------------------------------- la variable ---
Variable withFormula(const Variable& v, std::string_view pattern, std::string_view formula) {
    Variable x = v;
    const std::string key = upper(pattern);
    std::erase_if(x.places, [&](const MemberAddress& m) { return upper(m.path) == key || ty::memberCovers(pattern, m.path); });
    if (!trim(formula).empty()) x.places.push_back({std::string(pattern), trim(formula)});
    return x;
}

std::size_t coveredPlaces(const Variable& v, std::string_view pattern) {
    const std::string key = upper(pattern);
    return static_cast<std::size_t>(std::count_if(v.places.begin(), v.places.end(), [&](const MemberAddress& m) {
        return !trim(m.address).empty() && (upper(m.path) == key || ty::memberCovers(pattern, m.path));
    }));
}

// ----------------------------------------------------------------- l'apercu ---
Plan preview(const Project& p, const Variable& v, std::string_view pattern, std::string_view formula) {
    Plan plan;
    plan.pattern = std::string(pattern);
    plan.formula = trim(formula);
    if (!v.bound()) {
        plan.errors.push_back(v.name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement");
        return plan;
    }
    const auto before = ty::leafVariables(p, v);
    const auto cells = cellsOf(before, v, pattern);
    if (cells.empty()) {
        plan.errors.push_back("aucune case de " + v.name + " ne r\xC3\xA9pond \xC3\xA0 " + std::string(pattern));
        return plan;
    }
    const Shape sh = shape(p, v, pattern);
    plan.what = sh.path + " \xC2\xB7 " + sh.text;
    if (plan.formula.empty()) plan.errors.push_back("\xC3\xA9" "cris la formule (1000 + 10 * i)");
    // Une case entiere part d'un mot si la variable est dans des mots, d'un bit si elle est dans des bits.
    if (!plan.formula.empty() && !sh.leaf && !v.address.empty()) {
        std::string why;
        const std::string start = ty::formulaAddress(plan.formula, cells.front().idx, &why);
        if (!start.empty() && ty::bitArea(start) != sh.bits)
            plan.errors.push_back("les cases de " + v.name + " sont dans des " + (sh.bits ? "bits" : "mots") + " (" + v.address + ") : leur d\xC3\xA9part aussi");
    }
    std::vector<std::string> why;
    const Variable x = withFormula(v, pattern, plan.formula);
    const auto after = ty::leafVariables(p, x, nullptr, &why);
    // Les autres variables du meme equipement : leurs places.
    Occupancy others;
    for (const auto& o : p.programs.variables) {
        if (o.id == v.id || !o.bound() || upper(o.equipment) != upper(v.equipment)) continue;
        for (const auto& leaf : ty::leafVariables(p, o)) {
            Spot s;
            if (leaf.bound() && spotOf(leaf, s)) others.add(s, leaf.name);
        }
    }
    // Les cases de la variable elle-meme, apres. 1.12.19 : deux a la meme place (dont une qui change) ne
    // bloque plus - modifier pour toutes les cases, c'est accepter les chevauchements ; l'apercu les montre
    // comme ceux des autres variables (« ... l'a aussi », en ambre).
    std::set<std::size_t> changed;
    for (const auto& c : cells) changed.insert(c.leaves.begin(), c.leaves.end());
    Occupancy self;
    std::vector<Spot> spots(after.size());
    std::vector<char> placed(after.size(), 0);
    std::set<std::string> changedNames;
    for (const auto i : changed) if (i < after.size()) changedNames.insert(after[i].name);
    std::vector<std::vector<std::string>> selfShared(after.size());
    std::map<std::string, std::size_t> indexOf;
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (!after[i].bound() || !spotOf(after[i], spots[i])) continue;
        placed[i] = 1;
        for (const auto& n : self.who(spots[i]))
            if (changedNames.count(n) || changedNames.count(after[i].name)) {
                selfShared[i].push_back(n);
                if (const auto it = indexOf.find(n); it != indexOf.end()) selfShared[it->second].push_back(after[i].name);
            }
        self.add(spots[i], after[i].name);
        indexOf.emplace(after[i].name, i);
    }
    for (const auto& c : cells) {
        Row r;
        r.cell = c.cell;
        r.target = c.target;
        r.before = startOf(before, c, v);
        r.after = startOf(after, c, v);
        for (const auto i : c.leaves) {
            if (i >= after.size()) continue;
            if (r.error.empty() && i < why.size() && !why[i].empty() && after[i].address.empty()) r.error = why[i];
            if (r.error.empty() && !placed[i]) {
                std::string bad;
                Spot s;
                if (!spotOf(after[i], s, &bad)) r.error = after[i].address.empty() ? bad : after[i].address + " : " + bad;
            }
            if (placed[i]) {
                for (auto& n : others.who(spots[i]))
                    if (std::find(r.sharedWith.begin(), r.sharedWith.end(), n) == r.sharedWith.end()) r.sharedWith.push_back(std::move(n));
                for (const auto& n : selfShared[i])
                    if (std::find(r.sharedWith.begin(), r.sharedWith.end(), n) == r.sharedWith.end()) r.sharedWith.push_back(n);
            }
        }
        if (!r.error.empty()) r.after.clear();
        if (!r.sharedWith.empty()) ++plan.shared;
        plan.rows.push_back(std::move(r));
    }
    std::size_t bad = 0;
    std::string firstBad;
    for (const auto& r : plan.rows)
        if (!r.error.empty() && ++bad == 1) firstBad = v.name + r.cell + " : " + r.error;
    if (bad && !plan.formula.empty())
        plan.errors.insert(plan.errors.begin(), firstBad + (bad > 1 ? " (" + plural(static_cast<long long>(bad), "case", "cases") + " sans adresse)" : std::string{}));
    return plan;
}

// --------------------------------------------------------------- a la suite ---
std::string following(const Project& p, const Variable& v, std::string_view pattern, std::string_view start, std::string* why) {
    const auto leaves = ty::leafVariables(p, v);
    const auto cells = cellsOf(leaves, v, pattern);
    if (cells.empty()) return fail(why, "aucune case ne r\xC3\xA9pond \xC3\xA0 " + std::string(pattern)), std::string{};
    std::string k;
    if (!ordinalOf(cells, k, why)) return {};
    std::string prefix;
    long long n = 0;
    int bit = -1;
    if (!parseStart(start, prefix, n, bit) || equip::canonicalAddress(trim(start)).empty())
        return fail(why, "adresse illisible \xC2\xAB " + trim(start) + " \xC2\xBB (%MW1000, %M200, %MW551.0, 40001)"), std::string{};
    const Shape sh = shape(p, v, pattern);
    const bool bitsZone = bit < 0 && ty::bitArea(trim(start));
    const Variable& first = leaves[cells.front().leaves.front()];
    if (sh.leaf) {
        const bool isBool = first.type == "BOOL";
        if (bitsZone) {
            if (!isBool)
                return fail(why, "un " + first.type + " ne tient pas dans un bit (" + trim(start) + ") : partir d'un mot (%MW, 40001)"), std::string{};
            return join(prefix, term(n, 1, k));
        }
        if (bit >= 0) {
            if (!isBool) return fail(why, "un " + first.type + " prend des mots entiers : partir d'un mot, sans bit"), std::string{};
            // Les bits qui suivent, puis le mot suivant : (b + K) / 16 mots, le bit (b + K) MOD 16.
            const std::string over = bit == 0 ? k : "(" + std::to_string(bit) + " + " + k + ")";
            return join(prefix, std::to_string(n) + " + " + over + " / 16", over + " MOD 16");
        }
        return join(prefix, term(n, ty::wordsOf(first.type), k));
    }
    // Les cases entieres : chacune son poids (un depart de case est un mot, ou un bit comme la variable).
    if (bit >= 0) return fail(why, "une case part d'un mot, pas d'un bit (" + trim(start) + ")"), std::string{};
    if (!v.address.empty() && bitsZone != sh.bits)
        return fail(why, std::string("les cases de ") + v.name + " sont dans des " + (sh.bits ? "bits" : "mots") + " (" + v.address + ") : leur d\xC3\xA9part aussi"), std::string{};
    long long stride = 0;
    if (bitsZone) {
        for (const auto i : cells.front().leaves) stride += leaves[i].type == "BOOL" ? 1 : 0;
    } else {
        const std::string full = v.name + (!cells.front().cell.empty() && cells.front().cell.front() != '[' ? "." : "") + cells.front().cell;
        for (const auto& a : ty::flatten(p, v.name, v.type, v.initial, v.packBools).aggregates)
            if (upper(a.path) == upper(full)) stride = a.weight.words;
    }
    if (stride <= 0) return fail(why, "le poids d'une case est inconnu"), std::string{};
    return join(prefix, term(n, stride, k));
}

std::string current(const Project& p, const Variable& v, std::string_view pattern) {
    const auto leaves = ty::leafVariables(p, v);
    const auto cells = cellsOf(leaves, v, pattern);
    if (cells.empty()) return {};
    const std::string a0 = startOf(leaves, cells.front(), v);
    if (a0.empty()) return {};
    std::string k;
    if (cells.size() < 2 || !ordinalOf(cells, k, nullptr)) return following(p, v, pattern, a0);
    std::string p0, p1;
    long long n0 = 0, n1 = 0;
    int b0 = -1, b1 = -1;
    if (!parseStart(a0, p0, n0, b0) || !parseStart(startOf(leaves, cells[1], v), p1, n1, b1) || upper(p0) != upper(p1) || b0 != b1 || n1 <= n0)
        return following(p, v, pattern, a0);
    // Un REAL de Schneider s'ecrit %MF : la formule garde le prefixe des mots (%MW) quand la case est un mot.
    return join(p0, term(n0, n1 - n0, k), b0 >= 0 ? std::to_string(b0) : std::string{});
}

} // namespace hmi::addr
