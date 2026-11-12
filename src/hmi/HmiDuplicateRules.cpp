// 1.50.0 (maquette, planche 04) : Dupliquer pour 500 reperes - les compteurs, les groupes, les regles (voir
// HmiDuplicateRules.hpp). Le calcul des formules est celui des adresses (hmi::types::formulaValue, namedFormulaAddress).
#include "HmiDuplicateRules.hpp"

#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>
#include <utility>

namespace hmi::dup {

namespace {

const std::string kDot = "\xC2\xB7";   // le point du milieu : "Departs[.]"

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// "API.Armoires[0].Departs" -> "API.Armoires[\xC2\xB7].Departs" (les indices d'avant : le point du milieu).
std::string dotted(std::string_view path) {
    std::string out;
    int depth = 0;
    for (const char c : path) {
        if (c == '[') {
            if (depth++ == 0) out += "[" + kDot;
            continue;
        }
        if (c == ']') {
            if (depth > 0 && --depth == 0) out += ']';
            continue;
        }
        if (depth == 0) out += c;
    }
    return out;
}

// Les crochets d'un chemin (A[0].B[1].C : 2) - la profondeur d'un indice de C est 3.
int bracketsOf(std::string_view path) {
    int n = 0, depth = 0;
    for (const char c : path) {
        if (c == '[' && depth++ == 0) ++n;
        else if (c == ']' && depth > 0) --depth;
    }
    return n;
}

// Le crochet d'un indice : "[\xC2\xB7]" ; deux dimensions : "[\xC2\xB7, *]".
std::string slotBracket(int dim, int dims) {
    if (dims <= 1) return "[" + kDot + "]";
    std::string inside;
    for (int k = 0; k < dims; ++k) inside += (k ? ", " : "") + (k == dim ? kDot : std::string("*"));
    return "[" + inside + "]";
}

// "0", "0..3", "0, 2, 5", "0, 2, 5, 7\xE2\x80\xA6"
std::string valuesText(const std::vector<long long>& v) {
    if (v.empty()) return {};
    if (v.size() == 1) return std::to_string(v.front());
    bool contiguous = true;
    for (std::size_t i = 1; i < v.size(); ++i) contiguous = contiguous && v[i] == v[i - 1] + 1;
    if (contiguous) return std::to_string(v.front()) + ".." + std::to_string(v.back());
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < 4; ++i) out += (i ? ", " : "") + std::to_string(v[i]);
    if (v.size() > 4) out += "\xE2\x80\xA6";
    return out;
}

void addValue(std::vector<long long>& v, long long x) {
    const auto it = std::lower_bound(v.begin(), v.end(), x);
    if (it == v.end() || *it != x) v.insert(it, x);
}

const char* letterOf(int depth) {
    switch (depth) {
    case 1: return "i";
    case 2: return "j";
    case 3: return "k";
    default: return nullptr;
    }
}

} // namespace

// ------------------------------------------------------------- compteurs ---
long long Counters::places() const noexcept {
    if (mode == CountMode::Series) return static_cast<long long>(std::max(0, copies)) + 1;
    if (grid.empty()) return 1;
    long long n = 1;
    for (const auto& r : grid) {
        n *= r.size();
        if (n == 0 || n > 1000000) return n;
    }
    return n;
}

Place placeAt(const Counters& c, long long p) {
    Place out;
    out.n = p;
    if (c.mode == CountMode::Series) {
        out.i = p;
        return out;
    }
    long long rest = p;
    long long* slot[3] = {&out.i, &out.j, &out.k};
    for (std::size_t q = c.grid.size(); q-- > 0;) {
        const long long size = std::max<long long>(1, c.grid[q].size());
        if (q < 3) *slot[q] = c.grid[q].from + rest % size;
        rest /= size;
    }
    return out;
}

std::string placeText(const Counters& c, const Place& at) {
    if (c.mode == CountMode::Series) return "n = " + std::to_string(at.n);
    std::string out;
    const long long values[3] = {at.i, at.j, at.k};
    for (std::size_t q = 0; q < c.grid.size() && q < 3; ++q) out += (q ? ", " : "") + std::string(letterOf(static_cast<int>(q) + 1)) + " = " + std::to_string(values[q]);
    return out;
}

// -------------------------------------------------------------- formules ---
std::vector<std::pair<std::string, long long>> namesOf(const Counters& c, const Vars& v) {
    std::vector<std::pair<std::string, long long>> out;
    if (c.mode == CountMode::Series) out.emplace_back("i", v.at.i);
    else {
        const long long values[3] = {v.at.i, v.at.j, v.at.k};
        for (std::size_t q = 0; q < c.grid.size() && q < 3; ++q) out.emplace_back(letterOf(static_cast<int>(q) + 1), values[q]);
    }
    out.emplace_back("n", v.at.n);
    out.emplace_back("o", v.o);
    return out;
}

bool evalInteger(std::string_view formula, const Counters& c, const Vars& v, long long& out, std::string* why) {
    if (trim(formula).empty()) {
        if (why) *why = "formule vide";
        return false;
    }
    return types::formulaValue(formula, namesOf(c, v), out, why);
}

std::optional<AddressText> addressOf(std::string_view text) {
    const std::string t = trim(text);
    if (t.size() < 3 || t[0] != '%') return std::nullopt;
    std::size_t k = 1;
    while (k < t.size() && std::isalpha(static_cast<unsigned char>(t[k]))) ++k;
    if (k == 1 || k > 4) return std::nullopt;
    std::size_t e = k;
    while (e < t.size() && std::isdigit(static_cast<unsigned char>(t[e]))) ++e;
    if (e == k || e - k > 9) return std::nullopt;
    AddressText a;
    a.area = t.substr(0, k);
    a.number = std::stoll(t.substr(k, e - k));
    if (e < t.size()) {
        if (t[e] != '.' || e + 1 >= t.size()) return std::nullopt;
        for (std::size_t q = e + 1; q < t.size(); ++q)
            if (!std::isdigit(static_cast<unsigned char>(t[q]))) return std::nullopt;
        a.rest = t.substr(e);
    }
    return a;
}

std::string evalAddress(std::string_view formula, std::string_view original, const Counters& c, const Vars& v, std::string* why) {
    std::string f = trim(formula);
    if (f.empty()) {
        if (why) *why = "formule vide";
        return {};
    }
    // "(o + n)" : la zone (et le bit) de l'original.
    if (f.front() == '(')
        if (const auto a = addressOf(original)) f = a->area + f + a->rest;
    return types::namedFormulaAddress(f, namesOf(c, v), why);
}

std::string evalText(std::string_view tmpl, const Counters& c, const Vars& v, std::string* why) {
    std::string t = trim(tmpl);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') t = t.substr(1, t.size() - 2);
    std::string out;
    for (std::size_t k = 0; k < t.size();) {
        const char ch = t[k];
        if ((ch == '{' || ch == '}') && k + 1 < t.size() && t[k + 1] == ch) {
            out += ch;
            k += 2;
            continue;
        }
        if (ch != '{') {
            out += ch;
            ++k;
            continue;
        }
        const std::size_t end = t.find('}', k + 1);
        if (end == std::string::npos) {
            if (why) *why = "il manque une accolade fermante";
            return {};
        }
        std::string inside = t.substr(k + 1, end - k - 1);
        std::size_t width = 0;
        if (const auto colon = inside.rfind(':'); colon != std::string::npos) {
            const std::string fmt = trim(inside.substr(colon + 1));
            if (fmt.empty() || fmt.size() > 2 || !std::all_of(fmt.begin(), fmt.end(), [](char d) { return std::isdigit(static_cast<unsigned char>(d)) != 0; })) {
                if (why) *why = "\xC2\xAB :" + fmt + " \xC2\xBB : un nombre de chiffres ({j:02})";
                return {};
            }
            width = static_cast<std::size_t>(std::stoi(fmt));
            inside.resize(colon);
        }
        long long value = 0;
        if (!evalInteger(inside, c, v, value, why)) return {};
        std::string num = std::to_string(value < 0 ? -value : value);
        if (num.size() < width) num.insert(0, width - num.size(), '0');
        out += (value < 0 ? "-" : "") + num;
        k = end + 1;
    }
    return out;
}

// -------------------------------------------------------------- groupes ---
std::string depthKey(std::string_view path, int dim, int dims, bool marked) {
    std::string g;
    int depth = 0;
    for (const char ch : path) {
        if (ch == '[') {
            if (depth++ == 0) g += "[*]";
            continue;
        }
        if (ch == ']') {
            if (depth > 0) --depth;
            continue;
        }
        if (depth == 0) g += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    std::string key = (marked ? "I:" : "H:") + g;
    if (dims > 1) key += "#" + std::to_string(dim) + "/" + std::to_string(dims);
    return key;
}

const Group* Model::group(std::string_view key) const {
    for (const auto& g : groups)
        if (g.key == key) return &g;
    return nullptr;
}

int Model::groupIndex(std::string_view key) const {
    for (std::size_t i = 0; i < groups.size(); ++i)
        if (groups[i].key == key) return static_cast<int>(i);
    return -1;
}

Model modelOf(const Scan& s, std::string_view originalName, const BoundsFn& bounds) {
    Model m;
    std::vector<Group> made;
    std::map<std::string, std::size_t> at;
    const auto groupFor = [&](const std::string& key, GroupKind kind) -> std::size_t {
        if (const auto it = at.find(key); it != at.end()) return it->second;
        Group g;
        g.kind = kind;
        g.key = key;
        at.emplace(key, made.size());
        made.push_back(std::move(g));
        return made.size() - 1;
    };
    const auto indexGroup = [&](const std::string& path, int dim, int dims, bool marked) {
        const std::size_t gi = groupFor(depthKey(path, dim, dims, marked), GroupKind::Index);
        Group& g = made[gi];
        if (g.label.empty()) {
            const std::string d = dotted(path);
            g.label = d + slotBracket(dim, dims);
            const auto dot = d.rfind('.');
            g.tail = (dot == std::string::npos ? d : d.substr(dot + 1)) + slotBracket(dim, dims);
            g.depth = bracketsOf(path) + 1 + dim;
            g.dim = dim;
            g.dims = dims;
            g.marked = marked;
            if (bounds && dims == 1) g.bounds = bounds(path);
        }
        return gi;
    };
    m.markers.resize(s.markers.size());
    std::vector<std::string> areas;
    for (std::size_t mi = 0; mi < s.markers.size(); ++mi) {
        const auto& mk = s.markers[mi];
        auto& mm = m.markers[mi];
        const auto uses = indicesIn(mk.name);
        if (!uses.empty()) {
            mm.kind = GroupKind::Index;
            int deepest = -1;
            for (const auto& u : uses) {
                const std::size_t gi = indexGroup(u.path, u.dim, u.dims, true);
                Group& g = made[gi];
                addValue(g.values, u.value);
                if (g.markers.empty() || g.markers.back() != mi) {
                    g.markers.push_back(mi);
                    g.spots += mk.spots.size();
                }
                mm.slots.push_back({gi, u});
                if (g.depth >= deepest) {
                    deepest = g.depth;
                    mm.group = gi;
                }
            }
            continue;
        }
        if (auto a = addressOf(mk.name)) {
            mm.kind = GroupKind::Address;
            mm.group = groupFor("A", GroupKind::Address);
            if (std::find(areas.begin(), areas.end(), a->area) == areas.end()) areas.push_back(a->area);
            mm.address = std::move(a);
        } else if (!mk.variable) {
            mm.kind = GroupKind::Text;
            mm.group = groupFor("T", GroupKind::Text);
        } else {
            mm.kind = GroupKind::Variable;
            mm.group = groupFor("V", GroupKind::Variable);
        }
        Group& g = made[mm.group];
        if (g.original.empty()) g.original = mk.name;
        g.markers.push_back(mi);
        g.spots += mk.spots.size();
    }
    m.indexGroup.resize(s.indices.size());
    for (std::size_t ii = 0; ii < s.indices.size(); ++ii) {
        const auto& info = s.indices[ii];
        const std::size_t gi = indexGroup(info.path, info.dim, info.dims, false);
        Group& g = made[gi];
        addValue(g.values, info.value);
        g.indices.push_back(ii);
        g.spots += info.spots.size();
        if (!g.bounds && info.bounds) g.bounds = info.bounds;
        m.indexGroup[ii] = gi;
    }
    if (!originalName.empty()) {
        Group& g = made[groupFor("N", GroupKind::Name)];
        g.original = std::string(originalName);
    }
    // Les libelles des groupes qui ne sont pas des profondeurs.
    for (auto& g : made) {
        if (g.kind == GroupKind::Index) {
            g.original = valuesText(g.values);
            continue;
        }
        if (g.kind == GroupKind::Address) {
            g.label = "Adresses";
            for (std::size_t q = 0; q < areas.size(); ++q) g.label += (q ? ", " : " ") + areas[q];
            g.tail = "Adresses";
        } else if (g.kind == GroupKind::Text) g.label = g.tail = "Textes";
        else if (g.kind == GroupKind::Variable) g.label = g.tail = "Variables";
        else g.label = g.tail = "Noms des objets";
    }
    // L'ordre : les profondeurs (1, 2, 3... ; dans un repere, puis hors repere), puis A, T, V, N.
    std::vector<std::size_t> order(made.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto rank = [&](const Group& g) {
        switch (g.kind) {
        case GroupKind::Index: return 0;
        case GroupKind::Address: return 1;
        case GroupKind::Text: return 2;
        case GroupKind::Variable: return 3;
        default: return 4;
        }
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const Group &x = made[a], &y = made[b];
        if (rank(x) != rank(y)) return rank(x) < rank(y);
        if (x.kind != GroupKind::Index) return false;
        if (x.marked != y.marked) return x.marked;
        return x.depth < y.depth;
    });
    std::vector<std::size_t> to(made.size());
    for (std::size_t i = 0; i < order.size(); ++i) to[order[i]] = i;
    m.groups.reserve(made.size());
    for (const std::size_t i : order) m.groups.push_back(std::move(made[i]));
    for (auto& mm : m.markers) {
        mm.group = to[mm.group];
        for (auto& sl : mm.slots) sl.group = to[sl.group];
    }
    for (auto& gi : m.indexGroup) gi = to[gi];
    return m;
}

// --------------------------------------------------------------- regles ---
const Rule& Rules::rule(const Group& g) const {
    static const Rule kFixed{};
    const auto it = groups.find(g.key);
    return it == groups.end() ? kFixed : it->second;
}

Own Rules::ownOf(std::string_view markerKey) const {
    const auto it = own.find(std::string(markerKey));
    return it == own.end() ? Own{} : it->second;
}

Rules defaultRules(const Scan& s, const Model& m, const Counters& c) {
    Rules r;
    r.counters = c;
    const bool series = c.mode == CountMode::Series;
    const bool followOutside = followByDefault(s);
    const auto letter = [&](const Group& g) -> Rule {
        const char* l = letterOf(g.depth);
        if (l && static_cast<std::size_t>(g.depth) <= c.grid.size()) return {RuleKind::Formula, l};
        return {RuleKind::Fixed, {}};
    };
    for (const auto& g : m.groups) {
        Rule rule;
        switch (g.kind) {
        case GroupKind::Index:
            if (g.marked) rule = series ? Rule{RuleKind::Formula, "o + n"} : letter(g);
            else if (!followOutside) rule = {RuleKind::Fixed, {}};
            else if (series) {
                // Comme defaultPlan : suivi si chaque indice tient dans ses bornes (sans copie : pour une).
                const bool fits = std::all_of(g.indices.begin(), g.indices.end(),
                                              [&](std::size_t ii) { return followFits(s.indices[ii], std::max(1, c.copies)); });
                rule = fits ? Rule{RuleKind::Formula, "o + n"} : Rule{RuleKind::Fixed, {}};
            } else rule = letter(g);
            break;
        case GroupKind::Address: {
            // Une zone (et un bit) pour toutes : "%MW(o + n)" ; sinon "(o + n)" garde celle de chacune.
            std::string area, rest;
            bool same = true;
            for (const std::size_t mi : g.markers) {
                const auto& a = m.markers[mi].address;
                if (!a) continue;
                if (area.empty()) area = a->area, rest = a->rest;
                else same = same && upper(area) == upper(a->area) && rest == a->rest;
            }
            rule = {RuleKind::Formula, same && !area.empty() ? area + "(o + n)" + rest : std::string("(o + n)")};
            break;
        }
        case GroupKind::Text:
        case GroupKind::Variable: rule = {RuleKind::Empty, {}}; break;
        case GroupKind::Name: rule = {RuleKind::Auto, {}}; break;
        }
        r.groups[g.key] = rule;
    }
    return r;
}

std::vector<Range> gridFromBounds(const Model& m, int counters) {
    std::vector<Range> out;
    for (int d = 1; d <= std::clamp(counters, 1, 3); ++d) {
        const Group* best = nullptr;
        for (const auto& g : m.groups)
            if (g.kind == GroupKind::Index && g.depth == d && (!best || (g.marked && !best->marked))) best = &g;
        Range r;
        if (best && !best->values.empty()) {
            r.from = best->values.front();
            r.to = best->bounds && best->bounds->second > r.from ? best->bounds->second : r.from + 1;
        } else r.to = 1;
        out.push_back(r);
    }
    return out;
}

// -------------------------------------------------------------- le calcul ---
namespace {

// Le calcul des valeurs, ses formules gardees : une profondeur et une valeur d'origine donnent la meme suite pour
// tous les reperes qui la partagent (448 reperes de Departs[0] : un seul calcul par copie).
class Evaluator {
public:
    Evaluator(const Scan& s, const Model& m, const Rules& r) : s_(s), m_(m), r_(r) {}

    // La regle d'une profondeur pour un repere : la sienne (a part, cette profondeur) ou celle du groupe ; `id` : la cle du cache.
    struct Pick {
        Rule rule;
        long long id{0};
    };
    Pick slotRule(std::size_t mi, std::size_t group, const Own& own) const {
        const Group& g = m_.groups[group];
        if (own.kind == OwnKind::Formula && own.group == g.key) return {{RuleKind::Formula, own.formula}, -1 - static_cast<long long>(mi)};
        return {r_.rule(g), static_cast<long long>(group)};
    }

    // Un indice a la place `p` ; faux et `why` : la formule est fausse.
    bool index(const Pick& pick, long long o, long long p, long long& out, std::string* why) {
        if (pick.rule.kind != RuleKind::Formula) {
            out = o;
            return true;
        }
        const auto key = std::make_tuple(pick.id, o, p);
        if (const auto it = cache_.find(key); it != cache_.end()) {
            out = it->second.first;
            if (!it->second.second.empty() && why) *why = it->second.second;
            return it->second.second.empty();
        }
        std::string err;
        long long v = 0;
        const bool ok = evalInteger(pick.rule.formula, r_.counters, {placeAt(r_.counters, p), o}, v, &err);
        cache_.emplace(key, std::make_pair(v, ok ? std::string{} : (err.empty() ? std::string("formule fausse") : err)));
        out = v;
        if (!ok && why) *why = err;
        return ok;
    }

    // La valeur d'un repere a la place `p` ; `keep` : le repere se garde (fixe) ; vide et `why` : une formule fausse.
    std::string marker(std::size_t mi, long long p, const Own& own, bool& keep, std::string* why) {
        keep = false;
        const auto& mk = s_.markers[mi];
        const auto& mm = m_.markers[mi];
        if (own.kind == OwnKind::Keep) {
            keep = true;
            return {};
        }
        if (mm.kind == GroupKind::Index) {
            std::string out;
            std::size_t from = 0;
            bool moved = false;
            for (const auto& sl : mm.slots) {
                const Pick pick = slotRule(mi, sl.group, own);
                long long v = sl.use.value;
                if (!index(pick, sl.use.value, p, v, why)) return {};
                if (pick.rule.kind == RuleKind::Formula) moved = true;
                out.append(mk.name, from, sl.use.at - from);
                out += std::to_string(v);
                from = sl.use.at + sl.use.length;
            }
            out.append(mk.name, from, std::string::npos);
            (void)moved;
            return out;   // toutes fixes : le morceau lui-meme (la copie le garde, sans le compter)
        }
        const Group& g = m_.groups[mm.group];
        const Rule rule = own.kind == OwnKind::Formula ? Rule{RuleKind::Formula, own.formula} : r_.rule(g);
        if (rule.kind == RuleKind::Fixed) {
            keep = true;
            return {};
        }
        if (rule.kind != RuleKind::Formula) return {};   // a remplir
        const Place at = placeAt(r_.counters, p);
        if (mm.kind == GroupKind::Address)
            return evalAddress(rule.formula, mk.name, r_.counters, {at, mm.address ? mm.address->number : 0}, why);
        // Un texte, une variable : la meme pour tous les reperes du groupe (sauf a part).
        const long long id = own.kind == OwnKind::Formula ? -1 - static_cast<long long>(mi) : static_cast<long long>(mm.group);
        const auto key = std::make_pair(id, p);
        if (const auto it = texts_.find(key); it != texts_.end()) {
            if (!it->second.second.empty() && why) *why = it->second.second;
            return it->second.first;
        }
        std::string err;
        std::string v = evalText(rule.formula, r_.counters, {at, 0}, &err);
        if (!err.empty()) v.clear();
        texts_.emplace(key, std::make_pair(v, err));
        if (!err.empty() && why) *why = err;
        return v;
    }

private:
    const Scan&  s_;
    const Model& m_;
    const Rules& r_;
    std::map<std::tuple<long long, long long, long long>, std::pair<long long, std::string>> cache_;
    std::map<std::pair<long long, long long>, std::pair<std::string, std::string>>         texts_;
};

void note(std::vector<RuleError>* errors, const std::string& where, const std::string& why, long long place) {
    if (!errors || why.empty()) return;
    for (const auto& e : *errors)
        if (e.where == where) return;
    errors->push_back({where, why, place});
}

} // namespace

void computeRows(const Scan& s, const Model& m, const Rules& r, const std::vector<std::string>& names, Plan& plan,
                 std::vector<std::string>* keep, std::vector<RuleError>* errors) {
    long long places = r.counters.places();
    if (places > kMaxCopies + 1) {
        note(errors, "Les copies", std::to_string(places - 1) + " copies : " + std::to_string(kMaxCopies) + " au plus", 0);
        places = kMaxCopies + 1;
    }
    if (places < 1) {
        note(errors, "Les copies", "un compteur est vide (de ... \xC3\xA0 ... : la fin avant le d\xC3\xA9" "but)", 0);
        places = 1;
    }
    const auto P = static_cast<std::size_t>(places);
    plan.rows.resize(P);
    Evaluator ev(s, m, r);
    // Les noms.
    if (const Group* ng = m.group("N"); ng && !r.namesByHand) {
        const Rule& nr = r.rule(*ng);
        for (std::size_t p = 0; p < P; ++p) {
            auto& name = plan.rows[p].name;
            if (p == 0) {
                name = ng->original;
                continue;
            }
            if (nr.kind == RuleKind::Auto) name = p < names.size() ? names[p] : std::string{};
            else if (nr.kind == RuleKind::Formula) {
                std::string why;
                name = evalText(nr.formula, r.counters, {placeAt(r.counters, static_cast<long long>(p)), 0}, &why);
                note(errors, ng->label, why, static_cast<long long>(p));
            } else name.clear();   // fixe : un nom libre
        }
    }
    // Les indices hors repere.
    for (std::size_t p = 0; p < P; ++p) plan.rows[p].indices.clear();
    for (std::size_t gi = 0; gi < m.groups.size(); ++gi) {
        const Group& g = m.groups[gi];
        if (g.kind != GroupKind::Index || g.marked) continue;
        const Rule& rule = r.rule(g);
        if (rule.kind != RuleKind::Formula) continue;
        const Evaluator::Pick pick{rule, static_cast<long long>(gi)};
        for (const std::size_t ii : g.indices) {
            const auto& info = s.indices[ii];
            for (std::size_t p = 1; p < P; ++p) {
                long long v = info.value;
                std::string why;
                if (ev.index(pick, info.value, static_cast<long long>(p), v, &why)) plan.rows[p].indices[info.key()] = v;
                else note(errors, g.label, why, static_cast<long long>(p));
            }
        }
    }
    // Les reperes.
    for (std::size_t mi = 0; mi < s.markers.size(); ++mi) {
        const std::string key = markerKey(s.markers[mi].name);
        const Own own = r.ownOf(key);
        if (own.kind == OwnKind::Cells) continue;   // ses cases : les regles n'y touchent pas
        bool kept = false;
        for (std::size_t p = 0; p < P; ++p) {
            std::string why;
            bool k = false;
            std::string v = ev.marker(mi, static_cast<long long>(p), own, k, &why);
            kept = kept || k;
            if (!why.empty()) note(errors, "$" + s.markers[mi].name + "$", why, static_cast<long long>(p));
            if (v.empty()) plan.rows[p].markers.erase(key);
            else plan.rows[p].markers[key] = std::move(v);
        }
        if (kept && keep && std::find(keep->begin(), keep->end(), key) == keep->end()) keep->push_back(key);
    }
}

std::string ruleValue(const Scan& s, const Model& m, const Rules& r, std::size_t marker, long long place, std::string* why) {
    if (marker >= s.markers.size()) return {};
    Own own = r.ownOf(markerKey(s.markers[marker].name));
    if (own.kind == OwnKind::Cells) own = Own{};   // ses cases : ce que dirait son groupe
    Evaluator ev(s, m, r);
    bool keep = false;
    return ev.marker(marker, place, own, keep, why);
}

// --------------------------------------------------------- les adresses ---
std::map<std::string, std::string> projectAddresses(const Project& p) {
    std::map<std::string, std::string> out;
    const auto norm = [](std::string_view a) {
        std::string t;
        for (const char c : a)
            if (!std::isspace(static_cast<unsigned char>(c))) t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return t;
    };
    for (const auto& v : p.programs.variables) {
        if (v.address.empty()) continue;
        if (types::isComposite(v.type)) {
            for (const auto& leaf : types::leafVariables(p, v))
                if (!leaf.address.empty()) out.emplace(norm(leaf.address), leaf.name);
        } else
            out.emplace(norm(v.address), v.name);
    }
    return out;
}

std::vector<CellCheck> addressWarnings(const Scan& s, const Model& m, const Plan& plan, const std::map<std::string, std::string>& owners) {
    std::vector<CellCheck> out;
    const auto norm = [](std::string_view a) {
        std::string t;
        for (const char c : a)
            if (!std::isspace(static_cast<unsigned char>(c))) t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return t;
    };
    // Les adresses de l'original (ses reperes d'adresse) : une copie qui en reprend une avertit aussi.
    std::map<std::string, std::string> given;   // adresse -> qui l'a deja (une ligne, l'original)
    for (std::size_t mi = 0; mi < s.markers.size(); ++mi)
        if (m.markers[mi].kind == GroupKind::Address) given.emplace(norm(s.markers[mi].name), "l'original ($" + s.markers[mi].name + "$)");
    for (std::size_t r = 1; r < plan.rows.size(); ++r)
        for (std::size_t mi = 0; mi < s.markers.size(); ++mi) {
            if (m.markers[mi].kind != GroupKind::Address) continue;
            const auto it = plan.rows[r].markers.find(markerKey(s.markers[mi].name));
            if (it == plan.rows[r].markers.end() || it->second.empty()) continue;
            const std::string a = norm(it->second);
            const std::string column = "$" + s.markers[mi].name + "$";
            if (const auto o = owners.find(a); o != owners.end())
                out.push_back({static_cast<int>(r), column, CellState::Ok, it->second + " d\xC3\xA9j\xC3\xA0 prise par " + o->second, {}, false});
            else if (const auto g = given.find(a); g != given.end())
                out.push_back({static_cast<int>(r), column, CellState::Ok, it->second + " d\xC3\xA9j\xC3\xA0 donn\xC3\xA9" "e \xC3\xA0 " + g->second, {}, false});
            else
                given.emplace(a, "la ligne " + std::to_string(r + 1) + " (" + column + ")");
        }
    return out;
}

std::size_t replacements(const Scan& s, const Plan& plan) {
    std::size_t n = 0;
    std::vector<std::string> keys;
    keys.reserve(s.markers.size());
    for (const auto& mk : s.markers) keys.push_back(markerKey(mk.name));
    for (std::size_t r = 1; r < plan.rows.size(); ++r)
        for (std::size_t mi = 0; mi < s.markers.size(); ++mi) {
            const auto it = plan.rows[r].markers.find(keys[mi]);
            if (it != plan.rows[r].markers.end() && !it->second.empty() && it->second != s.markers[mi].name) n += s.markers[mi].uses;
        }
    return n;
}

} // namespace hmi::dup
